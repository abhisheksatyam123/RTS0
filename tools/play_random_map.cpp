// Plays a trigger-scripted random map (or a data/maps JSON map) to its end, headless. Player 1
// is driven by a small economy-and-army bot whose every action is a NetCommand posted to
// TurnManager, as the session's input sends it (train, gather, build, attack-move); the other
// seats run the AI. The map's trigger script runs as it does in a match.
//
//   g++ -std=c++20 -O2 -Isrc tools/play_random_map.cpp src/simulation/simulation.cpp src/simulation/rms_generator.cpp
//       src/core/job_system.cpp src/network/turn_manager.cpp src/network/net_host.cpp -lpthread -o /tmp/play_random_map
//   /tmp/play_random_map --random danubius --minutes 60
//   /tmp/play_random_map --json gallic_fields --players 3 --minutes 60
//
// Exit 0 when the match ends by itself within --minutes (a defeat or a win for anyone), 1 when it
// is still running at the limit (reported as NO END, not a pass), 2 on a usage error.
#define main probe_main
#include "probe_trigger_maps.cpp"
#undef main

#include "network/turn_manager.h"
#include "network/net_types.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace rtso;

namespace {

constexpr uint8_t ME = 0; // the bot's seat (player 1)
constexpr float THINK_S = 10.0f;
constexpr float ATTACK_AT_S = 900.0f;      // the army marches from this game time
constexpr float ATTACK_EVERY_S = 120.0f;   // and is re-sent this often
constexpr int ARMY_TO_ATTACK = 25;

struct Bot {
	Simulation &s;
	TurnManager &tm;
	float clock = 0.0f;
	float next_think = 0.0f;
	float next_attack = ATTACK_AT_S;
	int houses = 0;
	int commands = 0;
	bool barracks_ordered = false;
	int failed_builds = 0;

	Bot(Simulation &sim, TurnManager &t) : s(sim), tm(t) {}

	void post(uint8_t type, uint32_t target, std::vector<uint32_t> ids = {}, uint8_t param = 0, float tx = 0.0f, float tz = 0.0f) {
		NetCommand c;
		c.client_id = 0;
		c.cmd_type = type;
		c.target_id = target;
		c.entity_ids = std::move(ids);
		c.param = param;
		c.target_x = tx;
		c.target_z = tz;
		tm.post_local_command(c);
		++commands;
	}

	void tick_for(float seconds) {
		for (float t = 0.0f; t < seconds; t += TurnManager::TURN_DURATION) {
			tm.tick(TurnManager::TURN_DURATION, s);
			clock += TurnManager::TURN_DURATION;
		}
	}

	// --- queries ------------------------------------------------------------------------------
	std::vector<uint32_t> units_of(uint8_t team, std::initializer_list<UnitType> types) const {
		std::vector<uint32_t> out;
		for (size_t i = 0; i < s.units_.count; ++i) {
			if (!s.units_.alive[i] || s.units_.team[i] != team) continue;
			for (UnitType t : types)
				if (s.units_.unit_type[i] == t) { out.push_back(s.units_.id[i]); break; }
		}
		return out;
	}
	std::vector<uint32_t> idle(const std::vector<uint32_t> &ids) const {
		std::vector<uint32_t> out;
		for (uint32_t id : ids)
			if (!s.unit_has_order(id)) out.push_back(id);
		return out;
	}
	uint32_t building(uint8_t team, uint8_t type) const {
		for (const Simulation::Building &b : s.buildings_)
			if (b.team == team && b.type == type && b.hp > 0.0f && !b.wrecked && b.build_progress >= 1.0f) return b.id;
		return 0;
	}
	uint32_t civic_centre(uint8_t team) const {
		for (const Simulation::Building &b : s.buildings_)
			if (b.team == team && is_civic_centre(b.type) && b.hp > 0.0f && !b.wrecked && b.build_progress >= 1.0f) return b.id;
		return 0;
	}
	// The civic centre of another player that is still standing, nearest to (x, z).
	bool enemy_cc(float x, float z, float &ex, float &ez) const {
		float best = 1e30f;
		bool found = false;
		for (const Simulation::Building &b : s.buildings_) {
			if (b.team == ME || b.team >= MAX_PLAYERS || !is_civic_centre(b.type) || b.hp <= 0.0f || b.wrecked || b.build_progress < 1.0f) continue;
			if (s.is_defeated(b.team)) continue;
			const float d = (b.x - x) * (b.x - x) + (b.z - z) * (b.z - z);
			if (d < best) { best = d; ex = b.x; ez = b.z; found = true; }
		}
		return found;
	}
	// Nearest living resource of the given generic type (0 tree, 3 food), to (x, z).
	uint32_t nearest_resource(float x, float z, uint8_t type, float max_d) const {
		uint32_t best = 0;
		float bd = max_d * max_d;
		for (size_t i = 0; i < s.resources_.count; ++i) {
			if (!s.resources_.alive[i] || s.resources_.amount[i] <= 0 || s.resources_.type[i] != type) continue;
			const float dx = s.resources_.x[i] - x, dz = s.resources_.z[i] - z;
			const float d = dx * dx + dz * dz;
			if (d < bd) { bd = d; best = s.resources_.id[i]; }
		}
		return best;
	}
	// The generic type of a resource (0 tree, 1 gold, 2 stone, 3 food), 255 when it is gone.
	uint8_t resource_type(uint32_t id) const {
		for (size_t i = 0; i < s.resources_.count; ++i)
			if (s.resources_.id[i] == id) return s.resources_.type[i];
		return 255;
	}
	bool unit_pos(uint32_t id, float &x, float &z) const {
		const int32_t i = s.lookup_unit(id);
		if (i < 0) return false;
		x = s.units_.pos_x[i];
		z = s.units_.pos_z[i];
		return true;
	}
	// A free site near (x, z) for `type`, rmin to rmax metres out.
	bool find_site(uint8_t type, float x, float z, float rmin, float rmax, int &cx, int &cy) const {
		const float step = Simulation::GRID;
		float best = 1e30f;
		bool found = false;
		for (float dx = -rmax; dx <= rmax; dx += step)
			for (float dz = -rmax; dz <= rmax; dz += step) {
				const float r = std::sqrt(dx * dx + dz * dz);
				if (r < rmin || r > rmax) continue;
				const int ix = (int)std::floor((x + dx) / step), iy = (int)std::floor((z + dz) / step);
				if (s.check_placement(ME, ix, iy, type) != Simulation::PLACE_OK) continue;
				if (r < best) { best = r; cx = ix; cy = iy; found = true; }
			}
		return found;
	}
	void build(uint8_t type, float x, float z, std::vector<uint32_t> builders, float rmin, float rmax) {
		int cx = 0, cy = 0;
		if (builders.empty() || !find_site(type, x, z, rmin, rmax, cx, cy)) { ++failed_builds; return; }
		post((uint8_t)NetCommandType::NET_CMD_BUILD, Simulation::pack_angle(Simulation::DEFAULT_BUILDING_ROT), std::move(builders), type,
				(float)cx, (float)cy);
	}

	// --- the bot ------------------------------------------------------------------------------
	void think() {
		const uint32_t cc = civic_centre(ME);
		if (!cc) return;
		const Simulation::Building *ccb = nullptr;
		for (const Simulation::Building &b : s.buildings_) if (b.id == cc) ccb = &b;
		if (!ccb) return;
		const float cx = ccb->x, cz = ccb->z;

		auto females = units_of(ME, {UNIT_FEMALE_CITIZEN});
		auto soldiers = units_of(ME, {UNIT_SWORDSMAN, UNIT_SPEARMAN, UNIT_ARCHER, UNIT_CAVALRY});
		const size_t pop = females.size() + soldiers.size();

		// Civilians: a dozen on berries, the rest on wood.
		if (females.size() < 30) post((uint8_t)NetCommandType::NET_CMD_TRAIN, cc, {}, UNIT_FEMALE_CITIZEN, 0.0f, 0.0f);
		int food_gatherers = 0;
		for (uint32_t f : females) {
			const int32_t i = s.lookup_unit(f);
			if (i >= 0 && s.units_.has_order[i] && s.units_.target_resource_id[i] && resource_type(s.units_.target_resource_id[i]) == 3) ++food_gatherers;
		}
		for (uint32_t f : idle(females)) {
			float x = 0.0f, z = 0.0f;
			if (!unit_pos(f, x, z)) continue;
			uint32_t r = food_gatherers < 12 ? nearest_resource(cx, cz, 3, 80.0f) : 0;
			if (r) ++food_gatherers;
			if (!r) r = nearest_resource(cx, cz, 0, 120.0f);
			if (r) post((uint8_t)NetCommandType::NET_CMD_GATHER, r, {f});
		}
		// Houses keep ahead of the population.
		if ((int)pop + 5 >= 10 + 8 * houses && !females.empty() && houses < 40) {
			++houses;
			build(BLD_HOUSE, cx, cz, {females[(size_t)houses % females.size()]}, 10.0f, 30.0f);
		}
		// A barracks once there are a dozen civilians; then citizen soldiers from it.
		uint32_t barracks = building(ME, BLD_BARRACKS);
		if (!barracks && !barracks_ordered && females.size() >= 12 && soldiers.size() < 1) {
			barracks_ordered = true;
			build(BLD_BARRACKS, cx - 14.0f, cz + 10.0f, {females[0], females[1]}, 8.0f, 30.0f);
		}
		if (barracks && soldiers.size() < 60) post((uint8_t)NetCommandType::NET_CMD_TRAIN, barracks, {}, UNIT_SPEARMAN, 0.0f, 0.0f);
		for (uint32_t u : idle(soldiers)) {
			float x = 0.0f, z = 0.0f;
			if (!unit_pos(u, x, z)) continue;
			if (clock < ATTACK_AT_S) {
				if (const uint32_t r = nearest_resource(cx, cz, 0, 120.0f)) post((uint8_t)NetCommandType::NET_CMD_GATHER, r, {u});
			}
		}
		// The army marches on the nearest enemy civic centre.
		if (clock >= next_attack && soldiers.size() >= (size_t)ARMY_TO_ATTACK) {
			float ex = 0.0f, ez = 0.0f;
			if (enemy_cc(cx, cz, ex, ez)) {
				post((uint8_t)NetCommandType::NET_CMD_ATTACK_MOVE, 0, soldiers, 0, ex, ez);
				next_attack = clock + ATTACK_EVERY_S;
			}
		}
	}

	void report() {
		int mine_u = 0, mine_b = 0;
		for (size_t i = 0; i < s.units_.count; ++i) mine_u += s.units_.alive[i] && s.units_.team[i] == ME;
		for (const auto &b : s.buildings_) mine_b += b.team == ME && b.hp > 0.0f && !b.wrecked;
		std::printf("[%7.1fs] p1 %d units %d buildings, houses %d, commands %d, failed builds %d, p2 defeated %d\n", clock, mine_u, mine_b, houses,
				commands, failed_builds, (int)s.is_defeated(1));
	}
};

} // namespace

int main(int argc, char **argv) {
	std::string json_map, random_map;
	double minutes = 60.0;
	uint32_t seed = 7;
	int players = 2;
	float size = 256.0f;
	for (int i = 1; i < argc; ++i) {
		auto next = [&]() { return i + 1 < argc ? argv[++i] : (char *)"0"; };
		if (!std::strcmp(argv[i], "--json")) json_map = next();
		else if (!std::strcmp(argv[i], "--random")) random_map = next();
		else if (!std::strcmp(argv[i], "--minutes")) minutes = std::atof(next());
		else if (!std::strcmp(argv[i], "--seed")) seed = (uint32_t)std::strtoul(next(), nullptr, 10);
		else if (!std::strcmp(argv[i], "--players")) players = std::atoi(next());
		else if (!std::strcmp(argv[i], "--size")) size = (float)std::atof(next());
		else { std::fprintf(stderr, "usage: play_random_map (--json NAME | --random MAPID) [--minutes M] [--seed S] [--players N] [--size T]\n"); return 2; }
	}
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	static Simulation s;
	MapPlayerOverrides ov;
	if (!json_map.empty()) {
		if (!load_json_map(s, json_map, seed, ov)) return 2;
		players = std::max(1, ov.players);
	} else if (!random_map.empty()) {
		int type = -1;
		for (int t = 0; t < MAP_TYPE_COUNT; ++t)
			if (random_map == map_type_info((uint8_t)t).id) { type = t; break; }
		if (type < 0) { std::fprintf(stderr, "unknown random map %s\n", random_map.c_str()); return 2; }
		s.reset();
		s.set_sim_seed(seed);
		s.set_waves_enabled(false);
		s.generate_random_map((MapType)type, seed, players, size * 0.5f);
	} else {
		std::fprintf(stderr, "--json NAME or --random MAPID\n");
		return 2;
	}
	// Conquest, as the lobby's default; the bot's seat is a human, the other seats run the AI.
	s.set_trigger_difficulty(3);
	s.set_victory_conditions(1);
	s.set_waves_enabled(false);
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		const bool seated = p < players;
		s.set_player_active(p, seated, seated && p != ME);
		if (seated && p != ME) s.set_petra_difficulty(p, 3);
	}
	for (const std::string &u : g_unported_scripts) std::printf("UNPORTED trigger script: %s\n", u.c_str());

	TurnManager tm;
	Bot bot(s, tm);
	const float limit = (float)(minutes * 60.0);
	float next_report = 300.0f;
	std::printf("playing %s%s, %d seats, seed %u, limit %.0f min\n", random_map.empty() ? json_map.c_str() : random_map.c_str(),
			random_map.empty() ? "" : " (random)", players, seed, minutes);
	bot.tick_for(0.8f);
	while (!s.game_over_ && !s.victory_ && bot.clock < limit) {
		if (bot.clock >= bot.next_think) {
			bot.think();
			bot.next_think = bot.clock + THINK_S;
		}
		bot.tick_for(TurnManager::TURN_DURATION);
		for (const auto &d : s.take_trigger_dialogs()) std::printf("[%7.1fs] DIALOG to %02x: %s\n", bot.clock, d.players, d.caption.c_str());
		for (const auto &n : s.take_session_notices()) {
			if (n.kind == Simulation::SessionNotice::DEFEATED) std::printf("[%7.1fs] DEFEATED p%u (%s)\n", bot.clock, n.player + 1, notice_reason(n.tech));
			else if (n.kind == Simulation::SessionNotice::WON) std::printf("[%7.1fs] WON mask %02x (%s)\n", bot.clock, n.other, notice_reason(n.tech));
		}
		if (bot.clock >= next_report) {
			bot.report();
			next_report += 300.0f;
		}
	}
	const bool ended = s.game_over_ || s.victory_;
	std::printf("%s at %.1fs: game_over %d victory %d winners %02x defeated %02x commands %d\n", ended ? "END" : "NO END", bot.clock,
			(int)s.game_over_, (int)s.victory_, s.winner_mask(), s.defeated_mask_, bot.commands);
	return ended ? 0 : 1;
}
