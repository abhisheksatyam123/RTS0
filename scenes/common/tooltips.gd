extends CanvasLayer
## Tooltips for every page, drawn on this layer instead of Godot's PopupPanel
## tooltips, which Godot 4.3 can't use here (they disconnect the root Window,
## so project.godot keeps gui/timers/tooltip_delay_sec at an hour).
##
## 0 A.D. 0.28 gui/common/setup.xml: snToolTip on the menus (delay 500 ms,
## maxwidth 400, offset 16 32, black text on white with a black border) and
## sessionToolTip in a match (delay 100 ms, white text on the dark session
## sprite). The hovered control's get_tooltip() decides the text, so per-item
## tooltips of lists and trees work, and a control's _make_custom_tooltip()
## replaces the text as Godot's own tooltips would.

const MENU_DELAY := 0.5
const SESSION_DELAY := 0.1
const MAX_WIDTH := 400.0
const OFFSET := Vector2(16, 32)
const BUFFER := 4.0

var _panel: PanelContainer
var _label: RichTextLabel
var _menu_style: StyleBoxFlat
var _session_style: StyleBoxFlat
var _custom: Control
var _owner: Control
var _text := ""
var _hover_time := 0.0

func _ready() -> void:
	layer = 128
	process_mode = Node.PROCESS_MODE_ALWAYS
	_menu_style = _style(Color(1, 1, 1, 0.95), Color(0, 0, 0))
	_session_style = _style(Color(0.06, 0.07, 0.09, 0.92), Color(0.45, 0.42, 0.32))
	_panel = PanelContainer.new()
	_panel.name = "Tooltip"
	_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_panel.visible = false
	add_child(_panel)
	_label = RichTextLabel.new()
	_label.bbcode_enabled = true
	_label.fit_content = true
	_label.scroll_active = false
	_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_label.add_theme_font_size_override("normal_font_size", 14)
	_panel.add_child(_label)

static func _style(bg: Color, border: Color) -> StyleBoxFlat:
	var sb := StyleBoxFlat.new()
	sb.bg_color = bg
	sb.border_color = border
	sb.set_border_width_all(1)
	sb.set_content_margin_all(5)
	return sb

## The tooltip text shown now ("" when hidden), for tests.
func shown_text() -> String:
	return _text if _panel.visible else ""

func _in_session() -> bool:
	var scene := get_tree().current_scene
	return scene != null and scene.has_method("is_game_paused")

func _process(delta: float) -> void:
	var vp := get_viewport()
	var c := vp.gui_get_hovered_control()
	var mouse := vp.get_mouse_position()
	var text := ""
	if c != null and c.is_visible_in_tree() and not Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT) \
			and not Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT):
		text = c.get_tooltip(c.get_global_transform_with_canvas().affine_inverse() * mouse)
	if text.strip_edges().is_empty() and not (c != null and c.has_method("_make_custom_tooltip") and not text.is_empty()):
		_hide()
		return
	if c != _owner or text != _text:
		_hide()
		_owner = c
		_text = text
		_hover_time = 0.0
	_hover_time += delta if Engine.time_scale <= 0.0 else delta / Engine.time_scale
	if not _panel.visible and _hover_time >= (SESSION_DELAY if _in_session() else MENU_DELAY):
		_show(c, text)
	if _panel.visible:
		_place(mouse, vp.get_visible_rect().size)

func _show(c: Control, text: String) -> void:
	var session := _in_session()
	_panel.add_theme_stylebox_override("panel", _session_style if session else _menu_style)
	if c.has_method("_make_custom_tooltip"):
		var custom = c._make_custom_tooltip(text)
		if custom is Control:
			_custom = custom
			_custom.mouse_filter = Control.MOUSE_FILTER_IGNORE
			_label.visible = false
			_panel.add_child(_custom)
	if _custom == null:
		_label.visible = true
		_label.add_theme_color_override("default_color", Color.WHITE if session else Color.BLACK)
		var font := _label.get_theme_font("normal_font")
		var widest := 0.0
		for line in text.split("\n"):
			widest = maxf(widest, font.get_string_size(line, HORIZONTAL_ALIGNMENT_LEFT, -1, 14).x)
		_label.custom_minimum_size = Vector2(minf(widest + 4.0, MAX_WIDTH), 0)
		_label.text = text
	_panel.reset_size()
	_panel.visible = true

## setup.xml offset 16 32 below the cursor, kept on screen (buffer_zone 4).
func _place(mouse: Vector2, screen: Vector2) -> void:
	var size := _panel.get_combined_minimum_size()
	var pos := mouse + OFFSET
	if pos.x + size.x > screen.x - BUFFER:
		pos.x = maxf(BUFFER, screen.x - BUFFER - size.x)
	if pos.y + size.y > screen.y - BUFFER:
		pos.y = maxf(BUFFER, mouse.y - size.y - BUFFER)
	_panel.position = pos
	_panel.size = size

func _hide() -> void:
	if _custom:
		_custom.queue_free()
		_custom = null
	_panel.visible = false
	_owner = null
	_text = ""
	_hover_time = 0.0
