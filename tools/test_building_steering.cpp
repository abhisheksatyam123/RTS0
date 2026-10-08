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
//  For every scenario the unit bodies must never overlap a footprint that blocks movement by more
//  than MAX_PENETRATION (separation() alone allows deep overlap when a unit walks head-on into a wall).
//  Exit: 0 = PASS, 1 = FAIL. Prints the measurements either way.
#include "simulation/simulation.h"

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

// Deepest overlap of any live team-0 unit body with any building footprint.
static float max_penetration(const Simulation &s) {
	const Simulation::UnitView u = s.units();
	float worst = 0.0f;
	for (size_t i = 0; i < u.count; ++i) {
		if (!u.alive[i] || u.team[i] != 0) continue;
		const float r = unit_collision_radius(u.unit_type[i]);
		for (const auto &b : s.buildings()) {
			if (b.wrecked || !building_blocks_movement(b.type)) continue;
			const float ex = b.half_size + r - std::fabs(u.pos_x[i] - b.x);
			const float ez = b.half_size + r - std::fabs(u.pos_z[i] - b.z);
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

int main() {
	std::printf("RTS0 building steering test (src/simulation/nav/building_steering.h)\n");
	guard_behind_wall();
	gather_around_houses();
	move_through_gap();
	gather_behind_long_wall();
	walk_across_fields();
	if (g_failures) {
		std::printf("FAIL (%d)\n", g_failures);
		return 1;
	}
	std::printf("PASS\n");
	return 0;
}
