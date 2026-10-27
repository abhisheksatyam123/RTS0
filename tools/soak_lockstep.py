#!/usr/bin/env python3
"""Networked lockstep soak: matches of 2-8 human clients with Petra in the other seats and
late observers, over UDP loopback with packet loss, through a pause, a save/load of every
peer's world, and a seated player who leaves and rejoins into its seat under the same name.
Every peer's per-turn hash must match the host's, every peer must end cleanly with the
right seats and names, and the host's replay of the match must play back offline with every
turn's hash matching (tools/test_netcode.cpp --net-peer, --replay-check).

  python3 tools/soak_lockstep.py --binary /tmp/np --out /tmp/soak_net [--seeds 1 2] [--only 3,5]

Build the binary as tools/playtest_lockstep.py does (tools/test_netcode.cpp and the
simulation sources). Exit code 0 = every match passed.
"""
import argparse
import os
import re
import subprocess
import sys
import time

# (humans, seats, observers, loss, leave_and_rejoin, pause, saveload)
MATCHES = [
    (2, 3, 1, 0.05, False, True, True),
    (3, 4, 0, 0.10, True, False, True),
    (4, 6, 1, 0.05, True, True, True),
    (6, 8, 2, 0.02, True, False, False),
    (8, 8, 1, 0.05, True, True, True),
    (4, 8, 0, 0.20, False, True, False),
    (5, 7, 1, 0.00, True, True, True),
    (2, 8, 2, 0.10, True, False, True),
]
NAMES = ["alice", "bob", "carol", "dave", "erin", "frank", "grace", "heidi"]


def read_hashes(path):
    rows = {}
    if os.path.exists(path):
        for line in open(path, errors="replace"):
            p = line.split()
            if len(p) >= 2 and p[0].isdigit():
                rows[int(p[0])] = p[1]
    return rows


def lines(text, tag):
    """The key=value fields of each '[net-peer] role: tag ...' line, by role."""
    out = {}
    for l in (text or "").splitlines():
        m = re.match(r"\[net-peer\] (\w+): " + tag + r" (.*)", l)
        if m:
            out[m.group(1)] = dict(kv.split("=", 1) for kv in m.group(2).split() if "=" in kv)
            out[m.group(1)]["_raw"] = m.group(2)
    return out


def run_match(binary, out, idx, seed, port, turns, speed, m):
    humans, seats, observers, loss, rejoin, pause, saveload = m
    tag = f"m{idx}_s{seed}"
    d = os.path.join(out, tag)
    os.makedirs(d, exist_ok=True)
    common = ["--speed", str(speed), "--loss", str(loss), "--humans", str(humans), "--seats", str(seats)]
    if saveload:
        common += ["--saveload-turn", str(turns // 2)]
    procs = {}

    def start(key, role, *more):
        log = os.path.join(d, key + ".log")
        if os.path.exists(log):
            os.remove(log)
        f = open(os.path.join(d, key + ".out"), "w")
        procs[key] = (subprocess.Popen([binary, "--net-peer", role, str(port), str(seed), str(turns), log, *common, *more],
                                       stdout=f, stderr=subprocess.STDOUT, text=True), log, f)

    host_more = ["--name", NAMES[0], "--replay-out", os.path.join(d, "replay.bin")]
    if pause:
        host_more += ["--pause-turn", str(turns // 3), "--pause-ms", "2000"]
    start("host", "host", *host_more)
    time.sleep(0.3)
    leaver = humans - 1 if rejoin else None
    leave_turn = turns // 4
    for c in range(1, humans):
        more = ["--name", NAMES[c]]
        if c == leaver:
            more += ["--quit-turn", str(leave_turn)]
        start(NAMES[c], "join", *more)
        time.sleep(0.1)
    t_end = time.time() + 900

    def host_turns():
        return len(read_hashes(procs["host"][1]))
    # Observers join a running match; the leaver comes back a while after it left.
    for k in range(observers):
        while time.time() < t_end and host_turns() < turns // 6 + 40 * k and procs["host"][0].poll() is None:
            time.sleep(0.1)
        start(f"watcher{k}", "join", "--name", f"watcher{k}", "--observe", "1")
    if leaver is not None:
        p = procs[NAMES[leaver]][0]
        try:
            p.wait(timeout=600)
        except subprocess.TimeoutExpired:
            pass
        while time.time() < t_end and host_turns() < leave_turn + 60 and procs["host"][0].poll() is None:
            time.sleep(0.1)
        start(NAMES[leaver] + "_back", "join", "--name", NAMES[leaver], "--rejoin", "1")
    rcs, outs = {}, {}
    for key, (p, log, f) in procs.items():
        try:
            rcs[key] = p.wait(timeout=max(5, t_end - time.time()))
        except subprocess.TimeoutExpired:
            p.kill()
            rcs[key] = "timeout"
        f.close()
        outs[key] = open(os.path.join(d, key + ".out"), errors="replace").read()
    h = read_hashes(procs["host"][1])
    problems, notes = [], []
    left_at = turns
    if leaver is not None:
        m_left = re.search(rf"client {leaver} left at turn (\d+)", outs["host"])
        if m_left:
            left_at = int(m_left.group(1))
    if len(h) != turns:
        problems.append(f"host ran {len(h)} of {turns} turns")
    for key, rc in rcs.items():
        if rc != 0:
            problems.append(f"{key} exit {rc}: " + " | ".join(l for l in outs[key].splitlines()
                                                              if "OOS" in l or "stall" in l or "could not" in l or "no connection" in l)[:300])
        rows = read_hashes(procs[key][1])
        bad = next((t for t in sorted(rows) if t in h and rows[t] != h[t]), None)
        if bad is not None and leaver is not None and key == NAMES[leaver] and bad > left_at:
            # The leaver ran turns the host had already dropped it from: its world parts from
            # the match only after it is gone (noted, not a failure of the match).
            notes.append(f"{key} ran turn {bad} after the host dropped it at turn {left_at} ({rows[bad]} vs host {h[bad]})")
        elif bad is not None:
            problems.append(f"{key} OOS at turn {bad} ({rows[bad]} vs host {h[bad]})")
        last = max(rows) if rows else 0
        if key != NAMES[leaver if leaver is not None else 0] and key != "host" and rc == 0 and last != turns and not (leaver is not None and key == NAMES[leaver]):
            problems.append(f"{key} stopped at turn {last}")
    # Seats and names: the host's final client list.
    roster = lines(outs["host"], "roster").get("host", {}).get("list", "")
    entries = {e.split(":")[1]: e.split(":") for e in roster.split(",") if e.count(":") >= 3}
    for c in range(humans):
        e = entries.get(NAMES[c])
        if not e or e[2] != str(c):
            problems.append(f"{NAMES[c]} not in seat {c}: {e}")
    for k in range(observers):
        e = entries.get(f"watcher{k}")
        if not e or e[2] != "255":
            problems.append(f"watcher{k} not an observer: {e}")
    if leaver is not None:
        rj = lines(outs.get(NAMES[leaver] + "_back", ""), "rejoin").get("join", {})
        if rj.get("player") != str(leaver):
            problems.append(f"{NAMES[leaver]} rejoined into seat {rj.get('player')}, not {leaver}")
    if saveload:
        for key in procs:
            sl = lines(outs[key], "saveload")
            for role, v in sl.items():
                if v.get("ok") == "1" and v.get("before") != v.get("after"):
                    problems.append(f"{key} save/load changed the hash {v.get('before')} -> {v.get('after')}")
                elif v.get("ok") != "1" and key == "host":
                    problems.append(f"{key} save/load failed")
    if pause:
        for key in procs:
            ps = lines(outs[key], "pause")
            for role, v in ps.items():
                if key in ("host",) + tuple(NAMES[1:humans]) and key != (NAMES[leaver] if leaver is not None else None) and v.get("seen") != "1":
                    problems.append(f"{key} never saw the pause")
    # Replay of the host's record, offline.
    cmap = lines(outs["host"], "clientmap").get("host", {}).get("_raw", "")
    rp = subprocess.run([binary, "--net-peer", "host", "0", str(seed), str(turns), os.path.join(d, "replay.log"),
                         "--humans", str(humans), "--seats", str(seats),
                         *(["--saveload-turn", str(turns // 2)] if saveload else []),
                         "--replay-check", os.path.join(d, "replay.bin"), "--client-map", cmap],
                        capture_output=True, text=True, timeout=600)
    rline = next((l for l in rp.stdout.splitlines() if l.startswith("[replay]")), "[replay] no result")
    if rp.returncode != 0 or "ok" not in rline:
        problems.append("replay: " + rline)
    desc = (f"{tag}: {humans} humans, {seats - humans} Petra, {observers} observers, loss {loss:.2f}"
            f"{', leave+rejoin' if rejoin else ''}{', pause' if pause else ''}{', save/load' if saveload else ''}")
    final = h.get(turns, "-")
    print(("PASS " if not problems else "FAIL ") + desc + f" | final {final} | {rline}", flush=True)
    for p in problems:
        print("   - " + p, flush=True)
    for n in notes:
        print("   note: " + n, flush=True)
    return not problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--seeds", type=int, nargs="+", default=[1])
    ap.add_argument("--turns", type=int, default=800)
    ap.add_argument("--speed", type=float, default=8.0)
    ap.add_argument("--port", type=int, default=22000)
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    only = {int(x) for x in a.only.split(",") if x}
    ok, n, port = 0, 0, a.port
    for seed in a.seeds:
        for i, m in enumerate(MATCHES):
            if only and i not in only:
                continue
            n += 1
            ok += run_match(a.binary, a.out, i, seed, port, a.turns, a.speed, m)
            port += 3
    print(f"\n{ok}/{n} matches passed")
    return 0 if ok == n else 1


if __name__ == "__main__":
    sys.exit(main())
