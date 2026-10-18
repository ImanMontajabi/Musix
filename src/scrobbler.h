#pragma once
// Listening history sent to a ListenBrainz-compatible server.
//
// ListenBrainz takes a single user token over HTTPS, which is all this needs:
// no application registration, and no secret that would have to ship inside the
// player. Anything that speaks the same submit-listens endpoint works too, so
// the server address is a setting rather than a constant.
//
// Two things are sent. "Playing now" is a courtesy, sent when a song starts and
// forgotten if it fails. A listen is the real record, sent once a song has been
// played far enough to count, and kept until the server accepts it.
#include <QNetworkAccessManager>
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVariantMap>
#include <functional>

class Scrobbler : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
  Q_PROPERTY(QString server READ server WRITE setServer NOTIFY changed)
  Q_PROPERTY(QString account READ account NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(bool hasToken READ hasToken NOTIFY changed)
  Q_PROPERTY(int pending READ pending NOTIFY changed)
  Q_PROPERTY(bool keyringAvailable READ keyringAvailable CONSTANT)
public:
  explicit Scrobbler(QObject *parent = nullptr, bool restore = true);
  ~Scrobbler() override;

  static constexpr const char *defaultServer = "https://api.listenbrainz.org";
  // A song counts as listened to once it has played for half its length or
  // four minutes, whichever comes first. Anything under thirty seconds is never
  // counted. These are the thresholds the service itself documents.
  static constexpr qint64 minimumLength = 30000;
  static constexpr qint64 longestWait = 240000;
  static qint64 thresholdFor(qint64 durationMs);
  // The submitted shape of a song, or an empty map when it cannot be submitted.
  static QVariantMap listenFor(const QVariantMap &track, qint64 startedAt);

  bool enabled() const { return m_settings.value("scrobbler/enabled", false).toBool(); }
  void setEnabled(bool on);
  QString server() const { return m_settings.value("scrobbler/server", defaultServer).toString(); }
  void setServer(const QString &value);
  QString account() const { return m_account; }
  QString status() const { return m_status; }
  bool busy() const { return m_busy; }
  bool hasToken() const { return !m_token.isEmpty(); }
  int pending() const { return m_queue.size(); }
  bool keyringAvailable() const;
  // Where the token lives on macOS, which has no Secret Service: a 0600 file, like the Telegram session.
  QString tokenFile() const { return m_tokenFile; }
  void setTokenFile(const QString &path) { m_tokenFile = path; }

  // Check a token and remember it when the server accepts it.
  Q_INVOKABLE void signIn(const QString &token);
  Q_INVOKABLE void signOut();
  Q_INVOKABLE void retryPending();

  void nowPlaying(const QVariantMap &track);
  void submit(const QVariantMap &track, qint64 startedAt);

signals:
  void changed();

private:
  friend class BackendTest;
  void send(const QVariantMap &payload, const QString &type,
            std::function<void(bool, const QString &)> done);
  void flush();
  void loadQueue();
  void saveQueue();
  void secret(const QStringList &args, const QByteArray &input,
              std::function<void(bool, QByteArray)> callback);
  void setStatus(const QString &value);

  QSettings m_settings;
  QNetworkAccessManager m_network;
  void fileSecret(const QStringList &args, const QByteArray &input, std::function<void(bool, QByteArray)> callback);
  QString m_token, m_account, m_status, m_tokenFile;
  bool m_busy = false, m_flushing = false;
  // Listens the server has not taken yet, oldest first.
  QVariantList m_queue;
  QTimer m_retry;
};
