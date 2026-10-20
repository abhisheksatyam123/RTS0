extends SceneTree
# 0.28's order modifiers on placement and selection (gui/session/input.js, selection.js):
#   1. Shift-placing a house queues it for the builders (tryPlaceBuilding "queued"): they
#      keep gathering, and placement goes on; Alt alone places once and ends it;
#   2. Alt orders the first selected unit able to do it (popOneFromSelection): a woman, not
#      the cavalryman ahead of her, for a house and for a resource;
#   3. a control group keeps and brings back every structure saved to it (performGroup);
#   4. a formation is selected as one (addFormationMembers) by Ctrl+click removal, a
#      double-click and a control group.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_order_one.gd
# Exit code 0 = pass.

const BLD_HOUSE := 1
const ORDER_GATHER := 6
const ORDER_BUILD := 7

var main
var failures := 0
var cfg := RtsoUserConfig.new()

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
	k.alt_pressed = down and code == KEY_ALT
	k.ctrl_pressed = down and code == KEY_CTRL
	Input.parse_input_event(k)
	await frames(1)

func key(code: Key, ctrl := false) -> void:
	for down in [true, false]:
		var k := InputEventKey.new()
		k.keycode = code
		k.physical_keycode = code
		k.pressed = down
		k.ctrl_pressed = ctrl
		Input.parse_input_event(k)
		await frames(1)
	await frames(12) # past the double-press window

func click(pos: Vector2, button := MOUSE_BUTTON_LEFT, double := false) -> void:
	await move(pos)
	for down in [true, false]:
		var mb := InputEventMouseButton.new()
		mb.button_index = button
		mb.pressed = down
		mb.double_click = double and down
		mb.position = pos
		mb.global_position = pos
		mb.shift_pressed = Input.is_key_pressed(KEY_SHIFT)
		mb.alt_pressed = Input.is_key_pressed(KEY_ALT)
		mb.ctrl_pressed = Input.is_key_pressed(KEY_CTRL)
		root.push_input(mb, true)
		await frames(1)
	await frames(2)

func unit(id: int) -> Dictionary:
	for u in main.debug_selection_state()["units"]:
		if u["id"] == id:
			return u
	return {}

func selected() -> Array:
	return Array(main.debug_selection_state()["selected"])

# A screen point near the Civic Center where a house can go (no placement tooltip).
func free_house_spot(centre: Vector2) -> Vector2:
	main.enter_build_mode(true, BLD_HOUSE)
	for dy in range(-240, 241, 60):
		for dx in range(-300, 301, 60):
			if abs(dx) < 120 and abs(dy) < 120:
				continue
			await move(centre + Vector2(dx, dy))
			if not main.debug_placement_tip()["visible"]:
				main.enter_build_mode(false)
				return centre + Vector2(dx, dy)
	main.enter_build_mode(false)
	return Vector2(-1, -1)

func run() -> void:
	await frames(60)
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
	var cc_screen: Vector2 = main.debug_selected_building()["screen"]
	main.clear_selection()

	print("1. Shift queues a placed house")
	var women: PackedInt32Array = main.debug_spawn_units("athen/support_female_citizen", 2)
	main.debug_gather_nearest() # every idle worker, these two too
	await turns(4)
	check(int(unit(women[0])["order_kind"]) == ORDER_GATHER, "the women gather")
	var spot := await free_house_spot(cc_screen)
	check(spot.x >= 0.0, "a free spot for a house")
	main.debug_select_units(women)
	main.enter_build_mode(true, BLD_HOUSE)
	await hold(KEY_SHIFT, true)
	await click(spot)
	await hold(KEY_SHIFT, false)
	await turns(3)
	var w: Dictionary = unit(women[0])
	check(int(w["order_kind"]) == ORDER_GATHER and int(w["queued_orders"]) >= 1,
			"they keep gathering with the house queued (order %d, %d queued)" % [int(w["order_kind"]), int(w["queued_orders"])])
	check(main.debug_placing(), "Shift keeps placing")
	main.enter_build_mode(false)

	print("2. Alt orders the first able unit")
	var cav: PackedInt32Array = main.debug_spawn_units("athen/cavalry_javelineer_b", 1)
	var woman: PackedInt32Array = main.debug_spawn_units("athen/support_female_citizen", 1)
	main.debug_select_units(PackedInt32Array([cav[0], woman[0]]))
	await frames(2)
	var spot2 := await free_house_spot(cc_screen)
	main.debug_select_units(PackedInt32Array([cav[0], woman[0]]))
	main.enter_build_mode(true, BLD_HOUSE)
	await hold(KEY_ALT, true)
	await click(spot2)
	await hold(KEY_ALT, false)
	check(selected() == [cav[0]], "Alt-placing sends the woman, the cavalryman stays selected: %s" % str(selected()))
	check(not main.debug_placing(), "Alt without Shift ends the placement")
	await turns(3)
	check(int(unit(woman[0])["order_kind"]) == ORDER_BUILD, "the woman goes to build")
	var res: int = int(unit(women[1])["target_resource"])
	var res_at: Vector2 = main.debug_resource_screen(res)
	var woman2: PackedInt32Array = main.debug_spawn_units("athen/support_female_citizen", 1)
	main.debug_select_units(PackedInt32Array([cav[0], woman2[0]]))
	await frames(2)
	if res_at.x >= 0.0:
		await hold(KEY_ALT, true)
		await click(res_at, MOUSE_BUTTON_RIGHT)
		await hold(KEY_ALT, false)
		await turns(3)
		check(selected() == [cav[0]], "Alt+right-click on a resource sends the woman: %s" % str(selected()))
		check(int(unit(woman2[0])["order_kind"]) == ORDER_GATHER, "she gathers it")
	else:
		check(false, "the women's resource is on screen")

	print("3. A control group keeps every structure")
	var h1: int = main.debug_spawn_building(BLD_HOUSE, 30.0, 30.0)
	var h2: int = main.debug_spawn_building(BLD_HOUSE, -30.0, 30.0)
	main.debug_select_structures(PackedInt32Array([cc, h1, h2]))
	await frames(2)
	await key(KEY_3, true)
	main.clear_selection()
	await frames(2)
	await key(KEY_3)
	check(main.debug_selected_structures().size() == 3, "the group brings back its 3 structures: %s" % str(main.debug_selected_structures()))
	main.clear_selection()

	print("4. A formation is selected as one")
	var keep_one: String = cfg.get_value("gui.session.selectformationasone")
	cfg.set_value("gui.session.selectformationasone", "true")
	var spears: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 2)
	var archers: PackedInt32Array = main.debug_spawn_units("athen/infantry_archer_b", 2)
	var band := PackedInt32Array(spears)
	band.append_array(archers)
	main.debug_select_units(band)
	main._on_formation_selected(2) # box
	await turns(4)
	var formed := 0
	for id in band:
		if int(unit(id)["formation"]) != 0:
			formed += 1
	check(formed == 4, "4 soldiers in a box (%d)" % formed)
	main.debug_select_units(band)
	await frames(2)
	await hold(KEY_CTRL, true)
	await click(main.debug_unit_screen(archers[0]))
	await hold(KEY_CTRL, false)
	var left := selected().filter(func(id): return band.has(id))
	check(left.is_empty(), "Ctrl+click drops the whole formation: %s" % str(left))
	main.clear_selection()
	await create_timer(0.8).timeout
	var at: Vector2 = main.debug_unit_screen(spears[0])
	await click(at)
	await click(at, MOUSE_BUTTON_LEFT, true)
	var got := selected().filter(func(id): return band.has(id))
	check(got.size() == 4, "a double-click brings the archers of the formation too: %d of 4" % got.size())
	main.clear_selection()
	await create_timer(0.8).timeout
	main.debug_select_units(PackedInt32Array([spears[0]]))
	await frames(2)
	await key(KEY_4, true)
	main.clear_selection()
	await frames(2)
	await key(KEY_4)
	got = selected().filter(func(id): return band.has(id))
	check(got.size() == 4, "a control group of one member brings its formation: %d of 4" % got.size())
	cfg.set_value("gui.session.selectformationasone", keep_one)

	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
