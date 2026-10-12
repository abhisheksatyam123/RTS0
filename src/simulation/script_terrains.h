#pragma once

#include "terrain_biomes.h"

namespace rtso {

// 0 A.D. random-map scripts that paint their own terrains (maps/random/<script>.js
// constants) instead of taking them from the biome, by RTS0 map id
// (MapTypeInfo::id). The slots are TerrainBiome's and BiomeFlora's: a map with an
// entry here paints and plants from it instead of terrain_biome(MapTypeInfo::biome).
// Every name is one the script uses; where the script has no terrain for a role,
// the slot reuses its closest other terrain, as the comment says.
struct ScriptTerrains {
	const char *map_id; // MapTypeInfo::id
	TerrainBiome terrain;
	BiomeFlora flora;
};

inline const ScriptTerrains *script_terrains(const char *map_id) {
	static const ScriptTerrains kScripts[] = {
		// aegean_sea.js: carob and date palm forests; date, fan palm, poplar and cypress stragglers.
		{"aegean_sea",
		 {"aegean_sea", {"medit_grass_field", "medit_grass_field_a", "medit_grass_field_b"}, {"medit_grass_shrubs"},
		  {"medit_dirt_b", "medit_rocks_grass", "medit_rocks_shrubs", "medit_dirt_c"}, {"medit_cliff_aegean"},
		  {"medit_grass_shrubs", "medit_rocks_grass_shrubs"}, {"medit_dirt", "medit_dirt_c"}, "medit_city_pavement",
		  "medit_city_pavement", "medit_sand", "medit_sand_wet", "medit_sea_depths"},
		 {"aegean_sea", {{FLORA_BROADLEAF, FLORA_PALM}}, {FLORA_PALM, FLORA_BROADLEAF, FLORA_PINE},
		  {{FLORA_BROADLEAF, FLORA_PALM}}, FLORA_BERRY}},
		// anatolian_plateau.js: no water or cliffs, so cliff, hill and the shore slots reuse its dirt and grass.
		{"anatolian_plateau",
		 {"anatolian_plateau", {"steppe_grass_a", "steppe_grass_c", "steppe_grass_d"}, {"steppe_grass_c"},
		  {"steppe_grass_b", "steppe_grass_c", "steppe_grass_a", "steppe_grass_d"}, {"steppe_dirt_b"},
		  {"steppe_dirt_a", "steppe_dirt_b"}, {"steppe_dirt_a", "steppe_dirt_b"}, "road_stones", "road_stones", "steppe_grass_a",
		  "steppe_dirt_a", "steppe_dirt_a"},
		 {"anatolian_plateau", {{FLORA_BROADLEAF, FLORA_BROADLEAF}}, {FLORA_BUSH, FLORA_BROADLEAF},
		  {{FLORA_BROADLEAF, FLORA_BROADLEAF}}, FLORA_BERRY}},
		// arctic_summer.js: no roads, so road reuses tDirt; shore_blend reuses tHalfSnow.
		{"arctic_summer",
		 {"arctic_summer", {"alpine_grass_rocky"}, {"alpine_grass"}, {"alpine_grass", "polar_grass_snow", "ice_dirt", "polar_snow_rocks"},
		  {"polar_cliff_a", "polar_cliff_b"}, {"polar_ice_cracked"}, {"ice_dirt"}, "ice_dirt", "ice_dirt", "polar_grass_snow",
		  "alpine_shore_rocks", "polar_ice_b"},
		 {"arctic_summer", {{FLORA_BUSH, FLORA_BUSH}}, {FLORA_BUSH}, {{FLORA_BUSH, FLORA_BUSH}}, FLORA_BERRY}},
		// ardennes_forest.js: no hills, dirt or water, so hill reuses tCliff and dirt and the shore slots its mud and grass.
		{"ardennes_forest",
		 {"ardennes_forest", {"steppe_grass_03", "steppe_grass_03", "alpine_cliff_c", "temperate_grass_mud_01"},
		  {"steppe_grass_03", "temperate_grass_mud_01"}, {"steppe_grass_04", "aegean_grass_dirt_03", "alpine_grass_d"},
		  {"alpine_cliff_c", "temperate_cliff_01"}, {"alpine_cliff_c", "temperate_cliff_01"}, {"temperate_grass_mud_01"},
		  "new_alpine_citytile", "new_alpine_grass_dirt_a", "steppe_grass_04", "temperate_grass_mud_01", "temperate_grass_mud_01"},
		 {"ardennes_forest", {{FLORA_OAK, FLORA_PINE}}, {FLORA_OAK}, {{FLORA_PINE, FLORA_PINE}}, FLORA_BERRY}},
		// atlas_mountains.js: carob and aleppo pine forests; no hills or water, so hill reuses tCliff and its rocks,
		// and the shore slots its grass and dirt.
		{"atlas_mountains",
		 {"atlas_mountains", {"medit_rocks_grass", "medit_rocks_grass", "medit_rocks_grass_shrubs", "medit_rocks_shrubs"},
		  {"medit_grass_field_dry"}, {"medit_rocks_grass_shrubs", "medit_rocks_shrubs", "medit_rocks_grass", "medit_grass_wild"},
		  {"medit_cliff_italia"}, {"medit_cliff_italia", "medit_rocks_shrubs"}, {"medit_dirt"}, "medit_city_tile", "medit_city_tile",
		  "medit_rocks_grass", "medit_dirt", "medit_dirt"},
		 {"atlas_mountains", {{FLORA_BROADLEAF, FLORA_BROADLEAF}}, {FLORA_BROADLEAF, FLORA_PINE}, {{FLORA_PINE, FLORA_PINE}},
		  FLORA_BERRY}},
		// botswanan_haven.js: no forests (toona, baobab and bush stragglers), so forest reuses tGrassA and tGrassB.
		{"botswanan_haven",
		 {"botswanan_haven", {"savanna_grass_b_wetseason"}, {"savanna_shrubs_a_wetseason", "savanna_shrubs_a"},
		  {"savanna_shrubs_a_wetseason", "savanna_shrubs_a", "savanna_grass_a_wetseason"}, {"savanna_cliff_a"},
		  {"savanna_grass_a_wetseason"}, {"savanna_mud_a"}, "savanna_tile_a", "savanna_tile_a", "savanna_grass_b_wetseason",
		  "savanna_riparian_wet", "savanna_mud_a"},
		 {"botswanan_haven", {{FLORA_BROADLEAF, FLORA_BAOBAB}}, {FLORA_BROADLEAF, FLORA_BAOBAB, FLORA_BUSH},
		  {{FLORA_BUSH, FLORA_BUSH}}, FLORA_BERRY}},
		// corsica.js: forests stand on grass, so forest reuses tLushGrass; no dirt, so dirt reuses its rocks.
		{"corsica",
		 {"corsica", {"medit_grass_field", "medit_grass_field_b", "temp_grass_c"}, {"medit_grass_field", "medit_grass_field_a"},
		  {"medit_grass_field", "medit_grass_field_a", "medit_rocks_grass", "temp_grass"}, {"temp_cliff_b", "temp_cliff_a"},
		  {"medit_cliff_italia_grass", "medit_cliff_italia"}, {"medit_rocks_grass", "medit_rocks"}, "medit_city_tile",
		  "medit_rocks_grass", "medit_rocks_grass", "medit_sand", "medit_sea_depths"},
		 {"corsica", {{FLORA_PINE, FLORA_PALM}}, {FLORA_PINE, FLORA_PALM}, {{FLORA_PALM, FLORA_PALM}}, FLORA_BERRY}},
		// cycladic_archipelago.js: carob and beech main forests; palm, aleppo pine and poplar forests.
		{"cycladic_archipelago",
		 {"cycladic_archipelago", {"medit_rocks_grass", "medit_rocks_grass", "medit_dirt", "medit_rocks_grass_shrubs"},
		  {"medit_forestfloor_a"}, {"medit_grass_field_dry", "medit_grass_field_b", "medit_shrubs", "medit_rocks_grass_shrubs"},
		  {"medit_cliff_italia", "medit_cliff_italia_grass"}, {"medit_cliff_aegean_shrubs", "medit_cliff_italia_grass"},
		  {"medit_dirt"}, "medit_city_tile", "medit_city_tile", "medit_rocks_grass", "medit_sand", "medit_sea_depths"},
		 {"cycladic_archipelago", {{FLORA_BROADLEAF, FLORA_BROADLEAF}}, {FLORA_PALM, FLORA_PINE},
		  {{FLORA_PALM, FLORA_PALM}, {FLORA_PINE, FLORA_PINE}, {FLORA_BROADLEAF, FLORA_BROADLEAF}}, FLORA_BERRY}},
		// danubius.js: oak forests and poplar, beech and acacia forests; no hills, dirt or shore blend, so hill reuses
		// tCliff, dirt tShore and shore_blend tIsland.
		{"danubius",
		 {"danubius", {"temp_grass_aut", "temp_grass_plants_aut", "temp_grass_c_aut", "temp_grass_d_aut"}, {"temp_forestfloor_aut"},
		  {"medit_shrubs_golden", "medit_grass_field_b", "temp_grass_long_b_aut", "temp_grass_plants_aut"}, {"temp_cliff_a"},
		  {"temp_cliff_a"}, {"temp_dirt_gravel_b"}, "steppe_river_rocks", "steppe_river_rocks", "temp_grass_long_b_aut",
		  "temp_dirt_gravel_b", "steppe_river_rocks_wet"},
		 {"danubius", {{FLORA_OAK, FLORA_OAK}}, {FLORA_OAK, FLORA_BROADLEAF, FLORA_ACACIA},
		  {{FLORA_BROADLEAF, FLORA_BROADLEAF}, {FLORA_BROADLEAF, FLORA_ACACIA}}, FLORA_BERRY}},
		// dodecanese.js: carob and date palm forests; fan palm, poplar and cypress forests.
		{"dodecanese",
		 {"dodecanese", {"medit_grass_shrubs", "medit_grass_wild", "medit_rocks_grass_shrubs", "medit_dirt_b"}, {"medit_grass_wild"},
		  {"medit_plants_dirt", "medit_grass_flowers", "medit_rocks_grass", "medit_rocks_shrubs"}, {"medit_cliff_aegean"},
		  {"medit_grass_shrubs", "medit_rocks_grass_shrubs"}, {"medit_dirt", "medit_dirt_c"}, "medit_city_pavement",
		  "medit_city_pavement", "medit_sand", "medit_sand_wet", "medit_sea_depths"},
		 {"dodecanese", {{FLORA_BROADLEAF, FLORA_PALM}}, {FLORA_BROADLEAF, FLORA_PINE, FLORA_PALM},
		  {{FLORA_PALM, FLORA_BROADLEAF}, {FLORA_BROADLEAF, FLORA_PINE}}, FLORA_BERRY}},
		// elephantine.js: the island forest floor is pickRandom(tGrass) (first option here); no cliffs or shore,
		// so cliff and hill reuse its rocky dirt and the shore slots its cracked dirt and tWater.
		{"elephantine",
		 {"elephantine", {"savanna_dirt_rocks_a_red", "savanna_dirt_a_red", "savanna_dirt_b_red"},
		  {"savanna_forestfloor_b_red", "savanna_shrubs_a_wetseason"},
		  {"savanna_shrubs_a_wetseason", "alpine_grass_b_wild", "medit_shrubs_a", "steppe_grass_green_a"},
		  {"savanna_dirt_rocks_a", "savanna_dirt_rocks_a_red"}, {"savanna_dirt_rocks_a", "savanna_dirt_rocks_a_red"},
		  {"new_savanna_dirt_c", "new_savanna_dirt_d"}, "savanna_tile_a_red", "savanna_dirt_rocks_a", "savanna_dirt_plants_cracked",
		  "desert_sand_wet", "desert_sand_wet"},
		 {"elephantine", {{FLORA_PALM, FLORA_PALM}}, {FLORA_ACACIA}, {{FLORA_ACACIA, FLORA_ACACIA}}, FLORA_BERRY}},
		// english_channel.js: beech forests; no dirt, so dirt reuses tGrassC; shore_blend is tGrassPatchBlend.
		{"english_channel",
		 {"english_channel", {"temp_grass_long"}, {"temp_plants_bog"},
		  {"temp_grass_plants", "temp_plants_bog", "temp_mud_a", "temp_grass_d"}, {"temp_cliff_a", "temp_cliff_b"},
		  {"temp_highlands", "temp_grass_long_b"}, {"temp_mud_a"}, "temp_road", "temp_road_overgrown", "temp_grass_long_b",
		  "temp_dirt_gravel", "temp_dirt_gravel_b"},
		 {"english_channel", {{FLORA_BROADLEAF, FLORA_BROADLEAF}}, {FLORA_BROADLEAF}, {{FLORA_BROADLEAF, FLORA_BROADLEAF}},
		  FLORA_BERRY}},
		// extinct_volcano.js: dead tree and beech forests, oak and dead oak forests; no dirt, so dirt reuses tHillMedium2.
		{"extinct_volcano",
		 {"extinct_volcano", {"ocean_rock_a"}, {"ocean_rock_a", "ocean_rock_b"},
		  {"cliff volcanic light", "ocean_rock_a", "temp_grass_d", "temp_grass_clovers"}, {"cliff volcanic coarse", "cave_walls"},
		  {"cliff volcanic light", "ocean_rock_b"}, {"ocean_rock_b"}, "road1", "road1", "cliff volcanic light", "ocean_rock_a",
		  "ocean_rock_b"},
		 {"extinct_volcano", {{FLORA_BROADLEAF, FLORA_BROADLEAF}}, {FLORA_BROADLEAF, FLORA_OAK, FLORA_BUSH},
		  {{FLORA_OAK, FLORA_OAK}}, FLORA_BERRY}},
		// fields_of_meroe.js: its main and dirt terrains and berry come from the biome (g_Terrains, g_Gaia), so main,
		// cliff, hill and dirt reuse its own grass, sand and farmland, and fruit is the viewer's pick.
		{"fields_of_meroe",
		 {"fields_of_meroe", {"desert_grass_a_sand"}, {"savanna_forestfloor_b"},
		  {"desert_grass_a", "desert_grass_a_sand", "desert_farmland"}, {"desert_grass_a_sand"}, {"desert_grass_a_sand"},
		  {"desert_farmland"}, "savanna_tile_a", "desert_city_tile", "desert_grass_a", "savanna_riparian_wet", "savanna_riparian_wet"},
		 {"fields_of_meroe", {{FLORA_ACACIA, FLORA_ACACIA}}, {FLORA_BAOBAB, FLORA_ACACIA}, {{FLORA_PALM, FLORA_PALM}},
		  FLORA_DEFAULT}},
		// fortress.js: beech and oak forests, pine forests; no dirt or shore blend, so they reuse tGrassC and tGrassPatch.
		{"fortress",
		 {"fortress", {"temperate_grass_04", "temperate_grass_03", "temperate_grass_04"}, {"temperate_forestfloor_01"},
		  {"temperate_grass_05", "temperate_grass_02", "temperate_grass_mud_01", "temperate_grass_dirt_01"},
		  {"temperate_cliff_01", "temperate_cliff_02"}, {"temperate_rocks_dirt_01"}, {"temperate_grass_mud_01", "temperate_mud_01"},
		  "temperate_paving_03", "temperate_paving_03", "temperate_grass_dirt_01", "temperate_grass_mud_01", "temperate_mud_01"},
		 {"fortress", {{FLORA_BROADLEAF, FLORA_BROADLEAF}, {FLORA_OAK, FLORA_OAK}}, {FLORA_OAK, FLORA_BROADLEAF, FLORA_PINE},
		  {{FLORA_PINE, FLORA_PINE}}, FLORA_BERRY}},
		// guadalquivir_river.js: poplar and carob forests; no cliffs, hills or dirt, so they reuse its dry grass and
		// plants; shore_blend reuses tGrass.
		{"guadalquivir_river",
		 {"guadalquivir_river", {"medit_grass_field_a", "medit_grass_field_b"}, {"medit_grass_shrubs", "medit_plants_dirt"},
		  {"medit_grass_field_b", "medit_grass_field_brown", "medit_grass_field_dry", "medit_grass_shrubs"},
		  {"medit_grass_field_dry"}, {"medit_grass_field_dry", "medit_grass_field_brown"}, {"medit_plants_dirt"}, "medit_city_tile",
		  "medit_city_tile", "medit_grass_field_a", "sand_grass_25", "medit_sand_wet"},
		 {"guadalquivir_river", {{FLORA_BROADLEAF, FLORA_BROADLEAF}}, {FLORA_BROADLEAF}, {{FLORA_BROADLEAF, FLORA_BROADLEAF}},
		  FLORA_BERRY}},
		// india.js: palm groves on grass; no forest floor, cliffs, hills or shore blend, so they reuse its grass and dirt.
		{"india",
		 {"india", {"savanna_grass_a"}, {"savanna_grass_a", "savanna_dirt_a"}, {"savanna_dirt_a", "savanna_dirt_b"},
		  {"savanna_dirt_b"}, {"savanna_dirt_b"}, {"savanna_dirt_a", "savanna_dirt_b"}, "savanna_tile_a_dirt_red",
		  "savanna_tile_a_dirt_red", "savanna_dirt_a", "savanna_riparian_bank", "savanna_riparian_wet"},
		 {"india", {{FLORA_PALM, FLORA_PALM}}, {FLORA_PALM}, {{FLORA_PALM, FLORA_PALM}}, FLORA_BERRY}},
		// jebel_barkal.js: palm forests on pickRandom(tGrass) (two options here); acacia in the desert; no shore, so
		// shore reuses tWater.
		{"jebel_barkal",
		 {"jebel_barkal", {"desert_sand_dunes_100"}, {"savanna_shrubs_a_wetseason", "alpine_grass_b_wild"},
		  {"desert_grass_a", "steppe_grass_dirt_66", "medit_shrubs_a", "steppe_grass_green_a"},
		  {"savanna_cliff_a_red", "savanna_cliff_b_red"}, {"savanna_dirt_rocks_a", "savanna_dirt_rocks_b"},
		  {"new_savanna_dirt_c", "new_savanna_dirt_d"}, "savanna_tile_a", "road_med", "desert_grass_a", "desert_sand_wet",
		  "desert_sand_wet"},
		 {"jebel_barkal", {{FLORA_PALM, FLORA_PALM}}, {FLORA_ACACIA}, {{FLORA_ACACIA, FLORA_ACACIA}}, FLORA_BERRY}},
		// kerala.js: toona and palm forests; no hills or dirt, so hill reuses tCliff and dirt tGrassA.
		{"kerala",
		 {"kerala", {"tropic_grass_c", "tropic_grass_c", "tropic_grass_plants", "tropic_plants"}, {"tropic_grass_plants"},
		  {"tropic_plants_c", "tropic_grass_c", "tropic_plants", "tropic_plants_b"}, {"tropic_cliff_a", "tropic_cliff_a_plants"},
		  {"tropic_cliff_a", "tropic_cliff_a_plants"}, {"tropic_plants_c"}, "tropic_citytile_a", "tropic_citytile_plants",
		  "tropic_beach_dry_plants", "tropic_beach_dry", "tropic_beach_wet"},
		 {"kerala", {{FLORA_BROADLEAF, FLORA_BROADLEAF}}, {FLORA_BROADLEAF, FLORA_PALM}, {{FLORA_PALM, FLORA_PALM}}, FLORA_BERRY}},
		// latium.js: carob and beech main forests; palm, aleppo pine and poplar forests; no hills, so hill reuses tCliff.
		{"latium",
		 {"latium", {"medit_grass_field_dry", "medit_grass_field_brown", "medit_grass_field_b"}, {"medit_grass_wild"},
		  {"medit_grass_shrubs", "medit_grass_flowers", "medit_rocks_grass", "medit_grass_field_dry"},
		  {"medit_cliff_italia", "medit_cliff_italia_grass"}, {"medit_cliff_italia_grass", "medit_cliff_italia"}, {"medit_dirt"},
		  "medit_city_tile", "medit_city_tile", "medit_rocks_grass", "medit_sand", "medit_sea_depths"},
		 {"latium", {{FLORA_BROADLEAF, FLORA_BROADLEAF}}, {FLORA_BROADLEAF, FLORA_PINE},
		  {{FLORA_PALM, FLORA_PALM}, {FLORA_PINE, FLORA_PINE}, {FLORA_BROADLEAF, FLORA_BROADLEAF}}, FLORA_BERRY}},
		// lorraine_plain.js: beech and oak forests; no cliffs, hills or dirt, so cliff and dirt reuse tGrassC, hill
		// tGrassPatchBlend; shore_blend is tGrassPatchBlend.
		{"lorraine_plain",
		 {"lorraine_plain", {"temp_grass_long"}, {"temp_plants_bog"},
		  {"temp_grass_plants", "temp_plants_bog", "temp_mud_a", "temp_grass_d"}, {"temp_mud_a"}, {"temp_grass_long_b"},
		  {"temp_mud_a"}, "temp_road", "temp_road_overgrown", "temp_grass_long_b", "temp_plants_bog", "temp_mud_a"},
		 {"lorraine_plain", {{FLORA_BROADLEAF, FLORA_BROADLEAF}}, {FLORA_OAK, FLORA_BROADLEAF}, {{FLORA_OAK, FLORA_OAK}},
		  FLORA_BERRY}},
		// northern_lights.js: winter pines only and no fruit bush; no hills or dirt, so hill reuses tSnowE and tCliff,
		// dirt tForestFloor.
		{"northern_lights",
		 {"northern_lights", {"polar_snow_b"}, {"polar_tundra_snow"}, {"polar_ice_snow", "polar_ice", "polar_snow_a", "polar_snow_glacial"},
		  {"polar_snow_rocks"}, {"polar_snow_glacial", "polar_snow_rocks"}, {"polar_tundra_snow"}, "new_alpine_citytile",
		  "new_alpine_citytile", "alpine_shore_rocks_icy", "alpine_shore_rocks", "alpine_shore_rocks"},
		 {"northern_lights", {{FLORA_PINE, FLORA_PINE}}, {FLORA_PINE}, {{FLORA_PINE, FLORA_PINE}}, FLORA_DEFAULT}},
		// oasis.js: date palm forests; no cliffs, hills or shore blend, so they reuse its rocky dirt, dunes and sand.
		{"oasis_0ad",
		 {"oasis_0ad", {"desert_sand_dunes_100", "desert_dirt_cracks", "desert_sand_smooth", "desert_dirt_rough"},
		  {"desert_forestfloor_palms"}, {"desert_sand_dunes_50", "desert_dirt_rough_2", "desert_sand_smooth", "desert_dirt_rocks_2"},
		  {"desert_dirt_rocks_2"}, {"desert_dirt_rocks_2", "desert_sand_dunes_50"}, {"desert_dirt_rough", "desert_dirt_rough_2"},
		  "desert_city_tile", "desert_city_tile", "desert_sand_smooth", "dirta", "desert_sand_wet"},
		 {"oasis_0ad", {{FLORA_PALM, FLORA_PALM}}, {FLORA_PALM}, {{FLORA_PALM, FLORA_PALM}}, FLORA_BERRY}},
		// phoenician_levant.js: date palm and carob forests; no shore blend, so it reuses tRocksGrass.
		{"phoenician_levant",
		 {"phoenician_levant", {"medit_dirt", "medit_dirt_b", "medit_dirt_c", "medit_rocks_grass"}, {"medit_rocks_shrubs"},
		  {"medit_rocks_grass", "medit_rocks_shrubs", "medit_dirt_b", "medit_dirt_c"}, {"medit_cliff_aegean"},
		  {"medit_dirt_c", "medit_rocks_grass"}, {"medit_dirt", "medit_dirt_b"}, "medit_city_pavement", "medit_city_pavement",
		  "medit_rocks_grass", "medit_sand", "medit_sand_wet"},
		 {"phoenician_levant", {{FLORA_PALM, FLORA_PALM}, {FLORA_PALM, FLORA_BROADLEAF}}, {FLORA_PALM, FLORA_BROADLEAF, FLORA_PINE},
		  {{FLORA_PALM, FLORA_PALM}, {FLORA_PALM, FLORA_BROADLEAF}}, FLORA_BERRY}},
		// polar_sea.js: four terrains and no trees or fruit; every other role reuses tSecondary or tShore.
		{"polar_sea",
		 {"polar_sea", {"alpine_snow_01"}, {"alpine_snow_02"}, {"alpine_snow_02"}, {"alpine_ice_01"}, {"alpine_ice_01"},
		  {"alpine_snow_02"}, "alpine_snow_02", "alpine_snow_02", "alpine_snow_02", "alpine_ice_01", "alpine_ice_01"},
		 {"polar_sea", {}, {}, {}, FLORA_DEFAULT}},
		// pyrenean_sierra.js: aleppo pine and beech forests; no dirt, so dirt reuses tDirtyGrass.
		{"pyrenean_sierra",
		 {"pyrenean_sierra", {"new_alpine_grass_d", "new_alpine_grass_b", "new_alpine_grass_e"},
		  {"alpine_forrestfloor", "alpine_grass_d"}, {"new_alpine_grass_a", "alpine_grass_d", "alpine_grass_a", "alpine_grass_c"},
		  {"alpine_cliff_b", "alpine_cliff_c"}, {"alpine_grass_rocky", "alpine_snow_rocky"}, {"alpine_grass_c"},
		  "new_alpine_citytile", "new_alpine_citytile", "beach_scrub_50_", "beach_c", "sand_wet_a"},
		 {"pyrenean_sierra", {{FLORA_PINE, FLORA_BROADLEAF}}, {FLORA_PINE, FLORA_BROADLEAF}, {{FLORA_PINE, FLORA_BROADLEAF}},
		  FLORA_BERRY}},
		// rhine_marshlands.js: beech and oak forests; no cliffs or hills, so they reuse tMud and tGrassB.
		{"rhine_marshlands",
		 {"rhine_marshlands", {"temp_grass", "temp_grass", "temp_grass_d"}, {"temp_plants_bog"},
		  {"temp_grass_plants", "temp_plants_bog", "temp_grass_d", "temp_mud_a"}, {"temp_mud_a"}, {"temp_plants_bog"},
		  {"temp_mud_a"}, "temp_road", "temp_road_overgrown", "temp_grass_plants", "temp_plants_bog", "temp_mud_a"},
		 {"rhine_marshlands", {{FLORA_BROADLEAF, FLORA_BROADLEAF}}, {FLORA_OAK, FLORA_BROADLEAF}, {{FLORA_OAK, FLORA_OAK}},
		  FLORA_BERRY}},
		// saharan_oases.js: date palm forests on tLush; no cliffs or hills, so they reuse tDirtCracks and tSandDunes.
		{"saharan_oases",
		 {"saharan_oases", {"desert_sand_dunes_100"}, {"desert_grass_a"},
		  {"desert_sand_smooth", "desert_sand_dunes_50", "desert_grass_a_sand", "desert_dirt_cracks"}, {"desert_dirt_cracks"},
		  {"desert_sand_dunes_50"}, {"desert_dirt_rough_2", "desert_dirt_rough"}, "desert_city_tile", "desert_city_tile_plaza",
		  "desert_grass_a_sand", "desert_shore_stones", "desert_shore_stones_wet"},
		 {"saharan_oases", {{FLORA_PALM, FLORA_PALM}}, {FLORA_PALM}, {{FLORA_PALM, FLORA_PALM}}, FLORA_BERRY}},
		// sahel.js: baobab only; no forest floor, cliffs or shore blend, so they reuse its grass and rocky dirt.
		{"sahel",
		 {"sahel", {"savanna_grass_a"}, {"savanna_grass_b", "savanna_shrubs_a"},
		  {"savanna_grass_b", "savanna_shrubs_a", "savanna_dirt_rocks_a", "savanna_dirt_b"}, {"savanna_dirt_rocks_c"},
		  {"savanna_dirt_rocks_b", "savanna_dirt_rocks_c"}, {"savanna_dirt_rocks_a", "savanna_dirt_b"}, "savanna_tile_a",
		  "savanna_tile_a", "savanna_grass_b", "savanna_riparian_bank", "savanna_riparian_wet"},
		 {"sahel", {{FLORA_BAOBAB, FLORA_BAOBAB}}, {FLORA_BAOBAB}, {{FLORA_BAOBAB, FLORA_BAOBAB}}, FLORA_BERRY}},
		// sahel_watering_holes.js: baobab forests; no shore blend, so it reuses tGrassPatch.
		{"sahel_watering_holes",
		 {"sahel_watering_holes", {"savanna_grass_a"}, {"savanna_forestfloor_a"},
		  {"savanna_grass_b", "savanna_dirt_rocks_c", "savanna_dirt_rocks_a", "savanna_dirt_rocks_b"}, {"savanna_cliff_b"},
		  {"savanna_cliff_a"}, {"savanna_dirt_rocks_c", "savanna_dirt_rocks_a"}, "savanna_tile_a_red", "savanna_tile_a_red",
		  "savanna_grass_b", "savanna_riparian_bank", "savanna_riparian_wet"},
		 {"sahel_watering_holes", {{FLORA_BAOBAB, FLORA_BAOBAB}}, {FLORA_BAOBAB}, {{FLORA_BAOBAB, FLORA_BAOBAB}}, FLORA_BERRY}},
		// scythian_rivulet.js: dead tree and dead oak forests, dead tree forests; no dirt or shore blend, so they reuse
		// tTier3Terrain and tTier1Terrain.
		{"scythian_rivulet",
		 {"scythian_rivulet", {"alpine_snow_a"}, {"alpine_forrestfloor_snow", "polar_snow_rocks"}, {"snow rough", "snow_01", "snow rocks"},
		  {"alpine_cliff_a", "alpine_cliff_b"}, {"alpine_snow_glacial"}, {"snow rocks"}, "new_alpine_citytile", "alpine_snow_rocky",
		  "snow rough", "alpine_shore_rocks_icy", "polar_ice_b"},
		 {"scythian_rivulet", {{FLORA_BROADLEAF, FLORA_OAK}}, {FLORA_BROADLEAF, FLORA_OAK, FLORA_PINE, FLORA_BUSH},
		  {{FLORA_BROADLEAF, FLORA_BROADLEAF}}, FLORA_BERRY}},
		// syria.js: date palm forests and tamarix forests; no water, so the shore slots reuse its grass.
		{"syria",
		 {"syria", {"desert_dirt_rocks_1", "desert_dirt_cracks"}, {"forestfloor_dirty", "desert_forestfloor_palms"},
		  {"desert_grass_a_sand", "desert_grass_a", "medit_dirt_dry", "desert_dirt_rough"},
		  {"desert_cliff_persia_1", "desert_cliff_persia_2"}, {"desert_dirt_rocks_1", "desert_dirt_rocks_2"},
		  {"desert_dirt_rough", "desert_dirt_rough_2"}, "desert_shore_stones", "desert_grass_a_stones", "desert_grass_a",
		  "desert_grass_a_sand", "desert_grass_a_sand"},
		 {"syria", {{FLORA_PALM, FLORA_PALM}}, {FLORA_PALM, FLORA_BROADLEAF, FLORA_PINE}, {{FLORA_BROADLEAF, FLORA_BROADLEAF}},
		  FLORA_BERRY}},
		// volcanic_lands.js: dead trees only and no fruit bush; no hills, dirt or water, so hill reuses tCliff and dirt
		// and the shore slots its ash rocks.
		{"volcanic_lands",
		 {"volcanic_lands", {"cliff volcanic light", "ocean_rock_a", "ocean_rock_b"}, {"ocean_rock_b", "ocean_rock_a"},
		  {"cliff volcanic light", "ocean_rock_a", "ocean_rock_b"}, {"cliff volcanic coarse", "cave_walls"},
		  {"cliff volcanic coarse", "cave_walls"}, {"ocean_rock_b"}, "road1", "road1", "ocean_rock_a", "ocean_rock_a",
		  "ocean_rock_b"},
		 {"volcanic_lands", {{FLORA_BROADLEAF, FLORA_BROADLEAF}}, {FLORA_BROADLEAF}, {{FLORA_BROADLEAF, FLORA_BROADLEAF}},
		  FLORA_DEFAULT}},
	};
	for (const auto &s : kScripts)
		if (std::strcmp(map_id, s.map_id) == 0) return &s;
	return nullptr;
}

} // namespace rtso
