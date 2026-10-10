extends SceneTree
# A real click on a command-panel button: the mouse goes down on the House
# button of a selected worker, stays down for a few frames while the panel
# refreshes, and comes up; the click has to start build placement. The panel
# used to replace its buttons every frame, so the press and the release hit
# different Buttons and nothing fired.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_command_panel.gd
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

func mouse(pos: Vector2, pressed := -1) -> void:
	var mm := InputEventMouseMotion.new()
	mm.position = pos
	mm.global_position = pos
	Input.parse_input_event(mm)
	if pressed >= 0:
		var mb := InputEventMouseButton.new()
		mb.button_index = MOUSE_BUTTON_LEFT
		mb.pressed = pressed == 1
		mb.position = pos
		mb.global_position = pos
		Input.parse_input_event(mb)

func run() -> void:
	await frames(60)
	main.debug_command_button("House")
	await frames(5)
	var a: Dictionary = main.debug_command_button("House")
	check(a.has("id"), "a selected worker shows a House button")
	if not a.has("id"):
		quit(1)
		return
	check(not a["disabled"], "the House button is enabled")
	await frames(3)
	var b: Dictionary = main.debug_command_button("House")
	check(a["id"] == b["id"], "the House button is the same Button from frame to frame")
	# Events are in window pixels; the headless window is tiny and the
	# canvas_items stretch scales the 1280-wide UI into it.
	var at: Vector2 = root.get_final_transform() * (b["rect"] as Rect2).get_center()
	mouse(at)
	await frames(2)
	var hov = root.gui_get_hovered_control()
	print("  rect ", b["rect"], " hovered ", hov, " ", hov.get_class() if hov else "", " ", hov.text if hov is Button else "")
	mouse(at, 1)
	await frames(4)
	check(not main.debug_command_button("House")["build_mode"], "nothing happens while the button is held")
	mouse(at, 0)
	await frames(2)
	check(main.debug_command_button("House")["build_mode"], "releasing the click starts House placement")
	print("FAILURES: ", failures)
	quit(1 if failures else 0)
