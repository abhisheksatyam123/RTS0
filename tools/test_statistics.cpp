// Per-player match statistics (0 A.D. StatisticsTracker.js, statistics_tracker.h).
//
//   1. A kill credits the killer's enemy_units_killed and the victim's
//      units_lost in the unit's classes, with its resource value.
//   2. Tribute and barter fill tributes_sent/received and resources_sold/bought.
//   3. A four-player Petra match fills every seated player's counters (trained,
//      gathered, used, constructed, explored, controlled) and samples them every
//      30 s, like StatisticsTracker.updateSequences.
//   4. Save/load keeps every counter and sample, and the loaded copy keeps
//      counting the same way.
//   5. Resource and population counts (GetResourceCounts, GetPopulationCount),
//      livestock bred at a corral (unitsTrained.Domestic, not in the total) and
//      successful and failed bribes (VisionSharing.AddSpy, Commands.js).
//   7. Gathered resources count as they are gathered (ResourceGatherer PerformGather), by
//      the supply's type then: a load never dropped off still counts, and only fruit
//      and grain count as vegetarian food.
//   6. Deleting a unit or a structure counts as neither lost nor killed: only
//      Attacking.js TargetKilled reports deaths, Health.Kill does not.
//
//   g++ -std=c++20 -O2 -Isrc tools/test_statistics.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp -lpthread -o /tmp/test_statistics
#define private public // test 5 sets resources and finds the corral's animals
#include "simulation/simulation.h"
#undef private
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static void run(Simulation &s, int ticks) {
	for (int t = 0; t < ticks; ++t) s.tick(Simulation::FIXED_STEP);
}

static void test_kill_credit() {
	std::printf("kill credit\n");
	Simulation s;
	s.reset();
	s.set_victory_condition(VICTORY_ENDLESS);
	s.set_player_active(0, true, false);
	s.set_player_active(1, true, false);
	s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(40.0f, 40.0f, BLD_TOWN_CENTER, 1, 1.0f);
	uint32_t sw = s.spawn_unit(0.0f, 0.0f, 0, UNIT_SWORDSMAN, 160.0f);
	uint32_t fem = s.spawn_unit(1.5f, 0.0f, 1, UNIT_FEMALE_CITIZEN, 25.0f);
	s.order_attack(&sw, 1, fem);
	run(s, 20 * 20);
	const PlayerStatistics a = s.statistics(0), b = s.statistics(1);
	std::printf("  killer total %u worker %u female %u value %u; victim lost %u value %u\n",
	            a.enemy_units_killed[STAT_UNIT_TOTAL], a.enemy_units_killed[STAT_UNIT_WORKER],
	            a.enemy_units_killed[STAT_UNIT_CIVILIAN], a.enemy_units_killed_value,
	            b.units_lost[STAT_UNIT_TOTAL], b.units_lost_value);
	CHECK(a.enemy_units_killed[STAT_UNIT_TOTAL] == 1);
	CHECK(a.enemy_units_killed[STAT_UNIT_CIVILIAN] == 1);
	CHECK(a.enemy_units_killed[STAT_UNIT_WORKER] == 1);
	CHECK(a.enemy_units_killed[STAT_UNIT_INFANTRY] == 0);
	CHECK(b.units_lost[STAT_UNIT_TOTAL] == 1);
	CHECK(b.units_lost[STAT_UNIT_CIVILIAN] == 1);
	CHECK(a.enemy_units_killed_value > 0 && a.enemy_units_killed_value == b.units_lost_value);
	CHECK(a.units_lost[STAT_UNIT_TOTAL] == 0);
	CHECK(b.enemy_units_killed[STAT_UNIT_TOTAL] == 0);

	// Classes follow 0 A.D.'s templates.
	CHECK(stat_unit_classes(UNIT_SPEARMAN, ROSTER_CITIZEN) == ((1u << STAT_UNIT_INFANTRY) | (1u << STAT_UNIT_WORKER)));
	// template_unit_cavalry.xml: Citizen Soldier Cavalry, no Worker.
	CHECK(stat_unit_classes(UNIT_CAVALRY, ROSTER_CITIZEN) == (1u << STAT_UNIT_CAVALRY));
	CHECK(stat_unit_classes(UNIT_SPEARMAN, ROSTER_CHAMPION) == ((1u << STAT_UNIT_INFANTRY) | (1u << STAT_UNIT_CHAMPION)));
	CHECK(stat_unit_classes(UNIT_MERCHANT_SHIP, 0) == ((1u << STAT_UNIT_SHIP) | (1u << STAT_UNIT_TRADER)));
	// template_unit_elephant_archer.xml, template_unit_support_elephant.xml: no counted class;
	// champion elephants are Champions.
	CHECK(stat_unit_classes(UNIT_ELEPHANT, ROSTER_RANGED | ROSTER_CITIZEN | ROSTER_NO_GATHER) == 0);
	CHECK(stat_unit_classes(UNIT_WORKER_ELEPHANT, 0) == 0);
	CHECK(stat_unit_classes(UNIT_ELEPHANT, ROSTER_CHAMPION) == (1u << STAT_UNIT_CHAMPION));
	CHECK(stat_building_classes(BLD_TOWN_CENTER) == (1u << STAT_BLD_CIV_CENTRE));
	CHECK(stat_building_classes(BLD_MILITARY_COLONY) == (1u << STAT_BLD_CIV_CENTRE));
	CHECK(stat_building_classes(BLD_WALL_SEGMENT) == 0);
	// 0.28: towers, temples and fields are in no column; docks are Economic and
	// Military, fortresses Fortress and Military.
	CHECK(stat_building_classes(BLD_TOWER) == 0 && stat_building_classes(BLD_SENTRY_TOWER) == 0);
	CHECK(stat_building_classes(BLD_TEMPLE) == 0 && stat_building_classes(BLD_FIELD) == 0);
	CHECK(stat_building_classes(BLD_DOCK) == ((1u << STAT_BLD_ECONOMIC) | (1u << STAT_BLD_MILITARY)));
	CHECK(stat_building_classes(BLD_FORTRESS) == ((1u << STAT_BLD_FORTRESS) | (1u << STAT_BLD_MILITARY)));
	CHECK(stat_building_classes(BLD_MERCENARY_CAMP) == (1u << STAT_BLD_MILITARY));
}

static void test_tribute_barter() {
	std::printf("tribute and barter\n");
	Simulation s;
	s.reset();
	s.set_victory_condition(VICTORY_ENDLESS);
	s.set_player_active(0, true, false);
	s.set_player_active(1, true, false);
	s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(40.0f, 40.0f, BLD_TOWN_CENTER, 1, 1.0f);
	s.spawn_building_world(-30.0f, -40.0f, BLD_MARKET, 0, 1.0f);
	CHECK(s.cmd_tribute(0, 1, RES_WOOD, 100));
	CHECK(s.cmd_barter(0, RES_FOOD, RES_GOLD, 100));
	const PlayerStatistics a = s.statistics(0), b = s.statistics(1);
	CHECK(a.tributes_sent == 100 && b.tributes_received == 100);
	CHECK(a.resources_sold[RES_FOOD] == 100);
	CHECK(a.resources_bought[RES_GOLD] > 0 && a.resources_bought[RES_GOLD] < 100);
	// Barter.js sells through TrySubtractResources, so the sale is also used (the summary
	// shows used minus sold); a tribute is not (SubtractResourcesOrNotify).
	CHECK(a.resources_used[RES_FOOD] == 100 && a.resources_used[RES_WOOD] == 0);
}

static void test_map_measures() {
	std::printf("map exploration and control\n");
	Simulation s;
	s.reset();
	s.set_victory_condition(VICTORY_ENDLESS);
	s.set_player_active(0, true, false);
	s.set_player_active(1, true, false);
	s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(40.0f, -40.0f, BLD_TOWN_CENTER, 1, 1.0f);
	run(s, 2 * 20);
	// Whole percentages, as GetTerritoryPercentage and GetPercentMapExplored return them.
	const float c0 = s.percent_map_controlled(0), c1 = s.percent_map_controlled(1);
	const float e0 = s.percent_map_explored(0);
	CHECK(c0 > 0.0f && c0 == std::floor(c0) && c1 > 0.0f);
	CHECK(e0 > 0.0f && e0 < 100.0f && e0 == std::floor(e0));
	// Territory cut off from its civic centre is not counted (only connected tiles are).
	auto tiles = [&](uint8_t owner) {
		size_t n = 0;
		for (uint8_t v : s.territory_grid().data) n += v == owner + 1;
		return n;
	};
	const size_t owned = tiles(0);
	s.spawn_building_world(40.0f, 45.0f, BLD_BARRACKS, 0, 1.0f);
	run(s, 2 * 20);
	CHECK(tiles(0) > owned);
	CHECK(s.percent_map_controlled(0) == c0);
	// The team columns add the team up only when teams are locked.
	const uint8_t teams[MAX_PLAYERS] = {1, 1};
	s.set_setup_teams(teams);
	CHECK(s.team_percent_map_controlled(0) == c0);
	CHECK(s.team_percent_map_explored(0) == s.percent_map_explored(0));
	s.set_lock_teams(true);
	CHECK(s.team_percent_map_controlled(0) == c0 + c1);
	CHECK(s.team_percent_map_explored(0) > s.percent_map_explored(0));
	// Revealed Map shows the map without exploring it; Explored Map explores all of it.
	const float seen = s.percent_map_explored(0);
	s.set_reveal_map(true);
	CHECK(s.percent_map_explored(0) == seen);
	s.set_explore_map(true);
	CHECK(s.percent_map_explored(0) == 100.0f);
}

static bool same(const PlayerStatistics &a, const PlayerStatistics &b) {
	return std::memcmp(&a, &b, sizeof(a)) == 0;
}

static void test_match() {
	std::printf("four-player Petra match\n");
	const uint32_t seed = 42;
	auto build = [&](Simulation &s) {
		s.reset();
		s.set_sim_seed(seed);
		s.set_waves_enabled(false);
		s.generate_random_map((MapType)5, seed, 4, 128.0f);
		s.set_victory_condition(VICTORY_ENDLESS);
	};
	Simulation s;
	build(s);
	const int minutes = 12;
	run(s, minutes * 60 * 20);
	const auto &samples = s.statistics_samples();
	std::printf("  %zu samples after %d min\n", samples.size(), minutes);
	CHECK(samples.size() == (size_t)(minutes * 2 + 1)); // the first turn's row, then every 30 s
	if (!samples.empty()) CHECK(samples.front().time < 0.5f && samples.front().players[0].units_trained[STAT_UNIT_TOTAL] == 0);
	if (!samples.empty()) CHECK(std::fabs(samples.back().time - minutes * 60.0f) < 0.5f);
	int seated = 0;
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		if (!s.player_in_match(p)) continue;
		++seated;
		const PlayerStatistics st = s.statistics(p);
		uint32_t gathered = 0;
		int32_t used = 0;
		for (int r = 0; r < RES_COUNT; ++r) { gathered += st.resources_gathered[r]; used += st.resources_used[r]; }
		std::printf("  p%u trained %u lost %u killed %u built %u gathered %u used %d explored %.1f%% controlled %.1f%% (peak %.1f%%)\n",
		            p, st.units_trained[STAT_UNIT_TOTAL], st.units_lost[STAT_UNIT_TOTAL], st.enemy_units_killed[STAT_UNIT_TOTAL],
		            st.buildings_constructed[STAT_BLD_TOTAL], gathered, used, s.percent_map_explored(p),
		            s.percent_map_controlled(p), st.peak_percent_map_controlled);
		if (!s.petra_runs_for(p)) continue; // nobody plays player 0 here
		CHECK(st.units_trained[STAT_UNIT_TOTAL] > 0);
		CHECK(st.units_trained[STAT_UNIT_WORKER] > 0);
		CHECK(gathered > 0);
		CHECK(used > 0);
		CHECK(st.buildings_constructed[STAT_BLD_TOTAL] > 0);
		CHECK(s.percent_map_explored(p) > 0.0f && s.percent_map_explored(p) <= 100.0f);
		CHECK(s.team_percent_map_explored(p) >= s.percent_map_explored(p));
		CHECK(s.percent_map_controlled(p) > 0.0f);
		CHECK(st.peak_percent_map_controlled >= s.percent_map_controlled(p));
	}
	CHECK(seated >= 2);
	// Every kill credited to someone is somebody's loss.
	uint32_t lost = 0, killed = 0;
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		lost += s.statistics(p).units_lost[STAT_UNIT_TOTAL];
		killed += s.statistics(p).enemy_units_killed[STAT_UNIT_TOTAL];
	}
	CHECK(killed <= lost);

	std::printf("save/load keeps the statistics\n");
	const char *path = "/tmp/rts0_test_statistics.rts0sav";
	CHECK(s.save_state(path));
	Simulation t;
	build(t);
	CHECK(t.load_state(path));
	CHECK(t.statistics_samples().size() == samples.size());
	for (size_t i = 0; i < samples.size() && i < t.statistics_samples().size(); ++i)
		CHECK(std::memcmp(&samples[i], &t.statistics_samples()[i], sizeof(StatisticsSample)) == 0);
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		CHECK(same(s.statistics(p), t.statistics(p)));
		CHECK(s.percent_map_explored(p) == t.percent_map_explored(p));
	}
	run(s, 2 * 60 * 20);
	run(t, 2 * 60 * 20);
	CHECK(t.statistics_samples().size() == s.statistics_samples().size());
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) CHECK(same(s.statistics(p), t.statistics(p)));
	std::remove(path);
}

static void test_gathered_each_tick() {
	std::printf("resources gathered on each gather tick\n");
	Simulation s;
	s.reset();
	s.set_victory_condition(VICTORY_ENDLESS);
	s.set_player_active(0, true, false);
	s.set_player_active(1, true, false);
	s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(40.0f, -40.0f, BLD_TOWN_CENTER, 1, 1.0f);
	// Far from any dropsite, so nothing is dropped off during the test.
	const uint32_t bush = s.spawn_resource_world(30.0f, 40.0f, RES_FOOD, 200, RES_SUB_NONE);
	const uint32_t tree = s.spawn_resource_world(-30.0f, 40.0f, RES_WOOD, 200);
	const uint32_t woman = s.spawn_unit(30.0f, 38.0f, 0, UNIT_FEMALE_CITIZEN);
	const uint32_t cutter = s.spawn_unit(-30.0f, 38.0f, 0, UNIT_FEMALE_CITIZEN);
	s.order_gather(&woman, 1, bush);
	s.order_gather(&cutter, 1, tree);
	run(s, 6 * 20);
	PlayerStatistics a = s.statistics(0);
	const int wi = s.lookup_unit(woman), ci = s.lookup_unit(cutter);
	CHECK(wi >= 0 && ci >= 0);
	const int carried_food = s.units_.carry[wi], carried_wood = s.units_.carry[ci];
	std::printf("  carried food %d wood %d; gathered food %u (vegetarian %u) wood %u; stockpile food %.0f\n", carried_food, carried_wood,
		a.resources_gathered[RES_FOOD], a.vegetarian_food_gathered, a.resources_gathered[RES_WOOD], s.players_[0].res[RES_FOOD]);
	CHECK(carried_food > 0 && a.resources_gathered[RES_FOOD] == (uint32_t)carried_food);
	CHECK(a.vegetarian_food_gathered == a.resources_gathered[RES_FOOD]);
	CHECK(carried_wood > 0 && a.resources_gathered[RES_WOOD] == (uint32_t)carried_wood);
	// The loads die with the gatherers and stay counted.
	const uint32_t ids[2] = {woman, cutter};
	s.cmd_delete(0, ids, 2);
	run(s, 20);
	PlayerStatistics b = s.statistics(0);
	CHECK(b.resources_gathered[RES_FOOD] == a.resources_gathered[RES_FOOD] && b.resources_gathered[RES_WOOD] == a.resources_gathered[RES_WOOD]);
	// Meat is food, but not vegetarian.
	const uint32_t carcass = s.spawn_resource_world(30.0f, 40.0f, RES_FOOD, 100, RES_SUB_MEAT);
	const uint32_t hunter = s.spawn_unit(30.0f, 38.0f, 0, UNIT_FEMALE_CITIZEN);
	s.order_gather(&hunter, 1, carcass);
	run(s, 6 * 20);
	PlayerStatistics c = s.statistics(0);
	CHECK(c.resources_gathered[RES_FOOD] > b.resources_gathered[RES_FOOD] && c.vegetarian_food_gathered == b.vegetarian_food_gathered);
}

static void test_counts() {
	std::printf("resource and population counts, livestock, bribes\n");
	Simulation s;
	s.generate_random_map(MAP_MAINLAND, 9, 2, 64.0f);
	s.set_victory_condition(VICTORY_ENDLESS);
	s.set_player_active(0, true, false);
	s.set_player_active(1, true, false);
	float ex = 0, ez = 0, ox = 0, oz = 0;
	for (const auto &b : s.buildings()) {
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) { ex = b.x; ez = b.z; }
		if (b.team == 0 && b.type == BLD_TOWN_CENTER) { ox = b.x; oz = b.z; }
	}
	s.players_[0].res[RES_FOOD] = 1234.7f;
	s.players_[0].res[RES_GOLD] = 1000.0f;
	PlayerStatistics a = s.statistics(0);
	CHECK(a.resources_count[RES_FOOD] == 1234 && a.resources_count[RES_GOLD] == 1000);
	CHECK(a.population_count > 0 && (int)a.population_count == s.pop_current(0));
	// Livestock: an animal from the corral counts as Domestic only.
	const uint32_t trained_before = a.units_trained[STAT_UNIT_TOTAL];
	// 20 tiles toward the centre: the base sits 0.35 of the map out (mainland.js), so +14, +14 is off the circle.
	const float ol = std::max(1.0f, std::hypot(ox, oz));
	const uint32_t corral = s.spawn_building_world(ox - 20.0f * ox / ol, oz - 20.0f * oz / ol, BLD_CORRAL, 0, 1.0f);
	CHECK(s.cmd_breed_livestock(0, corral, FAUNA_SHEEP_TRAINABLE, 2));
	run(s, 90 * 20);
	a = s.statistics(0);
	std::printf("  livestock %u, trained %u -> %u\n", a.livestock_bred, trained_before, a.units_trained[STAT_UNIT_TOTAL]);
	CHECK(a.livestock_bred == 2);
	CHECK(a.units_trained[STAT_UNIT_TOTAL] == trained_before);
	// Bribes: none to bribe fails, a trader succeeds.
	s.players_[0].res[RES_GOLD] = 1000.0f;
	s.complete_research(0, TECH_UNLOCK_SPIES);
	const int32_t gold_used_before = s.statistics(0).resources_used[RES_GOLD];
	CHECK(s.cmd_bribe(0, 1) == Simulation::BRIBE_NO_UNITS);
	s.spawn_unit(ex + 6.0f, ez + 6.0f, 1, UNIT_TRADER);
	CHECK(s.cmd_bribe(0, 1) == Simulation::BRIBE_OK);
	a = s.statistics(0);
	CHECK(a.successful_bribes == 1 && a.failed_bribes == 1);
	// Both bribes are paid through TrySubtractResources (Commands.js), so they count as used.
	const int32_t bribe_spent = (int32_t)std::lround(1000.0f - s.players_[0].res[RES_GOLD]);
	std::printf("  bribes spent %d, metal used %d -> %d\n", bribe_spent, gold_used_before, a.resources_used[RES_GOLD]);
	CHECK(bribe_spent > 0 && a.resources_used[RES_GOLD] - gold_used_before == bribe_spent);
	CHECK(s.statistics(1).successful_bribes == 0 && s.statistics(1).failed_bribes == 0);
	// The 30 s rows carry the counts at their time.
	run(s, 31 * 20);
	const StatisticsSample &row = s.statistics_samples().back();
	CHECK(row.players[0].successful_bribes == 1 && row.players[0].livestock_bred == 2);
	CHECK(row.players[0].population_count > 0 && row.players[0].resources_count[RES_GOLD] == (uint32_t)s.player_resource(0, RES_GOLD));
}

static void test_delete_not_lost() {
	std::printf("deleting is not losing\n");
	Simulation s;
	s.reset();
	s.set_victory_condition(VICTORY_ENDLESS);
	s.set_player_active(0, true, false);
	s.set_player_active(1, true, false);
	s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(40.0f, 40.0f, BLD_TOWN_CENTER, 1, 1.0f);
	const uint32_t ids[2] = {s.spawn_unit(-30.0f, -30.0f, 0, UNIT_FEMALE_CITIZEN, 25.0f),
	                         s.spawn_building_world(-28.0f, -40.0f, BLD_HOUSE, 0, 1.0f)};
	run(s, 20);
	s.cmd_delete(0, ids, 2);
	run(s, 40);
	const PlayerStatistics a = s.statistics(0);
	std::printf("  lost units %u value %u, structures %u value %u\n", a.units_lost[STAT_UNIT_TOTAL],
	            a.units_lost_value, a.buildings_lost[STAT_BLD_TOTAL], a.buildings_lost_value);
	CHECK(s.lookup_unit(ids[0]) < 0 || !s.units_.alive[s.lookup_unit(ids[0])]);
	CHECK(a.units_lost[STAT_UNIT_TOTAL] == 0 && a.units_lost_value == 0);
	CHECK(a.buildings_lost[STAT_BLD_TOTAL] == 0 && a.buildings_lost_value == 0);
}

// template_unit_hero_infantry / _cavalry add Infantry or Cavalry; elephant heroes have neither.
static void test_hero_classes() {
	std::printf("hero classes\n");
	const char *ids[3] = {"spart/hero_leonidas", "mace/hero_alexander_iii", "cart/hero_hannibal"};
	const uint8_t want[3] = {STAT_UNIT_INFANTRY, STAT_UNIT_CAVALRY, STAT_UNIT_TOTAL};
	for (int h = 0; h < 3; ++h) {
		Simulation s;
		s.reset();
		s.set_victory_condition(VICTORY_ENDLESS);
		s.set_player_active(0, true, false);
		s.set_player_active(1, true, false);
		s.spawn_building_world(-40.0f, -40.0f, BLD_TOWN_CENTER, 0, 1.0f);
		s.spawn_building_world(40.0f, 40.0f, BLD_TOWN_CENTER, 1, 1.0f);
		const uint16_t tmpl = civ_unit_template_by_id(ids[h]);
		CHECK(tmpl < CIV_UNIT_TEMPLATE_COUNT);
		uint32_t sw[4];
		for (int k = 0; k < 4; ++k) sw[k] = s.spawn_unit(-1.0f + k * 0.7f, 0.0f, 0, UNIT_SWORDSMAN, 160.0f);
		const uint32_t hero = s.spawn_unit(0.0f, 1.5f, 1, (UnitType)civ_unit_template(tmpl).unit_type, 1.0f, tmpl);
		s.order_attack(sw, 4, hero);
		run(s, 20 * 20);
		const PlayerStatistics a = s.statistics(0);
		std::printf("  %s: killed %u hero %u infantry %u cavalry %u\n", ids[h], a.enemy_units_killed[STAT_UNIT_TOTAL],
		            a.enemy_units_killed[STAT_UNIT_HERO], a.enemy_units_killed[STAT_UNIT_INFANTRY], a.enemy_units_killed[STAT_UNIT_CAVALRY]);
		CHECK(a.enemy_units_killed[STAT_UNIT_HERO] == 1);
		CHECK(a.enemy_units_killed[STAT_UNIT_INFANTRY] == (want[h] == STAT_UNIT_INFANTRY ? 1u : 0u));
		CHECK(a.enemy_units_killed[STAT_UNIT_CAVALRY] == (want[h] == STAT_UNIT_CAVALRY ? 1u : 0u));
	}
}

int main() {
	test_kill_credit();
	test_hero_classes();
	test_delete_not_lost();
	test_tribute_barter();
	test_counts();
	test_gathered_each_tick();
	test_map_measures();
	test_match();
	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
