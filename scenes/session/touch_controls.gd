extends CanvasLayer

signal select_all_army_requested()
signal idle_worker_requested()
signal stop_requested()
signal build_requested()
signal market_requested()
signal diplo_requested()
signal auto_helper_cycled(level: int)

const AUTO_LEVEL_NAMES = ["OFF", "WORK", "TOWN", "WAR"]
const AUTO_LEVEL_TIPS = [
	"OFF: You control everything.",
	"WORK: Idle villagers gather. You attack and train.",
	"TOWN: Houses, training, gathering. You attack.",
	"WAR: Full helper, including raids."
]
var _auto_level: int = 0

func _ready() -> void:
	visible = OS.has_feature("mobile") or DisplayServer.is_touchscreen_available() or "--touch-input" in OS.get_cmdline_user_args()
	_update_auto_button()

func _exit_tree() -> void:
	var touch = get_node_or_null("/root/TouchInput")
	if touch:
		touch.release_modifiers()
		if touch.has_method("set_box_select_mode"):
			touch.set_box_select_mode(false)

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

func _on_box_toggled(on: bool) -> void:
	var touch = get_node_or_null("/root/TouchInput")
	if touch and touch.has_method("set_box_select_mode"):
		touch.set_box_select_mode(on)

func _update_auto_button() -> void:
	var btn: Button = get_node_or_null("RightBar/BtnAuto")
	if btn:
		btn.text = "🤖\n" + AUTO_LEVEL_NAMES[_auto_level]
		btn.tooltip_text = "AUTO: " + AUTO_LEVEL_TIPS[_auto_level]

func _on_auto_pressed() -> void:
	_auto_level = (_auto_level + 1) % 4
	_update_auto_button()
	emit_signal("auto_helper_cycled", _auto_level)
	var main = get_parent()
	if main and main.has_method("set_auto_helper_level"):
		main.call("set_auto_helper_level", _auto_level)
	elif main and main.has_method("cycle_auto_workers"):
		main.call("cycle_auto_workers")

func _on_army_pressed() -> void:
	var main = get_parent()
	if main and main.has_method("select_all_military"):
		main.call("select_all_military")
	else:
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
