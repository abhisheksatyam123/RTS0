extends SceneTree
# End-to-end check of the save and load dialogs (0 A.D. gui/loadgame):
#   1. the load list has 0 A.D.'s columns, newest first, sorts by any column and
#      hides saves of another save format until the filter is unticked;
#   2. the details show the map with its preview, players, played time, type,
#      size, victory and save format;
#   3. an older save asks before loading, a newer one can't be loaded;
#   4. Shift+Delete deletes without asking, Delete asks first; a save made with
#      other mods lists as incompatible, names its mods in orange and asks first;
#   5. in a match, Save writes a new save with its description, and saving over
#      the selected one asks first and keeps its description, while Enter in the description always writes a new one;
#   6. Load from the load dialog loads the save into a new match.
#
#   godot --headless --path . --fixed-fps 60 -s tools/e2e_savegames.gd
# Exit code 0 = pass.

var failures := 0
const NAMES := ["zz_e2e_save_a", "zz_e2e_save_b", "zz_e2e_save_old", "zz_e2e_save_new", "zz_e2e_save_match", "zz_e2e_save_mods"]
var Dialog: GDScript

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

func player(id: int, name: String, team: int, ai: bool) -> Dictionary:
	return {"id": id, "name": name, "civ": id, "civ_name": ["Athenians", "Spartans"][id], "team": team, "ai": ai,
		"state": "active", "color": {"r": 0.2, "g": 0.3, "b": 0.8}}

func write_save(name: String, version: int, timestamp: int, map_id: String, map_name: String, desc: String) -> void:
	DirAccess.make_dir_recursive_absolute("user://saves")
	FileAccess.open("user://saves/%s.rts0sav" % name, FileAccess.WRITE).store_string("x")
	FileAccess.open("user://saves/%s.json" % name, FileAccess.WRITE).store_string(JSON.stringify({
		"save_version": version, "timestamp": timestamp, "description": desc, "map_id": map_id, "map_name": map_name,
		"game_time": 754.0, "settings": {"map_id": map_id, "map_size": 256, "victory": 1},
		"players": [player(0, "e2eAlpha", 1, false), player(1, "Bot One", 2, true)]}))

func cleanup() -> void:
	for n in NAMES:
		for ext in [".rts0sav", ".json", ".png"]:
			if FileAccess.file_exists("user://saves/" + n + ext):
				DirAccess.remove_absolute(ProjectSettings.globalize_path("user://saves/" + n + ext))

func ids(d) -> Array:
	return d.saves.map(func(s): return s.id).filter(func(i): return i.begins_with("zz_e2e"))

func key(code: Key, shift := false) -> void:
	var ev := InputEventKey.new()
	ev.keycode = code
	ev.shift_pressed = shift
	ev.pressed = true
	Input.parse_input_event(ev)

func run() -> void:
	Dialog = load("res://scenes/session/savegame_dialog.gd")
	cleanup()
	var v: int = RtsoMain.get_save_version()
	write_save(NAMES[0], v, 1893510000, "random_continental", "E2E Continental (RMS)", "before the attack")
	write_save(NAMES[1], v, 1896300000, "random_oasis", "E2E Oasis", "")
	write_save(NAMES[2], v - 1, 1896400000, "random_oasis", "E2E Oasis", "old")
	write_save(NAMES[3], v + 1, 1896500000, "random_oasis", "E2E Oasis", "new")
	change_scene_to_file("res://scenes/menu/main_menu.tscn")
	await frames(3)

	print("1. list")
	var d = Dialog.open(current_scene, "load")
	await frames(2)
	var titles := []
	for c in d.list.columns:
		titles.append(d.list.get_column_title(c))
	check(titles == ["Date / Time", "Map Type", "Map Name", "Description"], "0 A.D.'s columns: " + str(titles))
	check(d.find_child("Title", true, false).text == "Load Game" and d.btn_ok.text == "Load", "Load Game with a Load button")
	check(ids(d) == [NAMES[1], NAMES[0]], "compatible saves only, newest first: " + str(ids(d)))
	var row: TreeItem = d.list.get_root().get_child(d.saves.map(func(s): return s.id).find(NAMES[0]))
	check(row.get_text(1) == "Random" and row.get_text(2) == "E2E Continental" and row.get_text(3) == "before the attack", # " (RMS)" trimmed
		"a row shows map type, map and description")
	check(d.selected_save().get("id", "") == d.saves[0].id, "the first save is selected")
	d.sort_by("mapName")
	check(ids(d) == [NAMES[0], NAMES[1]], "Map Name sorts A to Z")
	d.sort_by("mapName")
	check(ids(d) == [NAMES[1], NAMES[0]], "and again Z to A")
	var arrows = preload("res://scenes/common/sort_arrows.gd").attach(d.list)
	check(range(d.list.columns).map(func(c): return arrows.sorted_as(c)) == ["", "", "desc", ""],
		"ModernArrowDown on Map Name, ModernNotSorted on the other headings")
	d.sort_by("date")
	d.compatibility_filter.button_pressed = false
	check(ids(d) == [NAMES[3], NAMES[2], NAMES[1], NAMES[0]], "unticking the filter shows the other save formats")
	check(d.list.get_root().get_child(0).get_custom_color(0) == d.INCOMPATIBLE, "greyed out")
	# load.xml: BackgroundTranslucent (0 0 0 85); ModernSortedList sans-bold-stroke-14 with the modern selection;
	# "No saved games found." centred in 20 12 100%-20 100.
	check(is_equal_approx((d.get_child(0) as ColorRect).color.a, 85.0 / 255.0), "the page dims at 0 0 0 85")
	check(d.list.get_theme_font_size("font_size") == 14 and d.list.get_theme_font("font") is FontVariation
			and not d.list.has_theme_stylebox_override("selected"), "the list in bold 14 with the modern selection")
	check(d.feedback.horizontal_alignment == HORIZONTAL_ALIGNMENT_CENTER and d.feedback.offset_top == 12 and d.feedback.offset_bottom == 100,
			"the empty-list notice is centred")

	print("2. details")
	d.select_save(d.saves.map(func(s): return s.id).find(NAMES[0]))
	# map_preview is null in an unimported headless project; the dialog shows what Game Setup shows.
	check(d.details.visible and d.labels.name.text == "E2E Continental"
		and d.labels.preview.texture == load("res://scenes/menu/gamesetup.gd").map_preview("random_continental"), "the map and its preview")
	check(d.labels.players.text == "2" and d.labels.time.text == "12:34" and d.labels.type.text == "Random"
		and d.labels.size.text == "Normal" and d.labels.victory.text == "Conquest, Wonder",
		"players, played time, type, size, victory: %s %s %s %s %s" % [d.labels.players.text, d.labels.time.text, d.labels.type.text, d.labels.size.text, d.labels.victory.text])
	check(d.find_child("GameSelection", true, false).get_column_title(0).begins_with("Date / Time") and not d.invalid_logo.visible, "a selected save hides the logo")
	check(d.labels.version.text == "Mods: RTS0 (save format v%d)" % v and d.players_text.get_parsed_text().contains("e2eAlpha (Athenians)"), "save format and players")
	d.select_save(d.saves.map(func(s): return s.id).find(NAMES[2]))
	check(d.labels.version.get_theme_color("font_color") == Color.WHITE, "another save format with the same mods keeps Mods white (hasSameMods)")
	d.select_save(d.saves.map(func(s): return s.id).find(NAMES[0]))

	print("3. other save formats")
	d.select_save(d.saves.map(func(s): return s.id).find(NAMES[3]))
	d.load_pressed()
	await frames(1)
	check(d.message != null and d.message.message.contains("needs RTS0 save format"), "a newer save can't be loaded")
	d.message.find_child("Button1", true, false).pressed.emit() # OK
	d.select_save(d.saves.map(func(s): return s.id).find(NAMES[2]))
	d.load_pressed()
	await frames(1)
	check(d.confirm != null and d.confirm.message.contains("Do you still want to proceed?"), "an older save asks first")
	d.confirm.find_child("Button1", true, false).pressed.emit() # No

	print("4. delete")
	key(KEY_DELETE, true)
	await frames(2)
	check(not FileAccess.file_exists("user://saves/%s.rts0sav" % NAMES[2]) and not ids(d).has(NAMES[2]), "Shift+Delete deletes without asking")
	check(d.selected_save().get("id", "") == NAMES[1], "and selects the next save")
	key(KEY_DELETE)
	await frames(2)
	check(d.confirm != null and d.confirm.message.contains("permanently deleted"), "Delete asks first")
	check(d.confirm != null and RegEx.create_from_string("^\"\\d{4}-\\d\\d-\\d\\d \\d\\d:\\d\\d:\\d\\d maps/(random|skirmishes|scenarios)/\\S+").search(d.confirm.message) != null,
		"naming the save by its date and map path (generateSavegameLabel): " + (d.confirm.message.get_slice("\n", 0) if d.confirm else ""))
	d.confirm.find_child("Button2", true, false).pressed.emit() # Yes
	await frames(1)
	check(not FileAccess.file_exists("user://saves/%s.json" % NAMES[1]), "and deletes once confirmed")
	d.close()
	# SavegameList / SavegameDetails / SavegameLoader: the mods a save was made with.
	write_save(NAMES[5], v, 1896600000, "random_oasis", "E2E Oasis", "modded")
	var modded = JSON.parse_string(FileAccess.get_file_as_string("user://saves/%s.json" % NAMES[5]))
	modded["mods"] = [{"name": "zz_other_mod", "version": "9", "ignoreInCompatibilityChecks": false}]
	FileAccess.open("user://saves/%s.json" % NAMES[5], FileAccess.WRITE).store_string(JSON.stringify(modded))
	d = Dialog.open(current_scene, "load")
	await frames(2)
	check(not ids(d).has(NAMES[5]), "a save made with other mods is filtered out")
	d.compatibility_filter.button_pressed = false
	var mi: int = d.saves.map(func(s): return s.id).find(NAMES[5])
	check(mi >= 0 and d.list.get_root().get_child(mi).get_custom_color(0) == d.INCOMPATIBLE, "and greyed out without the filter")
	d.select_save(mi)
	check(d.labels.version.text == "Mods: zz_other_mod (9)" and d.labels.version.get_theme_color("font_color") == Color.ORANGE,
		"its details name its mods in orange: " + d.labels.version.text)
	d.load_pressed()
	await frames(1)
	check(d.confirm != null and d.confirm.message.contains("needs a different sequence of mods") and d.confirm.message.contains("zz_other_mod (9)")
		and d.confirm.message.contains("Do you still want to proceed?"), "loading it asks first, naming the mods")
	if d.confirm:
		d.confirm.find_child("Button1", true, false).pressed.emit() # No
	d.close()

	print("5. save in a match")
	var ms = root.get_node("MatchSettings")
	ms.map_id = "random_continental"
	ms.seed = 99
	ms.map_size = 128
	ms.slot_types = [0, 1]
	ms.slot_civs = [0, 1]
	ms.slot_teams = [1, 2]
	ms.replay_dir = ""
	ms.load_save_path = ""
	change_scene_to_file("res://scenes/main.tscn")
	await frames(120)
	var m = current_scene
	d = Dialog.open(m, "save", m)
	await frames(2)
	check(d.selected_save().is_empty() and d.btn_ok.text == "Save" and d.description.visible, "nothing selected: Save makes a new save")
	var before := Array(DirAccess.get_files_at("user://saves"))
	d.description.text = "e2e first"
	d.description.text_changed.emit(d.description.text)
	d.save_pressed()
	await frames(2)
	var made := ""
	var dir := DirAccess.open("user://saves")
	for f in dir.get_files():
		if f.ends_with(".rts0sav") and not before.has(f):
			made = f.get_basename()
	check(made != "" and not is_instance_valid(d), "the save is written and the dialog closes")
	var log_label: RichTextLabel = m.find_child("LogMessages", true, false)
	check(log_label != null and log_label.visible and log_label.get_parsed_text() == "Saved game to 'saves/%s.rts0sav'" % made, "\"Saved game to 'saves/%s.rts0sav'\" in the top-left log" % made)
	if made != "":
		DirAccess.rename_absolute(ProjectSettings.globalize_path("user://saves/%s.rts0sav" % made), ProjectSettings.globalize_path("user://saves/%s.rts0sav" % NAMES[4]))
		DirAccess.rename_absolute(ProjectSettings.globalize_path("user://saves/%s.json" % made), ProjectSettings.globalize_path("user://saves/%s.json" % NAMES[4]))
	d = Dialog.open(m, "save", m)
	await frames(2)
	d.select_save(d.saves.map(func(s): return s.id).find(NAMES[4]))
	check(d.description.text == "e2e first", "picking a save fills in its description")
	d.save_pressed()
	await frames(1)
	check(d.confirm != null and d.confirm.message.contains("permanently overwritten"), "saving over it asks first")
	d.confirm.find_child("Button2", true, false).pressed.emit() # Yes
	await frames(2)
	var meta: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("user://saves/%s.json" % NAMES[4]))
	check(not is_instance_valid(d) and meta.get("description", "") == "e2e first" and int(meta.save_version) == v, "and overwrites it")
	var saved_tick: float = meta.get("game_time", 0.0)
	d = Dialog.open(m, "save", m)
	await frames(2)
	d.select_save(d.saves.map(func(s): return s.id).find(NAMES[4]))
	before = Array(DirAccess.get_files_at("user://saves"))
	d.description.text_submitted.emit(d.description.text)
	await frames(2)
	var fresh := Array(DirAccess.get_files_at("user://saves")).filter(func(f): return f.ends_with(".rts0sav") and not before.has(f))
	check(fresh.size() == 1 and not is_instance_valid(d), "Enter in the description writes a new save, even with one selected")
	for f in fresh:
		for ext in [".rts0sav", ".json", ".png"]:
			DirAccess.remove_absolute(ProjectSettings.globalize_path("user://saves/" + f.get_basename() + ext))

	print("6. load")
	var written = JSON.parse_string(FileAccess.get_file_as_string("user://saves/%s.json" % NAMES[4]))
	check(written is Dictionary and written.get("mods") is Array and not written.mods.is_empty(), "a save records the mods it was made with")
	d = Dialog.open(m, "load", m)
	await frames(2)
	d.select_save(d.saves.map(func(s): return s.id).find(NAMES[4]))
	d.load_pressed()
	for i in 1200: # through the loading screen
		await process_frame
		if current_scene != m and current_scene and current_scene.has_method("save_game_as"):
			break
	await frames(5)
	var m2 = current_scene
	check(m2 != m and m2.has_method("save_game_as"), "Load starts the match again")
	var t: float = m2.get_match_statistics().get("game_time", -1.0)
	check(t >= saved_tick and t < saved_tick + 2.0, "at the saved time (%.1f s, saved at %.1f s)" % [t, saved_tick])
	cleanup()
	m2.queue_free() # leave the match before quitting
	await frames(3)
	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(0 if failures == 0 else 1)
