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
//     the corners, not the unturned square, and a woman finishes a turned house.
//  8. Siege through a gap: a catapult keeps 0 A.D.'s "large" clearance (3 m, unit_building_clearance)
//     from buildings, so it takes a 2-unit (8 m) gap in a long row of houses instead of going around.
//  9. Walk into a building: a move to a point inside a town centre ends beside it, as close as the
//     unit can stand (0 A.D. MakeGoalReachable), instead of pushing at its wall for good.
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
	// Far-off town centers for both sides keep the match running (tick() stops once it ends).
	s.spawn_building_world(-52.0f, -52.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(52.0f, 52.0f, BLD_TOWN_CENTER, 1, 1.0f);
}

// Deepest overlap of any live team-0 unit's building clearance with any building footprint, turned
// by its rot.
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
			const float ez = b.half_size + r - std::fabs(dx * sn + dz * c);
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
	if (g_failures) {
		std::printf("FAIL (%d)\n", g_failures);
		return 1;
	}
	std::printf("PASS\n");
	return 0;
}
