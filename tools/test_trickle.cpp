// 0 A.D. ResourceTrickle.js: the Wonder (1 of each every 2 s), the Han Imperial Ministry
// (0.1 wood, stone and metal every 2 s), animals garrisoned in a corral (1, 2 or 3 food
// every 5 s by size, at most 8 animals) and the Ptolemaic team bonus (+1 food a second).
// Farmsteads and markets trickle nothing.
//   g++ -std=c++20 -O2 -Isrc tools/test_trickle.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_trickle && /tmp/test_trickle
#define private public
#include "simulation/simulation.h"
#undef private
#include "network/turn_manager.h"
#include <cmath>
#include <cstdio>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

static void run(Simulation &s, float seconds) {
	for (int t = 0; t < (int)std::lround(seconds / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
}

static void fresh(Simulation &s) {
	s.set_sim_seed(3);
	s.reset();
	s.players_[0].civ = CIV_ATHENIANS;
	s.players_[1].civ = CIV_ATHENIANS;
	for (uint8_t p = 0; p < 2; ++p) s.players_[p].active = 1, s.players_[p].is_ai = 0; // no Petra spending
	s.set_victory_condition(VICTORY_ENDLESS);
	s.spawn_building_world(-50.0f, -50.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(50.0f, 50.0f, BLD_TOWN_CENTER, 1, 1.0f);
}

static void corral_animal(Simulation &s, uint32_t corral, float x, float z, uint8_t kind) {
	const uint32_t id = s.spawn_fauna(x, z, kind);
	const size_t f = s.fauna_id_to_idx_.at(id);
	s.fauna_.behavior[f] = FAUNA_CORRALLED;
	s.fauna_.corral_id[f] = corral;
}

int main() {
	static Simulation s;

	// 1. The Wonder pays its owner, any player, 1 of each every 2 s; the Ministry 0.1 wood,
	//    stone and metal; farmsteads and markets nothing.
	{
		fresh(s);
		s.spawn_building_world(30.0f, 30.0f, BLD_WONDER, 1, 1.0f);
		s.spawn_building_world(-30.0f, -20.0f, BLD_MINISTRY, 0, 1.0f);
		s.spawn_building_world(-20.0f, -30.0f, BLD_FARMSTEAD, 0, 1.0f);
		s.spawn_building_world(-30.0f, -36.0f, BLD_MARKET, 0, 1.0f);
		float p0[RES_COUNT], p1[RES_COUNT];
		for (int r = 0; r < RES_COUNT; ++r) p0[r] = s.players_[0].res[r], p1[r] = s.players_[1].res[r];
		run(s, 10.0f);
		for (uint8_t r : {RES_FOOD, RES_WOOD, RES_STONE, RES_GOLD})
			CHECK(near(s.players_[1].res[r] - p1[r], 5.0f));
		CHECK(near(s.players_[0].res[RES_FOOD] - p0[RES_FOOD], 0.0f));
		for (uint8_t r : {RES_WOOD, RES_STONE, RES_GOLD})
			CHECK(near(s.players_[0].res[r] - p0[r], 0.5f));
		std::printf("wonder +%.1f of each in 10 s, ministry +%.1f wood\n",
				s.players_[1].res[RES_WOOD] - p1[RES_WOOD], s.players_[0].res[RES_WOOD] - p0[RES_WOOD]);
	}

	// 2. A corral pays 1 food per sheep, 2 per pig and 3 per cow every 5 s; one still under
	//    construction pays nothing.
	uint32_t corral = 0;
	{
		fresh(s);
		corral = s.spawn_building_world(10.0f, 10.0f, BLD_CORRAL, 0, 1.0f);
		const uint32_t site = s.spawn_building_world(-10.0f, 20.0f, BLD_CORRAL, 0, 0.0f);
		corral_animal(s, corral, 10.0f, 10.5f, FAUNA_SHEEP);
		corral_animal(s, corral, 10.5f, 10.0f, FAUNA_PIG);
		corral_animal(s, corral, 9.5f, 10.0f, FAUNA_CATTLE_COW);
		corral_animal(s, site, -10.0f, 20.5f, FAUNA_CATTLE_COW);
		CHECK(s.corral_count(corral) == 3);
		const float food0 = s.players_[0].res[RES_FOOD];
		run(s, 10.0f);
		CHECK(near(s.players_[0].res[RES_FOOD] - food0, 12.0f));
		std::printf("corral with a sheep, a pig and a cow: +%.0f food in 10 s\n", s.players_[0].res[RES_FOOD] - food0);
	}

	// 3. Save/load keeps the trickle on the same ticks.
	{
		const char *path = "/tmp/rts0_test_trickle.sav";
		CHECK(s.save_state(path));
		static Simulation l;
		CHECK(l.load_state(path));
		run(s, 7.0f);
		run(l, 7.0f);
		CHECK(TurnManager().boot_sim_hash(s) == TurnManager().boot_sim_hash(l));
		CHECK(near(s.players_[0].res[RES_FOOD], l.players_[0].res[RES_FOOD]));
		std::remove(path);
	}

	// 4. A corral holds 8 animals (GarrisonHolder/Max): a ninth herded sheep goes elsewhere.
	{
		fresh(s);
		corral = s.spawn_building_world(10.0f, -6.0f, BLD_CORRAL, 0, 1.0f);
		for (int k = 0; k < CORRAL_GARRISON_MAX; ++k) corral_animal(s, corral, 10.0f + 0.2f * k, -6.0f, FAUNA_GOAT);
		const uint32_t herder = s.spawn_unit(0.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
		const uint32_t sheep = s.spawn_fauna(2.0f, 0.0f, FAUNA_SHEEP);
		s.order_herd(&herder, 1, sheep);
		run(s, 30.0f);
		CHECK(s.corral_count(corral) == CORRAL_GARRISON_MAX);
		auto it = s.fauna_id_to_idx_.find(sheep);
		CHECK(it == s.fauna_id_to_idx_.end() || s.fauna_.corral_id[it->second] != corral);
	}

	// 5. The Ptolemaic team bonus: +1 food a second for the Ptolemies and their allies only.
	{
		fresh(s);
		s.players_[0].civ = CIV_PTOLEMIES;
		const float f0 = s.players_[0].res[RES_FOOD], f1 = s.players_[1].res[RES_FOOD];
		run(s, 10.0f);
		CHECK(near(s.players_[0].res[RES_FOOD] - f0, 10.0f));
		CHECK(near(s.players_[1].res[RES_FOOD] - f1, 0.0f));
		std::printf("ptolemies +%.1f food in 10 s, their enemy +%.1f\n", s.players_[0].res[RES_FOOD] - f0, s.players_[1].res[RES_FOOD] - f1);
	}

	if (failures) {
		std::printf("test_trickle: %d failure(s)\n", failures);
		return 1;
	}
	std::printf("test_trickle: all passed\n");
	return 0;
}
