// Civ roster wiring tests: spawned and trained units carry their civ's 0 A.D.
// template, heroes train with their auras, civ bonuses come from data, and the
// template survives a save/load round trip.
//   g++ -std=c++20 -O2 -Isrc tools/test_civ_units.cpp src/simulation/simulation.cpp
//       src/simulation/rms_generator.cpp src/core/job_system.cpp -lpthread -o /tmp/test_civ_units
//       && /tmp/test_civ_units
#include "simulation/simulation.h"
#include "simulation/entity_limits_data.h"
#include "simulation/promotion_data.h"
#include <cmath>
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

static uint16_t roster_id(const char *id) {
	for (uint16_t i = 0; i < CIV_UNIT_TEMPLATE_COUNT; ++i)
		if (std::strcmp(civ_unit_template(i).id, id) == 0) return i;
	return CIV_UNIT_TEMPLATE_NONE;
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

	// The Germans (civ 15) spawn their own units; Gaia's id is never a player's civ.
	s.set_team_civ(5, CIV_GERMANS);
	s.set_team_civ(5, CIV_GAIA);
	CHECK(s.team_civ(5) == CIV_GERMANS);
	uint32_t germ_sp = s.spawn_unit(16.0f, 0.0f, 5, UNIT_SPEARMAN);
	CHECK(std::strcmp(civ_unit_template(s.unit_template(germ_sp)).id, "germ/infantry_spearman_b") == 0);
	// Grove of Fetters: Priests and Seeresses +50% health (its effect row, not one past the table).
	CHECK(tech_effects_table()[tech_info(TECH_GROVE_OF_FETTERS).effect_begin].stat == TSTAT_HEALTH);
	{
		static Simulation g;
		g.set_sim_seed(7);
		g.set_team_civ(0, CIV_GERMANS);
		g.reset();
		g.set_player_resources(0, 5000, 5000, 5000, 5000);
		g.player(0).phase = 3;
		g.spawn_building(-20, -20, BLD_TOWN_CENTER, 0, 1.0f);
		const uint32_t temple = g.spawn_building(-10, -30, BLD_TEMPLE, 0, 1.0f);
		const uint32_t priest = g.spawn_unit(-12.0f, -20.0f, 0, UNIT_HEALER);
		const float hp0 = g.units().max_hp[index_of(g, priest)];
		CHECK(g.cmd_research(0, temple, TECH_GROVE_OF_FETTERS));
		for (int k = 0; k < (int)((tech_info(TECH_GROVE_OF_FETTERS).research_time + 1.0f) / Simulation::FIXED_STEP); ++k)
			g.tick(Simulation::FIXED_STEP);
		CHECK(g.player(0).has_tech(TECH_GROVE_OF_FETTERS));
		CHECK(g.units().max_hp[index_of(g, priest)] > hp0 * 1.49f);
	}

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
	// Heroes train only where 0 A.D. trains them: Athens' at the Prytaneion.
	CHECK(!m.cmd_train_template(0, tc, peri));
	CHECK(!m.cmd_train(0, tc, UNIT_HERO));
	const uint32_t pryt = m.spawn_building(-10, -30, BLD_PRYTANEION, 0, 1.0f);
	CHECK(m.cmd_train_template(0, pryt, peri));
	// EntityLimits Hero 1: no second hero while one is queued or alive.
	const uint16_t themi = find_template(CIV_ATHENIANS, "Themistocles");
	CHECK(!m.cmd_train_template(0, pryt, themi));
	CHECK(!m.cmd_train_template(0, tc, find_template(CIV_HAN, "Infantry Crossbowman"))); // other civ's unit
	// 0 A.D. has no RTS0 villager: its row is only a class default, never trained by name.
	CHECK(!m.cmd_train_template(0, tc, roster_id("athen/rts0_villager")));
	CHECK(m.cmd_train(0, tc, UNIT_VILLAGER));
	for (const auto &b : m.buildings())
		if (b.id == tc) CHECK(!b.queue.empty() && b.queue.back().tmpl == roster_id("athen/support_civilian"));
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
	CHECK(!m.cmd_train_template(0, pryt, themi));
	// MatchLimit 1 (template_unit_hero.xml): once Pericles dies he cannot be trained
	// again this match, while another hero can; a save keeps it.
	{
		auto v = m.units();
		uint32_t pid = 0;
		for (size_t u = 0; u < v.count; ++u)
			if (v.team[u] == 0 && v.tmpl[u] == peri) pid = v.id[u];
		m.cmd_delete(0, &pid, 1);
		for (int i = 0; i < 20 * 15; ++i) m.tick(Simulation::FIXED_STEP);
		bool alive = false;
		auto w = m.units();
		for (size_t u = 0; u < w.count; ++u)
			if (w.alive[u] && w.tmpl[u] == peri) alive = true;
		CHECK(!alive);
		CHECK(!m.cmd_train_template(0, pryt, peri));
		const char *mpath = "/tmp/rts0_match_limit.sav";
		CHECK(m.save_state(mpath));
		static Simulation r;
		CHECK(r.load_state(mpath));
		CHECK(!r.cmd_train_template(0, pryt, peri));
		CHECK(r.cmd_train_template(0, pryt, themi));
		CHECK(m.cmd_train_template(0, pryt, themi));
	}

	// Classes a civ lacks are refused (Athenians field no elephants).
	CHECK(!m.cmd_train(0, tc, UNIT_ELEPHANT));
	// A class request trains only what the building's Trainer list names: no cavalry
	// or healers at the barracks, no swordsmen at the Civic Centre (0 A.D. Trainer.js).
	{
		const uint32_t bar = m.spawn_building(-20, -6, BLD_BARRACKS, 0, 1.0f);
		CHECK(!m.cmd_train(0, bar, UNIT_CAVALRY));
		CHECK(!m.cmd_train(0, bar, UNIT_HEALER));
		CHECK(m.cmd_train(0, bar, UNIT_SPEARMAN));
		CHECK(!m.cmd_train(0, tc, UNIT_SWORDSMAN));
	}

	// Another civ's unique structure trains its Trainer list's "units/{civ}/..." for the captor
	// (Trainer.js): an Athenian Roman Army Camp trains Athenian rams, an Athenian Crannog
	// Athenian infantry and fishing boats.
	{
		static Simulation k;
		k.set_sim_seed(11);
		k.set_team_civ(0, CIV_ATHENIANS);
		k.set_team_civ(1, CIV_ROMANS);
		k.reset();
		k.set_player_resources(0, 5000, 5000, 5000, 5000);
		k.player(0).phase = 3;
		const uint32_t camp = k.spawn_building(-20, -20, BLD_ARMY_CAMP, 0, 1.0f);
		CHECK(k.cmd_train(0, camp, UNIT_SIEGE_RAM));
		const Simulation::Building *b = nullptr;
		for (const Simulation::Building &x : k.buildings()) if (x.id == camp) b = &x;
		CHECK(b && !b->queue.empty() && b->queue.back().tmpl == roster_id("athen/siege_ram"));
		CHECK(!k.cmd_train(0, camp, UNIT_CAVALRY));
		const uint32_t crannog = k.spawn_building(20, -20, BLD_CRANNOG, 0, 1.0f);
		CHECK(k.cmd_train(0, crannog, UNIT_SPEARMAN));
		CHECK(civ_unit_template(roster_id("athen/ship_fishing")).trainers.test(BLD_CRANNOG));
		// A kind the captor builds itself keeps its own list: the Roman Army Camp adds nothing to Roman rows.
		CHECK(civ_unit_template(roster_id("rome/infantry_swordsman_a")).trainers == bld_bit(BLD_ARMY_CAMP));
	}

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
		t.spawn_building(-20, -20, BLD_TOWN_CENTER, 0, 1.0f);
		const uint32_t pryt = t.spawn_building(-10, -30, BLD_PRYTANEION, 0, 1.0f);
		for (int i = 0; i < 4; ++i) t.spawn_building(-30 + i * 3, -30, BLD_HOUSE, 0, 1.0f);
		const float gold_before = t.player(0).gold();
		CHECK(t.cmd_train_template(0, pryt, pericles));
		CHECK(t.player(0).gold() == gold_before);
		CHECK(t.cmd_cancel_train(0, pryt, 0)); // refund matches what was paid
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
		// German Stolen Arms adds -5 metal (ModifiersManager "add", after the multipliers),
		// for Germans and their allies; a multiplier of -5 would make them free.
		uint16_t teut = CIV_UNIT_TEMPLATE_NONE;
		for (uint16_t i = 0; i < CIV_UNIT_TEMPLATE_COUNT; ++i)
			if (std::strcmp(civ_unit_template(i).id, "germ/infantry_swordsman_b") == 0) teut = i;
		CHECK(teut != CIV_UNIT_TEMPLATE_NONE);
		CHECK(civ_team_bonus_cost_add(CIV_GERMANS, 0, RES_GOLD, teut) == -5.0f);
		CHECK(civ_team_bonus_mult(CIV_GERMANS, 0, TEAM_BONUS_UNIT_COST, RES_GOLD, teut) == 1.0f);
		{
			Simulation g;
			g.set_team_civ(0, CIV_GERMANS);
			float cost[RES_COUNT];
			const CivUnitTemplate &tt = civ_unit_template(teut);
			g.unit_train_cost(0, teut, civ_template_unit_info(teut, tt.unit_type), cost);
			CHECK(tt.gold_cost > 5 && cost[RES_GOLD] == (float)tt.gold_cost - 5.0f);
		}

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
		// 0.28's Builder lists name only ptol/library; the mace and sele libraries, the ptol
		// mercenary camp, rome's amphitheater and temple of Mars and the germ totem are map-only.
		CHECK(civ_can_build(CIV_PTOLEMIES, BLD_LIBRARY) && !civ_can_build(CIV_SELEUCIDS, BLD_LIBRARY) &&
			!civ_can_build(CIV_MACEDONIANS, BLD_LIBRARY) && !civ_can_build(CIV_ROMANS, BLD_LIBRARY));
		CHECK(!civ_can_build(CIV_PTOLEMIES, BLD_MERCENARY_CAMP) && !civ_can_build(CIV_ROMANS, BLD_AMPHITHEATER) &&
			!civ_can_build(CIV_ROMANS, BLD_TEMPLE_MARS) && !civ_can_build(CIV_GERMANS, BLD_MONUMENT) &&
			civ_can_build(CIV_IBERIANS, BLD_MONUMENT) && civ_can_build(CIV_ROMANS, BLD_SIEGE_WALL));
		// A cell in player 0's territory where a Syssiton-sized structure fits.
		int px = 0, py = 0;
		bool found = false;
		c.spawn_building_world(-30.0f, -30.0f, BLD_TOWN_CENTER, 0, 1.0f);
		for (int i = 0; i < 60; ++i) c.tick(Simulation::FIXED_STEP); // territory refreshes at 1 Hz
		for (int r = 2; r < 20 && !found; ++r)
			for (int dy = -r; dy <= r && !found; ++dy)
				for (int dx = -r; dx <= r && !found; ++dx) {
					const int x = (int)std::floor(-30.0f / Simulation::GRID) + dx, y = (int)std::floor(-30.0f / Simulation::GRID) + dy;
					if (c.check_placement(0, x, y, BLD_SYSSITON) == Simulation::PLACE_OK) { px = x; py = y; found = true; }
				}
		CHECK(found);
		// 0 A.D.'s Military Colony keeps 120 m from any CivilCentre.
		CHECK(c.check_placement(0, px, py, BLD_MILITARY_COLONY) != Simulation::PLACE_OK);
		CHECK(c.cmd_build(0, px, py, BLD_GYMNASIUM) == 0); // Athenian only
		CHECK(c.cmd_build(0, px, py, BLD_GEROUSIA) == 0);  // Spartan, but Town phase
		CHECK(c.cmd_build(0, px, py, BLD_SYSSITON) != 0);  // Village phase in 0 A.D.
		const BuildingTypeInfo &sy = building_type_info(BLD_SYSSITON);
		CHECK(std::strcmp(sy.name, "Military Mess Hall") == 0 && sy.max_hp == 2000.0f && sy.stone_cost == 150 && sy.gold_cost == 150); // 0.28
		// 0.28: Spartan champions train at the Syssiton, their heroes at the Gerousia.
		CHECK(civ_unit_template(roster_id("spart/champion_infantry_spear")).trainers == bld_bit(BLD_SYSSITON));
		for (const char *hero : {"spart/hero_leonidas", "spart/hero_brasidas", "spart/hero_agis"})
			CHECK(civ_unit_template(roster_id(hero)).trainers == bld_bit(BLD_GEROUSIA));
		// 0.28's other trainer and phase changes: Marian Legionaries come only by promotion, the
		// Roman Army Camp trains Antesignani and the onager, Briton boats leave the Civic Center,
		// the Han minister is a Village phase unit and the bow Immortal trains at the barracks.
		CHECK(!civ_unit_template(roster_id("rome/infantry_legionary")).trainers);
		CHECK(civ_unit_template(roster_id("rome/infantry_antesignanus")).trainers == bld_bit(BLD_ARMY_CAMP));
		CHECK(civ_unit_template(roster_id("rome/siege_onager_packed")).trainers == (bld_bit(BLD_ARSENAL) | bld_bit(BLD_ARMY_CAMP)));
		CHECK(!(civ_unit_template(roster_id("brit/ship_fishing")).trainers & bld_bit(BLD_TOWN_CENTER)));
		CHECK(civ_unit_template(roster_id("han/support_minister")).phase == 1);
		CHECK(civ_unit_template(roster_id("pers/champion_infantry_archer_upgrade")).trainers == bld_bit(BLD_BARRACKS));
		CHECK(bld_bit(BLD_MINISTRY).test(45));
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

	// 0 A.D. Builder lists decide which generic structures a civ lays, and units
	// train at their own 0 A.D. buildings: cavalry at the stable, siege at the
	// arsenal, Carthaginian mercenaries at the embassy.
	{
		CHECK(civ_can_build(CIV_MAURYANS, BLD_ELEPHANT_STABLE) && !civ_can_build(CIV_ATHENIANS, BLD_ELEPHANT_STABLE));
		CHECK(civ_can_build(CIV_CARTHAGINIANS, BLD_EMBASSY) && !civ_can_build(CIV_HAN, BLD_EMBASSY));
		CHECK(civ_can_build(CIV_ATHENIANS, BLD_THEATER) && !civ_can_build(CIV_HAN, BLD_THEATER));
		CHECK(!civ_can_build(CIV_SPARTANS, BLD_WALL_SEGMENT) && civ_can_build(CIV_ATHENIANS, BLD_WALL_SEGMENT));
		CHECK(civ_can_build(CIV_HAN, BLD_STABLE) && civ_can_build(CIV_HAN, BLD_HOUSE));
		const uint16_t cav = roster_id("athen/cavalry_swordsman_b");
		const uint16_t ram = roster_id("athen/siege_ram");
		const uint16_t merc = roster_id("cart/infantry_swordsman_gaul_b");
		CHECK(cav != CIV_UNIT_TEMPLATE_NONE && ram != CIV_UNIT_TEMPLATE_NONE && merc != CIV_UNIT_TEMPLATE_NONE);
		CHECK(civ_unit_template(cav).trainers.test(BLD_STABLE) && !civ_unit_template(cav).trainers.test(BLD_BARRACKS));
		CHECK(civ_unit_template(ram).trainers.test(BLD_ARSENAL) && !civ_unit_template(ram).trainers.test(BLD_BARRACKS));
		CHECK(civ_unit_template(merc).trainers == bld_bit(BLD_EMBASSY));
		// The Briton crannog (structures/brit/crannog.xml): a Town phase Civic Center that trains
		// what the Civic Center trains and ships, and shoots like one.
		CHECK(civ_can_build(CIV_BRITONS, BLD_CRANNOG) && !civ_can_build(CIV_GAULS, BLD_CRANNOG));
		CHECK(is_civic_centre(BLD_CRANNOG) && is_territory_root(BLD_CRANNOG) && building_required_phase(BLD_CRANNOG) == 2);
		CHECK(civ_unit_template(roster_id("brit/ship_fishing")).trainers.test(BLD_CRANNOG));
		CHECK(civ_unit_template(roster_id("brit/infantry_spearman_b")).trainers.test(BLD_CRANNOG));
		CHECK(civ_unit_template(roster_id("brit/support_civilian")).trainers.test(BLD_CRANNOG));
		CHECK(civ_unit_template(roster_id("brit/rts0_villager")).trainers.test(BLD_CRANNOG));
		CHECK(!civ_unit_template(roster_id("gaul/rts0_villager")).trainers.test(BLD_CRANNOG));
		CHECK(building_ai_info(BLD_CRANNOG).shoots() && civ_building_is_dropsite(BLD_CRANNOG));
		CHECK(territory_influence_info(BLD_CRANNOG).root && territory_influence_info(BLD_CRANNOG).weight == 10000);
		// 0.28's Civilians (template_unit_support_civilian) replace the Female Citizens: the Civic
		// Center trains support_civilian, houses support_civilian_house after Fertility Festival, and
		// the women's rows stay for saves but train nowhere.
		for (uint8_t c = 0; c < CIV_COUNT; ++c) {
			if (!is_playable_civ(c)) continue;
			const uint16_t civ_t = civ_default_template(c, UNIT_FEMALE_CITIZEN);
			CHECK(civ_t != CIV_UNIT_TEMPLATE_NONE && std::strstr(civ_unit_template(civ_t).id, "/support_civilian"));
		}
		CHECK(civ_unit_template(roster_id("athen/support_civilian")).trainers == bld_bit(BLD_TOWN_CENTER));
		CHECK(civ_unit_template(roster_id("athen/support_civilian_house")).trainers.test(BLD_HOUSE));
		CHECK(!civ_unit_template(roster_id("athen/support_civilian_house")).trainers.test(BLD_TOWN_CENTER));
		CHECK(civ_unit_template(roster_id("athen/support_civilian_house")).unlock_tech == TECH_UNLOCK_CIVILIANS_HOUSE_GENERIC);
		CHECK(!civ_unit_template(roster_id("athen/support_female_citizen")).trainers);
		CHECK(!civ_unit_template(roster_id("athen/support_female_citizen_house")).trainers);
		CHECK(std::strcmp(civ_unit_template(roster_id("athen/support_civilian")).name, "Athenian Artisan") == 0);
		// 0.28's Germans (civ 15, after Gaia's 14): rows appended after the Civilians, the Great
		// Hall trains the noble cavalry, the Fortress the three heroes; they draw their own models.
		CHECK(civ_unit_template(roster_id("germ/champion_cavalry")).trainers == bld_bit(BLD_GREAT_HALL));
		CHECK(civ_can_build(CIV_GERMANS, BLD_GREAT_HALL) && !civ_can_build(CIV_GAULS, BLD_GREAT_HALL));
		CHECK(civ_can_train(CIV_GERMANS, UNIT_HERO) && civ_unit_template(roster_id("germ/hero_teutobod")).civ == CIV_GERMANS);
		CHECK(std::strstr(civ_unit_template(civ_default_template(CIV_GERMANS, UNIT_FEMALE_CITIZEN)).id, "germ/support_civilian"));
		CHECK(civ_unit_template(roster_id("germ/support_civilian")).tech_classes & TC_CIVILIAN); // Bovine Tradition
		CHECK(std::strcmp(civ_unit_model(CIV_GERMANS, UNIT_SPEARMAN), "germ_spearman") == 0);
		CHECK(tech_available_to_civ(TECH_GROVE_OF_FETTERS, CIV_GERMANS) && !tech_available_to_civ(TECH_GROVE_OF_FETTERS, CIV_GAULS));
		CHECK(!tech_available_to_civ(TECH_TOWER_HEALTH, CIV_GERMANS) && tech_available_to_civ(TECH_TOWER_HEALTH, CIV_GAULS));
		CHECK((civ_unit_template(roster_id("athen/support_civilian")).flags & ROSTER_CIVILIAN) &&
		      !(civ_unit_template(roster_id("athen/support_female_citizen")).flags & ROSTER_CIVILIAN));
		// Civilians are female or male (Identity/Phenotype "female male"), one per unit id, about half
		// each; the women's rows and soldiers keep their one phenotype. A male Macedonian Civilian
		// draws 0.28's Athenian citizen_male (the athen_villager model).
		{
			const uint16_t civ_t = roster_id("athen/support_civilian");
			int male = 0;
			for (uint32_t id = 1; id <= 1000; ++id) male += civilian_is_male(civ_t, id) ? 1 : 0;
			CHECK(male > 400 && male < 600);
			CHECK(civilian_is_male(civ_t, 7) == civilian_is_male(civ_t, 7));
			bool other = false;
			for (uint32_t id = 1; id <= 64; ++id)
				other = other || civilian_is_male(roster_id("athen/support_female_citizen"), id) ||
					civilian_is_male(roster_id("athen/infantry_spearman_b"), id);
			CHECK(!other);
			CHECK(std::strcmp(civ_unit_model(CIV_MACEDONIANS, UNIT_VILLAGER), "athen_villager") == 0);
			CHECK(std::strcmp(civ_unit_model(CIV_ATHENIANS, UNIT_VILLAGER), "athen_villager") == 0);
		}
		// Carthage's three embassies each train their own mercenaries (structures/cart/embassy_*.xml).
		CHECK(civ_can_build(CIV_CARTHAGINIANS, BLD_EMBASSY_IBERIAN) && civ_can_build(CIV_CARTHAGINIANS, BLD_EMBASSY_ITALIC));
		CHECK(!civ_can_build(CIV_ROMANS, BLD_EMBASSY_ITALIC));
		CHECK(civ_unit_template(roster_id("cart/infantry_slinger_iber_b")).trainers == bld_bit(BLD_EMBASSY_IBERIAN));
		CHECK(civ_unit_template(roster_id("cart/infantry_spearman_ital_b")).trainers == bld_bit(BLD_EMBASSY_ITALIC));
		CHECK(civ_unit_template(roster_id("cart/cavalry_spearman_ital_b")).trainers == bld_bit(BLD_EMBASSY_ITALIC));
		// 0.28 dropped the Iberian heavy cavalry and the Samnite swordsman: their rows stay for saves.
		CHECK(!civ_unit_template(roster_id("cart/cavalry_swordsman_iber_b")).trainers);
		CHECK(!civ_unit_template(roster_id("cart/infantry_swordsman_ital_b")).trainers);
		// Marian Reforms (0.28): the Forum trains Levy Auxiliary Spearmen once researched, and elite
		// spearmen and cavalry become conscripts and Auxiliaries (Promotion/Entity).
		{
			const CivUnitTemplate &levy = civ_unit_template(roster_id("rome/infantry_spearman_conscript"));
			CHECK(levy.trainers == bld_bit(BLD_TOWN_CENTER) && levy.unlock_tech == TECH_ROMAN_REFORMS);
			auto promotes_to = [](const char *from, const char *to) {
				for (const PromotionStep &s : PROMOTION_STEPS)
					if (std::strcmp(s.id, from) == 0 && s.target && std::strcmp(s.target, to) == 0) return true;
				return false;
			};
			CHECK(promotes_to("rome/infantry_spearman_b", "rome/infantry_spearman_conscript"));
			CHECK(promotes_to("rome/cavalry_spearman_b", "rome/cavalry_spearman_auxiliary_b"));
			CHECK(promotes_to("rome/cavalry_javelineer_b", "rome/cavalry_javelineer_auxiliary_b"));
			CHECK(promotes_to("rome/champion_infantry_swordsman", "rome/champion_infantry_swordsman_first"));
			// The map-only Marian legionaries: _02 -> _03 at 150 xp, _03 -> the Centurion at 300.
			CHECK(promotes_to("rome/champion_infantry_swordsman_02", "rome/champion_infantry_swordsman_03"));
			CHECK(promotes_to("rome/champion_infantry_swordsman_03", "rome/champion_infantry_swordsman_04"));
			CHECK(!promotes_to("rome/champion_infantry_swordsman_04", "rome/champion_infantry_swordsman_05"));
			CHECK(civ_unit_template(roster_id("rome/champion_infantry_swordsman_04")).hp == 140.0f);
			CHECK(civ_unit_template(roster_id("rome/champion_infantry_swordsman_05")).hp == 180.0f);
		}
		CHECK(civ_unit_template(roster_id("cart/cavalry_swordsman_gaul_b")).trainers == bld_bit(BLD_EMBASSY));
		CHECK(std::strcmp(building_type_info(BLD_EMBASSY).name, "Celtic Embassy") == 0 &&
			building_type_info(BLD_EMBASSY).wood_cost == 200 && building_type_info(BLD_EMBASSY).max_hp == 1200.0f);
		CHECK(building_type_info(BLD_EMBASSY_IBERIAN).stone_cost == 100 && building_type_info(BLD_EMBASSY_IBERIAN).wood_cost == 100); // 0.28
		CHECK(building_type_info(BLD_EMBASSY_ITALIC).stone_cost == 100 && building_type_info(BLD_EMBASSY_ITALIC).max_hp == 1500.0f); // 0.28
		CHECK(building_required_phase(BLD_EMBASSY_IBERIAN) == 2 && building_phase_class(BLD_EMBASSY_ITALIC) == 2);
		{
			// 0.28 structures/<civ>/*.xml where a civ's template differs from the generic one, as
			// the build button prices them (structure_cost_for, structure_build_time_for).
			static Simulation c;
			c.set_sim_seed(9);
			c.set_team_civ(0, CIV_BRITONS);
			c.set_team_civ(1, CIV_IBERIANS);
			c.reset();
			float p0[RES_COUNT], p1[RES_COUNT];
			c.structure_cost_for(0, BLD_BARRACKS, p0);
			c.structure_cost_for(1, BLD_BARRACKS, p1);
			CHECK(p0[RES_WOOD] == 300.0f && p0[RES_STONE] == 0.0f && p1[RES_WOOD] == 200.0f && p1[RES_STONE] == 100.0f);
			c.structure_cost_for(1, BLD_TOWER, p1);
			CHECK(p1[RES_WOOD] == 50.0f && p1[RES_STONE] == 250.0f && c.structure_max_hp(BLD_TOWER, 1) == 2400.0f);
			CHECK(std::fabs(c.structure_build_time_for(1, BLD_TOWER) - 50.0f) < 0.01f);
			CHECK(civ_building_type_info(CIV_ATHENIANS, BLD_HOUSE).pop_bonus == 10 && civ_building_type_info(CIV_ATHENIANS, BLD_HOUSE).max_hp == 1200.0f);
			CHECK(civ_building_type_info(CIV_GAULS, BLD_HOUSE).pop_bonus == 5 && civ_building_type_info(CIV_MAURYANS, BLD_WALL_TOWER).wood_cost == 60);
		}
		CHECK(building_type_info(BLD_EMBASSY_ITALIC).garrison_cap == 6 &&
			garrison_list_matches(structure_garrison_list(BLD_EMBASSY_ITALIC, CIV_CARTHAGINIANS), TC_CAVALRY));
		static Simulation b;
		b.set_sim_seed(9);
		b.set_team_civ(0, CIV_ATHENIANS);
		b.reset();
		b.player(0).phase = 3;
		for (float &r : b.player(0).res) r = 5000.0f;
		CHECK(b.cmd_build(0, 10.0f, 10.0f, BLD_ELEPHANT_STABLE) == 0);
		const uint32_t bar = b.spawn_building_world(-20.0f, -20.0f, BLD_BARRACKS, 0, 1.0f);
		const uint32_t stb = b.spawn_building_world(20.0f, -20.0f, BLD_STABLE, 0, 1.0f);
		CHECK(!b.cmd_train_template(0, bar, cav));
		CHECK(b.cmd_train_template(0, stb, cav));
	}

	// The Carthaginian Cothon (structures/cart/super_dock): Carthage only, laid on
	// a shore, a territory root that trains warships but no fishing boats.
	{
		CHECK(civ_can_build(CIV_CARTHAGINIANS, BLD_SUPER_DOCK) && !civ_can_build(CIV_ATHENIANS, BLD_SUPER_DOCK));
		CHECK(civ_building_territory_radius(BLD_SUPER_DOCK) > civ_building_territory_radius(BLD_MILITARY_COLONY));
		CHECK(building_type_info(BLD_SUPER_DOCK).territory_decay_rate == 0.0f);
		const uint16_t arrow = roster_id("cart/ship_arrow"), fishing = roster_id("cart/ship_fishing");
		CHECK(civ_unit_template(arrow).trainers & bld_bit(BLD_SUPER_DOCK));
		CHECK(!(civ_unit_template(fishing).trainers & bld_bit(BLD_SUPER_DOCK)));
		static Simulation c;
		c.set_sim_seed(11);
		c.set_team_civ(0, CIV_CARTHAGINIANS);
		c.reset();
		c.player(0).phase = 3;
		for (float &r : c.player(0).res) r = 5000.0f;
		CHECK(c.cmd_build(0, 10.0f, 10.0f, BLD_SUPER_DOCK) == 0); // inland
		const uint32_t cothon = c.spawn_building_world(30.0f, -30.0f, BLD_SUPER_DOCK, 0, 1.0f);
		CHECK(c.cmd_train_template(0, cothon, arrow));
		CHECK(!c.cmd_train_template(0, cothon, fishing));
	}

	// Mercenaries (mixins/mercenary.xml) and Neodamodes keep the Citizen class but
	// have no ResourceGatherer, so they gather nothing; citizen soldiers do.
	{
		const uint16_t merc = roster_id("athen/infantry_archer_b");
		const uint16_t merc_cav = roster_id("cart/cavalry_swordsman_gaul_b");
		const uint16_t neo = roster_id("spart/infantry_spearman_neodamodes");
		const uint16_t hoplite = roster_id("athen/infantry_spearman_b");
		CHECK(merc != CIV_UNIT_TEMPLATE_NONE && merc_cav != CIV_UNIT_TEMPLATE_NONE);
		CHECK(neo != CIV_UNIT_TEMPLATE_NONE && hoplite != CIV_UNIT_TEMPLATE_NONE);
		CHECK(civ_unit_template(merc).flags & ROSTER_CITIZEN);
		CHECK(unit_gather_class(civ_unit_template(merc).unit_type, merc) == GATHER_NONE);
		CHECK(unit_gather_class(civ_unit_template(merc_cav).unit_type, merc_cav) == GATHER_NONE);
		CHECK(unit_gather_class(civ_unit_template(neo).unit_type, neo) == GATHER_NONE);
		CHECK(unit_gather_class(civ_unit_template(hoplite).unit_type, hoplite) == GATHER_INFANTRY);
	}

	// 0 A.D. EntityLimits (template_player.xml): limits per category, raised by
	// LimitChangers and lifted by LimitRemovers.
	{
		static Simulation e;
		e.set_sim_seed(9);
		e.set_team_civ(0, CIV_ATHENIANS);
		e.set_team_civ(1, CIV_PTOLEMIES);
		e.set_team_civ(2, CIV_BRITONS);
		e.set_team_civ(3, CIV_MAURYANS);
		e.set_team_civ(4, CIV_ROMANS);
		e.reset();
		// CivilCentre 1 until phase_town (every player starts with its own).
		e.spawn_building(-40, -40, BLD_TOWN_CENTER, 0, 1.0f);
		CHECK(e.entity_limit(0, LIMIT_CIVIL_CENTRE) == 1);
		CHECK(!e.entity_limit_allows_building(0, BLD_TOWN_CENTER));
		e.player(0).phase = 2;
		CHECK(e.entity_limit(0, LIMIT_CIVIL_CENTRE) == -1 && e.entity_limit_allows_building(0, BLD_TOWN_CENTER));
		// Wonder 1, counting foundations; no 0 A.D. limit on fortresses.
		CHECK(e.entity_limit_allows_building(0, BLD_WONDER));
		e.spawn_building(40, -40, BLD_WONDER, 0, 0.0f);
		CHECK(e.entity_limit_count(0, LIMIT_WONDER) == 1 && !e.entity_limit_allows_building(0, BLD_WONDER));
		CHECK(e.entity_limit_allows_building(0, BLD_FORTRESS));
		// Ptolemies and Seleucids also need a hero for more civic centres.
		e.spawn_building(-40, 40, BLD_TOWN_CENTER, 1, 1.0f);
		e.player(1).phase = 3;
		CHECK(!e.entity_limit_allows_building(1, BLD_TOWN_CENTER));
		e.spawn_unit(-30.0f, 40.0f, 1, UNIT_HERO, 0.0f, find_template(CIV_PTOLEMIES, "Ptolemy IV"));
		CHECK(e.entity_limit_allows_building(1, BLD_TOWN_CENTER));
		// Ptolemy IV raises the Juggernaut limit by 4.
		CHECK(e.entity_limit(1, LIMIT_JUGGERNAUT) == 5);
		// Britons: WarDog 0 plus 10 per finished Kennel; Kennel 2.
		CHECK(!e.entity_limit_allows_unit(2, UNIT_WAR_DOG, roster_id("brit/war_dog")));
		e.spawn_building(40, 40, BLD_KENNEL, 2, 0.0f);
		CHECK(e.entity_limit(2, LIMIT_WAR_DOG) == 0); // a foundation changes no limit
		e.spawn_building(46, 40, BLD_KENNEL, 2, 1.0f);
		CHECK(e.entity_limit(2, LIMIT_WAR_DOG) == 10);
		CHECK(e.entity_limit_allows_unit(2, UNIT_WAR_DOG, roster_id("brit/war_dog"), 10));
		CHECK(!e.entity_limit_allows_unit(2, UNIT_WAR_DOG, roster_id("brit/war_dog"), 11));
		CHECK(!e.entity_limit_allows_building(2, BLD_KENNEL));
		// Mauryans: Pillar 0 plus 5 while Ashoka lives.
		CHECK(!e.entity_limit_allows_building(3, BLD_PILLAR));
		const uint32_t ashoka = e.spawn_unit(60.0f, 0.0f, 3, UNIT_HERO, 0.0f, roster_id("maur/hero_ashoka"));
		CHECK(e.entity_limit(3, LIMIT_PILLAR) == 5 && e.entity_limit_allows_building(3, BLD_PILLAR));
		CHECK(ashoka != 0);
		// A garrisoned entity still counts (EntityLimits counts owned entities, garrisoned or not):
		// Ashoka inside a civic centre keeps the Pillar bonus and the Hero slot.
		const uint32_t cc3 = e.spawn_building_world(64.0f, 0.0f, BLD_TOWN_CENTER, 3, 1.0f);
		CHECK(e.garrison_unit_now(ashoka, cc3));
		CHECK(e.entity_limit(3, LIMIT_PILLAR) == 5);
		CHECK(!e.entity_limit_allows_unit(3, UNIT_HERO, roster_id("maur/hero_chandragupta")));
		// Romans: Centurion 8.
		const uint16_t centurion = roster_id("rome/champion_infantry_swordsman_centurion");
		CHECK(e.entity_limit_allows_unit(4, UNIT_SWORDSMAN, centurion, 8));
		CHECK(!e.entity_limit_allows_unit(4, UNIT_SWORDSMAN, centurion, 9));
		// Heroes: one at a time, queued or alive.
		CHECK(e.entity_limit(0, LIMIT_HERO) == 1 && e.entity_limit(0, LIMIT_ANIMAL) == 50 &&
		      BUILDING_LIMIT_CATEGORY[BLD_ICE_HOUSE] == LIMIT_YAKHCHAL && e.entity_limit(0, LIMIT_YAKHCHAL) == 5);
		CHECK(!e.entity_limit_allows_unit(1, UNIT_HERO, find_template(CIV_PTOLEMIES, "Cleopatra VII")));
	}

	// A unit from a "{native}" trainer keeps its template but belongs to its owner: the
	// owner's civ bonuses apply, not its template civ's baked ones (G34). A German misses
	// nothing on a captured Briton civic centre's civilian, and pays no Briton bonus either.
	s.set_team_civ(6, CIV_GERMANS);
	const uint16_t brit_civ = roster_id("brit/support_civilian");
	CHECK(brit_civ != CIV_UNIT_TEMPLATE_NONE);
	const uint32_t bc = s.spawn_unit(20.0f, 20.0f, 6, UNIT_FEMALE_CITIZEN, 0.0f, brit_civ);
	CHECK(s.unit_template(bc) == brit_civ);
	const float brit_speed = civ_unit_template(brit_civ).speed;
	// German civilians get germ_women +0.5 m/s (x 0.356) over the Briton base: 9.5 m/s.
	CHECK(std::fabs(s.units().speed[index_of(s, bc)] - (brit_speed + 0.5f * 0.356f)) < 1e-3f);
	CHECK(std::fabs(s.units().speed[index_of(s, bc)] - civ_unit_template(roster_id("germ/support_civilian")).speed) < 1e-3f);
	// Same-civ training is untouched: a Briton civilian for a Briton keeps its baked speed.
	const uint32_t bc2 = s.spawn_unit(20.0f, 24.0f, 3, UNIT_FEMALE_CITIZEN, 0.0f, brit_civ);
	CHECK(s.units().speed[index_of(s, bc2)] == brit_speed);
	// Gaul cavalry damage is its owner's: an Athenian's loses the x1.1, a Gaul's keeps it.
	const uint16_t gaul_cav = roster_id("gaul/cavalry_swordsman_b");
	CHECK(gaul_cav != CIV_UNIT_TEMPLATE_NONE);
	const float gaul_dmg = civ_unit_template(gaul_cav).attack_damage;
	const uint32_t ac = s.spawn_unit(24.0f, 20.0f, 0, UNIT_CAVALRY, 0.0f, gaul_cav);
	CHECK(std::fabs(s.units().attack_damage[index_of(s, ac)] - gaul_dmg / 1.1f) < 1e-3f * gaul_dmg);
	const uint32_t gc = s.spawn_unit(24.0f, 24.0f, 4, UNIT_CAVALRY, 0.0f, gaul_cav);
	CHECK(s.units().attack_damage[index_of(s, gc)] == gaul_dmg);

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
