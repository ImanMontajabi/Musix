// The update check against a local stand-in for GitHub: tag parsing, version
// order, which releases count, what an automatic check keeps quiet about,
// and what a failure says. Nothing here reaches the real API.
#include "updatechecker.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QJsonObject release(const QString &tag, bool dmg = true, bool draft = false, bool prerelease = false) {
  QJsonArray assets{QJsonObject{{"name", "Musix-" + tag.mid(1) + "-ffmpeg-7.1-source.tar.xz"}}};
  if (dmg)
    assets.append(QJsonObject{{"name", "Musix-" + tag.mid(1) + "-arm64.dmg"}});
  return {{"tag_name", tag}, {"draft", draft}, {"prerelease", prerelease}, {"assets", assets},
          // Never to be opened: the page comes from the tag, not the response.
          {"html_url", "https://evil.example/" + tag}};
}
} // namespace

class UpdateTest : public QObject {
  Q_OBJECT
  QTemporaryDir storage;
  QTcpServer http;
  QByteArray status = "200 OK", headers, body;
  QUrl endpoint;

private slots:
  void initTestCase() {
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, storage.path());
    QCoreApplication::setOrganizationName("MusixTests");
    QCoreApplication::setApplicationName("updates");
    QVERIFY(http.listen(QHostAddress::LocalHost));
    endpoint = QUrl("http://127.0.0.1:" + QString::number(http.serverPort()) + "/releases");
    connect(&http, &QTcpServer::newConnection, this, [this] {
      while (http.hasPendingConnections()) {
        auto socket = http.nextPendingConnection();
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
          const auto request = socket->property("request").toByteArray() + socket->readAll();
          socket->setProperty("request", request);
          if (!request.contains("\r\n\r\n") || socket->property("sent").toBool())
            return;
          socket->setProperty("sent", true);
          socket->write("HTTP/1.1 " + status + "\r\n" + headers + "Content-Type: application/json\r\nContent-Length: " +
                        QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
          socket->disconnectFromHost();
        });
      }
    });
  }
  void init() {
    QSettings().clear();
    status = "200 OK";
    headers.clear();
    body.clear();
  }

  void tagsReadWithEitherV() {
    QCOMPARE(UpdateChecker::parseTag("v0.14.0"), QVersionNumber(0, 14));
    QCOMPARE(UpdateChecker::parseTag("V0.13.0"), QVersionNumber(0, 13));
    QCOMPARE(UpdateChecker::parseTag("0.14.10"), QVersionNumber(0, 14, 10));
    QCOMPARE(UpdateChecker::parseTag(" v1.2 "), QVersionNumber(1, 2));
    for (const auto *bad : {"v0.14.0-rc1", "latest", "", "v", "v0.14.x", "musix-0.14.0", "v0.14.0/../x"})
      QVERIFY2(UpdateChecker::parseTag(bad).isNull(), bad);
  }
  void versionsCompareAsNumbers() {
    QVERIFY(UpdateChecker::parseTag("v0.14.10") > UpdateChecker::parseTag("v0.14.9"));
    QVERIFY(UpdateChecker::parseTag("v0.14.0") > UpdateChecker::parseTag("V0.13.1"));
    QVERIFY(UpdateChecker::parseTag("v1.0") > UpdateChecker::parseTag("v0.99.99"));
    // 0.14 and 0.14.0 are one release; neither is newer than the other.
    QCOMPARE(UpdateChecker::parseTag("v0.14"), UpdateChecker::parseTag("v0.14.0"));
  }
  void onlyInstallableReleasesCount() {
    const QJsonArray releases{release("v0.16.0", true, true), release("v0.15.0", true, false, true),
                              release("v0.14.10", false), release("v0.14.9"), release("V0.13.0"),
                              QJsonObject{{"tag_name", "nightly"}, {"draft", false}, {"prerelease", false}},
                              QJsonObject{{"draft", false}}};
    const auto best = UpdateChecker::newest(releases);
    QCOMPARE(best.tag, QString("v0.14.9"));
    QCOMPARE(best.name(), QString("0.14.9"));
    QVERIFY(!UpdateChecker::newest({}).valid());
    // A release missing the draft or prerelease flags is not assumed published.
    QVERIFY(!UpdateChecker::newest({QJsonObject{{"tag_name", "v9.0.0"}, {"assets", QJsonArray{QJsonObject{{"name", "Musix-9.0.0-arm64.dmg"}}}}}}).valid());
  }
  void pagesAreOnlyEverThisRepository() {
    QCOMPARE(UpdateChecker::releasePage("v0.14.0"), QUrl("https://github.com/ImanMontajabi/Musix/releases/tag/v0.14.0"));
    QCOMPARE(UpdateChecker::releasePage("V0.13.0"), QUrl("https://github.com/ImanMontajabi/Musix/releases/tag/V0.13.0"));
    QVERIFY(!UpdateChecker::releasePage("../../evil").isValid());
    QVERIFY(!UpdateChecker::releasePage("v1.0?x=https://evil.example").isValid());
  }
  void endpointMustBeLoopback() {
    UpdateChecker checker("0.14.0");
    QVERIFY(!checker.setEndpoint(QUrl("https://evil.example/releases")));
    QVERIFY(!checker.setEndpoint(QUrl("http://192.168.1.2/releases")));
    QVERIFY(checker.setEndpoint(endpoint));
  }

  void newerReleaseIsOffered() {
    body = QJsonDocument(QJsonArray{release("v0.14.1"), release("v0.14.0")}).toJson();
    UpdateChecker checker("0.14.0");
    QVERIFY(checker.setEndpoint(endpoint));
    QSignalSpy offered(&checker, &UpdateChecker::available);
    checker.check(true);
    QVERIFY(checker.checking());
    QTRY_COMPARE(offered.count(), 1);
    QCOMPARE(offered.first().at(0).toString(), QString("0.14.1"));
    QCOMPARE(offered.first().at(1).toString(), QString("0.14.0"));
    QCOMPARE(checker.availableVersion(), QString("0.14.1"));
    QVERIFY(!checker.checking());
  }
  void currentReleaseIsUpToDate() {
    body = QJsonDocument(QJsonArray{release("v0.14.0"), release("V0.13.0")}).toJson();
    UpdateChecker checker("0.14.0");
    QVERIFY(checker.setEndpoint(endpoint));
    QSignalSpy current(&checker, &UpdateChecker::upToDate), offered(&checker, &UpdateChecker::available);
    checker.check(true);
    QTRY_COMPARE(current.count(), 1);
    QCOMPARE(offered.count(), 0);
    QVERIFY(checker.status().startsWith("You’re up to date"));
  }
  void automaticCheckRespectsLaterAndSkip() {
    body = QJsonDocument(QJsonArray{release("v0.14.1")}).toJson();
    UpdateChecker checker("0.14.0");
    QVERIFY(checker.setEndpoint(endpoint));
    QSignalSpy offered(&checker, &UpdateChecker::available);
    checker.check(false);
    QTRY_COMPARE(offered.count(), 1);
    QCOMPARE(offered.first().at(2).toBool(), false);
    checker.later();
    checker.check(false);
    QTRY_VERIFY(!checker.checking());
    QCOMPARE(offered.count(), 1);  // put off until tomorrow
    QSettings().setValue("updates/laterAt", QDateTime::currentSecsSinceEpoch() - 25 * 60 * 60);
    checker.check(false);
    QTRY_COMPARE(offered.count(), 2);  // and it is tomorrow
    checker.skip();
    QSettings().setValue("updates/laterAt", 0);
    checker.check(false);
    QTRY_VERIFY(!checker.checking());
    QCOMPARE(offered.count(), 2);  // skipped for good
    checker.check(true);
    QTRY_COMPARE(offered.count(), 3);  // but a manual check always says
    // A newer version than the skipped one is news again.
    body = QJsonDocument(QJsonArray{release("v0.14.2")}).toJson();
    checker.check(false);
    QTRY_COMPARE(offered.count(), 4);
    QCOMPARE(offered.last().at(0).toString(), QString("0.14.2"));
  }
  void automaticCheckRecordsTheDay() {
    body = QJsonDocument(QJsonArray{release("v0.14.0")}).toJson();
    UpdateChecker checker("0.14.0");
    QVERIFY(checker.setEndpoint(endpoint));
    QVERIFY(!QSettings().contains("updates/lastCheck"));
    checker.check(false);
    QTRY_VERIFY(!checker.checking());
    QVERIFY(qAbs(QSettings().value("updates/lastCheck").toLongLong() - QDateTime::currentSecsSinceEpoch()) < 5);
    QVERIFY(checker.automatic());
    checker.setAutomatic(false);
    QVERIFY(!checker.automatic() && !QSettings().value("updates/automatic", true).toBool());
  }
  void rateLimitSaysSo() {
    status = "403 Forbidden";
    headers = "x-ratelimit-remaining: 0\r\nx-ratelimit-reset: " + QByteArray::number(QDateTime::currentSecsSinceEpoch() + 1800) + "\r\n";
    body = R"({"message":"API rate limit exceeded"})";
    UpdateChecker checker("0.14.0");
    QVERIFY(checker.setEndpoint(endpoint));
    QSignalSpy failed(&checker, &UpdateChecker::failed);
    checker.check(true);
    QTRY_COMPARE(failed.count(), 1);
    QVERIFY2(failed.first().at(0).toString().contains("limiting") && failed.first().at(0).toString().contains("Try again after"),
             qPrintable(failed.first().at(0).toString()));
  }
  void serverErrorAndNonsenseAreErrors() {
    UpdateChecker checker("0.14.0");
    QVERIFY(checker.setEndpoint(endpoint));
    QSignalSpy failed(&checker, &UpdateChecker::failed), offered(&checker, &UpdateChecker::available);
    status = "500 Internal Server Error";
    checker.check(true);
    QTRY_COMPARE(failed.count(), 1);
    QVERIFY(failed.last().at(0).toString().contains("HTTP 500"));
    status = "200 OK";
    body = "<html>not json</html>";
    checker.check(true);
    QTRY_COMPARE(failed.count(), 2);
    body = R"({"tag_name":"v9.0.0"})";
    checker.check(true);
    QTRY_COMPARE(failed.count(), 3);
    QCOMPARE(offered.count(), 0);
  }
  void offlineSaysSo() {
    QTcpServer closed;
    QVERIFY(closed.listen(QHostAddress::LocalHost));
    const auto port = closed.serverPort();
    closed.close();
    UpdateChecker checker("0.14.0");
    QVERIFY(checker.setEndpoint(QUrl("http://127.0.0.1:" + QString::number(port) + "/releases")));
    QSignalSpy failed(&checker, &UpdateChecker::failed);
    checker.check(true);
    QTRY_COMPARE(failed.count(), 1);
    QVERIFY(failed.first().at(0).toString().contains("Couldn’t reach GitHub"));
  }
  void automaticFailureIsSilent() {
    status = "500 Internal Server Error";
    UpdateChecker checker("0.14.0");
    QVERIFY(checker.setEndpoint(endpoint));
    QSignalSpy failed(&checker, &UpdateChecker::failed);
    checker.check(false);
    QTRY_VERIFY(!checker.checking());
    QCOMPARE(failed.count(), 0);
    QVERIFY(checker.status().contains("HTTP 500"));
  }
  void redirectOffHostIsNotFollowed() {
    // Somewhere else that would answer, and answer with a newer release: if the
    // redirect were followed, the check would offer it.
    QTcpServer elsewhere;
    QVERIFY(elsewhere.listen(QHostAddress::LocalHost));
    bool reached = false;
    connect(&elsewhere, &QTcpServer::newConnection, this, [&] {
      reached = true;
      auto socket = elsewhere.nextPendingConnection();
      const auto answer = QJsonDocument(QJsonArray{release("v9.0.0")}).toJson();
      connect(socket, &QTcpSocket::readyRead, socket, [socket, answer] {
        socket->readAll();
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                      QByteArray::number(answer.size()) + "\r\nConnection: close\r\n\r\n" + answer);
        socket->disconnectFromHost();
      });
    });
    status = "302 Found";
    headers = "Location: http://localhost:" + QByteArray::number(elsewhere.serverPort()) + "/releases\r\n";
    UpdateChecker checker("0.14.0");
    QVERIFY(checker.setEndpoint(endpoint));
    QSignalSpy failed(&checker, &UpdateChecker::failed), offered(&checker, &UpdateChecker::available);
    checker.check(true);
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(offered.count(), 0);
    QVERIFY(!reached);
  }
};

QTEST_MAIN(UpdateTest)
#include "update_test.moc"
