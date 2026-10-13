extends SceneTree
# A real click on a command-panel button: the mouse goes down on the House
# button of a selected worker, stays down for a few frames while the panel
# refreshes, and comes up; the click has to start build placement. The panel
# used to replace its buttons every frame, so the press and the release hit
# different Buttons and nothing fired. A click on a selection panel group icon
# keeps only that group.
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
	main.enter_build_mode(false)

	await grid_test()
	await roof_test()
	print("FAILURES: ", failures)
	quit(1 if failures else 0)

func grid_test() -> void:
	# The selection panel: clicking a group icon keeps only that group.
	var g: Dictionary = main.debug_selection_grid(true)
	await frames(4)
	g = main.debug_selection_grid(false)
	var icons: Array = g["icons"]
	check(icons.size() >= 2, "selecting every starting unit shows %d group icons" % icons.size())
	if icons.size() >= 2:
		var before: int = g["selected"]
		var first: Dictionary = icons[0]
		var pos: Vector2 = root.get_final_transform() * (first["rect"] as Rect2).get_center()
		mouse(pos)
		await frames(2)
		mouse(pos, 1)
		await frames(3)
		mouse(pos, 0)
		await frames(3)
		var after: int = main.debug_selection_grid(false)["selected"]
		check(after > 0 and after < before, "clicking the first icon narrows %d units to %d (%s)" % [before, after, str(first["tip"]).get_slice("\n", 0)])

func roof_test() -> void:
	# A click on the Civic Centre's roof selects it (not the ground behind).
	main.debug_selection_grid(false)
	var r: Dictionary = main.debug_civic_centre_roof()
	# The camera glides to the Civic Centre; wait until the roof stops moving.
	for i in 200:
		await frames(1)
		var r2: Dictionary = main.debug_civic_centre_roof()
		var still: bool = (r2["roof"] as Vector2).distance_to(r["roof"]) < 0.5
		r = r2
		if still:
			break
	var roof: Vector2 = r["roof"]
	var g3: Vector3 = r["ground_under_roof"]
	var off := Vector2(g3.x, g3.z).distance_to(r["centre"])
	check(off > float(r["base_radius"]), "the ground under the roof point (%.1f up) is %.1f from the centre, outside the base test's %.1f" % [r["height"] * 0.8, off, r["base_radius"]])
	var rp: Vector2 = root.get_final_transform() * roof
	mouse(rp)
	await frames(2)
	mouse(rp, 1)
	await frames(2)
	mouse(rp, 0)
	await frames(3)
	check(main.debug_civic_centre_roof()["selected_building"] == r["id"], "clicking the Civic Centre's roof selects it")

