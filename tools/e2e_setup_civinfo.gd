extends SceneTree
# 0.28 Panels/Buttons/CivInfoButton.js: the info button at the Civilization heading and the
# civinfo / structree hotkeys open the Civilization Overview and Structure Tree over Game
# Setup; the button reopens the last one viewed, and Esc closes them.

var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	change_scene_to_file("res://scenes/menu/gamesetup.tscn")
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func press(code: Key, mods := {}) -> void:
	var e := InputEventKey.new()
	e.keycode = code
	e.physical_keycode = code
	e.alt_pressed = mods.get("alt", false)
	e.shift_pressed = mods.get("shift", false)
	e.pressed = true
	Input.parse_input_event(e)
	await frames(1)
	e = e.duplicate()
	e.pressed = false
	Input.parse_input_event(e)
	await frames(2)

func run() -> void:
	await frames(10)
	var page = current_scene
	var info: Button = page.find_child("CivInfoButton", true, false)
	check(info != null and info.tooltip_text.begins_with("[color=#fffb83][lb]Alt+Shift+H[rb][/color] / [color=#fffb83][lb]Alt+Shift+T[rb][/color]: View Civilization Overview / Structure Tree"), "the info button and its tooltip: " + (info.tooltip_text if info else ""))
	check(page.find_child("CivPreview", true, false) == null, "no invented civ blurb above the slots")
	info.pressed.emit()
	await frames(3)
	var layer: Node = page.get_node_or_null("CivReference")
	var tabs: TabContainer = layer.find_child("Pages", true, false) if layer else null
	check(tabs != null and tabs.current_tab == 0 and tabs.get_current_tab_control().name == "Civilization", "the button opens the Civilization Overview")
	await press(KEY_T, {"alt": true, "shift": true})
	check(tabs != null and tabs.current_tab == 1, "the structree hotkey switches to the Structure Tree")
	var close: Button = layer.find_child("Close", true, false) if layer else null
	check(close != null and close.tooltip_text.ends_with(": Close Structure Tree.") and close.tooltip_text.contains("[color="),
		"Close names its key and page: " + (close.tooltip_text if close else ""))
	await press(KEY_T, {"alt": true, "shift": true})
	await frames(2)
	check(page.get_node_or_null("CivReference") == null, "the Structure Tree's own hotkey closes it")
	info.pressed.emit()
	await frames(3)
	layer = page.get_node_or_null("CivReference")
	tabs = layer.find_child("Pages", true, false) if layer else null
	await press(KEY_ESCAPE)
	await frames(2)
	check(page.get_node_or_null("CivReference") == null, "Esc closes it")
	info.pressed.emit()
	await frames(3)
	layer = page.get_node_or_null("CivReference")
	tabs = layer.find_child("Pages", true, false) if layer else null
	check(tabs != null and tabs.current_tab == 1, "the button reopens the last one viewed")
	if layer:
		layer.queue_free()
	await frames(2)
	await press(KEY_H, {"alt": true, "shift": true})
	layer = page.get_node_or_null("CivReference")
	tabs = layer.find_child("Pages", true, false) if layer else null
	check(tabs != null and tabs.current_tab == 0, "the civinfo hotkey opens the Civilization Overview")
	print("FAILURES: ", failures)
	quit(1 if failures else 0)
