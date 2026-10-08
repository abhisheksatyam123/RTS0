// RTS0 headless match benchmark (driven by tools/bench_match.py).
//
// Builds against the simulation sources only (no Godot), so it can run in CI
// and on any dev box. Scenario "match": generate_random_map + fixed-length
// skirmish vs Petra with waves off. Measures the per-tick cost of a normal
// game and samples gameplay state (AI workers, phase, buildings) so behaviour
// changes show up. "war" (spawn_war(N) mass battle) is kept for ad-hoc use.
//
// Output is one JSON object on stdout (last line), plus optional progress
// lines prefixed with "[sample]".

#include "simulation/simulation.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace rtso;

namespace {

struct Counts {
	int units[2] = {0, 0};
	int workers[2] = {0, 0};
	int buildings[2] = {0, 0};
};

Counts count_state(const Simulation &s) {
	Counts c;
	auto u = s.units();
	for (size_t i = 0; i < u.count; ++i) {
		if (!u.alive[i] || u.team[i] > 1) continue;
		c.units[u.team[i]]++;
		if (u.unit_type[i] == UNIT_VILLAGER || u.unit_type[i] == UNIT_FEMALE_CITIZEN) c.workers[u.team[i]]++;
	}
	for (const auto &b : s.buildings()) {
		if (b.team <= 1 && b.hp > 0) c.buildings[b.team]++;
	}
	return c;
}

double pct(std::vector<double> v, double p) {
	if (v.empty()) return 0.0;
	std::sort(v.begin(), v.end());
	size_t i = (size_t)(p * (double)(v.size() - 1) + 0.5);
	return v[std::min(i, v.size() - 1)];
}

} // namespace

int main(int argc, char **argv) {
	std::string scenario = "match";
	uint32_t seed = 42;
	int map = MAP_RANDOM_CONTINENTAL;
	double minutes = 20.0;
	int war_units = 2000;
	float dt = 0.2f; // one lockstep turn (TurnManager TURN_DURATION)
	int sample_every_s = 120;
	// Endless (0 A.D. "Endless"): nobody is ever defeated, so the match always
	// runs its full length even once Petra overruns the idle player 0.
	int victory = VICTORY_ENDLESS;
	for (int i = 1; i < argc; ++i) {
		auto next = [&]() { return (i + 1 < argc) ? argv[++i] : (char *)"0"; };
		if (!strcmp(argv[i], "--scenario")) scenario = next();
		else if (!strcmp(argv[i], "--seed")) seed = (uint32_t)strtoul(next(), nullptr, 10);
		else if (!strcmp(argv[i], "--map")) map = atoi(next());
		else if (!strcmp(argv[i], "--minutes")) minutes = atof(next());
		else if (!strcmp(argv[i], "--units")) war_units = atoi(next());
		else if (!strcmp(argv[i], "--sample")) sample_every_s = atoi(next());
		else if (!strcmp(argv[i], "--victory")) victory = atoi(next());
	}

	Simulation s;
	s.reset();
	s.set_sim_seed(seed);
	s.set_waves_enabled(false);
	if (scenario == "war") {
		s.spawn_war(war_units);
	} else {
		s.generate_random_map((MapType)map, seed, 2, 64.0f);
	}
	s.set_victory_condition((uint8_t)victory);

	const int ticks = (int)(minutes * 60.0 / dt + 0.5);
	std::vector<double> tick_ms;
	tick_ms.reserve(ticks);
	int ended_at = -1;
	int next_sample = 0;
	auto wall0 = std::chrono::steady_clock::now();
	for (int t = 0; t < ticks; ++t) {
		const int sim_s = (int)(t * dt);
		if (sample_every_s > 0 && sim_s >= next_sample) {
			next_sample += sample_every_s;
			Counts c = count_state(s);
			fprintf(stderr, "[sample] t=%5ds p0 units=%d workers=%d bld=%d | ai units=%d workers=%d bld=%d phase=%d\n",
					sim_s, c.units[0], c.workers[0], c.buildings[0], c.units[1], c.workers[1], c.buildings[1],
					s.petra_phase());
		}
		if (s.is_game_over() || s.is_victory()) {
			ended_at = sim_s;
			break;
		}
		auto a = std::chrono::steady_clock::now();
		s.tick(dt);
		auto b = std::chrono::steady_clock::now();
		tick_ms.push_back(std::chrono::duration<double, std::milli>(b - a).count());
	}
	double wall_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count();

	double sum = 0.0;
	for (double v : tick_ms) sum += v;
	Counts c = count_state(s);
	printf("{\"scenario\":\"%s\",\"seed\":%u,\"map\":%d,\"sim_minutes\":%.2f,\"ticks\":%zu,"
		   "\"ended_at_s\":%d,\"wall_s\":%.3f,\"tick_ms_mean\":%.4f,\"tick_ms_p50\":%.4f,"
		   "\"tick_ms_p95\":%.4f,\"tick_ms_p99\":%.4f,\"tick_ms_max\":%.4f,"
		   "\"p0_units\":%d,\"p0_workers\":%d,\"p0_buildings\":%d,"
		   "\"ai_units\":%d,\"ai_workers\":%d,\"ai_buildings\":%d,\"ai_phase\":%d,"
		   "\"game_over\":%d,\"victory\":%d}\n",
		   scenario.c_str(), seed, map, tick_ms.size() * dt / 60.0, tick_ms.size(), ended_at, wall_s,
		   tick_ms.empty() ? 0.0 : sum / tick_ms.size(), pct(tick_ms, 0.50), pct(tick_ms, 0.95),
		   pct(tick_ms, 0.99), pct(tick_ms, 1.0), c.units[0], c.workers[0], c.buildings[0], c.units[1],
		   c.workers[1], c.buildings[1], s.petra_phase(), (int)s.is_game_over(), (int)s.is_victory());
	return 0;
}
