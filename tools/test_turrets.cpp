// 0 A.D. TurretHolder / Turretable: ranged infantry on the turret points of walls, gates,
// outposts and the Mauryan rampart tower. They stay visible and alive up there, shoot from the point's height,
// hold their ground, and finish any order but an attack up there until they leave the turret.
//   g++ -std=c++20 -O2 -Isrc tools/test_turrets.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_turrets && /tmp/test_turrets
#define private public
#include "simulation/simulation.h"
#undef private
#include "network/turn_manager.h"
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static void run(Simulation &s, float seconds) {
	for (int t = 0; t < (int)std::lround(seconds / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
}

static void fresh(Simulation &s) {
	s.set_sim_seed(9);
	s.reset();
	s.set_player_civ(CIV_ATHENIANS);
	s.set_victory_condition(VICTORY_ENDLESS);
	s.spawn_building_world(-50.0f, -50.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(50.0f, 50.0f, BLD_TOWN_CENTER, 1, 1.0f);
}

static uint16_t tmpl_of(const char *id) {
	for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT; ++t)
		if (std::strcmp(civ_unit_template(t).id, id) == 0) return t;
	std::printf("no roster template %s\n", id);
	++failures;
	return CIV_UNIT_TEMPLATE_NONE;
}

static const Simulation::Building &bld(Simulation &s, uint32_t id) { return s.buildings_[s.building_id_to_idx_.at(id)]; }

int main() {
	// 1. The generated points: an Athenian wall segment holds 4 (two rows), a gate 2, an
	//    outpost 1 at 8 m (2.24 RTS0 units). The Kushite gate's 6 points, on its two towers,
	//    are pulled onto the one-cell gate's ends rather than dropped.
	{
		CHECK(TURRET_POINTS[CIV_ATHENIANS][0].count == 4);
		CHECK(TURRET_POINTS[CIV_ATHENIANS][1].count == 2);
		CHECK(TURRET_POINTS[CIV_ATHENIANS][2].count == 1);
		CHECK(std::fabs(TURRET_POINTS[CIV_ATHENIANS][2].pt[0].y - 2.24f) < 1e-4f);
		CHECK(TURRET_POINTS[CIV_KUSHITES][1].count == 6);
		for (int k = 0; k < 6; ++k) CHECK(std::fabs(std::fabs(TURRET_POINTS[CIV_KUSHITES][1].pt[k].x) - 1.0f) < 1e-4f);
	}

	static Simulation s;
	const uint16_t archer = tmpl_of("athen/infantry_archer_b");
	const uint16_t hoplite = tmpl_of("athen/infantry_spearman_b");

	// 2. Archers ordered onto a wall stand on its points, alive and visible, at its height;
	//    a hoplite (melee, not Turretable) is refused; a fifth archer finds no free point.
	uint32_t wall = 0, a[5] = {}, hop = 0;
	{
		fresh(s);
		wall = s.spawn_building_world(0.0f, 0.0f, BLD_WALL_SEGMENT, 0, 1.0f);
		CHECK(bld(s, wall).garrison_cap == 4);
		for (int k = 0; k < 5; ++k) a[k] = s.spawn_unit(-3.0f + k * 0.8f, 3.0f, 0, UNIT_ARCHER, 0.0f, archer);
		hop = s.spawn_unit(0.0f, -3.0f, 0, UNIT_SPEARMAN, 0.0f, hoplite);
		CHECK(s.unit_turretable((size_t)s.lookup_unit(a[0])));
		CHECK(!s.unit_turretable((size_t)s.lookup_unit(hop)));
		s.order_garrison(a, 5, wall);
		s.order_garrison(&hop, 1, wall);
		run(s, 6.0f);
		const auto &list = bld(s, wall).garrisoned_units;
		CHECK(list.size() == 4);
		int up = 0;
		for (int k = 0; k < 5; ++k) {
			const int32_t i = s.lookup_unit(a[k]);
			if (s.turret_holder_of(a[k]) != wall) continue;
			++up;
			CHECK(s.units_.alive[i]);
			CHECK(std::fabs(s.unit_height_offset((size_t)i) - 3.22f) < 1e-3f);
			CHECK(std::fabs(s.units_.pos_x[i]) < 0.5f && std::fabs(s.units_.pos_z[i]) < 0.5f);
			CHECK(s.units_.stance[i] == STANCE_STAND_GROUND);
		}
		CHECK(up == 4);
		const int32_t h = s.lookup_unit(hop);
		CHECK(s.units_.alive[h] && s.turret_holder_of(hop) == 0);
		std::printf("wall: %d archers on its turret points, the hoplite stays down\n", up);
		// UnitAI.Garrison does nothing while the unit is a turret: it stays on the wall.
		const uint32_t tower = s.spawn_building_world(12.0f, 12.0f, BLD_TOWER, 0, 1.0f);
		const uint32_t on_wall = bld(s, wall).garrisoned_units[0];
		s.order_garrison(&on_wall, 1, tower);
		run(s, 4.0f);
		CHECK(s.turret_holder_of(on_wall) == wall && bld(s, tower).garrisoned_units.empty());
	}

	// 3. An enemy walks up: the turret archers shoot it from the wall and don't move.
	{
		const int32_t i0 = s.lookup_unit(bld(s, wall).garrisoned_units[0]);
		const float x0 = s.units_.pos_x[i0], z0 = s.units_.pos_z[i0];
		uint32_t foe = s.spawn_unit(0.0f, 9.0f, 1, UNIT_SWORDSMAN);
		s.units_.stance[s.lookup_unit(foe)] = STANCE_PASSIVE;
		const float hp0 = s.units_.max_hp[s.lookup_unit(foe)];
		run(s, 6.0f);
		const int32_t f = s.lookup_unit(foe);
		CHECK(f < 0 || !s.units_.alive[f] || s.units_.hp[f] < hp0);
		const int32_t i1 = s.lookup_unit(bld(s, wall).garrisoned_units[0]);
		CHECK(s.units_.pos_x[i1] == x0 && s.units_.pos_z[i1] == z0);
		CHECK(bld(s, wall).garrisoned_units.size() == 4);
		std::printf("turret archers hit the swordsman (hp %.0f of %.0f)\n", f >= 0 ? s.units_.hp[f] : 0.0f, hp0);
	}

	// 4. Save/load keeps the turrets and the lockstep hash.
	{
		const char *path = "/tmp/rts0_test_turrets.sav";
		CHECK(s.save_state(path));
		static Simulation l;
		CHECK(l.load_state(path));
		CHECK(TurnManager().boot_sim_hash(s) == TurnManager().boot_sim_hash(l));
		run(s, 2.0f);
		run(l, 2.0f);
		CHECK(TurnManager().boot_sim_hash(s) == TurnManager().boot_sim_hash(l));
		CHECK(l.turret_holder_of(bld(l, wall).garrisoned_units[0]) == wall);
		std::remove(path);
	}

	// 5. Turretable.OccupyTurret makes the unit immobile (UnitAI.SetTurretStance): a walk or a
	//    gather order finishes and it stays up, a stance command is ignored (Commands.js
	//    "stance" skips turrets); "leave-turret" takes it down beside the wall with its stance
	//    back; a killed turret unit leaves its point; unloading the wall brings the rest down.
	{
		const uint32_t first = bld(s, wall).garrisoned_units[0];
		const int32_t i = s.lookup_unit(first);
		const float x0 = s.units_.pos_x[i], z0 = s.units_.pos_z[i];
		s.cmd_move(&first, 1, 0.0f, 12.0f);
		run(s, 1.0f);
		CHECK(s.turret_holder_of(first) == wall && s.units_.order_kind[i] == ORDER_NONE);
		CHECK(s.units_.pos_x[i] == x0 && s.units_.pos_z[i] == z0);
		const uint32_t tree = s.spawn_resource_world(4.0f, 4.0f, RES_WOOD, 200);
		s.order_gather(&first, 1, tree);
		run(s, 1.0f);
		CHECK(s.turret_holder_of(first) == wall && s.units_.order_kind[i] == ORDER_NONE);
		s.order_set_stance(&first, 1, STANCE_AGGRESSIVE);
		CHECK(s.units_.stance[i] == STANCE_STAND_GROUND);
		std::printf("turret archer: stays up through a walk, a gather and a stance change\n");
		s.cmd_leave_turret(0, &first, 1);
		run(s, 0.2f);
		CHECK(s.turret_holder_of(first) == 0);
		CHECK(bld(s, wall).garrisoned_units.size() == 3);
		CHECK(s.unit_height_offset((size_t)i) == 0.0f);
		CHECK(s.units_.stance[i] == unit_default_stance(UNIT_ARCHER));
		CHECK(std::fabs(s.units_.pos_x[i]) > 1.0f || std::fabs(s.units_.pos_z[i]) > 1.0f);

		const uint32_t second = bld(s, wall).garrisoned_units[0];
		const int32_t j = s.lookup_unit(second);
		s.units_.hp[j] = 0.0f;
		s.units_.alive[j] = 0;
		s.units_.action_state[j] = ACTION_DYING;
		run(s, 0.2f);
		CHECK(bld(s, wall).garrisoned_units.size() == 2);

		s.order_ungarrison(wall);
		run(s, 0.2f);
		CHECK(bld(s, wall).garrisoned_units.empty());
		CHECK(s.turret_height_.empty());
	}

	// 6. An outpost takes one archer at 2.24; a wall segment no longer shoots arrows itself.
	{
		fresh(s);
		const uint32_t post = s.spawn_building_world(10.0f, 10.0f, BLD_OUTPOST, 0, 1.0f);
		uint32_t two[2] = {s.spawn_unit(10.0f, 13.0f, 0, UNIT_ARCHER, 0.0f, archer),
				s.spawn_unit(11.0f, 13.0f, 0, UNIT_ARCHER, 0.0f, archer)};
		// A passive unit holds its ground up there too (SetTurretStance: standground is the
		// only stance with respondStandGround), and is passive again once down.
		for (uint32_t id : two) s.units_.stance[s.lookup_unit(id)] = STANCE_PASSIVE;
		s.order_garrison(two, 2, post);
		run(s, 5.0f);
		CHECK(bld(s, post).garrisoned_units.size() == 1);
		const int32_t up = s.lookup_unit(bld(s, post).garrisoned_units[0]);
		CHECK(std::fabs(s.unit_height_offset((size_t)up) - 2.24f) < 1e-3f);
		std::printf("outpost: 1 archer at height %.2f\n", s.unit_height_offset((size_t)up));
		CHECK(s.units_.stance[up] == STANCE_STAND_GROUND);
		const uint32_t up_id = s.units_.id[up];
		s.cmd_unload(0, post, &up_id, 1);
		run(s, 0.2f);
		CHECK(s.turret_holder_of(up_id) == 0 && s.units_.stance[s.lookup_unit(up_id)] == STANCE_PASSIVE);
	}

	// 7. WallSet pieces hold their own length's points (wall_long 16, wall_medium 8, wall_gate 8,
	//    wall_short none), turned with the wall: archers on a long segment laid along z stand
	//    along z, within its length, at wall_long's height.
	{
		fresh(s);
		const float L0 = WALL_SEGMENT_LENGTHS[0], L1 = WALL_SEGMENT_LENGTHS[1], L2 = WALL_SEGMENT_LENGTHS[2];
		const float along_z = -std::atan2(1.0f, 0.0f);
		const uint32_t lng = s.spawn_wall_piece({BLD_WALL_SEGMENT, 0.0f, 0.0f, along_z, 0.5f * L0}, 0, 1.0f);
		const uint32_t med = s.spawn_wall_piece({BLD_WALL_SEGMENT, 20.0f, 0.0f, 0.0f, 0.5f * L1}, 0, 1.0f);
		const uint32_t sht = s.spawn_wall_piece({BLD_WALL_SEGMENT, 20.0f, 20.0f, 0.0f, 0.5f * L2}, 0, 1.0f);
		CHECK(bld(s, lng).garrison_cap == 16);
		CHECK(bld(s, med).garrison_cap == 8);
		CHECK(bld(s, sht).garrison_cap == 0);
		const float y = TURRET_POINTS_WALLSET[CIV_ATHENIANS][0].pt[0].y;
		uint32_t up[16];
		for (int k = 0; k < 16; ++k) up[k] = s.spawn_unit(-3.0f + (k % 8) * 0.8f, -6.0f - (k / 8) * 0.8f, 0, UNIT_ARCHER, 0.0f, archer);
		s.order_garrison(up, 16, lng);
		run(s, 10.0f);
		CHECK(bld(s, lng).garrisoned_units.size() == 16);
		float zmin = 1e9f, zmax = -1e9f, xmax = 0.0f;
		for (uint32_t id : bld(s, lng).garrisoned_units) {
			const int32_t i = s.lookup_unit(id);
			CHECK(std::fabs(s.unit_height_offset((size_t)i) - y) < 1e-3f);
			zmin = std::fmin(zmin, s.units_.pos_z[i]);
			zmax = std::fmax(zmax, s.units_.pos_z[i]);
			xmax = std::fmax(xmax, std::fabs(s.units_.pos_x[i]));
		}
		CHECK(zmax - zmin > 0.5f * L0 && zmax <= 0.5f * L0 + 1e-3f && zmin >= -0.5f * L0 - 1e-3f);
		CHECK(xmax < WALL_HALF_DEPTH + 0.5f);
		std::printf("long WallSet segment: 16 archers at %.2f, spread %.2f along it, %.2f across\n", y, zmax - zmin, xmax);
	}

	// 8. The Mauryan rampart tower (maur/tower_double) has a garrison and 16 turret points:
	//    "occupy-turret" (session.occupyturret) puts archers on the points, 8 at 12.5 m and 8 at
	//    18 m, while a plain garrison order still goes inside; a spearman can't occupy a point,
	//    and a 17th archer finds none free. The turret units survive a save, step down with
	//    "leave-turret" or the tower's "unload-turrets", and come down when the tower falls.
	{
		fresh(s);
		s.set_player_civ(CIV_MAURYANS);
		const uint16_t bow = tmpl_of("maur/infantry_archer_b");
		const uint16_t spear = tmpl_of("maur/infantry_spearman_b");
		const uint32_t tw = s.spawn_building_world(0.0f, 0.0f, BLD_TOWER_DOUBLE, 0, 1.0f);
		CHECK(s.turret_points_of(bld(s, tw)) == nullptr);
		CHECK(s.turret_holder_points(bld(s, tw)) == &TURRET_POINTS_TOWER_DOUBLE);
		CHECK(TURRET_POINTS_TOWER_DOUBLE.count == 16);
		uint32_t up[17], in[2];
		for (int k = 0; k < 17; ++k) up[k] = s.spawn_unit(-4.0f + (k % 9) * 0.8f, 5.0f + (k / 9) * 0.8f, 0, UNIT_ARCHER, 0.0f, bow);
		for (int k = 0; k < 2; ++k) in[k] = s.spawn_unit(-4.0f + k * 0.8f, -5.0f, 0, UNIT_ARCHER, 0.0f, bow);
		const uint32_t sp = s.spawn_unit(4.0f, -5.0f, 0, UNIT_SPEARMAN, 0.0f, spear);
		CHECK(s.can_occupy_turret_in(up[0], tw) && !s.can_occupy_turret_in(sp, tw));
		s.order_garrison(up, 17, tw, false, true);
		s.order_garrison(in, 2, tw);
		s.order_garrison(&sp, 1, tw, false, true);
		run(s, 10.0f);
		const auto &b = bld(s, tw);
		CHECK(b.turret_units.size() == 16);
		CHECK(b.garrisoned_units.size() == 2);
		int low = 0, high = 0;
		for (uint32_t id : b.turret_units) {
			const int32_t i = s.lookup_unit(id);
			CHECK(s.units_.alive[i] && s.turret_holder_of(id) == tw);
			const float h = s.unit_height_offset((size_t)i);
			low += std::fabs(h - 3.125f) < 1e-3f;
			high += std::fabs(h - 4.5f) < 1e-3f;
			CHECK(std::fabs(s.units_.pos_x[i]) < 0.6f && std::fabs(s.units_.pos_z[i]) < 0.6f);
		}
		CHECK(low == 8 && high == 8);
		// Whichever archer comes last finds no point free and stays down (arrival order follows the paths).
		int down = 0;
		for (int k = 0; k < 17; ++k) down += s.turret_holder_of(up[k]) == 0 && s.units_.alive[s.lookup_unit(up[k])];
		CHECK(down == 1);
		CHECK(s.turret_holder_of(sp) == 0 && s.units_.alive[s.lookup_unit(sp)]);
		std::printf("rampart tower: %d archers at 12.5 m, %d at 18 m, %zu inside\n", low, high, b.garrisoned_units.size());
		// tower_double has a TurretHolder but no Auras: its archers get none of wall_garrisoned's
		// +3 resistances or +20 m vision (the archer left down stands in for an unaided one).
		{
			int32_t ground = -1;
			for (int k = 0; k < 17; ++k)
				if (s.turret_holder_of(up[k]) == 0) ground = s.lookup_unit(up[k]);
			const int32_t top = s.lookup_unit(b.turret_units[0]);
			float r_top[3], r_ground[3];
			s.unit_resistance((size_t)top, r_top);
			s.unit_resistance((size_t)ground, r_ground);
			CHECK(ground >= 0 && r_top[0] == r_ground[0] && r_top[1] == r_ground[1] && r_top[2] == r_ground[2]);
			CHECK(s.unit_vision_at((size_t)top) == s.unit_vision_at((size_t)ground));
		}

		const char *path = "/tmp/rts0_test_turrets_tower.sav";
		CHECK(s.save_state(path));
		static Simulation l;
		CHECK(l.load_state(path));
		CHECK(bld(l, tw).turret_units == b.turret_units && l.turret_height_.size() == 16);
		CHECK(TurnManager().boot_sim_hash(s) == TurnManager().boot_sim_hash(l));
		run(s, 1.0f);
		run(l, 1.0f);
		CHECK(TurnManager().boot_sim_hash(s) == TurnManager().boot_sim_hash(l));
		std::remove(path);

		const uint32_t first = b.turret_units[0];
		s.cmd_leave_turret(0, &first, 1);
		run(s, 0.2f);
		CHECK(s.turret_holder_of(first) == 0 && b.turret_units.size() == 15);
		CHECK(s.units_.stance[s.lookup_unit(first)] == unit_default_stance(UNIT_ARCHER));
		s.cmd_unload_turrets(1, tw); // not the owner's, nor an ally's
		CHECK(b.turret_units.size() == 15);
		s.cmd_unload_turrets(0, tw);
		run(s, 0.2f);
		CHECK(b.turret_units.empty() && b.garrisoned_units.size() == 2 && s.turret_height_.empty());
		// Each steps onto its own spawn spot, none two on one.
		int stacked = 0;
		for (int p = 0; p < 17; ++p)
			for (int q = p + 1; q < 17; ++q) {
				const int32_t i = s.lookup_unit(up[p]), j = s.lookup_unit(up[q]);
				const float dx = s.units_.pos_x[i] - s.units_.pos_x[j], dz = s.units_.pos_z[i] - s.units_.pos_z[j];
				stacked += dx * dx + dz * dz < 0.01f;
			}
		CHECK(stacked == 0);

		// The rest walk off: the stepped-down archers stand all round the tower's foot.
		s.cmd_move(&up[4], 13, 0.0f, 12.0f);
		run(s, 4.0f);
		s.order_garrison(up, 4, tw, false, true);
		run(s, 6.0f);
		CHECK(bld(s, tw).turret_units.size() == 4);
		s.destroy_building(s.buildings_[s.building_id_to_idx_.at(tw)]);
		run(s, 0.2f);
		CHECK(bld(s, tw).turret_units.empty() && s.turret_height_.empty());
		for (int k = 0; k < 4; ++k) CHECK(s.units_.alive[s.lookup_unit(up[k])]);
		std::printf("rampart tower: turret units step down, unload and survive its fall\n");
	}

	// 9. Order.Attack with !AbleToMove: a turret archer told to attack a unit out of its reach
	//    drops the order and shoots the swordsman in range instead (ShouldChaseTargetedEntity).
	{
		fresh(s);
		s.set_reveal_map(true);
		const uint32_t w = s.spawn_building_world(0.0f, 0.0f, BLD_WALL_SEGMENT, 0, 1.0f);
		uint32_t up = s.spawn_unit(0.0f, 3.0f, 0, UNIT_ARCHER, 0.0f, archer);
		s.order_garrison(&up, 1, w);
		run(s, 4.0f);
		CHECK(s.turret_holder_of(up) == w);
		uint32_t far = s.spawn_unit(0.0f, 45.0f, 1, UNIT_SPEARMAN);
		s.units_.stance[s.lookup_unit(far)] = STANCE_PASSIVE;
		s.order_attack(&up, 1, far);
		uint32_t near = s.spawn_unit(0.0f, 9.0f, 1, UNIT_SWORDSMAN);
		s.units_.stance[s.lookup_unit(near)] = STANCE_STAND_GROUND; // stays put (a passive one runs off)
		const float hp0 = s.units_.max_hp[s.lookup_unit(near)];
		run(s, 8.0f);
		const int32_t ui = s.lookup_unit(up), ni = s.lookup_unit(near);
		CHECK(s.units_.order_kind[ui] != ORDER_ATTACK || s.units_.target_unit_id[ui] != far);
		CHECK(ni < 0 || !s.units_.alive[ni] || s.units_.hp[ni] < hp0);
		std::printf("out-of-reach order: turret archer order %d, swordsman hp %.0f of %.0f\n", s.units_.order_kind[ui],
				ni >= 0 ? s.units_.hp[ni] : 0.0f, hp0);
	}

	if (failures) {
		std::printf("test_turrets: %d failure(s)\n", failures);
		return 1;
	}
	std::printf("test_turrets: all passed\n");
	return 0;
}
