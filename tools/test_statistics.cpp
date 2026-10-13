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
//
//   g++ -std=c++20 -O2 -Isrc tools/test_statistics.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp -lpthread -o /tmp/test_statistics
#include "simulation/simulation.h"
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
	CHECK(stat_unit_classes(UNIT_SPEARMAN, ROSTER_CHAMPION) == ((1u << STAT_UNIT_INFANTRY) | (1u << STAT_UNIT_CHAMPION)));
	CHECK(stat_unit_classes(UNIT_MERCHANT_SHIP, 0) == ((1u << STAT_UNIT_SHIP) | (1u << STAT_UNIT_TRADER)));
	CHECK(stat_building_class(BLD_TOWN_CENTER) == STAT_BLD_CIV_CENTRE);
	CHECK(stat_building_class(BLD_WALL_SEGMENT) == STAT_BLD_TOTAL);
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
	CHECK(samples.size() == (size_t)(minutes * 2));
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

int main() {
	test_kill_credit();
	test_tribute_barter();
	test_match();
	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
