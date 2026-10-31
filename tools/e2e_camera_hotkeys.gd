extends SceneTree
# selection_panels_helpers.js jumpCamera / setCameraFollow and messages.js focusAttack:
# F5 jumps to the Ctrl+F5 bookmark, and F5 again (within camerajump.threshold of it)
# jumps back to where the camera was. Space (camera.lastattackfocus) follows the last
# attacked unit.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_camera_hotkeys.gd
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

func key(code: int, ctrl := false) -> void:
	for down in [true, false]:
		var k := InputEventKey.new()
		k.keycode = code
		k.physical_keycode = code
		k.ctrl_pressed = ctrl
		k.pressed = down
		root.push_input(k)
		await frames(1)
	await frames(2)

func drag(at: Vector2) -> void:
	for i in 3:
		var m := InputEventMouseMotion.new()
		m.position = at
		m.global_position = at
		m.relative = Vector2(30, 0)
		Input.parse_input_event(m)
		await frames(2)
	await frames(10)

func target() -> Vector2:
	return main.debug_camera()["target"]

func run() -> void:
	await frames(60)
	var mm := InputEventMouseMotion.new() # off the screen edges, which scroll the camera
	mm.position = root.get_final_transform() * Vector2(512, 400)
	mm.global_position = mm.position
	Input.parse_input_event(mm)
	await frames(4)

	print("1. F5 jumps to the bookmark, then back")
	var a: Vector2 = target()
	await key(KEY_F5, true)
	var b := a + Vector2(60, 40)
	main._on_minimap_clicked(b)
	await frames(2)
	check(target().distance_to(b) < 0.5, "the camera is away from the bookmark")
	await key(KEY_F5)
	check(target().distance_to(a) < 0.5, "F5 jumps to the bookmark: %s" % target())
	await key(KEY_F5)
	check(target().distance_to(b) < 0.5, "F5 again jumps back: %s" % target())

	print("2. Space follows the last attacked unit")
	var man := int(main.debug_spawn_units("athen/infantry_spearman_b", 1)[0])
	await frames(4)
	var pos: Vector2 = main.debug_ground_at(main.debug_unit_screen(man))
	main.notify_attack(Vector3(pos.x, 0, pos.y), 2, false, man)
	main._on_minimap_clicked(b)
	await frames(2)
	await key(KEY_SPACE)
	check(int(main.debug_camera()["follow"]) == man, "the camera follows the attacked unit")

	# Verify follow cancellation on any user mouse or key interaction
	var mb_l := InputEventMouseButton.new()
	mb_l.button_index = MOUSE_BUTTON_LEFT
	mb_l.pressed = true
	root.push_input(mb_l)
	await frames(2)
	check(int(main.debug_camera()["follow"]) == 0, "left click cancels follow immediately")
	mb_l.pressed = false
	root.push_input(mb_l)
	await frames(2)

	await key(KEY_SPACE)
	check(int(main.debug_camera()["follow"]) == man, "Space re-engages follow")
	var mb_r := InputEventMouseButton.new()
	mb_r.button_index = MOUSE_BUTTON_RIGHT
	mb_r.pressed = true
	root.push_input(mb_r)
	await frames(2)
	check(int(main.debug_camera()["follow"]) == 0, "right click cancels follow immediately")
	mb_r.pressed = false
	root.push_input(mb_r)
	await frames(2)

	await key(KEY_SPACE)
	check(int(main.debug_camera()["follow"]) == man, "Space re-engages follow")
	main._on_minimap_clicked(b)
	await frames(2)
	check(int(main.debug_camera()["follow"]) == 0, "minimap click cancels follow immediately")

	await key(KEY_SPACE)
	check(int(main.debug_camera()["follow"]) == man, "Space re-engages follow")
	await key(KEY_W)
	await frames(2)
	check(int(main.debug_camera()["follow"]) == 0, "pan key W cancels follow immediately")

	print("3. camera.pan (MouseMiddle) drags the view while held, and follows a rebind")
	var at := root.get_final_transform() * Vector2(512, 400)
	var mb := InputEventMouseButton.new()
	mb.button_index = MOUSE_BUTTON_MIDDLE
	mb.position = at
	mb.global_position = at
	mb.pressed = true
	Input.parse_input_event(mb)
	await frames(2)
	var p0: Vector2 = target()
	await drag(at)
	check(target().distance_to(p0) > 0.5, "the middle button drags the view")
	mb = mb.duplicate()
	mb.pressed = false
	Input.parse_input_event(mb)
	await frames(2)
	p0 = target()
	await drag(at)
	check(target().distance_to(p0) < 0.01, "released, moving the mouse doesn't")
	var hk := RtsoHotkeyConfig.new()
	hk.set_hotkey("camera.pan", PackedStringArray(["J"]))
	var j := InputEventKey.new()
	j.keycode = KEY_J
	j.physical_keycode = KEY_J
	j.pressed = true
	Input.parse_input_event(j)
	await frames(2)
	p0 = target()
	await drag(at)
	check(target().distance_to(p0) > 0.5, "camera.pan rebound to J: holding J drags")
	j = j.duplicate()
	j.pressed = false
	Input.parse_input_event(j)
	await frames(2)
	hk.reload()

	print("4. Edge scrolling toggles and margin")
	var rigs: Array = main.find_children("*", "RtsoCameraRig", true, false)
	if not rigs.is_empty():
		var rig = rigs[0]
		check(rig.is_edge_scroll_enabled(), "edge scrolling is enabled by default")
		rig.set_edge_scroll_enabled(false)
		check(not rig.is_edge_scroll_enabled(), "edge scrolling toggles off")
		rig.set_edge_scroll_enabled(true)
		check(rig.get_edge_scroll_margin() > 0.0, "edge scroll margin is positive (%.1f)" % rig.get_edge_scroll_margin())
		rig.set_edge_scroll_margin(16.0)
		check(is_equal_approx(rig.get_edge_scroll_margin(), 16.0), "edge scroll margin set to 16")

	print("FAILURES: ", failures)
	quit(1 if failures else 0)
