#include "audioanalysis.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <algorithm>

namespace {
constexpr quint32 Magic = 0x4d584556; // "MXEV"
constexpr quint16 Version = 1;
// A mix longer than this is still worth levelling, but its meters would cost
// more to keep than they are worth.
constexpr qint64 LongestSeconds = 4 * 60 * 60;
constexpr int KeptInMemory = 4;
} // namespace

std::array<double, Envelope::Bands> Envelope::at(qint64 positionMs) const {
  std::array<double, Bands> out{};
  if (!isValid() || positionMs < 0)
    return out;
  const qint64 frame = positionMs * FramesPerSecond / 1000;
  if (frame >= frames())
    return out;
  const auto *row = reinterpret_cast<const quint8 *>(levels.constData()) + frame * Bands;
  for (int band = 0; band < Bands; ++band)
    out[band] = row[band] / 100.0;
  return out;
}

QVariantList Envelope::toList(const std::array<double, Bands> &levels) {
  QVariantList list;
  for (double v : levels)
    list.append(v);
  return list;
}

QVariantList Envelope::mix(const std::array<double, Bands> &a, double weightA, const std::array<double, Bands> &b, double weightB) {
  std::array<double, Bands> out{};
  const double total = weightA + weightB;
  if (total <= 0)
    return toList(out);
  // The meters are in dB-like units already, so the quieter deck scales down
  // rather than adding; the louder band is the one heard.
  const double wa = weightA / total, wb = weightB / total;
  for (int band = 0; band < Bands; ++band)
    out[band] = std::round(std::max(a[band] * std::min(1.0, 2 * wa), b[band] * std::min(1.0, 2 * wb)) * 100) / 100;
  return toList(out);
}

void LoudnessIntegrator::configure(int sampleRate, int channelCount) {
  rate = sampleRate;
  channels = std::min(8, channelCount);
  state = {};
  hops.clear();
  hopSum = 0;
  filled = 0;
  hop = std::max(1, rate / 10);
  // BS.1770's two K-weighting stages, designed for any rate the way
  // libebur128 does rather than using the 48 kHz coefficients as printed.
  const double pi = 3.14159265358979323846;
  {
    const double f0 = 1681.974450955533, gain = 3.999843853973347, q = 0.7071752369554196;
    const double k = std::tan(pi * f0 / rate), vh = std::pow(10.0, gain / 20.0), vb = std::pow(vh, 0.4996667741545416);
    const double a0 = 1.0 + k / q + k * k;
    shelf = {(vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
             2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0};
  }
  {
    const double f0 = 38.13547087602444, q = 0.5003270373238773;
    const double k = std::tan(pi * f0 / rate), a0 = 1.0 + k / q + k * k;
    highpass = {1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0};
  }
}

void LoudnessIntegrator::process(const float *interleaved, qsizetype frames) {
  if (rate <= 0)
    return;
  for (qsizetype frame = 0; frame < frames; ++frame) {
    for (int c = 0; c < channels; ++c) {
      double x = interleaved[frame * channels + c];
      if (!std::isfinite(x))
        x = 0;
      auto &s = state[c];
      // Transposed direct form II, one stage after the other.
      const double y1 = shelf.b0 * x + s[0];
      s[0] = shelf.b1 * x - shelf.a1 * y1 + s[1];
      s[1] = shelf.b2 * x - shelf.a2 * y1;
      const double y2 = highpass.b0 * y1 + s[2];
      s[2] = highpass.b1 * y1 - highpass.a1 * y2 + s[3];
      s[3] = highpass.b2 * y1 - highpass.a2 * y2;
      hopSum += y2 * y2;
    }
    if (++filled == hop) {
      hops.push_back(hopSum / hop);
      hopSum = 0;
      filled = 0;
    }
  }
  for (auto &s : state)
    for (double &z : s)
      if (std::abs(z) < 1e-20)
        z = 0;
}

double LoudnessIntegrator::loudness() const {
  if (hops.size() < 4)
    return std::nan("");
  std::vector<double> blocks;
  blocks.reserve(hops.size());
  for (size_t i = 3; i < hops.size(); ++i)
    blocks.push_back((hops[i - 3] + hops[i - 2] + hops[i - 1] + hops[i]) / 4.0);
  auto lufs = [](double power) { return -0.691 + 10.0 * std::log10(std::max(power, 1e-20)); };
  double sum = 0;
  int count = 0;
  for (double p : blocks)
    if (lufs(p) > -70.0) {
      sum += p;
      ++count;
    }
  if (!count)
    return std::nan("");
  const double relative = lufs(sum / count) - 10.0;
  sum = 0;
  count = 0;
  for (double p : blocks)
    if (lufs(p) > -70.0 && lufs(p) > relative) {
      sum += p;
      ++count;
    }
  return count ? lufs(sum / count) : std::nan("");
}

EnvelopeBuilder::EnvelopeBuilder(int sampleRate, int channels)
    : m_rate(sampleRate), m_channels(channels), m_frameSize(std::max(1, sampleRate / Envelope::FramesPerSecond)) {
  m_levels.configure(sampleRate, channels);
  m_loudness.configure(sampleRate, channels);
}

void EnvelopeBuilder::feed(const float *interleaved, qsizetype frames) {
  m_loudness.process(interleaved, frames);
  qsizetype offset = 0;
  while (offset < frames) {
    const qsizetype take = std::min<qsizetype>(frames - offset, m_frameSize - m_pending);
    m_levels.process(interleaved + offset * m_channels, take, m_channels, m_rate);
    m_pending += int(take);
    offset += take;
    if (m_pending == m_frameSize) {
      for (const auto &v : m_levels.takeLevels())
        m_out.append(char(quint8(std::lround(std::clamp(v.toDouble(), 0.0, 1.0) * 100))));
      m_pending = 0;
    }
  }
}

Envelope EnvelopeBuilder::finish() {
  if (m_pending > m_frameSize / 2)
    for (const auto &v : m_levels.takeLevels())
      m_out.append(char(quint8(std::lround(std::clamp(v.toDouble(), 0.0, 1.0) * 100))));
  m_pending = 0;
  Envelope envelope;
  envelope.levels = m_out;
  envelope.loudness = m_loudness.loudness();
  return envelope;
}

LevellingGain levellingGain(double tagDb, double analysedLufs, double measuredDb) {
  // Every source is expressed against the same -18 reference: ReplayGain's,
  // and the one R128 tags are converted to.
  if (!std::isnan(tagDb))
    return {tagDb, "Track tag"};
  if (!std::isnan(analysedLufs))
    return {-18.0 - analysedLufs, "Analysed"};
  if (!std::isnan(measuredDb))
    return {-18.0 - measuredDb, "Measured"};
  return {};
}

AudioAnalysis::AudioAnalysis(QObject *parent) : QObject(parent) {}

AudioAnalysis::~AudioAnalysis() {
  if (m_cancel)
    m_cancel->store(true);
  if (m_thread) {
    m_thread->wait(5000);
    delete m_thread;
  }
}

void AudioAnalysis::setCacheDirectory(const QString &directory) {
  m_cacheDir = directory;
  QDir().mkpath(directory);
}

QString AudioAnalysis::pathFor(const QString &id) const {
  // Ids carry paths and URLs; the file name only needs to be stable.
  return m_cacheDir + "/" + QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha1).toHex()) + ".env";
}

void AudioAnalysis::remember(const QString &id, const Envelope &envelope) {
  m_memory.insert(id, envelope);
  m_memoryOrder.removeAll(id);
  m_memoryOrder.append(id);
  while (m_memoryOrder.size() > KeptInMemory)
    m_memory.remove(m_memoryOrder.takeFirst());
}

Envelope AudioAnalysis::envelope(const QString &id) {
  if (id.isEmpty())
    return {};
  if (const auto found = m_memory.constFind(id); found != m_memory.constEnd())
    return *found;
  if (m_cacheDir.isEmpty())
    return {};
  const auto path = pathFor(id);
  auto envelope = read(path);
  if (envelope.isValid()) {
    // Reading counts as use, which is what keeps it in the cache.
    QFile file(path);
    if (file.open(QIODevice::ReadWrite))
      file.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
    remember(id, envelope);
  }
  return envelope;
}

bool AudioAnalysis::pending(const QString &id) const {
  if (m_running == id)
    return true;
  return std::any_of(m_queue.cbegin(), m_queue.cend(), [&](const Job &job) { return job.id == id; });
}

void AudioAnalysis::request(const QString &id, const QString &file, bool urgent) {
  if (id.isEmpty() || file.isEmpty() || m_ffmpeg.isEmpty() || m_failed.contains(id) || pending(id))
    return;
  if (envelope(id).isValid())
    return;
  if (urgent)
    m_queue.prepend({id, file});
  else
    m_queue.append({id, file});
  // Only what is playing and what is next are ever worth the work.
  while (m_queue.size() > 3)
    m_queue.removeLast();
  startNext();
}

void AudioAnalysis::startNext() {
  if (m_thread || m_queue.isEmpty())
    return;
  const auto job = m_queue.takeFirst();
  m_running = job.id;
  auto cancel = std::make_shared<std::atomic_bool>(false);
  m_cancel = cancel;
  auto result = std::make_shared<Envelope>();
  const auto ffmpeg = m_ffmpeg, file = job.file;
  m_thread = QThread::create([ffmpeg, file, cancel, result] { *result = decode(ffmpeg, file, cancel.get()); });
  connect(m_thread, &QThread::finished, this, [this, job, cancel, result] {
    m_thread->deleteLater();
    m_thread = nullptr;
    m_running.clear();
    if (!cancel->load()) {
      if (result->isValid()) {
        if (!m_cacheDir.isEmpty() && write(pathFor(job.id), *result))
          trim();
        remember(job.id, *result);
        emit ready(job.id);
      } else {
        // A file ffmpeg cannot read will not read better next time.
        m_failed.append(job.id);
        if (m_failed.size() > 200)
          m_failed.removeFirst();
        emit failed(job.id);
      }
    }
    startNext();
  });
  m_thread->start(QThread::LowPriority);
}

Envelope AudioAnalysis::decode(const QString &ffmpeg, const QString &file, const std::atomic_bool *cancel) {
  if (!QFileInfo(file).isFile())
    return {};
  QProcess process;
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.setStandardErrorFile(QProcess::nullDevice());
  process.start(ffmpeg, {"-nostdin", "-v", "error", "-i", file, "-map", "0:a:0", "-vn", "-sn", "-dn",
                         "-ac", QString::number(Channels), "-ar", QString::number(SampleRate),
                         "-c:a", "pcm_f32le", "-f", "f32le", "pipe:1"});
  if (!process.waitForStarted(5000))
    return {};
  EnvelopeBuilder builder(SampleRate, Channels);
  constexpr qsizetype FrameBytes = sizeof(float) * Channels;
  const qint64 limit = LongestSeconds * SampleRate;
  qint64 frames = 0;
  QByteArray carry;
  auto consume = [&](const QByteArray &bytes) {
    carry += bytes;
    const qsizetype whole = carry.size() / FrameBytes;
    if (whole > 0 && frames < limit) {
      const qsizetype take = std::min<qsizetype>(whole, limit - frames);
      builder.feed(reinterpret_cast<const float *>(carry.constData()), take);
      frames += take;
    }
    carry.remove(0, whole * FrameBytes);
  };
  while (true) {
    if (cancel && cancel->load()) {
      process.kill();
      process.waitForFinished(2000);
      return {};
    }
    if (process.waitForReadyRead(250))
      consume(process.readAllStandardOutput());
    else if (process.state() == QProcess::NotRunning)
      break;
  }
  consume(process.readAllStandardOutput());
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || frames < SampleRate / 2)
    return {};
  return builder.finish();
}

bool AudioAnalysis::write(const QString &path, const Envelope &envelope) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly))
    return false;
  QDataStream out(&file);
  out.setVersion(QDataStream::Qt_6_5);
  out << Magic << Version << quint16(Envelope::FramesPerSecond) << quint8(Envelope::Bands) << envelope.loudness << envelope.levels;
  return out.status() == QDataStream::Ok && file.commit();
}

Envelope AudioAnalysis::read(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  QDataStream in(&file);
  in.setVersion(QDataStream::Qt_6_5);
  quint32 magic = 0;
  quint16 version = 0, fps = 0;
  quint8 bands = 0;
  Envelope envelope;
  in >> magic >> version >> fps >> bands >> envelope.loudness >> envelope.levels;
  // Anything written by another layout is simply analysed again.
  if (in.status() != QDataStream::Ok || magic != Magic || version != Version || fps != Envelope::FramesPerSecond ||
      bands != Envelope::Bands || envelope.levels.size() % Envelope::Bands)
    return {};
  return envelope;
}

void AudioAnalysis::trim() {
  QDir dir(m_cacheDir);
  auto files = dir.entryInfoList({"*.env"}, QDir::Files, QDir::Time | QDir::Reversed);
  qint64 total = 0;
  for (const auto &f : files)
    total += f.size();
  for (const auto &f : files) {
    if (total <= m_cacheLimit)
      break;
    total -= f.size();
    QFile::remove(f.absoluteFilePath());
  }
}

void AudioAnalysis::clearCache() {
  if (m_cancel)
    m_cancel->store(true);
  m_queue.clear();
  m_memory.clear();
  m_memoryOrder.clear();
  m_failed.clear();
  if (!m_cacheDir.isEmpty()) {
    QDir(m_cacheDir).removeRecursively();
    QDir().mkpath(m_cacheDir);
  }
}
