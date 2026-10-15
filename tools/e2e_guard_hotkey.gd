extends SceneTree
# unit_actions.js with session.guard (G) held: "guard" (specificness 40) on an own or allied
# unit or structure under the cursor with the action-guard cursor; else "remove-guard" (41)
# when a selected unit guards, with action-remove-guard; else the usual action, as
# hotkeyActionCheck fails for both. Guard and remove-guard play no voice (0.28's units have no
# order_guard sound group).
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_guard_hotkey.gd
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

func move(pos: Vector2) -> void:
	var mm := InputEventMouseMotion.new()
	mm.position = root.get_final_transform() * pos
	mm.global_position = mm.position
	Input.parse_input_event(mm)
	await frames(2)

func key(code: Key, down: bool) -> void:
	var k := InputEventKey.new()
	k.keycode = code
	k.physical_keycode = code
	k.pressed = down
	Input.parse_input_event(k)
	await frames(2)

func right_click(pos: Vector2) -> void:
	await move(pos)
	for down in [true, false]:
		var mb := InputEventMouseButton.new()
		mb.button_index = MOUSE_BUTTON_RIGHT
		mb.pressed = down
		mb.position = pos
		mb.global_position = pos
		root.push_input(mb, true)
		await frames(2)

func cursor() -> String:
	return str(main.debug_cursor_tip()["cursor"])

func wait_guard(unit: int, want: int) -> void:
	for i in 60:
		if main.debug_guard_target(unit) == want:
			return
		await frames(1)

func run() -> void:
	await frames(60)
	# Let the camera settle on the Civic Center first.
	var r: Dictionary = main.debug_civic_centre_roof()
	for i in 200:
		await frames(1)
		var r2: Dictionary = main.debug_civic_centre_roof()
		var still: bool = (r2["roof"] as Vector2).distance_to(r["roof"]) < 0.5
		r = r2
		if still and i > 10:
			break
	var men: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 6)
	await frames(10)
	var a := int(men[0])
	var b := int(men[5])
	main.debug_select_units(PackedInt32Array([a]))
	await frames(2)
	var a_at: Vector2 = main.debug_unit_screen(a)
	var b_at: Vector2 = main.debug_unit_screen(b)
	var ground: Vector2 = a_at + (a_at - b_at).normalized() * 120.0

	print("1. G over nothing with no guards: the usual cursor and order")
	await key(KEY_G, true)
	await move(ground)
	check(not cursor().begins_with("action-guard") and cursor() != "action-remove-guard", "no guard cursor over bare ground: " + cursor())
	var before: Vector2 = main.debug_unit_screen(a)
	await right_click(ground)
	await key(KEY_G, false)
	for i in 120: # the order lands on a later turn
		if main.debug_unit_screen(a).distance_to(before) > 5.0:
			break
		await frames(1)
	check(main.debug_unit_screen(a).distance_to(before) > 5.0, "the click walks the man there")

	print("2. G over an own man: guard")
	main.debug_select_units(PackedInt32Array([a]))
	b_at = main.debug_unit_screen(b)
	await key(KEY_G, true)
	await move(b_at)
	check(cursor() == "action-guard", "the guard cursor over the man: " + cursor())
	var voices: int = main.debug_voices()
	await right_click(b_at)
	await key(KEY_G, false)
	await wait_guard(a, b)
	check(main.debug_guard_target(a) == b, "the selected man guards him")
	check(main.debug_voices() == voices, "silently")

	print("3. G over bare ground while guarding: remove-guard")
	a_at = main.debug_unit_screen(a)
	b_at = main.debug_unit_screen(b)
	ground = a_at + (a_at - b_at).normalized() * 120.0
	await key(KEY_G, true)
	await move(ground)
	check(cursor() == "action-remove-guard", "the remove-guard cursor: " + cursor())
	await right_click(ground)
	await key(KEY_G, false)
	await wait_guard(a, 0)
	check(main.debug_guard_target(a) == 0, "he stops guarding")

	print("FAILURES: ", failures)
	quit(1 if failures else 0)
