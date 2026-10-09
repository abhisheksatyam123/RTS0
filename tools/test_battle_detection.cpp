// 0 A.D. BattleDetection.js: the sim counts the damage each player's entities deal to
// other players (OnGlobalAttacked: not to Gaia, not to themselves), and the detector
// (template_player.xml: 200 ms periods, 12-period record, 0.04 hp/ms) turns it into the
// BATTLE / PEACE state that picks the music.
//   g++ -std=c++20 -O2 -Isrc tools/test_battle_detection.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp -lpthread \
//       -o /tmp/test_battle_detection && /tmp/test_battle_detection
#define private public
#include "simulation/simulation.h"
#undef private
#include "presentation/attack_detection.h"
#include <cmath>
#include <cstdio>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static void fresh(Simulation &s) {
	s.set_sim_seed(4);
	s.reset();
	s.set_victory_condition(VICTORY_ENDLESS);
	for (uint8_t p = 0; p < 2; ++p) s.players_[p].active = 1, s.players_[p].is_ai = 0;
	s.spawn_building_world(-50.0f, -50.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(50.0f, 50.0f, BLD_TOWN_CENTER, 1, 1.0f);
}

int main() {
	static Simulation s;

	// 1. The sim counts what each player deals to another player, and nothing for damage
	//    to Gaia or to its own side.
	{
		fresh(s);
		s.note_attacked(1, 0, 2, 1, false, 10.0f);
		s.note_attacked(1, 0, 3, GAIA_PLAYER, false, 50.0f);
		s.note_attacked(1, 0, 4, 0, true, 50.0f);
		s.note_attacked(5, GAIA_PLAYER, 1, 0, false, 50.0f);
		CHECK(s.battle_damage_dealt(0) == 10.0);
		CHECK(s.battle_damage_dealt(1) == 0.0);
	}

	// 2. Two swordsmen fighting: each side's count grows by what it dealt, and a passive
	//    enemy that never strikes back deals nothing.
	{
		fresh(s);
		const uint32_t a = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SWORDSMAN);
		const uint32_t b = s.spawn_unit(1.0f, 0.0f, 1, UNIT_SWORDSMAN);
		s.units_.stance[s.lookup_unit(b)] = STANCE_PASSIVE;
		s.order_attack(&a, 1, b, false);
		const float hp0 = s.units_.hp[s.lookup_unit(b)];
		for (int t = 0; t < 100; ++t) s.tick(Simulation::FIXED_STEP);
		const int32_t bi = s.lookup_unit(b);
		const float lost = bi >= 0 ? hp0 - std::max(0.0f, s.units_.hp[bi]) : hp0;
		CHECK(s.battle_damage_dealt(0) > 0.0);
		CHECK(s.battle_damage_dealt(0) + 1e-3 >= lost);
		CHECK(s.battle_damage_dealt(1) == 0.0);
		std::printf("swordsman dealt %.1f damage in 5 s\n", s.battle_damage_dealt(0));
	}

	// 3. The detector's threshold is 0.04 hp/ms, 40 hp a second: 30 a second stays at
	//    peace, 100 a second reaches BATTLE (alertness 4), and calm brings PEACE back once
	//    alertness falls to 0.
	{
		BattleDetection d;
		double t = 0.0;
		for (int k = 0; k < 50; ++k, t += 0.1) { d.add_damage(3.0f, t); d.update(t); }
		CHECK(!d.in_battle());
		BattleDetection e;
		t = 0.0;
		int ticks_to_battle = -1;
		for (int k = 0; k < 200; ++k, t += 0.1) {
			e.add_damage(10.0f, t);
			e.update(t);
			if (e.in_battle() && ticks_to_battle < 0) ticks_to_battle = k;
		}
		CHECK(e.in_battle());
		for (int k = 0; k < 400; ++k, t += 0.1) e.update(t);
		CHECK(!e.in_battle());
		std::printf("100 hp/s: battle after %.1f s; peace again once calm\n", ticks_to_battle * 0.1);
	}

	if (failures) {
		std::printf("test_battle_detection: %d failure(s)\n", failures);
		return 1;
	}
	std::printf("test_battle_detection: all passed\n");
	return 0;
}
