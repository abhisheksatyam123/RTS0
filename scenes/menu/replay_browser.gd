extends Control
## Replay menu, ported from 0 A.D. gui/replaymenu (replay_menu.js,
## replay_filters.js, replay_actions.js, replay_menu.xml; GPL-2.0-or-later).
## Every recorded match goes in a list with sortable Date / Time, Players, Map
## Name, Size, Population and Duration columns. Filters cover the month, player
## names (Tab completes a name), map, map size, population, duration,
## single-player or multiplayer, victory condition and compatibility. The
## selected replay's map, settings and players show on the right, with its
## outcome behind the Spoiler box. Start Replay (or a double click) watches it,
## Summary opens its summary page, Delete removes it (Delete key; Shift skips
## the confirmation), and Rebuild Cache reads the folder again.
##
## A replay is a folder user://replays/<id>/ holding commands.rts0rec (the
## turns) and metadata.json (setup, players, summary), written by RtsoMain; a
## game continued from a save also keeps the save it started from
## (start.rts0sav).
const MODERN := preload("res://scenes/common/modern_theme.gd")

const REPLAY_ROOT := "user://replays"
const SetupPage = preload("res://scenes/menu/gamesetup.gd")
const SummaryPage = preload("res://scenes/session/summary_screen.gd")
const MSGBOX := preload("res://scenes/common/msgbox.gd")
const L10n := preload("res://scenes/menu/l10n.gd")
const Mods := preload("res://scenes/menu/mods.gd")
const LobbySession := preload("res://scenes/menu/lobby_session.gd")
const GOLD := Color(0.9, 0.75, 0.31)
## compatibilityColor: incompatible replays are greyed out.
const INCOMPATIBLE := Color8(96, 96, 96)
## Column ids and headings (replay_menu.xml), with their widths in percent.
const COLUMNS := [["months", "Date / Time", 12], ["players", "Players", 44], ["mapName", "Map Name", 14],
	["mapSize", "Size", 10], ["popCapacity", "Population", 10], ["duration", "Duration", 10]]
## g_DurationFilterIntervals, in minutes (-1: open).
## PopulationCapacities' types in order (the population sort's secondary key).
## Caps of 10000 or more read "Unlimited".
const POP_TYPES := ["player", "team", "world"]
const UNLIMITED_POP := 10000
const DURATIONS := [[-1, -1], [-1, 15], [15, 30], [30, 45], [45, 60], [60, 90], [90, 120], [120, -1]]
const SINGLEPLAYER := ["Any", "Single-player", "Multiplayer", "Campaigns"]
## initRatedGamesFilter.
const RATED_GAMES := ["Rated and unrated games", "Rated games", "Unrated games"]
## 0 A.D. map_sizes.json names by RTS0 width in meters.
const MAP_SIZES := [[128, "Tiny"], [192, "Small"], [256, "Normal"], [320, "Medium"], [384, "Large"],
	[448, "Very Large"], [512, "Giant"]]

## Every replay found (replay_menu.js loadReplays, sanitized):
## {dir, meta, compatible, why, timestamp, date, month, players, names, map,
##  size, pop, pop_type, duration, victory, multiplayer, map_type, map_id}
var _all: Array = []
## The ones the filters show, in list order.
var _replays: Array = []
var _player_names: Array = []
var _map_names: Array = []
var _pop_caps: Array = []
var sort_column := "months"
var sort_order := -1
var _selected_dir := ""

var replay_list: Tree
var _filename: LineEdit
var _date_filter: OptionButton
var _players_filter: LineEdit
var _map_filter: OptionButton
var _size_filter: OptionButton
var _pop_filter: OptionButton
var _duration_filter: OptionButton
var _compatibility_filter: CheckBox
var _singleplayer_filter: OptionButton
var _victory_filter: OptionButton
var _rated_filter: OptionButton
var _info: Control
var _info_empty: Control
var _info_labels := {}
var _players_text: RichTextLabel
var _spoiler: CheckBox
var _btn_start: Button
var _btn_delete: Button
var _btn_summary: Button
# The open message box (scenes/common/msgbox.gd), null when closed.
var _confirm: Control
var _message: Control
var _hotkeys = null
static var _map_info_cache := {}
## replaySelectionData: what start_replay leaves for the menu that opens after
## the replay (or its summary), which restores it once.
static var selection_data := {}
var _restore := {}

func _ready() -> void:
	if ClassDB.class_exists("RtsoHotkeyConfig"):
		_hotkeys = ClassDB.instantiate("RtsoHotkeyConfig")
	_build()
	_restore = selection_data
	selection_data = {}
	load_replays()

# --- Data ----------------------------------------------------------------------

## isReplayCompatible: 0 A.D. compares the engine version and mods. RTS0
## compares the replay format and the save format the replay was recorded
## with, since any change to the simulation's saved state changes how the game
## plays out.
func _compatibility(meta: Dictionary) -> Array:
	var fmt := int(meta.get("format", 1))
	if fmt > RtsoMain.get_replay_format():
		return [false, "Recorded by a newer RTS0 (replay format %d)." % fmt]
	var v := int(meta.get("save_version", 0))
	if v != RtsoMain.get_save_version():
		return [false, "Recorded with save format v%d; this build is v%d, so it would go out of sync." % [v, RtsoMain.get_save_version()]]
	# AreModsPlayCompatible with the replay's mods (a replay from before replays kept
	# them is checked on its formats alone).
	if meta.get("mods") is Array and not Mods.play_compatible(meta.mods, _active_mods()):
		return [false, "This replay needs a different sequence of mods:\n" + Mods.compared_mods_string(meta.mods, _active_mods()), true]
	return [true, ""]

var _mods_cache = null

## g_EngineInfo.mods, read once per page.
func _active_mods() -> Array:
	if _mods_cache == null:
		_mods_cache = Mods.engine_mods()
	return _mods_cache

## The game-start attributes of a multiplayer replay ("key=value;...").
static func _attribs(game_start: String) -> Dictionary:
	var out := {}
	for kv in game_start.split(";"):
		var i := kv.find("=")
		if i > 0:
			out[kv.substr(0, i)] = kv.substr(i + 1)
	return out

## The map's own description and width (gamesetup.gd's map list).
static func map_info(map_id: String) -> Dictionary:
	if _map_info_cache.has(map_id):
		return _map_info_cache[map_id]
	var info := {"desc": "", "size": 0, "type": "Random"}
	var random := false
	for r in SetupPage.RMS_MAPS:
		if r[0] == map_id:
			info = {"desc": SetupPage.rms_description(r), "size": 0, "type": "Random"}
			random = true
	if not random:
		# Map Type's title (Skirmish or Scenario) from the map's map_types.json entry.
		var type := str(SetupPage._map_types_data().get("maps", {}).get(map_id, {}).get("type", "skirmish"))
		info["type"] = type.capitalize()
		for t in SetupPage.MAP_TYPES:
			if t[0] == type:
				info["type"] = t[1]
		var path := "res://data/maps/%s.json" % map_id
		if FileAccess.file_exists(path):
			var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
			if parsed is Dictionary:
				info["desc"] = str(parsed.get("desc", ""))
				info["size"] = int(float(parsed.get("world_size", 0.0)))
	_map_info_cache[map_id] = info
	return info

## sanitizeInitAttributes plus what the columns and filters read.
func _entry(path: String, meta: Dictionary) -> Dictionary:
	var c := _compatibility(meta)
	var settings: Dictionary = meta.get("settings", {})
	var attribs := _attribs(str(meta.get("game_start", "")))
	var map_id := str(meta.get("map_id", settings.get("map_id", "")))
	var info := map_info(map_id)
	var size := int(meta.get("map_size", 0)) if info["type"] == "Random" else int(info["size"])
	# PopulationCap and PopulationCapType (replay_menu.js sanitizeInitAttributes):
	# settings.pop_cap is negative for a World Population, 0 for unlimited, and
	# a Team Population's cap with settings.team_pop.
	var setting_pop := int(settings.get("pop_cap", 300))
	# teampop= carries it in a multiplayer replay.
	var world := int(attribs.get("worldpop", -setting_pop if setting_pop < 0 else 0))
	var team := int(attribs.get("teampop", setting_pop if bool(settings.get("team_pop", false)) and setting_pop > 0 else 0))
	var pop := int(attribs.get("popcap", maxi(setting_pop, 0)))
	var pop_type := "world" if world > 0 else "team" if team > 0 or bool(settings.get("team_pop", false)) else "player"
	pop = world if world > 0 else team if team > 0 else (UNLIMITED_POP if pop <= 0 else pop)
	var victory := int(attribs.get("victory", settings.get("victory", 0)))
	var timestamp := int(meta.get("timestamp", 0))
	var local := timestamp + int(Time.get_time_zone_from_system().get("bias", 0)) * 60
	var date := Time.get_datetime_string_from_unix_time(local, true)
	var players: Array = meta.get("players", [])
	var humans := players.filter(func(p): return not p.get("ai", false)).size()
	return {"dir": path, "meta": meta, "compatible": c[0], "why": c[1], "mods_only": c.size() > 2, "timestamp": timestamp,
		"date": date.substr(0, 16), "month": date.substr(0, 7), "players": players,
		"names": players.map(func(p): return str(p.get("name", ""))),
		"map": str(meta.get("map_name", map_id)).trim_suffix(" (RMS)"), "map_id": map_id, "map_type": info["type"], "desc": info["desc"],
		"size": size, "pop": pop, "pop_type": pop_type,
		"duration": float(meta.get("start_time", 0.0)) + float(meta.get("duration", 0.0)),
		"victory": victory, "multiplayer": humans > 1, # replay_menu.js isMultiplayer
		# isRated: two humans, two players and RatingEnabled (game_start's rated=1).
		"rated": humans == 2 and players.size() == 2 and attribs.get("rated", "0") == "1",
		"campaign": str(settings.get("campaign_run", "")) != ""}

## loadReplays: read every replay folder, then refill the filters.
func load_replays() -> void:
	_all.clear()
	var dir := DirAccess.open(REPLAY_ROOT)
	if dir:
		for name in dir.get_directories():
			var path := REPLAY_ROOT.path_join(name)
			if not FileAccess.file_exists(path.path_join("commands.rts0rec")):
				continue
			var meta := _read_json(path.path_join("metadata.json"))
			# VisualReplay::LoadReplays: none shorter than minimumReplayDuration (3 s),
			# such as an early out-of-sync's.
			if not meta.is_empty() and not (meta.has("duration") and float(meta["duration"]) < 3.0):
				_all.append(_entry(path, meta))
	_player_names.clear()
	_map_names.clear()
	for r in _all:
		if r["map"] != "" and not _map_names.has(r["map"]):
			_map_names.append(r["map"])
		for p in r["players"]:
			var n := str(p.get("name", ""))
			if not p.get("ai", false) and n != "" and not _player_names.has(n):
				_player_names.append(n)
	_map_names.sort()
	_init_filters()
	if not _restore.is_empty():
		_apply_selection_data(_restore)
		_restore = {}
	display_replay_list()

## createReplaySelectionData.
func create_selection_data(directory: String) -> Dictionary:
	var filters := {"playernames": _players_filter.text, "compatibility": _compatibility_filter.button_pressed}
	for k in _dropdown_filters():
		var dd: OptionButton = _dropdown_filters()[k]
		filters[k] = dd.get_item_text(dd.selected) if dd.selected >= 0 else ""
	# 0.28 keeps the rated filter's index, which initRatedGamesFilter then looks
	# up among its list_data and never finds: it comes back as "Rated and unrated".
	filters["ratedGames"] = _rated_filter.selected
	return {"directory": directory, "column": sort_column, "columnOrder": sort_order, "filters": filters}

func _dropdown_filters() -> Dictionary:
	return {"date": _date_filter, "mapName": _map_filter, "mapSize": _size_filter, "popCap": _pop_filter,
		"duration": _duration_filter, "singleplayer": _singleplayer_filter, "victoryCondition": _victory_filter}

## loadReplays' "Restore user selection" and initFilters(replaySelectionData.filters).
func _apply_selection_data(d: Dictionary) -> void:
	var filters: Dictionary = d.get("filters", {})
	for k in _dropdown_filters():
		var dd: OptionButton = _dropdown_filters()[k]
		for i in dd.item_count:
			if dd.get_item_text(i) == str(filters.get(k, "")):
				dd.select(i)
	_players_filter.text = str(filters.get("playernames", ""))
	_compatibility_filter.set_pressed_no_signal(bool(filters.get("compatibility", _compatibility_filter.button_pressed)))
	_selected_dir = str(d.get("directory", ""))
	sort_column = str(d.get("column", sort_column))
	sort_order = int(d.get("columnOrder", sort_order))

static func map_size_name(size: int) -> String:
	for s in MAP_SIZES:
		if s[0] == size:
			return s[1]
	return "Default"

## The match's victory conditions joined with ", " (0.28 lists every one, "Conquest, Wonder").
static func victory_name(v: int) -> String:
	if v < 0 or v > 255 or (v >= SetupPage.VICTORIES.size() and v < SetupPage.VICTORY_COMBINED):
		return "Unknown"
	# replay_menu.js / SavegameDetails.js: each condition translated, joined with translate(", ");
	# no condition at all (an endless game) leaves the field empty.
	var mask := SetupPage.victory_mask(v)
	var names := PackedStringArray()
	for k in SetupPage.VICTORY_KINDS:
		if mask & int(k[0]):
			names.append(L10n.t(k[1]))
	return L10n.t(", ").join(names)

## translatePopulationCapacity.
static func population_name(pop: int, pop_type: String) -> String:
	return "Unlimited" if pop >= UNLIMITED_POP else "%d (%s)" % [pop, pop_type]

## initPopCapFilter: the caps of every population type (gamesetup.gd's
## POP_CAP_TYPES) merged into one ascending list, unlimited as 10000.
static func population_filter_caps() -> Array:
	var caps := []
	for t in SetupPage.POP_CAP_TYPES:
		for v in t[5]:
			if not caps.has(int(v)):
				caps.append(int(v))
	caps.sort()
	return caps

static func time_to_string(seconds: float) -> String:
	var s := int(seconds)
	if s >= 3600:
		return "%02d:%02d:%02d" % [s / 3600, (s / 60) % 60, s % 60] # l10n.js timeToString "HH:mm:ss"
	return "%02d:%02d" % [s / 60, s % 60]

# --- Filters (replay_filters.js) -------------------------------------------------

func _refill(dd: OptionButton, labels: Array) -> void:
	var keep := dd.get_item_text(dd.selected) if dd.item_count > 0 and dd.selected >= 0 else ""
	dd.clear()
	for l in labels:
		dd.add_item(str(l))
	var i := labels.find(keep)
	dd.select(i if i >= 0 else 0)

func _init_filters() -> void:
	var months := []
	for r in _all:
		if not months.has(r["month"]):
			months.append(r["month"])
	months.sort()
	_refill(_date_filter, ["Any"] + months)
	_refill(_map_filter, ["Any"] + _map_names)
	_refill(_size_filter, ["Any"] + MAP_SIZES.map(func(s): return s[1]))
	_pop_caps = population_filter_caps()
	_refill(_pop_filter, ["Any"] + _pop_caps.map(func(c): return "Unlimited" if c >= UNLIMITED_POP else str(c)))
	var durations := []
	for i in DURATIONS.size():
		var d: Array = DURATIONS[i]
		durations.append("Any" if i == 0 else ("< %d min" % d[1] if d[0] < 0 else ("> %d min" % d[0] if d[1] < 0 else "%d - %d min" % [d[0], d[1]])))
	_refill(_duration_filter, durations)
	if _singleplayer_filter.item_count == 0:
		_refill(_singleplayer_filter, SINGLEPLAYER)
	_refill(_victory_filter, ["Any victory condition"] + SetupPage.VICTORY_KINDS.map(func(k): return k[1]))
	_refill(_rated_filter, RATED_GAMES)

## filterReplay.
func passes(r: Dictionary) -> bool:
	if _compatibility_filter.button_pressed and not r["compatible"]:
		return false
	match _singleplayer_filter.selected:
		1:
			if r["multiplayer"] or r["campaign"]:
				return false
		2:
			if not r["multiplayer"] or r["campaign"]:
				return false
		3:
			if not r["campaign"]:
				return false
	# replay_filters.js: the replay's VictoryConditions include the picked one.
	if _rated_filter.selected == 1 and not r["rated"] or _rated_filter.selected == 2 and r["rated"]:
		return false
	if _victory_filter.selected > 0 and not (SetupPage.victory_mask(r["victory"]) & int(SetupPage.VICTORY_KINDS[_victory_filter.selected - 1][0])):
		return false
	if _date_filter.selected > 0 and r["month"] != _date_filter.get_item_text(_date_filter.selected):
		return false
	var names := " ".join(r["names"]).to_lower()
	for keyword in _players_filter.text.to_lower().split(" ", false):
		if not names.contains(keyword):
			return false
	if _map_filter.selected > 0 and r["map"] != _map_filter.get_item_text(_map_filter.selected):
		return false
	if _size_filter.selected > 0 and r["size"] != MAP_SIZES[_size_filter.selected - 1][0]:
		return false
	if _pop_filter.selected > 0 and r["pop"] != _pop_caps[_pop_filter.selected - 1]:
		return false
	if _duration_filter.selected > 0:
		var d: Array = DURATIONS[_duration_filter.selected]
		if (d[0] > -1 and r["duration"] < d[0] * 60) or (d[1] > -1 and r["duration"] > d[1] * 60):
			return false
	return true

func _sort_key(r: Dictionary) -> Variant:
	match sort_column:
		"duration":
			return r["duration"]
		"players":
			return r["players"].size()
		"mapName":
			return r["map"]
		"mapSize":
			return r["size"]
		"popCapacity":
			return [r["pop"], POP_TYPES.find(r["pop_type"])]
	return r["timestamp"]

## filterReplays + displayReplayList: filter, sort and fill the list, keeping
## the selected replay selected.
func display_replay_list() -> void:
	var sel := _selected()
	if not sel.is_empty():
		_selected_dir = sel["dir"]
	_replays = _all.filter(passes)
	_replays.sort_custom(func(a, b):
		var ka: Variant = _sort_key(a)
		var kb: Variant = _sort_key(b)
		return ka < kb if sort_order > 0 else ka > kb)
	replay_list.clear()
	var root := replay_list.create_item()
	var select: TreeItem = null
	for r in _replays:
		var it := replay_list.create_item(root)
		var cells := [r["date"], ", ".join(r["names"]), r["map"], map_size_name(r["size"]),
			population_name(r["pop"], r["pop_type"]), time_to_string(r["duration"])]
		for c in cells.size():
			it.set_text(c, cells[c])
			it.set_custom_color(c, Color8(172, 172, 212) if c == 0 else Color8(192, 192, 192))
			if not r["compatible"]:
				it.set_custom_color(c, INCOMPATIBLE)
		it.set_metadata(0, r["dir"])
		if r["dir"] == _selected_dir:
			select = it
	for c in COLUMNS.size():
		var arrow := ("  ▲" if sort_order > 0 else "  ▼") if COLUMNS[c][0] == sort_column else ""
		replay_list.set_column_title(c, COLUMNS[c][1] + arrow)
	if select:
		select.select(0)
	display_replay_details()

## The list's index of the selected replay, or -1.
func selected_index() -> int:
	var it := replay_list.get_selected() if replay_list else null
	return it.get_index() if it else -1

func _selected() -> Dictionary:
	var i := selected_index()
	return _replays[i] if i >= 0 and i < _replays.size() else {}

## Selects the replay at `index` in the list (tests and keyboard).
func select_replay(index: int) -> void:
	var root := replay_list.get_root()
	if root and index >= 0 and index < root.get_child_count():
		root.get_child(index).select(0)
	display_replay_details()

func sort_by(column: String) -> void:
	if column == sort_column:
		sort_order = -sort_order
	else:
		sort_column = column
		sort_order = 1
	display_replay_list()

## formatPlayerInfo: a line per player, "Name (Civ)" or "Name (Civ, Medium
## Balanced Petra Bot)", with ", won" or ", defeated" when spoilers are shown,
## under "Team N:" / "No Team:" captions when there is more than one team.
static func player_info(r: Dictionary, spoiler: bool) -> String:
	var all: Array = r["players"]
	var teams := {}
	for p in all:
		var t: int = SummaryPage.setup_team(p, all)
		if not teams.has(t):
			teams[t] = []
		teams[t].append(_player_description(p, spoiler))
	var keys := teams.keys()
	keys.sort()
	if keys.has(-1): # Object.keys order: the teams, then -1
		keys.erase(-1)
		keys.append(-1)
	if keys.size() == 1:
		return "\n".join(teams[keys[0]])
	return "\n\n".join(keys.map(func(t): return "[b]%s[/b]:\n%s" % [ # formatPlayerInfo: only the team name bold
		L10n.t("No Team") if t == -1 else L10n.sprintf(L10n.t("Team %(team)s"), {"team": t + 1}), "\n".join(teams[t])]))

static func _player_description(p: Dictionary, spoiler: bool) -> String:
	var c: Variant = p.get("color", null)
	var color := Color(float(c["r"]), float(c["g"]), float(c["b"])) if c is Dictionary else Color.WHITE
	var state := str(p.get("state", "won" if p.get("winner", false) else ("defeated" if p.get("defeated", false) else "active")))
	var shown_state := spoiler and state != "active" and state != ""
	# formatPlayerInfo: one msgid per combination, so a language can reorder them.
	var fmt := "%(playerName)s (%(civ)s, %(AIdescription)s" if p.get("ai", false) else "%(playerName)s (%(civ)s"
	fmt += ", %(state)s)" if shown_state else ")"
	# g_BuddySymbol: "• " before a lobby buddy's name (its rating split off).
	var name := str(p.get("name", "?"))
	var buddy := "• " if LobbySession.buddies().has(str(LobbySession.split_nick(name)["nick"])) else ""
	return L10n.sprintf(L10n.t(fmt), {
		"playerName": "[color=#%s]%s%s[/color]" % [color.to_html(false), buddy, name.replace("[", "[lb]")],
		"civ": L10n.t(str(p.get("civ_name", "Unknown Civilization"))),
		"AIdescription": SummaryPage.ai_description(p) if p.get("ai", false) else "",
		"state": L10n.t("defeated" if state == "defeated" else "won")})

## displayReplayDetails.
func display_replay_details() -> void:
	var r := _selected()
	var picked := not r.is_empty()
	_info.visible = picked
	_info_empty.visible = not picked
	_btn_start.disabled = not picked
	_btn_delete.disabled = not picked
	_filename.visible = picked
	_btn_summary.visible = picked and r["meta"].has("summary") # HasReplayMetadata
	if not picked:
		return
	_info_labels["name"].text = r["map"]
	# MapCache.getMapPreview: the map's preview, else 0 A.D.'s "no preview" picture.
	var preview: Texture2D = SetupPage.map_preview(r["map_id"]) if r["map_id"] != "" else null
	_info_labels["preview"].texture = preview if preview != null else preload("res://scenes/menu/campaigns.gd").no_preview()
	_info_labels["type"].text = r["map_type"]
	_info_labels["size"].text = map_size_name(r["size"])
	_info_labels["victory"].text = victory_name(r["victory"])
	var desc: String = r["desc"]
	if r["meta"].has("start_save"):
		desc += ("\n\n" if desc != "" else "") + "Continued from a saved game at %s." % time_to_string(float(r["meta"].get("start_time", 0.0)))
	if not r["compatible"]:
		desc += ("\n\n" if desc != "" else "") + "[color=#e07060]%s[/color]" % r["why"]
	_info_labels["desc"].text = desc
	_info_labels["players"].text = "Players: %d" % r["players"].size()
	_players_text.text = player_info(r, _spoiler.button_pressed)
	_filename.text = ProjectSettings.globalize_path(r["dir"])

# --- Actions (replay_actions.js) --------------------------------------------------

func _on_back_pressed() -> void:
	MatchSettings.go("res://scenes/menu/main_menu.tscn")

## startReplay, after the compatibility check.
func start_replay() -> void:
	var r := _selected()
	if r.is_empty():
		return
	if not r["compatible"]:
		_compatibility_error(r)
		return
	selection_data = create_selection_data(r["dir"])
	MatchSettings.replay_dir = r["dir"]
	MatchSettings.load_save_path = ""
	MatchSettings.go("res://scenes/main.tscn")

## displayReplayCompatibilityError.
func _compatibility_error(r: Dictionary) -> void:
	# A replay of this version under other mods names them alone.
	_show_message(r["why"] if r.get("mods_only", false) else "This replay is not compatible with your version of the game!\n" + r["why"],
		"Incompatible replay")

func _show_message(text: String, title: String) -> void:
	_message = MSGBOX.open(self, 500, 200, text, tr(title))
	_message.answered.connect(func(_i): _message = null)

## showReplaySummary.
func show_summary() -> void:
	var r := _selected()
	if r.is_empty():
		return
	if not r["compatible"]:
		_compatibility_error(r)
		return
	if not r["meta"].has("summary"):
		_show_message(tr("No summary data available."), "Error")
		return
	var s = load("res://scenes/session/summary_screen.tscn").instantiate()
	add_child(s)
	s.configure({"isReplay": true, "replayDirectory": r["dir"], "selectionOwner": "replay menu"})
	s.close_only()
	var summary: Dictionary = r["meta"]["summary"]
	s.show_summary(summary.get("victory", false), summary, float(r["meta"].get("duration", 0.0)))

## deleteReplayButtonPressed: Shift (session.savedgames.noconfirmation) skips the question.
func delete_pressed(no_confirmation := false) -> void:
	var r := _selected()
	if r.is_empty():
		return
	if no_confirmation:
		delete_selected()
		return
	_confirm = MSGBOX.open(self, 500, 200, "Are you sure you want to delete this replay permanently?\n%s" % ProjectSettings.globalize_path(r["dir"]),
		tr("Delete replay"), [tr("No"), tr("Yes")])
	_confirm.answered.connect(func(i: int):
		_confirm = null
		if i == 1: delete_selected())

func delete_selected() -> void:
	var r := _selected()
	if r.is_empty():
		return
	var dir := DirAccess.open(r["dir"])
	if dir:
		for f in dir.get_files():
			dir.remove(f)
	DirAccess.remove_absolute(ProjectSettings.globalize_path(r["dir"]))
	var index := selected_index()
	_selected_dir = ""
	load_replays()
	select_replay(mini(index, _replays.size() - 1)) # the replay that took its place

## Tab in the player filter (autoCompleteText): complete the word at the caret, only
## when exactly one player name starts with it. The list re-filters on Enter.
func autocomplete_player() -> void:
	var text := _players_filter.text
	var caret := _players_filter.caret_column
	var completed := try_auto_complete(text.substr(0, caret), _player_names)
	_players_filter.text = completed + text.substr(caret)
	_players_filter.caret_column = caret + completed.length() - caret

## tryAutoComplete: the last word of text, completed to the only name it starts.
static func try_auto_complete(text: String, names: Array) -> String:
	var words := text.split(" ")
	var last := words[words.size() - 1].to_lower()
	if last == "":
		return text
	var matching := []
	for n in names:
		if str(n).to_lower().begins_with(last):
			matching.append(n)
			if matching.size() > 1:
				return text
	if matching.size() != 1:
		return text
	words[words.size() - 1] = str(matching[0])
	return " ".join(words)

# --- Page ------------------------------------------------------------------------------

func _label(text: String, size := 14, color := Color.WHITE) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_color_override("font_color", color)
	return l

## ModernLabelText / ModernLeftLabelText: sans-bold-stroke-14, white.
func _bold_label(text: String) -> Label:
	var l := _label(text, 14)
	l.add_theme_font_override("font", SummaryPage.bold_font())
	l.add_theme_color_override("font_outline_color", Color.BLACK)
	l.add_theme_constant_override("outline_size", 3)
	return l

func _dropdown(name: String, tooltip: String) -> OptionButton:
	var dd := OptionButton.new()
	dd.name = name
	dd.tooltip_text = tooltip
	dd.clip_text = true
	dd.fit_to_longest_item = false
	dd.add_theme_font_size_override("font_size", 13)
	dd.item_selected.connect(func(_i): display_replay_list())
	return dd

func _anchor(c: Control, l: float, t: float, r: float, b: float, ol := 0.0, ot := 0.0, or_ := 0.0, ob := 0.0) -> Control:
	c.anchor_left = l
	c.anchor_top = t
	c.anchor_right = r
	c.anchor_bottom = b
	c.offset_left = ol
	c.offset_top = ot
	c.offset_right = or_
	c.offset_bottom = ob
	return c

func _info_row(parent: VBoxContainer, caption: String, key: String) -> void:
	var row := HBoxContainer.new()
	var cap := _bold_label(caption)
	cap.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	cap.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(cap)
	var val := _label("", 14)
	val.name = key.capitalize().replace(" ", "")
	val.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	val.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	row.add_child(val)
	parent.add_child(row)
	parent.add_child(HSeparator.new())
	_info_labels[key] = val

func _build() -> void:
	theme = MODERN.theme() # 0.28's modern skin
	var title := _bold_label("Replay Games") # TitleText
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	add_child(_anchor(title, 0.5, 0, 0.5, 0, -128, 4, 128, 32))
	# Left panel: filters, list and file name.
	var left := Control.new()
	left.name = "LeftPanel"
	add_child(_anchor(left, 0.03, 0.05, 1, 1, 0, 8, -255, -70))
	var filters := HBoxContainer.new()
	filters.name = "FilterPanel"
	filters.add_theme_constant_override("separation", 5)
	left.add_child(_anchor(filters, 0, 0, 1, 0, 0, 0, -13, 26))
	_date_filter = _dropdown("DateTimeFilter", "")
	_players_filter = LineEdit.new()
	_players_filter.name = "PlayersFilter"
	_players_filter.placeholder_text = "Search for Player"
	_players_filter.tooltip_text = "Filter replays by typing one or more, partial or complete player names. Press [Tab] to autocomplete player names."
	_players_filter.add_theme_font_size_override("font_size", 13)
	_players_filter.text_submitted.connect(func(_t): display_replay_list()) # replay_menu.xml: the Press action
	_players_filter.gui_input.connect(func(e):
		if e is InputEventKey and e.pressed and e.keycode == KEY_TAB:
			_players_filter.accept_event()
			autocomplete_player())
	_map_filter = _dropdown("MapNameFilter", "")
	_size_filter = _dropdown("MapSizeFilter", "")
	_pop_filter = _dropdown("PopulationFilter", "")
	_duration_filter = _dropdown("DurationFilter", "")
	for pair in [[_date_filter, 12], [_players_filter, 44], [_map_filter, 14], [_size_filter, 10], [_pop_filter, 10], [_duration_filter, 10]]:
		pair[0].size_flags_horizontal = Control.SIZE_EXPAND_FILL
		pair[0].size_flags_stretch_ratio = pair[1]
		filters.add_child(pair[0])
	replay_list = Tree.new()
	replay_list.name = "ReplaySelection"
	replay_list.columns = COLUMNS.size()
	replay_list.column_titles_visible = true
	replay_list.hide_root = true
	replay_list.select_mode = Tree.SELECT_ROW
	for c in COLUMNS.size():
		replay_list.set_column_expand(c, true)
		replay_list.set_column_expand_ratio(c, COLUMNS[c][2])
		replay_list.set_column_clip_content(c, true)
	var picked := StyleBoxFlat.new() # keeps the row's text readable when selected
	picked.bg_color = Color(0.55, 0.42, 0.15, 0.55)
	replay_list.add_theme_stylebox_override("selected", picked)
	replay_list.add_theme_stylebox_override("selected_focus", picked)
	replay_list.column_title_clicked.connect(func(c, _b): sort_by(COLUMNS[c][0]))
	replay_list.item_selected.connect(display_replay_details)
	replay_list.item_activated.connect(start_replay)
	left.add_child(_anchor(replay_list, 0, 0, 1, 1, 0, 35, 0, -30))
	_filename = LineEdit.new()
	_filename.name = "ReplayFilename"
	_filename.editable = false
	_filename.tooltip_text = "File path for the currently selected replay."
	_filename.add_theme_color_override("font_uneditable_color", Color8(208, 174, 132))
	_filename.add_theme_font_size_override("font_size", 13)
	left.add_child(_anchor(_filename, 0, 1, 1, 1, 0, -26, 0, 0))
	# Right panel: compatibility, game-type and victory filters, then details.
	var right := Control.new()
	right.name = "RightPanel"
	add_child(_anchor(right, 1, 0, 1, 1, -250, 48, -20, -70)) # rightPanel 100%-250 48; details end 100%-60 of 100%-10
	_compatibility_filter = CheckBox.new()
	_compatibility_filter.name = "CompatibilityFilter"
	_compatibility_filter.text = "Filter compatible replays"
	_compatibility_filter.button_pressed = true
	_compatibility_filter.flat = true
	_compatibility_filter.toggled.connect(func(_on): display_replay_list())
	right.add_child(_anchor(_compatibility_filter, 0, 0, 1, 0, 0, 12, 0, 38)) # caption 20 0 100% 50, centred
	_singleplayer_filter = _dropdown("SingleplayerFilter", "")
	right.add_child(_anchor(_singleplayer_filter, 0, 0, 1, 0, 0, 55, 0, 80))
	_victory_filter = _dropdown("VictoryConditionFilter", "")
	right.add_child(_anchor(_victory_filter, 0, 0, 1, 0, 0, 90, 0, 115))
	_rated_filter = _dropdown("RatedGamesFilter", "")
	right.add_child(_anchor(_rated_filter, 0, 0, 1, 0, 0, 125, 0, 150))
	var frame := StyleBoxFlat.new()
	frame.bg_color = Color(0, 0, 0, 0.35)
	frame.border_color = GOLD.darkened(0.3)
	frame.set_border_width_all(1)
	_info_empty = Panel.new()
	_info_empty.name = "ReplayInfoEmpty"
	_info_empty.add_theme_stylebox_override("panel", frame)
	var logo := TextureRect.new() # replayInfoEmpty's logo "50%-110 40 50%+110 140"
	logo.name = "Logo"
	logo.texture = load("res://assets/textures/ui/pregame/logo/0ad_logo.png")
	logo.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	logo.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	_info_empty.add_child(_anchor(logo, 0.5, 0, 0.5, 0, -110, 40, 110, 140))
	var subject := Panel.new() # subjectBox "3% 180 97% 99%": ModernDarkBoxWhite, its text left empty
	subject.name = "SubjectBox"
	var white := StyleBoxFlat.new()
	white.bg_color = Color(0, 0, 0, 0.35)
	white.border_color = Color(1, 1, 1, 0.8)
	white.set_border_width_all(1)
	subject.add_theme_stylebox_override("panel", white)
	_info_empty.add_child(_anchor(subject, 0.03, 0, 0.97, 0.99, 0, 180, 0, 0))
	right.add_child(_anchor(_info_empty, 0, 0, 1, 1, 0, 160, 0, 0))
	_info = PanelContainer.new()
	_info.name = "ReplayInfo"
	_info.add_theme_stylebox_override("panel", frame)
	right.add_child(_anchor(_info, 0, 0, 1, 1, 0, 160, 0, 0))
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 3)
	_info.add_child(box)
	var map_name := _bold_label("")
	map_name.name = "SgMapName"
	map_name.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	map_name.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	box.add_child(map_name)
	_info_labels["name"] = map_name
	# sgMapPreview: the map's 0 A.D. preview image (4:3).
	var preview := TextureRect.new()
	preview.name = "SgMapPreview"
	preview.custom_minimum_size = Vector2(0, 165)
	preview.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	preview.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	box.add_child(preview)
	_info_labels["preview"] = preview
	box.add_child(HSeparator.new())
	_info_row(box, "Map Type:", "type")
	_info_row(box, "Map Size:", "size")
	_info_row(box, "Victory:", "victory")
	var desc := RichTextLabel.new()
	desc.name = "SgMapDescription"
	desc.bbcode_enabled = true
	desc.custom_minimum_size = Vector2(0, 80)
	desc.size_flags_vertical = Control.SIZE_EXPAND_FILL
	desc.add_theme_font_size_override("normal_font_size", 12)
	box.add_child(desc)
	_info_labels["desc"] = desc
	var count := _bold_label("")
	count.name = "SgNbPlayers"
	count.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	box.add_child(count)
	_info_labels["players"] = count
	_players_text = RichTextLabel.new()
	_players_text.name = "SgPlayersNames"
	_players_text.bbcode_enabled = true
	_players_text.custom_minimum_size = Vector2(0, 90)
	_players_text.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_players_text.add_theme_font_size_override("normal_font_size", 13)
	_players_text.add_theme_font_size_override("bold_font_size", 13)
	box.add_child(_players_text)
	_spoiler = CheckBox.new()
	_spoiler.name = "ShowSpoiler"
	_spoiler.text = "Spoiler"
	_spoiler.flat = true
	_spoiler.toggled.connect(func(_on): display_replay_details())
	box.add_child(_spoiler)
	# Bottom panel: buttons.
	var bottom := Control.new() # each button in its own fifth, "0 0 19% 100%" .. "80% 0 99% 100%"
	bottom.name = "BottomPanel"
	add_child(_anchor(bottom, 0, 1, 1, 1, 25, -60, -25, -22))
	var buttons := [["Main Menu", _on_back_pressed, ""], ["Delete", delete_pressed, _delete_tooltip()],
		["Rebuild Cache", load_replays, "Rebuild the replay cache from scratch. Potentially slow!"],
		["Summary", show_summary, ""], ["Start Replay", start_replay, ""]]
	for i in buttons.size():
		var b: Array = buttons[i]
		var btn := Button.new()
		btn.name = str(b[0]).replace(" ", "") + "Button"
		btn.text = b[0]
		btn.tooltip_text = b[2]
		btn.pressed.connect(func(): b[1].call())
		bottom.add_child(_anchor(btn, i * 0.2, 0, i * 0.2 + 0.19, 1))
	_btn_delete = bottom.get_node("DeleteButton")
	_btn_summary = bottom.get_node("SummaryButton")
	_btn_start = bottom.get_node("StartReplayButton")
	_singleplayer_filter.clear()
	_refill(_singleplayer_filter, SINGLEPLAYER)

## initHotkeyTooltips: colorizeHotkey lists every combination bound to
## session.savedgames.delete and .noconfirmation, shortest first.
func _delete_tooltip() -> String:
	var keys: Dictionary = _hotkeys.call("get_hotkey_map") if _hotkeys else {}
	return "Delete the selected replay using %s." % hotkey_text(keys, "session.savedgames.delete") + \
		"\nHold %s to skip the confirmation dialog while deleting." % hotkey_text(keys, "session.savedgames.noconfirmation")

## colorizeHotkey: "[Delete, Backspace]", or "[Unassigned hotkey: name]" when nothing is bound.
static func hotkey_text(keys: Dictionary, hotkey: String) -> String:
	var combos := Array(keys.get(hotkey, PackedStringArray()))
	if combos.is_empty():
		return "[Unassigned hotkey: %s]" % hotkey
	var cfg := RtsoHotkeyConfig.new() # formatHotkeyCombination: the names 0.28 shows
	combos = combos.map(func(c): return cfg.display_combo(str(c)))
	combos.sort_custom(func(a, b): return str(a).length() < str(b).length())
	return "[%s]" % ", ".join(PackedStringArray(combos))

func _hotkey_combos(hotkey: String) -> PackedStringArray:
	return _hotkeys.call("get_hotkey_map").get(hotkey, PackedStringArray()) if _hotkeys else PackedStringArray()

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

func _unhandled_input(event: InputEvent) -> void:
	if not (event is InputEventKey) or not event.pressed or find_child("SummaryScreen", false, false) or _confirm != null or _message != null:
		return
	if _fired_ignoring_modifiers(event, "session.savedgames.delete") and not _players_filter.has_focus():
		get_viewport().set_input_as_handled()
		delete_pressed(_skip_confirmation(event))
	elif _fired(event, "summary"):
		get_viewport().set_input_as_handled()
		show_summary()

## A delete key still deletes while the no-confirmation modifier is held.
func _fired_ignoring_modifiers(event: InputEvent, hotkey: String) -> bool:
	if _hotkeys == null or not (event is InputEventKey) or not event.pressed or event.echo:
		return event is InputEventKey and event.pressed and _hotkeys == null and event.keycode == KEY_DELETE
	var key := str(_hotkeys.call("event_key_name", event)).to_lower()
	for combo in _hotkey_combos(hotkey):
		if str(combo).to_lower() == key:
			return true
	return false

func _read_json(path: String) -> Dictionary:
	if not FileAccess.file_exists(path):
		return {}
	var v = JSON.parse_string(FileAccess.get_file_as_string(path))
	return v if v is Dictionary else {}
