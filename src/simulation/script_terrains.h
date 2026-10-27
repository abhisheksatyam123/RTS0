#pragma once

#include "terrain_biomes.h"

namespace rtso {

// 0 A.D. random-map scripts that paint their own terrains (maps/random/<script>.js
// constants) instead of taking them from the biome, by RTS0 map id
// (MapTypeInfo::id). The slots are TerrainBiome's and BiomeFlora's: a map with an
// entry here paints and plants from it instead of terrain_biome(MapTypeInfo::biome).
// Every name is one the script uses; where the script has no terrain for a role,
// the slot reuses its closest other terrain, as the comment says.
// The tree slots' wood (BiomeFlora::tree*_wood) is each slot's template's
// ResourceSupply/Max; tools/check_tree_wood.py names the template of every slot,
// where a script's forest mixes more species than the two slots hold.
// rmgen paintTerrainBasedOnHeight: tiles from lo to hi 0 A.D. metres above the
// water line take one of the terrains, over whatever the generic rules painted
// (a later band over an earlier one), though not over roads, forest floor or
// mine dirt. Only for scripts whose synth_* keeps 0 A.D. metres (metres()).
// A band the script paints after its areas (below) has `late` set.
struct HeightBand {
	float lo, hi;
	const char *terrain[2];
	bool late;
};

// Areas a synth_* records (RmsGenerator record_area) where its script paints
// the area's own terrains.
enum AreaKind : uint8_t {
	AREA_NONE,
	AREA_HILL,     // createHills or the script's own hills
	AREA_MOUNTAIN, // createMountains
	AREA_ISLAND,
	AREA_PLAYER_ISLAND,
	AREA_LAKE,
	AREA_MARSH,
	AREA_VOLCANO,
	AREA_CORAL,    // a ring painted round an island, without elevation
	AREA_HIGHLANDS, // ngorongoro.js's highlands and eden, which take the highland patches
	AREA_GRASS,     // syria.js's grass round each base
	AREA_OASIS,     // oasis.js's central oasis: its palm band's floor
	AREA_OASIS_WATER, // and its water
};

// rmgen LayeredPainter(terrains, widths): a tile `d` tiles in from the area's
// edge (8-neighbour steps, 1 at the edge) takes the first layer whose summed
// widths reach d, else the last; each layer is one of up to 6 names. One layer
// with no widths is a TerrainPainter. Painted in the order the synth recorded the
// areas, over the generic rules and early bands, not over roads, forest floor or
// mine dirt.
struct AreaLayers {
	uint8_t kind; // AreaKind
	const char *layer[6][6];
	uint8_t width[5];
};

// rmgen createPatches / createLayeredPatches: for each size, `count` areas of
// ChainPlacer(1, floor(scaleByMapSize(3, 5)), size, 0.5) at random spots, painted
// with the layers (one layer: a TerrainPainter). Sizes and the count are
// scaleByMapSize(lo, hi) pairs; unused sizes are 0. The avoid* distances are the
// script's avoidClasses (-1: not avoided): water below the water line, hills
// (recorded hill and mountain areas and hill slopes), forest floor, the players'
// civic centres (clPlayer: 5 round each) and other patches (clDirt, clGrass).
// RTS0 paints these instead of its generic tier patches.
struct PatchSet {
	const char *layer[4][8];
	uint8_t width[3];
	float size[3][2];
	float count[2];
	int8_t avoid_water, avoid_hill, avoid_forest, avoid_player, avoid_patch;
};

struct ScriptTerrains {
	const char *map_id; // MapTypeInfo::id
	TerrainBiome terrain;
	BiomeFlora flora;
	HeightBand bands[6]; // in the script's paint order; unused ones have no terrain
	AreaLayers areas[4];
	PatchSet patches[4];
};

inline const ScriptTerrains *script_terrains(const char *map_id) {
	static const ScriptTerrains kScripts[] = {
		// aegean_sea.js: carob and date palm forests; date, fan palm, poplar and cypress stragglers.
		{"aegean_sea",
		 {"aegean_sea", {"medit_grass_field", "medit_grass_field_a", "medit_grass_field_b"}, {"medit_grass_shrubs"},
		  {"medit_dirt_b", "medit_rocks_grass", "medit_rocks_shrubs", "medit_dirt_c"}, {"medit_cliff_aegean"},
		  {"medit_grass_shrubs", "medit_rocks_grass_shrubs"}, {"medit_dirt", "medit_dirt_c"}, "medit_city_pavement",
		  "medit_city_pavement", "medit_sand", "medit_sand_wet", "medit_sea_depths"},
		 {"aegean_sea", {{FLORA_CAROB, FLORA_TREE_CRETAN_DATE_PALM_SHORT}}, {FLORA_FAN_PALM, FLORA_POPLAR, FLORA_CYPRESS},
		  {{FLORA_CAROB, FLORA_TREE_CRETAN_DATE_PALM_TALL}}, FLORA_BERRY,
		  {{200, 100}}, {200, 200, 200}, {{200, 200}}},
		 {{-1e9f, 1.0f, {"medit_sand_wet"}}, {1.0f, 2.0f, {"medit_sand"}}, {-1e9f, -3.0f, {"medit_sea_depths"}, true},
		  {-3.0f, -2.0f, {"medit_sea_coral_deep"}, true}, {-2.0f, -1.5f, {"medit_sea_coral_plants"}, true}},
		 {{AREA_HILL, {{"medit_grass_field"}, {"medit_cliff_aegean"}, {"medit_grass_shrubs", "medit_rocks_grass_shrubs", "medit_rocks_shrubs", "medit_rocks_grass", "medit_shrubs"}}, {1, 2}},
		  {AREA_MOUNTAIN, {{"medit_cliff_aegean"}}, {}},
		  {AREA_ISLAND, {{"medit_sand_wet"}, {"medit_sand"}, {"medit_grass_shrubs", "medit_rocks_grass_shrubs", "medit_rocks_shrubs", "medit_rocks_grass", "medit_shrubs"}}, {2, 1}}},
		 {{{{"medit_grass_field", "medit_grass_field_a"}, {"medit_grass_field_a", "medit_grass_field_b"}, {"medit_grass_field_b", "medit_grass_field"}}, {1, 1}, {{3.0f, 6.0f}, {5.0f, 10.0f}, {8.0f, 21.0f}}, {15.0f, 45.0f}, 2, 1, 0, 10, 2},
		  {{{"medit_rocks_shrubs"}, {"medit_rocks_grass"}, {"medit_dirt_b", "medit_dirt"}, {"medit_dirt_c", "medit_dirt"}}, {1, 1, 1}, {{3.0f, 6.0f}, {5.0f, 10.0f}, {8.0f, 21.0f}}, {15.0f, 45.0f}, 2, 1, 0, 10, 2}}},
		// anatolian_plateau.js: no water or cliffs, so cliff, hill and the shore slots reuse its dirt and grass.
		{"anatolian_plateau",
		 {"anatolian_plateau", {"steppe_grass_a", "steppe_grass_c", "steppe_grass_d"}, {"steppe_grass_c"},
		  {"steppe_grass_b", "steppe_grass_c", "steppe_grass_a", "steppe_grass_d"}, {"steppe_dirt_b"},
		  {"steppe_dirt_a", "steppe_dirt_b"}, {"steppe_dirt_a", "steppe_dirt_b"}, "road_stones", "road_stones", "steppe_grass_a",
		  "steppe_dirt_a", "steppe_dirt_a"},
		 {"anatolian_plateau", {{FLORA_POPLAR, FLORA_POPLAR}}, {FLORA_TREE_BUSH_TEMPERATE, FLORA_POPLAR},
		  {{FLORA_POPLAR, FLORA_POPLAR}}, FLORA_BERRY,
		  {{200, 200}}, {100, 200}, {{200, 200}}}, {}, {},
		 {{{{"steppe_grass_a", "steppe_grass_b", "steppe_grass_c", "steppe_grass_d", "steppe_grass_b", "steppe_grass_b", "steppe_grass_c", "steppe_grass_d"}, {"steppe_grass_a", "steppe_grass_b", "steppe_grass_c", "steppe_grass_d", "steppe_grass_b", "steppe_grass_b", "steppe_grass_c", "steppe_grass_d"}, {"steppe_grass_a", "steppe_grass_b", "steppe_grass_c", "steppe_grass_d", "steppe_grass_b", "steppe_grass_b", "steppe_grass_c", "steppe_grass_d"}}, {1, 1}, {{5.0f, 48.0f}, {6.0f, 84.0f}, {8.0f, 128.0f}}, {50.0f, 70.0f}, -1, 0, 0, 10, 2},
		  {{{"steppe_grass_a"}, {"steppe_dirt_a", "steppe_dirt_b"}}, {1}, {{5.0f, 32.0f}, {6.0f, 48.0f}, {7.0f, 80.0f}}, {50.0f, 90.0f}, -1, 0, 0, 10, 2},
		  {{{"steppe_grass_c"}, {"steppe_grass_b"}}, {1}, {{10.0f, 60.0f}, {15.0f, 90.0f}, {20.0f, 120.0f}}, {30.0f, 90.0f}, -1, 0, -1, 8, -1}}},
		// arctic_summer.js: no roads, so road reuses tDirt; shore_blend reuses tHalfSnow.
		{"arctic_summer",
		 {"arctic_summer", {"alpine_grass_rocky"}, {"alpine_grass"}, {"alpine_grass", "polar_grass_snow", "ice_dirt", "polar_snow_rocks"},
		  {"polar_cliff_a", "polar_cliff_b"}, {"polar_ice_cracked"}, {"ice_dirt"}, "ice_dirt", "ice_dirt", "polar_grass_snow",
		  "alpine_shore_rocks", "polar_ice_b"},
		 {"arctic_summer", {{FLORA_TREE_BUSH_BADLANDS, FLORA_TREE_BUSH_TEMPERATE}}, {FLORA_TREE_BUSH_BADLANDS}, {{FLORA_TREE_BUSH_BADLANDS, FLORA_TREE_BUSH_TEMPERATE}}, FLORA_BERRY,
		  {{100, 100}}, {100}, {{100, 100}}},
		 {{4.0f, 15.0f, {"polar_cliff_a", "polar_cliff_b"}, true}, {15.0f, 100.0f, {"polar_snow_rocks", "polar_ice"}, true}},
		 {{AREA_HILL, {{"alpine_grass_rocky"}, {"polar_cliff_a", "polar_cliff_b", "polar_cliff_snow"}, {"polar_ice_cracked"}}, {1, 2}},
		  {AREA_LAKE, {{"alpine_shore_rocks"}, {"polar_ice_b"}}, {1}}},
		 {{{{"ice_dirt", "polar_grass_snow", "ice_dirt"}, {"polar_grass_snow", "ice_dirt", "polar_snow_rocks", "polar_ice"}}, {2}, {{3.0f, 6.0f}, {5.0f, 10.0f}, {8.0f, 21.0f}}, {15.0f, 45.0f}, 3, 0, 0, 12, 5},
		  {{{"alpine_grass"}}, {}, {{2.0f, 4.0f}, {3.0f, 7.0f}, {5.0f, 15.0f}}, {15.0f, 45.0f}, 3, 0, 0, 12, 5},
		  {{{"alpine_grass"}}, {}, {{2.0f, 4.0f}, {3.0f, 7.0f}, {5.0f, 15.0f}}, {15.0f, 45.0f}, 3, 0, 0, 12, 5}}},
		// ardennes_forest.js: no hills, dirt or water, so hill reuses tCliff and dirt and the shore slots its mud and grass.
		{"ardennes_forest",
		 {"ardennes_forest", {"steppe_grass_03", "steppe_grass_03", "alpine_cliff_c", "temperate_grass_mud_01"},
		  {"steppe_grass_03", "temperate_grass_mud_01"}, {"steppe_grass_04", "aegean_grass_dirt_03", "alpine_grass_d"},
		  {"alpine_cliff_c", "temperate_cliff_01"}, {"alpine_cliff_c", "temperate_cliff_01"}, {"temperate_grass_mud_01"},
		  "new_alpine_citytile", "new_alpine_grass_dirt_a", "steppe_grass_04", "temperate_grass_mud_01", "temperate_grass_mud_01"},
		 {"ardennes_forest", {{FLORA_OAK, FLORA_PINE}}, {FLORA_TREE_OAK_LARGE}, {{FLORA_FIR, FLORA_PINE}}, FLORA_BERRY,
		  {{200, 200}}, {300}, {{200, 200}}}},
		// atlas_mountains.js: carob and aleppo pine forests; no hills or water, so hill reuses tCliff and its rocks,
		// and the shore slots its grass and dirt.
		{"atlas_mountains",
		 {"atlas_mountains", {"medit_rocks_grass", "medit_rocks_grass", "medit_rocks_grass_shrubs", "medit_rocks_shrubs"},
		  {"medit_grass_field_dry"}, {"medit_rocks_grass_shrubs", "medit_rocks_shrubs", "medit_rocks_grass", "medit_grass_wild"},
		  {"medit_cliff_italia"}, {"medit_cliff_italia", "medit_rocks_shrubs"}, {"medit_dirt"}, "medit_city_tile", "medit_city_tile",
		  "medit_rocks_grass", "medit_dirt", "medit_dirt"},
		 {"atlas_mountains", {{FLORA_CAROB, FLORA_CAROB}}, {FLORA_CAROB, FLORA_ALEPPO_PINE}, {{FLORA_ALEPPO_PINE, FLORA_ALEPPO_PINE}},
		  FLORA_BERRY,
		  {{200, 200}}, {200, 200}, {{200, 200}}}, {}, {},
		 {{{{"medit_rocks_grass"}, {"medit_dirt"}}, {2}, {{3.0f, 6.0f}, {5.0f, 10.0f}, {8.0f, 21.0f}}, {15.0f, 45.0f}, -1, 0, 0, 10, 3},
		  {{{"medit_rocks_grass_shrubs"}, {"medit_grass_wild"}}, {1}, {{2.0f, 4.0f}, {3.0f, 7.0f}, {5.0f, 15.0f}}, {15.0f, 45.0f}, -1, 0, 0, 10, 3}}},
		// botswanan_haven.js: no forests (toona, baobab and bush stragglers), so forest reuses tGrassA and tGrassB.
		{"botswanan_haven",
		 {"botswanan_haven", {"savanna_grass_b_wetseason"}, {"savanna_shrubs_a_wetseason", "savanna_shrubs_a"},
		  {"savanna_shrubs_a_wetseason", "savanna_shrubs_a", "savanna_grass_a_wetseason"}, {"savanna_cliff_a"},
		  {"savanna_grass_a_wetseason"}, {"savanna_mud_a"}, "savanna_tile_a", "savanna_tile_a", "savanna_grass_b_wetseason",
		  "savanna_riparian_wet", "savanna_mud_a"},
		 {"botswanan_haven", {{FLORA_TREE_TOONA, FLORA_BAOBAB}}, {FLORA_TREE_TOONA, FLORA_BAOBAB, FLORA_TREE_BUSH_TEMPERATE},
		  {{FLORA_TREE_BUSH_TEMPERATE, FLORA_TREE_BUSH_TEMPERATE}}, FLORA_BERRY,
		  {{200, 600}}, {200, 600, 100}, {{100, 100}}},
		 {},
		 {{AREA_HILL, {{"savanna_cliff_a"}, {"savanna_grass_a_wetseason"}}, {2}},
		  {AREA_MARSH, {{"savanna_grass_b_wetseason"}, {"savanna_riparian_wet"}, {"savanna_mud_a"}}, {1, 1}}}},
		// corsica.js: forests stand on grass, so forest reuses tLushGrass; no dirt, so dirt reuses its rocks.
		{"corsica",
		 {"corsica", {"medit_grass_field", "medit_grass_field_b", "temp_grass_c"}, {"medit_grass_field", "medit_grass_field_a"},
		  {"medit_grass_field", "medit_grass_field_a", "medit_rocks_grass", "temp_grass"}, {"temp_cliff_b", "temp_cliff_a"},
		  {"medit_cliff_italia_grass", "medit_cliff_italia"}, {"medit_rocks_grass", "medit_rocks"}, "medit_city_tile",
		  "medit_rocks_grass", "medit_rocks_grass", "medit_sand", "medit_sea_depths"},
		 {"corsica", {{FLORA_ALEPPO_PINE, FLORA_TREE_CRETAN_DATE_PALM_TALL}}, {FLORA_ALEPPO_PINE, FLORA_FAN_PALM}, {{FLORA_FAN_PALM, FLORA_CYPRESS}}, FLORA_BERRY,
		  {{200, 200}}, {200, 200}, {{200, 200}}}},
		// cycladic_archipelago.js: carob and beech main forests; palm, aleppo pine and poplar forests.
		{"cycladic_archipelago",
		 {"cycladic_archipelago", {"medit_rocks_grass", "medit_rocks_grass", "medit_dirt", "medit_rocks_grass_shrubs"},
		  {"medit_forestfloor_a"}, {"medit_grass_field_dry", "medit_grass_field_b", "medit_shrubs", "medit_rocks_grass_shrubs"},
		  {"medit_cliff_italia", "medit_cliff_italia_grass"}, {"medit_cliff_aegean_shrubs", "medit_cliff_italia_grass"},
		  {"medit_dirt"}, "medit_city_tile", "medit_city_tile", "medit_rocks_grass", "medit_sand", "medit_sea_depths"},
		 {"cycladic_archipelago", {{FLORA_CAROB, FLORA_BEECH}}, {FLORA_TREE_CRETAN_DATE_PALM_SHORT, FLORA_CYPRESS},
		  {{FLORA_FAN_PALM, FLORA_FAN_PALM}, {FLORA_ALEPPO_PINE, FLORA_ALEPPO_PINE}, {FLORA_POPLAR, FLORA_POPLAR}}, FLORA_BERRY,
		  {{200, 200}}, {100, 200}, {{200, 200}, {200, 200}, {200, 200}}},
		 {},
		 {{AREA_CORAL, {{"medit_sea_coral_deep"}, {"medit_sea_coral_plants"}}, {5}},
		  {AREA_ISLAND, {{"medit_sea_coral_plants"}, {"medit_sand_wet"}, {"medit_sand"}, {"medit_rocks_grass", "medit_sand", "medit_rocks_grass_shrubs"}, {"medit_rocks_grass", "medit_rocks_grass_shrubs"}, {"medit_rocks_grass", "medit_rocks_grass", "medit_dirt", "medit_rocks_grass_shrubs"}}, {1, 3, 1, 1, 2}},
		  {AREA_HILL, {{"medit_cliff_italia", "medit_cliff_italia", "medit_cliff_italia_grass"}, {"medit_cliff_aegean_shrubs", "medit_cliff_italia_grass", "medit_cliff_italia"}}, {2}}}},
		// danubius.js: oak forests and poplar, beech and acacia forests; no hills, dirt or shore blend, so hill reuses
		// tCliff, dirt tShore and shore_blend tIsland.
		{"danubius",
		 {"danubius", {"temp_grass_aut", "temp_grass_plants_aut", "temp_grass_c_aut", "temp_grass_d_aut"}, {"temp_forestfloor_aut"},
		  {"medit_shrubs_golden", "medit_grass_field_b", "temp_grass_long_b_aut", "temp_grass_plants_aut"}, {"temp_cliff_a"},
		  {"temp_cliff_a"}, {"temp_dirt_gravel_b"}, "steppe_river_rocks", "steppe_river_rocks", "temp_grass_long_b_aut",
		  "temp_dirt_gravel_b", "steppe_river_rocks_wet"},
		 {"danubius", {{FLORA_TREE_OAK_AUT, FLORA_TREE_OAK_AUT_NEW}}, {FLORA_OAK, FLORA_BEECH, FLORA_ACACIA},
		  {{FLORA_TREE_EURO_BEECH_AUT, FLORA_BEECH}, {FLORA_BEECH, FLORA_ACACIA}}, FLORA_BERRY,
		  {{200, 200}}, {200, 200, 200}, {{200, 200}, {200, 200}}},
		 {{-1e9f, 1.0f, {"steppe_river_rocks_wet"}}, {1.0f, 3.0f, {"temp_dirt_gravel_b"}}, {-20.0f, -3.0f, {"medit_sea_depths"}}}, {},
		 {{{{"medit_shrubs_golden", "grass_mediterranean_dry_1024test"}, {"grass_mediterranean_dry_1024test", "medit_grass_field_b"}, {"medit_grass_field_b", "medit_shrubs_golden"}}, {1, 1}, {{3.0f, 6.0f}, {5.0f, 10.0f}, {8.0f, 21.0f}}, {15.0f, 45.0f}, 2, 1, 0, 10, 2}}},
		// dodecanese.js: carob and date palm forests; fan palm, poplar and cypress forests.
		{"dodecanese",
		 {"dodecanese", {"medit_grass_shrubs", "medit_grass_wild", "medit_rocks_grass_shrubs", "medit_dirt_b"}, {"medit_grass_wild"},
		  {"medit_plants_dirt", "medit_grass_flowers", "medit_rocks_grass", "medit_rocks_shrubs"}, {"medit_cliff_aegean"},
		  {"medit_grass_shrubs", "medit_rocks_grass_shrubs"}, {"medit_dirt", "medit_dirt_c"}, "medit_city_pavement",
		  "medit_city_pavement", "medit_sand", "medit_sand_wet", "medit_sea_depths"},
		 {"dodecanese", {{FLORA_CAROB, FLORA_TREE_CRETAN_DATE_PALM_SHORT}}, {FLORA_POPLAR, FLORA_CYPRESS, FLORA_TREE_CRETAN_DATE_PALM_TALL},
		  {{FLORA_FAN_PALM, FLORA_POPLAR}, {FLORA_POPLAR, FLORA_CYPRESS}}, FLORA_BERRY,
		  {{200, 100}}, {200, 200, 200}, {{200, 200}, {200, 200}}},
		 {{-1e9f, -6.0f, {"medit_sea_depths"}, true}, {-6.0f, -4.0f, {"medit_sea_coral_deep"}, true},
		  {-4.0f, -2.0f, {"medit_sea_coral_plants"}, true}, {-2.0f, 1.0f, {"medit_sand_wet"}, true},
		  {1.0f, 3.0f, {"medit_sand"}, true}},
		 {{AREA_PLAYER_ISLAND, {{"medit_grass_shrubs", "medit_grass_wild", "medit_rocks_grass_shrubs", "medit_dirt_b", "medit_plants_dirt", "medit_grass_flowers"}}, {}}, {AREA_ISLAND, {{"medit_grass_shrubs", "medit_grass_wild", "medit_rocks_grass_shrubs", "medit_dirt_b", "medit_plants_dirt", "medit_grass_flowers"}}, {}},
		  {AREA_VOLCANO, {{"LavaTest06"}, {"LavaTest05"}}, {4}},
		  {AREA_HILL, {{"medit_cliff_aegean"}, {"medit_grass_shrubs", "medit_rocks_grass_shrubs", "medit_rocks_shrubs", "medit_rocks_grass", "medit_shrubs"}}, {2}}},
		 {{{{"medit_rocks_shrubs"}, {"medit_rocks_grass"}, {"medit_dirt_b", "medit_dirt"}, {"medit_dirt_c", "medit_dirt"}}, {1, 1, 1}, {{3.0f, 6.0f}, {5.0f, 10.0f}, {8.0f, 21.0f}}, {15.0f, 45.0f}, 4, 1, 1, -1, 2}}},
		// elephantine.js: the island forest floor is pickRandom(tGrass) (first option here); no cliffs or shore,
		// so cliff and hill reuse its rocky dirt and the shore slots its cracked dirt and tWater.
		{"elephantine",
		 {"elephantine", {"savanna_dirt_rocks_a_red", "savanna_dirt_a_red", "savanna_dirt_b_red"},
		  {"savanna_forestfloor_b_red", "savanna_shrubs_a_wetseason"},
		  {"savanna_shrubs_a_wetseason", "alpine_grass_b_wild", "medit_shrubs_a", "steppe_grass_green_a"},
		  {"savanna_dirt_rocks_a", "savanna_dirt_rocks_a_red"}, {"savanna_dirt_rocks_a", "savanna_dirt_rocks_a_red"},
		  {"new_savanna_dirt_c", "new_savanna_dirt_d"}, "savanna_tile_a_red", "savanna_dirt_rocks_a", "savanna_dirt_plants_cracked",
		  "desert_sand_wet", "desert_sand_wet"},
		 {"elephantine", {{FLORA_TREE_DATE_PALM, FLORA_TREE_DATE_PALM}}, {FLORA_ACACIA}, {{FLORA_ACACIA, FLORA_ACACIA}}, FLORA_BERRY,
		  {{200, 200}}, {200}, {{200, 200}}}, {}, {},
		 {{{{"new_savanna_dirt_c", "new_savanna_dirt_d", "savanna_dirt_b_red", "savanna_dirt_plants_cracked"}}, {}, {{5.0f, 15.0f}}, {5.0f, 30.0f}, 0, -1, 0, 12, 5}}},
		// english_channel.js: beech forests; no dirt, so dirt reuses tGrassC; shore_blend is tGrassPatchBlend.
		{"english_channel",
		 {"english_channel", {"temp_grass_long"}, {"temp_plants_bog"},
		  {"temp_grass_plants", "temp_plants_bog", "temp_mud_a", "temp_grass_d"}, {"temp_cliff_a", "temp_cliff_b"},
		  {"temp_highlands", "temp_grass_long_b"}, {"temp_mud_a"}, "temp_road", "temp_road_overgrown", "temp_grass_long_b",
		  "temp_dirt_gravel", "temp_dirt_gravel_b"},
		 {"english_channel", {{FLORA_BEECH, FLORA_BEECH}}, {FLORA_TREE_POPLAR}, {{FLORA_OAK, FLORA_OAK}},
		  FLORA_BERRY,
		  {{200, 200}}, {200}, {{200, 200}}},
		 {{-5.0f, 1.0f, {"temp_dirt_gravel_b"}}, {1.0f, 3.0f, {"temp_dirt_gravel"}}},
		 {{AREA_HILL, {{"temp_cliff_a", "temp_cliff_b"}, {"temp_cliff_a", "temp_cliff_b"}, {"temp_highlands", "temp_grass_long_b"}}, {1, 2}}},
		 {{{{"temp_grass", "temp_grass", "temp_grass_d", "temp_grass_plants"}, {"temp_plants_bog"}, {"temp_plants_bog", "temp_mud_a"}}, {1, 1}, {{3.0f, 6.0f}, {5.0f, 10.0f}, {8.0f, 21.0f}}, {15.0f, 45.0f}, 1, 0, 0, 6, 5},
		  {{{"temp_grass_long_b", "temp_grass_d", "temp_grass_clovers"}}, {}, {{2.0f, 4.0f}, {3.0f, 7.0f}, {5.0f, 15.0f}}, {15.0f, 45.0f}, 1, 0, 0, 6, 5}}},
		// extinct_volcano.js: dead tree and beech forests, oak and dead oak forests; no dirt, so dirt reuses tHillMedium2.
		{"extinct_volcano",
		 {"extinct_volcano", {"ocean_rock_a"}, {"ocean_rock_a", "ocean_rock_b"},
		  {"cliff volcanic light", "ocean_rock_a", "temp_grass_d", "temp_grass_clovers"}, {"cliff volcanic coarse", "cave_walls"},
		  {"cliff volcanic light", "ocean_rock_b"}, {"ocean_rock_b"}, "road1", "road1", "cliff volcanic light", "ocean_rock_a",
		  "ocean_rock_b"},
		 {"extinct_volcano", {{FLORA_DEAD, FLORA_BEECH}}, {FLORA_BEECH, FLORA_OAK, FLORA_TREE_BUSH_TEMPERATE},
		  {{FLORA_OAK, FLORA_TREE_OAK_DEAD}}, FLORA_BERRY,
		  {{200, 200}}, {200, 200, 100}, {{200, 200}}},
		 {},
		 // The bases' mountains (createMountain's tHillDark edge round the flattened tHillVeryDark), the
		 // volcano (createVolcano with no lava), the lakes and the hills.
		 {{AREA_MOUNTAIN, {{"cliff volcanic light"}, {"cliff volcanic coarse", "cave_walls"}}, {3}},
		  {AREA_VOLCANO, {{"cliff volcanic coarse", "cave_walls"}}, {}},
		  {AREA_LAKE, {{"cliff volcanic light"}, {"ocean_rock_a"}, {"ocean_rock_b"}}, {1, 1}},
		  {AREA_HILL, {{"cliff volcanic light"}}, {}}},
		 // "Creating hill patches": ClumpPlacers of scaleByMapSize(3, 48), (5, 84) and (8, 128) tiles, as
		 // chains of about a tenth as many circles, scaleByMapSize(20, 80) of each type and size (clGrass);
		 // then the grass patches, 5 from those. clBumps (ground 10 m up) is left to the hills' distance.
		 {{{{"ocean_rock_a"}, {"cliff volcanic light"}}, {1}, {{1.0f, 4.0f}, {1.0f, 7.0f}, {1.0f, 11.0f}}, {20.0f, 80.0f}, 3, 0, 0, 0, -1},
		  {{{"cliff volcanic light"}, {"ocean_rock_b"}}, {1}, {{1.0f, 4.0f}, {1.0f, 7.0f}, {1.0f, 11.0f}}, {20.0f, 80.0f}, 3, 0, 0, 0, -1},
		  {{{"ocean_rock_a"}, {"ocean_rock_b"}}, {1}, {{1.0f, 4.0f}, {1.0f, 7.0f}, {1.0f, 11.0f}}, {20.0f, 80.0f}, 3, 0, 0, 0, -1},
		  {{{"temp_grass_long_b"}, {"temp_grass_d", "temp_grass_clovers"}}, {1}, {{2.0f, 4.0f}, {3.0f, 7.0f}, {5.0f, 15.0f}}, {3.0f, 8.0f}, 1, 0, 0, 0, 5}}},
		// fields_of_meroe.js: its main and dirt terrains and berry come from the biome (g_Terrains, g_Gaia), so main,
		// cliff, hill and dirt reuse its own grass, sand and farmland, and fruit is the viewer's pick.
		{"fields_of_meroe",
		 {"fields_of_meroe", {"desert_grass_a_sand"}, {"savanna_forestfloor_b"},
		  {"desert_grass_a", "desert_grass_a_sand", "desert_farmland"}, {"desert_grass_a_sand"}, {"desert_grass_a_sand"},
		  {"desert_farmland"}, "savanna_tile_a", "desert_city_tile", "desert_grass_a", "savanna_riparian_wet", "savanna_riparian_wet"},
		 {"fields_of_meroe", {{FLORA_ACACIA, FLORA_ACACIA}}, {FLORA_BAOBAB, FLORA_ACACIA}, {{FLORA_TREE_DATE_PALM, FLORA_TREE_CRETAN_DATE_PALM_SHORT}},
		  FLORA_DEFAULT,
		  {{200, 200}}, {600, 200}, {{200, 100}}}},
		// fortress.js: beech and oak forests, pine forests; no dirt or shore blend, so they reuse tGrassC and tGrassPatch.
		{"fortress",
		 {"fortress", {"temperate_grass_04", "temperate_grass_03", "temperate_grass_04"}, {"temperate_forestfloor_01"},
		  {"temperate_grass_05", "temperate_grass_02", "temperate_grass_mud_01", "temperate_grass_dirt_01"},
		  {"temperate_cliff_01", "temperate_cliff_02"}, {"temperate_rocks_dirt_01"}, {"temperate_grass_mud_01", "temperate_mud_01"},
		  "temperate_paving_03", "temperate_paving_03", "temperate_grass_dirt_01", "temperate_grass_mud_01", "temperate_mud_01"},
		 {"fortress", {{FLORA_TREE_EURO_BEECH_AUT, FLORA_TREE_EURO_BEECH_AUT}, {FLORA_TREE_OAK_AUT, FLORA_TREE_OAK_AUT}}, {FLORA_TREE_OAK_AUT, FLORA_TREE_EURO_BEECH_AUT, FLORA_PINE},
		  {{FLORA_PINE, FLORA_PINE}}, FLORA_BERRY,
		  {{200, 200}, {200, 200}}, {200, 200, 200}, {{200, 200}}},
		 {},
		 {{AREA_LAKE, {{"temperate_grass_mud_01"}, {"temperate_mud_01"}}, {1}},
		  {AREA_HILL, {{"temperate_cliff_01", "temperate_cliff_02"}, {"temperate_cliff_01", "temperate_cliff_02"}, {"temperate_rocks_dirt_01"}}, {1, 2}}},
		 {{{{"temperate_grass_04", "temperate_grass_03", "temperate_grass_04", "temperate_grass_05"}, {"temperate_grass_05", "temperate_grass_02"}, {"temperate_grass_02", "temperate_grass_mud_01"}}, {1, 1}, {{3.0f, 6.0f}, {5.0f, 10.0f}, {8.0f, 21.0f}}, {15.0f, 45.0f}, 1, 0, 0, 1, 5},
		  {{{"temperate_grass_dirt_01"}}, {}, {{2.0f, 4.0f}, {3.0f, 7.0f}, {5.0f, 15.0f}}, {15.0f, 45.0f}, 1, 0, 0, 1, 5}}},
		// guadalquivir_river.js: poplar and carob forests; no cliffs, hills or dirt, so they reuse its dry grass and
		// plants; shore_blend reuses tGrass.
		{"guadalquivir_river",
		 {"guadalquivir_river", {"medit_grass_field_a", "medit_grass_field_b"}, {"medit_grass_shrubs", "medit_plants_dirt"},
		  {"medit_grass_field_b", "medit_grass_field_brown", "medit_grass_field_dry", "medit_grass_shrubs"},
		  {"medit_grass_field_dry"}, {"medit_grass_field_dry", "medit_grass_field_brown"}, {"medit_plants_dirt"}, "medit_city_tile",
		  "medit_city_tile", "medit_grass_field_a", "sand_grass_25", "medit_sand_wet"},
		 {"guadalquivir_river", {{FLORA_TREE_POPLAR, FLORA_TREE_POPLAR}}, {FLORA_CAROB}, {{FLORA_CAROB, FLORA_CAROB}},
		  FLORA_BERRY,
		  {{200, 200}}, {200}, {{200, 200}}}, {}, {},
		 {{{{"medit_grass_field_a", "medit_grass_field_b", "medit_grass_field_b"}, {"medit_grass_field_b", "medit_grass_field_brown"}, {"medit_grass_field_brown", "medit_grass_field_dry"}}, {1, 1}, {{3.0f, 6.0f}, {5.0f, 10.0f}, {8.0f, 21.0f}}, {15.0f, 45.0f}, 7, -1, 0, 8, 3},
		  {{{"medit_grass_shrubs"}}, {}, {{2.0f, 4.0f}, {3.0f, 7.0f}, {5.0f, 15.0f}}, {15.0f, 45.0f}, 7, -1, 0, 8, 3}}},
		// india.js: palm groves on grass; no forest floor, cliffs, hills or shore blend, so they reuse its grass and dirt.
		{"india",
		 {"india", {"savanna_grass_a"}, {"savanna_grass_a", "savanna_dirt_a"}, {"savanna_dirt_a", "savanna_dirt_b"},
		  {"savanna_dirt_b"}, {"savanna_dirt_b"}, {"savanna_dirt_a", "savanna_dirt_b"}, "savanna_tile_a_dirt_red",
		  "savanna_tile_a_dirt_red", "savanna_dirt_a", "savanna_riparian_bank", "savanna_riparian_wet"},
		 {"india", {{FLORA_TROPIC_PALM, FLORA_TREE_STRANGLER}}, {FLORA_TROPIC_PALM}, {{FLORA_TREE_PALM_PALMYRA, FLORA_TREE_PALM_TROPICAL}}, FLORA_BERRY,
		  {{200, 200}}, {200}, {{200, 200}}}},
		// jebel_barkal.js: palm forests on pickRandom(tGrass) (two options here); acacia in the desert; no shore, so
		// shore reuses tWater.
		{"jebel_barkal",
		 {"jebel_barkal", {"desert_sand_dunes_100"}, {"savanna_shrubs_a_wetseason", "alpine_grass_b_wild"},
		  {"desert_grass_a", "steppe_grass_dirt_66", "medit_shrubs_a", "steppe_grass_green_a"},
		  {"savanna_cliff_a_red", "savanna_cliff_b_red"}, {"savanna_dirt_rocks_a", "savanna_dirt_rocks_b"},
		  {"new_savanna_dirt_c", "new_savanna_dirt_d"}, "savanna_tile_a", "road_med", "desert_grass_a", "desert_sand_wet",
		  "desert_sand_wet"},
		 {"jebel_barkal", {{FLORA_TREE_DATE_PALM, FLORA_TREE_DATE_PALM}}, {FLORA_ACACIA}, {{FLORA_ACACIA, FLORA_ACACIA}}, FLORA_BERRY,
		  {{200, 200}}, {200}, {{200, 200}}}},
		// kerala.js: toona and palm forests; no hills or dirt, so hill reuses tCliff and dirt tGrassA.
		{"kerala",
		 {"kerala", {"tropic_grass_c", "tropic_grass_c", "tropic_grass_plants", "tropic_plants"}, {"tropic_grass_plants"},
		  {"tropic_plants_c", "tropic_grass_c", "tropic_plants", "tropic_plants_b"}, {"tropic_cliff_a", "tropic_cliff_a_plants"},
		  {"tropic_cliff_a", "tropic_cliff_a_plants"}, {"tropic_plants_c"}, "tropic_citytile_a", "tropic_citytile_plants",
		  "tropic_beach_dry_plants", "tropic_beach_dry", "tropic_beach_wet"},
		 {"kerala", {{FLORA_TREE_TOONA, FLORA_TREE_TOONA}}, {FLORA_TREE_TOONA, FLORA_TROPIC_PALM}, {{FLORA_TROPIC_PALM, FLORA_TROPIC_PALM}}, FLORA_BERRY,
		  {{200, 200}}, {200, 200}, {{200, 200}}}},
		// latium.js: carob and beech main forests; palm, aleppo pine and poplar forests; no hills, so hill reuses tCliff.
		{"latium",
		 {"latium", {"medit_grass_field_dry", "medit_grass_field_brown", "medit_grass_field_b"}, {"medit_grass_wild"},
		  {"medit_grass_shrubs", "medit_grass_flowers", "medit_rocks_grass", "medit_grass_field_dry"},
		  {"medit_cliff_italia", "medit_cliff_italia_grass"}, {"medit_cliff_italia_grass", "medit_cliff_italia"}, {"medit_dirt"},
		  "medit_city_tile", "medit_city_tile", "medit_rocks_grass", "medit_sand", "medit_sea_depths"},
		 {"latium", {{FLORA_CAROB, FLORA_BEECH}}, {FLORA_POPLAR, FLORA_ALEPPO_PINE},
		  {{FLORA_FAN_PALM, FLORA_FAN_PALM}, {FLORA_ALEPPO_PINE, FLORA_ALEPPO_PINE}, {FLORA_POPLAR, FLORA_POPLAR}}, FLORA_BERRY,
		  {{200, 200}}, {200, 200}, {{200, 200}, {200, 200}, {200, 200}}}},
		// lorraine_plain.js: beech and oak forests; no cliffs, hills or dirt, so cliff and dirt reuse tGrassC, hill
		// tGrassPatchBlend; shore_blend is tGrassPatchBlend.
		{"lorraine_plain",
		 {"lorraine_plain", {"temp_grass_long"}, {"temp_plants_bog"},
		  {"temp_grass_plants", "temp_plants_bog", "temp_mud_a", "temp_grass_d"}, {"temp_mud_a"}, {"temp_grass_long_b"},
		  {"temp_mud_a"}, "temp_road", "temp_road_overgrown", "temp_grass_long_b", "temp_plants_bog", "temp_mud_a"},
		 {"lorraine_plain", {{FLORA_BEECH, FLORA_BEECH}}, {FLORA_OAK, FLORA_BEECH}, {{FLORA_OAK, FLORA_OAK}},
		  FLORA_BERRY,
		  {{200, 200}}, {200, 200}, {{200, 200}}}},
		// lower_nubia.js: tSand everywhere, tPlateau above heightPlateau2 (heightScale(38): the band is its 30.4 m on
		// a 256-tile map), tCliffUpper on cliffs, tRoad city patches, tWater below the water line. No forest floor,
		// tiers or hills, so those reuse tSand; mine dirt reuses tPlateau. The Nile palms (oPalms) take the forest
		// slots (date palms, palm_tropic, medit_fan_palm); tree3 holds the straggler acacias, dead trees and
		// bush_badlands (one bush to two trees), which plant_flora mixes per tree on this map.
		{"lower_nubia",
		 {"lower_nubia", {"desert_sand_dunes_100"}, {"desert_sand_dunes_100"}, {},
		  {"medit_cliff_italia", "medit_cliff_italia_grass"}, {"desert_sand_dunes_100"}, {"savanna_dirt_a", "savanna_dirt_b"},
		  "savanna_tile_a", "savanna_tile_a", "desert_sand_wet", "desert_sand_wet", "desert_sand_wet"},
		 {"lower_nubia", {{FLORA_TREE_DATE_PALM, FLORA_TROPIC_PALM}}, {FLORA_ACACIA, FLORA_ACACIA, FLORA_DEAD, FLORA_DEAD, FLORA_TREE_BUSH_BADLANDS},
		  {{FLORA_FAN_PALM, FLORA_TREE_CRETAN_DATE_PALM_TALL}}, FLORA_BERRY,
		  {{200, 200}}, {200, 200, 200, 200, 100}, {{200, 200}}},
		 {{30.4f, 1e9f, {"savanna_dirt_a", "savanna_dirt_b"}}}},
		// ngorongoro.js: the savanna biome's savanna_grass_01 ground (the RandomMap's terrain), cliff, hill and dirt,
		// the script's roads and baobabs only (rmgen2 addForests plants no savanna forest, so forest floor reuses the
		// ground); the lowland patches are the tiers (its tier4 "savanna_forest_floor_a" names no 0 A.D. terrain, so
		// savanna_forestfloor_a), the highland patches the highlands' and eden's area. No water.
		{"ngorongoro",
		 {"ngorongoro", {"savanna_grass_01"}, {"savanna_grass_01"},
		  {"savanna_dirt_rocks_a", "savanna_grass_a", "savanna_grass_b", "savanna_forestfloor_a"}, {"sahara_rock_01"},
		  {"sahara_rock_01"}, {"aegean_grass_02"}, "road2", "savanna_riparian_dry", "steppe_grass_04", "steppe_grass_04",
		  "india_sand_01"},
		 {"ngorongoro", {{FLORA_BAOBAB, FLORA_BAOBAB}}, {FLORA_BAOBAB}, {{FLORA_BAOBAB, FLORA_BAOBAB}}, FLORA_BERRY,
		  {{600, 600}}, {600}, {{600, 600}}},
		 {},
		 {{AREA_HIGHLANDS, {{"savanna_grass_01", "savanna_grass_01", "savanna_grass_a_wetseason", "savanna_grass_b_wetseason",
		                     "savanna_shrubs_a_wetseason", "savanna_shrubs_b"}}, {}}}},
		// northern_lights.js: winter pines only and no fruit bush; no hills or dirt, so hill reuses tSnowE and tCliff,
		// dirt tForestFloor.
		{"northern_lights",
		 {"northern_lights", {"polar_snow_b"}, {"polar_tundra_snow"}, {"polar_ice_snow", "polar_ice", "polar_snow_a", "polar_snow_glacial"},
		  {"polar_snow_rocks"}, {"polar_snow_glacial", "polar_snow_rocks"}, {"polar_tundra_snow"}, "new_alpine_citytile",
		  "new_alpine_citytile", "alpine_shore_rocks_icy", "alpine_shore_rocks", "alpine_shore_rocks"},
		 {"northern_lights", {{FLORA_TREE_PINE_W, FLORA_TREE_PINE_W}}, {FLORA_TREE_PINE_W}, {{FLORA_TREE_PINE_W, FLORA_TREE_PINE_W}}, FLORA_DEFAULT,
		  {{200, 200}}, {200}, {{200, 200}}}},
		// oasis.js: date palm forests; no cliffs, hills or shore blend, so they reuse its rocky dirt, dunes and sand.
		{"oasis_0ad",
		 {"oasis_0ad", {"desert_sand_dunes_100", "desert_dirt_cracks", "desert_sand_smooth", "desert_dirt_rough"},
		  {"desert_forestfloor_palms"}, {"desert_sand_dunes_50", "desert_dirt_rough_2", "desert_sand_smooth", "desert_dirt_rocks_2"},
		  {"desert_dirt_rocks_2"}, {"desert_dirt_rocks_2", "desert_sand_dunes_50"}, {"desert_dirt_rough", "desert_dirt_rough_2"},
		  "desert_city_tile", "desert_city_tile", "desert_sand_smooth", "dirta", "desert_sand_wet"},
		 {"oasis_0ad", {{FLORA_TREE_CRETAN_DATE_PALM_SHORT, FLORA_TREE_CRETAN_DATE_PALM_TALL}}, {FLORA_TREE_CRETAN_DATE_PALM_SHORT}, {{FLORA_TREE_CRETAN_DATE_PALM_TALL, FLORA_TREE_CRETAN_DATE_PALM_SHORT}}, FLORA_BERRY,
		  {{100, 200}}, {100}, {{200, 100}}},
		 {},
		 // The players' ponds, the central oasis (palm band, then water) and the dunes.
		 {{AREA_LAKE, {{"dirta"}, {"desert_sand_wet"}}, {1}},
		  {AREA_OASIS, {{"desert_forestfloor_palms"}}, {}},
		  {AREA_OASIS_WATER, {{"desert_sand_wet"}}, {}},
		  {AREA_HILL, {{"desert_sand_dunes_50"}}, {}}},
		 // "Creating dirt patches": scaleByMapSize(15, 50) ClumpPlacers of 80 tiles (as chains of 8
		 // circles) in tDirt, 10 from the bases, 6 from their resources and 4 from the oasis and forests;
		 // the dunes painted after them in 0.28 are kept clear here.
		 {{{{"desert_dirt_rough", "desert_dirt_rough", "desert_dirt_rough", "desert_dirt_rough_2", "desert_dirt_rocks_2"}}, {}, {{8.0f, 8.0f}},
		   {15.0f, 50.0f}, 4, 0, 4, 10, -1}}},
		// phoenician_levant.js: date palm and carob forests; no shore blend, so it reuses tRocksGrass.
		{"phoenician_levant",
		 {"phoenician_levant", {"medit_dirt", "medit_dirt_b", "medit_dirt_c", "medit_rocks_grass"}, {"medit_rocks_shrubs"},
		  {"medit_rocks_grass", "medit_rocks_shrubs", "medit_dirt_b", "medit_dirt_c"}, {"medit_cliff_aegean"},
		  {"medit_dirt_c", "medit_rocks_grass"}, {"medit_dirt", "medit_dirt_b"}, "medit_city_pavement", "medit_city_pavement",
		  "medit_rocks_grass", "medit_sand", "medit_sand_wet"},
		 {"phoenician_levant", {{FLORA_TREE_CRETAN_DATE_PALM_SHORT, FLORA_TREE_CRETAN_DATE_PALM_TALL}, {FLORA_TREE_CRETAN_DATE_PALM_SHORT, FLORA_CAROB}}, {FLORA_FAN_PALM, FLORA_POPLAR, FLORA_CYPRESS},
		  {{FLORA_TREE_CRETAN_DATE_PALM_TALL, FLORA_TREE_CRETAN_DATE_PALM_SHORT}, {FLORA_TREE_CRETAN_DATE_PALM_TALL, FLORA_CAROB}}, FLORA_BERRY,
		  {{100, 200}, {100, 200}}, {200, 200, 200}, {{200, 100}, {200, 200}}},
		 {},
		 {{AREA_HILL, {{"medit_cliff_aegean"}, {"medit_dirt", "medit_dirt_b", "medit_dirt_c", "medit_rocks_grass", "medit_rocks_grass"}}, {2}}, {AREA_ISLAND, {{"medit_sand"}, {"medit_dirt", "medit_dirt_b", "medit_dirt_c", "medit_rocks_grass", "medit_rocks_grass"}}, {12}}}},
		// polar_sea.js: four terrains and no trees or fruit; every other role reuses tSecondary or tShore.
		{"polar_sea",
		 {"polar_sea", {"alpine_snow_01"}, {"alpine_snow_02"}, {"alpine_snow_02"}, {"alpine_ice_01"}, {"alpine_ice_01"},
		  {"alpine_snow_02"}, "alpine_snow_02", "alpine_snow_02", "alpine_snow_02", "alpine_ice_01", "alpine_ice_01"},
		 {"polar_sea", {}, {}, {}, FLORA_DEFAULT}, {}, {},
		 {{{{"alpine_snow_02"}}, {}, {{10.0f, 20.0f}, {20.0f, 30.0f}}, {15.0f, 45.0f}, 3, -1, -1, 12, 5}}},
		// pyrenean_sierra.js: aleppo pine and beech forests; no dirt, so dirt reuses tDirtyGrass.
		{"pyrenean_sierra",
		 {"pyrenean_sierra", {"new_alpine_grass_d", "new_alpine_grass_b", "new_alpine_grass_e"},
		  {"alpine_forrestfloor", "alpine_grass_d"}, {"new_alpine_grass_a", "alpine_grass_d", "alpine_grass_a", "alpine_grass_c"},
		  {"alpine_cliff_b", "alpine_cliff_c"}, {"alpine_grass_rocky", "alpine_snow_rocky"}, {"alpine_grass_c"},
		  "new_alpine_citytile", "new_alpine_citytile", "beach_scrub_50_", "beach_c", "sand_wet_a"},
		 {"pyrenean_sierra", {{FLORA_ALEPPO_PINE, FLORA_BEECH}}, {FLORA_ALEPPO_PINE, FLORA_BEECH}, {{FLORA_ALEPPO_PINE, FLORA_BEECH}},
		  FLORA_BERRY,
		  {{200, 200}}, {200, 200}, {{200, 200}}}},
		// rhine_marshlands.js: beech and oak forests; no cliffs or hills, so they reuse tMud and tGrassB.
		{"rhine_marshlands",
		 {"rhine_marshlands", {"temp_grass", "temp_grass", "temp_grass_d"}, {"temp_plants_bog"},
		  {"temp_grass_plants", "temp_plants_bog", "temp_grass_d", "temp_mud_a"}, {"temp_mud_a"}, {"temp_plants_bog"},
		  {"temp_mud_a"}, "temp_road", "temp_road_overgrown", "temp_grass_plants", "temp_plants_bog", "temp_mud_a"},
		 {"rhine_marshlands", {{FLORA_BEECH, FLORA_BEECH}}, {FLORA_OAK, FLORA_BEECH}, {{FLORA_OAK, FLORA_OAK}},
		  FLORA_BERRY,
		  {{200, 200}}, {200, 200}, {{200, 200}}}},
		// river_archipelago.js: toona, palm_tropic and palm_tropical forests on tForestFloor (toona the
		// tree1/2 forests, the palms the tree4/5 ones and straggler slots); hill reuses tCliff; tiers are
		// tGrassC, tGrassA, tGrassB and tGrass's tropic_plants_b, dirt tPlants. The shore bands are the
		// script's height bands (0-1 m, 1-2.8 m). Its grass and dirt patches keep off clPlayerTerritory
		// (a scaleByMapSize(15, 20) territory round each base), taken as 12 past the civic centre.
		{"river_archipelago",
		 {"river_archipelago", {"tropic_grass_c", "tropic_grass_c", "tropic_grass_plants", "tropic_plants"}, {"tropic_grass_plants"},
		  {"tropic_grass_c", "tropic_plants_c", "tropic_plants_c", "tropic_plants_b"}, {"tropic_cliff_a", "tropic_cliff_a_plants"},
		  {"tropic_cliff_a", "tropic_cliff_a_plants"}, {"tropic_plants"}, "tropic_citytile_a", "tropic_citytile_plants",
		  "tropic_beach_dry_plants", "tropic_beach_dry", "tropic_beach_wet"},
		 {"river_archipelago", {{FLORA_TREE_TOONA, FLORA_TREE_TOONA}}, {FLORA_TREE_PALM_TROPICAL}, {{FLORA_TROPIC_PALM, FLORA_TREE_PALM_TROPICAL}}, FLORA_BERRY,
		  {{200, 200}}, {200}, {{200, 200}}},
		 {{0.0f, 1.0f, {"tropic_beach_dry"}}, {1.0f, 2.8f, {"tropic_beach_dry_plants"}}},
		 {{AREA_HILL, {{"tropic_cliff_a", "tropic_cliff_a", "tropic_cliff_a", "tropic_cliff_a_plants"},
		               {"tropic_grass_c", "tropic_grass_c", "tropic_grass_c", "tropic_grass_plants", "tropic_plants", "tropic_plants_b"}}, {3}}},
		 {{{{"tropic_grass_c"}, {"tropic_plants_c"}, {"tropic_plants_c"}}, {2, 1}, {{3.0f, 6.0f}, {5.0f, 10.0f}, {8.0f, 21.0f}}, {20.0f, 80.0f}, 8, 0, 0, 12, 16},
		  {{{"tropic_plants"}}, {}, {{2.0f, 4.0f}, {3.0f, 7.0f}, {5.0f, 15.0f}}, {20.0f, 80.0f}, 8, 0, 0, 12, 16}}},
		// saharan_oases.js: date palm forests on tLush; no cliffs or hills, so they reuse tDirtCracks and tSandDunes.
		{"saharan_oases",
		 {"saharan_oases", {"desert_sand_dunes_100"}, {"desert_grass_a"},
		  {"desert_sand_smooth", "desert_sand_dunes_50", "desert_grass_a_sand", "desert_dirt_cracks"}, {"desert_dirt_cracks"},
		  {"desert_sand_dunes_50"}, {"desert_dirt_rough_2", "desert_dirt_rough"}, "desert_city_tile", "desert_city_tile_plaza",
		  "desert_grass_a_sand", "desert_shore_stones", "desert_shore_stones_wet"},
		 {"saharan_oases", {{FLORA_TREE_DATE_PALM, FLORA_TREE_CRETAN_DATE_PALM_SHORT}}, {FLORA_TREE_CRETAN_DATE_PALM_SHORT}, {{FLORA_TREE_CRETAN_DATE_PALM_SHORT, FLORA_TREE_DATE_PALM}}, FLORA_BERRY,
		  {{200, 100}}, {100}, {{100, 200}}}},
		// sahel.js: baobab only; no forest floor, cliffs or shore blend, so they reuse its grass and rocky dirt.
		{"sahel",
		 {"sahel", {"savanna_grass_a"}, {"savanna_grass_b", "savanna_shrubs_a"},
		  {"savanna_grass_b", "savanna_shrubs_a", "savanna_dirt_rocks_a", "savanna_dirt_b"}, {"savanna_dirt_rocks_c"},
		  {"savanna_dirt_rocks_b", "savanna_dirt_rocks_c"}, {"savanna_dirt_rocks_a", "savanna_dirt_b"}, "savanna_tile_a",
		  "savanna_tile_a", "savanna_grass_b", "savanna_riparian_bank", "savanna_riparian_wet"},
		 {"sahel", {{FLORA_BAOBAB, FLORA_BAOBAB}}, {FLORA_BAOBAB}, {{FLORA_BAOBAB, FLORA_BAOBAB}}, FLORA_BERRY,
		  {{600, 600}}, {600}, {{600, 600}}}},
		// sahel_watering_holes.js: baobab forests; no shore blend, so it reuses tGrassPatch.
		{"sahel_watering_holes",
		 {"sahel_watering_holes", {"savanna_grass_a"}, {"savanna_forestfloor_a"},
		  {"savanna_grass_b", "savanna_dirt_rocks_c", "savanna_dirt_rocks_a", "savanna_dirt_rocks_b"}, {"savanna_cliff_b"},
		  {"savanna_cliff_a"}, {"savanna_dirt_rocks_c", "savanna_dirt_rocks_a"}, "savanna_tile_a_red", "savanna_tile_a_red",
		  "savanna_grass_b", "savanna_riparian_bank", "savanna_riparian_wet"},
		 {"sahel_watering_holes", {{FLORA_BAOBAB, FLORA_BAOBAB}}, {FLORA_BAOBAB}, {{FLORA_BAOBAB, FLORA_BAOBAB}}, FLORA_BERRY,
		  {{600, 600}}, {600}, {{600, 600}}}},
		// scythian_rivulet.js: dead tree and dead oak forests, dead tree forests; no dirt or shore blend, so they reuse
		// tTier3Terrain and tTier1Terrain.
		{"scythian_rivulet",
		 {"scythian_rivulet", {"alpine_snow_a"}, {"alpine_forrestfloor_snow", "polar_snow_rocks"}, {"snow rough", "snow_01", "snow rocks"},
		  {"alpine_cliff_a", "alpine_cliff_b"}, {"alpine_snow_glacial"}, {"snow rocks"}, "new_alpine_citytile", "alpine_snow_rocky",
		  "snow rough", "alpine_shore_rocks_icy", "polar_ice_b"},
		 {"scythian_rivulet", {{FLORA_DEAD, FLORA_TREE_OAK_DEAD}}, {FLORA_DEAD, FLORA_TREE_OAK_DEAD, FLORA_PINE, FLORA_TREE_BUSH_BADLANDS},
		  {{FLORA_DEAD, FLORA_DEAD}}, FLORA_BERRY,
		  {{200, 200}}, {200, 200, 200, 100}, {{200, 200}}},
		 {},
		 {{AREA_HILL, {{"alpine_cliff_a", "alpine_cliff_b"}, {"alpine_cliff_a", "alpine_cliff_b"}, {"alpine_snow_glacial"}}, {1, 2}},
		  {AREA_MOUNTAIN, {{"alpine_cliff_a", "alpine_cliff_b"}}, {}}},
		 {{{{"alpine_snow_a", "snow rough"}, {"snow rough", "snow_01"}, {"snow_01", "snow rocks"}}, {1, 1}, {{3.0f, 6.0f}, {5.0f, 10.0f}, {8.0f, 21.0f}}, {25.0f, 55.0f}, 5, 2, 4, 12, 5}}},
		// syria.js: date palm forests and tamarix forests; no water, so the shore slots reuse its grass.
		{"syria",
		 {"syria", {"desert_dirt_rocks_1", "desert_dirt_cracks"}, {"forestfloor_dirty", "desert_forestfloor_palms"},
		  {"desert_grass_a_sand", "desert_grass_a", "medit_dirt_dry", "desert_dirt_rough"},
		  {"desert_cliff_persia_1", "desert_cliff_persia_2"}, {"desert_dirt_rocks_1", "desert_dirt_rocks_2"},
		  {"desert_dirt_rough", "desert_dirt_rough_2"}, "desert_shore_stones", "desert_grass_a_stones", "desert_grass_a",
		  "desert_grass_a_sand", "desert_grass_a_sand"},
		 {"syria", {{FLORA_TREE_DATE_PALM, FLORA_TREE_DATE_PALM}}, {FLORA_TREE_DATE_PALM, FLORA_TREE_TAMARIX, FLORA_ALEPPO_PINE}, {{FLORA_TREE_TAMARIX, FLORA_TREE_TAMARIX}},
		  FLORA_BERRY,
		  {{200, 200}}, {200, 200, 200}, {{200, 200}}},
		 {},
		 {{AREA_GRASS, {{"desert_grass_a_sand"}, {"desert_grass_a"}}, {3}},
		  {AREA_HILL, {{"desert_cliff_persia_1", "desert_cliff_persia_2"}, {"desert_dirt_rocks_1", "desert_dirt_rocks_2", "desert_dirt_rocks_3"}}, {2}}},
		 {{{{"medit_dirt_dry"}, {"desert_dirt_rough", "desert_dirt_rough_2"}}, {1}, {{3.0f, 6.0f}, {5.0f, 10.0f}, {8.0f, 21.0f}}, {50.0f, 90.0f}, -1, 0, 0, 8, 1},
		  {{{"medit_dirt_dry"}, {"desert_dirt_rough", "desert_dirt_rough_2"}}, {1}, {{6.0f, 30.0f}, {10.0f, 50.0f}, {16.0f, 70.0f}}, {30.0f, 90.0f}, -1, 0, 0, 8, 1}}},
		// the_nile.js: date and short Cretan date palms only; no cliffs or hills, so they reuse its cracked dirt and
		// half sand; the river and ponds are wet sand. Its bank strips go in an area (synth_the_nile) painted dry,
		// half-lush, then lush from 0.09 of the map out (widths of a 256-tile map).
		{"the_nile",
		 {"the_nile", {"desert_sand_dunes_100"}, {"desert_forestfloor_palms"},
		  {"desert_dirt_rough_2", "desert_sand_dunes_50", "desert_dirt_rough", "desert_sand_smooth"}, {"desert_dirt_cracks"},
		  {"desert_sand_dunes_50"}, {"desert_dirt_rough", "desert_dirt_cracks"}, "desert_city_tile", "desert_city_tile_plaza",
		  "desert_grass_a_sand", "desert_sand_wet", "desert_sand_wet"},
		 {"the_nile", {{FLORA_TREE_DATE_PALM, FLORA_TREE_CRETAN_DATE_PALM_SHORT}}, {FLORA_TREE_DATE_PALM, FLORA_TREE_CRETAN_DATE_PALM_SHORT}, {{FLORA_TREE_DATE_PALM, FLORA_TREE_CRETAN_DATE_PALM_SHORT}}, FLORA_BERRY,
		  {{200, 100}}, {200, 100}, {{200, 100}}},
		 {{-1e9f, 0.0f, {"desert_sand_wet"}, true}},
		 {{AREA_MARSH, {{"desert_plants_b"}, {"desert_grass_a_sand"}, {"desert_grass_a"}}, {8, 5}}}},
		// volcanic_lands.js: dead trees only and no fruit bush; no hills, dirt or water, so hill reuses tCliff and dirt
		// and the shore slots its ash rocks.
		{"volcanic_lands",
		 {"volcanic_lands", {"cliff volcanic light", "ocean_rock_a", "ocean_rock_b"}, {"ocean_rock_b", "ocean_rock_a"},
		  {"cliff volcanic light", "ocean_rock_a", "ocean_rock_b"}, {"cliff volcanic coarse", "cave_walls"},
		  {"cliff volcanic coarse", "cave_walls"}, {"ocean_rock_b"}, "road1", "road1", "ocean_rock_a", "ocean_rock_a",
		  "ocean_rock_b"},
		 {"volcanic_lands", {{FLORA_DEAD, FLORA_DEAD}}, {FLORA_DEAD}, {{FLORA_DEAD, FLORA_DEAD}},
		  FLORA_DEFAULT,
		  {{200, 200}}, {200}, {{200, 200}}},
		 {},
		 {{AREA_MOUNTAIN, {{"cliff volcanic coarse", "cave_walls"}}, {}},
		  {AREA_VOLCANO, {{"cliff volcanic coarse", "cave_walls"}, {"LavaTest05"}, {"LavaTest04"}, {"LavaTest03"}}, {1, 1, 1}},
		  {AREA_HILL, {{"cliff volcanic coarse", "cave_walls"}, {"cliff volcanic light", "ocean_rock_a", "ocean_rock_b"}}, {2}}}},
	};
	for (const auto &s : kScripts)
		if (std::strcmp(map_id, s.map_id) == 0) return &s;
	return nullptr;
}

} // namespace rtso
