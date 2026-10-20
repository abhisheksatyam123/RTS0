#!/usr/bin/env python3
"""RTS0 two-client lockstep playtest driver (docs/PLAN.md P1).

Compiles tools/test_netcode.cpp in --playtest mode with a plain g++ command
(no SCons, no Godot) and runs the two-peer lockstep harness across several
seeds. Every turn the harness prints the lockstep state hash of all three
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
import tempfile
import time

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# TurnManager::COMMAND_DELAY, which the saved-game case's expected turns follow.
with open(os.path.join(REPO_ROOT, "src", "network", "turn_manager.h")) as _f:
    COMMAND_DELAY = int(re.search(r"COMMAND_DELAY = (\d+);", _f.read()).group(1))

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


PAUSE_TURN, SAVELOAD_TURN = 200, 250


def peer_lines(outs: list, tag: str) -> dict:
    """'[net-peer] <role>: <tag> k=v ...' summary lines as {role: {k: v}}."""
    found = {}
    for out in outs:
        for line in (out or "").splitlines():
            m = re.match(r"\[net-peer\] (host|join): " + tag + r" (.*)", line)
            if m:
                found[m.group(1)] = dict(kv.split("=", 1) for kv in m.group(2).split() if "=" in kv)
    return found


class Peer:
    """A peer process whose output goes to a temp file, not a pipe: a pipe that
    nobody reads until the end fills up (a lossy link logs a lot), the peer
    blocks writing to it, and every wait on the others then runs its whole
    timeout. Each peer also has a hard deadline, so a hung step fails within
    --step-timeout instead of stacking one wait per peer."""
    step_timeout = 600.0

    def __init__(self, args, **kw):
        kw.pop("stdout", None)
        kw.pop("stderr", None)
        kw.pop("text", None)
        self._out = tempfile.NamedTemporaryFile(mode="w+")
        self._proc = subprocess.Popen(args, stdout=self._out, stderr=subprocess.STDOUT, text=True, **kw)
        self._deadline = time.time() + Peer.step_timeout

    def __getattr__(self, name):
        return getattr(self._proc, name)

    def communicate(self, timeout=None):
        left = max(1.0, self._deadline - time.time())
        if timeout is None:
            # Only after kill(): the process is going, so wait for it.
            self._proc.wait()
        else:
            try:
                self._proc.wait(timeout=min(timeout, left))
            except subprocess.TimeoutExpired:
                print(f"[net] peer {self._proc.args[2] if len(self._proc.args) > 2 else ''} "
                      f"still running at its step deadline; killed", flush=True)
                raise
        self._out.seek(0)
        return self._out.read(), None

    def printed(self, text: str) -> bool:
        """Whether the peer has printed text so far (a second handle, so the
        peer's own write position is left alone)."""
        with open(self._out.name) as f:
            return text in f.read()

    def wait(self, timeout=None):
        left = max(1.0, self._deadline - time.time())
        return self._proc.wait(timeout=left if timeout is None else min(timeout, left))


def run_net_case(binary: str, seed: int, turns: int, speed: float, loss: float,
                 port: int, out_dir: str, timeout: int, defeat_turn: int = 0,
                 more: tuple = ()) -> dict:
    """Two real processes, one host and one joiner, over UDP loopback.

    Each process owns one sim, posts only its own player's commands and runs
    turns off its own jittered wall clock, so this exercises the real
    TurnManager gate and NetHost reliable delivery (with injected packet loss).
    """
    logs = [os.path.join(out_dir, f"net_{role}_{seed}.log") for role in ("host", "join")]
    common = [str(port), str(seed), str(turns)]
    extra = ["--speed", str(speed), "--loss", str(loss), "--defeat-turn", str(defeat_turn),
             "--pause-turn", str(PAUSE_TURN), "--saveload-turn", str(SAVELOAD_TURN), *more]
    t0 = time.time()
    host = Peer([binary, "--net-peer", "host", *common, logs[0], *extra],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(0.3)
    join = Peer([binary, "--net-peer", "join", *common, logs[1], *extra],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    outs, rcs = [], []
    for p in (join, host):
        try:
            out, _ = p.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            p.kill()
            out, _ = p.communicate()
        outs.append(out)
        rcs.append(p.returncode)
    dt = time.time() - t0

    hashes, extras = [], []
    for path in logs:
        rows, ext = {}, {}
        if os.path.exists(path):
            with open(path) as f:
                for line in f:
                    parts = line.split()
                    if len(parts) >= 2:
                        rows[int(parts[0])] = parts[1]
                        ext[int(parts[0])] = dict(p.split("=", 1) for p in parts[3:] if "=" in p)
        hashes.append(rows)
        extras.append(ext)
    # Ceasefire: it must lift on the same turn on both peers, and no unit may
    # be hurt or killed while it holds.
    cf_end = [next((t for t in sorted(e) if e[t].get("cf") == "0"), None) for e in extras]
    cf_hits = [t for e in extras for t in sorted(e)
               if e[t].get("cf") == "1" and (e[t].get("wounds", "0") != "0" or e[t].get("dead", "0") != "0")]
    ceasefire_ok = cf_end[0] == cf_end[1] and not cf_hits
    # Defeat: the joiner's player (bit 1) falls on the same turn on both peers,
    # and the match goes on (host vs Petra) to the last turn.
    out_turn = [next((t for t in sorted(e) if int(e[t].get("defeated", "0")) & 2), None) for e in extras]
    over = [any(e[t].get("over") == "1" for t in e) for e in extras]
    # Tribute: the joiner's 77 stone reaches the host once, on the same turn on
    # both peers; its second tribute, sent after its defeat, is dropped. The
    # host's trader also brings stone home, so only turns where the joiner's
    # stone moves count.
    def stone_changes(e):
        rows = [(t, tuple(int(x) for x in e[t]["stone"].split(","))) for t in sorted(e) if "stone" in e[t]]
        return [(t, (b[0] - a[0], b[1] - a[1])) for (_, a), (t, b) in zip(rows, rows[1:]) if a != b]
    tributes = [stone_changes(e) for e in extras]
    # A tribute moves stone from one player to the other on one turn; the joiner's own
    # stone also rises once it captures the team-2 wonder (ResourceTrickle, +1 every 2 s),
    # sometimes on the turn the host's trader comes home, so only a transfer counts.
    tribute_ok = tributes[0] == tributes[1] and [d for _, d in tributes[0] if d[0] and d[0] == -d[1]] == [(77, -77)]
    # Upgrade: the host's wall (12) becomes a gate (8) on the same turn on both peers.
    gate_turn = [next((t for t in sorted(e) if e[t].get("wall") == "8"), None) for e in extras]
    upgrade_ok = gate_turn[0] is not None and gate_turn[0] == gate_turn[1]
    # Chat (0 A.D. NMT_CHAT through the host): the whisper reaches only the host,
    # stamped with the joiner's id; the line to an absent player reaches nobody;
    # the host's line to all reaches the joiner and is not echoed back.
    chats = {role: [l.split(": chat ", 1)[1] for o in outs for l in (o or "").splitlines()
                    if l.startswith(f"[net-peer] {role}: chat ")] for role in ("host", "join")}
    chat_ok = chats["host"] == ["peer-1: gg"] and chats["join"] == ["peer-0: hi all"]
    # Pause (NMT_CLIENT_PAUSED): both peers saw the host's pause and held turns.
    pauses = peer_lines(outs, "pause")
    pause_ok = (len(pauses) == 2 and all(p.get("seen") == "1" and p.get("by") == "0" for p in pauses.values())
                and int(pauses["join"].get("held_ms", "0")) >= 1000)
    # Trading goods (cmd 33): the host's mix changes once its command runs, then
    # the joiner's, on the same turns on both peers.
    def goods_steps(e):
        rows = [(t, e[t].get("goods")) for t in sorted(e) if "goods" in e[t]]
        return [(t, b) for (_, a), (t, b) in zip(rows, rows[1:]) if a != b]
    goods = [goods_steps(e) for e in extras]
    goods_ok = (goods[0] == goods[1] and len(goods[0]) == 2
                and 340 < goods[0][0][0] <= 344 and goods[0][0][1].startswith("10,20,30,40/")
                and 350 < goods[0][1][0] <= 354 and goods[0][1][1].endswith("/0,0,50,50"))
    # Formations (cmd 8, Formation.js): the squad forms a box (template 2) of 4,
    # then the controller walks (order 1) and attack-walks (order 2), each
    # within COMMAND_DELAY turns of its command, on the same turns on both peers.
    # An attack-walk that meets a foe in sight at once shows as its members
    # fighting (order 4) on the turn it lands.
    def form_steps(e):
        rows = [(t, e[t].get("form")) for t in sorted(e) if "form" in e[t]]
        return [(t, b) for (_, a), (t, b) in zip(rows, rows[1:]) if a != b]
    forms = [form_steps(e) for e in extras]
    def form_at(lo, hi, *want):
        return next((t for t, b in forms[0] if lo < t <= hi and b in want), None)
    form_turns = {"box": form_at(345, 349, "2:0:4"), "walk": form_at(355, 359, "2:1:4"),
                  "attack_walk": form_at(365, 369, "2:2:4", "2:4:4")}
    form_ok = forms[0] == forms[1] and all(v is not None for v in form_turns.values())
    # Return resource (cmd 34) and trade routes (cmd 35): the carrier takes the
    # drop-off order (12) and empties its 5 wood; the trader walks (1) with the
    # Shift-queued route behind it, then trades (11). Same turns on both peers.
    def steps(e, key):
        rows = [(t, e[t].get(key)) for t in sorted(e) if key in e[t]]
        return [(t, b) for (_, a), (t, b) in zip(rows, rows[1:]) if a != b]
    carries, trades = [steps(e, "carry") for e in extras], [steps(e, "trade") for e in extras]
    carry_turns = {"order": next((t for t, b in carries[0] if 430 < t <= 434 and b == "12:5"), None),
                   "dropped": next((t for t, b in carries[0] if 430 < t <= 530 and b.endswith(":0")), None)}
    trade_turns = {"walk": next((t for t, b in trades[0] if 422 < t <= 426 and b == "1:1"), None),
                   "trade": next((t for t, b in trades[0] if 422 < t <= 560 and b.startswith("11:")), None)}
    carry_ok = carries[0] == carries[1] and all(v is not None for v in carry_turns.values())
    trade_ok = trades[0] == trades[1] and all(v is not None for v in trade_turns.values())
    # Network status (NMT_CLIENT_PERFORMANCE): the host reports each client's round
    # trip once a second, and the joiner hears it.
    statuses = peer_lines(outs, "status")
    def rtts(role):
        v = statuses.get(role, {}).get("rtt", "-")
        return {} if v == "-" else {int(k): int(r) for k, r in (x.split(":") for x in v.split(","))}
    status_ok = (len(statuses) == 2 and int(statuses["join"].get("reports", "0")) >= 3
                 and 0 < rtts("join").get(1, 0) < 2000 and 0 < rtts("host").get(1, 0) < 2000)
    # Save/load: each world reloads from its own save with the same hash.
    saves = peer_lines(outs, "saveload")
    save_ok = (len(saves) == 2 and all(v.get("ok") == "1" and v.get("before") == v.get("after") for v in saves.values())
               and saves["host"].get("before") == saves["join"].get("before"))
    # Gate lock: the host locks the gate and later unlocks it, on the same turns on both peers.
    def lock_spans(e):
        rows = [(t, e[t].get("locked")) for t in sorted(e)]
        return [t for (_, a), (t, b) in zip(rows, rows[1:]) if a != b]
    locks = [lock_spans(e) for e in extras]
    lock_ok = locks[0] == locks[1] and len(locks[0]) == 2
    # Pack (cmd 29): the stone thrower's pack state changes on the same turns on
    # both peers.
    def pack_steps(e):
        rows = [(t, e[t].get("pack")) for t in sorted(e)]
        return [(t, b) for (_, a), (t, b) in zip(rows, rows[1:]) if a != b]
    packs = [pack_steps(e) for e in extras]
    # It is passive, so only the orders move it: unpacking (4) once the unpack
    # posted at 380 runs COMMAND_DELAY turns later, unpacked (2), then packing (3)
    # after the pack posted at 440, packed (1).
    pack_ok = (packs[0] == packs[1] and [b for _, b in packs[0]] == ["4", "2", "3", "1"]
               and 380 < packs[0][0][0] <= 384 and 440 < packs[0][2][0] <= 444)
    # Unit orders on the host's worker: order kind (6 gather, 1 walk, 255 dead)
    # and Shift-queue length, as (turn, kind, queue) changes.
    def work_steps(e):
        rows = [(t, e[t].get("work"), e[t].get("queue")) for t in sorted(e) if "work" in e[t]]
        return [(t, k, q) for (_, a, aq), (t, k, q) in zip(rows, rows[1:]) if (a, aq) != (k, q)]
    works = [work_steps(e) for e in extras]
    def first(pred, lo, hi):
        return next((t for t, k, q in works[0] if lo < t <= hi and pred(k, q)), None)
    work_turns = {
        "gather": first(lambda k, q: k == "6", 390, 394),       # cmd 4 at 390
        "walk": first(lambda k, q: k == "1", 400, 404),         # cmd 1 at 400
        "queued": first(lambda k, q: q == "1", 402, 406),       # cmd 1 + 0x80 at 402
        "queue_done": first(lambda k, q: q == "0", 403, 470),   # reached the first walk's end
        "back": first(lambda k, q: k == "6", 470, 474),         # cmd 31 at 470
        "deleted": first(lambda k, q: k == "255", 580, 584),    # cmd 30 at 580
    }
    # Heal (cmd 32): the healer takes the order (kind 10) once it runs, and the
    # swordsman wounded to 40 hp at turn 405 gains health.
    def heal_rows(e):
        return [(t, e[t].get("heal"), float(e[t].get("patient", "0"))) for t in sorted(e) if "heal" in e[t]]
    heals = [heal_rows(e) for e in extras]
    heal_turn = next((t for t, k, _ in heals[0] if 410 < t <= 414 and k == "10"), None)
    hp_at = {t: hp for t, _, hp in heals[0]}
    healed = max((hp for t, _, hp in heals[0] if 414 < t <= 480), default=0.0)
    heal_ok = heals[0] == heals[1] and heal_turn is not None and 0.0 < hp_at.get(410, 0.0) < 100.0 and healed >= hp_at[410] + 10.0
    work_ok = (works[0] == works[1] and all(v is not None for v in work_turns.values())
               and not any(k == "255" for t, k, q in works[0] if t <= 580))
    defeat_ok = (not defeat_turn) or (out_turn[0] is not None and out_turn[0] == out_turn[1]
                                      and out_turn[0] > defeat_turn and not any(over))
    common_turns = sorted(set(hashes[0]) & set(hashes[1]))
    divergent = next((t for t in common_turns if hashes[0][t] != hashes[1][t]), None)
    complete = len(hashes[0]) == turns and len(hashes[1]) == turns
    # Game options and teams must reach the joiner through NMT_GAME_START.
    setups = {}
    for out in outs:
        for line in (out or "").splitlines():
            m = re.match(r"\[net-peer\] (host|join): setup (.*)", line)
            if m:
                setups[m.group(1)] = m.group(2).strip()
    setup_ok = len(setups) == 2 and setups["host"] == setups["join"]
    return {
        "seed": seed, "turns": turns, "rcs": rcs, "seconds": dt,
        "host_turns": len(hashes[0]), "join_turns": len(hashes[1]),
        "divergent_turn": divergent,
        "final_hash": hashes[0].get(turns),
        "output": "".join(o for o in outs if o),
        "setup_ok": setup_ok, "setups": setups,
        "ceasefire_ok": ceasefire_ok, "ceasefire_end": cf_end, "ceasefire_hits": cf_hits[:5],
        "tribute_ok": tribute_ok, "tributes": tributes, "upgrade_ok": upgrade_ok, "gate_turn": gate_turn,
        "form_ok": form_ok, "forms": forms, "form_turns": form_turns,
        "carry_ok": carry_ok, "carries": carries, "carry_turns": carry_turns,
        "trade_ok": trade_ok, "trades": trades, "trade_turns": trade_turns,
        "goods_ok": goods_ok, "goods": goods,
        "status_ok": status_ok, "statuses": statuses,
        "heal_ok": heal_ok, "heal_turn": heal_turn, "healed": healed,
        "work_ok": work_ok, "works": works, "work_turns": work_turns,
        "lock_ok": lock_ok, "locks": locks, "pack_ok": pack_ok, "packs": packs, "chat_ok": chat_ok, "chats": chats,
        "pause_ok": pause_ok, "pauses": pauses, "save_ok": save_ok, "saves": saves,
        "defeat_ok": defeat_ok, "defeat_turn": out_turn, "game_over": over,
        "ok": divergent is None and complete and setup_ok and ceasefire_ok and defeat_ok and tribute_ok and upgrade_ok and lock_ok and pack_ok and work_ok and heal_ok and status_ok and goods_ok and form_ok and carry_ok and trade_ok and chat_ok and pause_ok and save_ok and all(rc == 0 for rc in rcs),
    }


def read_hashes(path: str) -> dict:
    rows = {}
    if os.path.exists(path):
        with open(path) as f:
            for line in f:
                parts = line.split()
                if len(parts) >= 2:
                    rows[int(parts[0])] = parts[1]
    return rows


def run_rejoin_case(binary: str, seed: int, speed: float, loss: float, port: int, out_dir: str,
                    timeout: int) -> bool:
    """0 A.D. rejoin: the joiner leaves after turn LEAVE, the host plays on alone,
    then a new process signs in under the same name and rejoins into its old seat
    from the host's saved world, and both must hash the same on every turn after
    the rejoin. A client with another name that asks first gets no seat: as in
    0.28's NetServer it may only watch."""
    turns, leave = 500, 200
    print(f"\n---- rejoin: joiner leaves after turn {leave}, comes back into its own seat ----")
    logs = {r: os.path.join(out_dir, f"rejoin_{r}.log") for r in ("host", "left", "intruder", "back")}
    for path in logs.values():
        if os.path.exists(path):
            os.remove(path)  # a stale log from an earlier run must not pass for this one
    common = ["--speed", str(speed), "--loss", str(loss)]
    def peer(role, log, *more):
        return Peer([binary, "--net-peer", role, str(port), str(seed), str(turns), log, *common, *more],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    # The host pauses at PAUSE for 5 s, and bob comes back during it: as 0.28's
    # NetServer does, the host must tell him who is pausing.
    pause = leave + 100
    host = peer("host", logs["host"], "--pause-turn", str(pause), "--pause-ms", "5000")
    time.sleep(0.3)
    left = peer("join", logs["left"], "--name", "bob", "--quit-turn", str(leave))
    outs, rcs = [], []
    def finish(p):
        try:
            out, _ = p.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            p.kill()
            out, _ = p.communicate()
        outs.append(out)
        rcs.append(p.returncode)
    finish(left)
    time.sleep(0.5)
    intruder = peer("join", logs["intruder"], "--name", "mallory", "--rejoin", "1")
    while intruder.poll() is None and not intruder.printed("rejoin turn="):
        time.sleep(0.05)
    while host.poll() is None and len(read_hashes(logs["host"])) < pause:
        time.sleep(0.05)
    back = peer("join", logs["back"], "--name", "bob", "--rejoin", "1", "--pause-turn", str(pause))
    finish(intruder)
    finish(back)
    finish(host)
    intruder_seat = peer_lines([outs[1]], "rejoin").get("join", {}).get("player")
    rep = peer_lines([outs[2]], "rejoin")
    h, b = read_hashes(logs["host"]), read_hashes(logs["back"])
    t0 = int(rep.get("join", {}).get("turn", "0"))
    # 0 A.D. NMT_REJOINED: the host and bob both hear "has rejoined the game."
    # for bob, and the host also hears "has joined the game." for mallory, who
    # only watches (she has left before bob is back).
    host_rejoined = peer_lines([outs[3]], "rejoined").get("host", {})
    back_rejoined = peer_lines([outs[2]], "rejoined").get("join", {})
    back_pause = peer_lines([outs[2]], "pause").get("join", {})
    intruder_client = peer_lines([outs[1]], "rejoin").get("join", {}).get("client")
    rejoined_ok = (back_rejoined.get("me") and intruder_client
                   and set(host_rejoined.get("clients", "").split(",")) == {intruder_client, back_rejoined.get("me")}
                   and back_rejoined.get("clients") == back_rejoined.get("me"))
    pause_ok = back_pause.get("seen") == "1" and back_pause.get("by") == "0"
    after = [t for t in sorted(b) if t > t0]
    bad = next((t for t in after if h.get(t) != b[t]), None)
    ok = (rcs == [0, 0, 0, 0] and t0 >= leave and rep.get("join", {}).get("player") == "1" and len(h) == turns
          and after == list(range(t0 + 1, turns + 1)) and bad is None and intruder_seat == "255"
          and "in the lockstep from turn" in (outs[2] or "") and rejoined_ok and pause_ok)
    if ok:
        joined = re.search(r"in the lockstep from turn (\d+) \(at turn (\d+)\)", outs[2] or "")
        print(f"[net] PASS rejoin: bob came back into player 2 at turn {t0}, then turns {t0 + 1}-{turns} "
              f"matched the host (final hash {h[turns]}); the match ran on while he loaded, and the lockstep "
              f"took him at turn {joined.group(1)}; he came back while the host paused and was told so, and both "
              f"heard that he has rejoined; mallory, who asked first, could only watch")
    else:
        for o in outs:
            for line in (o or "").splitlines():
                if line.startswith("[net-peer]") or line.startswith("[TurnManager]"):
                    print("   " + line)
        print(f"[net] FAIL rejoin: rc={rcs} rejoin={rep} intruder_seat={intruder_seat} rejoined={host_rejoined}/{back_rejoined} back_pause={back_pause} host_turns={len(h)} back_turns={len(b)} first_bad={bad}")
    return ok


def run_three_human_case(binary: str, seed: int, speed: float, loss: float, port: int, out_dir: str,
                          timeout: int) -> bool:
    """Three humans (0 A.D. hosts up to 8): the host relays each joiner's batches
    to the other. The third player leaves after turn LEAVE and a new process
    takes its seat; every peer must hash the same on every turn it ran."""
    turns, leave = 500, 200
    print(f"\n---- three humans: player 3 leaves after turn {leave}, a new client takes its seat ----")
    roles = ("host", "j1", "j2", "back")
    logs = {r: os.path.join(out_dir, f"three_{r}.log") for r in roles}
    for path in logs.values():
        if os.path.exists(path):
            os.remove(path)
    common = ["--speed", str(speed), "--loss", str(loss), "--humans", "3"]
    # Output goes to files, so the script can wait on what a peer printed.
    def peer(role, log, *more):
        with open(log + ".out", "w") as out:
            return subprocess.Popen([binary, "--net-peer", role, str(port), str(seed), str(turns), log, *common, *more],
                                    stdout=out, stderr=subprocess.STDOUT, text=True)
    def printed(log, text, secs=20.0):
        end = time.time() + secs
        while time.time() < end:
            if os.path.exists(log + ".out") and text in open(log + ".out").read():
                return True
            time.sleep(0.05)
        return False
    procs = {"host": peer("host", logs["host"])}
    time.sleep(0.3)
    procs["j1"] = peer("join", logs["j1"])
    # Connect order numbers the joiners: j2 starts only once j1 is client 1,
    # however loaded the machine is.
    printed(logs["j1"], "assigned local peer_id 1")
    procs["j2"] = peer("join", logs["j2"], "--quit-turn", str(leave))
    outs, rcs = {}, {}
    def finish(role):
        p = procs[role]
        try:
            p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()
        outs[role] = open(logs[role] + ".out").read()
        rcs[role] = p.returncode
    finish("j2")
    time.sleep(0.5)
    procs["back"] = peer("join", logs["back"], "--rejoin", "1")
    for role in ("back", "j1", "host"):
        finish(role)
    h = {r: read_hashes(logs[r]) for r in roles}
    rep = peer_lines([outs["back"]], "rejoin").get("join", {})
    t0 = int(rep.get("turn", "0"))
    bad = {r: next((t for t in sorted(h[r]) if h[r][t] != h["host"].get(t)), None) for r in ("j1", "j2", "back")}
    relayed = ("client 2 left after turn" in outs["j1"] and "client 3 takes player 2" in outs["j1"])
    # j2 leaves at the turn-LEAVE boundary before logging it, so it logs LEAVE - 1
    # turns; the host can serve the new client the world at turn LEAVE itself.
    checks = {
        "exit codes": all(rc == 0 for rc in rcs.values()),
        "host and j1 ran every turn": len(h["host"]) == turns and len(h["j1"]) == turns,
        "j2 ran to its leave turn": len(h["j2"]) >= leave - 1,
        "new client took player 2 after j2 left": rep.get("player") == "2" and t0 >= leave,
        "new client logged every turn after its rejoin": sorted(h["back"]) == list(range(t0 + 1, turns + 1)),
        "j1 heard the seat change": relayed,
        "hashes match the host": all(v is None for v in bad.values()),
    }
    ok = all(checks.values())
    if ok:
        print(f"[net] PASS three humans: all {turns} turns matched on host and player 2, player 3 left at "
              f"turn {leave} and a new client took its seat at turn {t0}, then matched to turn {turns} "
              f"(final hash {h['host'][turns]})")
    else:
        for role in roles:
            for line in outs.get(role, "").splitlines():
                if line.startswith("[net-peer]") or line.startswith("[TurnManager]"):
                    print(f"   {role}: {line}")
        failed = ", ".join(k for k, v in checks.items() if not v)
        print(f"[net] FAIL three humans ({failed}): rc={rcs} rejoin={rep} turns={ {r: len(v) for r, v in h.items()} } "
              f"first_bad={bad} relayed={relayed}")
    return ok


def run_observer_case(binary: str, seed: int, speed: float, loss: float, port: int, out_dir: str,
                      timeout: int) -> bool:
    """0 A.D. observer late join: two humans play, and a third client joins the
    running match as an observer. It loads the host's world, then must hash the
    same as the host on every later turn; it takes no seat, nobody waits on it,
    and every peer's player list shows it as an observer. The host admits late
    observers from its buddy list only (network.lateobservers "buddies"), so a
    stranger is refused, and with network.autocatchup the observer runs the
    turns it holds until it is within a few turns of the host."""
    turns = 400
    print("\n---- observer: a third client joins the running match to watch ----")
    roles = ("host", "join", "obs")
    logs = {r: os.path.join(out_dir, f"observer_{r}.log") for r in roles}
    for path in logs.values():
        if os.path.exists(path):
            os.remove(path)
    common = ["--speed", str(speed), "--loss", str(loss)]
    def peer(role, log, *more):
        return Peer([binary, "--net-peer", role, str(port), str(seed), str(turns), log, *common, *more],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    # 0 A.D. network.observerlimit: the host takes one observer, so a second is refused.
    host = peer("host", logs["host"], "--name", "alice", "--observer-limit", "1",
                "--late-observers", "buddies", "--buddies", "Watcher,peeker")
    time.sleep(0.3)
    # bob asks to rejoin again at turn 60, as a slow loader repeats its setup-time
    # request; the host must keep him seated (his row stays '1:bob:1:0').
    join = peer("join", logs["join"], "--name", "bob", "--stale-rejoin-turn", "60")
    # Join once the match is well under way.
    deadline = time.time() + min(timeout, Peer.step_timeout)
    while time.time() < deadline and len(read_hashes(logs["host"])) < 120 and host.poll() is None:
        time.sleep(0.1)
    obs = peer("join", logs["obs"], "--name", "watcher", "--observe", "1")
    while time.time() < deadline and obs.poll() is None and not read_hashes(logs["obs"]):
        time.sleep(0.1)
    extra_log = os.path.join(out_dir, "observer_extra.log")
    extra = peer("join", extra_log, "--name", "peeker", "--observe", "1")
    try:
        extra_out, _ = extra.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        extra.kill()
        extra_out, _ = extra.communicate()
    refused = extra.returncode == 3 and "could not rejoin (reason 7)" in (extra_out or "")
    # Not on the host's buddy list: no late observer (0 A.D. NDR_SERVER_ALREADY_IN_GAME).
    stranger = peer("join", os.path.join(out_dir, "observer_stranger.log"), "--name", "stranger", "--observe", "1")
    try:
        stranger_out, _ = stranger.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        stranger.kill()
        stranger_out, _ = stranger.communicate()
    stranger_refused = stranger.returncode == 3 and "could not rejoin (reason 6)" in (stranger_out or "")
    outs, rcs = {}, {}
    for role, p in (("obs", obs), ("join", join), ("host", host)):
        try:
            out, _ = p.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            p.kill()
            out, _ = p.communicate()
        outs[role], rcs[role] = out, p.returncode
        # Each peer's whole output, for a failure the summary lines don't explain.
        with open(os.path.join(out_dir, f"observer_{role}.out"), "w") as f:
            f.write(out or "")
    def lines(role, tag):
        return peer_lines([outs[role]], tag).get("host" if role == "host" else "join", {})
    rep = lines("obs", "rejoin")
    h, j, o = (read_hashes(logs[r]) for r in roles)
    t0 = int(rep.get("turn", "0"))
    after = [t for t in sorted(o) if t > t0]
    bad = next((t for t in after if h.get(t) != o[t]), None)
    pair_bad = next((t for t in sorted(h) if j.get(t) != h[t]), None)
    waited = {r: lines(r, "status").get("waited_on", "-").split(",") for r in ("host", "join")}
    want = {"0:alice:0:0", "1:bob:1:0", "2:watcher:255:1"}
    rosters = {r: set(lines(r, "roster").get("list", "-").split(",")) for r in roles}
    observer_flags = {r: lines(r, "roster").get("observer") for r in roles}
    checks = {
        "exit codes": all(rc == 0 for rc in rcs.values()),
        "observer joined with no seat": rep.get("player") == "255" and t0 > 0,
        "players ran every turn in sync": len(h) == turns and len(j) == turns and pair_bad is None,
        "observer matched the host after joining": after == list(range(t0 + 1, turns + 1)) and bad is None,
        "nobody waited on the observer": all("2" not in w for w in waited.values()),
        "every player list": all(r == want for r in rosters.values()),
        "observer flags": observer_flags == {"host": "0", "join": "0", "obs": "1"},
        "second observer refused (reason 7)": refused,
        "stranger refused (reason 6)": stranger_refused,
        # NMT_REJOINED for an observer: every peer hears "watcher has joined the game.".
        "everyone heard the observer join": all("2" in lines(r, "rejoined").get("clients", "").split(",") for r in roles),
    }
    behind = float(lines("obs", "catchup").get("behind_late", "99"))
    if loss < 0.3:
        # Without catch-up it stays as far behind as its transfer took (20+ turns).
        checks["observer caught up"] = behind <= 8.0
    ok = all(checks.values())
    if ok:
        print(f"[net] PASS observer: joined at turn {t0} as an observer, matched the host on turns {t0 + 1}-{turns}, "
              f"never waited on; every player list shows {sorted(want)}; a second observer past the limit "
              f"of 1 was refused (reason 7) and a stranger not on the buddy list (reason 6); it ran "
              f"{behind:.1f} turns behind the host at the end; every peer heard it join (final hash {h[turns]})")
    else:
        for role in roles:
            for line in (outs[role] or "").splitlines():
                if line.startswith("[net-peer]") or line.startswith("[TurnManager]"):
                    print(f"   [{role}] " + line)
        failed = ", ".join(k for k, v in checks.items() if not v)
        print(f"[net] FAIL observer ({failed}): rc={rcs} rejoin={rep} turns host/join/obs={len(h)}/{len(j)}/{len(o)} "
              f"first_bad={bad} pair_bad={pair_bad} waited={waited} rosters={rosters} flags={observer_flags} "
              f"extra_rc={extra.returncode} extra={(extra_out or '').strip().splitlines()[-1:]} "
              f"stranger_rc={stranger.returncode} behind={behind}")
    return ok


def run_observer_lag_case(binary: str, seed: int, speed: float, loss: float, port: int, out_dir: str,
                          timeout: int) -> bool:
    """0 A.D. network.observermaxlag: the host holds the match while an observer
    is more than 10 turns behind. The observer's machine stops for 3 s; the
    host must wait on it, and all three still agree on every turn."""
    turns = 400
    print("\n---- observer max lag: the host waits for an observer 10 turns behind ----")
    roles = ("host", "join", "obs")
    logs = {r: os.path.join(out_dir, f"obslag_{r}.log") for r in roles}
    for path in logs.values():
        if os.path.exists(path):
            os.remove(path)
    common = ["--speed", str(speed), "--loss", str(loss)]
    def peer(role, log, *more):
        return Peer([binary, "--net-peer", role, str(port), str(seed), str(turns), log, *common, *more],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    host = peer("host", logs["host"], "--name", "alice", "--observer-max-lag", "10")
    time.sleep(0.3)
    join = peer("join", logs["join"], "--name", "bob")
    deadline = time.time() + min(timeout, Peer.step_timeout)
    while time.time() < deadline and len(read_hashes(logs["host"])) < 120 and host.poll() is None:
        time.sleep(0.1)
    obs = peer("join", logs["obs"], "--name", "watcher", "--observe", "1", "--stall-turn", "250", "--stall-ms", "3000")
    outs, rcs = {}, {}
    for role, p in (("obs", obs), ("join", join), ("host", host)):
        try:
            out, _ = p.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            p.kill()
            out, _ = p.communicate()
        outs[role], rcs[role] = out, p.returncode
    def lines(role, tag):
        return peer_lines([outs[role]], tag).get("host" if role == "host" else "join", {})
    t0 = int(lines("obs", "rejoin").get("turn", "0"))
    h, j, o = (read_hashes(logs[r]) for r in roles)
    after = [t for t in sorted(o) if t > t0]
    bad = next((t for t in after if h.get(t) != o[t]), None)
    pair_bad = next((t for t in sorted(h) if j.get(t) != h[t]), None)
    waited = lines("host", "status").get("waited_on", "-").split(",")
    checks = {
        "exit codes": all(rc == 0 for rc in rcs.values()),
        "players ran every turn in sync": len(h) == turns and len(j) == turns and pair_bad is None,
        "observer matched the host after joining": t0 > 0 and after == list(range(t0 + 1, turns + 1)) and bad is None,
        "the host waited on the observer": "2" in waited,
    }
    ok = all(checks.values())
    if ok:
        print(f"[net] PASS observer max lag: the observer (joined at turn {t0}) stopped for 3 s at turn 250 and the "
              f"host waited on it (waited_on={','.join(waited)}); all three agree on every turn "
              f"(final hash {h[turns]})")
    else:
        for role in roles:
            for line in (outs[role] or "").splitlines():
                if line.startswith("[net-peer]") or line.startswith("[TurnManager]"):
                    print(f"   [{role}] " + line)
        failed = ", ".join(k for k, v in checks.items() if not v)
        print(f"[net] FAIL observer max lag ({failed}): rc={rcs} turns={len(h)}/{len(j)}/{len(o)} t0={t0} "
              f"first_bad={bad} pair_bad={pair_bad} waited={waited}")
    return ok


def run_saved_game_case(binary: str, seed: int, speed: float, loss: float, port: int, out_dir: str,
                        timeout: int) -> bool:
    """0 A.D. NMT_SAVED_GAME_START: the host continues a saved game. Its world
    has run 10 s on alone, so the joiner's own boot world differs; the joiner
    takes the host's world at Start, its seat from the host, and the match
    holds until it has loaded. Both must then agree on every turn."""
    turns = 400
    print("\n---- saved game: the host continues a saved game, the joiner loads its world ----")
    logs = {r: os.path.join(out_dir, f"saved_{r}.log") for r in ("host", "join")}
    for path in logs.values():
        if os.path.exists(path):
            os.remove(path)
    common = ["--speed", str(speed), "--loss", str(loss), "--saved-game", "1"]
    def peer(role, log, *more):
        return Peer([binary, "--net-peer", role, str(port), str(seed), str(turns), log, *common, *more],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    host = peer("host", logs["host"], "--name", "alice")
    time.sleep(0.3)
    join = peer("join", logs["join"], "--name", "bob")
    outs, rcs = {}, {}
    for role, p in (("join", join), ("host", host)):
        try:
            out, _ = p.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            p.kill()
            out, _ = p.communicate()
        outs[role], rcs[role] = out, p.returncode
    start_h = peer_lines([outs["host"]], "saved").get("host", {})
    start_j = peer_lines([outs["join"]], "saved").get("join", {})
    h, j = read_hashes(logs["host"]), read_hashes(logs["join"])
    first = min(j) if j else 0
    bad = next((t for t in sorted(j) if h.get(t) != j[t]), None)
    checks = {
        "exit codes": all(rc == 0 for rc in rcs.values()),
        "the joiner loaded the host's world": start_j.get("hash") == start_h.get("hash") and start_h.get("hash"),
        "the joiner kept its seat": start_j.get("player") == "1",
        # The lockstep took it at turn 1 + COMMAND_DELAY + 1: the host was still at turn 1.
        "no turn ran before it loaded": first == 2 and bool(h) and sorted(h)[0] == 2
                                        and f"in the lockstep from turn {COMMAND_DELAY + 2}" in (outs["host"] or ""),
        "both ran every turn in sync": len(h) == turns - 1 and sorted(j) == sorted(h) and bad is None,
    }
    ok = all(checks.values())
    if ok:
        print(f"[net] PASS saved game: the joiner loaded the host's world (hash {start_h.get('hash')}) as player 2 "
              f"before turn 2 ran, then turns 2-{turns} matched (final hash {h[turns]})")
    else:
        for role in ("host", "join"):
            for line in (outs[role] or "").splitlines():
                if line.startswith("[net-peer]") or line.startswith("[TurnManager]"):
                    print(f"   [{role}] " + line)
        failed = ", ".join(k for k, v in checks.items() if not v)
        print(f"[net] FAIL saved game ({failed}): rc={rcs} turns={len(h)}/{len(j)} first={first} first_bad={bad} "
              f"start={start_h}/{start_j}")
    return ok


def run_kick_case(binary: str, seed: int, speed: float, loss: float, port: int, out_dir: str,
                  timeout: int) -> bool:
    """0 A.D. password-protected hosting and kick/ban: three humans join a game
    with a password, a client with the wrong one is refused, the host bans the
    third player after turn KICK, and that name cannot come back. The host and
    player 2 play on in sync."""
    turns, kick = 400, 150
    print(f"\n---- password and ban: wrong password refused, player 3 banned after turn {kick} ----")
    roles = ("host", "j1", "j2", "wrong", "banned")
    logs = {r: os.path.join(out_dir, f"kick_{r}.log") for r in roles}
    for path in logs.values():
        if os.path.exists(path):
            os.remove(path)
    common = ["--speed", str(speed), "--loss", str(loss), "--humans", "3"]
    def peer(role, log, *more):
        return Peer([binary, "--net-peer", role, str(port), str(seed), str(turns), log, *common, *more],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    procs = {"host": peer("host", logs["host"], "--name", "host", "--password", "pw",
                          "--kick-turn", str(kick), "--kick-name", "carol", "--ban", "1")}
    time.sleep(0.3)
    procs["j1"] = peer("join", logs["j1"], "--name", "alice", "--password", "pw")
    time.sleep(0.3)
    procs["j2"] = peer("join", logs["j2"], "--name", "carol", "--password", "pw")
    time.sleep(1.5)  # the match is running
    procs["wrong"] = peer("join", logs["wrong"], "--name", "mallory", "--password", "nope")
    outs, rcs = {}, {}
    def finish(role):
        p = procs[role]
        try:
            out, _ = p.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            p.kill()
            out, _ = p.communicate()
        outs[role] = out or ""
        rcs[role] = p.returncode
    finish("wrong")
    finish("j2")
    time.sleep(0.5)
    procs["banned"] = peer("join", logs["banned"], "--name", "carol", "--password", "pw", "--rejoin", "1")
    for role in ("banned", "j1", "host"):
        finish(role)
    h, j1 = read_hashes(logs["host"]), read_hashes(logs["j1"])
    refused = {r: next((l.split("code=")[1] for l in outs[r].splitlines() if "refused code=" in l), None)
               for r in ("wrong", "banned")}
    # Kicked, then the link closes with 0 A.D.'s reason: banned (7).
    kicked = any("join: kicked at turn" in l and l.endswith("reason=7") for l in outs["j2"].splitlines())
    j1_heard = "join: kicks name=carol ban=1" in outs["j1"]
    bad = next((t for t in sorted(h) if j1.get(t) != h[t]), None)
    ok = (rcs["host"] == 0 and rcs["j1"] == 0 and rcs["j2"] == 0 and rcs["wrong"] == 4 and rcs["banned"] == 4
          and refused == {"wrong": "3", "banned": "4"} and kicked and j1_heard
          and len(h) == turns and len(j1) == turns and bad is None)
    if ok:
        print(f"[net] PASS password and ban: wrong password refused (code 3), player 3 banned after turn {kick} "
              f"and refused on its way back (code 4); host and player 2 matched on all {turns} turns "
              f"(final hash {h[turns]})")
    else:
        for role in roles:
            for line in outs.get(role, "").splitlines():
                if line.startswith("[net-peer]") or line.startswith("[TurnManager]") or "Refused" in line:
                    print(f"   {role}: {line}")
        print(f"[net] FAIL password and ban: rc={rcs} refused={refused} kicked={kicked} heard={j1_heard} "
              f"turns={len(h)},{len(j1)} first_bad={bad}")
    return ok


def run_host_gone_case(binary: str, seed: int, speed: float, loss: float, port: int, out_dir: str,
                       timeout: int) -> bool:
    """0 A.D. netstatus "disconnected": the host ends the game after turn LEAVE and
    both clients must hear why ("The host has ended the game.", reason 2); then a
    host drops off without a word and its client must notice the silence on its
    own (reason 0, after the 10 s timeout)."""
    turns, leave = 400, 120
    print(f"\n---- host gone: the host ends the game after turn {leave}, then a host vanishes ----")
    results = {}
    for kind, flag, joiners, want in (("quit", "--host-quit-turn", ("j1", "j2"), "2"),
                                      ("vanish", "--host-vanish-turn", ("j1",), "0")):
        logs = {r: os.path.join(out_dir, f"hostgone_{kind}_{r}.log") for r in ("host", *joiners)}
        for path in logs.values():
            if os.path.exists(path):
                os.remove(path)
        common = ["--speed", str(speed), "--loss", str(loss), "--humans", str(1 + len(joiners))]
        def peer(role, log, *more):
            return Peer([binary, "--net-peer", role, str(port), str(seed), str(turns), log, *common, *more],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        procs = {"host": peer("host", logs["host"], flag, str(leave))}
        for j in joiners:
            time.sleep(0.3)
            procs[j] = peer("join", logs[j])
        outs, rcs = {}, {}
        for role, p in procs.items():
            try:
                out, _ = p.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                p.kill()
                out, _ = p.communicate()
            outs[role] = out or ""
            rcs[role] = p.returncode
        lost = {j: next((l.split("reason=")[1].strip() for l in outs[j].splitlines()
                         if "join: lost the host at turn" in l), None) for j in joiners}
        h = read_hashes(logs["host"])
        # The host leaves before it logs turn LEAVE; a client may be a turn or two behind it.
        js = [read_hashes(logs[j]) for j in joiners]
        same = all(j and max(j) >= leave - 10 and all(h.get(t) == x for t, x in j.items() if t < leave) for j in js)
        checks = {"exit codes": all(rc == 0 for rc in rcs.values()),
                  f"reason {want}": all(r == want for r in lost.values()),
                  "in sync until the host left": len(h) == leave - 1 and same}
        results[kind] = checks
        if not all(checks.values()):
            for role, out in outs.items():
                for line in out.splitlines():
                    if line.startswith("[net-peer]") or "Lost the connection" in line:
                        print(f"   {kind} {role}: {line}")
            print(f"[net] {kind}: rc={rcs} lost={lost} host_turns={len(h)}")
        port += 10
    failed = [f"{kind}: {name}" for kind, checks in results.items() for name, ok in checks.items() if not ok]
    if not failed:
        print(f"[net] PASS host gone: the host ended the game after turn {leave} and both clients were told "
              f"(reason 2); a host that vanished was noticed by its client on its own (reason 0)")
        return True
    print(f"[net] FAIL host gone: {', '.join(failed)}")
    return False


def run_oos_case(binary: str, seed: int, speed: float, port: int, out_dir: str, timeout: int) -> bool:
    """0 A.D. OOS report: the joiner corrupts its world after turn FORCE; the host
    must report FORCE+1 (the first turn whose hashes differ) and both peers must
    write an oos_dump."""
    force = 100
    print(f"\n---- forced out-of-sync: joiner corrupts its world after turn {force} ----")
    dirs = {r: os.path.join(out_dir, f"oos_{r}") for r in ("host", "join")}
    for d in dirs.values():
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d)
    procs = {}
    for role in ("host", "join"):
        procs[role] = Peer(
            [binary, "--net-peer", role, str(port), str(seed), "400", os.path.join(out_dir, f"oos_{role}.log"),
             "--speed", str(speed), "--force-oos", str(force), "--oos-dir", dirs[role],
             "--name", "alice" if role == "host" else "bob"],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(0.3)
    outs, rcs = [], []
    for role in ("join", "host"):
        try:
            out, _ = procs[role].communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            procs[role].kill()
            out, _ = procs[role].communicate()
        outs.append(out)
        rcs.append(procs[role].returncode)
    rep = peer_lines(outs, "oos")
    dumps_ok = True
    states = {}
    for role, d in dirs.items():
        txt = os.path.join(d, "oos_dump.txt")
        if not (os.path.exists(txt) and os.path.exists(os.path.join(d, "oos_dump.dat"))):
            dumps_ok = False
            continue
        with open(txt) as f:
            lines = f.read().splitlines()
        dumps_ok = dumps_ok and lines[0].strip() == f"oos turn: {force + 1}" and "-- units (id type team x z hp action carry)" in lines
        states[role] = [l for l in lines if l.startswith("player 0:")]
    # The readable state shows where they part: the joiner's extra food.
    dumps_ok = dumps_ok and len(states) == 2 and states["host"] != states["join"]
    # The host names the out-of-sync player by name; the joiner's state differs
    # from the host's, the host's own is the expected one (OutOfSyncNetwork).
    ok = (rcs == [0, 0] and len(rep) == 2 and all(r.get("turn") == str(force + 1) for r in rep.values())
          and rep["join"].get("from_host") == "1" and rep["host"].get("players") == "bob"
          and rep["join"].get("players") == "bob" and rep["host"].get("matches_host") == "1"
          and rep["join"].get("matches_host") == "0" and dumps_ok)
    if ok:
        print(f"[net] PASS forced OOS: both peers report turn {force + 1}, players bob; the joiner's state differs from the host's; readable dumps differ in player 0's food")
    else:
        for o in outs:
            for line in (o or "").splitlines():
                if line.startswith("[net-peer]") or "Out-of-sync" in line or "Out-Of-Sync" in line:
                    print("   " + line)
        print(f"[net] FAIL forced OOS: rc={rcs} reports={rep} dumps_ok={dumps_ok}")
    return ok


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
    ap.add_argument("--step-timeout", type=int, default=600,
                    help="hard limit in seconds on each UDP case's peers, so a hung case fails fast (default 600)")
    ap.add_argument("--rebuild", action="store_true",
                    help="force rebuild even if the binary is newer than sources")
    ap.add_argument("--verbose-build", action="store_true")
    ap.add_argument("--net-turns", type=int, default=600,
                    help="turns for the two-process UDP session (0 = skip; 1500 = 5 min)")
    ap.add_argument("--net-speed", type=float, default=8.0,
                    help="wall-clock speed-up for the UDP session (1 = real time)")
    ap.add_argument("--net-loss", type=float, default=0.05,
                    help="fraction of datagrams each peer drops on send")
    ap.add_argument("--net-port", type=int, default=20611)
    ap.add_argument("--net-defeat-turn", type=int, default=450,
                    help="wipe out the joiner's player at this turn of the UDP session (0 = off)")
    args = ap.parse_args()
    Peer.step_timeout = args.step_timeout

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

    net_ok = True
    if args.net_turns > 0:
        seed = args.seeds[0]
        print(f"\n---- two-process UDP session seed {seed} : {args.net_turns} turns "
              f"speed x{args.net_speed} loss {args.net_loss:.0%} ----")
        nr = run_net_case(binary_path, seed, args.net_turns, args.net_speed,
                          args.net_loss, args.net_port, out_dir, args.timeout,
                          args.net_defeat_turn)
        net_ok = nr["ok"]
        for line in nr["output"].splitlines():
            if not line.startswith("[NetHost]"):
                print("   " + line)
        if nr["divergent_turn"] is not None:
            print(f"[net] OOS at turn {nr['divergent_turn']} ({nr['seconds']:.1f}s)")
        elif nr["ok"]:
            print(f"[net] PASS  {nr['turns']} turns on both peers, "
                  f"final_hash={nr['final_hash']} ({nr['seconds']:.1f}s)")
            print(f"[net] ceasefire lifted at turn {nr['ceasefire_end'][0]} on both peers, "
                  f"no combat before it; setup {nr['setups'].get('host')}")
            print(f"[net] tribute of 77 stone landed at turn {next(t for t, d in nr['tributes'][0] if d == (77, -77))} on both peers, the "
                  f"post-defeat one was dropped; wall became a gate at turn {nr['gate_turn'][0]}, "
                  f"locked at turn {nr['locks'][0][0]} and unlocked at turn {nr['locks'][0][1]}")
            print(f"[net] stone thrower unpacked and packed again on both peers at turns "
                  f"{[t for t, _ in nr['packs'][0]]}")
            wt = nr["work_turns"]
            print(f"[net] worker gathered at turn {wt['gather']}, walked at {wt['walk']} with a queued walk "
                  f"at {wt['queued']} (done at {wt['queue_done']}), back to work at {wt['back']}, "
                  f"deleted at {wt['deleted']}, on both peers")
            print(f"[net] trading goods set at turns {[t for t, _ in nr['goods'][0]]} on both peers "
                  f"({nr['goods'][0][-1][1]})")
            ft = nr["form_turns"]
            print(f"[net] squad formed a box at turn {ft['box']}, walked in formation at {ft['walk']} and "
                  f"attack-moved at {ft['attack_walk']}, on both peers")
            ct, tt = nr["carry_turns"], nr["trade_turns"]
            print(f"[net] carrier took the drop-off order at turn {ct['order']} and emptied at {ct['dropped']}; "
                  f"trader walked with its route queued at {tt['walk']} and started trading at {tt['trade']}")
            print(f"[net] network status: the joiner got {nr['statuses']['join']['reports']} reports from the "
                  f"host, last round trips (client:ms) {nr['statuses']['join']['rtt']}")
            print(f"[net] healer took the heal order at turn {nr['heal_turn']}; the swordsman wounded to 40 hp "
                  f"healed to {nr['healed']:.0f} on both peers")
            print(f"[net] chat: host got {nr['chats']['host']}, joiner got {nr['chats']['join']}; "
                  f"host paused at turn {PAUSE_TURN}, joiner held {nr['pauses']['join']['held_ms']} ms; "
                  f"save/load at turn {SAVELOAD_TURN} kept hash {nr['saves']['host']['before']} on both")
            if args.net_defeat_turn:
                print(f"[net] joiner's player wiped at turn {args.net_defeat_turn}, defeated at turn "
                      f"{nr['defeat_turn'][0]} on both peers; host and Petra played on to turn {nr['turns']}")
        elif not nr["tribute_ok"]:
            print(f"[net] FAIL tribute: stone changes (host, join) {nr['tributes']}")
        elif not nr["pack_ok"]:
            print(f"[net] FAIL pack: state changes (turn, state) {nr['packs']}")
        elif not nr["form_ok"]:
            print(f"[net] FAIL formations: {nr['form_turns']}, changes (turn, tmpl:order:members) {nr['forms']}")
        elif not nr["carry_ok"]:
            print(f"[net] FAIL return resource: {nr['carry_turns']}, changes (turn, order:carry) {nr['carries']}")
        elif not nr["trade_ok"]:
            print(f"[net] FAIL trade route: {nr['trade_turns']}, changes (turn, order:queue) {nr['trades']}")
        elif not nr["goods_ok"]:
            print(f"[net] FAIL trading goods: changes (turn, goods) {nr['goods']}")
        elif not nr["status_ok"]:
            print(f"[net] FAIL network status: {nr['statuses']}")
        elif not nr["heal_ok"]:
            print(f"[net] FAIL heal: order at {nr['heal_turn']}, best hp {nr['healed']}")
        elif not nr["work_ok"]:
            print(f"[net] FAIL unit orders: {nr['work_turns']}, changes (turn, kind, queue) {nr['works']}")
        elif not nr["chat_ok"]:
            print(f"[net] FAIL chat: received {nr['chats']}")
        elif not nr["pause_ok"]:
            print(f"[net] FAIL pause: {nr['pauses']}")
        elif not nr["save_ok"]:
            print(f"[net] FAIL save/load: {nr['saves']}")
        elif not nr["lock_ok"]:
            print(f"[net] FAIL gate lock: lock changes at turns {nr['locks']} (host, join)")
        elif not nr["upgrade_ok"]:
            print(f"[net] FAIL upgrade: wall became a gate at turns {nr['gate_turn']} (host, join)")
        elif not nr["defeat_ok"]:
            print(f"[net] FAIL defeat: defeated at turns {nr['defeat_turn']} (host, join), "
                  f"game over {nr['game_over']}")
        elif not nr["ceasefire_ok"]:
            print(f"[net] FAIL ceasefire: lifted at turns {nr['ceasefire_end']} "
                  f"(host, join), combat during it at {nr['ceasefire_hits']}")
        elif not nr["setup_ok"]:
            print(f"[net] FAIL game setup differs between peers: {nr['setups']}")
        else:
            print(f"[net] FAIL rc={nr['rcs']} host_turns={nr['host_turns']} "
                  f"join_turns={nr['join_turns']} ({nr['seconds']:.1f}s)")

    if args.net_turns > 0:
        net_ok = run_oos_case(binary_path, args.seeds[0], args.net_speed, args.net_port + 1,
                              out_dir, args.timeout) and net_ok
        net_ok = run_rejoin_case(binary_path, args.seeds[0], args.net_speed, args.net_loss,
                                 args.net_port + 2, out_dir, args.timeout) and net_ok
        net_ok = run_three_human_case(binary_path, args.seeds[0], args.net_speed, args.net_loss,
                                      args.net_port + 3, out_dir, args.timeout) and net_ok
        net_ok = run_kick_case(binary_path, args.seeds[0], args.net_speed, args.net_loss,
                               args.net_port + 4, out_dir, args.timeout) and net_ok
        net_ok = run_observer_case(binary_path, args.seeds[0], args.net_speed, args.net_loss,
                                   args.net_port + 5, out_dir, args.timeout) and net_ok
        net_ok = run_host_gone_case(binary_path, args.seeds[0], args.net_speed, args.net_loss,
                                    args.net_port + 6, out_dir, args.timeout) and net_ok
        net_ok = run_observer_lag_case(binary_path, args.seeds[0], args.net_speed, args.net_loss,
                                       args.net_port + 8, out_dir, args.timeout) and net_ok
        net_ok = run_saved_game_case(binary_path, args.seeds[0], args.net_speed, args.net_loss,
                                     args.net_port + 9, out_dir, args.timeout) and net_ok
        # The observer case again on a link that loses 45% of its datagrams: every
        # reliable packet (turn batches, the rejoin world's chunks) must still
        # arrive, as ENet's do, or the late observer stalls or never gets in.
        print("\n---- heavy loss: the observer case with 45% of datagrams lost ----")
        net_ok = run_observer_case(binary_path, args.seeds[0], args.net_speed, 0.45,
                                   args.net_port + 7, out_dir, args.timeout) and net_ok

    oos = [r for r in results if r["divergent_turn"] is not None]
    if not net_ok and not oos:
        return 1
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
