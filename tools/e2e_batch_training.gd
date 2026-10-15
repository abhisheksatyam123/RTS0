extends SceneTree
# input.js batch training (addTrainingToQueue, flushTrainingBatch): with Shift held,
# clicks on one trainee build a single batch of N x the batch size, posted when Shift
# is released; Ctrl+Shift+click takes a batch back off; a plain click queues one unit.
# The cell's count and tooltip follow the count a click would train (getTrainingStatus):
# the Cost line is for that many, with the batch's shorter time, and "Shift-click to
# train N." shows only while Shift is held.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_batch_training.gd
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
	k.shift_pressed = Input.is_key_pressed(KEY_SHIFT) or (down and code == KEY_SHIFT)
	k.ctrl_pressed = Input.is_key_pressed(KEY_CTRL) or (down and code == KEY_CTRL)
	if not down and code == KEY_SHIFT:
		k.shift_pressed = false
	if not down and code == KEY_CTRL:
		k.ctrl_pressed = false
	k.pressed = down
	Input.parse_input_event(k)
	await frames(1)

func queued() -> Array:
	var out := []
	for i in 16:
		var s: Dictionary = main.debug_queue_slot(i)
		if s.get("visible", false) and not s.get("ghost", false):
			out.append(int(s["count"]))
	return out

func run() -> void:
	await frames(60)
	for c in ["i want pizza 5000", "bring me my axe 5000", "i see a mountain 5000", "your money or your life 5000"]:
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
		if not rl.is_item_disabled(i):
			pick = i
			break
	var before := queued()
	var re_cost := RegEx.create_from_string("Cost: \\D*?(\\d+).*?(\\d+) seconds?")

	print("0. The tooltip for one unit")
	var tip1 := TT.plain(rl.get_item_tooltip(pick))
	print("    ", tip1.replace("\n", " | "))
	var c1 := re_cost.search(tip1)
	check(c1 != null and not tip1.contains("Shift-click") and rl.get_item_text(pick) == "", "one unit's cost, no batch line, no count")

	print("1. Shift+clicks build one batch")
	await key(KEY_SHIFT, true)
	for i in 3:
		main._on_roster_item_clicked(pick, Vector2(), MOUSE_BUTTON_LEFT)
		await frames(2)
	var tip15 := TT.plain(rl.get_item_tooltip(pick))
	print("    ", tip15.replace("\n", " | "))
	var c15 := re_cost.search(tip15)
	check(rl.get_item_text(pick) == "20" and tip15.contains("Shift-click to train 20."), "three batches held: the next click trains 20")
	if c1 and c15:
		var t1 := int(c1.get_string(2))
		var t20 := int(c15.get_string(2))
		check(int(c15.get_string(1)) == 20 * int(c1.get_string(1)) and t20 > t1 and t20 < 20 * t1,
				"the cost for 20 (%s) and the batch's time (%d s, one unit %d s)" % [c15.get_string(1), t20, t1])
	check(queued() == before, "nothing is queued while Shift is held")
	await key(KEY_CTRL, true)
	main._on_roster_item_clicked(pick, Vector2(), MOUSE_BUTTON_LEFT)
	await key(KEY_CTRL, false)
	await frames(2)
	await key(KEY_SHIFT, false)
	await frames(10)
	var after := queued()
	print("    ", before, " -> ", after)
	check(after.size() == before.size() + 1 and after[-1] == 10, "releasing Shift queues one batch of 2 x 5 (three clicks, one taken back)")

	print("2. A plain click")
	main._on_roster_item_clicked(pick, Vector2(), MOUSE_BUTTON_LEFT)
	await frames(10)
	var plain := queued()
	print("    ", plain)
	check(plain.size() == after.size() + 1 and plain[-1] <= 1, "queues one unit at once")
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
