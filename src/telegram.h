#pragma once
// The Telegram account: signing in, and the helper process that holds the session.
//
// A source of its own beside YouTube, local files and the music servers, not a
// MusicServer provider: it has none of what a Subsonic or Jellyfin server has
// (ratings, a play queue, one server at a time), and a person can be signed in
// to both. The sign-in cannot be a one-shot helper call either: the code
// Telegram sends belongs to the connection that asked for it, so helper/
// telegram.py stays up for as long as someone is looking at the dialog and
// speaks one JSON object per line. This class starts it, restarts it if it dies
// unasked, and turns its events into properties for the dialog.
//
// The API id and hash come from the build (telegram-api.local.cmake or
// -DMUSIX_TELEGRAM_API_ID/HASH), or from the environment of a checkout. They
// identify the app, not the person, and sit in the binary the way any
// client's do.
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <functional>

class Telegram : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString state READ state NOTIFY changed)
  Q_PROPERTY(bool available READ available CONSTANT)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(QString name READ name NOTIFY changed)
  Q_PROPERTY(QString username READ username NOTIFY changed)
  Q_PROPERTY(QString codeKind READ codeKind NOTIFY changed)
  Q_PROPERTY(QString passwordHint READ passwordHint NOTIFY changed)
  Q_PROPERTY(bool signedIn READ signedIn NOTIFY changed)
  Q_PROPERTY(QString sessionFolder READ sessionFolder CONSTANT)
public:
  // `directory` holds the session; `restore` reads who was signed in last time.
  explicit Telegram(const QString &directory, QObject *parent = nullptr);
  ~Telegram() override;
  // Where the interpreter and the script are is the backend's to say.
  void setLauncher(std::function<QString()> python, std::function<QString()> script);
  // The state is one of: closed (nothing running), starting, no-credentials,
  // offline, wait-phone, wait-code, wait-password, ready, error.
  QString state() const { return m_state; }
  bool available() const { return m_apiId > 0 && !m_apiHash.isEmpty(); }
  bool busy() const { return m_pending > 0 || m_state == "starting"; }
  QString error() const { return m_error; }
  QString name() const { return m_name; }
  QString username() const { return m_username; }
  QString codeKind() const { return m_codeKind; }
  QString passwordHint() const { return m_hint; }
  bool signedIn() const { return !m_name.isEmpty(); }
  QString sessionFolder() const { return m_directory; }

  // Dialog opened: bring the helper up. Dialog closed: let it go, since no
  // other part of the app needs the connection yet.
  Q_INVOKABLE void open();
  Q_INVOKABLE void close();
  Q_INVOKABLE void retry();
  Q_INVOKABLE void submitPhone(const QString &phone);
  Q_INVOKABLE void submitCode(const QString &code);
  Q_INVOKABLE void submitPassword(const QString &password);
  Q_INVOKABLE void back();
  Q_INVOKABLE void logout();
signals:
  void changed();
private:
  void start();
  void stop();
  void send(const QString &command, const QJsonObject &fields = {});
  void readOutput();
  void handle(const QJsonObject &message);
  void exited();
  void set(const QString &state, const QString &error = {});
  void restoreAccount();
  QString m_directory, m_state = "closed", m_error, m_name, m_username, m_codeKind, m_hint, m_apiHash;
  qint64 m_apiId = 0;
  QProcess *m_process = nullptr;
  QByteArray m_buffer;
  QTimer m_startTimeout, m_restart;
  int m_pending = 0, m_nextId = 1, m_crashes = 0;
  bool m_wanted = false;
  std::function<QString()> m_python, m_script;
};
