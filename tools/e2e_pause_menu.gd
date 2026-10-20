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
	# A child page takes the keyboard: the menu hotkey does nothing behind it,
	# a second open keeps the one page, and the lobby hotkey closes it (QuitButton.js).
	await key(KEY_F10)
	await frames(10)
	check(not menu.visible, "the lobby dialog takes the keyboard: F10 opens no menu behind it")
	load("res://scenes/menu/lobby.gd").open_dialog(self)
	await frames(2)
	check(root.get_children().filter(func(n) -> bool: return n.name.begins_with("LobbyDialog")).size() == 1,
		"opening the lobby again keeps the one dialog")
	for down in [true, false]:
		var k := InputEventKey.new()
		k.keycode = KEY_L
		k.physical_keycode = KEY_L
		k.alt_pressed = true
		k.pressed = down
		Input.parse_input_event(k)
		await frames(2)
	check(root.get_node_or_null("LobbyDialog") == null, "and the lobby hotkey (%s) closes it" % str(RtsoHotkeyConfig.new().get_default("lobby")))
	page = root.get_node_or_null("LobbyDialog")
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
	check(absf(prect.end.x - (mrect.end.x + 8.0)) < 1.0 and prect.position.y >= mrect.end.y and prect.size.x == 164.0 and mrect.size.x == 156.0,
		"the 164 px panel hangs under the 156 px Menu button, 8 px past its right edge: %s under %s" % [prect, mrect])
	# Menu.xml: StonePanelThinBorder with StoneButtonFancy buttons, 28 px at a 32 px pitch.
	var panel: PanelContainer = menu.get_node("CenterContainer/Panel")
	var pstyle = panel.get_theme_stylebox("panel")
	var b0: Button = btn(menu, "BtnManual")
	var b1: Button = btn(menu, "BtnChat")
	var over = b0.get_theme_stylebox("hover")
	check("parts" in pstyle and pstyle.parts.size() == 9 and pstyle.get_margin(SIDE_LEFT) == 4.0
		and "images" in over and over.images.size() == 3 and b0.get_theme_stylebox("normal").images.size() == 1
		and b0.get_theme_color("font_color") == Color.WHITE and b0.get_theme_font_size("font_size") == 14
		and b0.size.y == 28.0 and b1.position.y - b0.position.y == 32.0,
		"the menu is a StonePanelThinBorder of StoneButtonFancy buttons")
	# Esc (cancel) closes the menu and every open dialog (closeOpenDialogs).
	main.toggle_chat()
	await frames(2)
	main._on_menu_button_pressed()
	await frames(10)
	var esc := InputEventKey.new()
	esc.keycode = KEY_ESCAPE
	esc.physical_keycode = KEY_ESCAPE
	esc.pressed = true
	menu._unhandled_input(esc)
	await frames(10)
	check(not menu.visible and not main.debug_dialogs()["chat"], "Esc closes the menu and the chat")
	# Top panel: Game Speed first, then Diplomacy, Barter & Trade, Match Settings; their tooltips colorize the hotkey.
	var speed: Button = main.find_child("GameSpeedButton", true, false)
	main._refresh_menu_tooltip()
	var row: Array = speed.get_parent().get_children().filter(func(c): return c is Button and c.visible)
	var at: int = row.find(speed)
	check(at >= 0 and at + 3 < row.size() and speed.tooltip_text == "Game Speed" and speed.text == ""
		and str(row[at + 1].tooltip_text).ends_with(" Diplomacy") and str(row[at + 2].tooltip_text).ends_with(" Barter & Trade")
		and str(row[at + 3].tooltip_text).ends_with(" Match Settings") and str(row[at + 1].tooltip_text).contains("[color="),
		"Game Speed, then Diplomacy, Barter & Trade, Match Settings with coloured hotkeys: " + str(row[at + 1].tooltip_text if at >= 0 and at + 1 < row.size() else ""))
	var mb: Button = null
	# BuildLabel: the release line over the build string; no clock or phase in the bar.
	var bl: Label = main.find_child("BuildLabel", true, false)
	check(bl != null and bl.text.begins_with("Release XXVIII: Boiorix\nBuild: "), "the top panel's build label: " + (bl.text.replace("\n", " | ") if bl else ""))
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
