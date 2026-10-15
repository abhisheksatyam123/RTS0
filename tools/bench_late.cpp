// RTS0 late-game load benchmark: the worst realistic match, measured per lockstep turn.
//
// Every slot is a seated Petra (default 8 players on a Giant 512 map, Hard, 0.28's
// default 300 population), playing Conquest from the start. It reports the cost of one
// 200 ms lockstep turn (4 sim ticks) in windows of --window minutes, with the units
// alive at the end of each window, so the late game (200+ units a player) shows up on
// its own rather than averaged into the quiet opening. The turn budget is 200 ms; a
// turn should cost a small fraction of it, leaving the client its frame time.
// Build (one line):
//   g++ -std=c++20 -O2 -DNDEBUG -Isrc tools/bench_late.cpp src/simulation/simulation.cpp
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp
//       src/network/net_host.cpp -lpthread -o /tmp/bench_late
//   /tmp/bench_late --players 8 --size 512 --minutes 40
// --threads N sizes the job pool (default: the game's, one worker per spare core; 0 = main
// thread only, the clearest profile).
// --slow MS lists each turn over MS milliseconds (default 50) on stderr, with the time it spent
// rebuilding territory, finding long paths, and on fog, LOS and Petra's threat map.
// --save-at M writes the state at minute M to --save PATH; --load PATH resumes from such
// a save, so a profiler can be pointed at the late game alone (perf record -- ... --load).
#include "simulation/simulation.h"
#include "simulation/petra_ai.h"
#include "network/turn_manager.h"
#include "core/job_system.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace rtso;

namespace {

double pct(std::vector<double> v, double p) {
	if (v.empty()) return 0.0;
	std::sort(v.begin(), v.end());
	size_t i = (size_t)(p * (double)(v.size() - 1) + 0.5);
	return v[std::min(i, v.size() - 1)];
}

int alive_units(const Simulation &s, int *per_player, int players) {
	auto u = s.units();
	int n = 0;
	for (int p = 0; p < players; ++p) per_player[p] = 0;
	for (size_t i = 0; i < u.count; ++i) {
		if (!u.alive[i]) continue;
		++n;
		if (u.team[i] < players) per_player[u.team[i]]++;
	}
	return n;
}

} // namespace

int main(int argc, char **argv) {
	int map = MAP_RANDOM_CONTINENTAL, players = 8, difficulty = PETRA_HARD, pop = 300;
	uint32_t seed = 42;
	float size = 512.0f;
	double minutes = 40.0, window = 5.0, save_at = -1.0;
	std::string save_path, load_path;
	double slow_ms = 50.0; // turns slower than this are listed with Simulation::map_scale_stats()' share
	int threads = -1; // -1: the game's pool (register_types.cpp JobSystem start()), 0: main thread only
	for (int i = 1; i < argc; ++i) {
		auto next = [&]() { return i + 1 < argc ? argv[++i] : (char *)"0"; };
		if (!strcmp(argv[i], "--map")) map = atoi(next());
		else if (!strcmp(argv[i], "--seed")) seed = (uint32_t)strtoul(next(), nullptr, 10);
		else if (!strcmp(argv[i], "--size")) size = (float)atof(next());
		else if (!strcmp(argv[i], "--players")) players = atoi(next());
		else if (!strcmp(argv[i], "--difficulty")) difficulty = atoi(next());
		else if (!strcmp(argv[i], "--pop")) pop = atoi(next());
		else if (!strcmp(argv[i], "--minutes")) minutes = atof(next());
		else if (!strcmp(argv[i], "--window")) window = atof(next());
		else if (!strcmp(argv[i], "--save-at")) save_at = atof(next());
		else if (!strcmp(argv[i], "--save")) save_path = next();
		else if (!strcmp(argv[i], "--load")) load_path = next();
		else if (!strcmp(argv[i], "--threads")) threads = atoi(next());
		else if (!strcmp(argv[i], "--slow")) slow_ms = atof(next());
	}
	players = std::clamp(players, 1, (int)MAX_PLAYERS);

	if (threads != 0) JobSystem::instance().start(threads < 0 ? 0u : (unsigned)threads);
	Simulation s;
	s.reset();
	if (!load_path.empty()) {
		if (!s.load_state(load_path.c_str())) {
			fprintf(stderr, "could not load %s\n", load_path.c_str());
			return 2;
		}
	} else {
		s.set_sim_seed(seed);
		s.set_waves_enabled(false);
		const uint8_t civs[] = {0, 1, 2, 3, 4, 5, 6, 7};
		for (int p = 0; p < players; ++p) s.set_team_civ((uint8_t)p, civs[p % 8]);
		s.generate_random_map((MapType)map, seed, players, size * 0.5f);
		s.set_victory_condition(VICTORY_CONQUEST);
		for (int p = 0; p < players; ++p) s.set_team_civ((uint8_t)p, civs[p % 8]);
		for (uint8_t p = 0; p < MAX_PLAYERS; ++p) s.set_player_active(p, p < players, p < players);
		for (int p = 0; p < players; ++p) s.set_petra_difficulty((uint8_t)p, (uint8_t)difficulty);
		s.set_pop_cap(pop);
	}

	const int turns = (int)(minutes * 60.0 / 0.2 + 0.5);
	const int per_window = std::max(1, (int)(window * 60.0 / 0.2 + 0.5));
	std::vector<double> all, win;
	all.reserve(turns);
	int per_player[MAX_PLAYERS] = {};
	printf("%-9s %8s %8s %8s %8s  %6s  %s\n", "minutes", "mean_ms", "p50_ms", "p95_ms", "max_ms", "units", "per player");
	auto wall0 = std::chrono::steady_clock::now();
	for (int t = 0; t < turns; ++t) {
		if (save_at >= 0.0 && !save_path.empty() && t == (int)(save_at * 60.0 / 0.2 + 0.5)) {
			fprintf(stderr, "%s %s\n", s.save_state(save_path.c_str()) ? "saved" : "could not save", save_path.c_str());
		}
		if (s.is_game_over() || s.is_victory()) break;
		const Simulation::MapScaleStats st0 = s.map_scale_stats();
		auto a = std::chrono::steady_clock::now();
		for (float left = 0.2f; left > 1e-4f; left -= Simulation::FIXED_STEP) s.tick(Simulation::FIXED_STEP);
		auto b = std::chrono::steady_clock::now();
		const double ms = std::chrono::duration<double, std::milli>(b - a).count();
		all.push_back(ms);
		win.push_back(ms);
		if (ms > slow_ms) {
			const Simulation::MapScaleStats &st = s.map_scale_stats();
			fprintf(stderr, "[slow] turn %d (%.2f min): %.1f ms; territory %llu calls %.1f ms, paths %llu calls %.1f ms, "
					"fog %.1f ms, los %.1f ms, threat %.1f ms\n", t, (t + 1) * 0.2 / 60.0, ms,
					(unsigned long long)(st.territory_calls - st0.territory_calls), (st.territory_us - st0.territory_us) / 1000.0,
					(unsigned long long)(st.path_calls - st0.path_calls), (st.path_us - st0.path_us) / 1000.0,
					(st.fow_us - st0.fow_us) / 1000.0, (st.los_us - st0.los_us) / 1000.0, (st.threat_us - st0.threat_us) / 1000.0);
		}
		if ((int)win.size() == per_window || t + 1 == turns) {
			double sum = 0.0;
			for (double v : win) sum += v;
			const int n = alive_units(s, per_player, players);
			char pp[256] = {};
			for (int p = 0, o = 0; p < players && o < 240; ++p) o += snprintf(pp + o, sizeof(pp) - o, "%d ", per_player[p]);
			printf("%4.0f-%-4.0f %8.2f %8.2f %8.2f %8.2f  %6d  %s\n", (t + 1 - (int)win.size()) * 0.2 / 60.0,
					(t + 1) * 0.2 / 60.0, sum / win.size(), pct(win, 0.5), pct(win, 0.95), pct(win, 1.0), n, pp);
			fflush(stdout);
			win.clear();
		}
	}
	double sum = 0.0;
	size_t worst = 0;
	for (size_t k = 0; k < all.size(); ++k) {
		sum += all[k];
		if (all[k] > all[worst]) worst = k;
	}
	const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count();
	printf("{\"players\":%d,\"size\":%.0f,\"map\":%d,\"seed\":%u,\"turns\":%zu,\"wall_s\":%.1f,\"turn_ms_mean\":%.3f,"
		   "\"turn_ms_p95\":%.3f,\"turn_ms_p99\":%.3f,\"turn_ms_max\":%.3f,\"max_turn\":%zu,\"hash\":%llu}\n",
			players, size, map, seed, all.size(), wall, all.empty() ? 0.0 : sum / all.size(), pct(all, 0.95),
			pct(all, 0.99), pct(all, 1.0), worst, (unsigned long long)TurnManager().compute_sim_hash(s));
	return 0;
}
