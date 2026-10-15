#!/usr/bin/env python3
"""In-memory HTTP lobby for RTS0, after 0 A.D.'s multiplayer lobby (gui/lobby,
source/lobby/XmppClient.cpp). Not a game server: games are hosted by players and
joined directly over the game's own UDP protocol.

0 A.D. runs its lobby over XMPP: players sign in, see who is online and whether
they are playing, chat in the lobby room, and see the list of games that hosts
register (name, host, map, players, state). This server keeps the same data
behind a small JSON API:

  POST /register  {name, password}       -> {name}; creates an account (0 A.D. prelobby
                                         Register). 409 if the name is registered, 400 for a
                                         name that is not 1-20 of a-z 0-9 . _ -
  POST /login     {name, password}       -> {token, name}; 409 if the name is in use, 401 for
                                         a registered name with the wrong password (or none)
                                         or, with --accounts-only, a name with no account.
                                         Without --accounts-only an unregistered name signs
                                         in with no password, as a guest
  POST /logout    {token}
  POST /presence  {token, status}        status: available, away or playing
  GET  /players                          -> {players: [{name, status, moderator}], subject}
                                         subject is the room subject (0 A.D. Subject.js)
  POST /kick      {token, nick, reason, ban}
                                         a moderator (--moderators) kicks nick from the lobby, or
                                         with ban also keeps the name out (0 A.D. XmppClient kick and
                                         ban). The lobby chat says "<nick> has been kicked from the
                                         lobby." (KickStrings.js); the kicked player's next request
                                         answers 401 with "kicked": that line for them, and a banned
                                         name's login 403. 403 for a player who is no moderator
  POST /subject   {token, subject}       a moderator sets the room subject
  GET  /rooms                            -> {rooms: [...]}, without host_ip and host_port: as in
                                         0 A.D.'s game list, nobody learns a host's address from it
  POST /rooms/connect {token, id}        -> {host_ip, host_port, ticket}: the address a signed-in
                                         joiner connects to (0 A.D. sends a joiner the connection
                                         data on request, XmppClient SendIqGetConnectionData); one
                                         behind the host's public address learns the LAN one
                                         from /rooms/punch. ticket goes to the host with the
                                         joiner's authentication
  POST /rooms/ticket {token, id, ticket} -> {name}: the host redeems a joiner's ticket (once, within
                                         60 s) for the joiner's lobby name, as 0 A.D.'s host learns
                                         it from the lobby (LobbyAuth iq, NetServer OnLobbyAuth);
                                         404 for a ticket the lobby never gave for that game
  POST /rooms     {id, host_ip, host_port, players, max_players, map_id,
                   name, host_name, map_name, victory, state, password,
                   map_type, map_size, start_time, player_names}
                  registers a game or updates the fields sent (0 A.D. registerGame /
                  changeStateGame); without host_ip, the request's address is used.
                  map_type, map_size, start_time and player_names are optional:
                  the details 0 A.D.'s lobby shows for a selected game (GameDetails.js).
                  player_names is a list of {name, team, civ, ai, observer}, an AI
                  also with ai_difficulty and ai_behavior, and in a running game
                  state (won or defeated) and offline
  POST /rooms/unregister {token, id}     removes it (unregisterGame)
                  A game belongs to the account that registered it (its token), or
                  without one to the address it came from, as 0 A.D.'s bot keys a
                  game by the host's JID: only that host updates or removes it.
                  Another host sending the same id (two on the default port) gets a
                  game of its own, listed as "<id>-2" and so on; its later posts
                  with the old id still reach its own game.
  POST /rooms/punch {token, id, ip, port} a signed-in joiner's public game endpoint, from STUN (0 A.D.
                                         SendStunEndpointToHost). Answers {local_ip, local_port},
                                         the host's LAN address, when ip is the host's public
                                         address (one network: 0 A.D. then joins at the LAN address)
  GET  /rooms/punch?id=R&since=N[&token=T] -> {endpoints: [{seq, ip, port}], last}: the host punches
                                            a hole towards each (0 A.D. SendHolePunchingMessage)
  GET  /chat?since=N                     -> {messages: [{id, time, from, text}], last}
  POST /chat      {token, text}
  POST /report    {token, matchID, players, playerStates, ...}
                  a player's end-of-game report (gui/session/lobby/LobbyRatingReporter.js);
                  once every player has sent the same report, a finished game is
                  recorded, and a 1v1 with one winner changes both ratings (the
                  lobby bot's EcheLOn and elo.py; RatingsBot announces it in chat)
  GET  /leaderboard                      -> {players: [{name, rating}]}, best first (boardlist)
  GET  /profile?name=N                   -> {name, rating, highestRating, rank, totalGamesPlayed,
                                             wins, losses}; "-" for the rating fields until
                                             N's first rated game, 404 if the lobby has no
                                             game of N's (profile)

A host also sends local_ip and local_port with its game; they are kept out of
GET /rooms. With --stun-port the server also answers STUN Binding Requests
(RFC 5389) on UDP, as 0 A.D.'s lobby.stun.server does, so a player learns the
public address and port its NAT gives the game's socket.

A password is what the client sends, 0 A.D.'s EncryptPassword(password,
username): the server never sees the typed one, and keeps only a salted SHA-256
of it. Accounts live as long as the server, or in the JSON file given with
--accounts.

Players with a rated game carry their "rating" in GET /players. Ratings live as
long as the server, or in the JSON file given with --ratings.

A signed-in player or a game that has not been heard from for TIMEOUT seconds
is dropped, as the XMPP server drops a client that went away.
"""
import argparse
import hashlib
import json
import re
import os
import secrets
import socket
import struct
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
PUNCH_KEEP = 16         # joiner endpoints kept per game
PUNCH_TIMEOUT = 30.0    # seconds a joiner endpoint stays listed
PRIVATE_ROOM_KEYS = ("seen", "punch", "next_punch", "local_ip", "local_port", "base", "owner", "poster_ip",
                     "tickets")
TICKET_TIMEOUT = 60.0   # seconds a joiner's lobby ticket stays redeemable
LISTED_HIDDEN_KEYS = PRIVATE_ROOM_KEYS + ("host_ip", "host_port")
STUN_MAGIC_COOKIE = 0x2112A442


# lobby-bots xpartamupp/elo.py and leaderboard.py.
LEADERBOARD_DEFAULT_RATING = 1200
ELO_SURE_WIN_DIFFERENCE = 600.0     # a "sure win": nothing gained or lost
# 0 A.D. source/lobby/pkcs5_pbkdf2.cpp EncryptPassword: what a client sends as
# its password. PBKDF2-HMAC-SHA256, 1337 rounds, salted with SHA-256 of a fixed
# base and the (lower-case) name, as upper-case hex. LobbySession.encrypt_password
# computes the same in the game; tests check the two agree.
ENCRYPT_SALT_BASE = bytes([244, 243, 249, 244, 32, 33, 34, 35, 10, 11, 12, 13, 14, 15, 16, 17,
                           18, 19, 20, 32, 33, 244, 224, 127, 129, 130, 140, 153, 133, 123, 234, 123])
ENCRYPT_ITERATIONS = 1337


def encrypt_password(password, username):
    salt = hashlib.sha256(ENCRYPT_SALT_BASE + username.lower().encode("utf-8")).digest()
    return hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt, ENCRYPT_ITERATIONS).hex().upper()


# 0 A.D. prelobby checkUsername (credentials.js): 3 to 20 of a-z 0-9 . _ -, one
# letter at least, any case. Guests' names follow it too: a space, comma, "@"
# or " (1800)" would pass for something else (a buddy list entry, a moderator,
# a rating).
ACCOUNT_NAME = re.compile(r"^(?=.*[a-z])[a-z0-9._-]{3,20}$", re.IGNORECASE)
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


MAX_ROOM_MODS = 32


def _mods(value):
    """Sanitizes a room's mod list (LobbyGameRegistration.js mods, the host's
    Engine.GetEngineInfo().mods): {name, version, ignoreInCompatibilityChecks}
    per entry, in load order. Anything else becomes an empty list."""
    out = []
    for m in value if isinstance(value, list) else []:
        if isinstance(m, dict) and str(m.get("name") or "").strip():
            out.append({"name": str(m["name"]).strip()[:64], "version": str(m.get("version") or "")[:32],
                        "ignoreInCompatibilityChecks": bool(m.get("ignoreInCompatibilityChecks"))})
    return out[:MAX_ROOM_MODS]


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
        # LobbyGamelistReporter: State (won, defeated) and Offline in a running game.
        if p.get("state") in ("won", "defeated"):
            entry["state"] = p["state"]
        if p.get("offline") and not entry["ai"]:
            entry["offline"] = True
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
    def __init__(self, timeout=TIMEOUT, clock=time.monotonic, ratings_path=None,
                 accounts_path=None, accounts_only=False, moderators=(), subject=""):
        self.timeout = timeout
        # 0 A.D. MUC roles: these names (lower case) are moderators.
        self.moderators = {str(m).strip().lower() for m in moderators if str(m).strip()}
        self.banned = set()  # names (lower case) kept out of the lobby
        self.kicked = {}     # token -> the line its player gets on its next request
        self.subject = str(subject or "")[:MAX_TEXT]
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
        # name (lower case) -> {name, salt, hash}: hash = sha256(salt + password)
        self.accounts = {}
        self.accounts_path = accounts_path
        self.accounts_only = accounts_only
        if accounts_path:
            try:
                with open(accounts_path, encoding="utf-8") as f:
                    self.accounts = json.load(f)
            except (OSError, ValueError):
                self.accounts = {}

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

    def not_signed_in(self, token):
        """401 for a token with no player, saying why when a moderator kicked it."""
        why = self.kicked.pop(token or "", None)
        return 401, ({"error": "not signed in", "kicked": why} if why else {"error": "not signed in"})

    def kick(self, token, nick, reason="", ban=False):
        """XmppClient kick / ban: a moderator removes nick from the lobby."""
        p = self.player(token)
        if not p:
            return self.not_signed_in(token)
        if p["name"].lower() not in self.moderators:
            return 403, {"error": "The command is restricted to moderators."}
        nick = " ".join(str(nick or "").split())[:MAX_NAME]
        reason = " ".join(str(reason or "").split())[:MAX_TEXT]
        target = next((t for t, q in self.players.items() if q["name"].lower() == nick.lower()), None)
        if not nick or (target is None and not ban):
            return 404, {"error": "no such player"}
        if ban:
            self.banned.add(nick.lower())
        suffix = f" Reason: {reason}" if reason else ""
        if target is not None:
            name = self.players.pop(target)["name"]
            self.kicked[target] = ("You have been banned from the lobby!" if ban else
                                   "You have been kicked from the lobby!") + suffix
            while len(self.kicked) > 64:
                self.kicked.pop(next(iter(self.kicked)))
        else:
            name = nick
        self._say(None, f"{name} has been {'banned' if ban else 'kicked'} from the lobby.{suffix}")
        return 200, {}

    def set_subject(self, token, subject):
        p = self.player(token)
        if not p:
            return self.not_signed_in(token)
        if p["name"].lower() not in self.moderators:
            return 403, {"error": "The command is restricted to moderators."}
        self.subject = str(subject or "").strip()[:MAX_TEXT]
        # StatusMessageEvents.js Subject: the room hears of the change.
        self._say(None, f"{p['name']} changed the lobby subject to:\n{self.subject}" if self.subject
                  else f"{p['name']} deleted the lobby subject.")
        return 200, {"subject": self.subject}

    @staticmethod
    def _hash(salt, password):
        return hashlib.sha256((salt + str(password)).encode("utf-8")).hexdigest()

    def _save_accounts(self):
        if not self.accounts_path:
            return
        tmp = self.accounts_path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(self.accounts, f)
        os.replace(tmp, self.accounts_path)

    def register_account(self, name, password):
        """0 A.D. prelobby Register: an XMPP in-band registration."""
        name = str(name or "").strip()
        if not ACCOUNT_NAME.match(name):
            return 400, {"error": "Invalid username"}
        if not password:
            return 400, {"error": "Please enter your password"}
        if name.lower() in self.accounts:
            return 409, {"error": "Username already taken"}
        salt = secrets.token_hex(16)
        self.accounts[name.lower()] = {"name": name, "salt": salt, "hash": self._hash(salt, password)}
        self._save_accounts()
        return 200, {"name": name}

    def login(self, name, password=None):
        name = " ".join(str(name or "").split())[:MAX_NAME]
        if not name:
            return 400, {"error": "a name is required"}
        acct = self.accounts.get(name.lower())
        if acct:
            if not password or not secrets.compare_digest(self._hash(acct["salt"], password), acct["hash"]):
                return 401, {"error": "Wrong username or password"}
            name = acct["name"]
        elif password or self.accounts_only:
            return 401, {"error": "Wrong username or password" if password else "Register an account first"}
        elif not ACCOUNT_NAME.match(name):
            return 400, {"error": "Invalid username"}
        if name.lower() in self.banned:
            return 403, {"error": "You have been banned from the lobby!"}
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
            return self.not_signed_in(token)
        if status not in STATUSES:
            return 400, {"error": "status must be one of " + ", ".join(STATUSES)}
        p["status"] = status
        return 200, {}

    def player_list(self):
        rows = []
        for p in self.players.values():
            row = {"name": p["name"], "status": p["status"]}
            if p["name"].lower() in self.moderators:
                row["moderator"] = True
            r = self.ratings.get(p["name"].lower())
            if r and r.get("rating") is not None:
                row["rating"] = r["rating"]
            rows.append(row)
        return 200, {"players": sorted(rows, key=lambda r: r["name"].lower()), "subject": self.subject}

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
        elif rep["playerStates"] != states:
            # EcheLOn's ReportManager drops reports that differ from the match's first.
            return 409, {"error": "the report differs from the match's other reports"}
        rep["reporters"].add(p["name"].lower())
        if rep.get("done"):
            return 200, {"rated": False}
        if len(rep["reporters"]) < len({n.lower() for n in rep["players"]}):
            return 202, {"rated": False, "waiting": True}
        rep["done"] = True
        return 200, self._rate(rep)

    def _row(self, name):
        """A player's leaderboard row: rating None until a rated game (EcheLOn's -1);
        games counts every finished game the lobby recorded (len(player.games))."""
        r = self.ratings.setdefault(name.lower(), {"name": name, "rating": None, "highestRating": None,
                                                   "wins": 0, "losses": 0})
        r["name"] = name
        r.setdefault("games", r["wins"] + r["losses"])  # rows saved before games was kept
        return r

    def _rate(self, rep):
        """EcheLOn's Leaderboard.add_and_rate_game: a finished game (nobody still
        active, someone won) is recorded for its players; a 1v1 with exactly one
        winner also changes both ratings (_verify_game, _rate_game)."""
        names, states = rep["players"], rep["playerStates"]
        if "active" in states or "won" not in states:
            return {"rated": False}  # _add_game: unfinished, or no winner to record
        rows = [self._row(n) for n in names]
        for r in rows:
            r["games"] += 1
        winner = rows[states.index("won")]
        winner["wins"] += 1
        for r, st in zip(rows, states):
            if r is not winner and st != "won":
                r["losses"] += 1
        if len(names) != 2 or names[0].lower() == names[1].lower() or states.count("won") != 1:
            self._save_ratings()
            return {"rated": False}
        result = 1 if winner is rows[0] else -1
        for r in rows:
            if r["rating"] is None:
                r["rating"] = LEADERBOARD_DEFAULT_RATING
        # Both adjustments from the ratings before the game, each with the games
        # counted so far, this one included.
        change0 = get_rating_adjustment(rows[0]["rating"], rows[1]["rating"], rows[0]["games"], rows[1]["games"], result)
        change1 = get_rating_adjustment(rows[1]["rating"], rows[0]["rating"], rows[1]["games"], rows[0]["games"], -result)
        self._say("RatingsBot", "A rated game has ended. %s %s against %s. Rating Adjustment: %s (%d -> %d) and %s (%d -> %d)." % (
            rows[0]["name"], "won" if result == 1 else "lost", rows[1]["name"],
            rows[0]["name"], rows[0]["rating"], rows[0]["rating"] + change0,
            rows[1]["name"], rows[1]["rating"], rows[1]["rating"] + change1))
        for r, change in ((rows[0], change0), (rows[1], change1)):
            r["rating"] += change
            r["highestRating"] = max(r["rating"], r["highestRating"] if r["highestRating"] is not None else -1)
        self._save_ratings()
        return {"rated": True, "ratings": {r["name"]: r["rating"] for r in rows}}

    def _ranked(self):
        rated = [r for r in self.ratings.values() if r.get("rating") is not None]
        return sorted(rated, key=lambda r: (-r["rating"], r["name"].lower()))

    def leaderboard(self):
        board = [{"name": r["name"], "rating": r["rating"]} for r in self._ranked()[:LEADERBOARD_SIZE]]
        return 200, {"players": board}

    def profile(self, name):
        """EcheLOn's get_profile: rank counts the players rated at least as high;
        a player with recorded games but no rated one shows "-" for those."""
        r = self.ratings.get(str(name or "").strip().lower())
        if r is None:
            return 404, {"error": "player not found"}
        games = r.get("games", r["wins"] + r["losses"])
        out = {"name": r["name"], "totalGamesPlayed": games, "wins": r["wins"], "losses": games - r["wins"]}
        if r.get("rating") is None:
            out.update(rating="-", rank="-", highestRating="-")
        else:
            out.update(rating=r["rating"], highestRating=r["highestRating"],
                       rank=sum(1 for o in self._ranked() if o["rating"] >= r["rating"]))
        return 200, out

    def add_punch(self, data):
        """A joiner's STUN endpoint for the host to punch towards (0 A.D.'s
        Jingle session-initiate with one ICE-UDP server-reflexive candidate)."""
        if not self.player(data.get("token")):
            return 401, {"error": "not signed in"}
        room = next((r for r in self.rooms if r["id"] == str(data.get("id"))), None)
        if not room:
            return 404, {"error": "no such game"}
        ip = str(data.get("ip") or "")[:64]
        port = int(data.get("port") or 0)
        if not ip or not 0 < port < 65536:
            return 400, {"error": "bad endpoint"}
        now = self.clock()
        room["punch"] = [e for e in room["punch"] if now - e["time"] <= PUNCH_TIMEOUT][-(PUNCH_KEEP - 1):]
        room["punch"].append({"seq": room["next_punch"], "ip": ip, "port": port, "time": now})
        room["next_punch"] += 1
        # Behind the host's own public address: NAT loopback is not a given, so
        # the joiner uses the LAN address (0 A.D. asks for it with isLocalIP).
        if ip == room["host_ip"] and room["local_ip"] and room["local_port"]:
            return 200, {"local_ip": room["local_ip"], "local_port": room["local_port"]}
        return 200, {}

    def punches(self, room_id, since, token=None, peer_ip="127.0.0.1"):
        room = self._own_room(str(room_id), self._owner(token), peer_ip)
        if not room:
            return 404, {"error": "no such game"}
        now = self.clock()
        eps = [{"seq": e["seq"], "ip": e["ip"], "port": e["port"]} for e in room["punch"]
               if e["seq"] > since and now - e["time"] <= PUNCH_TIMEOUT]
        return 200, {"endpoints": eps, "last": room["next_punch"] - 1}

    def _owner(self, token):
        p = self.player(token) if token else None
        return p["name"] if p else ""

    def _own_room(self, rid, owner, peer_ip):
        """The poster's own game under id rid (its listed id, or the id it asked
        for): the signed-in account's, else the one posted from this address."""
        def mine(r):
            if owner and r["owner"] == owner:
                return True
            return r["poster_ip"] == peer_ip and (not owner or not r["owner"])
        rooms = [r for r in self.rooms if r["id"] == rid] + [r for r in self.rooms if r["base"] == rid and r["id"] != rid]
        return next((r for r in rooms if mine(r)), None)

    def room_list(self):
        return 200, {"rooms": [{k: v for k, v in r.items() if k not in LISTED_HIDDEN_KEYS} for r in self.rooms]}

    def connect(self, data):
        """A game's address, only for a signed-in joiner (0 A.D. XmppClient
        SendIqGetConnectionData; the host's own checks, the password and its
        bans, stay at the game socket)."""
        p = self.player(data.get("token"))
        if not p:
            return 401, {"error": "not signed in"}
        room = next((r for r in self.rooms if r["id"] == str(data.get("id"))), None)
        if not room:
            return 404, {"error": "no such game"}
        now = self.clock()
        tickets = {t: v for t, v in room["tickets"].items() if now - v["time"] <= TICKET_TIMEOUT}
        ticket = secrets.token_hex(16)
        tickets[ticket] = {"name": p["name"], "time": now}
        room["tickets"] = dict(list(tickets.items())[-64:])
        return 200, {"host_ip": room["host_ip"], "host_port": room["host_port"], "ticket": ticket}

    def redeem_ticket(self, data, peer_ip="127.0.0.1"):
        """The joiner a ticket was given to, for the host of its game only;
        a ticket counts once."""
        room = self._own_room(str(data.get("id")), self._owner(data.get("token")), peer_ip)
        if not room:
            return 404, {"error": "no such game"}
        t = room["tickets"].pop(str(data.get("ticket") or ""), None)
        if not t or self.clock() - t["time"] > TICKET_TIMEOUT:
            return 404, {"error": "unknown ticket"}
        return 200, {"name": t["name"]}

    def register(self, data, peer_ip="127.0.0.1"):
        """Registers a game, or updates the fields a host sends again (a field
        left out keeps its value). Without host_ip the game is reached at the
        address the request came from, as 0 A.D.'s lobby bot records it."""
        rid = str(data.get("id") or f"room-{int(data.get('host_port') or 20595)}")[:64]
        owner = self._owner(data.get("token"))
        old = self._own_room(rid, owner, peer_ip) or {}
        new_id = old.get("id", rid)
        n = 2
        while not old and any(r["id"] == new_id for r in self.rooms):
            new_id = f"{rid}-{n}"
            n += 1

        def field(key, default, kind):
            return kind(data[key]) if data.get(key) not in (None, "") else old.get(key, default)

        room = {
            "id": new_id,
            "base": old.get("base", rid),
            "owner": old.get("owner") or owner,
            "poster_ip": old.get("poster_ip", peer_ip),
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
            "mods": _mods(data["mods"]) if "mods" in data else old.get("mods"),
            "local_ip": field("local_ip", "", str)[:64],
            "local_port": field("local_port", 0, int),
            "punch": old.get("punch", []),
            "tickets": old.get("tickets", {}),
            "next_punch": old.get("next_punch", 1),
            "seen": self.clock(),
        }
        if room["state"] not in ROOM_STATES:
            room["state"] = "init"
        if room["mods"] is None:
            del room["mods"]  # a host that sends no mods (an older RTS0)
        public = {k: v for k, v in room.items() if k not in PRIVATE_ROOM_KEYS}
        if old:
            self.rooms[self.rooms.index(old)] = room
            return 200, public
        self.rooms.append(room)
        return 201, public

    def unregister(self, room_id, token=None, peer_ip="127.0.0.1"):
        room = self._own_room(str(room_id), self._owner(token), peer_ip)
        if not room:
            if any(r["id"] == str(room_id) for r in self.rooms):
                return 403, {"error": "not your game"}
            return 404, {"error": "no such game"}
        self.rooms.remove(room)
        return 200, {}

    def chat_since(self, since):
        msgs = [m for m in self.chat if m["id"] > since]
        return 200, {"messages": msgs, "last": self.next_chat - 1}

    def post_chat(self, token, text):
        p = self.player(token)
        if not p:
            return self.not_signed_in(token)
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
            elif path == "/rooms/punch":
                q = parse_qs(url.query)
                try:
                    since = int(q.get("since", ["0"])[0])
                except ValueError:
                    since = 0
                code, obj = LOBBY.punches(q.get("id", [""])[0], since, q.get("token", [""])[0],
                                          self.client_address[0])
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
                elif path == "/rooms/punch":
                    code, obj = LOBBY.add_punch(data)
                elif path == "/rooms/connect":
                    code, obj = LOBBY.connect(data)
                elif path == "/rooms/ticket":
                    code, obj = LOBBY.redeem_ticket(data, self.client_address[0])
                elif path == "/rooms/unregister":
                    code, obj = LOBBY.unregister(data.get("id"), data.get("token"), self.client_address[0])
                elif path == "/login":
                    code, obj = LOBBY.login(data.get("name"), data.get("password"))
                elif path == "/register":
                    code, obj = LOBBY.register_account(data.get("name"), data.get("password"))
                elif path == "/logout":
                    code, obj = LOBBY.logout(data.get("token"))
                elif path == "/presence":
                    code, obj = LOBBY.presence(data.get("token"), data.get("status"))
                elif path == "/chat":
                    code, obj = LOBBY.post_chat(data.get("token"), data.get("text"))
                elif path == "/kick":
                    code, obj = LOBBY.kick(data.get("token"), data.get("nick"), data.get("reason"),
                                           bool(data.get("ban")))
                elif path == "/subject":
                    code, obj = LOBBY.set_subject(data.get("token"), data.get("subject"))
                elif path == "/report":
                    code, obj = LOBBY.report(data.get("token"), data)
                else:
                    code, obj = 404, {"error": "not found"}
            except (TypeError, ValueError):
                code, obj = 400, {"error": "bad field"}
        self._json(code, obj)


def stun_answer(packet, addr):
    """A Binding Success for a Binding Request (RFC 5389), naming the address
    and port it came from in XOR-MAPPED-ADDRESS; None for anything else."""
    if len(packet) < 20:
        return None
    kind, length, cookie = struct.unpack("!HHI", packet[:8])
    if kind != 0x0001 or cookie != STUN_MAGIC_COOKIE or 20 + length > len(packet):
        return None
    try:
        ip = struct.unpack("!I", socket.inet_aton(addr[0]))[0]
    except OSError:
        return None  # not IPv4
    attr = struct.pack("!HHBBHI", 0x0020, 8, 0, 1, addr[1] ^ (STUN_MAGIC_COOKIE >> 16), ip ^ STUN_MAGIC_COOKIE)
    return struct.pack("!HHI", 0x0101, len(attr), STUN_MAGIC_COOKIE) + packet[8:20] + attr


def serve_stun(bind, port):
    """Answers STUN Binding Requests on UDP from a daemon thread; returns the
    socket (its port is getsockname()[1])."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((bind, port))

    def loop():
        while True:
            try:
                packet, addr = sock.recvfrom(2048)
            except OSError:
                return  # closed
            answer = stun_answer(packet, addr)
            if answer:
                try:
                    sock.sendto(answer, addr)
                except OSError:
                    pass

    threading.Thread(target=loop, daemon=True).start()
    return sock


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
    parser.add_argument("--accounts", help="JSON file that keeps the accounts between runs")
    parser.add_argument("--accounts-only", action="store_true",
                        help="refuse names with no account (0 A.D.'s lobby has no guests)")
    parser.add_argument("--stun-port", type=int, default=3478,
                        help="UDP port that answers STUN Binding Requests (0 A.D. lobby.stun.port); 0 = off")
    parser.add_argument("--moderators", default="",
                        help="comma-separated names that may /kick and /ban (0 A.D. lobby moderators)")
    parser.add_argument("--subject", default="", help="the lobby room's subject")
    args = parser.parse_args()
    global LOBBY
    LOBBY = Lobby(timeout=args.timeout, ratings_path=args.ratings, accounts_path=args.accounts,
                  accounts_only=args.accounts_only, moderators=args.moderators.split(","),
                  subject=args.subject)
    if args.stun_port:
        serve_stun(args.bind, args.stun_port)
    serve(args.bind, args.port, not args.quiet).serve_forever()


if __name__ == "__main__":
    main()
