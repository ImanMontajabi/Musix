#!/usr/bin/env bash
# Minimal, pure-LGPL, arm64 static ffmpeg/ffprobe for bundling.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
here="$root/build-packaging"
mkdir -p "$here"
ver="${FFMPEG_VERSION:-7.1}"
src="$here/ffmpeg-$ver"
out="$here/ffmpeg-out"
[ -d "$src" ] || {
  curl -fsSL "https://ffmpeg.org/releases/ffmpeg-$ver.tar.xz" -o "$here/ffmpeg-$ver.tar.xz"
  tar -xf "$here/ffmpeg-$ver.tar.xz" -C "$here"
}
cd "$src"
[ -f config.h ] || ./configure \
  --prefix="$out" \
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
make -j"$(sysctl -n hw.ncpu)"
make install
ls -lh "$out/bin/"
