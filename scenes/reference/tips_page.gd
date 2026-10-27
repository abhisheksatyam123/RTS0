extends Control
## Tips and Tricks, a port of 0 A.D. 0.28 gui/reference/tips (tips.xml,
## TipsPage.js, common/Buttons/CloseButton; GPL-2.0-or-later): every gameplay
## tip, shuffled, in a dialog with Previous/Next, the picture arrows and Close
## (hotkey "cancel"). Learn to Play > Tips and Tricks opens it over the menu
## (Engine.OpenChildPage with tipScrolling).

const L10n = preload("res://scenes/menu/l10n.gd")

signal closed

var display: Control
var _hotkeys := RtsoHotkeyConfig.new()

static func open(parent: Node) -> Control:
	var page = load("res://scenes/reference/tips_page.gd").new()
	page.name = "TipsPage"
	parent.add_child(page)
	return page

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	var fade := ColorRect.new() # BackgroundTranslucent
	fade.color = Color8(0, 0, 0, 85)
	fade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(fade)
	var panel := Panel.new() # ModernDialog, 976 x 510
	panel.name = "Dialog"
	panel.set_anchors_preset(Control.PRESET_CENTER)
	panel.offset_left = -488
	panel.offset_right = 488
	panel.offset_top = -255
	panel.offset_bottom = 255
	add_child(panel)
	var title := Label.new()
	title.name = "Title"
	title.text = tr("Tips and Tricks")
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.position = Vector2(488 - 128, -18)
	title.size = Vector2(256, 32)
	title.add_theme_font_size_override("font_size", 14) # TitleText: sans-bold-stroke-14
	title.add_theme_color_override("font_outline_color", Color.BLACK)
	title.add_theme_constant_override("outline_size", 4)
	panel.add_child(title)
	display = load("res://scenes/reference/tip_display.gd").new()
	display.name = "TipDisplay"
	display.scrolling = true
	display.position = Vector2(36, 40)
	panel.add_child(display)
	var close := Button.new()
	close.name = "CloseButton"
	close.text = tr("Close")
	close.position = Vector2(976 - 216, 510 - 56)
	close.size = Vector2(192, 32)
	close.tooltip_text = _hotkeys.colorize(tr("%(hotkey)s: Close Tips and Tricks."), "cancel")
	close.pressed.connect(close_page)
	panel.add_child(close)

func close_page() -> void:
	closed.emit()
	queue_free()

func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and "cancel" in _hotkeys.fired(event):
		get_viewport().set_input_as_handled()
		close_page()
