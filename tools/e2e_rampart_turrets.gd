extends SceneTree
# 0.28's "occupy-turret" on the Mauryan rampart tower (maur/tower_double), which has a
# garrison and 16 turret points:
#   1. selected archers show g_EntityCommands "occupy-turret" (Occupy Turret);
#   2. the button arms ACTION_OCCUPY_TURRET: the cursor over the tower is
#      action-occupy-turret, and the next left click (INPUT_PRESELECTEDACTION) sends the archers onto its points, not inside;
#   3. the tower then shows "unload-all-turrets" (Unload Turrets) with their count, and a
#      click brings them down.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_rampart_turrets.gd
# Exit code 0 = pass.

const BLD_TOWER_DOUBLE := 81

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

func named(label: String) -> Array:
	return main.debug_command_row().filter(func(e): return str(e["text"]).begins_with(label))

func press(e: Dictionary) -> void:
	(instance_from_id(e["id"]) as Button).pressed.emit()
	await frames(4)

func run() -> void:
	await frames(60)
	await move(Vector2(512, 400))
	# The camera settles on the Civic Center first.
	var r: Dictionary = main.debug_civic_centre_roof()
	for i in 200:
		await frames(1)
		var r2: Dictionary = main.debug_civic_centre_roof()
		var still: bool = (r2["roof"] as Vector2).distance_to(r["roof"]) < 0.5
		r = r2
		if still and i > 10:
			break
	# Four archers where debug_spawn_units puts the first (debug_spawn_offset, 16 m off the Civic Center's corner on dry land),
	# and the tower right beside them, within its LoadingRange (a debug-spawned structure
	# further off is not reliably reachable in this test map).
	var archers := PackedInt32Array()
	for k in 4:
		archers.append_array(main.debug_spawn_units("maur/infantry_archer_b", 1))
	var spot: Vector2 = main.debug_spawn_offset("maur/infantry_archer_b", 1)
	var tower: int = main.debug_spawn_building(BLD_TOWER_DOUBLE, spot.x + 1.7, spot.y)
	main.debug_select_entity(tower)
	await frames(4)
	var at: Vector2 = main.debug_selected_building()["screen"]
	main.debug_select_units(archers)
	await frames(4)
	await move(at)
	check(tower != 0 and main.debug_cursor_tip()["cursor"] == "action-garrison", "a rampart tower beside the archers: " + str(main.debug_cursor_tip()["cursor"]))

	print("1. Occupy Turret")
	var occupy := named("Occupy Turret")
	check(occupy.size() == 1 and str(occupy[0]["tip"]).contains("Order the selected units to occupy a turret point."), "selected archers: Occupy Turret")

	print("2. Armed, a right-click on the tower")
	if occupy.size() == 1:
		await press(occupy[0])
	await move(at)
	var t: Dictionary = main.debug_cursor_tip()
	check(t["cursor"] == "action-occupy-turret", "the occupy-turret cursor: " + str(t["cursor"]))
	for down in [true, false]:
		var mb := InputEventMouseButton.new()
		mb.button_index = MOUSE_BUTTON_LEFT
		mb.pressed = down
		mb.position = at
		mb.global_position = at
		root.push_input(mb, true)
		await frames(1)
	main.debug_select_entity(tower)
	var b: Dictionary = {}
	for i in 400:
		await frames(1)
		b = main.debug_selected_building()
		if int(b.get("turret_units", 0)) == 4:
			break
	# The walk itself is test_turrets'; here the four crowd one spot, and one may be pushed
	# off out of reach. Whoever gets there goes onto a point, none inside.
	var up := int(b.get("turret_units", 0))
	check(up >= 3 and int(b.get("garrisoned", -1)) == 0, "on its turret points, not inside: " + str(b))

	print("3. Unload Turrets")
	var unload := named("Unload Turrets")
	check(unload.size() == 1 and str(unload[0]["text"]) == "Unload Turrets (%d)" % up, "the tower: Unload Turrets (%d)" % up)
	if unload.size() == 1:
		await press(unload[0])
	await frames(10)
	check(int(main.debug_selected_building().get("turret_units", -1)) == 0 and named("Unload Turrets").is_empty(), "a click brings them down")
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
