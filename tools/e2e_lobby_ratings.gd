extends SceneTree
# Lobby ratings as 0 A.D. 0.28 has them:
#   1. LobbyRatingReporter: a player who won or lost a lobby game of two human
#      players reports it once; AI games, team games, observers and a game
#      still running report nothing;
#   2. against tools/lobby_server.py: once both players have reported, the
#      lobby's player list shows each rating, selecting a player fills the
#      profile panel (rank, highest rating, games, wins, losses, win rate) and
#      the Leaderboard lists the rated players best first.
#   3. GameDetails: the host's heartbeat sends its game's map, settings and
#      players; selecting the game shows them, each human as "name (rating)",
#      by team (GameDetails.js, formatPlayerInfo); the Rating column and
#      filter (lobby.columns.gamerating) average the players' ratings; before
#      Start the game lists only who plays and who watches, by name
#      (LobbyGameRegistration.formatClientsForStanza).
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
	preload("res://tools/lobby_test_clock.gd").run_in_wall_time(self, "res://tools/e2e_lobby_ratings.gd", run)

func frames(n: int) -> void:
	for i in n:
		await process_frame

# max_frames counts frames at 60 fps of real time: headless --fixed-fps runs
# frames unthrottled, and the lobby's HTTP answers take wall-clock time.
func until(cond: Callable, max_frames := 600) -> bool:
	var deadline := Time.get_ticks_msec() + max_frames * 1000 / 60
	while Time.get_ticks_msec() < deadline:
		if cond.call():
			return true
		await process_frame
	return cond.call()

func player(id: int, name: String, state: String, ai := false) -> Dictionary:
	return {"id": id, "name": name, "state": state, "ai": ai, "civ_code": "athen", "civ_name": "Athenians", "team": id + 1, "setup_team": -1,
		"economyScore": 10, "militaryScore": 20, "totalScore": 30, "percentMapExplored": 12.5,
		"stats": {"tradeIncome": 5, "resourcesGathered": {"food": 100, "vegetarianFood": 60},
			"unitsTrained": {"Infantry": 3}, "enemyUnitsKilled": {"Cavalry": 1},
			"buildingsConstructed": {"House": 2}}}

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
	check(r.get("civs") == "athen,athen," and r.get("teams") == "-1,-1,",
		"Players.js: civ codes, and the Diplomacy team (-1 without a team): " + str([r.get("civs"), r.get("teams")]))
	# LobbyRatingReport Resources, Units, Buildings, Misc percentMapExplored, Players teamsLocked.
	check(r.get("foodGathered") == "100,100," and r.get("vegetarianFoodGathered") == "60,60," and r.get("woodUsed") == "0,0,"
		and r.get("infantryUnitsTrained") == "3,3," and r.get("enemyCavalryUnitsKilled") == "1,1,"
		and r.get("houseBuildingsConstructed") == "2,2," and r.get("percentMapExplored") == "12.5,12.5,"
		and r.get("teamsLocked") == "false",
		"the resource, unit, building and exploration parts ride along: " + str(r))
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
	check(LobbySessionScript.rated_name("alice", 1265) == "alice (1265)" and LobbySessionScript.rated_name("bob", null) == "bob",
		"names show their rating (PlayerColor.ColorPlayerName)")
	check(LobbySessionScript.format_win_rate({"totalGamesPlayed": 3, "wins": 2}) == "66.67%"
		and LobbySessionScript.format_win_rate({}) == "-", "the win rate as ProfilePanel.FormatWinRate")

	print("2. the lobby")
	var out := []
	server_pid = OS.create_process("python3", [ProjectSettings.globalize_path("res://tools/lobby_server.py"),
		"--port", str(PORT), "--quiet", "--exit-with-parent"])
	check(server_pid > 0, "the lobby server starts")
	var ls: Node = root.get_node("/root/LobbySession")
	var signed := [0]
	# Wait for the server to answer, then sign in once: retrying a sign-in that
	# is slow to answer (a loaded machine) leaves the first one signed in on the
	# server and the retries refused as "Player already logged in".
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
	check(ratings == {"alice": "1265", "bob": "1135"}, "the player list's Rating column (%s)" % str(ratings))
	# PlayerList.xml: the Rating column sorts, low to high first, then high to low.
	lobby.sort_players_by(2)
	check(lobby._players.get_root().get_child(0).get_text(1) == "bob", "Rating sorts low to high")
	lobby.sort_players_by(2)
	check(lobby._players.get_root().get_child(0).get_text(1) == "alice", "a second click sorts high to low")
	lobby.sort_players_by(1)
	var bob_item: TreeItem = lobby._players.get_root().get_child(1)
	bob_item.select(0)
	await until(func() -> bool: return lobby._profile_values["totalGamesPlayed"].text == "1")
	check(lobby._profile.visible and lobby._profile_name.text == "bob (1135)", "selecting bob shows his profile")
	check(lobby._profile_values["rank"].text == "2" and lobby._profile_values["highestRating"].text == "1135"
		and lobby._profile_values["wins"].text == "0" and lobby._profile_values["losses"].text == "1"
		and lobby._profile_values["ratio"].text == "0.00%", "rank, highest rating, wins, losses and win rate")
	lobby._on_leaderboard_pressed()
	await until(func() -> bool: return lobby._board.get_root() != null and lobby._board.get_root().get_child_count() == 2)
	var board := []
	if lobby._board.get_root():
		for it in lobby._board.get_root().get_children():
			board.append([it.get_text(0), it.get_text(1), it.get_text(2)])
	check(board == [["1", "alice", "1265"], ["2", "bob", "1135"]], "the Leaderboard, best first")
	lobby._board.clear()
	lobby._leaderboard.custom_action.emit(&"update")
	await until(func() -> bool: return lobby._board.get_root() != null and lobby._board.get_root().get_child_count() == 2)
	check(lobby._board.get_root() != null and lobby._board.get_root().get_child_count() == 2, "Update asks the lobby again")
	lobby._leaderboard.hide()
	check(lobby._profile_role.text == "Player", "the profile names bob's role: Player")
	# ProfilePage: any player, by name.
	lobby._on_lookup_pressed()
	lobby._lookup_input.text = "ALICE"
	lobby._on_lookup_view()
	await until(func() -> bool: return lobby._lookup_grid.visible)
	check(lobby._lookup_grid.visible and lobby._lookup_values["name"].text == "alice (1265)"
		and lobby._lookup_values["rank"].text == "1", "Player Profile Lookup finds alice by name")
	lobby._lookup_input.text = "nobody"
	lobby._on_lookup_view()
	await until(func() -> bool: return lobby._lookup_error.visible)
	check(lobby._lookup_error.visible and lobby._lookup_error.text == 'Player "nobody" not found.' and not lobby._lookup_grid.visible,
		"and says when a player is not found")
	lobby._lookup.hide()

	print("3. game details")
	var ms: Node = root.get_node("/root/MatchSettings")
	ms.map_id = "random_continental"
	ms.map_size = 320
	ms.victory = 0x80 | 1 | 16
	ms.player_count = 3
	ms.slot_types = [0, 1, 0]
	ms.slot_civs = [0, -1, 1]
	ms.slot_teams = [1, 2, 1]
	ms.slot_names = ["", "Cleopatra", ""]
	ms.slot_diffs = [3, 4, 3]
	ms.slot_behs = [2, 3, 2]
	var net := {"slots": [{"client": 0, "name": "alice"}, {"client": -1, "name": ""}, {"client": 1, "name": "bob"}],
		"observers": [{"client": 2, "name": "carol"}]}
	var det: Dictionary = LobbySessionScript.room_details(ms, net, 0)
	check(det.get("map_name") == "Continental" and det.get("map_type") == "random" and det.get("map_size") == "Medium",
		"the map's name, type and size (%s)" % str(det))
	check(det.get("victory") == "Conquest,Wonder", "the victory conditions")
	var names: Array = det.get("player_names", [])
	check(names.size() == 4 and names[0] == {"name": "alice", "team": 0, "ai": false, "civ": "athen"}
		and names[1].get("name") == "Cleopatra" and names[1].get("ai") and names[1].get("civ") == "random"
		and names[1].get("team") == 1 and names[1].get("ai_difficulty") == 4 and names[1].get("ai_behavior") == 3
		and names[2].get("name") == "bob" and names[3] == {"name": "carol", "team": -1, "observer": true},
		"the seated players, the AI and the observers (%s)" % str(names))
	ms.slot_types = [0, 2, 0]
	check(LobbySessionScript.room_details(ms, net, 0)["player_names"].size() == 3, "a closed seat lists nobody")
	# LobbyGameRegistration.formatClientsForStanza: before Start only the names of
	# who plays and who watches, no AIs, teams or civs.
	var setup_net := net.duplicate(true)
	setup_net["started"] = false
	ms.slot_types = [0, 1, 0]
	var pre: Array = LobbySessionScript.room_details(ms, setup_net, 0)["player_names"]
	check(pre == [{"name": "alice"}, {"name": "bob"}, {"name": "carol", "team": -1, "observer": true}],
		"in setup: the players' names and the observers (%s)" % str(pre))
	var pre_text: String = LobbySessionScript.game_details({"name": "g", "player_names": pre}, {})
	check(pre_text.contains("\nalice\nbob\n") and pre_text.contains("Observer:[/b]\ncarol") and not pre_text.contains("Civilization"),
		"and the details list them by name: " + pre_text.replace("\n", " | "))
	ms.slot_types = [0, 2, 0]
	ms.map_id = "acropolis_bay"
	var sk: Dictionary = LobbySessionScript.room_details(ms, {}, 0)
	check(sk.get("map_type") == "skirmish" and sk.get("map_size") == "Default", "a skirmish map's size is Default")
	ms.slot_types = [0, 1, 0]
	ms.map_id = "random_continental"
	check(LobbySessionScript.game_rating(names, {"alice": 1265, "bob": 1135}) == 1200
		and LobbySessionScript.game_rating(names, {"alice": 1500, "bob": 1500}) == 1400
		and LobbySessionScript.game_rating([], {}) == 1200, "a game's rating averages its players, 1200 for none")
	check(lobby.game_columns(true) == ["Name", "Map Name", "Size", "Type", "Players", "Rating"]
		and lobby.game_columns(false).size() == 5, "the Rating column only with lobby.columns.gamerating")
	check(lobby.rating_passes(">1300", 1300) and not lobby.rating_passes(">1300", 1299)
		and lobby.rating_passes("<1200", 1199) and not lobby.rating_passes("<1200", 1200)
		and lobby.rating_passes("", 0), "the Rating filter as GameListFilters/Rating.js")

	var room := {"id": "room-20700", "host_port": 20700, "name": "alice's game", "host_name": "alice",
		"players": 2, "max_players": 8, "state": "running"}
	room.merge(LobbySessionScript.room_details(ms, net, 1760000000))
	var reg := [0]
	ls.post_json("/rooms", room, func(code: int, _o) -> void: reg[0] = code)
	await until(func() -> bool: return reg[0] != 0)
	check(reg[0] == 201, "the host registers its game with the details")
	lobby._refresh()
	await until(func() -> bool: return lobby._games.get_root() != null and lobby._games.get_root().get_child_count() == 1)
	var game_item: TreeItem = lobby._games.get_root().get_child(0) if lobby._games.get_root() and lobby._games.get_root().get_child_count() else null
	check(game_item != null, "the game is listed")
	if game_item:
		check(game_item.get_text(1) == "Continental" and game_item.get_text(2) == "Medium" and game_item.get_text(3) == "Random"
			and game_item.get_text(4) == "2/8 +1" and game_item.get_custom_color(0) == Color8(219, 0, 0),
			"its row: map, size, type, \"2/8 +1\" with carol watching, red while running: %s | %s | %s | %s" % [
			game_item.get_text(1), game_item.get_text(2), game_item.get_text(3), game_item.get_text(4)])
	if game_item:
		game_item.select(0)
	await frames(2)
	var text: String = lobby._details.get_parsed_text()
	check(lobby._details.visible, "selecting the game shows its details")
	for want in ["Continental", "Victory Conditions: Conquest, Wonder", "Map Type: Random", "Map Size: Medium",
			"alice's game", "Host: alice", "Game started at ", "Players: 2/8", "Team 1:\nalice (1265) (Athenians)\nbob (1135) (Spartans)",
			"Team 2:\nCleopatra (Random, Hard Aggressive Petra Bot)", "Observer:\ncarol"]:
		check(text.contains(want), "the details say \"%s\"" % want.replace("\n", " / "))
	if not text.contains("Team 1:"):
		print(text)

	ls.sign_out()
	OS.kill(server_pid)
	print("RESULT: " + ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
