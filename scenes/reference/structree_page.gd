extends VBoxContainer
## Structure Tree, port of 0 A.D. 0.28 gui/reference/structree (StructreePage.js,
## TreeSection, PhaseIdent, StructureBox, ProductionRowManager, TrainerSection,
## EntityBox, GPL-2.0-or-later): the civ's emblem, name and history; one band
## per phase with every structure the civ can build in it, each with a
## production row per phase from its own to the last (units, technologies and
## upgrades in the row of the phase that unlocks them); and beside the tree the
## Trainer Units that train, research or upgrade. Every icon has 0.28's tooltip
## (names, cost, tooltip, auras and stats) and opens the Template Viewer on a
## left or right click. Built from RtsoReference (the game's own tables).

const Common := preload("res://scenes/reference/reference_common.gd")
const Viewer := preload("res://scenes/reference/template_viewer.gd")
const PHASES := 3
const ROW_ICON := 30
## Wallsets (TemplateLister): their pieces are drawn on the wallset's box, not as
## boxes of their own; ProductionRowManager puts the tower in the first row.
const WALLSET_TOWER := {12: 22} # BLD_WALL_SEGMENT -> BLD_WALL_TOWER
const WALLSET_PIECES := [8, 22, 60, 64] # gates and the wall tower

signal civ_changed(civ: int)

var civ := 0
## Node the Template Viewer opens over (the current scene when unset).
var viewer_host: Node = null
## Tech ids already researched (in a game), drawn in green.
var researched := {}
## Hide the civ dropdown (in a game the tree shows the player's own civ).
var lock_civ := false

var _select
var _by_type := {} # the civ's structures by type, wallset pieces included
var _emblem: TextureRect
var _civ_name: Label
var _history: RichTextLabel
var _rows: VBoxContainer
var _trainers: VBoxContainer
var _trainer_section: VBoxContainer

## An icon whose tooltip is EntityBox.compileTooltip: names, cost, 0 A.D. tooltip,
## auras and the stat lines, built when first hovered.
class TipButton extends Button:
	var tip_kind := ""
	var tip_id := -1
	var tip_civ := 0
	var tip_extra := ""
	var tip_researcher := -1 # the structure type whose TechCostMultiplier prices a tech
	var tip_upgrade := {} # an Upgrade choice: its own cost, time and tooltip (getActualUpgradeData)

	func _make_custom_tooltip(_for_text: String) -> Object:
		var rt := RichTextLabel.new()
		rt.bbcode_enabled = true
		rt.fit_content = true
		rt.scroll_active = false
		rt.custom_minimum_size = Vector2(380, 0)
		rt.add_theme_font_size_override("normal_font_size", 13)
		rt.add_theme_font_size_override("bold_font_size", 13)
		rt.text = _tooltip_text()
		return rt

	func _tooltip_text() -> String:
		var ref = Common.ref()
		var d: Dictionary = {}
		var extra: Dictionary = {}
		match tip_kind:
			"unit":
				d = ref.get_unit(tip_id)
				extra = Common.unit_text(d.get("id", ""))
			"structure":
				d = ref.get_structure(tip_id, tip_civ)
				extra = Common.structure_text(tip_id, tip_civ)
			"tech":
				d = ref.get_tech(tip_id, tip_civ)
				extra = Common.tech_text(d.get("template", ""))
		if d.is_empty():
			return ""
		d = Common.civ_costed(d, tip_kind, tip_civ, tip_researcher)
		if not tip_upgrade.is_empty():
			d = d.duplicate()
			d["cost"] = tip_upgrade.get("upgrade_cost", {})
			d["time"] = tip_upgrade.get("upgrade_time", 0)
			d.erase("pop")
		var parts: PackedStringArray = []
		var generic: String = extra.get("generic_name", "")
		if generic == "":
			generic = d.get("name", "")
		var specific: String = extra.get("specific_name", d.get("specific_name", ""))
		var title := "[font_size=16][b]%s[/b][/font_size]" % (specific if specific != "" else generic)
		if specific != "" and specific != generic:
			title += " (%s)" % generic
		parts.append(title)
		var body := Viewer.stats_text(d, extra.get("viewer", {})) if tip_kind != "tech" else ""
		if tip_kind == "tech":
			var cost := Viewer.cost_line(d)
			if cost != "":
				body = "[b]Cost:[/b] " + cost
		var tooltip: String = extra.get("tooltip", "")
		if String(tip_upgrade.get("tooltip", "")) != "":
			tooltip = tip_upgrade["tooltip"]
		if tooltip == "" and tip_kind == "tech":
			tooltip = d.get("description", "")
		var auras := ""
		for a in d.get("auras", extra.get("auras", [])):
			auras += "\n" + Viewer.aura_line(a)
		if body.begins_with("[b]Cost:[/b]"):
			var nl := body.find("\n")
			parts.append(body if nl < 0 else body.substr(0, nl))
			body = "" if nl < 0 else body.substr(nl + 1)
		if tooltip != "":
			parts.append(tooltip)
		if auras != "":
			parts.append(auras.substr(1))
		if body != "":
			parts.append(body)
		if tip_extra != "":
			parts.append(tip_extra)
		parts.append("[color=#aaaaaa]Click for more information.[/color]")
		return "\n".join(parts)

func _init() -> void:
	name = "Structure Tree"

func _ready() -> void:
	set_meta("tooltip_style", "reference") # 0.28 referenceTooltip (tooltips.gd)
	add_theme_constant_override("separation", 6)
	var top := HBoxContainer.new()
	top.add_theme_constant_override("separation", 10)
	add_child(top)
	_emblem = TextureRect.new()
	_emblem.custom_minimum_size = Vector2(64, 64)
	_emblem.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_emblem.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	top.add_child(_emblem)
	var head := VBoxContainer.new()
	head.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	top.add_child(head)
	var name_row := HBoxContainer.new()
	head.add_child(name_row)
	_civ_name = Label.new()
	_civ_name.add_theme_font_size_override("font_size", 20)
	_civ_name.add_theme_color_override("font_color", Color(1, 0.83, 0.42))
	_civ_name.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	name_row.add_child(_civ_name)
	_select = Common.civ_select(_on_civ_selected)
	_select.visible = not lock_civ
	var heading := Common.civ_heading()
	heading.visible = _select.visible
	name_row.add_child(heading)
	name_row.add_child(_select)
	_history = RichTextLabel.new()
	_history.bbcode_enabled = true
	_history.custom_minimum_size = Vector2(0, 48)
	_history.scroll_active = true
	_history.add_theme_font_size_override("normal_font_size", 12)
	head.add_child(_history)
	var body := HBoxContainer.new()
	body.size_flags_vertical = Control.SIZE_EXPAND_FILL
	body.add_theme_constant_override("separation", 12) # StructreePage.SectionGap
	add_child(body)
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	body.add_child(scroll)
	_rows = VBoxContainer.new()
	_rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_rows.add_theme_constant_override("separation", 8)
	scroll.add_child(_rows)
	_trainer_section = VBoxContainer.new()
	_trainer_section.add_theme_constant_override("separation", 6)
	body.add_child(_trainer_section)
	var th := Label.new()
	th.text = "Trainer Units"
	th.add_theme_font_size_override("font_size", 16)
	th.add_theme_color_override("font_color", Color(1, 0.83, 0.42))
	_trainer_section.add_child(th)
	var tscroll := ScrollContainer.new()
	tscroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	tscroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	tscroll.custom_minimum_size = Vector2(170, 0)
	_trainer_section.add_child(tscroll)
	_trainers = VBoxContainer.new()
	_trainers.add_theme_constant_override("separation", 8)
	tscroll.add_child(_trainers)
	set_civ(civ)

func _on_civ_selected(index: int) -> void:
	var id: int = _select.get_item_id(index)
	set_civ(id)
	civ_changed.emit(id)

func set_civ(index: int) -> void:
	civ = index
	if _rows == null:
		return
	_select.select(_select.get_item_index(index))
	var info: Dictionary = Common.ref().get_civ_info(civ)
	_emblem.texture = Common.texture(info.get("emblem", ""))
	_civ_name.text = info.get("name", "")
	_history.text = Common.civ_history(civ)
	for c in _rows.get_children() + _trainers.get_children():
		c.get_parent().remove_child(c)
		c.queue_free()
	var tree: Array = []
	_by_type.clear()
	for b in Common.ref().get_structure_tree(civ):
		_by_type[int(b["type"])] = b
		if not int(b["type"]) in WALLSET_PIECES:
			tree.append(b)
	for phase in range(1, PHASES + 1):
		_rows.add_child(_phase_ident(phase))
		var flow := HFlowContainer.new()
		flow.add_theme_constant_override("h_separation", 8) # EntityBox.HMargin
		flow.add_theme_constant_override("v_separation", 12) # EntityBox.VMargin
		_rows.add_child(flow)
		for b in tree:
			if int(b["phase"]) == phase:
				flow.add_child(_structure_box(b))
	# TrainerSection: hidden when the civ has no trainer units.
	var trainers: Array = Common.ref().get_trainer_units(civ)
	for u in trainers:
		_trainers.add_child(_trainer_box(u))
	_trainer_section.visible = not trainers.is_empty()

## PhaseIdent: the phase's icon and name, then the icons of the later phases whose
## production rows the boxes below carry.
func _phase_ident(phase: int) -> Control:
	var head := HBoxContainer.new()
	head.add_theme_constant_override("separation", 6)
	# PhaseIdent.drawPhaseIcon: the civ's own phase tech (Kōmopolis for the Athenians).
	var phases: Array = Common.text().get("civs", {}).get(Common.civ_code(civ), {}).get("phases", [])
	var ph: Dictionary = phases[phase - 1] if phase - 1 < phases.size() else {}
	var title := Common.entity_name(ph.get("generic_name", "%s Phase" % Common.phase_name(phase)), ph.get("specific_name", ""))
	var icon := TextureRect.new()
	icon.texture = Common.texture(Common.ICON_ROOT + "technologies/" + ph["icon"]) if ph.get("icon", "") != "" else null
	if icon.texture == null:
		icon.texture = Common.texture(RtsoReference.phase_icon(phase))
	icon.custom_minimum_size = Vector2(28, 28)
	icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	icon.tooltip_text = title
	head.add_child(icon)
	var lbl := Label.new()
	lbl.text = title
	lbl.add_theme_font_size_override("font_size", 18)
	lbl.add_theme_color_override("font_color", Color(1, 0.83, 0.42))
	head.add_child(lbl)
	return head

func _caption(text: String) -> Label:
	var l := Label.new()
	l.text = text
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	l.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	l.custom_minimum_size = Vector2(96, 0) # EntityBox.MinWidth
	l.add_theme_font_size_override("font_size", 12)
	return l

## StructureBox: caption, icon, then ProductionRowManager's rows, one per phase from
## the structure's own (sortProductionsByPhase); units before technologies, then upgrades.
func _structure_box(b: Dictionary) -> Control:
	var panel := PanelContainer.new()
	var vb := VBoxContainer.new()
	vb.alignment = BoxContainer.ALIGNMENT_CENTER
	vb.add_theme_constant_override("separation", 3)
	panel.add_child(vb)
	vb.add_child(_caption(Common.caption_name(b["name"], Common.structure_text(int(b["type"]), civ))))
	var icon := _icon_button(Common.icon_for("structure", b, civ), 48, "structure", int(b["type"]))
	icon.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	vb.add_child(icon)
	var own := int(b["phase"])
	var rows := {}
	for p in range(own, PHASES + 1):
		var bar := PanelContainer.new() # the PhaseIdent's grey bar behind each row
		var bg := StyleBoxFlat.new()
		bg.bg_color = Color(1, 1, 1, 0.06)
		bar.add_theme_stylebox_override("panel", bg)
		bar.tooltip_text = "%s Phase" % Common.phase_name(p)
		vb.add_child(bar)
		var row := HBoxContainer.new()
		row.add_theme_constant_override("separation", 2)
		row.custom_minimum_size = Vector2(0, ROW_ICON)
		row.alignment = BoxContainer.ALIGNMENT_CENTER
		bar.add_child(row)
		rows[p] = row
	_fill_rows(rows, own, b["units"], b["techs"], b["upgrades"], "structure", int(b["type"]))
	var tower: Dictionary = _by_type.get(WALLSET_TOWER.get(int(b["type"]), -1), {})
	if not tower.is_empty():
		rows[own].add_child(_icon_button(Common.icon_for("structure", tower, civ), ROW_ICON, "structure", int(tower["type"])))
	return panel

func _row_for(rows: Dictionary, own: int, phase: int) -> Container:
	return rows[clampi(phase, own, PHASES)] if rows.has(clampi(phase, own, PHASES)) else rows.values()[0]

func _fill_rows(rows: Dictionary, own: int, units: Array, techs: Array, upgrades: Array, upgrade_kind: String, researcher := -1) -> void:
	for u in units:
		_row_for(rows, own, int(u["phase"])).add_child(
			_icon_button(Common.icon_for("unit", u, civ), ROW_ICON, "unit", int(u["tmpl"])))
	for t in techs:
		var done := researched.has(int(t["id"]))
		var tb := _icon_button(Common.icon_for("tech", t, civ), ROW_ICON, "tech", int(t["id"]),
			"[color=#80ff80]Researched[/color]" if done else "")
		tb.tip_researcher = researcher
		if done:
			tb.modulate = Color(0.55, 1.0, 0.55)
		_row_for(rows, own, int(t["phase"])).add_child(tb)
	for u in upgrades:
		var id := int(u["type"]) if upgrade_kind == "structure" else int(u["tmpl"])
		var ub := _icon_button(Common.icon_for(upgrade_kind, u, civ), ROW_ICON, upgrade_kind, id)
		ub.tip_upgrade = u
		_row_for(rows, own, int(u["phase"])).add_child(ub)

## TrainerBox: one production row, not sorted by phase.
func _trainer_box(u: Dictionary) -> Control:
	var panel := PanelContainer.new()
	var vb := VBoxContainer.new()
	vb.add_theme_constant_override("separation", 3)
	panel.add_child(vb)
	vb.add_child(_caption(Common.caption_name(u["name"], Common.unit_text(u["id"]))))
	var icon := _icon_button(Common.icon_for("unit", u, civ), 48, "unit", int(u["tmpl"]))
	icon.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	vb.add_child(icon)
	var row := HFlowContainer.new()
	row.custom_minimum_size = Vector2(150, ROW_ICON)
	row.add_theme_constant_override("h_separation", 2)
	vb.add_child(row)
	var rows := {1: row}
	_fill_rows(rows, 1, u["units"], u["techs"], [], "unit")
	for up in u["upgrades"]:
		var ub := _icon_button(Common.icon_for("unit", up, civ), ROW_ICON, "unit", int(up["tmpl"]))
		ub.tip_upgrade = up
		row.add_child(ub)
	return panel

func _icon_button(tex: Texture2D, px: int, kind: String, id: int, extra: String = "") -> TipButton:
	var b := TipButton.new()
	b.custom_minimum_size = Vector2(px, px)
	b.icon = tex
	b.expand_icon = true
	b.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER
	b.focus_mode = Control.FOCUS_NONE
	b.tip_kind = kind
	b.tip_id = id
	b.tip_civ = civ
	b.tip_extra = extra
	b.tooltip_text = " " # non-empty, so Godot asks _make_custom_tooltip
	# EntityBox.setViewerOnPress: onPress and onPressRight both open the viewer.
	b.button_mask = MOUSE_BUTTON_MASK_LEFT | MOUSE_BUTTON_MASK_RIGHT
	b.pressed.connect(func(): Viewer.open(viewer_host if viewer_host else get_tree().current_scene, kind, id, civ))
	return b
