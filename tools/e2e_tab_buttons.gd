extends SceneTree
# common/tab_buttons.xml and tab_buttons.js on Options, Credits and Game Setup:
# tab.next / tab.prev (Tab / Shift+Tab) and the mouse wheel over a tab button
# move a tab on, wrapping, and each tab's tooltip ends with the two lines saying
# so (Options' tabs start with their options.json tooltip, Game Setup's with
# "Click to toggle the … settings tab.").
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_tab_buttons.gd
# Exit code 0 = pass.

var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func key(code: Key, shift := false) -> void:
	for down in [true, false]:
		var k := InputEventKey.new()
		k.keycode = code
		k.physical_keycode = code
		k.shift_pressed = shift
		k.pressed = down
		root.push_input(k, true)
		await frames(1)
	await frames(1)

func wheel(c: Control, down: bool) -> void:
	var mb := InputEventMouseButton.new()
	mb.button_index = MOUSE_BUTTON_WHEEL_DOWN if down else MOUSE_BUTTON_WHEEL_UP
	mb.pressed = true
	c.gui_input.emit(mb)
	await frames(1)

func run() -> void:
	print("1. Options")
	change_scene_to_file("res://scenes/menu/options.tscn")
	await frames(5)
	var tabs: TabContainer = current_scene._tabs
	var tip: String = tabs.get_tab_tooltip(0)
	print("    ", tip.replace("\n", " | "))
	check(tip.begins_with("Change general options.\nScroll down or use [Tab, Alt+S] to move a tab down.\nScroll up or use [Alt+W, Shift+Tab] to move a tab up."), "the tab tooltip")
	var buttons: Array = current_scene._tab_buttons.get_children()
	check(buttons.size() == 9 and buttons[0].tooltip_text == tip and buttons[0].button_pressed, "nine vertical tab buttons, the first pressed")
	await key(KEY_TAB)
	check(tabs.current_tab == 1, "Tab moves to the next tab")
	await key(KEY_TAB, true)
	await key(KEY_TAB, true)
	check(tabs.current_tab == tabs.get_tab_count() - 1, "Shift+Tab moves back, wrapping")
	check(buttons[tabs.current_tab].button_pressed, "its button shows pressed")
	await wheel(buttons[0], true)
	check(tabs.current_tab == 0, "the wheel over the tabs moves on, wrapping")
	buttons[3].pressed.emit()
	check(tabs.current_tab == 3, "a click picks a tab")

	print("2. Credits")
	change_scene_to_file("res://scenes/menu/main_menu.tscn")
	await frames(3)
	var credits = load("res://scenes/reference/credits_page.gd").open(current_scene)
	await frames(3)
	var b0: Button = credits.tabs.get_child(0)
	check(b0.tooltip_text == "Scroll down or use [Tab, Alt+S] to move a tab down.\nScroll up or use [Alt+W, Shift+Tab] to move a tab up.", "the tab tooltip")
	await key(KEY_TAB)
	check(credits._panel == 1 and (credits.tabs.get_child(1) as Button).button_pressed, "Tab moves to the next tab")
	await wheel(b0, false)
	check(credits._panel == 0, "the wheel up moves back")
	credits.close_page()
	await frames(2)

	print("3. Game Setup")
	change_scene_to_file("res://scenes/menu/gamesetup.tscn")
	await frames(10)
	var gs = current_scene
	var tb: Button = gs.get_node("%TabMap")
	check(tb.tooltip_text.begins_with("Click to toggle the Map settings tab.\nScroll down or use [Tab, Alt+S] to move a tab down."), "the tab tooltip")
	gs.select_tab(-1)
	await key(KEY_TAB)
	check(gs._tab == 0, "Tab with no tab open opens the first")
	await key(KEY_TAB, true)
	check(gs._tab == 2, "Shift+Tab wraps to the last")
	await wheel(tb, true)
	check(gs._tab == 0, "the wheel moves on, wrapping")

	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
