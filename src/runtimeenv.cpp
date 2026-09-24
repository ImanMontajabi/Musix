#include "backend.h"
#include "profile.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

// A packaged application carries its own Python, its own resolver packages and
// its own ffmpeg, because the person running it is not expected to have any of
// them. A checkout carries none of that and keeps using what the scripts
// export. Everything here answers the same question twice, once for each case.

// Importing the package is not enough to know it survived: a gutted directory
// still imports as an empty namespace package. Ask for the entry points the
// resolver is actually used through.
static const char *resolverHealthCheck() {
  return "import yt_dlp,ytmusicapi;"
         "from yt_dlp import YoutubeDL;"
         "from ytmusicapi import YTMusic";
}

static bool resolverHealthy(const QString &python) {
  QProcess check;
  check.start(python, {"-c", resolverHealthCheck()});
  return check.waitForFinished(20000) && check.exitCode() == 0;
}

static QString resourcePath(const QString &relative) {
  // Contents/MacOS/../Resources inside a bundle; harmless elsewhere.
  return QDir::cleanPath(QCoreApplication::applicationDirPath() +
                         "/../Resources/" + relative);
}

QString Backend::bundledRuntime() const {
  const auto packaged = resourcePath("runtime");
  return QFileInfo::exists(packaged + "/bin/python3") ? packaged : QString();
}

QString Backend::writableRuntime() const {
  // The same directory the library lives in, so a person has one folder to
  // find rather than two.
  return Profile::location(QStandardPaths::AppDataLocation) +
         "/runtime";
}

QString Backend::pythonExecutable() const {
  if (const auto chosen = qEnvironmentVariable("SUNG_PYTHON"); !chosen.isEmpty())
    return chosen;
  // Prefer the copy that can be written to: it is the one the updater keeps
  // current. The bundle's own runtime carries the version it shipped with.
  // While a new seed waits to replace it, the old copy is left to drain.
  const auto seeded = writableRuntime() + "/bin/python3";
  if (QFileInfo::exists(seeded) && !m_seedSwapPending)
    return seeded;
  if (const auto packaged = bundledRuntime(); !packaged.isEmpty())
    return packaged + "/bin/python3";
  auto beside = QCoreApplication::applicationDirPath() + "/../runtime/bin/python";
  if (!QFile::exists(beside))
    beside = QCoreApplication::applicationDirPath() +
             "/../lib/musix/runtime/bin/python";
  return QFile::exists(beside) ? beside : QStringLiteral("python3");
}

QString Backend::helperScript() const {
  if (const auto chosen = qEnvironmentVariable("SUNG_HELPER"); !chosen.isEmpty())
    return chosen;
  if (const auto packaged = resourcePath("helper/catalog.py");
      QFileInfo::exists(packaged))
    return packaged;
  auto beside = QCoreApplication::applicationDirPath() + "/../helper/catalog.py";
  if (!QFile::exists(beside))
    beside = QCoreApplication::applicationDirPath() + "/../lib/musix/catalog.py";
  return beside;
}

QString Backend::ffmpegDirectory() const {
  if (const auto chosen = qEnvironmentVariable("SUNG_FFMPEG_DIR");
      !chosen.isEmpty())
    return chosen;
  const auto packaged = resourcePath("ffmpeg");
  return QFileInfo::exists(packaged + "/ffprobe") ? packaged : QString();
}

void Backend::seedRuntime() {
  const auto packaged = bundledRuntime();
  if (packaged.isEmpty())
    return; // A checkout has nothing to seed from.
  const auto target = writableRuntime();
  const auto stamp = target + "/.musix-version";
  QFile marker(stamp);
  if (marker.open(QIODevice::ReadOnly) &&
      marker.readAll().trimmed() == QCoreApplication::applicationVersion().toUtf8())
    return; // Already current for this build.
  if (m_seedProcess.state() != QProcess::NotRunning || m_seedSwapPending)
    return;
  QDir().mkpath(QFileInfo(target).absolutePath());
  const auto staging = target + ".new";
  QDir(staging).removeRecursively();
  // Copying in a child process keeps a first launch responsive; until it
  // lands, requests run against the bundle's own read-only runtime.
  connect(&m_seedProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
          this, [this, staging](int code, QProcess::ExitStatus status) {
            if (code != 0 || status != QProcess::NormalExit) {
              QDir(staging).removeRecursively();
              return;
            }
            m_seedSwapPending = true;
            installSeed();
          }, Qt::SingleShotConnection);
  m_seedProcess.start("/bin/cp", {"-a", packaged, staging});
}

void Backend::installSeed() {
  // Replacing the copy deletes files a running helper can still open by path:
  // the first request after an upgrade lost certifi's CA bundle that way and
  // failed with a TLS error. So the swap waits for everything running from
  // the old copy, and new requests use the bundle's runtime meanwhile, which
  // is what lets that wait end.
  const auto target = writableRuntime();
  const auto staging = target + ".new";
  bool busy = m_resolverProcess.state() != QProcess::NotRunning;
  for (const auto *p : std::as_const(m_processes))
    busy = busy || (p->state() != QProcess::NotRunning && p->program().startsWith(target + "/"));
  if (busy) {
    QTimer::singleShot(250, this, &Backend::installSeed);
    return;
  }
  m_seedSwapPending = false;
  QDir(target).removeRecursively();
  if (!QDir().rename(staging, target)) {
    QDir(staging).removeRecursively();
    return;
  }
  QFile stampFile(target + "/.musix-version");
  if (stampFile.open(QIODevice::WriteOnly))
    stampFile.write(QCoreApplication::applicationVersion().toUtf8());
  // Re-seeding has just put the version that shipped in this build back over
  // whatever the updater had reached, so ask again now rather than waiting
  // out the daily interval.
  updateResolver(true);
}

void Backend::updateResolver(bool force) {
  // Only ever the two packages that go stale when YouTube changes, and only in
  // the writable copy: the bundle is signed and must not be written to.
  const auto python = writableRuntime() + "/bin/python3";
  // Not while a seed is on its way: it would replace this copy under pip, and
  // it asks for an update itself once it is in place.
  if (!QFileInfo::exists(python) || m_resolverBusy ||
      m_seedProcess.state() != QProcess::NotRunning || m_seedSwapPending)
    return;
  const auto now = QDateTime::currentSecsSinceEpoch();
  const auto last = m_settings.value("resolverUpdated").toLongLong();
  if (!force && last && now - last < 24 * 60 * 60)
    return;
  m_resolverBusy = true;
  m_settings.setValue("resolverUpdated", now);
  // Remember what works before replacing it.
  QProcess probe;
  probe.start(python, {"-c", QString(resolverHealthCheck()) +
                                 ";print(yt_dlp.version.__version__,"
                                 "ytmusicapi.__version__)"});
  m_resolverRollback = probe.waitForFinished(8000)
                           ? QString::fromUtf8(probe.readAllStandardOutput()).trimmed()
                           : QString();
  connect(&m_resolverProcess,
          qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this](int code, QProcess::ExitStatus status) {
            finishResolverUpdate(code == 0 && status == QProcess::NormalExit);
          }, Qt::SingleShotConnection);
  m_resolverProcess.start(python, {"-m", "pip", "install", "--upgrade",
                                   "--disable-pip-version-check", "--quiet",
                                   "yt-dlp", "ytmusicapi"});
}

void Backend::finishResolverUpdate(bool installed) {
  const auto python = writableRuntime() + "/bin/python3";
  bool healthy = installed && resolverHealthy(python);
  if (!healthy && !m_resolverRollback.isEmpty()) {
    // An upgrade that will not import is worse than a stale one. Put back the
    // versions that were running a moment ago and say nothing.
    const auto parts = m_resolverRollback.split(' ');
    if (parts.size() == 2) {
      QProcess back;
      back.start(python, {"-m", "pip", "install", "--disable-pip-version-check",
                          "--quiet", "yt-dlp==" + parts[0],
                          "ytmusicapi==" + parts[1]});
      back.waitForFinished(120000);
      healthy = resolverHealthy(python);
    }
  }
  m_resolverBusy = false;
  if (!healthy && !m_resolverReseeded && !bundledRuntime().isEmpty()) {
    // There was nothing to roll back to, which is what an interrupted install
    // leaves behind: the probe found no working versions to record. The copy
    // in the bundle is the one that is known to work, so go back to that and
    // let the seed's own update carry it forward again.
    m_resolverReseeded = true;
    QFile::remove(writableRuntime() + "/.musix-version");
    seedRuntime();
    return;
  }
  if (healthy)
    m_streams.clear(); // Stale resolutions came from the previous resolver.
  if (m_retryAfterUpdate) {
    m_retryAfterUpdate = false;
    // Only worth repeating if something actually changed underneath.
    if (healthy)
      QTimer::singleShot(0, this, &Backend::retry);
  }
}
