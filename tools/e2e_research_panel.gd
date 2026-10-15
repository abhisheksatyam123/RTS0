extends SceneTree
# 0.28's Research panel (selection_panels.js g_SelectionPanels.Research,
# selection_panels_right/research_panel.xml):
#   1. each technology is an icon button (its 0.28 icon) with names, cost and the
#      right-click line in its tooltip;
#   2. a pair (pair_*.json, the Temple of Isis's Pharaonic and Serapis Cults)
#      stands together with the pair connector, top first, and hovering one
#      crosses out the other;
#   0. names follow getEntityNamesFormatted: the civ's specificName, then the generic one;
#   3. a technology the stock can't pay for is disabled under a red mask with the
#      missing resources; one whose requirements fail is disabled and grey;
#   4. a technology being researched leaves the panel (Researcher.js
#      GetTechnologiesList: researched or in-progress techs give way to their successor).
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
	print("0. Names")
	main.debug_train_buttons(0)  # a Civic Center: the Town Phase
	await frames(2)
	var town: Array = main.debug_command_row().filter(func(e): return TT.plain(str(e["tip"])).contains("(Town Phase)"))
	print("    ", town.map(func(e): return TT.plain(str(e["tip"])).get_slice("\n", 0)))
	check(town.size() == 1 and TT.plain(str(town[0]["tip"])).begins_with("KŌMOPOLIS (Town Phase)"), "the Ptolemies' Town Phase in small caps: KŌMOPOLIS (Town Phase)")
	check(town.size() == 1 and str(town[0]["tip"]).begins_with("[font_size=16][b]K[/b][/font_size][font_size=12][b]ŌMOPOLIS[/b][/font_size] ([font_size=16][b]Town Phase"), "its first letter big, the rest small (namePrimaryBig, namePrimarySmall)")

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
	# Requirements met, resources short (a phase-locked one lists them too, under grey).
	var short := func(e): return TT.plain(str(e["tip"])).contains("\nInsufficient resources: ") and not TT.plain(str(e["tip"])).contains("Unlocked in")
	var poor: Array = row.filter(short)
	print("    ", row.map(func(e): return "%s %s" % [e["text"], "disabled" if e["disabled"] else ""]))
	# Tribute wood away (100 at a time) until one is out of reach.
	for i in 4:
		if not poor.is_empty():
			break
		main._on_diplo_tribute_pressed(1, 0)
		await frames(10)
		row = main.debug_command_row()
		poor = row.filter(short)
	check(not poor.is_empty(), "a technology the stock can't pay for")
	if not poor.is_empty():
		var b: Button = instance_from_id(poor[0]["id"])
		var masks: Array = b.get_children().filter(func(c): return c is ColorRect)
		check(poor[0]["disabled"] and masks.size() == 1, "is disabled under a red mask")

	print("4. Researching one")
	var ready: Array = row.filter(func(e): return not e["disabled"])
	check(not ready.is_empty(), "a technology that can start")
	if not ready.is_empty():
		var name: String = ready[0]["text"]
		(instance_from_id(ready[0]["id"]) as Button).pressed.emit()
		await frames(10)
		main.debug_train_buttons(BLD_DOCK)
		await frames(2)
		row = main.debug_command_row()
		print("    ", row.map(func(e): return e["text"]))
		check(row.all(func(e): return e["text"] != name), "%s leaves the panel while it is researched" % name)
		check(row.all(func(e): return not TT.plain(str(e["tip"])).contains("Already being researched")), "no \"Already being researched\"")
	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
