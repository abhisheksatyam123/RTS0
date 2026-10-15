extends SceneTree
# The lobby's buddy list and game list filters as 0 A.D. 0.28 has them:
#   1. BuddyButton: with a player picked in the player list, "Mark as Buddy"
#      adds them to lobby.buddies and turns into "Unmark as Buddy"; oneself
#      cannot be marked; buddies carry "•" in the player list, in the game
#      list (a game they play in or watch, Game.js hasBuddies) and in the
#      game details (gamedescription.js); unmarking the last one keeps ",";
#   2. GameListFilters: Show only open games, map size, map type and player
#      count hide the games that don't match, and "Any" shows them again.
#   4. ChatCommandHandler: /away and /back set the presence (Away, Online),
#      unknown and moderator-only commands print a line and send nothing,
#      /me and /say need an argument, /clear empties the log.
#   5. LobbyGamelistReporter: a running game reports who won or was defeated
#      and who is offline, and the details show it (formatPlayerInfo).
#
#   godot --headless --path . -s tools/e2e_lobby_buddies.gd
# Exit code 0 = pass. Needs python3 for the lobby server.

const PORT := 18095

var failures := 0
var server_pid := -1

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

func names(tree: Tree, col: int) -> Array:
	var out := []
	if tree.get_root():
		for it in tree.get_root().get_children():
			out.append(it.get_text(col))
	return out

func room(id: String, players: int, max_players: int, state: String, size: String, type: String, who: Array) -> Dictionary:
	return {"id": id, "name": id, "host_name": "h", "players": players, "max_players": max_players, "state": state,
		"map_size": size, "map_type": type, "map_name": "Mainland", "player_names": who}

func run() -> void:
	var cfg := RtsoUserConfig.new()
	var saved_buddies: String = cfg.get_value("lobby.buddies")
	cfg.set_value("lobby.buddies", ",")
	cfg.save()
	server_pid = OS.create_process("python3", [ProjectSettings.globalize_path("res://tools/lobby_server.py"),
		"--port", str(PORT), "--quiet", "--stun-port", "0", "--exit-with-parent"])
	var ls: Node = root.get_node("/root/LobbySession")
	var lobby: Control = load("res://scenes/menu/lobby.tscn").instantiate()
	root.add_child(lobby)
	await process_frame
	var signed := [0]
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
	ls.sign_in("127.0.0.1:%d" % PORT, "alice", func(code: int, _o) -> void: signed[0] = code)
	await until(func() -> bool: return signed[0] != 0)
	var bob := [""]
	ls.post_json("/login", {"name": "bob"}, func(code: int, o) -> void:
		bob[0] = str(o.get("token", "")) if code == 200 and o is Dictionary else "-")
	await until(func() -> bool: return bob[0] != "")
	lobby._update_signed_in()

	print("1. buddies")
	lobby._refresh()
	await until(func() -> bool: return names(lobby._players, 1).size() == 2)
	check(names(lobby._players, 1) == ["alice", "bob"], "both players are listed, nobody marked")
	check(lobby._buddy_btn.disabled, "Mark as Buddy needs a player picked")
	for it in lobby._players.get_root().get_children():
		if str(it.get_metadata(0)) == "alice":
			it.select(0)
	check(lobby._buddy_btn.disabled, "not oneself")
	for it in lobby._players.get_root().get_children():
		if str(it.get_metadata(0)) == "bob":
			it.select(0)
	check(not lobby._buddy_btn.disabled and lobby._buddy_btn.text == "Mark as Buddy", "bob can be marked")
	lobby._buddy_btn.pressed.emit()
	await until(func() -> bool: return names(lobby._players, 1).has("• bob"))
	cfg.reload()
	check(cfg.get_value("lobby.buddies") == "bob" and lobby._buddy_btn.text == "Unmark as Buddy",
		"bob is kept in lobby.buddies (%s)" % cfg.get_value("lobby.buddies"))
	check(names(lobby._players, 1) == ["alice", "• bob"], "and marked with • in the player list")

	lobby._rooms = [room("with bob", 2, 2, "init", "Normal", "random", [{"name": "alice"}, {"name": "bob", "team": 0}]),
		room("bob watches", 1, 4, "running", "Small", "skirmish", [{"name": "carol"}, {"name": "bob", "observer": true}]),
		room("AI bob", 2, 4, "init", "Normal", "skirmish", [{"name": "dave"}, {"name": "bob", "ai": true}])]
	lobby._show_rooms()
	check(names(lobby._games, 0) == ["AI bob", "• with bob", "• bob watches"],
		"games with bob playing or watching are marked, an AI named bob is not: %s" % [names(lobby._games, 0)])
	check(lobby.room_buddies(lobby._rooms[0], lobby.buddies()) == 2 and lobby.room_buddies(lobby._rooms[1], lobby.buddies()) == 1,
		"a playing buddy ranks above a watching one (Game.js hasBuddies)")
	check(ls.game_details(lobby._rooms[0], {}).contains("• bob ("), "the game details mark bob")

	print("2. filters")
	lobby._open_filter.button_pressed = true
	check(names(lobby._games, 0) == ["AI bob"], "open games: in setup with a free slot")
	lobby._open_filter.button_pressed = false
	lobby._size_filter.select(2) # Small
	lobby._size_filter.item_selected.emit(2)
	check(names(lobby._games, 0) == ["• bob watches"], "map size Small")
	lobby._size_filter.select(0)
	lobby._type_filter.select(2) # Random
	lobby._type_filter.item_selected.emit(2)
	check(names(lobby._games, 0) == ["• with bob"], "map type Random")
	lobby._type_filter.select(0)
	lobby._count_filter.select(4) # 4 players
	lobby._count_filter.item_selected.emit(4)
	check(names(lobby._games, 0) == ["AI bob", "• bob watches"], "4 player slots")
	lobby._count_filter.select(0)
	lobby._count_filter.item_selected.emit(0)
	check(names(lobby._games, 0).size() == 3, "Any shows every game again")

	print("2b. sorting and colours (GameList.js, Game.js)")
	check(lobby._games.get_column_title(0) == "Name  ▲" and lobby._games.get_column_title(1) == "Map Name"
		and lobby._games.get_column_title(2) == "Size" and lobby._games.get_column_title(3) == "Type",
		"0.28's columns, sorted on Name: " + lobby._games.get_column_title(0))
	var rows: Array = lobby._games.get_root().get_children()
	check(rows[0].get_custom_color(0) == Color8(0, 219, 0) and rows[2].get_custom_color(0) == Color8(219, 0, 0),
		"setups listed first in green, running games in red")
	check(rows[2].get_text(2) == "Small" and rows[2].get_text(3) == "Skirmish" and rows[2].get_text(4) == "1/4 +1"
		and rows[1].get_text(3) == "Random" and rows[1].get_text(4) == "2/2", "size, type and the observer count \"1/4 +1\"")
	lobby._games.column_title_clicked.emit(4, MOUSE_BUTTON_LEFT) # Players
	check(names(lobby._games, 0) == ["• with bob", "AI bob", "• bob watches"] and lobby._games.get_column_title(4) == "Players  ▲",
		"a heading click sorts on it: %s" % [names(lobby._games, 0)])
	lobby._games.column_title_clicked.emit(2, MOUSE_BUTTON_LEFT) # Size
	lobby._games.column_title_clicked.emit(2, MOUSE_BUTTON_LEFT)
	check(names(lobby._games, 0)[2] == "• bob watches" and lobby._games.get_column_title(2) == "Size  ▼",
		"a second click reverses it: %s" % [names(lobby._games, 0)])
	lobby._games.column_title_clicked.emit(0, MOUSE_BUTTON_LEFT)

	print("3. unmark")
	lobby._buddy_btn.pressed.emit()
	await until(func() -> bool: return names(lobby._players, 1).has("bob"))
	cfg.reload()
	check(cfg.get_value("lobby.buddies") == "," and lobby._buddy_btn.text == "Mark as Buddy", "an empty list is kept as \",\"")
	check(not lobby.toggle_buddy("a,b", "alice") and not lobby.toggle_buddy("alice", "alice"), "no delimiter, not oneself")

	print("4. chat commands")
	var state := func(who: String) -> String:
		for it in lobby._players.get_root().get_children():
			if str(it.get_metadata(0)) == who:
				return it.get_text(0)
		return ""
	var send := func(t: String) -> void:
		lobby._chat_line.text = t
		lobby._on_send_pressed()
	send.call("/away")
	await until(func() -> bool: return state.call("alice") == "Away")
	check(state.call("alice") == "Away" and ls.away, "/away shows alice as Away")
	ls._beat()
	await create_timer(0.3).timeout
	lobby._refresh()
	await until(func() -> bool: return state.call("alice") == "Away", 30)
	check(state.call("alice") == "Away", "and the heartbeat keeps it")
	send.call("/back")
	await until(func() -> bool: return state.call("alice") == "Online")
	check(state.call("alice") == "Online" and not ls.away, "/back shows alice as Online")
	send.call("/dance")
	check(lobby._chat_log.get_parsed_text().contains("The command 'dance' is not supported. Try /help"), "an unknown command is refused")
	send.call("/kick bob")
	check(lobby._chat_log.get_parsed_text().contains("The command 'kick' is restricted to moderators."), "kick is for moderators")
	send.call("/me")
	check(lobby._chat_log.get_parsed_text().contains("The command '/me' requires at least one argument."), "/me needs an argument")
	send.call("/help")
	var log: String = lobby._chat_log.get_parsed_text()
	check(log.contains("away - Set your state to 'Away'.") and not log.contains("ban - "), "/help lists the commands a player may use")
	send.call("/clear")
	check(lobby._chat_log.get_parsed_text().strip_edges() == "", "/clear empties the log")
	send.call("/me waves")
	await until(func() -> bool: return lobby._chat_log.get_parsed_text().contains("* alice waves"))
	check(lobby._chat_log.get_parsed_text().contains("* alice waves"), "/me shows as * alice waves")

	print("5. player states")
	var ms: Node = root.get_node("/root/MatchSettings")
	ms.slot_types = [0, 0, 1]
	ms.slot_civs = [0, 0, 0]
	var det: Dictionary = ls.room_details(ms, {"slots": [{"name": "alice", "state": "won"},
		{"name": "bob", "state": "defeated", "offline": true}, {"state": "defeated"}]}, 0)
	var ps: Array = det["player_names"]
	check(ps.size() == 3 and ps[0].get("state") == "won" and ps[1].get("offline") == true and ps[1].get("state") == "defeated"
		and ps[2].get("state") == "defeated" and not ps[2].has("offline"), "the report carries state and offline: %s" % [ps])
	var text: String = ls.game_details({"name": "g", "player_names": ps}, {})
	check(text.contains("alice (") and text.contains(", won)"), "a winner reads (civ, won)")
	check(text.contains("bob (OFFLINE, defeated)"), "an offline loser reads (OFFLINE, defeated): " + text.replace("\n", " | "))

	ls.sign_out()
	cfg.set_value("lobby.buddies", saved_buddies)
	cfg.save()
	OS.kill(server_pid)
	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
