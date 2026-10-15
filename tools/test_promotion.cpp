// 0 A.D. unit promotion (Promotion.js, unit_advanced.json, unit_elite.json): RequiredXp per
// template, xp carried over between ranks, rank bonuses, chains that end in another unit,
// templates trained above Basic, RequiredXp technologies, the barracks xp trickle and
// unit upgrades (Upgrade.js: the Centurion, the Immortals' bow).
//   g++ -std=c++20 -O2 -Isrc tools/test_promotion.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_promotion && /tmp/test_promotion
#define private public
#include "simulation/simulation.h"
#undef private
#include "simulation/entity_limits_data.h"
#include <cmath>
#include <tuple>
#include <cstdio>
#include <cstring>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) <= eps * std::max(1.0f, std::fabs(b)); }

static uint16_t tmpl_of(const char *id) {
	for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT; ++t)
		if (std::strcmp(civ_unit_template(t).id, id) == 0) return t;
	std::printf("no roster template %s\n", id);
	++failures;
	return CIV_UNIT_TEMPLATE_NONE;
}

static void fresh(Simulation &s, uint8_t civ) {
	s.set_sim_seed(5);
	s.reset();
	s.set_player_civ(civ);
	s.spawn_building_world(-50.0f, -50.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(50.0f, 50.0f, BLD_TOWN_CENTER, 1, 1.0f);
}

static size_t idx(Simulation &s, uint32_t id) { return (size_t)s.lookup_unit(id); }

int main() {
	static Simulation s;

	// 1. An Athenian hoplite: RequiredXp 100 (template_unit_infantry), xp left over carries
	//    to the next rank, and each rank multiplies health by 1.25 and melee damage by 1.1
	//    (0.28's unit_advanced / unit_elite, no melee resistance), keeping the health
	//    fraction. Kills alone no longer promote.
	{
		fresh(s, CIV_ATHENIANS);
		const uint16_t hop = tmpl_of("athen/infantry_spearman_b");
		const size_t i = idx(s, s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN, 0.0f, hop));
		CHECK(s.units_.rank[i] == 1);
		CHECK(near(s.promotion_required_xp(i), 100.0f));
		const float hp0 = s.units_.max_hp[i], dmg0 = s.units_.attack_damage[i];
		s.units_.kills[i] = 20;
		s.units_.hp[i] = hp0 * 0.5f;
		s.promote_if_due(i);
		CHECK(s.units_.rank[i] == 1);

		s.gain_xp(i, 130.0f);
		s.promote_if_due(i);
		CHECK(s.units_.rank[i] == 2);
		CHECK(s.units_.xp[i] == 30);
		CHECK(near(s.units_.max_hp[i], hp0 * 1.25f));
		CHECK(near(s.units_.hp[i], hp0 * 1.25f * 0.5f));
		CHECK(near(s.units_.attack_damage[i], dmg0 * 1.1f));
		CHECK(near(s.rank_melee_armor(i), 0.0f));
		CHECK(near(s.unit_capture_attack_at(i), unit_capture_attack(hop, UNIT_SPEARMAN) + 0.7f));

		s.gain_xp(i, 75.0f); // 105: Elite with 5 left over
		s.promote_if_due(i);
		CHECK(s.units_.rank[i] == 3);
		CHECK(s.units_.xp[i] == 5);
		CHECK(near(s.units_.max_hp[i], hp0 * 1.25f * 1.25f));
		CHECK(near(s.units_.attack_damage[i], dmg0 * 1.21f));
		CHECK(near(s.rank_melee_armor(i), 0.0f));
		CHECK(near(s.unit_capture_attack_at(i), unit_capture_attack(hop, UNIT_SPEARMAN) + 1.5f));

		// Elite hoplites promote to the City Guard champion (athen/champion_infantry), which
		// has no rank, after 250 xp: the champion's own stats and no rank bonus.
		CHECK(near(s.promotion_required_xp(i), 250.0f));
		s.gain_xp(i, 240.0f);
		s.promote_if_due(i);
		CHECK(s.units_.rank[i] == 3);
		s.gain_xp(i, 5.0f);
		s.promote_if_due(i);
		const uint16_t guard = tmpl_of("athen/champion_infantry");
		CHECK(s.units_.tmpl[i] == guard);
		CHECK(s.units_.rank[i] == 1);
		CHECK(near(s.units_.max_hp[i], civ_template_unit_info(guard, UNIT_SPEARMAN).hp * s.unit_tech_mods(0, UNIT_SPEARMAN, guard).health));
		CHECK(s.promotion_required_xp(i) < 0.0f);
	}

	// 2. Several ranks in one go (Promotion.js IncreaseXp loop), and ranged units: no melee
	//    bonus or armour, spread is spread_rank_multiplier at the shot. Cavalry need 150,
	//    and ranged soldiers 25% more (auto-researched soldier_ranged_experience).
	{
		fresh(s, CIV_ATHENIANS);
		const uint16_t arch = tmpl_of("spart/infantry_javelineer_b");
		const size_t a = idx(s, s.spawn_unit(0.0f, 0.0f, 0, UNIT_ARCHER, 0.0f, arch));
		const float dmg0 = s.units_.attack_damage[a];
		CHECK(near(s.promotion_required_xp(a), 125.0f));
		s.gain_xp(a, 300.0f);
		s.promote_if_due(a);
		CHECK(s.units_.rank[a] == 3);
		CHECK(s.units_.xp[a] == 50);
		CHECK(near(s.units_.attack_damage[a], dmg0));
		CHECK(s.rank_melee_armor(a) == 0.0f);

		const uint16_t cav = tmpl_of("athen/cavalry_javelineer_b");
		const size_t c = idx(s, s.spawn_unit(4.0f, 0.0f, 0, UNIT_CAVALRY, 0.0f, cav));
		CHECK(near(s.promotion_required_xp(c), 187.5f));
		const size_t sp = idx(s, s.spawn_unit(6.0f, 0.0f, 0, UNIT_SPEARMAN, 0.0f, tmpl_of("athen/infantry_spearman_b")));
		CHECK(near(s.promotion_required_xp(sp), 100.0f));

		// Mercenaries start Advanced (auto-researched upgrade_rank_advanced_mercenary: RequiredXp 0
		// at Basic), with Advanced health but no xp, and need the usual xp for Elite.
		const size_t m = idx(s, s.spawn_unit(10.0f, 0.0f, 0, UNIT_ARCHER, 0.0f, tmpl_of("athen/infantry_archer_b")));
		std::printf("  mercenary archer: rank %d, hp %.0f/%.0f, xp %d, needs %.1f\n", s.units_.rank[m], s.units_.hp[m],
				s.units_.max_hp[m], s.units_.xp[m], s.promotion_required_xp(m));
		CHECK(s.units_.rank[m] == 2 && s.units_.xp[m] == 0 && s.units_.hp[m] == s.units_.max_hp[m]);
		CHECK(near(s.promotion_required_xp(m), 125.0f));

		// Women, heroes and champions have no Promotion.
		const size_t w = idx(s, s.spawn_unit(8.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN));
		CHECK(s.promotion_required_xp(w) < 0.0f);
		s.gain_xp(w, 1000.0f);
		s.promote_if_due(w);
		CHECK(s.units_.rank[w] == 1);
	}

	// 3. Healers: +5 heal and +3 range per rank; a Kushite priest trained as _e starts Elite
	//    and trains 1.44x as long.
	{
		fresh(s, CIV_KUSHITES);
		const uint16_t pb = tmpl_of("kush/support_healer_b"), pe = tmpl_of("kush/support_healer_e");
		const size_t b = idx(s, s.spawn_unit(0.0f, 0.0f, 0, UNIT_HEALER, 0.0f, pb));
		const size_t e = idx(s, s.spawn_unit(2.0f, 0.0f, 0, UNIT_HEALER, 0.0f, pe));
		CHECK(s.units_.rank[b] == 1);
		CHECK(s.units_.rank[e] == 3);
		CHECK(near(s.units_.max_hp[e], s.units_.max_hp[b] * 1.5625f));
		CHECK(near(s.units_.hp[e], s.units_.max_hp[e]));
		CHECK(near(s.heal_amount_of(e), HEAL_HP + 10.0f));
		CHECK(near(s.healer_range_of(e), s.healer_range(0) + 6.0f * 0.28f));
		CHECK(near(Simulation::promotion_train_time_mult(pe), 1.44f));
		CHECK(near(Simulation::promotion_train_time_mult(pb), 1.0f));
	}

	// 4. Rome: an Elite swordsman becomes a Marian Legionary after 2000 more xp (another
	//    roster unit, Elite); Marian Reforms replaces RequiredXp with 0, so every Roman
	//    citizen soldier promotes straight to its 0.28 target (swordsmen Legionary, spearmen Levy); Army Camp veterans start Advanced.
	{
		fresh(s, CIV_ROMANS);
		const uint16_t sw = tmpl_of("rome/infantry_swordsman_b"), leg = tmpl_of("rome/infantry_legionary");
		const size_t i = idx(s, s.spawn_unit(0.0f, 0.0f, 0, UNIT_SWORDSMAN, 0.0f, sw));
		s.gain_xp(i, 200.0f);
		s.promote_if_due(i);
		CHECK(s.units_.rank[i] == 3 && s.units_.tmpl[i] == sw);
		CHECK(near(s.promotion_required_xp(i), 2000.0f));
		s.gain_xp(i, 2000.0f);
		s.promote_if_due(i);
		CHECK(s.units_.tmpl[i] == leg);
		CHECK(s.units_.rank[i] == 3);
		const float leg_hp = civ_template_unit_info(leg, UNIT_SWORDSMAN).hp * s.unit_tech_mods(0, UNIT_SWORDSMAN, leg).health;
		CHECK(near(s.units_.max_hp[i], leg_hp * 1.5625f));

		const uint16_t sp = tmpl_of("rome/infantry_spearman_b");
		const size_t j = idx(s, s.spawn_unit(3.0f, 0.0f, 0, UNIT_SPEARMAN, 0.0f, sp));
		const size_t veteran = idx(s, s.spawn_unit(6.0f, 0.0f, 0, UNIT_SPEARMAN, 0.0f, tmpl_of("rome/infantry_spearman_a")));
		CHECK(s.units_.rank[veteran] == 2);
		CHECK(near(Simulation::promotion_train_time_mult(tmpl_of("rome/infantry_spearman_a")), 1.2f));
		const float sw_secs = s.unit_train_seconds(0, sw); // Basic, before the Reforms
		s.complete_research(0, TECH_ROMAN_REFORMS);
		CHECK(near(s.promotion_required_xp(j), 0.0f));
		// Trainer.js GetUpgradedTemplate: a swordsman is now trained as the Elite Legionary,
		// at its cost and its Cost/BuildTime x1.2 x1.2.
		{
			uint8_t rank = 0;
			CHECK(s.trained_as(0, sw, rank) == leg && rank == 3);
			float c_sw[RES_COUNT], c_leg[RES_COUNT];
			s.unit_train_cost(0, sw, civ_template_unit_info(sw, UNIT_SWORDSMAN), c_sw);
			s.unit_train_cost(0, leg, civ_template_unit_info(leg, UNIT_SWORDSMAN), c_leg);
			for (int r = 0; r < RES_COUNT; ++r) CHECK(near(c_sw[r], c_leg[r]));
			CHECK(c_sw[RES_GOLD] > 0.0f);
			CHECK(near(s.unit_train_seconds(0, sw), s.unit_train_seconds(0, leg)));
			CHECK(near(s.unit_train_seconds(0, sw), sw_secs * 1.44f)); // both 10 s templates
		}
		s.tick(Simulation::FIXED_STEP);
		// 0.28: spearmen become Levy Auxiliary Spearmen (Basic, cannot promote), not Legionaries.
		const uint16_t levy = tmpl_of("rome/infantry_spearman_conscript");
		CHECK(s.units_.tmpl[j] == levy && s.units_.rank[j] == 1);
		CHECK(s.units_.unit_type[j] == UNIT_SPEARMAN);
		CHECK(s.units_.tmpl[veteran] == levy);
		// "Soldier !Auxiliary": the auxiliary cavalry keep their promotion xp after the reforms.
		const uint16_t aux = tmpl_of("rome/cavalry_javelineer_auxiliary_b");
		const size_t k = idx(s, s.spawn_unit(9.0f, 0.0f, 0, (UnitType)civ_unit_template(aux).unit_type, 0.0f, aux));
		CHECK(s.promotion_required_xp(k) > 0.0f);
	}

	// 5. Hoplite Tradition halves a hoplite's RequiredXp (Infantry Spearman !Hero).
	{
		fresh(s, CIV_SPARTANS);
		const size_t i = idx(s, s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN, 0.0f, tmpl_of("spart/infantry_spearman_b")));
		const size_t j = idx(s, s.spawn_unit(2.0f, 0.0f, 0, UNIT_ARCHER, 0.0f, tmpl_of("spart/infantry_javelineer_b")));
		s.complete_research(0, TECH_HOPLITE_TRADITION);
		CHECK(near(s.promotion_required_xp(i), 50.0f));
		CHECK(near(s.promotion_required_xp(j), 125.0f)); // ranged: soldier_ranged_experience only
	}

	// 5b. The Roman centurion's Combat Leadership (centurion_1): soldiers within 30 m need
	//     25% less xp to promote, like Han Xin's Northern Army; one farther away does not.
	{
		fresh(s, CIV_ROMANS);
		s.spawn_unit(0.0f, 0.0f, 0, UNIT_SWORDSMAN, 0.0f, tmpl_of("rome/champion_infantry_swordsman_centurion"));
		const size_t i = idx(s, s.spawn_unit(3.0f, 0.0f, 0, UNIT_SPEARMAN, 0.0f, tmpl_of("rome/infantry_spearman_b")));
		const size_t j = idx(s, s.spawn_unit(40.0f, 40.0f, 0, UNIT_SPEARMAN, 0.0f, tmpl_of("rome/infantry_spearman_b")));
		s.rebuild_auras();
		CHECK(near(s.promotion_required_xp(i), 75.0f));
		CHECK(near(s.promotion_required_xp(j), 100.0f));
	}

	// 6. Rigorous Training: a unit garrisoned in a barracks gains 1 xp a second.
	{
		fresh(s, CIV_ATHENIANS);
		const uint32_t bar = s.spawn_building_world(-30.0f, -30.0f, BLD_BARRACKS, 0, 1.0f); // in the civic centre's territory
		const uint32_t uid = s.spawn_unit(-30.0f, -36.0f, 0, UNIT_SPEARMAN, 0.0f, tmpl_of("athen/infantry_spearman_b"));
		CHECK(s.garrison_unit_now(uid, bar));
		for (int t = 0; t < (int)std::lround(101.0f / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
		const size_t i = idx(s, uid);
		CHECK(s.units_.rank[i] == 2);
		CHECK(s.units_.xp[i] <= 2);
	}

	// 6b. So do the Han academy and the Ptolemaic mercenary camp (both barracks children).
	for (const auto &[civ, type, tmpl] : {std::tuple{CIV_HAN, BLD_ACADEMY, "han/infantry_spearman_b"},
				std::tuple{CIV_PTOLEMIES, BLD_MERCENARY_CAMP, "ptol/infantry_spearman_merc_b"}}) {
		fresh(s, civ);
		const uint32_t bar = s.spawn_building_world(-30.0f, -30.0f, type, 0, 1.0f);
		const uint32_t uid = s.spawn_unit(-30.0f, -36.0f, 0, UNIT_SPEARMAN, 0.0f, tmpl_of(tmpl));
		CHECK(s.garrison_unit_now(uid, bar));
		for (int t = 0; t < (int)std::lround(10.5f / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
		CHECK(s.units_.xp[idx(s, uid)] >= 9.5f && s.units_.xp[idx(s, uid)] <= 11.0f);
	}

	// 7. Gaia units from a map's _e template start Elite.
	{
		fresh(s, CIV_ATHENIANS);
		const size_t g = idx(s, s.spawn_gaia_unit(0.0f, 0.0f, UNIT_SPEARMAN, "athen/infantry_spearman_e", 3));
		CHECK(s.units_.rank[g] == 3);
		CHECK(s.units_.tmpl[g] == tmpl_of("athen/infantry_spearman_b"));
		CHECK(s.units_.xp[g] == 0);
	}

	// 8. Unit upgrades (Upgrade.js): Roman elite infantry pay 100 food and 50 metal to
	//    become Centurions (12 s for swordsmen), keeping their health fraction; the
	//    Centurion limit counts upgrading units; cancel and death refund; a save keeps
	//    the timer; the Immortals switch between spear and bow for free in 6 s.
	{
		fresh(s, CIV_ROMANS);
		const uint16_t sw = tmpl_of("rome/infantry_swordsman_b");
		const uint16_t cent = tmpl_of("rome/champion_infantry_swordsman_centurion");
		auto elite = [&](float x) {
			const uint32_t id = s.spawn_unit(x, 0.0f, 0, UNIT_SWORDSMAN, 0.0f, sw);
			s.gain_xp(idx(s, id), 2000.0f);
			s.promote_if_due(idx(s, id));
			return id;
		};
		const uint32_t basic = s.spawn_unit(-4.0f, 0.0f, 0, UNIT_SWORDSMAN, 0.0f, sw);
		const uint32_t a = elite(0.0f), b = elite(3.0f), c = elite(6.0f);
		CHECK(s.units_.rank[idx(s, a)] == 3 && s.units_.tmpl[idx(s, a)] == sw);
		s.set_player_resources(0, 1000, 0, 1000, 0);
		CHECK(s.unit_upgrade_check(0, basic, cent) == Simulation::UPGRADE_ERR_NONE); // elite only
		CHECK(s.unit_upgrade_check(1, a, cent) == Simulation::UPGRADE_ERR_OWNER);
		CHECK(s.unit_upgrade_check(0, a, cent) == Simulation::UPGRADE_OK);
		const int cent_before = s.entity_limit_count(0, LIMIT_CENTURION);
		s.units_.hp[idx(s, a)] = s.units_.max_hp[idx(s, a)] * 0.5f;
		const uint32_t abc[3] = {a, b, c};
		CHECK(s.cmd_upgrade_units(0, abc, 3, cent));
		CHECK(near(s.player(0).food(), 1000.0f - 300.0f) && near(s.player(0).gold(), 1000.0f - 150.0f));
		CHECK(s.entity_limit_count(0, LIMIT_CENTURION) == cent_before + 3);
		CHECK(s.unit_upgrade_check(0, a, cent) == Simulation::UPGRADE_ERR_BUSY);
		// Cancel b: refunded. Kill c: refunded when its death is seen.
		CHECK(s.cmd_cancel_unit_upgrades(0, &b, 1));
		CHECK(near(s.player(0).food(), 800.0f) && near(s.player(0).gold(), 900.0f));
		s.cmd_delete(0, &c, 1);
		for (int t = 0; t < (int)std::lround(11.5f / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
		CHECK(near(s.player(0).food(), 900.0f) && near(s.player(0).gold(), 950.0f));
		CHECK(s.units_.tmpl[idx(s, a)] == sw && s.unit_upgrade_of(a));
		// A save part way keeps the upgrade: the loaded match finishes it on time.
		const char *path = "/tmp/rts0_unit_upgrade.sav";
		CHECK(s.save_state(path));
		static Simulation r;
		CHECK(r.load_state(path));
		std::remove(path);
		for (Simulation *m : {&s, &r}) {
			for (int t = 0; t < (int)std::lround(0.6f / Simulation::FIXED_STEP); ++t) m->tick(Simulation::FIXED_STEP);
			const size_t i = (size_t)m->lookup_unit(a);
			CHECK(m->units_.tmpl[i] == cent);
			CHECK(m->units_.unit_type[i] == UNIT_SWORDSMAN);
			CHECK(!m->unit_upgrade_of(a));
			CHECK(near(m->units_.hp[i], m->units_.max_hp[i] * 0.5f));
			CHECK(near(m->units_.max_hp[i], civ_unit_template(cent).hp));
		}
		CHECK(s.entity_limit_count(0, LIMIT_CENTURION) == cent_before + 1);
		CHECK(s.unit_upgrade_check(0, a, cent) == Simulation::UPGRADE_ERR_NONE); // Centurions don't upgrade

		fresh(s, CIV_PERSIANS);
		const uint16_t imm = tmpl_of("pers/champion_infantry");
		const uint16_t bow = tmpl_of("pers/champion_infantry_archer_upgrade");
		CHECK(civ_unit_template(bow).trainers == bld_bit(BLD_BARRACKS) && (civ_unit_template(bow).flags & ROSTER_RANGED)); // 0.28 trains both
		const uint32_t u = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN, 0.0f, imm);
		s.set_player_resources(0, 0, 0, 0, 0);
		CHECK(s.cmd_upgrade_units(0, &u, 1, bow));
		for (int t = 0; t < (int)std::lround(6.1f / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
		CHECK(s.units_.tmpl[idx(s, u)] == bow);
		CHECK(s.units_.unit_type[idx(s, u)] == civ_unit_template(bow).unit_type);
		CHECK(s.units_.attack_range[idx(s, u)] > 5.0f);
		CHECK(s.cmd_upgrade_units(0, &u, 1, imm));
		for (int t = 0; t < (int)std::lround(6.1f / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
		CHECK(s.units_.tmpl[idx(s, u)] == imm);
	}

	// Promotion to another template keeps a citizen soldier gathering: the new template's
	// gatherer-class capacity, as at spawn (Marian Reforms sends Roman spearmen up the chain).
	{
		fresh(s, CIV_ROMANS);
		const uint16_t sp = tmpl_of("rome/infantry_spearman_b"), con = tmpl_of("rome/infantry_spearman_conscript");
		const size_t a = idx(s, s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN, 0.0f, sp));
		const size_t b = idx(s, s.spawn_unit(3.0f, 0.0f, 0, (UnitType)civ_unit_template(con).unit_type, 0.0f, con));
		CHECK(s.units_.carry_capacity[a] > 0);
		s.promote_unit(a, con, 1);
		CHECK(s.units_.tmpl[a] == con);
		CHECK(s.units_.carry_capacity[a] == s.units_.carry_capacity[b] && s.units_.carry_capacity[a] > 0);
	}

	// RequiredXp auras: Han Xin's Northern Army (a 30 m range aura, Citizen Soldiers -20%)
	// reaches only nearby soldiers of his owner.
	{
		fresh(s, CIV_HAN);
		const uint16_t spear = tmpl_of("han/infantry_spearman_b");
		const size_t a = idx(s, s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN, 0.0f, spear));
		const uint32_t far_id = s.spawn_unit(30.0f, 0.0f, 0, UNIT_SPEARMAN, 0.0f, spear);
		const uint32_t foe_id = s.spawn_unit(0.0f, 3.0f, 1, UNIT_SPEARMAN, 0.0f, spear);
		const float base = s.promotion_required_xp(a);
		s.spawn_unit(2.0f, 0.0f, 0, UNIT_HERO, 0.0f, tmpl_of("han/hero_han_xin_horse"));
		s.rebuild_auras();
		CHECK(near(s.promotion_required_xp(a), base * 0.8f));
		CHECK(near(s.promotion_required_xp(idx(s, far_id)), base));
		CHECK(near(s.promotion_required_xp(idx(s, foe_id)), base));
	}

	// Mercenaries train Advanced (upgrade_rank_advanced_mercenary: RequiredXp 0), so their
	// time is the _a template's, x1.2.
	{
		fresh(s, CIV_KUSHITES);
		const uint16_t merc = tmpl_of("kush/infantry_maceman_merc_b");
		CHECK(merc != CIV_UNIT_TEMPLATE_NONE);
		uint8_t rank = 0;
		CHECK(s.trained_as(0, merc, rank) == merc && rank == 2);
		const uint8_t ty = civ_unit_template(merc).unit_type;
		CHECK(near(s.unit_train_seconds(0, merc) / civ_template_unit_info(merc, ty).train_time /
		           s.unit_tech_mods(0, ty, merc).train_time, 1.2f));
	}

	if (failures) {
		std::printf("test_promotion: %d failure(s)\n", failures);
		return 1;
	}
	std::printf("test_promotion: all passed\n");
	return 0;
}
