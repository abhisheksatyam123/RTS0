extends Node
# The multiplayer lobby connection (0 A.D. source/lobby/XmppClient.cpp), kept
# for the whole session as 0 A.D. keeps its XMPP client: the player stays
# signed in while a match runs and shows as "playing", and a host's game stays
# in the list (registerGame / changeStateGame) until the host leaves the match
# (unregisterGame). The lobby screen signs in through here; tools/lobby_server.py
# is the server.

const HEARTBEAT_S := 10.0
const REQUEST_TIMEOUT_S := 5.0

var address := "127.0.0.1:8088" # the lobby server, "ip:port"
var token := ""
var player_name := ""
# The game this player hosts, as registered with the lobby; empty when not hosting.
var room := {}

var _timer: Timer
var _room_in_match := false # the match that hosts `room` has started


func _ready() -> void:
	_timer = Timer.new()
	_timer.wait_time = HEARTBEAT_S
	_timer.autostart = true
	_timer.timeout.connect(_beat)
	add_child(_timer)


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
