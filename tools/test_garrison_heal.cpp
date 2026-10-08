// 0 A.D. GarrisonHolder on structures: BuffHeal heals garrisoned units every
// second (temple 3, civic centre 1, barracks 0), the Living Conditions tech
// (garrison_heal.json) adds 1, and below EjectHealth (10% of max health, 7.5%
// for a fortress) a structure unloads its garrison and refuses new units.
//   g++ -std=c++20 -O2 -Isrc tools/test_garrison_heal.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_garrison_heal && /tmp/test_garrison_heal
#include "simulation/simulation.h"
#include <cmath>
#include <cstdio>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static void run(Simulation &s, float seconds) {
	for (int t = 0; t < (int)std::lround(seconds / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
}

static Simulation::Building *find_bld(Simulation &s, uint32_t id) {
	for (const auto &b : s.buildings())
		if (b.id == id) return const_cast<Simulation::Building *>(&b); // test-only pokes (hp)
	return nullptr;
}

static int32_t idx_of(const Simulation &s, uint32_t id) {
	auto uv = s.units();
	for (size_t i = 0; i < uv.count; ++i) if (uv.id[i] == id) return (int32_t)i;
	return -1;
}

static void fresh(Simulation &s) {
	s.set_sim_seed(31);
	s.reset();
	s.set_player_resources(0, 5000, 5000, 5000, 5000);
	s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
}

// A spearman at half health garrisoned in a new `type` structure; returns hp gained in `secs`.
static float healed_in(Simulation &s, uint8_t type, float secs, float x = 0.0f) {
	uint32_t bld = s.spawn_building_world(x, 0.0f, type, 0, 1.0f);
	uint32_t u = s.spawn_unit(x + 1.0f, 8.0f, 0, UNIT_SPEARMAN);
	int32_t i = idx_of(s, u);
	const float half = s.units().max_hp[i] * 0.5f;
	const_cast<float *>(s.units().hp)[i] = half;
	s.order_garrison(&u, 1, bld);
	CHECK(s.get_garrison_count(bld) == 1);
	run(s, secs);
	return s.units().hp[idx_of(s, u)] - half;
}

int main() {
	// 1. Data: template_structure_civic_temple.xml BuffHeal 3, civil_centre 1, wonder 5,
	// corral 1 (EjectHealth 0.5), military 0, fortress EjectHealth 0.075.
	CHECK(building_garrison_heal(BLD_TEMPLE) == 3.0f && building_garrison_heal(BLD_TOWN_CENTER) == 1.0f);
	CHECK(building_garrison_heal(BLD_WONDER) == 5.0f && building_garrison_heal(BLD_BARRACKS) == 0.0f);
	CHECK(building_eject_health(BLD_FORTRESS) == 0.075f && building_eject_health(BLD_CORRAL) == 0.5f);
	CHECK(building_eject_health(BLD_BARRACKS) == 0.1f);
	const TechInfo &lc = tech_info(TECH_GARRISON_HEAL);
	CHECK(lc.food_cost == 500 && lc.gold_cost == 250 && lc.required_phase == 2 && lc.researcher == BLD_TEMPLE);

	static Simulation s;

	// 2. A temple heals 3 hp a second, a barracks none.
	{
		fresh(s);
		const float temple = healed_in(s, BLD_TEMPLE, 10.0f, 0.0f);
		const float barracks = healed_in(s, BLD_BARRACKS, 10.0f, 30.0f);
		std::printf("10 s garrisoned: temple +%.0f hp, barracks +%.0f hp\n", temple, barracks);
		CHECK(temple >= 27.0f && temple <= 33.0f);
		CHECK(barracks == 0.0f);
	}

	// 3. Living Conditions: +1 hp a second in every structure, barracks included.
	{
		fresh(s);
		s.player(0).phase = 2;
		uint32_t temple = s.spawn_building_world(-20.0f, 20.0f, BLD_TEMPLE, 0, 1.0f);
		CHECK(s.cmd_research(0, temple, TECH_GARRISON_HEAL));
		run(s, lc.research_time + 1.0f);
		CHECK(s.player(0).has_tech(TECH_GARRISON_HEAL));
		const float barracks = healed_in(s, BLD_BARRACKS, 10.0f, 30.0f);
		std::printf("10 s in a barracks with Living Conditions: +%.0f hp\n", barracks);
		CHECK(barracks >= 9.0f && barracks <= 11.0f);
	}

	// 4. EjectHealth: a barracks under 10% health unloads and takes no one in.
	{
		fresh(s);
		uint32_t bk = s.spawn_building_world(0.0f, 0.0f, BLD_BARRACKS, 0, 1.0f);
		uint32_t ids[3];
		for (int k = 0; k < 3; ++k) ids[k] = s.spawn_unit(1.0f + k, 8.0f, 0, UNIT_SPEARMAN);
		s.order_garrison(ids, 3, bk);
		CHECK(s.get_garrison_count(bk) == 3);
		Simulation::Building *b = find_bld(s, bk);
		b->hp = b->max_hp * 0.11f;
		run(s, 0.5f);
		CHECK(s.get_garrison_count(bk) == 3);
		b = find_bld(s, bk);
		b->hp = b->max_hp * 0.09f;
		run(s, 0.1f);
		CHECK(s.get_garrison_count(bk) == 0);
		for (uint32_t id : ids) CHECK(s.units().alive[idx_of(s, id)]);
		s.order_garrison(ids, 3, bk);
		CHECK(s.get_garrison_count(bk) == 0);
		// A fortress holds on down to 7.5%.
		uint32_t fort = s.spawn_building_world(40.0f, 40.0f, BLD_FORTRESS, 0, 1.0f);
		b = find_bld(s, fort);
		b->hp = b->max_hp * 0.09f;
		s.order_garrison(ids, 3, fort);
		CHECK(s.get_garrison_count(fort) == 3);
	}

	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
