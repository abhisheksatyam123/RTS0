extends CanvasLayer
# Save and load dialogs, a port of 0 A.D. gui/loadgame (load.xml,
# SavegameList.js, SavegameDetails.js, SavegameDeleter.js, SavegameLoader.js,
# SavegameWriter.js):
#   - the list has Date / Time, Map Type, Map Name and Description columns,
#     sorts by any of them (newest first by default) and by default shows only
#     compatible saves ("Filter compatible saved games");
#   - the details show the map with its preview image, the player count, played
#     time, map type, size, victory condition, save format and the players by team;
#   - Save writes a new save, or overwrites the selected one after asking, keeping
#     the selected save's description unless it was edited;
#   - Load loads the selected save, asking first when it was written with another
#     save format; a save from a newer RTS0 can't be loaded;
#   - Delete (session.savedgames.delete) asks first, unless Shift
#     (session.savedgames.noconfirmation) is held.
#
# Saves are user://saves/<name>.rts0sav with <name>.json (metadata) and
# <name>.png (a screenshot, when one could be taken) beside it, all written by
# RtsoMain.save_game. Save calls the RtsoMain it is given. Load (from a match
# or the main menu) starts the match scene with MatchSettings.load_save_path
# set, and the scene loads it after building the map.
const MODERN := preload("res://scenes/common/modern_theme.gd")
const SORT_ARROWS := preload("res://scenes/common/sort_arrows.gd")
const L10n := preload("res://scenes/menu/l10n.gd")
const MSGBOX := preload("res://scenes/common/msgbox.gd")

signal closed

const SAVE_ROOT := "user://saves"
const ReplayPage = preload("res://scenes/menu/replay_browser.gd")
const SetupPage = preload("res://scenes/menu/gamesetup.gd")
const Mods := preload("res://scenes/menu/mods.gd")
const GOLD := Color(0.9, 0.75, 0.31)
const INCOMPATIBLE := Color8(96, 96, 96)
const COLUMNS := [["date", "Date / Time", 24], ["mapType", "Map Type", 21], ["mapName", "Map Name", 33], ["description", "Description", 22]] # load.xml widths
var _bold: FontVariation = preload("res://scenes/session/summary_screen.gd").bold_font() # sans-bold

var mode := "load" # "save" or "load"
var game: Node = null # RtsoMain, or null in the main menu

var saves: Array = [] # shown entries: {path, id, meta, compatible, newer, date, map, map_id, map_type, size, victory, description}
var sort_column := "date"
var sort_order := -1
var list: Tree
var feedback: Label
var compatibility_filter: CheckBox
var description: LineEdit
var details: Control
var invalid_logo: TextureRect
var labels := {}
var players_text: RichTextLabel
var btn_ok: Button
var btn_delete: Button
# The open message box (scenes/common/msgbox.gd): a No/Yes question, or a notice; null when closed.
var confirm: Control
var message: Control
var _description_changed := false
var _selected_id := ""
var _pending := ""
var _hotkeys = null

static func open(parent: Node, dialog_mode: String, rts_main: Node = null) -> CanvasLayer:
	var d = load("res://scenes/session/savegame_dialog.gd").new()
	d.mode = dialog_mode
	d.game = rts_main
	parent.add_child(d)
	return d

func _ready() -> void:
	layer = 20
	process_mode = Node.PROCESS_MODE_ALWAYS
	if ClassDB.class_exists("RtsoHotkeyConfig"):
		_hotkeys = ClassDB.instantiate("RtsoHotkeyConfig")
	_build()
	refresh()
	if mode == "load":
		select_save(0) # LoadGamePage: selectFirst

# --- List (SavegameList.js) --------------------------------------------------------

## The save as the list and details read it.
func _entry(path: String, meta: Dictionary) -> Dictionary:
	var settings: Dictionary = meta.get("settings", {})
	var map_id := str(meta.get("map_id", settings.get("map_id", "")))
	var info := ReplayPage.map_info(map_id)
	var v := int(meta.get("save_version", 0))
	var date := str(meta.get("date", "")).replace("T", " ")
	if meta.has("timestamp"):
		var local := int(meta.timestamp) + int(Time.get_time_zone_from_system().get("bias", 0)) * 60
		date = Time.get_datetime_string_from_unix_time(local, true)
	# hasSameMods: a save from before saves kept their mods is checked on its format alone.
	var mods_ok: bool = not (meta.get("mods") is Array) or Mods.play_compatible(meta.mods, _active_mods())
	return {"path": path, "id": path.get_file().get_basename(), "meta": meta,
		"compatible": v == RtsoMain.get_save_version() and mods_ok, "mods_ok": mods_ok,
		"newer": v > RtsoMain.get_save_version(),
		"timestamp": int(meta.get("timestamp", 0)), "date": date, "map": str(meta.get("map_name", map_id)).trim_suffix(" (RMS)"),
		"map_id": map_id, "map_type": info["type"],
		"size": int(settings.get("map_size", 0)) if info["type"] == "Random" else int(info["size"]),
		"victory": int(settings.get("victory", 0)), "description": str(meta.get("description", "")),
		"players": meta.get("players", [])}

var _mods_cache = null

## Engine.GetEngineInfo().mods, read once per dialog.
func _active_mods() -> Array:
	if _mods_cache == null:
		_mods_cache = Mods.engine_mods()
	return _mods_cache

## updateSavegameList.
func refresh() -> void:
	var prev_index := list.get_selected().get_index() if list.get_selected() else -1
	saves.clear()
	var dir := DirAccess.open(SAVE_ROOT)
	if dir:
		for f in dir.get_files():
			if not f.ends_with(".rts0sav"):
				continue
			var path := SAVE_ROOT.path_join(f)
			var meta := _read_json(path.get_basename() + ".json")
			if meta.is_empty():
				# Saves from before metadata existed still list, by file date.
				meta = {"timestamp": FileAccess.get_modified_time(path), "map_name": "?"}
			var e := _entry(path, meta)
			# SavegameList campaignFilter: a campaign's saves only inside that run, the others only outside one;
			# with the filter off they list greyed, as incompatible.
			if str(meta.get("settings", {}).get("campaign_run", "")) != MatchSettings.campaign_run:
				if compatibility_filter.button_pressed or MatchSettings.campaign_run != "":
					continue
				e.compatible = false
			if compatibility_filter.button_pressed and not e.compatible:
				continue
			saves.append(e)
	saves.sort_custom(func(a, b):
		var ka = _sort_key(a)
		var kb = _sort_key(b)
		if ka == kb:
			return a.id < b.id
		return ka > kb if sort_order < 0 else ka < kb)
	list.clear()
	var root := list.create_item()
	for s in saves:
		var row := list.create_item(root)
		row.set_text(0, s.date)
		row.set_text(1, s.map_type)
		row.set_text(2, s.map)
		row.set_text(3, s.description)
		if not s.compatible:
			for c in COLUMNS.size():
				row.set_custom_color(c, INCOMPATIBLE)
	for c in COLUMNS.size():
		list.set_column_title(c, L10n.t(COLUMNS[c][1]))
	SORT_ARROWS.attach(list).set_sorted(COLUMNS.map(func(col): return col[0]).find(sort_column), sort_order)
	feedback.visible = saves.is_empty()
	list.mouse_filter = Control.MOUSE_FILTER_IGNORE if saves.is_empty() else Control.MOUSE_FILTER_STOP # gameSelection.enabled
	# Keep the selected save if it is still listed, else the same row (or the last one).
	var i := saves.map(func(s): return s.id).find(_selected_id)
	if i < 0 and _selected_id != "" and prev_index >= 0 and not saves.is_empty():
		i = mini(prev_index, saves.size() - 1)
	if i >= 0:
		list.set_selected(root.get_child(i), 0)
	else:
		_selected_id = ""
	_on_selection_change()

func _sort_key(s: Dictionary) -> Variant:
	match sort_column:
		"mapName": return s.map
		"mapType": return s.map_type
		"description": return s.description
	return s.timestamp

## Clicking a column heading sorts by it; clicking it again reverses the order.
func sort_by(column: String) -> void:
	if sort_column == column:
		sort_order = -sort_order
	else:
		sort_column = column
		sort_order = -1 if column == "date" else 1
	refresh()

func selected_save() -> Dictionary:
	var item := list.get_selected()
	if item == null:
		return {}
	var i := item.get_index()
	return saves[i] if i >= 0 and i < saves.size() else {}

func select_save(index: int) -> void:
	if index < 0 or index >= saves.size():
		list.deselect_all()
		_selected_id = ""
		_on_selection_change()
		return
	list.set_selected(list.get_root().get_child(index), 0)

## generateSavegameLabel: the date, initAttributes.map (the map's path, such as
## "maps/random/mainland") and the description.
func save_label(s: Dictionary) -> String:
	var fmt := "%(dateString)s %(map)s - %(description)s" if s.description != "" else "%(dateString)s %(map)s"
	return L10n.sprintf(L10n.t(fmt), {"dateString": s.date, "map": map_path(str(s.get("map_id", ""))), "description": s.description})

## The 0.28 map path behind a map id: maps/random/<script> for a random map, else
## maps/<skirmishes|scenarios|tutorials>/<name>.
static func map_path(map_id: String) -> String:
	for r in SetupPage.RMS_MAPS:
		if r[0] == map_id:
			return "maps/random/" + SetupPage.rms_script(map_id)
	var type := str(SetupPage._map_types_data().get("maps", {}).get(map_id, {}).get("type", "skirmish"))
	return "maps/%s/%s" % [{"scenario": "scenarios", "tutorial": "tutorials"}.get(type, "skirmishes"), map_id]

## Every handler's onSelectionChange.
func _on_selection_change() -> void:
	var s := selected_save()
	_selected_id = s.get("id", "")
	btn_delete.disabled = s.is_empty()
	btn_ok.disabled = mode == "load" and s.is_empty()
	if mode == "save" and not _description_changed and not s.is_empty():
		description.text = s.description
	_show_details(s)

# --- Details (SavegameDetails.js) ----------------------------------------------------

func _show_details(s: Dictionary) -> void:
	details.visible = not s.is_empty()
	invalid_logo.visible = s.is_empty() # invalidGame: the 0 A.D. logo
	if s.is_empty():
		return
	var m: Dictionary = s.meta
	labels.name.text = s.map
	var tex: Texture2D = SetupPage.map_preview(s.map_id) if s.map_id != "" else null
	if tex == null and FileAccess.file_exists(s.path.get_basename() + ".png"):
		var img := Image.load_from_file(ProjectSettings.globalize_path(s.path.get_basename() + ".png"))
		if img:
			tex = ImageTexture.create_from_image(img)
	labels.preview.texture = tex
	labels.players.text = str(s.players.size())
	labels.time.text = ReplayPage.time_to_string(float(m.get("game_time", 0.0)))
	labels.type.text = s.map_type
	labels.size.text = ReplayPage.map_size_name(s.size)
	labels.victory.text = ReplayPage.victory_name(s.victory)
	# SavegameDetails: the save's mods (modsToString), in orange when they differ.
	var v := int(m.get("save_version", 0))
	var mods_text := "RTS0 (save format v%d)" % v
	if m.get("mods") is Array:
		mods_text = ", ".join((m.mods as Array).map(func(x) -> String: return Mods.mod_to_string(x) if x is Dictionary else ""))
	labels.version.text = L10n.sprintf(L10n.t("Mods: %(mods)s"), {"mods": mods_text})
	labels.version.add_theme_color_override("font_color", Color.WHITE if s.mods_ok else Color.ORANGE)
	players_text.text = ReplayPage.player_info(s, true)

# --- Actions (SavegameLoader.js, SavegameWriter.js, SavegameDeleter.js) ---------------

func confirm_pressed() -> void:
	if mode == "save":
		save_pressed()
	else:
		load_pressed()

func load_pressed() -> void:
	var s := selected_save()
	if s.is_empty():
		return
	if s.compatible:
		_load(s.path)
		return
	var v := int(s.meta.get("save_version", 0))
	if s.newer:
		# A newer save format can't be read at all, so there is no Yes.
		_show_message(L10n.sprintf(L10n.t("This savegame needs RTS0 save format %(required)s or compatible. You are running save format %(current)s."),
			{"required": v, "current": RtsoMain.get_save_version()}))
		return
	# SavegameLoader: the engine-version line, the mods line, then "Do you still want to proceed?".
	var text := ""
	if v != RtsoMain.get_save_version():
		text += L10n.sprintf(L10n.t("This savegame needs RTS0 save format %(required)s or compatible. You are running save format %(current)s."),
			{"required": v, "current": RtsoMain.get_save_version()}) + "\n"
	if not s.mods_ok:
		text += L10n.t("This savegame needs a different sequence of mods:") + "\n" + Mods.compared_mods_string(s.meta.mods, _active_mods()) + "\n"
	_ask(L10n.t("Warning"), text + L10n.t("Do you still want to proceed?"), "load")

func save_pressed() -> void:
	var s := selected_save()
	if s.is_empty():
		_save_to(_new_name())
	else:
		_ask(L10n.t("OVERWRITE SAVE"), "\"%s\"\n" % save_label(s) + L10n.t("Saved game will be permanently overwritten, are you sure?"), "overwrite")

func delete_pressed(no_confirmation := false) -> void:
	var s := selected_save()
	if s.is_empty():
		return
	if no_confirmation:
		delete_selected()
	else:
		_ask(L10n.t("DELETE"), "\"%s\"\n" % save_label(s) + L10n.t("Saved game will be permanently deleted, are you sure?"), "delete")

func delete_selected() -> void:
	var s := selected_save()
	if s.is_empty():
		return
	var i := list.get_selected().get_index()
	for ext in [".rts0sav", ".json", ".png"]:
		var f: String = s.path.get_basename() + ext
		if FileAccess.file_exists(f):
			DirAccess.remove_absolute(ProjectSettings.globalize_path(f))
	_selected_id = ""
	refresh()
	# The next save takes the deleted one's place in the list.
	if not saves.is_empty():
		select_save(mini(i, saves.size() - 1))

## SavegameDeleter/SavegameWriter/SavegameLoader messageBox: No, Yes (500x250 for the load warning).
func _ask(title: String, text: String, action: String) -> void:
	_pending = action
	confirm = MSGBOX.open(self, 500, 250 if action == "load" else 200, text, title, [tr("No"), tr("Yes")])
	confirm.answered.connect(func(i: int):
		confirm = null
		if i == 1: _on_confirmed())

func _show_message(text: String) -> void:
	message = MSGBOX.open(self, 500, 200, text, tr("Error"))
	message.answered.connect(func(_i): message = null)

func _on_confirmed() -> void:
	var s := selected_save()
	if _pending == "delete":
		delete_selected()
	elif s.is_empty():
		return
	elif _pending == "overwrite":
		_save_to(s.id)
	elif _pending == "load":
		_load(s.path)

func _new_name() -> String:
	return "savegame_" + Time.get_datetime_string_from_system(false, false).replace(":", "").replace("-", "").replace("T", "_")

func _save_to(name: String) -> void:
	if game == null or not game.has_method("save_game_as"):
		_show_message(L10n.t("No match to save."))
		return
	if game.has_method("is_replay_playback") and game.is_replay_playback():
		_show_message(L10n.t("A replay can't be saved."))
		return
	if game.save_game_as(name, description.text.strip_edges()):
		close()
	else:
		_show_message(L10n.t("Saving failed."))

func _load(path: String) -> void:
	# Loading ends the current match (as in 0 A.D.): the match scene starts over,
	# rebuilds the map from the save's setup, then loads the save on top.
	MatchSettings.load_save_path = path
	MatchSettings.replay_dir = ""
	get_tree().paused = false
	MatchSettings.go("res://scenes/main.tscn")

func close() -> void:
	closed.emit()
	queue_free()

func _fired(event: InputEvent, hotkey: String) -> bool:
	if _hotkeys == null or not (event is InputEventKey) or not event.pressed or event.echo:
		return false
	var keys: Array = [str(_hotkeys.call("event_key_name", event)).to_lower()]
	for m in [["ctrl", event.ctrl_pressed], ["shift", event.shift_pressed], ["alt", event.alt_pressed]]:
		if m[1] and not keys.has(m[0]):
			keys.append(m[0])
	keys.sort()
	for combo in _hotkeys.call("get_hotkey_map").get(hotkey, PackedStringArray()):
		var want: Array = Array(str(combo).to_lower().split("+"))
		want.sort()
		if want == keys:
			return true
	return false

## session.savedgames.delete's key, whatever modifiers are held (the noconfirmation one goes with it).
func _delete_key(event: InputEventKey) -> bool:
	if _hotkeys == null:
		return event.keycode == KEY_DELETE
	var name := str(_hotkeys.call("event_key_name", event)).to_lower()
	for combo in _hotkeys.call("get_hotkey_map").get("session.savedgames.delete", PackedStringArray()):
		if str(combo).to_lower().split("+")[-1] == name:
			return true
	return false

## SavegameDeleter deleteButtonTooltip: every key bound to the delete hotkeys.
func _delete_tooltip() -> String:
	# colorizeHotkey: the gold "[key]", or "Unassigned hotkey: name" translated.
	var cfg := RtsoHotkeyConfig.new()
	return cfg.colorize(L10n.t("Delete the selected savegame using %(hotkey)s."), "session.savedgames.delete") + \
		"\n" + cfg.colorize(L10n.t("Hold %(hotkey)s to skip the confirmation dialog while deleting."), "session.savedgames.noconfirmation")

## session.savedgames.noconfirmation held, from the key event's own modifiers when it names one.
func _skip_confirmation(event: InputEventKey) -> bool:
	var keys: Dictionary = _hotkeys.call("get_hotkey_map") if _hotkeys else {}
	for combo in keys.get("session.savedgames.noconfirmation", PackedStringArray(["Shift"])):
		var c := str(combo).to_lower()
		if (c == "shift" and event.shift_pressed) or (c == "ctrl" and event.ctrl_pressed) or (c == "alt" and event.alt_pressed):
			return true
	return _held("session.savedgames.noconfirmation")

func _held(hotkey: String) -> bool:
	return _hotkeys.call("is_held", hotkey) if _hotkeys else Input.is_key_pressed(KEY_SHIFT)

func _unhandled_input(event: InputEvent) -> void:
	if not (event is InputEventKey) or not event.pressed or confirm != null or message != null:
		return
	if _fired(event, "cancel"):
		get_viewport().set_input_as_handled()
		close()
	elif _delete_key(event) and not description.has_focus():
		get_viewport().set_input_as_handled()
		delete_pressed(_skip_confirmation(event))

# --- Page (load.xml) -------------------------------------------------------------------

func _label(text: String, size := 14, color := Color.WHITE) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_color_override("font_color", color)
	return l

func _button(parent: Node, text: String, cb: Callable) -> Button:
	var b := Button.new()
	b.text = text
	b.custom_minimum_size = Vector2(0, 32)
	b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	b.pressed.connect(cb)
	parent.add_child(b)
	return b

func _info_row(parent: VBoxContainer, caption: String, key: String) -> void:
	var row := HBoxContainer.new()
	var cap := _label(caption, 14) # ModernLeftLabelText: white
	cap.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(cap)
	var val := _label("", 14)
	val.name = "Saved" + key.capitalize().replace(" ", "")
	val.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	val.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	row.add_child(val)
	parent.add_child(row)
	labels[key] = val

func _build() -> void:
	var bg := ColorRect.new() # BackgroundTranslucent
	bg.color = Color8(0, 0, 0, 85) # common/sprites.xml: 0 0 0 85
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)
	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	center.theme = MODERN.theme() # 0.28's modern skin
	add_child(center)
	var panel := PanelContainer.new() # ModernDialog, 920 x 650
	panel.theme_type_variation = "ModernDialogContainer"
	preload("res://scenes/common/fit_screen.gd").cap(panel, Vector2(920, 650))
	center.add_child(panel)
	var outer := VBoxContainer.new()
	outer.add_theme_constant_override("separation", 6)
	panel.add_child(outer)
	# load.xml: the title on the dialog's top edge (-18..14), as ModernDialog pages draw it.
	load("res://scenes/menu/hotkeys.gd")._edge_title(panel, tr("Save Game") if mode == "save" else tr("Load Game"))
	var cols := HBoxContainer.new()
	cols.size_flags_vertical = Control.SIZE_EXPAND_FILL
	cols.add_theme_constant_override("separation", 16)
	outer.add_child(cols)

	# Left: the list, the description (saving) and the buttons.
	var left := VBoxContainer.new()
	left.custom_minimum_size = Vector2(590, 0)
	left.add_theme_constant_override("separation", 8)
	cols.add_child(left)
	var list_box := Control.new()
	list_box.size_flags_vertical = Control.SIZE_EXPAND_FILL
	left.add_child(list_box)
	list = Tree.new()
	list.name = "GameSelection"
	list.set_anchors_preset(Control.PRESET_FULL_RECT)
	list.columns = COLUMNS.size()
	list.column_titles_visible = true
	list.hide_root = true
	list.select_mode = Tree.SELECT_ROW
	list.allow_reselect = true
	# ModernSortedList: sans-bold-stroke-14, the modern theme's white-edged selection.
	list.add_theme_font_override("font", _bold)
	list.add_theme_font_size_override("font_size", 14)
	list.add_theme_color_override("font_outline_color", Color.BLACK)
	list.add_theme_constant_override("outline_size", 3)
	for c in COLUMNS.size():
		list.set_column_expand(c, true)
		list.set_column_expand_ratio(c, COLUMNS[c][2])
		list.set_column_clip_content(c, true)
	list.column_title_clicked.connect(func(c, _b): sort_by(COLUMNS[c][0]))
	list.item_selected.connect(_on_selection_change)
	list.item_activated.connect(confirm_pressed) # onMouseLeftDoubleClickItem
	if mode == "save":
		# Clicking below the saves picks a new save again.
		list.empty_clicked.connect(func(_p, _b): select_save(-1))
	list_box.add_child(list)
	# ModernLabelText in 20 12 100%-20 100: centred both ways.
	feedback = _label("No saved games found.", 14)
	feedback.name = "GameSelectionFeedback"
	feedback.add_theme_font_override("font", _bold)
	feedback.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	feedback.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	feedback.anchor_right = 1.0
	feedback.offset_left = 20
	feedback.offset_top = 12
	feedback.offset_right = -20
	feedback.offset_bottom = 100
	list_box.add_child(feedback)
	description = LineEdit.new()
	description.name = "SaveGameDesc"
	description.visible = mode == "save"
	MatchSettings.prepare_line_edit(description)
	description.text_changed.connect(func(_t): _description_changed = true)
	# SavegameWriter: Enter in the description always writes a new save; only the
	# Save button overwrites the selected one.
	description.text_submitted.connect(func(_t): _save_to(_new_name()))
	left.add_child(description)
	var buttons := HBoxContainer.new()
	buttons.add_theme_constant_override("separation", 12)
	left.add_child(buttons)
	_button(buttons, "Cancel", close)
	btn_delete = _button(buttons, "Delete", func(): delete_pressed(_held("session.savedgames.noconfirmation")))
	btn_delete.tooltip_text = _delete_tooltip()
	btn_ok = _button(buttons, "Save" if mode == "save" else "Load", confirm_pressed)

	# Right: the filter and the details.
	var right := VBoxContainer.new()
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	right.add_theme_constant_override("separation", 4)
	cols.add_child(right)
	compatibility_filter = CheckBox.new()
	compatibility_filter.name = "CompatibilityFilter"
	compatibility_filter.text = "Filter compatible saved games"
	compatibility_filter.button_pressed = true
	compatibility_filter.flat = true
	compatibility_filter.toggled.connect(func(_on): refresh())
	right.add_child(compatibility_filter)
	var valid := VBoxContainer.new()
	valid.name = "ValidGame"
	valid.size_flags_vertical = Control.SIZE_EXPAND_FILL
	valid.add_theme_constant_override("separation", 3)
	right.add_child(valid)
	details = valid
	invalid_logo = TextureRect.new() # invalidGame "650 105 870 205", sprite 0ADLogo
	invalid_logo.name = "InvalidGame"
	invalid_logo.texture = load("res://assets/textures/ui/pregame/logo/0ad_logo.png")
	invalid_logo.custom_minimum_size = Vector2(220, 100)
	invalid_logo.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	invalid_logo.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	invalid_logo.visible = false
	right.add_child(invalid_logo)
	valid.add_child(HSeparator.new())
	var map_name := _label("", 14) # savedMapName: ModernLabelText, white, centred
	map_name.name = "SavedMapName"
	map_name.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	map_name.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	valid.add_child(map_name)
	labels["name"] = map_name
	var preview := TextureRect.new()
	preview.name = "SavedInfoPreview"
	preview.custom_minimum_size = Vector2(0, 188)
	preview.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	preview.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	valid.add_child(preview)
	labels["preview"] = preview
	_info_row(valid, "Players:", "players")
	_info_row(valid, "Played time:", "time")
	_info_row(valid, "Map Type:", "type")
	_info_row(valid, "Map Size:", "size")
	_info_row(valid, "Victory:", "victory")
	valid.add_child(HSeparator.new())
	var version := _label("", 14) # ModernLeftLabelText: sans-bold-stroke-14
	version.name = "SavedMods"
	version.add_theme_font_override("font", _bold)
	version.add_theme_color_override("font_outline_color", Color.BLACK)
	version.add_theme_constant_override("outline_size", 3)
	valid.add_child(version)
	labels["version"] = version
	valid.add_child(HSeparator.new())
	players_text = RichTextLabel.new()
	players_text.name = "SavedPlayersNames"
	players_text.bbcode_enabled = true
	players_text.size_flags_vertical = Control.SIZE_EXPAND_FILL
	players_text.add_theme_font_size_override("normal_font_size", 14) # MapPlayerList: sans-14
	players_text.add_theme_font_size_override("bold_font_size", 14)
	valid.add_child(players_text)


func _read_json(path: String) -> Dictionary:
	if not FileAccess.file_exists(path):
		return {}
	var v = JSON.parse_string(FileAccess.get_file_as_string(path))
	return v if v is Dictionary else {}
