#!/usr/bin/env python3
"""Checks tools/lobby_server.py: sign-in, presence, chat, the game list and
timeouts, first on the Lobby object with a fake clock, then over real HTTP."""
import json
import os
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
    code, room = lb.register({"id": "g1", "host_port": 20600, "players": 2, "state": "running"}, "10.0.0.5")
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
    check(call("/players")[1]["players"] == [{"name": "carol", "status": "available"}], "HTTP players")
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
    check(code == 200 and obj["rated"] and obj["ratings"] == {"alice": 1278, "bob": 1122},
          "the 1v1 is rated once both have reported")
    check(lb.report(b["token"], rep)[1] == {"rated": False}, "a repeated report rates nothing again")
    ratings = {p["name"]: p.get("rating") for p in lb.player_list()[1]["players"]}
    check(ratings == {"alice": 1278, "bob": 1122, "carol": None}, "the player list carries ratings")
    check(lb.leaderboard()[1]["players"] == [{"name": "alice", "rating": 1278}, {"name": "bob", "rating": 1122}],
          "the leaderboard lists rated players best first")
    code, prof = lb.profile("BOB")
    check(code == 200 and prof == {"name": "bob", "rating": 1122, "highestRating": 1200, "rank": 2,
                                   "totalGamesPlayed": 1, "wins": 0, "losses": 1}, "a player's profile")
    check(lb.profile("carol")[0] == 404, "a player with no rated game has no profile")
    team = {"matchID": "m2", "players": "alice,bob,carol,", "playerStates": "won,defeated,defeated,"}
    lb.report(a["token"], team)
    lb.report(b["token"], team)
    check(lb.report(c["token"], team)[1] == {"rated": False}, "only 1v1 games are rated")
    check(lobby_server.Lobby(ratings_path=path).profile("alice")[1]["rating"] == 1278,
          "ratings outlive the server in the --ratings file")


if __name__ == "__main__":
    test_object()
    test_ratings()
    test_http()
    print(f"{'FAILED ' + str(len(FAILS)) if FAILS else 'all lobby checks passed'}")
    sys.exit(1 if FAILS else 0)
