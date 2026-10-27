extends SceneTree
# 0.28 session options (gui/options/options.json, Display and Gameplay):
#   1. Game time and ceasefire overlays (OverlayCounterElapsedTime /
#      RemainingCeasefire): top-right lines, F12 toggles the game time and saves it;
#   2. Control group membership (gui.session.disjointcontrolgroups): "Single" takes
#      a unit saved to a group out of the others, "Multiple" leaves it in them;
#   3. Scroll batch increment ratio (input.js OnTrainMouseWheel): Shift+wheel over
#      the trainable units moves the batch by 1 / ratio per notch;
#   4. Detailed tooltips (showdetailedtooltips): the Training list's tooltips add
#      resistance, speed and the rest; off, they don't.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_session_options.gd
# Exit code 0 = pass.

const TT = preload("res://scenes/common/tooltips.gd")

var main
var failures := 0
var cfg := RtsoUserConfig.new()
var keep := {}
const KEYS := ["gui.session.timeelapsedcounter", "gui.session.ceasefirecounter", "gui.session.disjointcontrolgroups",
		"gui.session.scrollbatchratio", "showdetailedtooltips"]

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	current_scene = main # user_settings.gd reads the match from the current scene
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func overlay() -> String:
	var us = root.get_node("UserSettings")
	us._update_overlay()
	return us._overlay_label.text if us._overlay_label.visible else ""

func modifier(code: Key, on: bool) -> void:
	var k := InputEventKey.new()
	k.keycode = code
	k.physical_keycode = code
	k.ctrl_pressed = on and code == KEY_CTRL
	k.shift_pressed = on and code == KEY_SHIFT
	k.pressed = on
	Input.parse_input_event(k)
	await frames(1)

func tap(code: Key, ctrl := false) -> void:
	var k := InputEventKey.new()
	k.keycode = code
	k.physical_keycode = code
	k.ctrl_pressed = ctrl
	k.pressed = true
	root.push_input(k, true)
	await frames(1)
	k = k.duplicate()
	k.pressed = false
	root.push_input(k, true)
	await frames(2)

func selected() -> Array:
	return main.debug_selection_state()["selected"]

func roster() -> ItemList:
	for l in main.find_children("*", "ItemList", true, false):
		if l.is_visible_in_tree() and l.item_count > 0:
			return l
	return null

func run() -> void:
	await frames(60)
	for k in KEYS:
		keep[k] = cfg.get_value(k)

	print("1. Game time and ceasefire overlays")
	cfg.set_value("gui.session.timeelapsedcounter", "false")
	check(not RegEx.create_from_string("\\d\\d:\\d\\d").search(overlay()), "off: no game time: '%s'" % overlay())
	await tap(KEY_F12)
	check(cfg.get_bool("gui.session.timeelapsedcounter"), "F12 turns it on")
	check(RegEx.create_from_string("^\\d\\d:\\d\\d").search(overlay()) != null, "the game time shows: '%s'" % overlay())
	main.set_game_speed(2.0)
	check(overlay().contains("(2x)") or overlay().contains("(2.0x)"), "with the speed when not 1x: '%s'" % overlay())
	main.set_game_speed(1.0)
	await tap(KEY_F12)
	check(not cfg.get_bool("gui.session.timeelapsedcounter"), "F12 again turns it off")
	cfg.set_value("gui.session.ceasefirecounter", "true")
	check(overlay() == "", "no ceasefire, no ceasefire line: '%s'" % overlay())

	print("2. Control group membership")
	main.debug_selection_grid(true)
	await frames(2)
	var all: Array = selected()
	await modifier(KEY_CTRL, true)
	await tap(KEY_1, true)
	main.select_first_own(false)
	await frames(2)
	var one: Array = selected()
	cfg.set_value("gui.session.disjointcontrolgroups", "true")
	await tap(KEY_2, true)
	await modifier(KEY_CTRL, false)
	await tap(KEY_1)
	check(selected().size() == all.size() - 1 and not selected().has(one[0]), "Single: group 1 lost the unit saved to 2 (%d of %d)" % [selected().size(), all.size()])
	await create_timer(0.6).timeout # not a double tap
	main.debug_selection_grid(true)
	await frames(2)
	await modifier(KEY_CTRL, true)
	await tap(KEY_1, true)
	main.select_first_own(false)
	await frames(2)
	cfg.set_value("gui.session.disjointcontrolgroups", "false")
	await tap(KEY_2, true)
	await modifier(KEY_CTRL, false)
	await tap(KEY_1)
	check(selected().size() == all.size(), "Multiple: group 1 keeps it (%d of %d)" % [selected().size(), all.size()])
	# selection_group_icons.xml: groups 1 and 2 have buttons, numbered like the keys.
	var panel: Node = main.find_child("UnitGroupPanel", true, false)
	var shown: Array = panel.get_children().filter(func(b): return b.visible).map(func(b): return str(b.name))
	check(shown == ["UnitGroupButton0", "UnitGroupButton1"], "group buttons for groups 1 and 2: " + str(shown))
	var b2: Button = panel.get_child(1)
	check(b2.get_child(1).text == "2" and b2.get_child(0).texture != null, "the second reads 2 and shows a portrait")
	var press := func(b: Button, idx: MouseButton, double := false):
		var mb := InputEventMouseButton.new()
		mb.button_index = idx
		mb.pressed = true
		mb.double_click = double
		b.gui_input.emit(mb)
	press.call(b2, MOUSE_BUTTON_LEFT)
	await frames(2)
	check(selected() == one, "a click selects group 2")
	press.call(panel.get_child(0), MOUSE_BUTTON_LEFT)
	await frames(2)
	check(selected().size() == all.size(), "a click on 1 selects group 1")
	press.call(b2, MOUSE_BUTTON_RIGHT)
	await frames(2)
	check(not b2.visible, "a right click disbands group 2")

	print("3. Scroll batch increment ratio")
	main.select_first_own(true)
	await frames(5)
	var list := roster()
	check(list != null, "the Civic Centre lists its trainable units")
	if list:
		# getTrainingStatus: a batch is added only while Shift is held.
		check(not list.get_item_tooltip(0).contains("to train"), "no batch line without Shift")
		await modifier(KEY_SHIFT, true)
		await frames(2)
		list = roster()
		check(list.get_item_tooltip(0).contains("train 5."), "batch 5 to start: " + list.get_item_tooltip(0).get_slice("\n", list.get_item_tooltip(0).get_slice_count("\n") - 1))
		cfg.set_value("gui.session.scrollbatchratio", "2")
		await modifier(KEY_SHIFT, true)
		for n in 3:
			var mb := InputEventMouseButton.new()
			mb.button_index = MOUSE_BUTTON_WHEEL_UP
			mb.pressed = true
			mb.shift_pressed = true
			list.gui_input.emit(mb)
		await frames(3)
		list = roster()
		check(list.get_item_tooltip(0).contains("train 7."), "three notches at ratio 2 make 6.5, trains 7: " + list.get_item_tooltip(0).get_slice("\n", list.get_item_tooltip(0).get_slice_count("\n") - 1))
		await modifier(KEY_SHIFT, false)

		print("4. Detailed tooltips")
		cfg.set_value("showdetailedtooltips", "false")
		await frames(3)
		var tip := roster().get_item_tooltip(0)
		check(not tip.contains("Resistance:") and not tip.contains("Speed:"), "off: no detailed lines")
		cfg.set_value("showdetailedtooltips", "true")
		await frames(3)
		tip = roster().get_item_tooltip(0)
		check(tip.contains("Health:") and tip.contains("Resistance:") and tip.contains("Speed:"), "on: health, resistance and speed")
		# g_TooltipTextFormats: bold headers and resource icons, drawn by the Tooltips layer.
		check(tip.contains("[img") and TT.plain(tip).contains("Health:"), "in 0.28's tooltip formats")
		check(tip.contains("[b]Health:[/b]") and tip.contains("[b]Resistance:[/b]"), "the detailed lines keep their header font")
		print(tip)

	for k in KEYS:
		cfg.set_value(k, keep[k])
	cfg.save()
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
