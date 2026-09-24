#include "scrobbler.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

namespace {
// The keyring entry this player keeps its listening token under.
const QStringList tokenAttributes{"application", "sung", "account", "scrobbler"};
QStringList lookupArgs() { return QStringList{"lookup"} + tokenAttributes; }
QStringList storeArgs() {
  return QStringList{"store", "--label=Sung listening token"} + tokenAttributes;
}
QStringList clearArgs() { return QStringList{"clear"} + tokenAttributes; }
} // namespace

Scrobbler::Scrobbler(QObject *parent, bool restore) : QObject(parent) {
  m_retry.setSingleShot(true);
  m_retry.setInterval(60000);
  connect(&m_retry, &QTimer::timeout, this, &Scrobbler::flush);
  loadQueue();
  if (!restore)
    return;
  // The token lives in the keyring, never in the settings file.
  QTimer::singleShot(0, this, [this] {
    if (!enabled())
      return;
    secret(lookupArgs(), {}, [this](bool ok, QByteArray token) {
      if (token.endsWith('\n'))
        token.chop(1);
      if (!ok || token.trimmed().isEmpty()) {
        setStatus(keyringAvailable() ? "Sign in to send your listening history."
                                     : "A system keyring is needed to store the token.");
        return;
      }
      m_token = QString::fromUtf8(token).trimmed();
      m_account = m_settings.value("scrobbler/account").toString();
      setStatus(m_account.isEmpty() ? "Signed in" : "Signed in as " + m_account);
      emit changed();
      flush();
    });
  });
}

Scrobbler::~Scrobbler() {
  for (auto *reply : m_network.findChildren<QNetworkReply *>()) {
    reply->disconnect(this);
    reply->abort();
  }
}

bool Scrobbler::keyringAvailable() const {
#ifdef Q_OS_MACOS
  // secret-tool talks to a Secret Service, which macOS does not have. The
  // Keychain is the answer there, once Musix is signed with a stable identity.
  return false;
#else
  return !QStandardPaths::findExecutable("secret-tool").isEmpty();
#endif
}

qint64 Scrobbler::thresholdFor(qint64 durationMs) {
  if (durationMs < minimumLength)
    return -1;
  return qMin(durationMs / 2, longestWait);
}

QVariantMap Scrobbler::listenFor(const QVariantMap &track, qint64 startedAt) {
  const auto title = track.value("title").toString().trimmed();
  const auto artist = track.value("artist").toString().trimmed();
  // The service requires both, and a listen without them is noise.
  if (title.isEmpty() || artist.isEmpty() || startedAt <= 0)
    return {};
  QVariantMap metadata{{"track_name", title}, {"artist_name", artist}};
  const auto album = track.value("album").toString().trimmed();
  if (!album.isEmpty())
    metadata.insert("release_name", album);
  QVariantMap info{{"media_player", "Musix"}, {"submission_client", "Musix"}};
  const auto duration = track.value("seconds").toLongLong();
  if (duration > 0 && duration <= 86400)
    info.insert("duration", duration);
  metadata.insert("additional_info", info);
  return {{"listened_at", startedAt}, {"track_metadata", metadata}};
}

void Scrobbler::setEnabled(bool on) {
  if (enabled() == on)
    return;
  m_settings.setValue("scrobbler/enabled", on);
  if (!on)
    m_retry.stop();
  else
    flush();
  emit changed();
}

void Scrobbler::setServer(const QString &value) {
  auto trimmed = value.trimmed();
  if (trimmed.isEmpty())
    trimmed = defaultServer;
  const QUrl url(trimmed);
  // Listening history is personal, so it only ever leaves over HTTPS.
  if (!url.isValid() || url.scheme() != "https" || url.host().isEmpty()) {
    setStatus("Enter an https address for the listening server.");
    return;
  }
  while (trimmed.endsWith('/'))
    trimmed.chop(1);
  if (trimmed == server())
    return;
  m_settings.setValue("scrobbler/server", trimmed);
  // A token belongs to one server, so changing servers signs out.
  signOut();
  emit changed();
}

void Scrobbler::signIn(const QString &token) {
  const auto trimmed = token.trimmed();
  if (trimmed.isEmpty()) {
    setStatus("Paste the token from your listening account.");
    return;
  }
  if (!keyringAvailable()) {
    setStatus("A system keyring is needed to store the token.");
    return;
  }
  m_busy = true;
  setStatus("Checking the token…");
  emit changed();
  QNetworkRequest request{QUrl(server() + "/1/validate-token")};
  request.setRawHeader("Authorization", ("Token " + trimmed).toUtf8());
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  auto *reply = m_network.get(request);
  connect(reply, &QNetworkReply::finished, this, [this, reply, trimmed] {
    reply->deleteLater();
    m_busy = false;
    const auto body = QJsonDocument::fromJson(reply->readAll()).object().toVariantMap();
    const bool valid = reply->error() == QNetworkReply::NoError && body.value("valid").toBool();
    if (!valid) {
      const auto message = body.value("message").toString();
      setStatus(reply->error() != QNetworkReply::NoError && body.isEmpty()
                    ? "Could not reach the listening server."
                    : message.isEmpty() ? "That token was not accepted." : message);
      emit changed();
      return;
    }
    m_token = trimmed;
    m_account = body.value("user_name").toString();
    m_settings.setValue("scrobbler/account", m_account);
    m_settings.setValue("scrobbler/enabled", true);
    secret(storeArgs(), trimmed.toUtf8(), [this](bool stored, QByteArray) {
      if (!stored) {
        setStatus("Signed in, but the token could not be stored for next time.");
        emit changed();
      }
    });
    setStatus(m_account.isEmpty() ? "Signed in" : "Signed in as " + m_account);
    emit changed();
    flush();
  });
}

void Scrobbler::signOut() {
  m_token.clear();
  m_account.clear();
  m_settings.remove("scrobbler/account");
  m_settings.setValue("scrobbler/enabled", false);
  secret(clearArgs(), {}, [](bool, QByteArray) {});
  setStatus("Signed out");
  emit changed();
}

void Scrobbler::retryPending() { flush(); }

void Scrobbler::nowPlaying(const QVariantMap &track) {
  if (!enabled() || m_token.isEmpty())
    return;
  const auto listen = listenFor(track, QDateTime::currentSecsSinceEpoch());
  if (listen.isEmpty())
    return;
  // "Playing now" carries no timestamp and is not worth keeping if it fails.
  auto payload = listen;
  payload.remove("listened_at");
  send({{"listen_type", "playing_now"}, {"payload", QVariantList{payload}}}, "playing_now",
       [](bool, const QString &) {});
}

void Scrobbler::submit(const QVariantMap &track, qint64 startedAt) {
  if (!enabled() || m_token.isEmpty())
    return;
  const auto listen = listenFor(track, startedAt);
  if (listen.isEmpty())
    return;
  m_queue.append(listen);
  // A long spell offline should not grow without limit.
  while (m_queue.size() > 500)
    m_queue.removeFirst();
  saveQueue();
  emit changed();
  flush();
}

void Scrobbler::flush() {
  if (m_flushing || m_queue.isEmpty() || !enabled() || m_token.isEmpty())
    return;
  m_flushing = true;
  // Oldest first, and the whole batch stands or falls together.
  const auto batch = m_queue.mid(0, qMin(m_queue.size(), 50));
  send({{"listen_type", batch.size() == 1 ? "single" : "import"}, {"payload", batch}},
       "listens", [this, count = batch.size()](bool ok, const QString &message) {
         m_flushing = false;
         if (ok) {
           m_queue = m_queue.mid(count);
           saveQueue();
           setStatus(m_account.isEmpty() ? "Signed in" : "Signed in as " + m_account);
           emit changed();
           if (!m_queue.isEmpty())
             flush();
           return;
         }
         setStatus(message.isEmpty()
                       ? QString("%1 listen%2 waiting to be sent")
                             .arg(m_queue.size()).arg(m_queue.size() == 1 ? "" : "s")
                       : message);
         emit changed();
         m_retry.start();
       });
}

void Scrobbler::send(const QVariantMap &payload, const QString &type,
                     std::function<void(bool, const QString &)> done) {
  QNetworkRequest request{QUrl(server() + "/1/submit-listens")};
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  request.setRawHeader("Authorization", ("Token " + m_token).toUtf8());
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  const auto body = QJsonDocument::fromVariant(payload).toJson(QJsonDocument::Compact);
  auto *reply = m_network.post(request, body);
  connect(reply, &QNetworkReply::finished, this, [reply, done, type] {
    reply->deleteLater();
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::NoError && code >= 200 && code < 300) {
      done(true, {});
      return;
    }
    const auto body = QJsonDocument::fromJson(reply->readAll()).object().toVariantMap();
    auto message = body.value("error").toString();
    if (message.isEmpty())
      message = code == 401 ? "The listening server rejected the token."
                            : type == "listens" ? QString() : reply->errorString();
    done(false, message);
  });
}

void Scrobbler::loadQueue() {
  m_queue = m_settings.value("scrobbler/queue").toList();
}

void Scrobbler::saveQueue() {
  if (m_queue.isEmpty())
    m_settings.remove("scrobbler/queue");
  else
    m_settings.setValue("scrobbler/queue", m_queue);
}

void Scrobbler::setStatus(const QString &value) {
  if (m_status == value)
    return;
  m_status = value;
  emit changed();
}

void Scrobbler::secret(const QStringList &args, const QByteArray &input,
                       std::function<void(bool, QByteArray)> callback) {
  if (!keyringAvailable()) {
    callback(false, {});
    return;
  }
  auto *process = new QProcess(this);
  auto *timer = new QTimer(process);
  timer->setSingleShot(true);
  auto done = std::make_shared<bool>(false);
  auto finish = [process, done, callback](bool ok) {
    if (*done)
      return;
    *done = true;
    callback(ok, process->readAllStandardOutput());
    process->deleteLater();
  };
  connect(process, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError e) {
    if (e == QProcess::FailedToStart)
      finish(false);
  });
  connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [finish](int code, QProcess::ExitStatus status) {
            finish(code == 0 && status == QProcess::NormalExit);
          });
  connect(timer, &QTimer::timeout, process, [process] { process->kill(); });
  process->start("secret-tool", args);
  process->write(input);
  process->closeWriteChannel();
  timer->start(15000);
}
