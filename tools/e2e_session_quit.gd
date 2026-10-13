extends SceneTree
# Leaving a match and its summary as 0 A.D. 0.28 gui/session does:
#   1. the summary hotkey (Ctrl+Tab) closes the open dialogs before it opens
#      the summary (MenuButtons Summary.onPress: closeOpenDialogs);
#   2. quitting without the summary returns to the lobby while signed in to it
#      (session.js endGame: Engine.HasXmppClient), else to the main menu.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_session_quit.gd
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

func key(code: Key, pressed: bool, ctrl := false) -> void:
	var k := InputEventKey.new()
	k.keycode = code
	k.physical_keycode = code
	k.pressed = pressed
	k.ctrl_pressed = ctrl
	Input.parse_input_event(k)

func run() -> void:
	var mm := InputEventMouseMotion.new()
	mm.position = Vector2(32, 26)
	Input.parse_input_event(mm)
	await frames(60)

	print("1. Summary hotkey")
	main.toggle_chat()
	await frames(2)
	check(main.debug_dialogs()["chat"], "chat is open")
	key(KEY_CTRL, true, true)
	key(KEY_TAB, true, true)
	await frames(1)
	key(KEY_TAB, false, true)
	key(KEY_CTRL, false)
	await frames(3)
	var summary: Node = main.get_node_or_null("SummaryScreen")
	check(not main.debug_dialogs()["chat"], "Ctrl+Tab closes the chat")
	check(summary != null and summary.is_open(), "and opens the summary")
	if summary and summary.is_open():
		summary.continue_pressed()
	await frames(2)

	print("2. Quit to the lobby")
	var lobby: Node = root.get_node("LobbySession")
	var token: String = lobby.token
	lobby.token = "e2e"
	load("res://scenes/session/quit_confirmation.gd").end_game(main, false)
	await frames(20)
	var scene := current_scene.scene_file_path if current_scene else ""
	lobby.token = token
	check(scene.ends_with("lobby.tscn"), "signed in, Quit goes back to the lobby: " + scene)

	print("FAILURES: ", failures)
	quit(1 if failures else 0)
