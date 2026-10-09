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
//  7. Auras reach the players 0 A.D.'s affectedPlayers names: Hannibal buffs his
//     allies' soldiers, Marcellus and the carnyx weaken enemy soldiers, a temple
//     heals only its owner's humans by 1 hp/s within 40 m.
//  8. Health regeneration: Battlefield Medicine gives idle organic units +0.5 hp/s,
//     not siege engines and not before it is researched.
//  9. Health and vision auras: Ptolemy IV's pikemen get +40% max and current health
//     while near him and lose it when they leave; Han Xin gives allied soldiers +25%
//     vision everywhere.
// 10. Status effects are 0 A.D.'s ApplyStatus: maiden archers poison for 0.5 hp/s over
//     10 s (a second hit extends it), Iberian champion cavalry burn for 2 hp per 3 s
//     over 9 s; rams are immune to poison; spearmen no longer snare.
// 11. ObstructionManager IsInTargetRange: a swordsman just off a fortress's corner is in
//     reach (edge to edge) and hits it where it stands instead of walking to a side.
// 12. Civ structure auras (0 A.D. data/auras/structures): the Kushite pyramids and
//     Iberian monument buff their owner's soldiers and workers nearby, the Mauryan pillar
//     speeds traders, the Temple of Amun costs heroes 5% health, the prytaneion heals
//     garrisoned heroes, soldiers on walls get +3 armour and +20 m vision, a library
//     makes technologies 15% cheaper and a theater widens territory by 20%; Hippocrates
//     gives allied healers +1 per heal and Antiochus IV costs enemy structures 20% health.
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
#include <cstring>

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
	s.set_player_active(1, true, false); // no Petra: it would send a cavalry dummy off scouting
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

static void test_corner_reach() {
	std::printf("11. edge-to-edge reach at a structure's corner\n");
	Simulation s;
	setup(s);
	uint32_t fort = s.spawn_building_world(20.0f, 20.0f, BLD_FORTRESS, 1, 1.0f);
	const Simulation::Building *b = building(s, fort);
	const float h = b->half_size;
	const float r = unit_collision_radius(UNIT_SWORDSMAN);
	// Diagonally off the corner, the gap between the unit and the footprint is 1.3,
	// inside reach; the old centre-distance test added about 0.41 * half_size to it.
	const float off = (r + 1.3f) / std::sqrt(2.0f);
	uint32_t sw = s.spawn_unit(20.0f - h - off, 20.0f - h - off, 0, UNIT_SWORDSMAN);
	const int i = idx(s, sw);
	const float x0 = s.units_.pos_x[i], z0 = s.units_.pos_z[i];
	const float reach = s.units_.attack_range[i];
	std::printf("      fortress half %.2f, swordsman reach %.2f, radius %.2f\n", h, reach, r);
	check(std::sqrt(2.0f) * (h + off) - h - r > reach, "the centre test would call the corner out of reach");
	const float bhp = b->hp;
	s.order_attack_building(&sw, 1, fort);
	for (int k = 0; k < 200 && building(s, fort)->hp == bhp; ++k) s.tick(DT);
	const float moved = std::hypot(s.units_.pos_x[i] - x0, s.units_.pos_z[i] - z0);
	std::printf("      moved %.2f before the first hit\n", moved);
	check(building(s, fort)->hp < bhp, "the swordsman hits the fortress from its corner");
	check(moved < 0.5f, "without walking round to a side");
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

static uint16_t template_named(const char *suffix) {
	for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT; ++t) {
		const char *id = civ_unit_template(t).id;
		size_t n = std::strlen(id), m = std::strlen(suffix);
		if (n >= m && std::strcmp(id + n - m, suffix) == 0) return t;
	}
	return CIV_UNIT_TEMPLATE_NONE;
}

static void test_auras() {
	std::printf("7. auras\n");
	Simulation s;
	setup(s);
	s.set_mutual_ally(0, 2);
	const uint16_t hannibal = template_named("hero_hannibal");
	const uint16_t marcellus = template_named("hero_marcellus");
	const uint16_t cart_spear = template_named("cart/infantry_spearman_b");
	check(hannibal != CIV_UNIT_TEMPLATE_NONE && marcellus != CIV_UNIT_TEMPLATE_NONE
			&& cart_spear != CIV_UNIT_TEMPLATE_NONE, "roster has Hannibal, Marcellus and a Carthaginian spearman");
	auto attack_mod = [&](uint32_t id) {
		int i = idx(s, id);
		return s.aura_mgr_.query_modifier(s.units_.pos_x[i], s.units_.pos_z[i], s.units_.team[i], AURA_ATTACK,
			s.units_.tmpl[i], &s.unit_combat_at(i).classes);
	};
	s.spawn_unit(0.0f, 0.0f, 0, UNIT_HERO, 0.0f, hannibal);
	uint32_t own = s.spawn_unit(2.0f, 0.0f, 0, UNIT_SPEARMAN, 0.0f, cart_spear);
	uint32_t ally = s.spawn_unit(0.0f, 2.0f, 2, UNIT_SPEARMAN, 0.0f, cart_spear);
	uint32_t enemy = s.spawn_unit(-2.0f, 0.0f, 1, UNIT_SPEARMAN, 0.0f, cart_spear);
	s.rebuild_auras();
	check(std::fabs(attack_mod(own) - 1.2f) < 1e-4f, "Hannibal: own soldier +20% attack");
	check(std::fabs(attack_mod(ally) - 1.2f) < 1e-4f, "Hannibal: allied soldier +20% attack");
	check(attack_mod(enemy) == 0.0f, "Hannibal: enemy soldier unaffected");

	Simulation r;
	setup(r);
	r.spawn_unit(0.0f, 0.0f, 1, UNIT_HERO, 0.0f, marcellus);
	r.spawn_unit(30.0f, 30.0f, 1, UNIT_CARNYX);
	uint32_t near_hero = r.spawn_unit(3.0f, 0.0f, 0, UNIT_SWORDSMAN);
	uint32_t own_side = r.spawn_unit(0.0f, 3.0f, 1, UNIT_SWORDSMAN);
	uint32_t near_horn = r.spawn_unit(32.0f, 30.0f, 0, UNIT_SWORDSMAN);
	uint32_t far_horn = r.spawn_unit(38.0f, 30.0f, 0, UNIT_SWORDSMAN);
	uint32_t woman = r.spawn_unit(31.0f, 30.0f, 0, UNIT_FEMALE_CITIZEN);
	r.rebuild_auras();
	auto mod = [&](uint32_t id) {
		int i = idx(r, id);
		return r.aura_mgr_.query_modifier(r.units_.pos_x[i], r.units_.pos_z[i], r.units_.team[i], AURA_ATTACK,
			r.units_.tmpl[i], &r.unit_combat_at(i).classes);
	};
	check(std::fabs(mod(near_hero) - 0.9f) < 1e-4f, "Marcellus: enemy infantry -10% attack");
	check(mod(own_side) == 0.0f, "Marcellus: his own soldiers unaffected");
	check(std::fabs(mod(near_horn) - 0.9f) < 1e-4f, "carnyx: enemy soldier within 20 m -10% attack");
	check(mod(far_horn) == 0.0f, "carnyx: enemy soldier beyond 20 m unaffected");
	check(mod(woman) == 0.0f, "carnyx: enemy women (not soldiers) unaffected");

	Simulation t;
	setup(t);
	t.spawn_building_world(0.0f, 0.0f, BLD_TEMPLE, 0, 1.0f);
	uint32_t near_u = t.spawn_unit(8.0f, 0.0f, 0, UNIT_SWORDSMAN, 50.0f);
	uint32_t far_u = t.spawn_unit(13.0f, 0.0f, 0, UNIT_SWORDSMAN, 50.0f);
	uint32_t foe = t.spawn_unit(0.0f, 8.0f, 1, UNIT_SWORDSMAN, 50.0f);
	uint32_t ram = t.spawn_unit(-8.0f, 0.0f, 0, UNIT_SIEGE_RAM, 50.0f);
	t.rebuild_auras();
	auto heal = [&](uint32_t id) {
		int i = idx(t, id);
		return t.aura_mgr_.query_modifier(t.units_.pos_x[i], t.units_.pos_z[i], t.units_.team[i], AURA_HEAL,
			t.units_.tmpl[i], &t.unit_combat_at(i).classes);
	};
	check(std::fabs(heal(near_u) - 1.0f) < 1e-4f, "temple: own soldier within 40 m +1 hp/s");
	check(heal(far_u) == 0.0f, "temple: own soldier beyond 40 m not healed");
	check(heal(foe) == 0.0f, "temple: enemy soldier not healed");
	check(heal(ram) == 0.0f, "temple: siege (not Human) not healed");
}

static void test_regen() {
	std::printf("8. regeneration\n");
	Simulation s;
	setup(s);
	uint32_t man = s.spawn_unit(-20.0f, 20.0f, 0, UNIT_SWORDSMAN);
	uint32_t ram = s.spawn_unit(-24.0f, 20.0f, 0, UNIT_SIEGE_RAM);
	s.units_.hp[idx(s, man)] = 50.0f;
	s.units_.hp[idx(s, ram)] = 50.0f;
	for (int k = 0; k < 200; ++k) s.tick(DT);
	check(s.units_.hp[idx(s, man)] == 50.0f, "no regeneration before Battlefield Medicine");
	s.complete_research(0, TECH_HEALTH_REGEN_UNITS);
	for (int k = 0; k < 200; ++k) s.tick(DT);
	const float healed = s.units_.hp[idx(s, man)] - 50.0f;
	check(std::fabs(healed - 5.0f) < 0.05f, "idle swordsman regains 0.5 hp/s with Battlefield Medicine");
	check(s.units_.hp[idx(s, ram)] == 50.0f, "a ram (not organic) does not regenerate");
}

static void test_health_vision_auras() {
	std::printf("9. health and vision auras\n");
	Simulation s;
	setup(s);
	s.set_mutual_ally(0, 2);
	const uint16_t ptolemy = template_named("hero_ptolemy_iv");
	const uint16_t pike = template_named("ptol/infantry_pikeman_b");
	const uint16_t han_xin = template_named("han/hero_han_xin_horse");
	check(ptolemy != CIV_UNIT_TEMPLATE_NONE && pike != CIV_UNIT_TEMPLATE_NONE && han_xin != CIV_UNIT_TEMPLATE_NONE,
		"roster has Ptolemy IV, a Ptolemaic pikeman and Han Xin");
	uint32_t hero = s.spawn_unit(-20.0f, 20.0f, 0, UNIT_HERO, 0.0f, ptolemy);
	s.order_set_stance(&hero, 1, STANCE_STAND_GROUND);
	uint32_t p = s.spawn_unit(-18.0f, 20.0f, 0, UNIT_SPEARMAN, 0.0f, pike);
	s.order_set_stance(&p, 1, STANCE_STAND_GROUND);
	const float max0 = s.units_.max_hp[idx(s, p)];
	s.units_.hp[idx(s, p)] = max0 * 0.5f;
	for (int k = 0; k < 20; ++k) s.tick(DT);
	int i = idx(s, p);
	check(std::fabs(s.units_.max_hp[i] - max0 * 1.4f) < 0.01f, "pikeman near Ptolemy IV: max health x1.4");
	check(std::fabs(s.units_.hp[i] / s.units_.max_hp[i] - 0.5f) < 0.01f, "health fraction kept");
	s.units_.pos_x[i] = 20.0f; s.units_.pos_z[i] = -20.0f;
	for (int k = 0; k < 20; ++k) s.tick(DT);
	i = idx(s, p);
	check(std::fabs(s.units_.max_hp[i] - max0) < 0.01f, "pikeman away from him: max health back");

	s.spawn_unit(-30.0f, -30.0f, 0, UNIT_HERO, 0.0f, han_xin);
	uint32_t ally = s.spawn_unit(30.0f, -30.0f, 2, UNIT_SWORDSMAN);
	uint32_t foe = s.spawn_unit(30.0f, 30.0f, 1, UNIT_SWORDSMAN);
	s.rebuild_auras();
	check(std::fabs(s.unit_vision_at(idx(s, ally)) - s.unit_base_vision(idx(s, ally)) * 1.25f) < 1e-3f,
			"Han Xin: allied soldier +25% vision, map-wide");
	check(std::fabs(s.unit_vision_at(idx(s, foe)) - s.unit_base_vision(idx(s, foe))) < 1e-3f, "Han Xin: enemy soldier unchanged");
}

static void test_status() {
	std::printf("10. status effects\n");
	Simulation s;
	setup(s);
	const uint16_t maiden = template_named("maur/champion_maiden_archer");
	const uint16_t iber_cav = template_named("iber/champion_cavalry");
	check(maiden != CIV_UNIT_TEMPLATE_NONE && iber_cav != CIV_UNIT_TEMPLATE_NONE, "roster has maiden archers and Iberian champion cavalry");
	const CombatProfile &mp = unit_combat(maiden, UNIT_ARCHER_MAIDEN);
	const CombatProfile &cp = unit_combat(iber_cav, UNIT_CAVALRY);
	check(mp.status == 1 && std::fabs(mp.status_dps - 0.5f) < 1e-4f && mp.status_time == 10.0f, "maiden archer: Poisoned 1 per 2 s for 10 s");
	check(cp.status == 2 && std::fabs(cp.status_dps - 2.0f / 3.0f) < 1e-3f && cp.status_time == 9.0f, "Iberian champion cavalry: Burning 2 per 3 s for 9 s");
	uint32_t v = s.spawn_unit(-20.0f, 20.0f, 0, UNIT_SWORDSMAN);
	uint32_t ram = s.spawn_unit(-24.0f, 20.0f, 0, UNIT_SIEGE_RAM);
	const float hv = s.units_.hp[idx(s, v)], hr = s.units_.hp[idx(s, ram)];
	s.apply_status((size_t)idx(s, v), mp);
	s.apply_status((size_t)idx(s, v), mp);
	s.apply_status((size_t)idx(s, ram), mp);
	for (int k = 0; k < 500; ++k) s.tick(DT);
	check(std::fabs((hv - s.units_.hp[idx(s, v)]) - 10.0f) < 0.1f, "two poisoned hits: 0.5 hp/s for 20 s");
	check(s.units_.hp[idx(s, ram)] == hr, "a ram is immune to poison");
	check(s.units_.status_effects[idx(s, v)] == 0, "poison wears off");

	Simulation m;
	setup(m);
	uint32_t sp = m.spawn_unit(0.0f, 0.0f, 1, UNIT_SPEARMAN);
	uint32_t cav = m.spawn_unit(1.5f, 0.0f, 0, UNIT_CAVALRY, 1.0e6f);
	m.order_set_stance(&cav, 1, STANCE_STAND_GROUND);
	(void)sp;
	bool snared = false;
	for (int k = 0; k < 200; ++k) {
		m.tick(DT);
		if (m.units_.status_effects[idx(m, cav)] & ~(EFFECT_POISON | EFFECT_BURNING)) snared = true;
	}
	check(!snared, "spearmen hitting cavalry apply no invented snare");
}

static void test_structure_auras() {
	std::printf("12. civ structure auras\n");
	Simulation s;
	setup(s);
	s.set_team_civ(0, CIV_KUSHITES);
	s.set_team_civ(1, CIV_KUSHITES);
	const uint16_t spear = template_named("kush/infantry_spearman_b");
	const uint16_t woman = template_named("kush/support_female_citizen");
	check(spear != CIV_UNIT_TEMPLATE_NONE && woman != CIV_UNIT_TEMPLATE_NONE, "roster has a Kushite spearman and woman");
	s.spawn_building_world(-20.0f, 20.0f, BLD_PYRAMID_LARGE, 0, 1.0f);
	s.spawn_building_world(20.0f, -20.0f, BLD_PYRAMID_SMALL, 0, 1.0f);
	const uint32_t own = s.spawn_unit(-14.0f, 20.0f, 0, UNIT_SPEARMAN, 0.0f, spear);
	const uint32_t far = s.spawn_unit(-20.0f, -10.0f, 0, UNIT_SPEARMAN, 0.0f, spear);
	const uint32_t foe = s.spawn_unit(-20.0f, 26.0f, 1, UNIT_SPEARMAN, 0.0f, spear);
	const uint32_t worker = s.spawn_unit(20.0f, -14.0f, 0, UNIT_FEMALE_CITIZEN, 0.0f, woman);
	s.rebuild_auras();
	auto mod = [&](uint32_t id, AuraType t) {
		int i = idx(s, id);
		return s.aura_mgr_.query_modifier(s.units_.pos_x[i], s.units_.pos_z[i], s.units_.team[i], t,
			s.units_.tmpl[i], &s.unit_combat_at(i).classes);
	};
	check(std::fabs(mod(own, AURA_ATTACK) - 1.1f) < 1e-4f, "large pyramid: own soldier within 70 m +10% attack");
	check(std::fabs(mod(own, AURA_ARMOR) - 1.0f) < 1e-4f, "large pyramid: own soldier +1 resistance");
	float res_in[3] = {}, res_far[3] = {};
	s.unit_resistance((size_t)idx(s, own), res_in);
	s.unit_resistance((size_t)idx(s, far), res_far);
	check(std::fabs(res_in[2] - res_far[2] - 1.0f) < 1e-4f, "armour auras reach crush too");
	check(mod(far, AURA_ATTACK) == 0.0f, "large pyramid: soldier beyond 70 m unaffected");
	check(mod(foe, AURA_ATTACK) == 0.0f, "large pyramid: enemy soldier unaffected");
	check(std::fabs(mod(worker, AURA_GATHER) - (1.0f - 1.0f / 1.15f)) < 1e-3f, "small pyramid: own worker gathers 15% faster");

	// Temple of Amun: own heroes -5% health everywhere.
	const uint16_t amanirenas = template_named("kush/hero_amanirenas");
	const uint32_t hero = s.spawn_unit(40.0f, 40.0f, 0, UNIT_HERO, 0.0f, amanirenas);
	s.order_set_stance(&hero, 1, STANCE_STAND_GROUND);
	const float hero_max = s.units_.max_hp[idx(s, hero)];
	s.spawn_building_world(-40.0f, -40.0f, BLD_TEMPLE_AMUN, 0, 1.0f);
	for (int k = 0; k < 40; ++k) s.tick(DT);
	check(std::fabs(s.units_.max_hp[idx(s, hero)] - hero_max * 0.95f) < 0.01f, "Temple of Amun: hero max health x0.95");

	// Iberian monument (+20% attack) and Mauryan pillar (traders +20% speed).
	Simulation m;
	setup(m);
	m.set_team_civ(0, CIV_IBERIANS);
	m.set_team_civ(1, CIV_MAURYANS);
	const uint16_t iber = template_named("iber/infantry_spearman_b");
	const uint16_t trader = template_named("maur/support_trader");
	m.spawn_building_world(-20.0f, 20.0f, BLD_MONUMENT, 0, 1.0f);
	m.spawn_building_world(20.0f, -20.0f, BLD_PILLAR, 1, 1.0f);
	const uint32_t is = m.spawn_unit(-14.0f, 20.0f, 0, UNIT_SPEARMAN, 0.0f, iber);
	const uint32_t tr = m.spawn_unit(20.0f, -14.0f, 1, UNIT_TRADER, 0.0f, trader);
	m.rebuild_auras();
	auto mm = [&](uint32_t id, AuraType t) {
		int i = idx(m, id);
		return m.aura_mgr_.query_modifier(m.units_.pos_x[i], m.units_.pos_z[i], m.units_.team[i], t,
			m.units_.tmpl[i], &m.unit_combat_at(i).classes);
	};
	check(std::fabs(mm(is, AURA_ATTACK) - 1.2f) < 1e-4f, "monument: own soldier +20% attack");
	check(std::fabs(mm(tr, AURA_SPEED) - 1.2f) < 1e-4f, "pillar: own trader +20% speed");
	const float base_speed = m.units_.speed[idx(m, tr)];
	for (int k = 0; k < 20; ++k) m.tick(DT);
	check(m.units_.speed[idx(m, tr)] == base_speed, "speed auras apply while moving, not to the stored speed");

	// Prytaneion: garrisoned heroes regain 6 hp/s.
	Simulation g;
	setup(g);
	g.set_team_civ(0, CIV_ATHENIANS);
	const uint32_t pryt = g.spawn_building_world(-20.0f, 20.0f, BLD_PRYTANEION, 0, 1.0f);
	const uint32_t pericles = g.spawn_unit(-14.0f, 20.0f, 0, UNIT_HERO, 0.0f, template_named("athen/hero_pericles"));
	g.units_.hp[idx(g, pericles)] = 100.0f;
	check(g.garrison_unit_now(pericles, pryt), "Pericles garrisons in the prytaneion");
	for (int k = 0; k < 40; ++k) g.tick(DT);
	check(std::fabs(g.units_.hp[idx(g, pericles)] - 112.0f) < 0.5f, "prytaneion: garrisoned hero +6 hp/s");

	// Soldiers on a wall: +3 resistance, +20 m (x0.28) vision.
	const uint32_t guard = g.spawn_unit(20.0f, 20.0f, 0, UNIT_SWORDSMAN);
	float before[3] = {}, after[3] = {};
	g.unit_resistance((size_t)idx(g, guard), before);
	const float vis = g.unit_vision_at((size_t)idx(g, guard));
	g.turret_height_[guard] = 2.0f;
	g.unit_resistance((size_t)idx(g, guard), after);
	check(std::fabs(after[0] - before[0] - 3.0f) < 1e-4f && std::fabs(after[2] - before[2] - 3.0f) < 1e-4f,
		"turreted soldier: +3 hack and crush resistance");
	check(std::fabs(g.unit_vision_at((size_t)idx(g, guard)) - vis - 5.6f) < 1e-3f, "turreted soldier: +20 m vision");
	g.turret_height_.erase(guard);

	// Library: technologies 15% cheaper (cost and time). Theater: territory +20%.
	Simulation l;
	setup(l);
	l.set_team_civ(0, CIV_PTOLEMIES);
	const uint32_t store = l.spawn_building_world(-30.0f, -40.0f, BLD_STOREHOUSE, 0, 1.0f);
	for (float &r : l.players_[0].res) r = 5000.0f;
	check(l.cmd_research(0, store, TECH_WOODCUTTING_1), "research Iron Axe Heads");
	const float wood_plain = 5000.0f - l.players_[0].res[RES_WOOD];
	const float time_plain = l.buildings_[l.building_id_to_idx_[store]].research_duration;
	Simulation l2;
	setup(l2);
	l2.set_team_civ(0, CIV_PTOLEMIES);
	const uint32_t store2 = l2.spawn_building_world(-30.0f, -40.0f, BLD_STOREHOUSE, 0, 1.0f);
	l2.spawn_building_world(-30.0f, -20.0f, BLD_LIBRARY, 0, 1.0f);
	for (float &r : l2.players_[0].res) r = 5000.0f;
	check(l2.cmd_research(0, store2, TECH_WOODCUTTING_1), "research Iron Axe Heads with a library");
	check(std::fabs(5000.0f - l2.players_[0].res[RES_WOOD] - std::round(wood_plain * 0.85f)) < 1.0f, "library: tech cost x0.85");
	check(std::fabs(l2.buildings_[l2.building_id_to_idx_[store2]].research_duration - time_plain * 0.85f) < 1e-3f,
		"library: tech time x0.85");
	l2.rebuild_territory();
	float cc_plain = 0.0f;
	for (const auto &r : l2.territory_roots_) if (r.team == 0 && r.radius >= 32.0f) cc_plain = r.radius;
	l2.spawn_building_world(-10.0f, -40.0f, BLD_THEATER, 0, 1.0f);
	l2.rebuild_territory();
	float cc_theater = 0.0f;
	for (const auto &r : l2.territory_roots_) if (r.team == 0 && r.radius >= 32.0f) cc_theater = r.radius;
	check(cc_plain == 32.0f && std::fabs(cc_theater - 32.0f * 1.2f) < 1e-3f, "theater: civic centre territory x1.2");

	// Hippocrates' Hippocratic Oath: own and allied healers +1 health per heal, anywhere.
	Simulation h;
	setup(h);
	h.set_mutual_ally(0, 2);
	const uint32_t own_healer = h.spawn_unit(-30.0f, 30.0f, 0, UNIT_HEALER);
	const uint32_t ally_healer = h.spawn_unit(30.0f, -30.0f, 2, UNIT_HEALER);
	const uint32_t foe_healer = h.spawn_unit(30.0f, 30.0f, 1, UNIT_HEALER);
	const float plain = h.heal_amount_of((size_t)idx(h, own_healer));
	h.spawn_unit(-40.0f, -40.0f, 0, UNIT_HERO, 0.0f, template_named("athen/hero_hippocrates"));
	h.rebuild_auras();
	check(std::fabs(h.heal_amount_of((size_t)idx(h, own_healer)) - plain - 1.0f) < 1e-4f, "Hippocrates: own healer +1 per heal");
	check(std::fabs(h.heal_amount_of((size_t)idx(h, ally_healer)) - plain - 1.0f) < 1e-4f, "Hippocrates: allied healer +1 per heal");
	check(h.heal_amount_of((size_t)idx(h, foe_healer)) == plain, "Hippocrates: enemy healer unchanged");

	// Antiochus IV's Renowned Conqueror: enemy structures within 80 m -20% max and current health.
	Simulation a;
	setup(a);
	const uint32_t foe_house = a.spawn_building_world(-30.0f, 30.0f, BLD_HOUSE, 1, 1.0f);
	const uint32_t own_house = a.spawn_building_world(-30.0f, 24.0f, BLD_HOUSE, 0, 1.0f);
	auto bld = [&](uint32_t id) -> Simulation::Building & { return a.buildings_[a.building_id_to_idx_[id]]; };
	const float foe_max = bld(foe_house).max_hp, own_max = bld(own_house).max_hp;
	bld(foe_house).hp = foe_max * 0.5f;
	const uint32_t ant = a.spawn_unit(-24.0f, 30.0f, 0, UNIT_HERO, 0.0f, template_named("sele/hero_antiochus_iv"));
	a.order_set_stance(&ant, 1, STANCE_STAND_GROUND);
	for (int k = 0; k < 20; ++k) a.tick(DT);
	check(std::fabs(bld(foe_house).max_hp - foe_max * 0.8f) < 0.01f, "Antiochus IV: enemy house max health x0.8");
	check(std::fabs(bld(foe_house).hp / bld(foe_house).max_hp - 0.5f) < 0.01f, "Antiochus IV: health fraction kept");
	check(bld(own_house).max_hp == own_max, "Antiochus IV: own house unchanged");
	const int ai = idx(a, ant);
	a.units_.pos_x[ai] = 40.0f; a.units_.pos_z[ai] = -40.0f;
	a.units_.alive[ai] = 0; // gone from the field
	for (int k = 0; k < 20; ++k) a.tick(DT);
	check(std::fabs(bld(foe_house).max_hp - foe_max) < 0.01f, "Antiochus IV gone: enemy house health back");
}

int main() {
	test_data();
	test_bonus();
	test_restricted();
	test_structure_armour();
	test_spread();
	test_no_invented();
	test_auras();
	test_regen();
	test_health_vision_auras();
	test_status();
	test_corner_reach();
	test_structure_auras();
	if (g_failures) {
		std::printf("test_combat: FAIL (%d)\n", g_failures);
		return 1;
	}
	std::printf("test_combat: PASS\n");
	return 0;
}
