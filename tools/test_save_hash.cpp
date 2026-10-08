// Save/load keeps a match in sync: everything TurnManager::compute_sim_hash
// reads must survive Simulation::save_state / load_state.
//
// Plays a busy random-map match (two Petra AIs, armies, projectiles, fields,
// traders), saves at several points, loads each save into a second Simulation
// built the way the game builds one (same map, then load_state), and checks
//   1. the lockstep hash matches right after the load, and
//   2. both copies stay hash-identical while they keep playing.
// When the hash differs, the field-level comparison below names what was lost.
// A PR that adds hashed or per-tick state without saving it fails here.
//
//   g++ -std=c++20 -O2 -Isrc tools/test_save_hash.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/test_save_hash && /tmp/test_save_hash
#include "simulation/simulation.h"
#include "network/turn_manager.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

using namespace rtso;

static int failures = 0;
// One line per differing field: its first mismatch and how many entries differ.
static std::map<std::string, std::pair<int, std::string>> diffs;
static void diff(const char *what, size_t idx, double a, double b) {
	++failures;
	auto &d = diffs[what];
	if (d.first++ == 0) {
		char buf[160];
		std::snprintf(buf, sizeof(buf), "[%zu] saved %.6g, loaded %.6g", idx, a, b);
		d.second = buf;
	}
}
static void print_diffs() {
	for (const auto &kv : diffs)
		std::printf("  DIFF %-28s %4d differ, first %s\n", kv.first.c_str(), kv.second.first, kv.second.second.c_str());
	diffs.clear();
}
#define CMP(what, idx, a, b) do { if (!((a) == (b))) diff(what, (size_t)(idx), (double)(a), (double)(b)); } while (0)

static constexpr uint32_t SEED = 42;
static constexpr int MAP = 5; // random continental, as in bench_match
static constexpr int PLAYERS = 4;
static constexpr float MAP_HALF = 128.0f;

static void build(Simulation &s) {
	s.reset();
	s.set_sim_seed(SEED);
	s.set_waves_enabled(false);
	s.generate_random_map((MapType)MAP, SEED, PLAYERS, MAP_HALF);
	s.set_victory_condition(VICTORY_ENDLESS);
}

static void run(Simulation &s, int ticks) {
	for (int t = 0; t < ticks; ++t) s.tick(Simulation::FIXED_STEP);
}

// Field-level comparison of what compute_sim_hash reads, so a failure names the culprit.
static void compare(const Simulation &a, const Simulation &b) {
	auto ua = a.units(), ub = b.units();
	CMP("unit count", 0, ua.count, ub.count);
	for (size_t i = 0; i < ua.count && i < ub.count; ++i) {
		CMP("unit alive", i, ua.alive[i], ub.alive[i]);
		if (!ua.alive[i]) continue;
		CMP("unit id", i, ua.id[i], ub.id[i]);
		CMP("unit pos_x", i, ua.pos_x[i], ub.pos_x[i]);
		CMP("unit pos_z", i, ua.pos_z[i], ub.pos_z[i]);
		CMP("unit vel_x", i, ua.vel_x[i], ub.vel_x[i]);
		CMP("unit vel_z", i, ua.vel_z[i], ub.vel_z[i]);
		CMP("unit target_x", i, ua.target_x[i], ub.target_x[i]);
		CMP("unit target_z", i, ua.target_z[i], ub.target_z[i]);
		CMP("unit hp", i, ua.hp[i], ub.hp[i]);
		CMP("unit max_hp", i, ua.max_hp[i], ub.max_hp[i]);
		CMP("unit action_state", i, ua.action_state[i], ub.action_state[i]);
		CMP("unit unit_type", i, ua.unit_type[i], ub.unit_type[i]);
		CMP("unit team", i, ua.team[i], ub.team[i]);
		CMP("unit carry", i, ua.carry[i], ub.carry[i]);
		CMP("unit res_type_carried", i, ua.res_type_carried[i], ub.res_type_carried[i]);
		CMP("unit stance", i, ua.stance[i], ub.stance[i]);
		CMP("unit formation", i, ua.formation[i], ub.formation[i]);
		CMP("unit kills", i, ua.kills[i], ub.kills[i]);
		CMP("unit rank", i, ua.rank[i], ub.rank[i]);
		CMP("unit status_effects", i, ua.status_effects[i], ub.status_effects[i]);
		CMP("unit death_timer", i, ua.death_timer[i], ub.death_timer[i]);
		CMP("unit xp", i, ua.xp[i], ub.xp[i]);
		CMP("unit tmpl", i, ua.tmpl[i], ub.tmpl[i]);
	}
	const auto &ba = a.buildings(), &bb = b.buildings();
	CMP("building count", 0, ba.size(), bb.size());
	for (size_t i = 0; i < ba.size() && i < bb.size(); ++i) {
		const auto &x = ba[i], &y = bb[i];
		CMP("building id", i, x.id, y.id);
		CMP("building x", i, x.x, y.x);
		CMP("building z", i, x.z, y.z);
		CMP("building hp", i, x.hp, y.hp);
		CMP("building max_hp", i, x.max_hp, y.max_hp);
		CMP("building build_progress", i, x.build_progress, y.build_progress);
		CMP("building type", i, x.type, y.type);
		CMP("building team", i, x.team, y.team);
		CMP("building capture_points", i, x.capture_points, y.capture_points);
		for (int t = 0; t < 8; ++t) CMP("building capture_points_by_team", i, x.capture_points_by_team[t], y.capture_points_by_team[t]);
		CMP("building max_capture_points", i, x.max_capture_points, y.max_capture_points);
		CMP("building queue size", i, x.queue.size(), y.queue.size());
		CMP("building garrison size", i, x.garrisoned_units.size(), y.garrisoned_units.size());
		CMP("building is_burning", i, x.is_burning, y.is_burning);
		CMP("building upgrade_to", i, x.upgrade_to, y.upgrade_to);
		CMP("building upgrade_timer", i, x.upgrade_timer, y.upgrade_timer);
	}
	auto ra = a.resources(), rb = b.resources();
	CMP("resource count", 0, ra.count, rb.count);
	for (size_t i = 0; i < ra.count && i < rb.count; ++i) {
		CMP("resource id", i, ra.id[i], rb.id[i]);
		CMP("resource x", i, ra.x[i], rb.x[i]);
		CMP("resource z", i, ra.z[i], rb.z[i]);
		CMP("resource amount", i, ra.amount[i], rb.amount[i]);
		CMP("resource alive", i, ra.alive[i], rb.alive[i]);
		CMP("resource type", i, ra.type[i], rb.type[i]);
	}
	for (uint8_t p = 0; p < 8; ++p)
		for (uint8_t q = 0; q < 8; ++q) CMP("diplomacy", p * 8 + q, a.get_diplomacy(p, q), b.get_diplomacy(p, q));
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		const PlayerState &x = a.player(p), &y = b.player(p);
		for (int r = 0; r < RES_COUNT; ++r) CMP("player res", p * 4 + r, x.res[r], y.res[r]);
		CMP("player techs", p, x.techs == y.techs, true);
		CMP("player phase", p, x.phase, y.phase);
		CMP("player civ", p, x.civ, y.civ);
		CMP("player team", p, x.team, y.team);
		CMP("player active", p, x.active, y.active);
		CMP("player is_ai", p, x.is_ai, y.is_ai);
	}
	CMP("participant_mask", 0, a.participant_mask(), b.participant_mask());
	CMP("defeated_mask", 0, a.defeated_mask(), b.defeated_mask());
	CMP("winner_mask", 0, a.winner_mask(), b.winner_mask());
	CMP("game_over", 0, a.is_game_over(), b.is_game_over());
	const auto &pa = a.projectiles(), &pb = b.projectiles();
	CMP("projectile count", 0, pa.size(), pb.size());
	for (size_t i = 0; i < pa.size() && i < pb.size(); ++i) {
		CMP("projectile id", i, pa[i].id, pb[i].id);
		CMP("projectile alive", i, pa[i].alive, pb[i].alive);
		CMP("projectile current_x", i, pa[i].current_x, pb[i].current_x);
		CMP("projectile current_y", i, pa[i].current_y, pb[i].current_y);
		CMP("projectile progress", i, pa[i].progress, pb[i].progress);
		CMP("projectile damage", i, pa[i].damage, pb[i].damage);
		CMP("projectile target_unit_id", i, pa[i].target_unit_id, pb[i].target_unit_id);
	}
	const auto &la = a.relics(), &lb = b.relics();
	CMP("relic count", 0, la.size(), lb.size());
	for (size_t i = 0; i < la.size() && i < lb.size(); ++i) {
		CMP("relic x", i, la[i].x, lb[i].x);
		CMP("relic claimed", i, la[i].claimed, lb[i].claimed);
	}
	CMP("fow_res", 0, a.fow_res(), b.fow_res());
	if (a.fow_res() == b.fow_res()) {
		const int n = a.fow_res() * a.fow_res();
		const uint8_t *fa = a.fow_grid(), *fb = b.fow_grid();
		for (int i = 0; i < n; ++i) CMP("fow cell", i, fa[i], fb[i]);
	}
	const auto &ta = a.territory_grid(), &tb = b.territory_grid();
	CMP("territory size", 0, ta.data.size(), tb.data.size());
	for (size_t i = 0; i < ta.data.size() && i < tb.data.size(); ++i) CMP("territory cell", i, ta.data[i], tb.data[i]);
	for (uint8_t r = 0; r < RES_COUNT; ++r) CMP("barter price", r, a.market_buy_price(r), b.market_buy_price(r));
	CMP("ticks", 0, a.stats().ticks, b.stats().ticks);
	CMP("wave_number", 0, a.wave_number(), b.wave_number());
	CMP("next_id", 0, a.next_id(), b.next_id());
	CMP("sim_seed", 0, a.sim_seed(), b.sim_seed());
	CMP("sim_rng_state", 0, a.sim_rng_state(), b.sim_rng_state());
}

int main() {
	static Simulation live, loaded;
	TurnManager tm;
	build(live);
	const char *path = "/tmp/rts0_test_save_hash.sav";
	// Save points: early game, mid game (armies, fields, traders), later.
	const int save_at[] = {600, 3000, 6000, 12000, 18000};
	const int keep_playing = 600; // 30 s of play after each load
	int played = 0;
	for (int point : save_at) {
		run(live, point - played);
		played = point;
		if (!live.save_state(path)) { std::printf("FAIL save at tick %d\n", point); return 1; }
		build(loaded);
		if (!loaded.load_state(path)) { std::printf("FAIL load at tick %d\n", point); return 1; }

		const int before = failures;
		const uint32_t ha = tm.compute_sim_hash(live), hb = tm.compute_sim_hash(loaded);
		std::printf("tick %5d: %zu units, %zu buildings, hash %08x / %08x after load\n",
		            point, live.units().count, live.buildings().size(), ha, hb);
		if (ha != hb) {
			std::printf("  FAIL hash differs right after load\n");
			++failures;
			compare(live, loaded);
			print_diffs();
		}
		// Both copies keep playing; per-tick state that wasn't saved shows up here.
		static Simulation cont;
		build(cont);
		cont.load_state(path);
		int diverged = -1;
		for (int t = 0; t < keep_playing && failures == before; ++t) {
			live.tick(Simulation::FIXED_STEP);
			cont.tick(Simulation::FIXED_STEP);
			if (tm.compute_sim_hash(live) != tm.compute_sim_hash(cont)) { diverged = t + 1; break; }
		}
		played += keep_playing;
		if (diverged > 0) {
			std::printf("  FAIL the loaded copy diverged %d ticks after the load\n", diverged);
			++failures;
			compare(live, cont);
			print_diffs();
		} else if (failures == before) {
			std::printf("  ok: identical for %d ticks after the load\n", keep_playing);
		}
	}
	std::printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
	return failures ? 1 : 0;
}
