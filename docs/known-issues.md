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

# Recorded, not verified

Neither of these is an upstream report, and neither has been confirmed.

## A jump from one song to the next, cause unknown

In the first end-of-song run for 0.15.2 (a copy of the real library, 0.15.1,
2026-10-01 about 03:16), the app went from Adele at 1:47 to the next song,
"Bilit", at exactly 5:15, and then sat paused at 5:20. It was seen once and
never reproduced in several later runs, which all handed over at the song's
real end. It was closed with the cause unknown. Someone may have pressed Next,
clicked the progress bar and paused in that window, but that was not
established, and no code path that would do it by itself was found. Reopen it
if it is seen again, and note what was playing and whether anyone touched the
window.

## The streamed-length check in the release gate

`scripts/release-checks.sh` (7c27c50) fails unless `ui-playback.log` has the
`STREAMED_LENGTH` line and its `PASS`. It has been tested only against a dead
proxy, where it failed as intended. It has not yet been seen to pass in a
full gate run. The first normal run of `./scripts/release-checks.sh` should
confirm that it passes.

The packaged ffmpeg the check uses is `build-packaging/ffmpeg-out/bin`, the
build cache's copy. What a user's Mac runs is the copy inside the DMG, so
`dmg_ffmpeg` in the gate compares the SHA-256 of `ffmpeg` and `ffprobe` in the
two and fails if they differ. The gate runs before the DMG exists: with the
DMG for the version in `CMakeLists.txt` already built it makes the comparison,
and without one it prints "NOT CHECKED". `./scripts/release-checks.sh
--dmg-only`, run after `package-dmg.sh` and before tagging, makes just that
comparison and fails without the DMG. It matched on the 0.15.2 DMG and failed,
as intended, with an altered `ffmpeg` and with no DMG; it has not yet run as
part of a full gate.
