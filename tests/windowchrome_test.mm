#include "windowchrome.h"
#include <QQuickWindow>
#include <QtTest>
#import <AppKit/AppKit.h>

// What pinning does to the real NSWindow. The window is created but never shown, so nothing appears on screen.
class WindowChromeTest : public QObject {
  Q_OBJECT
private slots:
  void pinningSetsTheNativeLevelOnTheLiveWindow() {
    QQuickWindow window;
    window.setFlags(Qt::Window | Qt::FramelessWindowHint);
    window.create();
    WindowChrome chrome;
    const WId id = window.winId();
    NSWindow *native = reinterpret_cast<NSView *>(id).window;
    QVERIFY(native);
    QCOMPARE(int(native.level), int(NSNormalWindowLevel));
    QCOMPARE(chrome.nativeLevel(&window), int(NSNormalWindowLevel));

    chrome.setAlwaysOnTop(&window, true, true);
    QCOMPARE(int(native.level), int(NSFloatingWindowLevel));
    QCOMPARE(chrome.nativeLevel(&window), int(NSFloatingWindowLevel));
    QVERIFY(native.collectionBehavior & NSWindowCollectionBehaviorCanJoinAllSpaces);
    QVERIFY(native.collectionBehavior & NSWindowCollectionBehaviorFullScreenAuxiliary);
    // The native window is the same one, and Qt was never asked to recreate it with its flag.
    QCOMPARE(window.winId(), id);
    QCOMPARE(reinterpret_cast<NSView *>(window.winId()).window, native);
    QVERIFY(!(window.flags() & Qt::WindowStaysOnTopHint));

    chrome.setAlwaysOnTop(&window, false, true);
    QCOMPARE(int(native.level), int(NSNormalWindowLevel));
    QVERIFY(!(native.collectionBehavior & NSWindowCollectionBehaviorCanJoinAllSpaces));
    QVERIFY(!(native.collectionBehavior & NSWindowCollectionBehaviorFullScreenAuxiliary));
    QCOMPARE(window.winId(), id);
  }
  void theMainWindowIsPinnedWithoutTheSpacesBehaviour() {
    QQuickWindow window;
    window.create();
    WindowChrome chrome;
    NSWindow *native = reinterpret_cast<NSView *>(window.winId()).window;
    QVERIFY(native);
    const NSWindowCollectionBehavior before = native.collectionBehavior;
    chrome.setAlwaysOnTop(&window, true, false);
    QCOMPARE(int(native.level), int(NSFloatingWindowLevel));
    // Fullscreen-capable windows keep what they had: Auxiliary beside Primary is not allowed.
    QCOMPARE(native.collectionBehavior, before);
    chrome.setAlwaysOnTop(&window, false, false);
    QCOMPARE(int(native.level), int(NSNormalWindowLevel));
  }
  void withoutAWindowItIsANoOp() {
    WindowChrome chrome;
    chrome.setAlwaysOnTop(nullptr, true, true);
    QCOMPARE(chrome.nativeLevel(nullptr), -1);
  }
};
QTEST_MAIN(WindowChromeTest)
#include "windowchrome_test.moc"
