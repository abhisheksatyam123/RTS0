extends SceneTree
# Touch input for Android and iOS (scenes/common/touch_input.gd): touches become the
# mouse events the session reads.
#   tap -> left click, second quick tap -> double click, drag -> left drag,
#   hold still -> right click, two fingers -> pan and magnify gestures and no clicks,
#   a latched Shift/Ctrl/Alt -> Input reports the key held and the clicks carry it.
#
#   godot --headless --path . --fixed-fps 60 -s tools/e2e_touch_input.gd
# Exit code 0 = pass.

class Recorder extends Node:
	var events: Array = []
	func _input(e: InputEvent) -> void:
		if e is InputEventMouseButton or e is InputEventMagnifyGesture or e is InputEventPanGesture:
			events.append(e)

var failures := 0
var touch: Node
var rec: Recorder

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func finger(index: int, pos: Vector2, pressed: bool) -> void:
	var t := InputEventScreenTouch.new()
	t.index = index
	t.position = pos
	t.pressed = pressed
	Input.parse_input_event(t)
	await frames(2)

func slide(index: int, pos: Vector2) -> void:
	var d := InputEventScreenDrag.new()
	d.index = index
	d.position = pos
	Input.parse_input_event(d)
	await frames(2)

# Where the session sees a touch made at window point p (events are delivered in
# viewport coordinates, after the stretch transform).
func vp(p: Vector2) -> Vector2:
	return root.get_final_transform().affine_inverse() * p

func near(a: Vector2, b: Vector2) -> bool:
	return a.distance_to(b) < 0.01 * maxf(1.0, b.length())

func buttons(idx: int) -> Array:
	return rec.events.filter(func(e): return e is InputEventMouseButton and e.button_index == idx)

func run() -> void:
	touch = root.get_node_or_null("TouchInput")  # the autoload, when -s loads autoloads
	if touch == null:
		touch = load("res://scenes/common/touch_input.gd").new()
		touch.name = "TouchInput"
		root.add_child(touch)
	touch.activate()
	rec = Recorder.new()
	root.add_child(rec)
	await frames(2)
	check(not Input.emulate_mouse_from_touch, "Godot's left-only touch emulation is off")

	# Tap.
	await finger(0, Vector2(300, 200), true)
	await finger(0, Vector2(300, 200), false)
	var left := buttons(MOUSE_BUTTON_LEFT)
	check(left.size() == 2 and left[0].pressed and not left[1].pressed and near(left[0].position, vp(Vector2(300, 200))), "a tap is a left press and release where the finger was")
	check(buttons(MOUSE_BUTTON_RIGHT).is_empty(), "a tap gives no right click")

	# A second tap close by soon after is a double click.
	rec.events.clear()
	await finger(0, Vector2(304, 202), true)
	await finger(0, Vector2(304, 202), false)
	left = buttons(MOUSE_BUTTON_LEFT)
	check(left.size() == 2 and left[0].double_click, "a quick second tap is a double click")

	# Drag: left press at the start, release at the end.
	rec.events.clear()
	await finger(0, Vector2(100, 100), true)
	await slide(0, Vector2(160, 150))
	await slide(0, Vector2(260, 220))
	await finger(0, Vector2(260, 220), false)
	left = buttons(MOUSE_BUTTON_LEFT)
	check(left.size() == 2 and left[0].pressed and near(left[0].position, vp(Vector2(100, 100))) and not left[1].pressed and near(left[1].position, vp(Vector2(260, 220))), "a drag is a left drag from start to end (box select)")

	# Hold still: one right click, and no left click on lift.
	rec.events.clear()
	await finger(0, Vector2(400, 300), true)
	var t0 := Time.get_ticks_msec()
	while buttons(MOUSE_BUTTON_RIGHT).is_empty() and Time.get_ticks_msec() - t0 < 3000:
		await process_frame
	await finger(0, Vector2(400, 300), false)
	var right := buttons(MOUSE_BUTTON_RIGHT)
	check(right.size() == 2 and right[0].pressed and near(right[0].position, vp(Vector2(400, 300))), "holding still is a right click there (orders)")
	check(Time.get_ticks_msec() - t0 >= 400, "the right click waits for the hold")
	check(buttons(MOUSE_BUTTON_LEFT).is_empty(), "lifting after a hold gives no left click")

	# Two fingers: spread apart and move together -> magnify + pan, no clicks.
	rec.events.clear()
	await finger(0, Vector2(300, 300), true)
	await finger(1, Vector2(400, 300), true)
	await slide(0, Vector2(260, 280))
	await slide(1, Vector2(460, 280))
	await finger(1, Vector2(460, 280), false)
	await finger(0, Vector2(260, 280), false)
	var mags: Array = rec.events.filter(func(e): return e is InputEventMagnifyGesture)
	var pans: Array = rec.events.filter(func(e): return e is InputEventPanGesture)
	check(not mags.is_empty() and mags.any(func(e): return e.factor > 1.0), "spreading two fingers zooms in (magnify factor > 1)")
	check(not pans.is_empty(), "moving two fingers pans")
	check(buttons(MOUSE_BUTTON_LEFT).is_empty() and buttons(MOUSE_BUTTON_RIGHT).is_empty(), "two fingers give no clicks")

	# Latched modifiers.
	rec.events.clear()
	touch.set_modifier(KEY_SHIFT, true)
	await frames(2)
	check(Input.is_key_pressed(KEY_SHIFT), "a latched Shift reads as held (session.queue, selection.add)")
	await finger(0, Vector2(300, 200), true)
	var t1 := Time.get_ticks_msec()
	while buttons(MOUSE_BUTTON_RIGHT).is_empty() and Time.get_ticks_msec() - t1 < 3000:
		await process_frame
	await finger(0, Vector2(300, 200), false)
	right = buttons(MOUSE_BUTTON_RIGHT)
	check(not right.is_empty() and right[0].shift_pressed, "an order made with Shift latched carries Shift (queued)")
	touch.set_modifier(KEY_CTRL, true)
	await frames(2)
	check(Input.is_key_pressed(KEY_CTRL) and Input.is_key_pressed(KEY_SHIFT), "Ctrl latches alongside Shift")
	touch.release_modifiers()
	await frames(2)
	check(not Input.is_key_pressed(KEY_SHIFT) and not Input.is_key_pressed(KEY_CTRL), "release_modifiers lets both go")
	rec.events.clear()
	await finger(0, Vector2(300, 200), true)
	await finger(0, Vector2(300, 200), false)
	left = buttons(MOUSE_BUTTON_LEFT)
	check(left.size() == 2 and not left[0].shift_pressed and not left[0].ctrl_pressed, "clicks after the release carry no modifier")

	# The touch bar in a match drives the latch.
	var bar: Node = load("res://scenes/session/touch_controls.tscn").instantiate()
	root.add_child(bar)
	await frames(2)
	var shift_btn: Button = bar.get_node("LeftBar/BtnShift")
	shift_btn.button_pressed = true
	await frames(2)
	check(touch.is_modifier_on(KEY_SHIFT), "the touch bar's Shift button latches Shift")
	shift_btn.button_pressed = false
	await frames(2)
	check(not touch.is_modifier_on(KEY_SHIFT), "tapping it again releases Shift")

	print("e2e_touch_input: %s (%d failure(s))" % ["PASS" if failures == 0 else "FAIL", failures])
	quit(1 if failures else 0)
