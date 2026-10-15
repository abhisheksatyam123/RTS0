extends SceneTree
# Session dialogs as 0 A.D. 0.28 gui/session shows them:
#   1. PauseOverlay: pausing shows "Game Paused" and "Click to Resume Game",
#      and a click on it resumes;
#   2. closeOpenDialogs: opening chat, diplomacy, Barter & Trade or match
#      settings closes the others, and Esc closes them all at once and clears the
#      selection; held keys repeat only 0.28's on="KeyDown" actions;
#   3. ChatMessageFormatPlayer: your line reads "<name> text", "(Ally) <name>
#      text" to allies and "* name text" with /me; no placeholder line;
#   4. g_NetworkCommands in single player: /kick and /kickspecs say there is
#      nobody to kick, /list names the users, /clear empties the history, and
#      none of them is shown as a chat line.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_session_dialogs.gd
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

func dlg() -> Dictionary:
	return main.debug_dialogs()

func open_names() -> Array:
	var d := dlg()
	return ["chat", "diplomacy", "objectives", "barter"].filter(func(k): return d[k])

func key(code: Key, ctrl := false, echo := false) -> void:
	var e := InputEventKey.new()
	e.keycode = code
	e.physical_keycode = code
	e.ctrl_pressed = ctrl
	e.echo = echo
	e.pressed = true
	Input.parse_input_event(e)
	await frames(1)
	e = e.duplicate()
	e.pressed = false
	e.echo = false
	Input.parse_input_event(e)
	await frames(2)

func chat_input() -> LineEdit:
	for le in main.find_children("*", "LineEdit", true, false):
		if le.placeholder_text == "Type message...":
			return le
	return null

func run() -> void:
	var mm := InputEventMouseMotion.new()
	mm.position = Vector2(32, 26)
	Input.parse_input_event(mm)
	await frames(60)

	print("1. Pause overlay")
	check(not dlg()["overlay"], "no overlay while the game runs")
	main.toggle_pause()
	await frames(2)
	check(dlg()["overlay"] and dlg()["overlay_resume"], "pausing shows Game Paused and Click to Resume Game")
	var overlay: Button = null
	for b in main.find_children("*", "Button", true, false):
		if b.is_visible_in_tree() and b.get_children().any(func(c): return c is Label and c.text == "Game Paused"):
			overlay = b
	check(overlay != null, "the overlay is a button")
	if overlay:
		overlay.emit_signal("pressed")
	await frames(2)
	check(not dlg()["overlay"], "clicking it resumes")

	print("2. One dialog at a time")
	main.toggle_chat()
	await frames(2)
	check(open_names() == ["chat"], "chat opens: " + str(open_names()))
	main.toggle_diplomacy()
	await frames(2)
	check(open_names() == ["diplomacy"], "diplomacy closes chat: " + str(open_names()))
	main._toggle_session_dialog("BarterDialog")
	await frames(2)
	check(open_names() == ["barter"], "Barter & Trade closes diplomacy: " + str(open_names()))
	main._toggle_session_dialog("Objectives")
	await frames(2)
	check(open_names() == ["objectives"], "match settings close Barter & Trade: " + str(open_names()))
	var esc := InputEventKey.new()
	esc.keycode = KEY_ESCAPE
	esc.physical_keycode = KEY_ESCAPE
	esc.pressed = true
	Input.parse_input_event(esc)
	await frames(1)
	esc = esc.duplicate()
	esc.pressed = false
	Input.parse_input_event(esc)
	await frames(2)
	check(open_names().is_empty(), "Esc closes it: " + str(open_names()))
	# misc.xml: Escape is cancel (closeOpenDialogs) and selection.cancel (clearSelection),
	# both on one press; a held Escape repeats selection.cancel (on="KeyDown").
	main.select_first_own(false)
	main.toggle_diplomacy()
	await frames(2)
	await key(KEY_ESCAPE)
	check(open_names().is_empty() and (main.debug_selection_state()["selected"] as Array).is_empty(),
		"one Esc closes Diplomacy and clears the selection")
	main.select_first_own(false)
	await key(KEY_ESCAPE, false, true)
	check((main.debug_selection_state()["selected"] as Array).is_empty(), "a held Esc's repeat clears the selection too")
	await key(KEY_H, true, true)
	check(open_names().is_empty(), "a held Ctrl+H's repeat doesn't toggle Diplomacy (on=Press)")
	await key(KEY_H, true)
	check(open_names() == ["diplomacy"], "pressing Ctrl+H does")
	main.toggle_diplomacy()
	await frames(2)
	# MenuButtons and SessionMessageBox: closeOpenDialogs before a page or question.
	var pm: Node = main.get_node("PauseMenu")
	for b in [["Hotkeys", "_on_hotkeys_pressed"], ["Options", "_on_options_pressed"], ["Manual", "_on_manual_pressed"]]:
		main.toggle_diplomacy()
		await frames(2)
		var before: int = main.get_child_count()
		pm.call(b[1])
		await frames(2)
		check(open_names().is_empty(), b[0] + " closes Diplomacy: " + str(open_names()))
		for i in range(main.get_child_count() - 1, before - 1, -1):
			main.get_child(i).queue_free()
		await frames(2)
	main.toggle_chat()
	await frames(2)
	var q: Node = load("res://scenes/session/quit_confirmation.gd").resign(main)
	await frames(2)
	check(open_names().is_empty(), "the Resign question closes Chat: " + str(open_names()))
	q.queue_free()
	await frames(2)

	print("3. Chat lines")
	var log0: String = dlg()["chat_log"]
	check(not log0.contains("Press Enter") and not log0.contains("Hellenic League"), "no placeholder line")
	main.toggle_chat()
	await frames(2)
	var le := chat_input()
	check(le != null, "the chat input")
	if le:
		le.text = "hello there"
		main._on_chat_send_pressed()
		main.toggle_chat()
		await frames(2)
		le.text = "/me waves"
		main._on_chat_send_pressed()
		await frames(2)
	var log: String = dlg()["chat_log"]
	print("  log: ", log.strip_edges().replace("\n", " | "))
	var re := RegEx.create_from_string("<[^<>]+> hello there")
	check(re.search(log) != null, "your line reads <name> hello there")
	check(not log.contains("[All]") and not log.contains("Athenians:"), "without the old [All] Civ: prefix")
	check(RegEx.create_from_string("\\* \\S.* waves").search(log) != null, "/me reads * name waves")

	print("4. Chat commands")
	for cmd in ["/kick bob", "/banspecs", "/list"]:
		main.toggle_chat()
		await frames(2)
		le.text = cmd
		main._on_chat_send_pressed()
		await frames(2)
	log = dlg()["chat_log"]
	print("  log: ", log.strip_edges().replace("\n", " | "))
	check(log.contains("Offline game! Can not kick or ban."), "/kick: Offline game! Can not kick or ban.")
	check(log.contains("Offline game! No spectators to kick or ban."), "/banspecs: Offline game! No spectators to kick or ban.")
	check(RegEx.create_from_string("Users \\(1\\): \\S").search(log) != null, "/list: Users (1): <you>")
	check(not log.contains("> /kick") and not log.contains("> /list") and not log.contains("> /banspecs"),
		"the commands are not chat lines")
	print("5. Commands and addressees that reach nobody")
	main.toggle_chat()
	await frames(2)
	le.text = "/foo secret"
	main._on_chat_send_pressed()
	await frames(2)
	log = dlg()["chat_log"]
	check(log.contains("Unknown chat command: /foo") and not log.contains("> /foo") and not log.contains("secret"),
		"an unknown command is not sent")
	main.toggle_chat()
	await frames(2)
	le.text = "/msg nobody hello"
	main._on_chat_send_pressed()
	await frames(2)
	log = dlg()["chat_log"]
	check(log.contains("Unknown chat addressee: nobody hello") and not log.contains("> /msg") and not log.contains("> nobody"),
		"/msg to nobody known is not sent: " + log.strip_edges().get_slice("\n", log.strip_edges().get_slice_count("\n") - 1))
	main.toggle_chat()
	await frames(2)
	le.text = "unsent"
	main.toggle_chat()
	await frames(2)
	check(le.text == "", "closing empties the input")
	main.toggle_chat()
	await frames(2)
	main._on_chat_channel_btn_pressed(1)
	main.toggle_chat()
	await frames(2)
	main.chat_open_page()
	await frames(2)
	check(dlg()["chat"] and dlg()["chat_addressee"] == "Everyone", "the chat hotkey opens on Everyone: " + dlg()["chat_addressee"])

	main.toggle_chat()
	await frames(2)
	var overlay_before: int = dlg()["chat_overlay"]
	le.text = "/clear"
	main._on_chat_send_pressed()
	await frames(2)
	check(overlay_before > 0 and dlg()["chat_overlay"] == 0, "/clear empties the overlay")
	check(dlg()["chat_log"].contains("hello there"), "and keeps the history")

	print("6. History filter and the History toggle")
	main.toggle_chat()
	await frames(2)
	le.text = "/allies hold the line"
	main._on_chat_send_pressed()
	await frames(2)
	check(dlg()["chat_filters"] == 8, "eight filters offline, no Network notifications: %d" % dlg()["chat_filters"])
	main._on_chat_filter_selected(1) # Chat messages
	log = dlg()["chat_log"]
	check(log.contains("hello there") and log.contains("hold the line") and not log.contains("Offline game!"), "Chat messages leaves the notices out")
	main._on_chat_filter_selected(3) # Ally chat
	log = dlg()["chat_log"]
	check(log.contains("(Ally)") and log.contains("hold the line") and not log.contains("hello there"), "Ally chat shows only ally lines")
	main._on_chat_filter_selected(7) # Game notifications
	log = dlg()["chat_log"]
	check(log.contains("Offline game!") and not log.contains("hello there"), "Game notifications shows only the notices")
	main._on_chat_filter_selected(0)
	check(dlg()["chat_log"].contains("hello there") and dlg()["chat_log"].contains("Offline game!"), "Chat and notifications shows everything")
	check(dlg()["chat_extended"], "the history shows by default (chat.session.extended)")
	main._on_chat_history_toggled(false)
	check(not dlg()["chat_extended"], "unticking History hides it")
	main._on_chat_history_toggled(true)

	print("7. 0.28's chat dialog: centred, To / Text / Cancel / History / Send, 1024 characters")
	main.toggle_chat()
	await frames(2)
	check(le.max_length == 1024, "chatInput max_length 1024")
	var panel: Control = le.get_parent()
	while panel and not (panel is Panel):
		panel = panel.get_parent()
	var vp := root.get_visible_rect().size
	var rect := panel.get_global_rect()
	check(absf(rect.get_center().x - vp.x / 2.0) < 2.0 and absf(rect.size.x - 432.0) < 1.0, "a 432 px dialog in the middle: %s in %s" % [rect, vp])
	var texts := []
	for b in panel.find_children("*", "BaseButton", true, false):
		if b.is_visible_in_tree() and b.text != "":
			texts.append(b.text)
	check("Cancel" in texts and "Send" in texts and "History" in texts and not ("All" in texts or "Ally" in texts or "✕" in texts), "Cancel, History and Send only: %s" % [texts])
	main.toggle_chat()

	print("FAILURES: ", failures)
	quit(1 if failures else 0)
