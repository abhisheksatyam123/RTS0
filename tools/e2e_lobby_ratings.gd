extends SceneTree
# Lobby ratings as 0 A.D. 0.28 has them:
#   1. LobbyRatingReporter: a player who won or lost a lobby game of two human
#      players reports it once; AI games, team games, observers and a game
#      still running report nothing;
#   2. against tools/lobby_server.py: once both players have reported, the
#      lobby's player list shows each rating, selecting a player fills the
#      profile panel (rank, highest rating, games, wins, losses, win rate) and
#      the Leaderboard lists the rated players best first.
#
#   godot --headless --path . -s tools/e2e_lobby_ratings.gd
# Exit code 0 = pass. Needs python3 for the lobby server.

const PORT := 18093
const LobbySessionScript = preload("res://scenes/menu/lobby_session.gd")

var failures := 0
var server_pid := -1

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func until(cond: Callable, max_frames := 600) -> bool:
	for i in max_frames:
		if cond.call():
			return true
		await process_frame
	return cond.call()

func player(id: int, name: String, state: String, ai := false) -> Dictionary:
	return {"id": id, "name": name, "state": state, "ai": ai, "civ_name": "Athenians", "team": 1,
		"economyScore": 10, "militaryScore": 20, "totalScore": 30, "stats": {"tradeIncome": 5}}

func run() -> void:
	print("1. the report")
	var summary := {"seed": 42, "map_id": "random_mainland"}
	var stats := {"map_name": "Mainland", "game_time": 600.0,
		"players": [player(0, "alice", "won"), player(1, "bob", "defeated")]}
	var r: Dictionary = LobbySessionScript.rating_report(stats, summary, 1)
	check(r.get("players") == "alice,bob," and r.get("playerStates") == "won,defeated," and r.get("playerID") == 2,
		"a finished 1v1 reports its players and their states")
	check(r.get("matchID") == "42-random_mainland-alice,bob" and r.get("timeElapsed") == 600000,
		"both players name the match alike (seed, map, players)")
	check(r.get("tradeIncome") == "5,5,", "the Misc counters ride along")
	var other: Dictionary = LobbySessionScript.rating_report(stats, summary, 0)
	check(other.get("matchID") == r.get("matchID"), "the winner sends the same match id")
	stats.players[1].state = "active"
	check(LobbySessionScript.rating_report(stats, summary, 1).is_empty(), "nothing while the player still plays")
	stats.players[1].state = "defeated"
	stats.players[1].ai = true
	check(LobbySessionScript.rating_report(stats, summary, 0).is_empty(), "a game against the AI is not rated")
	stats.players[1].ai = false
	stats.players.append(player(2, "carol", "defeated"))
	check(LobbySessionScript.rating_report(stats, summary, 0).is_empty(), "only 1v1 games are rated")
	check(LobbySessionScript.rated_name("alice", 1278) == "alice (1278)" and LobbySessionScript.rated_name("bob", null) == "bob",
		"names show their rating (PlayerColor.ColorPlayerName)")
	check(LobbySessionScript.format_win_rate({"totalGamesPlayed": 3, "wins": 2}) == "66.67%"
		and LobbySessionScript.format_win_rate({}) == "-", "the win rate as ProfilePanel.FormatWinRate")

	print("2. the lobby")
	var out := []
	server_pid = OS.create_process("python3", [ProjectSettings.globalize_path("res://tools/lobby_server.py"),
		"--port", str(PORT), "--quiet"])
	check(server_pid > 0, "the lobby server starts")
	var ls: Node = root.get_node("/root/LobbySession")
	var signed := [0]
	for attempt in 50:
		ls.sign_in("127.0.0.1:%d" % PORT, "alice", func(code: int, _o) -> void: signed[0] = code)
		await until(func() -> bool: return signed[0] != 0 or false, 30)
		if signed[0] == 200:
			break
		await create_timer(0.1).timeout
	check(signed[0] == 200, "alice signs in")
	var bob_token := [""]
	ls.post_json("/login", {"name": "bob"}, func(code: int, o) -> void:
		bob_token[0] = str(o.get("token", "")) if code == 200 and o is Dictionary else "-")
	await until(func() -> bool: return bob_token[0] != "")
	var rep := r.duplicate()
	var rated := [0, 0]
	rep["token"] = ls.token
	ls.post_json("/report", rep, func(code: int, _o) -> void: rated[0] = code)
	await until(func() -> bool: return rated[0] != 0)
	rep["token"] = bob_token[0]
	ls.post_json("/report", rep, func(code: int, _o) -> void: rated[1] = code)
	await until(func() -> bool: return rated[1] != 0)
	check(rated == [202, 200], "the lobby rates the game once both have reported")

	var lobby: Control = load("res://scenes/menu/lobby.tscn").instantiate()
	root.add_child(lobby)
	await until(func() -> bool: return lobby._players.get_root() != null and lobby._players.get_root().get_child_count() == 2)
	var ratings := {}
	for it in lobby._players.get_root().get_children():
		ratings[it.get_text(1)] = it.get_text(2)
	check(ratings == {"alice": "1278", "bob": "1122"}, "the player list's Rating column (%s)" % str(ratings))
	var bob_item: TreeItem = lobby._players.get_root().get_child(1)
	bob_item.select(0)
	await until(func() -> bool: return lobby._profile_values["totalGamesPlayed"].text == "1")
	check(lobby._profile.visible and lobby._profile_name.text == "bob (1122)", "selecting bob shows his profile")
	check(lobby._profile_values["rank"].text == "2" and lobby._profile_values["highestRating"].text == "1200"
		and lobby._profile_values["wins"].text == "0" and lobby._profile_values["losses"].text == "1"
		and lobby._profile_values["ratio"].text == "0.00%", "rank, highest rating, wins, losses and win rate")
	lobby._on_leaderboard_pressed()
	await until(func() -> bool: return lobby._board.get_root() != null and lobby._board.get_root().get_child_count() == 2)
	var board := []
	if lobby._board.get_root():
		for it in lobby._board.get_root().get_children():
			board.append([it.get_text(0), it.get_text(1), it.get_text(2)])
	check(board == [["1", "alice", "1278"], ["2", "bob", "1122"]], "the Leaderboard, best first")

	ls.sign_out()
	OS.kill(server_pid)
	print("RESULT: " + ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
