// 0 A.D. WallSet walls (helpers/Walls.js GetWallPlacement, Commands.js TryConstructWall): a wall
// is laid as towers joined by long, medium and short segments that reach into the towers within
// the overlap limits; segments take their length's share of wall_long's cost and health, block
// the cells under their turned footprint, keep them through save/load, and only a long one
// upgrades to a gate. A wall snaps onto the player's towers and stops at the first piece it can't
// pay for. Palisades are a WallSet of their own (wallset_palisade), and a long one upgrades to a
// palisade gate; so are the Roman siege walls (rome/wallset_siege), which go in enemy land too,
// and their long segments become siege wall gates. An Iberian base starts walled on large maps.
// A destroyed piece leaves rubble that blocks nothing, so a breach opens the long paths. A ram or
// an elephant gets through no gap of 6 m or less in a wall, as 0.28's CheckMovement lets none.
//   g++ -std=c++20 -O2 -Isrc tools/test_walls.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_walls && /tmp/test_walls
#define private public // rebuild_territory, territory_owner_at
#include "simulation/simulation.h"
#undef private
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static const Simulation::Building *find_bld(const Simulation &s, uint32_t id) {
	for (const auto &b : s.buildings()) if (b.id == id) return &b;
	return nullptr;
}

static int count_type(const Simulation &s, uint8_t type) {
	int n = 0;
	for (const auto &b : s.buildings()) n += b.type == type;
	return n;
}

// Player 0 in the town phase with a Civic Centre at the origin.
static void setup(Simulation &s, float stone) {
	s.set_sim_seed(3);
	s.reset();
	s.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.player(0).phase = 2;
	s.player(0).res[RES_STONE] = stone;
}

int main() {
	static Simulation s;
	setup(s, 0.0f);
	const float t = WALL_TOWER_LENGTH;

	// 1. Every length from two towers apart up gets a placement: towers at both ends and between
	// the segments, each segment reaching into its towers within Min/MaxTowerOverlap.
	{
		bool all_ok = true;
		for (float d = t + 0.1f; d < 60.0f; d += 0.37f) {
			const auto p = s.wall_placement(0, -d * 0.5f, 10.0f, d * 0.5f, 10.0f);
			bool ok = p.size() >= 3 && p.size() % 2 == 1 && p.front().type == BLD_WALL_TOWER && p.back().type == BLD_WALL_TOWER &&
					std::fabs(p.front().x + d * 0.5f) < 1e-3f && std::fabs(p.back().x - d * 0.5f) < 1e-3f;
			for (size_t i = 1; ok && i + 1 < p.size(); i += 2) {
				const auto &seg = p[i];
				ok = seg.type == BLD_WALL_SEGMENT && p[i + 1].type == BLD_WALL_TOWER;
				const float reach_l = (seg.half_w - (seg.x - p[i - 1].x) + 0.5f * t) / t;
				const float reach_r = (seg.half_w - (p[i + 1].x - seg.x) + 0.5f * t) / t;
				for (float r : {reach_l, reach_r})
					ok = ok && r >= WALL_MIN_TOWER_OVERLAP - 1e-3f && r <= WALL_MAX_TOWER_OVERLAP + 1e-3f;
			}
			if (!ok) {
				std::printf("  no valid placement over %.2f\n", d);
				all_ok = false;
			}
		}
		CHECK(all_ok);
		CHECK(s.wall_placement(0, 0.0f, 10.0f, t * 0.9f, 10.0f).empty()); // no room for two towers
		// 20 units: two long segments; 12: a long and a short one, the long one first.
		const auto a = s.wall_placement(0, -10.0f, 10.0f, 10.0f, 10.0f);
		CHECK(a.size() == 5 && a[1].half_w == 4.5f && a[3].half_w == 4.5f);
		const auto b = s.wall_placement(0, -6.0f, 10.0f, 6.0f, 10.0f);
		CHECK(b.size() == 5 && b[1].half_w == 4.5f && b[3].half_w == 1.5f);
		std::printf("  placement: 20 units -> %zu pieces, 12 units -> long + short\n", a.size());
	}

	// 2. Laying it: foundations in order, segments by length (cost, health, footprint), the
	// cells under a segment blocked for other buildings.
	setup(s, 1000.0f);
	{
		const uint32_t worker = s.spawn_unit(0.0f, 6.0f, 0, UNIT_FEMALE_CITIZEN);
		const size_t laid = s.cmd_build_wall(0, -10.0f, 10.0f, 10.0f, 10.0f, &worker, 1);
		const float paid = 1000.0f - s.player(0).res[RES_STONE];
		std::printf("  a 20-unit wall: %zu pieces laid, %.0f stone\n", laid, paid);
		CHECK(laid == 5);
		CHECK(paid == 2.0f * WALL_LONG_STONE + 3.0f * building_type_info(BLD_WALL_TOWER).stone_cost);
		CHECK(count_type(s, BLD_WALL_TOWER) == 3 && count_type(s, BLD_WALL_SEGMENT) == 2);
		for (const auto &bld : s.buildings()) {
			if (bld.type != BLD_WALL_SEGMENT) continue;
			CHECK(bld.half_size == 4.5f && bld.half_d == WALL_HALF_DEPTH);
			// wall_long's Health after the civ's bonus (Hellenic Architecture for Athens).
			const float hp = WALL_LONG_HEALTH * s.structure_max_hp(BLD_WALL_SEGMENT, 0) / building_type_info(BLD_WALL_SEGMENT).max_hp;
			CHECK(std::fabs(bld.max_hp - hp) < 0.5f && bld.build_progress == 0.0f);
			CHECK(std::fabs(bld.ux) == 1.0f || std::fabs(bld.uz) == 1.0f); // along x
		}
		// A segment's middle (x = -5.5 +- 1) lies under no tower: a house can't stand there.
		CHECK(s.check_placement(0, -3, 5, BLD_HOUSE) == Simulation::PLACE_OBSTRUCTED);
		CHECK(s.check_placement(0, -3, 7, BLD_HOUSE) == Simulation::PLACE_OK);
		// Save/load keeps the segments' extents and the cells they block.
		const char *path = "/tmp/rtso_test_walls.sav";
		CHECK(s.save_state(path));
		static Simulation l;
		CHECK(l.load_state(path));
		int segs = 0;
		for (const auto &bld : l.buildings())
			if (bld.type == BLD_WALL_SEGMENT) segs += bld.half_size == 4.5f && bld.half_d == WALL_HALF_DEPTH;
		CHECK(segs == 2);
		CHECK(l.check_placement(0, -3, 5, BLD_HOUSE) == Simulation::PLACE_OBSTRUCTED);
		std::remove(path);

		// 3. A wall from that wall's end tower snaps onto it: no second tower there.
		const size_t more = s.cmd_build_wall(0, 10.3f, 10.2f, 10.0f, 22.0f);
		std::printf("  continuing from its end tower: %zu pieces, towers %d\n", more, count_type(s, BLD_WALL_TOWER));
		int at_joint = 0;
		for (const auto &bld : s.buildings())
			at_joint += bld.type == BLD_WALL_TOWER && std::fabs(bld.x - 10.0f) < 0.5f && std::fabs(bld.z - 10.0f) < 0.5f;
		CHECK(more == 4 && count_type(s, BLD_WALL_TOWER) == 5 && at_joint == 1); // long, tower, short, tower
	}

	// 4. TryConstructWall stops at the first piece the player can't pay for.
	setup(s, 100.0f);
	{
		const size_t laid = s.cmd_build_wall(0, -10.0f, 10.0f, 10.0f, 10.0f);
		std::printf("  100 stone: %zu pieces laid\n", laid);
		CHECK(laid == 2 && s.player(0).res[RES_STONE] == 100.0f - 48.0f - 36.0f);
	}

	// 5. Only a long segment upgrades to a gate (0 A.D. Upgrade is on wall_long alone).
	setup(s, 2000.0f);
	{
		s.player(0).phase = 3;
		s.player(0).res[RES_WOOD] = 2000.0f;
		CHECK(s.cmd_build_wall(0, -6.0f, 10.0f, 6.0f, 10.0f) == 5);
		uint32_t long_id = 0, short_id = 0;
		for (const auto &bld : s.buildings()) {
			if (bld.type != BLD_WALL_SEGMENT) continue;
			auto &m = const_cast<Simulation::Building &>(bld);
			m.build_progress = 1.0f;
			m.hp = m.max_hp;
			(bld.half_size == 4.5f ? long_id : short_id) = bld.id;
		}
		CHECK(long_id && short_id);
		CHECK(s.upgrade_check(0, long_id) == Simulation::UPGRADE_OK);
		CHECK(s.upgrade_check(0, short_id) != Simulation::UPGRADE_OK);
		CHECK(s.cmd_upgrade_building(0, long_id));
	}

	// 6. Built, a short segment blocks the ground between its towers.
	setup(s, 1000.0f);
	{
		const uint32_t worker = s.spawn_unit(0.0f, 7.0f, 0, UNIT_FEMALE_CITIZEN);
		CHECK(s.cmd_build_wall(0, -1.75f, 10.0f, 1.75f, 10.0f, &worker, 1) == 3);
		for (int k = 0; k < (int)(90.0f / Simulation::FIXED_STEP); ++k) s.tick(Simulation::FIXED_STEP);
		int done = 0;
		for (const auto &bld : s.buildings()) done += (bld.type == BLD_WALL_SEGMENT || bld.type == BLD_WALL_TOWER) && bld.build_progress >= 1.0f;
		auto [cx, cy] = s.nav_grid().world_to_cell(0.0f, 10.0f);
		const bool blocked = s.nav_grid().cells.at(cx, cy) == NAV_OBSTRUCTED;
		std::printf("  short wall built by one woman in 90 s: %d of 3 pieces, ground between the towers %s\n", done,
				blocked ? "blocked" : "open");
		CHECK(done == 3 && blocked);
	}

	// 7. Palisades (wallset_palisade): 14/9/4 m pieces between 3 m palisade towers, paid in wood
	// by length, in the village phase; a palisade wall snaps onto palisade towers only.
	setup(s, 0.0f);
	s.player(0).phase = 1;
	s.player(0).res[RES_WOOD] = 1000.0f;
	{
		const WallSetInfo &ws = wallset_info(WALLSET_PALISADE);
		bool all_ok = true;
		for (float d = ws.tower_length + 0.1f; d < 30.0f; d += 0.13f) {
			const auto p = s.wall_placement(0, -d * 0.5f, 10.0f, d * 0.5f, 10.0f, nullptr, nullptr, WALLSET_PALISADE);
			bool ok = p.size() >= 3 && p.size() % 2 == 1;
			for (size_t i = 0; ok && i < p.size(); ++i)
				ok = p[i].type == BLD_PALISADE && (i % 2 == 0) == is_palisade_tower(p[i].type, p[i].half_w);
			for (size_t i = 1; ok && i + 1 < p.size(); i += 2) {
				const float reach_l = (p[i].half_w - (p[i].x - p[i - 1].x) + 0.5f * ws.tower_length) / ws.tower_length;
				const float reach_r = (p[i].half_w - (p[i + 1].x - p[i].x) + 0.5f * ws.tower_length) / ws.tower_length;
				for (float r : {reach_l, reach_r}) ok = ok && r >= ws.min_overlap - 1e-3f && r <= ws.max_overlap + 1e-3f;
			}
			if (!ok) {
				std::printf("  no valid palisade placement over %.2f\n", d);
				all_ok = false;
			}
		}
		CHECK(all_ok);
		// 7 units: two long palisades (3.5 each) reaching into three towers.
		const auto a = s.wall_placement(0, -3.5f, 10.0f, 3.5f, 10.0f, nullptr, nullptr, WALLSET_PALISADE);
		CHECK(a.size() == 5 && a[1].half_w == 1.75f && a[3].half_w == 1.75f);

		const float wood0 = s.player(0).res[RES_WOOD];
		const size_t laid = s.cmd_build_wall(0, -3.5f, 10.0f, 3.5f, 10.0f, nullptr, 0, false, WALLSET_PALISADE);
		const float paid = wood0 - s.player(0).res[RES_WOOD];
		std::printf("  a 7-unit palisade: %zu pieces, %.0f wood\n", laid, paid);
		CHECK(laid == 5 && paid == 2.0f * 12.0f + 3.0f * 14.0f);
		int towers = 0, longs = 0;
		for (const auto &bld : s.buildings()) {
			if (bld.type != BLD_PALISADE) continue;
			const float scale = s.structure_max_hp(BLD_PALISADE, 0) / building_type_info(BLD_PALISADE).max_hp;
			if (is_palisade_tower(bld.type, bld.half_size)) {
				++towers;
				CHECK(bld.half_d == PALISADE_TOWER_HALF && std::fabs(bld.max_hp - 700.0f * scale) < 0.5f);
			} else {
				longs += bld.half_size == 1.75f;
				CHECK(bld.half_d == PALISADE_HALF_DEPTH && std::fabs(bld.max_hp - 600.0f * scale) < 0.5f);
			}
		}
		CHECK(towers == 3 && longs == 2);
		// A stone wall doesn't snap onto a palisade tower; a palisade does.
		CHECK(s.wall_snap_tower(0, 3.5f, 10.0f, WALLSET_STONE) == 0);
		CHECK(s.wall_snap_tower(0, 3.5f, 10.0f, WALLSET_PALISADE) != 0);
		const size_t more = s.cmd_build_wall(0, 3.6f, 10.1f, 3.5f, 14.0f, nullptr, 0, false, WALLSET_PALISADE);
		std::printf("  continuing the palisade from its end tower: %zu pieces\n", more);
		CHECK(more == 2); // a long palisade and the end tower
		// Stone walls still need the town phase.
		CHECK(s.cmd_build_wall(0, -3.0f, 20.0f, 3.0f, 20.0f) == 0);
	}

	// 8. Lockstep carries the start point in 1/32 units.
	{
		float x, z;
		Simulation::unpack_wall_point(Simulation::pack_wall_point(-37.25f, 101.5f), x, z);
		CHECK(x == -37.25f && z == 101.5f);
	}

	// 9. A long palisade upgrades to a palisade gate (palisades_long Upgrade: 10 wood, 6 s) with
	//    palisades_gate's 500 health; palisade towers and one-cell palisades don't. Unlocked, the
	//    gate lets long-range paths through; locked, it doesn't.
	setup(s, 0.0f);
	{
		s.player(0).res[RES_WOOD] = 1000.0f;
		CHECK(s.cmd_build_wall(0, -3.5f, 10.0f, 3.5f, 10.0f, nullptr, 0, false, WALLSET_PALISADE) == 5);
		const uint32_t cell = s.spawn_building_world(20.0f, 20.0f, BLD_PALISADE, 0, 1.0f);
		uint32_t long_id = 0, tower_id = 0;
		for (const auto &bld : s.buildings()) {
			if (bld.type != BLD_PALISADE) continue;
			auto &m = const_cast<Simulation::Building &>(bld);
			m.build_progress = 1.0f;
			m.hp = m.max_hp;
			if (bld.id == cell) continue;
			(is_palisade_tower(bld.type, bld.half_size) ? tower_id : long_id) = bld.id;
		}
		const BuildingUpgradeInfo up = building_upgrade(BLD_PALISADE, CIV_ATHENIANS);
		CHECK(up.to == BLD_PALISADE_GATE && up.wood_cost == 10 && up.time == 6.0f);
		CHECK(!civ_can_build(CIV_ATHENIANS, BLD_PALISADE_GATE));
		CHECK(s.upgrade_check(0, tower_id) == Simulation::UPGRADE_ERR_NONE);
		CHECK(s.upgrade_check(0, cell) == Simulation::UPGRADE_ERR_NONE);
		CHECK(s.upgrade_check(0, long_id) == Simulation::UPGRADE_OK);
		const float wood0 = s.player(0).res[RES_WOOD];
		CHECK(s.cmd_upgrade_building(0, long_id));
		CHECK(wood0 - s.player(0).res[RES_WOOD] == 10.0f);
		for (int k = 0; k < (int)(6.2f / Simulation::FIXED_STEP); ++k) s.tick(Simulation::FIXED_STEP);
		const Simulation::Building *g = find_bld(s, long_id);
		CHECK(g && g->type == BLD_PALISADE_GATE);
		if (g) {
			const float scale = s.structure_max_hp(BLD_PALISADE_GATE, 0) / building_type_info(BLD_PALISADE_GATE).max_hp;
			CHECK(std::fabs(g->max_hp - 500.0f * scale) < 0.5f && g->hp == g->max_hp);
			CHECK(g->half_size == 1.75f && g->half_d == PALISADE_HALF_DEPTH);
			auto [cx, cy] = s.nav_grid().world_to_cell(g->x, g->z);
			const bool open_paths = s.nav_grid().cells.at(cx, cy) != NAV_OBSTRUCTED;
			CHECK(s.cmd_lock_gate(0, long_id, true));
			const bool locked_blocks = s.nav_grid().cells.at(cx, cy) == NAV_OBSTRUCTED;
			std::printf("  palisade gate: %.0f hp, paths %s unlocked, %s locked\n", g->max_hp, open_paths ? "cross" : "blocked",
					locked_blocks ? "blocked" : "cross");
			CHECK(open_paths && locked_blocks);
			// Unlocked again, the owner's spearman walks through it.
			CHECK(s.cmd_lock_gate(0, long_id, false));
			const float gx = g->x;
			uint32_t u = s.spawn_unit(gx, 6.0f, 0, UNIT_SPEARMAN);
			s.cmd_move(&u, 1, gx, 14.0f);
			float cross_x = 1e9f, z = 6.0f;
			for (int k = 0; k < (int)(12.0f / Simulation::FIXED_STEP) && z < 13.5f; ++k) {
				s.tick(Simulation::FIXED_STEP);
				const auto uv = s.units();
				size_t i = 0;
				while (i < uv.count && uv.id[i] != u) ++i;
				if (i == uv.count) break;
				const float pz = uv.pos_z[i];
				if (z < 10.0f && pz >= 10.0f) cross_x = uv.pos_x[i];
				z = pz;
			}
			std::printf("  own spearman through the palisade gate: z %.1f, crossed at x %.2f (gate at %.2f)\n", z, cross_x, gx);
			CHECK(z >= 13.5f && std::fabs(cross_x - gx) < 1.2f);
		}
	}

	// 10. Roman siege walls (rome/wallset_siege): wood pieces laid from the city phase in own,
	//     neutral or enemy territory, with siege_wall_*'s health, turret points and no territory
	//     decay.
	setup(s, 0.0f);
	{
		s.player(0).civ = CIV_ROMANS;
		s.player(0).res[RES_WOOD] = 5000.0f;
		const uint32_t enemy_cc = s.spawn_building_world(60.0f, 0.0f, BLD_TOWN_CENTER, 1, 1.0f);
		CHECK(enemy_cc != 0);
		s.rebuild_territory();
		const WallSetInfo &ws = wallset_info(WALLSET_SIEGE);
		CHECK(civ_can_build(CIV_ROMANS, BLD_SIEGE_WALL) && !civ_can_build(CIV_ATHENIANS, BLD_SIEGE_WALL));
		CHECK(building_type_info(BLD_SIEGE_WALL).territory_decay_rate == 0.0f);
		// Town phase: not yet.
		CHECK(s.cmd_build_wall(0, -4.5f, 10.0f, 4.5f, 10.0f, nullptr, 0, false, WALLSET_SIEGE) == 0);
		s.player(0).phase = 3;
		// Every length gets a placement within MinTowerOverlap 0.05 and MaxTowerOverlap 1.00.
		bool all_ok = true;
		for (float d = ws.tower_length + 0.1f; d < 60.0f; d += 0.37f) {
			const auto p = s.wall_placement(0, -d * 0.5f, 30.0f, d * 0.5f, 30.0f, nullptr, nullptr, WALLSET_SIEGE);
			bool ok = p.size() >= 3 && p.size() % 2 == 1;
			for (size_t i = 0; ok && i < p.size(); ++i)
				ok = p[i].type == BLD_SIEGE_WALL && (i % 2 == 0) == is_siege_wall_tower(p[i].type, p[i].half_w);
			for (size_t i = 1; ok && i + 1 < p.size(); i += 2) {
				const float reach_l = (p[i].half_w - (p[i].x - p[i - 1].x) + 0.5f * ws.tower_length) / ws.tower_length;
				const float reach_r = (p[i].half_w - (p[i + 1].x - p[i].x) + 0.5f * ws.tower_length) / ws.tower_length;
				for (float r : {reach_l, reach_r}) ok = ok && r >= ws.min_overlap - 1e-3f && r <= ws.max_overlap + 1e-3f;
			}
			if (!ok) {
				std::printf("  no valid siege wall placement over %.2f\n", d);
				all_ok = false;
			}
		}
		CHECK(all_ok);

		// In the enemy's territory, where a stone wall may not go.
		const float ez = 6.0f;
		CHECK(s.territory_owner_at(52.0f, ez) == 1 && s.territory_owner_at(42.0f, ez) == 1);
		s.player(0).res[RES_STONE] = 5000.0f;
		CHECK(s.cmd_build_wall(0, 42.0f, ez, 52.0f, ez) == 0);
		const auto plan = s.wall_placement(0, 42.0f, ez, 52.0f, ez, nullptr, nullptr, WALLSET_SIEGE);
		float want = 0.0f;
		for (const auto &p : plan) want += wallset_piece_value(p.type, p.half_w, 1);
		const float wood0 = s.player(0).res[RES_WOOD];
		const size_t laid = s.cmd_build_wall(0, 42.0f, ez, 52.0f, ez, nullptr, 0, false, WALLSET_SIEGE);
		const float paid = wood0 - s.player(0).res[RES_WOOD];
		std::printf("  a 10-unit siege wall in enemy land: %zu of %zu pieces, %.0f wood\n", laid, plan.size(), paid);
		CHECK(laid == plan.size() && laid >= 3 && paid == want);

		// Finished, each piece has its template's health and turret points.
		uint32_t long_id = 0, tower_id = 0;
		const float scale = s.structure_max_hp(BLD_SIEGE_WALL, 0) / building_type_info(BLD_SIEGE_WALL).max_hp;
		for (const auto &bld : s.buildings()) {
			if (bld.type != BLD_SIEGE_WALL) continue;
			auto &m = const_cast<Simulation::Building &>(bld);
			m.build_progress = 1.0f;
			m.hp = m.max_hp;
			if (is_siege_wall_tower(bld.type, bld.half_size)) {
				tower_id = bld.id;
				CHECK(std::fabs(bld.max_hp - 3000.0f * scale) < 0.5f && bld.garrison_cap == 4 && bld.half_d == SIEGE_WALL_TOWER_HALF);
			} else if (bld.half_size == 0.5f * ws.lengths[0]) {
				long_id = bld.id;
				CHECK(std::fabs(bld.max_hp - 2250.0f * scale) < 0.5f && bld.garrison_cap == 7 && bld.half_d == SIEGE_WALL_HALF_DEPTH);
			} else {
				CHECK(bld.garrison_cap == (bld.half_size == 0.5f * ws.lengths[1] ? 4 : 0));
			}
		}
		CHECK(tower_id != 0);
		// On own land, a 10.5-unit wall starts with a long segment (Walls.js tries the longest first).
		CHECK(s.cmd_build_wall(0, -5.25f, 30.0f, 5.25f, 30.0f, nullptr, 0, false, WALLSET_SIEGE) >= 3);
		for (const auto &bld : s.buildings())
			if (bld.type == BLD_SIEGE_WALL && bld.half_size == 0.5f * ws.lengths[0]) {
				long_id = bld.id;
				CHECK(std::fabs(bld.max_hp - 2250.0f * scale) < 0.5f && bld.garrison_cap == 7 && bld.half_d == SIEGE_WALL_HALF_DEPTH);
			}
		CHECK(long_id != 0);
		// A siege wall snaps onto siege wall towers only.
		CHECK(s.wall_snap_tower(0, 52.0f, ez, WALLSET_STONE) == 0);
		CHECK(s.wall_snap_tower(0, 52.0f, ez, WALLSET_SIEGE) != 0);
		// Only a long segment upgrades, to the siege wall gate (siege_wall_long's Upgrade: wall_long's
		// 20 wood and 12 s), with siege_wall_gate's 1875 health and eight turret points. Unlocked, it
		// lets long-range paths through; locked, it doesn't.
		uint32_t short_id = 0;
		for (const auto &bld : s.buildings()) {
			if (bld.type != BLD_SIEGE_WALL) continue;
			auto &m = const_cast<Simulation::Building &>(bld);
			m.build_progress = 1.0f;
			m.hp = m.max_hp;
			if (!is_siege_wall_tower(bld.type, bld.half_size) && bld.half_size < 0.5f * ws.lengths[0] - 0.01f) short_id = bld.id;
		}
		CHECK(short_id != 0);
		const BuildingUpgradeInfo up = building_upgrade(BLD_SIEGE_WALL, CIV_ROMANS);
		CHECK(up.to == BLD_SIEGE_WALL_GATE && up.wood_cost == 20 && up.stone_cost == 0 && up.time == 12.0f);
		CHECK(building_upgrade(BLD_SIEGE_WALL, CIV_ATHENIANS).to == BLD_TYPE_COUNT);
		CHECK(!civ_can_build(CIV_ROMANS, BLD_SIEGE_WALL_GATE) && is_gate_type(BLD_SIEGE_WALL_GATE));
		CHECK(wallset_kind_of(BLD_SIEGE_WALL_GATE) == WALLSET_SIEGE);
		CHECK(building_type_info(BLD_SIEGE_WALL_GATE).territory_decay_rate == 0.0f);
		CHECK(s.upgrade_check(0, tower_id) == Simulation::UPGRADE_ERR_NONE);
		CHECK(s.upgrade_check(0, short_id) == Simulation::UPGRADE_ERR_NONE);
		CHECK(s.upgrade_check(0, long_id) == Simulation::UPGRADE_OK);
		const float wood1 = s.player(0).res[RES_WOOD];
		CHECK(s.cmd_upgrade_building(0, long_id));
		CHECK(wood1 - s.player(0).res[RES_WOOD] == 20.0f);
		for (int k = 0; k < (int)(12.2f / Simulation::FIXED_STEP); ++k) s.tick(Simulation::FIXED_STEP);
		const Simulation::Building *g = find_bld(s, long_id);
		CHECK(g && g->type == BLD_SIEGE_WALL_GATE);
		if (g) {
			const float gscale = s.structure_max_hp(BLD_SIEGE_WALL_GATE, 0) / building_type_info(BLD_SIEGE_WALL_GATE).max_hp;
			CHECK(std::fabs(g->max_hp - 1875.0f * gscale) < 0.5f && g->hp == g->max_hp);
			CHECK(g->half_size == 0.5f * ws.lengths[0] && g->half_d == SIEGE_WALL_HALF_DEPTH);
			const TurretPointSet *tp = s.turret_points_of(*g);
			CHECK(tp && tp->count == 8);
			CHECK(!s.can_capture_building(1, *g));
			auto [cx, cy] = s.nav_grid().world_to_cell(g->x, g->z);
			const bool open_paths = s.nav_grid().cells.at(cx, cy) != NAV_OBSTRUCTED;
			CHECK(s.cmd_lock_gate(0, long_id, true));
			const bool locked_blocks = s.nav_grid().cells.at(cx, cy) == NAV_OBSTRUCTED;
			std::printf("  siege wall gate: %.0f hp, %d turret points, paths %s unlocked, %s locked\n", g->max_hp,
					tp ? (int)tp->count : 0, open_paths ? "cross" : "blocked", locked_blocks ? "blocked" : "cross");
			CHECK(open_paths && locked_blocks);
		}
		// A map's siege wallset gate is the siege wall gate.
		const size_t n0 = s.buildings().size();
		s.spawn_gaia_gate(-20.0f, -20.0f, 0.0f, WALLSET_SIEGE);
		CHECK(s.buildings().size() == n0 + 1 && s.buildings().back().type == BLD_SIEGE_WALL_GATE &&
				s.buildings().back().half_size == 0.5f * ws.lengths[0]);
		// Not for other civs.
		s.player(0).civ = CIV_ATHENIANS;
		CHECK(s.cmd_build_wall(0, -6.0f, 40.0f, 6.0f, 40.0f, nullptr, 0, false, WALLSET_SIEGE) == 0);
	}

	// 11. The Iberians' Starting Walls (player.js placeStartingWalls): on a map over 128 tiles an
	// Iberian base gets built stone walls, a ring of long walls, gates and towers round the Civic
	// Centre (placeGenericFortress, radius 20), or only seven towers where the map script asks for
	// "towers"; other civs and Tiny maps get none.
	{
		auto walls_of = [&](uint8_t civ, MapType map, float half, int &towers, int &longs, int &gates, float &rmin, float &rmax) {
			s.reset();
			for (int p = 0; p < MAX_PLAYERS; ++p) s.player(p).civ = (Civilization)civ;
			s.generate_random_map(map, 7, 2, half);
			towers = longs = gates = 0;
			rmin = 1e9f;
			rmax = 0.0f;
			float cx = 0.0f, cz = 0.0f;
			for (const auto &b : s.buildings())
				if (b.type == BLD_TOWN_CENTER && b.team == 0) cx = b.x, cz = b.z;
			for (const auto &b : s.buildings()) {
				if (b.team != 0) continue;
				if (b.type != BLD_WALL_TOWER && b.type != BLD_WALL_SEGMENT && b.type != BLD_GATE) continue;
				towers += b.type == BLD_WALL_TOWER;
				longs += b.type == BLD_WALL_SEGMENT;
				gates += b.type == BLD_GATE;
				CHECK(b.build_progress >= 1.0f && b.hp == b.max_hp);
				const float r = std::hypot(b.x - cx, b.z - cz);
				rmin = std::min(rmin, r);
				rmax = std::max(rmax, r);
			}
		};
		int tw, lw, gt;
		float rmin, rmax;
		walls_of(CIV_IBERIANS, MAP_MAINLAND, 128.0f, tw, lw, gt, rmin, rmax);
		std::printf("  iberian mainland 256: %d towers, %d long walls, %d gates, %.1f..%.1f from the CC\n", tw, lw, gt, rmin, rmax);
		CHECK(tw >= 8 && tw == lw + gt && gt == tw / 3 && rmin > 8.0f && rmax < 32.0f);
		walls_of(CIV_IBERIANS, MAP_GEAR, 128.0f, tw, lw, gt, rmin, rmax);
		std::printf("  iberian gear 256: %d towers, %d long walls, %d gates\n", tw, lw, gt);
		CHECK(tw <= 7 && tw >= 5 && lw == 0 && gt == 0 && std::fabs(rmin - 15.0f) < 0.1f && std::fabs(rmax - 15.0f) < 0.1f);
		walls_of(CIV_IBERIANS, MAP_MAINLAND, 64.0f, tw, lw, gt, rmin, rmax);
		CHECK(tw == 0 && lw == 0 && gt == 0);
		walls_of(CIV_IBERIANS, MAP_SCHWARZWALD, 128.0f, tw, lw, gt, rmin, rmax);
		CHECK(tw == 0 && lw == 0 && gt == 0);
		walls_of(CIV_ATHENIANS, MAP_MAINLAND, 128.0f, tw, lw, gt, rmin, rmax);
		CHECK(tw == 0 && lw == 0 && gt == 0);
		// Nomad: placePlayerBase returns before the walls (rmgen2 player.js), so none.
		{
			static Simulation n;
			n.reset();
			for (int p = 0; p < MAX_PLAYERS; ++p) n.player(p).civ = CIV_IBERIANS;
			n.set_nomad(true);
			n.generate_random_map(MAP_MAINLAND, 7, 2, 128.0f);
			int nomad_walls = 0;
			for (const auto &b : n.buildings())
				nomad_walls += b.team < MAX_PLAYERS && (b.type == BLD_WALL_TOWER || b.type == BLD_WALL_SEGMENT || b.type == BLD_GATE);
			CHECK(nomad_walls == 0);
		}
	}

	// 11b. RTS0's own maps (0-10) have no 0.28 map script: there a ring a laid piece of which would
	// come within a tower's half width of a steep tile gives way to placePolygonalWall's seven towers
	// at radius 15, less those by steep tiles and those that would part the ground of either
	// passability class (place_starting_walls' off_steep). Elsewhere the ring stands as 0.28 lays it,
	// and place_generic_fortress lays the same pieces as before its outline moved to
	// generic_fortress_outline. Laid out as soak_match does: 4 players, 256 tiles, civs 8, 9, 10, 11
	// (Iberians as player 0).
	{
		static Simulation w; // its own, so the civs set here don't reach the sections below
		struct Piece { uint8_t type; float x, z, rot; };
		constexpr uint8_t L = BLD_WALL_SEGMENT, T = BLD_WALL_TOWER, G = BLD_GATE;
		auto gen4 = [&](MapType map, uint32_t seed) {
			w.reset();
			w.set_sim_seed(seed);
			w.set_waves_enabled(false);
			for (int p = 0; p < 4; ++p) w.set_team_civ((uint8_t)p, (uint8_t)(CIV_IBERIANS + p));
			w.generate_random_map(map, seed, 4, 128.0f);
		};
		auto is_wall = [](uint8_t t) { return t == BLD_WALL_TOWER || t == BLD_WALL_SEGMENT || t == BLD_GATE; };
		auto pieces_of = [&](uint8_t p, size_t from = 0) {
			std::vector<Piece> out;
			for (size_t i = from; i < w.buildings().size(); ++i) {
				const auto &b = w.buildings()[i];
				if (b.team == p && is_wall(b.type)) out.push_back({b.type, b.x, b.z, b.rot});
			}
			return out;
		};
		auto same_pieces = [](const std::vector<Piece> &got, const Piece *want, size_t n) {
			bool ok = got.size() == n;
			for (size_t i = 0; ok && i < n; ++i)
				ok = got[i].type == want[i].type && std::fabs(got[i].x - want[i].x) < 1e-3f && std::fabs(got[i].z - want[i].z) < 1e-3f &&
						std::fabs(got[i].rot - want[i].rot) < 1e-3f;
			if (!ok) {
				std::printf("  pieces differ: %zu laid, %zu expected\n", got.size(), n);
				for (const Piece &q : got) std::printf("    {%d, %.4ff, %.4ff, %.5ff},\n", q.type, q.x, q.z, q.rot);
			}
			return ok;
		};
		const float th = wallset_info(WALLSET_STONE).tower_half;
		// A laid ring's outline: its towers in the order laid, corner to corner, each side tested as
		// fortress_crosses_steep tests a side whose wall is laid, by a tower's half width and the
		// sampling pad (a whole ring only: a tower left out would cut a corner).
		auto ring_crosses_steep = [&](const std::vector<Piece> &ring) {
			std::vector<Piece> c;
			for (const Piece &q : ring) if (q.type == BLD_WALL_TOWER) c.push_back(q);
			const float rs = th + w.nav_grid_.steep_sample_pad();
			for (size_t i = 0; i < c.size(); ++i)
				if (w.nav_grid_.segment_near_steep_tile(c[i].x, c[i].z, c[(i + 1) % c.size()].x, c[(i + 1) % c.size()].z, rs)) return true;
			return false;
		};
		auto count = [](const std::vector<Piece> &v, uint8_t t) { int n = 0; for (const Piece &q : v) n += q.type == t; return n; };
		auto cc_of = [&](uint8_t p, float &cx, float &cz) {
			for (const auto &b : w.buildings())
				if (b.type == BLD_TOWN_CENTER && b.team == p) { cx = b.x; cz = b.z; return; }
		};

		// placePolygonalWall's corners round player 0's civic centre (place_starting_walls: radius 15,
		// orientation -pi/4), as {x, z, angle}; and of 64 points round the civic centre (32 angles at
		// radius 25 and 35) on ground of class cls, how many share the civic centre's tile region.
		struct Corner { float x, z, a; };
		auto corners_of = [&](float cx, float cz) {
			std::vector<Corner> out;
			const float step = 2.0f * 3.14159265f / 7.0f;
			for (int i = 0; i < 7; ++i) {
				const float a = -0.7853982f - 0.5f * step + (float)i * step;
				out.push_back({cx + 15.0f * det::cos(a), cz + 15.0f * det::sin(a), a});
			}
			return out;
		};
		auto reach = [&](float cx, float cz, int cls, int &valid) {
			float chs = 0.0f;
			for (const auto &b : w.buildings())
				if (b.type == BLD_TOWN_CENTER && b.team == 0) chs = b.half_size;
			uint32_t home = 0;
			for (int a = 0; a < 8 && !home; ++a)
				home = w.nav_grid_.tile_region_at(cx + det::cos(a * 0.785398f) * (chs + 1.5f), cz + det::sin(a * 0.785398f) * (chs + 1.5f), 6, cls);
			int same = 0;
			valid = 0;
			for (float r : {25.0f, 35.0f})
				for (int a = 0; a < 32; ++a) {
					const uint32_t q = w.nav_grid_.tile_region_at(cx + det::cos(a * 0.19634954f) * r, cz + det::sin(a * 0.19634954f) * r, 0, cls);
					if (!q) continue;
					++valid;
					same += q == home;
				}
			return same;
		};
		auto towers_ok = [&](const std::vector<Piece> &laid, float cx, float cz, int &off_steep, int &at_15) {
			off_steep = at_15 = 0;
			for (const Piece &q : laid) {
				if (q.type != BLD_WALL_TOWER) continue;
				off_steep += !w.nav_grid_.near_steep_tile(q.x, q.z, th);
				at_15 += std::fabs(std::hypot(q.x - cx, q.z - cz) - 15.0f) < 0.05f;
			}
		};

		// (a) Acropolis Heights seed 3: main's ring (13 towers, 9 long walls, 4 gates) ran up the cliff
		// by player 0's civic centre and its lowland walls shut the base in. Now no wall or gate, and
		// no tower either: of the seven corners only one is off steep ground, and it is the mouth of
		// the only ramp up to the civic centre's plateau. A tower there leaves the default class its
		// way (a 0.2 clearance passes beside it) but shuts the large one (0.75: rams, elephants,
		// siege) out of the plateau, so place_starting_walls leaves it out (tile_obs_parts_ground).
		gen4(MAP_ACROPOLIS, 3);
		{
			const std::vector<Piece> laid = pieces_of(0);
			float cx = 0.0f, cz = 0.0f;
			cc_of(0, cx, cz);
			int off_steep = 0, at_15 = 0;
			towers_ok(laid, cx, cz, off_steep, at_15);
			const int tw = count(laid, BLD_WALL_TOWER), lw = count(laid, BLD_WALL_SEGMENT), gt = count(laid, BLD_GATE);
			std::printf("  acropolis seed 3, iberian p0: %d towers (%d off steep ground, %d at 15), %d long walls, %d gates\n", tw, off_steep,
					at_15, lw, gt);
			CHECK(lw == 0 && gt == 0);
			CHECK(tw <= 7 && off_steep == tw && at_15 == tw);
			CHECK(tw == 0);
			// The ramp corner: the one corner on the map, above the water and off steep ground.
			std::vector<Corner> open;
			for (const Corner &c : corners_of(cx, cz))
				if (w.map_wall_site_ok(c.x, c.z) && !w.nav_grid_.near_steep_tile(c.x, c.z, th)) open.push_back(c);
			CHECK(open.size() == 1);
			if (open.size() == 1) {
				int v0 = 0, v1 = 0, u0 = 0, u1 = 0;
				const int a0 = reach(cx, cz, 0, v0), a1 = reach(cx, cz, 1, v1);
				std::vector<NavGrid::TileObstruction> before, after;
				w.list_structure_tile_obstructions(before);
				CHECK(w.place_map_wall_piece({BLD_WALL_TOWER, open[0].x, open[0].z, -open[0].a, th}, 0));
				w.rebuild_nav_grid();
				w.list_structure_tile_obstructions(after);
				const NavGrid::TileObstruction *t = nullptr;
				for (const auto &o : after)
					if (std::fabs(o.x - open[0].x) < 1e-4f && std::fabs(o.z - open[0].z) < 1e-4f) t = &o;
				CHECK(t != nullptr && after.size() == before.size() + 1);
				const bool parts0 = t && w.nav_grid_.tile_obs_parts_ground(*t, before, 0);
				const bool parts1 = t && w.nav_grid_.tile_obs_parts_ground(*t, before, 1);
				const int b0 = reach(cx, cz, 0, u0), b1 = reach(cx, cz, 1, u1);
				std::printf("  acropolis seed 3, a tower at the ramp: parts the ground %d (default) %d (large); round the civic centre "
						"default %d/%d -> %d/%d, large %d/%d -> %d/%d\n", (int)parts0, (int)parts1, a0, v0, b0, u0, a1, v1, b1, u1);
				CHECK(!parts0 && parts1);
				CHECK(v0 > 48 && a0 == v0 && b0 == a0 && u0 == v0); // default: all the ground round the plateau, tower or not
				CHECK(v1 > 48 && a1 == v1 && b1 == 0 && u1 == v1);   // large: all of it, and none once the tower stands
			}
		}
		// (a2) Acropolis Heights seed 31: the same corner is the only one off steep ground, but there
		// a tower at it shuts no way, so it stands: one tower, 15 from the civic centre, and all the
		// ground round the plateau in the civic centre's region for both classes.
		gen4(MAP_ACROPOLIS, 31);
		{
			const std::vector<Piece> laid = pieces_of(0);
			float cx = 0.0f, cz = 0.0f;
			cc_of(0, cx, cz);
			int off_steep = 0, at_15 = 0;
			towers_ok(laid, cx, cz, off_steep, at_15);
			const int tw = count(laid, BLD_WALL_TOWER), lw = count(laid, BLD_WALL_SEGMENT), gt = count(laid, BLD_GATE);
			int v0 = 0, v1 = 0;
			const int a0 = reach(cx, cz, 0, v0), a1 = reach(cx, cz, 1, v1);
			std::printf("  acropolis seed 31, iberian p0: %d towers (%d off steep ground, %d at 15), %d long walls, %d gates; round the "
					"civic centre default %d/%d, large %d/%d\n", tw, off_steep, at_15, lw, gt, a0, v0, a1, v1);
			CHECK(lw == 0 && gt == 0);
			CHECK(tw >= 1 && tw <= 7 && off_steep == tw && at_15 == tw);
			CHECK(v0 > 48 && a0 == v0 && v1 > 48 && a1 == v1);
		}

		// (b) Spartan Valley seed 10 (also RTS0's own): no laid piece of its ring comes within a tower's
		// half width of a steep tile, so main's ring stands, piece for piece. "Within" is a box square
		// with the map (near_steep_tile): the corner of its tower at (-116.72, -7.15), turned 2.751,
		// reaches 0.31 past that box, onto a steep tile (0.15 inside it), and no turned tower's
		// corner reaches more than sqrt 2 - 1 of the half width past it.
		static const Piece kSparta10[] = {
			{L, -123.6347f, -21.1182f, 1.97449f}, {T, -121.5577f, -16.2557f, 2.01627f}, {L, -122.0223f, -29.7682f, 0.79853f},
			{T, -125.7117f, -25.9806f, 1.38651f}, {G, -117.3843f, -38.7576f, 1.39042f}, {T, -118.3329f, -33.5558f, 1.09448f},
			{L, -111.1574f, -44.2699f, 0.05877f}, {T, -116.4357f, -43.9593f, 0.72460f}, {L, -100.8839f, -42.8464f, -0.33413f},
			{T, -105.8790f, -44.5805f, -0.13768f}, {G, -91.7074f, -37.8761f, -0.65866f}, {T, -95.8888f, -41.1124f, -0.49640f},
			{L, -86.9988f, -29.3787f, -1.47093f}, {T, -87.5260f, -34.6398f, -1.06480f}, {L, -89.2681f, -19.6300f, -2.12806f},
			{T, -86.4717f, -24.1175f, -1.79949f}, {G, -94.4895f, -10.4439f, -2.04727f}, {T, -92.0644f, -15.1425f, -2.08767f},
			{L, -101.7375f, -7.9128f, 2.71922f}, {T, -96.9146f, -5.7453f, -2.80562f}, {L, -111.6413f, -8.6172f, -2.86122f},
			{T, -106.5603f, -10.0803f, 3.07059f}, {G, -119.1400f, -11.7049f, 2.05914f}, {T, -116.7224f, -7.1541f, 2.75099f},
		};
		gen4(MAP_SPARTAN_VALLEY, 10);
		{
			const std::vector<Piece> laid = pieces_of(0);
			std::printf("  spartan valley seed 10, iberian p0: %zu pieces, ring crosses steep ground %d\n", laid.size(), (int)ring_crosses_steep(laid));
			CHECK(same_pieces(laid, kSparta10, sizeof(kSparta10) / sizeof(kSparta10[0])));
			CHECK(!ring_crosses_steep(laid));
			const Simulation::Building *t = nullptr;
			for (const auto &b : w.buildings())
				if (b.team == 0 && b.type == BLD_WALL_TOWER && std::hypot(b.x + 116.7224f, b.z + 7.1541f) < 1e-3f) t = &b;
			CHECK(t != nullptr);
			if (t) {
				// How far past the box the points of its turned square that lie on steep tiles reach.
				const nav::Vec2 u = nav::footprint_u(*t);
				float past = -1.0f;
				for (float lu = -t->hu; lu <= t->hu + 1e-4f; lu += 0.01f)
					for (float lv = -t->hv; lv <= t->hv + 1e-4f; lv += 0.01f) {
						const float px = t->x + lu * u.x - lv * u.y, pz = t->z + lu * u.y + lv * u.x;
						if (w.nav_grid_.steep_tile_in(px, pz, px, pz))
							past = std::max(past, std::max(std::fabs(px - t->x), std::fabs(pz - t->z)) - th);
					}
				std::printf("  spartan valley seed 10, the tower at (%.2f, %.2f): by steep ground %d (box), its corner %.3f past the box on a "
						"steep tile\n", t->x, t->z, (int)w.nav_grid_.near_steep_tile(t->x, t->z, th), past);
				CHECK(!w.nav_grid_.near_steep_tile(t->x, t->z, th));
				CHECK(past > 0.25f && past <= (std::sqrt(2.0f) - 1.0f) * th + 0.01f);
			}
		}

		// (c) Canyon (map 22, a 0.28 map script) seed 4: the ring crosses steep ground, and 3 of its
		// towers stand by steep tiles, yet it stands whole as on main (13 towers, 9 long walls, 4 gates).
		gen4(MAP_CANYON, 4);
		{
			const std::vector<Piece> laid = pieces_of(0);
			int near = 0;
			for (const Piece &q : laid) near += q.type == BLD_WALL_TOWER && w.nav_grid_.near_steep_tile(q.x, q.z, th);
			const int tw = count(laid, BLD_WALL_TOWER), lw = count(laid, BLD_WALL_SEGMENT), gt = count(laid, BLD_GATE);
			std::printf("  canyon seed 4, iberian p0: %d towers (%d by steep tiles), %d long walls, %d gates, ring crosses steep ground %d\n",
					tw, near, lw, gt, (int)ring_crosses_steep(laid));
			CHECK(tw == 13 && lw == 9 && gt == 4 && near == 3 && ring_crosses_steep(laid));
		}

		// (d) place_generic_fortress on flat ground lays main's pieces where main laid them: the
		// default (unstyled) ring, radius 20, and an Iberian-styled one, as Wall Demo lays it.
		static const Piece kDefault[] = {
			{L, 12.2973f, -17.9412f, -0.73379f}, {T, 8.3705f, -21.4821f, -0.02067f}, {L, 16.2613f, -9.1128f, -1.56375f},
			{T, 16.2240f, -14.4002f, -1.14877f}, {G, 15.2287f, 1.3527f, -1.77454f}, {T, 16.2985f, -3.8255f, -1.66915f},
			{L, 11.3391f, 11.0037f, -2.13327f}, {T, 14.1588f, 6.5308f, -1.95391f}, {L, 3.7636f, 17.7874f, -2.68929f},
			{T, 8.5194f, 15.4766f, -2.41128f}, {G, -5.0925f, 16.7598f, 2.45824f}, {T, -0.9922f, 20.0983f, 3.02607f},
			{L, -14.2388f, 11.8418f, 2.83825f}, {T, -9.1927f, 13.4212f, 2.64824f}, {L, -17.8299f, 5.1790f, 1.29202f},
			{T, -19.2849f, 10.2624f, 2.06513f}, {G, -16.6438f, -5.1850f, 1.62168f}, {T, -16.3749f, 0.0956f, 1.45685f},
			{L, -13.1829f, -14.2135f, 0.78781f}, {T, -16.9127f, -10.4657f, 1.20475f}, {L, -4.4197f, -16.3421f, -0.31124f},
			{T, -9.4531f, -17.9614f, 0.23829f}, {G, 4.4922f, -18.1025f, 0.71679f}, {T, 0.6138f, -14.7229f, 0.19502f},
		};
		static const Piece kStyled[] = {
			{L, 45.9965f, -54.9957f, 2.09496f}, {T, 48.6241f, -50.4505f, 2.32186f}, {L, 42.7295f, -64.7517f, 1.69291f},
			{T, 43.3690f, -59.5408f, 1.89393f}, {G, 46.5398f, -72.7486f, 0.55937f}, {T, 42.0900f, -69.9626f, 1.12614f},
			{L, 56.2324f, -75.8104f, 0.05258f}, {T, 50.9897f, -75.5345f, 0.30597f}, {L, 66.3772f, -74.2068f, -0.36612f},
			{T, 61.4752f, -76.0863f, -0.15677f}, {G, 73.0644f, -67.3902f, -1.22385f}, {T, 71.2793f, -72.3274f, -0.79498f},
			{L, 74.2110f, -57.2420f, -1.69273f}, {T, 74.8496f, -62.4530f, -1.45829f}, {L, 70.6042f, -47.7006f, -2.17169f},
			{T, 73.5724f, -52.0310f, -1.93221f}, {G, 62.4284f, -44.0369f, 3.01428f}, {T, 67.6359f, -43.3703f, -2.72030f},
			{L, 52.9225f, -47.5770f, 2.55232f}, {T, 57.2209f, -44.7035f, 2.78509f},
		};
		{
			w.set_sim_seed(3);
			w.reset();
			w.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
			const size_t n0 = w.buildings().size();
			const size_t a = w.place_generic_fortress(0, 0.0f, 0.0f, 20.0f, 0x5eed1234u);
			const std::vector<Piece> def = pieces_of(0, n0);
			const size_t n1 = w.buildings().size();
			const size_t b = w.place_generic_fortress(0, 60.0f, -60.0f, 18.0f, 77u, 0.5f, 3, 100, WALL_STYLE_IBER_STONE);
			const std::vector<Piece> sty = pieces_of(0, n1);
			std::printf("  generic fortress: %zu pieces (default), %zu (iberian style)\n", a, b);
			CHECK(a == 24 && same_pieces(def, kDefault, sizeof(kDefault) / sizeof(kDefault[0])));
			CHECK(b == 20 && same_pieces(sty, kStyled, sizeof(kStyled) / sizeof(kStyled[0])));
		}

		// (e) Mediterranean Sea (RMS), map 10, the last of RTS0's own maps, seed 10: main's ring (11
		// towers, 8 long walls, 4 gates) comes by steep ground, so the seven towers stand, 15 from the
		// civic centre, off steep ground. They are laid as a ring's pieces are (place_map_wall_piece),
		// so the trees and bushes under them go: a tree and a bush set on each tower's site, and the
		// towers laid again, none is left (spawn_wall_piece left them all). On the generated map one
		// tree is still within 0.75 of a tower's axis: snap_trees_to_tiles, which runs after the
		// walls, moved it onto its tile's centre there, as it can under a ring's pieces (3 under
		// spawn_wall_piece's towers).
		gen4(MAP_RANDOM_MEDITERRANEAN, 10);
		{
			const std::vector<Piece> laid = pieces_of(0);
			float cx = 0.0f, cz = 0.0f;
			cc_of(0, cx, cz);
			int off_steep = 0, at_15 = 0, under = 0;
			towers_ok(laid, cx, cz, off_steep, at_15);
			const auto &res = w.resources_;
			for (const Piece &q : laid) {
				if (q.type != BLD_WALL_TOWER) continue;
				const float c = det::cos(q.rot), sn = -det::sin(q.rot);
				for (size_t i = 0; i < res.count; ++i) {
					if (!res.alive[i] || !(res.type[i] == RES_WOOD || (res.type[i] == RES_FOOD && res.subtype[i] == RES_SUB_NONE))) continue;
					bool on = false;
					for (float u = -th; u <= th + 1e-3f; u += 0.5f)
						on |= det::hypot(res.x[i] - (q.x + u * c), res.z[i] - (q.z + u * sn)) <= 0.75f;
					under += on;
				}
			}
			const int tw = count(laid, BLD_WALL_TOWER), lw = count(laid, BLD_WALL_SEGMENT), gt = count(laid, BLD_GATE);
			std::printf("  mediterranean sea seed 10, iberian p0: %d towers (%d off steep ground, %d at 15), %d long walls, %d gates, "
					"%d trees or bushes under them\n", tw, off_steep, at_15, lw, gt, under);
			CHECK(tw == 7 && lw == 0 && gt == 0 && off_steep == tw && at_15 == tw);
			std::vector<uint32_t> planted;
			for (auto &b : const_cast<std::vector<Simulation::Building> &>(w.buildings()))
				if (b.team == 0 && b.type == BLD_WALL_TOWER && !b.wrecked) {
					planted.push_back(w.spawn_resource_world(b.x, b.z, RES_WOOD, 200));
					planted.push_back(w.spawn_resource_world(b.x + 0.5f, b.z, RES_FOOD, 200, RES_SUB_NONE));
					w.destroy_building(b);
				}
			const size_t again = w.place_starting_walls(0, cx, cz, 256, Simulation::STARTING_WALLS_TOWERS, -0.7853982f, 0u, true);
			int left = 0;
			for (uint32_t id : planted) {
				auto it = w.res_id_to_idx_.find(id);
				left += it != w.res_id_to_idx_.end() && res.alive[it->second];
			}
			std::printf("  laid again over a tree and a bush each: %zu towers, %d of %zu trees and bushes left\n", again, left, planted.size());
			CHECK(planted.size() == 14 && again == 7 && left == 0);
		}
	}

	// 12. fortress.js's "Spahbod" (placeCustomFortress): a closed square of WallSet pieces round
	// each Civic Centre, its first gate towards the map centre, the four side buildings inside.
	{
		s.reset();
		s.generate_random_map(MAP_FORTRESS, 5, 2, 128.0f);
		float cx = 0.0f, cz = 0.0f;
		for (const auto &b : s.buildings())
			if (b.type == BLD_TOWN_CENTER && b.team == 0) cx = b.x, cz = b.z;
		int longs = 0, gates = 0, towers = 0, inside = 0;
		float side = 0.0f, gate_dot = -1.0f;
		for (const auto &b : s.buildings()) {
			if (b.team != 0) continue;
			const float dx = b.x - cx, dz = b.z - cz, r = std::hypot(dx, dz);
			if (b.type == BLD_WALL_SEGMENT || b.type == BLD_GATE) {
				longs += b.type == BLD_WALL_SEGMENT;
				gates += b.type == BLD_GATE;
				CHECK(b.half_size == 0.5f * WALL_SEGMENT_LENGTHS[0] && b.build_progress >= 1.0f);
				side = std::max(side, r);
				// A gate faces the map centre (or away from it).
				if (b.type == BLD_GATE) gate_dot = std::max(gate_dot, std::fabs(-(dx * cx + dz * cz) / (r * std::hypot(cx, cz))));
			}
			towers += b.type == BLD_WALL_TOWER;
			if (b.type == BLD_BARRACKS || b.type == BLD_STABLE || b.type == BLD_TEMPLE || b.type == BLD_MARKET)
				inside += r < 14.0f;
		}
		std::printf("  fortress spahbod: %d long walls, %d gates, %d towers, %d side buildings inside, walls to %.1f\n",
				longs, gates, towers, inside, side);
		CHECK(longs == 10 && gates == 2 && towers == 12 && inside == 4 && side < 20.0f && gate_dot > 0.99f);
	}

	// 13. Ported maps lay their walls, palisades and gates as turned WallSet pieces
	// (port_0ad_maps.py wall_piece): a piece runs along its Position angle, (cos a, -sin a).
	{
		setup(s, 0.0f);
		const float a = 0.6f;
		const uint32_t wid = s.spawn_map_wall_piece(0, WALLSET_STONE, Simulation::MAP_WALL_SEGMENT, 0.0f, 30.0f, a, 9.0f);
		const auto *w = find_bld(s, wid);
		CHECK(w && w->type == BLD_WALL_SEGMENT && w->half_size == 4.5f && w->build_progress >= 1.0f);
		// Its footprint's long axis is the wall's run.
		const bool along = w && std::fabs(w->ux - std::cos(a)) < 1e-3f && std::fabs(w->uz + std::sin(a)) < 1e-3f && w->hu == 4.5f;
		CHECK(along);
		const uint32_t gid = s.spawn_map_wall_piece(GAIA_PLAYER, WALLSET_STONE, Simulation::MAP_WALL_GATE, 20.0f, -30.0f, 0.0f, 9.0f);
		const auto *g = find_bld(s, gid);
		CHECK(g && g->type == BLD_GATE && !s.can_capture_building(0, *g));
		const auto *pt = find_bld(s, s.spawn_map_wall_piece(0, WALLSET_PALISADE, Simulation::MAP_WALL_TOWER, -20.0f, -30.0f, 1.0f, 1.0f));
		CHECK(pt && is_palisade_tower(pt->type, pt->half_size));
		const auto *sg = find_bld(s, s.spawn_map_wall_piece(0, WALLSET_SIEGE, Simulation::MAP_WALL_GATE, -30.0f, 20.0f, 0.0f, 9.0f));
		CHECK(sg && sg->type == BLD_SIEGE_WALL_GATE);
		std::printf("  map wall piece at 0.6 rad: runs along (%.2f, %.2f)\n", w ? w->ux : 0.0f, w ? w->uz : 0.0f);
	}

	// 13b. palisades_spike_* have <TerritoryDecay disable=""/>, palisades_fort <Capturable
	// disable=""/> (port_0ad_maps.py no_decay, no_capture): out of territory a plain palisade
	// decays to Gaia, a spike stays its owner's, through save/load too.
	{
		setup(s, 0.0f);
		const float far = s.map_half() * 0.7f;
		const uint32_t plain = s.spawn_map_wall_piece(0, WALLSET_PALISADE, Simulation::MAP_WALL_SEGMENT, far, far, 0.0f, 3.0f);
		const uint32_t spike = s.spawn_map_wall_piece(0, WALLSET_PALISADE, Simulation::MAP_WALL_SEGMENT, -far, far, 0.0f, 3.0f);
		const uint32_t fort = s.spawn_map_wall_piece(0, WALLSET_PALISADE, Simulation::MAP_WALL_TOWER, far, -far, 0.0f, 2.5f);
		s.set_structure_no_decay(spike);
		s.set_structure_uncapturable(fort);
		for (int i = 0; i < 20 * 60; ++i) s.tick(Simulation::FIXED_STEP);
		const auto *p = find_bld(s, plain), *k = find_bld(s, spike), *f = find_bld(s, fort);
		CHECK(p && p->team == GAIA_PLAYER);
		CHECK(k && k->team == 0 && k->decay_disabled && k->territory_decay_rate == 0.0f);
		CHECK(f && f->team == 0 && f->max_capture_points == 0.0f && !s.can_capture_building(1, *f));
		const char *path = "/tmp/rtso_test_walls_decay.sav";
		CHECK(s.save_state(path));
		static Simulation l;
		CHECK(l.load_state(path));
		std::remove(path);
		const auto *lk = find_bld(l, spike), *lp = find_bld(l, plain);
		CHECK(lk && lk->decay_disabled && lk->territory_decay_rate == 0.0f);
		CHECK(lp && !lp->decay_disabled && lp->territory_decay_rate > 0.0f);
		std::printf("  out of territory for 60 s: plain palisade owner %u, spike owner %u, fort capture points %.0f\n",
		            p ? p->team : 255, k ? k->team : 255, f ? f->max_capture_points : -1.0f);
	}

	// 14. A wall piece tests its own rectangle against each tree's 1.5 m obstruction (0 A.D.
	// CheckFoundation), not the 8 m cells it touches: a tree beside the wall leaves it free, one on
	// it blocks it, and so does a turned wall's segment only where it really crosses the tree.
	{
		setup(s, 1000.0f);
		const float z = 13.0f; // a cell's middle row (cells are 2 units): the tree below shares the wall's cells
		const uint32_t beside = s.spawn_resource_world(8.0f, z + 0.625f + 0.375f + 0.05f, RES_WOOD, 200);
		auto p = s.wall_placement(0, -12.0f, z, 12.0f, z);
		bool all = !p.empty();
		for (const auto &q : p) all = all && s.check_wall_piece(0, q) == Simulation::PLACE_OK;
		CHECK(all);
		const uint32_t on = s.spawn_resource_world(-4.0f, z + 0.3f, RES_WOOD, 200);
		p = s.wall_placement(0, -12.0f, z, 12.0f, z);
		int blocked = 0;
		for (const auto &q : p) blocked += s.check_wall_piece(0, q) != Simulation::PLACE_OK;
		CHECK(blocked >= 1);
		CHECK(beside != 0 && on != 0);
		// A diagonal wall clear of the tree at (-4, z + 0.3), though its bounds hold it.
		p = s.wall_placement(0, -16.0f, z - 8.0f, -2.0f, z + 6.0f);
		all = !p.empty();
		for (const auto &q : p) all = all && s.check_wall_piece(0, q) == Simulation::PLACE_OK;
		CHECK(all);
		// A whole wall laid beside a house that shares its cells: every piece goes down, the towers
		// between segments included (cmd_build_wall tests each against the pieces laid before it).
		setup(s, 1000.0f);
		const uint32_t house = s.spawn_building_world(2.0f, z + 0.625f + 1.25f + 0.05f, BLD_HOUSE, 0, 1.0f);
		const auto plan = s.wall_placement(0, -12.0f, z, 12.0f, z);
		CHECK(house != 0 && s.cmd_build_wall(0, -12.0f, z, 12.0f, z, nullptr, 0, false) == plan.size());
	}

	// 15. Each civ lays its own WallSet (structures/{civ}/wall_tower WallPiece/Length, wallset_stone
	// overlaps, wall_* lengths and Static depth): Roman 10 m towers reaching 0.10-0.80 into 8 m deep
	// segments, kept through save/load; Ptolemaic 38 m long segments, which upgrade to gates.
	{
		setup(s, 1000.0f);
		s.set_team_civ(0, CIV_ROMANS);
		const WallSetInfo rome = wallset_info(WALLSET_STONE, CIV_ROMANS);
		CHECK(rome.tower_length == 2.5f && rome.min_overlap == 0.10f && rome.max_overlap == 0.80f && rome.half_depth == 1.0f);
		bool all_ok = true;
		for (float d = rome.tower_length + 0.1f; d < 60.0f; d += 0.37f) {
			const auto p = s.wall_placement(0, -d * 0.5f, 10.0f, d * 0.5f, 10.0f);
			bool ok = p.size() >= 3 && p.size() % 2 == 1;
			for (size_t i = 1; ok && i + 1 < p.size(); i += 2) {
				const auto &seg = p[i];
				const float tl = rome.tower_length;
				const float reach_l = (seg.half_w - (seg.x - p[i - 1].x) + 0.5f * tl) / tl;
				const float reach_r = (seg.half_w - (p[i + 1].x - seg.x) + 0.5f * tl) / tl;
				for (float r : {reach_l, reach_r}) ok = ok && r >= 0.10f - 1e-3f && r <= 0.80f + 1e-3f;
			}
			all_ok = all_ok && ok;
		}
		CHECK(all_ok);
		const size_t laid = s.cmd_build_wall(0, -10.0f, 10.0f, 10.0f, 10.0f);
		int deep = 0;
		for (const auto &bld : s.buildings()) deep += bld.type == BLD_WALL_SEGMENT && bld.half_d == 1.0f && bld.civ == CIV_ROMANS;
		const char *path = "/tmp/rtso_test_walls_civ.sav";
		CHECK(s.save_state(path));
		static Simulation l;
		CHECK(l.load_state(path));
		int deep_loaded = 0;
		for (const auto &bld : l.buildings()) deep_loaded += bld.type == BLD_WALL_SEGMENT && bld.half_d == 1.0f;
		std::remove(path);
		std::printf("  roman wall: %zu pieces, %d segments 8 m deep (%d after load)\n", laid, deep, deep_loaded);
		CHECK(laid >= 3 && deep >= 1 && deep_loaded == deep);

		setup(s, 1000.0f);
		s.set_team_civ(0, CIV_PTOLEMIES);
		const auto pp = s.wall_placement(0, -10.0f, 10.0f, 10.0f, 10.0f);
		CHECK(pp.size() == 5 && pp[1].half_w == 4.75f && pp[3].half_w == 4.75f);
		const float stone0 = s.player(0).res[RES_STONE];
		CHECK(s.cmd_build_wall(0, -10.0f, 10.0f, 10.0f, 10.0f) == 5);
		std::printf("  ptolemaic wall: long segments %.2f units, %.0f stone\n", pp[1].half_w * 2.0f, stone0 - s.player(0).res[RES_STONE]);
		uint32_t seg = 0;
		for (auto &bld : s.buildings_)
			if (bld.type == BLD_WALL_SEGMENT) { bld.build_progress = 1.0f; bld.hp = bld.max_hp; seg = bld.id; }
		CHECK(s.upgrade_check(0, seg) == Simulation::UPGRADE_OK);
		CHECK(std::fabs(wallset_piece_value(BLD_WALL_SEGMENT, 4.75f, 1, CIV_PTOLEMIES) - WALL_LONG_STONE) < 1e-3f);
		// The wall snaps onto a tower within half its WallPiece length (GuiInterface.js), not further.
		const float tx = pp.back().x, tz = pp.back().z, near = 0.5f * wallset_info(WALLSET_STONE, CIV_PTOLEMIES).tower_length;
		CHECK(s.wall_snap_tower(0, tx + near - 0.05f, tz, WALLSET_STONE) != 0);
		CHECK(s.wall_snap_tower(0, tx + near + 0.05f, tz, WALLSET_STONE) == 0);
	}

	// 16. A Gaia gate opens for Gaia's own units (Diplomacy: every player, Gaia included, is its
	// own ally), and still not for a player's.
	{
		setup(s, 0.0f);
		s.spawn_gaia_gate(20.0f, 20.0f, 0.0f, WALLSET_STONE);
		uint32_t gate = 0;
		for (const auto &bld : s.buildings()) if (bld.type == BLD_GATE) gate = bld.id;
		const uint32_t g = s.spawn_gaia_unit(20.0f, 22.0f, UNIT_SPEARMAN, nullptr);
		for (int i = 0; i < 10; ++i) s.tick(Simulation::FIXED_STEP);
		const bool opened = gate && find_bld(s, gate)->gate_open;
		std::printf("  gaia gate: %s for a gaia unit beside it (unit %u)\n", opened ? "open" : "shut", g);
		CHECK(opened);
	}

	// 17. On a circular map a wall piece past the passable edge fails CheckFoundation (the
	// off-world edge mask blocks every class): a palisade dragged out to a corner stops there.
	{
		setup(s, 1000.0f);
		s.player(0).res[RES_WOOD] = 5000.0f;
		s.set_circular_map(true);
		const float a = s.map_half() * 0.55f, b = s.map_half() * 0.95f;
		const auto p = s.wall_placement(0, a, a, b, b, nullptr, nullptr, WALLSET_PALISADE);
		int past = 0, past_ok = 0, inside_ok = 0;
		for (const auto &q : p) {
			const bool edge = s.past_map_edge(q.x, q.z), well_inside = !s.past_map_edge(q.x, q.z, 2.0f * q.half_w);
			const bool ok = s.check_wall_piece(0, q) == Simulation::PLACE_OK;
			past += edge;
			past_ok += edge && ok;
			inside_ok += well_inside && ok;
		}
		std::printf("  circular edge: %zu palisade pieces, %d centred past the edge (%d placeable), %d well inside placeable\n",
				p.size(), past, past_ok, inside_ok);
		CHECK(past > 0 && past_ok == 0 && inside_ok > 0);
	}

	// 18. BuildRestrictions.CheckPlacement: a seated human player can't build in unexplored land,
	// so a wall dragged into the shroud is laid only where the player has explored.
	{
		setup(s, 5000.0f);
		s.set_player_active(0, true, false);
		for (int i = 0; i < 10; ++i) s.tick(Simulation::FIXED_STEP);
		const float far = s.map_half() * 0.8f;
		const int fc = (int)(far / Simulation::GRID);
		const auto near_r = s.check_placement(0, -3, 7, BLD_HOUSE), far_r = s.check_placement(0, fc, fc, BLD_HOUSE);
		const size_t shroud = s.cmd_build_wall(0, far, far, far, far - 12.0f);
		const auto plan = s.wall_placement(0, -10.0f, 10.0f, far, 10.0f);
		const size_t into = s.cmd_build_wall(0, -10.0f, 10.0f, far, 10.0f);
		std::printf("  unexplored: house near %d far %d; wall in the shroud %zu pieces, into it %zu of %zu\n", (int)near_r,
				(int)far_r, shroud, into, plan.size());
		CHECK(near_r != Simulation::PLACE_UNEXPLORED && far_r == Simulation::PLACE_UNEXPLORED);
		CHECK(shroud == 0 && into >= 3 && into < plan.size());
		bool all_seen = true;
		for (const auto &bld : s.buildings())
			if (bld.type == BLD_WALL_TOWER || bld.type == BLD_WALL_SEGMENT) all_seen &= s.los_explored(0, bld.x, bld.z);
		CHECK(all_seen);
		// AI players have no visibility info in 0.28: no such check.
		s.set_player_active(0, true, true);
		CHECK(s.check_placement(0, fc, fc, BLD_HOUSE) != Simulation::PLACE_UNEXPLORED);
	}

	// 19. tryPlaceWall sends nothing when its first segment is blocked: no lone start tower is
	// laid or paid for. Short of stone is another matter (below).
	{
		setup(s, 1000.0f);
		const auto plan = s.wall_placement(0, -10.0f, 10.0f, 10.0f, 10.0f);
		CHECK(plan.size() >= 3 && plan[1].type == BLD_WALL_SEGMENT);
		s.spawn_building_world(plan[1].x, plan[1].z, BLD_HOUSE, 0, 1.0f);
		const size_t laid = s.cmd_build_wall(0, -10.0f, 10.0f, 10.0f, 10.0f);
		std::printf("  blocked first segment: %zu pieces, %d towers\n", laid, count_type(s, BLD_WALL_TOWER));
		CHECK(laid == 0 && count_type(s, BLD_WALL_TOWER) == 0 && s.player(0).res[RES_STONE] == 1000.0f);
		// Only enough stone for the start tower: SetWallPlacementPreview sends every placeable piece,
		// affordable or not, and TryConstructWall lays and pays for the tower, then stops at the
		// segment it can't pay for.
		setup(s, 60.0f);
		s.take_session_notices();
		const size_t lone = s.cmd_build_wall(0, -10.0f, 10.0f, 10.0f, 10.0f);
		std::printf("  stone for the start tower only: %zu piece laid, %d towers, %.0f stone left\n", lone, count_type(s, BLD_WALL_TOWER),
				s.player(0).res[RES_STONE]);
		CHECK(lone == 1 && count_type(s, BLD_WALL_TOWER) == 1 && count_type(s, BLD_WALL_SEGMENT) == 0 && s.player(0).res[RES_STONE] < 60.0f);
		// Player.js SubtractResourcesOrNotify: "Insufficient resources" with the stone still needed.
		bool told = false;
		for (const Simulation::SessionNotice &n : s.take_session_notices())
			told |= n.kind == Simulation::SessionNotice::TEXT && n.tech == Simulation::SessionNotice::TEXT_INSUFFICIENT_RESOURCES &&
					n.player == 0 && n.amounts[RES_STONE] > 0;
		CHECK(told);
	}

	// 20. 0.28 Builder lists hold only the WallSets: a wall tower or gate is never a lone foundation.
	{
		setup(s, 1000.0f);
		s.player(0).res[RES_WOOD] = 1000.0f;
		CHECK(s.cmd_build(0, -3, 7, BLD_WALL_TOWER) == 0 && s.cmd_build(0, -3, 7, BLD_GATE) == 0 &&
				s.cmd_build(0, -3, 7, BLD_WALL_SEGMENT) == 0 && s.cmd_build(0, -3, 7, BLD_PALISADE) == 0);
		CHECK(s.cmd_build(0, -3, 7, BLD_HOUSE) != 0);
	}

	// 21. RETURNRESOURCE.APPROACHING (UnitAI.js): a returner walks to the dropsite within range
	// (MoveToTargetRange), from the side it comes from. An Iberian woman inside her starting walls
	// on Continental (seed 19) takes wood to a dock on the shore past a gate: the walk to the
	// dock's centre would end on its water side, beyond the gate's Right part, and she pushed on
	// at the wall for good; she goes out through the door and drops the wood at the dock.
	{
		s.reset();
		s.set_sim_seed(19);
		s.set_waves_enabled(false);
		const uint8_t civs[4] = {CIV_IBERIANS, CIV_MAURYANS, CIV_KUSHITES, CIV_PTOLEMIES};
		for (uint8_t p = 0; p < 4; ++p) s.set_team_civ(p, civs[p]);
		s.generate_random_map(MAP_RANDOM_CONTINENTAL, 19, 4, 128.0f);
		for (uint8_t p = 0; p < 4; ++p) s.set_team_civ(p, civs[p]);
		for (uint8_t p = 0; p < MAX_PLAYERS; ++p) s.set_player_active(p, p < 4, false);
		// Continental is one of RTS0's own maps, where a laid piece of player 0's ring on this seed
		// comes within a tower's half width of a steep tile, so the seven towers stand instead (11b,
		// G97). Take them away and lay the ring map generation laid before, from the seed it drew for
		// player 0.
		{
			float cx = 0.0f, cz = 0.0f;
			for (auto &b : const_cast<std::vector<Simulation::Building> &>(s.buildings())) {
				if (b.team == 0 && b.type == BLD_TOWN_CENTER) cx = b.x, cz = b.z;
				if (b.team == 0 && b.type == BLD_WALL_TOWER && !b.wrecked) s.destroy_building(b);
			}
			CHECK(s.place_starting_walls(0, cx, cz, 256, Simulation::STARTING_WALLS_FORTRESS, -0.7853982f, 3261542810u) == 19);
		}
		const Simulation::Building *gate = nullptr;
		for (const auto &b : s.buildings())
			if (b.team == 0 && b.type == BLD_GATE && std::hypot(b.x - 36.75f, b.z - 39.0f) < 1.0f) gate = &b;
		// The layout comes from the map script: if Continental or the Iberian walls change, find the
		// new gate beside a shore and move the dock and the woman with it.
		if (!gate) std::printf("  Continental seed 19 has no Iberian gate at (36.75, 39.0) any more\n");
		CHECK(gate != nullptr);
		const uint32_t dock = s.spawn_building_world(34.0f, 36.0f, BLD_DOCK, 0, 1.0f, 0.785398f);
		const uint32_t woman = s.spawn_unit(49.25f, 30.65f, 0, UNIT_FEMALE_CITIZEN);
		auto at = [&](uint32_t id) {
			auto uv = s.units();
			for (size_t i = 0; i < uv.count; ++i) if (uv.id[i] == id) return (int32_t)i;
			return (int32_t)-1;
		};
		const int32_t w = at(woman);
		CHECK(dock != 0 && w >= 0);
		if (dock != 0 && w >= 0) {
			s.units_.res_type_carried[w] = RES_WOOD;
			s.units_.carry[w] = 10;
			const float wood0 = s.player_resource(0, RES_WOOD);
			s.order_return_resource(&woman, 1, dock);
			for (int t = 0; t < (int)(40.0f / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
			const int32_t i = at(woman);
			const float got = s.player_resource(0, RES_WOOD) - wood0;
			// Delivered at the dock itself, not at another dropsite after giving it up.
			const Simulation::Building *db = nullptr;
			for (const auto &b : s.buildings()) if (b.id == dock) db = &b;
			const float gap = db && i >= 0 ? nav::footprint_gap(*db, s.units_.pos_x[i], s.units_.pos_z[i]).length() : 99.0f;
			std::printf("  returner to a dock past an Iberian gate: %.0f wood delivered, woman at (%.2f, %.2f), %.2f from the dock\n", got,
					i >= 0 ? s.units_.pos_x[i] : NAN, i >= 0 ? s.units_.pos_z[i] : NAN, gap);
			CHECK(got == 10.0f && gap < 2.5f); // tick_unit_ai drops within 1.5 of the footprint, plus her body
		}
	}

	// 22. A destroyed structure leaves rubble that blocks nothing (0.28's template_rubble.xml has no
	// Obstruction), in the nav grid too: a breach in a wall opens the long paths through it, and
	// a save keeps it open.
	setup(s, 0.0f);
	{
		auto obstructed = [](const Simulation &sim) {
			size_t n = 0;
			for (uint8_t v : sim.nav_grid().cells.data) n += v == NAV_OBSTRUCTED;
			return n;
		};
		const size_t bare = obstructed(s);
		const uint32_t house = s.spawn_building_world(-25.0f, -25.0f, BLD_HOUSE, 0, 1.0f);
		const size_t with_house = obstructed(s);
		for (auto &b : s.buildings_)
			if (b.id == house) { s.destroy_building(b); break; }
		std::printf("  obstructed navcells: %zu bare, %zu with a house, %zu once it is destroyed\n", bare, with_house, obstructed(s));
		CHECK(with_house > bare && obstructed(s) == bare);

		// A 60-unit (240 m) wall across x = 25 with a middle segment destroyed: a spearman 10 units
		// from it walks through the breach, not round the wall's end. (Test 21 left a 128 map and
		// Iberian walls; the wall and the walk fit either.)
		s.begin_bulk_spawn();
		const auto pcs = s.wall_placement(0, 25.0f, -30.0f, 25.0f, 30.0f);
		std::vector<uint32_t> ids;
		for (const auto &p : pcs) ids.push_back(s.spawn_wall_piece(p, 0, 1.0f));
		s.end_bulk_spawn();
		CHECK(pcs.size() >= 3);
		size_t mid = 0; // pcs[0] is the tower at z = -30
		for (size_t k = 0; k < pcs.size(); ++k)
			if (pcs[k].type == BLD_WALL_SEGMENT && std::fabs(pcs[k].z) < std::fabs(pcs[mid].z)) mid = k;
		CHECK(mid < pcs.size() && pcs[mid].type == BLD_WALL_SEGMENT);
		const float gz = mid < pcs.size() ? pcs[mid].z : 0.0f;
		for (auto &b : s.buildings_)
			if (mid < ids.size() && b.id == ids[mid]) { s.destroy_building(b); break; }
		const uint32_t spear = s.spawn_unit(15.0f, gz, 0, UNIT_SPEARMAN);
		s.cmd_move(&spear, 1, 35.0f, gz);
		float crossed = -1.0f;
		for (int k = 0; k < (int)(20.0f / Simulation::FIXED_STEP) && crossed < 0.0f; ++k) {
			s.tick(Simulation::FIXED_STEP);
			const auto uv = s.units();
			for (size_t i = 0; i < uv.count; ++i)
				if (uv.id[i] == spear && std::hypot(uv.pos_x[i] - 35.0f, uv.pos_z[i] - gz) < 1.0f) crossed = (float)(k + 1) * Simulation::FIXED_STEP;
		}
		std::printf("  spearman across a 240 m wall through a destroyed segment: %.1f s (-1: not there in 20 s)\n", crossed);
		CHECK(crossed > 0.0f && crossed < 8.0f); // straight through is about 6 s; round the wall's end about 18 s

		const char *path = "/tmp/rtso_test_walls_wreck.sav";
		CHECK(s.save_state(path));
		static Simulation l;
		CHECK(l.load_state(path));
		std::remove(path);
		const auto *lw = mid < ids.size() ? find_bld(l, ids[mid]) : nullptr;
		CHECK(lw && lw->wrecked && obstructed(l) == obstructed(s));
	}

	// 23. Large units and narrow gaps. 0.28 CheckMovement grows each structure's square by the
	// walker's class Clearance and refuses a move into one from outside it (CCmpObstructionManager.cpp:
	// 918-932, Geometry.cpp:259-305), in a step (CCmpUnitMotion.h:1302, :1343) and in a push
	// (CCmpUnitMotion_System.cpp:628-641). So a ram or an elephant (the "large" class, 3 m) gets
	// through no gap of 6 m or less, and neither pathfinder finds a way through one either. A wall
	// across x = 25 with a gap in it, between two wall pieces or between a wall's end and a house
	// turned 45 degrees (the measured gap is to its corner): a ram and an elephant sent 3 or 10 units
	// across, the gap on their line, stay on their side of it. Through an 8 m gap both get across,
	// and so does a spearman (0.8 m) through a 2 m gap between wall pieces and a 3 m one beside the
	// house (at 2 m the house's corner, grown by 0.8 m, reaches 0.13 m past the line, which 0.28
	// refuses, and the wall's grown end leaves 7 cm between them).
	{
		constexpr float X = 25.0f;
		// The wall along x = X, pieces from z_from away in direction dir to the map's edge.
		auto wall_from = [&](float z_from, float dir) {
			for (float z = z_from + dir * 4.5f; std::fabs(z) < s.map_half() - 4.0f; z += dir * 8.5f)
				s.spawn_map_wall_piece(0, WALLSET_STONE, Simulation::MAP_WALL_SEGMENT, X, z, 1.5707963f, 9.0f);
		};
		// Sends the unit across the gap centred on z0; returns the furthest east it got within 3 units
		// of the gap in 40 s (one that goes round the wall's end, far up the map, may get across), and
		// whether it got within 1 of its goal.
		auto cross = [&](bool house, float gap_m, float z0, UnitType type, float d, bool &arrived) {
			setup(s, 0.0f);
			const float g = gap_m / 4.0f;
			s.begin_bulk_spawn();
			wall_from(z0 - 0.5f * g, -1.0f);
			if (house) {
				const float zb = z0 + 0.5f * g + 1.41421356f; // its lowest corner g above the wall's end
				s.spawn_building_world(X, zb, BLD_HOUSE, 0, 1.0f, 0.78539816f);
				wall_from(zb, 1.0f);
			} else {
				wall_from(z0 + 0.5f * g, 1.0f);
			}
			s.end_bulk_spawn();
			s.spawn_unit(-40.0f, 40.0f, 0, UNIT_SPEARMAN); // separation() pushes off structures with two units or more
			const uint32_t u = s.spawn_unit(X - d, z0, 0, type);
			s.order_set_stance(&u, 1, STANCE_PASSIVE);
			s.cmd_move(&u, 1, X + d, z0);
			float east = -1e9f;
			arrived = false;
			for (int k = 0; k < (int)(40.0f / Simulation::FIXED_STEP) && !arrived; ++k) {
				s.tick(Simulation::FIXED_STEP);
				const int32_t i = s.lookup_unit(u);
				if (i < 0) break;
				if (std::fabs(s.units_.pos_z[i] - z0) < 3.0f) east = std::max(east, s.units_.pos_x[i]);
				arrived = std::hypot(s.units_.pos_x[i] - (X + d), s.units_.pos_z[i] - z0) < 1.0f;
			}
			return east;
		};
		for (const bool house : {false, true})
			for (int gap_m = 1; gap_m <= 6; ++gap_m) {
				int crossed = 0, runs = 0;
				for (const UnitType type : {UNIT_SIEGE_RAM, UNIT_ELEPHANT})
					for (const float z0 : {0.0f, 1.0f})
						for (const float d : {3.0f, 10.0f}) {
							bool arrived;
							const float east = cross(house, (float)gap_m, z0, type, d, arrived);
							++runs;
							if (east >= X) {
								++crossed;
								std::printf("  %s through a %d m gap (z %.1f, from %.0f units off) got to x = %.2f\n",
										type == UNIT_SIEGE_RAM ? "ram" : "elephant", gap_m, z0, d, east);
							}
						}
				std::printf("  %s, %d m gap: %d of %d ram and elephant walks crossed\n", house ? "wall and turned house" : "two wall pieces",
						gap_m, crossed, runs);
				CHECK(crossed == 0);
			}
		int through = 0, tries = 0;
		for (const bool house : {false, true})
			for (const float z0 : {0.0f, 1.0f})
				for (const UnitType type : {UNIT_SIEGE_RAM, UNIT_ELEPHANT, UNIT_SPEARMAN}) {
					const float gap_m = type != UNIT_SPEARMAN ? 8.0f : house ? 3.0f : 2.0f;
					bool arrived;
					const float east = cross(house, gap_m, z0, type, 3.0f, arrived);
					through += arrived;
					++tries;
					if (!arrived)
						std::printf("  %s through a %.0f m gap (z %.1f) did not get across, at most x = %.2f by it\n",
								type == UNIT_SIEGE_RAM ? "ram" : type == UNIT_ELEPHANT ? "elephant" : "spearman", gap_m, z0, east);
				}
		std::printf("  rams and elephants through 8 m gaps, spearmen through 2 and 3 m ones: %d of %d got across\n", through, tries);
		CHECK(through == tries);
	}

	// BuildRestrictions.js Category "Wall": CheckFoundation(passClassName, true) tests terrain at
	// the centre point only. A long segment whose end hangs over deep water still goes down; one
	// centred on the water does not.
	{
		setup(s, 1000.0f);
		for (int k = 0; k < 10; ++k) s.tick(Simulation::FIXED_STEP);
		const float half = 4.5f;
		auto [cx, cy] = s.nav_grid_.world_to_cell(0.0f, 14.0f);
		auto [ex, ey] = s.nav_grid_.world_to_cell(0.0f + half - 0.5f, 14.0f);
		for (int y = cy - 2; y <= cy + 2; ++y)
			for (int x = cx + 1; x <= ex + 2; ++x) s.nav_grid_.cells.at(x, y) = NAV_WATER; // east of the centre
		const Simulation::WallPiece over{BLD_WALL_SEGMENT, 0.0f, 14.0f, 0.0f, half, s.player_civ(0)};
		const Simulation::WallPiece on{BLD_WALL_SEGMENT, 0.0f + half - 0.5f, 14.0f, 0.0f, half, s.player_civ(0)};
		const auto r_over = s.check_wall_piece(0, over), r_on = s.check_wall_piece(0, on);
		std::printf("  wall segment with its end over water: %d (centre on land), %d (centre on water)\n", (int)r_over, (int)r_on);
		CHECK(r_over == Simulation::PLACE_OK && r_on == Simulation::PLACE_TERRAIN);
	}

	// input.js PickSimilarPlayerEntities(wallSet.templates.tower, ..., exact template): a captured
	// tower of another civ is no snap target for the player's walls.
	{
		setup(s, 1000.0f);
		const uint32_t own = s.spawn_building_world(-14.0f, -14.0f, BLD_WALL_TOWER, 0, 1.0f);
		const uint32_t foreign = s.spawn_building_world(14.0f, -14.0f, BLD_WALL_TOWER, 0, 1.0f);
		for (auto &b : const_cast<std::vector<Simulation::Building> &>(s.buildings()))
			if (b.id == foreign) b.civ = s.player_civ(0) == CIV_SPARTANS ? CIV_ATHENIANS : CIV_SPARTANS;
		const uint32_t to_own = s.wall_snap_tower(0, -14.2f, -14.0f, WALLSET_STONE);
		const uint32_t to_foreign = s.wall_snap_tower(0, 14.2f, -14.0f, WALLSET_STONE);
		std::printf("  snap onto own tower %s, onto a captured foreign tower %s\n", to_own == own ? "yes" : "no", to_foreign ? "yes" : "no");
		CHECK(to_own == own && to_foreign == 0);
	}

	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
