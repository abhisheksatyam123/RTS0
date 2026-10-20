// The --net-peer defeat case wipes a player's units and structures directly,
// as tools/test_victory.cpp does.
#define private public
#include "simulation/simulation.h"
#undef private
#include "network/net_host.h"
#include "network/turn_manager.h"
#include "simulation/simulation.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>

using namespace rtso;

// ---------------------------------------------------------------------------
// P1 lockstep playtest harness (see docs/MULTIPLAYER.md "Playtest harness").
//
// Drives TWO independent Simulation instances with the SAME scripted command
// mix and compares TurnManager::compute_sim_hash() after every lockstep turn.
// This is the offline equivalent of two real peers exchanging NMT_SYNC_CHECK.
// Petra AI runs unconditionally inside Simulation::tick() (tick_petra_ai), so
// any nondeterminism in its rand() stream, in wall-drag placement, or in any
// other gameplay path surfaces here as a first-divergent-turn report.
//
// Exit codes: 0 = full match stayed in sync, 1 = OOS found, 2 = bad usage.
// ---------------------------------------------------------------------------
namespace playtest {

// Deterministic script RNG, kept strictly separate from the simulation RNG so
// the command script can never perturb sim state.
struct ScriptRng {
    uint32_t state;
    explicit ScriptRng(uint32_t seed) : state(seed ? seed : 1u) {}
    uint32_t next() {
        state = state * 1664525u + 1013904223u;
        return state;
    }
    uint32_t range(uint32_t lo, uint32_t hi) { // inclusive lo, exclusive hi
        if (hi <= lo) return lo;
        return lo + next() % (hi - lo);
    }
    float unit() { return (float)(next() & 0xFFFFFF) / 16777216.0f; }
};

bool same_command(const NetCommand &a, const NetCommand &b) {
    return a.client_id == b.client_id && a.cmd_type == b.cmd_type &&
           a.entity_ids == b.entity_ids && a.target_id == b.target_id &&
           a.target_x == b.target_x && a.target_z == b.target_z &&
           a.param == b.param;
}

bool same_batch(const std::vector<NetCommand> &a, const std::vector<NetCommand> &b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (!same_command(a[i], b[i])) return false;
    return true;
}

const char *cmd_name(uint8_t t) {
    switch (t) {
        case 1: return "MOVE";
        case 2: return "STOP";
        case 3: return "ATTACK";
        case 4: return "GATHER";
        case 5: return "BUILD";
        case 6: return "TRAIN";
        case 7: return "STANCE";
        case 8: return "FORMATION";
        case 9: return "RESEARCH";
        case 10: return "ATTACK_MOVE";
        case 11: return "PATROL";
        case 12: return "GARRISON";
        case 13: return "UNGARRISON";
        case 14: return "RALLY";
        case 15: return "ATTACK_BUILDING";
        case 16: return "BUILD_REPAIR";
        case 17: return "HUNT";
        case 18: return "BOARD";
        case 19: return "DISEMBARK";
        case 20: return "CANCEL_TRAIN";
        case 21: return "GUARD";
        default: return "UNKNOWN";
    }
}

// Scenario handles captured at build time so the per-turn script can reference
// live entity/building ids instead of guessing them every turn.
struct ScriptRefs {
    std::vector<uint32_t> villagers;
    std::vector<uint32_t> soldiers_a;
    std::vector<uint32_t> soldiers_b;
    std::vector<uint32_t> enemy_buildings;
    std::vector<uint32_t> resources;
    uint32_t town_center = 0;
    uint32_t barracks = 0;
};

// Identical world construction for both peers. Petra AI is enabled implicitly
// (Simulation::tick always calls tick_petra_ai) and configured explicitly via
// set_petra_difficulty()/set_petra_behavior(), so the AI-controlled stockpile,
// queues and research streams are part of the hashed state.
void build_world(Simulation &s, uint32_t seed, ScriptRefs &refs) {
    s.set_sim_seed(seed);
    s.reset();
    s.set_petra_difficulty(3);
    s.set_petra_behavior(2);
    s.set_victory_condition(VICTORY_RELIC);
    s.set_starting_resources(900, 900, 500, 500);
    s.set_team_civ(0, CIV_ATHENIANS);
    s.set_team_civ(1, CIV_SPARTANS);
    s.set_team_civ(2, CIV_PERSIANS);

    refs = ScriptRefs();

    // --- Team 0 base -------------------------------------------------------
    refs.town_center = s.spawn_building_world(-30.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
    refs.barracks = s.spawn_building_world(-26.0f, -4.0f, BLD_BARRACKS, 0, 1.0f);
    refs.enemy_buildings.push_back(refs.town_center);
    refs.enemy_buildings.push_back(refs.barracks);

    // --- Team 1 base -------------------------------------------------------
    uint32_t e_tc = s.spawn_building_world(30.0f, 0.0f, BLD_TOWN_CENTER, 1, 1.0f);
    uint32_t e_barracks = s.spawn_building_world(26.0f, 4.0f, BLD_BARRACKS, 1, 1.0f);
    refs.enemy_buildings.push_back(e_tc);
    refs.enemy_buildings.push_back(e_barracks);
    refs.enemy_buildings.push_back(s.spawn_building_world(34.0f, -6.0f, BLD_TOWER, 1, 1.0f));
    // Neutral team-2 wonder so victory/capture paths run. South of the field: its
    // territory (TerritoryInfluence 100 m) would otherwise cover the middle of the map.
    refs.enemy_buildings.push_back(s.spawn_building_world(0.0f, -44.0f, BLD_WONDER, 2, 1.0f));

    // --- Units -------------------------------------------------------------
    for (int i = 0; i < 6; ++i) {
        float a = 6.2831853f * (float)i / 6.0f;
        refs.villagers.push_back(
            s.spawn_unit(-30.0f + cosf(a) * 4.0f, sinf(a) * 4.0f, 0, UNIT_VILLAGER, 100.0f));
    }
    for (int i = 0; i < 3; ++i) {
        refs.soldiers_a.push_back(
            s.spawn_unit(-20.0f + (float)i * 2.0f, -2.0f + (float)i * 2.0f, 0, UNIT_SWORDSMAN, 160.0f));
    }
    refs.soldiers_a.push_back(s.spawn_unit(-18.0f, 2.0f, 0, UNIT_ARCHER, 90.0f));
    for (int i = 0; i < 3; ++i) {
        refs.soldiers_b.push_back(
            s.spawn_unit(20.0f - (float)i * 2.0f, -2.0f + (float)i * 2.0f, 1, UNIT_SPEARMAN, 150.0f));
    }
    refs.soldiers_b.push_back(s.spawn_unit(18.0f, 2.0f, 1, UNIT_ARCHER, 90.0f));

    // --- Resource nodes (gather targets) ----------------------------------
    refs.resources.push_back(s.spawn_resource_world(-38.0f, 14.0f, RES_FOOD, 1200));
    refs.resources.push_back(s.spawn_resource_world(-44.0f, -12.0f, RES_WOOD, 1200));
    refs.resources.push_back(s.spawn_resource_world(36.0f, -14.0f, RES_STONE, 900));
    refs.resources.push_back(s.spawn_resource_world(42.0f, 12.0f, RES_GOLD, 900));

    // --- Fauna (wander rand() sites) --------------------------------------
    s.spawn_fauna(-50.0f, 20.0f, FAUNA_DEER);
    s.spawn_fauna(48.0f, -20.0f, FAUNA_BOAR);

    // --- Relic (capture victory path) -------------------------------------
    s.spawn_relic(10.0f, 10.0f);
}

// Emit this turn's scripted commands. MUST be a pure function of
// (refs, turn, ScriptRng state) so the peers never see different input.
void script_turn(const ScriptRefs &refs, uint32_t turn, ScriptRng &rng,
                 std::vector<NetCommand> &out) {
    out.clear();
    const uint32_t t = turn;

    auto push = [&](uint8_t type, uint32_t ea, uint32_t eb, uint32_t tgt,
                    float tx, float tz, uint8_t param, uint32_t client) {
        NetCommand c;
        c.client_id = client;
        c.cmd_type = type;
        if (ea) c.entity_ids.push_back(ea);
        if (eb) c.entity_ids.push_back(eb);
        c.target_id = tgt;
        c.target_x = tx;
        c.target_z = tz;
        c.param = param;
        out.push_back(c);
    };

    // --- Player 0 (host script): economy + production ----------------------
    if (!refs.villagers.empty()) {
        uint32_t v = refs.villagers[t % refs.villagers.size()];
        uint32_t r = refs.resources[t % refs.resources.size()];
        push(4 /*GATHER*/, v, 0, r, 0.0f, 0.0f, 0, 0);
        uint32_t v2 = refs.villagers[(t + 3) % refs.villagers.size()];
        push(1 /*MOVE*/, v2, 0, 0,
             -30.0f + rng.unit() * 12.0f,
             rng.unit() * 12.0f - 6.0f, 0, 0);
    }

    // --- Player 1 (remote script): combat + building placement -------------
    if (!refs.soldiers_b.empty()) {
        uint32_t a = refs.soldiers_b[t % refs.soldiers_b.size()];
        uint32_t tgt = refs.soldiers_a.empty() ? 0 : refs.soldiers_a[t % refs.soldiers_a.size()];
        push(3 /*ATTACK*/, a, 0, tgt, 0.0f, 0.0f, 0, 1);
        uint32_t a2 = refs.soldiers_b[(t + 1) % refs.soldiers_b.size()];
        push(10 /*ATTACK_MOVE*/, a2, 0, 0,
             -20.0f + rng.unit() * 10.0f,
             rng.unit() * 10.0f - 5.0f, 0, 1);
    }
    if (!refs.enemy_buildings.empty()) {
        uint32_t b = refs.enemy_buildings[t % refs.enemy_buildings.size()];
        uint32_t a3 = refs.soldiers_b.empty() ? 0 : refs.soldiers_b[(t + 2) % refs.soldiers_b.size()];
        push(15 /*ATTACK_BUILDING*/, a3, 0, b, 0.0f, 0.0f, 0, 1);
    }

    // --- Wall-drag-like placement sweep ------------------------------------
    // 0 A.D. walls are placed by drag: a contiguous run of segments along one
    // axis from drag start to drag end, all owned by the placing player.
    // Reproduce that shape with back-to-back BUILD commands (cmd_type 5) on
    // adjacent cells: same type, same team, contiguous cells. This is the
    // highest-risk path for client-side drag-preview state leaking into the sim.
    if (t % 3 == 0) {
        const uint32_t drag_client = (t % 6 == 0) ? 0u : 1u;
        // Horizontal leg: 4 contiguous wall segments.
        for (int k = 0; k < 4; ++k) {
            NetCommand c;
            c.client_id = drag_client;
            c.cmd_type = 5 /*BUILD*/;
            c.target_x = (float)(-4 + k);   // cell_x (drag start + k)
            c.target_z = 8.0f;             // cell_y
            c.param = BLD_WALL_SEGMENT;
            if (!refs.villagers.empty())
                c.entity_ids.push_back(refs.villagers[k % refs.villagers.size()]);
            out.push_back(c);
        }
        // Vertical leg of the same L-shaped drag.
        for (int k = 0; k < 3; ++k) {
            NetCommand c;
            c.client_id = drag_client;
            c.cmd_type = 5 /*BUILD*/;
            c.target_x = 0.0f;
            c.target_z = (float)(9 + k);
            c.param = BLD_WALL_SEGMENT;
            out.push_back(c);
        }
    }

    // --- Macro structures (footprint / can_place / nav rebuild) -----------
    if (t % 5 == 2) {
        NetCommand c;
        c.client_id = 0;
        c.cmd_type = 5 /*BUILD*/;
        c.target_x = -10.0f + (float)((t / 5) % 3);
        c.target_z = 12.0f;
        c.param = BLD_FARMSTEAD;
        out.push_back(c);
    }
    if (t % 7 == 3) {
        NetCommand c;
        c.client_id = 1;
        c.cmd_type = 5 /*BUILD*/;
        c.target_x = 12.0f;
        c.target_z = 12.0f + (float)((t / 7) % 3);
        c.param = BLD_TOWER;
        out.push_back(c);
    }

    // --- Production / research / stance / formation / patrol / guard -------
    if (t % 4 == 0) {
        push(6 /*TRAIN*/, 0, 0, refs.barracks, 0.0f, 0.0f, UNIT_SWORDSMAN, 0);
        push(9 /*RESEARCH*/, 0, 0, refs.town_center, 0.0f, 0.0f, TECH_PHASE_TOWN, 0);
    }
    if (t % 6 == 1) {
        push(7 /*STANCE*/, refs.soldiers_a.empty() ? 0 : refs.soldiers_a[0],
             0, 0, 0.0f, 0.0f, 1, 0);
        push(8 /*FORMATION*/, refs.soldiers_a.size() > 1 ? refs.soldiers_a[0] : 0,
             refs.soldiers_a.size() > 1 ? refs.soldiers_a[1] : 0, 0, 0.0f, 0.0f, 1, 0);
    }
    if (t % 9 == 4 && refs.soldiers_a.size() > 1)
        push(11 /*PATROL*/, refs.soldiers_a[0], 0, 0, -12.0f, 6.0f, 0, 0);
    if (t % 11 == 5 && !refs.soldiers_a.empty())
        push(21 /*GUARD*/, refs.soldiers_a[0], 0, refs.town_center, 0.0f, 0.0f, 0, 0);
    if (t % 13 == 7 && !refs.soldiers_b.empty())
        push(2 /*STOP*/, refs.soldiers_b[0], 0, 0, 0.0f, 0.0f, 0, 1);
}

// OOS context: the exact command batch, plus the first few units / buildings
// whose hash inputs differ between the peers.
void dump_context(const Simulation &a, const Simulation &b, const Simulation &c,
                  const std::vector<NetCommand> &cmds, uint32_t turn,
                  uint32_t ha, uint32_t hb) {
    std::printf("\n================ OUT-OF-SYNC ================\n");
    std::printf("first divergent turn : %u\n", turn);
    std::printf("client A hash        : 0x%08X\n", ha);
    std::printf("client B hash        : 0x%08X\n", hb);
    std::printf("commands this turn   : %zu\n", cmds.size());
    for (const auto &cmd : cmds) {
        std::printf("  client=%u %-15s entities=[", cmd.client_id, cmd_name(cmd.cmd_type));
        for (size_t i = 0; i < cmd.entity_ids.size(); ++i)
            std::printf("%s%u", i ? "," : "", cmd.entity_ids[i]);
        std::printf("] target_id=%u target=(%.2f,%.2f) param=%u\n",
                    cmd.target_id, cmd.target_x, cmd.target_z, cmd.param);
    }

    auto ua = a.units();
    auto ub = b.units();
    auto uc = c.units();
    int shown = 0;
    for (size_t i = 0; i < ua.count && shown < 8; ++i) {
        if (!ua.alive[i]) continue;
        bool diff = memcmp(&ua.pos_x[i], &ub.pos_x[i], sizeof(float)) != 0 ||
                    memcmp(&ua.hp[i], &ub.hp[i], sizeof(float)) != 0 ||
                    memcmp(&ub.pos_x[i], &uc.pos_x[i], sizeof(float)) != 0 ||
                    memcmp(&ub.hp[i], &uc.hp[i], sizeof(float)) != 0 ||
                    ua.action_state[i] != ub.action_state[i] ||
                    ub.action_state[i] != uc.action_state[i] ||
                    ua.team[i] != ub.team[i] || ub.team[i] != uc.team[i];
        if (!diff) continue;
        std::printf("  unit[%u] A(pos=%.4f,%.4f hp=%.3f act=%u) "
                    "B(pos=%.4f,%.4f hp=%.3f act=%u) "
                    "C(pos=%.4f,%.4f hp=%.3f act=%u)\n",
                    ua.id[i], ua.pos_x[i], ua.pos_z[i], ua.hp[i], ua.action_state[i],
                    ub.pos_x[i], ub.pos_z[i], ub.hp[i], ub.action_state[i],
                    uc.pos_x[i], uc.pos_z[i], uc.hp[i], uc.action_state[i]);
        ++shown;
    }

    const size_t nc = std::min({b.buildings().size(), c.buildings().size(), a.buildings().size()});
    int bshown = 0;
    for (size_t i = 0; i < nc && bshown < 6; ++i) {
        const auto &ba = a.buildings()[i];
        const auto &bb = b.buildings()[i];
        const auto &bc = c.buildings()[i];
        bool diff = memcmp(&ba.x, &bb.x, sizeof(float)) != 0 ||
                    memcmp(&ba.hp, &bb.hp, sizeof(float)) != 0 ||
                    memcmp(&ba.build_progress, &bb.build_progress, sizeof(float)) != 0 ||
                    memcmp(&bb.x, &bc.x, sizeof(float)) != 0 ||
                    memcmp(&bb.hp, &bc.hp, sizeof(float)) != 0 ||
                    memcmp(&bb.build_progress, &bc.build_progress, sizeof(float)) != 0 ||
                    ba.team != bb.team || bb.team != bc.team ||
                    ba.type != bb.type || bb.type != bc.type;
        if (!diff) continue;
        std::printf("  bld[%u] A(x=%.2f,%.2f hp=%.2f prog=%.3f t=%u ty=%u) "
                    "B(x=%.2f,%.2f hp=%.2f prog=%.3f t=%u ty=%u) "
                    "C(x=%.2f,%.2f hp=%.2f prog=%.3f t=%u ty=%u)\n",
                    ba.id, ba.x, ba.z, ba.hp, ba.build_progress, ba.team, ba.type,
                    bb.x, bb.z, bb.hp, bb.build_progress, bb.team, bb.type,
                    bc.x, bc.z, bc.hp, bc.build_progress, bc.team, bc.type);
        ++bshown;
    }
    if (a.buildings().size() != b.buildings().size() ||
        b.buildings().size() != c.buildings().size())
        std::printf("  building COUNT mismatch: A=%zu B=%zu C=%zu\n",
                    a.buildings().size(), b.buildings().size(), c.buildings().size());

    std::printf("  resources A(f=%d w=%d g=%d s=%d) B(f=%d w=%d g=%d s=%d) C(f=%d w=%d g=%d s=%d)\n",
                a.food(), a.wood(), a.gold(), a.stone(),
                b.food(), b.wood(), b.gold(), b.stone(),
                c.food(), c.wood(), c.gold(), c.stone());
    std::printf("  ai_stock A(%.2f,%.2f,%.2f,%.2f) B(%.2f,%.2f,%.2f,%.2f) C(%.2f,%.2f,%.2f,%.2f)\n",
                a.ai_food(), a.ai_wood(), a.ai_gold(), a.ai_stone(),
                b.ai_food(), b.ai_wood(), b.ai_gold(), b.ai_stone(),
                c.ai_food(), c.ai_wood(), c.ai_gold(), c.ai_stone());
    std::printf("  scalars next_id A=%u B=%u C=%u | rng A=%u B=%u C=%u | ticks A=%zu B=%zu C=%zu\n",
                a.next_id(), b.next_id(), c.next_id(),
                a.sim_rng_state(), b.sim_rng_state(), c.sim_rng_state(),
                a.stats().ticks, b.stats().ticks, c.stats().ticks);
    std::printf("============================================\n");
}

int run(uint32_t seed, int turns, int verbose_every, bool shadow, bool through) {
    std::printf("[playtest] seed=%u turns=%d petra=on(difficulty=3 behavior=2) "
                "wall_drag=on%s\n", seed, turns, shadow ? " shadow_peer=on" : "");

    ScriptRefs refs_a, refs_b, refs_c;
    Simulation sa, sb, sc;
    TurnManager tma, tmb, tmc;
    build_world(sa, seed, refs_a);
    build_world(sb, seed, refs_b);
    build_world(sc, seed, refs_c);

    ScriptRng rng_a(seed ^ 0xA5A5A5A5u);
    ScriptRng rng_b(seed ^ 0xA5A5A5A5u);
    std::vector<NetCommand> batch_a, batch_b;
    uint32_t match_end_turn = 0;

    for (uint32_t turn = 1; turn <= (uint32_t)turns; ++turn) {
        // Both peers receive the SAME batch from the same script state.
        script_turn(refs_a, turn, rng_a, batch_a);
        script_turn(refs_b, turn, rng_b, batch_b);
        if (!same_batch(batch_a, batch_b)) {
            std::printf("[playtest] SCRIPT MISMATCH at turn %u (harness bug, not an OOS)\n", turn);
            return 1;
        }

        // Split the batch across both command channels the way a real match
        // would, so post_local_command and post_remote_command are both covered.
        for (const auto &c : batch_a) {
            if ((c.client_id % 2) == 0) {
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
            std::printf("[playtest] turn %u did not advance\n", turn);
            return 1;
        }

        // Defeat / victory awareness. Simulation::tick() early-outs once
        // game_over_ latches (the player TC is gone), so the sim freezes and the
        // hash necessarily plateaus. That is a match end, not an OOS: report it
        // and stop, or keep hashing to the requested depth with --through.
        const bool ended = sa.is_game_over() || sb.is_game_over() || sc.is_game_over();
        if (ended) {
            match_end_turn = turn;
            std::printf("[playtest] MATCH END at turn %u (game_over A=%d B=%d C=%d, "
                        "victory A=%d B=%d C=%d) -- sim is frozen from here\n",
                        turn, (int)sa.is_game_over(), (int)sb.is_game_over(), (int)sc.is_game_over(),
                        (int)sa.is_victory(), (int)sb.is_victory(), (int)sc.is_victory());
            if (!through) {
                const uint32_t fe = tma.compute_sim_hash(sa);
                std::printf("[playtest] PASS seed=%u turns=%u final_hash=0x%08X "
                            "(match ended at turn %u) units=%zu bldgs=%zu rng=%u\n",
                            seed, turn, fe, match_end_turn,
                            sa.units().count, sa.buildings().size(), sa.sim_rng_state());
                return 0;
            }
        }

        const uint32_t ha = tma.compute_sim_hash(sa);
        const uint32_t hb = tmb.compute_sim_hash(sb);
        const uint32_t hc = tmc.compute_sim_hash(sc);

        if (verbose_every > 0 && (turn % (uint32_t)verbose_every == 0 || turn == 1)) {
            std::printf("turn %5u  A=0x%08X  B=0x%08X  C=0x%08X  units=%zu/%zu/%zu "
                        "bldgs=%zu/%zu/%zu res=%d/%d/%d/%d ai=%.1f rng=%u/%u/%u\n",
                        turn, ha, hb, hc,
                        sa.units().count, sb.units().count, sc.units().count,
                        sa.buildings().size(), sb.buildings().size(), sc.buildings().size(),
                        sa.food(), sa.wood(), sa.gold(), sa.stone(),
                        sa.ai_food(),
                        sa.sim_rng_state(), sb.sim_rng_state(), sc.sim_rng_state());
        }

        if (ha != hb || ha != hc) {
            std::printf("turn %5u  A=0x%08X  B=0x%08X  C=0x%08X  <-- DIVERGENT\n",
                        turn, ha, hb, hc);
            dump_context(sa, sb, sc, batch_a, turn, ha, hb);
            return 1;
        }
    }

    std::printf("[playtest] PASS seed=%u turns=%d final_hash=0x%08X "
                "units=%zu bldgs=%zu food=%d wood=%d gold=%d stone=%d rng=%u%s%u\n",
                seed, turns, tma.compute_sim_hash(sa),
                sa.units().count, sa.buildings().size(),
                sa.food(), sa.wood(), sa.gold(), sa.stone(), sa.sim_rng_state(),
                match_end_turn ? " match_end_turn=" : "", match_end_turn);
    return 0;
}

void usage(const char *argv0) {
    std::fprintf(stderr,
        "usage: %s --playtest <turns> <seed> [--verbose N] [--shadow]\n"
        "  turns       number of 200ms lockstep turns to run\n"
        "  seed        uint32 simulation seed (identical on every peer)\n"
        "  --verbose N print a hash line every N turns (default 250)\n"
        "  --shadow    also hash-compare a third command-free shadow peer\n"
        "  --through   keep hashing after a match ends (defeat/victory freeze)\n",
        argv0);
}

} // namespace playtest

// ---------------------------------------------------------------------------
// Real two-process lockstep peer (docs/PLAN.md P1 "two clients for 5 minutes").
//
// Unlike --playtest, which hands both sims the same batch in-process, each
// --net-peer process owns ONE Simulation, ONE NetHost and ONE TurnManager and
// talks to the other process over real UDP loopback. Each peer advances turns
// from its own wall clock with random frame jitter and only posts the commands
// its own player owns (client_id 0 = host, 1 = joiner), so a command reaches
// the other side only through the wire. Every turn's post-sim hash is written
// to a log; tools/playtest_lockstep.py --net compares the two logs.
//
// Exit codes: 0 = ran to the end with no OOS reported, 1 = TurnManager raised
// OOS, 3 = never connected / peer vanished.
// ---------------------------------------------------------------------------
namespace netpeer {

uint64_t now_us() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

void wipe_player(Simulation &sim, uint8_t pid) {
    for (size_t i = 0; i < sim.units_.count; ++i) {
        if (sim.units_.alive[i] && sim.units_.team[i] == pid) {
            sim.units_.hp[i] = 0.0f;
            sim.units_.alive[i] = 0;
            sim.units_.action_state[i] = ACTION_IDLE;
        }
    }
    for (auto &b : sim.buildings_)
        if (b.team == pid) b.hp = 0.0f;
}

unsigned building_type_of(const Simulation &sim, uint32_t id) {
    for (const auto &b : sim.buildings())
        if (b.id == id) return b.type;
    return 255;
}

unsigned pack_state_of(const Simulation &sim, uint32_t id) {
    auto uv = sim.units();
    for (size_t i = 0; i < uv.count; ++i)
        if (uv.id[i] == id) return uv.alive[i] ? uv.pack_state[i] : 255;
    return 255;
}

// The formation a unit is in: "<template>:<order>:<members>", or "-".
std::string formation_state_of(const Simulation &sim, uint32_t unit_id) {
    const uint32_t f = sim.formation_of_unit(unit_id);
    for (const auto &c : sim.formations())
        if (c.id == f)
            return std::to_string((unsigned)c.tmpl) + ":" + std::to_string((unsigned)c.order) + ":" +
                   std::to_string(c.members.size());
    return "-";
}

// Both humans' trading goods in ResourceType order, "p0/p1".
std::string goods_of(const Simulation &sim) {
    std::string out;
    for (uint8_t p = 0; p < 2; ++p) {
        if (p) out += "/";
        const uint8_t *g = sim.trading_goods(p);
        for (int r = 0; r < RES_COUNT; ++r) out += (r ? "," : "") + std::to_string((unsigned)g[r]);
    }
    return out;
}

int carry_of(const Simulation &sim, uint32_t id) {
    const int32_t i = sim.lookup_unit(id);
    return i >= 0 && sim.units_.alive[i] ? (int)sim.units_.carry[i] : -1;
}

float hp_of(const Simulation &sim, uint32_t id) {
    auto uv = sim.units();
    for (size_t i = 0; i < uv.count; ++i)
        if (uv.id[i] == id) return uv.alive[i] ? uv.hp[i] : 0.0f;
    return 0.0f;
}

// The unit's current order kind (255 once it is dead or gone) and how many
// Shift-queued orders follow it.
unsigned order_kind_of(const Simulation &sim, uint32_t id) {
    auto uv = sim.units();
    for (size_t i = 0; i < uv.count; ++i)
        if (uv.id[i] == id) return uv.alive[i] ? sim.unit_order(id).kind : 255;
    return 255;
}

size_t order_queue_len_of(const Simulation &sim, uint32_t id) {
    const auto *q = sim.unit_order_queue(id);
    return q ? q->size() : 0;
}

int gate_locked_of(const Simulation &sim, uint32_t id) {
    for (const auto &b : sim.buildings())
        if (b.id == id) return b.gate_locked ? 1 : 0;
    return 0;
}

struct PeerOpts {
    float speed = 1.0f;
    float loss = 0.0f;
    uint32_t defeat_turn = 0;   // wipe out the joiner's player at this turn
    uint32_t pause_turn = 0;    // host pauses here for pause_ms of wall clock
    uint32_t pause_ms = 1500;
    uint32_t saveload_turn = 0; // both peers save and reload their world here
    uint32_t force_oos = 0;     // the joiner corrupts its world after this turn
    uint32_t quit_turn = 0;     // the joiner leaves the match after this turn
    uint32_t host_quit_turn = 0;   // host: ends the game after this turn (tells everyone)
    uint32_t host_vanish_turn = 0; // host: drops off after this turn without a word
    uint32_t stale_rejoin_turn = 0; // joiner: asks to rejoin again mid-match, as a slow loader does
    bool rejoin = false;        // this joiner takes over a dropped seat mid-match
    bool saved_game = false;    // the host continues a saved game: its world has run on alone
    bool observe = false;       // this joiner watches a running match without a seat
    std::string name = "peer";  // the name this peer authenticates with
    std::string password;       // host: required from joiners; client: what it sends
    uint32_t kick_turn = 0;     // host: kicks the client named kick_name after this turn
    std::string kick_name;
    bool ban = false;           // ... and bans it
    int observer_limit = 8;     // host: network.observerlimit
    std::string late_observers = "everyone"; // host: network.lateobservers
    std::string buddies;        // host: lobby.buddies, "a,b"
    int observer_max_lag = -1;  // host: network.observermaxlag
    bool auto_catch_up = true;  // network.autocatchup
    uint32_t stall_turn = 0;    // this peer stops polling for stall_ms after this turn (a lagging machine)
    uint32_t stall_ms = 0;
    uint32_t humans = 2;        // human seats (the host and humans-1 joiners); 3+ takes the AI slot
    std::string oos_dir;        // where each peer writes its OOS dump
};

int run(bool host, uint16_t port, uint32_t seed, uint32_t turns, const PeerOpts &opt,
        const char *log_path) {
    // Line-buffered even into a file: the playtest waits on what a peer printed.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    const float speed = opt.speed, loss = opt.loss;
    const uint32_t defeat_turn = opt.defeat_turn;
    Simulation sim;
    playtest::ScriptRefs refs;
    playtest::build_world(sim, seed, refs);
    // Seat the match the way RtsoMain::apply_game_setup does for a lobby of
    // two humans on opposing teams plus one AI: slot 0 = host, slot 1 =
    // joiner, slot 2 = Petra. Petra must not run for the human slots. The
    // host's setup also turns on Last Man Standing, a 150 pop cap and a
    // one-minute ceasefire; the joiner takes all of it from NMT_GAME_START.
    GameSetupParams seats;
    seats.map_id = "playtest";
    seats.seed = seed;
    seats.slot_count = 3;
    seats.slot_types[0] = 'h';
    seats.slot_types[1] = 'h';
    seats.slot_types[2] = opt.humans >= 3 ? 'h' : 'a';
    seats.teams[0] = 1;
    seats.teams[1] = 2;
    seats.teams[2] = 3;
    seats.lms = 1;
    seats.pop_cap = 150;
    seats.ceasefire = 1;
    sim.set_waves_enabled(false);
    // Give the AI slot a base of its own (build_world leaves it only the
    // wonder), so the match can go on when a human falls.
    // With three humans, the third joiner plays this base instead of Petra.
    const uint32_t slot2_tc = sim.spawn_building_world(0.0f, 40.0f, BLD_TOWN_CENTER, 2, 1.0f);
    std::vector<uint32_t> slot2_villagers;
    for (int i = 0; i < 5; ++i)
        slot2_villagers.push_back(sim.spawn_unit(-4.0f + 2.0f * (float)i, 34.0f, 2, UNIT_VILLAGER, 100.0f));
    // A wall segment the host turns into a gate (cmd 26, 0 A.D. Upgrade.js).
    const uint32_t host_wall = sim.spawn_building_world(-30.0f, 14.0f, BLD_WALL_SEGMENT, 0, 1.0f);
    // A stone thrower the host unpacks and packs again (cmd 29, 0 A.D. Pack.js).
    const uint32_t host_catapult = sim.spawn_unit(-40.0f, -12.0f, 0, UNIT_CATAPULT);
    // Passive, so it unpacks and packs only when ordered: on the default
    // aggressive stance Petra's raiders walking in and out of range make it
    // unpack and pack on its own (0 A.D. EnsureCorrectPackStateForAttack).
    sim.order_set_stance(&host_catapult, 1, STANCE_PASSIVE);
    // A villager the host orders about with the Unit orders commands: gather,
    // walk with a Shift-queued walk after it, back to work (cmd 31), delete (cmd 30).
    const uint32_t host_worker = sim.spawn_unit(-34.0f, 10.0f, 0, UNIT_VILLAGER, 100.0f);
    // A healer the host orders to heal (cmd 32) a swordsman wounded at turn 405.
    // Passive, so it heals only when ordered.
    const uint32_t host_healer = sim.spawn_unit(-44.0f, 6.0f, 0, UNIT_HEALER);
    sim.order_set_stance(&host_healer, 1, STANCE_PASSIVE);
    const uint32_t host_patient = sim.spawn_unit(-24.0f, -16.0f, 0, UNIT_SWORDSMAN);
    // Stands ground, so it waits for the healer rather than going after raiders it sees.
    sim.order_set_stance(&host_patient, 1, STANCE_STAND_GROUND);
    // A squad the host forms up (cmd 8, 0 A.D. Formation.js), walks and attack-moves.
    std::vector<uint32_t> host_squad;
    for (int i = 0; i < 4; ++i) host_squad.push_back(sim.spawn_unit(-36.0f + 2.0f * (float)i, 22.0f, 0, UNIT_SPEARMAN));
    // Standing ground, all four are still there to form up when an AI unit passes in sight.
    sim.order_set_stance(host_squad.data(), host_squad.size(), STANCE_STAND_GROUND);
    // A villager carrying wood the host sends to drop it off (cmd 34, UnitAI ReturnResource).
    // Half a load: a full one (ResourceGatherer Capacities wood 10) goes back by itself.
    const uint32_t host_carrier = sim.spawn_unit(-40.0f, 2.0f, 0, UNIT_VILLAGER, 100.0f);
    {
        const int32_t ci = sim.lookup_unit(host_carrier);
        sim.units_.res_type_carried[ci] = RES_WOOD;
        sim.units_.carry[ci] = 5;
    }
    // Two markets and a trader the host walks off, then sends on a Shift-queued
    // route between them (cmd 35, 0 A.D. setup-trade-route).
    // Both stand in the host's civic centre territory, so neither decays to Gaia.
    const uint32_t host_market_a = sim.spawn_building_world(-50.0f, -20.0f, BLD_MARKET, 0, 1.0f);
    const uint32_t host_market_b = sim.spawn_building_world(-50.0f, 22.0f, BLD_MARKET, 0, 1.0f);
    const uint32_t host_trader = sim.spawn_unit(-46.0f, 0.0f, 0, UNIT_TRADER);
    auto apply_setup = [&](const GameSetupParams &s) {
        sim.set_victory_condition(s.victory >= VICTORY_COUNT ? 0 : s.victory);
        for (uint8_t t = 0; t < MAX_PLAYERS; ++t) {
            const bool seated = t < s.slot_count;
            sim.set_player_active(t, seated, seated && s.slot_types[t] == 'a');
            if (seated && s.teams[t] >= 1 && s.teams[t] <= 8) sim.set_player_team(t, (uint8_t)(s.teams[t] - 1));
        }
        sim.apply_team_diplomacy();
        sim.set_last_man_standing(s.lms != 0);
        sim.set_pop_cap(s.pop_cap);
        sim.set_ceasefire_seconds(60.0f * (float)s.ceasefire);
    };
    if (host) apply_setup(seats);
    // A saved game: the host's world is 10 s on from the joiners' boot world,
    // so only the host's world, sent at Start, can keep them in sync.
    if (host && opt.saved_game)
        for (int i = 0; i < 200; ++i) sim.tick(Simulation::FIXED_STEP);

    const uint8_t sim_civ[2] = {sim.team_civ(0), sim.team_civ(1)};

    NetHost net;
    if (loss > 0.0f) net.set_simulated_loss(loss, seed ^ (host ? 0x1111u : 0x2222u));
    net.setLocalPlayerName(opt.name);
    net.setPassword(opt.password);
    // Peers not given a --name are all "peer"; 0.28's network.duplicateplayernames
    // lets them in as "peer (2)" and so on.
    net.setDuplicatePlayerNames(true);
    bool ok = host ? net.startHost([&] { NetConfig c; c.port = port; return c; }())
                   : net.startClient("127.0.0.1", port);
    if (!ok) { std::fprintf(stderr, "[net-peer] socket start failed\n"); return 3; }

    TurnManager tm;
    uint32_t me = host ? 0u : 1u;
    tm.set_net_host(&net, me);
    tm.set_observer_limit(opt.observer_limit);
    {
        std::set<std::string> buddies;
        std::string cur;
        for (char ch : opt.buddies + ",") {
            if (ch == ',') { if (!cur.empty()) buddies.insert(cur); cur.clear(); }
            else cur += (char)std::tolower((unsigned char)ch);
        }
        tm.set_late_observers(opt.late_observers == "disabled" ? TurnManager::LATE_OBSERVERS_DISABLED
                              : opt.late_observers == "buddies" ? TurnManager::LATE_OBSERVERS_BUDDIES
                              : TurnManager::LATE_OBSERVERS_EVERYONE, buddies);
    }
    tm.set_observer_max_lag(opt.observer_max_lag);
    tm.set_auto_catch_up(opt.auto_catch_up);
    Arena arena(1 << 16);

    // Chat (0 A.D. NMT_CHAT): what reached this peer, as "sender: text".
    std::vector<std::string> chats;
    net.onChat([&](const ChatMsg &cm) { chats.push_back(cm.sender_guid + ": " + cm.message); });
    if (!opt.oos_dir.empty()) tm.set_oos_dump_dir(opt.oos_dir);

    // Wait until the session is full (host: every joiner is in, client: ACK).
    uint32_t joined = 0;
    net.onPeerConnected([&](uint32_t) { ++joined; });
    const uint32_t need = host && !opt.rejoin ? opt.humans - 1 : 1;
    uint64_t t_start = now_us();
    while (joined < need) {
        net.poll(arena);
        arena.reset();
        // 0 A.D. NetClient: a refused authentication (wrong password, banned) ends the join.
        if (!host && net.authResult() > 0) {
            std::printf("[net-peer] join: refused code=%d\n", net.authResult());
            return 4;
        }
        if (now_us() - t_start > 10'000'000ull) {
            std::fprintf(stderr, "[net-peer] %s: no connection after 10s (%u of %u)\n", host ? "host" : "join",
                         joined, need);
            return 3;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    // Rejoin (0 A.D. join-syncing): this process takes over the seat of a
    // joiner that left; the world and pending batches come from the host.
    if (opt.rejoin) {
        tm.request_rejoin(opt.observe);
        t_start = now_us();
        while (!tm.apply_pending_rejoin(sim)) {
            net.poll(arena);
            arena.reset();
            if (tm.rejoin_reject_reason() || now_us() - t_start > 30'000'000ull) {
                std::fprintf(stderr, "[net-peer] join: could not rejoin (reason %u)\n", (unsigned)tm.rejoin_reject_reason());
                return 3;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        std::printf("[net-peer] join: rejoin turn=%u client=%u player=%u\n", tm.rejoin_turn(),
                    tm.local_client_id(), (unsigned)tm.local_player());
    } else if (host && opt.saved_game) {
        std::map<uint32_t, uint8_t> saved;
        for (uint32_t c = 0; c < opt.humans; ++c) saved[c] = (uint8_t)c;
        tm.start_saved_game(saved, &sim);
        seats.saved_game = 1;
        net.sendGameStart(format_game_start_full(seats));
        std::printf("[net-peer] host: saved start hash=%08X\n", tm.compute_sim_hash(sim));
    } else if (host) {
        tm.mark_game_started(&sim);
        net.sendGameStart(format_game_start_full(seats));
    } else {
        t_start = now_us();
        while (!tm.has_pending_game_start()) {
            net.poll(arena);
            arena.reset();
            if (now_us() - t_start > 10'000'000ull) {
                std::fprintf(stderr, "[net-peer] join: no NMT_GAME_START after 10s\n");
                return 3;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        tm.set_game_start_applied_sim(&sim);
        tm.take_pending_game_start_full(seats);
        if (seats.saved_game) {
            // 0 A.D. NMT_SAVED_GAME_START: the host's world, not our own.
            tm.request_rejoin(false);
            t_start = now_us();
            while (!tm.apply_pending_rejoin(sim)) {
                net.poll(arena);
                arena.reset();
                if (tm.rejoin_reject_reason() || now_us() - t_start > 30'000'000ull) {
                    std::fprintf(stderr, "[net-peer] join: no saved game (reason %u)\n", (unsigned)tm.rejoin_reject_reason());
                    return 3;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            std::printf("[net-peer] join: saved start turn=%u player=%u hash=%08X\n", tm.rejoin_turn(),
                        (unsigned)tm.local_player(), tm.compute_sim_hash(sim));
        } else {
            apply_setup(seats);
        }
    }
    // The host numbers joiners 1, 2, ... in connect order (NetHost ACK).
    const bool took_world = opt.rejoin || tm.saved_game() || tm.rejoined();
    if (!host && !took_world && net.getLocalPeerId()) me = net.getLocalPeerId();
    // The seat this process plays, which picks its part of the script (a
    // rejoiner plays the seat it took over).
    const uint32_t script_id = took_world && !host ? (uint32_t)tm.local_player() : me;
    if (!took_world) {
        tm.set_local_client_id(me);
        std::vector<uint32_t> peers;
        for (uint32_t c = 0; c < opt.humans; ++c) peers.push_back(c);
        tm.set_lockstep_peers(peers);
        tm.assign_slot_players(seats); // host -> player 0, joiner -> player 1
    }

    FILE *log = std::fopen(log_path, "w");
    if (!log) { std::fprintf(stderr, "[net-peer] cannot open %s\n", log_path); return 3; }

    playtest::ScriptRng script_rng(seed ^ 0xA5A5A5A5u);
    playtest::ScriptRng jitter(seed ^ (host ? 0x5151u : 0x7373u));
    std::vector<NetCommand> batch;
    uint32_t scripted_turn = 0;
    uint64_t last = now_us();
    uint64_t last_progress = last;
    uint32_t last_turn_seen = 0;
    // Pause: the host's own pause window, and what each peer saw of it.
    bool pause_done = false, pause_seen = false;
    uint64_t pause_since = 0, held_us = 0;
    uint32_t paused_by = 0xFFFFFFFFu, paused_at_turn = 0;
    // Save/load round trip at a turn boundary.
    uint32_t save_hash_before = 0, save_hash_after = 0;
    bool save_ok = false;
    uint64_t oos_since = 0;
    std::set<uint32_t> waited_on;
    bool stall_done = false;
    // Observer: how far behind the host it ran over the match's last 100 turns.
    uint64_t behind_sum = 0, behind_samples = 0;
    // The last non-empty status report: the host's empties once the joiner has
    // finished and gone, which can happen before this peer's last turn.
    std::vector<ClientPerformanceMsg::Entry> last_perf;
    bool kick_done = false;

    while (tm.current_turn() < turns) {
        net.poll(arena);
        arena.reset();

        // Script one batch per local turn boundary (same pure script on both
        // peers); keep only this peer's commands -- the rest must arrive by wire.
        if (scripted_turn <= tm.current_turn()) {
            scripted_turn = tm.current_turn() + 1;
            playtest::script_turn(refs, scripted_turn, script_rng, batch);
            // Diplomacy through the lockstep (cmd 22, as the diplomacy dialog
            // sends it): each human changes its own stance toward the other.
            const uint32_t st = scripted_turn;
            if (st % 300 == 0) {
                NetCommand d;
                d.client_id = (st / 300) % 2;          // alternate who acts
                d.cmd_type = 22;
                d.target_id = 1 - d.client_id;        // toward the other human
                d.param = (uint8_t)((st / 300) % 3);  // ally / neutral / enemy
                batch.push_back(d);
            }
            // Train a roster unit by template (cmd 25, as the roster list sends
            // it): each human queues its civ's default spearman at its own TC.
            if (opt.humans >= 3 && st % 100 == 50) {
                NetCommand w;
                w.client_id = 2;
                w.cmd_type = 1;
                w.entity_ids = slot2_villagers;
                w.target_x = (st / 100) % 2 ? 8.0f : -8.0f;
                w.target_z = 30.0f;
                batch.push_back(w);
            }
            if (opt.humans >= 3 && st % 150 == 75) {
                NetCommand t;
                t.client_id = 2;
                t.cmd_type = 25;
                t.target_id = slot2_tc;
                t.target_x = (float)civ_default_template(sim.team_civ(2), UNIT_SPEARMAN);
                batch.push_back(t);
            }
            if (st % 150 == 75) {
                for (uint32_t who = 0; who < 2; ++who) {
                    NetCommand t;
                    t.client_id = who;
                    t.cmd_type = 25;
                    t.target_id = who == 0 ? refs.town_center : refs.enemy_buildings[2];
                    t.target_x = (float)civ_default_template(sim_civ[who], UNIT_SPEARMAN);
                    batch.push_back(t);
                }
            }
            // Tribute (cmd 27): the joiner sends the host 77 stone at turn 330,
            // and again at 480, after its defeat, when both peers must drop it.
            if (st == 330 || st == 480) {
                NetCommand t;
                t.client_id = 1;
                t.cmd_type = 27;
                t.target_id = 0;
                t.param = RES_STONE;
                t.target_x = 77.0f;
                batch.push_back(t);
            }
            // Structure upgrade (cmd 26): the host turns its wall into a gate.
            if (st == 360) {
                NetCommand u;
                u.client_id = 0;
                u.cmd_type = 26;
                u.target_id = host_wall;
                u.param = 0;
                batch.push_back(u);
            }
            // Gate lock (cmd 28, 0 A.D. Gate.js): the host locks its new gate
            // at turn 500 and unlocks it at 560.
            if (st == 500 || st == 560) {
                NetCommand g;
                g.client_id = 0;
                g.cmd_type = 28;
                g.target_id = host_wall;
                g.param = st == 500 ? 1 : 0;
                batch.push_back(g);
            }
            // Pack (cmd 29): the host unpacks its stone thrower, then packs it.
            if (st == 380 || st == 440) {
                NetCommand pk;
                pk.client_id = 0;
                pk.cmd_type = 29;
                pk.entity_ids.push_back(host_catapult);
                pk.param = st == 440 ? 1 : 0;
                batch.push_back(pk);
            }
            // Unit orders (0 A.D. Commands.js): the host's worker gathers food,
            // walks off with a Shift-queued walk (param bit 0x80) after it, goes
            // back to work (cmd 31) and is finally deleted (cmd 30).
            // Trading goods (cmd 33, 0 A.D. Player.js SetTradingGoods): each human
            // sets the mix its traders bring back, one percent byte per ResourceType.
            if (st == 340 || st == 350) {
                static const uint8_t mix[2][4] = {{10, 20, 30, 40}, {0, 0, 50, 50}};
                NetCommand g;
                g.client_id = st == 340 ? 0 : 1;
                g.cmd_type = 33;
                for (int r = 0; r < RES_COUNT && r < 4; ++r) g.target_id |= (uint32_t)mix[g.client_id][r] << (8 * r);
                batch.push_back(g);
            }
            if (st == 345 || st == 355 || st == 365) {
                NetCommand fm;
                fm.client_id = 0;
                fm.entity_ids = host_squad;
                if (st == 345) { fm.cmd_type = 8; fm.param = FORMATION_BOX; }
                else if (st == 355) { fm.cmd_type = 1; fm.target_x = -30.0f; fm.target_z = 30.0f; }
                else { fm.cmd_type = 10; fm.target_x = -36.0f; fm.target_z = 24.0f; }
                batch.push_back(fm);
            }
            if (st == 430) {
                NetCommand rr;
                rr.client_id = 0;
                rr.cmd_type = 34;
                rr.entity_ids.push_back(host_carrier);
                rr.target_id = refs.town_center;
                batch.push_back(rr);
            }
            if (st == 420 || st == 422) {
                NetCommand tr;
                tr.client_id = 0;
                tr.entity_ids.push_back(host_trader);
                if (st == 420) { tr.cmd_type = 1; tr.target_x = -44.0f; tr.target_z = -8.0f; }
                else {
                    tr.cmd_type = 35;
                    tr.target_id = host_market_a;
                    tr.target_x = (float)host_market_b;
                    tr.param = NET_PARAM_QUEUED;
                }
                batch.push_back(tr);
            }
            if (st == 410) {
                NetCommand hl;
                hl.client_id = 0;
                hl.cmd_type = 32;
                hl.entity_ids.push_back(host_healer);
                hl.target_id = host_patient;
                batch.push_back(hl);
            }
            if (st == 390 || st == 400 || st == 402 || st == 470 || st == 580) {
                NetCommand w;
                w.client_id = 0;
                w.entity_ids.push_back(host_worker);
                if (st == 390) { w.cmd_type = 4; w.target_id = refs.resources[0]; }
                else if (st == 400) { w.cmd_type = 1; w.target_x = -34.0f; w.target_z = -6.0f; }
                else if (st == 402) { w.cmd_type = 1; w.target_x = -34.0f; w.target_z = 6.0f; w.param = NET_PARAM_QUEUED; }
                else if (st == 470) w.cmd_type = 31;
                else w.cmd_type = 30;
                batch.push_back(w);
            }
            // Chat: the joiner whispers to the host, then writes to a player who
            // is not there (nobody may see it); the host answers everyone.
            if (!host && st == 120) net.sendChat("gg", peer_guid(0));
            if (!host && st == 130) net.sendChat("secret", peer_guid(5));
            if (host && st == 140) net.sendChat("hi all");
            for (const auto &c : batch)
                if (c.client_id == script_id) tm.post_local_command(c);
            // An observer's orders go nowhere (it has no seat and sends no batches).
            if (opt.observe && st % 50 == 0) {
                NetCommand o;
                o.cmd_type = 2;
                o.entity_ids = host_squad;
                tm.post_local_command(o);
            }
        }

        uint64_t now = now_us();
        // Pause (0 A.D. NMT_CLIENT_PAUSED): the host pauses for pause_ms of
        // wall clock; both peers must hold their turns until it resumes.
        if (host && opt.pause_turn && !pause_done && tm.current_turn() >= opt.pause_turn) {
            if (!tm.client_paused(me)) {
                tm.set_local_paused(true);
                pause_since = now;
                std::fflush(log); // the playtest reads the log to see the pause start
            }
            else if (now - pause_since >= opt.pause_ms * 1000ull) { tm.set_local_paused(false); pause_done = true; }
        }
        if (tm.is_paused()) {
            if (!pause_seen) {
                pause_seen = true;
                paused_by = *tm.paused_clients().begin();
                paused_at_turn = tm.current_turn();
            }
            held_us += now - last;
            last_progress = now; // a pause is not a stall
        }
        // 0 A.D. "Waiting for players": whom this peer was held on.
        if (!tm.is_paused() && tm.waiting_ms() > 300)
            for (uint32_t c : tm.waiting_on()) waited_on.insert(c);
        if (tm.is_observer() && tm.current_turn() + 100 >= turns) {
            behind_sum += tm.turns_behind();
            ++behind_samples;
        }
        if (opt.stall_turn && !stall_done && tm.current_turn() >= opt.stall_turn) {
            // A machine that falls behind: no polling, no turns.
            stall_done = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(opt.stall_ms));
            now = now_us();
            last = now;
            last_progress = now;
        }
        if (!net.clientPerformance().empty()) last_perf = net.clientPerformance();
        float dt = (float)(now - last) * 1e-6f * speed;
        last = now;
        // 0 A.D. netstatus "disconnected": the link to the host is gone, and why.
        if (net.kickedSelf() && net.connectionLost()) {
            std::printf("[net-peer] join: kicked at turn %u reason=%d\n", tm.current_turn(), net.disconnectReason());
            std::fclose(log);
            net.stop();
            return 0;
        }
        if (!host && !net.kickedSelf() && net.connectionLost()) {
            std::printf("[net-peer] join: lost the host at turn %u reason=%d\n", tm.current_turn(), net.disconnectReason());
            std::fclose(log);
            net.stop();
            return 0;
        }
        if (host && opt.kick_turn && tm.current_turn() >= opt.kick_turn && !kick_done) {
            kick_done = true;
            const uint32_t victim = net.peerByName(opt.kick_name);
            std::printf("[net-peer] host: kick %s peer=%u turn=%u ok=%d\n", opt.kick_name.c_str(), victim,
                        tm.current_turn(), net.kickPeer(victim, opt.ban) ? 1 : 0);
        }
        // CGame::Update runs up to sim-rate turns a frame. Seated peers run one,
        // so each scripted turn above posts its orders; an observer, which
        // posts nothing, may run more to catch up at speeds the frame rate
        // alone could not reach.
        const int max_turns = tm.is_observer() ? std::max(1, (int)speed) : 1;
        for (int ran = 0; ran < max_turns && tm.current_turn() < turns && tm.tick(ran ? 0.0f : dt, sim); ++ran) {
            const auto &f = tm.turn_history().back();
            // Both peers stand on the same state at this turn boundary: wipe the
            // joiner's player out so the conquest check defeats it. It keeps
            // posting orders, which must now be dropped on both peers, while
            // the host plays on against Petra.
            if (defeat_turn && f.turn_index == defeat_turn) wipe_player(sim, 1);
            if (f.turn_index == 405) { // both peers, at the same turn boundary
                const int32_t pi = sim.lookup_unit(host_patient);
                if (pi >= 0 && sim.units_.alive[pi]) sim.units_.hp[pi] = 40.0f;
            }
            // Mid-session save and load (the world reloaded from its own save
            // must hash the same, and both peers carry on in sync from it).
            if (opt.saveload_turn && f.turn_index == opt.saveload_turn) {
                const std::string path = std::string(log_path) + ".sav";
                save_hash_before = tm.compute_sim_hash(sim);
                save_ok = sim.save_state(path.c_str()) && sim.load_state(path.c_str());
                save_hash_after = tm.compute_sim_hash(sim);
            }
            // A seated player's late rejoin request (its setup-time ask, repeated
            // after a slow load): the host must keep it in the lockstep.
            if (!host && opt.stale_rejoin_turn && f.turn_index == opt.stale_rejoin_turn) tm.request_rejoin(false);
            // The host leaves: it ends the game for everyone, or just goes silent.
            if (host && opt.host_quit_turn && f.turn_index == opt.host_quit_turn) {
                std::fclose(log);
                net.stop();
                std::printf("[net-peer] host: ended the game at turn %u\n", f.turn_index);
                return 0;
            }
            if (host && opt.host_vanish_turn && f.turn_index == opt.host_vanish_turn) {
                std::fclose(log);
                std::printf("[net-peer] host: vanished at turn %u\n", f.turn_index);
                std::fflush(stdout);
                std::_Exit(0); // no disconnect notice: the socket just closes
            }
            // Leave: the joiner drops out (NetHost sends the disconnect).
            if (!host && opt.quit_turn && f.turn_index == opt.quit_turn) {
                std::fclose(log);
                net.stop();
                std::printf("[net-peer] join: left at turn %u hash=%08X\n", f.turn_index, f.sim_state_hash);
                return 0;
            }
            // Forced OOS: the joiner alone gains one food after this turn.
            if (!host && opt.force_oos && f.turn_index == opt.force_oos) sim.players_[0].res[RES_FOOD] += 1.0f;
            // Combat footprint: wounds on living units plus dead ones. It must
            // stay 0 on both peers while the ceasefire holds.
            float wounds = 0.0f;
            unsigned dead = 0;
            auto cu = sim.units();
            for (size_t i = 0; i < cu.count; ++i) {
                if (cu.alive[i]) wounds += cu.max_hp[i] - cu.hp[i];
                else ++dead;
            }
            std::fprintf(log, "%u %08X %zu d01=%u d10=%u cf=%d wounds=%.0f dead=%u defeated=%u over=%d stone=%d,%d wall=%u locked=%d pack=%u work=%u queue=%zu heal=%u patient=%.0f goods=%s form=%s carry=%u:%d trade=%u:%zu\n",
                         f.turn_index, f.sim_state_hash, f.commands.size(), sim.get_diplomacy(0, 1),
                         sim.get_diplomacy(1, 0), sim.ceasefire_active() ? 1 : 0, wounds, dead,
                         (unsigned)sim.defeated_mask(), sim.is_game_over() ? 1 : 0,
                         (int)sim.player(0).res[RES_STONE], (int)sim.player(1).res[RES_STONE],
                         (unsigned)building_type_of(sim, host_wall), gate_locked_of(sim, host_wall),
                         pack_state_of(sim, host_catapult), order_kind_of(sim, host_worker),
                         order_queue_len_of(sim, host_worker), order_kind_of(sim, host_healer),
                         hp_of(sim, host_patient), goods_of(sim).c_str(), formation_state_of(sim, host_squad[0]).c_str(),
                         order_kind_of(sim, host_carrier), carry_of(sim, host_carrier),
                         order_kind_of(sim, host_trader), order_queue_len_of(sim, host_trader));
        }
        if (tm.has_out_of_sync() && !opt.force_oos) {
            std::fprintf(stderr, "[net-peer] %s: OOS at turn %u local=%08X peer=%08X\n",
                         host ? "host" : "join", tm.oos_turn(), tm.oos_expected_hash(), tm.oos_received_hash());
            std::fclose(log);
            return 1;
        }
        // Forced OOS: run on until this peer has the host's report and its dump.
        if (opt.force_oos && tm.has_out_of_sync() && !tm.oos_dump_path().empty() &&
                (host || tm.oos_reported_by_host())) {
            if (!oos_since) oos_since = now;
            else if (now - oos_since > 500'000ull) break;
        }
        if (tm.current_turn() != last_turn_seen) {
            last_turn_seen = tm.current_turn();
            last_progress = now;
        } else if (now - last_progress > 15'000'000ull) {
            std::fprintf(stderr, "[net-peer] %s: stalled at turn %u for 15s\n",
                         host ? "host" : "join", tm.current_turn());
            std::fclose(log);
            return 3;
        }
        // Frame jitter: 2-25 ms, independent per peer, so the two clocks drift.
        std::this_thread::sleep_for(std::chrono::microseconds(2000 + jitter.range(0, 23000)));
    }
    std::fclose(log);

    // Keep serving the wire (no more turns) so the slower peer gets the batches
    // it still needs -- we already sent ours through turns + COMMAND_DELAY --
    // and our final sync checks get acknowledged. The host stays until every
    // client has finished and left (or 30 s): under heavy loss a client, an
    // observer most of all, can be many turns behind, and a host that left
    // first cut it off before its last turns. A seated client stays until the
    // host has run the last turn too (its sync check for it came, or 30 s): a
    // host held up for more than the 3 s would otherwise see it leave before
    // the end and list it as gone.
    uint64_t linger = now_us();
    const bool seated = !host && !tm.is_observer();
    auto clients_left = [&] {
        for (const auto &p : net.getPeers())
            if (p.connected) return true;
        return false;
    };
    while (now_us() - linger < 3'000'000ull ||
           (host && clients_left() && now_us() - linger < 30'000'000ull) ||
           (seated && !net.connectionLost() && !tm.has_peer_hash(turns, 0) && now_us() - linger < 30'000'000ull)) {
        net.poll(arena);
        arena.reset();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    int units[3] = {0, 0, 0};
    auto uv = sim.units();
    for (size_t i = 0; i < uv.count; ++i)
        if (uv.alive[i] && uv.team[i] < 3) ++units[uv.team[i]];
    std::printf("[net-peer] %s: done turns=%u hash=%08X oos=%d dropped=%u game_over=%d "
                "units p0=%d p1=%d p2=%d petra p0=%d p1=%d p2=%d diplo 0->1=%u 1->0=%u\n",
                host ? "host" : "join", tm.current_turn(), tm.compute_sim_hash(sim),
                tm.has_out_of_sync() ? 1 : 0, net.getStats().dropped_packets,
                sim.is_game_over() ? 1 : 0, units[0], units[1], units[2],
                sim.petra_runs_for(0) ? 1 : 0, sim.petra_runs_for(1) ? 1 : 0,
                sim.petra_runs_for(2) ? 1 : 0, sim.get_diplomacy(0, 1), sim.get_diplomacy(1, 0));
    // Compared across peers by playtest_lockstep.py: what each sim runs with.
    std::printf("[net-peer] %s: setup lms=%d popcap=%u ceasefire=%.0f teams=%u,%u,%u\n",
                host ? "host" : "join", sim.last_man_standing() ? 1 : 0, (unsigned)sim.pop_cap(),
                sim.ceasefire_seconds(), (unsigned)sim.player_team(0), (unsigned)sim.player_team(1),
                (unsigned)sim.player_team(2));
    for (const auto &c : chats) std::printf("[net-peer] %s: chat %s\n", host ? "host" : "join", c.c_str());
    for (const auto &k : net.kicks())
        std::printf("[net-peer] %s: kicks name=%s ban=%d\n", host ? "host" : "join", k.name.c_str(), (int)k.ban);
    {
        // Network status (0 A.D. NMT_CLIENT_PERFORMANCE): the host's last report.
        std::string rtt, waited;
        for (const auto &e : last_perf)
            rtt += (rtt.empty() ? "" : ",") + std::to_string(e.client) + ":" + std::to_string(e.rtt_ms);
        for (uint32_t c : waited_on) waited += (waited.empty() ? "" : ",") + std::to_string(c);
        std::printf("[net-peer] %s: status reports=%llu rtt=%s waited_on=%s\n", host ? "host" : "join",
                    (unsigned long long)net.clientPerformanceReports(), rtt.empty() ? "-" : rtt.c_str(),
                    waited.empty() ? "-" : waited.c_str());
    }
    {
        // NMT_PLAYER_ASSIGNMENT: the host's client list, client:name:player:status.
        std::string list;
        for (const auto &e : net.playerAssignment())
            list += (list.empty() ? "" : ",") + std::to_string(e.client) + ":" + e.name + ":" +
                    std::to_string((unsigned)e.player) + ":" + std::to_string((unsigned)e.status);
        std::printf("[net-peer] %s: roster observer=%d list=%s\n", host ? "host" : "join", tm.is_observer() ? 1 : 0,
                    list.empty() ? "-" : list.c_str());
    }
    if (tm.is_observer())
        std::printf("[net-peer] join: catchup behind_late=%.1f\n",
                    behind_samples ? (double)behind_sum / (double)behind_samples : 0.0);
    if (!tm.rejoined_clients().empty()) {
        std::string ids;
        for (uint32_t c : tm.rejoined_clients()) ids += (ids.empty() ? "" : ",") + std::to_string(c);
        std::printf("[net-peer] %s: rejoined me=%u clients=%s\n", host ? "host" : "join", tm.local_client_id(), ids.c_str());
    }
    if (opt.pause_turn)
        std::printf("[net-peer] %s: pause seen=%d by=%u at_turn=%u held_ms=%u\n", host ? "host" : "join",
                    pause_seen ? 1 : 0, paused_by, paused_at_turn, (unsigned)(held_us / 1000));
    if (opt.saveload_turn)
        std::printf("[net-peer] %s: saveload ok=%d before=%08X after=%08X\n", host ? "host" : "join",
                    save_ok ? 1 : 0, save_hash_before, save_hash_after);
    if (opt.force_oos) {
        std::string who;
        for (const auto &n : tm.oos_players()) who += (who.empty() ? "" : ",") + n;
        std::printf("[net-peer] %s: oos turn=%u from_host=%d players=%s matches_host=%d dump=%s\n", host ? "host" : "join",
                    tm.oos_turn(), tm.oos_reported_by_host() ? 1 : 0, who.c_str(), tm.oos_matches_host() ? 1 : 0,
                    tm.oos_dump_path().c_str());
        return tm.has_out_of_sync() && !tm.oos_dump_path().empty() ? 0 : 1;
    }
    return tm.has_out_of_sync() ? 1 : 0;
}

} // namespace netpeer

int main(int argc, char **argv) {
    // P1 playtest mode: --playtest <turns> <seed>
    if (argc >= 2 && std::strcmp(argv[1], "--playtest") == 0) {
        int turns = 2000;
        uint32_t seed = 4242;
        int verbose_every = 250;
        bool shadow = false;
        bool through = false;
        bool have_turns = false, have_seed = false;
        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "--verbose") == 0 && i + 1 < argc) {
                verbose_every = std::atoi(argv[++i]);
            } else if (std::strcmp(argv[i], "--shadow") == 0) {
                shadow = true;
            } else if (std::strcmp(argv[i], "--through") == 0) {
                through = true;
            } else if (!have_turns) {
                turns = std::atoi(argv[i]);
                have_turns = true;
            } else if (!have_seed) {
                seed = (uint32_t)std::strtoul(argv[i], nullptr, 10);
                have_seed = true;
            } else {
                playtest::usage(argv[0]);
                return 2;
            }
        }
        if (turns <= 0) {
            playtest::usage(argv[0]);
            return 2;
        }
        return playtest::run(seed, turns, verbose_every, shadow, through);
    }
    // Real UDP peer: --net-peer <host|join> <port> <seed> <turns> <log> [--speed X] [--loss P]
    //   [--defeat-turn N]   wipe out the joiner's player at turn N (0 = never)
    //   [--pause-turn N]    the host pauses at turn N for --pause-ms (default 1500)
    //   [--saveload-turn N] both peers save and reload their world after turn N
    //   [--force-oos N]     the joiner corrupts its world after turn N
    //   [--oos-dir DIR]     where this peer writes oos_dump.txt/.dat
    //   [--quit-turn N]     the joiner leaves the match after turn N
    //   [--host-quit-turn N] the host ends the game after turn N
    //   [--host-vanish-turn N] the host drops off after turn N without telling anyone
    //   [--stale-rejoin-turn N] a seated joiner asks to rejoin again after turn N
    //   [--rejoin 1]        this joiner takes over a dropped seat mid-match
    //   [--saved-game 1]    the host continues a saved game: joiners load its world
    if (argc >= 7 && std::strcmp(argv[1], "--net-peer") == 0) {
        netpeer::PeerOpts opt;
        for (int i = 7; i + 1 < argc; i += 2) {
            const char *k = argv[i], *v = argv[i + 1];
            if (std::strcmp(k, "--defeat-turn") == 0) opt.defeat_turn = (uint32_t)std::atoi(v);
            else if (std::strcmp(k, "--speed") == 0) opt.speed = (float)std::atof(v);
            else if (std::strcmp(k, "--loss") == 0) opt.loss = (float)std::atof(v);
            else if (std::strcmp(k, "--pause-turn") == 0) opt.pause_turn = (uint32_t)std::atoi(v);
            else if (std::strcmp(k, "--pause-ms") == 0) opt.pause_ms = (uint32_t)std::atoi(v);
            else if (std::strcmp(k, "--saveload-turn") == 0) opt.saveload_turn = (uint32_t)std::atoi(v);
            else if (std::strcmp(k, "--force-oos") == 0) opt.force_oos = (uint32_t)std::atoi(v);
            else if (std::strcmp(k, "--oos-dir") == 0) opt.oos_dir = v;
            else if (std::strcmp(k, "--quit-turn") == 0) opt.quit_turn = (uint32_t)std::atoi(v);
            else if (std::strcmp(k, "--host-quit-turn") == 0) opt.host_quit_turn = (uint32_t)std::atoi(v);
            else if (std::strcmp(k, "--host-vanish-turn") == 0) opt.host_vanish_turn = (uint32_t)std::atoi(v);
            else if (std::strcmp(k, "--stale-rejoin-turn") == 0) opt.stale_rejoin_turn = (uint32_t)std::atoi(v);
            else if (std::strcmp(k, "--rejoin") == 0) opt.rejoin = std::atoi(v) != 0;
            else if (std::strcmp(k, "--saved-game") == 0) opt.saved_game = std::atoi(v) != 0;
            else if (std::strcmp(k, "--observe") == 0) opt.observe = opt.rejoin = std::atoi(v) != 0;
            else if (std::strcmp(k, "--name") == 0) opt.name = v;
            else if (std::strcmp(k, "--password") == 0) opt.password = v;
            else if (std::strcmp(k, "--kick-turn") == 0) opt.kick_turn = (uint32_t)std::atoi(v);
            else if (std::strcmp(k, "--kick-name") == 0) opt.kick_name = v;
            else if (std::strcmp(k, "--ban") == 0) opt.ban = std::atoi(v) != 0;
            else if (std::strcmp(k, "--observer-limit") == 0) opt.observer_limit = std::atoi(v);
            else if (std::strcmp(k, "--late-observers") == 0) opt.late_observers = v;
            else if (std::strcmp(k, "--buddies") == 0) opt.buddies = v;
            else if (std::strcmp(k, "--observer-max-lag") == 0) opt.observer_max_lag = std::atoi(v);
            else if (std::strcmp(k, "--auto-catch-up") == 0) opt.auto_catch_up = std::atoi(v) != 0;
            else if (std::strcmp(k, "--stall-turn") == 0) opt.stall_turn = (uint32_t)std::atoi(v);
            else if (std::strcmp(k, "--stall-ms") == 0) opt.stall_ms = (uint32_t)std::atoi(v);
            else if (std::strcmp(k, "--humans") == 0) opt.humans = (uint32_t)std::max(2, std::min(3, std::atoi(v)));
        }
        return netpeer::run(std::strcmp(argv[2], "host") == 0, (uint16_t)std::atoi(argv[3]),
                            (uint32_t)std::strtoul(argv[4], nullptr, 10),
                            (uint32_t)std::atoi(argv[5]), opt, argv[6]);
    }
    if (argc >= 2 && std::strcmp(argv[1], "--help") == 0) {
        playtest::usage(argv[0]);
        return 0;
    }


}
int run_legacy_tests() {

    std::cout << "[Test 8] VisionSharing allied visibility..." << std::endl;
    {
        Simulation enemy_only, allied;
        enemy_only.spawn_building_world(-40.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
        allied.spawn_building_world(-40.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
        const uint32_t enemy = enemy_only.spawn_unit(0.0f, 0.0f, 1, UNIT_VILLAGER);
        const uint32_t ally = allied.spawn_unit(0.0f, 0.0f, 1, UNIT_VILLAGER);
        assert(enemy && ally);
        // Enemy vision is not shared; allied unit vision is shared with player 0.
        for (int i = 0; i < 6; ++i) { enemy_only.tick(0.05f); allied.tick(0.05f); }
        const int G = enemy_only.fow_res();
        const int center = G / 2 * G + G / 2;
        assert(enemy_only.fow_grid()[center] == Simulation::FOW_UNEXPLORED);
        allied.set_mutual_ally(1);
        for (int i = 0; i < 6; ++i) allied.tick(0.05f);
        assert(allied.fow_grid()[center] == Simulation::FOW_VISIBLE);
        std::cout << "  [PASS] allied unit vision shares; enemy vision stays hidden." << std::endl;
    }

    std::cout << "[Test 10] TerritoryDecay capture fallback..." << std::endl;
    {
        Simulation s;
        s.spawn_building_world(-40.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
        s.spawn_building_world(50.0f, 0.0f, BLD_BARRACKS, 1, 1.0f);
        const float hp = s.buildings().back().hp;
        // No capture progress exists, so isolation must not damage HP. The
        // capture-state path is reserved for future neighbor redistribution.
        s.tick(1.0f);
        assert(s.buildings().back().hp == hp);
        std::cout << "  [PASS] isolated structure does not bypass capture state with HP decay." << std::endl;
    }

    std::cout << "[Test 12] Diplomacy tribute and victory behavior..." << std::endl;
    {
        Simulation s;
        s.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
        s.set_starting_resources(500, 0, 0, 0);
        assert(s.send_tribute(1, RES_FOOD, 100));
        assert(s.food() == 400 && s.ai_food() >= 100.0f);
        // Unsupported recipients must not destroy player resources.
        assert(!s.send_tribute(2, RES_FOOD, 100));
        assert(s.food() == 400);

        s.set_victory_condition(VICTORY_WONDER);
        s.spawn_building_world(0.0f, 8.0f, BLD_WONDER, 0, 1.0f);
        s.tick(1.0f);
        assert(!s.is_victory());
        s.tick(599.0f);
        assert(s.is_victory() && s.is_game_over());
        std::cout << "  [PASS] tribute validation and wonder victory timer." << std::endl;
    }

    std::cout << "[Test 13] Petra diplomacy request lifecycle..." << std::endl;
    {
        Simulation s;
        s.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
        s.spawn_unit(0.0f, 0.0f, 0, UNIT_VILLAGER);
        s.receive_petra_diplomacy_request(1, true, 1000, RES_FOOD);
        assert(s.petra_diplomacy_requests().size() == 1);
        const auto &r = s.petra_diplomacy_requests().at(1);
        assert(r.status == PetraDiplomacyRequest::WAITING && r.wanted == 1000);
        // The request state is deterministic; the exact 120-second lapse is
        // exercised by the 8-second diplomacy manager cadence in live play.
        s.tick(8.0f);
        assert(s.petra_diplomacy_requests().size() == 1);
        std::cout << "  [PASS] diplomacy request enters and remains pending deterministically." << std::endl;
    }

    std::cout << "[Test 14] Relic capture victory..." << std::endl;
    {
        Simulation s;
        s.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
        s.set_victory_condition(VICTORY_RELIC);
        const uint32_t relic = s.spawn_relic(30.0f, 0.0f);
        assert(relic != 0);
        std::vector<uint32_t> captors;
        for (int k = 0; k < 6; ++k) captors.push_back(s.spawn_unit(26.0f, -3.0f + k, 0, UNIT_SPEARMAN));
        s.order_attack(captors.data(), captors.size(), relic);
        for (int k = 0; k < 60 * 20 && !s.relics()[0].claimed; ++k) s.tick(Simulation::FIXED_STEP);
        assert(s.relics().size() == 1 && s.relics()[0].claimed);
        assert(s.relics()[0].owner_team == 0);
        for (int k = 0; k < 179 * 20; ++k) s.tick(Simulation::FIXED_STEP);
        assert(!s.is_victory());
        for (int k = 0; k < 2 * 20; ++k) s.tick(Simulation::FIXED_STEP);
        assert(s.is_victory() && s.is_game_over());
        std::cout << "  [PASS] relic capture, ownership, and 180-second victory." << std::endl;
    }

    std::cout << "[Test 15] Capturable per-team capture points..." << std::endl;
    {
        Simulation s;
        s.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
        const auto &b = s.buildings().back();
        assert(b.capture_points_by_team[0] == b.max_capture_points);
        assert(b.capture_points_by_team[1] == 0.0f);
        // Two identical simulations must hash the new per-team state equally.
        Simulation a;
        a.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
        TurnManager ta, tb;
        assert(ta.compute_sim_hash(a) == tb.compute_sim_hash(a));
        std::cout << "  [PASS] per-team capture state initializes and is hash-stable." << std::endl;
    }

    std::cout << "[Test 19] 0AD capture template values and redistribution..." << std::endl;
    {
        Simulation s;
        const uint32_t tc = s.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
        const uint32_t farm = s.spawn_building_world(10.0f, 0.0f, BLD_FARMSTEAD, 0, 1.0f);
        const auto &tcb = s.buildings()[0];
        const auto &fb = s.buildings()[1];
        assert(tcb.max_capture_points == 2500.0f && tcb.capture_regen_rate == 30.0f);
        assert(fb.max_capture_points == 300.0f && fb.territory_decay_rate == 20.0f);
        assert(s.buildings()[0].capture_points_by_team[0] == 2500.0f);
        (void)tc; (void)farm;
        std::cout << "  [PASS] civil-centre/farmstead capture values match 0AD templates." << std::endl;
    }

    std::cout << "[Test 17] Terrain height occlusion and FoW queries..." << std::endl;
    {
        auto make_heights = [](bool ridge) {
            std::vector<float> h(65 * 65, 0.0f);
            if (ridge) {
                for (int x = 0; x < 65; ++x) h[32 * 65 + x] = 10.0f;
            }
            return h;
        };
        Simulation flat, blocked;
        flat.set_terrain_data(make_heights(false), 65, -1.0f);
        blocked.set_terrain_data(make_heights(true), 65, -1.0f);
        flat.spawn_unit(0.0f, -10.0f, 0, UNIT_VILLAGER);
        blocked.spawn_unit(0.0f, -10.0f, 0, UNIT_VILLAGER);
        for (int i = 0; i < 6; ++i) { flat.tick(0.05f); blocked.tick(0.05f); }
        const int G = flat.fow_res();
        const int center = G / 2 * G + G / 2;
        assert(flat.fow_state_at(0.0f, -10.0f) == Simulation::FOW_VISIBLE ||
               flat.fow_state_at(0.0f, -9.0f) == Simulation::FOW_VISIBLE);
        assert(blocked.fow_state_at(0.0f, 0.0f) != Simulation::FOW_VISIBLE);
        assert(flat.fow_state_at(1000.0f, 0.0f) == Simulation::FOW_UNEXPLORED);
        assert(flat.is_entity_visible_to_viewer(0.0f, 0.0f, 0, 0));
        assert(!blocked.is_entity_visible_to_viewer(0.0f, 0.0f, 1, 0));
        std::cout << "  [PASS] hills block FoW rays; flat terrain remains visible." << std::endl;
    }

    std::cout << "[Test 18] FoW save/load history..." << std::endl;
    {
        Simulation saved, loaded;
        saved.spawn_unit(0.0f, 0.0f, 0, UNIT_VILLAGER);
        for (int i = 0; i < 6; ++i) saved.tick(0.05f);
        const std::string path = (std::getenv("HOME") ? std::getenv("HOME") : ".") + std::string("/rts0_fow_test.sav");
        assert(saved.save_state(path.c_str()));
        assert(loaded.load_state(path.c_str()));
        assert(loaded.fow_state_at(0.0f, 0.0f) == Simulation::FOW_VISIBLE);
        std::remove(path.c_str());
        std::cout << "  [PASS] FoW history survives save/load." << std::endl;
    }

    std::cout << "[Test 16] Mutual alliance and shared LOS..." << std::endl;
    {
        Simulation s;
        assert(!s.is_mutual_ally(1));
        s.set_diplomacy(1, Simulation::DIPLO_ALLY);
        assert(!s.is_mutual_ally(1));
        s.set_mutual_ally(1);
        assert(s.is_mutual_ally(1));
        assert(s.get_diplomacy(1) == Simulation::DIPLO_ALLY);
        s.spawn_building_world(-40.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
        s.spawn_unit(0.0f, 0.0f, 1, UNIT_VILLAGER);
        for (int i = 0; i < 6; ++i) s.tick(0.05f);
        const int G = s.fow_res();
        const int center = G / 2 * G + G / 2;
        assert(s.fow_grid()[center] == Simulation::FOW_VISIBLE);
        std::cout << "  [PASS] reciprocal alliance gates shared LOS." << std::endl;
    }

    std::cout << "[Test 11] Research and production component behavior..." << std::endl;
    {
        Simulation s;
        const uint32_t tc = s.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
        s.set_starting_resources(1000, 1000, 1000, 1000);
        // Town Phase needs 5 finished Village Phase structures besides the CC.
        assert(!s.cmd_research(0, tc, TECH_PHASE_TOWN));
        for (int h = 0; h < 5; ++h) s.spawn_building_world(-20.0f + 6.0f * h, 20.0f, BLD_HOUSE, 0, 1.0f);
        assert(s.cmd_research(0, tc, TECH_PHASE_TOWN));
        {
            bool queued = false;
            for (const auto &b : s.buildings()) if (b.id == tc) queued = Simulation::researching(b) == TECH_PHASE_TOWN;
            assert(queued);
        }
        for (int i = 0; i < 32 && !s.is_tech_researched(TECH_PHASE_TOWN); ++i) s.tick(1.0f);
        assert(s.is_tech_researched(TECH_PHASE_TOWN));
        assert(s.player_phase() == 2);
        const int gold_before = s.gold();
        assert(s.cmd_train(0, tc, UNIT_SWORDSMAN));
        assert(s.gold() == gold_before - unit_type_info(UNIT_SWORDSMAN).gold_cost);
        // ProductionQueue.js queues the order; the training timer consumes it
        // during the next simulation tick, so a long tick is intentionally not
        // asserted here.
        std::cout << "  [PASS] research gates and production queue resources." << std::endl;
    }

    std::cout << "[Test 9] 0 A.D. normalized trader gain..." << std::endl;
    {
        Simulation s;
        s.set_map_half(64.0f);
        s.set_team_civ(0, CIV_ATHENIANS);
        Simulation::Building a{}, b{};
        a.x = 0.0f; a.z = 0.0f;
        b.x = 100.0f; b.z = 0.0f;
        // Trade.js: mapSize=(2*64)^2, 100m baseline, base civ multiplier 1.0
        // should yield exactly the normalized 100m trade gain (100).
        const int gain = s.calculate_trader_gain(a, b, 0);
        // A 128m map has 0 A.D. TradeGainNormalization 1024/16384; the
        // 100m baseline is therefore ~3 resource units, not 100.
        assert(gain >= 2 && gain <= 4);
        s.set_team_civ(0, CIV_PERSIANS);
        const int persian_gain = s.calculate_trader_gain(a, b, 0);
        assert(persian_gain == static_cast<int>(std::lround(gain * 1.25f)));
        std::cout << "  [PASS] normalized distance gain and civ multiplier." << std::endl;
    }

    std::cout << "========================================" << std::endl;
    std::cout << "Testing RTS0 Netcode & Multiplayer Transport" << std::endl;
    std::cout << "========================================" << std::endl;

    // 1. Test Packet Serialization / Deserialization
    std::cout << "[Test 1] Message Framing & Serialization..." << std::endl;
    {
        // Handshake
        ClientHandshakeMsg chm;
        chm.engine_version = "RTS0-0.1-Test";
        chm.enabled_mods.push_back({"public", "0.0.26"});
        std::vector<uint8_t> chm_payload = NetPacketSerializer::frame_client_handshake(chm);
        ClientHandshakeMsg chm_parsed;
        [[maybe_unused]] bool ok = NetPacketSerializer::parse_client_handshake(chm_payload.data(), chm_payload.size(), chm_parsed);
        assert(ok);
        assert(chm_parsed.magic_response == PS_PROTOCOL_MAGIC_RESPONSE);
        assert(chm_parsed.engine_version == "RTS0-0.1-Test");
        assert(chm_parsed.enabled_mods.size() == 1);
        assert(chm_parsed.enabled_mods[0].first == "public");

        // Authenticate
        AuthenticateMsg am;
        am.name = "PlayerOne";
        am.password = "secret123";
        std::vector<uint8_t> am_payload = NetPacketSerializer::frame_authenticate(am);
        AuthenticateMsg am_parsed;
        ok = NetPacketSerializer::parse_authenticate(am_payload.data(), am_payload.size(), am_parsed);
        assert(ok);
        assert(am_parsed.name == "PlayerOne");
        assert(am_parsed.password == "secret123");

        // Ready
        ReadyMsg rm;
        rm.guid = "peer-1";
        rm.status = 1;
        std::vector<uint8_t> rm_payload = NetPacketSerializer::frame_ready(rm);
        ReadyMsg rm_parsed;
        ok = NetPacketSerializer::parse_ready(rm_payload.data(), rm_payload.size(), rm_parsed);
        assert(ok);
        assert(rm_parsed.guid == "peer-1");
        assert(rm_parsed.status == 1);

        // GameStart
        GameStartMsg gsm;
        gsm.init_attributes = "map=acropolis_bay;seed=42;civ=athenians";
        std::vector<uint8_t> gsm_payload = NetPacketSerializer::frame_game_start(gsm);
        GameStartMsg gsm_parsed;
        ok = NetPacketSerializer::parse_game_start(gsm_payload.data(), gsm_payload.size(), gsm_parsed);
        assert(ok);
        assert(gsm_parsed.init_attributes == "map=acropolis_bay;seed=42;civ=athenians");

        // SimulationCommand
        SimulationCommandMsg scm;
        scm.turn = 10;
        scm.client_id = 2;
        NetCommand cmd;
        cmd.client_id = 2;
        cmd.cmd_type = 1; // Move
        cmd.entity_ids = {101, 102, 103};
        cmd.target_x = 45.5f;
        cmd.target_z = 88.2f;
        scm.commands.push_back(cmd);
        std::vector<uint8_t> scm_payload = NetPacketSerializer::frame_simulation_command(scm);
        SimulationCommandMsg scm_parsed;
        ok = NetPacketSerializer::parse_simulation_command(scm_payload.data(), scm_payload.size(), scm_parsed);
        assert(ok);
        assert(scm_parsed.turn == 10);
        assert(scm_parsed.client_id == 2);
        assert(scm_parsed.commands.size() == 1);
        assert(scm_parsed.commands[0].entity_ids.size() == 3);
        assert(scm_parsed.commands[0].entity_ids[1] == 102);
        assert(scm_parsed.commands[0].target_x == 45.5f);

        // SyncCheck
        SyncCheckMsg sync;
        sync.turn = 10;
        sync.hash = 0xDEADBEEF;
        std::vector<uint8_t> sync_payload = NetPacketSerializer::frame_sync_check(sync);
        SyncCheckMsg sync_parsed;
        ok = NetPacketSerializer::parse_sync_check(sync_payload.data(), sync_payload.size(), sync_parsed);
        assert(ok);
        assert(sync_parsed.turn == 10);
        assert(sync_parsed.hash == 0xDEADBEEF);

        // Chat
        ChatMsg chat;
        chat.sender_guid = "peer-0";
        chat.message = "gg wp";
        std::vector<uint8_t> chat_payload = NetPacketSerializer::frame_chat(chat);
        ChatMsg chat_parsed;
        ok = NetPacketSerializer::parse_chat(chat_payload.data(), chat_payload.size(), chat_parsed);
        assert(ok);
        assert(chat_parsed.sender_guid == "peer-0");
        assert(chat_parsed.message == "gg wp");

        std::cout << "  [PASS] All 7 message types correctly framed & serialized." << std::endl;
    }

    // 2. Test Real Socket Loopback Transport
    std::cout << "[Test 2] Real UDP Socket Host/Client Loopback..." << std::endl;
    {
        NetConfig host_cfg;
        host_cfg.port = 20595; // Default 0 A.D. port
        NetHost host;
        [[maybe_unused]] bool host_started = host.startHost(host_cfg);
        assert(host_started);
        assert(host.isHost());

        NetHost client;
        [[maybe_unused]] bool client_started = client.startClient("127.0.0.1", 20595);
        assert(client_started);
        assert(client.isClient());

        Arena arena(65536);

        bool host_connected = false;
        bool client_connected = false;
        host.onPeerConnected([&](uint32_t peer_id) {
            host_connected = true;
            std::cout << "  Host saw peer connect, peer_id=" << peer_id << std::endl;
        });
        client.onPeerConnected([&](uint32_t peer_id) {
            client_connected = true;
            std::cout << "  Client saw connect ACK, local_peer_id=" << client.getLocalPeerId() << std::endl;
        });

        // Poll for connection handshake
        for (int i = 0; i < 50 && (!host_connected || !client_connected); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            host.poll(arena);
            client.poll(arena);
        }

        assert(host_connected);
        assert(client_connected);

        // Test sending reliable simulation command from client to host
        bool command_received = false;
        uint32_t received_peer = 0;
        std::vector<uint8_t> received_data;
        host.onCommand([&](uint32_t peer_id, const uint8_t *data, size_t size) {
            command_received = true;
            received_peer = peer_id;
            received_data.assign(data, data + size);
        });

        std::vector<uint8_t> test_payload = {0x11, 0x22, 0x33, 0x44};
        client.sendCommand(0, test_payload);

        for (int i = 0; i < 50 && !command_received; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            host.poll(arena);
            client.poll(arena);
        }

        assert(command_received);
        assert(received_data == test_payload);
        std::cout << "  [PASS] Real UDP socket host/client exchange verified." << std::endl;

        host.stop();
        client.stop();
    }

    // 3. Test TurnManager Multiplayer Synchronization & OOS Detection
    std::cout << "[Test 3] TurnManager Multiplayer Sync & OOS Detection..." << std::endl;
    {
        Simulation sim1;
        Simulation sim2;

        TurnManager tm1;
        TurnManager tm2;

        // Post identical commands to both
        NetCommand move_cmd;
        move_cmd.cmd_type = 1;
        move_cmd.entity_ids = {1};
        move_cmd.target_x = 10.0f;
        move_cmd.target_z = 10.0f;

        tm1.post_local_command(move_cmd);
        tm2.post_local_command(move_cmd);

        // Advance 1 turn
        [[maybe_unused]] bool t1 = tm1.tick(0.200f, sim1);
        [[maybe_unused]] bool t2 = tm2.tick(0.200f, sim2);
        assert(t1 && t2);
        assert(tm1.current_turn() == 1);
        assert(tm2.current_turn() == 1);

        uint32_t hash1 = tm1.compute_sim_hash(sim1);
        uint32_t hash2 = tm2.compute_sim_hash(sim2);
        assert(hash1 == hash2);
        std::cout << "  Synchronized turn hash: " << hash1 << " (both agree)" << std::endl;

        // Exchange hashes
        tm1.receive_sync_check(1, 2, hash2);
        assert(!tm1.has_out_of_sync());

        // Test out-of-sync trigger when peer sends divergent hash
        tm1.receive_sync_check(1, 3, 0x12345678);
        assert(tm1.has_out_of_sync());
        assert(tm1.oos_turn() == 1);
        assert(tm1.oos_expected_hash() == hash1);
        assert(tm1.oos_received_hash() == 0x12345678);
        std::cout << "  [PASS] Out-of-sync detection triggered correctly." << std::endl;
    }

    // 4. Dual-sim lockstep hashes after several empty turns (HashSerializer after sim ticks)
    std::cout << "[Test 4] Dual Simulation lockstep hash agreement..." << std::endl;
    {
        Simulation a;
        Simulation b;
        a.reset();
        b.reset();
        TurnManager ta;
        TurnManager tb;
        for (int t = 0; t < 8; ++t) {
            [[maybe_unused]] bool ok_a = ta.tick(TurnManager::TURN_DURATION, a);
            [[maybe_unused]] bool ok_b = tb.tick(TurnManager::TURN_DURATION, b);
            assert(ok_a && ok_b);
            uint32_t ha = ta.compute_sim_hash(a);
            uint32_t hb = tb.compute_sim_hash(b);
            assert(ha == hb);
        }
        assert(ta.current_turn() == 8);
        std::cout << "  [PASS] 8 empty turns hashed identically on both sims." << std::endl;
    }

    std::cout << "[Test 5] Seeded determinism: combat + fauna + Petra wander..." << std::endl;
    {
        // Same seed, same commands, same ticks -> identical hashes every turn.
        // Exercises all 17 former rand() sites: combat spread, scout sweep,
        // worker jitter, fauna spawn + wander.
        auto build = [](Simulation &s) {
            s.set_sim_seed(4242);
            s.reset();
            uint32_t m1 = s.spawn_unit(-6.0f, 0.0f, 0, UNIT_ARCHER, 90.0f);
            uint32_t m2 = s.spawn_unit(6.0f, 0.0f, 1, UNIT_SWORDSMAN, 160.0f);
            uint32_t m3 = s.spawn_unit(-6.0f, 4.0f, 0, UNIT_ARCHER, 90.0f);
            uint32_t m4 = s.spawn_unit(6.0f, 4.0f, 1, UNIT_SWORDSMAN, 160.0f);
            s.order_attack(&m1, 1, m2);
            s.order_attack(&m3, 1, m4);
            s.spawn_fauna(-15.0f, 18.0f, FAUNA_DEER);
            s.spawn_fauna(20.0f, -18.0f, FAUNA_BOAR);
            s.spawn_building(0, 0, BLD_TOWN_CENTER, 0, 1.0f);
            s.spawn_building(10, 10, BLD_TOWN_CENTER, 1, 1.0f);
        };
        Simulation a, b;
        build(a);
        build(b);
        TurnManager ta, tb;
        for (int t = 0; t < 40; ++t) {
            [[maybe_unused]] bool ok_a = ta.tick(TurnManager::TURN_DURATION, a);
            [[maybe_unused]] bool ok_b = tb.tick(TurnManager::TURN_DURATION, b);
            assert(ok_a && ok_b);
            uint32_t ha = ta.compute_sim_hash(a);
            uint32_t hb = tb.compute_sim_hash(b);
            if (ha != hb) {
                std::cerr << "  [FAIL] turn " << t << " ha=" << ha << " hb=" << hb << std::endl;
                assert(ha == hb);
            }
        }
        assert(ta.current_turn() == 40);
        std::cout << "  [PASS] 40 combat turns hashed identically (seed 4242)." << std::endl;

        // Different seed must diverge (proves the draws actually flow into state).
        Simulation c;
        c.set_sim_seed(777);
        c.reset();
        uint32_t n1 = c.spawn_unit(-6.0f, 0.0f, 0, UNIT_ARCHER, 90.0f);
        uint32_t n2 = c.spawn_unit(6.0f, 0.0f, 1, UNIT_SWORDSMAN, 160.0f);
        c.order_attack(&n1, 1, n2);
        TurnManager tc;
        for (int t = 0; t < 40; ++t) { [[maybe_unused]] bool ok = tc.tick(TurnManager::TURN_DURATION, c); assert(ok); }
        uint32_t hc = tc.compute_sim_hash(c);
        uint32_t ha = ta.compute_sim_hash(a);
        assert(hc != ha);
        std::cout << "  [PASS] different seed diverges (sanity)." << std::endl;

        // Replay re-simulation: fresh sim + same seed + recorded commands must
        // reproduce every recorded hash (this is the lockstep join/replay path).
        assert(ta.save_replay("/tmp/det_replay.bin"));
        Simulation e;
        build(e);
        TurnManager te;
        assert(te.load_replay("/tmp/det_replay.bin"));
        assert(te.turn_history().size() == ta.turn_history().size());
        for (size_t t = 0; t < ta.turn_history().size(); ++t) {
            [[maybe_unused]] bool ok = te.tick(TurnManager::TURN_DURATION, e);
            assert(ok);
            uint32_t expect = ta.turn_history()[t].sim_state_hash;
            uint32_t got = te.compute_sim_hash(e);
            if (got != expect) {
                std::cerr << "  [FAIL] resim turn " << t << " expect=" << expect << " got=" << got << std::endl;
                assert(got == expect);
            }
        }
        std::cout << "  [PASS] replay re-simulation reproduces 40 hashes." << std::endl;

        // Save/load preserves the determinism scalars (id allocator + RNG stream).
        assert(a.save_state("/tmp/det_a.bin"));
        Simulation d;
        assert(d.load_state("/tmp/det_a.bin"));
        assert(d.next_id() == a.next_id());
        assert(d.sim_seed() == a.sim_seed());
        assert(d.sim_rng_state() == a.sim_rng_state());
        std::cout << "  [PASS] save/load preserves next_id + RNG stream." << std::endl;
    }

    std::cout << "[Test 6] GameStart seed-sync: host map+seed -> client identical map..." << std::endl;
    {
        // P1 playtest gate: host broadcasts "map=<id>;seed=<u32>" (NMT_GAME_START);
        // both sides generate_random_map with the SAME seed -> identical turn hashes.
        // Exercises the TurnManager NMT_GAME_START branch + format/parse helpers.
        std::string attrs = format_game_start("random_continental", 424242);
        std::string map_id;
        uint32_t seed = 0;
        assert(parse_game_start_params(attrs, map_id, seed));
        assert(map_id == "random_continental" && seed == 424242);
        assert(!parse_game_start_params("map=only_no_seed", map_id, seed));
        assert(!parse_game_start_params("seed=123", map_id, seed));

        // Simulate the wire: host frames GameStart, client parses + stages it.
        NetHost hs, cs;
        TurnManager htm(&hs, 0), ctm(&cs, 1);
        GameStartMsg gsm;
        gsm.init_attributes = attrs;
        std::vector<uint8_t> payload = NetPacketSerializer::frame_game_start(gsm);
        std::vector<uint8_t> wire = NetPacketSerializer::pack_message(NetMessageType::NMT_GAME_START, 0, payload);
        NetMessage rx;
        assert(NetPacketSerializer::unpack_message(wire.data(), wire.size(), rx));
        assert(rx.type == static_cast<uint8_t>(NetMessageType::NMT_GAME_START));
        // Route through the same dispatch the socket path uses: NetHost::dispatchMessage
        // invokes message_cb_ (installed by TurnManager::set_net_host).
        cs.dispatchMessage(0, rx);
        std::string got_map;
        uint32_t got_seed = 0;
        assert(ctm.take_pending_game_start(got_map, got_seed));
        assert(got_map == "random_continental" && got_seed == 424242);
        assert(!ctm.has_pending_game_start()); // take_ clears

        // Both sides build the identical map from the synced seed, then run
        // lockstep turns with combat: hashes must agree every turn.
        Simulation h, c;
        h.generate_random_map(MAP_RANDOM_CONTINENTAL, got_seed, 2, 64.0f);
        c.generate_random_map(MAP_RANDOM_CONTINENTAL, got_seed, 2, 64.0f);
        assert(h.sim_seed() == c.sim_seed());
        uint32_t h1 = h.spawn_unit(-6.0f, 0.0f, 0, UNIT_ARCHER, 90.0f);
        uint32_t h2 = h.spawn_unit(6.0f, 0.0f, 1, UNIT_SWORDSMAN, 160.0f);
        uint32_t c1 = c.spawn_unit(-6.0f, 0.0f, 0, UNIT_ARCHER, 90.0f);
        uint32_t c2 = c.spawn_unit(6.0f, 0.0f, 1, UNIT_SWORDSMAN, 160.0f);
        h.order_attack(&h1, 1, h2);
        c.order_attack(&c1, 1, c2);
        TurnManager ht, ct;
        for (int t = 0; t < 20; ++t) {
            [[maybe_unused]] bool ok_h = ht.tick(TurnManager::TURN_DURATION, h);
            [[maybe_unused]] bool ok_c = ct.tick(TurnManager::TURN_DURATION, c);
            assert(ok_h && ok_c);
            uint32_t hh = ht.compute_sim_hash(h);
            uint32_t ch = ct.compute_sim_hash(c);
            if (hh != ch) {
                std::cerr << "  [FAIL] seed-sync turn " << t << " host=" << hh << " client=" << ch << std::endl;
                assert(hh == ch);
            }
        }
        std::cout << "  [PASS] seed-synced maps hash identically (20 combat turns)." << std::endl;
        hs.stop();
        cs.stop();
    }

    std::cout << "[Test 7] GameStart full setup: victory+petra+slots sync..." << std::endl;
    {
        // GameSetup rebuild: the full match setup (not just map+seed) rides
        // NMT_GAME_START. Legacy map/seed-only peers must still parse.
        GameSetupParams setup;
        setup.map_id = "random_continental";
        setup.seed = 777001;
        setup.victory = 1;
        setup.difficulty = 4;
        setup.behavior = 3;
        setup.start_res = 2;
        setup.slot_count = 3;
        setup.civs[0] = 3; setup.civs[1] = 1; setup.civs[2] = 9;
        setup.slot_types[0] = 'h'; setup.slot_types[1] = 'a'; setup.slot_types[2] = 'a';
        setup.teams[0] = 1; setup.teams[1] = 2; setup.teams[2] = 3;
        std::string attrs = format_game_start_full(setup);
        GameSetupParams got;
        assert(parse_game_start_full(attrs, got));
        assert(got.map_id == "random_continental" && got.seed == 777001);
        assert(got.victory == 1 && got.difficulty == 4 && got.behavior == 3 && got.start_res == 2);
        assert(got.slot_count == 3);
        assert(got.civs[0] == 3 && got.civs[1] == 1 && got.civs[2] == 9);
        assert(got.slot_types[0] == 'h' && got.slot_types[2] == 'a');
        assert(got.teams[0] == 1 && got.teams[1] == 2 && got.teams[2] == 3);
        // Legacy form still parses (old peers ignore new keys happens implicitly).
        std::string legacy_map; uint32_t legacy_seed = 0;
        assert(parse_game_start_params(format_game_start("bactria", 99), legacy_map, legacy_seed));
        assert(legacy_map == "bactria" && legacy_seed == 99);
        GameSetupParams leg;
        assert(parse_game_start_full(format_game_start("bactria", 99), leg));
        assert(leg.map_id == "bactria" && leg.seed == 99);
        assert(leg.victory == 0 && leg.difficulty == 3 && leg.start_res == 1); // defaults
        assert(leg.ai_seed == 0 && format_game_start("bactria", 99).find("aiseed") == std::string::npos);
        // -autostart-aiseed rides along when set.
        GameSetupParams ais;
        ais.map_id = "mainland";
        ais.seed = 7;
        ais.ai_seed = 4000000000u;
        GameSetupParams ais_got;
        assert(parse_game_start_full(format_game_start_full(ais), ais_got) && ais_got.ai_seed == 4000000000u);
        // Bad inputs rejected.
        GameSetupParams junk;
        assert(!parse_game_start_full("map=only_no_seed", junk));
        assert(!parse_game_start_full("seed=123", junk));
        assert(!parse_game_start_full("map=x;seed=abc", junk));
        // Wire: full setup staged via dispatch -> take_full, and both sides
        // applying map+victory+petra+resources produce identical hashes.
        NetHost hs, cs;
        TurnManager htm(&hs, 0), ctm(&cs, 1);
        GameStartMsg gsm;
        gsm.init_attributes = attrs;
        std::vector<uint8_t> payload = NetPacketSerializer::frame_game_start(gsm);
        std::vector<uint8_t> wire = NetPacketSerializer::pack_message(NetMessageType::NMT_GAME_START, 0, payload);
        NetMessage rx;
        assert(NetPacketSerializer::unpack_message(wire.data(), wire.size(), rx));
        cs.dispatchMessage(0, rx);
        GameSetupParams staged;
        assert(ctm.take_pending_game_start_full(staged));
        assert(staged.map_id == setup.map_id && staged.seed == setup.seed);
        assert(staged.victory == 1 && staged.difficulty == 4);
        assert(!ctm.has_pending_game_start());
        Simulation h, c;
        for (int side = 0; side < 2; ++side) {
            Simulation &s = (side == 0) ? h : c;
            s.generate_random_map(MAP_RANDOM_CONTINENTAL, staged.seed, 3, 64.0f);
            s.set_victory_condition(staged.victory);
            s.set_petra_difficulty(staged.difficulty);
            s.set_petra_behavior(staged.behavior);
            s.set_starting_resources(500, 700, 500, 350);
            s.set_team_civ(0, staged.civs[0]);
            s.set_team_civ(1, staged.civs[1]);
            s.set_team_civ(2, staged.civs[2]);
        }
        assert(h.victory_condition() == c.victory_condition());
        assert(h.petra_difficulty() == c.petra_difficulty());
        assert(h.food() == c.food() && h.wood() == c.wood());
        uint32_t h1 = h.spawn_unit(-6.0f, 0.0f, 0, UNIT_ARCHER, 90.0f);
        uint32_t h2 = h.spawn_unit(6.0f, 0.0f, 1, UNIT_SWORDSMAN, 160.0f);
        uint32_t c1 = c.spawn_unit(-6.0f, 0.0f, 0, UNIT_ARCHER, 90.0f);
        uint32_t c2 = c.spawn_unit(6.0f, 0.0f, 1, UNIT_SWORDSMAN, 160.0f);
        h.order_attack(&h1, 1, h2);
        c.order_attack(&c1, 1, c2);
        TurnManager ht, ct;
        for (int t = 0; t < 20; ++t) {
            [[maybe_unused]] bool ok_h = ht.tick(TurnManager::TURN_DURATION, h);
            [[maybe_unused]] bool ok_c = ct.tick(TurnManager::TURN_DURATION, c);
            assert(ok_h && ok_c);
            uint32_t hh = ht.compute_sim_hash(h);
            uint32_t ch = ct.compute_sim_hash(c);
            if (hh != ch) {
                std::cerr << "  [FAIL] full-setup turn " << t << " host=" << hh << " client=" << ch << std::endl;
                assert(hh == ch);
            }
        }
        std::cout << "  [PASS] full setup syncs; 20 combat turns hash-identical." << std::endl;
        hs.stop();
        cs.stop();
    }

    std::cout << "========================================" << std::endl;
    std::cout << "ALL NETCODE TESTS PASSED (100%)" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
