extends SceneTree
# A tooltip shown when the Summary opens over the match (Ctrl+Tab or the menu's
# Summary) goes: the hovered top-panel button is under the Summary now, so its
# "[Ctrl+B] Barter & Trade" doesn't stay drawn over the Summary's title, whether
# the mouse stays put or moves.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_summary_tooltip.gd
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

func move(pos: Vector2) -> void:
	var mm := InputEventMouseMotion.new()
	mm.position = root.get_final_transform() * pos
	mm.global_position = mm.position
	Input.parse_input_event(mm)
	Input.flush_buffered_events()
	await frames(3)

func find_tip(n: Node, part: String) -> Control:
	if n is Control and (n as Control).is_visible_in_tree() and str((n as Control).tooltip_text).contains(part):
		return n
	for c in n.get_children():
		var f := find_tip(c, part)
		if f:
			return f
	return null

func run() -> void:
	await frames(60)
	var tips = root.get_node("Tooltips")
	var barter := find_tip(main, "Barter")
	if barter == null and main.has_method("_refresh_menu_tooltip"):
		main._refresh_menu_tooltip()
		barter = find_tip(main, "Barter")
	check(barter != null, "the Barter & Trade button")
	if barter == null:
		quit(1)
		return
	var at := barter.get_global_rect().get_center()
	await move(at)
	await frames(10)
	check(tips.shown_text().contains("Barter"), "its tooltip shows: " + tips.shown_text())
	main.toggle_summary()
	await frames(10)
	check(tips.shown_text() == "", "the Summary opening clears it: '%s'" % tips.shown_text())
	await move(at + Vector2(3, 2))
	await frames(10)
	check(not tips.shown_text().contains("Barter"), "and moving over the Summary doesn't bring it back: '%s'" % tips.shown_text())
	main.toggle_summary()
	await frames(5)
	print("FAILURES: ", failures)
	quit(1 if failures else 0)
