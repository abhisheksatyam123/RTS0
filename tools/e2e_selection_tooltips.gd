extends SceneTree
# 0.28's Selection panel tooltips (g_SelectionPanels.Selection setupButton): a group
# icon's tooltip is the template's names and then what the group's units carry,
# summed by resource; a group carrying nothing has only its names.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_selection_tooltips.gd
# Exit code 0 = pass.

const RES_WOOD := 0
const RES_FOOD := 3

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
	var men: PackedInt32Array = main.debug_spawn_units("athen/infantry_spearman_b", 3)
	var archers: PackedInt32Array = main.debug_spawn_units("athen/infantry_archer_b", 1)
	main.debug_set_carry(men[0], RES_WOOD, 10)
	main.debug_set_carry(men[1], RES_WOOD, 7)
	main.debug_set_carry(men[2], RES_FOOD, 5)
	var all := men.duplicate()
	all.append_array(archers)
	main.debug_select_units(all)
	await frames(6)
	var tips: Array = main.debug_selection_grid(false)["icons"].map(func(e): return str(e["tip"]))
	print("    ", tips)
	check(tips.size() == 2, "two groups")
	if tips.size() == 2:
		# selection.js getKeys: groups in sorted "p<player>&<template>" order, so
		# units/athen/infantry_archer_b comes before units/athen/infantry_spearman_b.
		var archer_tip: String = tips[0]
		var spear_tip: String = tips[1]
		var lines: PackedStringArray = spear_tip.split("\n")
		check(lines.size() >= 2 and lines[1] == "Food 5  Wood 17", "spearmen carry Food 5  Wood 17")
		check(not spear_tip.contains("Health"), "no health line")
		check(not archer_tip.contains("Food") and not archer_tip.contains("Wood"), "archer carries nothing")
		check(archer_tip.ends_with("Click to select only these, Ctrl+click or right-click to deselect them."), "click help last")
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
