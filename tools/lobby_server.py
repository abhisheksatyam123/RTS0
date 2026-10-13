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
                   name, host_name, map_name, victory, state, password}
                  registers a game or updates the fields sent (0 A.D. registerGame /
                  changeStateGame); without host_ip, the request's address is used
  POST /rooms/unregister {id}            removes it (unregisterGame)
  GET  /chat?since=N                     -> {messages: [{id, time, from, text}], last}
  POST /chat      {token, text}

A signed-in player or a game that has not been heard from for TIMEOUT seconds
is dropped, as the XMPP server drops a client that went away.
"""
import argparse
import json
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


class Lobby:
    def __init__(self, timeout=TIMEOUT, clock=time.monotonic):
        self.timeout = timeout
        self.clock = clock
        self.lock = threading.Lock()
        self.players = {}   # token -> {name, status, seen}
        self.rooms = []     # dicts, plus "seen"
        self.chat = []      # {id, time, from, text}
        self.next_chat = 1

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
        rows = [{"name": p["name"], "status": p["status"]} for p in self.players.values()]
        return 200, {"players": sorted(rows, key=lambda r: r["name"].lower())}

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
    args = parser.parse_args()
    LOBBY.timeout = args.timeout
    serve(args.bind, args.port, not args.quiet).serve_forever()


if __name__ == "__main__":
    main()
