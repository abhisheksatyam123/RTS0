// RTS0 building steering test (headless: no SCons, no Godot).
// Checks that land units get around buildings with the short-path steering in move_units
// (src/simulation/nav/building_steering.h, which uses the VertexPathfinder port).
// Build and run:
//   g++ -std=c++20 -O2 -Isrc tools/test_building_steering.cpp src/simulation/simulation.cpp \
//     src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//     src/network/net_host.cpp -lpthread -o /tmp/test_steer && /tmp/test_steer
//
//  1. Guard behind a wall: a guard order moves straight at its target (no long path). The target
//     stands behind a wall of segments, so the guard has to walk around the wall's end.
//  2. Gather around houses: a gather order also moves straight at the wood pile, and a row of
//     houses stands between the villagers and the pile.
//  3. Move through a gap: a move order along a long path through a gap in a row of barracks.
//  4. Gather behind a long wall: the wall is too long for the short-path range, so the gatherer
//     needs the long path move_units requests when buildings block a GATHER/BUILD/RETURN_DROPSITE
//     unit's straight line.
//  5. Walk across fields: fields don't block movement (0 A.D. BlockMovement=false), so a unit
//     walks straight over a row of them instead of around.
//  6. Out of a house cluster: a catapult among houses, whose long path starts on navcells in
//     2-unit gaps it can't stand in, moves on past those waypoints instead of pushing at them for good.
//  7. Walk past a turned barracks: turned 45 degrees, its corners reach 41% further along the axes
//     than an unturned one's sides (nav/footprint.h). A unit walking along that axis must go around
//     the corners, not the unturned square, and a woman finishes a turned house. A long, thin
//     footprint (0 A.D.'s long wall, 36 x 5 m) lies along its width, and a quarter turn swaps its
//     width and depth in every footprint test and in the nav grid.
//  8. Siege through a gap: a catapult keeps 0 A.D.'s "large" clearance (3 m, unit_building_clearance)
//     from buildings, so it takes a 2-unit (8 m) gap in a long row of houses instead of going around.
//  9. Walk into a building: a move to a point inside a town centre ends beside it, as close as the
//     unit can stand (0 A.D. MakeGoalReachable), instead of pushing at its wall for good.
// 10. Walk given again every tick: a long path leaves out the navcell the unit stands in (0 A.D.
//     LongPathfinder), so a unit given a new path every tick, as Petra sends its scout home, still
//     gets there instead of heading back to its navcell's centre each time.
// 11. Walk to one point: twelve units each sent alone to the same point stop within 12 m (3 units)
//     of it once the others obstruct them (0 A.D. UnitAI DefaultRelaxedMaxRange), instead of
//     pushing for the point for good.
// 12. Ship sent ashore: a point a ship can't reach gives way to the nearest one it can
//     (MakeGoalReachable), so the ship stops at the shore instead of sailing onto the land.
// 13. Walk into a closed gate: an enemy's long path goes through an unlocked gate, which then
//     stops it dead; the walk ends after 35 turns (7 s) of failed movement (0 A.D. UnitMotion
//     MAX_FAILED_MOVEMENTS), instead of pushing at the gate for good.
// 14. Crowds at the shore: ships packed against the shore and soldiers packed at the water's edge
//     push each other apart, but never onto terrain they can't cross (0 A.D. pushing checks
//     CheckMovement), so no ship runs aground and no soldier is pushed out to sea.
// 15. Cliffs: a cliff steeper than 0 A.D.'s MaxTerrainSlope (pathfinder.xml, 1.0 for every land
//     class) splits the map but for one ramp. A swordsman, a horseman and a gatherer all go round
//     by the ramp instead of climbing the cliff, and no house can be placed on it.
// 16. Chases: an attacker paths round water and cliffs as 0 A.D. UnitMotion paths every move, and
//     gives up a target it can't reach (UnitAI MovementUpdate with likelyFailure) instead of wading
//     across the sea after it or climbing a cliff. A player's attack on a unit or a house across the
//     sea ends at the shore; a soldier's own fight with an enemy across a channel is dropped at the
//     water's edge; a player's attack on a unit up a cliff goes round by the ramp and gets there.
//  For every scenario the unit bodies must never overlap a footprint that blocks movement by more
//  than MAX_PENETRATION (separation() alone allows deep overlap when a unit walks head-on into a wall).
//  Exit: 0 = PASS, 1 = FAIL. Prints the measurements either way.
// Private access only to turn a standing building (Building::set_rot) and rasterize it.
#define private public
#include "simulation/simulation.h"
#undef private

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace rtso;

static constexpr float DT = 0.05f;
static constexpr float MAX_PENETRATION = 0.25f; // world units of body overlap with a footprint

static int g_failures = 0;
static void check(bool ok, const char *what) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", what);
		++g_failures;
	}
}

static void setup(Simulation &s) {
	s.set_sim_seed(4242);
	s.reset();
	// Human seats: Petra would defend its passive targets and walk them home (defenseArmy.js clear).
	for (uint8_t p = 0; p < 2; ++p) s.player(p).active = 1, s.player(p).is_ai = 0;
	// Far-off town centers for both sides keep the match running (tick() stops once it ends).
	s.spawn_building_world(-52.0f, -52.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(52.0f, 52.0f, BLD_TOWN_CENTER, 1, 1.0f);
}

// Deepest overlap of any live team-0 unit's building clearance with any building footprint, turned
// by its rot: its half width along u and half depth along v.
static float max_penetration(const Simulation &s) {
	const Simulation::UnitView u = s.units();
	float worst = 0.0f;
	for (size_t i = 0; i < u.count; ++i) {
		if (!u.alive[i] || u.team[i] != 0) continue;
		const float r = unit_building_clearance(u.unit_type[i]);
		for (const auto &b : s.buildings()) {
			if (b.wrecked || !Simulation::blocks_movement_now(b)) continue; // an uncommitted foundation blocks nothing
			// In the footprint's own axes, u = (cos a, -sin a) and v = (sin a, cos a) (0 A.D.
			// GetObstructionSquare), worked out here rather than taken from nav/footprint.h.
			const float c = std::cos(b.rot), sn = std::sin(b.rot);
			const float dx = u.pos_x[i] - b.x, dz = u.pos_z[i] - b.z;
			const float ex = b.half_size + r - std::fabs(dx * c - dz * sn);
			const float ez = b.half_d + r - std::fabs(dx * sn + dz * c);
			if (ex > 0.0f && ez > 0.0f) worst = std::max(worst, std::min(ex, ez));
		}
	}
	return worst;
}

static int index_of(const Simulation &s, uint32_t id) {
	const Simulation::UnitView u = s.units();
	for (size_t i = 0; i < u.count; ++i)
		if (u.id[i] == id) return static_cast<int>(i);
	return -1;
}

// 1. Guard behind a wall.
static void guard_behind_wall() {
	Simulation s;
	setup(s);
	// Wall segments are 2x2; cells 0 x [-5,4] make a wall at x in [0,2], z in [-10,10].
	for (int cy = -5; cy <= 4; ++cy) s.spawn_building(0, cy, BLD_WALL_SEGMENT, 0, 1.0f);
	const uint32_t target = s.spawn_unit(8.0f, 0.0f, 0, UNIT_SPEARMAN, 150.0f);
	const uint32_t guard = s.spawn_unit(-8.0f, 0.0f, 0, UNIT_SWORDSMAN, 160.0f);
	s.order_guard(&guard, 1, target);
	int arrived = -1;
	float pen = 0.0f;
	for (int t = 0; t < 1200 && arrived < 0; ++t) {
		s.tick(DT);
		pen = std::max(pen, max_penetration(s));
		const int gi = index_of(s, guard), ti = index_of(s, target);
		if (gi < 0 || ti < 0) break;
		const Simulation::UnitView u = s.units();
		const float dx = u.pos_x[gi] - u.pos_x[ti], dz = u.pos_z[gi] - u.pos_z[ti];
		if (dx * dx + dz * dz <= 3.2f * 3.2f) arrived = t;
	}
	std::printf("  guard behind wall: arrived after %d ticks (-1 = never in 1200), max penetration %.3f\n", arrived, pen);
	check(arrived >= 0, "guard never reached its target behind the wall");
	check(pen <= MAX_PENETRATION, "guard overlapped a wall segment");
}

// 2. Gather around houses.
static void gather_around_houses() {
	Simulation s;
	setup(s);
	s.spawn_building_world(-24.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
	// Houses are 2x2; a touching row at x = -13, z in [-7, 7].
	for (int k = -3; k <= 3; ++k) s.spawn_building_world(-13.0f, 2.0f * k, BLD_HOUSE, 0, 1.0f);
	const uint32_t wood = s.spawn_resource_world(-4.0f, 0.0f, RES_WOOD, 5000);
	std::vector<uint32_t> vills;
	for (int k = 0; k < 3; ++k) vills.push_back(s.spawn_unit(-19.0f, -1.5f + 1.5f * k, 0, UNIT_VILLAGER, 100.0f));
	s.order_gather(vills.data(), vills.size(), wood);
	int arrived = -1;
	float pen = 0.0f;
	for (int t = 0; t < 1200 && arrived < 0; ++t) {
		s.tick(DT);
		pen = std::max(pen, max_penetration(s));
		const Simulation::UnitView u = s.units();
		int at_pile = 0;
		for (uint32_t id : vills) {
			const int i = index_of(s, id);
			if (i < 0) continue;
			const float dx = u.pos_x[i] + 4.0f, dz = u.pos_z[i];
			if (dx * dx + dz * dz <= 3.0f * 3.0f) ++at_pile;
		}
		if (at_pile == (int)vills.size()) arrived = t;
	}
	std::printf("  gather around houses: all 3 at the wood pile after %d ticks (-1 = never in 1200), max penetration %.3f\n", arrived, pen);
	check(arrived >= 0, "villagers never reached the wood pile behind the houses");
	check(pen <= MAX_PENETRATION, "villager overlapped a house");
}

// 4. Gather behind a long wall: too long to get around inside the short-path range, so it needs
// the long path that move_units now requests for GATHER/BUILD/RETURN_DROPSITE.
static void gather_behind_long_wall() {
	Simulation s;
	setup(s);
	// Wall segments at x in [0, 2] from z = -30 to z = 30, with a gap at z in [20, 24].
	for (int cy = -15; cy <= 14; ++cy)
		if (cy != 10 && cy != 11) s.spawn_building(0, cy, BLD_WALL_SEGMENT, 0, 1.0f);
	const uint32_t wood = s.spawn_resource_world(10.0f, 0.0f, RES_WOOD, 5000);
	const uint32_t vill = s.spawn_unit(-8.0f, 0.0f, 0, UNIT_VILLAGER, 100.0f);
	s.order_gather(&vill, 1, wood);
	int arrived = -1;
	float pen = 0.0f;
	for (int t = 0; t < 2400 && arrived < 0; ++t) {
		s.tick(DT);
		pen = std::max(pen, max_penetration(s));
		const int i = index_of(s, vill);
		if (i < 0) break;
		const Simulation::UnitView u = s.units();
		const float dx = u.pos_x[i] - 10.0f, dz = u.pos_z[i];
		if (dx * dx + dz * dz <= 3.0f * 3.0f) arrived = t;
	}
	std::printf("  gather behind long wall: at the wood pile after %d ticks (-1 = never in 2400), max penetration %.3f\n", arrived, pen);
	check(arrived >= 0, "villager never got through the gap in the long wall");
	check(pen <= MAX_PENETRATION, "villager overlapped a wall segment");
}

// 3. Move through a gap.
static void move_through_gap() {
	Simulation s;
	setup(s);
	// Barracks are 4x4. A row at x = 2 from z = -22 to z = 22, with a gap at z in [-2, 2].
	for (int k = -5; k <= 5; ++k) {
		if (k == 0) continue;
		s.spawn_building_world(2.0f, 4.0f * k, BLD_BARRACKS, 0, 1.0f);
	}
	std::vector<uint32_t> ids;
	for (int k = 0; k < 4; ++k) ids.push_back(s.spawn_unit(-10.0f, -14.0f + 1.5f * k, 0, UNIT_SWORDSMAN, 160.0f));
	s.cmd_move(ids.data(), ids.size(), 12.0f, -12.0f);
	int arrived = -1;
	float pen = 0.0f;
	for (int t = 0; t < 1200 && arrived < 0; ++t) {
		s.tick(DT);
		pen = std::max(pen, max_penetration(s));
		const Simulation::UnitView u = s.units();
		int on_far_side = 0;
		for (uint32_t id : ids) {
			const int i = index_of(s, id);
			if (i >= 0 && u.pos_x[i] > 6.0f) ++on_far_side;
		}
		if (on_far_side == (int)ids.size()) arrived = t;
	}
	std::printf("  move through gap: all 4 through after %d ticks (-1 = never in 1200), max penetration %.3f\n", arrived, pen);
	check(arrived >= 0, "units did not get through the gap");
	check(pen <= MAX_PENETRATION, "unit overlapped a barracks");
}

// 5. Walk across fields.
static void walk_across_fields() {
	Simulation s;
	setup(s);
	// A touching column of fields across the straight line from x = -12 to x = 12.
	s.spawn_building_world(0.0f, 0.0f, BLD_FIELD, 0, 1.0f);
	const float half = s.buildings().back().half_size;
	for (int k = 1; k <= 2; ++k) {
		s.spawn_building_world(0.0f, 2.0f * half * k, BLD_FIELD, 0, 1.0f);
		s.spawn_building_world(0.0f, -2.0f * half * k, BLD_FIELD, 0, 1.0f);
	}
	const uint32_t walker = s.spawn_unit(-12.0f, 0.0f, 0, UNIT_SWORDSMAN, 160.0f);
	s.cmd_move(&walker, 1, 12.0f, 0.0f);
	int arrived = -1;
	float max_dev = 0.0f;
	for (int t = 0; t < 1200 && arrived < 0; ++t) {
		s.tick(DT);
		const int i = index_of(s, walker);
		if (i < 0) break;
		const Simulation::UnitView u = s.units();
		max_dev = std::max(max_dev, std::fabs(u.pos_z[i]));
		const float dx = u.pos_x[i] - 12.0f, dz = u.pos_z[i];
		if (dx * dx + dz * dz <= 1.0f) arrived = t;
	}
	std::printf("  walk across fields: arrived after %d ticks (-1 = never in 1200), max sideways %.3f\n", arrived, max_dev);
	check(arrived >= 0, "unit never crossed the fields");
	// Long-path waypoints sit on navcell centres, 1 world unit off the line; going around the
	// fields would take the unit more than 10 units sideways.
	check(max_dev <= 2.0f, "unit walked around the fields instead of across");
}

// 6. Out of a house cluster: Petra builds houses around an idle catapult (test_petra's roster
// army, #136). The catapult's long path starts on navcells in 2-unit gaps between the houses, closer to
// them than a catapult can stand, so it could never reach those waypoints and stayed put.
static void out_of_house_cluster() {
	Simulation s;
	setup(s);
	s.spawn_building_world(-21.0f, -33.0f, BLD_HOUSE, 0, 1.0f);
	s.spawn_building_world(-17.0f, -33.0f, BLD_HOUSE, 0, 1.0f);
	s.spawn_building_world(-25.0f, -31.0f, BLD_HOUSE, 0, 1.0f);
	const uint32_t cat = s.spawn_unit(-22.89f, -35.68f, 0, UNIT_CATAPULT, 0.0f);
	for (int t = 0; t < 20; ++t) s.tick(DT);
	// Foundations placed on top of it: the builder's arrival sends the catapult off each one
	// (Foundation.js Commit, UnitAI LeaveFoundation), and the committed foundation blocks it.
	const uint32_t builder = s.spawn_unit(-21.0f, -40.0f, 0, UNIT_FEMALE_CITIZEN, 0.0f);
	const uint32_t f1 = s.spawn_building_world(-23.0f, -37.0f, BLD_HOUSE, 0, 0.0f);
	s.order_build_or_repair(&builder, 1, f1);
	for (int t = 0; t < 400; ++t) s.tick(DT);
	const uint32_t f2 = s.spawn_building_world(-19.0f, -37.0f, BLD_HOUSE, 0, 0.0f);
	s.order_build_or_repair(&builder, 1, f2);
	for (int t = 0; t < 400; ++t) s.tick(DT);
	s.cmd_move(&cat, 1, 9.0f, -35.0f);
	int arrived = -1;
	float pen = 0.0f;
	for (int t = 0; t < 1200 && arrived < 0; ++t) {
		s.tick(DT);
		pen = std::max(pen, max_penetration(s));
		const int i = index_of(s, cat);
		if (i < 0) break;
		const Simulation::UnitView u = s.units();
		const float dx = u.pos_x[i] - 9.0f, dz = u.pos_z[i] + 35.0f;
		if (dx * dx + dz * dz <= 1.0f) arrived = t;
	}
	std::printf("  out of a house cluster: catapult arrived after %d ticks (-1 = never in 1200), max penetration %.3f\n", arrived, pen);
	check(arrived >= 0, "catapult never left the house cluster");
	check(pen <= MAX_PENETRATION, "catapult overlapped a house");
}

// 7. Walk past a turned barracks.
static void walk_past_turned_building() {
	constexpr float QUARTER_PI = 0.785398163f;
	float ux, uz;
	nav::footprint_axis(Simulation::DEFAULT_BUILDING_ROT, ux, uz);
	check(ux == 1.0f && uz == 0.0f, "the default facing is not axis-aligned");
	nav::footprint_axis(Simulation::unpack_angle(Simulation::pack_angle(2.0f * QUARTER_PI)), ux, uz);
	check(ux == 1.0f && uz == 0.0f, "a quarter turn is not axis-aligned");
	nav::footprint_axis(Simulation::DEFAULT_BUILDING_ROT + QUARTER_PI, ux, uz);
	check(std::fabs(ux - 0.70710678f) < 1e-5f && std::fabs(uz + 0.70710678f) < 1e-5f, "45 degrees gives the wrong axis");

	// 0 A.D.'s long wall: 36 x 5 m, half extents 4.5 and 0.625 RTS0 units.
	Simulation::Building wall{};
	wall.set_extents(4.5f, 0.625f);
	check(nav::footprint_contains(wall, 4.2f, 0.0f, 0.0f) && !nav::footprint_contains(wall, 0.0f, 0.8f, 0.0f),
			"the unturned wall does not lie along x");
	wall.set_rot(Simulation::DEFAULT_BUILDING_ROT + 2.0f * QUARTER_PI);
	check(nav::footprint_aligned(wall) && nav::footprint_extent(wall) == 4.5f, "a quarter-turned wall is not axis-aligned");
	check(nav::footprint_contains(wall, 0.0f, 4.2f, 0.0f) && !nav::footprint_contains(wall, 0.8f, 0.0f, 0.0f),
			"a quarter turn does not swap the wall's width and depth");
	check(nav::footprint_overlaps_box(wall, -0.2f, 4.0f, 0.2f, 4.4f) && !nav::footprint_overlaps_box(wall, 0.7f, -0.2f, 1.0f, 0.2f),
			"the quarter-turned wall's placement box is wrong");
	wall.set_rot(Simulation::DEFAULT_BUILDING_ROT + QUARTER_PI);
	check(std::fabs(nav::footprint_extent(wall) - 5.125f * 0.70710678f) < 1e-4f, "the 45-degree wall's bounds are wrong");
	check(nav::footprint_contains(wall, 2.8f, -2.8f, 0.0f) && !nav::footprint_contains(wall, 2.8f, 2.8f, 0.0f),
			"the 45-degree wall does not lie along its u axis");
	{
		Simulation w;
		setup(w);
		// On a navcell centre: grown by the rasterizer's 1 unit, the wall spans one 2-unit navcell across.
		const uint32_t seg = w.spawn_building_world(-29.0f, -29.0f, BLD_WALL_SEGMENT, 0, 1.0f);
		for (auto &b : w.buildings_)
			if (b.id == seg) {
				b.set_extents(4.5f, 0.625f);
				b.set_rot(Simulation::DEFAULT_BUILDING_ROT + 2.0f * QUARTER_PI);
			}
		w.rebuild_nav_grid();
		auto open = [&](float x, float z) {
			const auto [cx, cy] = w.nav_grid_.world_to_cell(x, z);
			return w.nav_grid_.is_passable_for(cx, cy, PASS_INFANTRY);
		};
		check(!open(-29.0f, -25.5f) && !open(-29.0f, -32.5f) && open(-26.0f, -29.0f) && open(-29.0f, -22.5f),
				"the nav grid does not block the quarter-turned wall along z");
	}

	Simulation s;
	setup(s);
	const uint32_t barracks = s.spawn_building_world(0.0f, 0.0f, BLD_BARRACKS, 0, 1.0f);
	const uint32_t house = s.spawn_building_world(0.0f, 16.0f, BLD_HOUSE, 0, 0.0f);
	for (auto &b : s.buildings_)
		if (b.id == barracks || b.id == house) b.set_rot(Simulation::DEFAULT_BUILDING_ROT + QUARTER_PI);
	s.rebuild_nav_grid();
	const auto &turned = s.buildings()[s.building_id_to_idx_.at(barracks)];
	check(nav::footprint_extent(turned) > turned.half_size * 1.41f, "the turned barracks' bounds are too small");
	// For placement: the turned barracks reaches a box past its unturned side, not one in its unturned corner.
	check(nav::footprint_overlaps_box(turned, 2.2f, -0.2f, 2.6f, 0.2f), "the turned barracks misses a box under its corner");
	check(!nav::footprint_overlaps_box(turned, 1.6f, 1.6f, 1.9f, 1.9f), "the turned barracks covers its unturned corner");

	const uint32_t walker = s.spawn_unit(-12.0f, 0.0f, 0, UNIT_SWORDSMAN, 160.0f);
	s.cmd_move(&walker, 1, 12.0f, 0.0f);
	const uint32_t woman = s.spawn_unit(-6.0f, 16.5f, 0, UNIT_FEMALE_CITIZEN, 100.0f);
	s.order_build_or_repair(&woman, 1, house);
	int arrived = -1, built = -1;
	float pen = 0.0f;
	for (int t = 0; t < 1200 && (arrived < 0 || built < 0); ++t) {
		s.tick(DT);
		pen = std::max(pen, max_penetration(s));
		const int i = index_of(s, walker);
		if (i < 0) break;
		const Simulation::UnitView u = s.units();
		const float dx = u.pos_x[i] - 12.0f, dz = u.pos_z[i];
		if (arrived < 0 && dx * dx + dz * dz <= 1.0f) arrived = t;
		if (built < 0 && s.buildings()[s.building_id_to_idx_.at(house)].build_progress >= 1.0f) built = t;
	}
	std::printf("  walk past turned barracks: arrived after %d ticks, house built after %d (-1 = never in 1200), "
			"max penetration %.3f\n", arrived, built, pen);
	check(arrived >= 0, "unit never got past the turned barracks");
	check(built >= 0, "woman never finished the turned house");
	check(pen <= MAX_PENETRATION, "unit overlapped a turned building");
}

// 8. Siege through a gap.
static void siege_through_gap() {
	Simulation s;
	setup(s);
	// Houses are 2x2: a touching row at x = 0 from z = -21 to z = 21, with a gap at z in [-1, 1].
	for (int k = 1; k <= 10; ++k) {
		s.spawn_building_world(0.0f, 2.0f * k, BLD_HOUSE, 0, 1.0f);
		s.spawn_building_world(0.0f, -2.0f * k, BLD_HOUSE, 0, 1.0f);
	}
	const uint32_t cat = s.spawn_unit(-10.0f, 0.5f, 0, UNIT_CATAPULT, 0.0f);
	s.cmd_move(&cat, 1, 10.0f, 0.0f);
	int arrived = -1;
	float pen = 0.0f, max_dev = 0.0f;
	for (int t = 0; t < 1200 && arrived < 0; ++t) {
		s.tick(DT);
		pen = std::max(pen, max_penetration(s));
		const int i = index_of(s, cat);
		if (i < 0) break;
		const Simulation::UnitView u = s.units();
		max_dev = std::max(max_dev, std::fabs(u.pos_z[i]));
		const float dx = u.pos_x[i] - 10.0f, dz = u.pos_z[i];
		if (dx * dx + dz * dz <= 1.0f) arrived = t;
	}
	std::printf("  siege through a gap: catapult arrived after %d ticks (-1 = never in 1200), max sideways %.3f, "
			"max penetration %.3f\n", arrived, max_dev, pen);
	check(arrived >= 0, "catapult never got past the row of houses");
	check(max_dev <= 2.0f, "catapult went around the houses instead of through the gap");
	check(pen <= MAX_PENETRATION, "catapult overlapped a house");
}

// 9. Walk into a building.
static void walk_into_building() {
	Simulation s;
	setup(s);
	s.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
	const float half = s.buildings().back().half_size;
	const uint32_t cat = s.spawn_unit(-12.0f, 0.3f, 0, UNIT_CATAPULT, 0.0f);
	const uint32_t sword = s.spawn_unit(0.3f, 12.0f, 0, UNIT_SWORDSMAN, 160.0f);
	s.cmd_move(&cat, 1, -half + 0.3f, 0.0f);
	s.cmd_move(&sword, 1, 0.0f, half - 0.5f);
	int idle = -1;
	float pen = 0.0f;
	for (int t = 0; t < 1200 && idle < 0; ++t) {
		s.tick(DT);
		pen = std::max(pen, max_penetration(s));
		const int ci = index_of(s, cat), si = index_of(s, sword);
		if (ci < 0 || si < 0) break;
		const Simulation::UnitView u = s.units();
		if (u.action_state[ci] == ACTION_IDLE && u.action_state[si] == ACTION_IDLE) idle = t;
	}
	const Simulation::UnitView u = s.units();
	const int ci = index_of(s, cat), si = index_of(s, sword);
	std::printf("  walk into a building: both stopped beside the town centre after %d ticks (-1 = never in 1200), "
			"catapult at x %.2f, swordsman at z %.2f, max penetration %.3f\n", idle, ci >= 0 ? u.pos_x[ci] : 0.0f,
			si >= 0 ? u.pos_z[si] : 0.0f, pen);
	check(idle >= 0, "a walk to a point inside a building never ended");
	check(ci >= 0 && u.pos_x[ci] < -half && u.pos_x[ci] > -half - 1.5f, "the catapult stopped away from the town centre's near side");
	check(si >= 0 && u.pos_z[si] > half && u.pos_z[si] < half + 1.0f, "the swordsman stopped away from the town centre's near side");
	check(pen <= MAX_PENETRATION, "a unit overlapped the town centre");
}

static void walk_reissued_every_tick() {
	Simulation s;
	setup(s);
	// Off its navcell's centre, as a unit usually stands.
	const uint32_t scout = s.spawn_unit(0.2f, 0.2f, 0, UNIT_SWORDSMAN, 0.0f);
	int arrived = -1;
	for (int t = 0; t < 600 && arrived < 0; ++t) {
		s.cmd_move(&scout, 1, -30.0f, 0.2f); // as Petra's scout is sent home every tick
		s.tick(DT);
		const int i = index_of(s, scout);
		if (i < 0) break;
		if (std::hypot(s.units().pos_x[i] + 30.0f, s.units().pos_z[i] - 0.2f) < 1.0f) arrived = t;
	}
	std::printf("  walk reissued every tick: arrived after %d ticks (-1 = never in 600)\n", arrived);
	check(arrived >= 0, "a walk given again every tick never arrived");
}

static void walk_to_one_point() {
	Simulation s;
	setup(s);
	std::vector<uint32_t> ids;
	for (int k = 0; k < 12; ++k) ids.push_back(s.spawn_unit(-6.0f + 1.1f * k, -14.0f, 0, UNIT_SWORDSMAN, 0.0f));
	for (uint32_t id : ids) s.cmd_move(&id, 1, 0.0f, 0.0f); // one at a time: all to the same point
	int all_idle = -1;
	float far = 0.0f;
	for (int t = 0; t < 1200 && all_idle < 0; ++t) {
		s.tick(DT);
		bool idle = true;
		for (uint32_t id : ids) {
			const int i = index_of(s, id);
			if (i >= 0 && s.units().action_state[i] != ACTION_IDLE) idle = false;
		}
		if (idle) all_idle = t;
	}
	for (uint32_t id : ids) {
		const int i = index_of(s, id);
		if (i >= 0) far = std::max(far, std::hypot(s.units().pos_x[i], s.units().pos_z[i]));
	}
	std::printf("  walk to one point: all 12 stopped after %d ticks (-1 = never in 1200), farthest %.2f from it\n", all_idle, far);
	check(all_idle >= 0, "units sent to one point kept pushing for it");
	check(far <= 3.0f + 1.0f, "a unit sent to one point stopped beyond 12 m of it");
}

// A sea across the middle of the map (|z| < 16), as tools/test_naval.cpp makes.
static void sea_setup(Simulation &s) {
	setup(s);
	constexpr int RES = 65;
	constexpr float HALF = 64.0f;
	s.set_map_half(HALF);
	std::vector<float> h((size_t)RES * RES);
	for (int j = 0; j < RES; ++j)
		for (int i = 0; i < RES; ++i) {
			const float z = -HALF + 2.0f * HALF * j / (RES - 1);
			h[(size_t)j * RES + i] = std::fabs(z) < 16.0f ? -3.0f : 3.0f;
		}
	s.set_terrain_data(h, RES, 0.0f);
	s.begin_bulk_spawn();
	s.end_bulk_spawn(); // marks the water on the nav grid
}

static void ships_that_cannot_get_there() {
	// A trireme a player sends ashore stops at the shore (MakeGoalReachable).
	Simulation s;
	sea_setup(s);
	const uint32_t ship = s.spawn_unit(0.0f, -5.0f, 0, UNIT_TRIREME, 0.0f);
	s.order_walk(&ship, 1, 0.0f, 30.0f, false, true); // a player's walk
	int idle = -1;
	for (int t = 0; t < 1200 && idle < 0; ++t) {
		s.tick(DT);
		const int i = index_of(s, ship);
		if (i >= 0 && s.units().action_state[i] == ACTION_IDLE) idle = t;
	}
	const int si = index_of(s, ship);
	const float z = si >= 0 ? s.units().pos_z[si] : 99.0f;
	std::printf("  ship sent ashore: stopped after %d ticks (-1 = never in 1200) at z %.2f\n", idle, z);
	check(idle >= 0, "a ship sent ashore never stopped");
	check(z < 16.0f, "a ship sent ashore sailed onto the land");

}

static void crowds_at_the_shore() {
	Simulation s;
	sea_setup(s);
	std::vector<uint32_t> ships, men;
	// Two tight 4 x 3 clumps, one each side of the shore line at z = 16.
	for (int k = 0; k < 12; ++k) {
		const float dx = 0.2f * (float)(k % 4), dz = 0.2f * (float)(k / 4);
		ships.push_back(s.spawn_unit(dx, 14.0f + dz, 0, UNIT_TRIREME, 0.0f));
		men.push_back(s.spawn_unit(20.0f + dx, 17.0f + dz, 0, UNIT_SWORDSMAN, 0.0f));
	}
	int aground = 0, at_sea = 0;
	for (int t = 0; t < 200; ++t) {
		s.tick(DT);
		const Simulation::UnitView u = s.units();
		auto on = [&](uint32_t id, uint8_t pass) {
			const int i = index_of(s, id);
			const auto [cx, cy] = s.nav_grid_.world_to_cell(u.pos_x[i], u.pos_z[i]);
			return s.nav_grid_.is_passable_for(cx, cy, pass);
		};
		for (uint32_t id : ships) aground += !on(id, PASS_SHIP);
		for (uint32_t id : men) at_sea += !on(id, PASS_INFANTRY);
	}
	std::printf("  crowds at the shore: ship-ticks aground %d, soldier-ticks at sea %d (of %d each)\n", aground, at_sea, 200 * 12);
	check(aground == 0, "pushing ran a ship aground");
	check(at_sea == 0, "pushing carried a soldier out to sea");
}

// An enemy walking through a closed gate in a long wall: the long path goes through the gate
// (unlocked gates don't block pathfinding), the gate stops the unit dead, and the walk ends after
// 35 turns of not moving (MoveFailed) instead of pushing at the gate for good.
static void walk_into_closed_gate() {
	Simulation s;
	setup(s);
	s.spawn_building_world(0.0f, 0.0f, BLD_GATE, 1, 1.0f);
	const float gh = s.buildings().back().half_size;
	s.spawn_building_world(0.0f, 0.0f, BLD_WALL_SEGMENT, 1, 1.0f);
	const float wh = s.buildings().back().half_size;
	s.buildings_.pop_back(); // only measuring the segment
	for (float z = gh + wh; z < 60.0f; z += 2.0f * wh) {
		s.spawn_building_world(0.0f, z, BLD_WALL_SEGMENT, 1, 1.0f);
		s.spawn_building_world(0.0f, -z, BLD_WALL_SEGMENT, 1, 1.0f);
	}
	const uint32_t sword = s.spawn_unit(-10.0f, 0.0f, 0, UNIT_SWORDSMAN, 0.0f);
	s.cmd_move(&sword, 1, 10.0f, 0.0f);
	int at_gate = -1, stopped = -1;
	float x = -10.0f;
	for (int t = 0; t < 1200 && stopped < 0; ++t) {
		s.tick(DT);
		const int i = index_of(s, sword);
		if (i < 0) break;
		const Simulation::UnitView u = s.units();
		x = u.pos_x[i];
		if (at_gate < 0 && x > -gh - 0.5f) at_gate = t;
		if (u.action_state[i] == ACTION_IDLE) stopped = t;
	}
	std::printf("  walk into a closed gate: at the gate after %d ticks, walk ended after %d (-1 = never in 1200), x %.2f\n",
			at_gate, stopped, x);
	check(stopped >= 0, "a walk into a closed gate never ended");
	check(at_gate < 0 || stopped < 0 || stopped - at_gate <= 35 * 4 + 20, "a walk stopped by a closed gate ended later than 35 turns");
	check(x < 0.0f, "the unit got through the closed gate");
}

// A cliff along x = 0, 8 units high over 2 (slope 4), and a ramp for z in [16, 34] that climbs
// the same 8 units over 20 (slope 0.4): the top is x > 10 on the ramp, x > 1 elsewhere.
static void cliff_setup(Simulation &s) {
	setup(s);
	constexpr int RES = 65;
	constexpr float HALF = 64.0f;
	s.set_map_half(HALF);
	std::vector<float> h((size_t)RES * RES);
	for (int j = 0; j < RES; ++j)
		for (int i = 0; i < RES; ++i) {
			const float x = -HALF + 2.0f * HALF * i / (RES - 1), z = -HALF + 2.0f * HALF * j / (RES - 1);
			const bool ramp = z >= 16.0f && z <= 34.0f;
			h[(size_t)j * RES + i] = ramp ? 8.0f * std::clamp((x + 10.0f) / 20.0f, 0.0f, 1.0f)
			                              : 8.0f * std::clamp((x + 1.0f) / 2.0f, 0.0f, 1.0f);
		}
	s.set_terrain_data(h, RES, -1.0f);
	s.begin_bulk_spawn();
	s.end_bulk_spawn();
}

static void cliffs() {
	Simulation s;
	cliff_setup(s);
	auto steep_at = [&](float x, float z) {
		const auto [cx, cy] = s.nav_grid_.world_to_cell(x, z);
		return s.nav_grid_.is_steep(cx, cy);
	};
	check(steep_at(0.0f, 0.0f) && !steep_at(0.0f, 25.0f) && !steep_at(-20.0f, 0.0f), "the cliff is not steep or the ramp is");
	struct Walker { uint32_t id; float gx, gz; int arrived = -1, on_cliff = 0; float max_z = -1e9f; const char *what; };
	std::vector<Walker> w;
	w.push_back({s.spawn_unit(-20.0f, 0.0f, 0, UNIT_SWORDSMAN, 0.0f), 20.0f, 0.0f});
	w.back().what = "swordsman";
	w.push_back({s.spawn_unit(-20.0f, -4.0f, 0, UNIT_CAVALRY, 0.0f), 20.0f, -4.0f});
	w.back().what = "horseman";
	s.cmd_move(&w[0].id, 1, w[0].gx, w[0].gz);
	s.cmd_move(&w[1].id, 1, w[1].gx, w[1].gz);
	const uint32_t wood = s.spawn_resource_world(20.0f, -10.0f, RES_WOOD, 5000);
	w.push_back({s.spawn_unit(-20.0f, -10.0f, 0, UNIT_VILLAGER, 100.0f), 20.0f, -10.0f});
	w.back().what = "gatherer";
	s.order_gather(&w[2].id, 1, wood);
	for (int t = 0; t < 2400; ++t) {
		s.tick(DT);
		const Simulation::UnitView u = s.units();
		for (Walker &k : w) {
			const int i = index_of(s, k.id);
			if (i < 0 || k.arrived >= 0) continue;
			k.on_cliff += steep_at(u.pos_x[i], u.pos_z[i]);
			k.max_z = std::max(k.max_z, u.pos_z[i]);
			const float dx = u.pos_x[i] - k.gx, dz = u.pos_z[i] - k.gz;
			if (dx * dx + dz * dz <= 3.0f * 3.0f) k.arrived = t;
		}
	}
	for (const Walker &k : w) {
		std::printf("  cliffs: %s up after %d ticks (-1 = never in 2400), %d ticks on the cliff, furthest z %.1f\n",
				k.what, k.arrived, k.on_cliff, k.max_z);
		check(k.arrived >= 0, "a unit never got up the ramp");
		check(k.on_cliff == 0, "a unit climbed the cliff");
		check(k.max_z >= 16.0f, "a unit got up without taking the ramp");
	}
	check(s.check_placement(0, -1, -8, BLD_HOUSE) == Simulation::PLACE_TERRAIN, "a house can be placed on the cliff");
	check(s.check_placement(0, -12, -8, BLD_HOUSE) != Simulation::PLACE_TERRAIN, "a house can't be placed on flat ground");
}

// 16. Chases.
static void channel_setup(Simulation &s, float half_width) {
	setup(s);
	constexpr int RES = 65;
	constexpr float HALF = 64.0f;
	s.set_map_half(HALF);
	std::vector<float> h((size_t)RES * RES);
	for (int j = 0; j < RES; ++j)
		for (int i = 0; i < RES; ++i) {
			const float z = -HALF + 2.0f * HALF * j / (RES - 1);
			h[(size_t)j * RES + i] = std::fabs(z) < half_width ? -3.0f : 3.0f;
		}
	s.set_terrain_data(h, RES, 0.0f);
	s.begin_bulk_spawn();
	s.end_bulk_spawn();
}

static void chases() {
	auto deep_at = [](const Simulation &s, uint32_t id) {
		const int i = index_of(s, id);
		const auto [cx, cy] = s.nav_grid_.world_to_cell(s.units_.pos_x[i], s.units_.pos_z[i]);
		return s.nav_grid_.cells.inside(cx, cy) && s.nav_grid_.cells.at(cx, cy) == NAV_WATER;
	};
	auto attacking = [](const Simulation &s, uint32_t id, uint32_t target) {
		const int i = index_of(s, id);
		return s.units_.action_state[i] == ACTION_ATTACK &&
				(s.units_.target_unit_id[i] == target || s.units_.target_building_id[i] == target);
	};
	// A player's attack on a unit, then on a house, across the sea (z -16 to 16).
	for (int house = 0; house < 2; ++house) {
		Simulation s;
		channel_setup(s, 16.0f);
		uint32_t target;
		if (house) {
			target = s.spawn_building_world(0.0f, 30.0f, BLD_HOUSE, 1, 1.0f);
		} else {
			target = s.spawn_unit(0.0f, 30.0f, 1, UNIT_SPEARMAN, 0.0f);
			s.order_set_stance(&target, 1, STANCE_PASSIVE);
		}
		const uint32_t a = s.spawn_unit(0.0f, -30.0f, 0, UNIT_SWORDSMAN, 0.0f);
		s.set_reveal_map(true); // the player sees the far shore (a chase into the fog ends, CheckTargetVisible)
		if (house) s.order_attack_building(&a, 1, target);
		else s.order_attack(&a, 1, target);
		int gave_up = -1, wet = 0;
		for (int t = 0; t < 800; ++t) {
			s.tick(DT);
			wet += deep_at(s, a);
			if (gave_up < 0 && !attacking(s, a, target)) gave_up = t;
		}
		const float z = s.units_.pos_z[index_of(s, a)];
		std::printf("  chases: attack on a %s across the sea given up after %d ticks (-1 = never in 800), %d ticks in deep water, stands at z %.1f\n",
				house ? "house" : "unit", gave_up, wet, z);
		check(gave_up >= 0, "an attack across the sea was never given up");
		check(wet == 0, "an attacker waded into the sea after its target");
		check(z > -20.0f && z < -12.0f, "an attacker that gave up is not at the shore");
	}
	// A soldier's own fight with an enemy across a channel (z -6 to 6), well within its sight.
	{
		Simulation s;
		channel_setup(s, 6.0f);
		const uint32_t foe = s.spawn_unit(0.0f, 9.0f, 1, UNIT_SPEARMAN, 0.0f);
		s.order_set_stance(&foe, 1, STANCE_PASSIVE);
		const uint32_t a = s.spawn_unit(0.0f, -9.0f, 0, UNIT_SWORDSMAN, 0.0f);
		const uint64_t paths0 = s.map_scale_stats().path_calls;
		int wet = 0, chased = 0;
		float min_z = 0.0f, max_z = -99.0f;
		for (int t = 0; t < 400; ++t) {
			s.tick(DT);
			wet += deep_at(s, a);
			chased += attacking(s, a, foe);
			const float z = s.units_.pos_z[index_of(s, a)];
			min_z = std::min(min_z, z);
			max_z = std::max(max_z, z);
		}
		const int fi = index_of(s, foe);
		std::printf("  chases: own fight across a channel: %d ticks chasing, %d in deep water, z %.1f to %.1f, foe hp %.0f/%.0f, %llu paths\n",
				chased, wet, min_z, max_z, s.units_.hp[fi], s.units_.max_hp[fi],
				(unsigned long long)(s.map_scale_stats().path_calls - paths0));
		check(chased > 0, "a soldier did not go for an enemy across a channel");
		check(wet == 0, "a soldier waded into a channel after an enemy");
		check(s.units_.hp[fi] == s.units_.max_hp[fi], "a swordsman hit an enemy across a channel");
	}
	// A player's attack on a unit up a cliff with one ramp (cliff_setup).
	{
		Simulation s;
		cliff_setup(s);
		const uint32_t foe = s.spawn_unit(20.0f, 0.0f, 1, UNIT_SPEARMAN, 0.0f);
		s.order_set_stance(&foe, 1, STANCE_PASSIVE);
		const uint32_t a = s.spawn_unit(-20.0f, 0.0f, 0, UNIT_SWORDSMAN, 0.0f);
		s.set_reveal_map(true); // the player sees the cliff top (a chase into the fog ends, CheckTargetVisible)
		s.order_attack(&a, 1, foe);
		int hit = -1, on_cliff = 0;
		for (int t = 0; t < 2400 && hit < 0; ++t) {
			s.tick(DT);
			const int i = index_of(s, a), fi = index_of(s, foe);
			const auto [cx, cy] = s.nav_grid_.world_to_cell(s.units_.pos_x[i], s.units_.pos_z[i]);
			on_cliff += s.nav_grid_.is_steep(cx, cy);
			if (fi < 0 || !s.units_.alive[fi] || s.units_.hp[fi] < s.units_.max_hp[fi]) hit = t;
		}
		std::printf("  chases: attack up the cliff landed after %d ticks (-1 = never in 2400), %d ticks on the cliff\n", hit, on_cliff);
		check(hit >= 0, "an attacker never reached its target up the ramp");
		check(on_cliff == 0, "an attacker climbed the cliff to its target");
	}
}

int main() {
	std::printf("RTS0 building steering test (src/simulation/nav/building_steering.h)\n");
	guard_behind_wall();
	gather_around_houses();
	move_through_gap();
	gather_behind_long_wall();
	walk_across_fields();
	out_of_house_cluster();
	walk_past_turned_building();
	siege_through_gap();
	walk_into_building();
	walk_reissued_every_tick();
	walk_to_one_point();
	ships_that_cannot_get_there();
	walk_into_closed_gate();
	crowds_at_the_shore();
	cliffs();
	chases();
	if (g_failures) {
		std::printf("FAIL (%d)\n", g_failures);
		return 1;
	}
	std::printf("PASS\n");
	return 0;
}
