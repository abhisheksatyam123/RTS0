// Civ roster wiring tests: spawned and trained units carry their civ's 0 A.D.
// template, heroes train with their auras, civ bonuses come from data, and the
// template survives a save/load round trip.
//   g++ -std=c++20 -O2 -Isrc tools/test_civ_units.cpp src/simulation/simulation.cpp
//       src/simulation/rms_generator.cpp src/core/job_system.cpp -lpthread -o /tmp/test_civ_units
//       && /tmp/test_civ_units
#include "simulation/simulation.h"
#include <cstdio>
#include <cstring>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static int32_t index_of(const Simulation &s, uint32_t uid) {
	auto v = s.units();
	for (size_t i = 0; i < v.count; ++i)
		if (v.id[i] == uid) return (int32_t)i;
	return -1;
}

static uint16_t find_template(uint8_t civ, const char *name) {
	for (uint16_t i = civ_roster_begin(civ); i < civ_roster_end(civ); ++i)
		if (std::strcmp(civ_unit_template(i).name, name) == 0) return i;
	return CIV_UNIT_TEMPLATE_NONE;
}

int main() {
	static Simulation s;
	s.set_sim_seed(5);
	s.set_team_civ(0, CIV_ATHENIANS);
	s.set_team_civ(1, CIV_HAN);
	s.reset();

	// Spawning a class gives the owner's civ unit with 0 A.D. stats.
	uint32_t hop = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN);
	int32_t hi = index_of(s, hop);
	CHECK(hi >= 0);
	uint16_t ht = s.unit_template(hop);
	CHECK(ht != CIV_UNIT_TEMPLATE_NONE);
	CHECK(std::strcmp(civ_unit_template(ht).name, "Athenian Hoplite") == 0);
	CHECK(s.units().max_hp[hi] == civ_unit_template(ht).hp);

	uint32_t han = s.spawn_unit(4.0f, 0.0f, 1, UNIT_SPEARMAN);
	CHECK(civ_unit_template(s.unit_template(han)).civ == CIV_HAN);

	// An explicit hp still overrides the template (start-of-match armies).
	uint32_t tough = s.spawn_unit(0.0f, 4.0f, 0, UNIT_SWORDSMAN, 160.0f);
	CHECK(s.units().max_hp[index_of(s, tough)] == 160.0f);

	// Civ unit bonuses are in the data: Spartan women have 0 A.D.'s +40% health.
	s.set_team_civ(2, CIV_SPARTANS);
	uint32_t sw = s.spawn_unit(8.0f, 0.0f, 2, UNIT_FEMALE_CITIZEN);
	uint32_t aw = s.spawn_unit(8.0f, 4.0f, 0, UNIT_FEMALE_CITIZEN);
	CHECK(s.units().max_hp[index_of(s, sw)] > s.units().max_hp[index_of(s, aw)] * 1.3f);

	// Britons' Woad Warriors: infantry +5% speed over the same template elsewhere.
	s.set_team_civ(3, CIV_BRITONS);
	s.set_team_civ(4, CIV_GAULS);
	uint32_t bsp = s.spawn_unit(12.0f, 0.0f, 3, UNIT_SPEARMAN);
	uint32_t gsp = s.spawn_unit(12.0f, 4.0f, 4, UNIT_SPEARMAN);
	CHECK(civ_unit_template(s.unit_template(bsp)).speed > civ_unit_template(s.unit_template(gsp)).speed * 1.04f);

	// Training a hero by template: Pericles, with his own aura. A fresh match, so
	// the extra players above don't end it early.
	static Simulation m;
	m.set_sim_seed(6);
	m.set_team_civ(0, CIV_ATHENIANS);
	m.reset();
	uint16_t peri = find_template(CIV_ATHENIANS, "Pericles");
	CHECK(peri != CIV_UNIT_TEMPLATE_NONE);
	m.set_player_resources(0, 5000, 5000, 5000, 5000);
	uint32_t tc = m.spawn_building(-20, -20, BLD_TOWN_CENTER, 0, 1.0f);
	for (int i = 0; i < 4; ++i) m.spawn_building(-30 + i * 3, -30, BLD_HOUSE, 0, 1.0f);
	// Heroes need the City phase, as 0 A.D.'s phase_city requirement.
	CHECK(civ_unit_template(peri).phase == 3);
	CHECK(!m.cmd_train_template(0, tc, peri));
	m.player(0).phase = 3;
	CHECK(m.cmd_train_template(0, tc, peri));
	CHECK(!m.cmd_train_template(0, tc, find_template(CIV_HAN, "Infantry Crossbowman"))); // other civ's unit
	const float food_before = m.player(0).food();
	CHECK(m.cmd_train(0, tc, UNIT_FEMALE_CITIZEN));
	CHECK(m.player(0).food() < food_before);
	// Pericles trains in 0 A.D.'s 50 s; look for him as soon as he is out.
	bool got_pericles = false;
	for (int i = 0; i < 20 * 70 && !got_pericles; ++i) {
		m.tick(Simulation::FIXED_STEP);
		auto v = m.units();
		for (size_t u = 0; u < v.count; ++u)
			if (v.team[u] == 0 && v.tmpl[u] == peri) got_pericles = true;
	}
	CHECK(got_pericles);

	// Classes a civ lacks are refused (Athenians field no elephants).
	CHECK(!m.cmd_train(0, tc, UNIT_ELEPHANT));

	// Building health takes 0 A.D.'s civ structure bonus (Hellenic Architecture +10%).
	uint32_t ath_house = s.spawn_building(10, 10, BLD_HOUSE, 0, 1.0f);
	s.set_team_civ(5, CIV_PERSIANS);
	uint32_t per_house = s.spawn_building(14, 10, BLD_HOUSE, 5, 1.0f);
	float ath_hp = 0, per_hp = 0;
	for (const auto &b : s.buildings()) {
		if (b.id == ath_house) ath_hp = b.max_hp;
		if (b.id == per_house) per_hp = b.max_hp;
	}
	CHECK(ath_hp > per_hp * 1.09f && ath_hp < per_hp * 1.11f);

	// Team bonuses (0 A.D. data/auras/teambonuses) reach the civ's player and its allies.
	{
		static Simulation t;
		t.set_sim_seed(7);
		t.set_team_civ(0, CIV_ATHENIANS);
		t.reset();
		uint16_t pericles = find_template(CIV_ATHENIANS, "Pericles");
		float cost[RES_COUNT];
		const UnitTypeInfo &pinfo = civ_template_unit_info(pericles, UNIT_HERO);
		t.unit_train_cost(0, pericles, pinfo, cost);
		CHECK(cost[RES_FOOD] + cost[RES_GOLD] > 0.0f); // no Spartan ally yet: full price

		// A Spartan ally still in the match makes heroes free (Peloponnesian League).
		t.set_team_civ(2, CIV_SPARTANS);
		t.set_player_team(2, t.player_team(0));
		CHECK(t.ally_civ_mask(0) == 0); // owns nothing yet, so not in the match
		t.spawn_building(30, -30, BLD_HOUSE, 2, 1.0f);
		CHECK(t.ally_civ_mask(0) == civ_bit(CIV_SPARTANS));
		t.unit_train_cost(0, pericles, pinfo, cost);
		CHECK(cost[RES_FOOD] == 0.0f && cost[RES_WOOD] == 0.0f && cost[RES_GOLD] == 0.0f && cost[RES_STONE] == 0.0f);
		t.set_player_resources(0, 5000, 5000, 5000, 5000);
		t.player(0).phase = 3;
		uint32_t tc = t.spawn_building(-20, -20, BLD_TOWN_CENTER, 0, 1.0f);
		for (int i = 0; i < 4; ++i) t.spawn_building(-30 + i * 3, -30, BLD_HOUSE, 0, 1.0f);
		const float gold_before = t.player(0).gold();
		CHECK(t.cmd_train_template(0, tc, pericles));
		CHECK(t.player(0).gold() == gold_before);
		CHECK(t.cmd_cancel_train(0, tc, 0)); // refund matches what was paid
		CHECK(t.player(0).gold() == gold_before);

		// Building and tech bonuses: Persian barracks, Athenian civic centre techs.
		CHECK(civ_team_bonus_mult(CIV_PERSIANS, 0, TEAM_BONUS_BUILDING_COST, RES_WOOD, CIV_UNIT_TEMPLATE_NONE, BLD_BARRACKS) == 0.8f);
		CHECK(civ_team_bonus_mult(CIV_PERSIANS, 0, TEAM_BONUS_BUILDING_COST, RES_WOOD, CIV_UNIT_TEMPLATE_NONE, BLD_HOUSE) == 1.0f);
		CHECK(t.team_bonus_mult(0, TEAM_BONUS_TECH_TIME, -1, CIV_UNIT_TEMPLATE_NONE, BLD_TOWN_CENTER) == 0.5f);
		// Han Silk Road reaches allies only, not the Han themselves.
		CHECK(civ_team_bonus_mult(CIV_HAN, 0, TEAM_BONUS_TRADE_INTERNATIONAL) == 1.0f);
		CHECK(civ_team_bonus_mult(CIV_ATHENIANS, civ_bit(CIV_HAN), TEAM_BONUS_TRADE_INTERNATIONAL) > 1.19f);
		// Two allies of the same civ don't stack one bonus.
		CHECK(civ_team_bonus_mult(CIV_SPARTANS, civ_bit(CIV_SPARTANS), TEAM_BONUS_UNIT_COST, RES_FOOD, pericles) == 0.0f);

		// Ptolemaic allies trickle +1 food/s.
		t.set_team_civ(3, CIV_PTOLEMIES);
		t.set_player_team(3, t.player_team(0));
		t.spawn_building(30, 30, BLD_HOUSE, 3, 1.0f);
		const float food_before = t.player(3).food();
		for (int i = 0; i < 20; ++i) t.tick(Simulation::FIXED_STEP);
		CHECK(t.player(3).food() >= food_before + 0.9f);
	}

	// Mercenaries take 0 A.D.'s mercenary mixin: metal only, 30% faster training.
	{
		uint16_t merc = find_template(CIV_CARTHAGINIANS, "Gallic Mercenary Swordsman");
		CHECK(merc != CIV_UNIT_TEMPLATE_NONE);
		const CivUnitTemplate &u = civ_unit_template(merc);
		CHECK((u.flags & ROSTER_MERCENARY) && u.food_cost == 0 && u.wood_cost == 0 && u.gold_cost > 0);
		CHECK(civ_team_bonus_mult(CIV_CARTHAGINIANS, 0, TEAM_BONUS_UNIT_TRAIN_TIME, -1, merc) == 0.5f);
	}

	// Civ-specific structures (0 A.D. structures/<civ>/): only their civ builds
	// them, at their 0 A.D. phase, and the civ's units train there.
	{
		static Simulation c;
		c.set_sim_seed(9);
		c.set_team_civ(0, CIV_SPARTANS);
		c.reset();
		c.set_player_resources(0, 5000, 5000, 5000, 5000);
		CHECK(civ_can_build(CIV_SPARTANS, BLD_SYSSITON) && !civ_can_build(CIV_ATHENIANS, BLD_SYSSITON));
		CHECK(civ_can_build(CIV_ATHENIANS, BLD_HOUSE) && civ_can_build(CIV_HAN, BLD_MINISTRY));
		CHECK(civ_can_build(CIV_PTOLEMIES, BLD_LIBRARY) && civ_can_build(CIV_SELEUCIDS, BLD_LIBRARY) &&
			civ_can_build(CIV_MACEDONIANS, BLD_LIBRARY) && !civ_can_build(CIV_ROMANS, BLD_LIBRARY));
		CHECK(c.cmd_build(0, 0, 0, BLD_GYMNASIUM) == 0); // Athenian only
		CHECK(c.cmd_build(0, 0, 0, BLD_GEROUSIA) == 0);  // Spartan, but Town phase
		CHECK(c.cmd_build(0, 0, 0, BLD_SYSSITON) != 0);  // Village phase in 0 A.D.
		const BuildingTypeInfo &sy = building_type_info(BLD_SYSSITON);
		CHECK(std::strcmp(sy.name, "Military Mess Hall") == 0 && sy.max_hp == 2000.0f && sy.stone_cost == 200);
		// Spartan champions and heroes train at the Syssiton, as in 0 A.D.
		int at_syssiton = 0;
		for (uint16_t i = civ_roster_begin(CIV_SPARTANS); i < civ_roster_end(CIV_SPARTANS); ++i)
			if (civ_unit_template(i).trainers & bld_bit(BLD_SYSSITON)) ++at_syssiton;
		CHECK(at_syssiton >= 3);
		CHECK(bld_bit(BLD_MINISTRY) == (1ull << 45));
		// 0 A.D. Loot of civ-specific structures (Looter.js), metal as gold.
		CHECK(building_loot(BLD_ARMY_CAMP).wood == 80 && building_loot(BLD_ARMY_CAMP).stone == 30);
		CHECK(building_loot(BLD_PYRAMID_LARGE).stone == 90 && building_loot(BLD_PYRAMID_LARGE).metal == 30);
		// A Military Colony is a territory root and a dropsite; the Lighthouse sees far.
		CHECK(civ_building_territory_radius(BLD_MILITARY_COLONY) > 10.0f && civ_building_is_dropsite(BLD_MILITARY_COLONY));
		CHECK(civ_building_territory_radius(BLD_LIBRARY) == 0.0f && !civ_building_is_dropsite(BLD_LIBRARY));
		CHECK(civ_building_vision(BLD_LIGHTHOUSE) > 40.0f);
		auto claimed = [&]() {
			int n = 0;
			for (uint8_t v : c.territory_grid().data) n += v != 0;
			return n;
		};
		for (int i = 0; i < 60; ++i) c.tick(Simulation::FIXED_STEP); // territory refreshes at 1 Hz
		const int before = claimed();
		c.set_team_civ(1, CIV_PTOLEMIES);
		c.spawn_building_world(40.0f, 40.0f, BLD_MILITARY_COLONY, 1, 1.0f);
		for (int i = 0; i < 60; ++i) c.tick(Simulation::FIXED_STEP);
		CHECK(claimed() > before);
	}

	// Templates survive save/load.
	const char *path = "/tmp/rtso_test_civ_units.sav";
	CHECK(s.save_state(path));
	uint16_t han_tmpl = s.unit_template(han);
	static Simulation r;
	CHECK(r.load_state(path));
	CHECK(r.unit_template(hop) == ht);
	CHECK(r.unit_template(han) == han_tmpl);
	std::remove(path);

	if (failures == 0) std::printf("ALL PASS\n");
	return failures == 0 ? 0 : 1;
}
