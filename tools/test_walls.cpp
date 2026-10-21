// 0 A.D. WallSet walls (helpers/Walls.js GetWallPlacement, Commands.js TryConstructWall): a wall
// is laid as towers joined by long, medium and short segments that reach into the towers within
// the overlap limits; segments take their length's share of wall_long's cost and health, block
// the cells under their turned footprint, keep them through save/load, and only a long one
// upgrades to a gate. A wall snaps onto the player's towers and stops at the first piece it can't
// pay for. Palisades are a WallSet of their own (wallset_palisade), and a long one upgrades to a
// palisade gate; so are the Roman siege walls (rome/wallset_siege), which go in enemy land too,
// and their long segments become siege wall gates. An Iberian base starts walled on large maps.
// A destroyed piece leaves rubble that blocks nothing, so a breach opens the long paths.
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
	// laid or paid for.
	{
		setup(s, 1000.0f);
		const auto plan = s.wall_placement(0, -10.0f, 10.0f, 10.0f, 10.0f);
		CHECK(plan.size() >= 3 && plan[1].type == BLD_WALL_SEGMENT);
		s.spawn_building_world(plan[1].x, plan[1].z, BLD_HOUSE, 0, 1.0f);
		const size_t laid = s.cmd_build_wall(0, -10.0f, 10.0f, 10.0f, 10.0f);
		std::printf("  blocked first segment: %zu pieces, %d towers\n", laid, count_type(s, BLD_WALL_TOWER));
		CHECK(laid == 0 && count_type(s, BLD_WALL_TOWER) == 0 && s.player(0).res[RES_STONE] == 1000.0f);
		// Only enough stone for the start tower: nothing either.
		setup(s, 60.0f);
		s.take_session_notices();
		CHECK(s.cmd_build_wall(0, -10.0f, 10.0f, 10.0f, 10.0f) == 0 && s.player(0).res[RES_STONE] == 60.0f);
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

	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
