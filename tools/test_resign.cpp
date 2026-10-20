// Headless checks for 0 A.D.'s "resign" command (Commands.js: cmpPlayer.Defeat),
// lockstep command 49 run through TurnManager as a single-player game runs it.
// Build: g++ -std=c++20 -O2 -Isrc tools/test_resign.cpp src/simulation/simulation.cpp
//        src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp
//        src/network/net_host.cpp -lpthread -o test_resign
#include "simulation/simulation.h"
#include "network/turn_manager.h"
#include "network/net_types.h"
#include <cstdio>

using namespace rtso;

static int failures = 0;
#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                    \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			failures++;                                                   \
		}                                                                 \
	} while (0)

static Simulation *fresh() {
	auto *sim = new Simulation();
	sim->set_victory_condition(VICTORY_CONQUEST);
	sim->spawn_initial();
	return sim;
}

static void turns(TurnManager &tm, Simulation &sim, int n) {
	for (int i = 0; i < n; ++i) tm.tick(TurnManager::TURN_DURATION, sim);
}

static void post_resign(TurnManager &tm) {
	NetCommand c;
	c.client_id = 0; // the local player's client: player 0
	c.cmd_type = (uint8_t)NetCommandType::NET_CMD_RESIGN;
	tm.post_local_command(c);
}

static int owned_units(const Simulation &sim, uint8_t team) {
	auto uv = sim.units();
	int n = 0;
	for (size_t i = 0; i < uv.count; ++i)
		if (uv.alive[i] && uv.team[i] == team) ++n;
	return n;
}

static int owned_buildings(const Simulation &sim, uint8_t team) {
	int n = 0;
	for (const auto &b : sim.buildings())
		if (b.hp > 0.0f && b.team == team) ++n;
	return n;
}

int main() {
	CHECK((uint8_t)NetCommandType::NET_CMD_RESIGN == 49);

	// 1. With an ally still playing, resigning puts the player out: its units and
	// structures turn Gaia, it watches with the map revealed, and the match goes on.
	{
		Simulation *sim = fresh();
		sim->set_player_team(2, sim->player(0).team);
		sim->spawn_unit(-40.0f, 40.0f, 2, UNIT_SWORDSMAN);
		TurnManager tm;
		turns(tm, *sim, 3);
		const int units0 = owned_units(*sim, 0), blds0 = owned_buildings(*sim, 0);
		const int gaia_units = owned_units(*sim, GAIA_PLAYER), gaia_blds = owned_buildings(*sim, GAIA_PLAYER);
		CHECK(units0 > 0 && blds0 > 0 && !sim->is_defeated(0));
		post_resign(tm);
		turns(tm, *sim, 3);
		CHECK(sim->is_defeated(0) && !sim->is_defeated(2) && !sim->is_defeated(1));
		CHECK(!sim->is_game_over() && !sim->is_victory());
		CHECK(owned_units(*sim, 0) == 0 && owned_buildings(*sim, 0) == 0);
		CHECK(owned_units(*sim, GAIA_PLAYER) == gaia_units + units0);
		CHECK(owned_buildings(*sim, GAIA_PLAYER) == gaia_blds + blds0);
		// The centre cell: a circular map keeps its off-world corners unseen even when revealed.
		CHECK(sim->fow_grid(0)[(size_t)(sim->fow_res() / 2) * (size_t)sim->fow_res() + (size_t)(sim->fow_res() / 2)] == Simulation::FOW_VISIBLE);
		// A second resign from the resigned player changes nothing.
		post_resign(tm);
		turns(tm, *sim, 1);
		CHECK(sim->is_defeated(0) && !sim->is_game_over());
		delete sim;
	}

	// 2. One against one: resigning hands the match to the other player.
	{
		Simulation *sim = fresh();
		TurnManager tm;
		turns(tm, *sim, 3);
		post_resign(tm);
		turns(tm, *sim, 5);
		CHECK(sim->is_defeated(0));
		CHECK(sim->is_game_over() && sim->is_victory_for(1) && !sim->is_victory_for(0));
		delete sim;
	}

	// 3. The sim call refuses a player that is already out, and bad ids.
	{
		Simulation *sim = fresh();
		sim->set_player_team(2, sim->player(0).team);
		sim->spawn_unit(-40.0f, 40.0f, 2, UNIT_SWORDSMAN);
		CHECK(sim->cmd_resign(0));
		CHECK(!sim->cmd_resign(0));
		CHECK(!sim->cmd_resign(MAX_PLAYERS));
		delete sim;
	}

	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
