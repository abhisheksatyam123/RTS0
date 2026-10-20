extends SceneTree
# 0.28's session menu (Menu.js, MenuButtons.js): opening it doesn't pause the
# game; Esc ("cancel") and the menu hotkey close it; its Pause button pauses and
# reads Resume until pressed again; Save pauses a single-player game while its
# page is open; there is no Load entry. The panel hangs under the Menu button (Menu.xml), whose
# tooltip names the hotkey, and a click in the game world closes it (input.js g_Menu.close).
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
	check(names == ["BtnManual", "BtnChat", "BtnSave", "BtnSummary", "BtnLobby", "BtnOptions", "BtnHotkeys", "BtnPause", "BtnResign", "BtnExit"],
		"0.28's buttons in order, no Load: " + str(names))
	main._on_menu_button_pressed()
	await frames(10) # the slide down
	check(menu.visible and not paused and not main.is_game_paused(), "the menu opens over the running game")
	check(btn(menu, "BtnLobby").disabled, "Lobby is disabled without a lobby connection")
	check(btn(menu, "BtnSummary").tooltip_text == "Press [color=#fffb83][lb]Ctrl+Tab[rb][/color] to open the summary screen.",
		"Summary's tooltip names the summary hotkey in gold (colorizeHotkey): " + btn(menu, "BtnSummary").tooltip_text)
	await key(KEY_ESCAPE)
	check(not menu.visible, "Esc closes the menu")
	check(main.get_node_or_null("PauseMenuClosing") != null, "and its panel slides back up (Menu.js close)")
	await frames(10)
	check(main.get_node_or_null("PauseMenuClosing") == null, "then the sliding copy is gone")
	# MenuButtons Lobby: enabled while signed in (Engine.HasXmppClient), and it opens
	# the lobby page as a dialog (page_lobby.xml {"dialog": true}), not the host/join panel.
	var ls = root.get_node("LobbySession")
	ls.token = "e2e"
	main._on_menu_button_pressed()
	await frames(10)
	check(not btn(menu, "BtnLobby").disabled, "Lobby is enabled once signed in")
	menu._on_lobby_pressed()
	await frames(2)
	var page = root.get_node_or_null("LobbyDialog")
	check(page != null and page.get_child(0).dialog and not menu.visible, "it opens the lobby page as a dialog and closes the menu")
	if page:
		page.queue_free()
	ls.token = ""
	await frames(2)

	main._on_menu_button_pressed()
	await frames(10) # the slide down
	await key(KEY_F10)
	check(not menu.visible, "the menu hotkey (F10) closes it too")
	await key(KEY_F10)
	await frames(10) # the slide down
	check(menu.visible and btn(menu, "BtnPause").text == "Pause" and not btn(menu, "BtnPause").disabled, "and opens it again, Pause enabled")
	check(btn(menu, "BtnPause").tooltip_text.begins_with("Press ") and btn(menu, "BtnPause").tooltip_text.ends_with(" to pause or resume the game."),
		"Pause's 0.28 tooltip: " + btn(menu, "BtnPause").tooltip_text)

	await click(btn(menu, "BtnPause").get_global_rect().get_center())
	check(not menu.visible and main.is_game_paused(), "Pause closes the menu and pauses")
	main._on_menu_button_pressed()
	await frames(10) # the slide down
	check(btn(menu, "BtnPause").text == "Resume", "the button reads Resume while paused")
	await click(btn(menu, "BtnPause").get_global_rect().get_center())
	check(not menu.visible and not main.is_game_paused(), "Resume resumes")

	main._on_menu_button_pressed()
	await frames(10) # the slide down
	await click(btn(menu, "BtnSave").get_global_rect().get_center())
	await frames(2)
	var dlg = main.get_children().filter(func(c): return c.get_script() == load("res://scenes/session/savegame_dialog.gd"))
	check(not menu.visible and dlg.size() == 1 and paused, "Save opens its page and pauses the single-player game")
	if dlg.size() == 1:
		dlg[0].close()
	await frames(2)
	check(not paused, "closing the page resumes")

	for b in [["BtnHotkeys", "Hotkeys"], ["BtnManual", "Manual"]]:
		main._on_menu_button_pressed()
		await frames(10)
		await click(btn(menu, b[0]).get_global_rect().get_center())
		await frames(3)
		var pages: Array = main.find_children(b[1], "Control", true, false)
		check(not menu.visible and pages.size() == 1 and paused, b[1] + " opens over the game and pauses it")
		if pages.size() == 1:
			if b[1] == "Hotkeys":
				pages[0]._on_close()
			else:
				pages[0].close_page()
		await frames(3)
		check(not paused and main.find_children(b[1], "Control", true, false).is_empty(), b[1] + " closes back to the running game")
	print("Layout, tooltip and world clicks")
	main._on_menu_button_pressed()
	await frames(10)
	var mrect: Rect2 = main.menu_button_rect()
	var prect: Rect2 = menu.get_node("CenterContainer/Panel").get_global_rect()
	check(absf(prect.end.x - mrect.end.x) < 1.0 and prect.position.y >= mrect.end.y and prect.size.x == 164.0,
		"the panel hangs under the Menu button: %s under %s" % [prect, mrect])
	var mb: Button = null
	for b in main.find_children("*", "Button", true, false):
		if b.text == "Menu":
			mb = b
	check(mb != null and mb.tooltip_text.begins_with("Press ") and mb.tooltip_text.ends_with(" to toggle this menu."),
		"the Menu button's tooltip: " + (mb.tooltip_text if mb else ""))
	check(mb != null and "[color=#fffb83][lb]F10[rb][/color]" in mb.tooltip_text, "in gold, as colorizeHotkey: " + (mb.tooltip_text if mb else ""))
	await click(root.get_visible_rect().size / 2)
	check(not menu.visible, "a click in the game world closes it")

	print("Pass 10: the pause hotkey, Chat, the window focus and the Template Viewer")
	main._on_menu_button_pressed()
	await frames(10)
	await key(KEY_PAUSE)
	check(not menu.visible and main.is_game_paused(), "the pause hotkey closes the menu and pauses (Menu.js initButton)")
	main._on_menu_button_pressed()
	await frames(10)
	check(btn(menu, "BtnPause").text == "Resume", "the reopened menu reads Resume")
	await key(KEY_PAUSE)
	await frames(2)
	check(not menu.visible and not main.is_game_paused(), "and the hotkey resumes")
	main._on_menu_button_pressed()
	await frames(10)
	await click(btn(menu, "BtnChat").get_global_rect().get_center())
	await frames(2)
	check(main.is_chat_open(), "Chat opens the chat")
	main._on_menu_button_pressed()
	await frames(10)
	await click(btn(menu, "BtnChat").get_global_rect().get_center())
	await frames(2)
	check(main.is_chat_open(), "and pressing it again keeps it open (Chat.openPage, not a toggle)")
	await key(KEY_ESCAPE)
	await frames(2)
	var t0: float = main.get_overlay_times()["elapsed"]
	main.notification(NOTIFICATION_APPLICATION_FOCUS_OUT)
	await frames(20)
	var t1: float = main.get_overlay_times()["elapsed"]
	check(t1 == t0 and not main.is_game_paused(), "with the window unfocused the game waits, with no pause overlay (pauseonfocusloss): %.2f -> %.2f" % [t0, t1])
	main.notification(NOTIFICATION_APPLICATION_FOCUS_IN)
	await frames(20)
	check(main.get_overlay_times()["elapsed"] > t1, "and it carries on when focus returns")
	main.open_template_viewer("unit", 0, 0)
	await frames(2)
	var viewers: Array = main.find_children("TemplateViewer", "Control", true, false)
	check(viewers.size() == 1 and paused, "the Template Viewer pauses the single-player game (showTemplateDetails)")
	if viewers.size() == 1:
		viewers[0].close_viewer()
	await frames(3)
	check(not paused, "and closing it resumes")
	print("FAILURES: ", failures)
	quit(1 if failures else 0)
