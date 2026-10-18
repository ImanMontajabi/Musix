// How Telegram channels' marketing is taken off titles and artists, on the
// patterns real channels use, and what must survive.
#include "telegramtext.h"
#include <QtTest>

using telegramtext::cleanSong;
using telegramtext::stripPromo;

class TelegramTextTest : public QObject {
  Q_OBJECT
private slots:
  void handlesAndLinksAreStripped_data() {
    QTest::addColumn<QString>("raw");
    QTest::addColumn<QString>("clean");
    QTest::newRow("trailing handle") << "Blinding Lights @MusicChannel" << "Blinding Lights";
    QTest::newRow("id marker") << "Blinding Lights 🆔 @MusicChannel" << "Blinding Lights";
    QTest::newRow("join") << "Blinding Lights Join @MusicChannel" << "Blinding Lights";
    QTest::newRow("bracketed handle") << "Blinding Lights [ @MyChannel ]" << "Blinding Lights";
    QTest::newRow("round bracketed link") << "Blinding Lights (t.me/music)" << "Blinding Lights";
    QTest::newRow("pipe handle") << "Blinding Lights | @Channel" << "Blinding Lights";
    QTest::newRow("leading handle") << "@Channel - Blinding Lights" << "Blinding Lights";
    QTest::newRow("https link") << "Blinding Lights https://t.me/music_channel" << "Blinding Lights";
    QTest::newRow("bare link") << "Blinding Lights t.me/music_channel" << "Blinding Lights";
    QTest::newRow("www link") << "Blinding Lights www.example.com/x" << "Blinding Lights";
    QTest::newRow("two handles") << "Blinding Lights [@first_ch | @second_ch]" << "Blinding Lights";
    QTest::newRow("handle between") << "Blinding Lights @Chan (Live)" << "Blinding Lights (Live)";
    QTest::newRow("file extension") << "Blinding Lights.mp3" << "Blinding Lights";
    QTest::newRow("extension then handle") << "Blinding Lights @Chan.flac" << "Blinding Lights";
    QTest::newRow("uppercase extension") << "Blinding Lights.MP3" << "Blinding Lights";
    QTest::newRow("handle with digits and underscore") << "Song | @Music_Hub_24" << "Song";
  }
  void handlesAndLinksAreStripped() {
    QFETCH(QString, raw);
    QFETCH(QString, clean);
    QCOMPARE(stripPromo(raw), clean);
    QCOMPARE(stripPromo(clean), clean); // idempotent
  }
  void theMusicsOwnBracketsSurvive_data() {
    QTest::addColumn<QString>("text");
    QTest::newRow("remix") << "Song (TMC & BLH Remix)";
    QTest::newRow("prod") << "Song (Prod. Metro Boomin)";
    QTest::newRow("feat") << "Song (feat. Someone)";
    QTest::newRow("live") << "Song (Live)";
    QTest::newRow("square") << "Song [Remastered 2011]";
    QTest::newRow("dash") << "Song - Live at Wembley";
    QTest::newRow("at in the middle of a word") << "Rock@Home";
    QTest::newRow("email") << "mail me at someone@example.com";
    QTest::newRow("dotted title") << "Mr. Brightside";
    QTest::newRow("short at") << "Me @ You";
  }
  void theMusicsOwnBracketsSurvive() {
    QFETCH(QString, text);
    QCOMPARE(stripPromo(text), text);
  }
  void artistAndTitleAreSplitWhenThereIsNoPerformer() {
    auto c = cleanSong("The Weeknd - Blinding Lights @Chan", "");
    QCOMPARE(c.artist, QString("The Weeknd"));
    QCOMPARE(c.title, QString("Blinding Lights"));
    c = cleanSong("The Weeknd – Blinding Lights [t.me/xyz_ch]", "Telegram Audio");
    QCOMPARE(c.artist, QString("The Weeknd"));
    QCOMPARE(c.title, QString("Blinding Lights"));
    c = cleanSong("The Weeknd - Blinding Lights - Remix", "");
    QCOMPARE(c.title, QString("Blinding Lights - Remix")); // the first separator is the split
    c = cleanSong("The Weeknd - Blinding Lights.mp3", "unknown");
    QCOMPARE(c.artist, QString("The Weeknd"));
    QCOMPARE(c.title, QString("Blinding Lights"));
  }
  void aKnownPerformerIsNeverReplacedBySplitting() {
    const auto c = cleanSong("Someone - Else", "Real Artist @Chan");
    QCOMPARE(c.artist, QString("Real Artist"));
    QCOMPARE(c.title, QString("Someone - Else"));
  }
  void notEveryDashIsASplit() {
    QCOMPARE(cleanSong("01 - Intro", "").title, QString("01 - Intro"));
    QCOMPARE(cleanSong("01 - Intro", "").artist, QString());
    QCOMPARE(cleanSong("Intro-Outro", "").artist, QString());   // no spaces: a hyphenated word
    QCOMPARE(cleanSong("- Intro", "").artist, QString());
    QCOMPARE(cleanSong("Artist - ", "").artist, QString());      // nothing after it
  }
  void aTitleThatIsOnlyPromoKeepsWhatTheMessageSaid() {
    QCOMPARE(cleanSong("@MusicChannel", "").title, QString("@MusicChannel"));
    QCOMPARE(cleanSong("", "").title, QString("Untitled"));
    QCOMPARE(cleanSong("@Chan.mp3", "").title, QString("@Chan"));
  }
  void anArtistThatIsOnlyPromoIsEmpty() {
    QCOMPARE(cleanSong("Song", "@Chan").artist, QString());
    QCOMPARE(cleanSong("Song", "Artist (@Chan)").artist, QString("Artist"));
  }
  void cleaningIsStableWhateverTheOrder() {
    for (const auto &raw : {QString("A - B @chan"), QString("A – B | @chan"), QString("B"), QString("01 - B"), QString("B (Live) [@chan]")}) {
      const auto once = cleanSong(raw, "");
      const auto twice = cleanSong(once.title, once.artist);
      QCOMPARE(twice.title, once.title);
      QCOMPARE(twice.artist, once.artist);
    }
  }
};
QTEST_APPLESS_MAIN(TelegramTextTest)
#include "telegramtext_test.moc"
