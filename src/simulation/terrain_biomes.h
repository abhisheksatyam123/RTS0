#pragma once

#include <cstring>

namespace rtso {

// 0 A.D. random-map terrains per biome (maps/random/rmbiome/defaultbiome.json,
// generic/<biome>.json "Terrains" and generic/<biome>.js setupBiome_<biome>),
// by RTS0 biome name (MapTypeInfo::biome). A list holds up to four names, and a
// tile painted with a list takes one of them at random, as rmgen's
// TerrainPainter does with an array. Unused slots are nullptr.
struct TerrainBiome {
	const char *biome;      // RTS0 biome name
	const char *main[4];    // mainTerrain
	const char *forest[2];  // forestFloor1, forestFloor2
	const char *tier[4];    // tier1Terrain .. tier4Terrain
	const char *cliff[2];
	const char *hill[2];
	const char *dirt[2];
	const char *road, *road_wild, *shore_blend, *shore, *water;
};

inline const TerrainBiome &terrain_biome(const char *biome) {
	static const TerrainBiome kBiomes[] = {
		// generic/aegean (0 A.D.'s Mediterranean); RTS0's Mediterranean and Latium maps too.
		{"aegean", {"aegean_grass_02", "aegean_grass_dirt_01", "aegean_grass_01"}, {"aegean_forestfloor_01", "aegean_grass_01"},
		 {"aegean_grass_dirt_01", "aegean_grass_01", "aegean_grass_03", "aegean_grass_02"}, {"aegean_cliff_01", "aegean_cliff_02"},
		 {"aegean_mountain_01", "aegean_dirt_rocks_01"}, {"aegean_dirt_01", "aegean_rocks_grass_01"}, "aegean_paving_02",
		 "aegean_rocks_grass_01", "aegean_sand_01", "aegean_sand_01", "aegean_sand_02_wet"},
		{"alpine", {"steppe_grass_02"}, {"alpine_forestfloor_01", "alpine_forestfloor_01"},
		 {"steppe_grass_dirt_02", "alpine_forestfloor_02", "steppe_rocks_dirt_01", "aegean_grass_dirt_03"}, {"alpine_rock_02"},
		 {"alpine_rock_01", "alpine_rock_02"}, {"alpine_forestfloor_01"}, "alpine_paving_stones_02", "alpine_paving_stones_02",
		 "aegean_grass_dirt_03", "aegean_grass_dirt_03", "steppe_rocks_dirt_01"},
		// generic/arctic.
		{"polar", {"alpine_snow_01", "alpine_snow_02"}, {"alpine_forestfloor_02", "alpine_forestfloor_02"},
		 {"alpine_snow_01", "alpine_snow_02", "polar_snow_b", "alpine_snow_02"}, {"alpine_rock_01", "alpine_rock_02"},
		 {"alpine_rock_01_snow", "alpine_rock_02_snow"}, {"alpine_snow_01", "alpine_snow_02"}, "alpine_paving_stones_01",
		 "alpine_paving_stones_01", "alpine_rock_01_snow", "alpine_rocks_dirt_01", "alpine_rocks_dirt_01"},
		// generic/sahara; RTS0's oasis and desert maps.
		{"oasis", {"sahara_sand_01", "sahara_sand_02", "sahara_sand_05"}, {"sahara_forestfloor_01", "sahara_forestfloor_02"},
		 {"sahara_rocks_dirt_01", "sahara_rocks_dirt_02", "sahara_sand_03", "sahara_sand_04"}, {"sahara_rock_01"}, {"sahara_dirt_02"},
		 {"sahara_dirt_01", "sahara_dirt_02"}, "sahara_paving_stones_01", "sahara_paving_stones_01_sand_01", "sahara_sand_01_dunes",
		 "sahara_sand_02", "sahara_sand_04_wet"},
		// generic/temperate (setupBiome_temperate's first set).
		{"temperate", {"temperate_grass_04"}, {"temperate_forestfloor_01", "temperate_forestfloor_02"},
		 {"temperate_grass_dirt_02", "temperate_grass_03", "temperate_grass_04", "temperate_grass_01"},
		 {"temperate_cliff_01", "temperate_cliff_02"}, {"temperate_rocks_dirt_01", "temperate_grass_dirt_03"},
		 {"temperate_mud_01", "temperate_grass_mud_01"}, "temperate_paving_03", "temperate_paving_03", "temperate_grass_dirt_01",
		 "temperate_grass_mud_01", "temperate_mud_01"},
		{"steppe", {"steppe_grass_03"}, {"steppe_grass_dirt_01", "aegean_grass_dirt_03"},
		 {"steppe_grass_02", "steppe_grass_dirt_02", "steppe_grass_dirt_03", "steppe_grass_dirt_01"},
		 {"aegean_cliff_seaside_01", "steppe_rocks_dirt_01"}, {"steppe_rocks_dirt_01", "steppe_rocks_dirt_01"},
		 {"steppe_grass_dirt_01", "india_grass_dirt_02"}, "sahara_paving_stones_01", "sahara_paving_stones_01", "steppe_grass_03",
		 "india_grass_dirt_02", "steppe_grass_mud_01"},
		{"savanna", {"savanna_grass_01"}, {"sahara_dirt_cracks_01", "sahara_dirt_02"},
		 {"aegean_grass_01", "savanna_grass_03", "aegean_grass_01", "aegean_grass_02"}, {"sahara_rock_01"}, {"sahara_rock_01"},
		 {"aegean_grass_02"}, "sahara_paving_stones_02", "sahara_paving_stones_02", "steppe_grass_04", "steppe_grass_04", "india_sand_01"},
		{"india", {"india_grass_01", "india_grass_02", "india_grass_dirt_01", "india_grass_dirt_02"},
		 {"india_forestfloor_01", "india_forestfloor_02"}, {"india_grass_02", "india_mud_01", "india_grass_dirt_01", "india_grass_dirt_02"},
		 {"india_cliff_02", "india_cliff_03"}, {"india_dirt_01", "india_dirt_02"}, {"india_dirt_01", "india_dirt_02"}, "india_paving_02",
		 "india_paving_02", "india_grass_dirt_01", "india_sand_01", "india_mud_01"},
		// volcanic_lands.js.
		{"volcanic", {"cliff volcanic light", "ocean_rock_a", "ocean_rock_b"}, {"cliff volcanic light", "ocean_rock_a"},
		 {"cliff volcanic light", "ocean_rock_a", "ocean_rock_b", "ocean_rock_a"}, {"cliff volcanic coarse", "cave_walls"},
		 {"cliff volcanic coarse", "cave_walls"}, {"ocean_rock_b"}, "road1", "road1", "ocean_rock_a", "ocean_rock_a", "ocean_rock_b"},
	};
	static const struct { const char *from, *to; } kAliases[] = {
		{"mediterranean", "aegean"}, {"latium", "aegean"}, {"desert", "oasis"}, {"persia", "steppe"}, {"tropical", "india"}};
	for (const auto &a : kAliases)
		if (std::strcmp(biome, a.from) == 0) biome = a.to;
	for (const auto &b : kBiomes)
		if (std::strcmp(biome, b.biome) == 0) return b;
	return kBiomes[0];
}

} // namespace rtso
