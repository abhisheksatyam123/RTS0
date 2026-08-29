#!/usr/bin/env python3
"""
RTS0 Headless Dedicated Server & Match Runner
Runs the high-performance C++ SoA simulation headless on servers/CI.

Usage:
  python3 tools/server.py [--map MAP] [--players NUM] [--turns MAX_TURNS] [--port PORT]
"""
import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]

def find_godot():
    env = os.environ.get("GODOT")
    if env and Path(env).exists():
        return Path(env)
    for name in ["godot", "godot4", "Godot_v4.3-stable_linux_x86_64", "Godot"]:
        p = shutil.which(name)
        if p:
            return Path(p)
    candidates = [Path("/usr/local/bin/godot"), Path("/opt/godot/godot"), Path.home() / ".local/bin/godot"]
    for c in candidates:
        if c.exists():
            return c
    return None

def main():
    parser = argparse.ArgumentParser(description="RTS0 Headless Dedicated Server")
    parser.add_argument("--map", default="random_continental", help="Map ID (e.g. random_continental, random_oasis, acropolis_bay)")
    parser.add_argument("--players", type=int, default=2, help="Number of players (2-8)")
    parser.add_argument("--turns", type=int, default=1200, help="Max match simulation turns (20 Hz = 60s/1200 turns)")
    parser.add_argument("--port", type=int, default=7777, help="Server ENet host port")
    parser.add_argument("--headless", action="store_true", default=True, help="Run headless")
    args = parser.parse_args()

    print("==================================================")
    print("       RTS0 DEDICATED HEADLESS SERVER            ")
    print("==================================================")
    print(f"Map:          {args.map}")
    print(f"Players:      {args.players}")
    print(f"Max Turns:    {args.turns} ({args.turns/20:.1f}s sim time)")
    print(f"Port:         {args.port}")
    print("==================================================")

    godot_bin = find_godot()
    if godot_bin:
        cmd = [
            str(godot_bin),
            "--headless",
            "--path", str(REPO),
            "res://scenes/main.tscn",
            "--",
            "--server",
            f"--map={args.map}",
            f"--players={args.players}",
            f"--port={args.port}",
            f"--max-turns={args.turns}"
        ]
        print(f"Starting server process: {' '.join(cmd)}")
        try:
            p = subprocess.Popen(cmd, cwd=REPO)
            p.wait()
        except KeyboardInterrupt:
            print("
Shutting down server gracefully...")
            p.terminate()
            p.wait()
    else:
        print("Godot binary not found in PATH or GODOT environment variable.")
        print("To run in headless server mode, install Godot 4.3 or set GODOT=/path/to/godot")

if __name__ == "__main__":
    main()
