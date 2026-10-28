extends SceneTree
# 0.28 gui/session/selection.js and input.js with someone else's entity selected (here an ally's):
#   1. Esc (selection.cancel -> g_Selection.reset()) drops an inspected other unit;
#   2. while it is the sole selection, Shift+click on one's own unit adds nothing (addList);
#   3. Ctrl+click on it removes it (removeList);
#   4. a double-click on it picks the player's own units of its template on screen
#      (PickSimilarPlayerEntities names no player);
#   5. a structure captured away leaves a multi-selection, the first one too (update);
#   6. livestock icons go in template order, not pick order (getKeys sorts).
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_selection_foreign.gd
# Exit code 0 = pass.

const BLD_HOUSE := 1
const FAUNA_SHEEP := 2
const FAUNA_GOAT := 31

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
	ms.slot_types = [0, 0] # no AI
	ms.slot_teams = [1, 1] # an ally: its units stand by ours without a fight
	ms.cheats = true # debug_convert
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

func key(code: Key, pressed: bool) -> void:
	var k := InputEventKey.new()
	k.keycode = code
	k.physical_keycode = code
	k.pressed = pressed
	k.shift_pressed = pressed and code == KEY_SHIFT
	k.ctrl_pressed = pressed and code == KEY_CTRL
	Input.parse_input_event(k)
	await frames(2)

func move(pos: Vector2) -> void:
	var mm := InputEventMouseMotion.new()
	mm.position = root.get_final_transform() * pos
	mm.global_position = mm.position
	Input.parse_input_event(mm)
	await frames(2)

func button(pos: Vector2, pressed: bool, double := false, shift := false, ctrl := false) -> void:
	var mb := InputEventMouseButton.new()
	mb.button_index = MOUSE_BUTTON_LEFT
	mb.pressed = pressed
	mb.double_click = double
	mb.shift_pressed = shift
	mb.ctrl_pressed = ctrl
	mb.position = pos
	mb.global_position = pos
	root.push_input(mb, true)
	await frames(2)

func click(pos: Vector2, double := false, shift := false, ctrl := false) -> void:
	await move(pos)
	await button(pos, true, double, shift, ctrl)
	await button(pos, false, false, shift, ctrl)

func state() -> Dictionary:
	return main.debug_selection_state()

func selected() -> Array:
	return Array(state()["selected"])

# A screen spot whose hover highlights unit `id` (something may stand in front of its feet).
func spot_of(id: int) -> Vector2:
	var p: Vector2 = main.debug_unit_screen(id)
	for off in [Vector2.ZERO, Vector2(0, -12), Vector2(0, -24), Vector2(0, 8)]:
		await move(p + off)
		await frames(3)
		if main.debug_highlight().has(id):
			return p + off
	return p

func run() -> void:
	await frames(60)
	var mid: Vector2 = root.get_visible_rect().size / 2
	await move(mid)
	var roof: Dictionary = main.debug_civic_centre_roof()
	for i in 200: # the camera settles on our Civic Centre
		await frames(1)
		var r2: Dictionary = main.debug_civic_centre_roof()
		var still: bool = (r2["roof"] as Vector2).distance_to(roof["roof"]) < 0.5
		roof = r2
		if still and i > 10:
			break
	var row: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 3)
	var mine := PackedInt32Array([row[0], row[2]])
	var other := PackedInt32Array([row[1]])
	# Out in front of the Civic Centre (camera side), with the camera over them.
	var cc: int = main.debug_civic_centre_id(0)
	var cc_pos: Vector2 = main.debug_building_fow(cc)["pos"]
	main.debug_select_units(row)
	main._on_minimap_order(cc_pos + Vector2(0, 16))
	await turns(40)
	main.debug_convert(other, 1) # the middle one is the ally's
	var c: Dictionary = main.debug_camera()
	var ground: Vector2 = main.debug_unit_ground(other[0])
	c["x"] = ground.x
	c["z"] = ground.y
	main.debug_set_camera_data(c)
	await frames(10)
	main.clear_selection()
	await frames(2)

	print("1. Esc drops an inspected other")
	var at := await spot_of(other[0])
	await click(at)
	check(int(state()["inspected"]) == other[0] and selected().is_empty(), "a click inspects the other spearman: %s" % state())
	await key(KEY_ESCAPE, true)
	await key(KEY_ESCAPE, false)
	check(int(state()["inspected"]) == 0, "Esc drops it: %s" % state())

	print("2. Shift+click adds nothing to a lone other")
	at = await spot_of(other[0])
	await click(at)
	var mine_at := await spot_of(mine[0])
	await key(KEY_SHIFT, true)
	await click(mine_at, false, true)
	await key(KEY_SHIFT, false)
	check(selected().is_empty() and int(state()["inspected"]) == other[0], "the other stays the selection: %s" % state())
	await click(mine_at)
	check(selected().size() == 1 and Array(mine).has(selected()[0]), "a plain click there picks our spearman: %s" % str(selected()))

	print("3. Ctrl+click removes the inspected other")
	at = await spot_of(other[0])
	await click(at)
	check(int(state()["inspected"]) == other[0], "inspected again")
	await key(KEY_CTRL, true)
	await click(at, false, false, true)
	await key(KEY_CTRL, false)
	check(int(state()["inspected"]) == 0 and selected().is_empty(), "Ctrl+click drops it: %s" % state())

	print("4. Double-click on the other picks our own of its template")
	at = await spot_of(other[0])
	await click(at)
	await move(at)
	await button(at, true, true)
	await button(at, false)
	var s := selected()
	check(s.has(mine[0]) and s.has(mine[1]) and not s.has(other[0]) and int(state()["inspected"]) == 0,
			"our two spearmen: %s" % state())

	print("5. A captured structure leaves a multi-selection")
	var h1: int = main.debug_spawn_building(BLD_HOUSE, -24.0, 20.0)
	var h2: int = main.debug_spawn_building(BLD_HOUSE, 24.0, 20.0)
	main.debug_select_structures(PackedInt32Array([h1, h2]))
	await frames(2)
	check(Array(main.debug_selected_structures()) == [h1, h2], "both houses: %s" % str(main.debug_selected_structures()))
	main.debug_convert(PackedInt32Array([h1]), 1) # captured away
	await turns(4)
	var left := Array(main.debug_selected_structures())
	check(left == [h2] and int(state()["building"]) == h2, "the first one, captured, leaves: %s" % str(left))

	print("6. Livestock icons in template order")
	var sheep: PackedInt32Array = main.debug_spawn_fauna(FAUNA_SHEEP, 2, true, 10.0, -16.0)
	var goats: PackedInt32Array = main.debug_spawn_fauna(FAUNA_GOAT, 2, true, -10.0, -16.0)
	await turns(2)
	var ids := PackedInt32Array()
	ids.append_array(sheep)
	ids.append_array(goats)
	main.debug_select_units(ids)
	await frames(4)
	var tips: Array = []
	for e in main.debug_selection_grid(false)["icons"]:
		if e["visible"]:
			tips.append(String(e["tip"]))
	check(tips.size() == 2 and tips[0].find("Goat") >= 0 and tips[1].find("Sheep") >= 0, "goats before sheep: %s" % str(tips))

	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
