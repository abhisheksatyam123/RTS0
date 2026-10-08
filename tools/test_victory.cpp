// Headless checks for game modes and victory conditions (Simulation::check_victory_conditions).
// Build: g++ -std=c++20 -O2 -Isrc tools/test_victory.cpp src/simulation/simulation.cpp
//        src/simulation/rms_generator.cpp src/core/job_system.cpp -lpthread -o test_victory
// The checks poke simulation state directly to stage end-of-match situations.
#define private public
#include "simulation/simulation.h"
#undef private
#include "network/net_types.h"
#include <cstdio>
#include <cmath>
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
	// 9f. Player 0 falling doesn't end the match: it observes (map revealed, no orders reach
	// the sim) while its ally fights on. The ally's win is the ally's alone.
	{
		Simulation *sim = fresh(VICTORY_CONQUEST);
		sim->set_player_team(2, sim->player(0).team);
		sim->spawn_unit(-40.0f, 40.0f, 2, UNIT_SWORDSMAN);
		run(*sim, 0.5f);
		wipe(*sim, 0, true, true);
		run(*sim, 1.0f);
		CHECK(!sim->is_game_over() && sim->is_defeated(0) && !sim->is_defeated(2));
		CHECK(sim->fow_grid(0)[0] == Simulation::FOW_VISIBLE);
		wipe(*sim, 1, true, true);
		run(*sim, 1.0f);
		CHECK(sim->is_game_over() && !sim->is_victory());
		CHECK(sim->is_victory_for(2) && !sim->is_victory_for(0) && !sim->is_victory_for(1));
		delete sim;
	}
	// 9g. Last Man Standing free-for-all: the match runs until one player is left.
	{
		Simulation *sim = fresh(VICTORY_CONQUEST);
		sim->set_last_man_standing(true);
		sim->spawn_unit(-40.0f, 40.0f, 2, UNIT_SWORDSMAN);
		run(*sim, 0.5f);
		wipe(*sim, 0, true, true);
		run(*sim, 1.0f);
		CHECK(!sim->is_game_over() && sim->is_defeated(0));
		wipe(*sim, 1, true, true);
		run(*sim, 1.0f);
		CHECK(sim->is_game_over() && sim->winner_mask() == (1u << 2) && sim->is_victory_for(2));
		delete sim;
	}
	// 9h. A defeated player stays out even if it gets units back, and defeat survives a save.
	{
		Simulation *sim = fresh(VICTORY_CONQUEST);
		sim->set_player_team(2, sim->player(0).team);
		sim->spawn_unit(-40.0f, 40.0f, 2, UNIT_SWORDSMAN);
		run(*sim, 0.5f);
		wipe(*sim, 0, true, true);
		run(*sim, 1.0f);
		sim->spawn_unit(0.0f, 0.0f, 0, UNIT_SWORDSMAN);
		run(*sim, 1.0f);
		CHECK(sim->is_defeated(0) && !sim->is_game_over());
		const char *path = "test_victory_save.bin";
		CHECK(sim->save_state(path));
		Simulation loaded;
		CHECK(loaded.load_state(path));
		std::remove(path);
		CHECK(loaded.is_defeated(0) && !loaded.is_defeated(2) && loaded.participant_mask() == sim->participant_mask());
		delete sim;
	}
	// 9i. A defeated player's leftovers turn Gaia (Player.js): its structures stay, owned by
	// no slot, hold no territory, and any player can capture them.
	{
		Simulation *sim = fresh(VICTORY_CONQUEST_UNITS);
		sim->spawn_unit(-40.0f, 40.0f, 2, UNIT_SWORDSMAN);
		const uint32_t house = sim->spawn_building(-30, -30, BLD_HOUSE, 1, 1.0f); // cells: (-60, -60) m
		run(*sim, 0.5f);
		wipe(*sim, 1, true, false);
		run(*sim, 1.0f);
		CHECK(sim->is_defeated(1) && !sim->is_game_over());
		int gaia = 0, still_p1 = 0;
		for (const auto &b : sim->buildings_) {
			if (b.team == GAIA_PLAYER) gaia++;
			if (b.team == 1) still_p1++;
		}
		CHECK(gaia > 0 && still_p1 == 0);
		const Simulation::Building *hb = nullptr;
		for (const auto &b : sim->buildings_) if (b.id == house) hb = &b;
		CHECK(hb && hb->team == GAIA_PLAYER);
		// Capture is an attack (Attack.js Capture): the swordsmen are ordered to capture it.
		uint32_t capturers[6];
		for (int k = 0; k < 6; ++k) capturers[k] = sim->spawn_unit(hb->x + 4.0f + (float)k, hb->z + 4.0f, 0, UNIT_SWORDSMAN);
		sim->order_attack_building(capturers, 6, house, false, true);
		run(*sim, 60.0f);
		for (const auto &b : sim->buildings_) if (b.id == house) hb = &b;
		CHECK(hb->team == 0);
		delete sim;
	}
	// 9d. An ally's wonder wins for player 0 in Wonder mode.
	{
		Simulation *sim = fresh(VICTORY_WONDER);
		sim->set_player_team(2, sim->player(0).team);
		sim->spawn_building(-20, 20, BLD_WONDER, 2, 1.0f);
		run(*sim, 2.0f);
		CHECK(sim->is_wonder_active());
		sim->wonder_hold_[2] = 599.9f;
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
		p.reveal = 1;
		p.explore = 1;
		p.wonder_min = 5;
		p.relic_min = 45;
		p.relics = 6;
		p.biome = BIOME_POLAR + 1;
		p.slot_count = 3;
		p.diffs[1] = 5; p.diffs[2] = 0;
		p.behs[1] = 3; p.behs[2] = 1;
		GameSetupParams q;
		CHECK(parse_game_start_full(format_game_start_full(p), q));
		CHECK(q.victory == VICTORY_REGICIDE && q.waves == 1 && q.map_size == 384 && q.lms == 1 && q.pop_cap == 300 && q.ceasefire == 10);
		CHECK(q.reveal == 1 && q.explore == 1 && q.wonder_min == 5 && q.relic_min == 45 && q.relics == 6);
		CHECK(q.biome == BIOME_POLAR + 1 && q.diffs[0] == 3 && q.diffs[1] == 5 && q.diffs[2] == 0);
		CHECK(q.behs[0] == 2 && q.behs[1] == 3 && q.behs[2] == 1);
		GameSetupParams d;
		CHECK(d.wonder_min == 20 && d.relic_min == 20 && d.relics == 2); // 0 A.D.'s defaults
	}
	// 11. Wonder hold time and relic count from game setup.
	{
		Simulation *sim = fresh(VICTORY_WONDER);
		sim->set_wonder_duration(120.0f);
		sim->spawn_building(-20, 20, BLD_WONDER, 0, 1.0f);
		run(*sim, 119.0f);
		CHECK(!sim->is_game_over() && sim->wonder_time_remaining() < 2.0f);
		run(*sim, 2.0f);
		CHECK(sim->is_victory());
		delete sim;
	}
	for (int want : {1, 2, 9}) {
		Simulation *sim = fresh(VICTORY_RELIC);
		const size_t map_relics = sim->relics().size();
		sim->set_relic_count(want);
		sim->set_relic_duration(60.0f);
		run(*sim, 0.1f);
		CHECK(sim->relics().size() == (size_t)want);
		bool spread = true; // no two relics on the same spot
		for (size_t i = 0; i < sim->relics().size(); ++i)
			for (size_t j = i + 1; j < sim->relics().size(); ++j)
				spread &= std::hypot(sim->relics()[i].x - sim->relics()[j].x, sim->relics()[i].z - sim->relics()[j].z) > 1.0f;
		CHECK(spread);
		CHECK(map_relics > 0);
		delete sim;
	}
	{
		Simulation *sim = fresh(VICTORY_RELIC); // no setting: the map's relics stay
		const size_t map_relics = sim->relics().size();
		run(*sim, 0.1f);
		CHECK(sim->relics().size() == map_relics);
		delete sim;
	}

	// 12. A 6-slot lobby (U6): the random map has a base per slot and every AI slot gets Petra.
	{
		Simulation sim;
		sim.generate_random_map(MAP_MAINLAND, 11, 6, 128.0f);
		for (uint8_t t = 0; t < MAX_PLAYERS; ++t) sim.set_player_active(t, t < 6, t > 0 && t < 6);
		int tcs[MAX_PLAYERS] = {};
		for (const auto &b : sim.buildings_)
			if (b.type == BLD_TOWN_CENTER && b.team < MAX_PLAYERS) tcs[b.team]++;
		for (uint8_t t = 0; t < 6; ++t) CHECK(tcs[t] == 1);
		for (uint8_t t = 1; t < 6; ++t) CHECK(sim.petra_runs_for(t));
		CHECK(!sim.petra_runs_for(0) && !sim.petra_runs_for(6));
	}

	// 13. Game Setup biome override for random maps (and back to the map's own).
	{
		Simulation sim;
		sim.set_biome_override(BIOME_POLAR);
		sim.generate_random_map(MAP_MAINLAND, 5, 2, 64.0f);
		CHECK(sim.biome() == BIOME_POLAR && sim.current_map_biome() == "polar");
		sim.set_biome_override(-1);
		sim.generate_random_map(MAP_MAINLAND, 5, 2, 64.0f);
		CHECK(sim.biome() != BIOME_POLAR && sim.current_map_biome() != "polar");
	}

	// 14. Game Setup per-slot AI settings, applied after the shared ones as main_node does.
	{
		Simulation sim;
		sim.generate_random_map(MAP_MAINLAND, 5, 3, 64.0f);
		sim.set_player_active(1, true, true);
		sim.set_player_active(2, true, true);
		sim.set_petra_difficulty(PETRA_MEDIUM);
		sim.set_petra_behavior(PETRA_BEHAVIOR_BALANCED);
		sim.set_petra_difficulty(1, PETRA_VERY_HARD);
		sim.set_petra_behavior(1, PETRA_BEHAVIOR_AGGRESSIVE);
		sim.set_petra_difficulty(2, PETRA_EASY);
		sim.set_petra_behavior(2, PETRA_BEHAVIOR_BALANCED); // main_node sets both for every slot
		CHECK(sim.petra_config_for(1).difficulty == PETRA_VERY_HARD);
		CHECK(sim.petra_config_for(1).behavior == PETRA_BEHAVIOR_AGGRESSIVE);
		CHECK(sim.petra_config_for(2).difficulty == PETRA_EASY);
		CHECK(sim.petra_config_for(2).behavior == PETRA_BEHAVIOR_BALANCED);
		CHECK(sim.ai_gather_mult(1) > sim.ai_gather_mult(2));
	}

	if (failures) {
		std::printf("%d check(s) failed\n", failures);
		return 1;
	}
	std::printf("test_victory: all checks passed\n");
	return 0;
}
