#!/usr/bin/env python3
"""Back up the real Musix profile before anything that could touch it.

Copies the library folder and the settings into
~/MusixBackups/auto/<timestamp>-<reason>, checks the copy against the original
file by file, and keeps the newest KEEP automatic backups. Backups made by hand
elsewhere in ~/MusixBackups are never touched.

The runtime copy is left out: it is 90% of the profile's size, and Musix copies
it again from its own bundle when it is missing. Settings are read through the
preferences service (defaults export), which is where the app's live values are.
"""
import datetime, hashlib, os, plistlib, shutil, subprocess, sys
from pathlib import Path

KEEP = 5
HOME = Path.home()
DATA = HOME / "Library/Application Support/Sung"
DOMAIN = "com.sung.sung"
ROOT = HOME / "MusixBackups/auto"

def files(base):
    for dirpath, dirnames, filenames in os.walk(base):
        rel = Path(dirpath).relative_to(base)
        if rel.parts[:2] == ("sung", "runtime"):
            dirnames.clear()
            continue
        dirnames.sort()
        for name in sorted(filenames):
            yield rel / name

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    if sys.platform != "darwin":
        print("backup-profile: only the macOS profile layout is known; nothing backed up")
        return 0
    reason = "-".join(sys.argv[1:]) or "manual"
    reason = "".join(c if c.isalnum() or c in "-_" else "-" for c in reason)[:40]
    running = subprocess.run(["pgrep", "-f", "Musix.app/Contents/MacOS"], capture_output=True).returncode == 0
    final = ROOT / f"{datetime.datetime.now():%Y-%m-%d-%H%M%S}-{reason}"
    # Written under another name and renamed once checked, so a backup that
    # fails halfway never counts as one of the ones kept.
    target = final.with_name(final.name + ".partial")
    target.mkdir(parents=True)
    copied = 0
    if DATA.is_dir():
        for rel in files(DATA):
            source, dest = DATA / rel, target / "Sung" / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, dest)
            if digest(source) != digest(dest):
                sys.exit(f"backup-profile: {rel} did not copy intact; the backup in {target} is incomplete")
            copied += 1
    exported = subprocess.run(["defaults", "export", DOMAIN, "-"], capture_output=True)
    settings = plistlib.loads(exported.stdout) if exported.returncode == 0 else {}
    (target / f"{DOMAIN}.plist").write_bytes(plistlib.dumps(settings, fmt=plistlib.FMT_BINARY))
    if plistlib.loads((target / f"{DOMAIN}.plist").read_bytes()) != settings:
        sys.exit("backup-profile: the settings did not copy intact")
    target.rename(final)
    target = final
    note = " (Musix was running, so the library may change right after this)" if running else ""
    print(f"backup-profile: {copied} files and {len(settings)} settings in {target}{note}")

    backups = sorted(p for p in ROOT.iterdir() if p.is_dir() and not p.name.endswith(".partial"))
    for old in backups[:-KEEP]:
        shutil.rmtree(old)
        print(f"backup-profile: removed the oldest automatic backup, {old.name}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
