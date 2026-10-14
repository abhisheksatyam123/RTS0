extends SceneTree
# unit_actions.js "garrison" getActionInfo: with units selected, hovering an own
# structure they can garrison in shows the garrison cursor and "Current garrison: x/y"
# beside it, in orange once the structure is full.
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
	check(t["cursor"] == "action-garrison", "the garrison cursor over the Civic Center")
	check(t["visible"] and str(t["text"]).begins_with("Current garrison: 0/"), "Current garrison: 0/capacity")
	check(t["color"] == Color(1, 1, 1), "white while there is room")
	var cap := int(str(t["text"]).get_slice("/", 1))
	var fill: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", cap)
	main.debug_put_inside(fill, cc)
	await frames(4)
	t = main.debug_cursor_tip()
	print("    ", t)
	check(str(t["text"]) == "Current garrison: %d/%d" % [cap, cap], "full: Current garrison: cap/cap")
	check(t["color"] == Color(1.0, 0.65, 0.0), "orange once full")
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
