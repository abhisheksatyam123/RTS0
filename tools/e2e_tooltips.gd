extends SceneTree
# End-to-end check of the Tooltips layer (0 A.D. 0.28 gui/common/setup.xml
# snToolTip: shown 500 ms after the cursor rests on a control, offset 16 32,
# at most 400 px wide):
#   1. a main menu submenu button shows its tooltip at once in the fixed
#      top-right panel (pregame/menupanel.xml pgToolTip), hidden on leaving;
#   2. moving off it hides the tooltip;
#   3. a Game Setup control's tooltip shows the same way, kept on screen.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_tooltips.gd
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

func move_to(p: Vector2) -> void:
	var ev := InputEventMouseMotion.new()
	ev.position = root.get_final_transform() * p
	ev.global_position = ev.position
	Input.parse_input_event(ev)
	Input.flush_buffered_events()

func find_tipped(n: Node, want: String = "") -> Control:
	if n is Control and n.is_visible_in_tree() and n.mouse_filter != Control.MOUSE_FILTER_IGNORE \
			and not n.tooltip_text.is_empty() and (want.is_empty() or n.tooltip_text == want):
		return n
	for c in n.get_children():
		var f := find_tipped(c, want)
		if f:
			return f
	return null

func tips() -> Node:
	return root.get_node("Tooltips")

func hover_check(c: Control, label: String) -> void:
	var tip: String = c.tooltip_text
	move_to(c.get_global_rect().get_center())
	await frames(4)
	check(tips().shown_text() == "", label + ": nothing shown before the 500 ms delay")
	await frames(14)
	check(tips().shown_text() == tip, label + ": tooltip shown after the delay (\"%s\")" % tip.left(40))
	var panel: Control = tips().get_node("Tooltip")
	var screen := root.get_visible_rect().size
	check(panel.get_global_rect().size.x <= 400.0 + 12.0, label + ": at most 400 px wide")
	check(Rect2(Vector2.ZERO, screen).encloses(panel.get_global_rect()), label + ": kept on screen")
	move_to(Vector2(2, 2))
	await frames(3)
	check(tips().shown_text() == "", label + ": hidden after the cursor leaves")

## pgToolTip: no delay, white text in the TranslucentPanel at "100%-313 25 100% 200".
func pg_check(c: Control, label: String) -> void:
	var tip: String = c.tooltip_text
	move_to(c.get_global_rect().get_center())
	await frames(2)
	check(tips().shown_text() == tip, label + ": shown at once (\"%s\")" % tip.left(40))
	var panel: Control = tips().get_node("Tooltip")
	var screen := root.get_visible_rect().size
	check(panel.position.is_equal_approx(Vector2(screen.x - 313, 25)) and is_equal_approx(panel.size.x, 313.0),
			label + ": in the fixed top-right panel (%s)" % panel.get_global_rect())
	var bg: Color = (panel.get_theme_stylebox("panel") as StyleBoxFlat).bg_color
	check(bg.a < 0.5 and tips()._label.get_theme_color("default_color") == Color.WHITE, label + ": white on the translucent panel")
	move_to(Vector2(2, 2))
	await frames(3)
	check(tips().shown_text() == "", label + ": hidden after the cursor leaves")

func run() -> void:
	check(root.has_node("Tooltips"), "Tooltips autoload is registered")
	change_scene_to_file("res://scenes/menu/main_menu.tscn")
	await frames(10)
	current_scene.get_node("MainPanel/Buttons/BtnSingle").pressed.emit()
	await frames(4)
	var b := find_tipped(current_scene, "Start a new single-player game.")
	check(b != null, "Single-player submenu has Matches with its tooltip")
	if b:
		await pg_check(b, "Matches")
	change_scene_to_file("res://scenes/menu/gamesetup.tscn")
	await frames(10)
	var m := find_tipped(current_scene, "Select a map to play on.")
	check(m != null, "Game Setup has the map selector tooltip")
	if m:
		await hover_check(m, "Game Setup map selector")
	print("PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
