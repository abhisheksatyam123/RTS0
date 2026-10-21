// RTS0 combat fidelity test (headless: no SCons, no Godot).
// Checks the port of 0 A.D.'s Attack.js, helpers/Attack.js and DelayedDamage.js rules
// (data in civ_attack_data.h from tools/extract_0ad_attack.py):
//  1. Data: template bonuses (spearmen x3 against cavalry), structure armour, classes.
//  2. Spearmen deal their template bonus to cavalry; swordsmen have none.
//  3. RestrictedClasses: a ram never attacks units, a dog never attacks structures.
//  4. Structures take damage through their own template armour; foundations through
//     template_structure's Foundation form (1/10/1).
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
// 13. Attack timing and projectiles: a spearman that walks up strikes PrepareTime
//     (0.5 s) after it reaches its foe, then every RepeatTime; an arrow flies at its
//     shooter's Projectile Speed (at the distance scale) with a Gravity arc; an arrow
//     that lands on another unit deals what the archer's attack does to that unit.
// 14. Damage as GetTotalAttackEffects: no minimum (an archer barely scratches a ram);
//     a unit's damage to structures follows its techs, rank and attack auras like its
//     damage to units; capture strength is divided by 0.1 + 0.9 * hp / maxHp.
// 15. Projectiles land on what is there (DelayedDamage.js Hit): an arrow aimed at a
//     structure that lands on a unit hurts the unit, one aimed at a unit that lands in an
//     enemy structure hurts the structure, each by the shooter's attack against its own
//     armour at impact; one landing on nothing misses; arrows at structures scatter.
// 16. A ranged hunter shoots arrows at the animal (Attack.js PerformAttack); the one that
//     kills it gives the archer its Loot xp and the archer goes on to gather the carcass.
// 17. Poisoned and Burning damage counts as an attack by whoever applied it: the kill and
//     the statistics go to them (no xp, as 0.28's HandleAttackEffects), and the source is saved.
// 18. Germanic hero and champion auras: Boiorix gives his own soldiers within 60 m +5 m
//     ranged and +2 m melee reach, Lugius costs enemy units and structures within 100 m
//     20 m vision, the Seeress drains enemies within 15 m by 2 hp/s, down to death.
// 19. Aura stacking (Auras.js, ModifiersManager): different auras stack (the Kushite large
//     pyramid's +10% attack with Hannibal's +20%), one aura counts once however many
//     sources project it (two Marcellus, two pyramids), and a battering ram walks
//     0.15 m/s faster per Infantry unit inside (units/ram_garrison).
// Build and run:
//   g++ -std=c++20 -O2 -Isrc tools/test_combat.cpp src/simulation/simulation.cpp \
//     src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//     src/network/net_host.cpp -lpthread -o /tmp/test_combat && /tmp/test_combat
// Exit: 0 = PASS, 1 = FAIL.
#define private public
#include "simulation/simulation.h"
#undef private

#include <cmath>
#include <set>
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
	check(std::fabs(combat_bonus(spear, cav, CIV_ATHENIANS) - 2.5f) < 1e-4f, "spearman x2.5 against cavalry (0.28)");
	check(std::fabs(combat_bonus(spear, sword, CIV_ATHENIANS) - 1.0f) < 1e-4f, "spearman x1 against infantry");
	check(std::fabs(combat_bonus(sword, cav, CIV_ATHENIANS) - 1.0f) < 1e-4f, "swordsman has no bonus");
	const CombatProfile &cc = building_combat(BLD_TOWN_CENTER);
	check(cc.hack_armor == 29.0f && cc.pierce_armor == 35.0f && cc.crush_armor == 3.0f, "civic centre armour 29/35/3 (0.28)");
	const CombatProfile &unknown = building_combat(BLD_TYPE_COUNT + 9);
	check(unknown.classes.has(CC_STRUCTURE), "a type past the table is a generic structure");
	const CombatProfile &ram = unit_combat(CIV_UNIT_TEMPLATE_NONE, UNIT_SIEGE_RAM);
	check(!combat_can_target(ram, sword) && combat_can_target(ram, cc), "ram: no units, yes structures");
	const CombatProfile &dog = unit_combat(CIV_UNIT_TEMPLATE_NONE, UNIT_WAR_DOG);
	check(!combat_can_target(dog, cc) && combat_can_target(dog, sword), "dog: no structures, yes units");
	check(unit_combat(CIV_UNIT_TEMPLATE_NONE, UNIT_ARCHER).spread == 2.25f, "archer spread 2.25 (0.28)");
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
	check(building(s, bar)->hp >= bhp2, "a dog never attacks a structure"); // Petra's units may repair it meanwhile
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
	// GetTotalAttackEffects: no minimum of 1 (#276).
	const float want = scale * (ia.hack_damage * powf(0.9f, bc.hack_armor) +
			ia.pierce_damage * powf(0.9f, bc.pierce_armor) + ia.crush_damage * powf(0.9f, bc.crush_armor));
	const float got = bhp - building(s, bar)->hp;
	std::printf("      swordsman on barracks: %.3f (want %.3f, armour %.0f/%.0f/%.0f)\n", got, want,
			bc.hack_armor, bc.pierce_armor, bc.crush_armor);
	check(std::fabs(got - want) < 0.01f, "barracks take damage through their template armour");
	// Resistance.js: a foundation's Foundation form, Hack 1 Pierce 10 Crush 1 for every structure.
	const uint32_t fnd = s.spawn_building_world(-20.0f, 20.0f, BLD_FORTRESS, 1, 0.5f);
	float fr[3], br[3];
	s.structure_resistance(*building(s, fnd), fr);
	s.structure_resistance(*building(s, bar), br);
	std::printf("      fortress foundation resistance %.0f/%.0f/%.0f, finished barracks %.0f/%.0f/%.0f\n",
			fr[0], fr[1], fr[2], br[0], br[1], br[2]);
	check(fr[0] == 1.0f && fr[1] == 10.0f && fr[2] == 1.0f, "a foundation takes damage with the Foundation resistance");
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
	s.set_player_active(1, true, false); // no Petra: its defense would send the dummy at the archer
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
	// "Ally" (Auras.js): the players Hannibal's owner counts as allies, whatever they answer.
	s.set_diplomacy(0, 3, Simulation::DIPLO_ALLY);
	s.set_diplomacy(4, 0, Simulation::DIPLO_ALLY);
	uint32_t courted = s.spawn_unit(2.0f, 2.0f, 3, UNIT_SPEARMAN, 0.0f, cart_spear);
	uint32_t courting = s.spawn_unit(-2.0f, 2.0f, 4, UNIT_SPEARMAN, 0.0f, cart_spear);
	s.rebuild_auras();
	check(std::fabs(attack_mod(courted) - 1.2f) < 1e-4f, "Hannibal: a player his owner allies one-sidedly gets +20%");
	check(attack_mod(courting) == 0.0f, "Hannibal: a player allying his owner one-sidedly does not");

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

	// Units' own auras from their templates: the Roman centurion's Combat Leadership
	// (soldiers +10% attack within 30 m) and the Gallic trumpeter's carnyx, which the
	// roster row now carries itself on top of the generic carnyx (one effect, not two).
	Simulation c;
	setup(c);
	const uint16_t centurion = template_named("rome/champion_infantry_swordsman_centurion");
	const uint16_t trumpeter = template_named("gaul/champion_infantry_trumpeter");
	check(centurion != CIV_UNIT_TEMPLATE_NONE && trumpeter != CIV_UNIT_TEMPLATE_NONE,
		"roster has the centurion and the Gallic trumpeter");
	c.spawn_unit(0.0f, 0.0f, 0, UNIT_SWORDSMAN, 0.0f, centurion);
	uint32_t legionary = c.spawn_unit(3.0f, 0.0f, 0, UNIT_SWORDSMAN);
	c.spawn_unit(30.0f, 30.0f, 1, UNIT_CARNYX, 0.0f, trumpeter);
	uint32_t foe_near_horn = c.spawn_unit(32.0f, 30.0f, 0, UNIT_SWORDSMAN);
	c.rebuild_auras();
	auto cmod = [&](uint32_t id) {
		int i = idx(c, id);
		return c.aura_mgr_.query_modifier(c.units_.pos_x[i], c.units_.pos_z[i], c.units_.team[i], AURA_ATTACK,
			c.units_.tmpl[i], &c.unit_combat_at(i).classes);
	};
	check(std::fabs(cmod(legionary) - 1.1f) < 1e-4f, "centurion: own soldier within 30 m +10% attack");
	check(std::fabs(cmod(foe_near_horn) - 0.9f) < 1e-4f, "Gallic trumpeter: enemy soldier -10% attack, once");

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
	// Auras.js: one aura applies once, however many sources project it; the Temple of Isis
	// projects the same temple_heal.
	t.spawn_building_world(4.0f, -4.0f, BLD_TEMPLE, 0, 1.0f);
	t.spawn_building_world(-4.0f, 4.0f, BLD_TEMPLE_ISIS, 0, 1.0f);
	t.rebuild_auras();
	std::printf("      two temples and an Isis temple heal %.2f hp/s\n", heal(near_u));
	check(std::fabs(heal(near_u) - 1.0f) < 1e-4f, "temple: overlapping temples heal +1 hp/s, not +1 each");
	// A global hero aura holds while its hero is garrisoned (Darius: own units +15% speed).
	Simulation g;
	setup(g);
	const uint32_t darius = g.spawn_unit(0.0f, 0.0f, 0, UNIT_HERO, 0.0f, template_named("pers/hero_darius_i"));
	const uint32_t walker = g.spawn_unit(30.0f, 30.0f, 0, UNIT_SWORDSMAN, 50.0f);
	auto speed = [&]() {
		int i = idx(g, walker);
		return g.aura_mgr_.query_modifier(g.units_.pos_x[i], g.units_.pos_z[i], g.units_.team[i], AURA_SPEED,
			g.units_.tmpl[i], &g.unit_combat_at(i).classes);
	};
	g.rebuild_auras();
	const float out = speed();
	const uint32_t bk = g.spawn_building_world(0.0f, 8.0f, BLD_FORTRESS, 0, 1.0f);
	check(g.garrison_unit_now(darius, bk), "Darius garrisoned in the fortress");
	g.rebuild_auras();
	std::printf("      Darius' speed aura: %.3f outside, %.3f garrisoned\n", out, speed());
	check(std::fabs(out - 1.15f) < 1e-4f && std::fabs(speed() - out) < 1e-4f, "a garrisoned hero's global aura still applies");
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
	// Health.js keeps regenerating a garrisoned unit (idle inside), even where BuffHeal is 0.
	uint32_t bk = s.spawn_building_world(-30.0f, 30.0f, BLD_BARRACKS, 0, 1.0f);
	uint32_t in = s.spawn_unit(-30.0f, 36.0f, 0, UNIT_SWORDSMAN);
	check(s.garrison_unit_now(in, bk), "swordsman garrisoned in the barracks");
	s.units_.hp[idx(s, in)] = 50.0f;
	for (int k = 0; k < 200; ++k) s.tick(DT);
	const float inside = s.units_.hp[idx(s, in)] - 50.0f;
	std::printf("      garrisoned swordsman regained %.2f hp in 10 s\n", inside);
	check(std::fabs(inside - 5.0f) < 0.05f, "a garrisoned swordsman regains 0.5 hp/s too");
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
	check(mp.status == 1 && mp.status_damage == 2.0f && std::fabs(mp.status_interval - 0.6f) < 1e-6f && std::fabs(mp.status_time - 3.6f) < 1e-4f,
			"maiden archer: Poisoned 2 per 0.6 s for 3.6 s (0.28)");
	check(cp.status == 2 && cp.status_damage == 2.0f && cp.status_interval == 3.0f && cp.status_time == 9.0f, "Iberian champion cavalry: Burning 2 per 3 s for 9 s");
	uint32_t v = s.spawn_unit(-20.0f, 20.0f, 0, UNIT_SWORDSMAN);
	uint32_t ram = s.spawn_unit(-24.0f, 20.0f, 0, UNIT_SIEGE_RAM);
	const float hv = s.units_.hp[idx(s, v)], hr = s.units_.hp[idx(s, ram)];
	s.apply_status((size_t)idx(s, v), mp);
	s.apply_status((size_t)idx(s, v), mp);
	s.apply_status((size_t)idx(s, ram), mp);
	{
		// selection_details.js statusEffectsIcons: each stacked copy, its Damage, Interval and
		// the Duration still to run.
		Simulation::StatusView sv[5];
		check(s.statuses_on(Simulation::STATUS_ON_UNIT, v, sv, 5) == 2 && sv[1].kind == 1 && sv[1].damage == 2.0f &&
				std::fabs(sv[1].interval - 0.6f) < 1e-6f && std::fabs(sv[1].remaining - 3.6f) < 1e-4f,
				"the details panel lists both poison stacks with their full duration");
		check(s.statuses_on(Simulation::STATUS_ON_UNIT, ram, sv, 5) == 0, "and none on the immune ram");
		for (int k = 0; k < 15; ++k) s.tick(DT); // the first tick, then one Interval later
		s.statuses_on(Simulation::STATUS_ON_UNIT, v, sv, 5);
		check(std::fabs(sv[0].remaining - 3.0f) < 1e-3f, "the duration counts down an Interval at a time");
	}
	for (int k = 0; k < 500; ++k) s.tick(DT);
	// StatusEffectsReceiver Stack: each hit is its own status, ticking at once and every 0.6 s
	// until 3.6 s have passed, 7 ticks of 2.
	check(std::fabs((hv - s.units_.hp[idx(s, v)]) - 28.0f) < 1e-3f, "two poisoned hits stack: 2 x 7 ticks of 2");
	check(s.units_.hp[idx(s, ram)] == hr, "a ram is immune to poison");
	check(s.units_.status_effects[idx(s, v)] == 0, "poison wears off");

	// C2: structures and animals have a StatusEffectsReceiver too; structures block only poison.
	{
		Simulation w;
		setup(w);
		const uint32_t house = w.spawn_building_world(20.0f, -20.0f, BLD_HOUSE, 1, 1.0f);
		Simulation::Building &hb = w.buildings_[w.building_id_to_idx_.at(house)];
		const float h0 = hb.hp;
		w.apply_status_structure(hb, cp, 0, 0);
		w.apply_status_structure(hb, mp, 0, 0);
		const uint32_t deer = w.spawn_fauna(-28.0f, -40.0f, FAUNA_DEER);
		const size_t df = w.fauna_id_to_idx_.at(deer);
		const float d0 = w.fauna_.hp[df];
		w.apply_status_fauna(df, cp, 0, 0);
		for (int k = 0; k < 300; ++k) w.tick(DT);
		const Simulation::Building &hb2 = w.buildings_[w.building_id_to_idx_.at(house)];
		check(std::fabs((h0 - hb2.hp) - 8.0f) < 1e-3f, "Iberian champion cavalry's burn on a house: 4 ticks of 2, no poison");
		const auto fit = w.fauna_id_to_idx_.find(deer);
		check(fit != w.fauna_id_to_idx_.end() && std::fabs((d0 - w.fauna_.hp[fit->second]) - 8.0f) < 1e-3f, "and on a deer");
		check(w.statuses_.empty(), "both burn out");
	}
	// Saved halfway, a burn deals the rest after loading.
	{
		Simulation w;
		setup(w);
		const uint32_t sw = dummy(w, 20.0f, 20.0f, UNIT_SWORDSMAN);
		const float h0 = w.units_.hp[idx(w, sw)];
		w.apply_status((size_t)idx(w, sw), cp, 0, 0);
		for (int k = 0; k < (int)(4.0f / DT); ++k) w.tick(DT);
		const char *path = "/tmp/rts0_test_status_half.sav";
		check(w.save_state(path), "save mid-burn");
		Simulation r;
		check(r.load_state(path), "load mid-burn");
		std::remove(path);
		for (int k = 0; k < 300; ++k) r.tick(DT);
		check(std::fabs((h0 - r.units_.hp[idx(r, sw)]) - 8.0f) < 1e-3f, "a burn saved halfway still deals 8 in all");
	}

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
	// Different auras multiply (ModifiersManager): Viridomarus' global +15% with the pyramid's.
	const uint32_t viridomarus = s.spawn_unit(-40.0f, 40.0f, 0, UNIT_HERO, 0.0f, template_named("gaul/hero_viridomarus"));
	s.order_set_stance(&viridomarus, 1, STANCE_STAND_GROUND);
	s.rebuild_auras();
	std::printf("      pyramid and Viridomarus: interval cut %.4f\n", mod(worker, AURA_GATHER));
	check(std::fabs(mod(worker, AURA_GATHER) - (1.0f - 1.0f / (1.15f * 1.15f))) < 1e-3f, "pyramid and Viridomarus: +15% twice, multiplied");

	// Temple of Amun: own heroes -5% health everywhere.
	const uint16_t amanirenas = template_named("kush/hero_amanirenas");
	const uint32_t hero = s.spawn_unit(40.0f, 40.0f, 0, UNIT_HERO, 0.0f, amanirenas);
	s.order_set_stance(&hero, 1, STANCE_STAND_GROUND);
	const float hero_max = s.units_.max_hp[idx(s, hero)];
	s.spawn_building_world(-40.0f, -40.0f, BLD_TEMPLE_AMUN, 0, 1.0f);
	for (int k = 0; k < 40; ++k) s.tick(DT);
	check(std::fabs(s.units_.max_hp[idx(s, hero)] - hero_max * 0.95f) < 0.01f, "Temple of Amun: hero max health x0.95");
	// Stackable (kush_temple_amun.json): a second temple, say a captured one, takes another 5%.
	s.spawn_building_world(-40.0f, 40.0f, BLD_TEMPLE_AMUN, 0, 1.0f);
	for (int k = 0; k < 40; ++k) s.tick(DT);
	check(std::fabs(s.units_.max_hp[idx(s, hero)] - hero_max * 0.95f * 0.95f) < 0.01f, "two Temples of Amun: x0.95 twice");

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
	const float time_plain = l.buildings_[l.building_id_to_idx_[store]].queue.front().duration;
	Simulation l2;
	setup(l2);
	l2.set_team_civ(0, CIV_PTOLEMIES);
	const uint32_t store2 = l2.spawn_building_world(-30.0f, -40.0f, BLD_STOREHOUSE, 0, 1.0f);
	l2.spawn_building_world(-30.0f, -20.0f, BLD_LIBRARY, 0, 1.0f);
	for (float &r : l2.players_[0].res) r = 5000.0f;
	check(l2.cmd_research(0, store2, TECH_WOODCUTTING_1), "research Iron Axe Heads with a library");
	check(std::fabs(5000.0f - l2.players_[0].res[RES_WOOD] - std::round(wood_plain * 0.85f)) < 1.0f, "library: tech cost x0.85");
	check(std::fabs(l2.buildings_[l2.building_id_to_idx_[store2]].queue.front().duration - time_plain * 0.85f) < 1e-3f,
		"library: tech time x0.85");
	l2.rebuild_territory();
	const float cc_r = territory_influence_info(BLD_TOWN_CENTER).radius; // 140 m (TerritoryInfluence)
	float cc_plain = 0.0f;
	for (const auto &r : l2.territory_roots_) if (r.team == 0 && r.radius >= cc_r) cc_plain = r.radius;
	l2.spawn_building_world(-10.0f, -40.0f, BLD_THEATER, 0, 1.0f);
	l2.rebuild_territory();
	float cc_theater = 0.0f;
	for (const auto &r : l2.territory_roots_) if (r.team == 0 && r.radius >= cc_r) cc_theater = r.radius;
	check(cc_plain == cc_r && std::fabs(cc_theater - cc_r * 1.2f) < 1e-3f, "theater: civic centre territory x1.2");

	// Hippocrates' Hippocratic Oath: own and allied healers +3 health per heal (0.28), anywhere.
	Simulation h;
	setup(h);
	h.set_mutual_ally(0, 2);
	const uint32_t own_healer = h.spawn_unit(-30.0f, 30.0f, 0, UNIT_HEALER);
	const uint32_t ally_healer = h.spawn_unit(30.0f, -30.0f, 2, UNIT_HEALER);
	const uint32_t foe_healer = h.spawn_unit(30.0f, 30.0f, 1, UNIT_HEALER);
	const float plain = h.heal_amount_of((size_t)idx(h, own_healer));
	h.spawn_unit(-40.0f, -40.0f, 0, UNIT_HERO, 0.0f, template_named("athen/hero_hippocrates"));
	h.rebuild_auras();
	check(std::fabs(h.heal_amount_of((size_t)idx(h, own_healer)) - plain - 3.0f) < 1e-4f, "Hippocrates: own healer +3 per heal");
	check(std::fabs(h.heal_amount_of((size_t)idx(h, ally_healer)) - plain - 3.0f) < 1e-4f, "Hippocrates: allied healer +3 per heal");
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

static void test_timing_projectiles() {
	std::printf("13. attack timing and projectiles\n");
	const CombatProfile &spear = unit_combat(CIV_UNIT_TEMPLATE_NONE, UNIT_SPEARMAN);
	const CombatProfile &arch = unit_combat(CIV_UNIT_TEMPLATE_NONE, UNIT_ARCHER);
	check(spear.prepare == 0.5f && spear.proj_speed == 0.0f, "spearman: PrepareTime 0.5 s, no projectile");
	check(arch.proj_speed == 100.0f && arch.proj_gravity == 50.0f, "archer: Projectile Speed 100, Gravity 50");

	// A spearman closes in on a dummy; time from first being in reach to the first hit.
	{
		Simulation s;
		setup(s);
		uint32_t a = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN);
		uint32_t d = dummy(s, 6.0f, 0.0f, UNIT_SWORDSMAN);
		const int ai = idx(s, a), di = idx(s, d);
		s.order_attack(&a, 1, d);
		const float hp0 = s.units_.hp[di];
		float t = 0.0f, reached = -1.0f, first = -1.0f, second = -1.0f;
		float last_hp = hp0;
		for (int k = 0; k < 400 && second < 0.0f; ++k) {
			s.tick(DT);
			t += DT;
			const float dx = s.units_.pos_x[ai] - s.units_.pos_x[di], dz = s.units_.pos_z[ai] - s.units_.pos_z[di];
			const float r = s.units_.attack_range[ai];
			if (reached < 0.0f && dx * dx + dz * dz <= r * r) reached = t;
			if (s.units_.hp[di] < last_hp) {
				if (first < 0.0f) first = t; else second = t;
				last_hp = s.units_.hp[di];
			}
		}
		std::printf("      in reach at %.2f s, hits at %.2f and %.2f s\n", reached, first, second);
		check(reached >= 0.0f && first - reached >= 0.5f - DT - 1e-3f && first - reached <= 0.5f + 2 * DT,
				"the first hit comes PrepareTime after reaching the foe");
		check(std::fabs((second - first) - s.units_.attack_cooldown[ai]) <= DT + 1e-3f, "then one every RepeatTime");
	}

	// An archer's first arrow: its speed and flight time follow the template.
	{
		Simulation s;
		setup(s);
		uint32_t a = s.spawn_unit(0.0f, 0.0f, 0, UNIT_ARCHER);
		uint32_t d = dummy(s, 14.0f, 0.0f, UNIT_SWORDSMAN);
		s.order_attack(&a, 1, d);
		const Projectile *p = nullptr;
		for (int k = 0; k < 200 && !p; ++k) {
			s.tick(DT);
			for (const auto &q : s.projectiles_) if (q.alive && q.source_id == a) { p = &q; break; }
		}
		const CombatProfile &ap = s.unit_combat_at((size_t)idx(s, a));
		std::printf("      arrow speed %.2f units/s for Speed %.0f m/s\n", p ? p->speed : 0.0f, ap.proj_speed);
		check(p && ap.proj_speed > 0.0f && std::fabs(p->speed - ap.proj_speed * COMBAT_DISTANCE_SCALE) < 1e-3f,
				"the arrow flies at its template's Projectile Speed, scaled");
		if (p) {
			const float hx = p->target_x - p->start_x, hz = p->target_z - p->start_z;
			const float tf = std::sqrt(hx * hx + hz * hz) / p->speed;
			check(std::fabs(p->arc_height - ap.proj_gravity * tf * tf / 8.0f * COMBAT_DISTANCE_SCALE) < 1e-3f, "its arc follows its Gravity");
		}
	}

	// A stray arrow: aimed at a cavalryman, it lands on a spearman instead.
	{
		Simulation s;
		setup(s);
		uint32_t a = s.spawn_unit(0.0f, 0.0f, 0, UNIT_ARCHER);
		uint32_t aimed = dummy(s, 30.0f, 30.0f, UNIT_CAVALRY, 500.0f);
		uint32_t hit = dummy(s, 12.0f, 0.0f, UNIT_SPEARMAN, 500.0f);
		s.order_set_stance(&a, 1, STANCE_STAND_GROUND);
		const int ai = idx(s, a), hi = idx(s, hit), mi = idx(s, aimed);
		const float want = s.unit_attack_damage((size_t)ai, (size_t)hi);
		const float wrong = s.unit_attack_damage((size_t)ai, (size_t)mi);
		const float hp0 = s.units_.hp[hi];
		s.fire_arrow(0.0f, 1.4f, 0.0f, 12.0f, s.get_terrain_height(12.0f, 0.0f) + 0.8f, 0.0f, wrong, 0, aimed, EFFECT_NONE, 1, false, 0, a);
		for (int k = 0; k < 40; ++k) s.tick_projectiles(DT);
		std::printf("      stray hit for %.2f (the spearman takes %.2f, the cavalryman would %.2f)\n", hp0 - s.units_.hp[hi], want, wrong);
		check(std::fabs((hp0 - s.units_.hp[hi]) - want) < 1e-3f && std::fabs(want - wrong) > 0.1f,
				"a stray deals the archer's damage against the unit it hits");
	}
}

// First hit of unit `u` on structure `bid` (hp lost), or -1.
static float first_structure_hit(Simulation &s, uint32_t u, uint32_t bid) {
	Simulation::Building *b = building(s, bid);
	const float hp0 = b->hp;
	s.order_attack_building(&u, 1, bid, false, false);
	for (int k = 0; k < 600; ++k) {
		s.tick(DT);
		b = building(s, bid);
		if (b->hp < hp0) return hp0 - b->hp;
	}
	return -1.0f;
}

static void test_damage_effects() {
	std::printf("14. attack effects: no minimum, structures, capture\n");
	{
		Simulation s;
		setup(s);
		uint32_t a = s.spawn_unit(0.0f, 0.0f, 0, UNIT_ARCHER);
		uint32_t r = dummy(s, 10.0f, 0.0f, UNIT_SIEGE_RAM);
		const float d = s.unit_attack_damage((size_t)idx(s, a), (size_t)idx(s, r));
		std::printf("      archer on ram: %.3f per arrow\n", d);
		check(d > 0.0f && d < 1.0f, "an archer's arrow does a fraction of a point to a ram (no minimum of 1)");
	}
	{
		// The same spearman twice, the second with twice its attack (as a technology
		// would give): its blow on a barracks doubles too.
		float hit[2];
		for (int k = 0; k < 2; ++k) {
			Simulation s;
			setup(s);
			uint32_t bid = s.spawn_building_world(20.0f, 0.0f, BLD_BARRACKS, 1, 1.0f);
			uint32_t u = s.spawn_unit(20.0f - building(s, bid)->half_size - 1.5f, 0.0f, 0, UNIT_SPEARMAN);
			if (k == 1) s.units_.attack_damage[idx(s, u)] *= 2.0f;
			hit[k] = first_structure_hit(s, u, bid);
		}
		std::printf("      spearman on a barracks: %.3f, with twice the attack %.3f\n", hit[0], hit[1]);
		check(hit[0] > 0.0f && std::fabs(hit[1] - 2.0f * hit[0]) < 1e-3f, "a stronger attack hits structures harder too");
	}
	{
		float pts[2];
		for (int k = 0; k < 2; ++k) {
			Simulation s;
			setup(s);
			uint32_t bid = s.spawn_building_world(20.0f, 0.0f, BLD_BARRACKS, 1, 1.0f);
			Simulation::Building *b = building(s, bid);
			if (k == 1) b->hp = b->max_hp * 0.5f;
			uint32_t u = s.spawn_unit(20.0f - b->half_size - 1.0f, 0.0f, 0, UNIT_SPEARMAN);
			s.order_attack_building(&u, 1, bid, false, true);
			// The first blow's points, less one tick of the owner's regeneration.
			pts[k] = 0.0f;
			for (int t = 0; t < 400 && pts[k] <= 0.0f; ++t) {
				s.tick(DT);
				pts[k] = building(s, bid)->capture_points_by_team[0];
			}
		}
		std::printf("      capture per blow: %.3f at full health, %.3f at half\n", pts[0], pts[1]);
		check(pts[0] > 0.0f && std::fabs(pts[1] / pts[0] - 1.0f / 0.55f) < 0.02f, "a structure at half health is captured 1/0.55 times as fast");
	}
}

// Fire one arrow from unit `src` that lands at (x, z), aimed at `target` (a structure
// when `at_building`), with a launch damage the hit must not use, and tick until it lands.
static void land_arrow(Simulation &s, uint32_t src, float x, float z, uint32_t target, bool at_building) {
	const int i = idx(s, src);
	s.fire_arrow(s.units_.pos_x[i], 2.0f, s.units_.pos_z[i], x, 0.8f, z, 999.0f, 0, target, 0, 1, at_building, 0, src);
	const uint32_t pid = s.projectiles_.back().id;
	for (int k = 0; k < 400; ++k) {
		bool flying = false;
		for (const Projectile &p : s.projectiles_) flying |= p.id == pid;
		if (!flying) return;
		s.tick(DT);
	}
}

static void test_projectile_landing() {
	std::printf("15. projectiles land on what is there (DelayedDamage.js Hit)\n");
	auto scene = [](Simulation &s, uint32_t &archer, uint32_t &bid, uint32_t &spear) {
		setup(s);
		archer = s.spawn_unit(0.0f, 0.0f, 0, UNIT_ARCHER);
		s.order_set_stance(&archer, 1, STANCE_STAND_GROUND);
		bid = s.spawn_building_world(30.0f, 0.0f, BLD_BARRACKS, 1, 1.0f);
		// Its territory: on neutral land the barracks would decay and Petra would garrison the spearman.
		s.spawn_building_world(52.0f, 0.0f, BLD_TOWN_CENTER, 1, 1.0f);
		spear = dummy(s, 30.0f, building(s, bid)->half_size + 3.0f, UNIT_SPEARMAN, 500.0f); // a float this size keeps the hit exact
		s.units_.attack_damage[idx(s, spear)] = 0.0f; // never answers
	};
	{
		// Aimed at the barracks, lands on the spearman beside it: he takes the archer's
		// attack against his own armour, the barracks nothing.
		Simulation s;
		uint32_t archer, bid, spear;
		scene(s, archer, bid, spear);
		const int sp = idx(s, spear);
		const float hp0 = s.units_.hp[sp], bhp0 = building(s, bid)->hp;
		const float want = s.unit_attack_damage((size_t)idx(s, archer), (size_t)sp);
		land_arrow(s, archer, s.units_.pos_x[sp], s.units_.pos_z[sp], bid, true);
		std::printf("      arrow at the barracks lands on a spearman: -%.3f (attack %.3f), barracks -%.3f\n",
				hp0 - s.units_.hp[sp], want, bhp0 - building(s, bid)->hp);
		check(std::fabs(hp0 - s.units_.hp[sp] - want) < 1e-3f && building(s, bid)->hp == bhp0,
				"a stray from a structure attack hits the unit it lands on");
	}
	{
		// Aimed at the spearman, lands inside the barracks: the barracks takes the
		// archer's attack against its own armour.
		Simulation s;
		uint32_t archer, bid, spear;
		scene(s, archer, bid, spear);
		Simulation::Building *b = building(s, bid);
		const float bhp0 = b->hp, want = s.unit_damage_to_structure((size_t)idx(s, archer), *b);
		const float hp0 = s.units_.hp[idx(s, spear)];
		land_arrow(s, archer, b->x, b->z, spear, false);
		std::printf("      arrow at the spearman lands in the barracks: -%.3f (attack %.3f)\n", bhp0 - building(s, bid)->hp, want);
		check(std::fabs(bhp0 - building(s, bid)->hp - want) < 1e-3f && s.units_.hp[idx(s, spear)] == hp0,
				"a stray hits the enemy structure it lands in");
	}
	{
		// Lands on open ground beyond both: nothing is hurt.
		Simulation s;
		uint32_t archer, bid, spear;
		scene(s, archer, bid, spear);
		const float bhp0 = building(s, bid)->hp, hp0 = s.units_.hp[idx(s, spear)];
		land_arrow(s, archer, 30.0f, -building(s, bid)->half_size - 6.0f, bid, true);
		check(building(s, bid)->hp == bhp0 && s.units_.hp[idx(s, spear)] == hp0, "an arrow that lands on nothing misses");
	}
	{
		// An archer shooting a far structure scatters by its Spread like against units.
		Simulation s;
		uint32_t archer, bid, spear;
		scene(s, archer, bid, spear);
		s.units_.pos_z[idx(s, spear)] = 60.0f; // out of the way
		Simulation::Building *b = building(s, bid);
		s.units_.pos_x[idx(s, archer)] = b->x - b->half_size - 15.0f;
		s.order_attack_building(&archer, 1, bid, false, false);
		std::set<uint32_t> seen;
		float min_x = 1e9f, max_x = -1e9f, min_z = 1e9f, max_z = -1e9f;
		for (int k = 0; k < 1500; ++k) {
			s.tick(DT);
			for (const Projectile &p : s.projectiles_) {
				if (p.source_id != archer || !seen.insert(p.id).second) continue;
				min_x = std::min(min_x, p.target_x); max_x = std::max(max_x, p.target_x);
				min_z = std::min(min_z, p.target_z); max_z = std::max(max_z, p.target_z);
			}
		}
		std::printf("      %zu arrows at the barracks land over %.2f x %.2f\n", seen.size(), max_x - min_x, max_z - min_z);
		check(seen.size() >= 5 && max_x - min_x > 0.05f && max_z - min_z > 0.05f, "arrows at a structure scatter by Spread");
	}
}

static void test_ranged_hunting() {
	std::printf("16. ranged hunters shoot projectiles at animals\n");
	Simulation s;
	setup(s);
	uint32_t archer = s.spawn_unit(-40.0f, -40.0f, 0, UNIT_ARCHER);
	uint32_t deer = s.spawn_fauna(-28.0f, -40.0f, FAUNA_DEER);
	const uint16_t xp0 = s.units_.xp[idx(s, archer)];
	s.order_hunt(&archer, 1, deer);
	std::set<uint32_t> shots;
	bool dead = false;
	for (int k = 0; k < 1200 && !dead; ++k) {
		s.tick(DT);
		for (const Projectile &p : s.projectiles_)
			if (p.source_id == archer && p.target_unit_id == deer) shots.insert(p.id);
		const auto it = s.fauna_id_to_idx_.find(deer);
		dead = it == s.fauna_id_to_idx_.end() || !s.fauna_.alive[it->second];
	}
	for (int k = 0; k < 50; ++k) s.tick(DT);
	const int a = idx(s, archer);
	const uint8_t st = s.units_.action_state[a];
	std::printf("      %zu shots, deer dead %d, xp %d -> %d, archer state %d\n", shots.size(), (int)dead, xp0, s.units_.xp[a], st);
	check(!shots.empty(), "an archer hunting a deer shoots arrows at it");
	check(dead && std::fabs(s.units_.xp[a] + s.units_.xp_partial[a] - (xp0 + fauna_type_info(FAUNA_DEER).loot_xp)) < 0.01f,
			"the arrows that kill it give the archer its Loot xp, a share per hit");
	check(st == ACTION_GATHER || st == ACTION_MOVE || st == ACTION_RETURN_DROPSITE, "the archer goes on to gather the carcass");
}

static void test_status_credit() {
	std::printf("17. status effects credit whoever applied them (StatusEffectsReceiver)\n");
	Simulation s;
	setup(s);
	const uint16_t maiden = template_named("maur/champion_maiden_archer");
	const CombatProfile &mp = unit_combat(maiden, UNIT_ARCHER_MAIDEN);
	uint32_t archer = s.spawn_unit(-40.0f, 0.0f, 0, UNIT_ARCHER);
	s.order_set_stance(&archer, 1, STANCE_STAND_GROUND);
	uint32_t v = dummy(s, 40.0f, 0.0f, UNIT_SWORDSMAN, 2.0f);
	const uint16_t xp0 = s.units_.xp[idx(s, archer)];
	const uint32_t kills0 = s.stats_[0].enemy_units_killed[STAT_UNIT_TOTAL];
	s.apply_status((size_t)idx(s, v), mp, archer, 0);
	const int vi = idx(s, v);
	for (int k = 0; k < 2000 && s.units_.alive[vi]; ++k) s.tick(DT);
	std::printf("      poisoned swordsman dead %d; archer xp %d -> %d; player 0 kills %u -> %u\n", (int)!s.units_.alive[vi],
			xp0, s.units_.xp[idx(s, archer)], kills0, s.stats_[0].enemy_units_killed[STAT_UNIT_TOTAL]);
	check(!s.units_.alive[vi], "poison kills the swordsman");
	// helpers/Attack.js HandleAttackEffects: a StatusEffect's damage gives no xp.
	check(s.units_.xp[idx(s, archer)] == xp0 && s.units_.xp_partial[idx(s, archer)] == 0.0f, "poison damage gives the poisoner no xp");
	check(s.stats_[0].enemy_units_killed[STAT_UNIT_TOTAL] == kills0 + 1, "the kill counts for the poisoner's player");

	// Saved and loaded, a status keeps its source.
	Simulation a;
	setup(a);
	uint32_t a_arch = a.spawn_unit(-40.0f, 0.0f, 0, UNIT_ARCHER);
	uint32_t a_v = dummy(a, 40.0f, 0.0f, UNIT_SWORDSMAN, 2.0f);
	a.apply_status((size_t)idx(a, a_v), mp, a_arch, 0);
	const char *path = "/tmp/rts0_test_status_credit.sav";
	check(a.save_state(path), "save");
	Simulation b;
	check(b.load_state(path), "load");
	std::remove(path);
	const int bv = idx(b, a_v);
	check(bv >= 0 && b.statuses_.size() == 1 && b.statuses_[0].target == a_v && b.statuses_[0].src == a_arch && b.statuses_[0].src_owner == 0,
			"the poison's source survives save and load");
}

static void test_reach_and_responses() {
	std::printf("20. CanAttack heights\n");
	// Attack.js CanAttack: a swordsman can't reach an archer on a wall's turret point (the
	// height offset is more than its MaxRange), so it never picks that archer, even when shot.
	{
		Simulation s;
		setup(s);
		const uint32_t wall = s.spawn_building_world(-40.0f, -40.0f, BLD_WALL_SEGMENT, 0, 1.0f);
		const uint32_t archer = s.spawn_unit(-40.0f, -37.0f, 0, UNIT_ARCHER);
		s.order_garrison(&archer, 1, wall);
		for (int k = 0; k < 200; ++k) s.tick(DT);
		check(s.turret_holder_of(archer) == wall, "the archer stands on the wall");
		const uint32_t sw = s.spawn_unit(-40.0f, -34.0f, 1, UNIT_SWORDSMAN, 1.0e6f);
		bool went_for_archer = false;
		for (int k = 0; k < 200; ++k) {
			s.tick(DT);
			if (s.units_.target_unit_id[idx(s, sw)] == archer) went_for_archer = true;
		}
		check(!s.unit_can_target((size_t)idx(s, sw), (size_t)idx(s, archer)), "the swordsman can't target the archer on the wall");
		check(!went_for_archer, "the swordsman never goes for the archer, though it is shot at");
	}
}

static void test_attack_rate_and_invulnerable() {
	std::printf("21. attack-rate auras and invulnerable targets\n");
	// Cleopatra's Patriot multiplies Attack/*/RepeatTime by 0.8 (not damage by 1.25).
	{
		Simulation s;
		setup(s);
		const uint16_t cleo = template_named("ptol/hero_cleopatra_vii");
		const uint16_t pike = template_named("ptol/infantry_pikeman_b");
		check(cleo != CIV_UNIT_TEMPLATE_NONE && pike != CIV_UNIT_TEMPLATE_NONE, "roster has Cleopatra and a pikeman");
		s.spawn_unit(0.0f, 0.0f, 0, UNIT_HERO, 0.0f, cleo);
		const uint32_t near_u = s.spawn_unit(3.0f, 0.0f, 0, UNIT_SPEARMAN, 0.0f, pike);
		const uint32_t far_u = s.spawn_unit(40.0f, 0.0f, 0, UNIT_SPEARMAN, 0.0f, pike);
		s.rebuild_auras();
		const int n = idx(s, near_u), f = idx(s, far_u);
		check(std::fabs(s.unit_repeat_time(n) - 0.8f * s.units_.attack_cooldown[n]) < 1e-4f, "a pikeman near her attacks 20% sooner");
		check(s.unit_repeat_time(f) == s.units_.attack_cooldown[f], "one out of her range doesn't");
		check(s.aura_mgr_.query_modifier(s.units_.pos_x[n], s.units_.pos_z[n], 0, AURA_ATTACK, s.units_.tmpl[n],
				&s.unit_combat_at(n).classes) == 0.0f, "and hits no harder");
	}
	// helpers/Attack.js HandleAttackEffects: an invulnerable target takes no damage, gives
	// no xp, sends no Attacked message and takes no status.
	{
		Simulation s;
		setup(s);
		uint32_t sw = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SWORDSMAN);
		const uint32_t v = dummy(s, 1.5f, 0.0f, UNIT_SWORDSMAN, 100.0f);
		s.triggers_.invulnerable.push_back(v);
		const float hp0 = s.units_.hp[idx(s, v)];
		s.order_attack(&sw, 1, v);
		for (int k = 0; k < 200; ++k) s.tick(DT);
		const int a = idx(s, sw), t = idx(s, v);
		check(s.units_.hp[t] == hp0, "the invulnerable unit takes no damage");
		check(s.units_.xp[a] == 0 && s.units_.xp_partial[a] == 0.0f, "its attacker gains no xp");
		check(s.units_.attacked_by[t] == 0, "and it isn't told it was attacked");
		const CombatProfile &mp = unit_combat(template_named("maur/champion_maiden_archer"), UNIT_ARCHER_MAIDEN);
		s.apply_status((size_t)t, mp, sw, 0);
		check(s.units_.status_effects[t] == 0, "nor poisoned");
	}
}

static void test_germanic_auras() {
	std::printf("18. Germanic hero and champion auras\n");
	Simulation s;
	setup(s);
	const uint16_t boiorix = template_named("germ/hero_boiorix"), lugius = template_named("germ/hero_lugius");
	const uint16_t seeress = template_named("germ/champion_healer");
	const uint16_t slinger = template_named("germ/infantry_slinger_b"), sword = template_named("germ/infantry_swordsman_b");
	check(boiorix != CIV_UNIT_TEMPLATE_NONE && lugius != CIV_UNIT_TEMPLATE_NONE && seeress != CIV_UNIT_TEMPLATE_NONE,
		"roster has Boiorix, Lugius and the Seeress");
	s.spawn_unit(-20.0f, 0.0f, 0, UNIT_HERO, 0.0f, boiorix);
	uint32_t sl = s.spawn_unit(-18.0f, 0.0f, 0, UNIT_ARCHER, 0.0f, slinger);
	uint32_t sw = s.spawn_unit(-18.0f, 2.0f, 0, UNIT_SWORDSMAN, 0.0f, sword);
	uint32_t far_sl = s.spawn_unit(-20.0f, 30.0f, 0, UNIT_ARCHER, 0.0f, slinger);
	uint32_t foe = s.spawn_unit(-22.0f, 0.0f, 1, UNIT_ARCHER, 0.0f, slinger);
	s.rebuild_auras();
	auto reach = [&](uint32_t id) { const int i = idx(s, id); return s.attack_reach((size_t)i) - s.units_.attack_range[i]; };
	check(std::fabs(reach(sl) - 5.0f * COMBAT_DISTANCE_SCALE) < 1e-4f, "Boiorix: own slinger nearby +5 m range");
	check(std::fabs(reach(sw) - 2.0f * 0.6f) < 1e-4f, "Boiorix: own swordsman nearby +2 m melee range");
	check(reach(far_sl) == 0.0f, "Boiorix: own slinger beyond 60 m unchanged");
	check(reach(foe) == 0.0f, "Boiorix: enemy slinger unchanged");

	Simulation v;
	setup(v);
	v.spawn_unit(20.0f, 0.0f, 0, UNIT_HERO, 0.0f, lugius);
	uint32_t enemy = v.spawn_unit(30.0f, 0.0f, 1, UNIT_SWORDSMAN);
	uint32_t own = v.spawn_unit(25.0f, 0.0f, 0, UNIT_SWORDSMAN);
	uint32_t tower = v.spawn_building_world(30.0f, 10.0f, BLD_TOWER, 1, 1.0f);
	const float tower_vis = v.building_vision_range(*building(v, tower));
	v.rebuild_auras();
	check(std::fabs(v.unit_vision_at(idx(v, enemy)) - std::max(0.0f, v.unit_base_vision(idx(v, enemy)) - 20.0f * COMBAT_DISTANCE_SCALE)) < 1e-3f,
		"Lugius: enemy soldier -20 m vision");
	check(std::fabs(v.unit_vision_at(idx(v, own)) - v.unit_base_vision(idx(v, own))) < 1e-3f, "Lugius: own soldier unchanged");
	check(std::fabs(v.building_vision_range(*building(v, tower)) - std::max(0.0f, tower_vis - 20.0f * COMBAT_DISTANCE_SCALE)) < 1e-3f,
		"Lugius: enemy tower -20 m vision");

	Simulation h;
	setup(h);
	h.players_[1].active = 1; // a human seat: Petra would send the dummy to build
	h.players_[1].is_ai = 0;
	uint32_t seer = h.spawn_unit(0.0f, -20.0f, 0, UNIT_HEALER, 0.0f, seeress);
	h.order_set_stance(&seer, 1, STANCE_STAND_GROUND);
	uint32_t victim = dummy(h, 3.5f, -20.0f, UNIT_SWORDSMAN, 30.0f); // beyond its own reach
	uint32_t friend_u = h.spawn_unit(-2.0f, -20.0f, 0, UNIT_SWORDSMAN, 30.0f);
	h.order_set_stance(&friend_u, 1, STANCE_STAND_GROUND);
	h.rebuild_auras();
	const float hp0 = h.units_.hp[idx(h, victim)];
	for (int k = 0; k < 100; ++k) h.tick(DT);
	const int vi = idx(h, victim);
	check(vi >= 0 && std::fabs(hp0 - h.units_.hp[vi] - 10.0f) < 0.2f, "Seeress: enemy at full health loses 2 hp/s");
	check(h.units_.hp[idx(h, friend_u)] == 30.0f, "Seeress: own soldier not drained");
	// Pinned in place (a wounded soldier walks off).
	h.units_.hp[vi] = 3.0f;
	for (int k = 0; k < 40; ++k) {
		const int a = idx(h, victim);
		if (a < 0 || !h.units_.alive[a]) break;
		h.units_.pos_x[a] = 3.5f; h.units_.pos_z[a] = -20.0f;
		h.tick(DT);
	}
	const int vd = idx(h, victim);
	check(vd < 0 || !h.units_.alive[vd], "Seeress: the drain kills");
}

static void test_aura_stacking() {
	std::printf("19. aura stacking\n");
	Simulation s;
	setup(s);
	s.set_team_civ(0, CIV_KUSHITES);
	const uint16_t spear = template_named("kush/infantry_spearman_b");
	const uint16_t hannibal = template_named("hero_hannibal");
	const uint16_t marcellus = template_named("hero_marcellus");
	check(spear != CIV_UNIT_TEMPLATE_NONE && hannibal != CIV_UNIT_TEMPLATE_NONE && marcellus != CIV_UNIT_TEMPLATE_NONE,
		"roster has a Kushite spearman, Hannibal and Marcellus");
	s.spawn_building_world(-20.0f, 20.0f, BLD_PYRAMID_LARGE, 0, 1.0f);
	s.spawn_building_world(-26.0f, 20.0f, BLD_PYRAMID_LARGE, 0, 1.0f);
	const uint32_t own = s.spawn_unit(-14.0f, 20.0f, 0, UNIT_SPEARMAN, 0.0f, spear);
	s.rebuild_auras();
	auto mod = [&](Simulation &m, uint32_t id, AuraType t) {
		int i = idx(m, id);
		return m.aura_mgr_.query_modifier(m.units_.pos_x[i], m.units_.pos_z[i], m.units_.team[i], t,
			m.units_.tmpl[i], &m.unit_combat_at(i).classes);
	};
	check(std::fabs(mod(s, own, AURA_ATTACK) - 1.1f) < 1e-4f, "two large pyramids: +10% attack once");
	check(std::fabs(mod(s, own, AURA_ARMOR) - 1.0f) < 1e-4f, "two large pyramids: +1 resistance once");
	s.spawn_unit(-14.0f, 22.0f, 0, UNIT_HERO, 0.0f, hannibal);
	s.rebuild_auras();
	check(std::fabs(mod(s, own, AURA_ATTACK) - 1.1f * 1.2f) < 1e-4f, "large pyramid and Hannibal stack: x1.1 x1.2");

	Simulation r;
	setup(r);
	r.spawn_unit(0.0f, 0.0f, 1, UNIT_HERO, 0.0f, marcellus);
	r.spawn_unit(0.0f, 4.0f, 1, UNIT_HERO, 0.0f, marcellus);
	const uint32_t foe = r.spawn_unit(3.0f, 0.0f, 0, UNIT_SWORDSMAN);
	r.rebuild_auras();
	check(std::fabs(mod(r, foe, AURA_ATTACK) - 0.9f) < 1e-4f, "two Marcellus: enemy infantry -10% attack once");

	// Battering Team: +0.15 m/s per Infantry unit garrisoned, owner's or a mutual ally's.
	Simulation g;
	setup(g);
	g.set_mutual_ally(0, 2);
	const uint32_t ram = g.spawn_unit(0.0f, 0.0f, 0, UNIT_SIEGE_RAM);
	const float empty = g.unit_walk_speed((size_t)idx(g, ram));
	std::vector<uint32_t> crew;
	crew.push_back(g.spawn_unit(0.5f, 0.5f, 0, UNIT_SWORDSMAN));
	crew.push_back(g.spawn_unit(0.5f, -0.5f, 0, UNIT_SPEARMAN));
	g.order_board_transport(crew.data(), crew.size(), ram);
	const uint32_t friend_ = g.spawn_unit(-0.5f, 0.5f, 2, UNIT_SWORDSMAN);
	g.order_board_transport(&friend_, 1, ram);
	const size_t aboard = g.get_transport_passenger_count(ram);
	check(aboard >= 2, "infantry board the ram");
	const float full = g.unit_walk_speed((size_t)idx(g, ram));
	check(std::fabs(full - empty - 0.15f * FORMATION_METRE * (float)aboard) < 1e-5f,
		"Battering Team: the ram walks 0.15 m/s faster per infantryman inside");
}

static void test_razed_target() {
	std::printf("22. attackers let go of a razed structure\n");
	// 0 A.D. destroys a razed structure, so its other attackers find their target gone;
	// RTS0 keeps a wreck for the rubble, and it must not hold them.
	auto raze = [](int melee, bool archer_too, float &loot) {
		Simulation s;
		setup(s);
		const uint32_t bid = s.spawn_building_world(30.0f, 0.0f, BLD_HOUSE, 1, 1.0f);
		s.spawn_building_world(52.0f, 0.0f, BLD_TOWN_CENTER, 1, 1.0f);
		std::vector<uint32_t> ids;
		Simulation::Building *b = building(s, bid);
		for (int k = 0; k < melee; ++k) ids.push_back(s.spawn_unit(b->x - b->half_size - 0.8f, -1.0f + (float)k, 0, UNIT_SPEARMAN));
		if (archer_too) ids.push_back(s.spawn_unit(0.0f, 0.0f, 0, UNIT_ARCHER));
		s.order_attack_building(ids.data(), ids.size(), bid, false, false);
		const float res0 = s.players_[0].res[0] + s.players_[0].res[1] + s.players_[0].res[2] + s.players_[0].res[3];
		for (int k = 0; k < 40; ++k) s.tick(DT); // swings under way
		building(s, bid)->hp = 0.5f;
		for (int k = 0; k < 200; ++k) s.tick(DT);
		loot = s.players_[0].res[0] + s.players_[0].res[1] + s.players_[0].res[2] + s.players_[0].res[3] - res0;
		int holding = 0;
		for (uint32_t id : ids) {
			const int u = idx(s, id);
			if (u >= 0 && s.units_.target_building_id[u] == bid) ++holding;
		}
		std::printf("      %d spearmen%s: house wrecked %d, %d still aim at it, loot %.0f\n", melee, archer_too ? " + archer" : "",
				(int)building(s, bid)->wrecked, holding, loot);
		return building(s, bid)->wrecked && holding == 0;
	};
	float one = 0.0f, three = 0.0f, ranged = 0.0f;
	check(raze(1, false, one), "a lone spearman stops at the rubble");
	check(raze(3, false, three) && std::fabs(three - one) < 0.5f, "three spearmen loot it once and all stop at the rubble");
	check(raze(0, true, ranged), "an archer stops shooting at the rubble");
}

static void test_uphill_archer() {
	std::printf("23. a ranged attacker walks into its elevation-adapted range\n");
	// UnitAI MoveToTargetAttackRange: below its target an archer's parabolic range is shorter
	// than its MaxRange, so it walks closer before it stops, or it would stand there unable to shoot.
	auto shoot = [](float hill, float &stood, float &taken) {
		Simulation s;
		setup(s);
		s.set_player_active(1, true, false); // no Petra: its defense would send the target at the archer
		if (hill > 0.0f) { // a rise from x = 8 to a plateau at x = 14 (the nav grid is left as it was)
			s.terrain_res_ = 257;
			s.terrain_heights_.assign((size_t)s.terrain_res_ * s.terrain_res_, 0.0f);
			for (int z = 0; z < s.terrain_res_; ++z)
				for (int x = 0; x < s.terrain_res_; ++x) {
					const float wx = -s.map_half_ + 2.0f * s.map_half_ * (float)x / (float)(s.terrain_res_ - 1);
					s.terrain_heights_[(size_t)z * s.terrain_res_ + x] = hill * std::clamp((wx - 8.0f) / 6.0f, 0.0f, 1.0f);
				}
		}
		const uint32_t archer = s.spawn_unit(0.0f, 0.0f, 0, UNIT_ARCHER);
		const uint32_t mark = dummy(s, 18.0f, 0.0f, UNIT_SPEARMAN); // in sight, out of reach
		const float hp0 = s.units_.hp[idx(s, mark)];
		s.order_attack(&archer, 1, mark);
		for (int k = 0; k < 400; ++k) s.tick(DT);
		const int a = idx(s, archer), m = idx(s, mark);
		stood = std::hypot(s.units_.pos_x[m] - s.units_.pos_x[a], s.units_.pos_z[m] - s.units_.pos_z[a]);
		taken = hp0 - s.units_.hp[m];
		std::printf("      target %.1f up (archer at %.1f): it stops %.2f away (reach %.2f), deals %.0f\n",
				s.unit_height((size_t)m) - s.unit_height((size_t)a), s.unit_height((size_t)a), stood, s.attack_reach((size_t)a), taken);
	};
	float flat_d, flat_hp, up_d, up_hp;
	shoot(0.0f, flat_d, flat_hp);
	shoot(3.0f, up_d, up_hp);
	check(flat_hp > 0.0f, "on level ground the archer shoots its target");
	check(up_d < flat_d - 1.0f && up_hp > 0.0f, "below its target it walks closer and shoots it");
	{
		// Likewise a structure up on the rise.
		Simulation s;
		setup(s);
		s.set_player_active(1, true, false);
		s.terrain_res_ = 257;
		s.terrain_heights_.assign((size_t)s.terrain_res_ * s.terrain_res_, 0.0f);
		for (int z = 0; z < s.terrain_res_; ++z)
			for (int x = 0; x < s.terrain_res_; ++x) {
				const float wx = -s.map_half_ + 2.0f * s.map_half_ * (float)x / (float)(s.terrain_res_ - 1);
				s.terrain_heights_[(size_t)z * s.terrain_res_ + x] = 4.0f * std::clamp((wx - 8.0f) / 6.0f, 0.0f, 1.0f);
			}
		const uint32_t bid = s.spawn_building_world(24.0f, 0.0f, BLD_HOUSE, 1, 1.0f);
		s.spawn_building_world(52.0f, 0.0f, BLD_TOWN_CENTER, 1, 1.0f);
		const uint32_t archer = s.spawn_unit(0.0f, 0.0f, 0, UNIT_ARCHER);
		s.set_reveal_map(true);
		const float hp0 = building(s, bid)->hp;
		s.order_attack_building(&archer, 1, bid, false, false);
		for (int k = 0; k < 600; ++k) s.tick(DT);
		const int a = idx(s, archer);
		std::printf("      house 4 up: archer at x %.2f, house -%.0f\n", s.units_.pos_x[a], hp0 - building(s, bid)->hp);
		check(building(s, bid)->hp < hp0, "below a structure it walks closer and shoots it");
	}
}

// StatusEffectsReceiver keeps its timers while the entity is out of the world: a burning or
// poisoned unit that garrisons or boards keeps taking damage, and one that dies inside leaves
// its holder (GarrisonHolder OnGlobalOwnershipChanged) with no corpse.
static void test_status_inside() {
	std::printf("24. status effects tick inside garrisons and ships\n");
	Simulation s;
	setup(s);
	const uint16_t iber_cav = template_named("iber/champion_cavalry");
	const CombatProfile &cp = unit_combat(iber_cav, UNIT_CAVALRY); // Burning 2 per 3 s for 9 s
	const uint32_t house = s.spawn_building_world(-20.0f, -20.0f, BLD_HOUSE, 0, 1.0f);
	uint32_t in_house = s.spawn_unit(-24.0f, -20.0f, 0, UNIT_SWORDSMAN);
	s.apply_status((size_t)idx(s, in_house), cp, 0, 1);
	s.garrison_now((size_t)idx(s, in_house), *building(s, house));
	s.units_.hp[idx(s, in_house)] = 3.0f; // dies on the second tick, 3 s in
	const uint32_t ship = s.spawn_unit(30.0f, -30.0f, 0, UNIT_TRIREME);
	const uint32_t aboard = s.spawn_unit(30.0f, -30.0f, 0, UNIT_SWORDSMAN);
	const uint32_t sick = s.spawn_unit(30.0f, -30.0f, 0, UNIT_SWORDSMAN);
	for (uint32_t p : {aboard, sick}) {
		const int i = idx(s, p);
		s.apply_status((size_t)i, cp, 0, 1);
		s.units_.action_state[i] = ACTION_GARRISONED;
		s.units_.alive[i] = 0;
		s.transport_passengers_[ship].push_back(p);
	}
	s.units_.hp[idx(s, aboard)] = 3.0f;
	const float h_sick = s.units_.hp[idx(s, sick)];
	for (int k = 0; k < 2; ++k) s.tick(DT);
	check(s.units_.hp[idx(s, in_house)] < 3.0f && (s.units_.status_effects[idx(s, in_house)] & EFFECT_BURNING),
			"a burning unit that garrisons keeps burning inside");
	for (int k = 0; k < (int)(4.0f / DT); ++k) s.tick(DT);
	check(idx(s, in_house) < 0 && building(s, house)->garrisoned_units.empty(), "it dies inside and leaves the house");
	check(idx(s, aboard) < 0, "a burning passenger dies aboard");
	const auto pit = s.transport_passengers_.find(ship);
	check(pit != s.transport_passengers_.end() && pit->second.size() == 1 && pit->second[0] == sick,
			"and leaves the ship, the other passenger stays aboard");
	for (int k = 0; k < (int)(8.0f / DT); ++k) s.tick(DT);
	check(std::fabs((h_sick - s.units_.hp[idx(s, sick)]) - 8.0f) < 1e-3f && s.units_.status_effects[idx(s, sick)] == 0,
			"a passenger takes the whole burn aboard: 4 ticks of 2");
}

// 25. BuildingAI range: IsInTargetParabolicRange measures from the structure's square
//     obstruction, so a fortress reaches as far past its corner as past a side.
static bool fortress_aims_at(float gx, float gz) {
	Simulation s;
	setup(s);
	const uint32_t fort = s.spawn_building_world(0.0f, 0.0f, BLD_FORTRESS, 0, 1.0f);
	Simulation::Building *b = building(s, fort);
	b->set_rot(0.0f);
	// gx, gz: the gap from the footprint's +x and +z edges.
	dummy(s, b->x + b->hu + gx, b->z + b->hv + gz, UNIT_SPEARMAN);
	s.tick(DT);
	return building(s, fort)->arrow_timer_on;
}
static void test_arrow_square_range() {
	std::printf("25. arrow range from the square footprint\n");
	// The farthest gap past a side at which the fortress aims (the unit stands mid-side: gz is
	// minus the half depth, so it is level with the centre).
	Simulation probe;
	setup(probe);
	const uint32_t fid = probe.spawn_building_world(0.0f, 0.0f, BLD_FORTRESS, 0, 1.0f);
	building(probe, fid)->set_rot(0.0f);
	const float hv = building(probe, fid)->hv;
	float lo = 0.0f, hi = 60.0f;
	for (int k = 0; k < 14; ++k) {
		const float mid = 0.5f * (lo + hi);
		(fortress_aims_at(mid, -hv) ? lo : hi) = mid;
	}
	const float side = lo, diag = 0.97f * side / std::sqrt(2.0f);
	std::printf("      side reach %.2f, corner gap %.2f along each axis\n", side, diag);
	check(side > 1.0f && fortress_aims_at(diag, diag), "a fortress aims at a unit just inside its reach past a corner");
}

static void test_hill_tower_reach() {
	std::printf("26b. a tower on a hill reaches units past its flat range\n");
	// BuildingAI.js CreateActiveParabolicQuery has no flat cap: a defence tower 10 units up
	// reaches sqrt(R^2 + 2R(Origin + 10)) past its edge, beyond its flat R + Origin reach.
	auto shoots_at = [](float H, float gap) {
		Simulation s;
		setup(s);
		s.set_reveal_map(true);
		const int res = 257;
		const float mh = s.map_half(), cell = 2.0f * mh / (res - 1);
		std::vector<float> hm(res * res, 0.0f);
		for (int j = 0; j < res; ++j)
			for (int i = 0; i < res; ++i)
				if (std::fabs(-mh + i * cell + 40.0f) <= 4.0f && std::fabs(-mh + j * cell + 40.0f) <= 4.0f) hm[j * res + i] = H;
		s.set_terrain_data(hm, res, -10.0f);
		const uint32_t t = s.spawn_building_world(-40.0f, -40.0f, BLD_TOWER, 0, 1.0f);
		const float half = building(s, t)->half_size;
		uint32_t f = s.spawn_unit(-40.0f + half + gap, -40.0f, 1, UNIT_SPEARMAN, 0.0f);
		s.order_set_stance(&f, 1, STANCE_STAND_GROUND);
		const float x = -40.0f + half + gap + s.unit_clearance_at((size_t)idx(s, f));
		for (int k = 0; k < 40; ++k) {
			const int32_t i = idx(s, f);
			s.units_.hp[i] = s.units_.max_hp[i];
			s.units_.pos_x[i] = x;
			s.units_.pos_z[i] = -40.0f;
			s.tick(DT);
			for (const auto &p : s.projectiles()) if (p.source_id == t) return true;
		}
		return false;
	};
	const bool flat = shoots_at(0.0f, 26.0f), hill = shoots_at(10.0f, 26.0f);
	std::printf("      foe 26 units off: flat tower %s, hill tower %s\n", flat ? "shoots" : "holds", hill ? "shoots" : "holds");
	check(!flat && hill, "a tower 10 units up shoots a unit 26 units off its edge; on flat ground it can't");
}

static void test_aura_vs_animals() {
	std::printf("26. attack auras count against animals too\n");
	Simulation s;
	setup(s);
	const uint16_t hannibal = template_named("hero_hannibal");
	const uint16_t cart_spear = template_named("cart/infantry_spearman_b");
	const uint32_t spear = s.spawn_unit(2.0f, 0.0f, 0, UNIT_SPEARMAN, 0.0f, cart_spear);
	const uint32_t boar = s.spawn_fauna(4.0f, 0.0f, FAUNA_BOAR);
	const size_t f = s.fauna_id_to_idx_.at(boar);
	s.rebuild_auras();
	const float plain = s.unit_damage_to_fauna((size_t)idx(s, spear), f);
	s.spawn_unit(0.0f, 0.0f, 0, UNIT_HERO, 0.0f, hannibal);
	s.rebuild_auras();
	const float led = s.unit_damage_to_fauna((size_t)idx(s, spear), f);
	std::printf("      spearman vs boar: %.2f alone, %.2f by Hannibal\n", plain, led);
	check(plain > 0.0f && std::fabs(led - plain * 1.2f) < 1e-3f, "Hannibal's +20% attack applies to a spearman's hit on a boar");
}


// P14: 0.28 Health.js/Attack damage-at-impact semantics (Attack.js attackData at fire,
// helpers/Attack.js GetTotalAttackEffects at impact, DelayedDamage.js Hit fall-through,
// targetVelocity from the previous turn's position).

// An arrow's damage is frozen at fire: an attack aura on the shooter at fire counts,
// one that leaves before impact does not add more, and researching nothing mid-flight
// changes nothing either way.
static void test_p14_frozen_damage() {
	std::printf("27. arrows keep their fire-time strengths to impact (P14)\n");
	Simulation s;
	setup(s);
	s.set_player_active(1, true, false);
	const uint16_t hannibal = template_named("hero_hannibal");
	uint32_t ar = s.spawn_unit(0.0f, 0.0f, 0, UNIT_ARCHER);
	uint32_t han = s.spawn_unit(2.0f, 0.0f, 0, UNIT_HERO, 0.0f, hannibal);
	uint32_t d = dummy(s, 20.0f, 0.0f, UNIT_SPEARMAN, 500.0f);
	const int ai = idx(s, ar), di = idx(s, d);
	s.units_.attack_damage[di] = 0.0f; // never answers
	s.units_.attack_range[ai] = std::max(s.units_.attack_range[ai], 21.0f);
	s.order_attack(&ar, 1, d);
	// The first arrow in flight, with Hannibal's +20% in its frozen strengths.
	float parts[4] = {};
	bool flying = false;
	for (int k = 0; k < 400 && !flying; ++k) {
		s.tick(DT);
		for (const Projectile &q : s.projectiles_)
			if (q.alive && q.source_id == ar) {
				parts[0] = q.dmg_parts[0]; parts[1] = q.dmg_parts[1];
				parts[2] = q.dmg_parts[2]; parts[3] = q.dmg_parts[3];
				check(q.src_kind == 1 && q.src_tmpl == s.units_.tmpl[ai], "the arrow records its shooter");
				flying = true;
			}
	}
	check(flying, "the archer looses an arrow");
	check(parts[1] > 0.0f, "the arrow carries pierce from the fire-time attack");
	// Hannibal marches off before it lands: under 0.28 the arrow keeps his aura.
	const int hi = idx(s, han);
	s.units_.pos_x[hi] = 60.0f;
	s.units_.pos_z[hi] = 60.0f;
	const float hp0 = s.units_.hp[di];
	for (int k = 0; k < 400 && s.units_.hp[di] == hp0; ++k) s.tick(DT);
	const float drop = hp0 - s.units_.hp[di];
	float res[3];
	s.unit_resistance((size_t)di, res);
	const float bonus = combat_bonus(unit_combat(s.units_.tmpl[ai], s.units_.unit_type[ai]),
			s.unit_combat_at((size_t)di), s.civ_of(1));
	const float want = bonus * (parts[0] * powf(0.9f, res[0]) + parts[1] * powf(0.9f, res[1]) +
			parts[2] * powf(0.9f, res[2]) + parts[3]);
	std::printf("      arrow lands for %.3f, fire-time strengths say %.3f\n", drop, want);
	check(drop > 0.0f && std::fabs(drop - want) < 1e-3f, "impact meets fire-time strengths with the victim's armour");
}

// A stray arrow of a dead shooter meets the animal or unit it lands on, not the aimed
// target's armour: aimed at a ram, killing the archer mid-flight, it still guts the
// swordsman it lands on.
static void test_p14_stray_armour() {
	std::printf("28. a dead shooter's stray uses its victim's armour (P14)\n");
	Simulation s;
	setup(s);
	s.set_player_active(1, true, false);
	uint32_t ar = s.spawn_unit(0.0f, 0.0f, 0, UNIT_ARCHER);
	uint32_t ram = dummy(s, 20.0f, 0.0f, UNIT_SIEGE_RAM, 500.0f);
	uint32_t sw = dummy(s, 23.5f, 0.0f, UNIT_SWORDSMAN, 500.0f);
	const int ai = idx(s, ar), si = idx(s, sw), ri = idx(s, ram);
	s.units_.attack_damage[si] = 0.0f;
	s.units_.attack_damage[ri] = 0.0f;
	// Fired at the ram, landing exactly on the swordsman beside it.
	s.fire_arrow(s.units_.pos_x[ai], 2.0f, s.units_.pos_z[ai], s.units_.pos_x[si], 0.8f, s.units_.pos_z[si],
			999.0f, 0, ram, 0, 1, false, 0, ar);
	float parts[4] = {s.projectiles_.back().dmg_parts[0], s.projectiles_.back().dmg_parts[1],
		s.projectiles_.back().dmg_parts[2], s.projectiles_.back().dmg_parts[3]};
	check(parts[1] > 0.0f, "the arrow carries the live shooter's strengths");
	// The archer dies in flight: a destroyed attacker applies no tech or aura change,
	// and the arrow still hurts whoever it lands on (0.28 keeps data.attackData).
	s.units_.hp[ai] = 0.0f;
	s.units_.alive[ai] = 0;
	const float hp0 = s.units_.hp[si], ram0 = s.units_.hp[ri];
	for (int k = 0; k < 400 && s.units_.hp[si] == hp0; ++k) s.tick(DT);
	const float drop = hp0 - s.units_.hp[si];
	float res[3];
	s.unit_resistance((size_t)si, res);
	const float bonus = combat_bonus(unit_combat(s.units_.tmpl[ai], s.units_.unit_type[ai]),
			s.unit_combat_at((size_t)si), s.civ_of(1));
	const float want = bonus * (parts[0] * powf(0.9f, res[0]) + parts[1] * powf(0.9f, res[1]) +
			parts[2] * powf(0.9f, res[2]) + parts[3]);
	std::printf("      stray on the swordsman: %.3f, fire-time strengths say %.3f (ram kept %.3f)\n",
			drop, want, ram0 - s.units_.hp[ri]);
	check(drop > 1.0f && std::fabs(drop - want) < 1e-3f, "the stray meets the swordsman's armour, not the ram's");
	check(s.units_.hp[ri] == ram0, "the aimed ram, out of the footprint, takes nothing");
}

// An arrow aimed at an invulnerable unit falls through to the next unit the landing
// point is inside of (DelayedDamage.js Hit: HandleAttackEffects is false for it).
static void test_p14_invulnerable_fallthrough() {
	std::printf("29. an arrow at an invulnerable unit falls to the next one (P14)\n");
	Simulation s;
	setup(s);
	s.set_player_active(1, true, false);
	uint32_t ar = s.spawn_unit(0.0f, 0.0f, 0, UNIT_ARCHER);
	uint32_t a = dummy(s, 20.0f, 0.0f, UNIT_SPEARMAN, 500.0f);
	uint32_t b = dummy(s, 21.0f, 0.0f, UNIT_SPEARMAN, 500.0f);
	s.units_.attack_damage[idx(s, a)] = 0.0f;
	s.units_.attack_damage[idx(s, b)] = 0.0f;
	s.triggers_.invulnerable.push_back(a);
	// Lands on the mortal one, aimed at the invulnerable one beside it.
	s.fire_arrow(s.units_.pos_x[idx(s, ar)], 2.0f, s.units_.pos_z[idx(s, ar)], 21.0f, 0.8f, 0.0f,
			999.0f, 0, a, 0, 1, false, 0, ar);
	const float hp0 = s.units_.hp[idx(s, b)], a0 = s.units_.hp[idx(s, a)];
	for (int k = 0; k < 400 && s.units_.hp[idx(s, b)] == hp0; ++k) s.tick(DT);
	std::printf("      mortal spearman -%.3f, invulnerable one -%.3f\n",
			hp0 - s.units_.hp[idx(s, b)], a0 - s.units_.hp[idx(s, a)]);
	check(s.units_.hp[idx(s, b)] < hp0, "the next unit inside the landing point takes the arrow");
	check(s.units_.hp[idx(s, a)] == a0, "the invulnerable aimed target takes nothing");
}

// Aim predicts with the last turn's average velocity ((position - previous position) /
// turn, CCmpPosition m_PrevX/m_PrevZ), not the instantaneous one.
static void test_p14_aim_velocity() {
	std::printf("30. aim uses the last turn's velocity (P14)\n");
	Simulation s;
	setup(s);
	uint32_t w = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SWORDSMAN);
	const int wi = idx(s, w);
	s.order_walk(&w, 1, 60.0f, 0.0f);
	float px = 0.0f, cruise = 0.0f;
	for (int k = 0; k < 10; ++k) {
		s.tick(DT);
		if (k == 5) px = s.units_.pos_x[wi];
	}
	cruise = (s.units_.pos_x[wi] - px) / 0.2f; // two lockstep turns of steady walking
	check(cruise > 0.5f, "the walker cruises");
	// Stop mid-turn: the instantaneous velocity is zero at once, but the aim velocity
	// still reports the last turn's until the turn boundary snapshots it.
	s.cmd_stop(&w, 1);
	s.tick(DT);
	float vx = 0.0f, vz = 0.0f;
	s.aim_velocity_unit((size_t)wi, vx, vz);
	std::printf("      cruising %.2f u/s, after one stopped tick aim says %.2f, instant %.2f\n",
			cruise, vx, s.units_.vel_x[wi]);
	check(std::fabs(s.units_.vel_x[wi]) < 1e-3f, "the stopped unit's instantaneous velocity is zero");
	check(std::fabs(vx - 0.5f * cruise) < 0.35f * cruise, "aim still leads with the last turn's velocity");
	for (int k = 0; k < 4; ++k) s.tick(DT); // cross the turn boundary
	s.aim_velocity_unit((size_t)wi, vx, vz);
	check(std::fabs(vx) < 1e-3f && std::fabs(vz) < 1e-3f, "past the boundary, aim knows it stopped");
}

// What main's P14 left: PredictTimeToTarget's lead, ApplyStatus carried by the arrow, and
// the aimed animal before a unit under the same landing point (DelayedDamage.js Hit).
static void test_p14_leftovers() {
	std::printf("31. arrow lead, status and aimed animals as DelayedDamage.js Hit (P14)\n");
	const float away = Simulation::predict_time_to_target(0.0f, 0.0f, 20.0f, 30.0f, 0.0f, 5.0f, 0.0f);
	const float toward = Simulation::predict_time_to_target(0.0f, 0.0f, 20.0f, 30.0f, 0.0f, -5.0f, 0.0f);
	const float outrun = Simulation::predict_time_to_target(0.0f, 0.0f, 4.0f, 30.0f, 0.0f, 5.0f, 0.0f);
	std::printf("      meet a target 30 away: %.3f s fleeing, %.3f s closing, %.3f outrun\n", away, toward, outrun);
	check(std::fabs(away - 2.0f) < 1e-4f && std::fabs(toward - 1.2f) < 1e-4f, "lead time solves where arrow and target meet");
	check(outrun == 0.0f, "a target that outruns the arrow is aimed at where it stands");
	{
		// A maiden archer's poison arrow still poisons after she dies in flight.
		Simulation s;
		setup(s);
		s.set_player_active(1, true, false);
		const uint16_t maiden = template_named("maur/champion_maiden_archer");
		uint32_t ar = s.spawn_unit(0.0f, 0.0f, 0, UNIT_ARCHER_MAIDEN, 0.0f, maiden);
		uint32_t sp = dummy(s, 20.0f, 0.0f, UNIT_SPEARMAN, 500.0f);
		const int ai = idx(s, ar), ti = idx(s, sp);
		s.units_.attack_damage[ti] = 0.0f;
		s.fire_arrow(s.units_.pos_x[ai], 2.0f, s.units_.pos_z[ai], 20.0f, 0.8f, 0.0f, 999.0f, 0, sp,
				status_effect_bit(s.unit_combat_at((size_t)ai).status), 1, false, 0, ar);
		s.units_.hp[ai] = 0.0f;
		s.units_.alive[ai] = 0;
		const float hp0 = s.units_.hp[ti];
		for (int k = 0; k < 400 && s.units_.hp[ti] == hp0; ++k) s.tick(DT);
		check(s.units_.hp[ti] < hp0 && s.units_.status_effects[ti] != 0, "a dead archer's arrow still poisons");
	}
	{
		// Aimed at a boar with an enemy spearman under the same point: the boar takes it. The archer
		// shoots from 6 away, so the arrow lands before the two, standing on each other, push each
		// other out of the point (CCmpUnitMotion_System.cpp:671-804 pushes animals as units).
		Simulation s;
		setup(s);
		s.set_player_active(1, true, false);
		uint32_t ar = s.spawn_unit(14.0f, 0.0f, 0, UNIT_ARCHER);
		const uint32_t boar = s.spawn_fauna(20.0f, 0.0f, FAUNA_BOAR);
		uint32_t sp = dummy(s, 20.1f, 0.0f, UNIT_SPEARMAN, 500.0f);
		s.units_.attack_damage[idx(s, sp)] = 0.0f;
		const int ai = idx(s, ar);
		s.fire_arrow(s.units_.pos_x[ai], 2.0f, s.units_.pos_z[ai], 20.0f, 0.8f, 0.0f, 999.0f, 0, boar, 0, 1, false, 0, ar);
		const size_t f = s.fauna_id_to_idx_.at(boar);
		const float f0 = s.fauna_.hp[f], hp0 = s.units_.hp[idx(s, sp)];
		const uint32_t pid = s.projectiles_.back().id;
		for (int k = 0; k < 400; ++k) {
			bool flying = false;
			for (const Projectile &p : s.projectiles_) flying |= p.id == pid;
			if (!flying) break;
			s.tick(DT);
		}
		const auto it = s.fauna_id_to_idx_.find(boar);
		const bool boar_hit = it == s.fauna_id_to_idx_.end() || s.fauna_.hp[it->second] < f0;
		std::printf("      boar hit %d, spearman -%.3f\n", boar_hit, hp0 - s.units_.hp[idx(s, sp)]);
		check(boar_hit && s.units_.hp[idx(s, sp)] == hp0, "the aimed animal takes the arrow before a unit beside it");
	}
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
	test_timing_projectiles();
	test_damage_effects();
	test_projectile_landing();
	test_ranged_hunting();
	test_status_credit();
	test_germanic_auras();
	test_aura_stacking();
	test_reach_and_responses();
	test_attack_rate_and_invulnerable();
	test_razed_target();
	test_uphill_archer();
	test_status_inside();
	test_arrow_square_range();
	test_aura_vs_animals();
	test_hill_tower_reach();
	test_p14_frozen_damage();
	test_p14_stray_armour();
	test_p14_invulnerable_fallthrough();
	test_p14_aim_velocity();
	test_p14_leftovers();
	if (g_failures) {
		std::printf("test_combat: FAIL (%d)\n", g_failures);
		return 1;
	}
	std::printf("test_combat: PASS\n");
	return 0;
}
