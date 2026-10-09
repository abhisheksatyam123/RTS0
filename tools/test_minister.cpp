// The Han Imperial Minister (units/han/support_minister): trained at the Imperial
// Ministry in the town phase for 100 food and 100 metal, a passive support unit that
// gathers nothing. Garrisoned ministers add 0.1 wood, stone and metal to the ministry's
// trickle each (han_minister_garrison_ministry.json) and cut the cost and time of
// technologies researched in any other structure by 2% each (han_minister_garrison.json).
// The Civil Service pair acts on them.
//   g++ -std=c++20 -O2 -Isrc tools/test_minister.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_minister && /tmp/test_minister
#define private public
#include "simulation/simulation.h"
#undef private
#include "network/turn_manager.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

static void run(Simulation &s, float seconds) {
	for (int t = 0; t < (int)std::lround(seconds / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
}

static void fresh(Simulation &s) {
	s.set_sim_seed(6);
	s.reset();
	s.players_[0].civ = CIV_HAN;
	s.players_[1].civ = CIV_ATHENIANS;
	for (uint8_t p = 0; p < 2; ++p) s.players_[p].active = 1, s.players_[p].is_ai = 0; // no Petra spending
	s.set_victory_condition(VICTORY_ENDLESS);
	s.spawn_building_world(-50.0f, -50.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(50.0f, 50.0f, BLD_TOWN_CENTER, 1, 1.0f);
	s.players_[0].phase = 2;
	for (int r = 0; r < RES_COUNT; ++r) s.players_[0].res[r] = 5000.0f;
}

static uint16_t minister_tmpl() {
	for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT; ++t)
		if (std::strcmp(civ_unit_template(t).id, "han/support_minister") == 0) return t;
	return CIV_UNIT_TEMPLATE_NONE;
}

static uint32_t spawn_minister(Simulation &s, float x, float z) {
	return s.spawn_unit(x, z, 0, UNIT_MINISTER, 0.0f, minister_tmpl());
}

int main() {
	// 1. The roster row: 0 A.D.'s cost, time, health, phase and trainer.
	const uint16_t mt = minister_tmpl();
	CHECK(mt != CIV_UNIT_TEMPLATE_NONE);
	if (mt == CIV_UNIT_TEMPLATE_NONE) return 1;
	{
		const CivUnitTemplate &ct = civ_unit_template(mt);
		CHECK(ct.unit_type == UNIT_MINISTER && ct.civ == CIV_HAN && ct.phase == 2);
		CHECK(ct.trainers == bld_bit(BLD_MINISTRY));
		CHECK(ct.food_cost == 100 && ct.gold_cost == 100 && ct.wood_cost == 0 && near(ct.train_time, 15.0f));
		CHECK(near(ct.hp, 200.0f));
		CHECK((ct.tech_classes & TC_MINISTER) && !(ct.tech_classes & TC_SOLDIER));
		CHECK(unit_default_stance(UNIT_MINISTER) == STANCE_PASSIVE);
		// Added after the promotion-only rows so no saved template id moved, and still
		// part of the Han roster.
		CHECK(mt >= CIV_ROSTER_PROMOTION_ONLY_FIRST);
		const auto &han = civ_roster_ids(CIV_HAN);
		CHECK(std::find(han.begin(), han.end(), mt) != han.end());
		CHECK(han.front() == civ_roster_begin(CIV_HAN));
	}

	static Simulation s;

	// 2. The ministry trains one; it won't gather.
	uint32_t ministry = 0;
	{
		fresh(s);
		ministry = s.spawn_building_world(-30.0f, -20.0f, BLD_MINISTRY, 0, 1.0f);
		const float f0 = s.players_[0].res[RES_FOOD], m0 = s.players_[0].res[RES_GOLD];
		CHECK(s.cmd_train_template(0, ministry, mt));
		CHECK(near(f0 - s.players_[0].res[RES_FOOD], 100.0f) && near(m0 - s.players_[0].res[RES_GOLD], 100.0f));
		const uint32_t tc = s.spawn_building_world(-10.0f, -40.0f, BLD_BARRACKS, 0, 1.0f);
		CHECK(!s.cmd_train_template(0, tc, mt)); // only the ministry trains it
		run(s, 16.0f);
		uint32_t trained = 0;
		for (size_t i = 0; i < s.units_.count; ++i)
			if (s.units_.alive[i] && s.units_.tmpl[i] == mt) trained = s.units_.id[i];
		CHECK(trained != 0);
		const int32_t ui = s.lookup_unit(trained);
		CHECK(ui >= 0 && s.units_.unit_type[ui] == UNIT_MINISTER);
		const uint32_t tree = s.spawn_resource(2, 2, RES_WOOD);
		s.order_gather(&trained, 1, tree);
		CHECK(ui >= 0 && s.units_.action_state[ui] != ACTION_GATHER);
		std::printf("ministry trained an Imperial Minister\n");
	}

	// 3. Two ministers garrisoned in the ministry: 0.3 wood, stone and metal every 2 s.
	{
		fresh(s);
		ministry = s.spawn_building_world(-30.0f, -20.0f, BLD_MINISTRY, 0, 1.0f);
		uint32_t two[2] = {spawn_minister(s, -30.0f, -15.0f), spawn_minister(s, -29.0f, -15.0f)};
		s.order_garrison(two, 2, ministry);
		run(s, 6.0f);
		const auto &b = s.buildings_[s.building_id_to_idx_.at(ministry)];
		CHECK(s.garrisoned_ministers(b) == 2);
		const float w0 = s.players_[0].res[RES_WOOD];
		run(s, 10.0f);
		CHECK(near(s.players_[0].res[RES_WOOD] - w0, 1.5f));
		std::printf("ministry with 2 ministers: +%.1f wood in 10 s\n", s.players_[0].res[RES_WOOD] - w0);
	}

	// 4. Ministers garrisoned in another structure: its technologies cost 2% less and take
	//    2% less time per minister; the ministry's own technologies don't change.
	{
		fresh(s);
		const uint32_t farm = s.spawn_building_world(-30.0f, -20.0f, BLD_FARMSTEAD, 0, 1.0f);
		uint32_t two[2] = {spawn_minister(s, -30.0f, -15.0f), spawn_minister(s, -29.0f, -15.0f)};
		s.order_garrison(two, 2, farm);
		run(s, 6.0f);
		CHECK(s.garrisoned_ministers(s.buildings_[s.building_id_to_idx_.at(farm)]) == 2);
		uint8_t tech = TECH_NONE; // a Han farmstead technology
		for (uint8_t t = 1; t < TECH_COUNT && tech == TECH_NONE; ++t)
			if (tech_info(t).researcher == BLD_FARMSTEAD && s.research_check(0, t, farm) == Simulation::TECH_OK) tech = t;
		CHECK(tech != TECH_NONE);
		const TechInfo &ti = tech_info(tech);
		const float before[RES_COUNT] = {s.players_[0].res[0], s.players_[0].res[1], s.players_[0].res[2], s.players_[0].res[3]};
		CHECK(s.cmd_research(0, farm, tech));
		const float k = 0.98f * 0.98f;
		CHECK(near(before[RES_FOOD] - s.players_[0].res[RES_FOOD], std::round(ti.food_cost * k)));
		CHECK(near(before[RES_WOOD] - s.players_[0].res[RES_WOOD], std::round(ti.wood_cost * k)));
		const auto &fb = s.buildings_[s.building_id_to_idx_.at(farm)];
		CHECK(std::fabs(fb.research_duration - ti.research_time * k * s.tech_mods(0).bld_research_time[BLD_FARMSTEAD]) < 1e-2f);
		std::printf("farmstead with 2 ministers: research x%.4f\n", k);
	}

	// 5. The Civil Service pair: Efficient Bureaucracy halves the minister's training time,
	//    Intensive Training gives +50% health.
	{
		fresh(s);
		ministry = s.spawn_building_world(-30.0f, -20.0f, BLD_MINISTRY, 0, 1.0f);
		s.complete_research(0, TECH_CIVIL_SERVICE_01);
		CHECK(near(s.tech_mods(0).tmpl[mt].train_time, 0.5f));
		fresh(s);
		s.complete_research(0, TECH_CIVIL_SERVICE_02);
		CHECK(near(s.tech_mods(0).tmpl[mt].health, 1.5f));
		const uint32_t m = spawn_minister(s, 0.0f, 0.0f);
		const int32_t mi = s.lookup_unit(m);
		std::printf("intensive training: minister max hp %.0f\n", s.units_.max_hp[mi]);
		CHECK(near(s.units_.max_hp[mi], 300.0f));
	}

	// 6. Save/load with ministers garrisoned keeps the hash.
	{
		fresh(s);
		ministry = s.spawn_building_world(-30.0f, -20.0f, BLD_MINISTRY, 0, 1.0f);
		uint32_t one = spawn_minister(s, -30.0f, -15.0f);
		s.order_garrison(&one, 1, ministry);
		run(s, 6.0f);
		const char *path = "/tmp/rts0_test_minister.sav";
		CHECK(s.save_state(path));
		static Simulation l;
		CHECK(l.load_state(path));
		run(s, 4.0f);
		run(l, 4.0f);
		CHECK(TurnManager().boot_sim_hash(s) == TurnManager().boot_sim_hash(l));
		CHECK(near(s.players_[0].res[RES_WOOD], l.players_[0].res[RES_WOOD]));
		std::remove(path);
	}

	if (failures) {
		std::printf("test_minister: %d failure(s)\n", failures);
		return 1;
	}
	std::printf("test_minister: all passed\n");
	return 0;
}
