extends VBoxContainer
## Civilization Overview, port of 0 A.D. gui/reference/civinfo (CivInfoPage.js
## with its GameplaySection: civ bonuses, team bonuses, specific technologies,
## specific structures and heroes; and HistorySection, GPL-2.0-or-later).
## Bonus and history text is 0 A.D.'s (data/civs/civinfo.json and
## data/reference/templates.json, CC BY-SA 3.0); the lists come from the
## game's own tables, and each name opens the Template Viewer.

const Common := preload("res://scenes/reference/reference_common.gd")
const Viewer := preload("res://scenes/reference/template_viewer.gd")
const CIVINFO_PATH := "res://data/civs/civinfo.json"

signal civ_changed(civ: int)

var civ := 0
var viewer_host: Node = null
var lock_civ := false

var _civinfo := {} # code -> 0 A.D. civ JSON extract
var _select
var _emblem: TextureRect
var _gameplay: RichTextLabel
var _history: RichTextLabel

func _init() -> void:
	name = "Civilization"

func _ready() -> void:
	set_meta("tooltip_style", "civinfo") # 0.28 civInfoTooltip (tooltips.gd)
	add_theme_constant_override("separation", 8)
	var data = JSON.parse_string(FileAccess.get_file_as_string(CIVINFO_PATH)) if FileAccess.file_exists(CIVINFO_PATH) else null
	if typeof(data) == TYPE_ARRAY:
		for c in data:
			_civinfo[c["code"]] = c
	var top := HBoxContainer.new()
	add_child(top)
	_select = Common.civ_select(_on_civ_selected)
	_select.visible = not lock_civ
	var heading := Common.civ_heading()
	heading.visible = _select.visible
	top.add_child(heading)
	top.add_child(_select)
	_emblem = TextureRect.new()
	_emblem.custom_minimum_size = Vector2(56, 56)
	_emblem.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_emblem.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	top.add_child(_emblem)
	var cols := HBoxContainer.new()
	cols.size_flags_vertical = Control.SIZE_EXPAND_FILL
	cols.add_theme_constant_override("separation", 16)
	add_child(cols)
	_gameplay = Common.rich_text()
	_gameplay.size_flags_stretch_ratio = 1.6
	_gameplay.meta_clicked.connect(_on_meta)
	cols.add_child(_gameplay)
	_history = Common.rich_text()
	cols.add_child(_history)
	set_civ(civ)

func _on_civ_selected(index: int) -> void:
	var id: int = _select.get_item_id(index)
	set_civ(id)
	civ_changed.emit(id)

func _on_meta(meta) -> void:
	var parts := str(meta).split(":")
	if parts.size() == 2:
		Viewer.open(viewer_host if viewer_host else get_tree().current_scene, parts[0], int(parts[1]), civ)

func set_civ(index: int) -> void:
	civ = index
	if _gameplay == null:
		return
	_select.select(_select.get_item_index(index))
	var d: Dictionary = Common.ref().get_civ_info(civ)
	_emblem.texture = Common.texture(d.get("emblem", ""))
	_gameplay.text = gameplay_text(d)
	var hist := Common.civ_history(civ)
	# CivInfoPage.selectCiv: the civ's name (sans-bold-20) beside its emblem, then its History.
	_history.text = Common.heading(d["name"], 20) + ("\n" + hist if hist != "" else "")

## 0.28 CivInfoPage.formatEntry: "• Name (i)" with the info text on hover, and the
## description on the next line, indented.
static func entry(label: String, info: String, description: String) -> String:
	var t := "• [b]%s[/b]" % label
	if info != "":
		t += " [hint=\"%s\"][color=#9fd3ff](i)[/color][/hint]" % info.replace("\"", "\u201d").replace("]", ")")
	if description != "":
		t += "\n     " + description
	return t + "\n"

static func _heading(one: String, many: String, n: int) -> String:
	return Common.heading(one if n == 1 else many, 14)

## 0.28 GameplaySection: civ and team bonuses (BonusesSubsection), specific
## technologies, specific structures and heroes, each as formatEntry lines.
func gameplay_text(d: Dictionary) -> String:
	var t := ""
	var civ_ref: Dictionary = Common.text().get("civs", {}).get(d["code"], {})
	# Bonuses: auto-researched civ techs, then player auras, then {civ}.json CivBonuses.
	var lines := PackedStringArray()
	for b in civ_ref.get("bonuses", []):
		lines.append(entry(b["name"], b.get("info", ""), b.get("description", "")))
	for b in _civinfo.get(d["code"], {}).get("bonuses", []):
		lines.append(entry(b["name"], b.get("history", ""), b.get("description", "")))
	t += _heading("Civilization Bonus", "Civilization Bonuses", lines.size())
	if lines.is_empty():
		t += d["bonus_text"] + "\n"
	t += "".join(lines)
	lines = PackedStringArray()
	for b in civ_ref.get("team_bonuses", d["team_bonuses"]):
		lines.append(entry(b["name"], "", b.get("description", "")))
	t += "\n" + _heading("Team Bonus", "Team Bonuses", lines.size()) + "".join(lines)
	# Specific Technologies: requirements naming the civ (Subsection.getTechnologyCaptions).
	var specific: Array = civ_ref.get("specific_techs", [])
	lines = PackedStringArray()
	for tch in d["techs"]:
		var tmpl: String = tch.get("template", "")
		if not (tmpl in specific if civ_ref.has("specific_techs") else bool(tch.get("civ_specific", false))):
			continue
		var tx: Dictionary = Common.tech_text(tmpl)
		lines.append(entry(Common.link("tech", int(tch["id"]), tch["name"]), tx.get("description", tch["description"]),
			tx.get("tooltip", "")))
	t += "\n" + _heading("Specific Technology", "Specific Technologies", lines.size()) + "".join(lines)
	# Specific Structures: "CivSpecific Structure" classes.
	var own := [] # JSON numbers are floats; the structure types are ints
	for x in civ_ref.get("specific_structures", []):
		own.append(int(x))
	lines = PackedStringArray()
	for s in d["structures"]:
		if not (int(s["type"]) in own if civ_ref.has("specific_structures") else bool(s.get("unique", false))):
			continue
		var st: Dictionary = Common.structure_text(int(s["type"]), civ)
		var label := Common.entity_name(st.get("generic_name", s["name"]), st.get("specific_name", ""))
		lines.append(entry(Common.link("structure", int(s["type"]), label), "", st.get("tooltip", "")))
	t += "\n" + _heading("Specific Structure", "Specific Structures", lines.size()) + "".join(lines)
	lines = PackedStringArray()
	for h in d["heroes"]:
		var ut: Dictionary = Common.unit_text(h.get("id", ""))
		var label := Common.entity_name(ut.get("generic_name", h["name"]), ut.get("specific_name", h["specific_name"]))
		lines.append(entry(Common.link("unit", int(h["tmpl"]), label), "", ut.get("tooltip", "")))
	t += "\n" + _heading("Hero", "Heroes", lines.size()) + "".join(lines)
	return t
