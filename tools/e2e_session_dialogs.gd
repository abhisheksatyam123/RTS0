extends SceneTree
# Session dialogs as 0 A.D. 0.28 gui/session shows them:
#   1. PauseOverlay: pausing shows "Game Paused" and "Click to Resume Game",
#      and a click on it resumes;
#   2. closeOpenDialogs: opening chat, diplomacy, Barter & Trade or match
#      settings closes the others, and Esc closes them all at once;
#   3. ChatMessageFormatPlayer: your line reads "<name> text", "(Ally) <name>
#      text" to allies and "* name text" with /me; no placeholder line.
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

	print("FAILURES: ", failures)
	quit(1 if failures else 0)
