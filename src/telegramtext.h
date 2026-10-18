#pragma once
// What a Telegram song's title and artist say once the channel's own marketing
// is taken off them. Channels append "@name", "t.me/…" links and "Join @name"
// to the performer and the title, and a document's title is its file name.
// Cleaning them is what makes a library readable and what lets a lyrics or
// scrobble lookup match: "Song | @channel" is nobody's track.
#include <QRegularExpression>
#include <QString>

namespace telegramtext {

struct Cleaned {
  QString title, artist;
};

namespace detail {
// A handle or a link, with the word that only ever points at a channel in front of it.
inline const QString promo =
    QStringLiteral("(?:(?<!\\w)(?:join|follow|subscribe(?:\\s+to)?|via|channel)\\s*[:：]?\\s*|🆔\\s*[:：]?\\s*)?"
                   "(?:(?<![\\w.@])@\\w{3,}|(?:https?://|www\\.)[^\\s\\]\\)\\}|,;]+|(?<![\\w.])(?:t|telegram)\\.me/[^\\s\\]\\)\\}|,;]+)");
inline const QRegularExpression &wrapped() {
  // A bracket whose whole content is promo: "[ @name ]", "(t.me/music)", "【@a | @b】".
  static const QRegularExpression re(
      QStringLiteral("[\\[\\(\\{【（]\\s*") + promo + QStringLiteral("(?:\\s*[|,&/·•-]\\s*") + promo + QStringLiteral(")*\\s*[\\]\\)\\}】）]"),
      QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
  return re;
}
inline const QRegularExpression &bare() {
  static const QRegularExpression re(promo, QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
  return re;
}
inline const QRegularExpression &extension() {
  static const QRegularExpression re(QStringLiteral("\\.(?:mp3|flac|m4a|ogg|opus|wav|aac|wma|alac|aiff?|ape)$"),
                                     QRegularExpression::CaseInsensitiveOption);
  return re;
}
inline bool placeholder(const QString &artist) {
  const auto a = artist.trimmed().toLower();
  return a.isEmpty() || a == QLatin1String("telegram audio") || a == QLatin1String("unknown") || a == QLatin1String("unknown artist") ||
         a == QLatin1String("<unknown>");
}
inline QString trimmedEnds(QString s) {
  static const QString edge = QStringLiteral(" \t|-–—•·~:,/");
  int from = 0, to = s.size();
  while (from < to && edge.contains(s[from])) ++from;
  while (to > from && edge.contains(s[to - 1])) --to;
  return s.mid(from, to - from);
}
} // namespace detail

// Handles, links and the brackets and pipes that only wrapped them, and an
// audio file extension. Parentheses and brackets that belong to the music —
// "(feat. …)", "(Live)", "(TMC & BLH Remix)" — are left alone. A string with
// nothing to remove comes back as it was (trimmed).
inline QString stripPromo(const QString &text) {
  QString s = text.trimmed();
  s.remove(detail::extension());
  const QString before = s;
  s.remove(detail::wrapped());
  s.remove(detail::bare());
  if (s == before) return s.trimmed();
  static const QRegularExpression empty(QStringLiteral("[\\[\\(\\{【（]\\s*[\\]\\)\\}】）]"));
  static const QRegularExpression pipes(QStringLiteral("\\s*\\|(?:\\s*\\|)+\\s*"));
  static const QRegularExpression spaces(QStringLiteral("\\s+"));
  s.remove(empty);
  s.replace(pipes, QStringLiteral(" | "));
  s.replace(spaces, QStringLiteral(" "));
  return detail::trimmedEnds(s);
}

// The row's title and artist as they should be shown: promo gone, and, when no
// performer is known, "Artist - Title" split into its two parts.
inline Cleaned cleanSong(const QString &title, const QString &artist) {
  QString t = stripPromo(title), a = stripPromo(artist);
  if (detail::placeholder(a)) a.clear();
  if (t.isEmpty()) {
    // All promo: keep what the message said rather than an empty title.
    t = title.trimmed();
    t.remove(detail::extension());
    if (t.isEmpty()) t = QStringLiteral("Untitled");
    return {t, a};
  }
  if (a.isEmpty()) {
    static const QRegularExpression number(QStringLiteral("^\\d{1,3}[.)]?$"));
    int at = -1;
    for (const auto &separator : {QStringLiteral(" - "), QStringLiteral(" – ")}) {
      const int found = t.indexOf(separator);
      if (found > 0 && (at < 0 || found < at)) at = found;
    }
    if (at > 0) {
      const auto left = t.left(at).trimmed(), right = t.mid(at + 3).trimmed();
      if (!left.isEmpty() && !right.isEmpty() && !number.match(left).hasMatch()) return {right, left};
    }
  }
  return {t, a};
}

} // namespace telegramtext
