#include "windowchrome.h"

// The macOS build compiles windowchrome.mm instead; see CMakeLists.txt. A
// desktop that draws its own title bar beside the window has nothing to blend.
#ifndef Q_OS_MACOS
void WindowChrome::blend(QQuickWindow *) {}
void WindowChrome::refresh() {}
#endif
