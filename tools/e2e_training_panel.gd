extends SceneTree
# 0.28's Training panel (selection_panels.js Training setupButton) on a Civic Center:
#   1. portraits, the first tooltip line led by the queue hotkey ("[Z] ...");
#   2. with no food a trainee is masked red and its tooltip says what is missing;
#   3. while Shift is held each portrait shows the batch a click would train (5).
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_training_panel.gd
# Exit code 0 = pass.

const RES_FOOD := 3

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

func key(code: int, down: bool) -> void:
	var k := InputEventKey.new()
	k.keycode = code
	k.physical_keycode = code
	k.shift_pressed = down and code == KEY_SHIFT
	k.pressed = down
	Input.parse_input_event(k)
	await frames(1)

func roster() -> ItemList:
	for l in main.find_children("*", "ItemList", true, false):
		if l.is_visible_in_tree() and l.item_count > 0:
			return l
	return null

func run() -> void:
	await frames(60)
	main.select_first_own(true)
	await frames(6)
	var rl := roster()
	check(rl != null and rl.get_item_icon(0) != null and rl.get_item_text(0) == "", "portraits without text")
	if rl == null:
		quit(1)
		return
	print("    ", TT.plain(rl.get_item_tooltip(0)).get_slice("\n", 0))
	check(TT.plain(rl.get_item_tooltip(0)).begins_with("[Z] "), "the first line starts with the hotkey")

	print("2. No food")
	var women := -1
	for i in rl.item_count:
		if TT.plain(rl.get_item_tooltip(i)).contains("Food") and not rl.is_item_disabled(i):
			women = i
			break
	main.debug_set_resource(RES_FOOD, 0)
	await frames(3)
	print("    ", TT.plain(rl.get_item_tooltip(women)).get_slice("\n", TT.plain(rl.get_item_tooltip(women)).get_slice_count("\n") - 1))
	check(women >= 0 and rl.get_item_icon_modulate(women).g < 0.5 and rl.get_item_icon_modulate(women).r > 0.9, "masked red")
	check(TT.plain(rl.get_item_tooltip(women)).contains("Insufficient resources"), "the tooltip lists what is missing")
	main.debug_set_resource(RES_FOOD, 5000)
	await frames(3)
	check(rl.get_item_icon_modulate(women) == Color(1, 1, 1), "back to normal once affordable")

	print("3. Shift held")
	await key(KEY_SHIFT, true)
	await frames(3)
	check(rl.get_item_text(women) == "5", "shows the batch of 5: '%s'" % rl.get_item_text(women))
	await key(KEY_SHIFT, false)
	await frames(3)
	check(rl.get_item_text(women) == "", "gone when Shift is up")
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
