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
const MODERN := preload("res://scenes/common/modern_theme.gd")

const ScenarioStart = preload("res://scenes/menu/scenario_start.gd")
const MapBrowser = preload("res://scenes/menu/map_browser.gd")
const L10n = preload("res://scenes/menu/l10n.gd")
const TAB_BUTTONS_JS := preload("res://scenes/common/tab_buttons.gd")
const MAP_BROWSER_HOTKEY := "gamesetup.mapbrowser.open"
# Loaded at run time: game_description.gd preloads this page.
var GameDescription = load("res://scenes/session/game_description.gd")
const AIConfig = preload("res://scenes/menu/ai_config.gd")
# The slot grid: name, kind, civ, team, AI gear, colour.
const SLOT_COLUMNS := 6

const RefCommon := preload("res://scenes/reference/reference_common.gd")
const CIVS := [
	{"id": 0, "code": "athen", "name": "Athenians", "emblem": "res://assets/textures/ui/emblems/athenians.png"},
	{"id": 1, "code": "spart", "name": "Spartans", "emblem": "res://assets/textures/ui/emblems/spartans.png"},
	{"id": 2, "code": "pers", "name": "Persians", "emblem": "res://assets/textures/ui/emblems/persians.png"},
	{"id": 3, "code": "rome", "name": "Romans", "emblem": "res://assets/textures/ui/emblems/romans.png"},
	{"id": 4, "code": "cart", "name": "Carthaginians", "emblem": "res://assets/textures/ui/emblems/carthaginians.png"},
	{"id": 5, "code": "mace", "name": "Macedonians", "emblem": "res://assets/textures/ui/emblems/macedonians.png"},
	{"id": 6, "code": "brit", "name": "Britons", "emblem": "res://assets/textures/ui/emblems/britons.png"},
	{"id": 7, "code": "gaul", "name": "Gauls", "emblem": "res://assets/textures/ui/emblems/gauls.png"},
	{"id": 8, "code": "iber", "name": "Iberians", "emblem": "res://assets/textures/ui/emblems/iberians.png"},
	{"id": 9, "code": "maur", "name": "Mauryans", "emblem": "res://assets/textures/ui/emblems/mauryans.png"},
	{"id": 10, "code": "kush", "name": "Kushites", "emblem": "res://assets/textures/ui/emblems/kushites.png"},
	{"id": 11, "code": "ptol", "name": "Ptolemies", "emblem": "res://assets/textures/ui/emblems/ptolemies.png"},
	{"id": 12, "code": "sele", "name": "Seleucids", "emblem": "res://assets/textures/ui/emblems/seleucids.png"},
	{"id": 13, "code": "han", "name": "Han Chinese", "emblem": "res://assets/textures/ui/emblems/han.png"},
	{"id": 15, "code": "germ", "name": "Germans", "emblem": "res://assets/textures/ui/emblems/germans.png"},
]
## A slot's "civ" is the rtso::Civilization id (14 is Gaia, never listed); this one is "Random".
const RANDOM_CIV := -1

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
	["random_jebel_barkal", "Jebel Barkal (RMS)", "Random", 8, 128, "savanna", "The Nile and the hill of Jebel Barkal, with the walled Kushite city of Napata that patrols and attacks. Supports 2-8 players."],
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
	["random_land_grab", "Land Grab (RMS)", "Random", 8, 128, "temperate", "Players start on small islands on one side of the map; a big continent on the other side waits for expansion. Supports 2-8 players."],
	["random_the_nile", "The Nile (RMS)", "Random", 8, 128, "desert", "A calm wide river divides the map into western and eastern parts. Supports 2-8 players."],
	["random_unknown", "Unknown (RMS)", "Random", 8, 128, "temperate", "One of 13 land and naval landscapes, picked at random. Supports 2-8 players."],
	["random_lower_nubia", "Lower Nubia (RMS)", "Random", 8, 128, "savanna", "The Nile between its first and second cataracts, desert plateaus and cliffs from a real elevation map. Supports 2-8 players."],
	["random_migration", "Migration (RMS)", "Random", 8, 128, "temperate", "Home islands on the outside, a rich continent in the middle; ships needed to reach it. Supports 2-8 players."],
	["random_ngorongoro", "Ngorongoro (RMS)", "Random", 8, 128, "savanna", "Savanna lowlands round the crater's highlands and a central Eden rich in metal, from a real elevation map. Supports 2-8 players."],
	["random_river_archipelago", "River Archipelago (RMS)", "Random", 8, 128, "india", "Narrow tropical strips of land split by waterways with few shallow bridges; Mauryan tribesmen hold the outermost islands. Supports 2-8 players."],
	["random_continent", "Continent (RMS)", "Random", 8, 128, "temperate", "All players starts on a continent surrounded by water. Supports 2-8 players."],
	["random_foothills", "Foothills (RMS)", "Random", 8, 128, "temperate", "A land map with rolling hills and no obstructions. Supports 2-8 players."],
	["random_coast_range", "Coast Range (RMS)", "Random", 8, 128, "temperate", "All players starts on a continent surrounded by water. This map is designed and balanced for 2 teams only. Supports 2-8 players."],
	# wall_demo.json's "demo" keyword lists it only under the Demo Maps filter, as in 0.28.
	["random_wall_demo", "Wall Demo (RMS)", "Random", 8, 128, "temperate", "A demonstration of wall placement methods/code in random maps. Giant map size is recommended!"],
	["random_new_rms_test", "New RMS Test (RMS)", "Random", 8, 128, "temperate", "A basic test of the random map generator - not playable."],
]

# 0 A.D. 0.28 Population Cap Type and its caps (simulation/data/settings/
# population_capacities.json): [name, title, tooltip, cap title, cap tooltip,
# caps, default cap]; 10000 reads "Unlimited". RTS0's pop_cap setting is the
# cap per player (0 = unlimited), its negative a World Population, and with
# team_pop the cap a Team Population (Simulation::set_team_pop_cap).
# Ceasefire lengths in minutes follow.
const POP_CAP_TYPES := [
	["player", "Player Population", "Locked population cap for all players.", "Player Population Cap",
		"Choose the player population cap.", [50, 100, 150, 200, 250, 300, 10000], 300],
	["team", "Team Population", "Distributes a team's total population cap evenly over all its living members. Enables the setting 'Locked teams'.",
		"Team Population Cap", "Choose the team population cap.",
		[100, 150, 200, 250, 300, 350, 400, 450, 500, 550, 600, 700, 800, 1000, 10000], 400],
	["world", "World Population", "Distributes the total population cap evenly over all living players.", "World Population Cap",
		"Choose the world population cap.", [100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100, 1200, 1600, 2000, 2400, 10000], 600],
]
const POP_TYPE_PLAYER := 0
const POP_TYPE_TEAM := 1
const POP_TYPE_WORLD := 2
const POP_UNLIMITED := 10000
# 0.28 Sliders/Ceasefire.js (0-45 min), WonderDuration.js and RelicDuration.js (0-60 min,
# default 20) and RelicCount.js (1 to the number of civs, default 2), one entry a step.
const SettingSlider := preload("res://scenes/menu/setting_slider.gd")
static var CEASEFIRES: Array = range(0, 46)
static var VICTORY_TIMES: Array = range(0, 61)
static var RELIC_COUNTS: Array = range(1, CIVS.size() + 1)
# 0 A.D. map trigger options: Flood's WaterLevel and Extinct Volcano's SeaLevelRiseTime.
# flood.json WaterLevels: [Name, Description] (WaterLevel.js shows the hovered one's Description).
const WATER_LEVELS := [["Rising", "The water rises. The homeislands will become uninhabitable."],
	["Shallow", "The water doesn't rise. Ships and troops can battle in chest deep waters."],
	["Deep", "The water doesn't rise. You have to use ships to travel to the mainland."]]
# extinct_volcano.json SeaLevelRise: Min 0, Max 60, a minute a step (SeaLevelRiseTime.js).
static var SEA_RISE_TIMES: Array = range(0, 61)
# 0 A.D. 0.28 Player Placement (simulation/data/settings/player_placements.json,
# gamesetup PlayerPlacement.js), index == rtso::TeamPlacement: [0.28 id, name,
# description]. A random map offers the patterns its JSON lists in
# PlayerPlacements (data/map_types.json "player_placements"), in that order,
# after Random.
const TEAM_PLACEMENTS := [["random", "Random", "Select a random player placement pattern when starting the game."],
	["circle", "Circle", "Players are placed in a circle spanning the map."],
	["groupedLines", "Grouped Lines", "Allied players are placed along opposing radial lines."],
	["randomGroup", "Random Group", "Allied players are grouped, but otherwise placed randomly on the map."],
	["stronghold", "Stronghold", "Allied players are tightly grouped together."],
	["river", "River", "Allied players are placed on two parallel lines."]]
# RTS0's own random maps whose script name matches a 0.28 map's (Archipelago
# is random_archipelago_0ad): they take none of its settings.
const OWN_RMS_MAPS := ["random_archipelago"]
# The sim places players by pattern on every map that lists one, River (5)
# included (#429); off, only Ambush and Frontier offer it, without River.
const PLACEMENT_EVERY_MAP := true
const PLACEMENT_MAPS_BEFORE := ["random_ambush", "random_frontier"]
const TEAM_PLACEMENT_TOOLTIP := "Select one of the starting position patterns of this map."
# 0 A.D. Landscape (unknown.json Landscapes, gamesettings Landscape.js): [name,
# value, description];
# value is RmsGenerator::UNKNOWN_LANDSCAPE_* (3 + landscape). Landscape.js order:
# Random, the group headers, then each group's items sorted by name.
const LANDSCAPES := [["Random", 0, "Select a random landscape."],
	["Random Land", 1, "Choose a random land landscape"],
	["Random Naval", 2, "Choose a random naval landscape"],
	["Central River", 7, "A small central river."],
	["Continent", 4, "All players starts on a continent surrounded by water."],
	["Edge Seas", 10, "Players are aligned on a strip of land with seas bordering on one or both sides that may hold islands."],
	["Gulf", 11, "Land shaped like a concrescent moon around a central lake."],
	["Isthmus", 6, "Two Mediterranean land masses connected by a narrow spit of land, called an 'Isthmus'."],
	["Lakes", 12, "Mainland style with some small random lakes."],
	["Lowlands", 14, "The land is enclosed by a hill that leaves a small area per player connected to the large central place."],
	["Mainland", 15, "A typical map without any water."],
	["Passes", 13, "A large hill encompasses the map and leaves players only a small passage to the two neighboring players."],
	["Naval: Archipelago", 3, "Players are scattered across island chains and disconnected islands."],
	["Naval: Central Sea", 5, "A huge sea is dividing the map into two halves."],
	["Naval: Lake and rivers", 9, "A central lake with rivers possibly separating neighboring players."]]
const LANDSCAPE_MAPS := ["random_unknown"]
# 0 A.D. data/settings/trigger_difficulties.json: [name, tooltip], value = index + 1.
# Shown for the maps whose json lists SupportedTriggerDifficulties.
const TRIGGER_DIFFICULTIES := [["Very Easy", "Choose this difficulty if you are discovering 0 A.D."],
	["Easy", "Choose this difficulty if you do not like being knocked down."],
	["Medium", "Choose this difficulty if you have already a good experience with 0 A.D."],
	["Hard", "Choose this difficulty if you want to be really challenged."],
	["Very Hard", "Choose this difficulty if you do not mind being swept out."]]
const TRIGGER_DIFFICULTY_MAPS := ["random_jebel_barkal"]
# 0 A.D. Daytime (the maps' json settings.Daytime): [name, description] per option;
# the dropdown adds Random first, which lets the map pick as its script does.
const DAYTIMES := {
	"random_danubius": [["Day", "Bright daylight illuminates the scene."],
		["Night", "Attackers are covered by the darkness of the night."]],
	"random_polar_sea": [["Dawn", "Scattered sunlight illuminates the scenery in a vivid red tone."],
		["Daylight", "The scenery is illuminated by direct or indirect sunlight."]],
}

# 0 A.D. common/settings.js loadMapTypes: [name, title, description]; Skirmish is the default.
const MAP_TYPES := [["skirmish", "Skirmish", "A map with a predefined landscape and number of players. Freely select the other game settings."],
	["random", "Random", "Create a unique map with a different resource distribution each time. Freely select the number of players and teams."],
	["scenario", "Scenario", "A map with a predefined landscape and matchsettings."]]
# 0 A.D. gui/maps/MapFilters.js: [name, title, description, match on the map's Keywords].
const MAP_FILTERS := [["default", "Default", "All maps except naval and demo maps.", ["!naval !demo !hidden"]],
	["naval", "Naval Maps", "Maps where ships are needed to reach the enemy.", ["naval"]],
	["demo", "Demo Maps", "These maps are not playable but for demonstration purposes only.", ["demo"]],
	["new", "New Maps", "Maps that are brand new in this release of the game.", ["new"]],
	["multiplayer", "Best for MP", "Maps that are recommended for Multiplayer play.", ["multiplayer"]],
	["trigger", "Trigger Maps", "Maps that come with scripted events and potentially spawn enemy units.", ["trigger"]],
	["all", "All Maps", "Every map of the chosen maptype.", ["!"]]]
# Each map's 0.28 type, Keywords and scenario settings (tools/extract_0ad_map_types.py).
const MAP_TYPES_FILE := "res://data/map_types.json"
# MapSelection.js: the Random map type's first entry picks one of the listed maps on Start.
const RANDOM_MAP_ID := "random"
const RANDOM_MAP := {"id": RANDOM_MAP_ID, "name": "Random", "kind": "Random", "type": "random", "keywords": [],
	"players": 8, "size": 128, "biome": "random", "desc": "Pick any of the given maps at random."}
# 0 A.D. starting_resources.json amounts, index == START_RES.
const START_RES_AMOUNTS := [100, 300, 500, 1000, 3000, 50000]

# 0.28 settings/map_sizes.json (width in meters) for random maps; Normal is the default.
const DEFAULT_MAP_SIZE := 2
const MAP_SIZES := [[128, "Tiny"], [192, "Small"], [256, "Normal"],
	[320, "Medium"], [384, "Large"], [448, "Very Large"], [512, "Giant"]]
# map_sizes.json Tooltip, index == MAP_SIZES.
const MAP_SIZE_TIPS := ["This map size has barely any room for even the most basic resources.",
	"Small maps are suitable for 1v1 matches, as players can reach the enemy quickly while still having some room to expand.",
	"This map size provides plenty of space for two players, but even eight players fit.",
	"This is the ideal mapsize for 4 to 8 players, because there is plenty of space to expand while the neighbors are quickly reachable.",
	"The large mapsize is recommended when great portions of the map are covered with water or inaccessible mountains.",
	"Very large maps are not recommended as it takes very long to reach the enemy and the game performance can be reduced.",
	"Giant maps are not recommended as it takes very long to reach the enemy and the game performance can be reduced."]

# Index == rtso::VictoryCondition id (sim_types.h).
const VICTORIES := ["Conquest — destroy all enemies", "Wonder — hold a Wonder", "Relic — claim and hold all relics", "Endless — no victory, sandbox",
	"Conquest Units — kill every enemy unit", "Conquest Structures — raze every enemy structure", "Regicide — kill the enemy hero, keep yours"]
# 0 A.D. settings/starting_resources.json: the same amount of every resource.
const START_RES := ["Very Low (100)", "Low (300)", "Medium (500)", "High (1000)", "Very High (3000)", "Deathmatch (50000)"]
const DIFFS := ["Sandbox", "Very Easy", "Easy", "Medium", "Hard", "Very Hard"]
# Options-page aibehavior values -> RTS0 PetraBehavior (petra_ai.h).
const AI_BEHAVIOR_IDS := {"random": 0, "defensive": 1, "balanced": 2, "aggressive": 3}
const BEHAVIORS := ["Random", "Defensive", "Balanced", "Aggressive"]
## The AI Behavior list order (0.28 settings.js loadAIBehaviors); entries are BEHAVIORS values.
const BEHAVIOR_ORDER := [0, 2, 1, 3]
# Random-map biome override (index - 1 == rtso::BiomeType) and 0 A.D. game speeds.
# GameSettingControlDropdown.RandomItemTags: Random entries read in orange.
const RANDOM_ITEM_COLOR := Color8(255, 165, 0)
const RANDOM_BIOME := 99 # the Biome dropdown's Random entry (its item id too); resolved at Start
const BIOMES := ["Map default", "Aegean", "Latium", "Alpine", "Oasis", "Temperate", "Steppe", "Savanna", "Polar", "India", "Autumn", "Nubia"]
## 0.28 rmbiome ids -> BIOMES index, as tools/extract_0ad_map_types.py RTS0_BIOMES maps
## them to BiomeType for the sim (map_biomes_data.h); 0.28 biomes RTS0 lacks are left out.
const RMBIOME_INDEX := {"generic/aegean": 1, "generic/alpine": 3, "generic/sahara": 4, "generic/temperate": 5,
	"generic/steppe": 6, "generic/savanna": 7, "generic/arctic": 8, "generic/india": 9,
	"generic/autumn": 10, "generic/nubia": 11}
# 0.28 simulation/data/settings/game_speeds.json: [speed, title, fast forward].
const SPEEDS := [[0.1, "Turtle (0.1×)", false], [0.25, "Slow (0.25×)", false], [0.5, "Leisurely (0.5×)", false],
	[0.75, "Relaxed (0.75×)", false], [1.0, "Normal (1×)", false], [1.25, "Fast (1.25×)", false],
	[1.5, "Very Fast (1.5×)", false], [2.0, "Insane (2×)", false], [5.0, "Fast Forward (5×)", true],
	[10.0, "Fast Forward (10×)", true], [20.0, "Fast Forward (20×)", true]]
const DEFAULT_SPEED := 4 # Normal (1×)
# rtso::PLAYER_COLORS: 0 A.D. player_defaults.json (each player's default), then PlayerColor.js DefaultColors.
const PLAYER_COLORS := [Color8(21, 55, 149), Color8(150, 20, 20), Color8(86, 180, 31), Color8(231, 200, 5),
	Color8(50, 170, 170), Color8(160, 80, 200), Color8(220, 115, 16), Color8(64, 64, 64),
	Color8(10, 10, 190), Color8(230, 10, 10), Color8(125, 235, 15), Color8(255, 255, 55),
	Color8(130, 0, 230), Color8(255, 130, 0), Color8(10, 230, 230), Color8(20, 80, 60),
	Color8(220, 160, 220), Color8(80, 255, 190), Color8(50, 150, 255), Color8(100, 150, 30),
	Color8(100, 60, 30), Color8(128, 0, 64), Color8(255, 200, 140), Color8(80, 80, 80)]
const VICTORY_REGICIDE := 6
# 0.28 settings/victory_conditions/*.json in GUIOrder: [VictoryKind bit (sim_types.h), title,
# description]. Game Setup combines them; none checked is an endless game.
const VICTORY_KINDS := [
	[1, "Conquest", "Defeat opponents by killing all their units and destroying all their structures."],
	[4, "Conquest Structures", "Defeat opponents by destroying all their structures."],
	[2, "Conquest Units", "Defeat opponents by killing all their units."],
	[8, "Conquest Civic Centers", "Defeat opponents by destroying all their fully built civic centers."],
	[32, "Capture the Relic", "Capture all relics spread across the map and keep them for a certain time to win the game."],
	[64, "Regicide", "Defeat opponents by killing their hero."],
	[16, "Wonder", "Be the first to build or capture a Wonder and keep it for a certain time to win the game. The timer will be reset when the Wonder is destroyed or captured. If the allied victory mode is enabled, the timer will also be reset when the alliances of the owner change."]]
const VK_REGICIDE := 64
# 0.28 victory condition file names (a scenario's settings VictoryConditions) -> VictoryKind bit.
const VICTORY_NAMES := {"conquest": 1, "conquest_units": 2, "conquest_structures": 4, "conquest_civic_centers": 8,
	"wonder": 16, "capture_the_relic": 32, "regicide": 64}
# DisabledWhenChecked: checking a condition unchecks and disables these.
const VICTORY_EXCLUDES := {1: 2 | 4 | 8, 2: 1, 4: 1 | 8, 8: 1 | 4}
# victory_mask() of each single code (index == rtso::VictoryCondition); VICTORY_COMBINED | mask otherwise.
const VICTORY_CODE_MASKS := [1, 1 | 16, 1 | 32, 0, 2, 4, 1 | 64]
const VICTORY_COMBINED := 0x80
var _victory_checks := {}

static func victory_mask(code: int) -> int:
	if code & VICTORY_COMBINED:
		return code & 0x7F
	return VICTORY_CODE_MASKS[code] if code >= 0 and code < VICTORY_CODE_MASKS.size() else 1

static func victory_code(mask: int) -> int:
	var i := VICTORY_CODE_MASKS.find(mask & 0x7F)
	return i if i >= 0 else VICTORY_COMBINED | (mask & 0x7F)

## "Conquest, Wonder" for a victory code; "Endless" when nothing is checked.
static func victory_title(code: int) -> String:
	var mask := victory_mask(code)
	var names := PackedStringArray()
	for k in VICTORY_KINDS:
		if mask & int(k[0]):
			names.append(k[1])
	return ", ".join(names) if not names.is_empty() else "Endless"
const CivAINames = preload("res://scenes/menu/civ_ai_names.gd")
var _color_icons: Array = []

var _maps: Array = []          # all map dicts
var _filtered: Array = []      # after filter
var _sel_map: int = 0
# The Player Placement picked (rtso::TeamPlacement), kept while maps change.
var _pop_type := POP_TYPE_PLAYER
var _team_placement := 0
var _biome := RANDOM_BIOME # the Biome pick (BIOMES index or RANDOM_BIOME), kept across maps that offer it
var _map_type := "skirmish"
var _map_filter_name := "default"
var _filter_names: Array = []  # the filters the map type has maps for, in dropdown order
var _want_map_id := ""         # the map to select on the next refresh_map_list
var _was_scenario := false     # the slots hold a scenario's locked teams
var _slot_rows: Array = []     # per-slot {kind,civ,team,diff,beh}
## Single player: the row you play (0.28 PlayerAssignment), -1 to watch the AIs.
var _local_slot := 0
## The player assignment entry that seats you (its other ids are the kinds).
const KIND_YOU := 10
var _map_browser: Control = null
var _desc_tick := 0.0
var _ai_buttons: Array = []
var _hotkeys := RtsoHotkeyConfig.new()
var _rebuilding_slots := false

func _ready() -> void:
	# 0.28's modern skin over the stone theme's panels and popups.
	var skin: Theme = theme.duplicate() if theme else Theme.new()
	skin.merge_with(MODERN.theme())
	theme = skin
	for b in TAB_BUTTONS:
		get_node("%" + b).theme_type_variation = "ModernTabButtonHorizontal"
	_build_description()
	_build_slot_headings()
	_load_maps()
	_build_map_type()
	_build_map_filter()
	_build_map_list()
	_build_options()
	_restore_settings()
	refresh_map_list()
	refresh_slots()
	# The panel has its width once the first layout is done.
	get_tree().process_frame.connect(_size_slot_columns, CONNECT_ONE_SHOT)
	_update_preview()
	_init_net()
	_render_reset_buttons()
	_build_tips()
	_build_tabs()
	_title_labels()

## GameSettingControl.js TitleCaptionFormat: every setting's title reads "%(setting)s:".
static func _title(caption: String) -> String:
	return L10n.sprintf(TranslationServer.translate("%(setting)s:"), {"setting": TranslationServer.translate(caption)})

func _title_labels() -> void:
	for tab in SETTING_TABS:
		for l in get_node("VBox/HBox/LeftScroll/Left/" + tab).find_children("*Label", "Label", true, false):
			if l.name != "SeedLabel" and not l.text.ends_with(":"):
				l.mouse_filter = Control.MOUSE_FILTER_PASS # its tooltip shows on hover
				l.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
				l.text = _title(l.text)

## GameSettingControl.updateVisibility: a setting the player can't change (a
## client's, a scenario's locked ones, a saved game's) shows its value as text,
## the label carrying the control's caption: a dropdown its item, a slider its
## caption, a checkbox Yes or No.
func _render_value_texts() -> void:
	var roots: Array = SETTING_TABS.map(func(t): return get_node("VBox/HBox/LeftScroll/Left/" + t))
	roots.append(_slots_grid())
	for root in roots:
		for c in root.find_children("*", "Control", true, false):
			if c is RtsoSafeSelect:
				# PlayerColor has no label: its locked swatch stays.
				_value_text(c, c.disabled and c.text.strip_edges() != "", c.text, c.get_theme_color("font_color"))
			elif c is CheckBox:
				_checkbox_value_text(c)
			elif c.has_method("get_slider"):
				c.get_slider().visible = c.get_slider().editable

func _value_text(c: Control, locked: bool, text: String, color: Color) -> void:
	var l: Label = c.get_node_or_null("ValueText")
	if not locked:
		if l:
			c.self_modulate = Color.WHITE
			l.queue_free()
			l.name = "ValueTextGone"
		return
	if l == null:
		l = Label.new()
		l.name = "ValueText"
		l.mouse_filter = Control.MOUSE_FILTER_IGNORE
		l.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		l.clip_text = true
		l.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
		c.add_child(l)
		l.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
		l.offset_left = 4
		c.self_modulate = Color.TRANSPARENT
	l.text = text
	l.add_theme_color_override("font_color", color)

## GameSettingControlCheckbox: "Yes" / "No" in place of the box.
var _no_icon: Texture2D
func _checkbox_value_text(c: CheckBox) -> void:
	if not c.has_meta("title"):
		if not c.disabled:
			return
		c.set_meta("title", c.text)
	var title := str(c.get_meta("title"))
	if not c.disabled:
		c.text = title
		c.remove_meta("title")
		for i in ["checked_disabled", "unchecked_disabled"]:
			c.remove_theme_icon_override(i)
		c.remove_theme_color_override("font_disabled_color")
		return
	c.text = "%s %s" % [_title(title), tr("Yes") if c.button_pressed else tr("No")]
	if _no_icon == null:
		_no_icon = ImageTexture.create_from_image(Image.create(1, 1, false, Image.FORMAT_RGBA8))
	for i in ["checked_disabled", "unchecked_disabled"]:
		c.add_theme_icon_override(i, _no_icon)
	c.add_theme_color_override("font_disabled_color", c.get_theme_color("font_color"))

# ---------------------------------------------------------------- tabs ---

## 0.28 GameSettingTabs.js and selectPanel: the Map, Player and Game Type buttons
## each open their settings (GameSettingsLayout.js); pressing the open one, or the
## cancel hotkey, closes it. A client, who can't change them, starts with none open.
const SETTING_TABS := ["MapTab", "PlayerTab", "GameTypeTab"]
const TAB_BUTTONS := ["TabMap", "TabPlayer", "TabGameType"]
var _tab := -1

func _build_tabs() -> void:
	var keys: Dictionary = _hotkeys.get_hotkey_map()
	for i in TAB_BUTTONS.size():
		var b: Button = get_node("%" + TAB_BUTTONS[i])
		b.pressed.connect(func(): select_tab(-1 if _tab == i else i))
		# tab_buttons.js placeTabButtons: the toggle line, then the scroll lines.
		b.tooltip_text = TAB_BUTTONS_JS.tooltip(b.tooltip_text, false, keys)
		b.gui_input.connect(func(ev: InputEvent):
			var d: int = TAB_BUTTONS_JS.wheel(ev)
			if d != 0:
				select_tab(TAB_BUTTONS_JS.next_index(_tab, d, TAB_BUTTONS.size()))
				b.accept_event())
	# A container re-sort puts the sliding panel back at 0; keep it where it is.
	(get_node("%TabButtons").get_parent() as Container).sort_children.connect(func():
		if _slide_panel != null:
			_slide_panel.position.x = _slide_x)
	select_tab(-1 if _mp_role == "client" else 0)

func select_tab(i: int) -> void:
	var was := _tab
	_tab = i
	var slide := RtsoUserConfig.new().get_bool("gui.gamesetup.settingsslide")
	for k in SETTING_TABS.size():
		var panel: Control = get_node("%" + SETTING_TABS[k])
		# The panel that closes with nothing taking its place stays up while it
		# slides out; the others switch at once, as 0.28's do.
		panel.visible = k == i or (slide and i == -1 and k == was)
		(get_node("%" + TAB_BUTTONS[k]) as Button).set_pressed_no_signal(k == i)
	if not slide:
		_slide_x = 0.0
		_slide_panel = null
		return
	if was == -1 and i != -1:
		_slide_panel = get_node("%" + SETTING_TABS[i])
		if _slide_x == 0.0:
			_slide_x = -_slide_width()
	elif i == -1 and was != -1:
		_slide_panel = get_node("%" + SETTING_TABS[was])
	elif i != -1:
		_slide_panel = get_node("%" + SETTING_TABS[i])

## 0.28 GameSettingsPanel.js onTick (gui.gamesetup.settingsslide): the settings
## panel slides in from the left at 1.2 px/ms when a tab opens and back out when
## the last one closes.
const SLIDE_SPEED := 1200.0 # px per second
var _slide_x := 0.0
var _slide_panel: Control = null

func _slide_width() -> float:
	return maxf((get_node("%TabButtons") as Control).size.x, 330.0)

func _tick_slide(delta: float) -> void:
	if _slide_panel == null:
		return
	var target := 0.0 if _tab != -1 else -_slide_width()
	_slide_x = move_toward(_slide_x, target, SLIDE_SPEED * delta)
	_slide_panel.position.x = _slide_x
	if _slide_x != target:
		return
	if _tab == -1:
		_slide_panel.visible = false
		_slide_panel.position.x = 0.0
		_slide_x = 0.0
	_slide_panel = null

# ---------------------------------------------------------------- maps ---

func _load_maps() -> void:
	_maps = load_all_maps()

## Every map Game Setup and the Map Browser list: data/maps scenarios and
## skirmishes (typed by map_types.json), then the random maps.
static func load_all_maps() -> Array:
	var maps: Array = []
	var dir := DirAccess.open("res://data/maps")
	if dir != null:
		dir.list_dir_begin()
		var file_name := dir.get_next()
		var scenario_ids: Array = []
		while file_name != "":
			# 0 A.D.'s tutorials (maps/tutorials) start from Learn to Play and the
			# Tutorial campaign; its Scenario type lists maps/scenarios only.
			if not dir.current_is_dir() and file_name.ends_with(".json") \
					and not ScenarioStart.TUTORIAL_MAPS.has(file_name.get_basename()):
				scenario_ids.append(file_name.get_basename())
			file_name = dir.get_next()
		scenario_ids.sort()
		for sid in scenario_ids:
			maps.append(_read_scenario(sid))
	if maps.is_empty():
		for p in ["acropolis_bay", "corinthian_isthmus", "atlas_valleys", "bactria", "battle_for_the_tiber"]:
			maps.append(_read_scenario(p))
	var types := _map_types_data()
	var known: Dictionary = types.get("maps", {})
	for m in maps:
		var t: Dictionary = known.get(m["id"], {})
		m["type"] = str(t.get("type", "skirmish"))
		m["kind"] = "Scenario" if m["type"] == "scenario" else "Skirmish"
		m["keywords"] = t.get("keywords", [])
		m["settings"] = t.get("settings", {})
	var random_kw: Dictionary = types.get("random_keywords", {})
	var placements: Dictionary = types.get("player_placements", {})
	var biomes: Dictionary = types.get("random_biomes", {})
	for r in RMS_MAPS:
		# 0 A.D. lists random maps by name alone: the Map Type says they are random.
		maps.append({"id": r[0], "name": str(r[1]).trim_suffix(" (RMS)"), "kind": r[2], "type": "random",
			"keywords": random_kw.get(rms_script(r[0]), []), "placements": [] if r[0] in OWN_RMS_MAPS or not (PLACEMENT_EVERY_MAP or r[0] in PLACEMENT_MAPS_BEFORE) else placement_values(placements.get(rms_script(r[0]), [])),
			"players": r[3],
			"size": r[4], "biome": r[5], "desc": rms_description(r),
			"biomes": [] if r[0] in OWN_RMS_MAPS else biome_values(biomes.get(rms_script(r[0]), []))})
	return maps

## The BIOMES entries a random map's 0.28 SupportedBiomes allows, in BIOMES order.
static func biome_values(ids: Array) -> Array:
	var out := []
	for id in ids:
		var i := int(RMBIOME_INDEX.get(str(id), 0))
		if i > 0 and not out.has(i):
			out.append(i)
	out.sort()
	return out

## The 0.28 maps/random script an RTS0 random map ports ("random_archipelago_0ad" -> "archipelago").
## MapCache.getTranslatedMapDescription: a random map's Description is its 0.28
## JSON's (maps/random/<script>.json settings.Description); RTS0's own maps keep theirs.
static func rms_description(row: Array) -> String:
	if row[0] in OWN_RMS_MAPS:
		return str(row[6])
	return str(_map_types_data().get("random_descriptions", {}).get(rms_script(row[0]), row[6]))

static func rms_script(map_id: String) -> String:
	var s := map_id.trim_prefix("random_").trim_suffix("_0ad")
	return "neareastern_badlands" if s == "nearestern_badlands" else s

## 0.28 Dropdowns/Biome.js: Random (the default), then only the map's SupportedBiomes,
## hidden on a map with none; the pick stays when the new map offers it, else Random.
func _rebuild_biomes(values: Array) -> void:
	var slot := get_node_or_null("%BiomeSlot")
	if slot == null or slot.get_child_count() == 0:
		return
	var bio: RtsoSafeSelect = slot.get_child(0)
	bio.clear()
	bio.add_item(tr("Random"), RANDOM_BIOME)
	bio.set_item_tooltip(0, tr("Pick a biome at random."))
	bio.set_item_color(0, RANDOM_ITEM_COLOR)
	for v in values:
		bio.add_item(GameDescription.BIOME_TEXT.get(v, [BIOMES[v]])[0], v) # the rmbiome's Title
		bio.set_item_tooltip(bio.get_item_count() - 1, GameDescription.BIOME_TEXT.get(v, ["", ""])[1])
	bio.select(maxi(([RANDOM_BIOME] + values).find(_biome), 0))
	slot.visible = not values.is_empty()
	%BiomeLabel.visible = not values.is_empty()

func _selected_biome() -> int:
	var slot := get_node_or_null("%BiomeSlot")
	if slot == null or slot.get_child_count() == 0 or not slot.visible:
		return 0
	var bio: RtsoSafeSelect = slot.get_child(0)
	return bio.get_item_id(bio.get_selected()) if bio.get_selected() >= 0 else RANDOM_BIOME

func _on_biome_selected(_index: int) -> void:
	_biome = _selected_biome()

## 0.28 PlayerPlacement.render: Random, then the map's patterns; the pick stays
## when the new map offers it, else Random.
func _rebuild_team_placement(placements: Array) -> void:
	var slot := get_node_or_null("%TeamPlacementSlot")
	if slot == null or slot.get_child_count() == 0:
		return
	var tpl: RtsoSafeSelect = slot.get_child(0)
	tpl.clear()
	tpl.add_item(TEAM_PLACEMENTS[0][1], 0)
	tpl.set_item_color(0, RANDOM_ITEM_COLOR)
	tpl.set_item_tooltip(0, tr(TEAM_PLACEMENTS[0][2]))
	for v in placements:
		tpl.add_item(TEAM_PLACEMENTS[v][1], v)
		tpl.set_item_tooltip(tpl.get_item_count() - 1, tr(TEAM_PLACEMENTS[v][2]))
	var i := ([0] + placements).find(_team_placement)
	tpl.select(maxi(i, 0))
	_update_team_placement_tooltip()

func _selected_team_placement() -> int:
	var slot := get_node_or_null("%TeamPlacementSlot")
	if slot == null or slot.get_child_count() == 0 or not slot.visible:
		return 0
	var tpl: RtsoSafeSelect = slot.get_child(0)
	return maxi(tpl.get_item_id(tpl.get_selected()), 0) if tpl.get_selected() >= 0 else 0

func _on_team_placement_selected(_index: int) -> void:
	_team_placement = _selected_team_placement()
	_update_team_placement_tooltip()

## The selected pattern's description (0.28 shows the hovered one), else the title's.
func _update_team_placement_tooltip() -> void:
	var slot := get_node_or_null("%TeamPlacementSlot")
	if slot == null or slot.get_child_count() == 0:
		return
	var tpl: RtsoSafeSelect = slot.get_child(0)
	# PlayerPlacement.onHoverChange: the hovered pattern's description shows in the open list.
	tpl.tooltip_text = tr(TEAM_PLACEMENT_TOOLTIP)
	var lbl := get_node_or_null("%TeamPlacementLabel")
	if lbl:
		lbl.tooltip_text = TEAM_PLACEMENT_TOOLTIP

## A map's 0.28 PlayerPlacements ids as rtso::TeamPlacement values.
static func placement_values(ids: Array) -> Array:
	var out := []
	for id in ids:
		var v := TEAM_PLACEMENTS.map(func(p): return p[0]).find(str(id))
		if v > 0 and (v != 5 or PLACEMENT_EVERY_MAP):
			out.append(v)
	return out

static func _map_types_data() -> Dictionary:
	var f := FileAccess.open(MAP_TYPES_FILE, FileAccess.READ)
	var d: Variant = JSON.parse_string(f.get_as_text()) if f else null
	return d if typeof(d) == TYPE_DICTIONARY else {}

## globalscripts MatchesClassList: any entry whose space-separated classes all
## hold ("!x" = x absent); "!" alone matches every map.
static func matches_class_list(classes: Array, match: Array) -> bool:
	for sub in match:
		var ok := true
		for c in str(sub).split(" ", false):
			if c.begins_with("!") == classes.has(c.substr(1) if c.begins_with("!") else c):
				ok = false
				break
		if ok:
			return true
	return false

static func _read_scenario(map_id: String) -> Dictionary:
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
	# The map's players in player order (the JSON lists them by "id"), whose civs
	# and teams load_map_seeded applies.
	pl = pl.filter(func(p): return typeof(p) == TYPE_DICTIONARY)
	pl.sort_custom(func(a, b): return int(a.get("id", 0)) < int(b.get("id", 0)))
	d["map_players"] = pl
	return d

func _pop_type_select() -> RtsoSafeSelect:
	return %PopCapTypeSlot.get_child(0)

func _pop_select() -> RtsoSafeSelect:
	return %PopCapSlot.get_child(0)

## Selects the type and cap a pop_cap setting (and team_pop) stand for, as
## 0.28's PopulationCapType and PopulationCap dropdowns show them.
func _select_pop(setting: int, team: bool) -> void:
	var type := POP_TYPE_TEAM if team and setting > 0 else (POP_TYPE_WORLD if setting < 0 else POP_TYPE_PLAYER)
	var cap := absi(setting) if setting != 0 else POP_UNLIMITED
	_set_pop_type(type, cap)

## PopulationCapType.onSelectionChange: the new type's caps at its default.
func _on_pop_type_selected(i: int) -> void:
	_set_pop_type(clampi(i, 0, POP_CAP_TYPES.size() - 1), -1)

func _set_pop_type(type: int, cap: int) -> void:
	_pop_type = type
	var t: Array = POP_CAP_TYPES[type]
	_pop_type_select().select(type)
	_pop_type_select().tooltip_text = tr(t[2])
	%PopCapLabel.text = _title(t[3])
	var pop := _pop_select()
	pop.clear()
	for v in t[5]:
		pop.add_item(tr("Unlimited") if int(v) >= POP_UNLIMITED else str(v))
	var at: int = t[5].find(cap)
	pop.select(at if at >= 0 else t[5].find(t[6]))
	pop.tooltip_text = tr(t[4])
	_render_pop_tips()

## 0.28 PopulationCap.onHoverChange: an entry whose cap lets the match pass 1200 units
## (players × cap, teams × cap with each teamless player a team, or the world cap) warns
## of lag instead of the cap type's tooltip.
const POP_RECOMMENDATION := 1200
func _render_pop_tips() -> void:
	if %PopCapSlot.get_child_count() == 0:
		return
	var pop := _pop_select()
	var t: Array = POP_CAP_TYPES[_pop_type]
	var n := mini(int(_slot_count_node().value), _slot_rows.size())
	var teams := {}
	for i in range(n):
		var team := int(_slot_rows[i]["team"])
		teams[team if team > 0 else -1 - i] = true
	for i in range(mini(pop.get_item_count(), (t[5] as Array).size())):
		var cap := int(t[5][i])
		var total := cap
		match _pop_type:
			POP_TYPE_PLAYER:
				total = n * cap
			POP_TYPE_TEAM:
				total = teams.size() * cap
		pop.set_item_tooltip(i, tr("Warning: These settings can result in significant lag when all players reach their maximum population capacity.")
			if total > POP_RECOMMENDATION else tr(t[4]))
	_update_lock_teams_for_pop()

## The selected cap as RTS0's pop_cap setting.
func _pop_cap_setting() -> int:
	var caps: Array = POP_CAP_TYPES[_pop_type][5]
	var v := int(caps[clampi(_pop_select().get_selected(), 0, caps.size() - 1)])
	match _pop_type:
		POP_TYPE_PLAYER:
			return 0 if v >= POP_UNLIMITED else v
		POP_TYPE_WORLD:
			return -v
	return v

## 0.28 LockedTeams: Team Population turns Locked teams on and keeps it on.
func _update_lock_teams_for_pop() -> void:
	var lock: CheckBox = %LockTeams
	if _pop_type == POP_TYPE_TEAM:
		lock.button_pressed = true
		lock.disabled = true
	else:
		lock.disabled = _is_scenario() or _rated_locked
	_render_mode_checks()

## GameSettingControlSlider: one step per item, read as the dropdowns are.
func _make_slider(slot: Control) -> SettingSlider:
	for c in slot.get_children():
		c.queue_free()
	var sl := SettingSlider.new()
	sl.set_anchors_preset(Control.PRESET_FULL_RECT)
	slot.add_child(sl)
	return sl

func _make_option(slot: Control) -> RtsoSafeSelect:
	for c in slot.get_children():
		c.queue_free()
	var ob := RtsoSafeSelect.new()
	ob.set_anchors_preset(Control.PRESET_FULL_RECT)
	slot.add_child(ob)
	return ob

## 0 A.D. gamesetup MapType dropdown.
func _build_map_type() -> void:
	var t := _make_option(%MapTypeSlot)
	for mt in MAP_TYPES:
		t.add_item(mt[1])
		t.set_item_tooltip(t.get_item_count() - 1, tr(mt[2])) # MapType: g_MapTypes.Description
	t.select(0)
	_update_map_type_tooltip()
	t.item_selected.connect(_on_map_type_selected)

func _update_map_type_tooltip() -> void:
	var t: RtsoSafeSelect = %MapTypeSlot.get_child(0)
	for i in range(MAP_TYPES.size()):
		if MAP_TYPES[i][0] == _map_type:
			t.select(i)
			# MapType.onHoverChange: the hovered type's description shows in the open list.
			t.tooltip_text = tr("Select a map type.")
			%MapTypeLabel.tooltip_text = t.tooltip_text

func _on_map_type_selected(idx: int) -> void:
	_map_type = MAP_TYPES[clampi(idx, 0, MAP_TYPES.size() - 1)][0]
	_update_map_type_tooltip()
	_rebuild_map_filter()
	refresh_map_list()
	_clamp_slots_to_map()

## 0 A.D. gamesetup MapFilter dropdown: the filters with maps of this type.
func _build_map_filter() -> void:
	var f := _make_option(%MapFilterSlot)
	f.item_selected.connect(_on_map_filter_selected)
	_rebuild_map_filter()

## MapFilters.getAvailableMapFilters: the MAP_FILTERS entries with maps of the type.
static func available_filters(maps: Array, map_type: String) -> Array:
	return MAP_FILTERS.filter(func(flt): return maps.any(func(m): return m["type"] == map_type and matches_class_list(m["keywords"], flt[3])))

## A text field has the keys (the chat input's Tab completes names).
func _focus_is_text() -> bool:
	var f := get_viewport().gui_get_focus_owner()
	return f is LineEdit or f is TextEdit

## MapFilters.getFilteredMaps: the maps of the type the filter matches, by name,
## after the Random entry for random maps.
static func filtered_maps(maps: Array, map_type: String, filter_name: String) -> Array:
	var classes: Array = MAP_FILTERS[0][3]
	for flt in MAP_FILTERS:
		if flt[0] == filter_name:
			classes = flt[3]
	var out := maps.filter(func(m): return m["type"] == map_type and matches_class_list(m["keywords"], classes))
	out.sort_custom(func(a, b): return str(a["name"]).naturalnocasecmp_to(str(b["name"])) < 0)
	if map_type == "random":
		out.push_front(RANDOM_MAP)
	return out

func _rebuild_map_filter() -> void:
	var f: RtsoSafeSelect = %MapFilterSlot.get_child(0)
	f.clear()
	_filter_names.clear()
	for flt in available_filters(_maps, _map_type):
		_filter_names.append(flt[0])
		f.add_item(flt[1])
		f.set_item_tooltip(f.get_item_count() - 1, tr(flt[2])) # MapFilter: Description
	if not _filter_names.has(_map_filter_name):
		_map_filter_name = _filter_names[0] if not _filter_names.is_empty() else "default"
	var shown := not _filter_names.is_empty()
	%MapFilterLabel.visible = shown
	%MapFilterSlot.visible = shown
	if shown:
		var i := _filter_names.find(_map_filter_name)
		f.select(i)
		f.tooltip_text = tr("Select a map filter.")
		%MapFilterLabel.tooltip_text = f.tooltip_text

func _filter_index(name: String) -> int:
	for i in range(MAP_FILTERS.size()):
		if MAP_FILTERS[i][0] == name:
			return i
	return 0

func _on_map_filter_selected(idx: int) -> void:
	if idx < 0 or idx >= _filter_names.size():
		return
	_map_filter_name = _filter_names[idx]
	_rebuild_map_filter()
	refresh_map_list()
	_clamp_slots_to_map()

## Shows map_id: its type, and a filter that lists it ("All Maps" for one the
## default filter hides).
func _select_map_id(map_id: String) -> void:
	var m := {}
	for x in _maps:
		if str(x["id"]) == map_id:
			m = x
	if map_id == RANDOM_MAP_ID:
		m = RANDOM_MAP
	if m.is_empty():
		return
	_map_type = str(m["type"])
	_update_map_type_tooltip()
	if map_id != RANDOM_MAP_ID and not matches_class_list(m["keywords"], MAP_FILTERS[_filter_index(_map_filter_name)][3]):
		_map_filter_name = "all"
	_rebuild_map_filter()
	_want_map_id = map_id

## MapSelection.js: the Select Map dropdown, each map's description on hover.
func _build_map_list() -> void:
	var list: RtsoSafeSelect = %MapList
	list.clear()
	list.item_selected.connect(_on_map_list_selected)
	# 0.28 Single/Buttons/MapBrowser.js "Browse Maps" and Panels/MapPreview.js: both open the Map Browser.
	var browse := Button.new()
	browse.name = "BrowseMaps"
	browse.text = tr("Browse Maps")
	browse.tooltip_text = _hotkeys.colorize(L10n.t("Press %(hotkey)s to view the list of available maps."), MAP_BROWSER_HOTKEY)
	browse.pressed.connect(_open_map_browser)
	list.get_parent().add_child(browse)
	list.get_parent().move_child(browse, list.get_index() + 1)
	var preview := $VBox/HBox/Right/Preview as Control
	preview.tooltip_text = tr("Click to view the list of available maps.")
	preview.mouse_filter = Control.MOUSE_FILTER_STOP
	preview.gui_input.connect(func(ev):
		if ev is InputEventMouseButton and ev.pressed and ev.button_index == MOUSE_BUTTON_LEFT:
			_open_map_browser())

## MapBrowserPage.openPage: on the current type, filter and map; only the
## controller picks (a client looks).
func _open_map_browser() -> void:
	# MapPreview.js / MapBrowser.js: a saved game's map is fixed, so neither opens.
	if (_map_browser != null and is_instance_valid(_map_browser)) or _saved_game():
		return
	_map_browser = MapBrowser.open_overlay(self, _maps, _map_type, _map_filter_name, str(_selected_map().get("id", "")), _mp_role != "client")
	_map_browser.map_chosen.connect(_on_map_browsed)

## MapBrowserPage.onSubmitMapSelection: the type, then the filter, then the map.
func _on_map_browsed(map_id: String, map_type: String, map_filter: String) -> void:
	if not map_type.is_empty():
		_map_type = map_type
		_update_map_type_tooltip()
	if not map_filter.is_empty():
		_map_filter_name = map_filter
	_rebuild_map_filter()
	if not map_id.is_empty():
		_want_map_id = map_id
	refresh_map_list()
	_clamp_slots_to_map()

func _unhandled_input(event: InputEvent) -> void:
	if not (event is InputEventKey and event.pressed and not event.echo):
		return
	var fired = _hotkeys.fired(event)
	if MAP_BROWSER_HOTKEY in fired:
		get_viewport().set_input_as_handled()
		_open_map_browser()
	elif "civinfo" in fired or "structree" in fired:
		get_viewport().set_input_as_handled()
		# CivInfoPage / StructreePage: a page's own hotkey closes it.
		var want := 0 if "civinfo" in fired else 1
		var open := get_node_or_null("CivReference")
		if open and (open.find_child("Pages", true, false) as TabContainer).current_tab == want:
			open.queue_free()
		else:
			open_civ_reference(want)
	elif "lobby" in fired and LobbySession.signed_in():
		# LobbyButton.xml hotkey="lobby": the lobby dialog over the setup.
		get_viewport().set_input_as_handled()
		open_lobby_dialog()
	elif "cancel" in fired and get_node_or_null("CivReference"):
		get_viewport().set_input_as_handled()
		get_node("CivReference").queue_free()
	elif "cancel" in fired and _tab >= 0:
		get_viewport().set_input_as_handled()
		select_tab(-1)
	elif ("tab.next" in fired or "tab.prev" in fired) and not _focus_is_text():
		get_viewport().set_input_as_handled()
		select_tab(TAB_BUTTONS_JS.next_index(_tab, 1 if "tab.next" in fired else -1, TAB_BUTTONS.size()))

## CivInfoButton: the Civilization Overview (0) or Structure Tree (1) over the setup, on
## slot 1's civilization; the two hotkeys switch between them (pageLoop) and the
## button reopens the last one viewed, on the civilization last viewed (pageLoop's args).
var _civ_ref_tab := 0
var _civ_ref_civ := -1
func open_civ_reference(tab: int) -> void:
	_civ_ref_tab = tab
	var layer := get_node_or_null("CivReference") as CanvasLayer
	if layer:
		(layer.find_child("Pages", true, false) as TabContainer).current_tab = tab
		return
	layer = CanvasLayer.new()
	layer.name = "CivReference"
	layer.layer = 20
	var dim := ColorRect.new()
	dim.color = Color(0, 0, 0, 0.6)
	dim.set_anchors_preset(Control.PRESET_FULL_RECT)
	layer.add_child(dim)
	var panel := PanelContainer.new()
	panel.theme = theme
	panel.theme_type_variation = "ModernDialogContainer"
	panel.set_anchors_preset(Control.PRESET_FULL_RECT)
	panel.offset_left = 40
	panel.offset_top = 30
	panel.offset_right = -40
	panel.offset_bottom = -30
	layer.add_child(panel)
	var vbox := VBoxContainer.new()
	panel.add_child(vbox)
	var tabs := TabContainer.new()
	tabs.name = "Pages"
	tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vbox.add_child(tabs)
	var civinfo = load("res://scenes/reference/civinfo_page.gd").new()
	var tree = load("res://scenes/reference/structree_page.gd").new()
	tabs.add_child(civinfo)
	tabs.add_child(tree)
	civinfo.civ_changed.connect(tree.set_civ)
	tree.civ_changed.connect(civinfo.set_civ)
	civinfo.civ_changed.connect(func(c): _civ_ref_civ = c)
	tree.civ_changed.connect(func(c): _civ_ref_civ = c)
	var civ := _civ_ref_civ if _civ_ref_civ >= 0 else _slot_civ(maxi(_local_slot, 0))
	if _civ_pos(civ) >= 0: # not Random
		civinfo.set_civ.call_deferred(civ)
		tree.set_civ.call_deferred(civ)
	tabs.current_tab = tab
	var close := Button.new()
	close.name = "Close"
	# CloseButton: the close key and the page it closes.
	var close_tip := func() -> void:
		close.tooltip_text = _hotkeys.colorize(L10n.t("%(hotkey)s: Close Structure Tree.") if tabs.current_tab == 1
			else L10n.t("%(hotkey)s: Close Civilization Overview."), "cancel")
	tabs.tab_changed.connect(func(i):
		_civ_ref_tab = i
		close_tip.call())
	close_tip.call()
	close.text = tr("Close")
	close.custom_minimum_size = Vector2(160, 32)
	close.size_flags_horizontal = Control.SIZE_SHRINK_END
	close.pressed.connect(layer.queue_free)
	vbox.add_child(close)
	add_child(layer)

## MapFilters.getFilteredMaps: the maps of the type the filter matches, by
## name, after the Random entry for random maps; keeps the selected map if listed.
func refresh_map_list() -> void:
	var keep := _want_map_id if not _want_map_id.is_empty() else str(_selected_map().get("id", ""))
	_want_map_id = ""
	var list: RtsoSafeSelect = %MapList
	list.clear()
	_filtered = filtered_maps(_maps, _map_type, _map_filter_name)
	_sel_map = 0
	for i in range(_filtered.size()):
		list.add_item(str(_filtered[i]["name"]))
		var desc := str(_filtered[i].get("desc", ""))
		list.set_item_tooltip(i, tr(desc) if desc != "" else tr("Select a map to play on."))
		if str(_filtered[i]["id"]) == RANDOM_MAP_ID:
			list.set_item_color(i, RANDOM_ITEM_COLOR)
		if str(_filtered[i]["id"]) == keep:
			_sel_map = i
	if not _filtered.is_empty():
		list.select(_sel_map)
	_apply_map_locks()
	_update_preview()

func _on_map_list_selected(idx: int) -> void:
	_sel_map = idx
	_clamp_slots_to_map()
	_apply_map_locks()
	_update_preview()

func _is_scenario() -> bool:
	return str(_selected_map().get("type", "")) == "scenario"

## The scenario's player entry for slot i from its map JSON (0.28's PlayerData,
## tools/port_0ad_maps.py), {} otherwise.
func _map_player(i: int) -> Dictionary:
	if not _is_scenario():
		return {}
	var pl: Array = _selected_map().get("map_players", [])
	return pl[i] if i < pl.size() else {}

## A map player's civ as a civ id (the JSON's civ code or number), -1 when unset.
func _map_civ(p: Dictionary) -> int:
	var c: Variant = p.get("civ", -1)
	if typeof(c) == TYPE_STRING:
		return _civ_index(c)
	return int(c) if _civ_pos(int(c)) >= 0 else -1

func _civ_index(code: String) -> int:
	for c in CIVS:
		if c["code"] == code:
			return int(c["id"])
	return -1

## Position of civ id in CIVS (and the slot civ menu), -1 if not listed.
func _civ_pos(id: int) -> int:
	for i in range(CIVS.size()):
		if int(CIVS[i]["id"]) == id:
			return i
	return -1

## 0 A.D. gamesetup on a scenario ("predefined matchsettings"): the player count
## is the map's, and civs, teams, colours, population, starting resources,
## ceasefire, Last Man Standing, map exploration, treasures and spies are the
## map's, shown disabled. Only random maps let you pick the player count.
func _apply_map_locks() -> void:
	_render_reset_buttons()
	var m := _selected_map()
	var scen := _is_scenario()
	var s: Dictionary = m.get("settings", {}) if scen else {}
	_slot_count_node().editable = m.is_empty() or str(m.get("type", "")) == "random"
	# PlayerCount.render: setEnabled(map type random).
	if _player_count_select():
		_player_count_select().disabled = not _slot_count_node().editable or _mp_role == "client"
	# 0.28 MapSize.available: random maps only.
	%MapSizeLabel.visible = _slot_count_node().editable
	%MapSizeSlot.visible = _slot_count_node().editable
	var checks := {"LastManStanding": "LastManStanding", "StartRow/LockTeams": "LockTeams",
		"StartRow/DisableTreasures": "DisableTreasures", "MapVisRow/RevealMap": "RevealMap",
		"MapVisRow/ExploreMap": "ExploreMap", "MapVisRow/AlliedView": "AllyView", "MapVisRow/DisableSpies": "DisableSpies"}
	for path in checks:
		var cb: BaseButton = get_node("%" + path.get_file())
		cb.disabled = scen
		if scen:
			cb.button_pressed = bool(s.get(checks[path], false))
	_render_mode_checks()
	var pop: RtsoSafeSelect = %PopCapSlot.get_child(0)
	var res: RtsoSafeSelect = %ResSlot.get_child(0)
	var cf: SettingSlider = %CeasefireSlot.get_child(0)
	pop.disabled = scen
	_pop_type_select().disabled = scen
	res.disabled = scen
	cf.disabled = scen
	# VictoryConditions.onMapChange: a scenario's list replaces the conditions (an empty
	# one, as the sandboxes have, is an endless game); VictoryConditionCheckbox disables
	# every box on a scenario.
	if scen and typeof(s.get("VictoryConditions")) == TYPE_ARRAY:
		var vmask := 0
		for n in s["VictoryConditions"]:
			vmask |= int(VICTORY_NAMES.get(str(n), 0))
		_set_victory_mask(vmask)
	else:
		_set_victory_mask(_victory_mask())
	if scen:
		for bit in _victory_checks:
			_victory_checks[bit].disabled = true
	if not scen:
		pop.select(pop.get_selected())  # back from "Per Player"
		pop.tooltip_text = tr(POP_CAP_TYPES[_pop_type][4])
		_update_lock_teams_for_pop()
		res.select(res.get_selected())
		if _was_scenario:
			# The scenario's teams would leave allies without an enemy: no team, as 0 A.D. starts.
			for r in _slot_rows:
				r["team"] = 0
		_was_scenario = false
		apply_map_colors(m.get("map_players", []))
		refresh_slots()
		return
	_was_scenario = true
	cf.select(maxi(CEASEFIRES.find(int(s.get("Ceasefire", 0))), 0))
	var players: Array = s.get("players", [])
	_select_pop(int(s.get("PopulationCap", 300)), false)
	# InitGame.js SetPerPlayerPopulationCaps: the map file's pop_limit per player.
	var map_players: Array = m.get("map_players", [])
	var per_player: Array = map_players.filter(func(p): return p.has("pop_limit"))
	if not per_player.is_empty() or players.any(func(p): return typeof(p) == TYPE_DICTIONARY and p.has("pop")):
		pop.text = "Per Player"
	res.select(maxi(START_RES_AMOUNTS.find(int(s.get("StartingResources", 300))), 0))
	if players.any(func(p): return typeof(p) == TYPE_DICTIONARY and p.has("resources")):
		res.text = "Per Player"
	_ensure_slot_rows(int(_slot_count_node().value))
	for i in range(_slot_rows.size()):
		var p := _map_player(i)
		var c := _map_civ(p)
		if c >= 0:
			_slot_rows[i]["civ"] = c
		if not p.is_empty():
			_slot_rows[i]["team"] = clampi(int(p.get("team", -1)) + 1, 0, 4)
	apply_map_colors(map_players)
	refresh_slots()

# 0 A.D. shows WaterLevel and SeaLevelRiseTime only for the maps whose triggers
# read them, and TeamPlacement only for the maps that list placement patterns.
func _update_map_options() -> void:
	var id := str(_selected_map().get("id", ""))
	var row := %MapOptionsRow
	if row == null:
		return
	%WaterLevelLabel.visible = id == "random_flood"
	%WaterLevelSlot.visible = id == "random_flood"
	%SeaRiseLabel.visible = id == "random_extinct_volcano"
	%SeaRiseSlot.visible = id == "random_extinct_volcano"
	var placements: Array = _selected_map().get("placements", [])
	var tp := not placements.is_empty()
	_rebuild_team_placement(placements)
	_rebuild_biomes(_selected_map().get("biomes", []))
	%TeamPlacementLabel.visible = tp
	%TeamPlacementSlot.visible = tp
	var ls := id in LANDSCAPE_MAPS
	%LandscapeLabel.visible = ls
	%LandscapeSlot.visible = ls
	var td := id in TRIGGER_DIFFICULTY_MAPS
	%TriggerDifficultyLabel.visible = td
	%TriggerDifficultySlot.visible = td
	var dt := DAYTIMES.has(id)
	%DaytimeLabel.visible = dt
	%DaytimeSlot.visible = dt
	if dt:
		_fill_daytimes(id)
	row.visible = id == "random_flood" or id == "random_extinct_volcano" or tp or ls or td or dt or %BiomeLabel.visible

## The Daytime dropdown for map `id`: Random, then the map's own options.
func _fill_daytimes(id: String) -> void:
	var slot := %DaytimeSlot
	if slot.get_child_count() == 0:
		return
	var dsel: RtsoSafeSelect = slot.get_child(0)
	var keep := maxi(dsel.get_selected(), 0)
	dsel.clear()
	dsel.add_item(tr("Random"))
	dsel.set_item_tooltip(0, tr("Randomly pick a time of the day."))
	dsel.set_item_color(0, RANDOM_ITEM_COLOR)
	for d in DAYTIMES[id]:
		dsel.add_item(tr(d[0]))
		dsel.set_item_tooltip(dsel.get_item_count() - 1, tr(d[1]))
	%DaytimeLabel.tooltip_text = tr("Select whether the match takes place at daylight or night.")
	dsel.select(clampi(keep, 0, dsel.get_item_count() - 1))

func _selected_map() -> Dictionary:
	if _filtered.is_empty():
		return {}
	return _filtered[clampi(_sel_map, 0, _filtered.size() - 1)]

## The map's preview image (assets/maps/previews, tools/fetch_0ad_previews.py): 0 A.D.'s
## own for ported maps and maps from 0 A.D. scripts, a drawn one for RTS0's own random
## maps; null when the map has none (some sandbox scenarios).
static func map_preview(map_id: String) -> Texture2D:
	var path := "res://assets/maps/previews/%s.jpg" % map_id
	return load(path) as Texture2D if ResourceLoader.exists(path) else null

func _update_preview() -> void:
	_update_map_options()
	var m := _selected_map()
	var lbl := get_node_or_null("VBox/HBox/Right/Preview/Info/Label") as Label
	var img := $VBox/HBox/Right/Preview/Image as TextureRect
	var info := get_node_or_null("VBox/HBox/Right/Preview/Info") as Control
	if img:
		img.texture = null if m.is_empty() else map_preview(str(m["id"]))
		img.visible = img.texture != null
		if info: info.offset_left = 192.0 if img.visible else 12.0
	# MapPreview.js: the translated map name, or "No selected map".
	if m.is_empty():
		if lbl: lbl.text = tr("No selected map")
		return
	if lbl:
		lbl.text = tr(str(m["name"]))
	_update_description()

## 0.28 Panels/GameDescription.js: getGameDescription of the settings as they
## stand, a line per setting, under the Map Preview's name and description.
func _update_description() -> void:
	var box := get_node_or_null("VBox/HBox/Right/Preview/Info/GameDescription") as RichTextLabel
	if box == null or _rebuilding_slots:
		return
	var text := ""
	var m := _selected_map()
	if not m.is_empty():
		text = GameDescription.describe(description_info())
	if text != box.text:
		box.text = text

## The settings as the in-game Match Settings dialog reads them
## (RtsoMain.get_match_settings_info keys).
func description_info() -> Dictionary:
	var st := _collect_setup()
	var m: Dictionary = st.get("map", {})
	var pop := int(st["pop_cap"])
	if bool(st["team_pop"]) and pop > 0:
		pop = -(pop + GameDescription.TEAM_POP_FLAG)
	return {"map_id": str(m.get("id", "")), "map_name": str(m.get("name", "")),
		"victory_mask": victory_mask(int(st["victory"])), "relic_min": st["relic_min"], "relic_count": st["relic_count"],
		"regicide_garrison": st["regicide_garrison"], "wonder_min": st["wonder_min"], "lock_teams": st["lock_teams"],
		"last_man_standing": st["lms"], "ceasefire_min": st["ceasefire"], "map_size": st["map_size"],
		"team_placement": st["team_placement"], "trigger_difficulty": st["trigger_difficulty"], "nomad": st["nomad"],
		"start_res": st["res"], "pop_cap": pop, "disable_treasures": st["disable_treasures"],
		"explore_map": st["explore"], "reveal_map": st["reveal"], "ally_view": st["ally_view"],
		"cheats": %Cheats.button_pressed if _mp_role != "none" else true,
		"biome": _selected_biome(), "rated": _rated != null and _rated.button_pressed,
		"player_count": (st["types"] as Array).size(), "lobby_game": _mp_role != "none" and LobbySession.signed_in()}

## The description scrolls in the Map Preview panel, beside the preview under
## the map's name line; the panel takes the page's spare height.
# 0.28 Panels/TipsPanel.js: a hint for newcomers under the player slots, in single
# player only (multiplayer shows the chat there), until its tick box is cleared
# (gui.gamesetup.enabletips).
const SP_TIP := "Although reasonably easy for an experienced player, the default AI level is quite challenging for new players yet to master the basic mechanisms of the game. Beginners are advised to start by playing against a lower level AI (Sandbox or Very Easy). Change the AI level by clicking the gear icon next to the player you want to modify in the selection panel above."

func _build_tips() -> void:
	if net_main != null or not RtsoUserConfig.new().get_bool("gui.gamesetup.enabletips"):
		return
	var panel := PanelContainer.new()
	panel.name = "SPTips"
	var box := VBoxContainer.new()
	panel.add_child(box)
	var row := HBoxContainer.new()
	box.add_child(row)
	var gear := Button.new() # _draw_gear draws on a button; this one is only the icon
	gear.flat = true
	gear.mouse_filter = Control.MOUSE_FILTER_IGNORE
	gear.focus_mode = Control.FOCUS_NONE
	gear.custom_minimum_size = Vector2(24, 24)
	gear.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
	gear.draw.connect(func(): _draw_gear(gear))
	row.add_child(gear)
	var text := Label.new()
	text.name = "AITips"
	text.text = tr(SP_TIP)
	text.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	text.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	text.add_theme_font_size_override("font_size", 12)
	row.add_child(text)
	var tick := CheckBox.new()
	tick.name = "DisplaySPTips"
	tick.text = tr("Show this message in the future.")
	tick.button_pressed = true
	tick.toggled.connect(func(on: bool):
		var cfg := RtsoUserConfig.new()
		cfg.set_value("gui.gamesetup.enabletips", "true" if on else "false")
		cfg.save())
	box.add_child(tick)
	$VBox/HBox/Right.add_child(panel)

func _build_description() -> void:
	var preview := $VBox/HBox/Right/Preview
	var lbl := $VBox/HBox/Right/Preview/Label as Label
	var info := VBoxContainer.new()
	info.name = "Info"
	info.set_anchors_preset(Control.PRESET_FULL_RECT)
	info.offset_left = lbl.offset_left
	info.offset_top = 6
	info.offset_right = -6
	info.offset_bottom = -6
	preview.add_child(info)
	lbl.reparent(info)
	lbl.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
	var box := RichTextLabel.new()
	box.name = "GameDescription"
	box.bbcode_enabled = true
	box.scroll_active = true
	box.size_flags_vertical = Control.SIZE_EXPAND_FILL
	box.add_theme_font_size_override("normal_font_size", 13)
	info.add_child(box)
	# PlayersPanel.xml civResetButton / teamResetButton: 16 px IconButtons at the
	# Civilization and Team headings, iconResetGold, iconResetWhite on hover.
	var header: Control = $VBox/HBox/Right/SlotsBox/SlotsHeader
	for b in [["ResetCivs", "Reset any civilizations that have been selected to the default (random).", _on_reset_civs],
			["ResetTeams", "Reset all teams to the default.", _on_reset_teams]]:
		var btn := TextureButton.new()
		btn.name = b[0]
		btn.texture_normal = load("res://assets/textures/ui/global/icon/reset_gold.png")
		btn.texture_hover = load("res://assets/textures/ui/global/icon/reset_white.png")
		btn.ignore_texture_size = true
		btn.stretch_mode = TextureButton.STRETCH_SCALE
		btn.size = Vector2(16, 16)
		btn.tooltip_text = tr(b[1])
		btn.pressed.connect(b[2])
		header.add_child(btn)

## 0.28 Buttons/ResetCivsButton.js: every player's civ back to Random.
func _on_reset_civs() -> void:
	for r in _slot_rows:
		r["civ"] = RANDOM_CIV
	refresh_slots()
	_update_preview()

## 0.28 Buttons/ResetTeamsButton.js: every player back to no team.
func _on_reset_teams() -> void:
	for r in _slot_rows:
		r["team"] = 0
	refresh_slots()

## The reset buttons are the controller's, and a scenario sets the civs and teams.
func _render_reset_buttons() -> void:
	for n in ["ResetCivs", "ResetTeams"]:
		var b := get_node_or_null("VBox/HBox/Right/SlotsBox/SlotsHeader/" + n) as Control
		if b:
			b.visible = not _is_scenario() and _mp_role != "client" and not _saved_game() # ResetCivsButton: hidden for a saved game

# ---------------------------------------------------------------- slots ---

func _player_count_select() -> RtsoSafeSelect:
	var slot := get_node_or_null("%PlayerCountSlot")
	return slot.get_child(0) as RtsoSafeSelect if slot and slot.get_child_count() > 0 else null

func _render_player_count(n: int) -> void:
	var pc := _player_count_select()
	if pc and pc.get_selected() != n - 1:
		pc.select(clampi(n - 1, 0, 7))

func _slot_count_node() -> SpinBox:
	return $VBox/HBox/Right/SlotsBox/SlotCountRow/SpinSlots as SpinBox

## 0.28 PlayersPanel.xml headings over the player rows, one per grid column (the AI
## gear's has none); Color hides with the colour pickers on a scenario.
const SLOT_HEADINGS := ["Player Name", "Color", "Player Placement", "", "Civilization", "Team"]
func _build_slot_headings() -> void:
	var header: Control = $VBox/HBox/Right/SlotsBox/SlotsHeader
	for h in SLOT_HEADINGS:
		var l := Label.new()
		l.text = tr(h)
		l.clip_text = true
		header.add_child(l)
	# Panels/Buttons/CivInfoButton.js: the info icon at the Civilization heading's end.
	var info := Button.new()
	info.name = "CivInfoButton"
	info.text = "i"
	info.flat = true
	info.add_theme_color_override("font_color", Color(0.95, 0.8, 0.3))
	info.add_theme_color_override("font_hover_color", Color.WHITE)
	info.custom_minimum_size = Vector2(16, 16)
	info.size = Vector2(16, 16)
	# CivInfoButton.js: colorizeHotkey for each of the two keys.
	info.tooltip_text = L10n.sprintf(L10n.t("%(hotkey_civinfo)s / %(hotkey_structree)s: View Civilization Overview / Structure Tree\nLast opened will be reopened on click."),
		{"hotkey_civinfo": _hotkeys.colorize("%(hotkey)s", "civinfo"),
		"hotkey_structree": _hotkeys.colorize("%(hotkey)s", "structree")})
	info.pressed.connect(func(): open_civ_reference(_civ_ref_tab))
	header.add_child(info)
	var grid := _slots_grid()
	grid.sort_children.connect(_place_slot_headings)
	# The columns follow the panel's width: on a new window size they let go first, so
	# the panel can shrink, and are set again from its new width.
	get_viewport().size_changed.connect(func():
		for c in grid.get_children():
			(c as Control).custom_minimum_size.x = 0.0
		_size_slot_columns.call_deferred())
	# PlayerColor.render: each row on its player's colour (alpha 100 of 255).
	grid.draw.connect(func():
		var cells := grid.get_children().filter(func(c): return not c.is_queued_for_deletion())
		for r in range(cells.size() / grid.columns):
			if r >= _slot_rows.size():
				break
			var first: Control = cells[r * grid.columns]
			var last: Control = cells[r * grid.columns + grid.columns - 1]
			var col: Color = PLAYER_COLORS[clampi(int(_slot_rows[r]["color"]), 0, PLAYER_COLORS.size() - 1)]
			col.a = 100.0 / 255.0
			grid.draw_rect(Rect2(Vector2(0.0, first.position.y - 1.0), Vector2(last.position.x + last.size.x, first.size.y + 2.0)), col))

func _place_slot_headings() -> void:
	var header: Control = $VBox/HBox/Right/SlotsBox/SlotsHeader
	var grid := _slots_grid()
	var cells := grid.get_children().filter(func(c): return not c.is_queued_for_deletion())
	var headings := header.get_children().filter(func(n): return n is Label)
	for c in range(mini(headings.size(), grid.columns)):
		var l: Label = headings[c]
		# PlayerColor.js: the Color heading only for the controller, never on a scenario.
		l.visible = c < cells.size() and (c != 1 or (not _is_scenario() and _mp_role != "client"))
		if c < cells.size():
			l.position = Vector2((cells[c] as Control).position.x, 0.0)
			l.size = Vector2((cells[c] as Control).size.x if c < grid.columns - 1 else maxf((cells[c] as Control).size.x, 48.0), header.size.y)
	var info := header.get_node_or_null("CivInfoButton") as Control
	if info and cells.size() > 4:
		var cc: Control = cells[4]
		info.position = Vector2(cc.position.x + cc.size.x - 37.0, 2.0) # 85%-37 .. 85%-21
		var rc := header.get_node_or_null("ResetCivs") as Control
		if rc:
			rc.position = Vector2(cc.position.x + cc.size.x - 16.0, 3.0) # 85%-16 .. 85%; iconReset draws 3 px down
	var rt := header.get_node_or_null("ResetTeams") as Control
	if rt and cells.size() > 5:
		var tc: Control = cells[5]
		rt.position = Vector2(tc.position.x + maxf(tc.size.x, 48.0) - 21.0, 3.0) # 100%-21 .. 100%-5
	grid.queue_redraw()

func _slots_grid() -> GridContainer:
	return $VBox/HBox/Right/SlotsBox/SlotsGrid as GridContainer

func _ensure_slot_rows(n: int) -> void:
	while _slot_rows.size() < n:
		var i := _slot_rows.size()
		_slot_rows.append({
			"kind": 1 if i > 0 else 0,
			"civ": RANDOM_CIV, # 0.28 PlayerCiv._resize: new players start as Random
			"team": 0, # 0 A.D.: no team until you pick one
			# 0 A.D. gui.gamesetup.aidifficulty / aibehavior from the options page.
			"diff": clampi(int(UserSettings.cfg.get_number("gui.gamesetup.aidifficulty")), 0, 5),
			"beh": AI_BEHAVIOR_IDS.get(UserSettings.cfg.get_value("gui.gamesetup.aibehavior"), 0),
			"color": _unused_color(),
		})
	if _slot_rows.size() > n:
		_slot_rows.resize(n)

## PlayerColor.onMapChange: a map's PlayerData Color is that player's colour.
## RTS0's players carry a palette index (the simulation, saves and the network
## share rtso::PLAYER_COLORS), so each takes the nearest palette colour no earlier
## map player took; a player the map gives no colour keeps its own unless a map
## colour took it, then it takes an unused one.
func apply_map_colors(map_players: Array) -> void:
	var taken := {}
	for i in range(mini(map_players.size(), _slot_rows.size())):
		var rgb: Variant = map_players[i].get("color") if typeof(map_players[i]) == TYPE_DICTIONARY else null
		if typeof(rgb) != TYPE_ARRAY or rgb.size() < 3:
			continue
		var idx := nearest_color(Color8(int(rgb[0]), int(rgb[1]), int(rgb[2])), taken)
		taken[idx] = true
		_slot_rows[i]["color"] = idx
	if taken.is_empty():
		return
	for i in range(_slot_rows.size()):
		var rgb: Variant = map_players[i].get("color") if i < map_players.size() and typeof(map_players[i]) == TYPE_DICTIONARY else null
		if typeof(rgb) == TYPE_ARRAY:
			continue
		if taken.has(int(_slot_rows[i]["color"])):
			_slot_rows[i]["color"] = -1
			_slot_rows[i]["color"] = _unused_color()
		taken[int(_slot_rows[i]["color"])] = true

## The palette entry closest to col (squared RGB distance), skipping those in taken.
static func nearest_color(col: Color, taken: Dictionary = {}) -> int:
	var best := -1
	var best_d := INF
	for c in range(PLAYER_COLORS.size()):
		if taken.has(c):
			continue
		var p: Color = PLAYER_COLORS[c]
		var d := (p.r - col.r) ** 2 + (p.g - col.g) ** 2 + (p.b - col.b) ** 2
		if d < best_d:
			best_d = d
			best = c
	return maxi(best, 0)

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
	_ai_buttons.clear()
	var n := int(_slot_count_node().value)
	_ensure_slot_rows(n)
	for i in range(n):
		_add_slot_row(grid, i)
	_rebuilding_slots = false
	_size_slot_columns()
	_apply_mp_locks()
	_render_speeds()
	_render_pop_tips()

## PlayersPanel.xml's columns over the panel's width W, 5 px apart: name 0-22%, colour
## 28 px, assignment to 50%+35, the 28 px AI gear, civ to 85%, team to 100%-5.
func _size_slot_columns() -> void:
	var grid := _slots_grid()
	var w: float = (get_node("VBox/HBox/Right/SlotsBox") as Control).size.x
	if w <= 0.0:
		return
	grid.add_theme_constant_override("h_separation", 5)
	var widths := [w * 0.22, 28.0, w * 0.28 - 3.0, 28.0, w * 0.35 - 73.0, w * 0.15 - 10.0]
	var cells := grid.get_children().filter(func(c): return not c.is_queued_for_deletion())
	for k in cells.size():
		var c: Control = cells[k]
		if c is Label:
			(c as Label).clip_text = true
		c.custom_minimum_size.x = maxf(floorf(widths[k % grid.columns]), 0.0)

## GameSpeed.render: the Fast Forward speeds only while nobody holds a player
## slot (every slot AI or unassigned); a speed no longer listed goes back to Normal.
func _render_speeds() -> void:
	var spd: RtsoSafeSelect = %SpeedSlot.get_child(0) if %SpeedSlot.get_child_count() else null
	if spd == null:
		return
	var n := int(_slot_count_node().value)
	var ff := true
	for i in mini(n, _slot_rows.size()):
		if int(_slot_rows[i]["kind"]) == 0 and (_mp_role != "none" or i == _local_slot):
			ff = false
	var listed := 0
	for sp in SPEEDS:
		if ff or not sp[2]:
			listed += 1
	if spd.get_item_count() == listed:
		return
	var current: int = spd.get_item_id(spd.get_selected()) if spd.get_selected() >= 0 else DEFAULT_SPEED
	spd.clear()
	for i in SPEEDS.size():
		if ff or not SPEEDS[i][2]:
			spd.add_item(SPEEDS[i][1], i)
	spd.select(spd.get_item_index(current) if spd.get_item_index(current) >= 0 else spd.get_item_index(DEFAULT_SPEED))

func _add_slot_row(grid: GridContainer, i: int) -> void:
	var row: Dictionary = _slot_rows[i]
	# PlayersPanel.xml's columns: name, colour, assignment (Player Placement), AI gear, civ, team.
	var cols := []
	cols.resize(6)
	var name := Label.new()
	name.text = _slot_label(i, row)
	if _mp_role != "none":
		name.add_theme_color_override("font_color", _ready_color(i))
	cols[0] = name
	# kind
	var kind_slot := Control.new()
	kind_slot.custom_minimum_size = Vector2(92, 28)
	cols[2] = kind_slot
	if _mp_role != "none":
		kind_slot.queue_free()
		cols[2] = _assignment_cell(i)
	else:
		var kind := _make_option(kind_slot)
		# 0.28 PlayerAssignment: you, "AI: Petra Bot", "Unassigned" or "Removed" (not on slot 1,
		# since a removed slot drops out and the later players move up).
		kind.add_item(singleplayer_name(), KIND_YOU)
		kind.add_item(tr("AI: %(ai)s").replace("%(ai)s", tr("Petra Bot")), 1)
		kind.add_item(tr("Unassigned"), 0)
		if i > 0:
			kind.add_item(tr("Removed"), 2)
		_color_assignment_items(kind, [])
		var k := KIND_YOU if i == _local_slot and int(row["kind"]) == 0 else int(row["kind"])
		kind.select(maxi(kind.get_item_index(k), 0))
		kind.tooltip_text = tr("Select player.")
		kind.item_selected.connect(func(ix: int): _on_slot_assign(kind.get_item_id(ix), i))
	# civ
	var civ_slot := Control.new()
	civ_slot.custom_minimum_size = Vector2(130, 28)
	cols[4] = civ_slot
	var civ := _make_option(civ_slot)
	# 0.28 PlayerCiv.getItems: Random first, then the civs by name.
	civ.add_item("Random")
	civ.set_item_tooltip(0, tr("Picks one civilization at random when the game starts."))
	civ.set_item_color(0, RANDOM_ITEM_COLOR)
	for ci in _civ_order():
		var c: Dictionary = CIVS[ci]
		civ.add_item(c["name"]) # PlayerCiv.js: names only
		civ.set_item_tooltip(civ.get_item_count() - 1, RefCommon.civ_history(int(c["id"]))) # PlayerCiv: g_CivData History
	var civ_pos := _civ_pos(int(row["civ"]))
	civ.select(_civ_order().find(civ_pos) + 1 if civ_pos >= 0 else 0)
	civ.disabled = _map_civ(_map_player(i)) >= 0
	civ.tooltip_text = tr("Choose the civilization for this player.")
	civ.item_selected.connect(_on_slot_civ.bind(i))
	# team
	var team_slot := Control.new()
	team_slot.custom_minimum_size = Vector2(64, 28)
	cols[5] = team_slot
	var team := _make_option(team_slot)
	team.add_item(tr("None"))
	for t in range(1, 5):
		team.add_item(str(t))
	team.tooltip_text = tr("Select player's team.")
	team.select(clampi(int(row["team"]), 0, 4))
	team.disabled = _is_scenario()
	team.item_selected.connect(_on_slot_team.bind(i))
	# 0.28 PerPlayer/AIConfigButton.js: a gear on AI rows opens the AI Configuration dialog.
	var ai_slot := Control.new()
	ai_slot.custom_minimum_size = Vector2(32, 28)
	cols[3] = ai_slot
	var gear := Button.new()
	gear.name = "AIConfigButton"
	gear.custom_minimum_size = Vector2(28, 28)
	gear.draw.connect(func(): _draw_gear(gear))
	gear.pressed.connect(func(): _open_ai_config(i))
	ai_slot.add_child(gear)
	while _ai_buttons.size() <= i:
		_ai_buttons.append(null)
	_ai_buttons[i] = gear
	_render_ai_button(i)
	# player colour (0 A.D. PlayerColor: picking a colour in use swaps the two players' colours)
	var col_slot := Control.new()
	col_slot.custom_minimum_size = Vector2(56, 28)
	cols[1] = col_slot
	var col := _make_option(col_slot)
	for c in range(PLAYER_COLORS.size()):
		col.add_icon_item(_color_icon(c), "")
	col.select(clampi(int(row["color"]), 0, PLAYER_COLORS.size() - 1))
	col.tooltip_text = tr("Pick a color.")
	col.item_selected.connect(_on_slot_color.bind(i))
	# PlayerColor.js hides the colours on a scenario and from clients (kept in
	# place for the grid's columns); the row keeps its player's colour.
	var hide_col := _is_scenario() or _mp_role == "client"
	col.disabled = hide_col
	col_slot.modulate.a = 0.0 if hide_col else 1.0
	for c in cols:
		grid.add_child(c)

## PlayerAssignment.onSelectionChange (single player). Picking yourself moves you here:
## a slot that was an AI hands its AI to the slot you left (else that one becomes
## Unassigned), and civ and colour swap too, except on a scenario (Client item).
func _on_slot_assign(id: int, slot: int) -> void:
	if _rebuilding_slots or slot < 0 or slot >= _slot_rows.size():
		return
	var row: Dictionary = _slot_rows[slot]
	if id != KIND_YOU:
		if slot == _local_slot:
			_local_slot = -1 # Unassigned, AI or Removed on your own slot: you watch
		_on_slot_kind(id, slot)
		refresh_slots.call_deferred()
		return
	var src := _local_slot
	if src >= 0 and src != slot and src < _slot_rows.size():
		var old: Dictionary = _slot_rows[src]
		old["kind"] = 1 if int(row["kind"]) == 1 else 0
		for key in (["diff", "beh"] + ([] if _is_scenario() else ["civ", "color"])):
			var t = old[key]
			old[key] = row[key]
			row[key] = t
	row["kind"] = 0
	_local_slot = slot
	_render_speeds()
	_update_preview()
	refresh_slots.call_deferred()

func _on_slot_kind(idx: int, slot: int) -> void:
	if _rebuilding_slots or slot < 0 or slot >= _slot_rows.size():
		return
	_slot_rows[slot]["kind"] = idx
	if slot == _local_slot and idx != 0:
		_local_slot = -1 # PlayerAssignment: you watch once your slot is given away
	_render_speeds()
	if slot == 0:
		refresh_slots.call_deferred() # slot 1's row follows its kind
	_render_ai_button(slot)
	_update_preview()

## Host: the client of slot `k` in the sent setup becomes unassigned (an observer).
func _unassign_net_slot(k: int) -> void:
	if _mp_role != "host" or k < 0:
		return
	var slots: Array = _net_state.get("slots", [])
	var holder := int(slots[k].get("client", -1)) if k < slots.size() else -1
	if holder >= 0:
		net_main.assign_net_client(holder, -1)

## Host: PlayerAssignmentsController.unassignInvalidPlayers, a lower player count
## leaves the clients of the dropped slots unassigned.
func _unassign_net_slots_from(count: int) -> void:
	var k := 0
	for i in _slot_rows.size():
		if i > 0 and int(_slot_rows[i]["kind"]) == 2:
			continue
		if i >= count:
			_unassign_net_slot(k)
		k += 1

## The AI dialog's AI Player: Petra makes the slot an AI, None an unassigned one.
func _set_slot_kind(slot: int, kind: int) -> void:
	if slot < 0 or slot >= _slot_rows.size():
		return
	_on_slot_kind(kind, slot)
	refresh_slots.call_deferred()

## AIConfigButton.render: shown on AI rows, with "Configure AI: %(description)s."
func _render_ai_button(slot: int) -> void:
	if slot < 0 or slot >= _ai_buttons.size() or _ai_buttons[slot] == null or not is_instance_valid(_ai_buttons[slot]):
		return
	var gear: Button = _ai_buttons[slot]
	var row: Dictionary = _slot_rows[slot]
	var ai := int(row["kind"]) == 1
	# Kept in the grid when hidden so the columns stay in line.
	gear.modulate.a = 1.0 if ai else 0.0
	gear.mouse_filter = Control.MOUSE_FILTER_STOP if ai else Control.MOUSE_FILTER_IGNORE
	gear.tooltip_text = L10n.sprintf(L10n.t("Configure AI: %(description)s."),
		{"description": AIConfig.describe(int(row["diff"]), int(row["beh"]), DIFFS.map(func(d): return tr(d)), BEHAVIORS.map(func(b): return tr(b)))}) if ai else ""

## 0.28's ModernGear sprite, drawn: a cog of eight teeth with a hole.
static func _draw_gear(b: Button) -> void:
	var c := b.size / 2.0
	var r := minf(b.size.x, b.size.y) * 0.3
	var col := Color(0.93, 0.85, 0.6) if not b.is_hovered() else Color(1, 0.95, 0.75)
	if b.disabled:
		col = col.darkened(0.45)
	for k in 8:
		var a := k * TAU / 8.0
		var d := Vector2.from_angle(a)
		var n := d.orthogonal() * r * 0.28
		b.draw_colored_polygon(PackedVector2Array([c + d * r * 0.8 - n, c + d * r * 1.35 - n, c + d * r * 1.35 + n, c + d * r * 0.8 + n]), col)
	b.draw_circle(c, r, col)
	b.draw_circle(c, r * 0.42, Color(0.1, 0.09, 0.08))

## AIConfigPage.openPage(playerIndex, enabled): only the controller edits.
func _open_ai_config(slot: int) -> void:
	if get_node_or_null("AIConfigLayer") != null:
		return
	var dlg = AIConfig.open_overlay(self, slot, _mp_role != "client" and not _saved_game())
	dlg.get_parent().name = "AIConfigLayer"

func _on_slot_civ(idx: int, slot: int) -> void:
	if _rebuilding_slots or slot < 0 or slot >= _slot_rows.size():
		return
	var order := _civ_order()
	_slot_rows[slot]["civ"] = int(CIVS[order[idx - 1]]["id"]) if idx >= 1 and idx <= order.size() else RANDOM_CIV
	_net_request("civ", int(_slot_rows[slot]["civ"]))
	_update_preview()

var _civ_order_cache: Array = []

## CIVS indices sorted by name, the order of the civ dropdowns after "Random".
func _civ_order() -> Array:
	if _civ_order_cache.size() != CIVS.size():
		_civ_order_cache = range(CIVS.size())
		_civ_order_cache.sort_custom(func(a, b): return str(CIVS[a]["name"]).naturalnocasecmp_to(str(CIVS[b]["name"])) < 0)
	return _civ_order_cache

func _on_slot_team(idx: int, slot: int) -> void:
	if _rebuilding_slots or slot < 0 or slot >= _slot_rows.size():
		return
	_slot_rows[slot]["team"] = idx
	_net_request("team", idx)
	_render_pop_tips()

func _on_slot_diff(idx: int, slot: int) -> void:
	if _rebuilding_slots or slot < 0 or slot >= _slot_rows.size():
		return
	_slot_rows[slot]["diff"] = idx

func _on_slot_color(idx: int, slot: int) -> void:
	if _rebuilding_slots or slot < 0 or slot >= _slot_rows.size():
		return
	if _mp_role == "client":
		# The host swaps nothing for a client: it takes a colour no one uses.
		_net_request("color", idx)
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
	# 0 A.D. PlayerCount: random maps take any count; skirmishes and scenarios
	# have the map's.
	var m := _selected_map()
	if m.is_empty() or str(m.get("type", "")) == "random":
		return
	var want := int(m["players"])
	var spin := _slot_count_node()
	if int(spin.value) != want:
		spin.value = want
		refresh_slots()

# ---------------------------------------------------------------- options ---

func _build_options() -> void:
	for k in VICTORY_KINDS:
		var cb := CheckBox.new()
		cb.text = k[1]
		cb.tooltip_text = k[2]
		cb.toggled.connect(_on_victory_toggled.bind(int(k[0])))
		%VictorySlot.add_child(cb)
		_victory_checks[int(k[0])] = cb
	_set_victory_mask(1)
	%LockTeams.toggled.connect(func(_on): _render_mode_checks())
	var msz := _make_option(%MapSizeSlot)
	for s in MAP_SIZES:
		msz.add_item(tr(s[1]), s[0])
		msz.set_item_tooltip(msz.get_item_count() - 1, tr(MAP_SIZE_TIPS[msz.get_item_count() - 1]))
	msz.select(DEFAULT_MAP_SIZE)
	msz.tooltip_text = tr("Select map size. (Larger sizes may reduce performance.)")
	var popt := _make_option(%PopCapTypeSlot)
	for t in POP_CAP_TYPES:
		popt.add_item(tr(t[1]))
		popt.set_item_tooltip(popt.get_item_count() - 1, tr(t[2])) # PopulationCapType: Tooltip
	popt.select(POP_TYPE_PLAYER)
	popt.tooltip_text = tr("Select a population capacity type.")
	popt.item_selected.connect(_on_pop_type_selected)
	_make_option(%PopCapSlot)
	_select_pop(300, false) # 0 A.D. default: 300 per player
	var cf := _make_slider(%CeasefireSlot)
	for c in CEASEFIRES:
		cf.add_item(tr("No ceasefire") if c == 0 else _minutes(c))
	cf.select(0)
	cf.tooltip_text = tr("Set time where no attacks are possible.")
	var wt := _make_slider(%WonderTimeSlot)
	var rt := _make_slider(%RelicTimeSlot)
	for t in VICTORY_TIMES:
		wt.add_item(tr("Immediate Victory.") if t == 0 else _minutes(t))
		rt.add_item(tr("Immediate Victory.") if t == 0 else _minutes(t))
	wt.select(VICTORY_TIMES.find(20))
	rt.select(VICTORY_TIMES.find(20))
	wt.tooltip_text = tr("Minutes until the player has achieved Wonder Victory")
	rt.tooltip_text = tr("Minutes until the player has achieved Relic Victory.")
	var rc := _make_slider(%RelicCountSlot)
	for c in RELIC_COUNTS:
		rc.add_item(L10n.sprintf(tr("%(number)s relic") if c == 1 else tr("%(number)s relics"), {"number": c}))
	rc.select(RELIC_COUNTS.find(2))
	rc.tooltip_text = tr("Total number of relics spawned on the map. Relic victory is most realistic with only one or two relics. With greater numbers, the relics are important to capture to receive aura bonuses.")
	var wl := _make_option(%WaterLevelSlot)
	for w in WATER_LEVELS:
		wl.add_item(tr(w[0]))
		wl.set_item_tooltip(wl.get_item_count() - 1, tr(w[1]))
	wl.select(0)
	wl.tooltip_text = tr("Select the water level of this map.")
	var sr := _make_slider(%SeaRiseSlot)
	for t in SEA_RISE_TIMES:
		sr.add_item(_minutes(t))
	sr.select(SEA_RISE_TIMES.find(25))
	sr.tooltip_text = tr("Set the time when the water will start to rise.")
	var tpl := _make_option(%TeamPlacementSlot)
	tpl.item_selected.connect(_on_team_placement_selected)
	_rebuild_team_placement([])
	var lsc := _make_option(%LandscapeSlot)
	for l in LANDSCAPES:
		lsc.add_item(tr(l[0]))
		lsc.set_item_tooltip(lsc.get_item_count() - 1, tr(l[2]))
		if int(l[1]) <= 2: # Random and the group headers (Landscape.js randomItems)
			lsc.set_item_color(lsc.get_item_count() - 1, RANDOM_ITEM_COLOR)
	%LandscapeLabel.tooltip_text = tr("Select one of the landscapes of this map.")
	lsc.select(0)
	var tdf := _make_option(%TriggerDifficultySlot)
	for d in TRIGGER_DIFFICULTIES:
		tdf.add_item(tr(d[0]))
		tdf.set_item_tooltip(tdf.get_item_count() - 1, tr(d[1]))
	%TriggerDifficultyLabel.tooltip_text = tr("Select the difficulty of this scenario.")
	tdf.select(2)
	var dsel := _make_option(%DaytimeSlot)
	dsel.add_item("Random")
	dsel.set_item_color(0, RANDOM_ITEM_COLOR)
	dsel.select(0)
	var res := _make_option(%ResSlot)
	for r in START_RES:
		res.add_item(tr(r.get_slice(" (", 0))) # starting_resources.json Title
		# StartingResources.HoverTooltip
		res.set_item_tooltip(res.get_item_count() - 1, tr("Initial amount of each resource: %(resources)s.").replace("%(resources)s", str(START_RES_AMOUNTS[res.get_item_count() - 1])))
	res.select(1)
	res.tooltip_text = tr("Select the game's starting resources.")
	# GameSettingControl.setTooltip: the title carries the setting's tooltip too.
	for pair in [["MapSizeLabel", "MapSizeSlot"], ["PopCapTypeLabel", "PopCapTypeSlot"], ["ResLabel", "ResSlot"], ["CeasefireLabel", "CeasefireSlot"]]:
		get_node("%" + pair[0]).tooltip_text = get_node("%" + pair[1]).get_child(0).tooltip_text
	var bio := _make_option(%BiomeSlot)
	bio.add_item(BIOMES[0], 0)
	bio.select(0)
	bio.item_selected.connect(_on_biome_selected)
	_make_option(%SpeedSlot)
	_render_speeds()
	_mute_spin(%SpinSeed)
	_mute_spin($VBox/HBox/Right/SlotsBox/SlotCountRow/SpinSlots)
	$VBox/HBox/Right/SlotsBox/SlotCountRow/SpinSlots.value_changed.connect(
		func(v: float) -> void:
			_unassign_net_slots_from(int(v))
			_render_player_count(int(v))
			refresh_slots())
	# 0.28 Dropdowns/PlayerCount.js: "Number of Players", 1 to g_MaxPlayers, in the Player tab
	# (the hidden SpinSlots keeps the count).
	var pc := _make_option(%PlayerCountSlot)
	for i in range(1, 9):
		pc.add_item(str(i), i)
	pc.tooltip_text = tr("Select number of players.")
	_render_player_count(int(_slot_count_node().value))
	pc.item_selected.connect(func(i: int) -> void: _slot_count_node().value = pc.get_item_id(i))
	%BtnReroll.pressed.connect(_on_reroll_seed)
	%SpinSeed.value_changed.connect(
		func(_v: float) -> void: _update_preview())

func _mute_spin(sb: SpinBox) -> void:
	if sb:
		MatchSettings.prepare_line_edit(sb.get_line_edit())
		MatchSettings.strip_engine_popups(sb)

func _on_reroll_seed() -> void:
	%SpinSeed.value = randi() % 1000000000
	_update_preview()

## The checked victory conditions as a VictoryKind mask.
func _victory_mask() -> int:
	var mask := 0
	for bit in _victory_checks:
		if _victory_checks[bit].button_pressed:
			mask |= int(bit)
	return mask

## Check exactly `mask`, then disable what the checked ones exclude (VictoryConditions.js).
func _set_victory_mask(mask: int) -> void:
	for bit in _victory_checks:
		_victory_checks[bit].set_pressed_no_signal((mask & int(bit)) != 0)
	var disabled := 0
	for bit in VICTORY_EXCLUDES:
		if mask & int(bit):
			disabled |= int(VICTORY_EXCLUDES[bit])
	for bit in _victory_checks:
		_victory_checks[bit].disabled = (disabled & int(bit)) != 0
	_refresh_victory_times(mask)
	_render_mode_checks()

## 0.28 Checkboxes: Last Man Standing is always shown but needs unlocked teams (its
## setting's "available") and no scenario; Nomad shows only on random maps; Hero
## Garrison only while Regicide is checked, and never on a scenario.
func _render_mode_checks() -> void:
	var scen := _is_scenario()
	%LastManStanding.disabled = scen or %LockTeams.button_pressed
	%Nomad.visible = str(_selected_map().get("kind", "")) == "Random"
	%RegicideGarrison.visible = not scen and (_victory_mask() & VK_REGICIDE) != 0

## 0.28 WonderDuration / RelicCount / RelicDuration: shown only while their condition
## is checked, and fixed (disabled) on a scenario.
func _refresh_victory_times(mask: int) -> void:
	# GameSettingsLayout.js: Relic Count, Relic Duration, Hero Garrison, Wonder Duration.
	var wonder := (mask & 16) != 0
	var relic := (mask & 32) != 0
	%VictoryTimesRow.visible = relic
	%WonderRow.visible = wonder
	var scen := _is_scenario()
	for n in ["WonderTimeSlot", "RelicCountSlot", "RelicTimeSlot"]:
		var slot: Node = get_node("%" + n)
		if slot.get_child_count() > 0:
			slot.get_child(0).disabled = scen

func _on_victory_toggled(on: bool, bit: int) -> void:
	var mask := _victory_mask()
	if on:
		mask &= ~int(VICTORY_EXCLUDES.get(bit, 0))
	_set_victory_mask(mask)

## The MatchSettings fields game setup restores, also the keys of the persisted file.
const PERSISTED_KEYS := ["map_id", "seed", "victory", "waves_enabled", "last_man_standing", "nomad",
	"regicide_garrison", "lock_teams", "disable_spies", "disable_treasures", "pop_cap", "team_pop",
	"ceasefire_min", "reveal_map", "explore_map", "ally_view", "wonder_min", "relic_min", "relic_count", "water_level",
	"sea_level_rise_min", "team_placement", "landscape", "trigger_difficulty", "map_daytime", "map_size", "start_res", "biome", "game_speed",
	"slot_types", "slot_civs", "slot_teams", "slot_diffs", "slot_behs", "slot_colors", "local_slot", "map_filter"]
## 0 A.D. PersistentMatchSettings.PersistedSettingsFileSingleplayer.
const PERSIST_FILE := "user://config/matchsettings.json"
## PersistentMatchSettings(isNetworked): a networked game's controller keeps its own file.
const PERSIST_FILE_MP := "user://config/matchsettings.mp.json"
const PERSIST_KEY := "persistmatchsettings"
const PERSIST_VERSION := 1

## 0 A.D. PersistentMatchSettings.loadFile: the settings saved on the last Start
## or Back while "Persist match settings" is on, else {}.
static func load_persisted(networked := false) -> Dictionary:
	var path := PERSIST_FILE_MP if networked else PERSIST_FILE
	if not RtsoUserConfig.new().get_bool(PERSIST_KEY) or not FileAccess.file_exists(path):
		return {}
	var f := FileAccess.open(path, FileAccess.READ)
	var data: Variant = JSON.parse_string(f.get_as_text()) if f else null
	if typeof(data) != TYPE_DICTIONARY or int(data.get("version", 0)) != PERSIST_VERSION:
		return {}
	var a: Variant = data.get("attributes", {})
	return a if typeof(a) == TYPE_DICTIONARY else {}

## PersistentMatchSettings.saveFile: written whether or not the option is on,
## empty when it is off, so turning it back on doesn't bring back old settings.
## GameSettingsController.savePersistentMatchSettings: only the controller,
## never for a saved game; a networked one to matchsettings.mp.json.
func save_persisted() -> void:
	if _mp_role == "client" or (_mp_role == "host" and _saved_game()):
		return
	var on := RtsoUserConfig.new().get_bool(PERSIST_KEY)
	var path := PERSIST_FILE_MP if _mp_role == "host" else PERSIST_FILE
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify({"version": PERSIST_VERSION, "attributes": page_state() if on else {}}, "\t"))

## The page as the player left it, before Random civs are picked and Closed
## slots dropped (0 A.D. persists the settings before they are resolved).
func page_state() -> Dictionary:
	var st := _collect_setup()
	var n := int($VBox/HBox/Right/SlotsBox/SlotCountRow/SpinSlots.value)
	var rows: Array = _slot_rows.slice(0, n)
	return {"map_id": str(st["map"].get("id", "")), "seed": st["seed"], "victory": st["victory"],
		"waves_enabled": st["waves"], "last_man_standing": st["lms"],
		"nomad": %Nomad.button_pressed,
		"regicide_garrison": %RegicideGarrison.button_pressed,
		"lock_teams": st["lock_teams"], "disable_spies": st["disable_spies"], "disable_treasures": st["disable_treasures"],
		"pop_cap": st["pop_cap"], "team_pop": st["team_pop"], "ceasefire_min": st["ceasefire"], "reveal_map": st["reveal"], "explore_map": st["explore"],
		"ally_view": bool(st.get("ally_view", false)),
		"wonder_min": st["wonder_min"], "relic_min": st["relic_min"], "relic_count": st["relic_count"],
		"water_level": st["water_level"], "sea_level_rise_min": st["sea_level_rise_min"],
		"team_placement": st["team_placement"], "landscape": st["landscape"],
		"trigger_difficulty": st["trigger_difficulty"], "map_daytime": st["map_daytime"], "map_size": st["map_size"],
		"start_res": st["res"], "biome": st["biome"], "game_speed": st["speed"],
		"slot_types": rows.map(func(r): return int(r["kind"])), "slot_civs": rows.map(func(r): return int(r["civ"])),
		"slot_teams": rows.map(func(r): return int(r["team"])), "slot_diffs": rows.map(func(r): return int(r["diff"])),
		"slot_behs": rows.map(func(r): return int(r["beh"])), "slot_colors": rows.map(func(r): return int(r["color"])),
		"local_slot": _local_slot,
		# GuiData.js: the Map Filter travels with the settings and is persisted with them.
		"map_filter": _map_filter_name}

## The settings to show: the persisted file when there is one, else what this
## run last started with (MatchSettings).
func _restore_settings() -> void:
	if not has_node("/root/MatchSettings"):
		return
	var m := get_node("/root/MatchSettings")
	var src := {}
	for k in PERSISTED_KEYS:
		src[k] = m.get(k)
	# GameSettingsController.onLoad: the controller's own persisted settings
	# (a networked host's from matchsettings.mp.json, none for a saved game);
	# a client's setup is the host's MatchSettings.
	var saved := {}
	# 0.28 LockedTeams.onLoad: every new setup opens with DefaultValue =
	# Engine.HasNetClient(), whatever was persisted ("in multiplayer mode,
	# players negotiate teams before starting the match"); single player keeps
	# open diplomacy. A client takes the host's value.
	var lock_default := -1
	if net_main == null:
		saved = load_persisted()
		lock_default = 0
	elif net_main.has_method("get_net_setup_state"):
		var st: Dictionary = net_main.get_net_setup_state()
		if bool(st.get("controller", false)) and not bool(st.get("saved_game", false)):
			saved = load_persisted(true)
			saved.erase("local_slot")
			lock_default = 1
	for k in saved:
		if src.has(k) and typeof(saved[k]) in [TYPE_FLOAT, TYPE_INT, TYPE_BOOL, TYPE_STRING, TYPE_ARRAY]:
			src[k] = saved[k]
	if lock_default >= 0:
		src["lock_teams"] = lock_default == 1
	_apply_settings(src)

func _apply_settings(ms: Dictionary) -> void:
	var res: RtsoSafeSelect = %ResSlot.get_child(0)
	var spd: RtsoSafeSelect = %SpeedSlot.get_child(0)
	_set_victory_mask(victory_mask(int(ms.get("victory"))))
	%Waves.button_pressed = bool(ms.get("waves_enabled"))
	%LastManStanding.button_pressed = bool(ms.get("last_man_standing"))
	%Nomad.button_pressed = bool(ms.get("nomad"))
	%RegicideGarrison.button_pressed = bool(ms.get("regicide_garrison"))
	%LockTeams.button_pressed = bool(ms.get("lock_teams"))
	%DisableSpies.button_pressed = bool(ms.get("disable_spies"))
	%DisableTreasures.button_pressed = bool(ms.get("disable_treasures"))
	_select_pop(int(ms.get("pop_cap")), bool(ms.get("team_pop")))
	var cf: SettingSlider = %CeasefireSlot.get_child(0)
	cf.select(maxi(CEASEFIRES.find(int(ms.get("ceasefire_min"))), 0))
	%RevealMap.button_pressed = bool(ms.get("reveal_map"))
	%ExploreMap.button_pressed = bool(ms.get("explore_map"))
	%AlliedView.button_pressed = bool(ms.get("ally_view"))
	var wt: SettingSlider = %WonderTimeSlot.get_child(0)
	var rt: SettingSlider = %RelicTimeSlot.get_child(0)
	var rc: SettingSlider = %RelicCountSlot.get_child(0)
	var wi := VICTORY_TIMES.find(int(ms.get("wonder_min")))
	var ri := VICTORY_TIMES.find(int(ms.get("relic_min")))
	var ci := RELIC_COUNTS.find(int(ms.get("relic_count")))
	wt.select(wi if wi >= 0 else VICTORY_TIMES.find(20))
	rt.select(ri if ri >= 0 else VICTORY_TIMES.find(20))
	rc.select(ci if ci >= 0 else RELIC_COUNTS.find(2))
	var wl: RtsoSafeSelect = %WaterLevelSlot.get_child(0)
	wl.select(clampi(int(ms.get("water_level")), 0, WATER_LEVELS.size() - 1))
	var sr: SettingSlider = %SeaRiseSlot.get_child(0)
	var si := SEA_RISE_TIMES.find(int(ms.get("sea_level_rise_min")))
	sr.select(si if si >= 0 else SEA_RISE_TIMES.find(25))
	_team_placement = clampi(int(ms.get("team_placement")), 0, TEAM_PLACEMENTS.size() - 1)
	_rebuild_team_placement(_selected_map().get("placements", []))
	var lsc: RtsoSafeSelect = %LandscapeSlot.get_child(0)
	lsc.select(0)
	for i in range(LANDSCAPES.size()):
		if int(LANDSCAPES[i][1]) == int(ms.get("landscape")):
			lsc.select(i)
	var tdf: RtsoSafeSelect = %TriggerDifficultySlot.get_child(0)
	tdf.select(clampi(int(ms.get("trigger_difficulty", 3)) - 1, 0, TRIGGER_DIFFICULTIES.size() - 1))
	var dsel: RtsoSafeSelect = %DaytimeSlot.get_child(0)
	if dsel.get_item_count() < 3:
		for i in range(dsel.get_item_count(), 3):
			dsel.add_item("Option %d" % i)
	dsel.select(clampi(int(ms.get("map_daytime", 0)), 0, 2))
	var msz: RtsoSafeSelect = %MapSizeSlot.get_child(0)
	for i in range(MAP_SIZES.size()):
		if int(MAP_SIZES[i][0]) == int(ms.get("map_size")):
			msz.select(i)
	res.select(clampi(int(ms.get("start_res")), 0, START_RES.size() - 1))
	_biome = int(ms.get("biome"))
	if _biome != RANDOM_BIOME:
		_biome = clampi(_biome, 0, BIOMES.size() - 1)
	_rebuild_biomes(_selected_map().get("biomes", []))
	for i in range(SPEEDS.size()):
		if is_equal_approx(float(SPEEDS[i][0]), float(ms.get("game_speed"))) and spd.get_item_index(i) >= 0:
			spd.select(spd.get_item_index(i))
	%SpinSeed.value = int(ms.get("seed"))
	# Restore map selection by id, with its map type.
	var filter := str(ms.get("map_filter")) if ms.get("map_filter") != null else ""
	if filter != "" and MAP_FILTERS.any(func(f): return f[0] == filter):
		_map_filter_name = filter
	_select_map_id(str(ms.get("map_id")))
	# Restore slots.
	var types: Array = ms.get("slot_types", [])
	var civs: Array = ms.get("slot_civs", [])
	var teams: Array = ms.get("slot_teams", [])
	var sdiffs: Array = ms.get("slot_diffs", [])
	var sbehs: Array = ms.get("slot_behs", [])
	var scolors: Array = ms.get("slot_colors", [])
	if not types.is_empty():
		$VBox/HBox/Right/SlotsBox/SlotCountRow/SpinSlots.value = clampi(types.size(), 1, 8)
		_ensure_slot_rows(int($VBox/HBox/Right/SlotsBox/SlotCountRow/SpinSlots.value))
		for i in range(mini(types.size(), _slot_rows.size())):
			_slot_rows[i]["kind"] = clampi(int(types[i]), 0, 2)
			if i < civs.size():
				_slot_rows[i]["civ"] = int(civs[i]) if _civ_pos(int(civs[i])) >= 0 else RANDOM_CIV
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
		# local_slot -2 (older setups): you sit in slot 1 when it is Human.
		var ls := int(ms.get("local_slot", -2))
		_local_slot = ls if ls >= -1 and ls < _slot_rows.size() else (0 if int(_slot_rows[0]["kind"]) == 0 else -1)
		refresh_slots()

# ---------------------------------------------------------------- start ---

func _on_back_pressed() -> void:
	if _mp_role != "none":
		save_persisted() # GameSettingsController.onClose (the host's)
		# 0 A.D. gamesetup's Back in a networked game: to the lobby while signed in, else the main menu.
		net_main.leave_net_game()
		return
	save_persisted() # GameSettingsController.onClose
	MatchSettings.go("res://scenes/menu/main_menu.tscn")

func _collect_setup() -> Dictionary:
	var m := _selected_map()
	var seed := int(%SpinSeed.value)
	var vmask := _victory_mask()
	var res: RtsoSafeSelect = %ResSlot.get_child(0)
	var spd: RtsoSafeSelect = %SpeedSlot.get_child(0)
	var msz: RtsoSafeSelect = %MapSizeSlot.get_child(0)
	var pop: RtsoSafeSelect = %PopCapSlot.get_child(0)
	var cf: SettingSlider = %CeasefireSlot.get_child(0)
	var wt: SettingSlider = %WonderTimeSlot.get_child(0)
	var rt: SettingSlider = %RelicTimeSlot.get_child(0)
	var rc: SettingSlider = %RelicCountSlot.get_child(0)
	var wl: RtsoSafeSelect = %WaterLevelSlot.get_child(0)
	var sr: SettingSlider = %SeaRiseSlot.get_child(0)
	var lsc: RtsoSafeSelect = %LandscapeSlot.get_child(0)
	var tdf: RtsoSafeSelect = %TriggerDifficultySlot.get_child(0)
	var dsel: RtsoSafeSelect = %DaytimeSlot.get_child(0)
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
	var local := -1 # the setup slot you play (local_slot), -1 to watch
	for i in range(n):
		var k := int(_slot_rows[i]["kind"])
		if i == 0:
			k = mini(k, 1) # Human or AI, never closed
		if k == 2:
			continue # closed: 0 A.D. drops the slot, so later players move up
		if k == 0 and i == _local_slot:
			local = types.size()
		types.append(k)
		var c := int(_slot_rows[i]["civ"])
		if _civ_pos(c) < 0: # "Random": 0 A.D. picks one when the match starts
			c = int(CIVS[randi() % CIVS.size()]["id"])
		civs.append(c)
		teams.append(int(_slot_rows[i]["team"]))
		diffs.append(int(_slot_rows[i]["diff"]))
		behs.append(int(_slot_rows[i]["beh"]))
		colors.append(int(_slot_rows[i]["color"]))
		if k == 0:
			humans += 1
		elif k == 1:
			ai_total += 1
	var names := _player_names(types, civs, local)
	# Single-player Petra settings: the first AI slot's picks (per-slot AI is next).
	var diff := 3
	var beh := 0 # player_defaults.json AIBehavior "random"
	for i in range(n):
		if int(_slot_rows[i]["kind"]) == 1:
			diff = int(_slot_rows[i]["diff"])
			beh = int(_slot_rows[i]["beh"])
			break
	return {"map": m, "seed": seed, "victory": victory_code(vmask),
		"waves": %Waves.button_pressed,
		# 0.28 LastManStanding is only sent while teams are unlocked.
		"lms": %LastManStanding.button_pressed and (_is_scenario() or not %LockTeams.button_pressed),
		"pop_cap": _pop_cap_setting(),
		"team_pop": _pop_type == POP_TYPE_TEAM,
		"ceasefire": int(CEASEFIRES[maxi(cf.get_selected(), 0)]),
		"reveal": %RevealMap.button_pressed,
		"explore": %ExploreMap.button_pressed,
		"ally_view": %AlliedView.button_pressed,
		"wonder_min": int(VICTORY_TIMES[maxi(wt.get_selected(), 0)]),
		"relic_min": int(VICTORY_TIMES[maxi(rt.get_selected(), 0)]),
		"relic_count": int(RELIC_COUNTS[maxi(rc.get_selected(), 0)]),
		"water_level": maxi(wl.get_selected(), 0),
		"sea_level_rise_min": int(SEA_RISE_TIMES[maxi(sr.get_selected(), 0)]),
		"team_placement": _selected_team_placement(),
		"landscape": int(LANDSCAPES[maxi(lsc.get_selected(), 0)][1]),
		# Maps without trigger difficulties play at the default, Medium.
		"trigger_difficulty": maxi(tdf.get_selected(), 0) + 1 if str(m.get("id", "")) in TRIGGER_DIFFICULTY_MAPS else 3,
		"map_daytime": maxi(dsel.get_selected(), 0) if DAYTIMES.has(str(m.get("id", ""))) else 0,
		"map_size": int(MAP_SIZES[maxi(msz.get_selected(), 0)][0]),
		"res": res.get_selected(), "beh": beh, "diff": diff,
		"biome": _selected_biome(),
		"speed": float(SPEEDS[spd.get_item_id(spd.get_selected()) if spd.get_selected() >= 0 else DEFAULT_SPEED][0]),
		"types": types, "civs": civs, "teams": teams, "diffs": diffs, "behs": behs, "colors": colors, "names": names,
		# Nomad only on random maps, RegicideGarrison only with Regicide (as 0 A.D. settings).
		"nomad": %Nomad.button_pressed and not m.is_empty() and str(m.get("kind", "")) == "Random",
		"regicide_garrison": %RegicideGarrison.button_pressed and (vmask & VK_REGICIDE) != 0,
		"lock_teams": %LockTeams.button_pressed,
		# Cheats.js: always on in single-player, "the choice of that one player".
		"cheats": true,
		"disable_spies": %DisableSpies.button_pressed,
		"disable_treasures": %DisableTreasures.button_pressed,
		"humans": humans, "ai": ai_total, "slots": types.size(), "local_slot": local}

## functions_utility.js singleplayerName: Options' "Player name (single-player)",
## else the system user name.
static func singleplayer_name() -> String:
	var n := RtsoUserConfig.new().get_value("playername.singleplayer").strip_edges()
	if n.is_empty():
		n = OS.get_environment("USERNAME" if OS.get_name() == "Windows" else "USER").strip_edges()
	return n if not n.is_empty() else "Player 1"

# 0 A.D. PlayerName: your slot has your name (Options > player name), AI slots take an
# unused name from their civ's AINames ("Name 2" when the list runs out), other humans
# are "Player N".
func _player_names(types: Array, civs: Array, local := 0) -> Array:
	var you := singleplayer_name()
	var picked: Array = []
	var names: Array = []
	for i in range(types.size()):
		if i == local and int(types[i]) == 0:
			names.append(you if not you.is_empty() else "Player %d" % (i + 1))
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
	if _mp_role == "client":
		return
	var st := _collect_setup()
	if st["map"].is_empty():
		_show_error("No map selected.")
		return
	if _mp_role == "host":
		# The host's MatchSettings is the setup; RtsoMain starts every peer
		# once all players are ready (0.28 ReadyController).
		if bool(_net_state.get("can_start", false)):
			save_persisted() # launchGame, before Random is resolved
		_host_write_setup(true)
		net_main._on_lobby_start_pressed()
		return
	if int(st["local_slot"]) < 0 and int(st["ai"]) < 2:
		_show_error("Watching a game needs at least 2 AI players.")
		return
	save_persisted() # GameSettingsController.launchGame, before random choices are resolved
	var m: Dictionary = st["map"]
	if str(m["id"]) == RANDOM_MAP_ID:
		# MapSelection's Random: one of the maps the type and filter list.
		var pool := _filtered.filter(func(x): return str(x["id"]) != RANDOM_MAP_ID)
		if pool.is_empty():
			_show_error("No map selected.")
			return
		m = pool[randi() % pool.size()]
	if str(m["type"]) != "random" and int(st["slots"]) != int(m["players"]):
		_show_error("'%s' needs exactly %d players." % [m["name"], int(m["players"])])
		return
	if not has_node("/root/MatchSettings"):
		_show_error("MatchSettings autoload missing.")
		return
	var ms := get_node("/root/MatchSettings")
	ms.map_id = str(m["id"])
	ms.campaign_run = "" # a match from Game Setup is not a campaign level
	ms.campaign_level = ""
	ms.campaign_map_name = ""
	ms.seed = int(st["seed"])
	ms.seed_exact = false
	ms.ai_seed = 0 # only -autostart-aiseed sets one
	ms.victory = int(st["victory"])
	ms.waves_enabled = bool(st["waves"])
	ms.last_man_standing = bool(st["lms"])
	ms.pop_cap = int(st["pop_cap"])
	ms.team_pop = bool(st["team_pop"])
	ms.ceasefire_min = int(st["ceasefire"])
	ms.reveal_map = bool(st["reveal"])
	ms.explore_map = bool(st["explore"])
	ms.ally_view = bool(st.get("ally_view", false))
	ms.wonder_min = int(st["wonder_min"])
	ms.relic_min = int(st["relic_min"])
	ms.relic_count = int(st["relic_count"])
	ms.water_level = int(st["water_level"])
	ms.sea_level_rise_min = int(st["sea_level_rise_min"])
	ms.team_placement = int(st["team_placement"])
	ms.landscape = int(st["landscape"])
	ms.trigger_difficulty = int(st.get("trigger_difficulty", 3))
	ms.map_daytime = int(st.get("map_daytime", 0))
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
	ms.local_slot = int(st["local_slot"])
	ms.nomad = bool(st["nomad"])
	ms.regicide_garrison = bool(st["regicide_garrison"])
	ms.lock_teams = bool(st["lock_teams"])
	ms.cheats = bool(st["cheats"])
	ms.rated = false # RatingEnabled: only a lobby game is rated
	ms.disable_spies = bool(st["disable_spies"])
	ms.disable_treasures = bool(st["disable_treasures"])
	ms.biome = int(st["biome"])
	var offered: Array = m.get("biomes", [])
	if ms.biome == RANDOM_BIOME: # 0.28 picks the random biome at launch
		ms.biome = int(offered[randi() % offered.size()]) if not offered.is_empty() else 0
	ms.game_speed = float(st["speed"])
	ms.player_count = int(st["slots"])
	ms.player_civ = int(st["civs"][0])
	# Legacy back-compat: first AI slot's civ.
	ms.ai_civ = int(st["civs"][1]) if (st["types"] as Array).size() > 1 and int(st["types"][1]) == 1 else int(st["civs"][0])
	print("[GameSetup] Start — %s seed=%d victory=%d diff=%d beh=%d res=%d slots=%s civs=%s teams=%s" % [
		ms.map_id, ms.seed, ms.victory, ms.ai_difficulty, ms.ai_behavior,
		ms.start_res, str(st["types"]), str(st["civs"]), str(st["teams"])])
	var err := MatchSettings.go("res://scenes/main.tscn")
	if err != OK:
		_show_error("Could not load match scene.")

## translatePlural("%(min)s minute", "%(min)s minutes").
func _minutes(n: int) -> String:
	return L10n.sprintf(tr("%(min)s minute") if n == 1 else tr("%(min)s minutes"), {"min": n})

func _show_error(msg: String) -> void:
	var lbl := $VBox/HBox/Right/Buttons/BtnStart as Button
	push_warning("[GameSetup] " + msg)
	var err := $VBox/HBox/Right/ErrorLabel as Label
	if err:
		err.text = msg

# ------------------------------------------------------- networked setup ---
# 0 A.D. 0.28 gamesetup in a networked game. The host (the controller) edits
# the page as in single player; its MatchSettings is the setup RtsoMain sends
# to every client. A client shows the host's setup, may change only its own
# slot's civ, team and colour (asked of the host), and presses Ready; the
# host's Start waits until every player is ready. net_main is the RtsoMain
# that runs the networked game, set before the page enters the tree.

var net_main: Node = null
var _mp_role := "none"           # "none", "host" or "client"
var _net_state := {}             # net_main.get_net_setup_state()
var _net_sent := {}              # host: the settings written last
var _net_random_map := ""        # host: the map Random picked, kept while Random stays selected
var _net_names_key := ""
var _net_names: Array = []
var _net_tick := 0.0
var _ready_btn: Button
var _rated: CheckBox
var _setting_warning: Label
var _saved_label: Label
## The player count and cheats setting Rated Game was last set from (Rating.js maybeUpdate).
var _rated_key := ""
var _rated_locked := false # LockedTeams.onRatingChange: the rated game has locked the teams
## 0.28 ReadyButton captions and tooltips per ready state (NotReady, Ready,
## StayReady); pressing moves to the next state.
const READY_CAPTIONS := ["I'm ready", "Stay ready", "I'm not ready!"]
const READY_TOOLTIPS := ["State that you are ready to play.", "Stay ready even when the game settings change.",
	"State that you are not ready to play."]
## 0.28 StartGameButton tooltips.
const START_TOOLTIP := "Start a new game with the current settings."
const START_WAIT_TOOLTIP := "Start a new game with the current settings (disabled until all players are ready)."
const NET_SLOT_KEYS := ["slot_types", "slot_civs", "slot_teams", "slot_diffs", "slot_behs", "slot_colors"]

func _init_net() -> void:
	if net_main == null or not net_main.has_method("is_net_pregame") or not net_main.is_net_pregame():
		return
	_net_state = net_main.get_net_setup_state()
	_mp_role = "host" if bool(_net_state.get("controller", false)) else "client"
	_build_net_controls()
	_build_net_chat()
	var obs := Label.new()
	obs.name = "NetObservers"
	obs.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	obs.visible = false
	$VBox/HBox/Right/SlotsBox.add_child(obs)
	obs.owner = self
	obs.unique_name_in_owner = true
	net_main.net_setup_changed.connect(_on_net_setup_changed)
	if net_main.has_signal("net_setup_refused"):
		net_main.net_setup_refused.connect(_show_error)
	if _mp_role == "client":
		_on_net_setup_changed()
		_render_loading()
	else:
		_host_write_setup(true)
		refresh_slots()
		_update_net_controls()

func _build_net_controls() -> void:
	var right := $VBox/HBox/Right
	var start := $VBox/HBox/Right/Buttons/BtnStart as Button
	_ready_btn = Button.new()
	_ready_btn.name = "BtnReady"
	_ready_btn.custom_minimum_size = Vector2(0, 48)
	_ready_btn.pressed.connect(_on_ready_pressed)
	_ready_btn.gui_input.connect(_on_ready_gui_input)
	_ready_btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	start.get_parent().add_child(_ready_btn)
	start.get_parent().move_child(_ready_btn, start.get_index())
	# 0.28 GameSettingWarning: "Cheats enabled." or "Rated game." on every
	# networked setup page.
	_setting_warning = Label.new()
	_setting_warning.name = "GameSettingWarning"
	_setting_warning.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_setting_warning.add_theme_color_override("font_color", Color.RED) # GameSettingWarning.xml textcolor="red"
	right.add_child(_setting_warning)
	right.move_child(_setting_warning, $VBox/HBox/Right/Buttons.get_index())
	# 0.28 SavedGameLabel: shown while the controller hosts a saved game.
	_saved_label = Label.new()
	_saved_label.name = "SavedGameLabel"
	_saved_label.text = "Saved game"
	_saved_label.tooltip_text = "The controller loaded a saved game."
	_saved_label.mouse_filter = Control.MOUSE_FILTER_PASS
	_saved_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_saved_label.visible = false
	right.add_child(_saved_label)
	right.move_child(_saved_label, $VBox/HBox/Right/Buttons.get_index())
	_ready_btn.visible = _mp_role == "client"
	start.visible = _mp_role == "host"
	# 0.28 LobbyButton: with a lobby connection, the lobby opens as a dialog.
	var lobby_btn := Button.new()
	lobby_btn.name = "LobbyButton"
	lobby_btn.text = "Lobby"
	lobby_btn.tooltip_text = "Show the multiplayer lobby in a dialog window."
	lobby_btn.visible = LobbySession.signed_in()
	lobby_btn.pressed.connect(open_lobby_dialog)
	$VBox/Header.add_child(lobby_btn)
	# CancelButton: back to the lobby while signed in to it.
	($VBox/HBox/Right/Buttons/BtnBack as Button).tooltip_text = "Return to the lobby." if LobbySession.signed_in() else "Return to the main menu."
	lobby_btn.owner = self
	lobby_btn.unique_name_in_owner = true
	# 0.28 Rating.js "Rated Game" and Cheats.js, which the host sets in a
	# networked game (off by default) unless the game is rated.
	var cheats: CheckBox = %Cheats
	# A saved game keeps its own cheats setting (nothing but the seats changes).
	cheats.button_pressed = _saved_game() and bool(get_node("/root/MatchSettings").cheats)
	cheats.disabled = false
	cheats.toggled.connect(func(_on): _update_rated(); _update_net_controls())
	_rated = CheckBox.new()
	_rated.name = "RatedGame"
	_rated.text = "Rated Game"
	_rated.tooltip_text = "Toggle if this game will be rated for the leaderboard."
	_rated.toggled.connect(func(_on): _update_rated(); _update_net_controls())
	# GameSettingsLayout.js: Rating closes the Game Type tab.
	%TeamsRow.add_child(_rated)
	_rated.owner = self
	_rated.unique_name_in_owner = true
	_update_rated()

## Engine.OpenChildPage("page_lobby.xml", {"dialog": true}): the lobby over
## the setup on its own layer; it frees itself on Back.
func open_lobby_dialog() -> Node:
	return load("res://scenes/menu/lobby.gd").open_dialog(get_tree())

## 0.28 Panels/Chat/ChatPanel: the chat under the player slots, where single
## player shows its tips. The input goes through RtsoMain's chat send, so
## /kick, /ban, /list and /clear (executeNetworkCommand) work here too.
var _chat_rev := -1

func _build_net_chat() -> void:
	if not net_main.has_method("get_setup_chat"):
		return
	var panel := PanelContainer.new()
	panel.name = "NetChat"
	var box := VBoxContainer.new()
	panel.add_child(box)
	var log := RichTextLabel.new()
	log.name = "NetChatLog"
	log.bbcode_enabled = true
	log.scroll_following = true
	log.custom_minimum_size = Vector2(0, 110)
	log.size_flags_vertical = Control.SIZE_EXPAND_FILL
	log.add_theme_font_size_override("normal_font_size", 12)
	box.add_child(log)
	var row := HBoxContainer.new()
	box.add_child(row)
	var input := LineEdit.new()
	input.name = "NetChatInput"
	input.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	input.max_length = 1024 # gamesetup ChatPanel.xml chatInput max_length
	input.text_submitted.connect(func(_t: String) -> void: _send_net_chat())
	# ChatInputPanel.onTab / Tooltip: Tab completes player names, then civs and settings.
	# colorizeAutocompleteHotkey: the hard-coded Tab, in the hotkey colour.
	input.tooltip_text = L10n.sprintf(L10n.t("Press %(hotkey)s to autocomplete player names or settings."),
		{"hotkey": "[color=#fffb83][lb]" + tr("Tab", "hotkey") + "[rb][/color]"})
	input.gui_input.connect(func(ev: InputEvent) -> void: _on_net_chat_key(ev, input))
	row.add_child(input)
	var send := Button.new()
	send.name = "NetChatSend"
	send.text = tr("Send")
	send.pressed.connect(_send_net_chat)
	row.add_child(send)
	var right := $VBox/HBox/Right
	right.add_child(panel)
	right.move_child(panel, ($VBox/HBox/Right/ErrorLabel as Control).get_index())
	for n in [panel, log, input, send]:
		n.owner = self
		n.unique_name_in_owner = true
	_update_net_chat()

## functions_utility.js tryAutoComplete: the last word becomes the one entry that
## starts with it (any case), when exactly one does.
static func try_autocomplete(text: String, words: Array) -> String:
	var split := text.split(" ")
	var last := split[split.size() - 1]
	if last.is_empty():
		return text
	var found := []
	for w in words:
		if str(w).to_lower().begins_with(last.to_lower()):
			found.append(str(w))
			if found.size() > 1:
				break
	if found.size() != 1:
		return text
	split.remove_at(split.size() - 1)
	var head := " ".join(split)
	return (head + " " if head != "" else "") + found[0]

## ChatInputAutocomplete.getAutocompleteEntries: player names (order 100), civs (90),
## then the setting titles and every dropdown's choices (0), without repeats.
func autocomplete_entries() -> Array:
	var names: Array = []
	for e in Array(_net_state.get("slots", [])) + Array(_net_state.get("observers", [])):
		if int(e.get("client", -1)) >= 0 and str(e.get("name", "")) != "":
			names.append(str(e["name"]))
	for i in _civ_order():
		names.append(str(CIVS[i]["name"]))
	var settings: Array = []
	_collect_autocomplete($VBox, settings, _slots_grid())
	var out: Array = []
	for w in names + settings:
		if w != "" and not out.has(w):
			out.append(w)
	return out

## The per-player rows add only names and civs (above), as in 0.28.
func _collect_autocomplete(n: Node, out: Array, skip: Node) -> void:
	if n is Label and (n as Label).text.ends_with(":"):
		out.append((n as Label).text.trim_suffix(":").strip_edges())
	elif n is OptionButton or n is RtsoSafeSelect:
		var count: int = n.get_item_count()
		for i in count:
			out.append(str(n.get_item_text(i)))
	elif n is ItemList:
		var il := n as ItemList
		for i in il.item_count:
			out.append(il.get_item_text(i))
	for c in n.get_children():
		if c is Control and c != skip and c.name != "NetChat":
			_collect_autocomplete(c, out, skip)

## autoCompleteText: Tab completes the word before the caret.
func _on_net_chat_key(ev: InputEvent, line: LineEdit) -> void:
	var k := ev as InputEventKey
	if k == null or not k.pressed or k.keycode != KEY_TAB:
		return
	line.accept_event()
	var caret := line.caret_column
	var before := line.text.substr(0, caret)
	var done := try_autocomplete(before, autocomplete_entries())
	line.text = done + line.text.substr(caret)
	line.caret_column = caret + done.length() - before.length()

func _send_net_chat() -> void:
	var input: LineEdit = %NetChatInput
	var text := input.text.strip_edges()
	input.clear()
	if text != "":
		net_main.send_setup_chat(text)
	_update_net_chat()

func _update_net_chat() -> void:
	if not has_node("%NetChatLog"):
		return
	var chat: Dictionary = net_main.get_setup_chat()
	if int(chat.get("rev", 0)) == _chat_rev:
		return
	_chat_rev = int(chat.get("rev", 0))
	var log: RichTextLabel = %NetChatLog
	log.text = str(chat.get("text", ""))

## Rating.js: a lobby game of exactly two players without cheats may be rated, and is by
## default whenever the player count or cheats change (maybeUpdate: enabled = available);
## Cheats.js: a rated game has no cheats.
func _rated_available() -> bool:
	# Rating.js: a saved game is never rated.
	return not _saved_game() and has_node("/root/MatchSettings") and str(get_node("/root/MatchSettings").net_lobby) != "" \
		and _net_rows().size() == 2

func _update_rated() -> void:
	if _rated == null or _mp_role != "host":
		return
	var cheats: CheckBox = %Cheats
	var avail := _rated_available() and not cheats.button_pressed
	var key := "%d/%s" % [_net_rows().size(), cheats.button_pressed]
	if key != _rated_key:
		_rated_key = key
		_rated.set_pressed_no_signal(avail)
	if not avail:
		_rated.set_pressed_no_signal(false)
	_rated.visible = avail
	cheats.disabled = avail and _rated.button_pressed
	# LockedTeams.onRatingChange: a rated game has Locked teams on and fixed; unrating it unlocks them.
	var rated := avail and _rated.button_pressed
	if rated != _rated_locked:
		_rated_locked = rated
		%LockTeams.button_pressed = rated
		_update_lock_teams_for_pop()

func _update_net_controls() -> void:
	if _mp_role == "none":
		return
	if _saved_label:
		_saved_label.visible = _saved_game()
	if _setting_warning:
		var rated := _rated != null and _rated.visible and _rated.button_pressed
		_setting_warning.text = "Cheats enabled." if %Cheats.button_pressed and not rated \
				else "Rated game." if rated else ""
		_setting_warning.visible = _setting_warning.text != ""
	var start := $VBox/HBox/Right/Buttons/BtnStart as Button
	if _mp_role == "host":
		var ok := bool(_net_state.get("can_start", false))
		start.disabled = not ok
		start.tooltip_text = START_TOOLTIP if ok else START_WAIT_TOOLTIP
		return
	var ready := clampi(int(_net_state.get("ready", 0)), 0, 2)
	_ready_btn.text = READY_CAPTIONS[ready]
	_ready_btn.tooltip_text = READY_TOOLTIPS[ready]
	# ReadyButton.onPlayerAssignmentsChange: hidden for an unassigned client (an observer).
	_ready_btn.visible = int(_net_state.get("my_slot", -1)) >= 0

## The client lost the host in the setup: ChatInputPanel hides the chat input
## and its button, and ReadyButton disables itself; the page and its chat stay.
func net_disconnected() -> void:
	var input := get_node_or_null("%NetChatInput") as Control
	if input:
		input.get_parent().visible = false
	if _ready_btn:
		_ready_btn.disabled = true

func _on_ready_pressed() -> void:
	if _mp_role != "client":
		return
	_set_ready((clampi(int(_net_state.get("ready", 0)), 0, 2) + 1) % 3)

## ReadyButton.onPressRight: a right-click always means "not ready".
func _on_ready_gui_input(event: InputEvent) -> void:
	var mb := event as InputEventMouseButton
	if mb and mb.button_index == MOUSE_BUTTON_RIGHT and mb.pressed and _mp_role == "client" \
			and _ready_btn.visible and int(_net_state.get("ready", 0)) != 0:
		_set_ready(0)
		_ready_btn.accept_event()

func _set_ready(state: int) -> void:
	net_main.set_net_ready(state)
	_net_state = net_main.get_net_setup_state()
	refresh_slots()
	_update_net_controls()

func _process(delta: float) -> void:
	_tick_slide(delta)
	# GameDescription.updateGameDescription: the summary follows every setting.
	_desc_tick += delta
	if _desc_tick >= 0.3:
		_desc_tick = 0.0
		_update_description()
		_render_value_texts()
	if _mp_role == "none":
		return
	_update_net_chat()
	_net_tick += delta
	if _net_tick < 0.25:
		return
	_net_tick = 0.0
	if _mp_role == "host":
		_host_write_setup(false)
	_apply_mp_locks() # controls the page rebuilt since

## The rows that are in the setup (Closed slots are dropped, as at Start):
## the setup's slot k is row _net_rows()[k].
func _net_rows() -> Array:
	var rows: Array = []
	var n := mini(int(_slot_count_node().value), _slot_rows.size())
	for i in range(n):
		if i == 0 or int(_slot_rows[i]["kind"]) != 2:
			rows.append(i)
	return rows

## Host: the page into MatchSettings, which RtsoMain sends when it changed.
func _host_write_setup(force: bool) -> void:
	if _mp_role != "host" or not has_node("/root/MatchSettings"):
		return
	var ps := page_state()
	var keep := _net_rows()
	for k in NET_SLOT_KEYS:
		var a: Array = ps[k]
		ps[k] = keep.filter(func(i): return i < a.size()).map(func(i): return a[i])
	if not ps["slot_types"].is_empty():
		ps["slot_types"][0] = mini(int(ps["slot_types"][0]), 1)
	ps["net_map_random"] = str(ps["map_id"]) == RANDOM_MAP_ID
	if ps["net_map_random"]:
		# Every peer must load the same map, so Random picks one now; the
		# clients see "Random" until Start (launchGame resolves it), as
		# RtsoMain sends the setup with map "random".
		var pool := _filtered.filter(func(x): return str(x["id"]) != RANDOM_MAP_ID)
		if not pool.any(func(x): return str(x["id"]) == _net_random_map) and not pool.is_empty():
			_net_random_map = str(pool[randi() % pool.size()]["id"])
		ps["map_id"] = _net_random_map
	ps["map_locks_players"] = _is_scenario()
	_update_rated()
	ps["rated"] = _rated_available() and _rated.button_pressed
	# Cheats.js: the host's choice in a networked game, never in a rated one.
	ps["cheats"] = %Cheats.button_pressed and not ps["rated"]
	if not force and ps == _net_sent:
		return
	_net_sent = ps.duplicate(true)
	var ms := get_node("/root/MatchSettings")
	for k in ps:
		ms.set(k, ps[k])
	var key := str(ps["slot_types"]) + str(ps["slot_civs"])
	if key != _net_names_key:
		_net_names_key = key
		_net_names = _player_names(ps["slot_types"], ps["slot_civs"])
	ms.slot_names = _net_names
	ms.player_count = ps["slot_types"].size()
	ms.campaign_run = ""
	ms.campaign_level = ""
	ms.campaign_map_name = ""
	ms.ai_difficulty = 3
	ms.ai_behavior = 0
	for i in range(ps["slot_types"].size()):
		if int(ps["slot_types"][i]) == 1:
			ms.ai_difficulty = int(ps["slot_diffs"][i])
			ms.ai_behavior = int(ps["slot_behs"][i])
			break
	net_main.push_net_setup()

## LoadingPage: a client sees "Loading map data. Please wait…", not its own
## last settings, until the host's first setup arrives (GameSettingsController's
## loading state); the setup page shows only then.
var _loading_page: Control
func _net_loading() -> bool:
	return _mp_role == "client" and int(_net_state.get("version", 0)) <= 0

func _render_loading() -> void:
	var loading := _net_loading()
	if loading and _loading_page == null:
		_loading_page = Panel.new()
		_loading_page.name = "LoadingPage"
		_loading_page.theme = MODERN.theme()
		_loading_page.theme_type_variation = "ModernDialog"
		_loading_page.set_anchors_preset(Control.PRESET_CENTER)
		_loading_page.offset_left = -190
		_loading_page.offset_right = 190
		_loading_page.offset_top = -80
		_loading_page.offset_bottom = 80
		var title := Label.new()
		title.text = tr("Loading")
		title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		title.position = Vector2(62, -16)
		title.size = Vector2(256, 32)
		_loading_page.add_child(title)
		var text := Label.new()
		text.name = "Text"
		text.text = tr("Loading map data. Please wait…")
		text.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		text.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		text.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
		_loading_page.add_child(text)
		add_child(_loading_page)
	if _loading_page:
		_loading_page.visible = loading
	$VBox.visible = not loading

func _on_net_setup_changed() -> void:
	_net_state = net_main.get_net_setup_state()
	var ms := get_node("/root/MatchSettings")
	_render_loading()
	if _net_loading():
		return
	if _mp_role == "client":
		var src := {}
		for k in PERSISTED_KEYS:
			src[k] = ms.get(k)
		_apply_settings(src)
		refresh_map_list()
		%Cheats.set_pressed_no_signal(bool(ms.cheats))
		_rated.set_pressed_no_signal(bool(ms.rated))
		_rated.visible = bool(ms.rated)
	elif _mp_role == "host":
		# A client's own civ, team or colour, which RtsoMain wrote into MatchSettings.
		var rows := _net_rows()
		var fields := {"slot_civs": "civ", "slot_teams": "team", "slot_colors": "color"}
		for f in fields:
			var a: Array = ms.get(f)
			for k in range(mini(a.size(), rows.size())):
				_slot_rows[rows[k]][fields[f]] = int(a[k])
				if _net_sent.has(f) and k < _net_sent[f].size():
					_net_sent[f][k] = int(a[k])
	refresh_slots()
	_update_preview()
	_update_net_controls()
	_update_net_observers()

## A client asks the host for its own slot's civ, team or colour.
func _net_request(key: String, value: int) -> void:
	if _mp_role == "client" and not _rebuilding_slots:
		net_main.request_net_player_setting(key, value)

## The slot's name (PlayerName.js): player_defaults.json's "Player N", translated in
## a single-player setup, where the players' own names are only filled in at launch;
## in a networked game, the client in it.
func _slot_label(i: int, _row: Dictionary) -> String:
	var default_name := "Player %d" % (i + 1)
	if _mp_role == "none":
		return tr(default_name)
	var k := _net_rows().find(i) if _mp_role == "host" else i
	var slots: Array = _net_state.get("slots", [])
	if k < 0 or k >= slots.size() or int(slots[k].get("client", -1)) < 0:
		return default_name
	return str(slots[k].get("name", ""))

## PlayerName.js ReadyTags: a client not ready white, ready green; a slot with no client 150 150 250.
func _ready_color(i: int) -> Color:
	var k := _net_rows().find(i) if _mp_role == "host" else i
	var slots: Array = _net_state.get("slots", [])
	if k < 0 or k >= slots.size() or int(slots[k].get("client", -1)) < 0:
		return Color8(150, 150, 250)
	var e: Dictionary = slots[k]
	return Color.GREEN if int(e["client"]) > 0 and int(e.get("ready", 0)) > 0 else Color.WHITE

## 0.28 PlayerAssignment in a networked setup: one dropdown per slot listing the
## connected clients, "AI: Petra Bot", "Unassigned" and "Removed" (not on slot 1).
## Picking a client seats it here (one holding another slot swaps, as NetServer
## AssignPlayer does); AI, Unassigned and Removed send the holder to watch. A saved
## game offers no AI on a human slot and keeps an AI slot as it is. Only the host
## changes it; a client sees it locked.
const ASSIGN_CLIENT_ID := 10 # item id of client c: 10 + c

## PlayerAssignmentItem tags: a player white, an observer 170 170 250, an AI
## 70 150 70, Unassigned 140 140 140, Removed 255 140 140.
func _color_assignment_items(sel: RtsoSafeSelect, observers: Array) -> void:
	for ix in range(sel.get_item_count()):
		var id := sel.get_item_id(ix)
		var c := Color.WHITE
		if id == 1:
			c = Color8(70, 150, 70)
		elif id == 0:
			c = Color8(140, 140, 140)
		elif id == 2:
			c = Color8(255, 140, 140)
		elif id >= ASSIGN_CLIENT_ID and observers.has(id - ASSIGN_CLIENT_ID):
			c = Color8(170, 170, 250)
		sel.set_item_color(ix, c)

func _assignment_cell(i: int) -> Control:
	var cell := Control.new()
	cell.name = "Assign%d" % i
	cell.custom_minimum_size = Vector2(110, 28)
	var ob := _make_option(cell)
	ob.name = "AssignSelect"
	var kind := int(_slot_rows[i]["kind"]) if i > 0 else mini(int(_slot_rows[i]["kind"]), 1)
	var k := _net_rows().find(i) if _mp_role == "host" else i
	var slots: Array = _net_state.get("slots", [])
	var holder := int(slots[k].get("client", -1)) if k >= 0 and k < slots.size() else -1
	var clients: Array = []
	for e in slots + Array(_net_state.get("observers", [])):
		if int(e.get("client", -1)) >= 0 and not clients.any(func(x): return int(x["client"]) == int(e["client"])):
			clients.append(e)
	var saved := _saved_game()
	for e in clients:
		ob.add_item(str(e.get("name", "")), ASSIGN_CLIENT_ID + int(e["client"]))
	if not saved or kind == 1:
		ob.add_item(tr("AI: %(ai)s").replace("%(ai)s", tr("Petra Bot")), 1)
	ob.add_item(tr("Unassigned"), 0)
	if i > 0 and (not saved or kind == 2):
		ob.add_item(tr("Removed"), 2)
	var observers: Array = Array(_net_state.get("observers", [])).map(func(e): return int(e.get("client", -1)))
	_color_assignment_items(ob, observers)
	var sel := ASSIGN_CLIENT_ID + holder if kind == 0 and holder >= 0 else kind
	ob.select(maxi(ob.get_item_index(sel), 0))
	ob.tooltip_text = tr("Select player.")
	ob.disabled = _mp_role != "host" or (saved and kind == 1)
	ob.item_selected.connect(func(idx: int) -> void:
		if _rebuilding_slots or _mp_role != "host":
			return
		var id := int(ob.get_item_id(idx))
		if holder >= 0 and id < ASSIGN_CLIENT_ID:
			net_main.assign_net_client(holder, -1)
		if id >= ASSIGN_CLIENT_ID:
			# PlayerAssignment Client.onSelectionChange: a seated player takes its
			# civ and colour along (not on a scenario or a saved game), and an AI
			# here moves to the slot the player left.
			var src := _net_client_row(id - ASSIGN_CLIENT_ID)
			var moved := src >= 0 and src != i
			if moved:
				var old: Dictionary = _slot_rows[src]
				var row: Dictionary = _slot_rows[i]
				var keys: Array = ["diff", "beh"] if kind == 1 else []
				if not _is_scenario() and not saved:
					keys += ["civ", "color"]
				for key in keys:
					var t = old[key]
					old[key] = row[key]
					row[key] = t
				if kind == 1:
					_on_slot_kind(1, src)
			if kind != 0:
				_on_slot_kind(0, i)
			if moved or kind != 0:
				_host_write_setup(true)
			net_main.assign_net_client(id - ASSIGN_CLIENT_ID, _net_rows().find(i))
		elif id != kind:
			_on_slot_kind(id, i)
			_host_write_setup(true)
		_net_state = net_main.get_net_setup_state()
		refresh_slots.call_deferred())
	return cell

## Host: the slot row client `c` plays in the sent setup (-1 while it watches).
func _net_client_row(c: int) -> int:
	var slots: Array = _net_state.get("slots", [])
	var rows := _net_rows()
	for k in mini(slots.size(), rows.size()):
		if int(slots[k].get("client", -1)) == c:
			return int(rows[k])
	return -1

## Observers.js / the 0.28 player list: who watches, under the slots.
func _update_net_observers() -> void:
	if _mp_role == "none" or not has_node("%NetObservers"):
		return
	var names: Array = Array(_net_state.get("observers", [])).map(func(e): return str(e.get("name", "")))
	var lbl: Label = %NetObservers
	lbl.text = "Observers: " + ", ".join(names) if not names.is_empty() else ""
	lbl.visible = not names.is_empty()

## GameSettingControlManager with isSavedGame: the controller loaded a saved
## game, so only its PlayerAssignment stays enabled (EnabledWhenSavedGame).
func _saved_game() -> bool:
	return bool(_net_state.get("saved_game", false))

## Session UI's single lock pass: a client may change only its own slot's civ,
## team and colour (none on a map that sets them, or in a saved game); with a
## saved game the host may change only who plays which slot. Only adds
## disables, after the map's own locks. Back, the chat and the Ready button
## stay usable.
func _apply_mp_locks() -> void:
	if _mp_role == "none" or (_mp_role == "host" and not _saved_game()):
		return
	var keep := {}
	for n in [_ready_btn, $VBox/HBox/Right/Buttons/BtnBack, $VBox/HBox/Right/Buttons/BtnStart, get_node_or_null("%NetChat")]:
		if n:
			keep[n] = true
	var cells := _slots_grid().get_children().filter(func(c): return not c.is_queued_for_deletion())
	if _mp_role == "host":
		for c in cells:
			if c.has_node("AssignSelect"):
				keep[c] = true
	# GameSettingsTabs, CivInfoButton, AIConfigButton and MapBrowser.js: the tabs
	# and the viewers are no settings, so everyone opens them (a client and a
	# saved game's host read only); Browse Maps is hidden for a saved game.
	for name in TAB_BUTTONS + ["CivInfoButton", "AIConfigButton", "BrowseMaps"]:
		for b in $VBox/HBox.find_children(name, "BaseButton", true, false):
			keep[b] = true
			(b as BaseButton).disabled = false
	var browse := find_child("BrowseMaps", true, false) as Control
	if browse:
		browse.visible = not _saved_game()
	# GameSettingControl: a client changes nothing, its own slot included (0.28
	# NetServer relays only the controller's setup).
	_lock_tree($VBox/HBox, keep)

func _lock_tree(n: Node, keep: Dictionary) -> void:
	if keep.has(n):
		return
	if n is BaseButton:
		n.disabled = true
	elif n is SpinBox or n is Slider:
		n.editable = false
	elif n is LineEdit:
		n.editable = false
	elif n is ItemList:
		n.mouse_filter = Control.MOUSE_FILTER_IGNORE
		n.focus_mode = Control.FOCUS_NONE
	for c in n.get_children():
		_lock_tree(c, keep)
