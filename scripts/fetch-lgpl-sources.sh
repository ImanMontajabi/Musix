#!/usr/bin/env bash
# Corresponding source for the LGPL-2.1 libraries the bundle carries.
#
# Qt is LGPL-3.0, and GPLv3 section 6(d) lets that one be satisfied by pointing
# at download.qt.io, which NOTICE does. LGPL-2.1 has no such clause: section
# 6(d) says "from the same place", so glib and gettext travel with the release.
#
# Everything here is pinned by checksum. glib's bottle is built from a patched
# tree, so pristine upstream is not its corresponding source and the patch is
# archived with it, along with the formula that supplies the build arguments.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
out="$root/build-packaging/lgpl-sources"
dl="$out/.downloads"
work="$out/.work"
mkdir -p "$out" "$dl"

# The homebrew-core revision whose formulae built the bottles installed here.
# It is not a guess: the bottle digest recorded in each keg's sbom.spdx.json
# matches the bottle this revision declares. See NOTICE.
core_rev="fad6cdb45503e4976aeb7b1c4c4e0ec972f66013"

glib_version="2.90.0"
glib_tarball_sha="17d15cac2af80a33271127408e0abc2748eb297c595c2a26409e81e14e7d1b8f"
glib_formula_sha="7e41107faf209528d2e993afec53ad1006bbe8945772bde43c2295e8a2ef7659"
glib_patch_sha="d846efd0bf62918350da94f850db33b0f8727fece9bfaf8164566e3094e80c97"

# gettext's installed bottle predates the current formula, so it is pinned to
# the revision that built it. No gettext formula in this range patches the
# source, which is why only the upstream tarball is needed.
gettext_version="1.0"
gettext_rev="6265cb6ea7ef"
gettext_tarball_sha="85d99b79c981a404874c02e0342176cf75c7698e2b51fe41031cf6526d974f1a"
gettext_formula_sha="32965bd5b315f99bb0929b1099b7bde9d3371c4ebd0acb3d286c71c143527a53"

fetch() {  # url dest sha256
  [ -f "$2" ] && [ "$(shasum -a 256 "$2" | cut -d' ' -f1)" = "$3" ] && return 0
  curl -fsSL --retry 3 -o "$2.part" "$1"
  got="$(shasum -a 256 "$2.part" | cut -d' ' -f1)"
  [ "$got" = "$3" ] || { echo "$1 hashed $got, expected $3" >&2; rm -f "$2.part"; exit 1; }
  mv "$2.part" "$2"
}

rm -rf "$work"; mkdir -p "$work/glib" "$work/gettext"
mkdir -p "$dl/glib" "$dl/gettext"
raw="https://raw.githubusercontent.com/Homebrew/homebrew-core"

fetch "https://download.gnome.org/sources/glib/${glib_version%.*}/glib-$glib_version.tar.xz" \
      "$dl/glib/glib-$glib_version.tar.xz" "$glib_tarball_sha"
fetch "$raw/$core_rev/Formula/g/glib.rb"                 "$dl/glib/glib.rb"             "$glib_formula_sha"
fetch "$raw/$core_rev/Patches/glib/hardcoded-paths.diff" "$dl/glib/hardcoded-paths.diff" "$glib_patch_sha"
fetch "https://ftpmirror.gnu.org/gettext/gettext-$gettext_version.tar.gz" \
      "$dl/gettext/gettext-$gettext_version.tar.gz" "$gettext_tarball_sha"
fetch "$raw/$gettext_rev/Formula/g/gettext.rb" "$dl/gettext/gettext.rb" "$gettext_formula_sha"

# An archive that claims to be the corresponding source has to actually be it.
# If upstream ever reissues the tarball under the same name, this is where it
# shows up, rather than in a release nobody can rebuild.
echo "verifying the glib patch applies to glib-$glib_version"
rm -rf "$work/check"; mkdir -p "$work/check"
tar -xf "$dl/glib/glib-$glib_version.tar.xz" -C "$work/check"
( cd "$work/check/glib-$glib_version" &&
  patch -p1 --dry-run --force < "$dl/glib/hardcoded-paths.diff" ) ||
  { echo "the pinned glib patch no longer applies cleanly" >&2; exit 1; }
rm -rf "$work/check"

cat > "$work/glib/README.txt" <<TXT
Corresponding source for the GLib in Musix
==========================================

Musix bundles libglib-2.0.0.dylib and libgthread-2.0.0.dylib, from GLib
$glib_version, under the GNU Lesser General Public License, version 2.1 or
later. The license text is in Musix.app/Contents/Resources/licenses.

The bundled binaries are Homebrew's build, not a pristine upstream one, so
upstream alone would not be the corresponding source. This archive holds all
three parts:

  glib-$glib_version.tar.xz  the upstream release, from
                       https://download.gnome.org/sources/glib/${glib_version%.*}/
                       sha256 $glib_tarball_sha

  hardcoded-paths.diff Homebrew's only patch to the source. It replaces
                       compiled-in system paths in gio/xdgmime/xdgmime.c,
                       girepository/girepository.c and glib/gutils.c with a
                       @@HOMEBREW_PREFIX@@ placeholder that Homebrew rewrites
                       to the install prefix. Only the gutils.c hunk affects
                       the library Musix ships.
                       sha256 $glib_patch_sha

  glib.rb              the Homebrew formula: the script that controls
                       compilation, including the meson arguments used.
                       sha256 $glib_formula_sha

Both files come from Homebrew/homebrew-core at revision
$core_rev, the revision whose formula declares
the bottle digest recorded in the installed keg's sbom.spdx.json.

To rebuild:

  tar -xf glib-$glib_version.tar.xz
  cd glib-$glib_version
  patch -p1 < ../hardcoded-paths.diff
  # then meson, with the arguments glib.rb passes in its install block

Musix itself is at https://github.com/ImanMontajabi/Musix.
TXT

cat > "$work/gettext/README.txt" <<TXT
Corresponding source for the libintl in Musix
=============================================

Musix bundles libintl.8.dylib, from GNU gettext $gettext_version, under the
GNU Lesser General Public License, version 2.1 or later. The license text is
in Musix.app/Contents/Resources/licenses.

GNU gettext as a whole is partly GPL-3.0 and partly LGPL-2.1: the tools are
GPL, and the runtime library libintl is LGPL, per gettext-runtime/COPYING in
the tarball. Musix bundles only the library.

  gettext-$gettext_version.tar.gz  the upstream release, unmodified, from
                       https://ftpmirror.gnu.org/gettext/
                       sha256 $gettext_tarball_sha

  gettext.rb           the Homebrew formula that built it, from
                       Homebrew/homebrew-core revision $gettext_rev.

Homebrew applies no patches to gettext, so the upstream tarball above is the
complete corresponding source; the formula is included only because it holds
the configure arguments.

Musix itself is at https://github.com/ImanMontajabi/Musix.
TXT

# Same archive every time: fixed timestamps, no ownership, sorted entries.
# --no-recursion matters: without it tar walks the directory entry as well as
# taking the explicit list, and every file lands in the archive twice.
pack() {  # name dir
  ( cd "$(dirname "$2")" &&
    find "$(basename "$2")" -exec touch -t 197001010000 {} + &&
    find "$(basename "$2")" -print | LC_ALL=C sort |
      tar --uid 0 --gid 0 --numeric-owner --no-mac-metadata --no-recursion \
          -cJf "$out/$1.tar.xz" -T - )
}
cp "$dl/glib/"* "$work/glib/"; cp "$dl/gettext/"* "$work/gettext/"
mv "$work/glib" "$work/musix-glib-$glib_version-source"
mv "$work/gettext" "$work/musix-gettext-$gettext_version-source"
pack "glib-$glib_version-source"       "$work/musix-glib-$glib_version-source"
pack "gettext-$gettext_version-source" "$work/musix-gettext-$gettext_version-source"
rm -rf "$work"
ls -lh "$out"
