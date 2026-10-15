extends Control
## 0.28 gamesetup_mp pageConnecting (gamesetup_mp.xml): a joiner waits on an
## opaque page with a small box that says what the connection is doing, and
## Cancel (cancelSetup: Engine.DisconnectNetworkGame, back to the lobby).
## RtsoMain::update_net_connect_overlay puts it up and sets `text`.

var net_main: Object
var text := "Connecting to server...":
	set(value):
		text = value
		if _label:
			_label.text = value

var _label: Label

func _ready() -> void:
	name = "NetConnecting"
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	var bg := ColorRect.new()
	bg.color = Color(0.05, 0.05, 0.05, 1)
	bg.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(bg)
	var center := CenterContainer.new()
	center.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(center)
	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(400, 140)
	center.add_child(panel)
	var box := VBoxContainer.new()
	box.alignment = BoxContainer.ALIGNMENT_CENTER
	box.add_theme_constant_override("separation", 16)
	panel.add_child(box)
	_label = Label.new()
	_label.name = "Status"
	_label.text = text
	_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	box.add_child(_label)
	var cancel := Button.new()
	cancel.name = "Cancel"
	cancel.text = "Cancel"
	cancel.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	cancel.custom_minimum_size = Vector2(160, 0)
	cancel.pressed.connect(_on_cancel)
	box.add_child(cancel)

func _unhandled_input(ev: InputEvent) -> void:
	if ev.is_action_pressed("ui_cancel"):
		get_viewport().set_input_as_handled()
		_on_cancel()

func _on_cancel() -> void:
	if net_main:
		net_main.leave_net_game()
