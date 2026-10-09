extends SceneTree
# End-to-end check of the reference pages (ports of 0 A.D. gui/reference):
#   1. the menu's Civilization Overview and Structure Tree for all 14 civs:
#      bonuses, specific structures and technologies, heroes, history, and a
#      structure box for every structure the civ can build, by phase;
#   2. the Template Viewer for every roster unit, every structure of every
#      civ and every technology: a name, the stats the game plays with, 0 A.D.
#      tooltip text, and links that open other pages;
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
	check(civs.size() == 14, "14 civilizations")
	var empty_history := []
	for c in civs.size():
		civinfo._on_civ_selected(c) # as the dropdown does
		check(tree.civ == c, "%s: the structure tree follows the overview's civ" % civs[c]["name"])
		var txt: String = civinfo._gameplay.text
		var info: Dictionary = ref.get_civ_info(c)
		check(txt.contains("Civilization Bonus") and txt.contains("Team Bonus") and txt.contains("Hero"),
			"%s: bonuses, team bonuses and heroes" % civs[c]["name"])
		check(info["heroes"].size() >= 1, "%s: has heroes (%d)" % [civs[c]["name"], info["heroes"].size()])
		if Common.civ_history(c) == "":
			empty_history.append(civs[c]["code"])
		var st: Array = ref.get_structure_tree(c)
		var boxes := 0
		for row in tree._rows.get_children():
			if row is HFlowContainer:
				boxes += row.get_child_count()
		check(boxes == st.size() and boxes >= 20, "%s: a box for each of its %d structures" % [civs[c]["name"], st.size()])
		var trains := 0
		for b in st:
			trains += b["units"].size()
		check(trains > 10, "%s: structures train %d units" % [civs[c]["name"], trains])
	check(empty_history.is_empty(), "every civ has its 0 A.D. history " + str(empty_history))

	print("2. Template Viewer")
	var bad := []
	var no_tooltip := 0
	var units := 0
	for c in civs.size():
		for b in ref.get_structure_tree(c):
			for u in b["units"]:
				units += 1
				var d: Dictionary = ref.get_unit(int(u["tmpl"]))
				if d.is_empty() or d["name"] == "" or float(d["hp"]) <= 0:
					bad.append(u["id"])
				if Common.unit_text(u["id"]).get("tooltip", "") == "" and not u["id"].contains("rts0_"):
					no_tooltip += 1
	check(units > 300 and bad.is_empty(), "%d trainable units have stats %s" % [units, str(bad)])
	print("  units without a 0 A.D. tooltip: ", no_tooltip)
	var v = Viewer.open(page, "unit", int(ref.get_civ_info(0)["heroes"][0]["tmpl"]), 0)
	await frames(2)
	check(v._stats.text.contains("Health") and v._stats.text.contains("Attack") and v._stats.text.contains("Resistance"),
		"a hero shows health, attack and resistance")
	check(v._info.text.contains("Auras"), "a hero shows its auras")
	check(v._info.text.contains("Trained at"), "a hero shows where it is trained")
	# Follow a link, then go back.
	var tc_name: String = ref.get_structure(0, 0)["name"]
	v.show_template("unit", int(ref.get_structure_tree(0)[0]["units"][0]["tmpl"]), 0)
	check(v._info.text.contains("[url=structure:"), "a unit links to the structures that train it")
	check(v._info.text.contains("Builds:") or v._info.text.contains("Trained at"), "a citizen lists what it builds")
	v._on_meta("structure:0")
	check(v.kind == "structure" and v._name.text.contains(tc_name) or v._name.text.contains("Civic"), "the link opens the civic centre")
	check(v._info.text.contains("Trains:") and v._info.text.contains("Built by:"), "a civic centre lists what it trains and who builds it")
	v._go_back()
	check(v.kind == "unit", "Back returns to the unit")
	var bad_bld := []
	for c in civs.size():
		for b in ref.get_structure_tree(c):
			v.show_template("structure", int(b["type"]), c)
			if not v._stats.text.contains("Health") or v._name.text == "":
				bad_bld.append("%s/%s" % [civs[c]["code"], b["name"]])
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
