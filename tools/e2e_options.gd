extends SceneTree
# End-to-end check of the options page (port of 0 A.D. gui/options):
#   1. changes apply at once (sound busses, fog, shadows, GUI scale), Save
#      writes user://config/user.cfg next to the hotkey bindings without
#      dropping them, Revert and Reset work, and Close asks about unsaved
#      changes; GUI scale asks to keep the change for 5 seconds;
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
	check(page._tabs.get_child_count() == 9, "nine categories, as 0 A.D.'s options.json")
	var btn_save: Button = page.find_child("BtnSave", true, false)
	var btn_revert: Button = page.find_child("BtnRevert", true, false)
	check(btn_save.disabled and btn_revert.disabled, "Save and Revert are off with nothing changed")
	check(page._labels["shadowquality"].text == "Quality" and page._labels["shadowquality"].get_parent() is MarginContainer,
		"shadow quality reads \"Quality\", indented under Shadows")
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
	# options.json "timeout": 5000; timedConfirmation, No (or the countdown) reverts.
	check(page._confirm != null and page._confirm.name == "TimedConfirmation"
		and page._confirm._text.text == "Changes will be reverted in 5 seconds. Do you want to keep changes?",
		"GUI scale asks to keep the change, counting down from 5 seconds")
	page._confirm.find_child("Button1", true, false).pressed.emit() # No
	await frames(2)
	check(page._confirm == null and is_equal_approx(root.content_scale_factor, 1.0)
		and page._controls["gui.scale"][1].selected == 1, "No goes back to 100%")
	page._controls["gui.scale"][1].select(2)
	page._controls["gui.scale"][1].item_selected.emit(2)
	await frames(2)
	page._confirm.find_child("Button2", true, false).pressed.emit() # Yes
	await frames(2)
	check(page._confirm == null and is_equal_approx(root.content_scale_factor, 1.25), "Yes keeps 125%")
	check(page.has_unsaved_changes(), "the page knows about unsaved changes")
	check(not btn_save.disabled and not btn_revert.disabled, "a change turns Save and Revert on")
	page._on_close()
	check(page._confirm != null and page._confirm.message.contains("unsaved changes"), "Close with unsaved changes asks first")
	page._confirm.find_child("Button1", true, false).pressed.emit() # No
	check(page._confirm == null and page.is_inside_tree(), "No keeps the page open")
	page._on_save()
	check(btn_save.disabled and btn_revert.disabled, "saving turns them off again")
	var text := cfg_text()
	check(text.contains("sound.musicgain = \"0.4\"") and text.contains("gui.scale = \"1.25\""), "Save writes the changed values")
	check(text.contains("hotkey.session.stop"), "Save keeps the hotkey bindings")
	check(not text.contains("sound.mastergain"), "values left at their defaults are not written")
	page._controls["fog"][1].button_pressed = false
	await frames(2)
	check(cfg.get_value("fog") == "false", "unchecking fog changes the setting")
	page._on_revert()
	check(cfg.get_value("fog") == "true" and page._controls["fog"][1].button_pressed, "Revert goes back to the saved value")
	cfg.set_value("lobby.login", "alice")
	page._on_reset()
	check(page._confirm != null and page._confirm.message.contains("erase your saved settings"), "Reset asks first")
	page._confirm.find_child("Button2", true, false).pressed.emit() # Yes
	await frames(2)
	check(cfg.get_value("sound.musicgain") == "0.2" and is_equal_approx(root.content_scale_factor, 1.0),
		"Reset goes back to 0 A.D.'s defaults")
	check(is_equal_approx(page._controls["sound.musicgain"][1].get_child(0).value, 0.2), "Reset updates the sliders")
	check(not cfg_text().contains("sound.musicgain") and not page.has_unsaved_changes(), "and erases the saved settings")
	check(cfg.get_value("lobby.login") == "alice", "Reset keeps settings the page doesn't list (setDefaults: options.json keys only)")
	cfg.set_value("lobby.login", cfg.get_default("lobby.login"))
	var batch: HSlider = page._controls["gui.session.scrollbatchratio"][1].get_child(0)
	batch.value = 1.0
	check(is_equal_approx(batch.value, 1.0) and is_equal_approx(batch.max_value, 30.0) and batch.step <= 0.1,
		"the scroll batch slider reaches its default of 1 (step %s)" % batch.step)
	page._controls["sound.musicgain"][1].get_child(0).value = 0.4
	page._on_save()
	# options.json dependencies: the water sub-options follow Water effects, the cutoff
	# distance Cover whole map, the sharpness factor Sharpening.
	var fancy: CheckBox = page._controls["waterfancyeffects"][1]
	check(not fancy.disabled, "water sub-options are on with water effects")
	page._controls["watereffects"][1].button_pressed = false
	await frames(2)
	check(fancy.disabled and page._controls["waterrealdepth"][1].disabled, "and greyed out without them")
	check(page._labels["waterfancyeffects"].modulate.a < 0.6, "their labels grey out too")
	# options.js control tooltips: the value hints and a list item's own tooltip.
	page._controls["sound.mastergain"][1].mouse_entered.emit()
	check(page._description.text.ends_with("\nValue: 0.90 (min: 0.00, max: 2.00)"),
		"a slider's description gives its value, min and max: " + page._description.text.get_slice("\n", 1))
	page._controls["gui.session.dragdelta"][1].mouse_entered.emit()
	check(page._description.text.ends_with("\nMin: 1, Max: 200"), "a number's gives its range")
	page._controls["gui.session.diplomacycolors.ally"][1].mouse_entered.emit()
	check(page._description.text.contains("\nDefault: "), "a colour's gives its default")
	page._controls["antialiasing"][1].mouse_entered.emit()
	check(page._description.text.contains("\n") and page._description.text.contains("antialiasing"), "a list gives the chosen item's tooltip")
	var ally_edit: LineEdit = page._controls["gui.session.diplomacycolors.ally"][1].get_child(0)
	ally_edit.text_changed.emit("300 0 0")
	check(page._controls["gui.session.diplomacycolors.ally"][1].get_child(1).color == Color(1, 0, 1), "an invalid colour turns the swatch magenta")
	ally_edit.text_changed.emit(cfg.get_value("gui.session.diplomacycolors.ally"))
	page._controls["watereffects"][1].button_pressed = true
	page._controls["waterrefraction"][1].button_pressed = false
	await frames(2)
	check(not fancy.disabled and page._controls["waterrealdepth"][1].disabled, "real depth needs refraction")
	page._controls["waterrefraction"][1].button_pressed = true
	check(not page._controls["sharpness"][1].get_child(0).editable, "no sharpness factor while sharpening is off")
	page._controls["shadowscovermap"][1].button_pressed = true
	await frames(2)
	check(not page._controls["shadowscutoffdistance"][1].get_child(0).editable, "covering the map ignores the cutoff distance")
	page._controls["shadowscovermap"][1].button_pressed = false
	page._controls["textures.quality"][1].select(0)
	page._controls["textures.quality"][1].item_selected.emit(0)
	await frames(2)
	check(absf(root.texture_mipmap_bias - 2.0) < 0.01, "Low texture quality drops two mip levels")
	page._controls["textures.quality"][1].select(2)
	page._controls["textures.quality"][1].item_selected.emit(2)
	await frames(2)
	check(absf(root.texture_mipmap_bias) < 0.01, "High uses full textures")
	var aniso_key := "rendering/textures/default_filters/anisotropic_filtering_level"
	check(int(ProjectSettings.get_setting(aniso_key)) == 1, "the anisotropic filter defaults to 0.28's 2x")
	var aniso = page._controls["textures.maxanisotropy"][1]
	aniso.select(4)
	aniso.item_selected.emit(4)
	await frames(2)
	check(int(ProjectSettings.get_setting(aniso_key)) == 4 and RtsoUserConfig.new().get_value("textures.maxanisotropy") == "16",
		"16x reaches the renderer")
	aniso.select(1)
	aniso.item_selected.emit(1)
	await frames(2)
	page._controls["waterreflection"][1].button_pressed = false
	await frames(2)
	var wg: Dictionary = root.get_node("UserSettings").water_globals
	check(wg.get("rtso_water_reflection") == 0.0 and wg.get("rtso_water_effects") == 1.0, "water reflections off reach the water shader")
	check(wg.get("rtso_water_fancy") == 0.0, "high-quality water effects are off by default, as in 0.28")
	page._controls["waterreflection"][1].button_pressed = true

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
	var cutoff := sun.directional_shadow_max_distance
	cfg.set_value("shadowscovermap", "true")
	await frames(3)
	check(sun.directional_shadow_max_distance > cutoff, "Cover whole map lengthens the shadow distance")
	cfg.set_value("shadowscovermap", "false")
	await frames(3)
	check(is_equal_approx(sun.directional_shadow_max_distance, cutoff), "and turning it off restores the cutoff")
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
