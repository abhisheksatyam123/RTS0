// RTS0 save/load and replay soak: for every (map, seed) pair it plays a Petra match with
// scripted orders from two clients, and checks three things per turn:
//   1. Uninterrupted run: the reference per-turn sim hashes.
//   2. Save mid-match, load into a fresh Simulation built the way the game builds one, and
//      keep both copies playing the same order stream: the loaded copy must hash the same as
//      the reference right after the load and on every turn after it.
//   3. Replay: the reference run's replay (save_replay) validates, loads into a fresh
//      TurnManager + fresh Simulation, and reproduces every recorded per-turn hash with no
//      replay out-of-sync and a complete playback.
// Any mismatch prints a [desync] line naming the map, seed and turn. Exit code 0 = every match
// passed, 1 = at least one desync or failed check, 2 = bad usage.
//
//   g++ -std=c++20 -O2 -Isrc -Itools tools/soak_save_replay.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/soak_save_replay
//   /tmp/soak_save_replay --maps 15,14 --seeds 1,2,3 --turns 3000 --dir /tmp/soak_sr
//
// --turns counts lockstep turns (0.2 s of game time each); --save-turn sets the save point
// (default: a third of the way in). --dir holds the replay and save files and is removed per match.
#define private public
#include "simulation/simulation.h"
#undef private
#include "network/turn_manager.h"
#include "network/net_types.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace rtso;

namespace {

struct Options {
	std::vector<int> maps = {15, 14};
	std::vector<uint32_t> seeds = {1, 2, 3};
	uint32_t turns = 3000;
	uint32_t save_turn = 0; // 0 = a third of the way in
	int players = 4;
	float size = 256.0f;
	std::vector<int> civs = {0, 1, 2, 3};
	std::string dir = "/tmp/soak_save_replay";
};

std::vector<int> parse_list(const char *s) {
	std::vector<int> out;
	for (const char *p = s; *p;) {
		out.push_back(std::atoi(p));
		while (*p && *p != ',') ++p;
		if (*p == ',') ++p;
	}
	return out;
}

// Same map, Petra and team setup the game gives a skirmish (soak_match.cpp's setup, minus the
// JSON maps). Every copy of a match is built through this, so the comparisons are fair.
void build_match(Simulation &s, const Options &o, int map, uint32_t seed) {
	s.reset();
	s.set_sim_seed(seed);
	s.set_waves_enabled(false);
	for (int p = 0; p < o.players; ++p) s.set_team_civ((uint8_t)p, (uint8_t)o.civs[p % o.civs.size()]);
	s.generate_random_map((MapType)map, seed, o.players, o.size * 0.5f);
	s.set_victory_condition(VICTORY_CONQUEST);
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) s.set_player_active(p, p < o.players, p < o.players);
	for (int p = 0; p < o.players; ++p) {
		s.set_petra_difficulty((uint8_t)p, 3);
		s.set_petra_behavior((uint8_t)p, 2);
	}
}

// Orders for one turn. Client 0 moves its own units, client 1 attack-moves its own; the
// choice depends only on the sim state, so identical copies script identical orders.
struct TurnOrders {
	std::vector<NetCommand> local, remote;
};

struct Rng {
	uint32_t state;
	explicit Rng(uint32_t seed) : state(seed ? seed : 1u) {}
	uint32_t next() { state = state * 1664525u + 1013904223u; return state; }
	float unit() { return (float)(next() & 0xFFFFFF) / 16777216.0f; }
};

void collect_alive(const Simulation &s, uint8_t team, std::vector<uint32_t> &ids) {
	ids.clear();
	for (size_t i = 0; i < s.units_.count; ++i)
		if (s.units_.alive[i] && s.units_.team[i] == team) ids.push_back(s.units_.id[i]);
}

TurnOrders script_turn(const Simulation &s, uint32_t turn, Rng &rng) {
	TurnOrders o;
	std::vector<uint32_t> mine, theirs;
	if (turn % 4 == 1) {
		collect_alive(s, 0, mine);
		for (int k = 0; k < 3 && !mine.empty(); ++k) {
			NetCommand c;
			c.client_id = 0;
			c.cmd_type = (uint8_t)NetCommandType::NET_CMD_MOVE;
			c.entity_ids.push_back(mine[rng.next() % mine.size()]);
			c.target_x = -60.0f + rng.unit() * 120.0f;
			c.target_z = -60.0f + rng.unit() * 120.0f;
			o.local.push_back(c);
		}
	}
	if (turn % 4 == 3) {
		collect_alive(s, 1, theirs);
		for (int k = 0; k < 2 && !theirs.empty(); ++k) {
			NetCommand c;
			c.client_id = 1;
			c.cmd_type = (uint8_t)NetCommandType::NET_CMD_ATTACK_MOVE;
			c.entity_ids.push_back(theirs[rng.next() % theirs.size()]);
			c.target_x = -60.0f + rng.unit() * 120.0f;
			c.target_z = -60.0f + rng.unit() * 120.0f;
			o.remote.push_back(c);
		}
	}
	return o;
}

// One lockstep turn through the live submit/advance API, then its hash.
uint32_t run_turn(TurnManager &tm, Simulation &s, const TurnOrders &o) {
	for (const NetCommand &c : o.local) tm.post_local_command(c);
	if (!o.remote.empty()) tm.post_remote_commands(1, o.remote);
	tm.tick(TurnManager::TURN_DURATION, s);
	return tm.compute_sim_hash(s);
}

struct Result {
	int failures = 0;
};

void report(Result &r, const char *what, int map, uint32_t seed, uint32_t turn) {
	std::printf("[desync] map=%d seed=%u turn=%u %s\n", map, seed, turn, what);
	++r.failures;
}

bool soak_one(const Options &o, int map, uint32_t seed, Result &r) {
	const uint32_t n = o.turns;
	const uint32_t k = o.save_turn ? o.save_turn : n / 3;
	const std::string save_path = o.dir + "/match.rts0sav";
	const std::string replay_path = o.dir + "/match.rts0rec";
	std::vector<TurnOrders> script(n);
	std::vector<uint32_t> ref_hash(n);
	const int failures_before = r.failures;

	// 1. Uninterrupted reference run; it also decides the order stream.
	Simulation ref;
	build_match(ref, o, map, seed);
	TurnManager tm_ref;
	Rng rng(seed * 7919u + 17u);
	for (uint32_t t = 0; t < n; ++t) {
		script[t] = script_turn(ref, t + 1, rng);
		ref_hash[t] = run_turn(tm_ref, ref, script[t]);
	}
	if (!tm_ref.save_replay(replay_path)) {
		report(r, "save_replay failed", map, seed, n);
		return false;
	}
	const uint32_t final_ref_hash = ref_hash[n - 1];

	// 2. Save at turn k, load into a fresh copy, then play both on from the same orders.
	Simulation live;
	build_match(live, o, map, seed);
	TurnManager tm_live;
	for (uint32_t t = 0; t < k; ++t) {
		const uint32_t h = run_turn(tm_live, live, script[t]);
		if (h != ref_hash[t]) { report(r, "live copy diverged before the save", map, seed, t + 1); return false; }
	}
	if (!live.save_state(save_path.c_str())) {
		report(r, "save_state failed", map, seed, k);
		return false;
	}
	Simulation loaded;
	build_match(loaded, o, map, seed);
	if (!loaded.load_state(save_path.c_str())) {
		report(r, "load_state failed", map, seed, k);
		return false;
	}
	TurnManager tm_loaded;
	if (tm_loaded.compute_sim_hash(loaded) != ref_hash[k - 1]) {
		report(r, "loaded copy hashes differently right after load", map, seed, k);
	} else {
		for (uint32_t t = k; t < n; ++t) {
			const uint32_t hl = run_turn(tm_loaded, loaded, script[t]);
			const uint32_t hs = run_turn(tm_live, live, script[t]);
			if (hl != ref_hash[t]) { report(r, "loaded copy diverged from the uninterrupted run", map, seed, t + 1); break; }
			if (hs != ref_hash[t]) { report(r, "saved copy diverged from the uninterrupted run", map, seed, t + 1); break; }
		}
	}
	// 4. Replay that starts from the mid-match save, as the game's replay does: the save is the
	// replay's start.rts0sav, and the commands recorded after it (turn 1 onward) play on from it.
	if (r.failures == failures_before) {
		const std::string from_save_path = o.dir + "/match_from_save.rts0rec";
		std::string err;
		if (!tm_loaded.save_replay(from_save_path)) {
			report(r, "save_replay (from save) failed", map, seed, k);
		} else if (!tm_loaded.validate_replay(from_save_path, err)) {
			report(r, ("replay from save does not validate: " + err).c_str(), map, seed, k);
		} else {
			Simulation fsim;
			build_match(fsim, o, map, seed);
			if (!fsim.load_state(save_path.c_str())) {
				report(r, "load_state for the from-save replay failed", map, seed, k);
			} else {
				TurnManager fp;
				if (!fp.load_replay(from_save_path)) {
					report(r, ("load_replay (from save) failed: " + fp.last_replay_error()).c_str(), map, seed, k);
				} else {
					for (uint32_t t = k; t < n; ++t) {
						if (!fp.tick(TurnManager::TURN_DURATION, fsim)) {
							report(r, "from-save replay tick did not advance", map, seed, t + 1);
							break;
						}
						if (fp.replay_out_of_sync()) {
							report(r, "from-save replay out of sync", map, seed, t + 1);
							break;
						}
						if (fp.compute_sim_hash(fsim) != ref_hash[t]) {
							report(r, "from-save replayed hash differs from the uninterrupted run", map, seed, t + 1);
							break;
						}
					}
					if (!fp.replay_complete() && r.failures == failures_before)
						report(r, "from-save replay did not complete", map, seed, n);
				}
			}
		}
		std::remove(from_save_path.c_str());
	}
	std::remove(save_path.c_str());

	// 3. Replay the reference match into a fresh TurnManager + Simulation.
	if (r.failures == failures_before) {
		std::string err;
		if (!tm_ref.validate_replay(replay_path, err)) {
			report(r, ("replay does not validate: " + err).c_str(), map, seed, n);
		} else {
			Simulation rsim;
			build_match(rsim, o, map, seed);
			TurnManager rp;
			if (!rp.load_replay(replay_path)) {
				report(r, ("load_replay failed: " + rp.last_replay_error()).c_str(), map, seed, 0);
			} else {
				for (uint32_t t = 0; t < n; ++t) {
					if (!rp.tick(TurnManager::TURN_DURATION, rsim)) {
						report(r, "replay tick did not advance", map, seed, t + 1);
						break;
					}
					if (rp.replay_out_of_sync()) {
						report(r, "replay out of sync", map, seed, rp.replay_out_of_sync_turn());
						break;
					}
					if (rp.compute_sim_hash(rsim) != ref_hash[t]) {
						report(r, "replayed hash differs from the recorded run", map, seed, t + 1);
						break;
					}
				}
				if (!rp.replay_complete() && r.failures == failures_before)
					report(r, "replay did not complete", map, seed, n);
			}
		}
	}
	std::remove(replay_path.c_str());
	const bool ok = r.failures == failures_before;
	std::printf("%s map=%d seed=%u turns=%u save_turn=%u final_hash=%u\n", ok ? "[ok]" : "[fail]", map, seed, n, k,
			final_ref_hash);
	std::fflush(stdout);
	return ok;
}

} // namespace

int main(int argc, char **argv) {
	Options o;
	for (int i = 1; i < argc; ++i) {
		auto next = [&]() { return i + 1 < argc ? argv[++i] : (char *)"0"; };
		if (!std::strcmp(argv[i], "--maps")) o.maps = parse_list(next());
		else if (!std::strcmp(argv[i], "--seeds")) {
			o.seeds.clear();
			for (int v : parse_list(next())) o.seeds.push_back((uint32_t)v);
		} else if (!std::strcmp(argv[i], "--turns")) o.turns = (uint32_t)std::atoi(next());
		else if (!std::strcmp(argv[i], "--save-turn")) o.save_turn = (uint32_t)std::atoi(next());
		else if (!std::strcmp(argv[i], "--players")) o.players = std::atoi(next());
		else if (!std::strcmp(argv[i], "--size")) o.size = (float)std::atof(next());
		else if (!std::strcmp(argv[i], "--civs")) o.civs = parse_list(next());
		else if (!std::strcmp(argv[i], "--dir")) o.dir = next();
		else {
			std::fprintf(stderr, "unknown argument %s\n", argv[i]);
			return 2;
		}
	}
	if (o.turns < 4 || o.players < 2 || o.players > MAX_PLAYERS || o.maps.empty() || o.seeds.empty() || o.civs.empty()) {
		std::fprintf(stderr, "usage: soak_save_replay [--maps a,b] [--seeds a,b] [--turns N] [--save-turn K] [--players P] [--size S] [--civs a,b] [--dir D]\n");
		return 2;
	}
	if (o.save_turn >= o.turns) {
		std::fprintf(stderr, "--save-turn must be below --turns\n");
		return 2;
	}
	Result r;
	int matches = 0, passed = 0;
	for (int map : o.maps)
		for (uint32_t seed : o.seeds) {
			++matches;
			passed += soak_one(o, map, seed, r) ? 1 : 0;
		}
	std::printf("RESULT: %d/%d matches passed, %d desync(s)\n", passed, matches, r.failures);
	return r.failures ? 1 : 0;
}
