extends Node
# The multiplayer lobby connection (0 A.D. source/lobby/XmppClient.cpp), kept
# for the whole session as 0 A.D. keeps its XMPP client: the player stays
# signed in while a match runs and shows as "playing", and a host's game stays
# in the list (registerGame / changeStateGame) until the host leaves the match
# (unregisterGame). The lobby screen signs in through here; tools/lobby_server.py
# is the server.

const HEARTBEAT_S := 10.0
const REPORT_CHECK_S := 1.0
const NAT_TICK_S := 0.25
const PUNCH_POLL_S := 0.5 # how often a host in game setup asks for joiners to punch towards
const PUNCH_POLL_RUNNING_S := 2.0 # once the match runs (rejoiners and observers)
const CLIENT_PUNCH_DELAY_S := 1.0 # 0 A.D. NetClient waits this before its own punch
const REQUEST_TIMEOUT_S := 5.0

var address := "127.0.0.1:8088" # the lobby server, "ip:port"
var token := ""
var player_name := ""
# The game this player hosts, as registered with the lobby; empty when not hosting.
var room := {}

var _timer: Timer
var _report_timer: Timer
var _room_in_match := false # the match that hosts `room` has started
var _room_started_at := 0 # unix time the hosted match started, 0 before
var _reported := {} # matchID -> true: a player reports each match once
# NAT traversal (0 A.D. StunClient + Jingle hole punching), for the match
# hosted as `room` or joined as `join_room` (a game picked from the list).
var join_room := ""
var publish_lan := true # a host sends its LAN address for joiners behind its router
var _nat_timer: Timer
var _nat_asked := false
var _nat_published := false
var _nat_sent := false
var _punch_since := 0
var _punch_wait := 0.0
var _punch_busy := false


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
	_nat_timer = Timer.new()
	_nat_timer.wait_time = NAT_TICK_S
	_nat_timer.autostart = true
	_nat_timer.timeout.connect(_nat_tick)
	add_child(_nat_timer)


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
	join_room = ""


# The host's game: registered now and refreshed on every heartbeat.
func host_game(game: Dictionary) -> void:
	room = game.duplicate()
	join_room = ""
	_room_in_match = false
	_room_started_at = 0
	_reset_nat()
	post_json("/rooms", room)


# The game this player joins from the list ("" for a typed address, which
# 0 A.D. joins directly, with no STUN).
func join_game(room_id: String) -> void:
	join_room = room_id
	_reset_nat()


func _reset_nat() -> void:
	_nat_asked = false
	_nat_published = false
	_nat_sent = false
	_punch_since = 0
	_punch_wait = 0.0
	_punch_busy = false


# 0 A.D. default.cfg lobby.stun.server; empty means this lobby's own host.
func stun_server() -> String:
	var server := str(RtsoUserConfig.new().get_value("lobby.stun.server")).strip_edges()
	return server if server != "" else address.get_slice(":", 0)


# NAT traversal, as 0 A.D. does it through its lobby. Both sides ask a STUN
# server for their game socket's public endpoint. The host registers that
# endpoint as its game's address, plus its LAN address for players behind the
# same router. A joiner sends its endpoint to the lobby (SendStunEndpointToHost)
# and punches towards the host; the host punches towards every joiner the lobby
# names (SendHolePunchingMessage), so both NATs let the other side in.
func _nat_tick() -> void:
	var main := _match_node()
	if main == null or not main.has_method("get_net_nat_state") or token == "":
		return
	var hosting := not room.is_empty()
	if not hosting and join_room == "":
		return
	var cfg := RtsoUserConfig.new()
	if not cfg.get_bool("lobby.stun.enabled"):
		return
	if not _nat_asked:
		_nat_asked = true
		main.net_query_stun(stun_server(), int(cfg.get_number("lobby.stun.port")))
		return
	var nat: Dictionary = main.get_net_nat_state()
	if hosting:
		_host_nat_tick(main, nat)
	else:
		_join_nat_tick(main, nat)


func _host_nat_tick(main: Node, nat: Dictionary) -> void:
	var st: Dictionary = main.get_lobby_state()
	if not st.get("hosting", false):
		return
	if not _nat_published and int(nat.get("stun", 0)) == 2:
		_nat_published = true
		room["host_ip"] = str(nat.get("public_ip", ""))
		room["host_port"] = int(nat.get("public_port", 0))
		room["local_ip"] = str(nat.get("local_ip", "")) if publish_lan else ""
		room["local_port"] = int(nat.get("local_port", 0)) if publish_lan else 0
		post_json("/rooms", room)
	_punch_wait -= NAT_TICK_S
	if _punch_busy or _punch_wait > 0.0:
		return
	_punch_wait = PUNCH_POLL_RUNNING_S if st.get("started", false) else PUNCH_POLL_S
	_punch_busy = true
	get_json("/rooms/punch?id=%s&since=%d" % [str(room.get("id", "")).uri_encode(), _punch_since],
		func(code: int, obj) -> void:
			_punch_busy = false
			if code != 200 or not obj is Dictionary:
				return
			var m := _match_node()
			for e in obj.get("endpoints", []):
				if m != null and e is Dictionary:
					m.net_hole_punch(str(e.get("ip", "")), int(e.get("port", 0)))
			_punch_since = maxi(_punch_since, int(obj.get("last", 0))))


func _join_nat_tick(main: Node, nat: Dictionary) -> void:
	if _punch_wait > 0.0:
		_punch_wait -= NAT_TICK_S
		if _punch_wait <= 0.0:
			main.net_hole_punch(MatchSettings.net_ip, MatchSettings.net_port)
		return
	if _nat_sent or int(nat.get("stun", 0)) != 2:
		return
	_nat_sent = true
	post_json("/rooms/punch", {"token": token, "id": join_room, "ip": str(nat.get("public_ip", "")),
			"port": int(nat.get("public_port", 0))},
		func(code: int, obj) -> void:
			var m := _match_node()
			if m == null or code != 200 or not obj is Dictionary:
				return
			if str(obj.get("local_ip", "")) != "" and int(obj.get("local_port", 0)) > 0:
				# Same router as the host: its LAN address (no NAT loopback
				# needed), and ours for the host to punch towards (0 A.D. sends
				# its FindLocalIP address then).
				var mine: Dictionary = m.get_net_nat_state()
				post_json("/rooms/punch", {"token": token, "id": join_room, "ip": str(mine.get("local_ip", "")),
						"port": int(mine.get("local_port", 0))})
				m.net_retarget_host(str(obj["local_ip"]), int(obj["local_port"]))
			else:
				_punch_wait = CLIENT_PUNCH_DELAY_S)


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
	if st.get("started", false) and _room_started_at == 0:
		_room_started_at = int(Time.get_unix_time_from_system())
	room.merge(room_details(get_node("/root/MatchSettings"),
		main.get_net_setup_state() if main.has_method("get_net_setup_state") else {}, _room_started_at), true)
	post_json("/rooms", room)


# The game's details 0 A.D.'s host sends the lobby with its game (Game.js
# stanza: mapName, mapType, mapSize, victoryConditions, startTime, players),
# from the host's Game Setup (MatchSettings) and its seats
# (get_net_setup_state). Older lobby servers ignore the extra fields.
static func room_details(ms: Object, net: Dictionary, started_at: int) -> Dictionary:
	var setup: GDScript = load("res://scenes/menu/gamesetup.gd")
	var map_id := str(ms.get("map_id"))
	var map_name := map_id.replace("_", " ").capitalize()
	var map_type := ""
	for r in setup.RMS_MAPS:
		if r[0] == map_id:
			map_name = str(r[1]).trim_suffix(" (RMS)")
			map_type = "random"
	if map_type == "":
		var known: Dictionary = setup._map_types_data().get("maps", {})
		map_type = str((known.get(map_id, {}) as Dictionary).get("type", "skirmish"))
	# Game.js translateMapSize: the size's name for a random map, "Default" otherwise.
	var map_size := "Default"
	if map_type == "random":
		for sz in setup.MAP_SIZES:
			if int(sz[0]) == int(ms.get("map_size")):
				map_size = str(sz[1]).get_slice(" (", 0)
	var victory := PackedStringArray()
	var mask: int = setup.victory_mask(int(ms.get("victory")))
	for k in setup.VICTORY_KINDS:
		if mask & int(k[0]):
			victory.append(k[1])
	var at := func(key: String, i: int, default):
		var a: Array = ms.get(key)
		return a[i] if i < a.size() else default
	var players := []
	var slots: Array = net.get("slots", [])
	var count := slots.size() if not net.is_empty() else int(ms.get("player_count"))
	for i in count:
		var kind := int(at.call("slot_types", i, 0))
		var name := ""
		if kind == 1:
			name = str(at.call("slot_names", i, ""))
			if name == "":
				name = "AI"
		elif kind == 0:
			name = str((slots[i] as Dictionary).get("name", "")) if i < slots.size() else ""
		if name == "":
			continue # a closed or open seat
		var civ := int(at.call("slot_civs", i, -1))
		var p := {"name": name, "team": int(at.call("slot_teams", i, 0)) - 1, "ai": kind == 1,
			"civ": "random" if civ < 0 else (str(setup.CIVS[civ]["code"]) if civ < setup.CIVS.size() else "")}
		if kind == 1:
			p["ai_difficulty"] = int(at.call("slot_diffs", i, ms.get("ai_difficulty")))
			p["ai_behavior"] = int(at.call("slot_behs", i, ms.get("ai_behavior")))
		players.append(p)
	for o in net.get("observers", []):
		players.append({"name": str(o.get("name", "")), "team": -1, "observer": true})
	return {"map_id": map_id, "map_name": map_name, "map_type": map_type, "map_size": map_size,
		"victory": ",".join(victory), "start_time": started_at, "player_names": players}


# Game.js gameRating: the average rating of the playing (not observing)
# players, a player with none counting as g_DefaultLobbyRating.
const DEFAULT_LOBBY_RATING := 1200


static func game_rating(room_players: Array, ratings: Dictionary) -> int:
	var total := 0
	var count := 0
	for p in room_players:
		if p.get("observer", 0):
			continue
		var r = ratings.get(str(p.get("name", "")))
		total += int(r) if r != null and str(r) != "" else DEFAULT_LOBBY_RATING
		count += 1
	return roundi(float(total) / count) if count > 0 else DEFAULT_LOBBY_RATING


# LobbyPage/GameDetails.js: the selected game's map and settings, its name,
# host and start time, then "Players: n/m" over gamedescription.js
# formatPlayerInfo's list (by team, observers last), each human as
# "name (rating)" (their lobby nick).
static func game_details(room: Dictionary, ratings: Dictionary) -> String:
	var setup: GDScript = load("res://scenes/menu/gamesetup.gd")
	var summary: GDScript = load("res://scenes/session/summary_screen.gd")
	var esc := func(t) -> String: return str(t).replace("[", "[lb]")
	var map_title := str(room.get("map_name", "")) if str(room.get("map_name", "")) != "" else str(room.get("map_id", ""))
	var type_title := str(room.get("map_type", ""))
	var desc := ""
	for t in setup.MAP_TYPES:
		if t[0] == type_title:
			type_title = t[1]
	for r in setup.RMS_MAPS:
		if r[0] == str(room.get("map_id", "")):
			desc = r[6]
	var victory := str(room.get("victory", ""))
	var lines := PackedStringArray(["[b]%s[/b]" % esc.call(map_title),
		"[b]Victory Conditions:[/b] " + (esc.call(victory.replace(",", ", ")) if victory != "" else "Endless Game")])
	if type_title != "":
		lines.append("[b]Map Type:[/b] " + esc.call(type_title))
	if str(room.get("map_size", "")) != "":
		lines.append("[b]Map Size:[/b] " + esc.call(room["map_size"]))
	if desc != "":
		lines.append("[b]Map Description:[/b] " + esc.call(desc))
	lines.append("")
	lines.append(esc.call(room.get("name", room.get("id", ""))))
	lines.append("Host: " + esc.call(room.get("host_name", "")))
	if int(room.get("start_time", 0)) > 0:
		var t := Time.get_datetime_dict_from_unix_time(int(room["start_time"]) + int(Time.get_time_zone_from_system().get("bias", 0)) * 60)
		lines.append("Game started at %02d:%02d" % [int(t["hour"]), int(t["minute"])])
	lines.append("")
	lines.append("[b]Players: %d/%d[/b]" % [int(room.get("players", 1)), int(room.get("max_players", 8))])
	var teams := {}
	var observers := PackedStringArray()
	for p in room.get("player_names", []):
		var name := str(p.get("name", ""))
		if p.get("observer", 0):
			observers.append(esc.call(rated_name(name, ratings.get(name))))
			continue
		var civ := str(p.get("civ", ""))
		var civ_title := "Random" if civ == "random" else ("Unknown Civilization" if civ == "" else civ)
		for c in setup.CIVS:
			if c["code"] == civ:
				civ_title = c["name"]
		var parts := [civ_title]
		var who := name
		if p.get("ai", 0):
			parts.append(summary.ai_description(p))
		else:
			who = rated_name(name, ratings.get(name))
		var t := int(p.get("team", -1))
		if not teams.has(t):
			teams[t] = PackedStringArray()
		teams[t].append("%s (%s)" % [esc.call(who), ", ".join(parts)])
	var keys := teams.keys()
	keys.sort()
	if keys.has(-1):
		keys.erase(-1)
		keys.append(-1)
	var groups := PackedStringArray()
	if keys.size() == 1:
		groups.append("\n".join(teams[keys[0]]))
	else:
		for t in keys:
			groups.append("[b]%s:[/b]\n%s" % ["No Team" if t == -1 else "Team %d" % (t + 1), "\n".join(teams[t])])
	if not observers.is_empty():
		groups.append("[b]%s:[/b]\n%s" % ["Observer" if observers.size() == 1 else "Observers", "\n".join(observers)])
	if not groups.is_empty():
		lines.append("\n\n".join(groups))
	return "\n".join(lines)


# gui/session/lobby/LobbyRatingReporter.js: once this player has won or been
# defeated in a lobby game of two human players (the only rated games, Available),
# it sends the lobby its report; the lobby rates the game when both have.
# Observers send nothing. The match id is the same on both machines: the
# lockstep seed, map and the two names.
func _check_report() -> void:
	if token == "" or not bool(get_node("/root/MatchSettings").rated): # Rated Game off: LobbyRatingReporter sends nothing
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
