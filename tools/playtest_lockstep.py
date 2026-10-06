#!/usr/bin/env python3
"""RTS0 two-client lockstep playtest driver (docs/PLAN.md P1).

Compiles tools/test_netcode.cpp in --playtest mode with a plain g++ command
(no SCons, no Godot) and runs the two-peer lockstep harness across several
seeds. Every turn the harness prints the FNV-1a state hash of all three
simulation instances; the first turn where the hashes disagree is an
out-of-sync (OOS) and the harness exits 1 with a context dump.

Exit codes: 0 = every seed stayed in sync, 1 = OOS found, 2 = build failure.
"""

from __future__ import annotations

import argparse
import os
import re
import shlex
import shutil
import subprocess
import sys
import time

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Minimal translation units discovered by link probing (see docs/MULTIPLAYER.md
# "Playtest harness"). src/core/diagnostics/* is deliberately excluded: it pulls
# in godot-cpp, which cannot be linked outside the engine.
SOURCES = [
    "tools/test_netcode.cpp",
    "src/simulation/simulation.cpp",
    "src/simulation/rms_generator.cpp",
    "src/core/job_system.cpp",
    "src/network/turn_manager.cpp",
    "src/network/net_host.cpp",
]

CXXFLAGS = [
    "-std=c++20",
    "-O1",
    "-g",
    "-fno-exceptions",
    "-Isrc",
    "-Igodot-cpp/include",
    "-Igodot-cpp/gen/include",
]

# Keep assert() live in the legacy test path; the playtest path does not rely on it.
CXXFLAGS += ["-UNDEBUG"]


def default_out_dir() -> str:
    home = os.environ.get("HOME") or os.path.expanduser("~")
    return os.path.join(home, "tmp", "playtest")


def compiler() -> str:
    """Pick a usable C++20 host compiler.

    $CXX can point at a foreign-architecture conda cross compiler (this box
    exports an aarch64 conda g++ into an x86_64 shell), which cannot compile
    -std=c++20 and cannot produce a runnable binary. Probe candidates and keep
    the first one that actually accepts -std=c++20 for a trivial TU.
    """
    candidates = []
    if os.environ.get("CXX"):
        candidates.append(os.environ["CXX"])
    candidates += ["g++", "c++", "clang++"]
    found = shutil.which("g++") or shutil.which("c++")
    if found:
        candidates.append(found)
    seen, ordered = set(), []
    for c in candidates:
        if c and c not in seen:
            seen.add(c)
            ordered.append(c)
    probe_src = "int main(){return 0;}\n"
    for c in ordered:
        try:
            p = subprocess.run([c, "-std=c++20", "-x", "c++", "-", "-o", os.devnull],
                               input=probe_src, capture_output=True, text=True, timeout=60)
        except (OSError, subprocess.SubprocessError):
            continue
        if p.returncode == 0:
            return c
    return ordered[0] if ordered else "g++"


def parse_link_errors(output: str) -> list[str]:
    """Extract undefined symbols from a g++ link failure."""
    return sorted(set(re.findall(r"undefined reference to [`']([^']+)", output)))


def build(out_dir: str, source: str, verbose: bool, force: bool) -> str:
    os.makedirs(out_dir, exist_ok=True)
    binary = os.path.join(out_dir, source)
    cmd = [compiler(), *CXXFLAGS, *SOURCES, "-o", binary]
    print(f"[build] {' '.join(shlex.quote(c) for c in cmd)}")
    t0 = time.time()
    proc = subprocess.run(cmd, cwd=REPO_ROOT, capture_output=True, text=True)
    dt = time.time() - t0
    if proc.returncode != 0:
        syms = parse_link_errors(proc.stderr + proc.stdout)
        print(f"[build] FAILED rc={proc.returncode} in {dt:.1f}s")
        tail = "\n".join((proc.stderr or proc.stdout).splitlines()[-40:])
        print(tail)
        if syms:
            print("[build] undefined symbols:")
            for s in syms[:40]:
                print(f"  {s}")
        raise SystemExit(2)
    if verbose and proc.stderr.strip():
        print(proc.stderr.strip())
    print(f"[build] OK -> {binary} ({dt:.1f}s, {os.path.getsize(binary)} bytes)")
    return binary


TURN_RE = re.compile(r"^turn\s+(\d+)\s+A=0x([0-9A-F]{8})")
PASS_RE = re.compile(r"^\[playtest\] PASS seed=(\d+) turns=(\d+) final_hash=0x([0-9A-F]{8})")
MATCH_END_RE = re.compile(r"MATCH END at turn (\d+)")
DIV_RE = re.compile(r"first divergent turn\s*:\s*(\d+)")


def run_case(binary: str, seed: int, turns: int, verbose_every: int,
             shadow: bool, timeout: int, through: bool = False) -> dict:
    args = [binary, "--playtest", str(turns), str(seed),
            "--verbose", str(verbose_every)]
    if shadow:
        args.append("--shadow")
    if through:
        args.append("--through")
    t0 = time.time()
    try:
        proc = subprocess.run(args, capture_output=True, text=True, timeout=timeout)
        rc = proc.returncode
        out = proc.stdout
        err = proc.stderr
        timed_out = False
    except subprocess.TimeoutExpired as exc:
        rc = -1
        out = exc.stdout.decode() if isinstance(exc.stdout, bytes) else (exc.stdout or "")
        err = exc.stderr.decode() if isinstance(exc.stderr, bytes) else (exc.stderr or "")
        timed_out = True
    dt = time.time() - t0

    last_turn = 0
    final_hash = None
    divergent = None
    match_end = None
    for line in out.splitlines():
        m = TURN_RE.match(line)
        if m:
            last_turn = int(m.group(1))
            continue
        m = PASS_RE.match(line)
        if m:
            final_hash = m.group(3)
            continue
        m = DIV_RE.search(line)
        if m:
            divergent = int(m.group(1))
        m = MATCH_END_RE.search(line)
        if m:
            match_end = int(m.group(1))

    return {
        "seed": seed,
        "turns": turns,
        "rc": rc,
        "seconds": dt,
        "last_turn": last_turn,
        "final_hash": final_hash,
        "divergent_turn": divergent,
        "match_end_turn": match_end,
        "stdout": out,
        "stderr": err,
        "timed_out": timed_out,
        "ok": rc == 0 and divergent is None,
    }


def main() -> int:
    ap = argparse.ArgumentParser(
        description="RTS0 two-client lockstep playtest (compile + run OOS hunt)")
    ap.add_argument("--ticks", type=int, default=2000,
                    help="lockstep turns per seed (default 2000 = 400 s of game time)")
    ap.add_argument("--seeds", type=int, nargs="+", default=[4242, 777001],
                    help="simulation seeds to run (default 4242 777001)")
    ap.add_argument("--out-dir", default=default_out_dir(),
                    help="artifact directory (default $HOME/tmp/playtest)")
    ap.add_argument("--binary", default="netcode_playtest",
                    help="binary name written into --out-dir")
    ap.add_argument("--verbose-every", type=int, default=250,
                    help="print a hash line every N turns (0 = silent)")
    ap.add_argument("--shadow", action="store_true",
                    help="also run the third command-free shadow peer")
    ap.add_argument("--through", action="store_true",
                    help="keep hashing after a match ends (defeat freezes the sim)")
    ap.add_argument("--timeout", type=int, default=900,
                    help="per-seed wall-clock timeout in seconds (default 900)")
    ap.add_argument("--rebuild", action="store_true",
                    help="force rebuild even if the binary is newer than sources")
    ap.add_argument("--verbose-build", action="store_true")
    args = ap.parse_args()

    if not shutil.which(compiler()) and not os.path.exists(compiler()):
        print(f"[build] C++ compiler not found: {compiler()}", file=sys.stderr)
        return 2

    out_dir = os.path.abspath(args.out_dir)
    binary_path = os.path.join(out_dir, args.binary)
    need_build = args.rebuild or not os.path.exists(binary_path)
    if not need_build:
        newest = max(os.path.getmtime(os.path.join(REPO_ROOT, s)) for s in SOURCES)
        need_build = newest > os.path.getmtime(binary_path)
    if need_build:
        try:
            build(out_dir, args.binary, args.verbose_build, args.rebuild)
        except SystemExit as exc:
            return int(exc.code or 2)
    else:
        print(f"[build] up to date -> {binary_path}")

    print("=" * 72)
    print(f"RTS0 lockstep playtest  ticks/seed={args.ticks}  seeds={args.seeds}")
    print(f"  binary={binary_path}")
    print(f"  petra_ai=on  wall_drag=on  combat=on  gather/build/move=on")
    print("=" * 72)

    results = []
    for seed in args.seeds:
        print(f"\n---- seed {seed} : {args.ticks} turns ----")
        res = run_case(binary_path, seed, args.ticks, args.verbose_every,
                       args.shadow, args.timeout, args.through)
        results.append(res)
        if res["timed_out"]:
            print(f"[seed {seed}] TIMEOUT after {res['seconds']:.1f}s "
                  f"(rc={res['rc']})")
        if res["divergent_turn"] is not None:
            print(f"[seed {seed}] OOS at turn {res['divergent_turn']} "
                  f"rc={res['rc']} ({res['seconds']:.1f}s)")
        elif res["ok"]:
            end = res.get("match_end_turn")
            print(f"[seed {seed}] PASS  {res['last_turn']} turns, "
                  f"final_hash=0x{res['final_hash']} ({res['seconds']:.1f}s)"
                  + (f"  [match ended at turn {end}]" if end else ""))
        else:
            print(f"[seed {seed}] FAIL rc={res['rc']} ({res['seconds']:.1f}s)")
        # Echo the per-turn hash trace so the run log is self-contained.
        for line in res["stdout"].splitlines():
            if line.startswith("turn ") or line.startswith("[playtest]") \
                    or line.startswith("====") or line.startswith("first ") \
                    or line.startswith("client ") or line.startswith("commands ") \
                    or line.startswith("  client=") or line.startswith("  unit[") \
                    or line.startswith("  bld[") or line.startswith("  building ") \
                    or line.startswith("  resources ") or line.startswith("  ai_stock ") \
                    or line.startswith("  scalars "):
                print("   " + line)

    print("\n" + "=" * 72)
    print("SUMMARY")
    for r in results:
        verdict = "PASS" if r["ok"] else ("OOS" if r["divergent_turn"] is not None
                                          else "FAIL")
        print(f"  seed={r['seed']:<10} turns={r['turns']:<6} {verdict:<5} "
              f"last_turn={r['last_turn']:<6} "
              f"final_hash={r['final_hash'] or '-':<10} "
              f"{r['seconds']:.1f}s rc={r['rc']}")
    passed = sum(1 for r in results if r["ok"])
    print(f"  -> {passed}/{len(results)} seeds in sync")
    print("=" * 72)

    oos = [r for r in results if r["divergent_turn"] is not None]
    if oos:
        for r in oos:
            print(f"\n### OOS REPORT seed={r['seed']} first divergent turn="
                  f"{r['divergent_turn']} ###")
            idx = r["stdout"].find("================ OUT-OF-SYNC")
            print(r["stdout"][idx:] if idx >= 0 else r["stdout"][-4000:])
        return 1
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
