extends SceneTree
# The mod selector and mod loading as 0 A.D. 0.28 has them (gui/modmod
# modmod.js, validatemod.js), with mods in a scratch folder:
#   1. every folder's mod.json is a mod, the game itself the built-in 0ad mod;
#      a broken mod.json is incompatible, and so is a mod whose dependencies
#      the enabled mods don't meet (versionSatisfied's rules);
#   2. Available Mods lists the compatible ones; enabling one makes the mods
#      that need it compatible; Save Configuration stores mod.enabledmods in
#      load order;
#   3. disabling a mod takes the mods that need it along; the filter (and
#      Negate) narrows the lists;
#   3b. the Available list sorts by the clicked column; double-click enables
#      and disables, keeping a row selected; missing enabled mods show as
#      "This mod does not exist"; Help is the Pyrogenesis Mod Selector page;
#   4. at startup an enabled mod's .zip and .pck files load over the game's.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_modmod.gd
# Exit code 0 = pass.

const Mods := preload("res://scenes/menu/mods.gd")
const DIR := "user://mods_e2e/"
var failures := 0
var cfg := RtsoUserConfig.new()

func check(cond: bool, what: String) -> void:
	print("  ok   " if cond else "  FAIL ", what)
	if not cond:
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func frames(n: int) -> void:
	for i in n:
		await process_frame

func write_mod(folder: String, data: Dictionary) -> void:
	DirAccess.make_dir_recursive_absolute(DIR + folder)
	var f := FileAccess.open(DIR + folder + "/mod.json", FileAccess.WRITE)
	f.store_string(JSON.stringify(data))

func mod(mod_name: String, version: String, deps: Array) -> Dictionary:
	return {"name": mod_name, "version": version, "label": mod_name.capitalize(), "url": "example.org/" + mod_name,
		"description": "The %s mod." % mod_name, "dependencies": deps}

func shown(list: Tree) -> Array:
	var out := []
	var it := list.get_root().get_first_child() if list.get_root() else null
	while it:
		out.append(str(it.get_metadata(0)))
		it = it.get_next()
	return out

func select(list: Tree, folder: String, page) -> void:
	var it := list.get_root().get_first_child()
	while it:
		if str(it.get_metadata(0)) == folder:
			it.select(0)
			page._selected(list)
			return
		it = it.get_next()

func run() -> void:
	var saved := cfg.get_value(Mods.CONFIG_KEY)
	cfg.set_value(Mods.CONFIG_KEY, cfg.get_default(Mods.CONFIG_KEY))
	write_mod("alpha", mod("alpha", "1.0", ["0ad>=0.28"]))
	write_mod("beta", mod("beta", "2.1.3", ["alpha"]))
	write_mod("broken", mod("broken", "one", []))
	write_mod("old", mod("old", "1", ["0ad<0.27"]))

	print("1. Mods")
	var all := Mods.available(DIR)
	check(all.keys().size() == 5 and all.has("public") and all["public"]["name"] == "0ad", "the 0ad mod and four folders: " + str(all.keys()))
	check(Mods.enabled() == ["public"], "0ad enabled by default: " + str(Mods.enabled()))
	check(not Mods.validate(all["broken"]) and Mods.validate(all["beta"]), "a version of \"one\" is invalid")
	check(Mods.version_satisfied("0.28.0", ">=", "0.28") and Mods.version_satisfied("5.3", "<", "5.3.0") and not Mods.version_satisfied("0.28.0", "<", "0.27"), "versionSatisfied")

	print("2. Enable")
	var page = load("res://scenes/menu/modmod.gd").new()
	root.add_child(page)
	await frames(1)
	page.init_mods(DIR)
	await frames(1)
	check(shown(page.disabled_list) == ["alpha"] and shown(page.enabled_list) == ["public"], "only Alpha can be enabled: " + str(shown(page.disabled_list)))
	page.compat_filter.button_pressed = false
	check(shown(page.disabled_list).size() == 4, "all four without Filter compatible")
	page.compat_filter.button_pressed = true
	select(page.disabled_list, "alpha", page)
	check(page.toggle_button.text == "Enable" and not page.toggle_button.disabled and not page.web_button.disabled, "Enable and Visit Website")
	page.toggle_mod()
	check(shown(page.disabled_list) == ["beta"], "Beta needs Alpha and can be enabled now")
	page.enable_mod("beta")
	check(not page.save_button.disabled, "Save Configuration turns on")
	page.enabled = ["public", "beta", "alpha"]
	page.save_mods()
	check(cfg.get_value(Mods.CONFIG_KEY) == "mod public alpha beta", "saved in load order: " + cfg.get_value(Mods.CONFIG_KEY))

	print("3. Disable and filter")
	page.disable_mod("alpha")
	check(page.enabled == ["public"] and page.disabled.has("beta"), "Beta goes with Alpha: " + str(page.enabled))
	page.compat_filter.button_pressed = false
	page.filter_edit.text = "beta"
	page.display_lists()
	check(shown(page.disabled_list) == ["beta"], "the filter finds Beta: " + str(shown(page.disabled_list)))
	page.negate.button_pressed = true
	check(not shown(page.disabled_list).has("beta") and shown(page.disabled_list).size() == 3, "Negate leaves the others")
	page.queue_free()

	print("3b. Sorting, double-click, selection, missing mods, Help")
	cfg.set_value(Mods.CONFIG_KEY, cfg.get_default(Mods.CONFIG_KEY))
	page = load("res://scenes/menu/modmod.gd").new()
	root.add_child(page)
	await frames(1)
	page.init_mods(DIR)
	page.compat_filter.button_pressed = false
	check(shown(page.disabled_list) == ["alpha", "beta", "broken", "old"], "Available sorted by name: " + str(shown(page.disabled_list)))
	page.disabled_list.column_title_clicked.emit(0, MOUSE_BUTTON_LEFT)
	check(shown(page.disabled_list) == ["old", "broken", "beta", "alpha"], "a second click on Name reverses it: " + str(shown(page.disabled_list)))
	page.disabled_list.column_title_clicked.emit(1, MOUSE_BUTTON_LEFT)
	check(shown(page.disabled_list) == ["old", "alpha", "beta", "broken"], "Version sorts ascending: " + str(shown(page.disabled_list)))
	page.sort_by(0)
	select(page.disabled_list, "alpha", page)
	page.disabled_list.item_activated.emit()
	check(page.enabled.has("alpha"), "double-click enables")
	check(page._selected_folder(page.disabled_list) == "beta", "the next row stays selected: " + page._selected_folder(page.disabled_list))
	select(page.enabled_list, "alpha", page)
	page.enabled_list.item_activated.emit()
	check(not page.enabled.has("alpha"), "double-click disables")
	check(page._selected_folder(page.enabled_list) == "public", "the last row is selected after Disable")
	page.description.text = ""
	check(page.description.get_theme_color("default_color") == Color8(180, 180, 180), "the description is grey")
	var help = page.show_help()
	await frames(1)
	check(help.find_child("Title", true, false).text == "Pyrogenesis Mod Selector"
		and help.find_child("Text", true, false).text.begins_with("0 A.D. is designed to be easily modded."), "Help is the Pyrogenesis Mod Selector page")
	page.queue_free()
	cfg.set_value(Mods.CONFIG_KEY, "mod public gone beta")
	page = load("res://scenes/menu/modmod.gd").new()
	page.has_incompatible = true
	root.add_child(page)
	await frames(1)
	page.init_mods(DIR)
	check(shown(page.enabled_list) == ["public", "gone", "beta"] and page._value("gone", "name") == "This mod does not exist",
		"a missing enabled mod stays, as This mod does not exist: " + str(shown(page.enabled_list)))
	page.disable_mod("public")
	check(page.enabled.is_empty() and page.disabled.has("beta") and not page.disabled.has("gone"),
		"disabling drops the incompatible mods too: " + str(page.enabled))
	page.queue_free()
	cfg.set_value(Mods.CONFIG_KEY, cfg.get_default(Mods.CONFIG_KEY))

	print("4. Loading")
	DirAccess.make_dir_recursive_absolute(DIR + "alpha")
	var zip := ZIPPacker.new()
	zip.open(ProjectSettings.globalize_path(DIR + "alpha/alpha.zip"))
	zip.start_file("rts0_mod_probe.txt")
	zip.write_file("alpha".to_utf8_buffer())
	zip.close_file()
	zip.close()
	cfg.set_value(Mods.CONFIG_KEY, "mod public alpha")
	check(Mods.load_enabled_packs(DIR) == ["alpha"], "Alpha's pack loads")
	check(FileAccess.get_file_as_string("res://rts0_mod_probe.txt") == "alpha", "its file is in the game's files")
	cfg.set_value(Mods.CONFIG_KEY, "mod public beta")
	check(Mods.load_enabled_packs(DIR) == [] and Mods.incompatible == ["beta"], "Beta without Alpha doesn't load, and is incompatible")
	cfg.set_value(Mods.CONFIG_KEY, "mod public alpha gone")
	check(Mods.load_enabled_packs(DIR) == [] and Mods.incompatible == ["gone"],
		"a missing mod loads none of them (CheckAndEnableMods): " + str(Mods.incompatible))
	var host := Control.new()
	root.add_child(host)
	var inc = load("res://scenes/menu/modmod.gd").open_incompatible(host)
	await process_frame
	var box = inc.find_child("MsgBox", true, false)
	check(box != null and box.title == "Incompatible mods" and box.message.begins_with("You tried to start the game with incompatible or missing mods!")
		and box.captions == ["Ok"], "the Mod Selection opens with the Incompatible mods notice")
	check(box.find_child("Text", true, false).horizontal_alignment == HORIZONTAL_ALIGNMENT_LEFT, "its text is left-aligned")
	host.queue_free()

	cfg.set_value(Mods.CONFIG_KEY, saved)
	cfg.save()
	for f in ["alpha/alpha.zip", "alpha/mod.json", "beta/mod.json", "broken/mod.json", "old/mod.json"]:
		DirAccess.remove_absolute(DIR + f)
	for f in ["alpha", "beta", "broken", "old", ""]:
		DirAccess.remove_absolute(DIR + f)
	print("FAILURES: ", failures)
	quit(1 if failures else 0)
