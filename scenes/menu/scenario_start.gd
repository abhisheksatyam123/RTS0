extends RefCounted
## Starts a scenario map with its own players, as 0 A.D. does for campaign levels
## (startScenario) and Learn to Play's Tutorial (page_autostart): the local player
## in the first seat, AI in the others, the map's civs, cheats on.

## The maps 0 A.D. keeps in maps/tutorials: reached from Learn to Play and the
## Tutorial campaign, not listed in game setup (whose Scenario type is maps/scenarios).
const TUTORIAL_MAPS := ["introductory_tutorial", "starting_economy_walkthrough"]
const INTRODUCTORY_TUTORIAL := "introductory_tutorial"

## Fills MatchSettings for map_id. Returns false when the map is missing.
static func configure(ms: Node, map_id: String) -> bool:
	var p := "res://data/maps/%s.json" % map_id
	if not FileAccess.file_exists(p):
		push_error("[ScenarioStart] Map %s missing." % map_id)
		return false
	var f := FileAccess.open(p, FileAccess.READ)
	var parsed: Variant = JSON.parse_string(f.get_as_text()) if f else null
	var players: Array = parsed.get("players", []) if typeof(parsed) == TYPE_DICTIONARY else []
	var n := maxi(1, players.size())
	var types: Array = []
	var civs: Array = []
	for i in n:
		types.append(0 if i == 0 else 1)
		civs.append(int(players[i].get("civ", 0)) if i < players.size() and typeof(players[i]) == TYPE_DICTIONARY else 0)
	ms.map_id = map_id
	ms.seed = 0
	ms.seed_exact = false
	ms.ai_seed = 0
	ms.victory = 0
	ms.waves_enabled = false
	ms.cheats = true
	ms.player_count = n
	ms.slot_types = types
	ms.slot_civs = civs
	ms.slot_teams = range(n).map(func(_i): return 0)
	ms.slot_names = []
	ms.slot_colors = []
	ms.player_civ = civs[0]
	ms.ai_civ = civs[1] if n > 1 else civs[0]
	ms.campaign_run = ""
	ms.campaign_level = ""
	ms.campaign_map_name = ""
	return true
