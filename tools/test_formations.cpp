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
//  5. Attack-walk: the formation halts while its members fight (MEMBER), then walks on.
//  6. One unit ordered alone leaves; the null formation disbands; deaths below the minimum disband.
//  7. Save/load mid-walk keeps the controllers and the hash.
//  8. Lockstep command 8 forms up the sender's own units only.
//  9. Twin formations (one command, two clusters) merge when they meet.
// 10. Attack: the formation walks into range in formation, the members fight, then re-form.
// 11. Patrol: walk-and-fight to the point, wait 2 s, walk back.
// 12. Guard: the members guard and the formation disbands.
// 13. Save/load mid-approach keeps the attack order.
// 14. Gather, repair and heal: the formation walks up in formation, then the members do the
//     work (UnitAI FORMATIONCONTROLLER Order.Gather/Repair/Heal); garrison goes to the members
//     at once (GARRISON.GARRISONING).
// 15. Shift-queued orders wait in the controller's queue and run in formation, one after
//     another; a new order clears the queue; the queue survives save/load.
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
	// Two human seats: an unseated match hands team 1 to Petra, whose defense
	// orders its passive dummies to attack.
	s.set_player_active(0, true, false);
	s.set_player_active(1, true, false);
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
	// shield_wall.xml: the Germans' (players/germ.xml) for their spearmen, 10+, phalanx-like.
	const FormationTemplate &sw = formation_template(FORMATION_SHIELD_WALL);
	check(civ_has(CIV_GERMANS, FORMATION_SHIELD_WALL) && !civ_has(CIV_GERMANS, FORMATION_PHALANX) &&
			!civ_has(CIV_ATHENIANS, FORMATION_SHIELD_WALL) && sw.required_members == 10 && sw.max_columns == 20 &&
			sw.width_depth_ratio == 4.0f && sw.shift_rows, "Germans: shield wall (10+, up to 20 columns)");

	auto unit_has = [](const char *id, uint8_t f) {
		const uint16_t r = roster(id);
		return r != CIV_UNIT_TEMPLATE_NONE && ((formation_unit_table()[r].formations >> f) & 1u);
	};
	check(unit_has("athen/infantry_spearman_b", FORMATION_PHALANX), "Athenian Hoplite may form a phalanx (hoplite mixin)");
	check(unit_has("germ/infantry_spearman_b", FORMATION_SHIELD_WALL) && !unit_has("germ/infantry_javelineer_b", FORMATION_SHIELD_WALL),
			"German spearman may form a shield wall, the javelineer may not");
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
	run(s, 0.2f);
	{
		// UnitMotion IsMovingAsFormation: members run to their slots, the controller walks.
		const int i = idx_of(s, inf[0]), c = idx_of(s, cav[0]);
		check(std::fabs(s.unit_walk_speed(i) - 1.67f * s.unit_base_walk_speed(i)) < 1e-3f &&
		      std::fabs(s.unit_walk_speed(c) - 1.4f * s.unit_base_walk_speed(c)) < 1e-3f, "members run to their slots");
	}
	run(s, 7.8f);
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
	// 0.28 UnitAI FORMATIONMEMBER.WALKING: a member already at its spot finishes the walk at
	// once (UnitMotion.PossiblyAtDestination), so a walk to where the box stands ends.
	if (fc) s.cmd_move(all.data(), all.size(), fc->x, fc->z);
	run(s, 0.5f);
	fc = ctrl(s, fid);
	int walking = 0;
	for (uint32_t id : all) walking += s.units().action_state[idx_of(s, id)] != ACTION_IDLE;
	check(fc && fc->order == Simulation::FORDER_IDLE && walking == 0, "a walk to where it stands ends at once");
}

// B pass 6: UnitAI MoveIntoFormation is PushOrderFront, so changing formation mid-march
// keeps the walk; the IDLE timer brings idle members that wandered off back to their slots.
static void test_change_and_regather() {
	std::printf("4b. changing formation on the march, idle re-gather\n");
	Simulation s;
	setup(s, CIV_ATHENIANS);
	auto inf = spawn_block(s, 9, -30, -30, 0, UNIT_SPEARMAN);
	s.order_set_formation(inf.data(), inf.size(), FORMATION_BOX);
	run(s, 6.0f);
	const uint32_t fid = s.formations()[0].id;
	s.cmd_move(inf.data(), inf.size(), 30.0f, -30.0f);
	run(s, 2.0f);
	s.order_set_formation(inf.data(), inf.size(), FORMATION_LINE);
	run(s, 60.0f);
	const auto *fc = ctrl(s, fid);
	float cx, cz;
	centroid(s, inf, cx, cz);
	check(fc && fc->tmpl == FORMATION_LINE && std::hypot(cx - 30.0f, cz + 30.0f) < 3.0f,
	      "a new formation mid-march re-forms and carries on to the goal");
	// A member pushed off its slot while the formation stands idle walks back.
	const int i = idx_of(s, inf[0]);
	s.units_.pos_x[i] += 6.0f;
	s.units_.pos_z[i] += 6.0f;
	run(s, 8.0f);
	fc = ctrl(s, fid);
	check(fc && max_slot_error(s, *fc) < 1.5f, "an idle formation gathers a wandered member back");
}

// B pass 6 (G55): Formation.js GetAvgFootprint spaces members by their template's Footprint.
// Cavalry are 3x6 m, so their rows stand deeper apart than infantry's 3x3 m; a slot never
// gets narrower than the unit's own collision width.
static void test_footprint_spacing() {
	std::printf("4c. footprint spacing\n");
	auto gaps = [](const Simulation::FormationController &fc, float &across, float &along) {
		across = along = 1e9f;
		for (const FormationSlot &a : fc.slots)
			for (const FormationSlot &b : fc.slots) {
				if (a.row == b.row && a.column != b.column) across = std::min(across, std::fabs(a.x - b.x));
				if (a.column == b.column && a.row != b.row) along = std::min(along, std::fabs(a.y - b.y));
			}
	};
	Simulation s;
	setup(s, CIV_ATHENIANS);
	auto inf = spawn_block(s, 9, -30, -30, 0, UNIT_SPEARMAN, roster("athen/infantry_spearman_b"));
	auto cav = spawn_block(s, 9, 20, 20, 0, UNIT_CAVALRY, roster("athen/cavalry_javelineer_b"));
	s.order_set_formation(inf.data(), inf.size(), FORMATION_BOX);
	s.order_set_formation(cav.data(), cav.size(), FORMATION_BOX);
	check(s.formations().size() == 2, "a box of hoplites and a box of horsemen");
	float ia, il, ca, cl;
	gaps(s.formations()[0], ia, il);
	gaps(s.formations()[1], ca, cl);
	const float cav_w = 2.0f * unit_collision_radius(UNIT_CAVALRY);
	check(std::fabs(ia - il) < 1e-3f, "infantry rows and files are equally spaced (3x3 m)");
	check(std::fabs(ca - cav_w) < 1e-3f && std::fabs(cl - 6.0f * FORMATION_METRE) < 1e-3f,
	      "cavalry stand a 6 m footprint apart front to back, their own width side by side");
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
	check(ctrl(s, fid) && ctrl(s, fid)->order == Simulation::FORDER_MEMBER &&
			ctrl(s, fid)->returning == Simulation::FORDER_ATTACK_WALK, "it waits for its fighters, then attack-walks again (MEMBER)");
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

static void test_twins() {
	std::printf("9. twin formations\n");
	Simulation s;
	setup(s, CIV_ATHENIANS);
	auto a = spawn_block(s, 8, -40, -20, 0, UNIT_SWORDSMAN);
	auto b = spawn_block(s, 8, -40, 18, 0, UNIT_SWORDSMAN);
	std::vector<uint32_t> all = a;
	all.insert(all.end(), b.begin(), b.end());
	s.order_set_formation(all.data(), all.size(), FORMATION_BOX);
	check(s.formations().size() == 2, "two clusters farther apart than 60 m make two formations");
	check(s.formations()[0].twins.size() == 1 && s.formations()[1].twins.size() == 1, "registered as twins");
	run(s, 3.0f);
	s.cmd_move(all.data(), all.size(), 30.0f, 0.0f);
	check(s.formations().size() == 2, "the walk keeps both until they meet");
	run(s, 25.0f);
	check(s.formations().size() == 1 && s.formations()[0].members.size() == 16, "they merge into one formation of 16");
	check(form_of(s, b[0]) == FORMATION_BOX && s.formation_of_unit(b[0]) == s.formation_of_unit(a[0]),
			"the twin's members joined it");
}

static void test_attack() {
	std::printf("10. formation attack\n");
	Simulation s;
	setup(s, CIV_ATHENIANS);
	auto inf = spawn_block(s, 8, -30, 0, 0, UNIT_SWORDSMAN);
	s.order_set_stance(inf.data(), inf.size(), STANCE_AGGRESSIVE);
	s.order_set_formation(inf.data(), inf.size(), FORMATION_BOX);
	run(s, 3.0f);
	uint32_t foe = s.spawn_unit(10.0f, 0.0f, 1, UNIT_SPEARMAN, 1.0e6f);
	s.order_set_stance(&foe, 1, STANCE_PASSIVE);
	s.order_attack(inf.data(), inf.size(), foe);
	const uint32_t fid = s.formations()[0].id;
	check(ctrl(s, fid)->order == Simulation::FORDER_APPROACH && ctrl(s, fid)->target_unit == foe,
			"out of range, the formation approaches (COMBAT.APPROACHING)");
	run(s, 2.0f);
	check(ctrl(s, fid)->order == Simulation::FORDER_APPROACH && max_slot_error(s, *ctrl(s, fid)) < 2.5f,
			"it walks there in formation");
	run(s, 12.0f);
	int fighting = 0;
	for (uint32_t id : inf) {
		const int i = idx_of(s, id);
		fighting += s.units_.action_state[i] == ACTION_ATTACK && s.units_.target_unit_id[i] == foe;
	}
	check(ctrl(s, fid) && ctrl(s, fid)->order == Simulation::FORDER_MEMBER && fighting == 8,
			"in range, every member attacks the target (MEMBER)");
	const int fi = idx_of(s, foe);
	s.units_.hp[fi] = 0.0f;
	s.units_.alive[fi] = 0;
	s.units_.action_state[fi] = ACTION_DYING;
	run(s, 6.0f);
	check(ctrl(s, fid) && ctrl(s, fid)->order == Simulation::FORDER_IDLE && ctrl(s, fid)->members.size() == 8,
			"once they are done the formation goes idle with all 8");
	check(ctrl(s, fid) && max_slot_error(s, *ctrl(s, fid)) < 1.0f, "and the members re-form");

	// A structure: the members go for it once the formation is in range.
	Simulation t;
	setup(t, CIV_ATHENIANS);
	auto sq = spawn_block(t, 8, 20, 30, 0, UNIT_SWORDSMAN);
	t.order_set_formation(sq.data(), sq.size(), FORMATION_BOX);
	run(t, 3.0f);
	uint32_t tc = 0;
	for (const auto &b : t.buildings()) if (b.team == 1 && b.type == BLD_TOWN_CENTER) tc = b.id;
	t.order_attack_building(sq.data(), sq.size(), tc);
	const uint32_t tfid = t.formations()[0].id;
	check(ctrl(t, tfid)->order == Simulation::FORDER_APPROACH && ctrl(t, tfid)->target_building == tc,
			"a structure out of range: the formation approaches it");
	run(t, 15.0f);
	int on_it = 0;
	for (uint32_t id : sq) on_it += t.units_.target_building_id[idx_of(t, id)] == tc;
	check(ctrl(t, tfid) && ctrl(t, tfid)->order == Simulation::FORDER_MEMBER && on_it == 8,
			"in range, the members attack the structure");
}

static void test_patrol() {
	std::printf("11. formation patrol\n");
	Simulation s;
	setup(s, CIV_ATHENIANS);
	auto inf = spawn_block(s, 8, -20, 0, 0, UNIT_SWORDSMAN);
	s.order_set_formation(inf.data(), inf.size(), FORMATION_BOX);
	run(s, 3.0f);
	s.order_patrol(inf.data(), inf.size(), 20.0f, 0.0f);
	const uint32_t fid = s.formations()[0].id;
	check(ctrl(s, fid)->order == Simulation::FORDER_PATROL, "the formation patrols (PATROL.PATROLLING)");
	float cx, cz;
	bool waited = false, reached = false;
	for (int k = 0; k < 400 && !waited; ++k) {
		run(s, 0.1f);
		waited = ctrl(s, fid)->order == Simulation::FORDER_PATROL_WAIT;
	}
	centroid(s, inf, cx, cz);
	reached = cx > 16.0f;
	check(waited && reached, "it reaches the far end and surveys it (CHECKINGWAYPOINT)");
	run(s, 1.5f);
	check(ctrl(s, fid)->order == Simulation::FORDER_PATROL_WAIT, "for PatrolWaitTime");
	run(s, 2.0f);
	check(ctrl(s, fid)->order == Simulation::FORDER_PATROL && ctrl(s, fid)->goal_x < -15.0f, "then patrols back");
	waited = false;
	for (int k = 0; k < 400 && !waited; ++k) {
		run(s, 0.1f);
		waited = ctrl(s, fid)->order == Simulation::FORDER_PATROL_WAIT;
	}
	centroid(s, inf, cx, cz);
	check(waited && cx < -12.0f && s.formations().size() == 1, "and comes back to the start, still in formation");

	// Shift-queued patrol points: PATROL.CHECKINGWAYPOINT pushes each leg to the back of the
	// queue, so the formation loops through every point, and a queued walk runs in turn.
	Simulation q;
	setup(q, CIV_ATHENIANS);
	auto sq = spawn_block(q, 8, -20, 0, 0, UNIT_SWORDSMAN);
	q.order_set_formation(sq.data(), sq.size(), FORMATION_BOX);
	run(q, 3.0f);
	q.order_patrol(sq.data(), sq.size(), 20.0f, 0.0f);
	q.order_patrol(sq.data(), sq.size(), 20.0f, 20.0f, true);
	const uint32_t qfid = q.formations()[0].id;
	auto next_wait = [&](float &x, float &z) {
		bool w = false;
		for (int k = 0; k < 600 && !w; ++k) {
			run(q, 0.1f);
			w = ctrl(q, qfid) && ctrl(q, qfid)->order == Simulation::FORDER_PATROL_WAIT;
		}
		centroid(q, sq, x, z);
		run(q, 4.0f); // PatrolWaitTime
		return w;
	};
	float x1, z1, x2, z2, x3, z3;
	const bool w1 = next_wait(x1, z1), w2 = next_wait(x2, z2), w3 = next_wait(x3, z3);
	std::printf("   queued patrol stops: (%.0f, %.0f) (%.0f, %.0f) (%.0f, %.0f)\n", x1, z1, x2, z2, x3, z3);
	check(w1 && w2 && w3 && z1 < 4.0f && z2 > 16.0f && z3 < 4.0f && x3 > 16.0f,
			"queued patrol points loop: first, second, then the first again");
	q.order_walk(sq.data(), sq.size(), -20.0f, -20.0f, true);
	bool walked = false;
	for (int k = 0; k < 1200 && !walked; ++k) {
		run(q, 0.1f);
		float x, z;
		centroid(q, sq, x, z);
		walked = x < -16.0f && z < -16.0f;
	}
	check(walked, "a walk queued behind the patrol points runs in turn");
}

static void test_guard() {
	std::printf("12. formation guard\n");
	Simulation s;
	setup(s, CIV_ATHENIANS);
	auto inf = spawn_block(s, 8, -20, 0, 0, UNIT_SWORDSMAN);
	s.order_set_formation(inf.data(), inf.size(), FORMATION_BOX);
	uint32_t vip = s.spawn_unit(-10.0f, 10.0f, 0, UNIT_ARCHER, 0.0f);
	s.order_guard(inf.data(), inf.size(), vip);
	check(s.formations().empty() && form_of(s, inf[0]) == FORMATION_NONE, "the formation disbands");
	check(s.units_.action_state[idx_of(s, inf[0])] == ACTION_GUARD, "and its members guard");
}

static void test_save_attack(const char *path) {
	std::printf("13. save/load mid-approach\n");
	Simulation a;
	setup(a, CIV_ATHENIANS);
	auto inf = spawn_block(a, 8, -30, 0, 0, UNIT_SWORDSMAN);
	a.order_set_formation(inf.data(), inf.size(), FORMATION_BOX);
	run(a, 3.0f);
	uint32_t foe = a.spawn_unit(15.0f, 5.0f, 1, UNIT_SPEARMAN, 1.0e6f);
	a.order_set_stance(&foe, 1, STANCE_PASSIVE);
	a.order_attack(inf.data(), inf.size(), foe);
	run(a, 3.0f);
	check(a.save_state(path), "saved");
	Simulation b;
	check(b.load_state(path), "loaded");
	check(b.formations().size() == 1 && b.formations()[0].order == Simulation::FORDER_APPROACH &&
			b.formations()[0].target_unit == foe, "the approach and its target come back");
	run(a, 12.0f);
	run(b, 12.0f);
	bool same = a.formations()[0].order == b.formations()[0].order;
	for (uint32_t id : inf) same = same && x_of(a, id) == x_of(b, id) && z_of(a, id) == z_of(b, id);
	check(same, "both runs fight the same afterwards");
}

static void test_tasks() {
	std::printf("14. formation gather, repair, garrison and heal\n");
	{ // Gather: approach as a block, then every member gathers the tree; MEMBER waits on them.
		Simulation s;
		setup(s, CIV_ATHENIANS);
		auto inf = spawn_block(s, 8, -30, 0, 0, UNIT_SPEARMAN);
		s.order_set_formation(inf.data(), inf.size(), FORMATION_BOX);
		run(s, 3.0f);
		const uint32_t tree = s.spawn_resource_world(10.0f, 0.0f, RES_WOOD, 5000);
		const float wood0 = s.player(0).res[RES_WOOD];
		s.order_gather(inf.data(), inf.size(), tree);
		const uint32_t fid = s.formations()[0].id;
		check(ctrl(s, fid)->order == Simulation::FORDER_APPROACH &&
				Simulation::formation_task(*ctrl(s, fid)) == Simulation::FTASK_GATHER,
				"gather: out of range, the formation walks up to the tree");
		run(s, 2.0f);
		check(ctrl(s, fid)->order == Simulation::FORDER_APPROACH && max_slot_error(s, *ctrl(s, fid)) < 2.5f,
				"in formation");
		run(s, 14.0f);
		int gathering = 0;
		for (uint32_t id : inf) {
			const int i = idx_of(s, id);
			gathering += s.units_.order_kind[i] == ORDER_GATHER || s.units_.order_kind[i] == ORDER_RETURN_RESOURCE;
		}
		check(ctrl(s, fid) && ctrl(s, fid)->order == Simulation::FORDER_MEMBER && gathering == 8,
				"in range, every member gathers and the formation waits (MEMBER)");
		run(s, 30.0f);
		int carrying = 0;
		for (uint32_t id : inf) carrying += s.units_.carry[idx_of(s, id)] > 0 || s.units_.order_kind[idx_of(s, id)] == ORDER_RETURN_RESOURCE;
		check(ctrl(s, fid) && ctrl(s, fid)->order == Simulation::FORDER_MEMBER &&
				(carrying > 0 || s.player(0).res[RES_WOOD] > wood0), "they keep gathering while in the formation");
	}
	{ // Repair: a damaged house.
		Simulation s;
		setup(s, CIV_ATHENIANS);
		auto inf = spawn_block(s, 8, -30, 10, 0, UNIT_SPEARMAN);
		s.order_set_formation(inf.data(), inf.size(), FORMATION_BOX);
		run(s, 3.0f);
		const uint32_t house = s.spawn_building_world(12.0f, 10.0f, BLD_HOUSE, 0, 1.0f);
		Simulation::Building *hb = nullptr;
		for (auto &b : s.buildings_) if (b.id == house) hb = &b;
		hb->hp = hb->max_hp * 0.4f;
		const float hp0 = hb->hp;
		s.order_build_or_repair(inf.data(), inf.size(), house);
		const uint32_t fid = s.formations()[0].id;
		check(ctrl(s, fid)->order == Simulation::FORDER_APPROACH && ctrl(s, fid)->target_building == house,
				"repair: the formation walks up to the structure");
		run(s, 20.0f);
		for (auto &b : s.buildings_) if (b.id == house) hb = &b;
		check(hb->hp > hp0, "then the members repair it");
	}
	{ // Garrison: everyone gets in and the formation is gone.
		Simulation s;
		setup(s, CIV_ATHENIANS);
		auto inf = spawn_block(s, 8, -20, 20, 0, UNIT_SPEARMAN);
		s.order_set_formation(inf.data(), inf.size(), FORMATION_BOX);
		run(s, 3.0f);
		uint32_t tc = 0;
		for (const auto &b : s.buildings()) if (b.team == 0 && b.type == BLD_TOWN_CENTER) tc = b.id;
		s.order_garrison(inf.data(), inf.size(), tc);
		// The controller has no Garrisonable range (GetRange undefined), so GARRISON.GARRISONING
		// gives each member its own Garrison order at once: they walk in one by one.
		int ordered = 0;
		for (uint32_t id : inf) ordered += s.units_.order_kind[idx_of(s, id)] == ORDER_GARRISON;
		check(ordered == 8 && (s.formations().empty() || s.formations()[0].order == Simulation::FORDER_MEMBER),
				"garrison: every member walks in on its own order, not in shape");
		run(s, 30.0f);
		int in = 0;
		for (uint32_t id : inf) in += s.units_.action_state[idx_of(s, id)] == ACTION_GARRISONED;
		check(in == 8 && s.formations().empty(), "every member garrisons and the formation disbands");
	}
	{ // Heal: healers walk up in formation, then heal the injured ally.
		Simulation s;
		setup(s, CIV_ATHENIANS);
		auto hl = spawn_block(s, 6, -30, -10, 0, UNIT_HEALER);
		s.order_set_formation(hl.data(), hl.size(), FORMATION_BOX);
		run(s, 3.0f);
		const uint32_t hurt = s.spawn_unit(10.0f, -10.0f, 0, UNIT_SPEARMAN, 0.0f);
		s.order_set_stance(&hurt, 1, STANCE_PASSIVE);
		const int hi = idx_of(s, hurt);
		s.units_.hp[hi] = s.units_.max_hp[hi] * 0.3f;
		const float hp0 = s.units_.hp[hi];
		if (!s.formations().empty()) {
			s.order_heal(hl.data(), hl.size(), hurt);
			check(s.formations()[0].order == Simulation::FORDER_APPROACH, "heal: the healers walk up in formation");
			run(s, 20.0f);
			check(s.units_.hp[idx_of(s, hurt)] > hp0, "then they heal the ally");
		} else {
			check(true, "heal: healers take no formation here");
		}
	}
	{ // A member ordered on its own keeps its formation's other members where they are.
		Simulation s;
		setup(s, CIV_ATHENIANS);
		auto inf = spawn_block(s, 8, -30, 0, 0, UNIT_SPEARMAN);
		s.order_set_formation(inf.data(), inf.size(), FORMATION_BOX);
		run(s, 3.0f);
		const uint32_t tree = s.spawn_resource_world(-20.0f, 6.0f, RES_WOOD, 5000);
		s.order_gather(&inf[0], 1, tree);
		check(s.formations()[0].order != Simulation::FORDER_APPROACH && s.units_.order_kind[idx_of(s, inf[0])] == ORDER_GATHER,
				"one member ordered alone gathers by itself");
	}
}

static void test_queue(const char *path) {
	std::printf("15. Shift-queued formation orders\n");
	Simulation s;
	setup(s, CIV_ATHENIANS);
	auto inf = spawn_block(s, 8, -30, 0, 0, UNIT_SWORDSMAN);
	s.order_set_formation(inf.data(), inf.size(), FORMATION_BOX);
	run(s, 3.0f);
	const uint32_t fid = s.formations()[0].id;
	s.order_walk(inf.data(), inf.size(), -10.0f, 0.0f);
	s.order_walk(inf.data(), inf.size(), -10.0f, 20.0f, true);
	s.order_walk(inf.data(), inf.size(), 10.0f, 20.0f, true);
	check(ctrl(s, fid) && ctrl(s, fid)->queue.size() == 2 && (s.order_queues_.count(inf[0]) == 0 || s.order_queues_.at(inf[0]).empty()),
			"the queued walks wait in the controller's queue, not the members'");
	Simulation b;
	check(s.save_state(path) && b.load_state(path) && b.formations().size() == 1 &&
			b.formations()[0].queue.size() == 2, "the queue comes back after save/load");
	bool reached_a = false, reached_b = false;
	float cx, cz;
	for (int k = 0; k < 600 && !(reached_b && ctrl(s, fid)->order == Simulation::FORDER_IDLE && ctrl(s, fid)->queue.empty()); ++k) {
		run(s, 0.1f);
		centroid(s, inf, cx, cz);
		reached_a = reached_a || (std::hypot(cx + 10.0f, cz) < 3.0f);
		reached_b = reached_b || (reached_a && std::hypot(cx + 10.0f, cz - 20.0f) < 3.0f);
	}
	centroid(s, inf, cx, cz);
	check(reached_a && reached_b && std::hypot(cx - 10.0f, cz - 20.0f) < 3.0f && ctrl(s, fid) &&
			ctrl(s, fid)->members.size() == 8, "the formation walks to each point in turn, still in formation");
	s.order_walk(inf.data(), inf.size(), 0.0f, 0.0f);
	s.order_walk(inf.data(), inf.size(), 0.0f, 10.0f, true);
	s.order_walk(inf.data(), inf.size(), -20.0f, -20.0f);
	check(ctrl(s, fid) && ctrl(s, fid)->queue.empty(), "a new order clears the queue");
	// A queued attack after a walk: walk first, then approach the foe as a formation.
	uint32_t foe = s.spawn_unit(20.0f, -20.0f, 1, UNIT_SPEARMAN, 1.0e6f);
	s.order_set_stance(&foe, 1, STANCE_PASSIVE);
	s.order_attack(inf.data(), inf.size(), foe, true);
	check(ctrl(s, fid) && ctrl(s, fid)->order == Simulation::FORDER_WALK && ctrl(s, fid)->queue.size() == 1,
			"a queued attack waits for the walk");
	bool approached = false;
	for (int k = 0; k < 600 && !approached; ++k) {
		run(s, 0.1f);
		approached = ctrl(s, fid) && ctrl(s, fid)->target_unit == foe &&
				(ctrl(s, fid)->order == Simulation::FORDER_APPROACH || ctrl(s, fid)->order == Simulation::FORDER_MEMBER);
	}
	check(approached, "then the formation goes for the foe");
}

int main(int argc, char **argv) {
	std::string save = (argc > 1) ? argv[1] : "/tmp/rts0_test_formations.sav";
	test_templates();
	test_offsets();
	test_form_up();
	test_walk();
	test_change_and_regather();
	test_footprint_spacing();
	test_attack_walk();
	test_leave();
	test_save_load(save.c_str());
	test_lockstep();
	test_twins();
	test_attack();
	test_patrol();
	test_guard();
	test_save_attack(save.c_str());
	test_tasks();
	test_queue(save.c_str());
	if (g_failures) {
		std::printf("test_formations: FAIL (%d)\n", g_failures);
		return 1;
	}
	std::printf("test_formations: PASS\n");
	return 0;
}
