#!/usr/bin/env python3
"""A real two-client multiplayer session: two headless Godot game clients go
through the menu lobby (tools/lobby_server.py) as players would. One signs in
and hosts; the other signs in, finds the game in the list and joins it; the
host presses Start, and both play to a turn whose lockstep hash must match. The client freezes
for a few seconds as the match starts, so the host must show "Waiting for
players to connect:" with its name.
In game setup the client asks for the host's colour (refused, with the
reason), then for Germans, team 2 and colour 5, and presses Ready; the host's Start must be refused until then, and both must
play with those picks at the host's game speed (1.5x).
Meanwhile the lobby must show the game running with two players and both
players as in a game, as 0 A.D.'s lobby does.

It plays that match twice. The first time the client then leaves, and the
host's chat must say "bob has left the game."; the second time the host leaves,
and the client must show "Connection to the server has been lost." with "The
host has ended the game.", whose Exit opens the summary as disconnected.

Both clients sit behind a simulated NAT firewall (each drops datagrams from
endpoints it never sent to; --no-firewall turns it off), so the join works
only through 0 A.D.'s NAT traversal: both find their public endpoint with the
lobby's STUN responder, the joiner sends its endpoint through the lobby, and
the host punches a hole towards it.

Needs a Godot 4.3 binary (--godot or $GODOT) and an imported project
(.godot/) with the built GDExtension in bin/:

  python3 tools/playtest_godot_mp.py --godot /path/to/godot
"""
import argparse
import os
import shutil
import signal
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
    ap.add_argument("--no-firewall", action="store_true",
                    help="skip the simulated NAT firewall (each peer otherwise drops datagrams from "
                         "endpoints it never sent to, so the join needs STUN and the lobby's hole punch)")
    args = ap.parse_args()
    if not args.godot or not os.path.exists(args.godot):
        print("no Godot binary: pass --godot or set $GODOT")
        return 2

    server = lobby_server.serve("127.0.0.1", 0, verbose=False)
    lobby_server.LOBBY = lobby_server.Lobby()
    threading.Thread(target=server.serve_forever, daemon=True).start()
    lobby = f"127.0.0.1:{server.server_address[1]}"
    stun = lobby_server.serve_stun("127.0.0.1", 0)
    args.stun_port = stun.getsockname()[1]

    ok = True
    for leave in ("join", "host"):
        ok = play(args, lobby, leave) and ok
    server.shutdown()
    stun.close()
    return 0 if ok else 1


def play(args, lobby, leave):
    print(f"[gmp] ---- one match; then the {leave} leaves ----")
    port = args.port + (1 if leave == "host" else 0)  # a fresh room each time
    def client(role, name):
        cmd = [args.godot, "--headless", "--path", ROOT, "-s", "res://tools/godot_mp_probe.gd", "--",
               f"role={role}", f"name={name}", f"lobby={lobby}", f"port={port}",
               f"turns={args.turns}", f"timeout={args.timeout}", f"leave={leave}", f"stun={args.stun_port}", f"lan={1 if lan else 0}"]
        if not args.no_firewall:
            cmd.append("net_firewall=1")
        return subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    # The first match joins at the host's public address (NAT on both sides);
    # the second, from behind the host's own public address, at its LAN one.
    lan = leave == "host"
    host = client("host", "alice")
    time.sleep(2.0)
    if lan:
        until = time.time() + 30
        while time.time() < until:
            with lobby_server.LOBBY.lock:
                raw = next((r for r in lobby_server.LOBBY.rooms if r["id"] == f"room-{port}"), {})
            if raw.get("local_ip"):
                break
            time.sleep(0.25)
        # A machine with no route has no LAN address to publish.
        lan = bool(raw.get("local_ip"))
        if not lan:
            print("[gmp] note: the host found no LAN address; the joiner uses the public one")
    join = client("join", "bob")
    # Read both outputs as they come, so the host's Start can be seen live.
    lines = {"host": [], "join": []}
    def read(role, p):
        for line in p.stdout:
            lines[role].append(line.rstrip("\n"))
    readers = [threading.Thread(target=read, args=(r, p), daemon=True) for r, p in (("host", host), ("join", join))]
    for t in readers:
        t.start()
    # The client freezes for a few seconds as the match starts, as a slow
    # loader would: the host must show "Waiting for players to connect:" bob.
    frozen = None

    # What the lobby showed while the match ran.
    seen = {"running": False, "two": False, "playing": set(), "names": set(), "started_at": False,
            "punched": False}
    procs = (host, join)
    deadline = time.time() + args.timeout + 30
    while any(p.poll() is None for p in procs) and time.time() < deadline:
        with lobby_server.LOBBY.lock:
            rooms = lobby_server.LOBBY.room_list()[1]["rooms"]
            players = lobby_server.LOBBY.player_list()[1]["players"]
            raw = next((r for r in lobby_server.LOBBY.rooms if r["id"] == f"room-{port}"), None)
            # The joiner's STUN endpoint, sent for the host to punch towards.
            seen["punched"] = seen["punched"] or bool(raw and raw.get("punch"))
        room = next((r for r in rooms if r["id"] == f"room-{port}"), None)
        if room and room["state"] == "running":
            seen["running"] = True
        if room and room["players"] == 2:
            seen["two"] = True
        if room:
            # The game's details (GameDetails.js): who plays and when it started.
            seen["names"] |= {p["name"] for p in room.get("player_names", []) if not p.get("ai")}
            seen["started_at"] = seen["started_at"] or room.get("start_time", 0) > 0
        seen["playing"] |= {p["name"] for p in players if p["status"] == "playing"}
        if frozen is None and any("pressing Start" in l for l in lines["host"]):
            os.kill(join.pid, signal.SIGSTOP)
            frozen = time.time()
        elif frozen and time.time() - frozen > 4.0:
            os.kill(join.pid, signal.SIGCONT)
            frozen = 0
        time.sleep(0.25)
    if frozen:
        os.kill(join.pid, signal.SIGCONT)
    for p in procs:
        try:
            p.wait(timeout=10)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()
    for t in readers:
        t.join(timeout=5)
    outs = {role: "\n".join(lines[role]) for role in lines}
    if os.environ.get("GMP_DEBUG"):
        for role in lines:
            for l in lines[role]:
                if "NetHost" in l or "[gmp]" in l or "TurnManager" in l:
                    print(f"   {role}| {l}")

    def done(role):
        for line in gmp_lines(outs[role]):
            if " done " in line:
                return dict(kv.split("=", 1) for kv in line.split(" done ", 1)[1].split())
        return {}

    h, j = done("host"), done("join")
    ok = (host.returncode == 0 and join.returncode == 0 and h and j
          and h.get("hash") == j.get("hash") and h.get("hash") not in (None, "00000000")
          and h.get("seat") == "0" and j.get("seat") == "1"
          and seen["running"] and seen["two"] and seen["playing"] == {"alice", "bob"}
          and seen["names"] == {"alice", "bob"} and seen["started_at"])
    def said(role, key):
        return next((l.split(f" {key}=", 1)[1] for l in gmp_lines(outs[role]) if f" {key}=" in l), "")
    stun_found = all("[NetHost] STUN: public endpoint" in outs[role] for role in ("host", "join"))
    retargeted = "instead" in outs["join"]
    ok = ok and stun_found and seen["punched"] and retargeted == lan
    waited = said("host", "waiting")
    ok = ok and waited == "Waiting for players to connect: bob"
    ok = ok and said("host", "start_refused") == "true"
    ok = ok and said("join", "refused") == "Another player has that color"
    picks = {k: (h.get(k), j.get(k)) for k in ("speed", "civ1", "team1", "color1")}
    ok = ok and picks == {"speed": ("1.5", "1.5"), "civ1": ("15", "15"), "team1": ("2", "2"), "color1": ("5", "5")}
    if leave == "join":
        left = said("host", "chat_left")
        # Once, when bob really leaves: a slow start must not read as a leave.
        ok = ok and left.endswith("bob has left the game.") and said("host", "chat_left_lines") == "1"
    else:
        status = said("join", "net_status")
        ok = (ok and status.startswith("Connection to the server has been lost. | The host has ended the game.")
              and status.endswith("reason=2") and said("join", "summary_disconnected") == "true")
    for role in ("host", "join"):
        for line in gmp_lines(outs[role]):
            print("   " + line)
    if ok:
        print("[gmp] PASS the host's Start was refused until the client was ready; both played its picks "
              "(Germans, team 2, colour 5) at 1.5x")
        print(f"[gmp] PASS while the client was frozen at the start, the host showed: {waited}")
    if ok and leave == "join":
        print(f"[gmp] PASS the client left and the host's chat said: {said('host', 'chat_left')}")
    elif ok:
        print(f"[gmp] PASS the host left and the client showed: {said('join', 'net_status')}; "
              f"Exit opened the summary as disconnected")
    if ok:
        print("[gmp] PASS both peers found their public endpoint by STUN and the joiner's went through the "
              "lobby; " + ("behind simulated NAT firewalls, only the hole punches let the join in"
                           if not args.no_firewall else "no firewall") +
              ("; behind the host's own public address it joined at the host's LAN address" if lan
               else "; it joined at the host's public address"))
    if ok:
        print(f"[gmp] PASS two Godot clients: lobby sign-in, host, join from the game list, Start; "
              f"turn {args.turns} hash {h['hash']} on both (seats 0 and 1); the lobby showed the game "
              f"running with 2 players, alice and bob in a game and in its details, and its start time")
        return True
    if not (h and j):
        for role in ("host", "join"):
            print(f"---- {role} output (tail) ----")
            print("\n".join((outs[role] or "").splitlines()[-40:]))
    print(f"[gmp] FAIL rc host/join={host.returncode}/{join.returncode} host={h} join={j} lobby={seen} "
          f"stun={stun_found} lan={lan} retargeted={retargeted}")
    return False


if __name__ == "__main__":
    sys.exit(main())
