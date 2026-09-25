#include "audioanalysis.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <vector>

// Analysis ahead of playback: what the envelope measures, how a position reads
// it, how loud it says a recording is, and where it keeps what it worked out.
namespace {
constexpr int Rate = AudioAnalysis::SampleRate;
constexpr double Pi = 3.14159265358979323846;

std::vector<float> tone(double hz, double dbfs, double seconds, int rate = Rate) {
  const double amplitude = std::pow(10.0, dbfs / 20.0);
  const qsizetype frames = qsizetype(seconds * rate);
  std::vector<float> out(size_t(frames) * 2);
  for (qsizetype i = 0; i < frames; ++i)
    out[size_t(i) * 2] = out[size_t(i) * 2 + 1] = float(amplitude * std::sin(2 * Pi * hz * double(i) / rate));
  return out;
}

Envelope build(const std::vector<float> &samples) {
  EnvelopeBuilder builder(Rate, 2);
  // Fed in uneven pieces, as a pipe delivers them.
  qsizetype offset = 0, frames = qsizetype(samples.size() / 2), step = 997;
  while (offset < frames) {
    const auto take = std::min(step, frames - offset);
    builder.feed(samples.data() + offset * 2, take);
    offset += take;
    step = step == 997 ? 4099 : 997;
  }
  return builder.finish();
}

std::array<double, 5> average(const Envelope &e) {
  std::array<double, 5> sum{};
  for (int f = 0; f < e.frames(); ++f)
    for (int b = 0; b < 5; ++b)
      sum[b] += quint8(e.levels[f * 5 + b]);
  for (double &v : sum)
    v /= std::max(1, e.frames());
  return sum;
}

// A plain 16-bit stereo WAV, so the end-to-end check needs no fixtures.
bool writeWav(const QString &path, const std::vector<float> &samples, int rate) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly))
    return false;
  const quint32 data = quint32(samples.size() * 2);
  QDataStream out(&file);
  out.setByteOrder(QDataStream::LittleEndian);
  out.writeRawData("RIFF", 4);
  out << quint32(36 + data);
  out.writeRawData("WAVEfmt ", 8);
  out << quint32(16) << quint16(1) << quint16(2) << quint32(rate) << quint32(rate * 4) << quint16(4) << quint16(16);
  out.writeRawData("data", 4);
  out << data;
  for (float v : samples)
    out << qint16(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767));
  return out.status() == QDataStream::Ok;
}

// An ffmpeg that can write raw float audio: the packaging build's own, or one
// the developer names or has installed.
QString ffmpegWithFloatOutput() {
  QStringList candidates{qEnvironmentVariable("MUSIX_TEST_FFMPEG"),
                         QStringLiteral(MUSIX_SOURCE_DIR "/build-packaging/ffmpeg-out/bin/ffmpeg"),
                         QStandardPaths::findExecutable("ffmpeg")};
  for (const auto &c : candidates) {
    if (c.isEmpty() || !QFileInfo(c).isExecutable())
      continue;
    QProcess p;
    p.start(c, {"-hide_banner", "-encoders"});
    if (p.waitForFinished(10000) && p.readAllStandardOutput().contains("pcm_f32le"))
      return c;
  }
  return {};
}
} // namespace

class AnalysisTest : public QObject {
  Q_OBJECT
private slots:
  void aToneLandsInItsBand() {
    const auto low = average(build(tone(100, -12, 2)));
    QVERIFY2(low[0] > low[2] + 10 && low[0] > low[3] + 10, "100 Hz belongs to the lowest band");
    const auto high = average(build(tone(3500, -12, 2)));
    QVERIFY2(high[3] > high[0] + 10 && high[3] > high[1] + 10, "3.5 kHz belongs to the fourth band");
  }

  void framesFollowDuration() {
    const auto e = build(tone(440, -18, 10));
    QCOMPARE(e.frames(), 10 * Envelope::FramesPerSecond);
    QVERIFY(build(tone(440, -18, 0)).frames() == 0);
  }

  void theLiveMeterAndTheFileAgree() {
    // The same filters behind both: a thirtieth of a second measured as a
    // decoded buffer reads exactly what the envelope's first frame holds.
    const auto samples = tone(1200, -15, 1);
    const int frame = Rate / Envelope::FramesPerSecond;
    AudioLevels live;
    live.process(samples.data(), frame, 2, Rate);
    const auto direct = live.takeLevels();
    const auto e = build(samples);
    for (int band = 0; band < 5; ++band)
      QCOMPARE(e.at(0)[band], direct[band].toDouble());
    QVERIFY(direct[2].toDouble() > 0.5);
  }

  void positionPicksTheFrame() {
    Envelope e;
    e.levels = QByteArray("\x0a\x14\x1e\x28\x32" "\x3c\x46\x50\x5a\x64" "\x00\x00\x00\x00\x00", 15);
    QCOMPARE(e.frames(), 3);
    QCOMPARE(e.at(0)[0], 0.10);
    QCOMPARE(e.at(33)[4], 0.50);  // still the first thirtieth of a second
    QCOMPARE(e.at(34)[0], 0.60);  // the second frame
    QCOMPARE(e.at(66)[4], 1.00);
    QCOMPARE(e.at(67)[0], 0.00);
    QCOMPARE(e.at(100)[0], 0.00); // past the end: silence
    QCOMPARE(e.at(-5)[0], 0.00);
    // A position is a position: speed and pauses are the player's business.
    QCOMPARE(e.at(40), e.at(40));
  }

  void anOverlapMixesBothDecks() {
    const std::array<double, 5> a{0.8, 0.8, 0.8, 0.8, 0.8}, b{0.2, 0.4, 0.6, 0.9, 1.0}, zero{};
    QCOMPARE(Envelope::mix(a, 1, b, 0), Envelope::toList(a));
    QCOMPARE(Envelope::mix(a, 0, b, 1), Envelope::toList(b));
    // Halfway through an equal-power fade both are heard in full.
    const auto middle = Envelope::mix(a, std::cos(Pi / 4), b, std::sin(Pi / 4));
    QCOMPARE(middle[0].toDouble(), 0.8);
    QCOMPARE(middle[4].toDouble(), 1.0);
    // Near the end the outgoing song has faded down.
    const auto late = Envelope::mix(a, std::cos(Pi * 0.45), zero, std::sin(Pi * 0.45));
    QVERIFY(late[0].toDouble() < 0.3);
    QCOMPARE(Envelope::mix(a, 0, b, 0), Envelope::toList(zero));
  }

  void loudnessOfKnownSignals() {
    // BS.1770 is calibrated so a 1 kHz sine in both channels reads its level.
    QVERIFY(qAbs(build(tone(1000, -20, 10)).loudness - -20.0) < 0.3);
    QVERIFY(qAbs(build(tone(1000, -30, 10)).loudness - -30.0) < 0.3);
    // K-weighting: a low tone counts for less than its level.
    QVERIFY(build(tone(40, -20, 10)).loudness < -21.0);
  }

  void silenceIsGatedOut() {
    auto samples = tone(1000, -20, 5);
    samples.resize(samples.size() + size_t(20 * Rate * 2), 0.0f);
    QVERIFY(qAbs(build(samples).loudness - -20.0) < 0.3);
    QVERIFY(std::isnan(build(std::vector<float>(size_t(Rate * 2 * 3), 0.0f)).loudness));
  }

  void levellingPrefersTagsThenAnalysis() {
    const double nan = std::nan("");
    auto g = levellingGain(-3.5, -9, -12);
    QCOMPARE(g.source, QString("Track tag"));
    QCOMPARE(g.db, -3.5);
    g = levellingGain(nan, -9, -12);
    QCOMPARE(g.source, QString("Analysed"));
    QCOMPARE(g.db, -9.0); // -18 - -9
    g = levellingGain(nan, nan, -12);
    QCOMPARE(g.source, QString("Measured"));
    QCOMPARE(g.db, -6.0);
    QVERIFY(levellingGain(nan, nan, nan).source.isEmpty());
  }

  void envelopesSurviveOnDisk() {
    QTemporaryDir dir;
    const auto e = build(tone(440, -18, 3));
    const auto path = dir.filePath("a.env");
    QVERIFY(AudioAnalysis::write(path, e));
    const auto back = AudioAnalysis::read(path);
    QCOMPARE(back.levels, e.levels);
    QCOMPARE(back.loudness, e.loudness);
    // Anything else in its place is ignored, never trusted.
    QFile junk(dir.filePath("b.env"));
    QVERIFY(junk.open(QIODevice::WriteOnly));
    junk.write("not an envelope");
    junk.close();
    QVERIFY(!AudioAnalysis::read(dir.filePath("b.env")).isValid());
  }

  void theCacheDropsTheLeastRecentlyUsed() {
    QTemporaryDir dir;
    AudioAnalysis analysis;
    analysis.setCacheDirectory(dir.path());
    const auto e = build(tone(440, -18, 10));
    const auto now = QDateTime::currentDateTimeUtc();
    const QStringList ids{"oldest", "middle", "newest"};
    for (int i = 0; i < ids.size(); ++i) {
      QVERIFY(AudioAnalysis::write(analysis.pathFor(ids[i]), e));
      QFile f(analysis.pathFor(ids[i]));
      QVERIFY(f.open(QIODevice::ReadWrite));
      f.setFileTime(now.addSecs(-100 + i * 10), QFileDevice::FileModificationTime);
    }
    // Reading the oldest makes it the most recently used.
    QVERIFY(analysis.envelope("oldest").isValid());
    const qint64 each = QFileInfo(analysis.pathFor("oldest")).size();
    analysis.setCacheLimit(each * 2);
    analysis.trim();
    QVERIFY(QFile::exists(analysis.pathFor("oldest")));
    QVERIFY(!QFile::exists(analysis.pathFor("middle")));
    QVERIFY(QFile::exists(analysis.pathFor("newest")));
    analysis.clearCache();
    QVERIFY(QDir(dir.path()).entryList({"*.env"}, QDir::Files).isEmpty());
    QVERIFY(!analysis.envelope("oldest").isValid());
  }

  void aFileIsAnalysedThroughFfmpeg() {
    const auto ffmpeg = ffmpegWithFloatOutput();
    if (ffmpeg.isEmpty())
      QSKIP("No ffmpeg with the pcm_f32le encoder here; build-packaging/ffmpeg-out or MUSIX_TEST_FFMPEG provides one.");
    QTemporaryDir dir;
    const auto wav = dir.filePath("tone.wav");
    QVERIFY(writeWav(wav, tone(1000, -20, 6, 44100), 44100));
    AudioAnalysis analysis;
    analysis.setFfmpeg(ffmpeg);
    analysis.setCacheDirectory(dir.filePath("cache"));
    QSignalSpy ready(&analysis, &AudioAnalysis::ready);
    analysis.request("tone", wav, true);
    QVERIFY(analysis.pending("tone"));
    QVERIFY(ready.wait(30000));
    const auto e = analysis.envelope("tone");
    QVERIFY(qAbs(e.frames() - 6 * Envelope::FramesPerSecond) <= 1);
    QVERIFY2(qAbs(e.loudness - -20.0) < 0.5, qPrintable(QString::number(e.loudness)));
    // Another run finds it on disk without decoding again.
    AudioAnalysis later;
    later.setCacheDirectory(dir.filePath("cache"));
    QCOMPARE(later.envelope("tone").levels, e.levels);
    // What ffmpeg cannot read fails once and is not retried.
    QFile bogus(dir.filePath("bogus.m4a"));
    QVERIFY(bogus.open(QIODevice::WriteOnly));
    bogus.write(QByteArray(4096, 'x'));
    bogus.close();
    QSignalSpy failed(&analysis, &AudioAnalysis::failed);
    analysis.request("bogus", bogus.fileName(), true);
    QVERIFY(failed.wait(30000));
    analysis.request("bogus", bogus.fileName(), true);
    QVERIFY(!analysis.pending("bogus"));
  }

  void everyFormatMusixPlaysDecodes() {
    // Fixtures are encoded by whatever full ffmpeg is on the machine; the one
    // under test (the bundled build, given as MUSIX_TEST_FFMPEG) only decodes.
    const auto decoder = ffmpegWithFloatOutput();
    const auto encoder = QStandardPaths::findExecutable("ffmpeg");
    if (decoder.isEmpty() || encoder.isEmpty())
      QSKIP("Needs an ffmpeg to decode with and a full one to encode fixtures.");
    QTemporaryDir dir;
    const auto wav = dir.filePath("tone.wav");
    QVERIFY(writeWav(wav, tone(1000, -20, 8, 48000), 48000));
    const QList<QStringList> formats{{"m4a", "aac"}, {"webm", "libopus"}, {"opus", "libopus"}, {"mp3", "libmp3lame"},
                                     {"flac", "flac"}, {"ogg", "libvorbis"}, {"m4a", "alac"}, {"wv", "wavpack"}, {"wav", "pcm_s16le"}};
    int checked = 0;
    for (const auto &f : formats) {
      const auto out = dir.filePath(f[1] + "." + f[0]);
      QProcess enc;
      enc.start(encoder, {"-v", "error", "-y", "-i", wav, "-c:a", f[1], out});
      if (!enc.waitForFinished(30000) || enc.exitCode() != 0) {
        qWarning() << "skipping" << f[1] << "- this ffmpeg cannot encode it";
        continue;
      }
      const auto e = AudioAnalysis::decode(decoder, out, nullptr);
      QVERIFY2(e.isValid(), qPrintable(f[1]));
      QVERIFY2(qAbs(e.frames() - 8 * Envelope::FramesPerSecond) <= 2, qPrintable(f[1] + " " + QString::number(e.frames())));
      // Lossy codecs shift the level a little, never by much.
      QVERIFY2(qAbs(e.loudness - -20.0) < 1.0, qPrintable(f[1] + " " + QString::number(e.loudness)));
      ++checked;
    }
    QVERIFY(checked >= 4);
  }
};

QTEST_GUILESS_MAIN(AnalysisTest)
#include "audioanalysis_test.moc"
