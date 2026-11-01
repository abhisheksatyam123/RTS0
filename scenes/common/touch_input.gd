extends Node

## 0 A.D.-style Mobile / Android Touch Input Controller.
##
## Implements the full 0 A.D. mobile RTS touch control model:
##   1. Context Tap:
##      - Over GUI: Left Click (LMB) on buttons, lists, and HUD controls.
##      - In World while placing building: Left Click (LMB) places foundation.
##      - In World with units/buildings selected: Right Click (RMB) issues immediate
##        orders (move, attack, gather, build, repair, board, rally).
##      - In World with NO selection: Left Click (LMB) selects target.
##   2. Double Tap:
##      - Quick double-tap on a unit selects all on-screen units of that type.
##   3. Camera Pan vs Box Select:
##      - 1-finger drag by default pans the camera smoothly across the terrain.
##      - Box Selection is activated either by toggling "Box Select" on the Touch Bar
##        or holding the finger still for >= 0.20s before dragging (Bandbox armed).
##   4. Ground Long-Press:
##      - Holding still on ground for >= 0.35s and releasing clears selection.
##   5. Two-Finger Gestures:
##      - Pinch in/out: Zoom camera.
##      - Two-finger drag: Pan camera.
##      - Two-finger quick tap: Alternate Right Click order.
##   6. Android Back Button:
##      - Acts as Escape (cancels placement, closes dialogs, deselects, or pauses).

const BAND_ARM_SEC := 0.20
const HOLD_CLEAR_SEC := 0.35
const DRAG_PX := 16.0
const DOUBLE_TAP_SEC := 0.35
const TWO_FINGER_TAP_SEC := 0.35
const PARKED := Vector2(-10000, -10000)

var _touches := {}
var _start_pos := Vector2.ZERO
var _prev_pos := Vector2.ZERO
var _start_time := 0.0
var _is_gui_touch := false
var _dragging := false
var _bandbox_active := false
var _bandbox_armed := false
var _box_select_mode := false
var _held_long := false
var _multi := false
var _multi_tap := false
var _multi_time := 0.0
var _multi_pos := Vector2.ZERO
var _pinch_dist := 0.0
var _pinch_mid := Vector2.ZERO
var _last_tap_time := -10.0
var _last_tap_pos := Vector2.ZERO
var _active := false
var _mods := {KEY_SHIFT: false, KEY_CTRL: false, KEY_ALT: false}

func _ready() -> void:
	if OS.has_feature("mobile") or "--touch-input" in OS.get_cmdline_user_args() or DisplayServer.is_touchscreen_available():
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
	_park.call_deferred()

func set_box_select_mode(on: bool) -> void:
	_box_select_mode = on

func is_box_select_mode() -> bool:
	return _box_select_mode

func _get_main() -> Node:
	var cur := get_tree().current_scene
	if cur and (cur.is_class("RtsoMain") or cur.has_method("has_selection")):
		return cur
	return get_node_or_null("/root/Main")

func has_selection() -> bool:
	var main := _get_main()
	if main:
		if main.has_method("has_selection"):
			return main.has_selection()
		if "selected_ids" in main:
			return not (main.selected_ids as Array).is_empty()
	return false

func is_placing_building() -> bool:
	var main := _get_main()
	if main:
		if main.has_method("is_placing_building"):
			return main.is_placing_building()
		if "build_mode" in main:
			return bool(main.build_mode)
	return false

func _notification(what: int) -> void:
	if what == NOTIFICATION_WM_GO_BACK_REQUEST and _active:
		back()

func back() -> void:
	var scene := get_tree().current_scene
	var idle_match: bool = scene != null and scene.has_method("nothing_to_cancel") and scene.nothing_to_cancel()
	if _escape():
		return
	if idle_match:
		if scene.has_method("_on_menu_button_pressed"):
			scene._on_menu_button_pressed()
	elif scene != null and scene.has_method("_on_exit_pressed") and not scene.submenu_panel.visible:
		scene._on_exit_pressed()

func _escape() -> bool:
	var vp := get_viewport()
	var handled := false
	for pressed in [true, false]:
		var k := InputEventKey.new()
		k.keycode = KEY_ESCAPE
		k.physical_keycode = KEY_ESCAPE
		k.pressed = pressed
		vp.push_input(k)
		if pressed:
			handled = vp.is_input_handled()
	return handled

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

func _px(v: float) -> float:
	return v * maxf(get_viewport().get_final_transform().get_scale().x, 0.001)

func _now() -> float:
	return Time.get_ticks_msec() / 1000.0

func _is_over_gui(pos: Vector2) -> bool:
	_motion(pos, 0)
	var vp := get_viewport()
	if not vp:
		return false
	var c := vp.gui_get_hovered_control()
	if c != null and c.is_visible_in_tree() and c.mouse_filter != Control.MOUSE_FILTER_IGNORE:
		var cls_name := c.get_class()
		if cls_name == "SubViewportContainer" or c.name == "WorldView" or c.name == "Background" or cls_name == "RtsoSelectionRect":
			return false
		return true
	return false

func _input(event: InputEvent) -> void:
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
			_start_pos = e.position
			_prev_pos = e.position
			_start_time = _now()
			_dragging = false
			_bandbox_active = false
			_bandbox_armed = _box_select_mode
			_held_long = false
			_is_gui_touch = _is_over_gui(e.position)
			if _is_gui_touch:
				_motion(e.position, 0)
		elif _touches.size() >= 2:
			if not _multi:
				if _bandbox_active:
					_button(MOUSE_BUTTON_LEFT, false, _start_pos)
					_bandbox_active = false
				_multi_tap = not _dragging and not _held_long and _touches.size() == 2
				_multi_time = _now()
				_multi_pos = _start_pos
			else:
				_multi_tap = false
			_multi = true
			_pinch_reset()
		return

	# Touch Released (Finger UP)
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
		if _bandbox_active:
			_motion(e.position, MOUSE_BUTTON_MASK_LEFT)
			_button(MOUSE_BUTTON_LEFT, false, e.position)
		_dragging = false
		_bandbox_active = false
		_park()
		return

	# Tap or Long-press release without dragging
	if _held_long and not _is_gui_touch:
		# 0 A.D. Ground Hold Clear
		var main := _get_main()
		if main and main.has_method("clear_selection"):
			main.call("clear_selection")
		Input.vibrate_handheld(20)
	else:
		# Context Tap Dispatch
		var t := _now()
		if _is_gui_touch:
			_motion(e.position, 0)
			_button(MOUSE_BUTTON_LEFT, true, e.position)
			_button(MOUSE_BUTTON_LEFT, false, e.position)
		else:
			_motion(e.position, 0)
			if is_placing_building():
				# Placing building: Left Click places foundation
				_button(MOUSE_BUTTON_LEFT, true, e.position)
				_button(MOUSE_BUTTON_LEFT, false, e.position)
			elif has_selection():
				# Has selection: Right Click issues context command (move, attack, gather, build, rally)
				_button(MOUSE_BUTTON_RIGHT, true, e.position)
				_button(MOUSE_BUTTON_RIGHT, false, e.position)
				Input.vibrate_handheld(15)
			else:
				# No selection: Left Click selects
				var dbl := (t - _last_tap_time < DOUBLE_TAP_SEC and e.position.distance_to(_last_tap_pos) < _px(DRAG_PX * 2.0))
				_button(MOUSE_BUTTON_LEFT, true, e.position, dbl)
				_button(MOUSE_BUTTON_LEFT, false, e.position)
				_last_tap_time = -10.0 if dbl else t
				_last_tap_pos = e.position

	_held_long = false
	_dragging = false
	_bandbox_active = false
	_park()

func _on_drag(e: InputEventScreenDrag) -> void:
	_touches[e.index] = e.position
	if _multi:
		_two_finger()
		return

	if _is_gui_touch:
		_motion(e.position, MOUSE_BUTTON_MASK_LEFT if _dragging else 0)
		return

	if not _dragging and e.position.distance_to(_start_pos) > _px(DRAG_PX):
		_dragging = true
		if _bandbox_armed or _box_select_mode:
			_bandbox_active = true
			_motion(_start_pos, 0)
			_button(MOUSE_BUTTON_LEFT, true, _start_pos)
			_motion(e.position, MOUSE_BUTTON_MASK_LEFT)

	if _dragging:
		if _bandbox_active:
			_motion(e.position, MOUSE_BUTTON_MASK_LEFT)
		else:
			# 0 A.D. 1-Finger Camera Pan
			var delta_px := e.position - _prev_pos
			if delta_px.length() > 0.5:
				var pan := InputEventPanGesture.new()
				pan.position = e.position
				pan.delta = -delta_px * 0.12
				Input.parse_input_event(pan)

	_prev_pos = e.position

func _process(_delta: float) -> void:
	if _touches.size() != 1 or _multi or _dragging:
		return
	var dt := _now() - _start_time
	if dt >= BAND_ARM_SEC:
		_bandbox_armed = true
	if dt >= HOLD_CLEAR_SEC and not _held_long and not _is_gui_touch:
		_held_long = true
		Input.vibrate_handheld(25)

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
	if mid.distance_to(_multi_pos) > _px(DRAG_PX) or absf(dist - _pinch_dist) > _px(DRAG_PX):
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
		pan.delta = -d * 0.12
		Input.parse_input_event(pan)
	_pinch_dist = dist
	_pinch_mid = mid

func _motion(pos: Vector2, mask: int) -> void:
	var m := InputEventMouseMotion.new()
	m.position = pos
	m.global_position = pos
	m.button_mask = mask
	_apply_mods(m)
	Input.parse_input_event(m)

func _button(btn: MouseButton, pressed: bool, pos: Vector2, double_click := false) -> void:
	var b := InputEventMouseButton.new()
	b.button_index = btn
	b.pressed = pressed
	b.position = pos
	b.global_position = pos
	b.double_click = double_click
	if pressed:
		b.button_mask = (1 << (btn - 1)) if btn <= 3 else 0
	_apply_mods(b)
	Input.parse_input_event(b)

func _park() -> void:
	_motion(PARKED, 0)
