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
		// An outpost: once captured it stays on neutral land without decaying (TerritoryDecay.js).
		const uint32_t house = sim->spawn_building(-30, -30, BLD_OUTPOST, 1, 1.0f); // cells: (-60, -60) m
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
		p.colors[0] = 23; p.colors[1] = 0; p.colors[2] = 10;
		p.nomad = 1; p.regicide_garrison = 1; p.no_treasures = 1;
		p.names[0] = "Stan; the=Great, 100%"; p.names[1] = "Ptolemy Philadelphus"; p.names[2] = "";
		GameSetupParams q;
		CHECK(parse_game_start_full(format_game_start_full(p), q));
		CHECK(q.victory == VICTORY_REGICIDE && q.waves == 1 && q.map_size == 384 && q.lms == 1 && q.pop_cap == 300 && q.ceasefire == 10);
		CHECK(q.reveal == 1 && q.explore == 1 && q.wonder_min == 5 && q.relic_min == 45 && q.relics == 6);
		CHECK(q.biome == BIOME_POLAR + 1 && q.diffs[0] == 3 && q.diffs[1] == 5 && q.diffs[2] == 0);
		CHECK(q.behs[0] == 2 && q.behs[1] == 3 && q.behs[2] == 1);
		CHECK(q.colors[0] == 23 && q.colors[1] == 0 && q.colors[2] == 10 && q.colors[3] == 3);
		CHECK(q.nomad == 1 && q.regicide_garrison == 1 && q.no_treasures == 1);
		CHECK(q.names[0] == "Stan; the=Great, 100%" && q.names[1] == "Ptolemy Philadelphus" && q.names[2].empty() && q.names[3].empty());
		GameSetupParams longname; longname.names[0] = std::string(60, 'x'); longname.slot_count = 1;
		CHECK(parse_game_start_full(format_game_start_full(longname), q) && q.names[0].size() == 48);
		GameSetupParams d;
		CHECK(d.wonder_min == 20 && d.relic_min == 20 && d.relics == 2); // 0 A.D.'s defaults
	}
	// 11. Wonder hold time and relic count from game setup.
	{
		Simulation *sim = fresh(VICTORY_WONDER);
		sim->set_wonder_duration(120.0f);
		sim->spawn_building(-20, 20, BLD_WONDER, 0, 1.0f);
		sim->spawn_building(-20, 34, BLD_TOWN_CENTER, 0, 1.0f); // the wonder's territory: no decay to Gaia
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
		CHECK(map_relics == 0); // relics come from the victory condition, not the map
		delete sim;
	}
	{
		Simulation *sim = fresh(VICTORY_RELIC); // no setting: Setup.js's one relic
		run(*sim, 0.1f);
		CHECK(sim->relics().size() == 1);
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

	// 15. Nomad: a random map starts with the players' units and no player structures.
	{
		Simulation sim;
		sim.generate_random_map(MAP_MAINLAND, 5, 2, 64.0f);
		int structures = 0;
		for (const auto &b : sim.buildings()) structures += b.team < MAX_PLAYERS;
		const size_t units = sim.unit_count();
		CHECK(structures > 0);
		sim.set_nomad(true);
		sim.generate_random_map(MAP_MAINLAND, 5, 2, 64.0f);
		int left = 0;
		for (const auto &b : sim.buildings()) left += b.team < MAX_PLAYERS;
		CHECK(left == 0 && sim.unit_count() == units);
		sim.set_player_active(0, true, false);
		sim.set_player_active(1, true, true);
		sim.set_starting_resources(0, 0, 0, 0); // every Civic Centre resource comes from treasure
		const size_t before = sim.treasures().size();
		sim.set_victory_condition(VICTORY_REGICIDE);
		sim.tick(0.05f); // Regicide heroes spawn next to the first unit when there is no Civic Centre
		CHECK(sim.regicide_hero(0) != 0 && sim.regicide_hero(1) != 0);
		// placePlayersNomad: enough treasure for a Civic Centre on top of the starting resources.
		// The Town Center costs wood, metal and stone: at least one treasure of each per player.
		CHECK(sim.treasures().size() >= before + 6);
		int tre_res[RES_COUNT] = {};
		for (const auto &t : sim.treasures()) {
			const TreasureTypeInfo &ti = treasure_type_info(t.type);
			tre_res[RES_WOOD] += ti.amount[1]; tre_res[RES_STONE] += ti.amount[2]; tre_res[RES_GOLD] += ti.amount[3];
		}
		const BuildingTypeInfo &cc = building_type_info(BLD_TOWN_CENTER);
		CHECK(tre_res[RES_WOOD] >= 2 * cc.wood_cost && tre_res[RES_STONE] >= 2 * cc.stone_cost && tre_res[RES_GOLD] >= 2 * cc.gold_cost);
	}

	// 16. RegicideGarrison: the Regicide hero stays out of structures unless the option is on.
	for (int on = 0; on < 2; ++on) {
		Simulation sim;
		sim.generate_random_map(MAP_MAINLAND, 5, 2, 64.0f);
		sim.set_victory_condition(VICTORY_REGICIDE);
		sim.set_regicide_garrison(on != 0);
		sim.tick(0.05f);
		const uint32_t hero = sim.regicide_hero(0);
		uint32_t tc = 0;
		for (const auto &b : sim.buildings())
			if (b.team == 0 && b.type == BLD_TOWN_CENTER) tc = b.id;
		CHECK(hero != 0 && tc != 0);
		sim.order_garrison(&hero, 1, tc);
		for (int t = 0; t < 200 && sim.get_garrison_count(tc) == 0; ++t) sim.tick(0.05f);
		CHECK((sim.get_garrison_count(tc) > 0) == (on != 0));
	}

	// 18. DisableTreasures: no treasure is left once the match starts.
	{
		Simulation sim;
		sim.generate_random_map(MAP_MAINLAND, 5, 2, 64.0f);
		sim.spawn_resource_treasure(4.0f, 4.0f, RES_FOOD);
		CHECK(!sim.treasures().empty());
		sim.set_disable_treasures(true);
		sim.tick(0.05f);
		CHECK(sim.treasures().empty());
	}

	// 17. Player colours: 0 A.D. player_defaults.json by default, Game Setup palette after, Gaia white.
	{
		Simulation sim;
		CHECK(sim.player_color(0) == 0x153795 && sim.player_color(1) == 0x961414 && sim.player_color(GAIA_PLAYER) == 0xFFFFFF);
		sim.set_player_color(0, 23);
		sim.set_player_color(1, 24); // out of range: ignored
		CHECK(sim.player_color_index(0) == 23 && sim.player_color(0) == 0x505050 && sim.player_color_index(1) == 1);
	}

	// 19. Starting resources: 0 A.D.'s presets give every resource the same amount, sent as res=0..5.
	{
		Simulation sim;
		sim.set_starting_resources_preset(5);
		CHECK(sim.food(0) == 50000 && sim.wood(3) == 50000 && sim.gold(7) == 50000 && sim.stone(1) == 50000);
		sim.set_starting_resources_preset(9); // out of range: 0 A.D.'s default, Low
		CHECK(sim.food(0) == 300 && sim.stone(0) == 300);
		GameSetupParams gp, back;
		gp.start_res = 5;
		CHECK(parse_game_start_full(format_game_start_full(gp), back) && back.start_res == 5);
	}

	// 20. Team "None": a teamless player is never allied, even when its id equals a team's.
	{
		Simulation sim;
		const uint8_t teams[MAX_PLAYERS] = {3, 3, 0, 1, 0, 1, 0, 0};
		sim.set_setup_teams(teams);
		sim.apply_team_diplomacy();
		CHECK(sim.player_team(0) == sim.player_team(1) && sim.player_team(3) == sim.player_team(5));
		for (uint8_t p : {2, 4, 6, 7})
			for (uint8_t q = 0; q < MAX_PLAYERS; ++q)
				if (q != p) CHECK(sim.player_team(p) != sim.player_team(q));
		CHECK(sim.player_team(0) != sim.player_team(3));
	}

	// 21. World Population: the world cap is split among the seated players still in
	// the game, each defeat raises the others' share, and peers get it as worldpop=.
	{
		Simulation sim;
		for (uint8_t p = 0; p < 4; ++p) sim.set_player_active(p, true, false);
		sim.set_world_pop_cap(600);
		CHECK(sim.world_pop_cap() == 600 && sim.pop_cap() == 150 && sim.pop_cap_setting() == -600);
		sim.defeated_mask_ |= 1u << 3;
		CHECK(sim.pop_cap() == 200);
		CHECK(sim.pop_max(0) <= 200);
		sim.set_pop_cap(300);
		CHECK(sim.world_pop_cap() == 0 && sim.pop_cap() == 300);
		GameSetupParams gp, back;
		gp.world_pop = 2400;
		CHECK(parse_game_start_full(format_game_start_full(gp), back) && back.world_pop == 2400);
		GameSetupParams none;
		CHECK(parse_game_start_full(format_game_start_full(none), back) && back.world_pop == 0);
	}

	// 22. Lock Teams (0 A.D. Commands.js "diplomacy"): no stance change while teams are
	// locked or a ceasefire runs; the flag shares the v32 byte with RegicideGarrison.
	{
		Simulation sim;
		sim.set_player_active(0, true, false);
		sim.set_player_active(1, true, false);
		CHECK(sim.cmd_set_diplomacy(0, 1, Simulation::DIPLO_NEUTRAL));
		sim.set_lock_teams(true);
		sim.set_regicide_garrison(true);
		CHECK(sim.teams_locked() && sim.regicide_garrison());
		CHECK(!sim.cmd_set_diplomacy(0, 1, Simulation::DIPLO_ALLY) && sim.get_diplomacy(0, 1) == Simulation::DIPLO_NEUTRAL);
		sim.set_regicide_garrison(false);
		CHECK(sim.teams_locked() && !sim.regicide_garrison());
		sim.set_lock_teams(false);
		sim.set_ceasefire_seconds(60.0f);
		CHECK(!sim.cmd_set_diplomacy(0, 1, Simulation::DIPLO_ALLY));
		GameSetupParams gp, back;
		gp.lock_teams = 1;
		CHECK(parse_game_start_full(format_game_start_full(gp), back) && back.lock_teams == 1);
	}

	// 23. Cheats (0 A.D. Cheat.js): nothing works until Game Setup enables them.
	{
		Simulation sim;
		sim.generate_random_map(MAP_MAINLAND, 9, 2, 64.0f);
		sim.set_player_active(0, true, false);
		sim.set_player_active(1, true, false);
		const int food = sim.food(0);
		CHECK(!sim.cmd_cheat(0, Simulation::CHEAT_ADD_RESOURCE, 1000, RES_FOOD, nullptr, 0) && sim.food(0) == food);
		sim.set_cheats_enabled(true);
		CHECK(sim.cheats_enabled() && !sim.teams_locked());
		CHECK(sim.cmd_cheat(0, Simulation::CHEAT_ADD_RESOURCE, 1000, RES_FOOD, nullptr, 0) && sim.food(0) == food + 1000);
		CHECK(sim.cmd_cheat(0, Simulation::CHEAT_CHANGE_PHASE, 0, 0, nullptr, 0) && sim.player_phase(0) == 2);
		// wololo: an enemy unit becomes ours; black death kills it; exodia defeats player 2.
		uint32_t enemy = 0, tc = 0;
		for (size_t i = 0; i < sim.units_.count && !enemy; ++i)
			if (sim.units_.alive[i] && sim.units_.team[i] == 1) enemy = sim.units_.id[i];
		for (const auto &b : sim.buildings())
			if (b.team == 0 && b.type == BLD_TOWN_CENTER) tc = b.id;
		CHECK(enemy && sim.cmd_cheat(0, Simulation::CHEAT_CONVERT_UNITS, -1, 0, &enemy, 1));
		CHECK(sim.units_.team[sim.lookup_unit(enemy)] == 0);
		CHECK(sim.cmd_cheat(0, Simulation::CHEAT_KILL_UNITS, 0, 0, &enemy, 1) && !sim.units_.alive[sim.lookup_unit(enemy)]);
		const int pop = sim.pop_current(0);
		CHECK(tc && sim.cmd_cheat(0, Simulation::CHEAT_CREATE_UNITS, 3, 0, &tc, 1) && sim.pop_current(0) == pop + 3);
		CHECK(sim.cmd_cheat(0, Simulation::CHEAT_DEFEAT_PLAYER, 1, 0, nullptr, 0) && sim.is_defeated(1));
		CHECK(sim.cmd_cheat(0, Simulation::CHEAT_REVEAL_MAP, 0, 0, nullptr, 0) && sim.reveal_map());
		// the hive master: house bonuses jump to the population limit + 500; TARDIS: limit + 500.
		sim.set_pop_cap(300);
		CHECK(sim.pop_max(0) < 300);
		CHECK(sim.cmd_cheat(0, Simulation::CHEAT_MAX_POPULATION, 0, 0, nullptr, 0) && sim.pop_max(0) == 300);
		CHECK(sim.pop_max_uncapped(0) == 800);
		CHECK(sim.cmd_cheat(0, Simulation::CHEAT_CHANGE_MAX_POPULATION, 0, 0, nullptr, 0) && sim.pop_max(0) == 800);
		CHECK(sim.cmd_cheat(0, Simulation::CHEAT_CHANGE_MAX_POPULATION, 0, 0, nullptr, 0) && sim.pop_max(0) == 800);
		CHECK(sim.pop_max(1) < 300);
		// i am too busy toggles x0.01 build, train, research and pack times and x1000 gather rates.
		const float build = sim.tech_mods(0).bld_build_time[BLD_HOUSE];
		CHECK(sim.cmd_cheat(0, Simulation::CHEAT_FAST_ACTIONS, 0, 0, nullptr, 0));
		CHECK(std::fabs(sim.tech_mods(0).bld_build_time[BLD_HOUSE] - build * 0.01f) < 1e-6f);
		CHECK(sim.tech_mods(0).gather[UNIT_VILLAGER][Simulation::GK_WOOD] >= 999.0f && sim.tech_mods(1).gather[UNIT_VILLAGER][Simulation::GK_WOOD] < 10.0f);
		const char *cpath = "test_victory_cheats.sav";
		CHECK(sim.save_state(cpath));
		{
			Simulation loaded;
			CHECK(loaded.load_state(cpath) && loaded.pop_max(0) == 800 && loaded.cheat_mods(0).fast_actions == 1);
			CHECK(std::fabs(loaded.tech_mods(0).bld_build_time[BLD_HOUSE] - build * 0.01f) < 1e-6f);
		}
		std::remove(cpath);
		CHECK(sim.cmd_cheat(0, Simulation::CHEAT_FAST_ACTIONS, 0, 0, nullptr, 0) && sim.tech_mods(0).bld_build_time[BLD_HOUSE] == build);
		GameSetupParams gp, back;
		gp.cheats = 1;
		CHECK(parse_game_start_full(format_game_start_full(gp), back) && back.cheats == 1);
	}

	// 24. Spies (0 A.D. VisionSharing.AddSpy, unlock_spies, spy_counter) and DisableSpies.
	{
		Simulation sim;
		sim.generate_random_map(MAP_MAINLAND, 9, 2, 64.0f);
		sim.set_player_active(0, true, false);
		sim.set_player_active(1, true, false);
		float ex = 0, ez = 0;
		for (const auto &b : sim.buildings())
			if (b.team == 1 && b.type == BLD_TOWN_CENTER) { ex = b.x; ez = b.z; }
		sim.players_[0].res[RES_GOLD] = 1000.0f;
		CHECK(!sim.can_bribe(0, 1) && sim.cmd_bribe(0, 1) == Simulation::BRIBE_NOT_ALLOWED);
		sim.complete_research(0, TECH_UNLOCK_SPIES);
		CHECK(sim.can_bribe(0, 1) && sim.bribe_cost(0, 1) == 500);
		// Player 2 has no trader yet: a failed bribe costs a quarter.
		CHECK(sim.cmd_bribe(0, 1) == Simulation::BRIBE_NO_UNITS && sim.player_resource(0, RES_GOLD) == 875);
		const uint32_t trader = sim.spawn_unit(ex + 6.0f, ez + 6.0f, 1, UNIT_TRADER);
		sim.tick_fow();
		CHECK(sim.fow_state_at(ex + 6.0f, ez + 6.0f, 0) != Simulation::FOW_VISIBLE);
		CHECK(sim.cmd_bribe(0, 1) == Simulation::BRIBE_OK && sim.player_resource(0, RES_GOLD) == 375);
		CHECK(sim.spies().size() == 1 && sim.spies()[0].unit_id == trader && std::fabs(sim.spies()[0].remaining - 15.0f) < 1e-4f);
		sim.tick_fow();
		CHECK(sim.fow_state_at(ex + 6.0f, ez + 6.0f, 0) == Simulation::FOW_VISIBLE);
		// The only trader is already spied, and 375 metal can't pay for another.
		CHECK(sim.cmd_bribe(0, 1) == Simulation::BRIBE_NO_UNITS);
		const char *path = "test_victory_spies.sav";
		CHECK(sim.save_state(path));
		Simulation back_sim;
		CHECK(back_sim.load_state(path) && back_sim.spies().size() == 1 && back_sim.spies()[0].unit_id == trader);
		std::remove(path);
		sim.tick_spies(15.0f);
		CHECK(sim.spies().empty());
		// Counterintelligence: x1.5 against the bribed player.
		sim.complete_research(1, TECH_SPY_COUNTER);
		CHECK(sim.bribe_cost(0, 1) == 750 && sim.bribe_cost(0, 1, true) == 187);
		sim.set_disable_spies(true);
		CHECK(sim.spies_disabled() && !sim.can_bribe(0, 1));
		CHECK(sim.research_check(1, TECH_UNLOCK_SPIES) == Simulation::TECH_ERR_DISABLED);
		GameSetupParams gp, back;
		gp.no_spies = 1;
		CHECK(parse_game_start_full(format_game_start_full(gp), back) && back.no_spies == 1);
	}

	// 25. Teams without Lock Teams (0 A.D. Diplomacy): teammates start allied but either
	// one can break the alliance by leaving the team; locked teams can't change at all.
	{
		Simulation sim;
		for (uint8_t p = 0; p < 4; ++p) sim.set_player_active(p, true, false);
		const uint8_t teams[MAX_PLAYERS] = {1, 1, 0, 1};
		sim.set_setup_teams(teams);
		sim.apply_team_diplomacy();
		CHECK(sim.is_ally(0, 1) && sim.is_ally(0, 3) && !sim.is_hostile(1, 0) && sim.is_hostile(0, 2));
		CHECK(sim.cmd_set_diplomacy(0, 1, Simulation::DIPLO_ENEMY));
		CHECK(!sim.is_ally(0, 1) && sim.is_hostile(0, 1) && !sim.is_hostile(1, 0));
		CHECK(sim.is_ally(0, 3) && sim.is_ally(1, 3) && sim.same_team(1, 3) && !sim.same_team(0, 1));
		CHECK(sim.player_team(1) == 1 && sim.player_team(0) == 0);
		CHECK(sim.cmd_set_diplomacy(0, 1, Simulation::DIPLO_ALLY) && sim.is_ally(0, 1));
		sim.set_lock_teams(true);
		CHECK(!sim.cmd_set_diplomacy(1, 3, Simulation::DIPLO_ENEMY) && sim.is_ally(1, 3));
	}

	// 26. Session chat notices (0 A.D. messages.js tribute, barter, phase and
	// diplomacy notifications): the sim reports each once, presentation filters.
	{
		using N = Simulation::SessionNotice;
		Simulation sim;
		sim.generate_random_map(MAP_MAINLAND, 9, 2, 64.0f);
		sim.set_player_active(0, true, false);
		sim.set_player_active(1, true, false);
		(void)sim.take_session_notices();
		for (int r = 0; r < RES_COUNT; ++r) sim.players_[0].res[r] = 5000.0f;
		const int amounts[RES_COUNT] = {0, 0, 50, 100}; // wood, metal, stone, food
		CHECK(sim.cmd_tribute(0, 1, amounts));
		auto ns = sim.take_session_notices();
		CHECK(ns.size() == 1 && ns[0].kind == N::TRIBUTE && ns[0].player == 0 && ns[0].other == 1 &&
				ns[0].amounts[RES_STONE] == 50 && ns[0].amounts[RES_FOOD] == 100 && ns[0].amounts[RES_WOOD] == 0);
		CHECK(sim.take_session_notices().empty()); // handed over once
		CHECK(sim.cmd_set_diplomacy(0, 1, Simulation::DIPLO_NEUTRAL));
		CHECK(sim.cmd_set_diplomacy(0, 1, Simulation::DIPLO_NEUTRAL)); // no change, no notice
		ns = sim.take_session_notices();
		CHECK(ns.size() == 1 && ns[0].kind == N::DIPLOMACY && ns[0].other == 1 && ns[0].tech == Simulation::DIPLO_NEUTRAL);
		uint32_t tc = 0;
		for (const auto &b : sim.buildings())
			if (b.team == 0 && b.type == BLD_TOWN_CENTER) tc = b.id;
		CHECK(tc != 0);
		sim.complete_research(0, TECH_PHASE_TOWN); // e.g. the cheat path
		sim.complete_research(0, TECH_PHASE_TOWN); // already had it
		ns = sim.take_session_notices();
		CHECK(ns.size() == 1 && ns[0].kind == N::PHASE_COMPLETED && ns[0].tech == TECH_PHASE_TOWN);
		// Player 2 starts the Town Phase (five village structures) and cancels it.
		uint32_t tc1 = 0;
		float tx = 0, tz = 0;
		for (const auto &b : sim.buildings())
			if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tc1 = b.id; tx = b.x; tz = b.z; }
		for (int r = 0; r < RES_COUNT; ++r) sim.players_[1].res[r] = 5000.0f;
		for (int h = 0; h < 5; ++h) sim.spawn_building_world(tx - 8.0f + 4.0f * h, tz + 10.0f, BLD_HOUSE, 1, 1.0f);
		(void)sim.take_session_notices();
		const auto why = sim.research_check(1, TECH_PHASE_TOWN, tc1);
		CHECK(why == Simulation::TECH_OK);
		CHECK(sim.cmd_research(1, tc1, TECH_PHASE_TOWN));
		CHECK(sim.cmd_cancel_train(1, tc1, 0));
		ns = sim.take_session_notices();
		CHECK(ns.size() == 2 && ns[0].kind == N::PHASE_STARTED && ns[0].player == 1 && ns[1].kind == N::PHASE_ABORTED &&
				ns[1].tech == TECH_PHASE_TOWN);
		CHECK(sim.cmd_research(0, tc, TECH_WEAPONS_1) || true);
		ns = sim.take_session_notices();
		CHECK(ns.empty()); // only phases are announced
		sim.spawn_building_world(-20.0f, -20.0f, BLD_MARKET, 0, 1.0f);
		CHECK(sim.cmd_barter(0, RES_FOOD, RES_WOOD, 100));
		ns = sim.take_session_notices();
		CHECK(ns.size() == 1 && ns[0].kind == N::BARTER && ns[0].other == RES_FOOD && ns[0].tech == RES_WOOD &&
				ns[0].amounts[0] == 100 && ns[0].amounts[1] > 0);
		CHECK(sim.can_barter(0));
	}

	// 27. Landscape (0 A.D. unknown.json Landscapes): Unknown builds the chosen
	// landscape; Mainland has no water, Central Sea splits the map by water.
	{
		auto water_share = [](uint8_t landscape, uint32_t seed) {
			Simulation sim;
			sim.set_map_landscape(landscape);
			sim.generate_random_map(MAP_UNKNOWN, seed, 2, 64.0f);
			int wet = 0, all = 0;
			for (float x = -60.0f; x <= 60.0f; x += 2.0f)
				for (float z = -60.0f; z <= 60.0f; z += 2.0f) {
					if (x * x + z * z > 58.0f * 58.0f) continue; // circular map
					++all;
					wet += sim.get_terrain_height(x, z) < sim.water_height();
				}
			return (float)wet / (float)all;
		};
		for (uint32_t seed : {3u, 11u}) {
			CHECK(water_share(3 + 12, seed) == 0.0f);  // Mainland
			CHECK(water_share(3 + 2, seed) > 0.10f);   // Naval: Central Sea
		}
		GameSetupParams gp, back;
		gp.landscape = 3 + 12;
		CHECK(parse_game_start_full(format_game_start_full(gp), back) && back.landscape == 15);
		gp.landscape = 0;
		CHECK(format_game_start_full(gp).find("landscape=") == std::string::npos);
	}

	if (failures) {
		std::printf("%d check(s) failed\n", failures);
		return 1;
	}
	std::printf("test_victory: all checks passed\n");
	return 0;
}
