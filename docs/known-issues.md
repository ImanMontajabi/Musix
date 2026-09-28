# Known issues upstream

Problems in the libraries Musix is built on that Musix does not work around
yet. Each has its report upstream and a test that fails the moment the
behaviour changes, so a fix is noticed. Before each release, check the status
of every report here: when one is fixed in the Qt that Musix ships, update Qt,
drop the expected failure and the note in the release notes.

## A seek before playback lands on the whole second

**Report:** [COIN-1332](https://qt-project.atlassian.net/browse/COIN-1332)
· **Affects:** Qt 6.11.2 as shipped, and still present in Qt's 6.12 and dev
branches · **Found:** 2026-09-28

Seeking a song before it has started playing, including one Musix restores
paused when it opens, can land up to a second earlier than chosen. Resuming a
session saved at 1:00.7 plays from 1:00.0. Once playback is running, seeks are
exact.

**Cause.** `AVFMediaPlayer::setPosition` in Qt's darwin media backend builds
the target from the item's current time and keeps its timescale:

```objc
CMTime newTime = [playerItem currentTime];
newTime.value = (pos / 1000.0f) * newTime.timescale;
```

Before the playback clock has run, the current time is `kCMTimeZero`, whose
timescale is one second, so the target is truncated to whole seconds. The seek
itself asks for zero tolerance; the target is already wrong. Building it as
`CMTimeMakeWithSeconds(pos / 1000.0, NSEC_PER_SEC)` would fix it.

**Test.** `CrossfadeTest::aSeekBeforePlaybackLandsOnTheWholeSecond` in
`tests/crossfade_test.cpp` expects the failure (`QEXPECT_FAIL`). If Qt fixes
it, the test reports an unexpected pass, which fails the release checks.

**Not worked around.** A workaround that holds the output silent and seeks
again once the clock runs was written and measured (40–70 ms of added
silence in the affected cases, none in ordinary seeks) and left out of 0.15.1:
the error is under a second, it has been there since the first macOS release,
and the choice between that and shipping a patched Qt plugin is still open.
