extends Control
## The main menu's multiplayer dialogs, ported from 0 A.D. 0.28
## gui/gamesetup_mp (gamesetup_mp.js/.xml; GPL-2.0-or-later): "Connect by IP"
## opens pageJoin (player name, server, port), "Host New Game" and "Host
## Saved Game" open pageHost (player name, or the game's name and a password
## while signed in to the lobby, and the port). Continue checks the fields as
## confirmSetup does, keeps them in the user config and starts the game; the
## match scene then shows pageConnecting (RtsoMain update_net_connect_overlay).
## Cancel closes the dialog. The lobby page starts its joins and hosts through
## start_join and start_host too.
##
##     var page = GameSetupMp.open(self, "join")
##     var page = GameSetupMp.open(self, "host", true)   # Host Saved Game

signal closed

## network.js g_ValidPorts; NetHost.h PS_DEFAULT_PORT.
const PORT_MIN := 1024
const PORT_MAX := 65535
const DEFAULT_PORT := 20595

var mode := "join"          # "join" or "host"
var load_saved_game := false

var join_player_name: LineEdit
var join_server: LineEdit
var join_port: LineEdit
var host_player_name: LineEdit
var host_server_name: LineEdit
var host_port: LineEdit
var host_password: LineEdit
var host_feedback: Label
var continue_button: Button
var cancel_button: Button

## page_gamesetup_mp.xml over parent ({"multiplayerGameType": mode, "loadSavedGame": saved}).
static func open(parent: Node, game_type: String, saved := false) -> Control:
	var page = load("res://scenes/menu/gamesetup_mp.gd").new()
	page.name = "GameSetupMp"
	page.mode = game_type
	page.load_saved_game = saved
	parent.add_child(page)
	return page

## network.js getValidPort: the default port for anything outside 1024-65535.
static func valid_port(text: String) -> int:
	var t := text.strip_edges()
	return t.to_int() if t.is_valid_int() and t.to_int() >= PORT_MIN and t.to_int() <= PORT_MAX else DEFAULT_PORT

## gamesetup_mp.js startJoin (and the lobby's startJoinFromLobby with room_id):
## the match scene joins; a direct join keeps its address and port.
static func start_join(player: String, ip: String, port: int, password := "", observe := false,
		room_id := "", ticket := "") -> void:
	var ms := MatchSettings
	ms.net_mode = "join"
	ms.net_lobby = LobbySession.address
	ms.net_player_name = player
	ms.net_ip = ip
	ms.net_port = port
	ms.net_password = password
	ms.net_observe = observe
	ms.net_lobby_ticket = ticket
	if room_id == "":
		var cfg := RtsoUserConfig.new()
		cfg.set_value("multiplayerserver", ip)
		cfg.set_value("multiplayerjoining.port", str(port))
		cfg.save()
	LobbySession.join_game(room_id)
	ms.go("res://scenes/main.tscn")

## Engine.StartNetworkHost throws "Failed to start server" when the port
## cannot be opened (CNetServer::SetupConnection), and gamesetup_mp.js
## startHost shows it as "Cannot host game: %(message)s." and stays open.
static func host_port_error(port: int) -> String:
	var probe := PacketPeerUDP.new()
	var err := probe.bind(port)
	probe.close()
	return "" if err == OK else "Cannot host game: Failed to start server."

## startHost's messageBox titled "Error".
static func show_host_error(parent: Node, text: String) -> AcceptDialog:
	var box := AcceptDialog.new()
	box.name = "HostErrorDialog"
	box.title = "Error"
	box.dialog_text = text
	parent.add_child(box)
	box.confirmed.connect(box.queue_free)
	box.canceled.connect(box.queue_free)
	box.popup_centered()
	return box

## gamesetup_mp.js startHost: the match scene hosts on port, registered in the
## lobby while signed in. A saved game is picked first (handleAuthenticated's
## page_loadgame.xml over `parent`); closing that page hosts nothing.
static func start_host(parent: Node, player: String, game_name: String, port: int, password: String,
		saved: bool) -> void:
	var ms := MatchSettings
	ms.net_mode = "host"
	ms.net_lobby = LobbySession.address
	ms.net_player_name = player
	ms.net_port = port
	ms.net_password = password
	ms.net_observe = false
	var cfg := RtsoUserConfig.new()
	cfg.set_value("multiplayerhosting.port", str(port))
	cfg.save()
	LobbySession.map_upnp(port)
	if saved:
		ms.net_saved_game = true
		var d: Node = load("res://scenes/session/savegame_dialog.gd").open(parent, "load")
		d.closed.connect(func() -> void:
			ms.net_saved_game = false
			ms.net_mode = "")
		return
	if LobbySession.signed_in():
		# The same id main_node registers, so both update one entry (the lobby
		# matches main_node's untokened post to it by address).
		LobbySession.host_game({
			"id": "room-%d" % port,
			"host_port": port,
			"name": game_name,
			"host_name": player,
			"map_id": ms.map_id,
			"map_name": ms.map_id.replace("_", " ").capitalize(),
			"max_players": ms.player_count,
			"players": 1,
			"password": password != "",
			"state": "init",
		})
	ms.go("res://scenes/main.tscn")

static func multiplayer_name() -> String:
	return load("res://scenes/menu/lobby.gd").multiplayer_name()

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	var fade := ColorRect.new() # "ModernFade"
	fade.color = Color(0, 0, 0, 0.6)
	fade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(fade)
	theme = preload("res://scenes/common/modern_theme.gd").theme()
	# switchSetupPage: pageJoin is 145 high either side, pageHost 140 with the
	# lobby (its password row) and 125 without.
	var lobby := LobbySession.signed_in()
	var half := Vector2(230, 145 if mode == "join" else 140 if lobby else 125)
	var panel := Panel.new() # multiplayerPages, ModernDialog
	panel.name = "Dialog"
	panel.theme_type_variation = "ModernDialog"
	panel.set_anchors_preset(Control.PRESET_CENTER)
	panel.offset_left = -half.x
	panel.offset_right = half.x
	panel.offset_top = -half.y
	panel.offset_bottom = half.y
	add_child(panel)
	var title := Label.new()
	title.text = "Multiplayer"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.position = Vector2(half.x - 128, -18)
	title.size = Vector2(256, 32)
	panel.add_child(title)
	var w := half.x * 2
	var cfg := RtsoUserConfig.new()
	if mode == "join":
		_heading(panel, "Joining an existing game.", w)
		join_player_name = _row(panel, "Player Name:", 72, w, "joinPlayerName", multiplayer_name())
		join_server = _row(panel, "Server Hostname or IP:", 112, w, "joinServer", str(cfg.get_value("multiplayerserver")))
		join_port = _row(panel, "Server Port:", 152, w, "joinPort", str(valid_port(str(cfg.get_value("multiplayerjoining.port")))))
		join_port.tooltip_text = "Leave blank to use the default port."
	else:
		_heading(panel, "Set up your server to host.", w)
		# hostPlayerNameWrapper without the lobby, hostServerNameWrapper with it.
		if lobby:
			host_server_name = _row(panel, "Server Name:", 72, w, "hostServerName", "%s's game" % multiplayer_name())
			host_server_name.max_length = 256
		else:
			host_player_name = _row(panel, "Player Name:", 72, w, "hostPlayerName", multiplayer_name())
		host_port = _row(panel, "Server Port:", 112, w, "hostPort", str(valid_port(str(cfg.get_value("multiplayerhosting.port")))))
		host_port.tooltip_text = "Leave blank to use the default port."
		if lobby:
			# Host password is only used on games started through the lobby.
			host_password = _row(panel, "Server Password:", 148, w, "hostPassword", "")
			host_password.secret = true
			host_password.max_length = 256
	host_feedback = Label.new()
	host_feedback.name = "hostFeedback"
	host_feedback.add_theme_color_override("font_color", Color(1, 0, 0))
	host_feedback.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	host_feedback.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	host_feedback.position = Vector2(50, half.y * 2 - 80)
	host_feedback.size = Vector2(w - 100, 35)
	panel.add_child(host_feedback)
	cancel_button = Button.new()
	cancel_button.name = "cancelButton"
	cancel_button.text = "Cancel"
	cancel_button.position = Vector2(18, half.y * 2 - 45)
	cancel_button.size = Vector2(w / 2 - 23, 28)
	cancel_button.pressed.connect(cancel)
	panel.add_child(cancel_button)
	continue_button = Button.new()
	continue_button.name = "continueButton"
	continue_button.text = "Continue"
	continue_button.position = Vector2(w / 2 + 5, half.y * 2 - 45)
	continue_button.size = Vector2(w / 2 - 23, 28)
	continue_button.pressed.connect(confirm_setup)
	panel.add_child(continue_button)
	var first: LineEdit = join_player_name if mode == "join" else host_server_name if lobby else host_player_name
	first.grab_focus.call_deferred()

func _heading(panel: Control, text: String, w: float) -> void:
	var l := Label.new()
	l.text = text
	l.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	l.position = Vector2(0, 32)
	l.size = Vector2(w, 30)
	panel.add_child(l)

func _row(panel: Control, label: String, y: float, w: float, field: String, value: String) -> LineEdit:
	var l := Label.new()
	l.text = label
	l.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	l.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	l.position = Vector2(20, y)
	l.size = Vector2(w / 2 - 20, 24)
	panel.add_child(l)
	var e := LineEdit.new()
	e.name = field
	e.text = value
	e.position = Vector2(w / 2 + 10, y)
	e.size = Vector2(w / 2 - 30, 24)
	e.text_submitted.connect(func(_t: String) -> void: confirm_setup())
	panel.add_child(e)
	return e

## confirmSetup: the join, or the host after its checks. Returns whether a game starts.
func confirm_setup() -> bool:
	host_feedback.text = ""
	if mode == "join":
		var who := join_player_name.text.strip_edges()
		if who == "":
			who = "Player"
		_keep_name(who)
		start_join(who, join_server.text.strip_edges(), valid_port(join_port.text))
		_close()
		return true
	var game_name := ""
	if host_server_name:
		game_name = host_server_name.text.strip_edges()
		if game_name == "":
			host_feedback.text = "Please enter a valid server name."
			return false
	var typed := host_port.text.strip_edges()
	if not typed.is_valid_int() or valid_port(typed) != typed.to_int():
		host_feedback.text = "Server port number must be between %d and %d." % [PORT_MIN, PORT_MAX]
		return false
	var who := LobbySession.player_name if LobbySession.signed_in() else host_player_name.text.strip_edges()
	if who == "":
		who = "Player"
	_keep_name(who)
	if game_name == "":
		game_name = "%s's game" % who
	var failed := host_port_error(typed.to_int())
	if failed != "":
		show_host_error(self, failed)
		return false
	start_host(get_parent(), who, game_name, typed.to_int(), host_password.text if host_password else "", load_saved_game)
	_close()
	return true

## startHost / startJoin keep the name as playername.multiplayer.
func _keep_name(who: String) -> void:
	if LobbySession.signed_in() or who == multiplayer_name():
		return
	var cfg := RtsoUserConfig.new()
	cfg.set_value("playername.multiplayer", who)
	cfg.save()

## cancelButton: close the dialog.
func cancel() -> void:
	_close()

func _close() -> void:
	closed.emit()
	queue_free()

## _input, not _unhandled_input: a focused LineEdit takes Escape (ui_cancel) itself.
## gamesetup_mp.xml: Continue answers the "confirm" hotkey (Return by default) too.
func _input(event: InputEvent) -> void:
	if not (event is InputEventKey and event.pressed and not event.echo):
		return
	var fired: Array = Array(RtsoHotkeyConfig.new().fired(event))
	if "cancel" in fired:
		get_viewport().set_input_as_handled()
		cancel()
	elif "confirm" in fired and continue_button and continue_button.visible and not continue_button.disabled:
		get_viewport().set_input_as_handled()
		confirm_setup()
