extends SceneTree
# input.js INPUT_PRESELECTEDACTION: a command button (Guard, Patrol, Garrison, Repair, Occupy
# Turret, Call to Arms) arms an action that the next LEFT click runs on what is under the
# cursor, without selecting it. A click on an invalid target does nothing and ends it (0.28's
# determineAction gives the "none" action); a right click only drops it; Shift keeps it armed.
# On the minimap (MiniMap.js onWorldClick) a left click runs an armed ground action.
# An armed action over a target it can't take shows its -disabled cursor, and a carrier over
# a dropsite the action-return-<resource> cursor.
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
	mm.position = root.get_final_transform() * pos
	mm.global_position = mm.position
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

	print("1. Guard: an unusable left click ends it (determineAction's \"none\" action)")
	main._on_entity_command("add-guard")
	await move(ground)
	check(main.debug_cursor_tip()["cursor"] == "action-guard-disabled", "the disabled guard cursor over bare ground once armed")
	await click(MOUSE_BUTTON_LEFT, ground)
	check(selected() == [a], "a left click on bare ground selects nothing new: " + str(selected()))
	check(main.debug_guard_target(a) == 0, "and guards nothing")
	check(not main.debug_armed(), "disarmed")

	print("2. Guard: the left click on a man runs it")
	main._on_entity_command("add-guard")
	b_at = main.debug_unit_screen(b)
	await move(b_at)
	check(main.debug_cursor_tip()["cursor"] == "action-guard", "the guard cursor over a man it can guard: " + str(main.debug_cursor_tip()["cursor"]) + " at " + str(b_at))
	await click(MOUSE_BUTTON_LEFT, b_at)
	check(main.debug_guard_target(a) == b, "the selected man guards the clicked one: %d" % main.debug_guard_target(a))
	check(selected() == [a], "the clicked man is not selected: " + str(selected()))
	await move(ground)
	check(not str(main.debug_cursor_tip()["cursor"]).begins_with("action-guard"), "disarmed after one click")

	print("3. A right click cancels the armed action")
	main._on_entity_command("add-guard")
	await move(ground)
	check(main.debug_cursor_tip()["cursor"] == "action-guard-disabled", "armed again")
	main.debug_select_units(PackedInt32Array([c]))
	b_at = main.debug_unit_screen(b)
	await click(MOUSE_BUTTON_RIGHT, b_at)
	check(main.debug_guard_target(c) == 0, "the right click gives no guard order")
	await move(ground)
	check(not str(main.debug_cursor_tip()["cursor"]).begins_with("action-guard"), "and drops the action")

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

	print("5. The minimap: a left click runs an armed ground action, a right click cancels it")
	main.debug_select_units(PackedInt32Array([c]))
	var from: Vector2 = main.debug_ground_at(main.debug_unit_screen(c))
	var goal: Vector2 = from + Vector2(30, 0)
	main._on_entity_command("patrol")
	main._on_minimap_clicked(goal)
	await frames(60)
	var now: Vector2 = main.debug_ground_at(main.debug_unit_screen(c))
	check(now.distance_to(goal) < from.distance_to(goal) - 3.0, "the man patrols toward the minimap point: %s -> %s" % [from, now])
	await move(ground)
	check(main.debug_cursor_tip()["cursor"] != "action-patrol", "Patrol is disarmed after the minimap click")
	main._on_entity_command("patrol")
	main._on_minimap_order(from)
	await move(ground)
	check(main.debug_cursor_tip()["cursor"] != "action-patrol", "a minimap right click drops the armed Patrol")

	print("6. A carrier over a dropsite: the return cursor")
	main.debug_select_units(PackedInt32Array([a]))
	main.debug_set_carry(a, 0, 10) # wood
	var cc: Vector2 = r["centre"]
	var cc_at: Vector2 = root.get_camera_3d().unproject_position(Vector3(cc.x, 0.5, cc.y))
	await move(cc_at)
	check(main.debug_cursor_tip()["cursor"] == "action-return-wood", "action-return-wood over the Civic Center: " + str(main.debug_cursor_tip()["cursor"]))

	print("FAILURES: ", failures)
	quit(1 if failures else 0)
