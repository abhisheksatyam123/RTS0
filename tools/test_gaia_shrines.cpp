// The Gaia shrines and trading posts 0 A.D. 0.28 maps place (structures/shrine_*,
// structures/trading_post_*, mixins/shrine.xml, mixins/trading_post.xml): no Health, so
// they change hands only by capture; no territory; a shrine trains its own civ's Elite
// Healer, a trading post its holder's Traders and is a market for trade and barter.
//   g++ -std=c++20 -O2 -Isrc tools/test_gaia_shrines.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp -lpthread -o /tmp/test_gaia_shrines
//       && /tmp/test_gaia_shrines
#define private public // players_ (phase)
#include "simulation/simulation.h"
#undef private
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static uint16_t roster_id(const char *id) {
	for (uint16_t i = 0; i < CIV_UNIT_TEMPLATE_COUNT; ++i)
		if (std::strcmp(civ_unit_template(i).id, id) == 0) return i;
	return CIV_UNIT_TEMPLATE_NONE;
}

static const Simulation::Building *find_bld(const Simulation &s, uint32_t id) {
	for (const auto &b : s.buildings()) if (b.id == id) return &b;
	return nullptr;
}

static void run(Simulation &s, float seconds) {
	for (int t = 0; t < (int)std::lround(seconds / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
}

int main() {
	// 1. Data.
	{
		for (uint8_t t : {(uint8_t)BLD_SHRINE, (uint8_t)BLD_TRADING_POST}) {
			CHECK(!structure_has_health(t));
			CHECK(civ_building_civ_mask(t) == 0); // nobody builds one
			for (uint8_t c = 0; c < CIV_COUNT; ++c) CHECK(!civ_can_build(c, t));
			CHECK(territory_influence_info(t).weight == 0 && !is_territory_root(t)); // TerritoryInfluence disabled
			CHECK(building_type_info(t).capture_max_points == 500.0f);
			CHECK(!building_ai_info(t).shoots());
		}
		CHECK(structure_has_health(BLD_TEMPLE) && structure_has_health(BLD_MARKET));
		CHECK(building_type_info(BLD_SHRINE).capture_regen_rate == 15.0f); // the temple's RegenRate
		CHECK(building_type_info(BLD_SHRINE).garrison_cap == 20);
		CHECK(building_garrison_heal(BLD_SHRINE) == 3.0f);
		// Classes: a shrine is -Town; the trading post keeps the market's Town class.
		CHECK(building_phase_class(BLD_SHRINE) == 0 && building_phase_class(BLD_TRADING_POST) == 2);
		CHECK(is_conquest_critical(BLD_SHRINE) && !is_conquest_critical(BLD_TRADING_POST));
		CHECK(is_land_market(BLD_TRADING_POST) && is_land_market(BLD_MARKET) && !is_land_market(BLD_DOCK));
		CHECK(building_tech_classes(BLD_TRADING_POST) & TC_MARKET);
		// Researcher: none at a shrine; the market's less its trade technologies at a trading post.
		CHECK(!building_researches_tech(BLD_SHRINE, TECH_HEAL_RATE, CIV_MACEDONIANS));
		CHECK(building_researches_tech(BLD_TEMPLE, TECH_HEAL_RATE, CIV_MACEDONIANS));
		for (uint8_t t : {(uint8_t)TECH_TRADER_HEALTH, (uint8_t)TECH_TRADE_GAIN_01, (uint8_t)TECH_TRADE_GAIN_02,
		                  (uint8_t)TECH_TRADE_COMMERCIAL_TREATY})
			CHECK(!building_researches_tech(BLD_TRADING_POST, t, CIV_ROMANS) && building_researches_tech(BLD_MARKET, t, CIV_ROMANS));
		CHECK(building_researches_tech(BLD_TRADING_POST, TECH_UNLOCK_SHARED_LOS, CIV_ROMANS));
		CHECK(building_researches_tech(BLD_TRADING_POST, TECH_UNLOCK_SHARED_DROPSITES, CIV_ROMANS));
		// Map templates: each keeps its Identity/Civ.
		CHECK(map_structure_template("structures/shrine_greek_02").type == BLD_SHRINE);
		CHECK(map_structure_template("structures/shrine_greek_02").civ == CIV_MACEDONIANS);
		CHECK(map_structure_template("structures/shrine_italian").civ == CIV_ROMANS);
		CHECK(map_structure_template("structures/trading_post_desert").type == BLD_TRADING_POST);
		CHECK(map_structure_template("structures/trading_post_desert").civ == CIV_PERSIANS);
		CHECK(map_structure_template("structures/temple").type == BLD_TYPE_COUNT);
		// Trainers: each native civ's Elite Healer at the shrine, every Trader at the trading post.
		for (const char *id : {"mace/support_healer_e", "rome/support_healer_e", "pers/support_healer_e",
		                       "gaul/support_healer_e", "han/support_healer_e", "ptol/support_healer_e",
		                       "kush/support_healer_e"}) {
			const uint16_t k = roster_id(id);
			CHECK(k != CIV_UNIT_TEMPLATE_NONE && civ_unit_template(k).trainers.test(BLD_SHRINE));
			CHECK(k != CIV_UNIT_TEMPLATE_NONE && civ_unit_template(k).phase == 2); // healers need phase_town
		}
		CHECK(!civ_unit_template(roster_id("mace/support_healer_b")).trainers.test(BLD_SHRINE));
		// The Celtic shrine's elite druid keeps the Deas Celtica aura its _b declares.
		CHECK(civ_unit_template(roster_id("gaul/support_healer_e")).auras[0] >= 0 &&
		      civ_unit_template(roster_id("gaul/support_healer_e")).auras[0] == civ_unit_template(roster_id("gaul/support_healer_b")).auras[0]);
		CHECK(civ_unit_template(roster_id("rome/support_trader")).trainers.test(BLD_TRADING_POST));
	}

	// 2. A Macedonian shrine and a Persian trading post held by Gaia between a Roman and a Gaul.
	static Simulation s;
	s.set_sim_seed(5);
	s.set_team_civ(0, CIV_ROMANS);
	s.set_team_civ(1, CIV_GAULS);
	s.reset();
	for (uint8_t p = 0; p < 2; ++p) s.set_player_resources(p, 5000, 5000, 5000, 5000);
	s.spawn_building_world(-60.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(60.0f, 60.0f, BLD_TOWN_CENTER, 1, 1.0f);
	for (int k = 0; k < 4; ++k) s.spawn_building_world(-80.0f, -30.0f + 12.0f * (float)k, BLD_HOUSE, 0, 1.0f); // room for 20 swordsmen
	const uint32_t shrine = s.spawn_building_world(0.0f, 0.0f, BLD_SHRINE, GAIA_PLAYER, 1.0f);
	s.set_building_civ(shrine, CIV_MACEDONIANS);
	const uint32_t post = s.spawn_building_world(0.0f, 40.0f, BLD_TRADING_POST, GAIA_PLAYER, 1.0f);
	s.set_building_civ(post, CIV_PERSIANS);
	CHECK(find_bld(s, shrine)->civ == CIV_MACEDONIANS && find_bld(s, shrine)->garrison_cap == 20);
	const float hp0 = find_bld(s, shrine)->hp;

	// Attacks can't hurt it: a ram ordered to batter it drops the order, swordsmen told to
	// damage it (no capture) do nothing.
	const uint32_t ram = s.spawn_unit(8.0f, -12.0f, 0, UNIT_SIEGE_RAM, 0.0f);
	constexpr int N = 20;
	uint32_t sw[N];
	for (int k = 0; k < N; ++k) sw[k] = s.spawn_unit(-15.0f + 1.5f * (float)k, -10.0f - 2.0f * (float)(k % 2), 0, UNIT_SWORDSMAN, 0.0f);
	s.order_attack_building(&ram, 1, shrine);
	s.order_attack_building(sw, N, shrine);
	run(s, 10.0f);
	CHECK(find_bld(s, shrine)->hp == hp0 && find_bld(s, shrine)->team == GAIA_PLAYER);

	// Capturing it (the default right-click) takes it.
	s.order_attack_building(sw, N, shrine, false, true);
	for (int t = 0; t < 2400 && find_bld(s, shrine)->team != 0; ++t) s.tick(Simulation::FIXED_STEP);
	const Simulation::Building *sb = find_bld(s, shrine);
	if (sb->team != 0) std::printf("shrine still held by %d, player 0 has %.1f capture points\n", sb->team, sb->capture_points_by_team[0]);
	CHECK(sb && sb->team == 0 && !sb->wrecked && sb->hp == hp0);
	CHECK(sb && sb->civ == CIV_MACEDONIANS); // its template's civ, whoever holds it

	// The Roman holder trains Macedonian Elite Healers there in the Town Phase, not Roman ones.
	const uint16_t mace_e = roster_id("mace/support_healer_e"), rome_e = roster_id("rome/support_healer_e");
	CHECK(!s.cmd_train_template(0, shrine, mace_e, 1)); // healers need phase_town
	s.players_[0].phase = 2;
	CHECK(!s.cmd_train_template(0, shrine, rome_e, 1));
	CHECK(s.cmd_train_template(0, shrine, mace_e, 1));
	run(s, 60.0f);
	{
		bool trained = false;
		auto v = s.units();
		for (size_t i = 0; i < v.count; ++i)
			if (v.alive[i] && v.team[i] == 0 && s.unit_template(v.id[i]) == mace_e) trained = true;
		CHECK(trained);
	}
	// No technologies, and it can't be deleted.
	CHECK(s.research_check(0, TECH_HEAL_RATE, shrine) != Simulation::TECH_OK);
	s.cmd_delete(0, &shrine, 1);
	CHECK(find_bld(s, shrine)->team == 0 && !find_bld(s, shrine)->wrecked);

	// 3. The trading post: captured, it trains the holder's own Traders and lets it barter.
	CHECK(!s.can_barter(0));
	s.order_attack_building(sw, N, post, false, true);
	for (int t = 0; t < 2400 && find_bld(s, post)->team != 0; ++t) s.tick(Simulation::FIXED_STEP);
	CHECK(find_bld(s, post)->team == 0);
	CHECK(s.can_barter(0));
	CHECK(s.cmd_train_template(0, post, roster_id("rome/support_trader"), 1));
	CHECK(!s.cmd_train_template(0, post, roster_id("pers/support_trader"), 1));
	CHECK(s.research_check(0, TECH_TRADE_GAIN_01, post) != Simulation::TECH_OK);

	// 3b. A civic centre lists units/{native}/support_civilian: a captured Gaulish CC trains
	// Gaulish civilians for its Roman holder, and Roman soldiers alongside them.
	{
		const uint32_t gcc = s.spawn_building_world(-60.0f, 60.0f, BLD_TOWN_CENTER, 0, 1.0f);
		s.set_building_civ(gcc, CIV_GAULS);
		CHECK(!s.cmd_train_template(0, gcc, roster_id("rome/support_civilian"), 1));
		CHECK(s.cmd_train_template(0, gcc, roster_id("gaul/support_civilian"), 1));
		CHECK(!s.cmd_train_template(0, gcc, roster_id("gaul/infantry_spearman_b"), 1));
		CHECK(s.cmd_train_template(0, gcc, roster_id("rome/infantry_swordsman_b"), 1));
		CHECK(building_train_civ(BLD_HOUSE, CIV_GAULS, CIV_ROMANS, civ_unit_template(roster_id("rome/support_civilian_house"))) == CIV_ROMANS);
	}

	// 4. Saves keep the shrine's civ.
	const char *path = "/tmp/rtso_test_gaia_shrines.sav";
	CHECK(s.save_state(path));
	static Simulation r;
	CHECK(r.load_state(path));
	CHECK(find_bld(r, shrine) && find_bld(r, shrine)->type == BLD_SHRINE && find_bld(r, shrine)->civ == CIV_MACEDONIANS);
	std::remove(path);

	if (failures) {
		std::printf("test_gaia_shrines: %d failure(s)\n", failures);
		return 1;
	}
	std::printf("test_gaia_shrines: all passed\n");
	return 0;
}
