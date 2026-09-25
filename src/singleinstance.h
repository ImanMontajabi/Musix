#pragma once
#include <QByteArray>
#include <QElapsedTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QString>
#include <QThread>
#include <cerrno>
#include <csignal>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// One Musix per user: a second launch passes its request (a link, "--mini",
// or just "raise") to the one already running and exits.
//
// The listening socket exists from early in startup, before the event loop,
// so a connection alone proves nothing, and the running instance says it got
// the request. Silence is not permission to start a second instance, though:
// two of them on one profile would both write the library and the settings.
// So the launch waits while the other process is alive, and only takes over
// once it is gone; if it stays alive and silent, the person decides.
namespace SingleInstance {

inline constexpr char Ack[] = "ok\n";

struct Outcome {
  enum Kind { Gone, Handed, Unresponsive } kind = Gone;
  qint64 pid = 0; // the silent process, for Unresponsive
};

inline bool alive(qint64 pid) { return pid > 0 && (::kill(pid_t(pid), 0) == 0 || errno == EPERM); }

// True once the process is gone: after a silent one is quit, or while one exits.
inline bool waitUntilGone(qint64 pid, int ms) {
  QElapsedTimer clock;
  clock.start();
  while (alive(pid)) {
    if (clock.elapsed() > ms)
      return false;
    QThread::msleep(50);
  }
  return true;
}

// The process on the other end of a local socket, or 0 if the system won't say.
inline qint64 peerPid(const QLocalSocket &socket) {
  const auto fd = int(socket.socketDescriptor());
  if (fd < 0)
    return 0;
#ifdef LOCAL_PEERPID
  pid_t pid = 0;
  socklen_t size = sizeof(pid);
  return ::getsockopt(fd, SOL_LOCAL, LOCAL_PEERPID, &pid, &size) == 0 ? pid : 0;
#elif defined(SO_PEERCRED)
  struct ucred cred {};
  socklen_t size = sizeof(cred);
  return ::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &size) == 0 ? cred.pid : 0;
#else
  return 0;
#endif
}

// patienceMs is how long to keep waiting on a process that is alive but busy
// before calling it unresponsive; a process that goes away ends the wait.
inline Outcome handOff(const QString &name, const QByteArray &request, int connectMs = 120, int patienceMs = 10000) {
  QLocalSocket peer;
  peer.connectToServer(name);
  if (!peer.waitForConnected(connectMs))
    return {};
  const qint64 pid = peerPid(peer);
  peer.write(request);
  peer.flush();
  // Nothing left to write is not a failure; waitForBytesWritten says false then.
  if (peer.bytesToWrite() > 0 && !peer.waitForBytesWritten(150))
    return alive(pid) ? Outcome{Outcome::Unresponsive, pid} : Outcome{};
  QByteArray reply;
  QElapsedTimer clock;
  clock.start();
  while (!reply.contains('\n') && clock.elapsed() < patienceMs) {
    // Short steps, so a peer that dies meanwhile is noticed at once.
    if (peer.waitForReadyRead(250))
      reply += peer.readAll();
    else if (peer.state() != QLocalSocket::ConnectedState || !alive(pid))
      break;
  }
  if (reply.startsWith(Ack))
    return {Outcome::Handed, pid};
  // A process that is exiting closes its socket a moment before it is gone.
  if (peer.state() != QLocalSocket::ConnectedState && waitUntilGone(pid, 2000))
    return {};
  return alive(pid) ? Outcome{Outcome::Unresponsive, pid} : Outcome{};
}

// The running side: say the request arrived before acting on it.
inline void acknowledge(QLocalSocket *socket) {
  socket->write(Ack);
  socket->flush();
}

} // namespace SingleInstance
