extends Control
## Civilization info and structure tree, ports of 0 A.D. gui/reference/civinfo
## and gui/reference/structree (GPL-2.0-or-later). The text comes from the
## game's own tables (RtsoReference: rosters, buildings, techs, team bonuses,
## hero auras) and data/civs/civinfo.json (0 A.D. civ bonuses with their
## history, CC BY-SA 3.0), so it always matches what the game plays.

const CIVINFO_PATH := "res://data/civs/civinfo.json"
const PHASES := ["", "Village", "Town", "City"]

var ref := RtsoReference.new()
var civs: Array = []
var civinfo := {} # code -> 0 A.D. civ JSON extract
var civ := 0

var info_select
var tree_select
var info_text: RichTextLabel
var structree: Tree
var emblem: TextureRect

func _ready() -> void:
	civs = ref.get_civs()
	var data = JSON.parse_string(FileAccess.get_file_as_string(CIVINFO_PATH)) if FileAccess.file_exists(CIVINFO_PATH) else null
	if typeof(data) == TYPE_ARRAY:
		for c in data:
			civinfo[c["code"]] = c
	var tabs: TabContainer = $Center/Panel/VBox/TabContainer
	tabs.add_child(_build_civinfo_tab())
	tabs.add_child(_build_structree_tab())
	_show_civ(0)

func _civ_select() -> Control:
	var sel = RtsoSafeSelect.new()
	sel.custom_minimum_size = Vector2(240, 30)
	for c in civs:
		sel.add_item(c["name"])
	sel.item_selected.connect(_show_civ)
	return sel

func _build_civinfo_tab() -> Control:
	var vb := VBoxContainer.new()
	vb.name = "Civilization"
	var top := HBoxContainer.new()
	vb.add_child(top)
	info_select = _civ_select()
	top.add_child(info_select)
	emblem = TextureRect.new()
	emblem.custom_minimum_size = Vector2(56, 56)
	emblem.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	emblem.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	top.add_child(emblem)
	info_text = RichTextLabel.new()
	info_text.bbcode_enabled = true
	info_text.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vb.add_child(info_text)
	return vb

func _build_structree_tab() -> Control:
	var vb := VBoxContainer.new()
	vb.name = "Structure Tree"
	tree_select = _civ_select()
	vb.add_child(tree_select)
	structree = Tree.new()
	structree.columns = 3
	structree.hide_root = true
	structree.column_titles_visible = true
	structree.set_column_title(0, "Structure / unit / technology")
	structree.set_column_title(1, "Phase")
	structree.set_column_title(2, "Cost")
	structree.set_column_expand_ratio(0, 3)
	structree.set_column_expand_ratio(1, 1)
	structree.set_column_expand_ratio(2, 2)
	structree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vb.add_child(structree)
	return vb

func _show_civ(index: int) -> void:
	civ = index
	info_select.select(index)
	tree_select.select(index)
	_render_civinfo()
	_render_structree()

func _texture(path: String) -> Texture2D:
	if ResourceLoader.exists(path):
		return load(path)
	if FileAccess.file_exists(path):
		var img := Image.load_from_file(path)
		if img:
			return ImageTexture.create_from_image(img)
	return null

# gui/reference/civinfo: civ bonuses (with history), team bonuses, heroes.
func _render_civinfo() -> void:
	var d: Dictionary = ref.get_civ_info(civ)
	emblem.texture = _texture(d.get("emblem", ""))
	var t := "[font_size=22][b]%s[/b][/font_size]\n\n" % d["name"]
	t += "[color=#ffd36b][b]Civilization Bonuses[/b][/color]\n"
	var extract: Dictionary = civinfo.get(d["code"], {})
	if extract.get("bonuses", []).size() > 0:
		for b in extract["bonuses"]:
			t += "[b]%s[/b]: %s\n" % [b["name"], b["description"]]
			if b.get("history", "") != "":
				t += "[i][color=#bbbbaa]%s[/color][/i]\n" % b["history"]
	else:
		t += d["bonus_text"] + "\n"
	t += "\n[color=#ffd36b][b]Team Bonuses[/b][/color]\n"
	for b in d["team_bonuses"]:
		t += "[b]%s[/b]: %s\n" % [b["name"], b["description"]]
	t += "\n[color=#ffd36b][b]Heroes[/b][/color]\n"
	for h in d["heroes"]:
		var title: String = h["name"]
		if h["specific_name"] != "" and h["specific_name"] != title:
			title += " (%s)" % h["specific_name"]
		t += "[b]%s[/b]  [color=#aaaaaa]%s phase, %s[/color]\n" % [title, PHASES[clampi(int(h["phase"]), 0, 3)], h["cost"]]
		for a in h["auras"]:
			t += "    [color=#c9a0ff]%s[/color]: %s\n" % [a["name"], a["description"]]
	info_text.text = t

# gui/reference/structree: each structure with what it trains and researches.
func _render_structree() -> void:
	structree.clear()
	var root := structree.create_item()
	for b in ref.get_structure_tree(civ):
		if b["units"].is_empty() and b["techs"].is_empty():
			continue
		var it := structree.create_item(root)
		it.set_text(0, b["name"])
		it.set_text(1, PHASES[clampi(int(b["phase"]), 0, 3)])
		it.set_text(2, b["cost"])
		it.set_icon(0, _texture(b["portrait"]))
		it.set_icon_max_width(0, 28)
		for u in b["units"]:
			var ui := structree.create_item(it)
			var name: String = u["name"]
			if u["specific_name"] != "" and u["specific_name"] != name:
				name += " (%s)" % u["specific_name"]
			ui.set_text(0, "Trains " + name)
			ui.set_text(1, PHASES[clampi(int(u["phase"]), 0, 3)])
			ui.set_text(2, u["cost"])
		for tch in b["techs"]:
			var ti := structree.create_item(it)
			ti.set_text(0, "Researches " + tch["name"])
			ti.set_tooltip_text(0, tch["description"])
			ti.set_text(1, PHASES[clampi(int(tch["phase"]), 0, 3)])
			ti.set_text(2, tch["cost"])
		it.collapsed = false

func _on_back_pressed() -> void:
	MatchSettings.go("res://scenes/menu/main_menu.tscn")
