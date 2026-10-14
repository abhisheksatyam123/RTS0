extends RefCounted
## 0 A.D. 0.28 gui/common/gamedescription.js getGameDescription: one
## "[color=orange]Label[/color] details" line per match setting, as the
## in-game Match Settings dialog shows them. `info` holds the match's settings
## (RtsoMain.get_match_settings_info: the running simulation plus its Game Setup).

const SetupPage = preload("res://scenes/menu/gamesetup.gd")
const ReplayPage = preload("res://scenes/menu/replay_browser.gd")
const SummaryPage = preload("res://scenes/session/summary_screen.gd")

const HIGHLIGHT := "orange" # g_DescriptionHighlight

# rtso::VictoryKind bits (sim_types.h): the match's set of victory conditions.
const VK_CONQUEST := 1
const VK_CONQUEST_UNITS := 2
const VK_CONQUEST_STRUCTURES := 4
const VK_CONQUEST_CIVIC_CENTRES := 8
const VK_WONDER := 16
const VK_RELIC := 32
const VK_REGICIDE := 64

# simulation/data/settings/victory_conditions/*.json Title and Description.
const CONQUEST := ["Conquest", "Defeat opponents by killing all their units and destroying all their structures."]
const CONQUEST_UNITS := ["Conquest Units", "Defeat opponents by killing all their units."]
const CONQUEST_STRUCTURES := ["Conquest Structures", "Defeat opponents by destroying all their structures."]
const CONQUEST_CIVIC_CENTRES := ["Conquest Civic Centers", "Defeat opponents by destroying all their fully built civic centers."]
const REGICIDE := ["Regicide", "Defeat opponents by killing their hero."]
const WONDER_DESC := "Be the first to build or capture a Wonder and keep it for a certain time to win the game. The timer will be reset when the Wonder is destroyed or captured."
const RELIC_DESC := "Capture all relics spread across the map and keep them for a certain time to win the game."

# settings/population_capacities.json Name, Title, CapTitle.
const POP_PLAYER := ["Player Population", "Population Cap"]
const POP_TEAM := ["Team Population", "Team Population Cap"]
const POP_WORLD := ["World Population", "World Population Cap"]
const TEAM_POP_FLAG := 100000 # Simulation::TEAM_POP_FLAG: pop_cap <= -flag is a Team Population
const UNLIMITED_POP := 10000

## 0.28 messages.js g_PlayerStateMessages, for the dialog's player-state line.
const PLAYER_STATE_MESSAGES := {"won": "You have won!", "defeated": "You have been defeated!"}

static func _minutes(n: int, what: String) -> String:
	return "%s (%d %s)" % [what, n, "minute" if n == 1 else "minutes"]

## The 0.28 map type ("random", "skirmish" or "scenario") and its title.
static func map_type(map_id: String) -> String:
	if map_id.begins_with("random_") or map_id.begins_with("procedural_"):
		return "random"
	var known: Dictionary = SetupPage._map_types_data().get("maps", {})
	return str(known.get(map_id, {}).get("type", "skirmish"))

static func _map_type_title(type: String) -> String:
	for t in SetupPage.MAP_TYPES:
		if t[0] == type:
			return t[1]
	return type.capitalize()

## The scenario's own per-player settings (map_types.json players[]).
## The player placements a random map offers (Game Setup's list for it).
static func map_placements(map_id: String) -> Array:
	if map_id in SetupPage.OWN_RMS_MAPS or not (SetupPage.PLACEMENT_EVERY_MAP or map_id in SetupPage.PLACEMENT_MAPS_BEFORE):
		return []
	var placements: Dictionary = SetupPage._map_types_data().get("player_placements", {})
	return SetupPage.placement_values(placements.get(SetupPage.rms_script(map_id), []))

static func _scenario_players(map_id: String) -> Array:
	var known: Dictionary = SetupPage._map_types_data().get("maps", {})
	return known.get(map_id, {}).get("settings", {}).get("players", [])

static func titles(info: Dictionary) -> Array:
	var out: Array = []
	# One line per condition in g_VictoryConditions (GUIOrder) order.
	var vk := int(info.get("victory_mask", VK_CONQUEST))
	if vk == 0:
		out.append(["Endless Game", "No winner will be determined, even if everyone is defeated."])
	if vk & VK_CONQUEST:
		out.append(CONQUEST)
	if vk & VK_CONQUEST_STRUCTURES:
		out.append(CONQUEST_STRUCTURES)
	if vk & VK_CONQUEST_UNITS:
		out.append(CONQUEST_UNITS)
	if vk & VK_CONQUEST_CIVIC_CENTRES:
		out.append(CONQUEST_CIVIC_CENTRES)
	if vk & VK_RELIC:
		out.append([_minutes(int(info.get("relic_min", 0)), "Capture the Relic"), RELIC_DESC])
		out.append(["Relic Count", int(info.get("relic_count", 0))])
	if vk & VK_REGICIDE:
		out.append(REGICIDE)
		if bool(info.get("regicide_garrison", false)):
			out.append(["Hero Garrison", "Heroes can be garrisoned."])
		else:
			out.append(["Exposed Heroes", "Heroes cannot be garrisoned and they are vulnerable to raids."])
	if vk & VK_WONDER:
		out.append([_minutes(int(info.get("wonder_min", 0)), "Wonder"), WONDER_DESC])

	if bool(info.get("lock_teams", false)):
		out.append(["Locked Teams", "Players can't change the initial teams."])
	else:
		out.append(["Diplomacy", "Players can make alliances and declare war on allies."])
	if bool(info.get("last_man_standing", false)):
		out.append(["Last Man Standing", "Only one player can win the game. If the remaining players are allies, the game continues until only one remains."])
	else:
		out.append(["Allied Victory", "If one player wins, his or her allies win too. If one group of allies remains, they win."])

	var ceasefire := int(info.get("ceasefire_min", 0))
	out.append(["Ceasefire", "disabled" if ceasefire == 0
		else "For the first minute, other players will stay neutral." if ceasefire == 1
		else "For the first %d minutes, other players will stay neutral." % ceasefire])

	var map_id := str(info.get("map_id", ""))
	var type := map_type(map_id)
	out.append(["Map Name", str(info.get("map_name", map_id)).trim_suffix(" (RMS)")])
	out.append(["Map Description", str(ReplayPage.map_info(map_id).get("desc", ""))])
	out.append(["Map Type", _map_type_title(type)])
	if type == "random":
		var size: String = SummaryPage.MAP_SIZE_NAMES.get(int(info.get("map_size", 0)), "")
		if size != "":
			out.append(["Map Size", size])
		if not map_placements(map_id).is_empty():
			var tp: Array = SetupPage.TEAM_PLACEMENTS[clampi(int(info.get("team_placement", 0)), 0, SetupPage.TEAM_PLACEMENTS.size() - 1)]
			out.append([tp[1], tp[2]] if int(info.get("team_placement", 0)) > 0
				else ["Random Player Placement", "Randomly select a player placement from the list."])
		if SetupPage.TRIGGER_DIFFICULTY_MAPS.has(map_id):
			out.append(SetupPage.TRIGGER_DIFFICULTIES[clampi(int(info.get("trigger_difficulty", 3)) - 1, 0, 4)])
		var nomad := bool(info.get("nomad", false))
		out.append(["Nomad Mode", "Players start with only few units and have to find a suitable place to build their city."] if nomad
			else ["Civic Centers", "Players start with a Civic Center."])

	var players := _scenario_players(map_id) if type == "scenario" else []
	var start_res := clampi(int(info.get("start_res", 1)), 0, SetupPage.START_RES.size() - 1)
	out.append(["Starting Resources", "Per Player" if players.any(func(p): return p is Dictionary and p.has("resources"))
		else SetupPage.START_RES[start_res]])

	# Simulation::pop_cap_setting: > 0 per player, 0 unlimited, < 0 World, <= -TEAM_POP_FLAG Team.
	var pop_cap := int(info.get("pop_cap", 0))
	var cap_type: Array = POP_TEAM if pop_cap <= -TEAM_POP_FLAG else POP_WORLD if pop_cap < 0 else POP_PLAYER
	out.append(["Population Cap Type", cap_type[0]])
	var cap := (-pop_cap - TEAM_POP_FLAG) if pop_cap <= -TEAM_POP_FLAG else absi(pop_cap)
	if cap == 0:
		cap = UNLIMITED_POP
	out.append([cap_type[1], "Per Player" if players.any(func(p): return p is Dictionary and p.has("pop"))
		else str(cap) if cap < UNLIMITED_POP else "Unlimited"])

	out.append(["Treasures", "Disabled" if bool(info.get("disable_treasures", false)) else "As defined by the map."])
	out.append(["Explored Map", bool(info.get("explore_map", false))])
	out.append(["Revealed Map", bool(info.get("reveal_map", false))])
	out.append(["Allied View", bool(info.get("ally_view", false))])
	out.append(["Cheats", bool(info.get("cheats", false))])
	return out

## The dialog text: "label details" lines, a true value reading "enabled" and
## a false or empty one "disabled".
static func describe(info: Dictionary) -> String:
	var lines: PackedStringArray = []
	for t in titles(info):
		var v: Variant = t[1]
		var details: String = "enabled" if v is bool and v else \
			"disabled" if (v is bool and not v) or str(v) == "" else str(v)
		lines.append("[color=%s]%s[/color] %s" % [HIGHLIGHT, t[0], details])
	return "\n".join(lines)
