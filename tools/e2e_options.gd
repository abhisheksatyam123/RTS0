extends SceneTree
# End-to-end check of the options page (port of 0 A.D. gui/options):
#   1. changes apply at once (sound busses, fog, shadows, GUI scale), Save
#      writes user://config/user.cfg next to the hotkey bindings without
#      dropping them, Revert and Reset work, and Close asks about unsaved
#      changes;
#   2. a hotkey save or reset keeps the settings;
#   3. Game Setup takes its default AI difficulty and behaviour from the page;
#   4. in a match the pause menu opens the page over the game, and the sun
#      and environment follow the shadow and fog options.
#
#   godot --headless --path . --fixed-fps 60 -s tools/e2e_options.gd
# Exit code 0 = pass.

const CFG_PATH := "user://config/user.cfg"

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

func cfg_text() -> String:
	return FileAccess.get_file_as_string(CFG_PATH) if FileAccess.file_exists(CFG_PATH) else ""

func run() -> void:
	if FileAccess.file_exists(CFG_PATH):
		DirAccess.remove_absolute(CFG_PATH)
	var cfg := RtsoUserConfig.new()
	cfg.reload()
	var hk := RtsoHotkeyConfig.new()
	hk.reload()
	hk.set_hotkey("session.stop", PackedStringArray(["Ctrl+K"]))
	check(hk.save_user(), "a hotkey override is saved")

	print("1. options page")
	change_scene_to_file("res://scenes/menu/options.tscn")
	await frames(3)
	var page = current_scene
	check(page._tabs.get_child_count() == 7, "seven categories, as 0 A.D.'s options.json")
	var master: HSlider = page._controls["sound.mastergain"][1].get_child(0)
	check(is_equal_approx(master.value, 0.9), "master volume starts at 0 A.D.'s 0.9")
	var music_bus := AudioServer.get_bus_index("Music")
	check(music_bus > 0 and AudioServer.get_bus_index("Action") > 0 and AudioServer.get_bus_index("UI") > 0
		and AudioServer.get_bus_index("Ambient") > 0, "music, ambient, action and UI busses exist")
	check(is_equal_approx(AudioServer.get_bus_volume_db(music_bus), 0.0), "default music gain keeps RTS0's mix")
	page._controls["sound.musicgain"][1].get_child(0).value = 0.4
	await frames(2)
	check(absf(AudioServer.get_bus_volume_db(music_bus) - linear_to_db(2.0)) < 0.1, "music 0.4 doubles the music bus at once")
	page._controls["gui.scale"][1].select(2) # 125%
	page._controls["gui.scale"][1].item_selected.emit(2)
	await frames(2)
	check(is_equal_approx(root.content_scale_factor, 1.25), "GUI scale applies at once")
	check(page.has_unsaved_changes(), "the page knows about unsaved changes")
	page._on_close()
	check(page._confirm.visible, "Close with unsaved changes asks first")
	page._confirm.visible = false
	page._on_save()
	var text := cfg_text()
	check(text.contains("sound.musicgain = \"0.4\"") and text.contains("gui.scale = \"1.25\""), "Save writes the changed values")
	check(text.contains("hotkey.session.stop"), "Save keeps the hotkey bindings")
	check(not text.contains("sound.mastergain"), "values left at their defaults are not written")
	page._controls["fog"][1].button_pressed = false
	await frames(2)
	check(cfg.get_value("fog") == "false", "unchecking fog changes the setting")
	page._on_revert()
	check(cfg.get_value("fog") == "true" and page._controls["fog"][1].button_pressed, "Revert goes back to the saved value")
	page._on_reset()
	await frames(2)
	check(cfg.get_value("sound.musicgain") == "0.2" and is_equal_approx(root.content_scale_factor, 1.0),
		"Reset goes back to 0 A.D.'s defaults")
	check(is_equal_approx(page._controls["sound.musicgain"][1].get_child(0).value, 0.2), "Reset updates the sliders")
	page._on_revert()
	check(cfg.get_value("sound.musicgain") == "0.4", "Revert after Reset restores the saved values")

	print("2. hotkey saves keep the settings")
	hk.set_hotkey("session.stop", PackedStringArray(["Ctrl+J"]))
	hk.save_user()
	text = cfg_text()
	check(text.contains("sound.musicgain = \"0.4\"") and text.contains("Ctrl+J"), "a hotkey save keeps the options")
	hk.reset_user()
	text = cfg_text()
	check(text.contains("sound.musicgain = \"0.4\"") and not text.contains("hotkey.session.stop"), "hotkey Reset keeps the options")
	cfg.reload()
	check(cfg.get_value("sound.musicgain") == "0.4", "the saved value survives a reload")

	print("3. Game Setup defaults")
	cfg.set_value("gui.gamesetup.aidifficulty", "5")
	cfg.set_value("gui.gamesetup.aibehavior", "aggressive")
	change_scene_to_file("res://scenes/menu/gamesetup.tscn")
	await frames(3)
	var gs = current_scene
	gs._slot_rows.clear()
	gs._ensure_slot_rows(2)
	check(int(gs._slot_rows[1]["diff"]) == 5 and int(gs._slot_rows[1]["beh"]) == 3, "new AI slots are Very Hard and Aggressive")
	cfg.set_value("gui.gamesetup.aidifficulty", "3")
	cfg.set_value("gui.gamesetup.aibehavior", "random")

	print("4. in a match")
	var ms = root.get_node("MatchSettings")
	ms.map_id = "random_continental"
	ms.seed = 777
	ms.map_size = 128
	ms.slot_types = [0, 1]
	ms.slot_civs = [0, 1]
	ms.slot_teams = [1, 2]
	ms.victory = 3
	ms.replay_dir = ""
	ms.load_save_path = ""
	change_scene_to_file("res://scenes/main.tscn")
	await frames(30)
	var m = current_scene
	var sun: DirectionalLight3D = m.get_node("Sun")
	var env: Environment = m.find_children("*", "WorldEnvironment", true, false)[0].environment
	check(sun.shadow_enabled and env.fog_enabled, "shadows and fog are on by default")
	cfg.set_value("shadows", "false")
	cfg.set_value("fog", "false")
	await frames(3)
	check(not sun.shadow_enabled and not env.fog_enabled, "turning shadows and fog off reaches the scene")
	cfg.set_value("shadows", "true")
	cfg.set_value("fog", "true")
	await frames(3)
	check(sun.shadow_enabled and env.fog_enabled, "and back on restores them")
	m.get_node("PauseMenu")._on_options_pressed()
	await frames(3)
	var overlay = m.find_child("Options", true, false)
	check(overlay != null and overlay.overlay and current_scene == m, "the pause menu opens the options over the game")
	if overlay:
		overlay._on_close()
		await frames(3)
	check(m.find_child("Options", true, false) == null, "Close returns to the game")
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(0 if failures == 0 else 1)
