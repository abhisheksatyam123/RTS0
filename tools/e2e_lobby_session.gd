extends SceneTree
# The lobby session's life as 0 A.D. 0.28 has it:
#   1. XmppClient keeps the session's chat: a lobby page opened again (after a
#      match, or from the game setup) shows what was said, also while it was
#      closed (LobbyHandler replays the messages);
#   2. ConnectionHandler: when the lobby forgets our session, the page says
#      "Disconnected." and asks "You have been disconnected from the lobby. Do
#      you want to reconnect?"; Yes signs in again ("Connected.");
#   3. JoinButton: a running game the player is not in asks before joining
#      to watch;
#   4. PlayerList, ChatInputPanel, PlayerColor: "Search for Player" picks
#      a player on Enter, Tab completes names there and in the chat line,
#      picking a player selects the game they are in, each name has its own
#      colour and our name is highlighted in others' lines;
#   5. GameDetails: a selected game shows its map's preview and the map
#      description of any known map, not only random ones;
#   6. Game.js, GameDetails, JoinButton: a game needing other mods is grey
#      and listed first, its details say "Mods (incompatible):", and Join
#      offers the mod selection instead (signing out);
#   7. QuitButton.returnToMainMenu: leaving the lobby for the main menu signs
#      out (StopXmppClient).
#
#   godot --headless --path . -s tools/e2e_lobby_session.gd
# Exit code 0 = pass. Needs python3 for the lobby server.

const PORT := 18098

var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	preload("res://tools/lobby_test_clock.gd").run_in_wall_time(self, "res://tools/e2e_lobby_session.gd", run)

# max_frames counts frames at 60 fps of real time: headless --fixed-fps runs
# frames unthrottled, and the lobby's HTTP answers take wall-clock time.
func until(cond: Callable, max_frames := 600) -> bool:
	var deadline := Time.get_ticks_msec() + max_frames * 1000 / 60
	while Time.get_ticks_msec() < deadline:
		if cond.call():
			return true
		await process_frame
	return cond.call()

func MatchSettings_mode() -> String:
	return str(root.get_node("/root/MatchSettings").net_mode)

func open_lobby() -> Control:
	var lobby: Control = load("res://scenes/menu/lobby.tscn").instantiate()
	root.add_child(lobby)
	await process_frame
	return lobby

func run() -> void:
	var server_pid := OS.create_process("python3", [ProjectSettings.globalize_path("res://tools/lobby_server.py"),
		"--port", str(PORT), "--quiet", "--stun-port", "0", "--exit-with-parent"])
	var ls: Node = root.get_node("/root/LobbySession")
	var lobby := await open_lobby()
	lobby._server.text = "127.0.0.1:%d" % PORT
	lobby._name.text = "alice"
	# Wait for the server to answer, then sign in once: retrying a sign-in that
	# is slow to answer (a loaded machine) leaves the first one signed in on the
	# server and the retries refused as "that name is in use".
	ls.address = "127.0.0.1:%d" % PORT
	var up := [false]
	for attempt in 100:
		ls.get_json("/players", func(code: int, _o) -> void: up[0] = up[0] or code == 200)
		await until(func() -> bool: return up[0], 30)
		if up[0]:
			break
		await create_timer(0.1).timeout
	lobby._on_sign_pressed()
	await until(func() -> bool: return ls.signed_in())
	check(ls.signed_in(), "alice signs in on the lobby page")
	var bob := [""]
	ls.post_json("/login", {"name": "bob"}, func(code: int, o) -> void:
		bob[0] = str(o.get("token", "")) if code == 200 and o is Dictionary else "-")
	await until(func() -> bool: return bob[0] != "")
	var say := func(text: String) -> void:
		var sent := [false]
		ls.post_json("/chat", {"token": bob[0], "text": text}, func(_c: int, _o) -> void: sent[0] = true)
		await until(func() -> bool: return sent[0])

	print("1. the session's chat outlives the page")
	await say.call("hi alice")
	lobby._refresh()
	await until(func() -> bool: return lobby._chat_log.get_parsed_text().contains("bob: hi alice"))
	check(lobby._chat_log.get_parsed_text().contains("bob: hi alice"), "the page shows bob's line")
	# XmppMessages.js: lobby messages arrive pushed. With the fallback timer stopped,
	# /events alone brings bob's next line at once.
	lobby._timer.stop()
	var t0 := Time.get_ticks_msec()
	await say.call("pushed")
	await until(func() -> bool: return lobby._chat_log.get_parsed_text().contains("bob: pushed"), 120)
	var took := Time.get_ticks_msec() - t0
	check(lobby._chat_log.get_parsed_text().contains("bob: pushed") and took < 1500,
		"a line shows without waiting for the 3 s refresh (%d ms)" % took)
	lobby._timer.start()
	lobby.queue_free()
	await process_frame
	await say.call("are you there?")
	lobby = await open_lobby()
	await until(func() -> bool: return lobby._chat_log.get_parsed_text().contains("are you there?"))
	var text: String = lobby._chat_log.get_parsed_text()
	check(text.contains("bob: hi alice") and text.contains("bob: are you there?"),
		"a page opened again shows the line from before and the one said while it was closed")
	check(text.find("hi alice") < text.find("are you there?") and text.count("hi alice") == 1, "once each, in order")

	print("2. a lost session asks to reconnect")
	var gone := [false]
	ls.post_json("/logout", {"token": ls.token}, func(_c: int, _o) -> void: gone[0] = true) # the lobby forgets us
	await until(func() -> bool: return gone[0])
	ls._beat()
	await until(func() -> bool: return lobby.find_child("ReconnectDialog", true, false) != null)
	var dlg: ConfirmationDialog = lobby.find_child("ReconnectDialog", true, false)
	check(dlg != null and dlg.dialog_text == "You have been disconnected from the lobby. Do you want to reconnect?",
		"the page asks to reconnect")
	check(not ls.signed_in() and lobby._chat_log.get_parsed_text().contains("Disconnected."), "and says Disconnected.")
	if dlg:
		dlg.confirmed.emit()
	await until(func() -> bool: return ls.signed_in() and lobby._chat_log.get_parsed_text().contains("Connected."))
	check(ls.signed_in() and ls.player_name == "alice", "Yes signs alice in again")
	check(lobby._chat_log.get_parsed_text().contains("Connected."), "and the page says Connected.")

	print("3. a running game asks before joining to watch")
	var reg := [0]
	ls.post_json("/rooms", {"token": bob[0], "id": "room-29999", "host_ip": "127.0.0.1", "host_port": 29999,
		"name": "bob's game", "state": "running", "player_names": [{"name": "bob"}]}, func(c: int, _o) -> void: reg[0] = c)
	await until(func() -> bool: return reg[0] != 0)
	lobby._refresh()
	await until(func() -> bool: return lobby._games.get_root() != null and lobby._games.get_root().get_child_count() > 0)
	var row: TreeItem = lobby._games.get_root().get_child(0) if lobby._games.get_root() else null
	if row:
		row.select(0)
		await process_frame
	lobby._observe.button_pressed = false
	lobby._on_join_pressed()
	var ask: ConfirmationDialog = lobby.find_child("ObserveDialog", true, false)
	check(ask != null and ask.dialog_text == "The game has already started. Do you want to join as observer?",
		"Join asks whether to watch a game alice is not in")
	check(MatchSettings_mode() == "", "and joins nothing until she answers")
	if ask:
		ask.canceled.emit()
	await process_frame
	check(MatchSettings_mode() == "" and lobby.find_child("ObserveDialog", true, false) == null, "No joins nothing")

	print("4. player search, Tab completion, name colours")
	var L: GDScript = load("res://scenes/menu/lobby.gd")
	check(L.player_color("alice").to_html(false) == "6bedfa" and L.player_color("bob").to_html(false) == "6667ff"
		and L.player_color("Xx_Long.Player-Name_99").to_html(false) == "e78bec", "names get 0.28's colours (GetPlayerColor)")
	check(L.autocomplete("hello AL", ["alice", "bob"]) == "hello alice " and L.autocomplete("b", ["alice", "bob"]) == "bob, ",
		"Tab completes the last word, with \", \" after a lone name")
	check(L.autocomplete("al", ["alice", "albert"]) == "al" and L.autocomplete("hi ", ["alice"]) == "hi ",
		"and leaves an ambiguous or empty word alone")
	var games := [{"player_names": [{"name": "carl", "offline": true}]}, {"player_names": [{"name": "carl", "observer": true}]},
		{"player_names": [{"name": "carl (1500)"}]}, {"player_names": [{"name": "dora", "observer": true}]}]
	check(L.game_of_player(games, "carl") == 2 and L.game_of_player(games.slice(0, 2), "carl") == 1
		and L.game_of_player(games.slice(0, 1), "carl") == 0 and L.game_of_player(games, "dora") == 3
		and L.game_of_player(games, "eve") == -1, "a player's game: playing, else watching, else offline")
	lobby._refresh()
	await until(func() -> bool: return lobby._online_names().has("bob"))
	lobby._games.deselect_all()
	lobby._players_filter.text = "b"
	lobby._players_filter.caret_column = 1
	var tab := InputEventKey.new()
	tab.keycode = KEY_TAB
	tab.pressed = true
	lobby._on_name_input(tab, lobby._players_filter)
	check(lobby._players_filter.text == "bob, ", "Tab in Search for Player completes bob")
	lobby._players_filter.text_submitted.emit("bob")
	await process_frame
	check(lobby._selected_player == "bob", "Enter picks him in the player list")
	var picked: TreeItem = lobby._games.get_selected()
	check(picked != null and str(picked.get_metadata(0)) == "room-29999", "and selects the game he is in")
	var pcol: Color = lobby._players.get_selected().get_custom_color(1) if lobby._players.get_selected() else Color.BLACK
	check(pcol.to_html(false) == "6667ff", "his name is drawn in his colour")
	lobby._chat_line.text = "hi Bo"
	lobby._chat_line.caret_column = 5
	lobby._on_name_input(tab, lobby._chat_line)
	check(lobby._chat_line.text == "hi bob ", "Tab in the chat line completes him too")
	lobby._chat_line.text = ""
	await say.call("hello alice")
	lobby._refresh()
	await until(func() -> bool: return lobby._chat_log.get_parsed_text().contains("hello alice"))
	check(lobby._format_chat("bob", "hello alice", "alice") == "[color=#6667ff]bob[/color]: hello [color=#6bedfa]alice[/color]",
		"bob's line has his name in his colour and alice's highlighted")
	check(lobby._format_chat("alice", "alice here", "alice") == "[color=#6bedfa]alice[/color]: alice here"
		and lobby._format_chat("", "alice left", "alice") == "[color=#aaaaaa]alice left[/color]",
		"but not in her own lines nor in system lines")
	check(lobby._format_chat("bob", "/me waves [b]", "alice") == "* [b][color=#6667ff]bob[/color][/b] waves [lb]b]",
		"/me lines too, escaped")

	print("5. the game details show the map")
	reg[0] = 0
	ls.post_json("/rooms", {"token": bob[0], "id": "room-29998", "host_ip": "127.0.0.1", "host_port": 29998,
		"name": "bay game", "state": "init", "map_id": "acropolis_bay", "map_name": "Acropolis Bay",
		"map_type": "skirmish", "player_names": [{"name": "bob"}]}, func(c: int, _o) -> void: reg[0] = c)
	await until(func() -> bool: return reg[0] != 0)
	lobby._refresh()
	var find_room := func(id: String) -> TreeItem:
		if lobby._games.get_root():
			for it in lobby._games.get_root().get_children():
				if str(it.get_metadata(0)) == id:
					return it
		return null
	# Look a row up right before picking it: the 3 s refresh rebuilds the rows.
	var pick_room := func(id: String) -> void:
		var it: TreeItem = find_room.call(id)
		if it:
			it.select(0)
	await until(func() -> bool: return find_room.call("room-29998") != null)
	pick_room.call("room-29998")
	await process_frame
	# map_preview is null in an unimported headless project; the details show what Game Setup shows.
	var want: Texture2D = load("res://scenes/menu/gamesetup.gd").map_preview("acropolis_bay")
	check(lobby._details_preview.texture == want and lobby._details_preview.visible == (want != null),
		"a skirmish map's game shows Game Setup's preview of it")
	check(lobby._details.get_parsed_text().contains("Map Description: Each player starts the match atop a large flat plateau"),
		"and its description")
	pick_room.call("room-29999")
	await process_frame
	check(not lobby._details_preview.visible, "a game with no known map shows none")

	print("6. mod compatibility")
	var M: GDScript = load("res://scenes/menu/mods.gd")
	var ours: Array = M.engine_mods()
	check(ours.size() >= 1 and ours[0]["name"] == "0ad" and ours[0]["version"] == "0.28.0", "we play 0ad (0.28.0): %s" % [ours])
	var fx := {"name": "fx", "version": "1", "ignoreInCompatibilityChecks": true}
	check(M.play_compatible([ours[0], fx], [ours[0]]) and not M.play_compatible([{"name": "0ad", "version": "0.27.1"}], [ours[0]])
		and not M.play_compatible([ours[0], {"name": "x", "version": "1"}], [ours[0]]),
		"same mods and versions, skipping the ones that do not affect compatibility")
	check(M.mod_to_string(ours[0]) == "0ad (0.28.0)" and M.mod_to_string(fx) == "fx", "mods read \"name (version)\"")
	for g in [["room-29997", "old mods", [{"name": "0ad", "version": "0.27.1"}]], ["room-29996", "same mods", [ours[0], fx]]]:
		reg[0] = 0
		ls.post_json("/rooms", {"token": bob[0], "id": g[0], "host_ip": "127.0.0.1", "host_port": int(g[0].substr(5)),
			"name": g[1], "state": "init", "mods": g[2], "player_names": [{"name": "bob"}]}, func(c: int, _o) -> void: reg[0] = c)
		await until(func() -> bool: return reg[0] != 0)
	lobby._refresh()
	await until(func() -> bool: return find_room.call("room-29997") != null and find_room.call("room-29996") != null)
	var old: TreeItem = find_room.call("room-29997")
	check(old != null and old.get_index() == 0 and old.get_custom_color(0) == Color8(128, 128, 128),
		"a game needing other mods is listed first, in grey")
	var same: TreeItem = find_room.call("room-29996")
	check(same != null and same.get_custom_color(0) == Color8(0, 219, 0), "a game with our mods is green")
	pick_room.call("room-29996")
	await process_frame
	check(lobby._details.get_parsed_text().contains("Mods:\n0ad (0.28.0)\nfx"), "its details list the mods, fx last")
	pick_room.call("room-29997")
	await process_frame
	check(lobby._details.get_parsed_text().contains("Mods (incompatible):\n0ad (0.27.1)"), "the other one's say incompatible")
	lobby._on_join_pressed()
	var mods_ask: ConfirmationDialog = lobby.find_child("IncompatibleModsDialog", true, false)
	check(mods_ask != null and mods_ask.dialog_text == "Your active mods do not match the mods of this game.\n\n"
		+ "Required: 0ad (0.27.1)\nActive: " + ", ".join(ours.map(func(m) -> String: return M.mod_to_string(m)))
		+ "\n\nDo you want to switch to the mod selection page?", "Join offers the mod selection")
	check(MatchSettings_mode() == "" and lobby.find_child("ObserveDialog", true, false) == null, "and joins nothing")
	if mods_ask:
		mods_ask.confirmed.emit()
	await process_frame
	check(not ls.signed_in() and lobby.find_child("ModSelection", true, false) != null, "Yes signs out and opens it")
	var page: Node = lobby.find_child("ModSelection", true, false)
	if page:
		page.visible = false
	lobby._on_sign_pressed()
	await until(func() -> bool: return ls.signed_in())

	print("6b. a password game asks for it before joining")
	reg[0] = 0
	ls.post_json("/rooms", {"token": bob[0], "id": "room-29995", "host_ip": "127.0.0.1", "host_port": 29995,
		"name": "locked game", "state": "init", "password": true, "player_names": [{"name": "bob"}]},
		func(c: int, _o) -> void: reg[0] = c)
	await until(func() -> bool: return reg[0] != 0)
	lobby._refresh()
	await until(func() -> bool: return find_room.call("room-29995") != null)
	lobby._join_pwd.text = "row text"
	pick_room.call("room-29995")
	await process_frame
	lobby._on_join_pressed()
	var pw_dlg: ConfirmationDialog = lobby.find_child("PasswordDialog", true, false)
	var pw_edit: LineEdit = pw_dlg.find_child("ClientPassword", true, false) if pw_dlg else null
	check(pw_dlg != null and pw_dlg.ok_button_text == "Confirm" and pw_edit != null and pw_edit.secret and pw_edit.max_length == 256,
		"Join on a password game opens the password page (masked, 256, Confirm)")
	check(MatchSettings_mode() == "", "and sends nothing yet")
	if pw_dlg:
		pw_dlg.canceled.emit()
	await process_frame
	check(MatchSettings_mode() == "" and lobby.find_child("PasswordDialog", true, false) == null, "Cancel joins nothing")
	pick_room.call("room-29995")
	await process_frame
	lobby._on_join_pressed()
	pw_dlg = lobby.find_child("PasswordDialog", true, false)
	pw_edit = pw_dlg.find_child("ClientPassword", true, false) if pw_dlg else null
	if pw_edit:
		pw_edit.text = "secret"
		pw_dlg.confirmed.emit()
	var ms6 = root.get_node("/root/MatchSettings")
	await until(func() -> bool: return MatchSettings_mode() == "join")
	check(MatchSettings_mode() == "join" and ms6.net_password == "secret" and ms6.net_port == 29995,
		"Confirm joins with the typed password, not the Join row's (%s)" % ms6.net_password)
	# Stay on the lobby: drop the match the join opened.
	ms6.net_mode = ""
	await process_frame
	if current_scene:
		current_scene.queue_free()
	await process_frame
	lobby._host_port.text = "80"
	lobby._on_host_pressed()
	check(MatchSettings_mode() == "" and lobby._status.text.contains("Server port number must be between 1024 and 65535."),
		"the Host row refuses a port below 1024: " + lobby._status.text)

	print("7. back to the main menu signs out")
	lobby._on_back_pressed()
	check(not ls.signed_in(), "leaving the lobby signs alice out")
	var listed := [null]
	await create_timer(0.5).timeout
	ls.get_json("/players", func(_c: int, o) -> void: listed[0] = o)
	await until(func() -> bool: return listed[0] != null)
	var who := []
	for p in (listed[0] as Dictionary).get("players", []):
		who.append(str(p.get("name", "")))
	check(not who.has("alice"), "and the lobby no longer lists her: %s" % [who])

	OS.kill(server_pid)
	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
