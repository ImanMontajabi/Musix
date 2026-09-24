#include "backend.h"
#include <QDate>
#include <QDateTime>
#include <QRegularExpression>

// Following artists and YouTube channels, and what they have put out since.
//
// Everything is local: the follows, what each one has already been seen to
// have, and the releases found. What counts as new is the helper's to decide
// (detect_releases in helper/catalog.py); this side keeps the state, asks one
// follow at a time so a long list never runs many helpers at once, and never
// waits on an answer.
namespace {
constexpr qint64 Day = 24 * 60 * 60;
constexpr int MostReleases = 200;
QString idOf(const QVariant &v) { return v.toMap().value("id").toString(); }
} // namespace

bool Backend::isFollowing(const QString &id) const {
  for (const auto &v : m_following)
    if (idOf(v) == id)
      return true;
  return false;
}

int Backend::unreadReleases() const {
  int count = 0;
  for (const auto &v : m_releases)
    count += v.toMap().value("unread").toBool();
  return count;
}

void Backend::follow(const QVariantMap &target) {
  // A YouTube Music artist or a YouTube channel: both are channel ids.
  static const QRegularExpression channel("^UC[A-Za-z0-9_-]{22}$");
  const auto id = target.value("id").toString();
  if (!channel.match(id).hasMatch() || isFollowing(id))
    return;
  const auto kind = target.value("kind").toString() == "channel" ? "channel" : "artist";
  m_following.prepend(QVariantMap{{"id", id}, {"kind", kind}, {"title", target.value("title").toString().left(512)},
                                  {"art", target.value("art").toString()}, {"followedAt", QDateTime::currentSecsSinceEpoch()},
                                  {"known", QStringList{}}, {"baselined", false}});
  emit followingChanged();
  refreshFollowViews();
  m_saveTimer.start();
  emit toast("Following " + target.value("title").toString());
  // What is out already is recorded now, so none of it turns up as new.
  queueReleaseCheck(id);
}

void Backend::unfollow(const QString &id) {
  for (int i = 0; i < m_following.size(); ++i)
    if (idOf(m_following[i]) == id) {
      const auto title = m_following[i].toMap().value("title").toString();
      m_following.removeAt(i);
      QVariantList kept;
      for (const auto &v : m_releases)
        if (v.toMap().value("followId") != id)
          kept.append(v);
      m_releases = kept;
      m_releaseQueue.removeAll(id);
      emit followingChanged();
      refreshFollowViews();
      m_saveTimer.start();
      emit toast("Unfollowed " + title);
      return;
    }
}

void Backend::markReleaseRead(const QString &id) {
  bool changed = false;
  for (auto &v : m_releases) {
    auto release = v.toMap();
    if (idOf(release) == id && release.value("unread").toBool()) {
      release["unread"] = false;
      v = release;
      changed = true;
    }
  }
  if (!changed)
    return;
  emit followingChanged();
  refreshFollowViews();
  m_saveTimer.start();
}

void Backend::markAllReleasesRead() {
  if (!unreadReleases())
    return;
  for (auto &v : m_releases) {
    auto release = v.toMap();
    release["unread"] = false;
    v = release;
  }
  emit followingChanged();
  refreshFollowViews();
  m_saveTimer.start();
}

void Backend::startReleaseChecks() {
  // A while after launch, so the first screen gets the network first; then an
  // hourly look at whether a day has passed, for an app left running.
  QTimer::singleShot(90 * 1000, this, [this] { maybeCheckReleases(); });
  m_releaseTimer.setInterval(60 * 60 * 1000);
  connect(&m_releaseTimer, &QTimer::timeout, this, &Backend::maybeCheckReleases, Qt::UniqueConnection);
  m_releaseTimer.start();
}

void Backend::maybeCheckReleases() {
  const auto last = m_settings.value("releasesChecked").toLongLong();
  if (m_following.isEmpty() || !m_releaseQueue.isEmpty() || (last && QDateTime::currentSecsSinceEpoch() - last < Day))
    return;
  checkReleases(false);
}

void Backend::checkReleases(bool manual) {
  if (m_following.isEmpty() || !m_releaseQueue.isEmpty())
    return;
  m_releaseManual = manual;
  m_releaseFound = 0;
  m_settings.setValue("releasesChecked", QDateTime::currentSecsSinceEpoch());
  for (const auto &v : m_following)
    m_releaseQueue.append(idOf(v));
  emit followingChanged();
  nextReleaseCheck();
}

void Backend::queueReleaseCheck(const QString &id) {
  if (m_releaseQueue.contains(id))
    return;
  const bool idle = m_releaseQueue.isEmpty();
  m_releaseQueue.append(id);
  emit followingChanged();
  if (idle)
    nextReleaseCheck();
}

void Backend::nextReleaseCheck() {
  if (m_releaseQueue.isEmpty()) {
    if (m_releaseManual)
      emit toast(m_releaseFound ? QString("%1 new %2").arg(m_releaseFound).arg(m_releaseFound == 1 ? "release" : "releases")
                                : QString("Nothing new from who you follow"));
    else if (m_releaseFound)
      emit newReleases(m_releaseFound);
    m_releaseManual = false;
    emit followingChanged();
    return;
  }
  const auto id = m_releaseQueue.first();
  QVariantMap target;
  for (const auto &v : m_following)
    if (idOf(v) == id)
      target = v.toMap();
  if (target.isEmpty()) {
    m_releaseQueue.removeFirst();
    return nextReleaseCheck();
  }
  const bool baseline = !target.value("baselined").toBool();
  request("releases",
          {{"op", "releases"}, {"id", id}, {"kind", target.value("kind")}, {"known", target.value("known")},
           {"baseline", baseline}, {"year", QDate::currentDate().year()}},
          [this, id](const QVariantMap &data) {
            if (!m_releaseQueue.isEmpty() && m_releaseQueue.first() == id)
              m_releaseQueue.removeFirst();
            if (data.value("ok").toBool())
              applyReleaseCheck(id, data);
            // One that failed is simply asked again next time.
            QTimer::singleShot(0, this, &Backend::nextReleaseCheck);
          });
}

void Backend::applyReleaseCheck(const QString &id, const QVariantMap &data) {
  int at = -1;
  for (int i = 0; i < m_following.size(); ++i)
    if (idOf(m_following[i]) == id)
      at = i;
  if (at < 0)
    return;
  auto target = m_following[at].toMap();
  target["known"] = data.value("known").toStringList();
  target["baselined"] = true;
  target["kind"] = data.value("kind", target.value("kind"));
  if (!data.value("title").toString().isEmpty())
    target["title"] = data.value("title").toString().left(512);
  if (!data.value("art").toString().isEmpty())
    target["art"] = data.value("art");
  target["checkedAt"] = QDateTime::currentSecsSinceEpoch();
  m_following[at] = target;
  QSet<QString> have;
  for (const auto &v : m_releases)
    have.insert(idOf(v));
  // The helper returns the new ones newest first; they go in ahead of what
  // was already found, in that order.
  QVariantList found;
  for (const auto &v : data.value("items").toList()) {
    auto release = v.toMap();
    if (idOf(release).isEmpty() || have.contains(idOf(release)))
      continue;
    release["followId"] = id;
    release["followTitle"] = target.value("title");
    release["foundAt"] = QDateTime::currentSecsSinceEpoch();
    release["unread"] = true;
    found.append(release);
  }
  m_releaseFound += found.size();
  m_releases = (found + m_releases).mid(0, MostReleases);
  emit followingChanged();
  refreshFollowViews();
  m_saveTimer.start();
}

QVariantList Backend::followRows() const {
  QVariantList rows;
  for (const auto &v : m_following) {
    const auto target = v.toMap();
    int unread = 0;
    for (const auto &r : m_releases)
      unread += r.toMap().value("followId") == target.value("id") && r.toMap().value("unread").toBool();
    rows.append(QVariantMap{{"id", target.value("id")}, {"browseId", target.value("id")}, {"kind", target.value("kind")},
                            {"title", target.value("title")}, {"art", target.value("art")}, {"unread", unread > 0},
                            {"artist", target.value("kind") == "channel" ? "Channel" : "Artist"}});
  }
  return rows;
}

QVariantList Backend::withFollowSection(QVariantList sections) const {
  for (int i = sections.size() - 1; i >= 0; --i)
    if (sections[i].toMap().value("id") == "following")
      sections.removeAt(i);
  if (!m_releases.isEmpty())
    sections.prepend(QVariantMap{{"id", "following"}, {"title", "New from who you follow"}, {"items", m_releases.mid(0, 12)}});
  return sections;
}

void Backend::refreshFollowViews() {
  if (m_page == "home") {
    m_sections = withFollowSection(m_sections);
    emit catalogChanged();
  } else if (m_page == "library" && (m_libraryId == "following" || m_libraryId == "releases")) {
    m_results.assign(libraryRows(m_libraryId));
    emit catalogChanged();
  }
}

bool Backend::releaseNotifications() const { return m_settings.value("releaseNotifications", false).toBool(); }
void Backend::setReleaseNotifications(bool on) {
  if (on == releaseNotifications())
    return;
  m_settings.setValue("releaseNotifications", on);
  emit settingsChanged();
}
