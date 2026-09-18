// Live checks for the features added on top of the original interface. Each one
// drives the real application and asserts against what the running window
// reports, then photographs the result for review.
#include "uitest.h"
#include "backend.h"
#include "m3color.h"
#include "rowselection.h"
#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QFont>
#include <QTest>
#include <qpa/qwindowsysteminterface.h>
#include <functional>

namespace {
QQuickItem *shownItem(QQuickItem *root, const QString &name) {
  if (!root->isVisible())
    return nullptr;
  if (root->objectName() == name)
    return root;
  for (auto child : root->childItems())
    if (auto found = shownItem(child, name))
      return found;
  return nullptr;
}
QQuickItem *anyItem(QQuickItem *root, const QString &name) {
  if (root->objectName() == name)
    return root;
  for (auto child : root->childItems())
    if (auto found = anyItem(child, name))
      return found;
  return nullptr;
}

struct Check {
  Backend *backend;
  QQuickWindow *window;
  QString directory;
  int failures = 0;

  void check(bool ok, const QString &label) {
    fprintf(stdout, "%s %s\n", ok ? "PASS" : "FAIL", qPrintable(label));
    fflush(stdout);
    if (!ok)
      ++failures;
  }
  bool until(const std::function<bool()> &predicate, int timeout = 8000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeout)
      QTest::qWait(25);
    return predicate();
  }
  QVariant evaluate(const QString &script) {
    QQmlExpression expression(qmlContext(window), window, script);
    return expression.evaluate();
  }
  QColor themeColor(const QString &role) { return evaluate("Theme." + role).value<QColor>(); }
  void shot(const QString &name) {
    QTest::qWait(280);
    shotNow(name);
  }
  // Capturing mid-transition cannot afford to settle first.
  void shotNow(const QString &name) {
    check(window->grabWindow().save(directory + '/' + name + ".png"), "capture " + name);
  }
  void click(const QString &name) {
    tap(name);
    QTest::qWait(320);
  }
  // A click scoped to one part of the window, where the same row names appear
  // in more than one list at once.
  void clickWithin(QQuickItem *parent, const QString &name) {
    auto item = parent ? shownItem(parent, name) : nullptr;
    check(item, "find " + name + " in " + (parent ? parent->objectName() : QString("nothing")));
    if (!item)
      return;
    const auto point = item->mapToScene(item->boundingRect().center()).toPoint();
    QTest::mouseMove(window, point);
    QTest::qWait(60);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
    QTest::qWait(320);
  }
  // A click with no settling wait, for watching what the click sets off.
  void tap(const QString &name) {
    auto item = shownItem(window->contentItem(), name);
    check(item, "find " + name);
    if (!item)
      return;
    const auto point = item->mapToScene(item->boundingRect().center()).toPoint();
    QTest::mouseMove(window, point);
    QTest::qWait(60);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
  }
  QObject *dialog(const QString &name, const QString &method = "open") {
    auto found = window->findChild<QObject *>(name);
    check(found, "reach " + name);
    if (found) {
      QMetaObject::invokeMethod(found, qPrintable(method));
      QTest::qWait(420);
    }
    return found;
  }
  void closeDialog(QObject *target) {
    if (target)
      QMetaObject::invokeMethod(target, "close");
    QTest::qWait(320);
  }
  void finish() {
    fprintf(stdout, "RESULT %d failures\n", failures);
    fflush(stdout);
    QCoreApplication::exit(failures ? 1 : 0);
  }
};

void paintCover(const QString &path, const QColor &base, const QColor &accent) {
  QImage cover(640, 640, QImage::Format_RGB32);
  QPainter paint(&cover);
  paint.fillRect(cover.rect(), base);
  paint.setRenderHint(QPainter::Antialiasing);
  paint.fillRect(0, 0, 640, 240, accent);
  paint.setBrush(accent.lighter(130));
  paint.setPen(Qt::NoPen);
  paint.drawEllipse(QPoint(430, 430), 150, 150);
  paint.end();
  cover.save(path);
}

bool encodeTrack(Check &c, const QString &path, const QString &title, const QString &album,
                 const QString &artist, int track, const QStringList &extra = {}) {
  QStringList arguments{"-nostdin", "-v", "error", "-f", "lavfi", "-i", "anullsrc=r=8000:cl=mono",
                        "-t", "150", "-metadata", "title=" + title, "-metadata", "album=" + album,
                        "-metadata", "artist=" + artist, "-metadata", "album_artist=" + artist,
                        "-metadata", "date=2026", "-metadata", "track=" + QString::number(track)};
  arguments += extra;
  arguments << path;
  QProcess encode;
  encode.start("ffmpeg", arguments);
  const bool ok = encode.waitForFinished(20000) && encode.exitCode() == 0;
  c.check(ok, "generate " + QFileInfo(path).fileName());
  return ok;
}

double contrastOf(const QColor &a, const QColor &b) {
  const auto luminance = [](const QColor &c) {
    const auto channel = [](double v) {
      return v <= 0.040449936 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
  };
  const double x = luminance(a), y = luminance(b);
  return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}
} // namespace

void runDynamicColorTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music/Still Water");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1320, 860);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(false);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);
  b->setLyricsFallback(false);
  b->setArtworkAccent(false);
  b->setAccentColor("");
  QTest::qWait(200);

  paintCover(c.directory + "/music/Still Water/cover.png", QColor("#1f4f6b"), QColor("#d98324"));
  if (!encodeTrack(c, c.directory + "/music/Still Water/01.flac", "Across the still water",
                   "Still Water", "Rill", 1))
    return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the colour fixture");
  b->library("files");
  c.check(c.until([&] { return b->results()->count() == 1; }), "the fixture is in the library");

  // --- Nothing chosen: the built-in palette is untouched ---
  const auto plainBackground = c.themeColor("background");
  const auto plainSurface = c.themeColor("surface");
  c.check(!c.evaluate("Theme.useSource").toBool(), "no source colour by default");
  c.check(plainBackground == QColor("#181211"), "the built-in dark background is unchanged");
  c.shot("01-default-palette");

  // --- A chosen source colour reaches the surfaces, not just the accent ---
  b->setAccentColor("#386a20");
  QTest::qWait(250);
  c.check(c.evaluate("Theme.useSource").toBool(), "the chosen colour drives the scheme");
  const auto greenBackground = c.themeColor("background");
  const auto greenSurface = c.themeColor("surface");
  const auto greenContainer = c.themeColor("container");
  c.check(greenBackground != plainBackground && greenSurface != plainSurface,
          "surfaces follow the source colour, not only the accent");
  // Material tints neutrals with a trace of the source hue; it must stay a hint.
  for (const auto &pair : QList<QPair<QString, QColor>>{{"background", greenBackground},
                                                        {"surface", greenSurface},
                                                        {"container", greenContainer}}) {
    const double chroma = m3::measure(pair.second).chroma;
    c.check(chroma > 0.5 && chroma < 12.0,
            QString("%1 is tinted but stays neutral (chroma %2)").arg(pair.first).arg(chroma, 0, 'f', 1));
    c.check(qAbs(m3::measure(pair.second).hue - m3::measure(QColor("#386a20")).hue) < 12.0,
            QString("%1 carries the source hue").arg(pair.first));
  }
  // The surface ladder still climbs away from the background in a dark theme.
  c.check(m3::toneOf(greenBackground) < m3::toneOf(greenSurface) &&
              m3::toneOf(greenSurface) < m3::toneOf(greenContainer) &&
              m3::toneOf(greenContainer) < m3::toneOf(c.themeColor("high")),
          "dark surfaces lighten as they stack");
  c.shot("02-green-source-dark");

  const auto floors = [&](const QString &where) {
    const QStringList surfaces{"background", "surface", "container", "high"};
    for (const auto &surface : surfaces) {
      c.check(contrastOf(c.themeColor("text"), c.themeColor(surface)) >= 4.5,
              QString("%1: body text keeps 4.5:1 on %2").arg(where, surface));
      c.check(contrastOf(c.themeColor("muted"), c.themeColor(surface)) >= 4.5,
              QString("%1: secondary text keeps 4.5:1 on %2").arg(where, surface));
      c.check(contrastOf(c.themeColor("primary"), c.themeColor(surface)) >= 4.5,
              QString("%1: the accent keeps 4.5:1 on %2").arg(where, surface));
    }
    c.check(contrastOf(c.themeColor("primaryText"), c.themeColor("primary")) >= 4.5,
            where + ": text on the accent keeps 4.5:1");
    c.check(contrastOf(c.themeColor("containerText"), c.themeColor("primaryContainer")) >= 4.5,
            where + ": container text keeps 4.5:1");
    c.check(contrastOf(c.themeColor("outline"), c.themeColor("surface")) >= 1.3,
            where + ": dividers stay visible");
  };
  floors("green dark");

  b->setTheme("light");
  QTest::qWait(300);
  const auto lightBackground = c.themeColor("background");
  c.check(m3::toneOf(lightBackground) > 90, "the light theme starts from a near-white surface");
  c.check(m3::toneOf(c.themeColor("background")) > m3::toneOf(c.themeColor("container")),
          "light surfaces darken as they stack");
  floors("green light");
  c.shot("03-green-source-light");
  b->setTheme("dark");
  QTest::qWait(300);

  // --- Artwork drives it, and a different cover moves it ---
  b->setAccentColor("");
  b->setArtworkAccent(true);
  b->enqueueItems(b->results()->rows);
  b->playAt(0);
  c.check(c.until([&] { return b->playing(); }), "the fixture plays");
  c.check(c.until([&] { return c.evaluate("Theme.useArtwork").toBool(); }),
          "the cover becomes the source colour");
  const auto coverBackground = c.themeColor("background");
  c.check(coverBackground != plainBackground, "the cover re-tints the window");
  floors("cover dark");
  c.shot("04-artwork-source");

  const auto warmHue = m3::measure(c.themeColor("primary")).hue;
  c.evaluate("Theme.artworkSeed=Qt.rgba(0.20,0.36,0.78,1)");
  QTest::qWait(350);
  const auto coolHue = m3::measure(c.themeColor("primary")).hue;
  c.check(qAbs(warmHue - coolHue) > 40, "a different cover moves the whole scheme");
  c.check(c.themeColor("background") != coverBackground, "including its surfaces");
  floors("cool cover");
  c.shot("05-artwork-source-cool");

  // --- Turning it off restores the built-in palette exactly ---
  b->setArtworkAccent(false);
  c.evaluate("Theme.artworkSeed=Qt.rgba(0,0,0,0)");
  QTest::qWait(300);
  c.check(!c.evaluate("Theme.useSource").toBool(), "the scheme is released");
  c.check(c.themeColor("background") == plainBackground && c.themeColor("surface") == plainSurface,
          "the built-in palette returns untouched");
  c.shot("06-released");

  b->stop();
  b->clearQueue();
  c.finish();
}

void runNavigationMotionTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1320, 860);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(true);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);

  paintCover(c.directory + "/music/cover.png", QColor("#1f4f6b"), QColor("#d98324"));
  for (int i = 1; i <= 3; ++i)
    if (!encodeTrack(c, QString("%1/music/%2.flac").arg(c.directory).arg(i),
                     QString("Track %1").arg(i), "Motion", "Rill", i))
      return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the motion fixture");

  auto column = anyItem(w->contentItem(), "contentColumn");
  auto body = anyItem(w->contentItem(), "contentBody");
  auto destinations = w->findChild<QObject *>("destinationTransition");
  auto tabs = w->findChild<QObject *>("tabTransition");
  c.check(column && body && destinations && tabs, "the motion targets and controllers exist");
  if (!column || !body || !destinations || !tabs)
    return c.finish();

  // --- The durations Material specifies ---
  c.check(destinations->property("totalDuration").toInt() == 300, "a transition lasts 300ms");
  c.check(destinations->property("leaveDuration").toInt() == 90,
          "the outgoing view leaves over the first 30%");
  c.check(destinations->property("arriveDuration").toInt() == 210,
          "the incoming view arrives over the remaining 70%");
  c.check(qAbs(destinations->property("arriveScale").toReal() - 0.92) < 0.001,
          "fading through grows the incoming view from 92%");
  c.check(qAbs(destinations->property("axisTravel").toReal() - 30) < 0.001,
          "sharing an axis travels 30dp");

  // --- Rail destinations fade through ---
  b->home();
  c.check(c.until([&] { return !b->busy(); }), "Home is ready");
  QTest::qWait(400);
  c.check(qAbs(column->opacity() - 1) < 0.01 && qAbs(column->scale() - 1) < 0.01,
          "a settled view sits at full size and opacity");
  c.tap("nav_library");
  // Sample during the outgoing half: the view must be on its way out.
  QTest::qWait(45);
  const double leavingOpacity = column->opacity();
  c.check(leavingOpacity < 0.9 && leavingOpacity > 0.0,
          QString("the outgoing destination is fading (%1)").arg(leavingOpacity, 0, 'f', 2));
  c.check(qAbs(column->property("shift").toReal()) < 0.01,
          "fading through never moves the view sideways");
  c.shotNow("01-fade-through-leaving");
  // Sample during the incoming half: it grows back from 92%.
  c.check(c.until([&] { return column->scale() < 0.999; }, 400), "the arriving destination is scaled");
  const double arrivingScale = column->scale();
  c.check(arrivingScale >= 0.919 && arrivingScale < 1.0,
          QString("the arriving destination grows from 92%% (%1)").arg(arrivingScale, 0, 'f', 3));
  // Let the arriving view become legible before photographing it.
  c.until([&] { return column->scale() > 0.96; }, 300);
  c.shotNow("02-fade-through-arriving");
  c.check(c.until([&] { return !destinations->property("running").toBool(); }, 2000),
          "the destination transition completes");
  c.check(qAbs(column->opacity() - 1) < 0.01 && qAbs(column->scale() - 1) < 0.01 &&
              qAbs(column->property("shift").toReal()) < 0.01,
          "the view is handed back exactly as it was found");
  c.check(b->page() == "library", "and the destination actually changed");

  // --- Library tabs share the X axis ---
  c.check(w->property("libraryTab") == "favorites", "the library opens on Liked songs");
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("files")));
  QTest::qWait(45);
  const double forwardShift = body->property("shift").toReal();
  c.check(forwardShift < -1,
          QString("moving forward pushes the outgoing tab left (%1)").arg(forwardShift, 0, 'f', 1));
  c.check(qAbs(column->property("shift").toReal()) < 0.01 && qAbs(column->scale() - 1) < 0.01,
          "the tab bar itself stays put");
  c.shotNow("03-shared-axis-forward");
  c.check(c.until([&] { return body->property("shift").toReal() > 1; }, 400),
          "the arriving tab enters from the right");
  c.until([&] { return body->opacity() > 0.5; }, 300);
  c.shotNow("03b-shared-axis-arriving");
  c.check(c.until([&] { return !tabs->property("running").toBool(); }, 2000),
          "the tab transition completes");
  c.check(qAbs(body->property("shift").toReal()) < 0.01 && qAbs(body->opacity() - 1) < 0.01,
          "and settles back in place");
  c.check(w->property("libraryTab") == "files", "the tab actually changed");

  // Travelling back through the tabs reverses the axis.
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("favorites")));
  QTest::qWait(45);
  const double backShift = body->property("shift").toReal();
  c.check(backShift > 1,
          QString("moving back pushes the outgoing tab right (%1)").arg(backShift, 0, 'f', 1));
  c.shotNow("04-shared-axis-back");
  c.check(c.until([&] { return !tabs->property("running").toBool(); }, 2000), "it completes too");

  // --- Reduced motion is honoured: no animation, and navigation still works ---
  b->setMotion(false);
  QTest::qWait(200);
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("files")));
  QTest::qWait(30);
  c.check(!tabs->property("running").toBool() && !destinations->property("running").toBool(),
          "reduced motion skips the transition entirely");
  c.check(qAbs(body->opacity() - 1) < 0.01 && qAbs(body->property("shift").toReal()) < 0.01,
          "and leaves nothing half-animated");
  c.check(w->property("libraryTab") == "files", "navigation still arrives");
  c.shot("05-reduced-motion");
  b->setMotion(true);

  // --- A second navigation mid-flight must not strand the view ---
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("history")));
  QTest::qWait(40);
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("mixes")));
  c.check(c.until([&] { return !tabs->property("running").toBool(); }, 3000),
          "an interrupted transition still finishes");
  c.check(qAbs(body->opacity() - 1) < 0.01 && qAbs(body->property("shift").toReal()) < 0.01 &&
              qAbs(body->scale() - 1) < 0.01,
          "and the view is left whole");
  c.check(w->property("libraryTab") == "mixes", "the last destination wins");
  c.shot("06-interrupted");

  b->stop();
  b->clearQueue();
  c.finish();
}

void runArtistHeroTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music/Still Water");
  QDir().mkpath(c.directory + "/music/Night Ferry");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1320, 860);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(true);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);

  // One artist, two albums, six songs: enough for the hero to have something
  // true to report.
  paintCover(c.directory + "/music/Still Water/cover.png", QColor("#1f4f6b"), QColor("#d98324"));
  paintCover(c.directory + "/music/Night Ferry/cover.png", QColor("#3d2a52"), QColor("#4fa3a5"));
  const QStringList still{"The light arrives", "Across the still water", "A quiet moment"};
  const QStringList ferry{"Harbour lights", "Night ferry", "Coming ashore"};
  for (int i = 0; i < still.size(); ++i)
    if (!encodeTrack(c, QString("%1/music/Still Water/%2.flac").arg(c.directory).arg(i + 1),
                     still[i], "Still Water", "Rill", i + 1))
      return c.finish();
  for (int i = 0; i < ferry.size(); ++i)
    if (!encodeTrack(c, QString("%1/music/Night Ferry/%2.flac").arg(c.directory).arg(i + 1),
                     ferry[i], "Night Ferry", "Rill", i + 1))
      return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the artist fixture");

  // --- The hero belongs to artist pages only ---
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("local-albums")));
  c.check(c.until([&] { return b->results()->count() == 2; }), "two albums group");
  QTest::qWait(400);
  c.check(!shownItem(w->contentItem(), "artistHero"), "an album grid shows no artist hero");
  c.check(shownItem(w->contentItem(), "collectionHeaderTitle"), "it keeps the standard header");
  c.shot("01-albums-standard-header");

  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("local-artists")));
  c.check(c.until([&] { return b->results()->count() == 1; }), "one artist groups");
  QTest::qWait(400);
  c.check(!shownItem(w->contentItem(), "artistHero"), "the artist grid is not an artist page");

  b->open(b->results()->get(0));
  c.check(c.until([&] { return !b->busy() && b->page() == "local-artist"; }), "the artist opens");
  c.check(c.until([&] { return shownItem(w->contentItem(), "artistHero") != nullptr; }),
          "the artist page raises its hero");
  auto hero = shownItem(w->contentItem(), "artistHero");
  if (!hero)
    return c.finish();
  c.check(!shownItem(w->contentItem(), "collectionHeaderTitle"),
          "and stands in for the standard header rather than doubling it");

  // --- What it says is true ---
  const auto info = b->artistInfo();
  c.check(info.value("tracks").toInt() == 6, "the hero counts every song");
  c.check(info.value("albums").toInt() == 2, "and every album");
  c.check(info.value("seconds").toLongLong() == 900, "and the real running time");
  auto summary = shownItem(w->contentItem(), "artistHeroSummary");
  c.check(summary && summary->property("text").toString() == "2 albums · 6 songs · 15 min",
          "the summary reads back what it counted");
  auto name = shownItem(w->contentItem(), "artistHeroName");
  c.check(name && name->property("text").toString() == "Rill", "the hero names the artist");

  // --- Material's large top app bar proportions ---
  auto portrait = shownItem(w->contentItem(), "artistHeroPortrait");
  c.check(portrait && qAbs(portrait->property("radius").toReal() - portrait->width() / 2) < 1,
          "the portrait is round, as an artist's picture is");
  c.check(hero->height() > 150, "the open band is a hero, not a row");
  c.check(shownItem(w->contentItem(), "artistHeroBackdrop"), "the cover sits behind it");
  // Exactly one Play action is offered at a time.
  auto listPlay = [&] {
    auto row = shownItem(w->contentItem(), "collectionToolsButton");
    return row != nullptr;
  };
  c.check(!listPlay(), "the list's own action row stands down while the hero is open");
  const double expanded = hero->height();
  const double titleExpanded = name->property("font").value<QFont>().pixelSize();
  c.shot("02-artist-hero");

  // --- It collapses on scroll and comes back ---
  auto tracks = shownItem(w->contentItem(), "tracksView");
  c.check(tracks, "the artist's songs are listed");
  if (tracks) {
    tracks->setProperty("contentY", tracks->property("originY").toReal() + 300);
    c.check(c.until([&] { return hero->height() < expanded - 40; }, 2000),
            "scrolling collapses the hero");
    c.check(c.until([&] {
      return name->property("font").value<QFont>().pixelSize() < titleExpanded;
    }, 2000), "and the name shrinks with it");
    auto actions = anyItem(w->contentItem(), "artistHeroActions");
    c.check(actions && actions->opacity() < 0.3, "the actions fade out of the collapsed bar");
    c.check(listPlay(), "and the list's action row takes them back");
    c.shot("03-artist-hero-collapsed");
    tracks->setProperty("contentY", tracks->property("originY").toReal());
    c.check(c.until([&] { return hero->height() > expanded - 5; }, 2000),
            "scrolling back opens it again");
    c.shot("04-artist-hero-restored");
  }

  // --- Its actions work ---
  c.check(b->queue()->count() == 0, "nothing is queued yet");
  c.click("artistHeroPlay");
  c.check(c.until([&] { return b->queue()->count() == 6; }), "Play queues the artist's songs");
  c.check(c.until([&] { return b->playing(); }), "and starts them");
  c.shot("05-artist-hero-playing");
  b->stop();
  b->clearQueue();
  b->setShuffle(false);
  c.click("artistHeroShuffle");
  c.check(c.until([&] { return b->queue()->count() == 6; }), "Shuffle queues them too");
  c.check(b->shuffle(), "and turns shuffling on");
  b->setShuffle(false);

  // Pinning is offered exactly where the library has something to pin. A local
  // artist is a grouping of tags rather than a collection with an id, so the
  // action hides there, the same way the standard header hides it.
  auto pin = anyItem(w->contentItem(), "artistHeroPin");
  c.check(pin, "the hero carries a pin action");
  c.check(pin && pin->isVisible() == !b->collectionItem().isEmpty(),
          "the pin is offered only when there is a collection to pin");
  c.shot("06-artist-hero-actions");

  // --- Leaving the artist puts the standard header back ---
  b->back();
  c.check(c.until([&] { return b->page() != "local-artist"; }), "Back leaves the artist");
  QTest::qWait(500);
  c.check(!shownItem(w->contentItem(), "artistHero"), "the hero is released");
  c.check(shownItem(w->contentItem(), "collectionHeaderTitle"), "the standard header returns");
  c.shot("07-after-artist");

  b->stop();
  b->clearQueue();
  c.finish();
}

void runSingAlongTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1320, 860);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(true);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);
  b->setLyricsFallback(false);

  paintCover(c.directory + "/music/cover.png", QColor("#26324f"), QColor("#e2a03f"));
  if (!encodeTrack(c, c.directory + "/music/01.flac", "Across the still water", "Still Water",
                   "Rill", 1))
    return c.finish();
  if (!encodeTrack(c, c.directory + "/music/02.flac", "No words", "Still Water", "Rill", 2))
    return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the sing-along fixture");
  b->library("files");
  b->collection()->setSortKey("title");
  QTest::qWait(200);
  c.check(c.until([&] { return b->results()->count() == 2; }), "two songs are available");
  b->enqueueItems(b->results()->rows);
  b->playAt(0);
  c.check(c.until([&] { return b->playing(); }), "playback starts");
  const auto sung = b->current();

  // Lines with explicit ends, and one without, so the fallback is exercised.
  QFile lrc(c.directory + "/sing.lrc");
  c.check(lrc.open(QIODevice::WriteOnly), "write the timed lyrics");
  lrc.write("[00:10.00]The light arrives\n[00:20.00]Across the still water\n"
            "[00:30.00]A quiet moment\n[00:40.00]We move with the tide\n");
  lrc.close();
  b->importLyrics(QUrl::fromLocalFile(lrc.fileName()), sung.value("id").toString());
  c.check(c.until([&] { return b->lyricLines().size() == 4; }), "four timed lines load");

  // --- The progress the fill is drawn from ---
  b->seek(0);
  c.check(c.until([&] { return b->lyricIndex() < 0; }, 3000), "before the first line there is none");
  c.check(b->lyricProgress() < 0, "and no progress to report");
  struct Sample { int position; int line; double progress; const char *what; };
  for (const auto &s : {Sample{10000, 0, 0.0, "the first line starts empty"},
                        Sample{15000, 0, 0.5, "and is half sung halfway through"},
                        Sample{19500, 0, 0.95, "and nearly full at its end"},
                        Sample{20000, 1, 0.0, "the next line starts empty in turn"},
                        Sample{35000, 2, 0.5, "a middle line tracks the same way"},
                        // The last line is held to the end of the audio, so it
                        // fills at the pace the rest of the song is sung at.
                        Sample{42000, 3, 0.2, "and a trailing line fills at the song's pace"},
                        Sample{49000, 3, 0.9, "reaching the end of the line, not of the track"}}) {
    b->seek(s.position);
    c.check(c.until([&] { return b->lyricIndex() == s.line; }, 3000),
            QString("%1 (line %2)").arg(s.what).arg(s.line));
    const double measured = b->lyricProgress();
    c.check(qAbs(measured - s.progress) < 0.12,
            QString("%1: fill is %2, expected about %3")
                .arg(s.what).arg(measured, 0, 'f', 2).arg(s.progress, 0, 'f', 2));
  }
  // Progress is a fraction, always.
  for (int position = 0; position <= 60000; position += 1500) {
    b->seek(position);
    QTest::qWait(20);
    const double measured = b->lyricProgress();
    c.check(measured < 0 || (measured >= 0 && measured <= 1),
            QString("progress stays a fraction at %1ms").arg(position));
  }

  // --- The layout ---
  w->setProperty("immersive", true);
  c.check(c.until([&] { return shownItem(w->contentItem(), "immersivePlayer") != nullptr; }),
          "the immersive player opens");
  auto player = shownItem(w->contentItem(), "immersivePlayer");
  if (!player)
    return c.finish();
  c.check(player->property("hasTimedLyrics").toBool(), "the song offers timed lyrics");
  QMetaObject::invokeMethod(player, "layoutRequested", Q_ARG(QString, QString("singalong")));
  c.check(c.until([&] { return player->property("displayedLayout") == "singalong"; }),
          "sing along can be chosen");
  auto singAlong = shownItem(w->contentItem(), "singAlong");
  c.check(singAlong, "the sing-along surface is on screen");
  c.check(!shownItem(w->contentItem(), "immersiveArtwork"),
          "it takes the whole stage rather than sharing it with the cover");
  c.check(!shownItem(w->contentItem(), "liveLyrics"), "and replaces the reading view");
  if (!singAlong)
    return c.finish();

  // --- The line being sung is the one that is emphasised ---
  b->seek(15000);
  c.check(c.until([&] { return b->lyricIndex() == 0; }, 3000), "the first line is live");
  QTest::qWait(500);
  auto current = shownItem(w->contentItem(), "singAlongCurrent");
  c.check(current && current->property("text").toString() == "The light arrives",
          "the sung line is the one marked current");
  const double activeSize = current ? current->property("font").value<QFont>().pixelSize() : 0;
  c.check(activeSize >= 28, "it is set at display size");
  // Emphasis is carried by scale, so no line ever re-shapes its text.
  if (auto lineItem = current->parentItem()) {
    c.check(qAbs(lineItem->scale() - 1) < 0.02, "the sung line is at full size");
    if (auto other = shownItem(singAlong, "singAlongLine"))
      if (auto otherLine = other->parentItem()) {
        c.check(otherLine->scale() < 0.95, "and its neighbours stand back by scale");
        c.check(qAbs(other->property("font").value<QFont>().pixelSize() - activeSize) < 0.01,
                "at the same font size, so changing line re-shapes nothing");
      }
  }
  // Even a four-line lyric brings its live line to where the eye is looking,
  // rather than leaving it stranded at the top of the view.
  if (current) {
    const double centre = current->mapToScene(current->boundingRect().center()).y();
    c.check(centre > w->height() * 0.25 && centre < w->height() * 0.6,
            QString("the sung line sits in the reading band (%1 of %2)")
                .arg(centre, 0, 'f', 0).arg(w->height()));
  }
  c.shot("01-singalong-first-line");

  // The fill is a real measurement, not decoration: it tracks playback.
  auto fill = anyItem(singAlong, "singAlongFill");
  c.check(fill, "the sung line carries a fill");
  const double halfway = fill ? fill->width() : 0;

  // And it sweeps between playback reports rather than stepping with them.
  // Playback reports itself four times a second; sampled faster than that, the
  // fill has to keep moving in between.
  b->seek(11000);
  c.check(c.until([&] { return b->lyricIndex() == 0 && b->playing(); }, 3000),
          "the first line is being sung");
  QTest::qWait(300);
  int advances = 0;
  double previous = fill ? fill->width() : 0;
  for (int i = 0; i < 40; ++i) {
    QTest::qWait(25);
    const double now = fill ? fill->width() : 0;
    if (now > previous + 0.05)
      ++advances;
    previous = now;
  }
  // Four reports a second over a second of sampling would give at most ~4
  // steps; a sweep moves on nearly every frame.
  c.check(advances >= 12,
          QString("the fill sweeps rather than stepping (%1 advances in 40 samples)").arg(advances));
  c.shot("02-singalong-line-filling");

  b->seek(19000);
  c.check(c.until([&] { return b->lyricProgress() > 0.85; }, 3000), "playback nears the line's end");
  QTest::qWait(300);
  auto laterFill = anyItem(singAlong, "singAlongFill");
  c.check(laterFill && laterFill->width() > halfway,
          "the fill advances with the music, it does not merely appear");

  // Pausing stops the sweep where it stands rather than letting it run on.
  b->pause();
  QTest::qWait(400);
  const double held = laterFill ? laterFill->width() : 0;
  QTest::qWait(500);
  c.check(laterFill && qAbs(laterFill->width() - held) < 1.0,
          "a paused song holds its fill still");
  b->play();
  c.check(c.until([&] { return b->playing(); }, 5000), "and playing resumes it");

  b->seek(35000);
  c.check(c.until([&] { return b->lyricIndex() == 2; }, 3000), "a later line takes over");
  QTest::qWait(600);
  auto moved = shownItem(w->contentItem(), "singAlongCurrent");
  c.check(moved && moved->property("text").toString() == "A quiet moment",
          "emphasis follows the music to the next line");
  c.shot("03-singalong-later-line");

  // --- Instrumental stretches say so instead of going blank ---
  b->seek(2000);
  c.check(c.until([&] { return b->lyricIndex() < 0; }, 3000), "playback returns before the words");
  QTest::qWait(400);
  auto waiting = shownItem(w->contentItem(), "singAlongWaiting");
  c.check(waiting && waiting->isVisible(), "the wait is acknowledged rather than left blank");
  auto cue = shownItem(w->contentItem(), "singAlongCue");
  c.check(cue && cue->property("text").toString().contains("Lyrics in"),
          "and counts down to the first line");
  // The words stay on screen through a gap, so the countdown has to sit clear
  // of them rather than across them.
  if (waiting) {
    const auto cueRect = waiting->mapRectToScene(waiting->boundingRect());
    int overlaps = 0;
    for (const auto *name : {"singAlongCurrent", "singAlongLine"})
      if (auto text = shownItem(singAlong, name)) {
        const auto lineRect = text->mapRectToScene(text->boundingRect());
        if (cueRect.intersects(lineRect))
          ++overlaps;
      }
    c.check(overlaps == 0, QString("the countdown does not sit over the words (%1 overlapping)")
                               .arg(overlaps));
    auto list = shownItem(w->contentItem(), "singAlongLines");
    c.check(list && list->mapRectToScene(list->boundingRect()).bottom() <= cueRect.top() + 1,
            "the words are given room above it rather than running under it");
  }
  c.shot("04-singalong-waiting");

  // --- A song with no timed lyrics cannot be sung along to, and says so ---
  b->next();
  c.check(c.until([&] { return b->currentIndex() == 1 && b->playing(); }, 20000),
          "the next song plays (now " + b->current().value("title").toString() + ")");
  c.check(c.until([&] { return b->lyricLines().isEmpty(); }, 8000), "it has no timed lyrics");
  QTest::qWait(500);
  c.check(player->property("displayedLayout") == "artwork",
          "sing along steps aside when a song cannot drive it");
  c.check(player->property("preferredLayout") == "singalong",
          "without forgetting that it was chosen");
  c.shot("05-singalong-fallback");

  b->previous();
  c.check(c.until([&] { return b->lyricLines().size() == 4; }, 20000), "returning restores the lyrics");
  c.check(c.until([&] { return player->property("displayedLayout") == "singalong"; }, 3000),
          "and sing along returns with them");

  // --- Reduced motion keeps it usable ---
  b->setMotion(false);
  b->seek(25000);
  c.check(c.until([&] { return b->lyricIndex() == 1; }, 3000), "the line changes without motion");
  QTest::qWait(400);
  auto still = shownItem(w->contentItem(), "singAlongCurrent");
  c.check(still && still->property("text").toString() == "Across the still water",
          "the right line is still emphasised");
  c.shot("06-singalong-reduced-motion");
  b->setMotion(true);

  w->setProperty("immersive", false);
  QTest::qWait(300);
  b->stop();
  b->clearQueue();
  c.finish();
}

void runCrossfadeUiTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1320, 860);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(false);
  b->setVolume(0);
  b->setWatchMusicFolders(false);
  b->setCrossfadeSeconds(0);
  b->setGapless(true);

  auto settings = c.dialog("settingsDialog");
  c.check(settings, "Settings opens");
  if (!settings)
    return c.finish();
  settings->setProperty("category", 1);
  QTest::qWait(400);
  c.check(shownItem(w->contentItem(), "crossfadeSetting"), "Playback offers crossfade");
  c.check(shownItem(w->contentItem(), "gaplessSwitch"), "and gapless playback");
  auto value = shownItem(w->contentItem(), "crossfadeValue");
  c.check(value && value->property("text").toString() == "Off",
          "it reads Off until it is asked for");
  c.shot("01-crossfade-off");

  auto slider = shownItem(w->contentItem(), "crossfadeSlider");
  c.check(slider, "the crossfade slider is on screen");
  if (slider) {
    c.check(qAbs(slider->property("from").toReal()) < 0.001 &&
                qAbs(slider->property("to").toReal() - 12) < 0.001,
            "it spans nothing to twelve seconds");
    slider->setProperty("value", 6);
    QMetaObject::invokeMethod(slider, "moved");
    c.check(c.until([&] { return b->crossfadeSeconds() == 6; }), "moving it sets the overlap");
    QTest::qWait(250);
    c.check(value && value->property("text").toString() == "6 s", "and it reads back in seconds");
    // Bring the control itself into view for the capture.
    settings->setProperty("searchQuery", "crossfade");
    QTest::qWait(400);
    c.check(shownItem(w->contentItem(), "crossfadeSlider"), "the control is reachable by name");
    c.shot("02-crossfade-six-seconds");
    settings->setProperty("searchQuery", "");
    QTest::qWait(300);
  }

  // The setting survives the dialog and the session.
  c.closeDialog(settings);
  QTest::qWait(300);
  c.check(b->crossfadeSeconds() == 6, "the overlap is remembered");
  b->setCrossfadeSeconds(0);
  c.check(b->crossfadeSeconds() == 0, "and can be turned off again");

  // Searching Settings finds it by what it does, not only by its name.
  settings = c.dialog("settingsDialog");
  if (settings) {
    settings->setProperty("searchQuery", "overlap");
    QTest::qWait(400);
    c.check(shownItem(w->contentItem(), "crossfadeSetting"), "searching for overlap finds it");
    settings->setProperty("searchQuery", "pause between songs");
    QTest::qWait(400);
    c.check(shownItem(w->contentItem(), "gaplessSwitch"), "and gapless by what it prevents");
    c.shot("03-crossfade-search");
    settings->setProperty("searchQuery", "");
    QTest::qWait(300);
    c.closeDialog(settings);
  }
  c.finish();
}

void runTrackDetailsTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1320, 900);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(false);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);
  b->setLyricsFallback(false);

  paintCover(c.directory + "/music/cover.png", QColor("#26324f"), QColor("#e2a03f"));
  // A lossless recording with a full set of tags, so every field has something
  // true to report, and a lossy one, where some of them genuinely do not apply.
  if (!encodeTrack(c, c.directory + "/music/01.flac", "Across the still water", "Still Water",
                   "Rill", 3,
                   {"-metadata", "genre=Ambient", "-metadata", "composer=A Composer",
                    "-metadata", "disc=2", "-metadata", "album_artist=Various Artists",
                    "-ac", "2", "-sample_fmt", "s16"}))
    return c.finish();
  if (!encodeTrack(c, c.directory + "/music/02.mp3", "No tags", "Still Water", "Rill", 4,
                   {"-ac", "1"}))
    return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the metadata fixture");
  b->library("files");
  b->collection()->setSortKey("title");
  QTest::qWait(200);
  c.check(c.until([&] { return b->results()->count() == 2; }), "both recordings imported");

  QVariantMap lossless, lossy;
  for (const auto &row : b->results()->rows) {
    const auto t = row.toMap();
    if (t.value("title") == "Across the still water")
      lossless = t;
    else if (t.value("title") == "No tags")
      lossy = t;
  }
  c.check(!lossless.isEmpty() && !lossy.isEmpty(), "both recordings are readable");
  if (lossless.isEmpty())
    return c.finish();

  // --- The tags really were read off the file ---
  c.check(lossless.value("genre").toString() == "Ambient", "genre is read from the file");
  c.check(lossless.value("composer").toString() == "A Composer", "so is the composer");
  c.check(lossless.value("albumArtist").toString() == "Various Artists", "and the album artist");
  c.check(lossless.value("channels").toInt() == 2, "and the channel count");
  c.check(lossless.value("bitDepth").toInt() == 16, "and the bit depth of a lossless recording");
  c.check(lossy.value("bitDepth").toInt() == 0,
          "a lossy recording reports no depth, because it has none");
  c.check(lossy.value("channels").toInt() == 1, "but still reports its channels");

  // --- And they reach the dialog ---
  const auto details = b->trackDetails(lossless);
  QStringList labels;
  QVariantMap byLabel;
  for (const auto &row : details) {
    const auto entry = row.toMap();
    labels << entry.value("label").toString();
    byLabel.insert(entry.value("label").toString(), entry.value("value"));
  }
  for (const auto &expected : {"Title", "Artist", "Album artist", "Album", "Composer", "Genre",
                               "Year", "Track", "Source", "Duration", "File bit depth",
                               "File channels", "File sample rate"})
    c.check(labels.contains(expected), QString("details list %1").arg(expected));
  c.check(byLabel.value("Genre").toString() == "Ambient", "Genre reads back what was tagged");
  c.check(byLabel.value("Track").toString() == "3 on disc 2",
          "a numbered track on a multi-disc album says which disc");
  c.check(byLabel.value("File channels").toString() == "Stereo", "two channels read as Stereo");
  c.check(byLabel.value("File bit depth").toString() == "16-bit", "depth reads in bits");
  c.check(byLabel.value("Album artist").toString() == "Various Artists",
          "the album artist is listed when it differs from the performer");

  // A performer who is also the album artist is not repeated.
  const auto plain = b->trackDetails(lossy);
  QStringList lossyLabels;
  for (const auto &row : plain)
    lossyLabels << row.toMap().value("label").toString();
  c.check(!lossyLabels.contains("Album artist"),
          "an album artist the same as the performer is not repeated");
  c.check(!lossyLabels.contains("File bit depth"),
          "a lossy recording is not given a bit depth it does not have");
  c.check(lossyLabels.contains("File channels"), "but its channels are still reported");
  c.check(!lossyLabels.contains("Genre"), "an untagged genre is left out rather than shown empty");

  // --- The dialog itself ---
  auto dialog = w->findChild<QObject *>("trackDetailsDialog");
  c.check(dialog, "the details dialog exists");
  if (dialog) {
    QMetaObject::invokeMethod(dialog, "inspect", Q_ARG(QVariant, QVariant(lossless)));
    QTest::qWait(500);
    c.check(dialog->property("visible").toBool(), "it opens on a song");
    c.shot("01-track-details-full");
    QMetaObject::invokeMethod(dialog, "close");
    QTest::qWait(300);
    QMetaObject::invokeMethod(dialog, "inspect", Q_ARG(QVariant, QVariant(lossy)));
    QTest::qWait(500);
    c.shot("02-track-details-sparse");
    QMetaObject::invokeMethod(dialog, "close");
    QTest::qWait(300);
  }
  c.finish();
}

void runQueueHistoryTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1400, 880);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(false);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);
  b->setLyricsFallback(false);
  b->setCrossfadeSeconds(0);
  b->clearHistory();

  paintCover(c.directory + "/music/cover.png", QColor("#26324f"), QColor("#e2a03f"));
  const QStringList titles{"The light arrives", "Across the still water", "A quiet moment",
                           "We move with the tide"};
  for (int i = 0; i < titles.size(); ++i)
    if (!encodeTrack(c, QString("%1/music/%2.flac").arg(c.directory).arg(i + 1), titles[i],
                     "Still Water", "Rill", i + 1))
      return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the history fixture");
  b->library("files");
  c.check(c.until([&] { return b->results()->count() == 4; }), "four songs are available");
  b->enqueueItems(b->results()->rows);

  // --- Nothing has been played, so there is nothing to look back over ---
  w->setProperty("side", "queue");
  QTest::qWait(500);
  c.check(shownItem(w->contentItem(), "queueTabs"), "the queue panel offers both views");
  c.check(w->property("queueTab") == "next", "it opens on what is coming");
  c.check(shownItem(w->contentItem(), "queueView"), "the queue is the one on screen");
  {
    auto title = shownItem(w->contentItem(), "sidePanelTitle");
    c.check(title && title->property("text").toString() == "Up next", "and the panel says so");
  }
  c.check(!shownItem(w->contentItem(), "recentlyPlayedView"), "the look-back is not");
  c.shot("01-queue-up-next");

  w->setProperty("queueTab", "history");
  QTest::qWait(400);
  c.check(shownItem(w->contentItem(), "recentlyPlayedView"), "History shows the look-back");
  c.check(!shownItem(w->contentItem(), "queueView"), "and stands the queue down");
  c.check(b->recentlyPlayed()->count() == 0, "which is empty before anything has played");
  c.shot("02-queue-history-empty");

  // --- Playing songs fills it, newest first, without the song playing now ---
  w->setProperty("queueTab", "next");
  b->playAt(0);
  c.check(c.until([&] { return b->playing(); }), "the first song plays");
  QTest::qWait(400);
  c.check(b->recentlyPlayed()->count() == 0,
          "the song playing now is not something to look back over");
  b->playAt(1);
  c.check(c.until([&] { return b->playing() && b->currentIndex() == 1; }), "a second song plays");
  b->playAt(2);
  c.check(c.until([&] { return b->playing() && b->currentIndex() == 2; }), "and a third");
  QTest::qWait(500);
  c.check(c.until([&] { return b->recentlyPlayed()->count() == 2; }),
          "the two that finished are there to look back over");
  // Compare against the queue rather than the fixture, since the library
  // decides its own order.
  const auto first = b->queue()->get(0).value("title").toString();
  const auto second = b->queue()->get(1).value("title").toString();
  c.check(b->recentlyPlayed()->get(0).value("title").toString() == second,
          "newest first, so the one just before this one is at the top");
  c.check(b->recentlyPlayed()->get(1).value("title").toString() == first,
          "and the one before that next");

  w->setProperty("queueTab", "history");
  QTest::qWait(500);
  auto list = shownItem(w->contentItem(), "recentlyPlayedView");
  c.check(list && list->property("count").toInt() == 2, "the panel lists both of them");
  auto count = shownItem(w->contentItem(), "recentlyPlayedCount");
  c.check(count && count->property("text").toString().contains("2"), "and says how many");
  auto title = shownItem(w->contentItem(), "sidePanelTitle");
  c.check(title && title->property("text").toString() == "Recently played",
          "the panel says which of the two it is showing");
  c.check(!shownItem(w->contentItem(), "revealPlayingButton"),
          "and drops the shortcut that only makes sense for the queue");
  c.shot("03-queue-history-filled");

  // --- Playing from it keeps the queue, the way the history page does ---
  QStringList queuedBefore;
  for (int i = 0; i < b->queue()->count(); ++i)
    queuedBefore << b->queue()->get(i).value("id").toString();
  const auto wanted = b->recentlyPlayed()->get(0);
  c.clickWithin(list, "trackRow_0");
  c.check(c.until([&] { return b->current().value("id") == wanted.value("id"); }, 8000),
          "choosing one plays it");
  // The queue is kept rather than replaced: the chosen song is slotted in to
  // play next, and nothing that was queued is lost.
  QStringList queuedAfter;
  for (int i = 0; i < b->queue()->count(); ++i)
    queuedAfter << b->queue()->get(i).value("id").toString();
  for (const auto &id : queuedBefore)
    c.check(queuedAfter.contains(id), "the queue keeps everything it already held");
  c.check(queuedAfter.contains(wanted.value("id").toString()),
          "and the chosen song joins it rather than replacing it");
  c.shot("04-queue-history-played");

  // The song now playing left the look-back when it started.
  QTest::qWait(400);
  for (int i = 0; i < b->recentlyPlayed()->count(); ++i)
    c.check(b->recentlyPlayed()->get(i).value("id") != b->current().value("id"),
            "what is playing is never also in the look-back");

  // --- It links onward to the full history page ---
  c.click("openFullHistory");
  c.check(c.until([&] { return b->libraryId() == "history"; }, 4000),
          "the panel opens the full history page");
  c.shot("05-full-history");

  // --- Clearing history empties it, and Undo brings it back ---
  const int before = b->recentlyPlayed()->count();
  c.check(before > 0, "there is something to clear");
  b->clearHistory();
  QTest::qWait(300);
  c.check(b->recentlyPlayed()->count() == 0, "clearing history empties the look-back too");
  b->undo();
  QTest::qWait(300);
  c.check(b->recentlyPlayed()->count() == before, "and Undo restores it");

  w->setProperty("queueTab", "next");
  w->setProperty("side", "");
  b->stop();
  b->clearQueue();
  c.finish();
}

void runListeningStatsTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1320, 900);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(false);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);
  b->setLyricsFallback(false);
  b->setCrossfadeSeconds(0);
  b->clearListeningStats();

  // --- Nothing has been played ---
  const auto empty = b->listeningStats(7);
  c.check(empty.value("plays").toInt() == 0 && empty.value("seconds").toLongLong() == 0,
          "an unused library has listened to nothing");
  c.check(empty.value("topArtists").toList().isEmpty(), "and has no favourites yet");

  paintCover(c.directory + "/music/cover.png", QColor("#26324f"), QColor("#e2a03f"));
  // Two artists with different amounts of music, so the ranking has to think.
  const QList<QPair<QString, QString>> fixtures{{"Harbour lights", "Marble Coast"},
                                                {"Night ferry", "Marble Coast"},
                                                {"The light arrives", "Rill"}};
  for (int i = 0; i < fixtures.size(); ++i)
    if (!encodeTrack(c, QString("%1/music/%2.flac").arg(c.directory).arg(i + 1),
                     fixtures[i].first, "Still Water", fixtures[i].second, i + 1))
      return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the statistics fixture");
  b->library("files");
  c.check(c.until([&] { return b->results()->count() == 3; }), "three songs are available");
  b->enqueueItems(b->results()->rows);

  // --- Playing is counted, and counted per play rather than per song ---
  QHash<QString, int> expectedPlays;
  const auto playIndex = [&](int index) {
    b->playAt(index);
    c.check(c.until([&] { return b->playing() && b->currentIndex() == index; }, 10000),
            QString("song %1 plays").arg(index));
    QTest::qWait(250);
    expectedPlays[b->current().value("artist").toString()] += 1;
  };
  // Marble Coast twice over, Rill once.
  playIndex(0);
  playIndex(1);
  playIndex(2);
  playIndex(0);
  QTest::qWait(400);

  const auto week = b->listeningStats(7);
  c.check(week.value("plays").toInt() == 4, "every play is counted, not every song");
  c.check(week.value("songs").toInt() == 3, "and the distinct songs are counted separately");
  c.check(week.value("artists").toInt() == 2, "along with the artists behind them");
  const qint64 expectedSeconds = 4 * 150;
  c.check(week.value("seconds").toLongLong() == expectedSeconds,
          QString("the time listened adds up (%1, expected %2)")
              .arg(week.value("seconds").toLongLong()).arg(expectedSeconds));

  // What was actually played: queue rows 0, 1, 2 and 0 again. The library
  // decides its own order, so the expectations come from the queue.
  const auto twice = b->queue()->get(0);
  QHash<QString, int> playsByArtist;
  for (int row : {0, 1, 2, 0})
    playsByArtist[b->queue()->get(row).value("artist").toString()] += 1;
  const auto topArtists = week.value("topArtists").toList();
  c.check(topArtists.size() == playsByArtist.size(), "every artist played is ranked");
  for (const auto &entry : topArtists) {
    const auto artist = entry.toMap();
    const int expected = playsByArtist.value(artist.value("name").toString());
    c.check(artist.value("plays").toInt() == expected,
            QString("%1 is credited with %2 plays, expected %3")
                .arg(artist.value("name").toString()).arg(artist.value("plays").toInt()).arg(expected));
  }
  for (int i = 1; i < topArtists.size(); ++i)
    c.check(topArtists[i - 1].toMap().value("seconds").toLongLong() >=
                topArtists[i].toMap().value("seconds").toLongLong(),
            "the ranking runs from most time listened to least");
  const auto topSongs = week.value("topSongs").toList();
  c.check(!topSongs.isEmpty() &&
              topSongs[0].toMap().value("name").toString() == twice.value("title").toString(),
          QString("the song played twice leads the songs (got %1, expected %2)")
              .arg(topSongs.isEmpty() ? QString() : topSongs[0].toMap().value("name").toString(),
                   twice.value("title").toString()));
  c.check(!topSongs.isEmpty() && topSongs[0].toMap().value("plays").toInt() == 2,
          "and its count is right");

  // --- The period really narrows things ---
  const auto allTime = b->listeningStats(0);
  c.check(allTime.value("plays").toInt() == 4, "all time sees the same plays here");
  c.check(allTime.value("daily").toList().isEmpty(),
          "all time has no day-by-day shape to show");
  const auto week2 = b->listeningStats(7);
  c.check(week2.value("daily").toList().size() == 7, "a week is shown as seven days");
  qint64 dailyTotal = 0;
  for (const auto &row : week2.value("daily").toList())
    dailyTotal += row.toMap().value("seconds").toLongLong();
  c.check(dailyTotal == expectedSeconds, "the days add up to the period");

  // --- A private session records nothing ---
  const int before = b->listeningStats(0).value("plays").toInt();
  b->setHistoryPaused(true);
  playIndex(1);
  QTest::qWait(400);
  c.check(b->listeningStats(0).value("plays").toInt() == before,
          "a paused history records no statistics either");
  b->setHistoryPaused(false);

  // --- The dialog ---
  auto stats = c.dialog("listeningStatsDialog");
  c.check(stats, "the statistics dialog opens");
  if (!stats)
    return c.finish();
  auto time = shownItem(w->contentItem(), "statsTimeValue");
  c.check(time && time->property("text").toString() == "10 min",
          QString("the headline reads in minutes (%1)")
              .arg(time ? time->property("text").toString() : QString()));
  auto plays = shownItem(w->contentItem(), "statsPlaysValue");
  c.check(plays && plays->property("text").toString() == "4", "and the play count is shown");
  c.check(shownItem(w->contentItem(), "statsDaily"), "the week has a shape");
  auto bars = shownItem(w->contentItem(), "statsDailyBars");
  c.check(bars && bars->height() >= 56,
          QString("the day bars keep their height (%1px)").arg(bars ? bars->height() : 0));
  auto figure = shownItem(w->contentItem(), "statsTime");
  c.check(figure && figure->height() >= 72,
          QString("and the headline figures keep theirs (%1px)").arg(figure ? figure->height() : 0));
  auto list = shownItem(w->contentItem(), "statsRankingList");
  c.check(list && list->property("count").toInt() == 2, "the artists are ranked on screen");
  // A count is not the same as being on screen: the figures above must not
  // squeeze the ranking out of the dialog.
  c.check(list && list->height() > 100,
          QString("the ranking has room to be read (%1px)").arg(list ? list->height() : 0));
  auto row = list ? anyItem(list, "statsRow_0") : nullptr;
  c.check(row && row->width() > 200 && row->height() > 30, "and its rows have real size");
  if (row) {
    const auto centre = row->mapToScene(row->boundingRect().center());
    c.check(centre.y() > 0 && centre.y() < w->height() && centre.x() > 0 && centre.x() < w->width(),
            "and sit inside the window");
  }
  c.shot("01-listening-stats-artists");

  // Switching the ranking switches what is listed.
  stats->setProperty("ranking", "songs");
  QTest::qWait(400);
  c.check(list && list->property("count").toInt() == 3, "songs rank separately");
  c.shot("02-listening-stats-songs");

  // Switching the period re-reads the numbers.
  stats->setProperty("days", 0);
  QMetaObject::invokeMethod(stats, "refresh");
  QTest::qWait(400);
  c.check(!shownItem(w->contentItem(), "statsDaily"), "all time drops the day-by-day row");
  c.shot("03-listening-stats-all-time");

  // --- Clearing it empties it ---
  b->clearListeningStats();
  QTest::qWait(400);
  c.check(b->listeningStats(0).value("plays").toInt() == 0, "clearing removes every play");
  c.check(shownItem(w->contentItem(), "statsEmpty"), "and the dialog says so plainly");
  c.shot("04-listening-stats-cleared");
  c.closeDialog(stats);

  b->stop();
  b->clearQueue();
  c.finish();
}

void runPlaylistVersionsTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1320, 900);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(false);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);

  paintCover(c.directory + "/music/cover.png", QColor("#26324f"), QColor("#e2a03f"));
  for (int i = 1; i <= 4; ++i)
    if (!encodeTrack(c, QString("%1/music/%2.flac").arg(c.directory).arg(i),
                     QString("Track %1").arg(i), "Versions", "Rill", i))
      return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the versions fixture");
  b->library("files");
  c.check(c.until([&] { return b->results()->count() == 4; }), "four songs are available");
  const auto songs = b->results()->rows;

  // --- A new playlist has no history ---
  const auto playlist = b->createPlaylist("Evening drive");
  c.check(b->playlistVersions(playlist).isEmpty(), "a new playlist has no earlier versions");

  // --- Each edit puts the version it replaced aside ---
  b->addItemsToPlaylist(playlist, {songs[0], songs[1]});
  QTest::qWait(150);
  auto versions = b->playlistVersions(playlist);
  c.check(versions.size() == 1, "the first edit keeps the empty version it replaced");
  c.check(versions[0].toMap().value("count").toInt() == 0, "which held nothing");

  b->addItemsToPlaylist(playlist, {songs[2]});
  QTest::qWait(150);
  versions = b->playlistVersions(playlist);
  c.check(versions.size() == 2, "a second edit keeps a second version");
  c.check(versions[0].toMap().value("count").toInt() == 2,
          "newest first, so the two-song version is at the top");
  c.check(versions[0].toMap().value("summary").toString() == "2 songs", "and says so in words");
  c.check(versions[1].toMap().value("count").toInt() == 0, "with the empty one below it");

  b->openPlaylist(playlist);
  c.check(c.until([&] { return b->results()->count() == 3; }), "the playlist holds three songs");
  b->removePlaylistRows(playlist, {0});
  QTest::qWait(150);
  c.check(b->playlistVersions(playlist).size() == 3, "removing a song keeps a version too");
  c.check(b->playlistVersions(playlist)[0].toMap().value("count").toInt() == 3,
          "the version replaced held three");

  // An edit that changes nothing is not a version.
  const int before = b->playlistVersions(playlist).size();
  b->addItemsToPlaylist(playlist, {songs[1]});
  QTest::qWait(150);
  c.check(b->playlistVersions(playlist).size() == before,
          "adding a song that is already there keeps no new version");

  // --- Restoring brings a shape back, including a song removed since ---
  b->openPlaylist(playlist);
  QTest::qWait(200);
  const int current = b->results()->count();
  c.check(b->restorePlaylistVersion(playlist, 0), "the newest version can be restored");
  c.check(c.until([&] { return b->results()->count() == 3; }, 4000),
          QString("restoring brings back the three songs (was %1)").arg(current));
  QStringList restored;
  for (const auto &row : b->results()->rows)
    restored << row.toMap().value("id").toString();
  c.check(restored.contains(songs[0].toMap().value("id").toString()),
          "including the song that had been removed");

  // Restoring is itself an edit, so it is in the history and can be undone.
  c.check(b->playlistVersions(playlist).size() == before + 1,
          "the restore kept the version it replaced");
  b->undo();
  QTest::qWait(250);
  c.check(b->results()->count() == 2, "and ordinary Undo takes the restore back");

  // Restoring what is already there changes nothing.
  b->openPlaylist(playlist);
  QTest::qWait(200);
  const int versionsNow = b->playlistVersions(playlist).size();
  const auto same = b->results()->rows;
  b->restorePlaylistVersion(playlist, 0);
  QTest::qWait(200);
  c.check(b->playlistVersions(playlist).size() >= versionsNow, "history is never lost by a restore");

  // --- Bounded, so history cannot grow without limit ---
  for (int i = 0; i < 20; ++i) {
    b->removePlaylistRows(playlist, {0});
    b->addItemsToPlaylist(playlist, {songs[i % 4]});
    QTest::qWait(20);
  }
  c.check(b->playlistVersions(playlist).size() <= 12,
          QString("history is bounded (%1 versions)").arg(b->playlistVersions(playlist).size()));

  // --- The dialog ---
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("playlists")));
  QTest::qWait(400);
  auto dialog = w->findChild<QObject *>("playlistVersionsDialog");
  c.check(dialog, "the version history dialog exists");
  if (dialog) {
    QMetaObject::invokeMethod(dialog, "inspect", Q_ARG(QVariant, QVariant(playlist)),
                              Q_ARG(QVariant, QVariant("Evening drive")));
    QTest::qWait(500);
    c.check(dialog->property("visible").toBool(), "it opens on a playlist");
    auto list = shownItem(w->contentItem(), "playlistVersionsList");
    c.check(list && list->property("count").toInt() > 0, "and lists its versions");
    c.check(list && list->height() > 100, "with room to read them");
    auto when = shownItem(w->contentItem(), "playlistVersionWhen_0");
    c.check(when && when->property("text").toString().startsWith("Today"),
            QString("a version made just now is dated today (%1)")
                .arg(when ? when->property("text").toString() : QString()));
    c.shot("01-playlist-versions");

    // Restoring from the dialog changes the playlist.
    b->openPlaylist(playlist);
    QTest::qWait(300);
    const int shown = b->results()->count();
    const int wanted = b->playlistVersions(playlist)[0].toMap().value("count").toInt();
    c.clickWithin(list, "restoreVersion_0");
    c.check(c.until([&] { return b->results()->count() == wanted; }, 4000),
            QString("restoring from the dialog reshapes the playlist (%1 to %2)")
                .arg(shown).arg(wanted));
    c.shot("02-playlist-versions-restored");

    // Forgetting empties it.
    c.click("clearPlaylistVersions");
    c.check(c.until([&] { return b->playlistVersions(playlist).isEmpty(); }, 3000),
            "versions can be forgotten");
    c.check(shownItem(w->contentItem(), "playlistVersionsEmpty"), "and the dialog says so");
    c.shot("03-playlist-versions-empty");
    QMetaObject::invokeMethod(dialog, "close");
    QTest::qWait(300);
  }

  // --- A deleted playlist takes its history with it ---
  b->addItemsToPlaylist(playlist, {songs[3]});
  QTest::qWait(150);
  c.check(!b->playlistVersions(playlist).isEmpty(), "history starts again after an edit");
  b->deletePlaylist(playlist);
  QTest::qWait(200);
  c.check(b->playlistVersions(playlist).isEmpty(), "deleting the playlist forgets its versions");

  c.finish();
}

void runWindowWashTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1400, 880);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(false);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);
  b->setLyricsFallback(false);
  b->setArtworkAccent(false);
  b->setAccentColor("");
  b->setAmbientBackdrop(true);

  paintCover(c.directory + "/music/cover.png", QColor("#1b4f8a"), QColor("#e8622a"));
  for (int i = 1; i <= 3; ++i)
    if (!encodeTrack(c, QString("%1/music/%2.flac").arg(c.directory).arg(i),
                     QString("Track %1").arg(i), "Wash", "Rill", i))
      return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the wash fixture");
  b->library("files");
  c.check(c.until([&] { return b->results()->count() == 3; }), "the fixture is in the library");

  auto wash = anyItem(w->contentItem(), "windowBackdrop");
  c.check(wash, "the window carries a wash of its own");
  if (!wash)
    return c.finish();

  // --- It spans the window, not one panel of it ---
  c.check(qAbs(wash->width() - w->width()) < 1 && qAbs(wash->height() - w->height()) < 1,
          QString("the wash covers the whole window (%1x%2 of %3x%4)")
              .arg(wash->width()).arg(wash->height()).arg(w->width()).arg(w->height()));
  auto content = anyItem(w->contentItem(), "contentBody");
  c.check(content && wash->width() > content->width() + 40,
          "which is wider than the panel that used to carry it alone");

  // --- Nothing playing and nothing to borrow: no wash at all ---
  b->home();
  c.check(c.until([&] { return !b->busy(); }), "Home loads");
  QTest::qWait(400);
  c.check(w->property("windowArtwork").toString().isEmpty(), "there is no cover to wash with yet");
  c.check(!wash->property("active").toBool(), "so the window is left alone");
  c.check(qAbs(w->property("washAlpha").toReal() - 1) < 0.001,
          "and the surfaces stay opaque");
  c.shot("01-no-wash");

  // --- Playing something washes the window ---
  b->library("files");
  b->enqueueItems(b->results()->rows);
  b->playAt(0);
  c.check(c.until([&] { return b->playing(); }), "the fixture plays");
  c.check(c.until([&] { return wash->property("active").toBool(); }, 4000),
          "the playing cover washes the window");
  c.check(w->property("windowWashed").toBool(), "the window reports itself washed");
  c.check(w->property("washAlpha").toReal() < 1,
          "and the surfaces let it through rather than covering it");
  auto art = anyItem(wash, "ambientArt");
  c.check(art && art->property("source").toUrl() == QUrl(b->current().value("art").toString()),
          "the wash is taken from the cover that is playing");
  c.check(wash->property("drifts").toBool(), "a full-bleed wash drifts, having no corners to keep");
  c.shot("02-window-washed");

  // --- The wash reaches every part of the window, not just the middle ---
  // Sampled from the rendered window: the rail gutter, the player bar and the
  // panel all have to differ from the same scene with the wash turned off.
  const auto washed = w->grabWindow();
  b->setAmbientBackdrop(false);
  QTest::qWait(500);
  c.check(!wash->property("active").toBool(), "turning it off releases the wash");
  c.check(qAbs(w->property("washAlpha").toReal() - 1) < 0.001, "and the surfaces close up again");
  const auto plain = w->grabWindow();
  c.shot("03-wash-off");
  c.check(washed.size() == plain.size(), "both renders are the same size");
  if (washed.size() == plain.size()) {
    // Averaged over a small block, because a single pixel can coincide by
    // chance where the wash happens to be dark.
    const auto average = [](const QImage &image, int x, int y) {
      double r = 0, g = 0, bl = 0;
      int seen = 0;
      for (int dy = -10; dy <= 10; ++dy)
        for (int dx = -10; dx <= 10; ++dx) {
          const int sx = qBound(0, x + dx, image.width() - 1);
          const int sy = qBound(0, y + dy, image.height() - 1);
          const auto pixel = image.pixelColor(sx, sy);
          r += pixel.redF();
          g += pixel.greenF();
          bl += pixel.blueF();
          ++seen;
        }
      return QColor::fromRgbF(r / seen, g / seen, bl / seen);
    };
    struct Spot { const char *where; double x, y; };
    for (const auto &spot : {Spot{"the navigation rail", 0.03, 0.45},
                             Spot{"the player bar", 0.5, 0.94},
                             Spot{"the list behind the songs", 0.6, 0.6},
                             Spot{"the gutter above the content", 0.06, 0.03}}) {
      const int x = int(washed.width() * spot.x), y = int(washed.height() * spot.y);
      const auto a = average(washed, x, y), z = average(plain, x, y);
      const double difference = qAbs(a.redF() - z.redF()) + qAbs(a.greenF() - z.greenF()) +
                                qAbs(a.blueF() - z.blueF());
      c.check(difference > 0.004, QString("the wash reaches %1 (difference %2)")
                                      .arg(spot.where).arg(difference, 0, 'f', 4));
    }
  }
  b->setAmbientBackdrop(true);
  QTest::qWait(400);

  // --- Contrast survives it ---
  // Body text is drawn over these surfaces, so the composited surface is what
  // has to clear the floor, not the colour the theme nominally asks for.
  const auto lit = w->grabWindow();
  const auto text = c.themeColor("text");
  struct Surface { const char *where; double x, y; };
  for (const auto &surface : {Surface{"the song list", 0.62, 0.62},
                              Surface{"the player bar", 0.42, 0.93},
                              Surface{"the window behind the rail", 0.03, 0.5}}) {
    const auto sampled = lit.pixelColor(int(lit.width() * surface.x), int(lit.height() * surface.y));
    const double ratio = contrastOf(text, sampled);
    c.check(ratio >= 4.5, QString("body text keeps %1:1 over %2")
                              .arg(ratio, 0, 'f', 2).arg(surface.where));
  }
  c.shot("04-washed-contrast");

  // --- Immersive and the mini player carry their own treatment ---
  w->setProperty("immersive", true);
  QTest::qWait(500);
  c.check(!w->property("windowWashed").toBool(),
          "the immersive player is its own surface and is not washed twice");
  w->setProperty("immersive", false);
  QTest::qWait(400);
  c.check(c.until([&] { return w->property("windowWashed").toBool(); }, 3000),
          "leaving it restores the wash");

  b->stop();
  b->clearQueue();
  c.finish();
}

// --- Material foundations ----------------------------------------------------
// Shape, motion and typography are systems rather than features, so they are
// checked structurally: not "does this one corner look right" but "does every
// corner in the window come from the scale".

namespace {
// Every step of Material's corner radius scale.
bool onShapeScale(double radius, double width, double height) {
  for (double step : {0.0, 4.0, 8.0, 12.0, 16.0, 20.0, 28.0, 32.0, 48.0})
    if (qAbs(radius - step) < 0.01)
      return true;
  // `full` is a real half rounding rather than a large fixed number, measured
  // across the shorter side, which is what makes a bar read as a pill whether
  // it lies flat or stands upright.
  const double shorter = qMin(width, height);
  return shorter > 0 && qAbs(radius - shorter / 2) < 0.51;
}
struct Rounded { QString name; double radius, width, height; };
void collectRadii(QQuickItem *root, QList<Rounded> &out) {
  if (root->isVisible()) {
    const auto radius = root->property("radius");
    if (radius.isValid() && radius.canConvert<double>() && radius.toDouble() > 0)
      out.append({root->objectName(), radius.toDouble(), root->width(), root->height()});
  }
  for (auto child : root->childItems())
    collectRadii(child, out);
}
} // namespace

void runMaterialFoundationTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1400, 900);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(true);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);

  paintCover(c.directory + "/music/cover.png", QColor("#1f4f6b"), QColor("#d98324"));
  for (int i = 1; i <= 3; ++i)
    if (!encodeTrack(c, QString("%1/music/%2.flac").arg(c.directory).arg(i),
                     QString("Track %1").arg(i), "Foundations", "Rill", i))
      return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the foundations fixture");
  b->library("files");
  c.check(c.until([&] { return b->results()->count() == 3; }), "the fixture is listed");

  // --- Shape: the scale, and nothing but the scale ---
  const QList<QPair<QString, double>> scale{
      {"shapeNone", 0},      {"shapeExtraSmall", 4},  {"shapeSmall", 8},
      {"shapeMedium", 12},   {"shapeLarge", 16},      {"shapeLargeIncreased", 20},
      {"shapeExtraLarge", 28}, {"shapeExtraLargeIncreased", 32}, {"shapeExtraExtraLarge", 48}};
  for (const auto &step : scale)
    c.check(qAbs(c.evaluate("Theme." + step.first).toDouble() - step.second) < 0.01,
            QString("%1 is %2dp, as Material specifies").arg(step.first).arg(step.second));
  c.check(qAbs(c.evaluate("Theme.shapeFull(48)").toDouble() - 24) < 0.01,
          "full rounding is half the height, not a large fixed number");
  // Material's optical roundness: a nested shape subtracts the padding.
  c.check(qAbs(c.evaluate("Theme.shapeInside(48,14)").toDouble() - 34) < 0.01,
          "nested shapes subtract their padding rather than sharing a radius");

  QList<Rounded> radii;
  collectRadii(w->contentItem(), radii);
  c.check(radii.size() > 25,
          QString("the window has rounded shapes to check (%1)").arg(radii.size()));
  QStringList offScale;
  for (const auto &entry : radii)
    if (!onShapeScale(entry.radius, entry.width, entry.height))
      offScale << QString("%1 r=%2 %3x%4")
                      .arg(entry.name.isEmpty() ? QString("(unnamed)") : entry.name)
                      .arg(entry.radius, 0, 'f', 1)
                      .arg(entry.width, 0, 'f', 1)
                      .arg(entry.height, 0, 'f', 1);
  c.check(offScale.isEmpty(),
          QString("every corner on screen comes from the scale%1")
              .arg(offScale.isEmpty() ? QString() : ", but " + offScale.mid(0, 6).join("; ")));
  c.shot("01-shape-scale");

  // --- Motion: the published spring conversions, and a real overshoot ---
  c.check(b->motionScheme() == "expressive",
          "Material recommends the expressive scheme, so it is the default");
  const auto curve = [&c](const QString &token) { return c.evaluate("Theme." + token).toList(); };
  const auto spatial = curve("springSpatial");
  c.check(spatial.size() >= 4 && qAbs(spatial[1].toDouble() - 1.21) < 0.001,
          "the expressive spatial spring is the published conversion");
  c.check(spatial.size() >= 4 && spatial[1].toDouble() > 1.0,
          "which overshoots its target, because spatial springs bounce");
  for (const auto &effects : {"springFastEffects", "springEffects", "springSlowEffects"}) {
    const auto points = curve(effects);
    c.check(points.size() >= 4 && points[1].toDouble() <= 1.0 && points[3].toDouble() <= 1.0,
            QString("%1 never overshoots, because colour and opacity must not").arg(effects));
  }
  c.check(c.evaluate("Theme.springSpatialMs").toInt() == 500 &&
              c.evaluate("Theme.springFastEffectsMs").toInt() == 150,
          "the spring durations are the published ones");

  // Switching the scheme reaches the tokens, and the standard scheme settles
  // rather than bouncing.
  b->setMotionScheme("standard");
  QTest::qWait(200);
  const auto settled = curve("springSpatial");
  c.check(settled.size() >= 4 && qAbs(settled[1].toDouble() - 1.06) < 0.001,
          "the standard scheme swaps in its own spatial spring");
  c.check(settled[1].toDouble() < spatial[1].toDouble(),
          "which overshoots less than the expressive one");
  b->setMotionScheme("expressive");
  QTest::qWait(200);

  // The tokens are not decoration: a real animated property has to overshoot.
  auto rail = shownItem(w->contentItem(), "navigationRail");
  c.check(rail, "the navigation rail is on screen to measure");
  if (rail) {
    const auto expand = [&](const QString &scheme) {
      b->setMotionScheme(scheme);
      c.evaluate("railSettings.expanded=false");
      c.until([&] { return qAbs(rail->width() - 88) < 1; }, 2000);
      QTest::qWait(200);
      c.evaluate("railSettings.expanded=true");
      double widest = 0;
      QElapsedTimer timer;
      timer.start();
      while (timer.elapsed() < 1200) {
        widest = qMax(widest, rail->width());
        QTest::qWait(8);
      }
      return widest;
    };
    const double bouncy = expand("expressive");
    const double flat = expand("standard");
    c.check(bouncy > 220.5,
            QString("the expressive scheme overshoots the rail's 220dp (reached %1)")
                .arg(bouncy, 0, 'f', 1));
    c.check(bouncy > flat,
            QString("further than the standard scheme does (%1 against %2)")
                .arg(bouncy, 0, 'f', 1).arg(flat, 0, 'f', 1));
    b->setMotionScheme("expressive");
    c.evaluate("railSettings.expanded=false");
    QTest::qWait(400);
  }

  // --- Typography: emphasis on the font's own axes ---
  c.check(c.evaluate("Theme.emphasizedWidth").toInt() > c.evaluate("Theme.regularWidth").toInt(),
          "emphasis widens the variable font rather than only thickening it");
  auto title = shownItem(w->contentItem(), "collectionHeaderTitle");
  c.check(title && title->property("emphasized").toBool(),
          "the page headline uses the emphasized style");
  if (title) {
    const auto axes = title->property("font").value<QFont>().variableAxisValue(
        QFont::Tag("wdth"));
    c.check(qAbs(axes - c.evaluate("Theme.emphasizedWidth").toDouble()) < 0.01,
            QString("and the width axis is really set (%1)").arg(axes));
    c.check(title->property("font").value<QFont>().weight() >= QFont::DemiBold,
            "at the emphasized weight");
  }
  c.shot("02-emphasized-type");

  b->stop();
  b->clearQueue();
  c.finish();
}

void runMaterialComponentTests(Backend *b, QQuickWindow *w) {
  Check c{b, w, qEnvironmentVariable("SUNG_TEST_OUTPUT")};
  QDir().mkpath(c.directory + "/music");
  QWindowSystemInterface::handleFocusWindowChanged(w);
  w->resize(1400, 900);
  QTest::qWait(500);
  b->setTheme("dark");
  b->setMotion(true);
  b->setVolume(0);
  b->setAutoplay(false);
  b->setPrepareNext(false);
  b->setWatchMusicFolders(false);
  b->setOnlineArtwork(false);

  paintCover(c.directory + "/music/cover.png", QColor("#1f4f6b"), QColor("#d98324"));
  for (int i = 1; i <= 6; ++i)
    if (!encodeTrack(c, QString("%1/music/%2.flac").arg(c.directory).arg(i),
                     QString("Track %1").arg(i), "Components", "Rill", i))
      return c.finish();
  b->importMusicFolder(QUrl::fromLocalFile(c.directory + "/music"));
  c.check(c.until([&] { return !b->importingLocal(); }, 40000), "import the component fixture");
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("files")));
  c.check(c.until([&] { return b->results()->count() == 6; }), "the library is listed");
  QTest::qWait(500);

  // --- Split button: one control, two targets, asymmetric corners ---
  auto split = shownItem(w->contentItem(), "collectionPlay");
  c.check(split, "the collection action is a split button");
  auto action = split ? shownItem(split, "splitButtonAction") : nullptr;
  auto reveal = split ? shownItem(split, "splitButtonMenu") : nullptr;
  c.check(action && reveal, "made of a common button and a menu button");
  if (action && reveal) {
    // Material's inner corners: the facing edges are a small step, the outer
    // ones full, which is what makes two targets read as one control.
    const double actionOuter = action->property("background").value<QQuickItem *>()
                                   ->property("topLeftRadius").toDouble();
    const double actionInner = action->property("background").value<QQuickItem *>()
                                   ->property("topRightRadius").toDouble();
    const double revealInner = reveal->property("background").value<QQuickItem *>()
                                   ->property("topLeftRadius").toDouble();
    const double revealOuter = reveal->property("background").value<QQuickItem *>()
                                   ->property("topRightRadius").toDouble();
    c.check(qAbs(actionInner - revealInner) < 0.01,
            "the facing corners match each other across the seam");
    c.check(actionInner < actionOuter && revealInner < revealOuter,
            "and are smaller than the outer corners");
    c.check(qAbs(actionOuter - 24) < 0.01 && qAbs(revealOuter - 24) < 0.01,
            "the outer corners are full for a 48dp control");
    c.check(qAbs(reveal->x() - (action->x() + action->width())) < 4,
            "the halves sit together rather than apart");
    // The two halves do different things.
    c.check(b->queue()->count() == 0, "nothing is queued yet");
    c.click("splitButtonAction");
    c.check(c.until([&] { return b->queue()->count() == 6 && b->playing(); }, 8000),
            "the action half plays the collection");
    b->stop();
    b->clearQueue();
    QTest::qWait(300);
    const double resting = reveal->property("background").value<QQuickItem *>()
                               ->property("topRightRadius").toDouble();
    c.click("splitButtonMenu");
    c.check(c.until([&] { return split->property("menuOpen").toBool(); }, 3000),
            "the menu half opens a menu");
    c.check(c.until([&] {
      return reveal->property("background").value<QQuickItem *>()
                 ->property("topRightRadius").toDouble() < resting - 2;
    }, 2000), "and morphs its shape while it is open, as Material asks");
    c.shot("01-split-button-open");
    auto shuffle = shownItem(w->contentItem(), "collectionShuffle");
    c.check(shuffle, "the menu offers the related ways of starting the same songs");
    QTest::keyClick(w, Qt::Key_Escape);
    c.check(c.until([&] { return !split->property("menuOpen").toBool(); }, 3000),
            "closing it releases the morph");
  }

  // --- FAB menu: two to six related actions, and a morph into its own close ---
  auto fab = shownItem(w->contentItem(), "fab");
  auto fabMenu = anyItem(w->contentItem(), "libraryFab");
  c.check(fab && fabMenu, "the library carries a floating action button");
  if (fab && fabMenu) {
    const int actions = fabMenu->property("count").toInt();
    c.check(actions >= 2 && actions <= 6,
            QString("it opens between two and six related actions (%1)").arg(actions));
    auto shape = shownItem(fab, "fabShape");
    const double rested = shape ? shape->property("radius").toDouble() : 0;
    c.check(!shownItem(w->contentItem(), "fabMenuItem_0"), "which are closed to begin with");
    c.click("fab");
    c.check(c.until([&] { return fabMenu->property("open").toBool(); }, 3000), "tapping opens them");
    c.check(c.until([&] { return shownItem(w->contentItem(), "fabMenuItem_0") != nullptr; }, 3000),
            "the actions appear");
    c.check(c.until([&] { return shape && shape->property("radius").toDouble() > rested + 2; }, 2000),
            "and the button morphs into the menu's close button");
    c.shot("02-fab-menu-open");
    c.click("fab");
    c.check(c.until([&] { return !fabMenu->property("open").toBool(); }, 3000), "tapping again closes them");
    c.check(c.until([&] { return shape && qAbs(shape->property("radius").toDouble() - rested) < 1; }, 2000),
            "and the shape comes back");
  }

  // --- Carousel: items change size across the viewport, and snap ---
  b->home();
  c.check(c.until([&] { return !b->busy(); }), "Home loads");
  QTest::qWait(600);
  auto carousel = shownItem(w->contentItem(), "carousel");
  c.check(carousel, "Home lays its shelves out as carousels");
  if (carousel) {
    c.check(carousel->property("snapMode").toInt() == 1,
            "which snap items into place rather than resting part-way");
    auto first = anyItem(carousel, "carouselCell_0");
    c.check(first, "the carousel has cells");
    if (first) {
      auto card = shownItem(first, "carouselCard");
      c.check(card && qAbs(card->scale() - 1) < 0.02,
              "a cell fully in view is at full size");
      // Scrolling it towards the edge has to shrink it: that squashed preview
      // is what Material found people read as "there is more here".
      carousel->setProperty("contentX", carousel->property("contentX").toReal() +
                                            carousel->property("cellWidth").toReal() * 0.7);
      QTest::qWait(300);
      c.check(card && card->scale() < 0.95,
              QString("and shrinks as it leaves the viewport (%1)")
                  .arg(card ? card->scale() : 0, 0, 'f', 2));
      c.check(card && qAbs(card->property("parallax").toReal()) > 0.05,
              "with its visual travelling at a different speed from its container");
      carousel->setProperty("contentX", 0);
      QTest::qWait(300);
      c.check(card && qAbs(card->scale() - 1) < 0.02, "and grows back on return");
    }
    c.shot("03-carousel");
  }

  // --- Pull to refresh ---
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("files")));
  QTest::qWait(500);
  auto puller = anyItem(w->contentItem(), "contentRefresh");
  auto tracks = shownItem(w->contentItem(), "tracksView");
  c.check(puller && tracks, "the song list can be pulled to refresh");
  if (puller && tracks) {
    c.check(!puller->isVisible(), "the indicator is out of the way at rest");
    const double origin = tracks->property("originY").toReal();
    tracks->setProperty("contentY", origin - 20);
    QTest::qWait(150);
    c.check(puller->isVisible() && puller->property("progress").toReal() > 0.1 &&
                !puller->property("armed").toBool(),
            "a short pull shows the indicator without arming it");
    c.shot("04-pull-started");
    tracks->setProperty("contentY", origin - 90);
    QTest::qWait(150);
    c.check(puller->property("armed").toBool(), "pulling past the threshold arms it");
    c.shot("05-pull-armed");
    tracks->setProperty("contentY", origin);
    QTest::qWait(200);
    c.check(!puller->isVisible(), "and letting go without a refresh puts it away");
  }

  // --- Navigation: a rail at this size, a bar once the window is compact ---
  c.check(!w->property("compactWindow").toBool(), "a wide window is not compact");
  c.check(shownItem(w->contentItem(), "navigationRail"), "so navigation is a rail");
  c.check(!shownItem(w->contentItem(), "navigationBar"), "and not a bar");
  w->resize(520, 760);
  QTest::qWait(700);
  c.check(w->property("compactWindow").toBool(), "a narrow window is compact");
  auto bar = shownItem(w->contentItem(), "navigationBar");
  c.check(bar, "which moves navigation to a bar along the bottom");
  c.check(!shownItem(w->contentItem(), "navigationRail"), "and stands the rail down");
  if (bar) {
    c.check(bar->width() >= w->width() - 2, "the bar spans the window");
    const auto foot = bar->mapToScene(QPointF(0, bar->height())).y();
    c.check(qAbs(foot - w->height()) < 2,
            QString("and sits at the bottom of it (ends at %1 of %2)").arg(foot).arg(w->height()));
    int destinations = 0;
    for (const auto *key : {"home", "search", "library"})
      if (shownItem(bar, QString("navBar_") + key))
        ++destinations;
    c.check(destinations == 3, "with three destinations, which is Material's minimum");
    for (const auto *key : {"home", "search", "library"})
      c.check(shownItem(bar, QString("navBarLabel_") + key),
              QString("the %1 label is shown, never dropped").arg(key));
    // Exactly one destination carries the active indicator.
    int active = 0;
    for (const auto *key : {"home", "search", "library"})
      if (auto indicator = shownItem(bar, QString("navBarIndicator_") + key))
        if (indicator->property("color").value<QColor>() == c.themeColor("primaryContainer"))
          ++active;
    c.check(active == 1, QString("exactly one destination is marked active (%1)").arg(active));
    c.shot("06-navigation-bar");
    c.click("navBar_search");
    c.check(c.until([&] { return w->property("destination") == "search"; }, 3000),
            "and choosing one navigates");
    c.click("navBar_home");
    c.check(c.until([&] { return w->property("destination") == "home"; }, 3000), "as does another");
    c.shot("07-navigation-bar-home");
  }
  w->resize(1400, 900);
  QTest::qWait(700);
  c.check(shownItem(w->contentItem(), "navigationRail"), "widening brings the rail back");
  c.check(!shownItem(w->contentItem(), "navigationBar"), "and puts the bar away");

  // --- Floating toolbar in the immersive player ---
  QMetaObject::invokeMethod(w, "chooseLibrary", Q_ARG(QVariant, QVariant("files")));
  QTest::qWait(300);
  b->enqueueItems(b->results()->rows);
  b->playAt(0);
  c.check(c.until([&] { return b->playing(); }), "a song plays");
  w->setProperty("immersive", true);
  c.check(c.until([&] { return shownItem(w->contentItem(), "immersiveToolbar") != nullptr; }, 4000),
          "the immersive transport sits on a floating toolbar");
  auto toolbar = shownItem(w->contentItem(), "immersiveToolbar");
  if (toolbar) {
    c.check(toolbar->property("radius").toReal() > 20,
            "which floats as a rounded bar rather than being anchored into the surface");
    c.check(shownItem(toolbar, "immersivePlayButton"), "and holds the transport controls");
    c.check(!shownItem(w->contentItem(), "navigationBar"),
            "a toolbar and a navigation bar are never shown together");
  }
  c.shot("08-floating-toolbar");
  w->setProperty("immersive", false);
  QTest::qWait(400);

  b->stop();
  b->clearQueue();
  c.finish();
}
