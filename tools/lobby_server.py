#!/usr/bin/env python3
"""In-memory HTTP lobby for RTS0, after 0 A.D.'s multiplayer lobby (gui/lobby,
source/lobby/XmppClient.cpp). Not a game server: games are hosted by players and
joined directly over the game's own UDP protocol.

0 A.D. runs its lobby over XMPP: players sign in, see who is online and whether
they are playing, chat in the lobby room, and see the list of games that hosts
register (name, host, map, players, state). This server keeps the same data
behind a small JSON API:

  POST /login     {name}                 -> {token, name}; 409 if the name is in use
  POST /logout    {token}
  POST /presence  {token, status}        status: available, away or playing
  GET  /players                          -> {players: [{name, status}]}
  GET  /rooms                            -> {rooms: [...]}
  POST /rooms     {id, host_ip, host_port, players, max_players, map_id,
                   name, host_name, map_name, victory, state, password,
                   map_type, map_size, start_time, player_names}
                  registers a game or updates the fields sent (0 A.D. registerGame /
                  changeStateGame); without host_ip, the request's address is used.
                  map_type, map_size, start_time and player_names are optional:
                  the details 0 A.D.'s lobby shows for a selected game (GameDetails.js).
                  player_names is a list of {name, team, civ, ai, observer}, an AI
                  also with ai_difficulty and ai_behavior
  POST /rooms/unregister {id}            removes it (unregisterGame)
  GET  /chat?since=N                     -> {messages: [{id, time, from, text}], last}
  POST /chat      {token, text}
  POST /report    {token, matchID, players, playerStates, ...}
                  a player's end-of-game report (gui/session/lobby/LobbyRatingReporter.js);
                  once every player of a 1v1 has reported, both ratings change
                  (the lobby bot's EcheLOn and elo.py)
  GET  /leaderboard                      -> {players: [{name, rating}]}, best first (boardlist)
  GET  /profile?name=N                   -> {name, rating, highestRating, rank, totalGamesPlayed,
                                             wins, losses}; 404 if N has no rated game (profile)

Players with a rated game carry their "rating" in GET /players. Ratings live as
long as the server, or in the JSON file given with --ratings.

A signed-in player or a game that has not been heard from for TIMEOUT seconds
is dropped, as the XMPP server drops a client that went away.
"""
import argparse
import json
import os
import secrets
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

TIMEOUT = 30.0          # seconds without a request before a player or game is dropped
CHAT_KEEP = 200         # chat lines kept for late arrivals
MAX_NAME = 32
MAX_TEXT = 500
STATUSES = ("available", "away", "playing")
ROOM_STATES = ("init", "running")


# lobby-bots xpartamupp/elo.py and leaderboard.py.
LEADERBOARD_DEFAULT_RATING = 1200
ELO_SURE_WIN_DIFFERENCE = 600.0     # a "sure win": nothing gained or lost
ELO_K_FACTOR_CONSTANT_RATING = 2200.0  # ratings above this move at its rate
VOLATILITY_CONSTANT = 20.0          # games after which a rating is stable
ANTI_INFLATION = 0.015
LEADERBOARD_SIZE = 100
REPORTS_KEEP = 200                  # unfinished reports kept, oldest dropped first


def get_rating_adjustment(rating, opponent_rating, games_played, opponent_games_played, result):
    """elo.py get_rating_adjustment: result 1 a win, -1 a loss, 0 a draw."""
    player_volatility = (min(games_played, VOLATILITY_CONSTANT) / VOLATILITY_CONSTANT + 0.25) / 1.25
    rating_k_factor = 50.0 * (min(rating, ELO_K_FACTOR_CONSTANT_RATING) / ELO_K_FACTOR_CONSTANT_RATING + 1.0) / 2.0
    volatility = rating_k_factor * player_volatility
    rating_difference = opponent_rating - rating
    rating_adjustment = (rating_difference + result * ELO_SURE_WIN_DIFFERENCE) / volatility - ANTI_INFLATION
    if result == 1:
        return round(max(0.0, rating_adjustment))
    if result == -1:
        return round(min(0.0, rating_adjustment))
    return round(rating_adjustment)


MAX_ROOM_PLAYERS = 24


def _player_names(value):
    """Sanitizes a room's optional player list: {name, team, civ, ai,
    ai_difficulty, ai_behavior, observer} per entry, team -1 for none. Anything that isn't a list of
    objects with a name is dropped, so an older or broken host just shows no
    players."""
    out = []
    for p in value if isinstance(value, list) else []:
        if not isinstance(p, dict) or not str(p.get("name") or "").strip():
            continue
        def num(key, default, lo, hi):
            try:
                return max(lo, min(int(p.get(key, default)), hi))
            except (TypeError, ValueError):
                return default
        entry = {"name": str(p["name"]).strip()[:MAX_NAME], "team": num("team", -1, -1, 7),
                 "civ": str(p.get("civ") or "")[:32], "ai": 1 if p.get("ai") else 0,
                 "observer": 1 if p.get("observer") else 0}
        if entry["ai"]:
            entry["ai_difficulty"] = num("ai_difficulty", 3, 0, 5)
            entry["ai_behavior"] = num("ai_behavior", 2, 0, 3)
        out.append(entry)
        if len(out) >= MAX_ROOM_PLAYERS:
            break
    return out


def _split_list(value):
    """LobbyRatingReport's lists: "a,b," -> ["a", "b"]."""
    if isinstance(value, list):
        return [str(v) for v in value]
    return [v for v in str(value or "").split(",") if v != ""]


class Lobby:
    def __init__(self, timeout=TIMEOUT, clock=time.monotonic, ratings_path=None):
        self.timeout = timeout
        self.clock = clock
        self.lock = threading.Lock()
        self.players = {}   # token -> {name, status, seen}
        self.rooms = []     # dicts, plus "seen"
        self.chat = []      # {id, time, from, text}
        self.next_chat = 1
        # name (lower case) -> {name, rating, highestRating, wins, losses}
        self.ratings = {}
        self.reports = {}   # matchID -> {players, playerStates, reporters}
        self.ratings_path = ratings_path
        if ratings_path:
            try:
                with open(ratings_path, encoding="utf-8") as f:
                    self.ratings = json.load(f)
            except (OSError, ValueError):
                self.ratings = {}

    def expire(self):
        now = self.clock()
        gone = [t for t, p in self.players.items() if now - p["seen"] > self.timeout]
        for t in gone:
            self._say(None, f"{self.players[t]['name']} has left.")
            del self.players[t]
        self.rooms = [r for r in self.rooms if now - r["seen"] <= self.timeout]

    def _say(self, sender, text):
        self.chat.append({"id": self.next_chat, "time": time.time(), "from": sender or "", "text": text})
        self.next_chat += 1
        del self.chat[:-CHAT_KEEP]

    def player(self, token):
        p = self.players.get(token or "")
        if p:
            p["seen"] = self.clock()
        return p

    def login(self, name):
        name = " ".join(str(name or "").split())[:MAX_NAME]
        if not name:
            return 400, {"error": "a name is required"}
        if any(p["name"].lower() == name.lower() for p in self.players.values()):
            return 409, {"error": "that name is in use"}
        token = secrets.token_hex(16)
        self.players[token] = {"name": name, "status": "available", "seen": self.clock()}
        self._say(None, f"{name} has joined.")
        return 200, {"token": token, "name": name}

    def logout(self, token):
        p = self.players.pop(token or "", None)
        if not p:
            return 404, {"error": "not signed in"}
        self._say(None, f"{p['name']} has left.")
        return 200, {}

    def presence(self, token, status):
        p = self.player(token)
        if not p:
            return 401, {"error": "not signed in"}
        if status not in STATUSES:
            return 400, {"error": "status must be one of " + ", ".join(STATUSES)}
        p["status"] = status
        return 200, {}

    def player_list(self):
        rows = []
        for p in self.players.values():
            row = {"name": p["name"], "status": p["status"]}
            r = self.ratings.get(p["name"].lower())
            if r:
                row["rating"] = r["rating"]
            rows.append(row)
        return 200, {"players": sorted(rows, key=lambda r: r["name"].lower())}

    def _save_ratings(self):
        if not self.ratings_path:
            return
        tmp = self.ratings_path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(self.ratings, f)
        os.replace(tmp, self.ratings_path)

    def report(self, token, data):
        """EcheLOn's game reports: each player of the match sends one; when every
        player named in it has, a 1v1 with a winner and a loser is rated."""
        p = self.player(token)
        if not p:
            return 401, {"error": "not signed in"}
        match_id = str(data.get("matchID") or "")[:64]
        names = [n[:MAX_NAME] for n in _split_list(data.get("players"))]
        states = _split_list(data.get("playerStates"))
        if not match_id or len(names) < 2 or len(states) != len(names):
            return 400, {"error": "a report needs matchID, players and playerStates"}
        if p["name"].lower() not in [n.lower() for n in names]:
            return 403, {"error": "only a player of the match reports it"}
        rep = self.reports.get(match_id)
        if rep is None:
            rep = {"players": names, "playerStates": states, "reporters": set()}
            self.reports[match_id] = rep
            while len(self.reports) > REPORTS_KEEP:
                del self.reports[next(iter(self.reports))]
        elif [n.lower() for n in rep["players"]] != [n.lower() for n in names]:
            return 409, {"error": "the players differ from the match's other reports"}
        rep["reporters"].add(p["name"].lower())
        if rep.get("done"):
            return 200, {"rated": False}
        if len(rep["reporters"]) < len({n.lower() for n in rep["players"]}):
            return 202, {"rated": False, "waiting": True}
        rep["done"] = True
        return 200, self._rate(rep)

    def _rate(self, rep):
        """leaderboard.py rate_game: only 1v1 games change ratings."""
        names, states = rep["players"], rep["playerStates"]
        if len(names) != 2 or names[0].lower() == names[1].lower():
            return {"rated": False}
        result = {"won": 1, "defeated": -1}.get(states[0], 0)
        if result == 0 and states[1] not in ("won", "defeated"):
            return {"rated": False}  # nobody finished: nothing to rate
        rows = []
        for n in names:
            r = self.ratings.setdefault(n.lower(), {"name": n, "rating": LEADERBOARD_DEFAULT_RATING,
                                                    "highestRating": LEADERBOARD_DEFAULT_RATING,
                                                    "wins": 0, "losses": 0})
            r["name"] = n
            rows.append(r)
        games = [r["wins"] + r["losses"] for r in rows]
        change0 = get_rating_adjustment(rows[0]["rating"], rows[1]["rating"], games[0], games[1], result)
        change1 = get_rating_adjustment(rows[1]["rating"], rows[0]["rating"], games[1], games[0], -result)
        for r, change, res in ((rows[0], change0, result), (rows[1], change1, -result)):
            r["rating"] += change
            r["highestRating"] = max(r["highestRating"], r["rating"])
            if res == 1:
                r["wins"] += 1
            elif res == -1:
                r["losses"] += 1
        self._save_ratings()
        return {"rated": True, "ratings": {r["name"]: r["rating"] for r in rows}}

    def _ranked(self):
        rated = [r for r in self.ratings.values() if r["wins"] + r["losses"] > 0]
        return sorted(rated, key=lambda r: (-r["rating"], r["name"].lower()))

    def leaderboard(self):
        board = [{"name": r["name"], "rating": r["rating"]} for r in self._ranked()[:LEADERBOARD_SIZE]]
        return 200, {"players": board}

    def profile(self, name):
        name = str(name or "").strip().lower()
        ranked = self._ranked()
        for rank, r in enumerate(ranked, 1):
            if r["name"].lower() == name:
                return 200, {"name": r["name"], "rating": r["rating"], "highestRating": r["highestRating"],
                             "rank": rank, "totalGamesPlayed": r["wins"] + r["losses"],
                             "wins": r["wins"], "losses": r["losses"]}
        return 404, {"error": "player not found"}

    def room_list(self):
        return 200, {"rooms": [{k: v for k, v in r.items() if k != "seen"} for r in self.rooms]}

    def register(self, data, peer_ip="127.0.0.1"):
        """Registers a game, or updates the fields a host sends again (a field
        left out keeps its value). Without host_ip the game is reached at the
        address the request came from, as 0 A.D.'s lobby bot records it."""
        rid = str(data.get("id") or f"room-{int(data.get('host_port') or 20595)}")[:64]
        old = next((r for r in self.rooms if r["id"] == rid), {})

        def field(key, default, kind):
            return kind(data[key]) if data.get(key) not in (None, "") else old.get(key, default)

        room = {
            "id": rid,
            "host_ip": field("host_ip", peer_ip, str)[:64],
            "host_port": field("host_port", 20595, int),
            "players": field("players", 1, int),
            "max_players": field("max_players", 8, int),
            "map_id": field("map_id", "default", str)[:64],
            "name": field("name", "", str)[:64],
            "host_name": field("host_name", "", str)[:MAX_NAME],
            "map_name": field("map_name", "", str)[:64],
            "victory": field("victory", "", str)[:32],
            "state": field("state", "init", str),
            "password": (1 if data["password"] else 0) if "password" in data else old.get("password", 0),
            "map_type": field("map_type", "", str)[:32],
            "map_size": field("map_size", "", str)[:32],
            "start_time": field("start_time", 0, int),
            "player_names": _player_names(data["player_names"]) if "player_names" in data
            else old.get("player_names", []),
            "seen": self.clock(),
        }
        if room["state"] not in ROOM_STATES:
            room["state"] = "init"
        public = {k: v for k, v in room.items() if k != "seen"}
        if old:
            self.rooms[self.rooms.index(old)] = room
            return 200, public
        self.rooms.append(room)
        return 201, public

    def unregister(self, room_id):
        before = len(self.rooms)
        self.rooms = [r for r in self.rooms if r["id"] != str(room_id)]
        return (200, {}) if len(self.rooms) < before else (404, {"error": "no such game"})

    def chat_since(self, since):
        msgs = [m for m in self.chat if m["id"] > since]
        return 200, {"messages": msgs, "last": self.next_chat - 1}

    def post_chat(self, token, text):
        p = self.player(token)
        if not p:
            return 401, {"error": "not signed in"}
        text = str(text or "").strip()[:MAX_TEXT]
        if not text:
            return 400, {"error": "empty message"}
        self._say(p["name"], text)
        return 200, {"id": self.next_chat - 1}


LOBBY = Lobby()


class LobbyHandler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        if self.server.verbose:
            print("[lobby]", fmt % args)

    def _json(self, code, obj):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_GET(self):
        url = urlparse(self.path)
        path = url.path.rstrip("/")
        with LOBBY.lock:
            LOBBY.expire()
            if path == "/rooms":
                code, obj = LOBBY.room_list()
            elif path == "/players":
                code, obj = LOBBY.player_list()
            elif path == "/leaderboard":
                code, obj = LOBBY.leaderboard()
            elif path == "/profile":
                code, obj = LOBBY.profile(parse_qs(url.query).get("name", [""])[0])
            elif path == "/chat":
                try:
                    since = int(parse_qs(url.query).get("since", ["0"])[0])
                except ValueError:
                    since = 0
                code, obj = LOBBY.chat_since(since)
            else:
                code, obj = 404, {"error": "not found"}
        self._json(code, obj)

    def do_POST(self):
        path = urlparse(self.path).path.rstrip("/")
        length = int(self.headers.get("Content-Length", "0") or "0")
        raw = self.rfile.read(min(length, 65536)) if length else b"{}"
        try:
            data = json.loads(raw.decode("utf-8") or "{}")
            if not isinstance(data, dict):
                raise ValueError
        except ValueError:
            self._json(400, {"error": "invalid json"})
            return
        with LOBBY.lock:
            LOBBY.expire()
            try:
                if path == "/rooms":
                    code, obj = LOBBY.register(data, self.client_address[0])
                elif path == "/rooms/unregister":
                    code, obj = LOBBY.unregister(data.get("id"))
                elif path == "/login":
                    code, obj = LOBBY.login(data.get("name"))
                elif path == "/logout":
                    code, obj = LOBBY.logout(data.get("token"))
                elif path == "/presence":
                    code, obj = LOBBY.presence(data.get("token"), data.get("status"))
                elif path == "/chat":
                    code, obj = LOBBY.post_chat(data.get("token"), data.get("text"))
                elif path == "/report":
                    code, obj = LOBBY.report(data.get("token"), data)
                else:
                    code, obj = 404, {"error": "not found"}
            except (TypeError, ValueError):
                code, obj = 400, {"error": "bad field"}
        self._json(code, obj)


def serve(bind, port, verbose=True):
    server = ThreadingHTTPServer((bind, port), LobbyHandler)
    server.verbose = verbose
    return server


def main():
    parser = argparse.ArgumentParser(description="RTS0 multiplayer lobby (players, chat, game list)")
    parser.add_argument("--port", type=int, default=8088)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--timeout", type=float, default=TIMEOUT,
                        help="seconds without a request before a player or game is dropped")
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--ratings", help="JSON file that keeps the ratings between runs")
    args = parser.parse_args()
    global LOBBY
    LOBBY = Lobby(timeout=args.timeout, ratings_path=args.ratings)
    serve(args.bind, args.port, not args.quiet).serve_forever()


if __name__ == "__main__":
    main()
