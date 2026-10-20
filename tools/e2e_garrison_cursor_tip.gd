extends SceneTree
# unit_actions.js "garrison" getActionInfo: with units selected, hovering an own
# structure they can garrison in with session.garrison (Ctrl) held shows the garrison
# cursor and "Current garrison: x/y" beside it, in orange once the structure is full.
# Without the hotkey "move" (specificness 12) beats "garrison" (20): no garrison cursor.
# selection.js: a right-click garrison order keeps the units selected until they are
# inside (hidden), then they leave the selection.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_garrison_cursor_tip.gd
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

func key(code: int, down: bool) -> void:
	var k := InputEventKey.new()
	k.keycode = code
	k.physical_keycode = code
	k.ctrl_pressed = down and code == KEY_CTRL
	k.pressed = down
	Input.parse_input_event(k)
	await frames(1)

func run() -> void:
	await frames(60)
	# The camera settles on the Civic Center first.
	await move(Vector2(512, 400))
	var r: Dictionary = main.debug_civic_centre_roof()
	for i in 200:
		await frames(1)
		var r2: Dictionary = main.debug_civic_centre_roof()
		var still: bool = (r2["roof"] as Vector2).distance_to(r["roof"]) < 0.5
		r = r2
		if still and i > 10:
			break
	var cc: int = main.debug_civic_centre_id(0)
	main.debug_select_entity(cc)
	await frames(4)
	var at: Vector2 = main.debug_selected_building()["screen"]
	var men: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 1)
	main.debug_select_units(men)
	await frames(4)
	await move(at)
	var t: Dictionary = main.debug_cursor_tip()
	print("    ", t)
	check(t["cursor"] != "action-garrison" and not t["visible"], "no garrison cursor without Ctrl (move wins)")
	await key(KEY_CTRL, true)
	await move(at)
	t = main.debug_cursor_tip()
	print("    ", t)
	check(t["cursor"] == "action-garrison", "Ctrl: the garrison cursor over the Civic Center")
	check(t["visible"] and str(t["text"]).begins_with("Current garrison: 0/"), "Current garrison: 0/capacity")
	check(t["color"] == Color(1, 1, 1), "white while there is room")
	var cap := int(str(t["text"]).get_slice("/", 1))
	for down in [true, false]:
		var mb := InputEventMouseButton.new()
		mb.button_index = MOUSE_BUTTON_RIGHT
		mb.pressed = down
		mb.position = at
		mb.global_position = at
		root.push_input(mb, true)
		await frames(1)
	await key(KEY_CTRL, false)
	check(main.debug_selection_grid(false)["selected"] == 1, "the garrison order keeps the unit selected")
	for i in 400:
		await frames(1)
		if main.debug_selection_grid(false)["selected"] == 0:
			break
	check(main.debug_selection_grid(false)["selected"] == 0, "inside, it leaves the selection")
	main.debug_select_units(main.debug_spawn_units("athen/infantry_spearman_b", 1))
	await frames(4)
	await key(KEY_CTRL, true)
	await move(at)
	cap -= 1
	var fill: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", cap)
	main.debug_put_inside(fill, cc)
	await frames(4)
	t = main.debug_cursor_tip()
	print("    ", t)
	check(str(t["text"]) == "Current garrison: %d/%d" % [cap + 1, cap + 1], "full: Current garrison: cap/cap")
	check(t["color"] == Color(1.0, 0.65, 0.0), "orange once full")
	await key(KEY_CTRL, false)
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
