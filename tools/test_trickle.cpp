// 0 A.D. ResourceTrickle.js: the Wonder (1 of each every 2 s), animals garrisoned in a corral (1, 2 or 3 food
// every 5 s by size, at most 8 animals) and the Ptolemaic team bonus (+1 food a second).
// Farmsteads and markets trickle nothing. Corrals train their civ's animals. The Persian
// ice house trickles food, more after Subterranean Aqueducts.
//   g++ -std=c++20 -O2 -Isrc tools/test_trickle.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_trickle && /tmp/test_trickle
#define private public
#include "simulation/simulation.h"
#undef private
#include "network/turn_manager.h"
#include <cmath>
#include <algorithm>
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
	// Close enough that the test structures stand in its territory, not on neutral land
	// where they would decay to Gaia (TerritoryDecay.js).
	s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
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

	// 1. The Wonder pays its owner, any player, 1 of each every 2 s; the Han ministry
	//    (no ResourceTrickle in 0.28), farmsteads and markets nothing.
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
			CHECK(near(s.players_[0].res[r] - p0[r], 0.0f));
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
		const uint32_t sheep = s.spawn_fauna(2.0f, 0.0f, FAUNA_SHEEP_TRAINABLE, 0);
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

	// 6. The corral trains its civ's animals at their 0 A.D. cost and time: an Athenian cow
	//    is 150 food and 60 s (45 s after Stockbreeding) and steps out beside the corral, or
	//    is penned when it comes out if the corral's rally point is the corral (autoGarrison);
	//    a Han corral has no sheep and trains its first animal (a goat) instead; cancelling
	//    refunds the animal's cost.
	{
		fresh(s);
		const uint32_t c = s.spawn_building_world(-25.0f, -15.0f, BLD_CORRAL, 0, 1.0f); // in the civic centre's territory
		s.players_[0].res[RES_FOOD] = 1000.0f;
		CHECK(!s.cmd_breed_livestock(0, c, FAUNA_CATTLE_COW_TRAINABLE)); // cattle need the City Phase
		s.players_[0].phase = 3;
		CHECK(s.cmd_breed_livestock(0, c, FAUNA_CATTLE_COW_TRAINABLE));
		CHECK(near(s.players_[0].res[RES_FOOD], 850.0f));
		const auto &q = s.buildings_[s.building_id_to_idx_.at(c)].queue;
		CHECK(q.size() == 1 && q[0].tmpl == FAUNA_CATTLE_COW_TRAINABLE && near(q[0].duration, 60.0f));
		CHECK(!s.cmd_breed_livestock(1, c, FAUNA_GOAT_TRAINABLE)); // not the owner
		s.complete_research(0, TECH_GATHER_ANIMALS_STOCKBREEDING);
		CHECK(near(s.livestock_train_time(0, FAUNA_CATTLE_COW_TRAINABLE), 45.0f));
		// Petra's config.js Cheat: an Easy AI trains animals slower too, like any unit.
		s.set_player_active(1, true, true);
		s.set_petra_difficulty(PETRA_EASY);
		CHECK(s.ai_time_mult(1) > 1.0f && near(s.livestock_train_time(1, FAUNA_CATTLE_COW_TRAINABLE), 60.0f * s.ai_time_mult(1)));
		CHECK(s.cmd_breed_livestock(0, c, FAUNA_DEER)); // not on the list: the civ's sheep
		CHECK(q.size() == 2 && q[1].tmpl == FAUNA_SHEEP_TRAINABLE && near(q[1].duration, 30.0f));
		CHECK(near(s.players_[0].res[RES_FOOD], 800.0f));
		CHECK(s.cmd_cancel_train(0, c, 1));
		CHECK(near(s.players_[0].res[RES_FOOD], 850.0f));
		run(s, 61.0f);
		CHECK(s.corral_count(c) == 0);
		bool cow = false;
		for (size_t f = 0; f < s.fauna_.count; ++f)
			cow |= s.fauna_.alive[f] && s.fauna_.fauna_type[f] == FAUNA_CATTLE_COW_TRAINABLE && s.fauna_.corral_id[f] == 0 &&
				s.fauna_.behavior[f] != FAUNA_CORRALLED;
		CHECK(cow);
		auto &cb = s.buildings_[s.building_id_to_idx_.at(c)];
		cb.has_rally_point = true;
		cb.rally_target_bld_id = c;
		cb.rally_x = cb.x;
		cb.rally_z = cb.z;
		CHECK(s.cmd_breed_livestock(0, c, FAUNA_CATTLE_COW_TRAINABLE));
		run(s, 46.0f);
		CHECK(s.corral_count(c) == 1);
		cow = false;
		for (size_t f = 0; f < s.fauna_.count; ++f)
			cow |= s.fauna_.alive[f] && s.fauna_.fauna_type[f] == FAUNA_CATTLE_COW_TRAINABLE && s.fauna_.corral_id[f] == c;
		CHECK(cow);
		CHECK(Simulation::corral_animal_for(CIV_HAN, FAUNA_SHEEP_TRAINABLE) == FAUNA_GOAT_TRAINABLE);
		CHECK(Simulation::corral_animal_for(CIV_KUSHITES, FAUNA_CATTLE_SANGA_TRAINABLE) == FAUNA_CATTLE_SANGA_TRAINABLE);
		std::printf("corral: cow 150 food, 60 s; out beside it, penned with a rally on the corral\n");
	}

	// 6b. Batch training at the corral (Trainer.js, corral BatchTimeModifier 0.7): five sheep
	//     cost five times one and take 40 s x 5^0.7 (BuildTime x n^modifier); they all come out when it ends, and
	//     autoqueue queues the same batch again.
	{
		fresh(s);
		const uint32_t c = s.spawn_building_world(-25.0f, -15.0f, BLD_CORRAL, 0, 1.0f); // in the civic centre's territory
		s.players_[0].res[RES_FOOD] = 1000.0f;
		CHECK(s.cmd_breed_livestock(0, c, FAUNA_SHEEP_TRAINABLE, 5));
		CHECK(near(s.players_[0].res[RES_FOOD], 750.0f));
		const auto &q = s.buildings_[s.building_id_to_idx_.at(c)].queue;
		const float t5 = 40.0f * std::pow(5.0f, 0.7f);
		CHECK(q.size() == 1 && q[0].count == 5 && near(q[0].duration, t5));
		CHECK(s.cmd_set_autoqueue(0, c, true));
		run(s, t5 + 1.0f);
		int sheep = 0;
		for (size_t f = 0; f < s.fauna_.count; ++f)
			sheep += s.fauna_.alive[f] && s.fauna_.fauna_type[f] == FAUNA_SHEEP_TRAINABLE;
		CHECK(sheep == 5);
		CHECK(q.size() == 1 && q[0].count == 5);
		CHECK(near(s.players_[0].res[RES_FOOD], 500.0f));
		CHECK(s.cmd_cancel_train(0, c, 0));
		CHECK(near(s.players_[0].res[RES_FOOD], 750.0f));
		std::printf("corral batch: 5 sheep 250 food, %.0f s\n", t5);
	}

	// 7. The Persian ice house (structures/pers/ice_house.xml): 1 food every 2 s, 2 after
	//    Subterranean Aqueducts, researched at the ice house. Other civs can't build one.
	{
		fresh(s);
		s.players_[0].civ = CIV_PERSIANS;
		CHECK(civ_building_civ_mask(BLD_ICE_HOUSE) == civ_bit(CIV_PERSIANS));
		const uint32_t ice = s.spawn_building_world(-30.0f, -20.0f, BLD_ICE_HOUSE, 0, 1.0f);
		float f0 = s.players_[0].res[RES_FOOD];
		run(s, 10.0f);
		CHECK(near(s.players_[0].res[RES_FOOD] - f0, 5.0f));
		CHECK(tech_info(TECH_SUBTERRANEAN_AQUEDUCTS).researcher == BLD_ICE_HOUSE);
		s.players_[0].phase = 3;
		for (int r = 0; r < RES_COUNT; ++r) s.players_[0].res[r] = 1000.0f;
		CHECK(s.cmd_research(0, ice, TECH_SUBTERRANEAN_AQUEDUCTS));
		run(s, 41.0f);
		CHECK(s.players_[0].has_tech(TECH_SUBTERRANEAN_AQUEDUCTS));
		f0 = s.players_[0].res[RES_FOOD];
		run(s, 10.0f);
		CHECK(near(s.players_[0].res[RES_FOOD] - f0, 10.0f));
		std::printf("ice house: +%.0f food in 10 s with Subterranean Aqueducts\n", s.players_[0].res[RES_FOOD] - f0);
	}

	// 8. Each structure keeps its own timer (ResourceTrickle.js CheckTimer: SetInterval when it
	//    is finished): a wonder finished 1 s after another pays 1 s after it, every 2 s; a
	//    corral's timer starts with its first animal and stops when it empties.
	{
		fresh(s);
		s.spawn_building_world(30.0f, 30.0f, BLD_WONDER, 1, 1.0f);
		run(s, 1.0f);
		s.spawn_building_world(60.0f, 20.0f, BLD_WONDER, 1, 1.0f);
		std::vector<int> pays; // ticks after the second wonder at which wood rose, and by how much
		std::vector<float> by;
		for (int t = 1; t <= 100; ++t) {
			const float w = s.players_[1].res[RES_WOOD];
			s.tick(Simulation::FIXED_STEP);
			if (s.players_[1].res[RES_WOOD] > w + 0.5f) pays.push_back(t), by.push_back(s.players_[1].res[RES_WOOD] - w);
		}
		std::printf("two wonders 1 s apart: wood paid at ticks");
		for (size_t k = 0; k < pays.size(); ++k) std::printf(" %d(+%.0f)", pays[k], by[k]);
		std::printf("\n");
		// The first wonder pays every 2 s from its start (ticks 20, 60, 100 here), the second
		// every 2 s from its own (ticks 40, 80): one each, never both on one tick.
		CHECK((pays == std::vector<int>{20, 40, 60, 80, 100}));
		CHECK(std::all_of(by.begin(), by.end(), [](float v) { return near(v, 1.0f); }));

		fresh(s);
		corral = s.spawn_building_world(10.0f, 10.0f, BLD_CORRAL, 0, 1.0f);
		run(s, 0.6f);
		corral_animal(s, corral, 10.0f, 10.5f, FAUNA_SHEEP);
		int first = 0;
		for (int t = 1; t <= 200 && !first; ++t) {
			const float food = s.players_[0].res[RES_FOOD];
			s.tick(Simulation::FIXED_STEP);
			if (s.players_[0].res[RES_FOOD] > food + 0.5f) first = t;
		}
		std::printf("corral penned 0.6 s in: first food %d ticks after\n", first);
		CHECK(first == 100); // the first tick that sees it penned starts the timer: 5 s on
		const size_t f = s.fauna_id_to_idx_.at(s.fauna_.id[s.fauna_.count - 1]);
		s.fauna_.behavior[f] = FAUNA_IDLE_GRAZE;
		s.fauna_.corral_id[f] = 0;
		s.tick(Simulation::FIXED_STEP);
		for (const auto &b : s.buildings_)
			if (b.id == corral) CHECK(b.trickle_tick == -1);
	}

	if (failures) {
		std::printf("test_trickle: %d failure(s)\n", failures);
		return 1;
	}
	std::printf("test_trickle: all passed\n");
	return 0;
}
