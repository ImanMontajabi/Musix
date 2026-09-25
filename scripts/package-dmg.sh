#!/usr/bin/env bash
# Build a self-contained Musix.app and wrap it in a DMG.
#
# The result must run on a Mac with no Homebrew, no Python and no ffmpeg, so
# the bundle carries Qt (The Qt Company's own binaries), a relocatable Python
# with the resolver packages, and a pure-LGPL ffmpeg. Order matters: macdeployqt rewrites what it finds, so it
# runs before the payload is copied in, and the signature is applied last
# because every one of those steps invalidates it.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cache="$root/build-packaging"
build="$root/build-release"
stage="$cache/dmg"
version="$(sed -n 's/^project(Musix VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")"
python_release="${MUSIX_PYTHON_RELEASE:-20260901}"
python_version="${MUSIX_PYTHON_VERSION:-3.11.16}"
ffmpeg_version="${FFMPEG_VERSION:-7.1}"
# The oldest macOS the bundle promises, as CMakeLists.txt declares it. Every
# compiler below targets it, and every binary shipped is checked against it
# before signing, because one library built for something newer is enough to
# stop the whole app launching there.
deployment_target="$(sed -n 's/^set(CMAKE_OSX_DEPLOYMENT_TARGET "\([0-9.]*\)".*/\1/p' "$root/CMakeLists.txt")"
[ -n "$deployment_target" ] || { echo "CMakeLists.txt declares no CMAKE_OSX_DEPLOYMENT_TARGET" >&2; exit 1; }
export MACOSX_DEPLOYMENT_TARGET="$deployment_target"
mkdir -p "$cache"

say() { printf '\n== %s ==\n' "$1"; }

# Two runs share one staging directory, and the second one deletes it while the
# first is still deploying into it. That produced a bundle with two copies of
# the runtime in it and a pile of install_name_tool failures, and neither run
# had any way to tell. mkdir is atomic, so it is the lock.
lock="$cache/.build-lock"
# A live pid is not enough to believe the lock. Pids are reused, so a lock left
# behind by kill -9 can come to name somebody else's process, and then no build
# ever starts again and the message tells you to kill a stranger. The holder
# only counts if it is itself a packaging run.
holder_is_running() {
  [ -n "${1:-}" ] && kill -0 "$1" 2>/dev/null &&
    ps -o command= -p "$1" 2>/dev/null | grep -q 'package-dmg\.sh'
}
if ! mkdir "$lock" 2>/dev/null; then
  holder="$(cat "$lock/pid" 2>/dev/null || true)"
  if holder_is_running "$holder"; then
    echo "Another packaging run is already going: pid $holder, started $(cat "$lock/started" 2>/dev/null || echo 'unknown')." >&2
    echo "Wait for it to finish, or stop it with:  kill $holder" >&2
    exit 1
  fi
  # Whatever is in there is not a build, so it cannot be allowed to stop one.
  if [ -n "$holder" ] && kill -0 "$holder" 2>/dev/null; then
    echo "Clearing a lock naming pid $holder, which is not a packaging run." >&2
  else
    echo "Clearing a stale lock left by pid ${holder:-unknown}." >&2
  fi
  rm -rf "$lock"
  mkdir "$lock" || { echo "Could not take the build lock at $lock" >&2; exit 1; }
fi
echo $$ > "$lock/pid"
date '+%Y-%m-%d %H:%M:%S' > "$lock/started"
# EXIT covers the ordinary ending and the failures; the signals cover being
# killed. Only kill -9 can still leave the lock behind, which is what the
# staleness check above is for.
trap 'rm -rf "$lock"' EXIT
trap 'rm -rf "$lock"; exit 130' INT TERM

# What ships has to be traceable to a commit, so refuse to build from a tree
# that is not one. MUSIX_ALLOW_DIRTY exists for trying things out locally.
commit="$(git -C "$root" rev-parse HEAD 2>/dev/null || echo unknown)"
if [ -z "${MUSIX_ALLOW_DIRTY:-}" ] && ! git -C "$root" diff-index --quiet HEAD -- 2>/dev/null; then
  echo "Working tree has uncommitted changes; commit them first." >&2
  git -C "$root" status --short >&2
  echo "(set MUSIX_ALLOW_DIRTY=1 to build anyway, for a throwaway build)" >&2
  exit 1
fi
printf 'Musix %s from %s, for macOS %s and later\n' "$version" "$commit" "$deployment_target"

say "Qt"
qt="$("$root/scripts/fetch-qt.sh" | tail -1)"
echo "$qt"

say "Release build"
# --fresh, because a configure left over from a Homebrew-Qt build would keep
# pointing at Homebrew's Qt, and the build would quietly go on linking it.
cmake --fresh -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DSUNG_DIAGNOSTICS=OFF -DCMAKE_PREFIX_PATH="$qt"
cmake --build "$build" --parallel "${SUNG_BUILD_JOBS:-8}"

say "Python runtime with the resolver"
runtime="$cache/python-runtime"
# What a freshly built runtime contained, written when it was built. A cache
# somebody has since pip-installed into is not the runtime this release is
# supposed to ship, and reusing it puts whatever they added in the DMG --
# which is how Pillow once got there, unlicensed and unmentioned. Anything
# that does not match is treated as no cache at all.
packages="$runtime/.packages"
package_list() {
  "$runtime/bin/python3" - <<'LIST'
import importlib.metadata as m
print('\n'.join(sorted('%s %s' % (d.metadata['Name'], d.version)
                       for d in m.distributions())))
LIST
}
python_ready() {
  [ -x "$runtime/bin/python3" ] && [ -f "$runtime/lib/python3.11/LICENSE.txt" ] &&
    "$runtime/bin/python3" -c "from yt_dlp import YoutubeDL
from ytmusicapi import YTMusic" >/dev/null 2>&1 &&
    [ -f "$packages" ] && diff -q <(package_list) "$packages" >/dev/null 2>&1
}
if ! python_ready; then
  archive="cpython-$python_version+$python_release-aarch64-apple-darwin-install_only_stripped.tar.gz"
  curl -fsSL -o "$cache/python.tar.gz" \
    "https://github.com/astral-sh/python-build-standalone/releases/download/$python_release/$archive"
  rm -rf "$runtime"; mkdir -p "$runtime"
  tar -xzf "$cache/python.tar.gz" -C "$runtime" --strip-components=1
  # pip stays: it is how the app updates its own resolver later.
  "$runtime/bin/python3" -m pip install --disable-pip-version-check --quiet \
    --no-warn-script-location -r "$root/helper/requirements.txt"
  package_list > "$packages"
  python_ready || { echo "python runtime still incomplete after installing" >&2; exit 1; }
fi

say "ffmpeg"
# Three separate things are taken out of this cache later: the two binaries,
# the license text, and the tarball the release publishes as LGPL source. Any
# one of them missing has to send the build back, because skipping it only
# defers the failure to a cp further down.
ffmpeg_ready() {
  [ "$(cat "$cache/ffmpeg-out/.musix-target" 2>/dev/null)" = "$(MACOSX_DEPLOYMENT_TARGET="$deployment_target" "$root/scripts/build-ffmpeg.sh" --stamp)" ] &&
  [ -x "$cache/ffmpeg-out/bin/ffmpeg" ] && [ -x "$cache/ffmpeg-out/bin/ffprobe" ] &&
    [ -f "$cache/ffmpeg-$ffmpeg_version/COPYING.LGPLv2.1" ] &&
    [ -f "$cache/ffmpeg-$ffmpeg_version.tar.xz" ]
}
if ! ffmpeg_ready; then
  FFMPEG_VERSION="$ffmpeg_version" "$root/scripts/build-ffmpeg.sh"
  ffmpeg_ready || { echo "ffmpeg cache still incomplete after building" >&2; exit 1; }
fi

say "Assembling Musix.app"
rm -rf "$stage"; mkdir -p "$stage"
cp -a "$build/musix.app" "$stage/Musix.app"
app="$stage/Musix.app"

# Qt first, while the bundle is still only Qt's business. Its own macdeployqt,
# so the frameworks come from the Qt the app was linked against.
"$qt/bin/macdeployqt" "$app" -qmldir="$root/qml" -always-overwrite

say "Dropping what Musix never loads"
"$root/scripts/prune-bundle.sh" "$app" "$runtime/bin/python3"

# Qt's binaries are universal. There is no Intel build of anything else in the
# bundle, so the x86_64 half of Qt is weight nobody can run.
find "$app/Contents/Frameworks" "$app/Contents/PlugIns" -type f | while read -r f; do
  archs="$(lipo -archs "$f" 2>/dev/null)" || continue
  [ "$archs" = "arm64" ] && continue
  lipo "$f" -thin arm64 -output "$f.arm64" && mv "$f.arm64" "$f"
done

resources="$app/Contents/Resources"
mkdir -p "$resources/helper" "$resources/ffmpeg"
cp "$root/helper/catalog.py" "$root/helper/online_artwork.py" \
   "$root/helper/requirements.txt" "$resources/helper/"
cp "$cache/ffmpeg-out/bin/ffmpeg" "$cache/ffmpeg-out/bin/ffprobe" "$resources/ffmpeg/"
# cp -a copies into an existing directory rather than over it, which once
# produced runtime/python-runtime nested inside the bundle. Nothing here may
# depend on what the destination already held.
rm -rf "$resources/runtime"
cp -a "$runtime" "$resources/runtime"
cp "$root/LICENSE" "$root/NOTICE" "$resources/"

# Bytecode is rebuilt here rather than inherited, because what the cache
# happened to contain decided what shipped. Leaving it out is worse than
# untidy: the app runs from this read-only copy while the writable one is
# still being seeded, and Python would answer by writing .pyc back into
# Contents/Resources, which breaks the signature's resource seal.
#
# unchecked-hash keeps source mtimes out of the files and stops Python
# revalidating them, and stripping $resources keeps this machine's directory
# names out of every traceback the app can print.
#
# All of it runs against the staged copy rather than the cache, because a step
# that only ran when the cache was cold made the bundle depend on whether it
# had been built before. That is how idlelib and tkinter shipped in one build
# and not the next.
#
# The helper scripts count too, and for the same reason: catalog.py does
# "from online_artwork import lookup", which caches a sibling module next to
# itself inside Contents/Resources on the first artwork lookup of any ordinary
# session — seeded runtime or not.
rm -rf "$resources/runtime/lib/python3.11/"{idlelib,pydoc_data,test,tkinter}
find "$resources/runtime" "$resources/helper" -name __pycache__ -type d -prune \
  -exec rm -rf {} + 2>/dev/null || true
"$resources/runtime/bin/python3" -m compileall -q -f \
  --invalidation-mode unchecked-hash -s "$resources" \
  "$resources/runtime/lib/python3.11" "$resources/helper" >/dev/null

# The bundle redistributes other people's work, so it carries the full text of
# every one of their licenses rather than only naming them.
#
# Qt's own terms are the LGPL-3.0 and the GPL-3.0 it incorporates, kept in
# ./licenses because Qt's binaries ship no copy. The third-party code compiled
# into Qt -- harfbuzz, libpng, pcre2, freetype, masm and forty-odd more -- is
# written out of Qt's own qt_attribution.json files in the pinned module
# sources, by scripts/qt-licenses.py, which says what it leaves out and why.
licenses="$resources/licenses"
rm -rf "$licenses"; mkdir -p "$licenses"
cp "$root/licenses/LGPL-3.0.txt" "$licenses/Qt-LGPL-3.0.txt"
cp "$root/licenses/GPL-3.0.txt" "$licenses/Qt-LGPL-3.0-incorporates-GPL-3.0.txt"
python3 "$root/scripts/qt-licenses.py" "$cache/qt-src" "$licenses/Qt-third-party.txt" 2>/dev/null
cp "$root/licenses/MaterialSymbols-LICENSE.txt" "$licenses/"
cp "$root/licenses/Catppuccin-palette-MIT.txt" "$root/licenses/RosePine-palette-MIT.txt" "$licenses/"
cp "$cache/ffmpeg-$ffmpeg_version/COPYING.LGPLv2.1" "$licenses/FFmpeg-LGPL-2.1.txt"
cp "$runtime/lib/python3.11/LICENSE.txt" "$licenses/CPython-PSF.txt"
cp "$runtime"/lib/python3.11/site-packages/yt_dlp-*.dist-info/licenses/LICENSE \
   "$licenses/yt-dlp-Unlicense.txt"
cp "$runtime"/lib/python3.11/site-packages/ytmusicapi-*.dist-info/licenses/LICENSE \
   "$licenses/ytmusicapi-MIT.txt"

# What the license set above assumes about the bundle, checked against the
# bundle. Every framework must be Qt's; no loose library may ride along
# unlicensed; nothing may still point into Homebrew; and each thing
# qt-licenses.py leaves out must really be absent, or its exclusion was wrong.
"$runtime/bin/python3" - "$app" "$licenses" <<'AUDIT'
import sys, pathlib, subprocess
app, licenses = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
c = app / 'Contents'
problems = []
for fw in sorted((c / 'Frameworks').glob('*.framework')):
    if not fw.name.startswith('Qt'):
        problems.append('%s is bundled and is not part of Qt' % fw.name)
for lib in sorted((c / 'Frameworks').glob('*.dylib')):
    problems.append('%s is bundled and nothing in this build licenses it' % lib.name)
# Premises of the exclusions in scripts/qt-licenses.py.
absent = {
    'Frameworks/QtSql.framework': 'SQLite',
    'Frameworks/QtSpatialAudio.framework': 'Eigen, pffft and Resonance Audio',
    'Frameworks/QtTest.framework': "QtTest's third-party code",
    'PlugIns/imageformats/libqtiff.dylib': 'libtiff',
    'PlugIns/multimedia/libffmpegmediaplugin.dylib': "Qt's FFmpeg and Signalsmith Stretch",
    'Resources/qml/QtQuick/Controls/Material': 'the Material shadow values',
}
for path, what in absent.items():
    if (c / path).exists():
        problems.append('%s is in the bundle, so %s cannot be left out of the notices' % (path, what))
for lib in c.rglob('libav*.dylib'):
    problems.append('%s is in the bundle, so FFmpeg cannot be left out of the notices' % lib.relative_to(c))
for binary in [c / 'MacOS/musix'] + list((c / 'Frameworks').rglob('*')) + list((c / 'PlugIns').rglob('*.dylib')):
    if binary.is_file() and not binary.is_symlink():
        out = subprocess.run(['otool', '-L', str(binary)], capture_output=True, text=True).stdout
        if '/opt/homebrew' in out:
            problems.append('%s still links something in /opt/homebrew' % binary.relative_to(c))
for need in ('Qt-LGPL-3.0.txt', 'Qt-third-party.txt'):
    if not (licenses / need).is_file():
        problems.append('%s is missing from licenses/' % need)
if problems:
    print('Licensing gap:', file=sys.stderr)
    for m in problems:
        print('  ' + m, file=sys.stderr)
    sys.exit(1)
print('%d license texts; every framework is Qt, nothing unlicensed rides along' %
      len(list(licenses.iterdir())))
AUDIT

# One binary built for a newer macOS is enough to keep the app from launching
# on anything older, and it fails at load time with no message at all. Every
# Mach-O in the bundle is checked against the declared floor, and Info.plist
# has to say the same thing, so Finder refuses cleanly below it instead.
"$runtime/bin/python3" - "$app" "$deployment_target" <<'FLOOR'
import sys, pathlib, subprocess, plistlib
app, floor = pathlib.Path(sys.argv[1]), sys.argv[2]
key = lambda v: tuple(int(x) for x in v.split('.'))
newest, over = '0', []
for f in sorted(app.rglob('*')):
    if not f.is_file() or f.is_symlink():
        continue
    with open(f, 'rb') as fh:
        if fh.read(4) not in (b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe'):
            continue
    out = subprocess.run(['otool', '-l', str(f)], capture_output=True, text=True).stdout.split()
    for i, word in enumerate(out):
        if word == 'minos' or (word == 'version' and 'LC_VERSION_MIN_MACOSX' in out[max(0, i - 6):i]):
            m = out[i + 1]
            newest = max(newest, m, key=key)
            if key(m) > key(floor):
                over.append('%s  %s' % (m, f.relative_to(app)))
            break
declared = plistlib.loads((app / 'Contents/Info.plist').read_bytes()).get('LSMinimumSystemVersion')
if declared != floor:
    over.append('Info.plist says LSMinimumSystemVersion %s' % declared)
if over:
    print('Built for a newer macOS than %s:' % floor, file=sys.stderr)
    for o in over:
        print('  ' + o, file=sys.stderr)
    sys.exit(1)
print('every binary runs on macOS %s; the newest requirement found is %s' % (floor, newest))
FLOOR

# FFmpeg is LGPL-2.1, whose section 6 has no equivalent of GPLv3 6(d), so its
# source travels with the release rather than being pointed at. Qt is
# LGPL-3.0 and NOTICE gives the download.qt.io URL for it instead.
cp "$cache/ffmpeg-$ffmpeg_version.tar.xz" \
   "$root/Musix-$version-ffmpeg-$ffmpeg_version-source.tar.xz"

say "Signing (ad-hoc)"
# Unsigned arm64 code will not run at all; this is not notarization.
codesign --force --deep -s - "$app"
codesign --verify --deep "$app" && echo "signature ok"

say "Checking the signature survives being used"
# Python answers a missing .pyc by writing one, and Contents/Resources is
# inside the signature's resource seal, so an uncompiled module anywhere in
# the bundle turns a signed app into a damaged one the first time it is used.
# That has been introduced twice by hand. It is checked now.
"$resources/runtime/bin/python3" - "$app" <<'CHECK'
import sys, pathlib, importlib.util
app = pathlib.Path(sys.argv[1])
sources = sorted(app.rglob('*.py'))
missing = [str(p.relative_to(app)) for p in sources
           if not pathlib.Path(importlib.util.cache_from_source(str(p))).exists()]
if missing:
    print("Sources with no compiled bytecode; Python would write it at runtime:",
          file=sys.stderr)
    for path in missing[:25]:
        print("  " + path, file=sys.stderr)
    if len(missing) > 25:
        print("  ... and %d more" % (len(missing) - 25), file=sys.stderr)
    sys.exit(1)
print("%d .py files, all precompiled" % len(sources))
CHECK

# Anything in the bundle newer than this marker was written after signing.
marker="$cache/.sealed"; : > "$marker"
probe="$cache/seal-probe"; rm -rf "$probe"; mkdir -p "$probe/art" "$probe/scratch"
"$resources/runtime/bin/python3" - "$probe" <<'FIXTURE'
import sys, wave, struct, json, hashlib, time, pathlib
probe = pathlib.Path(sys.argv[1])
# A real WAV, so the metadata read reaches ffprobe instead of stopping at the
# extension check. Silence is enough; nothing here listens to it.
with wave.open(str(probe / 'tone.wav'), 'wb') as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(8000)
    w.writeframes(struct.pack('<800h', *([0] * 800)))
# A cache record that is still fresh, so the artwork lookup answers from disk.
# The import being tested happens at dispatch either way, and a build must not
# depend on somebody else's web service being reachable.
key = hashlib.sha256(json.dumps([4, '', '', '', '']).encode()).hexdigest()
(probe / 'art' / (key + '.json')).write_text(
    json.dumps({'expires': time.time() + 3600, 'status': 'retry'}))
FIXTURE

# The helper the way Backend::request runs it: one process, JSON in, JSON out,
# with the bundle's ffmpeg named on the environment.
helper() {
  SUNG_FFMPEG_DIR="$resources/ffmpeg" "$resources/runtime/bin/python3" \
    "$resources/helper/catalog.py" >/dev/null 2>&1 <<<"$1" || true
}
helper "{\"op\":\"local-files\",\"files\":[\"$probe/tone.wav\"],\"artDirectory\":\"$probe/art\"}"
helper "{\"op\":\"online-artwork\",\"artworkCache\":\"$probe/art\",\"scratch\":\"$probe/scratch\"}"
"$resources/runtime/bin/python3" -c "from yt_dlp import YoutubeDL
from ytmusicapi import YTMusic" >/dev/null

say "Smoke test"
# The app itself, launched from the bundle and used: a window, a local import
# through the bundled helper, playback and a seek, the mini player opened
# three times while playing, the themes and Settings. Any QML error fails it.
# It puts a window on screen for a few seconds. It runs on a throwaway profile
# and refuses to run on any other, and it clears the overrides a checkout
# sets, so only what the bundle carries is used.
smoke="$(mktemp -d "${TMPDIR:-/tmp}/musix-smoke.XXXXXX")"
if ! env -u SUNG_HELPER -u SUNG_PYTHON -u SUNG_FFMPEG_DIR MUSIX_PROFILE="$smoke" \
     "$app/Contents/MacOS/musix" --isolated --smoke-test 2>"$smoke.log"; then
  grep -v 'qt.qml.propertyCache\|qt.qpa.fonts' "$smoke.log" | tail -20 >&2
  rm -rf "$smoke" "$smoke.log"
  echo "The app failed its smoke test; no DMG is made from it." >&2
  exit 1
fi
rm -rf "$smoke" "$smoke.log"

written="$(find "$app" -newer "$marker")"
if [ -n "$written" ]; then
  echo "Using the app wrote into the signed bundle:" >&2
  printf '%s\n' "$written" | sed 's/^/  /' >&2
  exit 1
fi
codesign --verify --deep --strict "$app" ||
  { echo "The signature stopped verifying after ordinary use." >&2; exit 1; }
rm -rf "$probe" "$marker"
echo "helper, artwork, resolver and the app itself all ran; nothing written, seal intact"

say "DMG"
ln -sf /Applications "$stage/Applications"
dmg="$root/Musix-$version-arm64.dmg"
rm -f "$dmg"
hdiutil create -volname "Musix $version" -srcfolder "$stage" -ov -format UDZO \
  -quiet "$dmg"

say "Artifacts"
# These go up together: the DMG, and the source the LGPL entitles its
# recipients to for the one LGPL-2.1 component in it, ffmpeg. SHA256SUMS.txt
# is written last and covers the others, so it is also the list of what the
# release attaches.
artifacts=(
  "$dmg"
  "$root/Musix-$version-ffmpeg-$ffmpeg_version-source.tar.xz"
)
printf 'commit  %s\n' "$commit"
for f in "${artifacts[@]}"; do
  printf '\n%s\n  %s bytes (%s)\n  sha256  %s\n' \
    "$f" "$(stat -f '%z' "$f")" "$(du -h "$f" | cut -f1)" \
    "$(shasum -a 256 "$f" | cut -d' ' -f1)"
done
sums="$root/SHA256SUMS.txt"
( cd "$root" && shasum -a 256 "${artifacts[@]##*/}" ) > "$sums"
printf '\n%s\n' "$sums"
sed 's/^/  /' "$sums"
