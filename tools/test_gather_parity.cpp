// Gathering details checked against 0 A.D. (A27 simulation/components):
//   - Player.js AddResource has no cap: a player past 9999 keeps what it drops off.
//   - UnitAI REPAIR has no ReturnResource: a builder with a full load keeps building, and
//     ConstructionFinished drops the load at the finished structure when it takes it.
//   - Athenian Silver Owls (phase_town_athen / phase_city_athen): Workers metal.ore x1.1 at
//     Town and again at City; stone untouched.
//   - Loot.js GetResources / GetXp floor the modified amounts.
//   g++ -std=c++20 -O2 -Isrc tools/test_gather_parity.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_gather_parity && /tmp/test_gather_parity
#define private public
#include "simulation/simulation.h"
#undef private
#include <cmath>
#include <cstdio>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static void run(Simulation &s, float seconds) {
	for (int k = 0; k < (int)(seconds / Simulation::FIXED_STEP); ++k) s.tick(Simulation::FIXED_STEP);
}

static void fresh(Simulation &s) {
	s.set_sim_seed(3);
	s.reset();
	for (uint8_t p = 0; p < 2; ++p) s.set_player_active(p, true, false);
	s.set_player_team(0, 0);
	s.set_player_team(1, 1);
}

// Seconds a woman standing at a full mine takes to fill her basket.
static float fill_time(uint8_t civ, uint8_t phase, uint8_t res) {
	static Simulation s;
	fresh(s);
	s.player(0).civ = (Civilization)civ;
	s.player(0).phase = phase;
	const uint32_t mine = s.spawn_resource_world(0.0f, 0.0f, res, 5000);
	const uint32_t woman = s.spawn_unit(1.2f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
	s.order_gather(&woman, 1, mine);
	const int32_t w = s.lookup_unit(woman);
	const int32_t cap = s.carry_cap((size_t)w, res);
	int ticks = 0;
	while (s.units_.carry[w] < cap && ticks < 20000) { s.tick(Simulation::FIXED_STEP); ++ticks; }
	return ticks * Simulation::FIXED_STEP;
}

int main() {
	// 1. No resource cap for any player.
	{
		static Simulation s;
		fresh(s);
		s.player(1).res[RES_WOOD] = 9995.0f;
		const uint32_t store = s.spawn_building_world(20.0f, 0.0f, BLD_STOREHOUSE, 1, 1.0f);
		const uint32_t woman = s.spawn_unit(24.0f, 0.0f, 1, UNIT_FEMALE_CITIZEN);
		const int32_t w = s.lookup_unit(woman);
		s.units_.res_type_carried[w] = RES_WOOD;
		s.units_.carry[w] = 10;
		s.order_return_resource(&woman, 1, store);
		run(s, 6.0f);
		std::printf("  no cap: player 1 wood %d after dropping 10 at 9995\n", s.player_resource(1, RES_WOOD));
		CHECK(s.player_resource(1, RES_WOOD) == 10005);
	}

	// 2. A builder carrying a full load keeps it while she builds; the finished storehouse takes it.
	{
		static Simulation s;
		fresh(s);
		s.spawn_building_world(-40.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
		const uint32_t site = s.spawn_building_world(20.0f, 0.0f, BLD_STOREHOUSE, 0, 0.0f);
		const uint32_t woman = s.spawn_unit(24.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
		const int32_t w = s.lookup_unit(woman);
		s.units_.res_type_carried[w] = RES_WOOD;
		s.units_.carry[w] = s.carry_cap((size_t)w, RES_WOOD);
		const int wood0 = s.player_resource(0, RES_WOOD);
		s.order_build_or_repair(&woman, 1, site);
		run(s, 5.0f);
		const int32_t w2 = s.lookup_unit(woman);
		std::printf("  builder: state %d, carrying %d, x %.1f\n", s.units_.action_state[w2], s.units_.carry[w2], s.units_.pos_x[w2]);
		CHECK(s.units_.action_state[w2] == ACTION_BUILD && s.units_.carry[w2] == s.carry_cap((size_t)w2, RES_WOOD));
		bool done = false;
		for (int k = 0; k < 600 && !done; ++k) {
			run(s, 1.0f);
			for (const auto &b : s.buildings()) if (b.id == site) done = b.build_progress >= 1.0f;
		}
		run(s, 5.0f);
		std::printf("  builder: storehouse finished, wood +%d\n", s.player_resource(0, RES_WOOD) - wood0);
		CHECK(done && s.player_resource(0, RES_WOOD) == wood0 + s.carry_cap((size_t)s.lookup_unit(woman), RES_WOOD));
	}

	// 3. Silver Owls: Athenian women mine metal 10% faster in Town and 21% faster in City,
	// stone at the same rate in every phase; other civs get nothing.
	{
		const float m1 = fill_time(CIV_ATHENIANS, 1, RES_GOLD), m2 = fill_time(CIV_ATHENIANS, 2, RES_GOLD),
				m3 = fill_time(CIV_ATHENIANS, 3, RES_GOLD);
		const float s1 = fill_time(CIV_ATHENIANS, 1, RES_STONE), s3 = fill_time(CIV_ATHENIANS, 3, RES_STONE);
		const float sp1 = fill_time(CIV_SPARTANS, 1, RES_GOLD), sp3 = fill_time(CIV_SPARTANS, 3, RES_GOLD);
		std::printf("  Silver Owls: metal fill %.2f / %.2f / %.2f s by phase, stone %.2f / %.2f s, Spartan metal %.2f / %.2f s\n",
				m1, m2, m3, s1, s3, sp1, sp3);
		CHECK(std::fabs(m1 / m2 - 1.1f) < 0.03f && std::fabs(m1 / m3 - 1.21f) < 0.04f);
		CHECK(std::fabs(s1 - s3) < 0.06f && std::fabs(sp1 - sp3) < 0.06f && std::fabs(sp1 - m1) < 0.06f);
	}

	// 4. Loot is floored after the rank multiplier.
	{
		static Simulation s;
		fresh(s);
		const int food0 = s.player_resource(0, RES_FOOD), metal0 = s.player_resource(0, RES_GOLD);
		LootInfo loot{0, 5, 0, 0, 7};
		s.collect_loot(0, 0, loot, 1.5f, RES_COUNT, 0); // 7.5 -> 7, 10.5 -> 10
		std::printf("  loot: food +%d, metal +%d\n", s.player_resource(0, RES_FOOD) - food0, s.player_resource(0, RES_GOLD) - metal0);
		CHECK(s.player(0).res[RES_FOOD] == (float)food0 + 7.0f && s.player(0).res[RES_GOLD] == (float)metal0 + 10.0f);
	}

	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
