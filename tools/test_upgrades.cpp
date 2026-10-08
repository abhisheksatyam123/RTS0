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
		uint32_t st = s.spawn_building_world(0.0f, 0.0f, BLD_SENTRY_TOWER, 0, 1.0f);
		CHECK(s.upgrade_check(0, st) == Simulation::UPGRADE_ERR_PHASE);
		CHECK(!s.cmd_upgrade_building(0, st));
		s.player(0).phase = 2;
		CHECK(s.upgrade_check(1, st) == Simulation::UPGRADE_ERR_OWNER);
		Simulation::Building *b = find_bld(s, st);
		b->hp = b->max_hp * 0.5f;
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
		CHECK(b->garrison_cap == building_type_info(BLD_TOWER).garrison_cap);
		CHECK(b->max_capture_points >= building_type_info(BLD_TOWER).capture_max_points && near(b->capture_points, b->max_capture_points));
		CHECK(s.upgrade_check(0, st) == Simulation::UPGRADE_ERR_NONE);
		std::printf("sentry tower -> defense tower after 100 s, hp %.0f / %.0f\n", b->hp, b->max_hp);
	}

	// 3. Cancel refunds; not enough resources is refused.
	{
		fresh(s);
		s.player(0).phase = 2;
		uint32_t st = s.spawn_building_world(0.0f, 0.0f, BLD_SENTRY_TOWER, 0, 1.0f);
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

	// 4. Wall -> gate unloads the garrison that no longer fits (gate holds none).
	{
		fresh(s);
		uint32_t w = s.spawn_building_world(0.0f, 0.0f, BLD_WALL_SEGMENT, 0, 1.0f);
		uint32_t ids[3];
		for (int i = 0; i < 3; ++i) ids[i] = s.spawn_unit(0.5f + i, 3.0f, 0, UNIT_SPEARMAN);
		s.order_garrison(ids, 3, w);
		run(s, 10.0f);
		const int before = s.get_garrison_count(w);
		CHECK(before == 3);
		CHECK(s.cmd_upgrade_building(0, w));
		CHECK(s.player(0).wood() == 980.0f);
		run(s, 12.5f);
		CHECK(find_bld(s, w)->type == BLD_GATE);
		CHECK(s.get_garrison_count(w) == 0);
		int alive = 0;
		auto uv = s.units();
		for (size_t i = 0; i < uv.count; ++i)
			for (uint32_t id : ids)
				if (uv.id[i] == id && uv.alive[i]) ++alive;
		CHECK(alive == 3);
		std::printf("wall -> gate after 12 s, %d garrisoned units unloaded\n", before);
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
		uint32_t st = s.spawn_building_world(0.0f, 0.0f, BLD_SENTRY_TOWER, 0, 1.0f);
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
		uint32_t old_tower = s.spawn_building_world(0.0f, 0.0f, BLD_TOWER, 0, 1.0f);
		const float base = find_bld(s, old_tower)->max_hp;
		const TechInfo &ti = tech_info(TECH_TOWER_HEALTH);
		s.player(0).phase = 3;
		uint32_t lab = ti.researcher == BLD_TOWER ? old_tower : s.spawn_building_world(20.0f, 0.0f, ti.researcher, 0, 1.0f);
		s.set_player_resources(0, 5000, 5000, 5000, 5000);
		if (ti.prerequisite != TECH_NONE) s.player(0).techs.set(ti.prerequisite);
		CHECK(s.cmd_research(0, lab, TECH_TOWER_HEALTH));
		run(s, ti.research_time + 1.0f);
		CHECK(s.player(0).has_tech(TECH_TOWER_HEALTH));
		CHECK(near(find_bld(s, old_tower)->max_hp, base * 1.25f));
		uint32_t new_tower = s.spawn_building_world(-20.0f, 0.0f, BLD_TOWER, 0, 1.0f);
		CHECK(near(find_bld(s, new_tower)->max_hp, base * 1.25f));
		std::printf("tower placed after Tower Health: %.0f hp (was %.0f)\n", find_bld(s, new_tower)->max_hp, base);
	}

	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
