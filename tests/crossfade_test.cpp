// Crossfade and gapless handover, measured on real audio.
//
// The claims worth proving are that the next song is already playing before the
// last one stops, that the two volumes trade places on an equal-power curve
// rather than dipping through the middle, that the queue and everything hanging
// off it move with the audio, and that steering by hand takes the head start
// back cleanly.
#include "backend.h"
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

class CrossfadeTest : public QObject {
  Q_OBJECT
  QTemporaryDir storage;
  QTemporaryDir music;
  QStringList files;

  // Short recordings, so a transition arrives inside a test's patience. A tone
  // rather than silence where the meters have to see something.
  bool encode(const QString &name, const QString &title, int seconds, int hertz = 0) {
    const QString source = hertz > 0
                               ? QString("sine=frequency=%1:sample_rate=44100:duration=%2").arg(hertz).arg(seconds)
                               : "anullsrc=r=44100:cl=mono";
    QStringList arguments{"-nostdin", "-v", "error", "-f", "lavfi", "-i", source};
    if (hertz <= 0)
      arguments << "-t" << QString::number(seconds);
    arguments << "-metadata" << ("title=" + title) << "-metadata" << "artist=Fixture artist"
              << "-metadata" << "album=Handover" << music.filePath(name);
    QProcess run;
    run.start("ffmpeg", arguments);
    return run.waitForFinished(20000) && run.exitCode() == 0;
  }

  // A six-second song as YouTube serves audio: in fragments, with an index of
  // them, and with the whole length also declared in the header (ffmpeg leaves
  // that at zero, which the Mac's player copes with; YouTube's file does not,
  // and the player counts the length twice). Returns the file's path, or
  // nothing if ffmpeg could not make it.
  QString fragmentedSong(const QString &name, const QString &title) {
    const auto path = music.filePath(name);
    QProcess run;
    run.start("ffmpeg", {"-nostdin", "-v", "error", "-f", "lavfi", "-i", "sine=frequency=330:sample_rate=44100", "-t", "6",
                         "-c:a", "aac", "-b:a", "96k", "-metadata", "title=" + title, "-metadata", "artist=Fixture artist",
                         "-metadata", "album=Whole", "-movflags", "frag_keyframe+empty_moov+default_base_moof+global_sidx",
                         "-f", "mp4", path});
    if (!run.waitForFinished(20000) || run.exitCode() != 0)
      return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite))
      return {};
    auto bytes = file.readAll();
    // The three version 0 headers that carry a duration, at 44.1 kHz.
    const struct { const char *kind; int offset; } fields[] = {{"mvhd", 24}, {"tkhd", 28}, {"mdhd", 24}};
    for (const auto &field : fields) {
      const auto at = bytes.indexOf(field.kind) - 4;
      if (at < 0 || bytes.at(at + 8) != 0)
        return {};
      const quint32 length = qToBigEndian<quint32>(6 * 44100);
      memcpy(bytes.data() + at + field.offset, &length, 4);
    }
    file.seek(0);
    file.write(bytes);
    return path;
  }
  QString madeWhole(const QString &fragmented, const QString &name) {
    const auto path = music.filePath(name);
    QProcess run;
    run.start("ffmpeg", {"-nostdin", "-v", "error", "-i", fragmented, "-c", "copy", "-map", "0", "-dn", "-f", "mp4", path});
    return run.waitForFinished(20000) && run.exitCode() == 0 ? path : QString();
  }
  std::unique_ptr<Backend> playing(const QStringList &songs, bool gapless) {
    auto b = std::make_unique<Backend>();
    b->setVolume(0.6);
    b->setLyricsFallback(false);
    b->setAutoplay(false);
    b->setWatchMusicFolders(false);
    b->setOnlineArtwork(false);
    b->setPrepareNext(false);
    b->setCrossfadeSeconds(0);
    b->setGapless(gapless);
    b->clearQueue();
    QVariantList urls;
    for (const auto &song : songs)
      urls.append(QUrl::fromLocalFile(song));
    b->importLocalFiles(urls);
    if (!QTest::qWaitFor([&b] { return !b->importingLocal(); }, 20000))
      return {};
    b->library("files");
    // The library outlives a test, so the songs are picked out by where they are.
    QVariantList rows;
    for (const auto &song : songs)
      for (const auto &row : b->results()->rows)
        if (row.toMap().value("localPath").toString() == QFileInfo(song).canonicalFilePath())
          rows.append(row);
    if (rows.size() != songs.size())
      return {};
    b->enqueueItems(rows);
    b->playAt(0);
    return b;
  }

  // A backend with a three-song local queue, playing the first.
  std::unique_ptr<Backend> loaded(Backend **out = nullptr) {
    auto b = std::make_unique<Backend>();
    b->setVolume(0.8);
    b->setLyricsFallback(false);
    b->setAutoplay(false);
    b->setWatchMusicFolders(false);
    b->setOnlineArtwork(false);
    b->setPrepareNext(false);
    b->clearQueue();
    QVariantList urls;
    for (const auto &file : files)
      urls.append(QUrl::fromLocalFile(file));
    b->importLocalFiles(urls);
    if (out)
      *out = b.get();
    return b;
  }

private slots:
  void initTestCase() {
    qputenv("XDG_DATA_HOME", storage.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", storage.path().toUtf8());
    qputenv("XDG_CACHE_HOME", storage.path().toUtf8());
    QCoreApplication::setApplicationName("sung-crossfade-test");
    QCoreApplication::setOrganizationName("SungTests");
#ifdef Q_OS_MACOS
    // macOS ignores the XDG variables above, so the profile would otherwise
    // live on between runs and each run would start with the last one's
    // library. Only a location named for the test organisation is wiped.
    QSettings().clear();QSettings().sync();
    for(const auto location:{QStandardPaths::AppDataLocation,QStandardPaths::CacheLocation}){
      const auto path=QStandardPaths::writableLocation(location);if(path.contains("SungTests"))QDir(path).removeRecursively();
    }
#endif
    const auto helper = QFileInfo(QString::fromUtf8(qgetenv("SUNG_FIXTURE_HELPER")))
                            .dir().absoluteFilePath("../helper/catalog.py");
    qputenv("SUNG_HELPER", helper.toUtf8());
    qputenv("SUNG_PYTHON", "/usr/bin/python3");
    QVERIFY(encode("tone one.flac", "Tone one", 5, 330));
    // Three seconds of silence, then three of tone: where the meters light up
    // says which part of the file they are reading.
    {
      QProcess run;
      run.start("ffmpeg", {"-nostdin", "-v", "error", "-f", "lavfi", "-i", "anullsrc=r=44100:cl=mono:d=3", "-f", "lavfi", "-i",
                           "sine=frequency=330:sample_rate=44100:duration=3", "-filter_complex", "[0:a][1:a]concat=n=2:v=0:a=1",
                           "-metadata", "title=Late tone", "-metadata", "artist=Fixture artist", music.filePath("late tone.flac")});
      QVERIFY(run.waitForFinished(20000) && run.exitCode() == 0);
    }
    QVERIFY(encode("tone two.flac", "Tone two", 6, 220));
    QVERIFY(encode("01 first.flac", "First", 4));
    QVERIFY(encode("02 second.flac", "Second", 6));
    QVERIFY(encode("03 third.flac", "Third", 6));
    for (const auto &name : {"01 first.flac", "02 second.flac", "03 third.flac"})
      files << music.filePath(name);
  }

  // Standard streaming quality takes Opus in a WebM container, where this used
  // to take AAC in MP4. Decoding it is one thing; reporting a length and
  // seeking inside it are what the rest of the player needs, and WebM carries
  // its timing differently from MP4. A buffered YouTube song is played from
  // the file the helper wrote, which is what this drives. On macOS the helper
  // is asked for M4A instead, since AVFoundation cannot open WebM, so that is
  // what is played there.
  void opusInWebmPlaysAndSeeks() {
    const auto container = Backend::playableContainers().value(0, "webm");
    const auto stream = "stream one." + container;
    QVERIFY(encode(stream, "Stream one", 8, 440));
    Backend b;
    b.setVolume(0.8);b.setLyricsFallback(false);b.setAutoplay(false);b.setWatchMusicFolders(false);
    b.setOnlineArtwork(false);b.setPrepareNext(false);b.clearQueue();
    b.localTestSource(QUrl::fromLocalFile(music.filePath(stream)));
    QTRY_VERIFY_WITH_TIMEOUT(b.playing() && b.position() > 800, 15000);
    QVERIFY2(b.duration() > 6000, qPrintable(QString("duration %1").arg(b.duration())));
    QVERIFY(b.error().isEmpty());
    // The decoder reports what it is actually playing, which Track details shows.
    QString codec, rate;
    for (const auto &entry : b.trackDetails(b.current())) {
      const auto row = entry.toMap();
      if (row.value("label") == "Playback codec") codec = row.value("value").toString();
      if (row.value("label") == "Decoded sample rate") rate = row.value("value").toString();
    }
    // Both rows come from Qt's FFmpeg backend: the darwin one reports no codec
    // and never feeds the buffer tap the decoded rate is read from.
#ifndef Q_OS_MACOS
    QVERIFY2(codec.contains("opus", Qt::CaseInsensitive), qPrintable("codec: " + codec));
    QVERIFY2(!rate.isEmpty(), "the decoder reports a sample rate");
#endif
    b.seek(6000);
    QTRY_VERIFY_WITH_TIMEOUT(b.position() > 6200 && b.playing(), 10000);
    b.pause();
    const auto held = b.position();
    QTest::qWait(400);
    QVERIFY(qAbs(b.position() - held) < 250);
    // Resumed on the deck itself: this recording is not in a queue, because
    // it is played as the buffered stream, not imported.
    b.media()->play();
    QTRY_VERIFY_WITH_TIMEOUT(b.position() > held + 500, 10000);
    QVERIFY(b.error().isEmpty());
    b.stop();
  }

  // Nothing changes for anyone who has not asked for it.
  void overlapIsOffUntilAskedFor() {
    Backend b;
    QCOMPARE(b.crossfadeSeconds(), 0);
    QVERIFY(b.gapless());
    b.setCrossfadeSeconds(5);
    QCOMPARE(b.crossfadeSeconds(), 5);
    b.setCrossfadeSeconds(99);
    QCOMPARE(b.crossfadeSeconds(), 12);
    b.setCrossfadeSeconds(-3);
    QCOMPARE(b.crossfadeSeconds(), 0);
    {
      Backend restored;
      QCOMPARE(restored.crossfadeSeconds(), 0);
    }
  }

  // With no overlap configured, the next song still starts without the pause
  // the media pipeline would otherwise spend loading it.
  void gaplessKeepsPlayingAcrossTheJoin() {
    auto b = loaded();
    QTRY_VERIFY_WITH_TIMEOUT(!b->importingLocal(), 20000);
    b->library("files");
    QCOMPARE(b->results()->count(), 3);
    b->setCrossfadeSeconds(0);
    b->setGapless(true);
    b->enqueueItems(b->results()->rows);
    b->playAt(0);
    QTRY_VERIFY_WITH_TIMEOUT(b->playing() && b->duration() > 0, 10000);
    QCOMPARE(b->currentIndex(), 0);

    // Watch every 10ms from before the join until after it.
    int silentSamples = 0, samples = 0;
    QElapsedTimer watch;
    watch.start();
    while (b->currentIndex() == 0 && watch.elapsed() < 20000) {
      QTest::qWait(10);
      if (b->position() > 500 || b->currentIndex() > 0) {
        ++samples;
        if (!b->playing())
          ++silentSamples;
      }
    }
    QCOMPARE(b->currentIndex(), 1);
    QVERIFY2(samples > 20, "the join was actually observed");
    QVERIFY2(silentSamples <= 1,
             qPrintable(QString("playback stopped for %1 of %2 samples across the join")
                            .arg(silentSamples).arg(samples)));
    // The rest of the application moved with the audio.
    QCOMPARE(b->current().value("title").toString(), QString("Second"));
    QVERIFY(b->media()->source().toLocalFile().contains("02 second"));
    QTRY_VERIFY(b->duration() > 0);
    b->stop();
  }

  // Turning the handover off restores the ordinary transition, which does stop.
  void withoutGaplessTheJoinIsAnOrdinaryTransition() {
    auto b = loaded();
    QTRY_VERIFY_WITH_TIMEOUT(!b->importingLocal(), 20000);
    b->library("files");
    b->setCrossfadeSeconds(0);
    b->setGapless(false);
    b->enqueueItems(b->results()->rows);
    b->playAt(0);
    QTRY_VERIFY_WITH_TIMEOUT(b->playing() && b->duration() > 0, 10000);
    QElapsedTimer watch;
    watch.start();
    while (b->currentIndex() == 0 && watch.elapsed() < 20000)
      QTest::qWait(10);
    QCOMPARE(b->currentIndex(), 1);
    QVERIFY(b->m_handoffIndex < 0);
    b->stop();
  }

  // With an overlap, both decks sound at once and their gains trade places on
  // an equal-power curve.
  void overlapPlaysBothAndKeepsItsLoudness() {
    auto b = loaded();
    QTRY_VERIFY_WITH_TIMEOUT(!b->importingLocal(), 20000);
    b->library("files");
    b->setGapless(true);
    b->setCrossfadeSeconds(2);
    b->setVolume(0.8);
    b->enqueueItems(b->results()->rows);
    b->playAt(0);
    QTRY_VERIFY_WITH_TIMEOUT(b->playing() && b->duration() > 0, 10000);
    const double settled = b->effectiveVolume();
    QVERIFY(settled > 0.5);

    QTRY_VERIFY_WITH_TIMEOUT(b->crossfading(), 20000);
    // Sample the overlap as it runs.
    int both = 0, samples = 0;
    double worstPower = 1.0, lowestOutgoing = 1.0, highestIncoming = 0.0;
    while (b->crossfading() && samples < 400) {
      const double outgoing = b->activeAudio().volume();
      const double incoming = b->spareAudio().volume();
      if (b->m_media().playbackState() == QMediaPlayer::PlayingState &&
          b->spareDeck().playbackState() == QMediaPlayer::PlayingState)
        ++both;
      lowestOutgoing = std::min(lowestOutgoing, outgoing);
      highestIncoming = std::max(highestIncoming, incoming);
      // Equal power: the two gains squared should add up to the settled one
      // squared throughout, never dipping the way two linear ramps would.
      const double power = std::sqrt(outgoing * outgoing + incoming * incoming);
      if (incoming > 0.02 && outgoing > 0.02)
        worstPower = std::min(worstPower, power / settled);
      ++samples;
      QTest::qWait(10);
    }
    QVERIFY2(both > 5, qPrintable(QString("both decks played together for %1 samples").arg(both)));
    QVERIFY2(lowestOutgoing < settled * 0.2, "the outgoing song faded out");
    QVERIFY2(highestIncoming > settled * 0.8, "the incoming song faded in");
    QVERIFY2(worstPower > 0.9,
             qPrintable(QString("combined power dipped to %1 of the settled level")
                            .arg(worstPower, 0, 'f', 3)));

    // These fixtures are short enough that the next overlap follows straight
    // on, so the handover is measured the moment it happens rather than later.
    // These fixtures are short enough that the next overlap follows straight
    // on, so the handover is measured the moment it happens rather than later.
    QTRY_COMPARE_WITH_TIMEOUT(b->currentIndex(), 1, 10000);
    const double handedBack = b->activeAudio().volume();
    const double released = b->spareAudio().volume();
    QCOMPARE(b->current().value("title").toString(), QString("Second"));
    QVERIFY2(std::abs(handedBack - settled) < 0.02,
             qPrintable(QString("the mixer was handed back at %1, not %2").arg(handedBack).arg(settled)));
    QVERIFY2(released < 0.001,
             qPrintable(QString("the song that left was still at %1").arg(released)));
    QVERIFY(b->playing());
    QVERIFY2(b->m_usingB, "the decks swapped rather than the audio being reloaded");
    QVERIFY(b->media()->source().toLocalFile().contains("02 second"));
    b->stop();
  }

  // A song too short to hold two overlaps is played in full instead.
  void aShortRecordingIsNotMostlyOverlap() {
    auto b = loaded();
    QTRY_VERIFY_WITH_TIMEOUT(!b->importingLocal(), 20000);
    b->library("files");
    b->setCrossfadeSeconds(12);
    b->enqueueItems(b->results()->rows);
    b->playAt(0);
    QTRY_VERIFY_WITH_TIMEOUT(b->playing() && b->duration() > 0, 10000);
    QVERIFY(b->duration() < 12000 * 2);
    QElapsedTimer watch;
    watch.start();
    while (b->currentIndex() == 0 && watch.elapsed() < 15000) {
      QVERIFY2(!b->crossfading(), "a four second song must not be overlapped by twelve");
      QTest::qWait(20);
    }
    QCOMPARE(b->currentIndex(), 1);
    b->stop();
  }

  // Skipping by hand during an overlap lands on what was asked for, and leaves
  // nothing playing behind it.
  void steeringDuringAnOverlapTakesBackTheHeadStart() {
    auto b = loaded();
    QTRY_VERIFY_WITH_TIMEOUT(!b->importingLocal(), 20000);
    b->library("files");
    b->setCrossfadeSeconds(2);
    // Settings outlive a Backend within one process, so this test states the
    // ones it depends on rather than inheriting whatever ran before it.
    b->setRepeat(0);
    b->setShuffle(false);
    b->setGapless(true);
    b->enqueueItems(b->results()->rows);
    b->playAt(0);
    QTRY_VERIFY_WITH_TIMEOUT(b->playing() && b->duration() > 0, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(b->crossfading(), 20000);
    b->playAt(2);
    QVERIFY(!b->crossfading());
    QCOMPARE(b->m_handoffIndex, -1);
    QCOMPARE(b->spareDeck().playbackState(), QMediaPlayer::StoppedState);
    QVERIFY(b->spareDeck().source().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(b->playing(), 10000);
    QCOMPARE(b->currentIndex(), 2);
    QCOMPARE(b->current().value("title").toString(), QString("Third"));
    QTRY_VERIFY(std::abs(b->activeAudio().volume() - b->effectiveVolume()) < 0.02);
    b->stop();
  }

  // Seeking back out of the tail returns the head start too.
  void seekingOutOfTheTailReleasesTheSpare() {
    auto b = loaded();
    QTRY_VERIFY_WITH_TIMEOUT(!b->importingLocal(), 20000);
    b->library("files");
    b->setCrossfadeSeconds(2);
    b->enqueueItems(b->results()->rows);
    b->playAt(1);
    QTRY_VERIFY_WITH_TIMEOUT(b->playing() && b->duration() > 4000, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(b->crossfading(), 20000);
    b->seek(0);
    QVERIFY(!b->crossfading());
    QCOMPARE(b->m_handoffIndex, -1);
    QVERIFY(b->spareDeck().source().isEmpty());
    QCOMPARE(b->currentIndex(), 1);
    QTRY_VERIFY(std::abs(b->activeAudio().volume() - b->effectiveVolume()) < 0.02);
    b->stop();
  }

  // Repeating one song has nothing to fade into.
  void repeatingOneSongNeverOverlaps() {
    auto b = loaded();
    QTRY_VERIFY_WITH_TIMEOUT(!b->importingLocal(), 20000);
    b->library("files");
    b->setCrossfadeSeconds(2);
    b->setRepeat(2);
    b->enqueueItems(b->results()->rows);
    b->playAt(1);
    QTRY_VERIFY_WITH_TIMEOUT(b->playing() && b->duration() > 0, 10000);
    QElapsedTimer watch;
    watch.start();
    while (watch.elapsed() < 9000) {
      QVERIFY(!b->crossfading());
      QCOMPARE(b->currentIndex(), 1);
      QTest::qWait(25);
    }
    b->setRepeat(0);
    b->stop();
  }

  // The last song of a queue has nowhere to go, so it simply ends.
  void theEndOfTheQueueStillEnds() {
    auto b = loaded();
    QTRY_VERIFY_WITH_TIMEOUT(!b->importingLocal(), 20000);
    b->library("files");
    b->setCrossfadeSeconds(2);
    b->setRepeat(0);
    b->setAutoplay(false);
    b->enqueueItems(b->results()->rows);
    b->playAt(2);
    QTRY_VERIFY_WITH_TIMEOUT(b->playing() && b->duration() > 0, 10000);
    QElapsedTimer watch;
    watch.start();
    while (b->playing() && watch.elapsed() < 15000) {
      QVERIFY(!b->crossfading());
      QTest::qWait(25);
    }
    QVERIFY(!b->playing());
    QCOMPARE(b->currentIndex(), 2);
    QVERIFY(b->spareDeck().source().isEmpty());
  }

  // Shuffling decides where it is going before it starts going there, and
  // arrives at that very song.
  void shufflingPicksItsDestinationInAdvance() {
    auto b = loaded();
    QTRY_VERIFY_WITH_TIMEOUT(!b->importingLocal(), 20000);
    b->library("files");
    b->setCrossfadeSeconds(2);
    b->setShuffle(true);
    b->enqueueItems(b->results()->rows);
    b->playAt(0);
    QTRY_VERIFY_WITH_TIMEOUT(b->playing() && b->duration() > 0, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(b->crossfading(), 20000);
    const int decided = b->m_handoffIndex;
    QVERIFY2(decided >= 0 && decided != 0, "a destination was chosen up front");
    // Arrive at the song that was chosen, not at whichever one is next.
    QTRY_VERIFY_WITH_TIMEOUT(b->currentIndex() != 0, 10000);
    QCOMPARE(b->currentIndex(), decided);
    b->setShuffle(false);
    b->stop();
  }

  // The decoded-audio meters are fed by a tap on the player. Only the deck
  // being heard may carry one: a tap left on the idle deck starves the active
  // one, and the meters die. So the tap has to move with the swap.
  void theMetersFollowTheSwap() {
    auto b = std::make_unique<Backend>();
    b->setVolume(0.5);
    b->setLyricsFallback(false);
    b->setAutoplay(false);
    b->setWatchMusicFolders(false);
    b->setOnlineArtwork(false);
    b->setPrepareNext(false);
    b->setMotion(true);
    b->setUiActive(true);
    b->clearQueue();
    b->importLocalFiles({QUrl::fromLocalFile(music.filePath("tone one.flac")),
                         QUrl::fromLocalFile(music.filePath("tone two.flac"))});
    QTRY_VERIFY_WITH_TIMEOUT(!b->importingLocal(), 20000);
    b->library("files");
    // Earlier cases in this process have imported their own fixtures, so the
    // two tones are picked out by name rather than by being all there is.
    QVariantList tones;
    for (const auto &row : b->results()->rows)
      if (row.toMap().value("title").toString().startsWith("Tone "))
        tones.append(row);
    QCOMPARE(tones.size(), 2);
    b->setCrossfadeSeconds(2);
    b->enqueueItems(tones);
    QCOMPARE(b->queue()->count(), 2);
    b->playAt(0);
    QTRY_VERIFY_WITH_TIMEOUT(b->playing() && b->duration() > 0, 10000);
    const auto loud = [&b] {
      for (const auto &level : b->audioLevels())
        if (level.toDouble() > 0.05)
          return true;
      return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(loud(), 10000);
    const int deckBefore = b->m_usingB ? 1 : 0;
    QTRY_VERIFY_WITH_TIMEOUT(b->currentIndex() != 0, 20000);
    QVERIFY2(b->currentIndex() == 1,
             qPrintable(QString("the next song took over, index %1").arg(b->currentIndex())));
    QVERIFY2((b->m_usingB ? 1 : 0) != deckBefore, "the decks really did swap");
    QVERIFY(b->playing());
    // The tone on the other deck has to reach the meters just the same.
    QTRY_VERIFY_WITH_TIMEOUT(loud(), 10000);
    b->stop();
  }

  // A backend playing one fixture on its own, meters on.
  std::unique_ptr<Backend> playingAlone(const QString &title) {
    auto b = std::make_unique<Backend>();
    b->setVolume(0.5);
    b->setLyricsFallback(false);
    b->setAutoplay(false);
    b->setWatchMusicFolders(false);
    b->setOnlineArtwork(false);
    b->setPrepareNext(false);
    b->setMotion(true);
    b->setUiActive(true);
    b->setCrossfadeSeconds(0);
    b->clearQueue();
    b->importLocalFiles({QUrl::fromLocalFile(music.filePath(title.toLower() + ".flac"))});
    if (!QTest::qWaitFor([&b] { return !b->importingLocal(); }, 20000))
      return {};
    b->library("files");
    QVariantList rows;
    for (const auto &row : b->results()->rows)
      if (row.toMap().value("title").toString() == title)
        rows.append(row);
    if (rows.size() != 1)
      return {};
    b->enqueueItems(rows);
    b->playAt(0);
    return b;
  }
  static double loudest(const QVariantList &levels) {
    double m = 0;
    for (const auto &v : levels)
      m = std::max(m, v.toDouble());
    return m;
  }

  // The meters read the analysed file at the playback position: silent over
  // the silent part, lit over the tone, across seeks in both directions and
  // quiet while paused.
  void theMetersReadThePlaybackPosition() {
    auto b = playingAlone("Late tone");
    QVERIFY(b);
    QTRY_VERIFY_WITH_TIMEOUT(b->playing() && b->duration() > 0, 10000);
    const auto id = Backend::analysisKey(b->current());
    QTRY_VERIFY_WITH_TIMEOUT(b->audioAnalysis()->envelope(id).isValid(), 15000);
    QVERIFY(b->position() < 2500);
    QTest::qWait(300);
    QVERIFY2(loudest(b->audioLevels()) < 0.05, "silence reads as silence");
    b->seek(4000);
    QTRY_VERIFY_WITH_TIMEOUT(loudest(b->audioLevels()) > 0.3, 3000);
    // 330 Hz sits between the two lowest bands, never in the top one.
    QVERIFY(b->audioLevels()[1].toDouble() > b->audioLevels()[4].toDouble() + 0.2);
    b->seek(500);
    QTRY_VERIFY_WITH_TIMEOUT(loudest(b->audioLevels()) < 0.05, 3000);
    b->seek(3500);
    QTRY_VERIFY_WITH_TIMEOUT(loudest(b->audioLevels()) > 0.3, 3000);
    b->pause();
    QTRY_VERIFY_WITH_TIMEOUT(loudest(b->audioLevels()) < 0.01, 3000);
    b->stop();
  }

  // A song analysed on an earlier run is read from the cache the moment it
  // plays again after a restart. A restart restores the queue and the song
  // directly, without announcing a track change, and pressing play then
  // loads the file; nothing announces the cached envelope either, which is
  // how the meters and the visualizer once stayed empty.
  void aCachedAnalysisIsUsedAfterARestart() {
    {
      auto first = playingAlone("Tone one");
      QVERIFY(first);
      QTRY_VERIFY_WITH_TIMEOUT(first->playing(), 10000);
      QTRY_VERIFY_WITH_TIMEOUT(first->m_envelope.isValid(), 15000);
      first->pause();
      // Destroying it saves the session, as quitting does.
    }
    Backend restored; // loads the saved session: the queue and the song, not yet playing
    restored.setMotion(true);
    restored.setUiActive(true);
    QCOMPARE(restored.current().value("title").toString(), QString("Tone one"));
    QVERIFY(!restored.m_envelope.isValid());
    QSignalSpy tracks(&restored, &Backend::trackChanged);
    restored.play();
    QTRY_VERIFY_WITH_TIMEOUT(restored.playing(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(restored.m_envelope.isValid(), 1500);
    QVERIFY2(tracks.isEmpty(), "the restore path is the one without a track change");
    QTRY_VERIFY_WITH_TIMEOUT(loudest(restored.audioLevels()) > 0.2, 3000);
    restored.stop();
  }

  // YouTube's audio is fragmented MP4, and the Mac's player measures such a
  // file at twice its length: a 4:45 song showed 9:30, and its audio ended
  // mid-bar, then nothing played until the bar reached the end. The helper
  // rewrites what it downloads as an ordinary file, so that never reaches the
  // player. This records the behaviour that makes the rewrite necessary; if a
  // later macOS or Qt measures it correctly the expected failure passes, which
  // fails the test, as a reminder that the rewrite can go.
  void aFragmentedFileIsMeasuredAtTwiceItsLength() {
    const auto fragmented = fragmentedSong("fragmented one.m4a", "Fragmented one");
    QVERIFY(!fragmented.isEmpty());
    auto b = playing({fragmented}, true);
    QVERIFY(b);
    // The player's own measurement, not the length stored with the song, which
    // is what is shown until the player has read the file.
    QTRY_VERIFY_WITH_TIMEOUT(b->playing() && b->media()->duration() > 0, 10000);
    qInfo("fragmented six-second file: shown length %lld ms", (long long)b->duration());
    QEXPECT_FAIL("", "the Mac's player measures a fragmented MP4 at twice its length", Continue);
    QVERIFY2(qAbs(b->duration() - 6000) < 300, qPrintable(QString("shown length %1 ms").arg(b->duration())));
    QVERIFY(b->duration() > 10000);
    b->stop();
  }

  // What the user asked for: a song's shown length matches its real length,
  // and the next song starts when its audio really ends, not at some later
  // time. Both with the handover to the next song on and off.
  void aSongShowsItsRealLengthAndTheNextStartsWhenItEnds_data() {
    QTest::addColumn<bool>("gapless");
    QTest::newRow("with gapless handover") << true;
    QTest::newRow("without it") << false;
  }
  void aSongShowsItsRealLengthAndTheNextStartsWhenItEnds() {
    QFETCH(bool, gapless);
    const auto first = fragmentedSong(QString("whole first %1.m4a").arg(gapless), "Whole first");
    const auto second = fragmentedSong(QString("whole second %1.m4a").arg(gapless), "Whole second");
    QVERIFY(!first.isEmpty() && !second.isEmpty());
    const auto wholeFirst = madeWhole(first, QString("first whole %1.m4a").arg(gapless));
    const auto wholeSecond = madeWhole(second, QString("second whole %1.m4a").arg(gapless));
    QVERIFY(!wholeFirst.isEmpty() && !wholeSecond.isEmpty());
    auto b = playing({wholeFirst, wholeSecond}, gapless);
    QVERIFY(b);
    QTRY_VERIFY_WITH_TIMEOUT(b->playing() && b->media()->duration() > 0, 10000);
    QCOMPARE(b->currentIndex(), 0);
    QVERIFY2(qAbs(b->duration() - 6000) < 300, qPrintable(QString("shown length %1 ms for a six-second song").arg(b->duration())));
    // Play it to within a second and a half of its real end, a point named
    // outright so a wrongly measured file cannot move it.
    b->seek(4500);
    QElapsedTimer watch;
    watch.start();
    QTRY_VERIFY_WITH_TIMEOUT(b->currentIndex() == 1, 15000);
    const auto took = watch.elapsed();
    QVERIFY2(took > 900 && took < 3500,
             qPrintable(QString("the next song started %1 ms after a song with 1500 ms left, not after about 1500").arg(took)));
    QCOMPARE(b->current().value("title").toString(), QString("Whole second"));
    QTRY_VERIFY_WITH_TIMEOUT(b->playing() && b->media()->duration() > 0, 5000);
    QVERIFY2(qAbs(b->duration() - 6000) < 300, qPrintable(QString("the next song shows %1 ms").arg(b->duration())));
    b->stop();
  }

  // A known issue, kept visible rather than hidden. Qt's darwin backend (6.11,
  // and still in 6.12 and dev) builds a seek from the item's current time and
  // keeps its timescale; until the playback clock has run that timescale is a
  // second, so a seek on a song that has not yet played lands on the whole
  // second at or before the target. Musix does not work around it yet. If Qt
  // fixes it this reports an unexpected pass, which fails, as a reminder to
  // drop the note. Reported as https://qt-project.atlassian.net/browse/COIN-1332;
  // docs/known-issues.md tracks it.
  void aSeekBeforePlaybackLandsOnTheWholeSecond() {
    auto b = playingAlone("Tone two");
    QVERIFY(b);
    QTRY_VERIFY_WITH_TIMEOUT(b->playing() && b->duration() > 5000, 10000);
    const auto source = b->m_media().source();
    b->m_media().stop();
    b->m_media().setSource(QUrl());
    b->m_media().setSource(source);
    b->m_media().pause();
    QTRY_VERIFY_WITH_TIMEOUT(b->m_media().isSeekable() && b->m_media().duration() > 5000, 5000);
    b->seek(4200);
    QTest::qWait(1000);
    QEXPECT_FAIL("", "Qt's darwin backend rounds a seek before playback to the whole second (COIN-1332)", Continue);
    QVERIFY2(qAbs(b->m_media().position() - 4200) <= 30, qPrintable(QString("the player is at %1").arg(b->m_media().position())));
    QVERIFY(b->m_media().position() >= 4000 && b->m_media().position() <= 4200);
    b->stop();
  }

  // Levelling works from the analysed loudness, on the first play, for a file
  // with no tags.
  void levellingUsesTheAnalysis() {
    auto b = playingAlone("Tone two");
    QVERIFY(b);
    b->setVolumeNormalization(true);
    QTRY_VERIFY_WITH_TIMEOUT(b->playing() && b->duration() > 0, 10000);
    const auto id = Backend::analysisKey(b->current());
    QTRY_COMPARE_WITH_TIMEOUT(b->normalizationSource(), QString("Analysed"), 15000);
    const double lufs = b->analysedLoudness(id);
    QVERIFY2(lufs > -30 && lufs < -10, qPrintable(QString::number(lufs)));
    QVERIFY(std::abs(b->normalizationGainDb() - qBound(-15.0, -18.0 - lufs, 6.0)) < 0.01);
    // The glide finishes at the level the gain asks for.
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(b->activeAudio().volume() - b->effectiveVolume()) < 0.01, 3000);
    b->setVolumeNormalization(false);
    b->stop();
  }

  // Everything that normally rides on a track change still happens when the
  // decks swap instead.
  void theRestOfTheApplicationMovesWithTheAudio() {
    auto b = loaded();
    QTRY_VERIFY_WITH_TIMEOUT(!b->importingLocal(), 20000);
    b->library("files");
    b->setCrossfadeSeconds(2);
    b->enqueueItems(b->results()->rows);
    b->playAt(0);
    QTRY_VERIFY_WITH_TIMEOUT(b->playing() && b->duration() > 0, 10000);
    const auto leaving = b->current().value("id").toString();
    QSignalSpy tracks(b.get(), &Backend::trackChanged);
    QSignalSpy lyrics(b.get(), &Backend::lyricsChanged);
    const auto token = b->trackToken();
    QTRY_COMPARE_WITH_TIMEOUT(b->currentIndex(), 1, 20000);
    QTRY_VERIFY(!b->crossfading());
    QVERIFY2(tracks.count() >= 1, "the track change was announced");
    QVERIFY2(lyrics.count() >= 1, "the lyrics were released with it");
    QVERIFY2(b->trackToken() != token, "the track token moved on");
    QVERIFY(b->current().value("id").toString() != leaving);
    QVERIFY2(b->lyricLines().isEmpty(), "the old song's lyrics did not linger");
    // History records the song that is now playing.
    b->library("history");
    QVERIFY2(b->results()->count() >= 1, "the change was recorded in history");
    b->stop();
  }
};
QTEST_MAIN(CrossfadeTest)
#include "crossfade_test.moc"
