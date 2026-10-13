// 0 A.D. GarrisonHolder on land siege: siege towers (20) and rams (10) carry
// Support and Infantry, a siege tower fires one arrow per garrisoned Infantry
// (none when empty), and both unload below 10% health or when destroyed.
// Passengers survive save/load and are in the lockstep hash. Military Engineers
// cuts siege pack and unpack time by 40%.
//   g++ -std=c++20 -O2 -Isrc tools/test_siege_garrison.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_siege_garrison && /tmp/test_siege_garrison
#include "simulation/simulation.h"
#include "network/turn_manager.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static void run(Simulation &s, float seconds) {
	for (int t = 0; t < (int)std::lround(seconds / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
}

static uint16_t template_id(const char *id) {
	for (uint16_t i = 0; i < CIV_UNIT_TEMPLATE_COUNT; ++i)
		if (std::strcmp(civ_unit_template(i).id, id) == 0) return i;
	return CIV_UNIT_TEMPLATE_NONE;
}

static int32_t idx_of(const Simulation &s, uint32_t id) {
	auto uv = s.units();
	for (size_t i = 0; i < uv.count; ++i) if (uv.id[i] == id) return (int32_t)i;
	return -1;
}

static bool alive(const Simulation &s, uint32_t id) {
	int32_t i = idx_of(s, id);
	return i >= 0 && s.units().alive[i];
}

static float hp_of(const Simulation &s, uint32_t id) {
	int32_t i = idx_of(s, id);
	return i >= 0 ? s.units().hp[i] : -1.0f;
}

static void fresh(Simulation &s) {
	s.set_sim_seed(9);
	s.reset();
	s.set_team_civ(0, CIV_MACEDONIANS);
	s.spawn_building_world(-50.0f, -50.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(50.0f, 50.0f, BLD_TOWN_CENTER, 1, 1.0f);
	s.set_player_active(1, true, false); // player 1's units follow the test's orders, not Petra's defense
}

// Damage a siege tower with `archers` archers inside deals in `secs` to a sturdy enemy.
static float tower_damage(int archers, float secs) {
	static Simulation s;
	fresh(s);
	const uint16_t st_t = template_id("mace/siege_tower");
	uint32_t tower = s.spawn_unit(0.0f, 0.0f, 0, UNIT_BOLT_SHOOTER, 0.0f, st_t);
	std::vector<uint32_t> ids;
	for (int a = 0; a < archers; ++a) ids.push_back(s.spawn_unit(1.0f, 1.0f, 0, UNIT_ARCHER));
	if (!ids.empty()) s.order_board_transport(ids.data(), ids.size(), tower);
	uint32_t target = s.spawn_unit(10.0f, 0.0f, 1, UNIT_SPEARMAN, 20000.0f);
	s.order_set_stance(&target, 1, STANCE_PASSIVE);
	// Count the arrows the tower looses: whether each one hits depends on 0 A.D.'s
	// projectile spread (DelayedDamage.js), so damage is too noisy a measure.
	std::vector<uint32_t> seen;
	for (int t = 0; t < (int)std::lround(secs / Simulation::FIXED_STEP); ++t) {
		s.tick(Simulation::FIXED_STEP);
		for (const auto &p : s.projectiles())
			if (p.source_id == tower && std::find(seen.begin(), seen.end(), p.id) == seen.end()) seen.push_back(p.id);
	}
	return (float)seen.size();
}

int main() {
	const uint16_t st_t = template_id("mace/siege_tower");
	CHECK(st_t != CIV_UNIT_TEMPLATE_NONE);

	static Simulation s;

	// 1. Who is a garrison holder, and its capacity.
	{
		fresh(s);
		uint32_t tower = s.spawn_unit(0.0f, 0.0f, 0, UNIT_BOLT_SHOOTER, 0.0f, st_t);
		uint32_t ram = s.spawn_unit(8.0f, 0.0f, 0, UNIT_SIEGE_RAM);
		uint32_t bolt = s.spawn_unit(16.0f, 0.0f, 0, UNIT_BOLT_SHOOTER);
		CHECK(s.is_transport(tower) && s.get_transport_capacity(tower) == SIEGE_TOWER_GARRISON);
		CHECK(s.is_transport(ram) && s.get_transport_capacity(ram) == SIEGE_RAM_GARRISON);
		CHECK(!s.is_transport(bolt));
	}

	// 2. Support and Infantry board; cavalry and siege don't; full is full.
	{
		fresh(s);
		uint32_t tower = s.spawn_unit(0.0f, 0.0f, 0, UNIT_BOLT_SHOOTER, 0.0f, st_t);
		std::vector<uint32_t> ids;
		for (int a = 0; a < 5; ++a) ids.push_back(s.spawn_unit(1.0f, 0.5f, 0, UNIT_ARCHER));
		for (int f = 0; f < 2; ++f) ids.push_back(s.spawn_unit(0.5f, 1.0f, 0, UNIT_FEMALE_CITIZEN));
		uint32_t cav = s.spawn_unit(1.0f, 1.0f, 0, UNIT_CAVALRY);
		uint32_t ram = s.spawn_unit(1.5f, 1.0f, 0, UNIT_SIEGE_RAM);
		ids.push_back(cav);
		ids.push_back(ram);
		s.order_board_transport(ids.data(), ids.size(), tower);
		CHECK(s.get_transport_passenger_count(tower) == 7);
		CHECK(alive(s, cav) && alive(s, ram));
		CHECK(s.siege_tower_arrows((size_t)idx_of(s, tower)) == 5); // female citizens fire no arrows
		// A unit walking up boards on arrival.
		uint32_t late = s.spawn_unit(-12.0f, 0.0f, 0, UNIT_SPEARMAN);
		s.order_board_transport(&late, 1, tower);
		run(s, 6.0f);
		CHECK(!alive(s, late) && s.get_transport_passenger_count(tower) == 8);
		// The ram's ten seats fill and the eleventh soldier stays out.
		uint32_t ram2 = s.spawn_unit(20.0f, 20.0f, 0, UNIT_SIEGE_RAM);
		std::vector<uint32_t> sw;
		for (int k = 0; k < 11; ++k) sw.push_back(s.spawn_unit(21.0f, 20.5f, 0, UNIT_SWORDSMAN));
		s.order_board_transport(sw.data(), sw.size(), ram2);
		CHECK(s.get_transport_passenger_count(ram2) == 10);
		CHECK(alive(s, sw[10]));
	}

	// 3. Arrows: none when empty, one per garrisoned Infantry.
	{
		const float d0 = tower_damage(0, 12.0f);
		const float d1 = tower_damage(1, 12.0f);
		const float d5 = tower_damage(5, 12.0f);
		std::printf("siege tower arrows in 12 s: empty %.0f, 1 archer %.0f, 5 archers %.0f\n", d0, d1, d5);
		CHECK(d0 == 0.0f);
		CHECK(d1 > 0.0f);
		CHECK(d5 > d1 * 4.0f && d5 < d1 * 6.0f);
	}

	// 4. Unload below 10% health, unharmed and next to the tower.
	{
		fresh(s);
		uint32_t tower = s.spawn_unit(0.0f, 0.0f, 0, UNIT_BOLT_SHOOTER, 0.0f, st_t);
		std::vector<uint32_t> ids;
		for (int a = 0; a < 4; ++a) ids.push_back(s.spawn_unit(1.0f, 0.5f, 0, UNIT_SPEARMAN));
		s.order_board_transport(ids.data(), ids.size(), tower);
		const float full_hp = hp_of(s, ids[0]);
		run(s, 1.0f);
		CHECK(s.get_transport_passenger_count(tower) == 4);
		int32_t ti = idx_of(s, tower);
		const_cast<float *>(s.units().hp)[ti] = s.units().max_hp[ti] * 0.05f;
		run(s, 0.1f);
		CHECK(s.get_transport_passenger_count(tower) == 0);
		for (uint32_t id : ids) CHECK(alive(s, id) && hp_of(s, id) == full_hp);
	}

	// 5. Unload when destroyed.
	{
		fresh(s);
		uint32_t ram = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SIEGE_RAM);
		std::vector<uint32_t> ids;
		for (int a = 0; a < 3; ++a) ids.push_back(s.spawn_unit(0.5f, 0.5f, 0, UNIT_SWORDSMAN));
		s.order_board_transport(ids.data(), ids.size(), ram);
		CHECK(s.get_transport_passenger_count(ram) == 3);
		// Nearly wrecked, then finished off by enemy soldiers.
		const_cast<float *>(s.units().hp)[idx_of(s, ram)] = 2.0f;
		std::vector<uint32_t> foes;
		for (int k = 0; k < 3; ++k) foes.push_back(s.spawn_unit(1.5f + k, 1.5f, 1, UNIT_SWORDSMAN));
		s.order_attack(foes.data(), foes.size(), ram);
		run(s, 10.0f);
		CHECK(!alive(s, ram));
		int out = 0;
		for (uint32_t id : ids) {
			int32_t i = idx_of(s, id);
			if (i >= 0 && s.units().action_state[i] != ACTION_GARRISONED) ++out;
		}
		CHECK(out == 3); // unloaded, not stuck inside (they may have died fighting since)
		CHECK(s.get_transport_passenger_count(ram) == 0);
	}

	// 6. Save/load keeps the passengers and the hash covers them.
	{
		fresh(s);
		uint32_t tower = s.spawn_unit(0.0f, 0.0f, 0, UNIT_BOLT_SHOOTER, 0.0f, st_t);
		std::vector<uint32_t> ids;
		for (int a = 0; a < 3; ++a) ids.push_back(s.spawn_unit(1.0f, 0.5f, 0, UNIT_ARCHER));
		TurnManager tm;
		const uint32_t h0 = tm.compute_sim_hash(s);
		s.order_board_transport(ids.data(), ids.size(), tower);
		CHECK(tm.compute_sim_hash(s) != h0);
		const char *path = "/tmp/rts0_test_siege_garrison.sav";
		CHECK(s.save_state(path));
		static Simulation l;
		fresh(l);
		CHECK(l.load_state(path));
		CHECK(l.get_transport_passenger_count(tower) == 3);
		CHECK(tm.compute_sim_hash(s) == tm.compute_sim_hash(l));
		l.order_disembark_all(tower);
		CHECK(l.get_transport_passenger_count(tower) == 0 && alive(l, ids[0]));
	}

	// 7. Military Engineers (siege_pack_unpack.json): City phase, Arsenal, 800 food,
	// Siege Engines -40% pack and unpack time; not for civs whose arsenal lacks it.
	{
		const TechInfo &ti = tech_info(TECH_SIEGE_PACK_UNPACK);
		CHECK(ti.food_cost == 800 && ti.research_time == 40.0f && ti.required_phase == 3 && ti.researcher == BLD_ARSENAL);
		CHECK((ti.civ_mask >> CIV_MACEDONIANS) & 1);
		CHECK(!((ti.civ_mask >> CIV_BRITONS) & 1) && !((ti.civ_mask >> CIV_PERSIANS) & 1));
		fresh(s);
		uint32_t cat = s.spawn_unit(0.0f, 0.0f, 0, UNIT_CATAPULT);
		uint32_t foe_cat = s.spawn_unit(30.0f, 30.0f, 1, UNIT_CATAPULT);
		const float before = s.pack_time_s((size_t)idx_of(s, cat));
		s.player(0).phase = 3;
		s.set_player_resources(0, 5000, 5000, 5000, 5000);
		uint32_t arsenal = s.spawn_building_world(-30.0f, -30.0f, BLD_ARSENAL, 0, 1.0f); // in the civic centre's territory
		CHECK(s.cmd_research(0, arsenal, TECH_SIEGE_PACK_UNPACK));
		run(s, ti.research_time + 1.0f);
		CHECK(s.player(0).has_tech(TECH_SIEGE_PACK_UNPACK));
		const float after = s.pack_time_s((size_t)idx_of(s, cat));
		std::printf("catapult pack time: %.1f s, with Military Engineers %.1f s\n", before, after);
		CHECK(std::fabs(after - before * 0.6f) < 1e-4f);
		CHECK(s.pack_time_s((size_t)idx_of(s, foe_cat)) == before); // only the researcher's siege
		// Unpacking now finishes in 6 s instead of 10.
		const uint32_t cats[1] = {cat};
		s.order_pack(cats, 1, false);
		run(s, after - 0.5f);
		CHECK(s.units().pack_state[idx_of(s, cat)] != PACK_UNPACKED);
		run(s, 1.0f);
		CHECK(s.units().pack_state[idx_of(s, cat)] == PACK_UNPACKED);
	}

	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
