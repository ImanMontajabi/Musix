#!/usr/bin/env bash
# Every suite, before a release. Any failure stops the release.
#
# 0.14.1 and 0.15.0 shipped with UI suites failing, because nothing in the
# release ran them: package-dmg.sh runs the smoke test, test.sh runs ctest and
# the Python tests, and the fifty UI suites lived only in diagnostics builds.
# This runs all of it: ctest and the Python tests, then verify.py with the live
# stages, which builds a diagnostics app and runs every UI suite headless, each
# on a profile of its own. It needs the network for the live stages.
set -uo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
out="$root/verification/release-$(date +%Y%m%d-%H%M%S)"

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

echo
echo "ctest and Python:  $([ $unit -eq 0 ] && echo passed || echo "FAILED (exit $unit)")"
echo "UI and live suites: $([ $suites -eq 0 ] && echo passed || echo "FAILED, see $out/report.txt")"
[ $suites -ne 0 ] && grep -v '^PASS' "$out/report.txt"
echo "Streamed song length: $([ $streamed -eq 0 ] && echo passed || echo "MISSING")"
[ $unit -eq 0 ] && [ $suites -eq 0 ] && [ $streamed -eq 0 ]
