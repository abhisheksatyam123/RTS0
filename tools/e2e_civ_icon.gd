extends SceneTree
# 0.28's CivIcon (gui/session/top_panel/CivIcon.js): the top panel's civ emblem
# names the civ and the civinfo and structree hotkeys in its tooltip, and a
# click reopens the reference page last viewed (the Structure Tree at first).
# Opening one closes the other session dialogs and pauses a single-player
# game until it closes (implicitPause); the hotkeys switch between the pages.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_civ_icon.gd
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
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func click() -> void:
	var mb := InputEventMouseButton.new()
	mb.button_index = MOUSE_BUTTON_LEFT
	mb.pressed = true
	main._on_civ_icon_gui_input(mb)

func key(code: int, alt := false, shift := false) -> void:
	var k := InputEventKey.new()
	k.keycode = code
	k.physical_keycode = code
	k.alt_pressed = alt
	k.shift_pressed = shift
	k.pressed = true
	root.push_input(k)

func run() -> void:
	await frames(60)
	var icon: Control = main.find_child("CivIcon", true, false)
	var tree = main.get_node("TechTreeDialog")
	check(icon != null and icon.mouse_filter == Control.MOUSE_FILTER_STOP, "the civ emblem takes clicks")
	var tip: String = icon.tooltip_text
	check(tip.contains(": View Civilization Overview") and tip.contains(": View Structure Tree")
		and tip.ends_with("Last opened view will be reopened on click.") and not tip.begins_with("%"), "its tooltip: " + tip.replace("\n", " | "))
	# CivTags sans-bold-16; colorizeHotkey's gold brackets (or "Unassigned hotkey: civinfo").
	check(tip.begins_with("[font_size=16][b]") and tip.contains("[color=#fffb83][lb]") and tip.contains("[rb][/color]: View Civilization Overview"), "the civ name in bold 16, the hotkeys in gold")

	main.toggle_chat()
	await frames(1)
	click()
	await frames(2)
	check(tree.is_open and tree._tabs.current_tab == 0, "a click opens the Structure Tree")
	check(paused, "and pauses the single-player game")
	check(not main.debug_dialogs()["chat"], "the chat closed first")
	key(KEY_H, true, true) # civinfo
	await frames(2)
	check(tree.is_open and tree._tabs.current_tab == 1, "the civinfo hotkey switches to the Civilization Overview while paused")
	key(KEY_ESCAPE)
	await frames(2)
	check(not tree.is_open and not paused, "Escape closes it and the game resumes")
	click()
	await frames(2)
	check(tree.is_open and tree._tabs.current_tab == 1, "a click reopens the last one viewed")
	tree._on_close_pressed()
	await frames(1)
	check(not paused, "closed: not paused")
	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
