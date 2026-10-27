extends Control
## 0.28 gamesetup_mp pageConnecting (gamesetup_mp.xml): the 460x240 ModernDialog
## with "Multiplayer" on its top edge, the connection status in ModernLabelText at
## its middle, and only Cancel (cancelSetup: Engine.DisconnectNetworkGame, back to
## the lobby) at the bottom left, since switchSetupPage hides Continue here.
## 0.28 fades the main menu behind it; RTS0 opens it in the match scene before any
## world has loaded, so the page behind it stays opaque.
## RtsoMain::update_net_connect_overlay puts it up and sets `text`.

const MODERN := preload("res://scenes/common/modern_theme.gd")
const HOTKEYS := preload("res://scenes/menu/hotkeys.gd")

var net_main: Object
var text := "Connecting to server...":
	set(value):
		text = value
		if _label:
			_label.text = value

var _label: Label
var _hotkeys := RtsoHotkeyConfig.new()

func _ready() -> void:
	name = "NetConnecting"
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	theme = MODERN.theme()
	var bg := ColorRect.new()
	bg.color = Color(0.05, 0.05, 0.05, 1)
	bg.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(bg)
	# multiplayerPages: 50%-230 50%-120 50%+230 50%+120.
	var dlg := Panel.new()
	dlg.name = "Main"
	dlg.theme_type_variation = "ModernDialog"
	dlg.set_anchors_preset(Control.PRESET_CENTER)
	dlg.offset_left = -230
	dlg.offset_right = 230
	dlg.offset_top = -120
	dlg.offset_bottom = 120
	add_child(dlg)
	HOTKEYS._edge_title(dlg, tr("Multiplayer")).offset_top = -18 # 50%-128 -18 50%+128 14
	# pageConnecting at 0 32 100% 100%; connectionStatus at 0 50%-10 100% 50%+10.
	_label = Label.new()
	_label.name = "Status"
	_label.text = text
	_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	MODERN.label_text(_label)
	_label.anchor_right = 1.0
	_label.anchor_top = 0.5
	_label.anchor_bottom = 0.5
	_label.offset_left = 10
	_label.offset_right = -10
	_label.offset_top = 16 - 30
	_label.offset_bottom = 16 + 30
	dlg.add_child(_label)
	# cancelButton: ModernButtonRed at 18 100%-45 50%-5 100%-17, hotkey "cancel".
	var cancel := Button.new()
	cancel.name = "Cancel"
	cancel.text = tr("Cancel")
	cancel.anchor_top = 1.0
	cancel.anchor_bottom = 1.0
	cancel.anchor_right = 0.5
	cancel.offset_left = 18
	cancel.offset_right = -5
	cancel.offset_top = -45
	cancel.offset_bottom = -17
	cancel.pressed.connect(_on_cancel)
	dlg.add_child(cancel)

func _unhandled_input(ev: InputEvent) -> void:
	if ev is InputEventKey and ev.pressed and not ev.echo and "cancel" in _hotkeys.fired(ev):
		get_viewport().set_input_as_handled()
		_on_cancel()

func _on_cancel() -> void:
	if net_main:
		net_main.leave_net_game()
