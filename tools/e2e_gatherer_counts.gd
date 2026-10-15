extends SceneTree
# 0.28's top panel gatherer counts (CounterResource / CounterPopulation): a unit
# counts from the gather order on, while it walks to the resource, gathers and
# carries it back (UnitAI GATHER), so the total doesn't dip on each trip.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_gatherer_counts.gd
# Exit code 0 = pass.

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
	ms.slot_types = [0, 1]
	ms.slot_civs = [0, 1]
	ms.replay_dir = ""
	ms.load_save_path = ""
	change_scene_to_file("res://scenes/main.tscn")
	run.call_deferred()

func run() -> void:
	for i in 30:
		await process_frame
	var m = current_scene
	m.find_children("*", "RtsoCameraRig", true, false)[0].set_edge_scroll_enabled(false)
	var total: Label = m.find_child("PopGatherers", true, false)
	check(total != null, "the population counter shows the gatherers")
	m.debug_gather_nearest()
	for i in 40:
		await process_frame
	var lo := 1 << 30
	var hi := 0
	for i in 900: # 45 s: several trips to the dropsite
		await process_frame
		var n := int(total.text)
		lo = mini(lo, n)
		hi = maxi(hi, n)
	check(hi > 0, "the ordered units count as gatherers (%d)" % hi)
	check(lo == hi, "the count holds while they walk back to drop off: %d..%d" % [lo, hi])
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
