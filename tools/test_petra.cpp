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
//  Exit: 0 = PASS, 1 = FAIL.
// The ally check reads the sim's death log, which has no public accessor.
#define private public
#include "simulation/simulation.h"
#undef private
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
	CHECK(city_at > 0.0f && city_at < 15.0f * 60.0f, "city phase within 15 minutes (Town structures built first)");
	CHECK(peak_workers >= 25, "economy grows past 25 workers");
	CHECK(peak_fields >= 2, "farms fields once hunt and berries run low");
}

// Petra fields its civ's roster army: a hero, champions and siege engines once
// it reaches the city phase (attackPlan.js unitStat Hero / Champ* / Siege).
static void test_roster_army() {
	bool any_unpacked_fired = false;
	for (uint8_t civ : { (uint8_t)CIV_ATHENIANS, (uint8_t)CIV_MACEDONIANS }) {
		auto s = std::make_unique<Simulation>();
		s->set_sim_seed(42);
		s->reset();
		s->generate_random_map(MAP_RANDOM_CONTINENTAL, 42, 2, 64.0f);
		s->players_[1].civ = (Civilization)civ;
		s->set_victory_condition(VICTORY_ENDLESS); // keep playing once the idle player is beaten
		// A fortress outlasts the infantry's first wave, so the siege has something to fire at.
		s->spawn_building_world(42.0f, 42.0f, BLD_FORTRESS, 0, 1.0f);
		bool hero = false, champion = false, siege = false, tower = false, packed_moved = false, unpacked_fired = false;
		for (float t = 0.0f; t < 20.0f * 60.0f && !s->is_game_over(); t += Simulation::FIXED_STEP) {
			s->tick(Simulation::FIXED_STEP);
			if (s->ticks_ % 20 != 0) continue; // once a second
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
		std::printf("  %s by 20 min: phase %d, hero %s, champions %s, siege %s, siege tower %s, siege moves packed %s, fires unpacked %s\n",
		            civ_info((Civilization)civ).name, s->petra_phase(), hero ? "yes" : "no", champion ? "yes" : "no",
		            siege ? "yes" : "no", tower ? "yes" : "no",
		            packed_moved ? "yes" : "no", unpacked_fired ? "yes" : "no");
		CHECK(hero, "trains a hero in the city phase");
		CHECK(champion, "trains roster champions in the city phase");
		CHECK(siege, "trains siege engines");
		if (civ == CIV_MACEDONIANS) CHECK(tower, "builds a siege tower where the civ has one");
		CHECK(packed_moved, "siege packs to follow the army");
		any_unpacked_fired |= unpacked_fired;
	}
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
		for (float t = 0.0f; t < 12.0f * 60.0f; t += Simulation::FIXED_STEP) {
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
		auto s = match(PETRA_BEHAVIOR_AGGRESSIVE);
		int attack_size = 0, huge_size = 0;
		for (float t = 0.0f; t < 12.0f * 60.0f; t += Simulation::FIXED_STEP) {
			s->tick(Simulation::FIXED_STEP);
			const auto &m = s->petra_attack_manager();
			if (s->petra_attack_plan().state != Simulation::AttackPlan::WALKING) continue;
			if (m.kind == PETRA_ATTACK_ATTACK) attack_size = m.target_size;
			if (m.kind == PETRA_ATTACK_HUGE) huge_size = m.target_size;
		}
		const auto &m = s->petra_attack_manager();
		std::printf("  no rush target: rushes planned %d, launched attack %d then huge %d, target sizes %d / %d\n",
		            m.rush_number, m.launched[PETRA_ATTACK_ATTACK], m.launched[PETRA_ATTACK_HUGE], attack_size, huge_size);
		CHECK(m.launched[PETRA_ATTACK_RUSH] == 0, "no rush without an undefended target");
		CHECK(m.launched[PETRA_ATTACK_ATTACK] == 2, "the first two plans are Attack");
		CHECK(m.launched[PETRA_ATTACK_HUGE] >= 1, "then HugeAttack");
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

int main() {
	std::printf("test_petra\n");
	test_no_early_rush();
	test_phase_up();
	test_no_free_income();
	test_roster_army();
	test_diplomacy();
	test_ai_slots();
	test_attack_plans();
	std::printf(g_failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", g_failures);
	return g_failures ? 1 : 0;
}
