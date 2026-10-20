extends SceneTree
# End-to-end check of the replay menu (0 A.D. gui/replaymenu) on three made-up
# replays written to user://replays:
#   1. the list has 0 A.D.'s columns, newest first, and sorts by any column;
#   2. every filter narrows it (player names with Tab completion, month, map,
#      size, population, duration, single-player / multiplayer, victory, rated,
#      compatibility);
#   3. the details show the map, settings and players, the outcome only with
#      Spoiler, and an incompatible replay can't be started;
#   4. Shift+Delete deletes without asking.
#
#   godot --headless --path . --fixed-fps 60 -s tools/e2e_replay_menu.gd
# Exit code 0 = pass.

var failures := 0
const NAMES := ["zz_e2e_replay_a", "zz_e2e_replay_b", "zz_e2e_replay_c", "zz_e2e_replay_d"]

func check(cond: bool, what: String) -> void:
	if cond:
		print("  ok   ", what)
	else:
		print("  FAIL ", what)
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func player(id: int, name: String, team: int, ai: bool, state: String) -> Dictionary:
	return {"id": id, "name": name, "civ": id, "civ_name": ["Athenians", "Spartans", "Persians", "Romans"][id],
		"team": team, "ai": ai, "state": state, "winner": state == "won", "defeated": state == "defeated",
		"color": {"r": 0.2, "g": 0.3, "b": 0.8}}

func write_replay(name: String, meta: Dictionary) -> void:
	DirAccess.make_dir_recursive_absolute("user://replays/" + name)
	FileAccess.open("user://replays/%s/commands.rts0rec" % name, FileAccess.WRITE).store_string("x")
	FileAccess.open("user://replays/%s/metadata.json" % name, FileAccess.WRITE).store_string(JSON.stringify(meta))

func cleanup() -> void:
	for n in NAMES:
		var d := DirAccess.open("user://replays/" + n)
		if d:
			for f in d.get_files():
				d.remove(f)
			DirAccess.remove_absolute(ProjectSettings.globalize_path("user://replays/" + n))

func visible_dirs(b) -> Array:
	return b._replays.map(func(r): return r.dir.get_file()).filter(func(d): return d.begins_with("zz_e2e"))

func run() -> void:
	cleanup()
	var v: int = RtsoMain.get_save_version()
	# a: 2v2 single player on a Small random map, 20 min, Wonder, January 2030.
	write_replay(NAMES[0], {"format": 2, "save_version": v, "timestamp": 1893510000, "map_id": "random_continental",
		"map_name": "E2E Continental", "map_size": 192, "duration": 1200.0, "kind": "sp",
		"settings": {"pop_cap": 150, "victory": 1},
		"players": [player(0, "e2eAlpha", 1, false, "won"), player(1, "Bot One", 1, true, "won"),
			player(2, "Bot Two", 2, true, "defeated"), player(3, "Bot Three", 2, true, "defeated")],
		"summary": {"players": []}})
	# b: multiplayer rated 1v1, Giant, 50 min, Conquest, February 2030.
	write_replay(NAMES[1], {"format": 2, "save_version": v, "timestamp": 1896300000, "map_id": "random_oasis",
		"map_name": "E2E Oasis", "map_size": 512, "duration": 3000.0, "kind": "mp",
		"game_start": "map=random_oasis;seed=1;victory=0;popcap=300;worldpop=600;rated=1",
		"players": [player(0, "e2eAlpha", 1, false, "active"), player(1, "e2eBeta", 2, false, "active")]})
	# c: an old save format, so incompatible.
	write_replay(NAMES[2], {"format": 2, "save_version": v - 1, "timestamp": 1896400000, "map_id": "random_oasis",
		"map_name": "E2E Oasis", "map_size": 512, "duration": 600.0, "kind": "sp",
		"settings": {"pop_cap": 300, "victory": 0}, "players": [player(0, "e2eGamma", 1, false, "active")]})

	# d: 1 s long, as an early out-of-sync's: VisualReplay lists none under 3 s.
	write_replay(NAMES[3], {"format": 2, "save_version": v, "timestamp": 1896500000, "map_id": "random_oasis",
		"map_name": "E2E Oasis", "map_size": 512, "duration": 1.0, "kind": "mp",
		"players": [player(0, "e2eAlpha", 1, false, "active"), player(1, "e2eBeta", 2, false, "active")]})

	change_scene_to_file("res://scenes/menu/replay_browser.tscn")
	await frames(4)
	var b = current_scene

	print("1. list and sorting")
	var titles := []
	for c in b.replay_list.columns:
		titles.append(b.replay_list.get_column_title(c).trim_suffix("  ▼").trim_suffix("  ▲"))
	check(titles == ["Date / Time", "Players", "Map Name", "Size", "Population", "Duration"], "0 A.D.'s columns: " + str(titles))
	check(visible_dirs(b) == [NAMES[1], NAMES[0]], "compatible replays of 3 s or more only, newest first: " + str(visible_dirs(b)))
	var row: TreeItem = b.replay_list.get_root().get_child(b._replays.map(func(r): return r.dir.get_file()).find(NAMES[0]))
	check(row.get_text(1) == "e2eAlpha, Bot One, Bot Two, Bot Three" and row.get_text(3) == "Small" and row.get_text(4) == "150 (player)" and row.get_text(5) == "20:00",
		"a row shows players, size, population and duration: %s | %s | %s | %s" % [row.get_text(1), row.get_text(3), row.get_text(4), row.get_text(5)])
	b.sort_by("duration")
	check(visible_dirs(b) == [NAMES[0], NAMES[1]], "Duration sorts shortest first")
	b.sort_by("duration")
	check(visible_dirs(b) == [NAMES[1], NAMES[0]], "and again longest first")
	b.sort_by("players")
	check(visible_dirs(b) == [NAMES[1], NAMES[0]], "Players sorts by player count")
	b.sort_by("popCapacity")
	var pops: Array = b._replays.filter(func(r): return r.dir.get_file().begins_with("zz_e2e")).map(func(r): return b.population_name(r.pop, r.pop_type))
	check(pops == ["600 (world)", "150 (player)"] or pops == ["150 (player)", "600 (world)"], "a World Population reads as 0.28's translatePopulationCapacity: " + str(pops))
	var team: Dictionary = b._entry("x", {"map_id": "acropolis_bay", "settings": {"pop_cap": 200, "team_pop": true}})
	var team_mp: Dictionary = b._entry("y", {"map_id": "random_oasis", "game_start": "map=random_oasis;popcap=300;teampop=250"})
	check(b.population_name(team.pop, team.pop_type) == "200 (team)" and b.population_name(team_mp.pop, team_mp.pop_type) == "250 (team)",
		"a Team Population reads \"N (team)\", single player and multiplayer")
	check(team.map_type == "Skirmish" and b.map_info("arcadia").type == "Scenario" and team_mp.map_type == "Random",
		"Map Type is the map's own: Skirmish, Scenario or Random")
	# isReplayCompatible / displayReplayCompatibilityError: the replay's mods.
	var modded: Dictionary = b._entry("z", {"map_id": "random_oasis", "save_version": RtsoMain.get_save_version(),
		"mods": [{"name": "zz_other_mod", "version": "9", "ignoreInCompatibilityChecks": false}]})
	var same: Dictionary = b._entry("w", {"map_id": "random_oasis", "save_version": RtsoMain.get_save_version(), "mods": b._active_mods()})
	check(not modded.compatible and modded.mods_only and modded.why.contains("needs a different sequence of mods") and modded.why.contains("zz_other_mod (9)")
		and same.compatible, "a replay made with other mods is incompatible and names them; the same mods are fine")
	var first := visible_dirs(b)
	b.sort_by("popCapacity")
	check(visible_dirs(b) == [first[1], first[0]], "Population sorts by cap, then type")

	print("2. filters")
	b._players_filter.text = "e2eal bot"
	b.display_replay_list()
	check(visible_dirs(b) == [NAMES[0]], "every typed name must match")
	b._players_filter.text = "e2eBe"
	b._players_filter.caret_column = 5
	b.autocomplete_player()
	check(b._players_filter.text == "e2eBeta" and b._players_filter.caret_column == 7, "Tab completes a player's name")
	check(visible_dirs(b) == [NAMES[0]], "the list waits for Enter before re-filtering")
	b.display_replay_list()
	check(visible_dirs(b) == [NAMES[1]], "Enter filters by the completed name")
	b._players_filter.text = "e2e bot"
	b._players_filter.caret_column = 3
	b.autocomplete_player()
	check(b._players_filter.text == "e2e bot", "Tab leaves a word that starts several names")
	b._players_filter.text = "e2eB bot"
	b._players_filter.caret_column = 4
	b.autocomplete_player()
	check(b._players_filter.text == "e2eBeta bot" and b._players_filter.caret_column == 7, "Tab completes the word at the caret")
	check(b.hotkey_text({"x": PackedStringArray(["Shift+Delete", "Delete"])}, "x") == "[Delete, Shift+Delete]" and
		b.hotkey_text({}, "x") == "[Unassigned hotkey: x]", "delete tooltips list every key, shortest first")
	b._players_filter.text = ""
	b.display_replay_list()
	var pick := func(dd: OptionButton, text: String):
		for i in dd.item_count:
			if dd.get_item_text(i) == text:
				dd.select(i)
		b.display_replay_list()
	pick.call(b._size_filter, "Giant")
	check(visible_dirs(b) == [NAMES[1]], "map size")
	pick.call(b._size_filter, "Any")
	var caps := []
	for i in b._pop_filter.item_count:
		caps.append(b._pop_filter.get_item_text(i))
	check(caps[0] == "Any" and caps[-1] == "Unlimited" and caps.has("2400") and caps.find("300") < caps.find("400"),
		"one ascending list of every type's caps, Unlimited last (initPopCapFilter): " + str(caps))
	pick.call(b._pop_filter, "150")
	check(visible_dirs(b) == [NAMES[0]], "population")
	pick.call(b._pop_filter, "600")
	check(visible_dirs(b) == [NAMES[1]], "a World Population matches its cap")
	pick.call(b._pop_filter, "Any")
	pick.call(b._duration_filter, "45 - 60 min")
	check(visible_dirs(b) == [NAMES[1]], "duration")
	pick.call(b._duration_filter, "Any")
	pick.call(b._singleplayer_filter, "Multiplayer")
	check(visible_dirs(b) == [NAMES[1]], "multiplayer")
	pick.call(b._singleplayer_filter, "Single-player")
	check(visible_dirs(b) == [NAMES[0]], "single-player")
	pick.call(b._singleplayer_filter, "Any")
	pick.call(b._victory_filter, "Wonder")
	check(visible_dirs(b) == [NAMES[0]], "victory condition")
	pick.call(b._victory_filter, "Any victory condition")
	pick.call(b._rated_filter, "Rated games")
	check(visible_dirs(b) == [NAMES[1]], "rated games: two humans, two players, Rated Game")
	pick.call(b._rated_filter, "Unrated games")
	check(visible_dirs(b) == [NAMES[0]], "unrated games")
	check(b.create_selection_data("")["filters"]["ratedGames"] == 2, "the selection data keeps the rated filter's index")
	pick.call(b._rated_filter, "Rated and unrated games")
	var month := Time.get_datetime_string_from_unix_time(1893510000 + int(Time.get_time_zone_from_system().get("bias", 0)) * 60).substr(0, 7)
	pick.call(b._date_filter, month)
	check(visible_dirs(b) == [NAMES[0]], "month " + month)
	pick.call(b._date_filter, "Any")
	pick.call(b._map_filter, "E2E Oasis")
	check(visible_dirs(b) == [NAMES[1]], "map name")
	b._compatibility_filter.button_pressed = false
	check(visible_dirs(b).size() == 2 and visible_dirs(b).has(NAMES[2]), "unticking compatibility shows the old replay")
	pick.call(b._map_filter, "Any")

	print("3. details")
	b.sort_by("months")
	var idx: int = b._replays.map(func(r): return r.dir.get_file()).find(NAMES[0])
	b.select_replay(idx)
	check(b._info.visible and b._info_labels.name.text == "E2E Continental" and b._info_labels.victory.text == "Conquest, Wonder"
		and b._info_labels.type.text == "Random" and b._info_labels.size.text == "Small", "map, type, size and victory")
	# map_preview is null in an unimported headless project; the menu shows what Game Setup shows.
	check(b._info_labels.preview.texture == b.SetupPage.map_preview("random_continental")
		and b._info_labels.preview.visible == (b._info_labels.preview.texture != null), "the map's preview image")
	check(b._info_labels.players.text == "Players: 4" and b._players_text.get_parsed_text().contains("Team 1"), "players by team")
	check(not b._players_text.get_parsed_text().contains("won"), "no outcome without Spoiler")
	b._spoiler.button_pressed = true
	check(b._players_text.get_parsed_text().contains("e2eAlpha (Athenians, won)"), "Spoiler shows who won, as formatPlayerInfo")
	check(b._btn_summary.visible and b._filename.text.ends_with(NAMES[0]), "Summary and the file path")
	idx = b._replays.map(func(r): return r.dir.get_file()).find(NAMES[2])
	b.select_replay(idx)
	check(b.replay_list.get_selected().get_custom_color(1) == b.INCOMPATIBLE, "an incompatible replay is greyed out")
	b.start_replay()
	await frames(1)
	check(b._message != null and b._message.message.contains("save format"), "and can't be started")
	b._message.find_child("Button1", true, false).pressed.emit() # OK

	print("4. delete")
	var ev := InputEventKey.new()
	ev.keycode = KEY_DELETE
	ev.shift_pressed = true
	ev.pressed = true
	Input.parse_input_event(ev)
	await frames(2)
	check(not DirAccess.dir_exists_absolute("user://replays/" + NAMES[2]) and not visible_dirs(b).has(NAMES[2]),
		"Shift+Delete deletes without asking")
	cleanup()
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(0 if failures == 0 else 1)
