#!/usr/bin/env python3
"""In-memory HTTP lobby for RTS0 (GET/POST /rooms). Not a game server."""
import argparse
import json
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import urlparse

ROOMS = []


class LobbyHandler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
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
        path = urlparse(self.path).path
        if path in ("/rooms", "/rooms/"):
            self._json(200, {"rooms": ROOMS})
            return
        self._json(404, {"error": "not found"})

    def do_POST(self):
        path = urlparse(self.path).path
        if path not in ("/rooms", "/rooms/"):
            self._json(404, {"error": "not found"})
            return
        length = int(self.headers.get("Content-Length", "0") or "0")
        raw = self.rfile.read(length) if length else b"{}"
        try:
            data = json.loads(raw.decode("utf-8") or "{}")
        except json.JSONDecodeError:
            self._json(400, {"error": "invalid json"})
            return
        room = {
            "id": str(data.get("id") or f"room-{len(ROOMS) + 1}"),
            "host_ip": str(data.get("host_ip") or "127.0.0.1"),
            "host_port": int(data.get("host_port") or 20595),
            "players": int(data.get("players") or 1),
            "max_players": int(data.get("max_players") or 8),
            "map_id": str(data.get("map_id") or "default"),
        }
        for i, existing in enumerate(ROOMS):
            if existing["id"] == room["id"]:
                ROOMS[i] = room
                self._json(200, room)
                return
        ROOMS.append(room)
        self._json(201, room)


def main():
    parser = argparse.ArgumentParser(description="RTS0 HTTP lobby room list")
    parser.add_argument("--port", type=int, default=8088)
    parser.add_argument("--bind", default="127.0.0.1")
    args = parser.parse_args()
    HTTPServer((args.bind, args.port), LobbyHandler).serve_forever()


if __name__ == "__main__":
    main()
