#include "windowchrome.h"
#include <QGuiApplication>
#include <QPointer>
#include <QWindow>
#import <AppKit/AppKit.h>

namespace {
NSWindow *nativeWindow(QQuickWindow *window) {
  // On the cocoa platform a QWindow's winId is its NSView. On any other --
  // offscreen, which every UI test runs on -- it is not an object at all, and
  // messaging it crashed at startup.
  if (!window || QGuiApplication::platformName() != QLatin1String("cocoa"))
    return nil;
  auto view = reinterpret_cast<NSView *>(window->winId());
  return view ? view.window : nil;
}
} // namespace

void WindowChrome::blend(QQuickWindow *window) {
  if (!window || m_window)
    return;
  m_window = window;
  // Qt::ExpandedClientAreaHint and Qt::NoTitleBarBackgroundHint, set in
  // Main.qml, already give the full-height content and a title bar with no
  // background of its own. What they leave is the title text, drawn on top of
  // the app's own header, and an appearance still taken from the system.
  //
  // Both have to be reapplied rather than set once: going fullscreen and back
  // hands the window to AppKit and returns it with its own defaults.
  connect(window, &QWindow::visibilityChanged, this, [this] { refresh(); });
  connect(window, &QWindow::safeAreaMarginsChanged, this, [this] { refresh(); });
  // The chrome follows the window's own background rather than the system
  // setting, so a light app on a dark desktop still gets light buttons, and a
  // Noctalia palette takes them with it. Main.qml binds that colour to
  // Theme.background, which is the one answer everything else here uses too.
  connect(window, &QQuickWindow::colorChanged, this, [this] { refresh(); });
  refresh();
}

void WindowChrome::refresh() {
  if (!m_window)
    return;
  auto native = nativeWindow(m_window);
  if (!native)
    return;
  // Hidden, not cleared: the title string stays on the window, so Mission
  // Control, the Window menu and VoiceOver still say what this is.
  native.titleVisibility = NSWindowTitleHidden;
  const bool dark = m_window->color().lightnessF() < 0.5;
  native.appearance = [NSAppearance appearanceNamed:dark ? NSAppearanceNameDarkAqua
                                                        : NSAppearanceNameAqua];
  const qreal inset = m_window->safeAreaMargins().top();
  if (qFuzzyCompare(inset + 1, m_inset + 1))
    return;
  m_inset = inset;
  emit insetChanged();
}

void WindowChrome::titleBarDoubleClick() {
  auto native = nativeWindow(m_window);
  if (!native)
    return;
  // Desktop & Dock -> "Double-click a window's title bar to". The key is
  // absent until the setting is changed, and zooming is what macOS does then.
  // Reading it beats hardcoding a zoom, which is what a plain QML handler
  // would have done.
  NSString *action = [NSUserDefaults.standardUserDefaults
      stringForKey:@"AppleActionOnDoubleClick"];
  if ([action isEqualToString:@"Minimize"])
    [native performMiniaturize:nil];
  else if ([action isEqualToString:@"None"])
    return;
  else
    [native performZoom:nil];
}

void WindowChrome::roundCorners(QQuickWindow *window) {
  if (!window)
    return;
  // Qt::FramelessWindowHint takes the frame away but leaves an opaque
  // NSWindow behind the scene, so the square corners outside the rounded card
  // were the window itself showing through. Clearing its background is what
  // lets the alpha the scene graph draws reach the screen.
  //
  // The shadow is cut from that alpha, and AppKit only recomputes it when
  // asked, so it has to be invalidated again every time the window resizes or
  // a theme change repaints it -- otherwise the shadow keeps the old outline.
  // Held weakly: a queued call can outlive the window it was queued for, and
  // the render-thread race that crashed this code once is not the only way.
  const auto apply = [window = QPointer<QQuickWindow>(window)] {
    if (!window)
      return;
    auto native = nativeWindow(window);
    if (!native)
      return;
    native.opaque = NO;
    native.backgroundColor = NSColor.clearColor;
    native.hasShadow = YES;
    [native invalidateShadow];
  };
  connect(window, &QWindow::widthChanged, this, apply);
  connect(window, &QWindow::heightChanged, this, apply);
  connect(window, &QWindow::visibilityChanged, this, apply);
  // And once more after the first frame: at this point the scene has drawn
  // nothing, so a shadow cut from its alpha now would be cut from nothing.
  //
  // frameSwapped comes from the render thread, so each frame queues a call
  // here. A hand-rolled disconnect inside the slot ran too late: several
  // frames were already queued, the first call freed the connection, and the
  // next one disconnected through freed memory and crashed. A single-shot
  // connection is cut when the signal fires, on the emitting thread, so only
  // one call is ever queued.
  connect(window, &QQuickWindow::frameSwapped, this, apply, Qt::SingleShotConnection);
  apply();
}
