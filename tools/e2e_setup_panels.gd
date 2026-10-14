extends SceneTree
# End-to-end check of Game Setup's 0.28 panels (gui/gamesetup/Pages/GameSetupPage):
#   1. GameDescription.js: the settings as getGameDescription words them,
#      following a change (Regicide) without anything else being touched;
#   2. ResetCivsButton.js / ResetTeamsButton.js: every civ back to Random and
#      every team back to None; neither shows on a scenario.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_setup_panels.gd
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
	change_scene_to_file("res://scenes/menu/gamesetup.tscn")
	await frames(12)
	var page = current_scene
	var desc: RichTextLabel = page.get_node("VBox/HBox/Right/Preview/Info/GameDescription")
	var name := str(page._selected_map()["name"])
	check(desc.text.contains("[color=orange]Map Name[/color] " + name), "the description names the map (%s)" % name)
	check(desc.text.contains("[color=orange]Conquest[/color]") and not desc.text.contains("Regicide"), "Conquest, no Regicide")
	page._set_victory_mask(1 | 64)
	await frames(10)
	check(desc.text.contains("[color=orange]Regicide[/color]"), "checking Regicide shows in the description")

	print("2. Reset buttons")
	page._slot_rows[0]["civ"] = int(page.CIVS[0]["id"])
	page._slot_rows[1]["civ"] = int(page.CIVS[1]["id"])
	page._slot_rows[0]["team"] = 1
	page._slot_rows[1]["team"] = 2
	page.refresh_slots()
	await frames(2)
	var civs: Button = page.get_node("VBox/HBox/Right/SlotsBox/SlotCountRow/ResetCivs")
	var teams: Button = page.get_node("VBox/HBox/Right/SlotsBox/SlotCountRow/ResetTeams")
	check(civs.visible and teams.visible and civs.tooltip_text == "Reset any civilizations that have been selected to the default (random).", "both buttons on a random map")
	civs.pressed.emit()
	await frames(2)
	check(page._slot_rows.slice(0, 2).all(func(r): return int(r["civ"]) == page.RANDOM_CIV), "Reset Civs: every civ Random")
	check(int(page._slot_rows[0]["team"]) == 1 and int(page._slot_rows[1]["team"]) == 2, "and the teams kept")
	teams.pressed.emit()
	await frames(2)
	check(page._slot_rows.slice(0, 2).all(func(r): return int(r["team"]) == 0), "Reset Teams: every team None")
	page._select_map_id("acropolis_bay")
	page.refresh_map_list()
	await frames(2)
	if page._is_scenario():
		check(not civs.visible and not teams.visible, "a scenario hides them")
	else:
		page._on_map_type_selected(2)
		await frames(2)
		check(not page._is_scenario() or (not civs.visible and not teams.visible), "a scenario hides them")
	print("PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
