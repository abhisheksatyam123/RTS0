// RTS0 siege packing test (headless: no SCons, no Godot).
// Checks the port of 0 A.D.'s Pack.js and UnitAI's pack rules (Simulation::tick_pack):
// stone throwers and bolt shooters move packed, take 5 s (Pack/Time 5000) to unpack before
// they can fire, and 5 s to pack again before they can move.
// Build and run:
//   g++ -std=c++20 -O2 -Isrc tools/test_pack.cpp src/simulation/simulation.cpp \
//     src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//     src/network/net_host.cpp -lpthread -o /tmp/test_pack && /tmp/test_pack
//
//  1. Who packs: catapults and bolt shooters spawn packed (0 A.D. trains the "_packed"
//     template); infantry and rams have no Pack. Roster templates pack by ROSTER_PACKABLE,
//     and siege towers (ROSTER_SIEGE_TOWER) never do.
//  2. Attack in range: a packed catapult ordered to attack a target in range stays put and
//     holds fire while it unpacks, then fires.
//  3. Move while unpacked: a walk order makes it pack first (no movement for 5 s), then move.
//  4. Walk cancels unpacking at once (UnitAI g_OrdersCancelUnpacking).
//  5. Target out of range: an unpacked bolt shooter packs, closes in, unpacks and fires.
//  6. order_pack (the Pack/Unpack command) and save/load keep the pack state.
//  7. Lockstep command 29 (NET_CMD_PACK) unpacks and packs the sender's own engines only.
//  8. Walking away from an enemy in range packs and leaves (walks don't acquire targets by sight).
//  Exit: 0 = PASS, 1 = FAIL.
#include "simulation/simulation.h"
#include "network/turn_manager.h"

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
	// Human seats: Petra would defend the dummy and walk it home (defenseArmy.js clear).
	for (uint8_t p = 0; p < 2; ++p) s.player(p).active = 1, s.player(p).is_ai = 0;
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

// A tough enemy dummy that never fights back or walks off: stand ground (a passive
// unit flees when attacked, 0 A.D. UnitAI respondFlee), out of its own reach.
static uint32_t dummy(Simulation &s, float x, float z) {
	uint32_t id = s.spawn_unit(x, z, 1, UNIT_SPEARMAN, 1.0e6f);
	s.order_set_stance(&id, 1, STANCE_STAND_GROUND);
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

	int packers = 0, bad = 0, towers = 0;
	for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT; ++t) {
		const CivUnitTemplate &ct = civ_unit_template(t);
		if (ct.flags & ROSTER_SIEGE_TOWER) {
			++towers;
			if (unit_has_pack(t, ct.unit_type)) ++bad;
			continue;
		}
		if (!unit_has_pack(t, ct.unit_type)) continue;
		++packers;
		if (ct.unit_type != UNIT_CATAPULT && ct.unit_type != UNIT_BOLT_SHOOTER) ++bad;
	}
	std::printf("  roster templates with Pack: %d, siege towers: %d\n", packers, towers);
	check(packers > 0 && bad == 0, "only stone throwers and bolt shooters pack; siege towers don't");
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
	run(s, 3.0f); // 4 s in
	check(pack_of(s, cat) == PACK_UNPACKING, "still unpacking at 4 s");
	check(std::fabs(x_of(s, cat)) < 0.3f && std::fabs(z_of(s, cat)) < 0.3f, "does not move while unpacking");
	check(hp_of(s, tgt) == hp0 && s.projectiles().empty(), "holds fire while unpacking");
	run(s, 1.4f);
	check(pack_of(s, cat) == PACK_UNPACKED, "unpacked after 5 s");
	// PrepareTime (3 s) after unpacking it shoots; a stone can land wide (Spread).
	bool fired = false;
	for (int i = 0; i < 40; ++i) {
		s.tick(DT);
		fired |= !s.projectiles().empty();
	}
	check(fired || hp_of(s, tgt) < hp0, "fires once unpacked");
	check(std::fabs(x_of(s, cat)) < 0.3f, "stays put while firing");
}

static void test_move_while_unpacked() {
	std::printf("3. walk order while unpacked\n");
	Simulation s;
	setup(s);
	uint32_t cat = s.spawn_unit(0, 0, 0, UNIT_CATAPULT);
	s.order_pack(&cat, 1, false);
	run(s, 5.4f);
	check(pack_of(s, cat) == PACK_UNPACKED, "order_pack(false) unpacks in 5 s");
	s.cmd_move(&cat, 1, -30.0f, 0.0f);
	run(s, 1.0f);
	check(pack_of(s, cat) == PACK_PACKING, "walk order starts packing");
	run(s, 3.0f);
	check(std::fabs(x_of(s, cat)) < 0.3f, "does not move while packing");
	run(s, 1.4f);
	check(pack_of(s, cat) == PACK_PACKED, "packed after 5 s");
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
	s.set_reveal_map(true); // the target stands beyond its vision: let the player see it (CheckTargetVisible)
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
	check(pack_of(s, a) == PACK_UNPACKING, "Pack leaves an engine that is unpacking alone (CanPack)");
	s.order_pack(&a, 1, true, true);
	check(pack_of(s, a) == PACK_PACKED, "Cancel Unpack takes it back to packed");
	s.order_walk(&b, 1, 20, 0);
	s.order_pack(&b, 1, true);
	check(s.unit_order(b).kind == ORDER_WALK, "Pack leaves a packed engine walking on");
	std::remove(path);
}

static void test_lockstep() {
	std::printf("7. lockstep Pack/Unpack command\n");
	Simulation s;
	setup(s);
	uint32_t mine = s.spawn_unit(0, 0, 0, UNIT_CATAPULT);
	uint32_t theirs = s.spawn_unit(-20, -20, 1, UNIT_CATAPULT);
	// Passive so the two engines don't unpack to shoot each other.
	s.order_set_stance(&mine, 1, STANCE_PASSIVE);
	s.order_set_stance(&theirs, 1, STANCE_PASSIVE);
	TurnManager tm;
	auto turns = [&](float seconds) {
		int n = (int)std::lround(seconds / TurnManager::TURN_DURATION);
		for (int i = 0; i < n; ++i) tm.tick(TurnManager::TURN_DURATION, s);
	};
	check(static_cast<uint8_t>(NetCommandType::NET_CMD_PACK) == 29, "NET_CMD_PACK is command 29");
	NetCommand c;
	c.client_id = 0;
	c.cmd_type = (uint8_t)NetCommandType::NET_CMD_PACK;
	c.entity_ids = {mine, theirs};
	c.param = 0;
	tm.post_local_command(c);
	turns(12.0f);
	check(pack_of(s, mine) == PACK_UNPACKED, "command 29 unpacks the sender's catapult");
	check(pack_of(s, theirs) == PACK_PACKED, "and leaves another player's alone");
	c.param = 1;
	tm.post_local_command(c);
	turns(12.0f);
	check(pack_of(s, mine) == PACK_PACKED, "command 29 with param 1 packs it again");
}

static void test_retreat_under_fire() {
	std::printf("8. retreat while enemies are in range\n");
	Simulation s;
	setup(s);
	uint32_t cat = s.spawn_unit(0, 0, 0, UNIT_CATAPULT);
	const float r = s.unit_info_at(idx_of(s, cat)).attack_range;
	uint32_t tgt = dummy(s, r * 0.6f, 0.0f);
	s.order_attack(&cat, 1, tgt);
	run(s, 12.0f);
	check(pack_of(s, cat) == PACK_UNPACKED, "unpacked and firing");
	// A walk, even an unforced one, never picks targets it sees (UnitAI
	// INDIVIDUAL.WALKING), so the engine packs and leaves instead of
	// unpacking again for the enemy still in range.
	s.cmd_move(&cat, 1, -30.0f, 0.0f);
	run(s, 10.4f);
	check(pack_of(s, cat) == PACK_PACKED, "packs within 5 s");
	run(s, 6.0f);
	check(x_of(s, cat) < -2.0f, "and walks away");
}

// UnitAI Order.Garrison: an unpacked engine packs first (forced Pack), then gets in packed.
static void test_garrison_packs_first() {
	std::printf("garrison packs first\n");
	static Simulation s;
	setup(s);
	uint32_t ars = s.spawn_building_world(-20.0f, -20.0f, BLD_ARSENAL, 0, 1.0f);
	uint32_t cat = s.spawn_unit(-20.0f, -12.0f, 0, UNIT_CATAPULT);
	s.order_pack(&cat, 1, false);
	run(s, 10.4f);
	check(pack_of(s, cat) == PACK_UNPACKED, "unpacked beside the arsenal");
	s.order_garrison(&cat, 1, ars, false);
	run(s, 1.0f);
	check(s.units().alive[idx_of(s, cat)] && pack_of(s, cat) == PACK_PACKING, "the unpacked catapult packs before it garrisons");
	run(s, 30.0f);
	check(!s.units().alive[idx_of(s, cat)] && s.get_garrison_count(ars) == 1 && pack_of(s, cat) == PACK_PACKED,
		"once packed it walks in, packed");
}

static void test_stop_cancels_pack() {
	std::printf("10. Stop cancels a pack or unpack in progress (ReplaceOrder lets Stop through, Pack.CancelPack)\n");
	Simulation s;
	setup(s);
	uint32_t cat = s.spawn_unit(0, 0, 0, UNIT_CATAPULT);
	s.order_pack(&cat, 1, false);
	run(s, 5.4f);
	check(pack_of(s, cat) == PACK_UNPACKED, "deployed");
	s.cmd_move(&cat, 1, -30.0f, 0.0f);
	run(s, 1.0f);
	check(pack_of(s, cat) == PACK_PACKING, "a walk starts packing");
	s.cmd_stop(&cat, 1);
	run(s, 6.0f);
	check(pack_of(s, cat) == PACK_UNPACKED, "Stop keeps it unpacked");
	uint32_t cat2 = s.spawn_unit(6, 0, 0, UNIT_CATAPULT);
	s.order_pack(&cat2, 1, false);
	run(s, 2.0f);
	check(pack_of(s, cat2) == PACK_UNPACKING, "unpacking");
	s.cmd_stop(&cat2, 1);
	run(s, 6.0f);
	check(pack_of(s, cat2) == PACK_PACKED, "Stop keeps it packed");
}

// 11. Foundation.js Commit / UnitAI WillMoveFromFoundation: a packed engine on a foundation
//     walks off it (only one packing, or unpacked, stays), so the foundation starts.
static void test_packed_leaves_foundation() {
	std::printf("11. a packed engine makes way for a foundation\n");
	Simulation s;
	setup(s);
	uint32_t cat = s.spawn_unit(0.0f, 0.0f, 0, UNIT_CATAPULT); // in the middle of the site
	run(s, 1.0f);
	check(pack_of(s, cat) == PACK_PACKED, "catapult idle and packed");
	const uint32_t builder = s.spawn_unit(0.0f, -12.0f, 0, UNIT_FEMALE_CITIZEN);
	const uint32_t f = s.spawn_building_world(0.0f, 0.0f, BLD_BARRACKS, 0, 0.0f);
	s.order_build_or_repair(&builder, 1, f);
	run(s, 20.0f);
	float progress = 0.0f;
	for (const auto &b : s.buildings()) if (b.id == f) progress = b.build_progress;
	std::printf("  catapult at (%.1f, %.1f), barracks %.0f%% built\n", x_of(s, cat), z_of(s, cat), 100.0f * progress);
	check(progress > 0.05f, "the foundation started once the catapult had walked off");
}

int main(int argc, char **argv) {
	std::string save = (argc > 1) ? argv[1] : "/tmp/rts0_test_pack.sav";
	test_who_packs();
	test_attack_in_range();
	test_move_while_unpacked();
	test_walk_cancels_unpack();
	test_out_of_range();
	test_save_load(save.c_str());
	test_lockstep();
	test_retreat_under_fire();
	test_garrison_packs_first();
	test_stop_cancels_pack();
	test_packed_leaves_foundation();
	if (g_failures) {
		std::printf("test_pack: FAIL (%d)\n", g_failures);
		return 1;
	}
	std::printf("test_pack: PASS\n");
	return 0;
}
