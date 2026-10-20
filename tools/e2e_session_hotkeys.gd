extends SceneTree
# 0.28 session hotkeys RTS0 had in its table but never acted on:
#   1. session.move (unit_actions.js "move"): a right-click on a tree walks the
#      women there instead of sending them to gather;
#   2. session.autorallypoint (Ctrl): a right-click on the selected Civic
#      Center sets its rally point on itself (its units go inside), where a
#      plain right-click there drops the rally point;
#   3. session.queueunit.autoqueueon / autoqueueoff (training.xml) turn the
#      selected structure's autoqueue on and off.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_session_hotkeys.gd
# Exit code 0 = pass.

var main
var failures := 0
var hk := RtsoHotkeyConfig.new()

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

func key(code: int, down: bool, ctrl := false, shift := false) -> void:
	var k := InputEventKey.new()
	k.keycode = code
	k.physical_keycode = code
	k.ctrl_pressed = ctrl or code == KEY_CTRL
	k.shift_pressed = shift or code == KEY_SHIFT
	k.pressed = down
	Input.parse_input_event(k)
	await frames(1)

# As e2e_selection_clicks: motion through Input in window pixels, buttons to
# the viewport in its own.
func move(pos: Vector2) -> void:
	var mm := InputEventMouseMotion.new()
	mm.position = root.get_final_transform() * pos
	mm.global_position = mm.position
	Input.parse_input_event(mm)

func right_click(pos: Vector2) -> void:
	move(pos)
	await frames(2)
	for down in [true, false]:
		var mb := InputEventMouseButton.new()
		mb.button_index = MOUSE_BUTTON_RIGHT
		mb.pressed = down
		mb.position = pos
		mb.global_position = pos
		root.push_input(mb, true)
		await frames(1)
	await frames(2)

func run() -> void:
	await frames(60)
	move(Vector2(512, 400))
	# The camera glides to the Civic Centre; wait until it stops.
	var r: Dictionary = main.debug_civic_centre_roof()
	for i in 200:
		await frames(1)
		var r2: Dictionary = main.debug_civic_centre_roof()
		var still: bool = (r2["roof"] as Vector2).distance_to(r["roof"]) < 0.5
		r = r2
		if still and i > 10:
			break

	print("1. session.move")
	hk.set_hotkey("session.move", PackedStringArray(["J"]))
	main.select_first_own(false)
	var st: Dictionary = main.debug_selection_state()
	var tree_pos: Vector2 = st["nearest_resource_screen"]
	print("    tree at ", tree_pos, " viewport ", st["viewport"], " unit ", st["units"][0]["screen"])
	await key(KEY_J, true)
	await right_click(tree_pos)
	await key(KEY_J, false)
	st = main.debug_selection_state()
	check(int(st["units"][0]["target_resource"]) == 0, "J held: she walks there and gathers nothing")
	await right_click(tree_pos)
	# The order lands on a later lockstep turn: wait for turns, not frames.
	for i in 40:
		st = main.debug_selection_state()
		if int(st["units"][0]["target_resource"]) != 0:
			break
		await frames(1)
	check(int(st["units"][0]["target_resource"]) == int(st["nearest_resource"]), "without it: she goes to gather")

	print("2. session.autorallypoint")
	main.select_first_own(true)
	var b: Dictionary = main.debug_selected_building()
	print("    building ", b)
	await key(KEY_CTRL, true)
	await right_click(b["screen"])
	await key(KEY_CTRL, false)
	b = main.debug_selected_building()
	check(b["has_rally"] and b["rally_self"], "Ctrl+right-click on the Civic Center: its rally point on itself")
	await right_click(b["screen"])
	check(not main.debug_selected_building()["has_rally"], "a plain right-click there drops it")

	print("3. autoqueue")
	hk.set_hotkey("session.queueunit.autoqueueon", PackedStringArray(["Ctrl+J"]))
	hk.set_hotkey("session.queueunit.autoqueueoff", PackedStringArray(["Shift+J"]))
	await key(KEY_CTRL, true)
	await key(KEY_J, true, true)
	await key(KEY_J, false, true)
	await key(KEY_CTRL, false)
	await frames(2)
	check(main.debug_selected_building()["autoqueue"], "Ctrl+J turns autoqueue on")
	await key(KEY_SHIFT, true)
	await key(KEY_J, true, false, true)
	await key(KEY_J, false, false, true)
	await key(KEY_SHIFT, false)
	await frames(2)
	check(not main.debug_selected_building()["autoqueue"], "Shift+J turns it off")
	hk.reload()
	print("RESULT: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
	quit(0 if failures == 0 else 1)
