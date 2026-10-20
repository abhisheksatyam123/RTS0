extends SceneTree
# unit_actions.js "capture" (specificness 10): a right-click on one's own structure that an enemy
# holds capture points of (Capturable.CanCapture) shows the capture cursor and orders a capture,
# before "repair" (11) and "move" (12); one with no enemy points is a walk.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_recapture.gd
# Exit code 0 = pass.

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

func click(pos: Vector2, button := MOUSE_BUTTON_LEFT) -> void:
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

func settle() -> void:
	var r: Dictionary = main.debug_civic_centre_roof()
	for i in 200:
		await frames(1)
		var r2: Dictionary = main.debug_civic_centre_roof()
		var still: bool = (r2["roof"] as Vector2).distance_to(r["roof"]) < 0.5
		r = r2
		if still and i > 10:
			break

func cc_screen(cc: int) -> Vector2:
	main.debug_select_entity(cc)
	await frames(2)
	var at: Vector2 = main.debug_selected_building()["screen"]
	main.clear_selection()
	return at

func run() -> void:
	await frames(60)
	await move(Vector2(512, 400))
	await settle()
	var cc: int = main.debug_civic_centre_id(0)
	var men: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 2)
	await turns(2)

	main.debug_select_units(men)
	var at: Vector2 = await cc_screen(cc)
	main.debug_select_units(men)
	await move(at)
	check(str(main.debug_cursor_tip()["cursor"]) != "action-capture", "no capture cursor on our own, fully held Civic Center")
	await click(at, MOUSE_BUTTON_RIGHT)
	await turns(2)
	var o: Dictionary = main.debug_unit_order(men[0])
	check(not (int(o["target"]) == cc and o["capture"]), "a right-click there orders no capture")

	main.debug_set_capture_points(cc, 1, 300.0)
	var before: PackedFloat32Array = main.debug_capture_points(cc)
	print("    points ", before)
	check(before[1] > 0.0, "the enemy holds some of its points")
	at = await cc_screen(cc)
	main.debug_select_units(men)
	await move(at)
	var cur: String = str(main.debug_cursor_tip()["cursor"])
	print("    cursor ", cur)
	check(cur == "action-capture", "the capture cursor over it")
	await click(at, MOUSE_BUTTON_RIGHT)
	await turns(2)
	o = main.debug_unit_order(men[0])
	print("    order ", o)
	check(int(o["target"]) == cc and o["capture"], "a right-click orders the recapture")
	await turns(40)
	var after: PackedFloat32Array = main.debug_capture_points(cc)
	print("    points ", after)
	check(after[1] < before[1], "the enemy's points go down")

	print("FAILURES: ", failures)
	quit(1 if failures else 0)
