extends SceneTree
# input.js INPUT_BUILDING_PLACEMENT mousemotion: placing a structure the player can
# no longer afford stops on the next mouse move; while it is affordable a move keeps it.
# selection.js g_MaxSelectionSize: a selection holds at most 300 entities.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_selection_limits.gd
# Exit code 0 = pass.

const BLD_HOUSE := 1
const RES_WOOD := 0

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
	main.enter_build_mode(true, BLD_HOUSE)
	await move(Vector2(500, 400))
	check(main.debug_placing(), "a move keeps an affordable placement")
	main.debug_set_resource(RES_WOOD, 0)
	await frames(2)
	check(main.debug_placing(), "still placing until the mouse moves")
	await move(Vector2(520, 410))
	check(not main.debug_placing(), "the next move stops it")
	var many: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 320)
	main.debug_select_units(many)
	await frames(2)
	check(main.debug_selection_grid(false)["selected"] == 300, "320 units selected keep 300")
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
