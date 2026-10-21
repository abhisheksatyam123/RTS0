extends SceneTree
# unit_actions.js on an enemy structure, in specificness order: "attack" (9) before "capture"
# (10), so a plain right-click attacks it (allowCapture false) with the action-attack cursor;
# session.capture (C) captures it (action-capture), session.attack (F) attacks it, and
# session.attackmove (Ctrl) attack-moves to the spot (30) instead of attacking the structure.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_structure_click.gd
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
	k.ctrl_pressed = down and code == KEY_CTRL
	k.pressed = down
	Input.parse_input_event(k)
	await frames(1)

func right_click(pos: Vector2) -> void:
	await move(pos)
	for down in [true, false]:
		var mb := InputEventMouseButton.new()
		mb.button_index = MOUSE_BUTTON_RIGHT
		mb.pressed = down
		mb.position = pos
		mb.global_position = pos
		root.push_input(mb, true)
		await frames(1)
	await frames(12) # the command's turn

func cursor() -> String:
	return str(main.debug_cursor_tip()["cursor"])

# The order of the man after a right-click on the barracks with `code` held (KEY_NONE: none).
func order_with(code: Key, house: int, man: int) -> Dictionary:
	var at: Vector2 = main.debug_building_screen(house)
	if code != KEY_NONE:
		await key(code, true)
	await move(at)
	var c := cursor()
	await right_click(at)
	if code != KEY_NONE:
		await key(code, false)
	var o: Dictionary = main.debug_unit_order(man)
	o["cursor"] = c
	return o

func run() -> void:
	await frames(60)
	var r: Dictionary = main.debug_civic_centre_roof()
	for i in 200: # the camera settles on our Civic Center
		await frames(1)
		var r2: Dictionary = main.debug_civic_centre_roof()
		var still: bool = (r2["roof"] as Vector2).distance_to(r["roof"]) < 0.5
		r = r2
		if still and i > 10:
			break
	var house: int = main.debug_spawn_building(2, -4.0, 11.0, 1) # an enemy barracks beside our Civic Center
	var men: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 3)
	await frames(40) # seen by the men's vision first (the picking needs a sighted structure)
	main.debug_select_units(men)
	await frames(2)
	var at: Vector2 = main.debug_building_screen(house)
	check(house != 0 and at.x >= 0.0, "an enemy barracks on screen at %s" % at)

	print("1. A plain right-click attacks it")
	var o := await order_with(KEY_NONE, house, men[0])
	check(o["cursor"] == "action-attack", "the attack cursor: " + o["cursor"])
	check(o["kind"] == 5 and o["target"] == house and not o["capture"], "an attack on the barracks, no capture: %s" % o)

	print("2. C captures it")
	o = await order_with(KEY_C, house, men[0])
	check(o["cursor"] == "action-capture", "the capture cursor: " + o["cursor"])
	check(o["kind"] == 5 and o["target"] == house and o["capture"], "a capture of the barracks: %s" % o)

	print("3. F attacks it")
	o = await order_with(KEY_F, house, men[0])
	check(o["cursor"] == "action-attack", "the attack cursor: " + o["cursor"])
	check(o["kind"] == 5 and not o["capture"], "an attack, no capture: %s" % o)

	print("4. Ctrl attack-moves to it")
	o = await order_with(KEY_CTRL, house, men[0])
	check(o["cursor"] == "action-attack-move", "the attack-move cursor: " + o["cursor"])
	check(o["kind"] == 2, "an attack-walk: %s" % o)

	print("5. A click on an own man by an enemy barracks walks there (PickEntityAtPoint: the man)")
	var off: Vector2 = main.debug_spawn_offset("athen/infantry_spearman_b", 1)
	var by_man: int = main.debug_spawn_units("athen/infantry_spearman_b", 1)[0]
	# Its 4 x 4 footprint ends 0.7 from him, inside the 1.2 a click on it reaches beyond.
	main.debug_spawn_building(2, off.x, off.y + 2.7, 1)
	await frames(40)
	main.debug_select_units(PackedInt32Array([men[0]]))
	await frames(2)
	var by: Vector2 = main.debug_unit_screen(by_man)
	await move(by)
	var c := cursor()
	await right_click(by)
	o = main.debug_unit_order(men[0])
	check(c != "action-attack", "no attack cursor over him: " + c)
	check(o["kind"] == 1, "a walk, not an attack on the barracks: %s" % o)

	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
