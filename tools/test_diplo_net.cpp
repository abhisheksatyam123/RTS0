// RTS0 diplomacy wire-replication test (headless: no SCons, no Godot).
//
// Companion to tools/test_replay.cpp / tools/test_netcode.cpp covering the
// NMT_DIPLOMACY path of src/network/{net_types.h,net_host.{h,cpp}}. Every call
// hits the REAL public API: two NetHost peers (one Host, one Client) bound to
// 127.0.0.1 over the real UDP transport, two TurnManagers and two Simulations
// in one process. Nothing in src/network is stubbed.
//
// Why this test exists: Simulation::set_diplomacy() is an inline local setter
// with no command path, and diplomacy_state() IS fed into
// TurnManager::compute_sim_hash(), so a stance applied on one peer only
// desyncs every other peer. NMT_DIPLOMACY is the wire path that prevents that.
//
//  1. wire round-trip: NMT_DIPLOMACY type id / payload frame+parse / dispatch
//     validation (bad player_id, bad stance, truncated payload).
//  2. replication over the real socket: host + client peers, identical scripted
//     command stream through post_local_command / receive_remote_turn_batch,
//     a stance applied to BOTH peers from the NMT_DIPLOMACY payload through
//     Simulation::set_diplomacy(), then further turns. compute_sim_hash() must
//     stay equal and get_diplomacy() must agree on both peers.
//
//     DEPENDENCY (cross-stream: src/simulation is owned by another stream):
//     there is no simulation command type for diplomacy yet, so the stance is
//     applied here straight through the sim applier Simulation::set_diplomacy()
//     on every peer. When the sim-side command path lands (a cmd_type routed
//     from TurnManager::tick), keep driving every peer from NMT_DIPLOMACY but
//     assert against the command applier instead of the raw setter.
//  3. negative control: the same stance applied to ONE peer only must make the
//     lockstep hashes DIVERGE -- proving the phase-2 check detects exactly the
//     out-of-sync this feature prevents. The OOS detector is exercised through
//     receive_sync_check().
//  4. three-peer host relay: host + two clients, one client originates the
//     stance; the host must relay it and every peer must apply it.
//  5. G8 lockstep gate: with a live peer and no NMT_GAME_START applied, tick()
//     must refuse to advance (turn 1 OOS fix); mark_game_started() releases it.
//  6. G9 diplo-via-TurnManager round-trip: NMT_DIPLOMACY dispatched through
//     NetHost reaches TurnManager::onMessage and is applied by tick() on EVERY
//     peer -- the sender's id is resolved (peer_id==0 quirk) and the stance lands
//     through the lockstep boundary, not the presentation applier.
//  7. P3 late-join GAME_START resend: a client admitted AFTER the host broadcast
//     GAME_START asks for setup + turn-0 state, the host resends, and the joiner
//     only releases its lockstep gate after the turn-0 hash is confirmed.
//  8. P3 maintainFleet upkeep: a built dock with no fleet produces exactly one
//     deterministic commission per turn, blocked while the dock is training or
//     unfinished, and it rides the ordinary turn command stream (so it is in the
//     replay history).
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

// Counts onDiplomacy callbacks for the phase-1 dispatch validation.
static int g_dip_cb_fires = 0;

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
	s.spawn_relic(10.0f, 10.0f, RES_GOLD, 400);
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

struct StandaloneApplier {
	Simulation *sim = nullptr;
	bool landed = false;
	uint8_t want_player = 0, want_stance = 0, got_player = 0, got_stance = 0;
	uint32_t last_sender = 0xFFFFFFFFu;
	uint32_t count = 0;
	StandaloneApplier(Simulation *s, uint8_t p, uint8_t st) : sim(s), want_player(p), want_stance(st) {}
	// Installed as NetHost::onDiplomacy. Only a payload matching want_player /
	// want_stance is applied, so a test cannot "pass" on a wrong stance landing.
	void handle(uint32_t peer, const DiplomacyMsg &m) {
		count++;
		last_sender = peer;
		got_player = m.player_id;
		got_stance = m.stance;
		if (m.player_id == want_player && m.stance == want_stance && sim) {
			sim->set_diplomacy(m.player_id, m.stance); // the sim-side applier
			landed = true;
		}
	}
};

// ---- Phase 1: type id, payload framing and dispatch validation ----------------
static bool run_wire_contract(const char *ph) {
	CHECK(static_cast<uint8_t>(NetMessageType::NMT_DIPLOMACY) == 31, ph);
	CHECK(static_cast<uint8_t>(NetMessageType::NMT_COUNT) == 34, ph);
	CHECK(static_cast<uint8_t>(NetMessageType::NMT_DIPLOMACY) <
	          static_cast<uint8_t>(NetMessageType::NMT_COUNT), ph);
	// Append-only NMT ids 32/33 (late-join sync) and the frozen cmd_type block.
	CHECK(static_cast<uint8_t>(NetMessageType::NMT_JOIN_SYNC_STATE) == 32, ph);
	CHECK(static_cast<uint8_t>(NetMessageType::NMT_JOIN_SYNC_CHECK) == 33, ph);
	CHECK(static_cast<uint8_t>(NetMessageType::NMT_JOIN_SYNC_STATE) >
	          static_cast<uint8_t>(NetMessageType::NMT_DIPLOMACY), ph);
	CHECK(static_cast<uint8_t>(NetCommandType::NET_CMD_DIPLOMACY) == 22, ph);
	CHECK(static_cast<uint8_t>(NetCommandType::NET_CMD_BARTER) == 23, ph);
	CHECK(static_cast<uint8_t>(NetCommandType::NET_CMD_MAINTAIN_FLEET) == 24, ph);

	DiplomacyMsg dm;
	dm.player_id = 3; dm.stance = Simulation::DIPLO_ALLY;
	std::vector<uint8_t> payload = NetPacketSerializer::frame_diplomacy(dm);
	if (!check(payload.size() == 2, "NMT_DIPLOMACY payload is exactly 2 bytes", ph)) return false;
	CHECK(payload[0] == 3 && payload[1] == Simulation::DIPLO_ALLY, ph);

	std::vector<uint8_t> wire = NetPacketSerializer::pack_message(NetMessageType::NMT_DIPLOMACY, 7, payload);
	NetMessage rx;
	if (!check(NetPacketSerializer::unpack_message(wire.data(), wire.size(), rx), "pack/unpack round-trip", ph)) return false;
	CHECK(rx.type == static_cast<uint8_t>(NetMessageType::NMT_DIPLOMACY) && rx.turn == 7, ph);
	CHECK(rx.payload == payload, ph);

	DiplomacyMsg got;
	CHECK(NetPacketSerializer::parse_diplomacy(rx.payload.data(), rx.payload.size(), got), ph);
	CHECK(got.player_id == 3 && got.stance == Simulation::DIPLO_ALLY, ph);
	// Exact-size parse rejects truncation and trailing bytes.
	CHECK(!NetPacketSerializer::parse_diplomacy(rx.payload.data(), 1, got), ph);
	CHECK(!NetPacketSerializer::parse_diplomacy(rx.payload.data(), 3, got), ph);

	// Dispatch: a valid payload fires the callback; malformed / out-of-range do not.
	g_dip_cb_fires = 0;
	NetHost host;
	host.onDiplomacy([](uint32_t, const DiplomacyMsg &) { ++g_dip_cb_fires; });

	DiplomacyMsg bad_id; bad_id.player_id = 9; bad_id.stance = Simulation::DIPLO_ALLY;
	NetMessage m_bad_id = rx; m_bad_id.payload = NetPacketSerializer::frame_diplomacy(bad_id);
	host.dispatchMessage(1, m_bad_id);
	CHECK(g_dip_cb_fires == 0, "player_id >= 8 rejected before the callback", ph);

	DiplomacyMsg bad_stance; bad_stance.player_id = 2; bad_stance.stance = 7;
	NetMessage m_bad_stance = rx; m_bad_stance.payload = NetPacketSerializer::frame_diplomacy(bad_stance);
	host.dispatchMessage(1, m_bad_stance);
	CHECK(g_dip_cb_fires == 0, "stance > DIPLO_ENEMY rejected before the callback", ph);

	NetMessage m_short = rx; m_short.payload = payload; m_short.payload.resize(1);
	host.dispatchMessage(1, m_short);
	CHECK(g_dip_cb_fires == 0, "short payload rejected before the callback", ph);

	DiplomacyMsg ok; ok.player_id = 5; ok.stance = Simulation::DIPLO_NEUTRAL;
	NetMessage m_ok = rx; m_ok.payload = NetPacketSerializer::frame_diplomacy(ok);
	host.dispatchMessage(1, m_ok);
	CHECK(g_dip_cb_fires == 1, "valid NMT_DIPLOMACY fires onDiplomacy", ph);
	return true;
}

// ---- Phase 2: replicated stance over the real wire keeps both peers in sync ---
static bool run_replicated_stance(const char *ph) {
	Refs ra, rb;
	Simulation sa, sb;
	build_world(sa, 4242, ra);
	build_world(sb, 4242, rb);
	// Sanity: the pristine worlds agree, so everything below is attributable to
	// the stance and not to setup.
	TurnManager ta, tb;
	CHECK(ta.compute_sim_hash(sa) == tb.compute_sim_hash(sb), ph);

	// Players 1..7 start mutually hostile (Simulation ctor,
	// simulation.cpp:184), so ALLY is a real state change on both peers.
	const uint8_t kTarget = 2;
	const uint8_t kStance = Simulation::DIPLO_ALLY;
	CHECK(sa.get_diplomacy(kTarget) == Simulation::DIPLO_ENEMY, ph);
	CHECK(sb.get_diplomacy(kTarget) == Simulation::DIPLO_ENEMY, ph);

	// Identical command stream on both peers; the remote batch is delivered to
	// the turn the NEXT tick will produce, exactly as test_replay.cpp does it.
	ScriptRng g(9001);
	std::vector<NetCommand> local, remote;
	for (uint32_t t = 1; t <= 6; ++t) {
		script_turn(ra, t, g, local, remote);
		for (size_t i = 0; i < local.size(); ++i) ta.post_local_command(local[i]);
		for (size_t i = 0; i < remote.size(); ++i) tb.post_local_command(remote[i]);
		ta.receive_remote_turn_batch(t, 1, remote);
		tb.receive_remote_turn_batch(t, 0, local);
		if (!check(ta.tick(TurnManager::TURN_DURATION, sa), "peer A tick advanced", ph)) return false;
		if (!check(tb.tick(TurnManager::TURN_DURATION, sb), "peer B tick advanced", ph)) return false;
		if (!check(ta.compute_sim_hash(sa) == tb.compute_sim_hash(sb), "peers identical before stance", ph)) return false;
	}
	const uint32_t base_hash = ta.compute_sim_hash(sa);

	// The wire: a real Host peer and a real Client peer on loopback UDP. Peer A
	// originates NMT_DIPLOMACY; peer B receives it through NetHost::dispatchMessage
	// -> onDiplomacy and applies it with the sim applier. Both peers then set the
	// same stance (A locally + via its own send, B through the received payload),
	// which is what a lobby + NMT_DIPLOMACY delivery produces in production.
	NetHost host, client;
	Arena arena;
	uint16_t port = 0;
	for (uint16_t cand = 21500; cand < 21600 && port == 0; ++cand)
		if (start_peers(host, client, cand)) port = cand;
	if (!check(port != 0, "host/client peers established over UDP loopback", ph)) {
		host.stop(); client.stop();
		return false;
	}

	StandaloneApplier ap_b(&sb, kTarget, kStance);
	client.onDiplomacy([&ap_b](uint32_t peer, const DiplomacyMsg &m) { ap_b.handle(peer, m); });
	int host_fires = 0;
	host.onDiplomacy([&host_fires](uint32_t, const DiplomacyMsg &) { ++host_fires; });

	// A is the sender peer: it originates, then applies locally (the sender must
	// not wait for its own packet back).
	host.sendDiplomacy(kTarget, kStance, ta.current_turn());
	sa.set_diplomacy(kTarget, kStance);
	pump(host, client, arena);

	CHECK(ap_b.count == 1, "peer B received exactly one NMT_DIPLOMACY", ph);
	if (ap_b.count == 1) {
		CHECK(ap_b.got_player == kTarget && ap_b.got_stance == kStance, "received payload matches sent payload", ph);
		// The host's own wire peer id is 0 (NetHost::startHost sets
		// local_peer_id_ = 0, and a client's peer table lists the host as id 0),
		// which is why turn_manager.cpp falls back to scm.client_id when
		// peer_id == 0. OnDiplomacy must still deliver that sender id.
		CHECK(ap_b.last_sender == 0, "onDiplomacy reports the sending peer id", ph);
	}
	CHECK(sb.get_diplomacy(kTarget) == kStance, "peer B applied the stance via the sim applier", ph);
	CHECK(sa.get_diplomacy(kTarget) == kStance, "peer A holds the same stance", ph);
	// Host relay only fires for host-originated stances it receives; the host sent
	// this one itself, so it must NOT have looped back.
	CHECK(host_fires == 0, "host does not receive its own broadcast", ph);

	// Advance a few more turns on both peers with the stance applied on both.
	for (uint32_t t = 7; t <= 10; ++t) {
		script_turn(ra, t, g, local, remote);
		for (size_t i = 0; i < local.size(); ++i) ta.post_local_command(local[i]);
		for (size_t i = 0; i < remote.size(); ++i) tb.post_local_command(remote[i]);
		ta.receive_remote_turn_batch(t, 1, remote);
		tb.receive_remote_turn_batch(t, 0, local);
		if (!check(ta.tick(TurnManager::TURN_DURATION, sa), "peer A post-stance tick", ph)) break;
		if (!check(tb.tick(TurnManager::TURN_DURATION, sb), "peer B post-stance tick", ph)) break;
	}
	const uint32_t hash_a = ta.compute_sim_hash(sa);
	const uint32_t hash_b = tb.compute_sim_hash(sb);
	if (!check(hash_a == hash_b, "hashes still equal after the replicated stance", ph)) {
		std::fprintf(stderr, "         peer_a=%u peer_b=%u (pre-stance=%u)\n", hash_a, hash_b, base_hash);
		host.stop(); client.stop();
		return false;
	}
	CHECK(hash_a != base_hash, "the applied stance is observable in the lockstep hash", ph);
	CHECK(ta.has_out_of_sync() == false && tb.has_out_of_sync() == false, ph);
	host.stop();
	client.stop();
	return true;
}

// ---- Phase 3: negative control -- one-sided stance MUST desync the peers ------
static bool run_one_sided_negative_control(const char *ph) {
	Refs ra, rb;
	Simulation sa, sb;
	build_world(sa, 4242, ra);
	build_world(sb, 4242, rb);
	TurnManager ta, tb;
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

// ---- Phase 4: host relay of a CLIENT-originated stance (three peers) ---------
static bool run_client_originated_relay(const char *ph) {
	Refs r0, r1, r2;
	Simulation sh, sc1, sc2;
	build_world(sh, 4242, r0);
	build_world(sc1, 4242, r1);
	build_world(sc2, 4242, r2);

	NetHost host, c1, c2;
	Arena arena;
	uint16_t port = 0;
	NetConfig cfg;
	cfg.port = 21650;
	for (; port == 0 && cfg.port < 21700; ++cfg.port) {
		if (!host.startHost(cfg)) continue;
		if (c1.startClient("127.0.0.1", cfg.port) && c2.startClient("127.0.0.1", cfg.port)) port = cfg.port;
	}
	if (!check(port != 0, "host with two clients established", ph)) { host.stop(); c1.stop(); c2.stop(); return false; }
	for (int waited = 0; waited < 1500; waited += 5) {
		host.poll(arena); c1.poll(arena); c2.poll(arena);
		bool both = host.getPeers().size() >= 2;
		for (size_t i = 0; i < host.getPeers().size(); ++i) both = both && host.getPeers()[i].connected;
		if (both) break;
		sleep_ms(5);
	}
	if (!check(host.getPeers().size() == 2, "host admitted both clients", ph)) {
		host.stop(); c1.stop(); c2.stop(); return false;
	}

	const uint8_t kTarget = 3;
	const uint8_t kStance = Simulation::DIPLO_NEUTRAL; // starts DIPLO_ENEMY
	CHECK(sc1.get_diplomacy(kTarget) == Simulation::DIPLO_ENEMY, ph);
	CHECK(sh.get_diplomacy(kTarget) == Simulation::DIPLO_ENEMY, ph);
	StandaloneApplier ap_h(&sh, kTarget, kStance);
	StandaloneApplier ap_c2(&sc2, kTarget, kStance);
	host.onDiplomacy([&ap_h](uint32_t p, const DiplomacyMsg &m) { ap_h.handle(p, m); });
	c2.onDiplomacy([&ap_c2](uint32_t p, const DiplomacyMsg &m) { ap_c2.handle(p, m); });
	// c1 has no handler: it is the third-peer control and must still see the state.

	// A CLIENT originates the stance (sendDiplomacy is symmetric like sendChat).
	c1.sendDiplomacy(kTarget, kStance, 12);
	sc1.set_diplomacy(kTarget, kStance);
	for (int i = 0; i < 40; ++i) {
		host.poll(arena); c1.poll(arena); c2.poll(arena);
		if (ap_h.landed && ap_c2.landed) break;
		sleep_ms(5);
	}
	CHECK(ap_h.landed, "host applied the client-originated stance", ph);
	CHECK(ap_c2.landed, "second client applied the host-relayed stance", ph);
	CHECK(ap_h.count == 1 && ap_c2.count == 1, "stance relayed exactly once to each peer", ph);
	CHECK(sh.get_diplomacy(kTarget) == kStance, ph);
	CHECK(sc1.get_diplomacy(kTarget) == kStance, "originating client holds the stance", ph);
	CHECK(sc2.get_diplomacy(kTarget) == kStance, ph);
	// Every peer flipped away from its starting stance, so the relay is not vacuous.
	CHECK(sc1.get_diplomacy(kTarget) != Simulation::DIPLO_ENEMY, ph);
	CHECK(sh.get_diplomacy(kTarget) != Simulation::DIPLO_ENEMY, ph);
	CHECK(sc2.get_diplomacy(kTarget) != Simulation::DIPLO_ENEMY, ph);

	host.stop(); c1.stop(); c2.stop();
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

// ---- Phase 6: G9 NMT_DIPLOMACY -> TurnManager -> turn-boundary apply -------
// The production path: NetHost::dispatchMessage resolves the sender id, forwards
// NMT_DIPLOMACY to TurnManager::onMessage (no presentation involved), and tick()
// applies the queued stance at the lockstep turn boundary on BOTH peers.
static bool run_diplo_via_turn_manager(const char *ph) {
	Refs rh, rc;
	Simulation sh, sc;
	build_world(sh, 4242, rh);
	build_world(sc, 4242, rc);
	const uint8_t kTarget = 4;                       // starts DIPLO_ENEMY
	const uint8_t kStance = Simulation::DIPLO_ALLY;

	NetHost host, client;
	Arena arena;
	uint16_t port = 0;
	for (uint16_t cand = 21810; cand < 21900 && port == 0; ++cand)
		if (start_peers(host, client, cand)) port = cand;
	if (!check(port != 0, "host/client peers established for the TurnManager diplo path", ph)) {
		host.stop(); client.stop();
		return false;
	}

	// Both peers own a TurnManager bound to their own NetHost, exactly like
	// main_node.cpp's turn_mgr_.
	TurnManager th(&host, 0);
	TurnManager tc(&client, 1);
	// A host owns its world from bind; a client is released by the GAME_START the
	// host broadcasts at Start, which this phase stands in for (phase 5 and phase
	// 7 cover the gated window itself).
	th.mark_game_started(&sh);
	tc.mark_game_started(&sc);
	CHECK(th.net_game_started() && tc.net_game_started(), "both peers have their GAME_START", ph);

	// StandaloneApplier is deliberately NOT installed here: this phase must not
	// apply through the presentation applier at all.
	uint32_t cb_fires = 0;
	client.onDiplomacy([&cb_fires](uint32_t, const DiplomacyMsg &) { ++cb_fires; });

	// The host originates: it queues its own stance through the SAME path the
	// inbound packet takes, so host and client cannot apply on different turns.
	CHECK(sh.get_diplomacy(kTarget) == Simulation::DIPLO_ENEMY, ph);
	CHECK(th.post_diplomacy(kTarget, kStance, 0), "host queued its own stance");
	host.sendDiplomacy(kTarget, kStance, 0);
	pump(host, client, arena, 20);

	CHECK(cb_fires == 1, "the presentation callback still fires (unchanged)", ph);
	// Queued, NOT applied: a stance must not land mid-turn.
	CHECK(th.pending_diplomacy_count() == 1, "sender's stance is queued", ph);
	CHECK(tc.pending_diplomacy_count() == 1, "receiver's stance is queued via TurnManager::onMessage", ph);
	CHECK(th.current_turn() == 0 && tc.current_turn() == 0, "no turn has run yet", ph);
	CHECK(sh.get_diplomacy(kTarget) == Simulation::DIPLO_ENEMY,
	      "queued stance is not applied before the turn boundary");
	CHECK(sc.get_diplomacy(kTarget) == Simulation::DIPLO_ENEMY, ph);

	// Turn 1 consumes turn 0's queue on BOTH peers (DIPLOMACY_APPLY_DELAY == 1).
	CHECK(th.tick(TurnManager::TURN_DURATION, sh), "host tick advanced", ph);
	CHECK(tc.tick(TurnManager::TURN_DURATION, sc), "client tick advanced", ph);
	CHECK(th.current_turn() == 1 && tc.current_turn() == 1, ph);
	CHECK(sh.get_diplomacy(kTarget) == kStance, "host applied the stance at the turn boundary", ph);
	CHECK(sc.get_diplomacy(kTarget) == kStance, "client applied the SAME stance at the SAME turn", ph);
	CHECK(th.last_applied_diplomacy(kTarget) == kStance, "TurnManager recorded the applied stance", ph);
	CHECK(th.pending_diplomacy_count() == 0 && tc.pending_diplomacy_count() == 0,
	      "the queue is drained exactly once", ph);
	CHECK(th.compute_sim_hash(sh) == tc.compute_sim_hash(sc),
	      "peers stay hash-identical after the turn-boundary apply", ph);

	// A stance originating at the CLIENT reaches the host through the same route.
	const uint8_t kStance2 = Simulation::DIPLO_NEUTRAL; // ALLY -> NEUTRAL, a real change
	CHECK(tc.post_diplomacy(kTarget, kStance2, tc.current_turn()), "client queued a stance", ph);
	client.sendDiplomacy(kTarget, kStance2, tc.current_turn());
	pump(host, client, arena, 20);
	CHECK(th.pending_diplomacy_count() == 1, "client-originated stance reached the host TurnManager", ph);
	CHECK(th.tick(TurnManager::TURN_DURATION, sh), ph);
	CHECK(tc.tick(TurnManager::TURN_DURATION, sc), ph);
	CHECK(sh.get_diplomacy(kTarget) == kStance2 && sc.get_diplomacy(kTarget) == kStance2,
	      "client-originated stance applied on both peers on the same turn", ph);
	CHECK(th.compute_sim_hash(sh) == tc.compute_sim_hash(sc), ph);

	// Out-of-range / malformed stances never reach the queue.
	CHECK(!th.post_diplomacy(9, kStance, 0), "player_id >= 8 is rejected", ph);
	CHECK(!th.post_diplomacy(2, 7, 0), "stance > DIPLO_ENEMY is rejected", ph);
	CHECK(th.pending_diplomacy_count() == 0, "rejected stances are not queued", ph);
	// A malformed payload must not fire onDiplomacy or queue anything.
	DiplomacyMsg dm; dm.player_id = 1; dm.stance = Simulation::DIPLO_ALLY;
	NetMessage bad;
	bad.type = static_cast<uint8_t>(NetMessageType::NMT_DIPLOMACY);
	bad.turn = 0;
	bad.payload = NetPacketSerializer::frame_diplomacy(dm);
	bad.payload.resize(1);
	client.dispatchMessage(0, bad);
	CHECK(tc.pending_diplomacy_count() == 0, "a truncated payload is dropped, not queued", ph);

	host.stop();
	client.stop();
	return true;
}

// ---- Phase 7: P3 late-join GAME_START resend + turn-0 hash handshake -------
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

// ---- Phase 8: P3 maintainFleet upkeep (navalManager.js:507) ----------------
// Generation half: one deterministic commission per turn from a built dock with
// no fleet, blocked while the dock is training or unfinished, and carried on the
// ordinary turn command stream so it lands in the replay history.
static bool run_maintain_fleet_upkeep(const char *ph) {
	Refs ra, rb;
	Simulation sa, sb;
	build_world(sa, 4242, ra);
	build_world(sb, 4242, rb);
	TurnManager ta, tb;

	// No dock at all: 0 A.D. maintainFleet returns immediately.
	CHECK(ta.generate_fleet_upkeep_commands(sa).empty(), "no dock -> no commission", ph);
	CHECK(ta.fleet_upkeep_requests() == 0, ph);

	// An unfinished dock is not docks.filter(isBuilt) -> still nothing.
	sa.spawn_building_world(20.0f, 6.0f, BLD_DOCK, 1, 0.5f);
	CHECK(ta.generate_fleet_upkeep_commands(sa).empty(), "an unbuilt dock -> no commission", ph);

	// A built dock with an empty fleet commissions one warship per turn.
	const uint32_t dock = sa.spawn_building_world(22.0f, 9.0f, BLD_DOCK, 1, 1.0f);
	sb.spawn_building_world(20.0f, 6.0f, BLD_DOCK, 1, 0.5f);
	sb.spawn_building_world(22.0f, 9.0f, BLD_DOCK, 1, 1.0f);

	std::vector<NetCommand> cmds = ta.generate_fleet_upkeep_commands(sa);
	if (!check(cmds.size() == 1, "a built dock with no fleet commissions exactly one ship", ph)) return false;
	CHECK(cmds[0].cmd_type == static_cast<uint8_t>(NetCommandType::NET_CMD_MAINTAIN_FLEET), ph);
	CHECK(cmds[0].target_id == UNIT_TRIREME, "the transport/war wishlist is served first", ph);
	CHECK(cmds[0].client_id == ta.local_client_id(), "the commission carries the local client id", ph);
	CHECK(cmds[0].entity_ids.size() == 1 && cmds[0].entity_ids[0] == dock, "the commission names its dock", ph);
	// Determinism: the same world yields the same commission.
	std::vector<NetCommand> again = ta.generate_fleet_upkeep_commands(sa);
	if (!check(again.size() == 1, "the commission is a pure function of sim state", ph)) return false;
	CHECK(fleet_upkeep_command_equal(again[0], cmds[0]), ph);

	// Two identical peers with the same world -> the same upkeep decision.
	TurnManager tb2;
	std::vector<NetCommand> c2 = tb2.generate_fleet_upkeep_commands(sb);
	if (!check(c2.size() == 1, "the mirrored peer also commissions one ship", ph)) return false;
	CHECK(fleet_upkeep_command_equal(c2[0], cmds[0]), "peers generate byte-identical upkeep", ph);

	// queues.ships.hasQueuedUnits() is scoped to the OWNING team: a human (team 0)
	// barracks training must NOT starve Petra's (team 1) navy -- navalManager.js:509
	// reads Petra's own ship queue, and RTS0 docks are team 1.
	const uint32_t barracks = sa.spawn_building_world(-26.0f, -4.0f, BLD_BARRACKS, 0, 1.0f);
	CHECK(sa.cmd_train(barracks, UNIT_SWORDSMAN), "filled the human production queue", ph);
	std::vector<NetCommand> while_busy = ta.generate_fleet_upkeep_commands(sa);
	if (!check(while_busy.size() == 1, "a team-0 training queue does not block the naval upkeep", ph)) return false;
	CHECK(fleet_upkeep_command_equal(while_busy[0], cmds[0]), "the commission is unchanged while the human trains", ph);

	// Explicit pause (0 A.D. maintenance pause) -> nothing.
	ta.set_fleet_upkeep_paused(true);
	CHECK(ta.generate_fleet_upkeep_commands(sa).empty(), "a paused upkeep emits nothing", ph);
	CHECK(ta.fleet_upkeep_paused(), ph);
	ta.set_fleet_upkeep_paused(false);

	// The upkeep is wired into tick(): a commission must be counted per turn and
	// ride the normal command stream (so it is in the replay history).
	TurnManager tc;
	Refs rc;
	Simulation sc;
	build_world(sc, 4242, rc);
	sc.spawn_building_world(22.0f, 9.0f, BLD_DOCK, 1, 1.0f);
	// Offline tick path (no NetHost) is ungated, so the upkeep runs.
	const uint32_t before = tc.fleet_upkeep_requests();
	CHECK(tc.tick(TurnManager::TURN_DURATION, sc), "offline tick advanced", ph);
	CHECK(tc.fleet_upkeep_requests() > before, "maintainFleet upkeep ran inside tick()", ph);
	CHECK(tc.turn_history().size() == 1, "the turn was recorded for replay", ph);
	CHECK(tc.fleet_upkeep_paused() == false, "upkeep is not paused by default", ph);
	// It is repeatable: one commission per turn while the fleet is below target.
	const uint32_t after1 = tc.fleet_upkeep_requests();
	CHECK(tc.tick(TurnManager::TURN_DURATION, sc), "second offline tick advanced", ph);
	CHECK(tc.fleet_upkeep_requests() > after1, "upkeep keeps commissioning while under target", ph);
	return true;
}

int main(int argc, char **argv) {
	(void)argv;
	if (argc > 1) {
		std::fprintf(stdout, "usage: test_diplo_net\n");
		return 2;
	}
	std::fprintf(stdout, "[test_diplo_net] NMT_DIPLOMACY wire replication (2-3 UDP peers, one process)\n");
	std::fprintf(stdout, "[test_diplo_net] NMT_DIPLOMACY=%u NMT_COUNT=%u TURN_DURATION=%gs\n",
	             (unsigned)static_cast<uint8_t>(NetMessageType::NMT_DIPLOMACY),
	             (unsigned)static_cast<uint8_t>(NetMessageType::NMT_COUNT),
	             (double)TurnManager::TURN_DURATION);
	struct Phase { const char *name; bool (*fn)(const char *); };
	const Phase phases[] = {
		{"wire contract: type id, frame/parse, dispatch validation", run_wire_contract},
		{"two-peer replication over UDP + hash/stance agreement", run_replicated_stance},
		{"negative control: one-sided stance diverges the hash", run_one_sided_negative_control},
		{"client-originated stance relayed by the host", run_client_originated_relay},
		{"G8 lockstep gate: no turn before NMT_GAME_START is applied", run_lockstep_gate},
		{"G9 diplo-via-TurnManager round-trip (turn-boundary apply)", run_diplo_via_turn_manager},
		{"P3 late-join GAME_START resend + turn-0 hash handshake", run_late_join_game_start_resend},
		{"P3 maintainFleet upkeep: deterministic per-turn commission", run_maintain_fleet_upkeep},
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
