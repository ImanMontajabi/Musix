#pragma once
class Backend;
class QQuickWindow;

// --smoke-test: exercise the running app on a throwaway profile and exit 0
// only if every step worked. package-dmg.sh will not make a DMG without it.
void runSmokeTest(Backend *backend, QQuickWindow *window);
