extends SceneTree
# End-to-end check of the main menu's 0 A.D. 0.28 entries (gui/pregame/MainMenuItems.js,
# MainMenuItemHandler.js) and the message box (mod gui/msgbox, timedconfirmation):
#   1. the main buttons carry 0.28's tooltips; Continue Campaign is there,
#      disabled with no campaign run;
#   2. the submenus have 0.28's captions, Structure Tree's tooltip names its hotkey;
#   3. Settings > Hotkeys opens the hotkey editor over the menu;
#   4. Exit asks "Are you sure you want to quit 0 A.D.?"; No (and Escape) keeps the menu;
#   5. a timed confirmation counts down and answers its first button when time runs out;
#   6. Multiplayer > Connect by IP, Host New Game and Host Saved Game open 0.28's
#      gamesetup_mp dialogs (pageJoin, pageHost) over the menu: their fields and
#      defaults, confirmSetup's checks, and Continue starting the join or host.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_main_menu.gd
# Exit code 0 = pass.

var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func key(code: Key) -> void:
	for down in [true, false]:
		var k := InputEventKey.new()
		k.keycode = code
		k.physical_keycode = code
		k.pressed = down
		Input.parse_input_event(k)
		await frames(1)

func items(menu, id: String) -> Array:
	menu._toggle_submenu(id, menu.get_node("MainPanel/Buttons/BtnLearn"))
	var out: Array = menu.submenu_buttons_box.get_children().filter(func(b): return not b.is_queued_for_deletion())
	menu._close_submenu()
	return out

func run() -> void:
	change_scene_to_file("res://scenes/menu/main_menu.tscn")
	await frames(10)
	var menu = current_scene
	# colorizeHotkey: the gold "[key]", as in the session.
	var tree_tip: String = menu._item_tooltip({"tooltip": "%(hotkey)s: View the structure tree of civilizations featured in 0 A.D.", "hotkey": "structree"})
	check(tree_tip.begins_with("[color=#fffb83][lb]") and tree_tip.contains("[rb][/color]: View the structure tree"),
		"menu item tooltips colour their hotkey: " + tree_tip)
	check(menu._item_tooltip({"tooltip": "%(hotkey)s", "hotkey": "zz.none"}).contains("Unassigned hotkey: zz.none"),
		"an unbound one says \"Unassigned hotkey: name\"")
	print("1. Main buttons")
	var btns: Node = menu.get_node("MainPanel/Buttons")
	check(btns.get_node("BtnSingle").tooltip_text == "Start, load, or replay a single-player game.", "Single-player tooltip")
	check(btns.get_node("BtnExit").tooltip_text == "Exit the game.", "Exit tooltip")
	var cont: Button = btns.get_node("BtnContinue")
	check(cont.get_index() == btns.get_node("BtnLearn").get_index() + 1 and cont.disabled == (CampaignRun.current_run() == null),
		"Continue Campaign after Learn to Play, enabled only with a campaign run")

	print("1b. Project information")
	var info: Control = menu.get_node_or_null("MainPanel/ProjectInformation")
	check(info != null and info.get_index() < btns.get_index(), "ProjectInformation's panel, under the menu buttons")
	check(menu.community_buttons.map(func(b): return b.text) == ["Website", "Chat", "Report a Bug", "FAQ", "Translate the Game", "Donate"],
		"the six community buttons")
	check(str(menu.get_node("MainPanel/ProjectInformation/ProductDescription").text).contains("Release XXVIII: Boiorix"), "the release line")
	check(str(menu.get_node("MainPanel/ProductBuild").text).begins_with("Build: "), "the build string: " + str(menu.get_node("MainPanel/ProductBuild").text))
	menu.community_buttons[0].pressed.emit()
	await frames(2)
	var url_box: Node = menu.find_child("MsgBox", true, false)
	check(menu.opened_url == "https://play0ad.com/" and url_box != null and url_box.title == "Opening page", "Website opens play0ad.com, saying so")
	if url_box:
		url_box._answer(0)
	await frames(2)

	print("2. Submenus")
	var learn: Array = items(menu, "learn").map(func(b): return b.text)
	check(learn.has("Structure Tree") and learn.has("Map Overview"), "Learn to Play: " + str(learn))
	var st: Button = items(menu, "learn").filter(func(b): return b.text == "Structure Tree")[0]
	check(st.tooltip_text.ends_with(": View the structure tree of civilizations featured in 0 A.D.") and not st.tooltip_text.begins_with("%"), "Structure Tree names its hotkey: " + st.tooltip_text)
	var single: Array = items(menu, "single").map(func(b): return b.text)
	check(single.slice(0, 2) == ["Matches", "Load Game"] and single.has("Replays"), "Single-player: " + str(single))
	var multi: Array = items(menu, "multi").map(func(b): return b.text)
	check(multi == ["Game Lobby", "Connect by IP", "Host New Game", "Host Saved Game", "Replays"], "Multiplayer: " + str(multi))
	var settings: Array = items(menu, "settings").map(func(b): return b.text)
	check(settings == ["Options", "Hotkeys", "Language", "Mod Selection", "Welcome Screen"], "Settings: " + str(settings))

	print("3. Hotkeys")
	menu._execute_action("open_hotkeys")
	await frames(4)
	var editors: Array = menu.find_children("*", "CanvasLayer", true, false).filter(func(l): return l.get_child_count() > 0 and l.get_child(0).get_script() != null and l.get_child(0).get_script().resource_path.ends_with("hotkeys.gd"))
	check(editors.size() == 1 and current_scene == menu, "Settings > Hotkeys opens the hotkey editor over the menu")
	for layer in menu.find_children("*", "CanvasLayer", true, false):
		layer.queue_free()
	await frames(2)

	print("4. Exit")
	menu._on_exit_pressed()
	await frames(3)
	var box = menu.find_child("MsgBox", true, false)
	check(box != null and box.get_node("Main/Text").text == "Are you sure you want to quit 0 A.D.?" and box.get_node("Main/Title").text == "Confirmation", "Exit asks first")
	if box:
		check(box.get_node("Main/Button1").text == "No" and box.get_node("Main/Button2").text == "Yes", "No, then Yes")
		box.get_node("Main/Button1").pressed.emit()
		await frames(3)
		check(menu.find_child("MsgBox", true, false) == null and current_scene == menu, "No keeps the menu")
	menu._on_exit_pressed()
	await frames(3)
	await key(KEY_ESCAPE)
	await frames(3)
	check(menu.find_child("MsgBox", true, false) == null and current_scene == menu, "Escape answers No")

	print("5. Timed confirmation")
	var answer := [-1]
	var t = load("res://scenes/common/msgbox.gd").open_timed(menu, 400, 200, "Reverting in %(time)s seconds.", "time", 1500, "Confirm", ["Revert", "Keep"])
	t.answered.connect(func(i): answer[0] = i)
	await frames(2)
	check(t.get_node("Main/Text").text == "Reverting in 2 seconds.", "counts the seconds: " + t.get_node("Main/Text").text)
	var start := Time.get_ticks_msec()
	while answer[0] == -1 and Time.get_ticks_msec() - start < 5000:
		await frames(1)
	check(answer[0] == 0, "answers the first button when time runs out")

	print("6. Multiplayer dialogs")
	var ls: Node = root.get_node("/root/LobbySession")
	var ms: Node = root.get_node("/root/MatchSettings")
	var cfg := RtsoUserConfig.new()
	var kept := {}
	for k in ["multiplayerserver", "multiplayerjoining.port", "multiplayerhosting.port", "playername.multiplayer"]:
		kept[k] = cfg.get_value(k)
	cfg.set_value("multiplayerserver", "192.0.2.7")
	cfg.set_value("multiplayerjoining.port", "")
	cfg.set_value("multiplayerhosting.port", "21000")
	cfg.save()
	var mpname: String = load("res://scenes/menu/lobby.gd").multiplayer_name()
	menu._execute_action("open_mp_join")
	await frames(3)
	var mp = menu.find_child("GameSetupMp", false, false)
	check(mp != null and current_scene == menu, "Connect by IP opens the dialog over the menu")
	if mp:
		var labels: Array = mp.find_children("*", "Label", true, false).map(func(l): return l.text)
		check(labels.has("Multiplayer") and labels.has("Joining an existing game.") and labels.has("Player Name:")
			and labels.has("Server Hostname or IP:") and labels.has("Server Port:"), "pageJoin: " + str(labels))
		check(mp.join_player_name.text == mpname and mp.join_server.text == "192.0.2.7" and mp.join_port.text == "20595"
			and mp.join_port.tooltip_text == "Leave blank to use the default port.",
			"the last server, the default port for none, the multiplayer name")
		check(mp.continue_button.text == "Continue" and mp.cancel_button.text == "Cancel", "Continue and Cancel")
		# gamesetup_mp.js startJoin: a host name that does not resolve is reported at once, and the page stays.
		mp.join_server.text = "no-such-host.invalid"
		var joined_bad: bool = mp.confirm_setup()
		var bad_box: AcceptDialog = mp.find_child("HostErrorDialog", false, false)
		check(not joined_bad and bad_box != null and bad_box.title == "Error"
			and bad_box.dialog_text == "Cannot join game: Failed to connect to server." and current_scene == menu
			and menu.find_child("GameSetupMp", false, false) == mp,
			"an unknown host name: \"Cannot join game: Failed to connect to server.\" and the page stays open")
		if bad_box:
			bad_box.queue_free()
		mp.join_server.text = "192.0.2.7"
		mp.cancel_button.pressed.emit()
		await frames(2)
		check(menu.find_child("GameSetupMp", false, false) == null and current_scene == menu, "Cancel closes it")
	menu._execute_action("open_mp_host")
	await frames(3)
	mp = menu.find_child("GameSetupMp", false, false)
	if mp:
		var labels: Array = mp.find_children("*", "Label", true, false).map(func(l): return l.text)
		check(labels.has("Set up your server to host.") and mp.host_player_name != null and mp.host_server_name == null
			and mp.host_password == null and mp.host_port.text == "21000",
			"pageHost without the lobby: player name and port, no game name or password")
		mp.host_port.text = "80"
		check(not mp.confirm_setup() and mp.host_feedback.text == "Server port number must be between 1024 and 65535.",
			"a port below 1024 is refused")
		mp.host_port.text = ""
		check(not mp.confirm_setup() and current_scene == menu, "and so is none")
		# gamesetup_mp.js startHost: a port that cannot be opened is reported, and the page stays.
		var busy := PacketPeerUDP.new()
		busy.bind(21007)
		mp.host_port.text = "21007"
		var hosted: bool = mp.confirm_setup()
		var err_box: AcceptDialog = mp.find_child("HostErrorDialog", false, false)
		check(not hosted and err_box != null and err_box.title == "Error"
			and err_box.dialog_text == "Cannot host game: Failed to start server." and current_scene == menu
			and menu.find_child("GameSetupMp", false, false) == mp and ms.net_mode != "host",
			"a port already in use: \"Cannot host game: Failed to start server.\" and the page stays open")
		busy.close()
		if err_box:
			err_box.queue_free()
		await key(KEY_ESCAPE)
		await frames(2)
		check(menu.find_child("GameSetupMp", false, false) == null, "Escape cancels")
	else:
		check(false, "Host New Game opens the dialog")
	ls.token = "test-token"
	ls.player_name = "alice"
	menu._execute_action("open_mp_host_saved")
	await frames(3)
	mp = menu.find_child("GameSetupMp", false, false)
	if mp:
		check(mp.host_player_name == null and mp.host_server_name.text == mpname + "'s game" and mp.host_password != null
			and mp.host_password.secret, "signed in: the game's name (\"%(name)s's game\") and a password")
		mp.host_server_name.text = " "
		check(not mp.confirm_setup() and mp.host_feedback.text == "Please enter a valid server name.", "a game needs a name")
		mp.host_server_name.text = "alice's game"
		mp.host_port.text = "21001"
		check(mp.confirm_setup() and ms.net_mode == "host" and ms.net_port == 21001
			and ms.net_player_name == "alice" and ms.net_saved_game
			and str(RtsoUserConfig.new().get_value("multiplayerhosting.port")) == "21001",
			"Host Saved Game: Continue hosts as alice on the port kept in the config, saved game")
		await frames(3)
		var loads: Array = menu.find_children("*", "CanvasLayer", false, false).filter(func(l): return l.get_script() != null and l.get_script().resource_path.ends_with("savegame_dialog.gd"))
		check(loads.size() == 1 and menu.find_child("GameSetupMp", false, false) == null, "and asks for the save (page_loadgame)")
		for l in loads:
			l.closed.emit()
			l.queue_free()
		await frames(2)
		check(ms.net_mode == "" and not ms.net_saved_game, "closing the save page hosts nothing")
	else:
		check(false, "Host Saved Game opens the dialog")
	ls.token = ""
	menu._execute_action("open_mp_join")
	await frames(3)
	mp = menu.find_child("GameSetupMp", false, false)
	var joined := false
	if mp:
		mp.join_player_name.text = "bob"
		mp.join_server.text = "198.51.100.4"
		mp.join_port.text = "21002"
		joined = mp.confirm_setup()
	check(joined and ms.net_mode == "join" and ms.net_ip == "198.51.100.4" and ms.net_port == 21002
		and ms.net_player_name == "bob" and str(RtsoUserConfig.new().get_value("multiplayerserver")) == "198.51.100.4",
		"Continue joins the typed server and keeps it")
	cfg = RtsoUserConfig.new()
	for k in kept:
		cfg.set_value(k, kept[k])
	cfg.save()
	ms.net_mode = ""
	print("PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
