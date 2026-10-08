// RTS0 combat fidelity test (headless: no SCons, no Godot).
// Checks the port of 0 A.D.'s Attack.js, helpers/Attack.js and DelayedDamage.js rules
// (data in civ_attack_data.h from tools/extract_0ad_attack.py):
//  1. Data: template bonuses (spearmen x3 against cavalry), structure armour, classes.
//  2. Spearmen deal their template bonus to cavalry; swordsmen have none.
//  3. RestrictedClasses: a ram never attacks units, a dog never attacks structures.
//  4. Structures take damage through their own template armour.
//  5. Arrows scatter by template spread: close shots nearly always hit, long shots
//     often miss, and a stray arrow hits the enemy it lands on.
//  6. No invented mechanics: structures below a third of their health don't burn
//     down, soldiers cost no food upkeep.
// Build and run:
//   g++ -std=c++20 -O2 -Isrc tools/test_combat.cpp src/simulation/simulation.cpp \
//     src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//     src/network/net_host.cpp -lpthread -o /tmp/test_combat && /tmp/test_combat
// Exit: 0 = PASS, 1 = FAIL.
#define private public
#include "simulation/simulation.h"
#undef private

#include <cmath>
#include <cstdio>

using namespace rtso;

static constexpr float DT = 0.05f;

static int g_failures = 0;
static void check(bool ok, const char *what) {
	std::printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
	if (!ok) ++g_failures;
}

static void setup(Simulation &s) {
	s.set_sim_seed(1234);
	s.reset();
	s.set_victory_condition(VICTORY_ENDLESS);
	s.spawn_building_world(-56.0f, -56.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(56.0f, 56.0f, BLD_TOWN_CENTER, 1, 1.0f);
}
static int idx(const Simulation &s, uint32_t id) { return s.lookup_unit(id); }
// An enemy that holds still (stand ground; passive units run when hit).
static uint32_t dummy(Simulation &s, float x, float z, UnitType type, float hp = 1.0e6f) {
	uint32_t id = s.spawn_unit(x, z, 1, type, hp);
	s.order_set_stance(&id, 1, STANCE_STAND_GROUND);
	return id;
}
static Simulation::Building *building(Simulation &s, uint32_t id) {
	for (auto &b : s.buildings_)
		if (b.id == id) return &b;
	return nullptr;
}

static void test_data() {
	std::printf("1. data\n");
	const CombatProfile &spear = unit_combat(CIV_UNIT_TEMPLATE_NONE, UNIT_SPEARMAN);
	const CombatProfile &cav = unit_combat(CIV_UNIT_TEMPLATE_NONE, UNIT_CAVALRY);
	const CombatProfile &sword = unit_combat(CIV_UNIT_TEMPLATE_NONE, UNIT_SWORDSMAN);
	check(std::fabs(combat_bonus(spear, cav, CIV_ATHENIANS) - 3.0f) < 1e-4f, "spearman x3 against cavalry");
	check(std::fabs(combat_bonus(spear, sword, CIV_ATHENIANS) - 1.0f) < 1e-4f, "spearman x1 against infantry");
	check(std::fabs(combat_bonus(sword, cav, CIV_ATHENIANS) - 1.0f) < 1e-4f, "swordsman has no bonus");
	const CombatProfile &cc = building_combat(BLD_TOWN_CENTER);
	check(cc.hack_armor == 25.0f && cc.pierce_armor == 35.0f && cc.crush_armor == 3.0f, "civic centre armour 25/35/3");
	const CombatProfile &unknown = building_combat(BLD_TYPE_COUNT + 9);
	check(unknown.classes.has(CC_STRUCTURE), "a type past the table is a generic structure");
	const CombatProfile &ram = unit_combat(CIV_UNIT_TEMPLATE_NONE, UNIT_SIEGE_RAM);
	check(!combat_can_target(ram, sword) && combat_can_target(ram, cc), "ram: no units, yes structures");
	const CombatProfile &dog = unit_combat(CIV_UNIT_TEMPLATE_NONE, UNIT_WAR_DOG);
	check(!combat_can_target(dog, cc) && combat_can_target(dog, sword), "dog: no structures, yes units");
	check(unit_combat(CIV_UNIT_TEMPLATE_NONE, UNIT_ARCHER).spread == 2.5f, "archer spread 2.5");
}

// One melee hit from `att` on a fresh dummy of `type`; returns the damage dealt.
static float one_hit(UnitType att, UnitType type, float *expected) {
	Simulation s;
	setup(s);
	uint32_t a = s.spawn_unit(0, 0, 0, att);
	uint32_t d = dummy(s, 1.0f, 0, type);
	int ai = idx(s, a), di = idx(s, d);
	const float before = s.units_.hp[di];
	s.order_attack(&a, 1, d);
	for (int k = 0; k < 200 && s.units_.hp[di] == before; ++k) s.tick(DT);
	const auto &ia = s.unit_info_at(ai);
	const auto &id = s.unit_info_at(di);
	const float scale = ia.attack_damage > 0 ? s.units_.attack_damage[ai] / ia.attack_damage : 1.0f;
	const float bonus = combat_bonus(s.unit_combat_at(ai), s.unit_combat_at(di), s.civ_of(1));
	*expected = std::max(1.0f, bonus * scale * (ia.hack_damage * powf(0.9f, id.hack_armor) +
			ia.pierce_damage * powf(0.9f, id.pierce_armor) + ia.crush_damage * powf(0.9f, id.crush_armor)));
	return before - s.units_.hp[di];
}

static void test_bonus() {
	std::printf("2. template bonuses\n");
	float want = 0;
	float got = one_hit(UNIT_SPEARMAN, UNIT_CAVALRY, &want);
	std::printf("      spearman on cavalry: %.2f (want %.2f)\n", got, want);
	check(std::fabs(got - want) < 0.05f && want > 0, "spearman hits cavalry with its x3 bonus");
	got = one_hit(UNIT_SWORDSMAN, UNIT_SPEARMAN, &want);
	std::printf("      swordsman on spearman: %.2f (want %.2f)\n", got, want);
	check(std::fabs(got - want) < 0.05f, "swordsman on spearman: no invented 1.4x counter");
}

static void test_restricted() {
	std::printf("3. RestrictedClasses\n");
	Simulation s;
	setup(s);
	uint32_t ram = s.spawn_unit(0, 0, 0, UNIT_SIEGE_RAM);
	uint32_t spear = dummy(s, 1.5f, 0, UNIT_SPEARMAN, 500.0f);
	int si = idx(s, spear);
	s.order_attack(&ram, 1, spear);
	for (int k = 0; k < 200; ++k) s.tick(DT);
	check(s.units_.hp[si] == 500.0f, "a ram ordered onto a spearman never hurts it");
	check(s.units_.target_unit_id[idx(s, ram)] != spear, "and drops the order");

	uint32_t bar = s.spawn_building_world(20.0f, 0.0f, BLD_BARRACKS, 1, 1.0f);
	const float bhp = building(s, bar)->hp;
	s.order_attack_building(&ram, 1, bar);
	for (int k = 0; k < 600 && building(s, bar)->hp == bhp; ++k) s.tick(DT);
	check(building(s, bar)->hp < bhp, "the ram does batter a barracks");

	uint32_t dog = s.spawn_unit(0, 10.0f, 0, UNIT_WAR_DOG);
	const float bhp2 = building(s, bar)->hp;
	// keep the ram from counting: send it home first
	s.units_.alive[idx(s, ram)] = 0;
	s.order_attack_building(&dog, 1, bar);
	for (int k = 0; k < 200; ++k) s.tick(DT);
	check(building(s, bar)->hp == bhp2, "a dog never attacks a structure");
}

static void test_structure_armour() {
	std::printf("4. structure armour\n");
	Simulation s;
	setup(s);
	uint32_t sw = s.spawn_unit(0, 0, 0, UNIT_SWORDSMAN);
	uint32_t bar = s.spawn_building_world(6.0f, 0.0f, BLD_BARRACKS, 1, 1.0f);
	const float bhp = building(s, bar)->hp;
	s.order_attack_building(&sw, 1, bar); // damage, not capture
	for (int k = 0; k < 600 && building(s, bar)->hp == bhp; ++k) s.tick(DT);
	int i = idx(s, sw);
	const auto &ia = s.unit_info_at(i);
	const CombatProfile &bc = building_combat(BLD_BARRACKS);
	const float scale = ia.attack_damage > 0 ? s.units_.attack_damage[i] / ia.attack_damage : 1.0f;
	const float want = std::max(1.0f, scale * (ia.hack_damage * powf(0.9f, bc.hack_armor) +
			ia.pierce_damage * powf(0.9f, bc.pierce_armor) + ia.crush_damage * powf(0.9f, bc.crush_armor)));
	const float got = bhp - building(s, bar)->hp;
	std::printf("      swordsman on barracks: %.3f (want %.3f, armour %.0f/%.0f/%.0f)\n", got, want,
			bc.hack_armor, bc.pierce_armor, bc.crush_armor);
	check(std::fabs(got - want) < 0.01f, "barracks take damage through their template armour");
}

// Archer at `dist` shoots a passive enemy until `shots` arrows have landed.
static int hits_at(float dist, int shots, int *fired_out) {
	Simulation s;
	setup(s);
	uint32_t ar = s.spawn_unit(0, 0, 0, UNIT_ARCHER);
	uint32_t d = dummy(s, dist, 0, UNIT_SPEARMAN);
	int di = idx(s, d), ai = idx(s, ar);
	s.units_.attack_range[ai] = std::max(s.units_.attack_range[ai], dist + 1.0f);
	s.order_attack(&ar, 1, d);
	int fired = 0, hits = 0;
	float last_hp = s.units_.hp[di];
	size_t last_proj = 0;
	for (int k = 0; k < 40000 && fired < shots; ++k) {
		s.tick(DT);
		if (s.projectiles_.size() > last_proj) fired += (int)(s.projectiles_.size() - last_proj);
		last_proj = s.projectiles_.size();
		if (s.units_.hp[di] < last_hp) { ++hits; last_hp = s.units_.hp[di]; }
	}
	for (int k = 0; k < 100; ++k) { // let the last arrows land
		s.tick(DT);
		if (s.units_.hp[di] < last_hp) { ++hits; last_hp = s.units_.hp[di]; }
	}
	*fired_out = fired;
	return hits;
}

static void test_spread() {
	std::printf("5. arrows scatter and can miss\n");
	int fired = 0;
	int near_hits = hits_at(3.0f, 60, &fired);
	std::printf("      at 3: %d of %d hit\n", near_hits, fired);
	check(fired >= 60 && near_hits >= fired * 9 / 10, "close shots nearly always hit");
	int far_hits = hits_at(16.0f, 120, &fired);
	std::printf("      at 16: %d of %d hit\n", far_hits, fired);
	// sigma = 2.5 * 16 / 100 = 0.4; footprint 1.5 * 0.28 = 0.42: P(hit) = 1 - e^(-0.55) = 0.42
	check(far_hits > fired / 5 && far_hits < fired * 7 / 10, "long shots hit about 40% of the time");

	// A stray arrow lands on the enemy standing there instead.
	Simulation s;
	setup(s);
	uint32_t ar = s.spawn_unit(0, 0, 0, UNIT_ARCHER);
	s.order_set_stance(&ar, 1, STANCE_PASSIVE); // only the arrows fired by hand
	uint32_t aimed = dummy(s, 10.0f, 0, UNIT_SPEARMAN);
	uint32_t other = dummy(s, 10.0f, 5.0f, UNIT_SPEARMAN);
	const float hp_a = s.units_.hp[idx(s, aimed)], hp_o = s.units_.hp[idx(s, other)];
	s.fire_arrow(0, 1.4f, 0, 10.0f, 0.8f, 5.0f, 20.0f, 0, aimed, 0, 1, false, 0, ar);
	for (int k = 0; k < 40; ++k) s.tick(DT);
	check(s.units_.hp[idx(s, aimed)] == hp_a, "the aimed-at unit is untouched");
	check(s.units_.hp[idx(s, other)] < hp_o, "the unit where it landed is hit");
	s.fire_arrow(0, 1.4f, 0, 20.0f, 0.8f, -10.0f, 20.0f, 0, aimed, 0, 1, false, 0, ar);
	for (int k = 0; k < 40; ++k) s.tick(DT);
	check(s.units_.hp[idx(s, aimed)] == hp_a, "an arrow landing on nobody misses");
}

static void test_no_invented() {
	std::printf("6. no invented mechanics\n");
	Simulation s;
	setup(s);
	uint32_t bar = s.spawn_building_world(20.0f, 20.0f, BLD_BARRACKS, 1, 1.0f);
	Simulation::Building *b = building(s, bar);
	b->hp = b->max_hp * 0.2f;
	const float hp = b->hp;
	for (int k = 0; k < 600; ++k) s.tick(DT);
	check(building(s, bar)->hp >= hp, "a barracks at 20% health doesn't burn down");
	for (int k = 0; k < 40; ++k) dummy(s, -20.0f + (k % 8), -20.0f + (k / 8), UNIT_SWORDSMAN, 100.0f);
	for (int k = 0; k < 40; ++k) s.spawn_unit(-30.0f + (k % 8), 10.0f + (k / 8), 0, UNIT_SWORDSMAN);
	const float f0 = s.player(0).res[RES_FOOD];
	for (int k = 0; k < 600; ++k) s.tick(DT);
	check(s.player(0).res[RES_FOOD] >= f0, "40 soldiers cost player 0 no food upkeep");
}

int main() {
	test_data();
	test_bonus();
	test_restricted();
	test_structure_armour();
	test_spread();
	test_no_invented();
	if (g_failures) {
		std::printf("test_combat: FAIL (%d)\n", g_failures);
		return 1;
	}
	std::printf("test_combat: PASS\n");
	return 0;
}
