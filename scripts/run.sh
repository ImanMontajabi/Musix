#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
export SUNG_HELPER="$root/helper/catalog.py"
export SUNG_PYTHON="$root/runtime/bin/python"
bundled="$root/build/musix.app/Contents/MacOS/musix"
[ -x "$bundled" ] && exec "$bundled" "$@"
exec "$root/build/musix" "$@"
