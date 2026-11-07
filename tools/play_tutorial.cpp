// Plays a shipped tutorial to its end, headless. Each step is a NetCommand posted to TurnManager
// as the session's input would send it (Ready, gather, train, build, rally, research, attack), so
// the goals' triggers see the same commands a person's clicks send. The map is loaded from
// data/maps/NAME.json by the loader of tools/probe_trigger_maps.cpp.
//
//   g++ -std=c++20 -O2 -Isrc tools/play_tutorial.cpp src/simulation/simulation.cpp src/simulation/rms_generator.cpp
//       src/core/job_system.cpp src/network/turn_manager.cpp src/network/net_host.cpp -lpthread -o /tmp/play_tutorial
//   /tmp/play_tutorial introductory_tutorial
//   /tmp/play_tutorial starting_economy_walkthrough
//
// Checks: every goal is entered in order, the goal's trigger advances by itself once the player has
// done what the text asks, and the match ends with a win for player 1 (introductory) or the last
// goal is reached (walkthrough). Exit 0 = pass. Each goal that does not advance within its limit is
// a FAIL with the goal's text and the state the driver saw.

// The map loader and the JSON reader of the probe: its main() is renamed out of the way.
#define main probe_main
#include "probe_trigger_maps.cpp"
#undef main

#include "simulation/tutorial_data.h"
#include "network/turn_manager.h"
#include "network/net_types.h"

#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace rtso;

namespace {

constexpr uint8_t ME = 0;     // the tutorial's player (seat 1)
constexpr uint8_t ENEMY = 1;  // the tutorial's enemy
constexpr float GOAL_LIMIT_S = 400.0f;  // game seconds a goal may take before it counts as stuck

struct Driver {
	Simulation &s;
	TurnManager &tm;
	float clock = 0.0f;
	int failures = 0;

	Driver(Simulation &sim, TurnManager &t) : s(sim), tm(t) {}

	void tick_for(float seconds) {
		for (float t = 0.0f; t < seconds; t += TurnManager::TURN_DURATION) {
			tm.tick(TurnManager::TURN_DURATION, s);
			clock += TurnManager::TURN_DURATION;
		}
	}

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
		tick_for(4 * TurnManager::TURN_DURATION);
	}

	void ready() { post((uint8_t)NetCommandType::NET_CMD_TUTORIAL_READY, 0); }

	// --- the player's entities --------------------------------------------------------------
	std::vector<uint32_t> units(uint8_t team, const char *suffix = nullptr) const {
		std::vector<uint32_t> out;
		for (size_t i = 0; i < s.units_.count; ++i) {
			if (!s.units_.alive[i] || s.units_.team[i] != team) continue;
			if (suffix && !std::strstr(civ_unit_template(s.units_.tmpl[i]).id, suffix)) continue;
			out.push_back(s.units_.id[i]);
		}
		return out;
	}
	// Citizen soldiers (infantry, cavalry, ranged) of the player, as 0 A.D.'s Soldier class.
	std::vector<uint32_t> soldiers(uint8_t team) const {
		std::vector<uint32_t> out;
		for (size_t i = 0; i < s.units_.count; ++i) {
			if (!s.units_.alive[i] || s.units_.team[i] != team) continue;
			const char *id = civ_unit_template(s.units_.tmpl[i]).id;
			if (std::strstr(id, "infantry") || std::strstr(id, "cavalry") || std::strstr(id, "ranged")) out.push_back(s.units_.id[i]);
		}
		return out;
	}
	std::vector<uint32_t> civilians(uint8_t team) const {
		std::vector<uint32_t> out;
		for (size_t i = 0; i < s.units_.count; ++i) {
			if (!s.units_.alive[i] || s.units_.team[i] != team) continue;
			const char *id = civ_unit_template(s.units_.tmpl[i]).id;
			if (std::strstr(id, "support_")) out.push_back(s.units_.id[i]);
		}
		return out;
	}
	// The first building of `team` with type `t` that is finished (or any, when unfinished is set).
	uint32_t building(uint8_t team, uint8_t t, bool finished = true) const {
		for (const Simulation::Building &b : s.buildings_)
			if (b.team == team && b.type == t && b.hp > 0.0f && !b.wrecked && (!finished || b.build_progress >= 1.0f)) return b.id;
		return 0;
	}
	const Simulation::Building *find_building(uint32_t id) const {
		for (const Simulation::Building &b : s.buildings_)
			if (b.id == id) return &b;
		return nullptr;
	}
	uint32_t civic_centre(uint8_t team) const {
		for (const Simulation::Building &b : s.buildings_)
			if (b.team == team && is_civic_centre(b.type) && b.hp > 0.0f && !b.wrecked) return b.id;
		return 0;
	}
	// Nearest living resource of a tutorial kind (TUT_RES_* in simulation.cpp) to a point.
	uint32_t nearest_resource(float x, float z, uint8_t kind, float min_dist = 0.0f) const {
		uint32_t best = 0;
		float bd = 1e30f;
		for (size_t i = 0; i < s.resources_.count; ++i) {
			if (!s.resources_.alive[i] || s.resources_.amount[i] <= 0) continue;
			const uint32_t id = s.resources_.id[i];
			if (s.tutorial_resource_kind(id, false) != kind) continue;
			const float dx = s.resources_.x[i] - x, dz = s.resources_.z[i] - z;
			const float d = dx * dx + dz * dz;
			if (d < bd && d >= min_dist * min_dist) { bd = d; best = id; }
		}
		return best;
	}
	bool resource_pos(uint32_t id, float &x, float &z) const {
		for (size_t i = 0; i < s.resources_.count; ++i)
			if (s.resources_.id[i] == id) { x = s.resources_.x[i]; z = s.resources_.z[i]; return true; }
		return false;
	}
	void pos_of_building(uint32_t id, float &x, float &z) const {
		if (const Simulation::Building *b = find_building(id)) { x = b->x; z = b->z; }
	}
	bool unit_pos(uint32_t id, float &x, float &z) const {
		const int32_t i = s.lookup_unit(id);
		if (i < 0) return false;
		x = s.units_.pos_x[i];
		z = s.units_.pos_z[i];
		return true;
	}

	// A free site for `b_type` as close to (x, z) as possible, from rmin to rmax metres out.
	bool find_site(uint8_t b_type, float x, float z, float rmin, float rmax, int &cell_x, int &cell_y) const {
		const float step = Simulation::GRID; // check_placement's cell_x * GRID is the world x of the cell's corner
		float best = 1e30f;
		bool found = false;
		for (float dx = -rmax; dx <= rmax; dx += step)
			for (float dz = -rmax; dz <= rmax; dz += step) {
				const float r = std::sqrt(dx * dx + dz * dz);
				if (r < rmin || r > rmax) continue;
				const int cx = (int)std::floor((x + dx) / step), cy = (int)std::floor((z + dz) / step);
				if (s.check_placement(ME, cx, cy, b_type) != Simulation::PLACE_OK) continue;
				if (r < best) { best = r; cell_x = cx; cell_y = cy; found = true; }
			}
		return found;
	}
	// Places a building with `builders` (a real BUILD command: the sim hands them the repair too).
	bool build(uint8_t b_type, float x, float z, std::vector<uint32_t> builders, float rmin = 8.0f, float rmax = 30.0f) {
		int cx = 0, cy = 0;
		if (builders.empty() || !find_site(b_type, x, z, rmin, rmax, cx, cy)) return false;
		post((uint8_t)NetCommandType::NET_CMD_BUILD, Simulation::pack_angle(Simulation::DEFAULT_BUILDING_ROT), std::move(builders),
				b_type, (float)cx, (float)cy);
		return true;
	}
	bool train_template(uint32_t bld, const char *tmpl, int count) {
		const uint16_t t = civ_unit_template_by_id(tmpl);
		if (t == CIV_UNIT_TEMPLATE_NONE || !bld) return false;
		post((uint8_t)NetCommandType::NET_CMD_TRAIN_TEMPLATE, bld, {}, 0, (float)t, (float)count);
		return true;
	}
	// Rally: target_id is the structure whose rally point it is; entity_ids are the resource and the
	// structure the point is on (turn_manager.cpp, command 14).
	void rally(uint32_t bld, uint32_t res, uint32_t structure) {
		float x = 0.0f, z = 0.0f;
		if (!resource_pos(res, x, z)) pos_of_building(structure, x, z);
		post((uint8_t)NetCommandType::NET_CMD_RALLY, bld, {res, structure}, 0, x, z);
	}
	void research(uint32_t bld, uint8_t tech) {
		post((uint8_t)NetCommandType::NET_CMD_RESEARCH, bld, {}, tech, 0.0f, 0.0f);
	}
	void give(int amount) { s.set_player_resources(ME, amount, amount, amount, amount); }
};

// --- introductory_tutorial.js, goal by goal (the actions a person takes for each goal's text) ----
struct Intro {
	Driver &d;
	bool acted = false;
	bool rallied = false;
	float last_order = -100.0f;
	float last_train = -100.0f;
	int batches = 0;
	float assault_at = -1.0f;
	explicit Intro(Driver &dd) : d(dd) {}
	void new_goal(int) { acted = false; rallied = false; }
	// The player keeps training soldiers at the Barracks while the goals ask for an army (23 to 25).
	void before(int g) {
		if (g >= 23 && g <= 25 && d.clock - last_train >= 20.0f && batches < 14) {
			const uint32_t barracks = d.building(ME, BLD_BARRACKS, true);
			if (barracks && d.train_template(barracks, "spart/infantry_spearman_b", 5)) {
				last_train = d.clock;
				++batches;
			}
		}
	}

	// Returns true when the goal's action has been issued (the goal then advances by itself).
	bool act(int g) {
		Simulation &s = d.s;
		const uint32_t cc = d.civic_centre(ME);
		float cx = 0, cz = 0;
		if (cc) d.pos_of_building(cc, cx, cz);
		switch (g) {
			case 1: { // gather berries with a civilian
				auto c = d.civilians(ME);
				const uint32_t fruit = d.nearest_resource(cx, cz, 1);
				if (c.empty() || !fruit) return false;
				d.post((uint8_t)NetCommandType::NET_CMD_GATHER, fruit, {c[0]});
				return true;
			}
			case 2: { // a citizen soldier gathers wood
				auto sol = d.soldiers(ME);
				const uint32_t tree = d.nearest_resource(cx, cz, 2);
				if (sol.empty() || !tree) return false;
				d.post((uint8_t)NetCommandType::NET_CMD_GATHER, tree, {sol[0]});
				return true;
			}
			case 3: return d.train_template(cc, "spart/infantry_spearman_b", 5);
			case 4: { // a House with two civilians
				auto c = d.civilians(ME);
				if (c.size() < 2) return false;
				return d.build(BLD_HOUSE, cx, cz, {c[0], c[1]});
			}
			case 5: { // a Storehouse by the trees with the new hoplites
				auto sol = d.soldiers(ME);
				const uint32_t tree = d.nearest_resource(cx, cz, 2);
				float tx = 0, tz = 0;
				if (sol.empty() || !tree || !d.resource_pos(tree, tx, tz)) return false;
				return d.build(BLD_STOREHOUSE, tx, tz, sol, 4.0f, 14.0f);
			}
			case 6: return d.train_template(cc, "spart/infantry_javelineer_b", 5);
			case 7: { // a Farmstead with the idle civilians
				auto c = d.civilians(ME);
				if (c.empty()) return false;
				return d.build(BLD_FARMSTEAD, cx + 14.0f, cz, c);
			}
			case 8: return true; // waiting for the Farmstead to be built (STRUCTURE_BUILT)
			case 9: { // a Field beside the Farmstead
				auto c = d.civilians(ME);
				const uint32_t fs = d.building(ME, BLD_FARMSTEAD, false);
				float fx = 0, fz = 0;
				if (c.size() < 2 || !fs) return false;
				d.pos_of_building(fs, fx, fz);
				return d.build(BLD_FIELD, fx, fz, {c[0], c[1]}, 6.0f, 20.0f);
			}
			case 10: { // a second House with the skirmishers
				auto sol = d.soldiers(ME);
				if (sol.size() < 2) return false;
				return d.build(BLD_HOUSE, cx, cz, {sol[0], sol[1]}, 10.0f, 34.0f);
			}
			case 11: { // hoplites batch, then the Civic Centre's rally point on the wood
				if (!acted) {
					if (!d.train_template(cc, "spart/infantry_spearman_b", 5)) return false;
					acted = true;
					return false; // the rally comes next, same goal
				}
				const uint32_t tree = d.nearest_resource(cx, cz, 2);
				if (!tree) return false;
				d.rally(cc, tree, 0);
				return true;
			}
			case 12: { // a Barracks with four soldiers
				auto sol = d.soldiers(ME);
				if (sol.size() < 4) return false;
				return d.build(BLD_BARRACKS, cx - 14.0f, cz + 10.0f, {sol[0], sol[1], sol[2], sol[3]});
			}
			case 13: // advance to Town Phase at the Civic Centre (refused until its requirements are met)
				if (d.clock - last_order >= 3.0f) {
					last_order = d.clock;
					d.research(cc, TECH_PHASE_TOWN);
				}
				return false;
			case 14: return true; // the phase's research finishes by itself
			case 15: { // an Outpost on the north-east edge with two soldiers
				auto sol = d.soldiers(ME);
				if (sol.size() < 2) return false;
				return d.build(BLD_OUTPOST, cx + 22.0f, cz - 22.0f, {sol[0], sol[1]}, 10.0f, 30.0f);
			}
			case 16: { // civilians batch, then a rally on the finished Field
				if (!acted) {
					// tutorial_civilian: support_civilian when the roster has it, else the female citizen.
					const bool has_civilian = civ_unit_template_by_id("spart/support_civilian") != CIV_UNIT_TEMPLATE_NONE;
					if (!d.train_template(cc, has_civilian ? "spart/support_civilian" : "spart/support_female_citizen", 5)) return false;
					acted = true;
					return false;
				}
				const uint32_t field = d.building(ME, BLD_FIELD, true);
				if (!field || rallied) return false;
				rallied = true;
				d.rally(cc, 0, field);
				return true;
			}
			case 17: { // a Defence Tower near the Outpost
				auto sol = d.soldiers(ME);
				const uint32_t out = d.building(ME, BLD_OUTPOST, false);
				float ox = 0, oz = 0;
				if (sol.size() < 2 || !out) return false;
				d.pos_of_building(out, ox, oz);
				return d.build(BLD_TOWER, ox - 6.0f, oz, {sol[0], sol[1]}, 4.0f, 14.0f);
			}
			case 18: { // a Forge, then the Infantry Training technology (soldier_attack_melee_01)
				const uint32_t forge = d.building(ME, BLD_FORGE, true);
				if (!forge) {
					if (acted) return false;
					auto sol = d.soldiers(ME);
					if (sol.size() < 2) return false;
					acted = d.build(BLD_FORGE, cx, cz - 14.0f, {sol[0], sol[1]}, 10.0f, 30.0f);
					return false;
				}
				d.research(forge, TECH_WEAPONS_1);
				return true;
			}
			case 19: return true; // the enemy's attack starts once the research finishes (LaunchAttack)
			case 20: { // repel the attack: attack-move every soldier at the nearest enemy unit
				auto sol = d.soldiers(ME);
				float ex = cx, ez = cz;
				float best = 1e30f;
				bool any = false;
				for (size_t i = 0; i < s.units_.count; ++i) {
					if (!s.units_.alive[i] || s.units_.team[i] != ENEMY) continue;
					const float dx = s.units_.pos_x[i] - cx, dz = s.units_.pos_z[i] - cz;
					if (dx * dx + dz * dz < best) { best = dx * dx + dz * dz; ex = s.units_.pos_x[i]; ez = s.units_.pos_z[i]; any = true; }
				}
				if (!any) return false;
				if (d.clock - last_order < 4.0f) return false;
				last_order = d.clock;
				if (!sol.empty()) d.post((uint8_t)NetCommandType::NET_CMD_ATTACK_MOVE, 0, sol, 0, ex, ez);
				return false;
			}
			case 21: { // a Market and a Temple
				auto c = d.civilians(ME);
				if (c.size() < 4 && !d.soldiers(ME).empty()) c = d.soldiers(ME);
				if (c.size() < 4) return false;
				if (!acted) {
					const bool m = d.build(BLD_MARKET, cx - 18.0f, cz - 18.0f, {c[0], c[1]}, 8.0f, 30.0f);
					const bool t = d.build(BLD_TEMPLE, cx + 18.0f, cz + 14.0f, {c[2], c[3]}, 8.0f, 30.0f);
					acted = m && t;
				}
				return acted;
			}
			case 22: // advance to City Phase at the Civic Centre
				if (d.clock - last_order >= 3.0f) {
					last_order = d.clock;
					d.research(cc, TECH_PHASE_CITY);
				}
				return false;
			case 23: return true; // the City Phase research finishes by itself
			case 24: { // an Arsenal, then two battering rams (the Arsenal waits for City Phase: goal 23's Ready can come first)
				if (s.players_[ME].phase < 3) return false;
				const uint32_t arsenal = d.building(ME, BLD_ARSENAL, true);
				if (!arsenal) {
					if (acted) return false;
					auto sol = d.soldiers(ME);
					if (sol.size() < 4) return false;
					acted = d.build(BLD_ARSENAL, cx + 16.0f, cz + 16.0f, {sol[0], sol[1], sol[2], sol[3]}, 10.0f, 34.0f);
					return false;
				}
				return d.train_template(arsenal, "spart/siege_ram", 2);
			}
			case 25: { // the army gathers on our side of the enemy base, then the soldiers attack the Civic Centre's
				// defenders (six champions) while the rams destroy it once it is in sight
				const uint32_t ecc = d.civic_centre(ENEMY);
				if (!ecc) return false;
				const Simulation::Building *cb = d.find_building(ecc);
				if (!cb) return false;
				std::vector<uint32_t> rams = d.units(ME, "siege_ram");
				std::vector<uint32_t> foot = d.soldiers(ME);
				if (rams.empty()) return false; // the two rams are still in the Arsenal's queue
				std::vector<uint32_t> army = foot;
				army.insert(army.end(), rams.begin(), rams.end());
				if (d.clock - last_order < 8.0f) return false;
				last_order = d.clock;
				// The soldiers draw the Civic Centre's champions off first: the rams (slow, and killed by
				// melee) come in once at most one defender is left by the Civic Centre.
				int defenders = 0;
				for (size_t i = 0; i < s.units_.count; ++i)
					defenders += s.units_.alive[i] && s.units_.team[i] == ENEMY &&
							std::hypot(s.units_.pos_x[i] - cb->x, s.units_.pos_z[i] - cb->z) < 40.0f;
				if (assault_at < 0.0f) assault_at = d.clock;
				const bool rams_go = defenders <= 1 || d.clock - assault_at > 200.0f;
				if (!rams.empty() && rams_go && s.player_can_target_building(ME, ecc)) d.post((uint8_t)NetCommandType::NET_CMD_ATTACK_BUILD, ecc, rams);
				else if (!rams.empty() && rams_go) d.post((uint8_t)NetCommandType::NET_CMD_ATTACK_MOVE, 0, rams, 0, cb->x, cb->z);
				if (!foot.empty()) d.post((uint8_t)NetCommandType::NET_CMD_ATTACK_MOVE, 0, foot, 0, cb->x, cb->z);
				return false; // again every 8 s until the Civic Centre falls
			}
			default: return false;
		}
	}
};

// --- starting_economy_walkthrough.js, goal by goal ----------------------------------------------
struct Economy {
	Driver &d;
	bool acted = false;
	int stage = 0;
	float last_order = -100.0f;
	int houses = 0;
	std::vector<uint32_t> house_builders;
	explicit Economy(Driver &dd) : d(dd) {}
	void new_goal(int) { acted = false; }
	void before(int) {}

	// A herd's or a chicken's id, nearest to a point: the hunt target (a fauna entity).
	uint32_t nearest_fauna(float x, float z) const {
		uint32_t best = 0;
		float bd = 1e30f;
		for (size_t f = 0; f < d.s.fauna_.count; ++f) {
			if (!d.s.fauna_.alive[f]) continue;
			const float dx = d.s.fauna_.pos_x[f] - x, dz = d.s.fauna_.pos_z[f] - z;
			if (dx * dx + dz * dz < bd) { bd = dx * dx + dz * dz; best = d.s.fauna_.id[f]; }
		}
		return best;
	}
	std::vector<uint32_t> cavalry(uint8_t team) const {
		std::vector<uint32_t> out;
		for (size_t i = 0; i < d.s.units_.count; ++i) {
			if (!d.s.units_.alive[i] || d.s.units_.team[i] != team) continue;
			if (std::strstr(civ_unit_template(d.s.units_.tmpl[i]).id, "cavalry")) out.push_back(d.s.units_.id[i]);
		}
		return out;
	}
	std::vector<uint32_t> first(std::vector<uint32_t> v, size_t n) const { if (v.size() > n) v.resize(n); return v; }
	const char *civilian() const {
		return civ_unit_template_by_id("athen/support_civilian") != CIV_UNIT_TEMPLATE_NONE ? "athen/support_civilian" : "athen/support_female_citizen";
	}

	// Returns true when the goal's one-shot order is issued. Orders that must repeat return false.
	bool act(int g) {
		const uint32_t cc = d.civic_centre(ME);
		float cx = 0, cz = 0;
		if (cc) d.pos_of_building(cc, cx, cz);
		auto throttle = [&](float seconds) { if (d.clock - last_order < seconds) return false; last_order = d.clock; return true; };
		switch (g) {
			case 5: { // civilians gather the berries
				const uint32_t fruit = d.nearest_resource(cx, cz, 1);
				auto c = d.civilians(ME);
				if (!fruit || c.empty()) return false;
				d.post((uint8_t)NetCommandType::NET_CMD_GATHER, fruit, c);
				return true;
			}
			case 6: { // infantry gather wood
				const uint32_t tree = d.nearest_resource(cx, cz, 2);
				auto inf = d.soldiers(ME);
				if (!tree || inf.empty()) return false;
				d.post((uint8_t)NetCommandType::NET_CMD_GATHER, tree, first(inf, 5));
				return true;
			}
			case 7: { // cavalry hunt the chickens
				auto cav = cavalry(ME);
				float x = 0, z = 0;
				if (cav.empty() || !d.unit_pos(cav[0], x, z)) return false;
				const uint32_t fauna = nearest_fauna(x, z);
				if (!fauna) return false;
				d.post((uint8_t)NetCommandType::NET_CMD_HUNT, fauna, cav, 0);
				return true;
			}
			case 8: { // the Civic Centre's rally point on the wood
				const uint32_t tree = d.nearest_resource(cx, cz, 2);
				if (!tree || !cc) return false;
				d.rally(cc, tree, cc);
				return true;
			}
			case 9: return d.train_template(cc, "athen/infantry_spearman_b", 5);
			case 10: return false;
			case 11: { // a Storehouse by the trees with the new hoplites
				auto inf = d.soldiers(ME);
				const uint32_t tree = d.nearest_resource(cx, cz, 2);
				float tx = 0, tz = 0;
				if (inf.empty() || !tree || !d.resource_pos(tree, tx, tz)) return false;
				return d.build(BLD_STOREHOUSE, tx, tz, first(inf, 5), 4.0f, 14.0f);
			}
			case 12: return false;
			case 13: // the rally point goes before the civilians are trained
				if (!acted) { acted = true; d.post((uint8_t)NetCommandType::NET_CMD_RALLY, cc, {}, 1); return true; }
				return false;
			case 14: return d.train_template(cc, civilian(), 5);
			case 15: return false;
			case 16: return false;
			case 17: { // two Houses with two of the new civilians, who stay on them until the Houses stand
				auto c = d.civilians(ME);
				if (c.size() < 2) return false;
				if (!throttle(1.0f)) return false;
				if (houses >= 2) return false;
				std::vector<uint32_t> pair = {c[c.size() - 2], c[c.size() - 1]};
				if (d.build(BLD_HOUSE, cx, cz, pair, 10.0f, 34.0f)) {
					house_builders = pair;
					++houses;
				}
				return houses >= 2;
			}
			case 18: return false;
			case 19: { // the Farmstead with the three civilians who are not building Houses
				std::vector<uint32_t> idle;
				for (uint32_t id : d.civilians(ME))
					if (std::find(house_builders.begin(), house_builders.end(), id) == house_builders.end()) idle.push_back(id);
				if (idle.size() < 3) return false;
				return d.build(BLD_FARMSTEAD, cx - 16.0f, cz, first(idle, 3), 10.0f, 34.0f);
			}
			case 20: return false; // the Houses get built
			case 21: { // a Field beside the Farmstead, built by the two house builders (idle once the Houses stand)
				auto c = house_builders;
				const uint32_t fs = d.building(ME, BLD_FARMSTEAD, false);
				float fx = 0, fz = 0;
				if (c.size() < 2 || !fs) return false;
				d.pos_of_building(fs, fx, fz);
				return d.build(BLD_FIELD, fx, fz, {c[0], c[1]}, 6.0f, 20.0f);
			}
			case 22: { // the cavalry hunt the camels
				auto cav = cavalry(ME);
				float x = 0, z = 0;
				if (cav.empty() || !d.unit_pos(cav[0], x, z)) return false;
				if (!throttle(10.0f)) return false;
				const uint32_t fauna = nearest_fauna(x, z);
				if (fauna) d.post((uint8_t)NetCommandType::NET_CMD_HUNT, fauna, cav, 0);
				return false;
			}
			case 23: { // the Civic Centre's rally point on the Field
				const uint32_t field = d.building(ME, BLD_FIELD, false);
				if (!field || !cc) return false;
				d.rally(cc, 0, field);
				return true;
			}
			case 24: { // three civilians, one at a time
				if (!throttle(3.0f)) return false;
				d.train_template(cc, civilian(), 1);
				return false;
			}
			case 25: { // a research at the Farmstead
				const uint32_t fs = d.building(ME, BLD_FARMSTEAD, true);
				if (!fs || !throttle(3.0f)) return false;
				d.research(fs, TECH_GATHER_WICKER_BASKETS);
				return false;
			}
			case 26: { // a Barracks with four soldiers
				auto inf = d.soldiers(ME);
				if (inf.size() < 4) return false;
				return d.build(BLD_BARRACKS, cx + 14.0f, cz - 12.0f, first(inf, 4), 8.0f, 34.0f);
			}
			case 27: return false;
			case 28: // advance to Town Phase (refused until the Barracks stands)
				if (!throttle(3.0f)) return false;
				d.research(cc, TECH_PHASE_TOWN);
				return false;
			case 30: { // stone and then metal by the soldiers, queued from the Civic Centre
				auto inf = d.soldiers(ME);
				const uint32_t stone = d.nearest_resource(cx, cz, 5);
				const uint32_t metal = d.nearest_resource(cx, cz, 6);
				if (inf.size() < 2) return false;
				if (stage == 0 && stone && throttle(1.0f)) { d.post((uint8_t)NetCommandType::NET_CMD_GATHER, stone, first(inf, inf.size() / 2)); stage = 1; return false; }
				if (stage == 1 && metal && throttle(1.0f)) { d.post((uint8_t)NetCommandType::NET_CMD_GATHER, metal, std::vector<uint32_t>(inf.begin() + (long)(inf.size() / 2), inf.end())); stage = 2; return true; }
				return false;
			}
			default: return false;
		}
	}
};

} // namespace

// Plays one tutorial's goals in order: Ready when the button shows, the goal's action when it is
// due, and the goal's trigger advances it. Returns the exit code.
template <class Plan>
int play(Driver &d, Simulation &s, Plan &plan, const char *name, uint8_t tut, int last_goal) {
	int goal = -2;
	float goal_since = 0.0f;
	bool issued = false;
	float issued_at = 0.0f;
	int readies = 0;
	float at_end = -1.0f;
	bool ended_notice = false; // the "Tutorial completed" line of the win (introductory_tutorial.js)
	while (!s.is_game_over() && d.clock < 3600.0f) {
		const int g = s.tutorial_goal_index();
		if (g != goal) {
			if (goal >= 0) std::printf("[%7.1fs] goal %d done (%.1fs) phase %d\n", d.clock, goal, d.clock - goal_since, (int)s.players_[ME].phase);
			goal = g;
			goal_since = d.clock;
			issued = false;
			plan.new_goal(g);
			if (g >= 0 && g <= last_goal) std::printf("[%7.1fs] goal %d: %s\n", d.clock, g, tutorial_goal(tut, g)->lines[0].text);
		}
		if (g < 0) { d.tick_for(1.0f); continue; }
		if (g == last_goal && at_end < 0.0f) at_end = d.clock;
		if (s.tutorial_ready_button()) {
			if (g == last_goal && readies >= 1) break; // the walkthrough's last Ready: nothing follows it
			d.ready();
			++readies;
			continue;
		}
		plan.before(g);
		if (std::getenv("PLAY_TRACE") && g >= 23 && (int)d.clock % 20 == 0) {
			size_t rams = 0;
			for (size_t i = 0; i < s.units_.count; ++i) rams += s.units_.alive[i] && s.units_.team[i] == ME && std::strstr(civ_unit_template(s.units_.tmpl[i]).id, "siege_ram");
			const uint32_t ecc0 = d.civic_centre(ENEMY);
			const Simulation::Building *cb0 = d.find_building(ecc0);
			int def = 0, mine = 0;
			for (size_t i = 0; i < s.units_.count; ++i) {
				if (!s.units_.alive[i] || !cb0) continue;
				def += s.units_.team[i] == ENEMY && std::hypot(s.units_.pos_x[i] - cb0->x, s.units_.pos_z[i] - cb0->z) < 40.0f;
				mine += s.units_.team[i] == ME;
			}
			std::printf("  enemy CC hp %.0f, enemies within 40 m %d, my units %d, my soldiers %zu, can target %d\n", cb0 ? cb0->hp : -1.0f, def, mine, d.soldiers(ME).size(),
					cb0 ? (int)s.player_can_target_building(ME, ecc0) : -1);
			for (uint32_t r : d.units(ME, "siege_ram")) {
				const int32_t i = s.lookup_unit(r);
				if (i >= 0) std::printf("    ram at (%.1f,%.1f) order %d\n", s.units_.pos_x[i], s.units_.pos_z[i], (int)s.units_.order_kind[i]);
			}
			std::printf("  trace %.0f: goal %d rams %zu, arsenal queue %zu, food %d wood %d stone %d gold %d\n", d.clock, g, rams,
					[&] { for (const auto &b : s.buildings_) if (b.team == ME && b.type == BLD_ARSENAL) return b.queue.size(); return (size_t)99; }(),
					s.player_resource(ME, RES_FOOD), s.player_resource(ME, RES_WOOD), s.player_resource(ME, RES_STONE), s.player_resource(ME, RES_GOLD));
		}
		// act() returns true once its one-shot order is issued; the goal is then left alone for
		// 30 s (its trigger advances it), and a goal that does not advance is ordered again.
		if (!issued || d.clock - issued_at > 30.0f)
			if (plan.act(g)) {
				issued = true;
				issued_at = d.clock;
			}
		d.tick_for(0.5f);
		for (const auto &n : s.take_session_notices())
			if (n.kind == Simulation::SessionNotice::WON && n.tech == Simulation::SessionNotice::END_TUTORIAL) ended_notice = true;
		if (d.clock - goal_since > GOAL_LIMIT_S) {
			std::printf("FAIL goal %d stuck for %.0fs: %s\n", g, d.clock - goal_since, tutorial_goal(tut, g)->lines[0].text);
			for (size_t i = 0; i < s.units_.count; ++i)
				if (s.units_.alive[i] && s.units_.team[i] == ME)
					std::printf("  unit %u %s at (%.1f,%.1f)\n", s.units_.id[i], civ_unit_template(s.units_.tmpl[i]).id, s.units_.pos_x[i], s.units_.pos_z[i]);
			for (const Simulation::Building &b : s.buildings_)
				if (b.team == ME) std::printf("  building %u type %u at (%.1f,%.1f) progress %.2f queue %zu\n", b.id, b.type, b.x, b.z, b.build_progress, b.queue.size());
			std::printf("  resources f%d w%d s%d g%d\n", s.player_resource(ME, RES_FOOD), s.player_resource(ME, RES_WOOD),
					s.player_resource(ME, RES_STONE), s.player_resource(ME, RES_GOLD));
			return 1;
		}
	}
	const bool won = s.is_victory_for(ME);
	std::printf("END %s at %.1fs: goal %d, game over %d, won %d\n", name, d.clock, s.tutorial_goal_index(), (int)s.is_game_over(), (int)won);
	const bool reached = s.tutorial_goal_index() == last_goal;
	if (tut == TUTORIAL_INTRODUCTORY && !ended_notice) std::printf("  no \"Tutorial completed\" notice\n");
	if (!reached || (s.is_game_over() && !won) || (tut == TUTORIAL_INTRODUCTORY && (!won || !ended_notice))) {
		std::printf("FAIL the tutorial did not reach its end\n");
		return 1;
	}
	std::printf("RESULT: PASS (%s)\n", name);
	return 0;
}


int main(int argc, char **argv) {
	const std::string map = argc > 1 ? argv[1] : "introductory_tutorial";
	const bool economy = map == "starting_economy_walkthrough";
	if (!economy && map != "introductory_tutorial") {
		std::fprintf(stderr, "usage: play_tutorial introductory_tutorial | starting_economy_walkthrough\n");
		return 2;
	}
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	static Simulation s;
	MapPlayerOverrides ov;
	if (!load_json_map(s, map, 7, ov)) return 2;
	s.set_victory_conditions(0);
	s.set_waves_enabled(false);
	s.set_trigger_difficulty(3);
	s.set_player_active(ME, true, false);
	if (!economy) {
		s.set_player_active(ENEMY, true, true);
		s.set_petra_difficulty(ENEMY, 3);
	}
	s.register_trigger_script(map);
	TurnManager tm;
	Driver d(s, tm);
	d.tick_for(0.8f);
	d.give(5000);
	const uint8_t tut = economy ? TUTORIAL_ECONOMY : TUTORIAL_INTRODUCTORY;
	std::printf("tutorial %s: goal %d, ready %d\n", map.c_str(), s.tutorial_goal_index(), (int)s.tutorial_ready_button());
	if (s.tutorial_id() != tut) {
		std::printf("FAIL the tutorial is not registered\n");
		return 1;
	}
	const int last_goal = tutorial_goal_count(tut) - 1;
	if (economy) {
		Economy plan(d);
		return play(d, s, plan, map.c_str(), tut, last_goal);
	}
	Intro plan(d);
	return play(d, s, plan, map.c_str(), tut, last_goal);
}
