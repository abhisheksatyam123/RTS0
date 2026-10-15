#!/usr/bin/env python3
"""Checks tools/lobby_server.py: sign-in, presence, chat, the game list and
timeouts, first on the Lobby object with a fake clock, then over real HTTP,
and the NAT traversal pieces (hole-punch relay, STUN responder)."""
import json
import os
import socket
import struct
import sys
import threading
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lobby_server  # noqa: E402

FAILS = []


def check(cond, what):
    print(("PASS " if cond else "FAIL ") + what)
    if not cond:
        FAILS.append(what)


def test_object():
    now = [1000.0]
    lb = lobby_server.Lobby(timeout=30.0, clock=lambda: now[0])
    code, alice = lb.login("  alice  ")
    check(code == 200 and alice["name"] == "alice" and alice["token"], "sign in trims the name and returns a token")
    check(lb.login("Alice")[0] == 409, "a name in use (any case) is refused")
    check(lb.login("")[0] == 400, "an empty name is refused")
    for bad in ("bob, carol", "@mod", "eve (1800)", "x"):
        check(lb.login(bad)[0] == 400, f"a guest named {bad!r} is refused: it would pass for something else")
    _, bob = lb.login("bob")
    check([p["name"] for p in lb.player_list()[1]["players"]] == ["alice", "bob"], "players are listed by name")
    check(lb.presence(bob["token"], "playing")[0] == 200, "presence can change")
    check(lb.presence(bob["token"], "dancing")[0] == 400, "an unknown status is refused")
    check(lb.presence("nope", "away")[0] == 401, "presence needs a signed-in player")
    statuses = {p["name"]: p["status"] for p in lb.player_list()[1]["players"]}
    check(statuses == {"alice": "available", "bob": "playing"}, "the list shows each player's status")

    check(lb.post_chat(alice["token"], "  hello  ")[0] == 200, "a signed-in player can chat")
    check(lb.post_chat("nope", "hi")[0] == 401, "chat needs a signed-in player")
    check(lb.post_chat(alice["token"], "   ")[0] == 400, "an empty line is refused")
    _, c = lb.chat_since(0)
    texts = [(m["from"], m["text"]) for m in c["messages"]]
    check(texts == [("", "alice has joined."), ("", "bob has joined."), ("alice", "hello")],
          "chat keeps join notices and lines in order")
    _, c2 = lb.chat_since(c["last"])
    check(c2["messages"] == [] and c2["last"] == c["last"], "since=last returns nothing new")

    code, room = lb.register({"id": "g1", "host_port": 20600, "name": "Skirmish", "host_name": "alice",
                              "map_name": "Mainland", "players": 1, "max_players": 4, "password": True})
    check(code == 201 and room["password"] == 1 and room["state"] == "init", "a host registers a game")
    code, room = lb.register({"id": "g1", "host_port": 20600, "players": 2, "state": "running"})
    check(code == 200 and room["players"] == 2 and room["state"] == "running", "re-registering updates it")
    check(room["name"] == "Skirmish" and room["password"] == 1 and room["host_ip"] == "127.0.0.1",
          "fields left out keep their value")
    check(room["player_names"] == [] and room["map_type"] == "" and room["start_time"] == 0,
          "the optional details default to empty for an older host")
    names = [{"name": " alice ", "team": 0, "civ": "athen"}, {"name": "Petra", "team": 9, "ai": True, "ai_difficulty": 4},
             {"name": "eve", "observer": 1, "team": "x"}, {"team": 1}, "junk"]
    code, room = lb.register({"id": "g1", "player_names": names, "map_type": "random", "map_size": "Normal",
                              "start_time": 1760000000})
    check(room["player_names"] == [{"name": "alice", "team": 0, "civ": "athen", "ai": 0, "observer": 0},
                                   {"name": "Petra", "team": 7, "civ": "", "ai": 1, "observer": 0,
                                    "ai_difficulty": 4, "ai_behavior": 2},
                                   {"name": "eve", "team": -1, "civ": "", "ai": 0, "observer": 1}],
          "a host's player names are kept, sanitized")
    check(room["map_type"] == "random" and room["map_size"] == "Normal" and room["start_time"] == 1760000000,
          "a host sends the map type, map size and start time")
    check(lb.register({"id": "g1", "players": 3})[1]["player_names"][0]["name"] == "alice",
          "player names left out keep their value")
    check(lb.register({"id": "g1", "player_names": "bad"})[1]["player_names"] == [], "a bad player list is empty")
    check(lb.register({"id": "g3"}, "10.0.0.7")[1]["host_ip"] == "10.0.0.7", "without host_ip, the sender's address")
    check(lb.register({"id": "g2", "state": "bogus"})[1]["state"] == "init", "an unknown state reads as init")
    check(len(lb.room_list()[1]["rooms"]) == 3 and "seen" not in lb.room_list()[1]["rooms"][0],
          "the game list hides bookkeeping")
    check(lb.unregister("g2")[0] == 200 and lb.unregister("g2")[0] == 404, "a host unregisters its game")

    now[0] += 20.0
    lb.presence(alice["token"], "available")  # alice is heard from, bob is not
    now[0] += 15.0
    lb.expire()
    check([p["name"] for p in lb.player_list()[1]["players"]] == ["alice"], "a silent player is dropped")
    check(lb.room_list()[1]["rooms"] == [], "a game not refreshed is dropped")
    check(lb.chat_since(0)[1]["messages"][-1]["text"] == "bob has left.", "the drop is announced")
    check(lb.logout(alice["token"])[0] == 200 and lb.player_list()[1]["players"] == [], "sign out")
    check(lb.login("bob")[0] == 200, "a dropped name is free again")


def test_http():
    server = lobby_server.serve("127.0.0.1", 0, verbose=False)
    port = server.server_address[1]
    lobby_server.LOBBY = lobby_server.Lobby()
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{port}"

    def call(path, data=None):
        req = urllib.request.Request(base + path, data=None if data is None else json.dumps(data).encode(),
                                     headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=5) as r:
                return r.status, json.loads(r.read())
        except urllib.error.HTTPError as e:
            return e.code, json.loads(e.read())

    code, me = call("/login", {"name": "carol"})
    check(code == 200, "HTTP sign in")
    check(call("/chat", {"token": me["token"], "text": "gg"})[0] == 200, "HTTP chat")
    check(call("/chat?since=0")[1]["messages"][-1]["text"] == "gg", "HTTP chat history")
    check(call("/rooms", {"id": "r", "host_port": 20601, "name": "Test"})[0] == 201, "HTTP register")
    check(call("/rooms/")[1]["rooms"][0]["name"] == "Test", "HTTP game list")
    code, conn = call("/rooms/connect", {"token": me["token"], "id": "r"})
    check(conn.get("host_port") == 20601, "HTTP connect")
    check(call("/rooms/ticket", {"id": "r", "ticket": conn.get("ticket")}) == (200, {"name": "carol"}),
          "HTTP ticket redeemed by the game's host")
    check(call("/players")[1]["players"] == [{"name": "carol", "status": "available"}], "HTTP players")
    check(call("/rooms/punch", {"token": me["token"], "id": "r", "ip": "127.0.0.1", "port": 40000})[0] == 200, "HTTP punch")
    check(call("/rooms/punch?id=r&since=0")[1]["endpoints"][0]["port"] == 40000, "HTTP punch list")
    check(call("/register", {"name": "dave", "password": "ABC"})[0] == 200, "HTTP account registration")
    check(call("/login", {"name": "dave", "password": "XYZ"})[0] == 401, "HTTP sign in with the wrong password")
    check(call("/login", {"name": "dave", "password": "ABC"})[0] == 200, "HTTP sign in with the password")
    check(call("/nope")[0] == 404, "unknown path is 404")
    req = urllib.request.Request(base + "/login", data=b"[1]", headers={"Content-Type": "application/json"})
    try:
        urllib.request.urlopen(req, timeout=5)
        bad = 200
    except urllib.error.HTTPError as e:
        bad = e.code
    check(bad == 400, "a body that is not an object is refused")
    server.shutdown()


def test_ratings():
    import tempfile
    adj = lobby_server.get_rating_adjustment
    check(adj(1200, 1200, 0, 0, 1) == 78 and adj(1200, 1200, 0, 0, -1) == -78,
          "elo.py: two new players move 78 points")
    check(adj(1800, 1200, 30, 30, 1) == 0 and adj(1200, 1800, 30, 30, -1) == 0,
          "elo.py: a sure win (600 apart) changes nothing")
    check(adj(1500, 1500, 30, 30, -1) == -14, "elo.py: stable players at 1500 move 14 points")

    path = os.path.join(tempfile.mkdtemp(), "ratings.json")
    lb = lobby_server.Lobby(ratings_path=path)
    _, a = lb.login("alice")
    _, b = lb.login("bob")
    _, c = lb.login("carol")
    rep = {"matchID": "m1", "players": "alice,bob,", "playerStates": "won,defeated,", "civs": "athen,brit,"}
    check(lb.report("nope", rep)[0] == 401, "a report needs a signed-in player")
    check(lb.report(c["token"], rep)[0] == 403, "only a player of the match reports it")
    check(lb.report(a["token"], {"matchID": "m1"})[0] == 400, "a report needs its players and states")
    code, obj = lb.report(a["token"], rep)
    check(code == 202 and not obj["rated"], "the game waits for every player's report")
    check("rating" not in lb.player_list()[1]["players"][0], "no rating before a rated game")
    code, obj = lb.report(b["token"], rep)
    check(code == 200 and obj["rated"] and obj["ratings"] == {"alice": 1265, "bob": 1135},
          "the 1v1 is rated once both have reported, each counting this game played (len(player.games))")
    check(lb.chat[-1]["from"] == "RatingsBot" and lb.chat[-1]["text"] == "A rated game has ended. alice won "
          "against bob. Rating Adjustment: alice (1200 -> 1265) and bob (1200 -> 1135).", "RatingsBot announces it")
    check(lb.report(b["token"], rep)[1] == {"rated": False}, "a repeated report rates nothing again")
    ratings = {p["name"]: p.get("rating") for p in lb.player_list()[1]["players"]}
    check(ratings == {"alice": 1265, "bob": 1135, "carol": None}, "the player list carries ratings")
    check(lb.leaderboard()[1]["players"] == [{"name": "alice", "rating": 1265}, {"name": "bob", "rating": 1135}],
          "the leaderboard lists rated players best first")
    code, prof = lb.profile("BOB")
    check(code == 200 and prof == {"name": "bob", "rating": 1135, "highestRating": 1135, "rank": 2,
                                   "totalGamesPlayed": 1, "wins": 0, "losses": 1}, "a player's profile")
    check(lb.profile("carol")[0] == 404, "a player the lobby has no game of has no profile")
    team = {"matchID": "m2", "players": "alice,bob,carol,", "playerStates": "won,defeated,defeated,"}
    lb.report(a["token"], team)
    lb.report(b["token"], team)
    check(lb.report(c["token"], team)[1] == {"rated": False}, "only 1v1 games are rated")
    check(lb.profile("carol")[1] == {"name": "carol", "totalGamesPlayed": 1, "wins": 0, "losses": 1,
                                     "rating": "-", "rank": "-", "highestRating": "-"},
          "an unrated game counts in the profile, with no rating yet")
    check(lb.profile("alice")[1]["totalGamesPlayed"] == 2 and lb.profile("alice")[1]["wins"] == 2,
          "every finished game counts as played")
    for mid, states, why in (("m3", "defeated,defeated,", "no winner"), ("m4", "active,won,", "one still playing"),
                             ("m5", "won,won,", "two winners")):
        r3 = {"matchID": mid, "players": "alice,bob,", "playerStates": states}
        lb.report(a["token"], r3)
        check(lb.report(b["token"], r3)[1] == {"rated": False}, "a 1v1 with %s is not rated" % why)
    r6 = {"matchID": "m6", "players": "alice,bob,", "playerStates": "won,defeated,"}
    lb.report(a["token"], r6)
    check(lb.report(b["token"], dict(r6, playerStates="defeated,won,"))[0] == 409,
          "a report that differs from the match's first is refused")
    _, d = lb.login("dave")
    r7 = {"matchID": "m7", "players": "carol,dave,", "playerStates": "won,defeated,"}
    lb.report(c["token"], r7)
    lb.report(d["token"], r7)
    check([lb.profile(n)[1]["rank"] for n in ("alice", "carol", "bob", "dave")] == [1, 2, 4, 4],
          "rank counts the players rated at least as high, so bob and dave tie at 4")
    check(lobby_server.Lobby(ratings_path=path).profile("alice")[1]["rating"] == 1265,
          "ratings outlive the server in the --ratings file")


def test_room_owners():
    """0 A.D.'s bot keys a game by its host: two hosts on the default port get a
    game each, and only a game's own host updates or removes it."""
    lb = lobby_server.Lobby(timeout=60.0, clock=lambda: 1000.0)
    alice = lb.login("alice")[1]["token"]
    bob = lb.login("bob")[1]["token"]
    code, a = lb.register({"token": alice, "id": "room-20595", "name": "A", "host_port": 20595}, "198.51.100.1")
    code_b, b = lb.register({"token": bob, "id": "room-20595", "name": "B", "host_port": 20595}, "203.0.113.2")
    check(code == 201 and code_b == 201 and a["id"] == "room-20595" and b["id"] == "room-20595-2",
          "a second host on the same port gets a game of its own")
    lb.register({"token": bob, "id": "room-20595", "players": 2}, "203.0.113.2")
    lb.register({"id": "room-20595", "players": 3}, "198.51.100.1")  # the match scene's own post, no token
    rooms = {r["id"]: r for r in lb.room_list()[1]["rooms"]}
    addr = {rid: lb.connect({"token": bob, "id": rid})[1]["host_ip"] for rid in rooms}
    check(len(rooms) == 2 and addr["room-20595"] == "198.51.100.1" and rooms["room-20595"]["players"] == 3
          and addr["room-20595-2"] == "203.0.113.2" and rooms["room-20595-2"]["players"] == 2,
          "each host's heartbeat with the old id updates only its own game")
    check("owner" not in rooms["room-20595"] and "poster_ip" not in rooms["room-20595"], "the owner stays private")
    mallory = lb.login("mallory")[1]["token"]
    check(lb.unregister("room-20595", mallory, "192.0.2.66")[0] == 403 and len(lb.room_list()[1]["rooms"]) == 2,
          "someone else cannot remove a game")
    lb.add_punch({"token": mallory, "id": "room-20595-2", "ip": "192.0.2.66", "port": 5000})
    check(lb.punches("room-20595", 0, bob, "203.0.113.2")[1]["endpoints"][0]["port"] == 5000 and
          lb.punches("room-20595", 0, alice, "198.51.100.1")[1]["endpoints"] == [],
          "each host reads the punches sent to its own game")
    check(lb.unregister("room-20595", bob, "203.0.113.2")[0] == 200 and
          [r["id"] for r in lb.room_list()[1]["rooms"]] == ["room-20595"], "a host removes its own game")


def test_tickets():
    """0 A.D. lobby authentication: the host learns a joiner's lobby name from
    the lobby (LobbyAuth iq), here by redeeming the ticket /rooms/connect gave."""
    now = [1000.0]
    lb = lobby_server.Lobby(timeout=600.0, clock=lambda: now[0])
    host = lb.login("host")[1]["token"]
    lb.register({"token": host, "id": "room-20595", "host_port": 20595}, "198.51.100.1")
    bob = lb.login("Bob")[1]["token"]
    t1 = lb.connect({"token": bob, "id": "room-20595"})[1]["ticket"]
    t2 = lb.connect({"token": bob, "id": "room-20595"})[1]["ticket"]
    check(t1 != t2, "every ask gets a new ticket")
    check("tickets" not in lb.room_list()[1]["rooms"][0], "tickets stay out of the game list")
    mallory = lb.login("mallory")[1]["token"]
    check(lb.redeem_ticket({"token": mallory, "id": "room-20595", "ticket": t1}, "192.0.2.66")[0] == 404,
          "only the game's host redeems its tickets")
    check(lb.redeem_ticket({"token": host, "id": "room-20595", "ticket": t1}, "198.51.100.1") == (200, {"name": "Bob"}),
          "the host learns the ticket's lobby name")
    check(lb.redeem_ticket({"token": host, "id": "room-20595", "ticket": t1}, "198.51.100.1")[0] == 404,
          "a ticket counts once")
    check(lb.redeem_ticket({"token": host, "id": "room-20595", "ticket": "made-up"}, "198.51.100.1")[0] == 404,
          "a ticket the lobby never gave is unknown")
    lb.register({"token": host, "id": "room-20595", "players": 2}, "198.51.100.1")
    now[0] += lobby_server.TICKET_TIMEOUT + 1
    check(lb.redeem_ticket({"token": host, "id": "room-20595", "ticket": t2}, "198.51.100.1")[0] == 404,
          "a heartbeat keeps tickets, but they expire")
    lb.register({"id": "room-20600", "host_port": 20600}, "203.0.113.5")  # a saved game's own post, no token
    t3 = lb.connect({"token": bob, "id": "room-20600"})[1]["ticket"]
    other = lb.login("other")[1]["token"]
    check(lb.redeem_ticket({"token": other, "id": "room-20600", "ticket": t3}, "203.0.113.5") == (200, {"name": "Bob"}),
          "a game posted without a token is the host's at its address")


def test_player_states():
    """LobbyGamelistReporter: a running game's players carry State and Offline."""
    out = lobby_server._player_names([{"name": "alice", "state": "won"}, {"name": "bob", "state": "defeated", "offline": 1},
                                      {"name": "AI", "ai": 1, "offline": True, "state": "active"}])
    check(out[0].get("state") == "won" and out[1].get("offline") is True and out[1].get("state") == "defeated"
          and "offline" not in out[2] and "state" not in out[2] and "offline" not in out[0],
          "won, defeated and offline are kept, anything else dropped")


def test_moderation():
    """0 A.D. XmppClient kick and ban, MUC roles and the room subject."""
    lb = lobby_server.Lobby(timeout=60.0, clock=lambda: 1000.0, moderators=["Mod"], subject="Welcome")
    mod = lb.login("mod")[1]["token"]
    bob = lb.login("bob")[1]["token"]
    carol = lb.login("carol")[1]["token"]
    code, obj = lb.player_list()
    rows = {r["name"]: r for r in obj["players"]}
    check(rows["mod"].get("moderator") is True and "moderator" not in rows["bob"] and obj["subject"] == "Welcome",
          "moderators are marked and the subject is listed")
    check(lb.kick(bob, "carol")[0] == 403 and carol in lb.players, "a player who is no moderator cannot kick")
    check(lb.set_subject(bob, "x")[0] == 403 and lb.set_subject(mod, " Be nice ")[1]["subject"] == "Be nice",
          "only a moderator sets the subject")
    check(lb.chat[-1]["text"] == "mod changed the lobby subject to:\nBe nice" and lb.chat[-1]["from"] == "",
          "the room hears of the new subject")
    lb.set_subject(mod, "")
    check(lb.chat[-1]["text"] == "mod deleted the lobby subject.", "and of its removal")
    lb.set_subject(mod, "Be nice")
    check(lb.kick(mod, "nobody")[0] == 404, "kicking someone not in the lobby is refused")
    check(lb.kick(mod, "Bob", "spam")[0] == 200 and bob not in lb.players, "a moderator kicks bob")
    check(lb.chat[-1]["text"] == "bob has been kicked from the lobby. Reason: spam", "the lobby is told why")
    check(lb.presence(bob, "available") == (401, {"error": "not signed in",
          "kicked": "You have been kicked from the lobby! Reason: spam"}), "bob's next request says he was kicked")
    check(lb.presence(bob, "available")[1] == {"error": "not signed in"}, "once")
    check(lb.login("bob")[0] == 200, "a kicked player may sign in again")
    check(lb.kick(mod, "carol", "", True)[0] == 200 and lb.post_chat(carol, "hi")[1].get("kicked") ==
          "You have been banned from the lobby!", "a ban removes carol and tells her")
    check(lb.chat[-1]["text"] == "carol has been banned from the lobby.", "the lobby hears of the ban")
    check(lb.login("carol")[0] == 403 and lb.login("CAROL")[0] == 403, "a banned name cannot sign in")


def test_punch():
    now = [1000.0]
    lb = lobby_server.Lobby(timeout=60.0, clock=lambda: now[0])
    lb.register({"id": "g", "host_ip": "198.51.100.4", "host_port": 40001,
                 "local_ip": "192.168.1.10", "local_port": 20595})
    tok = lb.login("dave")[1]["token"]
    check(lb.add_punch({"id": "g", "ip": "203.0.113.9", "port": 51000})[0] == 401, "a punch needs a signed-in player")
    room = lb.room_list()[1]["rooms"][0]
    check("local_ip" not in room and "punch" not in room, "the host's LAN address stays out of the game list")
    check("host_ip" not in room and "host_port" not in room, "so does its public address")
    check(lb.connect({"id": "g"})[0] == 401, "only a signed-in player is told a game's address")
    check(lb.connect({"token": tok, "id": "nope"})[0] == 404, "an unknown game has no address")
    code, obj = lb.connect({"token": tok, "id": "g"})
    check(code == 200 and (obj["host_ip"], obj["host_port"]) == ("198.51.100.4", 40001) and len(obj["ticket"]) == 32,
          "a signed-in joiner is told the game's address, with a ticket")
    check(lb.add_punch({"token": tok, "id": "nope", "ip": "203.0.113.9", "port": 5})[0] == 404, "a punch needs a game")
    check(lb.add_punch({"token": tok, "id": "g", "ip": "", "port": 5})[0] == 400, "a punch needs an endpoint")
    check(lb.add_punch({"token": tok, "id": "g", "ip": "203.0.113.9", "port": 51000}) == (200, {}),
          "a joiner on another network gets no LAN address")
    code, obj = lb.punches("g", 0)
    check(code == 200 and obj["endpoints"] == [{"seq": 1, "ip": "203.0.113.9", "port": 51000}] and obj["last"] == 1,
          "the host reads the joiner's endpoint")
    check(lb.punches("g", 1)[1]["endpoints"] == [], "since= skips endpoints already punched")
    check(lb.add_punch({"token": tok, "id": "g", "ip": "198.51.100.4", "port": 52000}) ==
          (200, {"local_ip": "192.168.1.10", "local_port": 20595}),
          "a joiner behind the host's public address is given the LAN address")
    lb.register({"id": "g", "players": 2})
    check(lb.punches("g", 0)[1]["last"] == 2, "a heartbeat keeps the endpoints")
    now[0] += lobby_server.PUNCH_TIMEOUT + 1
    lb.register({"id": "g"})
    lb.presence(tok, "available")
    check(lb.punches("g", 0)[1]["endpoints"] == [], "endpoints expire")
    for i in range(lobby_server.PUNCH_KEEP + 4):
        lb.add_punch({"token": tok, "id": "g", "ip": "203.0.113.9", "port": 1000 + i})
    check(len(lb.punches("g", 0)[1]["endpoints"]) == lobby_server.PUNCH_KEEP, "at most PUNCH_KEEP endpoints per game")

    sock = lobby_server.serve_stun("127.0.0.1", 0)
    cli = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    cli.bind(("127.0.0.1", 0))
    cli.settimeout(2)
    txn = bytes(range(12))
    cli.sendto(struct.pack("!HHI", 1, 0, lobby_server.STUN_MAGIC_COOKIE) + txn, sock.getsockname())
    resp, _ = cli.recvfrom(512)
    kind, length, cookie = struct.unpack("!HHI", resp[:8])
    atype, alen, _, fam, xport, xip = struct.unpack("!HHBBHI", resp[20:32])
    ip = socket.inet_ntoa(struct.pack("!I", xip ^ lobby_server.STUN_MAGIC_COOKIE))
    check(kind == 0x0101 and resp[8:20] == txn and atype == 0x0020 and fam == 1 and
          (ip, xport ^ (lobby_server.STUN_MAGIC_COOKIE >> 16)) == cli.getsockname(),
          "the STUN responder names the request's own address and port")
    check(lobby_server.stun_answer(b"RTS0 not stun", ("127.0.0.1", 1)) is None, "non-STUN datagrams get no answer")
    cli.close()
    sock.close()


def test_accounts():
    import tempfile
    path = os.path.join(tempfile.mkdtemp(), "accounts.json")
    lb = lobby_server.Lobby(accounts_path=path)
    pw = lobby_server.encrypt_password("hunter22", "Alice")
    check(len(pw) == 64 and pw == pw.upper() and pw == lobby_server.encrypt_password("hunter22", "alice"),
          "EncryptPassword is 64 upper-case hex digits, salted by the name in any case")
    check(pw != lobby_server.encrypt_password("hunter22", "bob"), "another name salts the same password differently")
    check(lb.register_account("Alice", pw)[0] == 200, "register an account")
    check(lb.register_account("alice", pw)[0] == 409, "the name, in any case, can be registered once")
    check(lb.register_account("bad name!", pw)[0] == 400, "a name outside a-z 0-9 . _ - is refused")
    check(lb.register_account("x" * 21, pw)[0] == 400, "a name over 20 characters is refused")
    check(lb.register_account("ab", pw)[0] == 400 and lb.register_account("123", pw)[0] == 400,
          "as credentials.js: at least 3 characters, one of them a letter")
    check("hunter22" not in open(path).read() and pw not in open(path).read(),
          "the accounts file keeps neither the password nor what the client sent")
    check(lb.login("alice", "wrong")[0] == 401, "the wrong password is refused")
    check(lb.login("alice")[0] == 401, "a registered name needs its password")
    code, obj = lb.login("alice", pw)
    check(code == 200 and obj["name"] == "Alice", "the right password signs in, under the registered spelling")
    check(lb.login("bob", pw)[0] == 401, "a password for a name with no account is refused")
    check(lb.login("guest")[0] == 200, "a name with no account signs in as a guest")
    strict = lobby_server.Lobby(accounts_path=path, accounts_only=True)
    check(strict.login("carol")[0] == 401, "with --accounts-only a name with no account is refused")
    check(strict.login("ALICE", pw)[0] == 200, "accounts load from the file")


if __name__ == "__main__":
    test_accounts()
    test_object()
    test_room_owners()
    test_tickets()
    test_moderation()
    test_player_states()
    test_punch()
    test_ratings()
    test_http()
    print(f"{'FAILED ' + str(len(FAILS)) if FAILS else 'all lobby checks passed'}")
    sys.exit(1 if FAILS else 0)
