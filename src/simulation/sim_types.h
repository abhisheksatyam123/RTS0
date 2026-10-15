#ifndef RTSO_SIM_TYPES_H
#define RTSO_SIM_TYPES_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "flora_data.h"

namespace rtso {


// 0 A.D. DeathDamage.js & Loot.js: track recent death events for area splash and bounty
struct RecentDeath {
	float x = 0.0f;
	float z = 0.0f;
	uint8_t team = 0;
	uint8_t killer_team = 0;
	uint8_t type = 0;
	bool is_building = false;
	bool death_damage_applied = false;
	bool death_damage = false; // the dead unit has DeathDamage (fire ships); was the unused loot_applied byte
};
// DeathDamage.js: Circular splash when the entity dies (Range in RTS0 units, Damage summed).
struct DeathDamageInfo {
	float range;
	float damage; // Damage/Crush, against the target's crush Resistance
	bool friendly_fire;
	float ship_building_mult; // Bonuses: Classes "Ship Building" Multiplier
};
enum BiomeType : uint8_t {
	BIOME_AEGEAN = 0,
	BIOME_LATIUM = 1,
	BIOME_ALPINE = 2,
	BIOME_OASIS = 3,
	BIOME_TEMPERATE = 4,
	BIOME_STEPPE = 5,
	BIOME_SAVANNA = 6,
	BIOME_POLAR = 7,
	BIOME_INDIA = 8,
	BIOME_AUTUMN = 9, // generic/autumn (Rhine Valley, Fall)
	BIOME_NUBIA = 10, // generic/nubia
	BIOME_COUNT = 11
};

// Player colours, 0xRRGGBB. The first 8 are 0 A.D. simulation/data/settings/player_defaults.json
// (Player 1..8, each player's default); the other 16 are gamesettings/attributes/PlayerColor.js
// DefaultColors, the Game Setup palette. Game Setup picks one index per player.
constexpr int PLAYER_COLOR_COUNT = 24;
inline constexpr uint32_t PLAYER_COLORS[PLAYER_COLOR_COUNT] = {
	0x153795, 0x961414, 0x56B41F, 0xE7C805, 0x32AAAA, 0xA050C8, 0xDC7310, 0x404040,
	0x0A0ABE, 0xE60A0A, 0x7DEB0F, 0xFFFF37, 0x8200E6, 0xFF8200, 0x0AE6E6, 0x14503C,
	0xDCA0DC, 0x50FFBE, 0x3296FF, 0x64961E, 0x643C1E, 0x800040, 0xFFC88C, 0x505050,
};

enum Civilization : uint8_t {
	CIV_ATHENIANS = 0,
	CIV_SPARTANS = 1,
	CIV_PERSIANS = 2,
	CIV_ROMANS = 3,
	CIV_CARTHAGINIANS = 4,
	CIV_MACEDONIANS = 5,
	CIV_BRITONS = 6,
	CIV_GAULS = 7,
	CIV_IBERIANS = 8,
	CIV_MAURYANS = 9,
	CIV_KUSHITES = 10,
	CIV_PTOLEMIES = 11,
	CIV_SELEUCIDS = 12,
	CIV_HAN = 13, // appended so saved games and network messages keep civ ids
	// Not a playable civ: the civ saved on Gaia's structures (Building::civ, save v62). Every
	// per-civ table keeps an empty row for it; loops over playable civs skip it (is_playable_civ).
	CIV_GAIA = 14,
	CIV_GERMANS = 15, // 0.28's Germans (germ), after CIV_GAIA so saved Gaia structures keep 14
	CIV_COUNT = 16
};
inline constexpr uint8_t PLAYABLE_CIV_COUNT = CIV_COUNT - 1;

// A civ a player can play (0 A.D. data/civs/*.json SelectableInGameSetup): every
// Civilization but CIV_GAIA.
inline constexpr bool is_playable_civ(int civ) { return civ >= 0 && civ < CIV_COUNT && civ != CIV_GAIA; }
// The k-th playable civ (k taken modulo PLAYABLE_CIV_COUNT): for random civ picks.
inline constexpr uint8_t playable_civ(uint32_t k) {
	const uint8_t c = (uint8_t)(k % PLAYABLE_CIV_COUNT);
	return c < CIV_GAIA ? c : (uint8_t)(c + 1);
}

enum MapType : uint8_t {
	MAP_AEGEAN_ARCHIPELAGO = 0,
	MAP_PELOPONNESE = 1,
	MAP_ACROPOLIS = 2,
	MAP_SAHARA = 3,
	MAP_SPARTAN_VALLEY = 4,
	MAP_RANDOM_CONTINENTAL = 5,
	MAP_RANDOM_ARCHIPELAGO = 6,
	MAP_RANDOM_OASIS = 7,
	MAP_RANDOM_HIGHLANDS = 8,
	MAP_RANDOM_VALLEY = 9,
	MAP_RANDOM_MEDITERRANEAN = 10,
	MAP_DANUBIUS = 11,
	MAP_SAHARAN_OASES = 12,
	MAP_ANATOLIAN_PLATEAU = 13,
	MAP_JEBEL_BARKAL = 14,
	// Modelled on 0 A.D. maps/random/*.js
	MAP_MAINLAND = 15,
	MAP_ALPINE_LAKES = 16,
	MAP_LAKE = 17,
	MAP_GULF_OF_BOTHNIA = 18,
	MAP_ARDENNES_FOREST = 19,
	MAP_NEARESTERN_BADLANDS = 20,
	MAP_RIVERS = 21,
	MAP_CANYON = 22,
	MAP_CORINTHIAN_ISTHMUS = 23,
	MAP_BELGIAN_UPLANDS = 24,
	MAP_LORRAINE_PLAIN = 25,
	MAP_HYRCANIAN_SHORES = 26,
	MAP_NORTHERN_LIGHTS = 27,
	MAP_KERALA = 28,
	MAP_POLAR_SEA = 29,
	MAP_SAHEL = 30,
	MAP_AFRICAN_PLAINS = 31,
	MAP_ALPINE_VALLEY = 32,
	MAP_SYRIA = 33,
	MAP_CANTABRIAN_HIGHLANDS = 34,
	MAP_PYRENEAN_SIERRA = 35,
	MAP_SAHEL_WATERING_HOLES = 36,
	MAP_PERSIAN_HIGHLANDS = 37,
	MAP_VOLCANIC_LANDS = 38,
	MAP_LIONS_DEN = 39,
	MAP_HELLS_PASS = 40,
	MAP_DEEP_FOREST = 41,
	MAP_INDIA = 42,
	MAP_GUADALQUIVIR_RIVER = 43,
	MAP_FIELDS_OF_MEROE = 44,
	MAP_GEAR = 45,
	MAP_ATLAS_MOUNTAINS = 46,
	MAP_FRONTIER = 47,
	MAP_WILD_LAKE = 48,
	MAP_STRONGHOLD = 49,
	MAP_CALEDONIAN_MEADOWS = 50,
	MAP_RHINE_MARSHLANDS = 51,
	MAP_RED_SEA = 52,
	MAP_HELLAS = 53,
	MAP_ENGLISH_CHANNEL = 54,
	MAP_DODECANESE = 55,
	MAP_FORTRESS = 56,
	// 0 A.D. trigger maps (maps/random/*_triggers.js run as map triggers, triggers.h)
	MAP_FLOOD = 57,
	MAP_EXTINCT_VOLCANO = 58,
	MAP_AEGEAN_SEA = 59,
	MAP_ARCHIPELAGO_0AD = 60,
	MAP_ISLANDS = 61,
	MAP_CYCLADIC_ARCHIPELAGO = 62,
	MAP_SNOWFLAKE_SEAROCKS = 63,
	MAP_HARBOR = 64,
	MAP_CORSICA = 65,
	MAP_PHOENICIAN_LEVANT = 66,
	MAP_MARMARA = 67,
	MAP_BAHRAIN = 68,
	MAP_BOTSWANAN_HAVEN = 69,
	MAP_SCYTHIAN_RIVULET = 70,
	MAP_AMBUSH = 71,
	MAP_ARCTIC_SUMMER = 72,
	MAP_EMPIRE = 73,
	MAP_SCHWARZWALD = 74,
	MAP_RATUMACOS = 75,
	MAP_OASIS_0AD = 76,
	MAP_MEDITERRANEAN = 77,
	MAP_ELEPHANTINE = 78,
	MAP_ISLAND_STRONGHOLD = 79,
	MAP_POMPEII = 80,
	MAP_LATIUM = 81,
	MAP_UNKNOWN = 82,
	MAP_SURVIVAL_OF_THE_FITTEST = 83,
	MAP_LOWER_NUBIA = 84,
	MAP_MIGRATION = 85,
	MAP_NGORONGORO = 86,
	MAP_RIVER_ARCHIPELAGO = 87,
	MAP_LAND_GRAB = 88,
	MAP_THE_NILE = 89,
	MAP_CONTINENT = 90,
	MAP_FOOTHILLS = 91,
	MAP_COAST_RANGE = 92,
	MAP_WALL_DEMO = 93, // 0.28's demo of rmgen-common wall_builder.js (keyword "demo")
	MAP_TYPE_COUNT = 94
};

// 0 A.D. Team Placement (gamesettings/attributes/TeamPlacement.js): the
// starting-position pattern for random maps whose JSON lists TeamPlacements
// (rmgen-common/player.js playerPlacementByPattern). Random lets the map pick.
enum TeamPlacement : uint8_t {
	TEAM_PLACEMENT_RANDOM = 0,
	TEAM_PLACEMENT_RADIAL = 1,       // "Circle"
	TEAM_PLACEMENT_LINE = 2,
	TEAM_PLACEMENT_RANDOM_GROUP = 3,
	TEAM_PLACEMENT_STRONGHOLD = 4,
	TEAM_PLACEMENT_RIVER = 5,        // players on two parallel lines across the map
	TEAM_PLACEMENT_COUNT = 6
};

struct MapTypeInfo {
	MapType type;
	const char *id;
	const char *name;
	const char *biome;
	bool is_procedural;
	const char *description;
};

// The Team Placement patterns a random map offers: a bit (1 << TEAM_PLACEMENT_*)
// per pattern its 0.28 JSON lists in PlayerPlacements (circle, river,
// groupedLines, randomGroup, stronghold); 0 for maps without the setting.
inline uint8_t map_team_placements(uint8_t type) {
	constexpr uint8_t C = 1 << TEAM_PLACEMENT_RADIAL, R = 1 << TEAM_PLACEMENT_RIVER, L = 1 << TEAM_PLACEMENT_LINE,
	                  G = 1 << TEAM_PLACEMENT_RANDOM_GROUP, S = 1 << TEAM_PLACEMENT_STRONGHOLD;
	switch (type) {
		case MAP_AFRICAN_PLAINS: case MAP_ALPINE_LAKES: case MAP_AMBUSH: case MAP_ANATOLIAN_PLATEAU: case MAP_ARCHIPELAGO_0AD:
		case MAP_ARCTIC_SUMMER: case MAP_ATLAS_MOUNTAINS: case MAP_BOTSWANAN_HAVEN: case MAP_FRONTIER: case MAP_MAINLAND:
		case MAP_RHINE_MARSHLANDS: case MAP_SAHEL:
			return C | R | L | G | S;
		case MAP_ARDENNES_FOREST: return C | R | L | G;
		case MAP_CANTABRIAN_HIGHLANDS: return C | S;
		case MAP_COAST_RANGE: return C | R | S;
		case MAP_EXTINCT_VOLCANO: return C | R | G;
		case MAP_CONTINENT: case MAP_INDIA: case MAP_MIGRATION: case MAP_PERSIAN_HIGHLANDS: case MAP_VOLCANIC_LANDS:
			return C | R | S;
		case MAP_LAKE: case MAP_NEARESTERN_BADLANDS: return C | R;
		case MAP_FOOTHILLS: case MAP_SYRIA: return C | R | G | S;
		default: return 0;
	}
}
inline bool map_has_team_placement(uint8_t type) { return map_team_placements(type) != 0; }

// 0 A.D. CircularMap: the map is the circle inside the square (LOS and passability end at
// it). Every 0.28 random map is circular except the ones whose JSON says CircularMap false
// (corsica, phoenician_levant, latium, wall_demo); RTS0's own maps (0-10) fill the square.
inline bool map_circular(uint8_t type) {
	if (type <= MAP_RANDOM_MEDITERRANEAN || type >= MAP_TYPE_COUNT) return false;
	switch (type) {
		case MAP_CORSICA: case MAP_PHOENICIAN_LEVANT: case MAP_LATIUM: case MAP_WALL_DEMO: return false;
		default: return true;
	}
}

inline const MapTypeInfo &map_type_info(uint8_t type) {
	static const MapTypeInfo kTable[MAP_TYPE_COUNT] = {
		{MAP_AEGEAN_ARCHIPELAGO, "aegean_archipelago", "Aegean Archipelago", "aegean", true, "Scattered Greek islands surrounded by crystal waters, rich in coastal fishing and maritime routes."},
		{MAP_PELOPONNESE, "peloponnese", "Peloponnese Peninsula", "mediterranean", true, "Vast peninsula with jagged coastlines, defensive hills, and fertile coastal lowlands."},
		{MAP_ACROPOLIS, "acropolis", "Acropolis Heights", "mediterranean", true, "Elevated central plateaus offering formidable defensive high-ground over resource valleys."},
		{MAP_SAHARA, "sahara", "Sahara Dunes", "oasis", true, "Arid desert sands with scarce timber, contested watering holes, and rich mineral outcroppings."},
		{MAP_SPARTAN_VALLEY, "spartan_valley", "Spartan Valley", "alpine", true, "Narrow pass flanked by impassable mountain ranges, ideal for phalanx warfare and tactical choke points."},
		{MAP_RANDOM_CONTINENTAL, "random_continental", "Continental (RMS)", "mediterranean", true, "A grand landmass surrounded by ocean with winding inland rivers and rolling resource woodlands."},
		{MAP_RANDOM_ARCHIPELAGO, "random_archipelago", "Archipelago (RMS)", "aegean", true, "Deep-water island cluster requiring maritime expansion, docks, and naval supremacy."},
		{MAP_RANDOM_OASIS, "random_oasis", "Oasis (RMS)", "oasis", true, "Densely forested desert borders surrounding a lush, resource-heavy central lake basin."},
		{MAP_RANDOM_HIGHLANDS, "random_highlands", "Highlands (RMS)", "alpine", true, "Rugged alpine terrain with sheer mountain cliffs, gorge rivers, and strategic passes."},
		{MAP_RANDOM_VALLEY, "random_valley", "Fertile Valley (RMS)", "latium", true, "A lush river valley bounded by towering mountain ridges with balanced contested resources."},
		{MAP_RANDOM_MEDITERRANEAN, "random_mediterranean", "Mediterranean Sea (RMS)", "mediterranean", true, "A central sea ringed by continuous fertile mainland coasts for all civilizations."},
		{MAP_DANUBIUS, "danubius", "Danubius (RMS)", "temperate", true, "Players start along the banks of the river Danube, where Gallic villages and the ships of Celtic reinforcements attack all of them."},
		{MAP_SAHARAN_OASES, "saharan_oases", "Saharan Oases (RMS)", "oasis", true, "Arid desert with sinusoidal sand dunes, date palm groves, and freshwater oases."},
		{MAP_ANATOLIAN_PLATEAU, "anatolian_plateau", "Anatolian Plateau (RMS)", "steppe", true, "High plateau steppe surrounded by perimeter canyon ravines and rich mineral outcrops."},
		{MAP_JEBEL_BARKAL, "jebel_barkal", "Jebel Barkal (RMS)", "savanna", true, "The Nile and the hill of Jebel Barkal: the walled Kushite city of Napata, guarded by archers and siege, sends patrols and attacks against the players."},
		{MAP_MAINLAND, "mainland", "Mainland (RMS)", "mediterranean", true, "Open rolling land with no water barriers, scattered woods and mines. 0 A.D.'s standard land map."},
		{MAP_ALPINE_LAKES, "alpine_lakes", "Alpine Lakes (RMS)", "alpine", true, "High mountain ridges cradling cold lakes, with valleys between the player bases."},
		{MAP_LAKE, "lake", "Lake (RMS)", "temperate", true, "A great lake fills the middle of the map, ringed by the players' lands."},
		{MAP_GULF_OF_BOTHNIA, "gulf_of_bothnia", "Gulf of Bothnia (RMS)", "alpine", true, "A northern gulf cuts into the land from one edge, players settle around its shores."},
		{MAP_ARDENNES_FOREST, "ardennes_forest", "Ardennes Forest (RMS)", "temperate", true, "Dense hilly forest with clearings for each base; wood everywhere, little open ground."},
		{MAP_NEARESTERN_BADLANDS, "nearestern_badlands", "Near-Eastern Badlands (RMS)", "oasis", true, "Dry eroded badlands split by canyons, with a few oases and rich mines."},
		{MAP_RIVERS, "rivers", "Rivers (RMS)", "temperate", true, "Rivers run from a central lake out between the players, crossed by shallow fords."},
		{MAP_CANYON, "canyon", "Canyon (RMS)", "oasis", true, "Players start around the map in deep canyons."},
		{MAP_CORINTHIAN_ISTHMUS, "corinthian_isthmus", "Corinthian Isthmus (RMS)", "mediterranean", true, "Two lands joined by a narrow isthmus, with the sea on both sides of it."},
		{MAP_BELGIAN_UPLANDS, "belgian_uplands", "Belgian Uplands (RMS)", "temperate", true, "Steep wooded hills and valleys with no water, high ground everywhere."},
		{MAP_LORRAINE_PLAIN, "lorraine_plain", "Lorraine Plain (RMS)", "temperate", true, "A flat plain split by a river with a few fords, players on either bank."},
		{MAP_HYRCANIAN_SHORES, "hyrcanian_shores", "Hyrcanian Shores (RMS)", "temperate", true, "A sea along one edge, a forested coastal strip and wooded highlands behind it."},
		{MAP_NORTHERN_LIGHTS, "northern_lights", "Northern Lights (RMS)", "polar", true, "Snowy land with a frozen sea and ice floes to the north; wood is scarce."},
		{MAP_KERALA, "kerala", "Kerala (RMS)", "india", true, "Players on India's south-western shore, between the sea and the mountains."},
		{MAP_POLAR_SEA, "polar_sea", "Polar Sea (RMS)", "polar", true, "Icy land round a central sea, with small lakes and hardly any trees."},
		{MAP_SAHEL, "sahel", "Sahel (RMS)", "savanna", true, "Open, nearly flat savanna with a few water holes; plenty of food and mines, little wood."},
		{MAP_AFRICAN_PLAINS, "african_plains", "African Plains (RMS)", "savanna", true, "Lush plains cluttered with watering holes and hills, teeming with wildlife."},
		{MAP_ALPINE_VALLEY, "alpine_valley", "Alpine Valley (RMS)", "alpine", true, "Deep valleys for each player, walled off by high mountain ranges with passes."},
		{MAP_SYRIA, "syria", "Syria (RMS)", "desert", true, "Dry plains with slightly rolling highlands."},
		{MAP_CANTABRIAN_HIGHLANDS, "cantabrian_highlands", "Cantabrian Highlands (RMS)", "temperate", true, "Each player starts on a hill ringed by steep cliffs, with one ramp down."},
		{MAP_PYRENEAN_SIERRA, "pyrenean_sierra", "Pyrenean Sierra (RMS)", "alpine", true, "High mountains with two passes separate the enemies."},
		{MAP_SAHEL_WATERING_HOLES, "sahel_watering_holes", "Sahel Watering Holes (RMS)", "savanna", true, "Lines of water with shallows run between the players across the savanna."},
		{MAP_PERSIAN_HIGHLANDS, "persian_highlands", "Persian Highlands (RMS)", "persia", true, "A dry sunken plateau rich in minerals, ringed by rocky hills."},
		{MAP_VOLCANIC_LANDS, "volcanic_lands", "Volcanic Lands (RMS)", "volcanic", true, "A charred, dead land where players start around a smoking volcano."},
		{MAP_LIONS_DEN, "lions_den", "Lion's Den (RMS)", "steppe", true, "High cliffs protect each player's den; ramps lead down to a sunken valley and expansions."},
		{MAP_HELLS_PASS, "hells_pass", "Hell's Pass (RMS)", "alpine", true, "Steep mountain spines between the players leave narrow passes in the centre and along the edge."},
		{MAP_DEEP_FOREST, "deep_forest", "Deep Forest (RMS)", "temperate", true, "A deep dark forest in Germania, crossed by paths between the bases and a central hill."},
		{MAP_INDIA, "india", "India (RMS)", "india", true, "Parched central India before the monsoon; only one large, half dried-up lake remains."},
		{MAP_GUADALQUIVIR_RIVER, "guadalquivir_river", "Guadalquivir River (RMS)", "mediterranean", true, "Players on the Mediterranean shore, split by a river with three shallow crossings."},
		{MAP_FIELDS_OF_MEROE, "fields_of_meroe", "Fields of Meroe (RMS)", "savanna", true, "Kush's heartland by the Nile, crossed at two cataracts, with dunes and acacia groves."},
		{MAP_GEAR, "gear", "Gear (RMS)", "temperate", true, "A land with waterways laid out like a spider web round a central lake and island."},
		{MAP_ATLAS_MOUNTAINS, "atlas_mountains", "Atlas Mountains (RMS)", "desert", true, "A rugged North African land with little room for buildings and scarce wood."},
		{MAP_FRONTIER, "frontier", "Frontier (RMS)", "temperate", true, "A wild, unknown landscape of random hills, plateaus and lakes awaits rival explorers."},
		{MAP_WILD_LAKE, "wild_lake", "Wild Lake (RMS)", "temperate", true, "Rough wild terrain round a lake, with about a fifth of the land under water."},
		{MAP_STRONGHOLD, "stronghold", "Stronghold (RMS)", "temperate", true, "Teams start with nearly adjacent civic centres and must rely on each other to defend their rear."},
		{MAP_CALEDONIAN_MEADOWS, "caledonian_meadows", "Caledonian Meadows (RMS)", "alpine", true, "Fertile meadows surrounded by harsh terrain and snowmelt rivers; not always balanced."},
		{MAP_RHINE_MARSHLANDS, "rhine_marshlands", "Rhine Marshlands (RMS)", "temperate", true, "Shallow, passable wetlands with little room for building in the lowlands of the Rhine."},
		{MAP_RED_SEA, "red_sea", "Red Sea (RMS)", "desert", true, "The Red Sea between Africa and Arabia, painted from a real elevation map."},
		{MAP_HELLAS, "hellas", "Hellas (RMS)", "mediterranean", true, "A random corner of Greece and the Aegean, painted from a real elevation map."},
		{MAP_ENGLISH_CHANNEL, "english_channel", "English Channel (RMS)", "temperate", true, "Players start on both sides of the English Channel; crossing it takes ships."},
		{MAP_DODECANESE, "dodecanese", "Dodecanese (RMS)", "mediterranean", true, "A group of islands in the Aegean, a few joined by bridges; the rest need ships."},
		{MAP_FORTRESS, "fortress", "Fortress (RMS)", "temperate", true, "Players start inside walled fortresses with a market, barracks, stable and temple."},
		{MAP_FLOOD, "flood", "Flood (RMS)", "temperate", true, "A great flood moves across the valley. The home islands go under, so evacuate your units by ship to the mainland."},
		{MAP_EXTINCT_VOLCANO, "extinct_volcano", "Extinct Volcano (RMS)", "volcanic", true, "Rains raise the water from the 25th minute until only the hills and the great cone stay dry. Gaia towers guard the lowlands."},
		{MAP_AEGEAN_SEA, "aegean_sea", "Aegean Sea (RMS)", "aegean", true, "Players start on two sides of a sea with scattered islands."},
		{MAP_ARCHIPELAGO_0AD, "archipelago_0ad", "Archipelago, 0 A.D. (RMS)", "temperate", true, "A maze of islands of different sizes and shapes."},
		{MAP_ISLANDS, "islands", "Islands (RMS)", "temperate", true, "Players start in small islands while there are many others around."},
		{MAP_CYCLADIC_ARCHIPELAGO, "cycladic_archipelago", "Cycladic Archipelago (RMS)", "aegean", true, "Each player starts on an island surrounded by water."},
		{MAP_SNOWFLAKE_SEAROCKS, "snowflake_searocks", "Snowflake Searocks (RMS)", "temperate", true, "Many small islands connected to each other by narrow passages."},
		{MAP_HARBOR, "harbor", "Harbor (RMS)", "temperate", true, "A sheltered harbor for each player, a deep sea beyond, and narrow passages round the cliffs."},
		{MAP_CORSICA, "corsica", "Corsica vs Sardinia (RMS)", "mediterranean", true, "The players start on two opposing islands, both with a very jagged relief that will make landing difficult."},
		{MAP_PHOENICIAN_LEVANT, "phoenician_levant", "Phoenician Levant (RMS)", "mediterranean", true, "Players start in the eastern part of the map while a great sea is located to the west."},
		{MAP_MARMARA, "marmara", "Marmara (RMS)", "aegean", true, "The Propontis (Sea of Marmara) between Thrace and Asia Minor, joined by the Bosporus and the Hellespont; players start on fertile land."},
		{MAP_BAHRAIN, "bahrain", "Bahrain (RMS)", "desert", true, "Bahrain, the pearl island of the Sinus Persicus, stands empty between the coasts of Arabia: will you be its next ruler?"},
		{MAP_BOTSWANAN_HAVEN, "botswanan_haven", "Botswanan Haven (RMS)", "savanna", true, "Botswana in the wet season: herds graze amid tall, lush grasses where lions wait, and crocodiles lurk in the shallow pools."},
		{MAP_SCYTHIAN_RIVULET, "scythian_rivulet", "Scythian Rivulet (RMS)", "polar", true, "Carpeted in snow, a land of wolf packs either side of a frozen rivulet; players must conquer - or be conquered."},
		{MAP_AMBUSH, "ambush", "Ambush (RMS)", "temperate", true, "High bluffs overlook the terrain below; rich resources await on the cliffs, but beware of an ambush."},
		{MAP_ARCTIC_SUMMER, "arctic_summer", "Arctic Summer (RMS)", "polar", true, "Summer in the far north: wolves, deer, hares and muskox roam the plains between hills and lakes."},
		{MAP_EMPIRE, "empire", "Empire (RMS)", "mediterranean", true, "A neighbouring province has pledged allegiance to you: every player starts with two civic centres."},
		{MAP_SCHWARZWALD, "schwarzwald", "Schwarzwald (RMS)", "temperate", true, "A forest heavy map with a lake in the middle and plenty of resources."},
		{MAP_RATUMACOS, "ratumacos", "Ratumacos (RMS)", "alpine", true, "Players start on the banks of the Sequana (Seine) in northern Gaul, near the settlement of Ratumacos."},
		{MAP_OASIS_0AD, "oasis_0ad", "Oasis (0 A.D.) (RMS)", "oasis", true, "Players start around a small oasis in the centre of the map, which holds much of the wood."},
		{MAP_MEDITERRANEAN, "mediterranean_0ad", "Mediterranean (RMS)", "mediterranean", true, "The Mediterranean, almost completely enclosed by land, painted from a real elevation map."},
		{MAP_ELEPHANTINE, "elephantine", "Elephantine (RMS)", "savanna", true, "The fortified island of Elephantine on the Nile at the first cataract, between Egypt and Kush."},
		{MAP_ISLAND_STRONGHOLD, "island_stronghold", "Island Stronghold (RMS)", "temperate", true, "Teams start off with nearly adjacent civic centers on a small island, offering a fortified base from which to expand."},
		{MAP_POMPEII, "pompeii", "Pompeii (RMS)", "volcanic", true, "Pompeii, an ancient Roman coastal city in Campania, buried under ash and pumice when Mount Vesuvius erupted in AD 79."},
		{MAP_LATIUM, "latium", "Latium (RMS)", "latium", true, "The Italian peninsula: the fertile volcanic region of central western Italy where Rome was founded."},
		{MAP_UNKNOWN, "unknown", "Unknown (RMS)", "temperate", true, "The unknown: one of 13 land and naval landscapes, picked at random."},
		{MAP_SURVIVAL_OF_THE_FITTEST, "survivalofthefittest", "Survival of the Fittest (RMS)", "temperate", true, "Protect your base against endless waves of enemies. Use your woman citizen to collect the treasures at the center of the map before others do, and try to build your base up. The last player remaining will be the winner! The AI does not play this map."},
		{MAP_LOWER_NUBIA, "lower_nubia", "Lower Nubia (RMS)", "savanna", true, "Known in the Greco-Roman world as the Triakontaschoinos, Lower Nubia was the area between the first and second cataracts of the Nile and formed the traditional border region between Kush and Egypt. The scorching desert, steep valleys and towering cliffs of the area make this an inhospitable place to live, but the control of this ancient corridor was vital for any power seeking to control the lucrative trade routes on the Nile and affords access to the rich goldfields of the Nubian desert."},
		{MAP_MIGRATION, "migration", "Migration (RMS)", "temperate", true, "Players start on islands towards the outside of the map and must move to take advantage of the middle island before their resources run out."},
		{MAP_NGORONGORO, "ngorongoro", "Ngorongoro (RMS)", "savanna", true, "Ngorongoro Crater is the world's largest intact volcanic caldera and is one of the Seven Natural Wonders of Africa. Due to it's climate, biodiversity and history, Ngorongoro is believed by some to be Africa's Garden of Eden and the birthplace of mankind."},
		{MAP_RIVER_ARCHIPELAGO, "river_archipelago", "River Archipelago (RMS)", "india", true, "Narrow strips of lands are separated by waterways, leaving few and shallow landbridges between them. Tropical wetlands provide plenty of game, but beware of Mauryan tribesmen when exploring the outermost islands."},
		{MAP_LAND_GRAB, "land_grab", "Land Grab (RMS)", "temperate", true, "Players start in small islands in the eastern part of the map. There is a big continent in the west ready for expansion."},
		{MAP_THE_NILE, "the_nile", "The Nile (RMS)", "desert", true, "A calm wide river, representing the Nile River in Egypt, divides the map into western and eastern parts."},
		{MAP_CONTINENT, "continent", "Continent (RMS)", "temperate", true, "All players starts on a continent surrounded by water."},
		{MAP_FOOTHILLS, "foothills", "Foothills (RMS)", "temperate", true, "A land map with rolling hills and no obstructions."},
		{MAP_COAST_RANGE, "coast_range", "Coast Range (RMS)", "temperate", true, "All players starts on a continent surrounded by water. Designed and balanced for 2 teams only."},
		{MAP_WALL_DEMO, "wall_demo", "Wall Demo (RMS)", "temperate", true, "A demonstration of wall placement methods/code in random maps. Giant map size is recommended!"}
	};
	if (type >= MAP_TYPE_COUNT) return kTable[MAP_RANDOM_CONTINENTAL];
	return kTable[type];
}

inline bool is_procedural_map(uint8_t type) {
	return type < MAP_TYPE_COUNT;
}

// Naval maps: bases may sit on separate land, so armies need ships to meet.
inline bool map_is_naval(int type) {
	// Flood (flood.js): each player starts on a home island in a sea.
	// MAP_RANDOM_ARCHIPELAGO is RTS0's own island map: a home island each.
	switch (type) {
		case MAP_ENGLISH_CHANNEL: case MAP_DODECANESE: case MAP_FLOOD: case MAP_RANDOM_ARCHIPELAGO:
		case MAP_AEGEAN_SEA: case MAP_ARCHIPELAGO_0AD: case MAP_ISLANDS: case MAP_CYCLADIC_ARCHIPELAGO:
		case MAP_CORSICA: case MAP_BAHRAIN:
		case MAP_ELEPHANTINE: case MAP_ISLAND_STRONGHOLD: case MAP_UNKNOWN: case MAP_DANUBIUS:
		case MAP_MIGRATION:
		case MAP_RIVER_ARCHIPELAGO:
		case MAP_LAND_GRAB: case MAP_THE_NILE:
			return true;
		default:
			return false;
	}
}

enum UnitType : uint8_t {
	UNIT_VILLAGER = 0,
	UNIT_SWORDSMAN = 1,
	UNIT_ARCHER = 2,
	UNIT_SPEARMAN = 3,
	UNIT_CAVALRY = 4,
	UNIT_TRADER = 5,
	UNIT_CATAPULT = 6,
	UNIT_HERO = 7,
	UNIT_TRIREME = 8,
	UNIT_ELEPHANT = 9,
	UNIT_SIEGE_RAM = 10,
	UNIT_HEALER = 11,
	UNIT_CHARIOT = 12,
	UNIT_WAR_DOG = 13,
	UNIT_BOLT_SHOOTER = 14,
	UNIT_FISHING_BOAT = 15,
	UNIT_MERCHANT_SHIP = 16,
	UNIT_FEMALE_CITIZEN = 17,
	UNIT_CARNYX = 18,
	UNIT_WORKER_ELEPHANT = 19,
	UNIT_ARCHER_MAIDEN = 20,
	UNIT_CAMEL = 21,
	UNIT_SCYTHED_CHARIOT = 22,
	UNIT_MINISTER = 23, // Han Imperial Minister (units/han/support_minister)
	UNIT_CATAFALQUE = 24, // a relic (template_unit_catafalque)
	// A queued technology's ProductionOrder::unit_type, written to saves since v48; no unit has it.
	// Its rows in the per-class tables are empty.
	UNIT_TYPE_TECH = 25,
	UNIT_WAGON = 26, // the Germans' supply wagon (template_unit_support_wagon): a mobile dropsite
	UNIT_TYPE_COUNT = 27,
	UNIT_TYPE_NONE = 255
};

enum UnitAction : uint8_t {
	ACTION_IDLE = 0,
	ACTION_MOVE = 1,
	ACTION_ATTACK = 2,
	ACTION_GATHER = 3,
	ACTION_RETURN_DROPSITE = 4,
	ACTION_BUILD = 5,
	ACTION_GARRISONED = 6,
	ACTION_TRADE = 7,
	ACTION_DYING = 8,
	ACTION_ATTACK_MOVE = 9,
	ACTION_PATROL = 10,
	ACTION_GUARD  = 11,
	ACTION_FLEE   = 12, // 0 A.D. UnitAI INDIVIDUAL.FLEEING: running from attacked_by
	ACTION_HEAL   = 13, // UnitAI INDIVIDUAL.HEAL: walking to or healing target_unit_id
	ACTION_CHEER  = 14, // UnitAI INDIVIDUAL.CHEERING after a kill (attack_timer counts it down)
	ACTION_REPAIR = 15  // UnitAI INDIVIDUAL.REPAIR on a unit (Repairable siege or ship): walking to or repairing target_unit_id
};

enum BuildingType : uint8_t {
	BLD_TOWN_CENTER = 0,
	BLD_HOUSE = 1,
	BLD_BARRACKS = 2,
	BLD_STOREHOUSE = 3,
	BLD_TOWER = 4,
	BLD_FARMSTEAD = 5,
	BLD_MARKET = 6,
	BLD_FORTRESS = 7,
	BLD_GATE = 8,
	BLD_TEMPLE = 9,
	BLD_WONDER = 10,
	BLD_DOCK = 11,
	BLD_WALL_SEGMENT = 12,
	BLD_CORRAL = 13,
	// 0 A.D. generic structures RTS0 lacked (template_structure_*). Appended so
	// existing ids, saves and lockstep commands keep their values.
	BLD_FIELD = 14,           // resource_field
	BLD_STABLE = 15,          // military_stable
	BLD_RANGE = 16,           // military_range
	BLD_FORGE = 17,           // military_forge
	BLD_ARSENAL = 18,         // military_arsenal
	BLD_SENTRY_TOWER = 19,    // defensive_tower_sentry
	BLD_OUTPOST = 20,         // defensive_outpost
	BLD_PALISADE = 21,        // defensive_palisade
	BLD_WALL_TOWER = 22,      // defensive_wall_tower
	BLD_ELEPHANT_STABLE = 23, // military_elephant_stable
	BLD_THEATER = 24,         // special_theater
	BLD_EMBASSY = 25,         // cart/embassy_celtic (template_structure_military_embassy)
	// 0 A.D. civ-specific structures (area D, generated rows in civ_building_data.h).
	// Only the civs in civ_building_civ_mask() may build them.
	BLD_GYMNASIUM = 26,       // athen/gymnasium
	BLD_PRYTANEION = 27,      // athen/prytaneion
	BLD_GEROUSIA = 28,        // spart/gerousia
	BLD_SYSSITON = 29,        // spart/syssiton
	BLD_TACHARA = 30,         // pers/tachara
	BLD_ARMY_CAMP = 31,       // rome/army_camp
	BLD_AMPHITHEATER = 32,    // rome/amphitheater_pompeii
	BLD_LIBRARY = 33,         // mace, ptol, sele library
	BLD_KENNEL = 34,          // brit/kennel
	BLD_ASSEMBLY = 35,        // gaul/assembly
	BLD_MONUMENT = 36,        // iber/monument
	BLD_PALACE = 37,          // maur/palace
	BLD_PILLAR = 38,          // maur/pillar_ashoka
	BLD_PYRAMID_LARGE = 39,   // kush/pyramid_large
	BLD_PYRAMID_SMALL = 40,   // kush/pyramid_small
	BLD_MERCENARY_CAMP = 41,  // ptol/mercenary_camp
	BLD_LIGHTHOUSE = 42,      // ptol/lighthouse
	BLD_MILITARY_COLONY = 43, // ptol, sele military_colony
	BLD_ACADEMY = 44,         // han/academy
	BLD_MINISTRY = 45,        // han/ministry
	BLD_CAMP_BLEMMYE = 46,    // kush/camp_blemmye
	BLD_CAMP_NOBA = 47,       // kush/camp_noba
	BLD_TEMPLE_AMUN = 48,     // kush/temple_amun
	BLD_TEMPLE_VESTA = 49,    // rome/temple_vesta
	BLD_TEMPLE_MARS = 50,     // rome/temple_mars
	BLD_ICE_HOUSE = 51,       // pers/ice_house
	BLD_SUPER_DOCK = 52,      // cart/super_dock (Cothon)
	// Reached only by an Upgrade (civ_building_upgrade_only).
	BLD_TACHARA_FOOD = 53,    // pers/tachara_food
	BLD_TACHARA_WOOD = 54,    // pers/tachara_wood
	BLD_TACHARA_STONE = 55,   // pers/tachara_stone
	BLD_TACHARA_METAL = 56,   // pers/tachara_metal
	BLD_APARTMENT = 57,       // cart/apartment
	BLD_IMPERIAL_COURT = 58,  // han/civil_centre_court (a Civic Center: is_civic_centre)
	BLD_TOWER_GREAT = 59,     // han/defense_tower_great (a defense tower: is_defense_tower)
	BLD_PALISADE_GATE = 60,   // palisades_gate
	BLD_EMBASSY_IBERIAN = 61, // cart/embassy_iberian (BLD_EMBASSY is the Celtic one)
	BLD_EMBASSY_ITALIC = 62,  // cart/embassy_italic
	BLD_SIEGE_WALL = 63,      // rome/wallset_siege: siege_wall_long/medium/short and siege_wall_tower
	BLD_SIEGE_WALL_GATE = 64, // rome/siege_wall_gate (a siege_wall_long Upgrade)
	BLD_CRANNOG = 65,         // brit/crannog (a Civic Center on the shore: is_civic_centre)
	BLD_GREAT_HALL = 66,      // germ/great_hall (Town phase; champions and the ram)
	BLD_WAGON_ENCAMPMENT = 67, // germ/encampment (a Colony that claims no territory; researches Wagon Trains)
	BLD_WAGON_ENCAMPMENT_FORTIFIED = 68, // germ/encampment_fortified (its City-phase Upgrade; also trains the log ram)
	// Gaia structures maps place and nobody builds (mixins/shrine.xml, mixins/trading_post.xml):
	// no Health (captured, never destroyed), no territory; the building's civ is its template's.
	BLD_SHRINE = 69,          // structures/shrine_* (a temple: trains its civ's Elite Healer)
	BLD_TRADING_POST = 70,    // structures/trading_post_* (a market for trade and barter)
	// Structures only maps place (no Builder list names them): map_structure_template.
	BLD_TOWER_BOLT = 71,      // {athen,cart,mace,ptol,rome,spart}/tower_bolt
	BLD_TOWER_ARTILLERY = 72, // {athen,cart,mace,ptol,rome,sele}/tower_artillery
	BLD_STOA = 73,            // {athen,mace,spart}/royal_stoa (template_structure_civic_stoa)
	BLD_HELLENIC_STOA = 74,   // structures/hellenic_stoa
	BLD_INN = 75,             // pers/inn (a house: Population/Bonus 15)
	BLD_TENT = 76,            // rome/tent (a house: Population/Bonus 5)
	BLD_TRIUMPHAL_ARCH = 77,  // rome/arch (not capturable)
	BLD_HALL = 78,            // pers/hall (Gate of All Nations: template_structure_special)
	BLD_WAREHOUSE = 79,       // pers/warehouse (a house: Population/Bonus 5)
	BLD_TAVERN = 80,          // gaul/tavern (a Town house: Population/Bonus 10)
	BLD_TOWER_DOUBLE = 81,    // maur/tower_double (a stone tower: DefaultArrowCount 2)
	BLD_KUSH_SHRINE = 82,     // kush/shrine (a temple that trains and researches nothing)
	BLD_FORTRESS_OLD = 83,    // han/fortress_old (the fortress with the old actor)
	BLD_LAOZI_GATE = 84,      // han/laozigate (not capturable)
	BLD_TEMPLE_ISIS = 85,     // ptol/temple_2 (Temple of Isis: City, one per player, trains the heroes)
	BLD_TYPE_COUNT = 86
};

#include "civ_building_data.h"
#include "territory_data.h"
static_assert(TERRITORY_INFLUENCE_ROWS == BLD_TYPE_COUNT,
	"a new BuildingType needs its TerritoryInfluence row: run tools/extract_0ad_territory.py");
#include "vision_data.h"
static_assert(STRUCTURE_VISION_ROWS == BLD_TYPE_COUNT,
	"a new BuildingType needs its Vision row: run tools/extract_0ad_vision.py");

// The structure whose Researcher list an upgraded form keeps (the Tachara's tribute
// forms research the Tachara's technologies, the apartment the house's, the Han
// Imperial Court the Civic Center's, the Great Archery Tower the defense tower's). The
// maps' Persian inn and Roman tent are houses and their bolt and artillery towers defense
// towers wherever the role matters (building_researches_tech narrows what they research).
inline constexpr uint8_t building_research_base(uint8_t type) {
	return (type >= BLD_TACHARA_FOOD && type <= BLD_TACHARA_METAL) ? (uint8_t)BLD_TACHARA
	       : type == BLD_APARTMENT ? (uint8_t)BLD_HOUSE
	       : (type == BLD_IMPERIAL_COURT || type == BLD_CRANNOG) ? (uint8_t)BLD_TOWN_CENTER
	       : (type == BLD_TOWER_GREAT || type == BLD_TOWER_BOLT || type == BLD_TOWER_ARTILLERY ||
	          type == BLD_TOWER_DOUBLE) ? (uint8_t)BLD_TOWER
	       : (type == BLD_INN || type == BLD_TENT || type == BLD_WAREHOUSE || type == BLD_TAVERN) ? (uint8_t)BLD_HOUSE
	       : type == BLD_FORTRESS_OLD ? (uint8_t)BLD_FORTRESS
	       : (type == BLD_KUSH_SHRINE || type == BLD_TEMPLE_ISIS) ? (uint8_t)BLD_TEMPLE
	       : type == BLD_WAGON_ENCAMPMENT_FORTIFIED ? (uint8_t)BLD_WAGON_ENCAMPMENT
	       : (type == BLD_EMBASSY_IBERIAN || type == BLD_EMBASSY_ITALIC) ? (uint8_t)BLD_EMBASSY : type;
}

// Health disable="" (mixins/shrine.xml, mixins/trading_post.xml): attacks can't damage the
// structure (Attack.js CanAttack needs Health), so it changes hands only by capture.
inline constexpr bool structure_has_health(uint8_t type) {
	return type != BLD_SHRINE && type != BLD_TRADING_POST;
}

// The structures only maps place, by their 0 A.D. template ("structures/rome/tent", or a map's
// short "rome/tower_bolt"), with the template's Identity/Civ: the Gaia shrines and trading posts
// (structures/shrine_*.xml, trading_post_*.xml), the bolt and artillery towers, royal and
// Hellenic stoas, the Persian inn and the Roman tent and arch. CIV_GAIA: a template without a
// civ (the Hellenic stoa). BLD_TYPE_COUNT for others.
struct MapStructureTemplate { const char *stem; uint8_t type; uint8_t civ; };
inline MapStructureTemplate map_structure_template(const char *tpl) {
	static const MapStructureTemplate kRows[] = {
		{"shrine_celtic", BLD_SHRINE, CIV_GAULS}, {"shrine_chinese", BLD_SHRINE, CIV_HAN},
		{"shrine_egyptian", BLD_SHRINE, CIV_PTOLEMIES}, {"shrine_greek_01", BLD_SHRINE, CIV_MACEDONIANS},
		{"shrine_greek_02", BLD_SHRINE, CIV_MACEDONIANS}, {"shrine_italian", BLD_SHRINE, CIV_ROMANS},
		{"shrine_kushite", BLD_SHRINE, CIV_KUSHITES}, {"shrine_persian", BLD_SHRINE, CIV_PERSIANS},
		{"trading_post_celtic", BLD_TRADING_POST, CIV_GAULS}, {"trading_post_desert", BLD_TRADING_POST, CIV_PERSIANS},
		{"athen/tower_bolt", BLD_TOWER_BOLT, CIV_ATHENIANS}, {"cart/tower_bolt", BLD_TOWER_BOLT, CIV_CARTHAGINIANS},
		{"mace/tower_bolt", BLD_TOWER_BOLT, CIV_MACEDONIANS}, {"ptol/tower_bolt", BLD_TOWER_BOLT, CIV_PTOLEMIES},
		{"rome/tower_bolt", BLD_TOWER_BOLT, CIV_ROMANS}, {"spart/tower_bolt", BLD_TOWER_BOLT, CIV_SPARTANS},
		{"athen/tower_artillery", BLD_TOWER_ARTILLERY, CIV_ATHENIANS},
		{"cart/tower_artillery", BLD_TOWER_ARTILLERY, CIV_CARTHAGINIANS},
		{"mace/tower_artillery", BLD_TOWER_ARTILLERY, CIV_MACEDONIANS},
		{"ptol/tower_artillery", BLD_TOWER_ARTILLERY, CIV_PTOLEMIES},
		{"rome/tower_artillery", BLD_TOWER_ARTILLERY, CIV_ROMANS},
		{"sele/tower_artillery", BLD_TOWER_ARTILLERY, CIV_SELEUCIDS},
		{"athen/royal_stoa", BLD_STOA, CIV_ATHENIANS}, {"mace/royal_stoa", BLD_STOA, CIV_MACEDONIANS},
		{"spart/royal_stoa", BLD_STOA, CIV_SPARTANS}, {"hellenic_stoa", BLD_HELLENIC_STOA, CIV_GAIA},
		{"pers/inn", BLD_INN, CIV_PERSIANS}, {"rome/tent", BLD_TENT, CIV_ROMANS},
		{"rome/arch", BLD_TRIUMPHAL_ARCH, CIV_ROMANS},
		{"pers/hall", BLD_HALL, CIV_PERSIANS}, {"pers/warehouse", BLD_WAREHOUSE, CIV_PERSIANS},
		{"gaul/tavern", BLD_TAVERN, CIV_GAULS}, {"maur/tower_double", BLD_TOWER_DOUBLE, CIV_MAURYANS},
		{"kush/shrine", BLD_KUSH_SHRINE, CIV_KUSHITES}, {"han/fortress_old", BLD_FORTRESS_OLD, CIV_HAN},
		{"han/laozigate", BLD_LAOZI_GATE, CIV_HAN}, {"ptol/temple_2", BLD_TEMPLE_ISIS, CIV_PTOLEMIES},
	};
	const char *stem = std::strncmp(tpl, "structures/", 11) == 0 ? tpl + 11 : tpl;
	for (const auto &r : kRows)
		if (std::strcmp(r.stem, stem) == 0) return r;
	return {"", BLD_TYPE_COUNT, CIV_GAIA};
}

// 0 A.D. Market component with TradeType land and class Barter: the market and the Gaia
// trading post (mixins/trading_post.xml keeps template_structure_economic_market's).
inline constexpr bool is_land_market(uint8_t type) {
	return type == BLD_MARKET || type == BLD_TRADING_POST;
}

// 0 A.D. class CivCentre: the Civic Center and its Han Imperial Court form (a
// territory root, a dropsite for every resource, a conquest-critical base).
inline constexpr bool is_civic_centre(uint8_t type) {
	return type == BLD_TOWN_CENTER || type == BLD_IMPERIAL_COURT || type == BLD_CRANNOG;
}
// 0 A.D. class ConquestCritical (template_structure_civic, _military and _wonder, less the
// stoa and the forge): the structures that keep a player in a Conquest match. Towers, walls,
// palisades, economic and other special structures don't.
inline constexpr bool is_conquest_critical(uint8_t type) {
	switch (type) {
		case BLD_STOREHOUSE: case BLD_TOWER: case BLD_FARMSTEAD: case BLD_MARKET: case BLD_GATE:
		case BLD_WALL_SEGMENT: case BLD_CORRAL: case BLD_FIELD: case BLD_FORGE: case BLD_SENTRY_TOWER:
		case BLD_OUTPOST: case BLD_PALISADE: case BLD_WALL_TOWER: case BLD_THEATER: case BLD_AMPHITHEATER:
		case BLD_LIBRARY: case BLD_MONUMENT: case BLD_PILLAR: case BLD_PYRAMID_LARGE: case BLD_PYRAMID_SMALL:
		case BLD_LIGHTHOUSE: case BLD_ICE_HOUSE: case BLD_TOWER_GREAT: case BLD_PALISADE_GATE:
		case BLD_SIEGE_WALL: case BLD_SIEGE_WALL_GATE: case BLD_TRADING_POST: case BLD_TOWER_BOLT:
		case BLD_TOWER_ARTILLERY: case BLD_STOA: case BLD_TRIUMPHAL_ARCH: case BLD_TOWER_DOUBLE:
			return false;
		default:
			return type < BLD_TYPE_COUNT;
	}
}
// 0 A.D. units without ConquestCritical: catafalques, traders, healers, fishing and merchant ships.
inline constexpr bool is_conquest_critical_unit(uint8_t utype) {
	return utype != UNIT_CATAFALQUE && utype != UNIT_TRADER && utype != UNIT_HEALER &&
	       utype != UNIT_FISHING_BOAT && utype != UNIT_MERCHANT_SHIP;
}
// The defense tower, its Han Great Archery Tower form and the maps' Mauryan rampart tower (StoneTower).
inline constexpr bool is_defense_tower(uint8_t type) {
	return type == BLD_TOWER || type == BLD_TOWER_GREAT || type == BLD_TOWER_DOUBLE;
}
// TerritoryInfluence/Root: civic centres, wonders and a few civ structures (the
// military colony, palace, tachara, ministry, Cothon); territory connects through them.
inline bool is_territory_root(uint8_t type) { return territory_influence_info(type).root; }

// Structures that claim territory (TerritoryInfluence); docks, fields, outposts,
// palisades and camps claim none.
inline bool has_territory_influence(uint8_t type) { return territory_influence_info(type).radius > 0.0f; }

enum ResourceType : uint8_t {
	RES_WOOD = 0,
	RES_GOLD = 1,
	RES_STONE = 2,
	RES_FOOD = 3,
	RES_COUNT = 4
};

// Food supply flavour for ResourceGatherer gather_* clips. Wood/gold/stone
// ignore this; food defaults to fruit (berry bush) unless marked meat/grain.
enum ResourceSubtype : uint8_t {
	RES_SUB_NONE = 0,
	RES_SUB_MEAT = 1,
	RES_SUB_GRAIN = 2,
	RES_SUB_FISH = 3, // food.fish: only fishing boats gather it
	RES_SUB_RUINS = 4 // stone.ruins / metal.ruins (template_gaia_ruins): gather_class_ruins_rate
};

// The plant a tree or fruit supply shows: 0 A.D.'s gaia/tree/* and gaia/fruit/*
// templates. Kinds 1-26 are families sharing one species model (assets/models/vegetation);
// from FLORA_TEMPLATE_FIRST on, each kind is one template with its own model (flora_data.h).
// Only the drawing and the inspector read it; FLORA_DEFAULT lets the viewer pick from the
// map's biome.
enum FloraKind : uint8_t {
	FLORA_DEFAULT = 0,
	FLORA_BROADLEAF = 1, // elm, tamarix and other leafy trees without their own model
	FLORA_PINE = 2,      // pine, cedar, larch, spruce, yew
	FLORA_PALM = 3,      // date palms
	FLORA_OAK = 4,
	FLORA_BAOBAB = 5,
	FLORA_ACACIA = 6,
	FLORA_BAMBOO = 7,
	FLORA_BUSH = 8,      // gaia/tree/bush_* and saplings
	FLORA_BERRY = 9,     // gaia/fruit/berry_*, grapes
	FLORA_APPLE = 10,    // gaia/fruit/apple, fig
	FLORA_DATE = 11,     // gaia/fruit/date
	FLORA_CAROB = 12,
	FLORA_POPLAR = 13,
	FLORA_FAN_PALM = 14,    // medit_fan_palm
	FLORA_BEECH = 15,
	FLORA_DEAD = 16,        // dead, oak_dead, poplar_dead
	FLORA_CYPRESS = 17,
	FLORA_ALEPPO_PINE = 18, // aleppo_pine, pine_maritime, pine_black
	FLORA_FIR = 19,         // fir, fir_winter, pine_w
	FLORA_TROPIC_BUSH = 20,
	FLORA_OLIVE = 21,
	FLORA_BANYAN = 22,      // banyan, strangler
	FLORA_TROPIC_PALM = 23, // palm_tropic, palm_palmyra, palm_doum, senegal_date_palm
	FLORA_MAPLE = 24,
	FLORA_JUNIPER = 25,
	FLORA_TEAK = 26,        // teak, toona
	FLORA_FAMILY_COUNT = 27
};

constexpr int FLORA_COUNT = FLORA_TEMPLATE_FIRST + FLORA_TEMPLATE_COUNT;
static_assert(FLORA_TEMPLATE_FIRST == FLORA_FAMILY_COUNT, "template kinds follow the families");
static_assert(FLORA_COUNT <= 255, "FloraKind is a byte");

// The family (0-26) a kind counts as for game logic: itself, or a template kind's family.
inline uint8_t flora_family(uint8_t kind) {
	if (kind < FLORA_TEMPLATE_FIRST) return kind;
	return kind < FLORA_COUNT ? flora_template_info(kind - FLORA_TEMPLATE_FIRST).family : (uint8_t)FLORA_DEFAULT;
}

// The FloraKind a map JSON "resources" type names ("pine", "berry", ... or a template's
// "tree/elm", "fruit/fig"; tools/port_0ad_maps.py), or FLORA_COUNT for a name that is not a
// plant ("tree" and "food" are FLORA_DEFAULT).
inline uint8_t flora_kind_by_name(const char *name) {
	static const char *const kNames[FLORA_FAMILY_COUNT] = {
		"tree", "broadleaf", "pine", "palm", "oak", "baobab", "acacia", "bamboo", "bush", "berry", "apple", "date",
		"carob", "poplar", "fan_palm", "beech", "dead", "cypress", "aleppo_pine", "fir", "tropic_bush", "olive",
		"banyan", "tropic_palm", "maple", "juniper", "teak"};
	for (int k = 0; k < FLORA_FAMILY_COUNT; ++k)
		if (std::strcmp(name, kNames[k]) == 0) return (uint8_t)k;
	for (int k = 0; k < FLORA_TEMPLATE_COUNT; ++k)
		if (std::strcmp(name, flora_template_info(k).name) == 0) return (uint8_t)(FLORA_TEMPLATE_FIRST + k);
	if (std::strcmp(name, "food") == 0 || std::strcmp(name, "fruit") == 0) return FLORA_DEFAULT;
	return FLORA_COUNT;
}
inline bool flora_is_fruit(uint8_t kind) {
	if (kind >= FLORA_TEMPLATE_FIRST) return kind < FLORA_COUNT && flora_template_info(kind - FLORA_TEMPLATE_FIRST).fruit;
	return kind == FLORA_BERRY || kind == FLORA_APPLE || kind == FLORA_DATE;
}

// The width (= depth) in metres of a tree or fruit supply's Obstruction/Static: its
// template's (flora_data.h), else its family's, else template_gaia_fruit's 2.5 for food
// and template_gaia_tree's 1.6 for wood.
inline float flora_obstruction_m(uint8_t kind, bool food) {
	float w = kind >= FLORA_TEMPLATE_FIRST ? (kind < FLORA_COUNT ? flora_template_info(kind - FLORA_TEMPLATE_FIRST).obstruction : 0.0f)
	                                       : flora_family_obstruction(kind);
	return w > 0.0f ? w : food ? 2.5f : 1.6f;
}

// 0 A.D. Gaia ResourceSupply Max amounts that random maps place: gaia/tree/* hold
// 200 wood, template_gaia_ore and template_gaia_rock 1000 metal or stone, and their
// _large variants 5000. (Ported JSON maps read each entity's own template instead.)
constexpr int32_t TREE_WOOD_SUPPLY = 200;
constexpr int32_t MINE_SUPPLY = 1000;
constexpr int32_t LARGE_MINE_SUPPLY = 5000;

// 0 A.D. template_gaia_fish.xml ResourceSupply: 1000 food, at most 4 gatherers,
// and +1 food every 2 s while nobody gathers it (Change/Spawn, notGathered) up to Max.
constexpr int32_t FISH_FOOD_SUPPLY = 1000;
constexpr int FISH_MAX_GATHERERS = 4;
constexpr float FISH_REGROW_INTERVAL_S = 2.0f;
constexpr float FRUIT_REGROW_INTERVAL_S = 6.0f; // template_gaia_fruit Regrowth Interval 6000
// The ResourceSupply Change interval a supply runs (fruit Regrowth, fish Spawn), -1 for none.
inline float regrow_interval(uint8_t type, uint8_t subtype) {
	if (type != RES_FOOD) return -1.0f;
	return subtype == RES_SUB_FISH ? FISH_REGROW_INTERVAL_S : subtype == RES_SUB_NONE ? FRUIT_REGROW_INTERVAL_S : -1.0f;
}
// Fish footprint radius (10 m) plus a fishing boat's ResourceGatherer MaxDistance (6 m), in tiles.
constexpr float FISH_GATHER_REACH = 4.0f;

// 0 A.D. UnitAI.js g_Stances. Values are saved and sent over the wire; 3 was an
// invented "hold ground" and is now 0 A.D.'s "violent".
enum UnitStance : uint8_t {
	STANCE_AGGRESSIVE = 0,  // chase visible enemies until they leave vision
	STANCE_DEFENSIVE = 1,   // chase only near the held position (respondHoldGround)
	STANCE_STAND_GROUND = 2,// attack what is in range, never move
	STANCE_VIOLENT = 3,     // aggressive, and answers attackers even on forced orders
	STANCE_PASSIVE = 4      // never attack, flee when attacked (respondFlee)
};

// The order a unit is carrying out (0 A.D. UnitAI order types). action_state says
// what the unit is doing this tick; when an automatic fight or a flight ends the
// unit goes back to this order, and when the order is done it takes the next one
// from its queue (UnitAI FinishOrder).
enum UnitOrderKind : uint8_t {
	ORDER_NONE = 0,
	ORDER_WALK = 1,           // target x/z
	ORDER_WALK_AND_FIGHT = 2, // attack-move to x/z
	ORDER_PATROL = 3,         // between x2/z2 and x/z
	ORDER_ATTACK = 4,         // target unit
	ORDER_ATTACK_BUILDING = 5,// target building (capture or damage)
	ORDER_GATHER = 6,         // target resource
	ORDER_BUILD = 7,          // target building (build or repair), or a Repairable unit
	ORDER_GARRISON = 8,       // target building
	ORDER_GUARD = 9,          // target unit or structure (Guard.js)
	ORDER_HEAL = 10,          // target unit (UnitAI Heal)
	ORDER_TRADE = 11,         // target = first market, x2 holds the second market's id bits (UnitAI Trade)
	ORDER_RETURN_RESOURCE = 12, // target = the dropsite (UnitAI ReturnResource)
	ORDER_COLLECT_TREASURE = 13, // target treasure (UnitAI CollectTreasure)
	ORDER_CAPTURE_RELIC = 14, // target catafalque unit (UnitAI Attack with the Capture attack)
	ORDER_KIND_COUNT = 15
};

enum UnitOrderFlag : uint8_t {
	ORDER_FLAG_FORCE = 1,        // given by a player (order.data.force): walks ignore enemies, chases ignore the stance leash
	ORDER_FLAG_ALLOW_CAPTURE = 2,// Attack.js allowCapture: capture structures instead of damaging them
	ORDER_FLAG_UNITS_ONLY = 4,   // attack-move targetClasses "Unit" (session.attackmoveUnit)
	ORDER_FLAG_INTERRUPTED = 8,  // fighting or fleeing; go back to the order afterwards
	ORDER_FLAG_CHEER = 16,       // killed its target (UnitAI shouldCheer)
	// ORDER_BUILD only (bit 5 holds a gather order's subtype): a forced build that reached its
	// site (REPAIRING enter, order.data.autoharvest), so ConstructionFinished farms the field
	// or gathers near the new dropsite.
	ORDER_FLAG_AUTOHARVEST = 32,
	// ORDER_GARRISON only: occupy a turret point of the holder (Commands.js "occupy-turret").
	ORDER_FLAG_TURRET = 32
};
// A gather order keeps its resource's subtype (ResourceSubtype) in flag bits 5-7, so the
// unit can look for the same kind once the resource is gone (UnitAI GATHER FINDINGNEWTARGET).
enum : uint8_t {
	ORDER_FLAG_SUB_SHIFT = 5,
	ORDER_FLAG_SUB_MASK = 0xE0
};
// An attack-move keeps the structures it walks past in bits 5-7 (attackMove targetClasses
// "avoid", as Petra's attackPlan.js passes them): which defences, and economic structures.
enum : uint8_t {
	ORDER_AVOID_SHIFT = 5,
	ORDER_AVOID_MASK = 0x60,
	ORDER_AVOID_WALLS_FORTRESS = 1, // Palisade, Wall, Fortress
	ORDER_AVOID_WALLS_TOWERS = 2,   // Palisade, Wall, Tower, Fortress
	ORDER_AVOID_WALLS = 3,          // Palisade, Wall
	ORDER_AVOID_ECONOMY = 0x80      // House, Storehouse, Farmstead, Field, Forge
};

// One order: the unit's current one (in the SoA) or a Shift-queued one. No padding.
struct UnitOrder {
	uint8_t kind = ORDER_NONE;
	uint8_t flags = 0;
	uint8_t res_type = 0; // gather: the resource's generic type + 1 (0 unknown), for GatherNearPosition
	uint8_t pad = 0;
	uint32_t target = 0;
	float x = 0.0f, z = 0.0f;   // destination
	float x2 = 0.0f, z2 = 0.0f; // patrol start
};
static_assert(sizeof(UnitOrder) == 24, "UnitOrder is saved raw");

// 0 A.D. template_unit.xml UnitAI/FleeDistance (12 m) and the 64 m search of
// UnitAI FindNearbyResource / FindNearbyFoundation, in RTS0 units (metres x 0.28).
inline constexpr float FLEE_DISTANCE = 12.0f * 0.28f;
inline constexpr float NEARBY_SEARCH_RANGE = 64.0f * 0.28f;
// Attack.js Capture: template_unit_*.xml Capture MaxRange 4, RepeatTime 1000. 0 A.D.
// measures edge to edge; RTS0 melee reach is ~0.28 x R plus the radii, so Capture
// MaxRange 4 is the spearman's reach, as in 0 A.D. (Melee MaxRange 4): 2.4.
inline constexpr float CAPTURE_RANGE = 2.4f;
inline constexpr float CAPTURE_REPEAT_S = 1.0f;
// template_unit_catafalque.xml Capturable: CapturePoints 250, RegenRate 10 a second.
inline constexpr int RELIC_CAPTURE_POINTS = 250;
inline constexpr int RELIC_REGEN_RATE = 10;
// template_unit_support_healer.xml Heal (Health 5 every Interval 2000 ms) and Heal.js
// GetTimers (prepare 1000 ms); template_unit.xml UnitAI CheeringTime 2800 ms and
// UnitAI notifyToCheerInRange 30; GarrisonHolder LoadingRange (1-2 m past the footprint).
inline constexpr float HEAL_HP = 5.0f;
// 0.28 template_unit_hero_healer.xml Heal: Health 15, Range 20 (m, before COMBAT_DISTANCE_SCALE).
inline constexpr float HERO_HEAL_HP = 15.0f;
inline constexpr float HERO_HEAL_RANGE = 20.0f;
// 0.28 template_unit_champion_support_healer.xml Heal: Health 10, Range 20 (the Seeress).
inline constexpr float CHAMPION_HEAL_HP = 10.0f;
inline constexpr float CHAMPION_HEAL_RANGE = 20.0f;
inline constexpr float HEAL_INTERVAL_S = 2.0f;
inline constexpr float HEAL_PREPARE_S = 1.0f;
inline constexpr float CHEER_TIME_S = 2.8f;
inline constexpr float CHEER_NOTIFY_RANGE = 30.0f * 0.28f; // metres x 0.28
// AlertRaiser.js per structure template: the classes it alerts (FemaleCitizen, or
// Trader+!Ship for the market) and its ranges in RTS0 units (0 A.D. metres x 0.28;
// a negative range reaches the whole map).
struct AlertRaiserInfo {
	bool traders = false;
	float raise_range = 0.0f, end_range = 0.0f, search_range = 0.0f;
};
inline bool building_alert_raiser(uint8_t type, AlertRaiserInfo &out) {
	constexpr float S = 0.28f;
	switch (building_research_base(type)) {
		case BLD_TOWN_CENTER: case BLD_MILITARY_COLONY: case BLD_WAGON_ENCAMPMENT: out = {false, 120 * S, 180 * S, 100 * S}; return true;
		case BLD_STOREHOUSE: case BLD_FARMSTEAD: out = {false, 50 * S, 100 * S, 100 * S}; return true;
		case BLD_MARKET: out = {true, -1.0f, -1.0f, 100 * S}; return true;
		default: return false;
	}
}

// GarrisonHolder LoadingRange of a structure, 0 A.D. metres to RTS0 (x0.28), counted from
// its obstruction: 1 m for civic centres (military colonies, wagon encampments and the
// crannog too), houses and apartments, and Han's ministry, farmstead and storehouse;
// 6 m for the fortress (and Han's fortress_old) and the Roman army camp; 4 m for the corral; 2 m elsewhere
// (template_structure_*.xml and the civ templates of 0.28).
inline constexpr float structure_loading_range(uint8_t type, uint8_t civ) {
	switch (type) {
		case BLD_TOWN_CENTER: case BLD_IMPERIAL_COURT: case BLD_CRANNOG: case BLD_MILITARY_COLONY:
		case BLD_WAGON_ENCAMPMENT: case BLD_WAGON_ENCAMPMENT_FORTIFIED:
		case BLD_HOUSE: case BLD_APARTMENT: case BLD_MINISTRY:
			return 1.0f * 0.28f;
		case BLD_FARMSTEAD: case BLD_STOREHOUSE:
			return (civ == CIV_HAN ? 1.0f : 2.0f) * 0.28f;
		case BLD_FORTRESS: case BLD_FORTRESS_OLD: case BLD_ARMY_CAMP:
			return 6.0f * 0.28f;
		case BLD_CORRAL:
			return 4.0f * 0.28f;
		default:
			return 2.0f * 0.28f;
	}
}
// template_unit_siege_ram.xml and template_unit_siege_tower.xml LoadingRange, 2 m.
inline constexpr float SIEGE_LOADING_RANGE = 2.0f * 0.28f;
// Attack.js Ranged MinRange as a share of MaxRange (0.28): template_unit_siege_stonethrower.xml
// 26 of 85, template_unit_siege_stonethrower_onager.xml 20 of 60, template_unit_siege_boltshooter.xml
// 15 of 80; template_unit_siege_tower.xml has none. Scaled to each unit's own range.
inline float unit_min_range_ratio(uint8_t type) {
	switch (type) {
		case UNIT_CATAPULT: return 26.0f / 85.0f;
		case UNIT_BOLT_SHOOTER: return 15.0f / 80.0f;
		default: return 0.0f;
	}
}

// 0 A.D. formation templates (special/formations/*.xml). Ids 0-12 keep their old values
// (saves and lockstep cmd 8 carry them); 13-16 came with the Formation.js port.
// formation.h has the templates, which civs and units may use each, and the layout code.
enum UnitFormation : uint8_t {
	FORMATION_NONE = 0,          // null
	FORMATION_LINE = 1,          // line_closed ("Close Order")
	FORMATION_BOX = 2,           // box
	FORMATION_COLUMN = 3,        // column_closed ("Forced March")
	FORMATION_PHALANX = 4,       // phalanx (hoplites; athen, cart, mace, pers, ptol, sele, spart)
	FORMATION_WEDGE = 5,         // wedge (cavalry)
	FORMATION_SKIRMISH = 6,      // skirmish (ranged)
	FORMATION_LINE_OPEN = 7,     // line_open ("Open Order")
	FORMATION_COLUMN_OPEN = 8,   // column_open
	FORMATION_FLANK = 9,         // flank
	FORMATION_BATTLE_LINE = 10,  // battle_line
	FORMATION_SHIPS_LINE = 11,   // ships/line_staggered ("Fleet Battle Line")
	FORMATION_SHIPS_COLUMN = 12, // ships/column ("Maneuvering Column")
	FORMATION_SHIPS_FLANK = 13,  // ships/flank ("Flanking Formation")
	FORMATION_SYNTAGMA = 14,     // syntagma (pikemen; kush, mace, ptol, sele)
	FORMATION_TESTUDO = 15,      // testudo (melee infantry; rome)
	FORMATION_ANTI_CAVALRY = 16, // anti_cavalry (han and roman spearmen; han, rome)
	FORMATION_COUNT = 17,
	FORMATION_SCATTER = FORMATION_SKIRMISH // old name
};

enum StatusEffect : uint8_t {
	EFFECT_NONE = 0,
	EFFECT_POISON = 1,
	EFFECT_BURNING = 2,
	// Bits 4-32 were RTS0's invented snare, knockback, slow and bleed; tick_status_effects
	// clears them from saves made before they went.
};

enum VictoryCondition : uint8_t {
	VICTORY_CONQUEST = 0,
	VICTORY_WONDER = 1,
	VICTORY_RELIC = 2,
	VICTORY_ENDLESS = 3,
	VICTORY_CONQUEST_UNITS = 4, // 0 A.D. "Conquest Units": kill every enemy unit
	VICTORY_CONQUEST_STRUCTURES = 5, // 0 A.D. "Conquest Structures": raze every enemy structure
	VICTORY_REGICIDE = 6, // 0 A.D. "Regicide": each player starts with a hero; lose it and you lose
	VICTORY_COUNT
};

// 0 A.D. Game Setup combines victory conditions (VictoryConditions.js). A combination that
// isn't one of the codes above is stored as VICTORY_COMBINED | mask, so codes 0..6 (and old
// saves and replays) keep their meaning.
enum VictoryKind : uint8_t {
	VK_CONQUEST = 1 << 0,
	VK_CONQUEST_UNITS = 1 << 1,
	VK_CONQUEST_STRUCTURES = 1 << 2,
	VK_CONQUEST_CIVIC_CENTRES = 1 << 3,
	VK_WONDER = 1 << 4,
	VK_RELIC = 1 << 5,
	VK_REGICIDE = 1 << 6,
	VK_ALL = 0x7F,
};
constexpr uint8_t VICTORY_COMBINED = 0x80;
// The conditions each code stands for. Wonder, Relic and Regicide always played with
// Conquest on; Endless is none.
inline constexpr uint8_t victory_mask(uint8_t code) {
	switch (code) {
		case VICTORY_CONQUEST: return VK_CONQUEST;
		case VICTORY_WONDER: return VK_CONQUEST | VK_WONDER;
		case VICTORY_RELIC: return VK_CONQUEST | VK_RELIC;
		case VICTORY_ENDLESS: return 0;
		case VICTORY_CONQUEST_UNITS: return VK_CONQUEST_UNITS;
		case VICTORY_CONQUEST_STRUCTURES: return VK_CONQUEST_STRUCTURES;
		case VICTORY_REGICIDE: return VK_CONQUEST | VK_REGICIDE;
		default: return (code & VICTORY_COMBINED) ? (uint8_t)(code & VK_ALL) : (uint8_t)VK_CONQUEST;
	}
}
// The stored code for a set of conditions: the single code when one matches, else combined.
inline constexpr uint8_t victory_code(uint8_t mask) {
	mask &= VK_ALL;
	for (uint8_t c = 0; c < VICTORY_COUNT; ++c)
		if (victory_mask(c) == mask) return c;
	return (uint8_t)(VICTORY_COMBINED | mask);
}
inline constexpr bool valid_victory_code(uint8_t code) {
	return code < VICTORY_COUNT || (code & VICTORY_COMBINED);
}
// 0.28 victory_conditions/*.json DisabledWhenChecked: Conquest excludes the partial
// conquests, and each partial conquest excludes Conquest and the other "all" conquest.
inline constexpr uint8_t victory_excludes(uint8_t kind) {
	switch (kind) {
		case VK_CONQUEST: return VK_CONQUEST_UNITS | VK_CONQUEST_STRUCTURES | VK_CONQUEST_CIVIC_CENTRES;
		case VK_CONQUEST_UNITS: return VK_CONQUEST;
		case VK_CONQUEST_STRUCTURES: return VK_CONQUEST | VK_CONQUEST_CIVIC_CENTRES;
		case VK_CONQUEST_CIVIC_CENTRES: return VK_CONQUEST | VK_CONQUEST_STRUCTURES;
		default: return 0;
	}
}

struct BattleAlert {
	enum Type : uint8_t {
		ALERT_UNDER_ATTACK = 0,
		ALERT_BUILDING_UNDER_ATTACK = 1,
		ALERT_BUILDING_DESTROYED = 2,
		ALERT_STRUCTURE_CAPTURED = 3,
		ALERT_WONDER_COUNTDOWN = 4,
		ALERT_RELIC_CLAIMED = 5
	};
	Type type = ALERT_UNDER_ATTACK;
	float x = 0.0f;
	float z = 0.0f;
	uint32_t entity_id = 0;
	uint8_t team = 0;
	float time_stamp = 0.0f;
};

struct UnitTypeInfo {
	const char *name;
	const char *model_id;
	const char *portrait_id;
	float hp;
	float speed;
	float attack_range;
	float attack_damage;
	float attack_cooldown;
	int32_t carry_capacity;
	float aggro_range;
	float hack_armor;
	float pierce_armor;
	float crush_armor;
	int food_cost;
	int wood_cost;
	int gold_cost;
	int stone_cost;
	int pop_cost;
	float train_time;
	float hack_damage;
	float pierce_damage;
	float crush_damage;
};

inline const UnitTypeInfo &unit_type_info(uint8_t type) {
	static const UnitTypeInfo kTable[UNIT_TYPE_COUNT] = {
		// name, model, portrait, hp, speed, range, dmg, cd, carry, aggro, hack, pierce, crush, food, wood, gold, stone, pop, train_time, hack_dmg, pierce_dmg, crush_dmg
		{"Villager", "villager", "villager", 80.0f, 3.0f, 1.6f, 5.0f, 1.0f, 20, 3.0f, 2.0f, 4.0f, 15.0f, 50, 0, 0, 0, 1, 10.0f, 5.0f, 0.0f, 0.0f},
		{"Swordsman", "swordsman", "swordsman", 100.0f, 3.4f, 2.0f, 8.0f, 0.75f, 0, 10.0f, 3.0f, 3.0f, 15.0f, 0, 30, 60, 0, 1, 14.0f, 8.0f, 0.0f, 0.0f},
		{"Archer", "archer", "archer", 80.0f, 3.3f, 16.0f, 7.2f, 1.25f, 0, 16.0f, 1.0f, 1.0f, 15.0f, 0, 50, 40, 0, 1, 12.0f, 0.0f, 7.2f, 0.0f},
		{"Spearman", "spearman", "spearman", 100.0f, 3.2f, 2.4f, 8.5f, 1.0f, 0, 10.0f, 3.0f, 3.0f, 15.0f, 30, 40, 20, 0, 1, 12.0f, 4.25f, 4.25f, 0.0f},
		{"Cavalry", "cavalry", "cavalry", 180.0f, 5.5f, 2.2f, 11.5f, 1.25f, 20, 12.0f, 3.0f, 3.0f, 15.0f, 50, 0, 80, 0, 2, 18.0f, 11.5f, 0.0f, 0.0f},
		{"Merchant Trader", "trader", "trader", 120.0f, 4.2f, 1.0f, 0.0f, 1.0f, 50, 0.0f, 2.0f, 2.0f, 10.0f, 60, 60, 0, 0, 1, 12.0f, 0.0f, 0.0f, 0.0f},
		{"Catapult Artillery", "catapult", "catapult", 220.0f, 2.0f, 24.0f, 45.0f, 3.5f, 0, 24.0f, 2.0f, 5.0f, 20.0f, 0, 150, 100, 50, 3, 28.0f, 0.0f, 0.0f, 45.0f},
		{"Historical Champion Hero", "hero", "hero", 350.0f, 4.2f, 2.8f, 18.0f, 0.8f, 0, 16.0f, 6.0f, 6.0f, 25.0f, 200, 0, 200, 0, 3, 30.0f, 12.0f, 6.0f, 0.0f},
		{"War Trireme", "trireme", "trireme", 600.0f, 4.5f, 20.0f, 32.0f, 1.0f, 0, 20.0f, 5.0f, 8.0f, 20.0f, 0, 200, 150, 0, 3, 35.0f, 0.0f, 32.0f, 0.0f},
		{"Carthaginian War Elephant", "elephant", "elephant", 900.0f, 3.4f, 2.5f, 35.0f, 1.4f, 0, 16.0f, 5.0f, 8.0f, 20.0f, 250, 0, 200, 0, 4, 40.0f, 20.0f, 0.0f, 15.0f},
		{"Siege Battering Ram", "ram", "ram", 850.0f, 2.2f, 2.4f, 75.0f, 2.5f, 0, 12.0f, 2.0f, 12.0f, 15.0f, 0, 200, 100, 0, 3, 25.0f, 0.0f, 0.0f, 75.0f},
		{"Temple Healer", "healer", "healer", 90.0f, 3.2f, 12.0f, 0.0f, 1.0f, 0, 10.0f, 1.0f, 1.0f, 10.0f, 80, 0, 60, 0, 1, 14.0f, 0.0f, 0.0f, 0.0f},
		{"Celtic War Chariot", "chariot", "chariot", 280.0f, 5.8f, 14.0f, 14.0f, 1.1f, 0, 14.0f, 4.0f, 4.0f, 20.0f, 80, 100, 60, 0, 2, 22.0f, 4.0f, 10.0f, 0.0f},
		{"Celtic War Dog", "war_dog", "war_dog", 75.0f, 5.2f, 1.2f, 9.0f, 0.7f, 0, 12.0f, 1.0f, 2.0f, 8.0f, 60, 0, 0, 0, 1, 8.0f, 9.0f, 0.0f, 0.0f},
		{"Bolt Shooter Ballista", "bolt_shooter", "bolt_shooter", 200.0f, 2.2f, 28.0f, 40.0f, 3.0f, 0, 28.0f, 2.0f, 4.0f, 15.0f, 0, 120, 80, 0, 2, 22.0f, 0.0f, 40.0f, 0.0f},
		{"Fishing Boat", "fishing_boat", "fishing_boat", 150.0f, 3.8f, 1.5f, 0.0f, 1.0f, 30, 0.0f, 1.0f, 2.0f, 10.0f, 0, 75, 0, 0, 1, 12.0f, 0.0f, 0.0f, 0.0f},
		{"Merchant Cargo Ship", "merchant_ship", "merchant_ship", 300.0f, 4.0f, 1.0f, 0.0f, 1.0f, 100, 0.0f, 3.0f, 4.0f, 15.0f, 0, 120, 40, 0, 1, 18.0f, 0.0f, 0.0f, 0.0f},
		{"Female Citizen", "female_citizen", "female_citizen", 65.0f, 3.2f, 1.4f, 4.0f, 1.0f, 25, 3.0f, 1.0f, 3.0f, 10.0f, 40, 0, 0, 0, 1, 8.0f, 4.0f, 0.0f, 0.0f},
		{"Gallic Carnyx Herald", "carnyx", "carnyx", 140.0f, 3.5f, 2.0f, 7.0f, 1.0f, 0, 12.0f, 3.0f, 3.0f, 15.0f, 70, 40, 30, 0, 1, 15.0f, 7.0f, 0.0f, 0.0f},
		{"Mauryan Worker Elephant", "worker_elephant", "worker_elephant", 600.0f, 3.2f, 2.0f, 12.0f, 1.2f, 100, 6.0f, 4.0f, 6.0f, 20.0f, 180, 80, 0, 0, 2, 25.0f, 8.0f, 0.0f, 4.0f},
		{"Mauryan Archer Maiden", "archer_maiden", "archer_maiden", 95.0f, 3.6f, 17.0f, 8.5f, 1.1f, 0, 17.0f, 2.0f, 2.0f, 15.0f, 40, 60, 40, 0, 1, 14.0f, 0.0f, 8.5f, 0.0f},
		{"Persian/Arabian Camel", "camel", "camel", 190.0f, 5.2f, 2.4f, 12.0f, 1.2f, 0, 12.0f, 3.0f, 3.0f, 15.0f, 60, 0, 70, 0, 2, 16.0f, 12.0f, 0.0f, 0.0f},
		{"Seleucid Scythed Chariot", "scythed_chariot", "scythed_chariot", 320.0f, 5.6f, 2.2f, 24.0f, 1.0f, 0, 14.0f, 5.0f, 5.0f, 25.0f, 100, 80, 120, 0, 3, 26.0f, 18.0f, 0.0f, 6.0f},
		{"Imperial Minister", "healer", "healer", 200.0f, 3.204f, 1.8f, 10.0f, 1.0f, 0, 8.0f, 2.0f, 4.0f, 6.0f, 100, 0, 100, 0, 1, 15.0f, 10.0f, 0.0f, 0.0f},
		{"Catafalque", "ram", "ram", 1000.0f, 1.762f, 1.0f, 0.0f, 1.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f},
		{"Technology", "villager", "villager", 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f}, // UNIT_TYPE_TECH
		// template_unit_support_wagon (0.28 structures/germans/wagon.xml; the ram portrait for now)
		{"Supply Wagon", "wagon", "ram", 200.0f, 1.78f, 1.0f, 0.0f, 1.0f, 0, 0.0f, 4.0f, 4.0f, 10.0f, 50, 50, 0, 0, 1, 12.0f, 0.0f, 0.0f, 0.0f}
	};
	if (type >= UNIT_TYPE_COUNT) return kTable[UNIT_VILLAGER];
	return kTable[type];
}

inline float unit_collision_radius(uint8_t type) {
	switch (type) {
		case UNIT_VILLAGER: return 0.50f;
		case UNIT_SWORDSMAN: return 0.55f;
		case UNIT_ARCHER: return 0.50f;
		case UNIT_SPEARMAN: return 0.55f;
		case UNIT_CAVALRY: return 0.85f;
		case UNIT_TRADER: return 0.80f;
		case UNIT_CATAPULT: return 1.30f;
		case UNIT_HERO: return 0.85f;
		case UNIT_TRIREME: return 1.80f;
		case UNIT_ELEPHANT: return 1.40f;
		case UNIT_SIEGE_RAM: return 1.20f;
		case UNIT_HEALER: return 0.50f;
		case UNIT_CHARIOT: return 1.25f;
		case UNIT_WAR_DOG: return 0.40f;
		case UNIT_BOLT_SHOOTER: return 1.10f;
		case UNIT_FISHING_BOAT: return 1.10f;
		case UNIT_MERCHANT_SHIP: return 1.60f;
		case UNIT_FEMALE_CITIZEN: return 0.48f;
		case UNIT_CARNYX: return 0.55f;
		case UNIT_WORKER_ELEPHANT: return 1.35f;
		case UNIT_ARCHER_MAIDEN: return 0.50f;
		case UNIT_CAMEL: return 0.90f;
		case UNIT_SCYTHED_CHARIOT: return 1.35f;
		case UNIT_MINISTER: return 0.50f;
		case UNIT_CATAFALQUE: return 1.20f; // Footprint 5 x 12 m
		case UNIT_WAGON: return 1.40f; // Footprint 7 x 14 m
		default: return 0.55f;
	}
}

inline const char *unit_type_name(uint8_t type) { return unit_type_info(type).name; }
inline const char *unit_type_model(uint8_t type) { return unit_type_info(type).model_id; }
inline const char *unit_type_portrait(uint8_t type) { return unit_type_info(type).portrait_id; }

inline bool unit_is_ship(uint8_t type) {
	return type == UNIT_TRIREME || type == UNIT_FISHING_BOAT || type == UNIT_MERCHANT_SHIP;
}

// 0 A.D. passability class Clearance (simulation/data/pathfinder.xml) in RTS0 units: how close
// a land unit's or animal's centre comes to a structure. "default" 0.8 m, "large" 3 m.
inline constexpr float CLEARANCE_DEFAULT = 0.8f / 4.0f;
inline constexpr float CLEARANCE_LARGE = 3.0f / 4.0f;
// helpers/MapEdgeTiles.h MAP_EDGE_TILES in RTS0 units: 3 terrain tiles (12 m). On a circular map
// CCmpPathfinder makes everything that far inside the LOS circle's edge impassable to every class.
inline constexpr float MAP_EDGE_DIST = 12.0f / 4.0f;

inline bool unit_is_siege_engine(uint8_t type) {
	return type == UNIT_SIEGE_RAM || type == UNIT_CATAPULT || type == UNIT_BOLT_SHOOTER;
}

// A unit's Clearance for pushing and for the obstruction it is to walkers while it stands still
// (0 A.D. CCmpUnitMotionManager, CCmpObstructionManager unit shapes). 0 A.D. gives infantry 0.8 m;
// RTS0 draws its units bigger and keeps them further apart, so each unit's clearance follows its
// body (unit_collision_radius) and 0 A.D.'s crowd lengths scale by PUSH_SCALE, which puts two
// villagers left alone 1.4 units apart as before.
inline constexpr float PUSH_SCALE = 5.0f / 6.0f; // RTS0 units per 0 A.D. metre in a crowd
inline constexpr float PUSH_CLEARANCE_PER_RADIUS = 0.8f * PUSH_SCALE / 0.5f;
inline float unit_push_clearance(uint8_t type) {
	return unit_collision_radius(type) * PUSH_CLEARANCE_PER_RADIUS;
}
// ResourceGatherer MaxDistance of land gatherers (2 m, template_unit) between a gatherer's obstruction
// and a mine's square: 0.28 measures range between the two obstructions. The gatherers round a mine
// stand in a crowd, so the gap counts at the crowd scale past the unit's crowd clearance
// (unit_push_clearance): 0.28's band round a large mine holds its 24 gatherers, and one at the map
// scale would hold only half of them in RTS0's bigger crowds.
inline constexpr float MINE_GATHER_REACH = 2.0f * PUSH_SCALE;

// 0 A.D. ResourceDropsite on a unit: the Mauryan worker elephant (template_unit_support_elephant)
// and the supply wagon (template_unit_support_wagon) take food, wood, stone and metal. Only the
// wagon is Sharable, and ResourceDropsite.js shares a sharable dropsite by default.
inline bool unit_is_mobile_dropsite(uint8_t type) { return type == UNIT_WORKER_ELEPHANT || type == UNIT_WAGON; }
inline bool unit_dropsite_sharable(uint8_t type) { return type == UNIT_WAGON; }

// 0 A.D. Attack.js CanAttack: fishing/merchant/trader/healer ship no (or
// RestrictedClasses !SeaCreature). Fish are gathered, not fought.
inline bool unit_can_attack(uint8_t type) {
	return unit_type_info(type).attack_damage > 0.0f;
}

// 0 A.D. ship attack kinds for the three hulls RTS0 trains:
//   fishing  → Melee Harpoon vs SeaCreature only (gather in RTS0)
//   merchant → no Attack
//   trireme  → Ranged Arrow (template_unit_ship_warship_arrow; the ram /
//              siege / fire hulls are separate 0 A.D. units we do not train)
inline bool unit_has_ranged_attack(uint8_t type, uint8_t civ = CIV_ATHENIANS) {
	switch (type) {
		case UNIT_ARCHER:
		case UNIT_ARCHER_MAIDEN:
		case UNIT_BOLT_SHOOTER:
		case UNIT_CHARIOT:
		case UNIT_CATAPULT:
		case UNIT_TRIREME:
			return true;
		case UNIT_ELEPHANT:
			return civ == CIV_MAURYANS;
		default:
			return false;
	}
}

// 0 A.D. UnitAI/DefaultStance: support units (template_unit_support: civilians and women,
// healers, traders, ministers) and civilian ships are passive, so they flee when attacked;
// stone and bolt throwers stand ground.
inline uint8_t unit_default_stance(uint8_t type) {
	switch (type) {
		case UNIT_VILLAGER: // the civ's support worker, 0 A.D. 0.28's support_civilian
		case UNIT_FISHING_BOAT:
		case UNIT_MERCHANT_SHIP:
		case UNIT_TRADER:
		case UNIT_FEMALE_CITIZEN:
		case UNIT_HEALER:
		case UNIT_MINISTER:
		case UNIT_WAGON:
		case UNIT_WORKER_ELEPHANT: // maur/support_elephant: template_unit_support
			return STANCE_PASSIVE;
		case UNIT_CATAFALQUE:
			return STANCE_STAND_GROUND; // template_unit_catafalque UnitAI/DefaultStance
		case UNIT_CATAPULT: // template_unit_siege_stonethrower / _boltshooter
		case UNIT_BOLT_SHOOTER:
			return STANCE_STAND_GROUND;
		default:
			return STANCE_AGGRESSIVE;
	}
}

// Vision range (0 A.D. Vision/Range, scaled like the rest of RTS0): fog of war and
// UnitAI's attack queries both use it.
inline float unit_vision_range(uint8_t type) {
	switch (type) {
		case UNIT_ARCHER:   return 22.0f;
		case UNIT_CAVALRY:  return 20.0f;
		case UNIT_CATAPULT: return 24.0f;
		case UNIT_TRIREME:  return 28.0f;
		case UNIT_ELEPHANT: return 20.0f;
		case UNIT_HERO:     return 18.0f;
		default:            return 16.0f;
	}
}

// Corpse lifetime before removal. Ships use 0 A.D. CCmpDecay SinkingAnim (~4–5s).
inline float unit_death_duration(uint8_t type) {
	if (unit_is_ship(type)) return 5.0f;
	if (unit_is_siege_engine(type)) return 1.6f;
	return 1.4f;
}

struct BuildingTypeInfo {
	const char *name;
	const char *model_id;
	const char *portrait_id;
	float max_hp;
	int footprint; // 1 or 2
	int food_cost;
	int wood_cost;
	int gold_cost;
	int stone_cost;
	int pop_bonus;
	int garrison_cap;
	float build_time;
	// Arrows: building_ai_info() (0 A.D. BuildingAI and Attack/Ranged).
	// 0 A.D. Capturable.js / TerritoryDecay.js template values.
	float capture_max_points;
	float capture_regen_rate;
	float capture_garrison_regen_rate;
	float territory_decay_rate;
};

inline const BuildingTypeInfo &building_type_info(uint8_t type) {
	static const BuildingTypeInfo kTable[BLD_TYPE_COUNT] = {
		// name, model, portrait, hp, foot, f, w, g, s, pop, garrison, build_time,
		// capture max, regen, garrison regen, territory decay (0.28 Capturable from
		// template_structure.xml 500/5/1.0; civil centre 2500/30, fortress x8 and 45, temple 15,
		// template_structure_military and _defensive_tower 10, wonder x4, farmstead 300; the dock's
		// TerritoryDecay is disabled). garrison is GarrisonHolder/Max (0: none; the house's civ value and
		// other civ overrides are in structure_garrison_max, the List in structure_garrison_list).
		{"Town Center", "town_center", "town_center", 3000.0f, 2, 0, 300, 250, 300, 20, 20, 125.0f, 2500.0f, 30.0f, 1.0f, 20.0f},
		{"House", "house", "house", 800.0f, 1, 0, 75, 0, 0, 5, 3, 7.5f, 500.0f, 5.0f, 1.0f, 20.0f},
		{"Barracks", "barracks", "barracks", 2000.0f, 2, 0, 200, 0, 100, 0, 10, 37.5f, 500.0f, 10.0f, 1.0f, 20.0f},
		{"Storehouse", "storehouse", "storehouse", 800.0f, 1, 0, 100, 0, 0, 0, 0, 10.0f, 500.0f, 5.0f, 1.0f, 20.0f},
		{"Defense Tower", "wall_tower", "tower", 1000.0f, 1, 0, 100, 0, 100, 0, 5, 37.5f, 500.0f, 10.0f, 1.0f, 20.0f},
		{"Farmstead", "farmstead", "farmstead", 900.0f, 1, 0, 100, 0, 0, 0, 0, 11.25f, 300.0f, 5.0f, 1.0f, 20.0f},
		{"Market", "market", "market", 1500.0f, 2, 0, 300, 0, 0, 0, 0, 37.5f, 500.0f, 5.0f, 1.0f, 20.0f},
		{"Fortress", "fortress", "fortress", 5200.0f, 2, 0, 300, 0, 600, 0, 20, 112.5f, 4000.0f, 45.0f, 1.0f, 40.0f},
		{"Gate", "gate", "gate", 2500.0f, 1, 0, 12, 0, 18, 0, 0, 7.5f, 500.0f, 5.0f, 1.0f, 20.0f},
		{"Temple", "temple", "temple", 2000.0f, 2, 0, 0, 0, 300, 0, 20, 50.0f, 500.0f, 15.0f, 1.0f, 20.0f},
		{"Wonder Monument", "wonder", "wonder", 5000.0f, 2, 0, 1000, 1000, 1500, 0, 50, 250.0f, 2000.0f, 5.0f, 1.0f, 20.0f},
		{"Naval Shipyard Dock", "dock", "dock", 2500.0f, 2, 0, 200, 0, 0, 0, 0, 37.5f, 500.0f, 10.0f, 1.0f, 0.0f},
		{"Stone Wall Segment", "wall_segment", "wall", 3000.0f, 1, 0, 0, 0, 36, 0, 0, 9.0f, 500.0f, 5.0f, 1.0f, 20.0f},
		{"Livestock Corral", "corral", "corral", 500.0f, 2, 0, 100, 0, 0, 0, 8, 12.5f, 500.0f, 5.0f, 1.0f, 20.0f},
		// 0 A.D. 0.28 template_structure_* values: Health/Max, Cost/Resources,
		// GarrisonHolder/Max, Capturable. BuildTime is scaled by 0.25 to match
		// RTS0's compressed build times (barracks 150s -> 35s above); footprint
		// is 2 cells when the 0 A.D. footprint is wider than 15 m.
		{"Field", "field_plot", "field", 250.0f, 2, 0, 100, 0, 0, 0, 0, 12.5f, 500.0f, 5.0f, 1.0f, 20.0f},
		{"Stable", "stable", "stable", 2000.0f, 2, 0, 200, 0, 50, 0, 10, 30.0f, 500.0f, 10.0f, 1.0f, 20.0f},
		{"Archery Range", "range", "range", 2000.0f, 2, 0, 200, 0, 0, 0, 10, 30.0f, 500.0f, 10.0f, 1.0f, 20.0f},
		{"Forge", "blacksmith", "forge", 2000.0f, 2, 0, 200, 0, 0, 0, 1, 30.0f, 500.0f, 10.0f, 1.0f, 20.0f},
		{"Arsenal", "arsenal", "arsenal", 2000.0f, 2, 0, 300, 0, 0, 0, 5, 45.0f, 500.0f, 10.0f, 1.0f, 20.0f},
		{"Sentry Tower", "sentry_tower", "sentry_tower", 400.0f, 1, 0, 100, 0, 0, 0, 3, 10.0f, 500.0f, 10.0f, 1.0f, 20.0f},
		{"Outpost", "outpost", "outpost", 400.0f, 1, 0, 60, 0, 0, 0, 0, 7.5f, 500.0f, 5.0f, 1.0f, 20.0f},
		{"Palisade", "palisade", "palisade", 600.0f, 1, 0, 12, 0, 0, 0, 0, 3.0f, 500.0f, 5.0f, 1.0f, 20.0f},
		{"Wall Tower", "wall_tower", "wall_tower", 4000.0f, 1, 0, 0, 0, 48, 0, 4, 12.0f, 500.0f, 5.0f, 1.0f, 20.0f},
		{"Elephant Stable", "stable_elephant", "elephant_stable", 3000.0f, 2, 0, 200, 0, 200, 0, 5, 45.0f, 500.0f, 10.0f, 1.0f, 20.0f},
		{"Theater", "theater", "theater", 3000.0f, 2, 0, 200, 200, 600, 0, 5, 125.0f, 500.0f, 5.0f, 1.0f, 20.0f},
		{"Celtic Embassy", "embassy", "embassy", 1200.0f, 2, 0, 200, 0, 0, 0, 6, 37.5f, 500.0f, 10.0f, 1.0f, 20.0f}, // cart/embassy_celtic
		// Civ-specific structures, BLD_GYMNASIUM onwards (civ_building_data.h).
		RTSO_CIV_BUILDING_INFO_ROWS
	};
	if (type >= BLD_TYPE_COUNT) return kTable[BLD_HOUSE];
	return kTable[type];
}

// 0 A.D. structures/{athen,spart,mace,sele,kush,cart,pers,rome,han,germ}/house.xml take
// template_structure_civic_house_big (Population/Bonus 10, 150 wood, BuildTime 50, Health
// 1200); the other civs keep template_structure_civic_house (5, 75, 30, 800).
inline bool civ_has_big_house(uint8_t civ) {
	switch (civ) {
		case CIV_ATHENIANS: case CIV_SPARTANS: case CIV_MACEDONIANS: case CIV_SELEUCIDS: case CIV_KUSHITES:
		case CIV_CARTHAGINIANS: case CIV_PERSIANS: case CIV_ROMANS: case CIV_HAN: case CIV_GERMANS: return true;
		default: return false;
	}
}

// TerritoryInfluence of the civ's own template where it differs: the big house
// (template_structure_civic_house_big: 20 m at Weight 40000) and the Iberian tower (38 m).
inline TerritoryInfluenceInfo structure_territory_influence(uint8_t type, uint8_t civ) {
	TerritoryInfluenceInfo ti = territory_influence_info(type);
	if (type == BLD_HOUSE && civ_has_big_house(civ)) {
		ti.radius = 20.0f * 0.25f; // map units, as territory_data.h
		ti.weight = 40000;
	} else if (type == BLD_TOWER && civ == CIV_IBERIANS) {
		ti.radius = 38.0f * 0.25f;
	}
	return ti;
}

// Whether a civ's structure of `type` has a round 0 A.D. Footprint (<Circle>) rather than a square
// one. 0 A.D.'s obstruction stays square either way, so this only shapes where trained and
// unloaded units step out (Footprint.PickSpawnPoint). RTS0 draws the circle on the structure's
// half size (0 A.D.'s radii run 10-20% past half the obstruction, 15 m on the Briton civic
// centre's 25 m square), as it keeps one footprint per building type. From 0.28's
// simulation/templates: brit/{civil_centre,house,defense_tower,fortress,wonder}.xml,
// iber/{defense_tower,wall_tower}.xml, maur/wonder.xml, every civ's theater
// (template_structure_special_theater.xml) and athen/prytaneion, spart/gerousia, gaul/assembly,
// ptol/lighthouse, rome/temple_vesta, pers/ice_house and brit/crannog.
inline bool building_footprint_round(uint8_t civ, uint8_t type) {
	switch (type) {
		case BLD_THEATER: case BLD_PRYTANEION: case BLD_GEROUSIA: case BLD_ASSEMBLY:
		case BLD_LIGHTHOUSE: case BLD_TEMPLE_VESTA: case BLD_ICE_HOUSE: case BLD_CRANNOG:
			return true;
		case BLD_TOWN_CENTER: case BLD_HOUSE: case BLD_FORTRESS:
			return civ == CIV_BRITONS;
		case BLD_TOWER:
			return civ == CIV_BRITONS || civ == CIV_IBERIANS;
		case BLD_WALL_TOWER:
			return civ == CIV_IBERIANS;
		case BLD_WONDER:
			return civ == CIV_BRITONS || civ == CIV_MAURYANS;
		default:
			return false;
	}
}

// building_type_info for a civ: where structures/<civ>/<name>.xml differs from the generic row
// (0.28's own Health/Max, Cost and BuildTime x0.25). The big house
// (template_structure_civic_house_big: 1200 health, 150 wood, 10 population, 50 s) where the civ
// has one; brit/gaul/maur barracks and stables, brit/gaul temples and maur walls, gates and wall
// towers in wood; germ/temple 150 wood and 150 stone; iber/defense_tower; cart/dock 150 wood;
// han/field 60 wood and 30 s. GarrisonHolder/Max differences are in structure_garrison_max.
inline BuildingTypeInfo civ_building_type_info(uint8_t civ, uint8_t type) {
	BuildingTypeInfo info = building_type_info(type);
	const bool celt_or_maur = civ == CIV_BRITONS || civ == CIV_GAULS || civ == CIV_MAURYANS;
	auto cost = [&info](int wood, int stone) { info.wood_cost = wood; info.stone_cost = stone; };
	switch (type) {
		case BLD_HOUSE:
			if (civ_has_big_house(civ)) {
				info.max_hp = 1200.0f;
				info.wood_cost = 150;
				info.pop_bonus = 10;
				info.build_time = 50.0f * 0.25f;
			}
			break;
		case BLD_BARRACKS: if (celt_or_maur) cost(300, 0); break;
		case BLD_STABLE: if (celt_or_maur) cost(250, 0); break;
		case BLD_TEMPLE:
			if (civ == CIV_BRITONS || civ == CIV_GAULS) cost(300, 0);
			else if (civ == CIV_GERMANS) cost(150, 150);
			break;
		case BLD_GATE: if (civ == CIV_MAURYANS) cost(30, 0); break;
		case BLD_WALL_SEGMENT: if (civ == CIV_MAURYANS) cost(36, 0); break;
		case BLD_WALL_TOWER: if (civ == CIV_MAURYANS) cost(60, 0); break;
		case BLD_TOWER:
			if (civ == CIV_IBERIANS) {
				info.max_hp = 2400.0f;
				cost(50, 250);
				info.build_time = 200.0f * 0.25f;
			}
			break;
		case BLD_DOCK: if (civ == CIV_CARTHAGINIANS) info.wood_cost = 150; break;
		case BLD_MONUMENT:
			// germ/totem (Cerannus Totem): 800 health, 150 wood and 100 metal.
			if (civ == CIV_GERMANS) {
				info.max_hp = 800.0f;
				info.wood_cost = 150;
				info.gold_cost = 100;
				info.stone_cost = 0;
			}
			break;
		case BLD_FIELD:
			if (civ == CIV_HAN) {
				info.wood_cost = 60;
				info.build_time = 30.0f * 0.25f;
			}
			break;
		default: break;
	}
	return info;
}

// 0 A.D. WallSet (template_structure_wallset.xml, structures/*_wallset_stone.xml): a wall is
// laid as towers joined by long, medium and short segments (WallPiece/Length 36, 24 and 12 m,
// here in world units of 4 m), each segment reaching between MinTowerOverlap and MaxTowerOverlap
// of the way into the towers at its ends. A tower's WallPiece/Length is 7 m (athen/wall_tower;
// most civs' towers are about that wide), its obstruction one cell. A segment's obstruction is
// its length by 5 m (athen/wall_long Static depth 5).
inline constexpr float WALL_SEGMENT_LENGTHS[3] = {36.0f / 4.0f, 24.0f / 4.0f, 12.0f / 4.0f};
inline constexpr float WALL_TOWER_LENGTH = 7.0f / 4.0f;
inline constexpr float WALL_MIN_TOWER_OVERLAP = 0.05f, WALL_MAX_TOWER_OVERLAP = 0.90f;
inline constexpr float WALL_HALF_DEPTH = 5.0f / 4.0f * 0.5f;
// 0 A.D. wall_long Health 3000, Cost stone 36 and BuildTime 36 s (x0.25 like every RTS0 build
// time); wall_medium and wall_short have two thirds and a third of each, as of the length.
inline constexpr float WALL_LONG_HEALTH = 3000.0f, WALL_LONG_STONE = 36.0f, WALL_LONG_BUILD_TIME = 36.0f * 0.25f;

// wallset_palisade.xml (template_wallset MaxTowerOverlap 0.85): palisades_long/medium/short
// WallPiece/Length 14, 9 and 4 m with a Static depth of 2 m; palisades_tower WallPiece/Length 3 m
// on a 5 m square footprint. RTS0 has the one palisade type: a palisade tower is a BLD_PALISADE
// whose half width is PALISADE_TOWER_HALF, a segment one of the segment half lengths, and the
// one-cell palisade maps and older saves place (half width 1) stays as it is.
inline constexpr float PALISADE_SEGMENT_LENGTHS[3] = {14.0f / 4.0f, 9.0f / 4.0f, 4.0f / 4.0f};
inline constexpr float PALISADE_TOWER_LENGTH = 3.0f / 4.0f;
inline constexpr float PALISADE_TOWER_HALF = 5.0f / 4.0f * 0.5f;
inline constexpr float PALISADE_HALF_DEPTH = 2.0f / 4.0f * 0.5f;

// rome/wallset_siege.xml (MaxTowerOverlap 1.00): siege_wall_long/medium/short (WallPiece/Length 36,
// 24 and 12 m, Static depth 4 m) between siege_wall_towers (WallPiece/Length 6 m on a 7 m square
// footprint). Like palisades, one type holds both: a siege wall tower is a BLD_SIEGE_WALL whose
// half width is SIEGE_WALL_TOWER_HALF.
inline constexpr float SIEGE_WALL_TOWER_LENGTH = 6.0f / 4.0f;
inline constexpr float SIEGE_WALL_TOWER_HALF = 7.0f / 4.0f * 0.5f;
inline constexpr float SIEGE_WALL_HALF_DEPTH = 4.0f / 4.0f * 0.5f;

enum WallSetKind : uint8_t { WALLSET_STONE = 0, WALLSET_PALISADE = 1, WALLSET_SIEGE = 2, WALLSET_COUNT };

// A WallSet's pieces and their 0 A.D. values: Health/Max, Cost (stone for walls, wood for
// palisades) and BuildTime (x0.25) of the long, medium and short segment and of the tower. A
// tower whose values are negative keeps its building_type_info row (the stone wall tower).
struct WallSetInfo {
	uint8_t segment, tower;
	float lengths[3];
	float tower_length, tower_half, min_overlap, max_overlap, half_depth;
	float health[3], cost[3], build_time[3];
	float tower_health, tower_cost, tower_build_time;
};

// Each civ's stone WallSet (structures/{civ}/wallset_stone.xml Min/MaxTowerOverlap, else
// template_wallset's 0.05 and 0.85; wall_tower WallPiece/Length; wall_long/medium/short
// WallPiece/Length and Obstruction Static depth), in metres. Gaia's maps lay the Athenian set.
struct WallCivGeometry {
	float tower_length, min_overlap, max_overlap, lengths[3], depth;
};

inline const WallCivGeometry &wall_civ_geometry(uint8_t civ) {
	static const WallCivGeometry kCivs[CIV_COUNT] = {
		{7.0f, 0.05f, 0.90f, {36.0f, 24.0f, 12.0f}, 5.0f},  // athen
		{7.0f, 0.05f, 0.90f, {36.0f, 24.0f, 12.0f}, 5.0f},  // spart
		{7.0f, 0.05f, 0.85f, {36.0f, 24.0f, 12.0f}, 6.0f},  // pers (template_wallset overlaps)
		{10.0f, 0.10f, 0.80f, {36.0f, 24.0f, 12.0f}, 8.0f}, // rome
		{11.0f, 0.05f, 0.90f, {36.0f, 24.0f, 12.0f}, 7.0f}, // cart
		{7.0f, 0.05f, 0.90f, {36.0f, 24.0f, 12.0f}, 5.0f},  // mace
		{10.0f, 0.05f, 0.80f, {36.0f, 24.0f, 12.0f}, 6.0f}, // brit
		{8.0f, 0.05f, 0.80f, {36.0f, 24.0f, 12.0f}, 6.0f},  // gaul
		{10.0f, 0.20f, 0.80f, {36.0f, 24.0f, 12.0f}, 7.0f}, // iber
		{7.0f, 0.05f, 0.85f, {36.0f, 24.0f, 12.0f}, 4.0f},  // maur (template_wallset overlaps)
		{8.0f, 0.05f, 0.90f, {36.0f, 24.0f, 12.0f}, 5.0f},  // kush
		{9.0f, 0.15f, 0.90f, {38.0f, 25.0f, 15.0f}, 5.0f},  // ptol
		{7.0f, 0.05f, 0.90f, {34.0f, 21.0f, 11.0f}, 5.0f},  // sele (MinTowerOverlap from template_wallset)
		{10.0f, 0.10f, 0.80f, {36.0f, 24.0f, 12.0f}, 8.0f}, // han
		{7.0f, 0.05f, 0.90f, {36.0f, 24.0f, 12.0f}, 5.0f},  // Gaia: athen
		{9.0f, 0.05f, 0.80f, {36.0f, 24.0f, 12.0f}, 8.0f},  // germ
	};
	return kCivs[civ < CIV_COUNT ? civ : CIV_ATHENIANS];
}

// The share of a gate's length its Door takes: 0.28 gates are an obstruction cluster of Left, Door
// and Right, and Gate.js only switches the Door's BlockMovement and BlockPathfinding (shape 0, the
// first by name), so the sides keep blocking while the gate is open. Door / (Left + Door + Right)
// widths from structures/{civ}/wall_gate.xml, palisades_gate.xml and rome/siege_wall_gate.xml.
inline float gate_door_fraction(uint8_t type, uint8_t civ) {
	if (type == BLD_PALISADE_GATE) return 10.0f / 14.0f;
	if (type == BLD_SIEGE_WALL_GATE) return 13.0f / 36.0f;
	static const float kDoor[CIV_COUNT] = {
		15.0f / 36.0f, // athen
		16.0f / 36.0f, // spart
		16.0f / 36.0f, // pers
		12.0f / 36.0f, // rome
		19.0f / 36.0f, // cart
		15.0f / 36.0f, // mace
		13.0f / 36.0f, // brit
		15.0f / 37.0f, // gaul
		19.0f / 36.0f, // iber
		14.0f / 36.0f, // maur
		16.0f / 36.0f, // kush
		15.0f / 39.0f, // ptol
		14.0f / 34.0f, // sele
		8.0f / 36.0f,  // han
		15.0f / 36.0f, // Gaia: athen
		13.0f / 37.0f, // germ
	};
	return kDoor[civ < CIV_COUNT ? civ : CIV_ATHENIANS];
}

// The `kind` WallSet of `civ` (CIV_COUNT: the generic one, the Athenian stone set). Only stone
// walls differ by civ, and Han palisades reach further into their towers (han/wallset_palisade.xml
// MaxTowerOverlap 0.95).
inline WallSetInfo wallset_info(uint8_t kind, uint8_t civ = CIV_COUNT);

inline const WallSetInfo &wallset_base_info(uint8_t kind) {
	static const WallSetInfo kSets[WALLSET_COUNT] = {
		{BLD_WALL_SEGMENT, BLD_WALL_TOWER, {WALL_SEGMENT_LENGTHS[0], WALL_SEGMENT_LENGTHS[1], WALL_SEGMENT_LENGTHS[2]},
			WALL_TOWER_LENGTH, 1.0f, WALL_MIN_TOWER_OVERLAP, WALL_MAX_TOWER_OVERLAP, WALL_HALF_DEPTH,
			{WALL_LONG_HEALTH, WALL_LONG_HEALTH * 2.0f / 3.0f, WALL_LONG_HEALTH / 3.0f},
			{WALL_LONG_STONE, WALL_LONG_STONE * 2.0f / 3.0f, WALL_LONG_STONE / 3.0f},
			{WALL_LONG_BUILD_TIME, WALL_LONG_BUILD_TIME * 2.0f / 3.0f, WALL_LONG_BUILD_TIME / 3.0f},
			-1.0f, -1.0f, -1.0f},
		// palisades_*: Health/Max 1000 x 0.6, 0.4, 0.2 and the tower's 0.7; wood 12, 8, 4 and 14;
		// BuildTime 12, 8, 4 and 14 s.
		{BLD_PALISADE, BLD_PALISADE, {PALISADE_SEGMENT_LENGTHS[0], PALISADE_SEGMENT_LENGTHS[1], PALISADE_SEGMENT_LENGTHS[2]},
			PALISADE_TOWER_LENGTH, PALISADE_TOWER_HALF, 0.05f, 0.85f, PALISADE_HALF_DEPTH,
			{600.0f, 400.0f, 200.0f}, {12.0f, 8.0f, 4.0f}, {12.0f * 0.25f, 8.0f * 0.25f, 4.0f * 0.25f},
			700.0f, 14.0f, 14.0f * 0.25f},
		// rome/siege_wall_*: the stone wall's Health (3000, 2000, 1000; tower 4000) x 0.75; wood 60,
		// 40, 20 and 80; BuildTime 30, 20, 10 and 40 s.
		{BLD_SIEGE_WALL, BLD_SIEGE_WALL, {WALL_SEGMENT_LENGTHS[0], WALL_SEGMENT_LENGTHS[1], WALL_SEGMENT_LENGTHS[2]},
			SIEGE_WALL_TOWER_LENGTH, SIEGE_WALL_TOWER_HALF, 0.05f, 1.00f, SIEGE_WALL_HALF_DEPTH,
			{2250.0f, 1500.0f, 750.0f}, {60.0f, 40.0f, 20.0f}, {30.0f * 0.25f, 20.0f * 0.25f, 10.0f * 0.25f},
			3000.0f, 80.0f, 40.0f * 0.25f},
	};
	return kSets[kind < WALLSET_COUNT ? kind : 0];
}

inline WallSetInfo wallset_info(uint8_t kind, uint8_t civ) {
	WallSetInfo ws = wallset_base_info(kind);
	if (civ >= CIV_COUNT) return ws;
	if (ws.segment == BLD_WALL_SEGMENT) {
		const WallCivGeometry &g = wall_civ_geometry(civ);
		for (int k = 0; k < 3; ++k) ws.lengths[k] = g.lengths[k] / 4.0f;
		ws.tower_length = g.tower_length / 4.0f;
		ws.min_overlap = g.min_overlap;
		ws.max_overlap = g.max_overlap;
		ws.half_depth = g.depth / 4.0f * 0.5f;
	} else if (ws.segment == BLD_PALISADE && civ == CIV_HAN) {
		ws.max_overlap = 0.95f;
	}
	return ws;
}

// Gate.js holders: the stone gate, the palisade gate (a palisades_long Upgrade) and the Roman
// siege wall gate (a siege_wall_long Upgrade).
inline constexpr bool is_gate_type(uint8_t type) {
	return type == BLD_GATE || type == BLD_PALISADE_GATE || type == BLD_SIEGE_WALL_GATE;
}

inline bool is_palisade_tower(uint8_t type, float half_w) {
	return type == BLD_PALISADE && std::fabs(half_w - PALISADE_TOWER_HALF) < 0.01f;
}

inline bool is_siege_wall_tower(uint8_t type, float half_w) {
	return type == BLD_SIEGE_WALL && std::fabs(half_w - SIEGE_WALL_TOWER_HALF) < 0.01f;
}

// A WallSet tower held by its segments' type (a palisade tower, a siege wall tower).
inline bool is_wallset_tower_piece(uint8_t type, float half_w) {
	return is_palisade_tower(type, half_w) || is_siege_wall_tower(type, half_w);
}

// The WallSet a piece of `type` belongs to.
inline uint8_t wallset_kind_of(uint8_t type) {
	return type == BLD_PALISADE || type == BLD_PALISADE_GATE ? WALLSET_PALISADE
	       : type == BLD_SIEGE_WALL || type == BLD_SIEGE_WALL_GATE ? WALLSET_SIEGE : WALLSET_STONE;
}

// Whether a structure of half width `half_w` is a WallSet piece. The one-cell segment (and the
// gate it upgrades to) and palisade that maps and older saves place keep their square footprint
// and their building_type_info row.
inline bool is_wallset_piece(uint8_t type, float half_w) {
	if (type == BLD_PALISADE) return std::fabs(half_w - 1.0f) > 0.01f;
	if (type == BLD_PALISADE_GATE || type == BLD_SIEGE_WALL || type == BLD_SIEGE_WALL_GATE) return true;
	return (type == BLD_WALL_SEGMENT || type == BLD_GATE) && half_w > 1.01f;
}

// A WallSet piece's 0 A.D. values (wallset_info) by its type and length; negative when it keeps
// its building_type_info row. what: 0 Health, 1 Cost, 2 BuildTime.
// `civ` is the piece's template civ (Building::civ); the generic set's lengths are tried too, as
// maps lay Athenian pieces for every civ.
inline float wallset_piece_value(uint8_t type, float half_w, int what, uint8_t civ = CIV_COUNT) {
	if (!is_wallset_piece(type, half_w) || is_gate_type(type)) return -1.0f;
	const WallSetInfo ws = wallset_info(wallset_kind_of(type), civ);
	if (is_wallset_tower_piece(type, half_w)) return what == 0 ? ws.tower_health : what == 1 ? ws.tower_cost : ws.tower_build_time;
	const float *v = what == 0 ? ws.health : what == 1 ? ws.cost : ws.build_time;
	const WallSetInfo &generic = wallset_base_info(wallset_kind_of(type));
	for (const WallSetInfo *set : {&ws, &generic})
		for (int k = 0; k < 3; ++k)
			if (std::fabs(0.5f * set->lengths[k] - half_w) < 0.01f) return v[k];
	return v[0] * 2.0f * half_w / ws.lengths[0]; // any other length: its share of a long one
}

// Which of `civ`'s segments (0 long, 1 medium, 2 short) a WallSet segment of half length `half_w`
// is, by the civ's lengths or the generic ones maps lay; -1 for any other length.
inline int wallset_segment_index(uint8_t type, float half_w, uint8_t civ) {
	const WallSetInfo ws = wallset_info(wallset_kind_of(type), civ);
	const WallSetInfo &generic = wallset_base_info(wallset_kind_of(type));
	for (const WallSetInfo *set : {&ws, &generic})
		for (int k = 0; k < 3; ++k)
			if (std::fabs(0.5f * set->lengths[k] - half_w) < 0.01f) return k;
	return -1;
}

// Half the depth (along the footprint's v axis, nav/footprint.h) of a building of `type` whose half
// width is `half_w`, of `civ`'s template. RTS0's buildings are square but for WallSet segments,
// long and thin.
inline float building_half_depth(uint8_t type, float half_w, uint8_t civ = CIV_COUNT) {
	if (!is_wallset_piece(type, half_w) || is_wallset_tower_piece(type, half_w)) return half_w;
	return wallset_info(wallset_kind_of(type), civ).half_depth;
}

// 0 A.D. Identity/Requirements/Techs per structure: 1 = phase_village,
// 2 = phase_town, 3 = phase_city.
// The civ's own template where it differs: mace/arsenal and maur/elephant_stable are Town phase
// ("-phase_city phase_town"), as Macedonian siege and Mauryan elephants come a phase early.
inline uint8_t building_required_phase(uint8_t type, uint8_t civ = 0xFF) {
	if ((type == BLD_ARSENAL && civ == CIV_MACEDONIANS) || (type == BLD_ELEPHANT_STABLE && civ == CIV_MAURYANS)) return 2;
	switch (type) {
		case BLD_TOWER: case BLD_MARKET: case BLD_GATE: case BLD_TEMPLE:
		case BLD_WALL_SEGMENT: case BLD_FORGE: case BLD_WALL_TOWER: case BLD_EMBASSY:
			return 2;
		case BLD_FORTRESS: case BLD_WONDER: case BLD_ARSENAL:
		case BLD_ELEPHANT_STABLE: case BLD_THEATER:
			return 3;
		default:
			return type >= CIV_BUILDING_FIRST ? civ_building_phase(type) : 1;
	}
}

// 0 A.D. Upgrade component, for the structures RTS0 has both ends of:
// template_structure_defensive_tower_sentry.xml (<Tower>) and
// template_structure_defensive_wall_long.xml and palisades_long.xml (<Gate>). iber/sentry_tower.xml
// overrides the tower's stone cost and time (its wood cost is inherited).
struct BuildingUpgradeInfo {
	uint8_t to; // BLD_TYPE_COUNT: this structure has no upgrade
	int food_cost, wood_cost, gold_cost, stone_cost;
	float time;             // seconds
	uint8_t required_phase; // 1 village, 2 town (phase_town), 3 city
	const char *tooltip;
};

// The generic upgrades, which every civ has (one choice each).
inline BuildingUpgradeInfo generic_building_upgrade(uint8_t from, uint8_t civ) {
	switch (from) {
		case BLD_SENTRY_TOWER:
			if (civ == CIV_IBERIANS)
				return {BLD_TOWER, 0, 50, 0, 250, 120.0f, 2, "Reinforce with stone and upgrade to a defense tower."};
			return {BLD_TOWER, 0, 50, 0, 100, 100.0f, 2, "Reinforce with stone and upgrade to a defense tower."};
		case BLD_WALL_SEGMENT:
			return {BLD_GATE, 0, 20, 0, 0, 12.0f, 1, "Allow units access through Walls. Can be locked to prevent access."};
		case BLD_PALISADE: // palisades_long only (upgrade_check)
			return {BLD_PALISADE_GATE, 0, 10, 0, 0, 6.0f, 1, "Allow units access through Palisades. Can be locked to prevent access."};
		default:
			return {BLD_TYPE_COUNT, 0, 0, 0, 0, 0.0f, 1, nullptr};
	}
}

// Upgrade choice k (0-based) of a `from` structure owned by a `civ` player: the
// civ's own structure upgrades (civ_building_upgrade_table(), e.g. the Tachara's
// four tribute forms) first, then the generic one. `to` is BLD_TYPE_COUNT past the last.
inline BuildingUpgradeInfo building_upgrade(uint8_t from, uint8_t civ, int k = 0) {
	const uint16_t bit = civ < 16 ? (uint16_t)(1u << civ) : 0;
	for (const CivBuildingUpgrade *u = civ_building_upgrade_table(); u->from != BLD_TYPE_COUNT; ++u) {
		if (u->from != from || !(u->civs & bit)) continue;
		if (k-- == 0)
			return {u->to, u->food_cost, u->wood_cost, u->gold_cost, u->stone_cost, u->time, u->phase, u->tooltip};
	}
	if (k == 0) return generic_building_upgrade(from, civ);
	return {BLD_TYPE_COUNT, 0, 0, 0, 0, 0.0f, 1, nullptr};
}

// How many Upgrade choices building_upgrade() offers.
inline int building_upgrade_count(uint8_t from, uint8_t civ) {
	int n = 0;
	while (n < 8 && building_upgrade(from, civ, n).to != BLD_TYPE_COUNT) ++n;
	return n;
}

// 0 A.D. VisibleClasses "Village" (1) / "Town" (2): the structures that count
// toward phase_town_generic (five Village) and phase_city_generic (three Town).
// Civic centres, fields, towers and walls count toward neither.
// Civ structures carry their own: brit/kennel, ptol/mercenary_camp, han/ministry,
// kush/pyramid_small, spart/syssiton and pers/ice_house are Village; rome/army_camp
// and temple_vesta, kush/camp_blemmye and camp_noba, spart/gerousia, athen/gymnasium,
// iber/monument, cart/super_dock, mace/arsenal and maur/elephant_stable are Town.
inline uint8_t building_phase_class(uint8_t type, uint8_t civ = 0xFF) {
	if (type == BLD_TEMPLE_ISIS) return 0; // ptol/temple_2: VisibleClasses -Town City
	if (type == BLD_TENT) return 0; // rome/tent: VisibleClasses -Village
	if (type == BLD_STOA) return 2; // template_structure_civic_stoa: Town
	if (type == BLD_TAVERN || type == BLD_LAOZI_GATE) return 2; // gaul/tavern -Village Town, han/laozigate Town
	switch (building_research_base(type)) {
		case BLD_HOUSE: case BLD_APARTMENT: case BLD_BARRACKS: case BLD_STOREHOUSE: case BLD_FARMSTEAD:
		case BLD_DOCK: case BLD_CORRAL: case BLD_STABLE: case BLD_RANGE: case BLD_KENNEL:
		case BLD_MERCENARY_CAMP: case BLD_MINISTRY: case BLD_PYRAMID_SMALL: case BLD_SYSSITON: case BLD_ICE_HOUSE:
			return 1;
		case BLD_MARKET: case BLD_TEMPLE: case BLD_FORGE: case BLD_EMBASSY: case BLD_ARMY_CAMP:
		case BLD_TEMPLE_VESTA: case BLD_CAMP_BLEMMYE: case BLD_CAMP_NOBA: case BLD_GEROUSIA: case BLD_GYMNASIUM:
		case BLD_MONUMENT: case BLD_SUPER_DOCK: case BLD_TRADING_POST: // a shrine is -Town
			return 2;
		case BLD_ARSENAL: return civ == CIV_MACEDONIANS ? 2 : 0;
		case BLD_ELEPHANT_STABLE: return civ == CIV_MAURYANS ? 2 : 0;
		default:
			return 0;
	}
}

// 0 A.D. template_structure_resource_field ResourceSupply.
constexpr int32_t FIELD_FOOD_SUPPLY = 1000000000; // <Max>Infinity</Max>
constexpr int FIELD_MAX_GATHERERS = 5;            // <MaxGatherers>
// han/field.xml (the rice paddy) overrides it with 3.
inline int field_max_gatherers(uint8_t civ) { return civ == CIV_HAN ? 3 : FIELD_MAX_GATHERERS; }
constexpr float FIELD_DIMINISHING_RETURNS = 0.90f; // <DiminishingReturns>

// ResourceSupply.js GetDiminishingReturns: each extra gatherer on a field works
// at 0.9x the previous one; the returned factor scales every gatherer's rate.
inline float field_gather_efficiency(int gatherers) {
	if (gatherers <= 1) return 1.0f;
	float p = 1.0f;
	for (int i = 0; i < gatherers; ++i) p *= FIELD_DIMINISHING_RETURNS;
	return (1.0f - p) / (1.0f - FIELD_DIMINISHING_RETURNS) / (float)gatherers;
}

// 0 A.D. Obstruction BlockMovement=false: fields and the Han paifang (han/laozigate) don't block units.
inline bool building_blocks_movement(uint8_t type) {
	return type != BLD_FIELD && type != BLD_LAOZI_GATE;
}

inline const char *building_type_name(uint8_t type) { return building_type_info(type).name; }
inline const char *building_type_model(uint8_t type) { return building_type_info(type).model_id; }
inline const char *building_type_portrait(uint8_t type) { return building_type_info(type).portrait_id; }

// Technology modifiers: a subset of 0 A.D.'s ModificationTemplates paths that
// RTS0's simulation applies (tech_data.h maps each 0 A.D. path to one of these).
enum TechStat : uint8_t {
	TSTAT_NONE = 0,
	TSTAT_GATHER_GRAIN, TSTAT_GATHER_FRUIT, TSTAT_GATHER_MEAT, TSTAT_GATHER_FISH,
	TSTAT_GATHER_WOOD, TSTAT_GATHER_STONE, TSTAT_GATHER_METAL,
	TSTAT_CARRY_FOOD, TSTAT_CARRY_WOOD, TSTAT_CARRY_STONE, TSTAT_CARRY_METAL,
	TSTAT_MELEE_HACK, TSTAT_MELEE_PIERCE, TSTAT_MELEE_CRUSH,
	TSTAT_RANGED_HACK, TSTAT_RANGED_PIERCE, TSTAT_RANGED_CRUSH,
	TSTAT_ARMOR_HACK, TSTAT_ARMOR_PIERCE, TSTAT_ARMOR_CRUSH,
	TSTAT_HEALTH, TSTAT_SPEED, TSTAT_POP_BONUS, TSTAT_TRADE_GAIN, TSTAT_RANGE,
	TSTAT_BUILD_TIME, TSTAT_RESEARCH_TIME, TSTAT_HEAL_RANGE, TSTAT_HEAL_INTERVAL, TSTAT_ARROWS,
	TSTAT_PACK_TIME, TSTAT_GARRISON_HEAL,
	TSTAT_REGEN, TSTAT_IDLE_REGEN, // Health/RegenRate, Health/IdleRegenRate (hp per second)
	TSTAT_CAPTURE_POINTS, TSTAT_COST_FOOD, TSTAT_COST_WOOD, TSTAT_COST_STONE, TSTAT_COST_METAL,
	TSTAT_TRICKLE_FOOD, TSTAT_TRICKLE_WOOD, TSTAT_TRICKLE_STONE, TSTAT_TRICKLE_METAL, // ResourceTrickle/Rates/<res>
	TSTAT_TRADE_INTERNATIONAL, // Market/InternationalBonus of markets and docks
	// Paths only global relic auras change so far (relic_aura_data.h).
	TSTAT_GATHER_ALL,       // ResourceGatherer/BaseSpeed
	TSTAT_VISION,           // Vision/Range
	TSTAT_TERRITORY_RADIUS, // TerritoryInfluence/Radius
	TSTAT_CAPTURE_ATTACK,   // Attack/Capture/Capture
	TSTAT_MAX_POP,          // Player/MaxPopulation (the match's population limit)
	TSTAT_REQUIRED_XP,      // Promotion/RequiredXp
	TSTAT_TECH_COST_FOOD, TSTAT_TECH_COST_WOOD, TSTAT_TECH_COST_STONE, TSTAT_TECH_COST_METAL, // Researcher/TechCostMultiplier
	TSTAT_LOOT_FOOD, TSTAT_LOOT_WOOD, TSTAT_LOOT_STONE, TSTAT_LOOT_METAL, // Looter/Resource/<res>
	TSTAT_BATCH_TIME, // Trainer/BatchTimeModifier of training structures (added)
	TSTAT_SPREAD,          // Attack/Ranged/Projectile/Spread
	TSTAT_GARRISON_ARROWS, // BuildingAI/GarrisonArrowMultiplier
	TSTAT_MIN_RANGE,       // Attack/Ranged/MinRange of structures
	TSTAT_GARRISON_REGEN,  // Capturable/GarrisonRegenRate
	TSTAT_TERRITORY_DECAY, // TerritoryDecay/DecayRate (Migratory Resettlement)
	TSTAT_UNIT_LOOT_FOOD, TSTAT_UNIT_LOOT_WOOD, TSTAT_UNIT_LOOT_STONE, TSTAT_UNIT_LOOT_METAL, // Loot/<res> of the unit itself
	TSTAT_MELEE_FIRE, TSTAT_RANGED_FIRE, // Attack/<type>/Damage/Fire (fire ships)
	TSTAT_COUNT
};

enum TechOp : uint8_t { TOP_MUL = 0, TOP_ADD = 1, TOP_SET = 2 }; // TOP_SET: 0 A.D. "replace"

struct TechEffect {
	uint8_t stat;          // TechStat
	uint8_t op;            // TechOp
	uint16_t affects;      // first row in tech_affects_table(), 0 = applies to everything
	uint16_t affects_count; // alternatives (OR); each row is an AND of classes
	float value;
};

struct TechInfo {
	const char *name;
	const char *desc;
	int food_cost;
	int wood_cost;
	int gold_cost;
	int stone_cost;
	float research_time;
	uint8_t required_phase; // 1 = village, 2 = town, 3 = city
	uint8_t researcher;     // BuildingType that researches it
	uint8_t prerequisite;   // another TechnologyId, or TECH_NONE
	uint16_t civ_mask;      // bit per Civilization allowed to research it
	uint16_t effect_begin;  // index into tech_effects_table()
	uint16_t effect_count;
	const char *template_name; // 0 A.D. technology file name
};

#include "tech_data.h"
#include "building_ai_data.h"
#include "trainer_data.h"
static_assert(TRAINER_BUILDING_ROWS == BLD_TYPE_COUNT && TRAINER_CIV_ROWS == CIV_COUNT,
	"re-run tools/extract_0ad_trainer.py after adding buildings or civs");

// Researched technologies for one player. 256 bits covers every uint8_t id.
struct TechSet {
	uint64_t words[4] = {};
	bool has(uint8_t id) const { return (words[id >> 6] >> (id & 63)) & 1u; }
	void set(uint8_t id) { words[id >> 6] |= 1ull << (id & 63); }
	void clear() { words[0] = words[1] = words[2] = words[3] = 0; }
	bool empty() const { return (words[0] | words[1] | words[2] | words[3]) == 0; }
	bool operator==(const TechSet &o) const {
		return words[0] == o.words[0] && words[1] == o.words[1] && words[2] == o.words[2] && words[3] == o.words[3];
	}
};

inline bool tech_available_to_civ(uint8_t tech_id, uint8_t civ) {
	return civ < 16 && (tech_info(tech_id).civ_mask >> civ) & 1u;
}

// Can a building of this type research the tech for a player of this civ? Its research
// base's list, with 0.28's per-structure Researcher changes ("-" drops a token):
//  - the crannog drops phase_town and hellenistic_metropolis, the Han Imperial Court
//    phase_town, and the military colony (template_structure_civic_civil_centre_military_colony)
//    both phases and hellenistic_metropolis;
//  - Han: the Civic Center (and court) drop unlock_spies and spy_counter, which the
//    ministry researches; the forge drops archer_attack_spread, which the barracks and
//    stable research;
//  - the Mauryan palace researches unlock_spies and spy_counter too;
//  - Carthage: the dock drops warship_health, and the super dock (cart/super_dock) researches
//    it and eight other dock techs;
//  - a tech's researcher is one structure here, but 0.28 lists some at several: the market
//    (template_structure_economic_market) researches unlock_shared_los like the Civic Center,
//    the fortress tower_murderholes and the sentry tower tower_watch like the stone tower,
//    athen/gymnasium iphicratean_reforms like the dock, and rome/temple_vesta and
//    kush/temple_amun inherit template_structure_civic_temple's healer techs;
//  - no 0.28 structure researches agoge, and only cart/embassy_celtic (not the Iberian or
//    Italic embassy) celtic_auxiliaries.
inline bool building_researches_tech(uint8_t type, uint8_t tech_id, uint8_t civ) {
	const uint8_t r = tech_info(tech_id).researcher;
	if (tech_id == TECH_AGOGE) return false;
	if (tech_id == TECH_CELTIC_AUXILIARIES) return type == BLD_EMBASSY;
	if (tech_id == TECH_UNLOCK_SHARED_LOS && building_research_base(type) == BLD_MARKET) return true;
	if (tech_id == TECH_TOWER_MURDERHOLES && (type == BLD_FORTRESS || type == BLD_FORTRESS_OLD)) return true;
	if (tech_id == TECH_TOWER_WATCH && type == BLD_SENTRY_TOWER) return true;
	if (tech_id == TECH_IPHICRATEAN_REFORMS && type == BLD_GYMNASIUM) return true;
	if ((type == BLD_TEMPLE_VESTA || type == BLD_TEMPLE_AMUN || type == BLD_TEMPLE_MARS) && r == BLD_TEMPLE) return true;
	// ptol/mercenary_camp keeps template_structure_military_barracks' Researcher list.
	if (type == BLD_MERCENARY_CAMP && r == BLD_BARRACKS) return true;
	const bool spies = tech_id == TECH_UNLOCK_SPIES || tech_id == TECH_SPY_COUNTER;
	if (civ == CIV_HAN) {
		if (spies) return type == BLD_MINISTRY;
		if (tech_id == TECH_ARCHER_ATTACK_SPREAD) return type == BLD_BARRACKS || type == BLD_STABLE;
	}
	if (spies && type == BLD_PALACE) return true;
	if (civ == CIV_CARTHAGINIANS) {
		if (type == BLD_DOCK && tech_id == TECH_WARSHIP_HEALTH) return false;
		if (type == BLD_SUPER_DOCK) {
			switch (tech_id) {
			case TECH_DOCK_EFFICIENCY: case TECH_SHIP_VISION: case TECH_EXPLORATION:
			case TECH_WARSHIP_ARROW_ATTACK: case TECH_WARSHIP_RAMMING_ATTACK: case TECH_WARSHIP_FIRESHIP_ATTACK:
			case TECH_WARSHIP_SIEGE_ATTACK: case TECH_WARSHIP_RANGED_ATTACK: case TECH_WARSHIP_HEALTH: return true;
			default: break;
			}
		}
	}
	switch (type) {
	case BLD_CRANNOG:
		return r == BLD_TOWN_CENTER && tech_id != TECH_PHASE_TOWN && tech_id != TECH_HELLENISTIC_METROPOLIS;
	case BLD_IMPERIAL_COURT: return r == BLD_TOWN_CENTER && tech_id != TECH_PHASE_TOWN;
	case BLD_MILITARY_COLONY:
		return r == BLD_MILITARY_COLONY || (r == BLD_TOWN_CENTER && tech_id != TECH_PHASE_TOWN
		                                    && tech_id != TECH_PHASE_CITY && tech_id != TECH_HELLENISTIC_METROPOLIS);
	// germ/encampment shares the military colony's parent template and adds wagon_trains; its
	// fortified Upgrade keeps the list.
	case BLD_WAGON_ENCAMPMENT: case BLD_WAGON_ENCAMPMENT_FORTIFIED:
		return r == BLD_WAGON_ENCAMPMENT || (r == BLD_TOWN_CENTER && tech_id != TECH_PHASE_TOWN
		                                     && tech_id != TECH_PHASE_CITY && tech_id != TECH_HELLENISTIC_METROPOLIS
		                                     && tech_id != TECH_RESETTLEMENT); // germ/civil_centre's own
	// mixins/shrine.xml: Researcher disable="". mixins/trading_post.xml drops the market's trade
	// technologies (trader_health, trade_gain_01/02, trade_commercial_treaty).
	case BLD_SHRINE: return false;
	case BLD_TRADING_POST: return tech_id == TECH_UNLOCK_SHARED_LOS || tech_id == TECH_UNLOCK_SHARED_DROPSITES;
	// template_structure_defensive_tower_bolt and _artillery research only tower_health;
	// rome/tent has Researcher disable="" (pers/inn keeps the house's list).
	case BLD_TOWER_BOLT: case BLD_TOWER_ARTILLERY: return tech_id == TECH_TOWER_HEALTH;
	case BLD_TENT: return false;
	case BLD_KUSH_SHRINE: return false; // kush/shrine: Researcher disable=
	// ptol/temple_2 adds pair_unlock_cult_ptol (Pharaonic and Serapis Cult) to the temple's list.
	case BLD_TEMPLE_ISIS: return r == BLD_TEMPLE || r == BLD_TEMPLE_ISIS;
	default: return r == building_research_base(type);
	}
}

// Does an entity with these TechClass bits match effect `e`'s affects list?
inline bool tech_effect_applies(const TechEffect &e, TechClasses classes) {
	if (e.affects_count == 0) return true;
	const TechClassExpr *rows = tech_affects_table() + e.affects;
	for (uint16_t i = 0; i < e.affects_count; ++i)
		if ((classes & rows[i].need) == rows[i].need && (classes & rows[i].forbid) == 0) return true;
	return false;
}

// 0 A.D. Identity/Classes for RTS0's unit types, as far as technologies need them.
inline TechClasses unit_tech_classes(uint8_t type) {
	const TechClasses org = TC_UNIT | TC_ORGANIC;
	const TechClasses cs = org | TC_SOLDIER | TC_CITIZENSOLDIER | TC_CITIZEN;
	switch (type) {
		case UNIT_VILLAGER: return org | TC_WORKER | TC_CITIZEN | TC_CIVILIAN;
		case UNIT_FEMALE_CITIZEN: return org | TC_WORKER | TC_CITIZEN | TC_CIVILIAN | TC_FEMALECITIZEN;
		case UNIT_SWORDSMAN: return cs | TC_INFANTRY | TC_MELEE | TC_SWORDSMAN;
		case UNIT_ARCHER: return cs | TC_INFANTRY | TC_RANGED | TC_ARCHER;
		case UNIT_SPEARMAN: return cs | TC_INFANTRY | TC_MELEE | TC_SPEARMAN;
		case UNIT_CAVALRY: return cs | TC_CAVALRY | TC_MELEE;
		case UNIT_CAMEL: return cs | TC_CAVALRY | TC_MELEE;
		case UNIT_TRADER: return org | TC_TRADER | TC_CIVILIAN;
		case UNIT_HEALER: return org | TC_HEALER | TC_CIVILIAN;
		case UNIT_MINISTER: return org | TC_CIVILIAN | TC_MINISTER; // Support (Civilian) Minister
		case UNIT_HERO: return org | TC_HERO | TC_SOLDIER | TC_INFANTRY | TC_MELEE; // template_unit_hero VisibleClasses: Soldier
		case UNIT_ELEPHANT: return org | TC_SOLDIER | TC_ELEPHANT | TC_CHAMPION | TC_MELEE;
		case UNIT_CHARIOT: return org | TC_SOLDIER | TC_CAVALRY | TC_CHARIOT | TC_RANGED | TC_CHAMPION;
		case UNIT_SCYTHED_CHARIOT: return org | TC_SOLDIER | TC_CAVALRY | TC_CHARIOT | TC_MELEE | TC_CHAMPION;
		case UNIT_WAR_DOG: return org | TC_SOLDIER | TC_DOG | TC_MELEE;
		case UNIT_CARNYX: return org | TC_SOLDIER | TC_INFANTRY | TC_MELEE;
		case UNIT_ARCHER_MAIDEN: return org | TC_SOLDIER | TC_INFANTRY | TC_RANGED | TC_ARCHER | TC_CHAMPION;
		case UNIT_WORKER_ELEPHANT: return org | TC_WORKER | TC_ELEPHANT;
		case UNIT_WAGON: return org | TC_CIVILIAN | TC_WAGON; // Support (Civilian) Wagon
		case UNIT_CATAPULT: return TC_UNIT | TC_SIEGE | TC_RANGED | TC_STONETHROWER;
		case UNIT_BOLT_SHOOTER: return TC_UNIT | TC_SIEGE | TC_RANGED | TC_BOLTSHOOTER;
		case UNIT_SIEGE_RAM: return TC_UNIT | TC_SIEGE | TC_MELEE | TC_RAM;
		case UNIT_TRIREME: return TC_UNIT | TC_SHIP | TC_WARSHIP | TC_ARROWSHIP | TC_RANGED;
		case UNIT_FISHING_BOAT: return TC_UNIT | TC_SHIP | TC_FISHINGBOAT;
		case UNIT_MERCHANT_SHIP: return TC_UNIT | TC_SHIP | TC_TRADER;
		default: return TC_UNIT;
	}
}

// 0 A.D. Identity class Human (every organic unit, elephants and dogs too): what Heal.js
// HealableClasses on template_unit_support_healer.xml accepts.
inline bool unit_is_human(uint8_t type) {
	// Elephants, dogs, champions and heroes all carry Human (template_unit_elephant,
	// template_unit_dog, template_unit_champion, template_unit_hero).
	return (unit_tech_classes(type) & TC_ORGANIC) != 0;
}

// 0 A.D. GarrisonHolder on land siege: template_unit_siege_tower.xml (Max 20) and
// template_unit_siege_ram.xml (Max 10) take <List>Support Infantry</List> and
// unload everyone below EjectHealth 0.1 or when destroyed. The siege tower's
// BuildingAI fires no arrows of its own (DefaultArrowCount 0) and one per
// garrisoned Infantry (GarrisonArrowMultiplier 1), up to MaxArrowCount 10.
// 0 A.D. GarrisonHolder on structures (template_structure_*.xml): BuffHeal is the
// hitpoints each garrisoned unit regains per second; below EjectHealth of its
// maximum health a structure unloads its garrison and takes no one in. 0 = no
// garrison in 0 A.D. (storehouse, farmstead, market, gate, dock, walls, outpost).
inline float building_garrison_heal(uint8_t type) {
	switch (building_research_base(type)) {
		// han/ministry, ptol and sele military_colony (template_structure_civic_civil_centre_military_colony)
		// and germ/encampment BuffHeal 1
		case BLD_TOWN_CENTER: case BLD_CORRAL: case BLD_MINISTRY: case BLD_MILITARY_COLONY:
		case BLD_WAGON_ENCAMPMENT: return 1.0f;
		// kush/temple_amun, rome/temple_mars, rome/temple_vesta and the shrines keep template_structure_civic_temple's 3
		case BLD_TEMPLE: case BLD_TEMPLE_AMUN: case BLD_TEMPLE_MARS: case BLD_TEMPLE_VESTA: case BLD_SHRINE:
		case BLD_TEMPLE_ISIS: return 3.0f;
		case BLD_WONDER: return 5.0f;
		default: return 0.0f;
	}
}
// 0 A.D. structures/wall_garrisoned on walls, gates and outposts: turreted Soldiers
// +3 resistance to each damage type and +20 m vision (x0.28 like every Vision range).
inline constexpr float WALL_TURRET_ARMOR = 3.0f;
inline constexpr float WALL_TURRET_VISION = 20.0f * 0.28f;
// han/ministry unloads only when destroyed (EjectHealth 0); han/storehouse and
// han/farmstead seat a Minister with the usual 0.1.
inline float building_eject_health(uint8_t type, uint8_t civ) {
	switch (type) {
		case BLD_FORTRESS: case BLD_FORTRESS_OLD: return 0.075f; // han/fortress_old: a fortress child
		case BLD_CORRAL: return 0.5f;
		case BLD_MINISTRY: return 0.0f;
		case BLD_STOREHOUSE: case BLD_FARMSTEAD: return civ == CIV_HAN ? 0.1f : 0.0f;
		case BLD_MARKET: case BLD_GATE: case BLD_DOCK:
		case BLD_WALL_SEGMENT: case BLD_FIELD: case BLD_OUTPOST: case BLD_PALISADE: case BLD_PALISADE_GATE:
		case BLD_SIEGE_WALL: case BLD_SIEGE_WALL_GATE: return 0.0f;
		default: return 0.1f;
	}
}

inline constexpr int SIEGE_TOWER_GARRISON = 20;
inline constexpr int SIEGE_RAM_GARRISON = 10;
inline constexpr int SIEGE_TOWER_MAX_ARROWS = 10;
inline constexpr float SIEGE_EJECT_HEALTH = 0.1f;
inline TechClasses building_tech_classes(uint8_t type) {
	const TechClasses c = TC_STRUCTURE; // 0 A.D.'s Village/Town/City classes: no technology tests them
	switch (type) {
		// The Crannog and the military colony are template_structure_civic_civil_centre too,
		// whose VisibleClasses are Defensive CivilCentre (Danubius and Jebel Barkal aim at them).
		case BLD_TOWN_CENTER: case BLD_IMPERIAL_COURT: case BLD_CRANNOG: return c | TC_CIVCENTRE | TC_CIVIC | TC_DEFENSIVE;
		case BLD_HOUSE: case BLD_APARTMENT: return c | TC_HOUSE | TC_CIVIC;
		// han/academy and ptol/mercenary_camp are template_structure_military_barracks.
		case BLD_BARRACKS: case BLD_ACADEMY: case BLD_MERCENARY_CAMP: return c | TC_BARRACKS;
		case BLD_STOREHOUSE: return c | TC_STOREHOUSE;
		case BLD_TOWER: case BLD_TOWER_GREAT: case BLD_TOWER_DOUBLE: return c | TC_TOWER | TC_STONETOWER | TC_DEFENSIVE;
		case BLD_SENTRY_TOWER: return c | TC_TOWER | TC_SENTRYTOWER | TC_DEFENSIVE;
		case BLD_TOWER_BOLT: case BLD_TOWER_ARTILLERY: return c | TC_TOWER | TC_DEFENSIVE; // not StoneTower
		case BLD_INN: case BLD_TENT: case BLD_WAREHOUSE: case BLD_TAVERN: return c | TC_HOUSE | TC_CIVIC;
		case BLD_STOA: case BLD_HELLENIC_STOA: return c | TC_CIVIC;
		case BLD_WALL_TOWER: return c | TC_WALL | TC_DEFENSIVE;
		case BLD_FARMSTEAD: return c | TC_FARMSTEAD;
		case BLD_FIELD: return c | TC_FIELD;
		case BLD_CORRAL: return c | TC_CORRAL;
		case BLD_MARKET: case BLD_TRADING_POST: return c | TC_MARKET;
		case BLD_FORTRESS: case BLD_FORTRESS_OLD: return c | TC_FORTRESS | TC_DEFENSIVE;
		case BLD_GATE: case BLD_WALL_SEGMENT:
		case BLD_SIEGE_WALL: case BLD_SIEGE_WALL_GATE: return c | TC_WALL | TC_DEFENSIVE;
		// template_structure_defensive_palisade: Defensive Palisade, no Wall.
		case BLD_PALISADE: case BLD_PALISADE_GATE: return c | TC_DEFENSIVE;
		case BLD_TEMPLE: case BLD_SHRINE: case BLD_KUSH_SHRINE: case BLD_TEMPLE_ISIS: return c | TC_TEMPLE | TC_CIVIC;
		// Other template_structure_civic children. The Kushite temple's TempleOfApedemak
		// auras reach RTS0 as Temple, so the Temple of Amun stays out of that class.
		case BLD_TEMPLE_AMUN: case BLD_TEMPLE_VESTA: case BLD_TEMPLE_MARS:
		case BLD_PYRAMID_LARGE: case BLD_PYRAMID_SMALL: case BLD_MINISTRY: return c | TC_CIVIC;
		case BLD_WONDER: return c | TC_WONDER;
		case BLD_DOCK: return c | TC_DOCK;
		case BLD_STABLE: return c | TC_STABLE;
		case BLD_ELEPHANT_STABLE: return c; // template_structure_military_elephant_stable: ElephantStable, no Stable
		case BLD_FORGE: return c | TC_FORGE;
		case BLD_ARSENAL: return c | TC_ARSENAL;
		case BLD_OUTPOST: return c | TC_OUTPOST | TC_DEFENSIVE;
		case BLD_ICE_HOUSE: return c | TC_ICEHOUSE;
		case BLD_MILITARY_COLONY: return c | TC_CIVCENTRE | TC_CIVIC | TC_DEFENSIVE | TC_COLONY;
		// germ/encampment: VisibleClasses Civic Defensive Colony Encampment (no CivCentre).
		case BLD_WAGON_ENCAMPMENT: case BLD_WAGON_ENCAMPMENT_FORTIFIED: return c | TC_CIVIC | TC_DEFENSIVE | TC_COLONY;
		default: return c;
	}
}

// Stone walls and palisades alike: the pieces an attack-move steers clear of and Petra
// finds in its way (0.28's palisades have no Wall class, so technologies skip them).
inline bool is_wall_piece(uint8_t type) {
	return (building_tech_classes(type) & TC_WALL) || type == BLD_PALISADE || type == BLD_PALISADE_GATE;
}

// Whether an attack-move with these avoid bits skips a structure of `type`.
inline bool attack_move_avoids(uint8_t flags, uint8_t type) {
	TechClasses c = building_tech_classes(type);
	if (is_wall_piece(type)) c |= TC_WALL;
	if ((flags & ORDER_AVOID_ECONOMY) && (c & (TC_HOUSE | TC_STOREHOUSE | TC_FARMSTEAD | TC_FIELD | TC_FORGE))) return true;
	switch ((flags & ORDER_AVOID_MASK) >> ORDER_AVOID_SHIFT) {
		case ORDER_AVOID_WALLS_FORTRESS: return (c & (TC_WALL | TC_FORTRESS)) != 0;
		case ORDER_AVOID_WALLS_TOWERS: return (c & (TC_WALL | TC_TOWER | TC_FORTRESS)) != 0;
		case ORDER_AVOID_WALLS: return (c & TC_WALL) != 0;
		default: return false;
	}
}

// 0 A.D. GarrisonHolder/Max where a civ's own template differs from the
// building_type_info row (template_structure_*.xml): the small house
// (template_structure_civic_house, 3) for Britons, Gauls, Iberians, Mauryas and
// Ptolemies, the big house (template_structure_civic_house_big, 6) for the rest;
// iber/defense_tower 8; han/storehouse and han/farmstead 1 (a Minister's seat).
inline int structure_garrison_max(uint8_t type, uint8_t civ) {
	switch (type) {
		case BLD_HOUSE:
			return civ < CIV_COUNT && !civ_has_big_house(civ) ? 3 : 6;
		case BLD_TOWER: return civ == CIV_IBERIANS ? 8 : building_type_info(type).garrison_cap;
		case BLD_STOREHOUSE: case BLD_FARMSTEAD: return civ == CIV_HAN ? 1 : 0;
		default: return building_type_info(type).garrison_cap;
	}
}

// 0 A.D. GarrisonHolder/List (MatchesClassList: any entry, "A+B" needs both, "!A" forbids
// A). Support is RTS0's TC_CIVILIAN. corral "Animal" takes no unit (penned animals are fauna);
// the Han lists add Minister (structures/han/*.xml), all but the sentry and wall towers.
struct GarrisonClassList { uint8_t n; TechClassExpr e[5]; };
inline GarrisonClassList structure_garrison_list(uint8_t type, uint8_t civ) {
	constexpr TechClasses SUP = TC_CIVILIAN, INF = TC_INFANTRY, CAV = TC_CAVALRY;
	GarrisonClassList l{};
	auto add = [&](TechClasses need, TechClasses forbid = 0) { l.e[l.n++] = {need, forbid}; };
	switch (building_research_base(type)) { // the court and great tower keep their parent's List
		case BLD_TOWN_CENTER: case BLD_TEMPLE: case BLD_EMBASSY: case BLD_THEATER: case BLD_GYMNASIUM:
		case BLD_PRYTANEION: case BLD_GEROUSIA: case BLD_SYSSITON: case BLD_TACHARA: case BLD_AMPHITHEATER:
		case BLD_LIBRARY: case BLD_ASSEMBLY: case BLD_PALACE: case BLD_LIGHTHOUSE: case BLD_MILITARY_COLONY:
		case BLD_CAMP_BLEMMYE: case BLD_CAMP_NOBA: case BLD_TEMPLE_AMUN: case BLD_TEMPLE_VESTA:
		case BLD_TEMPLE_MARS: case BLD_TACHARA_FOOD: case BLD_TACHARA_WOOD: case BLD_TACHARA_STONE:
		case BLD_TACHARA_METAL: case BLD_WAGON_ENCAMPMENT: case BLD_GREAT_HALL: case BLD_SHRINE: case BLD_STOA: case BLD_HALL:
			add(SUP); add(INF); add(CAV); break; // template_structure_civic_civil_centre, _special, ...
		case BLD_HOUSE: case BLD_APARTMENT: add(SUP, TC_ELEPHANT); break; // Support+!Elephant
		case BLD_BARRACKS: case BLD_RANGE: case BLD_MERCENARY_CAMP: case BLD_WALL_TOWER: add(INF); break;
		case BLD_STABLE: add(CAV); break;
		case BLD_TOWER: case BLD_SENTRY_TOWER: add(SUP); add(INF); break; // template_structure_defensive_tower
		case BLD_FORTRESS: case BLD_ARMY_CAMP: add(SUP); add(INF); add(CAV); add(TC_SIEGE); break;
		case BLD_WONDER: add(SUP); add(TC_SOLDIER); break;
		case BLD_FORGE: add(INF); add(TC_HEALER); break;
		case BLD_ARSENAL: add(TC_SIEGE); break;
		case BLD_ELEPHANT_STABLE: add(TC_ELEPHANT); break;
		case BLD_KENNEL: add(TC_DOG); break;
		case BLD_ACADEMY: add(INF); add(CAV); break;
		case BLD_MINISTRY: add(SUP); add(INF); break;
		case BLD_SUPER_DOCK: add(SUP); add(INF); add(CAV); add(TC_SHIP); break;
		default: break; // storehouse, farmstead, corral, market, dock, ...: none
	}
	if (civ == CIV_HAN && type != BLD_SENTRY_TOWER && type != BLD_WALL_TOWER && l.n < 5) add(TC_MINISTER);
	return l;
}
inline bool garrison_list_matches(const GarrisonClassList &l, TechClasses classes) {
	for (uint8_t k = 0; k < l.n; ++k)
		if ((classes & l.e[k].need) == l.e[k].need && (classes & l.e[k].forbid) == 0) return true;
	return false;
}

inline constexpr uint32_t unit_bit(uint8_t type) { return (type < 32) ? (1u << type) : 0u; }

// A set of BuildingTypes (roster trainers, civ and team bonus targets): 128 bits,
// as BuildingType outgrew 64 ids. Read-only data; nothing saved uses it.
struct BldMask {
	uint64_t w[2] = {};
	constexpr BldMask operator|(BldMask o) const { BldMask r; r.w[0] = w[0] | o.w[0]; r.w[1] = w[1] | o.w[1]; return r; }
	constexpr BldMask operator&(BldMask o) const { BldMask r; r.w[0] = w[0] & o.w[0]; r.w[1] = w[1] & o.w[1]; return r; }
	constexpr BldMask &operator|=(BldMask o) { w[0] |= o.w[0]; w[1] |= o.w[1]; return *this; }
	constexpr bool operator==(const BldMask &) const = default;
	constexpr explicit operator bool() const { return (w[0] | w[1]) != 0; }
	constexpr bool test(uint8_t type) const { return type < 128 && ((w[type >> 6] >> (type & 63)) & 1u); }
};
static_assert(BLD_TYPE_COUNT <= 128, "widen BldMask");
inline constexpr BldMask bld_bit(uint8_t type) {
	BldMask m;
	if (type < 128) m.w[type >> 6] = 1ull << (type & 63);
	return m;
}

// ---------------------------------------------------------------------------
// Per-civ unit rosters, generated from 0 A.D.'s templates by
// tools/extract_0ad_rosters.py into civ_roster_data.h.
// Each civ unit (Athenian Hoplite, Han Crossbowman, Pericles, ...) carries
// 0 A.D.'s own stats and names the RTS0 UnitType that drives its behaviour,
// model fallback and AI role.
// ---------------------------------------------------------------------------

enum CivRosterFlag : uint16_t {
	ROSTER_HERO = 1,
	ROSTER_CHAMPION = 2,
	ROSTER_RANGED = 4,
	ROSTER_CITIZEN = 8,
	ROSTER_MERCENARY = 16,
	ROSTER_PACKABLE = 32, // 0 A.D. Pack component (10 s to pack or unpack)
	ROSTER_SIEGE_TOWER = 64,
	ROSTER_RELIC = 128, // a catafalque (template_unit_catafalque): Capture the Relic
	ROSTER_NO_GATHER = 256, // a Citizen whose template disables ResourceGatherer (mercenaries)
	ROSTER_CIVILIAN = 512, // 0.28's Civilian class (template_unit_support_civilian): StatisticsTracker's Civilian
	ROSTER_MAP_ONLY = 1024 // only maps place it (scenario heroes, Samnites, pirate ships): no structure trains it
};

inline constexpr uint16_t civ_bit(uint8_t civ) { return (civ < 16) ? (uint16_t)(1u << civ) : 0u; }

// Roster tables hold at most 64 * CIV_TEMPLATE_WORDS_MAX unit templates.
inline constexpr uint16_t CIV_TEMPLATE_WORDS_MAX = 8;

// A unit's short path around buildings (nav/building_steering.h), kept between ticks as 0 A.D.'s
// CCmpUnitMotion keeps m_ShortPath: the first waypoint (x, z), the aim it leads towards, and the
// obstacle key it was searched among. key 0 = no short path. Saved raw (no padding).
struct ShortPathMemo {
	float x = 0.0f, z = 0.0f;
	float aim_x = 0.0f, aim_z = 0.0f;
	uint32_t key = 0;
};
static_assert(sizeof(ShortPathMemo) == 20, "ShortPathMemo is saved raw");

// A unit's last short path search and what it found, kept beside the saved state, not in it: a
// search is a function of these inputs alone, so while they are the same bit for bit (a unit held
// where it stands, which searches again each tick for the same aim) its answer is reused, and a
// game loaded or joined, which starts without it, searches and finds the same.
struct ShortPathSearchMemo {
	uint32_t pos_x = 0, pos_z = 0, aim_x = 0, aim_z = 0, radius = 0, range = 0; // float bits
	uint32_t key = 0;     // the buildings (ShortPathMemo::key)
	uint32_t ignore = 0;  // the building not steered round
	uint32_t terrain = 0; // NavGrid::terrain_version
	uint64_t small = 0;   // the small obstacles' contents (small_obstacles_hash), 0 for none
	float out_x = 0.0f, out_z = 0.0f;
	bool found = false, valid = false;
};

// CCmpUnitMotion's m_FailedMovements and the move of the 0 A.D. turn under way (200 ms, four RTS0
// ticks), which motion_feedback() judges when the turn ends, as UnitMotion judges each turn's Move,
// and CCmpUnitMotionManager's pushing pressure. Saves before pressure held 0 in its byte.
struct MotionTurn {
	float from_x = 0.0f, from_z = 0.0f; // where the unit stood before its first step this turn
	float want_x = 0.0f, want_z = 0.0f; // the steps it tried this turn, summed
	uint8_t failed = 0;  // m_FailedMovements: obstructed turns in a row
	uint8_t stepped = 0; // it tried a step this turn
	uint8_t blocked = 0; // a closed gate stopped one of this turn's steps
	uint8_t pressure = 0; // MotionState::pushingPressure (0-255): how pushed the unit is; kept when a turn resets
};
static_assert(sizeof(MotionTurn) == 20, "MotionTurn is saved raw");

struct CivHeroAura {
	const char *id;
	const char *name;
	const char *description;
	float radius; // 0 A.D. metres; 0 = applies everywhere
	// Strongest effect RTS0's AuraManager can model (AuraType; 255 = none yet).
	uint8_t kind;
	float value;
	float range; // RTS0 units
	uint8_t players; // AuraPlayers bits (0 A.D. affectedPlayers)
	uint64_t affects[CIV_TEMPLATE_WORDS_MAX]; // bitset of roster rows the aura buffs
	bool structures; // "Structure" in its affects classes (Antiochus IV's Health/Max on enemy structures)
};

enum CivBonusStat : uint8_t {
	CIV_BONUS_BUILDING_HP = 1,
	CIV_BONUS_BUILDING_BUILD_TIME = 2,
	CIV_BONUS_BUILDING_WOOD_COST = 3,
	CIV_BONUS_BUILDING_STONE_COST = 4,
	CIV_BONUS_BUILDING_CAPTURE_POINTS = 5,
	CIV_BONUS_POP_CAP = 6,
	CIV_BONUS_BUILDING_LOOT_WOOD = 7,     // Loot/wood (Ptolemaic houses and economic structures, Seleucid fields)
	CIV_BONUS_BUILDING_RESEARCH_TIME = 8 // Researcher/TechCostMultiplier/time (Macedonian storehouse: 0)
};

// An auto-researched 0 A.D. civ bonus acting on buildings or the player.
struct CivPlayerBonus {
	uint16_t civs; // civ_bit() mask
	uint8_t stat; // CivBonusStat
	BldMask buildings; // bld_bit() mask (empty for player-wide stats)
	float mult;
	const char *id;
	const char *tooltip;
};

// What a 0 A.D. team bonus changes (data/auras/teambonuses).
enum TeamBonusStat : uint8_t {
	TEAM_BONUS_UNIT_COST = 1,          // per resource, units in the bitset
	TEAM_BONUS_UNIT_TRAIN_TIME = 2,
	TEAM_BONUS_BUILDING_COST = 3,      // per resource, buildings in the mask
	TEAM_BONUS_BUILDING_BUILD_TIME = 4,
	TEAM_BONUS_TECH_COST = 5,          // per resource, techs researched at the buildings in the mask
	TEAM_BONUS_TECH_TIME = 6,
	TEAM_BONUS_FOOD_TRICKLE = 7,       // food per second, added
	TEAM_BONUS_TRADE_INTERNATIONAL = 8, // Market/InternationalBonus of allies' markets (calculate_trade_gain)
	TEAM_BONUS_UNIT_COST_ADD = 9       // per resource, added after the multipliers (Stolen Arms -5 metal)
};

struct CivTeamBonus {
	uint8_t civ;
	uint8_t stat; // TeamBonusStat
	int8_t res; // ResourceType for cost stats, -1 otherwise
	bool allies_only; // 0 A.D. ExclusiveMutualAlly: not the owner itself
	BldMask buildings; // bld_bit() mask
	float value; // multiplier, or amount added for TEAM_BONUS_FOOD_TRICKLE and TEAM_BONUS_UNIT_COST_ADD
	const char *id;
	const char *name;
	const char *description;
	uint64_t units[CIV_TEMPLATE_WORDS_MAX]; // civ_unit_template_table() rows affected
};

struct CivUnitTemplate {
	const char *id; // 0 A.D. template path under units/, e.g. "athen/infantry_spearman_b"
	const char *name;
	const char *specific_name;
	uint8_t civ;
	uint8_t unit_type; // RTS0 behaviour class (UnitType)
	uint8_t phase; // 1 village, 2 town, 3 city
	uint16_t flags; // CivRosterFlag bits
	BldMask trainers; // bld_bit() of each BuildingType that trains it
	float hp;
	float speed;
	float attack_range;
	float attack_damage;
	float attack_cooldown;
	float hack_damage;
	float pierce_damage;
	float crush_damage;
	float hack_armor;
	float pierce_armor;
	float crush_armor;
	int16_t food_cost;
	int16_t wood_cost;
	int16_t gold_cost;
	int16_t stone_cost;
	uint8_t pop_cost;
	float train_time;
	int16_t carry_capacity;
	int16_t auras[3]; // index into civ_hero_aura_table(), -1 = none
	uint8_t unlock_tech; // TechnologyId from 0 A.D. Identity/Requirements (unlock_champion_*), or TECH_NONE
	TechClasses tech_classes; // TechClass bits of 0 A.D. Identity/Classes + VisibleClasses
	float vision; // 0 A.D. Vision/Range x0.28 (RTS0 units)
};

// 0 A.D. Upgrade on a unit (Upgrade.js): a paid, timed change to another roster row
// (Roman elite infantry to Centurion, the Immortals' spear-and-bow switch).
struct CivUnitUpgrade {
	uint16_t from; // civ_unit_template_table() row, 0xFFFF ends the table
	uint8_t from_rank; // rank the unit needs (1 basic, 2 advanced, 3 elite), 0 = any
	uint16_t to;
	int16_t food_cost;
	int16_t wood_cost;
	int16_t gold_cost; // 0 A.D. metal
	int16_t stone_cost;
	float time; // seconds
	uint8_t phase; // phase the choice's Requirements name, 1 = none
};

// 0 A.D. Population/Bonus on a unit (Population.js), the Germans' wagon house.
struct CivUnitPopBonus {
	uint16_t tmpl; // civ_unit_template_table() row, 0xFFFF ends the table
	int16_t bonus;
};

#include "civ_roster_data.h"
#include "aura_modifier_data.h"

// The relic_global_auras() row of civ_hero_aura `aura`, or -1 (a range aura, or not a catafalque's).
inline int relic_global_aura_index(int16_t aura) {
	static const auto kIndex = [] {
		std::array<int8_t, CIV_HERO_AURA_COUNT> idx{};
		for (uint16_t a = 0; a < CIV_HERO_AURA_COUNT; ++a) {
			idx[a] = -1;
			for (uint16_t g = 0; g < RELIC_GLOBAL_AURA_COUNT; ++g)
				if (std::strcmp(civ_hero_aura_table()[a].id, relic_global_auras()[g].id) == 0) idx[a] = (int8_t)g;
		}
		return idx;
	}();
	return aura >= 0 && aura < CIV_HERO_AURA_COUNT ? kIndex[(size_t)aura] : -1;
}
static_assert(RELIC_GLOBAL_AURA_COUNT <= 64, "global aura masks are uint64_t");

// The garrison_arrow_auras() row of civ_hero_aura `aura`, or -1 (no garrison BuildingAI effect).
inline int garrison_arrow_aura_index(int16_t aura) {
	static const auto kIndex = [] {
		std::array<int8_t, CIV_HERO_AURA_COUNT> idx{};
		for (uint16_t a = 0; a < CIV_HERO_AURA_COUNT; ++a) {
			idx[a] = -1;
			for (uint16_t g = 0; g < GARRISON_ARROW_AURA_COUNT; ++g)
				if (std::strcmp(civ_hero_aura_table()[a].id, garrison_arrow_auras()[g].id) == 0) idx[a] = (int8_t)g;
		}
		return idx;
	}();
	return aura >= 0 && aura < CIV_HERO_AURA_COUNT ? kIndex[(size_t)aura] : -1;
}

// The builder_rate_range_auras() row of civ_hero_aura `aura`, or -1 (no Builder/Rate effect).
inline int builder_rate_aura_index(int16_t aura) {
	static const auto kIndex = [] {
		std::array<int8_t, CIV_HERO_AURA_COUNT> idx{};
		for (uint16_t a = 0; a < CIV_HERO_AURA_COUNT; ++a) {
			idx[a] = -1;
			for (uint16_t g = 0; g < BUILDER_RATE_RANGE_AURA_COUNT; ++g)
				if (std::strcmp(civ_hero_aura_table()[a].id, builder_rate_range_auras()[g].id) == 0) idx[a] = (int8_t)g;
		}
		return idx;
	}();
	return aura >= 0 && aura < CIV_HERO_AURA_COUNT ? kIndex[(size_t)aura] : -1;
}

// Does an entity with these TechClass bits match relic aura modifier `m`?
inline bool relic_modifier_applies(const RelicAuraModifier &m, TechClasses classes) {
	if (m.alt_count == 0) return true;
	for (uint8_t i = 0; i < m.alt_count; ++i)
		if ((classes & m.alts[i].need) == m.alts[i].need && (classes & m.alts[i].forbid) == 0) return true;
	return false;
}
#include "civ_attack_data.h"
#include "splash_data.h"
#include "preference_data.h"
#include "unit_motion_data.h"
#include "formation.h"

inline constexpr uint16_t CIV_UNIT_TEMPLATE_NONE = 0xFFFF;
static_assert(CIV_UNIT_TEMPLATE_COUNT <= 64 * CIV_TEMPLATE_WORDS_MAX, "raise CIV_TEMPLATE_WORDS_MAX");

// Product of the civ's 0 A.D. bonuses on `stat` for a building type
// (pass BLD_TYPE_COUNT for player-wide stats such as CIV_BONUS_POP_CAP).
inline float civ_bonus_mult(uint8_t civ, uint8_t stat, uint8_t building = BLD_TYPE_COUNT) {
	float m = 1.0f;
	const CivPlayerBonus *b = civ_player_bonus_table();
	for (uint16_t i = 0; i < CIV_PLAYER_BONUS_COUNT; ++i) {
		if (b[i].stat != stat || !(b[i].civs & civ_bit(civ))) continue;
		if (building < BLD_TYPE_COUNT && !(b[i].buildings & bld_bit(building))) continue;
		m *= b[i].mult;
	}
	return m;
}

// Product of the 0 A.D. team bonuses a player gets on `stat`: `own_civ` is the
// player's civ, `ally_civs` the civ_bit() mask of its allies still in the match.
// Each civ's bonus counts once even when several allies share the civ, as 0 A.D.
// auras of the same name don't stack. `res` is the ResourceType for cost stats;
// `tmpl` the unit for unit stats; `building` the building for building and tech stats.
inline float civ_team_bonus_mult(uint8_t own_civ, uint16_t ally_civs, uint8_t stat, int8_t res = -1,
		uint16_t tmpl = 0xFFFF, uint8_t building = BLD_TYPE_COUNT) {
	float m = 1.0f;
	const CivTeamBonus *t = civ_team_bonus_table();
	for (uint16_t i = 0; i < CIV_TEAM_BONUS_COUNT; ++i) {
		const CivTeamBonus &b = t[i];
		if (b.stat != stat || b.res != res) continue;
		if (!(ally_civs & civ_bit(b.civ)) && (b.allies_only || b.civ != own_civ)) continue;
		if (stat == TEAM_BONUS_UNIT_COST || stat == TEAM_BONUS_UNIT_TRAIN_TIME || stat == TEAM_BONUS_UNIT_COST_ADD) {
			if (tmpl >= CIV_UNIT_TEMPLATE_COUNT || !((b.units[tmpl >> 6] >> (tmpl & 63)) & 1u)) continue;
		} else if (stat != TEAM_BONUS_FOOD_TRICKLE && stat != TEAM_BONUS_TRADE_INTERNATIONAL) {
			if (!(b.buildings & bld_bit(building))) continue;
		}
		m *= b.value;
	}
	return m;
}

// Sum of the team bonus amounts added to a unit's cost of one resource (TEAM_BONUS_UNIT_COST_ADD).
inline float civ_team_bonus_cost_add(uint8_t own_civ, uint16_t ally_civs, int8_t res, uint16_t tmpl) {
	float add = 0.0f;
	const CivTeamBonus *t = civ_team_bonus_table();
	for (uint16_t i = 0; i < CIV_TEAM_BONUS_COUNT; ++i) {
		const CivTeamBonus &b = t[i];
		if (b.stat != TEAM_BONUS_UNIT_COST_ADD || b.res != res) continue;
		if (!(ally_civs & civ_bit(b.civ)) && (b.allies_only || b.civ != own_civ)) continue;
		if (tmpl >= CIV_UNIT_TEMPLATE_COUNT || !((b.units[tmpl >> 6] >> (tmpl & 63)) & 1u)) continue;
		add += b.value;
	}
	return add;
}

// Sum of the team bonus food trickle (food per second) a player gets.
inline float civ_team_food_trickle(uint8_t own_civ, uint16_t ally_civs) {
	float add = 0.0f;
	const CivTeamBonus *t = civ_team_bonus_table();
	for (uint16_t i = 0; i < CIV_TEAM_BONUS_COUNT; ++i) {
		const CivTeamBonus &b = t[i];
		if (b.stat != TEAM_BONUS_FOOD_TRICKLE) continue;
		if (!(ally_civs & civ_bit(b.civ)) && (b.allies_only || b.civ != own_civ)) continue;
		add += b.value;
	}
	return add;
}

inline bool civ_aura_affects(int16_t aura, uint16_t tmpl) {
	if (aura < 0 || aura >= (int16_t)CIV_HERO_AURA_COUNT || tmpl >= CIV_UNIT_TEMPLATE_COUNT) return false;
	return (civ_hero_aura_table()[aura].affects[tmpl >> 6] >> (tmpl & 63)) & 1u;
}

inline const CivUnitTemplate &civ_unit_template(uint16_t id) {
	const CivUnitTemplate *t = civ_unit_template_table();
	return t[id < CIV_UNIT_TEMPLATE_COUNT ? id : 0];
}

// 0.28 Civilians (template_unit_support_civilian Identity/Phenotype "female male"): Identity.js
// picks one at random when the unit is created. RTS0 keeps no phenotype state; a hash of the
// unit id picks it, so a civilian keeps one look and voice for its whole life. Every other
// template has a single phenotype (UnitType decides it).
inline bool civilian_is_male(uint16_t tmpl, uint32_t unit_id) {
	if (tmpl >= CIV_UNIT_TEMPLATE_COUNT || !(civ_unit_template(tmpl).flags & ROSTER_CIVILIAN)) return false;
	return ((unit_id * 2654435761u) >> 31) != 0;
}

// The roster row of a 0 A.D. template path ("athen/infantry_javelineer_b"), or
// CIV_UNIT_TEMPLATE_NONE.
inline uint16_t civ_unit_template_by_id(const char *id) {
	for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT; ++t)
		if (std::strcmp(civ_unit_template(t).id, id) == 0) return t;
	return CIV_UNIT_TEMPLATE_NONE;
}

// Cost/Population of a unit (heroes, dogs and catafalques 0, siege and warships 2-4);
// units without a roster template fall back to their UnitType row.
inline int unit_pop_cost(uint16_t tmpl, uint8_t type) {
	return tmpl < CIV_UNIT_TEMPLATE_COUNT ? civ_unit_template(tmpl).pop_cost : unit_type_info(type).pop_cost;
}

// Population/Bonus of a unit: the population space it adds for its owner while it exists
// (units/germ/support_wagon_house 5). Units without a roster template add none.
inline int unit_pop_bonus(uint16_t tmpl) {
	if (tmpl >= CIV_UNIT_TEMPLATE_COUNT) return 0;
	for (const CivUnitPopBonus *b = civ_unit_pop_bonus_table(); b->tmpl != 0xFFFF; ++b)
		if (b->tmpl == tmpl) return b->bonus;
	return 0;
}

// Identity class Healer (Heal component): support healers and hero healers.
inline bool unit_is_healer(uint16_t tmpl, uint8_t type) {
	const TechClasses c = tmpl < CIV_UNIT_TEMPLATE_COUNT ? civ_unit_template(tmpl).tech_classes : unit_tech_classes(type);
	return (c & TC_HEALER) != 0;
}

// Heal/Range and Heal/Health of a healer's template, in metres before COMBAT_DISTANCE_SCALE:
// template_unit_support_healer 12 m / 5, template_unit_champion_support_healer 20 m / 10,
// template_unit_hero_healer 20 m / 15.
inline float unit_heal_range_base(uint16_t tmpl, uint8_t type) {
	if (type == UNIT_HERO) return HERO_HEAL_RANGE;
	if (tmpl < CIV_UNIT_TEMPLATE_COUNT && (civ_unit_template(tmpl).flags & ROSTER_CHAMPION)) return CHAMPION_HEAL_RANGE;
	return 12.0f;
}
// unit_min_range_ratio for the unit's template: siege towers have no MinRange, the onager 20 of 60.
inline float unit_min_range_ratio(uint8_t type, uint16_t tmpl) {
	if (tmpl < CIV_UNIT_TEMPLATE_COUNT) {
		const CivUnitTemplate &t = civ_unit_template(tmpl);
		if (t.flags & ROSTER_SIEGE_TOWER) return 0.0f;
		if (type == UNIT_CATAPULT && std::strstr(t.id, "onager")) return 20.0f / 60.0f;
	}
	return unit_min_range_ratio(type);
}

// UnitAI/DefaultStance of the unit's template where it differs from its RTS0 class: siege towers
// (template_unit_siege) and the Seeress (template_unit_champion_support_healer) are aggressive.
inline uint8_t unit_default_stance(uint8_t type, uint16_t tmpl) {
	if (tmpl < CIV_UNIT_TEMPLATE_COUNT) {
		const uint16_t flags = civ_unit_template(tmpl).flags;
		if ((flags & ROSTER_SIEGE_TOWER) || (type == UNIT_HEALER && (flags & ROSTER_CHAMPION))) return STANCE_AGGRESSIVE;
	}
	return unit_default_stance(type);
}
inline float unit_heal_hp_base(uint16_t tmpl, uint8_t type) {
	if (type == UNIT_HERO) return HERO_HEAL_HP;
	if (tmpl < CIV_UNIT_TEMPLATE_COUNT && (civ_unit_template(tmpl).flags & ROSTER_CHAMPION)) return CHAMPION_HEAL_HP;
	return HEAL_HP;
}

// 0 A.D. UnitMotion/PassabilityClass "large": template_unit_siege, every elephant
// (template_unit_elephant and the champion, hero and support elephants), chariots
// (mixins/chariot: the champion and hero chariots) and catafalques (template_unit_catafalque).
// Among 0 A.D.'s unit templates these are exactly the ones whose classes hold Siege, Elephant or
// Chariot, plus the catafalques and supply wagons (template_unit_support_wagon), so the roster
// row's classes decide it (a hero on an elephant or a chariot is large, one on foot or horseback
// is not).
inline bool unit_is_large(uint16_t tmpl, uint8_t type) {
	if (type == UNIT_CATAFALQUE || type == UNIT_WAGON) return true;
	const TechClasses c = tmpl < CIV_UNIT_TEMPLATE_COUNT ? civ_unit_template(tmpl).tech_classes : unit_tech_classes(type);
	return (c & (TC_SIEGE | TC_ELEPHANT | TC_CHARIOT)) != 0;
}

// UnitMotion Weight: 10 in template_unit.xml, five times that for siege engines, every elephant
// (template_unit_elephant and the champion, hero and support elephants) and the supply wagon,
// three times for chariots (mixins/chariot: the champion and hero chariots) and ten times for
// catafalques (template_unit_catafalque). A heavy unit pushes harder and is harder to push.
// Every roster row with the Siege, Elephant or Chariot class has one of these unit types, or is a
// hero, whose row's classes say what it rides (test_unit_pushing checks every row); pushing reads
// this for every unit every tick, so the rest never touch the roster table.
inline uint16_t unit_push_weight(uint16_t tmpl, uint8_t type) {
	if (type == UNIT_CATAFALQUE) return 100;
	if (unit_is_siege_engine(type) || type == UNIT_ELEPHANT || type == UNIT_WORKER_ELEPHANT || type == UNIT_WAGON) return 50;
	if (type == UNIT_CHARIOT || type == UNIT_SCYTHED_CHARIOT) return 30;
	if (type != UNIT_HERO || tmpl >= CIV_UNIT_TEMPLATE_COUNT) return 10;
	const TechClasses c = civ_unit_template(tmpl).tech_classes;
	return (c & TC_ELEPHANT) != 0 ? 50 : (c & TC_CHARIOT) != 0 ? 30 : 10;
}

// How close a unit's centre comes to a building: its passability class Clearance. Ships keep
// their hull radius: docks are the only buildings they meet. Between units RTS0 keeps
// unit_collision_radius.
inline float unit_building_clearance(uint16_t tmpl, uint8_t type) {
	if (unit_is_ship(type)) return unit_collision_radius(type);
	return unit_is_large(tmpl, type) ? CLEARANCE_LARGE : CLEARANCE_DEFAULT;
}

// 0 A.D. ResourceGatherer by template: citizen infantry (template_unit_infantry), support
// workers (template_unit_support_female_citizen, and template_unit_support_civilian for RTS0's
// villager, 0 A.D.'s male Civilian), citizen cavalry (template_unit_cavalry) and fishing ships
// (template_unit_ship_fishing). Champions, heroes, siege and the Mauryan worker elephant
// (a mobile dropsite) gather nothing.
enum GatherClass : uint8_t { GATHER_NONE, GATHER_INFANTRY, GATHER_SUPPORT, GATHER_CAVALRY, GATHER_FISHING };

inline uint8_t unit_gather_class(uint8_t unit_type, uint16_t tmpl) {
	if (unit_type == UNIT_FEMALE_CITIZEN || unit_type == UNIT_VILLAGER) return GATHER_SUPPORT;
	if (unit_type == UNIT_FISHING_BOAT) return GATHER_FISHING;
	const bool infantry = unit_type == UNIT_SWORDSMAN || unit_type == UNIT_SPEARMAN || unit_type == UNIT_ARCHER;
	const bool cavalry = unit_type == UNIT_CAVALRY || unit_type == UNIT_CAMEL;
	if (tmpl != CIV_UNIT_TEMPLATE_NONE) {
		// Roster rows: only citizen soldiers (ROSTER_CITIZEN; three citizen champions inherit
		// template_unit_infantry too) gather.
		const uint16_t flags = civ_unit_template(tmpl).flags;
		if (!(flags & ROSTER_CITIZEN) || (flags & ROSTER_NO_GATHER)) return GATHER_NONE;
		return infantry ? GATHER_INFANTRY : cavalry ? GATHER_CAVALRY : GATHER_NONE;
	}
	return infantry ? GATHER_INFANTRY : cavalry ? GATHER_CAVALRY : GATHER_NONE;
}

// 0 A.D. Attack/Slaughter (1000 hack at 2 m on Domestic animals) is on template_unit_infantry,
// template_unit_cavalry and template_unit_support_female_citizen only: citizen soldiers (and the
// citizen champions and mercenaries that inherit them) and women. Heroes, champions, dogs,
// healers, traders, ships and siege use their own attack on livestock.
inline bool unit_has_slaughter(uint8_t unit_type, uint16_t tmpl) {
	if (unit_type == UNIT_FEMALE_CITIZEN || unit_type == UNIT_VILLAGER) return true;
	const bool soldier = unit_type == UNIT_SWORDSMAN || unit_type == UNIT_SPEARMAN || unit_type == UNIT_ARCHER ||
	                     unit_type == UNIT_CAVALRY || unit_type == UNIT_CAMEL;
	if (!soldier) return false;
	return tmpl == CIV_UNIT_TEMPLATE_NONE || (civ_unit_template(tmpl).flags & ROSTER_CITIZEN) != 0;
}

// 0 A.D. Builder (mixins/builder): citizen infantry (template_unit_infantry) and the support
// workers build and repair; cavalry, champions, heroes, siege and ships do not.
// Mercenary citizen infantry (CitizenSoldier Infantry with ResourceGatherer disabled).
inline bool unit_is_mercenary_infantry(uint8_t unit_type, uint16_t tmpl) {
	if (tmpl >= CIV_UNIT_TEMPLATE_COUNT) return false;
	const uint16_t f = civ_unit_template(tmpl).flags;
	return (f & ROSTER_MERCENARY) && (f & ROSTER_CITIZEN) && !(f & ROSTER_CHAMPION) &&
			(unit_tech_classes(unit_type) & TC_INFANTRY) && !(unit_tech_classes(unit_type) & TC_CAVALRY);
}

// Mercenary infantry keep Builder in 0.28 though their ResourceGatherer is gone (template_unit_infantry
// pulls in mixins/builder); the Neodamodes, no mercenaries, disable it.
inline bool unit_can_build(uint8_t unit_type, uint16_t tmpl) {
	if (unit_type == UNIT_FEMALE_CITIZEN || unit_type == UNIT_VILLAGER || unit_type == UNIT_WORKER_ELEPHANT) return true;
	if (unit_is_mercenary_infantry(unit_type, tmpl)) return true;
	return unit_gather_class(unit_type, tmpl) == GATHER_INFANTRY;
}

// ResourceGatherer/Rates in units a second, by Simulation::GatherKind
// (grain, fruit, meat, fish, wood, stone, metal); 0 = can't gather it.
inline float gather_class_rate(uint8_t cls, uint8_t kind) {
	static const float kRates[5][7] = {
		{0, 0, 0, 0, 0, 0, 0},
		{0.25f, 0.5f, 1.0f, 0, 0.75f, 0.5f, 0.5f}, // infantry
		{0.5f, 1.0f, 1.0f, 0, 0.7f, 0.35f, 0.35f},  // support: female citizen, civilian
		{0, 0, 5.0f, 0, 0, 0, 0},                   // cavalry: food.meat only
		{0, 0, 0, 1.8f, 0, 0, 0},                   // fishing ship: food.fish only
	};
	return cls < 5 && kind < 7 ? kRates[cls][kind] : 0.0f;
}

// Rates for template_gaia_ruins supplies: wood.ruins 5, stone.ruins and metal.ruins 2, the
// same for citizen infantry and women; cavalry and ships have none.
inline float gather_class_ruins_rate(uint8_t cls, uint8_t res_type) {
	if (cls != GATHER_INFANTRY && cls != GATHER_SUPPORT) return 0.0f;
	return res_type == RES_WOOD ? 5.0f : (res_type == RES_STONE || res_type == RES_GOLD) ? 2.0f : 0.0f;
}

// ResourceGatherer/Capacities (the same for every type the class gathers).
inline int32_t gather_class_capacity(uint8_t cls) {
	switch (cls) {
		case GATHER_INFANTRY: case GATHER_SUPPORT: return 10;
		case GATHER_CAVALRY: return 20;
		case GATHER_FISHING: return 40;
		default: return 0;
	}
}

// ResourceSupply/MaxGatherers from the gaia templates: trees and fruit 8 (template_gaia_tree,
// _fruit), mines 12 and large mines 24 (template_gaia_ore/_rock and their _large), fish 4,
// fields 5, carcasses 8 unless their animal says otherwise.
inline uint8_t resource_default_max_gatherers(uint8_t type, uint8_t subtype, int32_t amount) {
	if (subtype == RES_SUB_FISH) return FISH_MAX_GATHERERS;
	if (subtype == RES_SUB_GRAIN) return 5; // FIELD_MAX_GATHERERS
	if (type == RES_GOLD || type == RES_STONE) return amount >= 5000 ? 24 : 12;
	return 8;
}

inline const CivHeroAura &civ_hero_aura(int16_t idx) {
	const CivHeroAura *a = civ_hero_aura_table();
	return a[(idx >= 0 && idx < CIV_HERO_AURA_COUNT) ? idx : 0];
}

// Roster of a civ is the template id range [civ_roster_begin, civ_roster_end).
inline uint16_t civ_roster_begin(uint8_t civ) {
	if (civ >= CIV_COUNT) civ = CIV_ATHENIANS;
	return civ_roster_offsets()[civ];
}

inline uint16_t civ_roster_end(uint8_t civ) {
	if (civ >= CIV_COUNT) civ = CIV_ATHENIANS;
	return civ_roster_offsets()[civ + 1];
}

// Every trainable row of a civ: its block [civ_roster_begin, civ_roster_end) and the
// rows added after the promotion-only ones (template ids, which saves and replays
// store, never move; tools/extract_0ad_rosters.py keep_previous_ids).
inline const std::vector<uint16_t> &civ_roster_ids(uint8_t civ) {
	static const auto kIds = [] {
		std::array<std::vector<uint16_t>, CIV_COUNT> ids;
		for (uint8_t c = 0; c < CIV_COUNT; ++c) {
			for (uint16_t i = civ_roster_begin(c); i < civ_roster_end(c); ++i) ids[c].push_back(i);
			for (uint16_t i = CIV_ROSTER_PROMOTION_ONLY_FIRST; i < CIV_UNIT_TEMPLATE_COUNT; ++i)
				if (civ_unit_template(i).civ == c && civ_unit_template(i).trainers) ids[c].push_back(i);
		}
		return ids;
	}();
	return kIds[civ < CIV_COUNT ? civ : CIV_ATHENIANS];
}

// The civ's standard unit for an RTS0 class: the citizen-soldier or support
// unit a generic "train a spearman" request should produce. Prefers non-hero,
// non-champion units with the earliest phase. CIV_UNIT_TEMPLATE_NONE if the
// civ has no unit of that class.
struct CivDefaultTemplates {
	uint16_t id[CIV_COUNT][UNIT_TYPE_COUNT];
};

inline uint16_t civ_default_template(uint8_t civ, uint8_t type) {
	static const CivDefaultTemplates cache = [] {
		CivDefaultTemplates out{};
		for (uint8_t c = 0; c < CIV_COUNT; ++c) {
			for (uint8_t t = 0; t < UNIT_TYPE_COUNT; ++t) {
				uint16_t best = CIV_UNIT_TEMPLATE_NONE;
				int best_score = 1 << 30;
				for (uint16_t i : civ_roster_ids(c)) {
					const CivUnitTemplate &u = civ_unit_template(i);
					// Siege towers share the bolt shooter's class but are only trained by name;
					// units only reached by promotion (no trainer) are never trained.
					if (u.unit_type != t || (u.flags & ROSTER_SIEGE_TOWER) || !u.trainers) continue;
					int score = u.phase * 4;
					if (u.flags & ROSTER_HERO) score += 100;
					if (u.flags & ROSTER_CHAMPION) score += 20;
					if (u.flags & ROSTER_MERCENARY) score += 2;
					if (!(u.trainers & (bld_bit(BLD_TOWN_CENTER) | bld_bit(BLD_BARRACKS) | bld_bit(BLD_DOCK) |
							bld_bit(BLD_TEMPLE) | bld_bit(BLD_MARKET) | bld_bit(BLD_FORTRESS)))) score += 1;
					if (score < best_score) {
						best_score = score;
						best = i;
					}
				}
				out.id[c][t] = best;
			}
		}
		return out;
	}();
	if (civ >= CIV_COUNT) civ = CIV_ATHENIANS;
	if (type >= UNIT_TYPE_COUNT) return CIV_UNIT_TEMPLATE_NONE;
	return cache.id[civ][type];
}

// UnitTypeInfo view of a roster unit: 0 A.D.'s stats for the civ unit, with
// the class's model, portrait and aggro range. CIV_UNIT_TEMPLATE_NONE (or an
// out-of-range id) falls back to the generic class row.
struct CivTemplateInfos {
	UnitTypeInfo info[CIV_UNIT_TEMPLATE_COUNT];
};

inline const UnitTypeInfo &civ_template_unit_info(uint16_t tmpl, uint8_t type) {
	static const CivTemplateInfos table = [] {
		CivTemplateInfos out{};
		for (uint16_t i = 0; i < CIV_UNIT_TEMPLATE_COUNT; ++i) {
			const CivUnitTemplate &t = civ_unit_template(i);
			UnitTypeInfo u = unit_type_info(t.unit_type);
			u.name = t.name;
			u.hp = t.hp;
			u.speed = t.speed;
			u.attack_range = t.attack_range;
			u.attack_damage = t.attack_damage;
			u.attack_cooldown = t.attack_cooldown;
			u.carry_capacity = t.carry_capacity;
			// template_unit_cavalry ResourceGatherer: citizen cavalry carry 20 meat (Capacities/food).
			if (t.unit_type == UNIT_CAVALRY && (t.flags & ROSTER_CITIZEN) && !(t.flags & ROSTER_NO_GATHER) && u.carry_capacity == 0) u.carry_capacity = 20;
			if (t.attack_range > u.aggro_range) u.aggro_range = t.attack_range;
			u.hack_armor = t.hack_armor;
			u.pierce_armor = t.pierce_armor;
			u.crush_armor = t.crush_armor;
			u.food_cost = t.food_cost;
			u.wood_cost = t.wood_cost;
			u.gold_cost = t.gold_cost;
			u.stone_cost = t.stone_cost;
			u.pop_cost = t.pop_cost;
			u.train_time = t.train_time;
			u.hack_damage = t.hack_damage;
			u.pierce_damage = t.pierce_damage;
			u.crush_damage = t.crush_damage;
			out.info[i] = u;
		}
		return out;
	}();
	if (tmpl < CIV_UNIT_TEMPLATE_COUNT) return table.info[tmpl];
	return unit_type_info(type);
}

// The civ's catafalque (units/{civ}/catafalque), or CIV_UNIT_TEMPLATE_NONE.
inline uint16_t civ_catafalque_template(uint8_t civ) {
	for (uint16_t i = CIV_ROSTER_PROMOTION_ONLY_FIRST; i < CIV_UNIT_TEMPLATE_COUNT; ++i)
		if ((civ_unit_template(i).flags & ROSTER_RELIC) && civ_unit_template(i).civ == civ) return i;
	return CIV_UNIT_TEMPLATE_NONE;
}

// Stats of the unit a civ fields for an RTS0 class (its default template).
inline const UnitTypeInfo &civ_unit_info(uint8_t civ, uint8_t type) {
	return civ_template_unit_info(civ_default_template(civ, type), type);
}

// True if the unit attacks at range: the roster's 0 A.D. attack when the
// unit has a template, else the class rule.
inline bool template_has_ranged_attack(uint16_t tmpl, uint8_t type, uint8_t civ) {
	if (tmpl < CIV_UNIT_TEMPLATE_COUNT) {
		const CivUnitTemplate &t = civ_unit_template(tmpl);
		return (t.flags & ROSTER_RANGED) != 0 && t.attack_damage > 0.0f;
	}
	return unit_has_ranged_attack(type, civ);
}

// 0 A.D. Pack.js. Stone throwers and bolt shooters (template_unit_siege_stonethrower.xml,
// template_unit_siege_boltshooter.xml) carry <Pack><Time>10000</Time></Pack>: they move as
// their "_packed" template and must unpack before they can fire, and pack again to move.
// A roster unit packs when its 0 A.D. template has a Pack component (ROSTER_PACKABLE), so
// siege towers and engines that disable Pack (Han Shoushe) are left alone; units without a
// roster template fall back to the StoneThrower/BoltShooter classes.
enum PackState : uint8_t {
	PACK_NONE = 0, // no Pack component
	PACK_PACKED = 1, // can move, cannot attack
	PACK_UNPACKED = 2, // can attack, cannot move
	PACK_PACKING = 3, // unpacked -> packed in progress
	PACK_UNPACKING = 4, // packed -> unpacked in progress
};
inline constexpr float PACK_TIME_S = 5.0f; // 0.28 template_unit_siege_stonethrower/_boltshooter Pack/Time 5000 ms

inline bool unit_has_pack(uint16_t tmpl, uint8_t type) {
	if (tmpl < CIV_UNIT_TEMPLATE_COUNT) return (civ_unit_template(tmpl).flags & ROSTER_PACKABLE) != 0;
	return (unit_tech_classes(type) & (TC_STONETHROWER | TC_BOLTSHOOTER)) != 0;
}
// 0 A.D. Trader units: land traders trade between Markets, merchant ships between Docks.
inline bool unit_is_trader(uint8_t type) { return type == UNIT_TRADER || type == UNIT_MERCHANT_SHIP; }
inline bool pack_state_can_move(uint8_t s) { return s == PACK_NONE || s == PACK_PACKED; }
inline bool pack_state_can_attack(uint8_t s) { return s == PACK_NONE || s == PACK_UNPACKED; }

// 0 A.D. Loot.js: what an entity yields when killed (resources to the killer's
// owner through Looter.js) and the experience it gives for each hit point taken
// from it (Health.js TakeDamage: xp * damage / max hp). Values are the resolved
// template_* and per-unit templates; metal is RTS0 gold.
struct LootInfo {
	uint16_t xp, food, wood, stone, metal;
};

inline LootInfo unit_class_loot(uint8_t type) {
	static const LootInfo table[UNIT_TYPE_COUNT] = {
		{100, 5, 0, 0, 0},    // VILLAGER: template_unit_infantry (citizen)
		{100, 5, 4, 0, 1},    // SWORDSMAN: infantry_melee_swordsman
		{100, 5, 5, 0, 0},    // ARCHER: infantry_ranged_archer
		{100, 5, 5, 0, 0},    // SPEARMAN: infantry_melee_spearman
		{130, 10, 5, 0, 0},   // CAVALRY: template_unit_cavalry
		{10, 10, 0, 0, 8},    // TRADER: support_trader
		{250, 0, 80, 50, 0},  // CATAPULT: siege_stonethrower
		{400, 10, 0, 0, 25},  // HERO: template_unit_hero
		{50, 0, 12, 0, 8},    // TRIREME: ship_bireme
		{300, 30, 0, 0, 20},  // ELEPHANT: champion_elephant
		{200, 0, 60, 0, 30},  // SIEGE_RAM: siege_ram
		{8, 10, 0, 0, 3},     // HEALER: support_healer
		{200, 15, 8, 0, 10},  // CHARIOT: champion_cavalry
		{100, 10, 0, 0, 0},   // WAR_DOG: champion_infantry_dog (Loot xp 100, food 10)
		{200, 0, 50, 0, 50},  // BOLT_SHOOTER: siege_boltshooter
		{0, 0, 10, 0, 0},     // FISHING_BOAT: ship_fishing
		{0, 0, 0, 0, 20},     // MERCHANT_SHIP: ship_merchant
		{3, 5, 0, 0, 0},      // FEMALE_CITIZEN: support_female_citizen
		{150, 18, 0, 0, 12},  // CARNYX: champion_infantry with the Celtic war horn
		{0, 10, 0, 0, 0},     // WORKER_ELEPHANT: support_elephant
		{150, 8, 6, 0, 8},    // ARCHER_MAIDEN: champion_infantry_archer
		{130, 10, 5, 0, 0},   // CAMEL: template_unit_cavalry
		{200, 15, 8, 0, 10},  // SCYTHED_CHARIOT: champion_cavalry
		{150, 10, 0, 0, 10},  // MINISTER: units/han/support_minister
		{0, 0, 0, 0, 0},      // CATAFALQUE: no Loot
		{0, 0, 0, 0, 0},      // TYPE_TECH: not a unit
		{100, 10, 10, 10, 10}, // WAGON: support_wagon
	};
	return type < UNIT_TYPE_COUNT ? table[type] : LootInfo{};
}

// A roster unit takes its own 0 A.D. template's Loot (civ_attack_data.h, mixins such as
// elephant_indian's x1.1 applied); RTS0-only rows (the male villager) take their class's.
inline LootInfo unit_loot(uint16_t tmpl, uint8_t type) {
	if (tmpl < CIV_UNIT_TEMPLATE_COUNT) {
		const CombatProfile &c = combat_template_table()[tmpl];
		if (c.has_loot) return {c.loot[0], c.loot[1], c.loot[2], c.loot[3], c.loot[4]};
	}
	return unit_class_loot(type);
}

// Structures yield resources (and no xp, but for the Han Imperial Ministry). Keyed by building type; the
// civ-specific structures (ids 26-45) take their rows from civ_building_data.h.
inline LootInfo building_loot(uint8_t type) {
	static const LootInfo table[] = {
		{0, 0, 60, 60, 50},    // TOWN_CENTER: civic_civil_centre
		{0, 0, 15, 0, 0},      // HOUSE: civic_house
		{0, 0, 40, 20, 0},     // BARRACKS: military_barracks
		{0, 0, 20, 0, 0},      // STOREHOUSE: economic_storehouse
		{0, 0, 20, 20, 0},     // TOWER: defensive_tower_defense
		{0, 0, 20, 0, 0},      // FARMSTEAD: economic_farmstead
		{0, 0, 60, 0, 0},      // MARKET: economic_market
		{0, 0, 60, 120, 0},    // FORTRESS: military_fortress
		{0, 0, 2, 4, 0},       // GATE: defensive_wall_gate
		{0, 0, 0, 60, 0},      // TEMPLE: civic_temple
		{0, 0, 200, 300, 200}, // WONDER: wonder
		{0, 0, 40, 0, 0},      // DOCK: military_dock
		{0, 0, 0, 7, 0},       // WALL_SEGMENT: defensive_wall_long
		{0, 0, 20, 0, 0},      // CORRAL: resource_corral
		{0, 50, 0, 0, 0},      // FIELD: resource_field
		{0, 0, 40, 10, 0},     // STABLE: military_stable
		{0, 0, 40, 0, 0},      // RANGE: military_range
		{0, 0, 40, 0, 0},      // FORGE: military_forge
		{0, 0, 60, 0, 0},      // ARSENAL: military_arsenal
		{0, 0, 20, 0, 0},      // SENTRY_TOWER: defensive_tower_sentry
		{0, 0, 12, 0, 0},      // OUTPOST: defensive_outpost
		{0, 0, 0, 0, 0},       // PALISADE: defensive_palisade
		{0, 0, 0, 10, 0},      // WALL_TOWER: defensive_wall_tower
		{0, 0, 40, 40, 0},     // ELEPHANT_STABLE: military_elephant_stable
		{0, 0, 40, 120, 40},   // THEATER: special_theater
		{0, 0, 40, 0, 0},      // EMBASSY: cart/embassy_celtic
		RTSO_CIV_BUILDING_LOOT_ROWS
	};
	static_assert(sizeof(table) / sizeof(table[0]) == BLD_TYPE_COUNT, "one loot row per BuildingType");
	return type < BLD_TYPE_COUNT ? table[type] : LootInfo{};
}
// A structure's own 0.28 template Loot: the civ's template where it differs (big houses 30 wood;
// Celtic and Mauryan wooden barracks, stables and temples; the Iberian tower; the Carthaginian
// dock; Mauryan wooden walls) and each WallSet piece's length (wall_long, _medium, _short 7, 5
// and 3 stone; palisades 3, 2, 1 wood and their tower 3; siege walls 12, 8, 4 wood and the
// tower 16).
inline LootInfo structure_loot(uint8_t type, uint8_t civ, float half_w) {
	LootInfo l = building_loot(type);
	const bool celt_or_maur = civ == CIV_BRITONS || civ == CIV_GAULS || civ == CIV_MAURYANS;
	auto set = [&l](uint16_t wood, uint16_t stone) { l.wood = wood; l.stone = stone; };
	switch (type) {
		case BLD_HOUSE: if (civ_has_big_house(civ)) set(30, 0); break;
		case BLD_BARRACKS: if (celt_or_maur) set(60, 0); break;
		case BLD_STABLE: if (celt_or_maur) set(50, 0); break;
		case BLD_TEMPLE: if (civ == CIV_BRITONS || civ == CIV_GAULS) set(60, 0); break;
		case BLD_TOWER: if (civ == CIV_IBERIANS) set(10, 50); break;
		case BLD_DOCK: if (civ == CIV_CARTHAGINIANS) set(30, 0); break;
		case BLD_MONUMENT: if (civ == CIV_GERMANS) { set(30, 0); l.metal = 20; } break;
		case BLD_GATE: if (civ == CIV_MAURYANS) set(6, 0); break;
		case BLD_WALL_TOWER: if (civ == CIV_MAURYANS) set(12, 0); break;
		default: break;
	}
	if (!is_wallset_piece(type, half_w) || is_gate_type(type)) return l;
	const uint8_t kind = wallset_kind_of(type);
	const WallSetInfo &ws = wallset_info(kind);
	int k = 3; // the set's tower
	if (!is_wallset_tower_piece(type, half_w))
		for (k = 0; k < 2 && std::fabs(0.5f * ws.lengths[k] - half_w) >= 0.01f; ++k) {}
	static constexpr uint16_t STONE[4] = {7, 5, 3, 10}, PALISADE[4] = {3, 2, 1, 3}, SIEGE[4] = {12, 8, 4, 16};
	if (kind == WALLSET_PALISADE) set(PALISADE[k], 0);
	else if (kind == WALLSET_SIEGE) set(SIEGE[k], 0);
	else if (civ == CIV_MAURYANS) set(STONE[k], 0); // maur/wall_*: wood
	else set(0, STONE[k]);
	return l;
}
// 0 A.D. unit_advanced.json and unit_elite.json each multiply Loot/* by 1.2.
inline float loot_rank_multiplier(uint8_t rank) {
	return rank >= 3 ? 1.44f : (rank == 2 ? 1.2f : 1.0f);
}

// 0 A.D. combat classes (civ_attack_data.h, from tools/extract_0ad_attack.py).
static_assert(COMBAT_TEMPLATE_COUNT == CIV_UNIT_TEMPLATE_COUNT,
		"rosters changed: re-run tools/extract_0ad_attack.py");
static_assert(COMBAT_CLASS_ROWS == UNIT_TYPE_COUNT, "unit types changed: re-run tools/extract_0ad_attack.py");
static_assert(COMBAT_BUILDING_ROWS == BLD_TYPE_COUNT, "building types changed: re-run tools/extract_0ad_attack.py");
// 0 A.D. UnitMotion Acceleration and InstantTurnAngle and Position TurnRate of a unit's template
// (unit_motion_data.h, from tools/extract_0ad_motion.py).
static_assert(MOTION_TEMPLATE_COUNT == CIV_UNIT_TEMPLATE_COUNT, "rosters changed: re-run tools/extract_0ad_motion.py");
static_assert(MOTION_CLASS_ROWS == UNIT_TYPE_COUNT, "unit types changed: re-run tools/extract_0ad_motion.py");
inline const UnitMotionProfile &unit_motion(uint16_t tmpl, uint8_t type) {
	if (tmpl < CIV_UNIT_TEMPLATE_COUNT) return motion_template_table()[tmpl];
	return motion_class_table()[type < UNIT_TYPE_COUNT ? type : UNIT_VILLAGER];
}
// 0 A.D. Attack/<type>/Splash of a unit's primary attack (splash_data.h, from
// tools/extract_0ad_splash.py): a splash_profiles() row, 0 for none.
static_assert(SPLASH_TEMPLATE_COUNT == CIV_UNIT_TEMPLATE_COUNT, "rosters changed: re-run tools/extract_0ad_splash.py");
static_assert(SPLASH_CLASS_ROWS == UNIT_TYPE_COUNT, "unit types changed: re-run tools/extract_0ad_splash.py");
// Area damage (helpers/Attack.js CauseDamageOverArea) reaches entities by their
// footprint's edge. RTS0's units stand further apart than 0 A.D.'s (an infantry
// radius of 0.55 units against a 0.8 m clearance), so splash and death-damage
// radii scale like melee reach (extract_0ad_rosters.py MELEE_RANGE_SCALE) rather
// than like ranged ranges, or a 2 m splash would never reach the next soldier.
inline constexpr float AREA_DAMAGE_SCALE = 0.6f;
inline uint8_t unit_splash(uint16_t tmpl, uint8_t type) {
	if (tmpl < CIV_UNIT_TEMPLATE_COUNT) return splash_template_index()[tmpl];
	return type < UNIT_TYPE_COUNT ? splash_class_index()[type] : 0;
}
inline const CombatProfile &unit_combat(uint16_t tmpl, uint8_t type) {
	if (tmpl < CIV_UNIT_TEMPLATE_COUNT) return combat_template_table()[tmpl];
	return combat_class_table()[type < UNIT_TYPE_COUNT ? type : UNIT_VILLAGER];
}
// A unit's Capture attack (Attack/Capture/Capture in its 0 A.D. template, civ_attack_data.h):
// infantry 2.5, cavalry 1.75, champions 5, champion cavalry 3.5, heroes 10 (none on hero
// elephants and healer heroes), male civilians 1, the Han minister 4. Women, healers, traders,
// war elephants, dogs, siege and ships have none. RTS0's own villager captures as 0.28's civilian.
inline float unit_capture_attack(uint16_t tmpl, uint8_t type) {
	if (type == UNIT_VILLAGER) return 1.0f;
	return unit_combat(tmpl, type).capture;
}

// A building type past the table (a civ-unique structure added later) is a generic structure.
inline const CombatProfile &building_combat(uint8_t type) {
	return combat_building_table()[type < COMBAT_BUILDING_ROWS ? type : COMBAT_BUILDING_ROWS];
}
// A structure's Resistance levels (hack, pierce, crush): its type's template, or the civ's own
// where it differs (germ/totem 8, 20, 2 against the Iberian monument's 20, 30, 3).
inline void structure_armour(uint8_t type, uint8_t civ, float out[3]) {
	const CombatProfile &bc = building_combat(type);
	out[0] = bc.hack_armor;
	out[1] = bc.pierce_armor;
	out[2] = bc.crush_armor;
	if (type == BLD_MONUMENT && civ == CIV_GERMANS) {
		out[0] = 8.0f;
		out[1] = 20.0f;
		out[2] = 2.0f;
	}
}
// 0 A.D. Attack/<type>/PreferredClasses (preference_data.h, from tools/extract_0ad_preferences.py).
static_assert(PREF_TEMPLATE_COUNT == CIV_UNIT_TEMPLATE_COUNT, "rosters changed: re-run tools/extract_0ad_preferences.py");
static_assert(PREF_CLASS_ROWS == UNIT_TYPE_COUNT, "unit types changed: re-run tools/extract_0ad_preferences.py");
static_assert(PREF_BUILDING_ROWS == BLD_TYPE_COUNT, "building types changed: re-run tools/extract_0ad_preferences.py");
inline const PrefUnitRow &unit_preferences(uint16_t tmpl, uint8_t type) {
	if (tmpl < CIV_UNIT_TEMPLATE_COUNT) return pref_template_table()[tmpl];
	return pref_class_table()[type < UNIT_TYPE_COUNT ? type : UNIT_VILLAGER];
}
inline uint8_t building_pref_classes(uint8_t type) {
	return pref_building_classes()[type < PREF_BUILDING_ROWS ? type : PREF_BUILDING_ROWS];
}
inline constexpr uint8_t PREF_NONE = 255;
// Attack.js GetPreference: the index of the first PreferredClasses token the
// target's classes match (MatchesClassList), PREF_NONE if it matches none.
inline uint8_t preference_of(const PrefProfile &p, uint8_t target_classes) {
	for (uint8_t k = 0; k < p.count; ++k) {
		const PrefTerm &t = p.term[k];
		if ((target_classes & t.all) == t.all && !(target_classes & t.none)) return t.index;
	}
	return PREF_NONE;
}
// 0 A.D. Attack.js CanAttack: an attack never targets its RestrictedClasses.
inline bool combat_can_target(const CombatProfile &attacker, const CombatProfile &target) {
	return !target.classes.any_of(attacker.restricted);
}
// 0 A.D. helpers/Attack.js GetAttackBonus: each bonus whose Classes (and Civ) the target
// has multiplies the damage.
inline float combat_bonus(const CombatProfile &attacker, const CombatProfile &target, uint8_t target_civ) {
	float m = 1.0f;
	for (uint8_t k = 0; k < attacker.bonus_count; ++k) {
		const CombatBonus &b = attacker.bonus[k];
		if (b.civ != 255 && b.civ != target_civ) continue;
		if (target.classes.all_of(b.classes)) m *= b.multiplier;
	}
	return m;
}
// A projectile's footprint test radius in RTS0 units (PositionHelper.TestCollision).
inline float combat_footprint(const CombatProfile &p) { return p.footprint * COMBAT_DISTANCE_SCALE; }
// unit_advanced.json and unit_elite.json each multiply Attack/Ranged/Spread by 0.8.
// StatusEffect bit for a CombatProfile::status (1 Poisoned, 2 Burning).
inline uint8_t status_effect_bit(uint8_t status) {
	return status == 1 ? EFFECT_POISON : status == 2 ? EFFECT_BURNING : EFFECT_NONE;
}
inline float spread_rank_multiplier(uint8_t rank) {
	return rank >= 3 ? 0.64f : (rank == 2 ? 0.8f : 1.0f);
}

// Units this civilization can train: every RTS0 class its 0 A.D. roster has.
struct CivTrainMasks {
	uint32_t mask[CIV_COUNT];
};

inline uint32_t civ_train_mask(uint8_t civ) {
	static const CivTrainMasks masks = [] {
		CivTrainMasks out{};
		for (uint8_t c = 0; c < CIV_COUNT; ++c)
			for (uint16_t i : civ_roster_ids(c))
				if (!(civ_unit_template(i).flags & ROSTER_SIEGE_TOWER))
					out.mask[c] |= unit_bit(civ_unit_template(i).unit_type);
		return out;
	}();
	if (civ >= CIV_COUNT) civ = CIV_ATHENIANS;
	return masks.mask[civ];
}

// Structures this civilization may build: every generic one, plus its own
// civ-specific ones (0 A.D. structures/<civ>/, civ_building_civ_mask()).
inline bool civ_can_build(uint8_t civ, uint8_t type) {
	return type < BLD_TYPE_COUNT && civ < 16 && ((civ_building_civ_mask(type) >> civ) & 1u) &&
	       !civ_building_upgrade_only(type);
}

// Structures whose 0.28 Trainer list names units/{native}/...: they train their own civ's
// units whoever holds them (Trainer.js CalculateEntitiesMap): the shrines, the Kushite
// Noba and Blemmye camps and the Carthaginian embassies.
inline bool building_trains_native(uint8_t type) {
	return type == BLD_SHRINE || type == BLD_CAMP_NOBA || type == BLD_CAMP_BLEMMYE ||
	       type == BLD_EMBASSY || type == BLD_EMBASSY_IBERIAN || type == BLD_EMBASSY_ITALIC;
}

// The civ whose roster `t` must come from to train at a `type` structure built by `bld_civ`
// and held by a `owner_civ` player. Civic centres (the CC, crannog, Han court, Seleucid
// military colony and German encampments) list units/{native}/support_civilian, so a
// captured one trains its builder's civilians (template_structure_civic_civil_centre.xml).
inline uint8_t building_train_civ(uint8_t type, uint8_t bld_civ, uint8_t owner_civ, const CivUnitTemplate &t) {
	if (building_trains_native(type)) return bld_civ;
	const bool civic = type == BLD_TOWN_CENTER || type == BLD_CRANNOG || type == BLD_IMPERIAL_COURT ||
	                   type == BLD_MILITARY_COLONY || type == BLD_WAGON_ENCAMPMENT ||
	                   type == BLD_WAGON_ENCAMPMENT_FORTIFIED;
	return civic && (t.flags & ROSTER_CIVILIAN) && (t.trainers & bld_bit(type)) ? bld_civ : owner_civ;
}

// BuildingAI and Attack/Ranged of a civ's structure: the generic row plus 0.28's one civ
// override, iber/defense_tower.xml (DefaultArrowCount +1, MinRange 12 m).
inline BuildingAIInfo civ_building_ai_info(uint8_t civ, uint8_t type) {
	BuildingAIInfo ai = building_ai_info(type);
	if (type == BLD_TOWER && civ == CIV_IBERIANS) {
		ai.default_arrows += 1;
		ai.min_range = 12.0f * COMBAT_DISTANCE_SCALE;
	}
	return ai;
}

inline bool civ_can_train(uint8_t civ, uint8_t type) {
	return (civ_train_mask(civ) & unit_bit(type)) != 0;
}

struct CivInfo {
	Civilization civ;
	const char *code;
	const char *name;
	const char *emblem_path;
	const char *description;
	// 0 A.D. civ bonuses that act on gathering and trade. Unit bonuses are
	// folded into each civ's roster; building and population bonuses are in
	// civ_player_bonus_table(). Both come from tools/extract_0ad_rosters.py.
	float metal_gather_mult; // Athenian "Silver Owls": Workers metal x this at Town and again at City
	float trade_gain_mult; // land traders: Persian "Darics" (units/pers/support_trader GainMultiplier)
	float ship_trade_gain_mult; // merchant ships: Carthaginian (units/cart/ship_merchant GainMultiplier)
	// Added to the Market/InternationalBonus of the civ's markets and docks:
	// Carthaginian "Desirable Products" (structures/cart/{market,dock}.xml op="add" 0.1).
	float international_bonus_add;
};

inline const CivInfo &civ_info(uint8_t civ) {
	static const CivInfo kTable[CIV_COUNT] = {
		// civ, code, name, emblem, desc, metal_mult, trader gain mult, merchant ship gain mult, international bonus add
		{CIV_ATHENIANS, "athen", "Athenians", "res://assets/textures/ui/emblems/athenians.png",
		 civ_bonus_text(CIV_ATHENIANS),
		 1.10f, 1.0f, 1.0f, 0.0f},
		{CIV_SPARTANS, "spart", "Spartans", "res://assets/textures/ui/emblems/spartans.png",
		 civ_bonus_text(CIV_SPARTANS),
		 1.0f, 1.0f, 1.0f, 0.0f},
		{CIV_PERSIANS, "pers", "Persians", "res://assets/textures/ui/emblems/persians.png",
		 civ_bonus_text(CIV_PERSIANS),
		 1.0f, 1.25f, 1.0f, 0.0f},
		{CIV_ROMANS, "rome", "Romans", "res://assets/textures/ui/emblems/romans.png",
		 civ_bonus_text(CIV_ROMANS),
		 1.0f, 1.0f, 1.0f, 0.0f},
		{CIV_CARTHAGINIANS, "cart", "Carthaginians", "res://assets/textures/ui/emblems/carthaginians.png",
		 civ_bonus_text(CIV_CARTHAGINIANS),
		 1.0f, 1.0f, 1.25f, 0.1f},
		{CIV_MACEDONIANS, "mace", "Macedonians", "res://assets/textures/ui/emblems/macedonians.png",
		 civ_bonus_text(CIV_MACEDONIANS),
		 1.0f, 1.0f, 1.0f, 0.0f},
		{CIV_BRITONS, "brit", "Britons", "res://assets/textures/ui/emblems/britons.png",
		 civ_bonus_text(CIV_BRITONS),
		 1.0f, 1.0f, 1.0f, 0.0f},
		{CIV_GAULS, "gaul", "Gauls", "res://assets/textures/ui/emblems/gauls.png",
		 civ_bonus_text(CIV_GAULS),
		 1.0f, 1.0f, 1.0f, 0.0f},
		{CIV_IBERIANS, "iber", "Iberians", "res://assets/textures/ui/emblems/iberians.png",
		 civ_bonus_text(CIV_IBERIANS),
		 1.0f, 1.0f, 1.0f, 0.0f},
		{CIV_MAURYANS, "maur", "Mauryans", "res://assets/textures/ui/emblems/mauryans.png",
		 civ_bonus_text(CIV_MAURYANS),
		 1.0f, 1.0f, 1.0f, 0.0f},
		{CIV_KUSHITES, "kush", "Kushites", "res://assets/textures/ui/emblems/kushites.png",
		 civ_bonus_text(CIV_KUSHITES),
		 1.0f, 1.0f, 1.0f, 0.0f},
		{CIV_PTOLEMIES, "ptol", "Ptolemies", "res://assets/textures/ui/emblems/ptolemies.png",
		 civ_bonus_text(CIV_PTOLEMIES),
		 1.0f, 1.0f, 1.0f, 0.0f},
		{CIV_SELEUCIDS, "sele", "Seleucids", "res://assets/textures/ui/emblems/seleucids.png",
		 civ_bonus_text(CIV_SELEUCIDS),
		 1.0f, 1.0f, 1.0f, 0.0f},
		{CIV_HAN, "han", "Han Chinese", "res://assets/textures/ui/emblems/han.png",
		 civ_bonus_text(CIV_HAN),
		 1.0f, 1.0f, 1.0f, 0.0f},
		// Gaia's row: names Gaia's structures (Building::civ); never offered as a civ.
		{CIV_GAIA, "gaia", "Gaia", "res://assets/textures/ui/emblems/athenians.png", "", 1.0f, 1.0f, 1.0f, 0.0f},
		// The Gallic emblem until the Germans' own is extracted (ASSET_MANIFEST_TODO.md).
		{CIV_GERMANS, "germ", "Germans", "res://assets/textures/ui/emblems/germans.png",
		 civ_bonus_text(CIV_GERMANS),
		 1.0f, 1.0f, 1.0f, 0.0f}
	};
	if (civ >= CIV_COUNT) return kTable[CIV_ATHENIANS];
	return kTable[civ];
}


// Prefer extracted `{civcode}_{model}` GLB; ModelLibrary falls back to the generic mesh.
inline const char *civ_unit_model(uint8_t civ, uint8_t type) {
	static char names[CIV_COUNT][UNIT_TYPE_COUNT][40];
	static bool ready = false;
	if (!ready) {
		for (uint8_t c = 0; c < CIV_COUNT; ++c) {
			const char *code = civ_info(c).code;
			for (uint8_t t = 0; t < UNIT_TYPE_COUNT; ++t) {
				std::snprintf(names[c][t], sizeof(names[c][t]), "%s_%s", code, unit_type_info(t).model_id);
			}
		}
		// 0.28's Macedonian male citizen is the Athenian one (units/mace/support_civilian.xml:
		// units/athenians/citizen_{phenotype}.xml); mace_villager.glb is the pre-0.28 woman.
		std::snprintf(names[CIV_MACEDONIANS][UNIT_VILLAGER], sizeof(names[0][0]), "athen_villager");
		ready = true;
	}
	if (civ >= CIV_COUNT) civ = CIV_ATHENIANS;
	if (type >= UNIT_TYPE_COUNT) type = UNIT_VILLAGER;
	return names[civ][type];
}

inline const char *civ_building_model(uint8_t civ, uint8_t type) {
	static char names[CIV_COUNT][BLD_TYPE_COUNT][48];
	static bool ready = false;
	if (!ready) {
		for (uint8_t c = 0; c < CIV_COUNT; ++c) {
			const char *code = civ_info(c).code;
			for (uint8_t t = 0; t < BLD_TYPE_COUNT; ++t) {
				std::snprintf(names[c][t], sizeof(names[c][t]), "%s_%s", code, building_type_info(t).model_id);
			}
		}
		ready = true;
	}
	if (civ >= CIV_COUNT) civ = CIV_ATHENIANS;
	if (type >= BLD_TYPE_COUNT) type = BLD_HOUSE;
	return names[civ][type];
}

inline const char *civ_building_rubble(uint8_t civ, uint8_t type) {
	static char cc_rubble[CIV_COUNT][48];
	static bool ready = false;
	if (!ready) {
		for (uint8_t c = 0; c < CIV_COUNT; ++c)
			std::snprintf(cc_rubble[c], sizeof(cc_rubble[c]), "%s_town_center_rubble", civ_info(c).code);
		ready = true;
	}
	if (civ >= CIV_COUNT) civ = CIV_ATHENIANS;
	if (is_civic_centre(type))
		return cc_rubble[civ];
	switch (building_research_base(type)) {
		case BLD_HOUSE:
		case BLD_APARTMENT:
		case BLD_STOREHOUSE:
		case BLD_CORRAL:
		case BLD_TOWER:
		case BLD_FARMSTEAD:
		case BLD_FIELD:
		case BLD_SENTRY_TOWER:
		case BLD_OUTPOST:
		case BLD_PALISADE:
		case BLD_PALISADE_GATE:
		case BLD_SIEGE_WALL:
		case BLD_SIEGE_WALL_GATE:
		case BLD_WALL_TOWER:
			return "rubble_small";
		default:
			return "rubble_large";
	}
}

// 0 A.D. VisualActor/FoundationActor per structure class.
inline const char *civ_building_foundation(uint8_t type) {
	switch (building_research_base(type)) {
		case BLD_HOUSE:
		case BLD_APARTMENT:
		case BLD_STOREHOUSE:
		case BLD_CORRAL:
			return "foundation_3x3";
		case BLD_TOWER:
		case BLD_SENTRY_TOWER:
		case BLD_OUTPOST:
			return "foundation_3x3_tower";
		case BLD_FARMSTEAD:
			return "foundation_5x5";
		case BLD_BARRACKS:
			return "foundation_6x6";
		case BLD_TEMPLE:
			return "foundation_4x6";
		case BLD_WALL_SEGMENT:
		case BLD_GATE:
		case BLD_PALISADE:
		case BLD_PALISADE_GATE:
		case BLD_SIEGE_WALL:
		case BLD_SIEGE_WALL_GATE:
		case BLD_WALL_TOWER:
			return "foundation_wall";
		case BLD_FIELD:
			return "field_plot";
		case BLD_DOCK:
			return "foundation_dock";
		case BLD_TOWN_CENTER:
		case BLD_MARKET:
		case BLD_FORTRESS:
		case BLD_WONDER:
			return "foundation_8x8";
		default:
			return "foundation_5x5";
	}
}

inline const char *civ_building_scaffold(uint8_t type) {
	switch (building_research_base(type)) {
		case BLD_HOUSE:
		case BLD_STOREHOUSE:
		case BLD_CORRAL:
		case BLD_TOWER:
		case BLD_WALL_SEGMENT:
		case BLD_GATE:
		case BLD_SENTRY_TOWER:
		case BLD_OUTPOST:
		case BLD_PALISADE:
		case BLD_PALISADE_GATE:
		case BLD_SIEGE_WALL:
		case BLD_SIEGE_WALL_GATE:
		case BLD_WALL_TOWER:
		case BLD_FIELD:
			return "scaffold_small";
		default:
			return "scaffold_large";
	}
}

// Fauna species, treasures and ruins: generated from 0 A.D.'s Gaia templates
// (tools/extract_0ad_fauna.py -> gaia_data.h, included below the namespace).

enum FaunaBehavior : uint8_t {
	FAUNA_IDLE_GRAZE = 0, // UnitAI ANIMAL IDLE.LINGERING (feeding)
	FAUNA_FLEE       = 1, // INDIVIDUAL.FLEEING
	FAUNA_ATTACK     = 2, // INDIVIDUAL.COMBAT
	FAUNA_HERDED     = 3,
	FAUNA_CARCASS    = 4,
	FAUNA_CORRALLED  = 5,
	FAUNA_ROAMING    = 6, // IDLE.ROAMING
	FAUNA_MOVING     = 7  // INDIVIDUAL.WALKING: an owned animal going where its owner sent it
};

} // namespace rtso

#include "gaia_data.h"

#endif
