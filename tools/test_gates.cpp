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
		s.cmd_move(&u, 1, -20.0f, 1.0f);
		run(s, 4.0f);
		CHECK(!find_bld(s, gate)->gate_open);
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

	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
