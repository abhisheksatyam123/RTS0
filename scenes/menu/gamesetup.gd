extends Control
## Game Setup — mirrors 0 A.D. gui/page_gamesetup.xml:
## map filter + map list + description/players/size preview + seed,
## per-player slot rows (Human/AI/Closed, civ, team, difficulty),
## match options (victory, starting resources). Everything is persisted
## to the MatchSettings autoload; Start validates then loads main.tscn,
## whose _ready() applies the setup via load_map_seeded + sim setters.
##
## Sim constraints honored (see scratchpad/todo.md GameSetup section):
## - victory / petra difficulty / behavior / starting-res have sim setters
## - NO popcap / reveal / nomad / ceasefire setters exist -> not shown
## - Petra only drives team 1, RMS spawns team-per-spawn-index -> slots > 4
##   still spawn (base+units) but have no AI economy; teams beyond 2 share
##   teams 0/1 via diplomacy defaults. Slot cap is 8 like 0 A.D.

const CIVS := [
	{"code": "athen", "name": "Athenians", "emblem": "res://assets/textures/ui/emblems/athenians.png", "blurb": "Silver Owls: +15% metal gather. Balanced hoplites and a strong navy."},
	{"code": "spart", "name": "Spartans", "emblem": "res://assets/textures/ui/emblems/spartans.png", "blurb": "Agoge: +35% HP and +25% melee for citizen-soldiers and champions."},
	{"code": "pers", "name": "Persians", "emblem": "res://assets/textures/ui/emblems/persians.png", "blurb": "Achaemenid trade: +25% market gain, faster cavalry production."},
	{"code": "rome", "name": "Romans", "emblem": "res://assets/textures/ui/emblems/romans.png", "blurb": "Legionary discipline: +30% build rate, heavy legionary armor."},
	{"code": "cart", "name": "Carthaginians", "emblem": "res://assets/textures/ui/emblems/carthaginians.png", "blurb": "Naval mastery and war elephants. Elite mercenary forces."},
	{"code": "mace", "name": "Macedonians", "emblem": "res://assets/textures/ui/emblems/macedonians.png", "blurb": "Sarissa phalanx and companion cavalry shock charges."},
	{"code": "brit", "name": "Britons", "emblem": "res://assets/textures/ui/emblems/britons.png", "blurb": "Celtic chariot raiders, war dogs, and rotary mills."},
	{"code": "gaul", "name": "Gauls", "emblem": "res://assets/textures/ui/emblems/gauls.png", "blurb": "Carnyx morale aura and fearless broadsword champions."},
	{"code": "iber", "name": "Iberians", "emblem": "res://assets/textures/ui/emblems/iberians.png", "blurb": "Balearic slingers, fast cavalry, +20% mining rate."},
	{"code": "maur", "name": "Mauryans", "emblem": "res://assets/textures/ui/emblems/mauryans.png", "blurb": "Worker elephants, archer maidens, high economic stamina."},
	{"code": "kush", "name": "Kushites", "emblem": "res://assets/textures/ui/emblems/kushites.png", "blurb": "Bleeding and poison arrows. Nubian archers, pyramid shrines."},
	{"code": "ptol", "name": "Ptolemies", "emblem": "res://assets/textures/ui/emblems/ptolemies.png", "blurb": "Mercenary camps, camel raiders, +30% trade gain."},
	{"code": "sele", "name": "Seleucids", "emblem": "res://assets/textures/ui/emblems/seleucids.png", "blurb": "Silver shields, cataphracts, and scythed chariots."},
	{"code": "han", "name": "Han Chinese", "emblem": "res://assets/textures/ui/emblems/han.png", "blurb": "Crossbowmen, war chariots and imperial ministers."},
]

# (map_id, display name, kind, players, size_m, biome, description)
# Scenario JSONs are read live for desc/size/players; procedural RMS
# entries come from Simulation::get_available_maps order.
const RMS_MAPS := [
	["random_continental", "Continental (RMS)", "Random", 8, 128, "mediterranean", "A grand landmass with winding rivers and rolling resource woodlands. Supports 2-8 players."],
	["random_archipelago", "Archipelago (RMS)", "Random", 8, 128, "aegean", "Deep-water island cluster: docks and naval supremacy required. Supports 2-8 players."],
	["random_oasis", "Oasis (RMS)", "Random", 8, 128, "oasis", "Forested desert borders around a lush central lake basin. Supports 2-8 players."],
	["random_highlands", "Highlands (RMS)", "Random", 8, 128, "alpine", "Rugged alpine terrain with cliffs, gorge rivers, and passes. Supports 2-8 players."],
	["random_valley", "Fertile Valley (RMS)", "Random", 8, 128, "latium", "Lush river valley with balanced contested resources. Supports 2-8 players."],
	["random_mediterranean", "Mediterranean Sea (RMS)", "Random", 8, 128, "mediterranean", "Central sea ringed by fertile mainland coasts. Supports 2-8 players."],
	["random_acropolis", "Acropolis Heights (RMS)", "Random", 8, 128, "mediterranean", "Elevated central plateaus over resource valleys. Supports 2-8 players."],
	["random_spartan_valley", "Spartan Valley (RMS)", "Random", 2, 128, "alpine", "Narrow pass for phalanx warfare and choke points. Best with 2 players."],
	["random_danubius", "Danubius (RMS)", "Random", 4, 128, "temperate", "Meandering dual-channel river with fords and a central island. Supports 2-4 players."],
	["random_saharan_oases", "Saharan Oases (RMS)", "Random", 8, 128, "oasis", "Arid dunes, date palm groves, and freshwater oases. Supports 2-8 players."],
	["random_anatolian_plateau", "Anatolian Plateau (RMS)", "Random", 8, 128, "steppe", "High plateau steppe with canyon ravines and rich minerals. Supports 2-8 players."],
	["random_jebel_barkal", "Jebel Barkal (RMS)", "Random", 8, 128, "savanna", "Central sandstone mesa with high-ground advantage. Supports 2-8 players."],
	["random_mainland", "Mainland (RMS)", "Random", 8, 128, "mediterranean", "Open rolling land with no water barriers. Supports 2-8 players."],
	["random_alpine_lakes", "Alpine Lakes (RMS)", "Random", 8, 128, "alpine", "Ridged mountains cradling cold lakes. Supports 2-8 players."],
	["random_lake", "Lake (RMS)", "Random", 8, 128, "temperate", "A great central lake ringed by the players. Supports 2-8 players."],
	["random_gulf_of_bothnia", "Gulf of Bothnia (RMS)", "Random", 8, 128, "alpine", "A northern gulf cuts in from one edge; players around its shores. Supports 2-8 players."],
	["random_ardennes_forest", "Ardennes Forest (RMS)", "Random", 8, 128, "temperate", "Dense hilly forest with clearings for each base. Supports 2-8 players."],
	["random_nearestern_badlands", "Near-Eastern Badlands (RMS)", "Random", 8, 128, "oasis", "Eroded desert canyons with a few oases and rich mines. Supports 2-8 players."],
	["random_rivers", "Rivers (RMS)", "Random", 8, 128, "temperate", "Rivers run from a central lake out between the players, crossed at fords. Supports 2-8 players."],
	["random_canyon", "Canyon (RMS)", "Random", 8, 128, "oasis", "Each base sits in a gorge leading to a central basin. Supports 2-8 players."],
	["random_corinthian_isthmus", "Corinthian Isthmus (RMS)", "Random", 8, 128, "mediterranean", "Two lands joined by a narrow isthmus between two gulfs. Supports 2-8 players."],
	["random_belgian_uplands", "Belgian Uplands (RMS)", "Random", 8, 128, "temperate", "Steep wooded hills and valleys with no water. Supports 2-8 players."],
	["random_lorraine_plain", "Lorraine Plain (RMS)", "Random", 8, 128, "temperate", "A flat plain split by a river with a few fords. Supports 2-8 players."],
	["random_hyrcanian_shores", "Hyrcanian Shores (RMS)", "Random", 8, 128, "temperate", "A sea along one edge, a coastal strip and wooded highlands. Supports 2-8 players."],
	["random_northern_lights", "Northern Lights (RMS)", "Random", 8, 128, "polar", "Snowy land with a frozen sea and ice floes; scarce wood. Supports 2-8 players."],
	["random_kerala", "Kerala (RMS)", "Random", 8, 128, "india", "India's south-western shore, between the sea and the mountains. Supports 2-8 players."],
	["random_flood", "Flood (RMS)", "Random", 8, 128, "temperate", "A great flood moves across the valley. The home islands go under, so evacuate your units by ship to the mainland. Supports 2-8 players."],
	["random_extinct_volcano", "Extinct Volcano (RMS)", "Random", 8, 128, "volcanic", "Rains raise the water from the 25th minute until only the hills and the great cone stay dry. Gaia towers guard the lowlands. Supports 2-8 players."],
	["random_polar_sea", "Polar Sea (RMS)", "Random", 8, 128, "polar", "Icy land round a central sea, hardly any trees. Supports 2-8 players."],
	["random_sahel", "Sahel (RMS)", "Random", 8, 128, "savanna", "Open savanna with a few water holes; little wood. Supports 2-8 players."],
	["random_african_plains", "African Plains (RMS)", "Random", 8, 128, "savanna", "Lush plains with watering holes, hills and wildlife. Supports 2-8 players."],
	["random_alpine_valley", "Alpine Valley (RMS)", "Random", 8, 128, "alpine", "Deep valleys walled off by mountain ranges with passes. Supports 2-8 players."],
	["random_syria", "Syria (RMS)", "Random", 8, 128, "desert", "Dry plains with slightly rolling highlands. Supports 2-8 players."],
	["random_cantabrian_highlands", "Cantabrian Highlands (RMS)", "Random", 8, 128, "temperate", "Each player starts on a cliff-ringed hill with one ramp. Supports 2-8 players."],
	["random_pyrenean_sierra", "Pyrenean Sierra (RMS)", "Random", 8, 128, "alpine", "High mountains with two passes separate the teams. Supports 2-8 players."],
	["random_sahel_watering_holes", "Sahel Watering Holes (RMS)", "Random", 8, 128, "savanna", "Lines of water with shallows between the players. Supports 2-8 players."],
	["random_persian_highlands", "Persian Highlands (RMS)", "Random", 8, 128, "persia", "A dry sunken plateau rich in minerals, ringed by rocky hills. Supports 2-8 players."],
	["random_volcanic_lands", "Volcanic Lands (RMS)", "Random", 8, 128, "volcanic", "A charred land around a smoking volcano. Supports 2-8 players."],
	["random_lions_den", "Lion's Den (RMS)", "Random", 8, 128, "steppe", "Cliff-ringed dens with ramps down to a sunken valley. Supports 2-8 players."],
	["random_hells_pass", "Hell's Pass (RMS)", "Random", 8, 128, "alpine", "Mountain spines between the players, with narrow passes. Supports 2-8 players."],
	["random_deep_forest", "Deep Forest (RMS)", "Random", 8, 128, "temperate", "A deep forest crossed by paths between the bases. Supports 2-8 players."],
	["random_india", "India (RMS)", "Random", 8, 128, "india", "Parched land round one half dried-up lake. Supports 2-8 players."],
	["random_guadalquivir_river", "Guadalquivir River (RMS)", "Random", 8, 128, "mediterranean", "A coast split by a river with three shallows. Supports 2-8 players."],
	["random_fields_of_meroe", "Fields of Meroe (RMS)", "Random", 8, 128, "savanna", "The Nile with two cataract crossings, dunes and acacias. Supports 2-8 players."],
	["random_gear", "Gear (RMS)", "Random", 8, 128, "temperate", "Waterways like a spider web round a central lake. Supports 2-8 players."],
	["random_atlas_mountains", "Atlas Mountains (RMS)", "Random", 8, 128, "desert", "Rugged mountains with little room and scarce wood. Supports 2-8 players."],
	["random_frontier", "Frontier (RMS)", "Random", 8, 128, "temperate", "A random wild landscape of hills, plateaus and lakes. Supports 2-8 players."],
	["random_wild_lake", "Wild Lake (RMS)", "Random", 8, 128, "temperate", "Rough wild terrain round a lake. Supports 2-8 players."],
	["random_stronghold", "Stronghold (RMS)", "Random", 8, 128, "temperate", "Teammates start side by side and defend together. Supports 2-8 players."],
	["random_caledonian_meadows", "Caledonian Meadows (RMS)", "Random", 8, 128, "alpine", "Fertile meadows amid harsh terrain and rivers. Supports 2-8 players."],
	["random_rhine_marshlands", "Rhine Marshlands (RMS)", "Random", 8, 128, "temperate", "Shallow wetlands units can wade through. Supports 2-8 players."],
	["random_red_sea", "Red Sea (RMS)", "Random", 8, 128, "desert", "The Red Sea coasts of Africa and Arabia from a real elevation map. Supports 2-8 players."],
	["random_hellas", "Hellas (RMS)", "Random", 8, 128, "mediterranean", "A random part of Greece from a real elevation map. Supports 2-8 players."],
	["random_english_channel", "English Channel (RMS)", "Random", 8, 128, "temperate", "Two coasts split by the Channel; ships needed to cross. Supports 2-8 players."],
	["random_dodecanese", "Dodecanese (RMS)", "Random", 8, 128, "mediterranean", "Aegean islands, a few joined by bridges. Supports 2-8 players."],
	["random_fortress", "Fortress (RMS)", "Random", 8, 128, "temperate", "Every player starts inside a walled fortress. Supports 2-8 players."],
	["random_aegean_sea", "Aegean Sea (RMS)", "Random", 8, 128, "aegean", "Two coasts across a sea with scattered islands; ships needed to cross. Supports 2-8 players."],
	["random_archipelago_0ad", "Archipelago, 0 A.D. (RMS)", "Random", 8, 128, "temperate", "A maze of islands of different sizes and shapes. Supports 2-8 players."],
	["random_islands", "Islands (RMS)", "Random", 8, 128, "temperate", "Each player starts on a small island with a dock, among many others. Supports 2-8 players."],
	["random_cycladic_archipelago", "Cycladic Archipelago (RMS)", "Random", 8, 128, "aegean", "Each player starts on an island of the Cyclades. Supports 2-8 players."],
	["random_snowflake_searocks", "Snowflake Searocks (RMS)", "Random", 8, 128, "temperate", "Small rocky islands joined by narrow causeways. Supports 2-8 players."],
	["random_harbor", "Harbor (RMS)", "Random", 8, 128, "temperate", "A sheltered harbor for each player, cliffs and a deep central sea. Supports 2-8 players."],
	["random_corsica", "Corsica vs Sardinia (RMS)", "Random", 8, 128, "mediterranean", "Two opposing islands with jagged relief; ships needed to land. Supports 2-8 players."],
	["random_phoenician_levant", "Phoenician Levant (RMS)", "Random", 8, 128, "mediterranean", "Players start in the east, a great sea and Cyprus to the west. Supports 2-8 players."],
	["random_marmara", "Marmara (RMS)", "Random", 8, 128, "aegean", "The Sea of Marmara between Thrace and Asia Minor from a real elevation map. Supports 2-8 players."],
	["random_bahrain", "Bahrain (RMS)", "Random", 8, 128, "desert", "Arabian coasts round the island of Bahrain from a real elevation map; ships needed. Supports 2-8 players."],
	["random_botswanan_haven", "Botswanan Haven (RMS)", "Random", 8, 128, "savanna", "Wet-season savanna with hills and shallow marsh pools. Supports 2-8 players."],
	["random_scythian_rivulet", "Scythian Rivulet (RMS)", "Random", 8, 128, "polar", "Snowy land either side of a shallow frozen rivulet. Supports 2-8 players."],
	["random_ambush", "Ambush (RMS)", "Random", 8, 128, "temperate", "High bluffs with rich resources overlook the plain. Supports 2-8 players."],
	["random_arctic_summer", "Arctic Summer (RMS)", "Random", 8, 128, "polar", "Northern summer plains with hills, lakes and plenty of game. Supports 2-8 players."],
	["random_empire", "Empire (RMS)", "Random", 8, 128, "mediterranean", "Every player starts with two civic centres. Supports 2-8 players."],
	["random_schwarzwald", "Schwarzwald (RMS)", "Random", 8, 128, "temperate", "Heavy forest round a central lake. Supports 2-8 players."],
	["random_ratumacos", "Ratumacos (RMS)", "Random", 8, 128, "alpine", "Both banks of the Seine near Ratumacos (Rouen). Supports 2-8 players."],
	["random_oasis_0ad", "Oasis (0 A.D.) (RMS)", "Random", 8, 128, "oasis", "Desert round a central oasis that holds most of the wood. Supports 2-8 players."],
	["random_mediterranean_0ad", "Mediterranean (RMS)", "Random", 8, 128, "mediterranean", "The Mediterranean Sea and its coasts from a real elevation map. Supports 2-8 players."],
	["random_elephantine", "Elephantine (RMS)", "Random", 8, 128, "savanna", "The Nile at its first cataract; ships needed to cross. Supports 2-8 players."],
	["random_island_stronghold", "Island Stronghold (RMS)", "Random", 8, 128, "temperate", "Each team starts packed together on its own island. Supports 2-8 players."],
	["random_pompeii", "Pompeii (RMS)", "Random", 8, 128, "volcanic", "The Bay of Naples under Vesuvius from a real elevation map. Supports 2-8 players."],
	["random_latium", "Latium (RMS)", "Random", 8, 128, "latium", "Rolling Italian land between two seas, with cliffs and plateaus. Supports 2-8 players."],
	["random_unknown", "Unknown (RMS)", "Random", 8, 128, "temperate", "One of 13 land and naval landscapes, picked at random. Supports 2-8 players."],
]

# 0 A.D. population limits per player (0 = unlimited) and ceasefire lengths in minutes.
# 0 A.D. population_capacities.json (default 300), then world_population_capacities.json:
# a negative value is a World Population shared by the players still in the game.
const POP_CAPS := [[50, "50"], [100, "100"], [150, "150"], [200, "200"], [250, "250"], [300, "300"], [0, "Unlimited"],
	[-100, "World 100"], [-200, "World 200"], [-300, "World 300"], [-400, "World 400"], [-500, "World 500"],
	[-600, "World 600"], [-700, "World 700"], [-800, "World 800"], [-900, "World 900"], [-1000, "World 1000"],
	[-1100, "World 1100"], [-1200, "World 1200"], [-1600, "World 1600"], [-2000, "World 2000"], [-2400, "World 2400"]]
const CEASEFIRES := [0, 1, 2, 3, 4, 5, 10, 15, 20, 30, 45, 60]
# 0 A.D. Wonder / Capture the Relic hold times in minutes (default 20) and relic counts (default 2).
const VICTORY_TIMES := [0, 1, 2, 5, 10, 15, 20, 30, 45, 60]
const RELIC_COUNTS := [1, 2, 3, 4, 5, 6, 8, 10, 12, 16]
# 0 A.D. map trigger options: Flood's WaterLevel and Extinct Volcano's SeaLevelRiseTime.
const WATER_LEVELS := ["Rising water", "Shallow water", "Deep water"]
const SEA_RISE_TIMES := [0, 5, 10, 15, 20, 25, 30, 40, 50, 60]
# 0 A.D. Team Placement (gamesettings/attributes/TeamPlacement.js), index ==
# rtso::TeamPlacement; offered by the maps whose JSON lists TeamPlacements.
const TEAM_PLACEMENTS := [["Random", "Select a random team placement pattern when starting the game."],
	["Circle", "Allied players are grouped and placed with opposing players on one circle spanning the map."],
	["Line", "Allied players are placed in a linear pattern."],
	["Random Group", "Allied players are grouped, but otherwise placed randomly on the map."],
	["Stronghold", "Allied players are grouped in one random place of the map."]]
const TEAM_PLACEMENT_MAPS := ["random_ambush", "random_frontier"]

# 0 A.D. map sizes (width in meters) for random maps.
const MAP_SIZES := [[128, "Tiny (128 m)"], [192, "Small (192 m)"], [256, "Medium (256 m)"],
	[320, "Normal (320 m)"], [384, "Large (384 m)"], [448, "Very Large (448 m)"], [512, "Giant (512 m)"]]

# Index == rtso::VictoryCondition id (sim_types.h).
const VICTORIES := ["Conquest — destroy all enemies", "Wonder — hold a Wonder", "Relic — claim and hold all relics", "Endless — no victory, sandbox",
	"Conquest Units — kill every enemy unit", "Conquest Structures — raze every enemy structure", "Regicide — kill the enemy hero, keep yours"]
# 0 A.D. settings/starting_resources.json: the same amount of every resource.
const START_RES := ["Very Low (100)", "Low (300)", "Medium (500)", "High (1000)", "Very High (3000)", "Deathmatch (50000)"]
const DIFFS := ["Sandbox", "Very Easy", "Easy", "Medium", "Hard", "Very Hard"]
# Options-page aibehavior values -> RTS0 PetraBehavior (petra_ai.h).
const AI_BEHAVIOR_IDS := {"random": 0, "defensive": 1, "balanced": 2, "aggressive": 3}
const BEHAVIORS := ["Random", "Defensive", "Balanced", "Aggressive"]
# Random-map biome override (index - 1 == rtso::BiomeType) and 0 A.D. game speeds.
const BIOMES := ["Map default", "Aegean", "Latium", "Alpine", "Oasis", "Temperate", "Steppe", "Savanna", "Polar", "India"]
const SPEEDS := [[0.5, "0.5x"], [0.75, "0.75x"], [1.0, "1x"], [1.25, "1.25x"], [1.5, "1.5x"], [2.0, "2x"]]
const SLOT_KINDS := ["Human", "AI", "Closed"]
# rtso::PLAYER_COLORS: 0 A.D. player_defaults.json (each player's default), then PlayerColor.js DefaultColors.
const PLAYER_COLORS := [Color8(21, 55, 149), Color8(150, 20, 20), Color8(86, 180, 31), Color8(231, 200, 5),
	Color8(50, 170, 170), Color8(160, 80, 200), Color8(220, 115, 16), Color8(64, 64, 64),
	Color8(10, 10, 190), Color8(230, 10, 10), Color8(125, 235, 15), Color8(255, 255, 55),
	Color8(130, 0, 230), Color8(255, 130, 0), Color8(10, 230, 230), Color8(20, 80, 60),
	Color8(220, 160, 220), Color8(80, 255, 190), Color8(50, 150, 255), Color8(100, 150, 30),
	Color8(100, 60, 30), Color8(128, 0, 64), Color8(255, 200, 140), Color8(80, 80, 80)]
const VICTORY_REGICIDE := 6
const CivAINames = preload("res://scenes/menu/civ_ai_names.gd")
var _color_icons: Array = []

var _maps: Array = []          # all map dicts
var _filtered: Array = []      # after filter
var _sel_map: int = 0
var _slot_rows: Array = []     # per-slot {kind,civ,team,diff,beh}
var _rebuilding_slots := false

func _ready() -> void:
	_load_maps()
	_build_map_filter()
	_build_map_list()
	_build_options()
	_restore_settings()
	refresh_map_list()
	refresh_slots()
	_update_preview()

# ---------------------------------------------------------------- maps ---

func _load_maps() -> void:
	_maps.clear()
	var dir := DirAccess.open("res://data/maps")
	if dir != null:
		dir.list_dir_begin()
		var file_name := dir.get_next()
		var scenario_ids: Array = []
		while file_name != "":
			if not dir.current_is_dir() and file_name.ends_with(".json"):
				scenario_ids.append(file_name.get_basename())
			file_name = dir.get_next()
		scenario_ids.sort()
		for sid in scenario_ids:
			_maps.append(_read_scenario(sid))
	if _maps.is_empty():
		for p in ["acropolis_bay", "corinthian_isthmus", "atlas_valleys", "bactria", "battle_for_the_tiber"]:
			_maps.append(_read_scenario(p))
	for r in RMS_MAPS:
		_maps.append({"id": r[0], "name": r[1], "kind": r[2], "players": r[3],
			"size": r[4], "biome": r[5], "desc": r[6]})

func _read_scenario(map_id: String) -> Dictionary:
	var d := {"id": map_id, "name": map_id, "kind": "Scenario",
		"players": 2, "size": 256, "biome": "mediterranean", "desc": ""}
	var path := "res://data/maps/" + map_id + ".json"
	if not FileAccess.file_exists(path):
		return d
	var fa := FileAccess.open(path, FileAccess.READ)
	if fa == null:
		return d
	var parsed: Variant = JSON.parse_string(fa.get_as_text())
	if typeof(parsed) != TYPE_DICTIONARY:
		return d
	d["name"] = str(parsed.get("name", map_id))
	d["biome"] = str(parsed.get("biome", "mediterranean"))
	d["desc"] = str(parsed.get("desc", ""))
	d["size"] = int(float(parsed.get("world_size", 256.0)))
	var pl: Array = parsed.get("players", [])
	if not pl.is_empty():
		d["players"] = pl.size()
	return d

func _make_option(slot: Control) -> RtsoSafeSelect:
	for c in slot.get_children():
		c.queue_free()
	var ob := RtsoSafeSelect.new()
	ob.set_anchors_preset(Control.PRESET_FULL_RECT)
	slot.add_child(ob)
	return ob

func _build_map_filter() -> void:
	var f := _make_option($VBox/HBox/Left/MapFilterSlot)
	f.add_item("All maps")
	f.add_item("Scenarios")
	f.add_item("Random (RMS)")
	f.select(0)
	f.item_selected.connect(func(_i: int) -> void: refresh_map_list())

func _build_map_list() -> void:
	var list: ItemList = $VBox/HBox/Left/MapList
	list.clear()
	list.item_selected.connect(_on_map_list_selected)

func _map_filter() -> int:
	var f: RtsoSafeSelect = $VBox/HBox/Left/MapFilterSlot.get_child(0)
	return f.get_selected()

func refresh_map_list() -> void:
	var list: ItemList = $VBox/HBox/Left/MapList
	list.clear()
	_filtered.clear()
	var flt := _map_filter()
	for m in _maps:
		if flt == 1 and m["kind"] != "Scenario":
			continue
		if flt == 2 and m["kind"] != "Random":
			continue
		_filtered.append(m)
		list.add_item(str(m["name"]))
	if _sel_map < 0 or _sel_map >= _filtered.size():
		_sel_map = 0
	if not _filtered.is_empty():
		list.select(_sel_map)
	_update_preview()

func _on_map_list_selected(idx: int) -> void:
	_sel_map = idx
	_update_preview()
	_clamp_slots_to_map()

# 0 A.D. shows WaterLevel and SeaLevelRiseTime only for the maps whose triggers
# read them, and TeamPlacement only for the maps that list placement patterns.
func _update_map_options() -> void:
	var id := str(_selected_map().get("id", ""))
	var row := $VBox/HBox/Left/MapOptionsRow
	if row == null:
		return
	$VBox/HBox/Left/MapOptionsRow/WaterLevelLabel.visible = id == "random_flood"
	$VBox/HBox/Left/MapOptionsRow/WaterLevelSlot.visible = id == "random_flood"
	$VBox/HBox/Left/MapOptionsRow/SeaRiseLabel.visible = id == "random_extinct_volcano"
	$VBox/HBox/Left/MapOptionsRow/SeaRiseSlot.visible = id == "random_extinct_volcano"
	var tp := id in TEAM_PLACEMENT_MAPS
	$VBox/HBox/Left/MapOptionsRow/TeamPlacementLabel.visible = tp
	$VBox/HBox/Left/MapOptionsRow/TeamPlacementSlot.visible = tp
	row.visible = id == "random_flood" or id == "random_extinct_volcano" or tp

func _selected_map() -> Dictionary:
	if _filtered.is_empty():
		return {}
	return _filtered[clampi(_sel_map, 0, _filtered.size() - 1)]

func _update_preview() -> void:
	_update_map_options()
	var m := _selected_map()
	var lbl := $VBox/HBox/Right/Preview/Label as Label
	var crest := $VBox/HBox/Right/CivPreview/Crest as TextureRect
	var civ_lbl := $VBox/HBox/Right/CivPreview/CivLabel as Label
	if m.is_empty():
		if lbl: lbl.text = "No map selected."
		return
	var seed_txt := "Random seed" if int($VBox/HBox/Left/SeedRow/SpinSeed.value) == 0 else "Seed %d" % int($VBox/HBox/Left/SeedRow/SpinSeed.value)
	if lbl:
		lbl.text = "%s\n[%s • %s • %dm • %d players]\n\n%s\n\n%s" % [
			m["name"], m["kind"], m["biome"], int(m["size"]), int(m["players"]), seed_txt, m["desc"]]
		if _slot_rows.size() > 0 and int(_slot_rows[0]["kind"]) == 1:
			lbl.text += "\n\nNo Human slot: you watch the AIs play with the map revealed."
	# Civ preview follows slot 0 (the human player).
	var civ := _slot_civ(0)
	if crest and civ >= 0 and civ < CIVS.size():
		var tex: Texture2D = load(CIVS[civ]["emblem"])
		if tex: crest.texture = tex
	if civ_lbl and civ >= 0 and civ < CIVS.size():
		civ_lbl.text = "%s\n\n%s" % [CIVS[civ]["name"], CIVS[civ]["blurb"]]

# ---------------------------------------------------------------- slots ---

func _slot_count_node() -> SpinBox:
	return $VBox/HBox/Right/SlotsBox/SlotCountRow/SpinSlots as SpinBox

func _slots_grid() -> GridContainer:
	return $VBox/HBox/Right/SlotsBox/SlotsGrid as GridContainer

func _ensure_slot_rows(n: int) -> void:
	while _slot_rows.size() < n:
		var i := _slot_rows.size()
		_slot_rows.append({
			"kind": 1 if i > 0 else 0,
			"civ": i % CIVS.size(),
			"team": 0, # 0 A.D.: no team until you pick one
			# 0 A.D. gui.gamesetup.aidifficulty / aibehavior from the options page.
			"diff": clampi(int(UserSettings.cfg.get_number("gui.gamesetup.aidifficulty")), 0, 5),
			"beh": AI_BEHAVIOR_IDS.get(UserSettings.cfg.get_value("gui.gamesetup.aibehavior"), 0),
			"color": _unused_color(),
		})
	if _slot_rows.size() > n:
		_slot_rows.resize(n)

func _unused_color() -> int:
	for c in range(PLAYER_COLORS.size()):
		var used := false
		for r in _slot_rows:
			if int(r.get("color", -1)) == c:
				used = true
				break
		if not used:
			return c
	return 0

func _color_icon(c: int) -> Texture2D:
	if _color_icons.is_empty():
		for col in PLAYER_COLORS:
			var img := Image.create(16, 16, false, Image.FORMAT_RGB8)
			img.fill(col)
			_color_icons.append(ImageTexture.create_from_image(img))
	return _color_icons[c]

func refresh_slots() -> void:
	_rebuilding_slots = true
	var grid := _slots_grid()
	for c in grid.get_children():
		c.queue_free()
	var n := int(_slot_count_node().value)
	_ensure_slot_rows(n)
	for i in range(n):
		_add_slot_row(grid, i)
	_rebuilding_slots = false

func _add_slot_row(grid: GridContainer, i: int) -> void:
	var row: Dictionary = _slot_rows[i]
	var name := Label.new()
	name.text = "P%d%s" % [i + 1, " (You)" if i == 0 and int(row["kind"]) == 0 else ""]
	grid.add_child(name)
	# kind
	var kind_slot := Control.new()
	kind_slot.custom_minimum_size = Vector2(92, 28)
	grid.add_child(kind_slot)
	var kind := _make_option(kind_slot)
	# Slot 1 is you, or AI: with no Human slot you watch the AIs play (0 A.D. lets every slot be AI).
	for k in (SLOT_KINDS if i > 0 else SLOT_KINDS.slice(0, 2)):
		kind.add_item(k)
	kind.select(int(row["kind"]) if i > 0 else mini(int(row["kind"]), 1))
	kind.item_selected.connect(_on_slot_kind.bind(i))
	# civ
	var civ_slot := Control.new()
	civ_slot.custom_minimum_size = Vector2(130, 28)
	grid.add_child(civ_slot)
	var civ := _make_option(civ_slot)
	for c in CIVS:
		var emblem: Texture2D = load(c["emblem"])
		if emblem: civ.add_icon_item(emblem, c["name"])
		else: civ.add_item(c["name"])
	civ.add_item("Random")
	civ.select(clampi(int(row["civ"]), 0, CIVS.size()))
	civ.item_selected.connect(_on_slot_civ.bind(i))
	# team
	var team_slot := Control.new()
	team_slot.custom_minimum_size = Vector2(64, 28)
	grid.add_child(team_slot)
	var team := _make_option(team_slot)
	team.add_item("None")
	for t in range(1, 5):
		team.add_item("T%d" % t)
	team.select(clampi(int(row["team"]), 0, 4))
	team.item_selected.connect(_on_slot_team.bind(i))
	# AI difficulty (per-slot; slot 0 uses the global default row)
	var diff_slot := Control.new()
	diff_slot.custom_minimum_size = Vector2(92, 28)
	grid.add_child(diff_slot)
	var diff := _make_option(diff_slot)
	for dname in DIFFS:
		diff.add_item(dname)
	diff.select(clampi(int(row["diff"]), 0, 5))
	diff.item_selected.connect(_on_slot_diff.bind(i))
	# AI behaviour (per slot)
	var beh_slot := Control.new()
	beh_slot.custom_minimum_size = Vector2(92, 28)
	grid.add_child(beh_slot)
	var beh := _make_option(beh_slot)
	for bname in BEHAVIORS:
		beh.add_item(bname)
	beh.select(clampi(int(row["beh"]), 0, 3))
	beh.item_selected.connect(_on_slot_beh.bind(i))
	# player colour (0 A.D. PlayerColor: picking a colour in use swaps the two players' colours)
	var col_slot := Control.new()
	col_slot.custom_minimum_size = Vector2(56, 28)
	grid.add_child(col_slot)
	var col := _make_option(col_slot)
	for c in range(PLAYER_COLORS.size()):
		col.add_icon_item(_color_icon(c), "")
	col.select(clampi(int(row["color"]), 0, PLAYER_COLORS.size() - 1))
	col.item_selected.connect(_on_slot_color.bind(i))

func _on_slot_kind(idx: int, slot: int) -> void:
	if _rebuilding_slots or slot < 0 or slot >= _slot_rows.size():
		return
	_slot_rows[slot]["kind"] = idx
	if slot == 0:
		refresh_slots.call_deferred() # the "(You)" label follows slot 1's kind
	_update_preview()

func _on_slot_civ(idx: int, slot: int) -> void:
	if _rebuilding_slots or slot < 0 or slot >= _slot_rows.size():
		return
	_slot_rows[slot]["civ"] = idx
	_update_preview()

func _on_slot_team(idx: int, slot: int) -> void:
	if _rebuilding_slots or slot < 0 or slot >= _slot_rows.size():
		return
	_slot_rows[slot]["team"] = idx

func _on_slot_diff(idx: int, slot: int) -> void:
	if _rebuilding_slots or slot < 0 or slot >= _slot_rows.size():
		return
	_slot_rows[slot]["diff"] = idx

func _on_slot_color(idx: int, slot: int) -> void:
	if _rebuilding_slots or slot < 0 or slot >= _slot_rows.size():
		return
	var old := int(_slot_rows[slot]["color"])
	for r in _slot_rows:
		if int(r["color"]) == idx:
			r["color"] = old
	_slot_rows[slot]["color"] = idx
	refresh_slots.call_deferred()

func _on_slot_beh(idx: int, slot: int) -> void:
	if _rebuilding_slots or slot < 0 or slot >= _slot_rows.size():
		return
	_slot_rows[slot]["beh"] = idx

func _slot_civ(slot: int) -> int:
	if slot < 0 or slot >= _slot_rows.size():
		return 0
	return int(_slot_rows[slot]["civ"])

func _clamp_slots_to_map() -> void:
	# RMS maps spawn any count; scenarios are fixed-size. Warn, don't force:
	# extra spawns still get bases but no AI economy (Petra drives team 1).
	var m := _selected_map()
	if m.is_empty() or str(m.get("kind", "")) != "Scenario":
		return
	var want := int(m["players"])
	var spin := _slot_count_node()
	if int(spin.value) != want:
		spin.value = want
		refresh_slots()

# ---------------------------------------------------------------- options ---

func _build_options() -> void:
	var vic := _make_option($VBox/HBox/Left/VictorySlot)
	for v in VICTORIES:
		vic.add_item(v)
	vic.select(0)
	var msz := _make_option($VBox/HBox/Left/MapSizeSlot)
	for s in MAP_SIZES:
		msz.add_item(s[1], s[0])
	msz.select(0)
	var pop := _make_option($VBox/HBox/Left/PopCapSlot)
	for p in POP_CAPS:
		pop.add_item(p[1])
	pop.select(5) # 0 A.D. default: 300
	var cf := _make_option($VBox/HBox/Left/CeasefireSlot)
	for c in CEASEFIRES:
		cf.add_item("No ceasefire" if c == 0 else "%d min" % c)
	cf.select(0)
	var wt := _make_option($VBox/HBox/Left/VictoryTimesRow/WonderTimeSlot)
	var rt := _make_option($VBox/HBox/Left/VictoryTimesRow/RelicTimeSlot)
	for t in VICTORY_TIMES:
		wt.add_item("%d min" % t)
		rt.add_item("hold %d min" % t)
	wt.select(VICTORY_TIMES.find(20))
	rt.select(VICTORY_TIMES.find(20))
	var rc := _make_option($VBox/HBox/Left/VictoryTimesRow/RelicCountSlot)
	for c in RELIC_COUNTS:
		rc.add_item(str(c))
	rc.select(RELIC_COUNTS.find(2))
	var wl := _make_option($VBox/HBox/Left/MapOptionsRow/WaterLevelSlot)
	for w in WATER_LEVELS:
		wl.add_item(w)
	wl.select(0)
	var sr := _make_option($VBox/HBox/Left/MapOptionsRow/SeaRiseSlot)
	for t in SEA_RISE_TIMES:
		sr.add_item("rise at %d min" % t)
	sr.select(SEA_RISE_TIMES.find(25))
	var tpl := _make_option($VBox/HBox/Left/MapOptionsRow/TeamPlacementSlot)
	var tips := PackedStringArray()
	for p in TEAM_PLACEMENTS:
		tpl.add_item(p[0])
		tips.append("%s: %s" % [p[0], p[1]])
	$VBox/HBox/Left/MapOptionsRow/TeamPlacementLabel.tooltip_text = "\n".join(tips)
	tpl.select(0)
	var res := _make_option($VBox/HBox/Left/ResSlot)
	for r in START_RES:
		res.add_item(r)
	res.select(1)
	var bio := _make_option($VBox/HBox/Left/BiomeSpeedRow/BiomeSlot)
	for b in BIOMES:
		bio.add_item(b)
	bio.select(0)
	var spd := _make_option($VBox/HBox/Left/BiomeSpeedRow/SpeedSlot)
	for sp in SPEEDS:
		spd.add_item(sp[1])
	spd.select(2)
	_mute_spin($VBox/HBox/Left/SeedRow/SpinSeed)
	_mute_spin($VBox/HBox/Right/SlotsBox/SlotCountRow/SpinSlots)
	$VBox/HBox/Right/SlotsBox/SlotCountRow/SpinSlots.value_changed.connect(
		func(_v: float) -> void: refresh_slots())
	$VBox/HBox/Left/SeedRow/BtnReroll.pressed.connect(_on_reroll_seed)
	$VBox/HBox/Left/SeedRow/SpinSeed.value_changed.connect(
		func(_v: float) -> void: _update_preview())

func _mute_spin(sb: SpinBox) -> void:
	if sb:
		MatchSettings.prepare_line_edit(sb.get_line_edit())
		MatchSettings.strip_engine_popups(sb)

func _on_reroll_seed() -> void:
	$VBox/HBox/Left/SeedRow/SpinSeed.value = randi() % 1000000000
	_update_preview()

func _restore_settings() -> void:
	if not has_node("/root/MatchSettings"):
		return
	var ms := get_node("/root/MatchSettings")
	var vic: RtsoSafeSelect = $VBox/HBox/Left/VictorySlot.get_child(0)
	var res: RtsoSafeSelect = $VBox/HBox/Left/ResSlot.get_child(0)
	var bio: RtsoSafeSelect = $VBox/HBox/Left/BiomeSpeedRow/BiomeSlot.get_child(0)
	var spd: RtsoSafeSelect = $VBox/HBox/Left/BiomeSpeedRow/SpeedSlot.get_child(0)
	vic.select(clampi(int(ms.get("victory")), 0, VICTORIES.size() - 1))
	$VBox/HBox/Left/Waves.button_pressed = bool(ms.get("waves_enabled"))
	$VBox/HBox/Left/LastManStanding.button_pressed = bool(ms.get("last_man_standing"))
	$VBox/HBox/Left/StartRow/Nomad.button_pressed = bool(ms.get("nomad"))
	$VBox/HBox/Left/StartRow/RegicideGarrison.button_pressed = bool(ms.get("regicide_garrison"))
	$VBox/HBox/Left/StartRow/LockTeams.button_pressed = bool(ms.get("lock_teams"))
	$VBox/HBox/Left/MapVisRow/Cheats.button_pressed = bool(ms.get("cheats"))
	$VBox/HBox/Left/StartRow/DisableTreasures.button_pressed = bool(ms.get("disable_treasures"))
	var pop: RtsoSafeSelect = $VBox/HBox/Left/PopCapSlot.get_child(0)
	for i in range(POP_CAPS.size()):
		if int(POP_CAPS[i][0]) == int(ms.get("pop_cap")):
			pop.select(i)
	var cf: RtsoSafeSelect = $VBox/HBox/Left/CeasefireSlot.get_child(0)
	cf.select(maxi(CEASEFIRES.find(int(ms.get("ceasefire_min"))), 0))
	$VBox/HBox/Left/MapVisRow/RevealMap.button_pressed = bool(ms.get("reveal_map"))
	$VBox/HBox/Left/MapVisRow/ExploreMap.button_pressed = bool(ms.get("explore_map"))
	var wt: RtsoSafeSelect = $VBox/HBox/Left/VictoryTimesRow/WonderTimeSlot.get_child(0)
	var rt: RtsoSafeSelect = $VBox/HBox/Left/VictoryTimesRow/RelicTimeSlot.get_child(0)
	var rc: RtsoSafeSelect = $VBox/HBox/Left/VictoryTimesRow/RelicCountSlot.get_child(0)
	var wi := VICTORY_TIMES.find(int(ms.get("wonder_min")))
	var ri := VICTORY_TIMES.find(int(ms.get("relic_min")))
	var ci := RELIC_COUNTS.find(int(ms.get("relic_count")))
	wt.select(wi if wi >= 0 else VICTORY_TIMES.find(20))
	rt.select(ri if ri >= 0 else VICTORY_TIMES.find(20))
	rc.select(ci if ci >= 0 else RELIC_COUNTS.find(2))
	var wl: RtsoSafeSelect = $VBox/HBox/Left/MapOptionsRow/WaterLevelSlot.get_child(0)
	wl.select(clampi(int(ms.get("water_level")), 0, WATER_LEVELS.size() - 1))
	var sr: RtsoSafeSelect = $VBox/HBox/Left/MapOptionsRow/SeaRiseSlot.get_child(0)
	var si := SEA_RISE_TIMES.find(int(ms.get("sea_level_rise_min")))
	sr.select(si if si >= 0 else SEA_RISE_TIMES.find(25))
	var tpl: RtsoSafeSelect = $VBox/HBox/Left/MapOptionsRow/TeamPlacementSlot.get_child(0)
	tpl.select(clampi(int(ms.get("team_placement")), 0, TEAM_PLACEMENTS.size() - 1))
	var msz: RtsoSafeSelect = $VBox/HBox/Left/MapSizeSlot.get_child(0)
	for i in range(MAP_SIZES.size()):
		if int(MAP_SIZES[i][0]) == int(ms.get("map_size")):
			msz.select(i)
	res.select(clampi(int(ms.get("start_res")), 0, START_RES.size() - 1))
	bio.select(clampi(int(ms.get("biome")), 0, BIOMES.size() - 1))
	for i in range(SPEEDS.size()):
		if is_equal_approx(float(SPEEDS[i][0]), float(ms.get("game_speed"))):
			spd.select(i)
	$VBox/HBox/Left/SeedRow/SpinSeed.value = int(ms.get("seed"))
	# Restore map selection by id.
	var want: String = str(ms.get("map_id"))
	for i in range(_filtered.size()):
		if str(_filtered[i]["id"]) == want:
			_sel_map = i
			break
	# Restore slots.
	var types: Array = ms.get("slot_types")
	var civs: Array = ms.get("slot_civs")
	var teams: Array = ms.get("slot_teams")
	var sdiffs: Array = ms.get("slot_diffs")
	var sbehs: Array = ms.get("slot_behs")
	var scolors: Array = ms.get("slot_colors")
	if not types.is_empty():
		$VBox/HBox/Right/SlotsBox/SlotCountRow/SpinSlots.value = clampi(types.size(), 2, 8)
		_ensure_slot_rows(int($VBox/HBox/Right/SlotsBox/SlotCountRow/SpinSlots.value))
		for i in range(mini(types.size(), _slot_rows.size())):
			_slot_rows[i]["kind"] = clampi(int(types[i]), 0, 2)
			if i < civs.size():
				_slot_rows[i]["civ"] = clampi(int(civs[i]), 0, CIVS.size() - 1)
			if i < teams.size():
				_slot_rows[i]["team"] = clampi(int(teams[i]), 0, 4)
			if i < sdiffs.size():
				_slot_rows[i]["diff"] = clampi(int(sdiffs[i]), 0, 5)
			if i < sbehs.size():
				_slot_rows[i]["beh"] = clampi(int(sbehs[i]), 0, 3)
			if i < scolors.size():
				_slot_rows[i]["color"] = clampi(int(scolors[i]), 0, PLAYER_COLORS.size() - 1)
			if i == 0:
				_slot_rows[i]["kind"] = mini(int(_slot_rows[i]["kind"]), 1)
		refresh_slots()

# ---------------------------------------------------------------- start ---

func _on_back_pressed() -> void:
	MatchSettings.go("res://scenes/menu/main_menu.tscn")

func _collect_setup() -> Dictionary:
	var m := _selected_map()
	var seed := int($VBox/HBox/Left/SeedRow/SpinSeed.value)
	var vic: RtsoSafeSelect = $VBox/HBox/Left/VictorySlot.get_child(0)
	var res: RtsoSafeSelect = $VBox/HBox/Left/ResSlot.get_child(0)
	var bio: RtsoSafeSelect = $VBox/HBox/Left/BiomeSpeedRow/BiomeSlot.get_child(0)
	var spd: RtsoSafeSelect = $VBox/HBox/Left/BiomeSpeedRow/SpeedSlot.get_child(0)
	var msz: RtsoSafeSelect = $VBox/HBox/Left/MapSizeSlot.get_child(0)
	var pop: RtsoSafeSelect = $VBox/HBox/Left/PopCapSlot.get_child(0)
	var cf: RtsoSafeSelect = $VBox/HBox/Left/CeasefireSlot.get_child(0)
	var wt: RtsoSafeSelect = $VBox/HBox/Left/VictoryTimesRow/WonderTimeSlot.get_child(0)
	var rt: RtsoSafeSelect = $VBox/HBox/Left/VictoryTimesRow/RelicTimeSlot.get_child(0)
	var rc: RtsoSafeSelect = $VBox/HBox/Left/VictoryTimesRow/RelicCountSlot.get_child(0)
	var wl: RtsoSafeSelect = $VBox/HBox/Left/MapOptionsRow/WaterLevelSlot.get_child(0)
	var sr: RtsoSafeSelect = $VBox/HBox/Left/MapOptionsRow/SeaRiseSlot.get_child(0)
	var tpl: RtsoSafeSelect = $VBox/HBox/Left/MapOptionsRow/TeamPlacementSlot.get_child(0)
	var n := int($VBox/HBox/Right/SlotsBox/SlotCountRow/SpinSlots.value)
	_ensure_slot_rows(n)
	var types: Array = []
	var civs: Array = []
	var teams: Array = []
	var diffs: Array = []
	var behs: Array = []
	var colors: Array = []
	var humans := 0
	var ai_total := 0
	for i in range(n):
		var k := int(_slot_rows[i]["kind"])
		if i == 0:
			k = mini(k, 1) # Human or AI, never closed
		if k == 2:
			continue # closed: 0 A.D. drops the slot, so later players move up
		types.append(k)
		var c := int(_slot_rows[i]["civ"])
		if c >= CIVS.size(): # "Random": 0 A.D. picks one when the match starts
			c = randi() % CIVS.size()
		civs.append(c)
		teams.append(int(_slot_rows[i]["team"]))
		diffs.append(int(_slot_rows[i]["diff"]))
		behs.append(int(_slot_rows[i]["beh"]))
		colors.append(int(_slot_rows[i]["color"]))
		if k == 0:
			humans += 1
		elif k == 1:
			ai_total += 1
	var names := _player_names(types, civs)
	# Single-player Petra settings: the first AI slot's picks (per-slot AI is next).
	var diff := 3
	var beh := 2
	for i in range(n):
		if int(_slot_rows[i]["kind"]) == 1:
			diff = int(_slot_rows[i]["diff"])
			beh = int(_slot_rows[i]["beh"])
			break
	return {"map": m, "seed": seed, "victory": vic.get_selected(),
		"waves": $VBox/HBox/Left/Waves.button_pressed,
		"lms": $VBox/HBox/Left/LastManStanding.button_pressed,
		"pop_cap": int(POP_CAPS[maxi(pop.get_selected(), 0)][0]),
		"ceasefire": int(CEASEFIRES[maxi(cf.get_selected(), 0)]),
		"reveal": $VBox/HBox/Left/MapVisRow/RevealMap.button_pressed,
		"explore": $VBox/HBox/Left/MapVisRow/ExploreMap.button_pressed,
		"wonder_min": int(VICTORY_TIMES[maxi(wt.get_selected(), 0)]),
		"relic_min": int(VICTORY_TIMES[maxi(rt.get_selected(), 0)]),
		"relic_count": int(RELIC_COUNTS[maxi(rc.get_selected(), 0)]),
		"water_level": maxi(wl.get_selected(), 0),
		"sea_level_rise_min": int(SEA_RISE_TIMES[maxi(sr.get_selected(), 0)]),
		"team_placement": maxi(tpl.get_selected(), 0),
		"map_size": int(MAP_SIZES[maxi(msz.get_selected(), 0)][0]),
		"res": res.get_selected(), "beh": beh, "diff": diff,
		"biome": maxi(bio.get_selected(), 0),
		"speed": float(SPEEDS[maxi(spd.get_selected(), 0)][0]),
		"types": types, "civs": civs, "teams": teams, "diffs": diffs, "behs": behs, "colors": colors, "names": names,
		# Nomad only on random maps, RegicideGarrison only with Regicide (as 0 A.D. settings).
		"nomad": $VBox/HBox/Left/StartRow/Nomad.button_pressed and not m.is_empty() and str(m.get("kind", "")) == "Random",
		"regicide_garrison": $VBox/HBox/Left/StartRow/RegicideGarrison.button_pressed and vic.get_selected() == VICTORY_REGICIDE,
		"lock_teams": $VBox/HBox/Left/StartRow/LockTeams.button_pressed,
		"cheats": $VBox/HBox/Left/MapVisRow/Cheats.button_pressed,
		"disable_treasures": $VBox/HBox/Left/StartRow/DisableTreasures.button_pressed,
		"humans": humans, "ai": ai_total, "slots": types.size()}

# 0 A.D. PlayerName: slot 1 is you (Options > player name), AI slots take an unused name from
# their civ's AINames ("Name 2" when the list runs out), other humans are "Player N".
func _player_names(types: Array, civs: Array) -> Array:
	var you := "Player 1"
	var cfg := ConfigFile.new()
	if cfg.load("user://config.ini") == OK:
		you = str(cfg.get_value("general", "player_name_sp", you)).strip_edges()
	var picked: Array = []
	var names: Array = []
	for i in range(types.size()):
		if i == 0 and int(types[i]) == 0:
			names.append(you if not you.is_empty() else "Player 1")
		elif int(types[i]) == 1:
			var pool: Array = CivAINames.AI_NAMES[clampi(int(civs[i]), 0, CivAINames.AI_NAMES.size() - 1)]
			var free: Array = pool.filter(func(n): return not picked.has(n))
			if free.is_empty():
				free = pool
			var chosen: String = free[randi() % free.size()]
			var dup := picked.count(chosen)
			picked.append(chosen)
			names.append(chosen if dup == 0 else "%s %d" % [chosen, dup + 1])
		else:
			names.append("Player %d" % (i + 1))
	return names

func _on_start_pressed() -> void:
	var st := _collect_setup()
	if st["map"].is_empty():
		_show_error("No map selected.")
		return
	if int(st["humans"]) < 1 and int(st["ai"]) < 2:
		_show_error("Watching a game needs at least 2 AI players.")
		return
	if int(st["ai"]) < 1:
		_show_error("Need at least 1 AI opponent.")
		return
	if int(st["humans"]) > 0 and int(st["types"][0]) != 0:
		_show_error("Slot 1 must be Human, or make every slot AI to watch.")
		return
	var m: Dictionary = st["map"]
	if str(m["kind"]) == "Scenario" and int(st["slots"]) != int(m["players"]):
		_show_error("'%s' needs exactly %d players (scenario)." % [m["name"], int(m["players"])])
		return
	if not has_node("/root/MatchSettings"):
		_show_error("MatchSettings autoload missing.")
		return
	var ms := get_node("/root/MatchSettings")
	ms.map_id = str(m["id"])
	ms.seed = int(st["seed"])
	ms.victory = int(st["victory"])
	ms.waves_enabled = bool(st["waves"])
	ms.last_man_standing = bool(st["lms"])
	ms.pop_cap = int(st["pop_cap"])
	ms.ceasefire_min = int(st["ceasefire"])
	ms.reveal_map = bool(st["reveal"])
	ms.explore_map = bool(st["explore"])
	ms.wonder_min = int(st["wonder_min"])
	ms.relic_min = int(st["relic_min"])
	ms.relic_count = int(st["relic_count"])
	ms.water_level = int(st["water_level"])
	ms.sea_level_rise_min = int(st["sea_level_rise_min"])
	ms.team_placement = int(st["team_placement"])
	ms.map_size = int(st["map_size"])
	ms.ai_difficulty = int(st["diff"])
	ms.ai_behavior = int(st["beh"])
	ms.start_res = int(st["res"])
	ms.slot_types = st["types"]
	ms.slot_civs = st["civs"]
	ms.slot_teams = st["teams"]
	ms.slot_diffs = st["diffs"]
	ms.slot_behs = st["behs"]
	ms.slot_colors = st["colors"]
	ms.slot_names = st["names"]
	ms.nomad = bool(st["nomad"])
	ms.regicide_garrison = bool(st["regicide_garrison"])
	ms.lock_teams = bool(st["lock_teams"])
	ms.cheats = bool(st["cheats"])
	ms.disable_treasures = bool(st["disable_treasures"])
	ms.biome = int(st["biome"])
	ms.game_speed = float(st["speed"])
	ms.player_count = int(st["slots"])
	ms.player_civ = int(st["civs"][0])
	# Legacy back-compat: first AI slot's civ.
	ms.ai_civ = int(st["civs"][1]) if int(st["types"][1]) == 1 else int(st["civs"][0])
	print("[GameSetup] Start — %s seed=%d victory=%d diff=%d beh=%d res=%d slots=%s civs=%s teams=%s" % [
		ms.map_id, ms.seed, ms.victory, ms.ai_difficulty, ms.ai_behavior,
		ms.start_res, str(st["types"]), str(st["civs"]), str(st["teams"])])
	var err := MatchSettings.go("res://scenes/main.tscn")
	if err != OK:
		_show_error("Could not load match scene.")

func _show_error(msg: String) -> void:
	var lbl := $VBox/HBox/Right/BtnStart as Button
	push_warning("[GameSetup] " + msg)
	var err := $VBox/HBox/Right/ErrorLabel as Label
	if err:
		err.text = msg
