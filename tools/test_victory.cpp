// Headless checks for game modes and victory conditions (Simulation::check_victory_conditions).
// Build: g++ -std=c++20 -O2 -Isrc tools/test_victory.cpp src/simulation/simulation.cpp
//        src/simulation/rms_generator.cpp src/core/job_system.cpp -lpthread -o test_victory
// The checks poke simulation state directly to stage end-of-match situations.
#define private public
#include <algorithm>
#include <set>
#include "simulation/simulation.h"
#include "simulation/start_entities_data.h"
#include <cstring>
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
	// 5b. Conquest Structures: a placed foundation is a Structure (special/filter/foundation),
	// so it keeps its owner in.
	{
		Simulation *sim = fresh(VICTORY_CONQUEST_STRUCTURES);
		Simulation::Building *keep = nullptr;
		for (auto &b : sim->buildings_)
			if (b.team == 1) {
				if (!keep) keep = &b;
				else b.hp = 0.0f;
			}
		CHECK(keep != nullptr);
		keep->build_progress = 0.0f;
		keep->hp = 1.0f;
		sim->record_.buildings_destroyed++;
		run(*sim, 1.0f);
		CHECK(!sim->is_victory() && !sim->is_defeated(1));
		keep->hp = 0.0f;
		run(*sim, 1.0f);
		CHECK(sim->is_victory());
		delete sim;
	}
	// 5c. The last two enemies falling on one tick: players fall one at a time and the first
	// defeat hands the other the win (EndGameManager after each SetState), not a draw.
	{
		Simulation *sim = fresh(VICTORY_CONQUEST_UNITS);
		wipe(*sim, 0, true, false);
		wipe(*sim, 1, true, false);
		run(*sim, 1.0f);
		CHECK(sim->is_game_over() && sim->is_defeated(0) != sim->is_defeated(1));
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
		// Regicide.js: only the spawned hero counts; a second hero the enemy trained
		// doesn't keep it in the game.
		sim->spawn_unit(u.pos_x[enemy_hero] + 6.0f, u.pos_z[enemy_hero], 1, UNIT_HERO);
		kill_unit(*sim, enemy_hero);
		run(*sim, 1.0f);
		CHECK(sim->is_victory());
		delete sim;
	}
	// 7b. ConquestCritical (Conquest*.js class filters): towers, fields, traders and an
	// unfinished barracks don't keep a player in a Conquest match; a house does. The partial
	// conquests count every unit or every structure of their own kind.
	for (int k = 0; k < 4; ++k) {
		const VictoryCondition vc[4] = {VICTORY_CONQUEST, VICTORY_CONQUEST, VICTORY_CONQUEST_STRUCTURES, VICTORY_CONQUEST_UNITS};
		Simulation *sim = fresh(vc[k]);
		run(*sim, 0.05f);
		wipe(*sim, 1, true, true);
		sim->stats_[1].units_lost[STAT_UNIT_TOTAL]++; // the wipe stands for losses in battle
		if (k != 3) {
			sim->spawn_building(30, 30, BLD_TOWER, 1, 1.0f);
			sim->spawn_building(40, 30, BLD_FIELD, 1, 1.0f);
			sim->spawn_building(30, 40, BLD_BARRACKS, 1, 0.5f);
		}
		if (k != 2) sim->spawn_unit(35, 35, 1, UNIT_TRADER);
		if (k == 1) sim->spawn_building(40, 40, BLD_HOUSE, 1, 1.0f);
		run(*sim, 1.0f);
		const bool want_out = k == 0;
		if (sim->is_victory() != want_out) std::printf("  conquest-critical case %d: player 1 %s\n", k, want_out ? "should be out" : "should stay");
		CHECK(sim->is_victory() == want_out);
		delete sim;
	}
	// 7c. The playerstate chat lines (0 A.D. messages.js "defeat"/"won"): each defeat is
	// reported once with its reason, then the winners.
	{
		using N = Simulation::SessionNotice;
		Simulation *sim = fresh(VICTORY_CONQUEST);
		run(*sim, 0.05f);
		(void)sim->take_session_notices();
		wipe(*sim, 1, true, true);
		run(*sim, 1.0f);
		auto ns = sim->take_session_notices();
		bool out = false, won = false;
		for (const auto &n : ns) {
			out |= n.kind == N::DEFEATED && n.player == 1 && n.tech == N::END_CONQUEST;
			won |= n.kind == N::WON && n.other == 1 && n.tech == N::END_LAST_ALIVE;
		}
		CHECK(out && won);
		delete sim;
		// Regicide.js picks from every Hero template of the civ, those only maps place too
		// (Han: Liu Bang, Han Xin, Wei Qing on foot as well as mounted).
		{
			bool map_only = false, trained = false;
			for (uint32_t seed = 1; seed <= 40 && !(map_only && trained); ++seed) {
				auto *hs = new Simulation();
				hs->set_sim_seed(seed);
				hs->set_team_civ(0, CIV_HAN);
				hs->set_victory_condition(VICTORY_REGICIDE);
				hs->set_waves_enabled(false);
				hs->spawn_initial();
				run(*hs, 0.05f);
				const int32_t u = hs->lookup_unit(hs->regicide_hero(0));
				if (u >= 0) {
					const uint16_t t = hs->units_.tmpl[u];
					CHECK(t < CIV_UNIT_TEMPLATE_COUNT && civ_unit_template(t).civ == CIV_HAN && (civ_unit_template(t).flags & ROSTER_HERO));
					if (t < CIV_UNIT_TEMPLATE_COUNT) (civ_unit_template(t).flags & ROSTER_MAP_ONLY ? map_only : trained) = true;
				}
				delete hs;
			}
			CHECK(map_only && trained);
		}
		sim = fresh(VICTORY_REGICIDE);
		run(*sim, 0.05f);
		(void)sim->take_session_notices();
		const int32_t h = sim->lookup_unit(sim->regicide_hero(1));
		CHECK(h >= 0);
		if (h >= 0) kill_unit(*sim, (size_t)h);
		run(*sim, 1.0f);
		ns = sim->take_session_notices();
		CHECK(!ns.empty() && ns[0].kind == N::DEFEATED && ns[0].player == 1 && ns[0].tech == N::END_HERO);
		delete sim;
		sim = fresh(VICTORY_CONQUEST);
		sim->set_player_active(2, true, false);
		run(*sim, 0.05f);
		(void)sim->take_session_notices();
		CHECK(sim->cmd_resign(1));
		ns = sim->take_session_notices();
		CHECK(ns.size() == 1 && ns[0].kind == N::DEFEATED && ns[0].player == 1 && ns[0].tech == N::END_RESIGNED);
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
		// The centre cell: a circular map keeps its off-world corners unseen even when revealed.
		CHECK(sim->fow_grid(0)[(size_t)(sim->fow_res() / 2) * (size_t)sim->fow_res() + (size_t)(sim->fow_res() / 2)] == Simulation::FOW_VISIBLE);
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
		run(*sim, 0.05f); // built after the start, so its countdown runs
		sim->spawn_building(-20, 20, BLD_WONDER, 2, 1.0f);
		run(*sim, 2.0f);
		CHECK(sim->is_wonder_active());
		for (auto &b : sim->buildings_) if (b.type == BLD_WONDER) b.wonder_hold = 599.9f;
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
		// CeasefireManager.js: "Set every enemy (except gaia) to neutral", so Gaia still fights.
		CHECK(sim.can_attack(0, GAIA_PLAYER) && sim.can_attack(GAIA_PLAYER, 1));
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
		CHECK(q.behs[0] == 0 && q.behs[1] == 3 && q.behs[2] == 1);
		CHECK(q.colors[0] == 23 && q.colors[1] == 0 && q.colors[2] == 10 && q.colors[3] == 3);
		CHECK(q.nomad == 1 && q.regicide_garrison == 1 && q.no_treasures == 1);
		CHECK(q.names[0] == "Stan; the=Great, 100%" && q.names[1] == "Ptolemy Philadelphus" && q.names[2].empty() && q.names[3].empty());
		// The last biome (Nubia, BIOME_COUNT) survives the setup string too.
		p.biome = BIOME_NUBIA + 1;
		CHECK(parse_game_start_full(format_game_start_full(p), q) && q.biome == BIOME_NUBIA + 1);
		GameSetupParams longname; longname.names[0] = std::string(60, 'x'); longname.slot_count = 1;
		CHECK(parse_game_start_full(format_game_start_full(longname), q) && q.names[0].size() == 48);
		GameSetupParams d;
		CHECK(d.wonder_min == 20 && d.relic_min == 20 && d.relics == 2); // 0 A.D.'s defaults
	}
	// 10b. WonderVictory.js counts from OwnershipChanged, which the map's own wonders never
	// send (the scripts load after them): a starting wonder has no countdown, a diplomacy
	// change doesn't start one, and it counts once it changes hands.
	{
		Simulation *sim = fresh(VICTORY_WONDER);
		sim->set_wonder_duration(30.0f);
		const uint32_t tc = sim->spawn_building(-20, 34, BLD_TOWN_CENTER, 0, 1.0f); // territory: no decay to Gaia
		const uint32_t w = sim->spawn_building(-20, 20, BLD_WONDER, 0, 1.0f);
		run(*sim, 40.0f);
		CHECK(!sim->is_game_over() && !sim->is_wonder_active() && sim->wonder_hold(0) == 0.0f);
		for (uint8_t p = 0; p < 3; ++p) sim->set_player_active(p, true, false);
		sim->set_mutual_ally(0, 2);
		run(*sim, 1.0f);
		CHECK(sim->wonder_hold(0) == 0.0f);
		for (auto &b : sim->buildings_) // captured, with the civic centre beside it
			if (b.id == w || b.id == tc) {
				b.team = 1;
				for (auto &c : b.capture_points_by_team) c = 0.0f;
				b.capture_points_by_team[1] = b.max_capture_points;
			}
		run(*sim, 5.0f);
		CHECK(sim->wonder_hold(1) > 4.0f);
		delete sim;
	}
	// 11. Wonder hold time and relic count from game setup.
	{
		Simulation *sim = fresh(VICTORY_WONDER);
		sim->set_wonder_duration(120.0f);
		run(*sim, 0.05f);
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
		// Trees aside: the forests are drawn after the bases, so their count moves with the rng.
		auto non_wood = [&]() {
			size_t n = 0;
			for (size_t i = 0; i < sim.resources().count; ++i) n += sim.resources().type[i] != RES_WOOD;
			return n;
		};
		const size_t units = sim.unit_count(), resources = non_wood();
		CHECK(structures > 0);
		sim.set_nomad(true);
		sim.generate_random_map(MAP_MAINLAND, 5, 2, 64.0f);
		int left = 0;
		for (const auto &b : sim.buildings()) left += b.team < MAX_PLAYERS;
		CHECK(left == 0 && sim.unit_count() == units);
		// placePlayerBase places nothing (no base berries, woodline or mines), and
		// placePlayersNomad groups each player's units 1-3 tiles round one spot.
		CHECK(non_wood() < resources);
		{
			const auto uv = sim.units();
			for (uint8_t p = 0; p < 2; ++p) {
				float cx = 0.0f, cz = 0.0f;
				int n = 0;
				for (size_t i = 0; i < uv.count; ++i)
					if (uv.alive[i] && uv.team[i] == p) { cx += uv.pos_x[i]; cz += uv.pos_z[i]; ++n; }
				CHECK(n > 0);
				cx /= (float)n; cz /= (float)n;
				for (size_t i = 0; i < uv.count; ++i)
					if (uv.alive[i] && uv.team[i] == p) CHECK(std::hypot(uv.pos_x[i] - cx, uv.pos_z[i] - cz) < 6.5f);
			}
		}
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

	// 15a. fortress.js places the StartEntities infantry a second time at 8 tiles.
	{
		Simulation main_sim, fort;
		main_sim.generate_random_map(MAP_MAINLAND, 5, 2, 64.0f);
		fort.generate_random_map(MAP_FORTRESS, 5, 2, 64.0f);
		int infantry = 0;
		for (int e = 0; e < START_ENTITY_COUNT; ++e)
			for (uint8_t p = 0; p < 2; ++p)
				if (start_entity_table()[e].civ == fort.team_civ(p) && std::strstr(start_entity_table()[e].tmpl, "infantry"))
					infantry += start_entity_table()[e].count;
		CHECK(infantry > 0 && fort.unit_count() == main_sim.unit_count() + (size_t)infantry);
	}

	// 15b. Nomad with Disable Treasures: Setup.js destroys every treasure, the Nomad ones too.
	{
		Simulation sim;
		sim.set_nomad(true);
		sim.set_disable_treasures(true);
		sim.generate_random_map(MAP_MAINLAND, 5, 2, 64.0f);
		sim.set_player_active(0, true, false);
		sim.set_player_active(1, true, true);
		sim.set_starting_resources(0, 0, 0, 0);
		sim.set_victory_condition(VICTORY_CONQUEST);
		sim.tick(0.05f);
		CHECK(sim.treasures().empty());
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
		// special/filter/ungarrisonable disables Garrisonable only: Turretable stays, so the
		// hero mans an outpost's turret point either way.
		// (An archer stands in as the hero, as the map's random hero may not be Turretable.)
		const uint32_t post = sim.spawn_building_world(10.0f, 10.0f, BLD_OUTPOST, 0);
		sim.regicide_hero_[0] = sim.spawn_unit(8.0f, 8.0f, 0, UNIT_ARCHER);
		const int32_t h = sim.lookup_unit(sim.regicide_hero_[0]);
		CHECK(h >= 0 && sim.unit_turretable((size_t)h) && sim.unit_garrisonable((size_t)h) == (on != 0));
		for (const auto &b : sim.buildings_)
			if (b.id == post) CHECK(sim.structure_accepts_turret(b, (size_t)h) && sim.structure_accepts_garrison(b, (size_t)h));
	}

	// 19. Regicide.js SpawnRegicideHero: a random hero of the player's civ, stepping out beside
	// the Civic Centre (not inside it); a ship start boards it, or without Regicide Garrison
	// puts it on land by the Gaia entity nearest the ship.
	{
		std::set<uint16_t> picked;
		for (uint32_t seed = 1; seed <= 8; ++seed) {
			Simulation sim;
			sim.generate_random_map(MAP_MAINLAND, 5, 2, 64.0f);
			sim.set_sim_seed(seed); // the match seed (the map's own seed reseeds on generation)
			sim.set_victory_condition(VICTORY_REGICIDE);
			sim.tick(0.05f);
			const uint32_t hero = sim.regicide_hero(0);
			CHECK(hero != 0);
			const uint16_t t = sim.unit_template(hero);
			CHECK(t < CIV_UNIT_TEMPLATE_COUNT && (civ_unit_template(t).flags & ROSTER_HERO) &&
					civ_unit_template(t).civ == sim.player_civ(0));
			picked.insert(t);
			const int32_t h = sim.lookup_unit(hero);
			for (const auto &b : sim.buildings())
				if (b.team == 0 && is_civic_centre(b.type)) {
					const auto uv = sim.units();
					CHECK(std::fabs(uv.pos_x[h] - b.x) > b.half_size || std::fabs(uv.pos_z[h] - b.z) > b.half_size);
					break;
				}
		}
		CHECK(picked.size() >= 2);
		for (int on = 0; on < 2; ++on) {
			Simulation sim;
			sim.set_player_active(0, true, false);
			const uint32_t ship = sim.spawn_unit(0.0f, 0.0f, 0, UNIT_TRIREME);
			sim.spawn_resource_world(12.0f, 0.0f, RES_WOOD);
			sim.spawn_resource_world(40.0f, 0.0f, RES_WOOD);
			sim.set_victory_condition(VICTORY_REGICIDE);
			sim.set_regicide_garrison(on != 0);
			sim.tick(0.05f);
			const uint32_t hero = sim.regicide_hero(0);
			CHECK(hero != 0);
			const auto *aboard = sim.get_transport_passengers(ship);
			const bool on_ship = aboard && std::find(aboard->begin(), aboard->end(), hero) != aboard->end();
			CHECK(on_ship == (on != 0));
			if (!on) {
				const int32_t h = sim.lookup_unit(hero);
				const auto uv = sim.units();
				CHECK(std::fabs(uv.pos_x[h] - 12.0f) < 3.0f && std::fabs(uv.pos_z[h]) < 3.0f);
			}
		}
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
		// A scenario's per-player PopulationLimit (Miletus P1 1000, Death Canyon's attackers 1):
		// that player's own cap, the others keep the setup's; it beats World Population for
		// that player, survives a save and reset() clears it.
		sim.set_player_pop_cap(1, 1000);
		sim.set_player_pop_cap(2, 1);
		CHECK(sim.pop_cap(0) == 300 && sim.pop_cap(1) == 1000 && sim.pop_cap(2) == 1 && sim.pop_limit(2) == 1);
		CHECK(sim.pop_max(2) <= 1);
		sim.set_world_pop_cap(600);
		CHECK(sim.pop_cap(1) == 1000 && sim.pop_cap(0) == 200);
		sim.set_pop_cap(300);
		{
			Simulation *a = fresh(VICTORY_CONQUEST), *loaded = fresh(VICTORY_CONQUEST);
			a->set_player_pop_cap(1, 1000);
			a->set_player_pop_cap(2, 1);
			const char *ppath = "test_victory_popcap.sav";
			CHECK(a->save_state(ppath));
			CHECK(loaded->load_state(ppath) && loaded->player_pop_cap(1) == 1000 && loaded->pop_cap(2) == 1 &&
					loaded->pop_cap(0) == a->pop_cap(0));
			std::remove(ppath);
			a->reset();
			CHECK(a->player_pop_cap(1) == 0 && a->pop_cap(1) == a->pop_cap());
			delete a;
			delete loaded;
		}
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
		(void)sim.take_session_notices();
		CHECK(sim.cmd_cheat(0, Simulation::CHEAT_DEFEAT_PLAYER, 1, 0, nullptr, 0) && sim.is_defeated(1));
		{ // Cheat.js: "%(player)s has been defeated (cheat)."
			bool noted = false;
			for (const Simulation::SessionNotice &n : sim.take_session_notices())
				noted |= n.kind == Simulation::SessionNotice::DEFEATED && n.player == 1 && n.tech == Simulation::SessionNotice::END_CHEAT;
			CHECK(noted);
		}
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
		sim.tick_los();
		sim.tick_fow();
		CHECK(sim.fow_state_at(ex + 6.0f, ez + 6.0f, 0) != Simulation::FOW_VISIBLE && !sim.los_visible(0, ex + 6.0f, ez + 6.0f));
		CHECK(sim.cmd_bribe(0, 1) == Simulation::BRIBE_OK && sim.player_resource(0, RES_GOLD) == 375);
		CHECK(sim.spies().size() == 1 && sim.spies()[0].unit_id == trader && std::fabs(sim.spies()[0].remaining - 15.0f) < 1e-4f);
		sim.tick_los();
		sim.tick_fow();
		CHECK(sim.fow_state_at(ex + 6.0f, ez + 6.0f, 0) == Simulation::FOW_VISIBLE);
		CHECK(sim.los_visible(0, ex + 6.0f, ez + 6.0f)); // the briber's own LOS too (targeting, mirages)
		// The only trader is already spied, and 375 metal can't pay for another.
		CHECK(sim.cmd_bribe(0, 1) == Simulation::BRIBE_NO_UNITS);
		const char *path = "test_victory_spies.sav";
		CHECK(sim.save_state(path));
		Simulation back_sim;
		CHECK(back_sim.load_state(path) && back_sim.spies().size() == 1 && back_sim.spies()[0].unit_id == trader);
		CHECK(back_sim.los_visible(0, ex + 6.0f, ez + 6.0f)); // the spy's circle is rebuilt on load
		std::remove(path);
		sim.tick_spies(15.0f);
		CHECK(sim.spies().empty());
		sim.tick_los();
		CHECK(!sim.los_visible(0, ex + 6.0f, ez + 6.0f)); // the spy's vision ends with it
		// VisionSharing.js AddSpy: Duration x 60 / the unit's modified vision, so Exploration's
		// +20% trader vision shortens the spy to 12.5 s.
		sim.complete_research(1, TECH_EXPLORATION);
		sim.players_[0].res[RES_GOLD] = 1000.0f;
		CHECK(sim.cmd_bribe(0, 1) == Simulation::BRIBE_OK && sim.spies().size() == 1 &&
		      std::fabs(sim.spies()[0].remaining - 12.5f) < 1e-3f);
		sim.tick_spies(12.5f);
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
		// Diplomacy.js mutual worsening: war on the old teammate is war back.
		CHECK(!sim.is_ally(0, 1) && sim.is_hostile(0, 1) && sim.is_hostile(1, 0));
		CHECK(sim.is_ally(0, 3) && sim.is_ally(1, 3) && sim.same_team(1, 3) && !sim.same_team(0, 1));
		CHECK(sim.player_team(1) == 1 && sim.player_team(0) == 0);
		CHECK(sim.cmd_set_diplomacy(0, 1, Simulation::DIPLO_ALLY) && !sim.is_ally(0, 1)); // allying is one-sided
		CHECK(sim.cmd_set_diplomacy(1, 0, Simulation::DIPLO_ALLY) && sim.is_ally(0, 1));
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
		// 0.28 TechnologyManager StartedResearch/StoppedResearch: a phase announces "started" when the
		// queue starts it and "aborted" only if it had started, so a cancel before any tick says nothing.
		CHECK(sim.cmd_research(1, tc1, TECH_PHASE_TOWN));
		CHECK(sim.cmd_cancel_train(1, tc1, 0));
		ns = sim.take_session_notices();
		CHECK(std::none_of(ns.begin(), ns.end(), [](const auto &n) { return n.kind == N::PHASE_STARTED || n.kind == N::PHASE_ABORTED; }));
		for (int r = 0; r < RES_COUNT; ++r) sim.players_[1].res[r] = 5000.0f;
		CHECK(sim.cmd_research(1, tc1, TECH_PHASE_TOWN));
		for (int i = 0; i < 25; ++i) sim.tick(0.05f); // the queue's first second (ProgressInterval)
		CHECK(sim.cmd_cancel_train(1, tc1, 0));
		ns = sim.take_session_notices();
		std::erase_if(ns, [](const auto &n) { return n.kind != N::PHASE_STARTED && n.kind != N::PHASE_ABORTED; });
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
		// Trigger Difficulty rides NMT_GAME_START as trigdiff= (left out at Medium).
		CHECK(format_game_start_full(gp).find("trigdiff=") == std::string::npos);
		gp.trigger_difficulty = 5;
		CHECK(parse_game_start_full(format_game_start_full(gp), back) && back.trigger_difficulty == 5);
		gp.trigger_difficulty = 3;
	}

	// 28. Daytime (danubius.json Day/Night, polar_sea.json Dawn/Daylight): the chosen
	// option, else the map's own pick from the seed (Danubius mostly by day), none
	// elsewhere; the map itself is the same by day and by night; daytime= on the wire.
	{
		auto daytime = [](MapType map, uint8_t choice, uint32_t seed, float *terrain = nullptr) {
			static Simulation sim;
			sim.reset();
			sim.set_map_daytime_choice(choice);
			sim.generate_random_map(map, seed, 2, 64.0f);
			if (terrain) {
				*terrain = 0.0f;
				for (float x = -60.0f; x <= 60.0f; x += 4.0f)
					for (float z = -60.0f; z <= 60.0f; z += 4.0f) *terrain += sim.get_terrain_height(x, z);
				*terrain += (float)sim.units_.count * 1000.0f;
			}
			return sim.map_daytime();
		};
		CHECK(daytime(MAP_DANUBIUS, 2, 5) == 2 && daytime(MAP_POLAR_SEA, 1, 5) == 1);
		CHECK(daytime(MAP_MAINLAND, 2, 5) == 0);
		int days = 0;
		for (uint32_t seed = 1; seed <= 30; ++seed) {
			const uint8_t d = daytime(MAP_DANUBIUS, 0, seed);
			CHECK(d == 1 || d == 2);
			days += d == 1;
		}
		CHECK(days > 12 && days < 30);
		float h_day = 0.0f, h_night = 1.0f;
		daytime(MAP_DANUBIUS, 1, 7, &h_day);
		daytime(MAP_DANUBIUS, 2, 7, &h_night);
		CHECK(h_day == h_night);
		GameSetupParams gp, back;
		CHECK(format_game_start_full(gp).find("daytime=") == std::string::npos);
		gp.daytime = 2;
		CHECK(parse_game_start_full(format_game_start_full(gp), back) && back.daytime == 2);
	}
	// 29. Combined victory conditions (0.28 VictoryConditions.js): codes 0..6 keep their
	// meaning, any other set is VICTORY_COMBINED | mask; each condition defeats on its own;
	// Conquest Civic Centers needs a civic centre lost first; victory= carries the code.
	{
		for (uint8_t c = 0; c < VICTORY_COUNT; ++c) CHECK(victory_code(victory_mask(c)) == c);
		CHECK(victory_mask(VICTORY_WONDER) == (VK_CONQUEST | VK_WONDER));
		CHECK(victory_code(VK_CONQUEST | VK_WONDER | VK_RELIC) == (VICTORY_COMBINED | VK_CONQUEST | VK_WONDER | VK_RELIC));
		CHECK(victory_code(0) == VICTORY_ENDLESS && valid_victory_code(0x85) && !valid_victory_code(7));
		CHECK((victory_excludes(VK_CONQUEST) & VK_CONQUEST_UNITS) && !(victory_excludes(VK_WONDER) & VK_RELIC));
		auto combined = [](uint8_t mask) {
			Simulation *sim = fresh(VICTORY_CONQUEST);
			sim->set_victory_conditions(mask);
			return sim;
		};
		// Units + Structures: razing the structures is enough, as with Structures alone.
		Simulation *sim = combined(VK_CONQUEST_UNITS | VK_CONQUEST_STRUCTURES);
		CHECK(sim->victory_condition() == (VICTORY_COMBINED | VK_CONQUEST_UNITS | VK_CONQUEST_STRUCTURES));
		wipe(*sim, 1, false, true);
		run(*sim, 1.0f);
		CHECK(sim->is_victory());
		delete sim;
		// Wonder and Relic together both run.
		sim = combined(VK_CONQUEST | VK_WONDER | VK_RELIC);
		CHECK(sim->has_victory(VK_WONDER) && sim->has_victory(VK_RELIC) && !sim->has_victory(VK_REGICIDE));
		delete sim;
		// Regicide alone: heroes spawn; the enemy losing its structures doesn't end it.
		sim = combined(VK_REGICIDE);
		run(*sim, 0.05f);
		wipe(*sim, 1, false, true);
		run(*sim, 1.0f);
		CHECK(!sim->is_game_over());
		for (size_t i = 0; i < sim->units_.count; ++i)
			if (sim->units_.alive[i] && sim->units_.unit_type[i] == UNIT_HERO && sim->units_.team[i] == 1) kill_unit(*sim, i);
		run(*sim, 1.0f);
		CHECK(sim->is_victory());
		delete sim;
		// Conquest Civic Centers: losing the last finished civic centre defeats; a player
		// that never had one (its civic centre removed outright) is still in.
		sim = combined(VK_CONQUEST_CIVIC_CENTRES);
		std::erase_if(sim->buildings_, [](const auto &b) { return b.team == 1 && is_civic_centre(b.type); });
		run(*sim, 1.0f);
		CHECK(!sim->is_game_over());
		delete sim;
		sim = combined(VK_CONQUEST_CIVIC_CENTRES);
		for (auto &b : sim->buildings_)
			if (b.team == 1 && is_civic_centre(b.type)) sim->mark_building_wreck(b, 0);
		sim->record_.buildings_destroyed++;
		run(*sim, 1.0f);
		CHECK(sim->is_victory());
		delete sim;
		// Nomad (ConquestCommon.js: a query fires only when its last entity leaves): with no
		// structures to start, Conquest Structures keeps the players in on their units, and
		// "ConquestCritical Structure" defeats once the critical units are gone too.
		auto nomad = [&](uint8_t mask) {
			Simulation *n = combined(mask);
			n->set_nomad(true);
			std::erase_if(n->buildings_, [](const auto &b) { return b.team < MAX_PLAYERS; });
			return n;
		};
		auto reason = [](Simulation &n, uint8_t player) {
			int why = -1;
			for (const auto &note : n.take_session_notices())
				if (note.kind == Simulation::SessionNotice::DEFEATED && note.player == player) why = note.tech;
			return why;
		};
		sim = nomad(VK_CONQUEST_STRUCTURES);
		run(*sim, 1.0f);
		CHECK(!sim->is_game_over() && sim->defeated_mask() == 0);
		wipe(*sim, 1, true, false);
		run(*sim, 1.0f);
		CHECK(sim->is_victory() && reason(*sim, 1) == Simulation::SessionNotice::END_STRUCTURES_CRITICAL);
		// Player.js Win: the winners watch on with the map revealed, and the world keeps going.
		const auto before = sim->ticks_;
		for (int i = 0; i < 20; ++i) sim->tick(0.05f);
		CHECK(sim->ticks_ == before + 20 && sim->is_victory());
		delete sim;
		// Conquest Civic Centers: a Nomad player wiped out before building one is out
		// ("lost all civic centers and critical units and structures").
		sim = nomad(VK_CONQUEST_CIVIC_CENTRES);
		run(*sim, 1.0f);
		CHECK(!sim->is_game_over());
		wipe(*sim, 1, true, false);
		run(*sim, 1.0f);
		CHECK(sim->is_victory() && reason(*sim, 1) == Simulation::SessionNotice::END_CIVIC_CENTRES_CRITICAL);
		delete sim;
		GameSetupParams gp, back;
		gp.victory = VICTORY_COMBINED | VK_CONQUEST | VK_WONDER | VK_RELIC;
		CHECK(parse_game_start_full(format_game_start_full(gp), back) && back.victory == gp.victory);
		GameSetupParams bad, back2;
		bad.victory = 7;
		parse_game_start_full(format_game_start_full(bad), back2);
		CHECK(back2.victory == 0);
	}

	// 29. Team Population (0 A.D. 0.28 PopulationCapManager "team"): each team's cap
	// is split by its members still in the game, a teamless player keeps all of it,
	// a defeat raises the teammates' share, the setting survives pop_cap_setting()
	// and peers get it as teampop=.
	{
		Simulation sim;
		for (uint8_t p = 0; p < 4; ++p) sim.set_player_active(p, true, false);
		const uint8_t teams[MAX_PLAYERS] = {1, 1, 1, 0, 0, 0, 0, 0};
		sim.set_setup_teams(teams);
		sim.set_team_pop_cap(400);
		CHECK(sim.team_pop_cap() == 400 && sim.world_pop_cap() == 0);
		CHECK(sim.pop_cap(0) == 133 && sim.pop_cap(2) == 133 && sim.pop_cap(3) == 400);
		CHECK(sim.pop_max(3) <= 400 && sim.pop_max(0) <= 133);
		sim.defeated_mask_ |= 1u << 2;
		CHECK(sim.pop_cap(0) == 200 && sim.pop_cap(1) == 200 && sim.pop_cap(3) == 400);
		const int setting = sim.pop_cap_setting();
		Simulation copy;
		copy.set_pop_cap_setting(setting);
		CHECK(copy.team_pop_cap() == 400 && copy.world_pop_cap() == 0);
		copy.set_pop_cap_setting(-600);
		CHECK(copy.world_pop_cap() == 600 && copy.team_pop_cap() == 0);
		copy.set_pop_cap_setting(250);
		CHECK(copy.pop_cap(0) == 250 && copy.team_pop_cap() == 0);
		sim.set_team_pop_cap(99999);
		CHECK(sim.team_pop_cap() == Simulation::MAX_TEAM_POP);
		GameSetupParams gp, back;
		gp.team_pop = 600;
		CHECK(parse_game_start_full(format_game_start_full(gp), back) && back.team_pop == 600);
		GameSetupParams none;
		CHECK(format_game_start_full(none).find("teampop=") == std::string::npos);
		CHECK(parse_game_start_full(format_game_start_full(none), back) && back.team_pop == 0);
	}

	// 30. Allied View (0 A.D. InitGame.js AllyView): every player starts with Diplomacy's
	// SharedLosTech (Cartography) researched, so allies see what each other see from the
	// first turn; the setting rides the save's setup flags and the ally= wire key.
	{
		Simulation sim;
		for (uint8_t p = 0; p < 2; ++p) sim.set_player_active(p, true, false);
		const uint8_t teams[MAX_PLAYERS] = {1, 1};
		sim.set_setup_teams(teams);
		sim.apply_team_diplomacy();
		sim.tick(0.05f);
		CHECK(!sim.ally_view() && !sim.shares_los(0, 1));
		Simulation av;
		av.generate_random_map(MAP_MAINLAND, 5, 2, 64.0f);
		for (uint8_t p = 0; p < 2; ++p) av.set_player_active(p, true, false);
		av.set_setup_teams(teams);
		av.apply_team_diplomacy();
		av.set_ally_view(true);
		av.tick(0.05f);
		CHECK(av.ally_view() && av.shares_los(0, 1) && av.shares_los(1, 0));
		CHECK(av.player(0).has_tech(TECH_UNLOCK_SHARED_LOS) && av.player(1).has_tech(TECH_UNLOCK_SHARED_LOS));
		const char *apath = "test_victory_ally_view.sav";
		CHECK(av.save_state(apath));
		{
			Simulation loaded;
			CHECK(loaded.load_state(apath));
			CHECK(loaded.ally_view());
			CHECK(loaded.shares_los(0, 1));
		}
		std::remove(apath);
		GameSetupParams gp, back;
		gp.ally = 1;
		CHECK(parse_game_start_full(format_game_start_full(gp), back) && back.ally == 1);
		GameSetupParams none;
		CHECK(format_game_start_full(none).find("ally=") == std::string::npos);
	}

	// 20. CaptureTheRelic.js: a relic changing hands, even within one side, restarts the
	// countdown; a diplomacy change restarts it only when the winners change; the winners are
	// every player mutually allied to all relic owners; nothing runs while Gaia holds one.
	{
		Simulation *sim = fresh(VICTORY_RELIC);
		for (uint8_t p = 0; p < 3; ++p) sim->set_player_active(p, true, false);
		sim->set_relic_count(2);
		sim->set_relic_duration(60.0f);
		sim->spawn_unit(-30.0f, 30.0f, 2, UNIT_VILLAGER);
		sim->set_mutual_ally(0, 2);
		run(*sim, 0.1f);
		CHECK(sim->relics().size() == 2);
		auto give = [&](size_t k, uint8_t p) { sim->units_.team[sim->lookup_unit(sim->relics()[k].id)] = p; };
		CHECK(sim->relic_winners() == 0 && sim->relic_victory_timer() == 0.0f); // Gaia holds them
		give(0, 0);
		run(*sim, 5.0f);
		CHECK(sim->relic_victory_timer() == 0.0f); // one still Gaia's
		give(1, 0);
		run(*sim, 30.0f);
		CHECK(sim->relic_countdown_players() == 0b101 && sim->relic_victory_timer() > 29.0f);
		give(1, 2); // to an ally: same winners, but a relic changed hands
		run(*sim, 1.0f);
		CHECK(sim->relic_countdown_players() == 0b101 && sim->relic_victory_timer() < 1.5f);
		run(*sim, 20.0f);
		const float before = sim->relic_victory_timer();
		sim->set_diplomacy(0, 1, Simulation::DIPLO_NEUTRAL); // one-sided: winners unchanged, no restart
		run(*sim, 1.0f);
		CHECK(sim->relic_victory_timer() > before);
		sim->set_mutual_ally(0, 1);
		sim->set_mutual_ally(1, 2); // player 1 joins the winners: restart
		run(*sim, 1.0f);
		CHECK(sim->relic_countdown_players() == 0b111 && sim->relic_victory_timer() < 1.5f);
		sim->set_diplomacy(0, 2, Simulation::DIPLO_ENEMY); // the owners fall out: no countdown
		run(*sim, 1.0f);
		CHECK(sim->relic_winners() == 0 && sim->relic_victory_timer() == 0.0f);
		sim->set_mutual_ally(0, 2);
		run(*sim, 30.0f);
		CHECK(!sim->is_game_over() && sim->relic_victory_timer() > 29.0f);
		// A save mid-countdown keeps the winners, so the loaded game does not restart.
		const char *rpath = "test_victory_relic.sav";
		CHECK(sim->save_state(rpath));
		{
			Simulation loaded;
			CHECK(loaded.load_state(rpath));
			CHECK(loaded.relic_countdown_players() == 0b111);
			const float t0 = loaded.relic_victory_timer();
			loaded.tick(0.05f);
			CHECK(loaded.relic_victory_timer() > t0);
		}
		std::remove(rpath);
		run(*sim, 31.0f);
		CHECK(sim->is_game_over() && sim->winner_mask_ == 0b111); // player 1 holds no relic but wins
		delete sim;
	}
	{
		// Last Man Standing: two owners never count down, however allied.
		Simulation *sim = fresh(VICTORY_RELIC);
		for (uint8_t p = 0; p < 2; ++p) sim->set_player_active(p, true, false);
		sim->set_last_man_standing(true);
		sim->set_relic_count(2);
		run(*sim, 0.1f);
		sim->set_mutual_ally(0, 1);
		sim->units_.team[sim->lookup_unit(sim->relics()[0].id)] = 0;
		sim->units_.team[sim->lookup_unit(sim->relics()[1].id)] = 1;
		run(*sim, 2.0f);
		CHECK(sim->relic_winners() == 0 && sim->relic_victory_timer() == 0.0f);
		sim->units_.team[sim->lookup_unit(sim->relics()[1].id)] = 0;
		run(*sim, 2.0f);
		CHECK(sim->relic_countdown_players() == 0b1 && sim->relic_victory_timer() > 1.5f);
		delete sim;
	}

	// 21. WonderVictory.js: each wonder keeps its own countdown, so losing one while holding
	// another does not carry the lost one's time over; a change of the owner's mutual allies
	// restarts it; a save keeps each wonder's countdown.
	{
		Simulation *sim = fresh(VICTORY_WONDER);
		for (uint8_t p = 0; p < 3; ++p) sim->set_player_active(p, true, false);
		sim->set_wonder_duration(120.0f);
		sim->spawn_unit(-30.0f, 30.0f, 2, UNIT_VILLAGER);
		sim->spawn_building(-20, 34, BLD_TOWN_CENTER, 0, 1.0f); // territory: no decay to Gaia
		run(*sim, 0.05f);
		const uint32_t w1 = sim->spawn_building(-20, 20, BLD_WONDER, 0, 1.0f);
		run(*sim, 60.0f);
		const uint32_t w2 = sim->spawn_building(-40, 34, BLD_WONDER, 0, 1.0f);
		run(*sim, 20.0f);
		auto wonder = [&](uint32_t id) -> Simulation::Building * {
			for (auto &b : sim->buildings_) if (b.id == id) return &b;
			return nullptr;
		};
		CHECK(wonder(w1)->wonder_hold > 79.0f && wonder(w2)->wonder_hold < 21.0f);
		CHECK(sim->wonder_hold(0) > 79.0f);
		wonder(w1)->hp = 0.0f; // the old wonder falls: the new one's 20 s count, not 80 s
		run(*sim, 1.0f);
		CHECK(sim->wonder_hold(0) < 22.0f && !sim->is_game_over());
		sim->set_mutual_ally(0, 2); // a new ally: the countdown restarts
		run(*sim, 1.0f);
		CHECK(wonder(w2)->wonder_hold < 1.5f && wonder(w2)->wonder_allies == 0b100);
		run(*sim, 30.0f);
		sim->set_diplomacy(1, 0, Simulation::DIPLO_ALLY); // one-sided: allies unchanged, no restart
		run(*sim, 1.0f);
		CHECK(wonder(w2)->wonder_hold > 30.0f);
		const char *wpath = "test_victory_wonder.sav";
		CHECK(sim->save_state(wpath));
		{
			Simulation loaded;
			CHECK(loaded.load_state(wpath));
			float hold = -1.0f;
			for (const auto &b : loaded.buildings_) if (b.id == w2) hold = b.wonder_hold;
			CHECK(std::fabs(hold - wonder(w2)->wonder_hold) < 0.001f);
		}
		std::remove(wpath);
		run(*sim, 90.0f);
		CHECK(sim->is_game_over() && sim->winner_mask_ == 0b101);
		delete sim;
	}

	if (failures) {
		std::printf("%d check(s) failed\n", failures);
		return 1;
	}
	std::printf("test_victory: all checks passed\n");
	return 0;
}
