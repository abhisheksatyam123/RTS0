extends SceneTree
# 0.28's CameraController limits (CameraController.cpp, default.cfg [view]):
# the look-at point stays CAMERA_EDGE_MARGIN (2 tiles, 8 m = 2 units) inside
# the map's square edge, or inside its circle on a circular map; the tilt keeps
# to view.rotate.x.min/max (28-60 degrees, 35 by default) and the zoom, the
# distance from the look-at point, to view.zoom.min/max (50-200 m, 120 m by
# default; RTS0 units are 4 m) in 32 m wheel steps.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_camera_edge.gd
# Exit code 0 = pass.

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func wheel(button: MouseButton) -> void:
	for down in [true, false]:
		var m := InputEventMouseButton.new()
		m.button_index = button
		m.pressed = down
		m.position = Vector2(640, 360)
		Input.parse_input_event(m)
	await frames(2)

func run() -> void:
	await frames(30)
	var rigs: Array = main.find_children("*", "RtsoCameraRig", true, false)
	check(not rigs.is_empty(), "the match has a camera rig")
	if rigs.is_empty():
		quit(1)
		return
	var rig = rigs[0]
	var half: float = rig.get_map_bounds_half()
	var r := half - 2.0
	check(half > 10.0, "the rig knows the map's size (%.1f)" % half)

	var cam: Dictionary = rig.camera_data()
	check(is_equal_approx(float(cam["pitch"]), 35.0), "the match opens at view.rotate.x.default 35 degrees (%.1f)" % cam["pitch"])
	check(is_equal_approx(float(cam["zoom"]), 30.0), "and view.zoom.default 120 m away (%.1f units)" % cam["zoom"])

	rig.set_map_circular(false)
	rig.set_target_pos(Vector3(1.0e5, 0, -1.0e5))
	cam = rig.camera_data()
	check(is_equal_approx(float(cam["x"]), r) and is_equal_approx(float(cam["z"]), -r), "a square map keeps the look-at point 2 tiles inside its corner (%.1f, %.1f)" % [cam["x"], cam["z"]])

	rig.set_map_circular(true)
	rig.set_target_pos(Vector3(1.0e5, 0, 1.0e5))
	cam = rig.camera_data()
	var d := Vector2(float(cam["x"]), float(cam["z"])).length()
	check(absf(d - r) < 0.01 and float(cam["x"]) > 0.0 and is_equal_approx(float(cam["x"]), float(cam["z"])), "a circular map keeps it 2 tiles inside the circle, toward the corner asked for (%.2f of %.2f)" % [d, r])
	rig.set_target_pos(Vector3(r * 0.5, 0, -r * 0.5))
	cam = rig.camera_data()
	check(is_equal_approx(float(cam["x"]), r * 0.5), "and leaves points inside the circle alone")

	for i in 20:
		await wheel(MOUSE_BUTTON_WHEEL_UP)
	check(is_equal_approx(float(rig.camera_data()["zoom"]), 12.5), "the wheel zooms in to view.zoom.min 50 m (%.2f units)" % rig.camera_data()["zoom"])
	await wheel(MOUSE_BUTTON_WHEEL_DOWN)
	check(is_equal_approx(float(rig.camera_data()["zoom"]), 20.5), "one wheel step out is view.zoom.speed.wheel 32 m (%.2f units)" % rig.camera_data()["zoom"])
	for i in 20:
		await wheel(MOUSE_BUTTON_WHEEL_DOWN)
	check(is_equal_approx(float(rig.camera_data()["zoom"]), 50.0), "and out to view.zoom.max 200 m (%.2f units)" % rig.camera_data()["zoom"])

	main.debug_set_camera_data({"pitch": 80.0})
	await frames(2)
	check(is_equal_approx(rig.get_pitch_deg(), 60.0), "the tilt keeps to view.rotate.x.max 60 degrees (%.1f)" % rig.get_pitch_deg())
	main.debug_set_camera_data({"pitch": 10.0})
	await frames(2)
	check(is_equal_approx(rig.get_pitch_deg(), 28.0), "and view.rotate.x.min 28 degrees (%.1f)" % rig.get_pitch_deg())
	rig.reset_camera()
	await frames(2)
	cam = rig.camera_data()
	check(is_equal_approx(float(cam["pitch"]), 35.0) and is_equal_approx(float(cam["zoom"]), 30.0), "camera.reset restores 35 degrees and 120 m")

	print("FAILURES: %d" % failures)
	quit(1 if failures > 0 else 0)
