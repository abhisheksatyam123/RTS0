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
#   5. QuitButton.returnToMainMenu: leaving the lobby for the main menu signs
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
	run.call_deferred()

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
		"--port", str(PORT), "--quiet", "--stun-port", "0"])
	var ls: Node = root.get_node("/root/LobbySession")
	var lobby := await open_lobby()
	lobby._server.text = "127.0.0.1:%d" % PORT
	lobby._name.text = "alice"
	for attempt in 50:
		lobby._on_sign_pressed()
		await until(func() -> bool: return ls.signed_in() or not lobby._sign_btn.disabled, 60)
		if ls.signed_in():
			break
		await create_timer(0.1).timeout
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

	print("5. back to the main menu signs out")
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
