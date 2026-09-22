#!/usr/bin/env bash
# Minimal, pure-LGPL, arm64 static ffmpeg/ffprobe for bundling.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
here="$root/build-packaging"
mkdir -p "$here"
ver="${FFMPEG_VERSION:-7.1}"
# The same floor as the app. Without it the compiler targets the build
# machine's own macOS and ffmpeg alone would keep the bundle off anything older.
target="${MACOSX_DEPLOYMENT_TARGET:-14.0}"
export MACOSX_DEPLOYMENT_TARGET="$target"
src="$here/ffmpeg-$ver"
out="$here/ffmpeg-out"
tarball="$here/ffmpeg-$ver.tar.xz"
# The tarball is checked separately from the tree it was extracted into: the
# release publishes it as the LGPL corresponding source, so a cache that kept
# only the tree still has to fetch it back. Downloading to .part first means an
# interrupted fetch cannot leave a truncated file that later looks cached.
if [ ! -f "$tarball" ]; then
  curl -fsSL "https://ffmpeg.org/releases/ffmpeg-$ver.tar.xz" -o "$tarball.part"
  mv "$tarball.part" "$tarball"
fi
[ -d "$src" ] || tar -xf "$tarball" -C "$here"
cd "$src"
# A tree configured for another deployment target is rebuilt from scratch.
if [ -f config.h ] && [ "$(cat .musix-target 2>/dev/null)" != "$target" ]; then
  make distclean >/dev/null 2>&1 || true
fi
[ -f config.h ] || ./configure \
  --prefix="$out" \
  --extra-cflags="-mmacosx-version-min=$target" \
  --extra-ldflags="-mmacosx-version-min=$target" \
  --disable-gpl --disable-nonfree --disable-autodetect \
  --disable-doc --disable-debug --enable-small \
  --disable-shared --enable-static \
  --disable-ffplay --disable-avdevice --disable-postproc \
  --disable-devices --disable-hwaccels --disable-bsfs \
  --disable-encoders --enable-encoder=mjpeg,png \
  --disable-muxers --enable-muxer=image2,mjpeg \
  --disable-filters --enable-filter=scale,null,anull,aformat,aresample,format \
  --disable-protocols --enable-protocol=file,pipe \
  --enable-ffmpeg --enable-ffprobe
echo "$target" > .musix-target
make -j"$(sysctl -n hw.ncpu)"
make install
echo "$target" > "$out/.musix-target"
ls -lh "$out/bin/"
