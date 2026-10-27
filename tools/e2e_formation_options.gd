extends SceneTree
# 0.28's two formation options (gui/options/options.json, Gameplay):
#   1. Battalion-style formations (gui.session.selectformationasone, selection.js
#      addFormationMembers): clicking one unit of a formation selects all of it;
#   2. Formation control (gui.session.formationwalkonly, AutoFormation.getNull):
#      "Walk/Patrol Only" takes the units out of their formation for any other
#      order (here gather); "No override" leaves the formation to do it;
#   3. Default formation (gui.session.defaultformation, AutoFormation.js): a walk puts
#      units in no formation into it (Box out of the box); right-click on a formation
#      button makes it the default, on None turns the feature off.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_formation_options.gd
# Exit code 0 = pass.

var main
var failures := 0
var cfg := RtsoUserConfig.new()

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

func units() -> Array:
	return main.debug_selection_state().get("units", [])

func formed(ids: Array) -> Array:
	return units().filter(func(u): return ids.has(u["id"]) and int(u["formation"]) != 0)

func form_up() -> Array:
	main.debug_selection_grid(true)
	await frames(2)
	var ids: Array = main.debug_selection_state()["selected"]
	main._on_formation_selected(2) # box
	await frames(20)
	return ids

func click(at: Vector2) -> void:
	var mb := InputEventMouseButton.new()
	mb.button_index = MOUSE_BUTTON_LEFT
	mb.position = at
	mb.global_position = at
	mb.pressed = true
	root.push_input(mb, true)
	await frames(1)
	mb = mb.duplicate()
	mb.pressed = false
	root.push_input(mb, true)
	await frames(3)

func right_click_beside(ids: Array) -> void:
	var u: Dictionary = units().filter(func(x): return ids.has(x["id"]))[0]
	var at: Vector2 = root.get_camera_3d().unproject_position(Vector3(u["pos"].x + 5.0, 0.0, u["pos"].y + 5.0))
	var mb := InputEventMouseButton.new()
	mb.button_index = MOUSE_BUTTON_RIGHT
	mb.position = at
	mb.global_position = at
	mb.pressed = true
	root.push_input(mb, true)
	await frames(1)
	mb = mb.duplicate()
	mb.pressed = false
	root.push_input(mb, true)
	await frames(10)

func right_press_formation(f: int) -> void:
	var mb := InputEventMouseButton.new()
	mb.button_index = MOUSE_BUTTON_RIGHT
	mb.pressed = true
	main._on_formation_gui_input(mb, f)

func run() -> void:
	await frames(60)
	var keep_walk: String = cfg.get_value("gui.session.formationwalkonly")
	var keep_one: String = cfg.get_value("gui.session.selectformationasone")
	# Soldiers only: the civilians can't take a box.
	var civ := PackedInt32Array()
	for u in units():
		if u["civilian"]:
			civ.append(u["id"])
	main.debug_delete_units(civ)
	await frames(10)

	print("1. Battalion-style formations")
	var ids: Array = await form_up()
	check(ids.size() >= 2 and formed(ids).size() == ids.size(), "%d soldiers in a box" % formed(ids).size())
	var one: Dictionary = formed(ids)[0] if formed(ids).size() else {}
	if not one.is_empty():
		var at: Vector2 = root.get_camera_3d().unproject_position(Vector3(one["pos"].x, 0.5, one["pos"].y))
		main.clear_selection()
		cfg.set_value("gui.session.selectformationasone", "true")
		await click(at)
		var sel: Array = main.debug_selection_state()["selected"]
		check(sel.size() == ids.size(), "one click selects the formation: %d of %d" % [sel.size(), ids.size()])
		main.clear_selection()
		await create_timer(0.8).timeout # not a double click
		cfg.set_value("gui.session.selectformationasone", "false")
		for u in units(): # the box may have shuffled since
			if u["id"] == one["id"]:
				at = root.get_camera_3d().unproject_position(Vector3(u["pos"].x, 0.5, u["pos"].y))
		await click(at)
		sel = main.debug_selection_state()["selected"]
		check(sel.size() == 1, "off: one click selects one unit: %d" % sel.size())

	print("2. Formation control")
	cfg.set_value("gui.session.formationwalkonly", "false")
	ids = await form_up()
	main.debug_gather_nearest() # the selected box gathers
	await frames(10)
	check(formed(ids).size() == ids.size(), "No override: the box gathers as a formation (%d formed)" % formed(ids).size())
	cfg.set_value("gui.session.formationwalkonly", "true")
	ids = await form_up()
	check(formed(ids).size() == ids.size(), "formed up again")
	main.debug_gather_nearest()
	await frames(10)
	check(formed(ids).is_empty(), "Walk/Patrol Only: gathering leaves the formation (%d formed)" % formed(ids).size())

	print("3. Default formation")
	var keep_default: String = cfg.get_value("gui.session.defaultformation")
	check(RtsoUserConfig.new().get_value("gui.session.defaultformation") != "", "a default formation is configured")
	right_press_formation(2)
	check(cfg.get_value("gui.session.defaultformation") == "special/formations/box", "right-click on Box makes it the default")
	await frames(10)
	# formation_panel.xml: 38x38 buttons, upgrade.png over the default formation's icon.
	var def_btn: Button = null
	for b in main.find_children("*", "Button", true, false):
		if b.is_visible_in_tree() and b.tooltip_text.contains("This is the default formation, used for movement orders."):
			def_btn = b
	check(def_btn != null and def_btn.custom_minimum_size == Vector2(38, 38) and def_btn.find_child("default", false, false) != null
		and str((def_btn.find_child("default", false, false) as TextureRect).texture.resource_path).ends_with("icons/upgrade.png"),
		"the default formation's 38 px button carries upgrade.png")
	ids = main.debug_selection_state()["selected"]
	main._on_formation_selected(0) # leave the box
	await frames(10)
	check(formed(ids).is_empty(), "out of formation (%d formed)" % formed(ids).size())
	await right_click_beside(ids)
	check(formed(ids).size() == ids.size(), "a walk puts them in the default Box (%d of %d)" % [formed(ids).size(), ids.size()])
	right_press_formation(0)
	check(cfg.get_value("gui.session.defaultformation") == "special/formations/null", "right-click on None turns it off")
	main._on_formation_selected(0)
	await frames(10)
	await right_click_beside(ids)
	check(formed(ids).is_empty(), "then a walk keeps them loose (%d formed)" % formed(ids).size())
	# AutoFormation.js: lastDefault starts as the saved default, so the toggle hotkey
	# (session.toggledefaultformation) turns a saved Close Order off and back on, not Box.
	cfg.set_value("gui.session.defaultformation", "special/formations/line_closed")
	main.toggle_default_formation()
	check(cfg.get_value("gui.session.defaultformation") == "special/formations/null", "the toggle turns the default off")
	main.toggle_default_formation()
	check(cfg.get_value("gui.session.defaultformation") == "special/formations/line_closed", "and back on to the saved Close Order")

	cfg.set_value("gui.session.defaultformation", keep_default)
	cfg.set_value("gui.session.formationwalkonly", keep_walk)
	cfg.set_value("gui.session.selectformationasone", keep_one)
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
