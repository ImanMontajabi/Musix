<div align="center">

# Musix

**YouTube Music, your music files, and your music server — one library, one native app.**

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![macOS Apple Silicon](https://img.shields.io/badge/macOS-Apple%20Silicon-black.svg?logo=apple)](#requirements)
[![Latest release](https://img.shields.io/github/v/release/ImanMontajabi/Musix?label=latest)](https://github.com/ImanMontajabi/Musix/releases/latest)

A minimal Material 3 player built with C++20 and Qt Quick. No account, no telemetry, no embedded browser.

### [⬇ Download the latest release](https://github.com/ImanMontajabi/Musix/releases/latest)

</div>

## Screenshots

### Mini player
<img width="566" height="262" alt="The Musix mini player: a compact rounded window with album art, track title, a lyric line and transport controls" src="https://github.com/user-attachments/assets/63ca9eb1-c9ae-42a2-9408-ebec2c486961" />

### Play from YouTube Music
<img width="1366" height="868" alt="The Musix home page showing YouTube Music shelves of album artwork, with the navigation rail on the left and the player bar along the bottom" src="https://github.com/user-attachments/assets/f3016ea7-710b-46d0-96fe-0b2bcbc7dcce" />

### Play your local music
<img width="1366" height="868" alt="The Musix local library showing imported songs in a list with embedded artwork, and the now-playing panel on the right" src="https://github.com/user-attachments/assets/ae6dfbba-4402-4566-a47e-122071082b2f" />

## Features

- **YouTube Music** — search songs, albums, artists and playlists, and play audio with no browser and no ad interface. Standard or data-saver quality.
- **Your own files** — import MP3, FLAC, OGG, Opus, M4A, AAC, WAV, AIFF and WMA, individually or a folder at a time. Folders are watched and rescanned while the app runs.
- **Navidrome / Subsonic and Jellyfin** — browse and search your server, stream original or transcoded audio, edit permitted playlists, and sync favourites and history.
- **Artwork** — embedded covers, sidecar images, and animated covers from GIF, WebP, MP4 or WebM. Missing covers for YouTube songs are looked up on Apple Music, then MusicBrainz.
- **Lyrics** — synchronised lyrics with an immersive view, timing adjustment, LRC import and search with jump-to-line.
- **Library tools** — likes, history, smart playlists, M3U import and export, custom playlist covers, multi-selection, drag reordering and Undo.
- **Playback** — mini player, immersive player, queue editing, volume normalisation, per-song gain, shuffle, repeat, sleep timer and playback speed.
- **Appearance** — light and dark themes that follow the system, a pickable Material accent colour, artwork-derived colour and an ambient cover backdrop.
- **Keyboard and assistive use** — every control takes focus and shows it, and colours are solved to hold 4.5:1 contrast in both themes.

The window has no separate title bar: the app's background runs to the top edge and the traffic lights sit on it.

Full documentation is in the **[Musix guide](docs/guide.md)**.

## Requirements

- **Apple Silicon** (M1 or later). There is no Intel build.
- **macOS 14 (Sonoma) or later.** Every binary in the app is built for macOS 14, and the build refuses to ship one that is not.

Nothing else. Qt, Python, the YouTube resolver and FFmpeg all travel inside the app, and it keeps the resolver up to date on its own.

## Install

1. Download `Musix-<version>-arm64.dmg` from the [latest release](https://github.com/ImanMontajabi/Musix/releases/latest).
2. Open it and drag **Musix** to **Applications**.
3. Open Musix once from Applications. macOS will refuse to launch it.
4. Go to **System Settings → Privacy & Security**, scroll to the message naming Musix, and click **Open Anyway**. Confirm once.

Every launch after that is ordinary. The refusal is expected: Musix is signed, but not with a paid Apple Developer certificate, so macOS cannot check it against Apple's notary service and asks you to decide. If it says *"Musix is damaged and can't be opened"* instead, the download is corrupt — check it against the `SHA256SUMS.txt` on the release and download it again.

## Keyboard shortcuts

| Key | Action |
| --- | --- |
| Space | Play / pause |
| ⌘F or ⌘K | Focus search |
| ⌘⇧P | Quick actions |
| ⌘J | Show the playing song in the queue |
| ⌘L | Queue |
| ⌘Y | Lyrics |
| ⌘⇧M | Mini player |
| ⌘⇧F | Immersive player |
| ⌘← / → | Previous / next track |
| ← / → | Seek ten seconds |
| 0 – 9 | Jump to that tenth of the track |
| ⌘↑ / ↓ | Volume |
| M | Mute |
| ⌥← | Back |
| ⌘A | Select songs in the focused list |
| ⎋ | Close the current view or clear the selection |
| ? or F1 | Shortcut reference |
| ⌘, | Settings |
| ⌘M | Minimize |
| ⌘W | Close the window, which quits as the close button does |
| ⌘Q | Quit |

⌘H hides the app as usual. The in-app sheet (**?**) lists these too.

## Privacy

Musix has no analytics and no telemetry, and it never asks you to sign in to anything of ours.

What leaves your Mac, and only when the matching feature is used:

| Goes to | What is sent | When |
| --- | --- | --- |
| `music.youtube.com` | Search terms, and the id of the song being played | Browsing or playing YouTube Music. Anonymous — no Google account is involved, and likes and playlists stay local. |
| `itunes.apple.com`, `music.apple.com` | A song's title, artist and album | Looking for a cover or an animated cover a YouTube song has none of |
| `musicbrainz.org`, `coverartarchive.org` | A song's title, artist and album | Only when Apple Music has no match |
| `lrclib.net` | A song's title, artist, album and duration | Lyrics, when the source has none. Can be turned off in Settings. |
| Your own server | Whatever Subsonic or Jellyfin needs | Only if you connect one |
| `api.github.com` | A request for the list of Musix releases; the only thing it says about you is your Musix version, in the user agent | Checking for updates: once a day while Musix is running, and whenever you choose **Check for Updates…**. Turn off **Settings → Updates → Check for updates automatically** to stop the daily check. Musix only tells you a new version exists and opens its GitHub page; it never downloads or installs anything itself. |

Requests identify the app as `Musix/<version> ( https://github.com/ImanMontajabi/Musix )`, which is what MusicBrainz and LRCLIB ask of a client.

Your data lives in `~/Library/Application Support/Sung/sung/`, settings in `~/Library/Preferences/com.sung.sung.plist`, and cache in `~/Library/Caches/Sung/sung/`. The folder is still called Sung for compatibility with existing installs.

## Troubleshooting

**"Musix is damaged and can't be opened."** The download is corrupt, or the quarantine flag was cleared oddly. Verify the DMG against `SHA256SUMS.txt` on the release and download it again.

**macOS says the app needs a newer version of macOS.** Musix needs macOS 14 or later. Musix 0.12.0 needed macOS 27 by accident; that was fixed in 0.13.0.

**A song will not play.** Playback depends on YouTube's availability, your region and the network. Musix buffers a song before playing it, so starting can take a moment. When YouTube changes how audio is served, the app refreshes its own resolver in the background — at most once a day, and immediately after a failure — keeping the last working version if an update is broken. There is nothing to run by hand.

**My server password is not remembered.** It is not saved on macOS. Storing it needs a freedesktop secret service, which macOS does not have, so a server connection lasts for the session and the app says so when you connect. Address and username are remembered; the password is not.

**ListenBrainz scrobbling will not stay signed in.** Same reason — the token has nowhere to be stored, and Settings says a system keyring is needed.

**Media keys and notifications do nothing.** Both go through D-Bus, which macOS does not have. They work on Linux only.

## Development

```bash
./scripts/setup.sh   # ./runtime venv with yt-dlp and ytmusicapi
./scripts/build.sh   # configures ./build and builds
./scripts/run.sh     # runs it against the checkout
./scripts/test.sh    # ctest plus the Python suites
```

`./scripts/package-dmg.sh` produces the distributable `Musix-<version>-arm64.dmg`: a Release build against Qt's official binaries, `macdeployqt`, the unused Qt plugins pruned, a relocatable Python carrying the resolver, an LGPL FFmpeg from `./scripts/build-ffmpeg.sh`, every bundled library's license text, and an ad-hoc signature. It refuses to build from a dirty tree, prints the commit it is building, and checks the signature still verifies after the app has been used. Two builds of the same commit produce a byte-identical bundle.

Architecture and conventions are in [CLAUDE.md](CLAUDE.md).

### Releasing

macOS releases are cut from a clean tree; `package-dmg.sh` refuses to run otherwise, so that every DMG traces back to one commit.

1. Commit everything. Bump `project(Musix VERSION ...)` in `CMakeLists.txt` and commit that too — the version names the DMG, the volume and the FFmpeg source tarball.
2. Build:

   ```bash
   ./scripts/package-dmg.sh
   ```

   It prints the commit it is building, and finishes by printing the path, size and SHA-256 of every artifact, and writing `SHA256SUMS.txt` covering them. Keep that output; the checksums go in the release notes.

   The first run fetches The Qt Company's official Qt 6.11.2 binaries and its pinned module sources into `build-packaging/` with `scripts/fetch-qt.sh`, and reuses them after that. The app is built against those rather than Homebrew's Qt, because Homebrew's bottles require the macOS they were built on. The build fails if any binary in the bundle needs a newer macOS than `CMAKE_OSX_DEPLOYMENT_TARGET`.
3. Test the DMG the way somebody receiving it would, from Finder rather than a terminal — open the image, drag Musix to Applications, launch it, and walk through **Privacy & Security → Open Anyway** once. A quarantined copy is the only way to see what a first launch actually does:

   ```bash
   xattr -w com.apple.quarantine "0081;$(printf %x $(date +%s));Safari;" Musix-<version>-arm64.dmg
   ```

   The first launch must be refused with an **Open Anyway** option. If macOS says the app is *damaged*, the signature is broken — do not ship it.
4. Tag the commit `package-dmg.sh` printed — not whatever `HEAD` is by now — and push:

   ```bash
   git tag -a v<version> -m "Musix v<version>" <the commit the build reported>
   git push origin main --follow-tags
   ```

   V0.12.0 and V0.13.0 were both tagged at commits behind the ones their DMGs were built from — V0.13.0's tag is at `6403e0b`, which still builds 0.12.0; its DMG came from `0cb3183` — so the published source did not produce the published binary. The build prints the hash for this reason; use it.

5. Create the release with every artifact attached:

   ```bash
   gh release create v<version> \
     Musix-<version>-arm64.dmg \
     Musix-<version>-ffmpeg-<ffmpeg version>-source.tar.xz \
     SHA256SUMS.txt \
     --title "Musix v<version>" --notes-file notes.md
   ```

   The FFmpeg tarball is not optional. FFmpeg is LGPL-2.1, whose section 6 requires the corresponding source be offered from the same place as the binary, and the release page is that place. Qt is LGPL-3.0 and is met by the `download.qt.io` URL in `NOTICE` instead, which GPLv3 section 6(d) allows. The notes must carry the checksums and the one-time **Open Anyway** step.

Run automated tests:

```bash
./scripts/test.sh
./scripts/verify.sh --offline
```

The offline suite includes immersive-player checks at normal and high DPI, plus process-restart checks for saved layout preferences. To run just these checks against a build configured with `-DSUNG_DIAGNOSTICS=ON`:

```bash
python3 tests/immersive_regression.py --binary /path/to/diagnostics/musix --output verification/immersive
```

Use a new output directory. These checks run offscreen with generated silent music and isolated settings.

The full integration suite is `./scripts/verify.sh`. It needs network access, a working audio session, Google Sans Flex, Qt Test, `qdbus6` and `dbus-run-session` (`qt6-tools` and `dbus` provide the command-line tools on Arch). It briefly plays audio at low volume and uses isolated test profiles. The online-artwork checks compare fresh downloads and cached replay across five albums, then verify native rendering with muted YouTube playback. These live checks depend on the albums remaining available.

Reports and screenshots are written to the ignored `verification/` directory. Do not attach raw logs or cookie files to issues; playback logs may contain signed media URLs. The Git allowlist keeps build outputs, runtime environments, personal media and development reports out of the repository.

To test the server integration, build the test targets and provide a Navidrome executable:

```bash
./scripts/test.sh
python3 tests/navidrome_integration.py \
  --navidrome /path/to/navidrome \
  --test-binary build-tests/musix-subsonic-tests \
  --output verification/navidrome
```

The script starts a loopback-only server, creates a temporary account and 105 generated audio fixtures, and tests browsing, playback, seeking, lyrics, playlist edits, ratings, favorites, queue restoration and scrobbling. It stops the server and removes its temporary data afterward. Use a new output directory for each run. With a diagnostics build, add `--ui-binary /path/to/musix` to exercise the rendered interface too.

To test Jellyfin with generated music and disposable accounts:

```bash
python3 tests/jellyfin_integration.py \
  --server-binary /path/to/jellyfin \
  --test-binary build-tests/musix-jellyfin-tests \
  --output verification/jellyfin
```

The server binds to loopback only and stops after testing. Test data and credentials stay in the private output directory; remove it when finished. Add `--ui-binary /path/to/musix` for rendered UI checks, or additionally `--native-ui` to use Hyprland workspace 2. The normal test suite also checks malformed responses, redirects, cancellation, credential persistence and failed downloads using a local mock server.


## License

[MIT](LICENSE). Material Symbols are licensed under Apache-2.0; see [NOTICE](NOTICE) for third-party acknowledgments, including the license texts and LGPL corresponding source for everything the macOS bundle redistributes.

Musix is a fork of [Sung](https://github.com/yappologistic/Sung) by yappologistic, used under the MIT License, and the great majority of this code is theirs. **Linux users should go to [Sung](https://github.com/yappologistic/Sung)**, which is where the Linux player is maintained; Musix adds the macOS port, the self-contained bundle and the self-updating resolver.

Musix is an independent project and is not affiliated with Google or YouTube.
