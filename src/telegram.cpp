#include "telegram.h"
#include <QStorageInfo>
#include "telegramtext.h"
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
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>
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
  loadNoArt();
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
  m_streamId.clear();
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
      const auto chat = m_sources[i].toMap().value("chatId").toString();
      m_sources.removeAt(i);
      // A chat's pictures go with its last source: a forum's topics share them.
      bool shared = false;
      for (const auto &other : m_sources) shared = shared || other.toMap().value("chatId") == chat;
      if (!shared) {
        // Its songs are gone from the library, and what was kept of them goes too.
        for (const auto &id : m_pinned.keys())
          if (m_pinned.value(id).value("telegramChat") == chat) unpin(id, true);
        m_artQueue.erase(std::remove_if(m_artQueue.begin(), m_artQueue.end(), [&](const ArtTask &t) { return t.chatId == chat; }), m_artQueue.end());
        if (!chat.isEmpty() && !m_artDirectory.isEmpty()) QDir(chatFolder(chat)).removeRecursively();
      }
      saveSources();
      emit sourcesChanged();
      emit sourceRemoved(key);
      break;
    }
}

void Telegram::refresh(const QString &key, bool full) {
  const auto source = sourceFor(key);
  const auto status = source.value("status").toString();
  if (source.isEmpty() || status == "queued" || status == "syncing" || status == "waiting") return;
  if (full) m_fullSync.insert(key);
  setSource(key, {{"status", "queued"}, {"message", ""}});
  whenReady([this, key] { startSync(key); });
}

void Telegram::refreshAll(bool full) {
  for (const auto &source : m_sources) refresh(source.toMap().value("key").toString(), full);
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
                  {"fileName", song.value("fileName").toString()},
                  {"thumb", song.value("thumb").toBool()}};
  if (const auto picture = artFile(chat, row.value("id").toString()); !picture.isEmpty()) row["art"] = artUrl(picture);
  // What a file said about itself when it was played stays, through a refresh
  // that only knows what the message carried.
  const auto learned = m_tags.value(row.value("id").toString());
  for (auto it = learned.begin(); it != learned.end(); ++it) row[it.key()] = it.value();
  // A title that is the file's name is still the file's name once it is cleaned; learn() looks for that.
  const auto rawTitle = song.value("title").toString();
  if (rawTitle == "Untitled" || rawTitle == QFileInfo(song.value("fileName").toString()).completeBaseName()) row["titleFromName"] = true;
  const auto cleaned = telegramtext::cleanSong(row.value("title").toString(), row.value("artist").toString());
  row["title"] = cleaned.title;
  row["artist"] = cleaned.artist;
  if (const auto topic = source.value("topicId").toString(); !topic.isEmpty() && topic != "0") row["telegramTopic"] = topic;
  return row;
}

void Telegram::startSync(const QString &key) {
  const auto source = sourceFor(key);
  if (source.isEmpty() || source.value("status") != "queued") return;
  const int job = m_nextJob++;
  // Newer messages only, when the last sync ran to its end under today's rules.
  const bool forced = m_fullSync.remove(key);
  const qint64 last = source.value("lastMessageId").toLongLong();
  const bool incremental = !forced && last > 0 && source.value("syncVersion").toInt() == syncVersion;
  const int base = incremental ? source.value("count").toInt() : 0;
  m_jobs.insert(job, Job{key, {}, incremental, base});
  setSource(key, {{"status", "syncing"}, {"message", ""}, {"count", base}, {"added", 0}, {"incremental", incremental}});
  send("sync", {{"chatId", source.value("chatId").toString()}, {"topicId", source.value("topicId").toString()}, {"job", job},
                {"minId", incremental ? double(last) : 0.0}},
       [this, job](const QJsonObject &reply) { finishJob(job, reply); });
}

void Telegram::finishJob(int job, const QJsonObject &reply) {
  if (!m_jobs.contains(job)) return;
  const auto finished = m_jobs.take(job);
  const auto source = sourceFor(finished.key);
  const bool ok = reply.value("ok").toBool();
  const auto result = reply.value("result").toObject();
  const bool ranToEnd = ok && result.value("complete").toBool(true);
  // An incremental run only knows what is new, so its list is merged, never taken for the whole source.
  const bool complete = ranToEnd && !finished.incremental;
  const auto problem = reply.value("error").toString();
  const bool cancelled = !ok && problem == "Cancelled.";
  if (!source.isEmpty()) {
    // What arrived is kept even if the sync did not finish; the owner decides
    // how to merge it.
    if (ok || !finished.songs.isEmpty())
      emit songsReady(finished.key, source.value("title").toString(), finished.songs, complete, finished.incremental);
    if (ok || !finished.songs.isEmpty()) {
      const auto chat = source.value("chatId").toString();
      // Song pictures wait for the list to show them (wantArt); the chat's
      // own cover is the one thing a sync asks for.
      queueSourceArt(chat);
    }
    QVariantMap changes{{"status", ok || cancelled ? "" : "error"}, {"count", finished.base + finished.songs.size()},
                        {"added", finished.songs.size()}, {"incremental", false}};
    if (ok) changes["syncedAt"] = QDateTime::currentSecsSinceEpoch();
    // The newest message seen is what the next refresh asks past, but only after a run that reached the
    // end: a cancelled or capped one has skipped messages in between. A capped run starts over next time.
    if (ranToEnd) {
      changes["lastMessageId"] = qMax(qint64(result.value("maxId").toDouble()), finished.incremental ? source.value("lastMessageId").toLongLong() : 0);
      changes["syncVersion"] = syncVersion;
    } else if (ok) {
      changes["lastMessageId"] = 0;
    }
    changes["message"] = ok ? (ranToEnd ? QString() : QString("Stopped after %1 songs; refresh for the rest.").arg(finished.songs.size()))
                            : cancelled ? QStringLiteral("Cancelled.") : problem;
    setSource(finished.key, changes);
    saveSources();
    if (ok) retryOffline(); // Telegram answered, so songs that could not be had are worth another try
  }
  releaseIfIdle();
}

void Telegram::abandonJobs(const QString &message) {
  auto jobs = std::move(m_jobs);
  m_jobs.clear();
  for (auto it = jobs.begin(); it != jobs.end(); ++it) {
    const auto source = sourceFor(it->key);
    if (source.isEmpty()) continue;
    if (!it->songs.isEmpty()) emit songsReady(it->key, source.value("title").toString(), it->songs, false, it->incremental);
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
      setSource(entry.key, {{"count", entry.base + entry.songs.size()}, {"added", entry.songs.size()}});
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
  if (songId.isEmpty()) return {};
  for (const auto &directory : {m_cacheDirectory, m_offlineDirectory}) {
    if (directory.isEmpty()) continue;
    for (const auto &name : QDir(directory).entryList({songId + ".*"}, QDir::Files))
      if (!name.endsWith(".part")) return directory + "/" + name;
  }
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
    if (entry.started && entry.done && !entry.streaming) {
      next = {{"id", entry.song.value("id")}, {"received", entry.received}, {"total", entry.total ? entry.total : entry.song.value("size").toLongLong()}};
      break;
    }
  if (next == m_download) return;
  m_download = next;
  emit downloadChanged();
}

void Telegram::fetch(const QVariantMap &song, Fetched done, Streamed stream) {
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
      // A download that was started to be streamed can be asked for its URL
      // again (the player lost it); a plain one is waited for.
      if (stream && it->streamable) {
        it->streaming = true;
        if (it->started) requestStream(it.key(), std::move(stream));
        else it->stream = std::move(stream);
      }
      setDownload();
      return;
    }
  const int job = m_nextJob++;
  Fetch entry{song, std::move(done), false, 0, 0, {}, false, false};
  entry.streamable = entry.streaming = bool(stream);
  entry.stream = std::move(stream);
  m_fetches.insert(job, std::move(entry));
  whenReady([this, job] { startFetch(job); });
}

// Asks the helper for the URL of a download it is running in blocks. If it
// cannot be had the download goes on as an ordinary one and `done` is the way
// the song arrives.
void Telegram::requestStream(int job, Streamed stream) {
  send("stream", {{"fetchJob", job}}, [this, job, stream = std::move(stream)](const QJsonObject &reply) {
    auto it = m_fetches.find(job);
    if (it == m_fetches.end()) return;
    const auto result = reply.value("result").toObject();
    const QUrl url(result.value("url").toString());
    if (!reply.value("ok").toBool() || url.host() != "127.0.0.1") {
      it->streaming = it->streamable = false;
      setDownload();
      return;
    }
    m_streamId = it->song.value("id").toString();
    setDownload();
    stream(url.toString(), qint64(result.value("size").toDouble()));
  });
}

// The helper has to stay up for as long as the player reads from its URL, which
// is long after the download is over.
void Telegram::releaseStream() {
  if (m_streamId.isEmpty()) return;
  m_streamId.clear();
  releaseSoon();
}

void Telegram::prefetch(const QVariantMap &song) { fetch(song, {}); }

void Telegram::startFetch(int job) {
  auto it = m_fetches.find(job);
  if (it == m_fetches.end()) return;
  const auto remote = it->song.value("remoteId").toString();
  const auto colon = remote.lastIndexOf(':');
  it->started = true;
  setDownload();
  QJsonObject request{{"chatId", remote.left(colon)}, {"messageId", remote.mid(colon + 1)}, {"name", it->song.value("id").toString()},
                      {"directory", it->offline ? m_offlineDirectory : m_cacheDirectory}, {"limit", double(MaxSongBytes)}, {"job", job}};
  // The picture inside the file, if it has one and the song has none yet.
  const auto chat = it->song.value("telegramChat", remote.left(colon)).toString();
  if (!m_artDirectory.isEmpty() && artFile(chat, it->song.value("id").toString()).isEmpty()) {
    ensureFolder(chatFolder(chat));
    request.insert("artDirectory", chatFolder(chat));
    request.insert("artName", it->song.value("id").toString());
  }
  if (it->streamable) request.insert("stream", true);
  send("fetch", request,
       [this, job](const QJsonObject &reply) { finishFetch(job, reply); });
  if (auto again = m_fetches.find(job); again != m_fetches.end() && again->stream) requestStream(job, std::move(again->stream));
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
    if (file.isFile() && file.canonicalPath() == QFileInfo(finished.offline ? m_offlineDirectory : m_cacheDirectory).canonicalFilePath()) {
      data = {{"file", file.absoluteFilePath()}, {"cached", false}, {"tags", result.value("tags").toObject().toVariantMap()}};
      learn(finished.song, result.value("tags").toObject());
      if (!finished.offline) evict(id);
      const QFileInfo art(result.value("art").toString());
      const auto chat = finished.song.value("telegramChat").toString();
      if (art.isFile() && !m_artDirectory.isEmpty() && art.canonicalPath() == QFileInfo(chatFolder(chat)).canonicalFilePath()) {
        emit songLearned(id, {{"art", artUrl(art.absoluteFilePath())}});
        evictArt();
      } else if (finished.song.value("thumb").toBool() && artFile(chat, id).isEmpty()) {
        // No picture in the file; the message's own thumbnail, before the others.
        queueArt({"thumb", chat, finished.song.value("telegramMessage").toString(), id}, true);
      }
    } else {
      error = "Telegram didn’t deliver the song. Try again.";
    }
  } else {
    error = reply.value("error").toString();
  }
  releaseSoon();
  if (finished.offline) finishOffline(id, error.isEmpty(), error, reply.value("gone").toBool());
  else if (m_pinned.contains(id)) {
    // The player brought down a song that was asked to be kept: keep that copy rather than fetching it twice.
    if (error.isEmpty() && offlineFile(id).isEmpty() &&
        QFile::copy(data.value("file").toString(), m_offlineDirectory + "/" + QFileInfo(data.value("file").toString()).fileName())) {
      m_keepQueue.removeAll(id);
      m_keepFailed.remove(id);
      emit offlineChanged();
    }
    pumpOffline(); // it was waiting for the player's download to be over
  }
  if (finished.done) finished.done(data, error);
}

void Telegram::cancelFetch(const QString &songId) {
  for (auto it = m_fetches.begin(); it != m_fetches.end(); ++it) {
    if (it->song.value("id") != songId) continue;
    if (it->offline) {
      // Kept for later: only the player's interest ends.
      it->done = {};
      it->stream = {};
      it->streaming = false;
      setDownload();
      return;
    }
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
  QStringList gone; // offline downloads that did not arrive
  for (auto it = m_fetches.begin(); it != m_fetches.end();) {
    if (it->started && !startedToo) {
      ++it;
      continue;
    }
    if (it->done) waiting.append(it->done);
    if (it->offline) gone.append(it->song.value("id").toString());
    it = m_fetches.erase(it);
  }
  setDownload();
  for (const auto &id : gone) finishOffline(id, false, message, false);
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
  // The file's own tags carry the same channel marketing the message did.
  const auto fromTags = telegramtext::cleanSong(tags.value("title").toString(), tags.value("artist").toString());
  const auto artist = fromTags.artist;
  if (song.value("artist").toString().isEmpty() && !artist.isEmpty()) changes["artist"] = artist;
  const auto title = tags.value("title").toString().isEmpty() ? QString() : fromTags.title;
  const auto current = song.value("title").toString();
  const auto stem = QFileInfo(song.value("fileName").toString()).completeBaseName();
  if (!title.isEmpty() && (current == "Untitled" || song.value("titleFromName").toBool() || (!stem.isEmpty() && current == stem))) changes["title"] = title;
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
  clearArt();
  // What was kept offline is the account's too.
  const auto pinned = m_pinned.keys();
  if (!pinned.isEmpty() || !m_offlineDirectory.isEmpty()) {
    for (const auto &id : pinned) unpin(id, true);
    if (!m_offlineDirectory.isEmpty())
      for (const auto &name : QDir(m_offlineDirectory).entryList(QDir::Files)) QFile::remove(m_offlineDirectory + "/" + name);
  }
  if (!m_cacheDirectory.isEmpty())
    for (const auto &name : QDir(m_cacheDirectory).entryList(QDir::Files)) QFile::remove(m_cacheDirectory + "/" + name);
  if (!m_tags.isEmpty()) {
    m_tags.clear();
    QFile::remove(m_directory + "/tags.json");
  }
}

// --- Kept offline -------------------------------------------------------------
//
// A song the person pinned is brought down whole into its own folder, which the
// cache's trimming never looks at. Downloads go one at a time through the same
// fetch as playing, so the player can start a song that is still coming down.

void Telegram::setOfflineDirectory(const QString &directory) {
  m_offlineDirectory = directory;
  ensureFolder(directory);
  for (const auto &name : QDir(directory).entryList({"*.part"}, QDir::Files)) QFile::remove(directory + "/" + name);
  loadOffline();
  m_keepQueue.clear();
  for (auto it = m_pinned.begin(); it != m_pinned.end(); ++it)
    if (offlineFile(it.key()).isEmpty()) m_keepQueue.append(it.key());
  if (!m_keepQueue.isEmpty()) QTimer::singleShot(0, this, [this] { pumpOffline(); });
}

QString Telegram::offlineFile(const QString &songId) const {
  if (m_offlineDirectory.isEmpty() || songId.isEmpty()) return {};
  for (const auto &name : QDir(m_offlineDirectory).entryList({songId + ".*"}, QDir::Files))
    if (!name.endsWith(".part")) return m_offlineDirectory + "/" + name;
  return {};
}

bool Telegram::keptOffline(const QString &songId) const { return m_pinned.contains(songId) && !offlineFile(songId).isEmpty(); }

QString Telegram::offlineState(const QString &songId) const {
  if (!m_pinned.contains(songId)) return {};
  if (!offlineFile(songId).isEmpty()) return "ready";
  if (m_keepActive == songId) return "downloading";
  return m_keepFailed.contains(songId) ? "failed" : "queued";
}

QVariantMap Telegram::offline() const {
  QVariantMap states;
  for (auto it = m_pinned.begin(); it != m_pinned.end(); ++it) states.insert(it.key(), offlineState(it.key()));
  return states;
}

void Telegram::loadOffline() {
  m_pinned.clear();
  QFile file(m_directory + "/offline.json");
  if (!file.open(QIODevice::ReadOnly)) return;
  const auto all = QJsonDocument::fromJson(file.readAll()).object();
  for (auto it = all.begin(); it != all.end(); ++it) m_pinned.insert(it.key(), it.value().toObject().toVariantMap());
}

void Telegram::saveOffline() const {
  QDir().mkpath(m_directory);
  QJsonObject all;
  for (auto it = m_pinned.begin(); it != m_pinned.end(); ++it) all.insert(it.key(), QJsonObject::fromVariantMap(it.value()));
  QSaveFile file(m_directory + "/offline.json");
  if (!file.open(QIODevice::WriteOnly)) return;
  file.write(QJsonDocument(all).toJson(QJsonDocument::Compact));
  if (file.commit()) QFile::setPermissions(m_directory + "/offline.json", QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

bool Telegram::keepOffline(const QVariantList &songs) {
  if (m_offlineDirectory.isEmpty()) return false;
  if (QStorageInfo(m_offlineDirectory).bytesAvailable() < m_minFreeBytes) {
    emit offlineMessage("Not enough free disk space to keep songs offline; 500 MB must stay free.");
    return false;
  }
  bool added = false;
  for (const auto &entry : songs) {
    const auto song = entry.toMap();
    const auto id = song.value("id").toString();
    if (id.isEmpty() || song.value("source") != "telegram" || song.value("remoteId").toString().lastIndexOf(':') <= 0) continue;
    if (m_pinned.contains(id)) {
      // Pinned already: asking again is how a failed one is tried again.
      if (m_keepFailed.remove(id) && offlineFile(id).isEmpty() && !m_keepQueue.contains(id)) m_keepQueue.append(id);
      added = true;
      continue;
    }
    QVariantMap kept;
    for (const auto *key : {"id", "source", "kind", "server", "remoteId", "title", "artist", "album", "seconds", "mime", "size", "added",
                            "telegramChat", "telegramMessage", "telegramTopic", "fileName", "thumb"})
      if (song.contains(key)) kept.insert(key, song.value(key));
    m_pinned.insert(id, kept);
    added = true;
    if (!offlineFile(id).isEmpty()) continue;
    // The player may already have it in the cache; copy it rather than ask Telegram again.
    if (const auto path = cached(id); !path.isEmpty() && QFile::copy(path, m_offlineDirectory + "/" + QFileInfo(path).fileName())) continue;
    m_keepQueue.append(id);
  }
  if (!added) return true;
  saveOffline();
  emit offlineChanged();
  pumpOffline();
  return true;
}

void Telegram::stopKeepingOffline(const QStringList &ids) {
  for (const auto &id : ids) unpin(id, true);
  pumpOffline();
}

void Telegram::unpin(const QString &songId, bool removeFile) {
  const bool had = m_pinned.remove(songId) > 0;
  m_keepQueue.removeAll(songId);
  m_keepFailed.remove(songId);
  if (m_keepActive == songId) {
    for (auto it = m_fetches.begin(); it != m_fetches.end(); ++it)
      if (it->offline && it->song.value("id") == songId) {
        // Nobody waits for it now, but a player that does is told it did not come.
        const auto done = it->done;
        const int job = it.key();
        const bool started = it->started;
        m_fetches.erase(it);
        if (started) send("cancel", {{"job", job}}, {});
        setDownload();
        if (done) done({}, "Cancelled.");
        break;
      }
    m_keepActive.clear();
  }
  if (removeFile && !m_offlineDirectory.isEmpty())
    for (const auto &name : QDir(m_offlineDirectory).entryList({songId + ".*"}, QDir::Files)) QFile::remove(m_offlineDirectory + "/" + name);
  if (!had) return;
  saveOffline();
  emit offlineChanged();
}

void Telegram::retryOffline() {
  bool any = false;
  for (const auto &id : m_keepFailed) {
    if (m_pinned.contains(id) && offlineFile(id).isEmpty() && !m_keepQueue.contains(id)) m_keepQueue.append(id);
    any = true;
  }
  m_keepFailed.clear();
  if (any) emit offlineChanged();
  pumpOffline();
}

// The next song to keep, if nothing is being kept.
void Telegram::pumpOffline() {
  if (!m_keepActive.isEmpty() || m_keepQueue.isEmpty() || m_offlineDirectory.isEmpty()) return;
  if (QStorageInfo(m_offlineDirectory).bytesAvailable() < m_minFreeBytes) {
    // What had not come down is let go, rather than left to fill the disk later.
    const auto waiting = m_keepQueue;
    for (const auto &id : waiting) unpin(id, true);
    emit offlineMessage("Not enough free disk space to keep the rest offline; 500 MB must stay free.");
    return;
  }
  // A song the player is already bringing down into the cache is kept from that copy when it arrives.
  QString next;
  for (const auto &id : m_keepQueue)
    if (!fetching(id)) {
      next = id;
      break;
    }
  if (next.isEmpty()) return;
  m_keepQueue.removeAll(next);
  m_keepActive = next;
  const auto song = m_pinned.value(next);
  const int job = m_nextJob++;
  Fetch entry{song, {}, false, 0, 0, {}, false, false};
  entry.streamable = true; // a player that wants it meanwhile is given its blocks
  entry.offline = true;
  m_fetches.insert(job, std::move(entry));
  emit offlineChanged();
  whenReady([this, job] { startFetch(job); });
}

void Telegram::finishOffline(const QString &songId, bool ok, const QString &error, bool gone) {
  if (m_keepActive == songId) m_keepActive.clear();
  if (!m_pinned.contains(songId)) {
    pumpOffline();
    return;
  }
  const auto title = m_pinned.value(songId).value("title").toString();
  if (ok) {
    m_keepFailed.remove(songId);
    emit offlineChanged();
    pumpOffline();
    return;
  }
  if (gone) {
    // Deleted in Telegram before it was ever downloaded: there is nothing to keep.
    unpin(songId, true);
    emit offlineMessage(QString("“%1” is no longer in its chat, so it can’t be kept offline.").arg(title));
    pumpOffline();
    return;
  }
  // Pinned still, and tried again by retryOffline(); the rest of the queue waits with it.
  m_keepFailed.insert(songId);
  emit offlineChanged();
  emit offlineMessage(QString("Couldn’t keep “%1” offline: %2").arg(title, error));
}

// --- Cover art ----------------------------------------------------------------

void Telegram::setArtDirectory(const QString &directory) {
  m_artDirectory = directory;
  ensureFolder(directory);
  for (const auto &chat : QDir(directory).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
    for (const auto &name : QDir(directory + "/" + chat).entryList({"*.part"}, QDir::Files)) QFile::remove(directory + "/" + chat + "/" + name);
}

void Telegram::ensureFolder(const QString &path) const {
  QDir().mkpath(path);
  QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

// A chat id is digits with a leading minus; the folder name keeps to letters.
QString Telegram::chatFolder(const QString &chatId) const {
  auto name = chatId;
  name.replace('-', 'n');
  name.remove(QRegularExpression("[^A-Za-z0-9]"));
  return m_artDirectory + "/c" + name;
}

QString Telegram::artFile(const QString &chatId, const QString &name) const {
  if (m_artDirectory.isEmpty() || chatId.isEmpty() || name.isEmpty()) return {};
  for (const char *extension : {".jpg", ".png", ".webp"}) {
    const auto path = chatFolder(chatId) + "/" + name + extension;
    if (QFileInfo(path).isFile()) return path;
  }
  return {};
}

// The modification time rides along so a picture that was replaced under the
// same name is drawn again instead of coming from the image cache.
QString Telegram::artUrl(const QString &path) {
  QUrl url = QUrl::fromLocalFile(path);
  url.setQuery(QString::number(QFileInfo(path).lastModified().toMSecsSinceEpoch()));
  return url.toString();
}

QString Telegram::coverUrl(const QString &chatId) const {
  const auto path = artFile(chatId, "cover");
  return path.isEmpty() ? QString() : artUrl(path);
}

void Telegram::queueSourceArt(const QString &chatId) {
  const auto path = artFile(chatId, "cover");
  // A photo is asked for again after a week, in case the chat changed it.
  if (!path.isEmpty() && QFileInfo(path).lastModified().daysTo(QDateTime::currentDateTime()) < 7) return;
  queueArt({"photo", chatId, {}, "cover"});
}

void Telegram::queueArt(const ArtTask &task, bool first) {
  if (m_artDirectory.isEmpty()) return;
  const auto identity = task.chatId + "/" + task.name;
  if (m_artFailed.contains(identity) || (m_artBusy && m_artRunning.chatId == task.chatId && m_artRunning.name == task.name)) return;
  for (const auto &queued : m_artQueue)
    if (queued.chatId == task.chatId && queued.name == task.name) return;
  if (first) m_artQueue.prepend(task);
  else m_artQueue.append(task);
  pumpArt();
}

void Telegram::wantArt(const QVariantList &rows) {
  if (m_artDirectory.isEmpty() || !signedIn()) return;
  QList<ArtTask> wanted;
  for (const auto &value : rows) {
    const auto row = value.toMap();
    const auto id = row.value("id").toString(), chat = row.value("telegramChat").toString(), message = row.value("telegramMessage").toString();
    if (row.value("source") != "telegram" || id.isEmpty() || chat.isEmpty() || message.isEmpty() || m_noArt.contains(id)) continue;
    // A picture evicted since the row learned of it is wanted again; one on
    // disk that the row does not show yet is simply handed over.
    if (const auto art = QUrl(row.value("art").toString()); art.isLocalFile() && QFileInfo::exists(art.toLocalFile())) continue;
    if (const auto have = artFile(chat, id); !have.isEmpty()) {
      emit songLearned(id, {{"art", artUrl(have)}});
      continue;
    }
    wanted.append({"thumb", chat, message, id});
  }
  // What was waiting for its turn and is no longer on screen is let go.
  m_artQueue.erase(std::remove_if(m_artQueue.begin(), m_artQueue.end(), [](const ArtTask &t) { return t.kind == "thumb"; }), m_artQueue.end());
  for (const auto &task : wanted) queueArt(task);
}

void Telegram::rememberNoArt(const QString &id) {
  if (m_noArt.contains(id)) return;
  m_noArt.insert(id);
  m_noArtOrder.append(id);
  while (m_noArtOrder.size() > 20000) m_noArt.remove(m_noArtOrder.takeFirst());
  saveNoArt();
}

void Telegram::loadNoArt() {
  QFile file(m_directory + "/noart.json");
  if (!file.open(QIODevice::ReadOnly)) return;
  for (const auto &id : QJsonDocument::fromJson(file.readAll()).array())
    if (m_noArt.size() < 20000 && !m_noArt.contains(id.toString())) { m_noArt.insert(id.toString()); m_noArtOrder.append(id.toString()); }
}

void Telegram::saveNoArt() const {
  QDir().mkpath(m_directory);
  QSaveFile file(m_directory + "/noart.json");
  if (!file.open(QIODevice::WriteOnly)) return;
  file.write(QJsonDocument(QJsonArray::fromStringList(m_noArtOrder)).toJson(QJsonDocument::Compact));
  if (file.commit()) QFile::setPermissions(m_directory + "/noart.json", QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

// One picture at a time: they are small, and Telegram's limits are on how many
// requests come at once.
void Telegram::pumpArt() {
  if (m_artBusy || m_artQueue.isEmpty()) return;
  m_artBusy = true;
  const auto task = m_artQueue.takeFirst();
  m_artRunning = task;
  whenReady([this, task] {
    ensureFolder(chatFolder(task.chatId));
    send("art", {{"kind", task.kind}, {"chatId", task.chatId}, {"messageId", task.messageId}, {"name", task.name},
                 {"directory", chatFolder(task.chatId)}},
         [this, task](const QJsonObject &reply) {
           m_artBusy = false;
           const QFileInfo file(reply.value("result").toObject().value("file").toString());
           bool known = false;
           for (const auto &source : m_sources) known = known || source.toMap().value("chatId") == task.chatId;
           if (!known) { // the source was removed while this was coming down
             if (file.isFile()) QFile::remove(file.absoluteFilePath());
             QDir().rmdir(chatFolder(task.chatId));
           } else if (reply.value("ok").toBool() && task.kind == "thumb" && file.filePath().isEmpty()) {
             rememberNoArt(task.name); // Telegram and the file both have none: not worth asking again
           } else if (!reply.value("ok").toBool() || !file.isFile() || file.canonicalPath() != QFileInfo(chatFolder(task.chatId)).canonicalFilePath()) {
             m_artFailed.insert(task.chatId + "/" + task.name);
           } else if (task.kind == "photo") {
             emit coverReady(task.chatId, artUrl(file.absoluteFilePath()));
           } else {
             emit songLearned(task.name, {{"art", artUrl(file.absoluteFilePath())}});
             evictArt();
           }
           pumpArt();
           if (!m_artBusy) releaseIfIdle();
         });
  });
}

// Oldest pictures first out. A chat's own cover is not counted: there is one
// per source, and without it the playlist loses its face.
void Telegram::evictArt() {
  if (m_artDirectory.isEmpty()) return;
  QList<QFileInfo> pictures;
  for (const auto &chat : QDir(m_artDirectory).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
    for (const auto &info : QDir(m_artDirectory + "/" + chat).entryInfoList(QDir::Files))
      if (info.suffix() != "part" && info.completeBaseName() != "cover") pictures.append(info);
  std::sort(pictures.begin(), pictures.end(), [](const QFileInfo &a, const QFileInfo &b) { return a.lastModified() > b.lastModified(); });
  int count = 0;
  qint64 bytes = 0;
  for (const auto &info : pictures) {
    if (count >= m_artFiles || bytes + info.size() > m_artBytes) {
      QFile::remove(info.absoluteFilePath());
      continue;
    }
    ++count;
    bytes += info.size();
  }
}

void Telegram::clearArt() {
  m_artQueue.clear();
  m_artFailed.clear();
  if (!m_noArt.isEmpty()) {
    m_noArt.clear();
    m_noArtOrder.clear();
    QFile::remove(m_directory + "/noart.json");
  }
  if (m_artDirectory.isEmpty()) return;
  for (const auto &chat : QDir(m_artDirectory).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) QDir(m_artDirectory + "/" + chat).removeRecursively();
  emit artCleared();
}
