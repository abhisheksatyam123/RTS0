// The Germans' supply wagon, Wagon Encampment and Wagon Trains (0 A.D. 0.28):
// structures/germ/encampment(_fortified), units/germ/support_wagon(_house),
// data/technologies/wagon_trains.json, Population.js on units and Promotion.js.
//   g++ -std=c++20 -O2 -Isrc tools/test_wagon_trains.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp -lpthread -o /tmp/test_wagon_trains
//       && /tmp/test_wagon_trains
#define private public // players_ (phase), complete_research
#include "simulation/simulation.h"
#undef private
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static uint16_t roster_id(const char *id) {
	for (uint16_t i = 0; i < CIV_UNIT_TEMPLATE_COUNT; ++i)
		if (std::strcmp(civ_unit_template(i).id, id) == 0) return i;
	return CIV_UNIT_TEMPLATE_NONE;
}

static int32_t index_of(const Simulation &s, uint32_t uid) {
	auto v = s.units();
	for (size_t i = 0; i < v.count; ++i)
		if (v.id[i] == uid) return (int32_t)i;
	return -1;
}

static const Simulation::Building *find_bld(const Simulation &s, uint32_t id) {
	for (const auto &b : s.buildings()) if (b.id == id) return &b;
	return nullptr;
}

static void run(Simulation &s, float seconds) {
	for (int t = 0; t < (int)std::lround(seconds / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
}

int main() {
	const uint16_t wagon_t = roster_id("germ/support_wagon");
	const uint16_t house_t = roster_id("germ/support_wagon_house");

	// 1. Data. The encampment is the Germans' own Town-phase Colony: no territory, no arrows,
	// a dropsite for everything, 20 population and 20 garrison, with a City-phase Upgrade.
	{
		const BuildingTypeInfo &e = building_type_info(BLD_WAGON_ENCAMPMENT);
		CHECK(civ_can_build(CIV_GERMANS, BLD_WAGON_ENCAMPMENT) && !civ_can_build(CIV_GAULS, BLD_WAGON_ENCAMPMENT));
		CHECK(!civ_can_build(CIV_GERMANS, BLD_WAGON_ENCAMPMENT_FORTIFIED)); // reached by its Upgrade only
		CHECK(building_required_phase(BLD_WAGON_ENCAMPMENT) == 2);
		CHECK(e.max_hp == 1750.0f && e.wood_cost == 350 && e.stone_cost == 150 && e.pop_bonus == 20 && e.garrison_cap == 20);
		CHECK(building_type_info(BLD_WAGON_ENCAMPMENT_FORTIFIED).max_hp == 2625.0f); // Health x1.5
		CHECK(territory_influence_info(BLD_WAGON_ENCAMPMENT).weight == 0 && !is_territory_root(BLD_WAGON_ENCAMPMENT));
		CHECK(!is_civic_centre(BLD_WAGON_ENCAMPMENT) && civ_building_is_dropsite(BLD_WAGON_ENCAMPMENT));
		CHECK(!building_ai_info(BLD_WAGON_ENCAMPMENT).shoots());
		CHECK((building_tech_classes(BLD_WAGON_ENCAMPMENT) & TC_COLONY) && (building_tech_classes(BLD_MILITARY_COLONY) & TC_COLONY));
		CHECK(!(building_tech_classes(BLD_TOWN_CENTER) & TC_COLONY));
		CHECK(building_upgrade(BLD_WAGON_ENCAMPMENT, CIV_GERMANS).to == BLD_WAGON_ENCAMPMENT_FORTIFIED);
		CHECK(building_research_base(BLD_WAGON_ENCAMPMENT_FORTIFIED) == BLD_WAGON_ENCAMPMENT);

		// The Civic Center and both encampments train the wagon; the encampments also the
		// spearman, slinger and civilian, the fortified one the log ram.
		CHECK(wagon_t != CIV_UNIT_TEMPLATE_NONE && house_t != CIV_UNIT_TEMPLATE_NONE);
		const CivUnitTemplate &w = civ_unit_template(wagon_t);
		CHECK(w.unit_type == UNIT_WAGON && w.civ == CIV_GERMANS && w.food_cost == 25 && w.wood_cost == 50);
		CHECK(w.trainers == (bld_bit(BLD_TOWN_CENTER) | bld_bit(BLD_WAGON_ENCAMPMENT) | bld_bit(BLD_WAGON_ENCAMPMENT_FORTIFIED)));
		CHECK((w.tech_classes & TC_WAGON) && !(civ_unit_template(roster_id("germ/support_civilian")).tech_classes & TC_WAGON));
		CHECK(!civ_unit_template(house_t).trainers && civ_unit_template(house_t).unit_type == UNIT_WAGON);
		CHECK(civ_unit_template(roster_id("germ/infantry_spearman_b")).trainers.test(BLD_WAGON_ENCAMPMENT));
		CHECK(civ_unit_template(roster_id("germ/infantry_slinger_b")).trainers.test(BLD_WAGON_ENCAMPMENT));
		CHECK(civ_unit_template(roster_id("germ/support_civilian")).trainers.test(BLD_WAGON_ENCAMPMENT));
		CHECK(!civ_unit_template(roster_id("germ/infantry_swordsman_b")).trainers.test(BLD_WAGON_ENCAMPMENT));
		CHECK(civ_unit_template(roster_id("germ/siege_ram")).trainers.test(BLD_WAGON_ENCAMPMENT_FORTIFIED));
		CHECK(!civ_unit_template(roster_id("germ/siege_ram")).trainers.test(BLD_WAGON_ENCAMPMENT));
		CHECK(unit_pop_bonus(house_t) == 5 && unit_pop_bonus(wagon_t) == 0 && unit_pop_bonus(CIV_UNIT_TEMPLATE_NONE) == 0);

		const TechInfo &t = tech_info(TECH_WAGON_TRAINS);
		CHECK(std::strcmp(t.template_name, "wagon_trains") == 0 && t.researcher == BLD_WAGON_ENCAMPMENT);
		CHECK(t.wood_cost == 400 && t.gold_cost == 100 && t.food_cost == 0 && t.stone_cost == 0);
		CHECK(t.required_phase == 2 && t.research_time == 40.0f);
		CHECK(tech_available_to_civ(TECH_WAGON_TRAINS, CIV_GERMANS) && !tech_available_to_civ(TECH_WAGON_TRAINS, CIV_GAULS));
		CHECK(t.effect_count == 1 && tech_effects_table()[t.effect_begin].stat == TSTAT_POP_BONUS);
		CHECK(tech_effects_table()[t.effect_begin].op == TOP_ADD && tech_effects_table()[t.effect_begin].value == 10.0f);
		// The encampment researches the military colony's list (the Civic Center's without the phases)
		// plus Wagon Trains, and so does its fortified Upgrade.
		for (uint8_t enc_t : {(uint8_t)BLD_WAGON_ENCAMPMENT, (uint8_t)BLD_WAGON_ENCAMPMENT_FORTIFIED}) {
			CHECK(building_researches_tech(enc_t, TECH_WAGON_TRAINS, CIV_GERMANS));
			CHECK(building_researches_tech(enc_t, TECH_COLONIZATION, CIV_GERMANS));
			CHECK(!building_researches_tech(enc_t, TECH_PHASE_TOWN, CIV_GERMANS));
			CHECK(!building_researches_tech(enc_t, TECH_PHASE_CITY, CIV_GERMANS));
		}
		CHECK(!building_researches_tech(BLD_TOWN_CENTER, TECH_WAGON_TRAINS, CIV_GERMANS));
		const TechInfo &g = tech_info(TECH_GROVE_OF_FETTERS);
		CHECK(tech_effects_table()[g.effect_begin].stat == TSTAT_HEALTH && tech_effects_table()[g.effect_begin].value == 1.5f);
	}

	// 2. A Germanic Civic Center (20) and a Wagon Encampment (20); a wagon adds nothing yet.
	static Simulation s;
	s.set_sim_seed(3);
	s.set_team_civ(0, CIV_GERMANS);
	s.set_team_civ(1, CIV_GAULS);
	s.reset();
	for (uint8_t p = 0; p < 2; ++p) s.set_player_resources(p, 5000, 5000, 5000, 5000);
	s.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(60.0f, 60.0f, BLD_TOWN_CENTER, 1, 1.0f);
	const uint32_t enc = s.spawn_building_world(20.0f, 0.0f, BLD_WAGON_ENCAMPMENT, 0, 1.0f);
	const uint32_t wagon = s.spawn_unit(0.0f, 12.0f, 0, UNIT_WAGON, 0.0f, wagon_t);
	CHECK(s.unit_template(wagon) == wagon_t);
	run(s, 1.0f);
	CHECK(s.pop_max(0) == 40);
	CHECK(s.unit_template(wagon) == wagon_t); // 5000 xp to promote

	// Wagon Trains needs the Town Phase, an encampment and the Germans.
	CHECK(s.research_check(0, TECH_WAGON_TRAINS, enc) == Simulation::TECH_ERR_PHASE);
	s.players_[0].phase = 2;
	s.players_[1].phase = 2;
	CHECK(s.research_check(0, TECH_WAGON_TRAINS, enc) == Simulation::TECH_OK);
	CHECK(s.research_check(1, TECH_WAGON_TRAINS) == Simulation::TECH_ERR_CIV);

	// 3. Researching it: RequiredXp 0 for Wagons promotes every wagon to a wagon house at
	// once (Promotion.js OnValueModification), keeping its health fraction; each house adds
	// 5 population and Colonies 10 more.
	{
		const int32_t wi = index_of(s, wagon);
		s.units_.hp[wi] = s.units_.max_hp[wi] * 0.5f;
	}
	CHECK(s.cmd_research(0, enc, TECH_WAGON_TRAINS));
	CHECK(s.wood(0) == 4600.0f && s.gold(0) == 4900.0f);
	run(s, tech_info(TECH_WAGON_TRAINS).research_time + 1.0f);
	CHECK(s.players_[0].has_tech(TECH_WAGON_TRAINS));
	CHECK(s.unit_template(wagon) == house_t);
	{
		const int32_t wi = index_of(s, wagon);
		CHECK(wi >= 0 && s.units().unit_type[wi] == UNIT_WAGON);
		CHECK(std::fabs(s.units().hp[wi] - 0.5f * s.units().max_hp[wi]) < 0.01f);
	}
	CHECK(s.structure_pop_bonus(BLD_WAGON_ENCAMPMENT, 0) == 30 && s.structure_pop_bonus(BLD_TOWN_CENTER, 0) == 20);
	CHECK(s.pop_max(0) == 55);
	// A wagon trained after the research turns into a house too.
	const uint32_t wagon2 = s.spawn_unit(4.0f, 12.0f, 0, UNIT_WAGON, 0.0f, wagon_t);
	run(s, 0.5f);
	CHECK(s.unit_template(wagon2) == house_t);
	CHECK(s.pop_max(0) == 60);
	CHECK(unit_is_mobile_dropsite(UNIT_WAGON)); // a wagon house still takes every resource

	// 4. Saves: the units' population bonus is recounted on load.
	const char *path = "/tmp/rtso_test_wagon_trains.sav";
	CHECK(s.save_state(path));
	static Simulation r;
	CHECK(r.load_state(path));
	CHECK(r.pop_max(0) == 60 && r.unit_template(wagon2) == house_t);
	std::remove(path);

	// 5. The fortified encampment (City Phase, 200 wood 200 metal, 30 s) keeps the Colony bonus.
	s.players_[0].phase = 3;
	CHECK(s.cmd_upgrade_building(0, enc));
	run(s, 31.0f);
	const Simulation::Building *fb = find_bld(s, enc);
	CHECK(fb && fb->type == BLD_WAGON_ENCAMPMENT_FORTIFIED);
	CHECK(s.pop_max(0) == 60);
	CHECK(s.research_check(0, TECH_WAGON_TRAINS, enc) == Simulation::TECH_ERR_RESEARCHED);

	// 6. A wagon house that is deleted takes its 5 population with it.
	s.cmd_delete(0, &wagon, 1);
	run(s, 1.0f);
	CHECK(index_of(s, wagon) < 0 || !s.units().alive[index_of(s, wagon)]);
	CHECK(s.pop_max(0) == 55);

	if (failures == 0) std::printf("ALL PASS\n");
	return failures == 0 ? 0 : 1;
}
