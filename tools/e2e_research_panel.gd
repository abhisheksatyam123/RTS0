extends SceneTree
# 0.28's Research panel (selection_panels.js g_SelectionPanels.Research,
# selection_panels_right/research_panel.xml):
#   1. each technology is an icon button (its 0.28 icon) with names, cost and the
#      right-click line in its tooltip;
#   2. a pair (pair_*.json, the Temple of Isis's Pharaonic and Serapis Cults)
#      stands together with the pair connector, top first, and hovering one
#      crosses out the other;
#   3. a technology the stock can't pay for is disabled under a red mask with the
#      missing resources; one whose requirements fail is disabled and grey.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_research_panel.gd
# Exit code 0 = pass.

const BLD_DOCK := 11
const BLD_TEMPLE_ISIS := 85
const CIV_PTOLEMIES := 11

const TT = preload("res://scenes/common/tooltips.gd")

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	var ms = root.get_node("MatchSettings")
	ms.player_civ = CIV_PTOLEMIES
	ms.slot_civs = [CIV_PTOLEMIES, 1]
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func run() -> void:
	await frames(60)
	print("1-2. The Temple of Isis' pair")
	main.debug_train_buttons(BLD_TEMPLE_ISIS)
	await frames(2)
	var row: Array = main.debug_command_row()
	var pair: Array = row.filter(func(e): return e["paired"])
	print("    ", row.map(func(e): return "%s%s" % [e["text"], " (paired)" if e["paired"] else ""]))
	check(pair.size() == 2 and pair[0]["text"] == "Pharaonic Cult" and pair[1]["text"] == "Serapis Cult", "Pharaonic Cult above Serapis Cult")
	if pair.size() == 2:
		var top: Button = instance_from_id(pair[0]["id"])
		var bottom: Button = instance_from_id(pair[1]["id"])
		var box: Node = top.get_parent()
		check(box.name.begins_with("unitResearchPair") and box.find_child("unitResearchPairIcon", false, false) != null, "joined by the pair connector")
		check(top.text == "" and (top.find_child("unitResearchIcon", false, false) as TextureRect).texture != null, "an icon, no text")
		check(TT.plain(str(pair[0]["tip"])).contains("Right-click for more information.") and TT.plain(str(pair[0]["tip"])).contains("Cost: "), "names, cost, right-click line")
		var cross: Control = bottom.find_child("unitResearchUnchosenIcon", false, false)
		top.mouse_entered.emit()
		check(cross != null and cross.visible, "hovering one crosses out the other")
		top.mouse_exited.emit()
		check(cross != null and not cross.visible, "and not after")
		check(pair[0]["disabled"] and pair[1]["disabled"] and (top.find_child("unitResearchIcon", false, false) as TextureRect).material != null
			and (bottom.find_child("unitResearchIcon", false, false) as TextureRect).material != null, "City Phase not reached: both disabled and grey")

	print("3. The dock")
	main.debug_train_buttons(BLD_DOCK)
	await frames(2)
	row = main.debug_command_row()
	var poor: Array = row.filter(func(e): return TT.plain(str(e["tip"])).contains("\nInsufficient resources: "))
	print("    ", row.map(func(e): return "%s %s" % [e["text"], "disabled" if e["disabled"] else ""]))
	# Tribute wood away (100 at a time) until one is out of reach.
	for i in 4:
		if not poor.is_empty():
			break
		main._on_diplo_tribute_pressed(1, 0)
		await frames(10)
		row = main.debug_command_row()
		poor = row.filter(func(e): return TT.plain(str(e["tip"])).contains("\nInsufficient resources: "))
	check(not poor.is_empty(), "a technology the stock can't pay for")
	if not poor.is_empty():
		var b: Button = instance_from_id(poor[0]["id"])
		var masks: Array = b.get_children().filter(func(c): return c is ColorRect)
		check(poor[0]["disabled"] and masks.size() == 1, "is disabled under a red mask")
	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
