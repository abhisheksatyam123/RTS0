extends Node
# The multiplayer lobby connection (0 A.D. source/lobby/XmppClient.cpp), kept
# for the whole session as 0 A.D. keeps its XMPP client: the player stays
# signed in while a match runs and shows as "playing", and a host's game stays
# in the list (registerGame / changeStateGame) until the host leaves the match
# (unregisterGame). The lobby screen signs in through here; tools/lobby_server.py
# is the server.

const HEARTBEAT_S := 10.0
const REPORT_CHECK_S := 1.0
const REQUEST_TIMEOUT_S := 5.0

var address := "127.0.0.1:8088" # the lobby server, "ip:port"
var token := ""
var player_name := ""
# The game this player hosts, as registered with the lobby; empty when not hosting.
var room := {}

var _timer: Timer
var _report_timer: Timer
var _room_in_match := false # the match that hosts `room` has started
var _reported := {} # matchID -> true: a player reports each match once


func _ready() -> void:
	_timer = Timer.new()
	_timer.wait_time = HEARTBEAT_S
	_timer.autostart = true
	_timer.timeout.connect(_beat)
	add_child(_timer)
	_report_timer = Timer.new()
	_report_timer.wait_time = REPORT_CHECK_S
	_report_timer.autostart = true
	_report_timer.timeout.connect(_check_report)
	add_child(_report_timer)


func signed_in() -> bool:
	return token != ""


func base_url() -> String:
	return "http://" + address


# Calls back with (http_code, parsed_json); code 0 when the lobby is unreachable.
func get_json(path: String, cb: Callable = Callable()) -> void:
	_send(path, HTTPClient.METHOD_GET, "", cb)


func post_json(path: String, body: Dictionary, cb: Callable = Callable()) -> void:
	_send(path, HTTPClient.METHOD_POST, JSON.stringify(body), cb)


func _send(path: String, method: int, body: String, cb: Callable) -> void:
	var req := HTTPRequest.new()
	req.timeout = REQUEST_TIMEOUT_S
	add_child(req)
	req.request_completed.connect(func(result: int, code: int, _headers: PackedStringArray, data: PackedByteArray) -> void:
		req.queue_free()
		var ok := result == HTTPRequest.RESULT_SUCCESS
		var obj = JSON.parse_string(data.get_string_from_utf8()) if ok else null
		if cb.is_valid():
			cb.call(code if ok else 0, obj))
	var headers := PackedStringArray(["Content-Type: application/json"])
	if req.request(base_url() + path, headers, method, body) != OK:
		req.queue_free()
		if cb.is_valid():
			cb.call(0, null)


func sign_in(addr: String, name: String, cb: Callable) -> void:
	address = addr
	post_json("/login", {"name": name}, func(code: int, obj) -> void:
		if code == 200 and obj is Dictionary:
			token = str(obj.get("token", ""))
			player_name = str(obj.get("name", name))
		cb.call(code, obj))


func sign_out() -> void:
	if token != "":
		post_json("/logout", {"token": token})
	token = ""
	room = {}


# The host's game: registered now and refreshed on every heartbeat.
func host_game(game: Dictionary) -> void:
	room = game.duplicate()
	_room_in_match = false
	post_json("/rooms", room)


func _match_node() -> Node:
	var scene := get_tree().current_scene
	return scene if scene != null and scene.has_method("get_lobby_state") else null


func _beat() -> void:
	if token == "":
		return
	var main := _match_node()
	post_json("/presence", {"token": token, "status": "playing" if main != null else "available"})
	if room.is_empty():
		return
	if main == null:
		if _room_in_match:
			# Back from the match: the game is gone.
			post_json("/rooms/unregister", {"id": room.get("id", "")})
			room = {}
		return
	_room_in_match = true
	var st: Dictionary = main.get_lobby_state()
	if not st.get("hosting", false):
		return
	room["players"] = int(st.get("players", 1))
	room["state"] = "running" if st.get("started", false) else "init"
	post_json("/rooms", room)


# gui/session/lobby/LobbyRatingReporter.js: once this player has won or been
# defeated in a lobby game of two human players (the only rated games, Available),
# it sends the lobby its report; the lobby rates the game when both have.
# Observers send nothing. The match id is the same on both machines: the
# lockstep seed, map and the two names.
func _check_report() -> void:
	if token == "":
		return
	var main := _match_node()
	if main == null:
		return
	var st: Dictionary = main.get_lobby_state()
	if not st.get("started", false) or int(st.get("seat", 255)) >= 8:
		return
	var report := rating_report(main.get_match_statistics(), main.get_match_summary(), int(st.get("seat", 255)))
	if report.is_empty() or _reported.has(report["matchID"]):
		return
	_reported[report["matchID"]] = true
	report["token"] = token
	post_json("/report", report)


# The report LobbyRatingReporter builds (Players, Score and Misc parts), or {}
# while this match is not one to rate or `seat` is still playing.
static func rating_report(stats: Dictionary, summary: Dictionary, seat: int) -> Dictionary:
	var players: Array = stats.get("players", [])
	if players.size() != 2:
		return {}
	var mine := {}
	for p in players:
		if p.get("ai", false):
			return {}
		if int(p.get("id", -1)) == seat:
			mine = p
	if mine.is_empty() or str(mine.get("state", "active")) == "active":
		return {}
	var names: PackedStringArray = []
	for p in players:
		names.append(str(p.get("name", "")))
	var sorted_names := names.duplicate()
	sorted_names.sort()
	var join := func(key: String) -> String:
		var out := ""
		for p in players:
			out += str(p.get(key, "")) + ","
		return out
	var stat := func(key: String) -> String:
		var out := ""
		for p in players:
			out += str((p.get("stats", {}) as Dictionary).get(key, 0)) + ","
		return out
	return {
		"playerID": seat + 1,
		"matchID": "%s-%s-%s" % [str(summary.get("seed", 0)), str(summary.get("map_id", "")), ",".join(sorted_names)],
		"mapName": str(stats.get("map_name", "")),
		"timeElapsed": int(float(stats.get("game_time", 0.0)) * 1000.0),
		"players": ",".join(names) + ",",
		"playerStates": join.call("state"),
		"civs": join.call("civ_name"),
		"teams": join.call("team"),
		"economyScore": join.call("economyScore"),
		"militaryScore": join.call("militaryScore"),
		"totalScore": join.call("totalScore"),
		"tradeIncome": stat.call("tradeIncome"),
		"tributesSent": stat.call("tributesSent"),
		"tributesReceived": stat.call("tributesReceived"),
		"treasuresCollected": stat.call("treasuresCollected"),
		"lootCollected": stat.call("lootCollected"),
	}


# PlayerColor.ColorPlayerName: "name (rating)" once the player has a rating.
static func rated_name(name: String, rating) -> String:
	return "%s (%s)" % [name, str(rating)] if rating != null and str(rating) != "" else name


# ProfilePanel.FormatWinRate.
static func format_win_rate(profile: Dictionary) -> String:
	var games := int(profile.get("totalGamesPlayed", 0))
	if games <= 0:
		return "-"
	return "%.2f%%" % (float(profile.get("wins", 0)) / games * 100.0)
