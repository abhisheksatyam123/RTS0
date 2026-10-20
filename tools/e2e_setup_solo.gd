extends SceneTree
# 0.28 single-player Game Setup starts with any 1-8 players, AI or not
# (PlayerCount 1-8; GameSetupPage's Start is not held back for a missing AI):
#   1. the player count goes down to 1 and Start launches a game with you alone;
#   2. the match loads and does not end on its own, there being no enemy to beat.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_setup_solo.gd
# Exit code 0 = pass.

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
	var ms: Node = root.get_node("/root/MatchSettings")
	# Not persisted, so the 1-player setup doesn't carry into the next Game Setup test.
	var cfg := RtsoUserConfig.new()
	var saved_persist: String = cfg.get_value("persistmatchsettings")
	cfg.set_value("persistmatchsettings", "false")
	print("1. One player")
	change_scene_to_file("res://scenes/menu/gamesetup.tscn")
	await frames(12)
	var page = current_scene
	var spin: SpinBox = page.get_node("VBox/HBox/Right/SlotsBox/SlotCountRow/SpinSlots")
	check(spin.min_value == 1.0 and page.get_node("%PlayerCountSlot").get_child(0).get_item_text(0) == "1", "the count goes down to 1")
	page._select_map_id("random_mainland")
	page.refresh_map_list()
	spin.value = 1
	page.refresh_slots()
	await frames(2)
	page._on_start_pressed()
	check(ms.player_count == 1 and ms.slot_types == [0], "Start: you alone (%d, %s)" % [ms.player_count, ms.slot_types])

	print("2. The match")
	var t0 := Time.get_ticks_msec()
	while Time.get_ticks_msec() - t0 < 120000:
		await process_frame
		if current_scene and current_scene.has_method("get_match_summary") and not current_scene.get_match_summary().is_empty():
			break
	check(current_scene != null and current_scene.has_method("get_match_summary"), "the match loads")
	await frames(200)
	var s: Dictionary = current_scene.get_match_summary() if current_scene and current_scene.has_method("get_match_summary") else {}
	check(not s.is_empty() and not bool(s.get("ended", true)), "and keeps going: %s" % str(s.get("ended", "?")))
	cfg.set_value("persistmatchsettings", saved_persist)
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
