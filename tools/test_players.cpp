// Per-player economy tests: 8 independent stock/tech/phase slots, tribute between
// players, per-owner research, teams, save/load v6 round trip and the lockstep hash.
//   g++ -std=c++20 -O2 -Isrc tools/test_players.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_players && /tmp/test_players
#include "simulation/simulation.h"
#include "network/turn_manager.h"
#include <cstdio>
#include <cstdlib>

using namespace rtso;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static Simulation &fresh(Simulation &s) {
	s.set_sim_seed(99);
	s.reset();
	return s;
}

int main() {
	static Simulation s;
	fresh(s);

	// Reset: player 0 keeps the human stock, every other slot Petra's.
	CHECK(s.food() == 350 && s.wood() == 450 && s.gold() == 300 && s.stone() == 250);
	for (uint8_t p = 1; p < MAX_PLAYERS; ++p) {
		CHECK(s.player(p).food() == 400.0f && s.player(p).wood() == 600.0f);
		CHECK(s.player(p).phase == 1 && s.player(p).tech_mask == 0);
		CHECK(s.player_team(p) == p); // free-for-all by default
	}
	CHECK(s.ai_food() == s.player(Simulation::PETRA_PLAYER).food());

	// Slots are independent.
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) s.set_player_resources(p, 100 * (p + 1), 10, 20, 30);
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) CHECK(s.player_resource(p, RES_FOOD) == 100 * (p + 1));
	CHECK(s.food() == 100);

	// Civs for all 8 slots.
	s.set_team_civ(7, CIV_MAURYANS);
	CHECK(s.team_civ(7) == CIV_MAURYANS);
	CHECK(s.team_civ(200) == CIV_ATHENIANS);

	// Tribute from player 0 reaches any other player; bad ids and overdrafts are rejected.
	s.set_player_resources(0, 500, 0, 0, 0);
	CHECK(s.send_tribute(5, RES_FOOD, 200));
	CHECK(s.food() == 300 && s.player_resource(5, RES_FOOD) == 800);
	CHECK(!s.send_tribute(5, RES_FOOD, 1000));
	CHECK(!s.send_tribute(0, RES_FOOD, 10));
	CHECK(!s.send_tribute(8, RES_FOOD, 10));
	CHECK(s.food() == 300);

	// Teams.
	s.set_player_team(2, 1);
	s.set_player_team(3, 1);
	CHECK(s.same_team(2, 3) && !s.same_team(2, 4) && s.same_team(4, 4));

	// Research completes for the building's owner only.
	fresh(s);
	s.set_player_resources(0, 5000, 5000, 5000, 5000);
	uint32_t tc0 = s.spawn_building_world(-30.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
	CHECK(s.cmd_research(tc0, TECH_PHASE_TOWN));
	for (int t = 0; t < 20 * 400 && s.player_phase() < 2; ++t) s.tick(Simulation::FIXED_STEP);
	CHECK(s.player_phase() == 2);
	CHECK(s.is_tech_researched(TECH_PHASE_TOWN));
	for (uint8_t p = 1; p < MAX_PLAYERS; ++p) CHECK(!s.player(p).has_tech(TECH_PHASE_TOWN));

	// Save/load v6 round trip keeps every slot, and the lockstep hash covers every slot.
	fresh(s);
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		s.set_player_resources(p, 11 * p, 22 * p, 33 * p, 44 * p);
		s.set_team_civ(p, (uint8_t)(p + 3));
		s.set_player_team(p, p / 2);
		s.set_player_active(p, true, p != 0);
	}
	s.player(6).tech_mask = 0x5u;
	s.player(6).phase = 3;
	const char *path = "/tmp/rts0_test_players.sav";
	CHECK(s.save_state(path));
	static Simulation l;
	fresh(l);
	CHECK(l.load_state(path));
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		const PlayerState &a = s.player(p), &b = l.player(p);
		for (int r = 0; r < RES_COUNT; ++r) CHECK(a.res[r] == b.res[r]);
		CHECK(a.tech_mask == b.tech_mask && a.phase == b.phase && a.civ == b.civ);
		CHECK(a.team == b.team && a.active == b.active && a.is_ai == b.is_ai);
	}
	TurnManager tm;
	CHECK(tm.compute_sim_hash(s) == tm.compute_sim_hash(l));
	l.player(7).res[RES_STONE] += 1.0f;
	CHECK(tm.compute_sim_hash(s) != tm.compute_sim_hash(l));
	l.player(7).res[RES_STONE] -= 1.0f;
	l.set_player_team(7, 0);
	CHECK(tm.compute_sim_hash(s) != tm.compute_sim_hash(l));

	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
