extends SceneTree
# hotkey="cancel" (Escape) outside a match, and the Hotkeys page's Reset question:
#   1. the main menu's open submenu closes (menupanel.xml closeMenuButton);
#   2. Options opened from the main menu closes, asking first when there are
#      unsaved changes (options.xml closeButton, shouldClosePage);
#   3. Hotkeys: Reset asks "Reset all hotkeys to default values?" before
#      clearing anything (resetUserHotkeys); Escape cancels the hotkey picker
#      and then closes the page; 0.28's button tooltips and the category
#      dropdown's per-category hover tooltips.
#
#   godot --headless --path . --fixed-fps 20 -s tools/e2e_menu_escape.gd
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

func escape() -> void:
	for down in [true, false]:
		var k := InputEventKey.new()
		k.keycode = KEY_ESCAPE
		k.physical_keycode = KEY_ESCAPE
		k.pressed = down
		root.push_input(k, true)
		await frames(1)
	await frames(2)

func msgbox() -> Node:
	for c in root.find_children("*", "", true, false):
		if c.has_signal("answered") and c.is_visible_in_tree():
			return c
	return null

func run() -> void:
	print("1. Main menu submenu")
	change_scene_to_file("res://scenes/menu/main_menu.tscn")
	await frames(3)
	var menu = current_scene
	menu._toggle_submenu("settings", menu.get_node("MainPanel/Buttons/BtnSettings"))
	await frames(2)
	check(menu.submenu_panel.visible, "Settings opens its submenu")
	await escape()
	check(not menu.submenu_panel.visible, "Escape closes it")

	print("2. Options from the main menu")
	change_scene_to_file("res://scenes/menu/options.tscn")
	await frames(5)
	var opts = current_scene
	var key: String = opts._saved.keys()[0]
	var old: String = opts.cfg.get_value(key)
	opts.cfg.set_value(key, "true" if old != "true" else "false")
	await escape()
	var box = msgbox()
	check(box != null and current_scene == opts, "with unsaved changes Escape asks first")
	await escape() # the question's No
	check(msgbox() == null and current_scene == opts, "and Escape on the question keeps the page")
	opts.cfg.set_value(key, old)
	await escape()
	await frames(5)
	check(current_scene != opts and current_scene.scene_file_path.ends_with("main_menu.tscn"), "without changes Escape goes back to the main menu")

	print("2b. Options over the main menu")
	menu = current_scene
	menu._execute_action("open_options")
	await frames(5)
	var over: Control = null
	for c in menu.find_children("*", "Control", true, false):
		if c.has_method("has_unsaved_changes"):
			over = c
	check(over != null and current_scene == menu, "Settings > Options opens over the main menu, as 0.28's child page")
	if over:
		var dlg: Control = over.find_child("Dialog", true, false)
		check(is_equal_approx(dlg.size.x, 800.0), "an 800 px dialog (%s)" % dlg.size)
	await escape()
	await frames(3)
	check(current_scene == menu and (over == null or not is_instance_valid(over) or over.is_queued_for_deletion()), "Escape closes it back to the menu")

	print("3. Hotkeys page")
	change_scene_to_file("res://scenes/menu/hotkeys.tscn")
	await frames(5)
	var hk = current_scene
	var tips := {}
	for b in hk.find_children("*", "Button", true, false):
		if b.text in ["Reset", "Save", "Close", "Accept", "Cancel"]:
			tips[b.text] = b.tooltip_text
	print("    ", tips)
	check(tips.get("Close", "") == "Unsaved changes will be lost" and tips.get("Save", "") == "Saves changes"
			and tips.get("Accept", "") == "Change the hotkeys and close", "0.28's button tooltips")
	var fs = hk.filter_select
	print("    ", fs.get_item_text(1), ": ", fs.get_item_tooltip(1))
	check(fs.get_item_tooltip(0) == "All available hotkeys." and fs.get_item_tooltip(1) != "", "the category list's hover tooltips (onFilterHover)")
	var name: String = hk.hotkeys.keys()[0]
	hk.cfg.set_hotkey(name, PackedStringArray(["Ctrl+Shift+F11"]))
	hk._on_reset_all()
	await frames(2)
	box = msgbox()
	check(box != null and box._text.text.begins_with("Reset all hotkeys to default values?"), "Reset asks first")
	check(Array(hk.cfg.get_hotkey_map().get(name, [])).has("Ctrl+Shift+F11"), "and changes nothing yet")
	box._answer(0)
	await frames(2)
	check(Array(hk.cfg.get_hotkey_map().get(name, [])).has("Ctrl+Shift+F11"), "No keeps the binding")
	hk.cfg.reload()
	# hotkeys.xml: a 700×688 dialog with Category: / Filter: and Name / Mapping columns;
	# mappings as 0.28 names the keys; a row with no description says so.
	var labels: Array = hk.find_children("*", "Label", true, false).map(func(l): return l.text)
	check(labels.has("Category:") and labels.has("Filter:") and hk.tree.get_column_title(0) == "Name"
		and hk.tree.get_column_title(1) == "Mapping", "Category:, Filter:, Name and Mapping")
	check(hk.dialog.size.x <= 700.0 and hk.dialog.size.y <= 688.0, "a 700×688 dialog (%s)" % hk.dialog.size)
	var rows := {}
	for it in hk.tree.get_root().get_children():
		rows[it.get_metadata(0)] = it
	check(rows.has("item.next") and rows["item.next"].get_text(1) == "→", "arrow keys show as 0.28's glyphs: " + (rows["item.next"].get_text(1) if rows.has("item.next") else ""))
	check(rows.has("pause") and rows["pause"].get_text(1) == "Pause, Shift+Space", "every mapping, shortest first")
	var undescribed := rows.keys().filter(func(h): return not hk.meta_hotkeys.has(h))
	check(undescribed.is_empty() or rows[undescribed[0]].get_tooltip_text(0) == "No tooltip available.", "No tooltip available.")
	hk.open_picker("pause")
	await frames(2)
	check(hk.picker_hint.text == "Click on any mapping to modify it.\n You may have up to 4 different hotkeys."
		and hk.slot_labels[0].tooltip_text == "Click to set the hotkey" and hk.slot_delete[0].tooltip_text == "Click to delete the hotkey"
		and hk.slot_labels[2].text == "(unused)", "the picker's instructions and slot tooltips")
	hk._close_picker(false)
	# HotkeyPicker Reset removes the user value and saves at once; other pending edits stay unsaved.
	var cfg_path := "user://config/user.cfg"
	var saved := FileAccess.get_file_as_string(cfg_path) if FileAccess.file_exists(cfg_path) else ""
	hk.cfg.set_hotkey("camera.zoom.in", PackedStringArray(["Ctrl+Shift+F10"]))
	hk.cfg.save_user()
	hk.cfg.reload()
	hk._setup_data()
	hk.open_picker("summary")
	hk.picker_combos[0] = "Ctrl+Shift+F9"
	hk._close_picker(true)
	hk.open_picker("camera.zoom.in")
	hk._on_picker_reset()
	var after := FileAccess.get_file_as_string(cfg_path)
	check(not after.contains("Ctrl+Shift+F10") and not after.contains("Ctrl+Shift+F9")
		and Array(hk.cfg.get_hotkey_map()["summary"]).has("Ctrl+Shift+F9"), "Reset saves that hotkey's default straight away, and only it")
	hk._close_picker(false)
	hk.cfg.reload()
	var f := FileAccess.open(cfg_path, FileAccess.WRITE)
	f.store_string(saved)
	f.close()
	hk.cfg.reload()
	hk._setup_data()
	hk.dirty = false
	hk.pending.clear()
	hk.open_picker(name)
	await frames(2)
	check(hk.picker.visible, "the picker opens")
	await escape()
	check(not hk.picker.visible and current_scene == hk, "Escape cancels the picker")
	await escape()
	await frames(5)
	check(current_scene != hk, "and then closes the page")

	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures else 0)
