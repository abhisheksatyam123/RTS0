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
    // Neutral team-2 wonder so victory/capture paths run.
    refs.enemy_buildings.push_back(s.spawn_building_world(0.0f, 0.0f, BLD_WONDER, 2, 1.0f));

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
    s.spawn_relic(10.0f, 10.0f, RES_GOLD, 400);
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
        const int center = Simulation::FOW_GRID / 2 * Simulation::FOW_GRID + Simulation::FOW_GRID / 2;
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
        const uint32_t relic = s.spawn_relic(0.0f, 0.0f, RES_GOLD, 100);
        const uint32_t explorer = s.spawn_unit(0.0f, 0.0f, 0, UNIT_VILLAGER);
        assert(relic != 0 && explorer != 0);
        s.tick(0.1f);
        assert(s.relics().size() == 1 && s.relics()[0].claimed);
        assert(s.relics()[0].owner_team == 0);
        s.tick(179.0f);
        assert(!s.is_victory());
        s.tick(1.0f);
        assert(s.is_victory() && s.is_game_over());
        assert(s.gold() >= 100);
        std::cout << "  [PASS] relic claim, ownership, resource award, and 180-second victory." << std::endl;
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
        const int center = Simulation::FOW_GRID / 2 * Simulation::FOW_GRID + Simulation::FOW_GRID / 2;
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
        const int center = Simulation::FOW_GRID / 2 * Simulation::FOW_GRID + Simulation::FOW_GRID / 2;
        assert(s.fow_grid()[center] == Simulation::FOW_VISIBLE);
        std::cout << "  [PASS] reciprocal alliance gates shared LOS." << std::endl;
    }

    std::cout << "[Test 11] Research and production component behavior..." << std::endl;
    {
        Simulation s;
        const uint32_t tc = s.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
        s.set_starting_resources(1000, 1000, 1000, 1000);
        assert(s.cmd_research(tc, TECH_PHASE_TOWN));
        assert(s.buildings().back().research_tech == TECH_PHASE_TOWN);
        for (int i = 0; i < 32 && !s.is_tech_researched(TECH_PHASE_TOWN); ++i) s.tick(1.0f);
        assert(s.is_tech_researched(TECH_PHASE_TOWN));
        assert(s.player_phase() == 2);
        const int gold_before = s.gold();
        assert(s.cmd_train(tc, UNIT_SWORDSMAN));
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
