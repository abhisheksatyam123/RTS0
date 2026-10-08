// bench_map_scale.cpp - how RTS0's map-size-sensitive systems scale.
//
// Generates a random map at each 0 A.D. map size, puts 240 player units on it,
// sends them in groups of 20 to random points across the whole map every 6 s
// (long paths that load the pathfinder), and runs 100 s of simulation. Prints
// per-call cost of fog of war, pathfinding, territory and the Petra threat map
// plus the average tick time.
//
//   g++ -std=c++20 -O2 -Isrc tools/bench_map_scale.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp -o /tmp/bench_map_scale
//   /tmp/bench_map_scale [map_type] [seconds] [only_size]
#include "simulation/simulation.h"
#include "simulation/rms_generator.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace rtso;

int main(int argc, char **argv) {
	const int map_type = argc > 1 ? std::atoi(argv[1]) : MAP_RANDOM_CONTINENTAL;
	const float seconds = argc > 2 ? (float)std::atof(argv[2]) : 100.0f;
	const int only_size = argc > 3 ? std::atoi(argv[3]) : 0;
	// 0 A.D. map sizes in tiles; one RTS0 world unit is one 0 A.D. tile.
	const int sizes[] = {128, 192, 256, 320, 384, 448, 512};

	std::printf("map=%s seconds=%.0f\n", map_type_info((uint8_t)map_type).name, seconds);
	std::printf("%5s %5s %9s %9s %10s %8s %10s %9s %9s %6s\n", "size", "grid", "tick_ms", "fow_ms",
	            "path_us", "paths", "terr_ms", "threat_ms", "gen_ms", "units");
	for (int size : sizes) {
		if (only_size && size != only_size) continue;
		Simulation sim;
		auto g0 = std::chrono::high_resolution_clock::now();
		sim.generate_random_map((MapType)map_type, 42, 2, size * 0.5f);
		double gen_ms = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - g0).count();
		const auto base = sim.map_scale_stats();

		// Player army around the team-0 town centre.
		float bx = 0, bz = 0;
		for (const auto &b : sim.buildings())
			if (b.team == 0 && b.type == BLD_TOWN_CENTER) { bx = b.x; bz = b.z; break; }
		std::vector<uint32_t> ids;
		for (int i = 0; i < 240; ++i) {
			float x = bx + (float)(i % 16) * 1.5f - 12.0f;
			float z = bz + (float)(i / 16) * 1.5f + 10.0f;
			uint32_t id = sim.spawn_unit(x, z, 0, UNIT_SPEARMAN);
			if (id) ids.push_back(id);
		}

		RmsRng rng(7);
		const float half = sim.map_half();
		const int ticks = (int)(seconds / Simulation::FIXED_STEP);
		const auto s0 = sim.stats();
		int ran = 0;
		for (int t = 0; t < ticks; ++t) {
			if (t % 120 == 0) {
				for (size_t g = 0; g + 20 <= ids.size(); g += 20) {
					float tx = rng.range_float(-half * 0.9f, half * 0.9f);
					float tz = rng.range_float(-half * 0.9f, half * 0.9f);
					sim.cmd_move(&ids[g], 20, tx, tz);
				}
			}
			sim.tick(Simulation::FIXED_STEP);
			++ran;
		}
		const auto s1 = sim.stats();
		const auto &st = sim.map_scale_stats();
		auto per = [](uint64_t us, uint64_t calls) { return calls ? (double)us / (double)calls : 0.0; };
		const size_t ticked = s1.ticks - s0.ticks;
		std::printf("%5d %5d %9.3f %9.3f %10.1f %8llu %10.3f %9.3f %9.1f %6zu%s\n", size, sim.fow_res(),
		            ticked ? (double)(s1.tick_us - s0.tick_us) / 1000.0 / (double)ticked : 0.0,
		            per(st.fow_us - base.fow_us, st.fow_calls - base.fow_calls) / 1000.0,
		            per(st.path_us - base.path_us, st.path_calls - base.path_calls),
		            (unsigned long long)(st.path_calls - base.path_calls),
		            per(st.territory_us, st.territory_calls) / 1000.0,
		            per(st.threat_us - base.threat_us, st.threat_calls - base.threat_calls) / 1000.0,
		            gen_ms, sim.unit_count(), (int)ticked < ran ? " (match ended early)" : "");
	}
	return 0;
}
