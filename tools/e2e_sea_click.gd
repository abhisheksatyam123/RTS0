extends SceneTree
# A click over the sea lands on the water surface, not the seabed (0 A.D. 0.28
# Engine.GetTerrainAtScreenPoint: GetWorldCoordinates above water), so ship orders
# and ship picks meet the drawn sea under the cursor.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_sea_click.gd
# Exit code 0 = pass.

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	var ms = root.get_node("MatchSettings")
	ms.map_id = "acropolis_bay"
	ms.seed = 1234
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func run() -> void:
	await frames(120)
	var vp: Vector2 = root.get_visible_rect().size
	# Look at the bay: the first camera spot whose screen centre is deep sea.
	var found := false
	for zi in range(-8, 9):
		for xi in range(-8, 9):
			main.debug_set_camera_data({"x": xi * 25.0, "z": zi * 25.0, "yaw": 0.0, "zoom": 30.0})
			await frames(2)
			var c: Dictionary = main.debug_click_surface(vp * 0.5)
			if float(c["terrain"]) < float(c["water"]) - 2.0:
				found = true
				break
		if found:
			break
	await frames(4)
	var sea := 0
	var on_surface := 0
	var land := 0
	var land_ok := 0
	for gy in range(1, 12):
		for gx in range(1, 16):
			var d: Dictionary = main.debug_click_surface(Vector2(vp.x * gx / 16.0, vp.y * 0.75 * gy / 12.0))
			var y: float = (d["pos"] as Vector3).y
			if float(d["terrain"]) < float(d["water"]) - 0.5:
				sea += 1
				if absf(y - float(d["water"])) < 0.01:
					on_surface += 1
			elif float(d["terrain"]) >= float(d["water"]):
				land += 1
				if absf(y - float(d["terrain"])) < 0.01:
					land_ok += 1
	print("  sea points ", sea, " on the surface ", on_surface, ", land points ", land, " on the ground ", land_ok)
	check(sea > 0, "the view shows some sea")
	check(sea > 0 and on_surface == sea, "every click over the sea lands on the water surface")
	check(land > 0 and land_ok == land, "every click over land lands on the terrain")
	print("FAILURES: ", failures)
	quit(1 if failures else 0)
