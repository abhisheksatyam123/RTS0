extends Control
## In-game manual, ported from 0 A.D. gui/manual (manual.js, manual.xml): the
## text of gui/manual/intro.txt with every "hotkey.<name>" replaced by the
## player's current binding, in a dialog with Close and View Online. It opens
## over the page that called open() (Engine.PushGuiPage). The text is copied
## to data/reference/manual.json by tools/extract_0ad_loading.py.

const DATA := "res://data/reference/manual.json"
const ONLINE := "https://trac.wildfiregames.com/wiki/0adManual"

signal closed

var text: RichTextLabel

static func open(parent: Node) -> Control:
	var page = load("res://scenes/reference/manual.gd").new()
	page.name = "Manual"
	parent.add_child(page)
	return page

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	var fade := ColorRect.new() # "ModernFade"
	fade.color = Color(0, 0, 0, 0.6)
	fade.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(fade)
	var panel := Panel.new()
	panel.anchor_left = 0.5
	panel.anchor_right = 0.5
	panel.anchor_top = 0.5
	panel.anchor_bottom = 0.5
	panel.offset_left = -466
	panel.offset_right = 466
	panel.offset_top = -316
	panel.offset_bottom = 316
	add_child(panel)
	var title := Label.new()
	title.text = "Manual"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.position = Vector2(466 - 128, 4)
	title.size = Vector2(256, 28)
	title.add_theme_font_size_override("font_size", 18)
	panel.add_child(title)
	text = RichTextLabel.new()
	text.name = "MainText"
	text.bbcode_enabled = true
	text.selection_enabled = true
	text.position = Vector2(20, 36)
	text.size = Vector2(892, 632 - 36 - 58)
	text.text = manual_bbcode()
	panel.add_child(text)
	var close := Button.new()
	close.text = "Close"
	close.position = Vector2(932 - 408, 632 - 52)
	close.size = Vector2(190, 28)
	close.pressed.connect(close_page)
	panel.add_child(close)
	var online := Button.new()
	online.text = "View Online"
	online.position = Vector2(932 - 214, 632 - 52)
	online.size = Vector2(190, 28)
	online.pressed.connect(func(): OS.shell_open(ONLINE))
	panel.add_child(online)

func close_page() -> void:
	closed.emit()
	queue_free()

## The manual over a match (MenuButtons Manual): its own layer above the HUD,
## working while the game is paused.
static func open_overlay(parent: Node) -> Control:
	var layer := CanvasLayer.new()
	layer.layer = 30
	layer.process_mode = Node.PROCESS_MODE_ALWAYS
	parent.add_child(layer)
	var page = open(layer)
	page.process_mode = Node.PROCESS_MODE_ALWAYS
	page.closed.connect(layer.queue_free)
	return page

func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and "cancel" in RtsoHotkeyConfig.new().fired(event):
		get_viewport().set_input_as_handled()
		close_page()

## intro.txt with its hotkey names bound, as BBCode.
static func manual_bbcode() -> String:
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(DATA)) if FileAccess.file_exists(DATA) else null
	var intro := str(parsed.get("intro", "")) if typeof(parsed) == TYPE_DICTIONARY else ""
	var hotkeys: Dictionary = RtsoHotkeyConfig.new().get_hotkey_map()
	var re := RegEx.create_from_string("hotkey\\.([a-z0-9_\\.]+[a-z0-9_])")
	var out := ""
	var pos := 0
	for m in re.search_all(intro):
		out += intro.substr(pos, m.get_start() - pos)
		var combos: PackedStringArray = hotkeys.get(m.get_string(1), PackedStringArray())
		out += ", ".join(combos) if not combos.is_empty() else "unassigned"
		pos = m.get_end()
	out += intro.substr(pos)
	return font_tags_to_bbcode(out.replace("[", "[lb]").replace("[lb]font=", "[font="))

## 0 A.D. GUI text sets a font until the next [font="..."] tag, for example
## [font="sans-bold-16"]; BBCode needs each run closed.
static func font_tags_to_bbcode(s: String) -> String:
	var re := RegEx.create_from_string("\\[font=\"([a-z-]+)-(\\d+)\"\\]")
	var out := ""
	var pos := 0
	var close := ""
	for m in re.search_all(s):
		out += s.substr(pos, m.get_start() - pos) + close
		var bold := m.get_string(1).contains("bold")
		out += "[font_size=%s]" % m.get_string(2) + ("[b]" if bold else "")
		close = ("[/b]" if bold else "") + "[/font_size]"
		pos = m.get_end()
	return out + s.substr(pos) + close
