extends SceneTree
# 0.28's click actions (gui/session/input.js, unit_actions.js, minimap/MiniMap.js):
#   1. a right-click while an action is armed only drops it: no order (INPUT_PRESELECTEDACTION);
#   2. a left click the armed action can't use ends it, unless Shift is held; on the minimap too;
#   3. a right-click on an allied unit walks there ("attack" needs an enemy);
#   4. cavalry right-clicking an own field walk there, with no cursor ("repair" needs a builder);
#   5. Shift+click on a unit keeps the selected structure (addList);
#   6. a right-click on the selected Civic Center with no rally point sets none;
#   7. Ctrl over an own Civic Center shows the garrison cursor for a carrier (hotkeys first).
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_click_actions.gd
# Exit code 0 = pass.

const BLD_FIELD := 14
const ORDER_WALK := 1
const RES_WOOD := 0

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
	ms.slot_teams = [1, 1]
	ms.slot_types = [0, 0]
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func turns(n: int) -> void:
	var until: int = int(main.get_replay_status().turn) + n
	var guard := 0
	while int(main.get_replay_status().turn) < until and guard < n * 40:
		await process_frame
		guard += 1

func move(pos: Vector2) -> void:
	var mm := InputEventMouseMotion.new()
	mm.position = root.get_final_transform() * pos
	mm.global_position = mm.position
	Input.parse_input_event(mm)
	await frames(2)

func hold(code: Key, down: bool) -> void:
	var k := InputEventKey.new()
	k.keycode = code
	k.physical_keycode = code
	k.pressed = down
	k.shift_pressed = down and code == KEY_SHIFT
	k.ctrl_pressed = down and code == KEY_CTRL
	Input.parse_input_event(k)
	await frames(1)

func click(pos: Vector2, button := MOUSE_BUTTON_LEFT) -> void:
	await move(pos)
	for down in [true, false]:
		var mb := InputEventMouseButton.new()
		mb.button_index = button
		mb.pressed = down
		mb.position = pos
		mb.global_position = pos
		mb.shift_pressed = Input.is_key_pressed(KEY_SHIFT)
		mb.ctrl_pressed = Input.is_key_pressed(KEY_CTRL)
		root.push_input(mb, true)
		await frames(1)
	await frames(2)

func unit(id: int) -> Dictionary:
	for u in main.debug_selection_state()["units"]:
		if u["id"] == id:
			return u
	return {}

func settle() -> void:
	var r: Dictionary = main.debug_civic_centre_roof()
	for i in 200:
		await frames(1)
		var r2: Dictionary = main.debug_civic_centre_roof()
		var still: bool = (r2["roof"] as Vector2).distance_to(r["roof"]) < 0.5
		r = r2
		if still and i > 10:
			break

func run() -> void:
	await frames(60)
	await move(Vector2(512, 400))
	await settle()
	var cc: int = main.debug_civic_centre_id(0)
	main.debug_select_entity(cc)
	await frames(4)
	var cc_screen: Vector2 = main.debug_selected_building()["screen"]
	main.clear_selection()
	var men: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 2)
	await turns(2)
	var open_ground: Vector2 = main.debug_unit_screen(men[0]) + Vector2(0, 60)

	print("1. A right-click drops the armed action and orders nothing")
	main.debug_select_units(men)
	main._on_entity_command("garrison")
	check(main.debug_armed(), "Garrison is armed")
	var before: Vector2 = unit(men[0])["pos"]
	await click(open_ground, MOUSE_BUTTON_RIGHT)
	await turns(6)
	check(not main.debug_armed(), "the right-click drops it")
	check(int(unit(men[0])["order_kind"]) != ORDER_WALK and (unit(men[0])["pos"] as Vector2).distance_to(before) < 0.5,
			"and the spearmen stay put (order %d)" % int(unit(men[0])["order_kind"]))

	print("2. A click the armed action can't use ends it")
	main._on_entity_command("garrison")
	await click(open_ground)
	check(not main.debug_armed(), "a left click on open ground ends Garrison")
	main._on_entity_command("garrison")
	await hold(KEY_SHIFT, true)
	await click(open_ground)
	await hold(KEY_SHIFT, false)
	check(main.debug_armed(), "with Shift it stays armed")
	var cam_before: Vector2 = main.debug_camera()["target"]
	main._on_minimap_clicked(cam_before + Vector2(40, 40))
	check(not main.debug_armed(), "a minimap click ends it")
	check((main.debug_camera()["target"] as Vector2).distance_to(cam_before) < 0.1, "and leaves the camera")

	print("3. A right-click on an allied unit walks there")
	var ally: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 1, 1)
	await turns(2)
	main.debug_select_units(PackedInt32Array([men[0]]))
	var ally_pos: Vector2 = main.debug_unit_pos(ally[0])
	main._on_minimap_clicked(ally_pos)
	await settle()
	await frames(30)
	await click(main.debug_unit_screen(ally[0]), MOUSE_BUTTON_RIGHT)
	await turns(3)
	check(int(unit(men[0])["order_kind"]) == ORDER_WALK, "the spearman walks to the ally (order %d)" % int(unit(men[0])["order_kind"]))
	main._on_minimap_clicked(before)
	await settle()
	await frames(30)

	print("4. Cavalry on an own field walk there")
	var field: int = main.debug_spawn_building(BLD_FIELD, 0.0, 28.0)
	var cav: PackedInt32Array = main.debug_spawn_units("athen/cavalry_javelineer_b", 1)
	await turns(2)
	main.debug_select_units(cav)
	await frames(2)
	var field_at: Vector2 = main.debug_building_screen(field)
	await move(field_at)
	check(str(main.debug_cursor_tip()["cursor"]) == "", "no cursor over the field: '%s'" % str(main.debug_cursor_tip()["cursor"]))
	await click(field_at, MOUSE_BUTTON_RIGHT)
	await turns(3)
	check(int(unit(cav[0])["order_kind"]) == ORDER_WALK, "the cavalryman walks there (order %d)" % int(unit(cav[0])["order_kind"]))

	print("5. Shift+click on a unit keeps the selected structure")
	main.debug_select_entity(cc)
	await frames(2)
	await hold(KEY_SHIFT, true)
	await click(main.debug_unit_screen(men[1]))
	await hold(KEY_SHIFT, false)
	var s: Dictionary = main.debug_selection_state()
	check(int(s["building"]) == cc and Array(s["selected"]).has(men[1]), "Civic Center and spearman both selected: building %d, %s" % [int(s["building"]), str(s["selected"])])
	main.clear_selection()

	print("6. No rally point from a right-click on the selected Civic Center")
	await frames(14)
	main.debug_select_entity(cc)
	await frames(2)
	check(not main.debug_selection_state().has("rally"), "no rally point yet")
	await move(cc_screen)
	check(str(main.debug_cursor_tip()["cursor"]) == "", "no cursor over itself: '%s'" % str(main.debug_cursor_tip()["cursor"]))
	await click(cc_screen, MOUSE_BUTTON_RIGHT)
	await turns(3)
	check(not main.debug_selection_state().has("rally"), "still none after the right-click")
	main.clear_selection()

	print("7. Ctrl over the Civic Center: the garrison cursor for a carrier")
	var woman: PackedInt32Array = main.debug_spawn_units("athen/support_female_citizen", 1)
	main.debug_set_carry(woman[0], RES_WOOD, 5)
	main.debug_select_units(woman)
	await frames(2)
	await move(cc_screen)
	var plain: String = str(main.debug_cursor_tip()["cursor"])
	await hold(KEY_CTRL, true)
	await move(cc_screen + Vector2(1, 0))
	var held: String = str(main.debug_cursor_tip()["cursor"])
	await hold(KEY_CTRL, false)
	check(plain == "action-return-wood", "without Ctrl she returns wood: '%s'" % plain)
	check(held == "action-garrison", "with Ctrl she garrisons: '%s'" % held)

	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
