// Per-player economy tests: 8 independent stock/tech/phase slots, tribute between
// players, per-owner research, teams, save/load v6 round trip and the lockstep hash.
//   g++ -std=c++20 -O2 -Isrc tools/test_players.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_players && /tmp/test_players
#include "simulation/simulation.h"
#include "network/turn_manager.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

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
		CHECK(s.player(p).phase == 1 && s.player(p).techs.empty());
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

	// Teammates are allies and never hostile, whatever the stances say.
	CHECK(s.is_ally(2, 3) && !s.is_hostile(2, 3) && !s.is_hostile(3, 2));
	s.set_diplomacy(2, 3, Simulation::DIPLO_ENEMY);
	CHECK(!s.is_hostile(2, 3));
	CHECK(s.is_hostile(2, 4) && s.is_hostile(4, 2));
	CHECK(!s.is_hostile(5, 5) && s.is_ally(5, 5));

	// Each player owns its own row: allying one way never moves the other player's stance.
	fresh(s);
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) s.set_player_team(p, p); // back to free-for-all
	s.apply_team_diplomacy();
	CHECK(s.cmd_set_diplomacy(3, 6, Simulation::DIPLO_ALLY));
	CHECK(s.get_diplomacy(3, 6) == Simulation::DIPLO_ALLY);
	CHECK(s.get_diplomacy(6, 3) == Simulation::DIPLO_ENEMY);
	CHECK(s.get_diplomacy(0, 6) == Simulation::DIPLO_ENEMY);
	// A one-sided alliance holds fire one way only (0 A.D. parity).
	CHECK(!s.is_hostile(3, 6) && s.is_hostile(6, 3));
	CHECK(!s.is_ally(3, 6));
	CHECK(s.cmd_set_diplomacy(6, 3, Simulation::DIPLO_ALLY));
	CHECK(s.is_ally(3, 6) && s.is_ally(6, 3));
	CHECK(!s.is_hostile(3, 6) && !s.is_hostile(6, 3));
	// Diplomacy.js OnDiplomacyChanged: a worse stance is mirrored, a better one is not.
	CHECK(s.cmd_set_diplomacy(3, 6, Simulation::DIPLO_NEUTRAL));
	CHECK(s.get_diplomacy(6, 3) == Simulation::DIPLO_NEUTRAL && !s.is_ally(3, 6));
	CHECK(s.cmd_set_diplomacy(6, 3, Simulation::DIPLO_ENEMY));
	CHECK(s.get_diplomacy(3, 6) == Simulation::DIPLO_ENEMY && s.is_hostile(3, 6) && s.is_hostile(6, 3));
	CHECK(s.get_diplomacy(0, 6) == Simulation::DIPLO_ENEMY && s.get_diplomacy(6, 0) == Simulation::DIPLO_ENEMY);
	// Rejected: self, out of range, unknown stance.
	CHECK(!s.cmd_set_diplomacy(2, 2, Simulation::DIPLO_ALLY));
	CHECK(!s.cmd_set_diplomacy(2, 8, Simulation::DIPLO_ALLY));
	CHECK(!s.cmd_set_diplomacy(8, 2, Simulation::DIPLO_ALLY));
	CHECK(!s.cmd_set_diplomacy(2, 3, 7));
	CHECK(s.get_diplomacy(2, 3) == Simulation::DIPLO_ENEMY);
	// Teams rewrite every stance when game setup assigns them.
	s.set_player_team(4, 1);
	s.set_player_team(5, 1);
	s.apply_team_diplomacy();
	CHECK(s.get_diplomacy(4, 5) == Simulation::DIPLO_ALLY && s.get_diplomacy(5, 4) == Simulation::DIPLO_ALLY);
	CHECK(s.get_diplomacy(3, 6) == Simulation::DIPLO_ENEMY); // the alliance above is gone
	CHECK(s.get_diplomacy(0, 4) == Simulation::DIPLO_ENEMY);

	// The game-setup starting-resources preset reaches every slot, not just player 0.
	fresh(s);
	s.set_starting_resources(300, 400, 300, 200);
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		CHECK(s.player(p).food() == 300.0f && s.player(p).wood() == 400.0f);
		CHECK(s.player(p).gold() == 300.0f && s.player(p).stone() == 200.0f);
	}

	// Research completes for the building's owner only.
	fresh(s);
	s.set_player_resources(0, 5000, 5000, 5000, 5000);
	uint32_t tc0 = s.spawn_building_world(-30.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
	for (int h = 0; h < 5; ++h) s.spawn_building_world(-20.0f + 6.0f * h, 10.0f, BLD_HOUSE, 0, 1.0f); // Town Phase needs 5 village structures
	CHECK(s.cmd_research(0, tc0, TECH_PHASE_TOWN));
	for (int t = 0; t < 20 * 400 && s.player_phase() < 2; ++t) s.tick(Simulation::FIXED_STEP);
	CHECK(s.player_phase() == 2);
	CHECK(s.is_tech_researched(TECH_PHASE_TOWN));
	for (uint8_t p = 1; p < MAX_PLAYERS; ++p) CHECK(!s.player(p).has_tech(TECH_PHASE_TOWN));

	// Commands run as their issuing player and only touch what that player owns.
	fresh(s);
	s.set_player_resources(0, 5000, 5000, 5000, 5000);
	s.set_player_resources(1, 5000, 5000, 5000, 5000);
	uint32_t b0 = s.spawn_building_world(-30.0f, 0.0f, BLD_BARRACKS, 0, 1.0f);
	uint32_t b1 = s.spawn_building_world(30.0f, 0.0f, BLD_BARRACKS, 1, 1.0f);
	// Archers are Village phase for every civ (several civs' swordsmen need Town phase).
	CHECK(!s.cmd_train(1, b0, UNIT_ARCHER));   // player 1 can't use player 0's barracks
	CHECK(s.food() == 5000);
	CHECK(s.cmd_train(1, b1, UNIT_ARCHER));
	auto stock = [&](uint8_t p) { int t = 0; for (int r = 0; r < RES_COUNT; ++r) t += s.player_resource(p, r); return t; };
	CHECK(stock(0) == 20000 && stock(1) < 20000);
	CHECK(!s.cmd_cancel_train(0, b1, 0));          // nor cancel player 1's queue
	int p1_stock = stock(1);
	CHECK(s.cmd_cancel_train(1, b1, 0));
	CHECK(stock(1) > p1_stock && stock(0) == 20000); // refund goes to the owner
	CHECK(!s.cmd_research(1, b0, TECH_WEAPONS_1));
	s.spawn_building_world(48.0f, 48.0f, BLD_TOWN_CENTER, 1, 1.0f); // houses need own territory
	uint32_t built = s.cmd_build(1, 20, 20, BLD_HOUSE);
	CHECK(built != 0 && s.owns_building(1, built) && !s.owns_building(0, built));
	CHECK(s.food() == 5000);                       // player 1 paid
	uint32_t u0 = s.spawn_unit(-20.0f, 5.0f, 0, UNIT_VILLAGER);
	uint32_t u1 = s.spawn_unit(20.0f, 5.0f, 1, UNIT_VILLAGER);
	uint32_t mixed[] = {u0, u1, 999999u};
	std::vector<uint32_t> owned;
	s.filter_owned_units(1, mixed, 3, owned);
	CHECK(owned.size() == 1 && owned[0] == u1);
	CHECK(s.pop_current(1) > 0 && s.pop_max(1) >= 10);
	TurnManager tmc;
	CHECK(tmc.player_for_client(0) == 0 && tmc.player_for_client(1) == 1 && tmc.player_for_client(50) == 0xFF);
	tmc.set_client_player(50, 3);
	CHECK(tmc.player_for_client(50) == 3);
	// NMT_GAME_START seating: client k takes the k-th human slot, the rest observe.
	GameSetupParams seats;
	seats.slot_count = 4;
	seats.slot_types[0] = 'a'; seats.slot_types[1] = 'h'; seats.slot_types[2] = 'a'; seats.slot_types[3] = 'h';
	tmc.assign_slot_players(seats);
	CHECK(tmc.player_for_client(0) == 1 && tmc.player_for_client(1) == 3 && tmc.player_for_client(2) == 0xFF);
	// 0 A.D. game setup observers take no slot: client 1 watches, client 2 plays the second human slot.
	seats.observers = 1u << 1;
	GameSetupParams wire;
	CHECK(parse_game_start_full(format_game_start_full(seats), wire) && wire.observers == seats.observers);
	tmc.assign_slot_players(wire);
	CHECK(tmc.player_for_client(0) == 1 && tmc.player_for_client(1) == 0xFF && tmc.player_for_client(2) == 3);
	wire.observers = 1; // the host watches
	tmc.assign_slot_players(wire);
	CHECK(tmc.player_for_client(0) == 0xFF && tmc.player_for_client(1) == 1 && tmc.player_for_client(2) == 3);
	seats.observers = 0;
	CHECK(format_game_start_full(seats).find("obs=") == std::string::npos); // old peers see no new key
	// 0 A.D. gamesetup player assignment: the host puts client 2 on the second human
	// slot (3) and client 1 on the first (1); client 0 is left to the free slots.
	CHECK(format_game_start_full(seats).find("seats=") == std::string::npos);
	seats.slot_count = 5;
	seats.slot_types[4] = 'h';
	seats.seats[2] = 3;
	seats.seats[1] = 1;
	CHECK(parse_game_start_full(format_game_start_full(seats), wire));
	CHECK(wire.seats[0] == 0xFF && wire.seats[1] == 1 && wire.seats[2] == 3 && wire.seats[3] == 0xFF);
	tmc.assign_slot_players(wire);
	CHECK(tmc.player_for_client(1) == 1 && tmc.player_for_client(2) == 3 && tmc.player_for_client(0) == 4);
	CHECK(tmc.player_for_client(3) == 0xFF);
	// A seat on a slot that is not human, or that another client has, falls back
	// to the free slots; an observer bit wins over a seat.
	wire.seats[0] = 3; wire.seats[3] = 2; wire.observers = 1u << 1;
	tmc.assign_slot_players(wire);
	CHECK(tmc.player_for_client(0) == 3 && tmc.player_for_client(1) == 0xFF && tmc.player_for_client(2) == 1);
	CHECK(tmc.player_for_client(3) == 4);
	{
		uint8_t out[8];
		TurnManager::seat_clients(wire, out); // the host's lobby shows the same seating Start makes
		for (uint32_t c = 0; c < 8; ++c) CHECK(out[c] == tmc.player_for_client(c));
	}
	{
		// Only connected clients take slots: with clients 0, 1 and 9 here, client
		// 9 gets the slot an absent id 2 would have taken (0.28 seats by GUID).
		GameSetupParams far;
		far.slot_count = 3;
		far.slot_types[0] = 'h'; far.slot_types[1] = 'h'; far.slot_types[2] = 'h';
		uint8_t out[NET_SETUP_CLIENTS];
		TurnManager::seat_clients(far, out, (1ull << 0) | (1ull << 1) | (1ull << 9));
		CHECK(out[0] == 0 && out[1] == 1 && out[9] == 2 && out[2] == 0xFF);
		// The host names every seat, so the clients' Start seats client 9 the same.
		for (uint32_t c = 0; c < NET_SETUP_CLIENTS; ++c) if (out[c] < 8) far.seats[c] = out[c];
		far.observers = 1ull << 33;
		GameSetupParams back;
		CHECK(parse_game_start_full(format_game_start_full(far), back) && back.seats[9] == 2 && back.observers == far.observers);
		tmc.assign_slot_players(back);
		CHECK(tmc.player_for_client(9) == 2 && tmc.player_for_client(1) == 1 && tmc.player_for_client(33) == 0xFF);
	}
	for (uint8_t &st : seats.seats) st = 0xFF;
	seats.slot_count = 4;
	tmc.reset();
	CHECK(tmc.player_for_client(1) == 1); // reset drops the seating

	// Save/load v9 round trip keeps every slot and every stance, and the lockstep
	// hash covers them all.
	fresh(s);
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		s.set_player_resources(p, 11 * p, 22 * p, 33 * p, 44 * p);
		s.set_team_civ(p, (uint8_t)(p + 3));
		s.set_player_team(p, p / 2);
		s.set_player_active(p, true, p != 0);
	}
	s.apply_team_diplomacy();
	s.set_diplomacy(2, 7, Simulation::DIPLO_NEUTRAL); // a mid-match stance, not a team's
	s.player(6).techs.set(TECH_PHASE_TOWN);
	s.player(6).techs.set(200); // ids past 64 survive the round trip too
	s.player(6).phase = 3;
	const char *path = "/tmp/rts0_test_players.sav";
	CHECK(s.save_state(path));
	static Simulation l;
	fresh(l);
	CHECK(l.load_state(path));
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		const PlayerState &a = s.player(p), &b = l.player(p);
		for (int r = 0; r < RES_COUNT; ++r) CHECK(a.res[r] == b.res[r]);
		CHECK(a.techs == b.techs && a.phase == b.phase && a.civ == b.civ);
		CHECK(a.team == b.team && a.active == b.active && a.is_ai == b.is_ai);
	}
	for (uint8_t x = 0; x < MAX_PLAYERS; ++x)
		for (uint8_t y = 0; y < MAX_PLAYERS; ++y)
			CHECK(s.get_diplomacy(x, y) == l.get_diplomacy(x, y));
	CHECK(l.get_diplomacy(2, 7) == Simulation::DIPLO_NEUTRAL);
	TurnManager tm;
	CHECK(tm.compute_sim_hash(s) == tm.compute_sim_hash(l));
	l.player(7).res[RES_STONE] += 1.0f;
	CHECK(tm.compute_sim_hash(s) != tm.compute_sim_hash(l));
	l.player(7).res[RES_STONE] -= 1.0f;
	l.set_player_team(7, 0);
	CHECK(tm.compute_sim_hash(s) != tm.compute_sim_hash(l));
	l.set_player_team(7, s.player_team(7));
	CHECK(tm.compute_sim_hash(s) == tm.compute_sim_hash(l));
	l.set_diplomacy(5, 1, Simulation::DIPLO_ALLY); // a row other than player 0's
	CHECK(tm.compute_sim_hash(s) != tm.compute_sim_hash(l));

	// 0 A.D. has no passive Town Center income: an idle player 0 earns nothing.
	fresh(s);
	s.spawn_building(20, 20, BLD_TOWN_CENTER, 0);
	const float food0 = s.player(0).food(), wood0 = s.player(0).wood();
	for (int i = 0; i < 600; ++i) s.tick(0.1f);
	CHECK(!s.is_game_over());
	CHECK(s.player(0).food() == food0 && s.player(0).wood() == wood0);

	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
