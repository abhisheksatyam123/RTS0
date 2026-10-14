extends SceneTree
# The 0.28 scenarios ported from its public.zip (port_0ad_maps.py --zip-scenarios):
# each one loads as a match with its players, their territory and its fixed
# civs, and plays on for a while without anyone losing.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_scenarios_028.gd
# Exit code 0 = pass.

const MAPS := {
	"azure_coast_2p": 2, "azure_coast_4p": 4, "azure_coast_8p": 8, "belgian_bog_night": 2,
	"fast_oasis": 4, "peloponnese": 6, "savanna_ravine": 2, "serengeti": 4, "ship_combat_demo": 2,
	"third_macedonian_war": 3,
}

var failures := 0

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

func run() -> void:
	var Loading = load("res://scenes/menu/loading.gd")
	var types: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://data/map_types.json"))["maps"]
	var ms = root.get_node("MatchSettings")
	for map_id in MAPS:
		var n: int = MAPS[map_id]
		print(map_id)
		check(types.get(map_id, {}).get("type", "") == "scenario", "Game Setup lists it as a scenario")
		check(FileAccess.file_exists("res://assets/maps/previews/%s.jpg" % map_id) or map_id == "ship_combat_demo", "it has its 0.28 preview")
		ms.map_id = map_id
		ms.seed = 7
		ms.slot_types = [0] + range(n - 1).map(func(_i): return 1)
		ms.slot_civs = range(n).map(func(_i): return 1)
		ms.slot_teams = range(n).map(func(i): return i + 1)
		ms.victory = 3
		ms.replay_dir = ""
		ms.load_save_path = ""
		ms.go("res://scenes/main.tscn")
		await frames(2)
		var page = current_scene
		var t0 := Time.get_ticks_msec()
		while is_instance_valid(page) and page.get_script() == Loading and Time.get_ticks_msec() - t0 < 180000:
			await process_frame
		var m = current_scene
		check(m != null and m.has_method("get_match_statistics"), "the match starts")
		if m == null or not m.has_method("get_match_statistics"):
			continue
		var t1 := Time.get_ticks_msec()
		while float(m.get_match_statistics().get("game_time", 0.0)) < 20.0 and Time.get_ticks_msec() - t1 < 120000:
			await process_frame
		var st: Dictionary = m.get_match_statistics()
		check(st.get("map_id", "") == map_id, "the match is on " + str(st.get("map_id", "")))
		check(float(st.get("game_time", 0.0)) >= 20.0, "it plays on: %.0f s" % float(st.get("game_time", 0.0)))
		var players: Array = st.get("players", [])
		check(players.size() == n, "%d players (want %d)" % [players.size(), n])
		var controlled := players.filter(func(p): return float(p.get("percentMapControlled", 0.0)) > 0.0).size()
		check(controlled > 0, "%d of them hold territory" % controlled)
		check(not st.get("victory", false), "nobody has won yet")
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(0 if failures == 0 else 1)
