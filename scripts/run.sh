#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
export SUNG_HELPER="$root/helper/catalog.py"
export SUNG_PYTHON="$root/runtime/bin/python"
# A development build on the real library is the one run here that can change
# it, so it gets a backup first.
if [ -z "${MUSIX_PROFILE:-}" ]; then python3 "$root/scripts/backup-profile.py" run-sh || exit 1; fi
bundled="$root/build/musix.app/Contents/MacOS/musix"
[ -x "$bundled" ] && exec "$bundled" "$@"
exec "$root/build/musix" "$@"
