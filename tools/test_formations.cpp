// RTS0 formation test (headless: no SCons, no Godot).
// Checks the port of 0 A.D.'s Formation.js, the formation parts of Commands.js and UnitAI.js,
// and the special/formations templates (src/simulation/formation.h, formation_data.h).
// Build and run:
//   g++ -std=c++20 -O2 -Isrc tools/test_formations.cpp src/simulation/simulation.cpp \
//     src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//     src/network/net_host.cpp -lpthread -o /tmp/test_formations && /tmp/test_formations
//
//  1. Templates and unlock rules: 17 templates with 0 A.D.'s numbers; Player.js formations per
//     civ (phalanx, syntagma, testudo, anti-cavalry); UnitAI/Formations per unit.
//  2. ComputeFormationOffsets layouts: box, close order, wedge, flank's centre gap, column.
//  3. The formation command forms units up in place; too few or the wrong units don't form.
//  4. A formation walks as one: at its slowest member's pace, in column for long walks,
//     in its own shape at the goal.
//  5. Attack-walk: the formation halts while its members fight, then walks on.
//  6. One unit ordered alone leaves; the null formation disbands; deaths below the minimum disband.
//  7. Save/load mid-walk keeps the controllers and the hash.
//  8. Lockstep command 8 forms up the sender's own units only.
//  Exit: 0 = PASS, 1 = FAIL.
#define private public
#include "simulation/simulation.h"
#undef private
#include "network/turn_manager.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace rtso;

static constexpr float DT = Simulation::FIXED_STEP; // the lockstep sim step

static int g_failures = 0;
static void check(bool ok, const char *what) {
	std::printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
	if (!ok) ++g_failures;
}

static void setup(Simulation &s, uint8_t civ0 = CIV_ATHENIANS) {
	s.set_sim_seed(4242);
	s.reset();
	s.set_victory_condition(VICTORY_ENDLESS);
	s.set_team_civ(0, civ0);
	s.set_team_civ(1, CIV_ROMANS);
	s.spawn_building_world(-56.0f, -56.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(56.0f, 56.0f, BLD_TOWN_CENTER, 1, 1.0f);
}

static int idx_of(const Simulation &s, uint32_t uid) {
	const auto u = s.units();
	for (size_t i = 0; i < u.count; ++i)
		if (u.id[i] == uid) return (int)i;
	return -1;
}
static float x_of(const Simulation &s, uint32_t uid) { return s.units().pos_x[idx_of(s, uid)]; }
static float z_of(const Simulation &s, uint32_t uid) { return s.units().pos_z[idx_of(s, uid)]; }
static uint8_t form_of(const Simulation &s, uint32_t uid) { return s.units().formation[idx_of(s, uid)]; }

static void run(Simulation &s, float seconds) {
	int n = (int)std::lround(seconds / DT);
	for (int i = 0; i < n; ++i) s.tick(DT);
}

static std::vector<uint32_t> spawn_block(Simulation &s, int n, float x0, float z0, uint8_t team, UnitType t,
		uint16_t tmpl = CIV_UNIT_TEMPLATE_NONE) {
	std::vector<uint32_t> ids;
	for (int i = 0; i < n; ++i) {
		uint32_t id = s.spawn_unit(x0 + (i % 5) * 1.6f, z0 + (i / 5) * 1.6f, team, t, 0.0f, tmpl);
		s.order_set_stance(&id, 1, STANCE_PASSIVE);
		ids.push_back(id);
	}
	return ids;
}

static uint16_t roster(const char *id) {
	for (uint16_t i = 0; i < CIV_UNIT_TEMPLATE_COUNT; ++i)
		if (!std::strcmp(civ_unit_template(i).id, id)) return i;
	return CIV_UNIT_TEMPLATE_NONE;
}

static const Simulation::FormationController *ctrl(const Simulation &s, uint32_t fid) {
	for (const auto &fc : s.formations())
		if (fc.id == fid) return &fc;
	return nullptr;
}

static void centroid(const Simulation &s, const std::vector<uint32_t> &ids, float &cx, float &cz) {
	cx = cz = 0.0f;
	for (uint32_t id : ids) { cx += x_of(s, id); cz += z_of(s, id); }
	cx /= ids.size();
	cz /= ids.size();
}

// Largest distance of a member from where its slot is now.
static float max_slot_error(const Simulation &s, const Simulation::FormationController &fc) {
	float worst = 0.0f;
	for (const FormationSlot &sl : fc.slots) {
		float wx, wz;
		formation_slot_world(fc.x, fc.z, fc.rot, sl.x, sl.y, wx, wz);
		worst = std::max(worst, std::hypot(x_of(s, sl.ent) - wx, z_of(s, sl.ent) - wz));
	}
	return worst;
}

static void test_templates() {
	std::printf("1. templates and unlock rules\n");
	const FormationTemplate &box = formation_template(FORMATION_BOX);
	check(!std::strcmp(box.path, "special/formations/box") && box.required_members == 4 &&
			box.sorting_order == FSORT_TO_THE_CENTER, "box: 4 members, fillToTheCenter");
	const FormationTemplate &ph = formation_template(FORMATION_PHALANX);
	check(ph.required_members == 10 && ph.width_mult == 0.6f && ph.max_columns == 20 && ph.max_rows == 8 && ph.shift_rows,
			"phalanx: 10 members, 0.6 separation, up to 20 columns and 8 rows");
	const FormationTemplate &sy = formation_template(FORMATION_SYNTAGMA);
	check(sy.required_members == 16 && sy.depth_mult == 0.55f && sy.sorting_classes[2] == FCLASS_ELITE,
			"syntagma: 16 members, sorted Hero Champion Elite Advanced Basic");
	const FormationTemplate &te = formation_template(FORMATION_TESTUDO);
	check(te.min_columns == 8 && te.max_columns == 8 && te.width_depth_ratio == 0.8f, "testudo: 8 columns");
	check(formation_template(FORMATION_SHIPS_COLUMN).speed_multiplier == 1.25f, "ships column: SpeedMultiplier 1.25");
	check(formation_template(FORMATION_FLANK).center_gap == 12.0f, "flank: CenterGap 12");

	auto civ_has = [](uint8_t civ, uint8_t f) { return (civ_formation_mask(civ) >> f) & 1u; };
	check(civ_has(CIV_ATHENIANS, FORMATION_PHALANX) && !civ_has(CIV_ATHENIANS, FORMATION_SYNTAGMA), "Athenians: phalanx, no syntagma");
	check(civ_has(CIV_MACEDONIANS, FORMATION_PHALANX) && civ_has(CIV_MACEDONIANS, FORMATION_SYNTAGMA), "Macedonians: phalanx and syntagma");
	check(civ_has(CIV_ROMANS, FORMATION_TESTUDO) && civ_has(CIV_ROMANS, FORMATION_ANTI_CAVALRY) &&
			!civ_has(CIV_ROMANS, FORMATION_PHALANX), "Romans: testudo and anti-cavalry, no phalanx");
	check(civ_has(CIV_HAN, FORMATION_ANTI_CAVALRY) && !civ_has(CIV_HAN, FORMATION_TESTUDO), "Han: anti-cavalry");
	check(civ_has(CIV_KUSHITES, FORMATION_SYNTAGMA) && !civ_has(CIV_KUSHITES, FORMATION_PHALANX), "Kushites: syntagma");
	check(!civ_has(CIV_BRITONS, FORMATION_PHALANX) && !civ_has(CIV_BRITONS, FORMATION_TESTUDO) &&
			civ_has(CIV_BRITONS, FORMATION_WEDGE) && civ_has(CIV_BRITONS, FORMATION_BOX), "Britons: only the common formations");

	auto unit_has = [](const char *id, uint8_t f) {
		const uint16_t r = roster(id);
		return r != CIV_UNIT_TEMPLATE_NONE && ((formation_unit_table()[r].formations >> f) & 1u);
	};
	check(unit_has("athen/infantry_spearman_b", FORMATION_PHALANX), "Athenian Hoplite may form a phalanx (hoplite mixin)");
	check(!unit_has("gaul/infantry_spearman_b", FORMATION_PHALANX), "a Gallic spearman may not");
	check(unit_has("mace/infantry_pikeman_b", FORMATION_SYNTAGMA), "Macedonian pikeman: syntagma");
	check(unit_has("rome/infantry_swordsman_b", FORMATION_TESTUDO), "Roman swordsman: testudo");
	check(unit_has("rome/infantry_spearman_b", FORMATION_ANTI_CAVALRY), "Roman spearman: anti-cavalry");
	check(unit_has("athen/cavalry_javelineer_b", FORMATION_WEDGE) && unit_has("athen/cavalry_javelineer_b", FORMATION_SKIRMISH),
			"ranged cavalry: wedge and skirmish");
	check(!unit_has("athen/infantry_slinger_b", FORMATION_WEDGE) && unit_has("athen/infantry_slinger_b", FORMATION_SKIRMISH),
			"slinger: skirmish, no wedge");
	check(formation_unit_table()[roster("athen/support_female_citizen")].formations == 0, "women take no formations");
	check(formation_unit_table()[roster("athen/ship_arrow")].formations == 0, "ships take no formations (0 A.D. disables them)");
}

static std::vector<FormationMember> members(int n, float w = 1.0f, uint16_t cls = FCLASS_INFANTRY | FCLASS_MELEE) {
	std::vector<FormationMember> m;
	for (int i = 0; i < n; ++i) m.push_back(FormationMember{(uint32_t)i + 1, cls, (float)(i % 4), (float)(i / 4), w, w});
	return m;
}

static void test_offsets() {
	std::printf("2. ComputeFormationOffsets layouts\n");
	FormationRng rng{1};
	uint8_t rows = 0;
	auto box = compute_formation_offsets(formation_template(FORMATION_BOX), false, members(9), 0, 0, 0, rng, &rows);
	float sx = 0, sy = 0;
	for (auto &o : box) { sx += o.x; sy += o.y; }
	check(box.size() == 9 && rows == 3, "box of 9 is 3 rows of 3");
	check(std::fabs(sx) < 1e-4f && std::fabs(sy) < 1e-4f, "offsets average to the centre");
	float maxx = 0;
	for (auto &o : box) maxx = std::max(maxx, std::fabs(o.x));
	check(std::fabs(maxx - 1.0f) < 1e-4f, "box spacing is one footprint");

	auto line = compute_formation_offsets(formation_template(FORMATION_LINE), false, members(12), 0, 0, 0, rng, &rows);
	check(line.size() == 12 && rows == 2, "close order of 12 is 2 ranks of 6 (WidthDepthRatio 4)");

	auto wedge = compute_formation_offsets(formation_template(FORMATION_WEDGE), false, members(6, 1.0f, FCLASS_CAVALRY), 0, 0, 0, rng, &rows);
	check(wedge.size() == 6 && rows == 3, "wedge of 6 is a triangle of 3 rows (1, 2, 3)");

	auto flank = compute_formation_offsets(formation_template(FORMATION_FLANK), false, members(8), 0, 0, 0, rng);
	float minx = 1e9f;
	for (auto &o : flank) minx = std::min(minx, std::fabs(o.x));
	check(flank.size() == 8 && minx >= 12.0f * FORMATION_METRE / 2.0f - 1e-3f, "flank leaves a centre gap");

	auto col = compute_formation_offsets(formation_template(FORMATION_BOX), true, members(9), 0, 0, 0, rng, &rows);
	check(col.size() == 9 && rows == 3, "marching column: 3 abreast");

	// Sorting: in a box, melee take the outer places and ranged the centre (fillToTheCenter).
	std::vector<FormationMember> mixed = members(9);
	mixed[4].classes = FCLASS_INFANTRY | FCLASS_RANGED;
	auto mb = compute_formation_offsets(formation_template(FORMATION_BOX), false, mixed, 0, 0, 0, rng);
	bool ranged_centre = false;
	for (auto &o : mb) if (o.ent == 5) ranged_centre = std::fabs(o.x) < 1e-4f && std::fabs(o.y) < 1e-4f;
	check(ranged_centre, "box puts the ranged unit in the centre");

	// Every member gets exactly one slot.
	std::vector<int> seen(10, 0);
	for (auto &o : box) seen[o.ent]++;
	bool once = true;
	for (int i = 1; i <= 9; ++i) once = once && seen[i] == 1;
	check(once, "each member takes one slot");
}

static void test_form_up() {
	std::printf("3. formation command\n");
	Simulation s;
	setup(s, CIV_ATHENIANS);
	auto hop = spawn_block(s, 12, -10, -10, 0, UNIT_SPEARMAN);
	check(s.can_move_into_formation(hop.data(), hop.size(), FORMATION_PHALANX), "12 Athenian hoplites can form a phalanx");
	s.order_set_formation(hop.data(), hop.size(), FORMATION_PHALANX);
	check(s.formations().size() == 1 && form_of(s, hop[0]) == FORMATION_PHALANX, "one phalanx controller");
	run(s, 8.0f);
	const auto *fc = s.formations().empty() ? nullptr : &s.formations()[0];
	check(fc && max_slot_error(s, *fc) < 1.0f, "members stand in their slots");

	auto few = spawn_block(s, 5, 10, 10, 0, UNIT_SPEARMAN);
	check(!s.can_move_into_formation(few.data(), few.size(), FORMATION_PHALANX), "5 hoplites are too few for a phalanx");
	s.order_set_formation(few.data(), few.size(), FORMATION_PHALANX);
	check(s.formations().size() == 1 && form_of(s, few[0]) == FORMATION_NONE, "so they don't form");

	auto women = spawn_block(s, 6, 20, -20, 0, UNIT_FEMALE_CITIZEN);
	s.order_set_formation(women.data(), women.size(), FORMATION_BOX);
	check(form_of(s, women[0]) == FORMATION_NONE, "women don't form up");

	Simulation r;
	setup(r, CIV_ROMANS);
	auto legion = spawn_block(r, 16, -10, -10, 0, UNIT_SWORDSMAN);
	r.order_set_formation(legion.data(), legion.size(), FORMATION_PHALANX);
	check(r.formations().empty(), "Romans can't form a phalanx");
	r.order_set_formation(legion.data(), legion.size(), FORMATION_TESTUDO);
	check(r.formations().size() == 1 && form_of(r, legion[0]) == FORMATION_TESTUDO, "16 Roman swordsmen form a testudo");
	run(r, 8.0f);
	check(max_slot_error(r, r.formations()[0]) < 0.5f, "and hold its tight slots (members don't push each other)");

	// Changing the template of a whole formation reuses its controller (Formation.js LoadFormation).
	const uint32_t fid = r.formations()[0].id;
	r.order_set_formation(legion.data(), legion.size(), FORMATION_BOX);
	check(r.formations().size() == 1 && r.formations()[0].id == fid && r.formations()[0].tmpl == FORMATION_BOX,
			"switching to box keeps the controller");
}

static void test_walk() {
	std::printf("4. walking\n");
	Simulation s;
	setup(s, CIV_ATHENIANS);
	auto inf = spawn_block(s, 8, -30, -30, 0, UNIT_SPEARMAN);
	auto cav = spawn_block(s, 4, -30, -24, 0, UNIT_CAVALRY);
	std::vector<uint32_t> all = inf;
	all.insert(all.end(), cav.begin(), cav.end());
	s.order_set_formation(all.data(), all.size(), FORMATION_BOX);
	run(s, 6.0f);
	check(s.formations().size() == 1, "infantry and cavalry form one box");
	const uint32_t fid = s.formations()[0].id;
	s.cmd_move(all.data(), all.size(), 30.0f, 30.0f);
	const auto *fc = ctrl(s, fid);
	check(fc && fc->order == Simulation::FORDER_WALK, "the walk goes to the controller");
	check(fc && fc->columnar, "a long walk marches in column");
	run(s, 8.0f);
	float cx, cz;
	centroid(s, all, cx, cz);
	float worst = 0.0f;
	for (uint32_t id : all) worst = std::max(worst, std::hypot(x_of(s, id) - cx, z_of(s, id) - cz));
	check(worst < 12.0f, "the column stays together (cavalry don't run ahead)");
	const float inf_speed = s.units_.speed[idx_of(s, inf[0])];
	const float walked = std::hypot(cx + 30.0f, cz + 27.0f);
	check(walked < inf_speed * 8.0f + 2.0f, "it walks at the infantry's pace");
	run(s, 40.0f);
	fc = ctrl(s, fid);
	centroid(s, all, cx, cz);
	check(std::hypot(cx - 30.0f, cz - 30.0f) < 3.0f, "it arrives");
	check(fc && fc->order == Simulation::FORDER_IDLE && !fc->columnar, "and stands as a box again");
	check(fc && max_slot_error(s, *fc) < 1.5f, "members are in their box slots");
}

static void test_attack_walk() {
	std::printf("5. attack-walk\n");
	Simulation s;
	setup(s, CIV_ATHENIANS);
	auto inf = spawn_block(s, 8, -30, 0, 0, UNIT_SWORDSMAN);
	s.order_set_stance(inf.data(), inf.size(), STANCE_AGGRESSIVE); // passive units never fight (g_Stances)
	s.order_set_formation(inf.data(), inf.size(), FORMATION_BOX);
	run(s, 4.0f);
	// Two passive enemy dummies on the way.
	std::vector<uint32_t> foes;
	for (int k = 0; k < 2; ++k) {
		uint32_t id = s.spawn_unit(0.0f, (float)k * 1.5f, 1, UNIT_SPEARMAN, 1.0e6f);
		s.order_set_stance(&id, 1, STANCE_PASSIVE);
		foes.push_back(id);
	}
	s.order_attack_move(inf.data(), inf.size(), 30.0f, 0.0f);
	const uint32_t fid = s.formations()[0].id;
	check(ctrl(s, fid)->order == Simulation::FORDER_ATTACK_WALK, "the formation attack-walks");
	run(s, 14.0f);
	float cx, cz;
	centroid(s, inf, cx, cz);
	int fighting = 0;
	for (uint32_t id : inf) fighting += s.units_.action_state[idx_of(s, id)] == ACTION_ATTACK;
	check(fighting > 0 && cx < 8.0f, "it stops at the enemy and fights");
	check(ctrl(s, fid) && ctrl(s, fid)->order == Simulation::FORDER_ATTACK_WALK, "still attack-walking while it fights");
	for (uint32_t id : foes) {
		const int i = idx_of(s, id);
		s.units_.hp[i] = 0.0f;
		s.units_.alive[i] = 0;
		s.units_.action_state[i] = ACTION_DYING;
	}
	run(s, 30.0f);
	centroid(s, inf, cx, cz);
	check(std::hypot(cx - 30.0f, cz) < 3.0f, "then walks on to the goal");
}

static void test_leave() {
	std::printf("6. leaving and disbanding\n");
	Simulation s;
	setup(s, CIV_ATHENIANS);
	auto inf = spawn_block(s, 6, -10, -10, 0, UNIT_SWORDSMAN);
	s.order_set_formation(inf.data(), inf.size(), FORMATION_BOX);
	check(s.formations().size() == 1 && s.formations()[0].members.size() == 6, "6 swordsmen in a box");
	s.cmd_move(&inf[0], 1, 20.0f, 0.0f);
	check(s.formations()[0].members.size() == 5 && form_of(s, inf[0]) == FORMATION_NONE, "one ordered alone leaves");
	s.order_set_formation(inf.data() + 1, 5, FORMATION_NONE);
	check(s.formations().empty() && form_of(s, inf[1]) == FORMATION_NONE, "the null formation disbands it");

	s.order_set_formation(inf.data(), inf.size(), FORMATION_BOX);
	check(s.formations().size() == 1, "re-formed");
	// Kill three: 3 left is below the box's 4.
	for (int k = 0; k < 3; ++k) {
		const int i = idx_of(s, inf[k]);
		s.units_.hp[i] = 0.0f;
		s.units_.alive[i] = 0;
		s.units_.action_state[i] = ACTION_DYING;
	}
	run(s, 1.0f);
	check(s.formations().empty(), "deaths below RequiredMemberCount disband it");
}

static void test_save_load(const char *path) {
	std::printf("7. save/load mid-walk\n");
	Simulation a;
	setup(a, CIV_ATHENIANS);
	auto inf = spawn_block(a, 10, -20, -20, 0, UNIT_SPEARMAN);
	a.order_set_formation(inf.data(), inf.size(), FORMATION_LINE);
	run(a, 3.0f);
	a.cmd_move(inf.data(), inf.size(), 25.0f, 20.0f);
	run(a, 4.0f);
	check(a.save_state(path), "saved");
	Simulation b;
	check(b.load_state(path), "loaded");
	check(b.formations().size() == 1 && b.formations()[0].members == a.formations()[0].members &&
			b.formations()[0].order == a.formations()[0].order, "the controller comes back");
	run(a, 10.0f);
	run(b, 10.0f);
	bool same = true;
	for (uint32_t id : inf) same = same && x_of(a, id) == x_of(b, id) && z_of(a, id) == z_of(b, id);
	check(same, "both runs walk the same afterwards");
}

static void test_lockstep() {
	std::printf("8. lockstep formation command\n");
	Simulation s;
	setup(s, CIV_ATHENIANS);
	auto mine = spawn_block(s, 6, -20, -20, 0, UNIT_SWORDSMAN);
	auto theirs = spawn_block(s, 6, 20, 20, 1, UNIT_SWORDSMAN);
	TurnManager tm;
	check(static_cast<uint8_t>(NetCommandType::NET_CMD_FORMATION) == 8, "NET_CMD_FORMATION is command 8");
	NetCommand c;
	c.client_id = 0;
	c.cmd_type = (uint8_t)NetCommandType::NET_CMD_FORMATION;
	c.entity_ids = mine;
	c.entity_ids.insert(c.entity_ids.end(), theirs.begin(), theirs.end());
	c.param = FORMATION_BOX;
	tm.post_local_command(c);
	for (int i = 0; i < 20; ++i) tm.tick(TurnManager::TURN_DURATION, s);
	check(form_of(s, mine[0]) == FORMATION_BOX, "command 8 forms up the sender's units");
	check(form_of(s, theirs[0]) == FORMATION_NONE, "and not another player's");
}

int main(int argc, char **argv) {
	std::string save = (argc > 1) ? argv[1] : "/tmp/rts0_test_formations.sav";
	test_templates();
	test_offsets();
	test_form_up();
	test_walk();
	test_attack_walk();
	test_leave();
	test_save_load(save.c_str());
	test_lockstep();
	if (g_failures) {
		std::printf("test_formations: FAIL (%d)\n", g_failures);
		return 1;
	}
	std::printf("test_formations: PASS\n");
	return 0;
}
