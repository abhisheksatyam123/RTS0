// 0 A.D. structure upgrades (Upgrade.js + helpers/Transform.js): sentry tower
// to defense tower, wall to gate, cancel/refund, garrison transfer, save/load
// and the lockstep hash. Also checks that structures placed after a health
// technology get its bonus.
//   g++ -std=c++20 -O2 -Isrc tools/test_upgrades.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_upgrades && /tmp/test_upgrades
#include "simulation/simulation.h"
#include "network/turn_manager.h"
#include <cmath>
#include <cstdio>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static void fresh(Simulation &s) {
	s.set_sim_seed(21);
	s.reset();
	s.set_player_resources(0, 1000, 1000, 1000, 1000);
	s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
}

static void run(Simulation &s, float seconds) {
	for (int t = 0; t < (int)std::lround(seconds / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
}

static Simulation::Building *find_bld(Simulation &s, uint32_t id) {
	for (const auto &b : s.buildings())
		if (b.id == id) return const_cast<Simulation::Building *>(&b); // test-only pokes (hp)
	return nullptr;
}

static bool near(float a, float b) { return std::fabs(a - b) <= 0.01f * std::max(1.0f, std::fabs(b)); }

int main() {
	// 1. Data: 0 A.D. template_structure_defensive_tower_sentry.xml, iber/sentry_tower.xml,
	// template_structure_defensive_wall_long.xml.
	{
		BuildingUpgradeInfo t = building_upgrade(BLD_SENTRY_TOWER, CIV_ATHENIANS);
		CHECK(t.to == BLD_TOWER && t.wood_cost == 50 && t.stone_cost == 100 && t.time == 100.0f && t.required_phase == 2);
		BuildingUpgradeInfo i = building_upgrade(BLD_SENTRY_TOWER, CIV_IBERIANS);
		CHECK(i.to == BLD_TOWER && i.wood_cost == 50 && i.stone_cost == 250 && i.time == 120.0f);
		BuildingUpgradeInfo g = building_upgrade(BLD_WALL_SEGMENT, CIV_ROMANS);
		CHECK(g.to == BLD_GATE && g.wood_cost == 20 && g.stone_cost == 0 && g.time == 12.0f && g.required_phase == 1);
		CHECK(building_upgrade(BLD_TOWN_CENTER, CIV_ATHENIANS).to == BLD_TYPE_COUNT);
		CHECK(building_upgrade(BLD_TOWER, CIV_ATHENIANS).to == BLD_TYPE_COUNT);
	}

	static Simulation s;

	// 2. Sentry tower -> defense tower: phase gate, payment, timer, health fraction.
	{
		fresh(s);
		// In the civic centre's territory, or the defense tower's own patch would decay to Gaia.
		uint32_t st = s.spawn_building_world(-20.0f, -30.0f, BLD_SENTRY_TOWER, 0, 1.0f);
		CHECK(s.upgrade_check(0, st) == Simulation::UPGRADE_ERR_PHASE);
		CHECK(!s.cmd_upgrade_building(0, st));
		s.player(0).phase = 2;
		CHECK(s.upgrade_check(1, st) == Simulation::UPGRADE_ERR_OWNER);
		Simulation::Building *b = find_bld(s, st);
		b->hp = b->max_hp * 0.5f;
		b->has_rally_point = true;
		b->autoqueue = true;
		CHECK(s.cmd_upgrade_building(0, st));
		CHECK(s.player(0).wood() == 950.0f && s.player(0).stone() == 900.0f);
		CHECK(s.upgrade_check(0, st) == Simulation::UPGRADE_ERR_BUSY);
		CHECK(!s.cmd_upgrade_building(0, st));
		run(s, 99.0f);
		b = find_bld(s, st);
		CHECK(b->type == BLD_SENTRY_TOWER && b->upgrade_to == BLD_TOWER);
		run(s, 2.0f);
		b = find_bld(s, st);
		CHECK(b->type == BLD_TOWER && b->upgrade_to == BLD_TYPE_COUNT);
		CHECK(near(b->max_hp, s.structure_max_hp(BLD_TOWER, 0)));
		CHECK(near(b->hp, b->max_hp * 0.5f));
		CHECK(!b->has_rally_point && !b->autoqueue); // Transform.js: a new entity, no rally point
		CHECK(b->garrison_cap == building_type_info(BLD_TOWER).garrison_cap);
		CHECK(b->max_capture_points >= building_type_info(BLD_TOWER).capture_max_points && near(b->capture_points, b->max_capture_points));
		CHECK(s.upgrade_check(0, st) == Simulation::UPGRADE_ERR_NONE);
		std::printf("sentry tower -> defense tower after 100 s, hp %.0f / %.0f\n", b->hp, b->max_hp);
	}

	// 3. Cancel refunds; not enough resources is refused.
	{
		fresh(s);
		s.player(0).phase = 2;
		uint32_t st = s.spawn_building_world(-20.0f, -30.0f, BLD_SENTRY_TOWER, 0, 1.0f); // in the civic centre's territory
		CHECK(s.cmd_upgrade_building(0, st));
		run(s, 30.0f);
		CHECK(!s.cmd_cancel_upgrade(1, st));
		CHECK(s.cmd_cancel_upgrade(0, st));
		CHECK(s.player(0).wood() == 1000.0f && s.player(0).stone() == 1000.0f);
		CHECK(find_bld(s, st)->type == BLD_SENTRY_TOWER && find_bld(s, st)->upgrade_to == BLD_TYPE_COUNT);
		run(s, 100.0f);
		CHECK(find_bld(s, st)->type == BLD_SENTRY_TOWER);
		s.set_player_resources(0, 1000, 1000, 1000, 50);
		CHECK(s.upgrade_check(0, st) == Simulation::UPGRADE_ERR_COST);
		CHECK(!s.cmd_upgrade_building(0, st));
	}

	// 4. Wall -> gate unloads the turret archers that no longer fit (an Athenian wall
	//    segment has 4 turret points, its gate 2).
	{
		fresh(s);
		uint32_t w = s.spawn_building_world(0.0f, 0.0f, BLD_WALL_SEGMENT, 0, 1.0f);
		uint32_t ids[3];
		for (int i = 0; i < 3; ++i) ids[i] = s.spawn_unit(0.5f + i, 3.0f, 0, UNIT_ARCHER);
		s.order_garrison(ids, 3, w);
		run(s, 10.0f);
		const int before = s.get_garrison_count(w);
		CHECK(before == 3);
		CHECK(s.cmd_upgrade_building(0, w));
		CHECK(s.player(0).wood() == 980.0f);
		run(s, 12.5f);
		CHECK(find_bld(s, w)->type == BLD_GATE);
		CHECK(s.get_garrison_count(w) == 2);
		int alive = 0;
		auto uv = s.units();
		for (size_t i = 0; i < uv.count; ++i)
			for (uint32_t id : ids)
				if (uv.id[i] == id && uv.alive[i]) ++alive;
		CHECK(alive == 3);
		std::printf("wall -> gate after 12 s, %d turret archers, 1 unloaded\n", before);
	}

	// 5. A structure destroyed mid-upgrade refunds its owner (Upgrade.js
	// OnOwnershipChanged -> CancelUpgrade(msg.from)).
	{
		fresh(s);
		uint32_t w = s.spawn_building_world(0.0f, 0.0f, BLD_WALL_SEGMENT, 0, 1.0f);
		CHECK(s.cmd_upgrade_building(0, w));
		find_bld(s, w)->hp = 0.0f;
		run(s, 1.0f);
		CHECK(s.player(0).wood() == 1000.0f);
		CHECK(find_bld(s, w) == nullptr || find_bld(s, w)->type == BLD_WALL_SEGMENT);
	}

	// 6. Save/load mid-upgrade and the lockstep hash: both copies finish identically.
	{
		fresh(s);
		s.player(0).phase = 2;
		uint32_t st = s.spawn_building_world(-20.0f, -30.0f, BLD_SENTRY_TOWER, 0, 1.0f); // in the civic centre's territory
		TurnManager tm;
		const uint32_t h0 = tm.compute_sim_hash(s);
		CHECK(s.cmd_upgrade_building(0, st));
		CHECK(tm.compute_sim_hash(s) != h0);
		run(s, 40.0f);
		const char *path = "/tmp/rts0_test_upgrades.sav";
		CHECK(s.save_state(path));
		static Simulation l;
		fresh(l);
		CHECK(l.load_state(path));
		Simulation::Building *a = find_bld(s, st), *b = find_bld(l, st);
		CHECK(b && b->upgrade_to == BLD_TOWER && near(b->upgrade_timer, a->upgrade_timer));
		CHECK(b && b->upgrade_duration == 100.0f && b->upgrade_paid[RES_STONE] == 100.0f);
		CHECK(tm.compute_sim_hash(s) == tm.compute_sim_hash(l));
		// Cancelling the loaded copy refunds what the original paid.
		CHECK(l.cmd_cancel_upgrade(0, st));
		CHECK(l.player(0).stone() == 1000.0f);
	}

	// 7. Lockstep command 26 starts and cancels an upgrade on the issuing player's structure.
	{
		fresh(s);
		uint32_t w = s.spawn_building_world(0.0f, 0.0f, BLD_WALL_SEGMENT, 0, 1.0f);
		TurnManager tm;
		auto turns = [&](int n) { for (int i = 0; i < n; ++i) tm.tick(TurnManager::TURN_DURATION, s); };
		NetCommand c;
		c.client_id = 0;
		c.cmd_type = (uint8_t)NetCommandType::NET_CMD_UPGRADE_BUILDING;
		c.target_id = w;
		tm.post_local_command(c);
		turns(5);
		CHECK(find_bld(s, w)->upgrade_to == BLD_GATE && s.player(0).wood() == 980.0f);
		c.param = 1;
		tm.post_local_command(c);
		turns(5);
		CHECK(find_bld(s, w)->upgrade_to == BLD_TYPE_COUNT && s.player(0).wood() == 1000.0f);
	}

	// 8. Structures placed after a health technology get its bonus too
	// (tower_health: +25% on defense towers), not only the ones standing then.
	{
		fresh(s);
		uint32_t old_tower = s.spawn_building_world(-20.0f, -30.0f, BLD_TOWER, 0, 1.0f); // in the civic centre's territory
		const float base = find_bld(s, old_tower)->max_hp;
		const TechInfo &ti = tech_info(TECH_TOWER_HEALTH);
		s.player(0).phase = 3;
		uint32_t lab = ti.researcher == BLD_TOWER ? old_tower : s.spawn_building_world(-25.0f, -12.0f, ti.researcher, 0, 1.0f);
		s.set_player_resources(0, 5000, 5000, 5000, 5000);
		if (ti.prerequisite != TECH_NONE) s.player(0).techs.set(ti.prerequisite);
		CHECK(s.cmd_research(0, lab, TECH_TOWER_HEALTH));
		run(s, ti.research_time + 1.0f);
		CHECK(s.player(0).has_tech(TECH_TOWER_HEALTH));
		CHECK(near(find_bld(s, old_tower)->max_hp, base * 1.25f));
		uint32_t new_tower = s.spawn_building_world(-46.0f, -15.0f, BLD_TOWER, 0, 1.0f);
		CHECK(near(find_bld(s, new_tower)->max_hp, base * 1.25f));
		std::printf("tower placed after Tower Health: %.0f hp (was %.0f)\n", find_bld(s, new_tower)->max_hp, base);
	}

	// 9. Civ structure upgrades (structures/<civ>/*.xml Upgrade): the Persian Tachara's
	// four tribute forms (10 s, free; each trickles 10 of its resource every 2 s and can
	// switch to the other three) and the Carthaginian house's apartment (50 wood, 50
	// stone, 55 s; population bonus 20, still trains women). Neither can be laid.
	{
		CHECK(building_upgrade_count(BLD_TACHARA, CIV_PERSIANS) == 4);
		CHECK(building_upgrade_count(BLD_TACHARA, CIV_ATHENIANS) == 0);
		CHECK(building_upgrade_count(BLD_TACHARA_FOOD, CIV_PERSIANS) == 3);
		CHECK(building_upgrade(BLD_TACHARA, CIV_PERSIANS, 2).to == BLD_TACHARA_STONE);
		CHECK(building_upgrade(BLD_TACHARA, CIV_PERSIANS, 4).to == BLD_TYPE_COUNT);
		const BuildingUpgradeInfo apt = building_upgrade(BLD_HOUSE, CIV_CARTHAGINIANS);
		CHECK(apt.to == BLD_APARTMENT && apt.wood_cost == 50 && apt.stone_cost == 50 && apt.time == 55.0f);
		CHECK(building_upgrade(BLD_HOUSE, CIV_ATHENIANS).to == BLD_TYPE_COUNT);
		CHECK(!civ_can_build(CIV_PERSIANS, BLD_TACHARA_FOOD) && civ_can_build(CIV_PERSIANS, BLD_TACHARA));
		// mixins/builder.xml lists {civ}/apartment: Carthage also lays one directly; no one else.
		CHECK(civ_can_build(CIV_CARTHAGINIANS, BLD_APARTMENT) && !civ_can_build(CIV_ATHENIANS, BLD_APARTMENT));
		CHECK(building_type_info(BLD_APARTMENT).pop_bonus == 20);
		CHECK(building_type_info(BLD_TACHARA_METAL).max_hp == building_type_info(BLD_TACHARA).max_hp);

		fresh(s);
		s.set_player_civ(CIV_PERSIANS);
		s.player(0).phase = 3;
		const uint32_t ta = s.spawn_building_world(-25.0f, -25.0f, BLD_TACHARA, 0, 1.0f);
		CHECK(s.upgrade_check(0, ta, 4) == Simulation::UPGRADE_ERR_NONE);
		CHECK(s.cmd_upgrade_building(0, ta, 3)); // metal
		run(s, 10.5f);
		CHECK(find_bld(s, ta)->type == BLD_TACHARA_METAL);
		const float metal = s.player(0).gold();
		run(s, 10.0f);
		CHECK(near(s.player(0).gold() - metal, 50.0f)); // 10 every 2 s
		// It still researches the Tachara's technologies and trains its heroes.
		CHECK(s.research_check(0, TECH_IMMORTALS, ta) != Simulation::TECH_ERR_BUILDING);
		// And switches to another tribute.
		CHECK(s.cmd_upgrade_building(0, ta, 0));
		CHECK(find_bld(s, ta)->upgrade_to == BLD_TACHARA_FOOD);
		run(s, 10.5f);
		CHECK(find_bld(s, ta)->type == BLD_TACHARA_FOOD);

		// Upgrade.js reads the structure's own template: a captured Tachara keeps its four
		// forms, and a captured Iberian sentry tower its 250-stone price.
		fresh(s);
		s.set_player_civ(CIV_ATHENIANS);
		s.player(0).phase = 3;
		const uint32_t ct = s.spawn_building_world(-25.0f, -25.0f, BLD_TACHARA, 0, 1.0f);
		s.set_building_civ(ct, CIV_PERSIANS);
		CHECK(s.upgrade_check(0, ct, 3) == Simulation::UPGRADE_OK);
		const uint32_t cs = s.spawn_building_world(-20.0f, -30.0f, BLD_SENTRY_TOWER, 0, 1.0f);
		s.set_building_civ(cs, CIV_IBERIANS);
		CHECK(s.cmd_upgrade_building(0, cs));
		CHECK(s.player(0).stone() == 750.0f);

		fresh(s);
		s.set_player_civ(CIV_CARTHAGINIANS);
		const uint32_t h = s.spawn_building_world(-25.0f, -25.0f, BLD_HOUSE, 0, 1.0f);
		const int pop0 = s.pop_max(0);
		CHECK(s.cmd_upgrade_building(0, h));
		CHECK(s.player(0).wood() == 950.0f && s.player(0).stone() == 950.0f);
		run(s, 55.5f);
		CHECK(find_bld(s, h)->type == BLD_APARTMENT);
		CHECK(s.pop_max(0) > pop0);
		CHECK(s.can_train_at(0, UNIT_FEMALE_CITIZEN, BLD_APARTMENT) == s.can_train_at(0, UNIT_FEMALE_CITIZEN, BLD_HOUSE));
		CHECK(!s.can_train_at(0, UNIT_SPEARMAN, BLD_APARTMENT));
	}

	// 10. The Han Imperial Court (civil_centre -> civil_centre_court: 300 wood, 300 stone,
	// 40 s, City phase) and Great Archery Tower (defense_tower -> defense_tower_great:
	// 200 stone, 40 s). Both keep their base type's role (a Civic Center: territory root,
	// dropsite; a stone tower that shoots, which like 0 A.D.'s is not a territory root) and
	// EntityLimits allows one of each.
	{
		const BuildingUpgradeInfo ct = building_upgrade(BLD_TOWN_CENTER, CIV_HAN);
		CHECK(ct.to == BLD_IMPERIAL_COURT && ct.wood_cost == 300 && ct.stone_cost == 300 && ct.time == 40.0f &&
			ct.required_phase == 3);
		const BuildingUpgradeInfo gt = building_upgrade(BLD_TOWER, CIV_HAN);
		CHECK(gt.to == BLD_TOWER_GREAT && gt.stone_cost == 200 && gt.wood_cost == 0 && gt.time == 40.0f);
		CHECK(!civ_can_build(CIV_HAN, BLD_IMPERIAL_COURT) && !civ_can_build(CIV_HAN, BLD_TOWER_GREAT));
		CHECK(is_civic_centre(BLD_IMPERIAL_COURT) && is_defense_tower(BLD_TOWER_GREAT));
		CHECK(is_territory_root(BLD_IMPERIAL_COURT) && !is_territory_root(BLD_TOWER_GREAT) && has_territory_influence(BLD_TOWER_GREAT));
		CHECK(building_tech_classes(BLD_IMPERIAL_COURT) & TC_CIVCENTRE);
		CHECK(building_tech_classes(BLD_TOWER_GREAT) & TC_STONETOWER);
		CHECK(building_type_info(BLD_IMPERIAL_COURT).max_hp > building_type_info(BLD_TOWN_CENTER).max_hp);
		CHECK(building_type_info(BLD_TOWER_GREAT).max_hp > building_type_info(BLD_TOWER).max_hp);
		// han/defense_tower_great.xml: DefaultArrowCount op="add" 2 on the tower's 4, no
		// MaxArrowCount (its GarrisonHolder/Max is the 10).
		CHECK(building_ai_info(BLD_TOWER_GREAT).default_arrows == building_ai_info(BLD_TOWER).default_arrows + 2 &&
		      building_ai_info(BLD_TOWER_GREAT).max_arrows == 0);
		CHECK(building_ai_info(BLD_TOWER_GREAT).shoots() &&
			building_ai_info(BLD_TOWER_GREAT).max_range > building_ai_info(BLD_TOWER).max_range);
		CHECK(building_ai_info(BLD_IMPERIAL_COURT).default_arrows == building_ai_info(BLD_TOWN_CENTER).default_arrows);
		// GarrisonHolder: the court holds 1.5x the Civic Center (30), the great tower 10, each with
		// its parent's List (cavalry into the court, not into the tower).
		CHECK(structure_garrison_max(BLD_IMPERIAL_COURT, CIV_HAN) == 30 && structure_garrison_max(BLD_TOWER_GREAT, CIV_HAN) == 10);
		CHECK(garrison_list_matches(structure_garrison_list(BLD_IMPERIAL_COURT, CIV_HAN), TC_CAVALRY));
		CHECK(garrison_list_matches(structure_garrison_list(BLD_TOWER_GREAT, CIV_HAN), TC_INFANTRY));
		CHECK(!garrison_list_matches(structure_garrison_list(BLD_TOWER_GREAT, CIV_HAN), TC_CAVALRY));

		fresh(s);
		s.set_player_civ(CIV_HAN);
		s.set_player_resources(0, 2000, 2000, 2000, 2000);
		uint32_t cc = 0;
		for (const auto &b : s.buildings())
			if (b.team == 0 && b.type == BLD_TOWN_CENTER) cc = b.id;
		const uint32_t cc2 = s.spawn_building_world(40.0f, 40.0f, BLD_TOWN_CENTER, 0, 1.0f);
		s.player(0).phase = 2;
		CHECK(s.upgrade_check(0, cc) == Simulation::UPGRADE_ERR_PHASE);
		s.player(0).phase = 3;
		CHECK(s.cmd_upgrade_building(0, cc));
		CHECK(s.player(0).wood() == 1700.0f && s.player(0).stone() == 1700.0f);
		// Upgrade.js ChangeUpgradedEntityCount: the second Civic Center can't start one too.
		CHECK(s.upgrade_check(0, cc2) == Simulation::UPGRADE_ERR_LIMIT);
		run(s, 40.5f);
		CHECK(find_bld(s, cc)->type == BLD_IMPERIAL_COURT);
		CHECK(s.upgrade_check(0, cc2) == Simulation::UPGRADE_ERR_LIMIT);
		// Han heroes train at the court (and fortress), no longer at a plain Civic Center.
		uint16_t hero = CIV_UNIT_TEMPLATE_NONE;
		for (uint16_t t = civ_roster_begin(CIV_HAN); t < civ_roster_end(CIV_HAN); ++t)
			if (civ_unit_template(t).flags & ROSTER_HERO) hero = t;
		CHECK(hero != CIV_UNIT_TEMPLATE_NONE);
		CHECK((civ_unit_template(hero).trainers & bld_bit(BLD_IMPERIAL_COURT)) &&
			!(civ_unit_template(hero).trainers & bld_bit(BLD_TOWN_CENTER)));
		// Still a Civic Center for training: citizens come from it as before.
		CHECK(s.can_train_at(0, UNIT_FEMALE_CITIZEN, BLD_IMPERIAL_COURT));

		const uint32_t t1 = s.spawn_building_world(0.0f, -40.0f, BLD_TOWER, 0, 1.0f);
		const uint32_t t2 = s.spawn_building_world(-40.0f, 0.0f, BLD_TOWER, 0, 1.0f);
		CHECK(s.cmd_upgrade_building(0, t1));
		CHECK(s.upgrade_check(0, t2) == Simulation::UPGRADE_ERR_LIMIT);
		run(s, 40.5f);
		CHECK(find_bld(s, t1)->type == BLD_TOWER_GREAT);
		CHECK(find_bld(s, t1)->hp > building_type_info(BLD_TOWER).max_hp);
	}

	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
