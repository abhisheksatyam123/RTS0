extends SceneTree
# Unit orders through the UI, as a human player gives them (0 A.D. 0.28 gui/session/input.js,
# unit_actions.js and hotkeys default.cfg): every order is a real mouse click or key press,
# and the check reads the order the unit took.
#   1. a right-click on open ground walks the selected soldiers there (ORDER_WALK);
#   2. a right-click on an enemy attacks it (ORDER_ATTACK);
#   3. Ctrl + right-click on open ground is attack-move (ORDER_WALK_AND_FIGHT);
#   4. holding P and left-clicking ground patrols (ORDER_PATROL);
#   5. the stance buttons, clicked, light only the one chosen (0.28's Defensive, Standground);
#   6. a right-click on a resource gathers it (ORDER_GATHER), and the same on a foundation
#      while J is held repairs it (ORDER_BUILD);
#   7. a right-click on open ground with a Civic Centre selected sets its rally point;
#   8. the Delete key destroys the selected soldier (session.kill).
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_unit_orders_ui.gd
# Exit code 0 = pass.

const ORDER_WALK := 1
const ORDER_WALK_AND_FIGHT := 2
const ORDER_PATROL := 3
const ORDER_ATTACK := 4
const ORDER_GATHER := 6
const ORDER_BUILD := 7
const BLD_HOUSE := 1
const SPEARMAN := "athen/infantry_spearman_b"
const WOMAN := "athen/support_female_citizen"

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
	ms.reveal_map = true # the foe stands in the fog otherwise, and a fogged unit can't be picked
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
	mm.position = pos
	mm.global_position = pos
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
	await frames(2)

func tap(code: Key) -> void:
	await hold(code, true)
	await hold(code, false)
	await frames(12) # past the double-press window

func click(pos: Vector2, button := MOUSE_BUTTON_LEFT) -> void:
	await move(pos)
	for down in [true, false]:
		var mb := InputEventMouseButton.new()
		mb.button_index = button
		mb.pressed = down
		mb.position = pos
		mb.global_position = pos
		mb.ctrl_pressed = Input.is_key_pressed(KEY_CTRL)
		mb.shift_pressed = Input.is_key_pressed(KEY_SHIFT)
		mb.alt_pressed = Input.is_key_pressed(KEY_ALT)
		root.push_input(mb, true)
		await frames(1)
	await frames(3)

func unit(id: int) -> Dictionary:
	for u in main.debug_selection_state()["units"]:
		if u["id"] == id:
			return u
	return {}

func alive(id: int) -> bool:
	return not unit(id).is_empty()

func selected() -> Array:
	return Array(main.debug_selection_state()["selected"])

func kind(id: int) -> int:
	return int(unit(id).get("order_kind", -1))

func pos(id: int) -> Vector2:
	return unit(id).get("pos", Vector2.ZERO)

# A screen point whose ground point lies on `id`'s feet: a right-click picks a unit within 2.2 m
# of its ground point (session_input.cpp near_pick), so the click has to land there.
func pick_spot(id: int) -> Vector2:
	var c: Vector2 = main.debug_unit_screen(id)
	var at: Vector2 = unit_xz(id)
	var best := c
	var best_d := INF
	for dy in range(-30, 31, 6):
		for dx in range(-30, 31, 6):
			var p := c + Vector2(dx, dy)
			var d: float = main.debug_ground_at(p).distance_to(at)
			if d < best_d:
				best_d = d
				best = p
	await move(best)
	return best

func unit_xz(id: int) -> Vector2:
	return main.debug_unit_pos(id)

# A screen point a little off a unit, so the click lands on open ground.
func ground_beside(id: int, dx: float, dy: float) -> Vector2:
	return main.debug_unit_screen(id) + Vector2(dx, dy)

func stance_button(stance: String) -> Button:
	for b in main.find_children("*", "Button", true, false):
		if String(b.name) == stance and b.has_signal("pressed"):
			return b
	return null

func lit_stances() -> Array:
	var out := []
	for s in ["Violent", "Aggressive", "Defensive", "Passive", "Standground"]:
		var b := stance_button(s)
		if b and b.button_pressed:
			out.append(s)
	return out

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

	var soldiers: PackedInt32Array = main.debug_spawn_units(SPEARMAN, 2)
	await turns(2)
	main.debug_select_units(soldiers)
	await frames(3)
	# Passive, so the soldiers don't fight the foe on their own while the walk runs.
	var passive := stance_button("Passive")
	if passive:
		await click(passive.get_global_rect().get_center())

	print("1. Right-click on open ground walks")
	var start := pos(soldiers[0])
	var goal := ground_beside(soldiers[0], 140.0, 0.0)
	await click(goal, MOUSE_BUTTON_RIGHT)
	await turns(2)
	check(kind(soldiers[0]) == ORDER_WALK, "the soldier walks (order %d)" % kind(soldiers[0]))
	await turns(60)
	check(pos(soldiers[0]).distance_to(start) > 1.0, "and moves: %.1f m" % pos(soldiers[0]).distance_to(start))

	print("2. Right-click on an enemy attacks it")
	# Spawned at the last moment: the Civic Centre's arrows kill a foe that stands near it.
	var foe: PackedInt32Array = main.debug_spawn_units(SPEARMAN, 1, 1, 0) # Player 1 is an enemy of Player 0
	main.debug_select_units(soldiers)
	await frames(3)
	var foe_at: Vector2 = await pick_spot(foe[0])
	await click(foe_at, MOUSE_BUTTON_RIGHT)
	await turns(2)
	check(kind(soldiers[0]) == ORDER_ATTACK or kind(soldiers[1]) == ORDER_ATTACK,
			"a soldier attacks the enemy (orders %d, %d)" % [kind(soldiers[0]), kind(soldiers[1])])

	print("3. Ctrl + right-click is attack-move")
	main.debug_select_units(soldiers)
	await frames(3)
	await hold(KEY_CTRL, true)
	await click(ground_beside(soldiers[0], -120.0, 40.0), MOUSE_BUTTON_RIGHT)
	await hold(KEY_CTRL, false)
	await turns(2)
	check(kind(soldiers[0]) == ORDER_WALK_AND_FIGHT, "attack-move walks and fights (order %d)" % kind(soldiers[0]))

	print("4. P + right-click patrols")
	main.debug_select_units(soldiers)
	await frames(3)
	await hold(KEY_P, true)
	await click(ground_beside(soldiers[0], 60.0, -120.0), MOUSE_BUTTON_RIGHT)
	await hold(KEY_P, false)
	await turns(2)
	check(kind(soldiers[0]) == ORDER_PATROL, "the soldier patrols (order %d)" % kind(soldiers[0]))

	print("5. Stances, clicked")
	main.debug_select_units(soldiers)
	await frames(3)
	var defensive := stance_button("Defensive")
	if defensive:
		await click(defensive.get_global_rect().get_center())
	await frames(10)
	check(lit_stances() == ["Defensive"], "Defensive alone is lit: %s" % str(lit_stances()))
	var stand := stance_button("Standground")
	if stand:
		await click(stand.get_global_rect().get_center())
	await frames(10)
	check(lit_stances() == ["Standground"], "then Standground alone: %s" % str(lit_stances()))

	print("6. Gather and repair by right-click")
	var women: PackedInt32Array = main.debug_spawn_units(WOMAN, 2)
	main.debug_gather_nearest() # every idle worker starts on the nearest supply
	await turns(4)
	var res: int = int(unit(women[0]).get("target_resource", -1))
	var res_at: Vector2 = main.debug_resource_screen(res) if res >= 0 else Vector2(-1, -1)
	var helper := women[1]
	main.debug_select_units(PackedInt32Array([helper]))
	await frames(3)
	if res_at.x > 0.0:
		await click(res_at, MOUSE_BUTTON_RIGHT)
		await turns(2)
		check(kind(helper) == ORDER_GATHER, "a right-click on a supply gathers it (order %d)" % kind(helper))
	else:
		check(false, "a supply to click on (women gather nothing)")
	var house := 0
	var house_at := Vector2(-1, -1)
	var vp: Vector2 = root.get_visible_rect().size
	for d in [Vector2(30, -20), Vector2(-30, -20), Vector2(40, -10), Vector2(-40, -10), Vector2(0, -30), Vector2(-30, -40)]:
		house = main.debug_spawn_building(BLD_HOUSE, d.x, d.y, 0, 0.4)
		await turns(1)
		house_at = main.debug_building_screen(house)
		if house_at.x > 40.0 and house_at.y > 40.0 and house_at.x < vp.x - 40.0 and house_at.y < vp.y - 220.0:
			break
	check(house_at.x > 40.0 and house_at.y > 40.0 and house_at.y < vp.y - 220.0, "a house foundation in view at %s" % str(house_at))
	main.debug_select_units(PackedInt32Array([women[0]]))
	await frames(3)
	await hold(KEY_J, true)
	for dy in [0.0, -14.0, -28.0, 14.0, -42.0]:
		await click(house_at + Vector2(0, dy), MOUSE_BUTTON_RIGHT)
		if kind(women[0]) == ORDER_BUILD:
			break
	await hold(KEY_J, false)
	await turns(2)
	check(kind(women[0]) == ORDER_BUILD, "J + right-click on a foundation repairs it (order %d)" % kind(women[0]))

	print("7. Rally point")
	main.debug_select_entity(cc)
	await frames(4)
	await click(cc_screen + Vector2(160.0, 60.0), MOUSE_BUTTON_RIGHT)
	await frames(6)
	var sel: Dictionary = main.debug_selection_state()
	check(sel.has("rally"), "a right-click on open ground sets the Civic Centre's rally point")
	main.clear_selection()

	print("8. Delete destroys the selected unit")
	main.debug_select_units(PackedInt32Array([soldiers[1]]))
	await frames(3)
	await tap(KEY_DELETE) # DeleteSelectionConfirmation: "Destroy everything currently selected?"
	await frames(10)
	check(alive(soldiers[1]), "Delete alone only asks")
	var yes: Button = null
	for b in main.find_children("*", "Button", true, false):
		if b.is_visible_in_tree() and b.text == "Yes":
			yes = b
	check(yes != null, "the question has a Yes button")
	if yes:
		await click(yes.get_global_rect().get_center())
	await turns(3)
	check(not alive(soldiers[1]), "Yes destroys the selected soldier")

	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
