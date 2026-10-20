extends Node
# The multiplayer lobby connection (0 A.D. source/lobby/XmppClient.cpp), kept
# for the whole session as 0 A.D. keeps its XMPP client: the player stays
# signed in while a match runs and shows as "playing", and a host's game stays
# in the list (registerGame / changeStateGame) until the host leaves the match
# (unregisterGame). The lobby screen signs in through here; tools/lobby_server.py
# is the server.

## 0 A.D. KickStrings Local: a moderator kicked or banned us, with the reason.
signal kicked(text: String)
## The lobby no longer knows our session (it expired, or the server restarted):
## 0.28 ConnectionHandler askReconnect.
signal disconnected

# 0.28 lobby ChatPanel.xml chatInput max_length; lobby_server.py MAX_CHAT.
const MAX_CHAT := 1024
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

# XmppClient keeps the session's lobby chat: every lobby page replays the
# messages after this id (-1: no page has read the chat since signing in).
var chat_from := -1
# The session was lost (disconnected) and not signed in again since.
var lost := false
var _last_login := {} # address, name and encrypted password of the last sign-in, to reconnect

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
	_room_timer = Timer.new()
	_room_timer.wait_time = ROOM_CHECK_S
	_room_timer.autostart = true
	_room_timer.timeout.connect(_room_tick)
	add_child(_room_timer)


## NetServer SetupUPnP: a host asks the router (an UPnP IGD) to forward its
## game port, on a thread so hosting never waits on it: discovery for 2 s,
## then a permanent "0AD Multiplayer" UDP mapping, checked by reading it back.
## 0.28 builds do this when compiled with miniupnpc; network.upnp turns it off.
## upnp_status is the last attempt's outcome, as NetServer logs it.
var upnp_status := ""
var _upnp_thread: Thread

func map_upnp(port: int) -> void:
	if not RtsoUserConfig.new().get_bool("network.upnp"):
		upnp_status = "off"
		return
	if _upnp_thread != null:
		if _upnp_thread.is_alive():
			return
		_upnp_thread.wait_to_finish()
	upnp_status = "discovering"
	_upnp_thread = Thread.new()
	_upnp_thread.start(_upnp_run.bind(port))

func upnp_busy() -> bool:
	return _upnp_thread != null and _upnp_thread.is_alive()

func _upnp_run(port: int) -> void:
	var u := UPNP.new()
	var out := ""
	if u.discover(2000, 2, "InternetGatewayDevice") != UPNP.UPNP_RESULT_SUCCESS or u.get_gateway() == null:
		out = "No IGD found"
	elif not u.get_gateway().is_valid_gateway():
		out = "No valid IGD found"
	else:
		var ext := u.query_external_address()
		var r := u.add_port_mapping(port, port, "0AD Multiplayer", "UDP", 0)
		if r != UPNP.UPNP_RESULT_SUCCESS:
			out = "AddPortMapping(%d) failed with code %d" % [port, r]
		else:
			out = "External %s:%d UDP is redirected to %s:%d" % [ext, port, u.get_gateway().igd_our_addr, port]
	print("Net server: UPnP: ", out)
	_upnp_done.call_deferred(out)

func _upnp_done(out: String) -> void:
	upnp_status = out

func _exit_tree() -> void:
	if _upnp_thread != null:
		_upnp_thread.wait_to_finish()


func signed_in() -> bool:
	return token != ""


## lobby.tls (XmppClient's TLS): https to the lobby when on.
func base_url() -> String:
	return ("https://" if RtsoUserConfig.new().get_bool("lobby.tls") else "http://") + address


# Calls back with (http_code, parsed_json); code 0 when the lobby is unreachable.
func get_json(path: String, cb: Callable = Callable()) -> void:
	_send(path, HTTPClient.METHOD_GET, "", cb)


func post_json(path: String, body: Dictionary, cb: Callable = Callable()) -> void:
	_send(path, HTTPClient.METHOD_POST, JSON.stringify(body), cb)


func _send(path: String, method: int, body: String, cb: Callable) -> void:
	var req := HTTPRequest.new()
	req.timeout = REQUEST_TIMEOUT_S
	add_child(req)
	# Only an answer to a request made with our own token can say we were kicked.
	var sent_token := token if token != "" and (body.contains(token) or path.contains(token)) else ""
	req.request_completed.connect(func(result: int, code: int, _headers: PackedStringArray, data: PackedByteArray) -> void:
		req.queue_free()
		var ok := result == HTTPRequest.RESULT_SUCCESS
		var obj = JSON.parse_string(data.get_string_from_utf8()) if ok else null
		if ok and code == 401 and obj is Dictionary and sent_token != "" and sent_token == token:
			token = ""
			room = {}
			join_room = ""
			moderator = false
			chat_from = -1
			if obj.has("kicked"):
				kicked.emit(str(obj["kicked"]))
			else:
				lost = true
				disconnected.emit()
		if cb.is_valid():
			cb.call(code if ok else 0, obj))
	var headers := PackedStringArray(["Content-Type: application/json"])
	if req.request(base_url() + path, headers, method, body) != OK:
		req.queue_free()
		if cb.is_valid():
			cb.call(0, null)


# 0 A.D. EncryptPassword (source/lobby/pkcs5_pbkdf2.cpp): the lobby gets
# PBKDF2-HMAC-SHA256 of the password, 1337 rounds, salted with SHA-256 of a
# fixed base and the lower-case name, as upper-case hex; never the typed text.
# tools/lobby_server.py encrypt_password is the same.
const ENCRYPT_SALT_BASE := [244, 243, 249, 244, 32, 33, 34, 35, 10, 11, 12, 13, 14, 15, 16, 17,
	18, 19, 20, 32, 33, 244, 224, 127, 129, 130, 140, 153, 133, 123, 234, 123]
const ENCRYPT_ITERATIONS := 1337

static func encrypt_password(password: String, username: String) -> String:
	var ctx := HashingContext.new()
	ctx.start(HashingContext.HASH_SHA256)
	ctx.update(PackedByteArray(ENCRYPT_SALT_BASE))
	ctx.update(username.to_lower().to_utf8_buffer())
	var salt := ctx.finish()
	var crypto := Crypto.new()
	var key := password.to_utf8_buffer()
	var u := crypto.hmac_digest(HashingContext.HASH_SHA256, key, salt + PackedByteArray([0, 0, 0, 1]))
	var out := u.duplicate()
	for _i in ENCRYPT_ITERATIONS - 1:
		u = crypto.hmac_digest(HashingContext.HASH_SHA256, key, u)
		for j in out.size():
			out[j] ^= u[j]
	return out.hex_encode().to_upper()


## 0 A.D. prelobby checkUsername / checkPassword / checkPasswordConfirmation
## for a new account (credentials.js; lobby/password.js minimumPasswordLength).
const MIN_PASSWORD_LENGTH := 8

static func register_problem(who: String, password: String, confirm: String) -> String:
	if who == "":
		return "Please enter your username"
	if not valid_name(who):
		return "Invalid username"
	if password == "":
		return "Please enter your password"
	if password.length() < MIN_PASSWORD_LENGTH:
		return "Please choose a longer password"
	if confirm == "":
		return "Please enter your password again"
	if password != confirm:
		return "Passwords do not match"
	return ""


## credentials.js: 3 to 20 of a-z, 0-9, ".", "_" and "-", with at least one
## letter, in any case (tools/lobby_server.py ACCOUNT_NAME).
static func valid_name(who: String) -> bool:
	return RegEx.create_from_string("^(?=.*[a-zA-Z])[a-zA-Z0-9._-]{3,20}$").search(who) != null


# 0 A.D. prelobby Register: an account the lobby keeps (tools/lobby_server.py --accounts).
func register(addr: String, name: String, password: String, cb: Callable) -> void:
	address = addr
	post_json("/register", {"name": name, "password": encrypt_password(password, name)}, cb)


# encrypted is encrypt_password's (what lobby.password keeps when the password
# is remembered); empty signs in as a guest, on a lobby that allows them.
# A sign-in right after a sign-out waits for the /logout answer: arriving
# first, it would find the name still in use (409).
var _logout_pending := false
var _after_logout: Array[Callable] = []

## AccountSettingsPage: Engine.LobbyChangePassword, encrypted as for a login;
## a reconnect then signs in with the new one.
func change_password(encrypted: String, cb: Callable) -> void:
	post_json("/password", {"token": token, "password": encrypted}, func(code: int, obj) -> void:
		if code == 200 and not _last_login.is_empty():
			_last_login["encrypted"] = encrypted
		cb.call(code, obj))


func sign_in(addr: String, name: String, cb: Callable, encrypted: String = "") -> void:
	if _logout_pending:
		_after_logout.append(func() -> void: sign_in(addr, name, cb, encrypted))
		return
	address = addr
	var body := {"name": name}
	if encrypted != "":
		body["password"] = encrypted
	post_json("/login", body, func(code: int, obj) -> void:
		if code == 200 and obj is Dictionary:
			token = str(obj.get("token", ""))
			player_name = str(obj.get("name", name))
			lost = false
			chat_from = -1
			_last_login = {"address": addr, "name": name, "encrypted": encrypted}
		cb.call(code, obj))


## The lobby server's terms at addr (0 A.D. prelobby/common/terms): calls back
## with [{page, title, text}], empty when the server has none or does not answer.
func fetch_terms(addr: String, cb: Callable) -> void:
	if not signed_in():
		address = addr
	get_json("/terms", func(code: int, obj) -> void:
		cb.call(Array(obj.get("pages", [])) if code == 200 and obj is Dictionary else []))


## termslobby.js getTermsHash: the accepted version, salted with the player name.
static func terms_hash(who: String, text: String) -> String:
	return (who + text).md5_text()


## ConnectionHandler askReconnect "Yes": sign in again as before.
func reconnect(cb: Callable) -> void:
	if _last_login.is_empty():
		cb.call(0, null)
		return
	sign_in(str(_last_login["address"]), str(_last_login["name"]), cb, str(_last_login["encrypted"]))


func sign_out() -> void:
	if token != "":
		_logout_pending = true
		post_json("/logout", {"token": token}, func(_code: int, _obj) -> void:
			_logout_pending = false
			var queued := _after_logout.duplicate()
			_after_logout.clear()
			for f in queued:
				f.call())
	token = ""
	room = {}
	join_room = ""
	chat_from = -1
	lost = false
	_last_login = {}


# A game post carries the host's token: the lobby keys a game by its host
# (0 A.D.'s bot by JID), so another host on the same port gets its own entry.
func _signed(body: Dictionary) -> Dictionary:
	var b := body.duplicate()
	b["token"] = token
	return b


# The host's game: registered now and refreshed on every heartbeat.
func host_game(game: Dictionary) -> void:
	room = game.duplicate()
	_room_posted = room.duplicate(true)
	join_room = ""
	_room_in_match = false
	_room_started_at = 0
	_reset_nat()
	post_json("/rooms", _signed(room))


## 0 A.D. lobby authentication (XmppClient LobbyAuth, NetServer
## OnLobbyAuth): the host asks the lobby whose ticket a joiner sent, and the
## match checks that name against the one the joiner gave. An unknown ticket,
## or an unreachable lobby, answers "".
func verify_join(peer: int, ticket: String) -> void:
	var answer := func(name: String) -> void:
		var m := _match_node()
		if m != null and m.has_method("net_lobby_auth_result"):
			m.net_lobby_auth_result(peer, ticket, name)
	if ticket == "" or token == "":
		answer.call("")
		return
	# A saved game hosted from the lobby is listed by the match itself, as
	# room-<port> (main_node _on_lobby_host_pressed).
	var rid := str(room.get("id", "room-%d" % int(get_node("/root/MatchSettings").net_port)))
	post_json("/rooms/ticket", {"token": token, "id": rid, "ticket": ticket},
		func(code: int, obj) -> void:
			answer.call(str(obj.get("name", "")) if code == 200 and obj is Dictionary else ""))


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
		post_json("/rooms", _signed(room))
	_punch_wait -= NAT_TICK_S
	if _punch_busy or _punch_wait > 0.0:
		return
	_punch_wait = PUNCH_POLL_RUNNING_S if st.get("started", false) else PUNCH_POLL_S
	_punch_busy = true
	get_json("/rooms/punch?id=%s&since=%d&token=%s" % [str(room.get("id", "")).uri_encode(), _punch_since, token.uri_encode()],
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
			var ms := get_node("/root/MatchSettings")
			main.net_hole_punch(ms.net_ip, ms.net_port)
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


## 0.28 lobby /away and /back: the presence chosen in the lobby, kept by every
## heartbeat until a match makes it "playing".
var away := false
## The lobby page is open as a dialog over the setup or the match: the presence
## stays "available" until it closes (LobbyHandler, QuitButton.js).
var dialog_open := false
## The lobby lists us as a moderator (0 A.D. LobbyGetPlayerRole "moderator").
var moderator := false

func _beat() -> void:
	if token == "":
		return
	var main := _match_node()
	post_json("/presence", {"token": token, "status": "playing" if main != null and not dialog_open else ("away" if away else "available")})
	_room_tick(true)


## LobbyHandler: opening the lobby page, or its dialog, makes us "available"
## (clearing Away); closing the dialog over a game makes us "playing" again.
func lobby_opened(as_dialog: bool) -> void:
	away = false
	dialog_open = as_dialog
	_beat()


func lobby_dialog_closed() -> void:
	dialog_open = false
	_beat()


# LobbyGameRegistration.js: the host re-registers its game soon after any
# change to it (500 ms, sendImmediately on Start) and unregisters once it
# leaves; the heartbeat posts it anyway to keep it listed.
const ROOM_CHECK_S := 0.5
var _room_timer: Timer
var _room_posted := {}

func _room_tick(always := false) -> void:
	if token == "" or room.is_empty():
		return
	var main := _match_node()
	if main == null:
		if _room_in_match:
			# Back from the match: the game is gone.
			post_json("/rooms/unregister", {"token": token, "id": room.get("id", "")})
			room = {}
			_room_posted = {}
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
	if always or room != _room_posted:
		_room_posted = room.duplicate(true)
		post_json("/rooms", _signed(room))


# The game's details 0 A.D.'s host sends the lobby with its game (Game.js
# stanza: mapName, mapType, mapSize, victoryConditions, startTime, players),
# from the host's Game Setup (MatchSettings) and its seats
# (get_net_setup_state). Older lobby servers ignore the extra fields.
static var _map_types := {} # map id -> its map type (the map list file, read once per map)

static func room_details(ms: Object, net: Dictionary, started_at: int) -> Dictionary:
	var setup: GDScript = load("res://scenes/menu/gamesetup.gd")
	var map_id := str(ms.get("map_id"))
	# A Random map stays Random in the game list until the game starts.
	if ms.get("net_map_random") == true and started_at <= 0:
		map_id = "random"
	var map_name := map_id.replace("_", " ").capitalize()
	var map_type := ""
	for r in setup.RMS_MAPS:
		if r[0] == map_id:
			map_name = str(r[1]).trim_suffix(" (RMS)")
			map_type = "random"
	if map_type == "":
		if not _map_types.has(map_id):
			var known: Dictionary = setup._map_types_data().get("maps", {})
			_map_types[map_id] = str((known.get(map_id, {}) as Dictionary).get("type", "skirmish"))
		map_type = _map_types[map_id]
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
	# LobbyGameRegistration.formatClientsForStanza: "Don't send teams, AIs or
	# anything else until the game was started", only who plays and who watches.
	var in_setup: bool = net.has("started") and not net["started"]
	for i in (0 if in_setup else count):
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
		# LobbyGamelistReporter getPlayers: State (won, defeated) and Offline.
		var seat: Dictionary = slots[i] if i < slots.size() else {}
		if str(seat.get("state", "")) != "":
			p["state"] = str(seat["state"])
		if kind == 0 and seat.get("offline", false):
			p["offline"] = true
		players.append(p)
	for e in (slots if in_setup else []):
		if str(e.get("name", "")) != "":
			players.append({"name": str(e["name"])})
	for o in net.get("observers", []):
		players.append({"name": str(o.get("name", "")), "team": -1, "observer": true})
	# LobbyGameRegistration.js maxnbp: the map's player count.
	return {"map_id": map_id, "map_name": map_name, "map_type": map_type, "map_size": map_size,
		"max_players": maxi(count, 1),
		"victory": ",".join(victory), "start_time": started_at, "player_names": players,
		# LobbyGameRegistration.js mods: Engine.GetEngineInfo().mods.
		"mods": load("res://scenes/menu/mods.gd").engine_mods()}


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
	# Game.js: the description of any map this install knows, random or not.
	desc = str(load("res://scenes/menu/replay_browser.gd").map_info(str(room.get("map_id", ""))).get("desc", ""))
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
	var buddy_list := buddies()
	for p in room.get("player_names", []):
		var name := str(p.get("name", ""))
		# gamedescription.js: "• " before a buddy's name.
		var mark := "• " if buddy_list.has(name) and not p.get("ai", 0) else ""
		if p.get("observer", 0):
			observers.append(mark + esc.call(rated_name(name, ratings.get(name))))
			continue
		var civ := str(p.get("civ", ""))
		var civ_title := "Random" if civ == "random" else ("Unknown Civilization" if civ == "" else civ)
		for c in setup.CIVS:
			if c["code"] == civ:
				civ_title = c["name"]
		# formatPlayerInfo: a player sent without a civ (a game in setup) is just its name.
		var parts := [civ_title] if p.has("civ") else []
		var who := name
		if p.get("ai", 0):
			parts.append(summary.ai_description(p))
		else:
			who = rated_name(name, ratings.get(name))
			# formatPlayerInfo: "%(playerName)s (OFFLINE, %(state)s)", no civ.
			if p.get("offline", false):
				parts = ["OFFLINE"]
		if str(p.get("state", "")) in ["won", "defeated"]:
			parts.append(str(p["state"]))
		var t := int(p.get("team", -1))
		if not teams.has(t):
			teams[t] = PackedStringArray()
		teams[t].append("%s%s (%s)" % [mark, esc.call(who), ", ".join(parts)] if not parts.is_empty() else mark + esc.call(who))
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
	# GameDetails.js: "Mods:" (red "Mods (incompatible):"), the ones that do
	# not affect compatibility last, in grey.
	if room.has("mods"):
		var modlib: GDScript = load("res://scenes/menu/mods.gd")
		lines.append("")
		lines.append("[b]Mods:[/b]" if game_compatible(room) else "[color=red][b]Mods (incompatible):[/b][/color]")
		var sorted_mods: Array = (room["mods"] as Array).filter(func(m) -> bool: return m is Dictionary)
		var plain := sorted_mods.filter(func(m) -> bool: return not m.get("ignoreInCompatibilityChecks", false))
		for m in plain + sorted_mods.filter(func(m) -> bool: return m.get("ignoreInCompatibilityChecks", false)):
			var line: String = esc.call(modlib.mod_to_string(m))
			lines.append(line if plain.has(m) else "[color=#b4b4b4]%s[/color]" % line)
	return "\n".join(lines)


## Game.js updateMods: a game is joinable with our mods when they match its
## (Mod.cpp AreModsPlayCompatible). A game from a host that sends no mods
## (an older RTS0) counts as compatible.
static func game_compatible(room: Dictionary) -> bool:
	if not room.has("mods"):
		return true
	var mods: Array = room["mods"] if room["mods"] is Array else []
	var modlib: GDScript = load("res://scenes/menu/mods.gd")
	return modlib.play_compatible(mods, modlib.engine_mods())


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
	var lower := func(t: String) -> String: return t.substr(0, 1).to_lower() + t.substr(1)
	# Per-class and per-resource counters, in the first player's key order as 0.28 does.
	var nested := func(group: String, key: String) -> String:
		var out := ""
		for p in players:
			out += str(((p.get("stats", {}) as Dictionary).get(group, {}) as Dictionary).get(key, 0)) + ","
		return out
	var first: Dictionary = players[0].get("stats", {})
	var extra := {}
	# Resources.js: <res>Gathered/Used/Sold/Bought and vegetarianFoodGathered.
	for action in ["Gathered", "Used", "Sold", "Bought"]:
		for res in ["food", "wood", "stone", "metal"]:
			extra[res + action] = nested.call("resources" + action, res)
	extra["vegetarianFoodGathered"] = nested.call("resourcesGathered", "vegetarianFood")
	# Units.js and Buildings.js: every class the tracker counts.
	for c in (first.get("unitsTrained", {}) as Dictionary).keys():
		extra[lower.call(str(c)) + "UnitsTrained"] = nested.call("unitsTrained", c)
	for c in (first.get("unitsLost", {}) as Dictionary).keys():
		extra[lower.call(str(c)) + "UnitsLost"] = nested.call("unitsLost", c)
	for c in (first.get("enemyUnitsKilled", {}) as Dictionary).keys():
		extra["enemy" + str(c) + "UnitsKilled"] = nested.call("enemyUnitsKilled", c)
	for c in (first.get("buildingsConstructed", {}) as Dictionary).keys():
		extra[lower.call(str(c)) + "BuildingsConstructed"] = nested.call("buildingsConstructed", c)
	for c in (first.get("buildingsLost", {}) as Dictionary).keys():
		extra[lower.call(str(c)) + "BuildingsLost"] = nested.call("buildingsLost", c)
	for c in (first.get("enemyBuildingsDestroyed", {}) as Dictionary).keys():
		extra["enemy" + str(c) + "BuildingsDestroyed"] = nested.call("enemyBuildingsDestroyed", c)
	var report := {
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
		# Misc.js percentMapExplored; Players.js teamsLocked.
		"percentMapExplored": join.call("percentMapExplored"),
		"teamsLocked": "true" if bool(stats.get("lock_teams", false)) else "false",
	}
	report.merge(extra)
	return report


## gamedescription.js g_Buddies: lobby.buddies, comma separated.
static func buddies() -> PackedStringArray:
	var out := PackedStringArray()
	for n in RtsoUserConfig.new().get_value("lobby.buddies").split(","):
		if n != "":
			out.append(n)
	return out


# PlayerColor.ColorPlayerName: "name (rating)" once the player has a rating.
static func rated_name(name: String, rating) -> String:
	return "%s (%s)" % [name, str(rating)] if rating != null and str(rating) != "" else name


# ProfilePanel.FormatWinRate.
static func format_win_rate(profile: Dictionary) -> String:
	var games := int(profile.get("totalGamesPlayed", 0))
	if games <= 0:
		return "-"
	return "%.2f%%" % (float(profile.get("wins", 0)) / games * 100.0)
