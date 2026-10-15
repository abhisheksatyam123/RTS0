extends SceneTree
# End-to-end check of Learn to Play's Tutorial (0 A.D. gui/pregame/MainMenuItems.js):
#   1. Learn to Play lists Tutorial after Manual;
#   2. it sets the match up as the Introductory Tutorial scenario with its own
#      players and cheats on, as page_autostart does;
#   3. the match loads and shows the tutorial panel;
#   4. game setup no longer lists the tutorials (0 A.D.'s Scenario type is maps/scenarios).
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_tutorial.gd
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

func learn_labels(menu) -> Array:
	menu._toggle_submenu("learn", menu.get_node("MainPanel/Buttons/BtnLearn"))
	return menu.submenu_buttons_box.get_children().filter(func(b): return not b.is_queued_for_deletion()).map(func(b): return b.text)

# The tutorial panel: a visible Panel holding a RichTextLabel with the goal text.
func tutorial_text(n: Node) -> String:
	if n is RichTextLabel and n.is_visible_in_tree() and n.get_parent() is Panel:
		var t: String = n.get_parsed_text()
		if t.length() > 20:
			return t
	for c in n.get_children():
		var t := tutorial_text(c)
		if t != "":
			return t
	return ""

func run() -> void:
	var ms = root.get_node("MatchSettings")

	print("1. Learn to Play")
	change_scene_to_file("res://scenes/menu/main_menu.tscn")
	await frames(4)
	var labels := learn_labels(current_scene)
	var at := labels.find("Tutorial")
	check(at >= 0 and at == labels.find("Manual") + 1, "Tutorial follows Manual: " + str(labels))

	print("2. Tutorial")
	ms.campaign_run = "stale"
	current_scene._execute_action("start_tutorial")
	check(ms.map_id == "introductory_tutorial", "the Introductory Tutorial is set up: " + str(ms.map_id))
	check(ms.cheats and ms.slot_types == [0, 1] and ms.player_count == 2,
		"local player in seat 1, the scenario's other player, cheats on: %s %s" % [ms.slot_types, ms.cheats])
	check(ms.campaign_run == "" and ms.campaign_level == "", "not part of a campaign run")

	print("3. The match")
	var text := ""
	var t0 := Time.get_ticks_msec()
	while Time.get_ticks_msec() - t0 < 120000:
		await process_frame
		if current_scene and current_scene.has_method("get_match_summary"):
			text = tutorial_text(current_scene)
			if text != "":
				break
	check(current_scene != null and current_scene.has_method("get_match_summary"), "the match loads")
	check(text != "", "the tutorial panel shows its first goal: " + text.substr(0, 80))
	# messages.js updateTutorial: the newest instruction in 255 226 149, a status line, and
	# Ready while the goal waits on it.
	var tp: Dictionary = current_scene.debug_tutorial_panel()
	print("    ", tp)
	check(str(tp["text"]).contains("[color=#ffe295]"), "the newest instruction is coloured")
	# The first goal has nothing to do: Ready from the first frame the panel shows it.
	check(tp["status"] == "Click when ready.", "the status line: " + str(tp["status"]))
	check(tp["button_visible"] and tp["button"] == "Ready", "the button reads Ready")
	# messages.js translate(): the instructions, status and button follow the language.
	var de: Translation = load("res://assets/l10n/de.po")
	de.locale = "de"
	TranslationServer.add_translation(de)
	var locale_before := TranslationServer.get_locale()
	TranslationServer.set_locale("de")
	await frames(3)
	tp = current_scene.debug_tutorial_panel()
	check(str(tp["text"]).contains("Willkommen") and tp["status"] == "Klicke, wenn du bereit bist."
		and tp["button"] == "Bereit", "the panel is translated: " + str(tp))
	TranslationServer.set_locale(locale_before)
	TranslationServer.remove_translation(de)
	await frames(3)

	print("4. Game setup")
	change_scene_to_file("res://scenes/menu/gamesetup.tscn")
	await frames(6)
	var ids: Array = current_scene._maps.map(func(m): return m["id"])
	check(not ids.has("introductory_tutorial") and not ids.has("starting_economy_walkthrough"), "the tutorials are not listed")
	check(ids.has("acropolis_bay"), "scenarios still are")

	print("RESULT: %s (%d failed)" % ["PASS" if failures == 0 else "FAIL", failures])
	quit(1 if failures else 0)
