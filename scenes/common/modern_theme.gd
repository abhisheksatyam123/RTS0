extends RefCounted
## 0 A.D. 0.28's "modern" dialog skin (mod gui/common/modern/styles.xml and
## sprites.xml) as a Godot Theme that the dialog pages set on their root:
## ModernButtonRed buttons (over: add_color 60 42 42, disabled: grayscale),
## ModernTickBox tick boxes, ModernSlider (the tick-on knob on a gold line),
## ModernInput and ModernDropDown dark boxes with gold top and bottom edges,
## ModernList, the vertical and horizontal tab buttons, ModernDialog's tiled
## background and white sans-bold-stroke labels. Types the skin leaves out
## fall back to the project theme.

const DIR := "res://assets/textures/ui/global/modern/"
const GOLD := Color8(208, 174, 133) # the mod's gold-separator.png
const WHITE_EDGE := Color(1, 1, 1, 0.8) # white-separator.png
const DARK_BOX := Color8(12, 12, 12, 100) # ModernDarkBoxGold background

static var _theme: Theme

## The shared skin; pages set it with `theme = ModernTheme.theme()`.
static func theme() -> Theme:
	if _theme == null:
		_theme = _build()
	return _theme

static func _tex(file: String) -> Texture2D:
	return load(DIR + file) if ResourceLoader.exists(DIR + file) else null

static func _button_box(file: String) -> StyleBox:
	var t := _tex(file)
	if t == null:
		return StyleBoxFlat.new()
	var sb := StyleBoxTexture.new()
	sb.texture = t
	sb.set_texture_margin_all(8) # the 8 px corners and edges of the red-unselected pieces
	sb.content_margin_left = 10
	sb.content_margin_right = 10
	sb.content_margin_top = 4
	sb.content_margin_bottom = 4
	return sb

## ModernDarkBox with ModernDarkBoxGoldBorder (or the white border of a selection).
static func _dark_box(edge: Color, bg := DARK_BOX, margin := 6.0) -> StyleBoxFlat:
	var sb := StyleBoxFlat.new()
	sb.bg_color = bg
	sb.border_color = edge
	sb.border_width_top = 1
	sb.border_width_bottom = 1
	sb.content_margin_left = margin
	sb.content_margin_right = margin
	sb.content_margin_top = 3
	sb.content_margin_bottom = 3
	return sb

## ModernTab{Vertical,Horizontal}{Background,Foreground}: gold edges, 1 px behind, 2 px in front.
static func _tab_box(front: bool) -> StyleBoxFlat:
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color8(255, 255, 255, 40) if front else Color8(50, 35, 0, 120)
	sb.border_color = Color.GOLD
	sb.set_border_width_all(2 if front else 1)
	sb.set_content_margin_all(6)
	return sb

static func _label_colors(t: Theme, type: String) -> void:
	t.set_color("font_color", type, Color.WHITE)
	t.set_color("font_outline_color", type, Color.BLACK)
	t.set_constant("outline_size", type, 3) # sans-bold-stroke

static func _build() -> Theme:
	var t := Theme.new()
	# ModernButtonRed: sprite_pressed is the plain sprite too.
	var red := _button_box("button-red.png")
	for type in ["Button"]:
		t.set_stylebox("normal", type, red)
		t.set_stylebox("pressed", type, red)
		t.set_stylebox("focus", type, StyleBoxEmpty.new())
		t.set_stylebox("hover", type, _button_box("button-red-over.png"))
		t.set_stylebox("hover_pressed", type, _button_box("button-red-over.png"))
		t.set_stylebox("disabled", type, _button_box("button-red-disabled.png"))
		for c in ["font_color", "font_hover_color", "font_pressed_color", "font_hover_pressed_color", "font_focus_color"]:
			t.set_color(c, type, Color.WHITE)
		t.set_color("font_disabled_color", type, Color8(210, 210, 210, 160))
		t.set_color("font_outline_color", type, Color.BLACK)
		t.set_constant("outline_size", type, 3)

	# The tab buttons (tab_buttons.js placeTabButtons): a type variation of Button.
	for variation in ["ModernTabButtonVertical", "ModernTabButtonHorizontal"]:
		t.set_type_variation(variation, "Button")
		var back := _tab_box(false)
		var front := _tab_box(true)
		t.set_stylebox("normal", variation, back)
		t.set_stylebox("hover", variation, back)
		t.set_stylebox("pressed", variation, front)
		t.set_stylebox("hover_pressed", variation, front)
		t.set_stylebox("disabled", variation, back)
	# TabContainer / TabBar tabs: ModernTabHorizontal.
	for type in ["TabContainer", "TabBar"]:
		t.set_stylebox("tab_unselected", type, _tab_box(false))
		t.set_stylebox("tab_hovered", type, _tab_box(false))
		t.set_stylebox("tab_selected", type, _tab_box(true))
		t.set_stylebox("tab_disabled", type, _tab_box(false))
		t.set_color("font_selected_color", type, Color.WHITE)
		t.set_color("font_unselected_color", type, Color(0.85, 0.85, 0.85))
		t.set_color("font_hovered_color", type, Color.WHITE)
		t.set_color("font_outline_color", type, Color.BLACK)
		t.set_constant("outline_size", type, 3)
	t.set_stylebox("panel", "TabContainer", _dark_box(GOLD, DARK_BOX, 8))

	# ModernTickBox: 22 px tick-on / tick-off, grayscale when disabled (TickDisabledOff adds 35 grey).
	for type in ["CheckBox", "CheckButton"]:
		t.set_icon("checked", type, _tex("tick-on-22.png"))
		t.set_icon("unchecked", type, _tex("tick-off-22.png"))
		t.set_icon("checked_disabled", type, _tex("tick-on-22-disabled.png"))
		t.set_icon("unchecked_disabled", type, _tex("tick-off-22-disabled.png"))
		for s in ["normal", "pressed", "hover", "hover_pressed", "disabled", "focus"]:
			t.set_stylebox(s, type, StyleBoxEmpty.new())
		_label_colors(t, type)

	# ModernSlider: the tick-on knob (button_width 20) on a 2 px gold line.
	var line := StyleBoxFlat.new()
	line.bg_color = GOLD
	line.content_margin_top = 1
	line.content_margin_bottom = 1
	t.set_stylebox("slider", "HSlider", line)
	t.set_stylebox("grabber_area", "HSlider", line)
	t.set_stylebox("grabber_area_highlight", "HSlider", line)
	t.set_icon("grabber", "HSlider", _tex("slider-button.png"))
	t.set_icon("grabber_highlight", "HSlider", _tex("slider-button.png"))
	t.set_icon("grabber_disabled", "HSlider", _tex("tick-on-22-disabled.png"))

	# ModernInput / ModernDropDown / ModernTextPanel: the dark box with gold edges.
	for type in ["LineEdit", "TextEdit"]:
		t.set_stylebox("normal", type, _dark_box(GOLD))
		t.set_stylebox("focus", type, _dark_box(GOLD))
		t.set_stylebox("read_only", type, _dark_box(GOLD.darkened(0.4)))
		t.set_color("font_color", type, Color.WHITE)
		t.set_color("selection_color", type, Color8(150, 0, 0))
		t.set_color("font_placeholder_color", type, Color.GRAY)
	for type in ["RtsoSafeSelect", "OptionButton"]:
		t.set_stylebox("normal", type, _dark_box(GOLD, DARK_BOX, 8))
		t.set_stylebox("pressed", type, _dark_box(GOLD, DARK_BOX, 8))
		t.set_stylebox("hover", type, _dark_box(WHITE_EDGE, DARK_BOX, 8))
		t.set_stylebox("hover_pressed", type, _dark_box(WHITE_EDGE, DARK_BOX, 8))
		t.set_stylebox("disabled", type, _dark_box(GOLD.darkened(0.5), DARK_BOX, 8))
		t.set_stylebox("focus", type, StyleBoxEmpty.new())
		for c in ["font_color", "font_hover_color", "font_pressed_color", "font_hover_pressed_color", "font_focus_color"]:
			t.set_color(c, type, Color.WHITE)
		t.set_color("font_disabled_color", type, Color8(140, 140, 140))
		t.set_color("font_outline_color", type, Color.BLACK)
		t.set_constant("outline_size", type, 2)
		t.set_icon("arrow", type, _tex("dropdown-arrow.png")) # ModernDropDown's dropdown_buffer arrow

	# ModernList / ModernSortedList: the dark box with a gold bottom edge, a white-edged selection.
	for type in ["ItemList", "Tree"]:
		t.set_stylebox("panel", type, _dark_box(GOLD))
		t.set_stylebox("focus", type, StyleBoxEmpty.new())
		var sel := _dark_box(WHITE_EDGE, Color8(255, 255, 255, 30), 2)
		t.set_stylebox("selected", type, sel)
		t.set_stylebox("selected_focus", type, sel)
		t.set_stylebox("cursor", type, StyleBoxEmpty.new())
		t.set_stylebox("cursor_unfocused", type, StyleBoxEmpty.new())
		t.set_color("font_color", type, Color.WHITE)
		t.set_color("font_selected_color", type, Color.WHITE)
		t.set_color("font_outline_color", type, Color.BLACK)
		t.set_constant("outline_size", type, 2)
	t.set_stylebox("title_button_normal", "Tree", _dark_box(GOLD))

	# ModernScrollBar: 15 px, no edge buttons.
	for type in ["VScrollBar", "HScrollBar"]:
		var back := StyleBoxFlat.new()
		back.bg_color = Color(0, 0, 0, 0.35)
		back.set_content_margin_all(4)
		var grab := StyleBoxFlat.new()
		grab.bg_color = GOLD.darkened(0.2)
		grab.set_corner_radius_all(3)
		t.set_stylebox("scroll", type, back)
		t.set_stylebox("grabber", type, grab)
		t.set_stylebox("grabber_highlight", type, grab)
		t.set_stylebox("grabber_pressed", type, grab)

	# ModernDialog: the tiled modern background (its gold title bar and corner decorations aside).
	var bg := _tex("background.png")
	if bg:
		var dlg := StyleBoxTexture.new()
		dlg.texture = bg
		dlg.axis_stretch_horizontal = StyleBoxTexture.AXIS_STRETCH_MODE_TILE
		dlg.axis_stretch_vertical = StyleBoxTexture.AXIS_STRETCH_MODE_TILE
		dlg.set_content_margin_all(12)
		t.set_type_variation("ModernDialog", "Panel")
		t.set_stylebox("panel", "ModernDialog", dlg)
		t.set_type_variation("ModernDialogContainer", "PanelContainer")
		t.set_stylebox("panel", "ModernDialogContainer", dlg)

	# ModernLabelText and the tick boxes' captions: white sans-bold-stroke-14. Plain Labels
	# keep the page's face at 14: the bold face makes the Game Setup player rows
	# taller than one line. Buttons keep the page's face too: RTS0's dropdowns are Buttons.
	_label_colors(t, "Label")
	t.set_font_size("font_size", "Label", 14)
	t.set_font("font", "CheckBox", bold_font())
	t.set_font_size("font_size", "CheckBox", 14)
	return t

## ModernLabelText on one control: white sans-bold-stroke-14.
static func label_text(l: Control) -> Control:
	l.add_theme_font_override("font", bold_font())
	l.add_theme_font_size_override("font_size", 14)
	l.add_theme_color_override("font_color", Color.WHITE)
	l.add_theme_color_override("font_outline_color", Color.BLACK)
	l.add_theme_constant_override("outline_size", 3)
	return l

static var _bold: FontVariation

## sans-bold: the project font emboldened, as the other 0.28 bold faces in RTS0.
static func bold_font() -> FontVariation:
	if _bold == null:
		_bold = FontVariation.new()
		_bold.base_font = ThemeDB.fallback_font
		_bold.variation_embolden = 0.6
	return _bold

## ModernWindow: opaque 12 12 12 under the tiled background, the shadow at the top and
## bottom, the border lines and the title frame.
static func window() -> Control:
	var w := Control.new()
	w.name = "ModernWindow"
	w.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	w.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var back := ColorRect.new()
	back.color = Color8(12, 12, 12)
	back.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	w.add_child(back)
	_part(w, "background.png", [0, 0, 1, 1], [12, 22, -12, -12], TextureRect.STRETCH_TILE)
	_part(w, "shadow-high.png", [0, 1, 1, 1], [12, -268, -12, -12], TextureRect.STRETCH_SCALE)
	_part(w, "shadow-high.png", [0, 0, 1, 0], [12, 22, -12, 268], TextureRect.STRETCH_SCALE).flip_v = true
	_part(w, "border.png", [0, 0, 1, 0], [10, 18, -10, 26], TextureRect.STRETCH_SCALE)
	_part(w, "border.png", [0, 1, 1, 1], [10, -16, -10, -8], TextureRect.STRETCH_SCALE)
	_part(w, "titlebar-middle.png", [0.5, 0, 0.5, 0], [-128, 4, 128, 37], TextureRect.STRETCH_SCALE)
	_part(w, "titlebar-left.png", [0.5, 0, 0.5, 0], [-154, 4, -122, 37], TextureRect.STRETCH_SCALE)
	var right := _part(w, "titlebar-left.png", [0.5, 0, 0.5, 0], [122, 4, 154, 37], TextureRect.STRETCH_SCALE)
	right.flip_h = true
	right.flip_v = true
	return w

static func _part(parent: Control, file: String, anchors: Array, offsets: Array, stretch: int) -> TextureRect:
	var r := TextureRect.new()
	r.texture = _tex(file)
	r.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	r.stretch_mode = stretch
	r.mouse_filter = Control.MOUSE_FILTER_IGNORE
	r.anchor_left = anchors[0]
	r.anchor_top = anchors[1]
	r.anchor_right = anchors[2]
	r.anchor_bottom = anchors[3]
	r.offset_left = offsets[0]
	r.offset_top = offsets[1]
	r.offset_right = offsets[2]
	r.offset_bottom = offsets[3]
	parent.add_child(r)
	return r
