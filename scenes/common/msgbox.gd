extends Control
## Message boxes, a port of 0 A.D. 0.28's mod gui/msgbox (msgbox.js/.xml),
## gui/timedconfirmation (TimedConfirmation) and gui/common/utilities.js
## (setButtonCaptionsAndVisibility, distributeButtonsHorizontally;
## GPL-2.0-or-later). A faded backdrop, a ModernDialog of the given size with
## its title on the top edge, the message, and up to three buttons spread
## along the bottom. Escape ("cancel") presses the first one, which by
## convention is the cancelling one. "answered" carries the pressed index.
##
##     var box = MsgBox.open(self, 400, 200, "Are you sure?", "Confirmation", ["No", "Yes"])
##     box.answered.connect(func(i): if i == 1: ...)

signal answered(index: int)

var width := 400.0
var height := 200.0
var message := ""
var title := ""
var captions: Array = []
# timedConfirmation: the message's %(time_parameter)s counts the seconds down,
# and the first button is pressed when they run out.
var timeout_ms := 0
var time_parameter := ""

var _hotkeys := RtsoHotkeyConfig.new()
var _text: Label
var _deadline := 0
var _done := false

## functions_msgbox.js openURL: the page in the browser, and a box saying so.
static var last_url := "" # for tests
static func open_url(parent: Node, url: String) -> Control:
	last_url = url
	if DisplayServer.get_name() != "headless":
		OS.shell_open(url)
	return open(parent, 600, 200,
		TranslationServer.translate("Opening %(url)s\n in default web browser. Please wait…").replace("%(url)s", url),
		TranslationServer.translate("Opening page"))

## messageBox(width, height, message, title, buttonCaptions) over parent.
static func open(parent: Node, w: float, h: float, msg: String, ttl: String, buttons: Array = []) -> Control:
	var layer := CanvasLayer.new()
	layer.layer = 60
	layer.process_mode = Node.PROCESS_MODE_ALWAYS
	parent.add_child(layer)
	var box = load("res://scenes/common/msgbox.gd").new()
	box.name = "MsgBox"
	box.width = w
	box.height = h
	box.message = msg
	box.title = ttl
	box.captions = buttons
	layer.add_child(box)
	box.answered.connect(func(_i): layer.queue_free())
	return box

## timedConfirmation(width, height, message, timeParameter, timeout, title, buttonCaptions).
static func open_timed(parent: Node, w: float, h: float, msg: String, time_param: String, timeout: int, ttl: String, buttons: Array = []) -> Control:
	var layer := CanvasLayer.new()
	layer.layer = 60
	layer.process_mode = Node.PROCESS_MODE_ALWAYS
	parent.add_child(layer)
	var box = load("res://scenes/common/msgbox.gd").new()
	box.name = "TimedConfirmation"
	box.width = w
	box.height = h
	box.message = msg
	box.title = ttl
	box.captions = buttons
	box.timeout_ms = timeout
	box.time_parameter = time_param
	layer.add_child(box)
	box.answered.connect(func(_i): layer.queue_free())
	return box

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	var fade := ColorRect.new()
	fade.color = Color8(0, 0, 0, 85) # ModernFade
	fade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(fade)
	theme = preload("res://scenes/common/modern_theme.gd").theme()
	var main := Panel.new()
	main.name = "Main"
	main.theme_type_variation = "ModernDialog"
	main.set_anchors_preset(Control.PRESET_CENTER)
	main.offset_left = -width / 2.0
	main.offset_right = width / 2.0
	main.offset_top = -height / 2.0
	main.offset_bottom = height / 2.0
	add_child(main)
	var bar := Label.new()
	bar.name = "Title"
	bar.text = title
	bar.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_bold_stroke(bar) # mbTitleBar: ModernLabelText, sans-bold-stroke-14
	bar.anchor_left = 0.5
	bar.anchor_right = 0.5
	bar.offset_left = -128
	bar.offset_right = 128
	bar.offset_top = -18
	bar.offset_bottom = 14
	main.add_child(bar)
	_text = Label.new()
	_text.name = "Text"
	_text.text = message
	_text.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_text.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_text.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	_text.set_anchors_preset(Control.PRESET_FULL_RECT)
	_text.offset_left = 18
	_text.offset_top = 18
	_text.offset_right = -18
	_text.offset_bottom = -64
	_bold_stroke(_text) # mbText: ModernLabelText
	main.add_child(_text)
	var names: Array = captions if not captions.is_empty() else [tr("OK")]
	# distributeButtonsHorizontally: 18 px in from each side, 10 px apart, 46-18 px from the bottom.
	var n := mini(names.size(), 3)
	for i in n:
		var b := Button.new()
		b.name = "Button%d" % (i + 1)
		b.text = str(names[i])
		_bold_stroke(b) # ModernButtonRed: sans-bold-stroke-14
		# distributeButtonsHorizontally: thirds at 33% and 66%.
		b.anchor_left = [0.0, 0.33, 0.66][i] if n == 3 else float(i) / n
		b.anchor_right = [0.33, 0.66, 1.0][i] if n == 3 else float(i + 1) / n
		b.anchor_top = 1.0
		b.anchor_bottom = 1.0
		b.offset_left = 18.0 if i == 0 else 5.0
		b.offset_right = -18.0 if i == n - 1 else -5.0
		b.offset_top = -46
		b.offset_bottom = -18
		b.pressed.connect(_answer.bind(i))
		main.add_child(b)
	if timeout_ms > 0:
		# tmcText: 18 40 100%-18 100%-64, under the timer line.
		_text.offset_top = 40
		_deadline = Time.get_ticks_msec() + timeout_ms
		_update_timer(timeout_ms)

func _answer(i: int) -> void:
	if _done:
		return
	_done = true
	answered.emit(i)

func _process(_delta: float) -> void:
	if timeout_ms <= 0 or _done:
		return
	var remaining := _deadline - Time.get_ticks_msec()
	if remaining < 1:
		_answer(0)
		return
	_update_timer(remaining)

func _update_timer(ms: int) -> void:
	_text.text = message.replace("%(" + time_parameter + ")s", str(ceili(ms / 1000.0)))

func _input(event: InputEvent) -> void:
	if not (event is InputEventKey or event is InputEventMouseButton):
		return
	if "cancel" in _hotkeys.fired(event):
		get_viewport().set_input_as_handled()
		_answer(0)

static func _bold_stroke(c: Control) -> void:
	c.add_theme_font_override("font", preload("res://scenes/session/summary_screen.gd").bold_font())
	c.add_theme_font_size_override("font_size", 14)
	c.add_theme_color_override("font_outline_color", Color.BLACK)
	c.add_theme_constant_override("outline_size", 3)
