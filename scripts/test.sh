#!/usr/bin/env bash
# Both suites always run, and the script fails if either did. It used to stop at
# the first failure under set -e, so a red ctest meant the Python suite was
# never run at all and its result was never seen.
set -uo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
set -e
cmake -S "$root" -B "$root/build-tests" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DSUNG_DIAGNOSTICS=OFF
cmake --build "$root/build-tests" --parallel "${SUNG_BUILD_JOBS:-4}"
set +e
QT_QPA_PLATFORMTHEME=generic ctest --test-dir "$root/build-tests" --output-on-failure
native=$?
python3 -m unittest discover -s "$root/tests" -p 'test_*.py'
python=$?
echo
echo "C++ (ctest):    $([ $native -eq 0 ] && echo passed || echo "FAILED (exit $native)")"
echo "Python:         $([ $python -eq 0 ] && echo passed || echo "FAILED (exit $python)")"
[ $native -eq 0 ] && [ $python -eq 0 ]
