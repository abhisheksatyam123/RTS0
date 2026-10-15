extends SceneTree
# 0.28's bird's eye view (camera.togglebirdseyeview, Shift+Tab): the camera
# looks straight down, the pitch keys leave it there, Shift+Tab again goes back
# to the pitch before, and camera.reset (R) leaves it too. F3 (mousegrabtoggle)
# confines the mouse to the window and frees it again. The camera speed
# hotkeys (Ctrl+Shift+S / Ctrl+Alt+S scroll, R rotate, Z zoom) scale each speed
# by view.*.speed.modifier (1.05).
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_birds_eye.gd
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

func key(code: Key, shift := false, ctrl := false, alt := false) -> void:
	for down in [true, false]:
		var k := InputEventKey.new()
		k.keycode = code
		k.physical_keycode = code
		k.shift_pressed = shift
		k.ctrl_pressed = ctrl
		k.alt_pressed = alt
		k.pressed = down
		Input.parse_input_event(k)
		await frames(2)

func run() -> void:
	await frames(30)
	var rig = main.find_children("*", "RtsoCameraRig", true, false)[0] if not main.find_children("*", "RtsoCameraRig", true, false).is_empty() else null
	check(rig != null, "the match has a camera rig")
	if rig == null:
		quit(1)
		return
	var start: float = rig.get_pitch_deg()
	await key(KEY_TAB, true)
	check(rig.is_birds_eye() and rig.get_pitch_deg() > 89.0, "Shift+Tab looks straight down: %.1f" % rig.get_pitch_deg())
	await frames(40)
	var cam: Camera3D = rig.get_child(0)
	check(cam.global_basis.z.y > 0.99, "the camera faces the ground: %s" % cam.global_basis.z)
	await key(KEY_TAB, true)
	check(not rig.is_birds_eye() and is_equal_approx(rig.get_pitch_deg(), start), "Shift+Tab again goes back to %.1f" % start)
	await key(KEY_TAB, true)
	await key(KEY_R)
	check(not rig.is_birds_eye() and is_equal_approx(rig.get_pitch_deg(), 50.6), "camera.reset (R) leaves it")
	var us = root.get_node("UserSettings")
	await key(KEY_F3)
	var first: bool = us.last_mouse_grab
	await key(KEY_F3)
	check(us.last_mouse_grab != first, "F3 toggles the mouse grab (mousegrabtoggle)")
	var codes := [KEY_S, KEY_R, KEY_Z]
	var names := ["scroll", "rotate", "zoom"]
	for i in 3:
		await key(codes[i], true, true)
		var up: Vector3 = rig.get_speed_multipliers()
		check(is_equal_approx(up[i], 1.05), "Ctrl+Shift+%s raises the %s speed: %s" % [OS.get_keycode_string(codes[i]), names[i], up])
		await key(codes[i], false, true, true)
		await key(codes[i], false, true, true)
		var down: Vector3 = rig.get_speed_multipliers()
		check(is_equal_approx(down[i], 1.0 / 1.05), "Ctrl+Alt+%s lowers it: %s" % [OS.get_keycode_string(codes[i]), down])
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(0 if failures == 0 else 1)
