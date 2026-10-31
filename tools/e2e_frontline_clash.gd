extends SceneTree

var main: Node
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func run() -> void:
	print("=====================================================")
	print("  TEST: FRONTLINE CLASH PRESET & SCENARIO INITIALIZATION")
	print("=====================================================")

	var ms = root.get_node_or_null("MatchSettings")
	check(ms != null, "MatchSettings autoload exists")
	if not ms:
		quit(1)
		return

	check(ms.has_method("configure_frontline_clash"), "MatchSettings has configure_frontline_clash()")
	ms.configure_frontline_clash(500)
	check(ms.map_id == "frontline_clash", "map_id is frontline_clash")
	check(ms.battle_preset == "frontline_clash", "battle_preset is frontline_clash")
	check(ms.battle_units == 500, "battle_units configured to 500")

	print("1. Instantiating Match Scene...")
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)

	await frames(10)

	var u_count: int = main.get_unit_count() if main.has_method("get_unit_count") else 0
	check(u_count >= 400, "Frontline clash units spawned: %d units" % u_count)

	var cam_data: Dictionary = main.debug_camera() if main.has_method("debug_camera") else {}
	var cam_target: Vector2 = cam_data.get("target", Vector2(999, 999))
	check(cam_target.length() < 1.0, "Camera target is centered at origin: %s" % str(cam_target))

	var sel = main.selected_ids if "selected_ids" in main else (main.get_selected_ids() if main.has_method("get_selected_ids") else [])
	check(sel.size() > 0, "Player vanguard is initially selected (%d units)" % sel.size())

	print("2. Advancing simulation frames...")
	await frames(8)

	var u_count_after: int = main.get_unit_count() if main.has_method("get_unit_count") else 0
	check(u_count_after > 0, "Combat simulation is actively running: %d units remaining" % u_count_after)

	print("-----------------------------------------------------")
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	print("=====================================================")
	quit(1 if failures else 0)
