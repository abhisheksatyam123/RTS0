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
// Private access only to stage a defeat and read wonder holds, as test_victory does, and to
// compare the woods' navcells and territory counts, which the hash doesn't read.
#define private public
#include "simulation/simulation.h"
#undef private
#include "network/turn_manager.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

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
		CMP("unit prev_x", i, ua.prev_x[i], ub.prev_x[i]);
		CMP("unit prev_z", i, ua.prev_z[i], ub.prev_z[i]);
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
		CMP("unit pack_state", i, ua.pack_state[i], ub.pack_state[i]);
		CMP("unit pack_elapsed", i, ua.pack_elapsed[i], ub.pack_elapsed[i]);
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
		CMP("resource subtype", i, ra.subtype[i], rb.subtype[i]);
		CMP("resource flora", i, ra.flora[i], rb.flora[i]);
	}
	for (uint8_t p = 0; p < 8; ++p)
		for (uint8_t q = 0; q < 8; ++q) CMP("diplomacy", p * 8 + q, a.get_diplomacy(p, q), b.get_diplomacy(p, q));
	for (uint8_t p = 0; p < 8; ++p)
		for (int r = 0; r < RES_COUNT; ++r) CMP("trading goods", p * RES_COUNT + r, a.trading_goods(p)[r], b.trading_goods(p)[r]);
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		const PlayerState &x = a.player(p), &y = b.player(p);
		for (int r = 0; r < RES_COUNT; ++r) CMP("player res", p * 4 + r, x.res[r], y.res[r]);
		CMP("player techs", p, x.techs == y.techs, true);
		CMP("player phase", p, x.phase, y.phase);
		CMP("player civ", p, x.civ, y.civ);
		CMP("player team", p, x.team, y.team);
		CMP("player active", p, x.active, y.active);
		CMP("player is_ai", p, x.is_ai, y.is_ai);
		CMP("player controls_all", p, x.controls_all, y.controls_all);
	}
	CMP("participant_mask", 0, a.participant_mask(), b.participant_mask());
	CMP("defeated_mask", 0, a.defeated_mask(), b.defeated_mask());
	CMP("winner_mask", 0, a.winner_mask(), b.winner_mask());
	CMP("game_over", 0, a.is_game_over(), b.is_game_over());
	for (int p = 0; p < MAX_PLAYERS; ++p) CMP("wonder_hold", p, a.wonder_hold_[p], b.wonder_hold_[p]);
	const auto &pa = a.projectiles(), &pb = b.projectiles();
	CMP("projectile count", 0, pa.size(), pb.size());
	for (size_t i = 0; i < pa.size() && i < pb.size(); ++i) {
		CMP("projectile id", i, pa[i].id, pb[i].id);
		CMP("projectile alive", i, pa[i].alive, pb[i].alive);
		CMP("projectile current_x", i, pa[i].current_x, pb[i].current_x);
		CMP("projectile current_y", i, pa[i].current_y, pb[i].current_y);
		CMP("projectile progress", i, pa[i].progress, pb[i].progress);
		for (int k = 0; k < 4; ++k) CMP("projectile dmg_parts", i * 4 + k, pa[i].dmg_parts[k], pb[i].dmg_parts[k]);
		CMP("projectile src_tmpl", i, pa[i].src_tmpl, pb[i].src_tmpl);
		CMP("projectile src_type", i, pa[i].src_type, pb[i].src_type);
		CMP("projectile src_kind", i, pa[i].src_kind, pb[i].src_kind);
		CMP("projectile target_unit_id", i, pa[i].target_unit_id, pb[i].target_unit_id);
	}
	const auto &la = a.relics(), &lb = b.relics();
	CMP("relic count", 0, la.size(), lb.size());
	for (size_t i = 0; i < la.size() && i < lb.size(); ++i) {
		CMP("relic x", i, la[i].x, lb[i].x);
		CMP("relic claimed", i, la[i].claimed, lb[i].claimed);
		CMP("relic civ", i, la[i].civ, lb[i].civ);
		CMP("relic capture points", i, la[i].capture_cp, lb[i].capture_cp);
		for (int t = 0; t <= MAX_PLAYERS; ++t) CMP("relic capture points per player", i, la[i].cp[t], lb[i].cp[t]);
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
	CMP("victory_condition", 0, a.victory_condition(), b.victory_condition());
	CMP("waves_enabled", 0, a.waves_enabled(), b.waves_enabled());
	CMP("last_man_standing", 0, a.last_man_standing(), b.last_man_standing());
	CMP("pop_cap", 0, a.pop_cap(), b.pop_cap());
	CMP("ceasefire_seconds", 0, a.ceasefire_seconds(), b.ceasefire_seconds());
	CMP("reveal_map", 0, a.reveal_map(), b.reveal_map());
	CMP("explore_map", 0, a.explore_map(), b.explore_map());
	CMP("wonder_duration", 0, a.wonder_duration(), b.wonder_duration());
	CMP("relic_duration", 0, a.relic_duration(), b.relic_duration());
	CMP("relic_count", 0, a.relic_count(), b.relic_count());
	CMP("ticks", 0, a.stats().ticks, b.stats().ticks);
	CMP("wave_number", 0, a.wave_number(), b.wave_number());
	CMP("next_id", 0, a.next_id(), b.next_id());
	CMP("sim_seed", 0, a.sim_seed(), b.sim_seed());
	CMP("sim_rng_state", 0, a.sim_rng_state(), b.sim_rng_state());
}

// Buildings thread repro: a Town Center and a wall on a fresh sim, saved and
// loaded into a reset() sim (no map built first), must hash the same. Two
// catapults ride along: one stays packed, the other is unpacking at the
// second save and unpacked at the third.
static void check_reset_load(TurnManager &tm, const char *path) {
	static Simulation a, b;
	a.reset();
	a.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 0);
	a.spawn_building_world(20.0f, 0.0f, BLD_WALL_SEGMENT, 0);
	// Game Setup options away from their defaults, so a save that drops them shows up.
	a.set_victory_condition(VICTORY_WONDER);
	a.set_last_man_standing(true);
	a.set_pop_cap(150);
	a.set_player_pop_cap(1, 1000); // a scenario player's own PopulationLimit (v68)
	a.set_ceasefire_seconds(300.0f);
	a.set_explore_map(true);
	a.set_wonder_duration(1200.0f);
	a.set_relic_duration(1200.0f);
	a.set_relic_count(2);
	const uint8_t goods[RES_COUNT] = {10, 20, 30, 40};
	a.cmd_set_trading_goods(1, goods);
	a.set_water_level_mode(Simulation::WATER_LEVEL_DEEP);
	a.set_sea_level_rise_minutes(40);
	{
		// Per-tile terrain textures (v49).
		Simulation::TerrainTiles tt;
		tt.res = 4;
		tt.names = {"medit_grass_field", "medit_rocks"};
		tt.tiles = {0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 0, 1, 0, 0};
		a.set_terrain_tiles(tt);
	}
	a.spawn_unit(-10.0f, 10.0f, 0, UNIT_CATAPULT);
	const uint32_t unpacking = a.spawn_unit(-14.0f, 10.0f, 0, UNIT_CATAPULT);
	a.order_pack(&unpacking, 1, false);
	// Player 1 holds a wonder; player 2 loses its only unit and is defeated. The wonder is
	// built after the first turn: one the map places runs no countdown (WonderVictory.js).
	run(a, 1);
	a.spawn_building_world(-30.0f, -30.0f, BLD_WONDER, 1);
	a.spawn_unit(30.0f, 30.0f, 2, UNIT_SWORDSMAN);
	run(a, 1);
	for (size_t i = 0; i < a.units_.count; ++i)
		if (a.units_.team[i] == 2) { a.units_.hp[i] = 0.0f; a.units_.alive[i] = 0; a.units_.action_state[i] = ACTION_IDLE; }
	run(a, 40);
	const int before = failures;
	for (int ticks : {0, 100, 200}) {
		run(a, ticks);
		if (!a.save_state(path)) { std::printf("FAIL save (reset sim)\n"); ++failures; return; }
		b.reset();
		if (!b.load_state(path)) { std::printf("FAIL load (reset sim)\n"); ++failures; return; }
		const uint32_t ha = tm.compute_sim_hash(a), hb = tm.compute_sim_hash(b);
		const int prior = failures;
		compare(a, b);
		if (ha != hb || failures != prior) {
			++failures;
			std::printf("  FAIL Town Center + wall, %d ticks: hash %08x saved, %08x loaded into a reset sim\n", ticks, ha, hb);
			print_diffs();
		}
	}
	uint8_t states = 0;
	for (size_t i = 0; i < a.units().count; ++i) states |= (uint8_t)(1u << a.units().pack_state[i]);
	if (!(states & (1u << PACK_PACKED)) || !(states & (1u << PACK_UNPACKED))) {
		std::printf("  FAIL expected one packed and one unpacked catapult by the last save\n");
		++failures;
	}
	if (!a.is_defeated(2) || a.is_defeated(1) || a.wonder_hold_[1] <= 0.0f || a.is_game_over()) {
		std::printf("  FAIL expected player 2 defeated and player 1's wonder hold running (hold %.1f s)\n", a.wonder_hold_[1]);
		++failures;
	}
	if (b.water_level_mode() != Simulation::WATER_LEVEL_DEEP || b.sea_level_rise_minutes() != 40) {
		std::printf("  FAIL map trigger Game Setup options not restored (%d, %d)\n", b.water_level_mode(), b.sea_level_rise_minutes());
		++failures;
	}
	if (b.terrain_tiles().res != 4 || b.terrain_tiles().tiles != a.terrain_tiles().tiles || b.terrain_tiles().names != a.terrain_tiles().names) {
		std::printf("  FAIL per-tile terrain not restored (res %d)\n", b.terrain_tiles().res);
		++failures;
	}
	if (failures == before) std::printf("Town Center + wall, packed/unpacked catapults, a defeated player and a wonder hold into a reset sim: ok\n");
}

// A technology queued at a structure is saved with unit_type UNIT_TYPE_TECH. Saves since v48
// wrote 25 there, then UNIT_TYPE_COUNT: such a save's queued technology must load as the same
// technology and finish as one, never as a unit of type 25.
static_assert(UNIT_TYPE_TECH == 25, "saves since v48 mark a queued technology with unit_type 25");
static void check_queued_tech(TurnManager &tm, const char *path) {
	static Simulation a, b;
	a.reset();
	for (float &r : a.players_[0].res) r = 5000.0f;
	const uint32_t cc = a.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
	const uint32_t store = a.spawn_building_world(-20.0f, 0.0f, BLD_STOREHOUSE, 0, 1.0f);
	const int before = failures;
	if (!a.cmd_research(0, store, TECH_WOODCUTTING_1) || !a.cmd_train(0, cc, UNIT_FEMALE_CITIZEN)) {
		std::printf("  FAIL could not queue Iron Axe Heads and a woman\n");
		++failures;
		return;
	}
	run(a, 40);
	if (!a.save_state(path)) { std::printf("FAIL save (queued tech)\n"); ++failures; return; }
	b.reset();
	if (!b.load_state(path)) { std::printf("FAIL load (queued tech)\n"); ++failures; return; }
	const auto &q = b.buildings_[b.building_id_to_idx_[store]].queue;
	if (q.size() != 1 || !q[0].is_tech() || q[0].tech != TECH_WOODCUTTING_1 || q[0].unit_type != UNIT_TYPE_TECH) {
		std::printf("  FAIL the storehouse's queued technology did not load as one (%zu orders)\n", q.size());
		++failures;
	}
	if (tm.compute_sim_hash(a) != tm.compute_sim_hash(b)) {
		std::printf("  FAIL queued tech: hash %08x saved, %08x loaded\n", tm.compute_sim_hash(a), tm.compute_sim_hash(b));
		++failures;
	}
	const size_t units0 = b.units().count;
	for (int t = 0; t < 2400 && !b.players_[0].has_tech(TECH_WOODCUTTING_1); ++t) b.tick(Simulation::FIXED_STEP);
	size_t techs_as_units = 0;
	for (size_t i = 0; i < b.units().count; ++i) techs_as_units += b.units().unit_type[i] == UNIT_TYPE_TECH;
	if (!b.players_[0].has_tech(TECH_WOODCUTTING_1) || techs_as_units || b.units().count > units0 + 1) {
		std::printf("  FAIL after the load: researched %d, %zu units of type 25, %zu new units\n",
		            b.players_[0].has_tech(TECH_WOODCUTTING_1), techs_as_units, b.units().count - units0);
		++failures;
	}
	if (failures == before) std::printf("a queued technology loads as a technology and is researched: ok\n");
}

// Two identical runs must write byte-identical saves: a raw-saved struct's
// uninitialised padding would make them differ (and break bench_sim's state_fnv64).
static std::vector<char> read_file(const char *path) {
	std::vector<char> v;
	if (FILE *f = std::fopen(path, "rb")) {
		char buf[65536];
		size_t n;
		while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) v.insert(v.end(), buf, buf + n);
		std::fclose(f);
	}
	return v;
}
static void check_identical_saves() {
	static Simulation a, b;
	build(a);
	run(a, 3000);
	a.save_state("/tmp/rts0_test_save_a.sav");
	build(b);
	run(b, 3000);
	b.save_state("/tmp/rts0_test_save_b.sav");
	const std::vector<char> fa = read_file("/tmp/rts0_test_save_a.sav"), fb = read_file("/tmp/rts0_test_save_b.sav");
	size_t differ = 0, first = 0;
	for (size_t i = 0; i < fa.size() && i < fb.size(); ++i)
		if (fa[i] != fb[i] && differ++ == 0) first = i;
	if (fa.empty() || fa.size() != fb.size() || differ) {
		std::printf("  FAIL two identical runs wrote different saves: %zu vs %zu bytes, %zu differ, first at %zu\n",
		            fa.size(), fb.size(), differ, first);
		++failures;
	} else {
		std::printf("two identical runs: byte-identical saves (%zu bytes)\n", fa.size());
	}
}

// A save from a map at a non-default size loads into a freshly reset() sim:
// the fog grids are read at the saved map's size, not the reset sim's.
static void check_other_size_load(TurnManager &tm, const char *path) {
	static Simulation a, b;
	a.reset();
	a.set_sim_seed(SEED);
	a.set_waves_enabled(false);
	a.generate_random_map((MapType)MAP, SEED, 2, 192.0f);
	a.set_victory_condition(VICTORY_ENDLESS);
	run(a, 200);
	const int before = failures;
	if (!a.save_state(path)) { std::printf("FAIL save (map half 192)\n"); ++failures; return; }
	b.reset();
	if (!b.load_state(path)) { std::printf("FAIL load (map half 192) into a reset sim\n"); ++failures; return; }
	if (b.map_half() != a.map_half() || b.fow_grid_.size() != a.fow_grid_.size()) {
		std::printf("  FAIL map half %.0f -> %.0f, fog %zu -> %zu cells\n", a.map_half(), b.map_half(),
		            a.fow_grid_.size(), b.fow_grid_.size());
		++failures;
	}
	if (b.terrain_heights() != a.terrain_heights() || b.get_terrain_height(40.0f, -60.0f) != a.get_terrain_height(40.0f, -60.0f)) {
		std::printf("  FAIL the heightmap did not come back with the save (%zu -> %zu heights)\n",
		            a.terrain_heights().size(), b.terrain_heights().size());
		++failures;
	}
	compare(a, b);
	if (tm.compute_sim_hash(a) != tm.compute_sim_hash(b)) {
		std::printf("  FAIL map half 192: hash %08x saved, %08x loaded into a reset sim\n",
		            tm.compute_sim_hash(a), tm.compute_sim_hash(b));
		++failures;
		print_diffs();
	}
	if (failures == before) std::printf("map half 192 into a reset sim: ok\n");
}

// A sim that played a random map and was then reset() must save and load like a
// fresh one: reset() has to clear every bit of hashed state the map left behind.
static void check_reused_sim(TurnManager &tm, const char *path) {
	static Simulation a, b;
	for (int pass = 0; pass < 2; ++pass) {
		Simulation &used = pass ? b : a; // pass 0: the saver was reused; pass 1: the loader
		build(used);
		run(used, 400);
		a.reset();
		a.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 0);
		a.spawn_unit(-10.0f, 10.0f, 0, UNIT_SWORDSMAN);
		a.spawn_unit(10.0f, 10.0f, 1, UNIT_SPEARMAN);
		run(a, 100);
		const int before = failures;
		if (!a.save_state(path)) { std::printf("FAIL save (reused sim)\n"); ++failures; return; }
		b.reset();
		if (!b.load_state(path)) { std::printf("FAIL load (reused sim)\n"); ++failures; return; }
		compare(a, b);
		const uint32_t ha = tm.compute_sim_hash(a), hb = tm.compute_sim_hash(b);
		if (ha != hb || failures != before) {
			++failures;
			std::printf("  FAIL %s sim reused after a random map: hash %08x saved, %08x loaded\n", pass ? "loading" : "saving", ha, hb);
			print_diffs();
		}
		int diverged = -1;
		for (int t = 0; t < 200 && diverged < 0; ++t) {
			a.tick(Simulation::FIXED_STEP);
			b.tick(Simulation::FIXED_STEP);
			if (tm.compute_sim_hash(a) != tm.compute_sim_hash(b)) diverged = t + 1;
		}
		if (diverged > 0) {
			++failures;
			std::printf("  FAIL %s sim reused after a random map: diverged %d ticks after the load\n", pass ? "loading" : "saving", diverged);
			compare(a, b);
			print_diffs();
		}
		if (failures == before) std::printf("%s sim reused after a random map and reset(): ok\n", pass ? "loading" : "saving");
	}
}

// Traders on routes, saved, loaded and saved again: the second save is the first, byte for byte.
// Their routes and legs live in hash maps, whose order follows the order their entries came in;
// the loaded game puts them in in the order the save lists them, so a save written in map order
// would list them otherwise the second time (and a rejoining client's state would differ in bytes).
static void check_trader_save_order(const char *path) {
	static Simulation a, b;
	a.reset();
	const uint32_t m0 = a.spawn_building_world(-60.0f, 0.0f, BLD_MARKET, 0, 1.0f);
	const uint32_t m1 = a.spawn_building_world(60.0f, 0.0f, BLD_MARKET, 0, 1.0f);
	for (int k = 0; k < 4; ++k) {
		const uint32_t trader = a.spawn_unit(-52.0f, -6.0f + 4.0f * (float)k, 0, UNIT_TRADER);
		a.order_trade(&trader, 1, m0, m1);
	}
	run(a, 100);
	if (a.trader_routes_map().size() < 2) {
		std::printf("  FAIL expected at least 2 of 4 traders on routes, %zu\n", a.trader_routes_map().size());
		++failures;
		return;
	}
	if (!a.save_state(path)) { std::printf("FAIL save (traders)\n"); ++failures; return; }
	b.reset();
	if (!b.load_state(path)) { std::printf("FAIL load (traders)\n"); ++failures; return; }
	const std::vector<char> first = read_file(path);
	if (!b.save_state(path) || read_file(path) != first) {
		std::printf("  FAIL %zu traders on routes: the loaded game saves differently\n", a.trader_routes_map().size());
		++failures;
		return;
	}
	std::printf("%zu traders on routes: the loaded game saves the same bytes: ok\n", a.trader_routes_map().size());
}

// A forest map's woods and territory come back with a save as the game that wrote it has them.
// Gaul cavalry and rams walk long paths across the map, through and around the woods, while the
// game is saved every 24 s; each save is loaded into a second sim built the same way, and the two
// play on side by side, their hashes compared every few ticks. Neither the woods' navcells nor the
// territory each player holds is saved: a load works both out again from what is (0.28 rebuilds its
// pathfinder grid from the obstructions on MT_Deserialized, CCmpPathfinder.cpp:222-223, and saves no
// territory, CCmpTerritoryManager.cpp:191), so the live game must hold what a fresh count gives.
// Then a game loaded from a save writes the same save again, byte for byte.
static void check_forest_twin(TurnManager &tm, const char *path) {
	static Simulation live, twin;
	const int maps[] = {MAP_ARDENNES_FOREST, MAP_CALEDONIAN_MEADOWS};
	const int save_every = 480, keep_playing = 200, every = 5;
	for (const int map : maps) {
		auto make = [&](Simulation &s) {
			s.reset();
			s.set_sim_seed(1);
			s.set_waves_enabled(false);
			for (uint8_t p = 0; p < PLAYERS; ++p) s.set_team_civ(p, (uint8_t)(6 + p));
			s.generate_random_map((MapType)map, 1, PLAYERS, MAP_HALF);
			s.set_victory_condition(VICTORY_ENDLESS);
			// Players at peace who build nothing: the woods stand as the map made them.
			for (uint8_t p = 0; p < PLAYERS; ++p) {
				s.set_player_active(p, true, false);
				s.set_player_team(p, 0);
			}
		};
		make(live);
		run(live, 20);
		uint64_t rng = 0x9E3779B97F4A7C15ull ^ (uint64_t)map;
		auto unit = [&]() {
			rng = rng * 6364136223846793005ull + 1442695040888963407ull;
			return (float)((uint32_t)(rng >> 33) & 0xFFFFFF) / (float)0x1000000;
		};
		// A spot a unit can stand on, clear of trees and away from the structures.
		auto open_point = [&](float &x, float &z) {
			for (int tries = 0; tries < 2000; ++tries) {
				x = (unit() * 2.0f - 1.0f) * (MAP_HALF - 4.0f);
				z = (unit() * 2.0f - 1.0f) * (MAP_HALF - 4.0f);
				auto [cx, cy] = live.nav_grid_.world_to_cell(x, z);
				if (!live.nav_grid_.cells.inside(cx, cy) || !live.nav_grid_.is_passable_for(cx, cy, PASS_INFANTRY)) continue;
				if (live.near_impassable_terrain(x, z, 1.2f)) continue;
				std::vector<Simulation::SteerObstacle> near;
				live.gaia_obstacles_near(x, z, 1.5f, near);
				if (!near.empty()) continue;
				bool clear = true;
				for (const auto &b : live.buildings_)
					if ((b.x - x) * (b.x - x) + (b.z - z) * (b.z - z) < 400.0f) { clear = false; break; }
				if (clear) return true;
			}
			return false;
		};
		std::vector<uint32_t> army;
		for (int k = 0; k < 30; ++k) {
			float x, z;
			if (!open_point(x, z)) break;
			const bool ram = k % 5 == 4;
			const uint32_t id = live.spawn_unit(x, z, 1, ram ? UNIT_SIEGE_RAM : UNIT_CAVALRY, 0.0f,
					civ_unit_template_by_id(ram ? "gaul/siege_ram" : "gaul/cavalry_javelineer_b"));
			if (live.lookup_unit(id) >= 0) army.push_back(id);
		}
		const int before = failures;
		int saves = 0, walks = 0;
		for (int wave = 0; wave < 3; ++wave) {
			for (uint32_t &id : army) {
				float x, z;
				if (live.lookup_unit(id) < 0 || !open_point(x, z)) continue;
				live.order_walk(&id, 1, x, z);
				++walks;
			}
			for (int t = 1; t <= 2 * save_every; ++t) {
				live.tick(Simulation::FIXED_STEP);
				if (t % save_every) continue;
				++saves;
				if (!live.save_state(path)) { std::printf("FAIL save (forest map %d)\n", map); ++failures; return; }
				make(twin);
				if (!twin.load_state(path)) { std::printf("FAIL load (forest map %d)\n", map); ++failures; return; }
				const std::pair<bool, const char *> derived[] = {
						{live.forest_cells_ != twin.forest_cells_, "the woods' navcells"},
						{live.nav_grid_.cells != twin.nav_grid_.cells, "the navcells"},
						{live.territory_tiles_ != twin.territory_tiles_, "the territory each player holds"}};
				for (const auto &[differ, what] : derived)
					if (differ) {
						std::printf("  FAIL forest map %d, save %d (tick %llu): %s differ after the load\n", map, saves,
						            (unsigned long long)live.ticks_, what);
						++failures;
					}
				const std::vector<char> a = read_file(path);
				if (!twin.save_state(path) || read_file(path) != a) {
					std::printf("  FAIL forest map %d, save %d: the loaded game saves differently\n", map, saves);
					++failures;
				}
				// The live game plays on through the comparison: the twin has to keep up with it.
				int diverged = tm.compute_sim_hash(live) != tm.compute_sim_hash(twin) ? 0 : -1;
				for (int k = 1; k <= keep_playing && diverged < 0; ++k) {
					live.tick(Simulation::FIXED_STEP);
					twin.tick(Simulation::FIXED_STEP);
					if (k % every == 0 && tm.compute_sim_hash(live) != tm.compute_sim_hash(twin)) diverged = k;
				}
				if (diverged >= 0) {
					std::printf("  FAIL forest map %d, save %d (tick %llu): the loaded copy diverged %d ticks after the load\n",
					            map, saves, (unsigned long long)live.ticks_, diverged);
					++failures;
					compare(live, twin);
					print_diffs();
				}
			}
		}
		if (failures == before)
			std::printf("forest map %d: %zu cavalry and rams, %d walks, %d saves, each identical for %d ticks: ok\n", map,
			            army.size(), walks, saves, keep_playing);
	}
}

int main() {
	static Simulation live, loaded;
	TurnManager tm;
	check_reused_sim(tm, "/tmp/rts0_test_save_hash.sav");
	check_identical_saves();
	check_queued_tech(tm, "/tmp/rts0_test_save_hash.sav");
	check_reset_load(tm, "/tmp/rts0_test_save_hash.sav");
	check_other_size_load(tm, "/tmp/rts0_test_save_hash.sav");
	check_trader_save_order("/tmp/rts0_test_save_hash.sav");
	check_forest_twin(tm, "/tmp/rts0_test_save_hash.sav");
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
		// The loaded game writes the same save again (traders and all): a save written in the order
		// a hash map happens to hold would differ between the two.
		const std::vector<char> saved = read_file(path);
		if (!loaded.save_state("/tmp/rts0_test_save_hash_2.sav") || read_file("/tmp/rts0_test_save_hash_2.sav") != saved) {
			std::printf("  FAIL the loaded game saves differently\n");
			++failures;
		}
		std::remove("/tmp/rts0_test_save_hash_2.sav");
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
