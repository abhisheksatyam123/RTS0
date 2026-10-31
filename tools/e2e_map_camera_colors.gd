extends SceneTree

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	if cond:
		print("  ok   ", what)
	else:
		print("  FAIL ", what)
		failures += 1

func _initialize() -> void:
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func run() -> void:
	await frames(10)
	check(main != null, "main scene instantiated")
	var rigs = main.find_children("*", "RtsoCameraRig", true, false)
	if not rigs.is_empty():
		rigs[0].set_edge_scroll_enabled(false)
	
	# Test 1: StartingCamera across all 14 ported maps
	var cam_test_maps := [
		"libyan_oasis", "sicilia_nomad", "sandbox_gauls", "peloponnese",
		"cinema_demo", "laconia", "walltest", "walls", "campaign_test_map",
		"polynesia", "ship_formations", "miletus", "sandbox_britons", "flight_demo"
	]
	for map_id in cam_test_maps:
		var file_path := "res://data/maps/%s.json" % map_id
		var data: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(file_path))
		var pls: Array = data.get("players", [])
		var p1: Dictionary = {}
		for p in pls:
			if int(p.get("id", 0)) == 1:
				p1 = p
				break
		if p1.is_empty() or not p1.has("camera") or typeof(p1["camera"]) != TYPE_DICTIONARY:
			continue
			
		var exp_cam: Dictionary = p1.get("camera", {})
		var ok: bool = main.load_map_seeded(map_id, 42)
		check(ok, "load_map_seeded succeeded for " + map_id)
		if not rigs.is_empty():
			rigs[0].set_edge_scroll_enabled(false)
		await frames(2)
		
		var cd: Dictionary = main.debug_camera()
		var target: Vector2 = cd.get("target", Vector2())
		var exp_x: float = float(exp_cam.get("x", 0.0))
		var exp_z: float = float(exp_cam.get("z", 0.0))
		var dist: float = Vector2(target.x - exp_x, target.y - exp_z).length()
		check(dist < 0.01, "%s camera target (%.2f, %.2f) matches StartingCamera (%.2f, %.2f)" % [map_id, target.x, target.y, exp_x, exp_z])
		
		if exp_cam.has("rot_y"):
			var exp_yaw: float = deg_to_rad(float(exp_cam.get("rot_y", 0.0)))
			var act_yaw: float = float(cd.get("yaw", 0.0))
			check(abs(act_yaw - exp_yaw) < 0.01, "%s camera yaw %.4f matches rot_y %.4f" % [map_id, act_yaw, exp_yaw])
		if exp_cam.has("rot_x"):
			var exp_pitch: float = float(exp_cam.get("rot_x", 0.0))
			var act_pitch: float = float(cd.get("pitch", 0.0))
			if exp_pitch >= 25.0 and exp_pitch <= 85.0:
				check(abs(act_pitch - exp_pitch) < 0.1, "%s camera pitch %.2f matches rot_x %.2f" % [map_id, act_pitch, exp_pitch])

	# Test 2: Map player colors nearest palette match on egypt
	print("Testing player colors on: egypt")
	var ok_egypt: bool = main.load_map_seeded("egypt", 42)
	check(ok_egypt, "load_map_seeded succeeded for egypt")
	await frames(2)
	
	var c0: int = main.get_player_color_index(0)
	var c1: int = main.get_player_color_index(1)
	var c2: int = main.get_player_color_index(2)
	var c3: int = main.get_player_color_index(3)
	var c4: int = main.get_player_color_index(4)
	var c5: int = main.get_player_color_index(5)
	
	check(c0 == 15, "egypt p1 color idx is 15 (nearest palette to [0,44,98]) (got %d)" % c0)
	check(c1 == 20, "egypt p2 color idx is 20 (brown) (got %d)" % c1)
	check(c2 == 12 || c2 == 21, "egypt p3 color idx is purple (12 or 21) (got %d)" % c2)
	check(c3 == 3, "egypt p4 color idx is 3 (yellow) (got %d)" % c3)
	check(c4 == 4, "egypt p5 color idx is 4 (teal) (got %d)" % c4)
	check(c5 == 1, "egypt p6 color idx is 1 (red) (got %d)" % c5)

	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
