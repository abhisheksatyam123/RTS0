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
	for c in ref().get_civs():
		if int(c["id"]) == civ:
			return c["code"]
	return ""

## 0 A.D. Identity text of a roster unit ({tooltip, history, classes, builds}).
static func unit_text(unit_id: String) -> Dictionary:
	return text().get("units", {}).get(unit_id, {})

## A wall, palisade or siege wall is shown as its WallSet (TemplateParser.getEntity): the
## set's name, tooltip, classes and icon over the long piece's stats and auras, no turrets.
static func structure_text(type: int, civ: int) -> Dictionary:
	var e: Dictionary = text().get("structures", {}).get(civ_code(civ), {}).get(str(type), {})
	if not e.has("wallset"):
		return e
	var w := e.duplicate(true)
	w.merge(e["wallset"], true)
	w.erase("wallset")
	w.get("viewer", {}).erase("turrets")
	return w

static func tech_text(template_name: String) -> Dictionary:
	return text().get("techs", {}).get(template_name, {})

static func civ_history(civ: int) -> String:
	# 0.28's History keeps "\n" escapes, which its GUI turns into line breaks.
	return text().get("civs", {}).get(civ_code(civ), {}).get("history", "").replace("\\n", "\n")

## EntityBox caption: the specific name when names show specific first (the default
## gui.session.howtoshownames), else the generic one; 0.28's names over RTS0's.
static func caption_name(generic: String, extra: Dictionary) -> String:
	var g: String = extra.get("generic_name", "")
	var sp: String = extra.get("specific_name", "")
	if g == "":
		g = generic
	if sp != "" and (name_mode == 0 or name_mode == 2):
		return sp
	return g

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

const ICON_ROOT := "res://assets/textures/ui/archive/portraits/"

## The 0.28 Identity/Icon (or technology icon) of a reference entry, else its RTS0
## portrait: kind "unit" ({id}), "structure" ({type}, for civ) or "tech" ({template}).
static func icon_for(kind: String, entry: Dictionary, civ: int = 0) -> Texture2D:
	var icon := ""
	match kind:
		"unit": icon = unit_text(entry.get("id", "")).get("icon", "")
		"structure": icon = structure_text(int(entry.get("type", -1)), civ).get("icon", "")
		"tech":
			icon = tech_text(entry.get("template", "")).get("icon", "")
			if icon != "":
				icon = "technologies/" + icon
	if icon != "":
		var t := texture(ICON_ROOT + icon)
		if t:
			return t
	return texture(entry.get("portrait", ""))

static func texture(path: String) -> Texture2D:
	if path == "":
		return null
	if ResourceLoader.exists(path):
		var res = load(path)
		if res != null:
			return res
	# Not imported yet (a new image before the editor has opened the project): read the file.
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

## 0.28 CivSelectDropdown: civs sorted by name (sortNameIgnoreCase), item ids are civ ids.
static func civ_select(parent_handler: Callable) -> Control:
	var sel = RtsoSafeSelect.new()
	sel.custom_minimum_size = Vector2(240, 30)
	var civs: Array = ref().get_civs()
	civs.sort_custom(func(a, b): return str(a["name"]).nocasecmp_to(str(b["name"])) < 0)
	for c in civs:
		sel.add_item(c["name"], int(c["id"]))
	sel.item_selected.connect(parent_handler)
	return sel

## CivSelectDropdown.prototype.Caption, the heading left of the dropdown.
static func civ_heading() -> Label:
	var l := Label.new()
	l.text = "Civilization:"
	return l

## Cost and time under the civ's own bonuses (0.28 TemplateParser applies the civ's
## auto-researched techs and player auras): kind "unit" or "structure", or "tech"
## researched at the structure type `researcher` (Researcher/TechCostMultiplier).
static func civ_costed(d: Dictionary, kind: String, civ: int, researcher: int = -1) -> Dictionary:
	var c: Dictionary = text().get("civs", {}).get(civ_code(civ), {})
	var f = null
	match kind:
		"unit": f = c.get("cost_mult", {}).get(str(d.get("id", "")))
		"structure": f = c.get("cost_mult", {}).get("s%d" % int(d.get("type", -1)))
		"tech": f = c.get("tech_cost_mult", {}).get(str(researcher))
	if f == null or typeof(d.get("cost")) != TYPE_DICTIONARY:
		return d
	var out := d.duplicate()
	var cost: Dictionary = d["cost"].duplicate()
	for i in 4:
		var r: String = ["food", "wood", "stone", "metal"][i]
		cost[r] = roundi(float(cost.get(r, 0)) * float(f[i]))
	out["cost"] = cost
	if d.has("time"):
		out["time"] = float(d["time"]) * float(f[4])
	return out
