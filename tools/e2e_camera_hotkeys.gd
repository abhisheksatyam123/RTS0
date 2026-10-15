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

	print("FAILURES: ", failures)
	quit(1 if failures else 0)
