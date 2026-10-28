// 0 A.D. technology tree: table sanity, research rules, effects, save/load
// and lockstep.
//   g++ -std=c++20 -O2 -Isrc tools/test_techs.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_techs && /tmp/test_techs
#define private public // tech_mods() for the cost checks
#include "simulation/simulation.h"
#undef private
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
	// Central, so the test structures stand in its territory, not on neutral land where
	// they would decay to Gaia (TerritoryDecay.js).
	return s.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
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
		CHECK(ti.civ_mask != 0 || t == TECH_WARSHIP_RAMMING_SPEED); // Flanking Maneuvers: not in 0.28
		// Only the free, instant Seleucid army reforms (pair members with no researchTime) take no time.
		CHECK(ti.research_time > 0.0f || (t == TECH_TRADITIONAL_ARMY_SELE || t == TECH_REFORMED_ARMY_SELE));
		for (int e = 0; e < ti.effect_count; ++e)
			CHECK(tech_effects_table()[ti.effect_begin + e].stat < TSTAT_COUNT);
	}
	{
		// 0.28 cost_healer ("Healer !Champion !Hero": time, metal cost and the healer's own metal
		// loot) and siege_health (Ram or SiegeTower).
		const TechInfo &ch = tech_info(TECH_COST_HEALER);
		CHECK(ch.effect_count == 3);
		for (int e = 0; e < ch.effect_count; ++e) {
			const TechEffect &fx = tech_effects_table()[ch.effect_begin + e];
			const TechClassExpr &x = tech_affects_table()[fx.affects];
			CHECK(fx.affects_count == 1 && x.need == TC_HEALER && x.forbid == (TC_CHAMPION | TC_HERO));
		}
		CHECK(tech_effects_table()[ch.effect_begin + 2].stat == TSTAT_UNIT_LOOT_METAL);
		const TechEffect &sh = tech_effects_table()[tech_info(TECH_SIEGE_HEALTH).effect_begin];
		CHECK(sh.affects_count == 2 && tech_affects_table()[sh.affects].need == TC_RAM &&
		      tech_affects_table()[sh.affects + 1].need == TC_SIEGETOWER);
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
	// 0.28 crannog.xml drops -phase_town_{civ} and -hellenistic_metropolis from the
	// Civic Center list it otherwise keeps (City Phase stays).
	CHECK(!building_researches_tech(BLD_CRANNOG, TECH_PHASE_TOWN, CIV_BRITONS));
	CHECK(!building_researches_tech(BLD_CRANNOG, TECH_HELLENISTIC_METROPOLIS, CIV_BRITONS));
	CHECK(building_researches_tech(BLD_CRANNOG, TECH_PHASE_CITY, CIV_BRITONS));
	CHECK(!building_researches_tech(BLD_IMPERIAL_COURT, TECH_PHASE_TOWN, CIV_HAN)); // civil_centre_court -phase_town_{civ}
	CHECK(building_researches_tech(BLD_IMPERIAL_COURT, TECH_PHASE_CITY, CIV_HAN));
	// The Ptolemaic military colony: the Civic Center list less both phases and the metropolis.
	CHECK(!building_researches_tech(BLD_MILITARY_COLONY, TECH_PHASE_TOWN, CIV_BRITONS));
	CHECK(!building_researches_tech(BLD_MILITARY_COLONY, TECH_PHASE_CITY, CIV_BRITONS));
	CHECK(!building_researches_tech(BLD_MILITARY_COLONY, TECH_HELLENISTIC_METROPOLIS, CIV_BRITONS));
	CHECK(building_researches_tech(BLD_MILITARY_COLONY, TECH_ARCHERY_TRADITION, CIV_BRITONS));
	CHECK(!building_researches_tech(BLD_MILITARY_COLONY, TECH_FARMING_1, CIV_BRITONS));
	// Espionage moved to the Civic Center (template_structure_civic_civil_centre); the Han
	// research it at the ministry, the Mauryans at the palace too.
	CHECK(building_researches_tech(BLD_TOWN_CENTER, TECH_UNLOCK_SPIES, CIV_ATHENIANS));
	CHECK(!building_researches_tech(BLD_MARKET, TECH_UNLOCK_SPIES, CIV_ATHENIANS));
	CHECK(!building_researches_tech(BLD_TOWN_CENTER, TECH_SPY_COUNTER, CIV_HAN));
	CHECK(building_researches_tech(BLD_MINISTRY, TECH_SPY_COUNTER, CIV_HAN));
	CHECK(building_researches_tech(BLD_PALACE, TECH_UNLOCK_SPIES, CIV_MAURYANS));
	// Han archery training is at the barracks and stable, not the forge.
	CHECK(!building_researches_tech(BLD_FORGE, TECH_ARCHER_ATTACK_SPREAD, CIV_HAN));
	CHECK(building_researches_tech(BLD_STABLE, TECH_ARCHER_ATTACK_SPREAD, CIV_HAN));
	CHECK(building_researches_tech(BLD_FORGE, TECH_ARCHER_ATTACK_SPREAD, CIV_ATHENIANS));
	// Carthage's dock drops Undergirding Cables; the super dock researches it.
	CHECK(!building_researches_tech(BLD_DOCK, TECH_WARSHIP_HEALTH, CIV_CARTHAGINIANS));
	CHECK(building_researches_tech(BLD_SUPER_DOCK, TECH_WARSHIP_HEALTH, CIV_CARTHAGINIANS));
	CHECK(building_researches_tech(BLD_SUPER_DOCK, TECH_EXPLORATION, CIV_CARTHAGINIANS));
	CHECK(building_researches_tech(BLD_DOCK, TECH_WARSHIP_HEALTH, CIV_ROMANS));
	// cart/dock drops ship_movement_speed and han/arsenal siege_bolt_accuracy, but their
	// requirements allow those civs: another civ's captured dock or arsenal offers them.
	CHECK(tech_available_to_civ(TECH_SHIP_MOVEMENT_SPEED, CIV_CARTHAGINIANS) && tech_available_to_civ(TECH_SHIP_MOVEMENT_SPEED, CIV_PERSIANS));
	CHECK(!tech_available_to_civ(TECH_SHIP_MOVEMENT_SPEED, CIV_ATHENIANS));
	CHECK(tech_available_to_civ(TECH_SIEGE_BOLT_ACCURACY, CIV_HAN));
	CHECK(!building_researches_tech(BLD_DOCK, TECH_SHIP_MOVEMENT_SPEED, CIV_CARTHAGINIANS));
	CHECK(building_researches_tech(BLD_DOCK, TECH_SHIP_MOVEMENT_SPEED, CIV_PERSIANS));
	CHECK(!building_researches_tech(BLD_ARSENAL, TECH_SIEGE_BOLT_ACCURACY, CIV_HAN));
	CHECK(building_researches_tech(BLD_ARSENAL, TECH_SIEGE_BOLT_ACCURACY, CIV_ROMANS));
	CHECK(!civ_structures_offer_tech(TECH_SIEGE_BOLT_ACCURACY, CIV_HAN) && !civ_structures_offer_tech(TECH_SHIP_MOVEMENT_SPEED, CIV_CARTHAGINIANS));
	// warship_arrow_attack: any of notciv cart/han/ptol/rome/sele, and a failing notciv in "any"
	// rules the civ out (InterpretTechRequirements), so those five never get Naval Oxybeles.
	for (uint8_t c : {CIV_CARTHAGINIANS, CIV_HAN, CIV_PTOLEMIES, CIV_ROMANS, CIV_SELEUCIDS})
		CHECK(!tech_available_to_civ(TECH_WARSHIP_ARROW_ATTACK, c));
	CHECK(tech_available_to_civ(TECH_WARSHIP_ARROW_ATTACK, CIV_ATHENIANS));
	// Techs 0.28 lists at more than one structure, and two it lists nowhere / at one embassy.
	CHECK(building_researches_tech(BLD_MARKET, TECH_UNLOCK_SHARED_LOS, CIV_ROMANS));
	CHECK(building_researches_tech(BLD_TOWN_CENTER, TECH_UNLOCK_SHARED_LOS, CIV_ROMANS));
	CHECK(building_researches_tech(BLD_FORTRESS, TECH_TOWER_MURDERHOLES, CIV_GAULS));
	CHECK(building_researches_tech(BLD_TOWER, TECH_TOWER_MURDERHOLES, CIV_GAULS));
	CHECK(building_researches_tech(BLD_SENTRY_TOWER, TECH_TOWER_WATCH, CIV_GAULS));
	CHECK(!building_researches_tech(BLD_SENTRY_TOWER, TECH_TOWER_MURDERHOLES, CIV_GAULS));
	CHECK(building_researches_tech(BLD_GYMNASIUM, TECH_IPHICRATEAN_REFORMS, CIV_ATHENIANS));
	CHECK(building_researches_tech(BLD_DOCK, TECH_IPHICRATEAN_REFORMS, CIV_ATHENIANS));
	CHECK(building_researches_tech(BLD_TEMPLE_VESTA, TECH_HEAL_RATE, CIV_ROMANS));
	CHECK(building_researches_tech(BLD_TEMPLE_AMUN, TECH_GARRISON_HEAL, CIV_KUSHITES));
	CHECK(!building_researches_tech(BLD_GEROUSIA, TECH_AGOGE, CIV_SPARTANS));
	CHECK(building_researches_tech(BLD_EMBASSY, TECH_CELTIC_AUXILIARIES, CIV_CARTHAGINIANS));
	CHECK(!building_researches_tech(BLD_EMBASSY_IBERIAN, TECH_CELTIC_AUXILIARIES, CIV_CARTHAGINIANS));
	CHECK(!building_researches_tech(BLD_EMBASSY_ITALIC, TECH_CELTIC_AUXILIARIES, CIV_CARTHAGINIANS));
	{
		Simulation c;
		fresh(c);
		for (int k = 0; k < 5; ++k) c.spawn_building_world(-20.0f + 6.0f * k, 20.0f, BLD_HOUSE, 0, 1.0f);
		CHECK(c.phase_structures(0, 1) >= 5);
		const uint32_t cr = c.spawn_building_world(40.0f, 40.0f, BLD_CRANNOG, 0, 1.0f);
		CHECK(c.research_check(0, TECH_PHASE_TOWN, cr) == Simulation::TECH_ERR_BUILDING);
	}
	CHECK(s.research_check(0, TECH_WEAPONS_1) == Simulation::TECH_ERR_PHASE);
	CHECK(s.research_check(0, TECH_ROMAN_ROADS) == Simulation::TECH_ERR_CIV);
	s.set_player_civ(2); // Britons may research Nisean horses only after the champion unlock
	CHECK(tech_available_to_civ(TECH_NISEAN_HORSES, 2));
	CHECK(s.research_check(0, TECH_NISEAN_HORSES) == Simulation::TECH_ERR_PREREQUISITE);
	s.set_player_civ(0);
	s.set_player_resources(0, 0, 0, 0, 0);
	CHECK(s.research_check(0, TECH_PHASE_TOWN, tc) == Simulation::TECH_ERR_COST);
	// The price shown and checked is the one paid: Democracy makes Town Phase 350f/350w, 15 s.
	{
		float paid[RES_COUNT], time = 0.0f;
		s.tech_price(0, TECH_PHASE_TOWN, BLD_TOWN_CENTER, paid, time);
		CHECK(paid[RES_FOOD] == 350.0f && paid[RES_WOOD] == 350.0f && paid[RES_GOLD] == 0.0f && paid[RES_STONE] == 0.0f);
		CHECK(std::fabs(time - tech_info(TECH_PHASE_TOWN).research_time * 0.5f) < 0.01f);
		s.set_player_resources(0, 350, 350, 0, 0);
		CHECK(s.research_check(0, TECH_PHASE_TOWN, tc) == Simulation::TECH_OK);
	}
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
	// 0.28 "supersedes" is a requirement: Wheelbarrow waits for Baskets, Iron Weapons for Bronze Weapons.
	CHECK(tech_supersedes(TECH_GATHER_CAPACITY_WHEELBARROW) == TECH_GATHER_CAPACITY_BASKET);
	CHECK(tech_supersedes(TECH_SOLDIER_ATTACK_MELEE_02) == TECH_WEAPONS_1 && tech_supersedes(TECH_WEAPONS_1) == TECH_NONE);
	CHECK(s.research_check(0, TECH_GATHER_CAPACITY_WHEELBARROW) == Simulation::TECH_ERR_PREREQUISITE);
	CHECK(s.research_check(0, TECH_SOLDIER_ATTACK_MELEE_02) == Simulation::TECH_ERR_PREREQUISITE);
	s.player(0).techs.set(TECH_GATHER_CAPACITY_BASKET);
	CHECK(s.research_check(0, TECH_GATHER_CAPACITY_WHEELBARROW) == Simulation::TECH_OK);
	// 0.28 Identity classes: civ structures count toward the phases too.
	CHECK(building_phase_class(BLD_SYSSITON) == 1 && building_phase_class(BLD_MINISTRY) == 1 && building_phase_class(BLD_KENNEL) == 1);
	CHECK(building_phase_class(BLD_GYMNASIUM) == 2 && building_phase_class(BLD_SUPER_DOCK) == 2 && building_phase_class(BLD_ARMY_CAMP) == 2);
	CHECK(building_phase_class(BLD_ARSENAL, CIV_MACEDONIANS) == 2 && building_phase_class(BLD_ARSENAL, CIV_ROMANS) == 0);
	CHECK(building_phase_class(BLD_ELEPHANT_STABLE, CIV_MAURYANS) == 2 && building_phase_class(BLD_ELEPHANT_STABLE, CIV_PERSIANS) == 0);
	// City Phase needs three Town structures.
	CHECK(s.research_check(0, TECH_PHASE_CITY, tc) == Simulation::TECH_ERR_STRUCTURES);

	// The Loom (+100% civilian health) applies to existing and new units; 0.28's Civilian
	// class is support_civilian's alone, so a trader (Support) keeps its health.
	uint32_t woman = s.spawn_unit(0.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
	uint32_t trader = s.spawn_unit(3.0f, 3.0f, 0, UNIT_TRADER);
	run(s, 0.1f);
	float hp0 = unit_max_hp(s, woman);
	const float trader_hp0 = unit_max_hp(s, trader);
	CHECK(hp0 > 0.0f && trader_hp0 > 0.0f);
	uint32_t house = 0;
	for (const auto &b : s.buildings()) if (b.type == BLD_HOUSE && b.team == 0) { house = b.id; break; }
	CHECK(s.cmd_research(0, house, TECH_HEALTH_CIVILIANS_01));
	run(s, tech_info(TECH_HEALTH_CIVILIANS_01).research_time + 1.0f);
	CHECK(s.is_tech_researched(TECH_HEALTH_CIVILIANS_01));
	CHECK(std::fabs(unit_max_hp(s, woman) - 2.0f * hp0) < 0.01f);
	uint32_t woman2 = s.spawn_unit(2.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
	CHECK(std::fabs(unit_max_hp(s, woman2) - 2.0f * hp0) < 0.01f);
	CHECK(std::fabs(unit_max_hp(s, trader) - trader_hp0) < 0.01f);
	// The detailed training tooltip reads the same Health: template_stat_mods (GetTemplateData).
	{
		auto u = s.units();
		for (size_t k = 0; k < u.count; ++k)
			if (u.id[k] == woman2) {
				const uint16_t wt = s.units_.tmpl[k];
				CHECK(std::fabs(s.template_stat_mods(0, wt).health - 2.0f) < 1e-4f);
				CHECK(std::fabs(civ_unit_template(wt).hp * s.template_stat_mods(0, wt).health - unit_max_hp(s, woman2)) < 0.01f);
				CHECK(std::fabs(s.template_stat_mods(1, wt).health - 1.0f) < 1e-4f); // not player 1's Loom
			}
	}

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
		// Healers train at the temple (their Trainer list); a Civic Centre trains none.
		uint32_t tc2 = temple;
		for (const auto &b : s.buildings()) if (b.type == BLD_TOWN_CENTER) CHECK(!s.cmd_train(0, b.id, UNIT_HEALER));
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
			if (b.id == dock) CHECK(!b.queue.empty() && std::fabs(b.queue.front().duration - expect) < 0.01f);
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
		CHECK(std::fabs(s.healer_range(0) - 12.0f * 0.28f) < 0.01f); // 12 m
		CHECK(std::fabs(s.healer_rate(0) - 2.5f) < 0.01f); // Heal.js: 5 hp every 2 s
		CHECK(s.cmd_research(0, temple, TECH_HEAL_RANGE));
		run(s, tech_info(TECH_HEAL_RANGE).research_time + 1.0f);
		CHECK(std::fabs(s.healer_range(0) - 17.0f * 0.28f) < 0.01f);
		CHECK(s.cmd_research(0, temple, TECH_HEAL_RATE));
		run(s, tech_info(TECH_HEAL_RATE).research_time + 1.0f);
		CHECK(std::fabs(s.healer_rate(0) - 3.125f) < 0.01f); // Interval x0.8
		CHECK(std::fabs(s.healer_range(1) - 12.0f * 0.28f) < 0.01f); // another player's healers are unaffected
	}

	// A healer keeps a soldier under fire alive only once Healing Range reaches
	// him: 4 RTS0 units (14 m) is outside 0 A.D.'s base 12 m (3.36) but within 17 m.
	{
		float hp_left[2] = {0.0f, 0.0f};
		for (int with = 0; with < 2; ++with) {
			fresh(s);
			s.player(0).phase = 2;
			uint32_t temple = s.spawn_building_world(20.0f, -20.0f, BLD_TEMPLE, 0, 1.0f);
			if (with) CHECK(s.cmd_research(0, temple, TECH_HEAL_RANGE));
			run(s, tech_info(TECH_HEAL_RANGE).research_time + 1.0f);
			CHECK(s.is_tech_researched(TECH_HEAL_RANGE) == (with != 0));
			s.spawn_building_world(0.0f, 40.0f, BLD_TOWER, 1, 1.0f);   // enemy tower
			uint32_t sp = s.spawn_unit(8.0f, 40.0f, 0, UNIT_SPEARMAN, 400.0f); // under its arrows
			uint32_t healer = s.spawn_unit(12.0f, 40.0f, 0, UNIT_HEALER, 5000.0f);
			s.order_set_stance(&healer, 1, STANCE_STAND_GROUND); // heals only whom its range reaches
			s.cmd_stop(&healer, 1);
			s.cmd_stop(&sp, 1);
			s.order_set_stance(&sp, 1, STANCE_STAND_GROUND); // stays put rather than charging the tower
			run(s, 30.0f); // long enough that healing outweighs which arrows happen to land
			hp_left[with] = unit_hp(s, sp);
		}
		std::printf("spearman hp after 30 s under fire: %.0f healed, %.0f not\n", hp_left[1], hp_left[0]);
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
		const float swordsman_time = queued_duration(s, bar); // the tech queues behind him (ProductionQueue.js)
		CHECK(s.cmd_research(0, bar, TECH_UNLOCK_CHAMPION_INFANTRY));
		run(s, swordsman_time + tech_info(TECH_UNLOCK_CHAMPION_INFANTRY).research_time + 1.0f);
		CHECK(s.can_train_at(0, UNIT_SWORDSMAN, BLD_BARRACKS, warrior));
		CHECK(s.cmd_train_template(0, bar, warrior));
	}

	// Civ-only techs at civ structures: the Spartan Agoge at the Gerousia makes
	// champion hoplites (0 A.D. classes Champion Infantry Spearman) 25% sturdier
	// and leaves citizen hoplites, which share their RTS0 unit type, alone.
	{
		fresh(s);
		s.set_player_civ(CIV_SPARTANS);
		s.player(0).phase = 3;
		uint16_t champ = CIV_UNIT_TEMPLATE_NONE, citizen = CIV_UNIT_TEMPLATE_NONE, neo = CIV_UNIT_TEMPLATE_NONE;
		for (uint16_t i = civ_roster_begin(CIV_SPARTANS); i < civ_roster_end(CIV_SPARTANS); ++i) {
			if (std::strcmp(civ_unit_template(i).id, "spart/champion_infantry_spear") == 0) champ = i;
			if (std::strcmp(civ_unit_template(i).id, "spart/infantry_spearman_b") == 0) citizen = i;
			if (std::strcmp(civ_unit_template(i).id, "spart/infantry_spearman_neodamodes") == 0) neo = i;
		}
		CHECK(champ != CIV_UNIT_TEMPLATE_NONE && citizen != CIV_UNIT_TEMPLATE_NONE && neo != CIV_UNIT_TEMPLATE_NONE);
		CHECK(civ_unit_template(champ).unit_type == civ_unit_template(citizen).unit_type);
		CHECK(tech_info(TECH_AGOGE).researcher == BLD_GEROUSIA);
		CHECK(!tech_available_to_civ(TECH_AGOGE, CIV_ATHENIANS));
		uint32_t ger = s.spawn_building_world(20.0f, -20.0f, BLD_GEROUSIA, 0, 1.0f);
		uint32_t bar = s.spawn_building_world(-20.0f, -20.0f, BLD_BARRACKS, 0, 1.0f);
		CHECK(s.research_check(0, TECH_AGOGE, bar) == Simulation::TECH_ERR_BUILDING);
		// 0.28's spart/gerousia researches only krypteia and unlock_neodamodes: no structure
		// offers agoge, so its effect is checked by granting it.
		CHECK(s.research_check(0, TECH_AGOGE, ger) == Simulation::TECH_ERR_BUILDING);
		CHECK(!s.cmd_research(0, ger, TECH_AGOGE));
		uint32_t a = s.spawn_unit(0.0f, 0.0f, 0, (UnitType)civ_unit_template(champ).unit_type, 0.0f, champ);
		uint32_t b = s.spawn_unit(2.0f, 0.0f, 0, (UnitType)civ_unit_template(citizen).unit_type, 0.0f, citizen);
		float champ_hp = unit_max_hp(s, a), citizen_hp = unit_max_hp(s, b);
		s.complete_research(0, TECH_AGOGE);
		run(s, 1.0f);
		CHECK(std::fabs(unit_max_hp(s, a) - 1.25f * champ_hp) < 0.01f);
		CHECK(std::fabs(unit_max_hp(s, b) - citizen_hp) < 0.01f);
		uint32_t a2 = s.spawn_unit(4.0f, 0.0f, 0, (UnitType)civ_unit_template(champ).unit_type, 0.0f, champ);
		CHECK(std::fabs(unit_max_hp(s, a2) - 1.25f * champ_hp) < 0.01f);
		// Neodamodes train at the barracks only after Unlock Neodamodes.
		const uint8_t nt = civ_unit_template(neo).unit_type;
		CHECK(!s.cmd_train_template(0, bar, neo));
		CHECK(s.cmd_research(0, ger, TECH_UNLOCK_NEODAMODES));
		run(s, tech_info(TECH_UNLOCK_NEODAMODES).research_time + 1.0f);
		CHECK(s.can_train_at(0, nt, BLD_BARRACKS, neo));
		CHECK(s.cmd_train_template(0, bar, neo));
	}

	// Capture-point and cost technologies (Capturable/CapturePoints, Cost/Resources).
	{
		const uint8_t civ_before = s.player(0).civ; // later blocks keep the civ they ran with
		auto research = [&](uint8_t tech, float x) {
			const uint32_t r = s.spawn_building_world(x, 20.0f, tech_info(tech).researcher, 0, 1.0f);
			CHECK(s.cmd_research(0, r, tech));
			run(s, tech_info(tech).research_time + 1.0f);
			CHECK(s.is_tech_researched(tech));
		};
		// Persian Architecture: structures +25% capture points, built or not yet built.
		fresh(s);
		s.set_player_civ(CIV_PERSIANS);
		s.player(0).phase = 3;
		const uint32_t before = s.spawn_building_world(20.0f, -20.0f, BLD_BARRACKS, 0, 1.0f);
		const float cp = find_bld(s, before)->max_capture_points;
		research(TECH_ARCHITECTURE_PERS, -10.0f);
		CHECK(std::fabs(find_bld(s, before)->max_capture_points - 1.25f * cp) < 0.01f);
		const uint32_t after = s.spawn_building_world(0.0f, -20.0f, BLD_BARRACKS, 0, 1.0f);
		CHECK(std::fabs(find_bld(s, after)->max_capture_points - 1.25f * cp) < 0.01f);

		// Colonization: Carthaginian civic structures cost 25% less.
		fresh(s);
		s.set_player_civ(CIV_CARTHAGINIANS);
		s.player(0).phase = 3;
		CHECK(s.tech_mods(0).bld_cost[BLD_HOUSE][RES_WOOD] == 1.0f);
		research(TECH_COLONIZATION, -10.0f);
		CHECK(s.tech_mods(0).bld_cost[BLD_HOUSE][RES_WOOD] == 0.75f);
		CHECK(s.tech_mods(0).bld_cost[BLD_BARRACKS][RES_WOOD] == 1.0f); // not Civic
		// Foundation.js keeps the costs a foundation was placed for: Colonization researched
		// after placing doesn't cut its refund.
		{
			fresh(s);
			s.set_player_civ(CIV_CARTHAGINIANS);
			s.player(0).phase = 3;
			float c[RES_COUNT], c2[RES_COUNT];
			s.structure_cost(0, BLD_HOUSE, -1.0f, c);
			uint32_t f = s.spawn_building_world(10.0f, 10.0f, BLD_HOUSE, 0, 0.0f);
			s.complete_research(0, TECH_COLONIZATION);
			s.structure_cost(0, BLD_HOUSE, -1.0f, c2);
			const float wood0 = s.player(0).res[RES_WOOD];
			s.cmd_delete(0, &f, 1);
			std::printf("house foundation placed at %.0f wood, %.0f after Colonization: refund %.0f\n", c[RES_WOOD], c2[RES_WOOD],
			            s.player(0).res[RES_WOOD] - wood0);
			CHECK(c2[RES_WOOD] < c[RES_WOOD] && s.player(0).res[RES_WOOD] - wood0 == std::ceil(c[RES_WOOD]));
			// Cost.js GetResourceCosts keeps the fraction: 150 x 0.75 is 112.5 wood, paid as it is.
			CHECK(c2[RES_WOOD] == 112.5f);
			const float wood1 = s.player(0).res[RES_WOOD];
			CHECK(s.pay_for_structure(0, BLD_HOUSE, -1.0f) && s.player(0).res[RES_WOOD] == wood1 - 112.5f);
		}

		// Hellenistic Metropolis: "CivCentre !Colony" (Seleucid military colonies keep theirs).
		fresh(s);
		s.set_player_civ(CIV_SELEUCIDS);
		s.complete_research(0, TECH_HELLENISTIC_METROPOLIS);
		CHECK(s.tech_mods(0).bld_health[BLD_TOWN_CENTER] == 2.0f);
		CHECK(s.tech_mods(0).bld_capture[BLD_TOWN_CENTER] == 2.0f);
		CHECK(s.tech_mods(0).bld_health[BLD_MILITARY_COLONY] == 1.0f);
		CHECK(s.tech_mods(0).bld_capture[BLD_MILITARY_COLONY] == 1.0f);
		CHECK(s.tech_mods(0).bld_health[BLD_CRANNOG] == 1.0f); // !Naval: a captured crannog keeps its own

		// Hippocratic Oath (cost_healer): healers cost no metal.
		fresh(s);
		s.set_player_civ(CIV_ATHENIANS);
		s.player(0).phase = 3;
		const uint16_t healer = civ_default_template(CIV_ATHENIANS, UNIT_HEALER);
		CHECK(healer != CIV_UNIT_TEMPLATE_NONE);
		float cost[RES_COUNT];
		s.unit_train_cost(0, healer, civ_template_unit_info(healer, UNIT_HEALER), cost);
		CHECK(cost[RES_GOLD] > 0.0f);
		research(TECH_COST_HEALER, -10.0f);
		s.unit_train_cost(0, healer, civ_template_unit_info(healer, UNIT_HEALER), cost);
		CHECK(cost[RES_GOLD] == 0.0f);

		// Dock warship techs: Bronze Ram reaches ramming ships (0 A.D. NavalRam) only and
		// needs only the City phase (0.28 has no Flanking Maneuvers), and the Athenian
		// Arsenal of Philon (after Shipwrights) repairs warships.
		fresh(s);
		s.set_player_civ(CIV_ATHENIANS);
		s.player(0).phase = 3;
		uint16_t ram = CIV_UNIT_TEMPLATE_NONE, arrow = CIV_UNIT_TEMPLATE_NONE;
		for (uint16_t i = civ_roster_begin(CIV_ATHENIANS); i < civ_roster_end(CIV_ATHENIANS); ++i) {
			if (std::strcmp(civ_unit_template(i).id, "athen/ship_ram") == 0) ram = i;
			if (std::strcmp(civ_unit_template(i).id, "athen/ship_arrow") == 0) arrow = i;
		}
		CHECK(ram != CIV_UNIT_TEMPLATE_NONE && arrow != CIV_UNIT_TEMPLATE_NONE);
		CHECK(!tech_available_to_civ(TECH_WARSHIP_RAMMING_SPEED, CIV_ATHENIANS)); // not in 0.28
		CHECK(tech_info(TECH_WARSHIP_RAMMING_ATTACK).prerequisite == TECH_NONE);
		CHECK(tech_info(TECH_ARSENAL_PHILON).prerequisite == TECH_DOCK_EFFICIENCY);
		CHECK(!tech_available_to_civ(TECH_ARSENAL_PHILON, CIV_SPARTANS));
		research(TECH_WARSHIP_RAMMING_ATTACK, -30.0f);
		// Bronze Ram multiplies Attack/Melee/Damage/Hack only: 320 x1.3 + 50 crush.
		CHECK(std::fabs(s.tech_mods(0).tmpl[ram].atk_mul[0] - 1.3f) < 1e-4f && s.tech_mods(0).tmpl[ram].atk_mul[2] == 1.0f);
		CHECK(std::fabs(s.tech_mods(0).tmpl[ram].attack - (320.0f * 1.3f + 50.0f) / 370.0f) < 1e-4f);
		CHECK(s.tech_mods(0).tmpl[arrow].attack == 1.0f);
		research(TECH_WARSHIP_HEALTH, -50.0f); // Undergirding Cables: arrow and ramming ships
		CHECK(std::fabs(s.tech_mods(0).tmpl[ram].health - 1.2f) < 1e-4f);
		CHECK(s.tech_mods(0).regen[UNIT_TRIREME] == 0.0f);
		research(TECH_DOCK_EFFICIENCY, -70.0f);
		research(TECH_ARSENAL_PHILON, -90.0f);
		CHECK(s.tech_mods(0).regen[UNIT_TRIREME] == 1.0f);
		CHECK(s.tech_mods(0).regen[UNIT_FISHING_BOAT] == 0.0f);

		// 0.28 naval classes: Naval Ballistae (NavalSiege range and vision x1.2), Burning
		// Pitch (Ignited health x1.25) and Lookouts (Ship vision x1.1, ScoutShip x1.25 more).
		auto tmpl_id = [](const char *id) {
			for (uint16_t i = 0; i < CIV_UNIT_TEMPLATE_COUNT; ++i)
				if (std::strcmp(civ_unit_template(i).id, id) == 0) return i;
			return (uint16_t)CIV_UNIT_TEMPLATE_NONE;
		};
		const uint16_t siege = tmpl_id("rome/ship_siege"), scout = tmpl_id("rome/ship_scout");
		const uint16_t ignited = tmpl_id("brit/ship_fire_fire"), unlit = tmpl_id("brit/ship_fire");
		CHECK(siege != CIV_UNIT_TEMPLATE_NONE && scout != CIV_UNIT_TEMPLATE_NONE && ignited != CIV_UNIT_TEMPLATE_NONE);
		fresh(s);
		s.set_player_civ(CIV_ROMANS);
		s.player(0).phase = 3;
		research(TECH_WARSHIP_SIEGE_ATTACK, -10.0f);
		research(TECH_SHIP_VISION, -30.0f);
		CHECK(std::fabs(s.tech_mods(0).tmpl[siege].range_mul - 1.2f) < 1e-4f);
		CHECK(std::fabs(s.tech_mods(0).tmpl[siege].vision - 1.2f * 1.1f) < 1e-4f);
		CHECK(std::fabs(s.tech_mods(0).tmpl[scout].vision - 1.1f * 1.25f) < 1e-4f);
		CHECK(s.tech_mods(0).tmpl[scout].range_mul == 1.0f);
		fresh(s);
		s.set_player_civ(CIV_BRITONS);
		s.player(0).phase = 3;
		research(TECH_WARSHIP_FIRESHIP_ATTACK, -10.0f);
		CHECK(std::fabs(s.tech_mods(0).tmpl[ignited].health - 1.25f) < 1e-4f);
		CHECK(s.tech_mods(0).tmpl[unlit].health == 1.0f); // only the ignited form is Ignited
		// Undergirding Cables affects ArrowShip or NavalRam: no fire ship (Warship Fireship Melee)
		// is a NavalRam, so even a captor's fire ships keep their health.
		CHECK((civ_unit_template(unlit).tech_classes & TC_FIRESHIP) && (civ_unit_template(ignited).tech_classes & TC_FIRESHIP));
		CHECK(!(civ_unit_template(ram).tech_classes & TC_FIRESHIP));
		fresh(s);
		s.set_player_civ(CIV_ATHENIANS);
		s.player(0).phase = 3;
		research(TECH_WARSHIP_HEALTH, -10.0f);
		CHECK(std::fabs(s.tech_mods(0).tmpl[ram].health - 1.2f) < 1e-4f);
		CHECK(s.tech_mods(0).tmpl[unlit].health == 1.0f && s.tech_mods(0).tmpl[ignited].health == 1.0f);

		s.set_player_civ(civ_before);
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
			// Stays out of the tower's MinRange rather than walking up to hack at it.
			s.order_set_stance(&foe, 1, STANCE_STAND_GROUND);
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
		float cost[RES_COUNT];
		s.structure_cost_for(0, BLD_WALL_TOWER, cost);
		CHECK(std::fabs(cost[RES_STONE] - 2.0f * building_type_info(BLD_WALL_TOWER).stone_cost) < 1.01f);
		// 0.28's Builder lists only the WallSets: no lone wall tower or gate foundations.
		CHECK(s.cmd_build(0, -12, 0, BLD_WALL_TOWER) == 0);
		CHECK(s.cmd_build(0, -12, 0, BLD_GATE) == 0);
	}

	// Auras.js: Viriato's Guerrilla Tactics (+20% soldier speed) multiplies walk
	// speed only while a soldier is in range, on top of Horse Racing (+10%), and
	// never rewrites the speed technologies set.
	{
		fresh(s);
		s.set_player_civ(CIV_IBERIANS);
		uint32_t stable = s.spawn_building_world(20.0f, -20.0f, BLD_STABLE, 0, 1.0f);
		CHECK(s.cmd_research(0, stable, TECH_CAVALRY_MOVEMENT_SPEED));
		run(s, tech_info(TECH_CAVALRY_MOVEMENT_SPEED).research_time + 1.0f);
		CHECK(s.is_tech_researched(TECH_CAVALRY_MOVEMENT_SPEED));
		uint16_t viriato = CIV_UNIT_TEMPLATE_NONE;
		for (uint16_t i = civ_roster_begin(CIV_IBERIANS); i < civ_roster_end(CIV_IBERIANS); ++i)
			if (std::strcmp(civ_unit_template(i).specific_name, "Viriato") == 0) viriato = i;
		CHECK(viriato != CIV_UNIT_TEMPLATE_NONE);
		// Distance a horseman covers in the next second; ride() starts a fresh one
		// riding east from (-20, z).
		uint16_t tmpl = CIV_UNIT_TEMPLATE_NONE;
		uint32_t cav = 0;
		auto x = [&] {
			auto u = s.units();
			for (size_t i = 0; i < u.count; ++i)
				if (u.id[i] == cav) { tmpl = u.tmpl[i]; return u.pos_x[i]; }
			return 0.0f;
		};
		auto second = [&] {
			const float from = x();
			run(s, 1.0f);
			return x() - from;
		};
		auto ride = [&](float z) {
			cav = s.spawn_unit(-20.0f, z, 0, UNIT_CAVALRY);
			s.cmd_move(&cav, 1, 40.0f, z);
			run(s, 0.8f); // up to speed (UnitMotion Acceleration)
			return second();
		};
		const float alone = ride(0.0f);
		const float teched = civ_unit_template(tmpl).speed * 1.1f;
		CHECK(civ_aura_affects(civ_unit_template(viriato).auras[0], tmpl));
		uint32_t hero = s.spawn_unit(-14.0f, 4.0f, 0, UNIT_HERO, 0.0f, viriato);
		run(s, 0.5f); // auras are gathered every 0.4 s
		const float inspired = ride(-4.0f);
		// A save taken mid-ride carries the auras: the loaded copy rides on in step.
		{
			static Simulation copy;
			const char *aura_path = "/tmp/rts0_test_techs_aura.sav";
			CHECK(s.save_state(aura_path));
			fresh(copy);
			CHECK(copy.load_state(aura_path));
			run(s, 1.0f);
			run(copy, 1.0f);
			TurnManager tm;
			CHECK(tm.compute_sim_hash(s) == tm.compute_sim_hash(copy));
		}
		s.cmd_delete(0, &hero, 1);
		run(s, 0.5f);
		const float after = second(); // the same horseman, the aura gone
		std::printf("horseman in 1 s: %.2f m alone, %.2f m beside Viriato, %.2f m after (teched speed %.2f)\n",
		            alone, inspired, after, teched);
		CHECK(std::fabs(alone - teched) < 0.03f * teched);
		CHECK(std::fabs(inspired - teched * 1.2f) < 0.03f * teched);
		CHECK(std::fabs(after - teched) < 0.03f * teched);
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

	// Stat technologies (Archery Training, Bolt Accuracy, Lookouts, Exploration, Carrier
	// Pigeons, Crenellations, Murder Holes) and the Vision/Range rows of Healing Range and
	// Arrow Shooters: multiplied first, then added metres x0.28.
	{
		static Simulation v;
		fresh(v);
		v.set_player_civ(CIV_CARTHAGINIANS);
		auto idx = [&](uint32_t id) { auto u = v.units(); for (size_t i = 0; i < u.count; ++i) if (u.id[i] == id) return i; return (size_t)0; };
		const size_t archer = idx(v.spawn_unit(0.0f, 0.0f, 0, UNIT_ARCHER));
		const size_t ship = idx(v.spawn_unit(0.0f, 10.0f, 0, UNIT_TRIREME));
		const size_t healer = idx(v.spawn_unit(0.0f, 20.0f, 0, UNIT_HEALER));
		const uint32_t outpost = v.spawn_building_world(20.0f, 20.0f, BLD_OUTPOST, 0, 1.0f);
		const uint32_t tower = v.spawn_building_world(30.0f, -20.0f, BLD_TOWER, 0, 1.0f);
		auto building = [&](uint32_t id) -> const Simulation::Building & { return v.buildings_[v.building_id_to_idx_[id]]; };
		const float ship_vis = v.unit_vision_at(ship), healer_vis = v.unit_vision_at(healer);
		const float outpost_vis = v.building_vision_range(building(outpost)), tower_vis = v.building_vision_range(building(tower));
		CHECK(std::fabs(v.unit_tech_mods(0, UNIT_ARCHER, v.units_.tmpl[archer]).spread - 1.0f) < 1e-6f);
		for (uint8_t t : {TECH_ARCHER_ATTACK_SPREAD, TECH_SHIP_VISION, TECH_EXPLORATION, TECH_OUTPOST_VISION,
		                  TECH_TOWER_CRENELLATIONS, TECH_TOWER_MURDERHOLES, TECH_HEAL_RANGE, TECH_TOWER_RANGE})
			v.complete_research(0, t);
		CHECK(std::fabs(v.unit_tech_mods(0, UNIT_ARCHER, v.units_.tmpl[archer]).spread - 0.8f) < 1e-6f);
		CHECK(std::fabs(v.unit_vision_at(ship) - ship_vis * 1.1f * 1.2f * 1.25f) < 1e-3f); // a Carthaginian scout ship: Lookouts' ScoutShip row too
		CHECK(std::fabs(v.unit_vision_at(healer) - (healer_vis + 5.0f * 0.28f)) < 1e-3f);
		CHECK(std::fabs(v.building_vision_range(building(outpost)) - outpost_vis * 1.333333f) < 1e-3f);
		CHECK(std::fabs(v.building_vision_range(building(tower)) - (tower_vis + 8.0f * 0.28f)) < 1e-3f);
		CHECK(std::fabs(v.tech_mods(0).bld_garrison_arrows[BLD_TOWER] - 1.4f) < 1e-6f);
		CHECK(v.tech_mods(0).bld_min_range[BLD_TOWER] == 0.0f && v.tech_mods(0).bld_min_range[BLD_FORTRESS] == 0.0f);
		CHECK(v.tech_mods(0).bld_min_range[BLD_BARRACKS] < 0.0f);
		// Bolt Accuracy is the arsenal's, and not every civ's (siege_bolt_accuracy notciv list).
		CHECK(tech_info(TECH_SIEGE_BOLT_ACCURACY).researcher == BLD_ARSENAL);
		CHECK(!tech_available_to_civ(TECH_SIEGE_BOLT_ACCURACY, CIV_PERSIANS) && tech_available_to_civ(TECH_SIEGE_BOLT_ACCURACY, CIV_ROMANS));
		CHECK(!tech_available_to_civ(TECH_ARCHER_ATTACK_SPREAD, CIV_ROMANS));
		std::printf("stat techs: ship vision %.2f -> %.2f, outpost %.2f -> %.2f, tower %.2f -> %.2f\n", ship_vis,
		            v.unit_vision_at(ship), outpost_vis, v.building_vision_range(building(outpost)), tower_vis, v.building_vision_range(building(tower)));
	}

	// Resource-Rich Economy (civbonuses/mace_storehouse.json): Macedonian storehouse techs
	// research instantly (TechCostMultiplier/time 0); other civs and buildings take the usual time.
	{
		static Simulation a, m;
		fresh(a);
		const uint32_t sh_a = a.spawn_building_world(20.0f, -20.0f, BLD_STOREHOUSE, 0, 1.0f);
		CHECK(a.cmd_research(0, sh_a, TECH_WOODCUTTING_1));
		CHECK(queued_duration(a, sh_a) > 0.0f); // Athenians
		m.set_player_civ(CIV_MACEDONIANS); // civs are set before the match starts
		fresh(m);
		const uint32_t sh = m.spawn_building_world(20.0f, -20.0f, BLD_STOREHOUSE, 0, 1.0f);
		CHECK(m.cmd_research(0, sh, TECH_WOODCUTTING_1));
		CHECK(queued_duration(m, sh) == 0.0f);
		run(m, 1.1f); // done at the queue's first second (ProgressTimeout)
		CHECK(m.players_[0].techs.has(TECH_WOODCUTTING_1));
		CHECK(civ_bonus_mult(CIV_MACEDONIANS, CIV_BONUS_BUILDING_RESEARCH_TIME, BLD_TOWN_CENTER) == 1.0f);
		// A map's own civ, set after the reset: the bonus holds from the start, not only after a research.
		static Simulation j;
		fresh(j);
		j.set_team_civ(0, CIV_MACEDONIANS);
		const uint32_t sh_j = j.spawn_building_world(20.0f, -20.0f, BLD_STOREHOUSE, 0, 1.0f);
		CHECK(j.cmd_research(0, sh_j, TECH_WOODCUTTING_1));
		CHECK(queued_duration(j, sh_j) == 0.0f);
		std::printf("Resource-Rich Economy: storehouse tech %.0f s for Athenians, instant for Macedonians\n",
				tech_info(TECH_WOODCUTTING_1).research_time);
	}

	// Glorious Expansion (wonder_population_cap): each finished Wonder raises the player's
	// maximum population by 20%, stacking, once the technology is researched.
	{
		static Simulation w;
		fresh(w);
		w.set_pop_cap(300);
		const int base = w.pop_limit(0);
		w.spawn_building_world(20.0f, 20.0f, BLD_WONDER, 0, 1.0f);
		w.spawn_building_world(-20.0f, 20.0f, BLD_WONDER, 0, 1.0f);
		CHECK(w.pop_limit(0) == base);
		CHECK(tech_info(TECH_WONDER_POPULATION_CAP).researcher == BLD_WONDER);
		w.players_[0].techs.set(TECH_WONDER_POPULATION_CAP);
		CHECK(w.pop_limit(0) == (int)std::lround(base * 1.2f * 1.2f));
		CHECK(w.pop_limit(1) == base);
		std::printf("Glorious Expansion: population limit %d -> %d with two Wonders\n", base, w.pop_limit(0));
	}

	// han/maur/pers_population (autoResearch civ bonuses): Player/MaxPopulation x1.1.
	{
		static Simulation w;
		fresh(w);
		w.set_pop_cap(300);
		int lim[4];
		const uint8_t civs[4] = {CIV_HAN, CIV_MAURYANS, CIV_PERSIANS, CIV_ATHENIANS};
		for (int k = 0; k < 4; ++k) { w.set_player_civ(civs[k]); lim[k] = w.pop_limit(0); }
		w.set_player_civ(CIV_HAN);
		w.set_pop_cap(200);
		std::printf("civ max population: han %d maur %d pers %d athen %d; han on a 200 cap %d\n", lim[0], lim[1], lim[2], lim[3], w.pop_limit(0));
		CHECK(lim[0] == 330 && lim[1] == 330 && lim[2] == 330 && lim[3] == 300 && w.pop_limit(0) == 220);
	}

	// Migratory Resettlement (resettlement.json): Germanic structures except walls keep their
	// capture points off their own territory (TerritoryDecay/DecayRate x0.00001).
	{
		static Simulation g;
		g.set_team_civ(0, CIV_GERMANS);
		fresh(g);
		const float far = g.map_half_ * 0.85f;
		const uint32_t bar = g.spawn_building_world(far, far, BLD_BARRACKS, 0, 1.0f);
		const uint32_t wall = g.spawn_building_world(-far, far, BLD_WALL_SEGMENT, 0, 1.0f);
		auto bld = [&](uint32_t id) -> Simulation::Building & { return g.buildings_[g.building_id_to_idx_[id]]; };
		CHECK(tech_available_to_civ(TECH_RESETTLEMENT, CIV_GERMANS) && !tech_available_to_civ(TECH_RESETTLEMENT, CIV_GAULS));
		CHECK(tech_info(TECH_RESETTLEMENT).researcher == BLD_TOWN_CENTER && tech_info(TECH_RESETTLEMENT).required_phase == 2);
		const float base = bld(bar).territory_decay_rate;
		CHECK(base > 0.0f && g.structure_decay_rate(bld(bar)) == base);
		run(g, 5.0f);
		const float lost = bld(bar).max_capture_points - bld(bar).capture_points_by_team[0];
		CHECK(lost > 0.0f); // decays to Gaia on neutral ground before the tech
		g.complete_research(0, TECH_RESETTLEMENT);
		CHECK(std::fabs(g.structure_decay_rate(bld(bar)) - base * 1e-5f) < 1e-9f);
		CHECK(g.structure_decay_rate(bld(wall)) == bld(wall).territory_decay_rate);
		const float held = bld(bar).capture_points_by_team[0];
		run(g, 5.0f);
		CHECK(bld(bar).capture_points_by_team[0] >= held - 0.01f);
		std::printf("Migratory Resettlement: barracks decay %.2f -> %.6f points/s\n", base, g.structure_decay_rate(bld(bar)));
	}

	// Attack technologies change each damage kind on its own (Attack/<type>/Damage/<kind>,
	// GetTechModifiedProperty_numeric: x every multiply, then + every add): Naval Oxybeles
	// gives Arrow Ships +10 crush on top of their 32 pierce, and Poison Arrows scales each of
	// a slinger's kinds.
	{
		static Simulation n;
		fresh(n);
		auto idx = [&](uint32_t id) { auto u = n.units(); for (size_t i = 0; i < u.count; ++i) if (u.id[i] == id) return i; return (size_t)0; };
		const uint16_t ship_t = civ_unit_template_by_id("athen/ship_arrow");
		const uint16_t sling_t = civ_unit_template_by_id("athen/infantry_slinger_b");
		CHECK(ship_t < CIV_UNIT_TEMPLATE_COUNT && sling_t < CIV_UNIT_TEMPLATE_COUNT);
		const size_t ship = idx(n.spawn_unit(0.0f, 30.0f, 0, (UnitType)civ_unit_template(ship_t).unit_type, 0.0f, ship_t));
		const size_t sling = idx(n.spawn_unit(4.0f, 30.0f, 0, (UnitType)civ_unit_template(sling_t).unit_type, 0.0f, sling_t));
		float d[4];
		n.unit_tech_damage(ship, d);
		CHECK(d[0] == 0.0f && std::fabs(d[1] - 32.0f) < 1e-3f && d[2] == 0.0f);
		n.complete_research(0, TECH_WARSHIP_ARROW_ATTACK);
		n.unit_tech_damage(ship, d);
		CHECK(d[0] == 0.0f && std::fabs(d[1] - 32.0f) < 1e-3f && std::fabs(d[2] - 10.0f) < 1e-3f);
		CHECK(std::fabs(n.units_.attack_damage[ship] - 42.0f) < 1e-3f);
		const float ship_crush = d[2];
		n.unit_tech_damage(sling, d);
		const float sling_pierce = d[1], sling_crush = d[2];
		n.complete_research(0, TECH_POISON_ARROWS);
		n.unit_tech_damage(sling, d);
		CHECK(std::fabs(d[1] - sling_pierce * 1.2f) < 1e-3f && std::fabs(d[2] - sling_crush * 1.2f) < 1e-3f);
		CHECK(std::fabs(n.units_.attack_damage[sling] - (d[0] + d[1] + d[2])) < 1e-3f);
		n.unit_tech_damage(ship, d);
		CHECK(std::fabs(d[2] - ship_crush) < 1e-3f); // Poison Arrows is for soldiers
		std::printf("attack per damage kind: Arrow Ship 0/32/0 -> %.1f/%.1f/%.1f with Naval Oxybeles\n", 0.0f, 32.0f, ship_crush);
	}

	// Fire ships (template_unit_ship_fire): Melee Fire 15 every 0.8 s, 3x against Ships and
	// Buildings, Burning 1 a second for 10 s; nothing resists Fire. Burning Pitch scales the
	// ignited ship's Fire by 1.25 (Attack/Melee/Damage/Fire, affects Ignited).
	{
		static Simulation n;
		fresh(n);
		auto idx = [&](uint32_t id) { auto u = n.units(); for (size_t i = 0; i < u.count; ++i) if (u.id[i] == id) return i; return (size_t)0; };
		const uint16_t fire_t = civ_unit_template_by_id("brit/ship_fire");
		const uint16_t lit_t = civ_unit_template_by_id("brit/ship_fire_fire");
		const uint16_t foe_t = civ_unit_template_by_id("athen/ship_arrow");
		const uint16_t hop_t = civ_unit_template_by_id("athen/infantry_spearman_b");
		CHECK(fire_t < CIV_UNIT_TEMPLATE_COUNT && lit_t < CIV_UNIT_TEMPLATE_COUNT);
		CHECK(civ_unit_template(fire_t).attack_damage == 15.0f && std::fabs(civ_unit_template(fire_t).attack_cooldown - 0.8f) < 1e-4f);
		const size_t fire = idx(n.spawn_unit(0.0f, 30.0f, 0, (UnitType)civ_unit_template(fire_t).unit_type, 0.0f, fire_t));
		const size_t lit = idx(n.spawn_unit(4.0f, 30.0f, 0, (UnitType)civ_unit_template(lit_t).unit_type, 0.0f, lit_t));
		const size_t foe = idx(n.spawn_unit(8.0f, 30.0f, 1, (UnitType)civ_unit_template(foe_t).unit_type, 0.0f, foe_t));
		const size_t hop = idx(n.spawn_unit(8.0f, 34.0f, 1, UNIT_SPEARMAN, 0.0f, hop_t));
		float d[4];
		n.unit_tech_damage(fire, d);
		CHECK(d[0] == 0.0f && d[1] == 0.0f && d[2] == 0.0f && std::fabs(d[3] - 15.0f) < 1e-3f);
		const float vs_ship = n.unit_attack_damage(fire, foe), vs_soldier = n.unit_attack_damage(fire, hop);
		CHECK(std::fabs(vs_ship - 45.0f) < 1e-3f && std::fabs(vs_soldier - 15.0f) < 1e-3f);
		CHECK(n.unit_combat_at(fire).status == 2 && n.unit_combat_at(fire).status_damage == 2.0f && n.unit_combat_at(fire).status_interval == 2.0f);
		n.complete_research(0, TECH_WARSHIP_FIRESHIP_ATTACK);
		n.unit_tech_damage(lit, d);
		CHECK(std::fabs(d[3] - 18.75f) < 1e-3f && std::fabs(n.units_.attack_damage[lit] - 18.75f) < 1e-3f);
		n.unit_tech_damage(fire, d);
		CHECK(std::fabs(d[3] - 15.0f) < 1e-3f); // only the Ignited one
		std::printf("fire ship: %.1f vs a ship, %.1f vs a soldier; ignited with Burning Pitch %.2f\n", vs_ship, vs_soldier,
				n.units_.attack_damage[lit]);
	}

	// MaxRange technologies add metres, scaled x0.28 as the templates' ranges are: Archery
	// Tradition's +10 m is 2.8 units on a Persian archer, existing and newly trained, and
	// Arrow Shooters' +8 m is 2.24 units on a sentry tower.
	{
		static Simulation n;
		const uint32_t cc = fresh(n);
		(void)cc;
		auto idx = [&](uint32_t id) { auto u = n.units(); for (size_t i = 0; i < u.count; ++i) if (u.id[i] == id) return i; return (size_t)0; };
		const uint16_t arch_t = civ_unit_template_by_id("pers/infantry_archer_b");
		CHECK(arch_t < CIV_UNIT_TEMPLATE_COUNT);
		const size_t a = idx(n.spawn_unit(0.0f, 30.0f, 0, (UnitType)civ_unit_template(arch_t).unit_type, 0.0f, arch_t));
		const float r0 = n.units_.attack_range[a];
		n.complete_research(0, TECH_ARCHERY_TRADITION);
		const size_t b = idx(n.spawn_unit(4.0f, 30.0f, 0, (UnitType)civ_unit_template(arch_t).unit_type, 0.0f, arch_t));
		CHECK(std::fabs(n.units_.attack_range[a] - (r0 + 2.8f)) < 1e-3f);
		CHECK(std::fabs(n.units_.attack_range[b] - (r0 + 2.8f)) < 1e-3f);
		const uint32_t tw = n.spawn_building_world(20.0f, 0.0f, BLD_TOWER, 0, 1.0f);
		const float t0 = n.building_arrow_range(*find_bld(n, tw));
		n.complete_research(0, TECH_TOWER_RANGE);
		const float t1 = n.building_arrow_range(*find_bld(n, tw));
		CHECK(std::fabs(t1 - t0 - 2.24f) < 1e-3f);
		std::printf("Archery Tradition: archer range %.2f -> %.2f; Arrow Shooters: sentry tower %.2f -> %.2f\n", r0,
				n.units_.attack_range[a], t0, t1);
	}

	// ptol_structures affects "Economic !CivCentre !Naval": a mill (mill_greek, a farmstead) the
	// Ptolemies capture has 0.6x capture points; a structure that changes hands takes its new
	// owner's Health modifiers (a Ptolemaic house 0.6x, an Athenian one Hellenic Architecture).
	{
		static Simulation k;
		k.set_sim_seed(5);
		k.set_team_civ(0, CIV_PTOLEMIES);
		k.set_team_civ(1, CIV_ATHENIANS);
		k.reset();
		auto bld = [&](uint32_t id) -> const Simulation::Building * {
			for (const Simulation::Building &x : k.buildings()) if (x.id == id) return &x;
			return nullptr;
		};
		const uint32_t mill = k.spawn_building(-5, -20, BLD_MILL, GAIA_PLAYER, 1.0f);
		CHECK(bld(mill) && bld(mill)->max_capture_points > 0.0f);
		const float gaia_cp = bld(mill)->max_capture_points;
		k.cheat_convert(0, &mill, 1);
		std::printf("Ptolemaic-held mill: %.1f capture points (Gaia %.1f)\n", bld(mill)->max_capture_points, gaia_cp);
		CHECK(bld(mill)->team == 0 && std::fabs(bld(mill)->max_capture_points - gaia_cp * 0.6f) < 0.01f);
		const uint32_t house = k.spawn_building(10, 10, BLD_HOUSE, 0, 1.0f);
		const float ptol_hp = bld(house)->max_hp;
		k.cheat_convert(1, &house, 1);
		const float want = ptol_hp * k.structure_hp_mult(BLD_HOUSE, 1) / k.structure_hp_mult(BLD_HOUSE, 0);
		std::printf("Ptolemaic house %.1f hp, captured by Athens %.1f (want %.1f)\n", ptol_hp, bld(house)->max_hp, want);
		CHECK(std::fabs(k.structure_hp_mult(BLD_HOUSE, 0) - 0.6f) < 1e-4f);
		CHECK(bld(house)->team == 1 && std::fabs(bld(house)->max_hp - want) < 0.01f && bld(house)->hp <= bld(house)->max_hp + 0.01f);
	}

	// Researcher.js GetTechnologiesList reads a structure's own template: a captured Carthaginian
	// dock keeps cart/dock's "-warship_health", an Athenian one researches it; a Han player can
	// research Espionage at a captured Athenian Civic Center (han/civil_centre drops it).
	{
		static Simulation k;
		k.set_sim_seed(5);
		k.set_team_civ(0, CIV_ATHENIANS);
		k.set_team_civ(1, CIV_HAN);
		k.reset();
		for (uint8_t p : {0, 1}) {
			k.player(p).phase = 3;
			k.set_player_resources(p, 5000, 5000, 5000, 5000);
		}
		const uint32_t cart_dock = k.spawn_building(-30, 30, BLD_DOCK, 0, 1.0f);
		const uint32_t athen_dock = k.spawn_building(30, 30, BLD_DOCK, 0, 1.0f);
		k.set_building_civ(cart_dock, CIV_CARTHAGINIANS);
		k.set_building_civ(athen_dock, CIV_ATHENIANS);
		CHECK(k.research_check(0, TECH_WARSHIP_HEALTH, cart_dock) == Simulation::TECH_ERR_BUILDING);
		CHECK(k.research_check(0, TECH_WARSHIP_HEALTH, athen_dock) != Simulation::TECH_ERR_BUILDING);
		const uint32_t cc = k.spawn_building(30, -30, BLD_TOWN_CENTER, 1, 1.0f);
		k.set_building_civ(cc, CIV_ATHENIANS);
		CHECK(k.research_check(1, TECH_UNLOCK_SPIES, cc) != Simulation::TECH_ERR_BUILDING);
		k.set_building_civ(cc, CIV_HAN);
		CHECK(k.research_check(1, TECH_UNLOCK_SPIES, cc) == Simulation::TECH_ERR_BUILDING);
	}
	// TQ1: han/arsenal drops Bolt Accuracy and cart/dock Phoenician Naval Tradition, but their
	// requirements allow those civs, so a Han player researches Bolt Accuracy at a captured
	// Roman arsenal and a Carthaginian one Naval Tradition at a captured Persian dock.
	{
		static Simulation k;
		k.set_sim_seed(5);
		k.set_team_civ(0, CIV_HAN);
		k.set_team_civ(1, CIV_CARTHAGINIANS);
		k.reset();
		for (uint8_t p : {0, 1}) {
			k.player(p).phase = 3;
			k.set_player_resources(p, 5000, 5000, 5000, 5000);
		}
		const uint32_t han_arsenal = k.spawn_building(-30, -30, BLD_ARSENAL, 0, 1.0f);
		const uint32_t rome_arsenal = k.spawn_building(-30, 30, BLD_ARSENAL, 0, 1.0f);
		k.set_building_civ(han_arsenal, CIV_HAN);
		k.set_building_civ(rome_arsenal, CIV_ROMANS);
		CHECK(k.research_check(0, TECH_SIEGE_BOLT_ACCURACY, han_arsenal) == Simulation::TECH_ERR_BUILDING);
		CHECK(k.research_check(0, TECH_SIEGE_BOLT_ACCURACY, rome_arsenal) == Simulation::TECH_OK);
		k.player(1).techs.set(TECH_DOCK_EFFICIENCY); // the technology Naval Tradition supersedes
		const uint32_t cart_dock = k.spawn_building(30, -30, BLD_DOCK, 1, 1.0f);
		const uint32_t pers_dock = k.spawn_building(30, 30, BLD_DOCK, 1, 1.0f);
		k.set_building_civ(cart_dock, CIV_CARTHAGINIANS);
		k.set_building_civ(pers_dock, CIV_PERSIANS);
		CHECK(k.research_check(1, TECH_SHIP_MOVEMENT_SPEED, cart_dock) == Simulation::TECH_ERR_BUILDING);
		CHECK(k.research_check(1, TECH_SHIP_MOVEMENT_SPEED, pers_dock) == Simulation::TECH_OK);
	}

	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
