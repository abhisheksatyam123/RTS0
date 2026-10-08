// 0 A.D. TerritoryDecay.js + Capturable.js: territory regions are connected when they hold one of
// their owner's civic centres. A structure decays (20 capture points a second, against its own
// regeneration of 5) when it stands
//   - in neutral land: to Gaia (outposts and docks don't decay there),
//   - in another player's connected territory: to that player, unless they are allies,
//   - in its owner's territory cut off from every civic centre: to the connected regions
//     bordering it, or to Gaia when there are none; never next to a connected ally.
// A Gaia structure in a player's connected territory decays to that player. When decay leaves
// Gaia with the most points, the structure turns Gaia.
//   g++ -std=c++20 -O2 -Isrc tools/test_territory_decay.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_territory_decay && /tmp/test_territory_decay
#include "simulation/simulation.h"
#include "network/turn_manager.h"
#include <cmath>
#include <cstdio>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static void run(Simulation &s, float seconds) {
	for (int t = 0; t < (int)std::lround(seconds / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
}

static Simulation::Building *find_bld(Simulation &s, uint32_t id) {
	for (const auto &b : s.buildings())
		if (b.id == id) return const_cast<Simulation::Building *>(&b); // test-only pokes
	return nullptr;
}

static uint8_t owner_of(Simulation &s, uint32_t id) { return find_bld(s, id)->team; }
static float points_of(Simulation &s, uint32_t id, uint8_t p) { return find_bld(s, id)->capture_points_by_team[p]; }

// Player 0's civic centre in the south-west, player 1's in the north-east.
static void fresh(Simulation &s, bool allied = false) {
	s.set_sim_seed(41);
	s.reset();
	s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(40.0f, 40.0f, BLD_TOWN_CENTER, 1, 1.0f);
	if (allied) {
		s.set_diplomacy(0, 1, Simulation::DIPLO_ALLY);
		s.set_diplomacy(1, 0, Simulation::DIPLO_ALLY);
	}
}

int main() {
	static Simulation s;

	// 1. Own connected territory: no decay.
	{
		fresh(s);
		uint32_t bk = s.spawn_building_world(-25.0f, -25.0f, BLD_BARRACKS, 0, 1.0f);
		run(s, 40.0f);
		CHECK(owner_of(s, bk) == 0);
		CHECK(points_of(s, bk, 0) == find_bld(s, bk)->max_capture_points);
	}

	// 2. Neutral land: decays to Gaia at 20 - 5 = 15 points a second, then turns Gaia.
	{
		fresh(s);
		uint32_t bk = s.spawn_building_world(-40.0f, 30.0f, BLD_BARRACKS, 0, 1.0f);
		uint32_t op = s.spawn_building_world(30.0f, -40.0f, BLD_OUTPOST, 0, 1.0f);
		const float max = find_bld(s, bk)->max_capture_points;
		run(s, 10.0f);
		const float after10 = points_of(s, bk, 0);
		std::printf("barracks in neutral land: %.0f of %.0f points after 10 s\n", after10, max);
		CHECK(owner_of(s, bk) == 0 && std::fabs(after10 - (max - 150.0f)) < 20.0f);
		run(s, max / 15.0f);
		CHECK(owner_of(s, bk) == GAIA_PLAYER);
		CHECK(owner_of(s, op) == 0); // outposts only decay in enemy territory
	}

	// 3. An enemy's connected territory: to that enemy. An ally's: no decay.
	{
		fresh(s);
		uint32_t h = s.spawn_building_world(25.0f, 30.0f, BLD_HOUSE, 0, 1.0f);
		run(s, find_bld(s, h)->max_capture_points / 15.0f + 5.0f);
		CHECK(owner_of(s, h) == 1);
		fresh(s, true);
		h = s.spawn_building_world(25.0f, 30.0f, BLD_HOUSE, 0, 1.0f);
		run(s, 40.0f);
		CHECK(owner_of(s, h) == 0 && points_of(s, h, 0) == find_bld(s, h)->max_capture_points);
	}

	// 4. A tower's own territory cut off from any civic centre, bordering nobody: to Gaia.
	{
		fresh(s);
		uint32_t tw = s.spawn_building_world(30.0f, -40.0f, BLD_TOWER, 0, 1.0f);
		run(s, find_bld(s, tw)->max_capture_points / 15.0f + 5.0f);
		CHECK(owner_of(s, tw) == GAIA_PLAYER);
	}

	// 5. Cut off but bordering an enemy's connected territory: to that enemy;
	// bordering an ally's: safe.
	{
		fresh(s);
		uint32_t tw = s.spawn_building_world(40.0f, -4.0f, BLD_TOWER, 0, 1.0f);
		run(s, find_bld(s, tw)->max_capture_points / 15.0f + 5.0f);
		std::printf("cut-off tower next to player 1: owner %d\n", owner_of(s, tw));
		CHECK(owner_of(s, tw) == 1);
		fresh(s, true);
		tw = s.spawn_building_world(40.0f, -4.0f, BLD_TOWER, 0, 1.0f);
		run(s, 40.0f);
		CHECK(owner_of(s, tw) == 0 && points_of(s, tw, 0) == find_bld(s, tw)->max_capture_points);
	}

	// 6. A Gaia structure in player 0's connected territory decays to player 0.
	{
		fresh(s);
		uint32_t bk = s.spawn_building_world(-25.0f, -25.0f, BLD_BARRACKS, 0, 1.0f);
		Simulation::Building *b = find_bld(s, bk);
		b->team = GAIA_PLAYER;
		b->capture_points_by_team.fill(0.0f);
		run(s, b->max_capture_points * 0.5f / 15.0f + 5.0f);
		CHECK(owner_of(s, bk) == 0);
	}

	// 7. Save mid-decay: the loaded copy decays the same way and hashes the same.
	{
		fresh(s);
		uint32_t bk = s.spawn_building_world(-40.0f, 30.0f, BLD_BARRACKS, 0, 1.0f);
		run(s, 8.0f);
		const char *path = "/tmp/rts0_test_territory_decay.sav";
		CHECK(s.save_state(path));
		static Simulation l;
		fresh(l);
		CHECK(l.load_state(path));
		run(s, 10.0f);
		run(l, 10.0f);
		TurnManager tm;
		CHECK(points_of(s, bk, 0) == points_of(l, bk, 0));
		CHECK(tm.compute_sim_hash(s) == tm.compute_sim_hash(l));
	}

	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
