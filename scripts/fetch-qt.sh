#!/usr/bin/env bash
# Qt for the macOS bundle: The Qt Company's own binaries, not Homebrew's.
#
# Homebrew builds every bottle on the newest macOS and stamps that version on it
# as a minimum: QtQuick and QtMultimedia came out requiring macOS 27, and the
# twenty-six Homebrew libraries Qt dragged in required 26 or 27. No setting on
# our side can lower a prebuilt library's floor. Qt's official 6.11.2 binaries
# target macOS 13 and link nothing outside macOS and Qt, which is the only way
# the app can honestly run on the macOS the project declares.
#
# The module sources are fetched too, pinned by the SHA-256 The Qt Company
# publishes beside them: they are where the license texts for the third-party
# code compiled into Qt come from, and they are what NOTICE points at as Qt's
# corresponding source.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cache="$root/build-packaging"
qt_version="6.11.2"
aqt_version="3.3.0"
tools="$cache/tools/aqt"
qt="$cache/qt"
src="$cache/qt-src"
mkdir -p "$cache/tools" "$src"

# The downloader lives in a venv of its own. It is a build tool: nothing in it
# goes near the bundle, and it is kept apart from python-runtime, which does.
if ! "$tools/bin/aqt" version 2>/dev/null | grep -q "$aqt_version"; then
  rm -rf "$tools"
  python3 -m venv "$tools"
  "$tools/bin/pip" install --quiet --disable-pip-version-check "aqtinstall==$aqt_version"
fi

prefix="$qt/$qt_version/macos"
qt_ready() {
  [ -f "$prefix/lib/QtCore.framework/Versions/A/QtCore" ] &&
    [ -f "$prefix/lib/QtMultimedia.framework/Versions/A/QtMultimedia" ] &&
    [ -f "$prefix/plugins/imageformats/libqwebp.dylib" ] &&
    [ -x "$prefix/bin/macdeployqt" ]
}
if ! qt_ready; then
  rm -rf "$qt"
  # aqt checks every archive against the hash in Qt's repository metadata.
  "$tools/bin/aqt" install-qt mac desktop "$qt_version" clang_64 \
    -m qtmultimedia qtimageformats -O "$qt"
  qt_ready || { echo "Qt $qt_version is incomplete after installing" >&2; exit 1; }
fi

base="https://download.qt.io/official_releases/qt/${qt_version%.*}/$qt_version/submodules"
while read -r sha module; do
  file="$src/$module-everywhere-src-$qt_version.tar.xz"
  [ -f "$file" ] && [ "$(shasum -a 256 "$file" | cut -d' ' -f1)" = "$sha" ] && continue
  curl -fsSL --retry 3 -o "$file.part" "$base/${file##*/}"
  got="$(shasum -a 256 "$file.part" | cut -d' ' -f1)"
  [ "$got" = "$sha" ] || { echo "${file##*/} hashed $got, expected $sha" >&2; rm -f "$file.part"; exit 1; }
  mv "$file.part" "$file"
done <<'PINS'
5b2e00eccaf5a4d8c14134ffa0ea8dfd0a35ae1ffc7f8d87fa4305a1ed23cf22 qtbase
215b7b70517e380123eabc6b92243f3c47b6f016a91d126057dbe53551c6b430 qtdeclarative
967b5e02ec6b793cdb360622cd6e703132836af983208d678dae4b50f109cd9f qtmultimedia
d594337feca84c26fb67fe87b85e6a5c12fda404b611d905f9d138210c311876 qtsvg
cecd8900f34b6550076309bc94f62f828008b633a4239e0a08c86788f41001f8 qtimageformats
PINS
echo "$prefix"
