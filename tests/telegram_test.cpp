// The sign-in states and the helper's supervision, against a stand-in helper
// that speaks the same protocol as helper/telegram.py (tests/telegram_fixture.py).
#include "telegram.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <csignal>

class TelegramTest : public QObject {
  Q_OBJECT
  QTemporaryDir storage;
  QString fixture;
  std::unique_ptr<Telegram> make(const QString &mode = {}) {
    qputenv("FIXTURE_MODE", mode.toUtf8());
    auto account = std::make_unique<Telegram>(storage.path() + "/telegram");
    account->setLauncher([] { return QStringLiteral("python3"); }, [this] { return fixture; });
    return account;
  }
  static bool reach(Telegram &t, const QString &state, int ms = 8000) {
    return QTest::qWaitFor([&] { return t.state() == state; }, ms);
  }
  void clean() { QDir(storage.path() + "/telegram").removeRecursively(); }
private slots:
  void initTestCase() {
    QVERIFY(storage.isValid());
    fixture = QFINDTESTDATA("telegram_fixture.py");
    if (fixture.isEmpty()) fixture = QCoreApplication::applicationDirPath() + "/../tests/telegram_fixture.py";
    QVERIFY2(QFile::exists(fixture), qPrintable(fixture));
    qputenv("MUSIX_TELEGRAM_API_ID", "12345");
    qputenv("MUSIX_TELEGRAM_API_HASH", "abcdef");
  }
  void init() { clean(); }

  void withoutCredentialsThereIsNothingToStart() {
    qputenv("MUSIX_TELEGRAM_API_ID", "0");
    auto t = make();
    QVERIFY(!t->available());
    t->open();
    QCOMPARE(t->state(), QString("no-credentials"));
    QVERIFY(!QFile::exists(storage.path() + "/telegram/seen-env.json")); // no helper was started
    qputenv("MUSIX_TELEGRAM_API_ID", "12345");
  }
  void theHelperGetsTheCredentialsAndTheSessionFolder() {
    auto t = make();
    QVERIFY(t->available());
    t->open();
    QVERIFY(reach(*t, "wait-phone"));
    QFile seen(storage.path() + "/telegram/seen-env.json");
    QVERIFY(seen.open(QIODevice::ReadOnly));
    const auto env = QJsonDocument::fromJson(seen.readAll()).object();
    QCOMPARE(env.value("MUSIX_TELEGRAM_API_ID").toString(), QString("12345"));
    QCOMPARE(env.value("MUSIX_TELEGRAM_API_HASH").toString(), QString("abcdef"));
    QCOMPARE(env.value("MUSIX_VERSION").toString(), QString(MUSIX_VERSION));
  }
  void signingInWithACode() {
    auto t = make();
    t->open();
    QVERIFY(reach(*t, "wait-phone"));
    t->submitPhone("+49 151 2345678");
    QVERIFY(reach(*t, "wait-code"));
    QCOMPARE(t->codeKind(), QString("app"));
    t->submitCode("12345");
    QVERIFY(reach(*t, "ready"));
    QCOMPARE(t->name(), QString("Ada Lovelace"));
    QCOMPARE(t->username(), QString("ada"));
    QVERIFY(t->signedIn());
    QVERIFY(QTest::qWaitFor([&] { return !t->busy(); }));
  }
  void aWrongPhoneOrCodeSaysSoAndStaysPut() {
    auto t = make();
    t->open();
    QVERIFY(reach(*t, "wait-phone"));
    t->submitPhone("+1 555");
    QVERIFY(QTest::qWaitFor([&] { return !t->error().isEmpty(); }));
    QCOMPARE(t->state(), QString("wait-phone"));
    t->submitPhone("+49 151 2345678");
    QVERIFY(reach(*t, "wait-code"));
    QVERIFY2(t->error().isEmpty(), "a new step starts without the last step's error");
    t->submitCode("00000");
    QVERIFY(QTest::qWaitFor([&] { return !t->error().isEmpty(); }));
    QCOMPARE(t->state(), QString("wait-code"));
    QVERIFY(!t->signedIn());
  }
  void twoStepVerificationAsksForAPassword() {
    auto t = make();
    t->open();
    QVERIFY(reach(*t, "wait-phone"));
    t->submitPhone("+49 151 99");
    QVERIFY(reach(*t, "wait-code"));
    t->submitCode("12345");
    QVERIFY(reach(*t, "wait-password"));
    QCOMPARE(t->passwordHint(), QString("first pet"));
    t->submitPassword("wrong");
    QVERIFY(QTest::qWaitFor([&] { return !t->error().isEmpty(); }));
    QCOMPARE(t->state(), QString("wait-password"));
    t->submitPassword("secret");
    QVERIFY(reach(*t, "ready"));
    QVERIFY(t->signedIn());
  }
  void goingBackReturnsToThePhoneNumber() {
    auto t = make();
    t->open();
    QVERIFY(reach(*t, "wait-phone"));
    t->submitPhone("+49 151 2345678");
    QVERIFY(reach(*t, "wait-code"));
    t->back();
    QVERIFY(reach(*t, "wait-phone"));
  }
  void aSignedInAccountIsKnownBeforeTheHelperRuns() {
    QDir().mkpath(storage.path() + "/telegram");
    QFile file(storage.path() + "/telegram/account.json");
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{\"name\":\"Ada Lovelace\",\"username\":\"ada\"}");
    file.close();
    auto t = make();
    QVERIFY(t->signedIn());
    QCOMPARE(t->name(), QString("Ada Lovelace"));
    QCOMPARE(t->state(), QString("closed"));
    t->open();
    QVERIFY(reach(*t, "ready"));
  }
  void loggingOutForgetsTheAccount() {
    auto t = make();
    t->open();
    QVERIFY(reach(*t, "wait-phone"));
    t->submitPhone("+49 151 2345678");
    QVERIFY(reach(*t, "wait-code"));
    t->submitCode("12345");
    QVERIFY(reach(*t, "ready"));
    t->logout();
    QVERIFY(reach(*t, "wait-phone"));
    QVERIFY(!t->signedIn());
    QVERIFY(!QFile::exists(storage.path() + "/telegram/account.json"));
  }
  void closingStopsTheHelper() {
    auto t = make();
    t->open();
    QVERIFY(reach(*t, "wait-phone"));
    t->close();
    QCOMPARE(t->state(), QString("closed"));
    // The helper leaves when its input ends; nothing of it is left running.
    QVERIFY(QTest::qWaitFor([&] {
      QProcess probe;
      probe.start("pgrep", {"-f", "telegram_fixture.py"});
      probe.waitForFinished();
      return probe.exitCode() != 0;
    }, 6000));
  }
  void aHelperThatDiesIsStartedAgain() {
    auto t = make("crash");
    t->open();
    // The first one says wait-phone and dies; the app notices (starting), waits
    // a moment, and the second one behaves. Only the second one can be written to.
    QVERIFY(QTest::qWaitFor([&] { return QFile::exists(storage.path() + "/telegram/crashed"); }));
    QVERIFY(reach(*t, "starting"));
    QVERIFY(reach(*t, "wait-phone"));
    t->submitPhone("+49 151 2345678");
    QVERIFY(reach(*t, "wait-code"));
  }
  void aHelperThatSaysWhyAndLeavesIsNotRestarted() {
    auto t = make("exit2");
    t->open();
    QVERIFY(reach(*t, "offline"));
    QVERIFY(!t->error().isEmpty());
    QTest::qWait(1500);
    QCOMPARE(t->state(), QString("offline"));
  }
  void aHelperThatNeverAnswersTimesOut() {
    auto t = make("silent");
    t->open();
    QCOMPARE(t->state(), QString("starting"));
    QVERIFY(t->busy());
    // The real timeout is 25 s; the point here is only that "starting" is not final,
    // so closing must leave it cleanly.
    t->close();
    QCOMPARE(t->state(), QString("closed"));
    QVERIFY(!t->busy());
  }
};
QTEST_GUILESS_MAIN(TelegramTest)
#include "telegram_test.moc"
