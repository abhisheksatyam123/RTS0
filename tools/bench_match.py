#!/usr/bin/env python3
"""RTS0 headless match benchmark.

Compiles tools/bench_match.cpp against the simulation sources with a plain
g++ command (no SCons, no Godot) and plays a 20-minute skirmish against Petra
on a random map with wave mode off. Reports per-tick cost for the whole game
and the AI's state at the end (workers, buildings, phase), so gameplay
changes show up next to perf. For fixed unit-count load scenarios use
tools/bench_sim.cpp instead.

Usage:
  python3 tools/bench_match.py                      # 20-minute match, seed 42
  python3 tools/bench_match.py --out after.json     # save results
  python3 tools/bench_match.py --baseline before.json  # compare, show deltas
  python3 tools/bench_match.py --minutes 5 --samples

Typical before/after flow for a change:
  git stash; python3 tools/bench_match.py --out /tmp/before.json; git stash pop
  python3 tools/bench_match.py --baseline /tmp/before.json

Timings depend on the machine, so only compare runs made on the same box.
--fail-over PCT exits 1 when tick p95 regresses by more than PCT percent
against --baseline.

Exit codes: 0 = ran (and within --fail-over), 1 = regression, 2 = build/run failure.
"""

from __future__ import annotations

import argparse
import json
import os
import shlex
import shutil
import subprocess
import sys
import time

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SOURCES = [
    "tools/bench_match.cpp",
    "src/simulation/simulation.cpp",
    "src/simulation/rms_generator.cpp",
    "src/core/job_system.cpp",
]

CXXFLAGS = ["-std=c++20", "-O2", "-DNDEBUG", "-w", "-Isrc", "-pthread"]

SCENARIOS = {
    "match": ["--scenario", "match"],
}

KEYS = [
    ("tick_ms_mean", "tick mean ms"),
    ("tick_ms_p50", "tick p50 ms"),
    ("tick_ms_p95", "tick p95 ms"),
    ("tick_ms_p99", "tick p99 ms"),
    ("tick_ms_max", "tick max ms"),
    ("wall_s", "wall s"),
    ("ai_workers", "AI workers"),
    ("ai_units", "AI units"),
    ("ai_buildings", "AI buildings"),
    ("ai_phase", "AI phase"),
]


def compiler() -> str:
    for c in [os.environ.get("CXX"), "g++", "clang++", "c++"]:
        if c and shutil.which(c):
            return c
    return "g++"


def build(out_dir: str) -> str:
    os.makedirs(out_dir, exist_ok=True)
    binary = os.path.join(out_dir, "bench_match")
    cmd = [compiler(), *CXXFLAGS, *SOURCES, "-o", binary]
    print(f"[build] {' '.join(shlex.quote(c) for c in cmd)}", flush=True)
    t0 = time.time()
    proc = subprocess.run(cmd, cwd=REPO_ROOT, capture_output=True, text=True)
    if proc.returncode != 0:
        print(f"[build] FAILED rc={proc.returncode}")
        print("\n".join((proc.stderr or proc.stdout).splitlines()[-40:]))
        raise SystemExit(2)
    print(f"[build] OK ({time.time() - t0:.1f}s)", flush=True)
    return binary


def run(binary: str, name: str, args: argparse.Namespace) -> dict:
    cmd = [binary, *SCENARIOS[name], "--seed", str(args.seed), "--map", str(args.map)]
    if args.minutes is not None:
        cmd += ["--minutes", str(args.minutes)]
    cmd += ["--sample", "120" if args.samples else "0"]
    print(f"[run] {name}: {' '.join(cmd[1:])}", flush=True)
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=args.timeout)
    if args.samples and proc.stderr:
        sys.stdout.write(proc.stderr)
    if proc.returncode != 0:
        print(f"[run] {name} FAILED rc={proc.returncode}")
        print(proc.stderr[-4000:])
        raise SystemExit(2)
    return json.loads(proc.stdout.strip().splitlines()[-1])


def fmt(v) -> str:
    return f"{v:.3f}" if isinstance(v, float) else str(v)


def report(results: dict, baseline: dict | None) -> float:
    worst = 0.0
    for name, r in results.items():
        ended = f", ended at {r['ended_at_s']}s" if r["ended_at_s"] >= 0 else ""
        print(f"\n== {name} (seed {r['seed']}, {r['sim_minutes']:.1f} sim min, {r['ticks']} ticks{ended})")
        base = (baseline or {}).get(name)
        for key, label in KEYS:
            line = f"  {label:<14} {fmt(r[key]):>10}"
            if base and key in base:
                b = base[key]
                line += f"   was {fmt(b):>10}"
                if isinstance(b, (int, float)) and b:
                    d = (r[key] - b) / b * 100.0
                    line += f"  ({d:+.1f}%)"
                    if key == "tick_ms_p95":
                        worst = max(worst, d)
            print(line)
    return worst


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--map", type=int, default=5, help="MapType id (default 5 = random continental)")
    ap.add_argument("--minutes", type=float, default=None, help="override sim minutes for every scenario")
    ap.add_argument("--samples", action="store_true", help="print a state sample every 2 sim minutes")
    ap.add_argument("--out", help="write results JSON here")
    ap.add_argument("--baseline", help="results JSON from an earlier run to compare against")
    ap.add_argument("--fail-over", type=float, default=None, metavar="PCT",
                    help="exit 1 if tick p95 regresses more than PCT%% vs --baseline")
    ap.add_argument("--build-dir", default=os.path.join(REPO_ROOT, "build", "bench"))
    ap.add_argument("--timeout", type=int, default=1800)
    args = ap.parse_args()

    binary = build(args.build_dir)
    results = {n: run(binary, n, args) for n in SCENARIOS}

    baseline = None
    if args.baseline:
        with open(args.baseline) as f:
            baseline = json.load(f)
    worst = report(results, baseline)

    if args.out:
        with open(args.out, "w") as f:
            json.dump(results, f, indent=2)
        print(f"\n[out] {args.out}")
    if baseline is not None and args.fail_over is not None and worst > args.fail_over:
        print(f"\n[FAIL] tick p95 regressed {worst:.1f}% (> {args.fail_over}%)")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
