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
	# options.json repeats Fog in Graphics (advanced): the second row follows.
	check(page._controls.has("fog#") and not page._controls["fog#"][1].button_pressed and page._controls.has("postproc#") and page._controls.has("shadows#"),
		"the advanced tab's Fog row shows the change; Post-processing and Shadows repeat there too")
	check(not page._controls.has("locale"), "no Language option (0.28 keeps it in the main menu)")
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
	check(page._labels["waterfancyeffects"].get_theme_color("font_color") == Color8(140, 140, 140), "their labels grey out too (textcolor_disabled)")
	# options.js control tooltips: the label carries the description, the control adds the
	# value hints, and a list item its own tooltip.
	check(page.find_child("Title", true, false).text == "Game Options" and page.find_child("Title", true, false).offset_top < -16,
		"the title sits on the dialog's top edge")
	check(page._labels["sound.mastergain"].tooltip_text != "" and not page._labels["sound.mastergain"].tooltip_text.contains("Value:"),
		"a label's tooltip is the option's description")
	check(master.tooltip_text.ends_with("\nValue: 0.90 (min: 0.00, max: 2.00)"),
		"a slider's tooltip gives its value, min and max: " + master.tooltip_text.get_slice("\n", 1))
	check(page._controls["sound.mastergain"][1].get_child_count() == 1, "the slider has no readout beside it")
	master.value = 0.5
	check(master.tooltip_text.ends_with("\nValue: 0.50 (min: 0.00, max: 2.00)"), "and follows the value")
	master.value = 0.9
	var drag: LineEdit = page._controls["gui.session.dragdelta"][1]
	check(drag.tooltip_text.ends_with("\nMin: 1, Max: 200"), "a number's gives its range")
	check(ally_tip(page).contains("\nDefault: "), "a colour's gives its default")
	var aa0 = page._controls["antialiasing"][1]
	check(not aa0.tooltip_text.contains("\n") and aa0.get_item_tooltip(0) != "", "a list's tooltip is its description; each item has one")
	check(page.find_child("Status", true, false) == null and not "_description" in page, "no description strip under the options")
	# sanitizeValue: a number outside its range reddens its box and makes Save ask.
	drag.text_changed.emit("500")
	check((drag.get_theme_stylebox("normal") as StyleBoxFlat).bg_color == Color8(127, 0, 0, 100), "an out-of-range number turns its box red")
	page._tabs.current_tab = 0
	page._on_save()
	check(page._confirm != null and page._confirm.message.contains("Some setting values are invalid")
		and page._tabs.get_current_tab_control() == drag.get_parent().get_parent(), "Save asks about it on its own tab")
	page._confirm.find_child("Button1", true, false).pressed.emit() # No
	await frames(2)
	drag.text_changed.emit(cfg.get_default("gui.session.dragdelta"))
	check((drag.get_theme_stylebox("normal") as StyleBoxFlat).bg_color == Color8(12, 12, 12, 100), "a valid one is dark again")
	# options.js rows: 26 px high, 2 apart; the tick box at the right end.
	page._tabs.current_tab = 3
	await frames(2)
	var r0: Control = page._labels["sound.mastergain"]
	var r1: Control = page._labels["sound.musicgain"]
	# 26 px rows; a headless run's taller fallback font can raise a label above that.
	var pitch := maxf(26.0, r0.get_combined_minimum_size().y) + 2.0
	check(absf(r1.global_position.y - r0.global_position.y - pitch) < 1.5, "rows are 26 px high, 2 apart (%.1f)" % (r1.global_position.y - r0.global_position.y))
	page._tabs.current_tab = 2
	await frames(2)
	var tick: Control = page._controls["watereffects"][1]
	var cell_right: float = tick.get_parent().global_position.x + tick.get_parent().size.x
	check(tick.global_position.x + tick.size.x > cell_right - 2.0, "the tick box sits at the right end of its row")
	check((page._tab_buttons.get_child(0) as Control).size.y >= 34 and page._tab_buttons.get_theme_constant("separation") == 5,
		"tab buttons are 34 px high, 5 apart")
	var ally_edit: LineEdit = page._controls["gui.session.diplomacycolors.ally"][1].get_child(0)
	ally_edit.text_changed.emit("300 0 0")
	check(page._controls["gui.session.diplomacycolors.ally"][1].get_child(1).color == Color(1, 0, 1), "an invalid colour turns the swatch magenta")
	# saveChanges: an invalid value shows its tab and asks before saving.
	page._tabs.current_tab = 0
	page._on_save()
	check(page._confirm != null and page._confirm.message.contains("Some setting values are invalid"), "Save asks about an invalid colour")
	check(page._tabs.get_current_tab_control().name == "Display", "on the Display tab")
	page._confirm.find_child("Button1", true, false).pressed.emit() # No
	await frames(2)
	check(not cfg_text().contains("300 0 0"), "No leaves the invalid colour unsaved")
	# page_colormixer: the button opens the mixer, Cancel sanitizes, Save writes the mix.
	var mix: Button = page._controls["gui.session.diplomacycolors.ally"][1].get_child(2)
	ally_edit.text = "abc 5.7"
	mix.pressed.emit()
	await frames(2)
	var mixer = page._confirm
	check(mixer != null and mixer.name == "ColorMixer" and mixer.find_child("Title", true, false).text == "Color", "the colour button opens the mixer")
	mixer.find_child("Button1", true, false).pressed.emit() # Cancel
	await frames(2)
	check(page._confirm == null and ally_edit.text == "0 5 0" and cfg.get_value("gui.session.diplomacycolors.ally") == "0 5 0",
		"Cancel writes back the sanitized colour: " + ally_edit.text)
	mix.pressed.emit()
	await frames(2)
	mixer = page._confirm
	mixer.sliders[0].value = 200
	check(mixer.find_child("ColorValue0", true, false).text == "200" and mixer.find_child("ColorDisplay", true, false).color == Color8(200, 5, 0),
		"a slider updates its value and the swatch")
	mixer.find_child("Button2", true, false).pressed.emit() # Save
	await frames(2)
	check(cfg.get_value("gui.session.diplomacycolors.ally") == "200 5 0"
		and page._controls["gui.session.diplomacycolors.ally"][1].get_child(1).color == Color8(200, 5, 0), "Save writes the mixed colour")
	ally_edit.text = cfg.get_default("gui.session.diplomacycolors.ally")
	ally_edit.text_changed.emit(ally_edit.text)
	# options.json: the Display tab's order, open-ended chat backlog, list item tooltips.
	var display: GridContainer = page._tabs.get_node("Display").get_child(0)
	check(display.get_child(1).name == "showdetailedtooltips" and display.get_child(3).name == "gui_session_howtoshownames",
		"Display starts with detailed tooltips, then entity names, as 0.28")
	var backlog: LineEdit = page._controls["lobby.history"][1]
	backlog.text_changed.emit("5000")
	check(cfg.get_value("lobby.history") == "5000" and not page._invalid("lobby.history"), "the chat backlog has no maximum")
	backlog.text_changed.emit("0")
	check(backlog.tooltip_text.ends_with("\nMin: 0"), "and its tooltip gives only the minimum")
	var aa = page._controls["antialiasing"][1]
	check(aa.get_item_tooltip(1).begins_with("Fast, but simple"), "list items carry their own tooltips")
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
	# Phones (or -mobiledefaults) start cheaper; desktop keeps 0 A.D.'s defaults.
	var mc := RtsoUserConfig.new()
	check(mc.get_default("shadows") == "true" and mc.get_default("adaptivefps.session") == "60" and mc.get_default("renderer.scale") == "1.0", "desktop defaults are 0 A.D.'s")
	mc.set_command_args(PackedStringArray(["-mobiledefaults"]))
	check(mc.get_default("shadows") == "false" and mc.get_default("postproc") == "false" and mc.get_default("particles") == "false"
		and mc.get_default("renderer.scale") == "0.75" and mc.get_default("renderer.upscale.technique") == "bilinear"
		and mc.get_default("adaptivefps.session") == "30" and mc.get_default("adaptivefps.menu") == "30", "phone defaults: no shadows, post or particles, 0.75 scale, 30 fps")
	check(mc.get_default("silhouettes") == "true" and mc.get_default("fog") == "true", "phones keep silhouettes and fog")
	mc.set_command_args(OS.get_cmdline_args() + OS.get_cmdline_user_args())
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(0 if failures == 0 else 1)

func ally_tip(page) -> String:
	return page._controls["gui.session.diplomacycolors.ally"][1].get_child(0).tooltip_text
