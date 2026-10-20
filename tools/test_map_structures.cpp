// The structures only 0 A.D. 0.28 maps place: bolt and artillery towers (template_structure_
// defensive_tower_bolt/_artillery), the royal and Hellenic stoas, the Persian inn, the Roman
// tent and triumphal arch. No Builder list names them; maps keep their own templates.
//   g++ -std=c++20 -O2 -Isrc tools/test_map_structures.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp -lpthread -o /tmp/test_map_structures
//       && /tmp/test_map_structures
#include "simulation/entity_limits_data.h"
#define private public // the mill test reads a gatherer's rate
#include "simulation/simulation.h"
#undef private
#include <cmath>
#include <cstdio>
#include <vector>

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
	// The rest of 0.28's map-only structures (egypt_3pv3p, the sandboxes, Delphi, the cinema demo).
	CHECK(map_structure_template("structures/pers/hall").type == BLD_HALL);
	CHECK(map_structure_template("structures/pers/warehouse").type == BLD_WAREHOUSE);
	CHECK(map_structure_template("gaul/tavern").type == BLD_TAVERN && map_structure_template("gaul/tavern").civ == CIV_GAULS);
	CHECK(map_structure_template("maur/tower_double").type == BLD_TOWER_DOUBLE);
	CHECK(map_structure_template("kush/shrine").type == BLD_KUSH_SHRINE);
	CHECK(map_structure_template("han/fortress_old").type == BLD_FORTRESS_OLD);
	CHECK(map_structure_template("han/laozigate").type == BLD_LAOZI_GATE);
	CHECK(building_type_info(BLD_HALL).max_hp == 3000.0f && building_type_info(BLD_HALL).garrison_cap == 10);
	CHECK(building_type_info(BLD_WAREHOUSE).pop_bonus == 5 && building_type_info(BLD_TAVERN).pop_bonus == 10);
	CHECK(building_phase_class(BLD_TAVERN) == 2 && building_phase_class(BLD_WAREHOUSE) == 1);
	// maur/tower_double: a stone tower with DefaultArrowCount 2.
	CHECK(building_ai_info(BLD_TOWER_DOUBLE).default_arrows == 2 && is_defense_tower(BLD_TOWER_DOUBLE));
	CHECK(building_researches_tech(BLD_TOWER_DOUBLE, TECH_TOWER_WATCH, CIV_MAURYANS));
	CHECK(building_ai_info(BLD_FORTRESS_OLD).default_arrows == 10);
	CHECK(building_eject_health(BLD_FORTRESS_OLD, CIV_HAN) == building_eject_health(BLD_FORTRESS, CIV_HAN)); // a fortress child
	CHECK(structure_loading_range(BLD_FORTRESS_OLD, CIV_HAN) == structure_loading_range(BLD_FORTRESS, CIV_HAN));
	// kush/shrine: a temple with Researcher and Trainer disabled.
	CHECK(!building_researches_tech(BLD_KUSH_SHRINE, TECH_HEAL_RATE, CIV_KUSHITES));
	CHECK(building_tech_classes(BLD_KUSH_SHRINE) & TC_TEMPLE);
	// template_structure_civic_temple's children are all Temple; only the Kushite temple is the
	// Temple of Apedemak (Arakamani's aura).
	for (uint8_t t : {BLD_TEMPLE_AMUN, BLD_TEMPLE_VESTA, BLD_TEMPLE_MARS, BLD_TEMPLE_ISIS}) {
		CHECK(building_tech_classes(t) & TC_TEMPLE);
		CHECK(!(building_tech_classes(t) & TC_TEMPLEOFAPEDEMAK));
	}
	CHECK(building_tech_classes(BLD_TEMPLE) & TC_TEMPLEOFAPEDEMAK);
	// han/laozigate: Capturable disable="".
	CHECK(building_type_info(BLD_LAOZI_GATE).capture_max_points == 0.0f);
	// pers/hall (template_structure_special) and han/laozigate (template_structure) carry no
	// ConquestCritical class.
	CHECK(!is_conquest_critical(BLD_TOWER_DOUBLE) && !is_conquest_critical(BLD_HALL) && !is_conquest_critical(BLD_LAOZI_GATE));

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

	// The Temple of Isis (ptol/temple_2): City phase, one per player, the only Ptolemaic hero trainer.
	CHECK(map_structure_template("structures/ptol/temple_2").type == BLD_TEMPLE_ISIS);
	CHECK(building_required_phase(BLD_TEMPLE_ISIS, CIV_PTOLEMIES) == 3);
	for (uint8_t c = 0; c < CIV_COUNT; ++c) CHECK(civ_can_build(c, BLD_TEMPLE_ISIS) == (c == CIV_PTOLEMIES));
	CHECK(BUILDING_LIMIT_CATEGORY[BLD_TEMPLE_ISIS] == LIMIT_TEMPLE_OF_ISIS);
	CHECK(CIV_ENTITY_LIMITS[CIV_PTOLEMIES][LIMIT_TEMPLE_OF_ISIS] == 1);
	CHECK(building_researches_tech(BLD_TEMPLE_ISIS, TECH_PHARAONIC_CULT, CIV_PTOLEMIES));
	CHECK(!building_researches_tech(BLD_TEMPLE, TECH_PHARAONIC_CULT, CIV_PTOLEMIES));
	CHECK(building_researches_tech(BLD_TEMPLE_ISIS, TECH_HEAL_RATE, CIV_PTOLEMIES));
	int ptol_heroes = 0;
	for (uint16_t id : civ_roster_ids(CIV_PTOLEMIES)) {
		const CivUnitTemplate &u = civ_unit_template(id);
		if (u.unit_type != UNIT_HERO) continue;
		++ptol_heroes;
		CHECK(u.trainers == bld_bit(BLD_TEMPLE_ISIS));
	}
	CHECK(ptol_heroes == 3);

	// The Carthaginian Tophet (cart/tophet): a map-only temple that trains the healer, not the
	// Carthaginian temple's champions, costs 200 stone and 100 metal and doesn't decay.
	CHECK(map_structure_template("structures/cart/tophet").type == BLD_TOPHET);
	CHECK(map_structure_template("cart/tophet").civ == CIV_CARTHAGINIANS);
	for (uint8_t c = 0; c < CIV_COUNT; ++c) CHECK(!civ_can_build(c, BLD_TOPHET));
	CHECK(building_type_info(BLD_TOPHET).stone_cost == 200 && building_type_info(BLD_TOPHET).gold_cost == 100 &&
		building_type_info(BLD_TOPHET).territory_decay_rate == 0.0f);
	CHECK(building_garrison_heal(BLD_TOPHET) == 3.0f && (building_tech_classes(BLD_TOPHET) & TC_TEMPLE));
	CHECK(building_researches_tech(BLD_TOPHET, TECH_HEAL_RATE, CIV_CARTHAGINIANS));
	{
		int healers = 0, champions = 0, temple_champions = 0;
		for (uint16_t id : civ_roster_ids(CIV_CARTHAGINIANS)) {
			const CivUnitTemplate &u = civ_unit_template(id);
			if (building_trains_template(BLD_TOPHET, CIV_CARTHAGINIANS, CIV_CARTHAGINIANS, id))
				(u.unit_type == UNIT_HEALER ? healers : champions)++;
			if (u.unit_type != UNIT_HEALER && building_trains_template(BLD_TEMPLE, CIV_CARTHAGINIANS, CIV_CARTHAGINIANS, id))
				++temple_champions;
		}
		CHECK(healers == 1 && champions == 0 && temple_champions > 0);
		std::printf("tophet: %d healer, %d others (the temple trains %d champions)\n", healers, champions, temple_champions);
	}

	// The Gaia mills (structures/mill_*, {brit,gaul}/rotarymill, mixins/rotary_mill.xml): a
	// food dropsite captured but never damaged, whose farmstead_60 aura makes its owner's grain
	// gatherers within 60 m gather x1.75.
	CHECK(map_structure_template("structures/mill_greek").type == BLD_MILL);
	CHECK(map_structure_template("structures/gaul/rotarymill").civ == CIV_GAULS);
	CHECK(!structure_has_health(BLD_MILL) && Simulation::is_dropsite_type(BLD_MILL) && !civ_building_is_dropsite(BLD_MILL));
	for (uint8_t c = 0; c < CIV_COUNT; ++c) CHECK(!civ_can_build(c, BLD_MILL));
	CHECK(building_type_info(BLD_MILL).capture_max_points == 500.0f && building_type_info(BLD_MILL).capture_garrison_regen_rate == 3.0f);
	auto grain_rate = [](float mill_dist, uint8_t mill_owner) {
		static Simulation m;
		m.set_sim_seed(5);
		m.set_team_civ(0, CIV_ROMANS);
		m.set_team_civ(1, CIV_GAULS);
		m.reset();
		m.spawn_building_world(-80.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
		m.spawn_building_world(80.0f, 80.0f, BLD_TOWN_CENTER, 1, 1.0f);
		const uint32_t field = m.spawn_building_world(-60.0f, -20.0f, BLD_FIELD, 0, 1.0f);
		if (mill_dist > 0.0f) m.spawn_building_world(-60.0f + mill_dist, -20.0f, BLD_MILL, mill_owner, 1.0f);
		uint32_t w = m.spawn_unit(-56.0f, -24.0f, 0, UNIT_FEMALE_CITIZEN);
		run(m, 0.1f);
		m.order_build_or_repair(&w, 1, field); // a right-click on the finished field farms it
		run(m, 4.0f);
		const int32_t k = m.lookup_unit(w);
		return k >= 0 && m.units_.action_state[k] == ACTION_GATHER ? m.units_.gather_rate[k] : 0.0f;
	};
	const float alone = grain_rate(0.0f, 0), by_mill = grain_rate(8.0f, 0);
	std::printf("mill: grain %.4f alone, %.4f beside our mill\n", alone, by_mill);
	CHECK(alone > 0.0f && std::fabs(by_mill - alone * 1.75f) < 1e-4f * alone);
	CHECK(std::fabs(grain_rate(8.0f, 1) - alone) < 1e-6f);     // an enemy's mill helps nobody of ours
	CHECK(std::fabs(grain_rate(8.0f, GAIA_PLAYER) - alone) < 1e-6f);
	CHECK(std::fabs(grain_rate(30.0f, 0) - alone) < 1e-6f);    // 120 m away: out of its 60 m

	// Swordsmen can't hurt a Gaia mill; they capture it.
	{
		static Simulation m;
		m.set_sim_seed(6);
		m.set_team_civ(0, CIV_ROMANS);
		m.set_team_civ(1, CIV_GAULS);
		m.reset();
		m.spawn_building_world(-80.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
		m.spawn_building_world(80.0f, 80.0f, BLD_TOWN_CENTER, 1, 1.0f);
		const uint32_t mill = m.spawn_building_world(-40.0f, 30.0f, BLD_MILL, GAIA_PLAYER, 1.0f);
		const float hp0 = find_bld(m, mill)->hp;
		std::vector<uint32_t> sw;
		for (int i = 0; i < 8; ++i) sw.push_back(m.spawn_unit(-40.0f + (float)(i % 4) * 1.5f - 2.0f, 24.0f - (float)(i / 4) * 1.5f, 0, UNIT_SWORDSMAN));
		m.order_attack_building(sw.data(), sw.size(), mill, false, false); // Ctrl: damage, which it has none to take
		run(m, 10.0f);
		CHECK(find_bld(m, mill) && find_bld(m, mill)->hp == hp0);
		m.order_attack_building(sw.data(), sw.size(), mill, false, true);
		run(m, 120.0f);
		std::printf("mill: owner %d after the capture\n", find_bld(m, mill) ? find_bld(m, mill)->team : -1);
		CHECK(find_bld(m, mill) && find_bld(m, mill)->team == 0);
	}

	if (failures) {
		std::printf("test_map_structures: %d failure(s)\n", failures);
		return 1;
	}
	std::printf("test_map_structures: all passed\n");
	return 0;
}
