#include "telegram.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
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
    stop();
    set("error", "Telegram didn’t respond. Try again.");
  });
  m_restart.setSingleShot(true);
  connect(&m_restart, &QTimer::timeout, this, [this] { if (m_wanted) start(); });
  restoreAccount();
}

Telegram::~Telegram() { stop(); }

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
}

void Telegram::set(const QString &state, const QString &error) {
  m_state = state;
  m_error = error;
  emit changed();
}

void Telegram::open() {
  m_wanted = true;
  m_crashes = 0;
  if (!available()) {
    set("no-credentials");
    return;
  }
  start();
}

void Telegram::close() {
  stop();
  m_pending = 0;
  set("closed");
}

void Telegram::retry() {
  stop();
  open();
}

void Telegram::start() {
  if (m_process) return;
  const auto python = m_python ? m_python() : QString();
  const auto script = m_script ? m_script() : QString();
  if (python.isEmpty() || script.isEmpty()) {
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
    set("error", "Telegram support can’t start. Run scripts/setup.sh.");
  });
  connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this, process](int code, QProcess::ExitStatus) {
            if (process != m_process) return;
            m_process = nullptr;
            process->deleteLater();
            m_buffer.clear();
            m_pending = 0;
            if (code == 2 && m_state != "starting") return; // said why, and asked for no restart
            exited();
          });
  m_process = process;
  m_buffer.clear();
  set("starting");
  m_startTimeout.start();
  process->start(python, {script});
}

void Telegram::exited() {
  if (!m_wanted) return;
  if (++m_crashes > 3) {
    set("error", "Telegram stopped unexpectedly. Try again.");
    return;
  }
  set("starting");
  m_restart.start(500 * m_crashes);
}

void Telegram::stop() {
  m_wanted = false;
  m_startTimeout.stop();
  m_restart.stop();
  auto process = m_process;
  m_process = nullptr;
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
void Telegram::logout() { send("logout"); }

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
    if (m_pending > 0) --m_pending;
    m_error = message.value("ok").toBool() ? QString() : message.value("error").toString();
    emit changed();
    return;
  }
  if (message.value("event").toString() != "state") return;
  m_startTimeout.stop();
  m_crashes = 0;
  const auto state = message.value("state").toString();
  m_codeKind = message.value("codeKind").toString();
  m_hint = message.value("passwordHint").toString();
  if (state == "ready") {
    m_name = message.value("name").toString();
    m_username = message.value("username").toString();
  } else if (state == "wait-phone") {
    // Signed out, whether by asking to or because Telegram ended the session.
    m_name.clear();
    m_username.clear();
  }
  set(state, message.value("error").toString());
}
