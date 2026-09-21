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
mkdir -p "$cache"

say() { printf '\n== %s ==\n' "$1"; }

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
  find "$runtime" -name '__pycache__' -type d -prune -exec rm -rf {} + 2>/dev/null || true
  rm -rf "$runtime/lib/python3.11/"{idlelib,pydoc_data,test,tkinter} 2>/dev/null || true
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

resources="$app/Contents/Resources"
mkdir -p "$resources/helper" "$resources/ffmpeg"
cp "$root/helper/catalog.py" "$root/helper/online_artwork.py" \
   "$root/helper/requirements.txt" "$resources/helper/"
cp "$cache/ffmpeg-out/bin/ffmpeg" "$cache/ffmpeg-out/bin/ffprobe" "$resources/ffmpeg/"
cp -a "$runtime" "$resources/runtime"
cp "$root/LICENSE" "$root/NOTICE" "$resources/"

# The bundle redistributes other people's work, so it carries their licenses
# rather than only naming them.
licenses="$resources/licenses"
mkdir -p "$licenses"
cp "$root/licenses/MaterialSymbols-LICENSE.txt" "$licenses/"
cp "$cache/ffmpeg-$ffmpeg_version/COPYING.LGPLv2.1" "$licenses/FFmpeg-LGPL-2.1.txt"
cp "$runtime/lib/python3.11/LICENSE.txt" "$licenses/CPython-PSF.txt"
cp "$runtime"/lib/python3.11/site-packages/yt_dlp-*.dist-info/licenses/LICENSE \
   "$licenses/yt-dlp-Unlicense.txt"
cp "$runtime"/lib/python3.11/site-packages/ytmusicapi-*.dist-info/licenses/LICENSE \
   "$licenses/ytmusicapi-MIT.txt"

# LGPL redistribution means the corresponding source has to be available. Put
# the exact tarball this ffmpeg was built from beside the DMG for the release.
cp "$cache/ffmpeg-$ffmpeg_version.tar.xz" \
   "$root/Musix-$version-ffmpeg-$ffmpeg_version-source.tar.xz"

say "Signing (ad-hoc)"
# Unsigned arm64 code will not run at all; this is not notarization.
codesign --force --deep -s - "$app"
codesign --verify --deep "$app" && echo "signature ok"

say "DMG"
ln -sf /Applications "$stage/Applications"
dmg="$root/Musix-$version-arm64.dmg"
rm -f "$dmg"
hdiutil create -volname "Musix $version" -srcfolder "$stage" -ov -format UDZO \
  -quiet "$dmg"
printf '\n%s (%s)\n' "$dmg" "$(du -h "$dmg" | cut -f1)"
