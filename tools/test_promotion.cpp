// 0 A.D. unit promotion (Promotion.js, unit_advanced.json, unit_elite.json): RequiredXp per
// template, xp carried over between ranks, rank bonuses, chains that end in another unit,
// templates trained above Basic, RequiredXp technologies and the barracks xp trickle.
//   g++ -std=c++20 -O2 -Isrc tools/test_promotion.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_promotion && /tmp/test_promotion
#define private public
#include "simulation/simulation.h"
#undef private
#include <cmath>
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
	//    to the next rank, and each rank multiplies health by 1.25 and melee damage by 1.2,
	//    keeping the health fraction. Kills alone no longer promote.
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
		CHECK(near(s.units_.attack_damage[i], dmg0 * 1.2f));
		CHECK(near(s.rank_melee_armor(i), 1.0f));
		CHECK(near(s.unit_capture_attack_at(i), unit_capture_attack(hop, UNIT_SPEARMAN) + 0.7f));

		s.gain_xp(i, 75.0f); // 105: Elite with 5 left over
		s.promote_if_due(i);
		CHECK(s.units_.rank[i] == 3);
		CHECK(s.units_.xp[i] == 5);
		CHECK(near(s.units_.max_hp[i], hp0 * 1.25f * 1.25f));
		CHECK(near(s.units_.attack_damage[i], dmg0 * 1.44f));
		CHECK(near(s.rank_melee_armor(i), 2.0f));
		CHECK(near(s.unit_capture_attack_at(i), unit_capture_attack(hop, UNIT_SPEARMAN) + 1.5f));

		// Elite hoplites promote to the City Guard champion (athen/champion_infantry), which
		// has no rank: the champion's own stats and no rank bonus.
		CHECK(near(s.promotion_required_xp(i), 100.0f));
		s.gain_xp(i, 95.0f);
		s.promote_if_due(i);
		const uint16_t guard = tmpl_of("athen/champion_infantry");
		CHECK(s.units_.tmpl[i] == guard);
		CHECK(s.units_.rank[i] == 1);
		CHECK(near(s.units_.max_hp[i], civ_template_unit_info(guard, UNIT_SPEARMAN).hp * s.unit_tech_mods(0, UNIT_SPEARMAN, guard).health));
		CHECK(s.promotion_required_xp(i) < 0.0f);
	}

	// 2. Several ranks in one go (Promotion.js IncreaseXp loop), and ranged units: no melee
	//    bonus or armour, spread is spread_rank_multiplier at the shot. Cavalry need 150.
	{
		fresh(s, CIV_ATHENIANS);
		const uint16_t arch = tmpl_of("athen/infantry_archer_b");
		const size_t a = idx(s, s.spawn_unit(0.0f, 0.0f, 0, UNIT_ARCHER, 0.0f, arch));
		const float dmg0 = s.units_.attack_damage[a];
		s.gain_xp(a, 250.0f);
		s.promote_if_due(a);
		CHECK(s.units_.rank[a] == 3);
		CHECK(s.units_.xp[a] == 50);
		CHECK(near(s.units_.attack_damage[a], dmg0));
		CHECK(s.rank_melee_armor(a) == 0.0f);

		const uint16_t cav = tmpl_of("athen/cavalry_javelineer_b");
		const size_t c = idx(s, s.spawn_unit(4.0f, 0.0f, 0, UNIT_CAVALRY, 0.0f, cav));
		CHECK(near(s.promotion_required_xp(c), 150.0f));

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
	//    citizen soldier promotes straight to Legionary; Army Camp veterans start Advanced.
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
		s.complete_research(0, TECH_ROMAN_REFORMS);
		CHECK(near(s.promotion_required_xp(j), 0.0f));
		s.tick(Simulation::FIXED_STEP);
		CHECK(s.units_.tmpl[j] == leg && s.units_.rank[j] == 3);
		CHECK(s.units_.unit_type[j] == UNIT_SWORDSMAN);
		CHECK(s.units_.tmpl[veteran] == leg);
	}

	// 5. Hoplite Tradition halves a hoplite's RequiredXp (Infantry Spearman !Hero).
	{
		fresh(s, CIV_SPARTANS);
		const size_t i = idx(s, s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN, 0.0f, tmpl_of("spart/infantry_spearman_b")));
		const size_t j = idx(s, s.spawn_unit(2.0f, 0.0f, 0, UNIT_ARCHER, 0.0f, tmpl_of("spart/infantry_javelineer_b")));
		s.complete_research(0, TECH_HOPLITE_TRADITION);
		CHECK(near(s.promotion_required_xp(i), 50.0f));
		CHECK(near(s.promotion_required_xp(j), 100.0f));
	}

	// 6. Rigorous Training: a unit garrisoned in a barracks gains 1 xp a second.
	{
		fresh(s, CIV_ATHENIANS);
		const uint32_t bar = s.spawn_building_world(0.0f, 20.0f, BLD_BARRACKS, 0, 1.0f);
		const uint32_t uid = s.spawn_unit(0.0f, 14.0f, 0, UNIT_SPEARMAN, 0.0f, tmpl_of("athen/infantry_spearman_b"));
		CHECK(s.garrison_unit_now(uid, bar));
		for (int t = 0; t < (int)std::lround(101.0f / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
		const size_t i = idx(s, uid);
		CHECK(s.units_.rank[i] == 2);
		CHECK(s.units_.xp[i] <= 2);
	}

	// 7. Gaia units from a map's _e template start Elite.
	{
		fresh(s, CIV_ATHENIANS);
		const size_t g = idx(s, s.spawn_gaia_unit(0.0f, 0.0f, UNIT_SPEARMAN, "athen/infantry_spearman_e", 3));
		CHECK(s.units_.rank[g] == 3);
		CHECK(s.units_.tmpl[g] == tmpl_of("athen/infantry_spearman_b"));
		CHECK(s.units_.xp[g] == 0);
	}

	if (failures) {
		std::printf("test_promotion: %d failure(s)\n", failures);
		return 1;
	}
	std::printf("test_promotion: all passed\n");
	return 0;
}
