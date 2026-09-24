#pragma once
// A macOS notification when an automatic check finds new releases from who
// you follow. Off unless asked for: turning it on is what asks macOS for
// permission, and macOS keeps the answer. The in-app badge does not depend on
// any of this.
#include <QObject>

class ReleaseNotifier : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  // Asks macOS; the answer arrives as permissionAnswered.
  Q_INVOKABLE void requestPermission();
  void notify(const QString &title, const QString &body);
signals:
  void permissionAnswered(bool granted, const QString &error);
};
