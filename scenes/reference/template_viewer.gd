extends Control
## Template Viewer, port of 0 A.D. gui/reference/viewer (ViewerPage.js with the
## tooltips.js stat lines, GPL-2.0-or-later): the portrait, name, cost and
## every stat of one unit, structure or technology, then its 0 A.D. tooltip and
## history, auras, classes and what builds, trains or researches it. Opened by
## the structure tree, the civilization overview and the "i" button of the
## in-game selection details. Names in the lists link to their own page.
##
## open(parent, kind, id, civ): kind "unit" (roster template id), "structure"
## (building type, for civ) or "tech" (technology id, for civ).

const Common := preload("res://scenes/reference/reference_common.gd")

var kind := ""
var id := -1
var civ := 0
var _history: Array = []

var _icon: TextureRect
var _name: RichTextLabel
var _stats: RichTextLabel
var _info: RichTextLabel
var _back: Button

static func open(parent: Node, p_kind: String, p_id: int, p_civ: int) -> Control:
	var v = load("res://scenes/reference/template_viewer.gd").new()
	v.name = "TemplateViewer"
	parent.add_child(v)
	v.show_template(p_kind, p_id, p_civ)
	return v

func _init() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	if ResourceLoader.exists("res://assets/themes/zeroad_theme.tres"):
		theme = load("res://assets/themes/zeroad_theme.tres")
	var dim := ColorRect.new()
	dim.color = Color(0, 0, 0, 0.55)
	dim.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(dim)
	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(center)
	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(760, 560)
	center.add_child(panel)
	var vb := VBoxContainer.new()
	vb.add_theme_constant_override("separation", 8)
	panel.add_child(vb)
	var top := HBoxContainer.new()
	top.add_theme_constant_override("separation", 12)
	vb.add_child(top)
	_icon = TextureRect.new()
	_icon.custom_minimum_size = Vector2(96, 96)
	_icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	top.add_child(_icon)
	var right := VBoxContainer.new()
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	top.add_child(right)
	_name = RichTextLabel.new()
	_name.bbcode_enabled = true
	_name.fit_content = true
	_name.scroll_active = false
	right.add_child(_name)
	_stats = RichTextLabel.new()
	_stats.bbcode_enabled = true
	_stats.fit_content = true
	_stats.scroll_active = false
	_stats.add_theme_font_size_override("normal_font_size", 13)
	_stats.add_theme_font_size_override("bold_font_size", 13)
	right.add_child(_stats)
	_info = Common.rich_text()
	_info.custom_minimum_size = Vector2(0, 260)
	_info.add_theme_font_size_override("normal_font_size", 13)
	_info.add_theme_font_size_override("bold_font_size", 13)
	_info.add_theme_font_size_override("italics_font_size", 13)
	_info.meta_clicked.connect(_on_meta)
	_stats.meta_clicked.connect(_on_meta)
	vb.add_child(_info)
	var buttons := HBoxContainer.new()
	buttons.alignment = BoxContainer.ALIGNMENT_END
	vb.add_child(buttons)
	_back = Button.new()
	_back.text = "Back"
	_back.custom_minimum_size = Vector2(100, 32)
	_back.pressed.connect(_go_back)
	buttons.add_child(_back)
	var close := Button.new()
	close.name = "Close"
	close.text = "Close"
	close.custom_minimum_size = Vector2(100, 32)
	close.pressed.connect(close_viewer)
	buttons.add_child(close)

func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_ESCAPE:
		close_viewer()
		get_viewport().set_input_as_handled()

func close_viewer() -> void:
	queue_free()

func _on_meta(meta) -> void:
	var parts := str(meta).split(":")
	if parts.size() == 2:
		_history.append([kind, id, civ])
		show_template(parts[0], int(parts[1]), civ, false)

func _go_back() -> void:
	if _history.is_empty():
		return
	var h: Array = _history.pop_back()
	show_template(h[0], h[1], h[2], false)

func show_template(p_kind: String, p_id: int, p_civ: int, reset_history: bool = true) -> void:
	if reset_history:
		_history.clear()
	kind = p_kind
	id = p_id
	civ = p_civ
	var d: Dictionary = {}
	match kind:
		"unit": d = Common.ref().get_unit(id)
		"structure": d = Common.ref().get_structure(id, civ)
		"tech": d = Common.ref().get_tech(id, civ)
	_back.visible = not _history.is_empty()
	if d.is_empty():
		_name.text = "[b]Unknown template[/b]"
		_stats.text = ""
		_info.text = ""
		return
	if kind == "unit":
		civ = int(d["civ"])
	var extra := _text_for(d)
	_icon.texture = Common.texture(d.get("portrait", ""))
	var specific: String = extra.get("specific_name", d.get("specific_name", ""))
	var generic: String = d["name"]
	var title := "[font_size=22][b]%s[/b][/font_size]" % (specific if specific != "" and specific != generic else generic)
	if specific != "" and specific != generic:
		title += "\n[color=%s]%s[/color]" % [Common.DIM, generic]
	_name.text = title
	_stats.text = stats_text(d)
	_info.text = info_text(d, extra)

func _text_for(d: Dictionary) -> Dictionary:
	match kind:
		"unit": return Common.unit_text(d.get("id", ""))
		"structure": return Common.structure_text(id, civ)
		"tech": return Common.tech_text(d.get("template", ""))
	return {}

static func _num(v) -> String:
	var f := float(v)
	return str(int(round(f))) if absf(f - round(f)) < 0.05 else "%.1f" % f

static func cost_line(d: Dictionary) -> String:
	var parts: PackedStringArray = []
	var cost: Dictionary = d.get("cost", {})
	for r in ["food", "wood", "stone", "metal"]:
		if int(cost.get(r, 0)) > 0:
			parts.append("%s %d" % [r.capitalize(), int(cost[r])])
	if float(d.get("time", 0)) > 0:
		parts.append("Time %s s" % _num(d["time"]))
	if int(d.get("pop", 0)) > 0:
		parts.append("Population %d" % int(d["pop"]))
	return "  ".join(parts)

## gui/common/tooltips.js stat lines, in ReferencePage.StatsFunctions order.
static func stats_text(d: Dictionary) -> String:
	var lines: PackedStringArray = []
	var cost := cost_line(d)
	if cost != "":
		lines.append("[b]Cost:[/b] " + cost)
	lines.append("[b]Phase:[/b] " + Common.phase_name(int(d.get("phase", 1))))
	if d.has("hp"):
		lines.append("[b]Health:[/b] " + _num(d["hp"]))
	if d.has("attack"):
		var a: Dictionary = d["attack"]
		var dmg: PackedStringArray = []
		for t in ["hack", "pierce", "crush"]:
			if float(a[t]) > 0:
				dmg.append("%s %s" % [_num(a[t]), t.capitalize()])
		var line := "[b]%s Attack:[/b] %s, interval %s s, range %s m" % [a["type"], ", ".join(dmg), _num(a["interval"]), _num(a["range"])]
		if float(a.get("spread", 0)) > 0:
			line += ", spread %s" % _num(a["spread"])
		lines.append(line)
		for b in a.get("bonuses", []):
			lines.append("    Bonus: %s× vs %s" % [_num(b["multiplier"]), b["classes"]])
		if String(a.get("restricted", "")) != "":
			lines.append("    Cannot attack: " + a["restricted"])
	if d.has("capture"):
		lines.append("[b]Capture Attack:[/b] %s points per second" % _num(d["capture"]))
	if d.has("heal"):
		var h: Dictionary = d["heal"]
		lines.append("[b]Heal:[/b] %s HP every %s s, range %s m" % [_num(h["hp"]), _num(h["interval"]), _num(h["range"])])
	if d.has("armour"):
		var r: Dictionary = d["armour"]
		lines.append("[b]Resistance:[/b] Hack %s (%d%%), Pierce %s (%d%%), Crush %s (%d%%)" % [
			_num(r["hack"]), int(round(r["hack_pct"])), _num(r["pierce"]), int(round(r["pierce_pct"])),
			_num(r["crush"]), int(round(r["crush_pct"]))])
	if int(d.get("garrison", 0)) > 0:
		var g := "[b]Garrison Limit:[/b] %d" % int(d["garrison"])
		if float(d.get("garrison_heal", 0)) > 0:
			g += ", heals %s HP/s" % _num(d["garrison_heal"])
		lines.append(g)
	if d.has("walk_speed"):
		lines.append("[b]Walk Speed:[/b] %s m/s" % _num(d["walk_speed"]))
	if d.has("vision"):
		lines.append("[b]Vision:[/b] %s m" % _num(d["vision"]))
	if int(d.get("carry", 0)) > 0:
		lines.append("[b]Carry Capacity:[/b] %d" % int(d["carry"]))
	if int(d.get("pop_bonus", 0)) > 0:
		lines.append("[b]Population Bonus:[/b] %d" % int(d["pop_bonus"]))
	if float(d.get("capture_points", 0)) > 0:
		lines.append("[b]Capture Points:[/b] %s, regenerates %s per second" % [_num(d["capture_points"]), _num(d["capture_regen"])])
	if d.has("loot"):
		var lp: PackedStringArray = []
		if int(d.get("loot_xp", 0)) > 0:
			lp.append("%d XP" % int(d["loot_xp"]))
		for r2 in ["food", "wood", "stone", "metal"]:
			if int(d["loot"].get(r2, 0)) > 0:
				lp.append("%s %d" % [r2.capitalize(), int(d["loot"][r2])])
		if not lp.is_empty():
			lines.append("[b]Loot:[/b] " + ", ".join(lp))
	return "\n".join(lines)

static func _links(items: Array, link_kind: String, key: String) -> String:
	var parts: PackedStringArray = []
	for it in items:
		parts.append(Common.link(link_kind, int(it[key]), Common.entity_name(it["name"], it.get("specific_name", ""))))
	return ", ".join(parts)

## ViewerPage.InfoFunctions: tooltip, history, description, auras, classes,
## built/trained/researched by, then what it builds, trains and researches.
func info_text(d: Dictionary, extra: Dictionary) -> String:
	var blocks: PackedStringArray = []
	var tooltip: String = extra.get("tooltip", d.get("description", "") if kind == "tech" else "")
	if kind == "tech" and tooltip == "":
		tooltip = d.get("description", "")
	if tooltip != "":
		blocks.append(tooltip)
	if extra.get("history", "") != "":
		blocks.append("[i]%s[/i]" % extra["history"])
	if kind == "tech" and extra.get("description", "") != "":
		blocks.append("[i]%s[/i]" % extra["description"])
	if kind == "tech" and extra.get("requirements_tooltip", "") != "":
		blocks.append("[color=%s]%s[/color]" % [Common.DIM, extra["requirements_tooltip"]])
	var auras: Array = d.get("auras", [])
	if not auras.is_empty():
		var t := "[b]Auras:[/b]"
		for a in auras:
			t += "\n[color=#c9a0ff]%s[/color]: %s" % [a["name"], a["description"]]
		blocks.append(t)
	var classes: Array = extra.get("classes", [])
	if not classes.is_empty():
		blocks.append("[b]Classes:[/b] " + ", ".join(classes))
	if d.has("roles") and not d["roles"].is_empty():
		blocks.append("[b]Rank:[/b] " + ", ".join(d["roles"]))
	match kind:
		"unit":
			if not d["trained_at"].is_empty():
				blocks.append("[b]Trained at:[/b] " + _links(d["trained_at"], "structure", "type"))
			if d.has("requires"):
				blocks.append("[b]Requires:[/b] " + Common.link("tech", int(d["requires"]["id"]), d["requires"]["name"]))
			var builds := _buildable(extra.get("builds", []))
			if not builds.is_empty():
				blocks.append("[b]Builds:[/b] " + _links(builds, "structure", "type"))
		"structure":
			var by := _built_by()
			if not by.is_empty():
				blocks.append("[b]Built by:[/b] " + _links(by, "unit", "tmpl"))
			if not d["trains"].is_empty():
				blocks.append("[b]Trains:[/b] " + _links(d["trains"], "unit", "tmpl"))
			if not d["researches"].is_empty():
				blocks.append("[b]Researches:[/b] " + _links(d["researches"], "tech", "id"))
			for u in d["upgrades"]:
				blocks.append("[b]Upgrades to:[/b] %s (%s, %s s) %s" % [
					Common.link("structure", int(u["type"]), u["name"]), u["cost_text"], _num(u["time"]), u["tooltip"]])
			if not bool(d.get("available", true)):
				blocks.append("[color=#ff8080]The %s cannot build this structure.[/color]" % d["civ_name"])
		"tech":
			blocks.append("[b]Researched at:[/b] " + Common.link("structure", int(d["researched_at"]["type"]), d["researched_at"]["name"]))
			if d.has("requires"):
				blocks.append("[b]Requires:[/b] " + Common.link("tech", int(d["requires"]["id"]), d["requires"]["name"]))
			if not d["unlocks"].is_empty():
				blocks.append("[b]Unlocks:[/b] " + _links(d["unlocks"], "tech", "id"))
			if not d["unlocks_units"].is_empty():
				blocks.append("[b]Unlocks training:[/b] " + _links(d["unlocks_units"], "unit", "tmpl"))
			blocks.append("[b]Civilizations:[/b] " + ", ".join(d["civs"]))
	return "\n\n".join(blocks)

## Structure types (from the 0 A.D. Builder list) this civ can build, as link items.
func _buildable(types: Array) -> Array:
	var out: Array = []
	var want := {}
	for t in types: # JSON numbers are floats
		want[int(t)] = true
	for b in Common.ref().get_structure_tree(civ):
		if want.has(int(b["type"])):
			out.append(b)
	return out

## The civ's roster units whose 0 A.D. Builder list has this structure.
func _built_by() -> Array:
	var out: Array = []
	var seen := {}
	for b in Common.ref().get_structure_tree(civ):
		for u in b["units"]:
			if seen.has(u["tmpl"]):
				continue
			seen[u["tmpl"]] = true
			if Common.unit_text(u["id"]).get("builds", []).has(float(id)):
				out.append(u)
	return out
