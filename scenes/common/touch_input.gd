extends Node

# Touch screens (Android, iOS) send touches, and the session and menus read the
# mouse. Godot's own touch-to-mouse emulation gives only the left button, so it
# is switched off here and touches become mouse events:
#   tap               left click (select, press a button); a quick second tap is a double click
#   drag              left drag (box select, scroll a list)
#   hold still        right click (order the selection: move, gather, attack, build)
#   two fingers       pan the camera; pinch to zoom
# Desktop builds never load this behaviour (it returns at once without a touch screen).

const HOLD_SEC := 0.45
const DRAG_PX := 18.0
const DOUBLE_TAP_SEC := 0.35

var _touches := {}       # index -> current position
var _start := Vector2.ZERO
var _start_time := 0.0
var _dragging := false
var _held := false
var _multi := false      # a second finger went down: no clicks until every finger lifts
var _last_tap_time := -10.0
var _last_tap_pos := Vector2.ZERO
var _pinch_dist := 0.0
var _pinch_mid := Vector2.ZERO
var _active := false

func _ready() -> void:
	_active = OS.has_feature("mobile")
	if not _active:
		set_process(false)
		set_process_input(false)
		return
	Input.emulate_mouse_from_touch = false
	process_mode = Node.PROCESS_MODE_ALWAYS

func _now() -> float:
	return Time.get_ticks_msec() / 1000.0

func _input(event: InputEvent) -> void:
	if event is InputEventScreenTouch:
		_on_touch(event)
	elif event is InputEventScreenDrag:
		_on_drag(event)

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
			if _dragging and not _multi:
				_button(MOUSE_BUTTON_LEFT, false, _touches.values()[0])
			_multi = true
			_pinch_reset()
		return
	_touches.erase(e.index)
	if _multi:
		if _touches.is_empty():
			_multi = false
		else:
			_pinch_reset()
		return
	if _dragging:
		_motion(e.position, MOUSE_BUTTON_MASK_LEFT)
		_button(MOUSE_BUTTON_LEFT, false, e.position)
	elif not _held:
		var t := _now()
		var dbl := t - _last_tap_time < DOUBLE_TAP_SEC and e.position.distance_to(_last_tap_pos) < DRAG_PX * 2.0
		_motion(e.position, 0)
		_button(MOUSE_BUTTON_LEFT, true, e.position, dbl)
		_button(MOUSE_BUTTON_LEFT, false, e.position)
		_last_tap_time = -10.0 if dbl else t
		_last_tap_pos = e.position
	_dragging = false
	_held = false

func _on_drag(e: InputEventScreenDrag) -> void:
	_touches[e.index] = e.position
	if _multi:
		_two_finger()
		return
	if _held:
		return
	if not _dragging and e.position.distance_to(_start) > DRAG_PX:
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
	if _pinch_dist > 1.0 and absf(dist - _pinch_dist) > 2.0:
		var mag := InputEventMagnifyGesture.new()
		mag.position = mid
		mag.factor = dist / _pinch_dist
		Input.parse_input_event(mag)
	var d := mid - _pinch_mid
	if d.length() > 0.5:
		var pan := InputEventPanGesture.new()
		pan.position = mid
		# The camera moves with the fingers, as on a map app.
		pan.delta = -d * 0.12
		Input.parse_input_event(pan)
	_pinch_dist = dist
	_pinch_mid = mid

func _motion(pos: Vector2, mask: int) -> void:
	var m := InputEventMouseMotion.new()
	m.position = pos
	m.global_position = pos
	m.button_mask = mask
	Input.parse_input_event(m)

func _button(idx: MouseButton, pressed: bool, pos: Vector2, dbl := false) -> void:
	var b := InputEventMouseButton.new()
	b.button_index = idx
	b.pressed = pressed
	b.double_click = dbl
	b.position = pos
	b.global_position = pos
	b.button_mask = (MOUSE_BUTTON_MASK_LEFT if idx == MOUSE_BUTTON_LEFT else MOUSE_BUTTON_MASK_RIGHT) if pressed else 0
	Input.parse_input_event(b)
