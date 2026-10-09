extends Control
# Replay menu, after 0 A.D. gui/replaymenu (replay_menu.js, replay_actions.js):
# every recorded match, newest first, with its date, map, players and length;
# Watch replays it, Summary shows its end-of-game statistics, Delete removes it.
# A replay is a folder user://replays/<id>/ holding commands.rts0rec (the turns)
# and metadata.json (setup, players, summary), written by RtsoMain.

const REPLAY_ROOT := "user://replays"

@onready var replay_list: ItemList = $Center/Panel/VBox/ReplayList

var _replays: Array = [] # {dir, meta}
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

func load_replays() -> void:
	replay_list.clear()
	_replays.clear()
	var dir := DirAccess.open(REPLAY_ROOT)
	if dir:
		for name in dir.get_directories():
			var path := REPLAY_ROOT.path_join(name)
			if not FileAccess.file_exists(path.path_join("commands.rts0rec")):
				continue
			var meta := _read_json(path.path_join("metadata.json"))
			if meta.is_empty():
				continue
			_replays.append({"dir": path, "meta": meta})
	_replays.sort_custom(func(a, b): return int(a.meta.get("timestamp", 0)) > int(b.meta.get("timestamp", 0)))
	for r in _replays:
		var m: Dictionary = r.meta
		replay_list.add_item("%s   %s   %s   %s" % [str(m.get("date", "")).replace("T", " "),
			m.get("map_name", m.get("map_id", "?")), _clock(float(m.get("duration", 0.0))), _player_line(m)])
	if _replays.is_empty():
		replay_list.add_item("No replays yet. Every match you play is recorded here.")
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
	$Center/Panel/VBox/Footer/BtnPlay.disabled = r.is_empty()
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
	t += "Length: %s (%d turns)\n" % [_clock(float(m.get("duration", 0.0))), int(m.get("turns", 0))]
	t += "Map size: %d   Seed: %d\n" % [int(m.get("map_size", 0)), int(m.get("seed", 0))]
	t += "Game: %s\n\n[b]Players[/b]\n" % ("multiplayer" if m.get("kind", "sp") == "mp" else "single player")
	for p in m.get("players", []):
		var state := "won" if p.get("winner", false) else ("defeated" if p.get("defeated", false) else "")
		t += "%s - %s, team %d%s %s\n" % [p.get("name", "?"), p.get("civ_name", "?"), int(p.get("team", 0)),
			" (AI)" if p.get("ai", false) else "", state]
	if not m.get("finished", false):
		t += "\n[i]Recorded until the player left the match.[/i]"
	_details.text = t

func _on_back_pressed() -> void:
	MatchSettings.go("res://scenes/menu/main_menu.tscn")

func _on_play_pressed() -> void:
	var r := _selected()
	if r.is_empty():
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
