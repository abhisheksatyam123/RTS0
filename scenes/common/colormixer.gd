extends Control
## The colour mixer, a port of 0 A.D. 0.28's mod gui/colormixer (colormixer.js/.xml;
## GPL-2.0-or-later): a faded backdrop and a 500x370 ModernDialog titled "Color" with
## a swatch and one 0-255 slider per channel. Cancel (or the "cancel" hotkey) gives back
## the initial colour sanitized (each part floored, 0 when malformed), Save the mixed
## one, both as "R G B" through "closed".
##
##     var mixer = ColorMixer.open(self, "100 0 200")
##     mixer.closed.connect(func(rgb): ...)

signal closed(color: String)

const LABELS := ["Red", "Green", "Blue"]

var initial := ""
var sliders: Array[HSlider] = []

var _hotkeys := RtsoHotkeyConfig.new()
var _values: Array[Label] = []
var _swatch: ColorRect
var _sanitized := ""
var _done := false

static func open(parent: Node, initial_color: String) -> Control:
	var layer := CanvasLayer.new()
	layer.layer = 60
	layer.process_mode = Node.PROCESS_MODE_ALWAYS
	parent.add_child(layer)
	var mixer = load("res://scenes/common/colormixer.gd").new()
	mixer.name = "ColorMixer"
	mixer.initial = initial_color
	layer.add_child(mixer)
	mixer.closed.connect(func(_c): layer.queue_free())
	return mixer

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	var fade := ColorRect.new()
	fade.color = Color8(0, 0, 0, 85)
	fade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(fade)
	theme = preload("res://scenes/common/modern_theme.gd").theme()
	var main := Panel.new()
	main.name = "Main"
	main.theme_type_variation = "ModernDialog"
	main.set_anchors_preset(Control.PRESET_CENTER)
	main.offset_left = -250
	main.offset_right = 250
	main.offset_top = -185
	main.offset_bottom = 185
	add_child(main)
	var bar := _label(main, "Title", tr("Color"))
	bar.add_theme_font_size_override("font_size", 18)
	bar.add_theme_color_override("font_outline_color", Color.BLACK)
	bar.add_theme_constant_override("outline_size", 4)
	bar.anchor_left = 0.5
	bar.anchor_right = 0.5
	bar.offset_left = -128
	bar.offset_right = 128
	bar.offset_top = -18
	bar.offset_bottom = 14
	var info := _label(main, "Info", tr("Move the sliders to change the Red, Green and Blue components of the Color"))
	info.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	info.anchor_right = 1.0
	info.offset_left = 24
	info.offset_right = -24
	info.offset_top = 50
	info.offset_bottom = 80
	_swatch = ColorRect.new()
	_swatch.name = "ColorDisplay"
	_swatch.anchor_left = 0.5
	_swatch.anchor_right = 0.5
	_swatch.offset_left = -32
	_swatch.offset_right = 32
	_swatch.offset_top = 105
	_swatch.offset_bottom = 145
	main.add_child(_swatch)
	var parts := initial.split(" ")
	for i in 3:
		# color[n]: 40 px per channel from y 165, the label over a slider and its value.
		var top := 165.0 + 40.0 * i
		var name_label := _label(main, "ColorLabel%d" % i, tr(LABELS[i]))
		name_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_LEFT
		name_label.anchor_right = 1.0
		name_label.offset_left = 10
		name_label.offset_right = -10
		name_label.offset_top = top
		name_label.offset_bottom = top + 20
		var v := floori(float(parts[i])) if i < parts.size() and parts[i].is_valid_float() else 0
		var s := HSlider.new()
		s.name = "ColorSlider%d" % i
		s.min_value = 0
		s.max_value = 255
		s.step = 1
		s.value = v
		s.anchor_right = 1.0
		s.offset_left = 10
		s.offset_right = -50
		s.offset_top = top + 20
		s.offset_bottom = top + 40
		main.add_child(s)
		sliders.append(s)
		var value := _label(main, "ColorValue%d" % i, str(int(s.value)))
		value.anchor_left = 1.0
		value.anchor_right = 1.0
		value.offset_left = -50
		value.offset_right = -10
		value.offset_top = top + 20
		value.offset_bottom = top + 40
		_values.append(value)
		s.value_changed.connect(_on_slider.bind(i))
	_sanitized = current_color()
	_swatch.color = _to_color()
	# distributeButtonsHorizontally: Cancel and Save along the bottom.
	var captions := [tr("Cancel"), tr("Save")]
	for i in 2:
		var b := Button.new()
		b.name = "Button%d" % (i + 1)
		b.text = captions[i]
		b.anchor_left = i / 2.0
		b.anchor_right = (i + 1) / 2.0
		b.anchor_top = 1.0
		b.anchor_bottom = 1.0
		b.offset_left = 18.0 if i == 0 else 5.0
		b.offset_right = -5.0 if i == 0 else -18.0
		b.offset_top = -46
		b.offset_bottom = -18
		b.pressed.connect(_answer.bind(i))
		main.add_child(b)

func _label(parent: Control, n: String, text: String) -> Label:
	var l := Label.new()
	l.name = n
	l.text = text
	l.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	l.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	parent.add_child(l)
	return l

func current_color() -> String:
	return " ".join(sliders.map(func(s): return str(int(s.value))))

func _to_color() -> Color:
	return Color8(int(sliders[0].value), int(sliders[1].value), int(sliders[2].value))

func _on_slider(x: float, i: int) -> void:
	_values[i].text = str(int(x))
	_swatch.color = _to_color()

## 0 is Cancel (the sanitized initial colour), 1 is Save (the mixed one).
func _answer(i: int) -> void:
	if _done:
		return
	_done = true
	closed.emit(_sanitized if i == 0 else current_color())

func _input(event: InputEvent) -> void:
	if not (event is InputEventKey or event is InputEventMouseButton):
		return
	if "cancel" in _hotkeys.fired(event):
		get_viewport().set_input_as_handled()
		_answer(0)
