// Multiplayer conquest victory over the lockstep net (headless: no SCons, no Godot).
//
// Three in-process peers (host A, client B, shadow C) run the same lockstep turns, the same
// way tools/test_netcode.cpp's --playtest does. Players 0 and 1 are human seats, each with
// its own client id. With --petra, players 2 and 3 are Petra seats too.
//
// Player 0's army damages Civic Centres one at a time (player 1's first, then each Petra
// Civic Centre still standing). Player 1's spearmen attack-move at player 0's base. Conquest
// ends the match once every enemy has lost its last critical structure (its Civic Centre)
// with nothing left to hold it.
//
// Checks, once the match ends:
//   1. all three peers reach game over on the same turn, with the same winners, defeated
//      mask and victory flags, and the same per-seat victory answer (what each human sees
//      on the summary screen);
//   2. the session notices match on all peers (DEFEATED / WON; the presentation draws the
//      game-over screen from these), and the winner's WON notice is there;
//   3. the state hashes stayed equal every turn, through the end and 20 turns past it.
//
// Build and run (the same flags as tools/playtest_lockstep.py):
//   g++ -std=c++20 -O1 -g -fno-exceptions -UNDEBUG -Isrc tools/test_mp_victory.cpp
//       src/simulation/simulation.cpp src/simulation/rms_generator.cpp src/core/job_system.cpp
//       src/network/turn_manager.cpp src/network/net_host.cpp -lpthread -o test_mp_victory
//   ./test_mp_victory [seed] [--petra] [max_turns]
// Exit 0 = the conquest ended the same on every peer. 1 = a divergence, a failed check, or
// no victory within max_turns. MPV_TRACE=1 prints the match every 500 turns.

#include "simulation/simulation.h"
#include "network/turn_manager.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace rtso;

namespace {

struct World {
	std::vector<uint32_t> team0_soldiers;
	std::vector<uint32_t> team1_soldiers;
	uint32_t team1_cc = 0;
	std::vector<uint32_t> petra_ccs; // Petra seats 2 and 3, when present
};

void build(Simulation &s, uint32_t seed, World &w, bool petra) {
	s.set_sim_seed(seed);
	s.reset();
	s.set_victory_condition(VICTORY_CONQUEST);
	s.set_starting_resources(0, 0, 0, 0);
	s.set_reveal_map(true); // the test is about the ending, not the scouting
	s.set_team_civ(0, CIV_ATHENIANS);
	s.set_team_civ(1, CIV_SPARTANS);

	// Player 0: a Civic Centre and 16 swordsmen, tough enough to live through the Civic
	// Centres' arrows long enough to raze them.
	s.spawn_building_world(-30.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
	for (int i = 0; i < 16; ++i)
		w.team0_soldiers.push_back(
			s.spawn_unit(-20.0f + (float)(i % 4) * 2.0f, -8.0f + (float)(i / 4) * 2.0f, 0, UNIT_SWORDSMAN, 4000.0f));

	// Player 1: a Civic Centre and three spearmen.
	w.team1_cc = s.spawn_building_world(24.0f, 0.0f, BLD_TOWN_CENTER, 1, 1.0f);
	for (int i = 0; i < 3; ++i)
		w.team1_soldiers.push_back(s.spawn_unit(20.0f, -2.0f + (float)i * 2.0f, 1, UNIT_SPEARMAN, 150.0f));

	if (petra) {
		s.set_team_civ(2, CIV_PERSIANS);
		s.set_team_civ(3, CIV_ATHENIANS);
		// Open ground west of player 1. A Civic Centre on unreachable terrain can't be
		// attacked: the army stands at its edge for the whole match.
		w.petra_ccs.push_back(s.spawn_building_world(-10.0f, 40.0f, BLD_TOWN_CENTER, 2, 1.0f));
		w.petra_ccs.push_back(s.spawn_building_world(-10.0f, -40.0f, BLD_TOWN_CENTER, 3, 1.0f));
		for (int i = 0; i < 3; ++i) {
			s.spawn_unit(-14.0f + (float)i * 2.0f, 36.0f, 2, UNIT_SPEARMAN, 150.0f);
			s.spawn_unit(-14.0f + (float)i * 2.0f, -36.0f, 3, UNIT_SPEARMAN, 150.0f);
		}
	}
}

bool alive_building(const Simulation &s, uint32_t id) {
	for (const auto &b : s.buildings())
		if (b.id == id && b.hp > 0.0f) return true;
	return false;
}

// The Civic Centre the army attacks next: player 1's, then the first Petra one still standing.
// Read from the sim, so every peer picks the same one.
uint32_t next_target(const Simulation &s, const World &w) {
	if (alive_building(s, w.team1_cc)) return w.team1_cc;
	for (uint32_t id : w.petra_ccs)
		if (alive_building(s, id)) return id;
	return 0;
}

// This turn's commands: a pure function of the world and the sim state, so both peers get
// the same batch from the same state, and the lockstep hash checks it.
void script_turn(const Simulation &view, const World &w, uint32_t turn, std::vector<NetCommand> &out) {
	out.clear();
	auto push = [&](uint32_t client, uint8_t type, uint32_t ent, uint32_t tgt, float tx, float tz) {
		NetCommand c;
		c.client_id = client;
		c.cmd_type = type;
		c.entity_ids.push_back(ent);
		c.target_id = tgt;
		c.target_x = tx;
		c.target_z = tz;
		// NET_PARAM_NO_CAPTURE: damage the Civic Centre instead of capturing it.
		c.param = (type == 15) ? NET_PARAM_NO_CAPTURE : 0;
		out.push_back(c);
	};
	// Player 0 (client 0): every swordsman attacks the next Civic Centre, re-ordered every 40
	// turns so an idle one picks it up again.
	const uint32_t target = next_target(view, w);
	if (target && (turn == 1 || turn % 40 == 0))
		for (uint32_t id : w.team0_soldiers) push(0, 15 /*ATTACK_BUILD*/, id, target, 0.0f, 0.0f);
	// Player 1 (client 1): spearmen attack-move at player 0's base.
	if (turn == 1 || turn % 40 == 0)
		for (uint32_t id : w.team1_soldiers) push(1, 10 /*ATTACK_MOVE*/, id, 0, -30.0f, 0.0f);
}

void trace(const Simulation &s, const World &w, uint32_t turn) {
	int t0 = 0, t1 = 0;
	for (size_t i = 0; i < s.units().count; ++i)
		if (s.units().alive[i]) (s.units().team[i] == 0 ? t0 : t1)++;
	std::printf("[trace] turn %u units: player0=%d others=%d defeated=0x%02X next_target=%u\n", turn, t0, t1,
		s.defeated_mask(), next_target(s, w));
}

} // namespace

int main(int argc, char **argv) {
	uint32_t seed = 7;
	bool petra = false;
	int max_turns = 40000;
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--petra") == 0) petra = true;
		else if (i == 1) seed = (uint32_t)std::strtoul(argv[i], nullptr, 10);
		else max_turns = std::atoi(argv[i]);
	}
	const bool trace_on = std::getenv("MPV_TRACE") != nullptr;

	std::printf("[mp-victory] seed=%u max_turns=%d conquest, 2 human seats%s, 3 peers\n", seed, max_turns,
		petra ? " + 2 Petra seats" : "");

	Simulation sa, sb, sc;
	TurnManager tma, tmb, tmc;
	World wa, wb, wc;
	build(sa, seed, wa, petra);
	build(sb, seed, wb, petra);
	build(sc, seed, wc, petra);

	std::vector<NetCommand> batch;
	int end_turn = 0;

	for (uint32_t turn = 1; turn <= (uint32_t)max_turns; ++turn) {
		// The orders come from peer A's state, which must equal B's and C's (checked below).
		script_turn(sa, wa, turn, batch);
		// Client 0 is the host (local on every peer's manager); client 1 arrives over the net.
		for (const auto &c : batch) {
			if (c.client_id == 0) {
				tma.post_local_command(c);
				tmb.post_local_command(c);
				tmc.post_local_command(c);
			} else {
				tma.post_remote_command(1, c);
				tmb.post_remote_command(1, c);
				tmc.post_remote_command(1, c);
			}
		}

		const bool oa = tma.tick(TurnManager::TURN_DURATION, sa);
		const bool ob = tmb.tick(TurnManager::TURN_DURATION, sb);
		const bool oc = tmc.tick(TurnManager::TURN_DURATION, sc);
		if (!oa || !ob || !oc) {
			std::printf("[mp-victory] turn %u did not advance (A=%d B=%d C=%d)\n", turn, oa, ob, oc);
			return 1;
		}

		const uint32_t ha = tma.compute_sim_hash(sa);
		const uint32_t hb = tmb.compute_sim_hash(sb);
		const uint32_t hc = tmc.compute_sim_hash(sc);
		if (ha != hb || ha != hc) {
			std::printf("[mp-victory] turn %u DIVERGENT A=0x%08X B=0x%08X C=0x%08X\n", turn, ha, hb, hc);
			return 1;
		}
		if (trace_on && turn % 500 == 0) trace(sa, wa, turn);

		if (sa.is_game_over() || sa.is_victory()) {
			end_turn = (int)turn;
			std::printf("[mp-victory] match over at turn %u (game_over A=%d B=%d C=%d)\n", turn,
				(int)sa.is_game_over(), (int)sb.is_game_over(), (int)sc.is_game_over());
			for (int extra = 0; extra < 20; ++extra) { // keep hashing past the end
				tma.tick(TurnManager::TURN_DURATION, sa);
				tmb.tick(TurnManager::TURN_DURATION, sb);
				tmc.tick(TurnManager::TURN_DURATION, sc);
				const uint32_t ea = tma.compute_sim_hash(sa);
				if (ea != tma.compute_sim_hash(sb) || ea != tma.compute_sim_hash(sc)) {
					std::printf("[mp-victory] DIVERGENT after the match ended (+%d turns)\n", extra + 1);
					return 1;
				}
			}
			break;
		}
	}

	if (!end_turn) {
		std::printf("[mp-victory] FAIL no victory in %d turns\n", max_turns);
		return 1;
	}

	int failures = 0;
	auto check = [&](bool ok, const char *what) {
		std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
		if (!ok) failures++;
	};

	check(sa.is_game_over() == sb.is_game_over() && sa.is_game_over() == sc.is_game_over(),
		"game_over agrees on all peers");
	check(sa.is_victory() == sb.is_victory() && sa.is_victory() == sc.is_victory(),
		"victory flag agrees on all peers");
	check(sa.winner_mask() == sb.winner_mask() && sa.winner_mask() == sc.winner_mask(),
		"winner mask agrees on all peers");
	check(sa.defeated_mask() == sb.defeated_mask() && sa.defeated_mask() == sc.defeated_mask(),
		"defeated mask agrees on all peers");
	check(sa.winner_mask() == 0x01, "player 0 is the winner");
	check((sa.defeated_mask() & 0x02) != 0, "player 1 is defeated");
	if (petra) check((sa.defeated_mask() & 0x0C) == 0x0C, "both Petra seats are defeated");
	for (uint8_t seat = 0; seat < 2; ++seat) {
		const bool va = sa.is_victory_for(seat), vb = sb.is_victory_for(seat), vc = sc.is_victory_for(seat);
		std::printf("       seat %u sees victory=%d on A, %d on B, %d on C\n", seat, va, vb, vc);
		check(va == vb && va == vc, "the seat's victory answer agrees on all peers");
	}
	check(sa.is_victory_for(0) && !sa.is_victory_for(1), "the winner's seat is the one that won");

	// The game-over screen is drawn from these session notices (session_lifecycle.cpp).
	const auto na = sa.take_session_notices();
	const auto nb = sb.take_session_notices();
	const auto nc = sc.take_session_notices();
	auto same = [](const std::vector<Simulation::SessionNotice> &x, const std::vector<Simulation::SessionNotice> &y) {
		if (x.size() != y.size()) return false;
		for (size_t i = 0; i < x.size(); ++i)
			if (x[i].kind != y[i].kind || x[i].player != y[i].player || x[i].other != y[i].other ||
			    x[i].tech != y[i].tech)
				return false;
		return true;
	};
	std::printf("       notices: A=%zu B=%zu C=%zu\n", na.size(), nb.size(), nc.size());
	for (const auto &n : na)
		std::printf("         kind=%u player=%u other=%u tech=%u\n", n.kind, n.player, n.other, n.tech);
	check(same(na, nb) && same(na, nc), "session notices agree on all peers");
	bool won = false;
	for (const auto &n : na)
		if (n.kind == Simulation::SessionNotice::WON) won = true;
	check(won, "a WON notice reached the game-over screen");

	std::printf("[mp-victory] %s: conquest ended at turn %d, %d check(s) failed\n",
		failures ? "FAIL" : "PASS", end_turn, failures);
	return failures ? 1 : 0;
}
