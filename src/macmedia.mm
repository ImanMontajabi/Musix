#include "macmedia.h"
#include <QLoggingCategory>
#include <QMetaObject>
#include <QNetworkReply>
#include <QPointer>
#include <QUrl>
#import <AppKit/AppKit.h>
#import <MediaPlayer/MediaPlayer.h>
#import <objc/message.h>

namespace {
NSString *ns(const QString &s) { return s.toNSString(); }
QString qs(id value) { return [value isKindOfClass:NSString.class] ? QString::fromNSString(value) : QString(); }
// A cover that never arrives or is huge is not worth holding up the controls for.
Q_LOGGING_CATEGORY(lcMedia, "musix.media", QtWarningMsg)
constexpr qint64 maxArtworkBytes = 8 * 1024 * 1024;
} // namespace

struct MacMedia::Impl {
  NSImage *image = nil;
  NSMutableArray *targets = [NSMutableArray array]; // (command, token) pairs, removed on exit
};

MacMedia::MacMedia(Backend *backend, QObject *parent) : QObject(parent), b(backend), d(new Impl) {
  MPRemoteCommandCenter *center = MPRemoteCommandCenter.sharedCommandCenter;
  QPointer<MacMedia> self(this);
  // By value: the blocks outlive this constructor, and a reference to `self` would dangle by the first key press.
  auto add = [&, self](MPRemoteCommand *command, QString name) {
    command.enabled = YES;
    id token = [command addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *event) {
      // The system calls from its own queue; the player answers on the main thread.
      qCInfo(lcMedia) << "remote command" << name << "allowed" << (self && self->allowed(name)) << "on main thread" << NSThread.isMainThread;
      if (!self || !self->allowed(name)) return MPRemoteCommandHandlerStatusNoActionableNowPlayingItem;
      const double argument = [event isKindOfClass:MPChangePlaybackPositionCommandEvent.class]
                                  ? ((MPChangePlaybackPositionCommandEvent *)event).positionTime : 0;
      QMetaObject::invokeMethod(self.data(), [self, name, argument] { if (self) self->perform(name, argument); }, Qt::QueuedConnection);
      return MPRemoteCommandHandlerStatusSuccess;
    }];
    [d->targets addObject:@[ command, token ]];
  };
  add(center.playCommand, "play");
  add(center.pauseCommand, "pause");
  add(center.togglePlayPauseCommand, "toggle");
  add(center.nextTrackCommand, "next");
  add(center.previousTrackCommand, "previous");
  add(center.changePlaybackPositionCommand, "seek");
  // Stepping through a song in fixed jumps and rating are not things Musix does.
  for (MPRemoteCommand *command : @[ center.skipForwardCommand, center.skipBackwardCommand, center.seekForwardCommand,
                                     center.seekBackwardCommand, center.likeCommand, center.dislikeCommand,
                                     center.bookmarkCommand, center.stopCommand ])
    command.enabled = NO;

  for (auto signal : {&Backend::trackChanged, &Backend::playbackChanged, &Backend::settingsChanged})
    connect(b, signal, this, [this] { publish(false); });
  connect(b, &Backend::seeked, this, [this] { publish(true); });
  publish(true);
}

MacMedia::~MacMedia() {
  for (NSArray *pair in d->targets) [pair[0] removeTarget:pair[1]];
  MPNowPlayingInfoCenter.defaultCenter.nowPlayingInfo = nil;
  MPNowPlayingInfoCenter.defaultCenter.playbackState = MPNowPlayingPlaybackStateStopped;
}

// The same rules mpris.h gives CanGoNext / CanGoPrevious.
bool MacMedia::allowed(const QString &command) const {
  const bool queued = b->queue()->count() > 0;
  if (command == "play" || command == "pause" || command == "toggle") return queued;
  if (command == "next")
    return queued && (b->currentIndex() + 1 < b->queue()->count() || b->repeat() == 1 || b->shuffle() ||
                      (b->autoplay() && !b->current().value("videoId").toString().isEmpty()));
  if (command == "previous") return queued && (b->position() > 3000 || b->currentIndex() > 0 || b->repeat() == 1);
  // Not isSeekable(): the player reports that false for a moment after a pause, and Backend::seek keeps the target until it can.
  if (command == "seek") return queued && b->duration() > 0;
  return false;
}

bool MacMedia::commandEnabled(const QString &command) const {
  MPRemoteCommandCenter *center = MPRemoteCommandCenter.sharedCommandCenter;
  MPRemoteCommand *c = command == "play" ? center.playCommand : command == "pause" ? center.pauseCommand
                     : command == "toggle" ? center.togglePlayPauseCommand : command == "next" ? center.nextTrackCommand
                     : command == "previous" ? center.previousTrackCommand : command == "seek" ? center.changePlaybackPositionCommand : nil;
  return c && c.enabled;
}

bool MacMedia::perform(const QString &command, double argument) {
  if (!allowed(command)) return false;
  // A skip keeps what the person had chosen: paused stays paused, stopped stays stopped.
  const bool paused = !b->playing() && !b->resolving(), stopped = b->stopped();
  if (command == "play") b->play();
  else if (command == "pause") b->pause();
  else if (command == "toggle") b->toggle();
  else if (command == "next" || command == "previous") {
    command == "next" ? b->next() : b->previous();
    if (stopped) b->stop(); else if (paused) b->pause();
  } else if (command == "seek") {
    const qint64 target = qint64(argument * 1000);
    b->seek(qBound<qint64>(0, target, b->duration()));
  }
  return true;
}

bool MacMedia::deliver(const QString &command) {
  MPRemoteCommandCenter *center = MPRemoteCommandCenter.sharedCommandCenter;
  MPRemoteCommand *c = command == "play" ? center.playCommand : command == "pause" ? center.pauseCommand
                     : command == "toggle" ? center.togglePlayPauseCommand : command == "next" ? center.nextTrackCommand
                     : command == "previous" ? center.previousTrackCommand : nil;
  SEL invoke = NSSelectorFromString(@"invokeCommandWithEvent:completion:");
  if (!c || ![c respondsToSelector:invoke]) return false;
  void (^done)(NSArray *) = ^(NSArray *) {};
  ((void (*)(id, SEL, id, id))objc_msgSend)(c, invoke, nil, done);
  return true;
}

QString MacMedia::playbackState() const {
  if (b->current().isEmpty() || b->stopped()) return "stopped";
  return b->playing() ? "playing" : "paused";
}

QVariantMap MacMedia::nowPlaying() const {
  NSDictionary *info = MPNowPlayingInfoCenter.defaultCenter.nowPlayingInfo;
  if (!info) return {};
  return {{"title", qs(info[MPMediaItemPropertyTitle])},
          {"artist", qs(info[MPMediaItemPropertyArtist])},
          {"album", qs(info[MPMediaItemPropertyAlbumTitle])},
          {"duration", [info[MPMediaItemPropertyPlaybackDuration] doubleValue]},
          {"elapsed", [info[MPNowPlayingInfoPropertyElapsedPlaybackTime] doubleValue]},
          {"rate", [info[MPNowPlayingInfoPropertyPlaybackRate] doubleValue]},
          {"artwork", info[MPMediaItemPropertyArtwork] != nil}};
}

void MacMedia::publish(bool force) {
  MPNowPlayingInfoCenter *center = MPNowPlayingInfoCenter.defaultCenter;
  const auto track = b->current();
  const QString state = playbackState();
  if (track.isEmpty()) {
    m_signature.clear();
    m_artUrl.clear();
    d->image = nil;
    center.nowPlayingInfo = nil;
    center.playbackState = MPNowPlayingPlaybackStateStopped;
    return;
  }
  const QString art = isServerSource(track.value("source")) ? b->serverArtwork() : b->displayArt(track);
  const double rate = state == "playing" ? b->playbackRate() : 0.0;
  const QString signature = QStringList{track.value("title").toString(), track.value("artist").toString(), track.value("album").toString(),
                                        QString::number(b->duration()), state, QString::number(rate), art}.join('\x1f');
  // The system extrapolates the elapsed time from the rate, so only a change of
  // song, state, rate or position (a seek) is worth telling it.
  if (!force && signature == m_signature) return;
  m_signature = signature;
  if (art != m_artUrl) loadArtwork(art);

  NSMutableDictionary *info = [NSMutableDictionary dictionary];
  info[MPMediaItemPropertyTitle] = ns(track.value("title").toString());
  if (const auto artist = track.value("artist").toString(); !artist.isEmpty()) info[MPMediaItemPropertyArtist] = ns(artist);
  if (const auto album = track.value("album").toString(); !album.isEmpty()) info[MPMediaItemPropertyAlbumTitle] = ns(album);
  if (b->duration() > 0) info[MPMediaItemPropertyPlaybackDuration] = @(b->duration() / 1000.0);
  info[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(b->position() / 1000.0);
  info[MPNowPlayingInfoPropertyPlaybackRate] = @(rate);
  info[MPNowPlayingInfoPropertyDefaultPlaybackRate] = @(1.0);
  info[MPNowPlayingInfoPropertyMediaType] = @(MPNowPlayingInfoMediaTypeAudio);
  if (NSImage *image = d->image)
    info[MPMediaItemPropertyArtwork] = [[MPMediaItemArtwork alloc] initWithBoundsSize:image.size
                                                                       requestHandler:^NSImage *(CGSize) { return image; }];
  center.nowPlayingInfo = info;
  center.playbackState = state == "playing" ? MPNowPlayingPlaybackStatePlaying
                         : state == "paused" ? MPNowPlayingPlaybackStatePaused : MPNowPlayingPlaybackStateStopped;
}

void MacMedia::loadArtwork(const QString &url) {
  m_artUrl = url;
  d->image = nil;
  const QUrl u(url);
  if (url.isEmpty()) return;
  if (u.isLocalFile() || (u.scheme().isEmpty() && url.startsWith('/'))) {
    d->image = [[NSImage alloc] initWithContentsOfFile:ns(u.isLocalFile() ? u.toLocalFile() : url)];
    return;
  }
  if (u.scheme() != "https" && u.scheme() != "http") return;
  QNetworkRequest request(u);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  auto *reply = m_net.get(request);
  QPointer<MacMedia> self(this);
  connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) { if (received > maxArtworkBytes) reply->abort(); });
  connect(reply, &QNetworkReply::finished, this, [self, reply, url] {
    reply->deleteLater();
    // A cover for a song that is no longer playing is not wanted.
    if (!self || self->m_artUrl != url || reply->error() != QNetworkReply::NoError) return;
    const QByteArray bytes = reply->readAll();
    NSImage *image = [[NSImage alloc] initWithData:[NSData dataWithBytes:bytes.constData() length:bytes.size()]];
    if (!image) return;
    self->d->image = image;
    self->publish(true);
  });
}

MacMedia *registerMacMedia(Backend *backend) { return new MacMedia(backend, backend); }
