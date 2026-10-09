extends CanvasLayer
# Match summary, after 0 A.D. gui/summary (summary.js, counters.js, layout.js):
# one row per player on each panel (Score, Buildings, Units, Resources, Market,
# Miscellaneous) and a Charts panel drawn from the 30-second statistics rows.
# The data comes from RtsoMain.get_match_statistics() (StatisticsTracker port).

@onready var title_label: Label = $CenterContainer/Panel/VBox/Title
@onready var score_label: Label = $CenterContainer/Panel/VBox/Score
@onready var stats_text: Label = $CenterContainer/Panel/VBox/Stats

# 0 A.D. default player colours (simulation/data/settings/player_defaults.json).
const PLAYER_COLORS := [
	Color8(46, 46, 200), Color8(150, 20, 20), Color8(50, 165, 5), Color8(230, 230, 75),
	Color8(50, 170, 170), Color8(160, 80, 200), Color8(235, 120, 20), Color8(64, 64, 64),
]
const UNIT_CLASSES := [["total", "Total"], ["Infantry", "Infantry"], ["Worker", "Worker"], ["Cavalry", "Cavalry"],
	["Champion", "Champion"], ["Hero", "Heroes"], ["Siege", "Siege"], ["Ship", "Navy"], ["Trader", "Traders"]]
const BUILDING_CLASSES := [["total", "Total"], ["House", "Houses"], ["Economic", "Economic"], ["Outpost", "Outposts"],
	["Military", "Military"], ["Fortress", "Fortresses"], ["CivCentre", "Civ Centers"], ["Wonder", "Wonders"]]
const RESOURCES := [["food", "Food"], ["wood", "Wood"], ["stone", "Stone"], ["metal", "Metal"]]
const CHART_METRICS := [["totalScore", "Total score"], ["economyScore", "Economy score"],
	["militaryScore", "Military score"], ["explorationScore", "Exploration score"],
	["resourcesGathered", "Resources gathered"], ["unitsTrained", "Units trained"],
	["enemyUnitsKilled", "Enemy units killed"], ["unitsLost", "Units lost"],
	["buildingsConstructed", "Buildings constructed"], ["percentMapControlled", "Map control (%)"],
	["percentMapExplored", "Map exploration (%)"], ["tradeIncome", "Trade income"]]

var _summary: Dictionary = {}
var _tabs: TabContainer
var _chart: Control
var _chart_metric := 0

func _ready() -> void:
	visible = false
	process_mode = Node.PROCESS_MODE_ALWAYS

func show_summary(is_victory: bool, record: Dictionary, elapsed_time: float) -> void:
	visible = true
	_summary = record
	var players: Array = record.get("players", [])
	if is_victory:
		title_label.text = "VICTORY"
		title_label.add_theme_color_override("font_color", Color(0.3, 1.0, 0.5))
	else:
		title_label.text = "DEFEAT" if not players.is_empty() else "MATCH OVER"
		title_label.add_theme_color_override("font_color", Color(1.0, 0.3, 0.3))
	var game_time: float = record.get("game_time", elapsed_time)
	score_label.text = "%s  |  Duration %s" % [record.get("map_name", ""), _clock(game_time)]
	if players.is_empty():
		_show_legacy(record)
		return
	stats_text.visible = false
	var panel: Control = $CenterContainer/Panel
	panel.custom_minimum_size = Vector2(980, 560)
	if _tabs:
		_tabs.queue_free()
	_tabs = TabContainer.new()
	_tabs.custom_minimum_size = Vector2(940, 420)
	_tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	var vbox: VBoxContainer = $CenterContainer/Panel/VBox
	vbox.add_child(_tabs)
	vbox.move_child(_tabs, stats_text.get_index() + 1)
	_add_panel("Score", ["Economy score", "Military score", "Exploration score", "Total score"],
		func(p): return [p.economyScore, p.militaryScore, p.explorationScore, p.totalScore])
	_add_panel("Buildings", _labels(BUILDING_CLASSES), _building_cells,
		"constructed / lost / destroyed / captured")
	_add_panel("Units", _labels(UNIT_CLASSES), _unit_cells, "trained / lost / killed")
	_add_panel("Resources", _labels(RESOURCES) + ["Total", "Tributes", "Treasures", "Loot"], _resource_cells,
		"gathered / used; tributes sent / received")
	_add_panel("Market", _labels(RESOURCES) + ["Barter efficiency", "Trade income"], _market_cells,
		"sold / bought")
	_add_panel("Miscellaneous", ["Kill / death ratio", "Map control (peak)", "Map control (now)",
		"Map exploration", "Vegetarian ratio", "Feminization"], _misc_cells)
	_add_charts()

func _show_legacy(record: Dictionary) -> void:
	stats_text.visible = true
	stats_text.text = "Enemies killed: %d   Units lost: %d   Units trained: %d\nFood %d  Wood %d  Metal %d  Stone %d\nStructures built: %d" % [
		record.get("enemies_killed", 0), record.get("units_lost", 0), record.get("units_trained", 0),
		record.get("total_food", 0), record.get("total_wood", 0), record.get("total_gold", 0),
		record.get("total_stone", 0), record.get("buildings_constructed", 0)]

func _labels(pairs: Array) -> Array:
	var out := []
	for p in pairs:
		out.append(p[1])
	return out

func _add_panel(title: String, headers: Array, cells: Callable, caption: String = "") -> void:
	var scroll := ScrollContainer.new()
	scroll.name = title
	var box := VBoxContainer.new()
	box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(box)
	if caption != "":
		var cap := Label.new()
		cap.text = caption
		cap.add_theme_font_size_override("font_size", 12)
		cap.add_theme_color_override("font_color", Color(0.75, 0.75, 0.75))
		box.add_child(cap)
	var grid := GridContainer.new()
	grid.columns = headers.size() + 1
	grid.add_theme_constant_override("h_separation", 18)
	grid.add_theme_constant_override("v_separation", 6)
	box.add_child(grid)
	_cell(grid, "Player", Color(0.9, 0.75, 0.31))
	for h in headers:
		_cell(grid, str(h), Color(0.9, 0.75, 0.31))
	var players: Array = _summary.get("players", [])
	for p in players:
		_player_cell(grid, p)
		for v in cells.call(p):
			_cell(grid, _fmt(v), Color.WHITE)
	_tabs.add_child(scroll)

func _player_cell(grid: GridContainer, p: Dictionary) -> void:
	var name_text := "%s  (%s, team %d)" % [p.get("name", "?"), p.get("civ_name", ""), p.get("team", 0)]
	if p.get("winner", false):
		name_text += "  - won"
	elif p.get("defeated", false):
		name_text += "  - defeated"
	_cell(grid, name_text, PLAYER_COLORS[int(p.get("id", 0)) % PLAYER_COLORS.size()].lightened(0.25))

func _cell(grid: GridContainer, text: String, color: Color) -> void:
	var l := Label.new()
	l.text = text
	l.add_theme_font_size_override("font_size", 13)
	l.add_theme_color_override("font_color", color)
	grid.add_child(l)

func _fmt(v) -> String:
	if v is float:
		return "%.1f" % v
	return str(v)

func _building_cells(p: Dictionary) -> Array:
	var s: Dictionary = p.stats
	var out := []
	for c in BUILDING_CLASSES:
		out.append("%d / %d / %d / %d" % [s.buildingsConstructed[c[0]], s.buildingsLost[c[0]],
			s.enemyBuildingsDestroyed[c[0]], s.buildingsCaptured[c[0]]])
	return out

func _unit_cells(p: Dictionary) -> Array:
	var s: Dictionary = p.stats
	var out := []
	for c in UNIT_CLASSES:
		out.append("%d / %d / %d" % [s.unitsTrained[c[0]], s.unitsLost[c[0]], s.enemyUnitsKilled[c[0]]])
	return out

func _resource_cells(p: Dictionary) -> Array:
	var s: Dictionary = p.stats
	var out := []
	var gathered := 0
	var used := 0
	for r in RESOURCES:
		out.append("%d / %d" % [s.resourcesGathered[r[0]], s.resourcesUsed[r[0]]])
		gathered += int(s.resourcesGathered[r[0]])
		used += int(s.resourcesUsed[r[0]])
	out.append("%d / %d" % [gathered, used])
	out.append("%d / %d" % [s.tributesSent, s.tributesReceived])
	out.append(s.treasuresCollected)
	out.append(s.lootCollected)
	return out

func _market_cells(p: Dictionary) -> Array:
	var s: Dictionary = p.stats
	var out := []
	var sold := 0
	var bought := 0
	for r in RESOURCES:
		out.append("%d / %d" % [s.resourcesSold[r[0]], s.resourcesBought[r[0]]])
		sold += int(s.resourcesSold[r[0]])
		bought += int(s.resourcesBought[r[0]])
	# counters.js calculateBarterEfficiency: bought / sold, in percent.
	out.append("%d%%" % int(round(100.0 * bought / sold)) if sold > 0 else "-")
	out.append(s.tradeIncome)
	return out

func _misc_cells(p: Dictionary) -> Array:
	var s: Dictionary = p.stats
	var killed := int(s.enemyUnitsKilled.total) + int(s.enemyBuildingsDestroyed.total)
	var lost := int(s.unitsLost.total) + int(s.buildingsLost.total)
	var kd := "%.2f" % (float(killed) / lost) if lost > 0 else ("-" if killed == 0 else "inf")
	var food := int(s.resourcesGathered.food)
	var veg := "%d%%" % int(round(100.0 * int(s.resourcesGathered.vegetarianFood) / food)) if food > 0 else "-"
	var trained := int(s.unitsTrained.total)
	var fem := "%d%%" % int(round(100.0 * int(s.unitsTrained.FemaleCitizen) / trained)) if trained > 0 else "-"
	return [kd, "%.1f%%" % s.peakPercentMapControlled, "%.1f%%" % p.percentMapControlled,
		"%.1f%%" % p.percentMapExplored, veg, fem]

func _add_charts() -> void:
	var box := VBoxContainer.new()
	box.name = "Charts"
	var pick := OptionButton.new()
	for m in CHART_METRICS:
		pick.add_item(m[1])
	pick.selected = _chart_metric
	pick.item_selected.connect(func(i):
		_chart_metric = i
		_chart.queue_redraw())
	box.add_child(pick)
	_chart = Control.new()
	_chart.custom_minimum_size = Vector2(900, 330)
	_chart.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_chart.draw.connect(_draw_chart)
	box.add_child(_chart)
	_tabs.add_child(box)

func _draw_chart() -> void:
	var samples: Array = _summary.get("samples", [])
	var players: Array = _summary.get("players", [])
	var sz := _chart.size
	var left := 60.0
	var bottom := sz.y - 24.0
	_chart.draw_line(Vector2(left, 8), Vector2(left, bottom), Color(0.6, 0.6, 0.6), 1.0)
	_chart.draw_line(Vector2(left, bottom), Vector2(sz.x - 8, bottom), Color(0.6, 0.6, 0.6), 1.0)
	if samples.is_empty():
		return
	var key: String = CHART_METRICS[_chart_metric][0]
	var max_v := 1.0
	var max_t := 1.0
	for row in samples:
		max_t = max(max_t, float(row.time))
		for c in row.players:
			max_v = max(max_v, float(c.get(key, 0)))
	var font := ThemeDB.fallback_font
	_chart.draw_string(font, Vector2(4, 16), _fmt_axis(max_v), HORIZONTAL_ALIGNMENT_LEFT, -1, 12)
	_chart.draw_string(font, Vector2(4, bottom), "0", HORIZONTAL_ALIGNMENT_LEFT, -1, 12)
	_chart.draw_string(font, Vector2(sz.x - 60, sz.y - 6), _clock(max_t), HORIZONTAL_ALIGNMENT_LEFT, -1, 12)
	for pi in players.size():
		var col: Color = PLAYER_COLORS[int(players[pi].get("id", pi)) % PLAYER_COLORS.size()].lightened(0.2)
		var prev := Vector2(left, bottom)
		for row in samples:
			if pi >= row.players.size():
				continue
			var v := float(row.players[pi].get(key, 0))
			var pt := Vector2(left + (sz.x - left - 8) * float(row.time) / max_t, bottom - (bottom - 8) * v / max_v)
			_chart.draw_line(prev, pt, col, 2.0, true)
			prev = pt
		_chart.draw_string(font, Vector2(left + 8 + pi * 150, sz.y - 6), str(players[pi].get("name", "")),
			HORIZONTAL_ALIGNMENT_LEFT, -1, 12, col)

func _fmt_axis(v: float) -> String:
	return "%d" % int(v) if v >= 10.0 else "%.1f" % v

func _clock(seconds: float) -> String:
	var s := int(seconds)
	return "%02d:%02d" % [s / 60, s % 60]

# Opened from the replay menu's Summary button: one button that closes it.
var _close_only := false
func close_only() -> void:
	_close_only = true
	$CenterContainer/Panel/VBox/HBoxButtons/BtnRestart.visible = false
	$CenterContainer/Panel/VBox/HBoxButtons/BtnMenu.text = "Close"

func _on_restart_pressed() -> void:
	get_tree().paused = false
	get_tree().reload_current_scene()

func _on_menu_pressed() -> void:
	if _close_only:
		queue_free()
		return
	get_tree().paused = false
	MatchSettings.go("res://scenes/menu/main_menu.tscn")
