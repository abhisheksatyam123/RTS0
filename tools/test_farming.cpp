// Fields, farming and the new 0 A.D. building types.
//   g++ -std=c++20 -O2 -Isrc tools/test_farming.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_farming && /tmp/test_farming
#include "simulation/simulation.h"
#include "network/turn_manager.h"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static void fresh(Simulation &s) {
	s.set_sim_seed(7);
	s.reset();
	s.set_player_resources(0, 5000, 5000, 5000, 5000);
	// Close enough that the test's building sites are in its territory (BuildRestrictions).
	s.spawn_building_world(-8.0f, -8.0f, BLD_TOWN_CENTER, 0, 1.0f);
}

static void run(Simulation &s, float seconds) {
	for (int t = 0; t < (int)(seconds / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
}

static const Simulation::Building *find_bld(const Simulation &s, uint32_t id) {
	for (const auto &b : s.buildings()) if (b.id == id) return &b;
	return nullptr;
}

static int count_grain(const Simulation &s, int32_t *amount = nullptr) {
	auto r = s.resources();
	int n = 0;
	for (size_t i = 0; i < r.count; ++i) {
		if (r.alive[i] && r.type[i] == RES_FOOD && r.amount[i] >= FIELD_FOOD_SUPPLY / 2) {
			++n;
			if (amount) *amount = r.amount[i];
		}
	}
	return n;
}

static uint32_t grain_at(const Simulation &s, float x, float z) {
	auto r = s.resources();
	for (size_t i = 0; i < r.count; ++i)
		if (r.alive[i] && r.subtype[i] == RES_SUB_GRAIN && r.x[i] == x && r.z[i] == z) return r.id[i];
	return 0;
}

int main() {
	static Simulation s;

	// Table: 26 generic building types plus area D's civ-specific ones; every
	// one has a name, cost and sane stats.
	CHECK(BLD_EMBASSY == 25 && CIV_BUILDING_FIRST == 26 && BLD_TYPE_COUNT == 98);
	for (uint8_t t = 0; t < BLD_TYPE_COUNT; ++t) {
		const BuildingTypeInfo &bi = building_type_info(t);
		CHECK(bi.name && bi.name[0] && bi.max_hp > 0.0f && bi.build_time > 0.0f);
		// Three cells only for the campaign test cities' 48 m obstruction.
		CHECK(bi.footprint == 1 || bi.footprint == 2 ||
			(bi.footprint == 3 && (t == BLD_CAMPAIGN_CITY || t == BLD_CAMPAIGN_CITY_MINOR)));
		CHECK(building_required_phase(t) >= 1 && building_required_phase(t) <= 3);
	}
	CHECK(!building_blocks_movement(BLD_FIELD) && building_blocks_movement(BLD_HOUSE));

	// ResourceSupply.js diminishing returns with DiminishingReturns 0.9.
	CHECK(field_gather_efficiency(1) == 1.0f);
	CHECK(std::fabs(field_gather_efficiency(2) - 0.95f) < 1e-5f);
	CHECK(std::fabs(field_gather_efficiency(5) - 0.81902f) < 1e-4f);

	// Phase gating in cmd_build: forge needs Town, arsenal needs City.
	fresh(s);
	CHECK(s.cmd_build(0, 0, 0, BLD_FORGE) == 0);
	CHECK(s.cmd_build(0, 0, 0, BLD_ARSENAL) == 0);
	CHECK(s.cmd_build(0, 0, 0, BLD_STABLE) != 0);
	CHECK(s.cmd_build(0, 4, 0, BLD_RANGE) == 0); // in no civ's 0 A.D. Builder list
	s.player(0).phase = 2;
	CHECK(s.cmd_build(0, 8, 0, BLD_FORGE) != 0);
	CHECK(s.cmd_build(0, 12, 0, BLD_ARSENAL) == 0);

	// A field costs 100 wood; its builder starts farming once it is finished.
	// (Each scenario stays under 35 s: the legacy wave mode attacks after that.)
	fresh(s);
	float wood0 = s.wood();
	uint32_t field = s.cmd_build(0, 4, 4, BLD_FIELD);
	CHECK(field != 0);
	CHECK(s.wood() == wood0 - 100.0f);
	s.spawn_building_world(6.0f, 16.0f, BLD_FARMSTEAD, 0, 1.0f);
	const Simulation::Building *fb = find_bld(s, field);
	float fx = fb->x, fz = fb->z;
	uint32_t woman = s.spawn_unit(fx + 4.0f, fz, 0, UNIT_FEMALE_CITIZEN);
	s.order_build_or_repair(&woman, 1, field);
	CHECK(count_grain(s) == 0);
	run(s, 18.0f);
	CHECK(find_bld(s, field)->build_progress >= 1.0f);
	uint32_t grain = grain_at(s, fx, fz);
	CHECK(grain != 0);
	CHECK(count_grain(s) == 1);
	{
		auto u = s.units();
		for (size_t i = 0; i < u.count; ++i)
			if (u.id[i] == woman) CHECK(u.target_resource_id[i] == grain);
	}

	// Food arrives at the farmstead and the field never runs out.
	int gathered0 = s.record().total_food_gathered;
	run(s, 40.0f); // food.grain 0.5 a second, 10 a trip
	CHECK(s.record().total_food_gathered > gathered0);
	int32_t amount = 0;
	CHECK(count_grain(s, &amount) == 1 && amount == FIELD_FOOD_SUPPLY);

	// A field doesn't block movement, so its foundation starts at once (Foundation.js Commit
	// checks blockers only for BlockMovement obstructions): a unit that can't make way (a
	// packed catapult) standing on the site doesn't keep the builder waiting.
	fresh(s);
	field = s.cmd_build(0, 4, 4, BLD_FIELD);
	fb = find_bld(s, field);
	fx = fb->x, fz = fb->z;
	s.spawn_unit(fx, fz, 0, UNIT_CATAPULT);
	woman = s.spawn_unit(fx + 4.0f, fz, 0, UNIT_FEMALE_CITIZEN);
	s.order_build_or_repair(&woman, 1, field);
	run(s, 6.0f);
	CHECK(find_bld(s, field)->build_progress > 0.0f);
	CHECK(!building_blocks_movement(BLD_LAOZI_GATE)); // han/laozigate: BlockMovement false

	// Right-clicking a finished field with workers farms it; at most 5 per
	// field, the rest move to a free field or stop.
	fresh(s);
	field = s.spawn_building_world(10.0f, 10.0f, BLD_FIELD, 0, 1.0f);
	std::vector<uint32_t> women;
	for (int k = 0; k < 7; ++k) women.push_back(s.spawn_unit(14.0f, 6.0f + k, 0, UNIT_FEMALE_CITIZEN));
	run(s, 0.1f);
	grain = grain_at(s, 10.0f, 10.0f);
	s.order_build_or_repair(women.data(), women.size(), field);
	run(s, 3.0f);
	auto on = [&](uint32_t rid) {
		auto u = s.units();
		int c = 0;
		for (size_t i = 0; i < u.count; ++i)
			if (u.alive[i] && u.target_resource_id[i] == rid &&
					(u.action_state[i] == ACTION_GATHER || u.action_state[i] == ACTION_RETURN_DROPSITE)) ++c;
		return c;
	};
	CHECK(on(grain) == FIELD_MAX_GATHERERS);
	s.spawn_building_world(20.0f, 10.0f, BLD_FIELD, 0, 1.0f);
	run(s, 0.1f);
	uint32_t grain2 = grain_at(s, 20.0f, 10.0f);
	s.order_build_or_repair(women.data(), women.size(), field);
	run(s, 3.0f);
	CHECK(on(grain) == FIELD_MAX_GATHERERS);
	CHECK(on(grain2) == 2);
	// Units walk onto fields (they don't block movement) and the food comes in.
	s.spawn_building_world(10.0f, 16.0f, BLD_FARMSTEAD, 0, 1.0f);
	int before = s.record().total_food_gathered;
	run(s, 40.0f);
	CHECK(s.record().total_food_gathered > before);

	// Lockstep: two identical sims stay in sync while farming.
	static Simulation a, b;
	for (Simulation *p : {&a, &b}) {
		fresh(*p);
		uint32_t f = p->spawn_building_world(10.0f, 10.0f, BLD_FIELD, 0, 1.0f);
		p->spawn_building_world(10.0f, 18.0f, BLD_FARMSTEAD, 0, 1.0f);
		std::vector<uint32_t> w;
		for (int k = 0; k < 6; ++k) w.push_back(p->spawn_unit(14.0f, 10.0f + k, 0, UNIT_FEMALE_CITIZEN));
		p->order_build_or_repair(w.data(), w.size(), f);
	}
	TurnManager tm;
	run(a, 60.0f);
	run(b, 60.0f);
	CHECK(tm.compute_sim_hash(a) == tm.compute_sim_hash(b));
	CHECK(a.food() > 5000.0f);

	// Save/load adopts the saved supply instead of allocating a new resource.
	const char *path = "/tmp/rts0_test_farming.sav";
	CHECK(a.save_state(path));
	static Simulation l;
	fresh(l);
	CHECK(l.load_state(path));
	uint32_t next_before = l.next_id();
	run(l, 1.0f);
	CHECK(count_grain(l) == 1);
	CHECK(l.next_id() == next_before || l.next_id() > next_before); // ids only grow
	CHECK(grain_at(l, 10.0f, 10.0f) == grain_at(a, 10.0f, 10.0f));

	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
