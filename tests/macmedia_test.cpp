#include "backend.h"
#include "macmedia.h"
#include <QGuiApplication>
#include <QImage>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

// What Control Center is told and what the media keys can do, read back from
// the system in this process. The keys themselves are not sent: each handler
// only calls MacMedia::perform(), which is what is driven here.
class MacMediaTest : public QObject {
  Q_OBJECT
  QTemporaryDir files;
  QVariantMap song(const QString &id, const QString &title, const QString &art = {}) {
    const auto path = files.filePath(id + ".wav");
    if (!QFileInfo::exists(path)) {
      QProcess encode;
      encode.start("ffmpeg", {"-nostdin", "-v", "error", "-f", "lavfi", "-i", "sine=frequency=330:sample_rate=22050", "-t", "90", path});
      if (!encode.waitForFinished(10000) || encode.exitCode() != 0) return {};
    }
    QVariantMap t{{"id", id}, {"title", title}, {"artist", "Artist " + id}, {"album", "Album"}, {"kind", "song"}, {"localPath", path}, {"seconds", 90}};
    if (!art.isEmpty()) t["art"] = QUrl::fromLocalFile(art).toString();
    return t;
  }
private slots:
  void initTestCase() {
    // A local song is prepared through the helper; the fixture answers for it.
    qputenv("SUNG_HELPER", qgetenv("SUNG_FIXTURE_HELPER")); qputenv("SUNG_PYTHON", "/usr/bin/python3");
    QCoreApplication::setApplicationName("sung-test");
    QCoreApplication::setOrganizationName("SungTests");
    // The profile of a test organisation is wiped; nothing else is touched.
    QSettings().clear(); QSettings().sync();
    for (const auto location : {QStandardPaths::AppDataLocation, QStandardPaths::CacheLocation}) {
      const auto path = QStandardPaths::writableLocation(location);
      if (path.contains("SungTests")) QDir(path).removeRecursively();
    }
  }
  void nothingPlayingTellsTheSystemNothing() {
    Backend b; b.setPauseOnDisconnect(false); b.setWatchMusicFolders(false); b.clearQueue();
    MacMedia media(&b);
    QVERIFY(media.nowPlaying().isEmpty());
    QCOMPARE(media.playbackState(), QString("stopped"));
    for (const auto *command : {"play", "pause", "toggle", "next", "previous", "seek"}) QVERIFY(!media.allowed(command));
    QVERIFY(!media.perform("toggle"));
    for (const auto *command : {"play", "pause", "toggle", "next", "previous", "seek"}) QVERIFY2(media.commandEnabled(command), command);
    QVERIFY(!media.commandEnabled("stop"));
  }
  void aSongIsDescribedAndFollowsPlayPauseAndSeek() {
    const auto cover = files.filePath("cover.png");
    QImage image(64, 64, QImage::Format_RGB32); image.fill(Qt::darkMagenta); QVERIFY(image.save(cover));
    const auto first = song("local_one", "First song", cover);
    QVERIFY(!first.isEmpty());
    Backend b; b.setPauseOnDisconnect(false); b.setWatchMusicFolders(false); b.clearQueue(); b.setVolume(0); b.setAutoplay(false); b.setPrepareNext(false);
    MacMedia media(&b);
    b.playItem(first);
    QTRY_VERIFY_WITH_TIMEOUT(b.playing() && b.duration() > 0, 8000);
    QTRY_COMPARE_WITH_TIMEOUT(media.nowPlaying().value("title").toString(), QString("First song"), 3000);
    auto info = media.nowPlaying();
    QCOMPARE(info.value("artist").toString(), QString("Artist local_one"));
    QCOMPARE(info.value("album").toString(), QString("Album"));
    QVERIFY(qAbs(info.value("duration").toDouble() - b.duration() / 1000.0) < 0.01);
    QVERIFY(info.value("artwork").toBool());
    QCOMPARE(info.value("rate").toDouble(), 1.0);
    QCOMPARE(media.playbackState(), QString("playing"));

    QVERIFY(media.perform("pause"));
    QTRY_VERIFY_WITH_TIMEOUT(!b.playing(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(media.nowPlaying().value("rate").toDouble(), 0.0, 3000);
    QCOMPARE(media.playbackState(), QString("paused"));
    // Paused, the elapsed time the system shows is where playback stopped.
    QVERIFY(qAbs(media.nowPlaying().value("elapsed").toDouble() - b.position() / 1000.0) < 1.0);

    QVERIFY(media.perform("seek", 40));
    QTRY_VERIFY_WITH_TIMEOUT(b.position() >= 39000 && b.position() < 42000, 4000);
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(media.nowPlaying().value("elapsed").toDouble() - 40.0) < 2.0, 3000);
    QVERIFY(media.perform("seek", 5000)); // past the end is kept inside the song
    QVERIFY(b.position() <= b.duration() + 1000);
    b.seek(1000);

    QVERIFY(media.perform("play"));
    QTRY_VERIFY_WITH_TIMEOUT(b.playing(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(media.nowPlaying().value("rate").toDouble(), 1.0, 3000);
    QVERIFY(media.perform("toggle"));
    QTRY_VERIFY_WITH_TIMEOUT(!b.playing(), 3000);
    QVERIFY(media.perform("toggle"));
    QTRY_VERIFY_WITH_TIMEOUT(b.playing(), 3000);

    b.setPlaybackRate(1.5);
    QTRY_COMPARE_WITH_TIMEOUT(media.nowPlaying().value("rate").toDouble(), 1.5, 3000);
    b.stop();
    QTRY_COMPARE_WITH_TIMEOUT(media.playbackState(), QString("stopped"), 3000);
  }
  void nextAndPreviousMoveThroughTheQueueAndKeepPause() {
    const auto first = song("local_one", "First song"), second = song("local_two", "Second song");
    QVERIFY(!first.isEmpty() && !second.isEmpty());
    Backend b; b.setPauseOnDisconnect(false); b.setWatchMusicFolders(false); b.clearQueue(); b.setVolume(0); b.setAutoplay(false); b.setPrepareNext(false);
    MacMedia media(&b);
    b.playItem(first); b.enqueue(second);
    QTRY_VERIFY_WITH_TIMEOUT(b.playing(), 8000);
    QVERIFY(media.allowed("next"));
    QVERIFY(!media.allowed("previous")); // first song, not yet three seconds in
    b.pause();
    QVERIFY(media.perform("next"));
    QTRY_COMPARE_WITH_TIMEOUT(media.nowPlaying().value("title").toString(), QString("Second song"), 5000);
    QVERIFY2(!b.playing(), "a skip while paused stays paused");
    QVERIFY(!media.allowed("next"));
    QVERIFY(media.allowed("previous"));
    QVERIFY(media.perform("previous"));
    QTRY_COMPARE_WITH_TIMEOUT(media.nowPlaying().value("title").toString(), QString("First song"), 5000);
  }
  void aSongWithoutArtworkOrArtistIsStillDescribed() {
    auto bare = song("local_bare", "Bare song");
    QVERIFY(!bare.isEmpty());
    bare.remove("artist"); bare.remove("album");
    Backend b; b.setPauseOnDisconnect(false); b.setWatchMusicFolders(false); b.clearQueue(); b.setVolume(0); b.setAutoplay(false); b.setPrepareNext(false);
    MacMedia media(&b);
    b.playItem(bare);
    QTRY_COMPARE_WITH_TIMEOUT(media.nowPlaying().value("title").toString(), QString("Bare song"), 8000);
    QVERIFY(!media.nowPlaying().value("artwork").toBool());
    QVERIFY(media.nowPlaying().value("artist").toString().isEmpty());
    b.stop();
  }
};
QTEST_MAIN(MacMediaTest)
#include "macmedia_test.moc"
