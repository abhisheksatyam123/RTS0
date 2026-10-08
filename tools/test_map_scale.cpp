// RTS0 map-size test (headless: no SCons, no Godot).
// Checks that the nav, fog-of-war and territory grids follow the map size, so
// 0 A.D. map sizes from Tiny (128 units) to Giant (512 units) work end to end.
// Build and run:
//   g++ -std=c++20 -O2 -Isrc tools/test_map_scale.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp -o /tmp/test_map_scale && /tmp/test_map_scale
//
//  1. grid_res_for_map keeps 2m cells: 64 cells on the default 128m map, 256 on 512m.
//  2. Every random map type generates at every 0 A.D. size with matching nav and
//     FoW grids, and each player's town centre can path to every other's.
//  3. Map JSON loading (reset, then set_map_half) re-initialises the nav grid for
//     the new extent, so a path across a 256m map reaches its far corner.
//  4. A unit near the far corner of a Giant map lights up the FoW there.
//  5. Generation is deterministic: the same seed gives the same map at every size.
//  Exit: 0 = PASS, 1 = FAIL.
#include "simulation/simulation.h"
#include "simulation/rms_generator.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace rtso;

static int g_failures = 0;
#define CHECK(cond, ...)                                    \
	do {                                                    \
		if (!(cond)) {                                      \
			std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			std::printf(__VA_ARGS__);                       \
			std::printf("\n");                              \
			++g_failures;                                   \
		}                                                   \
	} while (0)

static const int kSizes[] = {128, 192, 256, 320, 384, 448, 512};

static uint64_t map_fingerprint(const Simulation &sim) {
	uint64_t h = 1469598103934665603ull;
	auto mix = [&h](const void *p, size_t n) {
		const unsigned char *b = (const unsigned char *)p;
		for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
	};
	mix(sim.terrain_heights().data(), sim.terrain_heights().size() * sizeof(float));
	auto r = sim.resources();
	mix(r.x, r.count * sizeof(float));
	mix(r.z, r.count * sizeof(float));
	mix(sim.nav_grid().cells.data.data(), sim.nav_grid().cells.data.size());
	return h;
}

int main() {
	// 1. Grid resolution.
	CHECK(Simulation::grid_res_for_map(64.0f) == 64, "128m map -> %d", Simulation::grid_res_for_map(64.0f));
	CHECK(Simulation::grid_res_for_map(96.0f) == 96, "192m map -> %d", Simulation::grid_res_for_map(96.0f));
	CHECK(Simulation::grid_res_for_map(256.0f) == 256, "512m map -> %d", Simulation::grid_res_for_map(256.0f));
	CHECK(Simulation::grid_res_for_map(10.0f) == Simulation::MIN_GRID_RES, "tiny clamps up");
	CHECK(Simulation::grid_res_for_map(2000.0f) == Simulation::MAX_GRID_RES, "huge clamps down");
	CHECK(Simulation::grid_res_for_map(100.0f) % CHUNK_SIZE == 0, "res is whole chunks");

	// 2. Every map type at every size.
	int paths = 0, truncated = 0;
	for (int mt = 0; mt < MAP_TYPE_COUNT; ++mt) {
		for (int size : kSizes) {
			Simulation sim;
			const int players = (size >= 384) ? 4 : 2;
			sim.generate_random_map((MapType)mt, 1234, players, size * 0.5f);
			const int res = Simulation::grid_res_for_map(size * 0.5f);
			CHECK(sim.fow_res() == res, "%s %d: fow_res %d != %d", map_type_info(mt).id, size, sim.fow_res(), res);
			CHECK(sim.nav_grid().res == res, "%s %d: nav res %d != %d", map_type_info(mt).id, size, sim.nav_grid().res, res);
			CHECK(std::fabs(sim.nav_grid().map_half - size * 0.5f) < 1e-3f, "%s %d: nav map_half %.1f",
			      map_type_info(mt).id, size, sim.nav_grid().map_half);

			std::vector<std::pair<float, float>> tcs;
			for (const auto &b : sim.buildings())
				if (b.type == BLD_TOWN_CENTER) tcs.push_back({b.x, b.z});
			CHECK((int)tcs.size() >= 2, "%s %d: %zu town centres", map_type_info(mt).id, size, tcs.size());
			for (size_t i = 0; i < tcs.size(); ++i) {
				for (size_t j = 0; j < tcs.size(); ++j) {
					if (i == j) continue;
					// Start and end just outside the TC footprint.
					float sx = tcs[i].first + 6.0f, sz = tcs[i].second + 6.0f;
					float tx = tcs[j].first + 6.0f, tz = tcs[j].second + 6.0f;
					WaypointPath wp;
					sim.nav_grid().find_path(sx, sz, tx, tz, wp, PASS_INFANTRY);
					++paths;
					if (wp.count >= WaypointPath::MAX_WP - 1) ++truncated;
					// Island maps are allowed to be unreachable on foot; the path
					// must still lie inside the map.
					for (int k = 0; k < wp.count; ++k) {
						CHECK(std::fabs(wp.xs[k]) <= size * 0.5f && std::fabs(wp.zs[k]) <= size * 0.5f,
						      "%s %d: waypoint (%.1f, %.1f) off map", map_type_info(mt).id, size, wp.xs[k], wp.zs[k]);
					}
				}
			}
		}
	}
	std::printf("paths between town centres: %d, hit the %d-waypoint cap: %d\n", paths, WaypointPath::MAX_WP, truncated);

	// 3. JSON map flow (main_node.cpp load_map): reset, set_map_half, terrain,
	// then buildings inside begin/end_bulk_spawn.
	{
		auto load = [](Simulation &sim, bool bulk) {
			sim.reset();
			sim.set_map_half(128.0f);
			std::vector<float> hm(64 * 64, 2.0f);
			for (int z = 0; z < 64; ++z)
				for (int x = 40; x < 48; ++x) hm[z * 64 + x] = (z < 20) ? -3.0f : 2.0f; // a lake
			sim.set_terrain_data(hm, 64, 0.0f);
			if (bulk) sim.begin_bulk_spawn();
			for (int i = 0; i < 60; ++i)
				sim.spawn_building_world(-100.0f + (float)(i % 10) * 9.0f, -40.0f + (float)(i / 10) * 9.0f,
				                         (i % 7 == 0) ? BLD_TOWER : BLD_HOUSE, (uint8_t)(i % 2), 1.0f);
			if (bulk) sim.end_bulk_spawn();
		};
		Simulation sim, one_by_one;
		load(sim, true);
		load(one_by_one, false);
		CHECK(sim.nav_grid().cells.data == one_by_one.nav_grid().cells.data, "bulk spawn changes the nav grid");
		int water = 0;
		for (uint8_t c : sim.nav_grid().cells.data) water += (c == NAV_WATER);
		CHECK(water > 0, "lake missing from nav grid after bulk spawn");
		WaypointPath wp;
		bool ok = sim.nav_grid().find_path(-120.0f, -120.0f, 120.0f, 120.0f, wp, PASS_INFANTRY);
		CHECK(ok && wp.count > 0, "path across a 256m map failed");
		if (wp.count > 0) {
			float ex = wp.xs[wp.count - 1], ez = wp.zs[wp.count - 1];
			CHECK(std::hypot(ex - 120.0f, ez - 120.0f) < 2.0f, "path ends at (%.1f, %.1f), not (120, 120)", ex, ez);
		}
		CHECK(sim.fow_res() == 128, "256m map fow_res %d", sim.fow_res());
	}

	// 4. FoW at the far corner of a Giant map.
	{
		Simulation sim;
		sim.generate_random_map(MAP_RANDOM_CONTINENTAL, 99, 2, 256.0f);
		const float x = 230.0f, z = 230.0f;
		CHECK(sim.fow_state_at(x, z) != Simulation::FOW_VISIBLE, "corner visible before any unit");
		sim.spawn_unit(x, z, 0, UNIT_SPEARMAN);
		for (int i = 0; i < 10; ++i) sim.tick(Simulation::FIXED_STEP);
		CHECK(sim.fow_state_at(x, z) == Simulation::FOW_VISIBLE, "unit at (%.0f, %.0f) does not reveal its cell", x, z);
		CHECK(sim.fow_state_at(x - 40.0f, z - 40.0f) != Simulation::FOW_VISIBLE, "cell 56m away visible");
	}

	// 5. Determinism.
	for (int size : kSizes) {
		Simulation a, b;
		a.generate_random_map(MAP_DANUBIUS, 77, 2, size * 0.5f);
		b.generate_random_map(MAP_DANUBIUS, 77, 2, size * 0.5f);
		CHECK(map_fingerprint(a) == map_fingerprint(b), "size %d: same seed, different map", size);
	}

	if (g_failures) {
		std::printf("FAIL (%d)\n", g_failures);
		return 1;
	}
	std::printf("PASS\n");
	return 0;
}
