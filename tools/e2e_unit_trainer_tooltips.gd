extends SceneTree
# 0.28's Training and Research panels on a unit that trains (Trainer and Researcher on
# the Athenian arrow ship): a unit whose requirements fail says what it waits on
# (getRequirementsTooltip "Requires <tech>"), and a technology's tooltip is the Research
# panel's: names, tooltip, cost, right-click line, its requirementsTooltip and
# getNeededResourcesTooltip; no "Requirements not met".
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_unit_trainer_tooltips.gd
# Exit code 0 = pass.

const CIV_ATHENIANS := 0

const TT = preload("res://scenes/common/tooltips.gd")

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	var ms = root.get_node("MatchSettings")
	ms.player_civ = CIV_ATHENIANS
	ms.slot_civs = [CIV_ATHENIANS, 1]
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func run() -> void:
	await frames(60)
	var ship: PackedInt32Array = main.debug_spawn_units("athen/ship_arrow", 1)
	check(ship.size() == 1, "an Athenian arrow ship")
	if ship.size() == 1:
		main.debug_select_entity(ship[0])
	await frames(10)
	var tips: Array = []
	for l in main.find_children("*", "ItemList", true, false):
		if l.visible and l.item_count > 0:
			for i in l.item_count:
				tips.append(TT.plain(str(l.get_item_tooltip(i))))
	for t in tips:
		print("    ", t.replace("\n", " | "))
	check(tips.size() >= 3, "two units and a technology")
	check(tips.all(func(t): return not t.contains("Requirements not met")), "no \"Requirements not met\"")
	var marines: Array = tips.filter(func(t): return t.contains("(Athenian Marine)"))
	check(marines.size() == 1 and marines[0].contains("\nRequires Iphicratean Reforms"), "the marine waits on Iphicratean Reforms")
	var tech: Array = tips.filter(func(t): return t.begins_with("Iphicratean Reforms"))
	check(tech.size() == 1 and tech[0].contains("\nCost: ") and tech[0].contains("Right-click for more information.")
			and tech[0].contains("\nUnlocked in Town Phase."), "the tech: cost, right-click line, \"Unlocked in Town Phase.\"")
	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
