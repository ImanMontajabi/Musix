#pragma once
#include "audiolevels.h"
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVariantList>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

// Qt's macOS media backend never hands decoded audio to the application, so
// the meters and the levelling cannot listen to playback there. Instead each
// recording is decoded once, ahead of or alongside playback, into what those
// consumers need: the five meter bands over time and one loudness figure.

// What a recording does over time. Frames are indexed by media position, so a
// seek, a pause or a different speed reads the right place without any clock
// of its own.
struct Envelope {
  static constexpr int FramesPerSecond = 30, Bands = 5, SpectrumBands = 16;
  QByteArray levels; // frame-major, Bands per frame, 0..100
  // What the visualizers draw: a finer spectrum, log-spaced from 40 Hz to
  // 10 kHz, and where the beats are.
  QByteArray spectrum; // frame-major, SpectrumBands per frame, 0..100
  QByteArray beats;    // one per frame: 0, or the beat's strength 1..100
  double loudness = std::nan(""); // integrated, LUFS (ITU-R BS.1770)
  bool isValid() const { return !levels.isEmpty(); }
  int frames() const { return int(levels.size() / Bands); }
  static qint64 frameAt(qint64 positionMs) { return positionMs < 0 ? -1 : positionMs * FramesPerSecond / 1000; }
  // Band levels 0..1 at a position; silence outside the recording.
  std::array<double, Bands> at(qint64 positionMs) const;
  std::array<double, SpectrumBands> spectrumAt(qint64 positionMs) const;
  // The strongest beat, 0..1, in the frames after `fromMs` up to `toMs`.
  double beatBetween(qint64 fromMs, qint64 toMs) const;
  static QVariantList mixSpectrum(const std::array<double, SpectrumBands> &a, double weightA,
                                  const std::array<double, SpectrumBands> &b, double weightB);
  // Two decks overlapping: each contributes in proportion to how loud it is
  // in the mix, and the louder band wins.
  static QVariantList mix(const std::array<double, Bands> &a, double weightA, const std::array<double, Bands> &b, double weightB);
  static QVariantList toList(const std::array<double, Bands> &levels);
};

// Integrated loudness after ITU-R BS.1770-4: K-weighted, in 400 ms blocks
// overlapping by 75%, gated absolutely at -70 LUFS and relatively 10 LU below
// the ungated mean. This is the measure R128 tags carry, so a measured file
// and a tagged one are levelled against the same thing.
class LoudnessIntegrator {
  struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
  Biquad shelf, highpass;
  std::array<std::array<double, 4>, 8> state{}; // per channel: shelf z1 z2, highpass z1 z2
  int rate = 0, channels = 0, hop = 0, filled = 0;
  double hopSum = 0;
  std::vector<double> hops; // mean square summed over channels, per 100 ms
public:
  void configure(int sampleRate, int channelCount);
  void process(const float *interleaved, qsizetype frames);
  double loudness() const;
};

// Sixteen band-pass filters on the mono mix, log-spaced from 40 Hz to 10 kHz;
// the same filter as the meters, a little narrower so neighbours stay apart.
class SpectrumBank {
  struct Filter { double b = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; };
  std::array<Filter, Envelope::SpectrumBands> filters{};
  std::array<double, Envelope::SpectrumBands> energy{};
  qint64 samples = 0;
public:
  static double center(int band) { return 40.0 * std::pow(10000.0 / 40.0, band / double(Envelope::SpectrumBands - 1)); }
  void configure(int sampleRate);
  void process(const float *interleaved, qsizetype frames, int channels);
  // 0..1 on the same scale as the meters.
  std::array<double, Envelope::SpectrumBands> take();
};

// Beats, from the finished spectrum: where the low end (below 160 Hz) jumps
// above its own recent peak by more than is usual around it. At most one per
// 11 frames (0.37 s), the stronger kept, so what draws them can never flash
// more than three times a second.
QByteArray detectBeats(const QByteArray &spectrum);

// Decodes a file with the bundled ffmpeg and builds its envelope. Kept apart
// from the process so tests can feed it samples directly.
class EnvelopeBuilder {
  AudioLevels m_levels;
  SpectrumBank m_spectrum;
  QByteArray m_spectrumOut;
  LoudnessIntegrator m_loudness;
  int m_rate, m_channels, m_frameSize, m_pending = 0;
  QByteArray m_out;
public:
  EnvelopeBuilder(int sampleRate, int channels);
  void feed(const float *interleaved, qsizetype frames);
  Envelope finish();
};

// Where the levelling gain comes from, strongest first: a tag the file carries,
// the analysed loudness, then the older running measurement.
struct LevellingGain { double db = 0; QString source; };
LevellingGain levellingGain(double tagDb, double analysedLufs, double measuredDb);

class AudioAnalysis : public QObject {
  Q_OBJECT
public:
  static constexpr int SampleRate = 24000, Channels = 2;
  explicit AudioAnalysis(QObject *parent = nullptr);
  ~AudioAnalysis() override;
  void setFfmpeg(const QString &program) { m_ffmpeg = program; }
  void setCacheDirectory(const QString &directory);
  QString cacheDirectory() const { return m_cacheDir; }
  // Largest total the cache may grow to before the least recently used go.
  void setCacheLimit(qint64 bytes) { m_cacheLimit = bytes; }
  // A finished envelope, from memory or disk, or an invalid one.
  Envelope envelope(const QString &id);
  // Analyse a local file unless it already has been. The urgent one (what is
  // playing) goes ahead of any that were only prepared.
  void request(const QString &id, const QString &file, bool urgent);
  bool pending(const QString &id) const;
  void clearCache();
  static Envelope decode(const QString &ffmpeg, const QString &file, const std::atomic_bool *cancel);
  static bool write(const QString &path, const Envelope &envelope);
  static Envelope read(const QString &path);
  QString pathFor(const QString &id) const;
  // Drops the least recently used envelopes until the cache fits its limit.
  void trim();
signals:
  void ready(const QString &id);
  void failed(const QString &id);
private:
  struct Job { QString id, file; };
  void startNext();
  void remember(const QString &id, const Envelope &envelope);
  QString m_ffmpeg, m_cacheDir;
  qint64 m_cacheLimit = 64ll * 1024 * 1024;
  QList<Job> m_queue;
  QString m_running;
  QPointer<QThread> m_thread;
  std::shared_ptr<std::atomic_bool> m_cancel;
  QHash<QString, Envelope> m_memory;
  QStringList m_memoryOrder;
  QStringList m_failed;
};
