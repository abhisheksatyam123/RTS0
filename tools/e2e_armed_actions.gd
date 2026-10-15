extends SceneTree
# input.js INPUT_PRESELECTEDACTION: a command button (Guard, Patrol, Garrison, Repair, Occupy
# Turret, Call to Arms) arms an action that the next LEFT click runs on what is under the
# cursor, without selecting it. A click on an invalid target does nothing and keeps it armed;
# a right click drops it and gives the usual order; Shift keeps it armed after a click.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_armed_actions.gd
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
	mm.position = pos
	mm.global_position = pos
	Input.parse_input_event(mm)
	await frames(2)

func click(button: int, pos: Vector2) -> void:
	await move(pos)
	for down in [true, false]:
		var mb := InputEventMouseButton.new()
		mb.button_index = button
		mb.pressed = down
		mb.position = pos
		mb.global_position = pos
		root.push_input(mb, true)
		await frames(1)
	await frames(2)

func shift(down: bool) -> void:
	var k := InputEventKey.new()
	k.keycode = KEY_SHIFT
	k.physical_keycode = KEY_SHIFT
	k.shift_pressed = down
	k.pressed = down
	Input.parse_input_event(k)
	await frames(1)

func selected() -> Array:
	return Array(main.debug_selection_state()["selected"])

func run() -> void:
	await frames(60)
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
	var c := int(men[3])
	main.debug_select_units(PackedInt32Array([a]))
	await frames(2)
	var b_at: Vector2 = main.debug_unit_screen(b)
	var a_at: Vector2 = main.debug_unit_screen(a)
	# Open ground well away from the men and the Civic Center.
	var ground: Vector2 = a_at + (a_at - b_at).normalized() * 120.0
	print("    a ", a_at, " b ", b_at, " ground ", ground)

	print("1. Guard: an invalid left click keeps it armed")
	main._on_entity_command("add-guard")
	await move(ground)
	check(main.debug_cursor_tip()["cursor"] == "action-guard", "the guard cursor once armed")
	await click(MOUSE_BUTTON_LEFT, ground)
	check(selected() == [a], "a left click on bare ground selects nothing new: " + str(selected()))
	check(main.debug_guard_target(a) == 0, "and guards nothing")
	await move(ground)
	check(main.debug_cursor_tip()["cursor"] == "action-guard", "still armed")

	print("2. Guard: the left click on a man runs it")
	b_at = main.debug_unit_screen(b)
	await click(MOUSE_BUTTON_LEFT, b_at)
	check(main.debug_guard_target(a) == b, "the selected man guards the clicked one: %d" % main.debug_guard_target(a))
	check(selected() == [a], "the clicked man is not selected: " + str(selected()))
	await move(ground)
	check(main.debug_cursor_tip()["cursor"] != "action-guard", "disarmed after one click")

	print("3. A right click cancels the armed action")
	main._on_entity_command("add-guard")
	await move(ground)
	check(main.debug_cursor_tip()["cursor"] == "action-guard", "armed again")
	main.debug_select_units(PackedInt32Array([c]))
	b_at = main.debug_unit_screen(b)
	await click(MOUSE_BUTTON_RIGHT, b_at)
	check(main.debug_guard_target(c) == 0, "the right click gives no guard order")
	await move(ground)
	check(main.debug_cursor_tip()["cursor"] != "action-guard", "and drops the action")

	print("4. Shift keeps the action armed")
	main.debug_select_units(PackedInt32Array([c]))
	main._on_entity_command("patrol")
	await shift(true)
	await click(MOUSE_BUTTON_LEFT, ground)
	await move(ground + Vector2(30, 0))
	check(main.debug_cursor_tip()["cursor"] == "action-patrol", "Shift+click leaves Patrol armed")
	await shift(false)
	await click(MOUSE_BUTTON_LEFT, ground + Vector2(30, 0))
	await move(ground)
	check(main.debug_cursor_tip()["cursor"] != "action-patrol", "a plain click then disarms it")
	check(selected() == [c], "the selection is kept: " + str(selected()))

	print("FAILURES: ", failures)
	quit(1 if failures else 0)
