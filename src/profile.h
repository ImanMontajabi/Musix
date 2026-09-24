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
