// 0 A.D. allied dropsites (ResourceDropsite.js IsShared, Player sharedDropsites from the
// Diaspora tech unlock_shared_dropsites.json): a gatherer drops at an ally's dropsite only when
// her player has researched Diaspora (at a Market, as in 0.28), the owner shares it (the default), and
// the two are allies; the owner can stop sharing, and the setting survives save/load. Mobile
// dropsites follow the same rule: an ally's supply wagon (Sharable) takes her load, an ally's
// worker elephant (not Sharable) never does. A worker shut in a small pocket still heads for the
// nearest dropsite.
//   g++ -std=c++20 -O2 -Isrc tools/test_dropsite_sharing.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_dropsite_sharing && /tmp/test_dropsite_sharing
#define private public
#include "simulation/simulation.h"
#undef private
#include <cmath>
#include <cstdio>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static void run(Simulation &s, float seconds) {
	for (int k = 0; k < (int)(seconds / Simulation::FIXED_STEP); ++k) s.tick(Simulation::FIXED_STEP);
}

int main() {
	static Simulation s;
	s.set_sim_seed(5);
	s.reset();
	for (uint8_t p = 0; p < 3; ++p) s.set_player_active(p, true, false);
	s.set_player_team(0, 0);
	s.set_player_team(1, 0); // player 1: player 0's ally
	s.set_player_team(2, 1); // player 2: an enemy
	s.player(0).phase = 2;
	s.player(0).res[RES_FOOD] = s.player(0).res[RES_WOOD] = s.player(0).res[RES_STONE] = s.player(0).res[RES_GOLD] = 1000.0f;
	const uint32_t cc = s.spawn_building_world(-40.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
	const uint32_t ally_store = s.spawn_building_world(20.0f, 0.0f, BLD_STOREHOUSE, 1, 1.0f);
	const uint32_t enemy_store = s.spawn_building_world(20.0f, 20.0f, BLD_STOREHOUSE, 2, 1.0f);
	// Their owners' territory, so neither storehouse decays to Gaia on neutral land.
	s.spawn_building_world(46.0f, -6.0f, BLD_TOWN_CENTER, 1, 1.0f);
	s.spawn_building_world(20.0f, 48.0f, BLD_TOWN_CENTER, 2, 1.0f);
	const uint32_t woman = s.spawn_unit(26.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
	const int32_t w = s.lookup_unit(woman);
	s.units_.res_type_carried[w] = RES_WOOD;
	s.units_.carry[w] = 10;

	// 1. Without Diaspora an ally's storehouse is not hers. 0.28 researches Diaspora at
	// the Market (template_structure_economic_market Researcher), with no other requirement.
	CHECK(!s.can_return_resource(woman, ally_store));
	const uint32_t market = s.spawn_building_world(-40.0f, 16.0f, BLD_MARKET, 0, 1.0f);
	CHECK(s.research_check(0, TECH_UNLOCK_SHARED_DROPSITES, cc) == Simulation::TECH_ERR_BUILDING);
	CHECK(s.research_check(0, TECH_UNLOCK_SHARED_DROPSITES, market) == Simulation::TECH_OK);
	// Only the owner of shared dropsites gets the toggle.
	CHECK(!s.cmd_set_dropsite_sharing(1, ally_store, false));

	// 2. With Diaspora: the ally's storehouse takes her wood, the enemy's doesn't.
	CHECK(s.cmd_research(0, market, TECH_UNLOCK_SHARED_DROPSITES));
	run(s, 55.0f); // researchTime 50
	CHECK(s.player(0).has_tech(TECH_UNLOCK_SHARED_DROPSITES));
	// (An idle woman with a load takes it home meanwhile: give her another.)
	s.units_.res_type_carried[w] = RES_WOOD;
	s.units_.carry[w] = 10;
	CHECK(s.can_return_resource(woman, ally_store));
	CHECK(!s.can_return_resource(woman, enemy_store));
	const int wood0 = s.player_resource(0, RES_WOOD), wood1 = s.player_resource(1, RES_WOOD);
	s.order_return_resource(&woman, 1, ally_store);
	run(s, 10.0f);
	std::printf("  Diaspora: wood dropped at an ally's storehouse goes to her player (+%d), ally +%d\n",
			s.player_resource(0, RES_WOOD) - wood0, s.player_resource(1, RES_WOOD) - wood1);
	CHECK(s.player_resource(0, RES_WOOD) == wood0 + 10 && s.player_resource(1, RES_WOOD) == wood1);

	// 3. The owner (with Diaspora) stops sharing: the ally's gatherers can't drop there.
	s.player(1).techs.set(TECH_UNLOCK_SHARED_DROPSITES);
	CHECK(s.cmd_set_dropsite_sharing(1, ally_store, false));
	s.units_.res_type_carried[w] = RES_WOOD;
	s.units_.carry[w] = 10;
	CHECK(!s.can_return_resource(woman, ally_store));

	// 4. Save/load keeps the setting.
	const char *path = "/tmp/rtso_test_dropsite_sharing.sav";
	CHECK(s.save_state(path));
	static Simulation l;
	CHECK(l.load_state(path));
	std::remove(path);
	for (const auto &b : l.buildings())
		if (b.id == ally_store) CHECK(!b.dropsite_shared);
	CHECK(!l.can_return_resource(woman, ally_store));

	// 5. Sharing again, and allies breaking up: no longer usable.
	CHECK(s.cmd_set_dropsite_sharing(1, ally_store, true));
	CHECK(s.can_return_resource(woman, ally_store));
	s.set_player_team(1, 2);
	CHECK(!s.can_return_resource(woman, ally_store));

	// 6. Mobile dropsites (ResourceDropsite on template_unit_support_wagon and _elephant): a woman
	// carrying wood heads for the nearest one she may use.
	static Simulation m;
	m.set_sim_seed(5);
	m.reset();
	for (uint8_t p = 0; p < 3; ++p) m.set_player_active(p, true, false);
	m.set_player_team(0, 0);
	m.set_player_team(1, 0);
	m.set_player_team(2, 1);
	const uint32_t own_wagon = m.spawn_unit(0.0f, 0.0f, 0, UNIT_WAGON);
	const uint32_t ally_wagon = m.spawn_unit(40.0f, 0.0f, 1, UNIT_WAGON);
	const uint32_t ally_elephant = m.spawn_unit(-40.0f, 0.0f, 1, UNIT_WORKER_ELEPHANT);
	const uint32_t enemy_wagon = m.spawn_unit(0.0f, 40.0f, 2, UNIT_WAGON);
	// The dropsite a new woman carrying wood at (x, z) heads for (0: none of them).
	const uint32_t drops[] = {own_wagon, ally_wagon, ally_elephant, enemy_wagon};
	auto heads_for = [&](float x, float z) -> uint32_t {
		const int32_t k = m.lookup_unit(m.spawn_unit(x, z, 0, UNIT_FEMALE_CITIZEN));
		m.units_.res_type_carried[k] = RES_WOOD;
		m.units_.carry[k] = 10;
		m.units_.action_state[k] = ACTION_RETURN_DROPSITE;
		m.tick(Simulation::FIXED_STEP);
		for (uint32_t d : drops) {
			const int32_t j = m.lookup_unit(d);
			if (std::fabs(m.units_.target_x[k] - m.units_.pos_x[j]) < 0.01f &&
			    std::fabs(m.units_.target_z[k] - m.units_.pos_z[j]) < 0.01f)
				return d;
		}
		return 0;
	};
	const int mwood0 = m.player_resource(0, RES_WOOD), mwood1 = m.player_resource(1, RES_WOOD);
	CHECK(heads_for(0.0f, -4.0f) == own_wagon);
	CHECK(heads_for(40.0f, -4.0f) == own_wagon); // no Diaspora yet: past the ally's wagon to her own
	m.player(0).techs.set(TECH_UNLOCK_SHARED_DROPSITES);
	CHECK(heads_for(40.0f, -8.0f) == ally_wagon);
	CHECK(heads_for(-40.0f, -4.0f) == own_wagon); // an ally's worker elephant is not Sharable
	CHECK(heads_for(0.0f, 36.0f) == own_wagon);   // nor is an enemy's wagon
	run(m, 20.0f);
	std::printf("  mobile dropsites: five loads of wood dropped at wagons give her player +%d, the ally +%d\n",
			m.player_resource(0, RES_WOOD) - mwood0, m.player_resource(1, RES_WOOD) - mwood1);
	CHECK(m.player_resource(0, RES_WOOD) == mwood0 + 50 && m.player_resource(1, RES_WOOD) == mwood1);

	// 7. A worker shut in among obstructions (her navcell's land is a pocket of 9 navcells) heads for
	// the nearest dropsite, as 0.28's FindNearestDropsite (no reachability test) does, rather than
	// standing in return with her load for good. (test_naval: a worker on an island still waits.)
	static Simulation p;
	p.set_sim_seed(5);
	p.reset();
	p.set_player_active(0, true, false);
	p.spawn_building_world(-40.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
	const uint32_t store = p.spawn_building_world(16.0f, 0.0f, BLD_STOREHOUSE, 0, 1.0f);
	const int32_t pw = p.lookup_unit(p.spawn_unit(26.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN));
	auto &g = p.nav_grid_;
	auto [wcx, wcz] = g.world_to_cell(26.0f, 0.0f);
	for (int dy = -2; dy <= 2; ++dy)
		for (int dx = -2; dx <= 2; ++dx)
			if (std::max(std::abs(dx), std::abs(dy)) == 2) g.cells.at(wcx + dx, wcz + dy) = NAV_OBSTRUCTED;
	g.rebuild_regions();
	for (const auto &b : p.buildings())
		if (b.id == store) CHECK(!p.petra_same_land((size_t)pw, b)); // the pocket is her whole land
	p.units_.res_type_carried[pw] = RES_WOOD;
	p.units_.carry[pw] = 10;
	p.units_.action_state[pw] = ACTION_RETURN_DROPSITE;
	p.units_.target_x[pw] = p.units_.target_z[pw] = 99.0f;
	p.tick(Simulation::FIXED_STEP);
	CHECK(p.units_.action_state[pw] == ACTION_RETURN_DROPSITE);
	CHECK(std::fabs(p.units_.target_x[pw] - 16.0f) < 0.01f && std::fabs(p.units_.target_z[pw]) < 0.01f);

	// 8. RETURNINGRESOURCE with no dropsite at all: FinishOrder. With nothing queued she walks to
	// the supply still carrying and is free for a new task (no return held for good); with an order
	// queued she takes it.
	{
		static Simulation q;
		q.set_sim_seed(5);
		q.reset();
		const uint32_t tree = q.spawn_resource_world(6.0f, 0.0f, RES_WOOD, 200);
		const uint32_t w = q.spawn_unit(0.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
		const uint32_t v = q.spawn_unit(0.0f, 4.0f, 0, UNIT_FEMALE_CITIZEN);
		const uint32_t both[2] = {w, v};
		q.order_gather(both, 2, tree);
		q.order_gather(&v, 1, tree);
		q.order_walk(&v, 1, -10.0f, 4.0f, true);
		run(q, 20.0f);
		const int32_t wi = q.lookup_unit(w), vi = q.lookup_unit(v);
		std::printf("  no dropsite: carrying %d, action %d, order %d; with a walk queued: order %d at x %.1f\n",
				q.units_.carry[wi], (int)q.units_.action_state[wi], (int)q.units_.order_kind[wi], (int)q.units_.order_kind[vi],
				q.units_.pos_x[vi]);
		CHECK(q.units_.carry[wi] > 0 && q.units_.action_state[wi] != ACTION_RETURN_DROPSITE && q.units_.action_state[wi] != ACTION_GATHER);
		CHECK(q.units_.action_state[vi] != ACTION_RETURN_DROPSITE && q.units_.pos_x[vi] < -5.0f);
	}

	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
