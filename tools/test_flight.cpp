// UnitMotionFlying tests (units/plane, Flight Demo): a plane takes off along the ground, climbs
// to its flying height, circles a walk's point, is out of a swordsman's reach in the air, lands
// when stopped, dies landing on water, and its flight survives a save/load round trip. It takes
// one Support or Infantry passenger on board while on the ground, none in the air, and whoever
// is aboard dies with it.
//   g++ -std=c++20 -O2 -Isrc tools/test_flight.cpp src/simulation/simulation.cpp
//       src/simulation/rms_generator.cpp src/core/job_system.cpp -lpthread -o /tmp/test_flight
//       && /tmp/test_flight
#include "simulation/simulation.h"
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static int32_t index_of(const Simulation &s, uint32_t uid) {
	auto v = s.units();
	for (size_t i = 0; i < v.count; ++i)
		if (v.id[i] == uid) return (int32_t)i;
	return -1;
}

static uint16_t roster_id(const char *id) {
	for (uint16_t i = 0; i < CIV_UNIT_TEMPLATE_COUNT; ++i)
		if (std::strcmp(civ_unit_template(i).id, id) == 0) return i;
	return CIV_UNIT_TEMPLATE_NONE;
}

static constexpr float kDt = 0.05f;

static void run(Simulation &s, float seconds) {
	for (int k = 0; k < (int)std::lround(seconds / kDt); ++k) s.tick(kDt);
}

static float speed_of(const Simulation &s, uint32_t uid) {
	const int32_t i = index_of(s, uid);
	return i < 0 ? -1.0f : std::sqrt(s.units().vel_x[i] * s.units().vel_x[i] + s.units().vel_z[i] * s.units().vel_z[i]);
}

int main() {
	const uint16_t plane = roster_id("plane");
	CHECK(plane != CIV_UNIT_TEMPLATE_NONE);
	if (plane == CIV_UNIT_TEMPLATE_NONE) return 1;
	CHECK(civ_unit_template(plane).flags & ROSTER_FLYING);
	CHECK(std::fabs(civ_unit_template(plane).speed - PLANE_FLIGHT.max_speed) < 1e-3f); // GetWalkSpeed
	CHECK(std::fabs(unit_min_range_ratio(UNIT_BOLT_SHOOTER, plane) - 80.0f / 120.0f) < 1e-6f);

	static Simulation s;
	s.set_sim_seed(3);
	s.set_team_civ(0, CIV_ATHENIANS);
	s.set_team_civ(1, CIV_ROMANS);
	s.reset();
	s.set_map_half(120.0f);

	// Spawned on the ground, with no target: it stays put.
	uint32_t p = s.spawn_unit(0.0f, 0.0f, 0, UNIT_BOLT_SHOOTER, 0.0f, plane);
	int32_t pi = index_of(s, p);
	CHECK(pi >= 0 && s.unit_flies((size_t)pi));
	CHECK(s.flight_offset((size_t)pi) == 0.0f);
	run(s, 1.0f);
	pi = index_of(s, p);
	CHECK(s.units().pos_x[pi] == 0.0f && s.units().pos_z[pi] == 0.0f);

	// A walk far off: the take-off roll on the ground first, then the climb.
	s.cmd_move(&p, 1, 0.0f, 80.0f);
	run(s, 1.0f);
	pi = index_of(s, p);
	CHECK(s.flight_offset((size_t)pi) == 0.0f); // below TakeoffSpeed after 1 s (25 m/s^2 against 50 m/s)
	CHECK(speed_of(s, p) > 0.0f && speed_of(s, p) < PLANE_FLIGHT.takeoff_speed);
	run(s, 6.0f);
	pi = index_of(s, p);
	CHECK(s.flight_offset((size_t)pi) > 0.5f * PLANE_FLIGHT.flying_height);
	CHECK(std::fabs(speed_of(s, p) - PLANE_FLIGHT.max_speed) < 1e-3f);
	run(s, 10.0f);
	pi = index_of(s, p);
	CHECK(std::fabs(s.flight_offset((size_t)pi) - PLANE_FLIGHT.flying_height) < 1e-3f);
	float y = 0.0f, pitch = 1.0f, roll = 0.0f;
	CHECK(s.unit_flight_pose((size_t)pi, y, pitch, roll) && pitch == 0.0f);

	// A walk's range is 0 to 0: it never arrives, and circles its point.
	run(s, 20.0f);
	pi = index_of(s, p);
	const float cdx = s.units().pos_x[pi] - 0.0f, cdz = s.units().pos_z[pi] - 80.0f;
	CHECK(s.units().action_state[pi] == ACTION_MOVE);
	// It turns back at TurnRate once OvershootTime at MaxSpeed past it (120 m, 42.7 RTS0 units).
	CHECK(cdx * cdx + cdz * cdz < 70.0f * 70.0f);
	CHECK(std::fabs(speed_of(s, p) - PLANE_FLIGHT.max_speed) < 1e-3f);

	// In the air it is beyond a swordsman's reach (Attack.js CanAttack: height difference over
	// MaxRange) but not an archer's.
	const uint32_t sw = s.spawn_unit(10.0f, 80.0f, 1, UNIT_SWORDSMAN);
	const uint32_t ar = s.spawn_unit(-10.0f, 80.0f, 1, UNIT_ARCHER, 0.0f, roster_id("athen/infantry_archer_b")); // 60 m
	pi = index_of(s, p);
	CHECK(!s.unit_can_target((size_t)index_of(s, sw), (size_t)pi));
	CHECK(s.unit_can_target((size_t)index_of(s, ar), (size_t)pi));
	CHECK(s.unit_flight_pose((size_t)pi, y, pitch, roll) &&
			std::fabs(y - (s.get_terrain_height(s.units().pos_x[pi], s.units().pos_z[pi]) + PLANE_FLIGHT.flying_height)) < 1e-3f);

	// The flight survives a save and load: both games fly on the same way.
	const char *path = "/tmp/rtso_test_flight.sav";
	CHECK(s.save_state(path));
	static Simulation r;
	CHECK(r.load_state(path));
	std::remove(path);
	run(s, 3.0f);
	run(r, 3.0f);
	const int32_t a = index_of(s, p), b = index_of(r, p);
	CHECK(a >= 0 && b >= 0);
	if (a >= 0 && b >= 0) {
		CHECK(s.units().pos_x[a] == r.units().pos_x[b] && s.units().pos_z[a] == r.units().pos_z[b]);
		float ya = 0.0f, yb = 1.0f, pa = 0.0f, ra = 0.0f;
		CHECK(s.unit_flight_pose((size_t)a, ya, pa, ra) && r.unit_flight_pose((size_t)b, yb, pa, ra) && ya == yb);
		CHECK(s.units().heading_x[a] == r.units().heading_x[b]);
	}

	// Stopped, it lands where it is and rolls to a halt.
	s.cmd_stop(&p, 1);
	run(s, 30.0f);
	pi = index_of(s, p);
	CHECK(pi >= 0 && s.units().alive[pi]);
	CHECK(s.flight_offset((size_t)pi) == 0.0f);
	CHECK(speed_of(s, p) == 0.0f);
	const float sx = s.units().pos_x[pi], sz = s.units().pos_z[pi];
	CHECK(std::fabs(sx) <= 120.0f && std::fabs(sz) <= 120.0f);
	run(s, 2.0f);
	pi = index_of(s, p);
	CHECK(s.units().pos_x[pi] == sx && s.units().pos_z[pi] == sz);

	// An attack on a target in sight: it fires once the target is 80 to 120 m off (Machine Guns,
	// 327 damage), starting a landing each time it comes in range, and then lands with nothing
	// left to do. Its own vision (100 m) is short of that range, so a passive spearman of ours
	// keeps the target seen: alone, CheckTargetVisible would drop it in the fog. The target stands
	// its ground: a passive one flees until FleeDistance, out of sight.
	{
		static Simulation c;
		c.set_sim_seed(6);
		c.set_team_civ(0, CIV_ATHENIANS);
		c.set_team_civ(1, CIV_ROMANS);
		c.reset();
		c.set_map_half(120.0f);
		uint32_t cp = c.spawn_unit(-10.0f, 0.0f, 0, UNIT_BOLT_SHOOTER, 0.0f, plane);
		const uint32_t foe = c.spawn_unit(10.0f, 0.0f, 1, UNIT_SPEARMAN);
		const uint32_t spot = c.spawn_unit(14.0f, 0.0f, 0, UNIT_SPEARMAN);
		c.order_set_stance(&spot, 1, STANCE_PASSIVE);
		c.order_set_stance(&foe, 1, STANCE_STAND_GROUND);
		c.order_attack(&cp, 1, foe);
		run(c, 60.0f);
		const int32_t fi = index_of(c, foe);
		CHECK(fi < 0 || !c.units().alive[fi]);
		CHECK(index_of(c, cp) >= 0 && c.units().alive[index_of(c, cp)]);
		CHECK(c.flight_offset((size_t)index_of(c, cp)) == 0.0f);
	}

	// GarrisonHolder: one Support or Infantry unit boards it on the ground (Max 1); it rides along,
	// healing 1 hp a second, and can neither board nor get off once the take-off roll has begun.
	{
		static Simulation g;
		g.set_sim_seed(7);
		g.set_team_civ(0, CIV_ATHENIANS);
		g.reset();
		g.set_map_half(120.0f);
		const uint32_t gp = g.spawn_unit(0.0f, 0.0f, 0, UNIT_BOLT_SHOOTER, 0.0f, plane);
		CHECK(g.is_transport(gp) && g.get_transport_capacity(gp) == 1);
		const uint32_t inf = g.spawn_unit(3.0f, 0.0f, 0, UNIT_SPEARMAN);
		const uint32_t inf2 = g.spawn_unit(-3.0f, 0.0f, 0, UNIT_SPEARMAN);
		const uint32_t cav = g.spawn_unit(0.0f, 3.0f, 0, UNIT_CAVALRY);
		CHECK(!g.can_board_unit(gp, cav));
		g.set_unit_hp(inf, g.units().max_hp[index_of(g, inf)] - 40.0f);
		g.order_board_transport(&inf, 1, gp);
		run(g, 3.0f);
		CHECK(g.get_transport_passenger_count(gp) == 1);
		g.order_board_transport(&inf2, 1, gp);
		run(g, 3.0f);
		CHECK(g.get_transport_passenger_count(gp) == 1); // full
		// Off it goes with its passenger, hurt.
		int32_t ii = index_of(g, inf);
		const float hp0 = g.units().hp[ii];
		CHECK(hp0 < g.units().max_hp[ii] - 20.0f);
		uint32_t gpm = gp;
		g.cmd_move(&gpm, 1, 0.0f, 80.0f);
		run(g, 10.0f);
		const int32_t gi = index_of(g, gp);
		CHECK(g.flight_offset((size_t)gi) > 0.0f);
		ii = index_of(g, inf);
		CHECK(g.units().pos_x[ii] == g.units().pos_x[gi] && g.units().pos_z[ii] == g.units().pos_z[gi]);
		CHECK(std::fabs(g.units().hp[ii] - (hp0 + 10.0f)) < 0.1f);
		g.order_disembark_single(gp, inf);
		g.order_disembark_all(gp);
		CHECK(g.get_transport_passenger_count(gp) == 1); // GarrisonHolder.Eject refused in the air
		// Landed and stopped, it lets its passenger off.
		g.cmd_stop(&gpm, 1);
		run(g, 30.0f);
		CHECK(g.flight_offset((size_t)index_of(g, gp)) == 0.0f);
		g.order_disembark_all(gp);
		CHECK(g.get_transport_passenger_count(gp) == 0);
		ii = index_of(g, inf);
		CHECK(ii >= 0 && g.units().alive[ii]);
	}

	// DiesInWater: one that lands on the sea is killed, and its passenger with it (no
	// EjectClassesOnDestroy).
	static Simulation w;
	w.set_sim_seed(4);
	w.set_team_civ(0, CIV_ATHENIANS);
	w.reset();
	w.set_map_half(120.0f);
	const int res = 65;
	w.set_terrain_data(std::vector<float>((size_t)res * res, -3.0f), res, 0.0f);
	uint32_t q = w.spawn_unit(0.0f, 0.0f, 0, UNIT_BOLT_SHOOTER, 0.0f, plane);
	const uint32_t rider = w.spawn_unit(1.0f, 0.0f, 0, UNIT_SPEARMAN);
	w.order_board_transport(&rider, 1, q);
	run(w, 2.0f);
	CHECK(w.get_transport_passenger_count(q) == 1);
	w.cmd_move(&q, 1, 60.0f, 0.0f);
	run(w, 15.0f);
	CHECK(w.flight_offset((size_t)index_of(w, q)) > 0.5f * PLANE_FLIGHT.flying_height);
	w.cmd_stop(&q, 1);
	run(w, 40.0f);
	const int32_t qi = index_of(w, q);
	CHECK(qi < 0 || !w.units().alive[qi]);
	const int32_t ri = index_of(w, rider);
	CHECK(ri < 0 || (!w.units().alive[ri] && w.units().action_state[ri] != ACTION_GARRISONED));
	CHECK(w.get_transport_passenger_count(q) == 0);

	if (failures == 0) std::printf("ALL PASS\n");
	return failures == 0 ? 0 : 1;
}
