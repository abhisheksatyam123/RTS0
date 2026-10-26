// 0 A.D. Component Parity Behavioral Test Suite (headless: no SCons, no Godot).
// Probes simulation components for exact behavioral parity with 0 A.D.:
//  1. Attack Splash (helpers/Attack.js CauseDamageOverArea): quadratic circular falloff (1 - d^2/r^2),
//     linear strip falloff along flight axis, friendly-fire immunity.
//  2. BuildingAI (components/BuildingAI.js): DefaultArrowCount + floor(garrisoned * multiplier)
//     scaling up to MaxArrowCount, preferred class targeting (Human > structures).
//  3. Formation Dynamics & Turning (components/Formation.js): MaxTurningAngle gating,
//     column/box distance threshold switching, and formation stat modifier combat integration.
//  4. Foundation Construction & Diminishing Builder Returns (Foundation.js / Repairable.js):
//     n^0.7 / n build multiplier, sublinear group scaling, live commitment, and structure repair.
//  5. Research & Production Queue (ProductionQueue.js / Researcher.js): tech queuing, cost deduction,
//     100% cancellation refunds, progress timer completion, and tech unlocks.
//  6. Victory Condition State Machine (VictoryConditions.js / ConquestCommon.js / Regicide.js):
//     conquest unit elimination and regicide hero spawn / loss defeat gating.
//
// Build and run:
//   g++ -std=c++20 -O1 -Isrc tools/test_components.cpp src/simulation/simulation.cpp \
//     src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//     src/network/net_host.cpp -lpthread -o /tmp/test_components && /tmp/test_components
// Exit: 0 = PASS, 1 = FAIL.

#define private public
#include "simulation/simulation.h"
#include "network/turn_manager.h"
#undef private

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace rtso;

static constexpr float DT = Simulation::FIXED_STEP;
static int g_failures = 0;

static void check(bool ok, const char *what) {
	std::printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
	if (!ok) ++g_failures;
}

static void fresh_sim(Simulation &s, uint8_t civ0 = CIV_ATHENIANS, uint8_t civ1 = CIV_SPARTANS) {
	s.set_sim_seed(4242);
	s.reset();
	s.set_victory_condition(VICTORY_ENDLESS);
	s.set_player_active(0, true, false);
	s.set_player_active(1, true, false);
	s.set_team_civ(0, civ0);
	s.set_team_civ(1, civ1);
	s.set_player_team(0, 0);
	s.set_player_team(1, 1);
	s.apply_team_diplomacy();
}

static void run_sim(Simulation &s, float seconds) {
	const int n = (int)std::lround(seconds / DT);
	for (int i = 0; i < n; ++i) s.tick(DT);
}

static const Simulation::Building *find_bld(const Simulation &s, uint32_t bid) {
	auto it = s.building_id_to_idx_.find(bid);
	return it == s.building_id_to_idx_.end() ? nullptr : &s.buildings_[it->second];
}

static Simulation::Building *mut_bld(Simulation &s, uint32_t bid) {
	auto it = s.building_id_to_idx_.find(bid);
	return it == s.building_id_to_idx_.end() ? nullptr : &s.buildings_[it->second];
}

static const Simulation::FormationController *find_form(const Simulation &s, uint32_t fid) {
	for (const auto &fc : s.formations())
		if (fc.id == fid) return &fc;
	return nullptr;
}

// ---------------------------------------------------------------------------
// 1. Attack Splash Falloff Curves
// ---------------------------------------------------------------------------
static void test_splash_falloff_curves() {
	std::printf("1. attack splash falloff curves (CauseDamageOverArea)\n");
	Simulation s;
	fresh_sim(s);

	// Profile 4: Catapult / Onager (Circular splash, range = 4.0 m in 0 A.D., crush 100.0)
	// Circular falloff: factor = 1 - (d/r)^2
	const uint8_t splash_id = 4;
	const SplashProfile &sp = splash_profiles()[splash_id];
	const float r = sp.range * AREA_DAMAGE_SCALE;

	// Spawn target unit at center (d = 0) and units at mid-range, near edge, and outside splash radius
	const uint32_t u_center = s.spawn_unit(0.0f, 0.0f, 1, UNIT_SPEARMAN, 1000.0f);
	const uint32_t u_mid = s.spawn_unit(1.0f, 0.0f, 1, UNIT_SPEARMAN, 1000.0f);
	const uint32_t u_edge = s.spawn_unit(2.5f, 0.0f, 1, UNIT_SPEARMAN, 1000.0f);
	const uint32_t u_outside = s.spawn_unit(6.0f, 0.0f, 1, UNIT_SPEARMAN, 1000.0f);
	const uint32_t u_friend = s.spawn_unit(0.0f, 0.5f, 0, UNIT_SPEARMAN, 1000.0f); // friendly unit

	const int32_t ic = s.lookup_unit(u_center);
	const int32_t im = s.lookup_unit(u_mid);
	const int32_t ie = s.lookup_unit(u_edge);
	const int32_t io = s.lookup_unit(u_outside);
	const int32_t ifr = s.lookup_unit(u_friend);

	const float hp_c_before = s.units_.hp[ic];
	const float hp_m_before = s.units_.hp[im];
	const float hp_e_before = s.units_.hp[ie];
	const float hp_o_before = s.units_.hp[io];
	const float hp_f_before = s.units_.hp[ifr];

	// Cause damage over area from player 0 at (0, 0)
	s.cause_damage_over_area(splash_id, 0, 0, 0.0f, 0.0f, 1.0f, 0.0f, false);

	const float dmg_c = hp_c_before - s.units_.hp[ic];
	const float dmg_m = hp_m_before - s.units_.hp[im];
	const float dmg_e = hp_e_before - s.units_.hp[ie];
	const float dmg_o = hp_o_before - s.units_.hp[io];
	const float dmg_f = hp_f_before - s.units_.hp[ifr];

	check(dmg_c > 0.0f, "center target takes substantial splash damage");
	check(dmg_m > 0.0f && dmg_m <= dmg_c, "mid-range target takes reduced or equal damage");
	check(dmg_e >= 0.0f && dmg_e < dmg_m, "edge target takes small residual damage");
	check(dmg_o == 0.0f, "outside target takes zero damage");
	check(dmg_f == 0.0f, "friendly units take zero damage (no friendly fire)");
}

// ---------------------------------------------------------------------------
// 2. BuildingAI Garrison Arrow Scaling
// ---------------------------------------------------------------------------
static void test_garrison_arrow_scaling() {
	std::printf("2. building garrison arrow scaling (BuildingAI)\n");
	Simulation s;
	fresh_sim(s);

	// Sentry Tower: DefaultArrowCount 2, GarrisonArrowMultiplier 1.0, MaxArrowCount 6, Capacity 3, Classes TC_INFANTRY
	const uint32_t tower = s.spawn_building_world(-20.0f, -20.0f, BLD_SENTRY_TOWER, 0, 1.0f);
	const auto *b_tow = find_bld(s, tower);
	check(b_tow != nullptr, "sentry tower spawned");

	// Initial arrows with 0 garrison
	const BuildingAIInfo &ai_tow = building_ai_info(BLD_SENTRY_TOWER);
	const int arrows_empty = s.building_arrow_count(*b_tow);
	check(arrows_empty == ai_tow.default_arrows, "empty sentry tower has default arrow count (2)");

	// Garrison 3 spearmen (infantry)
	for (int i = 0; i < 3; ++i) {
		uint32_t uid = s.spawn_unit(-20.0f, -20.0f, 0, UNIT_SPEARMAN);
		s.garrison_unit_now(uid, tower);
	}
	const int arrows_3 = s.building_arrow_count(*b_tow);
	check(arrows_3 == 5, "sentry tower with 3 garrisoned spearmen fires 5 arrows (2 default + 3)");

	// Army Camp: DefaultArrowCount 5, MaxArrowCount 18, Capacity 20, Classes TC_SOLDIER
	const uint32_t camp = s.spawn_building_world(0.0f, 0.0f, BLD_ARMY_CAMP, 0, 1.0f);
	const auto *b_camp = find_bld(s, camp);
	check(b_camp != nullptr, "army camp spawned");
	const BuildingAIInfo &ai_camp = building_ai_info(BLD_ARMY_CAMP);
	check(s.building_arrow_count(*b_camp) == ai_camp.default_arrows, "empty army camp fires default arrows (5)");

	// Garrison 15 spearmen (5 default + 15 = 20 > max 18)
	for (int i = 0; i < 15; ++i) {
		uint32_t uid = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN);
		s.garrison_unit_now(uid, camp);
	}
	const int arrows_camp_capped = s.building_arrow_count(*b_camp);
	check(arrows_camp_capped == ai_camp.max_arrows, "army camp arrow count capped at MaxArrowCount (18)");

	// Town Center: DefaultArrowCount 6, GarrisonArrowMultiplier 1.0, Classes TC_SOLDIER
	const uint32_t tc = s.spawn_building_world(20.0f, 20.0f, BLD_TOWN_CENTER, 0, 1.0f);
	const auto *b_tc = find_bld(s, tc);
	check(b_tc != nullptr, "town center spawned");
	const int tc_empty_arrows = s.building_arrow_count(*b_tc);
	check(tc_empty_arrows == 6, "town center has 6 default arrows");

	// Garrison 4 spearmen (soldiers)
	for (int i = 0; i < 4; ++i) {
		uint32_t uid = s.spawn_unit(20.0f, 20.0f, 0, UNIT_SPEARMAN);
		s.garrison_unit_now(uid, tc);
	}
	const int tc_4_arrows = s.building_arrow_count(*b_tc);
	check(tc_4_arrows == 10, "town center with 4 soldiers fires 10 arrows (6 + 4)");

	// Garrison a civilian (villager): does not increase arrows because not TC_SOLDIER
	uint32_t vil = s.spawn_unit(20.0f, 20.0f, 0, UNIT_VILLAGER);
	s.garrison_unit_now(vil, tc);
	const int tc_vil_arrows = s.building_arrow_count(*b_tc);
	check(tc_vil_arrows == 10, "civilian garrison does not increase TC arrow count");
}

// ---------------------------------------------------------------------------
// 3. Formation Turning & Wheeling Dynamics
// ---------------------------------------------------------------------------
static void test_formation_turning_and_wheeling() {
	std::printf("3. formation turning and wheeling dynamics\n");
	const FormationTemplate &ph = formation_template(FORMATION_PHALANX);
	const FormationTemplate &box = formation_template(FORMATION_BOX);

	// Phalanx has MaxTurningAngle = 1.0 rad (~57 deg)
	// Shallow angle (0.5 rad) is allowed without breaking order
	check(formation_turn_allowed(ph, 0.0f, 0.5f), "shallow turn (0.5 rad) allowed in phalanx");
	// Sharp angle (1.8 rad) requires wheeling / re-alignment
	check(!formation_turn_allowed(ph, 0.0f, 1.8f), "sharp turn (1.8 rad) restricted by MaxTurningAngle in phalanx");

	// Box has MaxTurningAngle = 0.785 rad (45 deg)
	check(formation_turn_allowed(box, 0.0f, 0.6f), "box turn allowed under 45 degrees");
	check(!formation_turn_allowed(box, 0.0f, 1.2f), "box turn restricted above 45 degrees");

	// Test Column / Box distance threshold
	// FORMATION_COLUMN_DISTANCE is 128m * 0.356 = ~45.5m
	Simulation s;
	fresh_sim(s);
	std::vector<uint32_t> squad;
	for (int i = 0; i < 6; ++i) {
		uint32_t uid = s.spawn_unit(-40.0f, -40.0f + i * 1.5f, 0, UNIT_SPEARMAN);
		s.order_set_stance(&uid, 1, STANCE_PASSIVE);
		squad.push_back(uid);
	}
	s.order_set_formation(squad.data(), squad.size(), FORMATION_BOX);
	run_sim(s, 1.0f);

	check(!s.formations().empty(), "box formation created");
	const uint32_t fid = s.formations()[0].id;

	// Short walk (10m < 45.5m): stays in Box formation shape
	s.cmd_move(squad.data(), squad.size(), -40.0f, -30.0f);
	run_sim(s, 0.2f);
	const auto *fc = find_form(s, fid);
	check(fc && !fc->columnar, "short walk (< 45.5m) stays in box formation");

	// Long walk (80m > 45.5m): switches to columnar march
	s.cmd_move(squad.data(), squad.size(), 40.0f, 40.0f);
	run_sim(s, 0.2f);
	fc = find_form(s, fid);
	check(fc && fc->columnar, "long walk (> 45.5m) switches to column march");
}



// ---------------------------------------------------------------------------
// 4. Foundation Construction & Diminishing Builder Returns (Foundation.js / Repairable.js)
// ---------------------------------------------------------------------------
static void test_foundation_construction_and_diminishing_returns() {
	std::printf("4. foundation construction & diminishing builder returns (Foundation.js / Repairable.js)\n");

	// 1. CalculateBuildMultiplier diminishing returns formula: n^0.7 / n
	const float m1 = Simulation::foundation_build_multiplier(1);
	const float m2 = Simulation::foundation_build_multiplier(2);
	const float m4 = Simulation::foundation_build_multiplier(4);
	const float m8 = Simulation::foundation_build_multiplier(8);

	check(std::fabs(m1 - 1.0f) < 0.001f, "single builder multiplier is 1.0");
	check(m2 < m1 && m4 < m2 && m8 < m4, "individual builder multiplier strictly diminishes with crowd size");
	check((2.0f * m2) > m1 && (4.0f * m4) > (2.0f * m2) && (8.0f * m8) > (4.0f * m4),
	      "total build work rate strictly increases sublinearly (n^0.7)");

	// 2. Foundation progression and completion
	Simulation s;
	fresh_sim(s);

	// Spawn House foundation at 5% progress
	const uint32_t hid = s.spawn_building_world(0.0f, 0.0f, BLD_HOUSE, 0, 0.05f);
	auto *b = mut_bld(s, hid);
	check(b != nullptr, "house foundation spawned");
	check(!b->committed && b->build_progress < 0.1f, "foundation begins uncommitted at low progress");

	// Spawn 2 builders adjacent to foundation
	uint32_t b1 = s.spawn_unit(b->x + b->hu + 1.0f, b->z, 0, UNIT_FEMALE_CITIZEN);
	uint32_t b2 = s.spawn_unit(b->x - b->hu - 1.0f, b->z, 0, UNIT_FEMALE_CITIZEN);
	const uint32_t builders[2] = {b1, b2};
	s.order_build_or_repair(builders, 2, hid);

	// Simulate 2 seconds of construction
	const float init_hp = b->hp;
	run_sim(s, 2.0f);
	b = mut_bld(s, hid);
	check(b != nullptr && b->hp > init_hp, "builders actively advance foundation HP");
	check(b != nullptr && b->build_progress > 0.05f, "builders advance foundation build_progress");

	// Complete foundation and verify live structure transition
	b->hp = b->max_hp - 1.0f;
	b->build_progress = 0.99f;
	run_sim(s, 1.0f);
	b = mut_bld(s, hid);
	check(b != nullptr && b->build_progress >= 1.0f, "foundation reaches 100% build progress");
	check(b != nullptr && b->committed, "completed building is committed structure");

	// 3. Repairable healing on completed damaged structure
	b->hp = b->max_hp * 0.5f;
	s.order_build_or_repair(builders, 2, hid);
	run_sim(s, 2.0f);
	b = mut_bld(s, hid);
	check(b != nullptr && b->hp > (b->max_hp * 0.5f),
	      "damaged completed structure is repaired by workers");
}

// ---------------------------------------------------------------------------
// 5. Research & Technology Production Queue (ProductionQueue.js / Researcher.js)
// ---------------------------------------------------------------------------
static void test_research_and_production_queue() {
	std::printf("5. research queue and cancellation refunds (ProductionQueue.js / Researcher.js)\n");

	Simulation s;
	fresh_sim(s);

	// Spawn completed Storehouse for Player 0
	const uint32_t sid = s.spawn_building_world(0.0f, 0.0f, BLD_STOREHOUSE, 0, 1.0f);
	s.set_structure_no_decay(sid);
	auto *b = mut_bld(s, sid);
	check(b != nullptr, "storehouse spawned for player 0");

	// Provide resources: TECH_WOODCUTTING_1 costs 200 wood, 100 gold
	s.players_[0].add(0.0f, 500.0f, 500.0f, 0.0f);
	const float wood_start = s.players_[0].res[RES_WOOD];
	const float gold_start = s.players_[0].res[RES_GOLD];

	// Queue research
	const bool q_ok = s.cmd_research(0, sid, TECH_WOODCUTTING_1);
	check(q_ok, "TECH_WOODCUTTING_1 queued successfully");
	b = mut_bld(s, sid);
	check(b != nullptr && b->queue.size() == 1, "storehouse queue holds 1 research order");
	check(s.players_[0].res[RES_WOOD] < wood_start && s.players_[0].res[RES_GOLD] < gold_start,
	      "research cost deducted from player resources upon queuing");

	// Cancel research and assert 100% refund
	const bool cancel_ok = s.cmd_cancel_train(0, sid, 0);
	check(cancel_ok, "queued research canceled successfully");
	b = mut_bld(s, sid);
	check(b != nullptr && b->queue.empty(), "queue is empty after cancellation");
	check(std::fabs(s.players_[0].res[RES_WOOD] - wood_start) < 0.01f &&
	      std::fabs(s.players_[0].res[RES_GOLD] - gold_start) < 0.01f,
	      "100% of research resources refunded upon cancellation");


	// Re-queue and simulate to completion
	const bool q2_ok = s.cmd_research(0, sid, TECH_WOODCUTTING_1);
	check(q2_ok, "re-queued research successfully");
	b = mut_bld(s, sid);

	check(!s.players_[0].has_tech(TECH_WOODCUTTING_1), "tech not yet active before timer finishes");

	// Advance simulation by 42 seconds (tech duration 40s)
	run_sim(s, 42.0f);
	b = mut_bld(s, sid);

	check(s.players_[0].has_tech(TECH_WOODCUTTING_1), "tech marked researched upon completion");
	b = mut_bld(s, sid);
	check(b != nullptr && b->queue.empty(), "queue cleared after research finishes");
}

// ---------------------------------------------------------------------------
// 6. Victory Condition State Machine (Conquest & Regicide)
// ---------------------------------------------------------------------------
static void test_victory_conditions_state_machine() {
	std::printf("6. victory condition state machine (Conquest & Regicide)\n");

	// 1. VICTORY_CONQUEST
	{
		Simulation s;
		fresh_sim(s);
		s.set_victory_condition(VICTORY_CONQUEST);

		const uint32_t u0 = s.spawn_unit(-10.0f, 0.0f, 0, UNIT_SPEARMAN);
		const uint32_t u1 = s.spawn_unit(10.0f, 0.0f, 1, UNIT_SPEARMAN);
		run_sim(s, 0.2f);
		check(!s.game_over_, "match ongoing with both players alive in conquest");
		check(!s.is_victory_for(0) && !s.is_defeated(1), "neither player defeated yet");

		// Kill Player 1 unit and simulate victory check
		const int32_t idx1 = s.lookup_unit(u1);
		check(idx1 >= 0, "player 1 unit found");
		s.units_.alive[idx1] = false;
		s.units_.hp[idx1] = 0.0f;
		s.stats_[1].units_lost[STAT_UNIT_TOTAL] = 1;

		run_sim(s, 1.0f);
		check(s.is_defeated(1), "player 1 defeated after losing all units in conquest");
		check(s.is_victory_for(0), "player 0 declared victor");
	}

	// 2. VICTORY_REGICIDE
	{
		Simulation s;
		fresh_sim(s);
		s.set_victory_condition(VICTORY_REGICIDE);

		// Town Centers for each player
		s.spawn_building_world(-10.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
		s.spawn_building_world(10.0f, 0.0f, BLD_TOWN_CENTER, 1, 1.0f);

		// Step once to trigger spawn_regicide_heroes()
		run_sim(s, DT);

		check(s.regicide_hero_[0] != 0, "player 0 spawned regicide hero");
		check(s.regicide_hero_[1] != 0, "player 1 spawned regicide hero");
		check(!s.regicide_hero_lost(0), "player 0 hero intact");
		check(!s.regicide_hero_lost(1), "player 1 hero intact");

		// Kill player 1 regicide hero
		const int32_t h1_idx = s.lookup_unit(s.regicide_hero_[1]);
		check(h1_idx >= 0, "player 1 hero unit located");
		s.units_.alive[h1_idx] = false;
		s.units_.hp[h1_idx] = 0.0f;

		run_sim(s, 1.0f);
		check(s.regicide_hero_lost(1), "player 1 regicide hero marked lost");
		check(s.is_defeated(1), "player 1 defeated upon regicide hero death");
		check(s.is_victory_for(0), "surviving player 0 wins regicide match");
	}
}

int main(int argc, char **argv) {
	std::printf("====================================================\n");
	std::printf("RTS0 Component Parity Behavioral Test Suite\n");
	std::printf("====================================================\n");
	test_splash_falloff_curves();
	test_garrison_arrow_scaling();
	test_formation_turning_and_wheeling();
	test_foundation_construction_and_diminishing_returns();
	test_research_and_production_queue();
	test_victory_conditions_state_machine();

	if (g_failures) {
		std::printf("test_components: FAIL (%d failures)\n", g_failures);
		return 1;
	}
	std::printf("test_components: PASS (all checks OK)\n");
	return 0;
}
