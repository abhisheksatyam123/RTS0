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
	float town_at = -1.0f, city_at = -1.0f;
	int peak_workers = 0, peak_fields = 0;
	for (float t = 0.0f; t < 15.0f * 60.0f && !s->is_game_over(); t += Simulation::FIXED_STEP) {
		s->tick(Simulation::FIXED_STEP);
		if (town_at < 0.0f && s->petra_phase() >= 2) town_at = t;
		if (city_at < 0.0f && s->petra_phase() >= 3) city_at = t;
		if ((int)(t * 20.0f) % 200 == 0) {
			int w = count_units(*s, 1, true);
			if (w > peak_workers) peak_workers = w;
			int f = 0;
			for (const auto &b : s->buildings())
				if (b.team == 1 && b.type == BLD_FIELD && b.build_progress >= 1.0f) ++f;
			if (f > peak_fields) peak_fields = f;
		}
	}
	std::printf("  continental: town phase at %.0fs, city phase at %.0fs, peak workers %d, fields %d\n",
	            town_at, city_at, peak_workers, peak_fields);
	CHECK(town_at > 0.0f && town_at < 12.0f * 60.0f, "town phase within 12 minutes");
	CHECK(city_at > 0.0f, "city phase within 15 minutes (Town structures built first)");
	CHECK(peak_workers >= 25, "economy grows past 25 workers");
	CHECK(peak_fields >= 2, "farms fields once hunt and berries run low");
}

static int count_buildings(const Simulation &s, uint8_t team) {
	int n = 0;
	for (const auto &b : s.buildings()) if (b.team == team && b.hp > 0.0f) ++n;
	return n;
}

// Petra runs once per seated AI slot and never for a human slot.
static void test_ai_slots() {
	// Two humans: player 1 is seated as a human, so Petra must leave it alone.
	{
		auto s = std::make_unique<Simulation>();
		s->set_sim_seed(42);
		s->reset();
		s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
		s->set_player_active(0, true, false);
		s->set_player_active(1, true, false);
		const int units1 = count_units(*s, 1, false), blds1 = count_buildings(*s, 1);
		const float food1 = s->player_resource(1, RES_FOOD);
		for (float t = 0.0f; t < 120.0f; t += Simulation::FIXED_STEP) s->tick(Simulation::FIXED_STEP);
		std::printf("  two humans: player 1 units %d -> %d, buildings %d -> %d, food %.0f -> %d\n",
		            units1, count_units(*s, 1, false), blds1, count_buildings(*s, 1), food1,
		            s->player_resource(1, RES_FOOD));
		CHECK(count_units(*s, 1, false) == units1, "no units trained for a human slot");
		CHECK(count_buildings(*s, 1) == blds1, "no buildings placed for a human slot");
		CHECK(s->player_resource(1, RES_FOOD) <= (int)food1, "no AI trickle for a human slot");
	}
	// Human vs two AIs: both AIs build up their own economies.
	{
		auto s = std::make_unique<Simulation>();
		s->set_sim_seed(7);
		s->reset();
		s->generate_random_map(MAP_RANDOM_CONTINENTAL, 7, 3, 96.0f);
		s->set_player_active(0, true, false);
		s->set_player_active(1, true, true);
		s->set_player_active(2, true, true);
		const int w1 = count_units(*s, 1, true), w2 = count_units(*s, 2, true);
		const int b1 = count_buildings(*s, 1), b2 = count_buildings(*s, 2);
		for (float t = 0.0f; t < 240.0f && !s->is_game_over(); t += Simulation::FIXED_STEP) s->tick(Simulation::FIXED_STEP);
		std::printf("  two AIs: workers %d -> %d and %d -> %d, buildings %d -> %d and %d -> %d\n",
		            w1, count_units(*s, 1, true), w2, count_units(*s, 2, true),
		            b1, count_buildings(*s, 1), b2, count_buildings(*s, 2));
		CHECK(w1 > 0 && w2 > 0, "both AI players start with workers");
		CHECK(count_units(*s, 1, true) > w1 + 4, "AI player 1 trains workers");
		CHECK(count_units(*s, 2, true) > w2 + 4, "AI player 2 trains workers");
		CHECK(count_buildings(*s, 2) > b2 + 2, "AI player 2 builds");
	}
}

int main() {
	std::printf("test_petra\n");
	test_no_early_rush();
	test_phase_up();
	test_ai_slots();
	std::printf(g_failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", g_failures);
	return g_failures ? 1 : 0;
}
