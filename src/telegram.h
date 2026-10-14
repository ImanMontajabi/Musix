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
// Beyond signing in, this holds the sources a person has chosen to listen to
// (Saved Messages, a joined chat, a forum topic, a public channel), asks the
// helper to list their songs a page at a time, and hands each finished list to
// whoever owns the library (songsReady). The helper is up only while a dialog is
// open or a request is running; a refresh started from anywhere brings it up,
// waits for the session, and lets it go when the work is done.
//
// The API id and hash come from the build (telegram-api.local.cmake or
// -DMUSIX_TELEGRAM_API_ID/HASH), or from the environment of a checkout. They
// identify the app, not the person, and sit in the binary the way any
// client's do.
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
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
  Q_PROPERTY(QVariantList sources READ sources NOTIFY sourcesChanged)
  Q_PROPERTY(QVariantList chats READ chats NOTIFY listChanged)
  Q_PROPERTY(QVariantList topics READ topics NOTIFY listChanged)
  Q_PROPERTY(QVariantMap found READ found NOTIFY listChanged)
  Q_PROPERTY(bool listing READ listing NOTIFY listChanged)
  Q_PROPERTY(QString listError READ listError NOTIFY listChanged)
  Q_PROPERTY(bool syncing READ syncing NOTIFY sourcesChanged)
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
  QVariantList sources() const { return m_sources; }
  QVariantList chats() const { return m_chats; }
  QVariantList topics() const { return m_topics; }
  QVariantMap found() const { return m_found; }
  bool listing() const { return m_listing > 0; }
  QString listError() const { return m_listError; }
  bool syncing() const { return !m_jobs.isEmpty() || !m_afterReady.isEmpty(); }

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

  // Choosing what to listen to. Chat ids travel as strings: they are 13 digits
  // for a channel, which a JavaScript number holds but should not be asked to.
  Q_INVOKABLE void loadChats();
  Q_INVOKABLE void loadTopics(const QString &chatId);
  Q_INVOKABLE void clearTopics();
  Q_INVOKABLE void search(const QString &query);
  Q_INVOKABLE void clearFound();
  Q_INVOKABLE void addSource(const QString &chatId, const QString &title, const QString &kind,
                             const QString &username, const QString &topicId = {}, const QString &topicTitle = {});
  Q_INVOKABLE void removeSource(const QString &key);
  Q_INVOKABLE void refresh(const QString &key);
  Q_INVOKABLE void refreshAll();
  Q_INVOKABLE void cancelSync(const QString &key);
  Q_INVOKABLE bool hasSource(const QString &chatId, const QString &topicId = {}) const;
  // The playlist id the library keeps a source's songs under.
  Q_INVOKABLE static QString playlistId(const QString &key) { return "telegram:" + key; }
signals:
  void changed();
  void sourcesChanged();
  void listChanged();
  // A source's songs, ready to be kept. `complete` is false when the sync
  // stopped early (a long pause was asked for, it was cancelled, the helper
  // died): what arrived is still delivered, and the owner should add it to what
  // it already has instead of replacing that.
  void songsReady(const QString &key, const QString &title, const QVariantList &songs, bool complete);
  void sourceRemoved(const QString &key);
private:
  struct Job {
    QString key;
    QVariantList songs;
  };
  using Callback = std::function<void(const QJsonObject &)>;
  void send(const QString &command, const QJsonObject &fields, Callback done);
  void whenReady(std::function<void()> action);
  void failWaiting(const QString &message);
  void startSync(const QString &key);
  void finishJob(int job, const QJsonObject &reply);
  void abandonJobs(const QString &message);
  void setSource(const QString &key, const QVariantMap &changes);
  QVariantMap sourceFor(const QString &key) const;
  void loadSources();
  void saveSources() const;
  QVariantMap songRow(const QVariantMap &source, const QJsonObject &song) const;
  bool wanted() const { return m_holders > 0 || !m_jobs.isEmpty() || !m_afterReady.isEmpty(); }
  void stopProcess();
  void releaseIfIdle();
  void start();
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
  int m_pending = 0, m_nextId = 1, m_crashes = 0, m_holders = 0, m_listing = 0, m_nextJob = 1;
  QString m_userId, m_listError;
  QVariantList m_sources, m_chats, m_topics;
  QVariantMap m_found;
  QHash<int, Callback> m_callbacks;
  QHash<int, Job> m_jobs;
  QList<std::function<void()>> m_afterReady;
  std::function<QString()> m_python, m_script;
};
