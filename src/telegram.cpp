#include "telegram.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <csignal>
#include <unistd.h>

#ifndef MUSIX_TELEGRAM_API_ID
#define MUSIX_TELEGRAM_API_ID 0
#endif
#ifndef MUSIX_TELEGRAM_API_HASH
#define MUSIX_TELEGRAM_API_HASH ""
#endif

// Chat ids are 13 digits and survive a double exactly; this keeps them whole
// and as text, the way the rest of the app sees them.
static QString idText(const QJsonValue &value) {
  if (value.isString()) return value.toString();
  return QString::number(qint64(value.toDouble()));
}

static QString sourceKey(const QString &chatId, const QString &topicId) {
  return topicId.isEmpty() || topicId == "0" ? chatId : chatId + "/" + topicId;
}

Telegram::Telegram(const QString &directory, QObject *parent)
    : QObject(parent), m_directory(directory) {
  // The environment wins so that a checkout can be pointed at other
  // credentials without a rebuild, and so a test can stand in for the real ones.
  const auto env = QProcessEnvironment::systemEnvironment();
  m_apiId = env.contains("MUSIX_TELEGRAM_API_ID") ? env.value("MUSIX_TELEGRAM_API_ID").toLongLong()
                                                  : qint64(MUSIX_TELEGRAM_API_ID);
  m_apiHash = env.contains("MUSIX_TELEGRAM_API_HASH") ? env.value("MUSIX_TELEGRAM_API_HASH")
                                                      : QString(MUSIX_TELEGRAM_API_HASH);
  m_startTimeout.setSingleShot(true);
  m_startTimeout.setInterval(25000);
  connect(&m_startTimeout, &QTimer::timeout, this, [this] {
    if (m_state != "starting") return;
    stopProcess();
    failWaiting("Telegram didn’t respond. Try again.");
    set("error", "Telegram didn’t respond. Try again.");
  });
  m_restart.setSingleShot(true);
  connect(&m_restart, &QTimer::timeout, this, [this] { if (wanted()) start(); });
  // Playing a song brings the helper up, which takes a few seconds; it stays
  // for a while after the last download so the next song does not wait for that.
  m_linger.setSingleShot(true);
  m_linger.setInterval(90000);
  connect(&m_linger, &QTimer::timeout, this, [this] { releaseIfIdle(); });
  restoreAccount();
  loadSources();
  loadTags();
}

Telegram::~Telegram() { stopProcess(); }

void Telegram::setLauncher(std::function<QString()> python, std::function<QString()> script) {
  m_python = std::move(python);
  m_script = std::move(script);
}

void Telegram::restoreAccount() {
  QFile file(m_directory + "/account.json");
  if (!file.open(QIODevice::ReadOnly)) return;
  const auto account = QJsonDocument::fromJson(file.readAll()).object();
  m_name = account.value("name").toString();
  m_username = account.value("username").toString();
  m_userId = account.contains("userId") ? idText(account.value("userId")) : QString();
  if (m_userId == "0") m_userId.clear();
}

void Telegram::set(const QString &state, const QString &error) {
  m_state = state;
  m_error = error;
  emit changed();
}

// --- Process -------------------------------------------------------------

void Telegram::open() {
  ++m_holders;
  m_crashes = 0;
  if (!available()) {
    set("no-credentials");
    return;
  }
  start();
}

void Telegram::close() {
  if (m_holders > 0) --m_holders;
  releaseIfIdle();
}

// The helper goes when nothing wants it: no dialog is open, no request is
// running, nothing is waiting for the session.
void Telegram::releaseIfIdle() {
  if (wanted()) return;
  stopProcess();
  m_pending = 0;
  set("closed");
}

void Telegram::retry() {
  stopProcess();
  m_crashes = 0;
  if (!available()) {
    set("no-credentials");
    return;
  }
  start();
}

void Telegram::start() {
  if (m_process) return;
  const auto python = m_python ? m_python() : QString();
  const auto script = m_script ? m_script() : QString();
  if (python.isEmpty() || script.isEmpty()) {
    failWaiting("Telegram support can’t start: the helper wasn’t found.");
    set("error", "Telegram support can’t start: the helper wasn’t found.");
    return;
  }
  QDir().mkpath(m_directory);
  auto process = new QProcess(this);
  auto env = QProcessEnvironment::systemEnvironment();
  env.insert("MUSIX_TELEGRAM_DIR", m_directory);
  env.insert("MUSIX_TELEGRAM_API_ID", QString::number(m_apiId));
  env.insert("MUSIX_TELEGRAM_API_HASH", m_apiHash);
  env.insert("MUSIX_VERSION", QStringLiteral(MUSIX_VERSION));
  process->setProcessEnvironment(env);
  // Its own process group, so closing the app takes the helper and anything it
  // started with it, as Backend::request does for the catalogue helper.
  process->setChildProcessModifier([] { ::setsid(); });
  connect(process, &QProcess::readyReadStandardOutput, this, &Telegram::readOutput);
  // Telethon's log, which is never allowed to carry a secret; read so the pipe
  // can't fill and stall the helper.
  connect(process, &QProcess::readyReadStandardError, process, [process] { process->readAllStandardError(); });
  connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart || process != m_process) return;
    m_process = nullptr;
    process->deleteLater();
    failWaiting("Telegram support can’t start. Run scripts/setup.sh.");
    set("error", "Telegram support can’t start. Run scripts/setup.sh.");
  });
  connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this, process](int code, QProcess::ExitStatus) {
            if (process != m_process) return;
            m_process = nullptr;
            process->deleteLater();
            m_buffer.clear();
            m_pending = 0;
            m_callbacks.clear();
            const bool listed = m_listing > 0;
            m_listing = 0;
            abandonJobs("Telegram stopped. Refresh to try again.");
            failFetches(true, "Telegram stopped. Try again.");
            if (listed) m_listError = "Telegram stopped. Try again.";
            emit listChanged();
            if (code == 2 && m_state != "starting") {
              failWaiting(m_error);
              return; // said why, and asked for no restart
            }
            exited();
          });
  m_process = process;
  m_buffer.clear();
  set("starting");
  m_startTimeout.start();
  process->start(python, {script});
}

void Telegram::exited() {
  if (!wanted()) {
    releaseIfIdle();
    return;
  }
  if (++m_crashes > 3) {
    failWaiting("Telegram stopped unexpectedly. Try again.");
    set("error", "Telegram stopped unexpectedly. Try again.");
    return;
  }
  set("starting");
  m_restart.start(500 * m_crashes);
}

void Telegram::stopProcess() {
  m_startTimeout.stop();
  m_restart.stop();
  auto process = m_process;
  m_process = nullptr;
  m_callbacks.clear();
  if (!process) return;
  process->disconnect(this);
  // Let it finish on its own once its input ends, even if this object is gone.
  process->setParent(nullptr);
  // The helper leaves when its input ends. One that doesn't is killed with its
  // group a moment later rather than waited for.
  process->closeWriteChannel();
  const auto pid = process->processId();
  connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process, &QObject::deleteLater);
  QTimer::singleShot(2000, process, [process, pid] {
    if (process->state() == QProcess::NotRunning) return;
    if (pid > 0) ::kill(-pid, SIGKILL);
    process->kill();
  });
}

// --- Sign-in -------------------------------------------------------------

void Telegram::send(const QString &command, const QJsonObject &fields) {
  if (!m_process || m_process->state() != QProcess::Running) return;
  auto request = fields;
  request.insert("id", m_nextId++);
  request.insert("cmd", command);
  m_process->write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
  ++m_pending;
  m_error.clear();
  emit changed();
}

void Telegram::submitPhone(const QString &phone) { send("phone", {{"phone", phone}}); }
void Telegram::submitCode(const QString &code) { send("code", {{"code", code}}); }
void Telegram::submitPassword(const QString &password) { send("password", {{"password", password}}); }
void Telegram::back() { send("back"); }
void Telegram::logout() {
  failFetches(true, "Signed out of Telegram.");
  clearCache();
  send("logout");
}

// --- Requests that carry an answer ----------------------------------------

void Telegram::send(const QString &command, const QJsonObject &fields, Callback done) {
  if (!m_process || m_process->state() != QProcess::Running) {
    if (done) done({{"ok", false}, {"error", "Telegram isn’t running."}});
    return;
  }
  auto request = fields;
  const int id = m_nextId++;
  request.insert("id", id);
  request.insert("cmd", command);
  if (done) m_callbacks.insert(id, std::move(done));
  m_process->write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
}

// Work that needs the account runs when the session is up: at once if it is,
// otherwise after the helper has started and found it signed in.
void Telegram::whenReady(std::function<void()> action) {
  if (m_state == "ready") {
    action();
    return;
  }
  m_afterReady.append(std::move(action));
  emit sourcesChanged();
  if (!available()) {
    failWaiting("This build has no Telegram credentials.");
    return;
  }
  start();
}

void Telegram::failWaiting(const QString &message) {
  const bool had = !m_afterReady.isEmpty() || m_listing > 0;
  m_afterReady.clear();
  m_listing = 0;
  const auto reason = message.isEmpty() ? QStringLiteral("Sign in to Telegram first.") : message;
  failFetches(false, reason);
  for (auto &source : m_sources) {
    auto map = source.toMap();
    if (map.value("status") != "queued") continue;
    map["status"] = "error";
    map["message"] = reason;
    source = map;
  }
  if (had) m_listError = reason;
  emit sourcesChanged();
  emit listChanged();
  if (!wanted() && m_state != "no-credentials" && m_state != "error" && m_state != "offline") releaseIfIdle();
}

// --- Sources -------------------------------------------------------------

void Telegram::loadSources() {
  QFile file(m_directory + "/sources.json");
  if (!file.open(QIODevice::ReadOnly)) return;
  for (const auto &value : QJsonDocument::fromJson(file.readAll()).array()) {
    auto source = value.toObject().toVariantMap();
    if (source.value("key").toString().isEmpty()) continue;
    source["status"] = "";
    source["message"] = "";
    m_sources.append(source);
  }
}

void Telegram::saveSources() const {
  QDir().mkpath(m_directory);
  QJsonArray array;
  for (const auto &source : m_sources) {
    auto map = source.toMap();
    map.remove("status");
    map.remove("message");
    array.append(QJsonObject::fromVariantMap(map));
  }
  QFile file(m_directory + "/sources.json");
  if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  }
}

QVariantMap Telegram::sourceFor(const QString &key) const {
  for (const auto &source : m_sources)
    if (source.toMap().value("key") == key) return source.toMap();
  return {};
}

void Telegram::setSource(const QString &key, const QVariantMap &changes) {
  for (auto &source : m_sources) {
    auto map = source.toMap();
    if (map.value("key") != key) continue;
    for (auto it = changes.begin(); it != changes.end(); ++it) map[it.key()] = it.value();
    source = map;
    break;
  }
  emit sourcesChanged();
}

bool Telegram::hasSource(const QString &chatId, const QString &topicId) const {
  return !sourceFor(sourceKey(chatId, topicId)).isEmpty();
}

void Telegram::addSource(const QString &chatId, const QString &title, const QString &kind, const QString &username,
                         const QString &topicId, const QString &topicTitle) {
  const auto key = sourceKey(chatId, topicId);
  if (chatId.isEmpty() || hasSource(chatId, topicId)) return;
  const bool topic = !topicId.isEmpty() && topicId != "0";
  m_sources.append(QVariantMap{{"key", key}, {"chatId", chatId}, {"topicId", topic ? topicId : QString("0")},
                               {"title", topic ? title + " › " + topicTitle : title}, {"chatTitle", title},
                               {"kind", topic ? QString("topic") : kind}, {"username", username}, {"count", 0},
                               {"syncedAt", qint64(0)}, {"status", ""}, {"message", ""}});
  saveSources();
  emit sourcesChanged();
  refresh(key);
}

void Telegram::removeSource(const QString &key) {
  for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it)
    if (it->key == key) send("cancel", {{"job", it.key()}}, {});
  for (int i = 0; i < m_sources.size(); ++i)
    if (m_sources[i].toMap().value("key") == key) {
      m_sources.removeAt(i);
      saveSources();
      emit sourcesChanged();
      emit sourceRemoved(key);
      break;
    }
}

void Telegram::refresh(const QString &key) {
  const auto source = sourceFor(key);
  const auto status = source.value("status").toString();
  if (source.isEmpty() || status == "queued" || status == "syncing" || status == "waiting") return;
  setSource(key, {{"status", "queued"}, {"message", ""}});
  whenReady([this, key] { startSync(key); });
}

void Telegram::refreshAll() {
  for (const auto &source : m_sources) refresh(source.toMap().value("key").toString());
}

void Telegram::cancelSync(const QString &key) {
  for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it)
    if (it->key == key) send("cancel", {{"job", it.key()}}, {});
  // A refresh still waiting for the session is dropped; the action that was
  // queued for it finds no source in the "queued" state and does nothing.
  if (sourceFor(key).value("status") == "queued") setSource(key, {{"status", ""}, {"message", ""}});
}

QVariantMap Telegram::songRow(const QVariantMap &source, const QJsonObject &song) const {
  const auto chat = source.value("chatId").toString();
  const auto message = idText(song.value("messageId"));
  const auto identity = "telegram:" + m_userId;
  const auto hash = QCryptographicHash::hash((identity + "\n" + chat + ":" + message).toUtf8(), QCryptographicHash::Sha1).toHex();
  QVariantMap row{{"id", "tg_" + QString::fromLatin1(hash.left(24))},
                  {"source", "telegram"},
                  {"server", identity},
                  {"remoteId", chat + ":" + message},
                  {"kind", "song"},
                  {"title", song.value("title").toString()},
                  {"artist", song.value("artist").toString()},
                  {"album", source.value("chatTitle", source.value("title")).toString()},
                  {"seconds", song.value("seconds").toInt()},
                  {"mime", song.value("mime").toString()},
                  {"size", qint64(song.value("size").toDouble())},
                  {"added", qint64(song.value("date").toDouble())},
                  {"telegramChat", chat},
                  {"telegramMessage", message},
                  {"fileName", song.value("fileName").toString()}};
  // What a file said about itself when it was played stays, through a refresh
  // that only knows what the message carried.
  const auto learned = m_tags.value(row.value("id").toString());
  for (auto it = learned.begin(); it != learned.end(); ++it) row[it.key()] = it.value();
  if (const auto topic = source.value("topicId").toString(); !topic.isEmpty() && topic != "0") row["telegramTopic"] = topic;
  return row;
}

void Telegram::startSync(const QString &key) {
  const auto source = sourceFor(key);
  if (source.isEmpty() || source.value("status") != "queued") return;
  const int job = m_nextJob++;
  m_jobs.insert(job, Job{key, {}});
  setSource(key, {{"status", "syncing"}, {"message", ""}, {"count", 0}});
  send("sync", {{"chatId", source.value("chatId").toString()}, {"topicId", source.value("topicId").toString()}, {"job", job}},
       [this, job](const QJsonObject &reply) { finishJob(job, reply); });
}

void Telegram::finishJob(int job, const QJsonObject &reply) {
  if (!m_jobs.contains(job)) return;
  const auto finished = m_jobs.take(job);
  const auto source = sourceFor(finished.key);
  const bool ok = reply.value("ok").toBool();
  const auto result = reply.value("result").toObject();
  const bool complete = ok && result.value("complete").toBool(true);
  const auto problem = reply.value("error").toString();
  const bool cancelled = !ok && problem == "Cancelled.";
  if (!source.isEmpty()) {
    // What arrived is kept even if the sync did not finish; the owner decides
    // how to merge it.
    if (ok || !finished.songs.isEmpty())
      emit songsReady(finished.key, source.value("title").toString(), finished.songs, complete);
    QVariantMap changes{{"status", ok || cancelled ? "" : "error"}, {"count", finished.songs.size()}};
    if (ok) changes["syncedAt"] = QDateTime::currentSecsSinceEpoch();
    changes["message"] = ok ? (complete ? QString() : QString("Stopped after %1 songs; refresh for the rest.").arg(finished.songs.size()))
                            : cancelled ? QStringLiteral("Cancelled.") : problem;
    setSource(finished.key, changes);
    saveSources();
  }
  releaseIfIdle();
}

void Telegram::abandonJobs(const QString &message) {
  auto jobs = std::move(m_jobs);
  m_jobs.clear();
  for (auto it = jobs.begin(); it != jobs.end(); ++it) {
    const auto source = sourceFor(it->key);
    if (source.isEmpty()) continue;
    if (!it->songs.isEmpty()) emit songsReady(it->key, source.value("title").toString(), it->songs, false);
    setSource(it->key, {{"status", "error"}, {"message", message}, {"count", it->songs.size()}});
  }
}

// --- Lists to choose from -------------------------------------------------

void Telegram::loadChats() {
  m_listError.clear();
  ++m_listing;
  emit listChanged();
  whenReady([this] {
    send("dialogs", {}, [this](const QJsonObject &reply) {
      if (m_listing > 0) --m_listing;
      if (reply.value("ok").toBool()) {
        m_chats.clear();
        for (const auto &value : reply.value("result").toObject().value("dialogs").toArray()) {
          auto chat = value.toObject().toVariantMap();
          chat["chatId"] = idText(value.toObject().value("chatId"));
          m_chats.append(chat);
        }
      } else {
        m_listError = reply.value("error").toString();
      }
      emit listChanged();
      releaseIfIdle();
    });
  });
}

void Telegram::loadTopics(const QString &chatId) {
  m_listError.clear();
  m_topics.clear();
  ++m_listing;
  emit listChanged();
  whenReady([this, chatId] {
    send("topics", {{"chatId", chatId}}, [this](const QJsonObject &reply) {
      if (m_listing > 0) --m_listing;
      if (reply.value("ok").toBool()) {
        m_topics.clear();
        for (const auto &value : reply.value("result").toObject().value("topics").toArray()) {
          auto topic = value.toObject().toVariantMap();
          topic["topicId"] = idText(value.toObject().value("topicId"));
          m_topics.append(topic);
        }
      } else {
        m_listError = reply.value("error").toString();
      }
      emit listChanged();
      releaseIfIdle();
    });
  });
}

void Telegram::clearTopics() {
  m_topics.clear();
  emit listChanged();
}

void Telegram::search(const QString &query) {
  m_listError.clear();
  m_found.clear();
  ++m_listing;
  emit listChanged();
  whenReady([this, query] {
    send("resolve", {{"query", query}}, [this](const QJsonObject &reply) {
      if (m_listing > 0) --m_listing;
      if (reply.value("ok").toBool()) {
        const auto chat = reply.value("result").toObject().value("chat").toObject();
        m_found = chat.toVariantMap();
        m_found["chatId"] = idText(chat.value("chatId"));
      } else {
        m_listError = reply.value("error").toString();
      }
      emit listChanged();
      releaseIfIdle();
    });
  });
}

void Telegram::clearFound() {
  m_found.clear();
  m_listError.clear();
  emit listChanged();
}

// --- Output from the helper -----------------------------------------------

void Telegram::readOutput() {
  if (!m_process) return;
  m_buffer += m_process->readAllStandardOutput();
  int end;
  while ((end = m_buffer.indexOf('\n')) >= 0) {
    const auto line = m_buffer.left(end);
    m_buffer.remove(0, end + 1);
    const auto document = QJsonDocument::fromJson(line);
    if (document.isObject()) handle(document.object());
  }
}

void Telegram::handle(const QJsonObject &message) {
  if (message.contains("id")) {
    auto callback = m_callbacks.take(message.value("id").toInt());
    if (callback) {
      callback(message);
      return;
    }
    if (m_pending > 0) --m_pending;
    m_error = message.value("ok").toBool() ? QString() : message.value("error").toString();
    emit changed();
    return;
  }
  const auto event = message.value("event").toString();
  if (event == "progress" && message.contains("received")) {
    const int job = message.value("job").toInt();
    if (!m_fetches.contains(job)) return;
    auto &entry = m_fetches[job];
    entry.received = qint64(message.value("received").toDouble());
    entry.total = qint64(message.value("total").toDouble());
    setDownload();
    return;
  }
  if (event == "songs" || event == "progress" || event == "wait") {
    const int job = message.value("job").toInt();
    if (!m_jobs.contains(job)) return;
    auto &entry = m_jobs[job];
    const auto source = sourceFor(entry.key);
    if (event == "songs") {
      for (const auto &song : message.value("songs").toArray()) entry.songs.append(songRow(source, song.toObject()));
      setSource(entry.key, {{"count", entry.songs.size()}});
    } else if (event == "wait") {
      const int seconds = message.value("seconds").toInt();
      setSource(entry.key, seconds > 0
        ? QVariantMap{{"status", "waiting"}, {"message", QString("Telegram asked for a pause of %1 s; continuing by itself.").arg(seconds)}}
        : QVariantMap{{"status", "syncing"}, {"message", ""}});
    }
    return;
  }
  if (event != "state") return;
  m_startTimeout.stop();
  m_crashes = 0;
  const auto state = message.value("state").toString();
  m_codeKind = message.value("codeKind").toString();
  m_hint = message.value("passwordHint").toString();
  if (state == "ready") {
    m_name = message.value("name").toString();
    m_username = message.value("username").toString();
    m_userId = idText(message.value("userId"));
  } else if (state == "wait-phone") {
    // Signed out, whether by asking to or because Telegram ended the session.
    if (!m_name.isEmpty()) clearCache();
    m_name.clear();
    m_username.clear();
    m_userId.clear();
  }
  set(state, message.value("error").toString());
  if (state == "ready") {
    auto actions = std::move(m_afterReady);
    m_afterReady.clear();
    for (auto &action : actions) action();
  } else if (state == "wait-phone" || state == "offline") {
    if (!m_afterReady.isEmpty() || m_listing > 0)
      failWaiting(state == "offline" ? message.value("error").toString() : QStringLiteral("Sign in to Telegram first."));
  }
}

// --- Songs, whole -----------------------------------------------------------

void Telegram::setCacheDirectory(const QString &directory) {
  m_cacheDirectory = directory;
  QDir().mkpath(directory);
  QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
  // A download that was cut off by the app closing leaves its half behind.
  for (const auto &name : QDir(directory).entryList({"*.part"}, QDir::Files)) QFile::remove(directory + "/" + name);
}

QString Telegram::cached(const QString &songId) const {
  if (m_cacheDirectory.isEmpty() || songId.isEmpty()) return {};
  for (const auto &name : QDir(m_cacheDirectory).entryList({songId + ".*"}, QDir::Files))
    if (!name.endsWith(".part")) return m_cacheDirectory + "/" + name;
  return {};
}

bool Telegram::fetching(const QString &songId) const {
  for (const auto &entry : m_fetches)
    if (entry.song.value("id") == songId) return true;
  return false;
}

void Telegram::setDownload() {
  QVariantMap next;
  for (const auto &entry : m_fetches)
    if (entry.started && entry.done) {
      next = {{"id", entry.song.value("id")}, {"received", entry.received}, {"total", entry.total ? entry.total : entry.song.value("size").toLongLong()}};
      break;
    }
  if (next == m_download) return;
  m_download = next;
  emit downloadChanged();
}

void Telegram::fetch(const QVariantMap &song, Fetched done) {
  const auto id = song.value("id").toString();
  const auto remote = song.value("remoteId").toString();
  const auto colon = remote.lastIndexOf(':');
  if (id.isEmpty() || colon <= 0 || m_cacheDirectory.isEmpty()) {
    if (done) done({}, "This Telegram song can’t be found again. Refresh its playlist.");
    return;
  }
  if (const auto path = cached(id); !path.isEmpty()) {
    // Used now, so it is the last to go.
    if (QFile used(path); used.open(QIODevice::ReadOnly)) used.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
    if (done) done({{"file", path}, {"cached", true}}, {});
    return;
  }
  for (auto it = m_fetches.begin(); it != m_fetches.end(); ++it)
    if (it->song.value("id") == id) {
      if (done) it->done = std::move(done);
      setDownload();
      return;
    }
  const int job = m_nextJob++;
  m_fetches.insert(job, Fetch{song, std::move(done), false, 0, 0});
  whenReady([this, job] { startFetch(job); });
}

void Telegram::prefetch(const QVariantMap &song) { fetch(song, {}); }

void Telegram::startFetch(int job) {
  auto it = m_fetches.find(job);
  if (it == m_fetches.end()) return;
  const auto remote = it->song.value("remoteId").toString();
  const auto colon = remote.lastIndexOf(':');
  it->started = true;
  setDownload();
  send("fetch", {{"chatId", remote.left(colon)}, {"messageId", remote.mid(colon + 1)}, {"name", it->song.value("id").toString()},
                 {"directory", m_cacheDirectory}, {"limit", double(MaxSongBytes)}, {"job", job}},
       [this, job](const QJsonObject &reply) { finishFetch(job, reply); });
}

void Telegram::finishFetch(int job, const QJsonObject &reply) {
  if (!m_fetches.contains(job)) return; // cancelled meanwhile
  auto finished = m_fetches.take(job);
  setDownload();
  const auto id = finished.song.value("id").toString();
  QString error;
  QVariantMap data;
  if (reply.value("ok").toBool()) {
    const auto result = reply.value("result").toObject();
    const QFileInfo file(result.value("file").toString());
    // The helper is trusted with the account, not with where files go.
    if (file.isFile() && file.canonicalPath() == QFileInfo(m_cacheDirectory).canonicalFilePath()) {
      data = {{"file", file.absoluteFilePath()}, {"cached", false}, {"tags", result.value("tags").toObject().toVariantMap()}};
      learn(finished.song, result.value("tags").toObject());
      evict(id);
    } else {
      error = "Telegram didn’t deliver the song. Try again.";
    }
  } else {
    error = reply.value("error").toString();
  }
  releaseSoon();
  if (finished.done) finished.done(data, error);
}

void Telegram::cancelFetch(const QString &songId) {
  for (auto it = m_fetches.begin(); it != m_fetches.end(); ++it) {
    if (it->song.value("id") != songId) continue;
    const bool started = it->started;
    const int job = it.key();
    m_fetches.erase(it);
    // Queued behind the session: the action finds nothing to start. Running:
    // the helper stops reading and removes its half-file.
    if (started) send("cancel", {{"job", job}}, {});
    setDownload();
    if (started) releaseSoon(); // the next song is likely to want the session at once
    else releaseIfIdle();
    return;
  }
}

// Fetches that cannot go on tell whoever waits; a prefetch has nobody to tell.
void Telegram::failFetches(bool startedToo, const QString &message) {
  QList<Fetched> waiting;
  for (auto it = m_fetches.begin(); it != m_fetches.end();) {
    if (it->started && !startedToo) {
      ++it;
      continue;
    }
    if (it->done) waiting.append(it->done);
    it = m_fetches.erase(it);
  }
  setDownload();
  for (auto &done : waiting) done({}, message);
}

void Telegram::releaseSoon() {
  if (m_holders > 0 || !m_jobs.isEmpty()) return;
  m_linger.start();
}

// Fill what the sync could not know. A plain document carries no duration and
// no artist, and its title is its file name; the file's own tags usually do.
void Telegram::learn(const QVariantMap &song, const QJsonObject &tags) {
  QVariantMap changes;
  const auto seconds = tags.value("seconds").toInt();
  if (song.value("seconds").toInt() <= 0 && seconds > 0) changes["seconds"] = seconds;
  const auto artist = tags.value("artist").toString();
  if (song.value("artist").toString().isEmpty() && !artist.isEmpty()) changes["artist"] = artist;
  const auto title = tags.value("title").toString();
  const auto current = song.value("title").toString();
  const auto stem = QFileInfo(song.value("fileName").toString()).completeBaseName();
  if (!title.isEmpty() && (current == "Untitled" || (!stem.isEmpty() && current == stem))) changes["title"] = title;
  if (changes.isEmpty()) return;
  const auto id = song.value("id").toString();
  auto kept = m_tags.value(id);
  for (auto it = changes.begin(); it != changes.end(); ++it) kept[it.key()] = it.value();
  m_tags.insert(id, kept);
  saveTags();
  emit songLearned(id, changes);
}

void Telegram::loadTags() {
  QFile file(m_directory + "/tags.json");
  if (!file.open(QIODevice::ReadOnly)) return;
  const auto all = QJsonDocument::fromJson(file.readAll()).object();
  for (auto it = all.begin(); it != all.end(); ++it) m_tags.insert(it.key(), it.value().toObject().toVariantMap());
}

void Telegram::saveTags() const {
  QDir().mkpath(m_directory);
  QJsonObject all;
  for (auto it = m_tags.begin(); it != m_tags.end(); ++it) all.insert(it.key(), QJsonObject::fromVariantMap(it.value()));
  QSaveFile file(m_directory + "/tags.json");
  if (!file.open(QIODevice::WriteOnly)) return;
  file.write(QJsonDocument(all).toJson(QJsonDocument::Compact));
  if (file.commit()) QFile::setPermissions(m_directory + "/tags.json", QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

// Oldest first out, until both the song count and the size are within bounds;
// what is playing, what plays next and what just arrived stay whatever the sum.
void Telegram::evict(const QString &alsoKeep) {
  if (m_cacheDirectory.isEmpty()) return;
  int songs = 0;
  qint64 bytes = 0;
  for (const auto &info : QDir(m_cacheDirectory).entryInfoList(QDir::Files, QDir::Time)) { // newest first
    if (info.suffix() == "part") continue;
    const bool kept = info.completeBaseName() == alsoKeep || m_keep.contains(info.completeBaseName());
    if (!kept && (songs >= m_cacheSongs || bytes + info.size() > m_cacheBytes)) {
      QFile::remove(info.absoluteFilePath());
      continue;
    }
    ++songs;
    bytes += info.size();
  }
}

// The settings' "Clear cache": the downloaded songs go, except the ones playing
// or next, which the player has open. What the files taught is kept.
void Telegram::clearAudio() {
  if (m_cacheDirectory.isEmpty()) return;
  for (const auto &info : QDir(m_cacheDirectory).entryInfoList(QDir::Files))
    if ((info.suffix() == "part" && m_fetches.isEmpty()) || (info.suffix() != "part" && !m_keep.contains(info.completeBaseName()))) QFile::remove(info.absoluteFilePath());
}

// Signing out: nothing of the account stays on this Mac.
void Telegram::clearCache() {
  if (!m_cacheDirectory.isEmpty())
    for (const auto &name : QDir(m_cacheDirectory).entryList(QDir::Files)) QFile::remove(m_cacheDirectory + "/" + name);
  if (!m_tags.isEmpty()) {
    m_tags.clear();
    QFile::remove(m_directory + "/tags.json");
  }
}
