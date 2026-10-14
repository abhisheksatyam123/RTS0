extends SceneTree
# End-to-end check of the main menu's 0 A.D. 0.28 entries (gui/pregame/MainMenuItems.js,
# MainMenuItemHandler.js) and the message box (mod gui/msgbox, timedconfirmation):
#   1. the main buttons carry 0.28's tooltips; Continue Campaign is there,
#      disabled with no campaign run;
#   2. the submenus have 0.28's captions, Structure Tree's tooltip names its hotkey;
#   3. Settings > Hotkeys opens the hotkey editor over the menu;
#   4. Exit asks "Are you sure you want to quit 0 A.D.?"; No (and Escape) keeps the menu;
#   5. a timed confirmation counts down and answers its first button when time runs out.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_main_menu.gd
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

func key(code: Key) -> void:
	for down in [true, false]:
		var k := InputEventKey.new()
		k.keycode = code
		k.physical_keycode = code
		k.pressed = down
		Input.parse_input_event(k)
		await frames(1)

func items(menu, id: String) -> Array:
	menu._toggle_submenu(id, menu.get_node("MainPanel/Buttons/BtnLearn"))
	var out: Array = menu.submenu_buttons_box.get_children().filter(func(b): return not b.is_queued_for_deletion())
	menu._close_submenu()
	return out

func run() -> void:
	change_scene_to_file("res://scenes/menu/main_menu.tscn")
	await frames(10)
	var menu = current_scene
	print("1. Main buttons")
	var btns: Node = menu.get_node("MainPanel/Buttons")
	check(btns.get_node("BtnSingle").tooltip_text == "Start, load, or replay a single-player game.", "Single-player tooltip")
	check(btns.get_node("BtnExit").tooltip_text == "Exit the game.", "Exit tooltip")
	var cont: Button = btns.get_node("BtnContinue")
	check(cont.get_index() == btns.get_node("BtnLearn").get_index() + 1 and cont.disabled == (CampaignRun.current_run() == null),
		"Continue Campaign after Learn to Play, enabled only with a campaign run")

	print("2. Submenus")
	var learn: Array = items(menu, "learn").map(func(b): return b.text)
	check(learn.has("Structure Tree") and learn.has("Map Overview"), "Learn to Play: " + str(learn))
	var st: Button = items(menu, "learn").filter(func(b): return b.text == "Structure Tree")[0]
	check(st.tooltip_text.ends_with(": View the structure tree of civilizations featured in 0 A.D.") and not st.tooltip_text.begins_with("%"), "Structure Tree names its hotkey: " + st.tooltip_text)
	var single: Array = items(menu, "single").map(func(b): return b.text)
	check(single.slice(0, 2) == ["Matches", "Load Game"] and single.has("Replays"), "Single-player: " + str(single))
	var multi: Array = items(menu, "multi").map(func(b): return b.text)
	check(multi == ["Game Lobby", "Connect by IP", "Host New Game", "Host Saved Game", "Replays"], "Multiplayer: " + str(multi))
	var settings: Array = items(menu, "settings").map(func(b): return b.text)
	check(settings == ["Options", "Hotkeys", "Language", "Mod Selection", "Welcome Screen"], "Settings: " + str(settings))

	print("3. Hotkeys")
	menu._execute_action("open_hotkeys")
	await frames(4)
	var editors: Array = menu.find_children("*", "CanvasLayer", true, false).filter(func(l): return l.get_child_count() > 0 and l.get_child(0).get_script() != null and l.get_child(0).get_script().resource_path.ends_with("hotkeys.gd"))
	check(editors.size() == 1 and current_scene == menu, "Settings > Hotkeys opens the hotkey editor over the menu")
	for layer in menu.find_children("*", "CanvasLayer", true, false):
		layer.queue_free()
	await frames(2)

	print("4. Exit")
	menu._on_exit_pressed()
	await frames(3)
	var box = menu.find_child("MsgBox", true, false)
	check(box != null and box.get_node("Main/Text").text == "Are you sure you want to quit 0 A.D.?" and box.get_node("Main/Title").text == "Confirmation", "Exit asks first")
	if box:
		check(box.get_node("Main/Button1").text == "No" and box.get_node("Main/Button2").text == "Yes", "No, then Yes")
		box.get_node("Main/Button1").pressed.emit()
		await frames(3)
		check(menu.find_child("MsgBox", true, false) == null and current_scene == menu, "No keeps the menu")
	menu._on_exit_pressed()
	await frames(3)
	await key(KEY_ESCAPE)
	await frames(3)
	check(menu.find_child("MsgBox", true, false) == null and current_scene == menu, "Escape answers No")

	print("5. Timed confirmation")
	var answer := [-1]
	var t = load("res://scenes/common/msgbox.gd").open_timed(menu, 400, 200, "Reverting in %(time)s seconds.", "time", 1500, "Confirm", ["Revert", "Keep"])
	t.answered.connect(func(i): answer[0] = i)
	await frames(2)
	check(t.get_node("Main/Text").text == "Reverting in 2 seconds.", "counts the seconds: " + t.get_node("Main/Text").text)
	var start := Time.get_ticks_msec()
	while answer[0] == -1 and Time.get_ticks_msec() - start < 5000:
		await frames(1)
	check(answer[0] == 0, "answers the first button when time runs out")
	print("PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
