extends Node

## 0 A.D. Canonical Touch Input Controller for Mobile (Android / iOS).
##
## Implements the exact 0 A.D. mobile RTS touch control model:
##   1. Context Tap:
##      - Over GUI / HUD / Minimap: Left Click (LMB) activates controls.
##      - In World while placing building: Left Click (LMB) places foundation.
##      - In World with units/buildings selected: Right Click (RMB) issues immediate
##        orders (move, attack, gather, build, repair, board, rally).
##      - In World with NO selection: Left Click (LMB) selects target.
##   2. Double Tap:
##      - Quick double-tap selects all on-screen units of that type (LMB double-click).
##   3. Drag (Box Select / Bandbox):
##      - 1-finger drag in the world draws the selection bandbox (LMB drag).
##      - Drag on GUI / minimap pans or scrolls.
##      - Toggle Box button / bandbox arming fully supported.
##   4. Hold Still:
##      - Holding still for >= HOLD_SEC (0.45s) sends Right Click (RMB) at finger position,
##        ordering selected units (or minimap rally/orders), and suppresses left click on release.
##      - Latched modifiers (Shift/Ctrl/Alt) are carried by the order.
##   5. Two-Finger Gestures:
##      - Pinch in/out: Zoom camera (magnify gesture).
##      - Two-finger drag: Pan camera (pan gesture).
##      - Two-finger quick tap: Right Click order where the first finger went down.
##   6. Offscreen Pointer Parking:
##      - Pointer is parked off screen between touches to prevent unintended edge scrolling.
##   7. Android Back Button:
##      - Acts as Escape (cancels placement, closes dialogs, deselects, or opens/closes pause menu).

const HOLD_SEC := 0.45
const BAND_ARM_SEC := 0.20
const DRAG_PX := 18.0
const DOUBLE_TAP_SEC := 0.35
const TWO_FINGER_TAP_SEC := 0.40
const PARKED := Vector2(-200000, -200000)

var _touches := {}
var _start_pos := Vector2.ZERO
var _prev_pos := Vector2.ZERO
var _start_time := 0.0
var _dragging := false
var _held := false
var _box_select_mode := false
var _bandbox_armed := false
var _multi := false
var _multi_tap := false
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
	if cur != null and cur != self and (cur.is_class("RtsoMain") or cur.name == "Main"):
		return cur
	var root := get_tree().root
	if root != null:
		var main := root.get_node_or_null("Main")
		if main != null and main != self:
			return main
		for child in root.get_children():
			if child != self and (child.is_class("RtsoMain") or child.name == "Main"):
				return child
	return null

func has_selection() -> bool:
	var main := _get_main()
	if main != null:
		if main.has_method("has_selection"):
			return main.has_selection()
		if "selected_ids" in main:
			return not (main.selected_ids as Array).is_empty()
	return false

func is_placing_building() -> bool:
	var main := _get_main()
	if main != null:
		if main.has_method("is_placing_building"):
			return main.is_placing_building()
		if "build_mode" in main:
			return bool(main.build_mode)
	return false

func clear_selection() -> void:
	var main := _get_main()
	if main != null and main.has_method("clear_selection"):
		main.call("clear_selection")
	elif main != null and "selected_ids" in main:
		main.selected_ids = PackedInt32Array()

func request_ground_hold_clear() -> void:
	clear_selection()
	Input.vibrate_handheld(20)

func _notification(what: int) -> void:
	if what == NOTIFICATION_WM_GO_BACK_REQUEST and _active:
		back()

func back() -> void:
	var scene := get_tree().current_scene
	if scene == null:
		var root := get_tree().root
		for child in root.get_children():
			if child is Control or child is Node2D or child.is_class("RtsoMain"):
				scene = child
				break
	var idle_match: bool = scene != null and scene.has_method("nothing_to_cancel") and scene.nothing_to_cancel()
	if _escape():
		return
	if idle_match:
		if scene.has_method("_on_menu_button_pressed"):
			scene._on_menu_button_pressed()
	elif scene != null and scene.has_method("_on_exit_pressed") and ("submenu_panel" not in scene or not scene.submenu_panel.visible):
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
	var vp := get_viewport()
	if vp == null:
		return false
	var c := vp.gui_get_hovered_control()
	if c != null and c.is_visible_in_tree() and c.mouse_filter != Control.MOUSE_FILTER_IGNORE:
		var cls_name := c.get_class()
		if cls_name != "SubViewportContainer" and c.name != "WorldView" and c.name != "Background" and cls_name != "RtsoSelectionRect":
			return true
	return _check_gui_at(vp, _to_canvas(pos))

# A touch is in window pixels, but control rects are in the viewport's canvas units (the
# window is stretched over them): the touch's own mouse events take window pixels, the
# rect tests take canvas units.
func _to_canvas(window_pos: Vector2) -> Vector2:
	return get_viewport().get_final_transform().affine_inverse() * window_pos

# The visible control group icon under a point, if any (control_groups_view.h names them).
func _group_button_at(pos: Vector2) -> Button:
	var canvas := _to_canvas(pos)
	for n in get_tree().root.find_children("UnitGroupButton*", "Button", true, false):
		if (n as Button).is_visible_in_tree() and (n as Button).get_global_rect().has_point(canvas):
			return n
	return null

func _ctrl_click(pos: Vector2) -> void:
	var was: bool = _mods[KEY_CTRL]
	_mods[KEY_CTRL] = true
	_button(MOUSE_BUTTON_LEFT, true, pos)
	_button(MOUSE_BUTTON_LEFT, false, pos)
	_mods[KEY_CTRL] = was

func _check_gui_at(node: Node, pos: Vector2) -> bool:
	if node is Control and node.visible:
		var cls_name := node.get_class()
		if cls_name != "SubViewportContainer" and node.name != "WorldView" and node.name != "Background" and cls_name != "RtsoSelectionRect":
			var rect: Rect2 = node.get_global_rect()
			if rect.has_point(pos) and node.mouse_filter == Control.MOUSE_FILTER_STOP:
				return true
	for child in node.get_children():
		if _check_gui_at(child, pos):
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
			_held = false
			_bandbox_armed = _box_select_mode
			_motion(e.position, 0)
		elif _touches.size() >= 2:
			if not _multi:
				if _dragging:
					_button(MOUSE_BUTTON_LEFT, false, _touches.values()[0])
				_multi_tap = not _dragging and not _held and _touches.size() == 2
				_multi_time = _now()
				_multi_pos = _start_pos
			else:
				_multi_tap = false
			_multi = true
			_pinch_reset()
			_multi_mid = _pinch_mid
			_multi_dist = _pinch_dist
		return

	# Touch Released
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
		if _is_over_gui(e.position):
			_button(MOUSE_BUTTON_LEFT, true, e.position)
			_button(MOUSE_BUTTON_LEFT, false, e.position)
		elif is_placing_building():
			_button(MOUSE_BUTTON_LEFT, true, e.position)
			_button(MOUSE_BUTTON_LEFT, false, e.position)
		elif has_selection() and not dbl:
			# A double tap is never an order: its second tap selects the units of that type.
			_button(MOUSE_BUTTON_RIGHT, true, e.position)
			_button(MOUSE_BUTTON_RIGHT, false, e.position)
			Input.vibrate_handheld(15)
		else:
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
	if not _dragging and e.position.distance_to(_start_pos) > _px(DRAG_PX):
		_dragging = true
		_motion(_start_pos, 0)
		_button(MOUSE_BUTTON_LEFT, true, _start_pos)
	if _dragging:
		_motion(e.position, MOUSE_BUTTON_MASK_LEFT)
	_prev_pos = e.position

func _process(_delta: float) -> void:
	if _touches.size() != 1 or _multi or _dragging or _held:
		return
	var dt := _now() - _start_time
	if dt >= BAND_ARM_SEC:
		_bandbox_armed = true
	if dt >= HOLD_SEC:
		_held = true
		_motion(_start_pos, 0)
		if _group_button_at(_start_pos) != null:
			# A long press on a control group's icon sets the group to the selection (Ctrl-click);
			# a right click there would disband it.
			_ctrl_click(_start_pos)
		else:
			_button(MOUSE_BUTTON_RIGHT, true, _start_pos)
			_button(MOUSE_BUTTON_RIGHT, false, _start_pos)
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

func _park() -> void:
	_motion(PARKED, 0)
