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
// 16. Heal order (Heal.js, UnitAI HEAL): the healer walks up and heals 5 hp every 2 s,
//     then goes idle; lockstep heal (32).
// 17. An idle healer heals injured allies nearby by itself, not enemies or siege.
// 18. Garrison walks to the structure first and ignores enemies on the way.
// 19. A unit that kills its target cheers (CheeringTime), and so do idle units nearby.
// 20. Capturable.js: garrisoned units' capture attack adds to regen, the owner changes only
//     once it holds no points, walls and palisades can't be captured, women don't capture.
// 21. A gatherer whose tree runs out moves to the next one; with none left it drops off its load.
// 22. Repairers move on to nearby work once the structure is repaired.
// 23. The next resource is of the same kind (type.specific): berries after berries, not grain.
// 24. Attack.js MinRange: a catapult won't fire at a target too close; ordered to, it backs off.
// 25. Trade orders queue; an attacked trader keeps to its route with its goods.
// 26. Return resource (UnitAI ReturnResource): carriers take their load to the dropsite they
//     are sent to, not the nearest, then idle; it queues; lockstep cmd 34.
// 27. Ungarrison (Garrisonable.UnGarrison): units leaving a structure follow its rally point
//     (except one that only garrisons back in), their old queue is dropped; unload picks
//     units (UnloadTemplate), an enemy can't unload, lockstep cmd 13 carries the units.
// 28. AlertRaiser: raising an alert at a storehouse sends nearby women into the nearest
//     structures with room (not soldiers, not women far away); ending it unloads them and
//     sends them back to work; lockstep cmd 36.
// 29. A queued gather whose resource is gone gathers the same kind nearby (FINDINGNEWTARGET,
//     GatherNearPosition); queued attacks on the dead and garrisons into a full house are skipped.
// 30. A unit that collected a treasure goes for the nearest one within 64 m of it (COLLECTTREASURE
//     FINDINGNEWTARGET), not one farther away, and not when it has a queued order.
// 31. Target choice follows the attacker's PreferredClasses (Attack.js GetPreference, UnitAI
//     AttackEntitiesByPreference): an archer (Human) shoots a farther spearman before a nearer
//     ram, the nearest of equals before a wounded one, and a ram if nothing else is there.
// 32. UnitAI COMBAT: a fighting unit turns on a melee attacker, not a ranged one; after its
//     target dies it takes its next order; violent units chase beyond vision.
// 33. Idle soldiers attack enemy structures and dangerous animals in sight (after units they
//     prefer); an animal bite is an attack like any other; a guarding healer heals; stone
//     throwers stand ground by default.
// 34. A player's gather order is forced until work starts (UnitAI Gather): attacks on the
//     way are ignored, attacks at work are answered.
// 35. A load of another type is dropped when gathering starts; builders take their load to
//     the dropsite they finished; guards stop guarding a unit that changed owner.
// 38. UnitAI audit batch: a queued order after a gather goes first (with a drop-off ahead
//     of it); arrows from a structure are attacks; a capturing unit answers a ranged attacker;
//     garrisoning at a dropsite drops the load; the killer of a wolf cheers; CanGuard; guards
//     repair the structure they guard; idle healers in a formation heal; a siege engine
//     (un)packing ignores attacks; an idle full worker stays put.
// 39. A queued attack on a unit hidden in the fog is skipped (Order.Attack -> APPROACHING,
//     CheckTargetVisible); one on a unit in sight goes ahead.
// 40. UnitAI Run: a fleeing unit runs (RunMultiplier 1.67, cavalry 1.4, siege 1) and so does one
//     chasing a fleeing target; a plain walk doesn't.
// 41. A hunter whose carcass is eaten goes for the next animal nearby (GATHER FINDINGNEWTARGET
//     takes live animals), not a dangerous one.
// 42. Rally points: Shift adds positions, unset-rallypoint (cmd 14 param 1), gather near a
//     gone target, and a save round trip.
// 43. UnitAI FLEEING: the flight re-aims from where the attacker is now, ignores attackers
//     farther than the one fled from, and the source survives a save.
// 44. UnitAI REPAIRING autoharvest: a forced build turns into autoharvest at work, and only
//     such builders farm the field or gather near the dropsite they finish.
// 45. GATHER FINDINGNEWTARGET: an empty-handed gatherer with nothing left nearby walks to the
//     nearest dropsite to idle.
// 46. ShouldAbandonChase: once a second a chase whose target is hidden in the player's fog ends,
//     a player's attack too (CheckTargetVisible).
// Build and run:
//   g++ -std=c++20 -O2 -Isrc tools/test_unit_orders.cpp src/simulation/simulation.cpp \
//     src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//     src/network/net_host.cpp -lpthread -o /tmp/test_unit_orders && /tmp/test_unit_orders
//  Exit: 0 = PASS, 1 = FAIL.
#define private public
#include "simulation/simulation.h"
#undef private
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
static uint32_t target_of(const Simulation &s, uint32_t uid) { return s.units_.target_unit_id[idx_of(s, uid)]; }
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
	s.order_walk(&a, 1, 26, 0);
	const bool arrived = run_until(s, 20.0f, [&] { return dist(s, a, 26, 0) < 1.5f; });
	check(arrived, "walks past the enemy to its point");
	check(hp_of(s, e) == hp0, "without stopping to fight");
	run(s, 3.0f);
	check(dist(s, a, 26, 0) < 1.5f, "and stays when it gets there (enemy out of vision)");

	Simulation p;
	setup(p);
	uint32_t b = p.spawn_unit(-20, 0, 0, UNIT_SWORDSMAN);
	// Close enough to the straight path that the spearman, which looks for targets every fourth
	// tick, finds the walker in reach and attacks; the walk then turns to fight.
	uint32_t f = post(p, 0, 1.2f, UNIT_SPEARMAN, 5000.0f);
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
	// An archer out of the guard's sight shoots the guarded unit. It flees and the archer runs
	// after her (FLEEING re-aims from him), so the walking guard catches him once it stops.
	uint32_t e = s.spawn_unit(x_of(s, vip) + 14.0f, z_of(s, vip), 1, UNIT_ARCHER);
	s.order_attack(&e, 1, vip);
	const bool answered = run_until(s, 40.0f, [&] { return !alive(s, e); });
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
	// Citizen soldiers gather (template_unit_infantry ResourceGatherer).
	check(sp && s.unit_order(spear).kind == ORDER_GATHER && s.unit_order(spear).target == tree,
			"a citizen soldier rallied onto a tree gathers it");
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

// 0 A.D. Builder: citizen infantry build and repair (template_unit_infantry has the builder
// mixin); cavalry do not.
static void test_soldiers_build() {
	std::printf("9b. citizen infantry build, cavalry don't\n");
	Simulation s;
	setup(s);
	const uint32_t house = s.spawn_building_world(0, 0, BLD_HOUSE, 0, 0.0f);
	const uint32_t spear = s.spawn_unit(-4, 0, 0, UNIT_SPEARMAN);
	const uint32_t rider = s.spawn_unit(-4, 3, 0, UNIT_CAVALRY);
	uint32_t both[2] = {spear, rider};
	s.order_build_or_repair(both, 2, house);
	check(s.unit_order(spear).kind == ORDER_BUILD, "the spearman takes the build order");
	check(s.unit_order(rider).kind != ORDER_BUILD, "the horseman doesn't");
	const bool done = run_until(s, 120.0f, [&] { return bld(s, house)->build_progress >= 1.0f; });
	check(done, "the spearman finishes the house");
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
	const float cp0 = b->capture_points_by_team[1];
	// Units standing near the structure without orders take no points any more.
	uint32_t idle[2] = {s.spawn_unit(-8, 0, 0, UNIT_SPEARMAN), s.spawn_unit(-8, 2, 0, UNIT_SPEARMAN)};
	s.order_set_stance(idle, 2, STANCE_STAND_GROUND);
	run(s, 5.0f);
	check(bld(s, house)->capture_points_by_team[1] == cp0, "idle units near it don't capture");
	// They do stab it (stand ground, in reach): walk them off so only the capturers count.
	s.order_walk(idle, 2, -40, 0);
	const float hp1 = bld(s, house)->hp;
	uint32_t cap[6];
	for (int k = 0; k < 6; ++k) cap[k] = s.spawn_unit(-12.0f, -5.0f + 2.0f * k, 0, UNIT_SPEARMAN);
	s.order_attack_building(cap, 6, house, false, true);
	run_until(s, 30.0f, [&] { return act_of(s, cap[0]) == ACTION_ATTACK; });
	check(s.unit_capturing(idx_of(s, cap[0])), "the renderer sees it capturing (Capture clip)");
	const bool flipped = run_until(s, 240.0f, [&] { return bld(s, house)->team == 0; });
	check(flipped, "six spearmen ordered to capture take it");
	check(bld(s, house)->hp >= hp1 * 0.999f, "without damaging it");
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
	check(!t.unit_capturing(idx_of(t, att[0])), "and plays the Attack clip, not Capture");
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


static void test_heal_order() {
	std::printf("16. heal order\n");
	Simulation s;
	setup(s);
	uint32_t h = s.spawn_unit(-15, 0, 0, UNIT_HEALER);
	uint32_t w = s.spawn_unit(10, 0, 0, UNIT_SPEARMAN);
	s.set_unit_hp(w, 20.0f);
	s.order_set_stance(&w, 1, STANCE_STAND_GROUND);
	s.order_heal(&h, 1, w);
	check(act_of(s, h) == ACTION_HEAL, "the healer takes the order");
	const float reach = s.healer_range(0); // 0 A.D. Heal/Range 12 m
	const bool near = run_until(s, 15.0f, [&] { return dist(s, h, x_of(s, w), z_of(s, w)) <= reach; });
	check(near, "walks into heal range");
	run(s, 1.2f); // prepare time
	const float hp0 = hp_of(s, w);
	run(s, 10.0f);
	const float gained = hp_of(s, w) - hp0;
	check(gained >= 20.0f && gained <= 30.0f, "heals about 5 hp every 2 s");
	check(dist(s, h, x_of(s, w), z_of(s, w)) > reach * 0.75f, "from range, without walking up to it");
	const float maxhp = s.units().max_hp[idx_of(s, w)];
	const bool full = run_until(s, 120.0f, [&] { return hp_of(s, w) >= maxhp; });
	check(full, "until it is fully healed");
	run(s, 1.0f);
	check(act_of(s, h) == ACTION_IDLE && s.unit_order(h).kind == ORDER_NONE, "then goes idle");

	Simulation t;
	setup(t);
	uint32_t h2 = t.spawn_unit(0, 0, 0, UNIT_HEALER);
	uint32_t e = t.spawn_unit(5, 0, 1, UNIT_SPEARMAN);
	t.set_unit_hp(e, 20.0f);
	t.order_set_stance(&e, 1, STANCE_PASSIVE);
	t.order_heal(&h2, 1, e);
	check(act_of(t, h2) != ACTION_HEAL, "it won't heal an enemy");

	Simulation n;
	setup(n);
	TurnManager tm;
	uint32_t h3 = n.spawn_unit(0, 0, 0, UNIT_HEALER);
	n.order_set_stance(&h3, 1, STANCE_STAND_GROUND); // no auto-heal: only the command
	uint32_t w3 = n.spawn_unit(15, 0, 0, UNIT_SPEARMAN);
	n.set_unit_hp(w3, 20.0f);
	n.order_set_stance(&w3, 1, STANCE_STAND_GROUND);
	NetCommand c;
	c.client_id = 0;
	c.cmd_type = (uint8_t)NetCommandType::NET_CMD_HEAL;
	c.entity_ids = {h3};
	c.target_id = w3;
	tm.post_local_command(c);
	for (int k = 0; k < 10; ++k) tm.tick(TurnManager::TURN_DURATION, n);
	check(n.unit_order(h3).kind == ORDER_HEAL || hp_of(n, w3) > 20.0f, "lockstep heal (cmd 32) works");
}

static void test_auto_heal() {
	std::printf("17. idle healers heal injured allies nearby\n");
	Simulation s;
	setup(s);
	uint32_t h = s.spawn_unit(0, 0, 0, UNIT_HEALER);
	uint32_t w = s.spawn_unit(8, 0, 0, UNIT_SPEARMAN);
	s.set_unit_hp(w, 20.0f);
	s.order_set_stance(&w, 1, STANCE_STAND_GROUND);
	uint32_t e = s.spawn_unit(-6, 0, 1, UNIT_SPEARMAN);
	s.set_unit_hp(e, 20.0f);
	s.order_set_stance(&e, 1, STANCE_PASSIVE);
	uint32_t ram = s.spawn_unit(0, 6, 0, UNIT_SIEGE_RAM);
	s.set_unit_hp(ram, 50.0f);
	const float ehp = hp_of(s, e), rhp = hp_of(s, ram);
	run(s, 6.0f);
	check(s.unit_order(h).kind == ORDER_HEAL && s.unit_order(h).target == w, "picks the injured ally");
	check(hp_of(s, w) > 20.0f, "and heals it");
	check(hp_of(s, e) <= ehp && hp_of(s, ram) <= rhp, "not the enemy or the ram");
}

static void test_garrison_approach() {
	std::printf("18. garrison walks to the structure\n");
	Simulation s;
	setup(s);
	const uint32_t house = s.spawn_building_world(0, 0, BLD_HOUSE, 0, 1.0f);
	uint32_t u = s.spawn_unit(-30, 0, 0, UNIT_SPEARMAN);
	uint32_t e = post(s, -15, 3, UNIT_SPEARMAN, 5000.0f);
	const float ehp = hp_of(s, e);
	s.order_garrison(&u, 1, house);
	run(s, 0.4f);
	const auto *g = s.get_garrisoned_units(house);
	check(!g || g->empty(), "not inside at once from 30 m away");
	check(s.unit_order(u).kind == ORDER_GARRISON, "walking there with the order");
	const bool in = run_until(s, 30.0f, [&] {
		const auto *gg = s.get_garrisoned_units(house);
		return gg && gg->size() == 1;
	});
	check(in, "gets in once it reaches the house");
	check(hp_of(s, e) == ehp, "without fighting the enemy on the way");
}

static void test_cheer() {
	std::printf("19. cheering after a kill\n");
	Simulation s;
	setup(s);
	uint32_t a = s.spawn_unit(0, 0, 0, UNIT_SWORDSMAN);
	uint32_t friend_ = s.spawn_unit(-6, 0, 0, UNIT_SPEARMAN);
	uint32_t far_ = s.spawn_unit(-50, 0, 0, UNIT_SPEARMAN);
	uint32_t e = s.spawn_unit(2, 0, 1, UNIT_SPEARMAN, 1.0f);
	s.order_set_stance(&e, 1, STANCE_PASSIVE);
	s.order_attack(&a, 1, e);
	const bool killed = run_until(s, 10.0f, [&] { return !alive(s, e); });
	check(killed, "kills the enemy");
	const bool cheer = run_until(s, 1.0f, [&] { return act_of(s, a) == ACTION_CHEER; });
	check(cheer, "the killer cheers");
	check(act_of(s, friend_) == ACTION_CHEER, "an idle unit nearby cheers too");
	check(act_of(s, far_) == ACTION_IDLE, "one 50 m away doesn't");
	run(s, 2.0f);
	check(act_of(s, a) == ACTION_CHEER, "for a while");
	run(s, 1.4f);
	check(act_of(s, a) == ACTION_IDLE, "then idles (2.8 s)");
}

static void test_capture_rules() {
	std::printf("20. Capturable.js rules\n");
	{
		Simulation s;
		setup(s);
		const uint32_t house = s.spawn_building_world(0, 0, BLD_HOUSE, 1, 1.0f);
		uint32_t g[2] = {s.spawn_unit(0, 6, 1, UNIT_SPEARMAN), s.spawn_unit(1, 6, 1, UNIT_SPEARMAN)};
		s.order_garrison(g, 2, house);
		run(s, 10.0f);
		const auto *gg = s.get_garrisoned_units(house);
		check(gg && gg->size() == 2, "two spearmen garrisoned");
		uint32_t c = s.spawn_unit(-6, 0, 0, UNIT_SPEARMAN);
		s.order_attack_building(&c, 1, house, false, true);
		run(s, 20.0f);
		check(bld(s, house)->capture_points_by_team[1] >= 499.0f, "one spearman (2.5/s) can't beat 0.5 + 2 x 2.5 regen");
	}
	{
		Simulation s;
		setup(s);
		const uint32_t house = s.spawn_building_world(0, 0, BLD_HOUSE, 1, 1.0f);
		uint32_t cap[4];
		for (int k = 0; k < 4; ++k) cap[k] = s.spawn_unit(-6.0f, -3.0f + 2.0f * k, 0, UNIT_SPEARMAN);
		s.order_attack_building(cap, 4, house, false, true);
		const bool past_half = run_until(s, 120.0f, [&] { return bld(s, house)->capture_points_by_team[0] > 300.0f; });
		check(past_half && bld(s, house)->team == 1, "over half the points isn't enough");
		const bool flipped = run_until(s, 120.0f, [&] { return bld(s, house)->team == 0; });
		check(flipped && bld(s, house)->capture_points_by_team[1] <= 0.01f, "it changes hands once the owner holds none");
	}
	{
		Simulation s;
		setup(s);
		const uint32_t pal = s.spawn_building_world(0, 0, BLD_PALISADE, 1, 1.0f);
		const float cp = bld(s, pal)->capture_points_by_team[1], hp = bld(s, pal)->hp;
		uint32_t c[3] = {s.spawn_unit(-5, 0, 0, UNIT_SWORDSMAN), s.spawn_unit(-5, 2, 0, UNIT_SWORDSMAN),
				s.spawn_unit(-5, -2, 0, UNIT_SWORDSMAN)};
		s.order_attack_building(c, 3, pal, false, true);
		run(s, 20.0f);
		check(bld(s, pal)->capture_points_by_team[1] >= cp - 0.01f, "a palisade can't be captured");
		check(bld(s, pal)->hp < hp, "so a capture order attacks it");
	}
	{
		Simulation s;
		setup(s);
		const uint32_t house = s.spawn_building_world(0, 0, BLD_HOUSE, 1, 1.0f);
		const float cp = bld(s, house)->capture_points_by_team[1];
		uint32_t w = s.spawn_unit(-5, 0, 0, UNIT_FEMALE_CITIZEN);
		s.order_attack_building(&w, 1, house, false, true);
		run(s, 10.0f);
		check(bld(s, house)->capture_points_by_team[1] >= cp - 0.01f, "women have no capture attack");
	}
}

static void test_depletion() {
	std::printf("21. gatherers when a resource runs out\n");
	Simulation s;
	setup(s);
	const uint32_t store = s.spawn_building_world(-20, 0, BLD_STOREHOUSE, 0, 1.0f);
	const uint32_t small = s.spawn_resource_world(0, 0, RES_WOOD, 4);
	const uint32_t next = s.spawn_resource_world(5, 0, RES_WOOD, 500);
	uint32_t w = s.spawn_unit(-1.5f, 0, 0, UNIT_FEMALE_CITIZEN);
	s.order_gather(&w, 1, small);
	const bool moved = run_until(s, 30.0f, [&] { return s.unit_order(w).target == next; });
	check(moved, "moves on to the next tree");
	(void)store;

	Simulation t;
	setup(t);
	t.spawn_building_world(-20, 0, BLD_STOREHOUSE, 0, 1.0f);
	const uint32_t only = t.spawn_resource_world(0, 0, RES_WOOD, 4);
	uint32_t w2 = t.spawn_unit(-1.5f, 0, 0, UNIT_FEMALE_CITIZEN);
	const float wood0 = t.player(0).res[RES_WOOD];
	t.order_gather(&w2, 1, only);
	const bool dropped = run_until(t, 40.0f, [&] { return t.player(0).res[RES_WOOD] >= wood0 + 4.0f; });
	check(dropped, "with nothing left it drops off what it carries");
	run(t, 2.0f);
	check(act_of(t, w2) == ACTION_IDLE, "and then idles");
}

static void test_repair_follow_up() {
	std::printf("22. repairers move on\n");
	Simulation s;
	setup(s);
	const uint32_t store = s.spawn_building_world(0, 0, BLD_STOREHOUSE, 0, 1.0f);
	const uint32_t tree = s.spawn_resource_world(10, 0, RES_WOOD, 500);
	uint32_t e[6];
	for (int k = 0; k < 6; ++k) e[k] = s.spawn_unit(-6.0f, -5.0f + 2.0f * k, 1, UNIT_SWORDSMAN);
	s.order_attack_building(e, 6, store, false, false);
	const bool hurt = run_until(s, 30.0f, [&] { return bld(s, store)->hp < bld(s, store)->max_hp * 0.9f; });
	check(hurt, "the storehouse is damaged");
	s.cmd_delete(1, e, 6);
	uint32_t w = s.spawn_unit(-4, 4, 0, UNIT_FEMALE_CITIZEN);
	s.order_build_or_repair(&w, 1, store);
	const bool fixed = run_until(s, 120.0f, [&] { return bld(s, store)->hp >= bld(s, store)->max_hp; });
	check(fixed, "repaired");
	run(s, 1.0f);
	check(s.unit_order(w).kind == ORDER_GATHER && s.unit_order(w).target == tree, "the repairer gathers the wood nearby");
}

static void test_same_kind_next() {
	std::printf("23. the next resource is of the same kind\n");
	Simulation s;
	setup(s);
	s.spawn_building_world(-20, 0, BLD_STOREHOUSE, 0, 1.0f);
	s.spawn_building_world(-20, 12, BLD_FARMSTEAD, 0, 1.0f);
	const uint32_t bush = s.spawn_resource_world(0, 0, RES_FOOD, 4);
	const uint32_t grain = s.spawn_resource_world(4, 0, RES_FOOD, 500, RES_SUB_GRAIN);
	const uint32_t bush2 = s.spawn_resource_world(12, 0, RES_FOOD, 500);
	uint32_t w = s.spawn_unit(-1.5f, 0, 0, UNIT_FEMALE_CITIZEN);
	s.order_gather(&w, 1, bush);
	const bool moved = run_until(s, 60.0f, [&] { return s.unit_order(w).target != bush; });
	check(moved, "the bush runs out");
	check(s.unit_order(w).kind == ORDER_GATHER && s.unit_order(w).target == bush2,
			"the gatherer takes the farther bush, not the grain beside it");
	(void)grain;
}

static void test_min_range() {
	std::printf("24. siege minimum range\n");
	Simulation s;
	setup(s);
	uint32_t cat = s.spawn_unit(0, 0, 0, UNIT_CATAPULT);
	const float range = unit_type_info(UNIT_CATAPULT).attack_range;
	const float minr = range * unit_min_range_ratio(UNIT_CATAPULT);
	check(minr > 5.0f && minr < range, "a catapult has a minimum range");
	uint32_t near = post(s, 0, minr * 0.4f, UNIT_SPEARMAN, 5000.0f);
	const float hp0 = hp_of(s, near);
	run(s, 15.0f);
	check(hp_of(s, near) == hp0, "it leaves a unit inside its minimum range alone");
	s.order_attack(&cat, 1, near);
	bool fired = run_until(s, 90.0f, [&] { return hp_of(s, near) < hp0; });
	check(fired, "ordered to attack it, it backs off and fires");
	check(dist(s, cat, x_of(s, near), z_of(s, near)) >= minr, "from beyond its minimum range");
}

static void test_trade_orders() {
	std::printf("25. trade orders\n");
	Simulation s;
	setup(s);
	const uint32_t ma = s.spawn_building_world(-40, 0, BLD_MARKET, 0, 1.0f);
	const uint32_t mb = s.spawn_building_world(40, 0, BLD_MARKET, 0, 1.0f);
	uint32_t t = s.spawn_unit(-40, 12, 0, UNIT_TRADER);
	s.order_walk(&t, 1, -30, 12);
	s.order_trade(&t, 1, ma, mb, true);
	check(s.unit_order(t).kind == ORDER_WALK, "the trade order waits behind the walk");
	const bool trading = run_until(s, 20.0f, [&] { return act_of(s, t) == ACTION_TRADE; });
	check(trading && s.unit_order(t).kind == ORDER_TRADE, "then the trader starts its route");
	const bool loaded = run_until(s, 40.0f, [&] { return s.units().carry[idx_of(s, t)] > 0; });
	check(loaded, "it loads goods at the first market");
	run(s, 6.0f);
	const uint8_t goods = s.units().res_type_carried[idx_of(s, t)];
	uint32_t archer = post(s, x_of(s, t) + 6.0f, z_of(s, t) + 6.0f, UNIT_ARCHER);
	const float h0 = hp_of(s, t);
	const bool fled = run_until(s, 6.0f, [&] { return act_of(s, t) == ACTION_FLEE; });
	check(hp_of(s, t) < h0 && !fled && act_of(s, t) == ACTION_TRADE, "attacked, the trader keeps trading (TRADE ignores Attacked)");
	s.cmd_delete(1, &archer, 1);
	check(s.unit_order(t).kind == ORDER_TRADE, "on its route");
	check(s.units().carry[idx_of(s, t)] > 0 && s.units().res_type_carried[idx_of(s, t)] == goods,
			"still carrying its goods");
	const float before = s.player(0).res[goods];
	const bool paid = run_until(s, 60.0f, [&] { return s.player(0).res[goods] > before; });
	check(paid, "and delivers them at the second market");
}

static void test_return_resource() {
	std::printf("26. return resource\n");
	Simulation s;
	setup(s);
	const uint32_t near_store = s.spawn_building_world(-10, 0, BLD_STOREHOUSE, 0, 1.0f);
	const uint32_t far_store = s.spawn_building_world(30, 0, BLD_STOREHOUSE, 0, 1.0f);
	const uint32_t farmstead = s.spawn_building_world(0, 20, BLD_FARMSTEAD, 0, 1.0f);
	const uint32_t tree = s.spawn_resource_world(0, 0, RES_WOOD, 500);
	uint32_t w = s.spawn_unit(1.5f, 0, 0, UNIT_FEMALE_CITIZEN);
	s.order_gather(&w, 1, tree);
	const bool loaded = run_until(s, 30.0f, [&] { return s.units().carry[idx_of(s, w)] >= 5; });
	check(loaded, "the worker carries some wood");
	check(!s.can_return_resource(w, farmstead) && s.can_return_resource(w, far_store),
			"a farmstead takes no wood, a storehouse does");
	const float wood0 = s.player(0).res[RES_WOOD];
	s.order_return_resource(&w, 1, far_store);
	check(s.unit_order(w).kind == ORDER_RETURN_RESOURCE, "the order is ReturnResource");
	const bool dropped = run_until(s, 40.0f, [&] { return s.units().carry[idx_of(s, w)] == 0; });
	check(dropped && s.player(0).res[RES_WOOD] > wood0, "the wood is dropped off");
	check(dist(s, w, 30, 0) < dist(s, w, -10, 0), "at the storehouse it was sent to, not the nearer one");
	run(s, 2.0f);
	check(act_of(s, w) == ACTION_IDLE && s.unit_order(w).kind == ORDER_NONE, "then it idles (FinishOrder)");
	(void)near_store;

	// Queued after a gather: the worker gathers, then takes its load to the storehouse
	// and goes on to the walk queued after it.
	uint32_t v = s.spawn_unit(1.5f, 1.5f, 0, UNIT_FEMALE_CITIZEN);
	s.order_gather(&v, 1, tree);
	run(s, 6.0f);
	s.order_return_resource(&v, 1, far_store, true);
	s.order_walk(&v, 1, 10, 30, true);
	check(s.unit_order_queue(v) && s.unit_order_queue(v)->size() == 2, "return and walk wait in the queue");

	// Lockstep: cmd 34 on a carrying worker.
	Simulation l;
	setup(l);
	const uint32_t store2 = l.spawn_building_world(-20, 0, BLD_STOREHOUSE, 0, 1.0f);
	const uint32_t tree2 = l.spawn_resource_world(0, 0, RES_WOOD, 500);
	uint32_t x = l.spawn_unit(1.5f, 0, 0, UNIT_FEMALE_CITIZEN);
	l.order_gather(&x, 1, tree2);
	run_until(l, 30.0f, [&] { return l.units().carry[idx_of(l, x)] >= 5; });
	TurnManager tm;
	NetCommand c;
	c.client_id = 0;
	c.entity_ids = {x};
	c.cmd_type = (uint8_t)NetCommandType::NET_CMD_RETURN_RESOURCE;
	c.target_id = store2;
	tm.post_local_command(c);
	for (int i = 0; i < 4; ++i) tm.tick(TurnManager::TURN_DURATION, l);
	check(c.cmd_type == 34 && l.unit_order(x).kind == ORDER_RETURN_RESOURCE, "lockstep return resource (34)");
}

static void test_ungarrison_rally() {
	std::printf("27. ungarrison follows the rally point\n");
	Simulation s;
	setup(s);
	const uint32_t house = s.spawn_building_world(0, 0, BLD_HOUSE, 0, 1.0f);
	uint32_t a = s.spawn_unit(8, 0, 0, UNIT_SPEARMAN), b = s.spawn_unit(3, 1, 0, UNIT_SPEARMAN);
	s.order_garrison(&a, 1, house);
	s.order_walk(&a, 1, -20, 10, true); // waits behind the garrison order for good
	const bool inside = run_until(s, 10.0f, [&] { return !alive(s, a); });
	check(inside && s.garrison_unit_now(b, house), "two inside");
	check(s.unit_order_queue(a) && s.unit_order_queue(a)->size() == 1, "a walk still queued while inside");
	s.cmd_set_rally_point(0, house, 20, 0, 0, 0);
	s.cmd_unload(1, house, nullptr, 0);
	check(s.get_garrisoned_units(house)->size() == 2, "an enemy can't unload");
	s.cmd_unload(0, house, &b, 1);
	check(s.get_garrisoned_units(house)->size() == 1 && alive(s, b) && !alive(s, a), "unload picks one unit");
	check(s.unit_order(b).kind == ORDER_WALK, "it walks to the rally point");
	const bool there = run_until(s, 20.0f, [&] { return dist(s, b, 20, 0) < 2.0f; });
	check(there, "and gets there");
	s.order_ungarrison(house);
	check(alive(s, a) && s.unit_order(a).kind == ORDER_WALK && s.unit_order(a).x == 20.0f, "ungarrison all rallies too");
	check(!s.unit_order_queue(a) || s.unit_order_queue(a)->empty(), "the queued walk is dropped (Ungarrison replaces orders)");

	// Rally on the structure itself: garrison is ignored, the unit stays out and idles.
	s.cmd_set_rally_point(0, house, 0, 0, 0, house);
	check(s.garrison_unit_now(a, house), "back inside");
	s.order_ungarrison(house);
	run(s, 1.0f);
	check(alive(s, a) && s.unit_order(a).kind == ORDER_NONE, "a rally onto itself doesn't put it back in");

	// Lockstep: cmd 13 with ids unloads just those.
	Simulation l;
	setup(l);
	const uint32_t h2 = l.spawn_building_world(0, 0, BLD_HOUSE, 0, 1.0f);
	uint32_t c1 = l.spawn_unit(3, 0, 0, UNIT_SPEARMAN), c2 = l.spawn_unit(3, 1, 0, UNIT_SPEARMAN);
	l.garrison_unit_now(c1, h2);
	l.garrison_unit_now(c2, h2);
	TurnManager tm;
	NetCommand c;
	c.client_id = 0;
	c.entity_ids = {c2};
	c.cmd_type = (uint8_t)NetCommandType::NET_CMD_UNGARRISON;
	c.target_id = h2;
	tm.post_local_command(c);
	for (int i = 0; i < 4; ++i) tm.tick(TurnManager::TURN_DURATION, l);
	check(l.get_garrisoned_units(h2)->size() == 1 && alive(l, c2) && !alive(l, c1), "lockstep unload of one unit (13)");
}

static void test_alert() {
	std::printf("28. raise and end an alert\n");
	Simulation s;
	setup(s);
	const uint32_t store = s.spawn_building_world(0, 0, BLD_STOREHOUSE, 0, 1.0f);
	const uint32_t house = s.spawn_building_world(8, 0, BLD_HOUSE, 0, 1.0f);
	const uint32_t tree = s.spawn_resource_world(4, 8, RES_WOOD, 5000);
	uint32_t w[3];
	for (int k = 0; k < 3; ++k) w[k] = s.spawn_unit(3.0f + k, 6.5f, 0, UNIT_FEMALE_CITIZEN);
	s.order_gather(w, 3, tree);
	uint32_t far_w = s.spawn_unit(40, 0, 0, UNIT_FEMALE_CITIZEN);
	uint32_t sp = s.spawn_unit(2, 4, 0, UNIT_SPEARMAN);
	run(s, 3.0f);
	check(s.cmd_raise_alert(0, store), "a storehouse raises the alert");
	check(!s.cmd_raise_alert(0, house) && !s.cmd_raise_alert(1, store), "a house can't, nor can an enemy");
	check(s.unit_order(w[0]).kind == ORDER_GARRISON && s.unit_order(far_w).kind == ORDER_NONE &&
			s.unit_order(sp).kind == ORDER_NONE, "nearby women head for shelter, the far one and soldiers don't");
	const bool in = run_until(s, 20.0f, [&] { return !alive(s, w[0]) && !alive(s, w[1]) && !alive(s, w[2]); });
	check(in, "all three are inside");
	size_t inside = 0;
	for (uint32_t b : {store, house}) {
		const auto *g = s.get_garrisoned_units(b);
		inside += g ? g->size() : 0;
	}
	check(inside == 3, "in the storehouse or the house");
	s.cmd_set_rally_point(0, house, 30, 30, 0, 0);
	check(s.cmd_end_alert(0, store), "end of alert");
	check(alive(s, w[0]) && alive(s, w[1]) && alive(s, w[2]), "they come out");
	check(s.unit_order(w[0]).kind == ORDER_GATHER && s.unit_order(w[0]).target == tree, "and go back to their tree, not the rally point");

	// Lockstep cmd 36.
	Simulation l;
	setup(l);
	const uint32_t st2 = l.spawn_building_world(0, 0, BLD_STOREHOUSE, 0, 1.0f);
	l.spawn_building_world(8, 0, BLD_HOUSE, 0, 1.0f);
	uint32_t x = l.spawn_unit(3, 6.5f, 0, UNIT_FEMALE_CITIZEN);
	TurnManager tm;
	NetCommand c;
	c.client_id = 0;
	c.cmd_type = (uint8_t)NetCommandType::NET_CMD_ALERT;
	c.target_id = st2;
	tm.post_local_command(c);
	for (int i = 0; i < 4; ++i) tm.tick(TurnManager::TURN_DURATION, l);
	check(c.cmd_type == 36 && l.unit_order(x).kind == ORDER_GARRISON, "lockstep raise alert (36)");
}

static void test_queue_edges() {
	std::printf("29. queued orders whose target is gone\n");
	Simulation s;
	setup(s);
	uint32_t w = s.spawn_unit(0, 0, 0, UNIT_FEMALE_CITIZEN);
	const uint32_t t1 = s.spawn_resource_world(20, 0, RES_WOOD, 1); // cut before w gets there
	const uint32_t t2 = s.spawn_resource_world(22, 3, RES_WOOD, 500);
	uint32_t other = s.spawn_unit(19, 0, 0, UNIT_FEMALE_CITIZEN);
	s.order_gather(&other, 1, t1);
	s.order_walk(&w, 1, 10, 0);
	s.order_gather(&w, 1, t1, true);
	s.order_walk(&w, 1, -10, 0, true);
	const bool found = run_until(s, 30.0f, [&] { return s.unit_order(w).kind == ORDER_GATHER; });
	check(found && s.unit_order(w).target == t2, "the gather goes to the tree next to the one that was cut");
	check(s.unit_order_queue(w) && s.unit_order_queue(w)->size() == 1, "the walk after it still waits");

	uint32_t a = s.spawn_unit(0, 5, 0, UNIT_SPEARMAN);
	const uint32_t house = s.spawn_building_world(20, -20, BLD_HOUSE, 0, 1.0f);
	const Simulation::Building *hb = bld(s, house);
	for (int k = 0; k < hb->garrison_cap; ++k) {
		const uint32_t f = s.spawn_unit(18, -16.0f + k * 0.5f, 0, UNIT_FEMALE_CITIZEN);
		s.garrison_unit_now(f, house);
	}
	const uint32_t e = s.spawn_unit(40, 40, 1, UNIT_SPEARMAN);
	s.order_walk(&a, 1, 10, 5);
	s.order_attack(&a, 1, e, true);
	s.order_garrison(&a, 1, house, true);
	s.order_walk(&a, 1, 10, 25, true);
	s.cmd_delete(1, &e, 1);
	const bool walked = run_until(s, 30.0f, [&] { return dist(s, a, 10, 25) < 1.5f; });
	check(walked && alive(s, a), "an attack on the dead and a garrison into a full house are skipped");
}

static void test_next_treasure() {
	std::printf("30. collectors move on to the next treasure\n");
	Simulation s;
	setup(s);
	const uint32_t t1 = s.spawn_treasure(10, 0, TREASURE_FOOD_BIN);
	const uint32_t t2 = s.spawn_treasure(16, 4, TREASURE_FOOD_BIN);
	const uint32_t far_t = s.spawn_treasure(10, 40, TREASURE_FOOD_BIN);
	uint32_t u = s.spawn_unit(0, 0, 0, UNIT_SPEARMAN);
	s.order_collect_treasure(&u, 1, t1);
	const bool got1 = run_until(s, 30.0f, [&] { return !s.is_treasure_id(t1); });
	check(got1 && s.unit_order(u).kind == ORDER_COLLECT_TREASURE && s.unit_order(u).target == t2,
			"after the first it heads for the one nearby");
	const bool got2 = run_until(s, 30.0f, [&] { return !s.is_treasure_id(t2); });
	run(s, 1.0f);
	check(got2 && s.is_treasure_id(far_t) && s.unit_order(u).kind == ORDER_NONE, "but not for one 160 m away");

	const uint32_t t3 = s.spawn_treasure(30, 0, TREASURE_FOOD_BIN);
	s.spawn_treasure(34, 2, TREASURE_FOOD_BIN);
	s.order_collect_treasure(&u, 1, t3);
	s.order_walk(&u, 1, 20, -10, true);
	const bool got3 = run_until(s, 30.0f, [&] { return !s.is_treasure_id(t3); });
	check(got3 && s.unit_order(u).kind == ORDER_WALK, "a queued order comes first");
}

static void test_preferred_classes() {
	std::printf("31. target choice by PreferredClasses\n");
	{
		Simulation s;
		setup(s);
		s.spawn_unit(0, 0, 0, UNIT_ARCHER);
		uint32_t ram = s.spawn_unit(4, 0, 1, UNIT_SIEGE_RAM);
		uint32_t spear = s.spawn_unit(0, 9, 1, UNIT_SPEARMAN);
		uint32_t still[2] = {ram, spear};
		s.order_set_stance(still, 2, STANCE_STAND_GROUND);
		const float r0 = hp_of(s, ram), s0 = hp_of(s, spear);
		run(s, 4.0f);
		check(hp_of(s, spear) < s0 && hp_of(s, ram) == r0, "an archer shoots the spearman before the nearer ram");
	}
	{
		Simulation s;
		setup(s);
		s.spawn_unit(0, 0, 0, UNIT_ARCHER);
		uint32_t near = s.spawn_unit(5, 0, 1, UNIT_SPEARMAN);
		uint32_t hurt = s.spawn_unit(0, 8, 1, UNIT_SPEARMAN);
		s.set_unit_hp(hurt, 5.0f);
		uint32_t still[2] = {near, hurt};
		s.order_set_stance(still, 2, STANCE_STAND_GROUND);
		const float n0 = hp_of(s, near);
		run(s, 4.0f);
		check(hp_of(s, near) < n0 && alive(s, hurt) && hp_of(s, hurt) == 5.0f,
				"the nearest of equals, not a wounded one farther off");
	}
	{
		Simulation s;
		setup(s);
		s.spawn_unit(0, 0, 0, UNIT_ARCHER);
		uint32_t ram = s.spawn_unit(5, 0, 1, UNIT_SIEGE_RAM);
		s.order_set_stance(&ram, 1, STANCE_STAND_GROUND);
		const float r0 = hp_of(s, ram);
		run(s, 4.0f);
		check(hp_of(s, ram) < r0, "and the ram when nothing it prefers is there");
	}
}

static void test_unitai_combat() {
	std::printf("32. UnitAI COMBAT: melee attackers, the order after a fight, violent chases\n");
	{
		Simulation s;
		setup(s);
		uint32_t a = s.spawn_unit(0, 0, 0, UNIT_ARCHER, 5000.0f);
		uint32_t far = post(s, 7, 0, UNIT_SPEARMAN, 5000.0f);
		const bool shooting = run_until(s, 5.0f, [&] { return target_of(s, a) == far; });
		uint32_t sword = s.spawn_unit(0, -3, 1, UNIT_SWORDSMAN, 5000.0f);
		s.order_attack(&sword, 1, a);
		const bool turned = run_until(s, 8.0f, [&] { return target_of(s, a) == sword; });
		check(shooting && turned, "an archer shooting one enemy turns on a swordsman hitting it (Melee Attacked)");
	}
	{
		Simulation s;
		setup(s);
		uint32_t a = s.spawn_unit(0, 0, 0, UNIT_ARCHER, 5000.0f);
		uint32_t far = post(s, 7, 0, UNIT_SPEARMAN, 5000.0f);
		run_until(s, 5.0f, [&] { return target_of(s, a) == far; });
		uint32_t arch = s.spawn_unit(0, -6, 1, UNIT_ARCHER, 5000.0f);
		s.order_attack(&arch, 1, a);
		run(s, 6.0f);
		check(target_of(s, a) == far, "but not on another archer shooting it");
	}
	{
		Simulation s;
		setup(s);
		uint32_t a = s.spawn_unit(0, 0, 0, UNIT_SWORDSMAN, 5000.0f);
		uint32_t weak = post(s, 2, 0, UNIT_SPEARMAN, 1.0f);
		uint32_t other = post(s, 2, 3, UNIT_SPEARMAN, 5000.0f);
		s.order_attack(&a, 1, weak);
		s.order_walk(&a, 1, -20, 0, true);
		const bool killed = run_until(s, 6.0f, [&] { return !alive(s, weak); });
		run(s, 1.0f);
		check(killed && s.unit_order(a).kind == ORDER_WALK && target_of(s, a) != other,
				"after its target dies, a unit takes its next order, not the enemy beside it");
	}
	for (int violent = 0; violent < 2; ++violent) {
		Simulation s;
		setup(s);
		// A swordsman can't catch a cavalryman riding away.
		uint32_t a = s.spawn_unit(-40, -30, 0, UNIT_SWORDSMAN, 5000.0f);
		s.order_set_stance(&a, 1, violent ? STANCE_VIOLENT : STANCE_AGGRESSIVE);
		uint32_t e = s.spawn_unit(-35, -30, 1, UNIT_CAVALRY, 5000.0f);
		s.order_set_stance(&e, 1, STANCE_PASSIVE);
		// Passive scouts of its own keep the rider in sight: an attack on a unit hidden in
		// the fog is dropped whatever the stance (ShouldAbandonChase, CheckTargetVisible).
		for (float sx : {-10.0f, 15.0f, 40.0f}) {
			uint32_t scout = s.spawn_unit(sx, -40, 0, UNIT_CAVALRY, 5000.0f);
			s.order_set_stance(&scout, 1, STANCE_PASSIVE);
		}
		run_until(s, 5.0f, [&] { return target_of(s, a) == e; });
		s.order_walk(&e, 1, 55, -30);
		const float vision = s.unit_base_vision(idx_of(s, a));
		run_until(s, 20.0f, [&] { return x_of(s, e) - x_of(s, a) > vision + 4.0f; });
		const bool kept = target_of(s, a) == e;
		if (violent) check(kept, "a violent unit chases beyond its vision (respondChaseBeyondVision)");
		else check(!kept, "an aggressive one gives up there");
	}
}

static void test_unitai_targets() {
	std::printf("33. UnitAI targets: structures, dangerous animals, guarding healers, siege stance\n");
	{
		Simulation s;
		setup(s);
		const uint32_t bar = s.spawn_building_world(6, 0, BLD_BARRACKS, 1, 1.0f);
		s.spawn_unit(0, 0, 0, UNIT_SWORDSMAN);
		const float h0 = bld(s, bar)->hp;
		run(s, 10.0f);
		check(bld(s, bar)->hp < h0, "an idle soldier attacks an enemy structure in sight (AttackEntitiesByPreference)");
	}
	{
		Simulation s;
		setup(s);
		uint32_t a = s.spawn_unit(0, 0, 0, UNIT_SWORDSMAN);
		const uint32_t enemy = post(s, 3, 3, UNIT_SPEARMAN, 5000.0f);
		s.spawn_building_world(-5, 0, BLD_BARRACKS, 1, 1.0f);
		run(s, 3.0f);
		check(target_of(s, a) == enemy, "but a soldier it prefers first");
	}
	{
		Simulation s;
		setup(s);
		const uint32_t wolf = s.spawn_fauna(8, 0, FAUNA_WOLF);
		uint32_t a = s.spawn_unit(0, 0, 0, UNIT_SWORDSMAN);
		const bool hunting = run_until(s, 4.0f, [&] { return target_of(s, a) == wolf; });
		check(hunting, "an idle soldier attacks a wolf in sight (IsDangerousAnimal)");
		Simulation t;
		setup(t);
		t.spawn_fauna(6, 0, FAUNA_DEER);
		uint32_t b = t.spawn_unit(0, 0, 0, UNIT_SWORDSMAN);
		run(t, 4.0f);
		check(target_of(t, b) == 0, "not a deer");
	}
	{
		Simulation s;
		setup(s);
		const uint32_t tree = s.spawn_resource_world(0, 0, RES_WOOD, 5000);
		uint32_t w = s.spawn_unit(1.5f, 0, 0, UNIT_FEMALE_CITIZEN);
		s.order_gather(&w, 1, tree);
		run(s, 2.0f);
		s.spawn_fauna(3, 0, FAUNA_WOLF);
		const bool fled = run_until(s, 10.0f, [&] { return act_of(s, w) == ACTION_FLEE; });
		check(fled, "a woman bitten by a wolf flees (Attacked)");
	}
	{
		Simulation s;
		setup(s);
		uint32_t sol = s.spawn_unit(0, 0, 0, UNIT_SPEARMAN);
		uint32_t h = s.spawn_unit(1, 0, 0, UNIT_HEALER);
		s.order_guard(&h, 1, sol);
		run(s, 1.0f);
		s.set_unit_hp(sol, 20.0f);
		const bool healing = run_until(s, 3.0f, [&] { return act_of(s, h) == ACTION_HEAL; });
		run(s, 6.0f);
		check(healing && hp_of(s, sol) > 20.0f, "a guarding healer heals the unit it guards");
		run(s, 120.0f);
		check(s.unit_order(h).kind == ORDER_GUARD, "then guards it again");
	}
	{
		Simulation s;
		setup(s);
		const uint32_t c = s.spawn_unit(0, 0, 0, UNIT_CATAPULT);
		check(s.units().stance[idx_of(s, c)] == STANCE_STAND_GROUND, "stone throwers stand ground by default");
	}
}

static void test_forced_work_orders() {
	std::printf("34. a player's Gather and Repair are forced until work starts\n");
	Simulation s;
	setup(s);
	const uint32_t tree = s.spawn_resource_world(30, 0, RES_WOOD, 5000);
	uint32_t w = s.spawn_unit(0, 0, 0, UNIT_FEMALE_CITIZEN, 5000.0f);
	s.order_gather(&w, 1, tree);
	uint32_t arch = post(s, 12, 3, UNIT_ARCHER, 5000.0f); // they shoot her as she passes
	post(s, 8, -3, UNIT_ARCHER, 5000.0f);
	post(s, 18, -3, UNIT_ARCHER, 5000.0f);
	const float h0 = hp_of(s, w);
	bool fled_on_the_way = false;
	const bool reached = run_until(s, 30.0f, [&] {
		fled_on_the_way = fled_on_the_way || act_of(s, w) == ACTION_FLEE;
		return dist(s, w, 30, 0) < 2.5f;
	});
	check(reached && !fled_on_the_way && hp_of(s, w) < h0, "a woman sent to a tree walks on under fire");
	s.order_attack(&arch, 1, w);
	const bool now_flees = run_until(s, 20.0f, [&] { return act_of(s, w) == ACTION_FLEE; });
	check(now_flees, "once at work, an attack makes her flee");
}

static void test_loads_and_guards() {
	std::printf("35. loads of another type, loads at a new dropsite, guards after a change of owner\n");
	{
		Simulation s;
		setup(s);
		const uint32_t tree = s.spawn_resource_world(-20, -20, RES_WOOD, 500);
		const uint32_t bush = s.spawn_resource_world(-30, -20, RES_FOOD, 500);
		uint32_t w = s.spawn_unit(-21.5f, -20, 0, UNIT_FEMALE_CITIZEN);
		s.order_gather(&w, 1, tree);
		run_until(s, 20.0f, [&] { return s.units_.carry[idx_of(s, w)] >= 4; });
		const float wood0 = s.player(0).res[RES_WOOD];
		s.order_gather(&w, 1, bush);
		check(s.units_.res_type_carried[idx_of(s, w)] == RES_WOOD, "a woman sent to berries still carries wood on the way");
		const bool food = run_until(s, 20.0f, [&] {
			return s.units_.res_type_carried[idx_of(s, w)] == RES_FOOD && s.units_.carry[idx_of(s, w)] > 0;
		});
		check(food && s.units_.carry[idx_of(s, w)] < 4 && s.player(0).res[RES_WOOD] == wood0,
				"and drops it when she starts on the berries (DropResources): no wood turns into food");
	}
	{
		Simulation s;
		setup(s);
		const uint32_t store = s.spawn_building_world(0, 0, BLD_STOREHOUSE, 0, 0.9f);
		uint32_t w = s.spawn_unit(-4, 0, 0, UNIT_FEMALE_CITIZEN);
		s.units_.carry[idx_of(s, w)] = 8;
		s.units_.res_type_carried[idx_of(s, w)] = RES_WOOD;
		const float wood0 = s.player(0).res[RES_WOOD];
		s.order_build_or_repair(&w, 1, store);
		run_until(s, 40.0f, [&] { return bld(s, store)->build_progress >= 1.0f; });
		const bool dropped = run_until(s, 10.0f, [&] { return s.player(0).res[RES_WOOD] >= wood0 + 8; });
		check(dropped && s.units_.carry[idx_of(s, w)] == 0, "a builder carrying wood drops it at the storehouse it finished");
	}
	{
		Simulation s;
		setup(s);
		uint32_t vip = s.spawn_unit(0, 0, 0, UNIT_SPEARMAN);
		uint32_t g = s.spawn_unit(2, 0, 0, UNIT_SPEARMAN);
		s.order_guard(&g, 1, vip);
		run(s, 1.0f);
		check(s.unit_order(g).kind == ORDER_GUARD, "guarding");
		s.units_.team[idx_of(s, vip)] = 1; // converted
		run(s, 1.0f);
		check(s.unit_order(g).kind != ORDER_GUARD, "a guard stops guarding a unit that is now an enemy (Guard.js CheckGuards)");
	}
}

static void test_remove_guard_call_to_arms() {
	std::printf("36. remove-guard and call-to-arms\n");
	{
		Simulation s;
		setup(s);
		uint32_t vip = s.spawn_unit(0, 0, 0, UNIT_SPEARMAN);
		uint32_t g = s.spawn_unit(2, 0, 0, UNIT_SPEARMAN);
		uint32_t g2 = s.spawn_unit(2, 2, 0, UNIT_SPEARMAN);
		s.order_guard(&g, 1, vip);
		s.order_walk(&g2, 1, 6, 6);
		s.order_guard(&g2, 1, vip, true);
		run(s, 0.5f);
		s.order_remove_guard(&g, 1);
		check(s.unit_order(g).kind == ORDER_NONE, "remove-guard ends a Guard order");
		s.order_remove_guard(&g2, 1);
		check(s.unit_order(g2).kind == ORDER_WALK && !s.unit_order_queue(g2), "and drops a queued Guard, keeping the walk");
	}
	{
		Simulation s;
		setup(s);
		s.spawn_building_world(0, 0, BLD_STOREHOUSE, 0, 1.0f);
		uint32_t sp = s.spawn_unit(6, 0, 0, UNIT_SPEARMAN);
		s.units_.carry[idx_of(s, sp)] = 5;
		s.units_.res_type_carried[idx_of(s, sp)] = RES_WOOD;
		uint32_t w = s.spawn_unit(6, 2, 0, UNIT_FEMALE_CITIZEN);
		const uint32_t ids[2] = {sp, w};
		const float wood0 = s.player(0).res[RES_WOOD];
		s.order_call_to_arms(ids, 2, 40, 0);
		const auto *q = s.unit_order_queue(sp);
		check(s.unit_order(sp).kind == ORDER_RETURN_RESOURCE && q && q->size() == 1 &&
				(*q)[0].kind == ORDER_WALK_AND_FIGHT, "a soldier carrying wood drops it, then attack-walks");
		check(s.unit_order(w).kind == ORDER_NONE, "a woman doesn't answer the call");
		const bool off = run_until(s, 20.0f, [&] { return s.unit_order(sp).kind == ORDER_WALK_AND_FIGHT; });
		check(off && s.player(0).res[RES_WOOD] >= wood0 + 5, "the wood is in the stockpile and he is on his way");
	}
	{
		// Lockstep cmds 37 and 38.
		Simulation l;
		setup(l);
		uint32_t vip = l.spawn_unit(0, 0, 0, UNIT_SPEARMAN);
		uint32_t g = l.spawn_unit(2, 0, 0, UNIT_SPEARMAN);
		uint32_t a = l.spawn_unit(4, 0, 0, UNIT_ARCHER);
		l.order_guard(&g, 1, vip);
		TurnManager tm;
		NetCommand c;
		c.client_id = 0;
		c.cmd_type = (uint8_t)NetCommandType::NET_CMD_REMOVE_GUARD;
		c.entity_ids = {g};
		tm.post_local_command(c);
		NetCommand d;
		d.client_id = 0;
		d.cmd_type = (uint8_t)NetCommandType::NET_CMD_CALL_TO_ARMS;
		d.entity_ids = {a};
		d.target_x = 30;
		d.target_z = 0;
		tm.post_local_command(d);
		for (int i = 0; i < 4; ++i) tm.tick(TurnManager::TURN_DURATION, l);
		check(c.cmd_type == 37 && l.unit_order(g).kind == ORDER_NONE, "lockstep remove-guard (37)");
		check(d.cmd_type == 38 && l.unit_order(a).kind == ORDER_WALK_AND_FIGHT, "lockstep call-to-arms (38)");
	}
}

static void test_guard_structure() {
	std::printf("37. guarding a structure (Guard.js on any entity)\n");
	{
		Simulation s;
		setup(s);
		const uint32_t store = s.spawn_building_world(0, 0, BLD_STOREHOUSE, 0, 1.0f);
		const uint32_t theirs = s.spawn_building_world(40, 40, BLD_STOREHOUSE, 1, 1.0f);
		uint32_t g = s.spawn_unit(20, 0, 0, UNIT_SPEARMAN);
		s.order_guard(&g, 1, theirs);
		check(s.unit_order(g).kind != ORDER_GUARD, "an enemy structure can't be guarded");
		s.order_guard(&g, 1, store);
		check(s.unit_order(g).kind == ORDER_GUARD && act_of(s, g) == ACTION_GUARD, "a soldier guards our storehouse");
		const float h = bld(s, store)->half_size;
		const bool near = run_until(s, 20.0f, [&] {
			return std::max(std::fabs(s.units().pos_x[idx_of(s, g)]) - h, std::fabs(s.units().pos_z[idx_of(s, g)]) - h) < 3.0f;
		});
		check(near, "and walks over to stand by it");
		// An enemy archer out of the guard's sight shoots the storehouse.
		uint32_t a = post(s, -h - 12.0f, 0, UNIT_ARCHER);
		s.order_attack_building(&a, 1, store);
		const bool answers = run_until(s, 10.0f, [&] { return target_of(s, g) == a && act_of(s, g) == ACTION_ATTACK; });
		check(answers, "the guard goes for whoever attacks the structure (GuardedAttacked)");
		s.set_unit_hp(a, 0.0f);
		const bool back = run_until(s, 20.0f, [&] { return act_of(s, g) == ACTION_GUARD; });
		check(back && s.unit_order(g).kind == ORDER_GUARD && s.unit_order(g).target == store, "then guards it again");
		for (auto &b : s.buildings_) if (b.id == store) b.team = 1;
		run(s, 1.0f);
		check(s.unit_order(g).kind != ORDER_GUARD, "a guard lets go of a structure that is no longer ours");
	}
	{
		Simulation l;
		setup(l);
		const uint32_t store = l.spawn_building_world(0, 0, BLD_STOREHOUSE, 0, 1.0f);
		uint32_t g = l.spawn_unit(20, 0, 0, UNIT_SPEARMAN);
		TurnManager tm;
		NetCommand c;
		c.client_id = 0;
		c.cmd_type = 21;
		c.entity_ids = {g};
		c.target_id = store;
		tm.post_local_command(c);
		for (int i = 0; i < 4; ++i) tm.tick(TurnManager::TURN_DURATION, l);
		check(l.unit_order(g).kind == ORDER_GUARD && l.unit_order(g).target == store, "lockstep guard on a structure (21)");
	}
}

static void test_unitai_audit() {
	std::printf("38. UnitAI audit batch\n");
	{
		Simulation s;
		setup(s);
		s.spawn_building_world(-20, 0, BLD_STOREHOUSE, 0, 1.0f);
		const uint32_t small = s.spawn_resource_world(0, 0, RES_WOOD, 4);
		s.spawn_resource_world(5, 0, RES_WOOD, 500);
		uint32_t w = s.spawn_unit(-1.5f, 0, 0, UNIT_FEMALE_CITIZEN);
		s.order_gather(&w, 1, small);
		s.order_walk(&w, 1, 0.0f, 20.0f, true);
		const float wood0 = s.player(0).res[RES_WOOD];
		const bool walked = run_until(s, 60.0f, [&] { return dist(s, w, 0.0f, 20.0f) < 2.0f; });
		check(walked && s.player(0).res[RES_WOOD] >= wood0 + 4.0f,
				"a queued walk after a gather goes before the next tree, after a drop-off (FINDINGNEWTARGET)");
	}
	{
		Simulation s;
		setup(s);
		const uint32_t tower = s.spawn_building_world(30, 0, BLD_TOWER, 1, 1.0f);
		uint32_t w = s.spawn_unit(0, 0, 0, UNIT_FEMALE_CITIZEN);
		s.order_set_stance(&w, 1, STANCE_PASSIVE);
		uint32_t sol = s.spawn_unit(0, 6, 0, UNIT_SPEARMAN);
		s.units_.attacked_by[idx_of(s, w)] = tower;
		s.units_.attacked_by[idx_of(s, sol)] = tower;
		s.tick(DT);
		check(act_of(s, w) == ACTION_FLEE, "a woman shot by a tower flees from it");
		check(s.units_.target_building_id[idx_of(s, sol)] == tower, "an idle soldier shot by a tower goes for it");
	}
	{
		Simulation s;
		setup(s);
		const uint32_t house = s.spawn_building_world(10, 0, BLD_HOUSE, 1, 1.0f);
		uint32_t c = s.spawn_unit(5, 0, 0, UNIT_SPEARMAN);
		s.order_attack_building(&c, 1, house, false, true);
		const bool capturing = run_until(s, 10.0f, [&] { return s.unit_capturing(idx_of(s, c)); });
		uint32_t archer = post(s, 5, 12, UNIT_ARCHER);
		s.units_.attacked_by[idx_of(s, c)] = archer;
		s.tick(DT);
		check(capturing && target_of(s, c) == 0, "a player's capture order ignores a ranged attacker (order.data.force)");
		s.order_set_stance(&c, 1, STANCE_VIOLENT);
		s.units_.attacked_by[idx_of(s, c)] = archer;
		s.tick(DT);
		check(target_of(s, c) == archer, "a violent capturer turns on it (ATTACKING Attacked, targetAttackersAlways)");
	}
	{
		Simulation s;
		setup(s);
		uint32_t cc = 0;
		for (const auto &b : s.buildings()) if (b.type == BLD_TOWN_CENTER && b.team == 0) cc = b.id;
		uint32_t w = s.spawn_unit(-46, -46, 0, UNIT_FEMALE_CITIZEN);
		s.units_.carry[idx_of(s, w)] = 8;
		s.units_.res_type_carried[idx_of(s, w)] = RES_STONE;
		const float stone0 = s.player(0).res[RES_STONE];
		s.order_garrison(&w, 1, cc);
		const bool in = run_until(s, 20.0f, [&] { return act_of(s, w) == ACTION_GARRISONED; });
		check(in && s.player(0).res[RES_STONE] >= stone0 + 8.0f && s.units_.carry[idx_of(s, w)] == 0,
				"garrisoning at a dropsite drops the load first");
	}
	{
		Simulation s;
		setup(s);
		const uint32_t wolf = s.spawn_fauna(3, 0, FAUNA_WOLF);
		s.fauna_.hp[s.fauna_id_to_idx_.at(wolf)] = 1.0f;
		uint32_t a = s.spawn_unit(0, 0, 0, UNIT_SWORDSMAN);
		s.order_attack(&a, 1, wolf);
		const bool cheered = run_until(s, 10.0f, [&] { return act_of(s, a) == ACTION_CHEER; });
		check(cheered, "the unit that kills a wolf cheers (shouldCheer)");
	}
	{
		Simulation s;
		setup(s);
		const uint32_t store = s.spawn_building_world(0, 0, BLD_STOREHOUSE, 0, 1.0f);
		uint32_t ram = s.spawn_unit(10, 0, 0, UNIT_SIEGE_RAM);
		uint32_t trader = s.spawn_unit(10, 4, 0, UNIT_TRADER);
		s.order_guard(&ram, 1, store);
		s.order_guard(&trader, 1, store);
		check(s.unit_order(ram).kind != ORDER_GUARD && s.unit_order(trader).kind != ORDER_GUARD,
				"siege engines and traders don't guard (CanGuard)");
		uint32_t enemy = s.spawn_unit(10, 8, 1, UNIT_SPEARMAN);
		uint32_t g = s.spawn_unit(12, 8, 0, UNIT_SPEARMAN);
		s.order_guard(&g, 1, enemy);
		check(s.unit_order(g).kind != ORDER_GUARD, "nor guard an enemy unit");
		uint32_t w = s.spawn_unit(-8, 0, 0, UNIT_FEMALE_CITIZEN);
		s.order_guard(&w, 1, store);
		run(s, 2.0f);
		for (auto &b : s.buildings_) if (b.id == store) b.hp = b.max_hp * 0.5f;
		const bool repairing = run_until(s, 4.0f, [&] { return act_of(s, w) == ACTION_BUILD; });
		const bool fixed = run_until(s, 120.0f, [&] { return bld(s, store)->hp >= bld(s, store)->max_hp; });
		run(s, 1.0f);
		check(repairing && fixed, "a woman guarding a damaged storehouse repairs it (GUARDING Timer)");
		check(s.unit_order(w).kind == ORDER_GUARD && s.unit_order(w).target == store, "then guards it again");
	}
	{
		Simulation s;
		setup(s);
		uint32_t ids[5];
		for (int k = 0; k < 4; ++k) ids[k] = s.spawn_unit(-2.0f + 1.5f * k, 0, 0, UNIT_SPEARMAN);
		ids[4] = s.spawn_unit(0, -2, 0, UNIT_HEALER);
		s.order_set_formation(ids, 5, FORMATION_BOX);
		run(s, 3.0f);
		check(s.formation_group_[idx_of(s, ids[4])] != 0, "a healer in a box formation");
		s.set_unit_hp(ids[0], 20.0f);
		const bool heals = run_until(s, 6.0f, [&] { return act_of(s, ids[4]) == ACTION_HEAL; });
		check(heals, "an idle healer in a formation heals a hurt member (FORMATIONMEMBER.IDLE)");
	}
	{
		Simulation s;
		setup(s);
		s.spawn_building_world(-20, 0, BLD_STOREHOUSE, 0, 1.0f);
		uint32_t w = s.spawn_unit(0, 0, 0, UNIT_FEMALE_CITIZEN);
		s.units_.res_type_carried[idx_of(s, w)] = RES_WOOD;
		s.units_.carry[idx_of(s, w)] = s.carry_cap(idx_of(s, w), RES_WOOD);
		run(s, 5.0f);
		check(act_of(s, w) == ACTION_IDLE && dist(s, w, 0, 0) < 1.0f && s.units_.carry[idx_of(s, w)] > 0,
				"an idle worker with a full load stays put (UnitAI IDLE; Petra sends its own ReturnResource)");
	}
	{
		Simulation s;
		setup(s);
		uint32_t cat = s.spawn_unit(0, 0, 0, UNIT_CATAPULT);
		s.order_pack(&cat, 1, false);
		s.tick(DT);
		const uint8_t ps = s.units_.pack_state[idx_of(s, cat)];
		uint32_t e = post(s, 0, 8, UNIT_SWORDSMAN);
		s.units_.attacked_by[idx_of(s, cat)] = e;
		s.tick(DT);
		check(ps == PACK_UNPACKING && s.units_.pack_state[idx_of(s, cat)] == PACK_UNPACKING && target_of(s, cat) == 0,
				"a catapult unpacking ignores an attack");
	}
}

static void test_fog_queued_attack() {
	std::printf("39. queued attacks on units hidden in the fog\n");
	Simulation s;
	setup(s);
	uint32_t a = s.spawn_unit(0, 0, 0, UNIT_SPEARMAN);
	uint32_t seen = post(s, 6, 0, UNIT_SPEARMAN, 5000.0f);
	uint32_t hidden = post(s, 40, -30, UNIT_SPEARMAN);
	run(s, 1.0f);
	check(s.los_visible(0, 6, 0) && !s.los_visible(0, 40, -30), "one enemy in sight, one in the fog");
	s.order_walk(&a, 1, 0.0f, 2.0f);
	s.order_attack(&a, 1, hidden, true);
	s.order_walk(&a, 1, -6.0f, 2.0f, true);
	const bool walked = run_until(s, 20.0f, [&] { return dist(s, a, -6.0f, 2.0f) < 1.5f; });
	check(walked && target_of(s, a) == 0 && dist(s, a, 40.0f, -30.0f) > 30.0f,
			"the attack on the hidden one is skipped and the next walk goes ahead");
	s.order_walk(&a, 1, 0.0f, 2.0f);
	s.order_attack(&a, 1, seen, true);
	const bool attacks = run_until(s, 20.0f, [&] { return target_of(s, a) == seen; });
	check(attacks, "a queued attack on one in sight goes ahead");
}

static void test_run_speed() {
	std::printf("40. running while fleeing and chasing a fleeing target\n");
	Simulation s;
	setup(s);
	uint32_t w = s.spawn_unit(0, 0, 0, UNIT_FEMALE_CITIZEN);
	uint32_t cav = s.spawn_unit(0, 10, 0, UNIT_CAVALRY);
	uint32_t ram = s.spawn_unit(0, -10, 0, UNIT_SIEGE_RAM);
	const int wi = idx_of(s, w), ci = idx_of(s, cav), ri = idx_of(s, ram);
	check(std::fabs(s.unit_walk_speed(wi) - s.units_.speed[wi]) < 1e-4f, "a woman standing still walks at her walk speed");
	s.units_.action_state[wi] = ACTION_FLEE;
	s.units_.action_state[ci] = ACTION_FLEE;
	s.units_.action_state[ri] = ACTION_FLEE;
	check(std::fabs(s.unit_walk_speed(wi) - 1.67f * s.units_.speed[wi]) < 1e-3f, "she runs at 1.67x while fleeing");
	check(std::fabs(s.unit_walk_speed(ci) - 1.4f * s.units_.speed[ci]) < 1e-3f, "cavalry run at 1.4x");
	check(std::fabs(s.unit_walk_speed(ri) - s.units_.speed[ri]) < 1e-3f, "a ram doesn't run");

	Simulation t;
	setup(t);
	uint32_t woman = t.spawn_unit(0, 0, 0, UNIT_FEMALE_CITIZEN);
	t.order_set_stance(&woman, 1, STANCE_PASSIVE);
	uint32_t sp = t.spawn_unit(4, 0, 1, UNIT_SPEARMAN);
	t.order_attack(&sp, 1, woman);
	const bool fled = run_until(t, 10.0f, [&] { return act_of(t, woman) == ACTION_FLEE; });
	const int si = idx_of(t, sp);
	check(fled && t.unit_running((size_t)si) && t.unit_running((size_t)idx_of(t, woman)),
			"the woman flees from the spearman and both run");
	const float x0 = x_of(t, woman), z0 = z_of(t, woman);
	t.tick(DT);
	const float moved = std::hypot(x_of(t, woman) - x0, z_of(t, woman) - z0);
	check(moved > t.units_.speed[idx_of(t, woman)] * DT * 1.2f, "she covers more ground per turn than walking");
}

static void test_next_prey() {
	std::printf("41. hunters move on to the next animal\n");
	Simulation s;
	setup(s);
	s.spawn_building_world(-20, 0, BLD_STOREHOUSE, 0, 1.0f);
	s.spawn_building_world(-20, 8, BLD_FARMSTEAD, 0, 1.0f);
	const uint32_t d1 = s.spawn_fauna(4, 0, FAUNA_DEER);
	const uint32_t d2 = s.spawn_fauna(10, 6, FAUNA_DEER);
	s.spawn_fauna(6, -4, FAUNA_WOLF);
	const size_t f1 = s.fauna_id_to_idx_.at(d1);
	s.fauna_.hp[f1] = 1.0f;
	s.fauna_.food_amount[f1] = 5;
	uint32_t c = s.spawn_unit(0, 0, 0, UNIT_CAVALRY);
	s.order_set_stance(&c, 1, STANCE_PASSIVE); // leave the wolf alone
	s.order_hunt(&c, 1, d1);
	const bool next = run_until(s, 60.0f, [&] { return target_of(s, c) == d2; });
	check(next, "a horseman who has eaten up his deer goes for the next deer, not the wolf");
}

static void test_rally_positions() {
	std::printf("42. Shift rally positions, unset-rallypoint, gather near a gone target\n");
	Simulation s;
	setup(s);
	const uint32_t brk = s.spawn_building_world(-20, -20, BLD_BARRACKS, 0, 1.0f);
	const uint32_t tree = s.spawn_resource_world(10, -10, RES_WOOD, 500);
	const uint32_t tree2 = s.spawn_resource_world(12, -12, RES_WOOD, 500);
	auto bref = [&]() -> const Simulation::Building & { return *bld(s, brk); };
	s.cmd_set_rally_point(0, brk, 0, -20);
	s.cmd_set_rally_point(0, brk, 10, -10, tree, 0, true);
	s.cmd_set_rally_point(0, brk, 20, 0, 0, 0, true);
	check(bref().rally_more.size() == 2 && bref().rally_more[0].res_type == RES_WOOD, "Shift adds positions after the first");
	uint32_t sp = s.spawn_unit(-15, -20, 0, UNIT_SPEARMAN);
	s.apply_rally_point(sp, bref());
	const auto *q = s.unit_order_queue(sp);
	check(s.unit_order(sp).kind == ORDER_WALK && q && q->size() == 2 && (*q)[0].kind == ORDER_GATHER &&
			(*q)[0].target == tree && (*q)[1].kind == ORDER_WALK, "a trained unit walks, then gathers, then walks on");

	// gather-near-position: the rally tree is gone.
	s.cmd_set_rally_point(0, brk, 10, -10, tree, 0);
	check(bref().rally_more.empty(), "a click without Shift starts over");
	const int32_t ti = s.res_id_to_idx_.count(tree) ? (int32_t)s.res_id_to_idx_[tree] : -1;
	s.resources_.amount[ti] = 0;
	s.resources_.alive[ti] = 0;
	uint32_t w = s.spawn_unit(-15, -20, 0, UNIT_FEMALE_CITIZEN);
	s.apply_rally_point(w, bref());
	check(s.unit_order(w).kind == ORDER_GATHER && s.unit_order(w).target == tree2, "she gathers the next tree of that kind there");

	// Save round trip.
	s.cmd_set_rally_point(0, brk, 5, 5, 0, 0, true);
	const char *path = "/tmp/rts0_test_rally_positions.sav";
	check(s.save_state(path), "saved");
	Simulation t;
	check(t.load_state(path), "loaded");
	const Simulation::Building *tb = bld(t, brk);
	check(tb && tb->rally_res_type == RES_WOOD && tb->rally_more.size() == 1 && tb->rally_more[0].x == 5.0f,
			"the resource kind and the extra position survive a save");

	s.cmd_unset_rally_point(0, brk);
	check(!bref().has_rally_point && bref().rally_more.empty(), "unset-rallypoint forgets them");

	// Lockstep cmd 14: Shift adds (NET_PARAM_QUEUED), param 1 unsets.
	TurnManager tm;
	NetCommand a;
	a.client_id = 0;
	a.cmd_type = (uint8_t)NetCommandType::NET_CMD_RALLY;
	a.target_id = brk;
	a.target_x = 1;
	a.target_z = -20;
	tm.post_local_command(a);
	NetCommand b2 = a;
	b2.target_x = 2;
	b2.param = NET_PARAM_QUEUED;
	tm.post_local_command(b2);
	for (int i = 0; i < 4; ++i) tm.tick(TurnManager::TURN_DURATION, s);
	check(bref().has_rally_point && bref().rally_x == 1.0f && bref().rally_more.size() == 1, "lockstep Shift rally");
	NetCommand u = a;
	u.param = 1;
	tm.post_local_command(u);
	for (int i = 0; i < 4; ++i) tm.tick(TurnManager::TURN_DURATION, s);
	check(!bref().has_rally_point, "lockstep unset-rallypoint");
}


static void test_flee_source() {
	std::printf("43. a flight re-aims from a chasing attacker and ignores farther ones (UnitAI FLEEING)\n");
	Simulation s;
	setup(s);
	uint32_t w = s.spawn_unit(0, 0, 0, UNIT_FEMALE_CITIZEN);
	s.order_set_stance(&w, 1, STANCE_PASSIVE);
	uint32_t sp = s.spawn_unit(-2, 0, 1, UNIT_SPEARMAN);
	uint32_t far = s.spawn_unit(15, 0, 1, UNIT_ARCHER);
	const int wi = idx_of(s, w), si = idx_of(s, sp);
	s.units_.attacked_by[wi] = sp;
	s.respond_to_attack((size_t)wi);
	check(act_of(s, w) == ACTION_FLEE && s.units_.flee_from[wi] == sp && s.units_.target_x[wi] > 0.0f,
			"she flees east, away from the spearman");
	// The spearman comes round to her east side: she now runs west.
	s.units_.pos_x[si] = 2.0f;
	s.units_.pos_z[si] = 0.0f;
	check(!s.flee_done((size_t)wi) && s.units_.target_x[wi] < 0.0f, "the flight turns away from where he is now");
	// A farther attacker is ignored; a nearer one takes over.
	s.units_.attacked_by[wi] = far;
	s.respond_to_attack((size_t)wi);
	check(s.units_.flee_from[wi] == sp, "an archer farther off than the spearman is ignored");
	uint32_t near = s.spawn_unit(0, 1, 1, UNIT_SPEARMAN);
	s.units_.attacked_by[idx_of(s, w)] = near;
	s.respond_to_attack((size_t)idx_of(s, w));
	check(s.units_.flee_from[idx_of(s, w)] == near, "a nearer attacker is fled from instead");
	// Far enough away: the flight ends.
	const int wj = idx_of(s, w);
	s.units_.pos_z[wj] = 30.0f;
	check(s.flee_done((size_t)wj), "the flight ends once FleeDistance further off");
	s.units_.pos_z[wj] = 0.0f;
	const char *path = "/tmp/rts0_test_flee_source.sav";
	check(s.save_state(path), "saved");
	Simulation t;
	check(t.load_state(path), "loaded");
	const int ti = idx_of(t, w);
	check(ti >= 0 && t.units_.flee_from[ti] == near && t.units_.flee_dist[ti] == s.units_.flee_dist[wj],
			"the one fled from survives a save");
}


static void test_autoharvest() {
	std::printf("44. only the player's own builders farm the field or gather by the dropsite they finish\n");
	Simulation s;
	setup(s);
	const uint32_t store = s.spawn_building_world(0, 0, BLD_STOREHOUSE, 0, 0.9f);
	s.spawn_resource_world(10, 0, RES_WOOD, 500);
	uint32_t mine = s.spawn_unit(-4, 0, 0, UNIT_FEMALE_CITIZEN);
	uint32_t sent = s.spawn_unit(-4, 2, 0, UNIT_FEMALE_CITIZEN);
	s.order_build_or_repair(&mine, 1, store);
	s.order_build_or_repair(&sent, 1, store, false, false); // as UnitAI sends a builder over
	const bool started = run_until(s, 10.0f, [&] { return bld(s, store)->build_progress > 0.91f; });
	const uint8_t f = s.units_.order_flags[idx_of(s, mine)];
	check(started && !(f & ORDER_FLAG_FORCE) && (f & ORDER_FLAG_AUTOHARVEST),
			"at work the player's builder is no longer forced but keeps autoharvest");
	check(!(s.units_.order_flags[idx_of(s, sent)] & ORDER_FLAG_AUTOHARVEST), "the one UnitAI sent has none");
	// Interrupted and back to work: autoharvest stays.
	const UnitOrder o = s.current_order((size_t)idx_of(s, mine));
	s.apply_order(mine, o);
	check(s.units_.order_flags[idx_of(s, mine)] & ORDER_FLAG_AUTOHARVEST, "resuming the build keeps autoharvest");
	const bool done = run_until(s, 40.0f, [&] { return bld(s, store)->build_progress >= 1.0f; });
	run(s, 1.0f);
	check(done && s.unit_order(mine).kind == ORDER_GATHER, "the player's builder gathers the wood nearby");
	check(s.unit_order(sent).kind != ORDER_GATHER, "the other one doesn't");
}


static void test_idle_at_dropsite() {
	std::printf("45. an empty-handed gatherer with nothing left heads to the dropsite (FINDINGNEWTARGET)\n");
	Simulation s;
	setup(s);
	const uint32_t store = s.spawn_building_world(-20, 0, BLD_STOREHOUSE, 0, 1.0f);
	const uint32_t tree = s.spawn_resource_world(10, 0, RES_WOOD, 500);
	uint32_t w = s.spawn_unit(8, 0, 0, UNIT_FEMALE_CITIZEN);
	s.order_gather(&w, 1, tree);
	const bool working = run_until(s, 10.0f, [&] { return act_of(s, w) == ACTION_GATHER; });
	const int wi = idx_of(s, w);
	s.units_.carry[wi] = 0;
	const int ti = (int)s.res_id_to_idx_.at(tree);
	s.resources_.amount[ti] = 0;
	s.resources_.alive[ti] = 0;
	const bool walked = run_until(s, 30.0f, [&] { return dist(s, w, -20, 0) < 6.0f; });
	check(working && walked, "she walks back to the storehouse to idle there");
	(void)store;
}


static void test_chase_into_fog() {
	std::printf("46. a chase is dropped once the target is hidden in the fog (ShouldAbandonChase)\n");
	for (int forced = 0; forced < 2; ++forced) {
		Simulation s;
		setup(s);
		uint32_t a = s.spawn_unit(-40, -30, 0, UNIT_SWORDSMAN, 5000.0f);
		s.order_set_stance(&a, 1, STANCE_VIOLENT);
		uint32_t e = s.spawn_unit(-33, -30, 1, UNIT_CAVALRY, 5000.0f);
		s.order_set_stance(&e, 1, STANCE_PASSIVE);
		if (forced) s.order_attack(&a, 1, e);
		run_until(s, 5.0f, [&] { return target_of(s, a) == e; });
		s.order_walk(&e, 1, 55, -30);
		const bool dropped = run_until(s, 30.0f, [&] { return target_of(s, a) != e; });
		const int ai = idx_of(s, a), ei = idx_of(s, e);
		const bool hidden = !s.los_visible(0, s.units_.pos_x[ei], s.units_.pos_z[ei]);
		check(dropped && hidden && s.unit_order(a).kind != ORDER_ATTACK,
				forced ? "a player's attack on a rider gone into the fog ends" : "so does a violent unit's own chase");
		(void)ai;
	}
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
	test_soldiers_build();
	test_queue();
	test_delete();
	test_back_to_work();
	test_capture();
	test_save_load(save.c_str());
	test_lockstep();
	test_heal_order();
	test_auto_heal();
	test_garrison_approach();
	test_cheer();
	test_capture_rules();
	test_depletion();
	test_repair_follow_up();
	test_same_kind_next();
	test_min_range();
	test_trade_orders();
	test_return_resource();
	test_ungarrison_rally();
	test_alert();
	test_queue_edges();
	test_next_treasure();
	test_preferred_classes();
	test_unitai_combat();
	test_unitai_targets();
	test_forced_work_orders();
	test_loads_and_guards();
	test_remove_guard_call_to_arms();
	test_guard_structure();
	test_unitai_audit();
	test_fog_queued_attack();
	test_run_speed();
	test_next_prey();
	test_rally_positions();
	test_flee_source();
	test_autoharvest();
	test_idle_at_dropsite();
	test_chase_into_fog();
	if (g_failures) {
		std::printf("test_unit_orders: FAIL (%d)\n", g_failures);
		return 1;
	}
	std::printf("test_unit_orders: PASS\n");
	return 0;
}
