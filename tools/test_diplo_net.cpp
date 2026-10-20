// RTS0 diplomacy lockstep test (headless: no SCons, no Godot).
//
// Companion to tools/test_replay.cpp / tools/test_netcode.cpp. The diplomacy
// matrix is mixed into TurnManager::compute_sim_hash(), so a stance applied on
// one peer only, or on different turns, desyncs every other peer. A stance
// therefore reaches the sim through exactly one route: lockstep cmd 22, run on
// the same turn on every peer and as the player the sending client is seated
// as. The old NMT_DIPLOMACY carrier is retired (it scheduled by the receiver's
// own turn and trusted an actor byte from the wire).
//
//  1. wire contract: frozen ids, and a retired NMT_DIPLOMACY packet is dropped
//     before it reaches TurnManager.
//  2. cmd 22 on two lockstep peers: each player writes only its own row, on the
//     same turn on both peers, and the hashes stay equal.
//  3. negative control: the same stance applied to ONE peer only must make the
//     lockstep hashes DIVERGE, and receive_sync_check() must flag it.
//  4. a client cannot set another player's row: the batch sender decides the
//     player, whatever client_id the command claims.
//  5. G8 lockstep gate: with a live peer and no NMT_GAME_START applied, tick()
//     must refuse to advance (turn 1 OOS fix); mark_game_started() releases it.
//  6. P3 late-join GAME_START resend: a client admitted AFTER the host broadcast
//     GAME_START asks for setup + turn-0 state, the host resends, and the joiner
//     only releases its lockstep gate after the turn-0 hash is confirmed.
//  7. P3 maintainFleet upkeep: a built dock with no fleet produces exactly one
//     deterministic commission per turn, blocked while the dock is training or
//     unfinished, and it rides the ordinary turn command stream (so it is in the
//     replay history).
//  8. tribute (cmd 27) and barter (cmd 23) on two lockstep peers: both apply on
//     the same turn as the sending client's player, and the hashes stay equal.
//
// Exit: 0 = PASS, 1 = FAIL, 2 = bad usage.
#include "network/turn_manager.h"
#include "network/net_host.h"
#include "simulation/simulation.h"
#include "core/memory/arena.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>
using namespace rtso;

static int g_failures = 0;
static bool check(bool cond, const char *expr, const char *phase) {
	if (cond) return true;
	std::fprintf(stderr, "\n  [FAIL] %s: %s\n", phase, expr);
	++g_failures;
	return false;
}
// CHECK(cond, ph) mirrors tools/test_replay.cpp. An explanatory message can be
// supplied as an extra argument when the bare condition would be hard to read:
//   CHECK(cond, ph)   /   CHECK(cond, "why this matters", ph)
#define RTSO_NARG(...) RTSO_NARG_(__VA_ARGS__, 3, 2, 1, 0)
#define RTSO_NARG_(_1, _2, _3, N, ...) N
#define RTSO_SEL_I(n) RTSO_SEL_##n
#define RTSO_SEL(n) RTSO_SEL_I(n)
#define RTSO_SEL_1 CHECK_PLAIN
#define RTSO_SEL_2 CHECK_MSG
#define CHECK(cond, ...) RTSO_SEL(RTSO_NARG(__VA_ARGS__))(cond, __VA_ARGS__)
#define CHECK_PLAIN(cond, ph) check((cond), #cond, (ph))
#define CHECK_MSG(cond, msg, ph) check((cond), (msg), (ph))

struct Refs { std::vector<uint32_t> villagers, soldiers, resources; };

// Identical construction for every peer: same seed, Petra AI config and spawn
// order, so divergence cannot come from world setup.
static void build_world(Simulation &s, uint32_t seed, Refs &r) {
	r = Refs();
	s.set_sim_seed(seed); s.reset();
	s.set_petra_difficulty(3); s.set_petra_behavior(2);
	s.set_victory_condition(VICTORY_RELIC);
	s.set_starting_resources(900, 900, 500, 500);
	s.set_team_civ(0, CIV_ATHENIANS); s.set_team_civ(1, CIV_SPARTANS);
	s.spawn_building_world(-30.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_building_world(-26.0f, -4.0f, BLD_BARRACKS, 0, 1.0f);
	s.spawn_building_world(30.0f, 0.0f, BLD_TOWN_CENTER, 1, 1.0f);
	s.spawn_building_world(0.0f, 0.0f, BLD_WONDER, 2, 1.0f);
	for (int i = 0; i < 4; ++i) {
		const float a = 6.2831853f * (float)i / 4.0f;
		r.villagers.push_back(s.spawn_unit(-30.0f + std::cos(a) * 4.0f, std::sin(a) * 4.0f, 0, UNIT_VILLAGER, 100.0f));
	}
	for (int i = 0; i < 3; ++i)
		r.soldiers.push_back(s.spawn_unit(-20.0f + (float)i * 2.0f, -2.0f + (float)i * 2.0f, 0, UNIT_SWORDSMAN, 160.0f));
	r.soldiers.push_back(s.spawn_unit(18.0f, 2.0f, 1, UNIT_ARCHER, 90.0f));
	r.resources.push_back(s.spawn_resource_world(-38.0f, 14.0f, RES_FOOD, 1200));
	r.resources.push_back(s.spawn_resource_world(-44.0f, -12.0f, RES_WOOD, 1200));
	r.resources.push_back(s.spawn_resource_world(36.0f, -14.0f, RES_STONE, 900));
	s.spawn_relic(10.0f, 10.0f);
}

// Script RNG kept out of Simulation::SimRng so generating the order stream can
// never perturb sim state.
struct ScriptRng {
	uint32_t state;
	explicit ScriptRng(uint32_t seed) : state(seed ? seed : 1u) {}
	uint32_t next() { state = state * 1664525u + 1013904223u; return state; }
	float unit() { return (float)(next() & 0xFFFFFF) / 16777216.0f; }
};

// Turn N's orders: pure function of (refs, turn, rng state), so every peer runs
// a byte-identical stream.
static void script_turn(const Refs &r, uint32_t turn, ScriptRng &rng,
                        std::vector<NetCommand> &local, std::vector<NetCommand> &remote) {
	local.clear(); remote.clear();
	auto push = [](std::vector<NetCommand> &o, uint8_t type, uint32_t ea, uint32_t tgt,
	               float tx, float tz, uint8_t param, uint32_t client) {
		NetCommand c;
		c.client_id = client; c.cmd_type = type;
		if (ea) c.entity_ids.push_back(ea);
		c.target_id = tgt; c.target_x = tx; c.target_z = tz; c.param = param;
		o.push_back(c);
	};
	const uint32_t v = r.villagers[turn % r.villagers.size()];
	const uint32_t v2 = r.villagers[(turn + 3) % r.villagers.size()];
	push(local, 4 /*GATHER*/, v, r.resources[turn % r.resources.size()], 0.0f, 0.0f, 0, 0);
	push(local, 1 /*MOVE*/, v2, 0, -30.0f + rng.unit() * 12.0f, rng.unit() * 12.0f - 6.0f, 0, 0);
	push(local, 7 /*STANCE*/, v, 0, 0.0f, 0.0f, (uint8_t)(turn % 4), 0);
	const uint32_t a = r.soldiers[turn % r.soldiers.size()];
	push(remote, 10 /*ATTACK_MOVE*/, a, 0, -20.0f + rng.unit() * 10.0f, rng.unit() * 10.0f - 5.0f, 0, 1);
}

// NMT_DIPLOMACY travels over the reliable command channel, so poll the peers a
// few times with a real sleep to let the loopback stack deliver it.
static void pump(NetHost &a, NetHost &b, Arena &arena, int rounds = 8, int sleep_ms = 2) {
	for (int i = 0; i < rounds; ++i) {
		a.poll(arena);
		b.poll(arena);
		std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
	}
}
static void sleep_ms(int ms) {
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// Host on a high port, client dialed to 127.0.0.1, pumped until the host has
// admitted the client (or the budget runs out).
static bool start_peers(NetHost &h, NetHost &c, uint16_t port, int budget_ms = 1500) {
	NetConfig cfg;
	cfg.port = port;
	h.setLocalPlayerName("host"); // the host refuses a joiner by its own name
	c.setLocalPlayerName("joiner");
	if (!h.startHost(cfg)) return false;
	if (!c.startClient("127.0.0.1", port)) return false;
	Arena probe_arena;
	for (int waited = 0; waited < budget_ms; waited += 5) {
		h.poll(probe_arena);
		c.poll(probe_arena);
		if (h.getPeers().size() >= 1 && h.getPeers()[0].connected) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	return false;
}

// ---- Phase 1: frozen ids; a retired NMT_DIPLOMACY packet is dropped ---------
static bool run_wire_contract(const char *ph) {
	CHECK(static_cast<uint8_t>(NetMessageType::NMT_DIPLOMACY) == 31, ph);
	CHECK(static_cast<uint8_t>(NetMessageType::NMT_COUNT) == 36, ph);
	CHECK(static_cast<uint8_t>(NetMessageType::NMT_SETUP_REQUEST) == 35, ph);
	CHECK(static_cast<uint8_t>(NetMessageType::NMT_REJOIN_STATE) == 34, ph);
	// Append-only NMT ids 32/33 (late-join sync) and the frozen cmd_type block.
	CHECK(static_cast<uint8_t>(NetMessageType::NMT_JOIN_SYNC_STATE) == 32, ph);
	CHECK(static_cast<uint8_t>(NetMessageType::NMT_JOIN_SYNC_CHECK) == 33, ph);
	CHECK(static_cast<uint8_t>(NetCommandType::NET_CMD_DIPLOMACY) == 22, ph);
	CHECK(static_cast<uint8_t>(NetCommandType::NET_CMD_BARTER) == 23, ph);
	CHECK(static_cast<uint8_t>(NetCommandType::NET_CMD_MAINTAIN_FLEET) == 24, ph);
	CHECK(static_cast<uint8_t>(NetCommandType::NET_CMD_TRAIN_TEMPLATE) == 25, ph);
	CHECK(static_cast<uint8_t>(NetCommandType::NET_CMD_TRIBUTE) == 27, ph);

	// An old or hostile peer still sending NMT_DIPLOMACY: NetHost drops it before
	// TurnManager's onMessage, so it can never queue a stance out of band.
	NetHost host;
	int forwarded = 0;
	host.onMessage([&forwarded](uint32_t, const NetMessage &m) {
		if (m.type == static_cast<uint8_t>(NetMessageType::NMT_DIPLOMACY)) ++forwarded;
	});
	const std::vector<uint8_t> payload = {1, 0, (uint8_t)Simulation::DIPLO_ALLY}; // "player 1 sets player 0's row"
	std::vector<uint8_t> wire = NetPacketSerializer::pack_message(NetMessageType::NMT_DIPLOMACY, 3, payload);
	NetMessage rx;
	if (!check(NetPacketSerializer::unpack_message(wire.data(), wire.size(), rx), "pack/unpack round-trip", ph)) return false;
	host.dispatchMessage(1, rx);
	CHECK(forwarded == 0, "a retired NMT_DIPLOMACY never reaches TurnManager", ph);
	return true;
}

static NetCommand stance_cmd(uint8_t target, uint8_t stance, uint32_t claimed_client) {
	NetCommand c;
	c.cmd_type = 22;
	c.client_id = claimed_client;
	c.target_id = target;
	c.param = stance;
	return c;
}

// Run turns [from, to] on two lockstep peers with the scripted stream; extra
// commands for a turn ride along in each peer's own batch.
static bool run_turns(const Refs &ra, TurnManager &ta, Simulation &sa, TurnManager &tb, Simulation &sb,
                      ScriptRng &g, uint32_t from, uint32_t to, uint32_t extra_turn,
                      const std::vector<NetCommand> &extra_a, const std::vector<NetCommand> &extra_b,
                      const char *ph) {
	std::vector<NetCommand> local, remote;
	for (uint32_t t = from; t <= to; ++t) {
		script_turn(ra, t, g, local, remote);
		if (t == extra_turn) {
			local.insert(local.end(), extra_a.begin(), extra_a.end());
			remote.insert(remote.end(), extra_b.begin(), extra_b.end());
		}
		for (size_t i = 0; i < local.size(); ++i) ta.post_local_command(local[i]);
		for (size_t i = 0; i < remote.size(); ++i) tb.post_local_command(remote[i]);
		ta.receive_remote_turn_batch(t, 1, remote);
		tb.receive_remote_turn_batch(t, 0, local);
		if (!check(ta.tick(TurnManager::TURN_DURATION, sa), "peer A tick advanced", ph)) return false;
		if (!check(tb.tick(TurnManager::TURN_DURATION, sb), "peer B tick advanced", ph)) return false;
		if (!check(ta.compute_sim_hash(sa) == tb.compute_sim_hash(sb), "peers hash-identical every turn", ph)) return false;
	}
	return true;
}

// ---- Phase 2: cmd 22 on two lockstep peers ------------------------------------
static bool run_replicated_stance(const char *ph) {
	Refs ra, rb;
	Simulation sa, sb;
	build_world(sa, 4242, ra);
	build_world(sb, 4242, rb);
	TurnManager ta, tb;
	tb.set_local_client_id(1); // peer B is the joiner: its commands run as player 1
	CHECK(ta.compute_sim_hash(sa) == tb.compute_sim_hash(sb), ph);
	// Every player starts hostile to every other (free-for-all).
	CHECK(sa.get_diplomacy(0, 2) == Simulation::DIPLO_ENEMY && sa.get_diplomacy(1, 3) == Simulation::DIPLO_ENEMY, ph);

	ScriptRng g(9001);
	if (!run_turns(ra, ta, sa, tb, sb, g, 1, 3, 0, {}, {}, ph)) return false;
	const uint32_t base_hash = ta.compute_sim_hash(sa);

	// Turn 4: player 0 allies player 2; player 1 goes neutral toward player 3.
	const std::vector<NetCommand> from_a = {stance_cmd(2, Simulation::DIPLO_ALLY, 0)};
	const std::vector<NetCommand> from_b = {stance_cmd(3, Simulation::DIPLO_NEUTRAL, 1)};
	if (!run_turns(ra, ta, sa, tb, sb, g, 4, 4, 4, from_a, from_b, ph)) return false;
	for (Simulation *s : {&sa, &sb}) {
		CHECK(s->get_diplomacy(0, 2) == Simulation::DIPLO_ALLY, "player 0's own row moved", ph);
		CHECK(s->get_diplomacy(1, 3) == Simulation::DIPLO_NEUTRAL, "player 1's own row moved", ph);
		CHECK(s->get_diplomacy(1, 2) == Simulation::DIPLO_ENEMY, "player 0's stance did not touch row 1", ph);
		CHECK(s->get_diplomacy(0, 3) == Simulation::DIPLO_ENEMY, "player 1's stance did not touch row 0", ph);
	}
	CHECK(ta.compute_sim_hash(sa) != base_hash, "the stances are observable in the lockstep hash", ph);

	// Further turns with the stances applied stay in sync.
	if (!run_turns(ra, ta, sa, tb, sb, g, 5, 10, 0, {}, {}, ph)) return false;
	CHECK(!ta.has_out_of_sync() && !tb.has_out_of_sync(), ph);
	return true;
}

// ---- Phase 3: negative control -- one-sided stance MUST desync the peers ------
static bool run_one_sided_negative_control(const char *ph) {
	Refs ra, rb;
	Simulation sa, sb;
	build_world(sa, 4242, ra);
	build_world(sb, 4242, rb);
	TurnManager ta, tb;
	tb.set_local_client_id(1); // peer B is the joiner: its commands run as player 1
	CHECK(ta.compute_sim_hash(sa) == tb.compute_sim_hash(sb), ph);

	ScriptRng g(9001);
	std::vector<NetCommand> local, remote;
	for (uint32_t t = 1; t <= 4; ++t) {
		script_turn(ra, t, g, local, remote);
		for (size_t i = 0; i < local.size(); ++i) ta.post_local_command(local[i]);
		for (size_t i = 0; i < remote.size(); ++i) tb.post_local_command(remote[i]);
		ta.receive_remote_turn_batch(t, 1, remote);
		tb.receive_remote_turn_batch(t, 0, local);
		ta.tick(TurnManager::TURN_DURATION, sa);
		tb.tick(TurnManager::TURN_DURATION, sb);
	}
	const uint32_t before = ta.compute_sim_hash(sa);
	CHECK(ta.compute_sim_hash(sa) == tb.compute_sim_hash(sb), ph);

	// The bug NMT_DIPLOMACY prevents: the stance lands on ONE peer only, exactly
	// as the local setter from src/presentation/main_node.cpp can do today.
	sa.set_diplomacy(2, Simulation::DIPLO_ALLY);

	if (!check(sa.get_diplomacy(2) != sb.get_diplomacy(2),
	           "one-sided stance leaves the peers' get_diplomacy disagreeing", ph)) return false;
	// Same turn, no sim step in between: diplomacy_state() is in the hash.
	const uint32_t after_a = ta.compute_sim_hash(sa);
	const uint32_t after_b = tb.compute_sim_hash(sb);
	if (!check(after_a != after_b, "one-sided stance DIVERGES the lockstep hash", ph)) return false;
	if (!check(after_a != before, "one-sided stance changes the hash at all", ph)) return false;
	std::fprintf(stdout, "\n         pre=%u one_sided_a=%u one_sided_b=%u\n", before, after_a, after_b);

	// The engine's OOS detector must flag the same divergence when peers exchange
	// their hashes (NMT_SYNC_CHECK -> receive_sync_check). It compares recorded
	// frames, so use the PREVIOUS turn: the current turn's frame was hashed after
	// the one-sided stance. This is the same pattern turn_manager.cpp uses after
	// tick() (it compares peers against the PREVIOUS turn).
	const uint32_t prev_turn = ta.current_turn() - 1;
	CHECK(prev_turn > 0, ph);
	ta.receive_sync_check(prev_turn, 1, after_b);
	if (!check(ta.has_out_of_sync(), "receive_sync_check raises OOS on the diverged peer", ph)) return false;
	CHECK(ta.oos_turn() == prev_turn, ph);
	uint32_t frame_hash = 0;
	for (const TurnFrame &f : ta.turn_history())
		if (f.turn_index == prev_turn) frame_hash = f.sim_state_hash;
	CHECK(frame_hash != 0, "the compared turn has a recorded frame hash", ph);
	CHECK(ta.oos_received_hash() == after_b, "OOS carries the peer's diverged hash", ph);
	CHECK(ta.oos_expected_hash() == frame_hash, "OOS carries the locally recorded hash", ph);
	CHECK(ta.oos_expected_hash() != ta.oos_received_hash(), ph);
	CHECK(ta.oos_peers().size() == 1 && ta.oos_peers()[0] == 1, ph);

	// Symmetric case: the same stance applied to BOTH peers -- what the
	// NMT_DIPLOMACY payload produces in phase 2 -- must NOT raise OOS. Use fresh
	// peers whose recorded frames already contain the replicated stance, so the
	// comparison is turn-for-turn with the divergence case above.
	Refs ra2, rb2;
	Simulation sa2, sb2;
	build_world(sa2, 4242, ra2);
	build_world(sb2, 4242, rb2);
	TurnManager ta2, tb2;
	tb2.set_local_client_id(1); // peer B is the joiner: its commands run as player 1
	sa2.set_diplomacy(2, Simulation::DIPLO_ALLY);
	sb2.set_diplomacy(2, Simulation::DIPLO_ALLY);
	ScriptRng g2(9001);
	for (uint32_t t = 1; t <= 4; ++t) {
		script_turn(ra2, t, g2, local, remote);
		for (size_t i = 0; i < local.size(); ++i) ta2.post_local_command(local[i]);
		for (size_t i = 0; i < remote.size(); ++i) tb2.post_local_command(remote[i]);
		ta2.receive_remote_turn_batch(t, 1, remote);
		tb2.receive_remote_turn_batch(t, 0, local);
		ta2.tick(TurnManager::TURN_DURATION, sa2);
		tb2.tick(TurnManager::TURN_DURATION, sb2);
		CHECK(ta2.compute_sim_hash(sa2) == tb2.compute_sim_hash(sb2), "replicated stance keeps peers identical", ph);
	}
	CHECK(ta2.has_out_of_sync() == false && tb2.has_out_of_sync() == false, ph);
	// Feed the frame hash recorded for the turn being compared, mirroring the
	// NMT_SYNC_CHECK wire format: the "local_hash=..." in the log above is the
	// RECORDED frame hash, not the live one. Passing the live hash here made
	// receive_sync_check compare a frame hash against a live hash.
	uint32_t f3a = 0, f3b = 0;
	for (const TurnFrame &f : ta2.turn_history()) if (f.turn_index == ta2.current_turn() - 1) f3a = f.sim_state_hash;
	for (const TurnFrame &f : tb2.turn_history()) if (f.turn_index == tb2.current_turn() - 1) f3b = f.sim_state_hash;
	CHECK(f3a != 0 && f3a == f3b, "replicated peers recorded the same frame hash", ph);
	ta2.receive_sync_check(ta2.current_turn() - 1, 1, f3b);
	if (!check(ta2.has_out_of_sync() == false, "replicated stance raises no OOS", ph)) return false;
	// And the two histories genuinely differ from the one-sided run, i.e. the
	// stance is present in the hash rather than the two cases being identical.
	CHECK(ta2.turn_history().back().sim_state_hash != frame_hash, ph);
	return true;
}

// ---- Phase 4: a client cannot set another player's row ----------------------
// The batch sender decides the player: receive_remote_turn_batch stamps the
// sender's id over whatever client_id the command claims, and tick() runs cmd
// 22 as player_for_client(sender). A joiner claiming to be the host still only
// moves its own row.
static bool run_client_cannot_forge_actor(const char *ph) {
	Refs ra, rb;
	Simulation sa, sb;
	build_world(sa, 4242, ra);
	build_world(sb, 4242, rb);
	TurnManager ta, tb;
	tb.set_local_client_id(1);
	ScriptRng g(77);
	if (!run_turns(ra, ta, sa, tb, sb, g, 1, 2, 0, {}, {}, ph)) return false;

	const std::vector<NetCommand> forged = {stance_cmd(4, Simulation::DIPLO_ALLY, 0 /* claims to be the host */)};
	if (!run_turns(ra, ta, sa, tb, sb, g, 3, 3, 3, {}, forged, ph)) return false;
	for (Simulation *s : {&sa, &sb}) {
		CHECK(s->get_diplomacy(0, 4) == Simulation::DIPLO_ENEMY, "the host's row is untouched", ph);
		CHECK(s->get_diplomacy(1, 4) == Simulation::DIPLO_ALLY, "the stance landed on the sender's own row", ph);
	}
	// A stance toward oneself, out of range, or unknown changes nothing, and the
	// peers stay in sync (run_turns checks the hash every turn).
	const std::vector<NetCommand> bad = {stance_cmd(1, Simulation::DIPLO_ALLY, 1), stance_cmd(9, Simulation::DIPLO_ALLY, 1),
	                                     stance_cmd(5, 7, 1)};
	if (!run_turns(ra, ta, sa, tb, sb, g, 4, 4, 4, {}, bad, ph)) return false;
	for (Simulation *s : {&sa, &sb}) {
		CHECK(s->get_diplomacy(1, 1) == Simulation::DIPLO_ALLY, "a player's own diagonal never moves", ph);
		CHECK(s->get_diplomacy(1, 5) == Simulation::DIPLO_ENEMY, "an unknown stance is rejected", ph);
	}
	return true;
}

// ---- Phase 5: G8 lockstep gate -- no turn before NMT_GAME_START is applied --
static bool run_lockstep_gate(const char *ph) {
	Refs r;
	Simulation sim;
	build_world(sim, 4242, r);
	TurnManager tm;

	// Offline play has no peer, so nothing is gated: the gate exists for a SESSION,
	// where a peer that ticks before Start hashes its own boot world.
	CHECK(tm.net_game_started(), "offline TurnManager is not gated");
	CHECK(tm.tick(TurnManager::TURN_DURATION, sim), "offline tick advances");
	CHECK(tm.gated_tick_count() == 0, "offline tick is not counted as gated");

	// A live CLIENT peer with no GAME_START applied: the lockstep must stay shut.
	// (The gate targets the joiner: it is the peer ticking a boot world. A host is
	// released on bind because it owns its world -- see TurnManager::set_net_host.)
	NetHost host, client;
	NetConfig cfg;
	cfg.port = 21800;
	client.setLocalPlayerName("joiner");
	bool bound = host.startHost(cfg);
	bool dialed = client.startClient("127.0.0.1", cfg.port);
	Arena arena;
	for (int waited = 0; waited < 500 && !(host.getPeers().size() >= 1 && host.getPeers()[0].connected); waited += 5) {
		host.poll(arena);
		client.poll(arena);
		sleep_ms(5);
	}
	Simulation sim2;
	Refs r2;
	build_world(sim2, 4242, r2);
	TurnManager tm2(&client, 1);
	tm2.set_game_start_applied_sim(&sim2);
	CHECK(bound && dialed, "host/client peers established for the gate test");
	CHECK(tm2.get_net_host() != nullptr, "TurnManager is attached to the client");
	CHECK(!tm2.net_game_started(), "a live peer with no GAME_START is gated");
	const uint32_t hash_before = tm2.compute_sim_hash(sim2);
	for (int i = 0; i < 5; ++i) {
		if (!check(!tm2.tick(TurnManager::TURN_DURATION, sim2),
		           "tick refuses to advance while gated", ph)) return false;
	}
	CHECK(tm2.current_turn() == 0, "gated tick did not advance the turn counter");
	CHECK(tm2.gated_tick_count() == 5, "every gated tick is accounted for");
	CHECK(tm2.turn_history().empty(), "gated tick recorded no replay frame");
	// The turn-1 OOS signature: two peers that tick here would already disagree.
	CHECK(tm2.compute_sim_hash(sim2) == hash_before, "gated tick left the world untouched");

	// The NMT_GAME_START resend/apply releases the gate: note_game_start_applied()
	// is called by take_pending_game_start_full() once the game layer has the
	// setup. Deliver a real one here rather than forcing the flag.
	{
		GameSetupParams setup;
		setup.map_id = "random_continental";
		setup.seed = 4242;
		GameStartMsg gsm;
		gsm.init_attributes = format_game_start_full(setup);
		std::vector<uint8_t> payload = NetPacketSerializer::frame_game_start(gsm);
		std::vector<uint8_t> wire = NetPacketSerializer::pack_message(NetMessageType::NMT_GAME_START, 0, payload);
		NetMessage rx;
		if (!check(NetPacketSerializer::unpack_message(wire.data(), wire.size(), rx), "GAME_START frames", ph)) return false;
		client.dispatchMessage(0, rx);
		CHECK(tm2.has_pending_game_start(), "GAME_START was staged for the game layer", ph);
		GameSetupParams got;
		CHECK(tm2.take_pending_game_start_full(got), "the game layer consumed the setup", ph);
		CHECK(got.seed == 4242, ph);
	}
	// A malformed GAME_START must NOT open the gate.
	{
		GameStartMsg gsm;
		gsm.init_attributes = "this_has_no_seed";
		std::vector<uint8_t> payload = NetPacketSerializer::frame_game_start(gsm);
		std::vector<uint8_t> wire = NetPacketSerializer::pack_message(NetMessageType::NMT_GAME_START, 0, payload);
		NetMessage rx;
		NetPacketSerializer::unpack_message(wire.data(), wire.size(), rx);
		client.dispatchMessage(0, rx);
		CHECK(!tm2.has_pending_game_start(), "a malformed GAME_START is rejected", ph);
	}
	// Applying a GAME_START releases the gate; mark_game_started() is the host's
	// explicit equivalent.
	CHECK(tm2.net_game_started(), "mark_game_started releases the gate");
	CHECK(tm2.tick(TurnManager::TURN_DURATION, sim2), "tick advances once started");
	CHECK(tm2.current_turn() == 1, "released tick advanced exactly one turn");

	// clear_game_started() re-closes it (used when a session tears down).
	tm2.clear_game_started();
	CHECK(!tm2.net_game_started(), "clear_game_started re-closes the gate");
	CHECK(!tm2.tick(TurnManager::TURN_DURATION, sim2), "gate re-closes the lockstep");
	host.stop();
	client.stop();
	return true;
}

// ---- Phase 6: P3 late-join GAME_START resend + turn-0 hash handshake -------
static bool run_late_join_game_start_resend(const char *ph) {
	GameSetupParams setup;
	setup.map_id = "random_continental";
	setup.seed = 777001;
	setup.victory = 1;
	setup.difficulty = 4;
	setup.behavior = 3;
	setup.start_res = 2;
	setup.slot_count = 3;
	setup.civs[0] = 3; setup.civs[1] = 1; setup.civs[2] = 9;
	setup.slot_types[0] = 'h'; setup.slot_types[1] = 'a'; setup.slot_types[2] = 'x';
	setup.teams[0] = 1; setup.teams[1] = 2; setup.teams[2] = 2;
	const std::string attrs = format_game_start_full(setup);

	NetHost host, joiner;
	Arena arena;
	uint16_t port = 0;
	for (uint16_t cand = 21910; cand < 22000 && port == 0; ++cand)
		if (start_peers(host, joiner, cand)) port = cand;
	if (!check(port != 0, "host/joiner peers established", ph)) {
		host.stop(); joiner.stop();
		return false;
	}

	TurnManager th(&host, 0);
	TurnManager tj(&joiner, 1);
	CHECK(!host.game_start_sent(), "no GAME_START has been sent yet", ph);

	// A joiner asking BEFORE the host has an authoritative setup is refused.
	// (The joiner dials first, so this is the state a racing join sees.)
	tj.request_join_sync(0);
	pump(host, joiner, arena, 12);
	CHECK(!tj.join_sync_accepted(), "a pre-Start resync request is rejected", ph);
	CHECK(tj.join_sync_error() != 0, "the rejection carries an error code", ph);
	CHECK(tj.net_game_started(), "a rejected joiner is released so it cannot hang", ph);
	tj.reset();

	// The host starts the match: it applies its own setup locally and broadcasts.
	Refs rhost;
	Simulation shost;
	build_world(shost, 777001, rhost);
	th.mark_game_started(&shost);
	host.sendGameStart(attrs);
	CHECK(host.game_start_sent(), "sending GAME_START records the authoritative setup", ph);
	// The host's TurnManager keeps the validated attributes for the resend.
	{
		GameStartMsg gsm;
		gsm.init_attributes = attrs;
		std::vector<uint8_t> payload = NetPacketSerializer::frame_game_start(gsm);
		std::vector<uint8_t> wire = NetPacketSerializer::pack_message(NetMessageType::NMT_GAME_START, 0, payload);
		NetMessage rx;
		if (!check(NetPacketSerializer::unpack_message(wire.data(), wire.size(), rx), "host GAME_START frames", ph)) return false;
		host.dispatchMessage(1, rx); // host receives the echo through its own relay path
	}
	CHECK(th.last_game_start_attributes() == attrs, "TurnManager kept the verbatim attributes", ph);

	// The host world the joiner will rebuild. No turn has run yet, so the BOOT hash
	// (pre-command, pre-sim-step) is exactly the state the resend reproduces.
	Refs rh;
	Simulation sh;
	build_world(sh, 777001, rh);
	const uint32_t boot_hash = th.compute_sim_hash(shost);
	CHECK(boot_hash != 0, "the host boot hash is non-zero", ph);

	// The joiner, admitted AFTER the broadcast, asks for the resend.
	tj.request_join_sync(0);
	for (int i = 0; i < 60 && !tj.join_sync_accepted(); ++i) {
		host.poll(arena);
		joiner.poll(arena);
		sleep_ms(5);
	}
	CHECK(tj.join_sync_accepted(), "the host accepted the late-join resync", ph);
	CHECK(tj.join_sync_error() == 0, "an accepted resync carries no error", ph);
	CHECK(th.join_sync_round_trips() == 1, "the host answered exactly one request", ph);
	CHECK(th.last_game_start_attributes() == attrs, "the host resent its own attributes", ph);
	// The reply carries the turn-0 BOOT hash -- the only value a joiner rebuilding
	// from the resend can actually check (a post-turn hash would need a local
	// replay of every turn first).
	CHECK(tj.join_sync_turn() == 0, "the accepted sync is at turn 0", ph);
	CHECK(tj.join_sync_hash() == boot_hash,
	      "the host reports its boot hash, not a post-turn hash", ph);
	// The resend must be staged for the joiner's game layer, exactly like the
	// first broadcast -- and it must be the FULL setup, not just map+seed.
	GameSetupParams got;
	if (!check(tj.take_pending_game_start_full(got), "the joiner received the GAME_START resend", ph)) return false;
	CHECK(got.map_id == setup.map_id && got.seed == setup.seed, "resend carries map+seed", ph);
	CHECK(got.victory == setup.victory && got.difficulty == setup.difficulty &&
	      got.behavior == setup.behavior && got.start_res == setup.start_res,
	      "resend carries victory/difficulty/behavior/start_res", ph);
	CHECK(got.slot_count == setup.slot_count && got.civs[2] == setup.civs[2] &&
	      got.slot_types[0] == 'h' && got.slot_types[1] == 'a', "resend carries the slot table", ph);
	// Consuming the setup opened the joiner's gate at the recorded apply turn.
	CHECK(tj.net_game_started(), "the joiner released its gate on the resend", ph);
	CHECK(tj.apply_base_turn() == 0, "the joiner's apply base turn was recorded", ph);

	// The joiner rebuilds turn 0 from the resend and confirms the hash. The
	// TurnManager that owns the sync is tj itself -- the rebuild happens in the
	// game layer, not in the network layer.
	Refs rj;
	Simulation sj;
	build_world(sj, got.seed, rj);
	tj.set_game_start_applied_sim(&sj);
	const uint32_t rebuilt_hash = tj.compute_sim_hash(sj);
	if (!check(rebuilt_hash == boot_hash,
	           "the joiner's rebuilt turn-0 hash matches the host's", ph)) {
		std::fprintf(stderr, "         host_boot=%u joiner_rebuilt=%u\n", boot_hash, rebuilt_hash);
		host.stop(); joiner.stop();
		return false;
	}
	// Negative control for the handshake: a joiner whose world is NOT the host's
	// (here a different seed, standing in for a different map/civs/spawn order)
	// must be detectable, or the check is vacuous.
	{
		Simulation wrong;
		Refs rw;
		build_world(wrong, got.seed + 1, rw);
		CHECK(tj.compute_sim_hash(wrong) != boot_hash,
		      "a differently-seeded rebuild does NOT match the host's boot hash", ph);
	}

	// Closing the handshake releases the joiner's lockstep.
	JoinSyncCheckMsg chk;
	chk.role = JoinSyncMsg::ROLE_JOINER;
	chk.accepted = 1;
	chk.turn = 0;
	chk.hash = rebuilt_hash;
	tj.receive_join_sync_check(0, chk);
	CHECK(tj.join_sync_accepted() && !tj.join_sync_mismatch(), "joiner accepted the sync", ph);
	CHECK(tj.join_sync_turn() == 0, "joiner recorded the synced turn", ph);
	CHECK(tj.net_game_started(), "a confirmed sync releases the joiner's lockstep", ph);
	CHECK(tj.tick(TurnManager::TURN_DURATION, sj), "the confirmed joiner can now tick", ph);
	CHECK(tj.current_turn() == 1, ph);

	// A MISMATCH must abort the join rather than let the joiner tick.
	TurnManager tj_bad(&joiner, 1);
	tj_bad.mark_game_started();
	JoinSyncCheckMsg bad;
	bad.role = JoinSyncMsg::ROLE_JOINER;
	bad.accepted = 0;
	bad.turn = 0;
	bad.hash = boot_hash ^ 0x5a5a5a5au;
	tj_bad.receive_join_sync_check(0, bad);
	CHECK(tj_bad.join_sync_mismatch(), "a reported mismatch is recorded", ph);
	CHECK(!tj_bad.net_game_started(), "a mismatched join must not enter the lockstep", ph);
	if (!check(!tj_bad.tick(TurnManager::TURN_DURATION, sj), "a mismatched joiner is still gated", ph)) {
		host.stop(); joiner.stop();
		return false;
	}

	// Past the sync point the host has no provable turn-0 state left (RTS0 has no
	// state snapshot), so it must REFUSE rather than hand over a number the joiner
	// cannot check. Asking again now that turn 1 has completed must be rejected.
	Refs rh2;
	Simulation sh2;
	build_world(sh2, 777001, rh2);
	TurnManager th_late(&host, 0);
	th_late.mark_game_started();
	host.sendGameStart(attrs);
	{
		GameStartMsg gsm;
		gsm.init_attributes = attrs;
		std::vector<uint8_t> payload = NetPacketSerializer::frame_game_start(gsm);
		std::vector<uint8_t> wire = NetPacketSerializer::pack_message(NetMessageType::NMT_GAME_START, 0, payload);
		NetMessage rx;
		NetPacketSerializer::unpack_message(wire.data(), wire.size(), rx);
		host.dispatchMessage(1, rx);
	}
	CHECK(th_late.join_sync_turn_window() == 0, "the late-join window is closed by default", ph);
	TurnManager tj_late(&joiner, 1);
	CHECK(th_late.tick(TurnManager::TURN_DURATION, sh2), "host advanced past turn 0", ph);
	uint32_t st = 0, sh_hash = 0;
	CHECK(th_late.answer_join_sync(1, 0, st, sh_hash) == false,
	      "a joiner arriving after turn 1 is refused (no provable turn-0 state)", ph);
	CHECK(th_late.join_sync_round_trips() == 0, "a refused request resends nothing", ph);
	// A joiner that names a concrete turn still gets the host's post-turn hash,
	// which the game layer can verify after replaying locally.
	CHECK(th_late.answer_join_sync(1, 1, st, sh_hash), "a joiner naming turn 1 is served", ph);
	CHECK(sh_hash == th_late.turn_history().back().sim_state_hash,
	      "the served hash is the post-turn hash of that turn", ph);
	CHECK(th_late.answer_join_sync(1, 0, st, sh_hash) == false, ph);

	host.stop();
	joiner.stop();
	return true;
}

// ---- Phase 8: tribute and barter ride the turn stream ----------------------------
static NetCommand tribute_cmd(uint8_t to, uint8_t res, int amount, uint32_t claimed_client) {
	NetCommand c;
	c.cmd_type = static_cast<uint8_t>(NetCommandType::NET_CMD_TRIBUTE);
	c.client_id = claimed_client;
	c.target_id = to;
	c.target_x = (float)amount;
	c.param = res;
	return c;
}

static bool run_tribute_and_barter(const char *ph) {
	Refs ra, rb;
	Simulation sa, sb;
	build_world(sa, 4242, ra);
	build_world(sb, 4242, rb);
	// Player 1 owns a finished market, so it may barter.
	for (Simulation *s : {&sa, &sb}) s->spawn_building_world(20.0f, 20.0f, BLD_MARKET, 1, 1.0f);
	TurnManager ta, tb;
	tb.set_local_client_id(1); // peer B plays player 1
	ScriptRng g(9001);
	if (!run_turns(ra, ta, sa, tb, sb, g, 1, 2, 0, {}, {}, ph)) return false;
	const int p0_food = sa.player_resource(0, RES_FOOD), p1_food = sa.player_resource(1, RES_FOOD);
	const int p1_wood = sa.player_resource(1, RES_WOOD), p1_stone = sa.player_resource(1, RES_STONE);
	const int quote = sa.barter_quote(RES_WOOD, RES_STONE, 100);
	CHECK(quote == 82, "fresh market: 100 wood buys round(90/110*100) stone", ph);

	// Turn 3: player 1 tributes 100 food to player 0 and sells 100 wood for stone.
	// A tribute claiming client 0 still pays from player 1: the batch sender decides.
	NetCommand barter;
	barter.cmd_type = static_cast<uint8_t>(NetCommandType::NET_CMD_BARTER);
	barter.client_id = 1;
	barter.target_x = (float)RES_WOOD;
	barter.target_z = (float)RES_STONE;
	barter.target_id = 100;
	const std::vector<NetCommand> from_b = {tribute_cmd(0, RES_FOOD, 100, 1), tribute_cmd(0, RES_FOOD, 50, 0), barter};
	if (!run_turns(ra, ta, sa, tb, sb, g, 3, 3, 3, {}, from_b, ph)) return false;
	for (Simulation *s : {&sa, &sb}) {
		CHECK(s->player_resource(0, RES_FOOD) - p0_food == 150, "player 0 received both tributes", ph);
		CHECK(p1_food - s->player_resource(1, RES_FOOD) == 150, "player 1 paid both tributes", ph);
		CHECK(p1_wood - s->player_resource(1, RES_WOOD) == 100, "player 1 sold 100 wood", ph);
		CHECK(s->player_resource(1, RES_STONE) - p1_stone == quote, "player 1 bought the quoted stone", ph);
		// Barter.js (0.28): 2% per 100 bought, so 82 stone moves them 1.64%.
		CHECK(std::fabs(s->barter_price_diff(RES_WOOD) + 1.64f) < 1e-3f && std::fabs(s->barter_price_diff(RES_STONE) - 1.64f) < 1e-3f,
		      "the deal moved the shared prices by 1.64%", ph);
	}
	// Player 0 has no market: its barter is refused on both peers.
	NetCommand p0_barter = barter;
	p0_barter.client_id = 0;
	const int p0_wood = sa.player_resource(0, RES_WOOD);
	if (!run_turns(ra, ta, sa, tb, sb, g, 4, 4, 4, {p0_barter}, {}, ph)) return false;
	CHECK(sa.player_resource(0, RES_WOOD) == p0_wood && sb.player_resource(0, RES_WOOD) == p0_wood,
	      "no market, no barter", ph);
	if (!run_turns(ra, ta, sa, tb, sb, g, 5, 8, 0, {}, {}, ph)) return false;
	CHECK(!ta.has_out_of_sync() && !tb.has_out_of_sync(), ph);
	return true;
}

// Networked game setup (0 A.D. NMT_GAME_SETUP / NMT_READY plus RTS0's
// NMT_SETUP_REQUEST): the setup survives the wire, only the host's setup
// counts, the host stamps who asked and who is ready, and nothing is taken
// once the game started.
static bool run_game_setup_wire(const char *ph) {
	NetSetupState n;
	n.setup.map_id = "mainland";
	n.setup.seed = 0;
	n.setup.slot_count = 3;
	n.setup.civs[0] = 15; // Germans
	n.setup.civs[1] = RANDOM_CIV_WIRE;
	n.setup.speed = 150;
	n.setup.lock_players = 1;
	n.setup.rated = 1;
	n.setup.slot_types[2] = 'h';
	n.settings_version = 7;
	n.seq = 12;
	n.slot_client[0] = 0;
	n.slot_client[2] = 1;
	n.clients = 0x7;
	n.ready = 0x2;
	n.stay_ready = 0x1;
	n.observers = 0x4;
	n.names[0] = "alice";
	n.names[1] = "b;o,b=%";
	n.names[2] = "carol";
	NetSetupState back;
	CHECK(parse_net_setup(format_net_setup(n), back), "a setup parses back", ph);
	CHECK(back.setup.civs[0] == 15 && back.setup.civs[1] == RANDOM_CIV_WIRE, "Germans and Random civs survive", ph);
	CHECK(back.setup.speed == 150 && back.setup.lock_players == 1 && back.setup.rated == 1,
	      "game speed, the scenario lock and Rated Game survive", ph);
	CHECK(back.settings_version == 7 && back.seq == 12, "versions survive", ph);
	CHECK(back.slot_client[0] == 0 && back.slot_client[1] == -1 && back.slot_client[2] == 1, "seats survive", ph);
	CHECK(back.clients == 0x7 && back.ready == 0x2 && back.stay_ready == 0x1 && back.observers == 0x4,
	      "clients, ready, stay ready and observers survive", ph);
	CHECK(back.names[1] == "b;o,b=%" && back.names[2] == "carol", "names with separators survive", ph);
	CHECK(back.slot_of(1) == 2 && back.slot_of(2) == -1 && back.all_ready(), "slot_of and all_ready", ph);
	back.ready = 0;
	CHECK(!back.all_ready(), "a seated client not ready holds the start", ph);
	back.stay_ready = 0x2;
	CHECK(back.all_ready(), "stay ready counts as ready", ph);
	// 0.28 NetServer MAX_CLIENTS 41: clients past id 7 are listed, seated and counted too.
	n.slot_client[3] = 12;
	n.clients |= (1ull << 12) | (1ull << 40);
	n.observers |= 1ull << 40;
	n.names[12] = "dave";
	n.names[40] = "erin";
	NetSetupState wide;
	CHECK(parse_net_setup(format_net_setup(n), wide), "a setup with clients 12 and 40 parses back", ph);
	CHECK(wide.slot_client[3] == 12 && wide.slot_of(12) == 3 && wide.clients == n.clients && wide.observers == n.observers,
	      "client 12's seat and the 64-bit masks survive", ph);
	CHECK(wide.names[12] == "dave" && wide.names[40] == "erin" && wide.names[1] == "b;o,b=%", "names past client 7 survive", ph);
	CHECK(!wide.all_ready(), "a seated client 12 that is not ready holds the start", ph);
	wide.ready |= 1ull << 12;
	CHECK(wide.all_ready(), "and counts once ready", ph);
	GameSetupParams plain;
	CHECK(!parse_net_setup(format_game_start_full(plain), back), "a GAME_START string is no setup", ph);
	GameSetupParams g;
	CHECK(parse_game_start_full(format_game_start_full(n.setup), g) && g.civs[0] == 15, "GAME_START keeps civ 15", ph);

	NetHost host, joiner;
	Arena arena;
	uint16_t port = 0;
	for (uint16_t cand = 22010; cand < 22100 && port == 0; ++cand)
		if (start_peers(host, joiner, cand)) port = cand;
	if (!check(port != 0, "host/joiner peers established", ph)) {
		host.stop(); joiner.stop();
		return false;
	}
	for (int i = 0; i < 200 && !(host.getPeers().size() && host.getPeers()[0].is_authenticated); ++i) pump(host, joiner, arena, 1, 5);
	CHECK(host.getPeers().size() && host.getPeers()[0].is_authenticated, "the joiner is admitted", ph);
	const uint32_t cid = host.getPeers().empty() ? 0 : host.getPeers()[0].id;
	host.sendGameSetup(format_net_setup(n));
	joiner.sendGameSetup("map=x;seed=1;nsv=99"); // a client cannot send one
	joiner.sendSetupRequest("civ=3");
	joiner.sendReady(1, 7);
	pump(host, joiner, arena, 40, 5);
	auto got = joiner.takeGameSetups();
	CHECK(got.size() == 1 && parse_net_setup(got.empty() ? "" : got[0], back) && back.seq == 12, "the joiner gets the host's setup", ph);
	CHECK(joiner.takeGameSetups().empty(), "taken once", ph);
	CHECK(host.takeGameSetups().empty(), "the host ignores a client's setup", ph);
	auto reqs = host.takeSetupRequests();
	CHECK(reqs.size() == 1 && reqs[0].client == cid && reqs[0].request == "civ=3", "the host gets the request, stamped", ph);
	const NetPeer *p = host.getPeer(cid);
	CHECK(p && p->ready_status == 1 && p->ready_version == 7, "the host keeps the ready and its version", ph);
	host.sendSetupRefusal(cid, "Another player has that color");
	pump(host, joiner, arena, 40, 5);
	auto why = joiner.takeSetupRefusals();
	CHECK(why.size() == 1 && why[0] == "Another player has that color", "the joiner hears why it was refused", ph);
	CHECK(host.takeSetupRequests().empty(), "a refusal is no request", ph);
	host.clearAllReady();
	CHECK(p && p->ready_status == 0, "clear all ready", ph);
	joiner.sendReady(2, 7);
	pump(host, joiner, arena, 40, 5);
	host.clearAllReady();
	CHECK(p && p->ready_status == 2, "clear all ready keeps stay ready", ph);
	joiner.sendReady(0, 7);
	pump(host, joiner, arena, 40, 5);
	host.note_game_start_sent();
	joiner.sendSetupRequest("team=2");
	joiner.sendReady(1, 7);
	pump(host, joiner, arena, 40, 5);
	CHECK(host.takeSetupRequests().empty() && p && p->ready_status == 0, "nothing is taken once the game started", ph);
	host.stop();
	joiner.stop();
	return g_failures == 0;
}

// ---- Out-of-sync re-arms once the desynced client leaves ---------------------
// NetServerTurnManager::UninitialiseClient: the host reports a divergence once;
// after every out-of-sync client has left it checks again, so a second one is
// reported to everyone too.
static bool run_oos_rearm(const char *ph) {
	Refs ra;
	Simulation sa;
	build_world(sa, 4242, ra);
	NetHost h;
	NetConfig cfg;
	cfg.port = 0;
	if (!check(h.startHost(cfg), "a host to report from", ph)) return false;
	TurnManager ta;
	ta.set_net_host(&h, 0);
	ta.set_client_player(1, 1);
	ta.set_client_player(2, 2);
	ta.set_lockstep_peers({0, 1, 2});
	ta.mark_game_started(&sa);
	for (int t = 0; t < 4; ++t) {
		ta.receive_remote_turn_batch(ta.current_turn() + 1, 1, {});
		ta.receive_remote_turn_batch(ta.current_turn() + 1, 2, {});
		ta.tick(TurnManager::TURN_DURATION, sa);
	}
	const uint32_t t1 = ta.current_turn() - 1;
	ta.receive_sync_check(t1, 1, 0xDEADBEEF);
	if (!check(ta.has_out_of_sync() && ta.oos_turn() == t1, "client 1's wrong hash is reported", ph)) return false;
	ta.drop_client(1);
	CHECK(ta.has_out_of_sync() && ta.oos_turn() == t1, "the report stays readable after it left", ph);
	for (int t = 0; t < 2; ++t) {
		ta.receive_remote_turn_batch(ta.current_turn() + 1, 2, {});
		ta.tick(TurnManager::TURN_DURATION, sa);
	}
	ta.receive_sync_check(ta.current_turn() - 1, 2, 0xBADF00D);
	CHECK(ta.has_out_of_sync() && ta.oos_turn() > t1 && ta.oos_peers().size() == 1 && ta.oos_peers()[0] == 2,
	      "a later divergence is reported anew", ph);
	h.stop();
	return true;
}

int main(int argc, char **argv) {
	(void)argv;
	if (argc > 1) {
		std::fprintf(stdout, "usage: test_diplo_net\n");
		return 2;
	}
	std::fprintf(stdout, "[test_diplo_net] diplomacy over lockstep cmd 22 (two peers, one process)\n");
	struct Phase { const char *name; bool (*fn)(const char *); };
	const Phase phases[] = {
		{"wire contract: frozen ids, retired NMT_DIPLOMACY is dropped", run_wire_contract},
		{"cmd 22 on two lockstep peers: own rows, same turn, same hash", run_replicated_stance},
		{"negative control: one-sided stance diverges the hash", run_one_sided_negative_control},
		{"a client cannot set another player's row", run_client_cannot_forge_actor},
		{"G8 lockstep gate: no turn before NMT_GAME_START is applied", run_lockstep_gate},
		{"P3 late-join GAME_START resend + turn-0 hash handshake", run_late_join_game_start_resend},
		{"tribute (cmd 27) and barter (cmd 23) on two lockstep peers", run_tribute_and_barter},
		{"game setup: NMT_GAME_SETUP, setup requests and ready", run_game_setup_wire},
		{"out-of-sync re-arms after the desynced client leaves", run_oos_rearm},
	};
	const size_t n = sizeof(phases) / sizeof(phases[0]);
	for (size_t i = 0; i < n; ++i) {
		std::fprintf(stdout, "[phase] %s ... ", phases[i].name);
		std::fflush(stdout);
		const int before = g_failures;
		const bool ok = phases[i].fn(phases[i].name);
		std::fprintf(stdout, "%s\n", (ok && g_failures == before) ? "ok" : "FAILED");
	}
	if (g_failures) {
		std::fprintf(stdout, "RESULT: FAIL (%d failed check(s), %zu phase(s))\n", g_failures, n);
		return 1;
	}
	std::fprintf(stdout, "RESULT: PASS (%zu phase(s), 0 failed checks)\n", n);
	return 0;
}
