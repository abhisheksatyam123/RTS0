// RTS0 combat soak: a headless Petra-vs-Petra match that logs units which look
// broken. Each slot is a seated Petra; the map, civs, size and length come from
// the command line. Every simulated second it checks:
//   - stuck: has an order and a moving action but stays within 1 unit for 60 s;
//   - idle under order: ACTION_IDLE with an order kind set for 30 s;
//   - hits nothing: ACTION_ATTACK on one target for 60 s while the target's
//     health never drops (no one is hurting it, this unit included);
//   - siege that never fires: a siege engine with 120 s in ACTION_ATTACK and
//     no drop in any of its targets' health while it attacked;
//   - garrisoned for good: inside a holder for 15 min;
//   - ships that never land: a ship with passengers aboard for 180 s.
// --twin runs a second copy of the match and compares sim hashes every minute
// (determinism); --save-at M saves at minute M, loads into a third copy and
// compares that too. A crash or assert shows up as the process's exit code.
// Every report names the unit, its order and target, the trees within 1.5 units and,
// for a structure, whether it is on the unit's land. --watch ID --watch-from S prints
// unit ID every second from S on, and a minute later dumps what is around it.
// --json NAME plays data/maps/NAME.json (loaded as tools/probe_trigger_maps.cpp does) with
// --civs seated instead of the map's. It also logs, for a sweep (tools/sweep_matches.py):
//   - [slow]: a game minute that took more than 20 s of wall time (a stall);
//   - [spike]: a turn of the main copy that took more than its 200 ms of game time, and
//     [perf] every 5 minutes with the worst and mean turn since the last one, unit slots and
//     resident memory, and [mem] every 10 minutes with the containers that have more than
//     doubled since minute 10 (--diffs a,b sets the
//     Petra difficulty per seat, 0 Sandbox to 5 Very Hard);
//   - [phase]: each phase-up, and [no-phase] for a player still in Village at the end of a
//     match of 15+ minutes;
//   - [stats]: statistics that go wrong (a counter that drops, trained units below the
//     living units bought since the start, map explored or controlled outside 0-100, a missing 30 s sample).
// Build (one line):
//   g++ -std=c++20 -O2 -Isrc tools/soak_match.cpp src/simulation/simulation.cpp
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp
//       src/network/net_host.cpp -lpthread -o /tmp/soak_match
//   /tmp/soak_match --map 15 --civs 0,1,2,3 --minutes 30 --size 256 --seed 7 --twin
#define private public
#include "simulation/simulation.h"
#undef private
#include "network/turn_manager.h"
#define PROBE_LOADER_ONLY
#include "probe_trigger_maps.cpp"
#undef PROBE_LOADER_ONLY

#include <chrono>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using namespace rtso;

namespace {

struct Track {
	float ax = 0, az = 0;      // where the stuck window started
	int still_s = 0;           // seconds with an order and no movement
	int idle_order_s = 0;      // seconds idle with an order kind set
	uint32_t tgt = 0;          // attack target being watched
	float tgt_hp = 0;          // its health when the window started
	float tgt_cp = 0;          // the owner's capture points of a building target then
	int no_hurt_s = 0;         // seconds attacking it with no health drop
	int siege_attack_s = 0;    // siege: seconds in ACTION_ATTACK
	bool siege_hurt = false;   // siege: a target lost health while it attacked
	int garrison_s = 0;        // seconds inside a holder
	int laden_s = 0;           // ship: seconds with passengers aboard
	bool reported[6] = {};
};

const char *action_name(uint8_t a) {
	static const char *n[] = {"idle", "move", "attack", "gather", "return", "build", "garrisoned", "trade",
			"dying", "attack-move", "patrol", "guard", "flee", "heal", "cheer", "repair"};
	return a < sizeof(n) / sizeof(n[0]) ? n[a] : "?";
}

bool is_siege(uint8_t t) {
	return unit_is_siege_engine(t);
}

// Resident memory in MB (Linux /proc), 0 where unavailable.
double rss_mb() {
	std::ifstream f("/proc/self/statm");
	long pages = 0, resident = 0;
	if (!(f >> pages >> resident)) return 0.0;
	return resident * 4096.0 / (1024.0 * 1024.0);
}

// The simulation's growable containers, by size: what a late-game leak would grow.
std::vector<std::pair<const char *, size_t>> container_sizes(const Simulation &s) {
	return {
		{"unit_upgrades_", s.unit_upgrades_.size()},
		{"unit_queues_", s.unit_queues_.size()},
		{"unit_autoqueue_", s.unit_autoqueue_.size()},
		{"attacked_events_", s.attacked_events_.size()},
		{"ai_command_events_", s.ai_command_events_.size()},
		{"pending_attack_alerts_", s.pending_attack_alerts_.size()},
		{"fauna_strikes_", s.fauna_strikes_.size()},
		{"session_notices_", s.session_notices_.size()},
		{"transport_passengers_", s.transport_passengers_.size()},
		{"flight_", s.flight_.size()},
		{"turret_height_", s.turret_height_.size()},
		{"rampart_turrets_", s.rampart_turrets_.size()},
		{"turret_prev_stance_", s.turret_prev_stance_.size()},
		{"formations_", s.formations_.size()},
		{"formation_group_", s.formation_group_.size()},
		{"formation_slot_walk_", s.formation_slot_walk_.size()},
		{"buildings_", s.buildings_.size()},
		{"projectiles_", s.projectiles_.size()},
		{"statuses_", s.statuses_.size()},
		{"relics_", s.relics_.size()},
		{"treasures_", s.treasures_.size()},
		{"pending_alerts_", s.pending_alerts_.size()},
		{"unit_id_to_idx_", s.unit_id_to_idx_.size()},
		{"unit_idx_by_id_", s.unit_idx_by_id_.size()},
		{"field_res_by_bld_", s.field_res_by_bld_.size()},
		{"field_bld_by_res_", s.field_bld_by_res_.size()},
		{"field_gatherers_", s.field_gatherers_.size()},
		{"fish_gatherers_", s.fish_gatherers_.size()},
		{"supply_gatherers_", s.supply_gatherers_.size()},
		{"scenery_", s.scenery_.size()},
		{"petra_place_misses_", s.petra_place_misses_.size()},
		{"trader_routes_", s.trader_routes_.size()},
		{"trader_leg_", s.trader_leg_.size()},
		{"trader_goods_", s.trader_goods_.size()},
		{"trader_route_points_", s.trader_route_points_.size()},
		{"trader_route_idx_", s.trader_route_idx_.size()},
		{"trigger_dialogs_", s.trigger_dialogs_.size()},
		{"spies_", s.spies_.size()},
		{"stat_samples_", s.stat_samples_.size()},
		{"petra_started_attacks_", s.petra_started_attacks_.size()},
		{"petra_defense_armies_", s.petra_defense_armies_.size()},
		{"petra_transports_", s.petra_transports_.size()},
		{"petra_chat_", s.petra_chat_.size()},
		{"petra_diplomacy_requests_", s.petra_diplomacy_requests_.size()},
		{"petra_bases_", s.petra_bases_.size()},
		{"petra_fnd_watch_", s.petra_fnd_watch_.size()},
		{"petra_hunt_search_", s.petra_hunt_search_.size()},
		{"petra_move_to_gatherer_", s.petra_move_to_gatherer_.size()},
		{"petra_supply_", s.petra_supply_.size()},
		{"petra_targeted_relics_", s.petra_targeted_relics_.size()},
		{"petra_approach_", s.petra_approach_.size()},
		{"petra_inaccessible_", s.petra_inaccessible_.size()},
		{"petra_base_dropsite_checks_", s.petra_base_dropsite_checks_.size()},
		{"cc_captures_", s.cc_captures_.size()},
		{"petra_hit_events_", s.petra_hit_events_.size()},
		{"los_unit_src_", s.los_unit_src_.size()},
		{"los_bld_src_", s.los_bld_src_.size()},
		{"los_fauna_src_", s.los_fauna_src_.size()},
		{"los_spy_src_", s.los_spy_src_.size()},
		{"los_garrison_src_", s.los_garrison_src_.size()},
		{"petra_slots_", s.petra_slots_.size()},
		{"petra_ev_attacked_", s.petra_ev_attacked_.size()},
		{"petra_ev_captures_", s.petra_ev_captures_.size()},
		{"petra_ev_decay_", s.petra_ev_decay_.size()},
		{"petra_attack_requests_", s.petra_attack_requests_.size()},
		{"petra_turn_attacks_", s.petra_turn_attacks_.size()},
		{"petra_turn_attacker_", s.petra_turn_attacker_.size()},
		{"petra_turn_captures_", s.petra_turn_captures_.size()},
		{"petra_turn_decay_", s.petra_turn_decay_.size()},
		{"unit_paths_", s.unit_paths_.size()},
		{"steer_obstacles_", s.steer_obstacles_.size()},
		{"short_path_search_", s.short_path_search_.size()},
		{"sep_obstacles_", s.sep_obstacles_.size()},
		{"mine_obstacles_", s.mine_obstacles_.size()},
		{"gaia_obstacles_", s.gaia_obstacles_.size()},
		{"gaia_res_idx_", s.gaia_res_idx_.size()},
		{"mine_candidates_", s.mine_candidates_.size()},
		{"gate_pass_memo_", s.gate_pass_memo_.size()},
		{"capture_attack_", s.capture_attack_.size()},
		{"order_queues_", s.order_queues_.size()},
		{"guard_of_", s.guard_of_.size()},
		{"locked_dropsite_units_", s.locked_dropsite_units_.size()},
		{"work_orders_", s.work_orders_.size()},
		{"closed_gates_", s.closed_gates_.size()},
	};
}

float target_hp(const Simulation &s, uint32_t uid, uint32_t bid) {
	if (uid) {
		const int32_t j = s.lookup_unit(uid);
		if (j >= 0 && s.units_.alive[j]) return s.units_.hp[j];
		if (s.is_fauna_id(uid)) {
			for (size_t f = 0; f < s.fauna_.count; ++f)
				if (s.fauna_.id[f] == uid) return s.fauna_.hp[f];
		}
		return -1.0f;
	}
	if (bid) {
		auto it = s.building_id_to_idx_.find(bid);
		if (it != s.building_id_to_idx_.end()) return s.buildings_[it->second].hp;
	}
	return -1.0f;
}

// The owner's capture points of a building target, which a capture attack lowers. Kept apart
// from health: slow damage while the owner regenerates points must not read as no progress.
float target_cp(const Simulation &s, uint32_t bid) {
	auto it = bid ? s.building_id_to_idx_.find(bid) : s.building_id_to_idx_.end();
	if (it == s.building_id_to_idx_.end()) return -1.0f;
	const auto &b = s.buildings_[it->second];
	return b.team < MAX_PLAYERS ? b.capture_points_by_team[b.team] : -1.0f;
}

// data/maps/NAME.json with the map's own players, resources and teams; returns the players seated.
int setup_json(Simulation &s, const std::string &name, uint32_t seed, const std::vector<int> &civs, const std::vector<int> &diffs) {
	MapPlayerOverrides ov;
	g_civ_override = civs;
	if (!load_json_map(s, name, seed, ov)) return 0;
	const int players = std::max(1, ov.players);
	s.set_victory_condition(VICTORY_CONQUEST);
	s.set_waves_enabled(false);
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		if (ov.civ[p] >= 0) s.set_team_civ(p, (uint8_t)ov.civ[p]);
		if (ov.has_res[p]) s.set_player_resources(p, ov.res[p][0], ov.res[p][1], ov.res[p][3], ov.res[p][2]);
		s.set_player_pop_cap(p, ov.pop[p]);
	}
	if (ov.pop_limit > 0) s.set_pop_cap(ov.pop_limit);
	if (ov.has_teams) { s.set_setup_teams(ov.team); s.apply_team_diplomacy(); }
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) s.set_player_active(p, p < players, p < players);
	for (int p = 0; p < players; ++p) s.set_petra_difficulty((uint8_t)p, (uint8_t)diffs[p % diffs.size()]);
	return players;
}

void setup(Simulation &s, int map, uint32_t seed, int players, float size, const std::vector<int> &civs, bool circular,
		const std::vector<int> &diffs) {
	s.reset();
	s.set_sim_seed(seed);
	s.set_waves_enabled(false);
	if (circular) s.set_setup_flag(Simulation::SETUP_CIRCULAR_MAP, true);
	for (int p = 0; p < players; ++p) s.set_team_civ((uint8_t)p, (uint8_t)civs[p % civs.size()]);
	s.generate_random_map((MapType)map, seed, players, size * 0.5f);
	s.set_victory_condition(VICTORY_CONQUEST);
	for (int p = 0; p < players; ++p) s.set_team_civ((uint8_t)p, (uint8_t)civs[p % civs.size()]);
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) s.set_player_active(p, p < players, p < players);
	for (int p = 0; p < players; ++p) s.set_petra_difficulty((uint8_t)p, (uint8_t)diffs[p % diffs.size()]);
}

void turn(Simulation &s) {
	for (float left = 0.2f; left > 1e-4f; left -= Simulation::FIXED_STEP) s.tick(Simulation::FIXED_STEP);
}

} // namespace

int main(int argc, char **argv) {
	int map = MAP_MAINLAND, players = 4;
	uint32_t seed = 7;
	float size = 256.0f;
	double minutes = 30.0;
	bool twin = false, circular = false;
	double save_at = -1.0;
	uint32_t watch = 0;
	int watch_from = 0;
	std::vector<int> civs = {0, 1, 2, 3};
	std::string json;
	std::vector<int> diffs = {3};
	for (int i = 1; i < argc; ++i) {
		auto next = [&]() { return i + 1 < argc ? argv[++i] : (char *)"0"; };
		if (!strcmp(argv[i], "--map")) map = atoi(next());
		else if (!strcmp(argv[i], "--json")) json = next();
		else if (!strcmp(argv[i], "--seed")) seed = (uint32_t)strtoul(next(), nullptr, 10);
		else if (!strcmp(argv[i], "--size")) size = (float)atof(next());
		else if (!strcmp(argv[i], "--minutes")) minutes = atof(next());
		else if (!strcmp(argv[i], "--players")) players = atoi(next());
		else if (!strcmp(argv[i], "--twin")) twin = true;
		else if (!strcmp(argv[i], "--circular")) circular = true;
		else if (!strcmp(argv[i], "--save-at")) save_at = atof(next());
		else if (!strcmp(argv[i], "--watch")) watch = (uint32_t)strtoul(next(), nullptr, 10);
		else if (!strcmp(argv[i], "--watch-from")) watch_from = atoi(next());
		else if (!strcmp(argv[i], "--diffs")) {
			diffs.clear();
			for (char *t = strtok(next(), ","); t; t = strtok(nullptr, ",")) diffs.push_back(atoi(t));
		} else if (!strcmp(argv[i], "--civs")) {
			civs.clear();
			for (char *t = strtok(next(), ","); t; t = strtok(nullptr, ",")) civs.push_back(atoi(t));
		}
	}

	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	auto make = [&]() {
		auto s = std::make_unique<Simulation>();
		if (json.empty()) setup(*s, map, seed, players, size, civs, circular, diffs);
		else players = setup_json(*s, json, seed, civs, diffs);
		return s;
	};
	auto a = make();
	if (!players) return 2;
	std::unique_ptr<Simulation> b, c;
	if (twin) b = make();
	TurnManager tm;
	const std::string map_name = json.empty() ? std::string(map_type_info((uint8_t)map).id) : json;
	std::printf("soak map=%s seed=%u size=%.0f players=%d civs=", map_name.c_str(), seed, size, players);
	for (int p = 0; p < players; ++p) std::printf("%s%d", p ? "," : "", civs[p % civs.size()]);
	std::printf(" diffs=");
	for (int p = 0; p < players; ++p) std::printf("%s%d", p ? "," : "", diffs[p % diffs.size()]);
	std::printf(" circular=%d\n", circular);
	for (const std::string &u : g_unported_scripts) std::printf("[unported] trigger script %s\n", u.c_str());

	std::unordered_map<uint32_t, Track> tr;
	std::map<std::string, int> counts;
	int desyncs = 0;

	// Phase-ups and statistics (the counters StatisticsTracker keeps for the summary).
	uint8_t phase_seen[MAX_PLAYERS] = {};
	for (int p = 0; p < players; ++p) phase_seen[p] = a->player((uint8_t)p).phase;
	std::vector<PlayerStatistics> prev_stats(players);
	for (int p = 0; p < players; ++p) prev_stats[p] = a->statistics((uint8_t)p);
	int start_units[MAX_PLAYERS] = {}; // after the first turn (nomad maps spawn them in it), garrisoned ones too
	std::map<std::string, int> stat_reported;
	auto stat_issue = [&](int sec, int p, const std::string &key, const char *fmt, auto... args) {
		if (stat_reported[key + std::to_string(p)]++) return;
		++counts["stats"];
		std::printf("[stats] t=%ds p%d ", sec, p);
		std::printf(fmt, args...);
		std::printf("\n");
	};
	auto wall_minute = std::chrono::steady_clock::now();
	double slowest_minute = 0.0;

	const int turns = (int)(minutes * 60.0 / 0.2 + 0.5);
	double slowest_turn = 0.0, minute_worst_turn = 0.0, window_ms = 0.0;
	int window_turns = 0;
	// Late-game drift: mean turn time and memory per 10 minutes, and containers that keep
	// growing ([mem]); the first sample is taken at 10 minutes, once the start has settled.
	std::vector<std::pair<const char *, size_t>> mem_first;
	double rss_first = 0.0;
	int spikes = 0;
	for (int t = 1; t <= turns; ++t) {
		const auto turn_start = std::chrono::steady_clock::now();
		turn(*a);
		const double turn_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - turn_start).count();
		minute_worst_turn = std::max(minute_worst_turn, turn_ms);
		window_ms += turn_ms;
		++window_turns;
		slowest_turn = std::max(slowest_turn, turn_ms);
		if (turn_ms > 200.0 && spikes++ < 20) {
			// A 200 ms turn takes longer than the game time it covers: a hitch in a real match.
			std::printf("[spike] t=%.1fs %.0f ms for one turn, %zu units %zu buildings\n", t * 0.2, turn_ms, a->units_.count, a->buildings_.size());
			++counts["spike"];
		}
		if (b) turn(*b);
		if (c) turn(*c);
		if (t == 1)
			for (size_t i = 0; i < a->units_.count; ++i)
				if ((a->units_.alive[i] || a->units_.action_state[i] == ACTION_GARRISONED) && a->units_.team[i] < players)
					++start_units[a->units_.team[i]];
		if (t % 5) continue;
		const int sec = t / 5;
		Simulation &s = *a;
		if (save_at >= 0.0 && !c && sec >= (int)(save_at * 60.0)) {
			const std::string path = "/tmp/soak_" + std::to_string(seed) + "_" + std::to_string(map) + ".sav";
			c = std::make_unique<Simulation>();
			const bool saved = s.save_state(path.c_str());
			if (!saved || !c->load_state(path.c_str())) {
				size_t alive = 0, garrisoned = 0, dying = 0, gaia = 0;
				for (size_t i = 0; i < s.units_.count; ++i) {
					alive += s.units_.alive[i];
					garrisoned += s.units_.action_state[i] == ACTION_GARRISONED;
					dying += s.units_.action_state[i] == ACTION_DYING;
					gaia += s.units_.team[i] == GAIA_PLAYER;
				}
				std::printf("[desync] t=%ds %s failed (%zu unit slots: %zu alive, %zu garrisoned, %zu dying, %zu Gaia; %zu buildings)\n",
						sec, saved ? "load" : "save", s.units_.count, alive, garrisoned, dying, gaia, s.buildings_.size());
				++desyncs;
				c.reset();
				save_at = -1.0;
			} else if (tm.compute_sim_hash(s) != tm.compute_sim_hash(*c)) {
				std::printf("[desync] t=%ds loaded copy hashes differently right after load\n", sec);
				++desyncs;
			}
		}
		for (int p = 0; p < players; ++p) {
			const uint8_t ph = s.player((uint8_t)p).phase;
			if (ph != phase_seen[p]) {
				std::printf("[phase] t=%ds p%d %d -> %d\n", sec, p, phase_seen[p], ph);
				phase_seen[p] = ph;
			}
		}
		if (sec % 60 == 0) {
			const auto now = std::chrono::steady_clock::now();
			const double wall = std::chrono::duration<double>(now - wall_minute).count();
			wall_minute = now;
			slowest_minute = std::max(slowest_minute, wall);
			if (sec % 300 == 0) {
				size_t alive = 0;
				for (size_t i = 0; i < s.units_.count; ++i) alive += s.units_.alive[i];
				size_t wrecked = 0;
				for (const auto &b : s.buildings_) wrecked += b.wrecked;
				std::printf("[perf] t=%dm worst turn %.1f ms, mean %.2f ms, %zu living units, %zu unit slots, %zu buildings (%zu wrecked), rss %.0f MB\n",
						sec / 60, minute_worst_turn, window_turns ? window_ms / window_turns : 0.0, alive, s.units_.count,
						s.buildings_.size(), wrecked, rss_mb());
				minute_worst_turn = 0.0;
				window_ms = 0.0;
				window_turns = 0;
				if (sec % 600 == 0) {
					auto now_sizes = container_sizes(s);
					if (mem_first.empty()) {
						mem_first = now_sizes;
						rss_first = rss_mb();
					} else {
						std::printf("[mem] t=%dm rss %.0f MB (%+.0f since 10m);", sec / 60, rss_mb(), rss_mb() - rss_first);
						for (size_t k = 0; k < now_sizes.size(); ++k)
							if (now_sizes[k].second >= 256 && now_sizes[k].second > 2 * mem_first[k].second + 64)
								std::printf(" %s %zu (was %zu)", now_sizes[k].first, now_sizes[k].second, mem_first[k].second);
						std::printf("\n");
					}
				}
			}
			if (wall > 20.0 * (1 + (b != nullptr) + (c != nullptr))) {
				std::printf("[slow] t=%dm %.1f s of wall time for the last game minute\n", sec / 60, wall);
				++counts["slow"];
			}
			for (int p = 0; p < players; ++p) {
				const PlayerStatistics st = s.statistics((uint8_t)p);
				const PlayerStatistics &o = prev_stats[p];
				auto drop = [&](const char *name, uint32_t now_v, uint32_t was) {
					if (now_v < was) stat_issue(sec, p, name, "%s dropped %u -> %u", name, was, now_v);
				};
				const int T = STAT_UNIT_SLOTS - 1, TB = STAT_BLD_SLOTS - 1;
				drop("unitsTrained", st.units_trained[T], o.units_trained[T]);
				drop("unitsLost", st.units_lost[T], o.units_lost[T]);
				drop("enemyUnitsKilled", st.enemy_units_killed[T], o.enemy_units_killed[T]);
				drop("buildingsConstructed", st.buildings_constructed[TB], o.buildings_constructed[TB]);
				drop("buildingsLost", st.buildings_lost[TB], o.buildings_lost[TB]);
				drop("enemyBuildingsDestroyed", st.enemy_buildings_destroyed[TB], o.enemy_buildings_destroyed[TB]);
				for (int r = 0; r < RES_COUNT; ++r) {
					drop("resourcesGathered", st.resources_gathered[r], o.resources_gathered[r]);
					drop("resourcesSold", st.resources_sold[r], o.resources_sold[r]);
					drop("resourcesBought", st.resources_bought[r], o.resources_bought[r]);
				}
				drop("tradeIncome", st.trade_income, o.trade_income);
				drop("treasuresCollected", st.treasures_collected, o.treasures_collected);
				drop("lootCollected", st.loot_collected, o.loot_collected);
				drop("tributesSent", st.tributes_sent, o.tributes_sent);
				// Every living unit was there at the start, trained or captured (plus spawned by a
				// trigger, which only scenario maps do): fewer is a missed unitsTrained.
				int alive = 0;
				for (size_t i = 0; i < s.units_.count; ++i)
					if ((s.units_.alive[i] || s.units_.action_state[i] == ACTION_GARRISONED) && s.units_.team[i] == p) ++alive;
				const int lost = (int)st.units_lost[T];
				if (alive + lost > start_units[p] + (int)st.units_trained[T] + (int)st.units_captured[T] + (int)st.livestock_bred + 2)
					stat_issue(sec, p, "unitsTrainedLow", "living %d + lost %d > start %d + trained %u + captured %u + bred %u",
							alive, lost, start_units[p], st.units_trained[T], st.units_captured[T], st.livestock_bred);
				const float ex = s.percent_map_explored((uint8_t)p), ct = s.percent_map_controlled((uint8_t)p);
				if (!(ex >= 0.0f && ex <= 100.0f) || !(ct >= 0.0f && ct <= 100.0f))
					stat_issue(sec, p, "percent", "explored %.1f controlled %.1f outside 0-100", ex, ct);
				if (st.peak_percent_map_controlled + 0.01f < ct)
					stat_issue(sec, p, "peak", "peak controlled %.1f below controlled %.1f", st.peak_percent_map_controlled, ct);
				prev_stats[p] = st;
			}
			const size_t want = (size_t)(sec / (int)STATISTICS_SAMPLE_INTERVAL);
			const size_t have = s.statistics_samples().size();
			if (have + 1 < want || have > want + 1)
				stat_issue(sec, 0, "samples", "%zu statistics samples after %d s (want about %zu)", have, sec, want);
			const uint32_t h = tm.compute_sim_hash(s);
			if (b && tm.compute_sim_hash(*b) != h) {
				std::printf("[desync] t=%ds twin hash differs\n", sec);
				++desyncs;
				b.reset();
			}
			if (c && tm.compute_sim_hash(*c) != h) {
				std::printf("[desync] t=%ds save/load copy hash differs\n", sec);
				++desyncs;
				c.reset();
			}
		}
		if (sec % 300 == 0) {
			std::printf("[sample] t=%dm", sec / 60);
			for (int p = 0; p < players; ++p) {
				int units = 0, bld = 0;
				for (size_t i = 0; i < s.units_.count; ++i) units += s.units_.alive[i] && s.units_.team[i] == p;
				for (const auto &bb : s.buildings_) bld += bb.team == p && bb.hp > 0;
				std::printf(" | p%d %s units=%d bld=%d phase=%d", p, s.is_defeated((uint8_t)p) ? "DEFEATED" : "", units, bld,
						s.player((uint8_t)p).phase);
			}
			std::printf("\n");
		}
		if (watch && sec >= watch_from) {
			const int32_t w = s.lookup_unit(watch);
			if (w >= 0) {
				const size_t i = (size_t)w;
				const size_t np = i < s.unit_paths_.size() ? (size_t)(s.unit_paths_[i].count - s.unit_paths_[i].cur) : 0;
				std::printf("[watch] t=%ds pos=(%.2f,%.2f) vel=(%.2f,%.2f) act=%s order=%d/%u flags=%d has=%d goal=(%.1f,%.1f) wp=%d/%d path=%zu tu=%u tb=%u stance=%d\n",
						sec, s.units_.pos_x[i], s.units_.pos_z[i], s.units_.vel_x[i], s.units_.vel_z[i],
						action_name(s.units_.action_state[i]), s.units_.order_kind[i], s.units_.order_target[i],
						s.units_.order_flags[i], s.units_.has_order[i], s.units_.target_x[i], s.units_.target_z[i],
						s.units_.waypoint_cur[i], s.units_.waypoint_count[i], np, s.units_.target_unit_id[i],
						s.units_.target_building_id[i], s.units_.stance[i]);
				if (const uint32_t tu = s.units_.target_unit_id[i]) {
					const int32_t t = s.lookup_unit(tu);
					if (t >= 0)
						std::printf("   tu pos=(%.2f,%.2f) act=%s hp=%.1f alive=%d h=%.2f my_h=%.2f atk_timer=%.2f pack=%d\n", s.units_.pos_x[t], s.units_.pos_z[t],
								action_name(s.units_.action_state[t]), s.units_.hp[t], (int)s.units_.alive[t], s.unit_height((size_t)t), s.unit_height(i),
								s.units_.attack_timer[i], (int)s.units_.pack_state[i]);
				}
				if (const uint32_t tb = s.units_.target_building_id[i]) {
					auto bit = s.building_id_to_idx_.find(tb);
					if (bit != s.building_id_to_idx_.end()) {
						const auto &bb = s.buildings_[bit->second];
						std::printf("   tb type=%d team=%d hp=%.5f/%.0f prog=%.2f cp=", bb.type, bb.team, bb.hp, bb.max_hp, bb.build_progress);
						for (int p = 0; p < 8; ++p) std::printf("%.0f,", bb.capture_points_by_team[p]);
						std::printf(" max_cp=%.0f regen=%.2f gap=%.2f\n", bb.max_capture_points, bb.capture_regen_rate,
								nav::footprint_gap(bb, s.units_.pos_x[i], s.units_.pos_z[i]).length());
					}
				}
				if (sec == watch_from + 60) {
					for (size_t j = 0; j < s.units_.count; ++j) {
						if (j == i || !s.units_.alive[j]) continue;
						const float d = std::hypot(s.units_.pos_x[j] - s.units_.pos_x[i], s.units_.pos_z[j] - s.units_.pos_z[i]);
						if (d < 3.0f) std::printf("   near unit %u p%d type=%d act=%s order=%d d=%.2f vel=(%.2f,%.2f)\n", s.units_.id[j], s.units_.team[j],
								s.units_.unit_type[j], action_name(s.units_.action_state[j]), s.units_.order_kind[j], d, s.units_.vel_x[j], s.units_.vel_z[j]);
					}
					for (const auto &bb : s.buildings_) {
						const nav::Vec2 g = nav::footprint_gap(bb, s.units_.pos_x[i], s.units_.pos_z[i]);
						const float d = std::hypot(g.x, g.y);
						if (d < 6.0f) std::printf("   near bld %u type=%d team=%d gap=%.2f half=%.2f centre=(%.1f,%.1f) prog=%.2f\n", bb.id, bb.type, bb.team, d, bb.half_size, bb.x, bb.z, bb.build_progress);
					}
					{ // passability along the line to the goal
						const float gx = s.units_.target_x[i], gz = s.units_.target_z[i];
						std::printf("   line:");
						for (int k = 0; k <= 20; ++k) {
							const float px = s.units_.pos_x[i] + (gx - s.units_.pos_x[i]) * k / 20.0f, pz = s.units_.pos_z[i] + (gz - s.units_.pos_z[i]) * k / 20.0f;
							auto [cx, cy] = s.nav_grid_.world_to_navcells(px, pz);
							std::printf("%c", s.nav_grid_.is_passable((int)cx, (int)cy) ? '.' : '#');
						}
						std::printf("\n");
					}
					for (size_t r = 0; r < s.resources_.count; ++r) {
						const float d = std::hypot(s.resources_.x[r] - s.units_.pos_x[i], s.resources_.z[r] - s.units_.pos_z[i]);
						if (d < 4.0f) std::printf("   near resource type=%d d=%.2f\n", s.resources_.type[r], d);
					}
					for (const auto &sc : s.scenery_) {
						const float d = std::hypot(sc.x - s.units_.pos_x[i], sc.z - s.units_.pos_z[i]);
						if (d < 4.0f) std::printf("   near scenery d=%.2f\n", d);
					}
				}
			}
		}
		auto report = [&](size_t i, Track &k, int kind, const char *what, const char *extra) {
			if (k.reported[kind]) return;
			k.reported[kind] = true;
			++counts[what];
			const uint16_t tmpl = s.units_.tmpl[i];
			char ob[224] = "";
			// The structure it is after: its attack target, else its order's.
			const uint32_t ot = s.units_.target_building_id[i] ? s.units_.target_building_id[i] : s.units_.order_target[i];
			auto bit = s.building_id_to_idx_.find(ot);
			if (ot && bit != s.building_id_to_idx_.end()) {
				const auto &bb = s.buildings_[bit->second];
				std::snprintf(ob, sizeof ob, "order_bld type=%d team=%d hp=%.0f prog=%.2f garrison=%zu/%d dist=%.1f gap=%.1f dy=%.1f same_land=%d",
						bb.type, bb.team, bb.hp, bb.build_progress, bb.garrisoned_units.size(), (int)bb.garrison_cap,
						std::hypot(bb.x - s.units_.pos_x[i], bb.z - s.units_.pos_z[i]),
						nav::footprint_gap(bb, s.units_.pos_x[i], s.units_.pos_z[i]).length(),
						s.get_terrain_height(bb.x, bb.z) - s.unit_height(i), (int)s.petra_same_land(i, bb));
			} else if (ot) {
				std::snprintf(ob, sizeof ob, "order_target=%u", ot);
			}
			int trees = 0;
			for (size_t r = 0; r < s.resources_.count; ++r)
				if (s.resources_.alive[r] && std::hypot(s.resources_.x[r] - s.units_.pos_x[i], s.resources_.z[r] - s.units_.pos_z[i]) < 1.5f) ++trees;
			std::printf("[%s] t=%ds p%d %s id=%u at (%.0f,%.0f) action=%s order=%d target=%u/%u goal=(%.0f,%.0f) trees=%d %s %s\n", what, sec,
					s.units_.team[i], tmpl < CIV_UNIT_TEMPLATE_COUNT ? civ_unit_template(tmpl).id : "-", s.units_.id[i],
					s.units_.pos_x[i], s.units_.pos_z[i], action_name(s.units_.action_state[i]), s.units_.order_kind[i],
					s.units_.target_unit_id[i], s.units_.target_building_id[i], s.units_.target_x[i], s.units_.target_z[i], trees, ob, extra);
		};
		for (size_t i = 0; i < s.units_.count; ++i) {
			if (s.units_.team[i] >= MAX_PLAYERS) continue;
			const uint32_t id = s.units_.id[i];
			const uint8_t act = s.units_.action_state[i];
			Track &k = tr[id];
			if (act == ACTION_GARRISONED) {
				if (++k.garrison_s == 900) report(i, k, 4, "garrisoned-15min", "");
				continue;
			}
			k.garrison_s = 0;
			if (!s.units_.alive[i]) continue;
			const float x = s.units_.pos_x[i], z = s.units_.pos_z[i];
			// Stuck: an order that should move it, and it doesn't.
			const bool moving_action = act == ACTION_MOVE || act == ACTION_ATTACK_MOVE || act == ACTION_PATROL ||
					act == ACTION_RETURN_DROPSITE || act == ACTION_FLEE;
			if (s.units_.has_order[i] && moving_action) {
				if (std::hypot(x - k.ax, z - k.az) > 1.0f) { k.ax = x; k.az = z; k.still_s = 0; }
				else if (++k.still_s == 60) report(i, k, 0, "stuck", "");
			} else {
				k.ax = x; k.az = z; k.still_s = 0;
			}
			if (act == ACTION_IDLE && s.units_.order_kind[i] != ORDER_NONE) {
				if (++k.idle_order_s == 30) report(i, k, 1, "idle-under-order", "");
			} else {
				k.idle_order_s = 0;
			}
			// Hits nothing: the same target for 60 s with its health never dropping.
			const uint32_t tu = s.units_.target_unit_id[i], tb = s.units_.target_building_id[i];
			const uint32_t tgt = tu ? tu : tb;
			if (act == ACTION_ATTACK && tgt) {
				const float hp = target_hp(s, tu, tb), cp = tu ? -1.0f : target_cp(s, tb);
				const bool hurt = hp >= 0.0f && hp < k.tgt_hp - 0.01f, captured = cp >= 0.0f && cp < k.tgt_cp - 0.01f;
				if (tgt != k.tgt) { k.tgt = tgt; k.tgt_hp = hp; k.tgt_cp = cp; k.no_hurt_s = 0; }
				else if (hurt || captured) {
					// Each measure keeps its own low mark, so one recovering doesn't hide the other.
					k.tgt_hp = std::min(k.tgt_hp, hp);
					k.tgt_cp = std::min(k.tgt_cp, cp);
					k.no_hurt_s = 0;
					if (hurt && is_siege(s.units_.unit_type[i])) k.siege_hurt = true;
				} else if (++k.no_hurt_s == 60) {
					char buf[160];
					float tdist = -1.0f;
					const char *tk = "bld";
					if (tu) {
						const int32_t j = s.lookup_unit(tu);
						if (j >= 0) {
							tdist = std::hypot(s.units_.pos_x[j] - s.units_.pos_x[i], s.units_.pos_z[j] - s.units_.pos_z[i]);
							tk = s.units_.tmpl[j] < CIV_UNIT_TEMPLATE_COUNT ? civ_unit_template(s.units_.tmpl[j]).id : "unit";
						} else {
							tk = s.is_fauna_id(tu) ? "fauna" : "gone";
							for (size_t f = 0; f < s.fauna_.count; ++f)
								if (s.fauna_.id[f] == tu) tdist = std::hypot(s.fauna_.pos_x[f] - s.units_.pos_x[i], s.fauna_.pos_z[f] - s.units_.pos_z[i]);
						}
					}
					std::snprintf(buf, sizeof buf, "target hp %.0f cp %.0f kind=%s tdist=%.1f reach=%.1f vision=%.1f stance=%d", hp, cp, tk, tdist,
							s.attack_reach(i), s.unit_vision_at(i), s.units_.stance[i]);
					report(i, k, 2, "hits-nothing", buf);
				}
				if (is_siege(s.units_.unit_type[i]) && ++k.siege_attack_s == 120 && !k.siege_hurt)
					report(i, k, 3, "siege-never-fires", "");
			} else {
				k.tgt = 0; k.no_hurt_s = 0;
			}
			if (unit_is_ship(s.units_.unit_type[i])) {
				auto it = s.transport_passengers_.find(id);
				if (it != s.transport_passengers_.end() && !it->second.empty()) {
					if (++k.laden_s == 180) {
						char buf[64];
						std::snprintf(buf, sizeof buf, "%zu aboard", it->second.size());
						report(i, k, 5, "ship-never-lands", buf);
					}
				} else {
					k.laden_s = 0;
				}
			}
		}
		if (s.is_game_over() || s.is_victory()) {
			std::printf("[end] t=%ds game over\n", sec);
			break;
		}
	}
	const int end_sec = (int)(a->game_time() + 0.5);
	if (end_sec >= 15 * 60)
		for (int p = 0; p < players; ++p)
			if (!a->is_defeated((uint8_t)p) && a->player((uint8_t)p).phase < 2) {
				std::printf("[no-phase] p%d still in Village at %d s\n", p, end_sec);
				++counts["no-phase"];
			}
	std::printf("[summary] map=%s seed=%u desyncs=%d slowest_min=%.1fs slowest_turn=%.0fms", map_name.c_str(), seed, desyncs, slowest_minute,
			slowest_turn);
	for (const auto &[k, v] : counts) std::printf(" %s=%d", k.c_str(), v);
	std::printf("\n");
	return 0;
}
