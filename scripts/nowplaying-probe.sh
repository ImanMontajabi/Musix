#!/usr/bin/env bash
# Prints the Now Playing client macOS has and what it was told: the check that
# Control Center and the media keys have a Musix to talk to. Read-only unless
# MR_SEND (0 play, 1 pause, 2 toggle, 4 next, 5 previous) and MR_CLIENT are set:
# then that command goes to the Now Playing client only if its name contains
# MR_CLIENT, e.g. MR_CLIENT='(<pid>)' for a run you started yourself.
#   scripts/nowplaying-probe.sh [seconds]    polls once a second for that long (default: once)
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
clang -fobjc-arc -dynamiclib -framework Foundation "$root/tests/nowplaying_probe.m" -o "$work/libprobe.dylib"
cat > "$work/run.pl" <<'PERL'
use DynaLoader;
my $h = DynaLoader::dl_load_file($ARGV[0], 0) or die "load: " . DynaLoader::dl_error();
my $f = DynaLoader::dl_find_symbol($h, "probe_main") or die "symbol";
DynaLoader::dl_install_xsub("main::probe", $f); &probe;
PERL
for _ in $(seq 1 "${1:-1}"); do /usr/bin/perl "$work/run.pl" "$work/libprobe.dylib" 2>&1 | sed 's/^[0-9-]* [0-9:.]* perl\[[0-9:]*\] //'; sleep 1; done
