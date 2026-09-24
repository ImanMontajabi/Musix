#pragma once
// Tells the user a newer Musix exists, and nothing more.
//
// It asks GitHub for the project's releases and, if one is newer than this
// build, offers to open that release's page. It never downloads or installs
// anything: an ad-hoc signed app that replaced itself would be exactly the
// kind of binary nobody should trust, and the page is where the notes, the
// checksums and the Open Anyway advice live anyway.
//
// A release counts only if it is published, not a prerelease, carries a
// Musix-*-arm64.dmg, and has a tag that reads as a version, with or without
// the V the first tags were spelled with. The page opened is built from that
// tag on this repository, never taken from the response.
#include <QDateTime>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QVersionNumber>

class QNetworkReply;

class UpdateChecker : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
  Q_PROPERTY(bool automatic READ automatic WRITE setAutomatic NOTIFY settingsChanged)
  Q_PROPERTY(bool checking READ checking NOTIFY stateChanged)
  // What the last check found, for the Settings row: empty until one has run.
  Q_PROPERTY(QString status READ status NOTIFY stateChanged)
  Q_PROPERTY(QString availableVersion READ availableVersion NOTIFY stateChanged)

public:
  struct Release {
    QVersionNumber version;
    QString tag;
    // As the release spells it, for display: 0.15.0, not the normalized 0.15.
    QString name() const { return tag.mid(tag.startsWith('v') || tag.startsWith('V') ? 1 : 0); }
    bool valid() const { return !version.isNull(); }
  };
  enum class Outcome { Newer, Current, Offline, RateLimited, Failed };

  explicit UpdateChecker(const QString &current, QObject *parent = nullptr);

  QString currentVersion() const { return m_currentName; }
  bool automatic() const { return m_settings.value("updates/automatic", true).toBool(); }
  void setAutomatic(bool on);
  bool checking() const { return !m_reply.isNull(); }
  QString status() const { return m_status; }
  QString availableVersion() const { return m_available.name(); }

  // Checks at most once a day, the first a little after launch. Never blocks:
  // the request is asynchronous and nothing waits on it.
  void start();
  // Where the releases are asked for. Only a loopback address is accepted in
  // place of GitHub's, which is what the tests and a local mock use.
  bool setEndpoint(const QUrl &url);

  Q_INVOKABLE void check(bool manual = true);
  Q_INVOKABLE void openRelease();
  // After Later an automatic check stays quiet about this version until the
  // next day; after Skip it never mentions it again. A manual check always
  // says what it found.
  Q_INVOKABLE void later();
  Q_INVOKABLE void skip();

  static QVersionNumber parseTag(const QString &tag);
  static Release newest(const QJsonArray &releases);
  static QUrl releasePage(const QString &tag);

signals:
  void settingsChanged();
  void stateChanged();
  void available(const QString &version, const QString &current, bool manual);
  void upToDate();
  void failed(const QString &message);

private:
  void finished(QNetworkReply *reply, bool manual);
  void conclude(Outcome outcome, const QString &message, bool manual);
  bool suppressed(const Release &release) const;
  void maybeCheck();

  QVersionNumber m_current;
  QString m_currentName;
  QUrl m_endpoint;
  QSettings m_settings;
  QNetworkAccessManager m_network;
  QPointer<QNetworkReply> m_reply;
  QTimer m_daily;
  Release m_available;
  QString m_status;
};
