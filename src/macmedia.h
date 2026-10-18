#pragma once
// macOS's counterpart of mpris.h: what Control Center and the lock screen show
// (MPNowPlayingInfoCenter), and the media keys, AirPods and Touch Bar controls
// that reach the app (MPRemoteCommandCenter). The system delivers each command
// to a handler that only calls perform(), so the tests drive the same path
// without sending a key event to the whole machine.
#include "backend.h"
#include <QNetworkAccessManager>
#include <QObject>
#include <QVariantMap>
#include <memory>

class MacMedia : public QObject {
  Q_OBJECT
public:
  explicit MacMedia(Backend *backend, QObject *parent = nullptr);
  ~MacMedia() override;
  // What the system has been told, read back from it: title, artist, album,
  // duration and elapsed (seconds), rate, and whether artwork is attached.
  QVariantMap nowPlaying() const;
  // "playing", "paused" or "stopped".
  QString playbackState() const;
  // "play", "pause", "toggle", "next", "previous" or "seek".
  bool allowed(const QString &command) const;
  // Whether the system has a handler registered for it and offers it.
  bool commandEnabled(const QString &command) const;
  bool perform(const QString &command, double argument = 0);

private:
  struct Impl;
  void publish(bool force);
  void loadArtwork(const QString &url);
  Backend *b;
  QNetworkAccessManager m_net;
  QString m_signature, m_artUrl;
  std::unique_ptr<Impl> d;
};
MacMedia *registerMacMedia(Backend *backend);
