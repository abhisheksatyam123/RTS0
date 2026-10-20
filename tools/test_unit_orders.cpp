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
// 43. UnitAI FLEEING: the flight re-aims from where the attacker is now, answers every attacker
//     but the one fled from, and the source survives a save.
// 44. UnitAI REPAIRING autoharvest: a forced build turns into autoharvest at work, and only
//     such builders farm the field or gather near the dropsite they finish.
// 45. GATHER FINDINGNEWTARGET: an empty-handed gatherer with nothing left nearby walks to the
//     nearest dropsite to idle.
//     A hunter walking back to its eaten carcass (GatherNearPosition) keeps to the same animal:
//     it hunts one there or goes on to the dropsite, never the berry bush beside the spot.
// 46. ShouldAbandonChase: once a second a chase whose target is hidden in the player's fog ends,
//     a player's attack too (CheckTargetVisible).
// 47. Attacked by a unit hidden in the fog: the unit doesn't charge it (APPROACHING enter).
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
#include <cstring>
#include <string>
#include <tuple>
#include <utility>

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
	// Player 1's territory over the house, out of the swordsmen's sight: on neutral land it
	// decays to Gaia mid-fight, and attack-walk leaves Gaia's structures alone.
	s.spawn_building_world(4.0f, -36.0f, BLD_TOWN_CENTER, 1, 1.0f);
	const float house_hp = bld(s, house)->hp;
	s.order_attack_move(a, 3, 30, 0);
	const bool killed = run_until(s, 20.0f, [&] { return !alive(s, e); });
	check(killed, "kills the enemy on the way");
	const bool hit = run_until(s, 20.0f, [&] { const Simulation::Building *b = bld(s, house); return !b || b->hp < house_hp; });
	check(hit, "then attacks the enemy structure on the way");
	// Three swordsmen need more than 200 s on a house at 0.28's hack 24.
	const bool razed = run_until(s, 400.0f, [&] { const Simulation::Building *b = bld(s, house); return !b || b->wrecked; });
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

// UnitAI ESCORTING: a guard within three guard ranges of the unit it guards walks no faster
// than that unit moves (TryMatchTargetSpeed), so it doesn't overtake a slow one.
static void test_guard_speed() {
	std::printf("5b. guard matches the guarded unit's speed\n");
	Simulation s;
	setup(s);
	uint32_t vip = s.spawn_unit(0, 0, 0, UNIT_HERO, 5000.0f);
	s.order_set_stance(&vip, 1, STANCE_PASSIVE);
	uint32_t g = s.spawn_unit(-6, 0, 0, UNIT_SWORDSMAN);
	const size_t vi = (size_t)idx_of(s, vip), gi = (size_t)idx_of(s, g);
	s.units_.speed[vi] = s.unit_walk_speed(gi) * 0.4f;
	s.order_guard(&g, 1, vip);
	s.order_walk(&vip, 1, 40, 0);
	run(s, 1.0f);
	float fastest = 0.0f;
	for (int k = 0; k < 100; ++k) {
		const float x0 = x_of(s, g), z0 = z_of(s, g);
		s.tick(DT);
		fastest = std::max(fastest, std::hypot(x_of(s, g) - x0, z_of(s, g) - z0) / DT);
	}
	const float vs = s.unit_walk_speed(vi), gs = s.unit_walk_speed(gi);
	std::printf("  guarded walks %.2f, guard could walk %.2f, walked at most %.2f\n", vs, gs, fastest);
	check(fastest <= vs * 1.05f + 0.01f, "the guard walks no faster than the unit it guards");
	check(dist(s, g, x_of(s, vip), z_of(s, vip)) < 9.0f, "and keeps up with it");
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
	// unit_actions.js g_PatrolTargets ["Unit"]: a patrol walks past an enemy structure on its way.
	const uint32_t house = s.spawn_building_world(0, 3, BLD_HOUSE, 1, 1.0f);
	const float hp0 = bld(s, house)->hp;
	check((s.unit_order(p).flags & ORDER_FLAG_UNITS_ONLY) != 0, "a patrol fights units only");
	run(s, 20.0f);
	std::printf("  enemy house beside the patrol: %.0f of %.0f hp\n", bld(s, house)->hp, hp0);
	check(bld(s, house)->hp >= hp0, "and leaves an enemy house beside its way alone");
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
	// The support worker (0.28's civilian, template_unit_support) is passive too: it flees,
	// it doesn't stab back with its dagger.
	Simulation v;
	setup(v);
	const uint32_t tree2 = v.spawn_resource_world(0, 0, RES_WOOD, 5000);
	uint32_t c = v.spawn_unit(1.5f, 0, 0, UNIT_VILLAGER);
	check(v.units_.stance[idx_of(v, c)] == STANCE_PASSIVE, "a civilian starts passive");
	v.order_gather(&c, 1, tree2);
	run(v, 1.0f);
	uint32_t e2 = v.spawn_unit(4, 0, 1, UNIT_SWORDSMAN);
	v.order_attack(&e2, 1, c);
	check(run_until(v, 4.0f, [&] { return act_of(v, c) == ACTION_FLEE; }), "a civilian flees when attacked");
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
	s.set_explore_map(true); // FindNearbyResource takes only supplies the player has seen
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

// Player 1's territory over a structure at the origin, so it doesn't decay to Gaia on
// neutral land (TerritoryDecay.js); the civic centre's arrows don't reach the units here.
static void root_origin(Simulation &s) { s.spawn_building_world(0.0f, 26.0f, BLD_TOWN_CENTER, 1, 1.0f); }

static void test_capture() {
	std::printf("13. capture is an attack\n");
	Simulation s;
	setup(s);
	const uint32_t house = s.spawn_building_world(0, 0, BLD_BARRACKS, 1, 1.0f);
	root_origin(s);
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
	root_origin(t);
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
	s.set_explore_map(true); // the tree is in land player 0 hasn't seen: a gather there is refused
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
	// Stop keeps the queue from its first work order (UpdateWorkOrders): the queued gather.
	check(s.unit_order(w).kind == ORDER_GATHER && s.unit_order(w).target == tree,
			"Stop kept the queued gather: back to work goes to it");
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
	const uint32_t house = s.spawn_building_world(0, 0, BLD_BARRACKS, 0, 1.0f);
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
	check(in, "gets in once it reaches the barracks (a house takes Support only)");
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
		const uint32_t house = s.spawn_building_world(0, 0, BLD_BARRACKS, 1, 1.0f);
		root_origin(s);
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
		root_origin(s);
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
		root_origin(s);
		const float cp = bld(s, house)->capture_points_by_team[1];
		// A woman: the civilian 0.28 trains in her place (also UNIT_FEMALE_CITIZEN) captures at 1.
		uint32_t w = s.spawn_unit(-5, 0, 0, UNIT_FEMALE_CITIZEN, 0.0f, civ_unit_template_by_id("athen/support_female_citizen"));
		s.order_attack_building(&w, 1, house, false, true);
		run(s, 10.0f);
		check(bld(s, house)->capture_points_by_team[1] >= cp - 0.01f, "women have no capture attack");
	}
	{
		// 0.28 Attack/Capture/Capture per template and Capturable/RegenRate per structure.
		auto cap = [](const char *id) {
			const uint16_t t = civ_unit_template_by_id(id);
			return unit_capture_attack(t, civ_unit_template(t).unit_type);
		};
		check(cap("athen/support_civilian") == 1.0f && cap("athen/infantry_spearman_b") == 2.5f, "civilians 1, infantry 2.5");
		check(cap("athen/cavalry_javelineer_b") == 1.75f && cap("cart/champion_cavalry") == 3.5f, "cavalry 1.75, champion cavalry 3.5");
		check(cap("cart/champion_elephant") == 0.0f && cap("cart/hero_hannibal") == 0.0f && cap("brit/war_dog") == 0.0f,
		      "war elephants, hero elephants and dogs don't capture");
		check(cap("han/support_minister") == 4.0f, "the Han minister captures at 4");
		check(building_type_info(BLD_TOWN_CENTER).capture_regen_rate == 30.0f && building_type_info(BLD_FORTRESS).capture_regen_rate == 45.0f &&
		      building_type_info(BLD_TEMPLE).capture_regen_rate == 15.0f && building_type_info(BLD_TOWER).capture_regen_rate == 10.0f &&
		      building_type_info(BLD_BARRACKS).capture_regen_rate == 10.0f && building_type_info(BLD_HOUSE).capture_regen_rate == 5.0f,
		      "structures regain 5 a second, military and towers 10, temples 15, civic centres 30, fortresses 45");
		check(building_type_info(BLD_DOCK).territory_decay_rate == 0.0f, "docks don't decay");
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
	s.set_explore_map(true); // FindNearbyResource takes only supplies the player has seen
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
	s.set_explore_map(true); // FindNearbyResource takes only supplies the player has seen
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
	const uint32_t house = s.spawn_building_world(0, 0, BLD_BARRACKS, 0, 1.0f);
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
	const uint32_t h2 = l.spawn_building_world(0, 0, BLD_BARRACKS, 0, 1.0f);
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
	// AlertRaiser/List "Civilian": a legacy Female Citizen (no Civilian class) stays put.
	uint32_t legacy = s.spawn_unit(2, 5, 0, UNIT_FEMALE_CITIZEN, 0.0f, civ_unit_template_by_id("athen/support_female_citizen"));
	run(s, 3.0f);
	check(s.cmd_raise_alert(0, store), "a storehouse raises the alert");
	check(s.unit_order(legacy).kind != ORDER_GARRISON, "a legacy Female Citizen ignores it");
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
	// Order.Gather on a supply it can't gather -> FINDINGNEWTARGET, whose first step is "give up
	// on this order and try our next queued order" (FinishOrder): the walk after it.
	bool gathered_t2 = false;
	const bool walking = run_until(s, 30.0f, [&] {
		gathered_t2 |= s.unit_order(w).kind == ORDER_GATHER && s.unit_order(w).target == t2;
		return s.unit_order(w).kind == ORDER_WALK && s.unit_order(w).x < 0.0f;
	});
	check(walking && !gathered_t2, "the gather of the cut tree is skipped for the walk after it");
	check(!s.unit_order_queue(w) || s.unit_order_queue(w)->empty(), "nothing left queued");

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
		d.param = NET_PARAM_UNITS_ONLY; // session.attackmoveUnit held
		tm.post_local_command(d);
		for (int i = 0; i < 4; ++i) tm.tick(TurnManager::TURN_DURATION, l);
		check(c.cmd_type == 37 && l.unit_order(g).kind == ORDER_NONE, "lockstep remove-guard (37)");
		check(d.cmd_type == 38 && l.unit_order(a).kind == ORDER_WALK_AND_FIGHT, "lockstep call-to-arms (38)");
		check((l.unit_order(a).flags & ORDER_FLAG_UNITS_ONLY) && !(l.unit_order(a).flags & ORDER_FLAG_ALLOW_CAPTURE),
				"call-to-arms with session.attackmoveUnit fights units only, and captures only with session.capture");
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
	t.tick(DT); // gaining speed from rest (UnitMotion Acceleration); the flee is over within four turns
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
	s.set_explore_map(true); // FindNearbyResource takes only supplies the player has seen
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
	std::printf("43. a flight re-aims from a chasing attacker and from any other one (UnitAI FLEEING)\n");
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
	// The one fled from is ignored; any other attacker takes over, a farther one too (0.28's
	// distance check compares the unit's distance to itself).
	s.units_.attacked_by[wi] = sp;
	s.respond_to_attack((size_t)wi);
	check(s.units_.flee_from[wi] == sp && s.units_.target_x[wi] < 0.0f, "another hit from the spearman changes nothing");
	s.units_.attacked_by[wi] = far;
	s.respond_to_attack((size_t)wi);
	check(s.units_.flee_from[wi] == far && s.units_.target_x[wi] < 0.0f, "an archer farther off is fled from instead");
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
	s.set_explore_map(true); // FindNearbyResource takes only supplies the player has seen
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

// UnitAI.js FINDINGNEWTARGET: a gatherer far from its exhausted patch, with nothing near it
// or where it started, walks back to where it started and searches again first
// (GatherNearPosition, UnitAI.js:2640), instead of going to a dropsite at once.
static void test_gather_walk_back() {
	std::printf("45b. a gatherer far from its exhausted patch walks back to start first (FINDINGNEWTARGET)\n");
	Simulation s;
	setup(s);
	s.spawn_building_world(-20, 0, BLD_STOREHOUSE, 0, 1.0f);
	const uint32_t tree = s.spawn_resource_world(10, 0, RES_WOOD, 500);
	uint32_t w = s.spawn_unit(8, 0, 0, UNIT_FEMALE_CITIZEN);
	s.order_gather(&w, 1, tree);
	check(run_until(s, 10.0f, [&] { return act_of(s, w) == ACTION_GATHER; }), "she gathers the tree");
	// Driven far off, with the patch exhausted while away and nothing else around.
	int wi = idx_of(s, w);
	s.units_.pos_x[wi] = 40.0f;
	s.units_.pos_z[wi] = 0.0f;
	s.units_.target_x[wi] = 40.0f;
	s.units_.target_z[wi] = 0.0f;
	s.units_.waypoint_cur[wi] = 0;
	s.units_.waypoint_count[wi] = 0;
	s.units_.carry[wi] = 0;
	const int ti = (int)s.res_id_to_idx_.at(tree);
	s.resources_.amount[ti] = 0;
	s.resources_.alive[ti] = 0;
	const bool leg = run_until(s, 10.0f, [&] {
		const int k = idx_of(s, w);
		return s.unit_order(w).kind == ORDER_GATHER && s.units_.target_resource_id[k] == 0 &&
			s.units_.action_state[k] == ACTION_MOVE;
	});
	check(leg, "she walks back to where the patch was");
	const bool back = run_until(s, 60.0f, [&] { return dist(s, w, 10, 0) < 5.0f; });
	check(back, "she reaches the old patch");
	const bool idles = run_until(s, 60.0f, [&] { return dist(s, w, -20, 0) < 6.0f; });
	check(idles, "with nothing there she goes on to the storehouse");
}

// GatherNearPosition keeps the order's type and template (UnitAI.js:5705-5711), so a hunter
// walking back to where its carcass was searches there for the same animal (FINDINGNEWTARGET's
// filter, UnitAI.js:2622-2629), never for food of another kind: a rider can't gather the berry
// bush beside the spot (CanGather in FindNearbyResource, UnitAI.js:4478). With no deer left there
// it goes on to the farmstead; with a deer there it hunts it. Deep Forest soak (seed 2): a rider
// whose walk back had lost the meat kind stood at its old carcass for the rest of the match.
static void test_hunter_walk_back() {
	std::printf("45c. a hunter walking back to its eaten carcass keeps to meat (GatherNearPosition type and template)\n");
	for (int with_deer = 0; with_deer < 2; ++with_deer) {
		Simulation s;
		setup(s);
		s.set_explore_map(true); // FindNearbyResource takes only supplies the player has seen
		s.spawn_building_world(-20, 0, BLD_FARMSTEAD, 0, 1.0f);
		const uint32_t deer = s.spawn_fauna(10, 0, FAUNA_DEER);
		s.fauna_.hp[s.fauna_id_to_idx_.at(deer)] = 1.0f;
		uint32_t c = s.spawn_unit(6, 0, 0, UNIT_CAVALRY);
		s.order_set_stance(&c, 1, STANCE_PASSIVE);
		s.order_hunt(&c, 1, deer);
		const bool carving = run_until(s, 20.0f, [&] {
			const int k = idx_of(s, c);
			const uint32_t r = s.units_.target_resource_id[k];
			return s.units_.action_state[k] == ACTION_GATHER && r != 0 && s.res_id_to_idx_.count(r) &&
				s.resources_.subtype[s.res_id_to_idx_.at(r)] == RES_SUB_MEAT;
		});
		check(carving, "the rider kills the deer and gathers its carcass");
		if (!carving) continue;
		int ci = idx_of(s, c);
		const uint32_t carcass = s.units_.target_resource_id[ci];
		const int ri = (int)s.res_id_to_idx_.at(carcass);
		const float kx = s.resources_.x[ri], kz = s.resources_.z[ri];
		// A berry bush beside the spot: food, but not meat.
		const uint32_t berries = s.spawn_resource_world(kx + 1.0f, kz + 1.0f, RES_FOOD, 200);
		// Driven far off, out of search range of the spot, with the carcass eaten while away.
		s.units_.pos_x[ci] = 40.0f;
		s.units_.pos_z[ci] = 0.0f;
		s.units_.target_x[ci] = 40.0f;
		s.units_.target_z[ci] = 0.0f;
		s.units_.waypoint_cur[ci] = 0;
		s.units_.waypoint_count[ci] = 0;
		s.units_.carry[ci] = 0;
		s.resources_.amount[ri] = 0;
		s.resources_.alive[ri] = 0;
		const uint32_t deer2 = with_deer ? s.spawn_fauna(kx + 2.0f, kz - 2.0f, FAUNA_DEER) : 0;
		const bool leg = run_until(s, 10.0f, [&] {
			const int k = idx_of(s, c);
			return s.unit_order(c).kind == ORDER_GATHER && s.units_.target_resource_id[k] == 0 &&
				s.units_.action_state[k] == ACTION_MOVE;
		});
		ci = idx_of(s, c);
		const int sub = (s.units_.order_flags[ci] & ORDER_FLAG_SUB_MASK) >> ORDER_FLAG_SUB_SHIFT;
		check(leg && sub == RES_SUB_MEAT && s.units_.hunt_fauna[ci] == FAUNA_DEER + 1,
				"he walks back to the spot still after the deer's meat");
		bool berry = false;
		if (with_deer) {
			const bool hunts = run_until(s, 60.0f, [&] {
				berry |= s.units_.target_resource_id[idx_of(s, c)] == berries;
				return target_of(s, c) == deer2;
			});
			check(hunts && !berry, "with a deer there he hunts it, not the berries");
		} else {
			const bool home = run_until(s, 60.0f, [&] {
				berry |= s.units_.target_resource_id[idx_of(s, c)] == berries;
				return dist(s, c, -20, 0) < 6.0f && s.unit_order(c).kind != ORDER_GATHER;
			});
			check(home && !berry, "with nothing to hunt there he goes on to the farmstead, not to the berries");
		}
	}
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


static void test_hidden_attacker() {
	std::printf("47. a unit doesn't charge an attacker hidden in the fog (APPROACHING, CheckTargetVisible)\n");
	Simulation s;
	setup(s);
	uint32_t sw = s.spawn_unit(-40, 0, 0, UNIT_SWORDSMAN, 5000.0f);
	uint32_t far = s.spawn_unit(10, 0, 1, UNIT_ARCHER, 5000.0f);
	s.order_set_stance(&far, 1, STANCE_PASSIVE);
	run(s, DT); // line of sight
	const int si = idx_of(s, sw);
	check(!s.los_visible(0, x_of(s, far), z_of(s, far)), "the archer stands in the fog");
	s.units_.attacked_by[si] = far;
	s.respond_to_attack((size_t)si);
	check(target_of(s, sw) != far && act_of(s, sw) != ACTION_ATTACK, "the swordsman doesn't go for it");
	s.set_reveal_map(true);
	s.units_.attacked_by[si] = far;
	s.respond_to_attack((size_t)si);
	check(target_of(s, sw) == far, "one in sight is answered");
}

static void test_patrol_loop() {
	std::printf("48. queued patrol points are walked round in a loop (PATROLLING MovementUpdate)\n");
	Simulation s;
	setup(s);
	uint32_t p = s.spawn_unit(-10, 0, 0, UNIT_SPEARMAN);
	s.order_patrol(&p, 1, 10, 0);
	s.order_patrol(&p, 1, 10, 15, true);
	s.order_patrol(&p, 1, -10, 15, true);
	const float wx[3] = {10, 10, -10}, wz[3] = {0, 15, 15};
	std::vector<int> seen;
	int at = -1;
	for (int i = 0; i < 600 && seen.size() < 7; ++i) { // 120 s
		s.tick(DT);
		int now = -1;
		for (int k = 0; k < 3; ++k)
			if (dist(s, p, wx[k], wz[k]) < 1.5f) now = k;
		if (now >= 0 && now != at) seen.push_back(now);
		if (now >= 0) at = now;
	}
	std::string order;
	for (int k : seen) order += (char)('A' + k);
	std::printf("  waypoints in order: %s\n", order.c_str());
	check(order.rfind("ABCABCA", 0) == 0, "after the last point it goes back to the first, not the one before");
}

static void test_patrol_wait(const char *path) {
	std::printf("49. a patrolling unit surveys each waypoint for PatrolWaitTime (CHECKINGWAYPOINT)\n");
	Simulation s;
	setup(s);
	uint32_t p = s.spawn_unit(-10, 0, 0, UNIT_SPEARMAN);
	s.order_patrol(&p, 1, 10, 0);
	check(run_until(s, 20.0f, [&] { return dist(s, p, 10, 0) < 1.5f; }), "reaches the far end");
	float px = x_of(s, p), pz = z_of(s, p);
	int still = 0;
	for (int k = 0; k < 25; ++k) { // 5 s
		run(s, DT);
		const float nx = x_of(s, p), nz = z_of(s, p);
		if (std::fabs(nx - px) < 1e-4f && std::fabs(nz - pz) < 1e-4f) ++still;
		else if (still > 0) break;
		px = nx; pz = nz;
	}
	std::printf("  stood still %.1f s at the waypoint\n", still * DT);
	check(still * DT >= 0.8f && still * DT <= 1.6f, "it waits about a second there");
	check(run_until(s, 20.0f, [&] { return dist(s, p, -10, 0) < 1.5f; }), "then walks back to the start");
	// Saved mid-wait, the wait carries on the same way.
	check(run_until(s, 2.0f, [&] { return s.units_.patrol_wait[idx_of(s, p)] > 0.3f; }), "waits at the start too");
	check(s.save_state(path), "saved while waiting");
	Simulation t;
	check(t.load_state(path), "loaded");
	run(s, 3.0f);
	run(t, 3.0f);
	check(std::fabs(x_of(s, p) - x_of(t, p)) < 1e-4f && std::fabs(z_of(s, p) - z_of(t, p)) < 1e-4f,
			"both copies leave the waypoint together");
	std::remove(path);
}

static uint16_t roster_named(const char *suffix) {
	for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT; ++t) {
		const char *id = civ_unit_template(t).id;
		const size_t n = std::strlen(id), m = std::strlen(suffix);
		if (n >= m && std::strcmp(id + n - m, suffix) == 0) return t;
	}
	return CIV_UNIT_TEMPLATE_NONE;
}
static Simulation::Building *bld_mut(Simulation &s, uint32_t id) {
	for (Simulation::Building &b : s.buildings_)
		if (b.id == id) return &b;
	return nullptr;
}

static void test_capture_regen() {
	std::printf("50. capture regen: the phases' GarrisonRegenRate, a garrisoned hero, once a second (Capturable.js)\n");
	Simulation s;
	setup(s);
	const uint32_t bar = s.spawn_building_world(-20, -20, BLD_BARRACKS, 0, 1.0f);
	Simulation::Building *b = bld_mut(s, bar);
	const float base = b->capture_garrison_regen_rate;
	check(std::fabs(s.structure_capture_garrison_regen(*b) - base) < 1e-5f, "GarrisonRegenRate starts at the template's");
	s.players_[0].techs.set(TECH_PHASE_TOWN);
	s.refresh_tech_mods(0);
	check(std::fabs(s.structure_capture_garrison_regen(*b) - (base + 0.5f)) < 1e-5f, "Town Phase adds 0.5");
	s.players_[0].techs.set(TECH_PHASE_CITY);
	s.refresh_tech_mods(0);
	check(std::fabs(s.structure_capture_garrison_regen(*b) - (base + 1.5f)) < 1e-5f, "City Phase adds 1 more");
	// A garrisoned hero: Inspired Defense, +1 however many are inside.
	const uint16_t per = roster_named("athen/hero_pericles"), leo = roster_named("spart/hero_leonidas");
	check(per != CIV_UNIT_TEMPLATE_NONE && leo != CIV_UNIT_TEMPLATE_NONE, "roster has Pericles and Leonidas");
	uint32_t heroes[2] = {s.spawn_unit(-20, -12, 0, UNIT_HERO, 0.0f, per), s.spawn_unit(-21, -12, 0, UNIT_HERO, 0.0f, leo)};
	for (uint32_t h : heroes) s.garrison_now((size_t)idx_of(s, h), *bld_mut(s, bar));
	b = bld_mut(s, bar);
	check(b->garrisoned_units.size() == 2, "two heroes inside");
	check(std::fabs(s.structure_capture_garrison_regen(*b) - (base + 2.5f)) < 1e-5f, "a garrisoned hero adds 1, once");
	// TimerTick: points come back once a second, not a little every tick.
	b->capture_points_by_team[0] -= 100.0f;
	b->capture_points_by_team[1] += 100.0f;
	int steps = 0;
	float last = b->capture_points_by_team[0];
	for (int k = 0; k < 40; ++k) { // 2 s
		s.tick(Simulation::FIXED_STEP);
		const float now = bld(s, bar)->capture_points_by_team[0];
		if (now != last) ++steps;
		last = now;
	}
	std::printf("  regained in %d steps over 2 s\n", steps);
	check(steps == 2, "the garrison regains its points in one step a second");
}

static void test_capture_auras() {
	std::printf("51. capture auras: Pericles, Alexander and the Temple of Vesta (Auras.js)\n");
	Simulation s;
	setup(s);
	s.set_team_civ(1, CIV_ROMANS);
	const uint32_t house = s.spawn_building_world(-30, 10, BLD_HOUSE, 0, 1.0f);
	const float max0 = bld(s, house)->max_capture_points;
	const float own0 = bld(s, house)->capture_points_by_team[0];
	const uint32_t per = s.spawn_unit(-30, 16, 0, UNIT_HERO, 0.0f, roster_named("athen/hero_pericles"));
	run(s, 1.0f);
	check(std::fabs(bld(s, house)->max_capture_points - max0 * 1.5f) < 0.01f, "Pericles: own structures within 60 m +50% capture points");
	check(std::fabs(bld(s, house)->capture_points_by_team[0] - own0 * 1.5f) < 0.01f, "and the owner's points scale with it");
	const int pi = idx_of(s, per);
	s.units_.pos_x[pi] = 30.0f; s.units_.pos_z[pi] = 30.0f;
	run(s, 1.0f);
	check(std::fabs(bld(s, house)->max_capture_points - max0) < 0.01f, "back to the template's once he leaves");
	// Alexander halves an enemy Civil Centre's GarrisonRegenRate, not other structures'.
	Simulation a;
	setup(a);
	const Simulation::Building *cc = nullptr;
	for (const auto &b : a.buildings()) if (b.team == 1 && b.type == BLD_TOWN_CENTER) cc = &b;
	const uint32_t cc_id = cc->id;
	const uint32_t bar = a.spawn_building_world(50, 44, BLD_BARRACKS, 1, 1.0f);
	const float cc_rate = a.structure_capture_garrison_regen(*bld(a, cc_id));
	const float bar_rate = a.structure_capture_garrison_regen(*bld(a, bar));
	a.spawn_unit(50, 50, 0, UNIT_HERO, 0.0f, roster_named("mace/hero_alexander_iii"));
	run(a, 1.0f);
	check(std::fabs(a.structure_capture_garrison_regen(*bld(a, cc_id)) - cc_rate * 0.5f) < 1e-5f,
			"Alexander: an enemy Civil Centre's garrison regeneration -50%");
	check(std::fabs(a.structure_capture_garrison_regen(*bld(a, bar)) - bar_rate) < 1e-5f, "an enemy barracks keeps its own");
	// Eternal Fire: the Temple of Vesta gives own structures within 75 m +50% capture points.
	Simulation v;
	setup(v);
	v.set_team_civ(0, CIV_ROMANS);
	const uint32_t h = v.spawn_building_world(-30, 10, BLD_HOUSE, 0, 1.0f);
	const float hmax = bld(v, h)->max_capture_points;
	v.spawn_building_world(-30, 25, BLD_TEMPLE_VESTA, 0, 1.0f);
	run(v, 1.0f);
	check(std::fabs(bld(v, h)->max_capture_points - hmax * 1.5f) < 0.01f, "Eternal Fire: own structures within 75 m +50% capture points");
}

static void test_capture_owner_change() {
	std::printf("52. capture points on defeat, conversion and capture (OnOwnershipChanged, OnGlobalPlayerDefeated)\n");
	Simulation s;
	setup(s);
	s.set_player_active(2, true, false);
	const uint32_t theirs = s.spawn_building_world(20, 20, BLD_BARRACKS, 1, 1.0f);
	const uint32_t mine = s.spawn_building_world(-20, -20, BLD_BARRACKS, 0, 1.0f);
	Simulation::Building *t = bld_mut(s, theirs), *m = bld_mut(s, mine);
	const float tmax = t->max_capture_points;
	t->capture_points_by_team[1] -= 100.0f; t->capture_points_by_team[0] += 100.0f;
	m->capture_points_by_team[0] -= 60.0f; m->capture_points_by_team[1] += 60.0f;
	s.defeat_player(1);
	t = bld_mut(s, theirs); m = bld_mut(s, mine);
	// Gaia has no civ bonus: the maximum drops to the template's and every side's points with it.
	const float gmax = building_type_info(BLD_BARRACKS).capture_max_points;
	const float held = t->capture_points_by_team[0];
	check(t->team == GAIA_PLAYER && std::fabs(held - 100.0f * gmax / tmax) < 0.01f,
			"a defeated player's structure goes to Gaia and player 0 keeps its points");
	check(t->capture_points_by_team[1] == 0.0f && std::fabs(t->max_capture_points - gmax) < 0.01f,
			"the defeated player's points are Gaia's, at Gaia's maximum");
	check(m->capture_points_by_team[1] == 0.0f, "what the defeated held of player 0's barracks goes to Gaia");
	// Capture: the new owner's civ bonus sets the maximum (greek_structures +10%).
	Simulation c;
	setup(c);
	c.set_team_civ(0, CIV_ATHENIANS);
	c.set_team_civ(1, CIV_ROMANS);
	const uint32_t house = c.spawn_building_world(10, 10, BLD_HOUSE, 1, 1.0f);
	Simulation::Building *hb = bld_mut(c, house);
	const float hmax = hb->max_capture_points;
	hb->capture_points_by_team[1] = 0.0f; // as a capture attack left it
	hb->capture_points_by_team[0] = hmax;
	run(c, 1.0f);
	hb = bld_mut(c, house);
	check(hb->team == 0, "the house changes hands");
	check(std::fabs(hb->max_capture_points - hmax * 1.1f) < 0.01f, "and takes its new Athenian owner's +10% capture points");
}

static void test_capture_attack_auras() {
	std::printf("53. capture attack auras: Leonidas adds 1, the carnyx takes 10%% once (Auras.js, ModifiersManager)\n");
	Simulation s;
	setup(s);
	const uint16_t spear = roster_named("spart/infantry_spearman_b"), leo = roster_named("spart/hero_leonidas"),
		horn = roster_named("gaul/champion_infantry_trumpeter");
	check(spear != CIV_UNIT_TEMPLATE_NONE && leo != CIV_UNIT_TEMPLATE_NONE && horn != CIV_UNIT_TEMPLATE_NONE,
			"roster has the Spartan spearman, Leonidas and the trumpeter");
	const uint32_t u = s.spawn_unit(-20, -20, 0, (UnitType)civ_unit_template(spear).unit_type, 0.0f, spear);
	const int ui = idx_of(s, u);
	const float c0 = s.unit_capture_attack_at((size_t)ui);
	check(c0 > 0.0f, "a spearman captures");
	// Last Stand: Spearmen within 30 m (8.4 units) +1 capture attack strength.
	const uint32_t hero = s.spawn_unit(-20, -15, 0, UNIT_HERO, 0.0f, leo);
	run(s, 0.5f);
	std::printf("  spearman capture %.2f alone, %.2f beside Leonidas\n", c0, s.unit_capture_attack_at((size_t)ui));
	check(std::fabs(s.unit_capture_attack_at((size_t)ui) - (c0 + 1.0f)) < 1e-4f, "Leonidas: a spearman beside him +1");
	const int hi = idx_of(s, hero);
	s.units_.pos_x[hi] = -20.0f; s.units_.pos_z[hi] = -5.0f; // 15 units off
	run(s, 0.5f);
	check(std::fabs(s.unit_capture_attack_at((size_t)ui) - c0) < 1e-4f, "out of his 30 m the spearman's own again");
	s.units_.pos_z[hi] = -15.0f;
	// Intimidating Sound: enemy Soldiers within 20 m (5.6 units) x0.9, applied before the add,
	// and two trumpeters weaken no more than one.
	s.spawn_unit(-16, -20, 1, UNIT_CARNYX, 0.0f, horn);
	s.spawn_unit(-16, -21, 1, UNIT_CARNYX, 0.0f, horn);
	run(s, 0.5f);
	check(std::fabs(s.unit_capture_attack_at((size_t)ui) - (c0 * 0.9f + 1.0f)) < 1e-4f,
			"two enemy trumpeters: x0.9 once, then Leonidas' +1");
	// A garrisoned unit is out of every range aura.
	const uint32_t bar = s.spawn_building_world(-30, -30, BLD_BARRACKS, 0, 1.0f);
	s.garrison_now((size_t)ui, *bld_mut(s, bar));
	run(s, 0.5f);
	check(std::fabs(s.unit_capture_attack_at((size_t)idx_of(s, u)) - c0) < 1e-4f, "garrisoned: its own capture strength");
}

// Kill victim with killer (melee) and return what killer's owner gained.
static void loot_kill(Simulation &s, uint32_t killer, uint32_t victim, float out[RES_COUNT]) {
	float before[RES_COUNT];
	const uint8_t p = s.units_.team[idx_of(s, killer)];
	for (int r = 0; r < RES_COUNT; ++r) before[r] = s.player(p).res[r];
	s.order_attack(&killer, 1, victim);
	for (int k = 0; k < 200; ++k) {
		s.tick(DT);
		const int v = idx_of(s, victim);
		if (v < 0 || !s.units_.alive[v]) break;
	}
	for (int r = 0; r < RES_COUNT; ++r) out[r] = s.player(p).res[r] - before[r];
}

static void test_loot_auras() {
	std::printf("54. loot auras: Brennus +15 metal loot, Pericles' units give none (Looter.js, Loot.js)\n");
	const uint16_t sword = roster_named("gaul/infantry_spearman_b"), spear = roster_named("athen/infantry_spearman_b");
	check(sword != CIV_UNIT_TEMPLATE_NONE && spear != CIV_UNIT_TEMPLATE_NONE, "roster has the Gallic and Athenian spearmen");
	// mode 0: no hero; 1: Brennus beside the killer; 2: Pericles beside the victim.
	float gain[3][RES_COUNT];
	uint16_t xp[3];
	for (int mode = 0; mode < 3; ++mode) {
		Simulation s;
		setup(s);
		const uint32_t k = s.spawn_unit(0, 0, 0, (UnitType)civ_unit_template(sword).unit_type, 0.0f, sword);
		const uint32_t v = s.spawn_unit(1.5f, 0, 1, (UnitType)civ_unit_template(spear).unit_type, 1.0f, spear);
		if (mode == 1) s.spawn_unit(-3, 0, 0, UNIT_HERO, 0.0f, roster_named("gaul/hero_brennus"));
		if (mode == 2) s.spawn_unit(4.5f, 0, 1, UNIT_HERO, 0.0f, roster_named("athen/hero_pericles"));
		run(s, 0.5f); // auras rebuilt
		xp[mode] = (uint16_t)s.unit_loot_xp((size_t)idx_of(s, v)); // Loot/xp shared out per hit
		loot_kill(s, k, v, gain[mode]);
		std::printf("  mode %d: food %.0f wood %.0f stone %.0f metal %.0f, victim xp %u\n", mode, gain[mode][RES_FOOD],
			gain[mode][RES_WOOD], gain[mode][RES_STONE], gain[mode][RES_GOLD], (unsigned)xp[mode]);
	}
	check(gain[0][RES_FOOD] > 0.0f && xp[0] > 0, "a kill pays loot, and the victim is worth xp");
	check(std::fabs(gain[1][RES_GOLD] - gain[0][RES_GOLD] - 15.0f) < 1e-3f && gain[1][RES_FOOD] == gain[0][RES_FOOD],
			"Brennus: his Humans gain +15 metal loot");
	check(gain[2][RES_FOOD] == 0.0f && gain[2][RES_WOOD] == 0.0f && gain[2][RES_GOLD] == 0.0f && xp[2] == 0,
			"Pericles: a soldier beside him gives no loot and no xp");
}

static void test_hero_global_auras() {
	std::printf("55. hero global auras: Indibil, Pericles and Themistocles change costs while they live\n");
	Simulation s;
	setup(s);
	const uint16_t spear = roster_named("iber/infantry_spearman_b");
	check(spear != CIV_UNIT_TEMPLATE_NONE, "roster has the Iberian spearman");
	auto near1 = [](float a, float b) { return std::fabs(a - b) < 1e-5f; };
	check(near1(s.tech_mods(0).tmpl[spear].cost[RES_FOOD], 1.0f), "no hero: soldiers at full cost");
	// Indibil's Wealth of Iberia: soldiers -15% cost and -20% training time.
	const uint32_t ind = s.spawn_unit(-20, -20, 0, UNIT_HERO, 0.0f, roster_named("iber/hero_indibil"));
	for (int t = 0; t < 22; ++t) s.tick(Simulation::FIXED_STEP); // a global-aura pulse
	check(near1(s.tech_mods(0).tmpl[spear].cost[RES_FOOD], 0.85f) && near1(s.tech_mods(0).tmpl[spear].cost[RES_GOLD], 0.85f),
			"Indibil: his player's soldiers -15% cost");
	check(near1(s.tech_mods(0).tmpl[spear].train_time, 0.8f), "and -20% training time");
	check(near1(s.tech_mods(1).tmpl[spear].cost[RES_FOOD], 1.0f), "the enemy's soldiers keep theirs");
	{ // Trainer.js keeps the per-unit cost unrounded and floors count * cost: 50 * 0.85 -> 42, 85 for two.
		float c[RES_COUNT], r[RES_COUNT];
		const UnitTypeInfo &si = civ_template_unit_info(spear, civ_unit_template(spear).unit_type);
		s.unit_train_cost(0, spear, si, c, false);
		s.unit_train_cost(0, spear, si, r);
		check(near1(c[RES_FOOD], 42.5f) && near1(r[RES_FOOD], 43.0f), "Indibil: 42.5 food a spearman, 43 in the tooltip");
		const auto civ0 = s.player(0).civ;
		s.player(0).civ = CIV_IBERIANS;
		s.set_player_resources(0, 1000, 1000, 1000, 1000);
		s.player(0).phase = 2; // the Iberian spearman is a Town phase unit in 0.28
		const uint32_t ib = s.spawn_building_world(-40, -40, BLD_BARRACKS, 0, 1.0f);
		const float f0 = s.player(0).food();
		check(s.cmd_train_template(0, ib, spear, 1) && near1(f0 - s.player(0).food(), 42.0f), "a batch of one pays floor(42.5) = 42");
		check(s.cmd_train_template(0, ib, spear, 2) && near1(f0 - s.player(0).food(), 42.0f + 85.0f), "a batch of two pays floor(85) = 85");
		s.player(0).civ = civ0;
		s.player(0).phase = 1;
	}
	// Pericles' Periclean Building: technologies -10% cost and half the research time;
	// Themistocles' Long Walls: walls half the stone. A garrisoned hero still projects them.
	const uint32_t per = s.spawn_unit(-20, -16, 0, UNIT_HERO, 0.0f, roster_named("athen/hero_pericles"));
	const uint32_t the = s.spawn_unit(-20, -12, 0, UNIT_HERO, 0.0f, roster_named("athen/hero_themistocles"));
	const uint32_t bar = s.spawn_building_world(-30, -30, BLD_BARRACKS, 0, 1.0f);
	s.garrison_now((size_t)idx_of(s, per), *bld_mut(s, bar));
	for (int t = 0; t < 22; ++t) s.tick(Simulation::FIXED_STEP); // a global-aura pulse
	check(near1(s.tech_mods(0).bld_tech_cost[BLD_BARRACKS][RES_FOOD], 0.9f) && near1(s.tech_mods(0).bld_research_time[BLD_BARRACKS], 0.5f),
			"Pericles, garrisoned: technologies -10% cost, research time halved");
	check(near1(s.tech_mods(0).bld_cost[BLD_WALL_SEGMENT][RES_STONE], 0.5f), "Themistocles: walls half the stone");
	check(near1(s.tech_mods(0).bld_cost[BLD_PALISADE][RES_WOOD], 0.5f) && near1(s.tech_mods(0).bld_build_time[BLD_PALISADE], 0.8f),
			"and palisades half the wood, built 20% faster (affects Wall or Palisade)");
	check(s.aura_mgr_.has(AURA_SPEED), "his own ships' speed aura stays a range effect");
	// A dead hero's auras end with him.
	s.units_.hp[idx_of(s, ind)] = 0.0f;
	s.units_.alive[idx_of(s, ind)] = 0;
	for (int t = 0; t < 22; ++t) s.tick(Simulation::FIXED_STEP); // a global-aura pulse
	check(near1(s.tech_mods(0).tmpl[spear].cost[RES_FOOD], 1.0f), "Indibil dead: soldiers at full cost again");
	(void)the;
}

static void test_aura_secondary_effects() {
	std::printf("56. multi-effect hero auras: every AuraManager kind of one 0 A.D. aura applies\n");
	Simulation s;
	setup(s);
	auto near1 = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
	auto mod = [&](float x, float z, uint8_t team, AuraType t, uint16_t tmpl) {
		return s.aura_mgr_.query_modifier(x, z, team, t, tmpl);
	};
	// Brasidas' Helot Reforms: Citizen Infantry Javelineers +1 resistance and +25% ranged damage.
	const uint16_t jav = roster_named("spart/infantry_javelineer_b");
	s.spawn_unit(-20, -20, 0, UNIT_HERO, 0.0f, roster_named("spart/hero_brasidas"));
	// Boudicca's Champion Army: champions +20% attack and +10% movement speed.
	const uint16_t champ = roster_named("brit/champion_infantry_swordsman");
	s.spawn_unit(20, 20, 0, UNIT_HERO, 0.0f, roster_named("brit/hero_boudicca"));
	run(s, 1.0f);
	check(near1(mod(-18, -20, 0, AURA_ARMOR, jav), 1.0f), "Brasidas: javelineers +1 resistance");
	check(near1(mod(-18, -20, 0, AURA_ATTACK, jav), 1.25f), "and +25% ranged damage, beside it");
	check(mod(-18, -20, 0, AURA_ATTACK, roster_named("brit/infantry_spearman_b")) == 0.0f, "a spearman gets neither");
	check(near1(mod(22, 20, 0, AURA_ATTACK, champ), 1.2f), "Boudicca: champions +20% attack");
	check(near1(mod(22, 20, 0, AURA_SPEED, champ), 1.1f), "and +10% speed, beside it");
	check(mod(22, 20, 1, AURA_SPEED, champ) == 0.0f, "an enemy champion keeps its speed");
}

static void test_nastasen_warships() {
	std::printf("57. Nastasen's Gift of Warships: Kushite arrow ships -50 wood and metal while he lives\n");
	Simulation s;
	setup(s);
	s.set_team_civ(0, CIV_KUSHITES);
	const uint16_t ship = roster_named("kush/ship_arrow");
	check(ship != CIV_UNIT_TEMPLATE_NONE, "roster has the Kushite arrow ship");
	auto cost = [&](float out[RES_COUNT]) { s.unit_train_cost(0, ship, civ_template_unit_info(ship, UNIT_TRIREME), out); };
	float before[RES_COUNT], after[RES_COUNT];
	cost(before);
	check(before[RES_WOOD] > 50.0f && before[RES_GOLD] > 50.0f, "the ship costs wood and metal");
	const uint32_t nas = s.spawn_unit(-20, -20, 0, UNIT_HERO, 0.0f, roster_named("kush/hero_nastasen"));
	for (int t = 0; t < 22; ++t) s.tick(Simulation::FIXED_STEP); // a global-aura pulse
	cost(after);
	check(after[RES_WOOD] == before[RES_WOOD] - 50.0f && after[RES_GOLD] == before[RES_GOLD] - 50.0f,
			"Nastasen: -50 wood and -50 metal");
	check(after[RES_FOOD] == before[RES_FOOD], "food unchanged");
	s.units_.hp[idx_of(s, nas)] = 0.0f;
	s.units_.alive[idx_of(s, nas)] = 0;
	for (int t = 0; t < 22; ++t) s.tick(Simulation::FIXED_STEP);
	cost(after);
	check(after[RES_WOOD] == before[RES_WOOD], "Nastasen dead: full cost again");
}

static void test_arakamani_temples() {
	std::printf("61. Arakamani's Follower of Apedemak: the Kushite temple and Apedemak guard -30%% cost and time\n");
	auto near1 = [](float a, float b) { return std::fabs(a - b) < 1e-5f; };
	Simulation s;
	setup(s);
	s.set_team_civ(0, CIV_KUSHITES);
	const uint16_t ape = roster_named("kush/champion_infantry_apedemak"), amun = roster_named("kush/champion_infantry_amun");
	check(ape != CIV_UNIT_TEMPLATE_NONE && amun != CIV_UNIT_TEMPLATE_NONE, "roster has both temple guards");
	struct Costs { float temple[RES_COUNT], ape[RES_COUNT], amun[RES_COUNT], temple_time, ape_time, amun_time, amun_temple_time; };
	auto costs = [&]() {
		Costs c{};
		s.structure_cost(0, BLD_TEMPLE, -1.0f, c.temple);
		s.unit_train_cost(0, ape, civ_template_unit_info(ape, UNIT_SWORDSMAN), c.ape);
		s.unit_train_cost(0, amun, civ_template_unit_info(amun, UNIT_SWORDSMAN), c.amun);
		const auto &m = s.tech_mods(0);
		c.temple_time = m.bld_build_time[BLD_TEMPLE];
		c.amun_temple_time = m.bld_build_time[BLD_TEMPLE_AMUN];
		c.ape_time = s.unit_tech_mods(0, UNIT_SWORDSMAN, ape).train_time;
		c.amun_time = s.unit_tech_mods(0, UNIT_SWORDSMAN, amun).train_time;
		return c;
	};
	const Costs before = costs();
	const uint32_t ara = s.spawn_unit(-20, -20, 0, UNIT_HERO, 0.0f, roster_named("kush/hero_arakamani"));
	for (int t = 0; t < 22; ++t) s.tick(Simulation::FIXED_STEP); // a global-aura pulse
	const Costs after = costs();
	bool temple_ok = true, ape_ok = true, amun_ok = true;
	for (int r = 0; r < RES_COUNT; ++r) {
		temple_ok = temple_ok && after.temple[r] == std::round(before.temple[r] * 0.7f);
		ape_ok = ape_ok && after.ape[r] == std::round(before.ape[r] * 0.7f);
		amun_ok = amun_ok && after.amun[r] == before.amun[r];
	}
	check(before.temple[RES_STONE] > 0.0f && temple_ok, "Temple of Apedemak: -30% cost");
	check(near1(after.temple_time, before.temple_time * 0.7f), "and -30% build time");
	check(before.ape[RES_FOOD] > 0.0f && ape_ok, "Apedemak guard: -30% cost");
	check(near1(after.ape_time, before.ape_time * 0.7f), "and -30% training time");
	check(amun_ok && after.amun_time == before.amun_time && after.amun_temple_time == before.amun_temple_time,
			"the Amun guard and the Grand Temple of Amun keep theirs");
	s.units_.hp[idx_of(s, ara)] = 0.0f;
	s.units_.alive[idx_of(s, ara)] = 0;
	for (int t = 0; t < 22; ++t) s.tick(Simulation::FIXED_STEP);
	check(costs().temple[RES_STONE] == before.temple[RES_STONE], "Arakamani dead: full cost again");
}

static void test_gather_rate_auras() {
	std::printf("58. Iron Pelanors: workers near the Spartan catafalque mine metal 15%% faster\n");
	auto rate = [](bool catafalque, ResourceType res) {
		Simulation s;
		setup(s);
		s.set_team_civ(0, CIV_SPARTANS);
		const uint32_t mine = s.spawn_resource_world(0, 0, res, 5000);
		uint32_t w = s.spawn_unit(1.5f, 0, 0, UNIT_FEMALE_CITIZEN);
		if (catafalque) s.spawn_unit(6, 6, 0, UNIT_CATAFALQUE, 0.0f, roster_named("spart/catafalque"));
		s.order_gather(&w, 1, mine);
		run(s, 2.0f);
		return act_of(s, w) == ACTION_GATHER ? s.units_.gather_rate[s.lookup_unit(w)] : 0.0f;
	};
	const float alone = rate(false, RES_GOLD), near = rate(true, RES_GOLD);
	check(alone > 0.0f, "a worker mines metal");
	check(std::fabs(near - alone * 1.15f) < 1e-4f * alone, "beside the catafalque: x1.15");
	check(std::fabs(rate(true, RES_STONE) - rate(false, RES_STONE)) < 1e-6f, "stone mining unchanged");
}

static void test_attack_preference_switch() {
	std::printf("59. after each hit on a target it doesn't prefer, a unit looks again (0.28 Attack.js)\n");
	auto run_case = [](bool forced) {
		Simulation s;
		setup(s);
		const uint32_t house = s.spawn_building_world(0, 0, BLD_HOUSE, 1, 1.0f);
		s.spawn_building_world(0.0f, -36.0f, BLD_TOWN_CENTER, 1, 1.0f); // keeps the house player 1's
		uint32_t w = s.spawn_unit(-6, 0, 0, UNIT_SWORDSMAN);
		if (forced) s.order_attack_building(&w, 1, house);
		const float hp0 = bld(s, house)->hp;
		run_until(s, 20.0f, [&] { return bld(s, house)->hp < hp0; });
		uint32_t e = s.spawn_unit(-6, 9, 1, UNIT_SPEARMAN, 5000.0f);
		s.order_set_stance(&e, 1, STANCE_STAND_GROUND);
		return std::make_pair(bld(s, house)->hp < hp0, run_until(s, 6.0f, [&] { return target_of(s, w) == e; }));
	};
	const auto [hit, switched] = run_case(false);
	check(hit, "a swordsman with nothing else in sight hacks at an enemy house");
	check(switched, "and turns on the soldier who walks up after the next hit");
	const auto [hit2, switched2] = run_case(true);
	check(hit2 && !switched2, "a house it was ordered to attack keeps it busy");
}

static void test_healer_attack_order() {
	std::printf("60. a healer told to attack walks into heal range of the target (0.28 WalkToTargetRange)\n");
	Simulation s;
	setup(s);
	uint32_t h = s.spawn_unit(-20, 0, 0, UNIT_HEALER);
	const uint32_t e = post(s, 10, 0, UNIT_SPEARMAN, 5000.0f);
	s.order_attack(&h, 1, e);
	check(s.unit_order(h).kind == ORDER_WALK && (s.unit_order(h).flags & ORDER_FLAG_FORCE), "it takes a forced walk");
	const float range = s.healer_range_of(s.lookup_unit(h));
	run(s, 20.0f);
	const float d = dist(s, h, 10, 0);
	check(d <= range && d > 0.5f * range, "and stops in heal range, short of the enemy");
	// One already in range stays where it is.
	const float hx = s.units().pos_x[s.lookup_unit(h)];
	s.order_attack(&h, 1, e);
	run(s, 2.0f);
	check(std::fabs(s.units().pos_x[s.lookup_unit(h)] - hx) < 0.5f, "one already in range stays put");
}

static void test_restricted_attack_order() {
	std::printf("63. an attack order on a target its RestrictedClasses forbid is a walk (UnitAI.Attack CanAttack)\n");
	Simulation s;
	setup(s);
	uint32_t ram = s.spawn_unit(-20, 0, 0, UNIT_SIEGE_RAM);
	const uint32_t e = post(s, 10, 0, UNIT_SPEARMAN, 5000.0f);
	check(!s.unit_can_target(s.lookup_unit(ram), s.lookup_unit(e)), "a ram can't attack a soldier (Field Organic)");
	s.order_attack(&ram, 1, e);
	check(s.unit_order(ram).kind == ORDER_WALK && (s.unit_order(ram).flags & ORDER_FLAG_FORCE), "it walks to it instead");
	const float d0 = dist(s, ram, 10, 0);
	run(s, 10.0f);
	check(dist(s, ram, 10, 0) < d0 - 5.0f, "towards the soldier");
	uint32_t dog = s.spawn_unit(-20, 20, 0, UNIT_WAR_DOG);
	const uint32_t house = s.spawn_building_world(10, 20, BLD_HOUSE, 1, 1.0f);
	check(!s.unit_can_target_building(s.lookup_unit(dog), BLD_HOUSE), "a dog can't attack a structure");
	s.order_attack_building(&dog, 1, house, false, false);
	check(s.unit_order(dog).kind == ORDER_WALK, "and walks to the house");
}

static void test_walk_to_target_follows() {
	std::printf("65. a walk-to-target order follows a target that walks off (0.28 WalkToTarget MoveToTargetRange)\n");
	Simulation s;
	setup(s);
	uint32_t h = s.spawn_unit(-20, 0, 0, UNIT_HEALER);
	uint32_t ram = s.spawn_unit(-20, 6, 0, UNIT_SIEGE_RAM);
	const uint32_t e = post(s, 0, 0, UNIT_SPEARMAN, 5000.0f);
	s.order_attack(&h, 1, e);
	s.order_attack(&ram, 1, e);
	s.cmd_move(&e, 1, 0, 30);
	const float range = s.healer_range_of(s.lookup_unit(h));
	run(s, 40.0f);
	check(dist(s, e, 0, 30) < 2.0f, "the soldier walked off");
	const float dh = std::hypot(x_of(s, h) - x_of(s, e), z_of(s, h) - z_of(s, e));
	check(dh <= range + 0.5f, "the healer ends in heal range of where it went");
	check(s.unit_order(h).kind != ORDER_WALK, "and its order is done");
	const float dr = std::hypot(x_of(s, ram) - x_of(s, e), z_of(s, ram) - z_of(s, e));
	check(dr < 4.0f, "the ram ends beside it");
}

static void test_structure_arrows_and_force() {
	std::printf("66. a soldier at work answers structure arrows unless its order is forced (UnitAI Attacked)\n");
	Simulation s;
	setup(s);
	const uint32_t tower = s.spawn_building_world(40, 0, BLD_TOWER, 1, 1.0f);
	s.spawn_building_world(-20, 0, BLD_STOREHOUSE, 0, 1.0f);
	const uint32_t tree = s.spawn_resource_world(0, 0, RES_WOOD, 500);
	uint32_t gatherer = s.spawn_unit(-1.5f, 0, 0, UNIT_SPEARMAN);
	uint32_t walker = s.spawn_unit(0, 10, 0, UNIT_SPEARMAN);
	uint32_t violent = s.spawn_unit(0, -10, 0, UNIT_SPEARMAN);
	s.order_set_stance(&violent, 1, STANCE_VIOLENT);
	s.order_gather(&gatherer, 1, tree);
	check(run_until(s, 20.0f, [&] { return act_of(s, gatherer) == ACTION_GATHER; }), "the soldier starts gathering");
	s.order_walk(&walker, 1, -30.0f, 10.0f);
	s.order_walk(&violent, 1, -30.0f, -10.0f);
	s.tick(DT);
	for (uint32_t u : {gatherer, walker, violent}) s.units_.attacked_by[idx_of(s, u)] = tower;
	s.tick(DT);
	check(s.units_.target_building_id[idx_of(s, gatherer)] == tower, "a gatherer (no longer forced once at work) goes for the tower");
	check(s.units_.target_building_id[idx_of(s, walker)] != tower && s.unit_order(walker).kind == ORDER_WALK,
			"a soldier on a player's walk keeps walking");
	check(s.units_.target_building_id[idx_of(s, violent)] == tower, "a violent one answers even on a forced walk");
}

static void test_can_patrol() {
	std::printf("67. support units and civilian ships walk instead of patrolling (UnitAI CanPatrol)\n");
	Simulation s;
	setup(s);
	uint32_t woman = s.spawn_unit(0, 0, 0, UNIT_FEMALE_CITIZEN);
	uint32_t spartan = s.spawn_unit(0, 4, 0, UNIT_FEMALE_CITIZEN, 0.0f, civ_unit_template_by_id("spart/support_female_citizen"));
	uint32_t trader = s.spawn_unit(0, 8, 0, UNIT_TRADER);
	uint32_t soldier = s.spawn_unit(0, -4, 0, UNIT_SPEARMAN);
	for (uint32_t u : {woman, spartan, trader, soldier}) s.order_patrol(&u, 1, 20, 0);
	check(s.unit_order(woman).kind == ORDER_WALK, "a woman walks to the point");
	check(s.unit_order(trader).kind == ORDER_WALK, "so does a trader");
	check(s.unit_order(spartan).kind == ORDER_PATROL, "a Spartan woman patrols");
	check(s.unit_order(soldier).kind == ORDER_PATROL, "a soldier patrols");
	check(run_until(s, 20.0f, [&] { return dist(s, woman, 20, 0) < 2.0f; }), "the woman gets there");
	run(s, 10.0f);
	check(dist(s, woman, 20, 0) < 2.0f, "and stays, not walking back");
}

static void test_unarmed_aggressive_stays() {
	std::printf("68. an aggressive unit with no attack ignores attackers; only passive ones flee (UnitAI respondFlee)\n");
	Simulation s;
	setup(s);
	uint32_t hero = s.spawn_unit(0, 0, 0, UNIT_HERO, 0.0f, civ_unit_template_by_id("athen/hero_hippocrates"));
	uint32_t healer = s.spawn_unit(-40, 40, 0, UNIT_HEALER); // out of the hero's sight, so it isn't healed
	check(s.units_.stance[idx_of(s, hero)] == STANCE_AGGRESSIVE && s.units_.attack_damage[idx_of(s, hero)] <= 0.0f,
			"Hippocrates is aggressive with no attack");
	uint32_t a = s.spawn_unit(1.5f, 0, 1, UNIT_SWORDSMAN);
	uint32_t b = s.spawn_unit(-38.5f, 40, 1, UNIT_SWORDSMAN);
	s.order_attack(&a, 1, hero);
	s.order_attack(&b, 1, healer);
	bool fled = false;
	const bool hit = run_until(s, 10.0f, [&] {
		fled = fled || act_of(s, hero) == ACTION_FLEE;
		return hp_of(s, hero) < s.units_.max_hp[idx_of(s, hero)];
	});
	run(s, 2.0f);
	fled = fled || act_of(s, hero) == ACTION_FLEE;
	check(hit, "the hero is hit");
	check(!fled && dist(s, hero, 0, 0) < 1.5f, "and stays where it is");
	check(dist(s, healer, -40, 40) > 3.0f, "a passive healer runs off");
}

static void test_passive_mid_fight() {
	std::printf("69. set passive mid-fight, a unit hits its target while in reach but never chases it (UnitAI SwitchToStance)\n");
	Simulation s;
	setup(s);
	uint32_t a = s.spawn_unit(0, 0, 0, UNIT_SPEARMAN);
	uint32_t e = post(s, 1.5f, 0, UNIT_SPEARMAN, 5000.0f);
	check(run_until(s, 10.0f, [&] { return target_of(s, a) == e && hp_of(s, e) < 5000.0f; }), "the spearman takes on the enemy by itself");
	s.order_set_stance(&a, 1, STANCE_PASSIVE);
	const float hp0 = hp_of(s, e);
	run(s, 4.0f);
	check(hp_of(s, e) < hp0, "it keeps hitting while the enemy is in reach");
	const float ax = x_of(s, a), az = z_of(s, a);
	s.units_.pos_x[idx_of(s, e)] = 10.0f;
	run(s, 1.0f);
	const float hp1 = hp_of(s, e);
	run(s, 5.0f);
	check(target_of(s, a) == 0 && act_of(s, a) != ACTION_ATTACK, "once out of reach the fight is over");
	check(std::hypot(x_of(s, a) - ax, z_of(s, a) - az) < 1.5f && hp_of(s, e) == hp1, "without following it");
}

static void test_stance_held_position() {
	std::printf("70. a stance change holds where the unit stands, skips garrisoned units and unknown stances; defensive units keep it while hitting structures (SwitchToStance)\n");
	Simulation s;
	setup(s);
	uint32_t d = s.spawn_unit(0, 0, 0, UNIT_SPEARMAN);
	s.order_walk(&d, 1, -10, 0);
	check(run_until(s, 20.0f, [&] { return dist(s, d, -10, 0) < 1.0f && act_of(s, d) == ACTION_IDLE; }), "walks to its post");
	s.units_.anchor_x[idx_of(s, d)] = 30.0f; // somewhere else
	s.order_set_stance(&d, 1, STANCE_DEFENSIVE);
	check(std::hypot(s.units_.anchor_x[idx_of(s, d)] - x_of(s, d), s.units_.anchor_z[idx_of(s, d)] - z_of(s, d)) < 1e-3f,
			"the held position is where it stands");
	s.order_set_stance(&d, 1, 9);
	check(s.units_.stance[idx_of(s, d)] == STANCE_DEFENSIVE, "an unknown stance changes nothing");
	const uint32_t bk = s.spawn_building_world(-40.0f, -30.0f, BLD_BARRACKS, 0, 1.0f);
	uint32_t g = s.spawn_unit(-40.0f, -22.0f, 0, UNIT_SPEARMAN);
	s.order_garrison(&g, 1, bk);
	check(run_until(s, 15.0f, [&] { const auto *gg = s.get_garrisoned_units(bk); return gg && gg->size() == 1; }),
			"a spearman garrisons");
	s.order_set_stance(&g, 1, STANCE_STAND_GROUND);
	check(s.units_.stance[idx_of(s, g)] != STANCE_STAND_GROUND, "a garrisoned one keeps its stance");
	// A defensive unit away from its post that picks an enemy structure keeps its post.
	const float hx = s.units_.anchor_x[idx_of(s, d)], hz = s.units_.anchor_z[idx_of(s, d)];
	s.units_.pos_x[idx_of(s, d)] = -6.0f;
	s.spawn_building_world(-2.0f, 0.0f, BLD_HOUSE, 1, 1.0f);
	check(run_until(s, 5.0f, [&] { return s.units_.target_building_id[idx_of(s, d)] != 0; }), "it goes for an enemy house");
	check(s.units_.anchor_x[idx_of(s, d)] == hx && s.units_.anchor_z[idx_of(s, d)] == hz, "its held position stays");
}

static void test_defensive_walks_back() {
	std::printf("71. a defensive unit walks back to its held position after a player's attack, an aggressive one stays (COMBAT.FINDINGNEWTARGET WalkToHeldPosition)\n");
	auto run_case = [](uint8_t stance, bool structure) {
		Simulation s;
		setup(s);
		uint32_t d = s.spawn_unit(0, 0, 0, UNIT_SPEARMAN);
		s.order_set_stance(&d, 1, stance); // held at (0, 0)
		if (structure) {
			const uint32_t h = s.spawn_building_world(16.0f, 0.0f, BLD_HOUSE, 1, 1.0f);
			for (Simulation::Building &b : s.buildings_)
				if (b.id == h) b.hp = 1.0f;
			s.order_attack_building(&d, 1, h, false);
			check(run_until(s, 30.0f, [&] { const auto *b = bld(s, h); return !b || b->wrecked || b->hp <= 0.0f; }),
					"the spearman razes the house");
		} else {
			const uint32_t e = post(s, 16.0f, 0.0f, UNIT_SPEARMAN, 2.0f);
			run_until(s, 0.5f, [] { return false; }); // the player sees it first
			s.order_attack(&d, 1, e);
			check(run_until(s, 30.0f, [&] { return !s.units().alive[idx_of(s, e)]; }), "the spearman kills its target");
		}
		const float far = dist(s, d, 0, 0);
		check(far > 8.0f, "the target was well away from the held position");
		run_until(s, 30.0f, [&] { return dist(s, d, 0, 0) < 1.5f && act_of(s, d) == ACTION_IDLE; });
		return dist(s, d, 0, 0);
	};
	check(run_case(STANCE_DEFENSIVE, false) < 1.5f, "a defensive unit walks back after killing a unit (after its cheer)");
	check(run_case(STANCE_DEFENSIVE, true) < 1.5f, "... and after razing a structure");
	check(run_case(STANCE_AGGRESSIVE, false) > 8.0f, "an aggressive unit stays where the fight ended");
}

static void test_guard_stances() {
	std::printf("72. guards answer their charge's attacker in any stance and keep after it while it can hit the charge (GuardedAttacked, ShouldAbandonChase isGuardOf)\n");
	{
		Simulation s;
		setup(s);
		uint32_t vip = s.spawn_unit(0, 0, 0, UNIT_SPEARMAN, 5000.0f);
		// A passive soldier: a Support guard (a woman) tends an injured charge instead (case 81).
		uint32_t w = s.spawn_unit(-2, 0, 0, UNIT_SPEARMAN);
		s.order_set_stance(&w, 1, STANCE_PASSIVE);
		s.order_guard(&w, 1, vip);
		run(s, 1.0f);
		uint32_t e = s.spawn_unit(4, 0, 1, UNIT_SPEARMAN, 5000.0f);
		run(s, 0.5f);
		s.order_attack(&e, 1, vip);
		check(run_until(s, 15.0f, [&] { return target_of(s, w) == e && act_of(s, w) == ACTION_ATTACK; }),
				"a passive guard fights the unit attacking her charge");
	}
	{
		Simulation s;
		setup(s);
		uint32_t vip = s.spawn_unit(0, 0, 0, UNIT_SPEARMAN, 5000.0f);
		s.order_set_stance(&vip, 1, STANCE_STAND_GROUND); // it doesn't run the archer down itself
		uint32_t g = s.spawn_unit(-2, 0, 0, UNIT_SPEARMAN);
		s.order_set_stance(&g, 1, STANCE_DEFENSIVE);
		s.order_guard(&g, 1, vip);
		run(s, 1.0f);
		uint32_t a = post(s, 12.0f, 0.0f, UNIT_ARCHER, 5000.0f);
		run(s, 0.5f);
		s.order_attack(&a, 1, vip);
		check(run_until(s, 10.0f, [&] { return hp_of(s, vip) < 5000.0f; }), "an archer shoots the charge");
		check(run_until(s, 5.0f, [&] { return target_of(s, g) == a; }), "the defensive guard answers");
		s.units_.anchor_x[idx_of(s, g)] = -60.0f; // the archer is now far beyond its leash from the held position
		bool kept = true;
		const float hp0 = hp_of(s, a);
		const bool hit = run_until(s, 20.0f, [&] {
			kept = kept && target_of(s, g) == a;
			return hp_of(s, a) < hp0;
		});
		check(hit && kept, "but keeps after it while it can hit the charge");
	}
}

static void test_guard_towers_and_animals() {
	std::printf("73. guards go for a tower shooting their charge and an animal biting it (Guard.js OnAttacked, GuardedAttacked)\n");
	{
		Simulation s;
		setup(s);
		uint32_t vip = s.spawn_unit(0, 0, 0, UNIT_SPEARMAN, 5000.0f);
		s.order_set_stance(&vip, 1, STANCE_STAND_GROUND); // it doesn't go for the tower itself
		uint32_t g = s.spawn_unit(-6, 0, 0, UNIT_SPEARMAN);
		s.order_set_stance(&g, 1, STANCE_DEFENSIVE); // the tower is beyond what it would pick itself
		s.order_guard(&g, 1, vip);
		run(s, 1.0f);
		const uint32_t tower = s.spawn_building_world(14.0f, 0.0f, BLD_SENTRY_TOWER, 1, 1.0f);
		check(run_until(s, 15.0f, [&] { return hp_of(s, vip) < 5000.0f; }), "the tower shoots the charge");
		check(run_until(s, 5.0f, [&] { return s.units_.target_building_id[idx_of(s, g)] == tower; }),
				"its guard goes for the tower");
		s.order_set_stance(&g, 1, STANCE_STAND_GROUND);
		s.order_guard(&g, 1, vip);
		run(s, 3.0f);
		check(s.units_.target_building_id[idx_of(s, g)] == 0, "a guard standing its ground doesn't go for one out of its reach");
	}
	{
		Simulation s;
		setup(s);
		uint32_t vip = s.spawn_unit(0, 0, 0, UNIT_SPEARMAN, 5000.0f);
		s.order_set_stance(&vip, 1, STANCE_STAND_GROUND);
		uint32_t g = s.spawn_unit(-3, 0, 0, UNIT_SPEARMAN);
		s.order_set_stance(&g, 1, STANCE_PASSIVE); // passive: it picks no animal itself
		s.order_guard(&g, 1, vip);
		run(s, 1.0f);
		const uint32_t wolf = s.spawn_fauna(2.5f, 0.0f, FAUNA_WOLF_ARCTIC_VIOLENT);
		bool went = false;
		check(run_until(s, 15.0f, [&] {
			went = went || target_of(s, g) == wolf;
			return hp_of(s, vip) < 5000.0f && went;
		}), "a wolf bites the charge, and its guard goes for the wolf");
	}
}

static void test_structure_scan_by_centre() {
	std::printf("74. an idle soldier picks an enemy structure by its centre, not its nearest edge (SetupAttackRangeQuery)\n");
	auto picks = [](float off) {
		Simulation s;
		setup(s); // player 1's Civic Centre stands at (56, 56)
		const uint32_t probe = s.spawn_unit(-40.0f, 40.0f, 0, UNIT_SPEARMAN);
		const float vision = s.unit_vision_at(idx_of(s, probe));
		const uint32_t d = s.spawn_unit(56.0f - vision - off, 56.0f, 0, UNIT_SPEARMAN);
		s.set_reveal_map(true);
		return run_until(s, 3.0f, [&] { return s.units_.target_building_id[idx_of(s, d)] != 0; });
	};
	check(!picks(1.0f), "a Civic Centre whose edge, not centre, is within vision is left alone");
	check(picks(-1.0f), "one whose centre is within vision is attacked");
}

static void test_healer_stances() {
	std::printf("75. a healer standing its ground or defensive doesn't follow a wounded unit out of its leash (HEALING OutOfRange, WalkToHeldPosition)\n");
	auto run_case = [](uint8_t stance) {
		Simulation s;
		setup(s);
		uint32_t h = s.spawn_unit(0, 0, 0, UNIT_HEALER);
		s.order_set_stance(&h, 1, stance);
		uint32_t w = s.spawn_unit(1.5f, 0, 0, UNIT_SPEARMAN);
		s.units_.hp[idx_of(s, w)] = 10.0f;
		check(run_until(s, 5.0f, [&] { return act_of(s, h) == ACTION_HEAL && hp_of(s, w) > 10.0f; }), "the healer heals a wounded unit next to it");
		s.order_walk(&w, 1, 16.0f, 0.0f);
		float farthest = 0.0f;
		run_until(s, 15.0f, [&] { farthest = std::max(farthest, dist(s, h, 0, 0)); return false; });
		return std::make_pair(farthest, dist(s, h, 0, 0));
	};
	const auto [sg_far, sg_end] = run_case(STANCE_STAND_GROUND);
	check(sg_far < 1.0f, "standing its ground, it stays put");
	const auto [df_far, df_end] = run_case(STANCE_DEFENSIVE);
	check(df_far < 12.0f && df_end < 1.5f, "defensive, it follows only so far and walks back to where it stood");
	const auto [ag_far, ag_end] = run_case(STANCE_AGGRESSIVE);
	check(ag_end > 10.0f, "aggressive, it follows the unit");
}

static void test_forced_flag_not_inherited() {
	std::printf("76. the target a unit picks itself after a player's target dies is an unforced fight: it turns on a melee attacker (AttackVisibleEntity force false)\n");
	Simulation s;
	setup(s);
	uint32_t d = s.spawn_unit(0, 0, 0, UNIT_SPEARMAN, 5000.0f);
	const uint32_t e1 = post(s, 3.0f, 0.0f, UNIT_SPEARMAN, 2.0f);
	const uint32_t e2 = post(s, 10.0f, 6.0f, UNIT_SPEARMAN, 5000.0f);
	run(s, 0.5f);
	s.order_attack(&d, 1, e1);
	check(run_until(s, 15.0f, [&] { return !s.units().alive[idx_of(s, e1)]; }), "the spearman kills its target");
	check(run_until(s, 5.0f, [&] { return target_of(s, d) == e2; }), "and picks the next enemy itself");
	const uint32_t e3 = s.spawn_unit(x_of(s, d) - 1.5f, z_of(s, d), 1, UNIT_SWORDSMAN, 5000.0f);
	s.order_attack(&e3, 1, d);
	check(run_until(s, 10.0f, [&] { return target_of(s, d) == e3; }), "it turns on the swordsman hitting it");
}

static void test_range_check_between_hits() {
	std::printf("64. a target that leaves reach between hits takes no hit (0.28 Attack.js RepeatRangeCheck)\n");
	// Stepped at the game's FIXED_STEP: at DT the pushing between the two jumps far
	// enough in one tick to take a pair standing in reach out of it.
	auto step = [](Simulation &s, float seconds) {
		for (long n = std::lround(seconds / Simulation::FIXED_STEP); n > 0; --n) s.tick(Simulation::FIXED_STEP);
	};
	auto step_until = [](Simulation &s, float seconds, auto &&done) {
		for (long n = std::lround(seconds / Simulation::FIXED_STEP); n > 0; --n) {
			s.tick(Simulation::FIXED_STEP);
			if (done()) return true;
		}
		return false;
	};
	auto run_case = [&](bool leave) {
		Simulation s;
		setup(s);
		uint32_t w = s.spawn_unit(0, 0, 0, UNIT_SWORDSMAN);
		uint32_t e = post(s, 1.5f, 0, UNIT_SPEARMAN, 5000.0f);
		s.order_attack(&w, 1, e);
		// A hit in reach, so the next swing is under way (attack_timer < 0).
		float hp0 = hp_of(s, e);
		const bool hit = step_until(s, 10.0f, [&] {
			const float hp = hp_of(s, e);
			if (hp >= hp0) return false;
			hp0 = hp;
			return s.units_.attack_timer[idx_of(s, w)] < 0.0f;
		});
		if (leave) s.units_.pos_x[idx_of(s, e)] = 8.0f; // out of reach right after the hit
		const float hp1 = hp_of(s, e);
		step(s, 0.8f); // the next hit was due one RepeatTime (0.75 s) on, in the fourth turn
		const bool spared = hp_of(s, e) == hp1;
		const bool again = step_until(s, 10.0f, [&] { return hp_of(s, e) < hp1; });
		return std::make_tuple(hit, spared, again);
	};
	const auto [hit, spared, again] = run_case(true);
	check(hit, "a swordsman hits a soldier in reach");
	check(spared, "the soldier, gone out of reach, takes no hit from the swing under way");
	check(again, "the swordsman chases and hits it again");
	const auto [hit2, spared2, again2] = run_case(false);
	check(hit2 && !spared2, "one that stays in reach takes the next hit");
}

static const Simulation::Scenery *scenery_of(const Simulation &s, uint32_t id) {
	const int32_t k = s.scenery_index(id);
	return k >= 0 ? &s.scenery()[k] : nullptr;
}

// Capturable.js TimerTick counts simulation ticks: step at the game's own FIXED_STEP.
static void run_fixed(Simulation &s, float seconds) {
	for (int i = 0, n = (int)std::lround(seconds / Simulation::FIXED_STEP); i < n; ++i) s.tick(Simulation::FIXED_STEP);
}
template <class F> static bool run_until_fixed(Simulation &s, float seconds, F &&done) {
	for (int i = 0, n = (int)std::lround(seconds / Simulation::FIXED_STEP); i < n; ++i) {
		s.tick(Simulation::FIXED_STEP);
		if (done()) return true;
	}
	return done();
}

static void test_scenery_capture() {
	std::printf("62. Capturable scenery is captured, decays and regenerates (Capturable.js, TerritoryDecay.js)\n");
	const uint8_t fence = scenery_type_by_name("fence_long"), column = scenery_type_by_name("column_doric");
	{
		// Gaia's fence in a player's territory decays to that player.
		Simulation s;
		setup(s);
		const uint32_t f = s.spawn_scenery(-46, -56, fence, 0.0f, 1.0f);
		check(scenery_of(s, f)->owner == GAIA_PLAYER && scenery_of(s, f)->cp[MAX_PLAYERS] == 100.0f, "a fence starts Gaia's, 100 points");
		const bool decayed = run_until_fixed(s, 15.0f, [&] { return scenery_of(s, f)->owner == 0; });
		check(decayed, "in player 0's land it turns player 0's");
	}
	{
		// On neutral land: capturers take it, then it decays back to Gaia.
		Simulation s;
		setup(s);
		const uint32_t f = s.spawn_scenery(0, 0, fence, 0.0f, 1.0f);
		uint32_t one = s.spawn_unit(-3, 0, 1, UNIT_SPEARMAN);
		s.order_attack_scenery(&one, 1, f);
		run_fixed(s, 20.0f);
		check(scenery_of(s, f)->owner == GAIA_PLAYER && scenery_of(s, f)->cp[MAX_PLAYERS] >= 97.4f,
				"one spearman (2.5 a second) can't beat Gaia's regeneration (5)");
		const float hp0 = scenery_of(s, f)->hp;
		check(hp0 == scenery_type_info(fence).max_hp, "and capturing does no damage");
		uint32_t many[6];
		for (int k = 0; k < 6; ++k) many[k] = s.spawn_unit(-3.0f, -3.0f + 1.2f * k, 1, UNIT_SPEARMAN);
		s.order_attack_scenery(many, 6, f);
		const bool taken = run_until_fixed(s, 30.0f, [&] { return scenery_of(s, f)->owner == 1; });
		check(taken, "six take it for player 1");
		run_fixed(s, 2.0f);
		check(act_of(s, many[0]) != ACTION_ATTACK, "and stop once it is theirs");
		const bool back = run_until_fixed(s, 30.0f, [&] { return scenery_of(s, f)->owner == GAIA_PLAYER; });
		check(back, "out of player 1's territory it decays back to Gaia");
	}
	{
		// Ctrl attacks instead; a column is never Capturable.
		Simulation s;
		setup(s);
		const uint32_t f = s.spawn_scenery(0, 0, fence, 0.0f, 1.0f);
		const uint32_t c = s.spawn_scenery(0, 10, column, 0.0f, 1.0f);
		uint32_t a = s.spawn_unit(-3, 0, 1, UNIT_SWORDSMAN), b = s.spawn_unit(-3, 10, 1, UNIT_SWORDSMAN);
		s.order_attack_scenery(&a, 1, f, false);
		s.order_attack_scenery(&b, 1, c);
		run_fixed(s, 6.0f);
		check(scenery_of(s, f) == nullptr || scenery_of(s, f)->hp < scenery_type_info(fence).max_hp, "Ctrl: the fence is hacked at");
		check(scenery_of(s, c) == nullptr || scenery_of(s, c)->hp < scenery_type_info(column).max_hp, "the column is attacked");
	}
	{
		// Save round trip keeps owner and points.
		Simulation s;
		setup(s);
		const uint32_t f = s.spawn_scenery(-46, -56, fence, 0.0f, 1.0f);
		run_fixed(s, 2.0f);
		const auto cp = scenery_of(s, f)->cp;
		const char *path = "/tmp/rts0_test_scenery_capture.sav";
		check(s.save_state(path), "saved");
		Simulation t;
		check(t.load_state(path), "loaded");
		check(scenery_of(t, f) && scenery_of(t, f)->cp == cp && scenery_of(t, f)->owner == scenery_of(s, f)->owner,
				"owner and capture points survive a save");
	}
}

static void test_idle_drift_walk_back() {
	std::printf("77. a defensive unit entering IDLE more than 10 m from its held position walks back; an aggressive one stays (UnitAI IDLE Timer)\n");
	auto run_case = [](uint8_t stance) {
		Simulation s;
		setup(s);
		uint32_t d = s.spawn_unit(0, 0, 0, UNIT_SPEARMAN);
		s.order_set_stance(&d, 1, stance); // held at (0, 0)
		const uint32_t house = s.spawn_building_world(12.0f, 0.0f, BLD_HOUSE, 0, 0.0f);
		s.order_build_or_repair(&d, 1, house);
		check(run_until(s, 120.0f, [&] { const auto *b = bld(s, house); return b && b->build_progress >= 1.0f; }), "the house is built");
		check(dist(s, d, 0, 0) > 5.0f, "the builder stood well away from its held position");
		run(s, 15.0f);
		return dist(s, d, 0, 0);
	};
	check(run_case(STANCE_DEFENSIVE) < 1.5f, "a defensive builder walks back once idle");
	check(run_case(STANCE_AGGRESSIVE) > 5.0f, "an aggressive builder stays by the house");
}

static void test_queue_after_auto_fight() {
	std::printf("78. an order queued during a fight, flight or cheer the unit took up while idle runs once it ends (FinishOrder)\n");
	auto walked = [](Simulation &s, uint32_t u) {
		const auto *q = s.unit_order_queue(u);
		return dist(s, u, -30.0f, 0.0f) < 3.0f && (!q || q->empty());
	};
	{
		Simulation s;
		setup(s);
		uint32_t a = s.spawn_unit(0, 0, 0, UNIT_SWORDSMAN);
		s.spawn_unit(4, 0, 1, UNIT_SPEARMAN, 60.0f);
		run(s, 1.0f);
		check(act_of(s, a) == ACTION_ATTACK, "the idle swordsman fights the spearman");
		s.order_walk(&a, 1, -30, 0, true);
		check(run_until(s, 40.0f, [&] { return walked(s, a); }), "then walks the queued walk");
	}
	{
		Simulation s;
		setup(s);
		uint32_t w = s.spawn_unit(0, 0, 0, UNIT_FEMALE_CITIZEN);
		uint32_t e = s.spawn_unit(2, 0, 1, UNIT_SWORDSMAN);
		s.order_attack(&e, 1, w, false);
		check(run_until(s, 20.0f, [&] { return act_of(s, w) == ACTION_FLEE; }), "the woman flees");
		s.order_walk(&w, 1, -30, 0, true);
		s.cmd_delete(1, &e, 1);
		check(run_until(s, 40.0f, [&] { return walked(s, w); }), "then walks the queued walk");
	}
	{
		Simulation s;
		setup(s);
		uint32_t a = s.spawn_unit(0, 0, 0, UNIT_SWORDSMAN);
		s.spawn_unit(3, 0, 1, UNIT_SPEARMAN, 5.0f);
		check(run_until(s, 20.0f, [&] { return act_of(s, a) == ACTION_CHEER; }), "the swordsman cheers its kill");
		s.order_walk(&a, 1, -30, 0, true);
		check(run_until(s, 40.0f, [&] { return walked(s, a); }), "then walks the queued walk");
	}
}

static void test_healers_on_attack_move() {
	std::printf("79. a healer on attack-move or patrol heals the hurt on its way, then walks on (FindWalkAndFightTargets -> FindNewHealTargets)\n");
	for (int mode = 0; mode < 2; ++mode) {
		Simulation s;
		setup(s);
		uint32_t h = s.spawn_unit(-20, 0, 0, UNIT_HEALER);
		uint32_t w = s.spawn_unit(0, 2, 0, UNIT_SPEARMAN);
		const int wi = idx_of(s, w);
		s.units_.hp[wi] = s.units_.max_hp[wi] * 0.3f;
		const float hp0 = s.units_.hp[wi];
		if (mode == 0) s.order_attack_move(&h, 1, 25, 0, false, false, false);
		else s.order_patrol(&h, 1, 25, 0, false);
		check(run_until(s, 30.0f, [&] { return s.units_.hp[idx_of(s, w)] > hp0 + 1.0f; }),
				mode == 0 ? "the attack-moving healer heals the spearman" : "the patrolling healer heals the spearman");
		if (mode == 0)
			check(run_until(s, 120.0f, [&] { return dist(s, h, 25, 0) < 3.0f; }), "then goes on to the attack-move's end");
	}
}

static void test_alert_allied_shelter() {
	std::printf("80. an alert shelters civilians in a mutual ally's structures too, and its end lets them out (AlertRaiser mutualAllies)\n");
	Simulation s;
	setup(s);
	s.set_player_active(2, true, false);
	s.cmd_set_diplomacy(0, 2, Simulation::DIPLO_ALLY);
	s.cmd_set_diplomacy(2, 0, Simulation::DIPLO_ALLY);
	const uint32_t store = s.spawn_building_world(0, 0, BLD_STOREHOUSE, 0, 1.0f);
	const uint32_t ally_house = s.spawn_building_world(14, 0, BLD_HOUSE, 2, 1.0f);
	uint32_t w = s.spawn_unit(12, 4, 0, UNIT_FEMALE_CITIZEN);
	run(s, 1.0f);
	check(s.cmd_raise_alert(0, store), "the alert is raised");
	check(s.unit_order(w).kind == ORDER_GARRISON && s.unit_order(w).target == ally_house, "the woman runs into the ally's house beside her");
	check(run_until(s, 20.0f, [&] { const auto *g = s.get_garrisoned_units(ally_house); return g && !g->empty(); }), "she is inside");
	check(s.cmd_end_alert(0, store), "end of alert");
	check(alive(s, w), "she comes out of the ally's house");
}

static void test_guards_repair_charge() {
	std::printf("81. guards repair the siege engine they guard; a Support guard tends it when it is attacked (GUARDING Timer, GuardedAttacked)\n");
	for (UnitType ut : {UNIT_SPEARMAN, UNIT_FEMALE_CITIZEN}) {
		Simulation s;
		setup(s);
		uint32_t r = s.spawn_unit(0, 0, 0, UNIT_SIEGE_RAM);
		uint32_t g = s.spawn_unit(3, 0, 0, ut);
		const int ri = idx_of(s, r);
		s.units_.hp[ri] = s.units_.max_hp[ri] * 0.5f;
		const float h0 = s.units_.hp[ri];
		s.order_guard(&g, 1, r, false);
		check(run_until(s, 20.0f, [&] { return s.units_.hp[idx_of(s, r)] > h0 + 1.0f; }),
				ut == UNIT_SPEARMAN ? "a spearman guarding a damaged ram repairs it" : "a woman guarding a damaged ram repairs it");
		check(run_until(s, 120.0f, [&] { return s.unit_order(g).kind == ORDER_GUARD; }), "then guards again");
	}
	{
		Simulation s;
		setup(s);
		uint32_t r = s.spawn_unit(0, 0, 0, UNIT_SIEGE_RAM);
		uint32_t w = s.spawn_unit(3, 0, 0, UNIT_FEMALE_CITIZEN);
		s.order_guard(&w, 1, r, false);
		run(s, 1.0f);
		uint32_t e = s.spawn_unit(-3, 0, 1, UNIT_SWORDSMAN);
		s.order_attack(&e, 1, r, false);
		check(run_until(s, 20.0f, [&] { return s.units_.hp[idx_of(s, r)] < s.units_.max_hp[idx_of(s, r)]; }), "the swordsman hurts the ram");
		bool fought = false, repaired = false;
		run_until(s, 10.0f, [&] {
			fought |= s.units_.target_unit_id[idx_of(s, w)] == e && act_of(s, w) == ACTION_ATTACK;
			repaired |= act_of(s, w) == ACTION_REPAIR;
			return false;
		});
		check(repaired && !fought, "the woman repairs the ram instead of fighting its attacker");
	}
}

static void test_guard_memory(const char *path) {
	std::printf("83. a guard keeps its charge through other orders and goes back to guarding once idle (isGuardOf, IDLE Timer)\n");
	auto guarded = [](Simulation &s, uint32_t g, uint32_t c) { return s.unit_order(g).kind == ORDER_GUARD && s.unit_guard_target(g) == c; };
	{
		Simulation s;
		setup(s);
		uint32_t c = s.spawn_unit(0, 0, 0, UNIT_SWORDSMAN);
		uint32_t g = s.spawn_unit(3, 0, 0, UNIT_SPEARMAN);
		s.order_guard(&g, 1, c, false);
		run(s, 1.0f);
		s.cmd_move(&g, 1, 20.0f, 0.0f);
		check(s.unit_order(g).kind != ORDER_GUARD && s.unit_is_guarding(g), "a walk order keeps the charge (remove-guard still shows)");
		check(run_until(s, 60.0f, [&] { return guarded(s, g, c); }), "after the walk the spearman guards the swordsman again");
		check(run_until(s, 30.0f, [&] { const int gi = idx_of(s, g), ci = idx_of(s, c);
			const float dx = s.units_.pos_x[gi] - s.units_.pos_x[ci], dz = s.units_.pos_z[gi] - s.units_.pos_z[ci];
			return dx * dx + dz * dz < 8.0f * 8.0f; }), "and walks back to it");
		s.cmd_stop(&g, 1);
		check(run_until(s, 5.0f, [&] { return guarded(s, g, c); }), "Stop keeps the charge too");
		s.cmd_move(&g, 1, 20.0f, 0.0f);
		s.order_remove_guard(&g, 1);
		check(!s.unit_is_guarding(g), "remove-guard during the walk ends it");
		run(s, 40.0f);
		check(s.unit_order(g).kind == ORDER_NONE && !s.unit_is_guarding(g), "and the spearman stays idle where it walked");
	}
	{
		Simulation s;
		setup(s);
		uint32_t c = s.spawn_unit(0, 0, 0, UNIT_SWORDSMAN);
		uint32_t c2 = s.spawn_unit(0, 6, 0, UNIT_SWORDSMAN);
		uint32_t g = s.spawn_unit(3, 0, 0, UNIT_SPEARMAN);
		s.order_guard(&g, 1, c, false);
		run(s, 1.0f);
		s.cmd_move(&g, 1, 20.0f, 0.0f);
		s.order_guard(&g, 1, c2, true); // a queued guard of someone else drops the first charge at once
		check(s.unit_guard_target(g) == 0, "a queued guard order for another unit drops the old charge");
		check(run_until(s, 60.0f, [&] { return guarded(s, g, c2); }), "and the queued guard runs after the walk");
		s.cmd_move(&g, 1, 20.0f, 0.0f);
		s.cmd_delete(0, &c2, 1);
		run(s, 40.0f);
		check(s.unit_order(g).kind == ORDER_NONE && !s.unit_is_guarding(g), "a charge that dies meanwhile is not guarded again");
	}
	{
		Simulation s;
		setup(s);
		uint32_t c = s.spawn_unit(0, 0, 0, UNIT_SWORDSMAN);
		uint32_t g = s.spawn_unit(3, 0, 0, UNIT_SPEARMAN);
		s.order_guard(&g, 1, c, false);
		run(s, 1.0f);
		s.cmd_move(&g, 1, 20.0f, 0.0f);
		run(s, 1.0f);
		check(s.save_state(path), "saved during the walk");
		Simulation t;
		check(t.load_state(path), "loaded");
		check(t.unit_is_guarding(g), "the charge survives a save and load");
		check(run_until(t, 60.0f, [&] { return guarded(t, g, c); }), "and the loaded guard goes back to guarding");
	}
}

static void test_liu_bang_wall_regen() {
	std::printf("82. while Liu Bang lives, his owner's walls regenerate 10 hp/s (han_hero_liu_bang_1); palisades and others' walls don't\n");
	Simulation s;
	s.set_team_civ(0, CIV_HAN);
	s.set_team_civ(1, CIV_HAN);
	setup(s);
	const uint32_t wall = s.spawn_building_world(-20.0f, 20.0f, BLD_WALL_SEGMENT, 0, 1.0f);
	const uint32_t pal = s.spawn_building_world(-20.0f, 36.0f, BLD_PALISADE, 0, 1.0f);
	const uint32_t theirs = s.spawn_building_world(20.0f, -20.0f, BLD_WALL_SEGMENT, 1, 1.0f);
	auto wound = [&](uint32_t id) { bld_mut(s, id)->hp = bld_mut(s, id)->max_hp * 0.25f; return bld_mut(s, id)->hp; };
	const float w0 = wound(wall), p0 = wound(pal), t0 = wound(theirs);
	run(s, 5.0f);
	check(bld_mut(s, wall)->hp == w0, "no Liu Bang: the wall stays damaged");
	const uint32_t hero = s.spawn_unit(-40.0f, -40.0f, 0, UNIT_HERO, 0.0f, roster_named("han/hero_liu_bang"));
	run(s, 10.0f);
	const float w1 = bld_mut(s, wall)->hp;
	std::printf("  wall %.1f -> %.1f in 10 s\n", w0, w1);
	check(std::fabs(w1 - (w0 + 100.0f)) < 1.0f, "with Liu Bang: +10 hp/s");
	check(bld_mut(s, pal)->hp == p0 && bld_mut(s, theirs)->hp == t0, "not palisades, not the enemy's walls");
	s.cmd_delete(0, &hero, 1);
	run(s, 3.0f);
	const float w2 = bld_mut(s, wall)->hp;
	run(s, 5.0f);
	check(bld_mut(s, wall)->hp == w2, "after he dies the wall stops regenerating");
}

static void test_passive_siege_flees() {
	std::printf("84. a passive ram flees when hit (Order.Flee needs only AbleToMove); an aggressive one doesn't\n");
	for (uint8_t stance : {STANCE_PASSIVE, STANCE_AGGRESSIVE}) {
		Simulation s;
		setup(s);
		uint32_t ram = s.spawn_unit(0, 0, 0, UNIT_SIEGE_RAM);
		s.order_set_stance(&ram, 1, stance);
		uint32_t e = s.spawn_unit(2, 0, 1, UNIT_SPEARMAN);
		s.order_attack(&e, 1, ram, false);
		const bool fled = run_until(s, 15.0f, [&] { return act_of(s, ram) == ACTION_FLEE; });
		check(stance == STANCE_PASSIVE ? fled : !fled, stance == STANCE_PASSIVE ? "the passive ram flees" : "the aggressive ram stays");
	}
}

static void test_unload_self_garrison_rally() {
	std::printf("85. units unloaded from a structure whose rally garrisons back in it run none of its rally commands (OrderToRallyPoint)\n");
	Simulation s;
	setup(s);
	const uint32_t tower = s.spawn_building_world(10.0f, 10.0f, BLD_TOWER, 0, 1.0f);
	s.cmd_set_rally_point(0, tower, 10.0f, 10.0f, 0, tower);
	s.cmd_set_rally_point(0, tower, 30.0f, 10.0f, 0, 0, true);
	uint32_t u = s.spawn_unit(13.0f, 10.0f, 0, UNIT_SPEARMAN);
	s.order_garrison(&u, 1, tower);
	check(run_until(s, 10.0f, [&] { return act_of(s, u) == ACTION_GARRISONED; }), "the spearman garrisons");
	s.order_ungarrison(tower);
	run(s, 8.0f);
	const int i = idx_of(s, u);
	check(i >= 0 && s.units_.pos_x[i] < 20.0f, "after unloading it stays by the tower instead of walking to the Shift-added point");
}

static void test_allied_builders_continue() {
	std::printf("86. an ally's builders move on to a foundation of their own when your structure finishes (Foundation.GetBuilders)\n");
	Simulation s;
	setup(s);
	s.set_diplomacy(0, 1, Simulation::DIPLO_ALLY);
	s.set_diplomacy(1, 0, Simulation::DIPLO_ALLY);
	const uint32_t house = s.spawn_building_world(0.0f, 0.0f, BLD_HOUSE, 0, 0.9f);
	const uint32_t theirs = s.spawn_building_world(8.0f, 0.0f, BLD_HOUSE, 1, 0.0f);
	s.spawn_building_world(-8.0f, 0.0f, BLD_HOUSE, 0, 0.0f); // player 0's: not for the ally
	uint32_t a = s.spawn_unit(3.0f, 2.0f, 1, UNIT_FEMALE_CITIZEN);
	s.order_build_or_repair(&a, 1, house);
	const bool done = run_until(s, 30.0f, [&] { return bld(s, house)->build_progress >= 1.0f; });
	run(s, 0.5f);
	check(done && s.unit_order(a).kind == ORDER_BUILD && s.unit_order(a).target == theirs,
			"the allied builder goes on to its own player's foundation nearby");
}

static void test_work_order_queue() {
	std::printf("87. Back to Work restores the work queue from its first work order (UpdateWorkOrders)\n");
	Simulation s;
	setup(s);
	const uint32_t tree = s.spawn_resource_world(5, 0, RES_WOOD, 500);
	const uint32_t house = s.spawn_building_world(12.0f, 8.0f, BLD_HOUSE, 0, 0.0f);
	uint32_t w = s.spawn_unit(3.5f, 0, 0, UNIT_FEMALE_CITIZEN);
	s.order_gather(&w, 1, tree);
	s.order_build_or_repair(&w, 1, house, true); // Shift: build the house after
	run(s, 2.0f);
	s.order_walk(&w, 1, -20, 0);
	s.cmd_stop(&w, 1); // a Stop after that keeps the work orders the walk kept
	run(s, 1.0f);
	check(s.unit_has_work_order(w), "the work orders are kept");
	{
		const char *path = "/tmp/rts0_test_work_orders.sav";
		check(s.save_state(path), "saved");
		Simulation t;
		check(t.load_state(path), "loaded");
		check(t.work_orders_.count(w) && t.work_orders_.at(w).size() == 2, "the work queue survives a save");
	}
	s.order_back_to_work(&w, 1);
	const std::vector<UnitOrder> *q = s.unit_order_queue(w);
	check(s.unit_order(w).kind == ORDER_GATHER && s.unit_order(w).target == tree, "back to the tree");
	check(q && q->size() == 1 && (*q)[0].kind == ORDER_BUILD && (*q)[0].target == house, "with the house queued behind it");

	// A trader sheltered in a house goes back to trading.
	const uint32_t ma = s.spawn_building_world(-40, 20, BLD_MARKET, 0, 1.0f);
	const uint32_t mb = s.spawn_building_world(40, 20, BLD_MARKET, 0, 1.0f);
	const uint32_t shelter = s.spawn_building_world(-30, 30, BLD_HOUSE, 0, 1.0f);
	uint32_t tr = s.spawn_unit(-38, 28, 0, UNIT_TRADER);
	s.order_trade(&tr, 1, ma, mb, false);
	run(s, 2.0f);
	s.order_garrison(&tr, 1, shelter);
	run_until(s, 10.0f, [&] { return act_of(s, tr) == ACTION_GARRISONED; });
	s.order_ungarrison(shelter);
	run(s, 1.0f);
	s.order_back_to_work(&tr, 1);
	check(s.unit_order(tr).kind == ORDER_TRADE && s.unit_order(tr).target == ma, "the trader resumes its route");
}

static void test_range_aura_not_self() {
	std::printf("89. a range aura buffs the troops around its source but not the source itself (RangeManager queries leave out their own entity)\n");
	Simulation s;
	s.set_team_civ(0, CIV_SPARTANS);
	setup(s);
	const uint32_t leo = s.spawn_unit(-20.0f, -20.0f, 0, UNIT_HERO, 0.0f, roster_named("spart/hero_leonidas"));
	const uint32_t hop = s.spawn_unit(-21.0f, -20.0f, 0, UNIT_SPEARMAN, 0.0f, roster_named("spart/infantry_spearman_b"));
	run(s, 1.0f);
	auto ratio = [&](uint32_t id) {
		const size_t i = (size_t)idx_of(s, id);
		float base[4], eff[4];
		s.unit_tech_damage(i, base);
		s.unit_attack_effects(i, eff);
		return base[0] > 0.0f ? eff[0] / base[0] : 0.0f;
	};
	const float rh = ratio(hop), rl = ratio(leo);
	std::printf("  hack x%.2f on the spearman, x%.2f on Leonidas\n", rh, rl);
	check(std::fabs(rh - 1.25f) < 1e-4f, "Last Stand: a nearby spearman +25% melee attack");
	check(std::fabs(rl - 1.0f) < 1e-4f, "but not Leonidas himself");
}

static void test_rally_commands() {
	std::printf("88. rally commands a target alone doesn't give (RallyPointCommands.js)\n");
	Simulation s;
	setup(s);
	s.set_diplomacy(0, 2, Simulation::DIPLO_ALLY);
	s.set_diplomacy(2, 0, Simulation::DIPLO_ALLY);
	const uint32_t brk = s.spawn_building_world(0.0f, 0.0f, BLD_BARRACKS, 0, 1.0f);
	auto bref = [&]() -> const Simulation::Building & { return *bld(s, brk); };
	auto trained = [&](UnitType t) {
		const uint32_t u = s.spawn_unit(4.0f, 4.0f, 0, t);
		s.apply_rally_point(u, bref());
		return u;
	};
	s.cmd_set_rally_point(0, brk, 20.0f, 0.0f, 0, 0, false, RALLY_CMD_ATTACK_WALK);
	{
		// RallyPointCommands.js attack-walk sends no allowCapture (UnitAI.DEFAULT_CAPTURE).
		const UnitOrder o = s.unit_order(trained(UNIT_SPEARMAN));
		check(o.kind == ORDER_WALK_AND_FIGHT, "attack-walk (session.attackmove)");
		check(!(o.flags & ORDER_FLAG_ALLOW_CAPTURE), "an attack-walk rally damages structures, it doesn't capture them");
	}
	{
		// unit_actions.js "attack" on an enemy structure with hitpoints: allowCapture stays off.
		const uint32_t ebrk = s.spawn_building_world(40.0f, 20.0f, BLD_BARRACKS, 1, 1.0f);
		s.cmd_set_rally_point(0, brk, 40.0f, 20.0f, 0, ebrk, false);
		const UnitOrder o = s.unit_order(trained(UNIT_SPEARMAN));
		check(o.kind == ORDER_ATTACK_BUILDING && o.target == ebrk && !(o.flags & ORDER_FLAG_ALLOW_CAPTURE),
				"a rally on an enemy barracks attacks it with weapons");
		s.cmd_set_rally_point(0, brk, 20.0f, 0.0f, 0, 0, false);
	}
	s.cmd_set_rally_point(0, brk, 20.0f, 0.0f, 0, 0, false, RALLY_CMD_ATTACK_WALK_UNITS);
	{
		const UnitOrder o = s.unit_order(trained(UNIT_SPEARMAN));
		check(o.kind == ORDER_WALK_AND_FIGHT && (o.flags & ORDER_FLAG_UNITS_ONLY), "attack-walk on units only (session.attackmoveUnit)");
	}
	uint32_t foe = s.spawn_unit(22.0f, 0.0f, 1, UNIT_SPEARMAN);
	s.cmd_set_rally_point(0, brk, 22.0f, 0.0f, 0, 0, false, RALLY_CMD_ATTACK, foe);
	{
		const UnitOrder o = s.unit_order(trained(UNIT_SPEARMAN));
		check(o.kind == ORDER_ATTACK && o.target == foe, "attack an enemy unit");
	}
	s.cmd_delete(1, &foe, 1);
	check(s.unit_order(trained(UNIT_SPEARMAN)).kind == ORDER_WALK_AND_FIGHT, "attack-walk there once it is gone");
	const uint32_t treasure = s.spawn_treasure(-15.0f, 5.0f, 0);
	s.cmd_set_rally_point(0, brk, -15.0f, 5.0f, 0, 0, false, RALLY_CMD_TREASURE, treasure);
	{
		const UnitOrder o = s.unit_order(trained(UNIT_FEMALE_CITIZEN));
		check(o.kind == ORDER_COLLECT_TREASURE && o.target == treasure, "collect a treasure");
	}
	const uint32_t ally_house = s.spawn_building_world(-10.0f, -12.0f, BLD_HOUSE, 2, 1.0f);
	s.cmd_set_rally_point(0, brk, -10.0f, -12.0f, 0, ally_house);
	{
		const UnitOrder o = s.unit_order(trained(UNIT_FEMALE_CITIZEN)); // a house takes support units
		check(o.kind == ORDER_GARRISON && o.target == ally_house, "garrison in a mutual ally's structure");
	}
	const uint32_t ally_found = s.spawn_building_world(12.0f, -12.0f, BLD_HOUSE, 2, 0.0f);
	s.cmd_set_rally_point(0, brk, 12.0f, -12.0f, 0, ally_found);
	{
		const UnitOrder o = s.unit_order(trained(UNIT_FEMALE_CITIZEN));
		check(o.kind == ORDER_BUILD && o.target == ally_found, "repair an ally's foundation");
	}
	const uint32_t ma = s.spawn_building_world(-40, 20, BLD_MARKET, 0, 1.0f);
	const uint32_t mb = s.spawn_building_world(40, 20, BLD_MARKET, 0, 1.0f);
	s.cmd_set_rally_point(0, ma, 40.0f, 20.0f, 0, mb, false, RALLY_CMD_TRADE);
	{
		const uint32_t tr = s.spawn_unit(-36.0f, 24.0f, 0, UNIT_TRADER);
		s.apply_rally_point(tr, *bld(s, ma));
		const UnitOrder o = s.unit_order(tr);
		check(o.kind == ORDER_TRADE && o.target == ma, "a trader trades from the market that trained it to the rally market");
		const uint32_t w = s.spawn_unit(-36.0f, 26.0f, 0, UNIT_FEMALE_CITIZEN);
		s.apply_rally_point(w, *bld(s, ma));
		check(s.unit_order(w).kind != ORDER_TRADE, "anyone else doesn't trade");
	}
	// Shift-added steps keep their commands, through a save.
	s.cmd_set_rally_point(0, brk, 20.0f, 0.0f, 0, 0, false, RALLY_CMD_ATTACK_WALK);
	s.cmd_set_rally_point(0, brk, -15.0f, 5.0f, 0, 0, true, RALLY_CMD_TREASURE, treasure);
	const char *path = "/tmp/rts0_test_rally_commands.sav";
	check(s.save_state(path), "saved");
	Simulation t;
	check(t.load_state(path), "loaded");
	const Simulation::Building *tb = bld(t, brk);
	check(tb && tb->rally_cmd == RALLY_CMD_ATTACK_WALK && tb->rally_more.size() == 1 &&
			tb->rally_more[0].cmd == RALLY_CMD_TREASURE && tb->rally_more[0].ent_id == treasure, "the commands survive a save");
	const uint32_t u = t.spawn_unit(4.0f, 4.0f, 0, UNIT_FEMALE_CITIZEN);
	t.apply_rally_point(u, *tb);
	const std::vector<UnitOrder> *q = t.unit_order_queue(u);
	check(t.unit_order(u).kind == ORDER_WALK_AND_FIGHT && q && q->size() == 1 && (*q)[0].kind == ORDER_COLLECT_TREASURE,
			"attack-walk there, then the treasure");
}

// UnitAI CollectTreasure and ReturnResource are forced orders (Q2): the walker ignores
// enemies it sees and attacks on the way; UnitAI's own drop-off isn't forced.
static void test_forced_treasure_and_return() {
	std::printf("90. collecting a treasure and a player's return walk are forced (UnitAI.js)\n");
	{
		Simulation s;
		setup(s);
		uint32_t cav = s.spawn_unit(0.0f, 0.0f, 0, UNIT_CAVALRY);
		const uint32_t treasure = s.spawn_treasure(40.0f, 0.0f, 0);
		uint32_t foe = s.spawn_unit(20.0f, 3.0f, 1, UNIT_SPEARMAN, 1.0e6f);
		s.order_set_stance(&foe, 1, STANCE_PASSIVE);
		s.order_collect_treasure(&cav, 1, treasure);
		check((s.unit_order(cav).flags & ORDER_FLAG_FORCE) != 0, "the treasure order is forced");
		bool fought = false;
		for (int k = 0; k < 300 && s.unit_order(cav).kind == ORDER_COLLECT_TREASURE; ++k) {
			s.tick(Simulation::FIXED_STEP);
			fought |= act_of(s, cav) == ACTION_ATTACK;
		}
		check(!fought, "the treasure hunter rides past the enemy beside its path");
	}
	{
		Simulation s;
		setup(s);
		const uint32_t house = s.spawn_building_world(-30.0f, 0.0f, BLD_STOREHOUSE, 0, 1.0f);
		uint32_t w = s.spawn_unit(10.0f, 0.0f, 0, UNIT_SPEARMAN);
		const int wi = idx_of(s, w);
		s.units_.carry[wi] = 10;
		s.units_.res_type_carried[wi] = 1; // wood
		s.order_return_resource(&w, 1, house);
		check((s.unit_order(w).flags & ORDER_FLAG_FORCE) != 0, "a player's return order is forced");
		uint32_t foe = s.spawn_unit(12.0f, 0.0f, 1, UNIT_SPEARMAN, 1.0e6f);
		s.units_.attacked_by[wi] = foe;
		s.tick(Simulation::FIXED_STEP);
		check(s.unit_order(w).kind == ORDER_RETURN_RESOURCE && act_of(s, w) != ACTION_ATTACK,
				"attacked on the way, it keeps walking to the dropsite");
		s.order_return_resource(&w, 1, house, false, false);
		check(!(s.unit_order(w).flags & ORDER_FLAG_FORCE), "UnitAI's own drop-off isn't forced");
	}
}

// UnitAI CheckTargetVisible (UnitAI.js:4922-4941): a player's command on an entity it can't see
// and has no mirage of is refused for every peer, whatever the sender's interface let through:
// an enemy unit in the fog, a structure never seen, a supply in unexplored land. Once in sight
// (the structure: once seen) the same command goes through.
static void test_hidden_targets() {
	std::printf("hidden targets\n");
	Simulation s;
	setup(s);
	const uint32_t man = s.spawn_unit(-40, -40, 0, UNIT_SPEARMAN);
	const uint32_t foe = s.spawn_unit(20, -40, 1, UNIT_SPEARMAN);
	const uint32_t tree = s.spawn_resource_world(40, -20, RES_WOOD, 200);
	const uint32_t woman = s.spawn_unit(-40, -36, 0, UNIT_FEMALE_CITIZEN);
	uint32_t foe_cc = 0;
	for (const auto &b : s.buildings()) if (b.team == 1) foe_cc = b.id;
	TurnManager tm;
	auto turns = [&](float seconds) {
		int n = (int)std::lround(seconds / TurnManager::TURN_DURATION);
		for (int i = 0; i < n; ++i) tm.tick(TurnManager::TURN_DURATION, s);
	};
	turns(0.4f);
	check(!s.player_can_target_unit(0, foe) && !s.player_can_target_building(0, foe_cc) &&
			!s.player_can_target_resource(0, tree), "the foe, its civic centre and the tree are hidden from player 0");
	auto post = [&](uint8_t cmd, uint32_t who, uint32_t target) {
		NetCommand c;
		c.client_id = 0;
		c.entity_ids = {who};
		c.cmd_type = cmd;
		c.target_id = target;
		c.param = 0;
		tm.post_local_command(c);
	};
	post(3, man, foe);
	post(4, woman, tree);
	turns(0.4f);
	check(s.unit_order(man).kind != ORDER_ATTACK && s.unit_order(woman).kind != ORDER_GATHER,
			"an attack on a unit in the fog and a gather in the shroud are refused");
	post(15, man, foe_cc);
	turns(0.4f);
	check(s.unit_order(man).kind != ORDER_ATTACK_BUILDING, "an attack on a structure never seen is refused");
	// In sight: the foe walks up.
	s.units_.pos_x[idx_of(s, foe)] = -36.0f;
	s.units_.pos_z[idx_of(s, foe)] = -40.0f;
	turns(0.4f);
	check(s.player_can_target_unit(0, foe), "the foe in sight can be targeted");
	post(3, man, foe);
	turns(0.4f);
	check(s.unit_order(man).kind == ORDER_ATTACK, "and the attack goes through");
	// Own and allied entities are never refused.
	check(s.player_can_target_building(1, foe_cc) && s.player_can_target_unit(1, foe), "a player's own are always targetable");
	// template_unit_fauna.xml RetainInFog: an animal in unexplored land is hidden, but once its
	// land is explored it stays a target in the fog.
	const uint32_t sheep = s.spawn_fauna(-40.0f, 40.0f, FAUNA_SHEEP);
	turns(0.4f);
	check(!s.player_can_target_fauna(0, sheep), "a sheep in unexplored land is hidden");
	s.units_.pos_x[idx_of(s, woman)] = -40.0f;
	s.units_.pos_z[idx_of(s, woman)] = 36.0f;
	turns(0.4f);
	s.units_.pos_z[idx_of(s, woman)] = -36.0f;
	turns(0.4f);
	check(!s.los_visible(0, -40.0f, 40.0f) && s.player_can_target_fauna(0, sheep),
			"once explored, the sheep in the fog can still be targeted");
}

// Guard.js OnAttacked: a poison or burning tick on the guarded unit sends its guards after
// the source, while the poisoned unit's own UnitAI ignores the tick (fromStatusEffect).
static void test_status_tick_guards() {
	std::printf("91. guards answer a status tick on their charge; the charge doesn't\n");
	Simulation s;
	setup(s);
	uint16_t maiden = CIV_UNIT_TEMPLATE_NONE;
	for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT && maiden == CIV_UNIT_TEMPLATE_NONE; ++t) {
		const char *id = civ_unit_template(t).id;
		const size_t n = std::strlen(id), m = std::strlen("maur/champion_maiden_archer");
		if (n >= m && std::strcmp(id + n - m, "maur/champion_maiden_archer") == 0) maiden = t;
	}
	check(maiden != CIV_UNIT_TEMPLATE_NONE, "the roster has a maiden archer");
	uint32_t charge = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN, 1.0e6f);
	uint32_t guard = s.spawn_unit(2.0f, 0.0f, 0, UNIT_SPEARMAN);
	s.order_set_stance(&charge, 1, STANCE_STAND_GROUND);
	const uint32_t src = s.spawn_unit(30.0f, 0.0f, 1, UNIT_ARCHER_MAIDEN, 0.0f, maiden);
	s.units_.attack_damage[idx_of(s, src)] = 0.0f; // only the status hurts
	s.order_guard(&guard, 1, charge);
	for (int k = 0; k < 10; ++k) s.tick(Simulation::FIXED_STEP);
	s.apply_status((size_t)idx_of(s, charge), unit_combat(maiden, UNIT_ARCHER_MAIDEN), src, 1);
	bool guard_went = false;
	for (int k = 0; k < 60 && !guard_went; ++k) {
		s.tick(Simulation::FIXED_STEP);
		guard_went = target_of(s, guard) == src;
	}
	check(hp_of(s, charge) < 1.0e6f, "the poison ticks on the charge");
	check(guard_went, "its guard goes for the archer who poisoned it");
	check(target_of(s, charge) != src, "the charge itself doesn't answer the tick");
}

// Player.js on defeat makes a player's units Gaia's; UnitAI.OnOwnershipChanged stops them,
// drops their queue and work orders and resets the template's default stance (Q6).
static void test_defeated_units_reset() {
	std::printf("92. a defeated player's units turned Gaia stop and take their default stance\n");
	Simulation s;
	setup(s);
	uint32_t sp = s.spawn_unit(0.0f, 0.0f, 1, UNIT_SPEARMAN);
	s.order_set_stance(&sp, 1, STANCE_PASSIVE);
	s.order_walk(&sp, 1, 20.0f, 0.0f);
	s.order_walk(&sp, 1, 20.0f, 20.0f, true);
	check(s.unit_order_queue(sp) && !s.unit_order_queue(sp)->empty(), "a walk queued behind a walk");
	s.convert_to_gaia(1u << 1);
	const int i = idx_of(s, sp);
	check(s.units().team[i] == GAIA_PLAYER, "the spearman is Gaia's");
	check(s.unit_order(sp).kind == ORDER_NONE && !s.unit_order_queue(sp), "its orders and queue are gone");
	check(s.units().stance[i] == unit_default_stance(UNIT_SPEARMAN, s.units().tmpl[i]), "its stance is the template default again");
}

static void test_all_attackers_cheer() {
	std::printf("93. every unit attacking a unit cheers when it falls, siege engines too (shouldCheer)\n");
	Simulation s;
	setup(s);
	uint32_t a = s.spawn_unit(0, 0, 0, UNIT_SWORDSMAN);
	uint32_t b = s.spawn_unit(0, 3, 0, UNIT_SWORDSMAN);
	uint32_t bolt = s.spawn_unit(-6, 0, 0, UNIT_BOLT_SHOOTER);
	s.order_set_stance(&bolt, 1, STANCE_PASSIVE); // stays idle rather than unpacking to shoot
	uint32_t e = s.spawn_unit(2, 1, 1, UNIT_SPEARMAN, 1.0f);
	s.order_set_stance(&e, 1, STANCE_PASSIVE);
	uint32_t both[2] = {a, b};
	s.order_attack(both, 2, e);
	check(run_until(s, 10.0f, [&] { return !alive(s, e); }), "the two swordsmen kill the enemy");
	check(run_until(s, 1.0f, [&] { return act_of(s, a) == ACTION_CHEER && act_of(s, b) == ACTION_CHEER; }),
			"both cheer, not only the one that landed the kill");
	check(act_of(s, bolt) == ACTION_CHEER, "an idle bolt shooter nearby cheers too");
}

static void test_defensive_held_position() {
	std::printf("94. a defensive unit holds its walk or attack-walk destination, not its work, and walks back there after a given-up chase (heldPosition)\n");
	{
		// Attack-walk: a target near the route but far from the destination is given up at the
		// chase timer, and WalkToHeldPosition turns the attack-walk into a plain walk.
		Simulation s;
		setup(s);
		uint32_t a = s.spawn_unit(0, 0, 0, UNIT_SPEARMAN);
		s.order_set_stance(&a, 1, STANCE_DEFENSIVE);
		uint32_t w = s.spawn_unit(6, 7, 1, UNIT_FEMALE_CITIZEN);
		s.order_set_stance(&w, 1, STANCE_STAND_GROUND);
		s.order_walk(&w, 1, -20, 60); // she walks off, so catching her takes a while
		s.order_attack_move(&a, 1, 60, 0);
		check(run_until(s, 5.0f, [&] { return act_of(s, a) == ACTION_ATTACK; }), "the attack-walker goes for the woman");
		check(run_until(s, 4.0f, [&] { return s.unit_order(a).kind == ORDER_WALK; }),
				"gives her up, so far from its destination, and walks on");
		check(!(s.unit_order(a).flags & ORDER_FLAG_FORCE) && !s.unit_order_queue(a), "an unforced walk that replaced the attack-walk");
		run(s, 20.0f);
		check(dist(s, a, 60, 0) < 3.0f && alive(s, w), "it ends at the destination, the woman untouched");
	}
	{
		// Gathering keeps the held position where the last walk ended. An unforced gather (a
		// rally point's or Back to Work's) answers attacks; a player's own is forced.
		Simulation s;
		setup(s);
		uint32_t a = s.spawn_unit(0, 0, 0, UNIT_SPEARMAN);
		s.order_set_stance(&a, 1, STANCE_DEFENSIVE);
		const uint32_t tree = s.spawn_resource_world(30, 0, RES_WOOD, 5000);
		s.order_gather(&a, 1, tree, false, false);
		check(run_until(s, 15.0f, [&] { return act_of(s, a) == ACTION_GATHER && s.units().carry[idx_of(s, a)] > 0; }),
				"the spearman cuts the tree");
		uint32_t c = s.spawn_unit(32, 2, 1, UNIT_CAVALRY, 5000.0f);
		s.order_set_stance(&c, 1, STANCE_PASSIVE);
		const int ai = idx_of(s, a);
		s.units_.attacked_by[ai] = c;
		s.respond_to_attack((size_t)ai);
		check(act_of(s, a) == ACTION_GATHER, "an attacker at the tree, far from where its last walk ended, is ignored");
		s.order_walk(&a, 1, 27, 0);
		s.order_gather(&a, 1, tree, true, false);
		check(run_until(s, 10.0f, [&] { return act_of(s, a) == ACTION_GATHER; }), "walked to the tree's side, it cuts again");
		s.units_.attacked_by[idx_of(s, a)] = c;
		s.respond_to_attack((size_t)idx_of(s, a));
		check(act_of(s, a) == ACTION_ATTACK, "now it answers");
		s.order_walk(&c, 1, 80, 0); // the rider makes off
		check(run_until(s, 10.0f, [&] { return s.unit_order(a).kind == ORDER_WALK; }), "it gives up the chase");
		run(s, 15.0f);
		check(dist(s, a, 27, 0) < 3.0f && s.unit_order(a).kind != ORDER_GATHER, "and walks back, its gathering dropped");
	}
}

// UnitAI PerformGather / ReturnResource / CollectTreasure on a unit that can't do them:
// WalkToTarget (UnitAI.js:5646-5652, :5741-5747, :673-682). A citizen cavalryman sent with a
// woman to a tree rides there while she gathers; an empty-handed spearman sent with a loaded one
// to the storehouse walks along.
// UnitAI COMBAT (UnitAI.js 2080, 2311): APPROACHING walks, whatever the target does; only CHASING,
// which follows ATTACKING once the target leaves its range, runs after a fleeing target.
static void test_approach_walks() {
	std::printf("95. a unit walks up to a fleeing target and runs only once it has been in range of it (APPROACHING, CHASING)\n");
	Simulation s;
	setup(s);
	const uint32_t woman = s.spawn_unit(0, 0, 0, UNIT_FEMALE_CITIZEN);
	s.order_set_stance(&woman, 1, STANCE_PASSIVE);
	const uint32_t archer = s.spawn_unit(-8, 0, 1, UNIT_ARCHER);
	s.order_attack(&archer, 1, woman);
	const bool fled = run_until(s, 10.0f, [&] { return act_of(s, woman) == ACTION_FLEE; });
	const uint32_t sp = s.spawn_unit(30, 0, 1, UNIT_SPEARMAN);
	s.order_attack(&sp, 1, woman);
	s.tick(DT);
	const size_t si = (size_t)idx_of(s, sp);
	check(fled && act_of(s, woman) == ACTION_FLEE && act_of(s, sp) == ACTION_ATTACK && !s.unit_running(si),
			"a spearman sent at a fleeing woman walks up to her");
	check(std::fabs(s.unit_walk_speed(si) - s.units_.speed[si]) < 1e-4f, "at his walk speed");
	s.order_walk(&archer, 1, -40.0f, 0.0f);
	bool walked = true;
	const bool ran = run_until(s, 40.0f, [&] {
		if (!s.unit_running(si)) return false;
		walked = walked && s.units_.chase_target[si] == woman;
		return true;
	});
	check(ran && walked, "once he has been in range of her and she flees again, he runs after her");
}

static void test_cant_do_walks() {
	std::printf("units that can't gather or return walk to the target\n");
	Simulation s;
	setup(s);
	s.set_explore_map(true);
	const uint32_t tree = s.spawn_resource_world(20, 0, RES_WOOD, 500);
	const uint32_t woman = s.spawn_unit(0, 0, 0, UNIT_FEMALE_CITIZEN);
	const uint32_t cav = s.spawn_unit(0, 3, 0, UNIT_CAVALRY);
	const uint32_t both[2] = {woman, cav};
	s.order_gather(both, 2, tree);
	check(s.unit_order(woman).kind == ORDER_GATHER, "the woman gathers the tree");
	check(s.unit_order(cav).kind == ORDER_WALK && std::fabs(s.unit_order(cav).x - 20.0f) < 0.01f,
			"the citizen cavalryman (meat only) rides to it");
	const uint32_t store = s.spawn_building_world(-20, 20, BLD_STOREHOUSE, 0, 1.0f);
	const uint32_t loaded = s.spawn_unit(-5, 20, 0, UNIT_SPEARMAN), empty = s.spawn_unit(-5, 23, 0, UNIT_SPEARMAN);
	s.units_.carry[idx_of(s, loaded)] = 10;
	s.units_.res_type_carried[idx_of(s, loaded)] = RES_WOOD;
	const uint32_t pair[2] = {loaded, empty};
	s.order_return_resource(pair, 2, store);
	check(s.unit_order(loaded).kind == ORDER_RETURN_RESOURCE && s.unit_order(empty).kind == ORDER_WALK,
			"the loaded one returns, the empty-handed one walks to the storehouse");
}

int main(int argc, char **argv) {
	std::string save = (argc > 1) ? argv[1] : "/tmp/rts0_test_unit_orders.sav";
	test_forced_walk();
	test_retreat();
	test_stance_leash();
	test_attack_move();
	test_guard();
	test_guard_speed();
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
	test_gather_walk_back();
	test_hunter_walk_back();
	test_chase_into_fog();
	test_hidden_attacker();
	test_patrol_loop();
	test_patrol_wait(save.c_str());
	test_capture_regen();
	test_capture_auras();
	test_capture_owner_change();
	test_capture_attack_auras();
	test_loot_auras();
	test_hero_global_auras();
	test_aura_secondary_effects();
	test_nastasen_warships();
	test_gather_rate_auras();
	test_attack_preference_switch();
	test_healer_attack_order();
	test_restricted_attack_order();
	test_walk_to_target_follows();
	test_structure_arrows_and_force();
	test_scenery_capture();
	test_arakamani_temples();
	test_range_check_between_hits();
	test_can_patrol();
	test_unarmed_aggressive_stays();
	test_passive_mid_fight();
	test_stance_held_position();
	test_defensive_walks_back();
	test_guard_stances();
	test_guard_towers_and_animals();
	test_structure_scan_by_centre();
	test_healer_stances();
	test_forced_flag_not_inherited();
	test_idle_drift_walk_back();
	test_queue_after_auto_fight();
	test_healers_on_attack_move();
	test_alert_allied_shelter();
	test_guards_repair_charge();
	test_liu_bang_wall_regen();
	test_guard_memory(save.c_str());
	test_passive_siege_flees();
	test_unload_self_garrison_rally();
	test_allied_builders_continue();
	test_work_order_queue();
	test_rally_commands();
	test_forced_treasure_and_return();
	test_status_tick_guards();
	test_defeated_units_reset();
	test_all_attackers_cheer();
	test_defensive_held_position();
	test_range_aura_not_self();
	test_hidden_targets();
	test_cant_do_walks();
	test_approach_walks();
	if (g_failures) {
		std::printf("test_unit_orders: FAIL (%d)\n", g_failures);
		return 1;
	}
	std::printf("test_unit_orders: PASS\n");
	return 0;
}
