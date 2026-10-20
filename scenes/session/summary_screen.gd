extends CanvasLayer
## Summary page, ported from 0 A.D. gui/summary (summary.js, layout.js,
## counters.js, summary.xml; GPL-2.0-or-later): the Score, Structures, Units,
## Resources, Market and Miscellaneous tabs with one row per player (grouped
## by team, with "Team total" rows, when a team has two or more players), and
## a Charts tab with two charts whose Category / Value / Type pickers chart any
## counter over the match, per player or per team.
##
## The data is RtsoMain.get_match_statistics() (the StatisticsTracker port):
## every player's final counters plus a row of counters per 30 s. It is opened
## at the end of a game or replay, over the running game by the "summary"
## hotkey (Ctrl+Tab), and from the replay menu's Summary button.
const L10n := preload("res://scenes/menu/l10n.gd")

const Ref = preload("res://scenes/reference/reference_common.gd")
# gui/common/sprites.xml iconBubbleGold: global/icon/bubble.png, add_color 237 227 167.
const BUBBLE_ICON := "res://assets/textures/ui/global/icon/bubble.png"
const BUBBLE_GOLD := Color8(237, 227, 167)

## summary.js g_TypeColors.
const TYPE_COLORS := {
	"blue": Color8(196, 198, 255), "green": Color8(201, 255, 200),
	"red": Color8(255, 213, 213), "yellow": Color8(255, 255, 157),
}
## summary.js g_SummaryTypes: how a counter's parts are coloured and joined.
const SUMMARY_TYPES := {
	"percent": {"caption": "%", "postfix": "%"},
	"trained": {"color": "green", "caption": "Trained", "postfix": " / "},
	"constructed": {"color": "green", "caption": "Constructed", "postfix": " / "},
	"gathered": {"color": "green", "caption": "Gathered", "postfix": " / "},
	"count": {"caption": "Count", "hide": true, "postfix": ""},
	"sent": {"color": "green", "caption": "Sent", "postfix": " / "},
	"bought": {"color": "green", "caption": "Bought", "postfix": " / "},
	"income": {"color": "green", "caption": "Income", "postfix": " / "},
	"captured": {"color": "yellow", "caption": "Captured", "postfix": " / "},
	"succeeded": {"color": "green", "caption": "Succeeded", "postfix": " / "},
	"destroyed": {"color": "blue", "caption": "Destroyed", "postfix": "\n"},
	"killed": {"color": "blue", "caption": "Killed", "postfix": "\n"},
	"lost": {"color": "red", "caption": "Lost", "postfix": ""},
	"used": {"color": "red", "caption": "Used", "postfix": ""},
	"received": {"color": "red", "caption": "Received", "postfix": ""},
	"population": {"color": "red", "caption": "Population", "postfix": ""},
	"sold": {"color": "red", "caption": "Sold", "postfix": ""},
	"outcome": {"color": "red", "caption": "Outcome", "postfix": ""},
	"failed": {"color": "red", "caption": "Failed", "postfix": ""},
}
const INFINITY_SYMBOL := "∞"
## g_ResourceData order, with 0 A.D.'s names (RTS0's gold is metal).
const RESOURCES := [["food", "Food"], ["wood", "Wood"], ["stone", "Stone"], ["metal", "Metal"]]
## 0 A.D. player_defaults.json, for records written before players had colours.
const DEFAULT_COLORS := [
	Color8(46, 46, 200), Color8(150, 20, 20), Color8(50, 165, 5), Color8(230, 230, 75),
	Color8(50, 170, 170), Color8(160, 80, 200), Color8(235, 120, 20), Color8(64, 64, 64),
]
const AI_DIFFICULTIES := ["Sandbox", "Very Easy", "Easy", "Medium", "Hard", "Very Hard"]
const AI_BEHAVIORS := ["Random", "Defensive", "Balanced", "Aggressive"]
## gamesetup.gd MAP_SIZES names (0 A.D. map_sizes.json).
const MAP_SIZE_NAMES := {128: "Tiny", 192: "Small", 256: "Normal", 320: "Medium", 384: "Large", 448: "Very Large", 512: "Giant"}
const TAB_W := 118 # g_TabButtonWidth
const TAB_GAP := 6 # g_TabButtonDist
const BOX_H := 40 # g_PlayerBoxYSize
const BOX_GAP := 2 # g_PlayerBoxGap
const LONG_HEADING := 250 # g_LongHeadingWidth
const GOLD := Color(0.9, 0.75, 0.31)

## The selected tab and charts (data.gui.summarySelection): kept by the match's
## in-game Summary and by the replay menu while it is open; any other summary
## (the one after a match, a new match) starts on the Score tab.
const DEFAULT_SELECTION := {"panel": 0, "category": [0, 0], "value": [0, 1], "type": [0, 0], "teamCharts": false}
static var selection := DEFAULT_SELECTION.duplicate(true)
## Which match or menu `selection` belongs to.
static var selection_owner := ""

## How the page was opened (0 A.D. data.gui): isInGame, dialog, isReplay,
## replayDirectory, assignedPlayer, shownPlayers, fromReplayMenu.
var gui: Dictionary = {}
## Set by QuitConfirmation.end_game (session.js endGame) before switching to
## this scene: {gui, data}, the page of the match that was just left.
static var pending: Dictionary = {}
var data: Dictionary = {}
## The players shown: {id, name, civ, team, state, color, tooltip, si, final}.
var players: Array = []
## team -> [player indexes], or empty when teams are not shown.
var teams: Dictionary = {}
var panels: Array = []
var panel_index := 0
var series: Array = [[], []] # per chart: [{points: PackedVector2Array, color}]
var chart_format: Array = ["INTEGER", "INTEGER"]

var _window: Panel
var _tab_buttons: Array = []
var _general: Control
var _charts_panel: Control
var _chart_nodes: Array = []
var _pickers: Array = [] # per chart: [category, value, type, type label]
var _legend: RichTextLabel
var _team_toggle: CheckBox
var _replay_button: Button
var _continue_button: Button
var _paused_by_me := false
var _victory := false
var _hotkeys = null

func _ready() -> void:
	visible = false
	layer = 50
	process_mode = Node.PROCESS_MODE_ALWAYS
	if ClassDB.class_exists("RtsoHotkeyConfig"):
		_hotkeys = ClassDB.instantiate("RtsoHotkeyConfig")
	if not pending.is_empty() and get_parent() == get_tree().root:
		var p := pending
		pending = {}
		configure(p["gui"])
		show_summary(p["gui"].get("victory", false), p["data"], float(p["data"].get("game_time", 0.0)))
		# 0.28's summary keeps the match's music playing until the main menu
		# (summary.js has no music calls; only the main menu sets MENU).
		var ms = get_node_or_null("/root/MatchSettings")
		if ms != null and not ms.match_music.is_empty():
			var mm: Dictionary = ms.match_music
			ms.match_music = {}
			var menu_audio = get_node_or_null("/root/MenuAudio")
			if menu_audio != null:
				menu_audio.play_match_track(str(mm.get("track", "")), float(mm.get("position", 0.0)))

func is_open() -> bool:
	return visible

## Where the page is opened from (call before show_summary).
func configure(g: Dictionary) -> void:
	gui = g.duplicate()
	var owner := str(gui.get("selectionOwner", ""))
	if owner == "" or owner != selection_owner:
		selection = DEFAULT_SELECTION.duplicate(true)
	selection_owner = owner

## Opened from the replay menu's Summary button: Continue goes back to it.
func close_only() -> void:
	gui["fromReplayMenu"] = true

func show_summary(is_victory: bool, record: Dictionary, elapsed_time: float) -> void:
	data = record
	_victory = is_victory
	if not data.has("game_time"):
		data["game_time"] = elapsed_time
	if gui.get("isReplay", false) and str(gui.get("replayDirectory", "")) == "" and not gui.get("isInGame", false):
		gui["replayDirectory"] = MatchSettings.replay_dir
	_init_players()
	_init_teams()
	panels = score_panels()
	_build()
	visible = true
	if gui.get("isInGame", false) and not gui.get("networked", false) and not get_tree().paused:
		_paused_by_me = true # MenuButtons Summary: pauseControl.implicitPause()
		get_tree().paused = true
	select_panel(clampi(int(selection["panel"]), 0, panels.size()))

# --- Data (summary.js initTeamData, counters.js) ------------------------------

func _init_players() -> void:
	players.clear()
	var shown: Array = gui.get("shownPlayers", [])
	var all: Array = data.get("players", [])
	for si in all.size():
		var p: Dictionary = all[si]
		var id := int(p.get("id", si))
		if not shown.is_empty() and not shown.has(id):
			continue
		var c: Variant = p.get("color", null)
		var color: Color = Color(float(c["r"]), float(c["g"]), float(c["b"])) if c is Dictionary \
			else DEFAULT_COLORS[id % DEFAULT_COLORS.size()]
		var state := str(p.get("state", "won" if p.get("winner", false) else ("defeated" if p.get("defeated", false) else "active")))
		var tooltip := ""
		if p.get("ai", false):
			tooltip = ai_description(p)
		players.append({"id": id, "name": str(p.get("name", "Player %d" % (id + 1))), "civ": int(p.get("civ", 0)),
			"team": setup_team(p, all), "state": state, "color": color, "tooltip": tooltip, "si": si, "final": p})

## translateAISettings: "Medium Balanced Petra Bot".
static func ai_description(p: Dictionary) -> String:
	return "%s %s Petra Bot" % [AI_DIFFICULTIES[clampi(int(p.get("ai_difficulty", 3)), 0, 5)],
		AI_BEHAVIORS[clampi(int(p.get("ai_behavior", 2)), 0, 3)]]

## playerState.team: the Game Setup team, -1 for none. Records from before
## setup_team only have RTS0's sim team (its lowest member's id + 1): players
## sharing one form a team, numbered in that order.
static func setup_team(p: Dictionary, all: Array) -> int:
	if p.has("setup_team"):
		return int(p["setup_team"])
	var count := {}
	for q in all:
		var t := int(q.get("team", 0))
		count[t] = int(count.get(t, 0)) + 1
	var shared: Array = count.keys().filter(func(t): return count[t] > 1)
	shared.sort()
	return shared.find(int(p.get("team", 0)))

## initTeamData: teams are shown when teams are locked and one of them has two
## or more players; players without a team (-1) are listed first, untitled.
func _init_teams() -> void:
	teams.clear()
	if not data.get("lock_teams", true): # records from before lock_teams grouped any shared team
		return
	for i in players.size():
		var t: int = players[i]["team"]
		if not teams.has(t):
			teams[t] = []
		teams[t].append(i)
	if teams.keys().all(func(t): return t < 0 or teams[t].size() < 2):
		teams.clear()

func sample_count() -> int:
	return maxi(1, data.get("samples", []).size())

func sample_time(index: int) -> float:
	var rows: Array = data.get("samples", [])
	return float(rows[index]["time"]) if index < rows.size() else float(data.get("game_time", 0.0))

## A player's row at `index`; records written before rows carried every
## counter only have the final counters.
func _row(p: Dictionary, index: int) -> Dictionary:
	var rows: Array = data.get("samples", [])
	if index < rows.size():
		var per: Array = rows[index].get("players", [])
		if p["si"] < per.size() and per[p["si"]].has("stats"):
			return per[p["si"]]
		if index < rows.size() - 1:
			return {}
	return p["final"]

func _stats(p: Dictionary, index: int) -> Dictionary:
	return _row(p, index).get("stats", {})

static func _by(stats: Dictionary, key: String, cls: String) -> int:
	var d: Variant = stats.get(key, {})
	if not d is Dictionary:
		return 0
	# The Unit and Structure class counters; only trained units carry "Unit" (livestock
	# included), elsewhere and in older replays they equal the total.
	if (cls == "Unit" or cls == "Structure") and not d.has(cls):
		cls = "total"
	# Replays and saves from before 0.28's Civilian class carry FemaleCitizen in its place.
	if cls == "Civilian" and not d.has("Civilian"):
		cls = "FemaleCitizen"
	return int(d.get(cls, 0))

## RangeManager's percentages are whole numbers in 0 A.D.
func _percent_of(p: Dictionary, index: int, key: String) -> int:
	return int(floor(float(_row(p, index).get(key, 0.0))))

static func calculate_percent(dividend: float, divisor: float) -> Dictionary:
	return {"percent": int(floor(100.0 * dividend / divisor)) if divisor != 0.0 else 0}

static func calculate_ratio(dividend: float, divisor: float) -> Variant:
	if dividend == 0.0:
		return 0
	if divisor == 0.0:
		return INF
	return snappedf(dividend / divisor, 0.01)

## summaryArraySum: numbers add up, objects add up key by key.
static func array_sum(values: Array) -> Variant:
	var sum: Variant = values[0].duplicate() if values[0] is Dictionary else values[0]
	for i in range(1, values.size()):
		if sum is Dictionary:
			for k in sum:
				sum[k] += values[i][k]
		else:
			sum += values[i]
	return sum

func _team_values(team: int, index: int, type: String, counters: Array, headings: Array) -> Array:
	var col := headings.map(func(h): return h["id"]).find(type) - 1
	var fn: Callable = counters[col]["fn"]
	return teams[team].map(func(i): return fn.call(players[i], index, type))

func _team_total(team: int, index: int, getter: Callable) -> float:
	var total := 0.0
	for i in teams[team]:
		total += getter.call(players[i], index)
	return total

func _team_first(team: int) -> Dictionary:
	return players[teams[team][0]]

# Score
func calc_economy_score(p: Dictionary, index: int, _t := "") -> int:
	var s := _stats(p, index)
	var total := 0
	for r in RESOURCES:
		total += _by(s, "resourcesGathered", r[0])
	return roundi((total + int(s.get("tradeIncome", 0))) / 10.0)

func calc_military_score(p: Dictionary, index: int, _t := "") -> int:
	var s := _stats(p, index)
	return roundi((int(s.get("enemyUnitsKilledValue", 0)) + int(s.get("unitsCapturedValue", 0)) +
		int(s.get("enemyBuildingsDestroyedValue", 0)) + int(s.get("buildingsCapturedValue", 0))) / 10.0)

func calc_exploration_score(p: Dictionary, index: int, _t := "") -> int:
	return _percent_of(p, index, "percentMapExplored") * 10

func calc_score_total(p: Dictionary, index: int, _t := "") -> int:
	return calc_economy_score(p, index) + calc_military_score(p, index) + calc_exploration_score(p, index)

func calc_score_team(team: int, index: int, type: String, counters: Array, headings: Array) -> Variant:
	if type == "explorationScore":
		return _percent_of(_team_first(team), index, "teamPercentMapExplored") * 10
	if type == "totalScore":
		var sum := 0
		for t in ["economyScore", "militaryScore", "explorationScore"]:
			sum += int(calc_score_team(team, index, t, counters, headings))
		return sum
	return array_sum(_team_values(team, index, type, counters, headings))

# Structures and units
func calc_buildings(p: Dictionary, index: int, type: String) -> Dictionary:
	var s := _stats(p, index)
	return {"constructed": _by(s, "buildingsConstructed", type), "destroyed": _by(s, "enemyBuildingsDestroyed", type),
		"captured": _by(s, "buildingsCaptured", type), "lost": _by(s, "buildingsLost", type)}

func calc_units(p: Dictionary, index: int, type: String) -> Dictionary:
	var s := _stats(p, index)
	return {"trained": _by(s, "unitsTrained", type), "killed": _by(s, "enemyUnitsKilled", type),
		"lost": _by(s, "unitsLost", type)}

func calc_sum_team(team: int, index: int, type: String, counters: Array, headings: Array) -> Variant:
	return array_sum(_team_values(team, index, type, counters, headings))

# Resources
func calc_resources(p: Dictionary, index: int, type: String) -> Dictionary:
	var s := _stats(p, index)
	return {"count": _by(s, "resourcesCount", type), "gathered": _by(s, "resourcesGathered", type),
		"used": _by(s, "resourcesUsed", type) - _by(s, "resourcesSold", type)}

func calc_total_resources(p: Dictionary, index: int, _t := "") -> Dictionary:
	var out := {"count": 0, "gathered": 0, "used": 0}
	for r in RESOURCES:
		var v := calc_resources(p, index, r[0])
		for k in out:
			out[k] += v[k]
	return out

func calc_tributes(p: Dictionary, index: int, _t := "") -> Dictionary:
	var s := _stats(p, index)
	return {"sent": int(s.get("tributesSent", 0)), "received": int(s.get("tributesReceived", 0))}

func calc_treasures(p: Dictionary, index: int, _t := "") -> int:
	return int(_stats(p, index).get("treasuresCollected", 0))

func calc_loot(p: Dictionary, index: int, _t := "") -> int:
	return int(_stats(p, index).get("lootCollected", 0))

func calc_livestock(p: Dictionary, index: int, _t := "") -> int:
	return _by(_stats(p, index), "unitsTrained", "Domestic")

# Market
func calc_trade_income(p: Dictionary, index: int, _t := "") -> int:
	return int(_stats(p, index).get("tradeIncome", 0))

func _bought(p: Dictionary, index: int) -> float:
	var s := _stats(p, index)
	return float(RESOURCES.reduce(func(a, r): return a + _by(s, "resourcesBought", r[0]), 0))

func _sold(p: Dictionary, index: int) -> float:
	var s := _stats(p, index)
	return float(RESOURCES.reduce(func(a, r): return a + _by(s, "resourcesSold", r[0]), 0))

func calc_barter_efficiency(p: Dictionary, index: int, _t := "") -> Dictionary:
	return calculate_percent(_bought(p, index), _sold(p, index))

func calc_exchanged(p: Dictionary, index: int, type: String) -> Dictionary:
	var s := _stats(p, index)
	return {"bought": _by(s, "resourcesBought", type), "sold": _by(s, "resourcesSold", type)}

func calc_market_team(team: int, index: int, type: String, counters: Array, headings: Array) -> Variant:
	if type == "barterEfficency":
		return calculate_percent(_team_total(team, index, _bought), _team_total(team, index, _sold))
	return array_sum(_team_values(team, index, type, counters, headings))

# Miscellaneous
func _killed(p: Dictionary, index: int) -> float:
	return _by(_stats(p, index), "enemyUnitsKilled", "Unit")

func _lost(p: Dictionary, index: int) -> float:
	return _by(_stats(p, index), "unitsLost", "Unit")

func _food(p: Dictionary, index: int) -> float:
	return _by(_stats(p, index), "resourcesGathered", "food")

func _veg(p: Dictionary, index: int) -> float:
	return _by(_stats(p, index), "resourcesGathered", "vegetarianFood")

func _civilian(p: Dictionary, index: int) -> float:
	return _by(_stats(p, index), "unitsTrained", "Civilian")

func _worker(p: Dictionary, index: int) -> float:
	return _by(_stats(p, index), "unitsTrained", "Worker")

func calc_kill_death(p: Dictionary, index: int, _t := "") -> Variant:
	return calculate_ratio(_killed(p, index), _lost(p, index))

func calc_population(p: Dictionary, index: int, _t := "") -> Dictionary:
	return {"population": int(_stats(p, index).get("populationCount", 0))}

func calc_bribes(p: Dictionary, index: int, _t := "") -> Dictionary:
	var s := _stats(p, index)
	return {"succeeded": int(s.get("successfulBribes", 0)), "failed": int(s.get("failedBribes", 0))}

func calc_map_peak(p: Dictionary, index: int, _t := "") -> Dictionary:
	return {"percent": int(floor(float(_stats(p, index).get("peakPercentMapControlled", 0.0))))}

func calc_map_final(p: Dictionary, index: int, _t := "") -> Dictionary:
	return {"percent": _percent_of(p, index, "percentMapControlled")}

func calc_map_exploration(p: Dictionary, index: int, _t := "") -> Dictionary:
	return {"percent": _percent_of(p, index, "percentMapExplored")}

func calc_vegetarian(p: Dictionary, index: int, _t := "") -> Dictionary:
	return calculate_percent(_veg(p, index), _food(p, index))

## calculateCivilianization (0.28; Feminization before).
func calc_civilianization(p: Dictionary, index: int, _t := "") -> Dictionary:
	return calculate_percent(_civilian(p, index), _worker(p, index))

func calc_misc_team(team: int, index: int, type: String, counters: Array, headings: Array) -> Variant:
	match type:
		"bribes", "population":
			return array_sum(_team_values(team, index, type, counters, headings))
		"vegetarianRatio":
			return calculate_percent(_team_total(team, index, _veg), _team_total(team, index, _food))
		"civilianization":
			return calculate_percent(_team_total(team, index, _civilian), _team_total(team, index, _worker))
		"killDeath":
			return calculate_ratio(_team_total(team, index, _killed), _team_total(team, index, _lost))
		"mapControlPeak":
			return {"percent": int(floor(float(_stats(_team_first(team), index).get("teamPeakPercentMapControlled", 0.0))))}
		"mapControl":
			return {"percent": _percent_of(_team_first(team), index, "teamPercentMapControlled")}
		"mapExploration":
			return {"percent": _percent_of(_team_first(team), index, "teamPercentMapExplored")}
	return 0

static func H(id: String, caption: String, y: int, w: int, extra := {}) -> Dictionary:
	var d := {"id": id, "caption": caption, "y": y, "w": w}
	d.merge(extra)
	return d

static func C(w: int, fn: Callable, off: int) -> Dictionary:
	return {"w": w, "fn": fn, "off": off}

static func _hidden(c: Dictionary) -> Dictionary:
	c["hide"] = true
	return c

## layout.js getScorePanelsData. A heading or counter marked "hide"
## (hideInSummary) is only charted, not shown in the table.
func score_panels() -> Array:
	var name := H("playername", "Player name", 26, 200)
	var structures_h := [name, H("Structure", "Total", 34, 90)]
	for b in [["House", "Houses"], ["Economic", "Economic"], ["Outpost", "Outposts"], ["Military", "Military"],
			["Fortress", "Fortresses"], ["CivCentre", "Civ centers"], ["Wonder", "Wonders"]]:
		structures_h.append(H(b[0], b[1], 34, 90))
	var units_h := [name, H("Unit", "Total", 34, 90)]
	for u in [["Infantry", "Infantry"], ["Worker", "Worker"], ["Cavalry", "Cavalry"], ["Champion", "Champion"],
			["Hero", "Heroes"], ["Siege", "Siege"], ["Ship", "Navy"], ["Trader", "Traders"]]:
		units_h.append(H(u[0], u[1], 34, 90))
	var res_h := [name, H("total", "Total", 34, 110)]
	var res_c := [C(110, calc_total_resources, 12)]
	var market_h := [name, H("tradeIncome", "Trade income", 16, 120),
		H("barterEfficency", "Barter efficiency", 16, 120, {"format": "PERCENTAGE"})]
	var market_c := [C(120, calc_trade_income, 12), C(120, calc_barter_efficiency, 12)]
	for r in RESOURCES:
		res_h.append(H(r[0], r[1], 34, 110))
		res_c.append(C(110, calc_resources, 12))
		market_h.append(H(r[0], L10n.sprintf(L10n.t("%(resourceFirstWord)s exchanged"), {"resourceFirstWord": L10n.t(r[1])}), 16, 120))
		market_c.append(C(120, calc_exchanged, 12))
	res_h.append(H("tributes", "Tributes", 16, 110,
		{"header": L10n.sprintf(L10n.t("Tributes \n(%(sent)s / %(received)s)"), {"sent": colored_type("sent"), "received": colored_type("received")})}))
	res_h.append(H("treasuresCollected", "Treasures collected", 16, 85))
	res_h.append(H("loot", "Loot", 16, 85))
	res_h.append(H("livestock", "Livestock bred", 16, 85))
	res_c.append_array([C(110, calc_tributes, 12), C(85, calc_treasures, 12), C(85, calc_loot, 12),
		C(85, calc_livestock, 12)])
	return [
		{"label": "Score", "headings": [name, H("totalScore", "Total score", 16, 120),
			H("economyScore", "Economy score", 16, 120), H("militaryScore", "Military score", 16, 120),
			H("explorationScore", "Exploration score", 16, 120)],
		"titles": [],
		"counters": [C(120, calc_score_total, 12), C(120, calc_economy_score, 12),
			C(120, calc_military_score, 12), C(120, calc_exploration_score, 12)],
		"team_fn": calc_score_team},
		{"label": "Structures", "headings": structures_h,
		"titles": [{"caption": L10n.sprintf(L10n.t("Structure Statistics (%(constructed)s / %(destroyed)s / %(captured)s / %(lost)s)"),
			{"constructed": colored_type("constructed"), "destroyed": colored_type("destroyed"), "captured": colored_type("captured"),
			"lost": colored_type("lost")}), "y": 16, "w": 90 * 8}],
		"counters": range(8).map(func(_i): return C(90, calc_buildings, 3)),
		"team_fn": calc_sum_team},
		{"label": "Units", "headings": units_h,
		"titles": [{"caption": L10n.sprintf(L10n.t("Unit Statistics (%(trained)s / %(killed)s / %(lost)s)"),
			{"trained": colored_type("trained"), "killed": colored_type("killed"), "lost": colored_type("lost")}), "y": 16, "w": 90 * 9}],
		"counters": range(9).map(func(_i): return C(90, calc_units, 3)),
		"team_fn": calc_sum_team},
		{"label": "Resources", "headings": res_h,
		"titles": [{"caption": L10n.sprintf(L10n.t("Resource Statistics (%(gathered)s / %(used)s)"),
			{"gathered": colored_type("gathered"), "used": colored_type("used")}),
			"y": 16, "w": 110 * RESOURCES.size() + 110}],
		"counters": res_c, "team_fn": calc_sum_team},
		{"label": "Market", "headings": market_h, "titles": [], "counters": market_c, "team_fn": calc_market_team},
		{"label": "Miscellaneous", "headings": [name,
			H("killDeath", "Kill / Death ratio", 16, 110, {"format": "DECIMAL2"}),
			H("population", "Population", 16, 110, {"hide": true}),
			H("mapControlPeak", "Map control (peak)", 16, 110, {"format": "PERCENTAGE"}),
			H("mapControl", "Map control (finish)", 16, 110, {"format": "PERCENTAGE"}),
			H("mapExploration", "Map exploration", 16, 110, {"format": "PERCENTAGE"}),
			H("vegetarianRatio", "Vegetarian ratio", 16, 110, {"format": "PERCENTAGE"}),
			H("civilianization", "Civilianization", 16, 110, {"format": "PERCENTAGE"}),
			H("bribes", "Bribes", 16, 110,
				{"header": L10n.sprintf(L10n.t("Bribes\n(%(succeeded)s / %(failed)s)"), {"succeeded": colored_type("succeeded"), "failed": colored_type("failed")})})],
		"titles": [],
		"counters": [C(110, calc_kill_death, 12), _hidden(C(110, calc_population, 12)), C(110, calc_map_peak, 12),
			C(110, calc_map_final, 12), C(110, calc_map_exploration, 12), C(110, calc_vegetarian, 12),
			C(110, calc_civilianization, 12), C(110, calc_bribes, 12)],
		"team_fn": calc_misc_team},
	]

static func _bb(text: String, color: Color) -> String:
	return "[color=#%s]%s[/color]" % [color.to_html(false), text]

static func colored_type(type: String) -> String:
	var t: Dictionary = SUMMARY_TYPES[type]
	var caption := L10n.t(t["caption"])
	return _bb(caption, TYPE_COLORS[t["color"]]) if t.has("color") else caption

## counters.js formatSummaryValue, as BBCode.
static func format_value(v: Variant) -> String:
	if v is Dictionary:
		var out := ""
		for type in v:
			var t: Dictionary = SUMMARY_TYPES[type]
			if t.get("hide", false):
				continue
			out += (_bb(str(v[type]), TYPE_COLORS[t["color"]]) if t.has("color") else str(v[type])) + t["postfix"]
		return out
	if v is float and is_inf(v):
		return INFINITY_SYMBOL
	if v is float and v == floorf(v):
		return str(int(v)) # JS prints +(2).toFixed(2) as "2"
	return str(v)

## The plain value of one counter for a player (tests and charts).
func player_value(player_index: int, panel: int, column: int, index := -1) -> Variant:
	var pd: Dictionary = panels[panel]
	if index < 0:
		index = sample_count() - 1
	return pd["counters"][column]["fn"].call(players[player_index], index, pd["headings"][column + 1]["id"])

func team_value(team: int, panel: int, column: int, index := -1) -> Variant:
	var pd: Dictionary = panels[panel]
	if index < 0:
		index = sample_count() - 1
	return pd["team_fn"].call(team, index, pd["headings"][column + 1]["id"], pd["counters"], pd["headings"])

# --- Page ---------------------------------------------------------------------

static func time_to_string(seconds: float) -> String:
	var s := int(seconds)
	if s >= 3600:
		return "%02d:%02d:%02d" % [s / 3600, (s / 60) % 60, s % 60]
	return "%02d:%02d" % [s / 60, s % 60]

## initGUILabels.
func summary_text() -> String:
	if gui.get("isInGame", false):
		return "Current Scores"
	if gui.get("isReplay", false) or gui.get("fromReplayMenu", false):
		return "Scores at the end of the game."
	if gui.get("disconnected", false):
		return "You have been disconnected."
	var me := int(gui.get("assignedPlayer", data.get("local_player", -1)))
	var mine: Array = players.filter(func(p): return p["id"] == me)
	if me < 0 or mine.is_empty():
		return "You have left the game."
	match mine[0]["state"]:
		"won":
			return "You have won the battle!"
		"defeated":
			return "You have been defeated…"
	return "You have abandoned the game."

func map_caption() -> String:
	var map_name := str(data.get("map_name", "")).trim_suffix(" (RMS)") # summary.js: the plain map name
	var map_id := str(data.get("map_id", ""))
	var random := map_id.begins_with("random_") or map_id.begins_with("procedural_")
	var size := int(data.get("map_size", 0))
	# summary.js: a random map's size, else the map type's title (Skirmish or Scenario).
	var kind: String = MAP_SIZE_NAMES.get(size, "Random") if random \
		else str(load("res://scenes/menu/replay_browser.gd").map_info(map_id)["type"])
	return "%s - %s" % [map_name, kind] if kind != "" else map_name

# gui/common/modern/styles.xml ModernLabelText and its kin: sans-bold-stroke-14, white.
static var _bold_font: FontVariation

static func bold_font() -> FontVariation:
	if _bold_font == null:
		_bold_font = FontVariation.new()
		_bold_font.variation_embolden = 0.6
	return _bold_font

func _label(text: String, size := 14, color := Color.WHITE) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_font_override("font", bold_font())
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_color_override("font_color", color)
	l.add_theme_color_override("font_outline_color", Color.BLACK)
	l.add_theme_constant_override("outline_size", 3)
	l.mouse_filter = Control.MOUSE_FILTER_PASS
	return l

func _rich(bbcode: String, size := 14) -> RichTextLabel:
	var r := RichTextLabel.new()
	r.bbcode_enabled = true
	r.fit_content = true
	r.scroll_active = false
	r.autowrap_mode = TextServer.AUTOWRAP_OFF
	r.mouse_filter = Control.MOUSE_FILTER_IGNORE
	r.add_theme_font_override("normal_font", bold_font())
	r.add_theme_font_size_override("normal_font_size", size)
	r.add_theme_font_size_override("bold_font_size", size)
	r.add_theme_color_override("font_outline_color", Color.BLACK)
	r.add_theme_constant_override("outline_size", 3)
	r.text = bbcode
	return r

func _place(c: Control, parent: Control, pos: Vector2, size: Vector2) -> Control:
	parent.add_child(c)
	c.position = pos
	c.size = size
	return c

func _build() -> void:
	for c in get_children():
		remove_child(c)
		c.queue_free()
	_tab_buttons.clear()
	_chart_nodes.clear()
	_pickers.clear()
	var dialog: bool = gui.get("dialog", false)
	var fade := ColorRect.new() # ModernFade
	fade.color = Color(0, 0, 0, 0.6 if dialog else (1.0 if get_parent() == get_tree().root else 0.85))
	fade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(fade)
	_window = Panel.new() # summaryWindow: ModernDialog in game, else ModernWindow
	_window.name = "SummaryWindow"
	_window.theme = load("res://assets/themes/zeroad_theme.tres")
	_window.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	if dialog:
		_window.offset_left = 16
		_window.offset_right = -16
		_window.offset_top = 24
		_window.offset_bottom = -24
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.1, 0.11, 0.14, 0.97)
	style.border_color = GOLD
	style.set_border_width_all(2)
	_window.add_theme_stylebox_override("panel", style)
	add_child(_window)
	var title := _label(L10n.t("Summary")) # summaryWindowTitle: ModernLabelText
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.set_anchors_preset(Control.PRESET_CENTER_TOP)
	title.offset_left = -128
	title.offset_right = 128
	title.offset_top = 4
	title.offset_bottom = 30
	if dialog:
		# summary.js: in a dialog the title sits on the top edge, "50%-128 -16 50%+128 16".
		title.offset_top = -16
		title.offset_bottom = 16
		var plate := StyleBoxFlat.new()
		plate.bg_color = style.bg_color
		plate.border_color = GOLD
		plate.set_border_width_all(2)
		title.add_theme_stylebox_override("normal", plate)
		title.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	_window.add_child(title)
	var header := HBoxContainer.new()
	header.set_anchors_preset(Control.PRESET_TOP_WIDE)
	# summary.xml: "20 26 100%-20 92", each caption "16" in from its top and bottom.
	header.offset_left = 20
	header.offset_right = -20
	header.offset_top = 42
	header.offset_bottom = 76
	_window.add_child(header)
	for t in [[summary_text(), HORIZONTAL_ALIGNMENT_LEFT, "SummaryText"],
			[map_caption(), HORIZONTAL_ALIGNMENT_CENTER, "MapName"],
			[L10n.sprintf(L10n.t("Game time elapsed: %(time)s"), {"time": time_to_string(float(data.get("game_time", 0.0)))}), HORIZONTAL_ALIGNMENT_RIGHT, "TimeElapsed"]]:
		var l := _label(t[0], 18) # sans-bold-18
		var bold := FontVariation.new()
		bold.variation_embolden = 0.6
		l.add_theme_font_override("font", bold)
		l.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		l.name = t[2]
		l.horizontal_alignment = t[1]
		l.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		header.add_child(l)
	# Tab buttons (gui/common/tab_buttons.js placeTabButtons).
	var group := ButtonGroup.new()
	var labels: Array = panels.map(func(p): return p["label"]) + ["Charts"]
	for i in labels.size():
		var b := Button.new()
		b.name = "Tab%d" % i
		b.text = labels[i]
		b.toggle_mode = true
		b.button_group = group
		b.tooltip_text = "\n".join([L10n.sprintf(L10n.t("Focus the %(name)s summary tab."), {"name": L10n.t(labels[i])}),
			L10n.sprintf(L10n.t("Scroll down or use %(hotkey)s to move a tab right."), {"hotkey": _hotkey_text("tab.next")}),
			L10n.sprintf(L10n.t("Scroll up or use %(hotkey)s to move a tab left."), {"hotkey": _hotkey_text("tab.prev")})])
		b.pressed.connect(select_panel.bind(i))
		b.gui_input.connect(_on_tab_wheel)
		_place(b, _window, Vector2(17 + i * (TAB_W + TAB_GAP), 92), Vector2(TAB_W, 28)) # tabButtonsFrame "17 92 883 120"
		_tab_buttons.append(b)
	var frame := Panel.new() # generalPanel / chartsPanel frame
	frame.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	frame.offset_left = 20
	frame.offset_right = -20
	frame.offset_top = 120 # generalPanel "20 120 100%-20 100%-54"
	frame.offset_bottom = -54
	var fs := StyleBoxFlat.new()
	fs.bg_color = Color(0, 0, 0, 0.25)
	fs.border_color = Color(0.5, 0.45, 0.3)
	fs.set_border_width_all(1)
	frame.add_theme_stylebox_override("panel", fs)
	_window.add_child(frame)
	var scroll := ScrollContainer.new()
	scroll.name = "GeneralPanel"
	scroll.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	frame.add_child(scroll)
	_general = Control.new()
	scroll.add_child(_general)
	_charts_panel = Control.new()
	_charts_panel.name = "ChartsPanel"
	_charts_panel.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	frame.add_child(_charts_panel)
	_build_charts()
	# Buttons.
	_continue_button = Button.new()
	_continue_button.name = "ContinueButton"
	_continue_button.text = "Continue"
	_continue_button.pressed.connect(continue_pressed)
	_continue_button.set_anchors_preset(Control.PRESET_BOTTOM_RIGHT)
	_window.add_child(_continue_button)
	# summary.xml continueButton "100%-200 100%-48 100%-20 100%-20", replayButton "100%-410 ... 100%-210".
	_continue_button.offset_left = -200
	_continue_button.offset_right = -20
	_continue_button.offset_top = -48
	_continue_button.offset_bottom = -20
	_replay_button = Button.new()
	_replay_button.name = "ReplayButton"
	_replay_button.text = "Watch Replay"
	_replay_button.pressed.connect(start_replay)
	_replay_button.visible = not gui.get("isInGame", false) and str(gui.get("replayDirectory", "")) != ""
	_replay_button.set_anchors_preset(Control.PRESET_BOTTOM_RIGHT)
	_window.add_child(_replay_button)
	_replay_button.offset_left = -410
	_replay_button.offset_right = -210
	_replay_button.offset_top = -48
	_replay_button.offset_bottom = -20
	# lobbyButton: after a game while signed in to the lobby, left of the other buttons.
	# A 16 px speech bubble (sprite iconBubbleGold, iconBubbleWhite under the mouse), "100%-436
	# 100%-45 100%-420 100%-29": 10 px left of the Watch Replay button.
	var lobby := TextureButton.new()
	lobby.name = "LobbyButton"
	lobby.texture_normal = Ref.texture(BUBBLE_ICON)
	lobby.ignore_texture_size = true
	lobby.stretch_mode = TextureButton.STRETCH_KEEP_ASPECT_CENTERED
	lobby.self_modulate = BUBBLE_GOLD
	lobby.mouse_entered.connect(func(): lobby.self_modulate = Color.WHITE)
	lobby.mouse_exited.connect(func(): lobby.self_modulate = BUBBLE_GOLD)
	lobby.tooltip_text = "%s: Toggle the multiplayer lobby in a dialog window." % _hotkey_text("lobby")
	lobby.visible = not gui.get("isInGame", false) and _lobby_signed_in()
	lobby.pressed.connect(open_lobby)
	lobby.set_anchors_preset(Control.PRESET_BOTTOM_RIGHT)
	_window.add_child(lobby)
	var right := (_replay_button.offset_left if _replay_button.visible else _continue_button.offset_left) - 10
	lobby.offset_left = right - 16
	lobby.offset_right = right
	lobby.offset_top = -45
	lobby.offset_bottom = -29

## tab_buttons.xml MouseWheelDown / MouseWheelUp: the next or previous tab.
func _on_tab_wheel(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.pressed and \
			(event.button_index == MOUSE_BUTTON_WHEEL_DOWN or event.button_index == MOUSE_BUTTON_WHEEL_UP):
		var n := panels.size() + 1
		select_panel((panel_index + (1 if event.button_index == MOUSE_BUTTON_WHEEL_DOWN else n - 1)) % n)

## colorizeHotkey: every binding, shortest first, or "Unassigned hotkey: name".
func _hotkey_text(hotkey: String) -> String:
	return load("res://scenes/menu/replay_browser.gd").hotkey_text(_hotkeys.call("get_hotkey_map") if _hotkeys else {}, hotkey)

func select_panel(i: int) -> void:
	panel_index = clampi(i, 0, panels.size())
	selection["panel"] = panel_index
	if panel_index < _tab_buttons.size():
		_tab_buttons[panel_index].button_pressed = true
	var charts := panel_index >= panels.size()
	_general.get_parent().visible = not charts
	_charts_panel.visible = charts
	_team_toggle.visible = charts and not teams.is_empty()
	if charts:
		for n in 2:
			_update_category(n)
	else:
		_update_panel(panels[panel_index])

## updatePanelData: headings, title headings, a row per player, team totals.
func _update_panel(pd: Dictionary) -> void:
	for c in _general.get_children():
		_general.remove_child(c)
		c.queue_free()
	# counters.js: hideInSummary columns are left out of the table (they are charted).
	var headings: Array = pd["headings"].filter(func(h): return not h.get("hide", false))
	var counters: Array = pd["counters"].filter(func(c): return not c.get("hide", false))
	var left := 50
	for hi in headings.size():
		var h: Dictionary = headings[hi]
		var cap: String = L10n.t(h.get("header", h["caption"])) # layout.js translate(), before the markup
		if hi == 0 and not teams.is_empty() and not teams.has(-1):
			cap = "" # no players without a team: no "Player name" heading
		var r := _rich("[center][b]%s[/b][/center]" % cap if hi > 0 else "[b]%s[/b]" % cap)
		r.name = "Heading%d" % hi
		r.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART # long captions wrap in their column
		_place(r, _general, Vector2(left, h["y"]), Vector2(h["w"], 40))
		if h["w"] < LONG_HEADING:
			left += h["w"]
	left = 250
	for t in pd["titles"]:
		var r := _rich("[center][b]%s[/b][/center]" % L10n.t(t["caption"]))
		_place(r, _general, Vector2(left, t["y"]), Vector2(t["w"], 20))
	var row_w := 240
	for c in counters:
		row_w += c["w"]
	var index := sample_count() - 1
	var y := 65
	if teams.is_empty():
		for i in players.size():
			_player_row(players[i], counters, headings, index, Vector2(10, y + i * (BOX_H + BOX_GAP)), row_w)
		y += players.size() * (BOX_H + BOX_GAP)
	else:
		var keys := teams.keys()
		keys.sort()
		if keys.has(-1): # updateGeneralPanelTeams: noTeamsBox, then a box per team
			var loose: Array = teams[-1]
			for n in loose.size():
				_player_row(players[loose[n]], counters, headings, index, Vector2(10, y + n * (BOX_H + BOX_GAP)), row_w)
			y += loose.size() * (BOX_H + BOX_GAP) + 30
			keys.erase(-1)
		for t in keys:
			var members: Array = teams[t]
			var head := _label(L10n.sprintf(L10n.t("Team %(team)s"), {"team": t + 1}), 14) # ModernLeftTopLabelText: white
			head.name = "TeamHeading%d" % t
			_place(head, _general, Vector2(15, y + 5), Vector2(200, 22))
			for n in members.size():
				_player_row(players[members[n]], counters, headings, index, Vector2(10, y + 30 + n * (BOX_H + BOX_GAP)), row_w)
			var total_y: int = y + 30 + members.size() * (BOX_H + BOX_GAP) + 10
			var total := _label(L10n.t("Team total")) # teamHeading: ModernLeftLabelText, white
			_place(total, _general, Vector2(50, total_y), Vector2(180, 20))
			var x := 240
			for ci in counters.size():
				var v: Variant = pd["team_fn"].call(t, index, headings[ci + 1]["id"], counters, headings)
				var cell := _rich("[center]%s[/center]" % format_value(v))
				cell.name = "TeamValue%d_%d" % [t, ci]
				_place(cell, _general, Vector2(x + 20, total_y - 10 + 3 + counters[ci]["off"]), Vector2(counters[ci]["w"] - 20, 36))
				x += counters[ci]["w"]
			y = total_y + 42
	_general.custom_minimum_size = Vector2(maxi(row_w + 20, left + 20), y + 10)

func _player_row(p: Dictionary, counters: Array, headings: Array, index: int, pos: Vector2, width: int) -> void:
	var box := ColorRect.new()
	box.name = "Player%d" % p["id"]
	box.color = Color(p["color"], 50.0 / 255.0) # g_PlayerBoxAlpha
	_place(box, _general, pos, Vector2(width - pos.x, BOX_H))
	if p["state"] == "won" or p["state"] == "defeated": # setOutcomeIcon
		var icon := TextureRect.new()
		icon.texture = Ref.texture("res://assets/textures/ui/archive/icons/%s.png" % ("victory" if p["state"] == "won" else "defeat"))
		icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		icon.tooltip_text = "Victorious" if p["state"] == "won" else "Defeated"
		_place(icon, box, Vector2(4, 5), Vector2(32, 32))
	var name := _label(p["name"], 14)
	name.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED # a player's own name
	name.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	name.clip_text = true
	name.tooltip_text = p["tooltip"]
	_place(name, box, Vector2(46, 2), Vector2(168, BOX_H - 2))
	var civ_info: Dictionary = Ref.ref().get_civ_info(p["civ"])
	if not civ_info.is_empty():
		var emblem := TextureRect.new()
		emblem.texture = Ref.texture(str(civ_info.get("emblem", "")))
		emblem.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		emblem.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		emblem.tooltip_text = str(civ_info.get("name", ""))
		_place(emblem, box, Vector2(214, 5), Vector2(32, 32))
	var x := 240
	for ci in counters.size():
		var v: Variant = counters[ci]["fn"].call(p, index, headings[ci + 1]["id"])
		var cell := _rich("[center]%s[/center]" % format_value(v))
		cell.name = "Value%d" % ci
		_place(cell, box, Vector2(x - pos.x, counters[ci]["off"]), Vector2(counters[ci]["w"], BOX_H))
		x += counters[ci]["w"]

# --- Charts (summary.js updateCategoryDropdown .. updateChart) ----------------

func _build_charts() -> void:
	for k in 2:
		var part := Control.new()
		part.name = "Chart%dPart" % k
		part.anchor_left = 0.0 if k == 0 else 0.5
		part.anchor_right = 0.5 if k == 0 else 1.0
		part.anchor_bottom = 1.0
		part.offset_left = 15 if k == 0 else 5
		part.offset_right = -10 if k == 0 else -15
		part.offset_bottom = -35
		_charts_panel.add_child(part)
		var pickers := []
		for j in 3:
			var cap := _label(["Category:", "Value:", "Type:"][j])
			_place(cap, part, Vector2(1 + j * 150, 6), Vector2(144, 20)) # "1 6 145 26"
			var dd := OptionButton.new()
			dd.name = ["Category", "Value", "Type"][j]
			dd.tooltip_text = ["Category", "Value", "Type"][j]
			dd.fit_to_longest_item = false
			dd.clip_text = true
			_place(dd, part, Vector2(5 + j * 150, 28), Vector2(140, 28)) # "5 28 145 56"
			pickers.append(dd)
			if j == 2:
				pickers.append(cap)
		pickers[0].item_selected.connect(_on_category_selected.bind(k))
		pickers[1].item_selected.connect(_on_value_selected.bind(k))
		pickers[2].item_selected.connect(_on_type_selected.bind(k))
		_pickers.append(pickers)
		var bg := ColorRect.new()
		bg.color = Color(1, 1, 1, 20.0 / 255.0)
		bg.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
		bg.offset_top = 80 # "0 80 100% 100%"
		part.add_child(bg)
		var chart := Control.new()
		chart.name = "Chart%d" % k
		chart.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
		chart.offset_left = 5
		chart.offset_right = -5
		chart.offset_top = 85 # "5 85 100%-5 100%-25"
		chart.offset_bottom = -25
		chart.draw.connect(_draw_chart.bind(k))
		part.add_child(chart)
		_chart_nodes.append(chart)
		var axis := _label("Time elapsed")
		axis.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		axis.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_WIDE)
		axis.offset_top = -25 # XAxisLabel "5 100%-25 100%-5 100%-5"
		axis.offset_bottom = -5
		part.add_child(axis)
	_legend = _rich("")
	_legend.name = "ChartLegend"
	_legend.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_WIDE)
	_legend.offset_left = 15
	_legend.offset_right = -15
	_legend.offset_top = -32
	_legend.offset_bottom = -6
	_charts_panel.add_child(_legend)
	_team_toggle = CheckBox.new()
	_team_toggle.name = "ToggleTeam"
	_team_toggle.text = "Group by team"
	_team_toggle.button_pressed = bool(selection["teamCharts"]) and not teams.is_empty()
	_team_toggle.toggled.connect(func(on: bool):
		selection["teamCharts"] = on
		for n in 2:
			_update_category(n))
	_team_toggle.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)
	_window.add_child(_team_toggle)
	_team_toggle.offset_left = 24
	_team_toggle.offset_right = 260
	_team_toggle.offset_top = -46
	_team_toggle.offset_bottom = -16

func team_charts() -> bool:
	return _team_toggle != null and _team_toggle.button_pressed and not teams.is_empty()

func _update_category(n: int) -> void:
	var dd: OptionButton = _pickers[n][0]
	dd.clear()
	for p in panels:
		dd.add_item(p["label"])
	dd.select(clampi(int(selection["category"][n]), 0, panels.size() - 1))
	_update_value(n)

func _on_category_selected(i: int, n: int) -> void:
	if selection["category"][n] != i:
		selection["category"][n] = i
		selection["value"][n] = 0
		selection["type"][n] = 0
	_update_value(n)

func _update_value(n: int) -> void:
	var cat := clampi(int(selection["category"][n]), 0, panels.size() - 1)
	var dd: OptionButton = _pickers[n][1]
	dd.clear()
	var headings: Array = panels[cat]["headings"]
	for i in range(1, headings.size()):
		dd.add_item(headings[i]["caption"])
	selection["value"][n] = clampi(int(selection["value"][n]), 0, headings.size() - 2)
	dd.select(selection["value"][n])
	_update_type(n)

func _on_value_selected(i: int, n: int) -> void:
	if selection["value"][n] != i:
		selection["value"][n] = i
		selection["type"][n] = 0
	_update_type(n)

func _update_type(n: int) -> void:
	var cat := clampi(int(selection["category"][n]), 0, panels.size() - 1)
	var item: int = selection["value"][n]
	var test: Variant = player_value(0, cat, item, 0) if not players.is_empty() else 0
	var hide: bool = not (test is Dictionary) or test.size() < 2
	var dd: OptionButton = _pickers[n][2]
	dd.visible = not hide
	_pickers[n][3].visible = not hide
	if hide:
		_update_chart(n, cat, item, test.keys()[0] if test is Dictionary and not test.is_empty() else "")
		return
	dd.clear()
	for t in test.keys():
		dd.add_item(SUMMARY_TYPES[t]["caption"])
	selection["type"][n] = clampi(int(selection["type"][n]), 0, test.size() - 1)
	dd.select(selection["type"][n])
	_update_chart(n, cat, item, test.keys()[selection["type"][n]])

func _on_type_selected(i: int, n: int) -> void:
	selection["type"][n] = i
	_update_type(n)

func _y_number(v: Variant, type: String) -> float:
	if type != "" and v is Dictionary:
		v = v.get(type, 0)
	return float(v) # an infinite ratio stays infinite: the chart leaves it out

func _update_chart(n: int, cat: int, item: int, type: String) -> void:
	chart_format[n] = panels[cat]["headings"][item + 1].get("format", "INTEGER")
	var out := []
	var count := sample_count()
	if team_charts():
		var keys := teams.keys()
		keys.sort()
		for t in keys:
			var pts := PackedVector2Array()
			for index in count:
				pts.append(Vector2(sample_time(index), _y_number(team_value(t, cat, item, index), type)))
			out.append({"points": pts, "color": players[teams[t][0]]["color"]})
	else:
		for pi in players.size():
			var pts := PackedVector2Array()
			for index in count:
				pts.append(Vector2(sample_time(index), _y_number(player_value(pi, cat, item, index), type)))
			out.append({"points": pts, "color": players[pi]["color"]})
	series[n] = out
	_chart_nodes[n].queue_redraw()
	_update_legend()

## updateChartColorAndLegend.
func _update_legend() -> void:
	var parts: PackedStringArray = []
	if team_charts():
		var keys := teams.keys()
		keys.sort()
		for t in keys:
			parts.append("%s %s" % [_bb("■", players[teams[t][0]]["color"]),
				", ".join(teams[t].map(func(i): return players[i]["name"]))])
	else:
		for p in players:
			parts.append("%s %s" % [_bb("■", p["color"]), p["name"]])
	_legend.text = "[center]%s[/center]" % "   ".join(parts)

## CChart::AddFormattedValue.
static func format_axis(v: float, format: String) -> String:
	match format:
		"PERCENTAGE":
			return "%d%%" % roundi(v)
		"DECIMAL2":
			return "%.2f" % v
		"DURATION_SHORT":
			var sec := int(v)
			return "%d:%02d" % [sec / 60, sec % 60]
	return str(roundi(v))

# gui/common/modern/styles.xml ModernChart: axis_color, axis_width, buffer_zone, sans-stroke-14.
const CHART_AXIS_COLOR := Color8(209, 174, 132)
const CHART_AXIS_WIDTH := 2.0
const CHART_BUFFER := 4.0
const CHART_FONT_SIZE := 14

## CChart::UpdateBounds: the smallest and largest finite x and y over every series, from
## the first point on (no zero baseline); an axis with one value gets 1 added to its top.
## Returns [left_bottom, right_top, equal_x, equal_y].
static func chart_bounds(all_series: Array) -> Array:
	var lb := Vector2(INF, INF)
	var rt := Vector2(-INF, -INF)
	for s in all_series:
		for pt in s["points"]:
			if not is_inf(pt.x):
				lb.x = minf(lb.x, pt.x)
				rt.x = maxf(rt.x, pt.x)
			if not is_inf(pt.y):
				lb.y = minf(lb.y, pt.y)
				rt.y = maxf(rt.y, pt.y)
	if is_inf(lb.x):
		lb.x = 0.0
		rt.x = 0.0
	if is_inf(lb.y):
		lb.y = 0.0
		rt.y = 0.0
	var equal_x := rt.x == lb.x
	var equal_y := rt.y == lb.y
	if equal_x:
		rt.x += 1.0
	if equal_y:
		rt.y += 1.0
	return [lb, rt, equal_x, equal_y]

## CChart::Draw: the series as 2 px lines inside the axes, the left and bottom axes, and
## SetupText's labels: three y values down the left from the top, three x values along
## the bottom from the right (one each when an axis has a single value).
func _draw_chart(n: int) -> void:
	var chart: Control = _chart_nodes[n]
	if series[n].is_empty():
		return
	var font := ThemeDB.fallback_font
	var rect := Rect2(Vector2(CHART_AXIS_WIDTH, CHART_AXIS_WIDTH), chart.size - Vector2(2, 2) * CHART_AXIS_WIDTH)
	var b := chart_bounds(series[n])
	var lb: Vector2 = b[0]
	var rt: Vector2 = b[1]
	var scale := Vector2(rect.size.x / (rt.x - lb.x), rect.size.y / (rt.y - lb.y))
	for s in series[n]:
		for line in chart_lines(s["points"]):
			if line.size() < 2:
				continue # a one-point line draws nothing
			var pts := PackedVector2Array()
			for pt in line:
				pts.append(Vector2(rect.position.x + (pt.x - lb.x) * scale.x, rect.end.y - (pt.y - lb.y) * scale.y))
			chart.draw_polyline(pts, s["color"], 2.0, true)
	chart.draw_rect(Rect2(0, 0, CHART_AXIS_WIDTH, chart.size.y), CHART_AXIS_COLOR)
	chart.draw_rect(Rect2(0, chart.size.y - CHART_AXIS_WIDTH, chart.size.x, CHART_AXIS_WIDTH), CHART_AXIS_COLOR)
	var ascent := font.get_ascent(CHART_FONT_SIZE)
	var text := func(t: String, top_left: Vector2) -> void:
		chart.draw_string_outline(font, top_left + Vector2(CHART_BUFFER, CHART_BUFFER + ascent), t,
			HORIZONTAL_ALIGNMENT_LEFT, -1, CHART_FONT_SIZE, 3, Color.BLACK)
		chart.draw_string(font, top_left + Vector2(CHART_BUFFER, CHART_BUFFER + ascent), t,
			HORIZONTAL_ALIGNMENT_LEFT, -1, CHART_FONT_SIZE, Color.WHITE)
	var box := func(t: String) -> Vector2:
		return font.get_string_size(t, HORIZONTAL_ALIGNMENT_LEFT, -1, CHART_FONT_SIZE) + Vector2(2, 2) * CHART_BUFFER
	for i in (1 if b[3] else 3):
		text.call(format_axis(rt.y - (rt.y - lb.y) / 3.0 * i, chart_format[n]), rect.position + Vector2(0, rect.size.y / 3.0 * i))
	for i in (1 if b[2] else 3):
		var t := format_axis(rt.x - (rt.x - lb.x) / 3.0 * i, "DURATION_SHORT")
		text.call(t, rect.end - box.call(t) - Vector2(rect.size.x / 3.0 * i, 0))

## A series' runs of finite points, split where a point is infinite (CChart::Draw).
static func chart_lines(points: PackedVector2Array) -> Array:
	var out := []
	var cur := PackedVector2Array()
	for pt in points:
		if is_inf(pt.x) or is_inf(pt.y):
			if not cur.is_empty():
				out.append(cur)
			cur = PackedVector2Array()
		else:
			cur.append(pt)
	if not cur.is_empty():
		out.append(cur)
	return out

# --- Buttons and hotkeys -------------------------------------------------------

## continueButton.
func continue_pressed() -> void:
	if gui.get("isInGame", false):
		visible = false
		for c in get_children():
			remove_child(c)
			c.queue_free()
		if _paused_by_me:
			_paused_by_me = false
			get_tree().paused = false
		return
	if gui.get("fromReplayMenu", false):
		queue_free()
		return
	get_tree().paused = false
	if _lobby_signed_in(): # Engine.HasXmppClient()
		MatchSettings.go("res://scenes/menu/lobby.tscn")
	elif gui.get("isReplay", false):
		MatchSettings.go("res://scenes/menu/replay_browser.tscn")
	elif MatchSettings.campaign_run != "":
		leave_campaign(_victory)
	else:
		MatchSettings.go("res://scenes/menu/main_menu.tscn")

func _lobby_signed_in() -> bool:
	var lobby := get_tree().root.get_node_or_null("LobbySession")
	return lobby != null and lobby.signed_in()

## The lobby button: RTS0's lobby is a page, not a dialog, so it is opened in place of the summary.
func open_lobby() -> void:
	if not _lobby_signed_in():
		return
	get_tree().paused = false
	MatchSettings.go("res://scenes/menu/lobby.tscn")

## 0 A.D. session CampaignSession.onFinish and campaigns/default_menu/endgame: the
## level is completed the moment the player wins it, before the summary (the main
## node calls this once), so loading a save or quitting afterwards keeps it.
static func mark_campaign_won() -> void:
	if MatchSettings.campaign_run == "" or MatchSettings.campaign_level == "":
		return
	var run := CampaignRun.new(MatchSettings.campaign_run)
	if run.load_run():
		run.mark_level_complete(MatchSettings.campaign_level)

## Back to the run's menu after the match (a level won is completed again here,
## for a match won before mark_campaign_won existed). A replay of a campaign level
## also ends on the run's menu (session.js leaveGame) but completes nothing:
## CampaignSession.onFinish returns when the viewer is not one of the players.
static func leave_campaign(victory: bool, replaying := false) -> void:
	var run := CampaignRun.new(MatchSettings.campaign_run)
	if run.load_run():
		if victory and not replaying:
			run.mark_level_complete(MatchSettings.campaign_level)
		run.set_current()
	MatchSettings.campaign_run = ""
	MatchSettings.campaign_level = ""
	MatchSettings.go("res://scenes/menu/campaigns.tscn")

## startReplay.
func start_replay() -> void:
	var dir := str(gui.get("replayDirectory", ""))
	if dir == "" or not FileAccess.file_exists(dir.path_join("commands.rts0rec")):
		push_warning("Replay file not found!")
		return
	get_tree().paused = false
	MatchSettings.replay_dir = dir
	MatchSettings.load_save_path = ""
	MatchSettings.go("res://scenes/main.tscn")

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

func _input(event: InputEvent) -> void:
	if not visible:
		return
	if _fired(event, "summary") or (gui.get("isInGame", false) and event is InputEventKey and event.pressed
			and "cancel" in RtsoHotkeyConfig.new().fired(event)):
		get_viewport().set_input_as_handled()
		continue_pressed()
	elif _fired(event, "lobby") and not gui.get("isInGame", false) and _lobby_signed_in():
		get_viewport().set_input_as_handled() # summary.xml lobbyButton hotkey="lobby"
		open_lobby()
	elif _fired(event, "tab.next") or _fired(event, "tab.prev"):
		get_viewport().set_input_as_handled()
		var n := panels.size() + 1
		select_panel((panel_index + (1 if _fired(event, "tab.next") else n - 1)) % n)
