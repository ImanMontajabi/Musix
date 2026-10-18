# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Musix is a native Material 3 music player built with C++20/Qt 6 (Quick/QML) that plays YouTube Music, local files, and Subsonic/Navidrome or Jellyfin servers. It is a macOS app (Apple Silicon, macOS 14 and later); the source, binary and several paths still say `sung`, which is deliberate.

**Musix is its own project, not a fork that tracks Sung.** Upstream Sung (the `upstream` remote, yappologistic/Sung, fetch-only) is read for ideas and nothing more: do not plan or propose merges from it, and do not keep code shaped for future merges. When an upstream idea is worth having, build it the Musix way.

**Musix is macOS-only from 0.14.0 on.** New features do not need Linux support and are not tested there. The Linux code already in the tree (MPRIS, D-Bus notifications, `windowchrome.cpp`, the XDG paths) stays until it gets in the way, but nothing new has to keep it working.

## Build, run, test

```bash
./scripts/setup.sh   # creates ./runtime venv, installs helper/requirements.txt (yt-dlp, ytmusicapi)
./scripts/build.sh   # configures ./build (Release, Ninja, BUILD_TESTING=OFF) and builds
./scripts/run.sh     # runs ./build/sung with SUNG_HELPER/SUNG_PYTHON pointed at the checkout
```

On macOS, `./scripts/package-dmg.sh` builds the distributable `Musix-<version>-arm64.dmg`: a Release build against The Qt Company's official Qt binaries (fetched by `scripts/fetch-qt.sh`, not Homebrew's), `macdeployqt`, then a relocatable Python carrying the resolver and an LGPL ffmpeg from `./scripts/build-ffmpeg.sh`, ad-hoc signed. The payloads are downloaded and built once into the ignored `build-packaging/` and reused after that. Dev builds (`build.sh`, `test.sh`) still use Homebrew's Qt.

The bundle's minimum macOS is `CMAKE_OSX_DEPLOYMENT_TARGET` in `CMakeLists.txt` (14.0), and it is a promise only because every binary keeps it: Homebrew bottles carry the macOS they were built on as their minimum, which is why the DMG does not use them, and `package-dmg.sh` fails if any Mach-O in the bundle needs more. Third-party notices for the code compiled into Qt come from `scripts/qt-licenses.py`, which reads Qt's own `qt_attribution.json` files and names a reason for everything it leaves out; the build checks each of those premises.

Pass extra CMake args through `build.sh`, e.g. `./scripts/build.sh -DSUNG_DIAGNOSTICS=ON`. `SUNG_BUILD_JOBS` controls parallelism (default 4).

```bash
./scripts/test.sh          # configures ./build-tests (BUILD_TESTING=ON), builds, runs ctest + Python unittest
./scripts/verify.sh --offline   # full verification suite (tests/verify.py); omit --offline for live network/audio checks
```

`verify.py` gives every stage its own `MUSIX_PROFILE` and exits before running
a stage whose profile is, lies inside or holds the real library, cache or
Preferences folder. On macOS it reports MPRIS and the `/proc`-based performance
stages as skipped, and runs the fixture catalogue on the project's runtime
Python, since the system one is too old for the helper.

- `test.sh` runs both C++ (ctest, Qt Test based) and Python (`unittest discover -s tests -p 'test_*.py'`) suites. To run a single ctest target: `ctest --test-dir build-tests -R <name> --output-on-failure` (targets: `crossfade`, `m3color`, `subsonic-protocol`, `backend`, `notifications`; some, e.g. `sung-jellyfin-tests`, `sung-artwork-tests`, are built but not registered as ctest targets — run the binary in `build-tests/` directly).
- Single Python test: `python3 -m unittest tests.test_catalog -v` (run from repo root) or `python3 tests/test_online_artwork.py`.
- `tests/immersive_regression.py --binary /path/to/diagnostics/sung --output verification/<name>` runs the offscreen immersive/high-DPI/layout-persistence checks against a `-DSUNG_DIAGNOSTICS=ON` build; use a fresh output dir each run.
- Server integration tests spin up a disposable Navidrome/Jellyfin instance and generated audio fixtures — see `tests/navidrome_integration.py` and `tests/jellyfin_integration.py` (`--test-binary build-tests/sung-subsonic-tests`, optional `--ui-binary`/`--native-ui` for rendered checks). Skip unless you have those server binaries available.
- Reports/screenshots land in the git-ignored `verification/` directory.
- A `-DSUNG_DIAGNOSTICS=ON` build also compiles the interactive UI harness in `tests/uitest.cpp` and friends into the `sung` binary itself; each `--foo-test` CLI flag in `src/main.cpp` runs one of these Qt-Test-based UI suites headlessly (offscreen QPA) instead of the normal app. On macOS, give them `MUSIX_PROFILE` (see below) and a `SUNG_TEST_OUTPUT` directory for fixtures and captures. AVFoundation reports back on the main dispatch queue, which the offscreen platform's event loop never drains; a diagnostics build running offscreen on macOS therefore turns the main Core Foundation run loop from a timer (`main.cpp`), which is what lets these suites play audio headlessly. The ctest media targets, which are not diagnostics builds, still run on cocoa.
- **Launching the app without touching the real profile:** set `MUSIX_PROFILE=<dir>` and pass `--isolated`. The library, caches, runtime copy, settings (an INI file instead of the plist) and QML cache all go under that directory, and the run neither hands off to nor takes the single-instance socket of a Musix that is already running. This is the way to launch the GUI for testing; a second macOS user account does not work, because its processes cannot put windows on this session's screen.
- `--smoke-test` (in every build, not only diagnostics ones) exercises the running app and exits 0 only if everything worked: the window, a local import through the helper, playback and a seek, the mini player opened three times while playing, the themes and Settings; any QML error from the app's own files fails it. It refuses to run without `MUSIX_PROFILE`. `package-dmg.sh` runs it on the signed bundle and makes no DMG if it fails, so it puts a window on screen for a few seconds during every packaging build.

## Architecture

**Two-language split:** the C++/QML app never talks to YouTube directly. `Backend::request()` (src/backend.cpp) spawns `helper/catalog.py` as a one-shot subprocess per call (`python3 helper/catalog.py`, JSON args on stdin, JSON result on stdout, own process group so it can be killed cleanly), using `yt-dlp`/`ytmusicapi` under the hood. `helper/online_artwork.py` similarly resolves Apple Music/MusicBrainz cover art. There is no long-lived server or daemon — one process per request, killed on cancel/timeout (45s normal, 75s for `play`/`prepare`).

**`Backend` (src/backend.h/.cpp, ~3500 lines across backend.cpp/backendserver.cpp/productfeatures.cpp/presentationfeatures.cpp) is the app's single QML-facing god object**, exposed to QML as the `app` context property in `main.cpp`. It owns: the current catalog view/navigation stack (`page`/`viewKey`/`sections`/`results`), the playback queue and `QMediaPlayer`, library/local-file import, settings (`QSettings`), server plumbing, and drives most `Q_PROPERTY`/`NOTIFY` signals QML binds to. Its implementation is split across files by concern but it's one class:
  - `backend.cpp` — core state, navigation, catalog/queue mutation
  - `backendserver.cpp` — wiring to `MusicServer` (playback reporting, cover fetch on track change)
  - `productfeatures.cpp` — library-facing features (albums, smart playlists, folders, listening sessions, etc.)
  - `presentationfeatures.cpp` — home layout / density / start-page presentation settings

**`MusicServer` (src/musicserver.h) is a compile-time facade over `Subsonic` and `Jellyfin`**, dispatching every call (`browse`, `cover`, `lyrics`, `star`, `rate`, `scrobble`, `editPlaylist`, ...) to whichever backend is the active `m_provider`. `Subsonic`/`Jellyfin` (src/subsonic.*, src/jellyfin.*) each own their own HTTP/auth/protocol details independently; `MusicServer` is the only place `Backend` needs to know about.

**QML is the entire UI layer** (`qml/`, ~80 files, registered as the `SungUi` QML module in CMakeLists.txt). `Main.qml` (~2200 lines) is the application shell: window chrome, navigation state (`destination`, `libraryTab`, immersive mode, mini player), and wires most top-level behavior. Reusable Material 3 components are the `M*.qml` files (MButton, MCard, MDialog, MNavigationBar, MFabMenu, etc.) — check there before adding a new UI primitive. `Theme.qml` is a QML singleton (colors, type scale, motion tokens) computed from Material 3 dynamic color; `m3color.cpp`/`m3shape.cpp` implement the actual HCT/tonal-palette math and Material shape library in C++, exposed for QML to consume.

**Native QML types registered under `Sung.Native`** (see `main.cpp`): `RowSelection` (multi-select state for a model, tied to the displayed model rather than recycled delegates), `RoundedArt` (a `QQuickPaintedItem` that paints artwork — local, server, or YouTube video-frame — with rounding/shape masking and animated-cover support via `MotionArtwork`), and `MotionArtwork` itself (the single shared GIF/video decoder for whichever cover is currently playing, exposed as an uncreatable singleton-like instance so all now-playing surfaces share one decoder).

**Other native singletons wired into the QML context in `main.cpp`**: `windowResources` (releases GPU/scene resources after a window is hidden 30s, keeps app state alive), `motionArtwork`, `desktopTheme` (reads the desktop's color scheme / Noctalia palette via a `QFileSystemWatcher`), plus a custom `symbols` `QQuickImageProvider` that tints/caches Material Symbol SVGs at the requested optical size.

**Single-instance behavior**: `main.cpp` uses a `QLocalServer`/`QLocalSocket` pair keyed on uid so a second launch (unless `--isolated`) hands its argv (a YouTube link, or a raise/`--mini` request) to the already-running instance and exits.

**Telegram is a source of its own, not a `MusicServer` provider** (approved as an exception to the rule below, because it has no Subsonic-shaped features and can be signed in alongside a music server). `Telegram` (`src/telegram.cpp`) supervises `helper/telegram.py`, the one helper that is long-lived: it holds a Telethon session and speaks one JSON object per line on stdin/stdout, because the login code belongs to the connection that asked for it. The dialog is `qml/TelegramConnection.qml`, opened from Settings → Connections; the helper runs while it is open, while work is running, and briefly after a song was downloaded. The session is a 0600 file in `<data>/telegram/`. The app's API id and hash are not in the repository: put them in an untracked `telegram-api.local.cmake` (`set(MUSIX_TELEGRAM_API_ID …)` and `set(MUSIX_TELEGRAM_API_HASH "…")`), pass `-DMUSIX_TELEGRAM_API_ID/HASH`, or export them when configuring; a build without them shows a dialog saying so. A checkout can override them at run time with the same environment variables. Sources (Saved Messages, joined chats, forum topics, public channels by @name or link) are chosen in `qml/TelegramSources.qml`; `Telegram` keeps the choice in `<data>/telegram/sources.json`, the helper pages each source's audio messages (`sync`, with FLOOD_WAIT waited out up to 3 minutes and reported), and `Backend::telegramSongs` keeps each source's songs as a playlist with the fixed id `telegram:<key>` (rows have `source: "telegram"`, `remoteId: "chatId:messageId"`; a refresh replaces them). A Telegram song is played while it is downloaded: `Backend::resolveCurrent` asks `Telegram::fetch` with a stream callback, the helper re-finds the message from `remoteId` (refusing files over 200 MB) and brings the file down in 512 KB blocks (`Transfer`: wherever the player reads, and front to back from there, into `<cache>/telegram-audio/<id>.part`, which becomes the song only when whole), and a `stream` request for that job answers with a URL on the helper's own loopback HTTP server (`StreamServer`: 127.0.0.1, a random port, a random token in the path, Range supported) that the player opens at once; the helper is kept up (`Telegram::releaseStream`) for as long as the player may be reading from it. When the file is whole the fetch finishes as any other (tags via mutagen, `done` with the file) and later plays use the file like a local one (so gapless and crossfade work; `readySource` hands the spare deck a cached Telegram song; the next song is prefetched whole and never streamed). A song the helper cannot stream (no known size) is downloaded whole before it plays. Lyrics, radio and autoplay never see a Telegram id: they go by title and artist or are skipped. The cache keeps the 12 most recently used songs within 1 GiB, never evicting the song playing or the one next; skipping away cancels a download in progress, and the next song is fetched a minute before it is needed. What a file's tags taught (length, artist, a title that was a file name) is kept in `<data>/telegram/tags.json` and survives a refresh. The helper stays up 90 s after a download so the next song does not wait for a cold start. Cover art: the helper's `art` command fetches a song's picture as its row scrolls into view (`TrackList.qml` → `Telegram::wantArt`, one at a time, rows that scrolled away are dropped): the message thumbnail, else the cover in the first 512 KB of the file (as much of it as the tag's own length asks for), and a song with neither is remembered in `<data>/telegram/noart.json`; a sync asks only for a chat's profile photo (Saved Messages gets a drawn bookmark); `fetch` also extracts the picture inside a downloaded file with mutagen. Pictures live in `<cache>/telegram-art/c<chatId>/` (one folder per chat, 0700, files 0600, at most 2000 files / 64 MB, a chat's `cover.*` exempt), the song's `art` is the file URL, and the chat photo becomes the playlist's `customCover` (flagged `telegramCover` so a cover the person chose is never replaced). Removing a chat's last source, signing out and Clear cache delete the pictures, and `Backend::telegramArtCleared` makes rows forget them.

**Desktop integration** lives in dedicated small classes: `mpris.cpp/h` (MPRIS2 D-Bus media-key/metadata interface), `playbacknotifier.cpp/h` (desktop notifications), `scrobbler.cpp/h` (ListenBrainz-compatible scrobbling, independent of any music server). The D-Bus pieces are gated on `SUNG_DBUS`, which is off on macOS. `windowchrome.h` follows the same split: CMake compiles `windowchrome.mm` on macOS and `windowchrome.cpp` elsewhere, never both. On macOS the counterpart of MPRIS is `macmedia.mm` (`MacMedia`): it fills `MPNowPlayingInfoCenter` (title, artist, album, duration, elapsed time and rate, cover) and answers `MPRemoteCommandCenter`'s commands (play, pause, toggle, next, previous, scrubbing), whose handlers only call `MacMedia::perform()`; `macmedia_test.cpp` reads the system's state back in-process and never sends a key event. A run with `--isolated` does not register it, so tests never take Control Center. "Pause when audio output disconnects" is `QMediaDevices::audioOutputsChanged` everywhere, plus `pactl subscribe` on Linux and `OutputPortWatcher` (`outputlatency.cpp`, Core Audio data-source and device listeners) on macOS for the built-in jack, which stays one device; both end in `Backend::noteOutputPort`. The interface font is chosen once in `uifont.h` (Google Sans Flex if installed, else the system font on macOS, else Noto Sans) and reaches QML as `uiFontFamily`, which `Theme.fontFamily` uses.

**The macOS title bar is the window's own surface.** `Main.qml` sets `Qt::ExpandedClientAreaHint | Qt::NoTitleBarBackgroundHint` on macOS only, so the app's background runs the full height and the traffic lights sit on it. Qt Quick Controls already insets `contentItem` by the safe area, so nothing needs its own top padding — but the ambient wash is the window's `background` item rather than a child of the content, because a child would stop at that inset and put the seam back. `windowchrome.mm` hides the title text without clearing the title, and keeps the chrome's appearance matched to `window.color`. Linux is untouched: none of those flags exist there and `SafeArea` stays zero.

**Data locations at runtime**: on Linux, library data in `~/.local/share/Sung/sung/`, settings in `~/.config/Sung/`, cache in `~/.cache/Sung/sung/` (XDG overrides respected); on macOS, `~/Library/Application Support/Sung/sung/` and a `com.sung.sung` plist.

**Where the helper, Python and ffmpeg come from** is answered in one place, `src/runtimeenv.cpp`: a checkout uses what `run.sh` exports, a bundle uses its own `Contents/Resources`. Because the bundle is signed and must not be written to, the packaged runtime is copied once into the writable data directory and the app updates `yt-dlp`/`ytmusicapi` inside that copy — at most daily, immediately after a playback failure, and immediately after a re-seed. A resolver that will not import is rolled back, and failing that re-seeded from the bundle.

## Conventions to notice before editing

- Comments in this codebase explain *why*, not *what* — they're notably terse and only appear where a design decision is non-obvious (see `src/scrobbler.h`, `src/artworkurl.h`, `src/windowresources.h` for the house style). Match that style rather than adding narrative comments.
- Many files pack multiple statements per line intentionally (dense style throughout `backend.h`/`backend.cpp`); don't reflow files wholesale as a side effect of a small change.
- `Backend`, `Subsonic`, and `Jellyfin` are each single large classes by design (facade + one god object), not an oversight — new server providers should follow the `Subsonic`/`Jellyfin` shape and be added to `MusicServer`'s dispatch, not layered on top of `Backend` directly.
- QML files: new reusable widgets go in `qml/M*.qml` following the existing Material naming; new files must also be added to the `QML_FILES` list in `CMakeLists.txt`'s `qt_add_qml_module` call (and to the relevant test executable's source list in CMakeLists.txt if C++ sources are added).

## Working on this machine

These are hard rules, not preferences. Each one is here because breaking it
already cost something.

- **Never change a system-wide setting** — appearance, sound, displays, input,
  anything in System Settings — without asking first. Testing that the app
  follows the desktop's light/dark setting is not a reason to flip it; ask.
- **Screenshot the target window only**, with `screencapture -l <windowID>`.
  Never `-R` (a region) and never the whole screen: both capture whatever
  happens to be in front, which has already pulled a private messaging window
  into a transcript. Get the id from a helper that reads
  `CGWindowListCopyWindowInfo` and filters by pid.
- **Test and diagnostics runs cannot reach the real profile.** A diagnostics
  build, and any build given a test flag (`--*-test`, the smoke tests,
  `--audit`, `--benchmark`, `--screenshot`, `--tour`), exits with status 2
  unless `MUSIX_PROFILE` points outside the real library, cache and
  Preferences folders (`Profile::unsafeReason()`). Do not work around it. A
  run of the Linux-minded `verify.py` once wrote every suite's fixtures into
  the real library and cost the user their liked songs, queue and settings.
- **Backups are automatic.** `scripts/backup-profile.py <reason>` copies the
  library folder (without the runtime copy) and the settings, as the
  preferences service holds them, into `~/MusixBackups/auto/`, checks the copy
  and keeps the newest five. `test.sh`, `package-dmg.sh`, `verify.py` and
  `run.sh` (when it runs on the real profile) call it first; call it yourself
  before anything else that could touch the profile.
- **Never launch the GUI on the real profile without a backup.** Use
  `MUSIX_PROFILE=<scratch dir> … --isolated` instead, which keeps every write
  under that directory; `--isolated` alone does not isolate data, and `$HOME`
  does not reach `NSHomeDirectory()`. When the real profile genuinely has to
  be used (checking an upgrade over the user's own library), copy
  `~/Library/Application Support/Sung` and
  `~/Library/Preferences/com.sung.sung.plist` first, restore both afterwards
  (settings through the preferences service: `defaults import com.sung.sung
  <file>`, which merges, then `defaults delete` for each key the copy lacks;
  do not restart `cfprefsd`), and confirm with
  `shasum -a 256` that they match the backup. The backup goes in a dated
  folder under `~/MusixBackups/`, never in the session's scratchpad: the
  scratchpad is cleared when a session ends, and a backup lost that way once
  left the user's profile with nothing to fall back on.
- **Never install anything into `build-packaging/` or the bundled runtime** by
  hand; only the build scripts write there (`build-packaging/tools/` holds the
  build's own tools, such as the Qt downloader, and is never bundled). Those
  caches are what ships. A `pip install` into `build-packaging/
  python-runtime` for a one-off analysis put Pillow in the DMG, unlisted in
  `helper/requirements.txt` and unmentioned in `NOTICE`. Make a throwaway venv
  in the scratchpad instead. The build now refuses a runtime whose packages
  have drifted, which is a backstop, not a licence to install.
- **Never run two packaging builds at once.** `package-dmg.sh` takes a lock and
  will refuse, but the lock is the last line of defence, not permission to
  try: two runs sharing `build-packaging/dmg` once produced a bundle with the
  runtime nested inside itself and silently voided a day of results.
- **Verify macOS window behaviour, do not reason about it.** AppKit semantics
  that are true of a plain `NSWindow` are not necessarily true of what Qt
  builds. Window dragging was reported working on exactly that reasoning and
  was in fact broken.

## Releasing

Three releases in a row (V0.12.0, V0.13.0 and the first 0.13.1 build) went out
with a tag that did not match the build. The steps, in order:

1. Bump `project(Musix VERSION …)` in `CMakeLists.txt`, commit, and make sure
   the tree is clean: `package-dmg.sh` prints the commit it built from.
   Then run `./scripts/release-checks.sh` on that commit. It runs ctest, the
   Python tests and every UI and live suite (headless, each on its own
   profile), and any failing suite stops the release. 0.14.1 and 0.15.0
   shipped with UI suites failing because nothing in the release ran them.
   Do not loosen a check to get it through: a check that fails because the
   app changed on purpose is updated only with the commit that changed it
   named, and anything else is treated as a regression. Check the status of
   every upstream report in `docs/known-issues.md`, and carry each one that is
   still open into the release notes' known issues.
2. Build warm, then cold (delete `build-packaging/{python-runtime,python.tar.gz,
   qt,ffmpeg-out}` and `make distclean` in `build-packaging/ffmpeg-7.1`), and
   compare a manifest of every file's SHA-256 in `build-packaging/dmg/Musix.app`
   between the two. They must be identical. Signature, Gatekeeper, the
   minimum-macOS guard, the licence audit and the smoke test run inside the
   build; a failure in any of them stops it.
3. **Run the DMG the way a user's Mac does, and play a YouTube song.** The
   build's smoke test and a launch from a terminal both inherit Homebrew's
   `PATH`; an app opened from Finder has `/usr/bin:/bin:/usr/sbin:/sbin` only.
   0.14.1 through 0.15.1 shipped every YouTube song at twice its length because
   of that, and nothing in the release looked. So launch the final DMG's app as
   `env -i HOME=$HOME PATH=/usr/bin:/bin:/usr/sbin:/sbin TMPDIR=$TMPDIR
   MUSIX_PROFILE=<scratch> …/Musix.app/Contents/MacOS/musix --isolated`, search
   a song, play it, and compare the length it shows with the real one; then let
   it play to its end and see the next song start. The gate runs the same
   check headless (`ui-playback`, which `release-checks.sh` refuses to pass
   without), but a person looks at the packaged app too. Then install it with
   the user, who downloads it with Safari and drags it to Applications; the
   user clicks any Gatekeeper prompt.
   `package-dmg.sh` already refuses to make a DMG whose ffmpeg and ffprobe are
   not byte-identical to the ones the gate's live checks used
   (`scripts/check-dmg-ffmpeg.sh`); `./scripts/release-checks.sh --dmg-only`
   makes the same comparison again later, on the DMG as it now stands.
4. **Tag the exact commit the DMG build printed, never `HEAD`** or the latest
   commit. Tags are lowercase: `v<version>`. Push `main`, then the tag.
5. `gh release create v<version>` with the DMG, the FFmpeg source tarball and
   `SHA256SUMS.txt`, marked as the latest release.
6. **Verify the upload**: download every asset again and check its SHA-256
   against the local file and against `SHA256SUMS.txt`.
7. The user decides when to publish: draft the notes and wait for their OK.

