// 0 A.D. paired technologies (pair_*.json): a researcher offers two technologies side by
// side and the player gets one of them; once either is researched or started, the other
// is gone. RTS0 has the Ptolemaic cult (Pharaonic Cult or Serapis Cult, temple), the two
// Han ministry pairs (Civil Engineering, Civil Service) and the Mauryan farmstead pair
// (Wicker Baskets or Ahimsa). Serapis Cult raises the player's own trickle: +2 metal a
// second (template_player.xml ResourceTrickle, "affects": ["Player"]).
//   g++ -std=c++20 -O2 -Isrc tools/test_paired_techs.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_paired_techs && /tmp/test_paired_techs
#define private public
#include "simulation/simulation.h"
#undef private
#include "network/turn_manager.h"
#include <cmath>
#include <cstring>
#include <cstdio>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

static void run(Simulation &s, float seconds) {
	for (int t = 0; t < (int)std::lround(seconds / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
}

static void fresh(Simulation &s, Civilization civ) {
	s.set_sim_seed(5);
	s.reset();
	s.players_[0].civ = civ;
	s.players_[1].civ = CIV_ATHENIANS;
	for (uint8_t p = 0; p < 2; ++p) s.players_[p].active = 1, s.players_[p].is_ai = 0; // no Petra spending
	s.set_victory_condition(VICTORY_ENDLESS);
	s.spawn_building_world(-50.0f, -50.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(50.0f, 50.0f, BLD_TOWN_CENTER, 1, 1.0f);
	s.players_[0].phase = 3;
	for (int r = 0; r < RES_COUNT; ++r) s.players_[0].res[r] = 5000.0f;
}

int main() {
	// 1. The generated pairs point at each other, researched where 0 A.D. offers them.
	{
		const uint8_t pairs[][2] = {
			{TECH_PHARAONIC_CULT, TECH_SERAPIS_CULT},
			{TECH_CIVIL_ENGINEERING_01, TECH_CIVIL_ENGINEERING_02},
			{TECH_CIVIL_SERVICE_01, TECH_CIVIL_SERVICE_02},
			{TECH_GATHER_WICKER_BASKETS_MAUR, TECH_GATHER_AHIMSA},
		};
		for (const auto &p : pairs) {
			CHECK(tech_pair_partner(p[0]) == p[1]);
			CHECK(tech_pair_partner(p[1]) == p[0]);
			CHECK(tech_info(p[0]).researcher == tech_info(p[1]).researcher);
		}
		CHECK(tech_pair_partner(TECH_SUBTERRANEAN_AQUEDUCTS) == TECH_NONE);
		CHECK(tech_info(TECH_SERAPIS_CULT).researcher == BLD_TEMPLE);
		CHECK(tech_info(TECH_CIVIL_ENGINEERING_01).researcher == BLD_MINISTRY);
		CHECK(tech_info(TECH_GATHER_AHIMSA).researcher == BLD_FARMSTEAD);
		CHECK(tech_available_to_civ(TECH_SERAPIS_CULT, CIV_PTOLEMIES));
		CHECK(!tech_available_to_civ(TECH_SERAPIS_CULT, CIV_HAN));
	}

	static Simulation s;

	// 2. Ptolemies start Serapis Cult: Pharaonic Cult is refused at any temple, while it
	//    runs and after; Serapis then pays +2 metal a second, and nothing to the enemy.
	{
		fresh(s, CIV_PTOLEMIES);
		const uint32_t t1 = s.spawn_building_world(-30.0f, -20.0f, BLD_TEMPLE, 0, 1.0f);
		const uint32_t t2 = s.spawn_building_world(-20.0f, -30.0f, BLD_TEMPLE, 0, 1.0f);
		CHECK(s.research_check(0, TECH_PHARAONIC_CULT, t2) == Simulation::TECH_OK);
		CHECK(s.cmd_research(0, t1, TECH_SERAPIS_CULT));
		CHECK(s.research_check(0, TECH_PHARAONIC_CULT, t2) == Simulation::TECH_ERR_PAIRED);
		CHECK(!s.cmd_research(0, t2, TECH_PHARAONIC_CULT));
		run(s, 61.0f);
		CHECK(s.players_[0].has_tech(TECH_SERAPIS_CULT));
		CHECK(s.research_check(0, TECH_PHARAONIC_CULT) == Simulation::TECH_ERR_PAIRED);
		const float m0 = s.players_[0].res[RES_GOLD], e0 = s.players_[1].res[RES_GOLD];
		run(s, 10.0f);
		CHECK(near(s.players_[0].res[RES_GOLD] - m0, 20.0f));
		CHECK(near(s.players_[1].res[RES_GOLD] - e0, 0.0f));
		std::printf("serapis cult: +%.0f metal in 10 s\n", s.players_[0].res[RES_GOLD] - m0);

		// Save/load keeps the pick and the trickle in step.
		const char *path = "/tmp/rts0_test_paired_techs.sav";
		CHECK(s.save_state(path));
		static Simulation l;
		CHECK(l.load_state(path));
		run(s, 3.0f);
		run(l, 3.0f);
		CHECK(TurnManager().boot_sim_hash(s) == TurnManager().boot_sim_hash(l));
		CHECK(l.research_check(0, TECH_PHARAONIC_CULT) == Simulation::TECH_ERR_PAIRED);
		std::remove(path);
	}

	// 3. The other Ptolemaic pick: Pharaonic Cult gives heroes +2 hp a second.
	{
		fresh(s, CIV_PTOLEMIES);
		const uint32_t t1 = s.spawn_building_world(-30.0f, -20.0f, BLD_TEMPLE, 0, 1.0f);
		CHECK(s.cmd_research(0, t1, TECH_PHARAONIC_CULT));
		run(s, 51.0f);
		CHECK(s.players_[0].has_tech(TECH_PHARAONIC_CULT));
		CHECK(near(s.tech_mods(0).regen[UNIT_HERO], 2.0f));
		CHECK(s.research_check(0, TECH_SERAPIS_CULT) == Simulation::TECH_ERR_PAIRED);
		const float m0 = s.players_[0].res[RES_GOLD];
		run(s, 10.0f);
		CHECK(near(s.players_[0].res[RES_GOLD] - m0, 0.0f));
	}

	// 4. Han: Improved Construction (structures -10% cost and build time) shuts out Robust
	//    Architecture, but not the other pair at the same ministry.
	{
		fresh(s, CIV_HAN);
		const uint32_t min = s.spawn_building_world(-30.0f, -20.0f, BLD_MINISTRY, 0, 1.0f);
		CHECK(s.cmd_research(0, min, TECH_CIVIL_ENGINEERING_01));
		run(s, 41.0f);
		CHECK(s.players_[0].has_tech(TECH_CIVIL_ENGINEERING_01));
		CHECK(near(s.tech_mods(0).bld_cost[BLD_HOUSE][RES_WOOD], 0.9f));
		CHECK(near(s.tech_mods(0).bld_build_time[BLD_HOUSE], 0.9f));
		CHECK(s.research_check(0, TECH_CIVIL_ENGINEERING_02, min) == Simulation::TECH_ERR_PAIRED);
		CHECK(s.research_check(0, TECH_CIVIL_SERVICE_02, min) == Simulation::TECH_OK);
		CHECK(s.cmd_research(0, min, TECH_CIVIL_SERVICE_02));
		CHECK(s.research_check(0, TECH_CIVIL_SERVICE_01) == Simulation::TECH_ERR_PAIRED);
		std::printf("han ministry: improved construction, house wood x%.2f\n", s.tech_mods(0).bld_cost[BLD_HOUSE][RES_WOOD]);
	}

	// 5. Han the other way: Robust Architecture, structures +20% health.
	{
		fresh(s, CIV_HAN);
		const uint32_t min = s.spawn_building_world(-30.0f, -20.0f, BLD_MINISTRY, 0, 1.0f);
		const float hp0 = s.structure_max_hp(BLD_HOUSE, 0);
		CHECK(s.cmd_research(0, min, TECH_CIVIL_ENGINEERING_02));
		run(s, 41.0f);
		CHECK(near(s.structure_max_hp(BLD_HOUSE, 0) / hp0, 1.2f));
		CHECK(s.research_check(0, TECH_CIVIL_ENGINEERING_01) == Simulation::TECH_ERR_PAIRED);
	}

	// Seleucid army reforms (pair_unlock_champions_sele, barracks, free and instant): the
	// Traditional Army unlocks the champion pikeman, the Reform Army the champion swordsman,
	// and choosing one closes the other for good.
	{
		auto tmpl = [](const char *id) {
			for (uint16_t i = 0; i < CIV_UNIT_TEMPLATE_COUNT; ++i) if (!std::strcmp(civ_unit_template(i).id, id)) return i;
			return (uint16_t)CIV_UNIT_TEMPLATE_COUNT;
		};
		const uint16_t pike = tmpl("sele/champion_infantry_pikeman"), sword = tmpl("sele/champion_infantry_swordsman");
		CHECK(pike < CIV_UNIT_TEMPLATE_COUNT && sword < CIV_UNIT_TEMPLATE_COUNT);
		CHECK(tech_pair_partner(TECH_TRADITIONAL_ARMY_SELE) == TECH_REFORMED_ARMY_SELE);
		CHECK(tech_info(TECH_TRADITIONAL_ARMY_SELE).researcher == BLD_BARRACKS);
		CHECK(!tech_available_to_civ(TECH_TRADITIONAL_ARMY_SELE, CIV_MACEDONIANS));
		fresh(s, CIV_SELEUCIDS);
		const uint32_t barracks = s.spawn_building_world(-30.0f, -20.0f, BLD_BARRACKS, 0, 1.0f);
		CHECK(!s.cmd_train_template(0, barracks, pike));
		CHECK(!s.cmd_train_template(0, barracks, sword));
		const float food = s.players_[0].res[RES_FOOD];
		CHECK(s.cmd_research(0, barracks, TECH_TRADITIONAL_ARMY_SELE));
		run(s, 1.0f);
		CHECK(s.players_[0].has_tech(TECH_TRADITIONAL_ARMY_SELE) && near(s.players_[0].res[RES_FOOD], food));
		CHECK(s.research_check(0, TECH_REFORMED_ARMY_SELE) == Simulation::TECH_ERR_PAIRED);
		CHECK(s.cmd_train_template(0, barracks, pike));
		CHECK(!s.cmd_train_template(0, barracks, sword));
	}

	if (failures) {
		std::printf("test_paired_techs: %d failure(s)\n", failures);
		return 1;
	}
	std::printf("test_paired_techs: all passed\n");
	return 0;
}
