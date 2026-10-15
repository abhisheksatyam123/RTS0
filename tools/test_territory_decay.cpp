// 0 A.D. TerritoryDecay.js + Capturable.js: territory regions are connected when they hold one of
// their owner's civic centres. A structure decays (20 capture points a second, against its own
// regeneration) when it stands
//   - in neutral land: to Gaia (outposts and docks don't decay there); off in RTS0 until
//     placement keeps structures out of neutral land,
//   - in another player's connected territory: to that player, unless they are allies,
//   - in its owner's territory cut off from every civic centre: to the connected regions
//     bordering it, or to Gaia when there are none; never next to a connected ally,
//   - in another player's cut-off territory: to Gaia.
// A Gaia structure in a player's connected territory decays to that player. A structure changes
// hands once its owner holds no points, to whoever holds most (Gaia included).
//   g++ -std=c++20 -O2 -Isrc tools/test_territory_decay.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_territory_decay && /tmp/test_territory_decay
#define private public
#include "simulation/simulation.h"
#undef private
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
// Seconds a structure takes to decay away: its owner regains RegenRate a second against
// DecayRate (Capturable.js TimerTick), plus a margin.
static float decay_time(const Simulation::Building *b) {
	return b->max_capture_points / (b->territory_decay_rate - b->capture_regen_rate) + 5.0f;
}

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

	// 2. Neutral land: a structure decays to Gaia (template_structure Territory "neutral
	// enemy"); an outpost doesn't (Territory "enemy").
	{
		fresh(s);
		uint32_t bk = s.spawn_building_world(-40.0f, 30.0f, BLD_BARRACKS, 0, 1.0f);
		run(s, decay_time(find_bld(s, bk)));
		CHECK(owner_of(s, bk) == GAIA_PLAYER);
		fresh(s);
		uint32_t op = s.spawn_building_world(-40.0f, 30.0f, BLD_OUTPOST, 0, 1.0f);
		run(s, 40.0f);
		CHECK(owner_of(s, op) == 0 && points_of(s, op, 0) == find_bld(s, op)->max_capture_points);
	}

	// 3. An enemy's connected territory: to that enemy. An ally's: no decay.
	{
		fresh(s);
		uint32_t h = s.spawn_building_world(25.0f, 30.0f, BLD_HOUSE, 0, 1.0f);
		run(s, decay_time(find_bld(s, h)));
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
		run(s, decay_time(find_bld(s, tw)));
		CHECK(owner_of(s, tw) == GAIA_PLAYER);
	}

	// 5. Cut off but bordering an enemy's connected territory: to that enemy and, for the
	// bordering neutral tiles, to Gaia, by tile count (GetNeighbours, Capturable.js TimerTick);
	// bordering an ally's: safe.
	{
		fresh(s);
		uint32_t tw = s.spawn_building_world(40.0f, 1.0f, BLD_TOWER, 0, 1.0f);
		run(s, 1.0f);
		std::array<float, MAX_PLAYERS> to;
		float to_gaia = 0.0f;
		CHECK(s.territory_decay_targets(*find_bld(s, tw), to, to_gaia));
		std::printf("cut-off tower next to player 1: %.2f to player 1, %.2f to Gaia\n", to[1], to_gaia);
		CHECK(to[1] > 0.0f && to_gaia > 0.0f && std::fabs(to[1] + to_gaia - 1.0f) < 1e-4f);
		run(s, decay_time(find_bld(s, tw)));
		std::printf("  then owner %d\n", owner_of(s, tw));
		CHECK(owner_of(s, tw) == (to[1] > to_gaia ? 1 : GAIA_PLAYER));
		fresh(s, true);
		tw = s.spawn_building_world(40.0f, 1.0f, BLD_TOWER, 0, 1.0f);
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
		run(s, decay_time(b));
		CHECK(owner_of(s, bk) == 0);
	}

	// 8. A structure on another player's cut-off territory decays all to Gaia (TerritoryDecay.js
	// IsConnected, the #4749 special case), so a Gaia one there keeps its points rather than going
	// to the players bordering that territory.
	{
		fresh(s);
		s.spawn_building_world(40.0f, -4.0f, BLD_TOWER, 0, 1.0f);
		uint32_t bk = s.spawn_building_world(36.0f, -4.0f, BLD_BARRACKS, 0, 1.0f);
		Simulation::Building *b = find_bld(s, bk);
		b->team = GAIA_PLAYER;
		b->capture_points_by_team.fill(0.0f);
		s.rebuild_territory();
		run(s, 1.0f);
		std::printf("Gaia barracks at (36,-4): territory owner %d\n", s.territory_owner_at(36.0f, -4.0f));
		CHECK(s.territory_owner_at(36.0f, -4.0f) == 0);
		std::array<float, MAX_PLAYERS> to;
		float to_gaia = 0.0f;
		CHECK(s.territory_decay_targets(*find_bld(s, bk), to, to_gaia));
		CHECK(to_gaia == 1.0f && to[0] == 0.0f && to[1] == 0.0f);
		run(s, 30.0f);
		CHECK(owner_of(s, bk) == GAIA_PLAYER);
		CHECK(points_of(s, bk, 0) == 0.0f && points_of(s, bk, 1) == 0.0f);
	}

	// 7. Save mid-decay: the loaded copy decays the same way and hashes the same.
	{
		fresh(s);
		uint32_t bk = s.spawn_building_world(25.0f, 30.0f, BLD_HOUSE, 0, 1.0f);
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

	// Circular maps: territory stops at the passable edge (CCmpTerritoryManager cost 255 off the
	// world), so the ring and the corners hold none and don't count as map.
	{
		static Simulation c;
		c.set_sim_seed(41);
		c.reset();
		c.set_circular_map(true);
		const float h = c.map_half();
		const float d = (h - MAP_EDGE_DIST) * 0.72f; // near the edge, on the diagonal
		c.spawn_building_world(d, d, BLD_TOWN_CENTER, 0, 1.0f);
		c.rebuild_territory();
		CHECK(c.territory_owner_at(d, d) == 0);
		CHECK(c.territory_owner_at(h - 1.0f, h - 1.0f) == -1); // the corner, off the world
		const float in = (h - MAP_EDGE_DIST - 1.0f) * 0.7071f;
		const float out = (h - 1.0f) * 0.7071f;
		CHECK(c.territory_owner_at(in, in) == 0);   // just inside the edge
		CHECK(c.territory_owner_at(out, out) == -1); // in the edge band
		static Simulation q;
		q.set_sim_seed(41);
		q.reset();
		q.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
		q.rebuild_territory();
		static Simulation r;
		r.set_sim_seed(41);
		r.reset();
		r.set_circular_map(true);
		r.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
		r.rebuild_territory();
		// The same central territory is a bigger share of the circle than of the square (pi/4).
		const int sq = q.territory_percentages()[0], ci = r.territory_percentages()[0];
		std::printf("territory %% square %d circle %d\n", sq, ci);
		CHECK(ci >= sq * 4 / 3 - 1);
	}

	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
