// The structures only 0 A.D. 0.28 maps place: bolt and artillery towers (template_structure_
// defensive_tower_bolt/_artillery), the royal and Hellenic stoas, the Persian inn, the Roman
// tent and triumphal arch. No Builder list names them; maps keep their own templates.
//   g++ -std=c++20 -O2 -Isrc tools/test_map_structures.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp -lpthread -o /tmp/test_map_structures
//       && /tmp/test_map_structures
#include "simulation/simulation.h"
#include <cmath>
#include <cstdio>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static float unit_hp(const Simulation &s, uint32_t id) {
	auto v = s.units();
	for (size_t i = 0; i < v.count; ++i)
		if (v.id[i] == id) return v.alive[i] ? v.hp[i] : 0.0f;
	return 0.0f;
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
	for (uint8_t t = BLD_TOWER_BOLT; t <= BLD_TRIUMPHAL_ARCH; ++t) {
		CHECK(civ_building_civ_mask(t) == 0); // nobody builds one
		for (uint8_t c = 0; c < CIV_COUNT; ++c) CHECK(!civ_can_build(c, t));
		CHECK(structure_has_health(t));
	}
	CHECK(map_structure_template("structures/rome/tower_bolt").type == BLD_TOWER_BOLT);
	CHECK(map_structure_template("spart/tower_bolt").civ == CIV_SPARTANS);
	CHECK(map_structure_template("sele/tower_artillery").type == BLD_TOWER_ARTILLERY);
	CHECK(map_structure_template("structures/athen/royal_stoa").type == BLD_STOA);
	CHECK(map_structure_template("structures/hellenic_stoa").type == BLD_HELLENIC_STOA);
	CHECK(map_structure_template("structures/hellenic_stoa").civ == CIV_GAIA);
	CHECK(map_structure_template("structures/pers/inn").type == BLD_INN);
	CHECK(map_structure_template("structures/rome/tent").type == BLD_TENT);
	CHECK(map_structure_template("rome/arch").type == BLD_TRIUMPHAL_ARCH);
	CHECK(map_structure_template("structures/shrine_italian").type == BLD_SHRINE);
	CHECK(map_structure_template("rome/defense_tower").type == BLD_TYPE_COUNT);
	// Bolt tower: one 100 pierce bolt, 30-90 m, no arrows from its garrison.
	const BuildingAIInfo &bolt = building_ai_info(BLD_TOWER_BOLT);
	CHECK(bolt.default_arrows == 1 && bolt.garrison_arrow_multiplier == 0.0f);
	CHECK(bolt.pierce == 100.0f && bolt.prefers_human && !bolt.prefers_inorganic);
	const BuildingAIInfo &art = building_ai_info(BLD_TOWER_ARTILLERY);
	CHECK(art.crush == 90.0f && art.pierce == 0.0f && !art.prefers_human && art.prefers_inorganic);
	CHECK(building_researches_tech(BLD_TOWER_BOLT, TECH_TOWER_HEALTH, CIV_ROMANS));
	CHECK(!building_researches_tech(BLD_TOWER_BOLT, TECH_TOWER_WATCH, CIV_ROMANS));
	CHECK(building_researches_tech(BLD_TOWER, TECH_TOWER_WATCH, CIV_ROMANS));
	// The inn is a house with 15 population; the tent a house that researches nothing.
	CHECK(building_type_info(BLD_INN).pop_bonus == 15 && building_type_info(BLD_TENT).pop_bonus == 5);
	CHECK(building_researches_tech(BLD_INN, TECH_POP_HOUSE_01, CIV_PERSIANS));
	CHECK(!building_researches_tech(BLD_TENT, TECH_POP_HOUSE_01, CIV_ROMANS));
	CHECK(building_tech_classes(BLD_TENT) & TC_HOUSE);
	// Capturable disable="": the arch, the Iberian monument and the Edict Pillar.
	CHECK(building_type_info(BLD_TRIUMPHAL_ARCH).capture_max_points == 0.0f);
	CHECK(building_type_info(BLD_MONUMENT).capture_max_points == 0.0f);
	CHECK(building_type_info(BLD_PILLAR).capture_max_points == 0.0f);
	CHECK(building_type_info(BLD_STOA).capture_max_points == 500.0f);
	// Conquest: the royal stoa (-ConquestCritical), towers and the arch don't keep a player in.
	CHECK(!is_conquest_critical(BLD_STOA) && !is_conquest_critical(BLD_TOWER_BOLT) && !is_conquest_critical(BLD_TRIUMPHAL_ARCH));
	CHECK(is_conquest_critical(BLD_HELLENIC_STOA) && is_conquest_critical(BLD_INN));

	// 2. A Roman player's map structures.
	static Simulation s;
	s.set_sim_seed(3);
	s.set_team_civ(0, CIV_ROMANS);
	s.set_team_civ(1, CIV_GAULS);
	s.reset();
	s.spawn_building_world(-80.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(80.0f, 80.0f, BLD_TOWN_CENTER, 1, 1.0f);
	const int pop0 = s.pop_max(0);
	s.spawn_building_world(-80.0f, -30.0f, BLD_INN, 0, 1.0f);
	s.spawn_building_world(-80.0f, 30.0f, BLD_TENT, 0, 1.0f);
	CHECK(s.pop_max(0) == pop0 + 20);

	// The bolt tower shoots a swordsman between its minimum and maximum range.
	const uint32_t tower = s.spawn_building_world(-40.0f, 0.0f, BLD_TOWER_BOLT, 0, 1.0f);
	CHECK(find_bld(s, tower) && find_bld(s, tower)->hp == building_type_info(BLD_TOWER_BOLT).max_hp);
	const uint32_t near = s.spawn_unit(-40.0f, 4.0f, 1, UNIT_SWORDSMAN, 0.0f);    // inside the 8.4 minimum
	const uint32_t far = s.spawn_unit(-40.0f, 18.0f, 1, UNIT_SWORDSMAN, 0.0f);    // within 25.2
	const uint32_t both[2] = {near, far};
	s.order_set_stance(both, 2, STANCE_STAND_GROUND);
	const float near0 = unit_hp(s, near), far0 = unit_hp(s, far);
	run(s, 6.0f);
	std::printf("bolt tower: near %.1f -> %.1f, far %.1f -> %.1f\n", near0, unit_hp(s, near), far0, unit_hp(s, far));
	CHECK(unit_hp(s, far) < far0);
	CHECK(unit_hp(s, near) == near0); // too close for a bolt

	// The artillery tower prefers what isn't Organic: a ram over a nearer swordsman.
	const uint32_t arty = s.spawn_building_world(40.0f, -60.0f, BLD_TOWER_ARTILLERY, 0, 1.0f);
	(void)arty;
	const uint32_t sw = s.spawn_unit(40.0f, -46.0f, 1, UNIT_SWORDSMAN, 0.0f);
	const uint32_t ram = s.spawn_unit(40.0f, -40.0f, 1, UNIT_SIEGE_RAM, 0.0f);
	const uint32_t pair[2] = {sw, ram};
	s.order_set_stance(pair, 2, STANCE_STAND_GROUND);
	const float sw0 = unit_hp(s, sw), ram0 = unit_hp(s, ram);
	run(s, 40.0f); // PrepareTime 4.5 s, a stone each 5 s at 40 m/s, Spread 6 m
	std::printf("artillery tower: swordsman %.1f -> %.1f, ram %.1f -> %.1f\n", sw0, unit_hp(s, sw), ram0, unit_hp(s, ram));
	CHECK(unit_hp(s, ram) < ram0);
	CHECK(unit_hp(s, sw) == sw0);

	if (failures) {
		std::printf("test_map_structures: %d failure(s)\n", failures);
		return 1;
	}
	std::printf("test_map_structures: all passed\n");
	return 0;
}
