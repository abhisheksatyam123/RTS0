extends SceneTree
# The session panels Rendering's 2026-10-13 mouse playtest found wrong, as
# 0 A.D. 0.28 gui/session shows them:
#   1. units that die leave the selection (EntitySelection.update), so the
#      "N units selected" header follows the battle;
#   2. the Formation panel is a row of icons that fits the panel;
#   3. the command row is icons, and the Construction panel is an icon grid
#      the panel shows whole, Fortress and Arsenal included, with the name and
#      cost on each tooltip.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_playtest_panels.gd
# Exit code 0 = pass.

const TT = preload("res://scenes/common/tooltips.gd")

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

func header() -> String:
	for l in main.find_children("*", "Label", true, false):
		if l.is_visible_in_tree() and l.text.ends_with(" units selected"):
			return l.text
	return ""

func calls(b: Button, method: String) -> bool:
	for c in b.get_signal_connection_list("pressed"):
		if c["callable"].get_method() == method:
			return true
	return false

# The button shows its icon, or its icon can't load in this checkout (an
# unimported project); either way it carries no text but a count.
func icon_only(b: Button) -> bool:
	return b.icon != null and not b.text.lstrip("0123456789").length()

func run() -> void:
	var mm := InputEventMouseMotion.new()
	mm.position = Vector2(32, 26)
	Input.parse_input_event(mm)
	await frames(60)
	var imported := ResourceLoader.exists("res://assets/textures/ui/portraits/house.png")
	print("  icons load: ", imported)

	print("1. Deaths leave the selection")
	main.debug_selection_grid(true)
	await frames(3)
	var sel: Array = main.debug_selection_state()["selected"]
	check(sel.size() >= 6 and header() == "%d units selected" % sel.size(), "%d units selected: %s" % [sel.size(), header()])
	main.debug_delete_units(PackedInt32Array(sel.slice(0, 3)))
	await frames(10)
	var now: Array = main.debug_selection_state()["selected"]
	check(now.size() == sel.size() - 3 and not now.has(sel[0]), "the 3 dead leave it: %d" % now.size())
	check(header() == "%d units selected" % now.size(), "the header follows: " + header())

	print("2. Formation row")
	var forms: Array = main.find_children("*", "Button", true, false).filter(func(b): return b.is_visible_in_tree() and calls(b, "_on_formation_selected"))
	check(forms.size() >= 2, "%d formation buttons" % forms.size())
	if imported:
		check(forms.all(func(b): return icon_only(b)), "icons without clipped letters: " + str(forms.map(func(b): return b.text)))
	# Without imported icons (a fresh checkout) the buttons fall back to text and
	# the fit checks below don't apply.
	if forms.size() > 0 and imported:
		var row: Control = forms[0].get_parent()
		var panel: Control = row.get_parent()
		var right: float = forms.map(func(b): return b.get_global_rect().end.x).max()
		check(right <= panel.get_global_rect().end.x, "the row ends inside the panel: %.0f <= %.0f" % [right, panel.get_global_rect().end.x])
		check(forms.all(func(b): return b.tooltip_text.length() > 0), "names on the tooltips")

	print("3. Command row and construction grid")
	main.debug_selection_grid(true)
	await frames(2)
	var g: Dictionary = main.debug_selection_grid(false)
	instance_from_id(g["icons"][0]["id"]).emit_signal("pressed")  # the villagers
	await frames(4)
	var row: Array = main.debug_command_row()
	var builds: Array = []
	var cmds: Array = []
	for e in row:
		var b: Button = instance_from_id(e["id"])
		if calls(b, "_on_build_btn_pressed"):
			builds.append(b)
		elif calls(b, "_on_entity_command"):
			cmds.append(b)
	var names: Array = row.map(func(e): return str(e["text"]).get_slice(" (", 0))
	print("  row: ", names)
	check(builds.size() >= 20 and names.has("Fortress") and names.has("Arsenal"), "%d structures, Fortress and Arsenal among them" % builds.size())
	if imported:
		check(cmds.all(func(b): return icon_only(b)), "the commands are icons: " + str(cmds.map(func(b): return b.text)))
		var texty: Array = builds.filter(func(b): return not icon_only(b))
		check(texty.is_empty(), "every structure is an icon: " + str(texty.map(func(b): return b.text)))
	check(builds.all(func(b): return TT.plain(b.tooltip_text).contains("\nCost: ") or b.tooltip_text.begins_with("Field")), "name and cost on the tooltips")
	await frames(4)
	var scroll: ScrollContainer = builds[0].get_parent().get_parent() if builds.size() > 0 else null
	if scroll and imported:
		var view: Rect2 = scroll.get_global_rect()
		var below: Array = builds.filter(func(b): return b.get_global_rect().end.y > view.end.y + 0.5)
		check(below.is_empty(), "none below the fold: " + str(below.map(func(b): return str(b.get_meta("label", b.text)))))
		check(view.position.y >= 0.0, "the panel stays on screen: top %.0f" % view.position.y)

	print("FAILURES: ", failures)
	quit(1 if failures else 0)
