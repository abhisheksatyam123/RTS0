extends SceneTree
# End-to-end check of the campaign pages (0 A.D. gui/campaigns):
#   1. Single-player lists New Campaign and Load Campaign, and Continue Campaign
#      greyed out until there is a current run;
#   2. New Campaign lists 0.28's two campaigns and starting the Tutorial writes the
#      run and opens its menu: the Introductory Tutorial is available, the
#      Economy Walkthrough greyed out until the tutorial is done;
#   3. Start on a level sets the match up for its scenario and the run;
#   4. winning the level and closing the summary marks it completed and comes
#      back to the run's menu, where the walkthrough is now available;
#   5. Load Campaign lists the run (a broken one as BrokenRun), Delete removes it
#      and Escape goes back; a replay of a won level completes nothing;
#   7. Requires and Order are read as 0.28's MatchesClassList and indexOf sort do.
#
#   godot --headless --path . --fixed-fps 60 -s tools/e2e_campaigns.gd
# Exit code 0 = pass.

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

func single_labels(menu) -> Array:
	menu._toggle_submenu("single", menu.get_node("MainPanel/Buttons/BtnSingle"))
	return menu.submenu_buttons_box.get_children().filter(func(b): return not b.is_queued_for_deletion()).map(func(b): return b.text)

## The Single-player Continue Campaign button, or null.
func continue_button(menu) -> Button:
	menu._toggle_submenu("single", menu.get_node("MainPanel/Buttons/BtnSingle"))
	for b in menu.submenu_buttons_box.get_children():
		if not b.is_queued_for_deletion() and b.text == "Continue Campaign":
			return b
	return null

func level_texts(page) -> Array:
	var out: Array = []
	out.append_array(page.row_texts())
	return out

func run() -> void:
	# A clean slate: no saved runs.
	for r in CampaignRun.saved_runs():
		r["run"].destroy()
	CampaignRun.clear_current()

	print("1. Single-player")
	change_scene_to_file("res://scenes/menu/main_menu.tscn")
	await frames(4)
	var labels := single_labels(current_scene)
	check(labels.has("New Campaign") and labels.has("Load Campaign"), "New and Load Campaign listed: " + str(labels))
	var cont := continue_button(current_scene)
	check(cont != null and cont.disabled, "Continue Campaign is greyed out without a current run (hasCurrentRun)")

	print("2. New Campaign")
	current_scene._execute_action("new_campaign")
	await frames(6)
	var page = current_scene
	check(page.mode == "setup", "the page opens on the campaign list")
	check(level_texts(page) == ["Demo campaign - new maps", "Tutorial"], "0.28's two campaigns are listed: " + str(level_texts(page)))
	check(page.list.column_titles_visible and page.list.get_column_title(0) == "Name" and page.back_button.text == "Main Menu",
		"the campaign list's heading is Name, the back button Main Menu")
	page.select(0)
	check(page.info_preview.texture is AtlasTexture, "a campaign without an Image shows the no-preview picture")
	page.select(level_texts(page).find("Tutorial"))
	check(page.info_desc.text.replace(char(0xa0), " ") == "Learn how to play 0 A.D.", "its description shows: %s (%d)" % [page.info_desc.text, page.info_desc.text.length()])
	var made: CampaignRun = page.create_run("My run")
	await frames(2)
	check(FileAccess.file_exists(made.path()), "the run is written to " + made.path())
	check(CampaignRun.current_filename() == made.filename, "and becomes the current run")
	check(page.mode == "menu" and page.title.text == "My run - Tutorial", "its menu opens: " + page.title.text)
	var saved: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(made.path()))
	check(saved.get("template_identifier", "") == "tutorial" and not saved.has("template"),
		"the run file names its template like 0 A.D.'s (template_identifier): " + str(saved.keys()))
	check(page.start_button.text == "Start Scenario" and page.info_name.text == "No scenario selected", "Start Scenario, no scenario selected yet")
	var texts := level_texts(page)
	check(texts.size() == 2 and texts[0].begins_with("Introductory Tutorial") and texts[0].ends_with("Available"),
		"the Introductory Tutorial comes first, available: " + str(texts))
	check(texts.size() == 2 and texts[1] == "Economy Walkthrough", "the walkthrough is listed without a status: " + str(texts))
	# CampaignMenu displayLevelsList: Name and Status columns, only "Available" in green,
	# a locked level's name grey.
	var rows: Array = page.list.get_root().get_children()
	check(page.list.columns == 2 and page.list.column_titles_visible and rows[0].get_text(1) == "Available"
		and page.list.get_column_title(0) == "Scenario Name"
		and rows[0].get_custom_color(1) == page.GREEN and rows[0].get_custom_color(0) == page.NAME_COLOR
		and rows[1].get_custom_color(0) == page.LOCKED,
		"the status is its own column, green when available; names in 172 172 212, the locked name gray")
	check(not page.empty_text.visible, "no empty-list text on the level list")
	# translateWithContext: the names and statuses follow the language (German here).
	var locale_before := TranslationServer.get_locale()
	var de: Translation = load("res://assets/l10n/de.po")
	de.locale = "de"
	TranslationServer.add_translation(de)
	TranslationServer.set_locale("de")
	page.render()
	rows = page.list.get_root().get_children()
	check(rows[0].get_text(0) == "Interaktive Einführung" and rows[0].get_text(1) == "Verfügbar",
		"the level name and status are translated: %s / %s" % [rows[0].get_text(0), rows[0].get_text(1)])
	TranslationServer.set_locale(locale_before)
	TranslationServer.remove_translation(de)
	page.render()
	page.select(1)
	check(page.start_button.disabled, "the walkthrough cannot start before the tutorial is done")
	check(page.info_preview.texture != null and page.info_preview.visible,
		"the walkthrough, with no preview of its own, shows the no-preview picture")
	page.select(0)
	check(not page.start_button.disabled and page.info_name.text == "Introductory Tutorial", "the tutorial can start")
	check(page.info_preview.texture != null, "with its map preview")

	print("3. Start")
	var ms = root.get_node("MatchSettings")
	# Left over from an earlier Game Setup: a level starts from fresh settings (new GameSettings()).
	ms.local_slot = 1
	ms.pop_cap = 50
	ms.start_res = 3
	ms.game_speed = 2.0
	ms.reveal_map = true
	ms.ceasefire_min = 10
	ms.record_replay = false
	page.start_level("introduction")
	check(ms.local_slot == 0 and ms.pop_cap == 300 and ms.start_res == 1 and ms.game_speed == 1.0 and not ms.reveal_map
		and ms.ceasefire_min == 0, "the level starts from default settings in seat 1: slot %d pop %d res %d speed %s" % [
		ms.local_slot, ms.pop_cap, ms.start_res, ms.game_speed])
	check(not ms.record_replay, "the replay flag is not a match setting")
	ms.record_replay = true
	check(ms.slot_names.size() == 1 and ms.slot_names[0] == load("res://scenes/menu/gamesetup.gd").singleplayer_name(),
		"the local player has the single-player name: " + str(ms.slot_names))
	check(ms.map_id == "introductory_tutorial" and ms.campaign_run == made.filename and ms.campaign_level == "introduction",
		"the match is set up for the tutorial and the run: %s %s %s" % [ms.map_id, ms.campaign_run, ms.campaign_level])
	check(ms.campaign_map_name == "Introductory Tutorial", "the map is named by the level (startScenario mapName): " + ms.campaign_map_name)
	check(ms.slot_types == [0, 1] and ms.cheats, "the local player in seat 1, the scenario's other player, cheats on: " + str(ms.slot_types))
	await frames(2)
	# The loading page's threaded load of the match scene must finish first: a
	# summary scene loaded meanwhile comes back half-built, without its script.
	while ResourceLoader.load_threaded_get_status("res://scenes/main.tscn") == ResourceLoader.THREAD_LOAD_IN_PROGRESS:
		await frames(1)
	# Stand in for the match: back to a menu scene before the summary closes.
	change_scene_to_file("res://scenes/menu/main_menu.tscn")
	await frames(4)
	ms.campaign_run = made.filename
	ms.campaign_level = "introduction"

	print("4. The level won")
	# CampaignSession.onFinish: the main node marks the level as soon as it is won.
	load("res://scenes/session/summary_screen.gd").mark_campaign_won()
	var won := CampaignRun.new(made.filename)
	check(won.load_run() and won.is_completed("introduction"), "the level is completed the moment it is won")
	var summary = load("res://scenes/session/summary_screen.tscn").instantiate()
	root.add_child(summary)
	summary._victory = true
	summary.continue_pressed()
	await frames(6)
	page = current_scene
	check(page.get_script().resource_path == "res://scenes/menu/campaigns.gd" and page.mode == "menu", "Continue on the summary comes back to the run's menu")
	var back := CampaignRun.new(made.filename)
	check(back.load_run() and back.is_completed("introduction"), "the tutorial is completed in the run file")
	texts = level_texts(page)
	check(texts.size() == 2 and texts[0].ends_with("Completed") and texts[1].ends_with("Available"),
		"the tutorial shows Completed and the walkthrough Available: " + str(texts))
	check(ms.campaign_run == "", "the match settings no longer point at the run")
	summary.queue_free()

	print("4a. A replay of a won level completes nothing")
	var fresh := CampaignRun.new("zz_e2e_replay")
	fresh.set_template(CampaignRun.template_by_id("tutorial")).save()
	ms.campaign_run = fresh.filename
	ms.campaign_level = "introduction"
	load("res://scenes/session/summary_screen.gd").leave_campaign(true, true)
	await frames(6)
	check(fresh.load_run() and not fresh.is_completed("introduction") and current_scene.mode == "menu",
		"quitting the replay opens the run's menu without completing the level")
	fresh.destroy()
	CampaignRun.new(made.filename).set_current()
	change_scene_to_file("res://scenes/menu/campaigns.tscn")
	await frames(4)
	page = current_scene

	print("4b. Saved Games (CampaignMenu.loadSavegame)")
	DirAccess.make_dir_recursive_absolute("user://saves")
	var v: int = RtsoMain.get_save_version()
	for n in [["zz_e2e_camp_in", made.filename], ["zz_e2e_camp_out", ""]]:
		FileAccess.open("user://saves/%s.rts0sav" % n[0], FileAccess.WRITE).store_string("x")
		FileAccess.open("user://saves/%s.json" % n[0], FileAccess.WRITE).store_string(JSON.stringify({
			"save_version": v, "timestamp": 1893510000, "map_id": "introductory_tutorial", "map_name": "Tutorial",
			"settings": {"map_id": "introductory_tutorial", "campaign_run": n[1]}, "players": []}))
	var sg_button: Button = page.find_child("SavedGamesButton", true, false)
	check(sg_button != null and sg_button.visible and sg_button.text == "Saved Games", "the run's menu has Saved Games")
	var sg = page.load_savegame()
	await frames(2)
	var listed: Array = sg.saves.map(func(x): return x.id).filter(func(i): return i.begins_with("zz_e2e_camp"))
	check(listed == ["zz_e2e_camp_in"], "it lists only this run's saves: " + str(listed))
	sg.close()
	await frames(1)
	check(ms.campaign_run == "", "closing it leaves the match settings as they were")
	for n in ["zz_e2e_camp_in", "zz_e2e_camp_out"]:
		for ext in [".rts0sav", ".json"]:
			DirAccess.remove_absolute(ProjectSettings.globalize_path("user://saves/" + n + ext))

	print("5. Load Campaign")
	page.back_to_main_menu()
	await frames(4)
	cont = continue_button(current_scene)
	check(cont != null and not cont.disabled, "Continue Campaign is enabled while a run is current")
	current_scene._execute_action("load_campaign")
	await frames(6)
	page = current_scene
	FileAccess.open("user://saves/campaigns/zz_e2e_broken.0adcampaign", FileAccess.WRITE).store_string("{")
	page.runs = CampaignRun.saved_runs()
	page.render()
	check(page.mode == "load" and level_texts(page) == ["My run - Tutorial", "zz_e2e_broken.0adcampaign (file cannot be loaded)"],
		"the runs are listed, a broken one as BrokenRun: " + str(level_texts(page)))
	page.select(1)
	check(page.start_button.disabled and page.info_name.text == "Name of selected run:" and page.info_desc.text == "zz_e2e_broken.0adcampaign",
		"a broken run cannot load and is named by its file: " + page.info_desc.text)
	page.delete_selected()
	page.select(0)
	page.delete_pressed()
	check(page.confirm != null and page.confirm.message.contains("Are you sure you want to delete run My run"), "Delete asks first (LoadModal messageBox)")
	page.confirm.find_child("Button2", true, false).pressed.emit() # Yes
	await frames(2)
	check(not FileAccess.file_exists(made.path()) and CampaignRun.current_filename() == "", "Delete removes it and the current run")
	check(page.empty_text.visible and page.empty_text.text == "No ongoing campaigns.", "No ongoing campaigns.")
	var esc := InputEventKey.new()
	esc.keycode = KEY_ESCAPE
	esc.physical_keycode = KEY_ESCAPE
	esc.pressed = true
	page._unhandled_input(esc)
	await frames(4)
	check(current_scene.get_script().resource_path == "res://scenes/menu/main_menu.gd", "Escape on Load Campaign goes back to the main menu")

	print("6. Runs named like their campaign, and runs saved before template_identifier")
	var plain := CampaignRun.new("zz_e2e_plain")
	plain.set_template(CampaignRun.template_by_id("tutorial")).set_meta_description("Tutorial").save()
	check(plain.label() == "Tutorial" and plain.label(true) == "Tutorial - Tutorial", "getLabel: just the name unless full: " + plain.label())
	plain.destroy()
	var old_file := "user://saves/campaigns/zz_e2e_old.0adcampaign"
	FileAccess.open(old_file, FileAccess.WRITE).store_string(JSON.stringify({"template": CampaignRun.template_by_id("tutorial"),
		"data": {"completedLevels": ["introduction"]}, "meta": {"userDescription": "Old"}}))
	var old := CampaignRun.new("zz_e2e_old")
	check(old.load_run() and old.is_completed("introduction") and old.template.get("Name", "") == "Tutorial", "an older RTS0 run still loads")
	old.destroy()

	print("7. Requires and Order as 0.28 reads them")
	var t := CampaignRun.new("zz_e2e_rules")
	t.set_template({"identifier": "x", "Name": "X", "Order": ["b", "a"],
		"Levels": {"a": {"Requires": ["one", "two+three"]}, "b": {"Requires": "!one"}, "c": {}, "d": {"Requires": "one+"}}})
	t.data = {"completedLevels": ["two", "three"]}
	check(t.ordered_levels().map(func(e): return e["id"]) == ["c", "d", "b", "a"], "levels missing from Order come first, in template order: "
		+ str(t.ordered_levels().map(func(e): return e["id"])))
	check(t.meets_requirements(t.template["Levels"]["a"]) and t.meets_requirements(t.template["Levels"]["b"]),
		"Requires as an array of alternatives, and !class")
	t.data = {"completedLevels": ["one"]}
	check(not t.meets_requirements(t.template["Levels"]["d"]) and not t.meets_requirements(t.template["Levels"]["b"]),
		"\"one+\" needs an empty class too, as JavaScript's split keeps it")

	print("RESULT: %s (%d failed)" % ["PASS" if failures == 0 else "FAIL", failures])
	quit(1 if failures else 0)
