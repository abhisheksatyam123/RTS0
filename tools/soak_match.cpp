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
// Build (one line):
//   g++ -std=c++20 -O2 -Isrc tools/soak_match.cpp src/simulation/simulation.cpp
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp
//       src/network/net_host.cpp -lpthread -o /tmp/soak_match
//   /tmp/soak_match --map 15 --civs 0,1,2,3 --minutes 30 --size 256 --seed 7 --twin
#define private public
#include "simulation/simulation.h"
#undef private
#include "network/turn_manager.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
	if (bid) { // health plus the owner's capture points: a capture attack lowers the latter
		auto it = s.building_id_to_idx_.find(bid);
		if (it != s.building_id_to_idx_.end()) {
			const auto &b = s.buildings_[it->second];
			return b.hp + (b.team < MAX_PLAYERS ? b.capture_points_by_team[b.team] : 0.0f);
		}
	}
	return -1.0f;
}

void setup(Simulation &s, int map, uint32_t seed, int players, float size, const std::vector<int> &civs, bool circular) {
	s.reset();
	s.set_sim_seed(seed);
	s.set_waves_enabled(false);
	if (circular) s.set_setup_flag(Simulation::SETUP_CIRCULAR_MAP, true);
	for (int p = 0; p < players; ++p) s.set_team_civ((uint8_t)p, (uint8_t)civs[p % civs.size()]);
	s.generate_random_map((MapType)map, seed, players, size * 0.5f);
	s.set_victory_condition(VICTORY_CONQUEST);
	for (int p = 0; p < players; ++p) s.set_team_civ((uint8_t)p, (uint8_t)civs[p % civs.size()]);
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) s.set_player_active(p, p < players, p < players);
	for (int p = 0; p < players; ++p) s.set_petra_difficulty((uint8_t)p, 3);
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
	for (int i = 1; i < argc; ++i) {
		auto next = [&]() { return i + 1 < argc ? argv[++i] : (char *)"0"; };
		if (!strcmp(argv[i], "--map")) map = atoi(next());
		else if (!strcmp(argv[i], "--seed")) seed = (uint32_t)strtoul(next(), nullptr, 10);
		else if (!strcmp(argv[i], "--size")) size = (float)atof(next());
		else if (!strcmp(argv[i], "--minutes")) minutes = atof(next());
		else if (!strcmp(argv[i], "--players")) players = atoi(next());
		else if (!strcmp(argv[i], "--twin")) twin = true;
		else if (!strcmp(argv[i], "--circular")) circular = true;
		else if (!strcmp(argv[i], "--save-at")) save_at = atof(next());
		else if (!strcmp(argv[i], "--watch")) watch = (uint32_t)strtoul(next(), nullptr, 10);
		else if (!strcmp(argv[i], "--watch-from")) watch_from = atoi(next());
		else if (!strcmp(argv[i], "--civs")) {
			civs.clear();
			for (char *t = strtok(next(), ","); t; t = strtok(nullptr, ",")) civs.push_back(atoi(t));
		}
	}

	auto a = std::make_unique<Simulation>();
	setup(*a, map, seed, players, size, civs, circular);
	std::unique_ptr<Simulation> b, c;
	if (twin) {
		b = std::make_unique<Simulation>();
		setup(*b, map, seed, players, size, civs, circular);
	}
	TurnManager tm;
	std::printf("soak map=%d seed=%u size=%.0f players=%d civs=", map, seed, size, players);
	for (int p = 0; p < players; ++p) std::printf("%s%d", p ? "," : "", civs[p % civs.size()]);
	std::printf(" circular=%d\n", circular);

	std::unordered_map<uint32_t, Track> tr;
	std::map<std::string, int> counts;
	int desyncs = 0;
	const int turns = (int)(minutes * 60.0 / 0.2 + 0.5);
	for (int t = 1; t <= turns; ++t) {
		turn(*a);
		if (b) turn(*b);
		if (c) turn(*c);
		if (t % 5) continue;
		const int sec = t / 5;
		Simulation &s = *a;
		if (save_at >= 0.0 && !c && sec >= (int)(save_at * 60.0)) {
			const std::string path = "/tmp/soak_" + std::to_string(seed) + "_" + std::to_string(map) + ".sav";
			c = std::make_unique<Simulation>();
			if (!s.save_state(path.c_str()) || !c->load_state(path.c_str())) {
				std::printf("[desync] t=%ds save/load failed\n", sec);
				++desyncs;
				c.reset();
				save_at = -1.0;
			} else if (tm.compute_sim_hash(s) != tm.compute_sim_hash(*c)) {
				std::printf("[desync] t=%ds loaded copy hashes differently right after load\n", sec);
				++desyncs;
			}
		}
		if (sec % 60 == 0) {
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
				const float hp = target_hp(s, tu, tb);
				if (tgt != k.tgt) { k.tgt = tgt; k.tgt_hp = hp; k.no_hurt_s = 0; }
				else if (hp >= 0.0f && hp < k.tgt_hp - 0.01f) { k.tgt_hp = hp; k.no_hurt_s = 0; if (is_siege(s.units_.unit_type[i])) k.siege_hurt = true; }
				else if (++k.no_hurt_s == 60) {
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
					std::snprintf(buf, sizeof buf, "target hp %.0f kind=%s tdist=%.1f reach=%.1f vision=%.1f stance=%d", hp, tk, tdist,
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
	std::printf("[summary] map=%d seed=%u desyncs=%d", map, seed, desyncs);
	for (const auto &[k, v] : counts) std::printf(" %s=%d", k.c_str(), v);
	std::printf("\n");
	return 0;
}
