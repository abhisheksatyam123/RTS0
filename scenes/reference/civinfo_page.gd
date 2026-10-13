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
	add_theme_constant_override("separation", 8)
	var data = JSON.parse_string(FileAccess.get_file_as_string(CIVINFO_PATH)) if FileAccess.file_exists(CIVINFO_PATH) else null
	if typeof(data) == TYPE_ARRAY:
		for c in data:
			_civinfo[c["code"]] = c
	var top := HBoxContainer.new()
	add_child(top)
	_select = Common.civ_select(_on_civ_selected)
	_select.visible = not lock_civ
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
	_history.text = (Common.heading("History of the %s" % d["name"], 18) + "\n" + hist) if hist != "" else ""

func gameplay_text(d: Dictionary) -> String:
	var t := Common.heading("%s Gameplay" % d["name"], 20) + "\n"
	var extract: Dictionary = _civinfo.get(d["code"], {})
	var bonuses: Array = extract.get("bonuses", [])
	t += Common.heading("Civilization Bonus" if bonuses.size() == 1 else "Civilization Bonuses")
	if bonuses.is_empty():
		t += d["bonus_text"] + "\n"
	for b in bonuses:
		t += "[b]%s[/b]: %s\n" % [b["name"], b["description"]]
		if b.get("history", "") != "":
			t += "[i][color=#bbbbaa]%s[/color][/i]\n" % b["history"]
	t += "\n" + Common.heading("Team Bonus" if d["team_bonuses"].size() == 1 else "Team Bonuses")
	for b in d["team_bonuses"]:
		t += "[b]%s[/b]: %s\n" % [b["name"], b["description"]]
	t += "\n" + Common.heading("Specific Technology" if d["techs"].size() == 1 else "Specific Technologies")
	for tch in d["techs"]:
		t += "%s  [color=%s]%s Phase[/color]\n    %s\n" % [Common.link("tech", int(tch["id"]), tch["name"]),
			Common.DIM, Common.phase_name(int(tch["phase"])), tch["description"]]
	t += "\n" + Common.heading("Specific Structure" if d["structures"].size() == 1 else "Specific Structures")
	for s in d["structures"]:
		var st: Dictionary = Common.structure_text(int(s["type"]), civ)
		t += "%s  [color=%s]%s Phase[/color]\n" % [Common.link("structure", int(s["type"]), s["name"]),
			Common.DIM, Common.phase_name(int(s["phase"]))]
		if st.get("tooltip", "") != "":
			t += "    %s\n" % st["tooltip"]
	t += "\n" + Common.heading("Hero" if d["heroes"].size() == 1 else "Heroes")
	for h in d["heroes"]:
		t += "%s  [color=%s]%s Phase, %s[/color]\n" % [
			Common.link("unit", int(h["tmpl"]), Common.entity_name(h["name"], h["specific_name"])),
			Common.DIM, Common.phase_name(int(h["phase"])), h["cost"]]
		for a in h["auras"]:
			t += "    [color=#c9a0ff]%s[/color]: %s\n" % [a["name"], a["description"]]
	return t
