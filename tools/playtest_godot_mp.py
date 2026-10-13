#!/usr/bin/env python3
"""A real two-client multiplayer session: two headless Godot game clients go
through the menu lobby (tools/lobby_server.py) as players would. One signs in
and hosts; the other signs in, finds the game in the list and joins it; the
host presses Start, and both play to a turn whose lockstep hash must match.
Meanwhile the lobby must show the game running with two players and both
players as in a game, as 0 A.D.'s lobby does.

Needs a Godot 4.3 binary (--godot or $GODOT) and an imported project
(.godot/) with the built GDExtension in bin/:

  python3 tools/playtest_godot_mp.py --godot /path/to/godot
"""
import argparse
import os
import shutil
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import lobby_server  # noqa: E402


def gmp_lines(text):
    return [line for line in (text or "").splitlines() if line.startswith("[gmp]")]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--godot", default=os.environ.get("GODOT") or shutil.which("godot") or "")
    ap.add_argument("--port", type=int, default=20640, help="the game's UDP port")
    ap.add_argument("--turns", type=int, default=150)
    ap.add_argument("--timeout", type=int, default=300, help="seconds each client may take")
    args = ap.parse_args()
    if not args.godot or not os.path.exists(args.godot):
        print("no Godot binary: pass --godot or set $GODOT")
        return 2

    server = lobby_server.serve("127.0.0.1", 0, verbose=False)
    lobby_server.LOBBY = lobby_server.Lobby()
    threading.Thread(target=server.serve_forever, daemon=True).start()
    lobby = f"127.0.0.1:{server.server_address[1]}"

    def client(role, name):
        cmd = [args.godot, "--headless", "--path", ROOT, "-s", "res://tools/godot_mp_probe.gd", "--",
               f"role={role}", f"name={name}", f"lobby={lobby}", f"port={args.port}",
               f"turns={args.turns}", f"timeout={args.timeout}"]
        return subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    host = client("host", "alice")
    time.sleep(2.0)
    join = client("join", "bob")

    # What the lobby showed while the match ran.
    seen = {"running": False, "two": False, "playing": set()}
    procs = (host, join)
    deadline = time.time() + args.timeout + 30
    while any(p.poll() is None for p in procs) and time.time() < deadline:
        with lobby_server.LOBBY.lock:
            rooms = lobby_server.LOBBY.room_list()[1]["rooms"]
            players = lobby_server.LOBBY.player_list()[1]["players"]
        room = next((r for r in rooms if r["id"] == f"room-{args.port}"), None)
        if room and room["state"] == "running":
            seen["running"] = True
        if room and room["players"] == 2:
            seen["two"] = True
        seen["playing"] |= {p["name"] for p in players if p["status"] == "playing"}
        time.sleep(0.5)
    outs = {}
    for role, p in (("host", host), ("join", join)):
        try:
            out, _ = p.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            p.kill()
            out, _ = p.communicate()
        outs[role] = out
    server.shutdown()

    def done(role):
        for line in gmp_lines(outs[role]):
            if " done " in line:
                return dict(kv.split("=", 1) for kv in line.split(" done ", 1)[1].split())
        return {}

    h, j = done("host"), done("join")
    ok = (host.returncode == 0 and join.returncode == 0 and h and j
          and h.get("hash") == j.get("hash") and h.get("hash") not in (None, "00000000")
          and h.get("seat") == "0" and j.get("seat") == "1"
          and seen["running"] and seen["two"] and seen["playing"] == {"alice", "bob"})
    for role in ("host", "join"):
        for line in gmp_lines(outs[role]):
            print("   " + line)
    if ok:
        print(f"[gmp] PASS two Godot clients: lobby sign-in, host, join from the game list, Start; "
              f"turn {args.turns} hash {h['hash']} on both (seats 0 and 1); the lobby showed the game "
              f"running with 2 players and alice and bob in a game")
        return 0
    if not (h and j):
        for role in ("host", "join"):
            print(f"---- {role} output (tail) ----")
            print("\n".join((outs[role] or "").splitlines()[-40:]))
    print(f"[gmp] FAIL rc host/join={host.returncode}/{join.returncode} host={h} join={j} lobby={seen}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
