extends RefCounted
## 0 A.D. 0.28 gui/common/gamedescription.js getGameDescription: one
## "[color=orange]Label[/color] details" line per match setting, as the
## in-game Match Settings dialog shows them. `info` holds the match's settings
## (RtsoMain.get_match_settings_info: the running simulation plus its Game Setup).

const SetupPage = preload("res://scenes/menu/gamesetup.gd")
const ReplayPage = preload("res://scenes/menu/replay_browser.gd")
const SummaryPage = preload("res://scenes/session/summary_screen.gd")
const L10n := preload("res://scenes/menu/l10n.gd")

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

# maps/random/rmbiome/generic/*.json Description Title and Description, by Game Setup BIOMES index.
const BIOME_TEXT := {
	1: ["Aegean-Anatolian", "Start in a region blessed with the Mediterranean climate, a warm and inviting land. The cypresses are in a perpetual struggle with the dominant fan palms while deer graze in their shadows, blissfully unaware."],
	3: ["Subalpine", "Between the high summits of the Alps, the valleys are filled with fog in the early morning. The ground is full of gravel and rocks, but silver fir and spruce trees grow between them and provide shelter for deer and mountain goats."],
	4: ["Sahara", "A semi-desert area that boasts numerous clumps of date palms and acacia trees. Herds of camels roam the wild and the occasional gazelle jumps up in fright at being disturbed."],
	5: ["Temperate", "Lush grasslands carpet the land, in places giving way to magnificent and diverse broadleaf forests. Poplars, pines, beeches and oaks all vie for supremacy but this root-war is a never-ending struggle. Numerous apple trees dot the land, deer and sheep gorge themselves on this years fallen fruit."],
	6: ["Eurasian Steppe", "Wide grasslands stretching to the horizon, without any trees blocking the view. The Steppe is home to large herds of wild horses, which graze peacefully on the empty land, but flee quickly if you try to catch them."],
	7: ["Sudanian Savanna", "The transition between the dry Sahara to the north and the tropical forests to the south. The sunlight floods through the open canopy and lights the grassland between the trees, where herds of zebras, wildebeest, giraffes and elephants roam."],
	8: ["Arctic", "A region in the high snowy mountains. Biting winds sweep through the abundant conifer forests, making even the more resilient deer and mountain goats shiver."],
	9: ["India", "Explore the mysterious tropics. An extremely green, but also extremely humid environment awaits. The tall Toona trees look on disapprovingly at this invasion of their privacy and ferocious tigers are determined to defend their territory at all costs."],
	10: ["Rhine Valley (Fall)", "The first leaves have fallen. The landscape is a dazzling dash of colors irrevocably intertwined. The native beech and oak trees of this temperate zone display a multi-colored foliage while animals try to prepare for the approaching winter."],
	11: ["Nubia", "Nubia, a dry climate in which only the hardy Baobab trees thrive. Solitary gazelles graze the sparse grass, while herds of zebras, wildebeest, giraffes or elephants roam the wild in search of food."],
}
const RATING_DESC := "When the winner of this match is determined, the lobby score will be adapted."

## 0.28 messages.js g_PlayerStateMessages, for the dialog's player-state line.
const PLAYER_STATE_MESSAGES := {"won": "You have won!", "defeated": "You have been defeated!"}

## translatePlural("Wonder (%(min)s minute)", "Wonder (%(min)s minutes)", n).
static func _minutes(n: int, what: String) -> String:
	return L10n.sprintf(L10n.t(what + (" (%(min)s minute)" if n == 1 else " (%(min)s minutes)")), {"min": n})

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
		else L10n.sprintf(L10n.t("For the first %(min)s minutes, other players will stay neutral."), {"min": ceasefire})])

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
		# Biome: the Game Setup pick when the map's SupportedBiomes offer it; Random until
		# Start resolves it (0 keeps the map's own and has no line).
		var biome := int(info.get("biome", 0))
		var offered: Array = SetupPage.biome_values(SetupPage._map_types_data().get("random_biomes", {}).get(SetupPage.rms_script(map_id), []))
		if biome > 0 and BIOME_TEXT.has(biome) and offered.has(biome) and not map_id in SetupPage.OWN_RMS_MAPS:
			out.append(BIOME_TEXT[biome])
		elif biome == SetupPage.RANDOM_BIOME and not offered.is_empty() and not map_id in SetupPage.OWN_RMS_MAPS:
			out.append(["Random Biome", "Randomly select a biome from the list."])
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
	# Rating.js RatingEnabled: a rated 1v1 says so; any other lobby game reads disabled.
	var rated := bool(info.get("rated", false))
	if rated and int(info.get("player_count", 0)) == 2:
		out.append(["Rated game", RATING_DESC])
	elif bool(info.get("lobby_game", false)):
		out.append(["Rated game", RATING_DESC if rated else false])
	return out

## The dialog text: translated "%(label)s %(details)s" lines, a true value
## reading "enabled" and a false or empty one "disabled". Titles, descriptions
## and map names are translated as 0.28 translates them; formatted ones
## (_minutes, the ceasefire line) already are, and translating them again
## returns them unchanged.
static func describe(info: Dictionary) -> String:
	var lines: PackedStringArray = []
	var fmt := L10n.t("%(label)s %(details)s")
	for t in titles(info):
		var v: Variant = t[1]
		var details: String = L10n.t("enabled") if v is bool and v else \
			L10n.t("disabled") if (v is bool and not v) or str(v) == "" else L10n.t(str(v))
		lines.append(L10n.sprintf(fmt, {"label": "[color=%s]%s[/color]" % [HIGHLIGHT, L10n.t(str(t[0]))], "details": details}))
	return "\n".join(lines)
