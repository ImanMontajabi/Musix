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

// Nothing may carry a query or a fragment, so a cover URL cannot smuggle
// anything else along with it.
inline bool plainHttps(const QUrl &url) {
  return url.scheme() == "https" && !url.hasQuery() && !url.hasFragment() && url.userInfo().isEmpty();
}

// A cover on Apple's image service in the form its search API returns it: an
// isN-ssl.mzstatic.com host whose last path segment names the size.
inline bool isAppleCover(const QUrl &url) {
  static const QRegularExpression host("^is\\d+-ssl\\.mzstatic\\.com$");
  static const QRegularExpression size("/\\d+x\\d+bb\\.(?:jpg|png|webp)$");
  return plainHttps(url) && host.match(url.host()).hasMatch() && size.match(url.path()).hasMatch();
}

// A release group's front cover in the Cover Art Archive. The archive serves
// the picture from the Internet Archive, so fetching one ends up on a
// different host; that redirect is the network layer's business, not this.
inline bool isArchiveCover(const QUrl &url) {
  static const QRegularExpression path("^/release-group/[0-9a-f-]{36}/front$");
  return plainHttps(url) && url.host() == "coverartarchive.org" && path.match(url.path()).hasMatch();
}

// A cover from a service this build asked, and so a URL it may remember and
// draw in place of a video frame.
inline bool isAlbumCover(const QUrl &url) { return isAppleCover(url) || isArchiveCover(url); }

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
  } else if (isAppleCover(url)) {
    static const QRegularExpression dimensions("/(\\d+)x(\\d+)bb\\.");
    const auto match = dimensions.match(path);
    path.replace(match.capturedStart(), match.capturedLength(), QString("/%1x%1bb.").arg(grow(match)));
    url.setPath(path);
  } else if (isArchiveCover(url)) {
    // The archive keeps three sizes beside whatever was uploaded. Above the
    // largest of them the upload itself is the only bigger picture there is,
    // and a release that is missing a size falls back to it the same way.
    if (pixels <= 1200)
      url.setPath(path + (pixels <= 250 ? "-250" : pixels <= 500 ? "-500" : "-1200"));
  } else if (const auto id = videoId(url); !id.isEmpty() && pixels > 240) {
    // The catalogue's frame is 225 pixels tall, so anything drawn larger is
    // enlarging it. The HD frame exists only for HD uploads; when it is
    // missing the request fails and the loader falls back to the source.
    url = QUrl("https://i.ytimg.com/vi/" + id + "/maxresdefault.jpg");
  }
  return url;
}

} // namespace artworkurl
