// RTS0 unit orders, stances and capture test (headless: no SCons, no Godot).
// Checks the port of 0 A.D.'s UnitAI.js orders and stances, Guard.js, the
// Commands.js delete-entities / back-to-work / queued orders, and Attack.js
// Capture (Capturable.js Reduce) as an attack:
//  1. A player's walk ignores enemies on the way (order.data.force); a Petra walk still fights.
//  2. A unit in a fight can be walked away (retreat).
//  3. Stance leash: a defensive unit gives up a chase away from its held position and
//     walks back; an aggressive one gives up once the target leaves its vision.
//  4. Attack-move fights a unit on the way, then a structure, then walks on to its point;
//     "units only" leaves the structure alone.
//  5. Guard follows the guarded unit, answers its attacker, then guards again.
//  6. Patrol walks back and forth between its start and its point.
//  7. A woman (passive) flees when attacked and goes back to gathering.
//  8. Rally points work for every unit: a woman gathers, a soldier walks, a soldier
//     sent to an enemy structure attacks it.
//  9. Builders of a storehouse gather wood nearby when it is done.
// 10. Shift-queued orders run one after the other; a plain order drops the queue.
// 11. Delete kills own units and destroys own structures, nothing else.
// 12. Back to work resumes the last gather order.
// 13. Capture is an attack: only units ordered to capture, in 4 m, take points;
//     the structure changes owner without losing hp. Ctrl (no capture) damages it.
// 14. Save/load keeps orders and queues.
// 15. Lockstep: queued bit, one client's commands in its order, delete (30), back to work (31).
// Build and run:
//   g++ -std=c++20 -O2 -Isrc tools/test_unit_orders.cpp src/simulation/simulation.cpp \
//     src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//     src/network/net_host.cpp -lpthread -o /tmp/test_unit_orders && /tmp/test_unit_orders
//  Exit: 0 = PASS, 1 = FAIL.
#include "simulation/simulation.h"
#include "network/turn_manager.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace rtso;

static constexpr float DT = 0.2f; // one lockstep turn

static int g_failures = 0;
static void check(bool ok, const char *what) {
	std::printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
	if (!ok) ++g_failures;
}

static void setup(Simulation &s) {
	s.set_sim_seed(4242);
	s.reset();
	s.set_victory_condition(VICTORY_ENDLESS);
	// Two human seats, so no AI gives orders to player 1's units.
	s.set_player_active(0, true, false);
	s.set_player_active(1, true, false);
	s.spawn_building_world(-56.0f, -56.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(56.0f, 56.0f, BLD_TOWN_CENTER, 1, 1.0f);
}

static int idx_of(const Simulation &s, uint32_t uid) {
	const auto u = s.units();
	for (size_t i = 0; i < u.count; ++i)
		if (u.id[i] == uid) return (int)i;
	return -1;
}
static bool alive(const Simulation &s, uint32_t uid) {
	const int i = idx_of(s, uid);
	return i >= 0 && s.units().alive[i];
}
static float x_of(const Simulation &s, uint32_t uid) { return s.units().pos_x[idx_of(s, uid)]; }
static float z_of(const Simulation &s, uint32_t uid) { return s.units().pos_z[idx_of(s, uid)]; }
static float hp_of(const Simulation &s, uint32_t uid) { return s.units().hp[idx_of(s, uid)]; }
static uint8_t act_of(const Simulation &s, uint32_t uid) { return s.units().action_state[idx_of(s, uid)]; }
static float dist(const Simulation &s, uint32_t uid, float x, float z) {
	return std::hypot(x_of(s, uid) - x, z_of(s, uid) - z);
}
static const Simulation::Building *bld(const Simulation &s, uint32_t id) {
	for (const Simulation::Building &b : s.buildings())
		if (b.id == id) return &b;
	return nullptr;
}
static void run(Simulation &s, float seconds) {
	int n = (int)std::lround(seconds / DT);
	for (int i = 0; i < n; ++i) s.tick(DT);
}
template <class F> static bool run_until(Simulation &s, float seconds, F &&done) {
	int n = (int)std::lround(seconds / DT);
	for (int i = 0; i < n; ++i) {
		s.tick(DT);
		if (done()) return true;
	}
	return done();
}
// An enemy that stays put and can't reach anyone: stand ground, out of melee reach.
static uint32_t post(Simulation &s, float x, float z, UnitType t = UNIT_SPEARMAN, float hp = 0.0f) {
	uint32_t id = s.spawn_unit(x, z, 1, t, hp);
	s.order_set_stance(&id, 1, STANCE_STAND_GROUND);
	return id;
}

static void test_forced_walk() {
	std::printf("1. a player's walk ignores enemies\n");
	Simulation s;
	setup(s);
	uint32_t a = s.spawn_unit(-20, 0, 0, UNIT_SWORDSMAN);
	uint32_t e = post(s, 0, 3, UNIT_SPEARMAN, 5000.0f);
	const float hp0 = hp_of(s, e);
	s.order_walk(&a, 1, 20, 0);
	const bool arrived = run_until(s, 20.0f, [&] { return dist(s, a, 20, 0) < 1.5f; });
	check(arrived, "walks past the enemy to its point");
	check(hp_of(s, e) == hp0, "without stopping to fight");
	run(s, 3.0f);
	check(dist(s, a, 20, 0) < 1.5f, "and stays when it gets there (enemy out of vision)");

	Simulation p;
	setup(p);
	uint32_t b = p.spawn_unit(-20, 0, 0, UNIT_SWORDSMAN);
	uint32_t f = post(p, 0, 3, UNIT_SPEARMAN, 5000.0f);
	const float hp1 = hp_of(p, f);
	p.cmd_move(&b, 1, 20, 0); // Petra's walk: not forced
	run(p, 12.0f);
	check(hp_of(p, f) < hp1, "an AI walk (not forced) still turns to fight");
}

static void test_retreat() {
	std::printf("2. retreat from a fight\n");
	Simulation s;
	setup(s);
	uint32_t a = s.spawn_unit(0, 0, 0, UNIT_SWORDSMAN);
	uint32_t e = s.spawn_unit(3, 0, 1, UNIT_SWORDSMAN, 5000.0f);
	run(s, 3.0f);
	check(act_of(s, a) == ACTION_ATTACK, "the two are fighting");
	s.order_walk(&a, 1, -40, 0);
	const bool away = run_until(s, 25.0f, [&] { return dist(s, a, -40, 0) < 1.5f; });
	check(away, "a walk order takes the unit out of the fight");
	(void)e;
}

static void test_stance_leash() {
	std::printf("3. stance leash\n");
	Simulation s;
	setup(s);
	uint32_t d = s.spawn_unit(0, 0, 0, UNIT_SWORDSMAN);
	s.order_set_stance(&d, 1, STANCE_DEFENSIVE);
	uint32_t e = s.spawn_unit(7, 0, 1, UNIT_CAVALRY, 5000.0f);
	s.order_set_stance(&e, 1, STANCE_STAND_GROUND);
	const bool engaged = run_until(s, 5.0f, [&] { return act_of(s, d) == ACTION_ATTACK; });
	check(engaged, "defensive unit takes on an enemy near its post");
	s.order_walk(&e, 1, 50, 0); // the enemy rides off
	run(s, 15.0f);
	check(dist(s, d, 0, 0) < 3.0f, "gives up the chase and walks back to its post");

	Simulation t;
	setup(t);
	uint32_t g = t.spawn_unit(0, 0, 0, UNIT_SWORDSMAN); // aggressive by default
	uint32_t h = t.spawn_unit(10, 0, 1, UNIT_CAVALRY, 5000.0f);
	t.order_set_stance(&h, 1, STANCE_STAND_GROUND);
	run_until(t, 5.0f, [&] { return act_of(t, g) == ACTION_ATTACK; });
	t.order_walk(&h, 1, 60, 0);
	run(t, 25.0f);
	check(act_of(t, g) != ACTION_ATTACK && dist(t, g, 60, 0) > 20.0f,
			"aggressive unit gives up once the target leaves its vision");
}

static void test_attack_move() {
	std::printf("4. attack-move\n");
	Simulation s;
	setup(s);
	uint32_t a[3] = {s.spawn_unit(-30, 0, 0, UNIT_SWORDSMAN), s.spawn_unit(-30, 2, 0, UNIT_SWORDSMAN),
			s.spawn_unit(-30, -2, 0, UNIT_SWORDSMAN)};
	uint32_t e = s.spawn_unit(-10, 0, 1, UNIT_FEMALE_CITIZEN);
	s.order_set_stance(&e, 1, STANCE_STAND_GROUND);
	const uint32_t house = s.spawn_building_world(4, 0, BLD_HOUSE, 1, 1.0f);
	const float house_hp = bld(s, house)->hp;
	s.order_attack_move(a, 3, 30, 0);
	const bool killed = run_until(s, 20.0f, [&] { return !alive(s, e); });
	check(killed, "kills the enemy on the way");
	const bool hit = run_until(s, 20.0f, [&] { const Simulation::Building *b = bld(s, house); return !b || b->hp < house_hp; });
	check(hit, "then attacks the enemy structure on the way");
	const bool razed = run_until(s, 200.0f, [&] { const Simulation::Building *b = bld(s, house); return !b || b->wrecked; });
	check(razed, "destroys it");
	const bool on = run_until(s, 30.0f, [&] { return dist(s, a[0], 30, 0) < 4.0f; });
	check(on, "and carries on to its point");

	Simulation u;
	setup(u);
	uint32_t b = u.spawn_unit(-30, 0, 0, UNIT_SWORDSMAN);
	const uint32_t h2 = u.spawn_building_world(0, 0, BLD_HOUSE, 1, 1.0f);
	const float h2hp = bld(u, h2)->hp;
	u.order_attack_move(&b, 1, 30, 0, false, true);
	const bool passed = run_until(u, 30.0f, [&] { return dist(u, b, 30, 0) < 2.0f; });
	check(passed && bld(u, h2)->hp == h2hp, "units-only attack-move leaves structures alone");
}

static void test_guard() {
	std::printf("5. guard\n");
	Simulation s;
	setup(s);
	uint32_t vip = s.spawn_unit(0, 0, 0, UNIT_HERO, 5000.0f);
	s.order_set_stance(&vip, 1, STANCE_PASSIVE);
	uint32_t g = s.spawn_unit(-6, 0, 0, UNIT_SWORDSMAN);
	s.order_guard(&g, 1, vip);
	s.order_walk(&vip, 1, 0, 20);
	run(s, 10.0f);
	check(dist(s, g, x_of(s, vip), z_of(s, vip)) < 5.0f, "guard follows the guarded unit");
	check(s.unit_order(g).kind == ORDER_GUARD, "and keeps its guard order");
	// An archer out of the guard's sight shoots the guarded unit.
	uint32_t e = s.spawn_unit(x_of(s, vip) + 14.0f, z_of(s, vip), 1, UNIT_ARCHER);
	s.order_attack(&e, 1, vip);
	const bool answered = run_until(s, 20.0f, [&] { return !alive(s, e); });
	check(answered, "guard kills whoever attacks the guarded unit");
	run(s, 6.0f);
	check(s.unit_order(g).kind == ORDER_GUARD && act_of(s, g) == ACTION_GUARD &&
			dist(s, g, x_of(s, vip), z_of(s, vip)) < 5.0f, "then goes back to guarding");
}

static void test_patrol() {
	std::printf("6. patrol\n");
	Simulation s;
	setup(s);
	uint32_t p = s.spawn_unit(-10, 0, 0, UNIT_SPEARMAN);
	s.order_patrol(&p, 1, 10, 0);
	int ends_a = 0, ends_b = 0;
	bool at_a = true, at_b = false;
	for (int i = 0; i < 300; ++i) { // 60 s
		s.tick(DT);
		const bool na = dist(s, p, -10, 0) < 1.5f, nb = dist(s, p, 10, 0) < 1.5f;
		if (na && !at_a) ++ends_a;
		if (nb && !at_b) ++ends_b;
		at_a = na; at_b = nb;
	}
	std::printf("  reached the far end %d times, the start %d times\n", ends_b, ends_a);
	check(ends_b >= 2 && ends_a >= 2, "walks back and forth between both ends");
	check(std::fabs(z_of(s, p)) < 2.0f, "on the line between them");
}

static void test_women_flee() {
	std::printf("7. women flee and go back to work\n");
	Simulation s;
	setup(s);
	const uint32_t tree = s.spawn_resource_world(0, 0, RES_WOOD, 5000);
	uint32_t w = s.spawn_unit(1.5f, 0, 0, UNIT_FEMALE_CITIZEN);
	s.order_gather(&w, 1, tree);
	run(s, 1.0f);
	check(act_of(s, w) == ACTION_GATHER, "gathering");
	uint32_t e = s.spawn_unit(4, 0, 1, UNIT_SWORDSMAN);
	s.order_attack(&e, 1, w);
	const bool fled = run_until(s, 4.0f, [&] { return act_of(s, w) == ACTION_FLEE; });
	check(fled, "flees when attacked");
	s.cmd_stop(&e, 1);
	s.order_walk(&e, 1, 50, 50);
	const bool back = run_until(s, 20.0f, [&] {
		return alive(s, w) && (act_of(s, w) == ACTION_GATHER || act_of(s, w) == ACTION_RETURN_DROPSITE);
	});
	check(back, "goes back to gathering afterwards");
	check(s.unit_order(w).kind == ORDER_GATHER && s.unit_order(w).target == tree, "the same tree");
}

static void test_rally() {
	std::printf("8. rally points for every unit\n");
	Simulation s;
	setup(s);
	s.set_player_resources(0, 5000, 5000, 5000, 5000);
	const uint32_t tree = s.spawn_resource_world(-30, -30, RES_WOOD, 5000);
	uint32_t tc = 0, barracks = s.spawn_building_world(-30, -45, BLD_BARRACKS, 0, 1.0f);
	for (const Simulation::Building &b : s.buildings())
		if (b.type == BLD_TOWN_CENTER && b.team == 0) tc = b.id;
	s.cmd_set_rally_point(0, tc, -30, -30, tree, 0);
	check(s.cmd_train(0, tc, UNIT_FEMALE_CITIZEN), "woman queued");
	const uint32_t enemy_house = s.spawn_building_world(-10, -45, BLD_HOUSE, 1, 1.0f);
	s.cmd_set_rally_point(0, barracks, -10, -45, 0, enemy_house);
	check(s.cmd_train(0, barracks, UNIT_SPEARMAN), "spearman queued");
	auto find = [&](UnitType t) -> uint32_t {
		const auto u = s.units();
		for (size_t i = 0; i < u.count; ++i)
			if (u.alive[i] && u.team[i] == 0 && u.unit_type[i] == t) return u.id[i];
		return 0;
	};
	const bool trained = run_until(s, 40.0f, [&] { return find(UNIT_FEMALE_CITIZEN) && find(UNIT_SPEARMAN); });
	check(trained, "both trained");
	run(s, 1.0f);
	const uint32_t w = find(UNIT_FEMALE_CITIZEN), sw = find(UNIT_SPEARMAN);
	check(w && s.unit_order(w).kind == ORDER_GATHER && s.unit_order(w).target == tree, "woman goes to gather the rally tree");
	check(sw && s.unit_order(sw).kind == ORDER_ATTACK_BUILDING && s.unit_order(sw).target == enemy_house,
			"soldier rallied onto an enemy structure attacks it");

	s.cmd_set_rally_point(0, barracks, -20, -20, tree, 0);
	s.cmd_train(0, barracks, UNIT_SPEARMAN);
	auto find_new = [&]() -> uint32_t {
		const auto u = s.units();
		for (size_t i = 0; i < u.count; ++i)
			if (u.alive[i] && u.team[i] == 0 && u.unit_type[i] == UNIT_SPEARMAN && u.id[i] != sw) return u.id[i];
		return 0;
	};
	const bool sp = run_until(s, 30.0f, [&] { return find_new() != 0; });
	run(s, 1.0f);
	const uint32_t spear = find_new();
	check(sp && s.unit_order(spear).kind == ORDER_WALK, "a unit that can't gather walks to the rally point");
}

static void test_builders_after_dropsite() {
	std::printf("9. builders gather after a storehouse\n");
	Simulation s;
	setup(s);
	const uint32_t store = s.spawn_building_world(0, 0, BLD_STOREHOUSE, 0, 0.9f);
	const uint32_t tree = s.spawn_resource_world(10, 0, RES_WOOD, 500);
	uint32_t w[2] = {s.spawn_unit(-4, 0, 0, UNIT_FEMALE_CITIZEN), s.spawn_unit(-4, 2, 0, UNIT_FEMALE_CITIZEN)};
	s.order_build_or_repair(w, 2, store);
	const bool done = run_until(s, 40.0f, [&] { return bld(s, store)->build_progress >= 1.0f; });
	check(done, "storehouse finished");
	run(s, 1.0f);
	check(s.unit_order(w[0]).kind == ORDER_GATHER && s.unit_order(w[0]).target == tree &&
			s.unit_order(w[1]).kind == ORDER_GATHER, "both builders go to gather the wood nearby");
}

static void test_queue() {
	std::printf("10. Shift-queued orders\n");
	Simulation s;
	setup(s);
	const uint32_t tree = s.spawn_resource_world(20, 20, RES_WOOD, 500);
	uint32_t w = s.spawn_unit(0, 0, 0, UNIT_FEMALE_CITIZEN);
	s.order_walk(&w, 1, 10, 0);
	s.order_walk(&w, 1, 10, 10, true);
	s.order_gather(&w, 1, tree, true);
	const auto *q = s.unit_order_queue(w);
	check(q && q->size() == 2, "two orders queued behind the walk");
	bool corner = run_until(s, 20.0f, [&] { return dist(s, w, 10, 10) < 1.5f; });
	check(corner, "walks the first leg, then the second");
	const bool gathering = run_until(s, 20.0f, [&] { return act_of(s, w) == ACTION_GATHER && dist(s, w, 20, 20) < 3.0f; });
	check(gathering, "then gathers");
	s.order_walk(&w, 1, 0, 0, true);
	check(s.unit_order_queue(w) && s.unit_order_queue(w)->size() == 1, "a queued walk waits behind the gathering");
	s.order_walk(&w, 1, -5, 0);
	check(!s.unit_order_queue(w), "a plain order drops the queue");
	s.order_walk(&w, 1, 5, 5, true);
	s.cmd_stop(&w, 1);
	check(!s.unit_order_queue(w) && s.unit_order(w).kind == ORDER_NONE, "stop drops it too");
}

static void test_delete() {
	std::printf("11. delete\n");
	Simulation s;
	setup(s);
	uint32_t mine = s.spawn_unit(0, 0, 0, UNIT_SPEARMAN);
	uint32_t theirs = s.spawn_unit(30, 30, 1, UNIT_SPEARMAN);
	const uint32_t house = s.spawn_building_world(-20, 0, BLD_HOUSE, 0, 1.0f);
	const uint32_t their_house = s.spawn_building_world(20, -20, BLD_HOUSE, 1, 1.0f);
	const int f0[2] = {s.food(0), s.food(1)}, w0[2] = {s.wood(0), s.wood(1)};
	uint32_t ids[4] = {mine, theirs, house, their_house};
	s.cmd_delete(0, ids, 4);
	run(s, 0.4f);
	check(!alive(s, mine), "own unit dies");
	check(alive(s, theirs), "another player's unit doesn't");
	check(bld(s, house)->wrecked, "own structure is destroyed");
	check(!bld(s, their_house)->wrecked, "another player's structure isn't");
	check(s.food(0) == f0[0] && s.food(1) == f0[1] && s.wood(0) == w0[0] && s.wood(1) == w0[1], "no loot for anyone");
}

static void test_back_to_work() {
	std::printf("12. back to work\n");
	Simulation s;
	setup(s);
	const uint32_t tree = s.spawn_resource_world(5, 0, RES_WOOD, 500);
	uint32_t w = s.spawn_unit(3.5f, 0, 0, UNIT_FEMALE_CITIZEN);
	s.order_gather(&w, 1, tree);
	run(s, 2.0f);
	s.order_walk(&w, 1, -20, 0);
	run(s, 14.0f);
	check(s.unit_order(w).kind == ORDER_NONE, "walked off and idle");
	s.order_back_to_work(&w, 1);
	check(s.unit_order(w).kind == ORDER_GATHER && s.unit_order(w).target == tree, "back to the same tree");
}

static void test_capture() {
	std::printf("13. capture is an attack\n");
	Simulation s;
	setup(s);
	const uint32_t house = s.spawn_building_world(0, 0, BLD_BARRACKS, 1, 1.0f);
	const Simulation::Building *b = bld(s, house);
	const float hp0 = b->hp, cp0 = b->capture_points_by_team[1];
	// Units standing near the structure without orders take no points any more.
	uint32_t idle[2] = {s.spawn_unit(-8, 0, 0, UNIT_SPEARMAN), s.spawn_unit(-8, 2, 0, UNIT_SPEARMAN)};
	s.order_set_stance(idle, 2, STANCE_STAND_GROUND);
	run(s, 5.0f);
	check(bld(s, house)->capture_points_by_team[1] == cp0, "idle units near it don't capture");
	uint32_t cap[6];
	for (int k = 0; k < 6; ++k) cap[k] = s.spawn_unit(-12.0f, -5.0f + 2.0f * k, 0, UNIT_SPEARMAN);
	s.order_attack_building(cap, 6, house, false, true);
	const bool flipped = run_until(s, 240.0f, [&] { return bld(s, house)->team == 0; });
	check(flipped, "six spearmen ordered to capture take it");
	check(bld(s, house)->hp >= hp0 * 0.999f, "without damaging it");
	run(s, 2.0f);
	check(s.unit_order(cap[0]).kind == ORDER_NONE && act_of(s, cap[0]) != ACTION_ATTACK, "and stop once it is theirs");

	Simulation t;
	setup(t);
	const uint32_t h2 = t.spawn_building_world(0, 0, BLD_BARRACKS, 1, 1.0f);
	const float h2hp = bld(t, h2)->hp, h2cp = bld(t, h2)->capture_points_by_team[1];
	uint32_t att[4];
	for (int k = 0; k < 4; ++k) att[k] = t.spawn_unit(-12.0f, -3.0f + 2.0f * k, 0, UNIT_SWORDSMAN);
	t.order_attack_building(att, 4, h2, false, false); // Ctrl: attack
	run(t, 20.0f);
	check(bld(t, h2)->hp < h2hp, "Ctrl-attack damages it");
	check(bld(t, h2)->capture_points_by_team[1] >= h2cp - 0.01f && bld(t, h2)->team == 1, "and takes no capture points");
}

static void test_save_load(const char *path) {
	std::printf("14. save/load keeps orders\n");
	Simulation s;
	setup(s);
	uint32_t w = s.spawn_unit(0, 0, 0, UNIT_SPEARMAN);
	s.order_walk(&w, 1, 30, 0);
	s.order_patrol(&w, 1, 30, 30, true);
	run(s, 1.0f);
	check(s.save_state(path), "saved");
	Simulation t;
	check(t.load_state(path), "loaded");
	check(t.unit_order(w).kind == ORDER_WALK && (t.unit_order(w).flags & ORDER_FLAG_FORCE), "current order kept");
	const auto *q = t.unit_order_queue(w);
	check(q && q->size() == 1 && (*q)[0].kind == ORDER_PATROL, "queue kept");
	run(s, 10.0f);
	run(t, 10.0f);
	check(std::fabs(x_of(s, w) - x_of(t, w)) < 1e-4f && std::fabs(z_of(s, w) - z_of(t, w)) < 1e-4f,
			"both copies carry on the same way");
	std::remove(path);
}

static void test_lockstep() {
	std::printf("15. lockstep commands\n");
	Simulation s;
	setup(s);
	const uint32_t tree = s.spawn_resource_world(20, 0, RES_WOOD, 500);
	uint32_t w = s.spawn_unit(0, 0, 0, UNIT_FEMALE_CITIZEN);
	uint32_t doomed = s.spawn_unit(5, 5, 0, UNIT_SPEARMAN);
	uint32_t theirs = s.spawn_unit(40, 40, 1, UNIT_SPEARMAN);
	TurnManager tm;
	auto turns = [&](float seconds) {
		int n = (int)std::lround(seconds / TurnManager::TURN_DURATION);
		for (int i = 0; i < n; ++i) tm.tick(TurnManager::TURN_DURATION, s);
	};
	check((uint8_t)NetCommandType::NET_CMD_DELETE == 30 && (uint8_t)NetCommandType::NET_CMD_BACK_TO_WORK == 31,
			"delete is 30, back to work is 31");
	// One turn: walk, then Shift-gather, then Shift-walk. Gather (4) sorts before walk (1)
	// by command id; the queue must still follow the order they were given in.
	NetCommand c;
	c.client_id = 0;
	c.entity_ids = {w};
	c.cmd_type = 1; c.target_x = -10; c.target_z = 0; c.param = 0;
	tm.post_local_command(c);
	c.cmd_type = 4; c.target_id = tree; c.param = NET_PARAM_QUEUED;
	tm.post_local_command(c);
	c.cmd_type = 1; c.target_id = 0; c.target_x = 0; c.target_z = 15; c.param = NET_PARAM_QUEUED;
	tm.post_local_command(c);
	turns(0.4f);
	const auto *q = s.unit_order_queue(w);
	check(s.unit_order(w).kind == ORDER_WALK && q && q->size() == 2 && (*q)[0].kind == ORDER_GATHER &&
			(*q)[1].kind == ORDER_WALK, "walk, then gather, then walk, in the order given");
	NetCommand d;
	d.client_id = 0;
	d.cmd_type = 30;
	d.entity_ids = {doomed, theirs};
	tm.post_local_command(d);
	turns(0.4f);
	check(!alive(s, doomed) && alive(s, theirs), "command 30 deletes the sender's unit only");
	NetCommand st;
	st.client_id = 0; st.cmd_type = 2; st.entity_ids = {w};
	tm.post_local_command(st);
	turns(0.4f);
	NetCommand bw;
	bw.client_id = 0; bw.cmd_type = 31; bw.entity_ids = {w};
	tm.post_local_command(bw);
	turns(0.4f);
	check(s.unit_order(w).kind == ORDER_NONE, "no work order yet: back to work does nothing");
	c.cmd_type = 4; c.target_id = tree; c.param = 0; c.entity_ids = {w};
	tm.post_local_command(c);
	turns(1.0f);
	tm.post_local_command(st);
	turns(0.4f);
	tm.post_local_command(bw);
	turns(0.4f);
	check(s.unit_order(w).kind == ORDER_GATHER && s.unit_order(w).target == tree, "command 31 sends it back to gather");
}

int main(int argc, char **argv) {
	std::string save = (argc > 1) ? argv[1] : "/tmp/rts0_test_unit_orders.sav";
	test_forced_walk();
	test_retreat();
	test_stance_leash();
	test_attack_move();
	test_guard();
	test_patrol();
	test_women_flee();
	test_rally();
	test_builders_after_dropsite();
	test_queue();
	test_delete();
	test_back_to_work();
	test_capture();
	test_save_load(save.c_str());
	test_lockstep();
	if (g_failures) {
		std::printf("test_unit_orders: FAIL (%d)\n", g_failures);
		return 1;
	}
	std::printf("test_unit_orders: PASS\n");
	return 0;
}
