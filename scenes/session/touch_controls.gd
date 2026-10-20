extends CanvasLayer

signal select_all_army_requested()
signal idle_worker_requested()
signal stop_requested()
signal build_requested()
signal market_requested()
signal diplo_requested()

func _ready() -> void:
	# A touch screen's stand-in for keys and right-clicks. 0 A.D. 0.28 has no such
	# bar, and with a mouse it only covers the training list, so it shows on touch
	# screens alone.
	visible = OS.has_feature("mobile") or DisplayServer.is_touchscreen_available()

func _exit_tree() -> void:
	var touch = get_node_or_null("/root/TouchInput")
	if touch:
		touch.release_modifiers()

# Latched modifier keys: on until tapped again.
func _set_modifier(key: Key, on: bool) -> void:
	var touch = get_node_or_null("/root/TouchInput")
	if touch:
		touch.set_modifier(key, on)

func _on_shift_toggled(on: bool) -> void:
	_set_modifier(KEY_SHIFT, on)

func _on_ctrl_toggled(on: bool) -> void:
	_set_modifier(KEY_CTRL, on)

func _on_alt_toggled(on: bool) -> void:
	_set_modifier(KEY_ALT, on)

func _on_army_pressed() -> void:
	emit_signal("select_all_army_requested")

func _on_idle_pressed() -> void:
	var main = get_parent()
	if main and main.has_method("cycle_idle_worker"):
		main.call("cycle_idle_worker")

func _on_stop_pressed() -> void:
	var ev = InputEventKey.new()
	ev.keycode = KEY_S
	ev.pressed = true
	Input.parse_input_event(ev)

func _on_build_pressed() -> void:
	var main = get_parent()
	if main and main.has_method("enter_build_mode"):
		main.call("enter_build_mode", true, 1)

func _on_market_pressed() -> void:
	var barter = get_node_or_null("../BarterDialog")
	if barter:
		barter.call("toggle")

func _on_diplo_pressed() -> void:
	var diplo = get_node_or_null("../DiplomacyDialog")
	if diplo:
		diplo.call("toggle")
