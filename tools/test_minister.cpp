// The Han Imperial Minister (units/han/support_minister): trained at the Imperial
// Ministry in the town phase for 100 food and 100 metal, a passive support unit that
// gathers nothing. In 0.28 its only auras are range ones (han_minister_gathering and
// han_minister_building); garrisoned, it gives a structure nothing: the ministry has no
// ResourceTrickle and no technology gets cheaper. The Civil Service pair acts on it.
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
		CHECK(ct.unit_type == UNIT_MINISTER && ct.civ == CIV_HAN && ct.phase == 1); // 0.28: phase_village (the Ministry itself gates it)
		CHECK(ct.trainers == bld_bit(BLD_MINISTRY));
		CHECK(ct.food_cost == 100 && ct.gold_cost == 100 && ct.wood_cost == 0 && near(ct.train_time, 15.0f));
		CHECK(near(ct.hp, 50.0f)); // 0.28 support_minister
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

	// 3. Two ministers garrisoned in the ministry: no income (0.28's ministry has no ResourceTrickle).
	{
		fresh(s);
		ministry = s.spawn_building_world(-30.0f, -20.0f, BLD_MINISTRY, 0, 1.0f);
		uint32_t two[2] = {spawn_minister(s, -30.0f, -15.0f), spawn_minister(s, -29.0f, -15.0f)};
		s.order_garrison(two, 2, ministry);
		run(s, 6.0f);
		const auto &b = s.buildings_[s.building_id_to_idx_.at(ministry)];
		CHECK(b.garrisoned_units.size() == 2);
		const float w0 = s.players_[0].res[RES_WOOD];
		run(s, 10.0f);
		CHECK(near(s.players_[0].res[RES_WOOD] - w0, 0.0f));
		std::printf("ministry with 2 ministers: +%.1f wood in 10 s\n", s.players_[0].res[RES_WOOD] - w0);
	}

	// 4. Ministers garrisoned in another structure leave its technologies' cost and time
	//    alone (0.28 has no han_minister_garrison aura). A Han
	//    farmstead seats one (structures/han/farmstead.xml GarrisonHolder Max 1, List Minister),
	//    a Han barracks ten (List Infantry Minister).
	{
		fresh(s);
		const uint32_t farmstead = s.spawn_building_world(-30.0f, 0.0f, BLD_FARMSTEAD, 0, 1.0f);
		uint32_t pair[2] = {spawn_minister(s, -30.0f, 5.0f), spawn_minister(s, -29.0f, 5.0f)};
		s.order_garrison(pair, 2, farmstead);
		run(s, 6.0f);
		CHECK(s.buildings_[s.building_id_to_idx_.at(farmstead)].garrisoned_units.size() == 1);
		const uint32_t farm = s.spawn_building_world(-30.0f, -20.0f, BLD_BARRACKS, 0, 1.0f);
		uint32_t two[2] = {spawn_minister(s, -30.0f, -13.0f), spawn_minister(s, -29.0f, -13.0f)};
		s.order_garrison(two, 2, farm);
		run(s, 6.0f);
		CHECK(s.buildings_[s.building_id_to_idx_.at(farm)].garrisoned_units.size() == 2);
		uint8_t tech = TECH_NONE; // a Han barracks technology
		for (uint8_t t = 1; t < TECH_COUNT && tech == TECH_NONE; ++t)
			if (tech_info(t).researcher == BLD_BARRACKS && s.research_check(0, t, farm) == Simulation::TECH_OK) tech = t;
		CHECK(tech != TECH_NONE);
		const TechInfo &ti = tech_info(tech);
		const float before[RES_COUNT] = {s.players_[0].res[0], s.players_[0].res[1], s.players_[0].res[2], s.players_[0].res[3]};
		CHECK(s.cmd_research(0, farm, tech));
		const float k = 1.0f;
		CHECK(near(before[RES_FOOD] - s.players_[0].res[RES_FOOD], std::round(ti.food_cost * k)));
		CHECK(near(before[RES_WOOD] - s.players_[0].res[RES_WOOD], std::round(ti.wood_cost * k)));
		const auto &fb = s.buildings_[s.building_id_to_idx_.at(farm)];
		CHECK(!fb.queue.empty() && std::fabs(fb.queue.front().duration - ti.research_time * k * s.tech_mods(0).bld_research_time[BLD_BARRACKS]) < 1e-2f);
		std::printf("barracks with 2 ministers: research x%.4f\n", k);
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
		CHECK(near(s.units_.max_hp[mi], 75.0f));
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

	// 7. Builder/Rate range auras (builder_rate), 0.28 values: a minister within 20 m multiplies
	//    its owner's Workers' build rate by 1.1 (han_minister_building.json, not stackable);
	//    Xerxes' Administrator x1.25 (100 m, Workers) and Ptolemy I's Patron of Construction
	//    x1.1 (60 m, Builders) count once however many reach.
	{
		auto tmpl_of = [](const char *id) {
			for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT; ++t)
				if (std::strcmp(civ_unit_template(t).id, id) == 0) return t;
			return (uint16_t)CIV_UNIT_TEMPLATE_NONE;
		};
		auto rate = [&](uint32_t id) { return s.builder_rate((size_t)s.lookup_unit(id)); };
		const float r40 = 40.0f * COMBAT_DISTANCE_SCALE;
		fresh(s);
		const uint32_t worker = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN, 0.0f, tmpl_of("han/infantry_spearman_b"));
		const uint32_t enemy = s.spawn_unit(1.0f, 0.0f, 1, UNIT_SPEARMAN, 0.0f, tmpl_of("athen/infantry_spearman_b"));
		const uint32_t far_worker = s.spawn_unit(r40 + 3.0f, 0.0f, 0, UNIT_SPEARMAN, 0.0f, tmpl_of("han/infantry_spearman_b"));
		run(s, 0.5f);
		CHECK(near(rate(worker), 1.0f));
		spawn_minister(s, 0.0f, 2.0f);
		spawn_minister(s, 0.0f, -2.0f);
		run(s, 0.5f);
		std::printf("two ministers nearby: build rate x%.4f (want 1.1)\n", rate(worker));
		CHECK(near(rate(worker), 1.1f));
		CHECK(near(rate(enemy), 1.0f));
		CHECK(near(rate(far_worker), 1.0f)); // 20 m (5.6 units) reaches no further than the minister's edge
		// A worker builds a house faster by the same factor.
		fresh(s);
		uint32_t w = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN, 0.0f, tmpl_of("han/infantry_spearman_b"));
		spawn_minister(s, -2.0f, 2.0f);
		spawn_minister(s, -2.0f, -2.0f);
		const uint32_t house = s.spawn_building_world(4.0f, 0.0f, BLD_HOUSE, 0, 0.0f);
		s.order_build_or_repair(&w, 1, house);
		auto hb = [&]() -> const Simulation::Building & { return s.buildings_[s.building_id_to_idx_.at(house)]; };
		run(s, 2.0f);
		const float p0 = hb().build_progress;
		run(s, 4.0f);
		const float with = hb().build_progress - p0;
		const float bt = s.structure_build_time(hb());
		std::printf("house: %.4f of it in 4 s (want %.4f)\n", with, 4.0f * 1.1f / bt);
		CHECK(std::fabs(with - 4.0f * 1.1f / bt) < 0.01f);
		// The details panel's builders and time left (Foundation.js GetBuildTime): one builder at
		// x1.1, and with one more (totalBuilderRate + 1, at 2^0.7 / 2 each).
		{
			Simulation::BuildWork wk;
			CHECK(s.structure_build_work(hb(), wk) && !wk.repair && wk.builders == 1);
			const float left = (1.0f - hb().hp / hb().max_hp) * bt;
			CHECK(std::fabs(wk.remaining - left / 1.1f) < 0.05f);
			CHECK(std::fabs(wk.remaining_new - left / (2.1f * std::pow(2.0f, 0.7f) / 2.0f)) < 0.05f);
			std::printf("house: %d builder, %.2f s left, %.2f s with another\n", wk.builders, wk.remaining, wk.remaining_new);
		}

		// Xerxes once, even beside a second aura source of the same name.
		fresh(s);
		s.players_[0].civ = CIV_PERSIANS;
		const uint32_t pw = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN, 0.0f, tmpl_of("pers/infantry_spearman_b"));
		s.spawn_unit(0.0f, 3.0f, 0, UNIT_HERO, 0.0f, tmpl_of("pers/hero_xerxes_i"));
		s.spawn_unit(0.0f, -3.0f, 0, UNIT_HERO, 0.0f, tmpl_of("pers/hero_xerxes_i"));
		run(s, 0.5f);
		std::printf("Xerxes: build rate x%.3f (want 1.25)\n", rate(pw));
		CHECK(near(rate(pw), 1.25f));

		fresh(s);
		s.players_[0].civ = CIV_PTOLEMIES;
		uint16_t ptb = CIV_UNIT_TEMPLATE_NONE;
		int16_t patron = -1;
		for (int16_t a = 0; a < (int16_t)CIV_HERO_AURA_COUNT; ++a)
			if (std::strcmp(civ_hero_aura(a).id, "ptol_hero_ptolemy_i_1") == 0) patron = a;
		for (uint16_t t : civ_roster_ids(CIV_PTOLEMIES))
			if (ptb == CIV_UNIT_TEMPLATE_NONE && civ_aura_affects(patron, t) && civ_unit_template(t).unit_type == UNIT_SPEARMAN) ptb = t;
		CHECK(ptb != CIV_UNIT_TEMPLATE_NONE);
		const uint32_t tw = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN, 0.0f, ptb);
		s.spawn_unit(0.0f, 3.0f, 0, UNIT_HERO, 0.0f, tmpl_of("ptol/hero_ptolemy_i"));
		run(s, 0.5f);
		std::printf("Ptolemy I: build rate x%.3f (want 1.1)\n", rate(tw));
		CHECK(near(rate(tw), 1.1f));
	}

	// A Han corral seats a minister (structures/han/corral.xml), but its EjectClassesOnDestroy
	// is Animal only: below half health he dies inside (GarrisonHolder.EjectOrKill). A Han
	// farmstead (EjectClassesOnDestroy Unit) puts its minister outside.
	{
		fresh(s);
		const uint32_t corral = s.spawn_building_world(-30.0f, 0.0f, BLD_CORRAL, 0, 1.0f);
		const uint32_t farmstead = s.spawn_building_world(-30.0f, -20.0f, BLD_FARMSTEAD, 0, 1.0f);
		uint32_t m1 = spawn_minister(s, -30.0f, 5.0f), m2 = spawn_minister(s, -30.0f, -15.0f);
		s.order_garrison(&m1, 1, corral);
		s.order_garrison(&m2, 1, farmstead);
		run(s, 6.0f);
		auto &bc = s.buildings_[s.building_id_to_idx_.at(corral)];
		auto &bf = s.buildings_[s.building_id_to_idx_.at(farmstead)];
		CHECK(bc.garrisoned_units.size() == 1 && bf.garrisoned_units.size() == 1);
		bc.hp = bc.max_hp * 0.4f;
		bf.hp = bf.max_hp * 0.05f;
		run(s, 1.0f);
		const int32_t i1 = s.lookup_unit(m1), i2 = s.lookup_unit(m2);
		std::printf("corral minister alive %d, farmstead minister alive %d\n", i1 >= 0 && s.units_.alive[i1], i2 >= 0 && s.units_.alive[i2]);
		CHECK(i1 < 0 || !s.units_.alive[i1]);
		CHECK(i2 >= 0 && s.units_.alive[i2] && s.buildings_[s.building_id_to_idx_.at(farmstead)].garrisoned_units.empty());
	}

	if (failures) {
		std::printf("test_minister: %d failure(s)\n", failures);
		return 1;
	}
	std::printf("test_minister: all passed\n");
	return 0;
}
