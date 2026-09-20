#pragma once
#include <QRegularExpression>
#include <QUrl>

// How a cover URL from a catalogue becomes the request for a given size.
//
// Google's image service and Apple's both carry the size in the URL, so a
// surface can ask for the pixels it draws instead of the thumbnail it was
// handed. YouTube's frames for a video carry no size at all, only a fixed set
// of names; the catalogue hands out hqdefault, cropped to 400 × 225, and the
// 1280 × 720 frame sits beside it as maxresdefault whenever the upload was HD.
// Nothing here fetches: the loader decides what to request, the backend what
// to remember, and both need the same answers.
namespace artworkurl {

// The video id when the URL is one of YouTube's frames for a video, otherwise
// an empty string. Only YouTube's own image host counts, so a URL that merely
// looks like a frame cannot be substituted for one.
inline QString videoId(const QUrl &url) {
  static const QRegularExpression host("^i\\d?\\.ytimg\\.com$");
  static const QRegularExpression frame("^/vi(?:_webp)?/([A-Za-z0-9_-]{11})/[^/]+$");
  if (url.scheme() != "https" || !host.match(url.host()).hasMatch()) return {};
  const auto match = frame.match(url.path());
  return match.hasMatch() ? match.captured(1) : QString();
}

// True for a cover on Apple's image service in the form its search API
// returns it: an https URL on an isN-ssl.mzstatic.com host whose last path
// segment names the size, with no query to smuggle anything else in.
inline bool isAlbumCover(const QUrl &url) {
  static const QRegularExpression host("^is\\d+-ssl\\.mzstatic\\.com$");
  static const QRegularExpression size("/\\d+x\\d+bb\\.(?:jpg|png|webp)$");
  return url.scheme() == "https" && !url.hasQuery() && !url.hasFragment() && url.userInfo().isEmpty()
      && host.match(url.host()).hasMatch() && size.match(url.path()).hasMatch();
}

// The URL to fetch for `source` when it will be drawn `pixels` wide. Known
// services are asked for at least that size, never above 1600, which is the
// most any surface here draws; every other URL is returned untouched.
inline QUrl sized(const QUrl &source, int pixels) {
  QUrl url = source;
  auto path = url.path();
  const auto host = url.host();
  const auto grow = [&](const QRegularExpressionMatch &match) {
    return qMin(1600, qMax(qMax(match.captured(1).toInt(), match.captured(2).toInt()), pixels));
  };
  if (host == "lh3.googleusercontent.com" || host == "lh3.ggpht.com" || host == "yt3.googleusercontent.com" || host == "yt3.ggpht.com") {
    static const QRegularExpression dimensions("=w(\\d+)-h(\\d+)");
    const auto match = dimensions.match(path);
    if (match.hasMatch()) {
      path.replace(match.capturedStart(), match.capturedLength(), QString("=w%1-h%1").arg(grow(match)));
      url.setPath(path);
    }
  } else if (isAlbumCover(url)) {
    static const QRegularExpression dimensions("/(\\d+)x(\\d+)bb\\.");
    const auto match = dimensions.match(path);
    path.replace(match.capturedStart(), match.capturedLength(), QString("/%1x%1bb.").arg(grow(match)));
    url.setPath(path);
  } else if (const auto id = videoId(url); !id.isEmpty() && pixels > 240) {
    // The catalogue's frame is 225 pixels tall, so anything drawn larger is
    // enlarging it. The HD frame exists only for HD uploads; when it is
    // missing the request fails and the loader falls back to the source.
    url = QUrl("https://i.ytimg.com/vi/" + id + "/maxresdefault.jpg");
  }
  return url;
}

} // namespace artworkurl
