#include "singleinstance.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QThread>
#include <QtTest>

// A second launch hands its request to the running Musix when that one
// answers, waits while it is alive but busy, takes over only once it is gone,
// and never runs beside a living instance that has stopped answering.
class SingleInstanceTest : public QObject {
  Q_OBJECT
  static QString name(const char *tag) { return QString("musix-test-%1-%2").arg(tag).arg(QCoreApplication::applicationPid()); }

  // A running instance on its own thread, as it would be in its own process,
  // answering after `delayMs` of being busy.
  struct Instance {
    QThread thread;
    QObject host;
    QLocalServer *server = nullptr;
    QByteArray received;
    Instance(const QString &name, int delayMs) {
      host.moveToThread(&thread);
      thread.start();
      QMetaObject::invokeMethod(&host, [this, name, delayMs] {
        server = new QLocalServer(&host);
        QLocalServer::removeServer(name);
        server->listen(name);
        QObject::connect(server, &QLocalServer::newConnection, server, [this, delayMs] {
          auto socket = server->nextPendingConnection();
          QObject::connect(socket, &QLocalSocket::readyRead, socket, [this, socket, delayMs] {
            QThread::msleep(delayMs); // busy: its event loop gets to the request late
            SingleInstance::acknowledge(socket);
            received += socket->readAll();
          });
        });
      }, Qt::BlockingQueuedConnection);
    }
    ~Instance() {
      QMetaObject::invokeMethod(&host, [this] { delete server; }, Qt::BlockingQueuedConnection);
      thread.quit();
      thread.wait();
    }
  };

private slots:
  void nobodyRunningMeansStartNormally() {
    QElapsedTimer clock;
    clock.start();
    QCOMPARE(SingleInstance::handOff(name("none"), "raise").kind, SingleInstance::Outcome::Gone);
    QVERIFY(clock.elapsed() < 1000);
  }

  void aResponsiveInstanceTakesTheRequest() {
    Instance running(name("live"), 0);
    QVERIFY(running.server && running.server->isListening());
    const auto outcome = SingleInstance::handOff(name("live"), "https://music.youtube.com/watch?v=abc");
    QCOMPARE(outcome.kind, SingleInstance::Outcome::Handed);
    QCOMPARE(outcome.pid, qint64(QCoreApplication::applicationPid()));
    QTRY_COMPARE(running.received, QByteArray("https://music.youtube.com/watch?v=abc"));
  }

  void aBusyInstanceIsWaitedFor() {
    // Answering after three seconds is slow, not gone: the launch waits and
    // hands over rather than starting a second instance on the same profile.
    Instance running(name("busy"), 3000);
    QElapsedTimer clock;
    clock.start();
    const auto outcome = SingleInstance::handOff(name("busy"), "raise", 120, 10000);
    QCOMPARE(outcome.kind, SingleInstance::Outcome::Handed);
    QVERIFY(clock.elapsed() >= 2900);
    QTRY_COMPARE(running.received, QByteArray("raise"));
  }

  void aSilentLivingInstanceIsReportedNotTakenOver() {
    // Listening but never reaching its event loop, like an instance held in
    // its launch: the process is alive, so the person has to decide.
    QLocalServer stuck;
    QLocalServer::removeServer(name("stuck"));
    QVERIFY(stuck.listen(name("stuck")));
    QElapsedTimer clock;
    clock.start();
    const auto outcome = SingleInstance::handOff(name("stuck"), "raise", 120, 1500);
    QCOMPARE(outcome.kind, SingleInstance::Outcome::Unresponsive);
    QCOMPARE(outcome.pid, qint64(QCoreApplication::applicationPid()));
    QVERIFY(clock.elapsed() >= 1400);
  }

  void anInstanceThatDiesWhileSilentIsTakenOver() {
    // Another process holds the socket and says nothing, then goes away in
    // the middle of the wait: the launch notices and starts on its own.
    const auto path = QDir::temp().filePath(name("dying"));
    QFile::remove(path);
    QProcess holder;
    // The holder forks and its parent exits, so launchd adopts and reaps it
    // the moment it exits, as it does a real Musix; a child of this test
    // would linger as a zombie and still look alive.
    holder.start("/usr/bin/python3", {"-c", "import os,socket,sys,time\n"
                                             "if os.fork(): sys.exit(0)\n"
                                             "s=socket.socket(socket.AF_UNIX);s.bind(sys.argv[1]);s.listen(4)\n"
                                             "print('ready',flush=True);time.sleep(1)", path});
    QVERIFY(holder.waitForReadyRead(10000));
    QElapsedTimer clock;
    clock.start();
    const auto outcome = SingleInstance::handOff(name("dying"), "raise", 120, 10000);
    QCOMPARE(outcome.kind, SingleInstance::Outcome::Gone);
    QVERIFY2(clock.elapsed() < 3000, qPrintable(QString::number(clock.elapsed())));
    holder.waitForFinished();
    QFile::remove(path);
  }

  void livenessIsTheProcessItself() {
    QVERIFY(SingleInstance::alive(QCoreApplication::applicationPid()));
    QVERIFY(!SingleInstance::alive(0));
    QVERIFY(!SingleInstance::alive(999999));
  }
};

QTEST_GUILESS_MAIN(SingleInstanceTest)
#include "singleinstance_test.moc"
