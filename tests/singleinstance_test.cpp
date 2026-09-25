#include "singleinstance.h"
#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QtTest>

// A second launch hands its request to the running Musix only when that one
// answers; one that cannot answer must not swallow the launch.
class SingleInstanceTest : public QObject {
  Q_OBJECT
  static QString name(const char *tag) { return QString("musix-test-%1-%2").arg(tag).arg(QCoreApplication::applicationPid()); }
private slots:
  void nobodyRunningMeansStartNormally() {
    QElapsedTimer clock;
    clock.start();
    QVERIFY(!SingleInstance::handOff(name("none"), "raise"));
    QVERIFY(clock.elapsed() < 1000);
  }

  void aResponsiveInstanceTakesTheRequest() {
    // The running instance lives on its own thread with its own event loop,
    // as it does in its own process.
    QThread thread;
    QObject host;
    host.moveToThread(&thread);
    thread.start();
    struct Stop { QThread &t; ~Stop() { t.quit(); t.wait(); } } stop{thread};
    QByteArray received;
    QLocalServer *server = nullptr;
    QMetaObject::invokeMethod(&host, [&] {
      server = new QLocalServer(&host);
      QLocalServer::removeServer(name("live"));
      server->listen(name("live"));
      QObject::connect(server, &QLocalServer::newConnection, server, [&] {
        auto socket = server->nextPendingConnection();
        QObject::connect(socket, &QLocalSocket::readyRead, socket, [&, socket] {
          SingleInstance::acknowledge(socket);
          received += socket->readAll();
        });
      });
    }, Qt::BlockingQueuedConnection);
    QVERIFY(server && server->isListening());
    QVERIFY(SingleInstance::handOff(name("live"), "https://music.youtube.com/watch?v=abc"));
    QTRY_COMPARE(received, QByteArray("https://music.youtube.com/watch?v=abc"));
    QMetaObject::invokeMethod(&host, [&] { delete server; }, Qt::BlockingQueuedConnection);
  }

  void aStuckInstanceDoesNotSwallowTheLaunch() {
    // Listening but never getting to its event loop, like an instance held
    // before it finished launching: the connection is accepted by the system
    // and then nothing answers.
    QLocalServer stuck;
    QLocalServer::removeServer(name("stuck"));
    QVERIFY(stuck.listen(name("stuck")));
    QElapsedTimer clock;
    clock.start();
    QVERIFY(!SingleInstance::handOff(name("stuck"), "raise", 120, 800));
    QVERIFY(clock.elapsed() >= 700);
    QVERIFY(clock.elapsed() < 3000);
  }
};

QTEST_GUILESS_MAIN(SingleInstanceTest)
#include "singleinstance_test.moc"
