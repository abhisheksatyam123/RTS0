extends SceneTree
# 0.28's session menu (Menu.js, MenuButtons.js): opening it doesn't pause the
# game; Esc ("cancel") and the menu hotkey close it; its Pause button pauses and
# reads Resume until pressed again; Save pauses a single-player game while its
# page is open; there is no Load entry.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_pause_menu.gd
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

func click(pos: Vector2) -> void:
	var at := root.get_final_transform() * pos # window pixels
	var mm := InputEventMouseMotion.new()
	mm.position = at
	mm.global_position = at
	Input.parse_input_event(mm)
	await frames(2)
	for down in [true, false]:
		var mb := InputEventMouseButton.new()
		mb.button_index = MOUSE_BUTTON_LEFT
		mb.pressed = down
		mb.position = at
		mb.global_position = at
		Input.parse_input_event(mb)
		await frames(2)

func key(code: Key) -> void:
	for down in [true, false]:
		var k := InputEventKey.new()
		k.keycode = code
		k.physical_keycode = code
		k.pressed = down
		Input.parse_input_event(k)
		await frames(2)

func btn(menu, name: String) -> Button:
	return menu.get_node_or_null("CenterContainer/Panel/VBox/" + name)

func run() -> void:
	await frames(30)
	var menu = main.get_node("PauseMenu")
	var names: Array = menu.get_node("CenterContainer/Panel/VBox").get_children().map(func(c): return str(c.name))
	check(names == ["BtnChat", "BtnSave", "BtnSummary", "BtnLobby", "BtnOptions", "BtnPause", "BtnResign", "BtnExit"],
		"0.28's buttons in order, no Load: " + str(names))
	main._on_menu_button_pressed()
	await frames(2)
	check(menu.visible and not paused and not main.is_game_paused(), "the menu opens over the running game")
	await key(KEY_ESCAPE)
	check(not menu.visible, "Esc closes the menu")

	main._on_menu_button_pressed()
	await frames(2)
	await key(KEY_F10)
	check(not menu.visible, "the menu hotkey (F10) closes it too")
	await key(KEY_F10)
	check(menu.visible and btn(menu, "BtnPause").text == "Pause" and not btn(menu, "BtnPause").disabled, "and opens it again, Pause enabled")
	check(btn(menu, "BtnPause").tooltip_text.begins_with("Press ") and btn(menu, "BtnPause").tooltip_text.ends_with(" to pause or resume the game."),
		"Pause's 0.28 tooltip: " + btn(menu, "BtnPause").tooltip_text)

	await click(btn(menu, "BtnPause").get_global_rect().get_center())
	check(not menu.visible and main.is_game_paused(), "Pause closes the menu and pauses")
	main._on_menu_button_pressed()
	await frames(2)
	check(btn(menu, "BtnPause").text == "Resume", "the button reads Resume while paused")
	await click(btn(menu, "BtnPause").get_global_rect().get_center())
	check(not menu.visible and not main.is_game_paused(), "Resume resumes")

	main._on_menu_button_pressed()
	await frames(2)
	await click(btn(menu, "BtnSave").get_global_rect().get_center())
	await frames(2)
	var dlg = main.get_children().filter(func(c): return c.get_script() == load("res://scenes/session/savegame_dialog.gd"))
	check(not menu.visible and dlg.size() == 1 and paused, "Save opens its page and pauses the single-player game")
	if dlg.size() == 1:
		dlg[0].close()
	await frames(2)
	check(not paused, "closing the page resumes")
	print("FAILURES: ", failures)
	quit(1 if failures else 0)
