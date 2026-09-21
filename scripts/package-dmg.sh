#!/usr/bin/env bash
# Build a self-contained Musix.app and wrap it in a DMG.
#
# The result must run on a Mac with no Homebrew, no Python and no ffmpeg, so
# the bundle carries Qt, a relocatable Python with the resolver packages, and
# a pure-LGPL ffmpeg. Order matters: macdeployqt rewrites what it finds, so it
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
# The LGPL-2.1 libraries macdeployqt brings in, whose source ships with the
# release. Keep in step with scripts/fetch-lgpl-sources.sh.
glib_version="2.90.0"
gettext_version="1.0"
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
printf 'Musix %s from %s\n' "$version" "$commit"

say "Release build"
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DSUNG_DIAGNOSTICS=OFF
cmake --build "$build" --parallel "${SUNG_BUILD_JOBS:-8}"

say "Python runtime with the resolver"
runtime="$cache/python-runtime"
python_ready() {
  [ -x "$runtime/bin/python3" ] && [ -f "$runtime/lib/python3.11/LICENSE.txt" ] &&
    "$runtime/bin/python3" -c "from yt_dlp import YoutubeDL
from ytmusicapi import YTMusic" >/dev/null 2>&1
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
  python_ready || { echo "python runtime still incomplete after installing" >&2; exit 1; }
fi

say "ffmpeg"
# Three separate things are taken out of this cache later: the two binaries,
# the license text, and the tarball the release publishes as LGPL source. Any
# one of them missing has to send the build back, because skipping it only
# defers the failure to a cp further down.
ffmpeg_ready() {
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

# Qt first, while the bundle is still only Qt's business.
macdeployqt "$app" -qmldir="$root/qml" -always-overwrite

say "Dropping what Musix never loads"
"$root/scripts/prune-bundle.sh" "$app" "$runtime/bin/python3"

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
# every one of their licenses rather than only naming them. macdeployqt copies
# in Qt and a pile of Homebrew libraries without saying anything about their
# terms, so those are listed here too. NOTICE says which option was taken for
# the components offered under more than one.
#
# Most texts come out of the keg that supplied the dylib, so they track the
# version actually bundled. The rest are in ./licenses because the keg ships
# no copy: Qt's has no license file at all, FreeType's points at a docs/ file
# it does not install, libjpeg-turbo's cites a README.ijg that is not there,
# dbus's names LICENSES/AFL-2.1.txt which is also not there, and gettext's is
# the GPL that covers the tools rather than the LGPL that covers libintl.
licenses="$resources/licenses"
rm -rf "$licenses"; mkdir -p "$licenses"
brew_licenses="
Qt-LGPL-3.0.txt                     $root/licenses/LGPL-3.0.txt
Qt-LGPL-3.0-incorporates-GPL-3.0.txt $root/licenses/GPL-3.0.txt
glib-LGPL-2.1.txt                   /opt/homebrew/opt/glib/LGPL-2.1-or-later.txt
gettext-libintl-LGPL-2.1.txt        $root/licenses/LGPL-2.1.txt
dbus-AFL-2.1.txt                    $root/licenses/AFL-2.1.txt
dbus-COPYING.txt                    /opt/homebrew/opt/dbus/COPYING
freetype-FTL.txt                    $root/licenses/freetype-FTL.txt
freetype-LICENSE.txt                /opt/homebrew/opt/freetype/LICENSE.TXT
libjpeg-turbo-LICENSE.txt           /opt/homebrew/opt/jpeg-turbo/LICENSE.md
libjpeg-turbo-IJG-README.txt        $root/licenses/libjpeg-turbo-README.ijg.txt
brotli-MIT.txt                      /opt/homebrew/opt/brotli/LICENSE
double-conversion-BSD-3-Clause.txt  /opt/homebrew/opt/double-conversion/LICENSE
graphite2-LICENSE.txt               /opt/homebrew/opt/graphite2/LICENSE
harfbuzz-MIT.txt                    /opt/homebrew/opt/harfbuzz/COPYING
icu-Unicode-3.0.txt                 /opt/homebrew/opt/icu4c@78/LICENSE
libb2-CC0-1.0.txt                   /opt/homebrew/opt/libb2/COPYING
libpng-libpng-2.0.txt               /opt/homebrew/opt/libpng/LICENSE
md4c-MIT.txt                        /opt/homebrew/opt/md4c/LICENSE.md
openssl-Apache-2.0.txt              /opt/homebrew/opt/openssl@3/LICENSE.txt
pcre2-BSD-3-Clause.txt              /opt/homebrew/opt/pcre2/LICENCE.md
libwebp-BSD-3-Clause.txt            /opt/homebrew/opt/webp/COPYING
zstd-BSD-3-Clause.txt               /opt/homebrew/opt/zstd/LICENSE
"
while read -r dest src; do
  [ -n "$dest" ] || continue
  [ -f "$src" ] || { echo "no license text at $src (for $dest)" >&2; exit 1; }
  cp "$src" "$licenses/$dest"
done <<<"$brew_licenses"

cp "$root/licenses/MaterialSymbols-LICENSE.txt" "$licenses/"
cp "$cache/ffmpeg-$ffmpeg_version/COPYING.LGPLv2.1" "$licenses/FFmpeg-LGPL-2.1.txt"
cp "$runtime/lib/python3.11/LICENSE.txt" "$licenses/CPython-PSF.txt"
cp "$runtime"/lib/python3.11/site-packages/yt_dlp-*.dist-info/licenses/LICENSE \
   "$licenses/yt-dlp-Unlicense.txt"
cp "$runtime"/lib/python3.11/site-packages/ytmusicapi-*.dist-info/licenses/LICENSE \
   "$licenses/ytmusicapi-MIT.txt"

# A library that arrives in the bundle without a license text is the failure
# this is here to catch, so the check is against what is actually there.
"$runtime/bin/python3" - "$app" "$licenses" <<'AUDIT'
import sys, pathlib, re
app, licenses = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
# dylib stem -> the license file that covers it.
covered = {
    'libb2': 'libb2', 'libbrotlicommon': 'brotli', 'libbrotlidec': 'brotli',
    'libcrypto': 'openssl', 'libssl': 'openssl', 'libdbus-1': 'dbus',
    'libdouble-conversion': 'double-conversion', 'libfreetype': 'freetype',
    'libglib-2.0': 'glib', 'libgthread-2.0': 'glib', 'libgraphite2': 'graphite2',
    'libharfbuzz': 'harfbuzz', 'libicudata': 'icu', 'libicui18n': 'icu',
    'libicuuc': 'icu', 'libintl': 'gettext', 'libjpeg': 'libjpeg-turbo',
    'libmd4c': 'md4c', 'libpcre2-8': 'pcre2', 'libpcre2-16': 'pcre2',
    'libpng16': 'libpng', 'libsharpyuv': 'libwebp', 'libwebp': 'libwebp',
    'libwebpdemux': 'libwebp', 'libwebpmux': 'libwebp', 'libzstd': 'zstd',
}
names = [p.name for p in licenses.iterdir()]
missing = []
for lib in sorted((app / 'Contents/Frameworks').glob('*.dylib')):
    stem = re.sub(r'\.[0-9.]*dylib$', '', lib.name)
    key = covered.get(stem)
    if key is None:
        missing.append('%s is bundled and nothing in this build claims to license it' % lib.name)
    elif not any(n.startswith(key) for n in names):
        missing.append('%s needs the %s license text, which is not in licenses/' % (lib.name, key))
if not any(n.startswith('Qt-') for n in names):
    missing.append('Qt frameworks are bundled with no Qt license text')
if missing:
    print('Licensing gap:', file=sys.stderr)
    for m in missing:
        print('  ' + m, file=sys.stderr)
    sys.exit(1)
print('%d license texts cover every bundled library' % len(names))
AUDIT

# LGPL redistribution means the corresponding source has to be available.
# ffmpeg, glib and libintl are LGPL-2.1, whose section 6 has no equivalent of
# GPLv3 6(d), so their source travels with the release rather than being
# pointed at. Qt is LGPL-3.0 and NOTICE gives the download.qt.io URL instead.
cp "$cache/ffmpeg-$ffmpeg_version.tar.xz" \
   "$root/Musix-$version-ffmpeg-$ffmpeg_version-source.tar.xz"
lgpl="$cache/lgpl-sources"
lgpl_ready() { [ -f "$lgpl/glib-$glib_version-source.tar.xz" ] &&
               [ -f "$lgpl/gettext-$gettext_version-source.tar.xz" ]; }
if ! lgpl_ready; then
  "$root/scripts/fetch-lgpl-sources.sh"
  lgpl_ready || { echo "LGPL source archives still missing after fetching" >&2; exit 1; }
fi
cp "$lgpl/glib-$glib_version-source.tar.xz" \
   "$root/Musix-$version-glib-$glib_version-source.tar.xz"
cp "$lgpl/gettext-$gettext_version-source.tar.xz" \
   "$root/Musix-$version-gettext-$gettext_version-source.tar.xz"

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

written="$(find "$app" -newer "$marker")"
if [ -n "$written" ]; then
  echo "Using the app wrote into the signed bundle:" >&2
  printf '%s\n' "$written" | sed 's/^/  /' >&2
  exit 1
fi
codesign --verify --deep --strict "$app" ||
  { echo "The signature stopped verifying after ordinary use." >&2; exit 1; }
rm -rf "$probe" "$marker"
echo "helper, artwork and resolver all ran; nothing written, seal intact"

say "DMG"
ln -sf /Applications "$stage/Applications"
dmg="$root/Musix-$version-arm64.dmg"
rm -f "$dmg"
hdiutil create -volname "Musix $version" -srcfolder "$stage" -ov -format UDZO \
  -quiet "$dmg"

say "Artifacts"
# These go up together: the DMG, and the source the LGPL entitles its
# recipients to for each of the three LGPL-2.1 libraries in it. SHA256SUMS.txt
# is written last and covers the others, so it is also the list of what the
# release attaches.
artifacts=(
  "$dmg"
  "$root/Musix-$version-ffmpeg-$ffmpeg_version-source.tar.xz"
  "$root/Musix-$version-glib-$glib_version-source.tar.xz"
  "$root/Musix-$version-gettext-$gettext_version-source.tar.xz"
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
