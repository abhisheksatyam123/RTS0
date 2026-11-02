// RTS0 headless simulation-tick benchmark (no SCons, no Godot).
//
// Times Simulation::tick(FIXED_STEP) over three fixed, deterministic battles
// (small ~150 units, medium ~600, large ~1500), each with two teams of mixed
// unit types, villagers gathering next to their team's buildings, and soldiers
// attack-moving into the opposing army, so combat, separation, pathfinding,
// gather, vision/LOS and Petra AI all run. The terrain is a fixed 64x64 hill
// heightmap so LOS rays do real height lookups. Petra AI is unconditional
// inside tick() (there is no off switch), so every scenario runs it; team 1
// is Petra-controlled.
//
// Build (one line, -O2):
//   g++ -std=c++20 -O2 -Isrc tools/bench_sim.cpp src/simulation/simulation.cpp src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp src/network/net_host.cpp -o "$OUT/bench_sim"
//   (add -g for callgrind symbols; $OUT is any scratch dir)
//
// Run:
//   "$OUT/bench_sim" [--scenario small|medium|large|all] [--ticks N] [--serial] [--trace]
//     --ticks N     ticks per scenario (default 600 = 30 s of sim time)
//     --serial      do not start the JobSystem pool (the lockstep harness mode);
//                   default starts it like the game does (hw-1 workers)
//     --trace       also fold compute_sim_hash() and both FoW grids after every tick into a chain
//                   hash (costs extra time outside the timed region; for proofs)
//
// Output: per scenario mean/p95/max ms per tick, total ms, ticks actually run
// (a scenario stops early if the match ends), and three hashes:
//   sim_hash   TurnManager::compute_sim_hash() of the final state
//   state_fnv  FNV-1a 64 over the bytes of Simulation::save_state() (full state)
//   trace      chain of compute_sim_hash() + both FoW grids after every tick (--trace only)
// Identical hashes before and after a change prove the results are unchanged.
#include "core/job_system.h"
#include "network/turn_manager.h"
#include "simulation/simulation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace rtso;

namespace {

// Bench-local RNG, separate from the sim's SimRng so the scenario layout can
// never perturb sim state.
struct BenchRng {
	uint32_t state;
	explicit BenchRng(uint32_t seed) : state(seed ? seed : 1u) {}
	uint32_t next() { state = state * 1664525u + 1013904223u; return state; }
	float unit() { return (float)(next() & 0xFFFFFF) / 16777216.0f; }
};

uint64_t fnv1a64_fold(uint64_t h, const unsigned char *p, size_t n) {
	for (size_t i = 0; i < n; ++i) {
		h ^= p[i];
		h *= 1099511628211ull;
	}
	return h;
}

uint64_t fnv1a64(const unsigned char *p, size_t n) {
	return fnv1a64_fold(1469598103934665603ull, p, n);
}

struct Scenario {
	const char *name;
	int total_units; // both teams together
	uint32_t seed;
};

const Scenario kScenarios[] = {
	{"small", 150, 1001},
	{"medium", 600, 2002},
	{"large", 1500, 3003},
};

// Units are grouped per team so re-issued orders can address them without
// rescanning the whole SoA.
struct World {
	std::vector<uint32_t> soldiers[2];
	std::vector<uint32_t> villagers[2];
	std::vector<uint32_t> resources[2];
};

// Team 0 spawns on the west (x < 0), team 1 on the east, mirrored.
void build_world(Simulation &s, const Scenario &sc, World &w) {
	s.set_sim_seed(sc.seed);
	s.reset();
	s.set_petra_difficulty(3);
	s.set_petra_behavior(2);
	s.set_victory_condition(VICTORY_ENDLESS); // never end on conquest mid-bench
	s.set_starting_resources(900, 900, 500, 500);
	s.set_team_civ(0, CIV_ATHENIANS);
	s.set_team_civ(1, CIV_SPARTANS);

	// Deterministic 64x64 hill field (the game's default heightmap_res), so the
	// vision / LOS paths do real height lookups and rays get blocked by hills.
	const int res = 64;
	std::vector<float> heights((size_t)res * res);
	for (int z = 0; z < res; ++z) {
		for (int x = 0; x < res; ++x) {
			const float fx = (float)x / (float)(res - 1) * 2.0f - 1.0f;
			const float fz = (float)z / (float)(res - 1) * 2.0f - 1.0f;
			heights[(size_t)z * res + x] =
				3.0f * std::sin(fx * 5.0f) * std::cos(fz * 4.0f) +
				1.5f * std::sin((fx + fz) * 11.0f) + 0.8f * fx;
		}
	}
	s.set_terrain_data(heights, res, -1.0f);

	BenchRng rng(sc.seed ^ 0x9E3779B9u);
	const int per_team = sc.total_units / 2;
	const int villagers_n = per_team / 4;
	const int soldiers_n = per_team - villagers_n;

	for (uint8_t t = 0; t < 2; ++t) {
		const float sg = (t == 0) ? -1.0f : 1.0f;

		// Base: Town Center, barracks, storehouse, farmstead (behind the army).
		s.spawn_building_world(sg * 58.0f, 0.0f, BLD_TOWN_CENTER, t, 1.0f);
		s.spawn_building_world(sg * 54.0f, -12.0f, BLD_BARRACKS, t, 1.0f);
		s.spawn_building_world(sg * 50.0f, -22.0f, BLD_STOREHOUSE, t, 1.0f);
		s.spawn_building_world(sg * 50.0f, 22.0f, BLD_FARMSTEAD, t, 1.0f);

		// Resource nodes next to the base (large stock so nothing runs dry).
		w.resources[t].clear();
		w.resources[t].push_back(s.spawn_resource_world(sg * 60.0f, -26.0f, RES_WOOD, 40000));
		w.resources[t].push_back(s.spawn_resource_world(sg * 60.0f, 26.0f, RES_FOOD, 40000));
		w.resources[t].push_back(s.spawn_resource_world(sg * 46.0f, 32.0f, RES_GOLD, 40000));

		// Soldiers: a dense block whose front row sits at x = sg * 10 and whose
		// depth grows away from the centre; mixed types.
		w.soldiers[t].clear();
		const int cols = std::min(29, std::max(4, (int)std::ceil(std::sqrt((float)soldiers_n * 1.5f))));
		const float sp = 1.7f;
		for (int i = 0; i < soldiers_n; ++i) {
			const int row = i / cols;
			const int col = i % cols;
			const float x = sg * (10.0f + (float)row * sp);
			const float z = ((float)col - (cols - 1) * 0.5f) * sp;
			UnitType ut;
			switch (i % 20) {
				case 0: case 1: case 2: case 3: case 4: case 5: case 6:
					ut = UNIT_SWORDSMAN; break;
				case 7: case 8: case 9: case 10: case 11:
					ut = UNIT_SPEARMAN; break;
				case 12: case 13: case 14: case 15: case 16:
					ut = UNIT_ARCHER; break;
				default:
					ut = UNIT_CAVALRY; break;
			}
			w.soldiers[t].push_back(s.spawn_unit(x, z, t, ut, 0.0f));
		}

		// Villagers: spread around the storehouse / farmstead.
		w.villagers[t].clear();
		for (int i = 0; i < villagers_n; ++i) {
			const float jx = (rng.unit() - 0.5f) * 6.0f;
			const float jz = (rng.unit() - 0.5f) * 24.0f;
			w.villagers[t].push_back(s.spawn_unit(sg * 50.0f + jx, jz, t, UNIT_VILLAGER, 0.0f));
		}
	}

	// Attack-move each army into the other (team 0 marches east, team 1 west),
	// and send each villager to one of its team's resource nodes.
	for (uint8_t t = 0; t < 2; ++t) {
		const float tx = (t == 0) ? 24.0f : -24.0f;
		s.order_attack_move(w.soldiers[t].data(), w.soldiers[t].size(), tx, 0.0f);
		for (size_t i = 0; i < w.villagers[t].size(); ++i) {
			uint32_t rid = w.resources[t][i % w.resources[t].size()];
			s.order_gather(&w.villagers[t][i], 1, rid);
		}
	}
}

// Every 100 ticks, re-issue orders to units that have gone idle, so the battle
// keeps running for the whole bench window. Decided from sim state only.
void reissue_idle(Simulation &s, const World &w) {
	auto v = s.units();
	for (uint8_t t = 0; t < 2; ++t) {
		const float tx = (t == 0) ? 24.0f : -24.0f;
		std::vector<uint32_t> idle_soldiers;
		for (size_t i = 0; i < v.count; ++i) {
			if (!v.alive[i] || v.team[i] != t || v.action_state[i] != ACTION_IDLE) continue;
			if (v.unit_type[i] == UNIT_VILLAGER) continue;
			idle_soldiers.push_back(v.id[i]);
		}
		if (!idle_soldiers.empty())
			s.order_attack_move(idle_soldiers.data(), idle_soldiers.size(), tx, 0.0f);

		std::vector<uint32_t> idle_villagers;
		for (size_t i = 0; i < v.count; ++i) {
			if (!v.alive[i] || v.team[i] != t || v.action_state[i] != ACTION_IDLE) continue;
			if (v.unit_type[i] != UNIT_VILLAGER) continue;
			idle_villagers.push_back(v.id[i]);
		}
		for (size_t i = 0; i < idle_villagers.size(); ++i)
			s.order_gather(&idle_villagers[i], 1, w.resources[t][i % w.resources[t].size()]);
	}
}

uint64_t state_hash_via_save(const Simulation &s, const std::string &tag) {
	std::filesystem::path p = std::filesystem::temp_directory_path() / ("bench_sim_" + tag + ".state");
	if (!s.save_state(p.string().c_str())) return 0;
	std::ifstream in(p, std::ios::binary);
	std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	in.close();
	std::error_code ec;
	std::filesystem::remove(p, ec);
	return fnv1a64(bytes.data(), bytes.size());
}

struct Result {
	const char *name = "";
	size_t units_start = 0, units_end = 0;
	size_t ticks_run = 0;
	bool game_over = false;
	double total_ms = 0, mean_ms = 0, p95_ms = 0, max_ms = 0;
	uint32_t sim_hash = 0;
	uint64_t state_fnv = 0;
	uint64_t trace = 0;
	bool traced = false;
};

Result run_scenario(const Scenario &sc, int ticks, bool trace) {
	Result r;
	r.name = sc.name;
	auto sim = std::make_unique<Simulation>();
	World w;
	build_world(*sim, sc, w);
	r.units_start = sim->units().count;

	TurnManager tm;
	std::vector<double> us;
	us.reserve((size_t)ticks);
	uint64_t chain = 1469598103934665603ull;
	r.traced = trace;

	for (int tk = 0; tk < ticks; ++tk) {
		if (tk > 0 && tk % 100 == 0) reissue_idle(*sim, w);

		auto t0 = std::chrono::steady_clock::now();
		sim->tick(Simulation::FIXED_STEP);
		auto t1 = std::chrono::steady_clock::now();
		us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
		r.ticks_run = (size_t)tk + 1;

		if (trace) {
			uint64_t hv = tm.compute_sim_hash(*sim);
			chain = (chain ^ hv) * 1099511628211ull;
			// compute_sim_hash() does not cover the FoW grid: fold it in per tick.
			chain = fnv1a64_fold(chain, sim->fow_grid(), (size_t)sim->fow_res() * (size_t)sim->fow_res());
		}
		if (sim->is_game_over()) {
			r.game_over = true;
			break;
		}
	}

	double total = 0;
	for (double x : us) total += x;
	std::vector<double> sorted = us;
	std::sort(sorted.begin(), sorted.end());
	const size_t n = sorted.size();
	r.total_ms = total / 1000.0;
	r.mean_ms = n ? (total / (double)n) / 1000.0 : 0.0;
	r.p95_ms = n ? sorted[std::min(n - 1, (size_t)std::ceil(0.95 * (double)n) - 1)] / 1000.0 : 0.0;
	r.max_ms = n ? sorted[n - 1] / 1000.0 : 0.0;
	r.units_end = sim->units().count;
	r.sim_hash = tm.compute_sim_hash(*sim);
	r.state_fnv = state_hash_via_save(*sim, sc.name);
	r.trace = chain;
	return r;
}

void usage(const char *argv0) {
	std::fprintf(stderr,
		"usage: %s [--scenario small|medium|large|all] [--ticks N] [--serial] [--trace]\n",
		argv0);
}

} // namespace

int main(int argc, char **argv) {
	const char *which = "all";
	int ticks = 600;
	bool serial = false;
	bool trace = false;
	Scenario custom{}; // used when --scenario is a bare number
	bool have_custom = false;
	int workers = -1;
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
			which = argv[++i];
			char *end = nullptr;
			const long n = std::strtol(which, &end, 10);
			if (end && *end == 0 && n >= 2 && n <= 1000000) {
				custom = {"custom", (int)n, 4004u};
				have_custom = true;
			}
		} else if (std::strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) {
			ticks = std::atoi(argv[++i]);
		} else if (std::strcmp(argv[i], "--workers") == 0 && i + 1 < argc) {
			workers = std::atoi(argv[++i]);
		} else if (std::strcmp(argv[i], "--serial") == 0) {
			serial = true;
		} else if (std::strcmp(argv[i], "--trace") == 0) {
			trace = true;
		} else {
			usage(argv[0]);
			return 2;
		}
	}
	if (ticks <= 0) {
		usage(argv[0]);
		return 2;
	}

	if (!serial) {
		if (workers >= 0) JobSystem::instance().start((unsigned)workers);
		else JobSystem::instance().start();
	}
	std::printf("bench_sim: ticks=%d fixed_step=%.4f job_workers=%u mode=%s trace=%s\n",
		ticks, (double)Simulation::FIXED_STEP, JobSystem::instance().worker_count(),
		serial ? "serial" : "pool", trace ? "on" : "off");

	std::printf("%-8s %-13s %6s %9s %9s %9s %10s %4s %-12s %-18s %-18s\n",
		"scenario", "units", "ticks", "mean_ms", "p95_ms", "max_ms", "total_ms", "over",
		"sim_hash", "state_fnv64", "trace");
	int ran = 0;
	for (const Scenario &sc : kScenarios) {
		if (have_custom) break;
		if (std::strcmp(which, "all") != 0 && std::strcmp(which, sc.name) != 0) continue;
		Result r = run_scenario(sc, ticks, trace);
		++ran;
		char units[32];
		std::snprintf(units, sizeof units, "%zu->%zu", r.units_start, r.units_end);
		char trace_s[24] = "-";
		if (r.traced) std::snprintf(trace_s, sizeof trace_s, "0x%016llx", (unsigned long long)r.trace);
		std::printf("%-8s %-13s %6zu %9.4f %9.4f %9.4f %10.2f %4s 0x%08X   0x%016llx %s\n",
			r.name, units, r.ticks_run, r.mean_ms, r.p95_ms, r.max_ms, r.total_ms,
			r.game_over ? "yes" : "no", r.sim_hash,
			(unsigned long long)r.state_fnv, trace_s);
		std::fflush(stdout);
	}
	if (have_custom) {
		Result r = run_scenario(custom, ticks, trace);
		++ran;
		char units[32];
		std::snprintf(units, sizeof units, "%zu->%zu", r.units_start, r.units_end);
		std::printf("%-8s %-13s %6zu %9.4f %9.4f %9.4f %10.2f %4s 0x%08X   0x%016llx %s\n",
			r.name, units, r.ticks_run, r.mean_ms, r.p95_ms, r.max_ms, r.total_ms,
			r.game_over ? "yes" : "no", r.sim_hash,
			(unsigned long long)r.state_fnv, "-");
		std::fflush(stdout);
	}
	if (ran == 0) {
		usage(argv[0]);
		return 2;
	}
	JobSystem::instance().stop();
	return 0;
}
