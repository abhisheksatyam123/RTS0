extends Control
# Replay menu, after 0 A.D. gui/replaymenu (replay_menu.js, replay_actions.js,
# replay_filters.js): every recorded match, newest first, with its date, map,
# players and length; Watch replays it, Summary shows its end-of-game
# statistics, Delete removes it. Filters narrow the list by map, number of
# players and length, and replays this build can't play back faithfully (an
# older save format) are hidden unless asked for, and can't be watched.
# A replay is a folder user://replays/<id>/ holding commands.rts0rec (the turns)
# and metadata.json (setup, players, summary), written by RtsoMain; a game
# continued from a save also keeps the save it started from (start.rts0sav).

const REPLAY_ROOT := "user://replays"

@onready var replay_list: ItemList = $Center/Panel/VBox/ReplayList

var _all: Array = [] # every replay found: {dir, meta, compatible, why}
var _replays: Array = [] # the ones the filters show, in list order
var _filter_map: OptionButton
var _filter_players: OptionButton
var _filter_length: OptionButton
var _filter_compatible: CheckBox
var _details: RichTextLabel
var _btn_summary: Button
var _btn_delete: Button
var _confirm: ConfirmationDialog

func _ready() -> void:
	$Center/Panel.custom_minimum_size = Vector2(960, 560)
	$Center/Panel/VBox/Header/Title.text = "Replays"
	# List on the left, the selected replay's details on the right.
	var vbox: VBoxContainer = $Center/Panel/VBox
	var row := HBoxContainer.new()
	row.size_flags_vertical = Control.SIZE_EXPAND_FILL
	row.add_theme_constant_override("separation", 12)
	vbox.add_child(row)
	vbox.move_child(row, replay_list.get_index())
	replay_list.reparent(row)
	replay_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	replay_list.size_flags_stretch_ratio = 1.6
	replay_list.item_selected.connect(_on_selected)
	replay_list.item_activated.connect(func(_i): _on_play_pressed())
	_build_filters(vbox)
	_details = RichTextLabel.new()
	_details.bbcode_enabled = true
	_details.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_details.size_flags_vertical = Control.SIZE_EXPAND_FILL
	row.add_child(_details)
	var footer: HBoxContainer = $Center/Panel/VBox/Footer
	_btn_delete = Button.new()
	_btn_delete.text = "Delete"
	_btn_delete.custom_minimum_size = Vector2(120, 42)
	_btn_delete.pressed.connect(_on_delete_pressed)
	footer.add_child(_btn_delete)
	footer.move_child(_btn_delete, 0)
	_btn_summary = Button.new()
	_btn_summary.text = "Summary"
	_btn_summary.custom_minimum_size = Vector2(120, 42)
	_btn_summary.pressed.connect(_on_summary_pressed)
	footer.add_child(_btn_summary)
	footer.move_child(_btn_summary, 1)
	_confirm = ConfirmationDialog.new()
	_confirm.dialog_text = "Delete this replay? This cannot be undone."
	_confirm.confirmed.connect(_delete_selected)
	add_child(_confirm)
	load_replays()

func _build_filters(vbox: VBoxContainer) -> void:
	var bar := HBoxContainer.new()
	bar.add_theme_constant_override("separation", 10)
	vbox.add_child(bar)
	vbox.move_child(bar, replay_list.get_parent().get_index())
	_filter_map = OptionButton.new()
	_filter_map.custom_minimum_size = Vector2(220, 0)
	_filter_map.item_selected.connect(func(_i): _apply_filters())
	bar.add_child(_filter_map)
	_filter_players = OptionButton.new()
	for t in ["Any number of players", "2 players", "3 players", "4 players", "5 or more players"]:
		_filter_players.add_item(t)
	_filter_players.item_selected.connect(func(_i): _apply_filters())
	bar.add_child(_filter_players)
	_filter_length = OptionButton.new()
	for t in ["Any length", "Under 10 minutes", "10 to 30 minutes", "Over 30 minutes"]:
		_filter_length.add_item(t)
	_filter_length.item_selected.connect(func(_i): _apply_filters())
	bar.add_child(_filter_length)
	_filter_compatible = CheckBox.new()
	_filter_compatible.text = "Compatible only"
	_filter_compatible.button_pressed = true
	_filter_compatible.toggled.connect(func(_on): _apply_filters())
	bar.add_child(_filter_compatible)

# 0 A.D. replay_menu.js isReplayCompatible: same engine and mods. Here: a replay
# format this build reads and the save format it was recorded with, since any
# change to the simulation's saved state changes how the game plays out.
func _compatibility(meta: Dictionary) -> Array:
	var fmt := int(meta.get("format", 1))
	if fmt > RtsoMain.get_replay_format():
		return [false, "Recorded by a newer RTS0 (replay format %d)." % fmt]
	var v := int(meta.get("save_version", 0))
	if v != RtsoMain.get_save_version():
		return [false, "Recorded with save format v%d; this build is v%d, so it would go out of sync." % [v, RtsoMain.get_save_version()]]
	return [true, ""]

func load_replays() -> void:
	_all.clear()
	var dir := DirAccess.open(REPLAY_ROOT)
	if dir:
		for name in dir.get_directories():
			var path := REPLAY_ROOT.path_join(name)
			if not FileAccess.file_exists(path.path_join("commands.rts0rec")):
				continue
			var meta := _read_json(path.path_join("metadata.json"))
			if meta.is_empty():
				continue
			var c := _compatibility(meta)
			_all.append({"dir": path, "meta": meta, "compatible": c[0], "why": c[1]})
	_all.sort_custom(func(a, b): return int(a.meta.get("timestamp", 0)) > int(b.meta.get("timestamp", 0)))
	# The map filter lists the maps that have replays.
	var keep: String = _filter_map.get_item_text(_filter_map.selected) if _filter_map.item_count > 0 else ""
	_filter_map.clear()
	_filter_map.add_item("Any map")
	var maps := []
	for r in _all:
		var m: String = r.meta.get("map_name", r.meta.get("map_id", "?"))
		if not maps.has(m):
			maps.append(m)
	maps.sort()
	for m in maps:
		_filter_map.add_item(m)
		if m == keep:
			_filter_map.select(_filter_map.item_count - 1)
	_apply_filters()

func _passes(r: Dictionary) -> bool:
	var m: Dictionary = r.meta
	if _filter_compatible.button_pressed and not r.compatible:
		return false
	if _filter_map.selected > 0 and m.get("map_name", m.get("map_id", "?")) != _filter_map.get_item_text(_filter_map.selected):
		return false
	var n: int = m.get("players", []).size()
	match _filter_players.selected:
		1, 2, 3:
			if n != _filter_players.selected + 1:
				return false
		4:
			if n < 5:
				return false
	var minutes := (float(m.get("start_time", 0.0)) + float(m.get("duration", 0.0))) / 60.0
	match _filter_length.selected:
		1:
			if minutes >= 10.0:
				return false
		2:
			if minutes < 10.0 or minutes > 30.0:
				return false
		3:
			if minutes <= 30.0:
				return false
	return true

func _apply_filters() -> void:
	replay_list.clear()
	_replays = _all.filter(_passes)
	for r in _replays:
		var m: Dictionary = r.meta
		replay_list.add_item("%s   %s   %s   %s" % [str(m.get("date", "")).replace("T", " "),
			m.get("map_name", m.get("map_id", "?")), _clock(float(m.get("start_time", 0.0)) + float(m.get("duration", 0.0))), _player_line(m)])
		if not r.compatible:
			replay_list.set_item_custom_fg_color(replay_list.item_count - 1, Color(0.85, 0.4, 0.35))
	if _replays.is_empty():
		replay_list.add_item("No replays yet. Every match you play is recorded here." if _all.is_empty() else "No replays match these filters.")
		replay_list.set_item_disabled(0, true)
	_update_buttons()
	_details.text = ""

func _player_line(m: Dictionary) -> String:
	var parts := []
	for p in m.get("players", []):
		parts.append("%s (%s)" % [p.get("civ_name", "?"), "AI" if p.get("ai", false) else "human"])
	return " vs ".join(parts)

func _selected() -> Dictionary:
	var sel := replay_list.get_selected_items()
	if sel.is_empty() or sel[0] >= _replays.size():
		return {}
	return _replays[sel[0]]

func _update_buttons() -> void:
	var r := _selected()
	$Center/Panel/VBox/Footer/BtnPlay.disabled = r.is_empty() or not r.compatible
	_btn_delete.disabled = r.is_empty()
	_btn_summary.disabled = r.is_empty() or not r.meta.has("summary")

func _on_selected(_i: int) -> void:
	_update_buttons()
	var r := _selected()
	if r.is_empty():
		return
	var m: Dictionary = r.meta
	var t := "[b]%s[/b]\n" % m.get("map_name", m.get("map_id", ""))
	t += "Date: %s\n" % str(m.get("date", "")).replace("T", " ")
	if m.has("start_save"):
		t += "Continued from a saved game at %s\n" % _clock(float(m.get("start_time", 0.0)))
	t += "Length: %s (%d turns)\n" % [_clock(float(m.get("duration", 0.0))), int(m.get("turns", 0))]
	t += "Map size: %d   Seed: %d\n" % [int(m.get("map_size", 0)), int(m.get("seed", 0))]
	t += "Game: %s\n\n[b]Players[/b]\n" % ("multiplayer" if m.get("kind", "sp") == "mp" else "single player")
	for p in m.get("players", []):
		var state := "won" if p.get("winner", false) else ("defeated" if p.get("defeated", false) else "")
		t += "%s - %s, team %d%s %s\n" % [p.get("name", "?"), p.get("civ_name", "?"), int(p.get("team", 0)),
			" (AI)" if p.get("ai", false) else "", state]
	if not r.compatible:
		t += "\n[color=#e07060]%s[/color]\n" % r.why
	if not m.get("finished", false):
		t += "\n[i]Recorded until the player left the match.[/i]"
	_details.text = t

func _on_back_pressed() -> void:
	MatchSettings.go("res://scenes/menu/main_menu.tscn")

func _on_play_pressed() -> void:
	var r := _selected()
	if r.is_empty() or not r.compatible:
		return
	MatchSettings.replay_dir = r.dir
	MatchSettings.load_save_path = ""
	MatchSettings.go("res://scenes/main.tscn")

func _on_summary_pressed() -> void:
	var r := _selected()
	if r.is_empty() or not r.meta.has("summary"):
		return
	var s = load("res://scenes/session/summary_screen.tscn").instantiate()
	add_child(s)
	s.configure({"isReplay": true, "replayDirectory": r.dir})
	s.close_only()
	var summary: Dictionary = r.meta.summary
	s.show_summary(summary.get("victory", false), summary, float(r.meta.get("duration", 0.0)))

func _on_delete_pressed() -> void:
	if not _selected().is_empty():
		_confirm.popup_centered()

func _delete_selected() -> void:
	var r := _selected()
	if r.is_empty():
		return
	var dir := DirAccess.open(r.dir)
	if dir:
		for f in dir.get_files():
			dir.remove(f)
	DirAccess.remove_absolute(ProjectSettings.globalize_path(r.dir))
	load_replays()

func _read_json(path: String) -> Dictionary:
	if not FileAccess.file_exists(path):
		return {}
	var v = JSON.parse_string(FileAccess.get_file_as_string(path))
	return v if v is Dictionary else {}

func _clock(seconds: float) -> String:
	var s := int(seconds)
	return "%02d:%02d" % [s / 60, s % 60]
