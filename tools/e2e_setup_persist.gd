extends SceneTree
# End-to-end check of Game Setup's persisted settings and single-player rules
# (0 A.D. 0.28 gamesetup Persistence/PersistentMatchSettings.js, Cheats.js,
# common/functions_utility.js singleplayerName):
#   1. cheats are on and locked in single-player;
#   2. Back saves the page as left (a Random civ and a Closed slot included) and
#      the next visit shows it again;
#   3. Start saves too, and names you after Options' single-player name;
#   4. with "Persist match settings" off, Back saves nothing to restore.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_setup_persist.gd
# Exit code 0 = pass.

# Loaded at run time: the page script names autoloads, which -s scripts compile before.
var Setup

var failures := 0
var cfg := RtsoUserConfig.new()
var saved_cfg := {}

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func open_setup():
	change_scene_to_file("res://scenes/menu/gamesetup.tscn")
	await frames(6)
	return current_scene

func run() -> void:
	Setup = load("res://scenes/menu/gamesetup.gd")
	for k in ["playername.singleplayer", "persistmatchsettings"]:
		saved_cfg[k] = cfg.get_value(k)
	cfg.set_value("playername.singleplayer", "Tester")
	cfg.set_value("persistmatchsettings", "true")
	DirAccess.remove_absolute(ProjectSettings.globalize_path(Setup.PERSIST_FILE))
	var ms = root.get_node("MatchSettings")

	print("1. Cheats")
	var page = await open_setup()
	var cheats: CheckBox = page.get_node("VBox/HBox/LeftScroll/Left/MapVisRow/Cheats")
	check(cheats.button_pressed and cheats.disabled, "cheats are on and can't be turned off")

	print("2. Back keeps the page")
	page.get_node("VBox/HBox/Right/SlotsBox/SlotCountRow/SpinSlots").value = 3
	await frames(2)
	page._on_slot_civ(Setup.CIVS.size(), 1)  # Random
	page._on_slot_kind(2, 2)                 # Closed
	var pop: RtsoSafeSelect = page.get_node("VBox/HBox/LeftScroll/Left/PopCapSlot").get_child(0)
	pop.select(2)
	page._on_back_pressed()
	await frames(6)
	check(FileAccess.file_exists(Setup.PERSIST_FILE), "Back writes " + Setup.PERSIST_FILE)
	page = await open_setup()
	var rows: Array = page._slot_rows
	check(int(page.get_node("VBox/HBox/Right/SlotsBox/SlotCountRow/SpinSlots").value) == 3, "three slots again")
	check(rows.size() >= 3 and int(rows[1]["civ"]) == Setup.CIVS.size(), "slot 2 is still Random: %s" % (rows[1]["civ"] if rows.size() > 1 else "-"))
	check(rows.size() >= 3 and int(rows[2]["kind"]) == 2, "slot 3 is still Closed")
	pop = page.get_node("VBox/HBox/LeftScroll/Left/PopCapSlot").get_child(0)
	check(pop.get_selected() == 2, "the population limit is kept: %d" % pop.get_selected())

	print("3. Start")
	page.get_node("VBox/HBox/Right/SlotsBox/SlotCountRow/SpinSlots").value = 2  # the scenario's two players
	await frames(2)
	page._on_start_pressed()
	check(ms.cheats, "the match has cheats on")
	check(ms.slot_names.size() > 0 and ms.slot_names[0] == "Tester", "you are named after Options: " + str(ms.slot_names))
	check(ms.slot_civs.size() == 2 and int(ms.slot_civs[1]) < Setup.CIVS.size(), "the Random civ is picked for the match: " + str(ms.slot_civs))
	var kept: Dictionary = Setup.load_persisted()
	check(kept.get("slot_civs", []).size() == 2 and int(kept["slot_civs"][1]) == Setup.CIVS.size(), "the file keeps Random: " + str(kept.get("slot_civs")))
	await frames(2)

	print("4. Persist match settings off")
	cfg.set_value("persistmatchsettings", "false")
	page = await open_setup()
	page._on_back_pressed()
	await frames(4)
	var f := FileAccess.open(Setup.PERSIST_FILE, FileAccess.READ)
	var data = JSON.parse_string(f.get_as_text()) if f else null
	check(data is Dictionary and data.get("attributes", null) is Dictionary and data["attributes"].is_empty(), "Back saves no settings")
	check(Setup.load_persisted().is_empty(), "and nothing is restored")

	for k in saved_cfg:
		cfg.set_value(k, saved_cfg[k])
	DirAccess.remove_absolute(ProjectSettings.globalize_path(Setup.PERSIST_FILE))
	print("RESULT: %s (%d failed)" % ["PASS" if failures == 0 else "FAIL", failures])
	quit(1 if failures else 0)
