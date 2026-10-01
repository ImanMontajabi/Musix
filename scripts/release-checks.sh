#!/usr/bin/env bash
# Every suite, before a release. Any failure stops the release.
#
# 0.14.1 and 0.15.0 shipped with UI suites failing, because nothing in the
# release ran them: package-dmg.sh runs the smoke test, test.sh runs ctest and
# the Python tests, and the fifty UI suites lived only in diagnostics builds.
# This runs all of it: ctest and the Python tests, then verify.py with the live
# stages, which builds a diagnostics app and runs every UI suite headless, each
# on a profile of its own. It needs the network for the live stages.
#
# The live stages fetch songs with the ffmpeg in build-packaging/ffmpeg-out/bin;
# what a user's Mac runs is the copy inside the DMG. dmg_ffmpeg compares the two
# byte for byte. The gate runs before the DMG exists, so with the DMG for this
# version already built it checks it, and otherwise says plainly that it did
# not. `--dmg-only` runs just that check and needs the DMG: run it after
# package-dmg.sh and before tagging.
set -uo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
out="$root/verification/release-$(date +%Y%m%d-%H%M%S)"

# 0 identical, 1 differ or unreadable, 2 no DMG for this version yet.
dmg_ffmpeg() {
  local version dmg mount tool a b rc=0
  version="$(sed -n 's/^project(Musix VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")"
  dmg="$root/Musix-$version-arm64.dmg"
  [ -f "$dmg" ] || { echo "no $dmg" >&2; return 2; }
  mount="$(mktemp -d)"
  hdiutil attach -readonly -nobrowse -noverify -mountpoint "$mount" "$dmg" >/dev/null || { rmdir "$mount"; echo "could not open $dmg" >&2; return 1; }
  for tool in ffmpeg ffprobe; do
    a="$(shasum -a 256 < "$root/build-packaging/ffmpeg-out/bin/$tool" 2>/dev/null | cut -d' ' -f1)"
    b="$(shasum -a 256 < "$mount/Musix.app/Contents/Resources/ffmpeg/$tool" 2>/dev/null | cut -d' ' -f1)"
    if [ -n "$a" ] && [ "$a" = "$b" ]; then echo "$tool in the DMG is the one the live checks use: $a"
    else echo "$tool differs: build-packaging/ffmpeg-out/bin has ${a:-nothing}, the DMG has ${b:-nothing}" >&2; rc=1; fi
  done
  hdiutil detach "$mount" >/dev/null 2>&1; rmdir "$mount" 2>/dev/null
  return $rc
}
if [ "${1:-}" = "--dmg-only" ]; then dmg_ffmpeg; rc=$?; [ $rc -eq 2 ] && echo "build the DMG first" >&2; exit $rc; fi

# The live stages fetch songs the way the packaged app does, with the ffmpeg
# that ships; this builds it if it is not already built (a no-op otherwise).
"$root/scripts/build-ffmpeg.sh" > "$root/build-packaging/ffmpeg-check.log" 2>&1 || { echo "building the packaged ffmpeg failed; see build-packaging/ffmpeg-check.log" >&2; exit 1; }
export SUNG_PACKAGED_FFMPEG_DIR="$root/build-packaging/ffmpeg-out/bin"
"$root/scripts/test.sh"
unit=$?
python3 "$root/tests/verify.py" --build-dir "$root/build-diag" --output "$out"
suites=$?

# 0.14.1 through 0.15.1 showed every YouTube song at twice its length for anyone
# who opened Musix from Finder, and every suite was green: the length check only
# runs if the live search returned a song, and not at all under --offline. So
# the release does not pass unless that check ran, under a bare PATH with the
# ffmpeg that ships, and passed. verify.py's own summary can't say that.
streamed=1
if grep -q '^PASS a streamed song shows its real length$' "$out/ui-playback.log" 2>/dev/null &&
   grep -q '^STREAMED_LENGTH shown ' "$out/ui-playback.log"; then
  streamed=0
  grep '^STREAMED_LENGTH' "$out/ui-playback.log"
else
  echo "the streamed-song length check did not run and pass in $out/ui-playback.log; a release needs it" >&2
fi

dmg_ffmpeg; dmgff=$?

echo
echo "ctest and Python:  $([ $unit -eq 0 ] && echo passed || echo "FAILED (exit $unit)")"
echo "UI and live suites: $([ $suites -eq 0 ] && echo passed || echo "FAILED, see $out/report.txt")"
[ $suites -ne 0 ] && grep -v '^PASS' "$out/report.txt"
echo "Streamed song length: $([ $streamed -eq 0 ] && echo passed || echo "MISSING")"
echo "ffmpeg in the DMG:  $([ $dmgff -eq 0 ] && echo identical || [ $dmgff -eq 2 ] && echo "NOT CHECKED, no DMG yet: run release-checks.sh --dmg-only after package-dmg.sh" || echo "DIFFERS")"
[ $unit -eq 0 ] && [ $suites -eq 0 ] && [ $streamed -eq 0 ] && [ $dmgff -ne 1 ]
