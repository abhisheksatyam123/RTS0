// Headless checks for game modes and victory conditions (Simulation::check_victory_conditions).
// Build: g++ -std=c++20 -O2 -Isrc tools/test_victory.cpp src/simulation/simulation.cpp
//        src/simulation/rms_generator.cpp src/core/job_system.cpp -lpthread -o test_victory
// The checks poke simulation state directly to stage end-of-match situations.
#define private public
#include "simulation/simulation.h"
#undef private
#include <cstdio>
#include <cstdlib>

using namespace rtso;

static int failures = 0;
#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                    \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			failures++;                                                   \
		}                                                                 \
	} while (0)

static void run(Simulation &sim, float seconds) {
	for (int i = 0; i < (int)(seconds / 0.05f) && !sim.is_game_over() && !sim.is_victory(); ++i) sim.tick(0.05f);
}

static void kill_unit(Simulation &sim, size_t i) {
	sim.units_.hp[i] = 0.0f;
	sim.units_.alive[i] = 0;
	sim.units_.action_state[i] = ACTION_IDLE;
	if (sim.units_.team[i] != 0) sim.record_.enemies_killed++;
}

// Kill every unit / raze every structure a team owns.
static void wipe(Simulation &sim, uint8_t team, bool units, bool structures) {
	if (units) {
		for (size_t i = 0; i < sim.units_.count; ++i)
			if (sim.units_.team[i] == team) kill_unit(sim, i);
	}
	if (structures) {
		for (auto &b : sim.buildings_)
			if (b.team == team) b.hp = 0.0f;
		if (team != 0) sim.record_.buildings_destroyed++;
	}
}

static Simulation *fresh(VictoryCondition vc, bool waves = false) {
	auto *sim = new Simulation();
	sim->set_victory_condition(vc);
	sim->set_waves_enabled(waves);
	sim->spawn_initial();
	return sim;
}

int main() {
	// 1. Skirmish default: no waves, so no win for surviving them either.
	{
		Simulation *sim = fresh(VICTORY_CONQUEST);
		CHECK(!sim->waves_enabled());
		run(*sim, 300.0f);
		CHECK(sim->wave_number() == 0);
		CHECK(!sim->is_victory());
		delete sim;
	}
	// 2. Wave mode still works when turned on.
	{
		Simulation *sim = fresh(VICTORY_CONQUEST, true);
		run(*sim, 40.0f);
		CHECK(sim->wave_number() >= 1);
		delete sim;
	}
	// 3. Conquest: enemy needs both units and structures gone.
	{
		Simulation *sim = fresh(VICTORY_CONQUEST);
		wipe(*sim, 1, true, false);
		run(*sim, 1.0f);
		CHECK(!sim->is_victory());
		wipe(*sim, 1, false, true);
		run(*sim, 1.0f);
		CHECK(sim->is_victory());
		delete sim;
	}
	// 4. Conquest Units: killing units is enough.
	{
		Simulation *sim = fresh(VICTORY_CONQUEST_UNITS);
		wipe(*sim, 1, true, false);
		run(*sim, 1.0f);
		CHECK(sim->is_victory());
		delete sim;
	}
	// 5. Conquest Structures: razing structures is enough.
	{
		Simulation *sim = fresh(VICTORY_CONQUEST_STRUCTURES);
		wipe(*sim, 1, false, true);
		run(*sim, 1.0f);
		CHECK(sim->is_victory());
		delete sim;
	}
	// 6. Losing your Town Center alone doesn't end a conquest match.
	{
		Simulation *sim = fresh(VICTORY_CONQUEST);
		for (auto &b : sim->buildings_)
			if (b.team == 0 && b.type == BLD_TOWN_CENTER) b.hp = 0.0f;
		run(*sim, 1.0f);
		CHECK(!sim->is_game_over());
		wipe(*sim, 0, true, true);
		run(*sim, 1.0f);
		CHECK(sim->is_game_over() && !sim->is_victory());
		delete sim;
	}
	// 7. Regicide: both sides get a hero; killing the enemy hero wins.
	{
		Simulation *sim = fresh(VICTORY_REGICIDE);
		run(*sim, 0.05f);
		auto &u = sim->units_;
		int heroes[2] = {0, 0};
		size_t enemy_hero = 0;
		for (size_t i = 0; i < u.count; ++i) {
			if (!u.alive[i] || u.unit_type[i] != UNIT_HERO || u.team[i] > 1) continue;
			heroes[u.team[i]]++;
			if (u.team[i] == 1) enemy_hero = i;
		}
		CHECK(heroes[0] == 1 && heroes[1] == 1);
		CHECK(!sim->is_game_over());
		kill_unit(*sim, enemy_hero);
		run(*sim, 1.0f);
		CHECK(sim->is_victory());
		delete sim;
	}
	// 8. Endless: nobody is ever defeated.
	{
		Simulation *sim = fresh(VICTORY_ENDLESS);
		wipe(*sim, 0, true, true);
		run(*sim, 1.0f);
		CHECK(!sim->is_game_over());
		delete sim;
	}
	// 9. A finished wonder doesn't win a conquest match.
	{
		Simulation *sim = fresh(VICTORY_CONQUEST);
		sim->spawn_building(20, -20, BLD_WONDER, 0, 1.0f);
		run(*sim, 1.0f);
		CHECK(!sim->is_wonder_active());
		delete sim;
	}

	if (failures) {
		std::printf("%d check(s) failed\n", failures);
		return 1;
	}
	std::printf("test_victory: all checks passed\n");
	return 0;
}
