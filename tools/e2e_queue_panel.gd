extends SceneTree
# 0.28's production queue panel (selection_panels.js g_SelectionPanels.Queue,
# selection_panels_right/queue_panel.xml):
#   1. the production icon shows the first item's time left (m:ss), each item its
#      names, and a batch its size;
#   2. a batch waiting for population room says how much it lacks
#      ("Insufficient population capacity:");
#   3. with autoqueue on, ghost repeats of the queued units fill the 16 slots,
#      "The auto-queue will try to train this item later.", and a click on one
#      removes nothing;
#   4. a right click opens the Template Viewer, a left click removes the item.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_queue_panel.gd
# Exit code 0 = pass.

const TT = preload("res://scenes/common/tooltips.gd")
var main
var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	root.get_node("MatchSettings").cheats = true
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func key(code: int, down: bool) -> void:
	var k := InputEventKey.new()
	k.keycode = code
	k.physical_keycode = code
	k.shift_pressed = down and code == KEY_SHIFT
	k.pressed = down
	Input.parse_input_event(k)
	await frames(1)

func button(index: int) -> InputEventMouseButton:
	var mb := InputEventMouseButton.new()
	mb.button_index = index
	mb.pressed = true
	return mb

func run() -> void:
	await frames(60)
	# Cheats.js codes typed in the chat.
	for c in ["i want pizza 5000", "bring me my axe 5000"]:
		main.toggle_chat()
		await frames(2)
		var le := root.gui_get_focus_owner() as LineEdit
		if le:
			le.text = c
		main._on_chat_text_submitted(c)
	await frames(20)
	main.select_first_own(true)
	await frames(10)

	var rl: ItemList = null
	for n in main.find_children("*", "ItemList", true, false):
		if n.item_count > 0:
			rl = n
	var pick := 0
	for i in rl.item_count:
		if not rl.is_item_disabled(i) and rl.get_item_tooltip(i).contains("Artisan"):
			pick = i
	print("1. A batch bigger than the population room")
	await key(KEY_SHIFT, true)
	for i in 20:
		main._on_roster_gui_input(button(MOUSE_BUTTON_WHEEL_UP))
	main._on_roster_item_clicked(pick, Vector2(), MOUSE_BUTTON_LEFT)
	await key(KEY_SHIFT, false)
	await frames(20)
	var s: Dictionary = main.debug_queue_slot(0)
	check(s.get("visible", false) and int(s["count"]) > 20, "the batch shows its size: " + str(s.get("count")))
	check(str(s["time"]).count(":") == 1 and str(s["time"]).split(":")[1].length() == 2, "the production icon shows m:ss: " + str(s["time"]))
	check(str(s["tooltip"]).contains("[color=red][b]Insufficient population capacity:[/b][/color]\n[img") and TT.plain(str(s["tooltip"])).contains("Insufficient population capacity:\n"), "it lacks population room (objectionFont, the population icon)")
	check(str(s["tooltip"]).ends_with("Right-click for more information.") and s["kind"] == "unit", "names, then the right-click line")
	check(not main.debug_queue_slot(1)["visible"], "one item, one button")

	print("2. Autoqueue ghosts")
	main._on_autoqueue_pressed(1)
	await frames(20)
	var g: Dictionary = main.debug_queue_slot(15)
	check(g["visible"] and g["ghost"] and str(g["tooltip"]).contains("The auto-queue will try to train this item later."), "ghosts fill the 16 slots")
	check(not main.debug_queue_slot(0)["ghost"], "the real item comes first")
	main._on_queue_slot_gui_input(button(MOUSE_BUTTON_LEFT), 15)
	await frames(20)
	check(main.debug_queue_slot(0)["visible"] and not main.debug_queue_slot(0)["ghost"], "a click on a ghost removes nothing")

	print("3. Clicks")
	main._on_queue_slot_gui_input(button(MOUSE_BUTTON_RIGHT), 0)
	await frames(2)
	var viewer: Node = main.find_child("TemplateViewer", true, false)
	check(viewer != null, "a right click opens the Template Viewer")
	if viewer:
		viewer.queue_free()
	main._on_autoqueue_pressed(0)
	main._on_queue_slot_gui_input(button(MOUSE_BUTTON_LEFT), 0)
	await frames(20)
	check(not main.debug_queue_slot(0)["visible"], "a left click removes the item")
	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
