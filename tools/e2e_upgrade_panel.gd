extends SceneTree
# 0.28's Upgrade panel (selection_panels.js g_SelectionPanels.Upgrade):
#   1. only for a selection of one template: Persian Immortals show the bow switch as
#      an icon button, "Upgrade to a <specific> (<generic>).", with their count;
#      add a unit of another template and it goes;
#   2. clicked, the button becomes "Cancel Upgrading" with its progress overlay
#      shrinking, and a click on it cancels;
#   3. the cost is the whole selection's, and a selection the stock can't pay for is
#      disabled with the resources it lacks (Roman Centurions, 100 food 50 metal each).
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_upgrade_panel.gd
# Exit code 0 = pass.

const TT = preload("res://scenes/common/tooltips.gd")

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	root.get_node("MatchSettings").cheats = true
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

# The developer overlay row by its label (rows get added, so no fixed index).
func promote() -> void:
	main._on_dev_command(Array(main.debug_dev_overlay()["commands"]).find("Promote selected units"))

func upgrades() -> Array:
	return main.debug_command_row().filter(func(e): return TT.plain(str(e["tip"])).begins_with("Upgrade to a") or e["tip"] == "Cancel Upgrading" or TT.plain(str(e["tip"])).begins_with("Cannot upgrade"))

func run() -> void:
	await frames(60)
	print("1. Persian Immortals")
	var imm: PackedInt32Array = main.debug_spawn_units("pers/champion_infantry", 3)
	main.debug_select_units(imm)
	await frames(4)
	var u := upgrades()
	print("    ", u.map(func(e): return "%s | %s | x%d" % [e["text"], TT.plain(str(e["tip"])).replace("\n", " | "), e["count"]]))
	check(u.size() == 1 and TT.plain(str(u[0]["tip"])).begins_with("Upgrade to a Anušiya (Persian Immortal).") and u[0]["count"] == 3 and not u[0]["disabled"], "one button, names, three of them")
	var mixed := imm.duplicate()
	mixed.append_array(main.debug_spawn_units("athen/infantry_spearman_b", 1))
	main.debug_select_units(mixed)
	await frames(4)
	check(upgrades().is_empty(), "not with a unit of another template")

	print("2. Upgrading")
	main.debug_select_units(imm)
	await frames(2)
	u = upgrades()
	if u.size() == 1:
		(instance_from_id(u[0]["id"]) as Button).pressed.emit()
	await frames(30)
	u = upgrades()
	check(u.size() == 1 and u[0]["tip"] == "Cancel Upgrading" and u[0]["count"] == 3, "Cancel Upgrading, three of them")
	var p0: int = u[0]["progress"] if u.size() == 1 else -1
	await frames(20)
	u = upgrades()
	var p1: int = u[0]["progress"] if u.size() == 1 else -1
	check(p0 > 0 and p1 >= 0 and p1 < p0, "the progress overlay shrinks: %d then %d px" % [p0, p1])
	if u.size() == 1:
		(instance_from_id(u[0]["id"]) as Button).pressed.emit()
	await frames(30)
	u = upgrades()
	check(u.size() == 1 and TT.plain(str(u[0]["tip"])).begins_with("Upgrade to a"), "a click cancels")

	print("3. Roman Centurions")
	var rom: PackedInt32Array = main.debug_spawn_units("rome/infantry_spearman_b", 2)
	main.debug_select_units(rom)
	await frames(2)
	for i in 2:
		promote()
		await frames(30)
	u = upgrades()
	print("    ", u.map(func(e): return "%s | %s" % [e["text"], TT.plain(str(e["tip"])).replace("\n", " | ")]))
	check(u.size() == 1 and TT.plain(str(u[0]["tip"])).contains("200") and TT.plain(str(u[0]["tip"])).contains("100"), "two of them: the cost doubles")
	var more: PackedInt32Array = main.debug_spawn_units("rome/infantry_spearman_b", 8)
	main.debug_select_units(more)
	for i in 2:
		promote()
		await frames(30)
	more.append_array(rom)
	main.debug_select_units(more)
	await frames(4)
	u = upgrades()
	print("    ", u.map(func(e): return "%s | %s" % [e["text"], TT.plain(str(e["tip"])).replace("\n", " | ")]))
	check(u.size() == 1 and u[0]["disabled"] and TT.plain(str(u[0]["tip"])).contains("Insufficient resources:"), "ten of them: disabled, with what the stock lacks")
	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
