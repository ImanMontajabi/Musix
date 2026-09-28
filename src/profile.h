#pragma once
// Where Musix keeps what it writes.
//
// Normally that is the platform's own places. With MUSIX_PROFILE naming a
// directory, everything goes under it instead: library, caches, the runtime
// copy, settings and the QML cache. That is what lets the app be launched
// and exercised without touching a real library -- on macOS neither
// QStandardPaths nor QSettings looks at $HOME, so there is no other way in
// from outside short of a second user account, and another account's
// processes cannot put windows on this session's screen.
#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QSettings>
#include <QStandardPaths>
#include <QString>

namespace Profile {

inline QString root() { return qEnvironmentVariable("MUSIX_PROFILE"); }
inline bool isolated() { return !root().isEmpty(); }

inline QString location(QStandardPaths::StandardLocation type) {
  if (isolated()) {
    if (type == QStandardPaths::AppDataLocation || type == QStandardPaths::AppLocalDataLocation)
      return root() + "/data";
    if (type == QStandardPaths::CacheLocation)
      return root() + "/cache";
  }
  return QStandardPaths::writableLocation(type);
}

// Where an unisolated run writes: the library, the cache, and the folder the
// preferences domain lives in. Asked of QStandardPaths directly, so a profile
// never hides them, and not of $HOME, which on macOS they do not follow.
inline QStringList realLocations() {
  return {QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
          QStandardPaths::writableLocation(QStandardPaths::CacheLocation),
          QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)};
}

// A path as the file system will see it. The profile may not exist yet, so
// the nearest existing ancestor is resolved and the rest appended, which
// turns /tmp and /var into /private/tmp and /private/var.
inline QString resolved(const QString &path) {
  QFileInfo info(QDir::cleanPath(QDir(path).absolutePath()));
  QStringList rest;
  while (!info.exists() && !info.isRoot()) {
    rest.prepend(info.fileName());
    info = QFileInfo(info.absolutePath());
  }
  const auto base = info.canonicalFilePath();
  return QDir::cleanPath(rest.isEmpty() ? base : base + "/" + rest.join('/'));
}

// Why a run on this profile could reach the real library, or empty when it
// cannot: no profile at all, or one that is, lies inside, or holds any of
// the real locations.
inline QString unsafeReason() {
  if (!isolated())
    return "MUSIX_PROFILE is not set, so it would use the real library";
  const auto mine = resolved(root());
  for (const auto &location : realLocations()) {
    if (location.isEmpty())
      continue;
    const auto real = resolved(location);
    if (mine == real || mine.startsWith(real + '/') || real.startsWith(mine + '/'))
      return QString("MUSIX_PROFILE %1 overlaps the real %2").arg(mine, real);
  }
  return {};
}

// Before anything reads a setting: every default-constructed QSettings, the
// QML Settings elements included, then reads and writes an INI file under the
// profile rather than the app's preferences domain.
inline void install() {
  if (!isolated())
    return;
  QDir().mkpath(root());
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root() + "/settings");
  qputenv("QML_DISK_CACHE_PATH", (root() + "/cache/qmlcache").toUtf8());
}

} // namespace Profile
