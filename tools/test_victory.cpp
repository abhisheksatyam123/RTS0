// Headless checks for game modes and victory conditions (Simulation::check_victory_conditions).
// Build: g++ -std=c++20 -O2 -Isrc tools/test_victory.cpp src/simulation/simulation.cpp
//        src/simulation/rms_generator.cpp src/core/job_system.cpp -lpthread -o test_victory
// The checks poke simulation state directly to stage end-of-match situations.
#define private public
#include "simulation/simulation.h"
#undef private
#include "network/net_types.h"
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
	// 1b. Default arena: the starting armies sit outside each other's aggro range, so an
	// idle player loses nothing before Petra's first attack (~23 s in).
	{
		Simulation *sim = fresh(VICTORY_CONQUEST);
		auto count0 = [&]() {
			int n = 0;
			for (size_t i = 0; i < sim->units_.count; ++i)
				if (sim->units_.team[i] == 0 && sim->units_.alive[i]) n++;
			return n;
		};
		int start = count0();
		run(*sim, 20.0f);
		CHECK(count0() == start);
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

	// 9b. Players who start with units but no buildings are alive in Conquest, and in
	// Regicide they still get a hero.
	for (VictoryCondition vc : {VICTORY_CONQUEST, VICTORY_REGICIDE}) {
		Simulation sim;
		sim.reset();
		sim.set_victory_condition(vc);
		sim.spawn_building(0, 0, BLD_TOWN_CENTER, 0, 1.0f);
		sim.spawn_unit(3.0f, 3.0f, 0, UNIT_VILLAGER);
		sim.spawn_unit(-40.0f, 40.0f, 2, UNIT_SWORDSMAN);
		sim.spawn_unit(-35.0f, 40.0f, 3, UNIT_SWORDSMAN);
		sim.record_.enemies_killed = 1;
		run(sim, 2.0f);
		CHECK(!sim.is_game_over() && !sim.is_victory());
		int heroes = 0;
		for (size_t i = 0; i < sim.units_.count; ++i)
			if (sim.units_.alive[i] && sim.units_.unit_type[i] == UNIT_HERO && sim.units_.team[i] >= 2) heroes++;
		CHECK(vc != VICTORY_REGICIDE || heroes == 2);
	}

	// 9c. Allied victory: player 2 is on player 0's team, so beating player 1 wins the match.
	// Under Last Man Standing the ally must fall too.
	for (bool lms : {false, true}) {
		Simulation *sim = fresh(VICTORY_CONQUEST);
		sim->set_last_man_standing(lms);
		sim->set_player_team(2, sim->player(0).team);
		sim->spawn_unit(-40.0f, 40.0f, 2, UNIT_SWORDSMAN);
		CHECK(sim->is_ally_of_player0(2) && !sim->is_ally_of_player0(1));
		wipe(*sim, 1, true, true);
		run(*sim, 1.0f);
		CHECK(sim->is_victory() == !lms);
		if (!lms) CHECK(sim->is_victory_for(2) && !sim->is_victory_for(1));
		if (lms) {
			wipe(*sim, 2, true, true);
			run(*sim, 1.0f);
			CHECK(sim->is_victory());
		}
		delete sim;
	}
	// 9d. An ally's wonder wins for player 0 in Wonder mode.
	{
		Simulation *sim = fresh(VICTORY_WONDER);
		sim->set_player_team(2, sim->player(0).team);
		sim->spawn_building(-20, 20, BLD_WONDER, 2, 1.0f);
		run(*sim, 2.0f);
		CHECK(sim->is_wonder_active());
		sim->wonder_victory_timer_ = 599.9f;
		run(*sim, 1.0f);
		CHECK(sim->is_victory());
		delete sim;
	}

	// 9e. Ceasefire: nobody takes damage until it ends; the population limit caps pop_max.
	{
		Simulation sim;
		sim.reset();
		sim.set_ceasefire_seconds(5.0f);
		sim.spawn_unit(0.0f, 0.0f, 0, UNIT_SWORDSMAN);
		sim.spawn_unit(1.5f, 0.0f, 1, UNIT_SWORDSMAN);
		CHECK(sim.ceasefire_active() && !sim.can_attack(0, 1));
		run(sim, 4.0f);
		CHECK(sim.units_.hp[0] == sim.units_.max_hp[0] && sim.units_.hp[1] == sim.units_.max_hp[1]);
		run(sim, 3.0f);
		CHECK(!sim.ceasefire_active());
		CHECK(sim.units_.hp[0] < sim.units_.max_hp[0] || sim.units_.hp[1] < sim.units_.max_hp[1] ||
		      !sim.units_.alive[0] || !sim.units_.alive[1]);
	}
	{
		Simulation *sim = fresh(VICTORY_CONQUEST);
		int uncapped = sim->pop_max(0);
		sim->set_pop_cap(15);
		CHECK(uncapped > 15 && sim->pop_max(0) == 15);
		sim->set_pop_cap(0);
		CHECK(sim->pop_max(0) == uncapped);
		delete sim;
	}

	// 10. Victory condition, wave mode and map size reach the joining peer in NMT_GAME_START.
	{
		GameSetupParams p;
		p.map_id = "random_continental";
		p.seed = 7;
		p.victory = VICTORY_REGICIDE;
		p.waves = 1;
		p.map_size = 384;
		p.lms = 1;
		p.pop_cap = 300;
		p.ceasefire = 10;
		GameSetupParams q;
		CHECK(parse_game_start_full(format_game_start_full(p), q));
		CHECK(q.victory == VICTORY_REGICIDE && q.waves == 1 && q.map_size == 384 && q.lms == 1 && q.pop_cap == 300 && q.ceasefire == 10);
	}

	if (failures) {
		std::printf("%d check(s) failed\n", failures);
		return 1;
	}
	std::printf("test_victory: all checks passed\n");
	return 0;
}
