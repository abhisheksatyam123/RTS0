extends CanvasLayer
## Tooltips for every page, drawn on this layer instead of Godot's PopupPanel
## tooltips, which Godot 4.3 can't use here (they disconnect the root Window,
## so project.godot keeps gui/timers/tooltip_delay_sec at an hour).
## The reference pages pick their own style through a "tooltip_style" meta on
## an ancestor: reference/common/setup.xml referenceTooltip (50 ms, maxwidth
## 480, offset 16 24) and civinfo/setup.xml civInfoTooltip (50 ms, sans-12,
## maxwidth 440, offset 16 32, buffer_zone 10), both white on bkTooltip; the
## main menu's buttons use pregame/menupanel.xml pgToolTip: no delay, in a
## fixed TranslucentPanel at the top right (size "100%-313 25 100% 200",
## white sans-bold-stroke-14, buffer_zone 10).
##
## 0 A.D. 0.28 gui/common/setup.xml: snToolTip on the menus (delay 500 ms,
## maxwidth 400, offset 16 32, black text on white with a black border) and
## sessionToolTip in a match (delay 100 ms, white sans-14 on
## BackgroundSessionTooltip: dark brown 30 20 10 with a 1 px gold 237 227 167
## border). session/setup.xml adds sessionToolTipBold (sans-bold-14) and the
## bottom-anchored sessionToolTipBottom/-Bold (offset -4 -4, the box's bottom
## left at the cursor) that the panel buttons, minimap and research progress
## pick through the same meta. The hovered control's get_tooltip() decides the text, so per-item
## tooltips of lists and trees work, and a control's _make_custom_tooltip()
## replaces the text as Godot's own tooltips would.

const MENU_DELAY := 0.5
const SESSION_DELAY := 0.1
const MAX_WIDTH := 400.0
const OFFSET := Vector2(16, 32)
const BUFFER := 4.0
const REFERENCE_STYLES := {
	"reference": {"delay": 0.05, "width": 480.0, "offset": Vector2(16, 24), "font": 14, "buffer": 4.0},
	"civinfo": {"delay": 0.05, "width": 440.0, "offset": Vector2(16, 32), "font": 12, "buffer": 10.0},
	"session_bold": {"delay": 0.1, "width": 400.0, "offset": Vector2(16, 32), "font": 14, "bold": true},
	"session_bottom": {"delay": 0.1, "width": 400.0, "offset": Vector2(-4, -4), "font": 14, "bottom": true},
	"session_bottom_bold": {"delay": 0.1, "width": 400.0, "offset": Vector2(-4, -4), "font": 14, "bottom": true, "bold": true},
	"pg": {"delay": 0.0, "width": 293.0, "font": 14, "fixed": Rect2(-313, 25, 313, 175)},
}

var _panel: PanelContainer
var _label: RichTextLabel
var _menu_style: StyleBoxFlat
var _session_style: StyleBoxFlat
var _pg_style: StyleBoxFlat
var _custom: Control
var _owner: Control
var _text := ""
var _hover_time := 0.0
var _ref: Dictionary = {} # the hovered page's REFERENCE_STYLES entry, empty for the menu/session styles
var _BBCODE := RegEx.create_from_string("\\[/?(b|i|u|color|img|font_size)[^\\]]*\\](?:[^\\[]*\\[/img\\])?")

func _ready() -> void:
	layer = 128
	process_mode = Node.PROCESS_MODE_ALWAYS
	_menu_style = _style(Color(1, 1, 1, 0.95), Color(0, 0, 0))
	_session_style = _style(Color(30 / 255.0, 20 / 255.0, 10 / 255.0), Color(237 / 255.0, 227 / 255.0, 167 / 255.0))
	_pg_style = _style(Color(0, 0, 0, 85.0 / 255.0), Color(0, 0, 0, 0)) # BackgroundTranslucent
	_pg_style.set_border_width_all(0)
	_pg_style.set_content_margin_all(10)
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

## A formatted tooltip as plain text, for tests: resource icons read as their names
## ("Food 50"), the time icon as "10 seconds", and the other BBCode goes.
static func plain(text: String) -> String:
	var t := text
	var time := RegEx.create_from_string("\\[img[^\\]]*\\][^\\[]*time_small\\.png\\[/img\\] (\\d+)")
	for m in time.search_all(t):
		t = t.replace(m.get_string(), m.get_string(1) + (" second" if m.get_string(1) == "1" else " seconds"))
	var icon := RegEx.create_from_string("\\[img[^\\]]*\\][^\\[]*/(\\w+)_small\\.png\\[/img\\]")
	for m in icon.search_all(t):
		t = t.replace(m.get_string(), m.get_string(1).capitalize())
	t = RegEx.create_from_string("\\[/?(b|i|u|color|font_size)[^\\]]*\\]").sub(t, "", true)
	return t.replace("[lb]", "[").replace("[rb]", "]")

## The style shown now, for tests: {"bold", "bottom", "rect"}.
func shown_style() -> Dictionary:
	return {"bold": _ref.get("bold", false), "bottom": _ref.get("bottom", false),
		"rect": Rect2(_panel.position, _panel.size), "bg": _session_style.bg_color}

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
		_ref = _reference_style(c)
	_hover_time += delta if Engine.time_scale <= 0.0 else delta / Engine.time_scale
	var delay: float = _ref.get("delay", SESSION_DELAY if _in_session() else MENU_DELAY)
	if not _panel.visible and _hover_time >= delay:
		_show(c, text)
	if _panel.visible:
		_place(mouse, vp.get_visible_rect().size)

## The REFERENCE_STYLES entry named by the nearest "tooltip_style" meta above c.
static func _reference_style(c: Node) -> Dictionary:
	while c != null:
		if c.has_meta("tooltip_style"):
			return REFERENCE_STYLES.get(c.get_meta("tooltip_style"), {})
		c = c.get_parent()
	return {}

func _show(c: Control, text: String) -> void:
	var dark := _in_session() or not _ref.is_empty()
	var text_color := Color.WHITE if dark else Color.BLACK
	var size: int = _ref.get("font", 14)
	var max_width: float = _ref.get("width", MAX_WIDTH)
	var pg: bool = _ref.has("fixed")
	_panel.add_theme_stylebox_override("panel", _pg_style if pg else _session_style if dark else _menu_style)
	if c.has_method("_make_custom_tooltip"):
		var custom = c._make_custom_tooltip(text)
		if custom is Control:
			_custom = custom
			_custom.mouse_filter = Control.MOUSE_FILTER_IGNORE
			# Godot's own tooltip panel is dark, so a custom label without a colour
			# would be white on the white menu panel.
			if _custom is RichTextLabel and not _custom.has_theme_color_override("default_color"):
				_custom.add_theme_color_override("default_color", text_color)
			_label.visible = false
			_panel.add_child(_custom)
	if _custom == null:
		_label.visible = true
		_label.add_theme_color_override("default_color", text_color)
		_label.add_theme_font_size_override("normal_font_size", size)
		# sans-bold-stroke-14 on the menu panel.
		_label.add_theme_constant_override("outline_size", 3 if pg else 0)
		_label.add_theme_color_override("font_outline_color", Color.BLACK)
		if pg or _ref.get("bold", false):
			_label.add_theme_font_override("normal_font", _label.get_theme_font("bold_font"))
		else:
			_label.remove_theme_font_override("normal_font")
		var font := _label.get_theme_font("normal_font")
		var widest := 0.0
		# Measured without its BBCode (tooltips.js g_TooltipTextFormats); an inline icon is 16 wide.
		for line in text.split("\n"):
			var icons := line.count("[img")
			var plain := _BBCODE.sub(line, "", true).replace("[lb]", "[").replace("[rb]", "]")
			widest = maxf(widest, font.get_string_size(plain, HORIZONTAL_ALIGNMENT_LEFT, -1, size).x + icons * 16.0)
		_label.custom_minimum_size = Vector2(max_width if pg else minf(widest + 4.0, max_width), 0)
		_label.text = text
	_panel.reset_size()
	_panel.visible = true

## setup.xml offset 16 32 below the cursor, kept on screen (buffer_zone 4).
func _place(mouse: Vector2, screen: Vector2) -> void:
	if _ref.has("fixed"):
		var r: Rect2 = _ref["fixed"]
		_panel.position = Vector2(screen.x + r.position.x, r.position.y)
		_panel.size = Vector2(r.size.x, maxf(r.size.y, _panel.get_combined_minimum_size().y))
		return
	var size := _panel.get_combined_minimum_size()
	var buffer: float = _ref.get("buffer", BUFFER)
	var pos: Vector2 = mouse + _ref.get("offset", OFFSET)
	if _ref.get("bottom", false):
		# anchor="bottom": the offset places the box's bottom edge.
		pos.y -= size.y
		if pos.y < buffer:
			pos.y = buffer
	if pos.x + size.x > screen.x - buffer:
		pos.x = maxf(buffer, screen.x - buffer - size.x)
	if pos.y + size.y > screen.y - buffer:
		pos.y = maxf(buffer, mouse.y - size.y - buffer)
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
