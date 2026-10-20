extends SceneTree
# 0.28 PlayerAssignment in single player (gui/gamesetup/.../PerPlayer/Dropdowns/PlayerAssignment.js):
#   1. each slot offers you, "AI: Petra Bot", "Unassigned" and "Removed" (not slot 1);
#   2. picking yourself in an AI's slot moves you there, hands the AI to your old slot and
#      swaps civ and colour; Unassigned on your slot makes you watch;
#   3. the match seats you in the slot you picked: your units, your resign.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_setup_assign.gd
# Exit code 0 = pass.

var failures := 0
var cfg := RtsoUserConfig.new()
var saved_persist := ""

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func frames(n: int) -> void:
	for i in n:
		await process_frame

func _initialize() -> void:
	run.call_deferred()

func kind_select(page, row: int) -> RtsoSafeSelect:
	var cells: Array = page._slots_grid().get_children().filter(func(c): return not c.is_queued_for_deletion())
	return cells[row * page._slots_grid().columns + 2].get_child(0)

func items(sel: RtsoSafeSelect) -> Array:
	return range(sel.get_item_count()).map(func(i): return sel.get_item_text(i))

func run() -> void:
	saved_persist = cfg.get_value("persistmatchsettings")
	cfg.set_value("persistmatchsettings", "false")
	var ms = root.get_node("MatchSettings")
	ms.map_id = "random_continental"
	ms.slot_types = []
	ms.local_slot = -2
	change_scene_to_file("res://scenes/menu/gamesetup.tscn")
	await frames(8)
	var page = current_scene

	print("1. Player assignment entries")
	page._slot_count_node().value = 3
	await frames(3)
	for i in 3:
		page._slot_rows[i]["kind"] = 0 if i == 0 else 1
		page._slot_rows[i]["civ"] = i
		page._slot_rows[i]["color"] = i
	page.refresh_slots()
	await frames(2)
	var you: String = page.singleplayer_name()
	check(items(kind_select(page, 1)) == [you, "AI: Petra Bot", "Unassigned", "Removed"], "a slot offers you, AI, Unassigned and Removed: " + str(items(kind_select(page, 1))))
	check(items(kind_select(page, 0)) == [you, "AI: Petra Bot", "Unassigned"] and kind_select(page, 0).text == you, "slot 1 has no Removed, and is you")
	check(kind_select(page, 1).tooltip_text == "Select player.", "the 0.28 tooltip")

	print("2. Moving yourself")
	var ks := kind_select(page, 2)
	ks.select(0)
	ks.item_selected.emit(0)
	await frames(3)
	check(page._local_slot == 2 and int(page._slot_rows[2]["kind"]) == 0 and int(page._slot_rows[0]["kind"]) == 1,
		"you sit in slot 3 and its AI takes slot 1")
	check(int(page._slot_rows[0]["civ"]) == 2 and int(page._slot_rows[2]["civ"]) == 0 and int(page._slot_rows[0]["color"]) == 2 and int(page._slot_rows[2]["color"]) == 0,
		"civ and colour swap with it")
	var st: Dictionary = page._collect_setup()
	check(int(st["local_slot"]) == 2 and st["names"][2] == you, "the setup seats you in slot 3, with your name")
	check(page._slot_label(2, page._slot_rows[2]) == "P3 (You)" and page._slot_label(0, page._slot_rows[0]) == "P1", "P3 (You)")
	ks = kind_select(page, 2)
	ks.select(2)
	ks.item_selected.emit(2)
	await frames(3)
	st = page._collect_setup()
	check(page._local_slot == -1 and int(st["local_slot"]) == -1, "Unassigned on your own slot: you watch")
	ks = kind_select(page, 1)
	ks.select(0)
	ks.item_selected.emit(0)
	await frames(3)
	check(page._local_slot == 1 and int(page._slot_rows[1]["kind"]) == 0 and int(page._slot_rows[2]["kind"]) == 0, "picking yourself again from watching takes slot 2 and swaps nothing")
	cfg.set_value("persistmatchsettings", saved_persist)

	print("3. The match seats you there")
	ms.map_id = "random_continental"
	ms.seed = 7
	ms.slot_types = [1, 0]
	ms.slot_civs = [0, 1]
	ms.slot_teams = [0, 0]
	ms.local_slot = 1
	ms.replay_dir = ""
	ms.load_save_path = ""
	change_scene_to_file("res://scenes/main.tscn")
	await frames(60)
	var m = current_scene
	check(int(m.get_summary_gui(false)["assignedPlayer"]) == 1, "you play player 2: " + str(m.get_summary_gui(false)["assignedPlayer"]))
	check(int(m.get_match_statistics()["local_player"]) == 1, "the statistics follow player 2")
	var mine: Array = m.debug_selection_state().get("units", [])
	check(not mine.is_empty(), "player 2's units are yours (%d)" % mine.size())
	m.resign()
	await frames(40)
	check(bool(m.get_summary_gui(false)["isObserver"]), "resigning defeats player 2, the seat you played")
	ms.local_slot = -2
	print("FAILURES: ", failures)
	quit(1 if failures else 0)
