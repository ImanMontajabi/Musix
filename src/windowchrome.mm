#include "windowchrome.h"
#include <QWindow>
#import <AppKit/AppKit.h>

namespace {
NSWindow *nativeWindow(QQuickWindow *window) {
  if (!window)
    return nil;
  // On the cocoa platform a QWindow's winId is its NSView.
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
}
