extends SceneTree
# 0.28's demo maps (Keywords "demo") ported from its public.zip: Game Setup lists
# each under the Demo Maps filter, and each loads and runs for a while.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_demos_028.gd
# Exit code 0 = pass.

const MAPS := [
	"north_african_savanna", "skirmish_demo", "bridge_demo", "campaign_test_map",
	"campaign_test_map_2_heightmap", "combat_demo", "demo_trading", "eire_and_albion",
	"fishing_demo", "flight_demo", "multiplayer_demo", "napata_reconstruction",
	"pathfinding_demo", "pathfinding_terrain_demo", "polynesia", "reservoir", "resource_demo",
	"road_demo", "sandbox_germans", "sandbox_ptolemies_02", "ship_formations", "shipattacks",
	"territory_demo", "test_foundation", "tropical_island", "walls", "we_are_legion",
	"units_demo", "combat_demo_huge", "vertex_pathfinder_test", "triggers_demo",
	"pickup_test_map", "unit_chasing_test", "unit_dancing_test",
	"unit_pushing_test", "unit_motion_integration_test",
]

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
		print(map_id)
		check("demo" in types.get(map_id, {}).get("keywords", []), "Game Setup lists it under Demo Maps")
		var map: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://data/maps/%s.json" % map_id))
		# Maps whose players own nothing take Game Setup's default two slots.
		var n: int = map.get("players", []).size()
		if n == 0:
			n = 2
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
		while float(m.get_match_statistics().get("game_time", 0.0)) < 10.0 and Time.get_ticks_msec() - t1 < 120000:
			await process_frame
		var st: Dictionary = m.get_match_statistics()
		check(st.get("map_id", "") == map_id, "the match is on " + str(st.get("map_id", "")))
		check(float(st.get("game_time", 0.0)) >= 10.0, "it runs: %.0f s" % float(st.get("game_time", 0.0)))
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(0 if failures == 0 else 1)
