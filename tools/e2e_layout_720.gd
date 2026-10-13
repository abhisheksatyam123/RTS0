extends SceneTree
# Game Setup at 1280x720: Back and Start Game stay on screen (the options
# column scrolls), and each player row's civ dropdown keeps its emblem small.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_layout_720.gd
# Exit code 0 = pass.

var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func run() -> void:
	# A 1280x720 box stands in for the window (the headless window is square).
	var frame := Control.new()
	frame.size = Vector2(1280, 720)
	root.add_child(frame)
	var page: Control = load("res://scenes/menu/gamesetup.tscn").instantiate()
	frame.add_child(page)
	page.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for i in 10:
		await process_frame
	var screen := Rect2(Vector2.ZERO, Vector2(1280, 720))
	var back: Control = page.get_node("VBox/Header/BtnBack")
	var start: Control = page.get_node("VBox/HBox/Right/BtnStart")
	check(screen.encloses(back.get_global_rect()), "Back is on screen at %s" % back.get_global_rect())
	check(screen.encloses(start.get_global_rect()), "Start Game is on screen at %s" % start.get_global_rect())
	var grid: Control = page.get_node("VBox/HBox/Right/SlotsBox/SlotsGrid")
	var tallest := 0.0
	for c in grid.find_children("*", "RtsoSafeSelect", true, false):
		tallest = maxf(tallest, (c as Control).size.y)
	check(tallest > 0.0 and tallest <= 40.0, "player row dropdowns stay one line high, emblems included (tallest %.0f px)" % tallest)
	print("FAILURES: ", failures)
	quit(1 if failures else 0)
