extends SceneTree
# Fogging.js: a structure last seen in the fog shows, when clicked, how the viewer last saw it
# (its mirage), not how it stands now: the selection details read the same fog memory the
# world view draws, kept every frame, so the first click on it already shows the old state.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_fog_details.gd
# Exit code 0 = pass.

const BLD_HOUSE := 1

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	var ms = root.get_node("MatchSettings")
	ms.map_id = "random_continental"
	ms.seed = 11
	ms.map_size = 128
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func turns(n: int) -> void:
	var until: int = int(main.get_replay_status().turn) + n
	var guard := 0
	while int(main.get_replay_status().turn) < until and guard < n * 40:
		await process_frame
		guard += 1

func run() -> void:
	await frames(60)
	# An enemy house beyond the Civic Centre's sight, toward the map centre.
	var house := 0
	var spot := Vector2.ZERO
	var cc: Vector2 = main.debug_building_fow(main.debug_civic_centre_id(0))["pos"]
	for k in range(-4, 5):
		# Toward the map centre, turned up to 45 degrees either way.
		var off: Vector2 = (-cc).normalized().rotated(k * PI / 16.0) * 30.0
		var h: int = main.debug_spawn_building(BLD_HOUSE, off.x, off.y, 1)
		await frames(2)
		var f: Dictionary = main.debug_building_fow(h)
		if not f.is_empty() and int(f["fow"]) != 2:
			house = h
			spot = f["pos"]
			break
	check(house != 0, "an enemy house out of sight")
	if house == 0:
		quit(1)
		return
	var scout: PackedInt32Array = main.debug_spawn_units("athen/cavalry_javelineer_b", 1)
	main.debug_select_units(scout)
	main._on_minimap_order(spot)
	for _i in 120:
		await turns(1)
		if int(main.debug_building_fow(house)["fow"]) == 2:
			break
	var sp := Vector2.ZERO
	for u in main.debug_selection_state()["units"]:
		if int(u["id"]) == scout[0]:
			sp = u["pos"]
	print("    house at ", spot, ", the scout at ", sp, ", fog ", main.debug_building_fow(house))
	check(int(main.debug_building_fow(house)["fow"]) == 2, "the scout sees the house")
	await turns(2)
	main.debug_delete_units(scout)
	for _i in 60:
		await turns(1)
		if int(main.debug_building_fow(house)["fow"]) == 1:
			break
	check(int(main.debug_building_fow(house)["fow"]) == 1, "the house is in the fog")
	# Player 1 loses capture points in the fog; the first click on the house must still show
	# it full, as last seen (before, the details built their memory from the live house).
	main.debug_set_capture_points(house, 1, 1.0)
	await turns(2)
	main.debug_inspect_entity(house)
	await frames(2)
	var parts: Array = main.debug_inspector("current").get("capture_parts", [])
	var width := 0.0
	for p in parts:
		width += float(p["width"])
	print("    capture bar on the first click in the fog: ", parts)
	check(width > 100.0, "the details show the house as last seen, fully held (bar %.1f px)" % width)
	print("FAILURES: ", failures)
	quit(1 if failures else 0)
