#include "updatechecker.h"
#include <QDesktopServices>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

namespace {
const QString Repository = QStringLiteral("ImanMontajabi/Musix");
constexpr qint64 Day = 24 * 60 * 60;
// A day's worth of the project's releases is a few kilobytes; anything much
// bigger is not an answer to this question.
constexpr qint64 MostBytes = 4 * 1024 * 1024;
} // namespace

UpdateChecker::UpdateChecker(const QString &current, QObject *parent)
    : QObject(parent), m_current(QVersionNumber::fromString(current).normalized()), m_currentName(current),
      m_endpoint(QStringLiteral("https://api.github.com/repos/%1/releases?per_page=30").arg(Repository)) {
  m_daily.setInterval(60 * 60 * 1000);
  connect(&m_daily, &QTimer::timeout, this, &UpdateChecker::maybeCheck);
}

void UpdateChecker::setAutomatic(bool on) {
  if (on == automatic())
    return;
  m_settings.setValue("updates/automatic", on);
  emit settingsChanged();
  if (on)
    maybeCheck();
}

void UpdateChecker::start() {
  // Long enough after launch that the app has settled and the first screen
  // has what it needs from the network; then an hourly look at whether a day
  // has passed, for an app that is left running.
  QTimer::singleShot(20 * 1000, this, &UpdateChecker::maybeCheck);
  m_daily.start();
}

bool UpdateChecker::setEndpoint(const QUrl &url) {
  if (!url.isValid() || url.scheme() != "http" || !QHostAddress(url.host()).isLoopback())
    return false;
  m_endpoint = url;
  return true;
}

void UpdateChecker::maybeCheck() {
  if (!automatic() || checking())
    return;
  const auto last = m_settings.value("updates/lastCheck").toLongLong();
  if (last && QDateTime::currentSecsSinceEpoch() - last < Day)
    return;
  check(false);
}

QVersionNumber UpdateChecker::parseTag(const QString &tag) {
  // v0.14.0 and V0.13.0 are both tags this project has published. Anything
  // with a suffix -- a release candidate, a build note -- is not a version to
  // offer anyone.
  static const QRegularExpression shape(QStringLiteral("^[vV]?(\\d{1,4}(?:\\.\\d{1,4}){0,3})$"));
  const auto match = shape.match(tag.trimmed());
  // Normalized, so 0.14 and 0.14.0 are the same release rather than 0.14.0
  // counting as newer.
  return match.hasMatch() ? QVersionNumber::fromString(match.captured(1)).normalized() : QVersionNumber();
}

UpdateChecker::Release UpdateChecker::newest(const QJsonArray &releases) {
  static const QRegularExpression dmg(QStringLiteral("^Musix-[^/]+-arm64\\.dmg$"));
  Release best;
  for (const auto &value : releases) {
    const auto release = value.toObject();
    if (release.value("draft").toBool(true) || release.value("prerelease").toBool(true))
      continue;
    const auto tag = release.value("tag_name").toString();
    const auto version = parseTag(tag);
    if (version.isNull())
      continue;
    bool installable = false;
    for (const auto &asset : release.value("assets").toArray())
      installable = installable || dmg.match(asset.toObject().value("name").toString()).hasMatch();
    if (installable && (!best.valid() || version > best.version))
      best = {version, tag};
  }
  return best;
}

QUrl UpdateChecker::releasePage(const QString &tag) {
  if (parseTag(tag).isNull())
    return {};
  return QUrl(QStringLiteral("https://github.com/%1/releases/tag/%2").arg(Repository, tag));
}

void UpdateChecker::check(bool manual) {
  if (checking())
    return;
  if (!manual)
    m_settings.setValue("updates/lastCheck", QDateTime::currentSecsSinceEpoch());
  QNetworkRequest request(m_endpoint);
  request.setRawHeader("Accept", "application/vnd.github+json");
  request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
  request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Musix/%1 ( https://github.com/%2 )").arg(currentVersion(), Repository));
  request.setTransferTimeout(15000);
  // Nowhere else is an answer: a redirect off this host is not followed.
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
  m_reply = m_network.get(request);
  connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64) {
    if (received > MostBytes && m_reply)
      m_reply->abort();
  });
  connect(m_reply, &QNetworkReply::finished, this, [this, manual, reply = m_reply.data()] { finished(reply, manual); });
  emit stateChanged();
}

void UpdateChecker::finished(QNetworkReply *reply, bool manual) {
  reply->deleteLater();
  m_reply.clear();
  const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if ((code == 403 || code == 429) &&
      (reply->rawHeader("x-ratelimit-remaining") == "0" || code == 429)) {
    const auto reset = QDateTime::fromSecsSinceEpoch(reply->rawHeader("x-ratelimit-reset").toLongLong());
    const auto when = reset.isValid() && reset > QDateTime::currentDateTime()
                          ? " Try again after " + QLocale().toString(reset.time(), QLocale::ShortFormat) + "."
                          : QString(" Try again later.");
    return conclude(Outcome::RateLimited, "GitHub is limiting how often it can be asked." + when, manual);
  }
  if (reply->error() != QNetworkReply::NoError && code == 0)
    return conclude(Outcome::Offline, "Couldn’t reach GitHub. Check your connection and try again.", manual);
  if (code != 200)
    return conclude(Outcome::Failed, QStringLiteral("GitHub answered with an error (HTTP %1). Try again later.").arg(code), manual);
  const auto body = reply->readAll();
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(body, &error);
  if (error.error != QJsonParseError::NoError || !document.isArray())
    return conclude(Outcome::Failed, "GitHub’s answer could not be read. Try again later.", manual);
  const auto best = newest(document.array());
  if (best.valid() && best.version > m_current) {
    m_available = best;
    return conclude(Outcome::Newer, {}, manual);
  }
  m_available = {};
  conclude(Outcome::Current, {}, manual);
}

bool UpdateChecker::suppressed(const Release &release) const {
  const auto name = release.name();
  if (m_settings.value("updates/skipped").toString() == name)
    return true;
  return m_settings.value("updates/laterVersion").toString() == name &&
         QDateTime::currentSecsSinceEpoch() - m_settings.value("updates/laterAt").toLongLong() < Day;
}

void UpdateChecker::conclude(Outcome outcome, const QString &message, bool manual) {
  const auto at = QLocale().toString(QTime::currentTime(), QLocale::ShortFormat);
  switch (outcome) {
  case Outcome::Newer:
    m_status = QStringLiteral("Musix %1 is available. Checked at %2.").arg(availableVersion(), at);
    emit stateChanged();
    if (manual || !suppressed(m_available))
      emit available(availableVersion(), currentVersion(), manual);
    return;
  case Outcome::Current:
    m_status = QStringLiteral("You’re up to date. Checked at %1.").arg(at);
    emit stateChanged();
    if (manual)
      emit upToDate();
    return;
  default:
    m_status = message;
    emit stateChanged();
    // An automatic check that fails says nothing: the person did not ask.
    if (manual)
      emit failed(message);
  }
}

void UpdateChecker::openRelease() {
  const auto page = releasePage(m_available.tag);
  if (page.isValid())
    QDesktopServices::openUrl(page);
}

void UpdateChecker::later() {
  m_settings.setValue("updates/laterVersion", availableVersion());
  m_settings.setValue("updates/laterAt", QDateTime::currentSecsSinceEpoch());
}

void UpdateChecker::skip() { m_settings.setValue("updates/skipped", availableVersion()); }
