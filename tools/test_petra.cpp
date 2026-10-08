// RTS0 Petra AI headless regression test (no SCons, no Godot).
//   g++ -std=c++20 -O2 -Isrc tools/test_petra.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp -lpthread
//
//  1. No early rush: on the default arena with an idle player, Petra does not
//     launch an attack or kill player units during the village phase
//     (attackManager.js only plans default attacks from the town phase on).
//  2. No village-phase stall: on a continental random map Petra keeps
//     training workers and researches the town phase (headquarters.js /
//     researchManager.js checkPhase at Config.Economy.popPhase2).
//  Exit: 0 = PASS, 1 = FAIL.
#include "simulation/simulation.h"
#include <cstdio>
#include <memory>
using namespace rtso;

static int g_failures = 0;
#define CHECK(cond, what) do { if (!(cond)) { std::fprintf(stderr, "  [FAIL] %s: %s\n", what, #cond); ++g_failures; } } while (0)

static int count_units(const Simulation &s, uint8_t team, bool workers_only) {
	const auto u = s.units();
	int n = 0;
	for (size_t i = 0; i < u.count; ++i) {
		if (!u.alive[i] || u.team[i] != team) continue;
		const bool worker = u.unit_type[i] == UNIT_VILLAGER || u.unit_type[i] == UNIT_FEMALE_CITIZEN;
		if (!workers_only || worker) ++n;
	}
	return n;
}

static void test_no_early_rush() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(1);
	s->spawn_initial();
	const int start_units = count_units(*s, 0, false);
	bool attacked = false;
	float t = 0.0f;
	for (; t < 300.0f && s->petra_phase() < 2; t += Simulation::FIXED_STEP) {
		s->tick(Simulation::FIXED_STEP);
		const auto st = s->petra_attack_plan().state;
		if (st == Simulation::AttackPlan::WALKING || st == Simulation::AttackPlan::ATTACKED) attacked = true;
	}
	std::printf("  arena: %.0fs in village phase, player units %d -> %d, attack launched: %s\n",
	            t, start_units, count_units(*s, 0, false), attacked ? "yes" : "no");
	CHECK(!attacked, "no village-phase attack");
	CHECK(count_units(*s, 0, false) == start_units, "idle player loses no units");
	CHECK(!s->is_game_over(), "match still running");
}

static void test_phase_up() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->set_petra_difficulty(3);
	s->set_petra_behavior(0);
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	float town_at = -1.0f;
	int peak_workers = 0;
	for (float t = 0.0f; t < 15.0f * 60.0f && !s->is_game_over(); t += Simulation::FIXED_STEP) {
		s->tick(Simulation::FIXED_STEP);
		if (town_at < 0.0f && s->petra_phase() >= 2) town_at = t;
		if ((int)(t * 20.0f) % 200 == 0) {
			int w = count_units(*s, 1, true);
			if (w > peak_workers) peak_workers = w;
		}
	}
	std::printf("  continental: town phase at %.0fs, peak workers %d\n", town_at, peak_workers);
	CHECK(town_at > 0.0f && town_at < 12.0f * 60.0f, "town phase within 12 minutes");
	CHECK(peak_workers >= 25, "economy grows past 25 workers");
}

int main() {
	std::printf("test_petra\n");
	test_no_early_rush();
	test_phase_up();
	std::printf(g_failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", g_failures);
	return g_failures ? 1 : 0;
}
