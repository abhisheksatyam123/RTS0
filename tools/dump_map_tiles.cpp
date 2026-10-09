// Dumps a generated random map for tools/fetch_0ad_previews.py, which draws the
// preview of each RTS0 random map that has no 0 A.D. script (and so no 0 A.D.
// preview image): the per-tile terrain names, the height and water line per
// tile and every tree and mine, as JSON on stdout.
//
//   g++ -std=c++20 -O2 -Isrc tools/dump_map_tiles.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp -lpthread -o /tmp/dump_map_tiles
//   /tmp/dump_map_tiles random_continental [size] [seed]
#include "simulation/simulation.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace rtso;

int main(int argc, char **argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: %s <map id> [size] [seed]\n", argv[0]);
		return 2;
	}
	const std::string id = argv[1];
	const int size = argc > 2 ? std::atoi(argv[2]) : 256;
	const uint32_t seed = argc > 3 ? (uint32_t)std::strtoul(argv[3], nullptr, 10) : 1u;
	int type = -1;
	for (int t = 0; t < MAP_TYPE_COUNT; ++t) {
		const std::string tid = map_type_info((uint8_t)t).id;
		if (tid == id || "random_" + tid == id) { type = t; break; }
	}
	if (type < 0) {
		std::fprintf(stderr, "unknown map %s\n", id.c_str());
		return 1;
	}
	Simulation sim;
	sim.generate_random_map((MapType)type, seed, 4, size * 0.5f);
	const auto &t = sim.terrain_tiles();
	if (t.empty()) return 1;
	const float half = sim.map_half(), cell = 2.0f * half / (float)t.res;
	std::printf("{\"res\":%d,\"water\":%.3f,\"names\":[", t.res, sim.water_height());
	for (size_t i = 0; i < t.names.size(); ++i) std::printf("%s\"%s\"", i ? "," : "", t.names[i].c_str());
	std::printf("],\"tiles\":[");
	for (size_t i = 0; i < t.tiles.size(); ++i) std::printf("%s%d", i ? "," : "", t.tiles[i]);
	std::printf("],\"heights\":[");
	for (int z = 0; z < t.res; ++z)
		for (int x = 0; x < t.res; ++x)
			std::printf("%s%.2f", x || z ? "," : "",
					sim.get_terrain_height(-half + ((float)x + 0.5f) * cell, -half + ((float)z + 0.5f) * cell));
	std::printf("],\"half\":%.2f,\"resources\":[", half);
	const auto rv = sim.resources();
	bool first = true;
	for (size_t i = 0; i < rv.count; ++i) {
		if (!rv.alive[i] || rv.subtype[i] != RES_SUB_NONE) continue;
		std::printf("%s[%d,%.1f,%.1f]", first ? "" : ",", rv.type[i], rv.x[i], rv.z[i]);
		first = false;
	}
	std::printf("]}\n");
	return 0;
}
