extends Node

# Touch screens (Android, iOS) send touches, and the session and menus read the
# mouse. Godot's own touch-to-mouse emulation gives only the left button, so it
# is switched off here and touches become mouse events:
#   tap               left click (select, press a button); a quick second tap is a double click
#   drag              left drag (box select, scroll a list)
#   hold still        right click (order the selection: move, gather, attack, build)
#   two-finger tap    right click where the first finger went down (a quicker order)
#   two fingers       pan the camera; pinch to zoom
# The mouse is parked off the screen until a finger is down: a touch near an edge (or
# the pointer's start at a corner) would otherwise scroll the camera until the next touch.
# The session's touch bar latches Shift, Ctrl and Alt through set_modifier(): while one
# is on, Input reports the key held and every touch-made mouse event carries it, so
# Shift queues orders and adds to the selection, Ctrl and Alt work as with a keyboard.
# Desktop builds leave it off.

const HOLD_SEC := 0.45
const DRAG_PX := 18.0  # in UI pixels (the 1280x720 canvas), so the same finger travel on any screen
const DOUBLE_TAP_SEC := 0.35
const TWO_FINGER_TAP_SEC := 0.4
const PARKED := Vector2(-10000, -10000)

var _touches := {}       # index -> current position
var _start := Vector2.ZERO
var _start_time := 0.0
var _dragging := false
var _held := false
var _multi := false      # a second finger went down: no clicks until every finger lifts
var _multi_tap := false  # ... and it may still be a two-finger tap
var _multi_time := 0.0
var _multi_pos := Vector2.ZERO
var _multi_mid := Vector2.ZERO
var _multi_dist := 0.0
var _last_tap_time := -10.0
var _last_tap_pos := Vector2.ZERO
var _pinch_dist := 0.0
var _pinch_mid := Vector2.ZERO
var _active := false
var _mods := {KEY_SHIFT: false, KEY_CTRL: false, KEY_ALT: false}

func _ready() -> void:
	# --touch-input (after --) turns it on elsewhere, for touchscreen laptops and tools/e2e_touch_input.gd.
	if OS.has_feature("mobile") or "--touch-input" in OS.get_cmdline_user_args():
		activate()
	else:
		set_process(false)
		set_process_input(false)

func activate() -> void:
	_active = true
	Input.emulate_mouse_from_touch = false
	process_mode = Node.PROCESS_MODE_ALWAYS
	set_process(true)
	set_process_input(true)
	# Until the first touch the pointer sits at (0, 0), a screen corner: park it so the
	# camera doesn't edge-scroll from the start of a match.
	_park.call_deferred()

# Latch (on) or release a modifier key for touch play.
func set_modifier(key: Key, on: bool) -> void:
	if not _mods.has(key) or _mods[key] == on:
		return
	_mods[key] = on
	var k := InputEventKey.new()
	k.keycode = key
	k.physical_keycode = key
	k.pressed = on
	_apply_mods(k)
	Input.parse_input_event(k)

func is_modifier_on(key: Key) -> bool:
	return _mods.get(key, false)

func release_modifiers() -> void:
	for key in _mods.keys():
		set_modifier(key, false)

func _apply_mods(e: InputEventWithModifiers) -> void:
	e.shift_pressed = _mods[KEY_SHIFT]
	e.ctrl_pressed = _mods[KEY_CTRL]
	e.alt_pressed = _mods[KEY_ALT]

# UI pixels to window pixels: touches arrive in window pixels.
func _px(v: float) -> float:
	return v * maxf(get_viewport().get_final_transform().get_scale().x, 0.001)

func _now() -> float:
	return Time.get_ticks_msec() / 1000.0

func _input(event: InputEvent) -> void:
	# Events reach _input in viewport coordinates (after the canvas_items stretch), but
	# parse_input_event wants window coordinates, so map the touch back first.
	if event is InputEventScreenTouch or event is InputEventScreenDrag:
		var e: InputEvent = event.xformed_by(get_viewport().get_final_transform())
		if e is InputEventScreenTouch:
			_on_touch(e)
		else:
			_on_drag(e)

func _on_touch(e: InputEventScreenTouch) -> void:
	if e.pressed:
		_touches[e.index] = e.position
		if _touches.size() == 1 and not _multi:
			_start = e.position
			_start_time = _now()
			_dragging = false
			_held = false
			_motion(e.position, 0)
		elif _touches.size() >= 2:
			if not _multi:
				if _dragging:
					_button(MOUSE_BUTTON_LEFT, false, _touches.values()[0])
				_multi_tap = not _dragging and not _held and _touches.size() == 2
				_multi_time = _now()
				_multi_pos = _start
			else:
				_multi_tap = false
			_multi = true
			_pinch_reset()
			_multi_mid = _pinch_mid
			_multi_dist = _pinch_dist
		return
	_touches.erase(e.index)
	if _multi:
		if _touches.is_empty():
			_multi = false
			if _multi_tap and _now() - _multi_time < TWO_FINGER_TAP_SEC:
				_motion(_multi_pos, 0)
				_button(MOUSE_BUTTON_RIGHT, true, _multi_pos)
				_button(MOUSE_BUTTON_RIGHT, false, _multi_pos)
			_multi_tap = false
			_park()
		else:
			_pinch_reset()
		return
	if _dragging:
		_motion(e.position, MOUSE_BUTTON_MASK_LEFT)
		_button(MOUSE_BUTTON_LEFT, false, e.position)
	elif not _held:
		var t := _now()
		var dbl := t - _last_tap_time < DOUBLE_TAP_SEC and e.position.distance_to(_last_tap_pos) < _px(DRAG_PX * 2.0)
		_motion(e.position, 0)
		_button(MOUSE_BUTTON_LEFT, true, e.position, dbl)
		_button(MOUSE_BUTTON_LEFT, false, e.position)
		_last_tap_time = -10.0 if dbl else t
		_last_tap_pos = e.position
	_dragging = false
	_held = false
	_park()

func _on_drag(e: InputEventScreenDrag) -> void:
	_touches[e.index] = e.position
	if _multi:
		_two_finger()
		return
	if _held:
		return
	if not _dragging and e.position.distance_to(_start) > _px(DRAG_PX):
		_dragging = true
		_motion(_start, 0)
		_button(MOUSE_BUTTON_LEFT, true, _start)
	if _dragging:
		_motion(e.position, MOUSE_BUTTON_MASK_LEFT)

func _process(_delta: float) -> void:
	if _touches.size() != 1 or _multi or _dragging or _held:
		return
	if _now() - _start_time >= HOLD_SEC:
		_held = true
		_motion(_start, 0)
		_button(MOUSE_BUTTON_RIGHT, true, _start)
		_button(MOUSE_BUTTON_RIGHT, false, _start)
		Input.vibrate_handheld(30)

func _pinch_reset() -> void:
	var p: Array = _touches.values()
	if p.size() < 2:
		return
	_pinch_dist = (p[0] as Vector2).distance_to(p[1])
	_pinch_mid = ((p[0] as Vector2) + (p[1] as Vector2)) * 0.5

func _two_finger() -> void:
	var p: Array = _touches.values()
	if p.size() < 2:
		return
	var a: Vector2 = p[0]
	var b: Vector2 = p[1]
	var dist := a.distance_to(b)
	var mid := (a + b) * 0.5
	if mid.distance_to(_multi_mid) > _px(DRAG_PX) or absf(dist - _multi_dist) > _px(DRAG_PX):
		_multi_tap = false
	if _pinch_dist > _px(1.0) and absf(dist - _pinch_dist) > _px(2.0):
		var mag := InputEventMagnifyGesture.new()
		mag.position = mid
		mag.factor = dist / _pinch_dist
		Input.parse_input_event(mag)
	var d := (mid - _pinch_mid) / _px(1.0)
	if d.length() > 0.5:
		var pan := InputEventPanGesture.new()
		pan.position = mid
		# The camera moves with the fingers, as on a map app.
		pan.delta = -d * 0.12
		Input.parse_input_event(pan)
	_pinch_dist = dist
	_pinch_mid = mid

# Move the pointer off the screen once no finger is down (see the top).
func _park() -> void:
	if _touches.is_empty():
		_motion(PARKED, 0)

func _motion(pos: Vector2, mask: int) -> void:
	var m := InputEventMouseMotion.new()
	m.position = pos
	m.global_position = pos
	m.button_mask = mask
	_apply_mods(m)
	Input.parse_input_event(m)

func _button(idx: MouseButton, pressed: bool, pos: Vector2, dbl := false) -> void:
	var b := InputEventMouseButton.new()
	b.button_index = idx
	b.pressed = pressed
	b.double_click = dbl
	b.position = pos
	b.global_position = pos
	b.button_mask = (MOUSE_BUTTON_MASK_LEFT if idx == MOUSE_BUTTON_LEFT else MOUSE_BUTTON_MASK_RIGHT) if pressed else 0
	_apply_mods(b)
	Input.parse_input_event(b)
