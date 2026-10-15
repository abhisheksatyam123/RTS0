extends SceneTree
# The stance bar and selection icons as 0 A.D. 0.28 gui/session/selection_panels.js
# Stance and Selection panels show them:
#   1. the five stances in g_Stances order with their tooltips, shown only
#      while units are selected, each lit while a selected unit holds it;
#   2. pressing one sets it, and only it stays lit;
#   3. a right-click on a selection icon drops that group.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_stance_bar.gd
# Exit code 0 = pass.

const ORDER := ["Violent", "Aggressive", "Defensive", "Passive", "Standground"]

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

func stance_buttons() -> Array:
	var out := []
	for b in main.find_children("*", "Button", true, false):
		if ORDER.has(String(b.name)) and b.has_signal("pressed"):
			out.append(b)
	return out

func lit() -> Array:
	return stance_buttons().filter(func(b): return b.button_pressed).map(func(b): return String(b.name))

func shown() -> bool:
	var bs := stance_buttons()
	return bs.size() == 5 and bs[0].is_visible_in_tree()

func run() -> void:
	var mm := InputEventMouseMotion.new()
	mm.position = Vector2(32, 26)
	Input.parse_input_event(mm)
	await frames(60)

	print("1. The bar")
	var bs := stance_buttons()
	check(bs.map(func(b): return String(b.name)) == ORDER, "0.28's order: " + str(bs.map(func(b): return String(b.name))))
	check(bs.all(func(b): return b.text == "" and b.icon != null and b.custom_minimum_size == Vector2(38, 38)), "38 px icons without captions (stance_panel.xml)")
	check(bs.size() == 5 and bs[2].tooltip_text == "Defensive\nAttack nearby opponents, chase a short distance and return to the original location", "with 0.28's tooltips")
	main.clear_selection()
	await frames(3)
	check(not shown(), "hidden with nothing selected")
	main.select_first_own(true)
	await frames(3)
	check(not shown(), "hidden with a structure selected")
	main.debug_selection_grid(true)
	await frames(3)
	check(shown(), "shown with units selected")
	var l := lit()
	check(l.has("Aggressive") and l.has("Passive") and l.size() == 2, "soldiers' Aggressive and villagers' Passive are lit: " + str(l))

	print("2. Setting a stance")
	for b in stance_buttons():
		if b.name == "Defensive":
			b.emit_signal("pressed")
	await frames(20)
	check(lit() == ["Defensive"], "only Defensive is lit: " + str(lit()))

	print("3. Right-click drops a group")
	var g: Dictionary = main.debug_selection_grid(false)
	var before: int = g["selected"]
	var icon: Button = instance_from_id(g["icons"][0]["id"])
	var ev := InputEventMouseButton.new()
	ev.button_index = MOUSE_BUTTON_RIGHT
	ev.pressed = true
	icon.emit_signal("gui_input", ev)
	ev = ev.duplicate()
	ev.pressed = false
	icon.emit_signal("gui_input", ev)
	await frames(3)
	var after: int = main.debug_selection_grid(false)["selected"]
	check(after > 0 and after < before, "right-clicking the first icon drops it: %d -> %d" % [before, after])

	print("FAILURES: ", failures)
	quit(1 if failures else 0)
