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

const GameSetupMp := preload("res://scenes/menu/gamesetup_mp.gd")
const REFRESH_S := 3.0
# /events: how long the server may hold a request (below LobbySession's 5 s timeout).
const EVENTS_WAIT_S := 4.0

var _server: LineEdit
var _name: LineEdit
var _sign_btn: Button
var _password: LineEdit
var _password2: LineEdit # prelobby Register's passwordRepeat
var _remember: CheckBox
var _register_btn: Button
var _games: Tree
var _setup: GDScript = load("res://scenes/menu/gamesetup.gd")
var game_sort_column := "Name"
var game_sort_order := 1
var _players: Tree
## PlayerList.xml playersBox: sortable by status, name or rating, name ascending first.
var player_sort_column := 1
var player_sort_order := 1
var _last_players: Variant = null
var _players_filter: LineEdit
var _requests_sent := {} # _get_latest: per kind, the last request made
var _requests_applied := {} # and the last one whose answer was used
var _profile: VBoxContainer
var _profile_name: Label
var _profile_role: Label # ProfilePanel roleText
var _profile_values := {}
# ProfilePage: "Player Profile Lookup", any player by name.
var _lookup: AcceptDialog
var _lookup_input: LineEdit
var _lookup_error: Label
var _lookup_grid: GridContainer
var _lookup_values := {}
var _lookup_player := ""
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
var _events_version := -1 # -1: not watching
var _chat_backlog := true # the first fetch after signing in is the backlog (lobby.history)
var _chat_replay := false # the first fetch of a page reopened in the session: its chat so far
var _reconnect_dlg: ConfirmationDialog
var _rooms: Array = []
var _ratings := {} # player name -> rating, from the player list
## lobby.js with attribs.dialog: opened from the game setup's Lobby button
## over the setup, without Host and Join; Back (or Escape) closes it.
var dialog := false
signal closed

## The page as a dialog on its own layer over the game setup or the match (it frees
## itself on Back); nothing without a lobby connection (Engine.HasXmppClient).
static func open_dialog(tree: SceneTree) -> Node:
	if not tree.root.get_node("LobbySession").signed_in():
		return null
	var open := tree.root.get_node_or_null("LobbyDialog")
	if open and not open.is_queued_for_deletion():
		return open.get_child(0) # one lobby page at a time
	var layer := CanvasLayer.new()
	layer.name = "LobbyDialog"
	layer.layer = 40
	var lobby: Control = load("res://scenes/menu/lobby.tscn").instantiate()
	lobby.dialog = true
	lobby.closed.connect(layer.queue_free)
	layer.add_child(lobby)
	tree.root.add_child(layer)
	return lobby
var _moderators := {} # names the lobby lists as moderators
var _muted := {} # names with the "visitor" role (muted)
var _notice_dlg: AcceptDialog
var _subject := "" # the lobby room subject (Subject.js)
var _details: RichTextLabel
var _details_preview: TextureRect
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

# GameList.xml gamesBox: the columns, and the key each one sorts on. The
# first, headed by nothing, holds the lock of a game with a password.
const GAME_COLUMNS := ["Password", "Name", "Map Name", "Size", "Type", "Players"]
const GAME_SORT_KEYS := {"Password": "hasPassword", "Name": "gameName", "Map Name": "mapName", "Size": "mapSize", "Type": "mapType",
	"Players": "maxnbp", "Rating": "gameRating"}
# Game.js GameStatusOrder and StateTags: setups first, in green; running red.
const GAME_STATES := ["init", "waiting", "running"]
const STATE_COLORS := {"init": Color8(0, 219, 0), "waiting": Color8(255, 127, 0), "running": Color8(219, 0, 0)}
# GameList.xml textcolor of the other columns.
const GAME_COLUMN_COLORS := {"Map Name": Color8(128, 128, 128), "Size": Color8(128, 128, 128),
	"Type": Color8(0, 160, 160), "Players": Color8(0, 128, 128), "Rating": Color8(0, 160, 160)}
# GameListFilters/Rating.js RatingFilters, after "Any".
const RATING_FILTERS := ["", ">1500", ">1400", ">1300", ">1200", "<1200", "<1100", "<1000"]


func _ready() -> void:
	_build()
	# LobbyHandler: the page (not the dialog) plays the menu music, so the match
	# track a summary carried over stops here.
	if not dialog:
		MenuAudio.play_menu_music()
	if LobbySession.signed_in():
		LobbySession.lobby_opened(dialog)
	if dialog:
		tree_exiting.connect(LobbySession.lobby_dialog_closed)
	_timer = Timer.new()
	_timer.wait_time = REFRESH_S
	_timer.timeout.connect(_refresh)
	add_child(_timer)
	_timer.start()
	_watch_events()
	_server.text = LobbySession.address
	_name.text = LobbySession.player_name if LobbySession.signed_in() else multiplayer_name()
	LobbySession.kicked.connect(_on_kicked)
	LobbySession.disconnected.connect(_on_disconnected)
	if LobbySession.signed_in() and LobbySession.chat_from >= 0:
		# LobbyHandler: a reopened lobby shows the session's chat so far.
		_chat_last = LobbySession.chat_from
		_chat_backlog = false
		_chat_replay = true
	_update_signed_in()
	if LobbySession.lost:
		_ask_reconnect.call_deferred()
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
	header.add_child(_button("Player Profile Lookup", _on_lookup_pressed))
	# LobbyPage.xml: "Account settings" (AccountSettingsPage).
	header.add_child(_button("Account settings", _on_account_pressed))
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
	_password2 = _line("", 120)
	_password2.secret = true
	_password2.placeholder_text = "Repeat to register"
	login.add_child(_password2)
	_remember = CheckBox.new()
	_remember.text = "Remember password"
	_remember.button_pressed = cfg.get_bool("lobby.rememberpassword")
	_remember.toggled.connect(_on_remember_toggled)
	login.add_child(_remember)
	_sign_btn = _button("Sign In", _on_sign_pressed)
	login.add_child(_sign_btn)
	_register_btn = _button("Register", _on_register_pressed)
	login.add_child(_register_btn)
	# termslobby.js: the three terms buttons under the sign-in row.
	_terms_row = HBoxContainer.new()
	_terms_row.visible = not dialog
	root.add_child(_terms_row)
	for t in TERMS:
		var b := _button(t["title"], func() -> void: open_terms(t["page"]))
		b.name = "Terms" + t["page"]
		_terms_row.add_child(b)

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
	_type_filter.visible = not _rating_column # GameListFilters/Rating.js makes room for the rating filter
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
	_games.set_column_expand(0, false)
	_games.set_column_custom_minimum_width(0, 20)
	_games.set_column_expand_ratio(1, 3)
	_games.set_column_expand_ratio(3, 2)
	_games.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_games.item_selected.connect(_on_game_selected)
	# gamesBox sortable: a click on a heading sorts on it, again reverses.
	_games.column_title_clicked.connect(func(c: int, _b: int) -> void: sort_games_by(game_columns(_rating_column)[c]))
	_update_game_titles()
	if not dialog:
		_games.item_activated.connect(_on_join_pressed.bind(false))
	left.add_child(_games)
	# GameDetails.js sgMapPreview: the selected game's map, over its details.
	_details_preview = TextureRect.new()
	_details_preview.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_details_preview.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	_details_preview.custom_minimum_size = Vector2(0, 160)
	_details_preview.visible = false
	left.add_child(_details_preview)
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
	# PlayerList.xml playersFilter: Enter picks the typed player, Tab completes.
	_players_filter = _line("", 0)
	_players_filter.placeholder_text = "Search for Player"
	_players_filter.tooltip_text = AUTOCOMPLETE_TIP
	_players_filter.text_submitted.connect(_select_player)
	_players_filter.gui_input.connect(_on_name_input.bind(_players_filter))
	right.add_child(_players_filter)
	_players = Tree.new()
	_players.columns = 3
	_players.hide_root = true
	_players.column_titles_visible = true
	_update_player_titles()
	_players.column_title_clicked.connect(func(c: int, _b: int) -> void: sort_players_by(c))
	_players.set_column_expand_ratio(1, 2)
	_players.custom_minimum_size = Vector2(0, 140)
	_players.item_selected.connect(func() -> void:
		var it := _players.get_selected()
		var picked := str(it.get_metadata(0)) if it else ""
		if picked == _selected_player:
			return # the refresh re-selecting it
		_selected_player = picked
		_select_game_of(_selected_player)
		_update_buddy_button()
		_request_profile(_selected_player))
	right.add_child(_players)
	_buddy_btn = _button("Mark as Buddy", _on_buddy_pressed)
	_buddy_btn.disabled = true
	right.add_child(_buddy_btn)
	_players.item_activated.connect(_on_buddy_pressed) # PlayerList double click
	# onMouseLeftClickItem: a click on the picked player finds their game again.
	_players.item_mouse_selected.connect(func(_p: Vector2, _b: int) -> void: _select_game_of(_selected_player))
	_profile = _build_profile()
	right.add_child(_profile)
	_leaderboard = _build_leaderboard()
	add_child(_leaderboard)
	_lookup = _build_lookup()
	add_child(_lookup)
	right.add_child(_label("Chat"))
	_chat_log = RichTextLabel.new()
	_chat_log.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_chat_log.scroll_following = true
	_chat_log.bbcode_enabled = true
	right.add_child(_chat_log)
	var chat_row := HBoxContainer.new()
	right.add_child(chat_row)
	_chat_line = _line("", 0)
	_chat_line.max_length = LobbySession.MAX_CHAT # lobby ChatPanel.xml chatInput max_length
	_chat_line.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_chat_line.placeholder_text = "Chat"
	_chat_line.text_submitted.connect(func(_t: String) -> void: _on_send_pressed())
	_chat_line.tooltip_text = AUTOCOMPLETE_TIP # ChatInputPanel onTab
	_chat_line.gui_input.connect(_on_name_input.bind(_chat_line))
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
	_profile_role = _label("")
	box.add_child(_profile_role)
	var grid := GridContainer.new()
	grid.columns = 2
	box.add_child(grid)
	for row in PROFILE_ROWS:
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
	# LeaderboardPage leaderboardUpdateButton: ask the lobby again.
	dlg.add_button("Update", false, "update")
	dlg.custom_action.connect(func(action: StringName) -> void:
		if action == &"update" and LobbySession.signed_in():
			LobbySession.get_json("/leaderboard", _on_leaderboard))
	dlg.confirmed.connect(func() -> void: _request_profile(_selected_player))
	dlg.canceled.connect(func() -> void: _request_profile(_selected_player))
	return dlg


const NOT_AVAILABLE := "N/A"
const PROFILE_ROWS := [["rank", "Current Rank:"], ["highestRating", "Highest Rating:"], ["totalGamesPlayed", "Total Games:"],
	["wins", "Wins:"], ["losses", "Losses:"], ["ratio", "Win Rate:"]]


# ProfilePage.xml: "Enter playername:", View Profile, then the profile or
# 'Player "%(nick)s" not found.'
func _build_lookup() -> AcceptDialog:
	var dlg := AcceptDialog.new()
	dlg.name = "ProfileLookup"
	dlg.title = "Player Profile Lookup"
	dlg.ok_button_text = "Back"
	dlg.min_size = Vector2i(360, 300)
	var box := VBoxContainer.new()
	dlg.add_child(box)
	box.add_child(_label("Enter playername:"))
	var row := HBoxContainer.new()
	box.add_child(row)
	_lookup_input = _line("", 180)
	_lookup_input.text_submitted.connect(func(_t: String) -> void: _on_lookup_view())
	row.add_child(_lookup_input)
	row.add_child(_button("View Profile", _on_lookup_view))
	_lookup_error = _label("")
	_lookup_error.visible = false
	box.add_child(_lookup_error)
	_lookup_grid = GridContainer.new()
	_lookup_grid.columns = 2
	_lookup_grid.visible = false
	box.add_child(_lookup_grid)
	_lookup_values["name"] = _label("")
	_lookup_grid.add_child(_label("Player:"))
	_lookup_grid.add_child(_lookup_values["name"])
	for r in PROFILE_ROWS:
		_lookup_grid.add_child(_label(r[1]))
		_lookup_values[r[0]] = _label(NOT_AVAILABLE)
		_lookup_grid.add_child(_lookup_values[r[0]])
	return dlg


## AccountSettingsPage.xml: the new password twice, the request's result, and
## Close / Change password.
func _on_account_pressed() -> void:
	if not LobbySession.signed_in():
		_say("Sign in to change your account settings.")
		return
	var dlg := AcceptDialog.new()
	dlg.name = "AccountSettings"
	dlg.title = "Account settings"
	dlg.ok_button_text = "Close"
	dlg.min_size = Vector2i(460, 280)
	var box := VBoxContainer.new()
	dlg.add_child(box)
	box.add_child(_label("New password:"))
	var pw := _line("", 0)
	pw.name = "Password"
	pw.secret = true
	box.add_child(pw)
	box.add_child(_label("Confirm new password:"))
	var pw2 := _line("", 0)
	pw2.name = "PasswordConfirm"
	pw2.secret = true
	box.add_child(pw2)
	var result := _label("")
	result.name = "RequestResult"
	box.add_child(result)
	var change := dlg.add_button("Change password", true, "change")
	dlg.custom_action.connect(func(_a: StringName) -> void: _change_password(pw.text, pw2.text, result, change))
	dlg.confirmed.connect(dlg.queue_free)
	dlg.canceled.connect(dlg.queue_free)
	add_child(dlg)
	dlg.popup_centered()
	pw.grab_focus()


## AccountSettingsPage._changePassword: check the length and the confirmation,
## ask the lobby, then keep lobby.password only when it is remembered.
func _change_password(new_pass: String, confirm: String, result: Label, button: Button) -> void:
	var fail := func(text: String) -> void:
		result.add_theme_color_override("font_color", Color.RED)
		result.text = text
	result.add_theme_color_override("font_color", Color.WHITE)
	result.text = "Changing password…"
	if new_pass.length() < LobbySession.MIN_PASSWORD_LENGTH:
		fail.call("Please choose a longer password")
		return
	if confirm != new_pass:
		fail.call("Passwords do not match")
		return
	var encrypted: String = LobbySession.encrypt_password(new_pass, LobbySession.player_name)
	button.disabled = true
	LobbySession.change_password(encrypted, func(code: int, obj) -> void:
		if not is_instance_valid(result):
			return
		button.disabled = false
		if code == 0:
			fail.call("Request timed out.")
		elif code != 200:
			fail.call(str(obj.get("error", "Error")) if obj is Dictionary else "Error")
		else:
			result.text = "Password changed successfully."
			var cfg := RtsoUserConfig.new()
			cfg.set_value("lobby.password", encrypted if cfg.get_bool("lobby.rememberpassword") else "")
			cfg.save())


func _on_lookup_pressed() -> void:
	if not LobbySession.signed_in():
		_say("Sign in to look up a player.")
		return
	_lookup_error.visible = false
	_lookup_grid.visible = false
	_lookup.popup_centered()
	_lookup_input.grab_focus()


func _on_lookup_view() -> void:
	_lookup_player = _lookup_input.text.strip_edges()
	if _lookup_player == "":
		return
	var asked := _lookup_player
	LobbySession.get_json("/profile?name=" + asked.uri_encode(), func(code: int, obj) -> void:
		if not is_inside_tree() or asked != _lookup_player:
			return
		var found := code == 200 and obj is Dictionary
		_lookup_grid.visible = found
		_lookup_error.visible = not found
		if not found:
			_lookup_error.text = 'Player "%s" not found.' % asked
			return
		_lookup_values["name"].text = LobbySession.rated_name(str(obj.get("name", "")), obj.get("rating"))
		_lookup_values["name"].add_theme_color_override("font_color", player_color(str(obj.get("name", ""))))
		for k in ["rank", "highestRating", "totalGamesPlayed", "wins", "losses"]:
			_lookup_values[k].text = str(obj.get(k, NOT_AVAILABLE))
		_lookup_values["ratio"].text = LobbySession.format_win_rate(obj))


func _request_profile(player: String) -> void:
	_requested_player = player
	_profile.visible = player != "" or _profile_name.text != ""
	if player == "" or not LobbySession.signed_in():
		return
	_profile_name.text = player
	_profile_name.add_theme_color_override("font_color", player_color(player)) # ColorPlayerName
	# ProfilePanel RoleNames: Moderator, Muted Player (visitor), else Player.
	_profile_role.text = "Moderator" if _moderators.has(player) else "Muted Player" if _muted.has(player) else "Player"
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
	_password2.editable = not on
	_register_btn.disabled = on
	_chat_line.editable = on
	_chat_send.disabled = not on
	_update_chat_input()
	if not on:
		_players.clear()
		_games.clear()
		_rooms = []
		_shown_rooms = []
		_details.visible = false
	_update_buddy_button()
	_say(("Signed in as %s." % LobbySession.player_name) if on
		else "Sign in to see the games and players, or join an address directly.")


## ChatInputPanel.update: a muted player ("visitor") gets no chat input.
func _update_chat_input() -> void:
	var muted := LobbySession.signed_in() and _muted.has(LobbySession.player_name)
	_chat_line.visible = not muted
	_chat_send.visible = not muted


func _on_back_pressed() -> void:
	if dialog:
		# QuitButton.closeDialog: back to the game setup.
		closed.emit()
		queue_free()
		return
	# QuitButton.returnToMainMenu: StopXmppClient, so we leave the lobby.
	if LobbySession.signed_in() and LobbySession.room.is_empty():
		LobbySession.sign_out()
	MatchSettings.go("res://scenes/menu/main_menu.tscn")


## ConnectionHandler: the lobby lost our session.
func _on_disconnected() -> void:
	_system_line("Disconnected.")
	_update_signed_in()
	_ask_reconnect()


## credentials.js toggleRememberPassword: unticking a remembered password asks first;
## No ticks it again.
var _forget_dlg: ConfirmationDialog

func _on_remember_toggled(on: bool) -> void:
	var cfg := RtsoUserConfig.new()
	if on or not cfg.get_bool("lobby.rememberpassword") or cfg.get_value("lobby.password") == "" or _forget_dlg != null:
		return
	_forget_dlg = ConfirmationDialog.new()
	_forget_dlg.name = "ForgetPasswordDialog"
	_forget_dlg.title = "Confirmation"
	_forget_dlg.dialog_text = "Are you sure you want to delete the password after connecting?"
	_forget_dlg.ok_button_text = "Yes"
	_forget_dlg.cancel_button_text = "No"
	add_child(_forget_dlg)
	var done := func(keep: bool) -> void:
		_forget_dlg.queue_free()
		_forget_dlg = null
		if keep:
			_remember.set_pressed_no_signal(true)
	_forget_dlg.confirmed.connect(func() -> void: done.call(false))
	_forget_dlg.canceled.connect(func() -> void: done.call(true))
	_forget_dlg.popup_centered()


func _ask_reconnect() -> void:
	if _reconnect_dlg != null or LobbySession.signed_in():
		return
	_reconnect_dlg = ConfirmationDialog.new()
	_reconnect_dlg.name = "ReconnectDialog"
	_reconnect_dlg.title = "Connection Lost"
	_reconnect_dlg.dialog_text = "You have been disconnected from the lobby. Do you want to reconnect?"
	_reconnect_dlg.ok_button_text = "Yes"
	_reconnect_dlg.cancel_button_text = "No"
	add_child(_reconnect_dlg)
	var done := func() -> void:
		_reconnect_dlg.queue_free()
		_reconnect_dlg = null
	_reconnect_dlg.confirmed.connect(func() -> void:
		done.call()
		_say("Connecting…")
		LobbySession.reconnect(func(code: int, obj) -> void:
			if not is_inside_tree():
				return
			if code == 200:
				_chat_last = 0
				_chat_backlog = true
				_chat_replay = false
				_system_line("Connected.")
				_update_signed_in()
				_refresh()
			else:
				_say(lobby_error(code, obj))))
	_reconnect_dlg.canceled.connect(func() -> void:
		LobbySession.lost = false
		done.call())
	_reconnect_dlg.popup_centered()


func _unhandled_input(event: InputEvent) -> void:
	if not dialog:
		return
	if event.is_action_pressed("ui_cancel"):
		get_viewport().set_input_as_handled()
		_on_back_pressed()
		return
	if event is InputEventKey:
		# QuitButton.js: the "lobby" hotkey closes the dialog again.
		if event.pressed and "lobby" in _dialog_hotkeys().fired(event):
			get_viewport().set_input_as_handled()
			_on_back_pressed()
			return
		# A child page (Engine.OpenChildPage) is the top of the page stack and
		# takes the keyboard: no key reaches the setup or the match behind it.
		get_viewport().set_input_as_handled()


var _hotkeys: RtsoHotkeyConfig

func _dialog_hotkeys() -> RtsoHotkeyConfig:
	if _hotkeys == null:
		_hotkeys = RtsoHotkeyConfig.new()
	return _hotkeys


## termslobby.js initLobbyTerms: each page's title, instruction and config key.
const TERMS := [
	{"page": "Service", "title": "Terms of Service", "instruction": "Please read and accept the Terms of Service.",
		"config": "lobby.terms_of_service"},
	{"page": "Use", "title": "Terms of Use", "instruction": "Please read and accept the Terms of Use.",
		"config": "lobby.terms_of_use"},
	{"page": "Privacy", "title": "Privacy Policy", "instruction": "Please read and accept the Privacy Policy.",
		"config": "lobby.privacy_policy"},
]
var _terms_row: HBoxContainer
var _terms_dlg: ConfirmationDialog

func _terms_entry(page: String) -> Dictionary:
	for t in TERMS:
		if t["page"] == page:
			return t
	return {}

## checkTerms: the instruction of the first page this name has not accepted in
## its current version, or "" when all are (or the server has none).
func _terms_problem(who: String, pages: Array) -> String:
	var cfg := RtsoUserConfig.new()
	for p in pages:
		var t := _terms_entry(str(p.get("page", "")))
		if not t.is_empty() and cfg.get_value(t["config"]) != LobbySession.terms_hash(who, str(p.get("text", ""))):
			return t["instruction"]
	return ""

## Connect and Register wait until every page is accepted.
func _with_terms(who: String, then: Callable) -> void:
	LobbySession.fetch_terms(_server.text.strip_edges(), func(pages: Array) -> void:
		if not is_inside_tree():
			return
		var problem := _terms_problem(who, pages)
		if problem != "":
			_say(problem)
		else:
			then.call())

## openTerms / termsdialog: the page's text, Decline or Accept; the accepted
## version (salted with the player name) goes to the user config.
func open_terms(page: String) -> void:
	var t := _terms_entry(page)
	if t.is_empty() or _terms_dlg != null:
		return
	var who := _name.text.strip_edges()
	LobbySession.fetch_terms(_server.text.strip_edges(), func(pages: Array) -> void:
		if not is_inside_tree() or _terms_dlg != null:
			return
		var text := ""
		var found := false
		for p in pages:
			if str(p.get("page", "")) == page:
				text = str(p.get("text", ""))
				found = true
		if not found:
			_say("The lobby server at %s has no %s." % [_server.text, t["title"]])
			return
		_terms_dlg = ConfirmationDialog.new()
		_terms_dlg.name = "TermsDialog"
		_terms_dlg.title = t["title"]
		_terms_dlg.ok_button_text = "Accept"
		_terms_dlg.cancel_button_text = "Decline"
		var body := RichTextLabel.new()
		body.name = "TermsText"
		body.text = text
		body.custom_minimum_size = Vector2(560, 360)
		_terms_dlg.add_child(body)
		add_child(_terms_dlg)
		var done := func(accepted: bool) -> void:
			var cfg := RtsoUserConfig.new()
			cfg.set_value(t["config"], LobbySession.terms_hash(who, text) if accepted else "0")
			cfg.save()
			_terms_dlg.queue_free()
			_terms_dlg = null
		_terms_dlg.confirmed.connect(func() -> void: done.call(true))
		_terms_dlg.canceled.connect(func() -> void: done.call(false))
		_terms_dlg.popup_centered())

func _on_sign_pressed() -> void:
	if LobbySession.signed_in():
		LobbySession.sign_out()
		_update_signed_in()
		return
	var who := _name.text.strip_edges()
	if who == "":
		_say("Type a player name first.")
		return
	_with_terms(who, func() -> void: _sign_in(who))


# The password typed now, else the one lobby.password remembers for this name.
func _encrypted_password(who: String) -> String:
	if _password.text != "":
		return LobbySession.encrypt_password(_password.text, who)
	var cfg := RtsoUserConfig.new()
	if _remember.button_pressed and cfg.get_value("lobby.login").to_lower() == who.to_lower():
		return cfg.get_value("lobby.password")
	return ""


## feedback.js: the error's own text (XmppClient's, as lobby_server.py sends it),
## else 0.28's "Unknown error." line; a server that does not answer is
## ConnectionErrorToString's ConnConnectionRefused.
static func lobby_error(code: int, obj) -> String:
	if code == 0:
		return "The connection was refused by the server"
	var text := str(obj.get("error", "")) if obj is Dictionary else ""
	return text if text != "" else "Unknown error. This usually occurs because the same IP address is not allowed to register more than one account within one hour."


func _sign_in(who: String) -> void:
	_sign_btn.disabled = true
	_say("Connecting…") # login.js continueButton
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
			_password2.text = ""
			MatchSettings.net_player_name = LobbySession.player_name
			_chat_last = 0
			_chat_backlog = true
			_chat_log.clear()
			_update_signed_in()
			_refresh()
		else:
			_say(lobby_error(code, obj))
	LobbySession.sign_in(_server.text.strip_edges(), who, done, encrypted)


func _on_register_pressed() -> void:
	var who := _name.text.strip_edges()
	var problem := LobbySession.register_problem(who, _password.text, _password2.text)
	if problem != "":
		_say(problem)
		return
	_with_terms(who, func() -> void: _register(who))


func _register(who: String) -> void:
	_register_btn.disabled = true
	_say("Registering…") # register.js continueButton
	LobbySession.register(_server.text.strip_edges(), who, _password.text, func(code: int, obj) -> void:
		_register_btn.disabled = false
		if code == 200:
			_say("Registered") # register.js onRegistered; RTS0 then signs in at once
			_sign_in(who)
		else:
			_say(lobby_error(code, obj)))


## XmppMessages.js: 0 A.D. reads pushed lobby messages every GUI tick. Here the
## page waits on /events, which answers as soon as the chat, the player list or
## the game list changes, and refreshes at once; the REFRESH_S timer stays as a
## fallback (and for servers without /events).
func _watch_events() -> void:
	if _events_version >= 0:
		return # already waiting
	_events_version = 0
	_wait_events()


func _wait_events() -> void:
	if not is_inside_tree():
		_events_version = -1
		return
	if not LobbySession.signed_in():
		get_tree().create_timer(1.0).timeout.connect(_wait_events)
		return
	var since := _events_version
	LobbySession.get_json("/events?since=%d&wait=%s" % [since, EVENTS_WAIT_S], func(code: int, obj) -> void:
		if not is_inside_tree():
			_events_version = -1
			return
		if code == 404:
			_events_version = -1 # an older server: the timer alone
			return
		if code == 200 and obj is Dictionary:
			var v := int(obj.get("version", 0))
			if v != since:
				_events_version = v
				_refresh()
			_wait_events()
		else:
			get_tree().create_timer(1.0).timeout.connect(_wait_events))


func _refresh() -> void:
	if not LobbySession.signed_in():
		return
	_get_latest("/rooms", _on_rooms)
	_get_latest("/players", _on_players)
	_get_latest("/chat?since=%d" % _chat_last, _on_chat, "/chat")


## A list request whose answer is applied only if no later request of the same
## kind was answered first: an answer that overtakes an older one must not be
## undone by it (a game list from before a host registered, or chat from
## before the last line, which would show that line twice).
func _get_latest(path: String, cb: Callable, kind := "") -> void:
	kind = kind if kind != "" else path
	_requests_sent[kind] = int(_requests_sent.get(kind, 0)) + 1
	var n: int = _requests_sent[kind]
	LobbySession.get_json(path, func(code: int, obj) -> void:
		if n < int(_requests_applied.get(kind, 0)):
			return
		_requests_applied[kind] = n
		cb.call(code, obj))


func _on_rooms(code: int, obj) -> void:
	if code != 200 or not (obj is Dictionary):
		return
	_rooms = obj.get("rooms", [])
	_show_rooms()


## The game list's columns; with lobby.columns.gamerating, Rating
## (GameList.js gameRating) in place of Type (hidden_mapType).
static func game_columns(rating: bool) -> Array:
	var cols: Array = GAME_COLUMNS.duplicate()
	if rating:
		cols.erase("Type")
		cols.append("Rating")
	return cols


## GameListFilters/Rating.js: "" passes every game, ">N" a game rated at least
## N, "<N" one rated below N.
static func rating_passes(filter: String, rating: int) -> bool:
	if filter == "":
		return true
	var n := filter.substr(1).to_int()
	return rating >= n if filter.begins_with(">") else rating < n


func sort_players_by(column: int) -> void:
	if column == player_sort_column:
		player_sort_order = -player_sort_order
	else:
		player_sort_column = column
		player_sort_order = 1
	_update_player_titles()
	if _last_players != null:
		_on_players(200, _last_players)


func _update_player_titles() -> void:
	for i in 3:
		_players.set_column_title(i, ["Status", "Name", "Rating"][i]
			+ (("  ▲" if player_sort_order > 0 else "  ▼") if i == player_sort_column else ""))


## PlayerList.js parsePlayer and sortPlayers: status sorts by presence order
## (Online, Away, Busy) then name, name by lowercase name, rating by number
## (unrated as 0).
func sorted_players(list: Array) -> Array:
	const STATUS_ORDER := ["available", "away", "playing", "offline", "unknown"]
	var keyed := []
	for p in list:
		var key: Variant
		match player_sort_column:
			0:
				var at := STATUS_ORDER.find(str(p.get("status", "available")))
				key = str(at if at >= 0 else STATUS_ORDER.size() - 1) + str(p.get("name", "")).to_lower()
			2:
				key = float(p["rating"]) if p.get("rating") != null else 0.0
			_:
				key = str(p.get("name", "")).to_lower()
		keyed.append([key, p])
	keyed.sort_custom(func(a: Array, b: Array) -> bool:
		return a[0] < b[0] if player_sort_order > 0 else a[0] > b[0])
	return keyed.map(func(e: Array) -> Variant: return e[1])


func sort_games_by(column: String) -> void:
	if column == game_sort_column:
		game_sort_order = -game_sort_order
	else:
		game_sort_column = column
		game_sort_order = 1
	_update_game_titles()
	_show_rooms()


func _update_game_titles() -> void:
	var titles := game_columns(_rating_column)
	for i in titles.size():
		_games.set_column_title(i, ("" if titles[i] == "Password" else titles[i])
			+ (("  ▲" if game_sort_order > 0 else "  ▼") if titles[i] == game_sort_column else ""))


## Game.js sortValues: Name sorts setups, then running games, by name; the
## others on the stanza's value.
func _game_sort_value(r: Dictionary, column: String) -> Variant:
	match GAME_SORT_KEYS.get(column, "gameName"):
		"mapName":
			return game_map_title(r).to_lower()
		"mapSize":
			return _setup.MAP_SIZES.map(func(z) -> String: return str(z[1])).find(str(r.get("map_size", "")))
		"mapType":
			return str(r.get("map_type", ""))
		"maxnbp":
			return int(r.get("max_players", MAX_PLAYERS))
		"gameRating":
			return _room_rating(r)
		"hasPassword":
			return 1 if int(r.get("password", 0)) else 0
	# compatibility + state + name, as Game.js sortValues.gameName.
	return "%s%d%s" % [str(LobbySession.game_compatible(r)), GAME_STATES.find(str(r.get("state", "init"))), game_title(r).to_lower()]


static var _lock: Texture2D

## icon_private: the lock from the UI icons (read from the file where it is not imported).
static func _lock_icon() -> Texture2D:
	const PATH := "res://assets/textures/ui/archive/icons/locked_small.png"
	if _lock == null:
		var img: Image = null
		if ResourceLoader.exists(PATH):
			img = (load(PATH) as Texture2D).get_image()
		elif FileAccess.file_exists(PATH):
			img = Image.load_from_file(ProjectSettings.globalize_path(PATH))
		if img:
			img.resize(16, 16)
			_lock = ImageTexture.create_from_image(img)
	return _lock


static func game_title(r: Dictionary) -> String:
	return str(r.get("name", "")) if str(r.get("name", "")) != "" else str(r.get("id", ""))


static func game_map_title(r: Dictionary) -> String:
	return str(r.get("map_name", "")) if str(r.get("map_name", "")) != "" else str(r.get("map_id", ""))


## Game.js getTranslatedPlayerCount: "current/max", "+observers" when watched.
static func game_player_count(r: Dictionary) -> String:
	var watching := (r.get("player_names", []) as Array).filter(func(p) -> bool: return p.get("observer", false)).size()
	return "%d/%d" % [int(r.get("players", 1)), int(r.get("max_players", MAX_PLAYERS))] + (" +%d" % watching if watching else "")


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
	var keys := {}
	for r in _shown_rooms:
		keys[str(r.get("id", ""))] = _game_sort_value(r, game_sort_column)
	_shown_rooms.sort_custom(func(a, b) -> bool:
		var ka = keys[str(a.get("id", ""))]
		var kb = keys[str(b.get("id", ""))]
		if ka == kb:
			return str(a.get("id", "")) < str(b.get("id", ""))
		return ka < kb if game_sort_order > 0 else ka > kb)
	var buddy_list := buddies()
	var col := func(name: String) -> int: return game_columns(_rating_column).find(name)
	_games.clear()
	var top := _games.create_item()
	var reselected := false
	for r in _shown_rooms:
		var it := _games.create_item(top)
		# Game.js: the private column's [icon="icon_private"].
		if int(r.get("password", 0)):
			it.set_icon(0, _lock_icon())
			it.set_tooltip_text(0, "Password protected")
		var name_col: int = col.call("Name")
		it.set_text(name_col, (BUDDY_SYMBOL + " " if room_buddies(r, buddy_list) else "") + game_title(r))
		# Game.js IncompatibleTags: a game needing other mods is grey.
		it.set_custom_color(name_col, STATE_COLORS.get(str(r.get("state", "init")), STATE_COLORS["init"])
			if LobbySession.game_compatible(r) else Color8(128, 128, 128))
		it.set_text(col.call("Map Name"), game_map_title(r))
		it.set_text(col.call("Size"), str(r.get("map_size", "")))
		var type_title := ""
		for t in _setup.MAP_TYPES:
			if t[0] == str(r.get("map_type", "")):
				type_title = t[1]
		if col.call("Type") >= 0:
			it.set_text(col.call("Type"), type_title)
		if _rating_column:
			it.set_text(col.call("Rating"), str(_room_rating(r)))
		it.set_text(col.call("Players"), game_player_count(r))
		for c in GAME_COLUMN_COLORS:
			if col.call(c) >= 0:
				it.set_custom_color(col.call(c), GAME_COLUMN_COLORS[c])
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
	var type_i := _type_filter.selected - 1 if _type_filter.visible else -1
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
		if list.has(split_rating(str(p.get("name", "")))) and not p.get("ai", false):
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
	_get_latest("/players", _on_players) # the marks follow
	_show_rooms()


func _show_details(r: Dictionary) -> void:
	# Subject.js: the room subject shows while no game is selected.
	_details.visible = not r.is_empty() or (_subject != "" and LobbySession.signed_in())
	_details_preview.texture = null if r.is_empty() else \
			load("res://scenes/menu/gamesetup.gd").map_preview(str(r.get("map_id", "")))
	_details_preview.visible = _details_preview.texture != null
	if not r.is_empty():
		_details.text = LobbySession.game_details(r, _ratings)
	elif _details.visible:
		_details.text = _subject.replace("[", "[lb]")


## KickStrings.Local: a moderator removed us from the lobby.
func _on_kicked(text: String) -> void:
	_system_line(text.replace("[", "[lb]"))
	_say(text)
	_update_signed_in()
	# ConnectionHandler.onClientKicked: a message box titled KICKED or BANNED.
	var box := AcceptDialog.new()
	box.name = "KickedDialog"
	box.title = "BANNED" if text.contains("banned") else "KICKED"
	box.dialog_text = text
	add_child(box)
	box.confirmed.connect(box.queue_free)
	box.canceled.connect(box.queue_free)
	box.popup_centered()


func _on_players(code: int, obj) -> void:
	if code != 200 or not (obj is Dictionary):
		if code == 0:
			_say("Lost the lobby server at %s." % LobbySession.address)
		return
	_last_players = obj
	_players.clear()
	_ratings = {}
	_moderators = {}
	_muted = {}
	var buddy_list := buddies()
	var top := _players.create_item()
	for p in sorted_players(obj.get("players", [])):
		if p.get("rating") != null:
			_ratings[str(p.get("name", ""))] = p["rating"]
		if str(p.get("name", "")) == LobbySession.player_name:
			LobbySession.rating = p.get("rating")
		var st := str(p.get("status", "available"))
		if p.get("moderator", false):
			_moderators[str(p.get("name", ""))] = true
		if p.get("muted", false):
			_muted[str(p.get("name", ""))] = true
		var it := _players.create_item(top)
		var col: Color = {"available": Color(0.6, 0.9, 0.6), "away": Color(0.9, 0.8, 0.4), "playing": Color(0.9, 0.5, 0.5)}.get(st, Color.WHITE)
		# PlayerList.js presences: Online, Away, Busy (in a game).
		it.set_text(0, {"available": "Online", "away": "Away", "playing": "Busy"}.get(st, st))
		it.set_custom_color(0, col)
		it.set_text(1, (BUDDY_SYMBOL + " " if buddy_list.has(str(p.get("name", ""))) else "") + _nick(str(p.get("name", ""))))
		it.set_custom_color(1, player_color(str(p.get("name", ""))))
		# PlayerList.js: an unrated player shows "-", in the status colour like the rating.
		it.set_text(2, str(p["rating"]) if p.get("rating") != null else "-")
		it.set_custom_color(2, col)
		it.set_metadata(0, str(p.get("name", "")))
		if str(p.get("name", "")) == _selected_player:
			it.select(0)
	LobbySession.moderator = _moderators.has(LobbySession.player_name)
	_update_chat_input()
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
	var replay := _chat_replay
	_chat_backlog = false
	_chat_replay = false
	if backlog:
		# 0 A.D.'s lobby.history: how many past messages to load when joining.
		var keep := maxi(0, int(RtsoUserConfig.new().get_number("lobby.history")))
		messages = messages.slice(maxi(0, messages.size() - keep))
		# Where this session's chat starts, for the lobby pages opened later.
		LobbySession.chat_from = int(messages[0].get("id", 1)) - 1 if not messages.is_empty() \
				else int(obj.get("last", _chat_last))
	var me := LobbySession.player_name
	var notify := not backlog and not replay and me != "" and RtsoUserConfig.new().get_bool("sound.notify.nick")
	for m in messages:
		# Two requests in flight (the /events refresh and the timer's, or a
		# post's) can both carry a line: show each id once.
		if int(m.get("id", 0)) <= _chat_last:
			continue
		var who := str(m.get("from", ""))
		if notify and who != "" and who != me and str(m.get("text", "")).findn(me) >= 0:
			notify = false
			_play_nick_alert()
		var event: Dictionary = m.get("event", {}) if m.get("event") is Dictionary else {}
		var line := _format_event(event, me)
		_chat_log.append_text(_chat_time(m) + (line if line != "" else _format_chat(who, str(m.get("text", "")), me)) + "\n")
		if str(event.get("kind", "")) == "announcement" and not backlog and not replay:
			_show_notice(str(event.get("subject", "")), str(event.get("text", "")))
	_chat_last = maxi(_chat_last, int(obj.get("last", _chat_last)))


## ChatMessageFormat: a system line in grey; a player's line with their name
## in its colour (after /me, "* name text") and our name highlighted.
func _format_chat(who: String, raw: String, me: String) -> String:
	var text := raw.replace("[", "[lb]")
	if who == "":
		return "[color=#aaaaaa]%s[/color]" % text
	if who != me:
		text = highlight_name(text, me)
	if raw.begins_with("/me "):
		return "* [b]%s[/b] %s" % [colored_name(who, _nick(who)), text.substr(4)]
	if raw.begins_with("/say "):
		text = text.substr(5)
	return "%s: %s" % [colored_name(who, _nick(who)), text]


## StatusMessageEvents.js Role (worded for the reader) and
## ChatMessageFormatAnnouncement ("== Notice:"); "" for any other line.
const ROLE_STRINGS := [
	{"newrole": "visitor", "you": "You have been muted.", "nick": "%s has been muted."},
	{"newrole": "moderator", "you": "You are now a moderator.", "nick": "%s is now a moderator."},
	{"newrole": "participant", "oldrole": "visitor", "you": "You have been unmuted.", "nick": "%s has been unmuted."},
	{"newrole": "participant", "oldrole": "moderator", "you": "You are not a moderator anymore.",
		"nick": "%s is not a moderator anymore."},
]

func _format_event(event: Dictionary, me: String) -> String:
	match str(event.get("kind", "")):
		"role":
			for r in ROLE_STRINGS:
				if r["newrole"] == str(event.get("newrole", "")) and (not r.has("oldrole") or r["oldrole"] == str(event.get("oldrole", ""))):
					var nick := str(event.get("nick", ""))
					var txt: String = r["you"] if nick == me else r["nick"] % nick
					return "[color=#aaaaaa]%s[/color]" % txt.replace("[", "[lb]")
		"announcement":
			return "[b]== Notice:\n%s[/b]" % format_announcement(str(event.get("subject", "")), str(event.get("text", ""))).replace("[", "[lb]")
	return ""

## functions_utility.js formatXmppAnnouncement: the subject, a blank line, the text.
static func format_announcement(subject: String, text: String) -> String:
	var s := subject.strip_edges()
	var t := text.strip_edges()
	return s + ("\n\n" if s != "" and t != "" else "") + t

## AnnouncementHandler: a "Notice" box with the announcement.
func _show_notice(subject: String, text: String) -> void:
	if subject.strip_edges() == "" and text.strip_edges() == "":
		return
	if _notice_dlg != null:
		_notice_dlg.queue_free()
	_notice_dlg = AcceptDialog.new()
	_notice_dlg.name = "NoticeDialog"
	_notice_dlg.title = "Notice"
	_notice_dlg.dialog_text = format_announcement(subject, text)
	_notice_dlg.min_size = Vector2i(400, 250)
	add_child(_notice_dlg)
	_notice_dlg.confirmed.connect(func() -> void: _notice_dlg.queue_free(); _notice_dlg = null)
	_notice_dlg.popup_centered()


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

## PlayerColor.GetPlayerColor: a colour from the name's hash, lightness at
## least 0.7 so it reads on the dark background.
static func player_color(name: String) -> Color:
	var h := 0
	var buf := name.to_utf16_buffer()
	for i in range(0, buf.size() - 1, 2):
		# hash = charCode + ((hash << 5) - hash); JS "<<" takes hash's int32.
		h = (buf[i] | (buf[i + 1] << 8)) + (_int32(_int32(h) << 5) - h)
	h = _int32(h)
	var hsl := _rgb_to_hsl(((h >> 24) & 0xFF) / 255.0, ((h >> 16) & 0xFF) / 255.0, ((h >> 8) & 0xFF) / 255.0)
	var rgb := _hsl_to_rgb(hsl.x, hsl.y, maxf(0.7, hsl.z))
	return Color8(roundi(rgb.x * 255), roundi(rgb.y * 255), roundi(rgb.z * 255))

static func _int32(x: int) -> int:
	return ((x & 0xFFFFFFFF) ^ 0x80000000) - 0x80000000

## color.js rgbToHsl and hslToRgb.
static func _rgb_to_hsl(r: float, g: float, b: float) -> Vector3:
	var mx := maxf(r, maxf(g, b))
	var mn := minf(r, minf(g, b))
	var l := (mx + mn) / 2
	if mx == mn:
		return Vector3(0, 0, l)
	var d := mx - mn
	var s := d / (2 - mx - mn) if l > 0.5 else d / (mx + mn)
	var h: float
	if mx == r:
		h = (g - b) / d + (6.0 if g < b else 0.0)
	elif mx == g:
		h = (b - r) / d + 2
	else:
		h = (r - g) / d + 4
	return Vector3(h / 6, s, l)

static func _hsl_to_rgb(h: float, s: float, l: float) -> Vector3:
	if s == 0:
		return Vector3(l, l, l)
	var q := l * (1 + s) if l < 0.5 else l + s - l * s
	var p := 2 * l - q
	return Vector3(_hue2rgb(p, q, h + 1.0 / 3), _hue2rgb(p, q, h), _hue2rgb(p, q, h - 1.0 / 3))

static func _hue2rgb(p: float, q: float, t: float) -> float:
	if t < 0:
		t += 1
	if t > 1:
		t -= 1
	if t < 1.0 / 6:
		return p + (q - p) * 6 * t
	if t < 0.5:
		return q
	if t < 2.0 / 3:
		return p + (q - p) * (2.0 / 3 - t) * 6
	return p

## PlayerColor.ColorPlayerName as BBCode: what is shown (escaped), in the
## colour of the player's name.
static func colored_name(name: String, shown := "") -> String:
	return "[color=#%s]%s[/color]" % [player_color(name).to_html(false), (shown if shown != "" else name).replace("[", "[lb]")]

## ChatMessageFormat: the first mention of our name in an escaped chat line,
## in our colour.
static func highlight_name(text: String, me: String) -> String:
	var esc := me.replace("[", "[lb]")
	var at := text.find(esc)
	if me == "" or at < 0:
		return text
	return text.substr(0, at) + colored_name(me) + text.substr(at + esc.length())

## color.js colorizeAutocompleteHotkey.
const AUTOCOMPLETE_TIP := "Press [Tab] to autocomplete player names."

## functions_utility.js tryAutoComplete: the last word, completed when exactly
## one name starts with it (any case); ", " after a lone word, else " ".
static func autocomplete(text: String, names: Array) -> String:
	var words := text.split(" ")
	var last := words[words.size() - 1]
	if last.is_empty():
		return text
	var found := []
	for w in names:
		if str(w).to_lower().begins_with(last.to_lower()):
			found.append(str(w))
			if found.size() > 1:
				break
	if found.size() != 1:
		return text
	words.remove_at(words.size() - 1)
	var head := " ".join(words)
	return (head + " " + found[0] + " ") if head != "" else (found[0] + ", ")

func _online_names() -> Array:
	var names := []
	if _players.get_root():
		for it in _players.get_root().get_children():
			names.append(str(it.get_metadata(0)))
	return names

## autoCompleteText: Tab in the search field or the chat line completes the
## name before the caret.
func _on_name_input(ev: InputEvent, line: LineEdit) -> void:
	var k := ev as InputEventKey
	if k == null or not k.pressed or k.keycode != KEY_TAB:
		return
	line.accept_event()
	var caret := line.caret_column
	var before := line.text.substr(0, caret)
	var done := autocomplete(before, _online_names())
	line.text = done + line.text.substr(caret)
	line.caret_column = caret + done.length() - before.length()

## PlayerList.selectPlayer: Enter in the search field picks that player.
func _select_player(name: String) -> void:
	if _players.get_root() == null:
		return
	for it in _players.get_root().get_children():
		if str(it.get_metadata(0)) == name:
			it.select(0)
			_players.scroll_to_item(it)
			return

## GameList.selectGameFromPlayername: the game the player plays in, else the
## last one they watch, else the last one they were in (now offline).
static func game_of_player(rooms: Array, name: String) -> int:
	if name == "":
		return -1
	var pick := -1
	var as_observer := false
	for i in rooms.size():
		for p in rooms[i].get("player_names", []):
			if split_rating(str(p.get("name", ""))) != name:
				continue
			if p.get("observer", false):
				as_observer = true
				pick = i
			elif not p.get("offline", false):
				return i
			if not as_observer:
				pick = i
	return pick

## splitRatingFromNick: "name (1500)" is "name".
static func split_rating(s: String) -> String:
	return str(preload("res://scenes/menu/lobby_session.gd").split_nick(s)["nick"])

func _select_game_of(name: String) -> void:
	var i := game_of_player(_shown_rooms, name)
	var top := _games.get_root()
	if i < 0 or top == null or i >= top.get_child_count():
		return
	var it := top.get_child(i)
	if not it.is_selected(0):
		it.select(0)
	_games.scroll_to_item(it)

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
	["mute", "Mute a specified user in the lobby. Usage: /mute nick", true],
	["unmute", "Unmute a specified user in the lobby. Usage: /unmute nick", true],
	["announce", "Send a notice to everyone in the lobby. Usage: /announce text", true],
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
			LobbySession.chat_from = _chat_last # a page opened later starts from here too
		"kick", "ban":
			# Engine.LobbyKick / LobbyBan: the nick, then the reason.
			var j := args.find(" ")
			LobbySession.post_json("/kick", {"token": LobbySession.token, "nick": args.substr(0, j) if j >= 0 else args,
				"reason": args.substr(j + 1) if j >= 0 else "", "ban": cmd == "ban"}, func(_c: int, _o) -> void: _refresh())
		"mute", "unmute":
			LobbySession.post_json("/mute", {"token": LobbySession.token, "nick": args, "muted": cmd == "mute"},
				func(_c: int, _o) -> void: _refresh())
		"announce":
			LobbySession.post_json("/announce", {"token": LobbySession.token, "subject": "", "text": args},
				func(_c: int, _o) -> void: _refresh())
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


## The name a lobby player hosts or joins with: "nick (rating)" once rated (gamesetup_mp.js).
func _net_name(joined_room := {}) -> String:
	return LobbySession.rated_player_name(joined_room) if LobbySession.signed_in() else _player_name()


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


func _ask_mods(r: Dictionary) -> void:
	var modlib: GDScript = load("res://scenes/menu/mods.gd")
	var ask := ConfirmationDialog.new()
	ask.name = "IncompatibleModsDialog"
	ask.title = "Incompatible mods"
	ask.dialog_text = "Your active mods do not match the mods of this game.\n\n" \
		+ modlib.compared_mods_string(r.get("mods", []) if r.get("mods") is Array else [], modlib.engine_mods()) \
		+ "\n\nDo you want to switch to the mod selection page?"
	ask.ok_button_text = "Yes"
	ask.cancel_button_text = "No"
	add_child(ask)
	ask.canceled.connect(ask.queue_free)
	ask.confirmed.connect(func() -> void:
		ask.queue_free()
		# Engine.StopXmppClient, then the mod selection; closing it leaves for the main menu.
		LobbySession.sign_out()
		_update_signed_in()
		var page: Node = load("res://scenes/menu/modmod.gd").open(self)
		page.tree_exited.connect(func() -> void:
			if is_inside_tree():
				MatchSettings.go("res://scenes/menu/main_menu.tscn")))
	ask.popup_centered()


## password: the one typed in the password page (null until asked).
func _on_join_pressed(confirmed := false, password = null) -> void:
	var r := _selected_room()
	if _join_ip.text.strip_edges() == "" and not r.is_empty():
		# A listed game: its address comes from the lobby, and only to a
		# signed-in player (0 A.D. XmppClient SendIqGetConnectionData).
		if not LobbySession.signed_in():
			_say("Sign in to join a listed game, or type the host's address.")
			return
		# JoinButton.js: a game with other mods offers the mod selection instead.
		if not LobbySession.game_compatible(r):
			_ask_mods(r)
			return
		# JoinButton.js: a running game we are not in is joined only to watch, if we say so.
		if not confirmed and str(r.get("state", "init")) != "init" and not (r.get("player_names", []) as Array).any(
				func(p) -> bool: return split_rating(str(p.get("name", ""))) == LobbySession.player_name):
			var ask := ConfirmationDialog.new()
			ask.name = "ObserveDialog"
			ask.title = "Confirmation"
			ask.dialog_text = "The game has already started. Do you want to join as observer?"
			ask.ok_button_text = "Yes"
			ask.cancel_button_text = "No"
			add_child(ask)
			ask.confirmed.connect(func() -> void:
				ask.queue_free()
				_observe.button_pressed = true
				_on_join_pressed(true))
			ask.canceled.connect(ask.queue_free)
			ask.popup_centered()
			return
		# gamesetup_mp.js "join": a game with hasPassword opens pagePassword
		# before anything is sent, so a missing password costs no try.
		if password == null and int(r.get("password", 0)):
			_ask_password()
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
						str(obj.get("ticket", "")), "" if password == null else str(password)))
		return
	_start_join(_join_ip.text.strip_edges(), _join_port.text.to_int(), "")


# A typed address is joined directly; a listed game (room_id) through the
# lobby, with STUN and hole punching (0 A.D. TryToConnectWithSTUN).
# ticket: the lobby's, which a host signed in to the lobby redeems for this
# player's name (a typed address carries none, and such a host refuses it).
## password: a listed game's, from the password page; a typed address uses the Join row's.
func _start_join(ip: String, port: int, room_id: String, ticket := "", password = null) -> void:
	if ip == "" or port <= 0 or port > 65535:
		_say("Type the host's address and port, or pick a game.")
		return
	var joined := {}
	for r in _rooms:
		if str(r.get("id", "")) == room_id:
			joined = r
	GameSetupMp.start_join(_net_name(joined), ip, port, _join_pwd.text if password == null else str(password),
		_observe.button_pressed, room_id, ticket)


## gamesetup_mp.xml pagePassword: "Password:" (masked, at most 256) and Confirm.
func _ask_password() -> void:
	var dlg := ConfirmationDialog.new()
	dlg.name = "PasswordDialog"
	dlg.title = "Password"
	dlg.ok_button_text = "Confirm"
	var row := HBoxContainer.new()
	row.add_child(_label("Password:"))
	var pwd := LineEdit.new()
	pwd.name = "ClientPassword"
	pwd.secret = true
	pwd.max_length = 256
	pwd.custom_minimum_size = Vector2(200, 0)
	row.add_child(pwd)
	dlg.add_child(row)
	add_child(dlg)
	dlg.register_text_enter(pwd)
	dlg.confirmed.connect(func() -> void:
		dlg.queue_free()
		_on_join_pressed(true, pwd.text))
	dlg.canceled.connect(dlg.queue_free)
	dlg.popup_centered()
	pwd.grab_focus()


func _on_host_pressed(saved := false) -> void:
	# gamesetup_mp.js pageHost: the port must be 1024-65535.
	var typed := _host_port.text.strip_edges()
	if not typed.is_valid_int() or GameSetupMp.valid_port(typed) != typed.to_int():
		_say("Server port number must be between %d and %d." % [GameSetupMp.PORT_MIN, GameSetupMp.PORT_MAX])
		return
	var port := typed.to_int()
	var who := _player_name()
	var game_name := _host_name.text.strip_edges() if _host_name.text.strip_edges() != "" else who + "'s game"
	# gamesetup_mp.js: a game name another host already lists is refused.
	for r in _rooms:
		if str(r.get("name", "")) == game_name and str(r.get("host_name", "")) != who:
			_say("Game name already in use.")
			return
	var failed := GameSetupMp.host_port_error(port)
	if failed != "":
		_say(failed)
		GameSetupMp.show_host_error(self, failed)
		return
	GameSetupMp.start_host(self, _net_name(), game_name, port, _host_pwd.text, saved)
