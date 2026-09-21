#!/usr/bin/env bash
set -euo pipefail
prefix="${1:-$HOME/.local}"
rm -f -- "$prefix/bin/musix" "$prefix/share/applications/musix.desktop" "$prefix/share/icons/hicolor/scalable/apps/musix.svg" "$prefix/share/icons/hicolor/512x512/apps/musix.png"
if command -v gtk-update-icon-cache >/dev/null; then gtk-update-icon-cache -f -t "$prefix/share/icons/hicolor"; fi
rm -rf -- "$prefix/lib/musix" "$prefix/share/licenses/musix"
if command -v update-desktop-database >/dev/null; then update-desktop-database "$prefix/share/applications"; fi
printf 'Musix removed. Your library and settings were kept.\n'
