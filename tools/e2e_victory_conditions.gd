extends SceneTree
# End-to-end check of Game Setup's victory conditions (0 A.D. 0.28
# gamesettings/attributes/VictoryConditions.js, gamesetup VictoryConditionCheckbox.js):
#   1. Conquest is checked by default and disables the partial conquests;
#   2. unchecking it frees them, and checking Conquest Units disables Conquest;
#   3. Start sends the combination as VICTORY_COMBINED | mask;
#   4. a scenario checks its own conditions and locks every box; a sandbox's
#      empty list is an endless game.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_victory_conditions.gd
# Exit code 0 = pass.

# Loaded at run time: the page script names autoloads, which -s scripts compile before.
var Setup

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

func box(page, bit: int) -> CheckBox:
	return page._victory_checks[bit]

func ids(page) -> Array:
	return page._filtered.map(func(m): return str(m["id"]))

func select_map(page, id: String) -> bool:
	var i: int = ids(page).find(id)
	if i < 0:
		return false
	page.get_node("VBox/HBox/LeftScroll/Left/MapList").select(i)
	page._on_map_list_selected(i)
	return true

func run() -> void:
	Setup = load("res://scenes/menu/gamesetup.gd")
	var cfg := RtsoUserConfig.new()
	var persist = cfg.get_value("persistmatchsettings")
	cfg.set_value("persistmatchsettings", "false")
	var ms = root.get_node("MatchSettings")
	ms.victory = 0
	ms.map_id = "acropolis_bay"
	change_scene_to_file("res://scenes/menu/gamesetup.tscn")
	await frames(6)
	var page = current_scene

	print("1. Default")
	check(page._victory_checks.size() == 7, "a box per 0.28 condition: %d" % page._victory_checks.size())
	check(box(page, 1).button_pressed and page._victory_mask() == 1, "only Conquest is checked")
	check(box(page, 2).disabled and box(page, 4).disabled and box(page, 8).disabled, "the partial conquests are disabled")
	check(not box(page, 16).disabled and not box(page, 32).disabled and not box(page, 64).disabled, "Wonder, Relic and Regicide are free")

	print("2. Exclusions")
	box(page, 1).button_pressed = false
	check(not box(page, 2).disabled and not box(page, 8).disabled, "unchecking Conquest frees the partial conquests")
	box(page, 2).button_pressed = true
	check(box(page, 1).disabled and not box(page, 4).disabled, "Conquest Units disables Conquest only")
	box(page, 8).button_pressed = true
	check(box(page, 4).disabled, "Conquest Civic Centers disables Conquest Structures")
	box(page, 16).button_pressed = true
	check(page._victory_mask() == 2 | 8 | 16, "Units, Civic Centers and Wonder: %d" % page._victory_mask())

	print("3. Start")
	page._on_start_pressed()
	check(ms.victory == Setup.VICTORY_COMBINED | 2 | 8 | 16, "the match gets the combined code: %d" % ms.victory)
	check(Setup.victory_title(ms.victory) == "Conquest Units, Conquest Civic Centers, Wonder", Setup.victory_title(ms.victory))
	check(Setup.victory_code(Setup.victory_mask(1)) == 1 and Setup.victory_title(1) == "Conquest, Wonder", "Wonder alone stays code 1")
	await frames(4)

	print("4. Scenarios")
	change_scene_to_file("res://scenes/menu/gamesetup.tscn")
	await frames(6)
	page = current_scene
	check(page._victory_mask() == 2 | 8 | 16, "the page shows the last match's conditions")
	page._on_map_type_selected(2)
	await frames(2)
	check(select_map(page, "laconia"), "Laconia")
	await frames(2)
	check(page._victory_mask() == 1, "its conditions: Conquest")
	check(page._victory_checks.values().all(func(b): return b.disabled), "every box is locked")
	var any_sandbox := ""
	for f in range(page._filter_names.size()):
		page._on_map_filter_selected(f)
		if ids(page).has("sandbox_athenians"):
			any_sandbox = "sandbox_athenians"
			break
	if not any_sandbox.is_empty():
		select_map(page, any_sandbox)
		await frames(2)
		check(page._victory_mask() == 0 and page._collect_setup()["victory"] == 3, "a sandbox is an endless game")
	page._on_map_type_selected(0)
	await frames(2)
	check(not box(page, 16).disabled and not box(page, 64).disabled, "a skirmish frees the boxes again")

	cfg.set_value("persistmatchsettings", persist)
	print("RESULT: %s (%d failed)" % ["PASS" if failures == 0 else "FAIL", failures])
	quit(1 if failures else 0)
