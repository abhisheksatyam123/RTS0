extends CanvasLayer
# Save and load dialogs, after 0 A.D. gui/savedgames (save.js, load.js,
# savegame_list.js): every saved game newest first with its date, map and
# description. Save writes a new slot or overwrites the selected one (after
# asking); Load loads the selected one; Delete removes it (after asking).
#
# Saves are user://saves/<name>.rts0sav with <name>.json (metadata) and
# <name>.png (a screenshot, when one could be taken) beside it, all written by
# RtsoMain.save_game. A save written by a newer RTS0 (a higher save format) is
# shown in red and can't be loaded, as 0 A.D. marks incompatible saves. Save calls the RtsoMain it is given. Load
# (from a match or the main menu) starts the match scene with
# MatchSettings.load_save_path set, and the scene loads it after building the map.

signal closed

const SAVE_ROOT := "user://saves"

var mode := "load" # "save" or "load"
var game: Node = null # RtsoMain, or null in the main menu

var _saves: Array = [] # {path, meta}
var _list: ItemList
var _details: RichTextLabel
var _thumb: TextureRect
var _desc: LineEdit
var _status: Label
var _btn_ok: Button
var _btn_delete: Button
var _confirm: ConfirmationDialog
var _pending := ""

static func open(parent: Node, dialog_mode: String, rts_main: Node = null) -> CanvasLayer:
	var d = load("res://scenes/session/savegame_dialog.gd").new()
	d.mode = dialog_mode
	d.game = rts_main
	parent.add_child(d)
	return d

func _ready() -> void:
	layer = 20
	process_mode = Node.PROCESS_MODE_ALWAYS
	var theme_res = load("res://assets/themes/zeroad_theme.tres")
	var bg := ColorRect.new()
	bg.color = Color(0, 0, 0, 0.6)
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)
	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	if theme_res:
		center.theme = theme_res
	add_child(center)
	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(860, 500)
	center.add_child(panel)
	var vbox := VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 10)
	panel.add_child(vbox)
	var title := Label.new()
	title.text = "Save Game" if mode == "save" else "Load Game"
	title.add_theme_font_size_override("font_size", 20)
	title.add_theme_color_override("font_color", Color(0.9, 0.75, 0.31))
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	vbox.add_child(title)
	var row := HBoxContainer.new()
	row.size_flags_vertical = Control.SIZE_EXPAND_FILL
	row.add_theme_constant_override("separation", 12)
	vbox.add_child(row)
	_list = ItemList.new()
	_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_list.size_flags_stretch_ratio = 1.5
	_list.item_selected.connect(_on_selected)
	_list.item_activated.connect(func(_i): _on_ok())
	row.add_child(_list)
	var side := VBoxContainer.new()
	side.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(side)
	_thumb = TextureRect.new()
	_thumb.custom_minimum_size = Vector2(320, 180)
	_thumb.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_thumb.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	side.add_child(_thumb)
	_details = RichTextLabel.new()
	_details.bbcode_enabled = true
	_details.size_flags_vertical = Control.SIZE_EXPAND_FILL
	side.add_child(_details)
	if mode == "save":
		_desc = LineEdit.new()
		_desc.placeholder_text = "Description (optional)"
		_desc.max_length = 80
		MatchSettings.prepare_line_edit(_desc)
		_desc.text_submitted.connect(func(_t): _on_ok())
		vbox.add_child(_desc)
	_status = Label.new()
	_status.add_theme_color_override("font_color", Color(1.0, 0.6, 0.4))
	vbox.add_child(_status)
	var buttons := HBoxContainer.new()
	buttons.alignment = BoxContainer.ALIGNMENT_END
	buttons.add_theme_constant_override("separation", 12)
	vbox.add_child(buttons)
	_btn_delete = _button(buttons, "Delete", _on_delete)
	_button(buttons, "Cancel", close)
	_btn_ok = _button(buttons, "Save" if mode == "save" else "Load", _on_ok)
	_confirm = ConfirmationDialog.new()
	_confirm.process_mode = Node.PROCESS_MODE_ALWAYS
	_confirm.confirmed.connect(_on_confirmed)
	add_child(_confirm)
	refresh()
	if _desc:
		_desc.grab_focus()

func _button(parent: Node, text: String, cb: Callable) -> Button:
	var b := Button.new()
	b.text = text
	b.custom_minimum_size = Vector2(120, 36)
	b.pressed.connect(cb)
	parent.add_child(b)
	return b

func refresh() -> void:
	_list.clear()
	_saves.clear()
	var dir := DirAccess.open(SAVE_ROOT)
	if dir:
		for f in dir.get_files():
			if not f.ends_with(".rts0sav"):
				continue
			var path := SAVE_ROOT.path_join(f)
			var meta := _read_json(path.get_basename() + ".json")
			if meta.is_empty():
				# Saves from before metadata existed still list, by file date.
				meta = {"timestamp": FileAccess.get_modified_time(path), "map_name": "?",
					"date": Time.get_datetime_string_from_unix_time(FileAccess.get_modified_time(path), true)}
			var v := int(meta.get("save_version", 0))
			_saves.append({"path": path, "meta": meta, "compatible": v <= RtsoMain.get_save_version()})
	_saves.sort_custom(func(a, b): return int(a.meta.get("timestamp", 0)) > int(b.meta.get("timestamp", 0)))
	if mode == "save":
		_list.add_item("[New saved game]")
	for s in _saves:
		var m: Dictionary = s.meta
		var desc: String = m.get("description", "")
		if desc == "":
			desc = s.path.get_file().get_basename()
		_list.add_item("%s   %s   %s" % [str(m.get("date", "")).replace("T", " "), m.get("map_name", "?"), desc])
		if not s.compatible:
			_list.set_item_custom_fg_color(_list.item_count - 1, Color(0.85, 0.4, 0.35))
	if _list.item_count > 0:
		_list.select(0)
		_on_selected(0)
	else:
		_list.add_item("No saved games.")
		_list.set_item_disabled(0, true)
		_details.text = ""
	_update_buttons()

func _selected_save() -> Dictionary:
	var sel := _list.get_selected_items()
	if sel.is_empty():
		return {}
	var i: int = sel[0] - (1 if mode == "save" else 0)
	return _saves[i] if i >= 0 and i < _saves.size() else {}

func _update_buttons() -> void:
	var s := _selected_save()
	_btn_delete.disabled = s.is_empty()
	_btn_ok.disabled = mode == "load" and (s.is_empty() or not s.compatible)

func _on_selected(_i: int) -> void:
	_update_buttons()
	var s := _selected_save()
	_thumb.texture = null
	if s.is_empty():
		_details.text = "Saves the match in a new slot." if mode == "save" else ""
		return
	var png: String = s.path.get_basename() + ".png"
	if FileAccess.file_exists(png):
		var img := Image.load_from_file(ProjectSettings.globalize_path(png))
		if img:
			_thumb.texture = ImageTexture.create_from_image(img)
	var m: Dictionary = s.meta
	var t := "[b]%s[/b]\n" % m.get("map_name", "?")
	t += "Saved: %s\n" % str(m.get("date", "")).replace("T", " ")
	t += "Game time: %s\n" % _clock(float(m.get("game_time", 0.0)))
	if m.get("description", "") != "":
		t += "Description: %s\n" % m.description
	t += "\n[b]Players[/b]\n"
	for p in m.get("players", []):
		t += "%s - %s, team %d%s\n" % [p.get("name", "?"), p.get("civ_name", "?"), int(p.get("team", 0)),
			" (AI)" if p.get("ai", false) else ""]
	if not s.compatible:
		t += "\n[color=#e07060]Saved by a newer RTS0 (save format v%d; this build reads up to v%d).[/color]\n" % [int(m.get("save_version", 0)), RtsoMain.get_save_version()]
	_details.text = t
	if _desc and m.get("description", "") != "":
		_desc.text = m.description

func _on_ok() -> void:
	var s := _selected_save()
	if mode == "save":
		if not s.is_empty():
			_ask("Overwrite this saved game?", "overwrite")
			return
		_save_to(_new_name())
	else:
		if s.is_empty() or not s.compatible:
			return
		_load(s.path)

func _on_delete() -> void:
	if not _selected_save().is_empty():
		_ask("Delete this saved game? This cannot be undone.", "delete")

func _ask(text: String, action: String) -> void:
	_pending = action
	_confirm.dialog_text = text
	_confirm.popup_centered()

func _on_confirmed() -> void:
	var s := _selected_save()
	if s.is_empty():
		return
	if _pending == "delete":
		DirAccess.remove_absolute(ProjectSettings.globalize_path(s.path))
		DirAccess.remove_absolute(ProjectSettings.globalize_path(s.path.get_basename() + ".json"))
		DirAccess.remove_absolute(ProjectSettings.globalize_path(s.path.get_basename() + ".png"))
		refresh()
	elif _pending == "overwrite":
		_save_to(s.path.get_file().get_basename())

func _new_name() -> String:
	return "savegame_" + Time.get_datetime_string_from_system(false, false).replace(":", "").replace("-", "").replace("T", "_")

func _save_to(name: String) -> void:
	if game == null or not game.has_method("save_game_as"):
		_status.text = "No match to save."
		return
	if game.has_method("is_replay_playback") and game.is_replay_playback():
		_status.text = "A replay can't be saved."
		return
	var desc: String = _desc.text.strip_edges() if _desc else ""
	if game.save_game_as(name, desc):
		close()
	else:
		_status.text = "Saving failed."

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

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and event.keycode == KEY_ESCAPE:
		get_viewport().set_input_as_handled()
		close()

func _read_json(path: String) -> Dictionary:
	if not FileAccess.file_exists(path):
		return {}
	var v = JSON.parse_string(FileAccess.get_file_as_string(path))
	return v if v is Dictionary else {}

func _clock(seconds: float) -> String:
	var s := int(seconds)
	return "%02d:%02d:%02d" % [s / 3600, (s / 60) % 60, s % 60]
