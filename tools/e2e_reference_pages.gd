extends SceneTree
# End-to-end check of the reference pages (ports of 0 A.D. gui/reference):
#   1. the menu's Civilization Overview and Structure Tree for all 14 civs:
#      bonuses, specific structures and technologies, heroes, history, and a
#      structure box for every structure the civ can build, by phase;
#   2. the Template Viewer for every roster unit, every structure of every
#      civ and every technology: a name, the stats the game plays with, 0 A.D.
#      tooltip text, and links that open other pages;
#   1b. Escape, Alt+Shift+T and Alt+Shift+H on the menu's pages, with their tooltips;
#   3. in a match: the structree and civinfo hotkeys' dialog and the "i"
#      button of the selection details.
#
#   godot --headless --path . --fixed-fps 60 -s tools/e2e_reference_pages.gd
# Exit code 0 = pass.

const Common := preload("res://scenes/reference/reference_common.gd")
const Viewer := preload("res://scenes/reference/template_viewer.gd")

var failures := 0

func check(cond: bool, what: String) -> void:
	if cond:
		print("  ok   ", what)
	else:
		print("  FAIL ", what)
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func key(code: Key, alt_shift := false) -> void:
	var e := InputEventKey.new()
	e.keycode = code
	e.physical_keycode = code
	e.alt_pressed = alt_shift
	e.shift_pressed = alt_shift
	e.pressed = true
	Input.parse_input_event(e)
	await frames(1)
	e = e.duplicate()
	e.pressed = false
	Input.parse_input_event(e)
	await frames(1)

func run() -> void:
	print("1. Civilization Overview and Structure Tree")
	change_scene_to_file("res://scenes/menu/encyclopedia.tscn")
	await frames(3)
	var page = current_scene
	var tabs: TabContainer = page.get_node("Center/Panel/VBox/TabContainer")
	check(tabs.get_child_count() == 2, "two tabs")
	var civinfo = tabs.get_child(0)
	var tree = tabs.get_child(1)
	var ref: RtsoReference = Common.ref()
	var civs: Array = ref.get_civs()
	check(civs.size() == 15, "15 civilizations (with the Germans)")
	var empty_history := []
	for i in civs.size():
		var c := int(civs[i]["id"]) # the Germans are civ 15, after Gaia
		civinfo._on_civ_selected(civinfo._select.get_item_index(c)) # as the dropdown does
		check(tree.civ == c, "%s: the structure tree follows the overview's civ" % civs[i]["name"])
		var txt: String = civinfo._gameplay.text
		var info: Dictionary = ref.get_civ_info(c)
		check(txt.contains("Civilization Bonus") and txt.contains("Team Bonus") and txt.contains("Hero"),
			"%s: bonuses, team bonuses and heroes" % civs[i]["name"])
		check(info["heroes"].size() >= 1, "%s: has heroes (%d)" % [civs[i]["name"], info["heroes"].size()])
		if Common.civ_history(c) == "":
			empty_history.append(civs[i]["code"])
		var st: Array = ref.get_structure_tree(c)
		var boxes := 0
		for row in tree._rows.get_children():
			if row is HFlowContainer:
				boxes += row.get_child_count()
		var pieces := st.filter(func(b): return int(b["type"]) in tree.WALLSET_PIECES).size()
		check(boxes == st.size() - pieces and boxes >= 20, "%s: a box for each of its %d structures, wallset pieces on their wallset" % [civs[i]["name"], st.size() - pieces])
		var trains := 0
		for b in st:
			trains += b["units"].size()
		check(trains > 10, "%s: structures train %d units" % [civs[i]["name"], trains])
	check(empty_history.is_empty(), "every civ has its 0 A.D. history " + str(empty_history))
	# ProductionRowManager: the wallset box draws its tower in the first row.
	civinfo._on_civ_selected(civinfo._select.get_item_index(0))
	var wall_tower := false
	for row in tree._rows.get_children():
		if row is HFlowContainer:
			for box in row.get_children():
				for n in box.find_children("*", "", true, false):
					if n is Button and n.get("tip_kind") == "structure" and n.get("tip_id") == 22:
						wall_tower = true
	check(wall_tower, "the stone wall box shows the wall tower")
	# TemplateParser under the civ's modifiers: Athenian Democracy prices Civic Centre
	# techs x0.7 (time x0.5); Spartan heroes are free (Peloponnesian League).
	var cc_tip: Button = null
	for n in tree._rows.find_children("*", "Button", true, false):
		if n.get("tip_kind") == "tech" and n.get("tip_researcher") == 0:
			var raw: Dictionary = ref.get_tech(n.tip_id, 0)
			if int(raw["cost"].get("food", 0)) > 0:
				cc_tip = n
				break
	if cc_tip:
		var raw: Dictionary = ref.get_tech(cc_tip.tip_id, 0)
		check(cc_tip._tooltip_text().contains("Food %d" % roundi(int(raw["cost"]["food"]) * 0.7)),
			"an Athenian Civic Center tech costs 30% less")
	else:
		check(false, "an Athenian Civic Center tech costs 30% less")
	# getAurasTooltip: a structure's auras (templates.json) with their range, and a unit's
	# token-merged class auras before its own.
	var temple_tip: Button = null
	for n in tree._rows.find_children("*", "Button", true, false):
		if n.get("tip_kind") == "structure" and n.get("tip_id") == 9:
			temple_tip = n
			break
	check(temple_tip != null and temple_tip._tooltip_text().contains("Medical Treatment:") and temple_tip._tooltip_text().contains("Range: 40 meters"),
		"the temple's tooltip lists its Medical Treatment aura and range")
	var hero_auras: Array = ref.get_unit(int(ref.get_civ_info(1)["heroes"][0]["tmpl"]))["auras"]
	check(hero_auras.size() >= 3 and hero_auras[0]["name"] == "Inspired Defense", "a hero lists Inspired Defense first")
	var spart := 1
	var spart_hero: Dictionary = ref.get_civ_info(spart)["heroes"][0]
	var hc: Dictionary = Common.civ_costed(ref.get_unit(int(spart_hero["tmpl"])), "unit", spart)["cost"]
	check(int(hc["food"]) + int(hc["wood"]) + int(hc["metal"]) == 0, "Spartan heroes are free")
	# 0.28 civinfo: civ bonuses from auto-researched civ techs and player auras besides
	# {civ}.json, team bonuses from the player auras, (i) info hovers, and only the
	# techs and structures 0.28 counts as civ specific.
	check(Common.civ_code(civinfo._select.get_item_id(0)) == "athen" and Common.civ_code(civinfo._select.get_item_id(1)) == "brit", "civs sorted by name")
	civinfo._on_civ_selected(civinfo._select.get_item_index(0))
	var athen: String = civinfo._gameplay.text
	check(athen.contains("Delian League") and athen.contains("Silver Owls") and athen.contains("Democracy"),
		"Athenian bonuses: Delian League (tech), Silver Owls (civ json), Democracy (team)")
	check(athen.contains("[hint=") and athen.contains("Ships −25% construction time."), "entries carry an info hover and the tooltip text")
	var own: Array = Common.text()["civs"]["athen"]["specific_structures"]
	check(athen.contains("Specific Structures"), "Athenians have specific structures")
	check(athen.contains("Long Walls") and not athen.contains("Wicker Baskets"), "specific technologies only")
	check(athen.contains("Gymnasium") and athen.count("[url=structure:") == own.size(), "specific structures only (%d)" % own.size())
	# 0.28 structree: the civ header, a production row per phase under each box, the
	# Trainer Units section and EntityBox tooltips.
	civinfo._on_civ_selected(civinfo._select.get_item_index(0)) # Athenians: their warships train marines
	check(tree._civ_name.text == civs[0]["name"] and tree._history.text != "", "the tree shows the civ's name and history")
	check(tree._trainer_section.visible and tree._trainers.get_child_count() >= 2, "Athenian warships are Trainer Units")
	var village_box: Control = null
	for row in tree._rows.get_children():
		if row is HFlowContainer and village_box == null and row.get_child_count() > 0:
			village_box = row.get_child(0)
	var bars := 0
	for c in village_box.get_child(0).get_children():
		if c is PanelContainer:
			bars += 1
	check(bars == 3, "a village structure has a production row for each phase (%d)" % bars)
	var tip_btn = village_box.get_child(0).get_child(1)
	var tip: String = tip_btn._tooltip_text()
	check(tip.contains("Cost:") and tip.contains("Health:"), "a structure's tooltip has its cost and stats")
	civinfo._on_civ_selected(civinfo._select.get_item_index(1)) # Spartans: no trainer units
	check(not tree._trainer_section.visible, "the Trainer Units section hides when a civ has none")

	print("2. Template Viewer")
	var bad := []
	var no_tooltip := 0
	var units := 0
	for civ in civs:
		for b in ref.get_structure_tree(int(civ["id"])):
			for u in b["units"]:
				units += 1
				var d: Dictionary = ref.get_unit(int(u["tmpl"]))
				if d.is_empty() or d["name"] == "" or float(d["hp"]) <= 0:
					bad.append(u["id"])
				if Common.unit_text(u["id"]).get("tooltip", "") == "" and not u["id"].contains("rts0_"):
					no_tooltip += 1
	check(units > 300 and bad.is_empty(), "%d trainable units have stats %s" % [units, str(bad)])
	print("  units without a 0 A.D. tooltip: ", no_tooltip)
	# The first Athenian hero that fights (Hippocrates heals and has no attack, as in 0.28).
	var hero := -1
	for h in ref.get_civ_info(0)["heroes"]:
		if hero < 0 and ref.get_unit(int(h["tmpl"])).has("attack"):
			hero = int(h["tmpl"])
	var v = Viewer.open(page, "unit", hero, 0)
	await frames(2)
	check(v._stats.text.contains("Health") and v._stats.text.contains("Attack") and v._stats.text.contains("Resistance"),
		"a hero shows health, attack and resistance")
	check(v._info.text.contains("Auras"), "a hero shows its auras")
	check(v._info.text.contains("Trained by"), "a hero shows where it is trained")
	# Follow a link, then go back.
	var tc_name: String = ref.get_structure(0, 0)["name"]
	v.show_template("unit", int(ref.get_structure_tree(0)[0]["units"][0]["tmpl"]), 0)
	check(v._info.text.contains("[url=structure:"), "a unit links to the structures that train it")
	check(v._info.text.contains("Builds:") or v._info.text.contains("Trained by"), "a citizen lists what it builds")
	v._on_meta("structure:0")
	check(v.kind == "structure" and v._name.text.contains(tc_name) or v._name.text.contains("Civic"), "the link opens the civic centre")
	check(v._info.text.contains("Trains:") and v._info.text.contains("Built by:"), "a civic centre lists what it trains and who builds it")
	v._go_back()
	check(v.kind == "unit", "Back returns to the unit")
	# 0.28 tooltips.js lines: a citizen's speed and gather rates, a tower's arrows.
	check(v._stats.text.contains("Walk,") and v._stats.text.contains("Gather Rates:"), "a citizen shows its speed and gather rates")
	v.show_template("structure", 4, 0)
	check(v._stats.text.contains("Bow[/b]:") and v._stats.text.contains("Projectile Limit") and v._stats.text.contains("Range:[/b] 10 to 60 (+"),
		"a defense tower shows its bow, arrows and parabolic range")
	v.show_template("structure", 0, 0)
	check(v._stats.text.contains("Dropsite for:") and v._stats.text.contains("Rate:[/b] 6 arrows"), "a civic centre is a dropsite with an arrow rate")
	var bad_bld := []
	for civ in civs:
		var c := int(civ["id"])
		for b in ref.get_structure_tree(c):
			v.show_template("structure", int(b["type"]), c)
			if not v._stats.text.contains("Health") or v._name.text == "":
				bad_bld.append("%s/%s" % [civ["code"], b["name"]])
	check(bad_bld.is_empty(), "every civ's structures show their stats " + str(bad_bld))
	var techs := 0
	var bad_tech := []
	for t in range(1, 104):
		var d: Dictionary = ref.get_tech(t, 0)
		if d.is_empty():
			continue
		techs += 1
		v.show_template("tech", t, 0)
		if not v._info.text.contains("Researched at"):
			bad_tech.append(d["name"])
	check(techs > 90 and bad_tech.is_empty(), "%d technologies show where they are researched %s" % [techs, str(bad_tech)])
	v.close_viewer()
	await frames(2)
	check(page.get_node_or_null("TemplateViewer") == null, "Close removes the viewer")

	print("1b. Page hotkeys (StructreePage.js / CivInfoPage.js)")
	tabs.current_tab = 1
	await frames(1)
	check(page.get_node("Center/Panel/VBox/Header/BtnBack").tooltip_text == "Escape: Close Structure Tree."
		and tabs.get_tab_bar().get_tab_tooltip(0) == "Alt+Shift+H: Switch to Civilization Overview.",
		"the close and switch tooltips: " + page.get_node("Center/Panel/VBox/Header/BtnBack").tooltip_text)
	await key(KEY_H, true)
	check(tabs.current_tab == 0, "Alt+Shift+H switches to the Civilization Overview")
	await key(KEY_T, true)
	check(tabs.current_tab == 1, "Alt+Shift+T switches back to the Structure Tree")
	await key(KEY_ESCAPE)
	await frames(3)
	check(current_scene != page and str(current_scene.scene_file_path).ends_with("main_menu.tscn"), "Escape closes the page")

	print("3. In a match")
	var ms = root.get_node("MatchSettings")
	ms.map_id = "random_continental"
	ms.seed = 777
	ms.map_size = 128
	ms.slot_types = [0, 1]
	ms.slot_civs = [3, 1]
	ms.slot_teams = [1, 2]
	ms.victory = 3
	ms.replay_dir = ""
	ms.load_save_path = ""
	change_scene_to_file("res://scenes/main.tscn")
	await frames(30)
	var m = current_scene
	var dlg = m.get_node("TechTreeDialog")
	dlg.toggle()
	await frames(2)
	check(dlg.visible and dlg._tree.civ == 3, "the structree hotkey opens the player's civ (Romans)")
	dlg.toggle_civinfo()
	await frames(2)
	check(dlg.visible and dlg._tabs.current_tab == 1, "the civinfo hotkey switches to the overview")
	dlg.toggle_civinfo()
	check(not dlg.visible, "pressing it again closes the dialog")
	check(m.select_first_own(true), "select the civic centre")
	m._on_template_info_pressed()
	await frames(2)
	var tv = m.find_child("TemplateViewer*", true, false)
	check(tv != null and tv.kind == "structure" and tv.id == 0 and tv.civ == 3, "the i button opens the selected structure")
	if tv:
		tv.close_viewer()
	await frames(2)
	check(m.select_first_own(false), "select a unit")
	m._on_template_info_pressed()
	await frames(2)
	tv = m.find_child("TemplateViewer*", true, false)
	if tv:
		print("  viewer: ", tv.kind, " ", tv.id, " civ ", tv.civ)
	check(tv != null and tv.kind == "unit" and tv.civ == 3, "the i button opens the selected unit's template")
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(0 if failures == 0 else 1)
