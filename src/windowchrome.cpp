#include "windowchrome.h"

// The macOS build compiles windowchrome.mm instead; see CMakeLists.txt. A
// desktop that draws its own title bar beside the window has nothing to blend,
// nothing covering the strip that moves it, and no inset to report.
#ifndef Q_OS_MACOS
void WindowChrome::blend(QQuickWindow *) {}
void WindowChrome::titleBarDoubleClick() {}
void WindowChrome::roundCorners(QQuickWindow *) {}
void WindowChrome::refresh() {}
void WindowChrome::installAppMenuItems() {}
#endif
