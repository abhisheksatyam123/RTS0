// 0 A.D. gates (Gate.js): a wall across the map with one gate. The owner's units
// open it and walk through; an enemy is kept out; a locked gate keeps everyone
// out; a gate doesn't close on a unit standing in it; lock state reaches the nav
// grid, open/closed doesn't; save/load and the lockstep hash cover both flags. Only the Door
// opens: the gate's Left and Right parts keep blocking.
//   g++ -std=c++20 -O2 -Isrc tools/test_gates.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_gates && /tmp/test_gates
#define private public
#include "simulation/simulation.h"
#undef private
#include "network/turn_manager.h"
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static void run(Simulation &s, float seconds) {
	for (int t = 0; t < (int)std::lround(seconds / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
}

static const Simulation::Building *find_bld(const Simulation &s, uint32_t id) {
	for (const auto &b : s.buildings()) if (b.id == id) return &b;
	return nullptr;
}

static float unit_x(const Simulation &s, uint32_t id) {
	auto uv = s.units();
	for (size_t i = 0; i < uv.count; ++i) if (uv.id[i] == id) return uv.pos_x[i];
	return NAN;
}

// A north-south wall along x = 1 from edge to edge, with a gate at z = 1.
static uint32_t wall_with_gate(Simulation &s) {
	s.set_sim_seed(5);
	s.reset();
	// Both sides human: with no seat, player 1 runs Petra, whose base would put idle units
	// to work (an idle spearman builds its fields).
	for (uint8_t p = 0; p < 2; ++p) s.player(p).active = 1, s.player(p).is_ai = 0;
	s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(40.0f, 40.0f, BLD_TOWN_CENTER, 1, 1.0f);
	const int half_cells = (int)(s.map_half() / Simulation::GRID);
	uint32_t gate = 0;
	for (int c = -half_cells; c < half_cells; ++c) {
		const float z = c * Simulation::GRID + 1.0f;
		if (c == 0) gate = s.spawn_building_world(1.0f, z, BLD_GATE, 0, 1.0f);
		else s.spawn_building_world(1.0f, z, BLD_WALL_SEGMENT, 0, 1.0f);
	}
	// The wall stands in no-man's land, where it would decay to Gaia (TerritoryDecay); these
	// cases are about the gate, so keep it player 0's.
	for (const auto &b : s.buildings()) const_cast<Simulation::Building &>(b).territory_decay_rate = 0.0f;
	return gate;
}

static bool nav_blocked_at(const Simulation &s, float x, float z) {
	auto [cx, cy] = s.nav_grid().world_to_cell(x, z);
	return s.nav_grid().cells.at(cx, cy) == NAV_OBSTRUCTED;
}

// A closed ring of `sides` WallSet pieces of `kind` for player 0 (of `civ`) round (cx, cz), turned
// by `turn`: a tower at each corner and a long piece between each two, which reaches half a tower's
// half width into the towers at its ends (WallSet MinTowerOverlap); the first is a gate. Returns it.
static uint32_t ring_with_gate(Simulation &s, uint8_t kind, uint8_t civ, int sides, float cx, float cz, float turn, float *radius) {
	s.set_sim_seed(5);
	s.reset();
	s.player(0).active = 1, s.player(0).is_ai = 0;
	s.player(0).civ = (Civilization)civ;
	const WallSetInfo &ws = wallset_base_info(kind);
	const float len = ws.lengths[0], side = len + ws.tower_half, pi = 3.14159265f;
	const float r = 0.5f * side / std::sin(pi / (float)sides);
	*radius = r;
	uint32_t gate = 0;
	for (int k = 0; k < sides; ++k) {
		const float a0 = turn + 2.0f * pi * (float)k / (float)sides, a1 = turn + 2.0f * pi * (float)(k + 1) / (float)sides;
		const float ax = cx + r * std::cos(a0), az = cz + r * std::sin(a0), bx = cx + r * std::cos(a1), bz = cz + r * std::sin(a1);
		const float angle = std::atan2(-(bz - az), bx - ax); // a piece runs along (cos a, -sin a)
		const uint32_t id = s.spawn_map_wall_piece(0, kind, k == 0 ? Simulation::MAP_WALL_GATE : Simulation::MAP_WALL_SEGMENT,
				0.5f * (ax + bx), 0.5f * (az + bz), angle, len);
		if (k == 0) gate = id;
		s.spawn_map_wall_piece(0, kind, Simulation::MAP_WALL_TOWER, ax, az, angle, 1.0f);
	}
	return gate;
}

// Whether a land unit's long path can get from (x0, z0) to (x1, z1): a 4-connected flood over
// passable navcells, as the hierarchical pathfinder floods its regions.
static bool nav_connected(const Simulation &s, float x0, float z0, float x1, float z1) {
	const NavGrid &g = s.nav_grid();
	auto [sx, sy] = g.world_to_cell(x0, z0);
	auto [tx, ty] = g.world_to_cell(x1, z1);
	std::vector<uint8_t> seen((size_t)g.res * (size_t)g.res, 0);
	std::vector<std::pair<int, int>> open;
	if (g.is_passable(sx, sy)) open.push_back({sx, sy}), seen[(size_t)sy * g.res + sx] = 1;
	while (!open.empty()) {
		const auto [x, y] = open.back();
		open.pop_back();
		if (x == tx && y == ty) return true;
		const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
		for (const auto &e : d) {
			const int nx = x + e[0], ny = y + e[1];
			if (!g.cells.inside(nx, ny) || seen[(size_t)ny * g.res + nx] || !g.is_passable(nx, ny)) continue;
			seen[(size_t)ny * g.res + nx] = 1;
			open.push_back({nx, ny});
		}
	}
	return false;
}

// Whether a land unit's long path can get from (x0, z0) to (x1, z1) without leaving the navcells whose
// centres lie within `r` of (cx, cz).
static bool nav_connected_near(const Simulation &s, float x0, float z0, float x1, float z1, float cx, float cz, float r) {
	const NavGrid &g = s.nav_grid();
	const float cell = 2.0f * g.map_half / (float)g.res;
	auto near = [&](int x, int y) {
		const float dx = -g.map_half + ((float)x + 0.5f) * cell - cx, dz = -g.map_half + ((float)y + 0.5f) * cell - cz;
		return dx * dx + dz * dz <= r * r;
	};
	auto [sx, sy] = g.world_to_cell(x0, z0);
	auto [tx, ty] = g.world_to_cell(x1, z1);
	std::vector<uint8_t> seen((size_t)g.res * (size_t)g.res, 0);
	std::vector<std::pair<int, int>> open;
	if (g.is_passable(sx, sy) && near(sx, sy)) open.push_back({sx, sy}), seen[(size_t)sy * g.res + sx] = 1;
	while (!open.empty()) {
		const auto [x, y] = open.back();
		open.pop_back();
		if (x == tx && y == ty) return true;
		const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
		for (const auto &e : d) {
			const int nx = x + e[0], ny = y + e[1];
			if (!g.cells.inside(nx, ny) || seen[(size_t)ny * g.res + nx] || !near(nx, ny) || !g.is_passable(nx, ny)) continue;
			seen[(size_t)ny * g.res + nx] = 1;
			open.push_back({nx, ny});
		}
	}
	return false;
}

// Whether a land unit's long path crosses the wall at gate `g` near it: a 4-connected flood over
// passable navcells whose centres lie within `r` of the gate's centre, from those `out` or more in
// front of its line to one `out` or more behind it.
static bool gate_crossed_near(const Simulation &s, const Simulation::Building &g, float r, float out) {
	const NavGrid &ng = s.nav_grid();
	const bool along_v = g.hv > g.hu;
	const nav::Vec2 n = nav::footprint_dir(g, along_v ? 1.0f : 0.0f, along_v ? 0.0f : 1.0f);
	const float cell = 2.0f * ng.map_half / (float)ng.res;
	auto near = [&](int x, int y, float &dn) {
		const float dx = -ng.map_half + ((float)x + 0.5f) * cell - g.x, dz = -ng.map_half + ((float)y + 0.5f) * cell - g.z;
		dn = dx * n.x + dz * n.y;
		return dx * dx + dz * dz <= r * r;
	};
	std::vector<uint8_t> seen((size_t)ng.res * (size_t)ng.res, 0);
	std::vector<std::pair<int, int>> open;
	const auto [ci, cj] = ng.world_to_cell(g.x, g.z);
	const int reach = (int)std::ceil(r / cell) + 1;
	for (int y = cj - reach; y <= cj + reach; ++y)
		for (int x = ci - reach; x <= ci + reach; ++x) {
			float dn = 0.0f;
			if (ng.cells.inside(x, y) && near(x, y, dn) && dn >= out && ng.is_passable(x, y))
				seen[(size_t)y * ng.res + x] = 1, open.push_back({x, y});
		}
	while (!open.empty()) {
		const auto [x, y] = open.back();
		open.pop_back();
		float dn = 0.0f;
		near(x, y, dn);
		if (dn <= -out) return true;
		const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
		for (const auto &e : d) {
			const int nx = x + e[0], ny = y + e[1];
			if (!ng.cells.inside(nx, ny) || seen[(size_t)ny * ng.res + nx] || !near(nx, ny, dn) || !ng.is_passable(nx, ny)) continue;
			seen[(size_t)ny * ng.res + nx] = 1;
			open.push_back({nx, ny});
		}
	}
	return false;
}

// Whether navcell (x, y) meets the rectangle round (px, pz) with unit axes w and n = (-w.z, w.x)
// and half sizes hw along w and hn along n (separating axes: the grid's two and the rectangle's).
static bool navcell_meets(const NavGrid &g, int x, int y, float px, float pz, float wx, float wz, float hw, float hn) {
	const float cell = 2.0f * g.map_half / (float)g.res;
	const float x0 = -g.map_half + (float)x * cell, z0 = -g.map_half + (float)y * cell;
	const float nx = -wz, nz = wx;
	const float ex = hw * std::fabs(wx) + hn * std::fabs(nx), ez = hw * std::fabs(wz) + hn * std::fabs(nz);
	if (px + ex < x0 || px - ex > x0 + cell || pz + ez < z0 || pz - ez > z0 + cell) return false;
	const float ccx = x0 + 0.5f * cell - px, ccz = z0 + 0.5f * cell - pz, hc = 0.5f * cell;
	if (std::fabs(ccx * wx + ccz * wz) > hw + hc * (std::fabs(wx) + std::fabs(wz))) return false;
	if (std::fabs(ccx * nx + ccz * nz) > hn + hc * (std::fabs(nx) + std::fabs(nz))) return false;
	return true;
}

int main() {
	static Simulation s;

	// 1. The owner's unit opens the gate, walks through, and the gate closes behind it.
	{
		uint32_t gate = wall_with_gate(s);
		CHECK(s.is_hostile(0, 1));
		CHECK(!find_bld(s, gate)->gate_open);
		CHECK(!nav_blocked_at(s, 1.0f, 1.0f));  // unlocked gate: long-range paths cross it
		CHECK(nav_blocked_at(s, 1.0f, 9.0f));   // wall: they don't
		uint32_t u = s.spawn_unit(-12.0f, 1.0f, 0, UNIT_SPEARMAN);
		s.cmd_move(&u, 1, 14.0f, 1.0f);
		bool opened = false;
		for (int i = 0; i < 40 && unit_x(s, u) < 10.0f; ++i) {
			run(s, 0.5f);
			opened |= find_bld(s, gate)->gate_open;
		}
		CHECK(opened);
		CHECK(unit_x(s, u) > 10.0f);
		std::printf("own spearman crossed the gate: x = %.1f\n", unit_x(s, u));
		s.cmd_move(&u, 1, 40.0f, 1.0f);
		run(s, 6.0f);
		CHECK(!find_bld(s, gate)->gate_open);
	}

	// 2. An enemy stays out: the gate only opens for its owner and allies.
	{
		uint32_t gate = wall_with_gate(s);
		uint32_t e = s.spawn_unit(14.0f, 1.0f, 1, UNIT_SPEARMAN);
		s.order_set_stance(&e, 1, STANCE_PASSIVE);
		s.cmd_move(&e, 1, -14.0f, 1.0f);
		run(s, 20.0f);
		CHECK(!find_bld(s, gate)->gate_open);
		CHECK(unit_x(s, e) > 1.5f);
		std::printf("enemy spearman held at the gate: x = %.1f\n", unit_x(s, e));
	}

	// 3. Allies count as friends (Gate.js queries every ally of the owner).
	{
		uint32_t gate = wall_with_gate(s);
		s.set_diplomacy(0, 1, Simulation::DIPLO_ALLY);
		s.set_diplomacy(1, 0, Simulation::DIPLO_ALLY);
		uint32_t a = s.spawn_unit(14.0f, 1.0f, 1, UNIT_SPEARMAN);
		s.cmd_move(&a, 1, -14.0f, 1.0f);
		run(s, 20.0f);
		CHECK(unit_x(s, a) < -10.0f);
		(void)gate;
	}

	// 4. Locked: shut for the owner too, and long-range paths no longer cross it.
	{
		uint32_t gate = wall_with_gate(s);
		CHECK(!s.cmd_lock_gate(1, gate, true)); // not the owner
		CHECK(s.cmd_lock_gate(0, gate, true));
		CHECK(!s.cmd_lock_gate(0, gate, true)); // already locked
		CHECK(nav_blocked_at(s, 1.0f, 1.0f));
		uint32_t u = s.spawn_unit(-12.0f, 1.0f, 0, UNIT_SPEARMAN);
		s.cmd_move(&u, 1, 14.0f, 1.0f);
		run(s, 20.0f);
		CHECK(!find_bld(s, gate)->gate_open);
		CHECK(unit_x(s, u) < 0.5f);
		CHECK(s.cmd_lock_gate(0, gate, false));
		CHECK(!nav_blocked_at(s, 1.0f, 1.0f));
		s.cmd_move(&u, 1, 14.0f, 1.0f);
		run(s, 20.0f);
		CHECK(unit_x(s, u) > 10.0f);
	}

	// 5. A gate doesn't close on a unit standing in it (CloseGate retries).
	{
		uint32_t gate = wall_with_gate(s);
		uint32_t u = s.spawn_unit(1.0f, 1.0f, 0, UNIT_SPEARMAN); // in the opening
		s.order_set_stance(&u, 1, STANCE_PASSIVE);
		run(s, 0.5f);
		CHECK(find_bld(s, gate)->gate_open);
		CHECK(s.cmd_lock_gate(0, gate, true));
		run(s, 2.0f);
		CHECK(find_bld(s, gate)->gate_open); // locked, but someone is in the way
		CHECK(!nav_blocked_at(s, 1.0f, 1.0f)); // Gate.js blocks paths only once it has shut
		s.cmd_move(&u, 1, -20.0f, 1.0f);
		run(s, 4.0f);
		CHECK(!find_bld(s, gate)->gate_open);
		CHECK(nav_blocked_at(s, 1.0f, 1.0f));
		CHECK(unit_x(s, u) < -2.0f);
	}

	// 6. Save/load keeps both flags, and the lockstep hash sees them.
	{
		uint32_t gate = wall_with_gate(s);
		TurnManager tm;
		const uint32_t h0 = tm.compute_sim_hash(s);
		CHECK(s.cmd_lock_gate(0, gate, true));
		CHECK(tm.compute_sim_hash(s) != h0);
		const char *path = "/tmp/rts0_test_gates.sav";
		CHECK(s.save_state(path));
		static Simulation l;
		wall_with_gate(l);
		CHECK(l.load_state(path));
		CHECK(find_bld(l, gate) && find_bld(l, gate)->gate_locked);
		CHECK(tm.compute_sim_hash(s) == tm.compute_sim_hash(l));
		CHECK(nav_blocked_at(l, 1.0f, 1.0f));
	}

	// 7. Lockstep command 27 locks and unlocks.
	{
		uint32_t gate = wall_with_gate(s);
		TurnManager tm;
		auto turns = [&](int n) { for (int i = 0; i < n; ++i) tm.tick(TurnManager::TURN_DURATION, s); };
		NetCommand c;
		c.client_id = 0;
		c.cmd_type = (uint8_t)NetCommandType::NET_CMD_LOCK_GATE;
		c.target_id = gate;
		c.param = 1;
		tm.post_local_command(c);
		turns(5);
		CHECK(find_bld(s, gate)->gate_locked);
		c.param = 0;
		tm.post_local_command(c);
		turns(5);
		CHECK(!find_bld(s, gate)->gate_locked);
	}

	// 8. Upgrading a wall segment into a gate opens a path through the wall.
	{
		wall_with_gate(s);
		uint32_t w = 0;
		for (const auto &b : s.buildings())
			if (b.type == BLD_WALL_SEGMENT && std::fabs(b.z - 21.0f) < 0.1f) w = b.id;
		CHECK(w != 0);
		CHECK(nav_blocked_at(s, 1.0f, 21.0f));
		s.set_player_resources(0, 1000, 1000, 1000, 1000);
		CHECK(s.cmd_upgrade_building(0, w));
		run(s, 12.5f);
		CHECK(find_bld(s, w)->type == BLD_GATE);
		CHECK(!nav_blocked_at(s, 1.0f, 21.0f));
	}

	// 9. The pass range runs from the gate's edge to the unit's (6 m, tick_gates): a unit 2.5 units
	// off stays out of it, one just outside the gate opens it (it was 16 m from the edge).
	{
		uint32_t gate = wall_with_gate(s);
		const auto *g = find_bld(s, gate);
		const float edge = g->x - g->hu;
		uint32_t u = s.spawn_unit(edge - 2.5f, 1.0f, 0, UNIT_SPEARMAN);
		run(s, 1.0f);
		CHECK(!find_bld(s, gate)->gate_open);
		s.units_.remove_at((size_t)s.lookup_unit(u));
		s.spawn_unit(edge - 0.5f, 1.0f, 0, UNIT_SPEARMAN);
		run(s, 1.0f);
		CHECK(find_bld(s, gate)->gate_open);
	}

	// 10. A soldier on a turret point is immobile with no obstruction: it neither opens the gate
	// nor stops it closing, so a locked gate stays shut.
	{
		uint32_t gate = wall_with_gate(s);
		const auto *g = find_bld(s, gate);
		uint32_t u = s.spawn_unit(g->x, g->z, 0, UNIT_ARCHER);
		auto &gb = const_cast<Simulation::Building &>(*g);
		gb.garrisoned_units.push_back(u); // on the gate's turret point (tick_turrets keeps it there)
		run(s, 1.0f);
		CHECK(s.turret_height_.count(u) && !find_bld(s, gate)->gate_open);
		gb.garrisoned_units.clear();
		s.turret_height_.erase(u);
		const int32_t ui = s.lookup_unit(u);
		s.units_.pos_x[ui] = g->x;
		s.units_.pos_z[ui] = g->z;
		run(s, 0.4f);
		CHECK(find_bld(s, gate)->gate_open); // the same archer standing in it opens it
	}

	// 11. Gate.js uses the owner's allies, one way: player 0 allying player 1 lets 1's units
	// through though player 1 still counts 0 an enemy.
	{
		uint32_t gate = wall_with_gate(s);
		s.set_diplomacy(0, 1, Simulation::DIPLO_ALLY);
		const auto *g = find_bld(s, gate);
		s.spawn_unit(g->x + g->hu + 0.5f, 1.0f, 1, UNIT_SPEARMAN);
		run(s, 1.0f);
		CHECK(!s.is_ally(0, 1) && find_bld(s, gate)->gate_open);
	}

	// 12. The owner's livestock opens its gate (animals have UnitAI); a Gaia sheep doesn't.
	{
		uint32_t gate = wall_with_gate(s);
		const auto *g = find_bld(s, gate);
		const uint32_t wild = s.spawn_fauna(g->x - g->hu - 0.5f, 1.0f, FAUNA_SHEEP);
		run(s, 0.4f);
		CHECK(!find_bld(s, gate)->gate_open);
		(void)wild;
		s.spawn_fauna(g->x + g->hu + 0.5f, 1.0f, FAUNA_SHEEP, 0);
		run(s, 0.4f);
		CHECK(find_bld(s, gate)->gate_open);
	}

	// 13. Gate.js switches only the Door shape: an open gate's Left and Right parts keep blocking
	// movement and pathfinding. A Han gate's door is 8 of its 36 m (han/wall_gate.xml).
	{
		s.set_sim_seed(5);
		s.reset();
		s.player(0).active = 1, s.player(0).is_ai = 0;
		s.player(0).civ = CIV_HAN;
		s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
		const uint32_t gate = s.spawn_map_wall_piece(0, WALLSET_STONE, Simulation::MAP_WALL_GATE, 10.0f, 10.0f, 0.0f, 9.0f);
		const auto *g = find_bld(s, gate);
		CHECK(g && g->type == BLD_GATE && g->civ == CIV_HAN);
		auto parts = [&](float *hu_sum) {
			int n = 0;
			*hu_sum = 0.0f;
			for (const auto &o : s.steer_obstacles_) if (o.id == gate) { ++n; *hu_sum += o.hu; }
			return n;
		};
		const uint32_t far = s.spawn_unit(-30.0f, -30.0f, 0, UNIT_SPEARMAN); // units moving: the obstacle lists are built
		s.cmd_move(&far, 1, -28.0f, -30.0f);
		run(s, 0.2f);
		float closed_hu = 0.0f;
		const int closed = parts(&closed_hu);
		s.spawn_unit(g->x, g->z + g->hv + 1.0f, 0, UNIT_SPEARMAN);
		run(s, 0.4f);
		g = find_bld(s, gate);
		float open_hu = 0.0f;
		const int open = parts(&open_hu);
		const float door = g->hu * 8.0f / 36.0f, side_x = g->x + g->ux * 0.5f * (g->hu + door), side_z = g->z + g->uz * 0.5f * (g->hu + door);
		auto [scx, scz] = s.nav_grid_.world_to_cell(side_x, side_z);
		auto [ccx, ccz] = s.nav_grid_.world_to_cell(g->x, g->z);
		std::printf("  open Han gate: %d part(s) block (%.2f of %.2f half-length), side navcell %s, centre %s\n", open,
				open_hu, closed_hu, s.nav_grid_.cells.at(scx, scz) == NAV_OBSTRUCTED ? "blocked" : "open",
				s.nav_grid_.cells.at(ccx, ccz) == NAV_OBSTRUCTED ? "blocked" : "open");
		CHECK(g->gate_open && closed == 1 && std::fabs(closed_hu - g->hu) < 1e-3f && open == 2 && std::fabs(open_hu - (g->hu - door)) < 1e-3f);
		CHECK(s.nav_grid_.cells.at(scx, scz) == NAV_OBSTRUCTED && s.nav_grid_.cells.at(ccx, ccz) != NAV_OBSTRUCTED);
	}

	// 14. The gate's Left, Door and Right parts lie along its width (wall_gate.xml) however the
	// gate is turned: a north-south wall's gate is turned a quarter turn, its footprint's u axis
	// across the wall (Building::set_rot swaps hu and hv), and its door still lets the owner's
	// units through. An Iberian starting wall's gates stand at every angle.
	{
		s.set_sim_seed(5);
		s.reset();
		for (uint8_t p = 0; p < 2; ++p) s.player(p).active = 1, s.player(p).is_ai = 0;
		s.player(0).civ = CIV_IBERIANS;
		s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
		s.spawn_building_world(40.0f, 40.0f, BLD_TOWN_CENTER, 1, 1.0f);
		const float quarter = 1.5707963f, gate_len = 9.0f, seg_len = 9.0f, step = 8.5f;
		const uint32_t gate = s.spawn_map_wall_piece(0, WALLSET_STONE, Simulation::MAP_WALL_GATE, 1.0f, 1.0f, quarter, gate_len);
		for (float z = 1.0f + 0.5f * gate_len + 0.5f * step - 0.25f; z < s.map_half(); z += step)
			s.spawn_map_wall_piece(0, WALLSET_STONE, Simulation::MAP_WALL_SEGMENT, 1.0f, z, quarter, seg_len);
		for (float z = 1.0f - 0.5f * gate_len - 0.5f * step + 0.25f; z > -s.map_half(); z -= step)
			s.spawn_map_wall_piece(0, WALLSET_STONE, Simulation::MAP_WALL_SEGMENT, 1.0f, z, quarter, seg_len);
		for (const auto &b : s.buildings()) const_cast<Simulation::Building &>(b).territory_decay_rate = 0.0f;
		const auto *g = find_bld(s, gate);
		CHECK(g && g->type == BLD_GATE && g->hv > g->hu && std::fabs(g->hv - 0.5f * gate_len) < 1e-3f);
		// A spearman beside the gate's end holds it open; another walks through from further off.
		s.spawn_unit(g->x - g->hu - 0.3f, g->z - g->hv + 0.5f, 0, UNIT_SPEARMAN);
		run(s, 1.0f);
		g = find_bld(s, gate);
		CHECK(g->gate_open);
		const uint32_t u = s.spawn_unit(-12.0f, 1.0f, 0, UNIT_SPEARMAN);
		s.cmd_move(&u, 1, 12.0f, 1.0f);
		// While it is open, its two parts keep blocking either end of the gate, not its middle.
		bool crossed = false, seen_open = false, along_width = true;
		for (int k = 0; k < 80 && !crossed; ++k) {
			run(s, 0.25f);
			g = find_bld(s, gate);
			if (g->gate_open) {
				seen_open = true;
				int parts = 0;
				for (const auto &o : s.steer_obstacles_) if (o.id == gate) {
					++parts;
					along_width = along_width && std::fabs(o.x - g->x) < 1e-3f && std::fabs(o.z - g->z) > 0.5f * g->hv;
				}
				along_width = along_width && parts == 2;
			}
			crossed = unit_x(s, u) > 10.0f;
		}
		std::printf("  turned Iberian gate (hu %.2f, hv %.2f): spearman %s at x = %.2f, open parts %s\n", g->hu, g->hv,
				crossed ? "through" : "held", unit_x(s, u), along_width ? "along its width" : "across it");
		CHECK(crossed);
		CHECK(seen_open && along_width);
	}

	// 15. An open gate's Left and Right parts close the wall up to the towers at its ends, and its Door
	// stays open. 0.28 rasterizes each part onto 1 m navcells (CCmpObstructionManager::RasterizeHelper,
	// Rasterize.cpp RasterizeRectWithClearance: a navcell is blocked when its corners all lie within the
	// class clearance plus 1 m of the part), and the towers overlap the parts. Rings of wall round a
	// point, turned and moved off the navcell lattice: with the gate unlocked the inside reaches the
	// outside, locked it doesn't, and with the ground in its doorway impassable (Iberian starting walls
	// on a hill, as on Random Highlands, map 8 seed 1, gate 1298 at (65.73, 38.55)) neither does it: no
	// long path goes where a Left or Right part meets its tower.
	{
		struct Case { uint8_t kind, civ; int sides; const char *name; };
		const Case cases[] = {
			{WALLSET_STONE, CIV_IBERIANS, 8, "iberian stone"}, // the widest door, 19 of 36 m
			{WALLSET_STONE, CIV_HAN, 8, "han stone"},          // the narrowest, 8 of 36 m
			{WALLSET_STONE, CIV_ROMANS, 8, "roman stone"},
			{WALLSET_PALISADE, CIV_IBERIANS, 12, "palisade"},
			{WALLSET_SIEGE, CIV_ROMANS, 8, "roman siege"},
		};
		const float cell = 2.0f * s.nav_grid_.map_half / (float)s.nav_grid_.res;
		for (const Case &c : cases) {
			int runs = 0, door_open = 0, locked_shut = 0, junction_shut = 0;
			for (int t = 0; t < 12; ++t)
				for (int o = 0; o < 9; ++o) {
					const float pi = 3.14159265f, turn = (2.0f * pi / (float)c.sides) * (float)t / 12.0f;
					const float cx = 0.37f + 0.6f * (float)(o % 3), cz = -0.21f + 0.6f * (float)(o / 3);
					float r = 0.0f;
					const uint32_t gate = ring_with_gate(s, c.kind, c.civ, c.sides, cx, cz, turn, &r);
					++runs;
					const auto *g = find_bld(s, gate);
					if (!g || !is_gate_type(g->type) || g->gate_locked) continue;
					// Outside, across the ring from the gate.
					const float back = turn + pi + pi / (float)c.sides;
					const float ox = cx + (r + 8.0f) * std::cos(back), oz = cz + (r + 8.0f) * std::sin(back);
					door_open += nav_connected(s, cx, cz, ox, oz);
					// The doorway: the Door's width less the clearance and 1 m at each side (what 0.28 leaves open
					// to infantry), out two navcells either side of the wall.
					const bool along_v = g->hv > g->hu;
					const float half_w = along_v ? g->hv : g->hu, half_d = along_v ? g->hu : g->hv;
					const nav::Vec2 w = along_v ? nav::footprint_v(*g) : nav::Vec2(g->ux, g->uz);
					const float open = std::max(0.05f, half_w * gate_door_fraction(g->type, g->civ) - (CLEARANCE_DEFAULT + 0.25f));
					for (int y = 0; y < s.nav_grid_.res; ++y)
						for (int x = 0; x < s.nav_grid_.res; ++x)
							if (navcell_meets(s.nav_grid_, x, y, g->x, g->z, w.x, w.y, open, half_d + 2.0f * cell))
								s.nav_grid_.set_steep(x, y, true);
					junction_shut += !nav_connected(s, cx, cz, ox, oz);
					CHECK(s.cmd_lock_gate(0, gate, true));
					locked_shut += !nav_connected(s, cx, cz, ox, oz);
				}
			std::printf("  %s gate rings: door open %d/%d, locked shut %d/%d, doorway impassable shut %d/%d\n", c.name, door_open,
					runs, locked_shut, runs, junction_shut, runs);
			CHECK(door_open == runs && locked_shut == runs && junction_shut == runs);
		}
	}

	// 16. A second wall 7 to 8 m in front of an open gate (no gate of its own) stays shut: what keeps
	// the gate's door open on the navcells it shares with the pieces joined to the gate doesn't open
	// another wall's. 0.28 rasterizes each wall on its own (CCmpObstructionManager::RasterizeHelper).
	{
		struct Case { uint8_t kind, civ; int sides; const char *name; };
		const Case cases[] = {{WALLSET_PALISADE, CIV_IBERIANS, 12, "palisade"}, {WALLSET_SIEGE, CIV_ROMANS, 8, "roman siege"}};
		for (const Case &c : cases) {
			int runs = 0, outer_shut = 0;
			for (const float gap : {1.75f, 2.0f})
				for (int t = 0; t < 12; ++t)
					for (int o = 0; o < 9; ++o) {
						const float pi = 3.14159265f, turn = (2.0f * pi / (float)c.sides) * (float)t / 12.0f;
						const float cx = 0.13f + 2.0f * (float)(o % 3) / 3.0f, cz = -0.07f + 2.0f * (float)(o / 3) / 3.0f;
						float r = 0.0f;
						ring_with_gate(s, c.kind, c.civ, c.sides, cx, cz, turn, &r);
						// The same ring grown so its sides run `gap` outside the first's, all wall.
						const float ro = r + gap / std::cos(pi / (float)c.sides);
						const float len = 2.0f * ro * std::sin(pi / (float)c.sides) - wallset_base_info(c.kind).tower_half;
						for (int k = 0; k < c.sides; ++k) {
							const float a0 = turn + 2.0f * pi * (float)k / (float)c.sides, a1 = turn + 2.0f * pi * (float)(k + 1) / (float)c.sides;
							const float ax = cx + ro * std::cos(a0), az = cz + ro * std::sin(a0), bx = cx + ro * std::cos(a1), bz = cz + ro * std::sin(a1);
							const float angle = std::atan2(-(bz - az), bx - ax);
							s.spawn_map_wall_piece(0, c.kind, Simulation::MAP_WALL_SEGMENT, 0.5f * (ax + bx), 0.5f * (az + bz), angle, len);
							s.spawn_map_wall_piece(0, c.kind, Simulation::MAP_WALL_TOWER, ax, az, angle, 1.0f);
						}
						++runs;
						const float back = turn + pi + pi / (float)c.sides;
						outer_shut += !nav_connected(s, cx, cz, cx + (ro + 8.0f) * std::cos(back), cz + (ro + 8.0f) * std::sin(back));
					}
			std::printf("  %s gate inside a second wall: second wall shut %d/%d\n", c.name, outer_shut, runs);
			CHECK(outer_shut == runs);
		}
	}

	// 17. Slope closes the navcells in front of an open gate's door on one side, but on 0.28's 1 m
	// navcells a way runs from the door beside its Right part (CCmpPathfinder::TerrainUpdateHelper gives
	// each 1 m navcell the slope of its 4 m tile; the part blocks those within the clearance plus 1 m of
	// it): the part's navcell, the ground beside the part, stays open and the gate crossable, as on
	// Random Highlands, map 8 seed 1, gate 1384 at (54.24, -32.63). Locked, the gate shuts. Unlocked
	// again with the tower east of it wrecked (cmd_delete: Health.js HandleDeath destroys it, and its
	// rubble, template_rubble.xml, has no Obstruction), the way beside the part stays.
	{
		s.set_sim_seed(5);
		s.reset();
		s.player(0).active = 1, s.player(0).is_ai = 0;
		s.player(0).civ = CIV_IBERIANS;
		const float mh = s.map_half();
		const int hres = (int)std::lround(2.0f * mh) + 1;
		// Flat but for a cliff 8 high: every height at x <= 0, z >= 6. The navcells in front of the door
		// on the north, x -2 to 2 and z 4 to 6, reach its foot; the tiles x 0 to 1 and z 5 to 6 are its face.
		std::vector<float> h((size_t)hres * (size_t)hres, 0.0f);
		for (int j = 0; j < hres; ++j)
			for (int i = 0; i < hres; ++i)
				if (-mh + (float)i <= 0.0f && -mh + (float)j >= 6.0f) h[(size_t)j * hres + i] = 8.0f;
		s.set_terrain_data(h, hres, -1.0f);
		// An Iberian stone wall from edge to edge along z = 2.05, its gate centred on x = 0, a tower at
		// each of its ends.
		const WallSetInfo &ws = wallset_base_info(WALLSET_STONE);
		const float len = ws.lengths[0], z0 = 2.05f, side = len + ws.tower_half;
		const uint32_t gate = s.spawn_map_wall_piece(0, WALLSET_STONE, Simulation::MAP_WALL_GATE, 0.0f, z0, 0.0f, len);
		uint32_t east = 0;
		for (int k = 1; ((float)k - 0.5f) * side < mh; ++k)
			for (const float dir : {-1.0f, 1.0f}) {
				const uint32_t t = s.spawn_map_wall_piece(0, WALLSET_STONE, Simulation::MAP_WALL_TOWER, dir * ((float)k - 0.5f) * side, z0, 0.0f, 1.0f);
				if (k == 1 && dir > 0.0f) east = t;
				s.spawn_map_wall_piece(0, WALLSET_STONE, Simulation::MAP_WALL_SEGMENT, dir * (float)k * side, z0, 0.0f, len);
			}
		for (const auto &b : s.buildings()) const_cast<Simulation::Building &>(b).territory_decay_rate = 0.0f;
		s.rebuild_nav_grid();
		const auto *g = find_bld(s, gate);
		auto steep = [&](float x, float z) { const auto [cx, cy] = s.nav_grid_.world_to_cell(x, z); return s.nav_grid_.is_steep(cx, cy); };
		const bool front_steep = steep(-1.0f, 5.0f) && steep(1.0f, 5.0f) && !steep(3.0f, 5.0f) && !steep(-1.0f, 3.0f) && !steep(1.0f, 3.0f);
		const bool open = nav_connected(s, 0.0f, -24.0f, 12.0f, 24.0f);
		CHECK(g && is_gate_type(g->type) && !g->gate_locked && g->civ == CIV_IBERIANS);
		CHECK(s.cmd_lock_gate(0, gate, true));
		const bool locked_shut = !nav_connected(s, 0.0f, -24.0f, 12.0f, 24.0f);
		CHECK(s.cmd_lock_gate(0, gate, false));
		s.cmd_delete(0, &east, 1);
		const auto *tower = find_bld(s, east);
		const bool wrecked_open = tower && tower->wrecked && nav_connected(s, 0.0f, -24.0f, 12.0f, 24.0f);
		std::printf("  door front steep on the north (%s), 1 m way beside the part: %s; locked: %s; east tower wrecked: %s\n",
				front_steep ? "yes" : "no", open ? "crossable" : "shut", locked_shut ? "shut" : "crossable", wrecked_open ? "crossable" : "shut");
		CHECK(front_steep && open && locked_shut && wrecked_open);
	}

	// 18. Alpine Lakes (map 16) seed 16 with four Iberian players: each gets a starting wall (rmgen-common
	// player.js placeStartingWalls) round its civic centre on the lakes' slopes. On 0.28's 1 m navcells every
	// civic centre's ground reaches the others' through the gates, so the gates' parts must not wall player 3
	// in on the nav grid either; a long path reaches them too.
	{
		s.reset();
		s.set_sim_seed(16);
		s.set_waves_enabled(false);
		s.set_setup_flag(Simulation::SETUP_CIRCULAR_MAP, true);
		for (uint8_t p = 0; p < 4; ++p) s.set_team_civ(p, CIV_IBERIANS);
		s.generate_random_map((MapType)16, 16, 4, 128.0f);
		s.set_victory_condition(VICTORY_CONQUEST);
		for (uint8_t p = 0; p < 4; ++p) s.set_team_civ(p, CIV_IBERIANS);
		for (uint8_t p = 0; p < MAX_PLAYERS; ++p) s.set_player_active(p, p < 4, false);
		run(s, 5.0f * Simulation::FIXED_STEP);
		// The navcells round each player's civic centre, out to five a side, that are open.
		const NavGrid &g = s.nav_grid();
		std::vector<std::pair<int, int>> round[4];
		for (const auto &b : s.buildings()) {
			if (b.type != BLD_TOWN_CENTER || b.team >= 4 || !round[b.team].empty()) continue;
			const auto [cx, cy] = g.world_to_cell(b.x, b.z);
			for (int y = cy - 5; y <= cy + 5; ++y)
				for (int x = cx - 5; x <= cx + 5; ++x)
					if (g.cells.inside(x, y) && g.is_passable(x, y)) round[b.team].push_back({x, y});
		}
		// A 4-connected flood from player 3's.
		std::vector<uint8_t> seen((size_t)g.res * (size_t)g.res, 0);
		std::vector<std::pair<int, int>> open = round[3];
		for (const auto &[x, y] : open) seen[(size_t)y * g.res + x] = 1;
		while (!open.empty()) {
			const auto [x, y] = open.back();
			open.pop_back();
			const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
			for (const auto &e : d) {
				const int nx = x + e[0], ny = y + e[1];
				if (!g.cells.inside(nx, ny) || seen[(size_t)ny * g.res + nx] || !g.is_passable(nx, ny)) continue;
				seen[(size_t)ny * g.res + nx] = 1;
				open.push_back({nx, ny});
			}
		}
		int reached = 0;
		for (int p = 0; p < 3; ++p) {
			bool r = false;
			for (const auto &[x, y] : round[p]) r = r || seen[(size_t)y * g.res + x];
			reached += r;
		}
		std::printf("  Alpine Lakes seed 16, four Iberians: player 3's civic centre reaches %d of 3 others\n", reached);
		CHECK(!round[3].empty() && reached == 3);
	}

	// 19. Random Highlands (map 8) with four Iberian players, whose starting walls stand on its hills. Where a
	// slope closes the navcells in front of a door on one side, 0.28's 1 m navcells (CCmpPathfinder gives each
	// the slope of its 4 m tile, grown by the clearance; Rasterize.cpp blocks those within the clearance plus
	// 1 m of a part) may still cross beside the gate's Left or Right part, as in case 17: on seeds 1, 2 and 6
	// such a gate must stay crossable on the nav grid near it, and shut when locked. Where they cross nowhere
	// (the doorway's ground is impassable), the navcells holding a part and its tower must not carry a long
	// path across either, as on seeds 1 and 4: 0.28's long paths take another gate. Where two players'
	// walls cross, as Random Valley's seed 6 bases once did (two of its four bases stood side by side, before
	// the map's River placement was restored), player 1's gate at (56.71, -10.21) has its Door over player
	// 3's gate's Left part, and 0.28's 1 m navcells cross it there, so the navcells must too, whatever the
	// other gate's check decides for its parts. Those walls are built here as that map placed them.
	{
		struct Pin { MapType map; uint32_t seed; float x, z; bool crosses; };
		const Pin pins[] = {
			{MAP_RANDOM_HIGHLANDS, 1, 54.24f, -32.63f, true}, {MAP_RANDOM_HIGHLANDS, 1, 65.73f, 38.55f, false},
			{MAP_RANDOM_HIGHLANDS, 2, -47.78f, -34.26f, true}, {MAP_RANDOM_HIGHLANDS, 4, 63.00f, 37.10f, false},
			{MAP_RANDOM_HIGHLANDS, 6, 47.13f, 72.75f, true},
		};
		int made = -1;
		for (const Pin &pin : pins) {
			if ((int)(pin.map * 1000 + pin.seed) != made) {
				s.reset();
				s.set_sim_seed(pin.seed);
				s.set_waves_enabled(false);
				s.set_setup_flag(Simulation::SETUP_CIRCULAR_MAP, false);
				for (uint8_t p = 0; p < 4; ++p) s.set_team_civ(p, CIV_IBERIANS);
				s.generate_random_map(pin.map, pin.seed, 4, 128.0f);
				s.set_victory_condition(VICTORY_CONQUEST);
				for (uint8_t p = 0; p < 4; ++p) s.set_team_civ(p, CIV_IBERIANS);
				for (uint8_t p = 0; p < MAX_PLAYERS; ++p) s.set_player_active(p, p < 4, false);
				run(s, 5.0f * Simulation::FIXED_STEP);
				made = (int)(pin.map * 1000 + pin.seed);
			}
			const Simulation::Building *g = nullptr;
			for (const auto &b : s.buildings())
				if (is_gate_type(b.type) && !b.gate_locked && std::hypot(b.x - pin.x, b.z - pin.z) < 0.5f) g = &b;
			CHECK(g != nullptr);
			if (!g) continue;
			const uint32_t id = g->id;
			const uint8_t team = g->team;
			const bool open = gate_crossed_near(s, *g, 7.0f, 3.0f);
			bool locked_shut = true;
			if (pin.crosses) {
				CHECK(s.cmd_lock_gate(team, id, true));
				const auto *gl = find_bld(s, id);
				locked_shut = gl && !gate_crossed_near(s, *gl, 7.0f, 3.0f);
				CHECK(s.cmd_lock_gate(team, id, false)); // a later pin on this map sees the gate open again
			}
			std::printf("  map %d seed %u, gate at (%.2f, %.2f): %s on the nav grid (0.28: %s)%s\n", (int)pin.map, pin.seed, pin.x, pin.z,
					open ? "crosses" : "shut", pin.crosses ? "crosses" : "shut", pin.crosses ? (locked_shut ? "; locked: shut" : "; locked: crosses") : "");
			CHECK(open == pin.crosses && locked_shut);
		}
		{
			s.set_sim_seed(6);
			s.reset();
			s.set_map_half(128.0f);
			for (uint8_t p = 0; p < 4; ++p) s.player(p).active = 1, s.player(p).is_ai = 0, s.set_team_civ(p, CIV_IBERIANS);
			// Player 1's pieces then player 3's: piece, owner, centre, rotation, length.
			struct Piece { uint8_t piece, team; float x, z, rot, len; };
			const Piece pieces[] = {
				{Simulation::MAP_WALL_SEGMENT, 1, 74.2906f, -3.3222f, 2.870980f, 9.0f}, {Simulation::MAP_WALL_SEGMENT, 1, 63.9153f, -5.0131f, 3.089092f, 9.0f},
				{Simulation::MAP_WALL_TOWER, 1, 69.1956f, -4.7357f, 2.980036f, 2.0f}, {Simulation::MAP_WALL_GATE, 1, 56.7051f, -10.2133f, 1.944438f, 9.0f},
				{Simulation::MAP_WALL_TOWER, 1, 58.6351f, -5.2906f, 2.516765f, 2.0f}, {Simulation::MAP_WALL_SEGMENT, 1, 52.9938f, -20.1144f, 1.914414f, 9.0f},
				{Simulation::MAP_WALL_TOWER, 1, 54.7752f, -15.1360f, 1.929426f, 2.0f}, {Simulation::MAP_WALL_TOWER, 1, 51.2125f, -25.0928f, 1.520547f, 2.0f},
				{Simulation::MAP_WALL_TOWER, 3, 60.1352f, 8.1011f, 2.380916f, 2.0f}, {Simulation::MAP_WALL_SEGMENT, 3, 53.1577f, -4.3682f, 1.431011f, 9.0f},
				{Simulation::MAP_WALL_GATE, 3, 58.9172f, -11.2560f, 0.317733f, 9.0f}, {Simulation::MAP_WALL_TOWER, 3, 53.8944f, -9.6041f, 0.874372f, 2.0f},
				{Simulation::MAP_WALL_SEGMENT, 3, 67.4311f, -16.8791f, 0.849654f, 9.0f}, {Simulation::MAP_WALL_TOWER, 3, 63.9401f, -12.9079f, 0.583693f, 2.0f},
				{Simulation::MAP_WALL_TOWER, 3, 70.9221f, -20.8503f, 0.216459f, 2.0f},
			};
			uint32_t id = 0;
			for (const Piece &p : pieces) {
				const uint32_t b = s.spawn_map_wall_piece(p.team, WALLSET_STONE, p.piece, p.x, p.z, p.rot, p.len);
				if (p.piece == Simulation::MAP_WALL_GATE && p.team == 1) id = b;
			}
			// No civic centre holds this ground: keep the walls their owners'.
			for (const auto &b : s.buildings()) const_cast<Simulation::Building &>(b).territory_decay_rate = 0.0f;
			run(s, 5.0f * Simulation::FIXED_STEP);
			const auto *g = find_bld(s, id);
			CHECK(g && is_gate_type(g->type) && !g->gate_locked);
			const bool open = g && gate_crossed_near(s, *g, 7.0f, 3.0f);
			CHECK(s.cmd_lock_gate(1, id, true));
			g = find_bld(s, id);
			const bool locked_shut = g && !gate_crossed_near(s, *g, 7.0f, 3.0f);
			CHECK(s.cmd_lock_gate(1, id, false));
			std::printf("  crossing walls, player 1's gate at (56.71, -10.21): %s on the nav grid (0.28: crosses)%s\n",
					open ? "crosses" : "shut", locked_shut ? "; locked: shut" : "; locked: crosses");
			CHECK(open && locked_shut);
			// West of player 3's wall, at (51, -11), 0.28's 1 m navcells pass between its tower at (53.89, -9.60)
			// and player 1's at (54.78, -15.14), then beside player 3's gate's Left part, to the ground between
			// the gates at (63, -9). Those of that part's navcells must stay open, though its door crosses at
			// 8 m: within 9 of player 1's gate the nav grid joins them too.
			const bool junction = nav_connected_near(s, 51.0f, -11.0f, 63.0f, -9.0f, 56.71f, -10.21f, 9.0f);
			std::printf("  crossing walls, between the towers to between the gates: %s (0.28: joined)\n", junction ? "joined" : "apart");
			CHECK(junction);
		}
	}

	// 20. As case 17, but the slope reaches past the gate's centre: the ridge's steep tiles lie north of the
	// Door's whole open middle (9.5 m each side of its centre, less the clearance and 1 m), and 0.28's 1 m
	// navcells cross nowhere, so the nav grid must not either. With the tower east of the gate wrecked
	// (cmd_delete: Health.js HandleDeath destroys it, and its rubble, template_rubble.xml, has no
	// Obstruction) they cross through the 2 m its pieces leave between the gate's Right part and the next
	// segment (each reaches 2 m into the tower, WallSet MinTowerOverlap, and blocks the 1 m navcells within
	// the clearance plus 1 m of it, Rasterize.cpp RasterizeRectWithClearance), and so must the nav grid.
	// The gate is moved off the navcell lattice; where the segment's own navcells close the gap at 8 m too
	// (the WallSet segment rule in rebuild_nav_grid) it is left out.
	{
		struct Place { float ox, oz; };
		const Place places[] = {{0.5f, 0.0f}, {0.75f, 0.0f}, {1.0f, 0.0f}, {1.25f, 0.0f}, {1.5f, 0.0f}, {1.75f, 0.0f}, {0.5f, 1.5f}, {0.75f, 1.5f}};
		int runs = 0, live_shut = 0, wrecked_open = 0;
		for (const Place &pl : places) {
			s.set_sim_seed(5);
			s.reset();
			s.player(0).active = 1, s.player(0).is_ai = 0;
			s.player(0).civ = CIV_IBERIANS;
			const float mh = s.map_half();
			const int hres = (int)std::lround(2.0f * mh) + 1;
			const float z0 = 2.05f + pl.oz;
			// A ridge 8 high along the first row of heights 1.5 north of the wall, from the west edge to 2
			// past the gate's centre: the tiles either side of it are steep.
			const int zr = (int)std::ceil(z0 + 1.5f);
			const float xe = std::floor(pl.ox + 2.0f);
			std::vector<float> h((size_t)hres * (size_t)hres, 0.0f);
			for (int j = 0; j < hres; ++j)
				for (int i = 0; i < hres; ++i)
					if (-mh + (float)j == (float)zr && -mh + (float)i <= xe) h[(size_t)j * hres + i] = 8.0f;
			s.set_terrain_data(h, hres, -1.0f);
			const WallSetInfo &ws = wallset_base_info(WALLSET_STONE);
			const float len = ws.lengths[0], side = len + ws.tower_half;
			const uint32_t gate = s.spawn_map_wall_piece(0, WALLSET_STONE, Simulation::MAP_WALL_GATE, pl.ox, z0, 0.0f, len);
			uint32_t east = 0;
			for (int k = 1; ((float)k - 0.5f) * side < mh + side; ++k)
				for (const float dir : {-1.0f, 1.0f}) {
					const uint32_t t = s.spawn_map_wall_piece(0, WALLSET_STONE, Simulation::MAP_WALL_TOWER, pl.ox + dir * ((float)k - 0.5f) * side, z0, 0.0f, 1.0f);
					if (k == 1 && dir > 0.0f) east = t;
					s.spawn_map_wall_piece(0, WALLSET_STONE, Simulation::MAP_WALL_SEGMENT, pl.ox + dir * (float)k * side, z0, 0.0f, len);
				}
			for (const auto &b : s.buildings()) const_cast<Simulation::Building &>(b).territory_decay_rate = 0.0f;
			s.rebuild_nav_grid();
			const auto *g = find_bld(s, gate);
			CHECK(g && is_gate_type(g->type) && !g->gate_locked);
			++runs;
			live_shut += !nav_connected(s, pl.ox, z0 - 24.0f, pl.ox + 12.0f, z0 + 24.0f);
			s.cmd_delete(0, &east, 1);
			const auto *t = find_bld(s, east);
			wrecked_open += t && t->wrecked && nav_connected(s, pl.ox, z0 - 24.0f, pl.ox + 12.0f, z0 + 24.0f);
		}
		std::printf("  ridge across the doorway: towers standing, shut %d/%d; east tower wrecked, crossable %d/%d\n", live_shut, runs, wrecked_open, runs);
		CHECK(live_shut == runs && wrecked_open == runs);
	}

	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
