extends Control
# The multiplayer lobby, after 0 A.D. gui/prelobby (sign in) and gui/lobby: the
# players online with their status, the lobby chat, and the list of games hosts
# have registered (name, host, map, players, password, state). Hosting registers
# a game and starts the match as host; joining picks a game (or types an
# address) and starts the match as a client, as an observer for a game already
# running. The match itself is the in-game lobby panel of main_node, which the
# MatchSettings net_* keys hand the choice to. The server is
# tools/lobby_server.py; LobbySession keeps the sign-in alive across scenes.
# Ratings follow 0 A.D.'s: the player list's Rating column, the profile panel
# under it (LobbyPage/ProfilePanel.js) and the Leaderboard (LeaderboardPage).
# Selecting a game shows its details (LobbyPage/GameDetails.js); Options'
# lobby.columns.gamerating adds the game list's Rating column and filter
# (GameList.js, GameListFilters/Rating.js). The game list filters on map size,
# map type, player count and open games (GameListFilters); Mark as Buddy
# (Buttons/BuddyButton.js) keeps lobby.buddies, and buddies and the games they
# are in carry the "•" mark (gamedescription.js g_BuddySymbol).

const REFRESH_S := 3.0

var _server: LineEdit
var _name: LineEdit
var _sign_btn: Button
var _password: LineEdit
var _remember: CheckBox
var _register_btn: Button
var _games: Tree
var _players: Tree
var _profile: VBoxContainer
var _profile_name: Label
var _profile_values := {}
var _requested_player := "" # ProfilePanel.requestedPlayer
var _selected_player := "" # the player list's selection
var _leaderboard: AcceptDialog
var _board: Tree
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
var _chat_backlog := true # the first fetch after signing in is the backlog (lobby.history)
var _rooms: Array = []
var _ratings := {} # player name -> rating, from the player list
## lobby.js with attribs.dialog: opened from the game setup's Lobby button
## over the setup, without Host and Join; Back (or Escape) closes it.
var dialog := false
signal closed
var _moderators := {} # names the lobby lists as moderators
var _subject := "" # the lobby room subject (Subject.js)
var _details: RichTextLabel
var _rating_column := false # lobby.columns.gamerating
var _rating_filter: OptionButton
var _shown_rooms: Array = [] # _rooms past the filters
var _open_filter: CheckBox
var _size_filter: OptionButton
var _type_filter: OptionButton
var _count_filter: OptionButton
var _buddy_btn: Button

const BUDDY_SYMBOL := "•"
const MAX_PLAYERS := 8

const GAME_COLUMNS := ["Name", "Host", "Map", "Players", "Status"]
const RATING_COL := 3 # where the Rating column goes when shown
# GameListFilters/Rating.js RatingFilters, after "Any".
const RATING_FILTERS := ["", ">1500", ">1400", ">1300", ">1200", "<1200", "<1100", "<1000"]


func _ready() -> void:
	_build()
	_timer = Timer.new()
	_timer.wait_time = REFRESH_S
	_timer.timeout.connect(_refresh)
	add_child(_timer)
	_timer.start()
	_server.text = LobbySession.address
	_name.text = LobbySession.player_name if LobbySession.signed_in() else multiplayer_name()
	LobbySession.kicked.connect(_on_kicked)
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
	header.add_child(_button("Leaderboard", _on_leaderboard_pressed))
	header.add_child(_button("Back" if dialog else "Main Menu", _on_back_pressed))

	var login := HBoxContainer.new()
	login.visible = not dialog
	root.add_child(login)
	login.add_child(_label("Lobby server"))
	_server = _line("127.0.0.1:8088", 180)
	login.add_child(_server)
	login.add_child(_label("Player name"))
	var cfg := RtsoUserConfig.new()
	_name = _line(cfg.get_value("lobby.login") if cfg.get_value("lobby.login") != "" else "Player", 180)
	login.add_child(_name)
	# 0 A.D. prelobby Login / Register: an account's password; empty signs in
	# as a guest where the lobby allows it.
	login.add_child(_label("Password"))
	_password = _line("", 140)
	_password.secret = true
	login.add_child(_password)
	_remember = CheckBox.new()
	_remember.text = "Remember password"
	_remember.button_pressed = cfg.get_bool("lobby.rememberpassword")
	login.add_child(_remember)
	_sign_btn = _button("Sign In", _on_sign_pressed)
	login.add_child(_sign_btn)
	_register_btn = _button("Register", _on_register_pressed)
	login.add_child(_register_btn)

	var body := HBoxContainer.new()
	body.size_flags_vertical = Control.SIZE_EXPAND_FILL
	root.add_child(body)

	var left := VBoxContainer.new()
	left.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	left.size_flags_stretch_ratio = 2.0
	body.add_child(left)
	_rating_column = RtsoUserConfig.new().get_bool("lobby.columns.gamerating")
	var games_head := HBoxContainer.new()
	left.add_child(games_head)
	var games_title := _label("Games")
	games_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	games_head.add_child(games_title)
	# GameListFilters.xml: open games, map size, map type, player count, rating.
	_open_filter = CheckBox.new()
	_open_filter.text = "Show only open games"
	_open_filter.toggled.connect(func(_on: bool) -> void: _show_rooms())
	games_head.add_child(_open_filter)
	var setup: GDScript = load("res://scenes/menu/gamesetup.gd")
	_size_filter = _filter_dropdown(["Any"] + setup.MAP_SIZES.map(func(sz) -> String: return str(sz[1]).get_slice(" (", 0)),
		"Filter the games by map size.")
	games_head.add_child(_size_filter)
	_type_filter = _filter_dropdown(["Any"] + setup.MAP_TYPES.map(func(t) -> String: return str(t[1])),
		"Filter the games by map type.")
	games_head.add_child(_type_filter)
	var counts := ["Any"]
	for i in MAX_PLAYERS:
		counts.append(str(i + 1))
	_count_filter = _filter_dropdown(counts, "Filter the games by their number of player slots.")
	games_head.add_child(_count_filter)
	_rating_filter = OptionButton.new()
	for f in RATING_FILTERS:
		_rating_filter.add_item("Any" if f == "" else "%s %s" % [f.substr(0, 1), f.substr(1)])
	_rating_filter.tooltip_text = "Filter the games by the average rating of their players."
	_rating_filter.visible = _rating_column
	_rating_filter.item_selected.connect(func(_i: int) -> void: _show_rooms())
	games_head.add_child(_rating_filter)
	var titles := game_columns(_rating_column)
	_games = Tree.new()
	_games.columns = titles.size()
	_games.hide_root = true
	_games.column_titles_visible = true
	for i in titles.size():
		_games.set_column_title(i, titles[i])
	_games.set_column_expand_ratio(0, 3)
	_games.set_column_expand_ratio(2, 2)
	_games.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_games.item_selected.connect(_on_game_selected)
	if not dialog:
		_games.item_activated.connect(_on_join_pressed)
	left.add_child(_games)
	_details = RichTextLabel.new()
	_details.bbcode_enabled = true
	_details.fit_content = true
	_details.custom_minimum_size = Vector2(0, 60)
	_details.visible = false
	left.add_child(_details)

	var join := HBoxContainer.new()
	join.visible = not dialog
	left.add_child(join)
	join.add_child(_label("Address"))
	# gamesetup_mp.xml: the last address and ports, from multiplayerserver,
	# multiplayerjoining.port and multiplayerhosting.port.
	var mp_cfg := RtsoUserConfig.new()
	_join_ip = _line(mp_cfg.get_value("multiplayerserver"), 140)
	join.add_child(_join_ip)
	_join_port = _line(str(_valid_port(mp_cfg.get_value("multiplayerjoining.port"))), 70)
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
	host.visible = not dialog
	left.add_child(host)
	host.add_child(_label("Game name"))
	_host_name = _line("", 180)
	_host_name.placeholder_text = "My game"
	host.add_child(_host_name)
	_host_port = _line(str(_valid_port(mp_cfg.get_value("multiplayerhosting.port"))), 70)
	host.add_child(_host_port)
	_host_pwd = _line("", 110)
	_host_pwd.placeholder_text = "Password"
	_host_pwd.secret = true
	host.add_child(_host_pwd)
	host.add_child(_button("Host Game", _on_host_pressed))
	# LobbyPage hostSavedGameButton: pick a save, then host it.
	var saved_btn := _button("Host Saved Game", _on_host_saved_pressed)
	saved_btn.tooltip_text = "Continue playing a game from a savegame."
	host.add_child(saved_btn)

	var right := VBoxContainer.new()
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	body.add_child(right)
	right.add_child(_label("Players online"))
	_players = Tree.new()
	_players.columns = 3
	_players.hide_root = true
	_players.column_titles_visible = true
	for i in 3:
		_players.set_column_title(i, ["Status", "Name", "Rating"][i])
	_players.set_column_expand_ratio(1, 2)
	_players.custom_minimum_size = Vector2(0, 140)
	_players.item_selected.connect(func() -> void:
		var it := _players.get_selected()
		var picked := str(it.get_metadata(0)) if it else ""
		if picked == _selected_player:
			return # the refresh re-selecting it
		_selected_player = picked
		_update_buddy_button()
		_request_profile(_selected_player))
	right.add_child(_players)
	_buddy_btn = _button("Mark as Buddy", _on_buddy_pressed)
	_buddy_btn.disabled = true
	right.add_child(_buddy_btn)
	_players.item_activated.connect(_on_buddy_pressed) # PlayerList double click
	_profile = _build_profile()
	right.add_child(_profile)
	_leaderboard = _build_leaderboard()
	add_child(_leaderboard)
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


# LobbyPage/ProfilePanel.xml: the selected player's name and rating, then the
# rank, highest rating, games, wins, losses and win rate from the lobby.
func _build_profile() -> VBoxContainer:
	var box := VBoxContainer.new()
	box.visible = false
	_profile_name = _label("")
	_profile_name.add_theme_font_size_override("font_size", 16)
	box.add_child(_profile_name)
	var grid := GridContainer.new()
	grid.columns = 2
	box.add_child(grid)
	for row in [["rank", "Current Rank:"], ["highestRating", "Highest Rating:"], ["totalGamesPlayed", "Total Games:"],
			["wins", "Wins:"], ["losses", "Losses:"], ["ratio", "Win Rate:"]]:
		grid.add_child(_label(row[1]))
		var v := _label(NOT_AVAILABLE)
		grid.add_child(v)
		_profile_values[row[0]] = v
	return box


# LeaderboardPage: rank, name and rating of the best players; selecting one
# shows its profile, closing goes back to the player list's selection.
func _build_leaderboard() -> AcceptDialog:
	var dlg := AcceptDialog.new()
	dlg.title = "Leaderboard"
	dlg.ok_button_text = "Back"
	dlg.min_size = Vector2i(420, 420)
	_board = Tree.new()
	_board.columns = 3
	_board.hide_root = true
	_board.column_titles_visible = true
	for i in 3:
		_board.set_column_title(i, ["Rank", "Name", "Rating"][i])
	_board.set_column_expand_ratio(1, 3)
	_board.item_selected.connect(func() -> void:
		var it := _board.get_selected()
		if it:
			_request_profile(str(it.get_metadata(0))))
	dlg.add_child(_board)
	dlg.confirmed.connect(func() -> void: _request_profile(_selected_player))
	dlg.canceled.connect(func() -> void: _request_profile(_selected_player))
	return dlg


const NOT_AVAILABLE := "N/A"


func _request_profile(player: String) -> void:
	_requested_player = player
	_profile.visible = player != "" or _profile_name.text != ""
	if player == "" or not LobbySession.signed_in():
		return
	_profile_name.text = player
	for k in _profile_values:
		_profile_values[k].text = NOT_AVAILABLE
	LobbySession.get_json("/profile?name=" + player.uri_encode(), _on_profile)


func _on_profile(code: int, obj) -> void:
	if code != 200 or not (obj is Dictionary) or str(obj.get("name", "")).to_lower() != _requested_player.to_lower():
		return
	_profile_name.text = LobbySession.rated_name(str(obj.get("name", "")), obj.get("rating"))
	for k in ["rank", "highestRating", "totalGamesPlayed", "wins", "losses"]:
		_profile_values[k].text = str(obj.get(k, NOT_AVAILABLE))
	_profile_values["ratio"].text = LobbySession.format_win_rate(obj)


func _on_leaderboard_pressed() -> void:
	if not LobbySession.signed_in():
		_say("Sign in to see the leaderboard.")
		return
	_board.clear()
	_leaderboard.popup_centered()
	LobbySession.get_json("/leaderboard", _on_leaderboard)


func _on_leaderboard(code: int, obj) -> void:
	if code != 200 or not (obj is Dictionary):
		return
	_board.clear()
	var top := _board.create_item()
	var rank := 0
	for p in obj.get("players", []):
		rank += 1
		var it := _board.create_item(top)
		it.set_text(0, str(rank))
		it.set_text(1, str(p.get("name", "")))
		it.set_text(2, str(p.get("rating", "")))
		it.set_metadata(0, str(p.get("name", "")))


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
	_password.editable = not on
	_register_btn.disabled = on
	_chat_line.editable = on
	_chat_send.disabled = not on
	if not on:
		_players.clear()
		_games.clear()
		_rooms = []
		_shown_rooms = []
		_details.visible = false
	_update_buddy_button()
	_say(("Signed in as %s." % LobbySession.player_name) if on
		else "Sign in to see the games and players, or join an address directly.")


func _on_back_pressed() -> void:
	if dialog:
		# QuitButton.closeDialog: back to the game setup.
		closed.emit()
		queue_free()
		return
	MatchSettings.go("res://scenes/menu/main_menu.tscn")


func _unhandled_input(event: InputEvent) -> void:
	if dialog and event.is_action_pressed("ui_cancel"):
		get_viewport().set_input_as_handled()
		_on_back_pressed()


func _on_sign_pressed() -> void:
	if LobbySession.signed_in():
		LobbySession.sign_out()
		_update_signed_in()
		return
	var who := _name.text.strip_edges()
	if who == "":
		_say("Type a player name first.")
		return
	_sign_in(who)


# The password typed now, else the one lobby.password remembers for this name.
func _encrypted_password(who: String) -> String:
	if _password.text != "":
		return LobbySession.encrypt_password(_password.text, who)
	var cfg := RtsoUserConfig.new()
	if _remember.button_pressed and cfg.get_value("lobby.login").to_lower() == who.to_lower():
		return cfg.get_value("lobby.password")
	return ""


func _sign_in(who: String) -> void:
	_sign_btn.disabled = true
	_say("Signing in...")
	var encrypted := _encrypted_password(who)
	var done := func(code: int, obj) -> void:
		_sign_btn.disabled = false
		if code == 200:
			# 0 A.D. prelobby: lobby.login, and lobby.password (encrypted) when remembered.
			var cfg := RtsoUserConfig.new()
			cfg.set_value("lobby.login", LobbySession.player_name)
			cfg.set_value("lobby.rememberpassword", "true" if _remember.button_pressed else "false")
			cfg.set_value("lobby.password", encrypted if _remember.button_pressed else "")
			cfg.save()
			_password.text = ""
			MatchSettings.net_player_name = LobbySession.player_name
			_chat_last = 0
			_chat_backlog = true
			_chat_log.clear()
			_update_signed_in()
			_refresh()
		elif code == 0:
			_say("The lobby server at %s does not answer." % _server.text)
		else:
			_say("Sign in refused: %s" % (obj.get("error", code) if obj is Dictionary else str(code)))
	LobbySession.sign_in(_server.text.strip_edges(), who, done, encrypted)


func _on_register_pressed() -> void:
	var who := _name.text.strip_edges()
	# One field here, so the password is its own confirmation; 0 A.D. asks twice.
	var problem := LobbySession.register_problem(who, _password.text, _password.text)
	if problem != "":
		_say(problem)
		return
	_register_btn.disabled = true
	_say("Registering...")
	LobbySession.register(_server.text.strip_edges(), who, _password.text, func(code: int, obj) -> void:
		_register_btn.disabled = false
		if code == 200:
			_say("Registered %s; signing in..." % who)
			_sign_in(who)
		elif code == 0:
			_say("The lobby server at %s does not answer." % _server.text)
		else:
			_say("Registration failed: %s" % (obj.get("error", code) if obj is Dictionary else str(code))))


func _refresh() -> void:
	if not LobbySession.signed_in():
		return
	LobbySession.get_json("/rooms", _on_rooms)
	LobbySession.get_json("/players", _on_players)
	LobbySession.get_json("/chat?since=%d" % _chat_last, _on_chat)


func _on_rooms(code: int, obj) -> void:
	if code != 200 or not (obj is Dictionary):
		return
	_rooms = obj.get("rooms", [])
	_show_rooms()


## The game list's columns; Rating (GameList.js gameRating) when
## lobby.columns.gamerating is on.
static func game_columns(rating: bool) -> Array:
	var cols: Array = GAME_COLUMNS.duplicate()
	if rating:
		cols.insert(RATING_COL, "Rating")
	return cols


## GameListFilters/Rating.js: "" passes every game, ">N" a game rated at least
## N, "<N" one rated below N.
static func rating_passes(filter: String, rating: int) -> bool:
	if filter == "":
		return true
	var n := filter.substr(1).to_int()
	return rating >= n if filter.begins_with(">") else rating < n


func _room_rating(r: Dictionary) -> int:
	return LobbySession.game_rating(r.get("player_names", []), _ratings)


func _show_rooms() -> void:
	var keep := ""
	var sel := _games.get_selected()
	if sel:
		keep = str(sel.get_metadata(0))
	var filter: String = RATING_FILTERS[_rating_filter.selected] if _rating_column and _rating_filter.selected >= 0 else ""
	var f := _filters()
	_shown_rooms = _rooms.filter(func(r) -> bool:
		return rating_passes(filter, _room_rating(r)) and room_passes(r, f))
	var buddy_list := buddies()
	var col := func(name: String) -> int: return game_columns(_rating_column).find(name)
	_games.clear()
	var top := _games.create_item()
	var reselected := false
	for r in _shown_rooms:
		var it := _games.create_item(top)
		var title := str(r.get("name", "")) if str(r.get("name", "")) != "" else str(r.get("id", ""))
		it.set_text(0, (BUDDY_SYMBOL + " " if room_buddies(r, buddy_list) else "") + title
			+ (" (password)" if int(r.get("password", 0)) else ""))
		it.set_text(1, str(r.get("host_name", "")))
		it.set_text(2, str(r.get("map_name", "")) if str(r.get("map_name", "")) != "" else str(r.get("map_id", "")))
		if _rating_column:
			it.set_text(col.call("Rating"), str(_room_rating(r)))
		it.set_text(col.call("Players"), "%d/%d" % [int(r.get("players", 1)), int(r.get("max_players", 8))])
		it.set_text(col.call("Status"), "Running" if r.get("state", "init") == "running" else "Waiting")
		it.set_metadata(0, str(r.get("id", "")))
		if str(r.get("id", "")) == keep:
			it.select(0)
			reselected = true
	if reselected:
		_show_details(_selected_room())
	else:
		_show_details({})


func _filter_dropdown(items: Array, tip: String) -> OptionButton:
	var o := OptionButton.new()
	for t in items:
		o.add_item(str(t))
	o.tooltip_text = tip
	o.item_selected.connect(func(_i: int) -> void: _show_rooms())
	return o


func _filters() -> Dictionary:
	var setup: GDScript = load("res://scenes/menu/gamesetup.gd")
	var size_i := _size_filter.selected - 1
	var type_i := _type_filter.selected - 1
	return {"open": _open_filter.button_pressed,
		"map_size": str(setup.MAP_SIZES[size_i][1]).get_slice(" (", 0) if size_i >= 0 else "",
		"map_type": str(setup.MAP_TYPES[type_i][0]) if type_i >= 0 else "",
		"players": _count_filter.selected if _count_filter.selected > 0 else 0}


## GameListFilters: MapSize and MapType match the game's, PlayerCount its
## slots (maxnbp), OpenGame one still in setup with a free slot.
static func room_passes(r: Dictionary, f: Dictionary) -> bool:
	if f.get("open", false) and not (str(r.get("state", "init")) == "init"
			and int(r.get("players", 1)) < int(r.get("max_players", MAX_PLAYERS))):
		return false
	if str(f.get("map_size", "")) != "" and str(r.get("map_size", "")) != str(f["map_size"]):
		return false
	if str(f.get("map_type", "")) != "" and str(r.get("map_type", "")) != str(f["map_type"]):
		return false
	return int(f.get("players", 0)) == 0 or int(r.get("max_players", MAX_PLAYERS)) == int(f["players"])


## gamedescription.js g_Buddies (kept in lobby_session.gd for the game details).
static func buddies() -> PackedStringArray:
	return preload("res://scenes/menu/lobby_session.gd").buddies()


## Game.js hasBuddies: 2 a buddy plays in it, 1 a buddy only watches, 0 none.
static func room_buddies(r: Dictionary, list: PackedStringArray) -> int:
	var found := 0
	for p in r.get("player_names", []):
		if list.has(str(p.get("name", ""))) and not p.get("ai", false):
			found = maxi(found, 1 if p.get("observer", false) else 2)
	return found


## BuddyButton.onPress: mark or unmark a player; never oneself, nor a name
## holding the list's delimiter. An empty list is saved as "," (0.28 too).
static func toggle_buddy(name: String, own: String) -> bool:
	if name == "" or name == own or name.contains(","):
		return false
	var list := buddies()
	if list.has(name):
		list.remove_at(list.find(name))
	else:
		list.append(name)
	var cfg := RtsoUserConfig.new()
	cfg.set_value("lobby.buddies", ",".join(list) if not list.is_empty() else ",")
	cfg.save()
	return true


func _update_buddy_button() -> void:
	_buddy_btn.text = "Unmark as Buddy" if buddies().has(_selected_player) else "Mark as Buddy"
	_buddy_btn.disabled = not LobbySession.signed_in() or _selected_player == "" \
		or _selected_player == LobbySession.player_name


func _on_buddy_pressed() -> void:
	if not LobbySession.signed_in() or not toggle_buddy(_selected_player, LobbySession.player_name):
		return
	_update_buddy_button()
	LobbySession.get_json("/players", _on_players) # the marks follow
	_show_rooms()


func _show_details(r: Dictionary) -> void:
	# Subject.js: the room subject shows while no game is selected.
	_details.visible = not r.is_empty() or (_subject != "" and LobbySession.signed_in())
	if not r.is_empty():
		_details.text = LobbySession.game_details(r, _ratings)
	elif _details.visible:
		_details.text = _subject.replace("[", "[lb]")


## KickStrings.Local: a moderator removed us from the lobby.
func _on_kicked(text: String) -> void:
	_system_line(text.replace("[", "[lb]"))
	_say(text)
	_update_signed_in()


func _on_players(code: int, obj) -> void:
	if code != 200 or not (obj is Dictionary):
		if code == 0:
			_say("Lost the lobby server at %s." % LobbySession.address)
		return
	_players.clear()
	_ratings = {}
	_moderators = {}
	var buddy_list := buddies()
	var top := _players.create_item()
	for p in obj.get("players", []):
		if p.get("rating") != null:
			_ratings[str(p.get("name", ""))] = p["rating"]
		var st := str(p.get("status", "available"))
		if p.get("moderator", false):
			_moderators[str(p.get("name", ""))] = true
		var it := _players.create_item(top)
		var col: Color = {"available": Color(0.6, 0.9, 0.6), "away": Color(0.9, 0.8, 0.4), "playing": Color(0.9, 0.5, 0.5)}.get(st, Color.WHITE)
		# PlayerList.js presences: Online, Away, Busy (in a game).
		it.set_text(0, {"available": "Online", "away": "Away", "playing": "Busy"}.get(st, st))
		it.set_custom_color(0, col)
		it.set_text(1, (BUDDY_SYMBOL + " " if buddy_list.has(str(p.get("name", ""))) else "") + _nick(str(p.get("name", ""))))
		it.set_text(2, str(p.get("rating", "")))
		it.set_metadata(0, str(p.get("name", "")))
		if str(p.get("name", "")) == _selected_player:
			it.select(0)
	LobbySession.moderator = _moderators.has(LobbySession.player_name)
	var subject := str(obj.get("subject", ""))
	if subject != _subject:
		_subject = subject
		_show_details(_selected_room())
	if _rating_column or _details.visible:
		_show_rooms() # the games' ratings follow the players'


func _on_chat(code: int, obj) -> void:
	if code != 200 or not (obj is Dictionary):
		return
	var messages: Array = obj.get("messages", [])
	var backlog := _chat_backlog
	_chat_backlog = false
	if backlog:
		# 0 A.D.'s lobby.history: how many past messages to load when joining.
		var keep := maxi(0, int(RtsoUserConfig.new().get_number("lobby.history")))
		messages = messages.slice(maxi(0, messages.size() - keep))
	var me := LobbySession.player_name
	var notify := not backlog and me != "" and RtsoUserConfig.new().get_bool("sound.notify.nick")
	for m in messages:
		var who := str(m.get("from", ""))
		if notify and who != "" and who != me and str(m.get("text", "")).findn(me) >= 0:
			notify = false
			_play_nick_alert()
		var raw := str(m.get("text", ""))
		var text := raw.replace("[", "[lb]")
		var when := _chat_time(m)
		if who == "":
			_chat_log.append_text("%s[color=#aaaaaa]%s[/color]\n" % [when, text])
		elif raw.begins_with("/me "):
			# ChatMessageFormatMe: "* %(sender)s %(message)s".
			_chat_log.append_text("%s* [b]%s[/b] %s\n" % [when, _nick(who).replace("[", "[lb]"), text.substr(4)])
		else:
			if raw.begins_with("/say "):
				text = text.substr(5)
			_chat_log.append_text("%s[color=#e0c070]%s:[/color] %s\n" % [when, _nick(who).replace("[", "[lb]"), text])
	_chat_last = int(obj.get("last", _chat_last))


func _play_nick_alert() -> void:
	var p := AudioStreamPlayer.new()
	p.stream = load("res://assets/audio/interface/ui/chat_alert.ogg")
	add_child(p)
	p.finished.connect(p.queue_free)
	p.play()


## TimestampWrapper.js: "[HH:MM] " before a line when chat.timestamp is on.
func _chat_time(m: Dictionary) -> String:
	if not RtsoUserConfig.new().get_bool("chat.timestamp") or not m.has("time"):
		return ""
	var t := Time.get_datetime_dict_from_unix_time(int(m["time"]) + int(Time.get_time_zone_from_system().get("bias", 0)) * 60)
	return "[color=#aaaaaa][lb]%02d:%02d] [/color]" % [int(t["hour"]), int(t["minute"])]

## PlayerColor.ColorPlayerName: "@" before a moderator's name.
func _nick(name: String) -> String:
	return ("@" + name) if _moderators.has(name) else name

## 0.28 ChatCommandHandler.ChatCommands, in their order; the moderator-only
## ones (kick, ban) work for moderators (tools/lobby_server.py --moderators).
const CHAT_COMMANDS := [
	["away", "Set your state to 'Away'."],
	["back", "Set your state to 'Online'."],
	["kick", "Kick a specified user from the lobby. Usage: /kick nick reason", true],
	["ban", "Ban a specified user from the lobby. Usage: /ban nick reason", true],
	["help", "Show this help."],
	["me", "Send a chat message about yourself. Example: /me goes swimming."],
	["say", "Send text as a chat message (even if it starts with slash). Example: /say /help is a great command."],
	["clear", "Clear all chat scrollback."],
]

func _system_line(text: String) -> void:
	_chat_log.append_text("[color=#aaaaaa]%s[/color]\n" % text)

## handleChatCommand: true when the input was a command, run here and not sent.
func _chat_command(text: String) -> bool:
	if not text.begins_with("/"):
		return false
	var i := text.find(" ")
	var cmd := text.substr(1, i - 1 if i >= 0 else -1)
	var args := text.substr(i + 1).strip_edges() if i >= 0 else ""
	var entry: Array = []
	for c in CHAT_COMMANDS:
		if c[0] == cmd:
			entry = c
	var tag := func(s: String) -> String: return "[color=#c8c8ff]%s[/color]" % s.replace("[", "[lb]")
	if entry.is_empty():
		_system_line("The command '%s' is not supported. Try %s to get more information about the supported commands." % [tag.call(cmd), tag.call("/help")])
		return true
	if entry.size() > 2 and entry[2] and not LobbySession.moderator:
		_system_line("The command '%s' is restricted to moderators." % tag.call(cmd))
		return true
	match cmd:
		"away", "back":
			LobbySession.away = cmd == "away"
			LobbySession.post_json("/presence", {"token": LobbySession.token, "status": "away" if cmd == "away" else "available"},
				func(_c: int, _o) -> void: _refresh())
		"help":
			var txt := "Chat commands:"
			for c in CHAT_COMMANDS:
				if not (c.size() > 2 and c[2]) or LobbySession.moderator:
					txt += "\n%s - %s" % [tag.call(c[0]), c[1]]
			_system_line(txt)
		"me", "say":
			if args == "":
				_system_line("The command '%s' requires at least one argument." % tag.call("/" + cmd))
				return true
			return false
		"clear":
			_chat_log.clear()
		"kick", "ban":
			# Engine.LobbyKick / LobbyBan: the nick, then the reason.
			var j := args.find(" ")
			LobbySession.post_json("/kick", {"token": LobbySession.token, "nick": args.substr(0, j) if j >= 0 else args,
				"reason": args.substr(j + 1) if j >= 0 else "", "ban": cmd == "ban"}, func(_c: int, _o) -> void: _refresh())
	return true

func _on_send_pressed() -> void:
	var text := _chat_line.text.strip_edges()
	if text == "" or not LobbySession.signed_in():
		return
	_chat_line.text = ""
	if _chat_command(text):
		return
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
	_show_details(r)
	if r.is_empty():
		return
	# The game list carries no address (0 A.D.'s neither): Join asks the lobby.
	_join_ip.text = ""
	_join_ip.placeholder_text = "From the lobby"
	# 0 A.D. lets a running game be joined only to watch it.
	_observe.button_pressed = r.get("state", "init") == "running"
	_say("Selected %s%s." % [str(r.get("name", r.get("id", ""))),
		", which needs a password" if int(r.get("password", 0)) else ""])


func _player_name() -> String:
	var who := LobbySession.player_name if LobbySession.signed_in() else _name.text.strip_edges()
	if who == "":
		return "Player"
	# gamesetup_mp.js: the name you join or host with is kept as playername.multiplayer.
	if not LobbySession.signed_in() and who != multiplayer_name():
		var cfg := RtsoUserConfig.new()
		cfg.set_value("playername.multiplayer", who)
		cfg.save()
	return who


## functions_utility.js multiplayerName: Options' "Player name (multiplayer)",
## else the system user name.
static func multiplayer_name() -> String:
	var n := RtsoUserConfig.new().get_value("playername.multiplayer").strip_edges()
	if n.is_empty():
		n = OS.get_environment("USERNAME" if OS.get_name() == "Windows" else "USER").strip_edges()
	return n if not n.is_empty() else "Player"


## gamesetup_mp.js getValidPort: the default port for anything outside 1-65535.
static func _valid_port(text: String) -> int:
	var p := text.strip_edges().to_int()
	return p if text.strip_edges().is_valid_int() and p > 0 and p <= 65535 else 20595


func _on_host_saved_pressed() -> void:
	_on_host_pressed(true)


func _on_join_pressed() -> void:
	var r := _selected_room()
	if _join_ip.text.strip_edges() == "" and not r.is_empty():
		# A listed game: its address comes from the lobby, and only to a
		# signed-in player (0 A.D. XmppClient SendIqGetConnectionData).
		if not LobbySession.signed_in():
			_say("Sign in to join a listed game, or type the host's address.")
			return
		_say("Asking the lobby for the game's address...")
		LobbySession.post_json("/rooms/connect", {"token": LobbySession.token, "id": str(r.get("id", ""))},
			func(code: int, obj) -> void:
				if not is_inside_tree():
					return
				if code != 200 or not obj is Dictionary:
					_say("Could not get the game's address from the lobby.")
					return
				_start_join(str(obj.get("host_ip", "")), int(obj.get("host_port", 0)), str(r.get("id", "")),
						str(obj.get("ticket", ""))))
		return
	_start_join(_join_ip.text.strip_edges(), _join_port.text.to_int(), "")


# A typed address is joined directly; a listed game (room_id) through the
# lobby, with STUN and hole punching (0 A.D. TryToConnectWithSTUN).
# ticket: the lobby's, which a host signed in to the lobby redeems for this
# player's name (a typed address carries none, and such a host refuses it).
func _start_join(ip: String, port: int, room_id: String, ticket := "") -> void:
	if ip == "" or port <= 0 or port > 65535:
		_say("Type the host's address and port, or pick a game.")
		return
	MatchSettings.net_mode = "join"
	MatchSettings.net_lobby = LobbySession.address
	MatchSettings.net_player_name = _player_name()
	MatchSettings.net_ip = ip
	MatchSettings.net_port = port
	MatchSettings.net_password = _join_pwd.text
	MatchSettings.net_observe = _observe.button_pressed
	MatchSettings.net_lobby_ticket = ticket
	var via_lobby := room_id != ""
	if not via_lobby:
		# gamesetup_mp.js startJoin: a direct join keeps its address and port.
		var cfg := RtsoUserConfig.new()
		cfg.set_value("multiplayerserver", MatchSettings.net_ip)
		cfg.set_value("multiplayerjoining.port", str(port))
		cfg.save()
	LobbySession.join_game(room_id)
	MatchSettings.go("res://scenes/main.tscn")


func _on_host_pressed(saved := false) -> void:
	var port := _host_port.text.to_int()
	if port <= 0 or port > 65535:
		_say("Type a port for the game.")
		return
	var who := _player_name()
	var game_name := _host_name.text.strip_edges() if _host_name.text.strip_edges() != "" else who + "'s game"
	# gamesetup_mp.js: a game name another host already lists is refused.
	for r in _rooms:
		if str(r.get("name", "")) == game_name and str(r.get("host_name", "")) != who:
			_say("Game name already in use.")
			return
	MatchSettings.net_mode = "host"
	MatchSettings.net_lobby = LobbySession.address
	MatchSettings.net_player_name = who
	MatchSettings.net_port = port
	MatchSettings.net_password = _host_pwd.text
	MatchSettings.net_observe = false
	# gamesetup_mp.js startHost keeps multiplayerhosting.port.
	var cfg := RtsoUserConfig.new()
	cfg.set_value("multiplayerhosting.port", str(port))
	cfg.save()
	LobbySession.map_upnp(port)
	if saved:
		# gamesetup_mp.js handleAuthenticated: the host picks the save (the
		# load page), and the match scene hosts it once loaded
		# and registers its game in the lobby.
		MatchSettings.net_saved_game = true
		var d: Node = load("res://scenes/session/savegame_dialog.gd").open(self, "load")
		d.closed.connect(func() -> void:
			MatchSettings.net_saved_game = false
			MatchSettings.net_mode = "")
		return
	if LobbySession.signed_in():
		# The same id main_node registers, so both update one entry (the lobby
		# matches main_node's untokened post to it by address).
		LobbySession.host_game({
			"id": "room-%d" % port,
			"host_port": port,
			"name": game_name,
			"host_name": who,
			"map_id": MatchSettings.map_id,
			"map_name": MatchSettings.map_id.replace("_", " ").capitalize(),
			"max_players": 8,
			"players": 1,
			"password": _host_pwd.text != "",
			"state": "init",
		})
	MatchSettings.go("res://scenes/main.tscn")
