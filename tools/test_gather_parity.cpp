// Gathering details checked against 0 A.D. (A27 simulation/components):
//   - Player.js AddResource has no cap: a player past 9999 keeps what it drops off.
//   - UnitAI REPAIR has no ReturnResource: a builder with a full load keeps building, and
//     ConstructionFinished drops the load at the finished structure when it takes it.
//   - Athenian Silver Owls (phase_town_athen / phase_city_athen): Workers metal.ore x1.1 at
//     Town and again at City; stone untouched.
//   - Loot.js GetResources / GetXp floor the modified amounts.
//   - ResourceSupply Change timers run per supply: a fruit bush's Regrowth ticks 6 s after its
//     timer started, not on a map-wide clock.
//   - ResourceGatherer.StartGathering fixes the gather rate: a field's diminishing returns are
//     those when the farmer started, until she stops gathering.
//   g++ -std=c++20 -O2 -Isrc tools/test_gather_parity.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_gather_parity && /tmp/test_gather_parity
#define private public
#include "simulation/simulation.h"
#undef private
#include <cmath>
#include <cstdio>
#include <vector>

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

	// 3b. Carthaginian civbonuses cart_stone_01..03: Workers stone.rock x1.25 from the start and
	// again at Town and City (Carthage researches no servants, serfs or slaves); metal untouched.
	{
		const float a1 = fill_time(CIV_ATHENIANS, 1, RES_STONE);
		const float c1 = fill_time(CIV_CARTHAGINIANS, 1, RES_STONE), c2 = fill_time(CIV_CARTHAGINIANS, 2, RES_STONE),
				c3 = fill_time(CIV_CARTHAGINIANS, 3, RES_STONE), cm = fill_time(CIV_CARTHAGINIANS, 3, RES_GOLD);
		const float sm = fill_time(CIV_SPARTANS, 3, RES_GOLD);
		std::printf("  Carthaginian stone: fill %.2f / %.2f / %.2f s by phase (Athenian %.2f), metal %.2f (Spartan %.2f)\n",
				c1, c2, c3, a1, cm, sm);
		CHECK(std::fabs(a1 / c1 - 1.25f) < 0.04f && std::fabs(a1 / c2 - 1.5625f) < 0.06f && std::fabs(a1 / c3 - 1.953f) < 0.08f);
		CHECK(std::fabs(cm - sm) < 0.06f);
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

	// 5. A fruit bush's regrowth timer starts with the bush (ResourceSupply Init), 1 s into the
	// match here, so its ticks come at 7 s, 13 s, ... (not at 6 s, 12 s).
	{
		static Simulation s;
		fresh(s);
		run(s, 1.0f);
		const uint32_t bush = s.spawn_resource_world(30.0f, 30.0f, RES_FOOD, 200);
		auto amount = [&]() { return s.resources_.amount[s.res_id_to_idx_[bush]]; };
		run(s, 1.0f);
		s.resources_.amount[s.res_id_to_idx_[bush]] = 190;
		run(s, 4.5f); // 6.5 s
		const int at_6_5 = amount();
		run(s, 0.6f); // 7.1 s
		const int at_7_1 = amount();
		run(s, 6.0f); // 13.1 s
		std::printf("  fruit timer: %d at 6.5 s, %d at 7.1 s, %d at 13.1 s\n", at_6_5, at_7_1, amount());
		CHECK(at_6_5 == 190 && at_7_1 == 191 && amount() == 192);
	}

	// 6. A farmer alone on a field keeps her full rate until her load ends, though four more
	// join her (diminishing returns at StartGathering); her next load is slower.
	{
		static Simulation s;
		fresh(s);
		s.spawn_building_world(-55.0f, 30.0f, BLD_TOWN_CENTER, 0, 1.0f); // its territory keeps the farmstead from decaying
		s.spawn_building_world(-30.0f, 10.0f, BLD_FARMSTEAD, 0, 1.0f);
		const uint32_t field = s.spawn_building_world(-20.0f, 10.0f, BLD_FIELD, 0, 1.0f);
		const uint32_t first = s.spawn_unit(-16.0f, 10.0f, 0, UNIT_FEMALE_CITIZEN);
		run(s, 0.1f);
		s.order_build_or_repair(&first, 1, field);
		run(s, 2.0f);
		const int32_t f = s.lookup_unit(first);
		const float alone = s.units_.gather_rate[f];
		std::vector<uint32_t> more;
		for (int k = 0; k < 4; ++k) more.push_back(s.spawn_unit(-16.0f, 12.0f + k, 0, UNIT_FEMALE_CITIZEN));
		s.order_build_or_repair(more.data(), more.size(), field);
		run(s, 2.0f);
		const float later = s.units_.gather_rate[s.lookup_unit(first)];
		const float joined = s.units_.gather_rate[s.lookup_unit(more[0])];
		// Her next load, once she has been to the farmstead.
		float next = 0.0f;
		for (int k = 0; k < 2000 && !(next > 0.0f && next < alone); ++k) {
			run(s, 0.1f);
			next = s.units_.gather_rate[s.lookup_unit(first)];
		}
		std::printf("  field: alone %.3f/s, after four join %.3f/s (theirs %.3f/s), her next load %.3f/s\n",
				alone, later, joined, next);
		CHECK(alone > 0.0f && later == alone && joined < alone && std::fabs(next - joined) < 1e-4f);
	}

	// 7. Diminishing returns count the other farmers only (GetTargetGatherRate runs before
	// AddActiveGatherer): two farmers both work at the full rate, a third at 0.95.
	{
		static Simulation s;
		fresh(s);
		s.spawn_building_world(-55.0f, 30.0f, BLD_TOWN_CENTER, 0, 1.0f);
		s.spawn_building_world(-30.0f, 10.0f, BLD_FARMSTEAD, 0, 1.0f);
		const uint32_t field = s.spawn_building_world(-20.0f, 10.0f, BLD_FIELD, 0, 1.0f);
		std::vector<uint32_t> two = {s.spawn_unit(-16.0f, 10.0f, 0, UNIT_FEMALE_CITIZEN), s.spawn_unit(-16.0f, 11.0f, 0, UNIT_FEMALE_CITIZEN)};
		run(s, 0.1f);
		s.order_build_or_repair(two.data(), two.size(), field);
		run(s, 3.0f);
		const float r0 = s.units_.gather_rate[s.lookup_unit(two[0])], r1 = s.units_.gather_rate[s.lookup_unit(two[1])];
		const uint32_t third = s.spawn_unit(-16.0f, 12.0f, 0, UNIT_FEMALE_CITIZEN);
		s.order_build_or_repair(&third, 1, field);
		run(s, 3.0f);
		const float r2 = s.units_.gather_rate[s.lookup_unit(third)];
		std::printf("  field: two farmers %.3f/s and %.3f/s, a third %.3f/s\n", r0, r1, r2);
		CHECK(r0 > 0.0f && std::fabs(r1 - r0) < 1e-5f && std::fabs(r2 - r0 * 0.95f) < 1e-4f);
	}

	// 8. Wicker Baskets (+5 for Workers) reaches citizen infantry, whose template is a Worker.
	{
		static Simulation s;
		fresh(s);
		const uint32_t spear = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN);
		const uint32_t woman = s.spawn_unit(2.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
		const size_t u = (size_t)s.lookup_unit(spear), w = (size_t)s.lookup_unit(woman);
		const int32_t before = s.carry_cap(u, RES_WOOD), wbefore = s.carry_cap(w, RES_WOOD);
		s.player(0).techs.set(TECH_GATHER_CAPACITY_BASKET);
		s.rebuild_all_tech_mods();
		std::printf("  basket: infantry wood %d -> %d, woman %d -> %d (template %d)\n", before, s.carry_cap(u, RES_WOOD),
				wbefore, s.carry_cap(w, RES_WOOD), (int)s.units_.tmpl[u]);
		CHECK(s.units_.tmpl[u] < CIV_UNIT_TEMPLATE_COUNT && s.carry_cap(u, RES_WOOD) == before + 5 &&
		      s.carry_cap(w, RES_WOOD) == wbefore + 5);
	}

	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
