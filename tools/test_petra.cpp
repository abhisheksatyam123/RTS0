#include <cstring>
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
//  3. No free income: with no workers Petra earns nothing; difficulty works
//     through InitGame.js gather-rate and build-time multipliers instead.
//  4. Diplomacy (diplomacyManager.js): Petra answers any player's offer to
//     ally (accept, decline, or accept for a tribute), answers a declaration
//     of war in kind, and under Last Man Standing turns on its last ally.
//  5. AI slots: Petra leaves a human slot alone, runs one AI per AI slot, and
//     AIs on the same team never fight each other.
//  6. Attack plan types (attackManager.js / attackPlan.js): wood-rich bases
//     let aggressive AIs rush an undefended structure in the village phase,
//     the first two town-phase plans are "Attack" and later ones "HugeAttack",
//     and cavalry raid an enemy structure inside Petra's territory.
//  7. Each AI slot keeps its own difficulty and behaviour (game setup's
//     per-player AI settings), through its turns and a save and load.
//  8. Hunting (worker.js startHunting): gatherers collect a treasure nearby,
//     food gatherers kill the livestock by their dropsite first, citizen
//     cavalry hunt animals that flee, and no one hunts inside enemy territory.
//  9. First base (startingStrategy.js buildFirstBase): with its civic centre
//     gone and the wood for a new one only in a treasure, Petra collects the
//     treasure and founds a new Town Center where its workers stand.
// 10. Barter and trading goods (tradeManager.js): Petra sells spare food for
//     a resource its queues wait on, and with plenty of everything it sells food
//     for the scarcest finite resource; its trading goods favour what it lacks.
// 11. Kush builds its Blemmye and Noba camps and Temple of Amun, Rome its
//     Temple of Vesta (config.js buildings), and Rome researches its champion
//     unlock and trains champions unless its population is already at the cap
//     (researchWantedTechs unlock_champion).
//  Exit: 0 = PASS, 1 = FAIL.
// The ally check reads the sim's death log, which has no public accessor.
#define private public
#include "simulation/simulation.h"
#include "simulation/entity_limits_data.h"
#include "simulation/petra_chat.h"
#undef private
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
using namespace rtso;

static int g_failures = 0;
#define CHECK(cond, what) do { if (!(cond)) { std::fprintf(stderr, "  [FAIL] %s: %s\n", what, #cond); ++g_failures; } } while (0)

// Every defense army's defenders.
static std::vector<uint32_t> all_defenders(const Simulation &s) {
	std::vector<uint32_t> out;
	for (const PetraDefenseArmy &a : s.petra_defense_armies())
		out.insert(out.end(), a.defender_ids.begin(), a.defender_ids.end());
	return out;
}

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
	// The idle player is truly idle: passive, so its archer doesn't pick a fight with
	// Petra's gatherers passing by (which is not a rush, and they fight back).
	{
		const auto u = s->units();
		std::vector<uint32_t> mine;
		for (size_t i = 0; i < u.count; ++i)
			if (u.alive[i] && u.team[i] == 0) mine.push_back(u.id[i]);
		s->order_set_stance(mine.data(), mine.size(), STANCE_PASSIVE);
	}
	const int start_units = count_units(*s, 0, false);
	bool attacked = false;
	float t = 0.0f;
	for (; t < 300.0f && s->petra_phase() < 2; t += Simulation::FIXED_STEP) {
		s->tick(Simulation::FIXED_STEP);
		for (const auto &p : s->petra_started_attacks())
			if (p.state == Simulation::AttackPlan::WALKING || p.state == Simulation::AttackPlan::ATTACKED) attacked = true;
	}
	std::printf("  arena: %.0fs in village phase, player units %d -> %d, attack launched: %s, rushes allowed %d\n",
	            t, start_units, count_units(*s, 0, false), attacked ? "yes" : "no", s->petra_attack_manager().max_rushes);
	CHECK(!attacked, "no village-phase attack");
	// setRushes: one rush per 3000 wood above 8500 on the base's own land within 360 m; the
	// arena's base has room for one at most, and none goes out in the first 300 s.
	CHECK(s->petra_attack_manager().max_rushes <= 1, "at most one rush on the arena base (setRushes)");
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
	s->set_victory_condition(VICTORY_ENDLESS); // keep playing once the idle player is beaten
	float town_at = -1.0f, city_at = -1.0f;
	int peak_workers = 0, peak_fields = 0;
	// 18 minutes: at 0 A.D.'s gather rates (template_unit_*/ResourceGatherer) the city phase's
	// 750 stone and 750 metal come in around minute 16, as for 0 A.D.'s Petra.
	for (float t = 0.0f; t < 18.0f * 60.0f && !s->is_game_over(); t += Simulation::FIXED_STEP) {
		s->tick(Simulation::FIXED_STEP);
		if (town_at < 0.0f && s->petra_phase() >= 2) town_at = t;
		if (city_at < 0.0f && s->petra_phase() >= 3) city_at = t;
		if ((int)(t * 20.0f) % 200 == 0) {
			int w = 0;
			for (size_t i = 0; i < s->units_.count; ++i) w += s->units_.alive[i] && s->units_.team[i] == 1 && s->petra_is_worker(i);
			if (w > peak_workers) peak_workers = w;
			int f = 0;
			for (const auto &b : s->buildings())
				if (b.team == 1 && b.type == BLD_FIELD && b.build_progress >= 1.0f) ++f;
			if (f > peak_fields) peak_fields = f;
		}
	}
	std::printf("  continental: town phase at %.0fs, city phase at %.0fs, peak workers %d, fields %d\n",
	            town_at, city_at, peak_workers, peak_fields);
	CHECK(town_at > 0.0f && town_at < 13.0f * 60.0f, "town phase within 13 minutes");
	CHECK(city_at > 0.0f && city_at < 18.0f * 60.0f, "city phase within 18 minutes (Town structures built first)");
	CHECK(peak_workers >= 25, "economy grows past 25 workers");
	CHECK(peak_fields >= 2, "farms fields once hunt and berries run low");
}

// Petra fields its civ's roster army: a hero, champions and siege engines once
// it reaches the city phase (attackPlan.js unitStat Hero / Champ* / Siege).
static void test_roster_army() {
	bool any_unpacked_fired = false, any_packed_moved = false;
	for (uint8_t civ : { (uint8_t)CIV_ATHENIANS, (uint8_t)CIV_MACEDONIANS }) {
		auto s = std::make_unique<Simulation>();
		s->set_sim_seed(42);
		s->reset();
		s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
		s->set_team_civ(1, (uint8_t)civ);
		s->set_victory_condition(VICTORY_ENDLESS); // keep playing once the idle player is beaten
		// Garrisoned fortresses (ten more arrows each, BuildingAI) outlast the infantry's first
		// waves, so the siege (trained some ten minutes into the city phase at 0 A.D.'s gather
		// rates) still has something to fire at. They stand on dry land: no land unit wades out
		// to one at sea. The one at (20, 12) keeps Petra's second Civic Center off the neutral
		// strip below it (findEconomicCCLocation), where the forts would shoot its gatherers,
		// and stays off the way east from Petra's Civic Center, where it would shoot builders
		// walking to Petra's own towers there.
		for (const float *at : {(const float[2]){24.0f, 37.0f}, (const float[2]){20.0f, 12.0f}, (const float[2]){6.0f, 22.0f}}) {
			const uint32_t fort = s->spawn_building_world(at[0], at[1], BLD_FORTRESS, 0, 1.0f);
			std::vector<uint32_t> men;
			for (int k = 0; k < 10; ++k)
				men.push_back(s->spawn_unit(at[0] - 6.0f + (float)(k % 5), at[1] - 6.0f - (float)(k / 5), 0, UNIT_SPEARMAN));
			s->order_garrison(men.data(), men.size(), fort);
		}
		bool hero = false, champion = false, siege = false, tower = false, packed_moved = false, unpacked_fired = false;
		bool advanced = false, arsenal = false;
		// 45 minutes: at 0 A.D.'s gather rates (template_unit_*/ResourceGatherer) Petra reaches
		// the city phase around minute 15, as in 0 A.D., and its city army takes a while longer.
		for (float t = 0.0f; t < 45.0f * 60.0f && !s->is_game_over(); t += Simulation::FIXED_STEP) {
			s->tick(Simulation::FIXED_STEP);
			if (s->ticks_ % 20 != 0) continue; // once a second
			// A strong economy can raze the idle player before its siege is out: give
			// it a new fortress so the siege still has a target to march to and shell.
			// Only until the siege has done both: a fortress rising in range of siege
			// that never leaves keeps its attack going (attackPlan.js update ends one
			// only when its army or targets are gone), and two such attacks block every
			// later one (attackManager.js), with its hero and champions.
			bool idle_alive = false;
			for (const auto &b : s->buildings())
				if (b.team == 0 && b.hp > 0.0f && !b.wrecked) idle_alive = true;
			// Garrisoned like the first ones, so Petra shells it rather than capturing a string of
			// fortresses that each keep 20 of its soldiers inside against decay (TYPE_DECAY).
			if (!idle_alive && !(packed_moved && unpacked_fired)) {
				const uint32_t fort = s->spawn_building_world(24.0f, 37.0f, BLD_FORTRESS, 0, 1.0f);
				for (int k = 0; k < 10; ++k) {
					const uint32_t man = s->spawn_unit(18.0f + (float)(k % 5), 31.0f - (float)(k / 5), 0, UNIT_SPEARMAN);
					s->order_garrison(&man, 1, fort);
				}
			}
			for (const auto &b : s->buildings())
				if (b.team == 1 && b.build_progress >= 1.0f && !b.wrecked) {
					if (b.type == BLD_GYMNASIUM || b.type == BLD_THEATER) advanced = true;
					if (b.type == BLD_ARSENAL) arsenal = true;
				}
			const auto u = s->units();
			for (size_t i = 0; i < u.count; ++i) {
				if (!u.alive[i] || u.team[i] != 1 || u.tmpl[i] >= CIV_UNIT_TEMPLATE_COUNT) continue;
				const CivUnitTemplate &ct = civ_unit_template(u.tmpl[i]);
				if (ct.flags & ROSTER_HERO) hero = true;
				if (ct.flags & ROSTER_CHAMPION) champion = true;
				if (ct.flags & ROSTER_SIEGE_TOWER) tower = true;
				const uint8_t ps = s->units_.pack_state[i];
				if (ps == PACK_PACKED && (u.action_state[i] == ACTION_MOVE || u.action_state[i] == ACTION_ATTACK_MOVE)) packed_moved = true;
				if (ps == PACK_UNPACKED && u.action_state[i] == ACTION_ATTACK) unpacked_fired = true;
				else if (u.unit_type[i] == UNIT_CATAPULT || u.unit_type[i] == UNIT_SIEGE_RAM || u.unit_type[i] == UNIT_BOLT_SHOOTER) siege = true;
			}
		}
		std::printf("  %s by 45 min: phase %d, hero %s, champions %s, siege %s, siege tower %s, siege moves packed %s, fires unpacked %s, arsenal %s, %s %s\n",
		            civ_info((Civilization)civ).name, s->petra_phase(), hero ? "yes" : "no", champion ? "yes" : "no",
		            siege ? "yes" : "no", tower ? "yes" : "no",
		            packed_moved ? "yes" : "no", unpacked_fired ? "yes" : "no", arsenal ? "yes" : "no",
		            civ == CIV_ATHENIANS ? "gymnasium" : "theater", advanced ? "yes" : "no");
		CHECK(advanced, "builds the civ's advanced structure (config.js buildings)");
		CHECK(hero, "trains a hero in the city phase");
		CHECK(champion, "trains roster champions in the city phase");
		CHECK(siege, "trains siege engines");
		// HQ.findBestTrainableUnit leaves siege towers out: Petra does not know how to use them.
		if (civ == CIV_MACEDONIANS) CHECK(!tower, "trains no siege tower");
		// Petra picks siege by siegeStrength (attackPlan.js unitStat Siege), so a civ whose rams
		// outlast the fortresses may never field a catapult: packing is checked across both civs.
		any_packed_moved |= packed_moved;
		any_unpacked_fired |= unpacked_fired;
	}
	CHECK(any_packed_moved, "siege packs to follow the army");
	CHECK(any_unpacked_fired, "siege unpacks to fire in range");
}

static int count_buildings(const Simulation &s, uint8_t team) {
	int n = 0;
	for (const auto &b : s.buildings()) if (b.team == team && b.hp > 0.0f) ++n;
	return n;
}

// 0 A.D. gives the AI no flat income (InitGame.js "AI Bonus" scales gathering).
static void test_no_free_income() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(1);
	s->spawn_initial();
	s->set_petra_difficulty(PETRA_VERY_HARD);
	auto &u = s->units_;
	for (size_t i = 0; i < u.count; ++i)
		if (u.team[i] == 1) u.alive[i] = false;
	s->treasures_.clear(); // a unit it trains may pick one up: income, but not a free one
	int before = 0, after = 0;
	for (uint8_t r = 0; r < RES_COUNT; ++r) before += s->player_resource(1, r);
	for (float t = 0.0f; t < 30.0f; t += Simulation::FIXED_STEP) s->tick(Simulation::FIXED_STEP);
	for (uint8_t r = 0; r < RES_COUNT; ++r) after += s->player_resource(1, r);
	std::printf("  very hard AI with no workers: resources %d -> %d, gather x%.2f, time x%.2f (human x%.2f)\n",
	            before, after, s->ai_gather_mult(1), s->ai_time_mult(1), s->ai_gather_mult(0));
	CHECK(after <= before, "no resources without gatherers");
	CHECK(s->ai_gather_mult(1) > 1.5f && s->ai_gather_mult(0) == 1.0f, "very hard gathers at 1.56x, humans at 1x");
	s->set_petra_difficulty(PETRA_EASY);
	CHECK(s->ai_gather_mult(1) == 0.75f && s->ai_time_mult(1) == 1.10f, "easy gathers slower and builds slower");
}

// headquarters.js trainMoreWorkers / getAccountedWorkers: mercenaries trained as workers keep the
// Worker role though they cannot gather, so they count toward the worker target.
static void test_mercenary_workers() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(1);
	s->spawn_initial();
	s->petra_pid_ = 1;
	const int before = s->petra_accounted_workers();
	uint16_t merc = CIV_UNIT_TEMPLATE_NONE;
	for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT; ++t)
		if (!std::strcmp(civ_unit_template(t).id, "athen/infantry_archer_b")) merc = t;
	CHECK(merc != CIV_UNIT_TEMPLATE_NONE && unit_is_mercenary_infantry(UNIT_ARCHER, merc), "Cretan archers are mercenary infantry");
	for (int k = 0; k < 3; ++k) s->spawn_unit(10.0f + 2.0f * (float)k, 10.0f, 1, UNIT_ARCHER, 0.0f, merc);
	std::printf("  mercenary workers: accounted %d -> %d\n", before, s->petra_accounted_workers());
	CHECK(s->petra_accounted_workers() == before + 3, "three idle mercenaries count as workers");
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
	// No human (0 A.D. observer game): player 0 runs its own Petra like any AI slot.
	{
		auto s = std::make_unique<Simulation>();
		s->set_sim_seed(11);
		s->reset();
		s->generate_random_map(MAP_RANDOM_CONTINENTAL, 11, 2, 64.0f);
		s->set_player_active(0, true, true);
		s->set_player_active(1, true, true);
		const int w0 = count_units(*s, 0, true), b0 = count_buildings(*s, 0);
		for (float t = 0.0f; t < 240.0f && !s->is_game_over(); t += Simulation::FIXED_STEP) s->tick(Simulation::FIXED_STEP);
		std::printf("  AI-only game: player 0 workers %d -> %d, buildings %d -> %d\n",
		            w0, count_units(*s, 0, true), b0, count_buildings(*s, 0));
		CHECK(s->petra_runs_for(0) && s->petra_runs_for(1), "Petra runs for both slots");
		CHECK(count_units(*s, 0, true) > w0 + 4, "AI player 0 trains workers");
		CHECK(count_buildings(*s, 0) > b0 + 2, "AI player 0 builds");
	}
	// Human vs two allied AIs: teammates never kill each other's units.
	{
		auto s = std::make_unique<Simulation>();
		s->set_sim_seed(7);
		s->reset();
		s->generate_random_map(MAP_RANDOM_CONTINENTAL, 7, 3, 96.0f);
		s->set_player_active(0, true, false);
		s->set_player_active(1, true, true);
		s->set_player_active(2, true, true);
		s->set_player_team(1, 5);
		s->set_player_team(2, 5);
		s->apply_team_diplomacy();
		int friendly_kills = 0;
		size_t seen = 0;
		for (float t = 0.0f; t < 360.0f && !s->is_game_over(); t += Simulation::FIXED_STEP) {
			s->tick(Simulation::FIXED_STEP);
			for (; seen < s->recent_death_head_; ++seen) {
				const auto &d = s->recent_deaths_[seen % Simulation::MAX_RECENT_DEATHS];
				if ((d.team == 1 && d.killer_team == 2) || (d.team == 2 && d.killer_team == 1)) ++friendly_kills;
			}
		}
		std::printf("  allied AIs: kills between teammates %d\n", friendly_kills);
		CHECK(friendly_kills == 0, "AIs on one team never fight each other");
	}
}

// A line `from` said that `to` reads (0xFF: one said to everyone).
static bool said_to(const Simulation &s, uint8_t from, uint8_t to, uint8_t kind) {
	for (const auto &l : s.pending_petra_chat())
		if (l.from == from && l.kind == kind && (to == 0xFF ? l.to == PETRA_CHAT_EVERYONE : s.petra_chat_reaches(l, to)))
			return true;
	return false;
}

// Every queue funded and unpaused (checkPausedQueues pauses most of them for an AI with
// few workers), for tests of what Petra does once it pays.
static void fund_queues(Simulation &s) {
	for (auto &a : s.petra_queues_.accounts) a = {5000, 5000, 5000, 5000};
	for (auto &p : s.petra_queues_.paused) p = 0;
}

static void run_for(Simulation &s, float seconds) {
	for (float t = 0.0f; t < seconds && !s.is_game_over(); t += Simulation::FIXED_STEP) s.tick(Simulation::FIXED_STEP);
}

// Ticks until AI `pid` has played `turns` more turns (one every PETRA_TURN_TICKS).
static void run_ai_turns(Simulation &s, int turns, uint8_t pid = 1) {
	while (turns > 0 && !s.is_game_over()) {
		const bool due = s.petra_turn_due(pid);
		s.tick(Simulation::FIXED_STEP);
		if (due) --turns;
	}
}

static std::unique_ptr<Simulation> three_player_match(uint32_t seed) {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(seed);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, seed, 3, 96.0f);
	s->set_player_active(0, true, false);
	s->set_player_active(1, true, true);
	s->set_player_active(2, true, true);
	return s;
}

static void test_diplomacy() {
	// A human at war offers an AI an alliance: an enemy can't become an ally at once, so
	// the AI turns it down and suggests a neutrality pact (diplomacyManager.js checkEvents).
	{
		auto s = three_player_match(1);
		run_for(*s, 20.0f);
		s->clear_petra_chat();
		s->cmd_set_diplomacy(0, 1, Simulation::DIPLO_ALLY);
		run_for(*s, 10.0f);
		CHECK(said_to(*s, 1, 0, PETRA_CHAT_ANSWER_DECLINE_SUGGEST_NEUTRAL) && !s->is_ally(1, 0),
		      "an enemy's alliance offer is answered with a neutrality suggestion");
		for (const auto &l : s->pending_petra_chat())
			if (l.kind == PETRA_CHAT_ANSWER_DECLINE_SUGGEST_NEUTRAL)
				std::printf("  to an enemy's alliance offer AI 1 says: \"%s\"\n",
				            petra_chat_text(l, [](uint8_t p) { return std::string("Player ") + std::to_string(p); }).c_str());
	}
	// A human offers each AI a neutrality pact. Every offer gets an answer, and paying an
	// asked-for tribute seals it.
	int accepted = 0, declined = 0, paid = 0;
	for (uint32_t seed = 1; seed <= 6; ++seed) {
		auto s = three_player_match(seed);
		run_for(*s, 20.0f);
		s->clear_petra_chat();
		s->cmd_set_diplomacy(0, 1, Simulation::DIPLO_NEUTRAL);
		run_for(*s, 10.0f);
		const auto &req = s->petra_diplo_.received[0]; // AI 1's state lives in the members
		const bool neutral = s->get_diplomacy(1, 0) == Simulation::DIPLO_NEUTRAL;
		if (said_to(*s, 1, 0, PETRA_CHAT_ANSWER_ACCEPT)) {
			CHECK(neutral && said_to(*s, 1, 0xFF, PETRA_CHAT_NEW_DIPLOMACY), "accepted offer makes the AI neutral, and it says so");
			++accepted;
		} else if (said_to(*s, 1, 0, PETRA_CHAT_ANSWER_DECLINE)) {
			CHECK(!neutral, "declined offer leaves the AI hostile");
			++declined;
		} else if (said_to(*s, 1, 0, PETRA_CHAT_ANSWER_ACCEPT_WITH_TRIBUTE)) {
			CHECK(req.status == PetraReceivedRequest::WAITING_FOR_TRIBUTE && req.wanted >= 1000, "asks for at least 1000");
			s->players_[0].res[req.resource] += (float)req.wanted;
			CHECK(s->send_tribute(1, req.resource, req.wanted), "human pays the tribute");
			run_for(*s, 10.0f);
			CHECK(s->get_diplomacy(1, 0) == Simulation::DIPLO_NEUTRAL, "paid tribute seals the pact");
			++paid;
		} else {
			CHECK(false, "AI answers a neutrality offer");
		}
		// Only the human reads the answer; the other AI doesn't.
		for (const auto &l : s->pending_petra_chat())
			if (l.kind >= PETRA_CHAT_ANSWER_DECLINE && l.kind <= PETRA_CHAT_ANSWER_WAITING_FOR_TRIBUTE)
				CHECK(l.to == 0 && !s->petra_chat_reaches(l, 2), "a diplomacy answer goes to the asker only");
	}
	std::printf("  neutrality offers over 6 seeds: accepted %d, declined %d, accepted for tribute %d\n",
	            accepted, declined, paid);

	// attackManager.js currentEnemyPlayer outlives the attacks: the player AI 1 last
	// chose to attack is always turned down (handleDiplomacyRequest), even between attacks.
	{
		int refused = 0;
		for (uint32_t seed = 1; seed <= 4; ++seed) {
			auto s = three_player_match(seed);
			run_for(*s, 20.0f);
			s->petra_sent_diplo_.current_enemy = 1; // AI 1 last picked player 0
			s->clear_petra_chat();
			s->cmd_set_diplomacy(0, 1, Simulation::DIPLO_NEUTRAL);
			run_for(*s, 10.0f);
			refused += said_to(*s, 1, 0, PETRA_CHAT_ANSWER_DECLINE) && s->get_diplomacy(1, 0) == Simulation::DIPLO_ENEMY;
		}
		std::printf("  neutrality offers from AI 1's current enemy: %d of 4 refused\n", refused);
		CHECK(refused == 4, "the player Petra last chose to attack is always refused");
	}

	// Allies with AI 2, then the human declares war: AI 2 answers in kind.
	{
		auto s = three_player_match(3);
		s->set_mutual_ally(0, 2);
		run_for(*s, 20.0f);
		CHECK(s->is_ally(0, 2), "a standing alliance holds");
		s->cmd_set_diplomacy(0, 2, Simulation::DIPLO_ENEMY);
		run_for(*s, 10.0f);
		const bool back = s->get_diplomacy(2, 0) == Simulation::DIPLO_ENEMY;
		std::printf("  human declares war on its AI ally: AI answers with war: %s\n", back ? "yes" : "no");
		CHECK(back, "AI declares war back");
	}

	// Tribute: an AI helps a broke ally with what it has plenty of.
	{
		auto s = three_player_match(5);
		s->set_mutual_ally(1, 2);
		for (uint8_t r = 0; r < RES_COUNT; ++r) {
			s->players_[1].res[r] = 3000.0f;
			s->players_[2].res[r] = 0.0f;
		}
		float t = 0.0f;
		bool sent = false;
		for (; t < 120.0f && !sent; t += Simulation::FIXED_STEP) {
			s->tick(Simulation::FIXED_STEP);
			sent = said_to(*s, 1, 2, PETRA_CHAT_TRIBUTE);
		}
		const float got = s->players_[2].res[RES_FOOD] + s->players_[2].res[RES_STONE];
		std::printf("  AI 1 tributes its broke ally AI 2: %s at %.0fs, AI 2 food+stone %.0f, AI 1 keeps %.0f food\n",
		            sent ? "yes" : "no", t, got, s->players_[1].res[RES_FOOD]);
		CHECK(sent && got > 0.0f, "an AI tributes an ally short of resources");
	}

	// Trade: with an ally's market far away, Petra's traders run to it for the
	// international bonus rather than between its own two close markets.
	{
		auto s = three_player_match(5);
		s->set_mutual_ally(1, 2);
		s->players_[1].phase = 2;
		uint32_t own_tc = 0, ally_tc = 0;
		for (const auto &b : s->buildings()) {
			if (b.type != BLD_TOWN_CENTER) continue;
			if (b.team == 1) own_tc = b.id;
			if (b.team == 2) ally_tc = b.id;
		}
		CHECK(own_tc && ally_tc, "both AIs have a Town Center");
		const auto *tc1 = &s->buildings()[0], *tc2 = tc1;
		for (const auto &b : s->buildings()) { if (b.id == own_tc) tc1 = &b; if (b.id == ally_tc) tc2 = &b; }
		const float ox = tc1->x, oz = tc1->z, ax = tc2->x, az = tc2->z;
		const uint32_t m1 = s->spawn_building_world(ox + 12.0f, oz, BLD_MARKET, 1, 1.0f);
		const uint32_t m2 = s->spawn_building_world(ox, oz + 12.0f, BLD_MARKET, 1, 1.0f);
		const uint32_t ma = s->spawn_building_world(ax + 12.0f, az, BLD_MARKET, 2, 1.0f);
		const uint32_t trader = s->spawn_unit(ox + 8.0f, oz + 8.0f, 1, UNIT_TRADER);
		run_for(*s, 10.0f);
		auto it = s->trader_routes_.find(trader);
		const bool routed = it != s->trader_routes_.end();
		const bool to_ally = routed && (it->second.first == ma || it->second.second == ma);
		std::printf("  trader route with an allied market: %s (own markets %u, %u; ally %u)\n",
		            !routed ? "none" : to_ally ? "to the ally" : "own markets only", m1, m2, ma);
		CHECK(to_ally, "Petra trades with an allied market for the international bonus");
	}

	// Last Man Standing: two allied AIs, the human is out. Once one is strong
	// enough it turns on the other.
	{
		auto s = three_player_match(7);
		s->set_last_man_standing(true);
		s->set_mutual_ally(1, 2);
		run_for(*s, 5.0f);
		auto &u = s->units_;
		for (size_t i = 0; i < u.count; ++i) if (u.team[i] == 0) u.alive[i] = false;
		for (auto &b : s->buildings_) if (b.team == 0) b.hp = 0.0f;
		float t = 0.0f;
		for (; t < 20.0f * 60.0f && s->is_ally(1, 2); t += Simulation::FIXED_STEP) s->tick(Simulation::FIXED_STEP);
		std::printf("  last man standing: human defeated %s, allied AIs fall out at %.0fs\n",
		            s->is_defeated(0) ? "yes" : "no", s->is_ally(1, 2) ? -1.0f : t);
		CHECK(s->is_defeated(0), "the human is out");
		CHECK(!s->is_ally(1, 2), "one AI betrays the other once no enemy is left");
	}

	// init: a mutual ally at the start is an accepted alliance; PlayerDefeated
	// drops a defeated player's requests.
	{
		auto s = std::make_unique<Simulation>();
		s->set_sim_seed(3);
		s->reset();
		s->generate_random_map(MAP_RANDOM_CONTINENTAL, 3, 4, 96.0f);
		for (uint8_t p = 0; p < 4; ++p) s->set_player_active(p, true, p != 0);
		s->set_mutual_ally(1, 2);
		run_for(*s, 10.0f);
		CHECK(s->petra_diplo_.received[2].status == PetraReceivedRequest::ACCEPTED,
		      "a starting mutual ally is recorded as an accepted alliance");
		s->petra_diplo_.received[0].status = PetraReceivedRequest::WAITING_FOR_TRIBUTE;
		auto &u = s->units_;
		for (size_t i = 0; i < u.count; ++i) if (u.team[i] == 0) u.alive[i] = false;
		for (auto &b : s->buildings_) if (b.team == 0) b.hp = 0.0f;
		run_for(*s, 20.0f);
		std::printf("  init and defeat: defeated %d, ally recorded %d, defeated player's request %d\n", (int)s->is_defeated(0),
		            (int)s->petra_diplo_.received[2].status, (int)s->petra_diplo_.received[0].status);
		CHECK(s->is_defeated(0) && s->petra_diplo_.received[0].status == PetraReceivedRequest::NONE,
		      "a defeated player's request is dropped");
	}

	// checkEvents Attacked: an ally's hits on an enemy inside our territory raise
	// our cooperative trait by 0.003 each, arrows and splash included.
	{
		auto s = three_player_match(5);
		s->set_mutual_ally(1, 2);
		run_for(*s, 2.0f);
		float tx = 0.0f, tz = 0.0f;
		for (const auto &b : s->buildings())
			if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tx = b.x; tz = b.z; }
		s->petra_config_slot(1).cooperative = 0.5f;
		const float before = s->petra_config_slot(1).cooperative;
		const uint32_t foe = s->spawn_unit(tx + 12.0f, tz + 12.0f, 0, UNIT_ELEPHANT);
		const uint32_t foe_s = foe;
		s->order_set_stance(&foe_s, 1, STANCE_PASSIVE);
		std::vector<uint32_t> archers;
		for (int i = 0; i < 3; ++i) archers.push_back(s->spawn_unit(tx + 12.0f + 2.0f * (float)i, tz + 20.0f, 2, UNIT_ARCHER));
		s->order_attack(archers.data(), archers.size(), foe);
		run_for(*s, 10.0f);
		const float after = s->petra_config_slot(1).cooperative;
		std::printf("  ally fights in our territory: cooperative %.3f -> %.3f\n", before, after);
		CHECK(after >= std::min(1.0f, before + 0.01f), "an ally's hits in our territory raise cooperativeness");
	}

	// Last Man Standing with unlocked teams: lobby teams only set the starting
	// alliances, so teammates fall out too once no enemy is left.
	{
		auto s = three_player_match(7);
		s->set_last_man_standing(true);
		s->players_[1].team = s->players_[2].team = 1;
		run_for(*s, 5.0f);
		auto &u = s->units_;
		for (size_t i = 0; i < u.count; ++i) if (u.team[i] == 0) u.alive[i] = false;
		for (auto &b : s->buildings_) if (b.team == 0) b.hp = 0.0f;
		float t = 0.0f;
		for (; t < 20.0f * 60.0f && s->is_ally(1, 2); t += Simulation::FIXED_STEP) s->tick(Simulation::FIXED_STEP);
		std::printf("  last man standing, unlocked teams: teammates fall out at %.0fs\n", s->is_ally(1, 2) ? -1.0f : t);
		CHECK(!s->is_ally(1, 2), "teammates turn on each other when teams are unlocked");
	}

	// Last Man Standing in a wonder game: an ally with a finished wonder is
	// betrayed first, even when another ally has more entities (DM:335-348).
	{
		auto s = std::make_unique<Simulation>();
		s->set_sim_seed(9);
		s->reset();
		s->generate_random_map(MAP_RANDOM_CONTINENTAL, 9, 4, 96.0f);
		for (uint8_t p = 0; p < 4; ++p) s->set_player_active(p, true, p != 0);
		s->set_victory_condition(VICTORY_WONDER);
		s->set_last_man_standing(true);
		s->set_mutual_ally(1, 2);
		s->set_mutual_ally(1, 3);
		s->set_mutual_ally(2, 3);
		run_for(*s, 5.0f);
		auto &u = s->units_;
		for (size_t i = 0; i < u.count; ++i) if (u.team[i] == 0) u.alive[i] = false;
		for (auto &b : s->buildings_) if (b.team == 0) b.hp = 0.0f;
		float t1x = 0.0f, t1z = 0.0f, t2x = 0.0f, t2z = 0.0f, t3x = 0.0f, t3z = 0.0f;
		for (const auto &b : s->buildings()) {
			if (b.type != BLD_TOWN_CENTER) continue;
			if (b.team == 1) { t1x = b.x; t1z = b.z; }
			if (b.team == 2) { t2x = b.x; t2z = b.z; }
			if (b.team == 3) { t3x = b.x; t3z = b.z; }
		}
		for (int i = 0; i < 50; ++i) s->spawn_unit(t1x + 8.0f + (float)(i % 10), t1z + 8.0f + (float)(i / 10), 1, UNIT_SPEARMAN);
		for (int i = 0; i < 30; ++i) s->spawn_unit(t2x + 8.0f + (float)(i % 10), t2z + 8.0f + (float)(i / 10), 2, UNIT_SPEARMAN);
		s->spawn_building_world(t3x * 0.7f, t3z * 0.7f, BLD_WONDER, 3, 1.0f);
		int first = -1;
		for (float t = 0.0f; t < 4.0f * 60.0f && first < 0; t += Simulation::FIXED_STEP) {
			s->tick(Simulation::FIXED_STEP);
			if (s->get_diplomacy(1, 2) != Simulation::DIPLO_ALLY) first = 2;
			else if (s->get_diplomacy(1, 3) != Simulation::DIPLO_ALLY) first = 3;
		}
		std::printf("  last man standing, wonder game: AI 1 turns first on player %d (player 3 has the wonder)\n", first);
		CHECK(first == 3, "the ally with a finished wonder is betrayed first");
	}

	// Tribute: 0.28 gives an ally 30% of any resource we have over 200 and it
	// lacks, our own scarcest included.
	{
		auto s = three_player_match(5);
		s->set_mutual_ally(1, 2);
		float own[RES_COUNT];
		own[RES_WOOD] = own[RES_GOLD] = 3000.0f;
		own[RES_STONE] = own[RES_FOOD] = 1000.0f;
		for (uint8_t r = 0; r < RES_COUNT; ++r) {
			s->players_[1].res[r] = own[r];
			s->players_[2].res[r] = 0.0f;
		}
		bool sent = false;
		for (float t = 0.0f; t < 120.0f && !sent; t += Simulation::FIXED_STEP) {
			s->tick(Simulation::FIXED_STEP);
			sent = said_to(*s, 1, 2, PETRA_CHAT_TRIBUTE);
		}
		const float stone = s->players_[2].res[RES_STONE], food = s->players_[2].res[RES_FOOD];
		std::printf("  tribute of our scarcest: AI 2 gets stone %.0f, food %.0f\n", stone, food);
		CHECK(sent && (stone > 0.0f || food > 0.0f), "our two scarcest resources are tributed too");
	}
}

// attackManager.js update(): rushes while any are left, then "Attack" twice
// and "HugeAttack" after; raids answer enemy structures in Petra's territory.
static void test_attack_plans() {
	auto match = [](uint8_t behavior) {
		auto s = std::make_unique<Simulation>();
		s->set_sim_seed(42);
		s->reset();
		s->set_petra_difficulty(PETRA_MEDIUM);
		s->set_petra_behavior(behavior);
		s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
		s->set_victory_condition(VICTORY_ENDLESS);
		return s;
	};
	{
		// A storehouse more than 80 m from the idle player's Town Center is the
		// only structure a rush may target (rushTargetFinder); still in that Town
		// Center's territory, so it doesn't decay to Gaia.
		auto s = match(PETRA_BEHAVIOR_AGGRESSIVE);
		const uint32_t store = s->spawn_building_world(-3.0f, 25.0f, BLD_STOREHOUSE, 0, 1.0f);
		bool rushed = false, rush_at_store = false, told_allies = true, named_target = false;
		int attack_size = 0, huge_size = 0;
		for (float t = 0.0f; t < 25.0f * 60.0f; t += Simulation::FIXED_STEP) {
			s->tick(Simulation::FIXED_STEP);
			// chatLaunchAttack: "/allies", naming the player attacked.
			for (const auto &l : s->pending_petra_chat()) {
				if (l.kind != PETRA_CHAT_ATTACK && l.kind != PETRA_CHAT_HUGE_ATTACK) continue;
				told_allies &= l.to == PETRA_CHAT_ALLIES && !s->petra_chat_reaches(l, 0);
				named_target |= l.player == 0;
			}
			s->clear_petra_chat();
			for (const auto &plan : s->petra_started_attacks()) {
				// A rush that musters within reach of its target attacks on the tick it sets out.
				if (plan.state != Simulation::AttackPlan::WALKING && plan.state != Simulation::AttackPlan::ATTACKED) continue;
				if (plan.kind == PETRA_ATTACK_RUSH && s->petra_phase() == 1) {
					rushed = true;
					rush_at_store |= plan.target_building_id == store;
				}
				if (plan.kind == PETRA_ATTACK_ATTACK) attack_size = std::max(attack_size, (int)plan.initial_squad_size);
				if (plan.kind == PETRA_ATTACK_HUGE) huge_size = std::max(huge_size, (int)plan.initial_squad_size);
			}
		}
		const auto &m = s->petra_attack_manager();
		std::printf("  aggressive: rushes allowed %d, village rush %s (at the storehouse %s), launched rush %d attack %d huge %d, sizes %d / %d\n",
		            m.max_rushes, rushed ? "yes" : "no", rush_at_store ? "yes" : "no", m.launched[PETRA_ATTACK_RUSH],
		            m.launched[PETRA_ATTACK_ATTACK], m.launched[PETRA_ATTACK_HUGE], attack_size, huge_size);
		CHECK(m.max_rushes == 3, "an aggressive AI on a wood-rich base plans three rushes");
		CHECK(rushed && rush_at_store, "rushes the undefended storehouse in the village phase");
		CHECK(told_allies && named_target, "each attack is announced to allies only, naming the player attacked");
		// A first Attack that razes the idle player leaves nothing for a second one.
		bool enemy_left = false;
		for (const auto &b : s->buildings()) enemy_left |= b.team == 0 && b.hp > 0.0f;
		CHECK(m.launched[PETRA_ATTACK_ATTACK] >= 2 || (m.launched[PETRA_ATTACK_ATTACK] == 1 && !enemy_left),
		      "launches two Attack plans, or one that razes the enemy");
	}
	{
		// The kind of each plan as it starts mustering (whether a HugeAttack
		// marches before the idle player runs out of buildings depends on timing).
		auto s = match(PETRA_BEHAVIOR_AGGRESSIVE);
		// Fortresses on dry land around its base keep the idle player standing after the Attacks raze its
		// base (an unmanned fortress shoots only its 10 default arrows, BuildingAI), so there is
		// still a target to plan the HugeAttack against. The one at (20, 12) keeps Petra's second
		// Civic Center off the neutral strip below it and off the way east from Petra's Civic
		// Center, as in test_roster_army.
		s->spawn_building_world(24.0f, 37.0f, BLD_FORTRESS, 0, 1.0f);
		s->spawn_building_world(20.0f, 12.0f, BLD_FORTRESS, 0, 1.0f);
		s->spawn_building_world(6.0f, 22.0f, BLD_FORTRESS, 0, 1.0f);
		// Murder Holes: soldiers hacking at a fortress from under its arrows (MinRange) would
		// hold both attack slots for the rest of the match.
		s->complete_research(0, TECH_TOWER_MURDERHOLES);
		int attack_size = 0, huge_size = 0;
		std::vector<int> kinds;
		auto last = Simulation::AttackPlan::UNEXECUTED;
		// At 0 A.D.'s gather rates the HugeAttack starts mustering around minute 14-27 on this
		// map (later once traders take a market-dock route); 30 min leaves room for map changes.
		// An army that razes the idle player finds it rebuilt, so every plan has a target.
		float home_x = 0.0f, home_z = 0.0f;
		for (const auto &b : s->buildings()) if (b.team == 0 && b.type == BLD_TOWN_CENTER) { home_x = b.x; home_z = b.z; }
		int step = 0;
		for (float t = 0.0f; t < 30.0f * 60.0f; t += Simulation::FIXED_STEP) {
			s->tick(Simulation::FIXED_STEP);
			if (++step % 200 == 0) {
				bool any = false;
				for (const auto &b : s->buildings()) any |= b.team == 0 && b.hp > 0.0f;
				if (!any) s->spawn_building_world(home_x, home_z, BLD_TOWN_CENTER, 0, 1.0f);
			}
			const auto &m = s->petra_attack_manager();
			const auto st = s->petra_attack_plan().state;
			if (st == Simulation::AttackPlan::MUSTER && last != st) {
				kinds.push_back(m.kind);
				if (m.kind == PETRA_ATTACK_ATTACK) attack_size = m.target_size;
				if (m.kind == PETRA_ATTACK_HUGE) huge_size = m.target_size;
			}
			last = st;
		}
		const auto &m = s->petra_attack_manager();
		std::printf("  no rush target: rushes planned %d, plans", m.rush_number);
		for (int k : kinds) std::printf(" %s", k == PETRA_ATTACK_ATTACK ? "Attack" : k == PETRA_ATTACK_HUGE ? "HugeAttack" : "other");
		std::printf(", launched attack %d huge %d, target sizes %d / %d\n",
		            m.launched[PETRA_ATTACK_ATTACK], m.launched[PETRA_ATTACK_HUGE], attack_size, huge_size);
		CHECK(m.launched[PETRA_ATTACK_RUSH] == 0, "no rush without an undefended target");
		CHECK(kinds.size() >= 3 && kinds[0] == PETRA_ATTACK_ATTACK && kinds[1] == PETRA_ATTACK_ATTACK,
		      "the first two plans are Attack");
		CHECK(kinds.size() >= 3 && kinds[2] == PETRA_ATTACK_HUGE, "then HugeAttack");
		// With a HugeAttack under way the next plan is an Attack again (attackManager.js).
		CHECK(m.launched[PETRA_ATTACK_ATTACK] >= 2, "both Attack plans march");
		CHECK(huge_size > 2 * attack_size, "a HugeAttack musters a much larger army");
	}
	{
		auto s = match(PETRA_BEHAVIOR_DEFENSIVE);
		for (float t = 0.0f; t < 5.0f; t += Simulation::FIXED_STEP) s->tick(Simulation::FIXED_STEP);
		CHECK(s->petra_attack_manager().rushes_set && s->petra_attack_manager().max_rushes == 0,
		      "a defensive AI plans no rush");
	}
	{
		// Raid: an enemy outpost in Petra's territory, cavalry and spearmen at hand. (An
		// outpost claims no territory; a house's TerritoryInfluence would own its own ground.)
		// Six riders: two stay home to hunt (attackPlan.js addUnits).
		auto s = match(PETRA_BEHAVIOR_BALANCED);
		const uint32_t outpost = s->spawn_building_world(-10.0f, -40.0f, BLD_OUTPOST, 0, 1.0f);
		for (int i = 0; i < 6; ++i) s->spawn_unit(-30.0f + 2.0f * (float)i, -12.0f, 1, UNIT_CAVALRY);
		for (int i = 0; i < 4; ++i) s->spawn_unit(-30.0f + 2.0f * (float)i, -10.0f, 1, UNIT_SPEARMAN);
		bool raided = false, cavalry_only = true;
		size_t raiders = 0;
		for (float t = 0.0f; t < 20.0f && !raided; t += Simulation::FIXED_STEP) {
			s->tick(Simulation::FIXED_STEP);
			const auto &raid = s->petra_raid_plan();
			if (raid.state != Simulation::AttackPlan::WALKING || raid.target_building_id != outpost) continue;
			raided = true;
			raiders = raid.squad.size();
			const auto u = s->units();
			for (uint32_t id : raid.squad)
				for (size_t i = 0; i < u.count; ++i)
					if (u.id[i] == id && u.unit_type[i] != UNIT_CAVALRY) cavalry_only = false;
		}
		std::printf("  raid on an outpost in Petra's territory: %s with %zu riders, cavalry only %s\n",
		            raided ? "yes" : "no", raiders, cavalry_only ? "yes" : "no");
		CHECK(raided && raiders >= 3, "raids an enemy structure in its territory");
		CHECK(cavalry_only, "raids with cavalry only");
	}
}

// Game setup gives each AI player its own difficulty and behaviour.
static void test_slot_config() {
	auto s = three_player_match(4);
	// AI 1's edit comes first and must not leak into AI 2's slot.
	s->set_petra_behavior(1, PETRA_BEHAVIOR_DEFENSIVE);
	s->set_petra_difficulty(2, PETRA_EASY);
	CHECK(s->petra_config_for(2).behavior == PETRA_BEHAVIOR_BALANCED, "one AI's setting doesn't leak into another slot");
	s->set_petra_behavior(1, PETRA_BEHAVIOR_BALANCED);
	s->set_petra_difficulty(1, PETRA_VERY_EASY);
	s->set_petra_difficulty(2, PETRA_VERY_HARD);
	s->set_petra_behavior(2, PETRA_BEHAVIOR_AGGRESSIVE);
	run_for(*s, 60.0f);
	auto same = [&](const Simulation &x, const char *when) {
		const PetraConfig &a = x.petra_config_for(1), &b = x.petra_config_for(2);
		std::printf("  per-slot config %s: AI 1 difficulty %d gather x%.2f, AI 2 difficulty %d gather x%.2f time x%.2f aggressive %.2f\n",
		            when, a.difficulty, x.ai_gather_mult(1), b.difficulty, x.ai_gather_mult(2), x.ai_time_mult(2), b.aggressive);
		CHECK(a.difficulty == PETRA_VERY_EASY && b.difficulty == PETRA_VERY_HARD, "each AI keeps its own difficulty");
		CHECK(a.behavior == PETRA_BEHAVIOR_BALANCED && b.behavior == PETRA_BEHAVIOR_AGGRESSIVE, "each AI keeps its own behaviour");
		CHECK(x.ai_gather_mult(1) < 0.6f && x.ai_gather_mult(2) > 1.5f, "gather rate follows each AI's difficulty");
		CHECK(x.ai_gather_mult(0) == 1.0f, "the human gathers at the base rate");
	};
	same(*s, "after a minute");
	const char *path = "/tmp/rtso_test_petra_slots.sav";
	CHECK(s->save_state(path), "saves");
	auto t = three_player_match(4); // the game builds the same map, then loads
	CHECK(t->load_state(path), "loads");
	same(*t, "after save and load");
	std::remove(path);
}

// entityExtend.js allowCapture: Petra captures what its squad can take and
// damages what it can't (a garrisoned fortress), and siege never captures.
static void test_allow_capture() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	// All on dry land: land units can't wade into deep water (0 A.D. passability).
	const uint32_t house = s->spawn_building_world(-30.0f, 10.0f, BLD_HOUSE, 0, 1.0f);
	const uint32_t fort = s->spawn_building_world(-30.0f, 25.0f, BLD_FORTRESS, 0, 1.0f);
	std::vector<uint32_t> guards;
	for (int i = 0; i < 10; ++i) guards.push_back(s->spawn_unit(-36.0f + (float)i, 16.0f, 0, UNIT_SPEARMAN));
	s->order_garrison(guards.data(), guards.size(), fort);
	run_for(*s, 20.0f);
	std::vector<uint32_t> squad;
	// Five spearmen (12.5 a second) beat a house's RegenRate 5 plus an 80th of its points, also
	// once one of six is sent at the fortress.
	for (int i = 0; i < 6; ++i) squad.push_back(s->spawn_unit(-40.0f + 2.0f * (float)i, -2.0f, 1, UNIT_SPEARMAN));
	const uint32_t ram = s->spawn_unit(-40.0f, -4.0f, 1, UNIT_SIEGE_RAM);
	auto bld = [&](uint32_t id) -> const Simulation::Building & { return s->buildings()[s->building_id_to_idx_.at(id)]; };
	const auto &f = bld(fort);
	s->order_attack_building(squad.data(), squad.size(), house, false, true); // the squad closes on the house
	const bool take_house = s->petra_allow_capture(squad, squad[0], bld(house));
	const bool take_fort = s->petra_allow_capture(squad, squad[0], f);
	const bool ram_captures = s->petra_allow_capture(squad, ram, bld(house));
	std::printf("  allowCapture: house (%.0f points, regen %.1f) %s, fortress with %zu garrisoned %s, ram %s\n",
	            bld(house).max_capture_points, bld(house).capture_regen_rate, take_house ? "capture" : "damage", f.garrisoned_units.size(), take_fort ? "capture" : "damage",
	            ram_captures ? "capture" : "damage");
	CHECK(take_house, "captures a house");
	CHECK(f.garrisoned_units.size() == guards.size() && !take_fort, "damages a garrisoned fortress");
	CHECK(!ram_captures, "siege never captures");

	// A spearman sent to capture the fortress switches to damaging it.
	s->order_attack_building(&squad[1], 1, fort, false, true);
	s->petra_check_captures(squad, f.x, f.z);
	const auto &u = s->units_;
	const int32_t i = s->lookup_unit(squad[1]);
	CHECK(u.order_kind[i] == ORDER_ATTACK_BUILDING && !(u.order_flags[i] & ORDER_FLAG_ALLOW_CAPTURE),
	      "a capture the squad can't win becomes an attack");
	s->order_attack_building(&squad[2], 1, house, false, true);
	s->petra_check_captures(squad, f.x, f.z);
	const int32_t j = s->lookup_unit(squad[2]);
	CHECK(u.order_flags[j] & ORDER_FLAG_ALLOW_CAPTURE, "a winnable capture goes on");
}

// garrisonManager.js / defenseManager.js: Petra's infantry man a Town Center
// an enemy is in range of and leave once the enemy is gone; an attacked
// woman below 55% health shelters in a healing structure.
static void test_garrisons() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 1.0f);
	const Simulation::Building *tc = nullptr;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) tc = &b;
	CHECK(tc != nullptr, "Petra has a Town Center");
	if (!tc) return;
	const float tx = tc->x, tz = tc->z;
	const uint32_t tc_id = tc->id;
	auto &u = s->units_;
	auto inside = [&](uint32_t id) {
		const auto &b = s->buildings()[s->building_id_to_idx_.at(tc_id)];
		return std::find(b.garrisoned_units.begin(), b.garrisoned_units.end(), id) != b.garrisoned_units.end();
	};
	std::vector<uint32_t> archers;
	// The Town Center's GarrisonArrowClasses are Soldier: cavalry man it too.
	for (int i = 0; i < 3; ++i) archers.push_back(s->spawn_unit(tx + 14.0f + 2.0f * (float)i, tz + 12.0f, 1, i == 2 ? UNIT_CAVALRY : UNIT_ARCHER));
	std::vector<uint32_t> foes;
	for (int i = 0; i < 3; ++i) foes.push_back(s->spawn_unit(tx - 10.0f, tz - 8.0f + 2.0f * (float)i, 0, UNIT_SPEARMAN));
	int manned = 0;
	for (float t = 0.0f; t < 15.0f && manned < 3; t += Simulation::FIXED_STEP) {
		for (uint32_t id : foes) { // the attackers hold out under the Town Center's arrows
			const int32_t f = s->lookup_unit(id);
			if (f >= 0 && u.alive[f]) u.hp[f] = u.max_hp[f];
		}
		s->tick(Simulation::FIXED_STEP);
		manned = 0;
		for (uint32_t id : archers) manned += inside(id) ? 1 : 0;
	}
	// The enemy leaves (dies); garrisonManager lets the archers out.
	for (uint32_t id : foes) {
		const int32_t i = s->lookup_unit(id);
		if (i >= 0 && u.alive[i]) { u.pos_x[i] = -tx; u.pos_z[i] = -tz; u.has_order[i] = 0; u.hp[i] = 0.0f; }
	}
	run_for(*s, 3.0f);
	int left = 0;
	for (uint32_t id : archers) left += inside(id) ? 0 : 1;
	std::printf("  garrisons: %d of 2 archers and a horseman manned the Town Center under attack, %d left after\n", manned, left);
	CHECK(manned == 3, "soldiers man a Town Center an enemy is in range of");
	CHECK(left == 3, "and leave once no enemy is near");

	// A hurt woman under attack shelters in the (healing) Town Center.
	const uint32_t woman = s->spawn_unit(tx + 11.0f, tz - 11.0f, 1, UNIT_FEMALE_CITIZEN);
	const uint32_t foe = s->spawn_unit(tx + 14.0f, tz - 11.0f, 0, UNIT_SWORDSMAN);
	const int32_t w = s->lookup_unit(woman);
	u.hp[w] = u.max_hp[w] * 0.5f;
	s->order_attack(&foe, 1, woman);
	bool sheltered = false;
	for (float t = 0.0f; t < 15.0f && !sheltered; t += Simulation::FIXED_STEP) { // a gatherer standing in her path can hold her up a few seconds (#262)
		if (u.alive[w]) u.hp[w] = u.max_hp[w] * 0.5f; // she lives long enough to reach it
		s->tick(Simulation::FIXED_STEP);
		sheltered = inside(woman);
	}
	std::printf("  a hurt woman under attack shelters in the Town Center: %s\n", sheltered ? "yes" : "no");
	CHECK(sheltered, "an attacked support unit below 55%% health shelters");
}

// diplomacyManager.js sendDiplomacyRequest between AIs: AI 1 asks the AI it
// shares the most enemies with for a neutrality pact; that AI answers through
// its own handleDiplomacyRequest (accepting, or asking a tribute AI 1 pays), and
// AI 1 matches the stance it took.
static void test_diplomacy_requests() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(7);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 7, 4, 128.0f);
	s->set_player_active(0, true, false);
	for (uint8_t p = 1; p < 4; ++p) s->set_player_active(p, true, true);
	s->set_team_civ(3, (uint8_t)s->players_[1].civ); // the same civ never declines for good at once
	run_for(*s, 20.0f);
	s->petra_send_diplomacy_request(); // as AI 1, which runs between ticks
	int target = -1;
	for (int p = 0; p < 8; ++p) if (s->petra_sent_diplo_.sent_type[p]) target = p;
	CHECK(target == 3, "asks the last of the players sharing the most enemies");
	if (target != 3) return;
	const bool asked_neutral = s->petra_sent_diplo_.sent_type[3] == 2;
	// At 0 A.D.'s gather rates AI 1 has nowhere near a 1000-resource tribute this early:
	// a stock lets it pay one if AI 3 asks for it.
	for (int r = 0; r < RES_COUNT; ++r) s->players_[1].res[r] += 2000.0f;
	float t = 0.0f;
	for (; t < 120.0f && !(s->get_diplomacy(1, 3) == Simulation::DIPLO_NEUTRAL && s->get_diplomacy(3, 1) == Simulation::DIPLO_NEUTRAL); t += Simulation::FIXED_STEP)
		s->tick(Simulation::FIXED_STEP);
	const auto &answer = s->petra_slots_[3].petra_diplo_.received[1];
	std::printf("  AI 1 asks AI 3 for a %s: AI 3 %s, stances 1->3 %d and 3->1 %d after %.0fs\n",
	            asked_neutral ? "neutrality pact" : "alliance",
	            answer.status == PetraReceivedRequest::ACCEPTED ? "accepted" :
	            answer.status == PetraReceivedRequest::WAITING_FOR_TRIBUTE ? "wants a tribute" : "declined",
	            s->get_diplomacy(1, 3), s->get_diplomacy(3, 1), t);
	CHECK(asked_neutral, "asks an enemy for a neutrality pact first");
	CHECK(s->get_diplomacy(3, 1) == Simulation::DIPLO_NEUTRAL && s->get_diplomacy(1, 3) == Simulation::DIPLO_NEUTRAL,
	      "both AIs end up neutral to each other");
}

// diplomacyManager.js handleTributeRequest: an AI asked for a tribute in return
// for a pact pays it once it can spare it; a request it can't pay at once
// stands until the asker withdraws it (120 s), rather than being forgotten.
static void test_tribute_request_paid_later() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(7);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 7, 4, 128.0f);
	s->set_player_active(0, true, false);
	for (uint8_t p = 1; p < 4; ++p) s->set_player_active(p, true, true);
	s->set_team_civ(3, (uint8_t)s->players_[1].civ);
	run_for(*s, 20.0f);
	s->petra_send_diplomacy_request();
	if (!s->petra_sent_diplo_.sent_type[3]) { CHECK(false, "AI 1 asks AI 3"); return; }
	// AI 3 accepts a pact outright 80% of the time (handleDiplomacyRequest), so
	// give it the other answer here, as petra_handle_diplomacy_request would:
	// a neutrality pact for 1000 food, asked of AI 1.
	s->petra_diplo_request_inbox_[3][1] = 0;
	{
		PetraReceivedRequest &req = s->petra_slots_[3].petra_diplo_.received[1];
		req = PetraReceivedRequest{};
		req.status = PetraReceivedRequest::WAITING_FOR_TRIBUTE;
		req.type = 1;
		req.resource = RES_FOOD;
		req.wanted = 1000;
		req.warn_time = s->petra_slots_[3].petra_diplo_.elapsed + 60.0f;
		s->petra_tribute_request_wanted_[1][3] = req.wanted;
		s->petra_tribute_request_res_[1][3] = req.resource;
	}
	for (int r = 0; r < RES_COUNT; ++r) s->players_[1].res[r] = 0.0f; // broke when AI 3 answers
	bool asked = true;
	for (float t = 0.0f; t < 30.0f; t += Simulation::FIXED_STEP) {
		s->players_[1].res[RES_GOLD] = s->players_[1].res[RES_FOOD] = s->players_[1].res[RES_WOOD] = s->players_[1].res[RES_STONE] = 0.0f;
		s->tick(Simulation::FIXED_STEP);
		asked &= s->petra_slots_[3].petra_diplo_.received[1].status == PetraReceivedRequest::WAITING_FOR_TRIBUTE;
	}
	for (int r = 0; r < RES_COUNT; ++r) s->players_[1].res[r] += 2000.0f;
	float t = 0.0f;
	for (; t < 60.0f && !(s->get_diplomacy(3, 1) == Simulation::DIPLO_NEUTRAL && s->get_diplomacy(1, 3) == Simulation::DIPLO_NEUTRAL);
	     t += Simulation::FIXED_STEP)
		s->tick(Simulation::FIXED_STEP);
	std::printf("  tribute asked of a broke AI: request stood %s, paid and neutral %s after %.0fs more\n",
	            asked ? "yes" : "no", s->get_diplomacy(3, 1) == Simulation::DIPLO_NEUTRAL ? "yes" : "no", t);
	CHECK(asked, "AI 3's tribute request stands while AI 1 is broke");
	CHECK(s->get_diplomacy(3, 1) == Simulation::DIPLO_NEUTRAL && s->get_diplomacy(1, 3) == Simulation::DIPLO_NEUTRAL,
	      "AI 1 pays once it can, and the pact is sealed");
}

// worker.js startHunting: Petra's food gatherers slaughter the chickens by
// their Town Center, its citizen cavalry chase the deer, and an animal grazing
// in the enemy's territory is left alone.
static void test_hunting() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 1.0f);
	float ex = 0.0f, ez = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 0 && b.type == BLD_TOWN_CENTER) { ex = b.x; ez = b.z; }
	CHECK(s->territory_owner_at(ex + 6.0f, ez) == 0, "the enemy's Town Center holds its land");
	const uint32_t foreign = s->spawn_fauna(ex + 6.0f, ez, FAUNA_SHEEP);
	float px = 0.0f, pz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { px = b.x; pz = b.z; }
	const uint32_t treasure = s->spawn_resource_treasure(px + 9.0f, pz - 3.0f, RES_WOOD);
	auto &u = s->units_;
	auto &f = s->fauna_;
	std::vector<uint32_t> chickens;
	for (size_t i = 0; i < f.count; ++i)
		if (f.alive[i] && f.fauna_type[i] == FAUNA_CHICKEN && f.pos_x[i] < 0.0f) chickens.push_back(f.id[i]);
	CHECK(chickens.size() == 4, "Petra's base has its four chickens");
	bool cav_hunted_deer = false, worker_on_chicken = false, foreign_hunted = false;
	for (float t = 0.0f; t < 90.0f; t += Simulation::FIXED_STEP) {
		s->tick(Simulation::FIXED_STEP);
		for (size_t i = 0; i < u.count; ++i) {
			if (!u.alive[i] || u.team[i] != 1 || u.action_state[i] != ACTION_ATTACK) continue;
			const uint32_t tid = u.target_unit_id[i];
			auto it = s->fauna_id_to_idx_.find(tid);
			if (it == s->fauna_id_to_idx_.end() || s->lookup_unit(tid) >= 0) continue;
			const uint8_t ft = f.fauna_type[it->second];
			if (u.unit_type[i] == UNIT_CAVALRY && ft == FAUNA_DEER) cav_hunted_deer = true;
			if ((u.unit_type[i] == UNIT_VILLAGER || u.unit_type[i] == UNIT_FEMALE_CITIZEN) && ft == FAUNA_CHICKEN) worker_on_chicken = true;
			if (tid == foreign) foreign_hunted = true;
		}
	}
	int chickens_left = 0;
	for (uint32_t id : chickens) {
		auto it = s->fauna_id_to_idx_.find(id);
		if (it != s->fauna_id_to_idx_.end() && f.alive[it->second]) ++chickens_left;
	}
	const bool treasure_taken = !s->is_treasure_id(treasure);
	std::printf("  treasure by Petra's Town Center collected: %s\n", treasure_taken ? "yes" : "no");
	CHECK(treasure_taken, "Petra's gatherers collect a treasure by their base (gatherTreasure)");
	std::printf("  hunting: workers slaughter chickens %s (%d of %zu left after 90s), cavalry hunt deer %s, sheep in enemy land hunted %s\n",
	            worker_on_chicken ? "yes" : "no", chickens_left, chickens.size(), cav_hunted_deer ? "yes" : "no", foreign_hunted ? "yes" : "no");
	CHECK(worker_on_chicken && chickens_left * 2 <= (int)chickens.size(), "food gatherers slaughter the livestock by their dropsite");
	CHECK(cav_hunted_deer, "citizen cavalry hunt animals that flee");
	CHECK(!foreign_hunted, "no one hunts in enemy territory");
}

// worker.js and baseManager.js on 0.28: every FastMoving food gatherer is a hunter
// (the Ptolemaic camel archers too); a chase toward an animal in enemy territory is
// given up; a carcass with all its MaxGatherers taken is full.
static void test_hunters_028() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->set_team_civ(1, CIV_PTOLEMIES);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 1.0f);
	float px = 0.0f, pz = 0.0f, ex = 0.0f, ez = 0.0f;
	for (const auto &b : s->buildings()) {
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { px = b.x; pz = b.z; }
		if (b.team == 0 && b.type == BLD_TOWN_CENTER) { ex = b.x; ez = b.z; }
	}
	uint16_t camel_t = CIV_UNIT_TEMPLATE_NONE;
	for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT; ++t)
		if (std::strcmp(civ_unit_template(t).id, "ptol/cavalry_archer_b") == 0) camel_t = t;
	CHECK(camel_t != CIV_UNIT_TEMPLATE_NONE, "roster has the Nabataean camel archer");
	const uint32_t camel = s->spawn_unit(px + 6.0f, pz + 6.0f, 1, UNIT_CAMEL, 0.0f, camel_t);
	auto &u = s->units_;
	bool camel_hunts = false;
	for (float t = 0.0f; t < 40.0f && !camel_hunts; t += Simulation::FIXED_STEP) {
		s->tick(Simulation::FIXED_STEP);
		const int32_t ci = s->lookup_unit(camel);
		if (ci < 0) break;
		camel_hunts = (u.action_state[ci] == ACTION_ATTACK && s->is_fauna_id(u.target_unit_id[ci])) ||
		              u.res_type_carried[ci] == RES_FOOD;
	}
	std::printf("  camel archer hunts: %s\n", camel_hunts ? "yes" : "no");
	CHECK(camel_hunts, "the Ptolemaic camel archer is a hunter (FastMoving food gatherer)");

	// The camel archer sent after a deer standing in the enemy's land gives it up.
	const uint32_t deer = s->spawn_fauna(ex + 4.0f, ez + 4.0f, FAUNA_DEER);
	uint32_t rider = camel; // a gathering rider (the Ptolemies' default cavalry gather nothing)
	s->order_hunt(&rider, 1, deer);
	s->tick(Simulation::FIXED_STEP);
	s->petra_pid_ = 1;
	Simulation::PetraHuntScan scan = s->petra_hunt_scan();
	s->petra_check_hunt_drift(scan);
	const int32_t ri = s->lookup_unit(rider);
	CHECK(ri >= 0 && u.target_unit_id[ri] != deer, "a chase into enemy territory is given up (worker.js drift check)");

	// A carcass whose MaxGatherers are all taken is not picked.
	const uint32_t meat = s->spawn_resource_world(px + 8.0f, pz - 8.0f, RES_FOOD, 100, RES_SUB_MEAT, 1);
	Simulation::PetraHuntScan full = s->petra_hunt_scan();
	full.on_carcass[meat] = 1;
	bool live = false;
	const int32_t ri2 = s->lookup_unit(rider);
	CHECK(s->petra_find_hunt((size_t)ri2, px + 8.0f, pz - 8.0f, full, live) != meat, "a full carcass is skipped (isSupplyFull)");
	full.on_carcass[meat] = 0;
	CHECK(s->petra_find_hunt((size_t)ri2, px + 8.0f, pz - 8.0f, full, live) == meat, "the same carcass with room is picked");
	// isHuntable: "Do not hunt retaliating animals" (a boar fights back), only their carcasses; a
	// player's sheep in its ally's territory is that ally's to keep (worker.js startHunting).
	{
		Simulation::PetraHuntScan busy = s->petra_hunt_scan();
		busy.on_carcass[meat] = 1;
		const uint32_t boar = s->spawn_fauna(px + 8.5f, pz - 8.0f, FAUNA_BOAR);
		bool l = false;
		CHECK(s->petra_find_hunt((size_t)ri2, px + 8.0f, pz - 8.0f, busy, l) != boar, "a live boar is not hunted (isHuntable)");
		const uint32_t deer2 = s->spawn_fauna(px + 8.2f, pz - 8.0f, FAUNA_DEER);
		CHECK(s->petra_find_hunt((size_t)ri2, px + 8.0f, pz - 8.0f, busy, l) == deer2 && l, "a deer beside it is");
		s->set_player_active(3, true, false);
		s->set_diplomacy(1, 3, Simulation::DIPLO_ALLY);
		s->set_diplomacy(3, 1, Simulation::DIPLO_ALLY);
		s->spawn_building_world(px + 8.0f, pz - 22.0f, BLD_TOWN_CENTER, 3);
		run_for(*s, 0.5f);
		busy = s->petra_hunt_scan();
		busy.on_carcass[meat] = 1;
		const uint32_t sheep = s->spawn_fauna(px + 8.0f, pz - 21.0f, FAUNA_SHEEP, 3);
		const uint32_t own = s->spawn_fauna(px + 8.0f, pz - 20.0f, FAUNA_SHEEP); // a little farther
		const uint32_t pick = s->petra_find_hunt((size_t)ri2, px + 8.0f, pz - 21.0f, busy, l);
		std::printf("  ally's sheep %u, Gaia sheep %u: Petra picks %u\n", sheep, own, pick);
		CHECK(pick != sheep, "an ally's sheep in its territory is left alone");
		for (uint32_t id : {boar, deer2, sheep, own})
			if (auto it = s->fauna_id_to_idx_.find(id); it != s->fauna_id_to_idx_.end()) s->fauna_.alive[it->second] = 0;
		s->set_diplomacy(1, 3, Simulation::DIPLO_ENEMY);
		s->set_diplomacy(3, 1, Simulation::DIPLO_ENEMY);
	}
	// One walled in by blocked cells (deep in a circular map's edge) has no cell to be gathered from.
	{
		auto &g = s->nav_grid_;
		const auto [mx, mz] = g.world_to_cell(px + 8.0f, pz - 8.0f);
		std::vector<std::pair<size_t, uint8_t>> saved;
		for (int dz = -2; dz <= 2; ++dz)
			for (int dx = -2; dx <= 2; ++dx)
				if (g.cells.inside(mx + dx, mz + dz)) {
					saved.push_back({(size_t)(mz + dz) * g.res + (mx + dx), g.cells.at(mx + dx, mz + dz)});
					g.cells.at(mx + dx, mz + dz) = NAV_OBSTRUCTED;
					g.dirty.at(mx + dx, mz + dz) = 1;
				}
		g.rebuild_dirty_regions();
		CHECK(s->petra_find_hunt((size_t)ri2, px + 8.0f, pz - 8.0f, full, live) != meat, "a carcass no cell beside it reaches is skipped");
		for (const auto &[k, v] : saved) { g.cells.data[k] = v; g.dirty.data[k] = 1; }
		g.rebuild_dirty_regions();
	}

	// getOwnDropsites("food") holds the supply wagon; getAnyDropsites adds a mutual ally's
	// shared farmstead only once we have Diaspora (hasSharedDropsites).
	const size_t drops0 = s->petra_hunt_scan().food_drops.size();
	s->spawn_unit(px - 10.0f, pz + 10.0f, 1, UNIT_WAGON);
	CHECK(s->petra_hunt_scan().food_drops.size() == drops0 + 1, "the supply wagon is a food dropsite for hunting");
	s->set_player_active(2, true, false);
	s->set_diplomacy(1, 2, Simulation::DIPLO_ALLY);
	s->set_diplomacy(2, 1, Simulation::DIPLO_ALLY);
	s->spawn_building_world(px - 14.0f, pz - 14.0f, BLD_FARMSTEAD, 2);
	CHECK(s->petra_hunt_scan().food_drops.size() == drops0 + 1, "an ally's farmstead is not ours without Diaspora");
	s->complete_research(1, TECH_UNLOCK_SHARED_DROPSITES);
	CHECK(s->petra_hunt_scan().food_drops.size() == drops0 + 2, "an ally's shared farmstead counts with Diaspora");
}

// startingStrategy.js buildFirstBase: Petra loses its Town Center; the wood for a
// new one is in a treasure by its workers, so it collects that first, then
// founds a civic centre where they stand and builds it.
static void test_first_base() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 1.0f);
	float tx = 0.0f, tz = 0.0f;
	// The rest of its base goes too: without the Town Center's territory it would be
	// decaying to Gaia on neutral land, drawing Petra's workers off the treasure.
	for (auto &b : s->buildings_)
		if (b.team == 1) {
			if (b.type == BLD_TOWN_CENTER) { tx = b.x; tz = b.z; }
			s->destroy_building(b);
		}
	run_for(*s, 0.1f);
	auto &u = s->units_;
	for (size_t i = 0; i < u.count; ++i) // a nomad start: everyone stands idle
		if (u.alive[i] && u.team[i] == 1) { u.action_state[i] = ACTION_IDLE; u.has_order[i] = 0; u.target_resource_id[i] = 0; }
	PlayerState &ps = s->players_[1];
	ps.food() = 0.0f; ps.wood() = 50.0f; ps.gold() = 250.0f; ps.stone() = 300.0f;
	const uint32_t treasure = s->spawn_resource_treasure(tx + 9.0f, tz + 9.0f, RES_WOOD); // 300 wood, outside the wreck
	bool founded = false, built = false;
	float t = 0.0f;
	for (; t < 180.0f && !built; t += Simulation::FIXED_STEP) {
		s->tick(Simulation::FIXED_STEP);
		for (const auto &b : s->buildings())
			if (b.team == 1 && b.type == BLD_TOWN_CENTER && b.hp > 0.0f) { founded = true; built = b.build_progress >= 1.0f; }
	}
	std::printf("  first base: treasure collected %s, new Town Center founded %s and built %s (%.0fs)\n",
	            s->is_treasure_id(treasure) ? "no" : "yes", founded ? "yes" : "no", built ? "yes" : "no", t);
	CHECK(!s->is_treasure_id(treasure), "Petra collects the treasure it needs for a civic centre");
	CHECK(founded && built, "and founds and builds a new Town Center");
}

// tradeManager.js performBarter and setTradingGoods, called as AI 1 between ticks.
static void test_barter() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 1.0f);
	float tx = 0.0f, tz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tx = b.x; tz = b.z; }
	CHECK(!s->petra_perform_barter(), "no barter without a market");
	const uint32_t market = s->spawn_building((int)((tx + 12.0f) / Simulation::GRID), (int)(tz / Simulation::GRID), BLD_MARKET, 1, 1.0f);
	CHECK(market != 0, "a market for AI 1");
	PlayerState &ps = s->players_[1];
	for (auto &a : s->petra_queues_.accounts) a = PetraQueueAccount{};
	for (auto &w : s->petra_queues_.wants) w = PetraQueueAccount{};
	// GetCurrentGatherRates as the economy left them this turn: nobody gathers. The wood income
	// is trade's, which performBarter's rate doesn't count.
	for (float &g : ps.gathered_window) g = 0.0f;
	ps.gathered_window[RES_WOOD] = 1000.0f;
	for (int r = 0; r < RES_COUNT; ++r) s->petra_cur_rate_[r] = s->petra_want_rate_[r] = 0.0f;
	s->petra_rates_stamp_ = s->petra_rates_key();
	ps.food() = 3000.0f; ps.wood() = 0.0f; ps.gold() = 0.0f; ps.stone() = 0.0f;
	s->petra_queues_.wants[PETRA_Q_PLAN].wood = 300.0f; // a plan waits on wood
	const bool necessity = s->petra_perform_barter();
	std::printf("  barter: necessity deal %s, food %.0f wood %.0f\n", necessity ? "yes" : "no", ps.food(), ps.wood());
	CHECK(necessity && ps.wood() > 0.0f && ps.food() < 3000.0f, "sells spare food for the wood a plan waits on, trade income aside");

	s->petra_queues_.wants[PETRA_Q_PLAN] = PetraQueueAccount{};
	ps.food() = 6000.0f; ps.wood() = 2000.0f; ps.gold() = 300.0f; ps.stone() = 2000.0f;
	const float gold_before = ps.gold();
	const bool contingency = s->petra_perform_barter();
	std::printf("  barter: contingency deal %s, gold %.0f -> %.0f\n", contingency ? "yes" : "no", gold_before, ps.gold());
	CHECK(contingency && ps.gold() > gold_before, "sells spare food for the scarcest finite resource");

	// setTradingGoods: a share for low stock (more when wanted), the rest to pickMostNeededResources
	// (wanted against current gather rate): gold is wanted and nobody mines it.
	ps.food() = 300.0f; ps.wood() = 3000.0f; ps.gold() = 50.0f; ps.stone() = 3000.0f;
	s->petra_want_rate_[RES_GOLD] = 1.0f;
	s->petra_cur_rate_[RES_FOOD] = 2.0f;
	s->petra_set_trading_goods();
	const uint8_t *g = s->trading_goods(1);
	std::printf("  trading goods: food %d wood %d stone %d gold %d\n", g[RES_FOOD], g[RES_WOOD], g[RES_STONE], g[RES_GOLD]);
	CHECK(g[RES_FOOD] + g[RES_WOOD] + g[RES_STONE] + g[RES_GOLD] == 100, "trading goods add up to 100");
	CHECK(g[RES_GOLD] >= 20 && g[RES_WOOD] == 0 && g[RES_STONE] == 0, "short on gold, none for what it has plenty of");
}

// queueplanBuilding.js findGoodPosition favorBorder: with no market or dock anywhere to measure
// from (findMarketLocation [-1, -1, -1, 0]), the first market goes anywhere in our bases, +50
// on the map border's tiles, so a later market can be far from it.
// config.js setConfig scales and clamps popPhase2 first; startingStrategy.js then takes 3/4
// of that when the start is short of wood (saveResources).
static void test_pop_phase2_save_resources() {
	auto s = std::make_unique<Simulation>();
	s->reset();
	s->set_petra_difficulty(PETRA_MEDIUM);
	s->petra_pid_ = 1;
	s->petra_config_.pop_phase2 = 150;
	s->pop_cap_ = 300;
	s->petra_attack_mgr_.save_resources = 0;
	CHECK(s->petra_pop_phase2() == 150, "Medium phases at 150 with 300 population");
	s->petra_attack_mgr_.save_resources = 1;
	CHECK(s->petra_pop_phase2() == 112, "saving resources: floor(0.75 * 150)");
	s->pop_cap_ = 50; // floor(150 * sqrt(50/300)) = 61, clamped to 25, then 3/4
	const int low = s->petra_pop_phase2();
	std::printf("popPhase2 at 50 population, saving resources: %d (0.28 18)\n", low);
	CHECK(low == 18, "3/4 of the clamped value under a low population cap");
	// setConfig raised targetNumWorkers (and capped workPhase3) with the uncut popPhase2.
	s->pop_cap_ = 300;
	CHECK(s->petra_target_workers() == 150, "targetNumWorkers: max(100, popPhase2 150), before the cut");
	CHECK(s->petra_work_phase3() == 150, "workPhase3: min(180, targetNumWorkers 150)");
	// handleNewBase stops saving, but popPhase2 stays cut.
	s->petra_attack_mgr_.save_resources = 0;
	s->petra_attack_mgr_.phase2_cut_kept = 1;
	CHECK(s->petra_pop_phase2() == 112, "the 3/4 cut outlives saveResources");
}

// config.js setConfig: Config.popScaling takes sqrt(maxPop/300) under 300, and every helper
// built on it (transport ships, checkPausedQueues' workersMin) uses it. maxPop is
// getPopulationMax at the first turn, with the civ's population bonus.
static void test_petra_config_pop_scaling() {
	auto s = std::make_unique<Simulation>();
	s->reset();
	s->set_petra_difficulty(PETRA_MEDIUM);
	s->petra_pid_ = 1;
	s->players_[1].civ = CIV_ATHENIANS;
	s->pop_cap_ = 300;
	CHECK(std::fabs(s->petra_config_pop_scaling() - 1.0f) < 1e-4f, "no scaling at 300 population");
	s->pop_cap_ = 75;
	const float half = s->petra_config_pop_scaling();
	std::printf("popScaling at 75 population: %.3f (0.28 0.500)\n", half);
	CHECK(std::fabs(half - 0.5f) < 1e-3f, "sqrt(75/300) at 75 population");
	CHECK(std::fabs(s->petra_plan_pop_scaling() - half) < 1e-4f, "attack plans use the same scaling");
	s->players_[1].civ = CIV_PERSIANS;
	s->pop_cap_ = 200;
	std::printf("Persian maxPop at a 200 cap: %d (0.28 220)\n", s->petra_config_max_pop());
	CHECK(s->petra_config_max_pop() == 220, "pers_population raises getPopulationMax by 10%");
}

// startingStrategy.js gameStart: no civic centre and none to build (Death Canyon's invasion
// force): canBuildUnits is false, so no first base; dispatchUnits sends 5 units (half ranged,
// half melee infantry) and every support unit to the ally's civic centre, and attackManager
// starts an Attack at once with every other unit (assignUnits, mustStart).
static void test_petra_no_units() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 3, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	for (int a = 0; a < 3; ++a)
		for (int b = 0; b < 3; ++b)
			if (a != b) s->set_diplomacy(a, b, a == 0 || b == 0 ? Simulation::DIPLO_ENEMY : Simulation::DIPLO_ALLY);
	float x = 0.0f, z = 0.0f;
	for (auto &b : s->buildings_)
		if (b.team == 1) {
			if (b.type == BLD_TOWN_CENTER) { x = b.x; z = b.z; }
			s->destroy_building(b);
		}
	s->triggers_.disabled_buildings[1] |= bld_bit(BLD_TOWN_CENTER);
	for (int k = 0; k < 8; ++k) s->spawn_unit(x + (float)k, z + 3.0f, 1, UNIT_SPEARMAN);
	for (int k = 0; k < 8; ++k) s->spawn_unit(x + (float)k, z + 5.0f, 1, UNIT_ARCHER);
	run_for(*s, 30.0f);
	const PetraKept &kept = s->petra_kept_;
	int alive = 0, support = 0, in_attack = 0, allied_in_attack = 0, allied_soldiers = 0, defending = 0, idle = 0;
	bool cc = false;
	for (size_t i = 0; i < s->units_.count; ++i) {
		if (!s->units_.alive[i] || s->units_.team[i] != 1 || unit_is_ship(s->units_.unit_type[i])) continue;
		++alive;
		const uint32_t id = s->units_.id[i];
		const bool allied = s->petra_dispatched_to_ally(id);
		support += petra_is_support_unit(s->units_.unit_type[i]);
		allied_soldiers += allied && !petra_is_support_unit(s->units_.unit_type[i]);
		if (s->petra_in_attack_squad(id)) { ++in_attack; allied_in_attack += allied; }
		else if (!allied && s->petra_is_defender(id)) ++defending; // a defense army's: plan -2, not available
		else if (!allied && s->units_.order_kind[i] == ORDER_NONE && s->units_.action_state[i] == ACTION_IDLE) ++idle;
	}
	for (const auto &b : s->buildings_) cc |= b.team == 1 && b.hp > 0.0f && is_civic_centre(b.type);
	std::printf("  no units to build: canBuildUnits %s, %zu sent to the ally (%d soldiers, %d support units of ours), "
	            "%d of %d in an attack (%d defending, %d idle), civic centre laid out %s\n",
	            kept.no_units ? "false" : "true", kept.allied.size(), allied_soldiers, support, in_attack, alive,
	            defending, idle, cc ? "yes" : "no");
	CHECK(kept.no_units, "no civic centre and none to build: canBuildUnits false");
	CHECK(!cc, "and no first base");
	CHECK(allied_soldiers == 5 && (int)kept.allied.size() == 5 + support, "5 soldiers and every support unit guard the ally");
	CHECK(allied_in_attack == 0 && idle == 0 && 2 * in_attack > alive - (int)kept.allied.size(),
	      "an attack takes the other units at once, and none stands idle");
	run_for(*s, 60.0f);
	std::printf("  then %zu attack(s) under way\n", s->petra_started_attacks_.size());
	CHECK(!s->petra_started_attacks_.empty(), "and sets off");

	// The choice is saved.
	const std::string path = "/tmp/rtso_petra_no_units.sav";
	CHECK(s->save_state(path.c_str()), "save");
	auto t = std::make_unique<Simulation>();
	CHECK(t->load_state(path.c_str()), "load");
	std::remove(path.c_str());
	CHECK(t->petra_kept_.units_checked && t->petra_kept_.no_units && t->petra_kept_.allied == kept.allied,
	      "canBuildUnits and the allied units survive a save");
}

static void test_first_market_border() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 20.0f);
	s->petra_pid_ = 1;
	float cx = 0.0f, cz = 0.0f;
	for (const auto &b : s->buildings_)
		if (b.team == 1 && is_civic_centre(b.type) && b.build_progress >= 1.0f) { cx = b.x; cz = b.z; }
	const float half = s->map_half(), edge = half - 80.0f / 4.0f;
	auto on_border = [&](float x, float z) {
		return s->circular_map() ? x * x + z * z >= edge * edge : std::abs(x) >= edge || std::abs(z) >= edge;
	};
	// Our territory reaches the border somewhere (the start is near the map's edge).
	bool border_ours = false;
	for (float z = -half; z < half; z += 2.0f)
		for (float x = -half; x < half; x += 2.0f)
			border_ours |= on_border(x, z) && s->territory_owner_at(x, z) == 1;
	s->players_[1].phase = 2; // a market needs the town phase
	s->set_player_resources(1, 50000, 50000, 50000, 50000);
	for (auto &a : s->petra_queues_.accounts) a = {50000, 50000, 50000, 50000};
	for (auto &p : s->petra_queues_.paused) p = 0; // checkPausedQueues counts few workers yet
	s->petra_turn_built_ = false;
	const uint32_t id = s->petra_build_market(cx, cz, PETRA_Q_ECONOMIC_BUILDING);
	const Simulation::Building *m = nullptr;
	for (const auto &b : s->buildings_) if (b.id == id) m = &b;
	std::printf("  first market: %s at (%.1f, %.1f), civic centre (%.1f, %.1f), border in our territory %s\n",
	            m ? "placed" : "not placed", m ? m->x : 0.0f, m ? m->z : 0.0f, cx, cz, border_ours ? "yes" : "no");
	CHECK(border_ours, "our territory reaches the map border");
	CHECK(m && s->territory_owner_at(m->x, m->z) == 1, "the first market is in our territory");
	CHECK(m && on_border(m->x, m->z), "on the map border (favorBorder)");
}

// tradeManager.js prospectForNewMarket with headquarters.js findMarketLocation: with one
// market of its own, Petra looks for the spot in its territory farthest from it and
// builds a second market there when the route would pay; checkRoutes then finds it.
static void test_market_prospection() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 128.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 1.0f);
	float tx = 0.0f, tz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tx = b.x; tz = b.z; }
	// A second civic centre toward the middle of the map widens our territory.
	const float len = std::sqrt(tx * tx + tz * tz);
	s->spawn_building_world(tx - tx / len * 50.0f, tz - tz / len * 50.0f, BLD_TOWN_CENTER, 1, 1.0f);
	run_for(*s, 2.0f);
	const uint32_t first = s->spawn_building((int)((tx + 12.0f) / Simulation::GRID), (int)(tz / Simulation::GRID), BLD_MARKET, 1, 1.0f);
	CHECK(first != 0, "a market for AI 1");
	PlayerState &ps = s->players_[1];
	ps.phase = 2;
	ps.food() = ps.wood() = ps.gold() = ps.stone() = 5000.0f;
	for (auto &a : s->petra_queues_.accounts) a = PetraQueueAccount{};
	for (auto &w : s->petra_queues_.wants) w = PetraQueueAccount{};
	fund_queues(*s); // the queues are funded
	const Simulation::Building *m1 = nullptr;
	for (const auto &b : s->buildings()) if (b.id == first) m1 = &b;
	const float m1x = m1->x, m1z = m1->z;

	float x = 0.0f, z = 0.0f;
	int gain = 0;
	const int where = s->petra_find_market_location(x, z, gain);
	const float d = std::sqrt((x - m1x) * (x - m1x) + (z - m1z) * (z - m1z));
	std::printf("  market location: %d at %.1f from the first market, gain %d (minimal %d)\n", where, d, gain,
	            s->petra_trade_minimal_gain());
	CHECK(where != 0 && s->territory_owner_at(x, z) == 1, "a spot in our own territory");
	CHECK((where == 1) == (gain >= s->petra_trade_minimal_gain()), "kept only when the route would pay");

	Simulation::PetraTradeRoutes routes;
	s->petra_check_routes(routes);
	CHECK(routes.best.gain == 0 && routes.potential.gain == 0, "no route with a single market");
	s->petra_prospect_market(routes);
	uint32_t second = 0;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_MARKET && b.id != first) second = b.id;
	std::printf("  second market %s\n", second ? "placed" : "not placed");
	CHECK((second != 0) == (gain >= s->petra_trade_minimal_gain()), "a second market when the route would pay");
	if (!second) return;
	s->petra_check_routes(routes);
	CHECK(routes.best.gain == 0 && routes.potential.gain >= s->petra_trade_minimal_gain(), "a foundation is only a potential route");
	s->petra_prospect_market(routes);
	int markets = 0;
	for (const auto &b : s->buildings()) if (b.team == 1 && b.type == BLD_MARKET) ++markets;
	CHECK(markets == 2, "one market foundation at a time");
	for (auto &b : s->buildings_) if (b.id == second) { b.build_progress = 1.0f; b.hp = b.max_hp; }
	s->petra_check_routes(routes);
	std::printf("  route gain %d between %u and %u\n", routes.best.gain, routes.best.source, routes.best.target);
	CHECK(routes.best.gain >= s->petra_trade_minimal_gain() &&
	      ((routes.best.source == first && routes.best.target == second) || (routes.best.source == second && routes.best.target == first)),
	      "the two markets make the trade route");
	// isNewMarketWorth: a third market only if it would double the route's gain (or add 20).
	const int third = s->petra_find_market_location(x, z, gain);
	s->petra_prospect_market(routes);
	markets = 0;
	for (const auto &b : s->buildings()) if (b.team == 1 && b.type == BLD_MARKET) ++markets;
	std::printf("  third market spot gain %d: %d markets\n", third == 1 ? gain : 0, markets);
	CHECK((markets == 3) == (third == 1 && (gain >= 2 * routes.potential.gain || gain >= routes.potential.gain + 20)),
	      "a third market only when it would double the route's gain");
}

// checkRoutes: docks are Trade markets too, so a market and a dock on one land make a land
// route, priced for a land trader.
static void test_market_dock_route() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 128.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 1.0f);
	float tx = 0.0f, tz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tx = b.x; tz = b.z; }
	const float len = std::sqrt(tx * tx + tz * tz);
	s->spawn_building_world(tx - tx / len * 50.0f, tz - tz / len * 50.0f, BLD_TOWN_CENTER, 1, 1.0f);
	run_for(*s, 2.0f);
	s->petra_pid_ = 1;
	s->players_[1].phase = 2;
	// The dock first, as on a naval map (population 25, village phase) before the market
	// (town phase): the dock then has the lower id and is the route's source.
	const uint32_t dock = s->spawn_building((int)((tx - tx / len * 45.0f) / Simulation::GRID), (int)((tz - tz / len * 45.0f + 10.0f) / Simulation::GRID),
	                                        BLD_DOCK, 1, 1.0f);
	const uint32_t market = s->spawn_building((int)((tx + 12.0f) / Simulation::GRID), (int)(tz / Simulation::GRID), BLD_MARKET, 1, 1.0f);
	CHECK(market && dock, "a market and a dock for AI 1");
	Simulation::PetraTradeRoutes routes;
	s->petra_check_routes(routes);
	const Simulation::Building *m = nullptr, *d = nullptr;
	for (const auto &b : s->buildings()) { if (b.id == market) m = &b; if (b.id == dock) d = &b; }
	const int land_gain = s->trader_gain_at(m->x - d->x, m->z - d->z, 1, UNIT_TRADER);
	std::printf("  market-dock route: gain %d (land trader %d, minimal %d), land %u\n", routes.potential.gain, land_gain,
	            s->petra_trade_minimal_gain(), routes.potential.land);
	CHECK(land_gain < s->petra_trade_minimal_gain() ||
	      (routes.potential.land != 0 && routes.potential.gain == land_gain), "the market and the dock make a land route");
	CHECK(land_gain >= s->petra_trade_minimal_gain(), "the test spots are far enough apart to pay");

	// trainMoreTraders: the land trader trains at our market, not at the dock ending the route
	// (a dock can't train land traders).
	s->players_[1].food() = s->players_[1].wood() = s->players_[1].gold() = s->players_[1].stone() = 5000.0f;
	fund_queues(*s);
	while (!s->petra_turn_mod(5)) s->ticks_ += 4;
	s->petra_manage_trading(Simulation::FIXED_STEP);
	int at_market = 0, at_dock = 0;
	for (const auto &b : s->buildings())
		for (const auto &o : b.queue)
			if (!o.is_tech() && o.unit_type == UNIT_TRADER) (b.id == market ? at_market : b.id == dock ? at_dock : at_dock) += 1;
	std::printf("  route %u -> %u (dock %u): traders queued at the market %d, elsewhere %d\n", routes.best.source, routes.best.target, dock,
	            at_market, at_dock);
	CHECK(at_market == 1 && at_dock == 0, "the land trader trains at the market");
}

static void test_civ_structures() {
	for (uint8_t civ : { (uint8_t)CIV_KUSHITES, (uint8_t)CIV_ROMANS }) {
		auto s = std::make_unique<Simulation>();
		s->set_sim_seed(42);
		s->reset();
		s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
		s->set_team_civ(1, (uint8_t)civ);
		s->set_victory_condition(VICTORY_ENDLESS);
		bool camp = false, temple = false, champion = false;
		// 40 minutes: at 0 A.D.'s gather rates the city phase comes around minute 15-16, and Petra adds
		// each further advanced structure only past 110 population (Kush's fifth, the temple, near minute 36).
		for (float t = 0.0f; t < 40.0f * 60.0f; t += Simulation::FIXED_STEP) {
			s->tick(Simulation::FIXED_STEP);
			if (s->ticks_ % 20 != 0) continue;
			for (const auto &b : s->buildings()) {
				if (b.team != 1 || b.build_progress < 1.0f || b.wrecked) continue;
				if (b.type == BLD_CAMP_BLEMMYE || b.type == BLD_CAMP_NOBA) camp = true;
				if (b.type == BLD_TEMPLE_AMUN || b.type == BLD_TEMPLE_VESTA) temple = true;
			}
			const auto u = s->units();
			for (size_t i = 0; i < u.count; ++i)
				if (u.alive[i] && u.team[i] == 1 && u.tmpl[i] < CIV_UNIT_TEMPLATE_COUNT &&
				    (civ_unit_template(u.tmpl[i]).flags & ROSTER_CHAMPION)) champion = true;
		}
		// An economy that fills the population cap before the unlock finishes has no room
		// left to train champions in (nothing of Petra's dies once the idle player is gone).
		const bool unlocked = s->players_[1].has_tech(TECH_UNLOCK_CHAMPION_INFANTRY) || s->players_[1].has_tech(TECH_UNLOCK_CHAMPION_CAVALRY);
		// Capped as petra_pop_room sees it: no room left once the batches already queued start
		// (199 of 200 with one more cavalryman queued at a stable is as full as 200).
		int room = s->pop_max(1) - s->pop_current(1);
		for (const auto &b : s->buildings())
			if (b.team == 1)
				for (const auto &o : b.queue)
					if (!o.started) room -= s->order_pop(o, 1);
		const bool capped = room <= 0;
		std::printf("  %s by 40 min: camp %s, temple %s, champions %s (unlocked %s, population at its cap %s)\n",
		            civ_info((Civilization)civ).name, camp ? "yes" : "no", temple ? "yes" : "no", champion ? "yes" : "no",
		            unlocked ? "yes" : "no", capped ? "yes" : "no");
		if (civ == CIV_KUSHITES) CHECK(camp && temple, "Kush builds its camps and the Temple of Amun");
		else CHECK(temple && (champion || (unlocked && capped)), "Rome builds the Temple of Vesta and unlocks its champions");
	}
}

// emergencyManager.js: a lost civil centre is an emergency Petra tells its
// allies (only them) about; a new one ends it. Losing units alone is not.
static void test_emergency() {
	auto s = three_player_match(7);
	s->set_victory_condition(VICTORY_ENDLESS);
	s->set_mutual_ally(1, 2);
	run_for(*s, 3.0f);
	// Most of AI 1's units die, its structures stand: no emergency (both must fall).
	auto &u = s->units_;
	int killed = 0, own = 0;
	for (size_t i = 0; i < u.count; ++i) if (u.alive[i] && u.team[i] == 1) ++own;
	for (size_t i = 0; i < u.count && killed * 10 < own * 7; ++i)
		if (u.alive[i] && u.team[i] == 1) { u.hp[i] = 0.0f; ++killed; }
	run_for(*s, 2.0f);
	const bool units_only = s->petra_in_emergency();
	// Its Town Center falls: the only territory root.
	float tx = 0.0f, tz = 0.0f;
	for (auto &b : s->buildings_)
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { b.hp = 0.0f; tx = b.x; tz = b.z; }
	s->clear_petra_chat();
	bool entered = false, told_ally = false, told_foe = false;
	for (float t = 0.0f; t < 3.0f && !entered; t += Simulation::FIXED_STEP) {
		s->tick(Simulation::FIXED_STEP);
		entered = s->petra_in_emergency();
	}
	told_ally = said_to(*s, 1, 2, PETRA_CHAT_EMERGENCY);
	told_foe = said_to(*s, 1, 0, PETRA_CHAT_EMERGENCY) || said_to(*s, 1, 0xFF, PETRA_CHAT_EMERGENCY);
	// A new civil centre is a root more than when the emergency began.
	s->clear_petra_chat();
	s->spawn_building((int)((tx + 20.0f) / Simulation::GRID), (int)(tz / Simulation::GRID), BLD_TOWN_CENTER, 1, 1.0f);
	bool left = false;
	for (float t = 0.0f; t < 3.0f && !left; t += Simulation::FIXED_STEP) {
		s->tick(Simulation::FIXED_STEP);
		left = !s->petra_in_emergency();
	}
	std::printf("  emergency: units only %s; TC lost %s (ally told %s, enemy %s); new TC ends it %s\n",
		units_only ? "yes" : "no", entered ? "yes" : "no", told_ally ? "yes" : "no", told_foe ? "yes" : "no", left ? "yes" : "no");
	CHECK(!units_only, "losing units while the structures stand is no emergency");
	CHECK(entered && told_ally && !told_foe, "losing the last root is an emergency told to allies only");
	CHECK(left && said_to(*s, 1, 2, PETRA_CHAT_EMERGENCY_EXIT), "a new root ends it");
}

// victoryManager.js in regicide: soldiers guard the hero; attacked below 70%
// health it goes to heal in a temple, and the guards stay out of attacks.
static void test_regicide_hero() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_REGICIDE);
	s->set_regicide_garrison(true); // victoryManager.js garrisons the hero only when it is garrisonable
	run_for(*s, 1.0f);
	auto &u = s->units_;
	uint32_t hero = 0;
	for (size_t i = 0; i < u.count; ++i)
		if (u.alive[i] && u.team[i] == 1 && u.unit_type[i] == UNIT_HERO) hero = u.id[i];
	CHECK(hero != 0, "regicide gives AI 1 a hero");
	if (!hero) return;
	float tx = 0.0f, tz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tx = b.x; tz = b.z; }
	std::vector<uint32_t> champs;
	for (int i = 0; i < 2; ++i) champs.push_back(s->spawn_unit(tx + 10.0f + 3.0f * (float)i, tz + 10.0f, 1, UNIT_ELEPHANT));
	run_ai_turns(*s, 10); // victoryManager.js: guards every 10th AI turn
	int guarding = 0;
	for (uint32_t id : champs) {
		const int32_t c = s->lookup_unit(id);
		if (c >= 0 && u.order_kind[c] == ORDER_GUARD && u.order_target[c] == hero) ++guarding;
	}
	// A temple across the Town Center; the hero is hurt and under attack.
	const uint32_t temple = s->spawn_building((int)((tx - 14.0f) / Simulation::GRID), (int)(tz / Simulation::GRID), BLD_TEMPLE, 1, 1.0f);
	const int32_t h = s->lookup_unit(hero);
	const uint32_t foe = s->spawn_unit(u.pos_x[h] + 3.0f, u.pos_z[h], 0, UNIT_SWORDSMAN);
	s->order_attack(&foe, 1, hero);
	bool healing = false;
	for (float t = 0.0f; t < 15.0f && !healing; t += Simulation::FIXED_STEP) {
		const int32_t hh = s->lookup_unit(hero);
		if (hh < 0 || !u.alive[hh]) break;
		u.hp[hh] = u.max_hp[hh] * 0.6f;
		s->tick(Simulation::FIXED_STEP);
		(void)temple;
		for (const auto &b : s->buildings())
			if (b.team == 1 && s->structure_garrison_heal(b) > 0.0f &&
			    std::find(b.garrisoned_units.begin(), b.garrisoned_units.end(), hero) != b.garrisoned_units.end()) healing = true;
	}
	std::printf("  regicide: %d of 2 champions guard the hero; hurt and attacked, it heals inside: %s\n", guarding, healing ? "yes" : "no");
	CHECK(guarding == 2, "champions guard the regicide hero");
	CHECK(healing, "an attacked hero below 70% health garrisons in a healing structure");
}

// victoryManager.js reacts to every Attacked event on the hero: a hurt hero
// under a tower's arrows, with no unit targeting it, still goes to heal.
static void test_regicide_hero_tower_fire() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_REGICIDE);
	s->set_regicide_garrison(true);
	run_for(*s, 1.0f);
	auto &u = s->units_;
	const uint32_t hero = s->regicide_hero(1);
	CHECK(hero != 0, "regicide gives AI 1 a hero");
	if (!hero) return;
	float tx = 0.0f, tz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tx = b.x; tz = b.z; }
	const float hx = tx * 0.6f, hz = tz * 0.6f;
	int32_t h = s->lookup_unit(hero);
	u.pos_x[h] = hx; u.pos_z[h] = hz;
	const uint32_t stance_hero = hero;
	s->order_set_stance(&stance_hero, 1, STANCE_STAND_GROUND);
	s->spawn_building_world(hx + 5.0f, hz + 5.0f, BLD_TOWER, 0, 1.0f);
	bool hit = false, reacted = false;
	for (float t = 0.0f; t < 12.0f && !reacted; t += Simulation::FIXED_STEP) {
		h = s->lookup_unit(hero);
		if (h < 0 || !u.alive[h]) break;
		if (u.action_state[h] != ACTION_GARRISONED) u.hp[h] = u.max_hp[h] * 0.6f;
		s->tick(Simulation::FIXED_STEP);
		h = s->lookup_unit(hero);
		if (h < 0) break;
		hit |= u.attacked_by[h] != 0;
		reacted = u.order_kind[h] == ORDER_GARRISON || u.action_state[h] == ACTION_GARRISONED || u.stance[h] == STANCE_PASSIVE;
	}
	std::printf("  regicide hero under tower fire: hit %s, retreats %s\n", hit ? "yes" : "no", reacted ? "yes" : "no");
	CHECK(hit && reacted, "a hurt hero under tower fire retreats to heal");
}

// With RegicideGarrison off the hero can't garrison: victoryManager.js
// pickCriticalEntRetreatLocation sends it home on foot instead (no garrison
// order that is refused and retried every turn).
static void test_regicide_hero_no_garrison() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_REGICIDE);
	s->set_regicide_garrison(false);
	run_for(*s, 1.0f);
	auto &u = s->units_;
	uint32_t hero = 0;
	for (size_t i = 0; i < u.count; ++i)
		if (u.alive[i] && u.team[i] == 1 && u.unit_type[i] == UNIT_HERO) hero = u.id[i];
	CHECK(hero != 0, "regicide gives AI 1 a hero");
	if (!hero) return;
	float tx = 0.0f, tz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tx = b.x; tz = b.z; }
	// Out in the field toward the map centre, hurt and attacked.
	const float hx = tx * 0.55f, hz = tz * 0.55f;
	int32_t h = s->lookup_unit(hero);
	u.pos_x[h] = hx; u.pos_z[h] = hz;
	const uint32_t foe = s->spawn_unit(hx + 3.0f, hz, 0, UNIT_SWORDSMAN);
	s->order_attack(&foe, 1, hero);
	int garrison_orders = 0;
	bool inside = false;
	for (float t = 0.0f; t < 12.0f; t += Simulation::FIXED_STEP) {
		h = s->lookup_unit(hero);
		if (h < 0 || !u.alive[h]) break;
		u.hp[h] = u.max_hp[h] * 0.6f;
		s->tick(Simulation::FIXED_STEP);
		h = s->lookup_unit(hero);
		if (h >= 0 && u.order_kind[h] == ORDER_GARRISON) ++garrison_orders;
		if (h >= 0 && u.action_state[h] == ACTION_GARRISONED) inside = true;
	}
	h = s->lookup_unit(hero);
	const float d0 = std::hypot(hx - tx, hz - tz);
	const float d1 = h >= 0 ? std::hypot(u.pos_x[h] - tx, u.pos_z[h] - tz) : d0;
	std::printf("  regicide, no hero garrison: garrison orders %d, inside %s, distance to its Town Center %.1f -> %.1f\n",
	            garrison_orders, inside ? "yes" : "no", d0, d1);
	CHECK(!inside && garrison_orders == 0, "an ungarrisonable hero gets no garrison order");
	CHECK(d1 < d0 - 5.0f, "it walks home instead");
}

// Nomad (game setup): no civic centre at the start, so startingStrategy.js
// buildFirstBase founds one where the workers stand and the economy carries on.
static void test_nomad_first_base() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->set_nomad(true);
	// Seated players: placePlayersNomad leaves each one the treasure its civic centre needs.
	s->set_player_active(0, true, false);
	s->set_player_active(1, true, true);
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	auto town_centers = [&](bool built) {
		int n = 0;
		for (const auto &b : s->buildings())
			if (b.team == 1 && b.type == BLD_TOWN_CENTER && !b.wrecked && (!built || b.build_progress >= 1.0f)) ++n;
		return n;
	};
	const int at_start = town_centers(false);
	// The idle player's Nomad starting army stands close by on this small map; it holds
	// its fire, so the check measures Petra founding its base, not an early skirmish.
	std::vector<uint32_t> idle_army;
	for (size_t i = 0; i < s->units_.count; ++i)
		if (s->units_.alive[i] && s->units_.team[i] == 0) idle_army.push_back(s->units_.id[i]);
	s->order_set_stance(idle_army.data(), idle_army.size(), STANCE_PASSIVE);
	run_for(*s, 150.0f);
	int buildings = 0, houses = 0;
	for (const auto &b : s->buildings())
		if (b.team == 1 && !b.wrecked && b.build_progress >= 1.0f) {
			++buildings;
			houses += b.type == BLD_HOUSE;
		}
	std::printf("  nomad: Town Centers at start %d, built after 150 s %d, finished buildings %d, workers %d\n",
	            at_start, town_centers(true), buildings, count_units(*s, 1, true));
	CHECK(at_start == 0, "a Nomad start has no civic centre");
	CHECK(town_centers(true) == 1, "Petra founds and builds its first Town Center");
	// startingStrategy.js lays no opening houses: buildMoreHouses adds one once houseNeeded asks.
	CHECK(buildings >= 2 && houses >= 1, "and builds on from it");
}

// EntityLimits (D's #159): a Mauryan Pillar only stands while Ashoka lives.
// Petra asks before it searches for a spot, so it neither lays one out nor
// scans the map for one every turn (it did ~200k refused cells a game).
static void test_entity_limit_placement() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_team_civ(1, (uint8_t)CIV_MAURYANS);
	run_for(*s, 1.0f);
	float tx = 0.0f, tz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tx = b.x; tz = b.z; }
	const int pillar_foot = building_type_info(BLD_PILLAR).footprint;
	int free_cells = 0, pillar_ok = 0;
	for (int dx = -8; dx <= 8; ++dx)
		for (int dz = -8; dz <= 8; ++dz) {
			const int cx = (int)(tx / Simulation::GRID) + dx, cz = (int)(tz / Simulation::GRID) + dz;
			if (s->petra_can_place(cx, cz, pillar_foot, BLD_HOUSE)) ++free_cells;
			if (s->petra_can_place(cx, cz, pillar_foot, BLD_PILLAR)) ++pillar_ok;
		}
	const size_t before = s->buildings().size();
	const uint32_t placed = s->petra_place_building(BLD_PILLAR, tx, tz, 2, 16, PETRA_Q_MILITARY_BUILDING);
	std::printf("  entity limits: Pillar limit %d without Ashoka, %d free cells, Pillar placeable on %d, placed %s\n",
	            s->entity_limit(1, LIMIT_PILLAR), free_cells, pillar_ok, placed ? "yes" : "no");
	CHECK(s->entity_limit(1, LIMIT_PILLAR) == 0, "no Pillar without Ashoka");
	CHECK(free_cells > 0 && pillar_ok == 0, "Petra's placement refuses a limited structure everywhere");
	CHECK(!placed && s->buildings().size() == before, "and lays none out");
}

// attackPlan.js answers Attacked events at once: a soldier hacking at a
// structure turns on the unit that hits it, instead of standing under fire
// until the plan's next retarget (25 s here).
static void test_army_answers_attackers() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 1.0f);
	float tx = 0.0f, tz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tx = b.x; tz = b.z; }
	const float hx = -tx * 0.3f, hz = -tz * 0.3f; // the enemy's side of the map
	const uint32_t house = s->spawn_building_world(hx, hz, BLD_HOUSE, 0, 1.0f);
	float half = -1.0f;
	for (const auto &b : s->buildings()) if (b.id == house) half = b.half_size;
	CHECK(half > 0.0f, "an enemy house to attack");
	if (half <= 0.0f) return;
	// Clear player 0's own units away, so the only attacker is the one below.
	for (size_t j = 0; j < s->units_.count; ++j)
		if (s->units_.alive[j] && s->units_.team[j] == 0) { s->units_.alive[j] = 0; s->units_.action_state[j] = ACTION_DYING; }
	run_for(*s, 0.5f);
	std::vector<uint32_t> squad;
	// Sturdy enough that player 0's own units nearby can't break up the squad first.
	for (int k = 0; k < 3; ++k) squad.push_back(s->spawn_unit(hx + half + 1.0f, hz + 1.5f * (float)k, 1, UNIT_SPEARMAN, 1.0e4f));
	s->order_attack_building(squad.data(), squad.size(), house);
	auto &plan = s->petra_attack_plan_;
	plan.squad = squad;
	plan.initial_squad_size = squad.size();
	plan.target_building_id = house;
	plan.target_x = hx; plan.target_z = hz;
	plan.state = Simulation::AttackPlan::ATTACKED;
	plan.state_timer = 0.0f;
	run_for(*s, 2.0f);
	const uint32_t foe = s->spawn_unit(hx + half + 3.0f, hz, 0, UNIT_SWORDSMAN, 1.0e6f);
	s->order_attack(&foe, 1, squad[0]);
	run_for(*s, 4.0f);
	const int32_t i = s->lookup_unit(squad[0]);
	const bool turned = i >= 0 && s->units().alive[i] && s->units_.target_unit_id[i] == foe;
	int helpers = 0; // attackPlan.js: the two nearest squad mates join in
	for (size_t k = 1; k < squad.size(); ++k) {
		const int32_t j = s->lookup_unit(squad[k]);
		if (j >= 0 && s->units_.target_unit_id[j] == foe) ++helpers;
	}
	std::printf("  army under attack at a structure: the soldier hit turns on its attacker: %s, helpers %d (plan state %d)\n",
	            turned ? "yes" : "no", helpers, (int)plan.state);
	CHECK(turned, "a Petra soldier attacked while at a structure fights back at once");
	CHECK(helpers == 2, "its two nearest squad mates turn on the attacker too");
}

// attackPlan.js update: an attack's units pick their targets among the targetPlayer's
// units and structures (getEnemyUnits/getEnemyStructures(targetPlayer)), so an army sent at
// one enemy doesn't drift onto a third party standing nearer.
static void test_attack_targets_target_player() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 1.0f);
	float tx = 0.0f, tz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tx = b.x; tz = b.z; }
	const float hx = -tx * 0.3f, hz = -tz * 0.3f;
	for (size_t j = 0; j < s->units_.count; ++j)
		if (s->units_.alive[j] && s->units_.team[j] == 0) { s->units_.alive[j] = 0; s->units_.action_state[j] = ACTION_DYING; }
	run_for(*s, 0.5f);
	std::vector<uint32_t> squad;
	for (int k = 0; k < 3; ++k) {
		squad.push_back(s->spawn_unit(hx, hz + 1.5f * (float)k, 1, UNIT_SPEARMAN, 1.0e4f));
		s->units_.stance[s->lookup_unit(squad.back())] = STANCE_STAND_GROUND; // only Petra's orders move them
	}
	const uint32_t third = s->spawn_unit(hx + 6.0f, hz + 1.5f, 2, UNIT_SWORDSMAN, 1.0e6f);
	const uint32_t foe = s->spawn_unit(hx - 9.0f, hz + 1.5f, 0, UNIT_SWORDSMAN, 1.0e6f);
	for (uint32_t id : {third, foe}) s->units_.stance[s->lookup_unit(id)] = STANCE_PASSIVE;
	auto &plan = s->petra_attack_plan_;
	plan.squad = squad;
	plan.initial_squad_size = squad.size();
	plan.target_player = 0;
	plan.target_building_id = s->spawn_building_world(hx - 30.0f, hz, BLD_HOUSE, 0, 1.0f);
	plan.target_x = hx - 30.0f; plan.target_z = hz;
	plan.state = Simulation::AttackPlan::ATTACKED;
	plan.state_timer = 0.0f;
	run_ai_turns(*s, 2);
	int on_foe = 0, on_third = 0;
	for (uint32_t id : squad) {
		const int32_t i = s->lookup_unit(id);
		if (i < 0) continue;
		on_foe += s->units_.target_unit_id[i] == foe;
		on_third += s->units_.target_unit_id[i] == third;
	}
	std::printf("  attack on player 0: %d soldiers on its swordsman, %d on player 2's nearer one\n", on_foe, on_third);
	CHECK(on_third == 0 && on_foe > 0, "an attack's units fight only the target player's units (getEnemyUnits(targetPlayer))");
}

// attackPlan.js update: a unit that can attack walls goes for a Gate-class structure first
// (+10000), the palisade and siege wall gates as much as the stone one.
static void test_siege_prefers_any_gate() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 1.0f);
	float tx = 0.0f, tz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tx = b.x; tz = b.z; }
	const float hx = -tx * 0.3f, hz = -tz * 0.3f;
	for (size_t j = 0; j < s->units_.count; ++j)
		if (s->units_.alive[j] && s->units_.team[j] == 0) { s->units_.alive[j] = 0; s->units_.action_state[j] = ACTION_DYING; }
	run_for(*s, 0.5f);
	const uint32_t gate = s->spawn_building_world(hx + 8.0f, hz, BLD_PALISADE_GATE, 0, 1.0f);
	const uint32_t house = s->spawn_building_world(hx - 8.0f, hz, BLD_HOUSE, 0, 1.0f);
	const uint32_t ram = s->spawn_unit(hx, hz + 4.0f, 1, UNIT_SIEGE_RAM, 1.0e5f);
	s->units_.stance[s->lookup_unit(ram)] = STANCE_STAND_GROUND; // only Petra's orders move it
	auto &plan = s->petra_attack_plan_;
	plan.squad = {ram};
	plan.initial_squad_size = 1;
	plan.target_player = 0;
	plan.target_building_id = house;
	plan.target_x = hx; plan.target_z = hz;
	plan.state = Simulation::AttackPlan::ATTACKED;
	plan.state_timer = 0.0f;
	run_ai_turns(*s, 2);
	const int32_t r = s->lookup_unit(ram);
	const uint32_t on = r >= 0 ? s->units_.target_building_id[r] : 0;
	std::printf("  a ram by a palisade gate and a house goes for the %s\n", on == gate ? "gate" : on == house ? "house" : "neither");
	CHECK(on == gate, "a palisade gate draws a wall-breaker first (Gate class +10000)");
}

// attackPlan.js checkTargetObstruction / isBlocked: a target walled in on the army's land
// becomes the wall piece in the way, and soldiers without siege hack at it.
static void test_attack_blocked_by_walls() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 1.0f);
	float tx = 0.0f, tz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tx = b.x; tz = b.z; }
	const float hx = -tx * 0.3f, hz = -tz * 0.3f;
	for (size_t j = 0; j < s->units_.count; ++j)
		if (s->units_.alive[j] && s->units_.team[j] == 0) { s->units_.alive[j] = 0; s->units_.action_state[j] = ACTION_DYING; }
	run_for(*s, 0.5f);
	const uint32_t house = s->spawn_building_world(hx, hz, BLD_HOUSE, 0, 1.0f);
	// A closed palisade round the house (one-cell pieces, as maps place them).
	const float r = 7.0f;
	size_t laid = 0;
	for (float d = -r; d < r + 0.01f; d += 1.5f)
		for (const auto &p : {std::pair{hx + d, hz - r}, std::pair{hx + d, hz + r}, std::pair{hx - r, hz + d}, std::pair{hx + r, hz + d}})
			laid += s->spawn_building_world(p.first, p.second, BLD_PALISADE, 0, 1.0f) != 0;
	s->rebuild_nav_grid();
	const float ox = hx + (tx - hx) * 0.5f, oz = hz + (tz - hz) * 0.5f; // the army, outside
	float bx = 0.0f, bz = 0.0f;
	const uint32_t to = s->petra_target_obstruction(house, ox, oz, bx, bz);
	const auto it = s->building_id_to_idx_.find(to);
	const bool wall = to != house && it != s->building_id_to_idx_.end() && is_wall_piece(s->buildings_[it->second].type);
	std::printf("  %zu wall pieces round a house: the target becomes %s\n", laid, wall ? "a wall piece" : to == house ? "still the house" : "something else");
	CHECK(laid >= 8, "the wall closes round the house");
	CHECK(wall, "the wall piece in the way becomes the target (checkTargetObstruction)");
	// The house in the open is reached as it is.
	const uint32_t open = s->spawn_building_world(ox + 6.0f, oz, BLD_HOUSE, 0, 1.0f);
	CHECK(s->petra_target_obstruction(open, ox, oz, bx, bz) == open, "an open target stays the target");
	// Spearmen with the wall as target attack it.
	std::vector<uint32_t> squad;
	for (int k = 0; k < 4; ++k) {
		const uint32_t u = s->spawn_unit(bx + (ox - bx) * 0.2f + (float)k, bz + (oz - bz) * 0.2f, 1, UNIT_SPEARMAN, 1.0e5f);
		s->units_.stance[s->lookup_unit(u)] = STANCE_PASSIVE; // only Petra's orders: no wall piece of their own pick
		squad.push_back(u);
	}
	auto &plan = s->petra_attack_plan_;
	plan.squad = squad;
	plan.initial_squad_size = squad.size();
	plan.target_player = 0;
	plan.target_building_id = to;
	plan.target_x = bx; plan.target_z = bz;
	plan.state = Simulation::AttackPlan::ATTACKED;
	plan.state_timer = 0.0f;
	run_ai_turns(*s, 2);
	int on_wall = 0;
	for (uint32_t u : squad) {
		const int32_t i = s->lookup_unit(u);
		on_wall += i >= 0 && s->units_.target_building_id[i] == to;
	}
	std::printf("  %d of 4 spearmen attack the blocking wall\n", on_wall);
	CHECK(on_wall >= 3, "blocked soldiers hack at the wall (isBlocked: ent.attack(target))");
}

// attackPlan.js "Raid": FastMoving minSize 3 and targetSize 4 vary like any plan's (x0.8-1.2,
// x0.2 below Easy), and each plan made counts toward the next one's name (totalNumber).
static void test_raid_sizes_vary() {
	for (uint8_t d : {(uint8_t)PETRA_VERY_EASY, (uint8_t)PETRA_MEDIUM}) {
		auto s = std::make_unique<Simulation>();
		s->set_sim_seed(42);
		s->reset();
		s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
		s->set_victory_condition(VICTORY_ENDLESS);
		s->set_petra_difficulty(d);
		run_for(*s, 1.0f);
		float tx = 0.0f, tz = 0.0f;
		for (const auto &b : s->buildings())
			if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tx = b.x; tz = b.z; }
		s->spawn_building_world(tx + 10.0f, tz, BLD_OUTPOST, 0, 1.0f); // an enemy structure in Petra's land
		const uint8_t made = s->petra_attack_mgr_.plans_made;
		for (int k = 0; k < 20 && s->petra_raid_plan_.state != Simulation::AttackPlan::MUSTER; ++k) run_for(*s, 1.0f);
		const auto &m = s->petra_attack_mgr_;
		std::printf("  raid at difficulty %d: target %d, min %d, plans made %d\n", (int)d, (int)m.raid_target, (int)m.raid_min, (int)m.plans_made);
		CHECK(s->petra_raid_plan_.state == Simulation::AttackPlan::MUSTER, "an enemy structure in Petra's land starts a raid");
		if (d == PETRA_MEDIUM) CHECK(m.raid_target >= 4 && m.raid_target <= 5 && m.raid_min == 3, "a Medium raid wants 4-5 riders, 3 at least");
		else CHECK(m.raid_target == 1 && m.raid_min == 1, "a Very Easy raid wants one rider");
		CHECK(m.plans_made > made, "the raid counts as a plan made (totalNumber)");
	}
}

// defenseManager.js assignDefenders: soldiers of a plan still mustering defend the base
// and leave the plan; those of a started attack are held back.
static void test_mustering_units_defend() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 1.0f);
	float tx = 0.0f, tz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tx = b.x; tz = b.z; }
	for (size_t j = 0; j < s->units_.count; ++j)
		if (s->units_.alive[j] && (s->units_.team[j] == 0 || (s->units_.team[j] == 1 && !petra_is_support_unit(s->units_.unit_type[j])))) {
			s->units_.alive[j] = 0;
			s->units_.action_state[j] = ACTION_DYING;
		}
	run_for(*s, 0.5f);
	// Out of the Town Center's reach, so its garrison doesn't take the soldiers first.
	const float len = std::sqrt(tx * tx + tz * tz), ux = -tx / len, uz = -tz / len;
	const float hx = tx + ux * 26.0f, hz = tz + uz * 26.0f;
	s->spawn_building_world(hx, hz, BLD_HOUSE, 1, 1.0f);
	const uint32_t mustering = s->spawn_unit(hx - ux * 4.0f, hz - uz * 4.0f, 1, UNIT_SPEARMAN, 1.0e4f);
	const uint32_t marching = s->spawn_unit(hx - ux * 4.0f + uz * 2.0f, hz - uz * 4.0f - ux * 2.0f, 1, UNIT_SPEARMAN, 1.0e4f);
	auto &mgr = s->petra_attack_mgr_;
	mgr.rushes_set = 1;
	mgr.max_rushes = 0;
	mgr.target_size = 20;
	mgr.min_size = 20;
	auto &plan = s->petra_attack_plan_;
	plan.kind = PETRA_ATTACK_ATTACK;
	plan.squad = {mustering};
	plan.state = Simulation::AttackPlan::MUSTER;
	Simulation::AttackPlan started;
	started.kind = PETRA_ATTACK_ATTACK;
	started.state = Simulation::AttackPlan::WALKING;
	started.squad = {marching};
	started.initial_squad_size = 1;
	started.staging_x = started.target_x = -tx;
	started.staging_z = started.target_z = -tz;
	s->petra_started_attacks_.push_back(started);
	const uint32_t foe = s->spawn_unit(hx + ux * 3.0f, hz + uz * 3.0f, 0, UNIT_SWORDSMAN, 1.0e4f);
	(void)foe;
	run_for(*s, 3.0f);
	const auto def = all_defenders(*s);
	const bool m_def = std::find(def.begin(), def.end(), mustering) != def.end();
	const bool w_def = std::find(def.begin(), def.end(), marching) != def.end();
	const bool m_plan = std::find(plan.squad.begin(), plan.squad.end(), mustering) != plan.squad.end();
	std::printf("  mustering soldier defends %s (left its plan %s), marching soldier defends %s\n",
	            m_def ? "yes" : "no", m_plan ? "no" : "yes", w_def ? "yes" : "no");
	CHECK(m_def && !m_plan, "a soldier of a plan still mustering defends and leaves the plan");
	CHECK(!w_def, "a soldier of a started attack keeps marching");
}

// attackManager.js checkEvents AttackRequest: an allied AI with more than 12 soldiers
// mustering turns its plan on the requested enemy and marches at once; one with too few
// says its next attack will target that enemy.
static void test_attack_request() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(7);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 7, 4, 128.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	s->set_player_active(0, true, false);
	s->set_player_active(1, true, true);
	s->set_player_active(2, true, true);
	s->set_player_active(3, true, false);
	for (uint8_t a = 0; a < 4; ++a)
		for (uint8_t b = 0; b < 4; ++b)
			if (a != b) s->set_diplomacy(a, b, a == 0 || b == 0 ? Simulation::DIPLO_ENEMY : Simulation::DIPLO_ALLY);
	run_for(*s, 5.0f);
	CHECK(s->petra_slots_.size() > 2 && s->petra_slots_[2].initialized, "AI 2 runs its own Petra");
	if (s->petra_slots_.size() <= 2) return;
	float bx = 0.0f, bz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 2 && b.type == BLD_TOWN_CENTER) { bx = b.x; bz = b.z; }
	auto &slot = s->petra_slots_[2];
	auto &plan = slot.petra_attack_plan_;
	plan.kind = PETRA_ATTACK_ATTACK;
	plan.state = Simulation::AttackPlan::MUSTER;
	plan.squad.clear();
	for (int k = 0; k < 14; ++k) plan.squad.push_back(s->spawn_unit(bx + 6.0f + (float)(k % 5), bz + 6.0f + (float)(k / 5), 2, UNIT_SPEARMAN));
	s->cheat_mods_[2].pop_bonus = 200; // room to grow: a plan at the population cap starts at once (attackPlan.js lengthMin)
	slot.petra_attack_mgr_.kind = PETRA_ATTACK_ATTACK;
	slot.petra_attack_mgr_.target_size = 60;
	slot.petra_attack_mgr_.min_size = 30;
	s->clear_petra_chat();
	s->cmd_attack_request(3, 0);
	run_ai_turns(*s, 1, 2); // each AI answers on its own turn (AI 2's comes first)
	run_ai_turns(*s, 1, 1);
	bool requested_notice = false, join = false, decline = false;
	for (const auto &line : s->pending_petra_chat()) {
		requested_notice |= line.kind == PETRA_CHAT_ATTACK_REQUESTED && line.from == 3 && line.to == PETRA_CHAT_ALLIES;
		join |= line.kind == PETRA_CHAT_ATTACK_JOIN && line.from == 2;
		decline |= line.kind == PETRA_CHAT_ATTACK_DECLINE && line.from == 1;
	}
	const auto &prep1 = s->petra_attack_plan();
	std::printf("  attack request from player 3 against 0: notice %s, AI 2 (14 mustering) joins %s (forced %d, target player %d), AI 1 declines %s (next target %d)\n",
	            requested_notice ? "yes" : "no", join ? "yes" : "no", (int)slot.petra_attack_plan_.forced,
	            (int)slot.petra_attack_plan_.target_player, decline ? "yes" : "no", (int)prep1.target_player);
	CHECK(requested_notice, "the request is announced to the asker's allies");
	CHECK(join && slot.petra_attack_plan_.forced && slot.petra_attack_plan_.requested && slot.petra_attack_plan_.target_player == 0,
	      "an AI with more than 12 soldiers mustering joins against the requested enemy");
	CHECK(decline && prep1.target_player == 0, "an AI with too few soldiers declines and turns its next attack on that enemy");
	s->clear_petra_chat();
	bool marched = false;
	for (float t = 0.0f; t < 10.0f && !marched; t += Simulation::FIXED_STEP) {
		s->tick(Simulation::FIXED_STEP);
		for (const auto &p : s->petra_slots_[2].petra_started_attacks_)
			marched |= p.requested && p.state == Simulation::AttackPlan::WALKING;
		marched |= s->petra_slots_[2].petra_attack_plan_.requested && s->petra_slots_[2].petra_attack_plan_.completing; // gathering to set off
	}
	bool asked_again = false;
	for (const auto &line : s->pending_petra_chat()) asked_again |= line.kind == PETRA_CHAT_ATTACK_REQUESTED && line.from == 2;
	std::printf("  AI 2's requested attack marches: %s, asks its allies again: %s\n", marched ? "yes" : "no", asked_again ? "yes" : "no");
	CHECK(marched, "the requested attack marches at once");
	CHECK(!asked_again, "a requested attack asks no one to join it");
}

// attackPlan.js getPathToTarget, setRallyPoint, updatePreparation "completing" and
// UpdateWalking: a land army gathers at a rally point where its path leaves our territory,
// sets off once gathered, walks the path's waypoints and arrives together near its target.
static void test_attack_rally_point() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(7);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 7, 2, 128.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	s->set_player_active(0, true, false);
	s->set_player_active(1, true, true);
	s->set_diplomacy(0, 1, Simulation::DIPLO_ENEMY);
	s->set_diplomacy(1, 0, Simulation::DIPLO_ENEMY);
	run_for(*s, 5.0f);
	float bx = 0.0f, bz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { bx = b.x; bz = b.z; }
	// Player 0's own soldiers stay out of it: only its structures stand.
	for (size_t i = 0; i < s->units_.count; ++i)
		if (s->units_.alive[i] && s->units_.team[i] == 0) s->units_.stance[i] = STANCE_PASSIVE;
	auto &plan = s->petra_attack_plan_;
	plan = Simulation::AttackPlan();
	plan.kind = PETRA_ATTACK_ATTACK;
	plan.state = Simulation::AttackPlan::MUSTER;
	for (int k = 0; k < 12; ++k) plan.squad.push_back(s->spawn_unit(bx + 6.0f + (float)(k % 4), bz + 6.0f + (float)(k / 4), 1, UNIT_SPEARMAN));
	s->petra_attack_mgr_.kind = PETRA_ATTACK_ATTACK;
	s->petra_attack_mgr_.target_size = 12;
	s->petra_attack_mgr_.min_size = 12;
	const std::vector<uint32_t> army = plan.squad;
	auto centre = [&](float &cx, float &cz) {
		cx = cz = 0.0f;
		int n = 0;
		for (uint32_t uid : army) {
			const int32_t k = s->lookup_unit(uid);
			if (k >= 0 && s->units_.alive[k]) { cx += s->units_.pos_x[k]; cz += s->units_.pos_z[k]; ++n; }
		}
		if (n) { cx /= (float)n; cz /= (float)n; }
		return n;
	};
	for (int t = 0; t < 10 && !plan.completing; ++t) run_ai_turns(*s, 1);
	CHECK(plan.completing && plan.state == Simulation::AttackPlan::MUSTER, "a ready land army first gathers (completing)");
	if (!plan.completing) return;
	const float rx = plan.staging_x, rz = plan.staging_z;
	const float rally_d = std::sqrt((rx - bx) * (rx - bx) + (rz - bz) * (rz - bz));
	const bool rally_ours = s->territory_owner_at(rx, rz) == 1;
	const bool next_out = plan.path_x.size() > 1 && s->territory_owner_at(plan.path_x[1], plan.path_z[1]) != 1;
	std::printf("  rally point %.1f from our civic centre, ours %d, next waypoint out of our territory %d, %zu waypoints, started attacks %zu\n",
	            rally_d, (int)rally_ours, (int)next_out, plan.path_x.size(), s->petra_started_attacks_.size());
	CHECK(rally_ours && next_out, "the rally point is the path's last point in our territory");
	CHECK(s->petra_started_attacks_.empty(), "a completing plan has not started");
	float t_gather = 0.0f;
	for (; t_gather < 70.0f && s->petra_started_attacks_.empty(); t_gather += Simulation::FIXED_STEP) s->tick(Simulation::FIXED_STEP);
	CHECK(!s->petra_started_attacks_.empty(), "the army sets off once gathered");
	if (s->petra_started_attacks_.empty()) return;
	float cx, cz;
	centre(cx, cz);
	const float at_rally = std::sqrt((cx - rx) * (cx - rx) + (cz - rz) * (cz - rz));
	std::printf("  set off after %.1f s, army centre %.1f from the rally point\n", t_gather, at_rally);
	CHECK(at_rally < 6.0f, "the army sets off from its rally point");
	const auto &march = s->petra_started_attacks_[0];
	CHECK(march.state == Simulation::AttackPlan::WALKING && !march.path_x.empty(), "the march follows its path");
	float t_walk = 0.0f;
	size_t waypoints = march.path_x.size(), fewest = waypoints;
	for (; t_walk < 120.0f && !s->petra_started_attacks_.empty() && s->petra_started_attacks_[0].state == Simulation::AttackPlan::WALKING; t_walk += Simulation::FIXED_STEP) {
		s->tick(Simulation::FIXED_STEP);
		if (!s->petra_started_attacks_.empty()) fewest = std::min(fewest, s->petra_started_attacks_[0].path_x.size());
	}
	CHECK(!s->petra_started_attacks_.empty() && s->petra_started_attacks_[0].state == Simulation::AttackPlan::ATTACKED, "the march arrives");
	if (s->petra_started_attacks_.empty()) return;
	const auto &arr = s->petra_started_attacks_[0];
	const int n = centre(cx, cz);
	int together = 0;
	for (uint32_t uid : army) {
		const int32_t k = s->lookup_unit(uid);
		if (k >= 0 && s->units_.alive[k] && (s->units_.pos_x[k] - cx) * (s->units_.pos_x[k] - cx) + (s->units_.pos_z[k] - cz) * (s->units_.pos_z[k] - cz) < 12.0f * 12.0f) ++together;
	}
	const float to_target = std::sqrt((cx - arr.target_x) * (cx - arr.target_x) + (cz - arr.target_z) * (cz - arr.target_z));
	std::printf("  arrived after %.1f s (%zu of %zu waypoints passed), %.1f from the target, %d of %d within 12 of the centre\n",
	            t_walk, waypoints - fewest, waypoints, to_target, together, n);
	CHECK(fewest < waypoints, "the march passes its waypoints");
	CHECK(to_target < 26.0f || s->territory_owner_at(cx, cz) == 0, "the march arrives near the target");
	CHECK(n > 0 && together * 4 >= n * 3, "the army arrives together");
}

// attackPlan.js mustStart: an Attack starts once every class of its unitStat reaches its
// targetSize, not when its total does: a squad of spearmen alone waits for its archers and
// cavalry.
static void test_attack_waits_for_each_class() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(7);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 7, 2, 128.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	s->set_player_active(0, true, false);
	s->set_player_active(1, true, true);
	s->set_diplomacy(0, 1, Simulation::DIPLO_ENEMY);
	s->set_diplomacy(1, 0, Simulation::DIPLO_ENEMY);
	run_for(*s, 5.0f);
	float bx = 0.0f, bz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { bx = b.x; bz = b.z; }
	s->cheat_mods_[1].pop_bonus = 300;
	auto &mgr = s->petra_attack_mgr_;
	mgr.rushes_set = 1;
	mgr.max_rushes = 0;
	mgr.kind = PETRA_ATTACK_ATTACK;
	mgr.target_size = (int)std::ceil(38.0f * s->petra_plan_pop_scaling());
	auto stats = s->petra_plan_unit_stats(PETRA_ATTACK_ATTACK);
	int total = 0;
	for (const auto &st : stats) total += st.target;
	auto &plan = s->petra_attack_plan_;
	plan = Simulation::AttackPlan();
	plan.kind = PETRA_ATTACK_ATTACK;
	plan.state = Simulation::AttackPlan::MUSTER;
	int k = 0;
	auto add = [&](UnitType type, int n) {
		for (int j = 0; j < n; ++j, ++k) plan.squad.push_back(s->spawn_unit(bx + 6.0f + (float)(k % 8), bz + 6.0f + (float)(k / 8), 1, type));
	};
	add(UNIT_SPEARMAN, total);
	for (int r = 0; r < RES_COUNT; ++r) s->players_[1].res[r] = 0.0f; // it trains nothing meanwhile
	run_ai_turns(*s, 4);
	const bool early = s->petra_attack_plan_.completing || s->petra_attack_plan_.state != Simulation::AttackPlan::MUSTER;
	int ranged = 0, cavalry = 0;
	for (const auto &st : stats) {
		if ((st.classes & TC_RANGED).any() && (st.classes & TC_INFANTRY).any()) ranged = st.target;
		if ((st.classes & TC_CAVALRY).any()) cavalry = st.target;
	}
	add(UNIT_ARCHER, ranged);
	add(UNIT_CAVALRY, cavalry);
	bool started = false;
	for (int turn = 0; turn < 6 && !started; ++turn) {
		run_ai_turns(*s, 1);
		started = s->petra_attack_plan_.completing || s->petra_attack_plan_.state != Simulation::AttackPlan::MUSTER;
	}
	std::printf("  class sizes: %d spearmen (the plan's total) start it %s; with %d archers and %d riders %s\n",
	            total, early ? "yes" : "no", ranged, cavalry, started ? "yes" : "no");
	CHECK(!early, "a plan whose total is full but whose classes are not keeps mustering");
	CHECK(started, "the plan starts once every class is full");
}

// attackManager.js checkEvents AttackRequest counts every upcoming plan, the raid
// mustering included, and force-starts each one of 3 units or more.
static void test_attack_request_counts_raid() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(7);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 7, 4, 128.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	s->set_player_active(0, true, false);
	s->set_player_active(1, true, true);
	s->set_player_active(2, true, true);
	s->set_player_active(3, true, false);
	for (uint8_t a = 0; a < 4; ++a)
		for (uint8_t b = 0; b < 4; ++b)
			if (a != b) s->set_diplomacy(a, b, a == 0 || b == 0 ? Simulation::DIPLO_ENEMY : Simulation::DIPLO_ALLY);
	run_for(*s, 5.0f);
	if (s->petra_slots_.size() <= 2) { CHECK(false, "AI 2 runs its own Petra"); return; }
	float bx = 0.0f, bz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 2 && b.type == BLD_TOWN_CENTER) { bx = b.x; bz = b.z; }
	auto &slot = s->petra_slots_[2];
	auto &plan = slot.petra_attack_plan_;
	plan.kind = PETRA_ATTACK_ATTACK;
	plan.state = Simulation::AttackPlan::MUSTER;
	plan.squad.clear();
	for (int k = 0; k < 10; ++k) plan.squad.push_back(s->spawn_unit(bx + 6.0f + (float)(k % 5), bz + 6.0f + (float)(k / 5), 2, UNIT_SPEARMAN));
	s->cheat_mods_[2].pop_bonus = 200; // room to grow: a plan at the population cap starts at once (attackPlan.js lengthMin)
	slot.petra_attack_mgr_.kind = PETRA_ATTACK_ATTACK;
	slot.petra_attack_mgr_.target_size = 60;
	slot.petra_attack_mgr_.min_size = 30;
	// The raid on an enemy outpost in AI 2's land, 4 riders mustering of 5 wanted.
	auto &raid = slot.petra_raid_plan_;
	raid.state = Simulation::AttackPlan::MUSTER;
	raid.state_timer = 0.0f;
	raid.target_building_id = s->spawn_building_world(bx - 10.0f, bz, BLD_OUTPOST, 0, 1.0f);
	raid.target_x = bx - 10.0f; raid.target_z = bz;
	raid.squad.clear();
	for (int k = 0; k < 4; ++k) raid.squad.push_back(s->spawn_unit(bx - 6.0f, bz + 6.0f + (float)k, 2, UNIT_CAVALRY));
	slot.petra_attack_mgr_.raid_target = 5;
	slot.petra_attack_mgr_.raid_min = 5;
	s->clear_petra_chat();
	s->cmd_attack_request(3, 0);
	run_ai_turns(*s, 1, 2);
	bool join = false;
	for (const auto &line : s->pending_petra_chat()) join |= line.kind == PETRA_CHAT_ATTACK_JOIN && line.from == 2;
	std::printf("  10 mustering and a raid of 4: AI 2 joins %s (attack forced %d, raid forced %d, raid state %d)\n",
	            join ? "yes" : "no", (int)plan.forced, (int)raid.forced, (int)raid.state);
	CHECK(join && plan.forced, "the raid's riders count toward the 12 a join needs");
	CHECK(raid.forced, "the raid of 3 or more starts at once too");
}

// victoryManager.js capture_the_relic: Petra goes for a Gaia relic in its
// territory, not for one it has never seen.
static void test_relic_capture() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_RELIC);
	run_for(*s, 1.0f);
	float tx = 0.0f, tz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tx = b.x; tz = b.z; }
	s->spawn_unit(tx + 6.0f, tz + 6.0f, 1, UNIT_SPEARMAN);
	s->relics_.clear();
	s->spawn_relic(tx + 12.0f, tz - 12.0f);
	s->spawn_relic(-tx, -tz); // far side, unseen
	// Until the capture, up to 3 min: when the points run out depends on which starting
	// units Petra frees from work (0.28's StartEntities since #578), not on the relic code.
	float t = 0.0f;
	for (; t < 180.0f && !s->relics_[0].claimed; t += 1.0f) run_for(*s, 1.0f);
	const auto &near = s->relics_[0], &far = s->relics_[1];
	std::printf("  relics: own-territory relic %s after %.0f s, unseen relic %s\n",
		near.claimed ? (near.owner_team == 1 ? "taken by AI 1" : "taken by another") : "left", t, far.claimed ? "taken" : "left alone");
	CHECK(near.claimed && near.owner_team == 1, "Petra captures a Gaia relic in its territory");
	CHECK(!far.claimed, "and leaves a relic it has never seen");
	// abortCaptureGaiaRelic: a relic someone else takes first ends the expedition.
	const uint32_t taken = s->spawn_relic(tx - 12.0f, tz + 12.0f);
	const uint32_t seeker = s->spawn_unit(tx - 4.0f, tz + 4.0f, 1, UNIT_SPEARMAN);
	s->order_capture_relic(&seeker, 1, taken);
	const int32_t tr = s->lookup_unit(taken), su = s->lookup_unit(seeker);
	s->units_.team[tr] = 0;
	for (auto &rel : s->relics_)
		if (rel.id == taken) { rel.claimed = true; rel.owner_team = 0; }
	run_ai_turns(*s, 10);
	std::printf("  relic taken by player 0 first: the AI's capturer %s\n",
		s->units_.order_kind[su] == ORDER_CAPTURE_RELIC ? "keeps going" : "is called off");
	// Called off, it may go back to work or into the civic centre (garrisoned units are not alive).
	CHECK((s->units_.alive[su] || s->units_.action_state[su] == ACTION_GARRISONED) &&
			s->units_.order_kind[su] != ORDER_CAPTURE_RELIC, "Petra calls off a capture of a relic an enemy took");
}

// attackManager.js getRelicEnemyPlayer: in capture the relic, Petra attacks the enemy
// holding the most relics rather than its nearest one.
static void test_relic_enemy_player() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 3, 64.0f);
	s->set_victory_condition(VICTORY_RELIC);
	run_for(*s, 1.0f);
	s->relics_.clear();
	const uint8_t before = s->petra_enemy_player(PETRA_ATTACK_ATTACK);
	const uint8_t holder = before == 0 ? 2 : 0;
	for (int k = 0; k < 2; ++k) {
		const int32_t r = s->lookup_unit(s->spawn_relic(10.0f + 4.0f * (float)k, 10.0f));
		if (r >= 0) s->units_.team[r] = holder;
	}
	const uint8_t after = s->petra_enemy_player(PETRA_ATTACK_ATTACK);
	std::printf("  relic enemy: player %d without relics, player %d once player %d holds two\n", before, after, holder);
	CHECK(after == holder, "Petra targets the enemy holding the most relics (getRelicEnemyPlayer)");
	s->petra_pid_ = 1;
	float tx = 0.0f, tz = 0.0f;
	const uint32_t t = s->petra_nearest_target(PETRA_ATTACK_HUGE, true, holder, 0.0f, 0.0f, 0, tx, tz);
	const int32_t ti = s->lookup_unit(t);
	CHECK(s->is_relic_unit(ti), "and an attack on it goes for its relics first (defaultTargetFinder)");
}

// attackManager.js getRelicEnemyPlayer with Gaia (player 0) first while tryCaptureGaiaRelic
// holds: an attack goes for a Gaia relic no one targets yet, the next try waits the lapse
// (resetCaptureGaiaRelic, 240 s at Medium), and an army at its relic captures it.
static void test_relic_gaia_target() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_RELIC);
	run_for(*s, 1.0f);
	s->petra_pid_ = 1;
	float cx = 0.0f, cz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { cx = b.x; cz = b.z; }
	s->relics_.clear();
	const float rx = cx * 0.5f, rz = cz * 0.5f; // halfway to the middle, away from the enemy
	const uint32_t relic = s->spawn_relic(rx, rz);
	s->petra_attack_mgr_.gaia_relic_lapse = 0;
	CHECK(s->petra_enemy_player(PETRA_ATTACK_ATTACK) == GAIA_PLAYER, "with no player holding a relic, Gaia's relics are the enemy");
	Simulation::AttackPlan plan;
	plan.kind = PETRA_ATTACK_ATTACK;
	plan.state = Simulation::AttackPlan::MUSTER;
	float tx = 0.0f, tz = 0.0f;
	const uint32_t target = s->find_petra_attack_target(tx, tz, 0, &plan);
	const int lapse = s->petra_attack_mgr_.gaia_relic_lapse;
	std::printf("  Gaia relic: target %s, next try in %d s\n", target == relic ? "the relic" : "something else", 2 * lapse);
	CHECK(target == relic, "the attack targets the Gaia relic");
	CHECK(lapse == 120, "and the next try at Gaia's relics waits 240 s at Medium");
	CHECK(s->petra_enemy_player(PETRA_ATTACK_ATTACK) != GAIA_PLAYER, "meanwhile another enemy is picked");
	plan.target_building_id = relic;
	CHECK(s->find_petra_attack_target(tx, tz, 0, &plan) == relic, "a plan keeps the Gaia relic it targets");

	// An army that reached it captures it.
	plan.state = Simulation::AttackPlan::ATTACKED;
	plan.target_x = rx;
	plan.target_z = rz;
	for (int k = 0; k < 4; ++k) plan.squad.push_back(s->spawn_unit(rx + 3.0f + (float)k, rz + 3.0f, 1, UNIT_SPEARMAN));
	s->petra_started_attacks_.push_back(plan);
	s->petra_update_attack_plan(s->petra_started_attacks_.back(), 36.0f, 36.0f);
	int capturing = 0;
	for (uint32_t uid : plan.squad) {
		const int32_t i = s->lookup_unit(uid);
		capturing += i >= 0 && s->units_.order_kind[i] == ORDER_CAPTURE_RELIC && s->units_.order_target[i] == relic;
	}
	// 60 s: a one-tile ridge beside the relic's spot keeps one captor at its margin, and pushing
	// edges its neighbour in and out of reach, so a pulse is now and then missed (about 52 s).
	run_for(*s, 60.0f);
	const int32_t r = s->lookup_unit(relic);
	std::printf("  Gaia relic: %d of 4 soldiers sent to capture it, held by player %d after 60 s\n", capturing, r >= 0 ? (int)s->units_.team[r] : -1);
	CHECK(capturing == 4, "the army at the relic goes to capture it");
	CHECK(r >= 0 && s->units_.team[r] == 1, "and takes it");
}

// attackPlan.js moves armies with forced walks: a retreating army ignores what it
// sees on the way. An unforced walk let an unpacked catapult beside an enemy
// unpack to fire, then pack for the walk, for ever.
// A placement search that found no spot is remembered until something it read
// changes (petra_placement_signature), so it is not repeated every AI turn. The
// memory must never change a decision: a lost tower or a felled tree that opens
// a spot is seen at once.
static void test_placement_miss_memory_exact() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	run_for(*s, 1.0f);
	float hx = 0.0f, hz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { hx = b.x; hz = b.z; }
	s->players_[1].add(5000.0f, 5000.0f, 5000.0f, 5000.0f);
	auto fund = [&] {
		fund_queues(*s);
	};
	const float G = Simulation::GRID;
	const int hcx = (int)(hx / G), hcz = (int)(hz / G);
	auto ring_spot = [&](int cx, int cz, int min_r, int max_r, uint8_t btype) {
		for (int r = min_r; r <= max_r; ++r)
			for (int dx = -r; dx <= r; ++dx)
				for (int dz = -r; dz <= r; ++dz)
					if ((std::abs(dx) == r || std::abs(dz) == r) && s->petra_can_place(cx + dx, cz + dz, building_type_info(btype).footprint, btype))
						return true;
		return false;
	};

	// Towers keep 60 m (15 units) apart: a tower stops one in rings 3-4 around it until it falls.
	int px = 0, pz = 0;
	bool have_p = false;
	for (int r = 13; r <= 16 && !have_p; ++r)
		for (int dx = -r; dx <= r && !have_p; ++dx)
			for (int dz = -r; dz <= r && !have_p; ++dz)
				if ((std::abs(dx) == r || std::abs(dz) == r) && s->petra_can_place(hcx + dx, hcz + dz, 1, BLD_TOWER) &&
				    ring_spot(hcx + dx, hcz + dz, 3, 4, BLD_TOWER)) {
					px = hcx + dx; pz = hcz + dz; have_p = true;
				}
	fund();
	const uint32_t old_tower = s->spawn_building(px, pz, BLD_TOWER, 1, 1.0f);
	const float tx = (px + 0.5f) * G, tz = (pz + 0.5f) * G;
	const bool tower_fails = !s->petra_place_building(BLD_TOWER, tx, tz, 3, 4, PETRA_Q_DEFENSE_BUILDING);
	const bool tower_remembered = s->petra_place_misses_.size() == 1;
	s->destroy_building(s->buildings_[s->building_id_to_idx_.at(old_tower)]);
	const uint32_t tower = s->petra_place_building(BLD_TOWER, tx, tz, 3, 4, PETRA_Q_DEFENSE_BUILDING);

	// Trees on every cell a temple could take in rings 5-6 around the Civic Centre.
	const int foot = building_type_info(BLD_TEMPLE).footprint;
	for (int dx = -6; dx <= 6 + foot; ++dx)
		for (int dz = -6; dz <= 6 + foot; ++dz)
			if (std::max(std::abs(dx), std::abs(dz)) >= 5 || dx > 4 || dz > 4)
				s->spawn_resource_world((hcx + dx + 0.5f) * G, (hcz + dz + 0.5f) * G, RES_WOOD, 200);
	s->resource_block_tick_ = UINT64_MAX; // the obstruction cache is per tick; this is the same tick
	fund();
	const bool temple_fails = !s->petra_place_building(BLD_TEMPLE, hx, hz, 5, 6, PETRA_Q_ECONOMIC_BUILDING);
	const bool still_fails = !s->petra_place_building(BLD_TEMPLE, hx, hz, 5, 6, PETRA_Q_ECONOMIC_BUILDING);
	// Fell the trees under candidate spots until one is free.
	bool freed = false;
	for (int r = 5; r <= 6 && !freed; ++r)
		for (int dx = -r; dx <= r && !freed; ++dx)
			for (int dz = -r; dz <= r && !freed; ++dz) {
				if (std::abs(dx) != r && std::abs(dz) != r) continue;
				const int cx = hcx + dx, cz = hcz + dz;
				for (size_t i = 0; i < s->resources_.count; ++i) {
					const int rx = (int)std::floor(s->resources_.x[i] / G), rz = (int)std::floor(s->resources_.z[i] / G);
					if (rx >= cx && rx < cx + foot && rz >= cz && rz < cz + foot) s->resources_.amount[i] = 0;
				}
				s->resource_block_tick_ = UINT64_MAX;
				freed = s->petra_can_place(cx, cz, foot, BLD_TEMPLE);
			}
	const uint32_t temple = s->petra_place_building(BLD_TEMPLE, hx, hz, 5, 6, PETRA_Q_ECONOMIC_BUILDING);
	std::printf("  placement miss memory: tower %s, %s after its neighbour fell; temple %s, %s after felling\n",
	            tower_fails ? "blocked" : "not blocked", tower ? "placed" : "not placed",
	            temple_fails && still_fails ? "blocked" : "not blocked", temple ? "placed" : "not placed");
	CHECK(have_p, "found a tower spot to test with");
	CHECK(tower_fails && tower_remembered, "a search with no spot fails and is remembered");
	CHECK(tower != 0, "losing the tower that blocked a spot lets the next search use it");
	CHECK(temple_fails && still_fails && freed, "trees block every temple spot until felled");
	CHECK(temple != 0, "felling the trees on a spot lets the next search use it");
}

// baseManager.js findBestDropsiteLocation scores territory tiles on the resource maps: the
// storehouse goes by the big forest, not the small grove, beyond 60 m of the civic centre.
// queueplanBuilding.js keeps the farmstead out of the woods (a third of the wood map off).
// Petra pays a structure what it costs its own civ (structure_cost), not the generic row:
// a Briton barracks is 300 wood and no stone.
static void test_structure_cost_by_civ() {
	auto s = std::make_unique<Simulation>();
	s->reset();
	s->petra_pid_ = 1;
	s->set_team_civ(1, (uint8_t)CIV_BRITONS);
	s->set_player_resources(1, 1000, 1000, 1000, 1000);
	fund_queues(*s);
	float c[RES_COUNT];
	s->structure_cost(1, BLD_BARRACKS, -1.0f, c);
	const BuildingTypeInfo &generic = building_type_info(BLD_BARRACKS);
	std::printf("  Briton barracks %.0fw %.0fs (generic %dw %ds)\n", c[RES_WOOD], c[RES_STONE], generic.wood_cost, generic.stone_cost);
	CHECK(c[RES_WOOD] == 300.0f && c[RES_STONE] == 0.0f, "a Briton barracks costs 300 wood and no stone");
	CHECK(s->petra_try_pay_structure(PETRA_Q_MILITARY_BUILDING, BLD_BARRACKS), "Petra pays for it");
	CHECK(s->players_[1].wood() == 700.0f && s->players_[1].stone() == 1000.0f, "with 300 wood and no stone");
}

static void test_dropsite_sites() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	for (uint8_t p = 0; p < 2; ++p) s->players_[p].active = 1, s->players_[p].is_ai = 0;
	run_for(*s, 1.0f);
	float hx = 0.0f, hz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { hx = b.x; hz = b.z; }
	auto &r = s->resources_;
	for (size_t i = 0; i < r.count; ++i)
		if (r.alive[i] && std::hypot(r.x[i] - hx, r.z[i] - hz) < 45.0f) r.amount[i] = 0;
	// Toward the map centre a forest of 30 trees 22-30 units out, the other way a grove of 3.
	const float ux = hx > 0.0f ? -1.0f : 1.0f, uz = hz > 0.0f ? -1.0f : 1.0f;
	for (int i = 0; i < 6; ++i)
		for (int j = 0; j < 5; ++j)
			s->spawn_resource_world(hx + ux * (20.0f + 2.0f * (float)i), hz + uz * (2.0f * (float)j - 4.0f), RES_WOOD, 200);
	for (int i = 0; i < 3; ++i) s->spawn_resource_world(hx - ux * 22.0f, hz + 2.0f * (float)i, RES_WOOD, 200);
	s->resource_block_tick_ = UINT64_MAX;
	s->petra_pid_ = 1;
	s->players_[1].phase = 1;
	s->set_player_resources(1, 5000, 5000, 5000, 5000);
	fund_queues(*s);
	const uint32_t store = s->petra_place_dropsite(RES_WOOD, 1.0f);
	float sx = 0.0f, sz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.id == store) { sx = b.x; sz = b.z; }
	const float along = (sx - hx) * ux, d = std::hypot(sx - hx, sz - hz);
	std::printf("  storehouse %.1f units from the civic centre, %.1f toward the forest\n", d, along);
	CHECK(store != 0 && along > 10.0f, "the storehouse goes by the forest, not the grove");
	CHECK(d >= 15.0f, "and beyond 60 m of the civic centre");
	// The farmstead (a storehouse and a house stand): out of the wood map.
	s->spawn_building_world(hx - ux * 10.0f, hz - uz * 10.0f, BLD_HOUSE, 1, 1.0f);
	fund_queues(*s);
	s->petra_build_farmstead();
	float fx = 0.0f, fz = 0.0f;
	bool farm = false;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_FARMSTEAD) { fx = b.x; fz = b.z; farm = true; }
	std::vector<uint8_t> maps[3];
	s->petra_resource_maps(maps, -1e9f, 1e9f, -1e9f, 1e9f);
	const int w = (int)std::floor(2.0f * s->map_half_ / Simulation::GRID);
	const int fi = (int)std::floor((fz + s->map_half_) / Simulation::GRID) * w + (int)std::floor((fx + s->map_half_) / Simulation::GRID);
	std::printf("  farmstead %s at %.1f toward the forest, wood map %d there\n", farm ? "placed" : "missing", (fx - hx) * ux, farm ? (int)maps[0][(size_t)fi] : -1);
	CHECK(farm && maps[0][(size_t)fi] == 0, "the farmstead stands off the woods");
}

// headquarters.js findEconomicCCLocation reads resource density, not single
// nodes: with 0 A.D.'s 200-wood trees and nothing bigger, Petra still finds a
// site for a second civic centre and lays out storehouses by its forests. The
// town phase and 80 more units pass checkBaseExpansion's population threshold.
static void test_expansion_small_nodes() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 1.0f);
	auto &r = s->resources_;
	for (size_t i = 0; i < r.count; ++i)
		if (r.alive[i] && r.amount[i] > 200) r.amount[i] = 200;
	s->players_[1].phase = 2;
	s->set_player_resources(1, 5000, 5000, 5000, 5000);
	for (const auto &b : std::vector<Simulation::Building>(s->buildings()))
		if (b.team == 1 && b.type == BLD_TOWN_CENTER)
			for (int k = 0; k < 80; ++k)
				s->spawn_unit(b.x - 8.0f + (float)(k % 10) * 0.8f, b.z + 8.0f + (float)(k / 10) * 0.8f, 1, UNIT_VILLAGER);
	const size_t stores_before = std::count_if(s->buildings().begin(), s->buildings().end(),
		[](const Simulation::Building &b) { return b.team == 1 && b.type == BLD_STOREHOUSE; });
	int ccs = 0;
	size_t stores = 0;
	float t = 0.0f;
	for (; t < 90.0f && ccs < 2; t += 1.0f) {
		run_for(*s, 1.0f);
		ccs = 0;
		for (const auto &b : s->buildings())
			if (b.team == 1 && b.type == BLD_TOWN_CENTER && b.hp > 0.0f) ++ccs;
	}
	// checkResourceLevels' storehouse site (findBestDropsiteLocation) among the same trees.
	s->petra_pid_ = 1;
	fund_queues(*s);
	s->petra_place_dropsite(RES_WOOD);
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_STOREHOUSE && b.hp > 0.0f) ++stores;
	std::printf("  expansion on 200-wood trees: %d Town Centers after %.0fs, %zu storehouses (had %zu)\n", ccs, t, stores, stores_before);
	CHECK(ccs >= 2, "Petra lays out a second civic centre among 200-wood trees");
	CHECK(stores > stores_before, "and a storehouse by a forest of them");
}

// baseManager.js assignToFoundations repairs damaged structures: a civic centre
// below 30% health gets up to seven repairers within the builder share of the
// workforce, so a damaged house waits its turn; both heal.
static void test_repairs() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 20.0f);
	s->set_player_resources(1, 2000, 2000, 2000, 2000);
	Simulation::Building *cc = nullptr, *house = nullptr;
	for (auto &b : s->buildings_) {
		if (b.team != 1 || b.build_progress < 1.0f) continue;
		if (b.type == BLD_TOWN_CENTER) cc = &b;
		else if (b.type == BLD_HOUSE && !house) house = &b;
	}
	CHECK(cc != nullptr, "Petra has a civic centre");
	if (!cc) return;
	const uint32_t cc_id = cc->id, house_id = house ? house->id : 0;
	// Enough workers that the builder cap (0.6 of them for a civic centre under 30%) leaves
	// room for seven repairers whatever foundations are going up at this point.
	for (int k = 0; k < 16; ++k) s->spawn_unit(cc->x - 8.0f + (float)(k % 4), cc->z - 8.0f - (float)(k / 4), 1, UNIT_FEMALE_CITIZEN);
	cc->hp = cc->max_hp * 0.25f;
	if (house) house->hp = house->max_hp * 0.5f;
	// Most repairers at once over 3 s: a foundation laid where some stand pushes them off for a
	// moment, and they come back.
	int on_cc = 0, on_house = 0;
	const auto &u = s->units_;
	for (int step = 0; step < 6; ++step) {
		run_for(*s, 0.5f);
		int cc_now = 0, house_now = 0;
		for (size_t i = 0; i < u.count; ++i) {
			if (!u.alive[i] || u.team[i] != 1 || u.action_state[i] != ACTION_BUILD) continue;
			cc_now += u.target_building_id[i] == cc_id;
			house_now += house_id && u.target_building_id[i] == house_id;
		}
		on_cc = std::max(on_cc, cc_now);
		on_house = std::max(on_house, house_now);
	}
	run_for(*s, 40.0f);
	auto find = [&](uint32_t id) -> const Simulation::Building * {
		auto it = s->building_id_to_idx_.find(id);
		return id && it != s->building_id_to_idx_.end() ? &s->buildings_[it->second] : nullptr;
	};
	const auto *c2 = find(cc_id);
	const auto *h2 = find(house_id);
	std::printf("  repairs: %d on the civic centre (25%% -> %.0f%%), %d on the house (50%% -> %.0f%%)\n",
	            on_cc, c2 ? 100.0f * c2->hp / c2->max_hp : 0.0f, on_house, h2 ? 100.0f * h2->hp / h2->max_hp : 0.0f);
	CHECK(on_cc >= 5 && on_cc <= 7, "a civic centre under 30% health gets five to seven repairers");
	CHECK(c2 && c2->hp > c2->max_hp * 0.3f, "and heals");
	CHECK(on_house <= 1, "a house takes one repairer at most");
	CHECK(!house_id || (h2 && h2->hp > h2->max_hp * 0.5f), "the damaged house is repaired too");
}

// researchManager.js update: wanted techs (gather rates, capacities) in any phase,
// preferred ones (stone and metal rates, arrows, regeneration) from the town phase,
// then any technology at random from the city phase.
// researchManager.js parity: Petra finds what any of its researchers offers this civ (Han
// unlock_spies at the ministry, not the civic centre), pays the player's price and time
// (no AI research-time bonus: config.js Cheat's Cost/BuildTime reaches no technology),
// and stops phasing once the phase research has started (ResearchPlan.onStart).
static void test_research_parity_028() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 1.0f);
	const Simulation::Building *cc = nullptr;
	for (const auto &b : s->buildings_)
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) cc = &b;
	CHECK(cc != nullptr, "Petra has a civic centre");
	if (!cc) return;
	const uint32_t cc_id = cc->id;
	const float ccx = cc->x, ccz = cc->z; // cc points into buildings_, which the spawns below may move
	const int cx = (int)(ccx / Simulation::GRID), cz = (int)(ccz / Simulation::GRID);
	s->set_team_civ(1, (uint8_t)CIV_HAN);
	s->players_[1].phase = 3;
	std::vector<uint8_t> techs;
	s->petra_available_techs(techs);
	const bool spies_before = std::find(techs.begin(), techs.end(), (uint8_t)TECH_UNLOCK_SPIES) != techs.end();
	s->spawn_building(cx + 9, cz, BLD_MINISTRY, 1);
	s->petra_available_techs(techs);
	const bool spies_after = std::find(techs.begin(), techs.end(), (uint8_t)TECH_UNLOCK_SPIES) != techs.end();
	CHECK(!spies_before && spies_after, "Han Petra researches unlock_spies at the ministry");

	float paid[RES_COUNT], time = 0.0f;
	s->tech_price(1, TECH_HEALTH_CIVILIANS_01, BLD_HOUSE, paid, time);
	CHECK(std::fabs(time - tech_info(TECH_HEALTH_CIVILIANS_01).research_time * s->tech_mods(1).bld_research_time[BLD_HOUSE]) < 0.01f,
		"an AI researches in the player's time");

	s->players_[1].phase = 1;
	s->set_team_civ(1, (uint8_t)CIV_ATHENIANS);
	for (int k = 0; k < 160; ++k) s->spawn_unit(ccx - 12.0f + (float)(k % 16), ccz - 12.0f - (float)(k / 16), 1, UNIT_FEMALE_CITIZEN);
	const bool phasing = s->petra_phasing();
	s->set_player_resources(1, 5000, 5000, 5000, 5000);
	for (int k = 0; k < 5; ++k) s->spawn_building(cx - 12 + 4 * k, cz + 10, BLD_HOUSE, 1);
	const bool queued = s->cmd_research(1, cc_id, TECH_PHASE_TOWN);
	std::printf("  research parity: phasing %d, town phase queued %d, phasing while it researches %d\n", phasing, queued, s->petra_phasing());
	CHECK(phasing && queued && !s->petra_phasing(), "phasing ends once the phase research starts");
}

static void test_research_choices() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 20.0f);
	const Simulation::Building *cc = nullptr;
	for (const auto &b : s->buildings_)
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) cc = &b;
	CHECK(cc != nullptr, "Petra has a civic centre");
	if (!cc) return;
	const int cx = (int)(cc->x / Simulation::GRID), cz = (int)(cc->z / Simulation::GRID);
	s->players_[1].phase = 2;
	// Enough workers that checkPausedQueues lets minorTech run.
	for (int k = 0; k < 12; ++k) s->spawn_unit(cc->x - 8.0f + (float)(k % 4), cc->z - 8.0f - (float)(k / 4), 1, UNIT_FEMALE_CITIZEN);
	s->spawn_building(cx + 8, cz, BLD_STOREHOUSE, 1);
	s->spawn_building(cx - 8, cz, BLD_FARMSTEAD, 1);
	s->spawn_building(cx, cz + 9, BLD_FORGE, 1);
	auto researched = [&](std::initializer_list<uint8_t> stats) {
		int n = 0;
		for (uint8_t t = 1; t < TECH_COUNT; ++t) {
			if (!s->players_[1].has_tech(t)) continue;
			const TechInfo &ti = tech_info(t);
			for (uint16_t i = 0; i < ti.effect_count; ++i) {
				bool hit = false;
				for (uint8_t st : stats) hit |= tech_effects_table()[ti.effect_begin + i].stat == st;
				if (hit) { ++n; break; }
			}
		}
		return n;
	};
	auto forge_techs = [&] {
		int n = 0;
		for (uint8_t t = 1; t < TECH_COUNT; ++t) n += s->players_[1].has_tech(t) && tech_info(t).researcher == BLD_FORGE;
		return n;
	};
	for (int k = 0; k < 30; ++k) {
		s->set_player_resources(1, 5000, 5000, 5000, 5000);
		run_for(*s, 10.0f);
	}
	const int eco = researched({TSTAT_GATHER_GRAIN, TSTAT_GATHER_WOOD, TSTAT_CARRY_FOOD, TSTAT_CARRY_WOOD});
	const int mines = researched({TSTAT_GATHER_STONE, TSTAT_GATHER_METAL});
	const int fights = forge_techs();
	std::printf("  town phase: %d grain/wood/capacity techs, %d stone/metal techs, %d forge techs\n", eco, mines, fights);
	CHECK(eco >= 3, "researches gather rates and capacities past the first tier");
	CHECK(mines >= 2, "researches stone and metal rates in the town phase");
	CHECK(fights == 0, "no random forge techs before the city phase");
	s->players_[1].phase = 3;
	// 15 minutes: minorTech's random picks over 10 give one or two forge techs on some seeds.
	for (int k = 0; k < 90; ++k) {
		s->set_player_resources(1, 5000, 5000, 5000, 5000);
		run_for(*s, 10.0f);
	}
	const int fights3 = forge_techs();
	int all3 = 0;
	for (uint8_t t = 1; t < TECH_COUNT; ++t) all3 += s->players_[1].has_tech(t);
	std::printf("  city phase: %d forge techs (%d techs in all)\n", fights3, all3);
	CHECK(fights3 >= 2, "researches other technologies at random in the city phase");
	// Config.unusedNoAllyTechs: Cartography, Diaspora and the international trade bonus
	// only with an ally.
	CHECK(s->petra_skips_random_tech(TECH_UNLOCK_SHARED_LOS, false) && s->petra_skips_random_tech(TECH_UNLOCK_SHARED_DROPSITES, false),
	      "no shared vision or dropsites without an ally");
	CHECK(!s->petra_skips_random_tech(TECH_UNLOCK_SHARED_LOS, true) && !s->petra_skips_random_tech(TECH_UNLOCK_SHARED_DROPSITES, true),
	      "both with an ally");
}

// headquarters.js trainMoreWorkers: the civic centre trains citizen infantry as workers
// once support units pass supportRatio's curve, and they gather. buildMoreHouses: houses
// go up before the population is full, at most 3 at once below 80 population.
static void test_worker_mix() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	int start_soldiers = 0;
	for (size_t i = 0; i < s->units_.count; ++i)
		if (s->units_.alive[i] && s->units_.team[i] == 1 &&
		    unit_gather_class(s->units_.unit_type[i], s->units_.tmpl[i]) == GATHER_INFANTRY) ++start_soldiers;
	int max_houses = 0;
	for (int k = 0; k < 7 * 60; ++k) {
		run_for(*s, 1.0f);
		int houses = 0;
		for (const auto &b : s->buildings_) houses += b.team == 1 && b.type == BLD_HOUSE && b.build_progress < 1.0f;
		if (s->pop_current(1) <= 80) max_houses = std::max(max_houses, houses);
	}
	int supports = 0, soldiers = 0, gathering = 0;
	for (size_t i = 0; i < s->units_.count; ++i) {
		if (!s->units_.alive[i] || s->units_.team[i] != 1) continue;
		const uint8_t g = unit_gather_class(s->units_.unit_type[i], s->units_.tmpl[i]);
		if (g == GATHER_SUPPORT) ++supports;
		if (g != GATHER_INFANTRY || !s->petra_is_worker(i)) continue;
		const uint32_t id = s->units_.id[i];
		const auto &rush = s->petra_attack_plan_.squad;
		const auto defenders = all_defenders(*s);
		// A rush takes workers past the first 8, and the defense any soldier nearby.
		if (std::find(rush.begin(), rush.end(), id) != rush.end() ||
		    std::find(defenders.begin(), defenders.end(), id) != defenders.end()) continue;
		++soldiers;
		gathering += s->units_.action_state[i] == ACTION_GATHER || s->units_.action_state[i] == ACTION_RETURN_DROPSITE;
	}
	std::printf("  7 min: %d support, %d worker soldiers at home (%d infantry at start, %d gathering), at most %d houses at once\n",
	            supports, soldiers, start_soldiers, gathering, max_houses);
	CHECK(soldiers + (int)all_defenders(*s).size() >= 5, "trains citizen infantry as workers");
	CHECK(gathering * 2 >= soldiers, "most worker soldiers gather");
	const float support_max = 0.5f * (float)s->petra_target_workers(); // supportRatio is 0.35-0.5 by difficulty
	CHECK(supports > 10 && (float)supports <= support_max + 5.0f, "support units follow supportRatio");
	CHECK(max_houses >= 1 && max_houses <= 3, "houses go up a few at a time");
}

// findTrainableUnits / trainEmergencyUnits: Petra trains only what a building's Trainer list
// names and the map has not disabled (Seleucid pikemen train at the barracks only).
static void test_unit_choice() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->set_team_civ(1, CIV_SELEUCIDS);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	uint16_t spear = CIV_UNIT_TEMPLATE_NONE;
	for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT; ++t)
		if (std::strcmp(civ_unit_template(t).id, "sele/infantry_spearman_b") == 0) spear = t;
	const_cast<TriggerState &>(s->trigger_state()).disabled_templates.push_back(spear);
	int foreign = 0, disabled = 0, soldiers = 0;
	for (int k = 0; k < 7 * 60; ++k) {
		run_for(*s, 1.0f);
		for (const auto &b : s->buildings()) {
			if (b.team != 1) continue;
			for (const auto &o : b.queue) {
				if (o.is_tech() || o.tmpl >= CIV_UNIT_TEMPLATE_COUNT) continue;
				foreign += !(civ_unit_template(o.tmpl).trainers & bld_bit(b.type));
				disabled += o.tmpl == spear;
				soldiers += (civ_unit_template(o.tmpl).tech_classes & TC_CITIZENSOLDIER) != 0;
			}
		}
	}
	std::printf("  Seleucids, spearmen disabled, 7 min: %d queue-seconds of citizen soldiers, %d off their Trainer list, %d disabled\n",
	            soldiers, foreign, disabled);
	CHECK(soldiers > 0, "trains citizen soldiers");
	CHECK(foreign == 0, "trains each unit only where its Trainer list allows");
	CHECK(disabled == 0, "never trains a disabled template");
}

// victoryManager.js: in wonder victory a finished wonder is a critical entity,
// guarded by up to 10 + 5 * defensive soldiers, champions first, who turn on
// whoever attacks it.
static void test_wonder_guards() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_WONDER);
	run_for(*s, 1.0f);
	float tx = 0.0f, tz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tx = b.x; tz = b.z; }
	const float wx = tx * 0.8f, wz = tz * 0.8f;
	const uint32_t wonder = s->spawn_building_world(wx, wz, BLD_WONDER, 1, 1.0f);
	std::vector<uint32_t> champs;
	for (int i = 0; i < 4; ++i) champs.push_back(s->spawn_unit(tx + 10.0f + 3.0f * (float)i, tz + 10.0f, 1, UNIT_ELEPHANT));
	run_ai_turns(*s, 10); // victoryManager.js: guards every 10th AI turn
	auto &u = s->units_;
	int guarding = 0;
	for (uint32_t id : champs) {
		const int32_t c = s->lookup_unit(id);
		if (c >= 0 && u.order_kind[c] == ORDER_GUARD && u.order_target[c] == wonder) ++guarding;
	}
	const uint32_t foe = s->spawn_unit(wx + 8.0f, wz, 0, UNIT_SWORDSMAN);
	s->order_attack_building(&foe, 1, wonder);
	const int32_t f = s->lookup_unit(foe);
	const float foe_hp = u.hp[f];
	run_for(*s, 20.0f);
	const bool hurt = !u.alive[f] || u.hp[f] < foe_hp;
	std::printf("  wonder guards: %d of 4 champions guard the wonder, its attacker %s\n", guarding,
	            !u.alive[f] ? "killed" : (hurt ? "hurt" : "untouched"));
	CHECK(wonder != 0 && guarding == 4, "champions guard Petra's finished wonder");
	CHECK(hurt, "and fight whoever attacks it");
}

// victoryManager.js criticalEnts: an owned relic is guarded like a hero
// (4 + 5 * defensive); in regicide only the hero the game began with is
// critical, so a hero trained later draws no guards; and buildWonder counts
// live wonders, so a destroyed wonder's wreck is replaced by a new one.
static void test_victory_critical_ents() {
	{
		auto s = std::make_unique<Simulation>();
		s->set_sim_seed(42);
		s->reset();
		s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
		s->set_victory_condition(VICTORY_RELIC);
		run_for(*s, 1.0f);
		float tx = 0.0f, tz = 0.0f;
		for (const auto &b : s->buildings())
			if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tx = b.x; tz = b.z; }
		s->relics_.clear();
		const uint32_t relic = s->spawn_relic(tx + 8.0f, tz - 8.0f);
		const int32_t r = s->lookup_unit(relic);
		if (r >= 0) s->units_.team[r] = 1;
		std::vector<uint32_t> champs;
		for (int i = 0; i < 3; ++i) champs.push_back(s->spawn_unit(tx + 10.0f + 3.0f * (float)i, tz + 10.0f, 1, UNIT_ELEPHANT));
		run_ai_turns(*s, 10); // victoryManager.js: guards every 10th AI turn
		int guarding = 0;
		for (uint32_t id : champs) {
			const int32_t c = s->lookup_unit(id);
			if (c >= 0 && s->units_.order_kind[c] == ORDER_GUARD && s->units_.order_target[c] == relic) ++guarding;
		}
		std::printf("  relic guards: %d of 3 champions guard Petra's relic\n", guarding);
		CHECK(r >= 0 && guarding == 3, "champions guard a relic Petra owns");
	}
	{
		auto s = std::make_unique<Simulation>();
		s->set_sim_seed(42);
		s->reset();
		s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
		s->set_victory_condition(VICTORY_REGICIDE);
		run_for(*s, 1.0f);
		const uint32_t hero = s->regicide_hero(1);
		float tx = 0.0f, tz = 0.0f;
		for (const auto &b : s->buildings())
			if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tx = b.x; tz = b.z; }
		const uint32_t trained = s->spawn_unit(tx - 10.0f, tz - 10.0f, 1, UNIT_HERO);
		for (int i = 0; i < 14; ++i) s->spawn_unit(tx + 10.0f + 2.0f * (float)(i % 7), tz + 10.0f + 2.0f * (float)(i / 7), 1, UNIT_ELEPHANT);
		run_ai_turns(*s, 10); // victoryManager.js: guards every 10th AI turn
		int on_hero = 0, on_trained = 0;
		for (size_t i = 0; i < s->units_.count; ++i) {
			if (!s->units_.alive[i] || s->units_.team[i] != 1 || s->units_.order_kind[i] != ORDER_GUARD) continue;
			on_hero += s->units_.order_target[i] == hero;
			on_trained += s->units_.order_target[i] == trained;
		}
		std::printf("  regicide: %d guard the regicide hero, %d a hero trained later\n", on_hero, on_trained);
		CHECK(hero != 0 && on_hero >= 4, "the regicide hero is guarded");
		CHECK(on_trained == 0, "a hero trained later is not a critical entity");
		// Garrison events: the hero goes into its civic centre; its guards walk up to it.
		uint32_t tc = 0;
		for (const auto &b : s->buildings())
			if (b.team == 1 && b.type == BLD_TOWN_CENTER) tc = b.id;
		std::vector<uint32_t> guards;
		for (size_t i = 0; i < s->units_.count; ++i)
			if (s->units_.alive[i] && s->units_.team[i] == 1 && s->units_.order_kind[i] == ORDER_GUARD && s->units_.order_target[i] == hero)
				guards.push_back(s->units_.id[i]);
		s->set_regicide_garrison(true); // Regicide garrison: the hero may go in
		const bool inside = s->garrison_unit_now(hero, tc);
		run_ai_turns(*s, 1);
		int still = 0, near_tc = 0;
		for (uint32_t g : guards) {
			const int32_t u = s->lookup_unit(g);
			still += u >= 0 && s->units_.order_kind[u] == ORDER_GUARD && s->units_.order_target[u] == hero;
		}
		run_for(*s, 15.0f); // walking up (once the hero is out again they guard it anew)
		const auto &tcb = s->buildings_[s->building_id_to_idx_.at(tc)];
		for (uint32_t g : guards) {
			const int32_t u = s->lookup_unit(g);
			if (u >= 0 && s->units_.alive[u]) near_tc += std::hypot(s->units_.pos_x[u] - tcb.x, s->units_.pos_z[u] - tcb.z) < tcb.half_size + 4.0f;
		}
		std::printf("  regicide hero garrisoned: %zu guards, %d still guarding, %d by the civic centre\n", guards.size(), still, near_tc);
		CHECK(inside && !guards.empty() && still == 0 && near_tc > 0, "a garrisoned hero's guards walk to its holder");
	}
	{
		auto s = std::make_unique<Simulation>();
		s->set_sim_seed(42);
		s->reset();
		s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
		s->set_victory_condition(VICTORY_WONDER);
		run_for(*s, 1.0f);
		s->players_[1].phase = 3;
		float tx = 0.0f, tz = 0.0f;
		for (const auto &b : s->buildings())
			if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tx = b.x; tz = b.z; }
		const uint32_t first = s->spawn_building_world(tx * 0.8f, tz * 0.8f, BLD_WONDER, 1, 1.0f);
		s->cmd_delete(1, &first, 1);
		bool rebuilt = false;
		for (float t = 0.0f; t < 40.0f && !rebuilt; t += Simulation::FIXED_STEP) { // buildWonder every 10th AI turn
			s->players_[1].add(5000.0f, 5000.0f, 5000.0f, 5000.0f);
			fund_queues(*s);
			s->tick(Simulation::FIXED_STEP);
			for (const auto &b : s->buildings())
				if (b.team == 1 && b.type == BLD_WONDER && b.id != first && !b.wrecked) rebuilt = true;
		}
		std::printf("  wonder destroyed: a new one placed %s\n", rebuilt ? "yes" : "no");
		CHECK(rebuilt, "Petra starts a new wonder once its wonder is destroyed");
	}
}

static void test_retreat_walk_forced() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 1.0f);
	float tx = 0.0f, tz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tx = b.x; tz = b.z; }
	const float cx = tx * 0.4f, cz = tz * 0.4f; // well away from Petra's base
	const uint32_t cat = s->spawn_unit(cx, cz, 1, UNIT_CATAPULT);
	s->spawn_unit(cx, cz + 2.0f, 1, UNIT_SPEARMAN); // a comrade the enemy can fight too
	s->order_pack(&cat, 1, false);
	run_for(*s, 11.0f);
	const int32_t ci = s->lookup_unit(cat);
	const float r = s->unit_info_at((size_t)ci).attack_range;
	s->spawn_unit(cx + r * 0.7f, cz, 0, UNIT_SPEARMAN, 1.0e6f); // fights back and never dies
	run_for(*s, 1.0f);
	const bool unpacked = s->units_.pack_state[ci] == PACK_UNPACKED;
	auto &plan = s->petra_attack_plan_;
	// The catapult alone: with the faster spearman in the squad, the regroup centroid
	// reaches home first and frees the catapult mid-pack, so whether some other plan then
	// claims it depended on the rest of the match.
	plan.squad = {cat};
	plan.initial_squad_size = 1;
	plan.state = Simulation::AttackPlan::ABORT;
	float moved = 0.0f; // furthest it got from where it stood
	for (int k = 0; k < 25; ++k) {
		run_for(*s, 1.0f);
		const int32_t c2 = s->lookup_unit(cat);
		if (c2 >= 0) moved = std::max(moved, std::hypot(s->units().pos_x[c2] - cx, s->units().pos_z[c2] - cz));
	}
	std::printf("  retreat: unpacked catapult beside a fighting enemy %s, moved %.1f toward base in 25 s\n",
	            unpacked ? "yes" : "no", moved);
	CHECK(unpacked, "the catapult stood unpacked beside the enemy");
	CHECK(moved > 5.0f, "a retreat walk packs the catapult and leaves (forced walk)");
}

// worker.js returnResources: an idle Petra worker still carrying a load is ordered to
// a dropsite (ReturnResource) rather than left to UnitAI, which drops off nothing by itself.
static void test_idle_return() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 20.0f);
	auto &u = s->units_;
	// The support worker nearest Petra's civic centre (one off building a far house may have
	// no short way back).
	float cx = 0.0f, cz = 0.0f;
	for (const auto &b : s->buildings_)
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { cx = b.x; cz = b.z; }
	uint32_t uid = 0;
	float best = 0.0f;
	for (size_t i = 0; i < u.count; ++i) {
		if (!u.alive[i] || u.team[i] != 1 || unit_gather_class(u.unit_type[i], u.tmpl[i]) != GATHER_SUPPORT) continue;
		const float d = (u.pos_x[i] - cx) * (u.pos_x[i] - cx) + (u.pos_z[i] - cz) * (u.pos_z[i] - cz);
		if (!uid || d < best) { uid = u.id[i]; best = d; }
	}
	CHECK(uid != 0, "Petra has a support worker");
	if (!uid) return;
	s->cmd_stop(&uid, 1);
	const int32_t i = s->lookup_unit(uid);
	u.carry[i] = 5; // a partial load: under the carry cap
	u.res_type_carried[i] = RES_WOOD;
	bool ordered = false, dropped = false;
	for (int k = 0; k < 30 * 20 && !dropped; ++k) {
		s->tick(Simulation::FIXED_STEP);
		const int32_t j = s->lookup_unit(uid);
		if (j < 0) break;
		ordered |= u.order_kind[j] == ORDER_RETURN_RESOURCE;
		dropped = ordered && u.carry[j] == 0;
	}
	std::printf("  idle worker with a load: return order %s, dropped off %s\n", ordered ? "yes" : "no", dropped ? "yes" : "no");
	CHECK(ordered && dropped, "an idle worker carrying a load is sent to a dropsite");
}

// worker.js returnResources looks among gameState.getOwnDropsites(), units included: with no
// structure dropsite left, an idle worker carrying a load takes it to a supply wagon.
static void test_idle_return_to_wagon() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 20.0f);
	auto &u = s->units_;
	uint32_t uid = 0;
	for (size_t i = 0; i < u.count && !uid; ++i)
		if (u.alive[i] && u.team[i] == 1 && unit_gather_class(u.unit_type[i], u.tmpl[i]) == GATHER_SUPPORT) uid = u.id[i];
	CHECK(uid != 0, "Petra has a support worker");
	if (!uid) return;
	// No structure of its own takes the load any more.
	for (auto &b : s->buildings_) if (b.team == 1) b.team = GAIA_PLAYER;
	s->cmd_stop(&uid, 1);
	int32_t i = s->lookup_unit(uid);
	const uint32_t wagon = s->spawn_unit(u.pos_x[i] + 6.0f, u.pos_z[i], 1, UNIT_WAGON);
	u.carry[i] = 5;
	u.res_type_carried[i] = RES_WOOD;
	const float before = s->player(1).wood();
	bool dropped = false;
	for (int k = 0; k < 30 * 20 && !dropped; ++k) {
		s->tick(Simulation::FIXED_STEP);
		const int32_t j = s->lookup_unit(uid);
		if (j < 0) break;
		dropped = u.carry[j] == 0;
	}
	std::printf("  idle worker with a load and only a wagon: dropped off %s, wood %.0f -> %.0f\n", dropped ? "yes" : "no",
	            before, s->player(1).wood());
	CHECK(wagon != 0 && dropped && s->player(1).wood() >= before + 5.0f, "an idle worker with a load drops it at a supply wagon");
}

// worker.js startGathering skips a supply whose MaxGatherers are all taken (IsSupplyFull),
// so Petra's workers spread over the trees and mines instead of crowding the nearest.
static void test_gather_limits() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 20.0f);
	const Simulation::Building *cc = nullptr;
	for (const auto &b : s->buildings_)
		if (b.team == 1 && b.type == BLD_TOWN_CENTER && b.build_progress >= 1.0f) cc = &b;
	CHECK(cc != nullptr, "Petra has a civic centre");
	if (!cc) return;
	// Leave one tree and one mine by the civic centre: nothing else to gather or hunt.
	auto &r = s->resources_;
	size_t tree = SIZE_MAX, mine = SIZE_MAX;
	auto d2 = [&](size_t k) { return (r.x[k] - cc->x) * (r.x[k] - cc->x) + (r.z[k] - cc->z) * (r.z[k] - cc->z); };
	for (size_t k = 0; k < r.count; ++k) {
		if (!r.alive[k] || r.amount[k] <= 0) continue;
		if (r.type[k] == RES_WOOD && (tree == SIZE_MAX || d2(k) < d2(tree))) tree = k;
		if (r.type[k] == RES_GOLD && (mine == SIZE_MAX || d2(k) < d2(mine))) mine = k;
	}
	CHECK(tree != SIZE_MAX && mine != SIZE_MAX, "a tree and a mine on the map");
	if (tree == SIZE_MAX || mine == SIZE_MAX) return;
	for (size_t k = 0; k < r.count; ++k)
		if (k != tree && k != mine) r.amount[k] = 0;
	r.amount[tree] = r.amount[mine] = 5000;
	for (size_t f = 0; f < s->fauna_.count; ++f) s->fauna_.food_amount[f] = 0;
	// No foundations: a gatherer with no supply would help build a dropsite for its resource.
	for (auto &b : s->buildings_)
		if (b.team == 1 && b.hp > 0.0f && b.build_progress < 1.0f) s->destroy_building(b);
	const uint32_t tree_id = r.id[tree], mine_id = r.id[mine];
	for (int k = 0; k < 8; ++k) s->spawn_unit(cc->x - 8.0f + (float)(k % 4), cc->z - 8.0f - (float)(k / 4), 1, UNIT_FEMALE_CITIZEN);
	auto &u = s->units_;
	std::vector<uint32_t> workers;
	for (size_t i = 0; i < u.count; ++i)
		if (u.alive[i] && u.team[i] == 1 && (u.unit_type[i] == UNIT_VILLAGER || u.unit_type[i] == UNIT_FEMALE_CITIZEN)) {
			workers.push_back(u.id[i]);
			u.carry[i] = 0;
		}
	s->cmd_stop(workers.data(), workers.size());
	run_for(*s, 2.0f);
	int on_tree = 0, on_mine = 0;
	for (uint32_t id : workers) {
		const int32_t i = s->lookup_unit(id);
		if (i < 0) continue;
		on_tree += u.target_resource_id[i] == tree_id;
		on_mine += u.target_resource_id[i] == mine_id;
	}
	std::printf("  gather limits: %zu idle workers, %d sent to the tree (MaxGatherers %d), %d to the mine (%d)\n",
	            workers.size(), on_tree, s->resource_max_gatherers(tree_id), on_mine, s->resource_max_gatherers(mine_id));
	CHECK(on_tree <= s->resource_max_gatherers(tree_id) && on_mine <= s->resource_max_gatherers(mine_id),
	      "Petra sends no more gatherers to a supply than its MaxGatherers (IsSupplyFull)");
	CHECK(on_tree + on_mine > 8, "the rest go to another supply");
}

// garrisonManager.js addDecayingStructure: a defence tower of Petra's standing in enemy
// territory decays, so Petra garrisons the soldiers that stop the decay and keeps them in.
static void test_decay_garrison() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 20.0f);
	const Simulation::Building *foe_cc = nullptr;
	for (const auto &b : s->buildings_)
		if (b.team == 0 && b.type == BLD_TOWN_CENTER && b.build_progress >= 1.0f) foe_cc = &b;
	CHECK(foe_cc != nullptr, "player 0 has a civic centre");
	if (!foe_cc) return;
	const float tx = foe_cc->x + 14.0f, tz = foe_cc->z;
	const uint32_t tower = s->spawn_building_world(tx, tz, BLD_TOWER, 1);
	for (int k = 0; k < 6; ++k) s->spawn_unit(tx + 4.0f + (float)k, tz + 4.0f, 1, UNIT_SPEARMAN);
	int need = 0;
	{
		const int32_t ti = s->building_id_to_idx_.count(tower) ? (int32_t)s->building_id_to_idx_[tower] : -1;
		CHECK(ti >= 0, "the tower stands");
		if (ti < 0) return;
		need = s->petra_decay_garrison_min(s->buildings_[ti]);
	}
	run_for(*s, 15.0f);
	auto it = s->building_id_to_idx_.find(tower);
	const size_t inside = it == s->building_id_to_idx_.end() ? 0 : s->buildings_[it->second].garrisoned_units.size();
	const bool ours = it != s->building_id_to_idx_.end() && s->buildings_[it->second].team == 1;
	const int cap = ours ? s->buildings_[it->second].garrison_cap : 0;
	std::printf("  decaying tower in enemy land: needs %d, garrisoned %zu, still ours %s\n", need, inside, ours ? "yes" : "no");
	CHECK(need > 0, "a tower in enemy territory decays");
	CHECK(ours && (int)inside >= std::min(need, cap), "Petra garrisons a decaying tower (TYPE_DECAY)");
}

// headquarters.js checkEvents: a structure Petra just captured in enemy land, which no
// garrison will hold, is destroyed while an enemy holds part of it and Petra keeps under
// 70-80% of its capture points; with 95% it is kept.
static void test_decay_destroy() {
	for (int phase = 0; phase < 2; ++phase) {
		auto s = std::make_unique<Simulation>();
		s->set_sim_seed(42);
		s->reset();
		s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
		s->set_victory_condition(VICTORY_ENDLESS);
		run_for(*s, 20.0f);
		const Simulation::Building *foe_cc = nullptr;
		for (const auto &b : s->buildings_)
			if (b.team == 0 && b.type == BLD_TOWN_CENTER && b.build_progress >= 1.0f) foe_cc = &b;
		CHECK(foe_cc != nullptr, "player 0 has a civic centre");
		if (!foe_cc) return;
		while (!s->petra_turn_due(1)) s->tick(Simulation::FIXED_STEP); // AI 1 plays on the next tick
		const uint32_t id = s->spawn_building_world(foe_cc->x + 14.0f, foe_cc->z, BLD_STOREHOUSE, 1);
		Simulation::Building &b = s->buildings_[s->building_id_to_idx_[id]];
		const float own = phase == 0 ? 0.6f : 0.95f;
		b.capture_points_by_team.fill(0.0f);
		b.capture_points_by_team[1] = b.max_capture_points * own;
		b.capture_points_by_team[0] = b.max_capture_points * (1.0f - own);
		s->petra_decay_events_.push_back(id); // as if just captured
		run_ai_turns(*s, 1);
		auto it = s->building_id_to_idx_.find(id);
		const bool gone = it == s->building_id_to_idx_.end() || s->buildings_[it->second].wrecked ||
			s->buildings_[it->second].hp <= 0.0f;
		std::printf("  decaying storehouse with %.0f%% of its points: %s\n", own * 100.0f, gone ? "destroyed" : "kept");
		if (phase == 0) CHECK(gone, "Petra destroys a decaying structure it is about to lose");
		else CHECK(!gone, "Petra keeps a decaying structure it holds firmly");
	}
}

// defenseManager.js isDangerous / defenseArmy.js needsDefenders: Support units in our
// territory are no threat, and the defence is sized by strength (getMaxStrength), so
// war elephants draw more defenders than the same number of spearmen would.
static void test_defense_by_strength() {
	for (int phase = 0; phase < 2; ++phase) {
		auto s = std::make_unique<Simulation>();
		s->set_sim_seed(42);
		s->reset();
		s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
		s->set_victory_condition(VICTORY_ENDLESS);
		run_for(*s, 20.0f);
		const Simulation::Building *cc = nullptr;
		for (const auto &b : s->buildings_)
			if (b.team == 1 && b.type == BLD_TOWN_CENTER && b.build_progress >= 1.0f) cc = &b;
		CHECK(cc != nullptr, "Petra has a civic centre");
		if (!cc) return;
		const float cx = cc->x, cz = cc->z;
		for (int k = 0; k < 30; ++k) s->spawn_unit(cx - 10.0f + (float)(k % 6), cz - 10.0f - (float)(k / 6), 1, UNIT_SPEARMAN);
		const UnitType foe = phase == 0 ? UNIT_FEMALE_CITIZEN : UNIT_ELEPHANT;
		for (int k = 0; k < 2; ++k) s->spawn_unit(cx + 6.0f + (float)k, cz + 6.0f, 0, foe);
		run_ai_turns(*s, 3); // checkEnemyUnits looks in our land one turn in (players + 1)
		const size_t n = all_defenders(*s).size();
		const size_t by_count = (size_t)std::ceil(2.0f * s->petra_config_.defense_ratio_own);
		if (phase == 0) {
			std::printf("  defense: 2 enemy women by the civic centre draw %zu defenders\n", n);
			CHECK(n == 0, "Support units are not dangerous (isDangerous)");
		} else {
			std::printf("  defense: 2 war elephants draw %zu defenders (%zu by head count)\n", n, by_count);
			CHECK(n > by_count && n < 30, "defenders sized by strength (needsDefenders)");
		}
	}
}

// baseManager.js assignToFoundations targetNB: a new civic centre takes up to 15
// builders, a barracks 4, a house 3, with resources to spare (builderRatio 1).
static void test_builders_per_foundation() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 20.0f);
	s->set_player_resources(1, 5000, 5000, 5000, 5000);
	const Simulation::Building *cc = nullptr;
	for (const auto &b : s->buildings_)
		if (b.team == 1 && b.type == BLD_TOWN_CENTER && b.build_progress >= 1.0f) cc = &b;
	CHECK(cc != nullptr, "Petra has a civic centre");
	if (!cc) return;
	const float cx = cc->x, cz = cc->z;
	for (int k = 0; k < 30; ++k) s->spawn_unit(cx - 8.0f + (float)(k % 6), cz + 8.0f + (float)(k / 6), 1, UNIT_FEMALE_CITIZEN);
	const uint32_t barracks = s->spawn_building_world(cx + 12.0f, cz, BLD_BARRACKS, 1, 0.0f);
	const uint32_t house = s->spawn_building_world(cx - 12.0f, cz, BLD_HOUSE, 1, 0.0f);
	run_for(*s, 3.0f);
	auto on = [&](uint32_t id) {
		int n = 0;
		for (size_t i = 0; i < s->units_.count; ++i)
			if (s->units_.alive[i] && s->units_.team[i] == 1 && s->units_.action_state[i] == ACTION_BUILD && s->units_.target_building_id[i] == id) ++n;
		return n;
	};
	std::printf("  builders: barracks %d, house %d\n", on(barracks), on(house));
	CHECK(on(barracks) == 4, "a barracks foundation takes 4 builders");
	CHECK(on(house) == 3, "a house foundation takes 3 builders");
}

// defenseManager.js makeIntoArmy / checkEnemyArmies: two groups of foes on opposite sides of
// the civic centre make two armies, each with its own defenders; armies whose foes come
// within armyMergeSize of each other merge.
static void test_defense_armies() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 20.0f);
	const Simulation::Building *cc = nullptr;
	for (const auto &b : s->buildings_)
		if (b.team == 1 && b.type == BLD_TOWN_CENTER && b.build_progress >= 1.0f) cc = &b;
	CHECK(cc != nullptr, "Petra has a civic centre");
	if (!cc) return;
	const float cx = cc->x, cz = cc->z;
	for (int k = 0; k < 30; ++k) s->spawn_unit(cx - 3.0f + (float)(k % 6), cz + 12.0f + (float)(k / 6), 1, UNIT_SPEARMAN);
	std::vector<uint32_t> east, west;
	for (int k = 0; k < 3; ++k) east.push_back(s->spawn_unit(cx + 9.0f, cz - 8.0f + (float)k, 0, UNIT_SWORDSMAN, 1.0e4f));
	for (int k = 0; k < 3; ++k) west.push_back(s->spawn_unit(cx - 9.0f, cz - 8.0f + (float)k, 0, UNIT_SWORDSMAN, 1.0e4f));
	// checkEnemyUnits looks in our land one turn in (players + 1): the armies form within three
	// turns, and are counted the turn they do, before the fight moves either group.
	for (int t = 0; t < 3 && s->petra_defense_armies().empty(); ++t) run_ai_turns(*s, 1);
	const auto &armies = s->petra_defense_armies();
	size_t with_defenders = 0;
	for (const auto &a : armies) with_defenders += !a.defender_ids.empty();
	std::printf("  defense: two groups 18 units apart make %zu armies, %zu with defenders\n", armies.size(), with_defenders);
	CHECK(armies.size() == 2 && with_defenders == 2, "one army per group of foes, each defended (makeIntoArmy, assignDefenders)");
	// The western group joins the eastern one where its fight has taken it (a mine beside the
	// eastern group's spawn point blocks movement, so it can't stay put).
	float ex = 0.0f, ez = 0.0f;
	int en = 0;
	for (uint32_t id : east) {
		const int32_t i = s->lookup_unit(id);
		if (i < 0) continue;
		ex += s->units_.pos_x[i];
		ez += s->units_.pos_z[i];
		++en;
	}
	if (en) { ex /= (float)en; ez /= (float)en; } else { ex = cx + 10.0f; ez = cz - 7.0f; }
	for (size_t k = 0; k < west.size(); ++k) {
		const int32_t i = s->lookup_unit(west[k]);
		if (i < 0) continue;
		s->units_.pos_x[i] = ex + 1.0f;
		s->units_.pos_z[i] = ez - 1.0f + (float)k;
	}
	run_ai_turns(*s, 1);
	std::printf("  defense: after the groups meet, %zu army\n", armies.size());
	CHECK(armies.size() == 1, "armies that meet merge (armyMergeSize)");
}

// defenseManager.js checkEnemyUnits/isDangerous: ships and traders are skipped ("TODO what to
// do for ships ?"), and a support unit is dangerous only while it builds; a fighter by our
// structures still is.
static void test_defense_ignores_ships() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 20.0f);
	const Simulation::Building *cc = nullptr;
	for (const auto &b : s->buildings_)
		if (b.team == 1 && b.type == BLD_TOWN_CENTER && b.build_progress >= 1.0f) cc = &b;
	CHECK(cc != nullptr, "Petra has a civic centre");
	if (!cc) return;
	const float cx = cc->x, cz = cc->z;
	s->spawn_unit(cx + 9.0f, cz - 8.0f, 0, UNIT_TRIREME, 1.0e4f);
	s->spawn_unit(cx - 9.0f, cz - 8.0f, 0, UNIT_MERCHANT_SHIP, 1.0e4f);
	s->spawn_unit(cx - 9.0f, cz + 8.0f, 0, UNIT_FEMALE_CITIZEN, 1.0e4f);
	run_ai_turns(*s, 3);
	std::printf("  defense: a warship, a merchant ship and a woman by the civic centre make %zu armies\n", s->petra_defense_armies().size());
	CHECK(s->petra_defense_armies().empty(), "ships and idle support units raise no defense army (checkEnemyUnits, isDangerous)");
	s->spawn_unit(cx + 9.0f, cz + 8.0f, 0, UNIT_SWORDSMAN, 1.0e4f);
	run_ai_turns(*s, 3);
	CHECK(s->petra_defense_armies().size() == 1, "a swordsman by the civic centre still does");
}

// defenseManager.js switchToAttack: an army whose foes are gone, with a defender within
// 120 m of an enemy structure in our territory, attacks that structure alone and ends
// once it falls (uniqueTargetId).
static void test_defense_switch_to_attack() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 20.0f);
	const Simulation::Building *cc = nullptr;
	for (const auto &b : s->buildings_)
		if (b.team == 1 && b.type == BLD_TOWN_CENTER && b.build_progress >= 1.0f) cc = &b;
	CHECK(cc != nullptr, "Petra has a civic centre");
	if (!cc) return;
	const float cx = cc->x, cz = cc->z;
	for (int k = 0; k < 20; ++k) s->spawn_unit(cx - 3.0f + (float)(k % 5), cz + 12.0f + (float)(k / 5), 1, UNIT_SPEARMAN);
	const uint32_t tower = s->spawn_building_world(cx + 12.0f, cz - 10.0f, BLD_SENTRY_TOWER, 0, 0.2f);
	for (int k = 0; k < 3; ++k) s->spawn_unit(cx + 10.0f, cz - 7.0f + (float)k, 0, UNIT_SWORDSMAN, 20.0f);
	const auto unique = [&]() -> const Simulation::AttackPlan * {
		for (const auto &p : s->petra_started_attacks())
			if (p.kind == PETRA_ATTACK_UNIQUE) return &p;
		return nullptr;
	};
	const Simulation::AttackPlan *plan = nullptr;
	for (int t = 0; t < 60 && !plan; ++t) {
		run_for(*s, 0.5f);
		plan = unique();
	}
	std::printf("  defense: army switched to attack: %s, target %s, %zu units\n", plan ? "yes" : "no",
	            plan && plan->target_building_id == tower ? "the tower" : "other", plan ? plan->squad.size() : (size_t)0);
	CHECK(plan && plan->target_building_id == tower && !plan->squad.empty(), "the disbanding army attacks the enemy structure in our land");
	if (!plan) return;
	for (auto &b : s->buildings_)
		if (b.id == tower) s->destroy_building(b);
	run_for(*s, 2.0f);
	plan = unique();
	std::printf("  defense: after the tower falls the plan is %s\n", !plan ? "over" : plan->state == Simulation::AttackPlan::REGROUP ? "going home" : "still attacking");
	CHECK(!plan || plan->state == Simulation::AttackPlan::REGROUP, "it takes no other target (uniqueTargetId)");
}

// defenseArmy.js "capturing" armies: Petra wins back the points an enemy holds on one of its
// structures, and captures a Gaia structure in its land.
static void test_capturing_armies() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 20.0f);
	const Simulation::Building *cc = nullptr;
	for (const auto &b : s->buildings_)
		if (b.team == 1 && b.type == BLD_TOWN_CENTER && b.build_progress >= 1.0f) cc = &b;
	CHECK(cc != nullptr, "Petra has a civic centre");
	if (!cc) return;
	const float cx = cc->x, cz = cc->z;
	for (int k = 0; k < 10; ++k) s->spawn_unit(cx - 2.0f + (float)(k % 5), cz + 12.0f + (float)(k / 5), 1, UNIT_SPEARMAN);
	const uint32_t barracks = s->spawn_building_world(cx - 14.0f, cz - 14.0f, BLD_BARRACKS, 1, 1.0f);
	auto bld = [&](uint32_t id) -> Simulation::Building * {
		for (auto &b : s->buildings_)
			if (b.id == id) return &b;
		return nullptr;
	};
	Simulation::Building *b = bld(barracks);
	CHECK(b && b->max_capture_points > 0.0f, "the barracks can be captured");
	if (!b || b->max_capture_points <= 0.0f) return;
	b->capture_points_by_team[0] = 0.45f * b->max_capture_points;
	b->capture_points_by_team[1] = 0.55f * b->max_capture_points;
	const auto capturing = [&](uint32_t id) -> const PetraDefenseArmy * {
		for (const auto &a : s->petra_defense_armies())
			if (a.foe_ids.size() == 1 && a.foe_ids[0] == id) return &a;
		return nullptr;
	};
	bool formed = false, recapturing = false;
	for (int t = 0; t < 20 && !recapturing; ++t) {
		run_for(*s, 0.5f);
		if (const PetraDefenseArmy *a = capturing(barracks)) {
			formed = true;
			for (uint32_t uid : a->defender_ids) {
				const int32_t i = s->lookup_unit(uid);
				recapturing |= i >= 0 && s->units_.target_building_id[i] == barracks;
			}
		}
	}
	std::printf("  capturing: army for our barracks %s, defenders capturing it %s\n", formed ? "yes" : "no", recapturing ? "yes" : "no");
	CHECK(formed && recapturing, "a capturing army wins back the points an enemy holds on our structure");
	run_for(*s, 30.0f);
	b = bld(barracks);
	std::printf("  capturing: enemy points left %.0f, army %s\n", b ? b->capture_points_by_team[0] : -1.0f, capturing(barracks) ? "still there" : "disbanded");
	CHECK(b && b->capture_points_by_team[0] <= 0.0f && !capturing(barracks), "it disbands once the points are back");

	const uint32_t tower = s->spawn_building_world(cx + 14.0f, cz - 12.0f, BLD_SENTRY_TOWER, MAX_PLAYERS, 1.0f);
	bool taken = false;
	for (int t = 0; t < 240 && !taken; ++t) {
		run_for(*s, 0.5f);
		const Simulation::Building *tw = bld(tower);
		taken = tw && tw->team == 1;
	}
	std::printf("  capturing: Gaia sentry tower in our land taken: %s\n", taken ? "yes" : "no");
	CHECK(taken, "Petra captures a Gaia structure in its land");
}

// defenseArmy.js update: a defender capturing a structure allowCapture no longer allows
// (its regeneration outruns the army) damages it instead.
static void test_defenders_stop_hopeless_capture() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 20.0f);
	const Simulation::Building *cc = nullptr;
	for (const auto &b : s->buildings_)
		if (b.team == 1 && b.type == BLD_TOWN_CENTER && b.build_progress >= 1.0f) cc = &b;
	if (!cc) { CHECK(false, "Petra has a civic centre"); return; }
	const float cx = cc->x, cz = cc->z;
	for (int k = 0; k < 10; ++k) s->spawn_unit(cx - 2.0f + (float)(k % 5), cz + 12.0f + (float)(k / 5), 1, UNIT_SPEARMAN);
	const uint32_t tower = s->spawn_building_world(cx + 14.0f, cz - 12.0f, BLD_SENTRY_TOWER, MAX_PLAYERS, 1.0f);
	auto on_tower = [&](bool capturing) {
		int n = 0;
		for (size_t i = 0; i < s->units_.count; ++i)
			if (s->units_.alive[i] && s->units_.team[i] == 1 && s->units_.target_building_id[i] == tower &&
			    ((s->units_.order_flags[i] & ORDER_FLAG_ALLOW_CAPTURE) != 0) == capturing) ++n;
		return n;
	};
	int capturing = 0;
	for (int t = 0; t < 60 && capturing == 0; ++t) { run_for(*s, 0.5f); capturing = on_tower(true); }
	// Now the tower regenerates far faster than the army captures.
	for (auto &b : s->buildings_)
		if (b.id == tower) b.capture_regen_rate = 1.0e6f;
	run_for(*s, 4.0f);
	const int still = on_tower(true), hitting = on_tower(false);
	std::printf("  hopeless capture: %d capturing first, then %d still capturing, %d damaging\n", capturing, still, hitting);
	CHECK(capturing > 0, "the defenders first capture the Gaia tower");
	CHECK(still == 0 && hitting > 0, "they damage it once capturing is hopeless");
}

// attackManager.js assignBombers: an idle stone thrower shells an enemy structure it can
// reach from our land, and leaves alone those deep in enemy land.
static void test_assign_bombers() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 20.0f);
	const Simulation::Building *cc = nullptr, *enemy_cc = nullptr;
	for (const auto &b : s->buildings_) {
		if (b.type != BLD_TOWN_CENTER || b.build_progress < 1.0f) continue;
		if (b.team == 1) cc = &b;
		if (b.team == 0) enemy_cc = &b;
	}
	CHECK(cc && enemy_cc, "both sides have a civic centre");
	if (!cc || !enemy_cc) return;
	const float cx = cc->x, cz = cc->z;
	// Called straight after spawning, while the stone thrower is idle (its own UnitAI would
	// otherwise pick the tower up once it sees it).
	const uint32_t cat = s->spawn_unit(cx - 12.0f, cz + 6.0f, 1, UNIT_CATAPULT);
	const uint32_t enemy_cc_id = enemy_cc->id;
	s->petra_assign_bombers();
	int32_t ci = s->lookup_unit(cat);
	CHECK(ci >= 0 && !s->units_.has_order[ci], "it leaves the enemy civic centre in enemy land alone");
	(void)enemy_cc_id;
	const uint32_t tower = s->spawn_building_world(cx - 12.0f, cz + 18.0f, BLD_SENTRY_TOWER, 0, 1.0f);
	s->petra_assign_bombers();
	ci = s->lookup_unit(cat);
	const bool shelling = ci >= 0 && s->units_.order_kind[ci] == ORDER_ATTACK_BUILDING && s->units_.order_target[ci] == tower;
	std::printf("  bombers: idle stone thrower shells the enemy tower in our land: %s\n", shelling ? "yes" : "no");
	CHECK(shelling, "an idle stone thrower shells an enemy structure within reach (assignBombers)");
}

// attackManager.js cancelAttacksAgainstPlayer: once Petra makes peace with a player, its
// attack on that player turns back and the plan in preparation drops it as its target.
static void test_cancel_attacks_on_peace() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 1.0f);
	float tx = 0.0f, tz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tx = b.x; tz = b.z; }
	Simulation::AttackPlan marching;
	marching.kind = PETRA_ATTACK_ATTACK;
	marching.state = Simulation::AttackPlan::WALKING;
	marching.target_player = 0;
	for (int k = 0; k < 4; ++k) marching.squad.push_back(s->spawn_unit(tx - 10.0f + 2.0f * (float)k, tz - 12.0f, 1, UNIT_SPEARMAN));
	marching.initial_squad_size = marching.squad.size();
	for (const auto &b : s->buildings())
		if (b.team == 0 && b.type == BLD_TOWN_CENTER) { marching.target_building_id = b.id; marching.target_x = b.x; marching.target_z = b.z; }
	s->petra_started_attacks_.push_back(marching);
	s->petra_attack_plan_.target_player = 0;
	s->petra_change_diplomacy(0, Simulation::DIPLO_NEUTRAL);
	const bool aborted = !s->petra_started_attacks_.empty() && s->petra_started_attacks_[0].state == Simulation::AttackPlan::ABORT;
	const bool forgot = s->petra_attack_plan_.target_player == 0xFF;
	std::printf("  peace with the target: started attack aborted %s, plan in preparation forgets it %s\n",
		aborted ? "yes" : "no", forgot ? "yes" : "no");
	CHECK(aborted, "a started attack on a player Petra makes peace with is aborted (cancelAttacksAgainstPlayer)");
	CHECK(forgot, "the plan in preparation drops that player as its target");
	// Declaring war does not touch the attacks.
	s->petra_started_attacks_[0].state = Simulation::AttackPlan::WALKING;
	s->petra_change_diplomacy(0, Simulation::DIPLO_ENEMY);
	CHECK(s->petra_started_attacks_[0].state == Simulation::AttackPlan::WALKING, "going to war leaves attacks alone");
}

// defenseManager.js isDangerous / checkEvents outside our own land: Petra sends
// defenders to an ally's civic centre under attack, keeps a backup of up to 12
// far-off units at home, and answers a unit attacked in neutral land.
static void test_defend_allies() {
	auto s = three_player_match(7);
	s->set_mutual_ally(1, 2);
	run_for(*s, 1.0f);
	s->petra_config_slot(1).cooperative = 0.5f;
	auto &u = s->units_;
	const Simulation::Building *tc1 = nullptr, *tc2 = nullptr;
	for (const auto &b : s->buildings()) {
		if (b.type != BLD_TOWN_CENTER) continue;
		if (b.team == 1) tc1 = &b;
		if (b.team == 2) tc2 = &b;
	}
	CHECK(tc1 && tc2, "both AIs have a Town Center");
	if (!tc1 || !tc2) return;
	const float ax = tc2->x, az = tc2->z, hx = tc1->x, hz = tc1->z;
	// AI 1's soldiers at home, far from its ally.
	std::vector<uint32_t> mine;
	for (int i = 0; i < 16; ++i) mine.push_back(s->spawn_unit(hx + 10.0f + (float)(i % 4) * 2.0f, hz + 10.0f + (float)(i / 4) * 2.0f, 1, UNIT_SPEARMAN));
	std::vector<uint32_t> foes;
	for (int i = 0; i < 6; ++i) foes.push_back(s->spawn_unit(ax + 12.0f + 2.0f * (float)i, az + 12.0f, 0, UNIT_SPEARMAN));
	s->order_set_stance(foes.data(), foes.size(), STANCE_STAND_GROUND);
	auto is_foe = [&](uint32_t id) { return std::find(foes.begin(), foes.end(), id) != foes.end(); };
	int sent = 0, home = 0;
	for (float t = 0.0f; t < 3.0f; t += Simulation::FIXED_STEP) {
		for (uint32_t id : foes) {
			const int32_t f = s->lookup_unit(id);
			if (f >= 0 && u.alive[f]) u.hp[f] = u.max_hp[f];
		}
		s->tick(Simulation::FIXED_STEP);
	}
	for (size_t i = 0; i < u.count; ++i) { // the backup counts AI 1's starting soldiers too
		if (!u.alive[i] || u.team[i] != 1 || petra_is_support_unit(u.unit_type[i])) continue;
		if (is_foe(u.target_unit_id[i])) ++sent; else ++home;
	}
	const float dx = ax - hx, dz = az - hz;
	std::printf("  ally's Town Center attacked %.0f away: %d soldiers sent, %d stay home\n", std::sqrt(dx * dx + dz * dz), sent, home);
	CHECK(sent > 0, "Petra defends a cooperative ally's Town Center");
	CHECK(home >= 12, "and keeps 12 far-off units home as a backup");

	// Neutral land: an enemy hits one of AI 1's units there, and AI 1 answers.
	auto s2 = three_player_match(7);
	run_for(*s2, 1.0f);
	auto &u2 = s2->units_;
	float h2x = 0.0f, h2z = 0.0f, nx = 0.0f, nz = 0.0f, best = -1.0f;
	for (const auto &b : s2->buildings()) if (b.team == 1 && b.type == BLD_TOWN_CENTER) { h2x = b.x; h2z = b.z; }
	for (float r = 36.0f; r < 70.0f && best < 0.0f; r += 2.0f)
		for (int k = 0; k < 16 && best < 0.0f; ++k) {
			const float x = h2x + r * std::cos(0.3927f * (float)k), z = h2z + r * std::sin(0.3927f * (float)k);
			if (s2->territory_owner_at(x, z) < 0 && s2->territory_owner_at(x + 3.0f, z) < 0 && !s2->line_crosses_impassable(h2x, h2z, x, z)) { nx = x; nz = z; best = r; }
		}
	CHECK(best > 0.0f, "neutral land near AI 1");
	if (best < 0.0f) return;
	// A soldier holding its ground there, so neither walks into AI 1's land.
	const uint32_t victim = s2->spawn_unit(nx, nz, 1, UNIT_SPEARMAN);
	const uint32_t raider = s2->spawn_unit(nx + 2.0f, nz, 0, UNIT_SWORDSMAN);
	s2->order_set_stance(&victim, 1, STANCE_STAND_GROUND);
	for (int i = 0; i < 4; ++i) s2->spawn_unit(h2x + 8.0f + 2.0f * (float)i, h2z - 8.0f, 1, UNIT_SPEARMAN);
	s2->order_attack(&raider, 1, victim);
	bool answered = false;
	for (float t = 0.0f; t < 4.0f && !answered; t += Simulation::FIXED_STEP) {
		const int32_t v = s2->lookup_unit(victim);
		if (v >= 0 && u2.alive[v]) u2.hp[v] = u2.max_hp[v];
		const int32_t r = s2->lookup_unit(raider);
		if (r >= 0 && u2.alive[r]) u2.hp[r] = u2.max_hp[r];
		s2->tick(Simulation::FIXED_STEP);
		for (size_t g = 0; g < u2.count; ++g) // any of AI 1's soldiers but the one hit
			if (u2.alive[g] && u2.team[g] == 1 && u2.id[g] != victim && u2.target_unit_id[g] == raider) answered = true;
	}
	std::printf("  raider hits a soldier in neutral land %.0f from AI 1's Town Center: answered %s\n", best, answered ? "yes" : "no");
	CHECK(answered, "Petra answers an attacker in neutral land");
}

// defenseManager.js OwnershipChanged -> attackManager.js switchDefenseToAttack: the
// defense army fighting by a civic centre an enemy captures attacks it to take it back.
static void test_retake_civic_centre() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 1.0f);
	auto &u = s->units_;
	uint32_t tc_id = 0;
	float tx = 0.0f, tz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tc_id = b.id; tx = b.x; tz = b.z; }
	CHECK(tc_id != 0, "Petra has a Town Center");
	if (!tc_id) return;
	std::vector<uint32_t> mine, foes;
	// Cavalry are Soldiers, the Town Center's GarrisonArrowClasses, so garrisonUnitsInside would
	// send them in: a Town Center with no room keeps them outside, defending.
	s->buildings_[s->building_id_to_idx_.at(tc_id)].garrison_cap = 0;
	for (int i = 0; i < 10; ++i) mine.push_back(s->spawn_unit(tx + 10.0f + (float)(i % 5) * 2.0f, tz + 10.0f + (float)(i / 5) * 2.0f, 1, UNIT_CAVALRY));
	for (int i = 0; i < 6; ++i) foes.push_back(s->spawn_unit(tx - 22.0f, tz - 6.0f + 2.0f * (float)i, 0, UNIT_SWORDSMAN));
	auto hold = [&]() {
		for (uint32_t id : foes) {
			const int32_t f = s->lookup_unit(id);
			if (f >= 0 && u.alive[f]) u.hp[f] = u.max_hp[f];
		}
	};
	for (float t = 0.0f; t < 2.0f; t += Simulation::FIXED_STEP) { hold(); s->tick(Simulation::FIXED_STEP); }
	const size_t defenders = all_defenders(*s).size();
	// The enemy takes the Town Center.
	{
		auto &b = s->buildings_[s->building_id_to_idx_.at(tc_id)];
		b.capture_points_by_team.fill(0.0f);
		b.capture_points_by_team[0] = b.max_capture_points;
	}
	bool lost = false, switched = false;
	size_t squad = 0;
	for (float t = 0.0f; t < 1.0f && !switched; t += Simulation::FIXED_STEP) {
		hold();
		s->tick(Simulation::FIXED_STEP);
		lost |= s->buildings()[s->building_id_to_idx_.at(tc_id)].team == 0;
		for (const auto &plan : s->petra_started_attacks())
			if (plan.target_building_id == tc_id && plan.state == Simulation::AttackPlan::ATTACKED && !plan.squad.empty()) {
				switched = true;
				squad = plan.squad.size();
			}
	}
	std::printf("  civic centre captured (%zu defending): lost %s, defense turned into an attack on it %s (%zu units)\n",
	            defenders, lost ? "yes" : "no", switched ? "yes" : "no", squad);
	CHECK(defenders > 0 && lost, "the enemy takes a defended Town Center");
	CHECK(switched, "the defense army attacks the captured civic centre");
}

// attackManager.js update: a new Attack musters while another marches, as long as fewer
// than min(2, 1 + popMax/100) are under way and more than 12 population slots are free.
static void test_concurrent_attacks() {
	for (int big = 0; big < 2; ++big) {
		auto s = std::make_unique<Simulation>();
		s->set_sim_seed(42);
		s->reset();
		s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
		s->set_victory_condition(VICTORY_ENDLESS);
		run_for(*s, 1.0f);
		float tx = 0.0f, tz = 0.0f;
		for (const auto &b : s->buildings())
			if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tx = b.x; tz = b.z; }
		s->players_[1].phase = 2;
		s->spawn_building_world(tx + 14.0f, tz, BLD_BARRACKS, 1, 1.0f);
		if (big)
			for (int k = 0; k < 16; ++k) s->spawn_building_world(tx - 16.0f + 4.0f * (float)(k % 4), tz + 12.0f + 4.0f * (float)(k / 4), BLD_HOUSE, 1, 1.0f);
		auto &mgr = s->petra_attack_mgr_;
		mgr.rushes_set = 1;
		mgr.max_rushes = 0;
		mgr.attack_number = 1;
		// An Attack already on its way.
		Simulation::AttackPlan marching;
		marching.kind = PETRA_ATTACK_ATTACK;
		marching.state = Simulation::AttackPlan::WALKING;
		for (int k = 0; k < 4; ++k) marching.squad.push_back(s->spawn_unit(tx - 10.0f + 2.0f * (float)k, tz - 12.0f, 1, UNIT_SPEARMAN));
		marching.initial_squad_size = marching.squad.size();
		marching.staging_x = tx;
		marching.staging_z = tz - 20.0f;
		for (const auto &b : s->buildings())
			if (b.team == 0 && b.type == BLD_TOWN_CENTER) { marching.target_building_id = b.id; marching.target_x = b.x; marching.target_z = b.z; }
		s->petra_started_attacks_.push_back(marching);
		run_ai_turns(*s, 2);
		const int cap = s->pop_max(1);
		const bool second = s->petra_attack_plan().state == Simulation::AttackPlan::MUSTER;
		std::printf("  concurrent attacks: population cap %d, one marching, a second musters %s\n", cap, second ? "yes" : "no");
		if (big) CHECK(cap >= 100 && second, "a second Attack musters while one marches (popMax >= 100)");
		else CHECK(cap < 50 && !second, "a small population runs one Attack at a time");
	}
}

// worker.js moveToGatherer: Petra's mobile dropsites (worker elephant, supply wagon) walk
// to the nearest of its gatherers at work on their land.
static void test_mobile_dropsite_follows() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 30.0f);
	float tx = 0.0f, tz = 0.0f;
	for (const auto &b : s->buildings())
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tx = b.x; tz = b.z; }
	int gathering = 0;
	for (size_t i = 0; i < s->units_.count; ++i)
		gathering += s->units_.alive[i] && s->units_.team[i] == 1 && s->units_.action_state[i] == ACTION_GATHER;
	const uint32_t wagon = s->spawn_unit(tx + 20.0f, tz + 20.0f, 1, UNIT_WAGON);
	s->petra_move_dropsites_to_gatherers();
	const int32_t w = s->lookup_unit(wagon);
	float best = 1e30f;
	for (size_t i = 0; i < s->units_.count; ++i) {
		if (!s->units_.alive[i] || s->units_.team[i] != 1 || (s->units_.action_state[i] != ACTION_GATHER && s->units_.action_state[i] != ACTION_RETURN_DROPSITE)) continue;
		best = std::min(best, std::hypot(s->units_.pos_x[i] - s->units_.order_x[w], s->units_.pos_z[i] - s->units_.order_z[w]));
	}
	const bool walks = w >= 0 && s->units_.order_kind[w] == ORDER_WALK && best < 0.5f;
	std::printf("  mobile dropsite: %d gatherers at work, the wagon walks to one %s\n", gathering, walks ? "yes" : "no");
	CHECK(gathering > 0 && walks, "a mobile dropsite walks to the nearest gatherer (moveToGatherer)");
}

// attackPlan.js Abort: an aborted attack withdraws to the nearest of our bases on its
// land, a second civic centre near the front included, not to the first one.
static void test_abort_nearest_base() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 1.0f);
	float tx = 0.0f, tz = 0.0f, ex = 0.0f, ez = 0.0f;
	for (const auto &b : s->buildings()) {
		if (b.type != BLD_TOWN_CENTER) continue;
		if (b.team == 1) { tx = b.x; tz = b.z; }
		if (b.team == 0) { ex = b.x; ez = b.z; }
	}
	// A second civic centre halfway to the enemy.
	const float fx = 0.5f * (tx + ex), fz = 0.5f * (tz + ez);
	const uint32_t front = s->spawn_building_world(fx, fz, BLD_TOWN_CENTER, 1, 1.0f);
	run_for(*s, 3.0f);
	bool based = false;
	for (const auto &pb : s->petra_bases()) based |= pb.anchor_building_id == front;
	float bx = fx, bz = fz;
	if (auto it = s->building_id_to_idx_.find(front); it != s->building_id_to_idx_.end()) { bx = s->buildings_[it->second].x; bz = s->buildings_[it->second].z; }
	Simulation::AttackPlan plan;
	plan.kind = PETRA_ATTACK_ATTACK;
	plan.state = Simulation::AttackPlan::ABORT;
	for (int k = 0; k < 4; ++k) plan.squad.push_back(s->spawn_unit(bx + 0.7f * (ex - bx) + 2.0f * (float)k, bz + 0.7f * (ez - bz), 1, UNIT_SPEARMAN));
	s->petra_update_attack_plan(plan, tx, tz);
	const float d_front = std::hypot(plan.retreat_x - bx, plan.retreat_z - bz);
	std::printf("  abort: front base registered %s, army withdraws %.1f from it\n", based ? "yes" : "no", d_front);
	CHECK(based, "the second civic centre is a base");
	CHECK(plan.state == Simulation::AttackPlan::REGROUP && d_front < 1.0f, "an aborted attack withdraws to the nearest base on its land (Abort)");
}

// attackPlan.js updatePreparation "Fasten the end game": a mustering plan with siege
// engines that outnumbers 20 + twice the enemy's units marches at once (forceStart).
static void test_fasten_end_game() {
	for (int siege = 0; siege < 2; ++siege) {
		auto s = std::make_unique<Simulation>();
		s->set_sim_seed(42);
		s->reset();
		s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
		s->set_victory_condition(VICTORY_ENDLESS);
		run_for(*s, 1.0f);
		float tx = 0.0f, tz = 0.0f;
		for (const auto &b : s->buildings())
			if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tx = b.x; tz = b.z; }
		size_t enemies = 0;
		for (size_t i = 0; i < s->units_.count; ++i) enemies += s->units_.alive[i] && s->units_.team[i] == 0;
		auto &mgr = s->petra_attack_mgr_;
		mgr.rushes_set = 1;
		mgr.max_rushes = 0;
		auto &plan = s->petra_attack_plan_;
		plan = Simulation::AttackPlan();
		plan.kind = PETRA_ATTACK_HUGE;
		plan.state = Simulation::AttackPlan::MUSTER;
		const size_t n = 22 + 2 * enemies;
		for (size_t k = 0; k < n; ++k)
			plan.squad.push_back(s->spawn_unit(tx - 14.0f + 1.5f * (float)(k % 12), tz - 14.0f - 1.5f * (float)(k / 12), 1, UNIT_SPEARMAN));
		if (siege) plan.squad.push_back(s->spawn_unit(tx - 16.0f, tz - 20.0f, 1, UNIT_CATAPULT));
		mgr.kind = PETRA_ATTACK_HUGE;
		mgr.target_size = mgr.min_size = 500;
		s->cheat_mods_[1].pop_bonus = 200; // room to grow: a plan at the population cap starts at once (attackPlan.js lengthMin)
		// "Fasten the end game" every 5th AI turn; a forced plan still waits for the batches it
		// has in training (countOwnQueuedEntitiesWithMetadata).
		bool marched = false;
		for (int turn = 0; turn < 40 && !marched; ++turn) {
			run_ai_turns(*s, 1);
			marched = s->petra_attack_plan_.state != Simulation::AttackPlan::MUSTER || s->petra_attack_plan_.completing;
		}
		std::printf("  end game: %zu soldiers%s against %zu enemy units, marches %s\n", n, siege ? " and a catapult" : "", enemies, marched ? "yes" : "no");
		if (siege) CHECK(marched, "a plan with siege engines that far outnumbers the enemy starts (forceStart)");
		else CHECK(!marched, "without siege engines it keeps mustering to its size");
	}
}

// defenseManager.js garrisonUnitsInside, TYPE_DECAY: a structure just captured in enemy
// land takes its garrison even from an attack under way, and those units leave the plan.
static void test_decay_garrison_from_attack() {
	for (int holdable = 0; holdable < 2; ++holdable) {
		auto s = std::make_unique<Simulation>();
		s->set_sim_seed(42);
		s->reset();
		s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
		s->set_victory_condition(VICTORY_ENDLESS);
		run_for(*s, 20.0f);
		const Simulation::Building *foe_cc = nullptr;
		for (const auto &b : s->buildings_)
			if (b.team == 0 && b.type == BLD_TOWN_CENTER && b.build_progress >= 1.0f) foe_cc = &b;
		CHECK(foe_cc != nullptr, "player 0 has a civic centre");
		if (!foe_cc) return;
		const uint32_t cc_id = foe_cc->id;
		const float cx = foe_cc->x, cz = foe_cc->z, tx = cx + 14.0f, tz = cz;
		// Petra's other soldiers are gone: only the army attacking the civic centre is left
		// when a structure beside it turns Petra's.
		for (size_t q = 0; q < s->units_.count; ++q)
			if (s->units_.alive[q] && s->units_.team[q] == 1 && !petra_is_support_unit(s->units_.unit_type[q])) { s->units_.alive[q] = 0; s->units_.action_state[q] = ACTION_DYING; }
		Simulation::AttackPlan plan;
		plan.kind = PETRA_ATTACK_ATTACK;
		for (int k = 0; k < 6; ++k) plan.squad.push_back(s->spawn_unit(tx + 4.0f + (float)k, tz + 8.0f, 1, UNIT_SPEARMAN, 1.0e4f));
		plan.initial_squad_size = plan.squad.size();
		plan.target_building_id = cc_id;
		plan.target_x = cx; plan.target_z = cz;
		plan.state = Simulation::AttackPlan::ATTACKED;
		const std::vector<uint32_t> army = plan.squad;
		s->petra_started_attacks_.push_back(plan);
		// A tower's garrison (5 of the 10 it needs) can't stop its decay. A stock fortress needs
		// none (RegenRate 45 outdoes DecayRate 40), so this one regains only 10 a second (needing
		// 30) and is given room for 30 to exercise the garrison that does.
		const uint32_t held = s->spawn_building_world(tx + (holdable ? 4.0f : 0.0f), tz, holdable ? BLD_FORTRESS : BLD_TOWER, 1);
		int need = 0, cap = 0;
		if (auto it = s->building_id_to_idx_.find(held); it != s->building_id_to_idx_.end()) {
			need = s->petra_decay_garrison_min(s->buildings_[it->second]);
			if (holdable) {
				CHECK(need == 0, "a stock fortress outregenerates its decay");
				s->buildings_[it->second].capture_regen_rate = 10.0f;
				need = s->petra_decay_garrison_min(s->buildings_[it->second]);
				CHECK(s->buildings_[it->second].garrison_cap == 20 && need == 30, "regaining 10 a second it holds 20 of the 30 its decay needs");
				s->buildings_[it->second].garrison_cap = need;
			}
			cap = s->buildings_[it->second].garrison_cap;
		}
		run_for(*s, 15.0f);
		auto it = s->building_id_to_idx_.find(held);
		const bool ours = it != s->building_id_to_idx_.end() && s->buildings_[it->second].team == 1;
		int inside = 0, still_in_plan = 0;
		for (uint32_t uid : army) {
			if (ours) {
				const auto &g = s->buildings_[it->second].garrisoned_units;
				if (std::find(g.begin(), g.end(), uid) != g.end()) ++inside;
			}
			if (s->petra_in_attack_squad(uid)) ++still_in_plan;
		}
		std::printf("  decaying %s beside an attack: needs %d of %d, %d of the army inside, %d left in the attack\n",
		            holdable ? "fortress" : "tower", need, cap, inside, still_in_plan);
		if (holdable) {
			CHECK(need > 0 && need <= cap && ours && inside == 6, "Petra pulls soldiers out of an attack to hold a decaying fortress");
			CHECK(still_in_plan == 0, "and they leave the attack plan");
		} else {
			CHECK(need > cap && inside == 0, "a tower no garrison can hold takes no soldiers from the attack");
		}
	}
}

// headquarters.js buildDefenses: the first sentry tower waits for fortStartTime + towerLapseTime
// (180 + 360 s at defensive 0.5), the defenseBuilding plan holds one structure until it is paid for, and
// the next stone tower waits (1 + 0.1 * towers) * towerLapseTime after the last one was queued.
static void test_build_defenses() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 20.0f);
	s->set_player_resources(1, 5000, 5000, 5000, 5000);
	s->petra_pid_ = 1;
	auto at = [&](float secs) { s->ticks_ = (uint64_t)(secs / Simulation::FIXED_STEP + 0.5f); };
	auto count = [&](uint8_t type) {
		int n = 0;
		for (const auto &b : s->buildings_) n += b.team == 1 && b.type == type && b.hp > 0.0f;
		return n;
	};
	auto fund = [&] { fund_queues(*s); };
	s->players_[1].phase = 1;
	s->petra_config_.defensive = 0.5f; // towerLapseTime 360 x (1.1 - 0.2 x 0.5)
	s->petra_attack_mgr_.save_resources = 0;
	at(530.0f);
	s->petra_build_defenses();
	CHECK(s->petra_defense_build_.pending == 0, "no sentry tower before fortStartTime + towerLapseTime");
	at(550.0f);
	s->petra_build_defenses();
	CHECK(s->petra_defense_build_.pending == BLD_SENTRY_TOWER + 1, "a sentry tower is queued after 540 s");
	const int sentries = count(BLD_SENTRY_TOWER);
	s->petra_build_defenses();
	CHECK(count(BLD_SENTRY_TOWER) == sentries, "an unpaid plan waits");
	fund();
	s->petra_build_defenses();
	CHECK(count(BLD_SENTRY_TOWER) == sentries + 1 && s->petra_defense_build_.pending == 0, "the paid plan is placed");
	at(1000.0f);
	s->petra_build_defenses();
	CHECK(s->petra_defense_build_.pending == 0, "medium difficulty builds one sentry tower");
	s->players_[1].phase = 2;
	s->petra_build_defenses();
	CHECK(s->petra_defense_build_.pending == BLD_TOWER + 1, "the first stone tower is queued in the town phase");
	fund();
	s->petra_build_defenses();
	CHECK(count(BLD_TOWER) == 1, "the stone tower is placed");
	at(1300.0f);
	s->petra_build_defenses();
	CHECK(s->petra_defense_build_.pending == 0, "the second stone tower waits 1.1 x towerLapseTime");
}

// headquarters.js findDefensiveLocation: a tower goes on our frontier at the spot nearest an enemy
// civic centre, outside 80 m of enemy towers and civic centres and 30 m from our own towers.
static void test_defensive_location() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 20.0f);
	s->petra_pid_ = 1;
	float ox = 0, oz = 0, ex = 0, ez = 0;
	for (const auto &b : s->buildings_) {
		if (!is_civic_centre(b.type) || b.hp <= 0.0f) continue;
		if (b.team == 1) { ox = b.x; oz = b.z; }
		if (b.team == 0) { ex = b.x; ez = b.z; }
	}
	int cx = 0, cz = 0;
	CHECK(s->petra_find_defensive_location(BLD_SENTRY_TOWER, cx, cz), "a frontier spot for a sentry tower");
	const float foot = (float)building_type_info(BLD_SENTRY_TOWER).footprint * Simulation::GRID * 0.5f;
	const float x = (float)cx * Simulation::GRID + foot, z = (float)cz * Simulation::GRID + foot;
	const float to_enemy = std::hypot(x - ex, z - ez);
	const float cc_gap = std::hypot(ox - ex, oz - ez);
	std::printf("  sentry tower spot %.1f from the enemy civic centre, %.1f from ours (civic centres %.1f apart)\n",
	            to_enemy, std::hypot(x - ox, z - oz), cc_gap);
	CHECK(to_enemy >= 80.0f / 4.0f - 1.0f, "out of 80 m of the enemy civic centre");
	CHECK(to_enemy < cc_gap, "on the side facing the enemy");
	const uint32_t t = s->spawn_building(cx, cz, BLD_SENTRY_TOWER, 1, 1.0f);
	CHECK(t != 0, "the tower is placed");
	int cx2 = 0, cz2 = 0;
	if (s->petra_find_defensive_location(BLD_SENTRY_TOWER, cx2, cz2))
		CHECK(std::hypot((float)(cx2 - cx) * Simulation::GRID, (float)(cz2 - cz) * Simulation::GRID) >= 30.0f / 4.0f - 1.0f,
		      "the next tower keeps 30 m from it");

	// Wonder victory with our wonder up: the frontier rule is dropped and the tower
	// goes just beyond 50 m of the wonder's footprint, ringing it.
	{
		auto w = std::make_unique<Simulation>();
		w->set_sim_seed(42);
		w->reset();
		w->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
		w->set_victory_condition(VICTORY_WONDER);
		run_for(*w, 20.0f);
		w->petra_pid_ = 1;
		const float dx = ox - ex, dz = oz - ez, dl = std::hypot(dx, dz);
		const uint32_t wid = w->spawn_building_world(ox + dx / dl * 14.0f, oz + dz / dl * 14.0f, BLD_WONDER, 1, 1.0f);
		auto wit = w->building_id_to_idx_.find(wid);
		CHECK(wit != w->building_id_to_idx_.end(), "a wonder for AI 1");
		if (wit == w->building_id_to_idx_.end()) return;
		const auto &wb = w->buildings_[wit->second];
		int wx = 0, wz = 0;
		const bool got = w->petra_find_defensive_location(BLD_SENTRY_TOWER, wx, wz);
		const float px = (float)wx * Simulation::GRID + foot, pz = (float)wz * Simulation::GRID + foot;
		const float d = std::hypot(px - wb.x, pz - wb.z), dmin = 50.0f / 4.0f + wb.half_size;
		std::printf("  wonder victory: sentry tower spot %.1f from our wonder (at least %.1f)\n", d, dmin);
		CHECK(got && d >= dmin - 1.5f && d < dmin + 6.0f, "the tower rings our wonder just beyond 50 m");
	}
}

// queueplanTraining.js: each batch Petra queues carries what it is for (a worker, the attack
// in preparation, the raid); getBestTrainers/start send it to the trainer whose queue ends
// first, a civic centre 10 s later for soldiers; the attack's soldiers spawn into its army and
// the worker ones stay workers, wherever they trained; the tag survives a save and load.
static void test_order_plan_tags() {
	auto make = [] {
		auto s = std::make_unique<Simulation>();
		s->set_sim_seed(42);
		s->reset();
		s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
		s->set_victory_condition(VICTORY_ENDLESS);
		return s;
	};
	auto s = make();
	run_for(*s, 1.0f);
	s->petra_pid_ = 1;
	const Simulation::Building *cc = nullptr;
	for (const auto &b : s->buildings_)
		if (b.team == 1 && is_civic_centre(b.type)) cc = &b;
	CHECK(cc != nullptr, "Petra has a civic centre");
	if (!cc) return;
	const uint32_t cc_id = cc->id;
	uint32_t barracks = 0; // the starting one, else a new one
	int n_barracks = 0;
	for (const auto &b : s->buildings_)
		if (b.team == 1 && b.type == BLD_BARRACKS) { barracks = b.id; ++n_barracks; }
	if (!barracks) barracks = s->spawn_building_world(cc->x + 14.0f, cc->z, BLD_BARRACKS, 1, 1.0f), ++n_barracks;
	CHECK(n_barracks == 1, "Petra has one barracks");
	uint16_t tmpl = CIV_UNIT_TEMPLATE_NONE;
	for (uint16_t t : civ_roster_ids(s->players_[1].civ)) {
		const CivUnitTemplate &ct = civ_unit_template(t);
		if (ct.phase > 1 || unit_gather_class(ct.unit_type, t) != GATHER_INFANTRY) continue;
		if (!s->can_train_at(1, ct.unit_type, BLD_BARRACKS, t) || !s->can_train_at(1, ct.unit_type, BLD_TOWN_CENTER, t)) continue;
		tmpl = t;
		break;
	}
	CHECK(tmpl != CIV_UNIT_TEMPLATE_NONE, "a citizen soldier trains at both the barracks and the civic centre");
	if (tmpl == CIV_UNIT_TEMPLATE_NONE) return;
	const uint8_t type = civ_unit_template(tmpl).unit_type;
	auto queue_of = [&](uint32_t id) -> std::vector<ProductionOrder> & {
		return s->buildings_[s->building_id_to_idx_.at(id)].queue;
	};
	queue_of(cc_id).clear();
	queue_of(barracks).clear();
	CHECK(s->petra_best_trainer(tmpl, type, PRODUCTION_QUEUE_MAX) == barracks, "a soldier trains at the barracks over the civic centre");
	ProductionOrder busy;
	busy.unit_type = type;
	busy.tmpl = tmpl;
	busy.timer = 0.0f;
	busy.duration = 12.0f;
	queue_of(barracks).push_back(busy);
	CHECK(s->petra_best_trainer(tmpl, type, PRODUCTION_QUEUE_MAX) == cc_id, "and at the civic centre once the barracks has 10 s more to train");
	queue_of(barracks).clear();

	// One batch for the attack and one for the economy, both at the barracks and about to finish.
	std::vector<uint32_t> before;
	for (size_t i = 0; i < s->units_.count; ++i)
		if (s->units_.alive[i]) before.push_back(s->units_.id[i]);
	for (uint8_t plan : {PETRA_ORDER_ATTACK, PETRA_ORDER_WORKER}) {
		ProductionOrder o;
		o.unit_type = type;
		o.tmpl = tmpl;
		o.duration = 1.0f;
		o.timer = 0.95f;
		o.petra_plan = plan;
		queue_of(barracks).push_back(o);
	}
	run_for(*s, 1.0f);
	int spawned = 0, army = 0;
	for (size_t i = 0; i < s->units_.count; ++i) {
		if (!s->units_.alive[i] || s->units_.team[i] != 1 || s->units_.tmpl[i] != tmpl) continue;
		if (std::find(before.begin(), before.end(), s->units_.id[i]) != before.end()) continue;
		++spawned;
		army += !s->petra_is_worker(i);
	}
	std::printf("  plan tags: %d soldiers trained at the barracks, %d in the army\n", spawned, army);
	CHECK(spawned == 2 && army == 1, "the attack's soldier joins the army, the worker one stays a worker");

	// The tag is saved.
	ProductionOrder raid;
	raid.unit_type = type;
	raid.tmpl = tmpl;
	raid.timer = 0.0f;
	raid.duration = 60.0f;
	raid.petra_plan = PETRA_ORDER_RAID;
	queue_of(barracks).clear();
	queue_of(barracks).push_back(raid);
	const char *path = "/tmp/rtso_test_petra_plan_tags.sav";
	CHECK(s->save_state(path), "saves");
	auto t = make();
	CHECK(t->load_state(path), "loads");
	std::remove(path);
	auto it = t->building_id_to_idx_.find(barracks);
	CHECK(it != t->building_id_to_idx_.end() && !t->buildings_[it->second].queue.empty() &&
	      t->buildings_[it->second].queue.front().petra_plan == PETRA_ORDER_RAID, "a queued batch keeps its plan through a save and load");
}


// attackPlan.js addUnits for a raid: every free FastMoving soldier but two, kept back to hunt.
static void test_raid_keeps_hunters() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 1.0f);
	s->petra_pid_ = 1;
	float ox = 0, oz = 0;
	uint32_t target = 0;
	for (const auto &b : s->buildings_) {
		if (!is_civic_centre(b.type) || b.hp <= 0.0f) continue;
		if (b.team == 1) { ox = b.x; oz = b.z; }
	}
	// An enemy outpost inside our territory is the raid's target (raidTargetEntity).
	target = s->spawn_building_world(ox - 12.0f, oz - 12.0f, BLD_OUTPOST, 0, 1.0f);
	for (int k = 0; k < 3; ++k) s->spawn_unit(ox + 6.0f + 2.0f * k, oz + 6.0f, 1, UNIT_CAVALRY);
	s->petra_scout_unit_id_ = 0;
	int riders = 0;
	for (size_t i = 0; i < s->units_.count; ++i)
		if (s->units_.alive[i] && s->units_.team[i] == 1 &&
		    (s->units_.unit_type[i] == UNIT_CAVALRY || s->units_.unit_type[i] == UNIT_CAMEL || s->units_.unit_type[i] == UNIT_CHARIOT))
			++riders;
	auto &plan = s->petra_raid_plan_;
	plan.state = Simulation::AttackPlan::MUSTER;
	plan.state_timer = 0.0f;
	plan.target_building_id = target;
	plan.squad.clear();
	s->petra_manage_raid(Simulation::FIXED_STEP);
	std::printf("  raid: %d riders, %zu join the raid\n", riders, plan.squad.size());
	CHECK(s->petra_raid_target_ok(target), "the outpost stands in our territory");
	CHECK((int)plan.squad.size() == std::min(4, riders - 2), "a raid leaves two riders home to hunt");
}

// The first base's storehouse check (baseManager.js gatherers), added when it has none yet.
static PetraDropsiteCheck &base_dropsite_check(Simulation &s) {
	Simulation::PetraBasesMap m;
	s.petra_bases_map(m);
	for (auto &c : s.petra_base_dropsite_checks_)
		if (c.anchor_id == m.anchors.at(0).id) return c.check;
	s.petra_base_dropsite_checks_.push_back({m.anchors.at(0).id, PetraDropsiteCheck{}});
	return s.petra_base_dropsite_checks_.back().check;
}

// startingStrategy.js buildFirstBase lays the first wood dropsite and no houses: houses wait for
// buildMoreHouses' houseNeeded. queueplanBuilding.js findPosition keeps a house 60 m clear of
// other structures (-40), and requireHouses drops every influence so a house fits anywhere.
static void test_building_plan_028() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_player_resources(1, 5000, 5000, 5000, 5000);
	fund_queues(*s);
	s->petra_pid_ = 1;
	auto count = [&](uint8_t type) {
		int n = 0;
		for (const auto &b : s->buildings_) n += b.team == 1 && b.type == type && b.hp > 0.0f && !b.wrecked;
		return n;
	};
	const int houses = count(BLD_HOUSE);
	s->petra_opening_step_ = 0;
	for (int k = 0; k < 10; ++k) s->petra_manage_opening(1.0f);
	CHECK(s->petra_opening_step_ == 4, "the opening ends after the first wood dropsite");
	CHECK(count(BLD_HOUSE) == houses, "the opening lays no houses");
	CHECK(count(BLD_STOREHOUSE) >= 1, "the opening lays the first wood dropsite");

	// A house next to a barracks loses 40 at its foundation: the spot a house picks with no
	// barracks around, a barracks there pushes the next pick away.
	int cx = 0, cz = 0;
	CHECK(s->petra_find_position(BLD_HOUSE, nullptr, nullptr, false, cx, cz), "a house finds room");
	const float hx = ((float)cx + 1.0f) * Simulation::GRID, hz = ((float)cz + 1.0f) * Simulation::GRID;
	const uint32_t bar = s->spawn_building_world(hx, hz, BLD_BARRACKS, 1, 1.0f);
	CHECK(bar != 0, "a barracks stands where the house would have gone");
	int cx2 = 0, cz2 = 0;
	CHECK(s->petra_find_position(BLD_HOUSE, nullptr, nullptr, false, cx2, cz2), "a house still finds room");
	const float d = std::hypot(((float)cx2 + 1.0f) * Simulation::GRID - hx, ((float)cz2 + 1.0f) * Simulation::GRID - hz);
	std::printf("  building plan: house %.1f m from the barracks\n", d);
	CHECK(d > 12.0f, "a house keeps away from a barracks");
	// requireHouses: with every influence dropped the house takes the first free tile of the base.
	s->petra_require_houses_ = true;
	int cx3 = 0, cz3 = 0;
	CHECK(s->petra_find_position(BLD_HOUSE, nullptr, nullptr, false, cx3, cz3), "requireHouses still finds room");
	s->petra_require_houses_ = false;
	const float d3 = std::hypot(((float)cx3 + 1.0f) * Simulation::GRID - hx, ((float)cz3 + 1.0f) * Simulation::GRID - hz);
	std::printf("  building plan: requireHouses house %.1f m from the barracks\n", d3);
	CHECK(d3 < d, "requireHouses lets the house come closer");

	// findGoodPosition's gap: none for houses, dropsites, fields and markets, 4 m for most
	// structures, 8 m for fortresses, arsenals and elephant stables.
	CHECK(Simulation::petra_clearance(BLD_HOUSE) == 0 && Simulation::petra_clearance(BLD_STOREHOUSE) == 0 &&
	      Simulation::petra_clearance(BLD_FIELD) == 0 && Simulation::petra_clearance(BLD_MARKET) == 0 &&
	      Simulation::petra_clearance(BLD_DOCK) == 0, "houses, dropsites, fields and markets keep no gap");
	CHECK(Simulation::petra_clearance(BLD_BARRACKS) == 1 && Simulation::petra_clearance(BLD_TEMPLE) == 1,
	      "most structures keep 4 m");
	CHECK(Simulation::petra_clearance(BLD_FORTRESS) == 2 && Simulation::petra_clearance(BLD_ARSENAL) == 2 &&
	      Simulation::petra_clearance(BLD_ELEPHANT_STABLE) == 2, "fortresses, arsenals and elephant stables keep 8 m");
	// Right beside the barracks a house fits where a temple doesn't.
	bool beside = false;
	const int bcx = (int)std::floor(hx / Simulation::GRID), bcz = (int)std::floor(hz / Simulation::GRID);
	for (int dz = -4; dz <= 4 && !beside; ++dz)
		for (int dx = -4; dx <= 4 && !beside; ++dx)
			beside = s->petra_ring_clear(bcx + dx, bcz + dz, 1, 0) && !s->petra_ring_clear(bcx + dx, bcz + dz, 1, 1);
	CHECK(beside, "a house may stand right beside a barracks, a temple may not");

	// checkResourceLevels counts GATHER.GATHERING units only: one walking out to its trees counts
	// as neither used nor lost.
	auto &dc = base_dropsite_check(*s);
	dc = PetraDropsiteCheck{};
	for (size_t i = 0; i < s->units_.count; ++i)
		if (s->units_.team[i] == 1 && s->units_.alive[i]) s->units_.action_state[i] = ACTION_IDLE;
	for (auto &b : s->buildings_)
		if (b.team == 1) b.build_progress = 1.0f; // a storehouse foundation would restart the counts
	const uint32_t walker = s->spawn_unit(hx + 20.0f, hz, 1, UNIT_VILLAGER, 0.0f);
	const int32_t wi = s->lookup_unit(walker);
	s->units_.action_state[wi] = ACTION_GATHER;
	s->units_.res_type_carried[wi] = RES_WOOD;
	s->units_.gather_res[wi] = 0;
	s->petra_check_resource_levels(1000);
	CHECK(dc.used[0] == 0 && dc.lost[0] == 0, "a gatherer walking out isn't counted");
	s->units_.gather_res[wi] = 1;
	s->petra_check_resource_levels(1100); // an empty count looks again 10 turns on
	CHECK(dc.used[0] == 1, "a gatherer at work is counted");

	// queueplanBuilding.js canStart: one foundation per AI turn (turnCache.buildingBuilt).
	s->petra_in_turn_ = true;
	s->petra_turn_built_ = false;
	const uint32_t f1 = s->petra_place_generic(BLD_HOUSE, PETRA_Q_HOUSE);
	const uint32_t f2 = s->petra_place_generic(BLD_HOUSE, PETRA_Q_HOUSE);
	CHECK(f1 != 0 && f2 == 0 && !s->petra_place_no_room_, "a second foundation waits for the next AI turn");
	CHECK(s->petra_place_dropsite(RES_WOOD, 0.0f) == 0, "so does a storehouse");
	s->petra_turn_built_ = false; // the next turn
	CHECK(s->petra_place_generic(BLD_HOUSE, PETRA_Q_HOUSE) != 0, "the next turn lays one");
	s->petra_in_turn_ = s->petra_turn_built_ = false;
	// Played: each AI lays at most one foundation a turn.
	{
		auto m = std::make_unique<Simulation>();
		m->set_sim_seed(7);
		m->reset();
		m->generate_random_map(MAP_RANDOM_CONTINENTAL, 7, 2, 64.0f);
		m->set_player_resources(1, 5000, 5000, 5000, 5000);
		size_t before = m->buildings_.size();
		int most = 0, turns = 0;
		for (int t = 0; t < 4 * 60 * 20; ++t) {
			m->tick(Simulation::FIXED_STEP);
			int added = 0;
			for (size_t k = before; k < m->buildings_.size(); ++k) added += m->buildings_[k].team == 1;
			if (m->buildings_.size() != before) ++turns;
			most = std::max(most, added);
			before = m->buildings_.size();
		}
		std::printf("  one per turn: %d ticks laid foundations, at most %d in one\n", turns, most);
		CHECK(turns > 0 && most == 1, "no AI turn lays two foundations");
	}
}

// baseManager.js checkResourceLevels: a wood count with more than 15% of its gatherers walking
// back lays a storehouse by a forest at least 15 tiles from our dropsites, and its foundation
// restarts every count; headquarters.js buildFarmstead lays one farmstead once there is a
// storehouse and a house.
// queues.dropsites keeps a storehouse plan until it is funded: a check that decides on one it
// can't pay for keeps the decision (and freezes the counts) instead of counting afresh.
// terrain-analysis.js landPassMap: Canyon's cliffs split land as water does, and structures don't.
static void test_land_access() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(7);
	s->reset();
	s->generate_random_map(MAP_CANYON, 7, 2, 64.0f);
	const Simulation::Building *tc = nullptr;
	for (const auto &b : s->buildings_)
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) tc = &b;
	CHECK(tc != nullptr, "a civic centre for player 1");
	if (!tc) return;
	const uint16_t home = s->petra_access_at(tc->x, tc->z);
	int on = 0, off = 0;
	for (size_t r = 0; r < s->resources_.count; ++r)
		if (s->resources_.alive[r] && s->resources_.type[r] == RES_WOOD)
			(s->petra_access_at(s->resources_.x[r], s->resources_.z[r]) == home ? on : off)++;
	CHECK(home != 0 && on > 0 && off > on / 2, "Canyon's trees above the cliffs are on other land");
	// The civic centre's own cell is a structure's, yet on the same land as the ground beside it.
	const float side = tc->x + tc->half_size + 2.0f;
	CHECK(s->petra_access_at(side, tc->z) == home, "a structure doesn't split land");

	// tradeManager.js checkRoutes compares getLandAccess on every map: a market above the
	// cliffs and one beside the civic centre make no land route.
	s->petra_pid_ = 1;
	const float tcx = tc->x, tcz = tc->z;
	const uint32_t near = s->spawn_building_world(side + 6.0f, tcz, BLD_MARKET, 1, 1.0f);
	uint32_t far = 0;
	for (size_t r = 0; r < s->resources_.count && !far; ++r) {
		if (!s->resources_.alive[r] || s->resources_.type[r] != RES_WOOD) continue;
		const float x = s->resources_.x[r] + 6.0f, z = s->resources_.z[r];
		const float dx = x - tcx, dz = z - tcz;
		if (dx * dx + dz * dz < 60.0f * 60.0f || s->petra_access_at(x, z) == home || !s->petra_access_at(x, z)) continue;
		far = s->spawn_building_world(x, z, BLD_MARKET, 1, 1.0f);
	}
	CHECK(near && far, "a market by the civic centre and one above the cliffs");
	Simulation::PetraTradeRoutes routes;
	s->petra_check_routes(routes);
	std::printf("  markets on lands %u and %u: %zu routes\n", home, far ? s->petra_access_at(s->buildings_.back().x, s->buildings_.back().z) : 0,
	            routes.finished.size());
	CHECK(routes.finished.empty() && routes.potential.gain == 0, "no land route across the cliffs");
}

// worker.js isInaccessibleSupply: a gatherer that stands still at its supply for over 10 s
// without carrying more is sent once more, and failing again flags the supply for 600 s; the
// flags and each worker's watch ride in the save (v81).
static void test_inaccessible_supply() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->petra_pid_ = 1;
	auto &u = s->units_;
	int32_t w = -1;
	for (size_t i = 0; i < u.count && w < 0; ++i)
		if (u.alive[i] && u.team[i] == 1 && u.unit_type[i] == UNIT_FEMALE_CITIZEN) w = (int32_t)i;
	uint32_t bush = 0;
	for (size_t r = 0; r < s->resources_.count && !bush; ++r)
		if (s->resources_.alive[r] && s->resources_.type[r] == RES_FOOD && s->resources_.subtype[r] != RES_SUB_FISH) bush = s->resources_.id[r];
	CHECK(w >= 0 && bush != 0, "a woman and a food supply");
	if (w < 0 || !bush) return;
	Simulation::PetraHuntScan scan = s->petra_hunt_scan();
	auto stuck = [&] { // walking at it and getting nowhere
		u.action_state[w] = ACTION_GATHER;
		u.target_resource_id[w] = bush;
		u.carry[w] = 0;
	};
	float flagged_at = -1.0f;
	for (int turn = 0; turn < 40 && flagged_at < 0.0f; ++turn) {
		stuck();
		s->petra_check_inaccessible_supplies(scan);
		if (s->petra_supply_inaccessible(bush)) flagged_at = s->petra_timer_;
		s->petra_timer_ += 1.6f;
	}
	std::printf("  a supply its gatherer can't reach is flagged after %.1f s\n", flagged_at);
	CHECK(flagged_at > 20.0f && flagged_at < 30.0f, "flagged inaccessible after two 10 s stands");
	CHECK(u.action_state[w] == ACTION_IDLE, "the gatherer gives it up to be reassigned (retryWorking)");
	const char *path = "/tmp/rtso_test_petra_inaccessible.sav";
	CHECK(s->save_state(path), "saves");
	auto t = std::make_unique<Simulation>();
	CHECK(t->load_state(path), "loads");
	std::remove(path);
	CHECK(t->petra_supply_inaccessible(bush) && t->petra_inaccessible_.size() == s->petra_inaccessible_.size() &&
	      t->petra_approach_.size() == s->petra_approach_.size(), "the flag and the approach watch survive a save");
	s->petra_timer_ = flagged_at + 600.0f;
	CHECK(!s->petra_supply_inaccessible(bush), "the flag lasts 600 s");
}

static void test_pending_dropsite() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->petra_pid_ = 1;
	for (auto &b : s->buildings_)
		if (b.team == 1) b.build_progress = 1.0f; // no storehouse foundation restarting the counts
	auto storehouses = [&] {
		int n = 0;
		for (const auto &b : s->buildings_) n += b.team == 1 && b.type == BLD_STOREHOUSE && b.hp > 0.0f;
		return n;
	};
	for (auto &a : s->petra_queues_.accounts) a = {0, 0, 0, 0};
	s->set_player_resources(1, 0, 0, 0, 0);
	auto &dc = base_dropsite_check(*s);
	dc = PetraDropsiteCheck{};
	dc.used[0] = 120;
	dc.lost[0] = 40; // 25% walking back
	const int before = storehouses();
	s->petra_check_resource_levels(500);
	CHECK(storehouses() == before && dc.pending == RES_WOOD + 1, "an unpaid wood storehouse stays decided");
	CHECK(dc.used[0] == 0 && dc.lost[0] == 0, "the counts restart once it is decided");
	// worker.js lastHuntSearch rides in the same save (v79).
	s->petra_hunt_search_ = {{1234u, 56.5f}};
	const char *path = "/tmp/rtso_test_petra_pending.sav";
	CHECK(s->save_state(path), "saves");
	auto t = std::make_unique<Simulation>();
	CHECK(t->load_state(path), "loads");
	std::remove(path);
	CHECK(base_dropsite_check(*t).pending == RES_WOOD + 1, "the pending storehouse survives a save");
	CHECK(t->petra_hunt_search_.size() == 1 && t->petra_hunt_search_[0].id == 1234u && t->petra_hunt_search_[0].time == 56.5f,
	      "a hunter's last fruitless search survives a save");
	s->petra_hunt_search_.clear();
	fund_queues(*s);
	s->set_player_resources(1, 5000, 5000, 5000, 5000);
	s->petra_check_resource_levels(501);
	CHECK(storehouses() == before + 1 && dc.pending == 0, "once funded the storehouse goes up");
}

static void test_dropsites() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	auto count = [&](uint8_t type, bool foundations_only) {
		int n = 0;
		for (const auto &b : s->buildings_)
			n += b.team == 1 && b.type == type && b.hp > 0.0f && (!foundations_only || b.build_progress < 1.0f);
		return n;
	};
	// The opening storehouse (buildFirstBase) goes up first: its foundation would reset the counts.
	run_for(*s, 20.0f);
	for (int k = 0; k < 40 && count(BLD_STOREHOUSE, true) > 0; ++k) run_for(*s, 5.0f);
	CHECK(count(BLD_STOREHOUSE, true) == 0, "the opening storehouse is finished");
	for (const auto &b : s->buildings_)
		if (b.team == 1 && b.type == BLD_STOREHOUSE)
			for (const auto &o : s->buildings_)
				if (o.team == 1 && is_civic_centre(o.type))
					CHECK(std::hypot(o.x - b.x, o.z - b.z) >= 15.0f, "the opening storehouse stands by wood, 15 tiles from the civic centre");
	s->set_player_resources(1, 5000, 5000, 5000, 5000);
	fund_queues(*s);
	s->petra_pid_ = 1;
	const int stores = count(BLD_STOREHOUSE, false);
	auto &dc = base_dropsite_check(*s);
	dc = PetraDropsiteCheck{};
	dc.used[0] = 120; dc.lost[0] = 40;
	s->petra_check_resource_levels(1000);
	CHECK(count(BLD_STOREHOUSE, false) == stores + 1, "a high lost share lays a storehouse");
	for (const auto &b : s->buildings_) {
		if (b.team != 1 || b.type != BLD_STOREHOUSE || b.build_progress >= 1.0f) continue;
		for (const auto &o : s->buildings_)
			if (o.team == 1 && o.id != b.id && (is_civic_centre(o.type) || o.type == BLD_STOREHOUSE))
				CHECK(std::hypot(o.x - b.x, o.z - b.z) >= 15.0f, "the new storehouse keeps 15 tiles from our dropsites");
	}
	dc.used[1] = 50; dc.lost[1] = 50;
	s->petra_check_resource_levels(1001);
	CHECK(dc.used[1] == 0 && dc.lost[1] == 0 && dc.next_check[1] == 1001, "a storehouse foundation restarts the counts");
	CHECK(count(BLD_STOREHOUSE, false) == stores + 1, "one storehouse at a time");
	const int farms = count(BLD_FARMSTEAD, false);
	s->petra_build_farmstead();
	s->petra_build_farmstead();
	std::printf("  dropsites: storehouses %d -> %d, farmsteads %d -> %d\n", stores, count(BLD_STOREHOUSE, false), farms, count(BLD_FARMSTEAD, false));
	CHECK(count(BLD_FARMSTEAD, false) == std::max(farms, 1), "one farmstead");
}

// baseManager.js checkResourceLevels, food branch: the first field once the fruit and grain near
// the food dropsites is under 600, then provisionFields foundations (2 at Medium), one fewer
// while more than 300 food is still in reach.
// config.js setConfig: each AI draws its personality within its behaviour's range from the
// match seed, Sandbox keeps the middle, and locked teams add 0.3 cooperativeness.
// chatHelper.js: a line fills in the players, amount and resource it names.
static void test_chat_text() {
	PetraChatLine l;
	l.kind = PETRA_CHAT_ANSWER_ACCEPT_WITH_TRIBUTE;
	l.sub = 1; // neutral
	l.player = 3;
	l.amount = 1200;
	l.resource = RES_GOLD;
	auto name = [](uint8_t p) { return std::string("Player ") + std::to_string(p); };
	bool all_filled = true;
	for (uint8_t v = 0; v < 2; ++v) {
		l.variant = v;
		const std::string t = petra_chat_text(l, name);
		std::printf("  chat: \"%s\"\n", t.c_str());
		all_filled &= t.find("%(") == std::string::npos && t.find("Player 3") != std::string::npos &&
		              t.find("1200 metal") != std::string::npos;
	}
	CHECK(all_filled, "a tribute answer names the player, the amount and the resource");
	// The line and the resource name go through the translation first, then the names go in.
	{
		l.variant = 0;
		const std::string t = petra_chat_text(l, name, [](const char *msgid) {
			return std::string(msgid) == "metal" ? std::string("métal") : std::string("[fr] ") + msgid;
		});
		CHECK(t.rfind("[fr] ", 0) == 0 && t.find("1200 métal") != std::string::npos && t.find("Player 3") != std::string::npos,
		      "a translated tribute answer names the player, the amount and the resource");
	}
	// Every kind has lines, and none leaves a placeholder unfilled.
	for (uint8_t k = 0; k < PETRA_CHAT_KIND_COUNT; ++k)
		for (uint8_t sub = 0; sub < 3; ++sub) {
			const PetraChatPool p = petra_chat_pool(k, sub);
			CHECK(p.count > 0, "every chat kind has lines");
			for (uint8_t v = 0; v < p.count; ++v) {
				PetraChatLine c; c.kind = k; c.sub = sub; c.variant = v; c.player = 2; c.player2 = 4;
				CHECK(petra_chat_text(c, name).find("%(") == std::string::npos, "no placeholder left in a line");
			}
		}
}

static void test_personality_draw() {
	Simulation s;
	s.set_sim_seed(4242);
	s.set_petra_behavior(PETRA_BEHAVIOR_AGGRESSIVE);
	for (uint8_t p = 1; p < 4; ++p) s.set_petra_behavior(p, PETRA_BEHAVIOR_AGGRESSIVE);
	s.set_petra_behavior(4, PETRA_BEHAVIOR_DEFENSIVE);
	const float a2 = s.petra_config_for(2).aggressive, a3 = s.petra_config_for(3).aggressive;
	assert(a2 >= 0.73f && a2 <= 1.0f && a3 >= 0.73f && a3 <= 1.0f);
	assert(a2 != a3);
	const PetraConfig &d = s.petra_config_for(4);
	assert(d.aggressive >= 0.0f && d.aggressive <= 0.27f && d.defensive >= 0.73f);
	Simulation t;
	t.set_sim_seed(4242);
	t.set_petra_behavior(2, PETRA_BEHAVIOR_AGGRESSIVE);
	assert(t.petra_config_for(2).aggressive == a2); // same seed, same draw on every peer
	// Allied victory (no Last Man Standing) adds 0.15, locked teams 0.3 instead.
	t.set_last_man_standing(true);
	const float coop = t.petra_config_for(2).cooperative;
	t.set_last_man_standing(false);
	assert(std::fabs(t.petra_config_for(2).cooperative - std::min(1.0f, coop + 0.15f)) < 1e-5f);
	t.set_lock_teams(true);
	assert(std::fabs(t.petra_config_for(2).cooperative - std::min(1.0f, coop + 0.3f)) < 1e-5f);
	t.set_petra_difficulty(2, PETRA_SANDBOX);
	// setConfig draws no personality on Sandbox: the default 0.5 whatever the behaviour.
	assert(t.petra_config_for(2).aggressive == 0.5f && t.petra_config_for(2).defensive == 0.5f &&
	       t.petra_config_for(2).pop_scaling == 0.5f);
	// -autostart-aiseed: a set AI seed draws another personality, the same one on every peer
	// and whatever the match seed; 0 goes back to the match seed's draw.
	Simulation u;
	u.set_sim_seed(4242);
	u.set_petra_behavior(2, PETRA_BEHAVIOR_AGGRESSIVE);
	u.set_ai_seed(99);
	const float seeded = u.petra_config_for(2).aggressive;
	Simulation v;
	v.set_sim_seed(777);
	v.set_ai_seed(99);
	v.set_petra_behavior(2, PETRA_BEHAVIOR_AGGRESSIVE);
	assert(seeded != a2 && v.petra_config_for(2).aggressive == seeded);
	u.set_ai_seed(0);
	assert(u.petra_config_for(2).aggressive == a2);
	std::printf("personality: aggressive %.2f / %.2f, defensive AI %.2f, AI seed 99 %.2f\n", a2, a3, d.aggressive, seeded);
}

// headquarters.js trainEmergencyUnits: citizen infantry from the civic centre nearest the
// threat, garrisoned inside (the centre rallies to itself while they train), and the
// centre's unstarted items stopped; the rally goes once they are out.
static void test_emergency_units() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 20.0f);
	Simulation::Building *cc = nullptr;
	for (auto &b : s->buildings_)
		if (b.team == 1 && is_civic_centre(b.type) && b.build_progress >= 1.0f) cc = &b;
	CHECK(cc != nullptr, "Petra has a civic centre");
	if (!cc) return;
	for (int r = 0; r < RES_COUNT; ++r) s->players_[1].res[r] = 2000.0f;
	const uint32_t cc_id = cc->id;
	// No more than 5 s of training under way there: the started items go, the others stay.
	for (size_t q = cc->queue.size(); q-- > 0;)
		if (cc->queue[q].started || cc->queue[q].timer > 0.0f) s->cmd_cancel_train(1, cc_id, q);
	const size_t before = cc->garrisoned_units.size();
	const float far_x = cc->x + 60.0f; // 240 m: beyond distcut
	CHECK(!s->petra_train_emergency_units({{far_x, cc->z}}), "no emergency units for a threat beyond 141 m");
	const bool queued = s->petra_train_emergency_units({{cc->x + 10.0f, cc->z}});
	cc = &s->buildings_[s->building_id_to_idx_.at(cc_id)];
	bool infantry = false, unstarted_other = false;
	for (const auto &o : cc->queue) {
		const TechClasses cls = unit_tech_classes(o.unit_type);
		if ((cls & TC_INFANTRY) && (cls & TC_CITIZENSOLDIER)) infantry = true;
		else if (!o.started && o.timer <= 0.0f) unstarted_other = true;
	}
	std::printf("  emergency: queued %s, queue %zu items, rally to itself %s\n", queued ? "yes" : "no", cc->queue.size(),
	            s->petra_emergency_rally(*cc) ? "yes" : "no");
	CHECK(queued && infantry, "a citizen infantry unit is queued at the civic centre");
	CHECK(s->petra_emergency_rally(*cc), "the civic centre rallies to itself (garrisonType protection)");
	CHECK(!unstarted_other, "its other unstarted items are stopped");
	// Trained straight into the garrison (garrisonManager lets it out again with no enemy near).
	size_t most = before;
	for (float t = 0.0f; t < 120.0f && !s->buildings_[s->building_id_to_idx_.at(cc_id)].queue.empty(); t += Simulation::FIXED_STEP) {
		s->tick(Simulation::FIXED_STEP);
		most = std::max(most, s->buildings_[s->building_id_to_idx_.at(cc_id)].garrisoned_units.size());
	}
	most = std::max(most, s->buildings_[s->building_id_to_idx_.at(cc_id)].garrisoned_units.size());
	run_for(*s, 2.0f);
	cc = &s->buildings_[s->building_id_to_idx_.at(cc_id)];
	std::printf("  emergency: garrison %zu -> %zu, rally %s\n", before, most, s->petra_emergency_rally(*cc) ? "kept" : "gone");
	CHECK(most > before, "the emergency unit is trained into the civic centre's garrison");
	CHECK(!s->petra_emergency_rally(*cc), "the rally goes once the queue is empty");
}

// baseManager.js neededDefenders: a civic centre Petra gains from Medium on trains
// 3 + 2 (difficulty - 3) emergency units into its garrison, one after another.
static void test_new_cc_defenders() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->set_petra_difficulty(1, PETRA_HARD);
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 20.0f);
	const Simulation::Building *cc = nullptr;
	for (const auto &b : s->buildings_)
		if (b.team == 1 && is_civic_centre(b.type) && b.build_progress >= 1.0f) cc = &b;
	CHECK(cc != nullptr, "Petra has a civic centre");
	if (!cc) return;
	// Copy the position: spawning resizes buildings_ and would leave cc dangling.
	const float ccx = cc->x, ccz = cc->z;
	cc = nullptr;
	for (const auto &b : s->petra_bases()) CHECK(b.needed_defenders == 0, "the first civic centre trains no defenders");
	const uint32_t tc = s->spawn_building_world(ccx * 0.1f, ccz * 0.1f, BLD_TOWN_CENTER, 1, 1.0f);
	CHECK(tc != 0, "a second civic centre is placed");
	if (!tc) return;
	auto needed = [&]() -> int {
		for (const auto &b : s->petra_bases()) if (b.anchor_building_id == tc) return b.needed_defenders;
		return -1;
	};
	// Nothing to pay with until the base is seen, so none is queued yet.
	for (float t = 0.0f; t < 5.0f && needed() < 0; t += Simulation::FIXED_STEP) {
		for (int r = 0; r < RES_COUNT; ++r) s->players_[1].res[r] = 0.0f;
		s->tick(Simulation::FIXED_STEP);
	}
	const int start = needed();
	size_t most = 0;
	for (float t = 0.0f; t < 150.0f && needed() != 0; t += Simulation::FIXED_STEP) {
		for (int r = 0; r < RES_COUNT; ++r) s->players_[1].res[r] = std::max(s->players_[1].res[r], 1000.0f);
		s->tick(Simulation::FIXED_STEP);
		auto it = s->building_id_to_idx_.find(tc);
		if (it != s->building_id_to_idx_.end()) most = std::max(most, s->buildings_[it->second].garrisoned_units.size());
	}
	std::printf("  new civic centre at Hard (%.0f units from the first): %d defenders wanted, %d left, garrison up to %zu\n", 0.9f * std::hypot(ccx, ccz), start, needed(), most);
	CHECK(start == 5, "a new civic centre at Hard wants 5 defenders (3 + 2 (4 - 3))");
	CHECK(needed() == 0, "it trains them all");
	CHECK(most >= 1, "they garrison it");
}

static void test_food_fields() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 2.0f);
	s->set_player_resources(1, 5000, 5000, 5000, 5000);
	fund_queues(*s);
	s->petra_pid_ = 1;
	auto fields = [&](bool foundations_only) {
		int n = 0;
		for (const auto &b : s->buildings_)
			n += b.team == 1 && b.type == BLD_FIELD && b.hp > 0.0f && (!foundations_only || b.build_progress < 1.0f);
		return n;
	};
	CHECK(fields(false) == 0, "no field at the start");
	s->petra_manage_fields();
	CHECK(fields(false) == 0, "berries in reach: no field yet");
	std::vector<std::pair<size_t, int>> fruit;
	for (size_t r = 0; r < s->resources_.count; ++r)
		if (s->resources_.alive[r] && s->resources_.type[r] == RES_FOOD && s->resources_.subtype[r] != RES_SUB_FISH &&
		    s->resources_.subtype[r] != RES_SUB_MEAT) {
			fruit.push_back({r, (int)s->resources_.amount[r]});
			s->resources_.amount[r] = 0;
		}
	s->petra_manage_fields();
	const int first = fields(false);
	s->petra_manage_fields();
	const int second = fields(false);
	s->petra_manage_fields();
	const int third = fields(false);
	std::printf("  food fields: none while berries last, then %d, %d, %d\n", first, second, third);
	CHECK(first == 1, "the first field once the food in reach is gone");
	CHECK(second == 2 && third == 2, "two field foundations at a time (provisionFields)");
	for (const auto &[r, amount] : fruit) s->resources_.amount[r] = amount;
	for (auto &b : s->buildings_)
		if (b.team == 1 && b.type == BLD_FIELD && b.build_progress < 1.0f) { b.build_progress = 1.0f; break; }
	s->petra_manage_fields();
	CHECK(fields(true) == 1 && fields(false) == 2, "one foundation at a time while food is still in reach");
}

// headquarters.js buildTemple, buildWonder and saveResources: a temple once a market stands in
// the city phase (none while saving resources), a wonder every 30th AI turn near the population
// cap when the stock covers it, no defenses while saving without barter, and a new civic centre ends the saving (handleNewBase).
static void test_temple_wonder_save() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 20.0f);
	s->petra_pid_ = 1;
	const Simulation::Building *cc = nullptr;
	for (const auto &b : s->buildings_)
		if (b.team == 1 && is_civic_centre(b.type) && b.build_progress >= 1.0f) cc = &b;
	CHECK(cc != nullptr, "Petra has a civic centre");
	if (!cc) return;
	const float cx = cc->x, cz = cc->z;
	auto count = [&](uint8_t type) {
		int n = 0;
		for (const auto &b : s->buildings_) n += b.team == 1 && b.type == type && b.hp > 0.0f;
		return n;
	};
	auto temples = [&] { return count(BLD_TEMPLE) + count(BLD_TEMPLE_VESTA); };
	auto fund = [&] {
		s->set_player_resources(1, 50000, 50000, 50000, 50000);
		for (auto &a : s->petra_queues_.accounts) a = {50000, 50000, 50000, 50000};
		for (auto &p : s->petra_queues_.paused) p = 0; // checkPausedQueues counts few workers yet
	};
	CHECK(s->spawn_building_world(cx + 30.0f, cz, BLD_MARKET, 1, 1.0f) != 0, "a market is placed");
	CHECK(s->spawn_building_world(cx - 30.0f, cz, BLD_FORGE, 1, 1.0f) != 0, "a forge is placed");
	s->players_[1].phase = 2;
	fund();
	s->petra_attack_mgr_.save_resources = 0;
	// AI 1's played turn `p` (headquarters.js gates on playedTurn).
	auto set_turn = [&](uint32_t p) { s->ticks_ = 4 * (4 + 8 * (uint64_t)(p - 2)); };
	set_turn(3);
	CHECK(s->petra_played_turn() == 3, "the played turn follows the tick count");
	s->petra_manage_research(0.1f);
	CHECK(temples() == 0, "no temple in the town phase");
	s->players_[1].phase = 3;
	s->petra_attack_mgr_.save_resources = 1;
	fund();
	s->petra_manage_research(0.1f);
	CHECK(temples() == 0, "no temple while saving resources");
	s->petra_attack_mgr_.save_resources = 0;
	fund();
	s->petra_manage_research(0.1f);
	const int t = temples();
	fund();
	s->petra_manage_research(0.1f);
	CHECK(t == 1 && temples() == 1, "one temple in the city phase");

	s->pop_cap_ = s->pop_current(1); // population at its maximum
	fund();
	set_turn(33);
	s->petra_manage_research(0.1f);
	CHECK(count(BLD_WONDER) == 0, "no wonder off the 30th AI turn");
	set_turn(60);
	s->petra_manage_research(0.1f);
	const int wonders = count(BLD_WONDER);
	std::printf("  temple %d, wonder %d at population %d of %d\n", t, wonders, s->pop_current(1), s->pop_max(1));
	CHECK(wonders == 1, "a wonder on the 30th AI turn near the population cap");
	s->pop_cap_ = -1;

	s->petra_defense_build_.pending = 0;
	s->petra_attack_mgr_.save_resources = 1;
	for (auto &b : s->buildings_) if (b.team == 1 && b.type == BLD_MARKET) b.hp = 0.0f;
	s->petra_build_defenses();
	CHECK(s->petra_defense_build_.pending == 0, "no defenses while saving without barter");
	s->petra_attack_mgr_.save_resources = 0;
	s->petra_build_defenses();
	CHECK(s->petra_defense_build_.pending != 0, "defenses once not saving");

	s->petra_attack_mgr_.save_resources = 1;
	const uint32_t tc = s->spawn_building_world(cx * 0.1f, cz * 0.1f, BLD_TOWN_CENTER, 1, 1.0f);
	CHECK(tc != 0, "a second civic centre is placed");
	bool seen = false;
	for (float tt = 0.0f; tt < 5.0f && !seen; tt += Simulation::FIXED_STEP) {
		s->tick(Simulation::FIXED_STEP);
		for (const auto &b : s->petra_bases()) seen |= b.anchor_building_id == tc;
	}
	CHECK(seen && s->petra_attack_mgr_.save_resources == 0, "a new civic centre ends the saving");
}

// headquarters.js manageCorral: without fields, Petra breeds an animal of a kind while it owns no
// more of that kind than it has corrals (Gaia's animals don't count), and lays another corral
// while it has fewer than its phase number and over 30 population per corral.
// headquarters.js checkBaseExpansion: more than one kind of structure with no room left
// (buildManager.numberMissingRoom) sends Petra to found a new base, below the population
// threshold; a structure placed again clears its kind.
static void test_missing_room_expansion() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 2.0f);
	s->petra_pid_ = 1;
	s->players_[1].phase = 2;
	s->set_player_resources(1, 5000, 5000, 5000, 5000);
	for (auto &a : s->petra_queues_.accounts) a = {5000, 5000, 5000, 5000};
	for (auto &p : s->petra_queues_.paused) p = 0; // a 2 s old village pauses the civic centre queue
	auto bases = [&] {
		int n = 0;
		for (const auto &b : s->buildings_) n += b.team == 1 && b.hp > 0.0f && (is_civic_centre(b.type) || b.type == BLD_MILITARY_COLONY);
		return n;
	};
	const int before = bases();
	s->petra_attack_mgr_.missing_room = 1u << 0; // houses only
	s->petra_check_base_expansion();
	CHECK(bases() == before, "one kind without room: no new base");
	const uint32_t house = s->petra_place_generic(BLD_HOUSE, PETRA_Q_HOUSE);
	CHECK(house != 0 && s->petra_attack_mgr_.missing_room == 0, "a house placed clears its kind");
	s->petra_attack_mgr_.missing_room = (1u << 0) | (1u << 5); // houses and barracks
	s->petra_check_base_expansion();
	std::printf("  missing room: %d bases before, %d after\n", before, bases());
	CHECK(bases() == before + 1, "two kinds without room: a new base");
	// A search that finds no spot (asked for in a corner no base holds) sets its kind.
	s->petra_attack_mgr_.missing_room = 0;
	const float corner[2] = {-60.0f, -60.0f};
	CHECK(s->petra_place_generic(BLD_WONDER, PETRA_Q_WONDER, corner) == 0 &&
	          s->petra_attack_mgr_.missing_room == (1u << 7),
	      "no room for a wonder sets its kind");
}

static void test_corral_by_owner() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->set_victory_condition(VICTORY_ENDLESS);
	run_for(*s, 2.0f);
	s->petra_pid_ = 1;
	s->set_player_resources(1, 5000, 5000, 5000, 5000);
	s->petra_queues_.accounts[PETRA_Q_CORRAL] = {5000, 5000, 5000, 5000};
	s->triggers_.disabled_buildings[1] |= bld_bit(BLD_FIELD);
	const Simulation::Building *cc = nullptr;
	for (const auto &b : s->buildings_)
		if (b.team == 1 && is_civic_centre(b.type) && b.build_progress >= 1.0f) cc = &b;
	CHECK(cc != nullptr, "Petra has a civic centre");
	if (!cc) return;
	const float cx = cc->x, cz = cc->z;
	const uint32_t corral = s->spawn_building_world(cx + 20.0f, cz, BLD_CORRAL, 1, 1.0f);
	CHECK(corral != 0, "a corral is placed");
	auto corrals = [&] {
		int n = 0;
		for (const auto &b : s->buildings_) n += b.team == 1 && b.type == BLD_CORRAL && b.hp > 0.0f;
		return n;
	};
	auto queued = [&] {
		auto it = s->building_id_to_idx_.find(corral);
		return it == s->building_id_to_idx_.end() ? 0 : (int)s->buildings_[it->second].queue.size();
	};
	const uint8_t first = CORRAL_TRAINABLE[s->players_[1].civ].fauna[0];
	const uint8_t second = CORRAL_TRAINABLE[s->players_[1].civ].fauna[1];
	auto manage = [&] { s->ticks_ = (s->ticks_ / 16 + 1) * 16 + 12; s->petra_manage_fields(); };
	// Two of our own animals of the first kind: the corral breeds the second kind, though Gaia's
	// animals of that kind stand beside it.
	s->spawn_fauna(cx + 22.0f, cz, first, 1);
	s->spawn_fauna(cx + 23.0f, cz, first, 1);
	for (int k = 0; k < 4; ++k) s->spawn_fauna(cx + 21.0f, cz + 2.0f, second); // Gaia's, beside the corral
	manage();
	CHECK(corrals() == 1, "no second corral in the village phase");
	CHECK(queued() == 1, "an animal is bred");
	auto it = s->building_id_to_idx_.find(corral);
	if (it != s->building_id_to_idx_.end() && !s->buildings_[it->second].queue.empty())
		CHECK(s->buildings_[it->second].queue[0].tmpl == second, "the second kind: two of the first are ours, none of the second");
	s->players_[1].phase = 2;
	for (int k = 0; k < 30; ++k) s->spawn_unit(cx - 10.0f, cz + (float)k * 0.5f, 1, UNIT_VILLAGER);
	manage();
	std::printf("  corrals: %d at pop %d in the town phase, %d bred\n", corrals(), s->pop_current(1), queued());
	CHECK(corrals() == 2, "a second corral in the town phase past 30 population");
}

int main(int argc, char **argv) {
	// Optional test names on the command line run just those.
	auto want = [&](const char *name) {
		if (argc < 2) return true;
		for (int k = 1; k < argc; ++k) if (!std::strcmp(argv[k], name)) return true;
		return false;
	};
#define RUN(t) if (want(#t)) t()
	std::printf("test_petra\n");
	RUN(test_no_early_rush);
	RUN(test_phase_up);
	RUN(test_no_free_income);
	RUN(test_mercenary_workers);
	RUN(test_roster_army);
	RUN(test_diplomacy);
	RUN(test_ai_slots);
	RUN(test_attack_plans);
	RUN(test_attack_rally_point);
	RUN(test_attack_waits_for_each_class);
	RUN(test_slot_config);
	RUN(test_allow_capture);
	RUN(test_garrisons);
	RUN(test_diplomacy_requests);
	RUN(test_tribute_request_paid_later);
	RUN(test_hunting);
	RUN(test_hunters_028);
	RUN(test_first_base);
	RUN(test_barter);
	RUN(test_first_market_border);
	RUN(test_pop_phase2_save_resources);
	RUN(test_petra_config_pop_scaling);
	RUN(test_petra_no_units);
	RUN(test_market_prospection);
	RUN(test_market_dock_route);
	RUN(test_civ_structures);
	RUN(test_emergency);
	RUN(test_regicide_hero);
	RUN(test_regicide_hero_no_garrison);
	RUN(test_regicide_hero_tower_fire);
	RUN(test_nomad_first_base);
	RUN(test_entity_limit_placement);
	RUN(test_army_answers_attackers);
	RUN(test_attack_targets_target_player);
	RUN(test_siege_prefers_any_gate);
	RUN(test_raid_sizes_vary);
	RUN(test_attack_blocked_by_walls);
	RUN(test_mustering_units_defend);
	RUN(test_attack_request);
	RUN(test_attack_request_counts_raid);
	RUN(test_relic_capture);
	RUN(test_relic_enemy_player);
	RUN(test_relic_gaia_target);
	RUN(test_retreat_walk_forced);
	RUN(test_expansion_small_nodes);
	RUN(test_dropsite_sites);
	RUN(test_structure_cost_by_civ);
	RUN(test_repairs);
	RUN(test_idle_return);
	RUN(test_idle_return_to_wagon);
	RUN(test_gather_limits);
	RUN(test_decay_garrison);
	RUN(test_decay_garrison_from_attack);
	RUN(test_decay_destroy);
	RUN(test_defense_by_strength);
	RUN(test_defense_armies);
	RUN(test_defense_ignores_ships);
	RUN(test_defense_switch_to_attack);
	RUN(test_capturing_armies);
	RUN(test_defenders_stop_hopeless_capture);
	RUN(test_assign_bombers);
	RUN(test_cancel_attacks_on_peace);
	RUN(test_builders_per_foundation);
	RUN(test_wonder_guards);
	RUN(test_victory_critical_ents);
	RUN(test_placement_miss_memory_exact);
	RUN(test_defend_allies);
	RUN(test_retake_civic_centre);
	RUN(test_research_choices);
	RUN(test_research_parity_028);
	RUN(test_concurrent_attacks);
	RUN(test_fasten_end_game);
	RUN(test_abort_nearest_base);
	RUN(test_mobile_dropsite_follows);
	RUN(test_worker_mix);
	RUN(test_unit_choice);
	RUN(test_build_defenses);
	RUN(test_defensive_location);
	RUN(test_raid_keeps_hunters);
	RUN(test_order_plan_tags);
	RUN(test_dropsites);
	RUN(test_building_plan_028);
	RUN(test_food_fields);
	RUN(test_corral_by_owner);
	RUN(test_missing_room_expansion);
	RUN(test_emergency_units);
	RUN(test_new_cc_defenders);
	RUN(test_temple_wonder_save);
	RUN(test_personality_draw);
	RUN(test_chat_text);
	RUN(test_pending_dropsite);
	RUN(test_inaccessible_supply);
	RUN(test_land_access);
	std::printf(g_failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", g_failures);
	return g_failures ? 1 : 0;
}
