extends SceneTree
# End-to-end check of the campaign pages (0 A.D. gui/campaigns):
#   1. Single-player lists New Campaign and Load Campaign, and Continue Campaign
#      only while there is a current run;
#   2. New Campaign lists 0 A.D.'s Tutorial campaign and starting it writes the
#      run and opens its menu: the Introductory Tutorial is available, the
#      Economy Walkthrough greyed out until the tutorial is done;
#   3. Start on a level sets the match up for its scenario and the run;
#   4. winning the level and closing the summary marks it completed and comes
#      back to the run's menu, where the walkthrough is now available;
#   5. Load Campaign lists the run and Delete removes it.
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
	check(not labels.has("Continue Campaign"), "no Continue Campaign without a current run")

	print("2. New Campaign")
	current_scene._execute_action("new_campaign")
	await frames(6)
	var page = current_scene
	check(page.mode == "setup", "the page opens on the campaign list")
	check(level_texts(page).has("Tutorial"), "0 A.D.'s Tutorial campaign is listed: " + str(level_texts(page)))
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
		and rows[0].get_custom_color(1) == page.GREEN and rows[0].get_custom_color(0) != page.GREEN
		and rows[1].get_custom_color(0) == page.GRAY,
		"the status is its own column, green when available; the locked name is grey")
	page.select(1)
	check(page.start_button.disabled, "the walkthrough cannot start before the tutorial is done")
	page.select(0)
	check(not page.start_button.disabled and page.info_name.text == "Introductory Tutorial", "the tutorial can start")
	check(page.info_preview.texture != null, "with its map preview")

	print("3. Start")
	var ms = root.get_node("MatchSettings")
	page.start_level("introduction")
	check(ms.map_id == "introductory_tutorial" and ms.campaign_run == made.filename and ms.campaign_level == "introduction",
		"the match is set up for the tutorial and the run: %s %s %s" % [ms.map_id, ms.campaign_run, ms.campaign_level])
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
	check(single_labels(current_scene).has("Continue Campaign"), "Continue Campaign is listed while a run is current")
	current_scene._execute_action("load_campaign")
	await frames(6)
	page = current_scene
	check(page.mode == "load" and level_texts(page) == ["My run - Tutorial"], "the run is listed: " + str(level_texts(page)))
	page.select(0)
	page.delete_pressed()
	check(page.confirm != null and page.confirm.message.contains("Are you sure you want to delete run My run"), "Delete asks first (LoadModal messageBox)")
	page.confirm.find_child("Button2", true, false).pressed.emit() # Yes
	await frames(2)
	check(not FileAccess.file_exists(made.path()) and CampaignRun.current_filename() == "", "Delete removes it and the current run")
	check(page.empty_text.visible and page.empty_text.text == "No ongoing campaigns.", "No ongoing campaigns.")

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

	print("RESULT: %s (%d failed)" % ["PASS" if failures == 0 else "FAIL", failures])
	quit(1 if failures else 0)
