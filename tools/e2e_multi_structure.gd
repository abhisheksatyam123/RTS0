extends SceneTree
# selection.js / input.js: several of the player's structures can be selected at once
# (Shift+click, double-click on a type, a band box with no units in it, Ctrl+click to
# drop one), each is outlined, and training spreads over every selected structure that
# trains the unit (addTrainingToQueue, flushTrainingBatch).
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_multi_structure.gd
# Exit code 0 = pass.

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	# The batch Shift trains comes from user.cfg; pin 0 A.D.'s default (in memory only).
	RtsoUserConfig.new().set_value("gui.session.batchtrainingsize", "5")
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
	await frames(1)

func key(code: int, down: bool) -> void:
	var k := InputEventKey.new()
	k.keycode = code
	k.physical_keycode = code
	k.shift_pressed = down and code == KEY_SHIFT
	k.ctrl_pressed = down and code == KEY_CTRL
	k.pressed = down
	Input.parse_input_event(k)
	await frames(1)

func left(pos: Vector2, down: bool, double := false) -> void:
	var mb := InputEventMouseButton.new()
	mb.button_index = MOUSE_BUTTON_LEFT
	mb.pressed = down
	mb.double_click = double
	mb.position = pos
	mb.global_position = pos
	root.push_input(mb, true)
	await frames(1)

func click(pos: Vector2, double := false) -> void:
	await move(pos)
	await left(pos, true, double)
	await left(pos, false)
	await frames(2)

func structs() -> Array:
	return Array(main.debug_selected_structures())

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
	for res in 4:
		main.debug_set_resource(res, 5000)
	var b1: int = main.debug_spawn_building(2, -4, -14)
	var b2: int = main.debug_spawn_building(2, 8, -14)
	await frames(10)
	main.debug_select_entity(b2)
	await frames(2)
	var s2: Vector2 = main.debug_selected_building()["screen"]
	main.debug_select_entity(b1)
	await frames(2)
	var s1: Vector2 = main.debug_selected_building()["screen"]
	print("    barracks ", b1, " at ", s1, ", ", b2, " at ", s2)

	print("1. Shift+click adds a structure")
	await key(KEY_SHIFT, true)
	await click(s2 + Vector2(0, -12))
	await key(KEY_SHIFT, false)
	print("    ", structs())
	check(structs() == [b1, b2], "both barracks are selected")
	check(main.debug_structure_outlined(b1) and main.debug_structure_outlined(b2), "both are outlined")

	var grid: Dictionary = main.debug_selection_grid(false)
	var icons: Array = grid["icons"]
	print("    grid ", icons.map(func(e): return str(e["tip"]).get_slice("\n", 0)))
	check(icons.size() == 1 and str(icons[0]["tip"]).contains("Barracks"), "the details show one Barracks group")
	var ins: Dictionary = main.debug_inspector("current")
	var cparts: Array = ins["capture_multiple_parts"]
	print("    count ", ins["count"], " health ", ins["health_multiple"], " capture ", ins["capture_multiple"], " parts ", cparts)
	check(String(ins["count"]) == "2" and String(ins["health_multiple"]).begins_with("Health:"), "displayMultiple: the count in large text and a vertical health bar")
	check(cparts.size() == 1 and float(cparts[0]) > 35.0 and String(ins["capture_multiple"]) != "", "captureMultiple: their summed capture points show, all the owner's")

	print("2. Training spreads over them")
	var rl: ItemList = null
	for n in main.find_children("*", "ItemList", true, false):
		if n.is_visible_in_tree() and n.item_count > 0:
			rl = n
	var pick := 0
	for i in rl.item_count:
		if not rl.is_item_disabled(i):
			pick = i
			break
	main._on_roster_item_clicked(pick, Vector2(), MOUSE_BUTTON_LEFT)
	await frames(6)
	print("    queues ", main.debug_building_queue(b1), " ", main.debug_building_queue(b2))
	check(main.debug_building_queue(b1) == 1 and main.debug_building_queue(b2) == 1, "a click trains one in each")
	await key(KEY_SHIFT, true)
	main._on_roster_item_clicked(pick, Vector2(), MOUSE_BUTTON_LEFT)
	await frames(2)
	await key(KEY_SHIFT, false)
	for i in 60: # the batch is posted as a command, so it lands a turn later
		await frames(1)
		if main.debug_building_queue(b1) == 6 and main.debug_building_queue(b2) == 6:
			break
	print("    queues ", main.debug_building_queue(b1), " ", main.debug_building_queue(b2))
	check(main.debug_building_queue(b1) == 6 and main.debug_building_queue(b2) == 6, "a batch of 5 goes to each")
	var producers := []
	for i in 6:
		var slot: Dictionary = main.debug_queue_slot(i)
		if slot.get("visible", false) and not slot.get("ghost", false):
			producers.append(int(slot["producer"]))
	print("    merged queue producers ", producers)
	check(producers == [b1, b2, b1, b2], "the queue panel merges both queues, first items first")
	var ev := InputEventMouseButton.new()
	ev.button_index = MOUSE_BUTTON_LEFT
	ev.pressed = true
	main._on_queue_slot_gui_input(ev, 1)
	for i in 40:
		await frames(1)
		if main.debug_building_queue(b2) == 5:
			break
	print("    after cancelling slot 2: ", main.debug_building_queue(b1), " ", main.debug_building_queue(b2))
	check(main.debug_building_queue(b1) == 6 and main.debug_building_queue(b2) == 5, "a click on the second slot cancels the second barracks' item")
	var roster_one := rl.item_count
	var cc: int = main.debug_civic_centre_id(0)
	main.debug_select_entity(cc)
	await frames(2)
	var roster_cc := rl.item_count
	OS.delay_msec(600)
	await key(KEY_SHIFT, true)
	await click(s1 + Vector2(0, -12))
	await key(KEY_SHIFT, false)
	await frames(4)
	print("    roster: barracks ", roster_one, ", civic centre ", roster_cc, ", both ", rl.item_count, " structs ", structs())
	check(structs() == [cc, b1] and rl.item_count > roster_cc and rl.item_count > roster_one,
			"the roster lists what either structure trains")
	main.debug_select_entity(b1)
	await frames(2)
	await key(KEY_SHIFT, true)
	await click(s2 + Vector2(0, -12))
	await key(KEY_SHIFT, false)
	await frames(2)

	print("3. Ctrl+click drops one, a plain click selects one alone")
	await key(KEY_CTRL, true)
	await click(s2 + Vector2(0, -12))
	await key(KEY_CTRL, false)
	check(structs() == [b1], "Ctrl+click removed the second")
	check(not main.debug_structure_outlined(b2), "and its outline")

	print("4. Double-click selects every barracks on screen")
	await click(s1 + Vector2(0, -12))
	await click(s1 + Vector2(0, -12), true)
	print("    ", structs())
	check(structs().has(b1) and structs().has(b2), "double-click took both barracks")
	OS.delay_msec(600) # past the triple-click window
	await click(s1 + Vector2(0, -12))
	check(structs() == [b1], "a plain click keeps just the one")

	print("5. A band box with no units takes the structures in it")
	var lo := Vector2(minf(s1.x, s2.x) - 25, minf(s1.y, s2.y) - 25)
	var hi := Vector2(maxf(s1.x, s2.x) + 25, maxf(s1.y, s2.y) + 25)
	await move(lo)
	await left(lo, true)
	for k in range(1, 9):
		await move(lo.lerp(hi, k / 8.0))
	await left(hi, false)
	await frames(2)
	print("    ", structs(), " units ", main.debug_selection_grid(false)["selected"])
	check(structs().has(b1) and structs().has(b2), "the box selected both barracks")

	print("6. With units too, the group icon keeps only the structures")
	var men: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 2)
	main.debug_select_units(men)
	await frames(2)
	OS.delay_msec(600)
	await key(KEY_SHIFT, true)
	await click(s1 + Vector2(0, -12))
	await key(KEY_SHIFT, false)
	await frames(2)
	icons = main.debug_selection_grid(false)["icons"]
	print("    grid ", icons.map(func(e): return str(e["tip"]).get_slice("\n", 0)), " structs ", structs())
	check(icons.size() == 2, "a unit group and a Barracks group")
	var bar_icon = null
	for e in icons:
		if str(e["tip"]).contains("Barracks"):
			bar_icon = instance_from_id(int(e["id"]))
	if bar_icon:
		bar_icon.emit_signal("pressed")
	await frames(2)
	check(main.debug_selection_grid(false)["selected"] == 0 and structs() == [b1], "pressing it keeps only the Barracks")

	print("7. Commands act on every selected structure")
	OS.delay_msec(600)
	await key(KEY_SHIFT, true)
	await click(s2 + Vector2(0, -12))
	await key(KEY_SHIFT, false)
	await frames(2)
	check(structs() == [b1, b2], "both barracks again")
	main._on_entity_command("autoqueue-on")
	await frames(10)
	var aq := []
	for id in [b1, b2]:
		main.debug_select_entity(id)
		await frames(2)
		aq.append(bool(main.debug_selected_building()["autoqueue"]))
	print("    autoqueue ", aq)
	check(aq == [true, true], "autoqueue turns on in both")
	main.debug_select_entity(b1)
	await frames(2)
	OS.delay_msec(600)
	await key(KEY_SHIFT, true)
	await click(s2 + Vector2(0, -12))
	await key(KEY_SHIFT, false)
	await frames(2)
	print("    selected ", structs())
	var before := int(main.debug_structure_types().get(0, {}).get("Barracks", 0))
	main._on_entity_command("delete")
	await frames(1)
	# DeleteSelectionConfirmation: Yes.
	for q in main.get_children().filter(func(c): return c.get_script() == load("res://scenes/session/quit_confirmation.gd")):
		q.press(1)
	await frames(20)
	var after := int(main.debug_structure_types().get(0, {}).get("Barracks", 0))
	print("    barracks ", before, " -> ", after)
	check(before - after == 2, "delete destroys both")
	print("FAILURES: ", failures)
	quit(1 if failures > 0 else 0)
