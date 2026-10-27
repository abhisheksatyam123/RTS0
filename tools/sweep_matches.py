#!/usr/bin/env python3
"""Headless Petra-vs-Petra sweep over every random map (MapType) and every skirmish map
(data/map_types.json type "skirmish") with tools/soak_match.cpp: civs rotate so every civ
plays, each match runs a twin copy (determinism) and a save/load copy, and the run is
logged per map. Prints one line per match and a tally of what the runs reported.

  python3 tools/sweep_matches.py --soak /tmp/soak_match --out /tmp/sweep --minutes 20 --jobs 4
  python3 tools/sweep_matches.py ... --large --no-twin   # 8 players on large and giant maps,
                                                        # Petra difficulties Sandbox to Very Hard

A match that exits non-zero (crash, assert) or outlives --timeout is reported as such.
"""
import argparse
import json
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

PLAYABLE_CIVS = 15  # CIV_COUNT minus Gaia; civ 14 is Gaia, 15 Germans
CIVS = [c for c in range(16) if c != 14]


def random_maps():
    names = []
    for line in open("src/simulation/sim_types.h"):
        m = re.match(r"\s*MAP_[A-Z0-9_]+ = (\d+),", line)
        if m:
            names.append(int(m.group(1)))
    return sorted(set(names))


def skirmish_maps():
    d = json.load(open("data/map_types.json"))
    out = []

    def walk(j):
        if isinstance(j, dict):
            for k, v in j.items():
                if isinstance(v, dict) and v.get("type") == "skirmish":
                    out.append(k)
                else:
                    walk(v)
    walk(d["maps"])
    return sorted(set(out))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--soak", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--minutes", type=float, default=20)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--timeout", type=int, default=1200)
    ap.add_argument("--only", default="", help="comma list of map names to run")
    ap.add_argument("--large", action="store_true", help="8 players, 384/512 tiles, mixed difficulties; skirmish maps with 6+ seats")
    ap.add_argument("--no-twin", action="store_true")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    jobs = []
    k = 0
    players = 8 if a.large else 4
    for n, m in enumerate(random_maps()):
        civs = [CIVS[(k + i) % len(CIVS)] for i in range(players)]
        k += players
        size = ("384" if n % 2 else "512") if a.large else "256"
        args = ["--map", str(m), "--players", str(players), "--size", size]
        if a.large:
            args += ["--diffs", ",".join(str((n + i) % 6) for i in range(players))]
        jobs.append((f"r{m}", args, civs))
    for n, name in enumerate(skirmish_maps()):
        if a.large and len(json.load(open(f"data/maps/{name}.json")).get("players", [])) < 6:
            continue
        civs = [CIVS[(k + i) % len(CIVS)] for i in range(8)]
        k += 2
        args = ["--json", name]
        if a.large:
            args += ["--diffs", ",".join(str((n + i) % 6) for i in range(8))]
        jobs.append((name, args, civs))
    if a.only:
        keep = set(a.only.split(","))
        jobs = [j for j in jobs if j[0] in keep]

    def run(job):
        tag, args, civs = job
        log = os.path.join(a.out, tag + ".log")
        cmd = [a.soak] + args + ["--seed", str(a.seed), "--minutes", str(a.minutes),
                                 "--save-at", str(a.minutes / 2), "--civs", ",".join(map(str, civs))]
        if not a.no_twin:
            cmd.append("--twin")
        with open(log, "w") as f:
            try:
                rc = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, timeout=a.timeout).returncode
            except subprocess.TimeoutExpired:
                rc = "timeout"
        text = open(log, errors="replace").read()
        summary = next((l for l in text.splitlines() if l.startswith("[summary]")), "[no summary]")
        line = f"{tag} rc={rc} {summary}"
        print(line, flush=True)
        return tag, rc, text

    with ThreadPoolExecutor(a.jobs) as ex:
        results = list(ex.map(run, jobs))
    bad = [r for r in results if r[1] != 0]
    print(f"\n{len(results)} matches, {len(bad)} non-zero exits: " + ", ".join(f"{t}({rc})" for t, rc, _ in bad))
    tally = {}
    for _, _, text in results:
        for kind in re.findall(r"^\[([a-z-]+)\]", text, re.M):
            tally[kind] = tally.get(kind, 0) + 1
    print("reported:", ", ".join(f"{k}={v}" for k, v in sorted(tally.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
