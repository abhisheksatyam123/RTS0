extends SceneTree
# The lobby's buddy list and game list filters as 0 A.D. 0.28 has them:
#   1. BuddyButton: with a player picked in the player list, "Mark as Buddy"
#      adds them to lobby.buddies and turns into "Unmark as Buddy"; oneself
#      cannot be marked; buddies carry "•" in the player list, in the game
#      list (a game they play in or watch, Game.js hasBuddies) and in the
#      game details (gamedescription.js); unmarking the last one keeps ",";
#   2. GameListFilters: Show only open games, map size, map type and player
#      count hide the games that don't match, and "Any" shows them again.
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

func until(cond: Callable, max_frames := 600) -> bool:
	for i in max_frames:
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
		"--port", str(PORT), "--quiet", "--stun-port", "0"])
	var ls: Node = root.get_node("/root/LobbySession")
	var lobby: Control = load("res://scenes/menu/lobby.tscn").instantiate()
	root.add_child(lobby)
	await process_frame
	var signed := [0]
	for attempt in 50:
		ls.sign_in("127.0.0.1:%d" % PORT, "alice", func(code: int, _o) -> void: signed[0] = code)
		await until(func() -> bool: return signed[0] != 0, 30)
		if signed[0] == 200:
			break
		signed[0] = 0
		await create_timer(0.1).timeout
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
	check(names(lobby._games, 0) == ["• with bob", "• bob watches", "AI bob"],
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
	check(names(lobby._games, 0) == ["• bob watches", "AI bob"], "4 player slots")
	lobby._count_filter.select(0)
	lobby._count_filter.item_selected.emit(0)
	check(names(lobby._games, 0).size() == 3, "Any shows every game again")

	print("3. unmark")
	lobby._buddy_btn.pressed.emit()
	await until(func() -> bool: return names(lobby._players, 1).has("bob"))
	cfg.reload()
	check(cfg.get_value("lobby.buddies") == "," and lobby._buddy_btn.text == "Mark as Buddy", "an empty list is kept as \",\"")
	check(not lobby.toggle_buddy("a,b", "alice") and not lobby.toggle_buddy("alice", "alice"), "no delimiter, not oneself")

	ls.sign_out()
	cfg.set_value("lobby.buddies", saved_buddies)
	cfg.save()
	OS.kill(server_pid)
	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
