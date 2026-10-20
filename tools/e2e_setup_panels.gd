extends SceneTree
# End-to-end check of Game Setup's 0.28 panels (gui/gamesetup/Pages/GameSetupPage):
#   1. GameDescription.js: the settings as getGameDescription words them,
#      following a change (Regicide) without anything else being touched;
#   2. ResetCivsButton.js / ResetTeamsButton.js: every civ back to Random and
#      every team back to None; neither shows on a scenario;
#   3. TipsPanel.js: single player shows the newcomer tip under the slots until its
#      tick box is cleared (gui.gamesetup.enabletips); cleared, the next page has none;
#   4. GameSettingTabs.js: the Map, Player and Game Type tabs each show their own
#      settings (GameSettingsLayout.js), the open one's button closes it, so does
#      Escape (cancel), and the host or single player starts on Map;
#   5. GameSpeed.js: 0.28's named speeds, Fast Forward only with nobody in a player slot;
#   6. the dropdown entries' own hover tooltips (civ History, map sizes, resources...).
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_setup_panels.gd
# Exit code 0 = pass.

var failures := 0

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func run() -> void:
	change_scene_to_file("res://scenes/menu/gamesetup.tscn")
	await frames(12)
	var page = current_scene
	var desc: RichTextLabel = page.get_node("VBox/HBox/Right/Preview/Info/GameDescription")
	var name := str(page._selected_map()["name"])
	check(desc.text.contains("[color=orange]Map Name[/color] " + name), "the description names the map (%s)" % name)
	check(desc.text.contains("[color=orange]Conquest[/color]") and not desc.text.contains("Regicide"), "Conquest, no Regicide")
	page._set_victory_mask(1 | 64)
	await frames(10)
	check(desc.text.contains("[color=orange]Regicide[/color]"), "checking Regicide shows in the description")

	print("2. Reset buttons")
	page._slot_rows[0]["civ"] = int(page.CIVS[0]["id"])
	page._slot_rows[1]["civ"] = int(page.CIVS[1]["id"])
	page._slot_rows[0]["team"] = 1
	page._slot_rows[1]["team"] = 2
	page.refresh_slots()
	await frames(2)
	var civs: Button = page.get_node("VBox/HBox/Right/SlotsBox/SlotCountRow/ResetCivs")
	var teams: Button = page.get_node("VBox/HBox/Right/SlotsBox/SlotCountRow/ResetTeams")
	check(civs.visible and teams.visible and civs.tooltip_text == "Reset any civilizations that have been selected to the default (random).", "both buttons on a random map")
	civs.pressed.emit()
	await frames(2)
	check(page._slot_rows.slice(0, 2).all(func(r): return int(r["civ"]) == page.RANDOM_CIV), "Reset Civs: every civ Random")
	check(int(page._slot_rows[0]["team"]) == 1 and int(page._slot_rows[1]["team"]) == 2, "and the teams kept")
	teams.pressed.emit()
	await frames(2)
	check(page._slot_rows.slice(0, 2).all(func(r): return int(r["team"]) == 0), "Reset Teams: every team None")
	page._select_map_id("acropolis_bay")
	page.refresh_map_list()
	await frames(2)
	if page._is_scenario():
		check(not civs.visible and not teams.visible, "a scenario hides them")
	else:
		page._on_map_type_selected(2)
		await frames(2)
		check(not page._is_scenario() or (not civs.visible and not teams.visible), "a scenario hides them")

	print("3. Tips")
	var cfg := RtsoUserConfig.new()
	var keep_tips: String = cfg.get_value("gui.gamesetup.enabletips")
	cfg.set_value("gui.gamesetup.enabletips", "true")
	change_scene_to_file("res://scenes/menu/gamesetup.tscn")
	await frames(12)
	page = current_scene
	var tips: Control = page.get_node_or_null("VBox/HBox/Right/SPTips")
	check(tips != null and tips.visible and tips.find_child("AITips", true, false).text.begins_with("Although reasonably easy"),
		"the newcomer tip shows in single player")
	if tips:
		var tick: CheckBox = tips.find_child("DisplaySPTips", true, false)
		tick.button_pressed = false
		await frames(1)
		check(not cfg.get_bool("gui.gamesetup.enabletips"), "clearing the tick box turns the tips off")
	change_scene_to_file("res://scenes/menu/gamesetup.tscn")
	await frames(12)
	check(current_scene.get_node_or_null("VBox/HBox/Right/SPTips") == null, "and the next Game Setup has none")
	cfg.set_value("gui.gamesetup.enabletips", keep_tips)
	cfg.save()

	print("4. Settings tabs")
	page = current_scene
	var shown := func() -> Array:
		return ["MapTab", "PlayerTab", "GameTypeTab"].filter(func(t): return page.get_node("%" + t).visible)
	check(shown.call() == ["MapTab"] and page.get_node("%TabMap").button_pressed, "single player opens on the Map tab")
	check(page.get_node("%MapList").get_parent().name == "MapTab" and page.get_node("%Nomad").is_visible_in_tree()
		and not page.get_node("%PopCapSlot").is_visible_in_tree() and not page.get_node("%Waves").is_visible_in_tree(),
		"Map shows the map settings only")
	page.get_node("%TabPlayer").pressed.emit()
	await frames(1)
	check(shown.call() == ["PlayerTab"] and page.get_node("%PopCapSlot").is_visible_in_tree() and page.get_node("%Cheats").is_visible_in_tree(),
		"Player shows population, resources, spies and cheats")
	page.get_node("%TabGameType").pressed.emit()
	await frames(1)
	check(shown.call() == ["GameTypeTab"] and page.get_node("%VictorySlot").is_visible_in_tree() and page.get_node("%LockTeams").is_visible_in_tree()
		and page.get_node("%SpeedSlot").is_visible_in_tree(), "Game Type shows victory, speed, ceasefire and teams")
	check(page.get_node("%TabGameType").tooltip_text.begins_with("Click to toggle the Game Type settings tab.\nScroll down or use "), "with 0.28's tooltip")
	page.get_node("%TabGameType").pressed.emit()
	await frames(1)
	var gt: Control = page.get_node("%GameTypeTab")
	check(gt.visible and gt.position.x < 0.0 and not page.get_node("%TabGameType").button_pressed, "pressing the open tab slides it out")
	await frames(12)
	check(shown.call().is_empty() and gt.position.x == 0.0, "then closes it")
	page.select_tab(0)
	await frames(2)
	var mt: Control = page.get_node("%MapTab")
	var x0 := mt.position.x
	await frames(2)
	check(x0 < 0.0 and mt.position.x > x0, "opening a tab slides the panel in (%d, %d)" % [x0, mt.position.x])
	await frames(12)
	check(mt.position.x == 0.0, "to its place")
	var found := false
	for sec in load("res://scenes/menu/options.gd").SCHEMA:
		for o in sec[1]:
			if o[1] == "gui.gamesetup.settingsslide" and o[2] == "Enable settings panel slide":
				found = true
	check(found, "Options lists Enable settings panel slide")
	var ucfg := RtsoUserConfig.new()
	ucfg.set_value("gui.gamesetup.settingsslide", "false")
	page.select_tab(-1)
	check(shown.call().is_empty(), "with the slide off a tab closes at once")
	page.select_tab(2)
	await frames(1)
	check(gt.visible and gt.position.x == 0.0, "and opens in place")
	ucfg.set_value("gui.gamesetup.settingsslide", "true")
	page.select_tab(1)
	var esc := InputEventKey.new()
	esc.keycode = KEY_ESCAPE
	esc.physical_keycode = KEY_ESCAPE
	esc.pressed = true
	page._unhandled_input(esc)
	await frames(12)
	check(shown.call().is_empty(), "Escape closes it too")

	print("5. Game Speed (GameSpeed.js, game_speeds.json)")
	var spd = page.get_node("%SpeedSlot").get_child(0)
	var titles := func() -> Array:
		var out := []
		for i in spd.get_item_count():
			out.append(spd.get_item_text(i))
		return out
	check(page.get_node("%SpeedLabel").text == "Game Speed:" and spd.get_item_text(spd.get_selected()) == "Normal (1×)", "Game Speed, Normal (1×) by default")
	check(titles.call() == ["Turtle (0.1×)", "Slow (0.25×)", "Leisurely (0.5×)", "Relaxed (0.75×)", "Normal (1×)",
		"Fast (1.25×)", "Very Fast (1.5×)", "Insane (2×)"], "the eight speeds while you play: %s" % str(titles.call()))
	spd.select(7)
	for i in page._slot_rows.size():
		page._slot_rows[i]["kind"] = 1
	page._render_speeds()
	check(titles.call().size() == 11 and titles.call()[10] == "Fast Forward (20×)" and spd.get_item_text(spd.get_selected()) == "Insane (2×)",
		"Fast Forward too when every slot is an AI, Insane kept")
	spd.select(10)
	check(is_equal_approx(float(page._collect_setup()["speed"]), 20.0), "and the match gets 20×")
	page._slot_rows[0]["kind"] = 0
	page._render_speeds()
	check(titles.call().size() == 8 and spd.get_item_text(spd.get_selected()) == "Normal (1×)", "taking a seat again drops it back to Normal")

	print("6. Entry tooltips (onHoverChange)")
	var opt := func(slot: String): return page.get_node("%" + slot).get_child(0)
	check(opt.call("MapSizeSlot").get_item_tooltip(0) == "This map size has barely any room for even the most basic resources.", "map sizes (map_sizes.json)")
	check(opt.call("ResSlot").get_item_tooltip(1) == "Initial amount of each resource: 300.", "starting resources")
	check(opt.call("PopCapTypeSlot").get_item_tooltip(2) == "Distributes the total population cap evenly over all living players.", "population cap types")
	check(opt.call("MapTypeSlot").get_item_tooltip(2) == "A map with a predefined landscape and matchsettings.", "map types")
	check(opt.call("MapFilterSlot").get_item_tooltip(0) == "All maps except naval and demo maps.", "map filters")
	var civ_opts: Array = page._slots_grid().find_children("*", "RtsoSafeSelect", true, false).filter(func(o): return o.get_item_count() > 10 and o.get_item_text(0) == "Random")
	check(not civ_opts.is_empty() and civ_opts[0].get_item_tooltip(0) == "Picks one civilization at random when the game starts."
		and civ_opts[0].get_item_tooltip(1).length() > 40, "civs: Random, then each civ's History (%s…)" % (civ_opts[0].get_item_tooltip(1).substr(0, 40) if civ_opts else ""))
	print("PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
