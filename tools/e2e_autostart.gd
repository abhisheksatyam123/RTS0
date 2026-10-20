extends SceneTree
# 0 A.D.'s -autostart command line (GameSetup.cpp Autostart, readme.txt):
#   1. the options parse with one dash or two, repeated ones kept in order;
#   2. a random map takes -autostart-players, -size, -civ, -team, -ai, -aidiff,
#      -seed and -victory; seats without an AI stay uncontrolled;
#   3. a scenario keeps its own players and civs (-autostart-civ is ignored);
#   4. -autostart-player=-1 watches: every seat is played, a Sandbox AI where
#      none was named;
#   5. an unknown map type or map is refused;
#   6. the main menu started with the command line below goes straight to the match
#      (without it the test supplies the same options).
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_autostart.gd -- \
#     -autostart=random/mainland -autostart-ai=2:petra -autostart-civ=2:brit -autostart-seed=7
# Exit code 0 = pass.

# Loaded once the autoloads exist (gamesetup.gd reads UserSettings).
var Autostart
var GameSetup

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

func run() -> void:
	Autostart = load("res://scenes/menu/autostart.gd")
	GameSetup = load("res://scenes/menu/gamesetup.gd")
	var ms: Node = root.get_node("/root/MatchSettings")
	var maps: Array = GameSetup.load_all_maps()

	print("1. Parse")
	var o: Dictionary = Autostart.parse(PackedStringArray(["-autostart=\"random/mainland\"", "--autostart-civ=1:spart",
		"-autostart-civ=2:gaul", "-autostart-nonvisual", "-other=1"]))
	check(o.get("autostart") == ["random/mainland"] and o.get("autostart-civ") == ["1:spart", "2:gaul"]
		and o.get("autostart-nonvisual") == [""] and not o.has("other"), str(o))

	print("2. Random map")
	var err: String = Autostart.configure(ms, Autostart.parse(PackedStringArray(["-autostart=random/mainland",
		"-autostart-players=3", "-autostart-size=320", "-autostart-civ=1:spart", "-autostart-civ=3:germ",
		"-autostart-team=2:2", "-autostart-team=3:2", "-autostart-ai=2:petra", "-autostart-ai=3:petra",
		"-autostart-aidiff=3:5", "-autostart-seed=42", "-autostart-victory=wonder", "-autostart-victory=capture_the_relic",
		"-autostart-biome=generic/arctic", "-autostart-ceasefire=5"])), maps)
	check(err == "" and ms.map_id == "random_mainland" and ms.player_count == 3 and ms.map_size == 320, "Mainland, 3 players, 320 tiles")
	check(ms.slot_types == [0, 1, 1] and ms.slot_diffs == [3, 3, 5] and ms.slot_behs == [2, 2, 2], "you, then Petra Medium and Very Hard, Balanced")
	check(ms.slot_civs == [1, 0, 15] and ms.slot_teams == [0, 2, 2], "civs (Athenians unless named) and teams")
	check(ms.seed == 42 and ms.ceasefire_min == 5 and ms.biome == 8 and ms.cheats, "seed, ceasefire, biome, cheats")
	check(GameSetup.victory_mask(ms.victory) == 16 | 32, "Wonder and Capture the Relic: " + GameSetup.victory_title(ms.victory))
	Autostart.configure(ms, Autostart.parse(PackedStringArray(["-autostart=random/mainland", "-autostart-victory=endless", "-autostart-seed=-1"])), maps)
	check(GameSetup.victory_mask(ms.victory) == 0 and ms.slot_types == [0, 0], "endless; a seat with no AI stays a player nobody controls")
	Autostart.configure(ms, Autostart.parse(PackedStringArray(["-autostart=random/mainland"])), maps)
	check(GameSetup.victory_mask(ms.victory) == 1, "conquest by default")
	check(ms.seed == 0 and ms.seed_exact, "seed 0 by default, played as seed 0 (not randomized)")
	Autostart.configure(ms, Autostart.parse(PackedStringArray(["-autostart=random/mainland", "-autostart-seed=-1"])), maps)
	check(ms.seed >= 0 and not ms.seed_exact, "-autostart-seed=-1 picks one")

	print("3. Scenario")
	err = Autostart.configure(ms, Autostart.parse(PackedStringArray(["-autostart=scenarios/arcadia",
		"-autostart-civ=1:han", "-autostart-ai=2:petra", "-autostart-players=5"])), maps)
	check(err == "" and ms.map_id == "arcadia" and ms.player_count == 2 and ms.slot_civs == [1, 7], "Arcadia's own 2 players and civs")

	print("4. Observer")
	Autostart.configure(ms, Autostart.parse(PackedStringArray(["-autostart=random/mainland", "-autostart-players=3",
		"-autostart-ai=1:petra", "-autostart-ai=2:petra", "-autostart-player=-1"])), maps)
	check(ms.slot_types == [1, 1, 1] and ms.slot_diffs == [3, 3, 0] and ms.local_slot == -1, "every seat AI, the unnamed one Sandbox")
	Autostart.configure(ms, Autostart.parse(PackedStringArray(["-autostart=random/mainland", "-autostart-players=3",
		"-autostart-ai=1:petra", "-autostart-ai=3:petra", "-autostart-player=2"])), maps)
	check(ms.slot_types == [1, 0, 1] and ms.local_slot == 1, "-autostart-player=2 seats you in slot 2: %s %d" % [str(ms.slot_types), ms.local_slot])

	print("5. Refused")
	check(Autostart.configure(ms, Autostart.parse(PackedStringArray(["-autostart=maps/x"])), maps).begins_with("Unrecognized map type"), "unknown type")
	check(Autostart.configure(ms, Autostart.parse(PackedStringArray(["-autostart=skirmishes/no_such_map"])), maps).begins_with("Map "), "unknown map")

	print("5b. Multiplayer and run options")
	var d: Node = Autostart.configure_run(ms, Autostart.parse(PackedStringArray(["-autostart=random/mainland",
		"-autostart-host", "-autostart-host-players=3", "-autostart-port=20700", "-autostart-playername=alice"])))
	check(d != null and d.host and not d.client and d.host_players == 3 and ms.net_mode == "host" and ms.net_port == 20700
		and ms.net_player_name == "alice" and ms.record_replay and ms.ai_seed == 0, "host: waits for 3 players on port 20700 as alice")
	d.free()
	d = Autostart.configure_run(ms, Autostart.parse(PackedStringArray(["-autostart-client=10.0.0.5", "-autostart-disable-replay"])))
	check(d != null and d.client and ms.net_mode == "join" and ms.net_ip == "10.0.0.5" and ms.net_player_name == "anonymous"
		and not ms.record_replay, "client: joins 10.0.0.5 as anonymous, no replay")
	d.free()
	d = Autostart.configure_run(ms, Autostart.parse(PackedStringArray(["-autostart=random/mainland", "-autostart-nonvisual"])))
	check(d != null and d.nonvisual and not d.host and not d.client and ms.net_mode == "", "nonvisual, offline")
	# NonVisualTrigger.js: each player's statistics as JSON once the match is over.
	var lines: Array = d.statistics_lines({"players": [{"id": 1, "name": "A", "civ_name": "Gauls", "stats": {"unitsTrained": 5}}]})
	var parsed = JSON.parse_string(lines[0].trim_prefix("[Autostart] statistics ")) if lines.size() == 1 else null
	check(parsed is Dictionary and int(parsed["playerID"]) == 1 and parsed["civ"] == "Gauls" and int(parsed["statistics"]["unitsTrained"]) == 5,
		"a non-visual match prints each player's statistics: " + str(lines))
	d.free()
	check(Autostart.configure_run(ms, Autostart.parse(PackedStringArray(["-autostart=random/mainland", "-autostart-aiseed=12"]))) == null
		and ms.ai_seed == 12, "-autostart-aiseed=12 seeds the AIs")
	Autostart.configure_run(ms, Autostart.parse(PackedStringArray(["-autostart=random/mainland", "-autostart-aiseed=-1"])))
	check(ms.ai_seed > 0, "-autostart-aiseed=-1 picks one")
	check(Autostart.configure_run(ms, Autostart.parse(PackedStringArray(["-autostart=random/mainland"]))) == null
		and ms.record_replay and ms.net_mode == "", "a plain autostart needs no driver")

	print("6. Main menu")
	# Run without the command line above, the test supplies it.
	if not Autostart.parse(OS.get_cmdline_user_args()).has("autostart"):
		Autostart.args_override = PackedStringArray(["-autostart=random/mainland", "-autostart-ai=2:petra",
			"-autostart-civ=2:brit", "-autostart-seed=7"])
	var menu = load("res://scenes/menu/main_menu.tscn").instantiate()
	root.add_child(menu)
	current_scene = menu
	for i in 60:
		await frames(1)
		if current_scene != menu and current_scene != null:
			break
	check(current_scene != null and current_scene.scene_file_path in ["res://scenes/menu/loading.tscn", "res://scenes/main.tscn"], "the menu went to the match")
	check(ms.map_id == "random_mainland" and ms.seed == 7 and ms.slot_types == [0, 1] and ms.slot_civs == [0, 6], "with the command line's match")
	check(Autostart.consumed and not Autostart.run(ms), "only once")
	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
