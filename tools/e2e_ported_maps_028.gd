extends SceneTree
# End-to-end check of the 0.28 skirmish maps ported with
# port_0ad_maps.py --zip-skirmishes: each one is listed by the game, loads
# into the match scene with every slot seated, and its AI players gather and
# train within the first minutes.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_ported_maps_028.gd [map ids]
# Exit code 0 = pass.

const MAPS := ["obedska_bog", "obedska_bog_night", "sahyadri_buttes", "scythia", "tarim_basin_2p", "tarim_basin_4p", "team_oasis",
	"temperate_roadway", "thessalian_plains", "via_augusta", "white_cliffs_of_dover", "yangtze",
	# The other sizes of maps ported from GitHub or the built-in list (pass-3 audit).
	"corinthian_isthmus_2p", "cycladic_archipelago_3p", "greek_acropolis_2p", "median_oasis_4p", "neareastern_badlands_2p",
	"saharan_oases_2p"]

# 0.28 skirmishes start with no teams; a map whose description suggests them is
# played that way (White Cliffs of Dover: the Britons against four invaders who
# land on one beach).
const TEAMS := {"white_cliffs_of_dover": [1, 2, 2, 2, 2]}

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

func total(stats: Dictionary, key: String) -> int:
	var v = stats.get(key, 0)
	if v is Dictionary:
		var n := 0
		for k in v:
			if k != "total" and k != "vegetarianFood" and (v[k] is int or v[k] is float):
				n += int(v[k])
		return int(v.get("total", n))
	return int(v)

func run() -> void:
	var ids: Array = Array(OS.get_cmdline_user_args()) if OS.get_cmdline_user_args().size() > 0 else MAPS
	var ms = root.get_node("MatchSettings")
	for id in ids:
		print(id)
		var data = JSON.parse_string(FileAccess.get_file_as_string("res://data/maps/%s.json" % id))
		check(data is Dictionary, "data/maps/%s.json parses" % id)
		if not (data is Dictionary):
			continue
		var n: int = data.get("players", []).size()
		ms.map_id = id
		ms.seed = 1234
		ms.slot_types = []
		ms.slot_civs = []
		ms.slot_teams = []
		for i in n:
			ms.slot_types.append(0 if i == 0 else 1)
			ms.slot_civs.append(i % 14)
			ms.slot_teams.append(TEAMS[id][i] if TEAMS.has(id) else 0)
		ms.victory = 3
		ms.replay_dir = ""
		ms.load_save_path = ""
		change_scene_to_file("res://scenes/main.tscn")
		await frames(5)
		var m = current_scene
		check(m != null and m.has_method("get_match_statistics"), "the match scene starts")
		if m == null or not m.has_method("get_match_statistics"):
			continue
		var listed := false
		for e in m.get_available_maps():
			if (e is Dictionary and e.get("id", "") == id) or (e is String and e == id):
				listed = true
		check(listed, "listed among the available maps")
		m.set_game_speed(2.0)
		await frames(2400) # about four minutes of game time
		var stats: Dictionary = m.get_match_statistics()
		var players: Array = stats.get("players", [])
		check(players.size() >= n, "%d players in the statistics" % players.size())
		for p in range(1, min(n, players.size())):
			var s: Dictionary = players[p].get("stats", {})
			var gathered := total(s, "resourcesGathered")
			var trained := total(s, "unitsTrained")
			check(gathered > 0 and trained > 0, "AI player %d gathers (%d) and trains (%d)" % [p, gathered, trained])
	change_scene_to_file("res://scenes/menu/main_menu.tscn")
	await frames(3)
	print("e2e_ported_maps_028: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
