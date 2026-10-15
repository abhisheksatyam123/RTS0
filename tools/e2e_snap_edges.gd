extends SceneTree
# Snap to edges (0.28 input.js and ObstructionSnap.js): mines and ruins have edges to snap to
# like structures (GetEntitiesWithStaticObstructionOnScreen), and the hover resets the angle
# to the default before each snap (placementSupport.SetDefaultAngle).
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_snap_edges.gd
# Exit code 0 = pass.

const BLD_HOUSE := 1
const DEFAULT_ROT := PI # Simulation::DEFAULT_BUILDING_ROT

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

func run() -> void:
	await frames(60)
	# A house just east of where the mine will be, inside the snapping distance of its edge.
	var before: Dictionary = main.debug_snap_placement(BLD_HOUSE, 14.0, -12.0, true, 1.0)
	print("no mine: ", before)
	check(not bool(before["snapped"]), "nothing to snap to before the mine")
	check(absf(float(before["angle"]) - DEFAULT_ROT) < 1e-3, "the hover resets a stale angle to the default facing")
	var held: Dictionary = main.debug_snap_placement(BLD_HOUSE, 14.0, -12.0, false, 1.0)
	check(absf(float(held["angle"]) - 1.0) < 1e-3, "a drag keeps its own angle when nothing snaps")
	var mine: int = main.debug_spawn_mine(10.0, -12.0, 24)
	await frames(2)
	check(mine > 0, "a large stone mine is placed")
	var after: Dictionary = main.debug_snap_placement(BLD_HOUSE, 14.0, -12.0, true, 1.0)
	print("mine: ", after)
	check(bool(after["snapped"]), "a house snaps to the mine's edge")

	print("e2e_snap_edges: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(0 if failures == 0 else 1)
