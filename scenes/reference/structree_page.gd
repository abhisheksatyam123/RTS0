extends VBoxContainer
## Structure Tree, port of 0 A.D. gui/reference/structree (StructreePage.js,
## TreeSection, StructureBox, ProductionRow, GPL-2.0-or-later): one row per
## phase with every structure the civilization can build in it, and under each
## structure the units it trains and the technologies it researches. Clicking
## any icon opens the Template Viewer; hovering shows its name and cost below.
## Built from RtsoReference (the game's own roster, building and tech tables).

const Common := preload("res://scenes/reference/reference_common.gd")
const Viewer := preload("res://scenes/reference/template_viewer.gd")

signal civ_changed(civ: int)

var civ := 0
## Node the Template Viewer opens over (the current scene when unset).
var viewer_host: Node = null
## Tech ids already researched (in a game), drawn in green.
var researched := {}
## Hide the civ dropdown (in a game the tree shows the player's own civ).
var lock_civ := false

var _select
var _rows: VBoxContainer
var _hover: Label

func _init() -> void:
	name = "Structure Tree"

func _ready() -> void:
	add_theme_constant_override("separation", 6)
	var top := HBoxContainer.new()
	add_child(top)
	_select = Common.civ_select(_on_civ_selected)
	_select.visible = not lock_civ
	top.add_child(_select)
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	add_child(scroll)
	_rows = VBoxContainer.new()
	_rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_rows.add_theme_constant_override("separation", 10)
	scroll.add_child(_rows)
	_hover = Label.new()
	_hover.custom_minimum_size = Vector2(0, 22)
	_hover.add_theme_color_override("font_color", Color(1, 0.9, 0.65))
	_hover.text = "Click a structure, unit or technology for its details."
	add_child(_hover)
	set_civ(civ)

func _on_civ_selected(index: int) -> void:
	set_civ(index)
	civ_changed.emit(index)

func set_civ(index: int) -> void:
	civ = index
	if _rows == null:
		return
	_select.select(index)
	for c in _rows.get_children():
		_rows.remove_child(c)
		c.queue_free()
	var tree: Array = Common.ref().get_structure_tree(civ)
	for phase in range(1, 4):
		var head := HBoxContainer.new()
		var icon := TextureRect.new()
		icon.texture = Common.texture(RtsoReference.phase_icon(phase))
		icon.custom_minimum_size = Vector2(28, 28)
		icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		head.add_child(icon)
		var lbl := Label.new()
		lbl.text = "%s Phase" % Common.phase_name(phase)
		lbl.add_theme_font_size_override("font_size", 18)
		lbl.add_theme_color_override("font_color", Color(1, 0.83, 0.42))
		head.add_child(lbl)
		_rows.add_child(head)
		var flow := HFlowContainer.new()
		flow.add_theme_constant_override("h_separation", 8)
		flow.add_theme_constant_override("v_separation", 8)
		_rows.add_child(flow)
		for b in tree:
			if int(b["phase"]) == phase:
				flow.add_child(_structure_box(b))

func _structure_box(b: Dictionary) -> Control:
	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(150, 0)
	var vb := VBoxContainer.new()
	vb.add_theme_constant_override("separation", 3)
	panel.add_child(vb)
	var top := HBoxContainer.new()
	vb.add_child(top)
	var btn := _icon_button(b["portrait"], 48, "structure", int(b["type"]),
		"%s  (%s)" % [b["name"], b["cost"]])
	top.add_child(btn)
	var name_lbl := Label.new()
	name_lbl.text = b["name"]
	name_lbl.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	name_lbl.custom_minimum_size = Vector2(90, 0)
	name_lbl.add_theme_font_size_override("font_size", 12)
	top.add_child(name_lbl)
	if not b["units"].is_empty() or not b["techs"].is_empty():
		var prod := HFlowContainer.new()
		prod.custom_minimum_size = Vector2(140, 0)
		prod.add_theme_constant_override("h_separation", 2)
		prod.add_theme_constant_override("v_separation", 2)
		vb.add_child(prod)
		for u in b["units"]:
			var n := Common.entity_name(u["name"], u["specific_name"])
			prod.add_child(_icon_button(u["portrait"], 30, "unit", int(u["tmpl"]),
				"Trains %s  (%s, %s Phase)" % [n, u["cost"], Common.phase_name(int(u["phase"]))]))
		for t in b["techs"]:
			var tb := _icon_button(t["portrait"], 30, "tech", int(t["id"]),
				"Researches %s  (%s, %s Phase)%s" % [t["name"], t["cost"], Common.phase_name(int(t["phase"])),
					"  researched" if researched.has(int(t["id"])) else ""])
			tb.modulate = Color(0.55, 1.0, 0.55) if researched.has(int(t["id"])) else Color(0.8, 0.7, 1.0)
			prod.add_child(tb)
	return panel

func _icon_button(path: String, px: int, kind: String, id: int, hover: String) -> Button:
	var b := Button.new()
	b.custom_minimum_size = Vector2(px, px)
	b.icon = Common.texture(path)
	b.expand_icon = true
	b.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER
	b.focus_mode = Control.FOCUS_NONE
	b.mouse_entered.connect(func(): _hover.text = hover)
	b.pressed.connect(func(): Viewer.open(viewer_host if viewer_host else get_tree().current_scene, kind, id, civ))
	return b
