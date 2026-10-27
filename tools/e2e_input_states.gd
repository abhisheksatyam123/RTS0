extends SceneTree
# 0.28 gui/session/input.js input states:
#   1. a click on an invalid placement plays the error, sends nothing and keeps placing
#      (tryPlaceBuilding, INPUT_BUILDING_PLACEMENT);
#   2. a placement press starts turning the building only dragdelta squared (16 px) from the
#      click (INPUT_BUILDING_CLICK: distanceTo >= Math.square(getMaxDragDelta()));
#   3. control-group keys work only in INPUT_NORMAL (not while placing), and recalling an
#      empty group empties the selection (performGroup: g_Selection.reset());
#   4. with session.flare (K) held a right press sends a flare; the flare button does not arm
#      while placing (MiniMapFlareButton.onPress, INPUT_NORMAL only);
#   5. a structure's rally point goes where the right button comes up (INPUT_UNIT_POSITION_START);
#   6. a left click while the right button is held drops the right-click order;
#   7. a double click keeps the unit its first press hit (clickedEntity), even if the second
#      press misses.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_input_states.gd
# Exit code 0 = pass.

const BLD_HOUSE := 1

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	var ms = root.get_node("MatchSettings")
	ms.map_id = "random_continental"
	ms.seed = 11
	ms.map_size = 128
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func key(code: Key, pressed: bool, ctrl := false) -> void:
	var k := InputEventKey.new()
	k.keycode = code
	k.physical_keycode = code
	k.pressed = pressed
	k.ctrl_pressed = ctrl
	Input.parse_input_event(k)
	await frames(2)

func tap(code: Key, ctrl := false) -> void:
	if ctrl:
		await key(KEY_CTRL, true, true)
	await key(code, true, ctrl)
	await key(code, false, ctrl)
	if ctrl:
		await key(KEY_CTRL, false)

func move(pos: Vector2) -> void:
	var mm := InputEventMouseMotion.new()
	mm.position = root.get_final_transform() * pos
	mm.global_position = mm.position
	Input.parse_input_event(mm)
	await frames(2)

func button(pos: Vector2, index: MouseButton, pressed: bool, double := false) -> void:
	var mb := InputEventMouseButton.new()
	mb.button_index = index
	mb.pressed = pressed
	mb.double_click = double
	mb.position = pos
	mb.global_position = pos
	root.push_input(mb, true)
	await frames(2)

func click(pos: Vector2, index := MOUSE_BUTTON_LEFT) -> void:
	await move(pos)
	await button(pos, index, true)
	await button(pos, index, false)

func selected() -> Array:
	return Array(main.debug_selection_state()["selected"])

func turns(n: int) -> void:
	var until: int = int(main.get_replay_status().turn) + n
	var guard := 0
	while int(main.get_replay_status().turn) < until and guard < n * 40:
		await process_frame
		guard += 1

func run() -> void:
	await frames(60)
	var mid: Vector2 = root.get_visible_rect().size / 2
	await move(mid) # at the corner the camera would scroll away
	var roof: Dictionary = main.debug_civic_centre_roof()
	for i in 200: # the camera settles on our Civic Centre
		await frames(1)
		var r2: Dictionary = main.debug_civic_centre_roof()
		var still: bool = (r2["roof"] as Vector2).distance_to(roof["roof"]) < 0.5
		roof = r2
		if still and i > 10:
			break
	var cc: int = main.debug_civic_centre_id(0)
	main.debug_select_entity(cc)
	await frames(2)
	var cc_at: Vector2 = main.debug_selected_building()["screen"] # its middle, on the ground
	main.clear_selection()
	var men: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 2)
	await turns(3)
	print("    order before ", main.debug_unit_order(men[0]))

	print("1. An invalid placement")
	main.debug_select_units(men)
	await frames(2)
	main.enter_build_mode(true, BLD_HOUSE)
	await move(cc_at)
	await frames(3)
	print("    vp ", root.get_visible_rect().size, " cc ", cc_at, " tip ", main.debug_placement_tip())
	await click(cc_at) # on the Civic Centre
	await turns(2)
	check(main.debug_input_state()["build_mode"], "still placing after a click on the Civic Centre")
	check(int(main.debug_unit_order(men[0])["kind"]) == 0, "no build order: %s" % main.debug_unit_order(men[0]))
	check(selected().size() == 2, "the builders stay selected: %s" % str(selected()))

	print("2. Placement drag threshold")
	main.enter_build_mode(true, BLD_HOUSE)
	await frames(2)
	var at := cc_at + Vector2(0, 120)
	var start_angle: float = main.debug_input_state()["placement_angle"]
	await move(at)
	await button(at, MOUSE_BUTTON_LEFT, true)
	await move(at + Vector2(10, 0))
	check(is_equal_approx(main.debug_input_state()["placement_angle"], start_angle), "10 px: the facing stays")
	await move(at + Vector2(40, 0))
	check(not is_equal_approx(main.debug_input_state()["placement_angle"], start_angle), "40 px: it turns to the cursor")
	await move(at + Vector2(6, 0))
	check(is_equal_approx(main.debug_input_state()["placement_angle"], start_angle), "back near the click: the default facing")
	main.enter_build_mode(false)
	await button(at, MOUSE_BUTTON_LEFT, false)
	await frames(2)

	print("3. Control groups")
	main.debug_select_units(PackedInt32Array([men[0]]))
	await frames(2)
	await tap(KEY_1, true)
	main.debug_select_units(PackedInt32Array([men[1]]))
	await frames(2)
	main.enter_build_mode(true, BLD_HOUSE)
	await frames(2)
	await tap(KEY_1)
	check(selected() == [men[1]], "1 while placing keeps the selection: %s" % str(selected()))
	main.enter_build_mode(false)
	await frames(2)
	await tap(KEY_1)
	check(selected() == [men[0]], "then 1 recalls group 1: %s" % str(selected()))
	await tap(KEY_5)
	check(selected().is_empty(), "an empty group 5 deselects all: %s" % str(selected()))

	print("4. Flares")
	var flared: float = main.debug_input_state()["last_flare_time"]
	await key(KEY_K, true)
	await click(at, MOUSE_BUTTON_RIGHT)
	await key(KEY_K, false)
	check(main.debug_input_state()["last_flare_time"] > flared, "K + right press sends a flare")
	main.enter_build_mode(true, BLD_HOUSE)
	await frames(2)
	main._on_flare_button_pressed()
	check(not main.debug_input_state()["flare_armed"], "the flare button does not arm while placing")
	main.enter_build_mode(false)
	await frames(2)

	print("5. Rally point on release")
	main.debug_select_entity(cc)
	await frames(2)
	var p0 := cc_at + Vector2(-120, 80)
	var p1 := cc_at + Vector2(120, 80)
	await click(p1, MOUSE_BUTTON_RIGHT)
	await turns(2)
	var r1: Vector2 = main.debug_selected_building().get("rally", Vector2.ZERO)
	await click(p0, MOUSE_BUTTON_RIGHT)
	await turns(2)
	var r0: Vector2 = main.debug_selected_building().get("rally", Vector2.ZERO)
	check(r0.distance_to(r1) > 4.0, "two rally spots: %s %s" % [r0, r1])
	await move(p0)
	await button(p0, MOUSE_BUTTON_RIGHT, true)
	await move(p1)
	await button(p1, MOUSE_BUTTON_RIGHT, false)
	await turns(2)
	var r: Vector2 = main.debug_selected_building().get("rally", Vector2.ZERO)
	check(r.distance_to(r1) < 1.0, "press at one, release at the other: the flag at the release %s" % r)

	print("6. Left click while the right button is held")
	main.debug_select_units(PackedInt32Array([men[0]]))
	await frames(2)
	await move(p1)
	await button(p1, MOUSE_BUTTON_RIGHT, true)
	await button(p1, MOUSE_BUTTON_LEFT, true)
	await button(p1, MOUSE_BUTTON_LEFT, false)
	await button(p1, MOUSE_BUTTON_RIGHT, false)
	await turns(2)
	check(int(main.debug_unit_order(men[0])["kind"]) != 1, "no walk order: %s" % main.debug_unit_order(men[0]))
	check(selected() == [men[0]], "the selection stays: %s" % str(selected()))

	print("7. Double click keeps the first press's unit")
	# Out in front of the Civic Centre (camera side), with the camera over them.
	var cc_pos: Vector2 = main.debug_building_fow(cc)["pos"]
	main.debug_select_units(men)
	main._on_minimap_order(cc_pos + Vector2(0, 16))
	await turns(40)
	var c: Dictionary = main.debug_camera()
	var ground: Vector2 = main.debug_unit_ground(men[0])
	c["x"] = ground.x
	c["z"] = ground.y
	main.debug_set_camera_data(c)
	await frames(10)
	main.clear_selection()
	await frames(2)
	# A spot where a spearman picks himself (something can stand in front of his feet).
	var u_at: Vector2 = main.debug_unit_screen(men[0])
	for id in men:
		var p: Vector2 = main.debug_unit_screen(id)
		for off in [Vector2.ZERO, Vector2(0, -12), Vector2(0, -24)]:
			await move(p + off)
			await frames(3)
			if main.debug_highlight().has(id):
				u_at = p + off
				break
		if main.debug_highlight().has(id):
			break
	await click(u_at)
	check(selected().size() == 1, "the first click picks a spearman: %s" % str(selected()))
	var miss := u_at + Vector2(0, 90)
	for off in [Vector2(0, 90), Vector2(90, 0), Vector2(-90, 0), Vector2(0, -90), Vector2(70, 70)]:
		await move(u_at + off)
		await frames(3)
		if main.debug_highlight().is_empty():
			miss = u_at + off
			break
	check(main.debug_highlight().is_empty(), "the second press misses")
	await button(miss, MOUSE_BUTTON_LEFT, true, true)
	await button(miss, MOUSE_BUTTON_LEFT, false)
	check(selected().has(men[0]) and selected().has(men[1]), "both spearmen (its template) selected: %s" % str(selected()))

	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
