extends SceneTree
# 0.28's session.pushorderfront (input.js pushFront; unbound by default, bound to J here):
#   1. a unit order given with it held runs first, and the order the unit was on waits
#      at the front of its queue (UnitAI PushOrderFront); without it the order replaces;
#   2. a batch trained with it held goes to the front of the production queue, and the
#      batch in progress waits behind it, paused (ProductionQueue.AddItem pushFront):
#      greyed with "This item is paused.".
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_push_front.gd
# Exit code 0 = pass.

var main
var failures := 0
var hk := RtsoHotkeyConfig.new()

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
	k.pressed = down
	Input.parse_input_event(k)
	await frames(1)

func move(pos: Vector2) -> void:
	var mm := InputEventMouseMotion.new()
	mm.position = root.get_final_transform() * pos
	mm.global_position = mm.position
	Input.parse_input_event(mm)
	await frames(2)

func right_click(pos: Vector2) -> void:
	await move(pos)
	for down in [true, false]:
		var mb := InputEventMouseButton.new()
		mb.button_index = MOUSE_BUTTON_RIGHT
		mb.pressed = down
		mb.position = pos
		mb.global_position = pos
		root.push_input(mb, true)
		await frames(1)
	await frames(10)

func unit() -> Dictionary:
	return main.debug_selection_state()["units"][0]

func run() -> void:
	await frames(60)
	await move(Vector2(512, 400))
	var r: Dictionary = main.debug_civic_centre_roof()
	for i in 200:
		await frames(1)
		var r2: Dictionary = main.debug_civic_centre_roof()
		var still: bool = (r2["roof"] as Vector2).distance_to(r["roof"]) < 0.5
		r = r2
		if still and i > 10:
			break
	hk.set_hotkey("session.pushorderfront", PackedStringArray(["J"]))
	var vp: Vector2 = root.get_visible_rect().size

	print("1. Unit orders")
	main.select_first_own(false)
	await frames(2)
	await right_click(Vector2(vp.x * 0.2, vp.y * 0.3))
	check(int(unit()["order_kind"]) != 0 and int(unit()["queued_orders"]) == 0, "a walk, nothing queued: %s" % unit()["order_kind"])
	await key(KEY_J, true)
	await right_click(Vector2(vp.x * 0.7, vp.y * 0.3))
	await key(KEY_J, false)
	var u := unit()
	check(int(u["queued_orders"]) == 1 and u.has("queue_front_kind"), "J: the new walk runs, the old one waits first in the queue: %d" % u["queued_orders"])
	var resumed := false
	for i in 600:
		await frames(1)
		u = unit()
		if int(u["queued_orders"]) == 0:
			resumed = int(u["order_kind"]) != 0
			break
	check(resumed, "once there, the old walk resumes")
	await key(KEY_J, true)
	await right_click(Vector2(vp.x * 0.7, vp.y * 0.3))
	await key(KEY_J, false)
	await right_click(Vector2(vp.x * 0.5, vp.y * 0.25))
	check(int(unit()["queued_orders"]) == 0, "a plain order clears the queue")

	print("2. Training")
	for c in ["i want pizza 5000", "bring me my axe 5000"]:
		main._on_chat_text_submitted(c)
	await frames(20)
	main.select_first_own(true)
	await frames(10)
	var rl: ItemList = null
	for n in main.find_children("*", "ItemList", true, false):
		if n.item_count > 0 and n.is_visible_in_tree():
			rl = n
	var picks: Array = []
	for i in rl.item_count:
		if not rl.is_item_disabled(i):
			picks.append(i)
	main._on_roster_item_clicked(picks[0], Vector2(), MOUSE_BUTTON_LEFT)
	await frames(40) # the batch starts and makes some progress
	var first: String = str(main.debug_queue_slot(0)["tooltip"]).get_slice("\n", 0)
	await key(KEY_J, true)
	main._on_roster_item_clicked(picks[1], Vector2(), MOUSE_BUTTON_LEFT)
	await key(KEY_J, false)
	await frames(6)
	var s0: Dictionary = main.debug_queue_slot(0)
	var s1: Dictionary = main.debug_queue_slot(1)
	print("    ", str(s0["tooltip"]).get_slice("\n", 0), " | ", str(s1["tooltip"]).replace("\n", " / "))
	check(str(s1["tooltip"]).get_slice("\n", 0) == first and str(s0["tooltip"]).get_slice("\n", 0) != first, "the new batch goes first")
	check(s1["paused"] and str(s1["tooltip"]).contains("This item is paused.") and not s0["paused"], "the started one waits, paused")

	hk.reload()
	print("FAILURES: ", failures)
	quit(1 if failures else 0)
