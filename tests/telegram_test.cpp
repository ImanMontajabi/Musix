// The sign-in states and the helper's supervision, against a stand-in helper
// that speaks the same protocol as helper/telegram.py (tests/telegram_fixture.py).
#include "telegram.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QUrl>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>
#include <csignal>

struct Got {
  bool done = false;
  QVariantMap data;
  QString error;
};

// One GET against the helper's loopback server: {status, body}. The body is read
// to the length the headers promise, or until the server cuts the connection.
static QPair<int, QByteArray> httpGet(const QString &address, const QString &range = {}, const QString &path = {}) {
  const QUrl url(address);
  QTcpSocket socket;
  socket.connectToHost(url.host(), url.port());
  if (!socket.waitForConnected(5000)) return {0, {}};
  QByteArray request = "GET " + (path.isEmpty() ? url.path() : path).toUtf8() + " HTTP/1.1\r\nHost: test\r\nConnection: close\r\n";
  if (!range.isEmpty()) request += "Range: bytes=" + range.toUtf8() + "\r\n";
  socket.write(request + "\r\n");
  QByteArray all;
  qint64 length = -1;
  int headerEnd = -1;
  while (true) {
    if (headerEnd < 0 && (headerEnd = all.indexOf("\r\n\r\n")) >= 0) {
      const auto head = all.left(headerEnd).toLower();
      const auto at = head.indexOf("content-length:");
      length = at >= 0 ? head.mid(at + 15, head.indexOf("\r\n", at) < 0 ? -1 : head.indexOf("\r\n", at) - at - 15).trimmed().toLongLong() : 0;
    }
    if (headerEnd >= 0 && all.size() - (headerEnd + 4) >= length) break;
    if (!socket.waitForReadyRead(10000)) break;
    all += socket.readAll();
  }
  all += socket.readAll();
  const int status = all.mid(9, 3).toInt();
  return {status, headerEnd >= 0 ? all.mid(headerEnd + 4) : QByteArray()};
}

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
  void init() { clean(); QDir(cache()).removeRecursively(); QDir(art()).removeRecursively(); QFile::remove(storage.path() + "/telegram/art.log"); }

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
  // A song the way the library keeps it, before anything is known about the file.
  static QVariantMap song(const QString &id, int message, int seconds = 0) {
    return {{"id", id}, {"source", "telegram"}, {"kind", "song"}, {"remoteId", "-1001:" + QString::number(message)},
            {"title", "Untitled"}, {"artist", ""}, {"seconds", seconds}, {"fileName", "Untitled.wav"}};
  }
  static Telegram::Fetched into(Got &got) {
    return [&got](const QVariantMap &data, const QString &error) { got.done = true; got.data = data; got.error = error; };
  }
  bool fetched(Telegram &t, const QVariantMap &s, Got &got) {
    got = {};
    t.fetch(s, into(got));
    return QTest::qWaitFor([&] { return got.done; }, 15000);
  }
  QString cache() const { return storage.path() + "/cache"; }
  QString art() const { return storage.path() + "/art"; }
  QString artFolder(const QString &name) const { return art() + "/" + name; }
  bool artIdle(Telegram &t) { return QTest::qWaitFor([&] { return !t.artPending() && !t.syncing(); }, 15000); }
  int artRequests() const {
    QFile log(storage.path() + "/telegram/art.log");
    return log.open(QIODevice::ReadOnly) ? log.readAll().split('\n').size() - 1 : 0;
  }
  static QStringList files(const QString &folder) { return QDir(folder).entryList(QDir::Files, QDir::Name); }
  static bool ownerOnly(const QString &path) {
    const auto mode = QFileInfo(path).permissions();
    return !(mode & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther));
  }
  int fetchesLogged() const {
    QFile log(storage.path() + "/telegram/fetches.log");
    return log.open(QIODevice::ReadOnly) ? log.readAll().split('\n').size() - 1 : 0;
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
  void aSongIsDownloadedWholeIntoTheCacheAndItsTagsFillTheGaps() {
    signIn();
    auto t = make();
    t->setCacheDirectory(cache());
    QSignalSpy learned(t.get(), &Telegram::songLearned);
    QList<QVariantMap> shown;
    connect(t.get(), &Telegram::downloadChanged, this, [&] { shown.append(t->download()); });
    Got got;
    QVERIFY(fetched(*t, song("tg_one", 12), got));
    QVERIFY2(got.error.isEmpty(), qPrintable(got.error));
    QVERIFY(!got.data.value("cached").toBool());
    QCOMPARE(got.data.value("file").toString(), cache() + "/tg_one.wav");
    QVERIFY(QFileInfo(got.data.value("file").toString()).isFile());
    QCOMPARE(t->cached("tg_one"), cache() + "/tg_one.wav");
    // The length, artist and the file-name title were unknown; the file knew.
    QCOMPARE(learned.count(), 1);
    QCOMPARE(learned.first().at(0).toString(), QString("tg_one"));
    const auto changes = learned.first().at(1).toMap();
    QCOMPARE(changes.value("seconds").toInt(), 1);
    QCOMPARE(changes.value("artist").toString(), QString("Tag Artist"));
    QCOMPARE(changes.value("title").toString(), QString("Tagged 12"));
    // Progress was shown while it ran, and is gone when it finished.
    QVERIFY(!shown.isEmpty());
    QCOMPARE(shown.first().value("id").toString(), QString("tg_one"));
    qint64 most = 0;
    for (const auto &entry : shown) most = qMax(most, entry.value("received").toLongLong());
    QVERIFY(most > 0);
    QVERIFY(t->download().isEmpty());
    // What was learned is kept on disk for the next refresh, readable by the owner only.
    QFile kept(storage.path() + "/telegram/tags.json");
    QVERIFY(kept.exists());
    const auto mode = QFileInfo(kept).permissions();
    QVERIFY(mode.testFlags(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    QVERIFY(!(mode & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther)));
    // The second play is the cache: instant, and the helper is not asked again.
    QVERIFY(fetched(*t, song("tg_one", 12), got));
    QVERIFY(got.data.value("cached").toBool());
    QCOMPARE(fetchesLogged(), 1);
  }
  void aSongWithKnownDetailsIsLeftAlone() {
    signIn();
    auto t = make();
    t->setCacheDirectory(cache());
    QSignalSpy learned(t.get(), &Telegram::songLearned);
    auto known = song("tg_known", 13, 200);
    known["artist"] = "Real Artist";
    known["title"] = "Real Title";
    Got got;
    QVERIFY(fetched(*t, known, got));
    QCOMPARE(learned.count(), 0);
  }
  void theCacheKeepsTheRecentSongsAndDropsTheOldest() {
    signIn();
    auto t = make();
    t->setCacheDirectory(cache());
    t->setCacheLimits(2, 100 * 1024 * 1024);
    Got got;
    for (const char *id : {"tg_a", "tg_b"}) QVERIFY(fetched(*t, song(id, 1), got));
    // Playing a again makes b the oldest.
    QVERIFY(fetched(*t, song("tg_a", 1), got));
    QVERIFY(got.data.value("cached").toBool());
    QVERIFY(fetched(*t, song("tg_c", 1), got));
    QVERIFY(!t->cached("tg_a").isEmpty());
    QVERIFY(t->cached("tg_b").isEmpty());
    QVERIFY(!t->cached("tg_c").isEmpty());
  }
  void theCacheIsAlsoBoundedBySize() {
    signIn();
    auto t = make();
    t->setCacheDirectory(cache());
    t->setCacheLimits(10, 20000); // a song is a little over 16 kB
    Got got;
    for (const char *id : {"tg_a", "tg_b", "tg_c"}) QVERIFY(fetched(*t, song(id, 1), got));
    QVERIFY(!t->cached("tg_c").isEmpty()); // the newest always stays, even alone over the budget
    QVERIFY(t->cached("tg_a").isEmpty());
    QVERIFY(t->cached("tg_b").isEmpty());
  }
  void whatIsPlayingOrNextIsNeverEvicted() {
    signIn();
    auto t = make();
    t->setCacheDirectory(cache());
    t->setCacheLimits(1, 100 * 1024 * 1024);
    Got got;
    QVERIFY(fetched(*t, song("tg_a", 1), got));
    t->keep({"tg_a", "tg_b"});
    QVERIFY(fetched(*t, song("tg_b", 1), got));
    QVERIFY(fetched(*t, song("tg_c", 1), got));
    QVERIFY(!t->cached("tg_a").isEmpty());
    QVERIFY(!t->cached("tg_b").isEmpty());
    t->clearAudio(); // the settings' Clear cache spares them too
    QVERIFY(t->cached("tg_c").isEmpty());
    QVERIFY(!t->cached("tg_a").isEmpty());
  }
  void aDownloadThatIsSkippedIsCancelledAndLeavesNothing() {
    signIn();
    auto t = make("fetchslow");
    t->setCacheDirectory(cache());
    Got got;
    t->fetch(song("tg_skip", 21), into(got));
    QVERIFY(QTest::qWaitFor([&] { return t->download().value("id") == "tg_skip"; }, 10000));
    t->cancelFetch("tg_skip");
    QVERIFY(t->download().isEmpty());
    QTest::qWait(600);
    QVERIFY(!got.done); // nobody is told about a download they walked away from
    QVERIFY(QDir(cache()).entryList(QDir::Files).isEmpty());
    // The helper is still there for the next song.
    QCOMPARE(t->state(), QString("ready"));
  }
  // --- Playing while it comes down -----------------------------------------
  void aStreamedSongGivesAUrlBeforeItIsWholeAndTheFileAtTheEnd() {
    signIn();
    auto t = make();
    t->setCacheDirectory(cache());
    Got got;
    QString url;
    qint64 size = 0;
    t->fetch(song("tg_stream", 1004), into(got), [&](const QString &u, qint64 n) { url = u; size = n; });
    QVERIFY(QTest::qWaitFor([&] { return !url.isEmpty(); }, 15000));
    QVERIFY2(url.startsWith("http://127.0.0.1:"), qPrintable(url));
    QVERIFY(size > 1000000);
    QVERIFY(!got.done);                        // playable, not yet whole
    QVERIFY(t->fetching("tg_stream"));
    QVERIFY(t->download().isEmpty());          // the player, not a percentage, shows it
    QVERIFY(t->cached("tg_stream").isEmpty());
    const auto first = httpGet(url, "0-11");
    QCOMPARE(first.first, 206);
    QCOMPARE(first.second.left(4), QByteArray("RIFF"));
    QCOMPARE(first.second.size(), 12);
    QVERIFY(QTest::qWaitFor([&] { return got.done; }, 15000));
    QVERIFY2(got.error.isEmpty(), qPrintable(got.error));
    QCOMPARE(QFileInfo(got.data.value("file").toString()).size(), size);
    QVERIFY(!t->cached("tg_stream").isEmpty());
    QVERIFY(QDir(cache()).entryList({"*.part"}).isEmpty());
    // Whole now, the same URL still answers: the player may be partway through it.
    QCOMPARE(httpGet(url, "0-3").second, QByteArray("RIFF"));
  }
  void theHelperStaysForAsLongAsThePlayerMayBeReading() {
    signIn();
    auto t = make();
    t->setCacheDirectory(cache());
    t->setLinger(300);
    Got got;
    QString url;
    t->fetch(song("tg_stream", 1004), into(got), [&](const QString &u, qint64) { url = u; });
    QVERIFY(QTest::qWaitFor([&] { return got.done; }, 15000));
    QTest::qWait(1200);                        // four times the linger
    QCOMPARE(t->state(), QString("ready"));
    QCOMPARE(httpGet(url, "0-3").second, QByteArray("RIFF"));
    t->releaseStream();
    QVERIFY(reach(*t, "closed", 5000));
  }
  void skippingAwayFromAStreamedSongEndsItsUrlAndLeavesNothing() {
    signIn();
    auto t = make();
    t->setCacheDirectory(cache());
    Got got;
    QString url;
    t->fetch(song("tg_stream", 1004), into(got), [&](const QString &u, qint64) { url = u; });
    QVERIFY(QTest::qWaitFor([&] { return !url.isEmpty(); }, 15000));
    QCOMPARE(httpGet(url, "0-3").first, 206);
    t->cancelFetch("tg_stream");
    t->releaseStream();
    QTest::qWait(500);
    QCOMPARE(httpGet(url, "0-3").first, 404);
    QVERIFY(!got.done);
    QVERIFY(QDir(cache()).entryList(QDir::Files).isEmpty());
  }
  void anotherProgramCannotReadItWithoutTheToken() {
    signIn();
    auto t = make();
    t->setCacheDirectory(cache());
    Got got;
    QString url;
    t->fetch(song("tg_stream", 1004), into(got), [&](const QString &u, qint64) { url = u; });
    QVERIFY(QTest::qWaitFor([&] { return !url.isEmpty(); }, 15000));
    const auto name = QUrl(url).path().section('/', -1);
    QCOMPARE(httpGet(url, {}, "/" + name).first, 404);
    QCOMPARE(httpGet(url, {}, "/guess/" + name).first, 404);
    QCOMPARE(httpGet(url, "0-3").first, 206);
    t->cancelFetch("tg_stream");
    t->releaseStream();
  }
  void aSongThatCannotBeStreamedStillComesDownAndNobodyIsToldOfAStream() {
    signIn();
    auto t = make();
    t->setCacheDirectory(cache());
    Got got;
    bool streamed = false;
    t->fetch(song("tg_whole", 1000), into(got), [&](const QString &, qint64) { streamed = true; });
    QVERIFY(QTest::qWaitFor([&] { return got.done; }, 15000));
    QVERIFY2(got.error.isEmpty(), qPrintable(got.error));
    QVERIFY(!streamed);
    QVERIFY(!t->cached("tg_whole").isEmpty());
    // A cached song needs no stream either.
    got = {};
    t->fetch(song("tg_whole", 1000), into(got), [&](const QString &, qint64) { streamed = true; });
    QVERIFY(got.done && got.data.value("cached").toBool());
    QVERIFY(!streamed);
  }
  void askingAgainForAStreamingSongGivesTheSameUrl() {
    signIn();
    auto t = make();
    t->setCacheDirectory(cache());
    Got got;
    QString first, second;
    t->fetch(song("tg_stream", 1004), into(got), [&](const QString &u, qint64) { first = u; });
    QVERIFY(QTest::qWaitFor([&] { return !first.isEmpty(); }, 15000));
    t->fetch(song("tg_stream", 1004), into(got), [&](const QString &u, qint64) { second = u; });   // the player lost it
    QVERIFY(QTest::qWaitFor([&] { return !second.isEmpty(); }, 15000));
    QCOMPARE(second, first);
    QCOMPARE(fetchesLogged(), 1);
    QVERIFY(QTest::qWaitFor([&] { return got.done; }, 15000));
  }
  void askingForASongAlreadyComingDownloadsItOnce() {
    signIn();
    auto t = make();
    t->setCacheDirectory(cache());
    Got first, second;
    t->prefetch(song("tg_next", 31)); // the preload a minute before the end
    t->fetch(song("tg_next", 31), into(second)); // then the track change that wants it
    QVERIFY(QTest::qWaitFor([&] { return second.done; }, 15000));
    QVERIFY(second.error.isEmpty());
    QCOMPARE(fetchesLogged(), 1);
    QVERIFY(!first.done);
  }
  void aMissingSongAndATooBigOneSayWhy() {
    signIn();
    auto t = make();
    t->setCacheDirectory(cache());
    Got got;
    QVERIFY(fetched(*t, song("tg_gone", 9999), got));
    QCOMPARE(got.error, QString("This song is no longer in the chat."));
    QVERIFY(fetched(*t, song("tg_big", 8888), got));
    QVERIFY(got.error.contains("200 MB"));
    QVERIFY(QDir(cache()).entryList(QDir::Files).isEmpty());
  }
  void aSongThatCanNotBeFoundAgainSaysSo() {
    signIn();
    auto t = make();
    t->setCacheDirectory(cache());
    Got got;
    auto bad = song("tg_bad", 1);
    bad["remoteId"] = "nonsense";
    QVERIFY(fetched(*t, bad, got));
    QVERIFY(got.error.contains("Refresh"));
  }
  void withoutASessionPlayingSaysToSignInAndLetsTheHelperGo() {
    auto t = make(); // no account.json: the helper starts signed out
    QDir().mkpath(storage.path() + "/telegram");
    t->setCacheDirectory(cache());
    Got got;
    QVERIFY(fetched(*t, song("tg_nosession", 1), got));
    QVERIFY(got.error.contains("Sign in"));
    QVERIFY(reach(*t, "closed"));
  }
  void aHelperThatDiesDuringADownloadFailsItInsteadOfHanging() {
    signIn();
    auto t = make("fetchslow");
    t->setCacheDirectory(cache());
    Got got;
    t->fetch(song("tg_die", 41), into(got));
    QVERIFY(QTest::qWaitFor([&] { return t->download().value("id") == "tg_die"; }, 10000));
    QProcess::execute("pkill", {"-f", "telegram_fixture.py"});
    QVERIFY(QTest::qWaitFor([&] { return got.done; }, 10000));
    QVERIFY(!got.error.isEmpty());
  }
  void theHelperStaysUpForAWhileAfterASongSoTheNextOneDoesNotWaitForIt() {
    signIn();
    auto t = make();
    t->setCacheDirectory(cache());
    Got got;
    QVERIFY(fetched(*t, song("tg_linger", 1), got));
    QTest::qWait(300);
    QCOMPARE(t->state(), QString("ready"));
  }
  void halfFilesFromAnInterruptedRunAreRemoved() {
    QDir().mkpath(cache());
    QFile half(cache() + "/tg_half.part");
    QVERIFY(half.open(QIODevice::WriteOnly));
    half.write("xx");
    half.close();
    auto t = make();
    t->setCacheDirectory(cache());
    QVERIFY(!QFile::exists(cache() + "/tg_half.part"));
    QVERIFY(t->cached("tg_half").isEmpty());
  }
  void signingOutLeavesNoSongsOrTagsBehind() {
    signIn();
    auto t = make();
    t->setCacheDirectory(cache());
    Got got;
    QVERIFY(fetched(*t, song("tg_gone_on_logout", 1), got));
    QVERIFY(QFile::exists(storage.path() + "/telegram/tags.json"));
    t->logout();
    QVERIFY(QDir(cache()).entryList(QDir::Files).isEmpty());
    QVERIFY(!QFile::exists(storage.path() + "/telegram/tags.json"));
    QVERIFY(reach(*t, "wait-phone")); // the helper has handled it, so it can't reach into the next test's folder
  }
  // --- Cover art -----------------------------------------------------------
  static QVariantList rowsOf(const QSignalSpy &spy, int which = 0) { return spy.at(which).at(2).toList(); }
  static int artsIn(const QSignalSpy &learned) {
    int n = 0;
    for (const auto &entry : learned) n += entry.at(1).toMap().contains("art");
    return n;
  }
  void aSourceBringsItsChatsPhotoAndNothingMoreUntilSongsAreOnScreen() {
    signIn();
    auto t = make();
    t->setArtDirectory(art());
    QSignalSpy cover(t.get(), &Telegram::coverReady), learned(t.get(), &Telegram::songLearned);
    t->addSource("4242", "Saved Messages", "saved", "");
    QVERIFY(settled(*t, "4242"));
    QVERIFY(artIdle(*t));
    // The chat's cover, once.
    QCOMPARE(cover.count(), 1);
    QCOMPARE(cover.first().at(0).toString(), QString("4242"));
    const QUrl url(cover.first().at(1).toString());
    QVERIFY(url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile());
    QCOMPARE(t->coverUrl("4242"), url.toString());
    // A sync does not fetch song pictures: that waits for the list to show them.
    QCOMPARE(artRequests(), 1);
    QCOMPARE(artsIn(learned), 0);
  }
  void theSongsOnScreenGetTheirThumbnails() {
    signIn();
    auto t = make();
    t->setArtDirectory(art());
    QSignalSpy ready(t.get(), &Telegram::songsReady), learned(t.get(), &Telegram::songLearned);
    t->addSource("4242", "Saved Messages", "saved", "");
    QVERIFY(settled(*t, "4242"));
    QVERIFY(artIdle(*t));
    t->wantArt(rowsOf(ready));
    QVERIFY(artIdle(*t));
    // Two of the three songs have one.
    QCOMPARE(artsIn(learned), 2);
    QCOMPARE(files(artFolder("c4242")).size(), 3);
    // Only the owner can read any of it.
    QVERIFY(ownerOnly(art()));
    QVERIFY(ownerOnly(artFolder("c4242")));
    for (const auto &name : files(artFolder("c4242"))) QVERIFY2(ownerOnly(artFolder("c4242") + "/" + name), qPrintable(name));
  }
  void rowsThatScrolledAwayBeforeTheirTurnAreDropped() {
    signIn();
    auto t = make();
    t->setArtDirectory(art());
    QSignalSpy ready(t.get(), &Telegram::songsReady), learned(t.get(), &Telegram::songLearned);
    t->addSource("4242", "Saved Messages", "saved", "");
    QVERIFY(settled(*t, "4242"));
    t->wantArt(rowsOf(ready));
    t->wantArt({}); // scrolled somewhere with no Telegram songs
    QVERIFY(artIdle(*t));
    QVERIFY(artsIn(learned) <= 1); // at most the one already on its way
    QVERIFY(artRequests() <= 2);
  }
  void aSongWithNoPictureIsNotAskedAboutAgainEvenAfterARestart() {
    signIn();
    QVariantList rows;
    int asked = 0;
    {
      auto t = make();
      t->setArtDirectory(art());
      QSignalSpy ready(t.get(), &Telegram::songsReady);
      t->addSource("4242", "Saved Messages", "saved", "");
      QVERIFY(settled(*t, "4242"));
      rows = rowsOf(ready);
      t->wantArt(rows);
      QVERIFY(artIdle(*t));
      asked = artRequests();
      QCOMPARE(asked, 4); // the cover and three songs
      t->wantArt(rows);   // the list scrolls past again
      QVERIFY(artIdle(*t));
      QVERIFY(ownerOnly(storage.path() + "/telegram/noart.json"));
    }
    auto again = make();
    again->setArtDirectory(art());
    QSignalSpy learned(again.get(), &Telegram::songLearned);
    again->wantArt(rows);
    QVERIFY(artIdle(*again));
    QCOMPARE(artRequests(), asked);   // the one with none is remembered, the others are on disk
    QCOMPARE(artsIn(learned), 2);     // and the rows are told about those
    again->clearArt();
    again->wantArt(rows);
    QVERIFY(artIdle(*again));
    QVERIFY(artRequests() > asked);   // clearing the pictures forgets that too
  }
  void aPictureThatWasEvictedIsAskedForAgain() {
    signIn();
    auto t = make();
    t->setArtDirectory(art());
    QSignalSpy ready(t.get(), &Telegram::songsReady), learned(t.get(), &Telegram::songLearned);
    t->addSource("4242", "Saved Messages", "saved", "");
    QVERIFY(settled(*t, "4242"));
    auto row = rowsOf(ready).first().toMap();
    row["art"] = "file:///nowhere/at/all.png";
    t->wantArt({row});
    QVERIFY(artIdle(*t));
    QCOMPARE(artsIn(learned), 1);
  }
  void aChatWithoutAPhotoHasNoCover() {
    signIn();
    auto t = make();
    t->setArtDirectory(art());
    QSignalSpy cover(t.get(), &Telegram::coverReady);
    t->addSource("-1002", "Friends", "group", "");
    QVERIFY(settled(*t, "-1002"));
    QVERIFY(artIdle(*t));
    QCOMPARE(cover.count(), 0);
    QVERIFY(t->coverUrl("-1002").isEmpty());
  }
  void aRefreshKeepsWhatItAlreadyHasAndGivesItToTheRows() {
    signIn();
    auto t = make();
    t->setArtDirectory(art());
    QSignalSpy ready(t.get(), &Telegram::songsReady);
    t->addSource("4242", "Saved Messages", "saved", "");
    QVERIFY(settled(*t, "4242"));
    t->wantArt(rowsOf(ready));
    QVERIFY(artIdle(*t));
    const int asked = artRequests();
    QVERIFY(asked >= 3);
    t->refresh("4242");
    QVERIFY(QTest::qWaitFor([&] { return ready.count() == 2; }, 15000));
    QVERIFY(artIdle(*t));
    const auto rows = rowsOf(ready, 1);
    QVERIFY(rows.first().toMap().value("art").toString().contains("c4242"));   // a picture already on disk is on the row
    QVERIFY(rows.at(1).toMap().value("art").toString().isEmpty());             // and the song with none is still without
    t->wantArt(rows);
    QVERIFY(artIdle(*t));
    QCOMPARE(artRequests(), asked);                                           // nothing was asked for twice
  }
  void theArtIsBoundedOldestFirstButTheCoverStays() {
    signIn();
    auto t = make();
    t->setArtDirectory(art());
    t->setArtLimits(1, 1024 * 1024);
    QSignalSpy ready(t.get(), &Telegram::songsReady);
    t->addSource("4242", "Saved Messages", "saved", "");
    QVERIFY(settled(*t, "4242"));
    t->wantArt(rowsOf(ready));
    QVERIFY(artIdle(*t));
    const auto kept = files(artFolder("c4242"));
    QCOMPARE(kept.size(), 2); // one picture and the cover
    QVERIFY(kept.contains("cover.png"));
  }
  void removingTheLastSourceOfAChatRemovesItsPictures() {
    signIn();
    auto t = make();
    t->setArtDirectory(art());
    t->addSource("4242", "Saved Messages", "saved", "");
    QVERIFY(settled(*t, "4242"));
    QVERIFY(artIdle(*t));
    QVERIFY(QDir(artFolder("c4242")).exists());
    t->removeSource("4242");
    QVERIFY(!QDir(artFolder("c4242")).exists());
  }
  void aForumsTopicsShareItsPicturesUntilTheLastIsRemoved() {
    signIn();
    auto t = make();
    t->setArtDirectory(art());
    t->addSource("-1003", "Forum", "forum", "", "11", "Jazz");
    t->addSource("-1003", "Forum", "forum", "", "12", "Rock");
    QVERIFY(settled(*t, "-1003/11") && settled(*t, "-1003/12"));
    QVERIFY(artIdle(*t));
    QVERIFY(QDir(artFolder("cn1003")).exists());
    t->removeSource("-1003/11");
    QVERIFY(QDir(artFolder("cn1003")).exists());
    t->removeSource("-1003/12");
    QVERIFY(!QDir(artFolder("cn1003")).exists());
  }
  void clearingTheArtDeletesItAndSaysSo() {
    signIn();
    auto t = make();
    t->setArtDirectory(art());
    QSignalSpy cleared(t.get(), &Telegram::artCleared);
    t->addSource("4242", "Saved Messages", "saved", "");
    QVERIFY(settled(*t, "4242"));
    QVERIFY(artIdle(*t));
    t->clearArt();
    QCOMPARE(cleared.count(), 1);
    QVERIFY(QDir(art()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
    QVERIFY(t->coverUrl("4242").isEmpty());
  }
  void signingOutDeletesTheArtToo() {
    signIn();
    auto t = make();
    t->setArtDirectory(art());
    QSignalSpy cleared(t.get(), &Telegram::artCleared);
    t->open(); // the sign-in dialog, which is where Log out is
    t->addSource("4242", "Saved Messages", "saved", "");
    QVERIFY(settled(*t, "4242"));
    QVERIFY(artIdle(*t));
    t->logout();
    QVERIFY(reach(*t, "wait-phone"));
    QVERIFY(cleared.count() >= 1);
    QVERIFY(QDir(art()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
  }
  void thePictureInsideADownloadedFileBecomesTheSongsArt() {
    signIn();
    auto t = make();
    t->setArtDirectory(art());
    t->setCacheDirectory(cache());
    QSignalSpy learned(t.get(), &Telegram::songLearned);
    auto inside = song("tg_inside", 1003);
    inside["remoteId"] = "-100777:1003";
    inside["telegramChat"] = "-100777";
    inside["telegramMessage"] = "1003";
    inside["thumb"] = true;
    Got got;
    QVERIFY(fetched(*t, inside, got));
    QVERIFY2(got.error.isEmpty(), qPrintable(got.error));
    QVERIFY(artIdle(*t));
    QVERIFY(QFile::exists(artFolder("cn100777") + "/tg_inside.png"));
    int arts = 0;
    for (const auto &entry : learned) arts += entry.at(1).toMap().value("art").toString().contains("tg_inside.png");
    QCOMPARE(arts, 1);
    QVERIFY(ownerOnly(artFolder("cn100777") + "/tg_inside.png"));
  }
  void withNoPictureInsideTheMessagesThumbnailIsAskedFor() {
    signIn();
    auto t = make();
    t->setArtDirectory(art());
    t->setCacheDirectory(cache());
    QSignalSpy learned(t.get(), &Telegram::songLearned);
    t->addSource("4242", "Saved Messages", "saved", ""); // a picture is only kept for a chat that is still a source
    QVERIFY(settled(*t, "4242"));
    QVERIFY(artIdle(*t));
    learned.clear();
    auto plain = song("tg_thumb", 1002);
    plain["remoteId"] = "4242:1002";
    plain["telegramChat"] = "4242";
    plain["telegramMessage"] = "1002";
    plain["thumb"] = true;
    Got got;
    QVERIFY(fetched(*t, plain, got));
    QVERIFY(artIdle(*t));
    QVERIFY(QFile::exists(artFolder("c4242") + "/tg_thumb.png"));
    bool art = false;
    for (const auto &entry : learned) art = art || entry.at(1).toMap().contains("art");
    QVERIFY(art);
  }
  void aSongWhosePictureIsAlreadyThereIsNotAskedForAgain() {
    signIn();
    auto t = make();
    t->setArtDirectory(art());
    t->setCacheDirectory(cache());
    QDir().mkpath(artFolder("c4242"));
    QFile have(artFolder("c4242") + "/tg_have.jpg");
    QVERIFY(have.open(QIODevice::WriteOnly));
    have.write("x");
    have.close();
    auto owned = song("tg_have", 1002);
    owned["remoteId"] = "4242:1002";
    owned["telegramChat"] = "4242";
    owned["telegramMessage"] = "1002";
    owned["thumb"] = true;
    Got got;
    QVERIFY(fetched(*t, owned, got));
    QTest::qWait(300);
    QCOMPARE(artRequests(), 0);
  }
  void halfPicturesFromAnInterruptedRunAreRemoved() {
    QDir().mkpath(artFolder("c4242"));
    QFile half(artFolder("c4242") + "/tg_x.png.part");
    QVERIFY(half.open(QIODevice::WriteOnly));
    half.close();
    auto t = make();
    t->setArtDirectory(art());
    QVERIFY(!QFile::exists(half.fileName()));
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
