#include "windowchrome.h"
#include <QGuiApplication>
#include <QPointer>
#include <QTimer>
#include <QWindow>
#import <AppKit/AppKit.h>

// The target of the app menu's own item. AppKit keeps menu targets weakly, so
// this lives as long as the app does.
@interface MusixMenuTarget : NSObject {
@public
  QPointer<WindowChrome> chrome;
}
- (void)checkForUpdates:(id)sender;
@end
@implementation MusixMenuTarget
- (void)checkForUpdates:(id)sender {
  Q_UNUSED(sender);
  if (chrome)
    emit chrome->checkForUpdatesRequested();
}
@end

WindowChrome::WindowChrome(QObject *parent) : QObject(parent) {
  auto *workspace = NSWorkspace.sharedWorkspace;
  m_reduceMotion = workspace.accessibilityDisplayShouldReduceMotion;
  // The setting can change while Musix is open; it follows at once.
  id observer = [workspace.notificationCenter
      addObserverForName:NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification
                  object:nil
                   queue:NSOperationQueue.mainQueue
              usingBlock:^(NSNotification *) {
                const bool now = NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion;
                if (now == m_reduceMotion)
                  return;
                m_reduceMotion = now;
                emit reduceMotionChanged();
              }];
  m_motionObserver = [observer retain];
}

WindowChrome::~WindowChrome() {
  if (!m_motionObserver)
    return;
  id observer = static_cast<id>(m_motionObserver);
  [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:observer];
  [observer release];
}

bool WindowChrome::offerToQuitUnresponsive(qint64 pid) {
  NSRunningApplication *other = [NSRunningApplication runningApplicationWithProcessIdentifier:pid_t(pid)];
  NSAlert *alert = [[NSAlert alloc] init];
  alert.messageText = @"Musix is already running but isn’t responding.";
  alert.informativeText = @"Only one Musix can use your library at a time. Quit the other one to open Musix here; "
                          @"anything it hasn’t saved yet may be lost.";
  [alert addButtonWithTitle:@"Quit It and Open Musix"];
  [alert addButtonWithTitle:@"Cancel"];
  [NSApp activateIgnoringOtherApps:YES];
  const bool quit = [alert runModal] == NSAlertFirstButtonReturn;
  [alert release];
  if (!quit || !other)
    return quit;
  // A quit request first, which lets it save if it is only busy.
  [other terminate];
  for (int i = 0; i < 100 && !other.terminated; ++i)
    [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
  if (!other.terminated)
    [other forceTerminate];
  return true;
}

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
  installAppMenuItems();
}

void WindowChrome::installAppMenuItems() {
  if (QGuiApplication::platformName() != QLatin1String("cocoa"))
    return;
  // The app menu Qt builds has About, Services, Hide and Quit, and no way to
  // add to it without a QMenuBar -- which would bring Qt Widgets and its own
  // copy of every standard item, each shortcut then answered twice. So the
  // one item goes straight into the menu AppKit already shows, at the top
  // where Mac apps keep it. It carries no key equivalent to collide with.
  // The menu exists only once the app has finished launching.
  NSMenu *appMenu = NSApp.mainMenu.numberOfItems ? [NSApp.mainMenu itemAtIndex:0].submenu : nil;
  if (!appMenu) {
    QTimer::singleShot(200, this, [this] { installAppMenuItems(); });
    return;
  }
  static MusixMenuTarget *target = [[MusixMenuTarget alloc] init];
  target->chrome = this;
  if ([appMenu indexOfItemWithTarget:target andAction:@selector(checkForUpdates:)] >= 0)
    return;
  NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:@"Check for Updates…"
                                                action:@selector(checkForUpdates:)
                                         keyEquivalent:@""];
  item.target = target;
  // First: Qt gives this app no About item. Below About should one appear.
  NSInteger at = 0;
  if (appMenu.numberOfItems && [appMenu itemAtIndex:0].action == @selector(orderFrontStandardAboutPanel:))
    at = 1;
  [appMenu insertItem:item atIndex:at];
  [appMenu insertItem:[NSMenuItem separatorItem] atIndex:at + 1];
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
