// RTS0 replay round-trip test (headless: no SCons, no Godot, no NetHost).
// Companion to tools/test_netcode.cpp for the replay path of
// src/network/turn_manager.{h,cpp}. Every call hits the REAL public API; nothing
// in src/network is stubbed. Verify with:
//   g++ -std=c++20 -Isrc -fsyntax-only tools/test_replay.cpp
//
//  1. Two offline peers fed the SAME synthetic order stream through
//     post_local_command / post_remote_commands / receive_remote_turn_batch /
//     tick() stay bit-identical under compute_sim_hash(); reset() clears state.
//  2. save_replay -> validate_replay -> load_replay into a FRESH TurnManager +
//     FRESH Simulation reproduces every recorded per-turn hash, and the
//     replayed end state re-hashes to the hash captured before save.
//  3. Negative control: corrupting one stored post-turn hash must raise replay
//     OOS, proving check 2 is not vacuously true.
//  Exit: 0 = PASS, 1 = FAIL, 2 = bad usage.
#include "network/turn_manager.h"
#include "simulation/simulation.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
using namespace rtso;

static int g_failures = 0;
static bool check(bool cond, const char *expr, const char *phase) {
    if (cond) return true;
    std::fprintf(stderr, "\n  [FAIL] %s: %s\n", phase, expr);
    ++g_failures;
    return false;
}
#define CHECK(cond, phase) check((cond), #cond, (phase))

struct Refs { std::vector<uint32_t> villagers, soldiers, resources; };

// Identical construction for every peer: same sim seed, Petra AI config and
// spawn order. Any divergence here would void the hash comparison, so the live
// peer and every replay peer call this exactly once.
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

// Script RNG kept separate from Simulation::SimRng so building the order
// stream can never perturb sim state.
struct ScriptRng {
    uint32_t state;
    explicit ScriptRng(uint32_t seed) : state(seed ? seed : 1u) {}
    uint32_t next() { state = state * 1664525u + 1013904223u; return state; }
    float unit() { return (float)(next() & 0xFFFFFF) / 16777216.0f; }
};

// Turn N's orders: pure function of (refs, turn, rng state), so the live peer,
// the second live peer and the replay (which reads the batch back off disk)
// all execute byte-identical inputs.
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
    // Player 0 (local): gather + move + stance/formation churn.
    const uint32_t v = r.villagers[turn % r.villagers.size()];
    const uint32_t v2 = r.villagers[(turn + 3) % r.villagers.size()];
    push(local, 4 /*GATHER*/, v, r.resources[turn % r.resources.size()], 0.0f, 0.0f, 0, 0);
    push(local, 1 /*MOVE*/, v2, 0, -30.0f + rng.unit() * 12.0f, rng.unit() * 12.0f - 6.0f, 0, 0);
    push(local, 7 /*STANCE*/, v, 0, 0.0f, 0.0f, (uint8_t)(turn % 4), 0);
    push(local, 8 /*FORMATION*/, v2, 0, 0.0f, 0.0f, (uint8_t)(turn % 3), 0);
    // Player 1 (remote peer): attack-move + attack, plus a wall-drag build run.
    const uint32_t a = r.soldiers[turn % r.soldiers.size()];
    push(remote, 10 /*ATTACK_MOVE*/, a, 0, -20.0f + rng.unit() * 10.0f, rng.unit() * 10.0f - 5.0f, 0, 1);
    push(remote, 3 /*ATTACK*/, r.soldiers[(turn + 1) % r.soldiers.size()],
         r.soldiers[(turn + 2) % r.soldiers.size()], 0.0f, 0.0f, 0, 1);
    if (turn % 3 == 0)
        for (int k = 0; k < 3; ++k)
            push(remote, 5 /*BUILD*/, 0, 0, (float)(-4 + k), 8.0f, BLD_WALL_SEGMENT, 1);
}

// Drive n scripted turns into (tm, sim) through the real submit/advance API.
static bool record_turns(TurnManager &tm, Simulation &sim, const Refs &r, uint32_t n, const char *ph) {
    ScriptRng g(9001);
    std::vector<NetCommand> local, remote;
    for (uint32_t t = 1; t <= n; ++t) {
        script_turn(r, t, g, local, remote);
        for (size_t i = 0; i < local.size(); ++i) tm.post_local_command(local[i]);
        tm.receive_remote_turn_batch(t, 1, remote);
        if (!check(tm.tick(TurnManager::TURN_DURATION, sim), "tick advanced", ph)) return false;
    }
    return true;
}

static bool same_cmd(const NetCommand &a, const NetCommand &b) {
    return a.client_id == b.client_id && a.cmd_type == b.cmd_type &&
           a.target_id == b.target_id && a.param == b.param &&
           a.target_x == b.target_x && a.target_z == b.target_z &&
           a.entity_ids == b.entity_ids;
}

// The replay file is opaque binary outside src/network, so negative controls
// corrupt a copy at a known byte offset, never the original. v2 layout:
// magic(4) version(4) count(4), then frame0 = turn_index(4) + hash(4).
static const size_t kMagicOffset = 0, kFrame0HashOffset = 16;
static bool tamper_copy(const std::string &src, const std::string &dst, size_t off) {
    std::ifstream f(src, std::ios::binary);
    if (!f) return false;
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (off >= b.size()) return false;
    b[off] = (unsigned char)(b[off] ^ 0x01u);
    std::ofstream o(dst, std::ios::binary);
    if (!o) return false;
    o.write((const char *)b.data(), (std::streamsize)b.size());
    return o.good();
}
static long long fsize(const std::string &p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    return f ? (long long)f.tellg() : -1;
}

// Phase 1 -- two offline peers, real submit/advance API, hash agreement + reset.
static bool run_two_peer_hashes(const char *ph) {
    Refs ra, rb; Simulation sa, sb;
    build_world(sa, 4242, ra); build_world(sb, 4242, rb);
    TurnManager ta;              // real default (offline) ctor
    TurnManager tb(nullptr, 0);  // real TurnManager(NetHost*, client_id) ctor
    tb.set_local_client_id(0);
    CHECK(ta.local_client_id() == 0 && tb.local_client_id() == 0, ph);
    CHECK(ta.get_net_host() == nullptr && ta.current_turn() == 0, ph);
    CHECK(ta.is_replaying() == false && ta.turn_history().empty(), ph);

    // A sub-turn dt must not consume a turn (lockstep pacing sanity). Run this on
    // a throwaway pair: it advances a turn counter, and receive_remote_turn_batch
    // addresses its batch by the turn the NEXT tick will produce.
    {
        Simulation ts0;
        TurnManager tm0;
        CHECK(tm0.tick(TurnManager::TURN_DURATION * 0.5f, ts0) == false, ph);
        CHECK(tm0.current_turn() == 0, ph);
        CHECK(tm0.tick(TurnManager::TURN_DURATION * 0.5f, ts0) == true, ph);
        CHECK(tm0.current_turn() == 1 && tm0.turn_history().size() == 1, ph);
    }

    ScriptRng g(9001);
    std::vector<NetCommand> local, remote;
    for (uint32_t t = 1; t <= 6; ++t) {
        script_turn(ra, t, g, local, remote);
        for (size_t i = 0; i < local.size(); ++i) ta.post_local_command(local[i]);
        // A receives peer 1 via the turn-addressed batch path, B via the
        // immediate path: both deliver the same batch to the same turn frame.
        // Both must land while current_turn() == t-1, i.e. before the tick that
        // produces frame t, or the batch is never executed.
        ta.receive_remote_turn_batch(t, 1, remote);
        tb.post_remote_commands(1, remote);
        // The inbound copy for the other peer is what a real NMT_SIMULATION_COMMAND
        // would deliver. post_remote_* stamps the sender, so peer B must be told
        // explicitly to submit player 0's batch as its own -- as the live path in
        // turn_manager.cpp also re-stamps local cmds with local_client_id_ -- or
        // the two frames hold different order streams and (correctly) diverge.
        tb.post_remote_commands(0, local);
        CHECK(ta.tick(TurnManager::TURN_DURATION, sa), ph);
        CHECK(tb.tick(TurnManager::TURN_DURATION, sb), ph);
        CHECK(ta.current_turn() == t && tb.current_turn() == t, ph);
        const uint32_t ha = ta.compute_sim_hash(sa), hb = tb.compute_sim_hash(sb);
        if (!check(ha == hb, "peers must hash identically", ph)) {
            std::fprintf(stderr, "         turn=%u peer_a=%u peer_b=%u\n", t, ha, hb);
            return false;
        }
        const std::vector<TurnFrame> &h = ta.turn_history();
        if (!check(h.size() == t && h.back().turn_index == t, "one recorded frame per turn", ph)) return false;
        if (!check(h.back().sim_state_hash == ha, "frame hash == live hash", ph)) return false;
        if (!check(!h.back().commands.empty(), "frame carries commands", ph)) return false;
        for (size_t i = 0; i < h.back().commands.size(); ++i)
            if (!check(h.back().commands[i].client_id <= 1, "command client_id stamped", ph)) return false;
    }
    CHECK(ta.turn_history().size() == 6, ph);
    CHECK(ta.has_out_of_sync() == false && ta.oos_peers().empty(), ph);

    // reset() clears live bookkeeping; configured client identity survives it.
    TurnManager tc(nullptr, 7);
    tc.set_local_client_id(7);
    if (!record_turns(tc, sb, rb, 2, ph)) return false;
    tc.reset();
    CHECK(tc.current_turn() == 0 && tc.turn_history().empty(), ph);
    CHECK(tc.is_replaying() == false && tc.replay_complete() == false, ph);
    CHECK(tc.has_out_of_sync() == false && tc.last_replay_error().empty(), ph);
    CHECK(tc.local_client_id() == 7 && tc.get_net_host() == nullptr, ph);
    return true;
}

// Phase 2 -- save_replay / validate_replay / load_replay round-trip.
static bool run_round_trip(const char *ph) {
    const std::string path = "/tmp/test_replay_round_trip.bin";
    const uint32_t seed = 4242, kTurns = 8;
    Refs refs; Simulation sim;
    build_world(sim, seed, refs);
    TurnManager rec;
    if (!record_turns(rec, sim, refs, kTurns, ph)) return false;

    // Hash-before: live post-final-turn state, captured BEFORE serialization.
    const uint32_t hash_before = rec.compute_sim_hash(sim);
    const size_t frames = rec.turn_history().size();
    if (!check(frames == kTurns && hash_before != 0u, "recorded 8 frames, non-zero hash", ph)) return false;

    CHECK(rec.save_replay(path), ph);
    CHECK(fsize(path) > 12, ph); // magic + version + frame count minimum
    std::string verr;
    CHECK(rec.validate_replay(path, verr) && verr.empty(), ph);

    // Negative control: a flipped magic byte must be rejected with an error.
    const std::string bad = path + ".badmagic";
    if (check(tamper_copy(path, bad, kMagicOffset), "write bad-magic copy", ph)) {
        std::string berr;
        CHECK(!rec.validate_replay(bad, berr), ph);
        CHECK(!berr.empty(), ph);
        std::remove(bad.c_str());
    }

    // Deserialize into a FRESH TurnManager + FRESH Simulation.
    Simulation rsim; build_world(rsim, seed, refs);
    TurnManager rp;
    CHECK(rp.is_replaying() == false, ph);
    CHECK(rp.load_replay(path), ph);
    CHECK(rp.is_replaying() == true && rp.last_replay_error().empty(), ph);
    CHECK(rp.current_turn() == 0, ph); // load rewinds the turn counter
    CHECK(rp.replay_complete() == false && rp.replay_out_of_sync() == false, ph);
    CHECK(rp.turn_history().size() == frames, ph);

    // Every recorded command must survive the round-trip field-for-field.
    const std::vector<TurnFrame> &bf = rec.turn_history(), &af = rp.turn_history();
    for (size_t i = 0; i < frames; ++i) {
        if (!check(bf[i].turn_index == af[i].turn_index && bf[i].sim_state_hash == af[i].sim_state_hash,
                   "frame turn_index + sim_state_hash survive", ph)) return false;
        if (!check(bf[i].commands.size() == af[i].commands.size(), "command count survives", ph)) return false;
        for (size_t j = 0; j < bf[i].commands.size(); ++j)
            if (!check(same_cmd(bf[i].commands[j], af[i].commands[j]),
                       "NetCommand fields survive round-trip", ph)) return false;
    }

    // Playback: every tick re-runs the recorded batch and must reproduce the
    // recorded post-turn hash exactly.
    for (uint32_t t = 0; t < kTurns; ++t) {
        const uint32_t want = bf[t].sim_state_hash;
        if (!check(rp.tick(TurnManager::TURN_DURATION, rsim), "replay tick advanced", ph)) return false;
        const uint32_t got = rp.replay_computed_hash();
        if (!check(rp.replay_expected_hash() == want, "replay_expected_hash tracks frame", ph)) return false;
        if (!check(got == rp.compute_sim_hash(rsim), "frame hash == re-hash of replayed sim", ph)) return false;
        if (!check(got == want, "replayed hash == recorded hash", ph)) {
            std::fprintf(stderr, "         turn=%u recorded=%u replayed=%u\n", t + 1, want, got);
            return false;
        }
        CHECK(rp.current_turn() == t + 1, ph);
        CHECK(rp.replay_out_of_sync() == false && rp.replay_out_of_sync_turn() == 0, ph);
    }
    CHECK(rp.replay_complete() == true, ph);

    // Hash-after: THE round-trip assertion.
    const uint32_t hash_after = rp.compute_sim_hash(rsim);
    if (!check(hash_after == hash_before, "re-hash after load == hash before save", ph)) {
        std::fprintf(stderr, "         hash_before=%u hash_after=%u\n", hash_before, hash_after);
        return false;
    }
    CHECK(rec.compute_sim_hash(sim) == hash_after, ph);

    // Terminal state: a loaded replay never falls back to live.
    CHECK(rp.is_replaying() == true && rp.tick(TurnManager::TURN_DURATION, rsim), ph);
    CHECK(rp.replay_complete() == true && rp.replay_out_of_sync() == false, ph);
    CHECK(rp.turn_history().size() == frames, ph); // playback does not record
    std::remove(path.c_str());
    return true;
}

// Phase 3 -- OOS negative control: corrupting one stored post-turn hash must
// raise replay OOS, proving phase 2's equality check is not vacuously true.
static bool run_oos_control(const char *ph) {
    const std::string path = "/tmp/test_replay_oos.bin", tp = path + ".tamper";
    Refs refs; Simulation sim;
    build_world(sim, 4242, refs);
    TurnManager rec;
    if (!record_turns(rec, sim, refs, 4, ph)) return false;
    CHECK(rec.save_replay(path), ph);
    const uint32_t real0 = rec.turn_history().front().sim_state_hash;
    if (!check(real0 != 0u, "recorded first-turn hash is non-zero", ph)) return false;
    if (!check(tamper_copy(path, tp, kFrame0HashOffset), "write tampered-hash copy", ph)) return false;

    std::string berr;
    CHECK(rec.validate_replay(tp, berr) && berr.empty(), ph); // still valid structurally
    Simulation ts; build_world(ts, 4242, refs);
    TurnManager tam;
    CHECK(tam.load_replay(tp), ph);
    CHECK(tam.turn_history().front().sim_state_hash != real0, ph);
    tam.tick(TurnManager::TURN_DURATION, ts);
    if (!check(tam.replay_out_of_sync() == true, "tampered stored hash must raise replay OOS", ph)) {
        std::remove(tp.c_str()); std::remove(path.c_str());
        return false;
    }
    CHECK(tam.replay_out_of_sync_turn() == 1, ph);
    CHECK(tam.replay_expected_hash() != tam.replay_computed_hash(), ph);
    CHECK(tam.replay_computed_hash() == real0, ph); // the sim itself replayed fine
    std::remove(tp.c_str()); std::remove(path.c_str());
    return true;
}

// Phase 4 -- a rejoin in a multiplayer replay: client 12 (a new id, as a rejoiner
// gets) takes player 1 by the host's NET_CMD_PEER_JOINED in client 0's batch, then
// orders player 1's unit. Playback must seat it as the live game did; without the
// seat change its order goes nowhere.
static bool run_rejoin_seat(const char *ph) {
    const std::string path = "/tmp/test_replay_rejoin.bin";
    auto write = [&](bool with_join, uint32_t unit) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char *>(&v), 4); };
        auto u8 = [&](uint8_t v) { out.write(reinterpret_cast<const char *>(&v), 1); };
        auto f32 = [&](float v) { out.write(reinterpret_cast<const char *>(&v), 4); };
        u32(0x52545330); u32(3); u32(4);
        // turn 1: the seat change (target_x = the first turn it plays, target_id = client, param = seat)
        u32(1); u32(0); u32(with_join ? 1 : 0);
        if (with_join) { u32(0); u8((uint8_t)NetCommandType::NET_CMD_PEER_JOINED); f32(2.0f); f32(1.0f); u32(12); u8(1); u32(0); u32(0); }
        // turn 2: the rejoiner walks player 1's unit
        u32(2); u32(0); u32(1);
        u32(12); u8(1); f32(30.0f); f32(0.0f); u32(0); u8(0); u32(1); u32(unit); u32(0);
        u32(3); u32(0); u32(0);
        u32(4); u32(0); u32(0);
    };
    auto moved = [&](bool with_join) -> float {
        Simulation s;
        s.set_sim_seed(7);
        s.reset();
        const uint32_t id = s.spawn_unit(0.0f, 0.0f, 1, UNIT_SWORDSMAN, 0.0f);
        write(with_join, id);
        TurnManager tm;
        if (!tm.load_replay(path)) return -1.0f;
        for (int t = 0; t < 4; ++t) tm.tick(TurnManager::TURN_DURATION, s);
        auto uv = s.units();
        for (size_t i = 0; i < uv.count; ++i)
            if (uv.id[i] == id) return uv.pos_x[i];
        return -1.0f;
    };
    const float with_join = moved(true), without = moved(false);
    std::remove(path.c_str());
    CHECK(with_join > 0.2f, ph);
    CHECK(std::fabs(without) < 0.01f, ph);
    return with_join > 0.2f && std::fabs(without) < 0.01f;
}

int main(int argc, char **argv) {
    (void)argv;
    if (argc > 2) {
        std::fprintf(stdout, "usage: test_replay\n");
        return 2;
    }
    std::fprintf(stdout, "[test_replay] TurnManager replay round-trip (offline, no NetHost)\n");
    std::fprintf(stdout, "[test_replay] TURN_DURATION=%gs TICKS_PER_TURN=%d REPLAY_MAX_FILE_BYTES=%zu\n",
                 (double)TurnManager::TURN_DURATION, TurnManager::TICKS_PER_TURN,
                 (size_t)TurnManager::REPLAY_MAX_FILE_BYTES);
    struct Phase { const char *name; bool (*fn)(const char *); };
    const Phase phases[] = {
        {"two-peer hash agreement (submit/advance API) + reset", run_two_peer_hashes},
        {"save/validate/load round-trip + re-hash equality", run_round_trip},
        {"replay out-of-sync negative control", run_oos_control},
        {"a rejoiner's seat in a multiplayer replay", run_rejoin_seat},
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
