// 0 A.D. Gaia content: treasures (Treasure.js / TreasureCollector.js, the CollectTreasure
// order), ruins (template_gaia_ruins: stone.ruins at 4x a rock's rate, MaxGatherers),
// capturable Gaia mercenary camps, Gaia-owned map units and structures, random-map
// placement and save/load.
//   g++ -std=c++20 -O2 -Isrc tools/test_treasures.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_treasures && /tmp/test_treasures
#define private public
#include "simulation/simulation.h"
#undef private
#include "network/turn_manager.h"
#include <cmath>
#include <vector>
#include <cstdio>
#include <cstring>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static void run(Simulation &s, float seconds) {
	for (int t = 0; t < (int)std::lround(seconds / Simulation::FIXED_STEP); ++t) s.tick(Simulation::FIXED_STEP);
}

static void fresh(Simulation &s) {
	s.set_sim_seed(5);
	s.reset();
	for (uint8_t p = 0; p < 2; ++p) s.player(p).active = 1, s.player(p).is_ai = 0; // no Petra field builders
	s.spawn_building_world(-50.0f, -50.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(50.0f, 50.0f, BLD_TOWN_CENTER, 1, 1.0f);
}

static int ruins_gathering(const Simulation &s, uint32_t rid) {
	int n = 0;
	for (size_t i = 0; i < s.units_.count; ++i)
		if (s.units_.alive[i] && s.units_.action_state[i] == ACTION_GATHER && s.units_.target_resource_id[i] == rid) ++n;
	return n;
}

int main() {
	// 1. The tables: lookups by template stem, and the plain treasure of each resource.
	{
		CHECK(TREASURE_TYPE_COUNT == 27);
		CHECK(treasure_type_by_name("food_barrel") == TREASURE_FOOD_BARREL);
		CHECK(treasure_type_by_name("nothing") == TREASURE_TYPE_COUNT);
		CHECK(treasure_type_for_resource(RES_WOOD) == TREASURE_WOOD);
		CHECK(treasure_type_for_resource(RES_GOLD) == TREASURE_METAL);
		CHECK(ruins_type_by_name("unfinished_greek_temple") < RUINS_TYPE_COUNT);
		CHECK(ruins_type_info(ruins_type_by_name("column_doric")).max_gatherers == 1);
		// 0.28's ruins and scenery: the Unfinished Greek Temple holds 1000 stone, the bronze
		// statues are metal, and new scenery (wicker fences) comes after the saved ids.
		CHECK(ruins_type_info(ruins_type_by_name("unfinished_greek_temple")).amount == 1000);
		CHECK(ruins_type_info(ruins_type_by_name("metal_statue_wolf")).res_type == RES_GOLD);
		CHECK(scenery_type_by_name("table_square") == 18);
		CHECK(scenery_type_by_name("fence_wicker_long") >= 19 && scenery_type_by_name("fence_wicker_long") < SCENERY_TYPE_COUNT);
		CHECK(scenery_type_info(scenery_type_by_name("fence_wicker_short")).wooden);
	}

	static Simulation s;

	// 2. A woman collects a food treasure: after CollectTime its food is the player's, the
	//    treasure is gone and she goes idle.
	{
		fresh(s);
		const auto &info = treasure_type_info(TREASURE_FOOD_BARREL);
		uint32_t w = s.spawn_unit(0.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
		uint32_t t = s.spawn_treasure(8.0f, 0.0f, TREASURE_FOOD_BARREL);
		CHECK(t != 0 && s.is_treasure_id(t));
		const float food0 = s.player(0).res[RES_FOOD];
		s.order_collect_treasure(&w, 1, t);
		CHECK(s.units_.order_kind[s.lookup_unit(w)] == ORDER_COLLECT_TREASURE);
		run(s, 6.0f + info.collect_time);
		std::printf("collect: food %.0f -> %.0f (barrel %d), treasures left %zu\n", food0, s.player(0).res[RES_FOOD],
		            info.amount[0], s.treasures().size());
		CHECK(s.player(0).res[RES_FOOD] == food0 + (float)info.amount[0]);
		CHECK(!s.is_treasure_id(t));
		const int32_t wi = s.lookup_unit(w);
		CHECK(s.units_.action_state[wi] == ACTION_IDLE && s.units_.order_kind[wi] == ORDER_NONE);
	}

	// 2b. Treasure.js Reward: the resources are added and the collected counter goes up, but
	//     they don't count as gathered. FindNearbyTreasure goes on only to a treasure the
	//     player has seen (CheckTargetVisible), not to one in unexplored ground.
	{
		fresh(s);
		uint32_t w = s.spawn_unit(0.0f, 0.0f, 0, UNIT_FEMALE_CITIZEN);
		uint32_t t = s.spawn_treasure(2.0f, 0.0f, TREASURE_FOOD_BARREL);
		uint32_t hidden = s.spawn_treasure(-15.0f, 0.0f, TREASURE_FOOD_BARREL);
		run(s, 0.5f);
		CHECK(s.fow_state_at(-15.0f, 0.0f, 0) == Simulation::FOW_UNEXPLORED);
		const uint32_t gathered0 = s.statistics(0).resources_gathered[RES_FOOD];
		const uint32_t found0 = s.statistics(0).treasures_collected;
		s.order_collect_treasure(&w, 1, t);
		run(s, 4.0f);
		CHECK(!s.is_treasure_id(t));
		CHECK(s.statistics(0).treasures_collected == found0 + 1);
		CHECK(s.statistics(0).resources_gathered[RES_FOOD] == gathered0);
		CHECK(s.is_treasure_id(hidden) && s.units_.order_kind[s.lookup_unit(w)] == ORDER_NONE);
		// Once a scout has seen it, the next collection goes on to it.
		uint32_t scout = s.spawn_unit(-15.0f, 3.0f, 0, UNIT_SPEARMAN);
		run(s, 0.5f);
		uint32_t t2 = s.spawn_treasure(-3.0f, 0.0f, TREASURE_FOOD_BARREL);
		(void)scout;
		s.order_collect_treasure(&w, 1, t2);
		run(s, 4.0f);
		CHECK(!s.is_treasure_id(t2));
		CHECK(s.units_.order_kind[s.lookup_unit(w)] == ORDER_COLLECT_TREASURE &&
		      s.units_.order_target[s.lookup_unit(w)] == hidden);
	}

	// 3. Two players race for one treasure: whoever gets there first takes it, and the other's
	//    order ends. A war dog has no TreasureCollector.
	{
		fresh(s);
		uint32_t near_u = s.spawn_unit(2.0f, 0.0f, 0, UNIT_SPEARMAN);
		uint32_t far_u = s.spawn_unit(-14.0f, 0.0f, 1, UNIT_SPEARMAN);
		uint32_t dog = s.spawn_unit(0.0f, 4.0f, 0, UNIT_WAR_DOG);
		uint32_t t = s.spawn_treasure(6.0f, 0.0f, TREASURE_METAL);
		const float m0 = s.player(0).res[RES_GOLD], m1 = s.player(1).res[RES_GOLD];
		s.set_diplomacy(0, 1, Simulation::DIPLO_NEUTRAL);
		s.set_diplomacy(1, 0, Simulation::DIPLO_NEUTRAL);
		s.order_collect_treasure(&near_u, 1, t);
		s.order_collect_treasure(&far_u, 1, t);
		s.order_collect_treasure(&dog, 1, t);
		CHECK(s.units_.order_kind[s.lookup_unit(dog)] != ORDER_COLLECT_TREASURE);
		run(s, 12.0f);
		CHECK(!s.is_treasure_id(t));
		std::printf("race: metal p0 %.0f -> %.0f, p1 %.0f -> %.0f\n", m0, s.player(0).res[RES_GOLD], m1, s.player(1).res[RES_GOLD]);
		CHECK(s.player(0).res[RES_GOLD] == m0 + (float)treasure_type_info(TREASURE_METAL).amount[3]);
		CHECK(s.units_.order_kind[s.lookup_unit(far_u)] == ORDER_NONE);
	}

	// 3b. TreasureCollector.js: each collector keeps its own timer, so when the first one leaves the
	//     second finishes on its own clock instead of starting over.
	{
		fresh(s);
		uint32_t a = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN);
		uint32_t b = s.spawn_unit(0.0f, 1.0f, 0, UNIT_SPEARMAN);
		uint32_t t = s.spawn_treasure(1.0f, 0.5f, TREASURE_STONE);
		s.order_collect_treasure(&a, 1, t);
		run(s, 0.3f);
		s.order_collect_treasure(&b, 1, t);
		run(s, 0.4f);
		s.order_walk(&a, 1, -10.0f, 0.0f);
		run(s, 0.75f); // b has collected for ~1.15 s, but only ~0.75 s since a stopped
		std::printf("collectors: b took the treasure on its own timer: %s\n", s.is_treasure_id(t) ? "no" : "yes");
		CHECK(!s.is_treasure_id(t));
	}

	// 3c. A treasure's Obstruction/Static blocks foundations on it (a 20 x 10 m shipwreck too).
	{
		fresh(s);
		s.set_explore_map(true); // placement in unexplored land is refused before obstruction
		auto place = [&](float x, float z) {
			const int foot = building_type_info(BLD_HOUSE).footprint;
			return s.check_placement(0, (int)std::floor(x / Simulation::GRID) - foot / 2, (int)std::floor(z / Simulation::GRID) - foot / 2, BLD_HOUSE);
		};
		CHECK(place(-38.0f, -50.0f) != Simulation::PLACE_OBSTRUCTED);
		s.spawn_treasure(-38.0f, -50.0f, TREASURE_FOOD_BARREL);
		CHECK(place(-38.0f, -50.0f) == Simulation::PLACE_OBSTRUCTED);
		CHECK(place(-38.0f, -44.0f) != Simulation::PLACE_OBSTRUCTED);
		s.spawn_treasure(-38.0f, -42.0f, TREASURE_SHIPWRECK);
		CHECK(place(-38.0f, -44.0f) == Simulation::PLACE_OBSTRUCTED);
		CHECK(treasure_type_info(TREASURE_SHIPWRECK_RAM_BOW).width == 21.0f && treasure_type_info(TREASURE_METAL).width == 1.5f);
	}

	// 4. A queued collect runs after the walk before it (Shift-click).
	{
		fresh(s);
		uint32_t u = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN);
		uint32_t t = s.spawn_treasure(0.0f, 10.0f, TREASURE_WOOD);
		const float w0 = s.player(0).res[RES_WOOD];
		s.order_walk(&u, 1, 10.0f, 0.0f, false);
		s.order_collect_treasure(&u, 1, t, true);
		run(s, 1.0f);
		CHECK(s.is_treasure_id(t)); // still walking
		run(s, 15.0f);
		CHECK(!s.is_treasure_id(t) && s.player(0).res[RES_WOOD] > w0);
	}

	// 5. Ruins: stone.ruins at 2 against a rock's 0.5, and one gatherer at a time (MaxGatherers 1);
	//    the second worker finds no other ruin (UnitAI only switches to the same type.specific) and stops.
	{
		fresh(s);
		uint32_t a = s.spawn_unit(0.0f, 0.0f, 0, UNIT_VILLAGER);
		uint32_t b = s.spawn_unit(0.0f, 2.0f, 0, UNIT_VILLAGER);
		uint32_t ruin = s.spawn_ruins(4.0f, 0.0f, ruins_type_by_name("column_doric"));
		uint32_t rock = s.spawn_resource_world(4.0f, 6.0f, RES_STONE, 1000);
		CHECK(ruin != 0 && s.resource_max_gatherers(ruin) == 1);
		uint32_t both[2] = {a, b};
		s.order_gather(both, 2, ruin);
		run(s, 4.0f);
		CHECK(ruins_gathering(s, ruin) <= 1);
		// One works the ruin; the other doesn't take the rock beside it.
		const uint32_t ta = s.units_.target_resource_id[s.lookup_unit(a)], tb = s.units_.target_resource_id[s.lookup_unit(b)];
		CHECK((ta == ruin && tb == 0) || (ta == 0 && tb == ruin));
		CHECK(rock != 0);
		// Rate: one soldier on the ruin against one on a rock, a carry's worth each.
		fresh(s);
		uint32_t r1 = s.spawn_ruins(0.0f, 0.0f, ruins_type_by_name("stone_structure_tower"));
		uint32_t r2 = s.spawn_resource_world(20.0f, 0.0f, RES_STONE, 1000);
		uint32_t g1 = s.spawn_unit(1.5f, 0.0f, 0, UNIT_VILLAGER);
		uint32_t g2 = s.spawn_unit(21.5f, 0.0f, 0, UNIT_VILLAGER);
		s.order_gather(&g1, 1, r1);
		s.order_gather(&g2, 1, r2);
		// Time each takes to fill its 10-stone carry (template_unit_infantry Capacities) once at the supply.
		float full1 = 0.0f, full2 = 0.0f, t = 0.0f;
		auto at_supply = [&](uint32_t id, uint32_t res) {
			const int32_t k = s.lookup_unit(id);
			return s.units_.action_state[k] == ACTION_GATHER && s.units_.target_resource_id[k] == res;
		};
		float start1 = -1.0f, start2 = -1.0f;
		for (; t < 60.0f && (full1 == 0.0f || full2 == 0.0f); t += Simulation::FIXED_STEP) {
			s.tick(Simulation::FIXED_STEP);
			const int32_t k1 = s.lookup_unit(g1), k2 = s.lookup_unit(g2);
			if (start1 < 0.0f && at_supply(g1, r1) && s.units_.attack_timer[k1] > 0.0f) start1 = t;
			if (start2 < 0.0f && at_supply(g2, r2) && s.units_.attack_timer[k2] > 0.0f) start2 = t;
			if (full1 == 0.0f && s.units_.carry[k1] >= 10) full1 = t - start1;
			if (full2 == 0.0f && s.units_.carry[k2] >= 10) full2 = t - start2;
		}
		std::printf("ruins: a full carry in %.1f s from ruins, %.1f s from a rock\n", full1, full2);
		CHECK(full1 > 0.0f && full2 >= 3.0f * full1);
	}

	// 6. A Gaia mercenary camp: owned by Gaia, captured by a player's soldiers.
	{
		fresh(s);
		uint32_t camp = s.spawn_building_world(0.0f, 0.0f, BLD_MERCENARY_CAMP, GAIA_PLAYER, 1.0f);
		CHECK(camp != 0);
		const Simulation::Building *b = nullptr;
		for (const auto &x : s.buildings()) if (x.id == camp) b = &x;
		CHECK(b && b->team == GAIA_PLAYER);
		uint32_t army[10];
		for (int k = 0; k < 10; ++k) army[k] = s.spawn_unit(-10.0f + (float)(k % 5) * 1.2f, -10.0f - (float)(k / 5) * 1.2f, 0, UNIT_SPEARMAN);
		s.order_attack_building(army, 10, camp, false, true);
		run(s, 90.0f);
		b = nullptr;
		for (const auto &x : s.buildings()) if (x.id == camp) b = &x;
		std::printf("mercenary camp: owner %d after 90 s of capture\n", b ? (int)b->team : -1);
		CHECK(b && b->team == 0);
	}

	// 6b. Gaia-owned units and structures from a 0 A.D. map (Player 0): the roster template by
	//     name (an _e rank falls back to the _b template, then promotes to Elite), a garrison
	//     placed inside a Gaia tower, and Gaia's soldiers fighting a player's.
	{
		fresh(s);
		uint32_t tower = s.spawn_building_world(0.0f, 0.0f, BLD_SENTRY_TOWER, GAIA_PLAYER, 1.0f);
		uint32_t jav = s.spawn_gaia_unit(0.0f, 0.0f, UNIT_ARCHER, "gaul/infantry_javelineer_e", 3);
		uint32_t pirate = s.spawn_gaia_unit(20.0f, 0.0f, UNIT_SPEARMAN, "pirates/no_such_template", 1);
		CHECK(jav != 0 && pirate != 0);
		const uint16_t jt = s.unit_template(jav);
		CHECK(jt != CIV_UNIT_TEMPLATE_NONE && std::strcmp(civ_unit_template(jt).id, "gaul/infantry_javelineer_b") == 0);
		CHECK(s.garrison_unit_now(jav, tower));
		CHECK(s.get_garrison_count(tower) == 1);
		CHECK(!s.garrison_unit_now(s.spawn_unit(-3.0f, 0.0f, 0, UNIT_SPEARMAN), tower)); // not Gaia's
		run(s, 0.2f);
		const int32_t ji = s.lookup_unit(jav);
		CHECK(ji >= 0 && s.units_.rank[ji] == 3);
		uint32_t mine = s.spawn_unit(24.0f, 0.0f, 0, UNIT_SWORDSMAN);
		const int32_t pi = s.lookup_unit(pirate);
		const float hp0 = s.units_.hp[pi];
		run(s, 5.0f);
		const int32_t pi2 = s.lookup_unit(pirate), mi = s.lookup_unit(mine);
		const bool fought = pi2 < 0 || !s.units_.alive[pi2] || s.units_.hp[pi2] < hp0 || mi < 0 || !s.units_.alive[mi] ||
				s.units_.hp[mi] < unit_type_info(UNIT_SWORDSMAN).hp;
		std::printf("gaia units: javelineer rank %d in the tower, fight %s\n", (int)s.units_.rank[ji], fought ? "yes" : "no");
		CHECK(fought);
	}

	// 6c. Gaia scenery (structures/fence_long, a 1.5 x 13 m obstruction): it blocks the navcells
	//    along its length, turned by its orientation, and foundations on top; saved and loaded.
	{
		fresh(s);
		s.set_explore_map(true); // placement in unexplored land is refused before obstruction
		const uint8_t fence = scenery_type_by_name("fence_long");
		CHECK(fence < SCENERY_TYPE_COUNT && scenery_type_info(fence).depth == 13.0f);
		CHECK(scenery_type_by_name("nothing") == SCENERY_TYPE_COUNT);
		const uint32_t f0 = s.spawn_scenery(0.0f, 0.0f, fence, 0.0f, 0.25f);       // along z
		const uint32_t f1 = s.spawn_scenery(20.0f, 0.0f, fence, 1.5707963f, 0.25f); // along x
		CHECK(f0 != 0 && f1 != 0 && s.scenery().size() == 2);
		auto blocked = [&](float x, float z) {
			auto [cx, cy] = s.nav_grid_.world_to_cell(x, z);
			return s.nav_grid_.cells.at(cx, cy) == NAV_OBSTRUCTED;
		};
		CHECK(blocked(0.0f, 1.4f) && blocked(0.0f, -1.4f) && !blocked(0.0f, 4.0f));
		CHECK(blocked(21.4f, 0.0f) && blocked(18.6f, 0.0f) && !blocked(20.0f, 4.0f));
		CHECK(s.check_placement(0, -1, -1, BLD_HOUSE) == Simulation::PLACE_OBSTRUCTED);
		const char *path = "/tmp/rts0_test_scenery.sav";
		CHECK(s.save_state(path));
		static Simulation l;
		CHECK(l.load_state(path));
		CHECK(l.scenery().size() == 2 && l.scenery()[1].angle == s.scenery()[1].angle);
		CHECK(TurnManager().boot_sim_hash(s) == TurnManager().boot_sim_hash(l));
		std::remove(path);
		std::printf("scenery: 2 fences block the nav grid and a house foundation\n");
	}

	// 6c2. Scenery is a template_structure with Health: soldiers told to attack it (Ctrl; a
	//     plain order captures Capturable scenery, test_unit_orders 62) walk up and strike it;
	//     it falls, its owner-less Loot goes to the killer's player and its navcells open. An
	//     archer shoots it down too.
	{
		fresh(s);
		const uint8_t fence = scenery_type_by_name("fence_long");
		const uint32_t f0 = s.spawn_scenery(0.0f, 0.0f, fence, 0.0f, 0.25f);
		const uint32_t sw = s.spawn_unit(6.0f, 0.0f, 0, UNIT_SWORDSMAN);
		const float wood = s.players_[0].res[RES_WOOD];
		s.order_attack_scenery(&sw, 1, f0, false);
		CHECK(s.units_.target_unit_id[s.lookup_unit(sw)] == f0);
		for (int t = 0; t < 1200 && s.is_scenery_id(f0); ++t) run(s, Simulation::FIXED_STEP);
		CHECK(!s.is_scenery_id(f0));
		CHECK(s.players_[0].res[RES_WOOD] - wood == (float)scenery_type_info(fence).loot[1]);
		auto [cx, cy] = s.nav_grid_.world_to_cell(0.0f, 1.4f);
		CHECK(s.nav_grid_.cells.at(cx, cy) != NAV_OBSTRUCTED);
		CHECK(s.units_.target_unit_id[s.lookup_unit(sw)] == 0);

		const uint32_t bench = s.spawn_scenery(-10.0f, 10.0f, scenery_type_by_name("bench"), 0.0f, 0.25f);
		const uint32_t ar = s.spawn_unit(-10.0f, 3.0f, 0, UNIT_ARCHER);
		s.order_attack_scenery(&ar, 1, bench, false);
		float closest = 1e9f;
		for (int t = 0; t < 2400 && s.is_scenery_id(bench); ++t) {
			const int32_t a = s.lookup_unit(ar);
			closest = std::min(closest, std::hypot(s.units_.pos_x[a] + 10.0f, s.units_.pos_z[a] - 10.0f));
			run(s, Simulation::FIXED_STEP);
		}
		CHECK(!s.is_scenery_id(bench));
		CHECK(closest > 2.0f); // from bow range
		std::printf("scenery: a swordsman fells a fence (+%d wood), an archer a bench from %.1f\n",
		            scenery_type_info(fence).loot[1], closest);
	}

	// 6d. A Gaia field (a map's structures/*/field owned by Player 0): anyone may farm it.
	{
		fresh(s);
		const uint32_t field = s.spawn_building_world(0.0f, 0.0f, BLD_FIELD, GAIA_PLAYER, 1.0f);
		s.spawn_building_world(0.0f, 8.0f, BLD_FARMSTEAD, 0, 1.0f);
		run(s, 0.5f);
		const uint32_t grain = s.field_resource_id(field);
		CHECK(grain != 0);
		uint32_t w = s.spawn_unit(3.0f, 3.0f, 0, UNIT_FEMALE_CITIZEN);
		const float food0 = s.player(0).res[RES_FOOD];
		s.order_gather(&w, 1, grain);
		run(s, 40.0f);
		const int32_t wi = s.lookup_unit(w);
		const float got = s.player(0).res[RES_FOOD] - food0 + (wi >= 0 ? (float)s.units_.carry[wi] : 0.0f);
		std::printf("gaia field: %.0f food farmed in 40 s\n", got);
		CHECK(got > 0.0f);
	}

	// 7. Random maps place treasures and ruins where 0 A.D.'s scripts do.
	{
		s.generate_random_map(MAP_FORTRESS, 3, 2, 96.0f);
		const size_t fortress = s.treasures().size();
		s.generate_random_map(MAP_DEEP_FOREST, 3, 2, 96.0f);
		int ruins = 0;
		for (size_t r = 0; r < s.resources_.count; ++r) ruins += s.resources_.subtype[r] == RES_SUB_RUINS;
		s.generate_random_map(MAP_RANDOM_CONTINENTAL, 3, 2, 96.0f);
		const size_t mainland = s.treasures().size();
		std::printf("maps: fortress %zu treasures, deep forest %d ruins, continental %zu treasures\n", fortress, ruins, mainland);
		CHECK(fortress >= 20 && ruins == 1 && mainland == 0);
	}

	// 7b. The island and desert maps that call placePlayerBaseTreasures / shipwrecks in 0.28.
	{
		auto count = [&](MapType map, int &wrecks, int &wet_wrecks) {
			s.generate_random_map(map, 3, 2, 96.0f);
			wrecks = wet_wrecks = 0;
			for (const auto &t : s.treasures())
				if (t.type >= TREASURE_SHIPWRECK && t.type <= TREASURE_SHIPWRECK_SAIL_BOAT_CUT) {
					++wrecks;
					wet_wrecks += s.get_terrain_height(t.x, t.z) < s.water_height();
				}
			return (int)s.treasures().size();
		};
		int w = 0, ww = 0;
		const int cyc = count(MAP_CYCLADIC_ARCHIPELAGO, w, ww);
		const int cyc_w = w, cyc_ww = ww;
		const int arctic = count(MAP_ARCTIC_SUMMER, w, ww);
		const int oases = count(MAP_SAHARAN_OASES, w, ww);
		const int islands = count(MAP_ISLANDS, w, ww);
		std::printf("maps: cycladic %d (%d wrecks, %d in water), arctic %d, oases %d, islands %d\n",
			cyc, cyc_w, cyc_ww, arctic, oases, islands);
		CHECK(cyc_w > 0 && cyc_ww == cyc_w);
		CHECK(arctic > 0 && oases > 0 && islands > 0);
	}

	// 7c. COLLECTTREASURE.APPROACHING MovementUpdate with likelyFailure: a collector whose path ends
	// on the shore, short of a treasure across deep water, gives the order up instead of pushing on.
	{
		static Simulation s;
		s.set_sim_seed(7);
		s.reset();
		s.set_map_half(64.0f);
		const int res = 65;
		std::vector<float> h((size_t)res * res);
		for (int j = 0; j < res; ++j)
			for (int k = 0; k < res; ++k) h[(size_t)j * res + k] = std::fabs(-64.0f + 128.0f * j / (res - 1)) < 16.0f ? -3.0f : 3.0f;
		s.set_terrain_data(h, res, 0.0f);
		s.begin_bulk_spawn();
		s.end_bulk_spawn();
		for (uint8_t p = 0; p < 2; ++p) s.set_player_active(p, true, false);
		s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
		uint32_t u = s.spawn_unit(0.0f, -22.0f, 0, UNIT_SPEARMAN);
		const uint32_t t = s.spawn_treasure(0.0f, 24.0f, TREASURE_WOOD);
		s.order_collect_treasure(&u, 1, t);
		CHECK(s.units_.order_kind[s.lookup_unit(u)] == ORDER_COLLECT_TREASURE);
		run(s, 15.0f);
		const int32_t ui = s.lookup_unit(u);
		std::printf("unreachable treasure: collector at (%.1f, %.1f), order %d, state %d\n", s.units_.pos_x[ui], s.units_.pos_z[ui],
		            s.units_.order_kind[ui], s.units_.action_state[ui]);
		CHECK(s.units_.order_kind[ui] == ORDER_NONE && s.units_.action_state[ui] == ACTION_IDLE && s.units_.pos_z[ui] < -10.0f);
		CHECK(s.treasures().size() == 1);
	}

	// 8. Save/load keeps treasures, ruins caps and the lockstep hash; the Treasures toggle clears them.
	{
		// A fresh sim: one reused after generate_random_map loads back different terrain.
		static Simulation s;
		fresh(s);
		uint32_t u = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SPEARMAN);
		uint32_t t = s.spawn_treasure(10.0f, 0.0f, TREASURE_STONE);
		s.spawn_treasure(-10.0f, 4.0f, TREASURE_FOOD_BIN);
		uint32_t ruin = s.spawn_ruins(0.0f, 8.0f, ruins_type_by_name("pyramid_minor"));
		s.order_collect_treasure(&u, 1, t);
		run(s, 1.0f);
		const char *path = "/tmp/rts0_test_treasures.sav";
		CHECK(s.save_state(path));
		static Simulation l;
		CHECK(l.load_state(path));
		CHECK(l.treasures().size() == 2 && l.resource_max_gatherers(ruin) == 90);
		// Each ruin blocks its own Obstruction/Static: the minor pyramid 34x34 m, not template_gaia_ruins' 7x7.
		{
			nav::Footprint fp;
			CHECK(l.mine_footprint(l.res_id_to_idx_.at(ruin), fp) && std::fabs(fp.hu - 34.0f * 0.125f) < 1e-4f && std::fabs(fp.hv - fp.hu) < 1e-4f);
		}
		CHECK(TurnManager().boot_sim_hash(s) == TurnManager().boot_sim_hash(l));
		run(s, 8.0f);
		run(l, 8.0f);
		CHECK(TurnManager().boot_sim_hash(s) == TurnManager().boot_sim_hash(l));
		CHECK(s.treasures().size() == 1 && l.treasures().size() == 1);
		std::remove(path);
		{
			nav::Footprint fp;
			const uint32_t wolf = s.spawn_ruins(-20.0f, -20.0f, ruins_type_by_name("metal_statue_wolf"));
			CHECK(s.mine_footprint(s.res_id_to_idx_.at(wolf), fp) && std::fabs(fp.hu * fp.hv - 14.0f * 8.0f * 0.125f * 0.125f) < 1e-4f);
		}
		s.clear_treasures();
		CHECK(s.treasures().empty());
		CHECK(s.spawn_resource_treasure(0.0f, 0.0f, RES_WOOD) != 0 && s.treasures()[0].type == TREASURE_WOOD);
	}

	// 9. TreasureCollector range is edge to edge (ObstructionManager IsInTargetRange): a spearman
	//    3.5 from a shipwreck's centre (20x10 m) reaches it along its length, not across it, and
	//    not a barrel at the same distance.
	{
		fresh(s);
		const uint32_t wreck = s.spawn_treasure(0.0f, 0.0f, TREASURE_SHIPWRECK);
		const uint32_t barrel = s.spawn_treasure(30.0f, 0.0f, TREASURE_FOOD_BARREL);
		const uint32_t sp = s.spawn_unit(10.0f, 10.0f, 0, UNIT_SPEARMAN);
		const size_t i = (size_t)s.lookup_unit(sp);
		const float reach = s.treasure_reach(i);
		const Simulation::Treasure *tw = nullptr, *tb = nullptr;
		for (const auto &t : s.treasures()) {
			if (t.id == wreck) tw = &t;
			if (t.id == barrel) tb = &t;
		}
		CHECK(tw && tb);
		float ux, uz;
		bool swapped;
		nav::footprint_axis(Simulation::treasure_angle(wreck), ux, uz, swapped);
		// The 20 m side lies along u unless the quarter turn swapped it onto v = (-uz, ux).
		const float lx = swapped ? -uz : ux, lz = swapped ? ux : uz, wx = -lz, wz = lx;
		auto at = [&](float x, float z) { s.units_.pos_x[i] = x; s.units_.pos_z[i] = z; };
		at(3.5f * lx, 3.5f * lz);
		CHECK(s.treasure_within(i, *tw, reach));
		at(3.5f * wx, 3.5f * wz);
		CHECK(!s.treasure_within(i, *tw, reach));
		at(30.0f + 3.5f, 0.0f);
		CHECK(!s.treasure_within(i, *tb, reach));
		at(30.0f + 1.5f, 0.0f);
		CHECK(s.treasure_within(i, *tb, reach));
	}

	if (failures) {
		std::printf("test_treasures: %d failure(s)\n", failures);
		return 1;
	}
	std::printf("test_treasures: all passed\n");
	return 0;
}
