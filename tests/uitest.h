#pragma once
#include <QDir>
#include <QGuiApplication>
#include <QList>
#include <QQuickItem>
#include <QRegularExpression>
#include <QQuickWindow>
class Backend;
class QQuickWindow;

// Headless, the render loop lays items out and moves animations on only when
// it draws a frame, and it draws one only when asked: a click aimed from
// positions read before that lands beside its target, and one sent to a
// dialog still in its opening transition is ignored. Draw a frame first, as a
// screen would have long since.
inline void settleForInput(QQuickItem *item) {
  if (!item)
    return;
  QList<QQuickItem *> chain;
  for (auto p = item; p; p = p->parentItem())
    chain.prepend(p);
  for (auto p : chain)
    p->ensurePolished();
  if (auto window = item->window())
    window->grabWindow();
}
void runUiTests(Backend *, QQuickWindow *);
void runUiAudit(Backend*,QQuickWindow*);

void runRecoveryTests(Backend*,QQuickWindow*);

void runLyricsTests(Backend*,QQuickWindow*);

void runFeatureTests(Backend*,QQuickWindow*);
void runSearchSelectionTests(Backend*,QQuickWindow*);

void runVisualPolishTests(Backend*,QQuickWindow*);

void runServerTests(Backend*,QQuickWindow*);

void runRemoteServerTest(Backend*,QQuickWindow*);

void runQolTests(Backend*,QQuickWindow*);

void runLibraryQolTests(Backend*,QQuickWindow*);

void runVisualDelightTests(Backend*,QQuickWindow*);

void runAudioIndicatorTests(Backend*,QQuickWindow*);

void runInteractionTests(Backend *b, QQuickWindow *w);
void runFolderImportTests(Backend *b, QQuickWindow *w);

void runLocalArtworkTests(Backend *, QQuickWindow *);

void runOnlineArtworkTests(Backend *, QQuickWindow *);

void runOnlineArtworkLiveTests(Backend *, QQuickWindow *);
void runProductPolishTests(Backend *, QQuickWindow *);

void runLibraryPolishTests(Backend *, QQuickWindow *);

void runPlaybackPolishTests(Backend *, QQuickWindow *);

void runVisualRefinementTests(Backend *, QQuickWindow *);

void runListeningRefinementTests(Backend *backend,QQuickWindow *window);

void runInteractionRefinementTests(Backend*,QQuickWindow*);

void runImmersivePolishTests(Backend*,QQuickWindow*);

void runImmersiveEdgeTests(Backend*,QQuickWindow*);
void runImmersivePreferencesTest(Backend*,QQuickWindow*);

void runAmbientImmersiveTests(Backend*,QQuickWindow*);
void runPersonalizationTests(Backend*,QQuickWindow*);
void runHomeRailTests(Backend*,QQuickWindow*);
void runOnboardingTests(Backend*,QQuickWindow*);
void runLibraryExchangeTests(Backend*,QQuickWindow*);
void runBackdropPulseTests(Backend*,QQuickWindow*);
void runPlaybackMemoryTests(Backend*,QQuickWindow*);
void runInterfaceAuditTests(Backend*,QQuickWindow*);

void runTourCapture(Backend*,QQuickWindow*);
void runDynamicColorTests(Backend*,QQuickWindow*);
void runNavigationMotionTests(Backend*,QQuickWindow*);
void runArtistHeroTests(Backend*,QQuickWindow*);
void runSingAlongTests(Backend*,QQuickWindow*);
void runCrossfadeUiTests(Backend*,QQuickWindow*);
void runTrackDetailsTests(Backend*,QQuickWindow*);
void runQueueHistoryTests(Backend*,QQuickWindow*);
void runListeningStatsTests(Backend*,QQuickWindow*);
void runPlaylistVersionsTests(Backend*,QQuickWindow*);
void runWindowWashTests(Backend*,QQuickWindow*);
void runMaterialFoundationTests(Backend*,QQuickWindow*);
void runMaterialComponentTests(Backend*,QQuickWindow*);
void runMaterialDetailTests(Backend*,QQuickWindow*);
void runMaterialExpressiveTests(Backend*,QQuickWindow*);
void runMaterialSizingTests(Backend*,QQuickWindow*);
void runMaterialSchemeTests(Backend*,QQuickWindow*);
void runMaterialGrainTests(Backend*,QQuickWindow*);
void runMaterialScaleTests(Backend*,QQuickWindow*);
void runMaterialControlsTests(Backend*,QQuickWindow*);
void runMaterialAnatomyTests(Backend*,QQuickWindow*);
void runMaterialEmphasisTests(Backend*,QQuickWindow*);

// What every open window showed when a check failed, so a failure can be read
// off a picture instead of guessed at from its label.
inline void failShot(const QString &label) {
  static int count = 0;
  const auto dir = qEnvironmentVariable("SUNG_TEST_OUTPUT");
  if (dir.isEmpty())
    return;
  QDir().mkpath(dir + "/failures");
  auto name = label;
  name.replace(QRegularExpression("[^A-Za-z0-9]+"), "-");
  ++count;
  int index = 0;
  for (auto window : QGuiApplication::topLevelWindows())
    if (auto quick = qobject_cast<QQuickWindow *>(window); quick && quick->isVisible())
      quick->grabWindow().save(QString("%1/failures/%2-%3-%4.png").arg(dir).arg(count, 3, 10, QChar('0')).arg(name.left(60)).arg(index++));
}
