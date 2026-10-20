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
#include <cstring>

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

static uint16_t tmpl_of(const char *id) {
	for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT; ++t)
		if (std::strcmp(civ_unit_template(t).id, id) == 0) return t;
	return CIV_UNIT_TEMPLATE_NONE;
}

static void fresh(Simulation &s) {
	s.set_sim_seed(31);
	s.reset();
	s.set_player_resources(0, 5000, 5000, 5000, 5000);
	s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
}

// Garrison orders walk to the structure first (UnitAI GARRISON.APPROACHING).
static void garrison(Simulation &s, uint32_t *ids, size_t n, uint32_t bld) {
	s.order_garrison(ids, n, bld);
	for (int t = 0; t < 900 && s.get_garrison_count(bld) < (int)n; ++t) s.tick(0.1f);
}

// A spearman at half health garrisoned in a new `type` structure; returns hp gained in `secs`.
static float healed_in(Simulation &s, uint8_t type, float secs, float x = 0.0f) {
	uint32_t bld = s.spawn_building_world(x, 0.0f, type, 0, 1.0f);
	uint32_t u = s.spawn_unit(x + 1.0f, 8.0f, 0, UNIT_SPEARMAN);
	int32_t i = idx_of(s, u);
	const float half = s.units().max_hp[i] * 0.5f;
	const_cast<float *>(s.units().hp)[i] = half;
	garrison(s, &u, 1, bld);
	CHECK(s.get_garrison_count(bld) == 1);
	const float in = s.units().hp[idx_of(s, u)];
	run(s, secs);
	return s.units().hp[idx_of(s, u)] - in;
}

int main() {
	// 1. Data: template_structure_civic_temple.xml BuffHeal 3, civil_centre 1, wonder 5,
	// corral 1 (EjectHealth 0.5), military 0, fortress EjectHealth 0.075.
	CHECK(building_garrison_heal(BLD_TEMPLE) == 3.0f && building_garrison_heal(BLD_TOWN_CENTER) == 1.0f);
	CHECK(building_garrison_heal(BLD_WONDER) == 5.0f && building_garrison_heal(BLD_BARRACKS) == 0.0f);
	CHECK(building_eject_health(BLD_FORTRESS, CIV_ROMANS) == 0.075f && building_eject_health(BLD_CORRAL, CIV_ROMANS) == 0.5f);
	CHECK(building_eject_health(BLD_BARRACKS, CIV_ROMANS) == 0.1f);
	// han/ministry ejects only on destruction; Han storehouses and farmsteads seat a Minister (0.1).
	CHECK(building_eject_health(BLD_MINISTRY, CIV_HAN) == 0.0f);
	CHECK(building_eject_health(BLD_STOREHOUSE, CIV_HAN) == 0.1f && building_eject_health(BLD_FARMSTEAD, CIV_HAN) == 0.1f);
	CHECK(building_eject_health(BLD_STOREHOUSE, CIV_ROMANS) == 0.0f);
	// 0.28 BuffHeal: temple variants 3, military colonies and wagon encampments 1.
	CHECK(building_garrison_heal(BLD_TEMPLE_AMUN) == 3.0f && building_garrison_heal(BLD_TEMPLE_MARS) == 3.0f &&
	      building_garrison_heal(BLD_TEMPLE_VESTA) == 3.0f);
	CHECK(building_garrison_heal(BLD_MILITARY_COLONY) == 1.0f && building_garrison_heal(BLD_WAGON_ENCAMPMENT) == 1.0f &&
	      building_garrison_heal(BLD_WAGON_ENCAMPMENT_FORTIFIED) == 1.0f);
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
		uint32_t temple = s.spawn_building_world(-30.0f, -20.0f, BLD_TEMPLE, 0, 1.0f); // in the civic centre's territory
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
		garrison(s, ids, 3, bk);
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
		uint32_t fort = s.spawn_building_world(16.0f, 8.0f, BLD_FORTRESS, 0, 1.0f); // close by: under 33% it burns
		b = find_bld(s, fort);
		b->hp = b->max_hp * 0.09f;
		garrison(s, ids, 3, fort);
		CHECK(s.get_garrison_count(fort) == 3);
	}

	// 5. Heal.js HealableClasses Human: elephants and dogs are Human too.
	{
		fresh(s);
		CHECK(unit_is_human(UNIT_WORKER_ELEPHANT) && unit_is_human(UNIT_WAR_DOG));
		uint32_t h = s.spawn_unit(0.0f, 0.0f, 0, UNIT_HEALER);
		uint32_t e = s.spawn_unit(1.0f, 0.0f, 0, UNIT_WORKER_ELEPHANT);
		const int32_t ei = idx_of(s, e);
		const float half = s.units().max_hp[ei] * 0.5f;
		const_cast<float *>(s.units().hp)[ei] = half;
		s.order_heal(&h, 1, e);
		run(s, 6.0f);
		CHECK(s.units().hp[idx_of(s, e)] > half);
	}

	// 6. Hero healers (template_unit_hero_healer: Hippocrates) heal 15 hp per 2 s at range 20 (0.28).
	{
		fresh(s);
		const uint16_t hip = tmpl_of("athen/hero_hippocrates");
		CHECK(hip != CIV_UNIT_TEMPLATE_NONE && unit_is_healer(hip, UNIT_HERO));
		CHECK(!unit_is_healer(CIV_UNIT_TEMPLATE_NONE, UNIT_HERO) && unit_is_healer(CIV_UNIT_TEMPLATE_NONE, UNIT_HEALER));
		uint32_t h = s.spawn_unit(0.0f, 0.0f, 0, UNIT_HERO, 0.0f, hip);
		uint32_t u = s.spawn_unit(4.0f, 0.0f, 0, UNIT_SPEARMAN); // 4 units = 16 m: inside 20, outside 12
		const int32_t ui = idx_of(s, u);
		const float half = s.units().max_hp[ui] * 0.5f;
		const_cast<float *>(s.units().hp)[ui] = half;
		run(s, 7.0f); // idle hero healer finds the injured unit
		const float gained = s.units().hp[idx_of(s, u)] - half;
		std::printf("hero healer: +%.1f hp in 7 s, moved to x=%.2f\n", gained, s.units().pos_x[idx_of(s, h)]);
		CHECK(gained >= 40.0f && gained <= 80.0f); // 3-4 heals of 15 + Hippocrates' regen aura
		CHECK(std::fabs(s.units().pos_x[idx_of(s, h)]) < 1.0f); // healed without walking up
	}

	// 6b. The Seeress (template_unit_champion_support_healer) heals 10 hp at range 20 and is
	//     aggressive; support healers keep 5 hp at 12 and stay passive (UnitAI/DefaultStance).
	{
		fresh(s);
		const uint16_t seer = tmpl_of("germ/champion_healer");
		const uint16_t sup = tmpl_of("germ/support_healer_b");
		const int32_t si = idx_of(s, s.spawn_unit(0.0f, 0.0f, 0, UNIT_HEALER, 0.0f, seer));
		const int32_t hi = idx_of(s, s.spawn_unit(5.0f, 0.0f, 0, UNIT_HEALER, 0.0f, sup));
		CHECK(s.heal_amount_of(si) == CHAMPION_HEAL_HP && s.heal_amount_of(hi) == HEAL_HP);
		CHECK(std::fabs(s.healer_range_of(si) - s.healer_range_of(hi) - 8.0f * COMBAT_DISTANCE_SCALE) < 1e-4f);
		CHECK(s.units().stance[si] == STANCE_AGGRESSIVE && s.units().stance[hi] == STANCE_PASSIVE);
		// Siege towers (template_unit_siege) advance; bolt shooters stand ground; the Mauryan
		// worker elephant (template_unit_support) is passive.
		const int32_t ti = idx_of(s, s.spawn_unit(10.0f, 0.0f, 0, UNIT_BOLT_SHOOTER, 0.0f, tmpl_of("mace/siege_tower")));
		const int32_t bi = idx_of(s, s.spawn_unit(15.0f, 0.0f, 0, UNIT_BOLT_SHOOTER, 0.0f, tmpl_of("rome/siege_scorpio_packed")));
		const int32_t ei = idx_of(s, s.spawn_unit(20.0f, 0.0f, 0, UNIT_WORKER_ELEPHANT, 0.0f, tmpl_of("maur/support_elephant")));
		CHECK(s.units().stance[ti] == STANCE_AGGRESSIVE && s.units().stance[bi] == STANCE_STAND_GROUND);
		CHECK(s.units().stance[ei] == STANCE_PASSIVE);
	}

	// 6d. A passive healer searches its modified vision (UnitAI GetQueryRange takes Vision.GetRange
	//     with techs): heal_range and heal_range_2 add 5 m each, 30 m -> 40 m, so a wounded
	//     spearman 35 m away is found with both and not without.
	for (int techs = 0; techs < 2; ++techs) {
		fresh(s);
		if (techs) {
			s.debug_complete_research(0, TECH_HEAL_RANGE);
			s.debug_complete_research(0, TECH_HEAL_RANGE_2);
		}
		s.spawn_unit(0.0f, 0.0f, 0, UNIT_HEALER, 0.0f, tmpl_of("athen/support_healer_b"));
		const uint32_t u = s.spawn_unit(9.8f, 0.0f, 0, UNIT_SPEARMAN);
		const float half = s.units().max_hp[idx_of(s, u)] * 0.5f;
		const_cast<float *>(s.units().hp)[idx_of(s, u)] = half;
		const_cast<uint8_t *>(s.units().stance)[idx_of(s, u)] = STANCE_STAND_GROUND; // stays where it is
		run(s, 7.0f);
		const float gained = s.units().hp[idx_of(s, u)] - half;
		std::printf("support healer %s heal range techs, wounded at 35 m: +%.1f hp\n", techs ? "with" : "without", gained);
		CHECK(techs ? gained > 0.0f : gained == 0.0f);
	}

	// 6c. Builder (mixins/builder via template_unit_infantry): mercenary infantry build though they
	//     cannot gather; the Neodamodes, champions and mercenary cavalry do not.
	CHECK(unit_can_build(UNIT_ARCHER, tmpl_of("athen/infantry_archer_b")));
	CHECK(unit_can_build(UNIT_SWORDSMAN, tmpl_of("ptol/infantry_swordsman_merc_b")));
	CHECK(!unit_can_build(UNIT_SPEARMAN, tmpl_of("spart/infantry_spearman_neodamodes")));
	CHECK(!unit_can_build(UNIT_SWORDSMAN, tmpl_of("germ/champion_infantry_maceman")));
	CHECK(!unit_can_build(UNIT_CAVALRY, tmpl_of("cart/cavalry_spearman_ital_b")));
	// MinRange (0.28): bolt shooters 15 of 80, the onager 20 of 60, siege towers none.
	CHECK(std::fabs(unit_min_range_ratio(UNIT_BOLT_SHOOTER, tmpl_of("rome/siege_scorpio_packed")) - 15.0f / 80.0f) < 1e-6f);
	CHECK(std::fabs(unit_min_range_ratio(UNIT_CATAPULT, tmpl_of("rome/siege_onager_packed")) - 20.0f / 60.0f) < 1e-6f);
	CHECK(unit_min_range_ratio(UNIT_BOLT_SHOOTER, tmpl_of("mace/siege_tower")) == 0.0f);

	// 7. Han ministry BuffHeal 1.
	CHECK(building_garrison_heal(BLD_MINISTRY) == 1.0f);

	// 8. Foundation.js: a deleted foundation refunds ceil(cost x (1 - progress)).
	{
		fresh(s);
		uint32_t f = s.spawn_building_world(10.0f, 10.0f, BLD_HOUSE, 0, 0.5f);
		CHECK(find_bld(s, f) != nullptr);
		const float wood0 = s.player(0).res[RES_WOOD];
		s.cmd_delete(0, &f, 1);
		const float back = s.player(0).res[RES_WOOD] - wood0;
		std::printf("half-built house deleted: +%.0f wood\n", back);
		CHECK(back == std::ceil(civ_building_type_info(s.player_civ(0), BLD_HOUSE).wood_cost * 0.5f));
		// A finished structure gives nothing back.
		const float wood1 = s.player(0).res[RES_WOOD];
		uint32_t done = s.spawn_building_world(30.0f, 30.0f, BLD_HOUSE, 0, 1.0f);
		s.cmd_delete(0, &done, 1);
		CHECK(s.player(0).res[RES_WOOD] == wood1);
	}

	// 9. Builder.js CanRepair: no repairing an enemy structure.
	{
		fresh(s);
		uint32_t enemy = s.spawn_building_world(10.0f, 0.0f, BLD_HOUSE, 1, 1.0f);
		Simulation::Building *b = find_bld(s, enemy);
		b->hp = b->max_hp * 0.5f;
		uint32_t w = s.spawn_unit(6.0f, 0.0f, 0, UNIT_VILLAGER);
		s.order_build_or_repair(&w, 1, enemy);
		run(s, 10.0f);
		std::printf("enemy house after repair order: %.0f / %.0f\n", find_bld(s, enemy)->hp, find_bld(s, enemy)->max_hp);
		CHECK(find_bld(s, enemy)->hp <= find_bld(s, enemy)->max_hp * 0.5f);
	}

	// 10. 0.28 Pack/Time is 5 s for every stone thrower and bolt shooter, the scorpio included.
	{
		fresh(s);
		const uint16_t sc = tmpl_of("rome/siege_scorpio_packed");
		CHECK(sc != CIV_UNIT_TEMPLATE_NONE);
		uint32_t a = s.spawn_unit(0.0f, 0.0f, 0, UNIT_BOLT_SHOOTER, 0.0f, sc);
		uint32_t b = s.spawn_unit(3.0f, 0.0f, 0, UNIT_BOLT_SHOOTER);
		CHECK(std::fabs(s.pack_time_s(idx_of(s, a)) - 5.0f) < 1e-4f);
		CHECK(std::fabs(s.pack_time_s(idx_of(s, b)) - 5.0f) < 1e-4f);
	}

	// 11. GarrisonHolder/Max per template (template_structure_*.xml, structures/<civ>/*.xml).
	{
		CHECK(building_type_info(BLD_TEMPLE).garrison_cap == 20 && building_type_info(BLD_WONDER).garrison_cap == 50);
		CHECK(building_type_info(BLD_FORTRESS).garrison_cap == 20 && building_type_info(BLD_CORRAL).garrison_cap == 8);
		CHECK(building_type_info(BLD_TOWN_CENTER).garrison_cap == 20 && building_type_info(BLD_BARRACKS).garrison_cap == 10);
		CHECK(building_type_info(BLD_EMBASSY).garrison_cap == 6 && building_type_info(BLD_ARSENAL).garrison_cap == 5);
		CHECK(building_type_info(BLD_KENNEL).garrison_cap == 10 && building_type_info(BLD_FORGE).garrison_cap == 1);
		for (uint8_t t : {BLD_STOREHOUSE, BLD_FARMSTEAD, BLD_MARKET, BLD_DOCK, BLD_WALL_SEGMENT, BLD_GATE})
			CHECK(structure_garrison_max(t, CIV_ATHENIANS) == 0);
		CHECK(structure_garrison_max(BLD_HOUSE, CIV_ATHENIANS) == 6 && structure_garrison_max(BLD_HOUSE, CIV_BRITONS) == 3);
		CHECK(structure_garrison_max(BLD_TOWER, CIV_IBERIANS) == 8 && structure_garrison_max(BLD_TOWER, CIV_ROMANS) == 5);
		CHECK(structure_garrison_max(BLD_STOREHOUSE, CIV_HAN) == 1);
		fresh(s);
		s.player(1).civ = CIV_BRITONS;
		uint32_t h0 = s.spawn_building_world(0.0f, 0.0f, BLD_HOUSE, 0, 1.0f);
		uint32_t h1 = s.spawn_building_world(20.0f, 0.0f, BLD_HOUSE, 1, 1.0f);
		uint32_t st = s.spawn_building_world(0.0f, 20.0f, BLD_STOREHOUSE, 0, 1.0f);
		std::printf("house caps: athen %d, brit %d; storehouse %d\n", s.get_garrison_capacity(h0),
				s.get_garrison_capacity(h1), s.get_garrison_capacity(st));
		CHECK(s.get_garrison_capacity(h0) == 6 && s.get_garrison_capacity(h1) == 3 && s.get_garrison_capacity(st) == 0);
		uint32_t w = s.spawn_unit(0.0f, 24.0f, 0, UNIT_FEMALE_CITIZEN);
		garrison(s, &w, 1, st);
		CHECK(s.get_garrison_count(st) == 0);
	}

	// 12. GarrisonHolder/List: a barracks takes Infantry, not Cavalry; a house Support but not
	// a worker elephant (Support+!Elephant); a stable Cavalry; a wonder Support and Soldiers.
	{
		auto takes = [](uint8_t bld, uint8_t unit) {
			return garrison_list_matches(structure_garrison_list(bld, CIV_ATHENIANS), unit_tech_classes(unit));
		};
		CHECK(takes(BLD_BARRACKS, UNIT_SPEARMAN) && !takes(BLD_BARRACKS, UNIT_CAVALRY) && !takes(BLD_BARRACKS, UNIT_FEMALE_CITIZEN));
		CHECK(takes(BLD_STABLE, UNIT_CAVALRY) && !takes(BLD_STABLE, UNIT_SPEARMAN));
		CHECK(takes(BLD_HOUSE, UNIT_FEMALE_CITIZEN) && !takes(BLD_HOUSE, UNIT_WORKER_ELEPHANT) && !takes(BLD_HOUSE, UNIT_SPEARMAN));
		CHECK(takes(BLD_WONDER, UNIT_CAVALRY) && takes(BLD_WONDER, UNIT_FEMALE_CITIZEN) && !takes(BLD_WONDER, UNIT_CATAPULT));
		CHECK(takes(BLD_FORTRESS, UNIT_CATAPULT) && takes(BLD_ARSENAL, UNIT_SIEGE_RAM) && !takes(BLD_ARSENAL, UNIT_SPEARMAN));
		CHECK(takes(BLD_KENNEL, UNIT_WAR_DOG) && !takes(BLD_TOWN_CENTER, UNIT_WAR_DOG) && takes(BLD_ELEPHANT_STABLE, UNIT_ELEPHANT));
		CHECK(takes(BLD_FORGE, UNIT_HEALER) && takes(BLD_FORGE, UNIT_ARCHER) && !takes(BLD_FORGE, UNIT_CAVALRY));
		CHECK(!takes(BLD_CORRAL, UNIT_FEMALE_CITIZEN) && !takes(BLD_TOWER, UNIT_CAVALRY) && takes(BLD_TOWER, UNIT_FEMALE_CITIZEN));
		CHECK(!garrison_list_matches(structure_garrison_list(BLD_BARRACKS, CIV_ATHENIANS), unit_tech_classes(UNIT_MINISTER)));
		// germ/great_hall: template_structure_special's Support Infantry Cavalry.
		CHECK(takes(BLD_GREAT_HALL, UNIT_SPEARMAN) && takes(BLD_GREAT_HALL, UNIT_CAVALRY) && !takes(BLD_GREAT_HALL, UNIT_CATAPULT));
		CHECK(garrison_list_matches(structure_garrison_list(BLD_BARRACKS, CIV_HAN), unit_tech_classes(UNIT_MINISTER)));
		fresh(s);
		uint32_t bk = s.spawn_building_world(0.0f, 0.0f, BLD_BARRACKS, 0, 1.0f);
		uint32_t cav = s.spawn_unit(1.0f, 9.0f, 0, UNIT_CAVALRY);
		uint32_t sp = s.spawn_unit(3.0f, 9.0f, 0, UNIT_SPEARMAN);
		uint32_t both[2] = {cav, sp};
		s.order_garrison(both, 2, bk);
		run(s, 15.0f);
		const auto *in = s.get_garrisoned_units(bk);
		std::printf("barracks after cavalry + spearman garrison order: %d inside\n", s.get_garrison_count(bk));
		CHECK(s.get_garrison_count(bk) == 1 && in && (*in)[0] == sp);
		CHECK(!s.can_garrison_in(cav, bk) && s.units().alive[idx_of(s, cav)]);
		CHECK(!s.garrison_unit_now(cav, bk));
	}

	// 13. Garrisonable / IsOwnedByMutualAllyOfEntity: a unit garrisons in a mutual ally's
	// structure and stays its owner's; the holder's owner unloads anyone, the ally only its own,
	// an enemy neither garrisons nor unloads; ending the alliance puts it out.
	{
		fresh(s);
		s.set_mutual_ally(0, 2); // player 1 is the tests' Petra, which unloads idle shelterers
		uint32_t temple = s.spawn_building_world(0.0f, 0.0f, BLD_TEMPLE, 2, 1.0f);
		uint32_t mine = s.spawn_unit(1.0f, 10.0f, 0, UNIT_SPEARMAN);
		uint32_t theirs = s.spawn_unit(3.0f, 10.0f, 2, UNIT_SPEARMAN);
		uint32_t foe = s.spawn_unit(5.0f, 10.0f, 3, UNIT_SPEARMAN);
		s.order_set_stance(&foe, 1, STANCE_PASSIVE); // it only tries to get in
		CHECK(s.can_garrison_in(mine, temple) && !s.can_garrison_in(foe, temple));
		auto enter = [&](uint32_t u) { // one more unit in
			const int want = s.get_garrison_count(temple) + 1;
			s.order_garrison(&u, 1, temple);
			for (int t = 0; t < 900 && s.get_garrison_count(temple) < want; ++t) s.tick(0.1f);
		};
		enter(mine);
		enter(theirs);
		s.order_garrison(&foe, 1, temple);
		run(s, 3.0f);
		std::printf("allied temple holds %d (ours, theirs; not the enemy's)\n", s.get_garrison_count(temple));
		CHECK(s.get_garrison_count(temple) == 2);
		CHECK(s.units().team[idx_of(s, mine)] == 0 && s.units().action_state[idx_of(s, mine)] == ACTION_GARRISONED);
		CHECK(s.unloadable_by(0, temple).size() == 1 && s.unloadable_by(2, temple).size() == 2 && s.unloadable_by(3, temple).empty());
		s.cmd_unload(3, temple, nullptr, 0); // an enemy: nothing
		CHECK(s.get_garrison_count(temple) == 2);
		s.cmd_unload(0, temple, &theirs, 1); // the ally can't unload the owner's unit
		CHECK(s.get_garrison_count(temple) == 2);
		s.cmd_unload(0, temple, nullptr, 0); // unload-all-by-owner: only ours
		CHECK(s.get_garrison_count(temple) == 1 && (*s.get_garrisoned_units(temple))[0] == theirs);
		CHECK(s.units().alive[idx_of(s, mine)] && s.units().team[idx_of(s, mine)] == 0);
		enter(mine);
		CHECK(s.get_garrison_count(temple) == 2);
		s.cmd_unload(2, temple, nullptr, 0); // the owner unloads everyone
		CHECK(s.get_garrison_count(temple) == 0 && s.units().team[idx_of(s, mine)] == 0);
		garrison(s, &mine, 1, temple);
		CHECK(s.get_garrison_count(temple) == 1);
		s.set_diplomacy(2, 0, Simulation::DIPLO_NEUTRAL); // OnDiplomacyChanged
		run(s, 0.2f);
		CHECK(s.get_garrison_count(temple) == 0 && s.units().alive[idx_of(s, mine)] && s.units().team[idx_of(s, mine)] == 0);
	}

	// 14. Repairable on units: template_unit_siege and template_unit_ship (RepairTimeRatio 4), not
	// fire ships or living units. A builder walks up and repairs MaxHitpoints / (4 x BuildTime)
	// a second, then stops once the unit is whole (ConstructionFinished -> FinishOrder).
	{
		fresh(s);
		uint32_t ram = s.spawn_unit(10.0f, 0.0f, 0, UNIT_SIEGE_RAM);
		uint32_t ship = s.spawn_unit(30.0f, 30.0f, 0, UNIT_TRIREME);
		uint32_t fire = s.spawn_unit(30.0f, 34.0f, 0, UNIT_TRIREME, 0.0f, tmpl_of("brit/ship_fire"));
		uint32_t man = s.spawn_unit(-10.0f, 0.0f, 0, UNIT_SPEARMAN);
		CHECK(s.is_repairable_unit(ram) && s.is_repairable_unit(ship));
		CHECK(!s.is_repairable_unit(fire) && !s.is_repairable_unit(man));
		const int32_t ri = idx_of(s, ram);
		const float max = s.units().max_hp[ri];
		const_cast<float *>(s.units().hp)[ri] = max * 0.5f;
		uint32_t w = s.spawn_unit(4.0f, 0.0f, 0, UNIT_VILLAGER);
		s.order_build_or_repair(&w, 1, ram);
		CHECK(s.units().action_state[idx_of(s, w)] == ACTION_REPAIR);
		for (int t = 0; t < 400 && s.units().hp[idx_of(s, ram)] <= max * 0.5f; ++t) s.tick(Simulation::FIXED_STEP);
		CHECK(s.units().hp[idx_of(s, ram)] > max * 0.5f);
		const float h0 = s.units().hp[idx_of(s, ram)];
		run(s, 4.0f);
		const float rate = (s.units().hp[idx_of(s, ram)] - h0) / 4.0f;
		const float want = max / (4.0f * civ_template_unit_info(s.units().tmpl[idx_of(s, ram)], UNIT_SIEGE_RAM).train_time);
		std::printf("ram repaired at %.2f hp/s (want %.2f)\n", rate, want);
		CHECK(std::fabs(rate - want) < want * 0.1f);
		run(s, max * 0.5f / want + 2.0f);
		CHECK(s.units().hp[idx_of(s, ram)] == max);
		CHECK(s.units().action_state[idx_of(s, w)] == ACTION_IDLE);
		// No repairing an enemy's ram, a fire ship or a wounded soldier.
		uint32_t foe = s.spawn_unit(-20.0f, 10.0f, 1, UNIT_SIEGE_RAM);
		const_cast<float *>(s.units().hp)[idx_of(s, foe)] = 100.0f;
		const_cast<float *>(s.units().hp)[idx_of(s, man)] = 10.0f;
		s.order_build_or_repair(&w, 1, foe);
		CHECK(s.units().action_state[idx_of(s, w)] != ACTION_REPAIR);
		s.order_build_or_repair(&w, 1, man);
		CHECK(s.units().action_state[idx_of(s, w)] != ACTION_REPAIR);
	}

	// 15. A structure keeps its template through a capture (0 A.D. changes only the owner): a Han
	// storehouse taken by the Athenians keeps its Minister's seat; an Athenian barracks taken by
	// the Han gains none. A civ chosen after the start re-templates the player's structures.
	{
		fresh(s);
		s.set_team_civ(0, CIV_HAN);
		s.set_team_civ(1, CIV_ATHENIANS);
		uint32_t st = s.spawn_building_world(0.0f, 20.0f, BLD_STOREHOUSE, 0, 1.0f);
		uint32_t bk = s.spawn_building_world(30.0f, 0.0f, BLD_BARRACKS, 1, 1.0f);
		CHECK(s.get_garrison_capacity(st) == 1);
		find_bld(s, st)->team = 1; // captured (test poke: only the owner changes, as in tick_capture)
		find_bld(s, bk)->team = 0;
		std::printf("captured: han storehouse holds %d, athenian barracks takes a minister %d\n",
				s.get_garrison_capacity(st), (int)s.can_garrison_in(s.spawn_unit(32.0f, 9.0f, 0, UNIT_MINISTER), bk));
		CHECK(s.get_garrison_capacity(st) == 1);
		uint32_t min1 = s.spawn_unit(2.0f, 24.0f, 1, UNIT_MINISTER);
		CHECK(s.can_garrison_in(min1, st));
		uint32_t min0 = s.spawn_unit(31.0f, 9.0f, 0, UNIT_MINISTER);
		CHECK(!s.can_garrison_in(min0, bk));
		// Picking a civ after the start (game setup order) re-templates that player's own structures.
		uint32_t h = s.spawn_building_world(-30.0f, 0.0f, BLD_HOUSE, 0, 1.0f);
		CHECK(s.get_garrison_capacity(h) == 6); // Han: big house
		s.set_team_civ(0, CIV_BRITONS);
		CHECK(s.get_garrison_capacity(h) == 3 && s.get_garrison_capacity(bk) == 10);
	}

	// 16. UnitAI.Garrison: a unit the holder can't take (full, or not on its List) walks to it
	// instead; one already on its way when the holder fills arrives and stops there
	// (GARRISONING: Garrison fails, FinishOrder). The LoadingRange is 0.28's per template:
	// 1 m for a house, 6 m for a fortress, 4 m for a corral.
	{
		CHECK(std::fabs(structure_loading_range(BLD_HOUSE, CIV_ROMANS) - 0.28f) < 1e-4f &&
		      std::fabs(structure_loading_range(BLD_FORTRESS, CIV_ROMANS) - 6.0f * 0.28f) < 1e-4f &&
		      std::fabs(structure_loading_range(BLD_CORRAL, CIV_ROMANS) - 4.0f * 0.28f) < 1e-4f &&
		      std::fabs(structure_loading_range(BLD_STOREHOUSE, CIV_HAN) - 0.28f) < 1e-4f &&
		      std::fabs(structure_loading_range(BLD_BARRACKS, CIV_ROMANS) - 2.0f * 0.28f) < 1e-4f);
		fresh(s);
		s.set_team_civ(0, CIV_BRITONS);
		// The house's territory: a lone house decays to Gaia (TerritoryDecay) and ejects its units.
		s.spawn_building_world(-20.0f, -20.0f, BLD_TOWN_CENTER, 0, 1.0f);
		uint32_t h = s.spawn_building_world(0.0f, 0.0f, BLD_HOUSE, 0, 1.0f); // holds 3
		CHECK(s.get_garrison_capacity(h) == 3);
		uint32_t in[3];
		for (int k = 0; k < 3; ++k) in[k] = s.spawn_unit(-2.0f + 2.0f * k, 8.0f, 0, UNIT_VILLAGER);
		garrison(s, in, 3, h);
		CHECK(s.get_garrison_count(h) == 3);
		uint32_t late = s.spawn_unit(0.0f, 30.0f, 0, UNIT_VILLAGER); // house List: Support
		s.order_garrison(&late, 1, h);
		run(s, 12.0f);
		int32_t li = idx_of(s, late);
		const float near = std::hypot(s.units().pos_x[li], s.units().pos_z[li]);
		std::printf("ordered into a full house: walked from 30 to %.1f wu, garrisoned %d\n", near,
				s.get_garrison_count(h));
		CHECK(s.units().alive[li] && near < 10.0f && s.get_garrison_count(h) == 3);
		// Two walk to a house with one free seat: the first in takes it, the second stops beside it.
		s.cmd_unload(0, h, nullptr, 0);
		run(s, 0.5f);
		s.order_garrison(in, 2, h);
		run(s, 3.0f);
		CHECK(s.get_garrison_count(h) == 2);
		uint32_t pair[2] = {in[2], late};
		const int32_t a = idx_of(s, in[2]), c = idx_of(s, late);
		const_cast<float *>(s.units().pos_x)[a] = 0.0f; const_cast<float *>(s.units().pos_z)[a] = 20.0f;
		const_cast<float *>(s.units().pos_x)[c] = 0.0f; const_cast<float *>(s.units().pos_z)[c] = 40.0f;
		s.order_garrison(pair, 2, h);
		CHECK(s.units().action_state[c] == ACTION_MOVE); // a seat was free when it set off
		run(s, 15.0f);
		const float stop = std::hypot(s.units().pos_x[idx_of(s, late)], s.units().pos_z[idx_of(s, late)]);
		std::printf("house filled while walking: garrisoned %d, the other stopped %.1f wu from it\n",
				s.get_garrison_count(h), stop);
		CHECK(s.get_garrison_count(h) == 3 && s.units().alive[idx_of(s, late)] && stop < 10.0f);
		CHECK(s.units().action_state[idx_of(s, late)] == ACTION_IDLE);
		// A burning barracks (under EjectHealth): the unit walks up and stays outside.
		uint32_t bk = s.spawn_building_world(40.0f, 0.0f, BLD_BARRACKS, 0, 1.0f);
		Simulation::Building *bb = find_bld(s, bk);
		bb->hp = bb->max_hp * 0.05f;
		uint32_t spear = s.spawn_unit(40.0f, 30.0f, 0, UNIT_SPEARMAN);
		s.order_garrison(&spear, 1, bk);
		CHECK(s.units().action_state[idx_of(s, spear)] == ACTION_MOVE);
		run(s, 15.0f);
		const int32_t li2 = idx_of(s, spear);
		const float to_bk = std::hypot(s.units().pos_x[li2] - 40.0f, s.units().pos_z[li2]);
		std::printf("ordered into a burning barracks: %.1f wu from it, garrisoned %d\n", to_bk, s.get_garrison_count(bk));
		CHECK(s.get_garrison_count(bk) == 0 && s.units().alive[li2] && to_bk < 12.0f);
	}

	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
