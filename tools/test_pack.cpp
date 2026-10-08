// RTS0 siege packing test (headless: no SCons, no Godot).
// Checks the port of 0 A.D.'s Pack.js and UnitAI's pack rules (Simulation::tick_pack):
// stone throwers and bolt shooters move packed, take 10 s (Pack/Time 10000) to unpack before
// they can fire, and 10 s to pack again before they can move.
// Build and run:
//   g++ -std=c++20 -O2 -Isrc tools/test_pack.cpp src/simulation/simulation.cpp \
//     src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//     src/network/net_host.cpp -lpthread -o /tmp/test_pack && /tmp/test_pack
//
//  1. Who packs: catapults and bolt shooters spawn packed (0 A.D. trains the "_packed"
//     template); infantry and rams have no Pack. Every roster template that packs is a
//     "_packed" 0 A.D. template.
//  2. Attack in range: a packed catapult ordered to attack a target in range stays put and
//     holds fire while it unpacks, then fires.
//  3. Move while unpacked: a walk order makes it pack first (no movement for 10 s), then move.
//  4. Walk cancels unpacking at once (UnitAI g_OrdersCancelUnpacking).
//  5. Target out of range: an unpacked bolt shooter packs, closes in, unpacks and fires.
//  6. order_pack (the Pack/Unpack command) and save/load keep the pack state.
//  Exit: 0 = PASS, 1 = FAIL.
#include "simulation/simulation.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace rtso;

static constexpr float DT = 0.2f; // one lockstep turn

static int g_failures = 0;
static void check(bool ok, const char *what) {
	std::printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
	if (!ok) ++g_failures;
}

static void setup(Simulation &s) {
	s.set_sim_seed(777);
	s.reset();
	s.set_victory_condition(VICTORY_ENDLESS);
	s.spawn_building_world(-56.0f, -56.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(56.0f, 56.0f, BLD_TOWN_CENTER, 1, 1.0f);
}

static int idx_of(const Simulation &s, uint32_t uid) {
	const auto u = s.units();
	for (size_t i = 0; i < u.count; ++i)
		if (u.id[i] == uid) return (int)i;
	return -1;
}
static uint8_t pack_of(const Simulation &s, uint32_t uid) {
	int i = idx_of(s, uid);
	return i < 0 ? 255 : s.units().pack_state[i];
}
static float x_of(const Simulation &s, uint32_t uid) { return s.units().pos_x[idx_of(s, uid)]; }
static float z_of(const Simulation &s, uint32_t uid) { return s.units().pos_z[idx_of(s, uid)]; }
static float hp_of(const Simulation &s, uint32_t uid) { return s.units().hp[idx_of(s, uid)]; }

// A tough enemy dummy that never fights back or walks off.
static uint32_t dummy(Simulation &s, float x, float z) {
	uint32_t id = s.spawn_unit(x, z, 1, UNIT_SPEARMAN, 1.0e6f);
	s.order_set_stance(&id, 1, STANCE_PASSIVE);
	return id;
}

static void run(Simulation &s, float seconds) {
	int n = (int)std::lround(seconds / DT);
	for (int i = 0; i < n; ++i) s.tick(DT);
}

static void test_who_packs() {
	std::printf("1. who packs\n");
	Simulation s;
	setup(s);
	uint32_t cat = s.spawn_unit(0, 0, 0, UNIT_CATAPULT);
	uint32_t bolt = s.spawn_unit(2, 0, 0, UNIT_BOLT_SHOOTER);
	uint32_t ram = s.spawn_unit(4, 0, 0, UNIT_SIEGE_RAM);
	uint32_t spear = s.spawn_unit(6, 0, 0, UNIT_SPEARMAN);
	check(pack_of(s, cat) == PACK_PACKED, "catapult spawns packed");
	check(pack_of(s, bolt) == PACK_PACKED, "bolt shooter spawns packed");
	check(pack_of(s, ram) == PACK_NONE, "ram has no Pack");
	check(pack_of(s, spear) == PACK_NONE, "spearman has no Pack");

	int packers = 0, bad = 0;
	for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT; ++t) {
		const CivUnitTemplate &ct = civ_unit_template(t);
		if (!unit_has_pack(t, ct.unit_type)) continue;
		++packers;
		if (!std::strstr(ct.id, "_packed")) ++bad;
	}
	std::printf("  roster templates with Pack: %d\n", packers);
	check(packers > 0 && bad == 0, "roster templates that pack are 0 A.D. \"_packed\" templates");
}

static void test_attack_in_range() {
	std::printf("2. attack a target in range\n");
	Simulation s;
	setup(s);
	uint32_t cat = s.spawn_unit(0, 0, 0, UNIT_CATAPULT);
	const float r = s.unit_info_at(idx_of(s, cat)).attack_range;
	uint32_t tgt = dummy(s, r * 0.6f, 0.0f);
	const float hp0 = hp_of(s, tgt);
	s.order_attack(&cat, 1, tgt);
	run(s, 1.0f);
	check(pack_of(s, cat) == PACK_UNPACKING, "starts unpacking");
	run(s, 8.0f); // 9 s in
	check(pack_of(s, cat) == PACK_UNPACKING, "still unpacking at 9 s");
	check(std::fabs(x_of(s, cat)) < 0.3f && std::fabs(z_of(s, cat)) < 0.3f, "does not move while unpacking");
	check(hp_of(s, tgt) == hp0 && s.projectiles().empty(), "holds fire while unpacking");
	run(s, 1.4f);
	check(pack_of(s, cat) == PACK_UNPACKED, "unpacked after 10 s");
	run(s, 8.0f);
	check(hp_of(s, tgt) < hp0, "fires once unpacked");
	check(std::fabs(x_of(s, cat)) < 0.3f, "stays put while firing");
}

static void test_move_while_unpacked() {
	std::printf("3. walk order while unpacked\n");
	Simulation s;
	setup(s);
	uint32_t cat = s.spawn_unit(0, 0, 0, UNIT_CATAPULT);
	s.order_pack(&cat, 1, false);
	run(s, 10.4f);
	check(pack_of(s, cat) == PACK_UNPACKED, "order_pack(false) unpacks in 10 s");
	s.cmd_move(&cat, 1, -30.0f, 0.0f);
	run(s, 1.0f);
	check(pack_of(s, cat) == PACK_PACKING, "walk order starts packing");
	run(s, 8.0f);
	check(std::fabs(x_of(s, cat)) < 0.3f, "does not move while packing");
	run(s, 1.4f);
	check(pack_of(s, cat) == PACK_PACKED, "packed after 10 s");
	run(s, 3.0f);
	check(x_of(s, cat) < -2.0f, "moves once packed");
}

static void test_walk_cancels_unpack() {
	std::printf("4. walk order cancels unpacking\n");
	Simulation s;
	setup(s);
	uint32_t cat = s.spawn_unit(0, 0, 0, UNIT_CATAPULT);
	s.order_pack(&cat, 1, false);
	run(s, 3.0f);
	check(pack_of(s, cat) == PACK_UNPACKING, "unpacking 3 s in");
	s.cmd_move(&cat, 1, -30.0f, 0.0f);
	run(s, DT);
	check(pack_of(s, cat) == PACK_PACKED, "walk order cancels the unpack at once");
	run(s, 3.0f);
	check(x_of(s, cat) < -2.0f, "and walks off without waiting");
}

static void test_out_of_range() {
	std::printf("5. target out of range\n");
	Simulation s;
	setup(s);
	uint32_t bolt = s.spawn_unit(-40, 0, 0, UNIT_BOLT_SHOOTER);
	s.order_pack(&bolt, 1, false);
	run(s, 10.4f);
	check(pack_of(s, bolt) == PACK_UNPACKED, "bolt shooter unpacked");
	const float r = s.unit_info_at(idx_of(s, bolt)).attack_range;
	uint32_t tgt = dummy(s, -40.0f + r + 20.0f, 0.0f);
	const float hp0 = hp_of(s, tgt);
	s.order_attack(&bolt, 1, tgt);
	run(s, 1.0f);
	check(pack_of(s, bolt) == PACK_PACKING, "packs to chase a target out of range");
	run(s, 60.0f);
	check(hp_of(s, tgt) < hp0, "closes in, unpacks and fires");
	check(pack_of(s, bolt) == PACK_UNPACKED, "ends unpacked next to its target");
}

static void test_save_load(const char *path) {
	std::printf("6. pack command and save/load\n");
	Simulation s;
	setup(s);
	uint32_t a = s.spawn_unit(0, 0, 0, UNIT_CATAPULT);
	uint32_t b = s.spawn_unit(4, 0, 0, UNIT_BOLT_SHOOTER);
	s.order_pack(&a, 1, false);
	run(s, 4.0f);
	check(pack_of(s, a) == PACK_UNPACKING && pack_of(s, b) == PACK_PACKED, "order_pack(false) unpacks one");
	const float elapsed = s.units().pack_elapsed[idx_of(s, a)];
	check(s.save_state(path), "save_state");
	Simulation l;
	check(l.load_state(path), "load_state");
	check(pack_of(l, a) == PACK_UNPACKING && pack_of(l, b) == PACK_PACKED, "pack states survive save/load");
	check(std::fabs(l.units().pack_elapsed[idx_of(l, a)] - elapsed) < 1e-4f, "pack progress survives save/load");
	run(l, 6.4f);
	check(pack_of(l, a) == PACK_UNPACKED, "loaded game finishes the unpack on time");
	s.order_pack(&a, 1, true);
	check(pack_of(s, a) == PACK_PACKED, "Pack during an unpack cancels it");
	std::remove(path);
}

int main(int argc, char **argv) {
	std::string save = (argc > 1) ? argv[1] : "/tmp/rts0_test_pack.sav";
	test_who_packs();
	test_attack_in_range();
	test_move_while_unpacked();
	test_walk_cancels_unpack();
	test_out_of_range();
	test_save_load(save.c_str());
	if (g_failures) {
		std::printf("test_pack: FAIL (%d)\n", g_failures);
		return 1;
	}
	std::printf("test_pack: PASS\n");
	return 0;
}
