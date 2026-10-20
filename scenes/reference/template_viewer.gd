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
var _rank: TextureRect
const RANK_ICONS := "res://assets/textures/ui/archive/icons/ranks/%s.png"
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
	process_mode = Node.PROCESS_MODE_ALWAYS # it can pause the game it opens over
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
	_rank = TextureRect.new()
	_rank.custom_minimum_size = Vector2(24, 24)
	_rank.size = Vector2(24, 24)
	_rank.position = Vector2(72, 0)
	_rank.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_rank.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	_rank.visible = false
	_icon.add_child(_rank)
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
	if event is InputEventKey and event.pressed and not event.echo and "cancel" in RtsoHotkeyConfig.new().fired(event):
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
	if kind != "tech":
		d = Common.civ_costed(d, kind, civ)
	else:
		# ViewerPage.js: the cost times its first researcher's TechCostMultiplier.
		var rs: Array = d.get("researchers", [])
		var first: Dictionary = rs[0] if not rs.is_empty() else d.get("researched_at", {})
		d = Common.civ_costed(d, kind, civ, int(first.get("type", -1)) if first.get("kind", "structure") == "structure" else -1)
	_back.visible = not _history.is_empty()
	if d.is_empty():
		_name.text = "[b]Unknown template[/b]"
		_stats.text = ""
		_info.text = ""
		return
	if kind == "unit":
		civ = int(d["civ"])
	var extra := _text_for(d)
	_icon.texture = Common.icon_for(kind, d, civ)
	var specific: String = extra.get("specific_name", d.get("specific_name", ""))
	var generic: String = d["name"]
	var title := "[font_size=22][b]%s[/b][/font_size]" % (specific if specific != "" and specific != generic else generic)
	if specific != "" and specific != generic:
		title += "\n[color=%s]%s[/color]" % [Common.DIM, generic]
	_name.text = title
	var vw: Dictionary = extra.get("viewer", {})
	_stats.text = stats_text(d, vw)
	# ViewerPage entityRankGlyph: session/icons/ranks/<Identity/Rank>.png on the portrait.
	_rank.texture = Common.texture(RANK_ICONS % vw["rank"]) if vw.has("rank") else null
	_rank.visible = _rank.texture != null
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

const RES := ["food", "wood", "stone", "metal"]
const DAMAGE_ORDER := ["Hack", "Pierce", "Crush", "Poison", "Fire"]
const INDENT := "    "

static func _h(t: String) -> String:
	return "[b]%s[/b]" % t

## resourceIcon(): the resource's icon, then the subtype's name for "food.fruit".
static func _res_icon(code: String) -> String:
	var parts := code.replace("_", ".").split(".")
	var t := "[img=16x16]res://assets/textures/ui/icons/%s.png[/img]" % parts[0] if parts[0] in RES else parts[0].capitalize()
	if parts.size() > 1:
		t += " " + parts[1].capitalize()
	return t

## getSecondsString: "1 second", "1.5 seconds".
static func _seconds(v) -> String:
	var f := float(v)
	return "%s %s" % [_trim(f), "second" if is_equal_approx(f, 1.0) else "seconds"]

static func _trim(f: float) -> String:
	return str(int(f)) if is_equal_approx(f, round(f)) else str(snappedf(f, 0.01))

static func _damage(dmg: Dictionary) -> String:
	var keys: Array = dmg.keys()
	keys.sort_custom(func(x, y): return DAMAGE_ORDER.find(x) < DAMAGE_ORDER.find(y))
	var out: PackedStringArray = []
	for k in keys:
		if float(dmg[k]) != 0.0:
			out.append("%.1f %s" % [float(dmg[k]), k])
	return ", ".join(out)

## attackEffectsDetails: capture, damage, then the status effects it gives.
static func _effects(a: Dictionary) -> String:
	var out: PackedStringArray = []
	if a.has("capture"):
		out.append("%.1f Capture" % float(a["capture"]))
	if a.has("damage"):
		var dm := _damage(a["damage"])
		if dm != "":
			out.append(dm)
	if a.has("status") and not a["status"].is_empty():
		var names: PackedStringArray = []
		for code in a["status"]:
			names.append(a["status"][code]["name"])
		out.append("gives " + ", ".join(names))
	return ", ".join(out)

## rangeDetails, with the parabolic range from Origin/Y as "(+N)".
static func _range(a: Dictionary) -> String:
	var mx := float(a.get("max_range", 0))
	if mx <= 0.0:
		return ""
	var mn := int(round(float(a.get("min_range", 0))))
	var mxr := int(round(mx))
	var y := float(a.get("y_origin", 0))
	var rel := int(round(sqrt(mx * (2.0 * y + mx)) - mx)) if y > 0.0 else 0
	var t := "%s %s" % [_h("Range:"), ("%d to %d" % [mn, mxr]) if mn else str(mxr)]
	if rel:
		t += " (%s%d)" % ["+" if rel > 0 else "", rel]
	return t + (" meters" if mn or rel or mxr != 1 else " meter")

## attackRateDetails: "Interval: 1.5 seconds", "Rate: 3 arrows / 2 seconds" or
## "Garrison to fire arrows" for a structure with no default arrows.
static func _rate(interval: float, arrows) -> String:
	if interval <= 0.0:
		return ""
	if arrows != null and int(arrows) == 0:
		return "Garrison to fire arrows"
	if arrows != null and int(arrows) > 1:
		return "%s %d %s / %s" % [_h("Rate:"), int(arrows), "arrows", _seconds(interval)]
	return "%s %s" % [_h("Interval:"), _seconds(interval)]

## The template's attacks, with the numbers RTS0 plays (civ bonuses included, as the
## 0.28 reference applies the civ's modifiers) laid over the template's.
static func _attacks(d: Dictionary, vw: Dictionary) -> Array:
	var list: Array = []
	for a in vw.get("attacks", []):
		list.append(a.duplicate(true))
	var sim: Dictionary = d.get("attack", {})
	if not sim.is_empty():
		var hit := false
		for a in list:
			if a["type"] == sim["type"]:
				hit = true
				var dmg: Dictionary = a.get("damage", {})
				dmg["Hack"] = sim["hack"]
				dmg["Pierce"] = sim["pierce"]
				dmg["Crush"] = sim["crush"]
				a["damage"] = dmg
				a["repeat_time"] = sim["interval"]
				a["max_range"] = sim["range"]
		if not hit:
			list.append({"type": sim["type"], "name": sim["type"], "max_range": sim["range"], "repeat_time": sim["interval"],
				"damage": {"Hack": sim["hack"], "Pierce": sim["pierce"], "Crush": sim["crush"]}})
	if d.has("capture"):
		for a in list:
			if a["type"] == "Capture":
				a["capture"] = d["capture"]
	return list

## getAttackTooltip.
static func attack_text(d: Dictionary, vw: Dictionary) -> String:
	var rows: PackedStringArray = []
	var sim: Dictionary = d.get("attack", {})
	for a in _attacks(d, vw):
		var arrows = null
		if d.get("kind", "") == "structure" and sim.has("arrows"):
			arrows = int(sim["arrows"])
		var parts: PackedStringArray = []
		for p in [_effects(a), _range(a), _rate(float(a.get("repeat_time", 0)), arrows)]:
			if p != "":
				parts.append(p)
		var line := "%s: %s" % [_h(a["name"]), ", ".join(parts)]
		for code in a.get("status", {}):
			var st: Dictionary = a["status"][code]
			var sp: PackedStringArray = []
			if st.has("damage"):
				sp.append(_damage(st["damage"]))
			if st.has("interval"):
				sp.append("%s %s" % [_h("Interval:"), _seconds(st["interval"])])
			if st.has("duration"):
				sp.append("%s: %s" % [_h("Duration"), _seconds(st["duration"])])
			if st.get("tooltip", "") != "":
				sp.append(st["tooltip"])
			var stack: String = {"Extend": " (extends)", "Replace": " (replaces)", "Stack": " (stacks)"}.get(st.get("stackability", ""), "")
			line += "\n%s%s%s: %s%s" % [INDENT, INDENT, _h(st["name"]), ", ".join(sp), stack]
		if a.has("splash"):
			var spl: Dictionary = a["splash"]
			line += "\n%s%s%s: %s" % [INDENT, INDENT, _h("%s Splash" % spl.get("shape", "")), _effects(spl)]
			if bool(spl.get("friendly_fire", false)):
				line += ", Friendly Fire: Yes"
		rows.append(line)
	if rows.is_empty():
		return ""
	return _h("Attack:") + "\n" + INDENT + ("\n" + INDENT).join(rows)

static func _pct(level: float) -> String:
	return "%d%%" % (100 - int(round(pow(0.9, level) * 100.0)))

## getResistanceTooltip: damage, capture and status effects.
static func resistance_text(d: Dictionary, vw: Dictionary) -> String:
	var rows: PackedStringArray = []
	if d.has("armour"):
		var r: Dictionary = d["armour"]
		var parts: PackedStringArray = []
		for t in ["hack", "pierce", "crush"]:
			parts.append("%.1f %s [font_size=11](%s)[/font_size]" % [float(r[t]), t.capitalize(), _pct(float(r[t]))])
		rows.append("%s %s" % [_h("Damage:"), ", ".join(parts)])
	if vw.has("capture_resistance"):
		var c := float(vw["capture_resistance"])
		rows.append("%s %.1f Capture [font_size=11](%s)[/font_size]" % [_h("Capture:"), c, _pct(c)])
	if vw.has("status_resistance"):
		var parts2: PackedStringArray = []
		for code in vw["status_resistance"]:
			var e: Dictionary = vw["status_resistance"][code]
			var block := float(e["block"])
			var dur := float(e["duration"])
			if block >= 1.0:
				parts2.append("Blocks " + code)
			elif block <= 0.0:
				parts2.append("%s Duration reduction: %d%%" % [code, int(round(100.0 - dur * 100.0))])
			elif dur >= 1.0:
				parts2.append("%s Blocks: %d%%" % [code, int(round(block * 100.0))])
			else:
				parts2.append("%s Blocks: %d%%, Duration reduction: %d%%" % [code, int(round(block * 100.0)), int(round(100.0 - dur * 100.0))])
		rows.append("%s %s" % [_h("Status Effects:"), ", ".join(parts2)])
	if rows.is_empty():
		return ""
	return _h("Resistance:") + "\n" + INDENT + ("\n" + INDENT).join(rows)

## gui/common/tooltips.js lines in ViewerPage.StatsFunctions order: cost, then
## ReferencePage.StatsFunctions (dropsite, health, attack, healer, resistance,
## garrison, turrets, projectiles, speed, gather, supply, treasure, population
## bonus, trickle, upkeep, loot).
static func stats_text(d: Dictionary, vw: Dictionary = {}) -> String:
	var lines: PackedStringArray = []
	var cost := cost_line(d)
	if cost != "":
		lines.append(_h("Cost:") + " " + cost)
	if vw.has("dropsite"):
		var icons: PackedStringArray = []
		for r in vw["dropsite"]:
			icons.append(_res_icon(r))
		lines.append(_h("Dropsite for:") + " " + "  ".join(icons))
	if d.has("hp"):
		lines.append(_h("Health:") + " " + str(int(round(float(d["hp"])))))
	var atk := attack_text(d, vw)
	if atk != "":
		lines.append(atk)
	var heal: Dictionary = vw.get("heal", {})
	if heal.is_empty() and d.has("heal"):
		heal = {"health": d["heal"]["hp"], "range": d["heal"]["range"], "interval": d["heal"]["interval"]}
	if not heal.is_empty():
		lines.append("%s %s Health, %s %d meters, %s %s" % [_h("Heal:"), _trim(float(heal["health"])),
			_h("Range:"), int(round(float(heal["range"]))), _h("Interval:"), _seconds(heal["interval"])])
	var res := resistance_text(d, vw)
	if res != "":
		lines.append(res)
	var capacity := int(d.get("garrison", vw.get("garrison", {}).get("capacity", 0)))
	var buff := float(d.get("garrison_heal", vw.get("garrison", {}).get("buff_heal", 0)))
	if d.has("garrison") or vw.has("garrison"):
		lines.append("%s: %d" % [_h("Garrison Limit"), capacity])
		if buff > 0.0:
			lines.append("%s %d Health / second" % [_h("Heal:"), int(round(buff))])
	if float(vw.get("garrison_size", 1)) > 1.0:
		lines.append("%s: %s" % [_h("Garrison Size"), _trim(float(vw["garrison_size"]))])
	if int(vw.get("turrets", 0)) > 0:
		lines.append("%s: %d" % [_h("Turret Positions"), int(vw["turrets"])])
	var sim_atk: Dictionary = d.get("attack", {})
	if sim_atk.has("per_unit") and capacity > 0:
		var limit := int(sim_atk["arrows"]) + int(round(float(sim_atk["per_unit"]) * capacity))
		if int(sim_atk.get("max_arrows", 0)) > 0:
			limit = mini(limit, int(sim_atk["max_arrows"]))
		if limit > 0:
			lines.append("%s: %d, %s: %d, %s: %s" % [_h("Projectile Limit"), limit, _h("Default"), int(sim_atk["arrows"]),
				_h("Per Unit"), _trim(float(sim_atk["per_unit"]))])
	if vw.has("speed"):
		var sp: Dictionary = vw["speed"]
		if float(sp["walk"]) > 0.0 or float(sp["run"]) > 0.0:
			var walk := float(d.get("walk_speed", sp["walk"]))
			var run := float(sp["run"]) * (walk / float(sp["walk"]) if float(sp["walk"]) > 0.0 else 1.0)
			lines.append("%s %.1f Walk, %.1f Run, %.1f Acceleration" % [_h("Speed:"), walk, run, float(sp["acceleration"])])
	if vw.has("gather") and not vw["gather"].is_empty():
		var rates: PackedStringArray = []
		for k in vw["gather"]:
			rates.append("%s %.2f" % [_res_icon(k), float(vw["gather"][k])])
		lines.append(_h("Gather Rates:") + " " + "  ".join(rates))
	if vw.has("supply"):
		var amt = vw["supply"]["amount"]
		lines.append("%s %s %s" % [_h("Resource Supply:"), _res_icon(vw["supply"]["type"]), "∞" if str(amt) == "Infinity" else str(amt)])
	if vw.has("treasure"):
		var tr: PackedStringArray = []
		for r in RES:
			if vw["treasure"].has(r):
				tr.append("%s %s" % [_res_icon(r), str(vw["treasure"][r])])
		lines.append(_h("Reward:") + " " + "  ".join(tr))
	if int(d.get("pop_bonus", 0)) > 0:
		lines.append("%s %d" % [_h("Population Bonus:"), int(d["pop_bonus"])])
	for key in ["trickle", "upkeep"]:
		if vw.has(key):
			var tk: PackedStringArray = []
			for r in RES:
				if vw[key]["rates"].has(r):
					tk.append("%s %s" % [_res_icon(r), str(vw[key]["rates"][r])])
			lines.append("%s %s / %s" % [_h("Resource Trickle:" if key == "trickle" else "Upkeep:"), "  ".join(tk), _seconds(vw[key]["interval"])])
	if d.has("loot"):
		var lp: PackedStringArray = []
		for r2 in RES:
			if int(d["loot"].get(r2, 0)) > 0:
				lp.append("%s %d" % [_res_icon(r2), int(d["loot"][r2])])
		if int(d.get("loot_xp", 0)) > 0:
			lp.append("XP %d" % int(d["loot_xp"]))
		if not lp.is_empty():
			lines.append(_h("Loot:") + " " + "  ".join(lp))
	return "\n".join(lines)

static func _links(items: Array, link_kind: String, key: String) -> String:
	var parts: PackedStringArray = []
	for it in items:
		parts.append(Common.link(link_kind, int(it[key]), Common.entity_name(it["name"], it.get("specific_name", ""))))
	return ", ".join(parts)

## ViewerPage.InfoFunctions: tooltip, history, description, auras, classes,
## built/trained/researched by, then what it builds, trains and researches.
# getAurasTooltip's line: "Name: description", then " Range: N meters" for a range aura.
static func aura_line(a: Dictionary, label := "[b]%s:[/b]") -> String:
	var t: String = (label % a["name"]) + " " + String(a["description"])
	var radius := float(a.get("radius", 0))
	if radius > 0.0:
		t += " Range: %s %s" % [_num(radius), "meter" if is_equal_approx(radius, 1.0) else "meters"]
	return t

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
	var auras: Array = d.get("auras", extra.get("auras", [])) # a structure's from templates.json
	if not auras.is_empty():
		var t := "[b]Auras:[/b]"
		for a in auras:
			t += "\n" + aura_line(a, "[color=#c9a0ff]%s:[/color]")
		blocks.append(t)
	var classes: Array = extra.get("classes", [])
	if not classes.is_empty():
		blocks.append("[b]Classes:[/b] " + ", ".join(classes))
	match kind:
		"unit":
			var by: PackedStringArray = []
			if not d["trained_at"].is_empty():
				by.append(_links(d["trained_at"], "structure", "type"))
			if not d.get("trained_by_units", []).is_empty():
				by.append(_links(d["trained_by_units"], "unit", "tmpl"))
			if not by.is_empty():
				blocks.append("[b]Trained by:[/b] " + ", ".join(by))
			if d.has("requires"):
				blocks.append("[b]Requires:[/b] " + Common.link("tech", int(d["requires"]["id"]), d["requires"]["name"]))
			var builds := _buildable(extra.get("builds", []))
			if not builds.is_empty():
				blocks.append("[b]Builds:[/b] " + _links(builds, "structure", "type"))
			if not d.get("trains", []).is_empty():
				blocks.append("[b]Trains:[/b] " + _links(d["trains"], "unit", "tmpl"))
			if not d.get("researches", []).is_empty():
				blocks.append("[b]Researches:[/b] " + _links(d["researches"], "tech", "id"))
		"structure":
			var by := _built_by()
			if not by.is_empty():
				blocks.append("[b]Built by:[/b] " + _links(by, "unit", "tmpl"))
			if not d["trains"].is_empty():
				blocks.append("[b]Trains:[/b] " + _links(d["trains"], "unit", "tmpl"))
			if not d["researches"].is_empty():
				blocks.append("[b]Researches:[/b] " + _links(d["researches"], "tech", "id"))
			if not d["upgrades"].is_empty():
				blocks.append("[b]Upgradable to:[/b] " + _links(d["upgrades"], "structure", "type"))
			if not bool(d.get("available", true)):
				blocks.append("[color=#ff8080]The %s cannot build this structure.[/color]" % d["civ_name"])
		"tech":
			var at: PackedStringArray = []
			for r in d.get("researchers", []):
				if r["kind"] == "unit":
					at.append(Common.link("unit", int(r["tmpl"]), Common.entity_name(r["name"], r.get("specific_name", ""))))
				else:
					at.append(Common.link("structure", int(r["type"]), r["name"]))
			if at.is_empty():
				at.append(Common.link("structure", int(d["researched_at"]["type"]), d["researched_at"]["name"]))
			blocks.append("[b]Researched at:[/b] " + ", ".join(at))
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

## The Training panel's lines with Options' Detailed tooltips (showdetailedtooltips;
## selection_panels.js: health, attack, healer, resistance, garrison, turrets,
## projectiles, speed and dropsite tooltips), as plain text for a list tooltip.
static func detailed_tooltip(tmpl: int) -> String:
	var d: Dictionary = Common.ref().get_unit(tmpl)
	if d.is_empty():
		return ""
	d = Common.civ_costed(d, "unit", int(d.get("civ", 0)))
	var vw: Dictionary = Common.unit_text(d.get("id", "")).get("viewer", {})
	var keep := ["Health:", "Attack:", "Heal:", "Resistance:", "Garrison Limit", "Turret Positions", "Projectile Limit", "Speed:", "Dropsite for:"]
	var out: PackedStringArray = []
	var on := false
	var plain := _plain(stats_text(d, vw))
	for line in plain.split("\n"):
		if not line.begins_with(" ") and not line.begins_with("\t"):
			on = keep.any(func(k): return line.begins_with(k))
		if on:
			out.append(line)
	return "\n".join(out)

## BBCode to plain text: a resource icon becomes its name.
static func _plain(t: String) -> String:
	var icon := RegEx.create_from_string("\\[img=[^\\]]*\\]res://assets/textures/ui/icons/(\\w+)\\.png\\[/img\\]")
	for m in icon.search_all(t):
		t = t.replace(m.get_string(), m.get_string(1).capitalize())
	return RegEx.create_from_string("\\[/?[a-z_]+(=[^\\]]*)?\\]").sub(t, "", true)
