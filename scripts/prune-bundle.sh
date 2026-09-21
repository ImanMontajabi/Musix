#!/usr/bin/env bash
# Remove the Qt plugins, QML modules and libraries Musix cannot reach.
#
# macdeployqt deploys every plugin Qt has rather than the ones an app loads,
# which is why the build printed install_name_tool errors about frameworks
# that were never there: QtVirtualKeyboard, QtPdf, Qt3D, QtQuickTimeline,
# QtHunspellInputMethod. Those plugins could not have loaded. The rest are
# styles Musix does not use and image formats its artwork cannot be in.
set -euo pipefail
app="$1"; py="$2"
c="$app/Contents"

# Plugins are found by scanning a directory, so this is the whole contract.
# The image formats are the four artwork can arrive in. Local cover art is
# re-encoded to JPEG by the bundled ffmpeg before QImage ever sees it, and
# sidecar art is limited to ART_EXTENSIONS in helper/catalog.py, so TIFF, JP2,
# MNG, TGA, BMP and HEIF have no path into the app. Dropping those four takes
# libtiff, libjasper, libmng, liblcms2 and liblzma out of the bundle with them.
# icns and ico stay for the window and file icons Qt draws.
keep_plugins="
platforms/libqcocoa.dylib
multimedia/libdarwinmediaplugin.dylib
networkinformation/libqapplenetworkinformation.dylib
iconengines/libqsvgicon.dylib
imageformats/libqgif.dylib
imageformats/libqjpeg.dylib
imageformats/libqwebp.dylib
imageformats/libqsvg.dylib
imageformats/libqicns.dylib
imageformats/libqico.dylib
tls/libqopensslbackend.dylib
tls/libqsecuretransportbackend.dylib
tls/libqcertonlybackend.dylib
quick/libqtqmlcoreplugin.dylib
quick/libqmlplugin.dylib
quick/libmodelsplugin.dylib
quick/libworkerscriptplugin.dylib
quick/libqtquick2plugin.dylib
quick/libquickwindowplugin.dylib
quick/libqquicklayoutsplugin.dylib
quick/libqmlshapesplugin.dylib
quick/libqtquicktemplates2plugin.dylib
quick/libqtquickcontrols2plugin.dylib
quick/libqtquickcontrols2implplugin.dylib
quick/libqtquickcontrols2basicstyleplugin.dylib
quick/libqtquickcontrols2basicstyleimplplugin.dylib
quick/libqtquickdialogsplugin.dylib
quick/libqtquickdialogs2quickimplplugin.dylib
"
# A name here that Qt has renamed or dropped would silently stop being shipped,
# and the app would fail at runtime instead of here.
while IFS= read -r p; do
  [ -n "$p" ] || continue
  [ -e "$c/PlugIns/$p" ] || { echo "keep-list names a plugin that is not in the bundle: $p" >&2; exit 1; }
done <<<"$keep_plugins"
find "$c/PlugIns" -name '*.dylib' | while read -r f; do
  grep -qxF "${f#"$c/PlugIns/"}" <<<"$keep_plugins" || rm -f "$f"
done
find "$c/PlugIns" -type d -empty -delete

# The modules main.qml and its imports name, plus the ones those pull in
# without saying so in a QML import: QtQml's qmldir imports QtQml.Models and
# QtQml.WorkerScript "auto", the Basic style is chosen in C++ by
# QQuickStyle::setStyle rather than imported, and Basic's controls come from
# QtQuick.Controls.impl and QtQuick.Controls.Basic.impl.
keep_qml="
QML
QtCore
QtQml
QtQml/Models
QtQml/WorkerScript
QtQuick
QtQuick/Window
QtQuick/Layouts
QtQuick/Shapes
QtQuick/Templates
QtQuick/Controls
QtQuick/Controls/impl
QtQuick/Controls/Basic
QtQuick/Controls/Basic/impl
QtQuick/Dialogs
QtQuick/Dialogs/quickimpl
"
# Kept modules carry resource directories that are not modules themselves and
# are shipped with them; these are the exceptions, Qt Design Studio metadata
# that only a designer tool reads.
drop_qml="
QtQuick/Controls/designer
"
qml="$c/Resources/qml"
# A directory holding a qmldir is a module, and a module is shipped only if it
# is named above: QtQml/StateMachine sits inside QtQml's directory but is not
# part of QtQml. Anything else is either a resource directory belonging to a
# module that is kept, or a directory on the way to one, which keeps no files
# of its own. No depth limit: QtQuick/Controls/Basic/impl is a module four
# levels down, and the first version of this stopped at three and kept it by
# accident.
keep_dir() {
  local rel="$1" m inside=1 waypoint=1
  grep -qxF "$rel" <<<"$drop_qml" && return 1
  while IFS= read -r m; do
    [ -n "$m" ] || continue
    [ "$rel" = "$m" ] && return 0
    [ "${rel#"$m/"}" != "$rel" ] && inside=0
    [ "${m#"$rel/"}" != "$m" ] && waypoint=0
  done <<<"$keep_qml"
  [ -f "$qml/$rel/qmldir" ] && return 1        # a module nobody asked for
  [ $inside = 0 ] && return 0                  # a kept module's own resources
  [ $waypoint = 0 ] && return 2
  return 1
}
find "$qml" -mindepth 1 -type d -depth | while read -r d; do
  rel="${d#"$qml/"}"
  keep_dir "$rel" && continue
  case $? in
    2) find "$d" -maxdepth 1 -type f -delete;;  # a waypoint keeps no files
    *) rm -rf "$d";;
  esac
done

# Frameworks are not listed by hand. Whatever the code that is left cannot
# reach through its load commands is not in the app, whatever it is called.
"$py" - "$app" <<'PY'
import sys, subprocess, pathlib, shutil
app = pathlib.Path(sys.argv[1]); fw = app / 'Contents/Frameworks'
roots = [app / 'Contents/MacOS/musix'] + sorted((app / 'Contents/PlugIns').rglob('*.dylib'))
def deps(binary):
    out = subprocess.run(['otool', '-L', str(binary)], capture_output=True, text=True).stdout
    for line in out.splitlines()[1:]:
        ref = line.strip().split(' (')[0]
        # macdeployqt rewrites everything it moved to @rpath, and the rpath is
        # Contents/Frameworks. Anything else is a macOS system library.
        for prefix in ('@rpath/', '@executable_path/../Frameworks/', '@loader_path/../'):
            if ref.startswith(prefix):
                yield fw / ref[len(prefix):]
                break
seen, queue = set(), list(roots)
while queue:
    binary = queue.pop().resolve()
    if binary in seen or not binary.exists():
        continue
    seen.add(binary)
    queue.extend(deps(binary))
for entry in sorted(fw.iterdir()):
    payload = entry / 'Versions/A' / entry.name[:-10] if entry.name.endswith('.framework') else entry
    if payload.resolve() in seen:
        continue
    print('  dropped ' + entry.name)
    shutil.rmtree(entry) if entry.is_dir() else entry.unlink()
PY
