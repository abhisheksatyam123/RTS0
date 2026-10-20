#!/usr/bin/env python3
"""Runs tools/e2e_gl_compat.gd under Godot's Compatibility renderer (OpenGL) on a
virtual display and fails on any shader compile error in its log.

Headless Godot draws nothing (a dummy renderer), so shader errors only show with a
display: xvfb-run with Mesa's llvmpipe gives one without a GPU.

    python3 tools/check_gl_shaders.py [--godot PATH] [--shot PATH]
Exit code 0 = pass.
"""
import argparse
import os
import shutil
import subprocess
import sys

ERRORS = ("SHADER ERROR", "Shader compilation failed", "SCRIPT ERROR")


def main() -> int:
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser()
    ap.add_argument("--godot", default=os.environ.get("GODOT", shutil.which("godot") or "godot"))
    ap.add_argument("--shot", help="save the match's last frame as a PNG")
    args = ap.parse_args()
    if not shutil.which("xvfb-run"):
        print("xvfb-run not found (apt install xvfb)")
        return 2
    cmd = ["xvfb-run", "-a", "-s", "-screen 0 1280x720x24", args.godot, "--path", repo,
           "--audio-driver", "Dummy", "--rendering-driver", "opengl3", "--fixed-fps", "20",
           "-s", "tools/e2e_gl_compat.gd"]
    if args.shot:
        cmd += ["--", "--shot", os.path.abspath(args.shot)]
    p = subprocess.run(cmd, cwd=repo, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=1200)
    log = p.stdout.splitlines()
    bad = []
    for i, line in enumerate(log):
        if any(e in line for e in ERRORS):
            bad.append("\n".join(log[i:i + 2]))
    for line in log:
        if line.startswith(("  ok", "  FAIL", "  shot", "FAILURES", "OpenGL API")) or line[:2] in ("1.", "2."):
            print(line)
    if "Compatibility" not in p.stdout:
        print("FAIL: Godot did not start the Compatibility renderer")
        return 1
    for b in bad:
        print("FAIL:", b)
    # A crash on exit (rc 139) counts too: static Godot objects destroyed after
    # the engine shut down did that once (#1260).
    if p.returncode != 0:
        print("FAIL: Godot exited with", p.returncode)
    ok = p.returncode == 0 and "FAILURES: 0" in p.stdout and not bad
    print("RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
