// Map generation hashes: one line per random map type and seed, "map seed players size hash
// terrain [hash after --ticks]". Two builds of the game must print the same lines, whatever
// compiler, CPU or C library built them; tools/test_cross_compiler.py diffs a g++ build
// against a clang build with FMA (the arm64 stand-in).
// Build (one line):
//   g++ -std=c++20 -O2 -ffp-contract=off -Isrc tools/map_hashes.cpp src/simulation/simulation.cpp
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp
//       src/network/net_host.cpp -lpthread -o /tmp/map_hashes
//   /tmp/map_hashes [--seeds N] [--size S] [--map M] [--ticks T]
#include "simulation/simulation.h"
#include "network/turn_manager.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace rtso;

int main(int argc, char **argv) {
	int seeds = 2, ticks = 0, only = -1;
	float size = 256.0f;
	for (int i = 1; i + 1 < argc; ++i) {
		if (!strcmp(argv[i], "--seeds")) seeds = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--ticks")) ticks = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--size")) size = (float)atof(argv[++i]);
		else if (!strcmp(argv[i], "--map")) only = atoi(argv[++i]);
	}
	TurnManager tm;
	for (int m = 0; m < MAP_TYPE_COUNT; ++m) {
		if (only >= 0 && m != only) continue;
		for (int k = 0; k < seeds; ++k) {
			const uint32_t seed = 1000u + 7919u * (uint32_t)k;
			const int players = 2 + (k % 3) * 2;
			Simulation s;
			s.reset();
			s.set_sim_seed(seed);
			s.set_waves_enabled(false);
			for (int p = 0; p < players; ++p) s.set_team_civ((uint8_t)p, (uint8_t)((p + k) % 8));
			s.generate_random_map((MapType)m, seed, players, size * 0.5f);
			const uint32_t h0 = tm.compute_sim_hash(s);
			uint64_t th = 1469598103934665603ull; // FNV-1a over the height field's bits
			for (float v : s.terrain_heights()) {
				uint32_t b;
				std::memcpy(&b, &v, 4);
				th = (th ^ b) * 1099511628211ull;
			}
			for (int t = 0; t < ticks; ++t) s.tick(Simulation::FIXED_STEP);
			const uint32_t h1 = ticks ? tm.compute_sim_hash(s) : 0;
			printf("%d %u %d %.0f %08x %016llx %08x\n", m, seed, players, size, h0, (unsigned long long)th, h1);
			fflush(stdout);
		}
	}
	return 0;
}
