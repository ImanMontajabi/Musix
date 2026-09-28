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

"$root/scripts/test.sh"
unit=$?
python3 "$root/tests/verify.py" --build-dir "$root/build-diag" --output "$out"
suites=$?

echo
echo "ctest and Python:  $([ $unit -eq 0 ] && echo passed || echo "FAILED (exit $unit)")"
echo "UI and live suites: $([ $suites -eq 0 ] && echo passed || echo "FAILED, see $out/report.txt")"
[ $suites -ne 0 ] && grep -v '^PASS' "$out/report.txt"
[ $unit -eq 0 ] && [ $suites -eq 0 ]
