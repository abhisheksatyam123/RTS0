// RTS0 loot and experience test (headless: no SCons, no Godot).
// Checks the port of 0 A.D.'s Loot.js, Looter.js and the xp rules of Health.js, Heal.js
// and Promotion.js:
//  1. Tables: unit loot by class with hero, champion and siege tower overrides; building
//     loot by type, nothing for a type past the table.
//  2. A melee kill pays the victim's loot plus what it carried to the killer's owner.
//  3. Advanced and elite victims pay 1.2x and 1.44x (unit_advanced/unit_elite Loot/*).
//  4. Britons infantry loot 1 more of each resource (brit_woad_warriors).
//  5. Xp per hit: Loot/xp * health taken / max health, nothing for overkill.
//  6. Healer xp below one point accumulates (Heal.js PerformHeal).
//  7. Arrows: the shooter's owner loots; an arrow whose shooter died loots nothing.
//  8. Structures: towers loot, destroyed buildings pay their table row; the Ptolemaic and
//     Seleucid wood loot bonuses; Sacrificial Ritual's healers yield no metal.
//  9. Status-effect deaths (no attacker) pay nothing.
// 10. Save/load keeps projectile sources and partial xp.
// Build and run:
//   g++ -std=c++20 -O2 -Isrc tools/test_loot.cpp src/simulation/simulation.cpp \
//     src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//     src/network/net_host.cpp -lpthread -o /tmp/test_loot && /tmp/test_loot
// Exit: 0 = PASS, 1 = FAIL.
#define private public
#include "simulation/simulation.h"
#undef private

#include <cmath>
#include <cstring>
#include <cstdio>

using namespace rtso;

static constexpr float DT = 0.2f;

static int g_failures = 0;
static void check(bool ok, const char *what) {
	std::printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
	if (!ok) ++g_failures;
}
static bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

static void setup(Simulation &s) {
	s.set_sim_seed(4242);
	s.reset();
	s.set_victory_condition(VICTORY_ENDLESS);
	s.spawn_building_world(-56.0f, -56.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(56.0f, 56.0f, BLD_TOWN_CENTER, 1, 1.0f);
}
static int idx_of(const Simulation &s, uint32_t uid) { return s.lookup_unit(uid); }

struct Res {
	float food, wood, stone, gold;
};
static Res res_of(const Simulation &s, uint8_t p) {
	const auto &ps = s.player(p);
	return {ps.res[RES_FOOD], ps.res[RES_WOOD], ps.res[RES_STONE], ps.res[RES_GOLD]};
}
static bool gained(const Res &a, const Res &b, float food, float wood, float stone, float gold) {
	return near(b.food - a.food, food) && near(b.wood - a.wood, wood) && near(b.stone - a.stone, stone) &&
			near(b.gold - a.gold, gold);
}
static void print_gain(const Res &a, const Res &b) {
	std::printf("      gained food %.2f wood %.2f stone %.2f gold %.2f\n", b.food - a.food, b.wood - a.wood,
			b.stone - a.stone, b.gold - a.gold);
}

static uint16_t find_template(uint8_t flags_all, uint8_t type) {
	for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT; ++t) {
		const auto &u = civ_unit_template(t);
		if ((u.flags & flags_all) == flags_all && (type == UNIT_TYPE_NONE || u.unit_type == type)) return t;
	}
	return CIV_UNIT_TEMPLATE_NONE;
}

static void test_tables() {
	std::printf("1. loot tables\n");
	LootInfo sw = unit_loot(CIV_UNIT_TEMPLATE_NONE, UNIT_SWORDSMAN);
	check(sw.xp == 100 && sw.food == 5 && sw.wood == 4 && sw.metal == 1, "swordsman: xp 100, food 5, wood 4, metal 1");
	LootInfo fem = unit_loot(CIV_UNIT_TEMPLATE_NONE, UNIT_FEMALE_CITIZEN);
	check(fem.xp == 3 && fem.food == 5, "female citizen: xp 3, food 5");
	uint16_t hero = find_template(ROSTER_HERO, UNIT_TYPE_NONE);
	LootInfo h = unit_loot(hero, civ_unit_template(hero).unit_type);
	check(hero != CIV_UNIT_TEMPLATE_NONE && h.xp == 400 && h.metal == 25, "roster hero: template_unit_hero loot");
	uint16_t champ = find_template(ROSTER_CHAMPION, UNIT_SPEARMAN);
	LootInfo c = unit_loot(champ, UNIT_SPEARMAN);
	check(champ != CIV_UNIT_TEMPLATE_NONE && c.xp == 150 && c.metal == 8, "champion spearman: champion_infantry loot");
	uint16_t tower = find_template(ROSTER_SIEGE_TOWER, UNIT_TYPE_NONE);
	LootInfo t = unit_loot(tower, civ_unit_template(tower).unit_type);
	check(tower != CIV_UNIT_TEMPLATE_NONE && t.wood == 100 && t.metal == 60, "siege tower: wood 100, metal 60");
	// Each roster unit pays its own 0.28 template's Loot, mixins included.
	auto tl = [](const char *id) { for (uint16_t k = 0; k < CIV_UNIT_TEMPLATE_COUNT; ++k) if (!std::strcmp(civ_unit_template(k).id, id)) return unit_loot(k, civ_unit_template(k).unit_type); return LootInfo{}; };
	const LootInfo scout = tl("athen/ship_scout"), eleph = tl("pers/champion_elephant"), arch = tl("athen/infantry_archer_b");
	check(scout.xp == 20 && scout.food == 5 && scout.wood == 8 && scout.metal == 0, "a scout ship pays its own 20 xp, 5 food, 8 wood");
	check(eleph.xp == 330 && eleph.food == 33 && eleph.metal == 22, "an Indian elephant pays x1.1 (elephant_indian)");
	check(arch.xp == 100 && arch.food == 0 && arch.wood == 0 && arch.metal == 6, "a Cretan mercenary archer pays 6 metal");
	LootInfo tc = building_loot(BLD_TOWN_CENTER);
	check(tc.wood == 60 && tc.stone == 60 && tc.metal == 50 && tc.xp == 0, "civic centre: wood 60, stone 60, metal 50");
	LootInfo past = building_loot(BLD_TYPE_COUNT + 7);
	check(past.food == 0 && past.wood == 0 && past.stone == 0 && past.metal == 0, "a type past the table loots nothing");
	check(near(loot_rank_multiplier(1), 1.0f) && near(loot_rank_multiplier(2), 1.2f) && near(loot_rank_multiplier(3), 1.44f),
			"rank multipliers 1, 1.2, 1.44");
}

// Swordsman of `killer_team` next to a 1 hp victim; fight until the victim dies.
static bool melee_kill(Simulation &s, uint32_t killer, uint32_t victim) {
	s.order_attack(&killer, 1, victim);
	for (int k = 0; k < 100; ++k) {
		s.tick(DT);
		int v = idx_of(s, victim);
		if (v < 0 || !s.units_.alive[v]) return true;
	}
	return false;
}

static void test_melee_kill() {
	std::printf("2. melee kill pays loot and carried resources\n");
	Simulation s;
	setup(s);
	uint32_t sw = s.spawn_unit(0, 0, 0, UNIT_SWORDSMAN);
	uint32_t fem = s.spawn_unit(1.0f, 0, 1, UNIT_FEMALE_CITIZEN, 1.0f);
	int v = idx_of(s, fem);
	s.units_.carry[v] = 10;
	s.units_.res_type_carried[v] = RES_WOOD;
	Res a = res_of(s, 0);
	bool died = melee_kill(s, sw, fem);
	Res b = res_of(s, 0);
	check(died, "victim died");
	print_gain(a, b);
	check(gained(a, b, 5, 10, 0, 0), "killer's owner gets food 5 + the 10 wood carried");
	check(s.units_.kills[idx_of(s, sw)] == 1, "kill counted");
}

static void test_rank() {
	std::printf("3. advanced and elite victims pay more\n");
	for (uint8_t rank = 2; rank <= 3; ++rank) {
		Simulation s;
		setup(s);
		uint32_t sw = s.spawn_unit(0, 0, 0, UNIT_SWORDSMAN);
		uint32_t vic = s.spawn_unit(1.0f, 0, 1, UNIT_SWORDSMAN, 1.0f);
		s.units_.rank[idx_of(s, vic)] = rank;
		Res a = res_of(s, 0);
		melee_kill(s, sw, vic);
		Res b = res_of(s, 0);
		float m = rank == 2 ? 1.2f : 1.44f;
		print_gain(a, b);
		// Loot.js GetResources floors each modified amount.
		// Player 1 is Spartan: its swordsman is spart/champion_infantry_swordsman (Loot food 5, wood 4, metal 4).
		check(gained(a, b, std::floor(5 * m), std::floor(4 * m), 0, std::floor(4 * m)), rank == 2 ? "advanced: x1.2" : "elite: x1.44");
	}
}

static void test_britons() {
	std::printf("4. Britons infantry loot +1 of each resource\n");
	Simulation s;
	setup(s);
	s.set_team_civ(0, CIV_BRITONS);
	uint32_t sw = s.spawn_unit(0, 0, 0, UNIT_SWORDSMAN);
	uint32_t fem = s.spawn_unit(1.0f, 0, 1, UNIT_FEMALE_CITIZEN, 1.0f);
	Res a = res_of(s, 0);
	melee_kill(s, sw, fem);
	Res b = res_of(s, 0);
	print_gain(a, b);
	check(gained(a, b, 6, 1, 1, 1), "female citizen pays food 6, wood 1, stone 1, metal 1");
	s.cmd_delete(0, &sw, 1); // only the cavalryman may take the next kill
	s.set_team_civ(0, CIV_ATHENIANS);
	uint32_t cav = s.spawn_unit(0, 3.0f, 0, UNIT_CAVALRY);
	uint32_t fem2 = s.spawn_unit(1.0f, 3.0f, 1, UNIT_FEMALE_CITIZEN, 1.0f);
	s.set_team_civ(0, CIV_BRITONS);
	a = res_of(s, 0);
	melee_kill(s, cav, fem2);
	b = res_of(s, 0);
	print_gain(a, b);
	check(gained(a, b, 5, 0, 0, 0), "Briton cavalry (not infantry) get no bonus");
	// Boudicca and Cunobelin are Cavalry heroes: their own classes, not UNIT_HERO's, decide.
	s.cmd_delete(0, &cav, 1);
	const uint16_t boud = civ_unit_template_by_id("brit/hero_boudicca");
	check(boud < CIV_UNIT_TEMPLATE_COUNT, "roster has Boudicca");
	uint32_t hero = s.spawn_unit(0, 6.0f, 0, (UnitType)civ_unit_template(boud).unit_type, 0.0f, boud);
	uint32_t fem3 = s.spawn_unit(1.0f, 6.0f, 1, UNIT_FEMALE_CITIZEN, 1.0f);
	a = res_of(s, 0);
	melee_kill(s, hero, fem3);
	b = res_of(s, 0);
	print_gain(a, b);
	check(gained(a, b, 5, 0, 0, 0), "nor does Boudicca, a cavalry hero");
}

static void test_damage_xp() {
	std::printf("5. xp per hit\n");
	Simulation s;
	setup(s);
	uint32_t att = s.spawn_unit(0, 0, 0, UNIT_SWORDSMAN);
	uint32_t vic = s.spawn_unit(30.0f, 30.0f, 1, UNIT_SWORDSMAN);
	int a = idx_of(s, att), v = idx_of(s, vic);
	s.units_.rank[v] = 1; // Sparta's default swordsman is an Elite champion (Identity/Rank)
	float max_hp = s.units_.max_hp[v];
	s.award_damage_xp(att, (size_t)v, max_hp * 0.25f);
	check(s.units_.xp[a] == 25, "a quarter of a swordsman's health gives 25 xp");
	s.units_.hp[v] = max_hp * 0.10f;
	s.award_damage_xp(att, (size_t)v, max_hp * 5.0f);
	check(s.units_.xp[a] == 35, "overkill counts only the health left (10 more)");
	s.units_.rank[v] = 3;
	s.units_.hp[v] = max_hp;
	s.award_damage_xp(att, (size_t)v, max_hp * 0.5f);
	check(s.units_.xp[a] == 35 + 72, "elite victim gives 1.44x xp");
	// Far from the first pair so only this swordsman fights.
	uint32_t killer = s.spawn_unit(-30.0f, 30.0f, 0, UNIT_SWORDSMAN);
	uint32_t fem = s.spawn_unit(-29.0f, 30.0f, 1, UNIT_FEMALE_CITIZEN);
	melee_kill(s, killer, fem);
	int c = idx_of(s, killer);
	std::printf("      killer xp %u + %.3f\n", s.units_.xp[c], s.units_.xp_partial[c]);
	// spart/support_civilian: template_unit_support_civilian's 3 xp, +1.
	check(near(s.units_.xp[c] + s.units_.xp_partial[c], 4.0f), "killing a Spartan civilian gives her 4 xp, not a flat 100");
}

static void test_healer_xp() {
	std::printf("6. healer xp\n");
	Simulation s;
	setup(s);
	uint32_t healer = s.spawn_unit(0, 0, 0, UNIT_HEALER);
	uint32_t hurt = s.spawn_unit(1.0f, 0, 0, UNIT_SWORDSMAN);
	int h = idx_of(s, healer), u = idx_of(s, hurt);
	float max_hp = s.units_.max_hp[u];
	s.units_.hp[u] = max_hp * 0.5f;
	for (int k = 0; k < 2000 && s.units_.hp[u] < max_hp; ++k) s.tick(DT);
	float total = s.units_.xp[h] + s.units_.xp_partial[h];
	std::printf("      healer xp %u + %.3f after healing half a swordsman\n", s.units_.xp[h], s.units_.xp_partial[h]);
	check(near(s.units_.hp[u], max_hp), "swordsman healed to full");
	// The civ's swordsman template sets its Loot/xp (a champion's is 150).
	float want = unit_loot(s.units_.tmpl[u], UNIT_SWORDSMAN).xp * 0.5f;
	std::printf("      expected %.1f\n", want);
	check(std::fabs(total - want) < 0.5f, "healing half the health gives half the target's Loot/xp");
}

static void test_arrows() {
	std::printf("7. arrows loot for their shooter\n");
	Simulation s;
	setup(s);
	uint32_t archer = s.spawn_unit(0, 0, 0, UNIT_ARCHER);
	uint32_t fem = s.spawn_unit(10.0f, 0, 1, UNIT_FEMALE_CITIZEN, 1.0f);
	uint32_t fem2 = s.spawn_unit(10.0f, 4.0f, 1, UNIT_FEMALE_CITIZEN, 1.0f);
	s.order_set_stance(&archer, 1, STANCE_PASSIVE);
	int v = idx_of(s, fem);
	Res a = res_of(s, 0);
	s.fire_arrow(0, 1.4f, 0, 10.0f, 0.8f, 0, 50.0f, 0, fem, 0, 1, false, 0, archer);
	check(s.projectiles_.size() == 1 && s.projectiles_[0].source_id == archer, "projectile records its shooter");
	for (int k = 0; k < 20 && s.units_.alive[v]; ++k) s.tick(DT);
	Res b = res_of(s, 0);
	check(!s.units_.alive[v], "arrow killed the victim");
	check(gained(a, b, 5, 0, 0, 0), "shooter's owner gets food 5");
	check(s.units_.xp[idx_of(s, archer)] + s.units_.xp_partial[idx_of(s, archer)] > 2.9f, "shooter gets the victim's xp");

	s.fire_arrow(0, 1.4f, 0, 10.0f, 0.8f, 4.0f, 50.0f, 0, fem2, 0, 1, false, 0, archer);
	int ar = idx_of(s, archer);
	s.units_.hp[ar] = 0.0f;
	s.units_.alive[ar] = 0;
	s.units_.action_state[ar] = ACTION_DYING;
	int v2 = idx_of(s, fem2);
	a = res_of(s, 0);
	for (int k = 0; k < 20 && s.units_.alive[v2]; ++k) s.tick(DT);
	b = res_of(s, 0);
	check(!s.units_.alive[v2], "second arrow still kills");
	check(gained(a, b, 0, 0, 0, 0), "no loot once the shooter is dead (no Looter left)");
}

static void test_structures() {
	std::printf("8. structures\n");
	Simulation s;
	setup(s);
	uint32_t tower = s.spawn_building_world(0, 0, BLD_SENTRY_TOWER, 0, 1.0f);
	uint32_t fem = s.spawn_unit(30.0f, 30.0f, 1, UNIT_FEMALE_CITIZEN);
	Res a = res_of(s, 0);
	s.collect_unit_loot(tower, 0, (size_t)idx_of(s, fem));
	Res b = res_of(s, 0);
	check(gained(a, b, 5, 0, 0, 0), "a tower loots like a unit (no Britons infantry bonus)");
	uint32_t house = s.spawn_building_world(20.0f, -20.0f, BLD_BARRACKS, 1, 1.0f);
	const Simulation::Building *bp = nullptr;
	for (const auto &bb : s.buildings_) if (bb.id == house) bp = &bb;
	uint32_t sw = s.spawn_unit(0, 3.0f, 0, UNIT_SWORDSMAN);
	a = res_of(s, 0);
	s.collect_building_loot(sw, 0, *bp);
	b = res_of(s, 0);
	check(gained(a, b, 0, 40, 20, 0), "destroying barracks pays wood 40, stone 20");
	Simulation::Building fake = *bp;
	fake.type = BLD_TYPE_COUNT + 3;
	a = res_of(s, 0);
	s.collect_building_loot(sw, 0, fake);
	b = res_of(s, 0);
	check(gained(a, b, 0, 0, 0, 0), "an unknown building type pays nothing");
	// special/filter/foundation.xml keeps no Loot: a destroyed foundation pays nothing.
	Simulation::Building found = *bp;
	found.build_progress = 0.5f;
	a = res_of(s, 0);
	s.collect_building_loot(sw, 0, found);
	b = res_of(s, 0);
	check(gained(a, b, 0, 0, 0, 0), "a barracks foundation pays nothing");
	// Health.js: hitting the Han Imperial Ministry (Loot xp 200) pays its xp share of the damage.
	uint32_t mins = s.spawn_building_world(-30.0f, -30.0f, BLD_MINISTRY, 1, 1.0f);
	const Simulation::Building *mp = nullptr;
	for (const auto &bb : s.buildings_) if (bb.id == mins) mp = &bb;
	const int swi = idx_of(s, sw);
	const float xp0 = s.units_.xp[swi] + s.units_.xp_partial[swi];
	s.award_structure_damage_xp(sw, *mp, mp->max_hp * 0.25f);
	const float got = s.units_.xp[swi] + s.units_.xp_partial[swi] - xp0;
	std::printf("      a quarter of the ministry's health pays %.2f xp\n", got);
	check(building_loot(BLD_MINISTRY).xp == 200 && near(got, 50.0f), "damaging the Han ministry pays 50 xp per quarter of its health");
	// Sun-dried Mud Bricks (civbonuses/ptol_structures.json): Ptolemaic houses yield Loot/wood x0.6.
	uint32_t ph = s.spawn_building_world(-20.0f, 20.0f, BLD_HOUSE, 1, 1.0f);
	const Simulation::Building *php = nullptr;
	for (const auto &bb : s.buildings_) if (bb.id == ph) php = &bb;
	a = res_of(s, 0);
	s.collect_building_loot(sw, 0, *php);
	b = res_of(s, 0);
	check(gained(a, b, 0, 30, 0, 0), "an Athenian house (template_structure_civic_house_big) pays wood 30");
	// Each civ's own template and each WallSet piece's length (structure_loot).
	check(structure_loot(BLD_TOWER, CIV_IBERIANS, 1.0f).stone == 50 && structure_loot(BLD_TOWER, CIV_ATHENIANS, 1.0f).stone == 20,
		"the Iberian tower pays 50 stone, a generic one 20");
	check(structure_loot(BLD_BARRACKS, CIV_GAULS, 1.0f).wood == 60 && structure_loot(BLD_BARRACKS, CIV_GAULS, 1.0f).stone == 0,
		"a Gallic wooden barracks pays 60 wood");
	const WallSetInfo &stone = wallset_info(WALLSET_STONE), &pal = wallset_info(WALLSET_PALISADE);
	check(structure_loot(BLD_WALL_SEGMENT, CIV_ATHENIANS, 0.5f * stone.lengths[2]).stone == 3 &&
		structure_loot(BLD_WALL_SEGMENT, CIV_ATHENIANS, 0.5f * stone.lengths[0]).stone == 7 &&
		structure_loot(BLD_WALL_SEGMENT, CIV_MAURYANS, 0.5f * stone.lengths[1]).wood == 5,
		"wall pieces pay by length: short 3, long 7 stone; a Mauryan medium one 5 wood");
	// Ptolemaic and Seleucid wall_* are 38/25/15 and 34/21/11 m long, still 7/5/3 stone.
	for (uint8_t civ : {CIV_PTOLEMIES, CIV_SELEUCIDS}) {
		const WallSetInfo own = wallset_info(WALLSET_STONE, civ);
		check(structure_loot(BLD_WALL_SEGMENT, civ, 0.5f * own.lengths[0]).stone == 7 &&
			structure_loot(BLD_WALL_SEGMENT, civ, 0.5f * own.lengths[1]).stone == 5 &&
			structure_loot(BLD_WALL_SEGMENT, civ, 0.5f * own.lengths[2]).stone == 3,
			"Ptolemaic and Seleucid wall pieces pay 7, 5 and 3 stone by their own lengths");
	}
	check(structure_loot(BLD_PALISADE, CIV_GAULS, 0.5f * pal.lengths[0]).wood == 3 && structure_loot(BLD_PALISADE, CIV_GAULS, pal.tower_half).wood == 3 &&
		structure_loot(BLD_PALISADE, CIV_GAULS, 0.5f * pal.lengths[2]).wood == 1, "palisades pay 3, 1 and their tower 3 wood");
	s.set_team_civ(1, CIV_PTOLEMIES);
	a = res_of(s, 0);
	s.collect_building_loot(sw, 0, *php);
	b = res_of(s, 0);
	check(gained(a, b, 0, 9, 0, 0), "a Ptolemaic house pays wood 9 (x0.6)");
	// Fertile Crescent (civbonuses/sele_farms.json): Seleucid fields yield Loot/wood x0.75.
	uint32_t fld = s.spawn_building_world(-20.0f, -20.0f, BLD_FIELD, 1, 1.0f);
	const Simulation::Building *fp = nullptr;
	for (const auto &bb : s.buildings_) if (bb.id == fld) fp = &bb;
	const LootInfo fl = building_loot(BLD_FIELD);
	s.set_team_civ(1, CIV_ATHENIANS);
	a = res_of(s, 0);
	s.collect_building_loot(sw, 0, *fp);
	b = res_of(s, 0);
	check(gained(a, b, fl.food, fl.wood, fl.stone, fl.metal), "an Athenian field pays its table row");
	s.set_team_civ(1, CIV_SELEUCIDS);
	a = res_of(s, 0);
	s.collect_building_loot(sw, 0, *fp);
	b = res_of(s, 0);
	check(gained(a, b, fl.food, std::floor(fl.wood * 0.75f), fl.stone, fl.metal), "a Seleucid field pays wood x0.75");
	// Sacrificial Ritual (cost_healer.json): once its owner has it, a healer yields no metal loot.
	uint32_t heal = s.spawn_unit(30.0f, -30.0f, 1, UNIT_HEALER);
	const LootInfo hl = unit_loot(s.units_.tmpl[idx_of(s, heal)], UNIT_HEALER);
	a = res_of(s, 0);
	s.collect_unit_loot(sw, 0, (size_t)idx_of(s, heal));
	b = res_of(s, 0);
	check(hl.metal > 0 && gained(a, b, hl.food, hl.wood, hl.stone, hl.metal), "a healer pays its metal loot");
	s.players_[1].techs.set(TECH_COST_HEALER);
	a = res_of(s, 0);
	s.collect_unit_loot(sw, 0, (size_t)idx_of(s, heal));
	b = res_of(s, 0);
	check(gained(a, b, hl.food, hl.wood, hl.stone, 0), "after Sacrificial Ritual it pays no metal");
	// Celtic Auxiliaries (celtic_auxiliaries.json, Carthage's Celtic embassy): Mercenary
	// Swordsmen cost metal x0.5 and +50 food, and yield Loot/metal x0.5 and Loot/food +5.
	uint16_t gaul = 0xFFFF;
	for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT; ++t)
		if (std::strcmp(civ_unit_template(t).id, "cart/infantry_swordsman_gaul_b") == 0) gaul = t;
	check(gaul != 0xFFFF && (civ_unit_template(gaul).tech_classes & TC_MERCENARY) && (civ_unit_template(gaul).tech_classes & TC_SWORDSMAN),
			"the Gallic mercenary swordsman is a Mercenary Swordsman");
	check(tech_info(TECH_CELTIC_AUXILIARIES).researcher == BLD_EMBASSY && tech_available_to_civ(TECH_CELTIC_AUXILIARIES, CIV_CARTHAGINIANS) &&
			!tech_available_to_civ(TECH_CELTIC_AUXILIARIES, CIV_ROMANS), "Celtic Auxiliaries is Carthage's, at the Celtic embassy");
	s.set_team_civ(1, CIV_CARTHAGINIANS);
	uint32_t merc = s.spawn_unit(30.0f, -32.0f, 1, UNIT_SWORDSMAN, 0.0f, gaul);
	const LootInfo ml = unit_loot(gaul, UNIT_SWORDSMAN);
	a = res_of(s, 0);
	s.collect_unit_loot(sw, 0, (size_t)idx_of(s, merc));
	b = res_of(s, 0);
	const float rk = loot_rank_multiplier(s.units_.rank[(size_t)idx_of(s, merc)]); // mercenaries start Advanced
	check(ml.metal > 0 && gained(a, b, std::floor(ml.food * rk), std::floor(ml.wood * rk), 0, std::floor(ml.metal * rk)),
			"a mercenary swordsman pays its loot");
	s.complete_research(1, TECH_CELTIC_AUXILIARIES);
	const auto &tm = s.tech_mods(1).tmpl[gaul];
	check(near(tm.cost[RES_GOLD], 0.5f) && near(tm.cost_add[RES_FOOD], 50.0f), "Celtic Auxiliaries: metal cost x0.5, food +50");
	a = res_of(s, 0);
	s.collect_unit_loot(sw, 0, (size_t)idx_of(s, merc));
	b = res_of(s, 0);
	// Loot.js GetResources: the rank (unit_advanced/unit_elite Loot multiply) and these modifiers
	// together, multiplications first, floored once.
	check(gained(a, b, std::floor(ml.food * rk + 5.0f), std::floor(ml.wood * rk), 0, std::floor(ml.metal * rk * 0.5f)),
			"then its loot is metal x0.5, food +5");
}

static void test_status_death() {
	std::printf("9. status-effect deaths pay nothing\n");
	Simulation s;
	setup(s);
	uint32_t fem = s.spawn_unit(30.0f, 30.0f, 1, UNIT_FEMALE_CITIZEN, 1.0f);
	int v = idx_of(s, fem);
	CombatProfile poison{};
	poison.status = 1;
	poison.status_damage = 20.0f;
	poison.status_interval = 0.5f;
	poison.status_time = 5.0f;
	s.apply_status((size_t)v, poison);
	Res a0 = res_of(s, 0);
	for (int k = 0; k < 10 && s.units_.alive[v]; ++k) s.tick(DT);
	Res b0 = res_of(s, 0);
	check(!s.units_.alive[v], "poison killed her");
	check(gained(a0, b0, 0, 0, 0, 0), "player 0 gets nothing");
}

static void test_save_load() {
	std::printf("10. save/load keeps sources and partial xp\n");
	Simulation s;
	setup(s);
	uint32_t archer = s.spawn_unit(0, 0, 0, UNIT_ARCHER);
	uint32_t fem = s.spawn_unit(60.0f, 0, 1, UNIT_FEMALE_CITIZEN);
	s.gain_xp((size_t)idx_of(s, archer), 12.375f);
	s.fire_arrow(0, 1.4f, 0, 60.0f, 0.8f, 0, 5.0f, 0, fem, 0, 1, false, 0, archer);
	const char *path = "/tmp/rts0_test_loot.sav";
	check(s.save_state(path), "saved");
	Simulation t;
	check(t.load_state(path), "loaded");
	int a = t.lookup_unit(archer);
	check(a >= 0 && t.units_.xp[a] == 12 && near(t.units_.xp_partial[a], 0.375f), "xp 12 + 0.375 kept");
	check(t.projectiles_.size() == 1 && t.projectiles_[0].source_id == archer, "projectile source kept");
	std::remove(path);
}

int main() {
	test_tables();
	test_melee_kill();
	test_rank();
	test_britons();
	test_damage_xp();
	test_healer_xp();
	test_arrows();
	test_structures();
	test_status_death();
	test_save_load();
	if (g_failures) {
		std::printf("test_loot: FAIL (%d)\n", g_failures);
		return 1;
	}
	std::printf("test_loot: PASS\n");
	return 0;
}
