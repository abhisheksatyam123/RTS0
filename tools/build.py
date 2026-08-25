#!/usr/bin/env python3
"""
Cross-platform build driver for RTS0 — GDExtension + Native C++
Works on Windows / Linux / macOS with the same command line.

Usage:
  python tools/build.py windows      # -> build/windows/RTS0.exe
  python tools/build.py linux        # -> build/linux/RTS0.x86_64
  python tools/build.py android      # -> build/android/RTS0.apk
  python tools/build.py ios          # -> build/ios/RTS0
  python tools/build.py all          # windows + linux

Env:
  GODOT  path to Godot 4.3 editor/headless binary
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
BUILD = REPO / "build"
NATIVE = REPO / "native"

PRESETS = {
    "windows": ("Windows Desktop", BUILD / "windows" / "RTS0.exe"),
    "linux":   ("Linux",           BUILD / "linux" / "RTS0.x86_64"),
    "android": ("Android",         BUILD / "android" / "RTS0.apk"),
    "ios":     ("iOS",             BUILD / "ios" / "RTS0"),
}

def find_godot() -> Path:
    env = os.environ.get("GODOT")
    if env:
        p = Path(env)
        if p.exists():
            return p
        print(f"GODOT={env} not found", file=sys.stderr)
        sys.exit(1)
    which = shutil.which("godot") or shutil.which("godot4") or shutil.which("Godot_v4.3-stable_linux_x86_64") or shutil.which("Godot")
    if which:
        return Path(which)
    candidates = []
    if sys.platform == "win32":
        candidates += [Path(r"C:\Godot\Godot_v4.3-stable_win64.exe"), Path(r"C:\Program Files\Godot\Godot.exe")]
    else:
        candidates += [Path("/usr/local/bin/godot"), Path("/opt/godot/godot"), Path.home() / ".local/bin/godot"]
    for c in candidates:
        if c.exists():
            return c
    print("Godot not found: set GODOT env var to the Godot 4.3 binary.", file=sys.stderr)
    sys.exit(1)

def run(cmd, cwd=REPO):
    print(f"+ {' '.join(str(c) for c in cmd)}")
    r = subprocess.run(cmd, cwd=cwd)
    if r.returncode != 0:
        sys.exit(r.returncode)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("target", nargs="?", default="linux", choices=[*PRESETS.keys(), "all"])
    ap.add_argument("--godot", default=None, help="override GODOT binary")
    ap.add_argument("--skip-scons", action="store_true", help="skip SCons C++ native build step")
    args = ap.parse_args()

    targets = list(PRESETS.keys()) if args.target == "all" else [args.target]

    if not args.skip_scons:
        # Build GDExtension C++ library first
        scons_cmd = shutil.which("scons")
        if scons_cmd:
            print("== Building Native C++ GDExtension library ==")
            for t in targets:
                platform = "windows" if t == "windows" else ("linux" if t == "linux" else t)
                run([scons_cmd, f"platform={platform}", "target=template_release"], cwd=NATIVE)
        else:
            print("Warning: SCons not found, skipping C++ native library build.", file=sys.stderr)

    godot = Path(args.godot) if args.godot else find_godot()
    print(f"Using Godot: {godot}")
    print(f"Repo: {REPO}  Target: {args.target}")

    if sys.platform != "darwin" and "ios" in targets:
        print("iOS export requires macOS + Xcode — skipping on this host.", file=sys.stderr)
        targets = [t for t in targets if t != "ios"]
        if not targets:
            sys.exit(0)

    # 1. import
    run([str(godot), "--headless", "--path", str(REPO), "--import"])

    # 2. export each preset
    for t in targets:
        preset, out = PRESETS[t]
        out.parent.mkdir(parents=True, exist_ok=True)
        print(f"\n== Exporting {preset} -> {out} ==")
        run([str(godot), "--headless", "--path", str(REPO), "--export-release", preset, str(out)])
        if t == "linux":
            try: out.chmod(0o755)
            except: pass

    print(f"\nDone. Outputs in {BUILD}")

if __name__ == "__main__":
    main()
