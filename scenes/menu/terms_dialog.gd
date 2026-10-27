extends Control
## 0.28 gui/termsdialog (page_termsdialog.xml): a 720x580 ModernDialog over a
## ModernFade with the page title on its top edge, the Language dropdown, the text
## in a ModernTextPanel over a ModernFade, and Decline / Accept at the bottom right.

signal confirmed
signal canceled

const MODERN := preload("res://scenes/common/modern_theme.gd")
const HOTKEYS := preload("res://scenes/menu/hotkeys.gd")

var title := ""
var text := ""
var ok_button_text := "Accept"
var cancel_button_text := "Decline"
var _hotkeys := RtsoHotkeyConfig.new()

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	theme = MODERN.theme()
	var fade := ColorRect.new()
	fade.color = Color8(0, 0, 0, 85) # ModernFade
	fade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(fade)
	var dlg := Panel.new()
	dlg.name = "Main"
	dlg.theme_type_variation = "ModernDialog"
	dlg.set_anchors_preset(Control.PRESET_CENTER)
	dlg.offset_left = -360
	dlg.offset_right = 360
	dlg.offset_top = -290
	dlg.offset_bottom = 290
	add_child(dlg)
	HOTKEYS._edge_title(dlg, title).offset_top = -18 # 50%-128 -18 50%+128 14
	# languageLabel and languageDropdown at 25 18 100%-25 50: the label as wide as its
	# text, the dropdown 10 px after it. The lobby's pages come in one language.
	var lang := HBoxContainer.new()
	lang.add_theme_constant_override("separation", 10)
	_place(lang, 25, 18, -25, 50, false)
	dlg.add_child(lang)
	var label := Label.new()
	label.text = tr("Language")
	label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	HOTKEYS._label_text(label)
	lang.add_child(label)
	var drop := RtsoSafeSelect.new()
	drop.name = "LanguageDropdown"
	drop.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	drop.add_item("English")
	drop.select(0)
	lang.add_child(drop)
	# mainTextPanel (ModernFade) holding mainText (ModernTextPanel) at 20 60 100%-20 100%-50.
	var shade := ColorRect.new()
	shade.color = Color8(0, 0, 0, 85)
	_place(shade, 20, 60, -20, -50, true)
	dlg.add_child(shade)
	var body := RichTextLabel.new()
	body.name = "TermsText"
	body.text = text
	body.add_theme_color_override("default_color", Color.WHITE)
	var sb: StyleBoxFlat = MODERN._dark_box(MODERN.GOLD, MODERN.DARK_BOX, 8)
	sb.content_margin_top = 8
	sb.content_margin_bottom = 8
	body.add_theme_stylebox_override("normal", sb)
	body.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shade.add_child(body)
	# cancelButton and connectButton: 160 px ModernButtonRed at 100%-355 100%-45, 10 apart.
	for spec in [["Decline", cancel_button_text, 0, _answer.bind(false)], ["Accept", ok_button_text, 170, _answer.bind(true)]]:
		var b := Button.new()
		b.name = spec[0]
		b.text = spec[1]
		b.anchor_left = 1.0
		b.anchor_right = 1.0
		b.anchor_top = 1.0
		b.anchor_bottom = 1.0
		b.offset_left = -355 + spec[2]
		b.offset_right = -355 + spec[2] + 160
		b.offset_top = -45
		b.offset_bottom = -16
		b.pressed.connect(spec[3])
		dlg.add_child(b)

func _place(c: Control, l: float, t: float, r: float, b: float, to_bottom: bool) -> void:
	c.anchor_right = 1.0
	c.anchor_bottom = 1.0 if to_bottom else 0.0
	c.offset_left = l
	c.offset_top = t
	c.offset_right = r
	c.offset_bottom = b

func _answer(accepted: bool) -> void:
	if accepted:
		confirmed.emit()
	else:
		canceled.emit()

## cancelButton hotkey="cancel".
func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo and "cancel" in _hotkeys.fired(event):
		get_viewport().set_input_as_handled()
		_answer(false)
