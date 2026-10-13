extends Control
# The multiplayer lobby, after 0 A.D. gui/prelobby (sign in) and gui/lobby: the
# players online with their status, the lobby chat, and the list of games hosts
# have registered (name, host, map, players, password, state). Hosting registers
# a game and starts the match as host; joining picks a game (or types an
# address) and starts the match as a client, as an observer for a game already
# running. The match itself is the in-game lobby panel of main_node, which the
# MatchSettings net_* keys hand the choice to. The server is
# tools/lobby_server.py; LobbySession keeps the sign-in alive across scenes.

const REFRESH_S := 3.0

var _server: LineEdit
var _name: LineEdit
var _sign_btn: Button
var _games: Tree
var _players: ItemList
var _chat_log: RichTextLabel
var _chat_line: LineEdit
var _chat_send: Button
var _join_ip: LineEdit
var _join_port: LineEdit
var _join_pwd: LineEdit
var _observe: CheckBox
var _host_name: LineEdit
var _host_port: LineEdit
var _host_pwd: LineEdit
var _status: Label
var _timer: Timer
var _chat_last := 0
var _rooms: Array = []


func _ready() -> void:
	_build()
	_timer = Timer.new()
	_timer.wait_time = REFRESH_S
	_timer.timeout.connect(_refresh)
	add_child(_timer)
	_timer.start()
	_server.text = LobbySession.address
	_name.text = LobbySession.player_name if LobbySession.signed_in() else MatchSettings.net_player_name
	_update_signed_in()
	_refresh()


func _build() -> void:
	var bg := ColorRect.new()
	bg.color = Color(0.09, 0.09, 0.15, 1)
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)
	var root := VBoxContainer.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.offset_left = 24
	root.offset_top = 24
	root.offset_right = -24
	root.offset_bottom = -24
	add_child(root)

	var header := HBoxContainer.new()
	root.add_child(header)
	var title := Label.new()
	title.text = "Multiplayer Lobby"
	title.add_theme_font_size_override("font_size", 22)
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	header.add_child(title)
	header.add_child(_button("Main Menu", _on_back_pressed))

	var login := HBoxContainer.new()
	root.add_child(login)
	login.add_child(_label("Lobby server"))
	_server = _line("127.0.0.1:8088", 180)
	login.add_child(_server)
	login.add_child(_label("Player name"))
	_name = _line("Player", 180)
	login.add_child(_name)
	_sign_btn = _button("Sign In", _on_sign_pressed)
	login.add_child(_sign_btn)

	var body := HBoxContainer.new()
	body.size_flags_vertical = Control.SIZE_EXPAND_FILL
	root.add_child(body)

	var left := VBoxContainer.new()
	left.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	left.size_flags_stretch_ratio = 2.0
	body.add_child(left)
	left.add_child(_label("Games"))
	_games = Tree.new()
	_games.columns = 5
	_games.hide_root = true
	_games.column_titles_visible = true
	for i in 5:
		_games.set_column_title(i, ["Name", "Host", "Map", "Players", "Status"][i])
	_games.set_column_expand_ratio(0, 3)
	_games.set_column_expand_ratio(2, 2)
	_games.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_games.item_selected.connect(_on_game_selected)
	_games.item_activated.connect(_on_join_pressed)
	left.add_child(_games)

	var join := HBoxContainer.new()
	left.add_child(join)
	join.add_child(_label("Address"))
	_join_ip = _line("127.0.0.1", 140)
	join.add_child(_join_ip)
	_join_port = _line("20595", 70)
	join.add_child(_join_port)
	_join_pwd = _line("", 110)
	_join_pwd.placeholder_text = "Password"
	_join_pwd.secret = true
	join.add_child(_join_pwd)
	_observe = CheckBox.new()
	_observe.text = "Observe"
	join.add_child(_observe)
	join.add_child(_button("Join Game", _on_join_pressed))

	var host := HBoxContainer.new()
	left.add_child(host)
	host.add_child(_label("Game name"))
	_host_name = _line("", 180)
	_host_name.placeholder_text = "My game"
	host.add_child(_host_name)
	_host_port = _line("20595", 70)
	host.add_child(_host_port)
	_host_pwd = _line("", 110)
	_host_pwd.placeholder_text = "Password"
	_host_pwd.secret = true
	host.add_child(_host_pwd)
	host.add_child(_button("Host Game", _on_host_pressed))

	var right := VBoxContainer.new()
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	body.add_child(right)
	right.add_child(_label("Players online"))
	_players = ItemList.new()
	_players.custom_minimum_size = Vector2(0, 140)
	right.add_child(_players)
	right.add_child(_label("Chat"))
	_chat_log = RichTextLabel.new()
	_chat_log.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_chat_log.scroll_following = true
	_chat_log.bbcode_enabled = true
	right.add_child(_chat_log)
	var chat_row := HBoxContainer.new()
	right.add_child(chat_row)
	_chat_line = _line("", 0)
	_chat_line.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_chat_line.placeholder_text = "Chat"
	_chat_line.text_submitted.connect(func(_t: String) -> void: _on_send_pressed())
	chat_row.add_child(_chat_line)
	_chat_send = _button("Send", _on_send_pressed)
	chat_row.add_child(_chat_send)

	_status = Label.new()
	_status.add_theme_color_override("font_color", Color(0.8, 0.9, 0.8, 1))
	root.add_child(_status)


func _label(text: String) -> Label:
	var l := Label.new()
	l.text = text
	return l


func _line(text: String, width: float) -> LineEdit:
	var le := LineEdit.new()
	le.text = text
	le.custom_minimum_size = Vector2(width, 0)
	MatchSettings.prepare_line_edit(le)
	return le


func _button(text: String, cb: Callable) -> Button:
	var b := Button.new()
	b.text = text
	b.pressed.connect(cb)
	return b


func _say(text: String) -> void:
	_status.text = text


func _update_signed_in() -> void:
	var on := LobbySession.signed_in()
	_sign_btn.text = "Sign Out" if on else "Sign In"
	_server.editable = not on
	_name.editable = not on
	_chat_line.editable = on
	_chat_send.disabled = not on
	if not on:
		_players.clear()
		_games.clear()
		_rooms = []
	_say(("Signed in as %s." % LobbySession.player_name) if on
		else "Sign in to see the games and players, or join an address directly.")


func _on_back_pressed() -> void:
	MatchSettings.go("res://scenes/menu/main_menu.tscn")


func _on_sign_pressed() -> void:
	if LobbySession.signed_in():
		LobbySession.sign_out()
		_update_signed_in()
		return
	var who := _name.text.strip_edges()
	if who == "":
		_say("Type a player name first.")
		return
	_sign_btn.disabled = true
	_say("Signing in...")
	LobbySession.sign_in(_server.text.strip_edges(), who, func(code: int, obj) -> void:
		_sign_btn.disabled = false
		if code == 200:
			MatchSettings.net_player_name = LobbySession.player_name
			_chat_last = 0
			_chat_log.clear()
			_update_signed_in()
			_refresh()
		elif code == 0:
			_say("The lobby server at %s does not answer." % _server.text)
		else:
			_say("Sign in refused: %s" % (obj.get("error", code) if obj is Dictionary else str(code))))


func _refresh() -> void:
	if not LobbySession.signed_in():
		return
	LobbySession.get_json("/rooms", _on_rooms)
	LobbySession.get_json("/players", _on_players)
	LobbySession.get_json("/chat?since=%d" % _chat_last, _on_chat)


func _on_rooms(code: int, obj) -> void:
	if code != 200 or not (obj is Dictionary):
		return
	var keep := ""
	var sel := _games.get_selected()
	if sel:
		keep = str(sel.get_metadata(0))
	_rooms = obj.get("rooms", [])
	_games.clear()
	var top := _games.create_item()
	for r in _rooms:
		var it := _games.create_item(top)
		var title := str(r.get("name", "")) if str(r.get("name", "")) != "" else str(r.get("id", ""))
		it.set_text(0, title + (" (password)" if int(r.get("password", 0)) else ""))
		it.set_text(1, str(r.get("host_name", "")))
		it.set_text(2, str(r.get("map_name", "")) if str(r.get("map_name", "")) != "" else str(r.get("map_id", "")))
		it.set_text(3, "%d/%d" % [int(r.get("players", 1)), int(r.get("max_players", 8))])
		it.set_text(4, "Running" if r.get("state", "init") == "running" else "Waiting")
		it.set_metadata(0, str(r.get("id", "")))
		if str(r.get("id", "")) == keep:
			it.select(0)


func _on_players(code: int, obj) -> void:
	if code != 200 or not (obj is Dictionary):
		if code == 0:
			_say("Lost the lobby server at %s." % LobbySession.address)
		return
	_players.clear()
	for p in obj.get("players", []):
		var st := str(p.get("status", "available"))
		var i := _players.add_item("%s  (%s)" % [p.get("name", ""), {"available": "online", "away": "away", "playing": "in a game"}.get(st, st)])
		_players.set_item_custom_fg_color(i, {"available": Color(0.6, 0.9, 0.6), "away": Color(0.9, 0.8, 0.4), "playing": Color(0.9, 0.5, 0.5)}.get(st, Color.WHITE))


func _on_chat(code: int, obj) -> void:
	if code != 200 or not (obj is Dictionary):
		return
	for m in obj.get("messages", []):
		var who := str(m.get("from", ""))
		var text := str(m.get("text", "")).replace("[", "[lb]")
		if who == "":
			_chat_log.append_text("[color=#aaaaaa]%s[/color]\n" % text)
		else:
			_chat_log.append_text("[color=#e0c070]%s:[/color] %s\n" % [who.replace("[", "[lb]"), text])
	_chat_last = int(obj.get("last", _chat_last))


func _on_send_pressed() -> void:
	var text := _chat_line.text.strip_edges()
	if text == "" or not LobbySession.signed_in():
		return
	_chat_line.text = ""
	LobbySession.post_json("/chat", {"token": LobbySession.token, "text": text}, func(_c: int, _o) -> void: _refresh())


func _selected_room() -> Dictionary:
	var sel := _games.get_selected()
	if sel == null:
		return {}
	for r in _rooms:
		if str(r.get("id", "")) == str(sel.get_metadata(0)):
			return r
	return {}


func _on_game_selected() -> void:
	var r := _selected_room()
	if r.is_empty():
		return
	_join_ip.text = str(r.get("host_ip", "127.0.0.1"))
	_join_port.text = str(int(r.get("host_port", 20595)))
	# 0 A.D. lets a running game be joined only to watch it.
	_observe.button_pressed = r.get("state", "init") == "running"
	_say("Selected %s%s." % [str(r.get("name", r.get("id", ""))),
		", which needs a password" if int(r.get("password", 0)) else ""])


func _player_name() -> String:
	var who := LobbySession.player_name if LobbySession.signed_in() else _name.text.strip_edges()
	return who if who != "" else "Player"


func _on_join_pressed() -> void:
	var port := _join_port.text.to_int()
	if _join_ip.text.strip_edges() == "" or port <= 0 or port > 65535:
		_say("Type the host's address and port, or pick a game.")
		return
	MatchSettings.net_mode = "join"
	MatchSettings.net_lobby = LobbySession.address
	MatchSettings.net_player_name = _player_name()
	MatchSettings.net_ip = _join_ip.text.strip_edges()
	MatchSettings.net_port = port
	MatchSettings.net_password = _join_pwd.text
	MatchSettings.net_observe = _observe.button_pressed
	MatchSettings.go("res://scenes/main.tscn")


func _on_host_pressed() -> void:
	var port := _host_port.text.to_int()
	if port <= 0 or port > 65535:
		_say("Type a port for the game.")
		return
	var who := _player_name()
	MatchSettings.net_mode = "host"
	MatchSettings.net_lobby = LobbySession.address
	MatchSettings.net_player_name = who
	MatchSettings.net_port = port
	MatchSettings.net_password = _host_pwd.text
	MatchSettings.net_observe = false
	if LobbySession.signed_in():
		# The same id main_node registers, so both update one entry.
		LobbySession.host_game({
			"id": "room-%d" % port,
			"host_port": port,
			"name": _host_name.text.strip_edges() if _host_name.text.strip_edges() != "" else who + "'s game",
			"host_name": who,
			"map_id": MatchSettings.map_id,
			"map_name": MatchSettings.map_id.replace("_", " ").capitalize(),
			"max_players": 8,
			"players": 1,
			"password": _host_pwd.text != "",
			"state": "init",
		})
	MatchSettings.go("res://scenes/main.tscn")
