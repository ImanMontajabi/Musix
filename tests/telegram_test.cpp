// The sign-in states and the helper's supervision, against a stand-in helper
// that speaks the same protocol as helper/telegram.py (tests/telegram_fixture.py).
#include "telegram.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QJsonArray>
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

  // --- Choosing sources and syncing them ---------------------------------

  void signIn() {
    QDir().mkpath(storage.path() + "/telegram");
    QFile file(storage.path() + "/telegram/account.json");
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{\"name\":\"Ada Lovelace\",\"username\":\"ada\",\"userId\":4242}");
  }
  static bool settled(Telegram &t, const QString &key) {
    return QTest::qWaitFor([&] {
      for (const auto &v : t.sources())
        if (v.toMap().value("key") == key) {
          const auto status = v.toMap().value("status").toString();
          return status != "queued" && status != "syncing" && status != "waiting";
        }
      return false;
    }, 15000);
  }
  QVariantMap source(Telegram &t, const QString &key) {
    for (const auto &v : t.sources())
      if (v.toMap().value("key") == key) return v.toMap();
    return {};
  }
private slots:
  void thechatsToChooseFromAreListed() {
    signIn();
    auto t = make();
    t->loadChats();
    QVERIFY(t->listing());
    QVERIFY(QTest::qWaitFor([&] { return !t->listing(); }, 10000));
    QCOMPARE(t->chats().size(), 4);
    QCOMPARE(t->chats().first().toMap().value("title").toString(), QString("Saved Messages"));
    QCOMPARE(t->chats().first().toMap().value("chatId").toString(), QString("4242"));
    QCOMPARE(t->chats().at(1).toMap().value("chatId").toString(), QString("-1001"));
    QCOMPARE(t->chats().at(3).toMap().value("kind").toString(), QString("forum"));
    // Nothing keeps the helper once the list is in.
    QVERIFY(reach(*t, "closed"));
  }
  void aForumsTopicsAreListed() {
    signIn();
    auto t = make();
    t->loadTopics("-1003");
    QVERIFY(QTest::qWaitFor([&] { return !t->listing(); }, 10000));
    QCOMPARE(t->topics().size(), 2);
    QCOMPARE(t->topics().first().toMap().value("title").toString(), QString("Jazz"));
    QCOMPARE(t->topics().first().toMap().value("topicId").toString(), QString("11"));
  }
  void aPublicChannelIsFoundByNameAndAnUnknownOneSaysSo() {
    signIn();
    auto t = make();
    t->search("https://t.me/public_music");
    QVERIFY(QTest::qWaitFor([&] { return !t->listing(); }, 10000));
    QCOMPARE(t->found().value("title").toString(), QString("Public Music"));
    QCOMPARE(t->found().value("chatId").toString(), QString("-100777"));
    t->search("@nobody");
    QVERIFY(QTest::qWaitFor([&] { return !t->listing(); }, 10000));
    QVERIFY(t->found().isEmpty());
    QVERIFY(t->listError().contains("nobody"));
  }
  void addingASourceSyncsItIntoRowsTheLibraryCanKeep() {
    signIn();
    auto t = make();
    QSignalSpy ready(t.get(), &Telegram::songsReady);
    QStringList seen;
    connect(t.get(), &Telegram::sourcesChanged, this, [&] { seen << source(*t, "-1001").value("status").toString(); });
    t->addSource("-1001", "News", "channel", "news");
    QCOMPARE(source(*t, "-1001").value("status").toString(), QString("queued"));
    QVERIFY(settled(*t, "-1001"));
    QCOMPARE(ready.count(), 1);
    const auto songs = ready.first().at(2).toList();
    QCOMPARE(ready.first().at(0).toString(), QString("-1001"));
    QCOMPARE(ready.first().at(1).toString(), QString("News"));
    QVERIFY(ready.first().at(3).toBool()); // complete
    QCOMPARE(songs.size(), 250);
    const auto first = songs.first().toMap();
    QCOMPARE(first.value("source").toString(), QString("telegram"));
    QCOMPARE(first.value("kind").toString(), QString("song"));
    QCOMPARE(first.value("remoteId").toString(), QString("-1001:1000"));
    QCOMPARE(first.value("server").toString(), QString("telegram:4242"));
    QCOMPARE(first.value("album").toString(), QString("News"));
    QCOMPARE(first.value("seconds").toInt(), 180);
    QSet<QString> ids;
    for (const auto &s : songs) ids.insert(s.toMap().value("id").toString());
    QCOMPARE(ids.size(), 250); // every song has an id of its own
    QVERIFY(seen.contains("syncing"));
    QVERIFY2(seen.contains("waiting"), "a pause Telegram asked for is shown");
    const auto done = source(*t, "-1001");
    QCOMPARE(done.value("count").toInt(), 250);
    QVERIFY(done.value("syncedAt").toLongLong() > 0);
    QCOMPARE(done.value("status").toString(), QString(""));
    // With the work done and no dialog open, the helper goes.
    QVERIFY(reach(*t, "closed"));
  }
  void aForumTopicIsASourceOfItsOwn() {
    signIn();
    auto t = make();
    QSignalSpy ready(t.get(), &Telegram::songsReady);
    t->addSource("-1003", "Forum", "forum", "", "11", "Jazz");
    QVERIFY(settled(*t, "-1003/11"));
    QCOMPARE(source(*t, "-1003/11").value("title").toString(), QString("Forum › Jazz"));
    QCOMPARE(ready.first().at(2).toList().size(), 2);
    QCOMPARE(ready.first().at(2).toList().first().toMap().value("telegramTopic").toString(), QString("11"));
    // The whole forum is another source, with its own key.
    t->addSource("-1003", "Forum", "forum", "");
    QVERIFY(settled(*t, "-1003"));
    QCOMPARE(t->sources().size(), 2);
  }
  void aSourceIsOnlyAddedOnce() {
    signIn();
    auto t = make();
    t->addSource("4242", "Saved Messages", "saved", "");
    t->addSource("4242", "Saved Messages", "saved", "");
    QCOMPARE(t->sources().size(), 1);
    QVERIFY(t->hasSource("4242"));
    QVERIFY(settled(*t, "4242"));
  }
  void sourcesAreRememberedAndRefreshedByHand() {
    signIn();
    {
      auto t = make();
      t->addSource("-1002", "Friends", "group", "");
      QVERIFY(settled(*t, "-1002"));
    }
    auto again = make();
    QCOMPARE(again->sources().size(), 1);
    QCOMPARE(source(*again, "-1002").value("count").toInt(), 4);
    QCOMPARE(source(*again, "-1002").value("status").toString(), QString(""));
    QSignalSpy ready(again.get(), &Telegram::songsReady);
    again->refresh("-1002");
    QVERIFY(QTest::qWaitFor([&] { return ready.count() == 1; }, 10000));
    QCOMPARE(ready.first().at(2).toList().size(), 4);
    QVERIFY(ready.first().at(3).toBool());
  }
  void removingASourceForgetsIt() {
    signIn();
    auto t = make();
    t->addSource("-1002", "Friends", "group", "");
    QVERIFY(settled(*t, "-1002"));
    QSignalSpy gone(t.get(), &Telegram::sourceRemoved);
    t->removeSource("-1002");
    QCOMPARE(gone.count(), 1);
    QCOMPARE(gone.first().first().toString(), QString("-1002"));
    QVERIFY(t->sources().isEmpty());
    auto again = make();
    QVERIFY(again->sources().isEmpty());
    QCOMPARE(Telegram::playlistId("-1002"), QString("telegram:-1002"));
  }
  void aLongPauseEndsTheSyncButKeepsWhatArrived() {
    signIn();
    auto t = make("flood");
    QSignalSpy ready(t.get(), &Telegram::songsReady);
    t->addSource("-1001", "News", "channel", "news");
    QVERIFY(settled(*t, "-1001"));
    QCOMPARE(source(*t, "-1001").value("status").toString(), QString("error"));
    QVERIFY(source(*t, "-1001").value("message").toString().contains("wait 2 hours"));
    QCOMPARE(ready.count(), 1);
    QCOMPARE(ready.first().at(2).toList().size(), 100);
    QVERIFY2(!ready.first().at(3).toBool(), "partial: the owner merges it instead of replacing");
  }
  void aRunningSyncCanBeCancelled() {
    signIn();
    auto t = make("slow");
    QSignalSpy ready(t.get(), &Telegram::songsReady);
    t->addSource("-1001", "News", "channel", "news");
    QVERIFY(QTest::qWaitFor([&] { return source(*t, "-1001").value("count").toInt() >= 100; }, 10000));
    t->cancelSync("-1001");
    QVERIFY(settled(*t, "-1001"));
    QCOMPARE(source(*t, "-1001").value("status").toString(), QString(""));
    QCOMPARE(source(*t, "-1001").value("message").toString(), QString("Cancelled."));
    QCOMPARE(ready.count(), 1);
    QVERIFY(!ready.first().at(3).toBool());
  }
  void aRefreshWithoutASessionSaysToSignInAndLetsTheHelperGo() {
    // No account.json: the helper starts signed out.
    auto t = make();
    QDir().mkpath(storage.path() + "/telegram");
    t->addSource("-1002", "Friends", "group", "");
    QVERIFY(settled(*t, "-1002"));
    QCOMPARE(source(*t, "-1002").value("status").toString(), QString("error"));
    QVERIFY(source(*t, "-1002").value("message").toString().contains("Sign in"));
    QVERIFY(reach(*t, "closed"));
  }
  void aDialogHoldsTheHelperWhileWorkComesAndGoes() {
    signIn();
    auto t = make();
    t->open();
    QVERIFY(reach(*t, "ready"));
    t->addSource("-1002", "Friends", "group", "");
    QVERIFY(settled(*t, "-1002"));
    QCOMPARE(t->state(), QString("ready")); // still up: the dialog is open
    t->close();
    QCOMPARE(t->state(), QString("closed"));
  }
};
QTEST_GUILESS_MAIN(TelegramTest)
#include "telegram_test.moc"
