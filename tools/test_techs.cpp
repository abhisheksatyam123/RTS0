// 0 A.D. technology tree: table sanity, research rules, effects, save/load
// and lockstep.
//   g++ -std=c++20 -O2 -Isrc tools/test_techs.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_techs && /tmp/test_techs
#include "simulation/simulation.h"
#include "network/turn_manager.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static uint32_t fresh(Simulation &s) {
	s.set_sim_seed(11);
	s.reset();
	s.set_player_resources(0, 5000, 5000, 5000, 5000);
	return s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
}

static void run(Simulation &s, float seconds) {
	for (int t = 0; t < (int)(seconds / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
}

static float queued_duration(const Simulation &s, uint32_t bld) {
	for (const auto &b : s.buildings())
		if (b.id == bld && !b.queue.empty()) return b.queue.front().duration;
	return -1.0f;
}

static float unit_hp(const Simulation &s, uint32_t id) {
	auto u = s.units();
	for (size_t i = 0; i < u.count; ++i) if (u.id[i] == id) return u.hp[i];
	return -1.0f;
}

static const Simulation::Building *find_bld(const Simulation &s, uint32_t id) {
	for (const auto &b : s.buildings()) if (b.id == id) return &b;
	return nullptr;
}

static float unit_max_hp(const Simulation &s, uint32_t id) {
	auto u = s.units();
	for (size_t i = 0; i < u.count; ++i) if (u.id[i] == id) return u.max_hp[i];
	return -1.0f;
}

// Field + farmstead + 5 women farming; returns food gathered in `seconds`.
static int farm_for(Simulation &s, float seconds) {
	uint32_t f = s.spawn_building_world(10.0f, 10.0f, BLD_FIELD, 0, 1.0f);
	s.spawn_building_world(10.0f, 18.0f, BLD_FARMSTEAD, 0, 1.0f);
	std::vector<uint32_t> w;
	for (int k = 0; k < 5; ++k) w.push_back(s.spawn_unit(14.0f, 8.0f + k, 0, UNIT_FEMALE_CITIZEN));
	run(s, 0.1f);
	s.order_build_or_repair(w.data(), w.size(), f);
	int before = s.record().total_food_gathered;
	run(s, seconds);
	return s.record().total_food_gathered - before;
}

int main() {
	// Table: every tech names a real researcher, phase and prerequisite, and
	// its effects stay inside the effects table.
	CHECK(TECH_COUNT > 80);
	for (int t = 1; t < TECH_COUNT; ++t) {
		const TechInfo &ti = tech_info((uint8_t)t);
		CHECK(ti.name && ti.name[0] && ti.template_name && ti.template_name[0]);
		CHECK(ti.researcher < BLD_TYPE_COUNT);
		CHECK(ti.required_phase >= 1 && ti.required_phase <= 3);
		CHECK(ti.prerequisite < TECH_COUNT && ti.prerequisite != t);
		CHECK(ti.civ_mask != 0);
		CHECK(ti.research_time > 0.0f);
		for (int e = 0; e < ti.effect_count; ++e)
			CHECK(tech_effects_table()[ti.effect_begin + e].stat < TSTAT_COUNT);
	}
	CHECK(tech_info(TECH_FARMING_1).researcher == BLD_FARMSTEAD);
	CHECK(tech_info(TECH_PHASE_TOWN).researcher == BLD_TOWN_CENTER);

	static Simulation s;

	// Research rules.
	uint32_t tc = fresh(s);
	CHECK(s.research_check(0, TECH_NONE) == Simulation::TECH_ERR_INVALID);
	CHECK(s.research_check(0, TECH_PHASE_TOWN, tc) == Simulation::TECH_ERR_STRUCTURES);
	CHECK(!s.cmd_research(0, tc, TECH_PHASE_TOWN));
	// Towers and fields are not Village structures; houses and barracks are.
	for (int k = 0; k < 3; ++k) s.spawn_building_world(-10.0f + 8.0f * k, 30.0f, BLD_FIELD, 0, 1.0f);
	CHECK(s.phase_structures(0, 1) == 0);
	for (int k = 0; k < 4; ++k) s.spawn_building_world(-20.0f + 6.0f * k, 20.0f, BLD_HOUSE, 0, 1.0f);
	CHECK(s.research_check(0, TECH_PHASE_TOWN, tc) == Simulation::TECH_ERR_STRUCTURES);
	uint32_t barracks = s.spawn_building_world(20.0f, -20.0f, BLD_BARRACKS, 0, 1.0f);
	CHECK(s.phase_structures(0, 1) == 5);
	CHECK(s.research_check(0, TECH_PHASE_TOWN, tc) == Simulation::TECH_OK);
	CHECK(s.research_check(0, TECH_PHASE_TOWN, barracks) == Simulation::TECH_ERR_BUILDING);
	CHECK(s.research_check(0, TECH_WEAPONS_1) == Simulation::TECH_ERR_PHASE);
	CHECK(s.research_check(0, TECH_ROMAN_ROADS) == Simulation::TECH_ERR_CIV);
	s.set_player_civ(2); // Britons may research Nisean horses only after the champion unlock
	CHECK(tech_available_to_civ(TECH_NISEAN_HORSES, 2));
	CHECK(s.research_check(0, TECH_NISEAN_HORSES) == Simulation::TECH_ERR_PREREQUISITE);
	s.set_player_civ(0);
	s.set_player_resources(0, 0, 0, 0, 0);
	CHECK(s.research_check(0, TECH_PHASE_TOWN, tc) == Simulation::TECH_ERR_COST);
	s.set_player_resources(0, 5000, 5000, 5000, 5000);

	// Town Phase: 500f/500w, less the Athenian Democracy team bonus on civic centre
	// techs (-30%), takes its research time, then sets phase 2.
	CHECK(s.cmd_research(0, tc, TECH_PHASE_TOWN));
	CHECK(s.food() == 4650.0f && s.wood() == 4650.0f);
	CHECK(s.research_check(0, TECH_PHASE_TOWN) == Simulation::TECH_ERR_IN_PROGRESS);
	run(s, tech_info(TECH_PHASE_TOWN).research_time + 1.0f);
	CHECK(s.is_tech_researched(TECH_PHASE_TOWN));
	CHECK(s.player_phase() == 2);
	CHECK(s.research_check(0, TECH_PHASE_TOWN) == Simulation::TECH_ERR_RESEARCHED);
	// City Phase needs three Town structures.
	CHECK(s.research_check(0, TECH_PHASE_CITY, tc) == Simulation::TECH_ERR_STRUCTURES);

	// The Loom (+100% civilian health) applies to existing and new units.
	uint32_t woman = s.spawn_unit(0.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
	run(s, 0.1f);
	float hp0 = unit_max_hp(s, woman);
	CHECK(hp0 > 0.0f);
	uint32_t house = 0;
	for (const auto &b : s.buildings()) if (b.type == BLD_HOUSE && b.team == 0) { house = b.id; break; }
	CHECK(s.cmd_research(0, house, TECH_HEALTH_CIVILIANS_01));
	run(s, tech_info(TECH_HEALTH_CIVILIANS_01).research_time + 1.0f);
	CHECK(s.is_tech_researched(TECH_HEALTH_CIVILIANS_01));
	CHECK(std::fabs(unit_max_hp(s, woman) - 2.0f * hp0) < 0.01f);
	uint32_t woman2 = s.spawn_unit(2.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
	CHECK(std::fabs(unit_max_hp(s, woman2) - 2.0f * hp0) < 0.01f);

	// Home Garden: houses +20% population bonus.
	int pop0 = s.pop_max();
	CHECK(s.cmd_research(0, house, TECH_POP_HOUSE_01));
	run(s, tech_info(TECH_POP_HOUSE_01).research_time + 1.0f);
	CHECK(s.pop_max() > pop0);
	// Saved here, before the scenarios below reset the sim.
	const char *path = "/tmp/rts0_test_techs.sav";
	CHECK(s.save_state(path));
	const int saved_pop_max = s.pop_max();

	// Sacrificial Ritual: healers train 40% faster (Cost/BuildTime).
	{
		fresh(s);
		s.player(0).phase = 2;
		uint32_t temple = s.spawn_building_world(20.0f, -20.0f, BLD_TEMPLE, 0, 1.0f);
		uint32_t tc2 = 0;
		for (const auto &b : s.buildings()) if (b.type == BLD_TOWN_CENTER) tc2 = b.id;
		CHECK(s.cmd_train(0, tc2, UNIT_HEALER));
		float dur0 = queued_duration(s, tc2);
		CHECK(dur0 > 0.0f);
		CHECK(s.cmd_cancel_train(0, tc2, 0));
		CHECK(s.cmd_research(0, temple, TECH_COST_HEALER));
		run(s, tech_info(TECH_COST_HEALER).research_time + 1.0f);
		CHECK(s.is_tech_researched(TECH_COST_HEALER));
		CHECK(s.cmd_train(0, tc2, UNIT_HEALER));
		CHECK(std::fabs(queued_duration(s, tc2) - dur0 * 0.6f) < 0.01f);
	}

	// Shipwrights: dock technologies research 20% faster.
	{
		fresh(s);
		s.player(0).phase = 2;
		uint32_t dock = s.spawn_building_world(30.0f, 30.0f, BLD_DOCK, 0, 1.0f);
		CHECK(s.cmd_research(0, dock, TECH_DOCK_EFFICIENCY));
		run(s, tech_info(TECH_DOCK_EFFICIENCY).research_time + 1.0f);
		CHECK(s.is_tech_researched(TECH_DOCK_EFFICIENCY));
		CHECK(s.cmd_research(0, dock, TECH_FISHING_BOAT_GATHER_RATE));
		float expect = tech_info(TECH_FISHING_BOAT_GATHER_RATE).research_time * 0.8f;
		for (const auto &b : s.buildings())
			if (b.id == dock) CHECK(std::fabs(b.research_duration - expect) < 0.01f);
	}

	// Handicraft: traders gain 15% more per trip.
	{
		fresh(s);
		s.player(0).phase = 2;
		uint32_t m1 = s.spawn_building_world(-20.0f, -20.0f, BLD_MARKET, 0, 1.0f);
		uint32_t m2 = s.spawn_building_world(30.0f, 30.0f, BLD_MARKET, 0, 1.0f);
		const Simulation::Building *b1 = find_bld(s, m1), *b2 = find_bld(s, m2);
		float gain0 = s.trader_gains(b1, b2);
		CHECK(gain0 > 0.0f);
		CHECK(s.cmd_research(0, m1, TECH_TRADE_GAIN_01));
		run(s, tech_info(TECH_TRADE_GAIN_01).research_time + 1.0f);
		CHECK(s.trader_gains(find_bld(s, m1), find_bld(s, m2)) > gain0);
	}

	// Healing Range and Healing Rate: a healer reaches further and heals faster.
	{
		fresh(s);
		s.player(0).phase = 3;
		uint32_t temple = s.spawn_building_world(20.0f, -20.0f, BLD_TEMPLE, 0, 1.0f);
		CHECK(std::fabs(s.healer_range(0) - 12.0f) < 0.01f);
		CHECK(std::fabs(s.healer_rate(0) - 2.5f) < 0.01f); // Heal.js: 5 hp every 2 s
		CHECK(s.cmd_research(0, temple, TECH_HEAL_RANGE));
		run(s, tech_info(TECH_HEAL_RANGE).research_time + 1.0f);
		CHECK(std::fabs(s.healer_range(0) - 17.0f) < 0.01f);
		CHECK(s.cmd_research(0, temple, TECH_HEAL_RATE));
		run(s, tech_info(TECH_HEAL_RATE).research_time + 1.0f);
		CHECK(std::fabs(s.healer_rate(0) - 3.125f) < 0.01f); // Interval x0.8
		CHECK(s.healer_range(1) == 12.0f); // another player's healers are unaffected
	}

	// A healer keeps a soldier under fire alive only once Healing Range reaches
	// him: 14 m is outside 0 A.D.'s base 12 m.
	{
		float hp_left[2] = {0.0f, 0.0f};
		for (int with = 0; with < 2; ++with) {
			fresh(s);
			s.player(0).phase = 2;
			uint32_t temple = s.spawn_building_world(20.0f, -20.0f, BLD_TEMPLE, 0, 1.0f);
			if (with) CHECK(s.cmd_research(0, temple, TECH_HEAL_RANGE));
			run(s, tech_info(TECH_HEAL_RANGE).research_time + 1.0f);
			CHECK(s.is_tech_researched(TECH_HEAL_RANGE) == (with != 0));
			s.spawn_building_world(0.0f, 0.0f, BLD_TOWER, 1, 1.0f);   // enemy tower
			uint32_t sp = s.spawn_unit(8.0f, 0.0f, 0, UNIT_SPEARMAN, 400.0f); // under its arrows
			uint32_t healer = s.spawn_unit(22.0f, 0.0f, 0, UNIT_HEALER);
			s.cmd_stop(&healer, 1);
			s.cmd_stop(&sp, 1);
			run(s, 10.0f);
			hp_left[with] = unit_hp(s, sp);
		}
		std::printf("spearman hp after 10 s under fire: %.0f healed, %.0f not\n", hp_left[1], hp_left[0]);
		CHECK(hp_left[0] >= 0.0f && hp_left[1] > hp_left[0]);
	}

	// Champion unlock: each roster row carries 0 A.D.'s own Identity/Requirements.
	// The Mauryan Warrior waits for Unlock Champion Infantry; Maiden Guards need
	// only the City phase, as in 0 A.D.
	{
		fresh(s);
		s.set_player_civ(CIV_MAURYANS);
		s.player(0).phase = 3;
		uint32_t bar = s.spawn_building_world(20.0f, -20.0f, BLD_BARRACKS, 0, 1.0f);
		uint16_t warrior = CIV_UNIT_TEMPLATE_NONE, maiden = CIV_UNIT_TEMPLATE_NONE;
		for (uint16_t i = civ_roster_begin(CIV_MAURYANS); i < civ_roster_end(CIV_MAURYANS); ++i) {
			if (std::strcmp(civ_unit_template(i).id, "maur/champion_infantry_maceman") == 0) warrior = i;
			if (std::strcmp(civ_unit_template(i).id, "maur/champion_maiden") == 0) maiden = i;
		}
		CHECK(warrior != CIV_UNIT_TEMPLATE_NONE && maiden != CIV_UNIT_TEMPLATE_NONE);
		CHECK(tech_available_to_civ(TECH_UNLOCK_CHAMPION_INFANTRY, CIV_MAURYANS));
		CHECK(s.train_unlock_tech(0, UNIT_SWORDSMAN, BLD_BARRACKS, warrior) == TECH_UNLOCK_CHAMPION_INFANTRY);
		CHECK(s.train_unlock_tech(0, UNIT_ARCHER_MAIDEN, BLD_TOWN_CENTER, maiden) == TECH_NONE);
		CHECK(s.train_unlock_tech(0, UNIT_ARCHER_MAIDEN, BLD_BARRACKS) == TECH_UNLOCK_CHAMPION_INFANTRY); // class fallback
		CHECK(!s.cmd_train_template(0, bar, warrior));
		CHECK(s.cmd_train(0, bar, UNIT_SWORDSMAN)); // citizen swordsman: no unlock
		CHECK(s.cmd_research(0, bar, TECH_UNLOCK_CHAMPION_INFANTRY));
		run(s, tech_info(TECH_UNLOCK_CHAMPION_INFANTRY).research_time + 1.0f);
		CHECK(s.can_train_at(0, UNIT_SWORDSMAN, BLD_BARRACKS, warrior));
		CHECK(s.cmd_train_template(0, bar, warrior));
	}

	// Fertility Festival: houses train citizens, and only citizens.
	{
		fresh(s);
		uint32_t h = s.spawn_building_world(-20.0f, 20.0f, BLD_HOUSE, 0, 1.0f);
		CHECK(!s.can_train_at(0, UNIT_VILLAGER, BLD_HOUSE));
		CHECK(!s.cmd_train(0, h, UNIT_VILLAGER));
		CHECK(s.cmd_research(0, h, TECH_UNLOCK_CIVILIANS_HOUSE_GENERIC));
		run(s, tech_info(TECH_UNLOCK_CIVILIANS_HOUSE_GENERIC).research_time + 1.0f);
		CHECK(s.can_train_at(0, UNIT_VILLAGER, BLD_HOUSE));
		CHECK(s.cmd_train(0, h, UNIT_VILLAGER));
		CHECK(!s.can_train_at(0, UNIT_SWORDSMAN, BLD_HOUSE));
	}

	// Sentries: a tower shoots half again as many arrows, so it hurts more.
	{
		float dmg[2] = {0.0f, 0.0f};
		for (int with = 0; with < 2; ++with) {
			fresh(s);
			s.player(0).phase = 2;
			// Inside the civic centre's territory: a tower alone in no-man's land makes a cut-off
			// patch of territory and decays to Gaia (TerritoryDecay).
			uint32_t tower = s.spawn_building_world(-20.0f, -30.0f, BLD_TOWER, 0, 1.0f);
			if (with) {
				CHECK(s.cmd_research(0, tower, TECH_TOWER_WATCH));
				run(s, tech_info(TECH_TOWER_WATCH).research_time + 1.0f);
				CHECK(s.is_tech_researched(TECH_TOWER_WATCH));
			}
			uint32_t foe = s.spawn_unit(-12.0f, -30.0f, 1, UNIT_SWORDSMAN, 4000.0f);
			s.cmd_stop(&foe, 1);
			float hp0 = unit_hp(s, foe);
			run(s, 12.0f);
			dmg[with] = hp0 - unit_hp(s, foe);
		}
		std::printf("tower damage in 12 s: %.0f with Sentries, %.0f without\n", dmg[1], dmg[0]);
		CHECK(dmg[0] > 0.0f && dmg[1] > dmg[0]);
	}

	// Civ structure bonuses reach the buildings added after id 15 now that
	// bld_bit() is 32 bits wide (Carthaginian walls, area D's PR #21).
	{
		fresh(s);
		s.set_player_civ(CIV_CARTHAGINIANS);
		s.player(0).phase = 2;
		float stone0 = (float)s.stone();
		CHECK(s.cmd_build(0, -16, -16, BLD_WALL_TOWER) != 0); // in its Civic Centre's territory
		float paid = stone0 - (float)s.stone();
		CHECK(std::fabs(paid - 2.0f * building_type_info(BLD_WALL_TOWER).stone_cost) < 1.01f);
	}

	// Save/load keeps researched techs and their effects.
	static Simulation l;
	fresh(l);
	CHECK(l.load_state(path));
	CHECK(l.is_tech_researched(TECH_PHASE_TOWN) && l.is_tech_researched(TECH_HEALTH_CIVILIANS_01));
	CHECK(l.player_phase() == 2);
	CHECK(l.pop_max() == saved_pop_max);
	uint32_t woman3 = l.spawn_unit(4.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
	CHECK(std::fabs(unit_max_hp(l, woman3) - 2.0f * hp0) < 0.01f);
	TurnManager tm;
	CHECK(tm.compute_sim_hash(l) != 0);

	// Iron Plow (+20% grain): farmers with it bring in more food.
	static Simulation a, b;
	fresh(a);
	fresh(b);
	uint32_t fs = a.spawn_building_world(-20.0f, 20.0f, BLD_FARMSTEAD, 0, 1.0f);
	b.spawn_building_world(-20.0f, 20.0f, BLD_FARMSTEAD, 0, 1.0f);
	CHECK(a.cmd_research(0, fs, TECH_FARMING_1));
	run(a, tech_info(TECH_FARMING_1).research_time + 1.0f);
	run(b, tech_info(TECH_FARMING_1).research_time + 1.0f);
	CHECK(a.is_tech_researched(TECH_FARMING_1) && !b.is_tech_researched(TECH_FARMING_1));
	CHECK(tm.compute_sim_hash(a) != tm.compute_sim_hash(b));
	int with = farm_for(a, 150.0f), without = farm_for(b, 150.0f);
	std::printf("food in 150 s: %d with Iron Plow, %d without\n", with, without);
	CHECK(without > 0 && with > without);

	// Lockstep: identical research on two sims keeps them in sync.
	static Simulation c, d;
	for (Simulation *p : {&c, &d}) {
		uint32_t t = fresh(*p);
		for (int k = 0; k < 5; ++k) p->spawn_building_world(-20.0f + 6.0f * k, 20.0f, BLD_HOUSE, 0, 1.0f);
		p->cmd_research(0, t, TECH_PHASE_TOWN);
	}
	run(c, 35.0f);
	run(d, 35.0f);
	CHECK(c.player_phase() == 2);
	CHECK(tm.compute_sim_hash(c) == tm.compute_sim_hash(d));

	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
