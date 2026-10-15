extends SceneTree
# 0.28's command panel tooltips (gui/session/selection_panels.js):
#   1. Construction: names, visible classes, Identity tooltip, cost, dropsite, garrison,
#      population bonus, "Right-click for more information." and the EntityLimits line;
#      a right click opens the Template Viewer for the structure;
#   2. Training: names, classes, Identity tooltip, cost and the right-click line; a right
#      click on a row opens the viewer for the unit;
#   3. Research: the right-click line.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_command_tooltips.gd
# Exit code 0 = pass.

var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	current_scene = main
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func buttons(n: Node, out: Array) -> Array:
	if n is Button and n.is_visible_in_tree():
		out.append(n)
	for c in n.get_children():
		buttons(c, out)
	return out

func button_with(prefix: String) -> Button:
	for b in buttons(main, []):
		if b.tooltip_text.begins_with(prefix):
			return b
	return null

func roster() -> ItemList:
	for l in main.find_children("*", "ItemList", true, false):
		if l.is_visible_in_tree() and l.item_count > 0:
			return l
	return null

func viewers() -> int:
	var n := 0
	for c in main.find_children("*", "", true, false):
		if c.get_script() and str(c.get_script().resource_path).ends_with("template_viewer.gd"):
			n += 1
	return n

func close_viewers() -> void:
	for c in main.find_children("*", "", true, false):
		if c.get_script() and str(c.get_script().resource_path).ends_with("template_viewer.gd"):
			c.close_viewer()
	await frames(2)

func right_click(c: Control) -> void:
	var ev := InputEventMouseButton.new()
	ev.button_index = MOUSE_BUTTON_RIGHT
	ev.pressed = true
	c.gui_input.emit(ev)
	await frames(2)

func run() -> void:
	await frames(60)

	print("1. Construction")
	main.select_first_own(false)
	await frames(5)
	var house := button_with("House")
	if house == null:
		for b in buttons(main, []):
			if "Population Bonus:" in b.tooltip_text:
				house = b
				break
	check(house != null, "a worker's build buttons include a house")
	if house:
		var tip: String = house.tooltip_text
		print(tip)
		check(tip.contains("Classes:"), "visible classes")
		check(tip.contains("Cost:") and tip.contains("Population Bonus:"), "cost and population bonus")
		check(tip.contains("Right-click for more information."), "the right-click line")
		var cc := button_with("Civic Center")
		if cc == null:
			for b in buttons(main, []):
				if b.tooltip_text.contains("Civic Center") and b.tooltip_text.contains("Dropsite for:"):
					cc = b
		check(cc != null and cc.tooltip_text.contains("Dropsite for:") and cc.tooltip_text.contains("Garrison Limit"),
				"the Civic Center's dropsite and garrison lines")
		check(cc != null and cc.tooltip_text.contains("Current Count:"), "and its EntityLimits line")
		var temple: Button = null
		for b in buttons(main, []):
			if b.tooltip_text.contains("\nMedical Treatment: "):
				temple = b
		check(temple != null and temple.tooltip_text.contains("Humans +1 health regeneration rate. Range: 40 meters"),
				"a temple's aura line (getAurasTooltip)")
		# getEntityCostTooltip for a wallset: the pieces' range and the tower's cost.
		var pal: Button = null
		for b in buttons(main, []):
			if b.tooltip_text.contains("\nWalls:  "):
				pal = b
				break
		if pal:
			print(pal.tooltip_text)
		check(pal != null and RegEx.create_from_string("\nWalls:  .*\\d+ to .*\\d+.*\nTowers:  ").search(pal.tooltip_text) != null
				and not pal.tooltip_text.contains("\nCost:"), "a wallset: Walls N to M, Towers, no Cost line")
		var before := viewers()
		await right_click(house)
		check(viewers() == before + 1, "a right click opens the Template Viewer")
		await close_viewers()

	print("2. Training")
	main.select_first_own(true)
	await frames(5)
	var list := roster()
	check(list != null, "the Civic Centre lists its trainable units")
	if list:
		var tip := list.get_item_tooltip(0)
		print(tip)
		check(tip.contains("Classes:"), "visible classes")
		check(tip.contains("Right-click for more information."), "the right-click line")
		var before := viewers()
		list.item_clicked.emit(0, Vector2.ZERO, MOUSE_BUTTON_RIGHT)
		await frames(2)
		check(viewers() == before + 1, "a right click on a row opens the Template Viewer")
		await close_viewers()

	print("3. Research")
	var tech: Button = null
	for b in buttons(main, []):
		if b.tooltip_text.contains("Cost:") and b.tooltip_text.contains("Right-click for more information.") and not b.tooltip_text.contains("Classes:"):
			tech = b
			break
	check(tech != null, "a research button carries the right-click line")

	print("PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
