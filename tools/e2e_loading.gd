extends SceneTree
# End-to-end check of the loading page (port of 0 A.D. gui/loading) and the
# manual (gui/manual):
#   1. starting a match from Game Setup goes through the loading page, which
#      shows the map title, a tip with its picture and title, and a quote;
#   2. the progress bar follows RtsoMain's load_progress stages up to 100%,
#      then the page goes away and the match is the current scene;
#   3. scenario maps say "Loading", random maps "Generating", and a saved game
#      takes its map name from the save;
#   4. the manual replaces hotkey names with the current bindings.
#
#   godot --headless --path . --fixed-fps 60 -s tools/e2e_loading.gd
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

func run() -> void:
	var Loading = load("res://scenes/menu/loading.gd")
	print("1. a match starts through the loading page")
	var ms = root.get_node("MatchSettings")
	ms.map_id = "random_continental"
	ms.seed = 4242
	ms.map_size = 128
	ms.slot_types = [0, 1]
	ms.slot_civs = [0, 1]
	ms.slot_teams = [1, 2]
	ms.victory = 3
	ms.replay_dir = ""
	ms.load_save_path = ""
	ms.go("res://scenes/main.tscn")
	await frames(2)
	var page = current_scene
	check(page != null and page.get_script() == Loading, "the loading page is shown first")
	if page == null or page.get_script() != Loading:
		quit(1)
		return
	check(page.title.begins_with("Generating “Continental"), "random map title: " + page.title)
	var tip_title: Label = page.find_child("TipTitle", true, false)
	var tip_text: RichTextLabel = page.find_child("TipText", true, false)
	var tip_image: TextureRect = page.find_child("TipImage", true, false)
	check(tip_title != null and tip_title.text != "" and tip_title.text == tip_title.text.to_upper(), "a tip title: " + (tip_title.text if tip_title else ""))
	check(tip_text != null and tip_text.text.begins_with("[img=8x8]res://assets/textures/ui/tipdisplay/bullet-point.png[/img] "), "the tip's lines start with 0.28's BulletPoint")
	var pic: String = "res://assets/textures/ui/loading/tips/" + page.find_child("TipDisplay", true, false).current_image()
	check(tip_image != null and (tip_image.texture != null or not ResourceLoader.exists(pic)), "the tip has its picture (when imported)")
	var quote: Label = page.find_child("Quote", true, false)
	check(quote != null and quote.text.contains("—"), "a quote with its author")
	var bar: Label = page.find_child("ProgressText", true, false)
	check(bar != null and bar.text.ends_with("%"), "the bar shows a percentage")
	var last := -1
	var monotonic := true
	var t0 := Time.get_ticks_msec()
	while Time.get_ticks_msec() - t0 < 120000:
		if not is_instance_valid(page):
			break
		if page.progress < last:
			monotonic = false
		last = page.progress
		await process_frame
	check(not is_instance_valid(page), "the page goes away once the match is up")
	check(monotonic and last >= 95, "the bar only moves forward, up to %d%% before the match shows" % last)
	var m = current_scene
	check(m != null and m.has_method("get_tech_tree"), "the match is the current scene")
	check(Input.get_current_cursor_shape() == Input.CURSOR_ARROW, "the wait cursor is reset")
	var stages: Array = Loading.last_stages.map(func(s): return s[1])
	check(stages.has("Loading animations") and stages.has("Generating map") and stages.has("Building terrain"), "the match reports its stages: " + str(stages))

	print("2. the stages the match reports")
	ms.map_id = "acropolis_bay"
	ms.go("res://scenes/main.tscn")
	await frames(2)
	page = current_scene
	check(page.get_script() == Loading and page.title.begins_with("Loading “Acropolis Bay"), "scenario map title: " + str(page.title))
	while is_instance_valid(page):
		await process_frame
	check(Loading.last_stages.map(func(s): return s[1]).has("Loading map"), "a scenario reports Loading map")
	check(m != current_scene and current_scene.has_method("get_tech_tree"), "the second match replaced the first")
	check(Loading.map_display_name("random_mainland").begins_with("Mainland"), "map names come from Game Setup's list")

	print("3. a saved game names its map")
	var sm = current_scene
	check(sm.save_game("user://saves/e2e_loading.rts0sav", "loading test"), "a save is written")
	ms.load_save_path = "user://saves/e2e_loading.rts0sav"
	ms.go("res://scenes/main.tscn")
	await frames(2)
	page = current_scene
	check(page.get_script() == Loading and page.title.contains("Acropolis Bay"), "save title: " + str(page.title))
	while is_instance_valid(page):
		await process_frame
	check(Loading.last_stages.map(func(s): return s[1]).has("Loading saved game"), "a save reports Loading saved game")
	# The saved world's models load behind the page, not on the first frame of play.
	check(Loading.last_stages.map(func(s): return s[1]).has("Loading models"), "a save reports Loading models")
	ms.load_save_path = ""

	print("4. manual")
	var Manual = load("res://scenes/reference/manual.gd")
	var text: String = Manual.manual_bbcode()
	check(text.begins_with("[font_size=18][b]0") and text.contains("[font_size=16][b]Graphics settings[/b][/font_size]"), "the manual's headings become BBCode")
	check(not text.contains("hotkey.camera.zoom.in") and not text.contains("[font="), "hotkey names are replaced by their keys")
	var hk := RtsoHotkeyConfig.new()
	var zoom: PackedStringArray = hk.get_hotkey_map().get("camera.zoom.in", PackedStringArray())
	check(zoom.is_empty() or text.contains(zoom[0]), "the zoom key shows as " + str(zoom))
	var mp = Manual.open(current_scene)
	await frames(2)
	check(mp.text.get_parsed_text().contains("Basic controls"), "the manual page shows the text")
	mp.close_page()
	await frames(2)
	check(current_scene.find_child("Manual", true, false) == null, "Close removes the manual")
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(0 if failures == 0 else 1)
