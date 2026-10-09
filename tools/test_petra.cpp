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
//     unlock and trains champions (researchWantedTechs unlock_champion).
//  Exit: 0 = PASS, 1 = FAIL.
// The ally check reads the sim's death log, which has no public accessor.
#define private public
#include "simulation/simulation.h"
#include "simulation/entity_limits_data.h"
#undef private
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
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
		const auto st = s->petra_attack_plan().state;
		if (st == Simulation::AttackPlan::WALKING || st == Simulation::AttackPlan::ATTACKED) attacked = true;
	}
	std::printf("  arena: %.0fs in village phase, player units %d -> %d, attack launched: %s, rushes allowed %d\n",
	            t, start_units, count_units(*s, 0, false), attacked ? "yes" : "no", s->petra_attack_manager().max_rushes);
	CHECK(!attacked, "no village-phase attack");
	CHECK(s->petra_attack_manager().max_rushes == 0, "no rushes on a wood-poor base (setRushes)");
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
		s->players_[1].civ = (Civilization)civ;
		s->set_victory_condition(VICTORY_ENDLESS); // keep playing once the idle player is beaten
		// Garrisoned fortresses (ten more arrows each, BuildingAI) outlast the infantry's first
		// waves, so the siege (trained some ten minutes into the city phase at 0 A.D.'s gather
		// rates) still has something to fire at.
		for (const float *at : {(const float[2]){42.0f, 42.0f}, (const float[2]){26.0f, 50.0f}, (const float[2]){50.0f, 26.0f}}) {
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
			bool idle_alive = false;
			for (const auto &b : s->buildings())
				if (b.team == 0 && b.hp > 0.0f && !b.wrecked) idle_alive = true;
			if (!idle_alive) s->spawn_building_world(42.0f, 42.0f, BLD_FORTRESS, 0, 1.0f);
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
		if (civ == CIV_MACEDONIANS) CHECK(tower, "builds a siege tower where the civ has one");
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

static bool said_to(const Simulation &s, uint8_t from, uint8_t to, uint8_t kind) {
	for (const auto &l : s.pending_petra_chat())
		if (l.from == from && l.to == to && l.kind == kind) return true;
	return false;
}

static void run_for(Simulation &s, float seconds) {
	for (float t = 0.0f; t < seconds && !s.is_game_over(); t += Simulation::FIXED_STEP) s.tick(Simulation::FIXED_STEP);
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
	// A human offers an alliance to each AI. Every offer gets an answer, and
	// paying an asked-for tribute seals it.
	int accepted = 0, declined = 0, paid = 0;
	for (uint32_t seed = 1; seed <= 6; ++seed) {
		auto s = three_player_match(seed);
		run_for(*s, 20.0f);
		s->clear_petra_chat();
		s->cmd_set_diplomacy(0, 1, Simulation::DIPLO_ALLY);
		run_for(*s, 10.0f);
		const auto &req = s->petra_diplo_.received[0]; // AI 1's state lives in the members
		if (said_to(*s, 1, 0, PETRA_CHAT_DIPLO_ACCEPT)) {
			CHECK(s->is_ally(1, 0), "accepted offer makes the AI an ally");
			++accepted;
		} else if (said_to(*s, 1, 0, PETRA_CHAT_DIPLO_DECLINE)) {
			CHECK(!s->is_ally(1, 0), "declined offer leaves the AI hostile");
			++declined;
		} else if (said_to(*s, 1, 0, PETRA_CHAT_DIPLO_TRIBUTE)) {
			CHECK(req.status == PetraReceivedRequest::WAITING_FOR_TRIBUTE && req.wanted >= 1000, "asks for at least 1000");
			s->players_[0].res[req.resource] += (float)req.wanted;
			CHECK(s->send_tribute(1, req.resource, req.wanted), "human pays the tribute");
			run_for(*s, 10.0f);
			CHECK(s->is_ally(1, 0), "paid tribute seals the alliance");
			++paid;
		} else {
			CHECK(false, "AI answers an alliance offer");
		}
	}
	std::printf("  alliance offers over 6 seeds: accepted %d, declined %d, accepted for tribute %d\n",
	            accepted, declined, paid);

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
		// only structure a rush may target (rushTargetFinder).
		auto s = match(PETRA_BEHAVIOR_AGGRESSIVE);
		const uint32_t store = s->spawn_building_world(-55.0f, 40.0f, BLD_STOREHOUSE, 0, 1.0f);
		bool rushed = false, rush_at_store = false;
		int attack_size = 0, huge_size = 0;
		for (float t = 0.0f; t < 20.0f * 60.0f; t += Simulation::FIXED_STEP) {
			s->tick(Simulation::FIXED_STEP);
			const auto &m = s->petra_attack_manager();
			const auto &plan = s->petra_attack_plan();
			if (plan.state != Simulation::AttackPlan::WALKING) continue;
			if (m.kind == PETRA_ATTACK_RUSH && s->petra_phase() == 1) {
				rushed = true;
				rush_at_store |= plan.target_building_id == store;
			}
			if (m.kind == PETRA_ATTACK_ATTACK) attack_size = m.target_size;
			if (m.kind == PETRA_ATTACK_HUGE) huge_size = m.target_size;
		}
		const auto &m = s->petra_attack_manager();
		std::printf("  aggressive: rushes allowed %d, village rush %s (at the storehouse %s), launched rush %d attack %d huge %d, sizes %d / %d\n",
		            m.max_rushes, rushed ? "yes" : "no", rush_at_store ? "yes" : "no", m.launched[PETRA_ATTACK_RUSH],
		            m.launched[PETRA_ATTACK_ATTACK], m.launched[PETRA_ATTACK_HUGE], attack_size, huge_size);
		CHECK(m.max_rushes == 3, "an aggressive AI on a wood-rich base plans three rushes");
		CHECK(rushed && rush_at_store, "rushes the undefended storehouse in the village phase");
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
		// Fortresses on the far side keep the idle player standing after the Attacks raze its
		// base (an unmanned fortress shoots only its 10 default arrows, BuildingAI), so there is
		// still a target to plan the HugeAttack against.
		s->spawn_building_world(50.0f, 50.0f, BLD_FORTRESS, 0, 1.0f);
		s->spawn_building_world(55.0f, 10.0f, BLD_FORTRESS, 0, 1.0f);
		s->spawn_building_world(10.0f, 55.0f, BLD_FORTRESS, 0, 1.0f);
		int attack_size = 0, huge_size = 0;
		std::vector<int> kinds;
		auto last = Simulation::AttackPlan::UNEXECUTED;
		// At 0 A.D.'s gather rates the HugeAttack starts mustering around minute 14-16 on this
		// map; 20 min leaves room for map changes.
		// An army that razes the idle player finds it rebuilt, so every plan has a target.
		float home_x = 0.0f, home_z = 0.0f;
		for (const auto &b : s->buildings()) if (b.team == 0 && b.type == BLD_TOWN_CENTER) { home_x = b.x; home_z = b.z; }
		int step = 0;
		for (float t = 0.0f; t < 20.0f * 60.0f; t += Simulation::FIXED_STEP) {
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
		CHECK(m.launched[PETRA_ATTACK_ATTACK] == 2, "both Attack plans march");
		CHECK(huge_size > 2 * attack_size, "a HugeAttack musters a much larger army");
	}
	{
		auto s = match(PETRA_BEHAVIOR_DEFENSIVE);
		for (float t = 0.0f; t < 5.0f; t += Simulation::FIXED_STEP) s->tick(Simulation::FIXED_STEP);
		CHECK(s->petra_attack_manager().rushes_set && s->petra_attack_manager().max_rushes == 0,
		      "a defensive AI plans no rush");
	}
	{
		// Raid: an enemy house in Petra's territory, cavalry and spearmen at hand.
		auto s = match(PETRA_BEHAVIOR_BALANCED);
		const uint32_t house = s->spawn_building_world(-10.0f, -40.0f, BLD_HOUSE, 0, 1.0f);
		for (int i = 0; i < 4; ++i) {
			s->spawn_unit(-30.0f + 2.0f * (float)i, -12.0f, 1, UNIT_CAVALRY);
			s->spawn_unit(-30.0f + 2.0f * (float)i, -10.0f, 1, UNIT_SPEARMAN);
		}
		bool raided = false, cavalry_only = true;
		size_t raiders = 0;
		for (float t = 0.0f; t < 20.0f && !raided; t += Simulation::FIXED_STEP) {
			s->tick(Simulation::FIXED_STEP);
			const auto &raid = s->petra_raid_plan();
			if (raid.state != Simulation::AttackPlan::WALKING || raid.target_building_id != house) continue;
			raided = true;
			raiders = raid.squad.size();
			const auto u = s->units();
			for (uint32_t id : raid.squad)
				for (size_t i = 0; i < u.count; ++i)
					if (u.id[i] == id && u.unit_type[i] != UNIT_CAVALRY) cavalry_only = false;
		}
		std::printf("  raid on a house in Petra's territory: %s with %zu riders, cavalry only %s\n",
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
	for (int i = 0; i < 4; ++i) squad.push_back(s->spawn_unit(-40.0f + 2.0f * (float)i, -2.0f, 1, UNIT_SPEARMAN));
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
	for (int i = 0; i < 3; ++i) archers.push_back(s->spawn_unit(tx + 14.0f + 2.0f * (float)i, tz + 12.0f, 1, UNIT_ARCHER));
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
	std::printf("  garrisons: %d of 3 archers manned the Town Center under attack, %d left after\n", manned, left);
	CHECK(manned == 3, "infantry man a Town Center an enemy is in range of");
	CHECK(left == 3, "and leave once no enemy is near");

	// A hurt woman under attack shelters in the (healing) Town Center.
	const uint32_t woman = s->spawn_unit(tx + 11.0f, tz - 11.0f, 1, UNIT_FEMALE_CITIZEN);
	const uint32_t foe = s->spawn_unit(tx + 14.0f, tz - 11.0f, 0, UNIT_SWORDSMAN);
	const int32_t w = s->lookup_unit(woman);
	u.hp[w] = u.max_hp[w] * 0.5f;
	s->order_attack(&foe, 1, woman);
	bool sheltered = false;
	for (float t = 0.0f; t < 10.0f && !sheltered; t += Simulation::FIXED_STEP) {
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
	s->players_[3].civ = s->players_[1].civ; // the same civ never declines for good at once
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
	s->players_[3].civ = s->players_[1].civ;
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
	for (auto &b : s->buildings_)
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { tx = b.x; tz = b.z; s->destroy_building(b); }
	run_for(*s, 0.1f);
	auto &u = s->units_;
	for (size_t i = 0; i < u.count; ++i) // a nomad start: everyone stands idle
		if (u.alive[i] && u.team[i] == 1) { u.action_state[i] = ACTION_IDLE; u.has_order[i] = 0; u.target_resource_id[i] = 0; }
	PlayerState &ps = s->players_[1];
	ps.food() = 0.0f; ps.wood() = 50.0f; ps.gold() = 100.0f; ps.stone() = 200.0f;
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
	for (float &g : ps.gathered_window) g = 0.0f;
	ps.food() = 3000.0f; ps.wood() = 0.0f; ps.gold() = 0.0f; ps.stone() = 0.0f;
	s->petra_queues_.wants[Simulation::PETRA_QUEUE_MILITARY].wood = 300.0f; // a plan waits on wood
	const bool necessity = s->petra_perform_barter();
	std::printf("  barter: necessity deal %s, food %.0f wood %.0f\n", necessity ? "yes" : "no", ps.food(), ps.wood());
	CHECK(necessity && ps.wood() > 0.0f && ps.food() < 3000.0f, "sells spare food for the wood a plan waits on");

	s->petra_queues_.wants[Simulation::PETRA_QUEUE_MILITARY] = PetraQueueAccount{};
	ps.food() = 6000.0f; ps.wood() = 2000.0f; ps.gold() = 300.0f; ps.stone() = 2000.0f;
	const float gold_before = ps.gold();
	const bool contingency = s->petra_perform_barter();
	std::printf("  barter: contingency deal %s, gold %.0f -> %.0f\n", contingency ? "yes" : "no", gold_before, ps.gold());
	CHECK(contingency && ps.gold() > gold_before, "sells spare food for the scarcest finite resource");

	ps.food() = 100.0f; ps.wood() = 3000.0f; ps.gold() = 100.0f; ps.stone() = 3000.0f;
	s->petra_set_trading_goods();
	const uint8_t *g = s->trading_goods(1);
	std::printf("  trading goods: food %d wood %d stone %d gold %d\n", g[RES_FOOD], g[RES_WOOD], g[RES_STONE], g[RES_GOLD]);
	CHECK(g[RES_FOOD] + g[RES_WOOD] + g[RES_STONE] + g[RES_GOLD] == 100, "trading goods add up to 100");
	CHECK(g[RES_GOLD] >= 20 && g[RES_WOOD] == 0 && g[RES_STONE] == 0, "short on gold, none for what it has plenty of");
}

static void test_civ_structures() {
	for (uint8_t civ : { (uint8_t)CIV_KUSHITES, (uint8_t)CIV_ROMANS }) {
		auto s = std::make_unique<Simulation>();
		s->set_sim_seed(42);
		s->reset();
		s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
		s->players_[1].civ = (Civilization)civ;
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
		std::printf("  %s by 40 min: camp %s, temple %s, champions %s\n", civ_info((Civilization)civ).name,
		            camp ? "yes" : "no", temple ? "yes" : "no", champion ? "yes" : "no");
		if (civ == CIV_KUSHITES) CHECK(camp && temple, "Kush builds its camps and the Temple of Amun");
		else CHECK(temple && champion, "Rome builds the Temple of Vesta and unlocks its champions");
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
	run_for(*s, 3.0f);
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
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	auto town_centers = [&](bool built) {
		int n = 0;
		for (const auto &b : s->buildings())
			if (b.team == 1 && b.type == BLD_TOWN_CENTER && !b.wrecked && (!built || b.build_progress >= 1.0f)) ++n;
		return n;
	};
	const int at_start = town_centers(false);
	run_for(*s, 150.0f);
	int buildings = 0;
	for (const auto &b : s->buildings())
		if (b.team == 1 && !b.wrecked && b.build_progress >= 1.0f) ++buildings;
	std::printf("  nomad: Town Centers at start %d, built after 150 s %d, finished buildings %d, workers %d\n",
	            at_start, town_centers(true), buildings, count_units(*s, 1, true));
	CHECK(at_start == 0, "a Nomad start has no civic centre");
	CHECK(town_centers(true) == 1, "Petra founds and builds its first Town Center");
	CHECK(buildings >= 3, "and builds on from it");
}

// EntityLimits (D's #159): a Mauryan Pillar only stands while Ashoka lives.
// Petra asks before it searches for a spot, so it neither lays one out nor
// scans the map for one every turn (it did ~200k refused cells a game).
static void test_entity_limit_placement() {
	auto s = std::make_unique<Simulation>();
	s->set_sim_seed(42);
	s->reset();
	s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
	s->players_[1].civ = CIV_MAURYANS;
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
	const uint32_t placed = s->petra_place_building(BLD_PILLAR, tx, tz, 2, 16, Simulation::PETRA_QUEUE_MILITARY);
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
	std::printf("  army under attack at a structure: the soldier hit turns on its attacker: %s (plan state %d)\n",
	            turned ? "yes" : "no", (int)plan.state);
	CHECK(turned, "a Petra soldier attacked while at a structure fights back at once");
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
	run_for(*s, 60.0f);
	const auto &near = s->relics_[0], &far = s->relics_[1];
	std::printf("  relics: own-territory relic %s, unseen relic %s\n",
		near.claimed ? (near.owner_team == 1 ? "taken by AI 1" : "taken by another") : "left", far.claimed ? "taken" : "left alone");
	CHECK(near.claimed && near.owner_team == 1, "Petra captures a Gaia relic in its territory");
	CHECK(!far.claimed, "and leaves a relic it has never seen");
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
		auto &acc = s->petra_queues_.accounts[Simulation::PETRA_QUEUE_TECH];
		acc.food = acc.wood = acc.gold = acc.stone = 5000.0f;
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
	const bool tower_fails = !s->petra_place_building(BLD_TOWER, tx, tz, 3, 4, Simulation::PETRA_QUEUE_TECH);
	const bool tower_remembered = s->petra_place_misses_.size() == 1;
	s->destroy_building(s->buildings_[s->building_id_to_idx_.at(old_tower)]);
	const uint32_t tower = s->petra_place_building(BLD_TOWER, tx, tz, 3, 4, Simulation::PETRA_QUEUE_TECH);

	// Trees on every cell a temple could take in rings 5-6 around the Civic Centre.
	const int foot = building_type_info(BLD_TEMPLE).footprint;
	for (int dx = -6; dx <= 6 + foot; ++dx)
		for (int dz = -6; dz <= 6 + foot; ++dz)
			if (std::max(std::abs(dx), std::abs(dz)) >= 5 || dx > 4 || dz > 4)
				s->spawn_resource_world((hcx + dx + 0.5f) * G, (hcz + dz + 0.5f) * G, RES_WOOD, 200);
	s->resource_block_tick_ = UINT64_MAX; // the obstruction cache is per tick; this is the same tick
	fund();
	const bool temple_fails = !s->petra_place_building(BLD_TEMPLE, hx, hz, 5, 6, Simulation::PETRA_QUEUE_TECH);
	const bool still_fails = !s->petra_place_building(BLD_TEMPLE, hx, hz, 5, 6, Simulation::PETRA_QUEUE_TECH);
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
	const uint32_t temple = s->petra_place_building(BLD_TEMPLE, hx, hz, 5, 6, Simulation::PETRA_QUEUE_TECH);
	std::printf("  placement miss memory: tower %s, %s after its neighbour fell; temple %s, %s after felling\n",
	            tower_fails ? "blocked" : "not blocked", tower ? "placed" : "not placed",
	            temple_fails && still_fails ? "blocked" : "not blocked", temple ? "placed" : "not placed");
	CHECK(have_p, "found a tower spot to test with");
	CHECK(tower_fails && tower_remembered, "a search with no spot fails and is remembered");
	CHECK(tower != 0, "losing the tower that blocked a spot lets the next search use it");
	CHECK(temple_fails && still_fails && freed, "trees block every temple spot until felled");
	CHECK(temple != 0, "felling the trees on a spot lets the next search use it");
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

int main() {
	std::printf("test_petra\n");
	test_no_early_rush();
	test_phase_up();
	test_no_free_income();
	test_roster_army();
	test_diplomacy();
	test_ai_slots();
	test_attack_plans();
	test_slot_config();
	test_allow_capture();
	test_garrisons();
	test_diplomacy_requests();
	test_tribute_request_paid_later();
	test_hunting();
	test_first_base();
	test_barter();
	test_civ_structures();
	test_emergency();
	test_regicide_hero();
	test_regicide_hero_no_garrison();
	test_nomad_first_base();
	test_entity_limit_placement();
	test_army_answers_attackers();
	test_relic_capture();
	test_retreat_walk_forced();
	test_placement_miss_memory_exact();
	std::printf(g_failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", g_failures);
	return g_failures ? 1 : 0;
}
