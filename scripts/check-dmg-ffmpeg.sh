#!/usr/bin/env bash
# The ffmpeg and ffprobe inside a built DMG must be byte-identical to the ones
# in build-packaging/ffmpeg-out/bin, the copies the release gate's live checks
# fetch songs with. Otherwise what was tested is not what a user's Mac runs.
# Usage: check-dmg-ffmpeg.sh <dmg> [<ffmpeg-bin-dir>]. Exit 0 identical, 1 differ
# or unreadable.
set -uo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
dmg="${1:?usage: check-dmg-ffmpeg.sh <dmg> [<ffmpeg-bin-dir>]}"
bin="${2:-$root/build-packaging/ffmpeg-out/bin}"
[ -f "$dmg" ] || { echo "no $dmg" >&2; exit 1; }
mount="$(mktemp -d)"
hdiutil attach -readonly -nobrowse -noverify -mountpoint "$mount" "$dmg" >/dev/null 2>&1 || { rmdir "$mount"; echo "could not open $dmg" >&2; exit 1; }
rc=0
for tool in ffmpeg ffprobe; do
  a="$(shasum -a 256 < "$bin/$tool" 2>/dev/null | cut -d' ' -f1)"
  b="$(shasum -a 256 < "$mount/Musix.app/Contents/Resources/ffmpeg/$tool" 2>/dev/null | cut -d' ' -f1)"
  if [ -n "$a" ] && [ "$a" = "$b" ]; then echo "$tool in the DMG is the one the live checks use: $a"
  else echo "$tool differs: $bin has ${a:-nothing}, the DMG has ${b:-nothing}" >&2; rc=1; fi
done
hdiutil detach "$mount" >/dev/null 2>&1; rmdir "$mount" 2>/dev/null
exit $rc
