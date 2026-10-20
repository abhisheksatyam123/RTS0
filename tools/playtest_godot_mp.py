#!/usr/bin/env python3
"""A real two-client multiplayer session: two headless Godot game clients go
through the menu lobby (tools/lobby_server.py) as players would. One signs in
and hosts; the other signs in, finds the game in the list and joins it; the
host presses Start, and both play to a turn whose lockstep hash must match. The client freezes
for a few seconds as the match starts, so the host must show "Waiting for
players to connect:" with its name.
In game setup the client asks for a civ (refused: 0.28 lets only the host
change the setup), the host gives it Germans, team 2 and colour 5, and the client presses Ready; the host's Start must be refused until then, and both must
play with those picks at the host's game speed (1.5x).
Meanwhile the lobby must show the game running with two players and both
players as in a game, as 0 A.D.'s lobby does.

It plays that match twice. The first time the client then leaves, and the
host's chat must say "bob has left the game."; the second time the host leaves,
and the client must show "Connection to the server has been lost." with "The
host has ended the game.", whose Exit opens the summary as disconnected.
Then the host saves a match, hosts that saved game, and the client must load
the host's world into its old seat and stay in step with it. Last, a join at
the signed-in host's typed address must be refused as 0.28's lobby
authentication does ("Secure lobby authentication failed. Join via lobby.").

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
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
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
    ok = play_saved(args, lobby) and ok
    ok = play_setup_leave(args, lobby) and ok
    ok = play_connect_timeout(args, lobby) and ok
    ok = play_connect_cancel(args, lobby) and ok
    ok = play_direct_join_refused(args, lobby) and ok
    server.shutdown()
    stun.close()
    return 0 if ok else 1


def play_saved(args, lobby):
    """0 A.D. Host Saved Game: a two-player match is played alone for 20 s and
    saved; the host hosts that save, the joiner joins from the game list, and
    at Start the joiner loads the host's world (it never built its own) and
    takes the second seat. The setup refuses the joiner's civ, team and
    colour picks (0.28 locks every setting of a saved game but the seats). Their turn hashes must match."""
    print("[gmp] ---- a saved game: the host continues it, the joiner loads its world ----")
    port = args.port + 2
    def client(role, name, *more):
        cmd = [args.godot, "--headless", "--path", ROOT, "-s", "res://tools/godot_mp_probe.gd", "--",
               f"role={role}", f"name={name}", f"lobby={lobby}", f"port={port}", f"turns={args.turns}",
               f"timeout={args.timeout}", f"stun={args.stun_port}", "lan=0", *more]
        if not args.no_firewall:
            cmd.append("net_firewall=1")
        # A file, not a pipe: a peer whose pipe fills while the other is read blocks.
        log = tempfile.TemporaryFile(mode="w+")
        p = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, text=True)
        p.log = log
        return p
    def output(p):
        p.log.seek(0)
        return p.log.read()
    maker = client("make_save", "alice", "saves=gmp_saved_game")
    maker.wait(timeout=args.timeout)
    out_save = output(maker)
    if maker.returncode != 0 or "saved gmp_saved_game" not in out_save:
        print("\n".join(out_save.splitlines()[-30:]))
        print("[gmp] FAIL could not make the save")
        return False
    host = client("host", "alice", "saved=gmp_saved_game")
    time.sleep(2.0)
    join = client("join", "bob")
    outs = {}
    for role, p in (("join", join), ("host", host)):
        try:
            p.wait(timeout=args.timeout + 30)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()
        outs[role] = output(p)
    def done(role):
        for line in gmp_lines(outs[role]):
            if " done " in line:
                return dict(kv.split("=", 1) for kv in line.split(" done ", 1)[1].split())
        return {}
    h, j = done("host"), done("join")
    hosted = "HOST continues saved game" in outs["host"]
    loaded = "rejoined as client" in outs["join"] and "JOINER consumed NMT_GAME_START" not in outs["join"]
    ok = (host.returncode == 0 and join.returncode == 0 and h and j and hosted and loaded
          and h.get("hash") == j.get("hash") and h.get("hash") not in (None, "00000000")
          and h.get("seat") == "0" and j.get("seat") == "1"
          # GameSettingControl EnabledWhenSavedGame: the joiner's own picks are refused.
          and "refused=The controller loaded a saved game" in outs["join"]
          # A saved game's clients load it at Start: nobody "has rejoined".
          and h.get("rejoined_lines") == "0" and j.get("rejoined_lines") == "0")
    for role in ("host", "join"):
        for line in gmp_lines(outs[role]):
            print("   " + line)
    if ok:
        print(f"[gmp] PASS a saved game: the host hosted the save, the joiner loaded the host's world at Start "
              f"and took seat 1; turn {args.turns} hash {h['hash']} on both")
        return True
    for role in ("host", "join"):
        print(f"---- {role} output ----")
        print("\n".join(l for l in (outs[role] or "").splitlines()
                        if "[RTS0]" in l or "TurnManager" in l or "NetHost" in l or "[gmp]" in l))
    print(f"[gmp] FAIL saved game rc host/join={host.returncode}/{join.returncode} host={h} join={j} "
          f"hosted={hosted} loaded={loaded}")
    return False


def play_connect_cancel(args, lobby):
    """0.28 gamesetup_mp pageConnecting: a join shows "Connecting to server..."
    with Cancel over the screen, and Cancel goes back to the lobby."""
    print("[gmp] ---- Cancel while connecting ----")
    dead = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dead.bind(("127.0.0.1", 0))  # a port that swallows datagrams
    cmd = [args.godot, "--headless", "--path", ROOT, "-s", "res://tools/godot_mp_probe.gd", "--",
           "role=join", "name=carol", f"lobby={lobby}", f"timeout={args.timeout}",
           f"stun={args.stun_port}", "lan=0", f"join_addr=127.0.0.1:{dead.getsockname()[1]}", "join_cancel=1"]
    try:
        out = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                             timeout=args.timeout + 30).stdout
    except subprocess.TimeoutExpired as e:
        out = e.stdout or ""
    dead.close()
    box = next((l.split(" connecting_box=", 1)[1] for l in gmp_lines(out) if " connecting_box=" in l), "")
    scene = next((l.split(" connect_exit_scene=", 1)[1] for l in gmp_lines(out) if " connect_exit_scene=" in l), "")
    for line in gmp_lines(out):
        print("   " + line)
    if box == "Connecting to server..." and scene == "res://scenes/menu/lobby.tscn":
        print("[gmp] PASS the join showed \"Connecting to server...\" and Cancel went back to the lobby")
        return True
    print(f"[gmp] FAIL connect cancel box={box!r} scene={scene!r}")
    return False


def play_connect_timeout(args, lobby):
    """0.28 reportConnectionRequestTimeOut: a join at an address where nobody
    answers gives up with "Failed to connect to the server. The connection
    request has timed out." instead of waiting forever."""
    print("[gmp] ---- a join nobody answers ----")
    dead = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dead.bind(("127.0.0.1", 0))  # a port that swallows datagrams
    cmd = [args.godot, "--headless", "--path", ROOT, "-s", "res://tools/godot_mp_probe.gd", "--",
           "role=join", "name=carol", f"lobby={lobby}", f"timeout={args.timeout}",
           f"stun={args.stun_port}", "lan=0", f"join_addr=127.0.0.1:{dead.getsockname()[1]}"]
    log = tempfile.TemporaryFile(mode="w+")
    t0 = time.time()
    p = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, text=True)
    try:
        p.wait(timeout=args.timeout + 30)
    except subprocess.TimeoutExpired:
        p.kill()
        p.wait()
    dead.close()
    log.seek(0)
    out = log.read()
    status = next((l.split(" connect_status=", 1)[1] for l in gmp_lines(out) if " connect_status=" in l), "")
    scene = next((l.split(" connect_exit_scene=", 1)[1] for l in gmp_lines(out) if " connect_exit_scene=" in l), "")
    box = next((l.split(" connect_box=", 1)[1] for l in gmp_lines(out) if " connect_box=" in l), "")
    ok = (p.returncode == 0 and status.startswith("Failed to connect to the server. The connection request has timed out.")
          and "Please ensure" not in status and status.endswith("reason=0") and scene == "res://scenes/menu/lobby.tscn"
          and box == "Connection Error buttons=Close,Open FAQ")
    for line in gmp_lines(out):
        print("   " + line)
    if ok:
        print(f"[gmp] PASS a join nobody answered gave up after {time.time() - t0:.0f}s with: {status.split(' | ')[0]}; "
              "in a \"Connection Error\" box with Close and Open FAQ; Close went back to the lobby")
        return True
    print("\n".join(l for l in out.splitlines() if "NetHost" in l or "[gmp]" in l)[-3000:])
    print(f"[gmp] FAIL connect timeout rc={p.returncode} status={status!r} scene={scene!r}")
    return False


def play_direct_join_refused(args, lobby):
    """0.28 lobby authentication: a host signed in to the lobby admits only
    joiners the lobby vouches for, so a join at its typed address is refused
    with "Secure lobby authentication failed. Join via lobby." (reason 10)."""
    print("[gmp] ---- a typed-address join to a lobby host ----")
    port = args.port + 4
    base = [args.godot, "--headless", "--path", ROOT, "-s", "res://tools/godot_mp_probe.gd", "--",
            f"lobby={lobby}", f"timeout={args.timeout}", f"stun={args.stun_port}", "lan=0"]
    host_log = tempfile.TemporaryFile(mode="w+")
    host = subprocess.Popen(base + ["role=host", "name=alice", f"port={port}"],
                            stdout=host_log, stderr=subprocess.STDOUT, text=True)
    time.sleep(2.0)
    log = tempfile.TemporaryFile(mode="w+")
    p = subprocess.Popen(base + ["role=join", "name=dave", f"join_addr=127.0.0.1:{port}"],
                         stdout=log, stderr=subprocess.STDOUT, text=True)
    try:
        p.wait(timeout=args.timeout + 30)
    except subprocess.TimeoutExpired:
        p.kill()
        p.wait()
    host.kill()
    host.wait()
    log.seek(0)
    out = log.read()
    status = next((l.split(" connect_status=", 1)[1] for l in gmp_lines(out) if " connect_status=" in l), "")
    box = next((l.split(" connect_box=", 1)[1] for l in gmp_lines(out) if " connect_box=" in l), "")
    ok = (p.returncode == 0 and
          status == "Failed to connect to the server. | Secure lobby authentication failed. Join via lobby. reason=10"
          and box == " buttons=Exit")
    for line in gmp_lines(out):
        print("   " + line)
    if ok:
        print("[gmp] PASS a typed-address join to a lobby host was refused: Secure lobby authentication failed. "
              "Join via lobby.")
        return True
    print("\n".join(l for l in out.splitlines() if "NetHost" in l or "[gmp]" in l)[-3000:])
    print(f"[gmp] FAIL direct join rc={p.returncode} status={status!r}")
    return False


def play_setup_leave(args, lobby):
    """0.28 SetupWindow netwarn: bob freezes in the setup, and the host's setup
    page shows "bob losing connection" over itself. Then 0.28 reportDisconnect
    in the game setup: the host leaves before Start;
    the client's setup page closes for "Lost connection to the server." with
    the reason, and its OK goes back to the lobby."""
    print("[gmp] ---- the host leaves the game setup before Start ----")
    port = args.port + 3
    def client(role, name):
        cmd = [args.godot, "--headless", "--path", ROOT, "-s", "res://tools/godot_mp_probe.gd", "--",
               f"role={role}", f"name={name}", f"lobby={lobby}", f"port={port}", f"timeout={args.timeout}",
               f"stun={args.stun_port}", "lan=0", "setup_leave=1"]
        if not args.no_firewall:
            cmd.append("net_firewall=1")
        log = tempfile.TemporaryFile(mode="w+")
        p = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, text=True)
        p.log = log
        return p
    host = client("host", "alice")
    time.sleep(2.0)
    join = client("join", "bob")
    # Once the host has made bob an observer, bob freezes for a few seconds
    # (a stalled client in the setup) and the host's setup page must show it.
    deadline = time.time() + args.timeout
    while host.poll() is None and time.time() < deadline:
        with open(f"/proc/self/fd/{host.log.fileno()}") as f:
            if "unassigned slot 2" in f.read():
                break
        time.sleep(0.25)
    if join.poll() is None:
        os.kill(join.pid, signal.SIGSTOP)
        time.sleep(4.0)
        os.kill(join.pid, signal.SIGCONT)
    outs = {}
    for role, p in (("join", join), ("host", host)):
        try:
            p.wait(timeout=args.timeout + 30)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()
        p.log.seek(0)
        outs[role] = p.log.read()
    def said(key, role="join"):
        return next((l.split(f" {key}=", 1)[1] for l in gmp_lines(outs[role]) if f" {key}=" in l), "")
    # SetupWindow.js netwarn: over the setup page (layer 20), not under it.
    warning = said("setup_net_warning", "host")
    status = said("setup_status")
    moved = said("moved_to_observers")
    ok = (host.returncode == 0 and join.returncode == 0 and moved == "Observers: bob"
          and status == "Lost connection to the server. | The host has ended the game. reason=2 saw_page=true page=false"
          and said("setup_exit_scene") == "res://scenes/menu/lobby.tscn"
          and warning.startswith("bob losing connection (") and warning.endswith(" layer=22")
          # MapSelection.js Random: the host picked a map, the client saw only "Random".
          and said("setup_map") == "random" and said("random_pick", "host").endswith(" hidden=true")
          and not said("random_pick", "host").startswith("random "))
    for role in ("host", "join"):
        for line in gmp_lines(outs[role]):
            print("   " + line)
    if ok:
        print("[gmp] PASS the host's player assignment made bob an observer; his page listed: Observers: bob")
        print(f"[gmp] PASS the host chose Random and picked {said('random_pick', 'host').split()[0]}; the client's setup showed Random")
        print(f"[gmp] PASS bob froze in the setup; the host's setup page showed over it: {warning}")
        print(f"[gmp] PASS the host left the setup; the client's page closed for: {status.split(' reason=')[0]}; "
              f"OK went back to the lobby")
        return True
    for role in ("host", "join"):
        print(f"---- {role} output ----")
        print("\n".join(l for l in outs[role].splitlines() if "NetHost" in l or "[gmp]" in l)[-4000:])
    print(f"[gmp] FAIL setup leave rc host/join={host.returncode}/{join.returncode} status={status!r}")
    return False

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
            "punched": False, "max": set(), "start_at": 0.0, "running_at": 0.0}
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
            seen["running_at"] = seen["running_at"] or time.time()
        if room:
            seen["max"].add(room.get("max_players"))
        if room and room["players"] == 2:
            seen["two"] = True
        if room:
            # The game's details (GameDetails.js): who plays and when it started.
            seen["names"] |= {p["name"] for p in room.get("player_names", []) if not p.get("ai")}
            seen["started_at"] = seen["started_at"] or room.get("start_time", 0) > 0
        seen["playing"] |= {p["name"] for p in players if p["status"] == "playing"}
        if frozen is None and any("pressing Start" in l for l in lines["host"]):
            seen["start_at"] = time.time()
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
          and seen["names"] == {"alice", "bob"} and seen["started_at"]
          # LobbyGameRegistration.js: maxnbp is the map's player count, and the
          # host reports Start within moments, not on its next heartbeat.
          and seen["max"] == {2} and seen["start_at"] and 0 < seen["running_at"] - seen["start_at"] < 8.0)
    def said(role, key):
        return next((l.split(f" {key}=", 1)[1] for l in gmp_lines(outs[role]) if f" {key}=" in l), "")
    stun_found = all("[NetHost] STUN: public endpoint" in outs[role] for role in ("host", "join"))
    retargeted = "instead" in outs["join"]
    ok = ok and stun_found and seen["punched"] and retargeted == lan
    waited = said("host", "waiting")
    ok = ok and waited == "Waiting for players to connect: bob"
    # overlay.netwarnings (0 A.D. getNetworkWarnings): the frozen client reads
    # as losing connection while it is silent for over two seconds.
    net_warning = said("host", "net_warning")
    ok = ok and net_warning.startswith("bob losing connection (")
    ok = ok and said("host", "start_refused") == "true"
    ok = ok and said("join", "refused") == "Only the controller can change the game setup"
    # 0.28 gamesetup ChatPanel: the host's setup chat shows bob join, get ready and talk.
    setup_chat = said("host", "setup_chat")
    ok = ok and all(t in setup_chat for t in ("bob has joined", "* bob is ready!", "hello from bob",
                                              "<bob> /allies team up"))
    # 0.28 LobbyButton: the host opens the lobby as a dialog over the setup and closes it again.
    ok = ok and said("host", "lobby_dialog") == "true listed=true join_hidden=true closed=true"
    # 0.28 /list on both: the users by seat, and the command is not a chat line.
    users = {role: said(role, "users").replace(" list=", " ") for role in ("host", "join")}
    ok = ok and users == {"host": "2 alice,_bob", "join": "2 alice,_bob"}
    ok = ok and not any("> /list" in outs[role] for role in ("host", "join"))
    # 0.28 ChatAddressees: the other player as "■ name"; Tab completes alice and bob.
    addressees = {role: said(role, "addressees").split(" names=") for role in ("host", "join")}
    # When the host leaves, it may have ended the game before the client gets
    # there; then the client opens no chat at all (Chat.openPage, g_Disconnected).
    join_lost = leave == "host" and addressees["join"] == ["lost", "-"]
    ok = ok and addressees["host"] == ["Everyone|Allies|Enemies|Observers|■_bob", "alice,bob"]
    ok = ok and (join_lost or addressees["join"] == ["Everyone|Allies|Enemies|Observers|■_alice", "alice,bob"])
    # 0.28 PauseOverlay: the host's pause reads on the client as its name in its player colour.
    paused_by = said("join", "paused_by")
    # 0.28 PauseControl.canPause: "Waiting for players to connect:" allows no pause.
    wait_pause = [said(r, "wait_can_pause") for r in ("host", "join")]
    ok = ok and all(w in ("", "false") for w in wait_pause)
    ok = ok and re.fullmatch(r"\[center\]Paused by \[color=#[0-9a-f]{6}\]alice\[/color\]\[/center\]", paused_by) is not None
    ok = ok and "#ffffff]alice" not in paused_by
    # 0.28 page_loading at "start": both peers load the map under the loading page.
    loading = {role: said(role, "start_loading").split(",") for role in ("host", "join")}
    ok = ok and all("Building_terrain" in v for v in loading.values())
    picks = {k: (h.get(k), j.get(k)) for k in ("speed", "civ1", "team1", "color1")}
    ok = ok and picks == {"speed": ("1.5", "1.5"), "civ1": ("15", "15"), "team1": ("2", "2"), "color1": ("5", "5")}
    if leave == "join":
        left = said("host", "chat_left")
        # Once, when bob really leaves: a slow start must not read as a leave.
        ok = ok and left.endswith("bob has left the game.") and said("host", "chat_left_lines") == "1"
        ok = ok and said("host", "report_offline") == "bob"
    else:
        status = said("join", "net_status")
        ok = (ok and status.startswith("Connection to the server has been lost. | The host has ended the game.")
              and status.endswith("reason=2") and said("join", "summary_disconnected") == "true"
              and said("join", "chat_after_lost") == "false open=false")
    for role in ("host", "join"):
        for line in gmp_lines(outs[role]):
            print("   " + line)
    if ok:
        print("[gmp] PASS the host's Start was refused until the client was ready; both played its picks "
              "(Germans, team 2, colour 5) at 1.5x")
        print(f"[gmp] PASS while the client was frozen at the start, the host showed: {waited}; "
              f"network warning: {net_warning}")
        print("[gmp] PASS /list on both read: Users (2): alice, bob")
        print(f"[gmp] PASS the host's setup chat read: {setup_chat}")
        print("[gmp] PASS the setup's Lobby button opened the lobby as a dialog (game listed, no Host or Join) and Back closed it")
        print(f"[gmp] PASS the host's pause showed on the client as: {paused_by}")
        print(f"[gmp] PASS both loaded the map at Start under the loading page: {loading['join']}")
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
              f"running with 2 players, alice and bob in a game and in its details, and its start time; "
              f"listed as n/2 all along, and running {seen['running_at'] - seen['start_at']:.1f}s after Start")
        return True
    if not (h and j):
        for role in ("host", "join"):
            print(f"---- {role} output (tail) ----")
            print("\n".join((outs[role] or "").splitlines()[-40:]))
    print(f"[gmp] FAIL rc host/join={host.returncode}/{join.returncode} host={h} join={j} lobby={seen} "
          f"stun={stun_found} lan={lan} retargeted={retargeted} users={users}")
    return False


if __name__ == "__main__":
    sys.exit(main())
