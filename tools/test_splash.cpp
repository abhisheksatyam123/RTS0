// RTS0 splash damage test (headless: no SCons, no Godot).
// Checks the port of 0 A.D.'s Attack/<type>/Splash (Attack.js GetSplashData,
// helpers/Attack.js CauseDamageOverArea, DelayedDamage.js Hit), with the data
// in splash_data.h from tools/extract_0ad_splash.py:
//  1. Data: stone throwers, bolt shooters, siege warships, melee elephants and
//     the longsword mixin's champions splash, with their template numbers;
//     archers, spearmen and rams don't.
//  2. Circular: an onager's 4 m Crush 160 hurts the target and enemies by it,
//     less with distance (1 - d^2/r^2 from each one's edge), not its own side
//     (no FriendlyFire), not enemies outside the range; the centre takes the
//     Splash Damage less its own resistance.
//  3. Linear: a bolt shooter's 9 m strip, a fifth as wide, runs on from where
//     the bolt lands along its flight: units behind the target take damage,
//     units beside the strip or short of the impact don't.
//  4. In play: a stone thrower's stone and an elephant's tusks splash; an
//     archer's arrow hurts only whom it hits.
//  5. Animals: a splash hurts Gaia's animals in range, not the attacker's own livestock.
// Build and run:
//   g++ -std=c++20 -O2 -Isrc tools/test_splash.cpp src/simulation/simulation.cpp \
//     src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//     src/network/net_host.cpp -lpthread -o /tmp/test_splash && /tmp/test_splash
// Exit: 0 = PASS, 1 = FAIL.
#define private public
#include "simulation/simulation.h"
#undef private

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace rtso;

static constexpr float DT = 0.05f;

static int g_failures = 0;
static void check(bool ok, const char *what) {
	std::printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
	if (!ok) ++g_failures;
}

static void setup(Simulation &s) {
	s.set_sim_seed(4321);
	s.reset();
	s.set_victory_condition(VICTORY_ENDLESS);
	s.spawn_building_world(-56.0f, -56.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(56.0f, 56.0f, BLD_TOWN_CENTER, 1, 1.0f);
	s.set_player_active(1, true, false); // target dummies, not a Petra army that answers the attack
}
static int idx(const Simulation &s, uint32_t id) { return s.lookup_unit(id); }
static float hp(const Simulation &s, uint32_t id) { return s.units_.hp[idx(s, id)]; }
// A unit that holds its ground (passive units run when hit).
static uint32_t still(Simulation &s, float x, float z, uint8_t team, UnitType type, float h = 1.0e4f) {
	uint32_t id = s.spawn_unit(x, z, team, type, h);
	s.order_set_stance(&id, 1, STANCE_STAND_GROUND);
	return id;
}
static uint16_t template_named(const char *suffix) {
	for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT; ++t) {
		const char *id = civ_unit_template(t).id;
		size_t n = std::strlen(id), m = std::strlen(suffix);
		if (n >= m && std::strcmp(id + n - m, suffix) == 0) return t;
	}
	return CIV_UNIT_TEMPLATE_NONE;
}
static const SplashProfile &splash_of(const char *suffix) {
	return splash_profiles()[unit_splash(template_named(suffix), UNIT_VILLAGER)];
}

static void test_data() {
	std::printf("1. data\n");
	const SplashProfile &onager = splash_of("rome/siege_onager_packed");
	check(onager.shape == SPLASH_CIRCULAR && onager.range == 4.0f && onager.crush == 160.0f && !onager.friendly_fire,
			"onager: Circular 4 m, Crush 160, no friendly fire");
	const SplashProfile &litho = splash_of("athen/siege_lithobolos_packed");
	check(litho.shape == SPLASH_CIRCULAR && litho.range == 1.5f && litho.crush == 120.0f, "lithobolos: Circular 1.5 m, Crush 120");
	const SplashProfile &oxy = splash_of("athen/siege_oxybeles_packed");
	check(oxy.shape == SPLASH_LINEAR && oxy.range == 9.0f && oxy.pierce == 80.0f, "oxybeles: Linear 9 m, Pierce 80");
	check(splash_of("ptol/siege_polybolos_packed").pierce == 44.0f, "polybolos: Pierce 44");
	check(splash_of("rome/ship_siege").range == 4.0f && splash_of("rome/ship_siege").crush == 100.0f, "siege warship: 4 m, Crush 100");
	const SplashProfile &pers_ele = splash_of("pers/champion_elephant");
	check(pers_ele.range == 2.5f && pers_ele.hack == 33.0f && pers_ele.pierce == 0.0f && pers_ele.crush == 0.0f,
			"Indian elephant: 2.5 m, Hack 30 x1.1, nothing added for Pierce or Crush");
	check(splash_of("cart/champion_elephant").hack == 27.0f, "African elephant: Hack 30 x0.9");
	check(splash_of("cart/hero_hannibal").hack == 45.0f, "elephant hero: Hack 45");
	check(splash_of("mace/champion_infantry_swordsman").range == 2.5f && splash_of("mace/champion_infantry_swordsman").hack == 6.0f,
			"longsword champion: Hack 6 over 2.5 m");
	check(unit_splash(CIV_UNIT_TEMPLATE_NONE, UNIT_CATAPULT) != 0 && unit_splash(CIV_UNIT_TEMPLATE_NONE, UNIT_BOLT_SHOOTER) != 0,
			"generic stone and bolt throwers splash");
	check(unit_splash(template_named("athen/infantry_archer_b"), UNIT_ARCHER) == 0 &&
			unit_splash(CIV_UNIT_TEMPLATE_NONE, UNIT_SPEARMAN) == 0 && unit_splash(CIV_UNIT_TEMPLATE_NONE, UNIT_SIEGE_RAM) == 0,
			"archers, spearmen and rams don't");
}

static uint8_t row_of(const char *suffix) { return unit_splash(template_named(suffix), UNIT_VILLAGER); }

static void test_circular() {
	std::printf("2. circular\n");
	Simulation s;
	setup(s);
	const uint8_t onager = row_of("rome/siege_onager_packed");
	const uint32_t target = still(s, 0.0f, 0.0f, 1, UNIT_SWORDSMAN);
	const uint32_t near = still(s, 0.6f, 0.0f, 1, UNIT_SWORDSMAN);
	const uint32_t mid = still(s, 0.0f, 1.0f, 1, UNIT_SWORDSMAN);
	const uint32_t far = still(s, 3.5f, 0.0f, 1, UNIT_SWORDSMAN);
	const uint32_t own = still(s, -0.6f, 0.0f, 0, UNIT_SWORDSMAN);
	const float h0 = hp(s, target), o0 = hp(s, own);
	s.cause_damage_over_area(onager, 0, 0, 0.0f, 0.0f, 1.0f, 0.0f);
	const float dt = h0 - hp(s, target), dn = h0 - hp(s, near), dm = h0 - hp(s, mid);
	std::printf("  damage: target %.1f, 0.6 away %.1f, 1.0 away %.1f\n", dt, dn, dm);
	check(dt > 0.0f && dn > 0.0f && dm > 0.0f, "the target and enemies within 4 m take damage");
	check(dt >= dn && dn > dm, "less the further out");
	check(hp(s, far) == h0, "an enemy 3.5 units away takes none");
	check(hp(s, own) == o0, "the attacker's own unit takes none (no FriendlyFire)");
	// The swordsman's crush resistance (template) reduces Crush 160 at the centre.
	float res[3];
	s.unit_resistance(idx(s, target), res);
	check(std::fabs(dt - 160.0f * powf(0.9f, res[2])) < 0.01f, "the centre takes Crush 160 less its own resistance");
}

static void test_linear() {
	std::printf("3. linear\n");
	Simulation s;
	setup(s);
	const uint8_t oxy = row_of("athen/siege_oxybeles_packed");
	// The bolt flies along +x and lands at the origin.
	const uint32_t target = still(s, 0.0f, 0.0f, 1, UNIT_SPEARMAN);
	const uint32_t behind = still(s, 2.5f, 0.0f, 1, UNIT_SPEARMAN);
	const uint32_t beside = still(s, 1.0f, 2.2f, 1, UNIT_SPEARMAN);
	const uint32_t before = still(s, -2.0f, 0.0f, 1, UNIT_SPEARMAN);
	const uint32_t past = still(s, 6.5f, 0.0f, 1, UNIT_SPEARMAN);
	const float h0 = hp(s, target);
	s.cause_damage_over_area(oxy, 0, 0, 0.0f, 0.0f, 1.0f, 0.0f);
	std::printf("  damage: target %.1f, behind %.1f, beside %.1f, short %.1f, past %.1f\n",
			h0 - hp(s, target), h0 - hp(s, behind), h0 - hp(s, beside), h0 - hp(s, before), h0 - hp(s, past));
	check(hp(s, target) < h0 && hp(s, behind) < h0, "the target and a unit behind it on the bolt's line take damage");
	check(h0 - hp(s, behind) < h0 - hp(s, target), "less further along");
	check(hp(s, beside) == h0, "a unit beside the strip takes none");
	check(hp(s, before) == h0, "a unit short of the impact takes none");
	check(hp(s, past) == h0, "a unit beyond 9 m takes none");
}

static void test_in_play() {
	std::printf("4. in play\n");
	{
		Simulation s;
		setup(s);
		uint32_t cat = s.spawn_unit(0.0f, 0.0f, 0, UNIT_CATAPULT, 0.0f, template_named("rome/siege_onager_packed"));
		const float r = s.unit_info_at(idx(s, cat)).attack_range;
		const uint32_t target = still(s, r * 0.6f, 0.0f, 1, UNIT_SWORDSMAN);
		const uint32_t next = still(s, r * 0.6f, 0.7f, 1, UNIT_SWORDSMAN);
		const float h0 = hp(s, next);
		s.order_attack(&cat, 1, target);
		for (int k = 0; k < (int)(40.0f / DT); ++k) s.tick(DT);
		check(hp(s, target) < h0, "the onager hits its target");
		check(hp(s, next) < h0, "and its stones splash the swordsman beside it");
	}
	{
		Simulation s;
		setup(s);
		// Hannibal's elephant (Hack 45 over 2 m) into a tight block of spearmen.
		uint32_t ele = s.spawn_unit(-2.0f, 0.0f, 0, UNIT_HERO, 0.0f, template_named("cart/hero_hannibal"));
		std::vector<uint32_t> block;
		for (int k = 0; k < 9; ++k) block.push_back(still(s, 1.0f + 1.1f * (k % 3), -1.1f + 1.1f * (k / 3), 1, UNIT_SPEARMAN));
		const uint32_t target = block[3];
		const float h0 = hp(s, target);
		s.order_attack(&ele, 1, target);
		for (int k = 0; k < (int)(10.0f / DT); ++k) s.tick(DT);
		int splashed = 0;
		for (uint32_t id : block)
			if (id != target && hp(s, id) < h0) ++splashed;
		std::printf("  Hannibal's elephant: %d spearmen beside its target hurt\n", splashed);
		check(hp(s, target) < h0, "Hannibal's elephant hits its target");
		check(splashed > 0, "and its tusks splash the spearmen around it");
	}
	{
		Simulation s;
		setup(s);
		uint32_t arc = s.spawn_unit(0.0f, 0.0f, 0, UNIT_ARCHER);
		const uint32_t target = still(s, 4.0f, 0.0f, 1, UNIT_SWORDSMAN);
		const uint32_t next = still(s, 4.0f, 0.6f, 1, UNIT_SWORDSMAN);
		const float h0 = hp(s, next);
		s.order_attack(&arc, 1, target);
		for (int k = 0; k < (int)(10.0f / DT); ++k) s.tick(DT);
		check(hp(s, target) < h0 || hp(s, next) < h0, "the archer hits");
		check(hp(s, target) == h0 || hp(s, next) == h0, "and only whom its arrow lands on");
	}
}

static void test_animals() {
	std::printf("5. animals\n");
	Simulation s;
	setup(s);
	const uint8_t onager = row_of("rome/siege_onager_packed");
	const uint32_t deer = s.spawn_fauna(1.0f, 0.0f, FAUNA_DEER);
	const uint32_t far_deer = s.spawn_fauna(8.0f, 0.0f, FAUNA_DEER);
	const uint32_t sheep = s.spawn_fauna(-1.0f, 0.0f, FAUNA_SHEEP, 0);
	auto fhp = [&](uint32_t id) { return s.fauna_.hp[s.fauna_id_to_idx_.at(id)]; };
	const float d0 = fhp(deer), f0 = fhp(far_deer), s0 = fhp(sheep);
	s.cause_damage_over_area(onager, 0, 0, 0.0f, 0.0f, 1.0f, 0.0f);
	check(fhp(deer) < d0, "a Gaia deer by the impact takes the splash (CauseDamageOverArea queries Gaia too)");
	check(fhp(far_deer) == f0, "one out of range doesn't");
	check(fhp(sheep) == s0, "the attacker's own sheep doesn't");
	for (int k = 0; k < 4; ++k) s.tick(DT);
	const bool killed = d0 <= 160.0f * 0.5f;
	const auto it = s.fauna_id_to_idx_.find(deer);
	const bool gone = it == s.fauna_id_to_idx_.end() || !s.fauna_.alive[it->second];
	check(!killed || gone, "a deer the splash kills leaves the world (its carcass stays as food)");
}

int main() {
	test_data();
	test_circular();
	test_linear();
	test_in_play();
	test_animals();
	if (g_failures) {
		std::printf("test_splash: FAIL (%d)\n", g_failures);
		return 1;
	}
	std::printf("test_splash: PASS\n");
	return 0;
}
