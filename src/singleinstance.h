#pragma once
#include <QByteArray>
#include <QLocalServer>
#include <QLocalSocket>
#include <QString>

// One Musix per user: a second launch passes its request (a link, "--mini",
// or just "raise") to the one already running and exits. The listening socket
// exists from early in startup, before the event loop, so a connection alone
// proves nothing: an instance stuck in its launch still accepts it. The
// running instance therefore acknowledges each request, and a launch that
// hears nothing back starts on its own rather than vanishing into a peer that
// will never answer.
namespace SingleInstance {

inline constexpr char Ack[] = "ok\n";

// True when a running instance took the request.
inline bool handOff(const QString &name, const QByteArray &request, int connectMs = 120, int ackMs = 1500) {
  QLocalSocket peer;
  peer.connectToServer(name);
  if (!peer.waitForConnected(connectMs))
    return false;
  peer.write(request);
  peer.flush();
  // Nothing left to write is not a failure; waitForBytesWritten says false then.
  if (peer.bytesToWrite() > 0 && !peer.waitForBytesWritten(150))
    return false;
  QByteArray reply;
  while (!reply.contains('\n') && peer.waitForReadyRead(ackMs))
    reply += peer.readAll();
  return reply.startsWith(Ack);
}

// The running side: say the request arrived before acting on it.
inline void acknowledge(QLocalSocket *socket) {
  socket->write(Ack);
  socket->flush();
}

} // namespace SingleInstance
