extends RefCounted
## Shared helpers for the reference pages (port of 0 A.D. gui/reference/common,
## GPL-2.0-or-later): the game's own numbers come from RtsoReference, the 0 A.D.
## tooltip, history and class text from data/reference/templates.json
## (tools/extract_0ad_reference.py, CC BY-SA 3.0).

const TEXT_PATH := "res://data/reference/templates.json"
const PHASES := ["", "Village", "Town", "City"]
const HEADING := "#ffd36b"
const DIM := "#aaaaaa"

static var _text: Dictionary = {}
static var _ref: RtsoReference = null

static func ref() -> RtsoReference:
	if _ref == null:
		_ref = RtsoReference.new()
	return _ref

static func text() -> Dictionary:
	if _text.is_empty() and FileAccess.file_exists(TEXT_PATH):
		var d = JSON.parse_string(FileAccess.get_file_as_string(TEXT_PATH))
		if typeof(d) == TYPE_DICTIONARY:
			_text = d
	return _text

static func civ_code(civ: int) -> String:
	var civs: Array = ref().get_civs()
	return civs[civ]["code"] if civ >= 0 and civ < civs.size() else ""

## 0 A.D. Identity text of a roster unit ({tooltip, history, classes, builds}).
static func unit_text(unit_id: String) -> Dictionary:
	return text().get("units", {}).get(unit_id, {})

static func structure_text(type: int, civ: int) -> Dictionary:
	return text().get("structures", {}).get(civ_code(civ), {}).get(str(type), {})

static func tech_text(template_name: String) -> Dictionary:
	return text().get("techs", {}).get(template_name, {})

static func civ_history(civ: int) -> String:
	return text().get("civs", {}).get(civ_code(civ), {}).get("history", "")

static func phase_name(phase: int) -> String:
	return PHASES[clampi(phase, 0, 3)]

## 0 A.D. gui.session.howtoshownames: 0 specific first, 1 generic first,
## 2 only specific, 3 only generic.
static var name_mode := 0

static func entity_name(generic: String, specific: String) -> String:
	var mode := name_mode
	if specific == "" or specific == generic:
		return generic
	match mode:
		1: return "%s (%s)" % [generic, specific]
		2: return specific
		3: return generic
	return "%s (%s)" % [specific, generic]

static func texture(path: String) -> Texture2D:
	if path == "":
		return null
	if ResourceLoader.exists(path):
		return load(path)
	if FileAccess.file_exists(path):
		var img := Image.load_from_file(path)
		if img:
			return ImageTexture.create_from_image(img)
	return null

## A RichTextLabel link to another reference entry; the pages route
## meta_clicked "unit:<tmpl>", "structure:<type>" and "tech:<id>" to the viewer.
static func link(kind: String, id: int, label: String) -> String:
	return "[url=%s:%d][color=#9fd3ff]%s[/color][/url]" % [kind, id, label]

static func heading(t: String, size: int = 16) -> String:
	return "[font_size=%d][color=%s][b]%s[/b][/color][/font_size]\n" % [size, HEADING, t]

static func rich_text() -> RichTextLabel:
	var rt := RichTextLabel.new()
	rt.bbcode_enabled = true
	rt.scroll_active = true
	rt.selection_enabled = false
	rt.size_flags_vertical = Control.SIZE_EXPAND_FILL
	rt.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	return rt

static func civ_select(parent_handler: Callable) -> Control:
	var sel = RtsoSafeSelect.new()
	sel.custom_minimum_size = Vector2(240, 30)
	for c in ref().get_civs():
		sel.add_item(c["name"])
	sel.item_selected.connect(parent_handler)
	return sel
