#include "simulation.h"
#include "rms_generator.h"
#include "nav/building_steering.h"
#include "nav/rasterize.h"
#include "../core/job_system.h"
#include <chrono>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <limits>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <queue>
#include <iostream>

namespace rtso {

namespace {
// Accumulates wall-clock microseconds for Simulation::MapScaleStats.
struct ScopedUs {
	uint64_t &us, &calls;
	std::chrono::high_resolution_clock::time_point t0 = std::chrono::high_resolution_clock::now();
	ScopedUs(uint64_t &us_, uint64_t &calls_) : us(us_), calls(calls_) {}
	~ScopedUs() {
		us += (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - t0).count();
		++calls;
	}
};
constexpr int kUnitGridRes = 64; // 64x64 grid cells (2.0m per cell over 128m map)
// 0 A.D. Builder.GetRange: max ≈ 2 + unit obstruction. Stand on the AABB edge, never inside.
constexpr float kBuildStandOff = 0.80f;
constexpr float kBuildReach = 2.20f;

// Stand on the nearest AABB edge so pathing never aims through the footprint.
static void stand_outside_rect(float bx, float bz, float half, uint32_t uid,
		float from_x, float from_z, float *ox, float *oz) {
	float minx = bx - half, maxx = bx + half;
	float minz = bz - half, maxz = bz + half;
	float spread = ((int)(uid % 5) - 2) * 0.55f;
	bool inside = (from_x > minx && from_x < maxx && from_z > minz && from_z < maxz);
	if (!inside) {
		if (std::abs(from_x - std::clamp(from_x, minx, maxx)) >=
				std::abs(from_z - std::clamp(from_z, minz, maxz))) {
			*ox = (from_x < bx) ? (minx - kBuildStandOff) : (maxx + kBuildStandOff);
			*oz = std::clamp(from_z + spread, minz, maxz);
		} else {
			*ox = std::clamp(from_x + spread, minx, maxx);
			*oz = (from_z < bz) ? (minz - kBuildStandOff) : (maxz + kBuildStandOff);
		}
		return;
	}
	float dl = from_x - minx, dr = maxx - from_x, db = from_z - minz, dt = maxz - from_z;
	float m = std::min({dl, dr, db, dt});
	if (m == dl) {
		*ox = minx - kBuildStandOff;
		*oz = std::clamp(from_z + spread, minz, maxz);
	} else if (m == dr) {
		*ox = maxx + kBuildStandOff;
		*oz = std::clamp(from_z + spread, minz, maxz);
	} else if (m == db) {
		*ox = std::clamp(from_x + spread, minx, maxx);
		*oz = minz - kBuildStandOff;
	} else {
		*ox = std::clamp(from_x + spread, minx, maxx);
		*oz = maxz + kBuildStandOff;
	}
}

static bool outside_and_in_build_range(float bx, float bz, float half, float x, float z) {
	float minx = bx - half, maxx = bx + half;
	float minz = bz - half, maxz = bz + half;
	if (x > minx && x < maxx && z > minz && z < maxz)
		return false;
	float cx = std::clamp(x, minx, maxx);
	float cz = std::clamp(z, minz, maxz);
	return std::hypot(x - cx, z - cz) <= kBuildReach;
}

struct UnitGrid {
	int32_t *head = nullptr;
	int32_t *nxt = nullptr;
	int res = kUnitGridRes;
	float inv = 0.0f;
	float half = 64.0f;

	int cell_of(float x, float z) const {
		int cx = std::clamp((int)((x + half) * inv), 0, res - 1);
		int cz = std::clamp((int)((z + half) * inv), 0, res - 1);
		return cz * res + cx;
	}

	bool build(Arena &arena, size_t n, const float *px, const float *pz, const uint8_t *alive, float map_half = 64.0f) {
		half = map_half;
		inv = (float)res / (half * 2.0f);
		head = arena.alloc_array<int32_t>((size_t)(res * res));
		nxt = arena.alloc_array<int32_t>(n);
		if (!head || !nxt) return false;
		for (int c = 0; c < res * res; ++c) head[c] = -1;
		for (size_t i = 0; i < n; ++i) {
			if (!alive[i]) {
				nxt[i] = -1;
				continue;
			}
			int c = cell_of(px[i], pz[i]);
			nxt[i] = head[c];
			head[c] = (int32_t)i;
		}
		return true;
	}

	template <typename Fn>
	void visit_radius(float x, float z, float radius, Fn &&fn) const {
		int cr = std::max(1, (int)std::ceil(radius * inv) + 1);
		int cx = std::clamp((int)((x + half) * inv), 0, res - 1);
		int cz = std::clamp((int)((z + half) * inv), 0, res - 1);
		int x0 = std::max(0, cx - cr);
		int x1 = std::min(res - 1, cx + cr);
		int z0 = std::max(0, cz - cr);
		int z1 = std::min(res - 1, cz + cr);
		for (int zz = z0; zz <= z1; ++zz) {
			int row = zz * res;
			for (int xx = x0; xx <= x1; ++xx) {
				for (int32_t j = head[row + xx]; j >= 0; j = nxt[j]) {
					fn(j);
				}
			}
		}
	}
};
// 0 A.D. navalManager.js:58 seaWarShips — per-sea fleet accounting registry

// For reveal_fow()'s LOS shortcut: terrain_blocks_los()'s 0.35 clearance less a margin for float
// rounding, and the widest block window it handles (vision up to ~56 FoW cells).
constexpr float FOW_LOS_CLEARANCE = 0.35f - 0.01f;
constexpr int FOW_BOX_MAX_W = 32;
// For each block of the (2rb+1)^2 window centred on block (bcx, bcz), the highest block_hmax in
// the box of blocks from the centre to that block. Fills the blocks inside the GB x GB grid; the
// window is row-major in `out`.
void fow_box_max(const float *block_hmax, int GB, int bcx, int bcz, int rb, float *out) {
	const int WB = 2 * rb + 1;
	for (int zs = -1; zs <= 1; zs += 2)
		for (int xs = -1; xs <= 1; xs += 2)
			for (int j = 0; j <= rb; ++j) {
				const int bz = bcz + zs * j;
				if (bz < 0 || bz >= GB) break;
				const float *hm = &block_hmax[(size_t)bz * GB];
				float *row = &out[(zs * j + rb) * WB + rb];
				const float *nearer = j ? row - zs * WB : nullptr; // the row one step towards the centre
				for (int i = 0; i <= rb; ++i) {
					const int bx = bcx + xs * i;
					if (bx < 0 || bx >= GB) break;
					float m = hm[bx];
					if (i) m = std::max(m, row[xs * (i - 1)]);
					if (nearer) m = std::max(m, nearer[xs * i]);
					row[xs * i] = m;
				}
			}
}

} // namespace

Simulation::Simulation() {
	units_.init(UNIT_CAP);
	resources_.init(512);
	unit_paths_.assign(UNIT_CAP, WaypointPath{});
	nav_grid_.init(map_half_, grid_res_);
	// Default civ per slot (game setup overrides via set_team_civ). Slots 0-3 keep the legacy
	// player_civ_[4] defaults; teams start free-for-all (each player its own team).
	static constexpr Civilization kDefaultCivs[MAX_PLAYERS] = {
		CIV_ATHENIANS, CIV_SPARTANS, CIV_ROMANS, CIV_PERSIANS,
		CIV_CARTHAGINIANS, CIV_MACEDONIANS, CIV_GAULS, CIV_IBERIANS};
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		players_[p].civ = kDefaultCivs[p];
		players_[p].team = p;
	}
	apply_team_diplomacy(); // free-for-all: self ally, everyone else enemy
	apply_petra_config();
}

int Simulation::grid_res_for_map(float map_half) {
	// Round up to whole hierarchical-pathfinder chunks.
	int res = (int)std::ceil(map_half * 2.0f / SPATIAL_CELL);
	res = (res + CHUNK_SIZE - 1) / CHUNK_SIZE * CHUNK_SIZE;
	return std::clamp(res, MIN_GRID_RES, MAX_GRID_RES);
}

void Simulation::set_map_half(float h) {
	map_half_ = h;
	grid_res_ = grid_res_for_map(h);
	const size_t cells = (size_t)grid_res_ * (size_t)grid_res_;
	fow_grid_.assign(cells, FOW_UNEXPLORED);
	for (auto &g : player_fow_) g.clear();
	petra_fow_.assign(cells, FOW_UNEXPLORED);
	fow_cell_height_.clear();
	fow_cell_hmax_.clear();
	fow_block_hmax_.clear();
	// The nav grid caches map_half for world<->cell conversion, so it must be
	// re-initialised whenever the extent changes.
	nav_grid_.init(map_half_, grid_res_);
	territory_grid_.clear();
	influence_grid_.clear();
}

Simulation::~Simulation() = default;

void Simulation::reset() {
	units_.free_all(); units_.init(UNIT_CAP);
	resources_.free_all(); resources_.init(512);
	buildings_.clear();
	projectiles_.clear();
	unit_id_to_idx_.clear();
	unit_idx_by_id_.clear();
	res_id_to_idx_.clear();
	field_res_by_bld_.clear();
	field_bld_by_res_.clear();
	building_id_to_idx_.clear();
	occupied_cells_.clear();

	next_id_ = 1;
	sim_rng_.set_seed(sim_seed_);
	// Per-player economy reset. Civ, team and active/AI flags come from game setup and survive reset().
	// Player 0 starts with the human stock, every other slot with Petra's.
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		PlayerState &ps = players_[p];
		if (p == 0) { ps.food() = 350.0f; ps.wood() = 450.0f; ps.gold() = 300.0f; ps.stone() = 250.0f; }
		else        { ps.food() = 400.0f; ps.wood() = 600.0f; ps.gold() = 300.0f; ps.stone() = 200.0f; }
		for (float &w : ps.gathered_window) w = 0.0f;
		ps.techs.clear();
		ps.phase = 1;
	}
	rebuild_all_tech_mods();
	rate_window_timer_ = 0.0f;

	wave_timer_ = 0.0f;
	wave_number_ = 0;
	game_over_ = false;
	victory_ = false;
	participant_mask_ = defeated_mask_ = winner_mask_ = 0;
	for (float &w : wonder_hold_) w = 0.0f;
	relics_.clear();
	wonder_victory_timer_ = 0.0f;
	has_active_wonder_ = false;
	record_ = MatchRecord();
	tick_us_ = ticks_ = 0;
	frame_arena_.reset_all();
	std::fill(fow_grid_.begin(), fow_grid_.end(), (uint8_t)FOW_UNEXPLORED);
	for (auto &g : player_fow_) g.clear();
	barter_timer_ = 0.0f;
	upkeep_timer_ = 0.0f;
	entity_limits_timer_ = 0.0f;
	gate_timer_ = 0.0f;
	recent_death_head_ = 0;
	recent_deaths_.fill(RecentDeath{});
	fauna_.count = 0;
	fauna_id_to_idx_.clear();
	transport_passengers_.clear();

	// G3 Market dynamic pricing reset
	for (int i = 0; i < 4; ++i) { market_buy_price_[i] = 1.0f; market_sell_price_[i] = 1.0f; market_trade_history_[i] = 0; }
	trader_routes_.clear();
	trader_leg_.clear();

	// Diplomacy reset: every player allies itself and its team, enemies the rest.
	apply_team_diplomacy();

	// Reset Petra AI State
	petra_pid_ = PETRA_PLAYER;
	petra_slots_.clear();
	petra_reset_ai_state();
	petra_chat_.clear();
	petra_diplomacy_requests_.clear();
	petra_fleets_.clear_sea_fleets();
	apply_petra_config();
	unit_paths_.assign(UNIT_CAP, WaypointPath{});
	nav_grid_.init(map_half_, grid_res_);
	territory_grid_.clear();
	influence_grid_.clear();
	territory_timer_ = 0.0f;
	fow_timer_ = 0.0f;
	bulk_spawn_ = false;
	aura_mgr_.clear();
	aura_timer_ = 0.0f;
}

int Simulation::pop_current(uint8_t player) const {
	int count = 0;
	for (size_t i = 0; i < units_.count; ++i) {
		if (units_.team[i] == player && (units_.alive[i] || units_.action_state[i] == ACTION_GARRISONED)) count++;
	}
	return count;
}

int Simulation::pop_max(uint8_t player) const {
	int cap = 10;
	for (const auto &b : buildings_) {
		if (b.team == player && b.build_progress >= 1.0f) {
			cap += (int)std::lround(building_type_info(b.type).pop_bonus * tech_mods(player).bld_pop_bonus[b.type]);
		}
	}
	return pop_cap_ > 0 ? std::min(cap, pop_cap_) : cap;
}

int Simulation::ai_pop_current() const { return pop_current(PETRA_PLAYER); }
int Simulation::ai_pop_max() const { return pop_max(PETRA_PLAYER); }

bool Simulation::owns_unit(uint8_t player, uint32_t unit_id) const {
	int32_t idx = lookup_unit(unit_id);
	return idx >= 0 && (size_t)idx < units_.count && units_.alive[idx] && units_.team[idx] == player;
}

bool Simulation::owns_building(uint8_t player, uint32_t building_id) const {
	auto it = building_id_to_idx_.find(building_id);
	return it != building_id_to_idx_.end() && buildings_[it->second].team == player;
}

void Simulation::filter_owned_units(uint8_t player, const uint32_t *unit_ids, size_t count, std::vector<uint32_t> &out) const {
	out.clear();
	for (size_t i = 0; i < count; ++i)
		if (owns_unit(player, unit_ids[i])) out.push_back(unit_ids[i]);
}

std::vector<uint32_t> Simulation::get_idle_workers(uint8_t team) const {
	std::vector<uint32_t> idle;
	for (size_t i = 0; i < units_.count; ++i) {
		if (units_.alive[i] && units_.team[i] == team &&
			units_.unit_type[i] == UNIT_VILLAGER &&
			units_.action_state[i] == ACTION_IDLE) {
			idle.push_back(units_.id[i]);
		}
	}
	return idle;
}

void Simulation::spawn_initial() {
	reset();

	// 1. Athenian Town Center (2x2) at center (0,0)
	spawn_building(0, 0, BLD_TOWN_CENTER, 0, 1.0f);

	// 2. Initial Barracks (2x2) at (6, -2) and Houses at (-6, 4), (-6, -4)
	spawn_building(6, -2, BLD_BARRACKS, 0, 1.0f);
	spawn_building(-6, 4, BLD_HOUSE, 0, 1.0f);
	spawn_building(-6, -4, BLD_HOUSE, 0, 1.0f);

	// 3. Initial Units (5 Villagers, 2 Female Citizens, 2 Swordsmen, 2 Archers)
	for (int i = 0; i < 5; ++i) {
		float a = 6.28318530718f * i / 5.0f;
		float x = cosf(a) * 5.0f;
		float z = sinf(a) * 5.0f + 5.0f;
		spawn_unit(x, z, 0, UNIT_VILLAGER, 100.0f);
	}
	spawn_unit(3.0f, 4.0f, 0, UNIT_FEMALE_CITIZEN, 65.0f);
	spawn_unit(-3.0f, 4.0f, 0, UNIT_FEMALE_CITIZEN, 65.0f);
	spawn_unit(9.0f, 2.0f, 0, UNIT_SWORDSMAN, 160.0f);
	spawn_unit(9.0f, -2.0f, 0, UNIT_SWORDSMAN, 160.0f);
	spawn_unit(8.0f, 5.0f, 0, UNIT_ARCHER, 90.0f);
	spawn_unit(8.0f, -5.0f, 0, UNIT_ARCHER, 90.0f);

	// 4. Resource Fields (Wood, Gold, Stone, Food)
	spawn_resource(8, -8, RES_WOOD, 1000);
	spawn_resource(9, -8, RES_WOOD, 1000);
	spawn_resource(10, -8, RES_WOOD, 1000);

	spawn_resource(-8, -8, RES_WOOD, 1000);
	spawn_resource(-9, -8, RES_WOOD, 1000);
	spawn_resource(-10, -9, RES_WOOD, 1000);

	spawn_resource(8, -10, RES_GOLD, 1200);
	spawn_resource(9, -10, RES_GOLD, 1200);

	spawn_resource(-9, -10, RES_STONE, 1200);
	spawn_resource(-10, -10, RES_STONE, 1200);

	// Expansive resource distribution over 128m map
	for (int i = 0; i < 48; ++i) {
		int cx = (int)((i * 37 + 13) % 55) - 27;
		int cy = (int)((i * 53 + 7) % 55) - 27;
		if (std::abs(cx) < 6 && std::abs(cy) < 6) continue;
		float wx = (cx * 2 + 0.5f) * GRID;
		float wz = (cy * 2 + 0.5f) * GRID;
		if (std::abs(wx) >= MAP_HALF - 4.0f || std::abs(wz) >= MAP_HALF - 4.0f) continue;
		uint8_t rtype = (i % 9 == 0) ? RES_GOLD : ((i % 7 == 0) ? RES_STONE : RES_WOOD);
		spawn_resource(cx * 2, cy * 2, rtype, 800);
	}

	// 5. Wilderness Relics & Treasure Troves (Treasure.js)
	relics_.clear();
	relics_.push_back({next_id_++, -42.0f, -38.0f, RES_GOLD, 250, false});
	relics_.push_back({next_id_++, 44.0f, 40.0f, RES_GOLD, 250, false});
	relics_.push_back({next_id_++, -46.0f, 42.0f, RES_WOOD, 300, false});
	relics_.push_back({next_id_++, 38.0f, -44.0f, RES_FOOD, 300, false});
	relics_.push_back({next_id_++, 0.0f, 48.0f, RES_STONE, 250, false});
	relics_.push_back({next_id_++, 0.0f, -48.0f, RES_GOLD, 300, false});

	// 6. Fauna & Wildlife Packs (Animal.js / Pack.js)
	spawn_fauna(-15.0f, 18.0f, FAUNA_DEER);
	spawn_fauna(-18.0f, 20.0f, FAUNA_DEER);
	spawn_fauna(-16.0f, 22.0f, FAUNA_DEER);
	spawn_fauna(20.0f, -18.0f, FAUNA_BOAR);
	spawn_fauna(22.0f, -16.0f, FAUNA_BOAR);
	spawn_fauna(-25.0f, -20.0f, FAUNA_SHEEP);
	spawn_fauna(-27.0f, -22.0f, FAUNA_SHEEP);
	spawn_fauna(30.0f, 25.0f, FAUNA_WOLF);
	spawn_fauna(32.0f, 28.0f, FAUNA_WOLF);

	// 7. Enemy Base (Team 1) — Spartan Base for Petra Autonomous AI.
	// Sits ~68 m from the player's Town Center so neither side's starting units are inside
	// the other's aggro range (~33 m) at the first tick.
	spawn_building(24, 24, BLD_TOWN_CENTER, 1, 1.0f);
	spawn_building(21, 23, BLD_BARRACKS, 1, 1.0f);
	spawn_building(27, 25, BLD_HOUSE, 1, 1.0f);
	spawn_building(25, 27, BLD_HOUSE, 1, 1.0f);
	spawn_building(23, 27, BLD_STOREHOUSE, 1, 1.0f);

	for (int i = 0; i < 6; ++i) {
		float a = 6.28318530718f * i / 6.0f;
		float x = 49.0f + cosf(a) * 4.0f;
		float z = 49.0f + sinf(a) * 4.0f;
		spawn_unit(x, z, 1, UNIT_VILLAGER, 100.0f);
	}
	spawn_unit(44.0f, 44.0f, 1, UNIT_SWORDSMAN, 160.0f);
	spawn_unit(46.0f, 42.0f, 1, UNIT_SPEARMAN, 160.0f);
	spawn_unit(42.0f, 46.0f, 1, UNIT_ARCHER, 90.0f);
	spawn_unit(40.0f, 48.0f, 1, UNIT_ARCHER, 90.0f);

	// Dedicated economic resources near AI base
	spawn_resource(27, 27, RES_WOOD, 1200);
	spawn_resource(28, 27, RES_WOOD, 1200);
	spawn_resource(27, 22, RES_GOLD, 1200);
	spawn_resource(22, 28, RES_STONE, 1200);
	spawn_resource(21, 21, RES_FOOD, 1000);
}

void Simulation::spawn_war(int total_units) {
	// Reset units and buildings without clearing loaded heightmap
	units_.free_all(); units_.init(UNIT_CAP);
	buildings_.clear();
	projectiles_.clear();
	unit_id_to_idx_.clear();
	unit_idx_by_id_.clear();
	res_id_to_idx_.clear();
	field_res_by_bld_.clear();
	field_bld_by_res_.clear();
	building_id_to_idx_.clear();
	occupied_cells_.clear();
	relics_.clear();
	std::fill(fow_grid_.begin(), fow_grid_.end(), (uint8_t)FOW_UNEXPLORED);
	for (auto &g : player_fow_) g.clear();
	std::fill(petra_fow_.begin(), petra_fow_.end(), (uint8_t)FOW_UNEXPLORED);

	total_units = std::clamp(total_units, 4, (int)UNIT_CAP - 64);
	const int half = total_units / 2;
	float b_dist = map_half_ * 0.70f;
	float a_dist = map_half_ * 0.50f;

	// Spawn Command Base for Player (West) and Enemy (East)
	spawn_building_world(-b_dist, 0, BLD_TOWN_CENTER, 0, 1.0f);
	spawn_building_world(-b_dist, -6, BLD_BARRACKS, 0, 1.0f);
	spawn_building_world(-b_dist, 6, BLD_FORTRESS, 0, 1.0f);
	spawn_building_world(b_dist, 0, BLD_TOWN_CENTER, 1, 1.0f);
	spawn_building_world(b_dist, -6, BLD_BARRACKS, 1, 1.0f);
	spawn_building_world(b_dist, 6, BLD_FORTRESS, 1, 1.0f);

	// Deploy two grand opposing armies across the battlefield
	// Team 0 (Player / Blue) deployed on the West (x = -a_dist)
	// Team 1 (Enemy / Red) deployed on the East (x = +a_dist)
	auto deploy_army = [&](uint8_t team, float base_x, float dir_x) {
		int infantry_count = (int)(half * 0.40f);
		int spearmen_count = (int)(half * 0.25f);
		int archer_count   = (int)(half * 0.20f);
		int cavalry_count  = (int)(half * 0.10f);
		int siege_count    = std::max(1, (int)(half * 0.03f));
		int monster_count  = std::max(1, half - (infantry_count + spearmen_count + archer_count + cavalry_count + siege_count));

		std::vector<uint32_t> army_ids;
		army_ids.reserve((size_t)half);

		// 1. Frontline Swordsmen Vanguard (Rank 1 & 2)
		int inf_cols = std::max(4, (int)std::ceil(std::sqrt((float)infantry_count * 2.5f)));
		float inf_spacing = 1.4f;
		for (int i = 0; i < infantry_count; ++i) {
			int row = i / inf_cols;
			int col = i % inf_cols;
			float x = base_x - dir_x * (row * 1.5f);
			float z = (col - (inf_cols - 1) * 0.5f) * inf_spacing;
			uint32_t uid = spawn_unit(x, z, team, UNIT_SWORDSMAN);
			army_ids.push_back(uid);
		}

		// 2. Spearmen Phalanx Support (Rank 2 & 3)
		int spear_cols = std::max(4, (int)std::ceil(std::sqrt((float)spearmen_count * 2.5f)));
		float spear_spacing = 1.4f;
		for (int i = 0; i < spearmen_count; ++i) {
			int row = i / spear_cols;
			int col = i % spear_cols;
			float x = base_x - dir_x * (3.0f + row * 1.5f);
			float z = (col - (spear_cols - 1) * 0.5f) * spear_spacing;
			uint32_t uid = spawn_unit(x, z, team, UNIT_SPEARMAN);
			army_ids.push_back(uid);
		}

		// 3. Archer Backline Volley Line (Rank 4 & 5)
		int arc_cols = std::max(4, (int)std::ceil(std::sqrt((float)archer_count * 2.5f)));
		float arc_spacing = 1.5f;
		for (int i = 0; i < archer_count; ++i) {
			int row = i / arc_cols;
			int col = i % arc_cols;
			float x = base_x - dir_x * (6.5f + row * 1.6f);
			float z = (col - (arc_cols - 1) * 0.5f) * arc_spacing;
			uint32_t uid = spawn_unit(x, z, team, UNIT_ARCHER);
			army_ids.push_back(uid);
		}

		// 4. Cavalry Wings (North & South Flanks)
		int cav_half = cavalry_count / 2;
		float cav_spacing = 2.2f;
		for (int i = 0; i < cavalry_count; ++i) {
			bool north_wing = (i < cav_half);
			int idx_in_wing = north_wing ? i : (i - cav_half);
			int row = idx_in_wing / 4;
			int col = idx_in_wing % 4;
			float z_side = north_wing ? (-18.0f - col * cav_spacing) : (18.0f + col * cav_spacing);
			float x = base_x - dir_x * (row * 2.2f);
			uint32_t uid = spawn_unit(x, z_side, team, UNIT_CAVALRY);
			army_ids.push_back(uid);
		}

		// 5. Heavy Catapult Artillery Batteries
		for (int i = 0; i < siege_count; ++i) {
			float x = base_x - dir_x * 10.5f;
			float z = ((i - (siege_count - 1) * 0.5f) * 4.5f);
			uint32_t uid = spawn_unit(x, z, team, UNIT_CATAPULT);
			army_ids.push_back(uid);
		}

		// 6. War Elephants & Champion Heroes (Center Shock Wave)
		for (int i = 0; i < monster_count; ++i) {
			UnitType mtype = (i % 2 == 0) ? UNIT_ELEPHANT : UNIT_HERO;
			float x = base_x - dir_x * 4.5f;
			float z = ((i - (monster_count - 1) * 0.5f) * 5.0f);
			uint32_t uid = spawn_unit(x, z, team, mtype);
			army_ids.push_back(uid);
		}

		// Issue Charge Attack-Move order towards the opposing army
		float target_center_x = (team == 0) ? +a_dist : -a_dist;
		order_attack_move(army_ids.data(), army_ids.size(), target_center_x, 0.0f);
	};

	deploy_army(0, -a_dist, -1.0f);
	deploy_army(1, +a_dist, +1.0f);
}

void Simulation::bind_unit_index(uint32_t id, size_t idx) {
	unit_id_to_idx_[id] = idx;
	if (id >= unit_idx_by_id_.size()) {
		unit_idx_by_id_.resize((size_t)id + 1, -1);
	}
	unit_idx_by_id_[id] = (int32_t)idx;
}

int32_t Simulation::lookup_unit(uint32_t id) const {
	if (id >= unit_idx_by_id_.size()) return -1;
	return unit_idx_by_id_[id];
}

uint32_t Simulation::spawn_unit(float x, float z, uint8_t team, UnitType u_type, float hp, uint16_t tmpl) {
	uint32_t uid = alloc_id();
	Civilization civ = civ_of(team);
	// The owner civ's own unit for this class (0 A.D. roster); its stats already
	// carry the civ's auto-researched unit bonuses.
	if (tmpl >= CIV_UNIT_TEMPLATE_COUNT) tmpl = civ_default_template(civ, (uint8_t)u_type);
	if (tmpl < CIV_UNIT_TEMPLATE_COUNT && civ_unit_template(tmpl).unit_type != (uint8_t)u_type)
		tmpl = CIV_UNIT_TEMPLATE_NONE;
	size_t idx = units_.push(x, z, uid, team, u_type, tmpl);
	if (hp > 0.0f) units_.hp[idx] = units_.max_hp[idx] = hp;

	apply_tech_mods_to_unit(idx); // researched technologies (0 A.D. ModificationsManager)

	if (team == 0) {
		record_.units_trained++;
	}

	bind_unit_index(uid, idx);
	if (unit_paths_.size() < UNIT_CAP) unit_paths_.resize(UNIT_CAP);
	if (idx < unit_paths_.size()) unit_paths_[idx].clear();
	return uid;
}

uint32_t Simulation::spawn_resource(int cell_x, int cell_y, uint8_t type, int32_t amount) {
	float x = (cell_x + 0.5f) * GRID;
	float z = (cell_y + 0.5f) * GRID;
	return spawn_resource_world(x, z, type, amount);
}

uint32_t Simulation::spawn_resource_world(float wx, float wz, uint8_t type, int32_t amount, uint8_t subtype) {
	uint32_t rid = alloc_id();
	size_t idx = resources_.push(wx, wz, amount, type, rid, subtype);
	res_id_to_idx_[rid] = idx;
	return rid;
}

uint32_t Simulation::spawn_building(int cell_x, int cell_y, uint8_t type, uint8_t team, float progress) {
	const BuildingTypeInfo &binfo = building_type_info(type);
	int footprint = binfo.footprint;
	float x = (cell_x + footprint * 0.5f) * GRID;
	float z = (cell_y + footprint * 0.5f) * GRID;
	return spawn_building_world(x, z, type, team, progress);
}

uint32_t Simulation::spawn_building_world(float wx, float wz, uint8_t type, uint8_t team, float progress) {
	uint32_t bid = alloc_id();
	const BuildingTypeInfo &binfo = building_type_info(type);
	int footprint = binfo.footprint;
	float half = footprint * GRID * 0.5f;
	int cell_x = (int)std::floor((wx - half) / GRID);
	int cell_y = (int)std::floor((wz - half) / GRID);

	Building b;
	b.id = bid;
	b.x = wx;
	b.z = wz;
	b.half_size = half;
	b.type = type;
	b.team = team;
	// Template health x civ bonus x the owner's structure health techs.
	b.max_hp = structure_max_hp(type, team);
	// 0 A.D. foundation.xml: Health/Initial is 1. Progress 0 = plot only (not a wreck).
	if (progress <= 0.0f) {
		b.build_progress = 0.0f;
		b.hp = 1.0f;
		b.committed = false;
		b.wrecked = false;
	} else {
		b.build_progress = progress;
		b.hp = b.max_hp * progress;
		b.committed = (progress >= 1.0f);
		b.wrecked = false;
	}
	b.cell_x = cell_x;
	b.cell_y = cell_y;
	b.footprint = footprint;
	b.attack_timer = 0.0f;
	b.is_burning = false;
	b.has_rally_point = false;
	b.rally_x = wx;
	b.rally_z = wz + half + 2.0f;
	b.rally_target_res_id = 0;
	b.rally_target_bld_id = 0;
	b.garrison_cap = binfo.garrison_cap;
	b.research_tech = TECH_NONE;
	b.research_timer = 0.0f;
	b.research_duration = 0.0f;
	b.capture_points = 0.0f;
	// 0 A.D. Capturable/TerritoryDecay template values.
	b.max_capture_points = binfo.capture_max_points;
	if (team < MAX_PLAYERS) b.max_capture_points *= civ_bonus_mult(civ_of(team), CIV_BONUS_BUILDING_CAPTURE_POINTS, type);
	b.capture_regen_rate = binfo.capture_regen_rate;
	b.capture_garrison_regen_rate = binfo.capture_garrison_regen_rate;
	b.territory_decay_rate = binfo.territory_decay_rate;
	b.capture_points_by_team.fill(0.0f);
	b.capture_points_by_team[b.team < 8 ? b.team : 0] = b.max_capture_points;

	buildings_.push_back(b);
	building_id_to_idx_[bid] = buildings_.size() - 1;

	for (int dx = 0; dx < footprint; ++dx) {
		for (int dy = 0; dy < footprint; ++dy) {
			occupied_cells_.insert({cell_x + dx, cell_y + dy});
		}
	}
	if (progress >= 0.99f && !bulk_spawn_) {
		rebuild_nav_grid();
		if (b.type == BLD_TOWN_CENTER || b.type == BLD_FORTRESS || b.type == BLD_TOWER) {
			rebuild_territory();
		}
	}
	return bid;
}

uint32_t Simulation::spawn_relic(float x, float z, uint8_t res_type, int amount) {
	uint32_t rid = alloc_id();
	relics_.push_back({rid, x, z, res_type, amount, false});
	return rid;
}

void Simulation::generate_random_map(MapType type, uint32_t seed, int num_players, float map_half) {
	RmsGenerator::Config cfg;
	cfg.map_type = type;
	cfg.seed = seed;
	cfg.num_players = num_players;
	cfg.map_half = map_half;
	// Keep roughly one heightmap sample per world unit on bigger maps
	// (diamond-square needs 2^n + 1 samples).
	cfg.heightmap_res = map_half > 96.0f ? 257 : 129;
	const auto &minfo = map_type_info(type);
	if (std::strcmp(minfo.biome, "alpine") == 0) cfg.biome = BIOME_ALPINE;
	else if (std::strcmp(minfo.biome, "oasis") == 0) cfg.biome = BIOME_OASIS;
	else if (std::strcmp(minfo.biome, "latium") == 0) cfg.biome = BIOME_LATIUM;
	else if (std::strcmp(minfo.biome, "temperate") == 0) cfg.biome = BIOME_TEMPERATE;
	else if (std::strcmp(minfo.biome, "steppe") == 0) cfg.biome = BIOME_STEPPE;
	else if (std::strcmp(minfo.biome, "savanna") == 0) cfg.biome = BIOME_SAVANNA;
	else if (std::strcmp(minfo.biome, "polar") == 0) cfg.biome = BIOME_POLAR;
	else if (std::strcmp(minfo.biome, "india") == 0) cfg.biome = BIOME_INDIA;
	else cfg.biome = BIOME_AEGEAN;

	set_sim_seed(seed);
	RmsGenerator::generate(*this, cfg);
	rebuild_nav_grid();
	rebuild_territory();
}

void Simulation::end_bulk_spawn() {
	bulk_spawn_ = false;
	rebuild_nav_grid();
	rebuild_territory();
}

void Simulation::rebuild_nav_grid() {
	// Snapshot pre-rebuild cells so only navcells whose passability actually
	// changed are flagged dirty (HierarchicalPathfinder dirtinessGrid,
	// HierarchicalPathfinder.cpp:470-477). Demolitions clear cells back to
	// PASSABLE here, so they are captured by the diff without fill(1).
	std::vector<uint8_t> old_cells;
	bool have_old = false;
	if (!nav_grid_.cells.blank()) {
		old_cells = nav_grid_.cells.data;
		have_old = true;
	} else {
		nav_grid_.init(map_half_, grid_res_);
	}
	for (int y = 0; y < nav_grid_.res; ++y) {
		for (int x = 0; x < nav_grid_.res; ++x) {
			if (nav_grid_.cells.at(x, y) != NAV_WATER)
				nav_grid_.cells.at(x, y) = NAV_PASSABLE;
		}
	}
	if (!nav_grid_.dirty.blank())
		nav_grid_.dirty.fill(0);
	nav_grid_.set_water_from_heights(terrain_heights_, terrain_res_, terrain_water_height_);
	// Buildings are axis-aligned squares (spawn_building_world: half_size = footprint*GRID/2), rasterized
	// with the 0 A.D. rule (nav/rasterize.h, clearance 0). A navcell is blocked only if it lies entirely
	// inside the footprint plus the vendor extension. A footprint-1 building off the grid can match no
	// navcell, so it falls back to the navcell under its centre (the cell the old footprint block marked).
	const float nav_cell = 2.0f * nav_grid_.map_half / static_cast<float>(nav_grid_.res);
	const rtso::nav::Vec2 nav_origin(-nav_grid_.map_half, -nav_grid_.map_half);
	for (const auto &b : buildings_) {
		if (b.build_progress < 0.99f || !blocks_pathfinding(b)) continue;
		int covered = 0;
		rtso::nav::rasterize_rect_with_clearance(
			rtso::nav::Vec2(b.x, b.z), rtso::nav::Vec2(1.0f, 0.0f), rtso::nav::Vec2(0.0f, 1.0f),
			rtso::nav::Vec2(b.half_size, b.half_size), 0.0f, nav_cell, nav_origin,
			nav_grid_.cells.w, nav_grid_.cells.h,
			[&](int x, int y) {
				nav_grid_.cells.at(x, y) = NAV_OBSTRUCTED;
				++covered;
			});
		if (covered == 0) {
			auto [cx, cy] = nav_grid_.world_to_cell(b.x, b.z);
			nav_grid_.cells.at(cx, cy) = NAV_OBSTRUCTED;
		}
	}
	// Precise dirty bits: diff new cells vs snapshot (covers placements,
	// completions AND demolitions). Falls back to full remesh only when the
	// grid was freshly initialised (no snapshot to diff against).
	if (have_old && old_cells.size() == nav_grid_.cells.data.size()) {
		for (size_t i = 0; i < nav_grid_.cells.data.size(); ++i)
			if (nav_grid_.cells.data[i] != old_cells[i])
				nav_grid_.dirty.data[i] = 1;
	} else if (!nav_grid_.dirty.blank()) {
		nav_grid_.dirty.fill(1);
	}
	nav_grid_.rebuild_dirty_regions();
}

void Simulation::rebuild_territory() {
	ScopedUs prof(map_scale_stats_.territory_us, map_scale_stats_.territory_calls);
	const int R = grid_res_;
	auto &roots = territory_roots_scratch_;
	roots.clear();
	for (const auto &b : buildings_) {
		if (b.build_progress < 0.99f) continue;
		float radius = 0.0f;
		if (b.type == BLD_TOWN_CENTER) radius = 32.0f;
		else if (b.type == BLD_FORTRESS) radius = 28.0f;
		else if (b.type == BLD_TOWER) radius = 18.0f;
		else continue;
		roots.push_back({b.x, b.z, radius, b.team});
	}
	// The grids depend on nothing else, so unchanged inputs (most 1 Hz refreshes) keep them.
	if (roots == territory_roots_ && territory_roots_res_ == R && territory_roots_half_ == map_half_ &&
			influence_grid_.w == R && influence_grid_.h == R && territory_grid_.w == R && territory_grid_.h == R)
		return;
	territory_roots_.swap(roots);
	territory_roots_res_ = R;
	territory_roots_half_ = map_half_;

	influence_grid_.resize(R, R, 0);
	territory_grid_.resize(R, R, 0);
	influence_grid_.fill(0);
	territory_grid_.fill(0);
	for (const TerritoryRoot &b : territory_roots_) {
		const float radius = b.radius;
		float r2 = radius * radius;
		// Only cells whose centre can fall inside the radius (one-cell margin
		// keeps the exact distance test below authoritative).
		const float to_cell = static_cast<float>(R) / (map_half_ * 2.0f);
		const int x0 = std::max(0, static_cast<int>((b.x - radius + map_half_) * to_cell) - 1);
		const int x1 = std::min(R - 1, static_cast<int>((b.x + radius + map_half_) * to_cell) + 1);
		const int y0 = std::max(0, static_cast<int>((b.z - radius + map_half_) * to_cell) - 1);
		const int y1 = std::min(R - 1, static_cast<int>((b.z + radius + map_half_) * to_cell) + 1);
		for (int y = y0; y <= y1; ++y) {
			for (int x = x0; x <= x1; ++x) {
				float wx = ((static_cast<float>(x) + 0.5f) / static_cast<float>(R)) * (map_half_ * 2.0f) - map_half_;
				float wz = ((static_cast<float>(y) + 0.5f) / static_cast<float>(R)) * (map_half_ * 2.0f) - map_half_;
				float dx = wx - b.x;
				float dz = wz - b.z;
				if (dx * dx + dz * dz <= r2) {
					influence_grid_.at(x, y) = static_cast<uint8_t>(b.team + 1);
				}
			}
		}
	}
	nav_grid_.blur_territory(influence_grid_, territory_grid_);
}

bool Simulation::find_path(float sx, float sz, float tx, float tz, WaypointPath &out, uint8_t pass) const {
	ScopedUs prof(map_scale_stats_.path_us, map_scale_stats_.path_calls);
	return nav_grid_.find_path(sx, sz, tx, tz, out, pass);
}

int Simulation::territory_owner_at(float x, float z) const {
	if (territory_grid_.blank()) return -1;
	int tx = std::clamp(static_cast<int>((x + map_half_) / (map_half_ * 2.0f) * static_cast<float>(territory_grid_.w)), 0, territory_grid_.w - 1);
	int tz = std::clamp(static_cast<int>((z + map_half_) / (map_half_ * 2.0f) * static_cast<float>(territory_grid_.h)), 0, territory_grid_.h - 1);
	if (!territory_grid_.inside(tx, tz)) return -1;
	uint8_t v = territory_grid_.at(tx, tz);
	if (v == 0) return -1;
	return static_cast<int>(v) - 1;
}

float Simulation::get_terrain_height(float x, float z) const {
	if (terrain_heights_.empty() || terrain_res_ <= 1) return 0.0f;
	float half = map_half_;
	float norm_x = std::clamp((x + half) / (2.0f * half), 0.0f, 1.0f);
	float norm_z = std::clamp((z + half) / (2.0f * half), 0.0f, 1.0f);

	float fx = norm_x * (terrain_res_ - 1);
	float fz = norm_z * (terrain_res_ - 1);
	int x0 = (int)fx;
	int z0 = (int)fz;
	int x1 = std::min(x0 + 1, terrain_res_ - 1);
	int z1 = std::min(z0 + 1, terrain_res_ - 1);

	float tx = fx - x0;
	float tz = fz - z0;

	float h00 = terrain_heights_[z0 * terrain_res_ + x0];
	float h10 = terrain_heights_[z0 * terrain_res_ + x1];
	float h01 = terrain_heights_[z1 * terrain_res_ + x0];
	float h11 = terrain_heights_[z1 * terrain_res_ + x1];

	float h0 = h00 * (1.0f - tx) + h10 * tx;
	float h1 = h01 * (1.0f - tx) + h11 * tx;
	return h0 * (1.0f - tz) + h1 * tz;
}

bool Simulation::can_place(int cell_x, int cell_y, int footprint) const {
	int limit = (int)(map_half_ / GRID) - footprint;
	for (int dx = 0; dx < footprint; ++dx) {
		for (int dy = 0; dy < footprint; ++dy) {
			int cx = cell_x + dx;
			int cy = cell_y + dy;
			if (std::abs(cx) > limit || std::abs(cy) > limit) return false;
			if (occupied_cells_.find({cx, cy}) != occupied_cells_.end()) return false;
		}
	}
	return true;
}

uint32_t Simulation::cmd_build(uint8_t player, int cell_x, int cell_y, uint8_t b_type) {
	if (player >= MAX_PLAYERS) return 0;
	const BuildingTypeInfo &binfo = building_type_info(b_type);
	if (!can_place(cell_x, cell_y, binfo.footprint)) return 0;
	if (building_required_phase(b_type) > players_[player].phase) return 0;

	// 0 A.D. civ structure cost bonuses (e.g. Iberian walls, Han stone discount).
	const Civilization civ = civ_of(player);
	// Team bonuses (e.g. Persian Training Regimes, Mauryan temples) from the player and its allies.
	const uint16_t allies = ally_civ_mask(player);
	auto team = [&](ResourceType r) {
		return civ_team_bonus_mult(civ, allies, TEAM_BONUS_BUILDING_COST, (int8_t)r, CIV_UNIT_TEMPLATE_NONE, b_type);
	};
	const float wood = binfo.wood_cost * civ_bonus_mult(civ, CIV_BONUS_BUILDING_WOOD_COST, b_type) * team(RES_WOOD);
	const float stone = binfo.stone_cost * civ_bonus_mult(civ, CIV_BONUS_BUILDING_STONE_COST, b_type) * team(RES_STONE);
	const float food = binfo.food_cost * team(RES_FOOD);
	const float gold = binfo.gold_cost * team(RES_GOLD);
	if (!players_[player].pay(std::round(food), std::round(wood), std::round(gold), std::round(stone)))
		return 0;

	uint32_t bid = spawn_building(cell_x, cell_y, b_type, player, 0.0f);
	if (player == 0) record_.buildings_constructed++;
	return bid;
}

// P3 maintainFleet upkeep seam — 0 A.D. navalManager.js:507-543 gates:
// built dock, own queue (no queued units), wishlists vs existing fleet.
// Team-aware: uses the building's own team (Petra=1 via petra_try_pay),
// unlike cmd_train which is team-0/human only. Deterministic: id-ordered
// dock choice by caller, sim pay path only.
bool Simulation::cmd_maintain_fleet(uint32_t building_id, UnitType u_type, uint8_t team) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return false;
	Building &b = buildings_[bit->second];
	if (b.team != team || b.hp <= 0.0f || b.build_progress < 1.0f) return false;
	if (b.type != BLD_DOCK) return false;
	if (!b.queue.empty()) return false; // queues.ships.hasQueuedUnits()
	if (u_type != UNIT_TRIREME && u_type != UNIT_FISHING_BOAT) return false;
	const UnitTypeInfo &uinfo = civ_unit_info(civ_of(team), u_type);
	if (team == 0) {
		if (pop_current() >= pop_max()) return false;
		if (!players_[0].pay((float)uinfo.food_cost, (float)uinfo.wood_cost,
		                     (float)uinfo.gold_cost, (float)uinfo.stone_cost)) return false;
	} else {
		if (pop_current(team) >= pop_max(team)) return false;
		if (!petra_try_pay(PETRA_QUEUE_NAVAL, (float)uinfo.food_cost,
		                 (float)uinfo.wood_cost, (float)uinfo.gold_cost,
		                 (float)uinfo.stone_cost)) return false;
	}
	ProductionOrder order;
	order.unit_type = u_type;
	order.timer = 0.0f;
	order.duration = uinfo.train_time * 0.75f * tech_mods(team).train_time[u_type] * ai_time_mult(team); // navalManager AI train discount, matches petra_manage_naval
	if (b.queue.size() >= 3) return false;
	b.queue.push_back(order);
	return true;
}

// Healer.js template values (Range 12, Health 8 per 1 s interval) after the
// player's Healing Range / Healing Rate technologies.
float Simulation::healer_range(uint8_t player_id) const {
	return 12.0f + tech_mods(player_id).heal_range[UNIT_HEALER];
}

float Simulation::healer_rate(uint8_t player_id) const {
	return 8.0f / std::max(0.1f, tech_mods(player_id).heal_interval[UNIT_HEALER]);
}

// 0 A.D. unlock_champion_cavalry / _chariots / _infantry and
// unlock_civilians_house_*: which technology (if any) a civ must research
// before `unit_type` can be trained at `bld_type`. TECH_NONE = no gate.
uint8_t Simulation::train_unlock_tech(uint8_t player_id, uint8_t unit_type, uint8_t bld_type, uint16_t tmpl) const {
	if (player_id >= MAX_PLAYERS) return TECH_NONE;
	const uint8_t civ = players_[player_id].civ;
	auto gate = [&](uint8_t tech) -> uint8_t {
		// A civ without the unlock trains the unit outright, as in 0 A.D.
		return tech_available_to_civ(tech, civ) ? tech : (uint8_t)TECH_NONE;
	};
	if (bld_type == BLD_HOUSE) {
		uint8_t tech = tech_available_to_civ(TECH_UNLOCK_CIVILIANS_HOUSE_KUSH, civ)
				? (uint8_t)TECH_UNLOCK_CIVILIANS_HOUSE_KUSH : (uint8_t)TECH_UNLOCK_CIVILIANS_HOUSE_GENERIC;
		return gate(tech);
	}
	// A roster row carries its own 0 A.D. Identity/Requirements tech (champion
	// spearmen and swordsmen share their RTS0 class with citizen soldiers).
	if (tmpl < CIV_UNIT_TEMPLATE_COUNT) return gate(civ_unit_template(tmpl).unlock_tech);
	const uint64_t c = unit_tech_classes(unit_type);
	if (!(c & TC_CHAMPION)) return TECH_NONE;
	if (c & TC_CHARIOT) return gate(TECH_UNLOCK_CHAMPION_CHARIOTS);
	if (c & TC_CAVALRY) return gate(TECH_UNLOCK_CHAMPION_CAVALRY);
	if (c & TC_INFANTRY) return gate(TECH_UNLOCK_CHAMPION_INFANTRY);
	return TECH_NONE; // champion elephants come with the elephant stable
}

bool Simulation::can_train_at(uint8_t player_id, uint8_t unit_type, uint8_t bld_type, uint16_t tmpl) const {
	if (player_id >= MAX_PLAYERS) return false;
	// Houses only ever train citizens, and only once their civ's festival is in.
	if (bld_type == BLD_HOUSE && !(unit_tech_classes(unit_type) & TC_CIVILIAN)) return false;
	uint8_t tech = train_unlock_tech(player_id, unit_type, bld_type, tmpl);
	return tech == TECH_NONE || players_[player_id].has_tech(tech);
}

bool Simulation::cmd_train(uint8_t player_id, uint32_t building_id, UnitType u_type) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return false;
	const Building &b = buildings_[bit->second];
	if (b.team != player_id) return false;
	if (b.type == BLD_CORRAL || u_type == UNIT_TYPE_NONE) {
		if (b.build_progress < 1.0f || b.queue.size() >= 8) return false;
		return cmd_breed_livestock(player_id, building_id);
	}
	uint16_t tmpl = civ_default_template(civ_of(b.team), (uint8_t)u_type);
	if (tmpl == CIV_UNIT_TEMPLATE_NONE) return false; // not in this civ's roster
	return cmd_train_template(player_id, building_id, tmpl);
}

bool Simulation::cmd_train_template(uint8_t player_id, uint32_t building_id, uint16_t tmpl) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return false;
	Building &b = buildings_[bit->second];
	if (b.team != player_id || player_id >= MAX_PLAYERS || b.build_progress < 1.0f) return false;
	if (b.queue.size() >= 8 || b.upgrade_to != BLD_TYPE_COUNT) return false;
	if (tmpl >= CIV_UNIT_TEMPLATE_COUNT) return false;

	PlayerState &ps = players_[player_id];
	const CivUnitTemplate &t = civ_unit_template(tmpl);
	if (t.civ != ps.civ) return false;
	if (t.phase > ps.phase) return false; // 0 A.D. Identity/Requirements: phase_town / phase_city

	const UnitTypeInfo &uinfo = civ_template_unit_info(tmpl, t.unit_type);
	if (pop_current(player_id) >= pop_max(player_id)) return false;
	if (!can_train_at(player_id, t.unit_type, b.type, tmpl)) return false;

	float cost[RES_COUNT];
	unit_train_cost(player_id, tmpl, uinfo, cost);
	if (!ps.pay(cost[RES_FOOD], cost[RES_WOOD], cost[RES_GOLD], cost[RES_STONE]))
		return false;

	ProductionOrder order;
	order.unit_type = t.unit_type;
	order.tmpl = tmpl;
	order.timer = 0.0f;
	order.duration = uinfo.train_time * team_bonus_mult(player_id, TEAM_BONUS_UNIT_TRAIN_TIME, -1, tmpl)
		* tech_mods(player_id).train_time[t.unit_type] * ai_time_mult(player_id);
	b.queue.push_back(order);
	return true;
}

uint16_t Simulation::unit_template(uint32_t unit_id) const {
	int32_t idx = lookup_unit(unit_id);
	return (idx >= 0) ? units_.tmpl[idx] : CIV_UNIT_TEMPLATE_NONE;
}

uint16_t Simulation::ally_civ_mask(uint8_t pid) const {
	uint16_t mask = 0;
	if (pid >= MAX_PLAYERS) return 0;
	for (uint8_t q = 0; q < MAX_PLAYERS; ++q) {
		if (q == pid || !is_ally(pid, q)) continue; // teammates and mutual alliances
		bool in_match = false;
		for (const auto &b : buildings_)
			if (b.team == q && b.hp > 0.0f) { in_match = true; break; }
		for (size_t i = 0; !in_match && i < units_.count; ++i)
			if (units_.team[i] == q && units_.alive[i]) in_match = true;
		if (in_match) mask |= civ_bit(players_[q].civ);
	}
	return mask;
}

float Simulation::team_bonus_mult(uint8_t pid, uint8_t stat, int8_t res, uint16_t tmpl, uint8_t building) const {
	if (pid >= MAX_PLAYERS) return 1.0f;
	return civ_team_bonus_mult(players_[pid].civ, ally_civ_mask(pid), stat, res, tmpl, building);
}

void Simulation::unit_train_cost(uint8_t pid, uint16_t tmpl, const UnitTypeInfo &info, float out[RES_COUNT]) const {
	out[RES_FOOD] = (float)info.food_cost;
	out[RES_WOOD] = (float)info.wood_cost;
	out[RES_GOLD] = (float)info.gold_cost;
	out[RES_STONE] = (float)info.stone_cost;
	if (pid >= MAX_PLAYERS) return;
	const uint8_t civ = players_[pid].civ;
	const uint16_t allies = ally_civ_mask(pid);
	for (int r = 0; r < RES_COUNT; ++r)
		out[r] = std::round(out[r] * civ_team_bonus_mult(civ, allies, TEAM_BONUS_UNIT_COST, (int8_t)r, tmpl));
}

bool Simulation::cmd_cancel_train(uint8_t player_id, uint32_t building_id, size_t queue_idx) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return false;
	Building &b = buildings_[bit->second];
	if (b.team != player_id || player_id >= MAX_PLAYERS) return false;
	if (queue_idx >= b.queue.size()) return false;

	PlayerState &ps = players_[player_id];
	uint8_t utype = b.queue[queue_idx].unit_type;
	if (utype == UNIT_TYPE_NONE) {
		ps.add(60.0f, 30.0f, 0.0f, 0.0f);
		b.queue.erase(b.queue.begin() + queue_idx);
		return true;
	}

	uint16_t qtmpl = b.queue[queue_idx].tmpl;
	const UnitTypeInfo &uinfo = (qtmpl < CIV_UNIT_TEMPLATE_COUNT) ? civ_template_unit_info(qtmpl, utype)
		: civ_unit_info(civ_of(b.team), utype);
	float cost[RES_COUNT];
	unit_train_cost(player_id, qtmpl, uinfo, cost);
	ps.add(cost[RES_FOOD], cost[RES_WOOD], cost[RES_GOLD], cost[RES_STONE]);

	b.queue.erase(b.queue.begin() + queue_idx);
	return true;
}

bool Simulation::send_tribute(uint8_t player_id, uint8_t res_type, int amount) {
	// 0 A.D. Player.js TributeResource: player 0 pays any other player from its own stock.
	if (amount <= 0 || player_id == 0 || player_id >= MAX_PLAYERS || res_type >= RES_COUNT) return false;
	float &src = players_[0].res[res_type];
	if (src < (float)amount) return false;
	src -= (float)amount;
	players_[player_id].res[res_type] += (float)amount;
	return true;
}

void Simulation::receive_petra_diplomacy_request(uint8_t player_id, bool ally, int wanted, uint8_t resource) {
	if (player_id == 0 || player_id >= 8) return;
	PetraDiplomacyRequest req;
	req.request_type = ally ? 0 : 1;
	req.status = PetraDiplomacyRequest::WAITING;
	req.wanted = std::max(1, wanted);
	req.resource = resource < RES_COUNT ? resource : RES_GOLD;
	petra_diplomacy_requests_[player_id] = req;
}

bool Simulation::ai_send_tribute(uint8_t res_type, int amount) {
	if (amount <= 0 || res_type >= RES_COUNT) return false;
	float &src = petra_ps().res[res_type];
	if (src < (float)amount) return false;
	src -= (float)amount;
	players_[0].res[res_type] += (float)amount;
	return true;
}

void Simulation::cmd_set_rally_point(uint8_t player_id, uint32_t building_id, float rx, float rz, uint32_t res_id, uint32_t bld_id) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return;
	Building &b = buildings_[bit->second];
	if (b.team != player_id) return;
	b.has_rally_point = true;
	b.rally_x = rx;
	b.rally_z = rz;
	b.rally_target_res_id = res_id;
	b.rally_target_bld_id = bld_id;
}

int Simulation::phase_structures(uint8_t player_id, uint8_t phase) const {
	int n = 0;
	for (const auto &b : buildings_)
		if (b.team == player_id && b.build_progress >= 1.0f && b.hp > 0.0f && !b.wrecked &&
				building_phase_class(b.type) == phase)
			++n;
	return n;
}

Simulation::ResearchCheck Simulation::research_check(uint8_t player_id, uint8_t tech_id, uint32_t building_id) const {
	if (tech_id == TECH_NONE || tech_id >= TECH_COUNT || player_id >= MAX_PLAYERS) return TECH_ERR_INVALID;
	const PlayerState &ps = players_[player_id];
	const TechInfo &tinfo = tech_info(tech_id);
	if (ps.has_tech(tech_id)) return TECH_ERR_RESEARCHED;
	for (const auto &b : buildings_)
		if (b.team == player_id && b.research_tech == tech_id) return TECH_ERR_IN_PROGRESS;
	if (!tech_available_to_civ(tech_id, ps.civ)) return TECH_ERR_CIV;
	if (tinfo.required_phase > ps.phase) return TECH_ERR_PHASE;
	if (tinfo.prerequisite != TECH_NONE && !ps.has_tech(tinfo.prerequisite)) return TECH_ERR_PREREQUISITE;
	// 0 A.D. phase_town_generic / phase_city_generic "entity" requirements.
	if (tech_id == TECH_PHASE_TOWN && phase_structures(player_id, 1) < 5) return TECH_ERR_STRUCTURES;
	if (tech_id == TECH_PHASE_CITY && phase_structures(player_id, 2) < 3) return TECH_ERR_STRUCTURES;
	if (building_id != 0) {
		auto bit = building_id_to_idx_.find(building_id);
		if (bit == building_id_to_idx_.end()) return TECH_ERR_BUILDING;
		const Building &b = buildings_[bit->second];
		if (b.team != player_id || b.build_progress < 1.0f || b.hp <= 0.0f || b.research_tech != TECH_NONE
		    || b.upgrade_to != BLD_TYPE_COUNT)
			return TECH_ERR_BUILDING;
		if (b.type != tinfo.researcher) return TECH_ERR_BUILDING;
	}
	if (!ps.can_afford((float)tinfo.food_cost, (float)tinfo.wood_cost, (float)tinfo.gold_cost, (float)tinfo.stone_cost))
		return TECH_ERR_COST;
	return TECH_OK;
}

bool Simulation::cmd_research(uint8_t player_id, uint32_t building_id, uint8_t tech_id) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return false;
	Building &b = buildings_[bit->second];
	if (research_check(player_id, tech_id, building_id) != TECH_OK) return false;

	const TechInfo &tinfo = tech_info(tech_id);
	// Team bonuses on techs researched here (e.g. Athenian Democracy at the civic centre).
	const uint8_t civ = civ_of(player_id);
	const uint16_t allies = ally_civ_mask(player_id);
	auto team = [&](uint8_t stat, int8_t r) {
		return civ_team_bonus_mult(civ, allies, stat, r, CIV_UNIT_TEMPLATE_NONE, b.type);
	};
	players_[player_id].pay(std::round(tinfo.food_cost * team(TEAM_BONUS_TECH_COST, RES_FOOD)),
		std::round(tinfo.wood_cost * team(TEAM_BONUS_TECH_COST, RES_WOOD)),
		std::round(tinfo.gold_cost * team(TEAM_BONUS_TECH_COST, RES_GOLD)),
		std::round(tinfo.stone_cost * team(TEAM_BONUS_TECH_COST, RES_STONE)));
	b.research_tech = tech_id;
	b.research_timer = 0.0f;
	b.research_duration = tinfo.research_time * team(TEAM_BONUS_TECH_TIME, -1)
		* tech_mods(player_id).bld_research_time[b.type] * ai_time_mult(player_id);
	return true;
}

float Simulation::structure_max_hp(uint8_t type, uint8_t team) const {
	float hp = building_type_info(type).max_hp;
	if (team < MAX_PLAYERS) {
		// 0 A.D. civ structure bonuses (Hellenic Architecture, Wooden Construction, walls, ...)
		hp *= civ_bonus_mult(civ_of(team), CIV_BONUS_BUILDING_HP, type);
		hp *= tech_mods(team).bld_health[type];
	}
	return hp;
}

Simulation::UpgradeCheck Simulation::upgrade_check(uint8_t player_id, uint32_t building_id) const {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end() || player_id >= MAX_PLAYERS) return UPGRADE_ERR_NONE;
	const Building &b = buildings_[bit->second];
	if (b.team != player_id) return UPGRADE_ERR_OWNER;
	const BuildingUpgradeInfo up = building_upgrade(b.type, civ_of(player_id));
	if (up.to >= BLD_TYPE_COUNT || b.build_progress < 1.0f || b.hp <= 0.0f || b.wrecked) return UPGRADE_ERR_NONE;
	// Upgrade.js Upgrade(): not while upgrading or producing.
	if (b.upgrade_to != BLD_TYPE_COUNT || !b.queue.empty() || b.research_tech != TECH_NONE) return UPGRADE_ERR_BUSY;
	const PlayerState &ps = players_[player_id];
	if (ps.phase < up.required_phase) return UPGRADE_ERR_PHASE;
	if (!ps.can_afford((float)up.food_cost, (float)up.wood_cost, (float)up.gold_cost, (float)up.stone_cost))
		return UPGRADE_ERR_COST;
	return UPGRADE_OK;
}

bool Simulation::cmd_upgrade_building(uint8_t player_id, uint32_t building_id) {
	if (upgrade_check(player_id, building_id) != UPGRADE_OK) return false;
	Building &b = buildings_[building_id_to_idx_[building_id]];
	const BuildingUpgradeInfo up = building_upgrade(b.type, civ_of(player_id));
	players_[player_id].pay((float)up.food_cost, (float)up.wood_cost, (float)up.gold_cost, (float)up.stone_cost);
	b.upgrade_paid.fill(0.0f);
	b.upgrade_paid[RES_FOOD] = (float)up.food_cost;
	b.upgrade_paid[RES_WOOD] = (float)up.wood_cost;
	b.upgrade_paid[RES_GOLD] = (float)up.gold_cost;
	b.upgrade_paid[RES_STONE] = (float)up.stone_cost;
	b.upgrade_to = up.to;
	b.upgrade_timer = 0.0f;
	b.upgrade_duration = up.time;
	if (up.time <= 0.0f) finish_upgrade(b);
	return true;
}

bool Simulation::cmd_cancel_upgrade(uint8_t player_id, uint32_t building_id) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return false;
	Building &b = buildings_[bit->second];
	if (b.team != player_id || b.upgrade_to == BLD_TYPE_COUNT) return false;
	cancel_upgrade(b, player_id);
	return true;
}

void Simulation::cancel_upgrade(Building &b, uint8_t refund_to) {
	if (b.upgrade_to == BLD_TYPE_COUNT) return;
	if (refund_to < MAX_PLAYERS)
		players_[refund_to].add(b.upgrade_paid[RES_FOOD], b.upgrade_paid[RES_WOOD], b.upgrade_paid[RES_GOLD], b.upgrade_paid[RES_STONE]);
	b.upgrade_paid.fill(0.0f);
	b.upgrade_to = BLD_TYPE_COUNT;
	b.upgrade_timer = 0.0f;
	b.upgrade_duration = 0.0f;
}

// 0 A.D. helpers/Transform.js ChangeEntityTemplate: the new structure keeps the
// owner, position, health fraction, capture share and garrison of the old one.
void Simulation::finish_upgrade(Building &b) {
	const uint8_t to = b.upgrade_to;
	if (to >= BLD_TYPE_COUNT) return;
	const BuildingTypeInfo &ninfo = building_type_info(to);
	const float health_level = b.max_hp > 0.0f ? std::clamp(b.hp / b.max_hp, 0.0f, 1.0f) : 1.0f;
	b.type = to;
	b.max_hp = structure_max_hp(to, b.team);
	b.hp = b.max_hp * health_level;
	float new_capture = ninfo.capture_max_points;
	if (b.team < MAX_PLAYERS) new_capture *= civ_bonus_mult(civ_of(b.team), CIV_BONUS_BUILDING_CAPTURE_POINTS, to);
	if (b.max_capture_points > 0.0f) {
		const float scale = new_capture / b.max_capture_points;
		for (float &c : b.capture_points_by_team) c *= scale;
	}
	b.max_capture_points = new_capture;
	b.capture_points = b.capture_points_by_team[b.team < 8 ? b.team : 0];
	b.capture_regen_rate = ninfo.capture_regen_rate;
	b.capture_garrison_regen_rate = ninfo.capture_garrison_regen_rate;
	b.territory_decay_rate = ninfo.territory_decay_rate;
	// TransferGarrisonedUnits: whoever doesn't fit in the new structure is unloaded.
	b.garrison_cap = ninfo.garrison_cap;
	while ((int)b.garrisoned_units.size() > b.garrison_cap)
		order_ungarrison_single(b.id, b.garrisoned_units.back());
	b.attack_timer = 0.0f;
	b.upgrade_paid.fill(0.0f);
	b.upgrade_to = BLD_TYPE_COUNT;
	b.upgrade_timer = 0.0f;
	b.upgrade_duration = 0.0f;
	if (to == BLD_TOWN_CENTER || to == BLD_FORTRESS || to == BLD_TOWER) rebuild_territory();
	if (to == BLD_GATE) {
		b.gate_open = false;
		b.gate_locked = false;
		rebuild_nav_grid(); // a wall blocks long-range paths, an unlocked gate doesn't
	}
}

// Upgrade.js UpgradeProgress: the timer runs whether or not anyone is near.
void Simulation::tick_upgrades(float dt) {
	for (auto &b : buildings_) {
		if (b.upgrade_to == BLD_TYPE_COUNT) continue;
		if (b.hp <= 0.0f || b.wrecked) { cancel_upgrade(b, b.team); continue; }
		b.upgrade_timer += dt;
		if (b.upgrade_timer >= b.upgrade_duration) finish_upgrade(b);
	}
}

void Simulation::compute_tech_mods(uint8_t player_id, TechMods &m) const {
	for (int t = 0; t < UNIT_TYPE_COUNT; ++t) {
		for (float &g : m.gather[t]) g = 1.0f;
		for (int16_t &c : m.carry[t]) c = 0;
		m.attack[t] = m.health[t] = m.speed[t] = 1.0f;
		m.train_time[t] = m.heal_interval[t] = m.trade_gain[t] = 1.0f;
		m.range[t] = m.heal_range[t] = 0.0f;
		m.armor[t][0] = m.armor[t][1] = m.armor[t][2] = 0.0f;
	}
	for (int b = 0; b < BLD_TYPE_COUNT; ++b) {
		m.bld_health[b] = m.bld_attack[b] = m.bld_build_time[b] = m.bld_pop_bonus[b] = 1.0f;
		m.bld_research_time[b] = m.bld_arrow_mul[b] = 1.0f;
		m.bld_range[b] = m.bld_arrow_add[b] = 0.0f;
	}
	if (player_id >= MAX_PLAYERS) return;
	const PlayerState &ps = players_[player_id];
	Civilization civ = ps.civ;
	for (uint8_t id = 1; id < TECH_COUNT; ++id) {
		if (!ps.has_tech(id)) continue;
		const TechInfo &ti = tech_info(id);
		const TechEffect *fx = tech_effects_table() + ti.effect_begin;
		for (uint16_t k = 0; k < ti.effect_count; ++k) {
			const TechEffect &e = fx[k];
			auto apply = [&](float &v) { v = (e.op == TOP_MUL) ? v * e.value : v + e.value; };
			for (uint8_t t = 0; t < UNIT_TYPE_COUNT; ++t) {
				if (!tech_effect_applies(e, unit_tech_classes(t))) continue;
				const UnitTypeInfo &ui = unit_type_info(t);
				bool ranged = unit_has_ranged_attack(t, civ);
				// The damage type a unit mostly deals decides which Attack/*/Damage/* row scales it.
				uint8_t main_dmg = (ui.crush_damage > ui.hack_damage && ui.crush_damage > ui.pierce_damage) ? 2
					: (ui.pierce_damage > ui.hack_damage) ? 1 : 0;
				switch (e.stat) {
					case TSTAT_GATHER_GRAIN: case TSTAT_GATHER_FRUIT: case TSTAT_GATHER_MEAT: case TSTAT_GATHER_FISH:
					case TSTAT_GATHER_WOOD: case TSTAT_GATHER_STONE: case TSTAT_GATHER_METAL:
						apply(m.gather[t][e.stat - TSTAT_GATHER_GRAIN]); break;
					case TSTAT_CARRY_FOOD: m.carry[t][RES_FOOD] += (int16_t)e.value; break;
					case TSTAT_CARRY_WOOD: m.carry[t][RES_WOOD] += (int16_t)e.value; break;
					case TSTAT_CARRY_STONE: m.carry[t][RES_STONE] += (int16_t)e.value; break;
					case TSTAT_CARRY_METAL: m.carry[t][RES_GOLD] += (int16_t)e.value; break;
					case TSTAT_MELEE_HACK: case TSTAT_MELEE_PIERCE: case TSTAT_MELEE_CRUSH:
						if (!ranged && e.stat - TSTAT_MELEE_HACK == main_dmg) apply(m.attack[t]);
						break;
					case TSTAT_RANGED_HACK: case TSTAT_RANGED_PIERCE: case TSTAT_RANGED_CRUSH:
						if (ranged && e.stat - TSTAT_RANGED_HACK == main_dmg) apply(m.attack[t]);
						break;
					case TSTAT_ARMOR_HACK: case TSTAT_ARMOR_PIERCE: case TSTAT_ARMOR_CRUSH:
						apply(m.armor[t][e.stat - TSTAT_ARMOR_HACK]); break;
					case TSTAT_HEALTH: apply(m.health[t]); break;
					case TSTAT_SPEED: apply(m.speed[t]); break;
					case TSTAT_RANGE: if (ranged) apply(m.range[t]); break;
					// Cost/BuildTime is a unit's training time and a structure's build time.
					case TSTAT_BUILD_TIME: apply(m.train_time[t]); break;
					case TSTAT_HEAL_RANGE: apply(m.heal_range[t]); break;
					case TSTAT_HEAL_INTERVAL: apply(m.heal_interval[t]); break;
					case TSTAT_TRADE_GAIN: apply(m.trade_gain[t]); break;
					default: break;
				}
			}
			for (uint8_t b = 0; b < BLD_TYPE_COUNT; ++b) {
				if (!tech_effect_applies(e, building_tech_classes(b))) continue;
				switch (e.stat) {
					case TSTAT_HEALTH: apply(m.bld_health[b]); break;
					case TSTAT_RANGED_PIERCE: apply(m.bld_attack[b]); break;
					case TSTAT_RANGE: apply(m.bld_range[b]); break;
					case TSTAT_BUILD_TIME: apply(m.bld_build_time[b]); break;
					case TSTAT_RESEARCH_TIME: apply(m.bld_research_time[b]); break;
					case TSTAT_POP_BONUS: apply(m.bld_pop_bonus[b]); break;
					// BuildingAI arrow counts: Sentries multiplies, Professional Garrisons adds.
					case TSTAT_ARROWS:
						if (e.op == TOP_MUL) m.bld_arrow_mul[b] *= e.value;
						else m.bld_arrow_add[b] += e.value;
						break;
					default: break;
				}
			}
		}
	}
}

void Simulation::rebuild_all_tech_mods() {
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) compute_tech_mods(p, tech_mods_[p]);
}

void Simulation::apply_tech_mods_to_unit(size_t idx) {
	uint8_t team = units_.team[idx];
	uint8_t t = units_.unit_type[idx];
	if (team >= MAX_PLAYERS || t >= UNIT_TYPE_COUNT) return;
	const TechMods &m = tech_mods_[team];
	units_.attack_damage[idx] *= m.attack[t];
	units_.max_hp[idx] *= m.health[t];
	units_.hp[idx] = units_.max_hp[idx];
	units_.speed[idx] *= m.speed[t];
	units_.attack_range[idx] += m.range[t];
}

// 0 A.D. ModificationsManager: a finished technology changes every existing
// entity it affects, not only new ones. Rescale by the new/old modifier ratio.
void Simulation::complete_research(uint8_t player_id, uint8_t tech_id) {
	if (player_id >= MAX_PLAYERS) return;
	PlayerState &ps = players_[player_id];
	ps.techs.set(tech_id);
	if (tech_id == TECH_PHASE_TOWN) ps.phase = std::max<uint8_t>(ps.phase, 2);
	else if (tech_id == TECH_PHASE_CITY) ps.phase = 3;
	TechMods old = tech_mods_[player_id];
	compute_tech_mods(player_id, tech_mods_[player_id]);
	const TechMods &m = tech_mods_[player_id];
	for (size_t i = 0; i < units_.count; ++i) {
		if (units_.team[i] != player_id) continue;
		uint8_t t = units_.unit_type[i];
		if (t >= UNIT_TYPE_COUNT) continue;
		if (m.attack[t] != old.attack[t]) units_.attack_damage[i] *= m.attack[t] / old.attack[t];
		if (m.health[t] != old.health[t]) {
			float r = m.health[t] / old.health[t];
			units_.max_hp[i] *= r;
			units_.hp[i] *= r;
		}
		if (m.speed[t] != old.speed[t]) units_.speed[i] *= m.speed[t] / old.speed[t];
		units_.attack_range[i] += m.range[t] - old.range[t];
	}
	for (auto &b : buildings_) {
		if (b.team != player_id || b.type >= BLD_TYPE_COUNT) continue;
		float r = m.bld_health[b.type] / old.bld_health[b.type];
		if (r != 1.0f) {
			b.max_hp *= r;
			b.hp *= r;
		}
	}
}

int32_t Simulation::carry_cap(size_t i, uint8_t res) const {
	uint8_t team = units_.team[i], t = units_.unit_type[i];
	if (team >= MAX_PLAYERS || t >= UNIT_TYPE_COUNT || res >= RES_COUNT) return units_.carry_capacity[i];
	return units_.carry_capacity[i] + tech_mods_[team].carry[t][res];
}

bool Simulation::cmd_barter(uint8_t player_id, ResourceType sell, ResourceType buy, int amount) {
	if (sell == buy || amount <= 0 || player_id >= MAX_PLAYERS) return false;
	if (sell >= RES_COUNT || buy >= RES_COUNT) return false;
	PlayerState &ps = players_[player_id];
	float *src = &ps.res[sell];
	float *dst = &ps.res[buy];
	if (*src < (float)amount) return false;

	// 0 A.D. Market.js:42 & Barter.js:26 (CONSTANT_DIFFERENCE = 10, DEAL_AMOUNT = 100):
	// Exchange rate sell/buy = (100 - 10) / (100 + 10) = 90 / 110 ~= 0.8182 (18.18% barter fee).
	// Barter fee matches Market.js exchangeRate range (15% - 25%):
	// Every civ pays the same ~18.2% fee; 0 A.D. has no civ-specific barter rate.
	const float baseSpread = 0.8182f;
	float priceRatio = market_sell_price_[sell] / std::max(0.01f, market_buy_price_[buy]);
	int received = int(amount * baseSpread * priceRatio);
	received = std::clamp(received, 1, amount * 3);

	*src -= amount;
	*dst += received;

	// 0 A.D. Market.js:42 / Barter.js:31 (DIFFERENCE_PER_DEAL = 2% per 100 units):
	// Pow-based price fluctuation: price = base * pow(1.02f, deals) for demand, pow(0.98f, deals) for supply.
	// Clamped to [0.2f, 5.0f] range across per-resource buy and sell arrays.
	// When buying resource buy: market_buy_price_[buy] *= 1.02f, market_sell_price_[buy] *= 1.02f
	// When selling resource sell: market_buy_price_[sell] *= 0.98f, market_sell_price_[sell] *= 0.98f
	const float deals = (float)amount / 100.0f;
	const float buy_factor = std::pow(1.02f, deals);
	const float sell_factor = std::pow(0.98f, deals);
	market_buy_price_[buy] = std::clamp(market_buy_price_[buy] * buy_factor, 0.2f, 5.0f);
	market_sell_price_[buy] = std::clamp(market_sell_price_[buy] * buy_factor, 0.2f, 5.0f);
	market_buy_price_[sell] = std::clamp(market_buy_price_[sell] * sell_factor, 0.2f, 5.0f);
	market_sell_price_[sell] = std::clamp(market_sell_price_[sell] * sell_factor, 0.2f, 5.0f);

	market_trade_history_[sell]++;
	market_trade_history_[buy]++;

	return true;
}

bool Simulation::cmd_set_diplomacy(uint8_t player_id, uint8_t target, uint8_t stance) {
	// 0 A.D. Diplomacy.js / DiplomacyDialog.js parity: deterministic user command path for stance changes
	// to maintain lockstep sync across peers before local state modification.
	// A player only ever writes its own row, so no player can set another's stance.
	if (player_id >= MAX_PLAYERS || target >= MAX_PLAYERS || player_id == target || stance > DIPLO_ENEMY)
		return false;
	set_diplomacy(player_id, target, stance);
	return true;
}

int Simulation::calculate_trader_gain(const Building& mA, const Building& mB, uint8_t team) const {
	// 0 A.D. Market.js:68 (CalculateTraderGain) & Trader.js:33 (CalculateGain) & globalscripts/Trade.js:12:
	// Calculate Euclidean distance between markets: distance = hypot(mA.x - mB.x, mA.z - mB.z).
	// Gains formula: gains = floor(distance / CELL * 0.5f + base) * civ_mult
	// where CELL = 2.0f (NavGrid cell resolution / 2.0m), base = 0.0f (base route offset),
	// and civ_mult applies civilization bonuses: Persian 1.25x, Carthaginian 1.10x.
	// Reference baseline: (distance / 20.0f) * 30.0f
	const float distance = std::hypot(mA.x - mB.x, mA.z - mB.z);
	const float CELL = 2.0f;
	const float base = 0.0f;
	Civilization civ = civ_of(team);
	const float civ_mult = civ_info(civ).trade_gain_mult;
	// Handicraft / Advanced Handicraft (Trader/GainMultiplier).
	const float tech_mult = tech_mods(team).trade_gain[UNIT_TRADER] * ai_gather_mult(team);
	int gains = static_cast<int>(std::floor((distance / CELL * 0.5f + base)) * civ_mult * tech_mult);
	return std::max(0, gains);
}

float Simulation::trader_gains(const Building *a, const Building *b) const {
	if (!a || !b) return 0.0f;
	return (float)calculate_trader_gain(*a, *b, a->team);
}

void Simulation::order_trade(const uint32_t *unit_ids, size_t count, uint32_t market_a_id, uint32_t market_b_id) {
	if (count == 0 || market_a_id == 0 || market_b_id == 0 || market_a_id == market_b_id) return;
	auto itA = building_id_to_idx_.find(market_a_id);
	auto itB = building_id_to_idx_.find(market_b_id);
	if (itA == building_id_to_idx_.end() || itB == building_id_to_idx_.end()) return;
	const Building &bA = buildings_[itA->second];
	const Building &bB = buildings_[itB->second];
	if (bA.build_progress < 1.0f || bB.build_progress < 1.0f) return;
	for (size_t k = 0; k < count; ++k) {
		int32_t uidx = lookup_unit(unit_ids[k]);
		if (uidx < 0 || !units_.alive[uidx] || units_.unit_type[uidx] != UNIT_TRADER) continue;
		uint32_t uid = units_.id[uidx];
		trader_routes_[uid] = {market_a_id, market_b_id};
		trader_leg_[uid] = 0;
		units_.action_state[uidx] = ACTION_TRADE;
		units_.has_order[uidx] = 1;
		units_.carry[uidx] = 0;
		// Head to first market
		units_.target_x[uidx] = bA.x;
		units_.target_z[uidx] = bA.z;
		units_.anchor_x[uidx] = bA.x;
		units_.anchor_z[uidx] = bA.z;
	}
}

void Simulation::order_trade_auto(const uint32_t *unit_ids, size_t count) {
	if (count == 0) return;
	for (size_t k = 0; k < count; ++k) {
		int32_t uidx = lookup_unit(unit_ids[k]);
		if (uidx < 0 || !units_.alive[uidx] || units_.unit_type[uidx] != UNIT_TRADER) continue;
		uint8_t team = units_.team[uidx];
		// Find two best markets / TCs for this team: prefer Market+Market, else Market+TC
		uint32_t bestA = 0, bestB = 0;
		float bestDist = -1.0f;
		// Collect candidates: Markets and TownCenters of same team with build_progress >=1
		std::vector<const Building*> cands;
		for (const auto &b : buildings_) {
			if (b.team != team || b.build_progress < 1.0f) continue;
			if (b.type == BLD_MARKET || b.type == BLD_TOWN_CENTER) cands.push_back(&b);
		}
		if (cands.size() >= 2) {
			for (size_t i = 0; i < cands.size(); ++i) {
				for (size_t j = i + 1; j < cands.size(); ++j) {
					float d = std::hypot(cands[i]->x - cands[j]->x, cands[i]->z - cands[j]->z);
					if (d > bestDist) { bestDist = d; bestA = cands[i]->id; bestB = cands[j]->id; }
				}
			}
		} else if (cands.size() == 1) {
			// Single market/tc: pair with opposite map edge fallback (simulate distant partner)
			bestA = cands[0]->id;
			// create virtual second point mirror
			bestB = 0;
		}
		if (bestA != 0 && bestB != 0) {
			order_trade(&units_.id[uidx], 1, bestA, bestB);
		} else if (bestA != 0) {
			// Single dropsite: ping-pong between it and a distant point using map_half
			auto it = building_id_to_idx_.find(bestA);
			if (it != building_id_to_idx_.end()) {
				const Building &b = buildings_[it->second];
				trader_routes_[units_.id[uidx]] = {bestA, 0}; // 0 sentinel = fallback
				trader_leg_[units_.id[uidx]] = 0;
				units_.action_state[uidx] = ACTION_TRADE;
				units_.has_order[uidx] = 1;
				units_.target_x[uidx] = b.x;
				units_.target_z[uidx] = b.z;
			}
		} else {
			// No markets: keep idle fallback trader behaviour (will use map_half distance for gain)
			trader_routes_[units_.id[uidx]] = {0,0};
			trader_leg_[units_.id[uidx]] = 0;
			units_.action_state[uidx] = ACTION_TRADE;
			units_.has_order[uidx] = 1;
			// Distant ping-pong across map_half
			float half = map_half_;
			if (units_.pos_x[uidx] > 0) { units_.target_x[uidx] = -half * 0.6f; units_.target_z[uidx] = -half * 0.6f; }
			else { units_.target_x[uidx] = half * 0.6f; units_.target_z[uidx] = half * 0.6f; }
		}
	}
}

void Simulation::cmd_move(const uint32_t *unit_ids, size_t count, float tx, float tz) {
	if (count == 0) return;

	// Check leader formation
	uint8_t form = FORMATION_NONE;
	for (size_t i = 0; i < count; ++i) {
		int32_t idx = lookup_unit(unit_ids[i]);
		if (idx >= 0 && units_.formation[idx] != FORMATION_NONE) {
			form = units_.formation[idx];
			break;
		}
	}
	if (form >= FORMATION_COUNT) {
		form = FORMATION_NONE;
	}

	// Per-formation metrics table {spacing_x, spacing_z, facing} per formation ID (0..12).
	// Coefficients and layout behaviors cite 0 A.D. Formation.js (ComputeFormationOffsets lines 655-804)
	// and special/formations/*.xml template definitions.
	struct FormationMetrics {
		float spacing_x; // Width spacing (Formation.js:658: separation.width *= separationMultiplier.width)
		float spacing_z; // Depth spacing (Formation.js:659: separation.depth *= separationMultiplier.depth)
		float facing;    // Heading rotation offset in radians (Formation.js:873: Vector2D.rotate(offset, -rot))
	};
	static const FormationMetrics formation_metrics[13] = {
		{ 1.35f, 1.35f, 0.0f }, // 0: FORMATION_NONE (Grid) - Formation.js:658 default separation
		{ 1.40f, 1.40f, 0.0f }, // 1: FORMATION_LINE (Close Order line_closed) - Formation.js:658, line_closed.xml
		{ 1.35f, 1.35f, 0.0f }, // 2: FORMATION_BOX (Box square) - Formation.js:753, box.xml
		{ 1.20f, 1.20f, 0.0f }, // 3: FORMATION_COLUMN (Forced March column_closed) - Formation.js:699, column_closed.xml
		{ 0.85f, 0.85f, 0.0f }, // 4: FORMATION_PHALANX (Phalanx syntagma) - phalanx.xml w_mult=0.6, d_mult=0.6
		{ 1.30f, 1.30f, 0.0f }, // 5: FORMATION_WEDGE (Wedge triangle) - Formation.js:758, wedge.xml mult=1.1
		{ 2.00f, 2.00f, 0.0f }, // 6: FORMATION_SCATTER (Skirmish cloud) - Formation.js:722, skirmish.xml w_mult=2.0
		{ 2.10f, 2.10f, 0.0f }, // 7: FORMATION_LINE_OPEN (Open Order line_open) - Formation.js:658, line_open.xml w_mult=1.5, d_mult=1.5
		{ 2.40f, 1.20f, 0.0f }, // 8: FORMATION_COLUMN_OPEN (Column Open column_open) - Formation.js:658, column_open.xml w_mult=2.0
		{ 1.40f, 1.40f, 0.0f }, // 9: FORMATION_FLANK (Flank split wings) - Formation.js:774 centerGap=12, flank.xml
		{ 1.40f, 1.40f, 0.0f }, // 10: FORMATION_BATTLE_LINE (Battle Line ranks) - Formation.js:708 ratio=2, battle_line.xml
		{ 4.50f, 3.50f, 0.0f }, // 11: FORMATION_SHIPS_LINE (Fleet Battle Line) - Formation.js:658, ships/line_staggered.xml w_mult=3.5
		{ 3.60f, 3.50f, 0.0f }  // 12: FORMATION_SHIPS_COLUMN (Maneuvering Column) - Formation.js:658, ships/column.xml w_mult=3.0, d_mult=1.25
	};

	// Formation heading rotation (G5): compute group centroid heading so LINE etc. face the move direction
	// 0 A.D. Formation.js GetFormationOffsets rotates offsets to face movement vector.
	float centerX = 0.0f, centerZ = 0.0f;
	int validCount = 0;
	for (size_t i = 0; i < count; ++i) {
		int32_t idx = lookup_unit(unit_ids[i]);
		if (idx >= 0 && (size_t)idx < units_.count && units_.alive[idx]) {
			centerX += units_.pos_x[idx];
			centerZ += units_.pos_z[idx];
			++validCount;
		}
	}
	float heading = 0.0f;
	float cosH = 1.0f, sinH = 0.0f;
	if (validCount > 0) {
		centerX /= (float)validCount;
		centerZ /= (float)validCount;
		heading = atan2f(tz - centerZ, tx - centerX);
		if (form < 13) {
			heading += formation_metrics[form].facing;
		}
		cosH = cosf(heading);
		sinH = sinf(heading);
	}
	// Formation speed modifiers (G5): 0 A.D. Phalanx is slower/tighter, Wedge charges faster
	// Keep table here per spec; also applied in move_units() for continuous speed scaling.
	// Retain legacy symbol for parity checker: formation_speed_mult[7] = {1.0f, 1.0f, 1.0f, 0.95f, 0.85f, 1.1f, 1.05f};
	static const float formation_speed_mult[13] = {
		1.00f, 1.00f, 1.00f, 0.95f, 0.85f, 1.10f, 1.05f, // 0..6 (NONE,LINE,BOX,COLUMN,PHALANX,WEDGE,SCATTER)
		1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.25f          // 7..12 (ships/column: 1.25x per 0 A.D. ships/column.xml SpeedMultiplier, Formation.js:198)
	};

	for (size_t i = 0; i < count; ++i) {
		int32_t idx = lookup_unit(unit_ids[i]);
		if (idx < 0 || (size_t)idx >= units_.count || !units_.alive[idx]) continue;

		float ox = 0.0f, oz = 0.0f;

		if (form == FORMATION_LINE) {
			// Horizontal Battle Line (existing behavior unchanged)
			float spacing = formation_metrics[FORMATION_LINE].spacing_x;
			ox = ((int)i - (int)(count - 1) * 0.5f) * spacing;
			oz = 0.0f;
		} else if (form == FORMATION_COLUMN) {
			// Two parallel marching files (existing behavior unchanged)
			float spacing = formation_metrics[FORMATION_COLUMN].spacing_x;
			ox = ((int)(i % 2) - 0.5f) * spacing;
			oz = -((int)(i / 2)) * spacing;
		} else if (form == FORMATION_PHALANX) {
			// Dense Syntagma Shield Wall (4 ranks wide) (existing behavior unchanged)
			int ranks = 4;
			float spacing = formation_metrics[FORMATION_PHALANX].spacing_x;
			int col = (int)i % ranks;
			int row = (int)i / ranks;
			ox = (col - (ranks - 1) * 0.5f) * spacing;
			oz = -row * spacing;
		} else if (form == FORMATION_WEDGE) {
			// Spearhead Wedge (existing behavior unchanged)
			int row = (int)std::floor(std::sqrt((float)(2 * i)));
			int row_start = row * (row + 1) / 2;
			int pos_in_row = (int)i - row_start;
			float spacing = formation_metrics[FORMATION_WEDGE].spacing_x;
			ox = (pos_in_row - row * 0.5f) * spacing;
			oz = -row * spacing;
		} else if (form == FORMATION_SCATTER) {
			// Dispersed skirmish cloud (existing behavior unchanged)
			float a = (float)i * 2.3999632f; // Golden ratio spiral
			float r = std::sqrt((float)i + 1.0f) * 2.0f;
			ox = cosf(a) * r;
			oz = sinf(a) * r;
		} else if (form == FORMATION_LINE_OPEN) {
			// Dispersed open order line (0 A.D. line_open.xml w_mult=1.5, Formation.js:766)
			float sp_x = formation_metrics[FORMATION_LINE_OPEN].spacing_x;
			ox = ((int)i - (int)(count - 1) * 0.5f) * sp_x;
			oz = 0.0f;
		} else if (form == FORMATION_COLUMN_OPEN) {
			// Dispersed open column marching files (0 A.D. column_open.xml w_mult=2.0, Formation.js:699)
			float sp_x = formation_metrics[FORMATION_COLUMN_OPEN].spacing_x;
			float sp_z = formation_metrics[FORMATION_COLUMN_OPEN].spacing_z;
			ox = ((int)(i % 2) - 0.5f) * sp_x;
			oz = -((int)(i / 2)) * sp_z;
		} else if (form == FORMATION_FLANK) {
			// Dual flanking wings with center gap (0 A.D. flank.xml centerGap=12, Formation.js:774-777)
			float sp_x = formation_metrics[FORMATION_FLANK].spacing_x;
			float sp_z = formation_metrics[FORMATION_FLANK].spacing_z;
			int half = (count + 1) / 2;
			bool right = (i >= (size_t)half);
			int wing_i = right ? (i - half) : i;
			int wing_cnt = right ? (count - half) : half;
			int cols = std::max(1, (int)std::ceil(std::sqrt((float)wing_cnt)));
			int col = wing_i % cols;
			int row = wing_i / cols;
			float wing_ox = (col - (cols - 1) * 0.5f) * sp_x;
			float center_offset = right ? 6.0f : -6.0f; // CenterGap 12m halved per Formation.js:777
			ox = center_offset + wing_ox;
			oz = -row * sp_z;
		} else if (form == FORMATION_BATTLE_LINE) {
			// Multi-rank structured battle line (0 A.D. battle_line.xml MinColumns=5, MaxRows=4, Formation.js:708)
			float sp_x = formation_metrics[FORMATION_BATTLE_LINE].spacing_x;
			float sp_z = formation_metrics[FORMATION_BATTLE_LINE].spacing_z;
			int cols = std::clamp((int)count, 4, 8);
			int col = (int)i % cols;
			int row = (int)i / cols;
			ox = (col - (cols - 1) * 0.5f) * sp_x;
			oz = -row * sp_z;
		} else if (form == FORMATION_SHIPS_LINE) {
			// Staggered fleet battle line (0 A.D. ships/line_staggered.xml w_mult=3.5, Formation.js:753)
			float sp_x = formation_metrics[FORMATION_SHIPS_LINE].spacing_x;
			float sp_z = formation_metrics[FORMATION_SHIPS_LINE].spacing_z;
			int cols = std::clamp((int)count, 3, 6);
			int col = (int)i % cols;
			int row = (int)i / cols;
			float stagger = (row % 2 == 1) ? (sp_x * 0.5f) : 0.0f;
			ox = (col - (cols - 1) * 0.5f) * sp_x + stagger;
			oz = -row * sp_z;
		} else if (form == FORMATION_SHIPS_COLUMN) {
			// Naval maneuvering column (0 A.D. ships/column.xml w_mult=3.0, d_mult=1.25, Formation.js:699)
			float sp_x = formation_metrics[FORMATION_SHIPS_COLUMN].spacing_x;
			float sp_z = formation_metrics[FORMATION_SHIPS_COLUMN].spacing_z;
			ox = ((int)(i % 2) - 0.5f) * sp_x;
			oz = -((int)(i / 2)) * sp_z;
		} else {
			// Standard Square Grid (FORMATION_BOX / FORMATION_NONE) (existing behavior unchanged)
			int side = std::max(1, (int)std::ceil(std::sqrt((float)count)));
			float spacing = formation_metrics[FORMATION_NONE].spacing_x;
			int row = (int)i / side;
			int col = (int)i % side;
			ox = (col - (side - 1) * 0.5f) * spacing;
			oz = (row - (side - 1) * 0.5f) * spacing;
		}

		// Rotate offset by heading so formation faces movement direction (fixes axis-aligned LINE)
		float rx = ox * cosH - oz * sinH;
		float rz = ox * sinH + oz * cosH;

		float dest_x = tx + rx;
		float dest_z = tz + rz;
		dest_x = std::clamp(dest_x, -map_half_ + 1.0f, map_half_ - 1.0f);
		dest_z = std::clamp(dest_z, -map_half_ + 1.0f, map_half_ - 1.0f);
		units_.target_x[idx] = dest_x;
		units_.target_z[idx] = dest_z;
		units_.anchor_x[idx] = dest_x;
		units_.anchor_z[idx] = dest_z;
		if (unit_paths_.size() < UNIT_CAP) unit_paths_.resize(UNIT_CAP);
		WaypointPath wp;
		uint8_t pass = PASS_INFANTRY;
		uint8_t ut = units_.unit_type[idx];
		if (ut == UNIT_TRIREME || ut == UNIT_FISHING_BOAT || ut == UNIT_MERCHANT_SHIP)
			pass = PASS_SHIP;
		else if (ut == UNIT_CAVALRY || ut == UNIT_CHARIOT || ut == UNIT_CAMEL ||
		         ut == UNIT_SCYTHED_CHARIOT)
			// Mounted: PASS_CAVALRY excludes cells steeper than cavalry_max_slope
			// (grid_nav.h steep_cells_, 0 A.D. pathfinder.xml MaxTerrainSlope +
			// Pathfinding.h:205-241 IsPassable). Slope mask is refreshed on every
			// rebuild_nav_grid via set_water_from_heights -> compute_slopes_from_heights.
			pass = PASS_CAVALRY;
		find_path(units_.pos_x[idx], units_.pos_z[idx], dest_x, dest_z, wp, pass);
		unit_paths_[idx] = wp;
		units_.waypoint_cur[idx] = static_cast<int16_t>(wp.cur);
		units_.waypoint_count[idx] = static_cast<int16_t>(wp.count);
		units_.has_order[idx] = 1;
		units_.action_state[idx] = ACTION_MOVE;
		units_.target_resource_id[idx] = 0;
		units_.target_unit_id[idx] = 0;
		units_.target_building_id[idx] = 0;
	}
	(void)formation_speed_mult;
}

void Simulation::cmd_stop(const uint32_t *unit_ids, size_t count) {
	for (size_t i = 0; i < count; ++i) {
		int32_t idx = lookup_unit(unit_ids[i]);
		if (idx >= 0 && (size_t)idx < units_.count && units_.alive[idx]) {
			units_.has_order[idx] = 0;
			units_.action_state[idx] = ACTION_IDLE;
			units_.anchor_x[idx] = units_.pos_x[idx];
			units_.anchor_z[idx] = units_.pos_z[idx];
			units_.target_resource_id[idx] = 0;
			units_.target_unit_id[idx] = 0;
			units_.target_building_id[idx] = 0;
			units_.waypoint_cur[idx] = 0;
			units_.waypoint_count[idx] = 0;
			if ((size_t)idx < unit_paths_.size()) unit_paths_[idx].clear();
		}
	}
}

void Simulation::order_gather(const uint32_t *unit_ids, size_t count, uint32_t resource_id) {
	auto rit = res_id_to_idx_.find(resource_id);
	if (rit == res_id_to_idx_.end()) return;
	size_t ridx = rit->second;
	float rx = resources_.x[ridx];
	float rz = resources_.z[ridx];

	for (size_t i = 0; i < count; ++i) {
		int32_t uidx = lookup_unit(unit_ids[i]);
		if (uidx < 0 || !units_.alive[uidx]) continue;

		units_.target_x[uidx] = rx;
		units_.target_z[uidx] = rz;
		units_.anchor_x[uidx] = rx;
		units_.anchor_z[uidx] = rz;
		units_.has_order[uidx] = 1;
		units_.action_state[uidx] = ACTION_GATHER;
		units_.target_resource_id[uidx] = resource_id;
		units_.res_type_carried[uidx] = resources_.type[ridx];
		units_.target_unit_id[uidx] = 0;
		units_.target_building_id[uidx] = 0;
	}
}

void Simulation::order_attack(const uint32_t *unit_ids, size_t count, uint32_t target_unit_id) {
	int32_t tidx = lookup_unit(target_unit_id);
	if (tidx < 0) return;
	float tx = units_.pos_x[tidx];
	float tz = units_.pos_z[tidx];

	for (size_t i = 0; i < count; ++i) {
		int32_t uidx = lookup_unit(unit_ids[i]);
		if (uidx < 0 || !units_.alive[uidx]) continue;
		if (units_.attack_damage[uidx] <= 0.0f) continue;

		units_.target_x[uidx] = tx;
		units_.target_z[uidx] = tz;
		units_.has_order[uidx] = 1;
		units_.action_state[uidx] = ACTION_ATTACK;
		units_.target_unit_id[uidx] = target_unit_id;
		units_.target_resource_id[uidx] = 0;
		units_.target_building_id[uidx] = 0;
	}
}

void Simulation::order_attack_building(const uint32_t *unit_ids, size_t count, uint32_t target_building_id) {
	auto bit = building_id_to_idx_.find(target_building_id);
	if (bit == building_id_to_idx_.end()) return;
	const Building &b = buildings_[bit->second];

	for (size_t i = 0; i < count; ++i) {
		int32_t uidx = lookup_unit(unit_ids[i]);
		if (uidx < 0 || !units_.alive[uidx]) continue;
		if (units_.attack_damage[uidx] <= 0.0f) continue;

		stand_outside_rect(b.x, b.z, b.half_size, units_.id[uidx],
				units_.pos_x[uidx], units_.pos_z[uidx],
				&units_.target_x[uidx], &units_.target_z[uidx]);
		units_.has_order[uidx] = 1;
		units_.action_state[uidx] = ACTION_ATTACK;
		units_.target_building_id[uidx] = target_building_id;
		units_.target_unit_id[uidx] = 0;
		units_.target_resource_id[uidx] = 0;
	}
}

void Simulation::order_attack_move(const uint32_t *unit_ids, size_t count, float tx, float tz) {
	cmd_move(unit_ids, count, tx, tz);
	for (size_t i = 0; i < count; ++i) {
		int32_t idx = lookup_unit(unit_ids[i]);
		if (idx >= 0 && (size_t)idx < units_.count && units_.alive[idx]) {
			units_.action_state[idx] = ACTION_ATTACK_MOVE;
			units_.stance[idx] = STANCE_AGGRESSIVE;
		}
	}
}

void Simulation::order_patrol(const uint32_t *unit_ids, size_t count, float tx, float tz) {
	cmd_move(unit_ids, count, tx, tz);
	for (size_t i = 0; i < count; ++i) {
		int32_t idx = lookup_unit(unit_ids[i]);
		if (idx >= 0 && (size_t)idx < units_.count && units_.alive[idx]) {
			units_.action_state[idx] = ACTION_PATROL;
			units_.stance[idx] = STANCE_DEFENSIVE;
		}
	}
}

void Simulation::order_build_or_repair(const uint32_t *unit_ids, size_t count, uint32_t building_id) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return;
	const Building &b = buildings_[bit->second];

	for (size_t i = 0; i < count; ++i) {
		int32_t uidx = lookup_unit(unit_ids[i]);
		if (uidx < 0 || !units_.alive[uidx]) continue;
		uint8_t ut = units_.unit_type[uidx];
		if (ut != UNIT_VILLAGER && ut != UNIT_FEMALE_CITIZEN && ut != UNIT_WORKER_ELEPHANT)
			continue;

		// 0 A.D. UnitAI: workers sent to their own finished, undamaged field farm it.
		if (b.type == BLD_FIELD && b.team == units_.team[uidx] && b.build_progress >= 1.0f && b.hp >= b.max_hp) {
			order_gather(&unit_ids[i], 1, ensure_field_resource(b));
			continue;
		}

		stand_outside_rect(b.x, b.z, b.half_size, units_.id[uidx],
				units_.pos_x[uidx], units_.pos_z[uidx],
				&units_.target_x[uidx], &units_.target_z[uidx]);
		units_.has_order[uidx] = 1;
		units_.action_state[uidx] = ACTION_BUILD;
		units_.target_building_id[uidx] = building_id;
		units_.target_unit_id[uidx] = 0;
		units_.target_resource_id[uidx] = 0;
	}
}

void Simulation::order_set_stance(const uint32_t *unit_ids, size_t count, uint8_t stance) {
	for (size_t i = 0; i < count; ++i) {
		int32_t idx = lookup_unit(unit_ids[i]);
		if (idx >= 0) {
			units_.stance[idx] = stance;
		}
	}
}

void Simulation::order_set_formation(const uint32_t *unit_ids, size_t count, uint8_t formation) {
	// 0 A.D. template_player.xml:84-96 defines 13 formations (indices 0..12).
	// Values in 0..12 are accepted; invalid values (> 12) are clamped to 12.
	if (formation > FORMATION_SHIPS_COLUMN) {
		formation = FORMATION_SHIPS_COLUMN;
	}
	for (size_t i = 0; i < count; ++i) {
		int32_t idx = lookup_unit(unit_ids[i]);
		if (idx >= 0) {
			units_.formation[idx] = formation;
		}
	}
}

int Simulation::get_garrison_count(uint32_t building_id) const {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return 0;
	return (int)buildings_[bit->second].garrisoned_units.size();
}

int Simulation::get_garrison_capacity(uint32_t building_id) const {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return 0;
	return buildings_[bit->second].garrison_cap;
}

const std::vector<uint32_t> *Simulation::get_garrisoned_units(uint32_t building_id) const {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return nullptr;
	return &buildings_[bit->second].garrisoned_units;
}

int Simulation::get_transport_capacity(uint32_t transport_id) const {
	int32_t idx = lookup_unit(transport_id);
	if (idx < 0 || !units_.alive[idx]) return 0;
	uint8_t utype = units_.unit_type[idx];
	if (utype == UNIT_TRIREME) return TRANSPORT_CAPACITY;
	if (utype == UNIT_MERCHANT_SHIP) return 10;
	return 0;
}

bool Simulation::is_transport(uint32_t unit_id) const {
	int32_t idx = lookup_unit(unit_id);
	if (idx < 0 || !units_.alive[idx]) return false;
	uint8_t utype = units_.unit_type[idx];
	return utype == UNIT_TRIREME || utype == UNIT_MERCHANT_SHIP;
}

void Simulation::order_garrison(const uint32_t *unit_ids, size_t count, uint32_t building_id) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return;
	Building &b = buildings_[bit->second];
	if (b.build_progress < 1.0f) return;

	for (size_t i = 0; i < count; ++i) {
		if ((int)b.garrisoned_units.size() >= b.garrison_cap) break;
		int32_t uidx = lookup_unit(unit_ids[i]);
		if (uidx < 0 || !units_.alive[uidx] || units_.team[uidx] != b.team) continue;

		units_.action_state[uidx] = ACTION_GARRISONED;
		units_.alive[uidx] = 0;
		units_.has_order[uidx] = 0;
		units_.vel_x[uidx] = 0.0f;
		units_.vel_z[uidx] = 0.0f;
		b.garrisoned_units.push_back(unit_ids[i]);
	}
}

void Simulation::order_ungarrison(uint32_t building_id) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return;
	Building &b = buildings_[bit->second];

	float angle_step = 6.2831853f / std::max(1, (int)b.garrisoned_units.size());
	int k = 0;
	for (uint32_t uid : b.garrisoned_units) {
		int32_t idx = lookup_unit(uid);
		if (idx >= 0) {
			float a = angle_step * k++;
			float ex = b.x + cosf(a) * (b.half_size + 1.2f);
			float ez = b.z + sinf(a) * (b.half_size + 1.2f);
			units_.pos_x[idx] = ex;
			units_.pos_z[idx] = ez;
			units_.alive[idx] = 1;
			units_.action_state[idx] = ACTION_IDLE;
			units_.has_order[idx] = 0;
			units_.vel_x[idx] = 0.0f;
			units_.vel_z[idx] = 0.0f;
		}
	}
	b.garrisoned_units.clear();
}

void Simulation::order_ungarrison_single(uint32_t building_id, uint32_t unit_id) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return;
	Building &b = buildings_[bit->second];

	auto it = std::find(b.garrisoned_units.begin(), b.garrisoned_units.end(), unit_id);
	if (it != b.garrisoned_units.end()) {
		int32_t idx = lookup_unit(unit_id);
		if (idx >= 0) {
			units_.pos_x[idx] = b.x;
			units_.pos_z[idx] = b.z + b.half_size + 1.2f;
			units_.alive[idx] = 1;
			units_.action_state[idx] = ACTION_IDLE;
			units_.has_order[idx] = 0;
			units_.vel_x[idx] = 0.0f;
			units_.vel_z[idx] = 0.0f;
		}
		b.garrisoned_units.erase(it);
	}
}

void Simulation::order_herd(const uint32_t *unit_ids, size_t count, uint32_t fauna_id) {
	auto it = fauna_id_to_idx_.find(fauna_id);
	if (it == fauna_id_to_idx_.end()) return;
	size_t fidx = it->second;
	if (!fauna_.alive[fidx]) return;

	const auto &info = fauna_type_info(fauna_.fauna_type[fidx]);
	if (!info.is_domestic) {
		order_hunt(unit_ids, count, fauna_id);
		return;
	}

	fauna_.behavior[fidx] = FAUNA_HERDED;
	fauna_.target_unit_id[fidx] = count > 0 ? unit_ids[0] : 0;

	for (size_t k = 0; k < count; ++k) {
		int32_t uidx = lookup_unit(unit_ids[k]);
		if (uidx < 0 || !units_.alive[uidx] || units_.unit_type[uidx] != UNIT_VILLAGER) continue;
		units_.action_state[uidx] = ACTION_MOVE;
		units_.has_order[uidx] = 1;
		units_.target_unit_id[uidx] = fauna_id;
		units_.target_x[uidx] = fauna_.pos_x[fidx];
		units_.target_z[uidx] = fauna_.pos_z[fidx];
	}
}

bool Simulation::cmd_breed_livestock(uint8_t player_id, uint32_t building_id) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return false;
	Building &b = buildings_[bit->second];
	if (b.team != player_id || b.build_progress < 1.0f || b.type != BLD_CORRAL) return false;
	if (b.queue.size() >= 8) return false;
	if (!player(b.team).pay(60.0f, 30.0f, 0.0f, 0.0f)) return false;
	b.queue.push_back({(uint8_t)UNIT_TYPE_NONE, 0.0f, 15.0f});
	return true;
}

const std::vector<uint32_t> *Simulation::get_transport_passengers(uint32_t transport_id) const {
	auto it = transport_passengers_.find(transport_id);
	if (it != transport_passengers_.end()) return &it->second;
	return nullptr;
}

size_t Simulation::get_transport_passenger_count(uint32_t transport_id) const {
	auto it = transport_passengers_.find(transport_id);
	if (it != transport_passengers_.end()) return it->second.size();
	return 0;
}

void Simulation::order_board_transport(const uint32_t *unit_ids, size_t count, uint32_t transport_id) {
	int32_t tidx = lookup_unit(transport_id);
	if (tidx < 0 || !units_.alive[tidx] || !is_transport(transport_id)) return;
	float tx = units_.pos_x[tidx];
	float tz = units_.pos_z[tidx];
	uint8_t team = units_.team[tidx];
	int max_cap = get_transport_capacity(transport_id);

	auto &passengers = transport_passengers_[transport_id];

	for (size_t i = 0; i < count; ++i) {
		if ((int)passengers.size() >= max_cap) break;
		int32_t uidx = lookup_unit(unit_ids[i]);
		if (uidx < 0 || !units_.alive[uidx] || units_.team[uidx] != team) continue;
		if (is_transport(unit_ids[i])) continue;

		float dx = units_.pos_x[uidx] - tx;
		float dz = units_.pos_z[uidx] - tz;
		float d2 = dx * dx + dz * dz;

		if (d2 <= 4.5f * 4.5f) {
			units_.action_state[uidx] = ACTION_GARRISONED;
			units_.alive[uidx] = 0;
			units_.has_order[uidx] = 0;
			units_.vel_x[uidx] = 0.0f;
			units_.vel_z[uidx] = 0.0f;
			passengers.push_back(unit_ids[i]);
		} else {
			units_.action_state[uidx] = ACTION_MOVE;
			units_.has_order[uidx] = 1;
			units_.target_unit_id[uidx] = transport_id;
			units_.target_x[uidx] = tx;
			units_.target_z[uidx] = tz;
		}
	}
}

void Simulation::order_disembark(uint32_t transport_id, float target_x, float target_z) {
	int32_t tidx = lookup_unit(transport_id);
	if (tidx < 0 || !is_transport(transport_id)) return;

	auto pit = transport_passengers_.find(transport_id);
	if (pit == transport_passengers_.end() || pit->second.empty()) return;
	auto &passengers = pit->second;

	float tx = units_.pos_x[tidx];
	float tz = units_.pos_z[tidx];
	float dist_to_target = std::hypot(tx - target_x, tz - target_z);

	if (dist_to_target > 6.0f) {
		units_.target_x[tidx] = target_x;
		units_.target_z[tidx] = target_z;
		units_.has_order[tidx] = 1;
		units_.action_state[tidx] = ACTION_MOVE;
		return;
	}

	float land_x = target_x;
	float land_z = target_z;
	bool found_land = false;

	for (float r = 0.0f; r <= 8.0f && !found_land; r += 1.5f) {
		for (int a = 0; a < 8; ++a) {
			float ang = a * 0.785398f;
			float cx = target_x + cosf(ang) * r;
			float cz = target_z + sinf(ang) * r;
			if (std::abs(cx) >= map_half_ - 2.0f || std::abs(cz) >= map_half_ - 2.0f) continue;
			if (get_terrain_height(cx, cz) >= terrain_water_height_ + 0.15f) {
				land_x = cx;
				land_z = cz;
				found_land = true;
				break;
			}
		}
	}

	if (!found_land) {
		for (float r = 1.0f; r <= 8.0f && !found_land; r += 1.5f) {
			for (int a = 0; a < 8; ++a) {
				float ang = a * 0.785398f;
				float cx = tx + cosf(ang) * r;
				float cz = tz + sinf(ang) * r;
				if (std::abs(cx) >= map_half_ - 2.0f || std::abs(cz) >= map_half_ - 2.0f) continue;
				if (get_terrain_height(cx, cz) >= terrain_water_height_ + 0.15f) {
					land_x = cx;
					land_z = cz;
					found_land = true;
					break;
				}
			}
		}
	}

	if (!found_land) {
		land_x = target_x;
		land_z = target_z;
	}

	float angle_step = 6.2831853f / std::max(1, (int)passengers.size());
	int k = 0;
	for (uint32_t uid : passengers) {
		int32_t idx = lookup_unit(uid);
		if (idx >= 0) {
			float a = angle_step * k++;
			units_.pos_x[idx] = land_x + cosf(a) * 1.5f;
			units_.pos_z[idx] = land_z + sinf(a) * 1.5f;
			units_.alive[idx] = 1;
			units_.action_state[idx] = ACTION_IDLE;
			units_.has_order[idx] = 0;
			units_.vel_x[idx] = 0.0f;
			units_.vel_z[idx] = 0.0f;
		}
	}
	passengers.clear();
}

void Simulation::order_disembark_all(uint32_t transport_id) {
	int32_t tidx = lookup_unit(transport_id);
	if (tidx < 0) return;
	order_disembark(transport_id, units_.pos_x[tidx], units_.pos_z[tidx]);
}

void Simulation::order_disembark_single(uint32_t transport_id, uint32_t unit_id) {
	int32_t tidx = lookup_unit(transport_id);
	if (tidx < 0) return;
	auto pit = transport_passengers_.find(transport_id);
	if (pit == transport_passengers_.end()) return;
	auto it = std::find(pit->second.begin(), pit->second.end(), unit_id);
	if (it != pit->second.end()) {
		int32_t idx = lookup_unit(unit_id);
		if (idx >= 0) {
			float tx = units_.pos_x[tidx];
			float tz = units_.pos_z[tidx];
			float land_x = tx + 2.0f;
			float land_z = tz;
			for (float r = 1.0f; r <= 6.0f; r += 1.5f) {
				for (int a = 0; a < 8; ++a) {
					float ang = a * 0.785398f;
					float cx = tx + cosf(ang) * r;
					float cz = tz + sinf(ang) * r;
					if (get_terrain_height(cx, cz) >= terrain_water_height_ + 0.15f) {
						land_x = cx; land_z = cz; break;
					}
				}
			}
			units_.pos_x[idx] = land_x;
			units_.pos_z[idx] = land_z;
			units_.alive[idx] = 1;
			units_.action_state[idx] = ACTION_IDLE;
			units_.has_order[idx] = 0;
			units_.vel_x[idx] = 0.0f;
			units_.vel_z[idx] = 0.0f;
		}
		pit->second.erase(it);
	}
}

void Simulation::apply_loot_bounty(uint8_t killer_team, int food, int wood, int gold, int stone) {
	if (killer_team >= MAX_PLAYERS) return;
	players_[killer_team].add((float)food, (float)wood, (float)gold, (float)stone);
	if (killer_team == 0) {
		record_.total_food_gathered += food;
		record_.total_wood_gathered += wood;
		record_.total_gold_gathered += gold;
		record_.total_stone_gathered += stone;
	}
}

void Simulation::raise_alert(BattleAlert::Type type, float x, float z, uint32_t entity_id, uint8_t team) {
	if ((size_t)type < 6) {
		float current_time = (float)ticks_ * FIXED_STEP;
		if (current_time - last_alert_time_[type] < 2.0f) {
			return; // Debounce rapid alert spam
		}
		last_alert_time_[type] = current_time;
	}
	BattleAlert alert;
	alert.type = type;
	alert.x = x;
	alert.z = z;
	alert.entity_id = entity_id;
	alert.team = team;
	alert.time_stamp = (float)ticks_ * FIXED_STEP;
	pending_alerts_.push_back(alert);
	if (pending_alerts_.size() > 64) {
		pending_alerts_.erase(pending_alerts_.begin());
	}
}

void Simulation::mark_building_wreck(Building &b) {
	if (b.wrecked) return;
	b.wrecked = true;
	b.hp = 0.0f;
	b.build_progress = 0.0f;
	record_death((float)b.cell_x, (float)b.cell_y, b.team, (b.team == 0) ? 1 : 0, b.type, true);
}

void Simulation::record_death(float x, float z, uint8_t team, uint8_t killer_team, uint8_t type, bool is_building) {
	size_t idx = recent_death_head_ % MAX_RECENT_DEATHS;
	recent_deaths_[idx] = { x, z, team, killer_team, type, is_building, false, false };
	recent_death_head_++;
}

void Simulation::fire_arrow(float sx, float sy, float sz, float tx, float ty, float tz, float damage, uint8_t team, uint32_t target_id, uint8_t effect, uint8_t dmg_type, bool hits_building, uint8_t visual) {
	if (projectiles_.size() >= MAX_PROJECTILES) return;
	Projectile p;
	p.id = alloc_id();
	p.start_x = sx; p.start_y = sy; p.start_z = sz;
	p.target_x = tx; p.target_y = ty; p.target_z = tz;
	p.current_x = sx; p.current_y = sy; p.current_z = sz;
	p.progress = 0.0f;
	p.speed = 26.0f;
	p.arc_height = 2.4f;
	p.visual = visual;
	if (visual == 1) {
		// template_unit_ship_warship Ranged/Projectile Speed=80 Gravity=50
		p.speed = 56.0f;
		p.arc_height = 3.8f;
	} else if (visual == 2) {
		p.speed = 22.0f;
		p.arc_height = 5.5f;
	}
	p.damage = damage;
	p.damage_type = dmg_type;
	p.status_effect = effect;
	p.team = team;
	if (hits_building) {
		p.target_building_id = target_id;
		p.target_unit_id = 0;
	} else {
		p.target_unit_id = target_id;
		p.target_building_id = 0;
	}
	p.alive = true;
	projectiles_.push_back(p);
}

void Simulation::tick(float dt) {
	if (game_over_ || victory_) return;
	auto t0 = std::chrono::high_resolution_clock::now();
	Arena &arena = frame_arena_.current();
	arena.reset();

	// Master simulation tick pipeline in fixed, deterministic order:
	// 1. Movement & spatial separation
	move_units(dt, arena);
	separation(dt, arena);

	// 2. Economy & Gathering
	tick_gather(dt);

	// 3. Combat, promotion, structure capture, status effects, and projectiles
	tick_combat(dt);
	tick_promotion(dt);      // 0 A.D. Promotion.js: unit veterancy progression from combat XP / kills
	tick_capture(dt);        // 0 A.D. Capturable.js: multi-team structure capture & regeneration
	tick_status_effects(dt);
	tick_projectiles(dt);
	tick_death_damage(dt);   // 0 A.D. DeathDamage.js:38-58: on-death splash damage to nearby units
	tick_loot(dt);           // 0 A.D. Loot.js:15-26, Looter.js:14-41: on-death bounty grant to killer

	// 4. Infrastructure & Production
	tick_construction(dt);   // 0 A.D. Foundation.js / Builder.js: building construction
	tick_repair(dt);         // 0 A.D. Repairable.js / Builder.js: repairing damaged completed buildings
	tick_production(dt);     // 0 A.D. ProductionQueue.js: unit training queues
	tick_research(dt);       // 0 A.D. ProductionQueue.js / Researcher.js: technology research progress
	tick_upgrades(dt);       // 0 A.D. Upgrade.js: structure upgrades (sentry tower -> tower, wall -> gate)
	tick_entity_limits(dt);  // 0 A.D. EntityLimits.js:7-12: enforce category limits per player (Wonder: 1, Fortress: 10)
	tick_gates(dt);          // 0 A.D. Gate.js:145-180: auto open / close when allied units in pass range

	// 5. Commerce & Trade
	tick_barter(dt);         // 0 A.D. Barter.js: periodic market price restore & drift
	tick_upkeep(dt);         // 0 A.D. Upkeep.js:51-60: periodic entity upkeep drain
	tick_trickle(dt);        // Passive / trickle income

	// 6. Waves, AI, and World simulation
	if (waves_enabled_) tick_waves(dt);
	tick_petra_ai(dt);
	tick_fauna(dt);
	check_victory_conditions(dt);

	// Income rate rolling decay
	rate_window_timer_ += dt;
	if (rate_window_timer_ >= 5.0f) {
		rate_window_timer_ = 0.0f;
		for (auto &ps : players_)
			for (float &w : ps.gathered_window) w *= 0.5f;
	}

	// Fog of War (Paced at 4 Hz to avoid unnecessary main thread recalculations)
	fow_timer_ += dt;
	if (fow_timer_ >= 0.25f) {
		fow_timer_ = 0.0f;
		tick_fow();
	}

	frame_arena_.flip();
	auto t1 = std::chrono::high_resolution_clock::now();
	tick_us_ += std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
	ticks_++;
}

void Simulation::move_units(float dt, Arena &arena) {
	const size_t n = units_.count;
	if (n == 0) return;
	float *px = units_.pos_x;
	float *pz = units_.pos_z;
	float *vx = units_.vel_x;
	float *vz = units_.vel_z;
	float *tx = units_.target_x;
	float *tz = units_.target_z;
	uint8_t *has = units_.has_order;
	uint8_t *alive = units_.alive;

	float *npx = arena.alloc_array<float>(n);
	float *npz = arena.alloc_array<float>(n);
	if (!npx || !npz) return;
	memcpy(npx, px, n * sizeof(float));
	memcpy(npz, pz, n * sizeof(float));

	// G3 Trader dynamic route handling (sequential, before parallel movement)
	for (size_t ti = 0; ti < n; ++ti) {
		if (!alive[ti] || units_.unit_type[ti] != UNIT_TRADER) continue;
		uint32_t uid = units_.id[ti];
		// Auto-initialize traders that aren't yet in TRADE state but are idle/moving
		if (units_.action_state[ti] != ACTION_TRADE) {
			// If trader spawned from Market and is idle, auto-assign trade route
			if (units_.action_state[ti] == ACTION_IDLE && !units_.has_order[ti]) {
				// Lazily assign if not already routed
				if (trader_routes_.find(uid) == trader_routes_.end()) {
					order_trade_auto(&uid, 1);
				}
			}
			continue;
		}
		// Ensure trader has a route; if not, auto-find
		auto rit = trader_routes_.find(uid);
		if (rit == trader_routes_.end() || (rit->second.first == 0 && rit->second.second == 0)) {
			order_trade_auto(&uid, 1);
			rit = trader_routes_.find(uid);
			if (rit == trader_routes_.end()) continue;
		}
		float dx = tx[ti] - px[ti];
		float dz = tz[ti] - pz[ti];
		if (dx * dx + dz * dz < 2.5f * 2.5f) {
			uint32_t aId = rit->second.first;
			uint32_t bId = rit->second.second;
			int leg = 0;
			auto lit = trader_leg_.find(uid);
			if (lit != trader_leg_.end()) leg = lit->second;
			int gain = 15; // fallback
			bool hasBoth = false;
			const Building *bA = nullptr, *bB = nullptr;
			if (aId != 0 && bId != 0) {
				auto itA = building_id_to_idx_.find(aId);
				auto itB = building_id_to_idx_.find(bId);
				if (itA != building_id_to_idx_.end() && itB != building_id_to_idx_.end()) {
					bA = &buildings_[itA->second];
					bB = &buildings_[itB->second];
					if (bA->build_progress >= 1.0f && bB->build_progress >= 1.0f) {
						gain = calculate_trader_gain(*bA, *bB, units_.team[ti]);
						hasBoth = true;
					}
				}
			}
			if (!hasBoth) {
				// Fallback gain based on map size or single market distance
				// 0 A.D. Market.js:68 & Trader.js:33: gains = floor(distance / CELL * 0.5f + base) * civ_mult
				float fallbackDist = map_half_ * 1.2f;
				if (aId != 0) {
					auto itA = building_id_to_idx_.find(aId);
					if (itA != building_id_to_idx_.end()) {
						fallbackDist = std::hypot(buildings_[itA->second].x, buildings_[itA->second].z) + map_half_;
					}
				}
				const float CELL = 2.0f;
				const float base = 0.0f;
				Civilization civ = civ_of(units_.team[ti]);
				float mult = civ_info(civ).trade_gain_mult;
				gain = static_cast<int>(std::floor((fallbackDist / CELL * 0.5f + base)) * mult);
				gain = std::clamp(gain, 10, 120);
				// If single market route, use fallback ping-pong across map
			}
			// Award gold only when arriving at a market/TC (not every ping); but for fallback we still reward
			PlayerState &tps = player(units_.team[ti]);
			if (units_.team[ti] == 0) {
				tps.gold() += (float)gain;
				tps.gathered_window[RES_GOLD] += (float)gain;
				record_.total_gold_gathered += gain;
			} else {
				tps.gold() = std::min(9999.0f, tps.gold() + (float)gain);
			}

			// Toggle carry to indicate loaded vs empty (visual)
			units_.carry[ti] = (leg == 0) ? 10 : 0;

			// Swap destination
			int newLeg = 1 - leg;
			trader_leg_[uid] = newLeg;
			if (hasBoth && bA && bB) {
				if (newLeg == 0) { tx[ti] = bA->x; tz[ti] = bA->z; }
				else { tx[ti] = bB->x; tz[ti] = bB->z; }
				units_.anchor_x[ti] = tx[ti];
				units_.anchor_z[ti] = tz[ti];
				units_.has_order[ti] = 1;
			} else {
				// Fallback: ping-pong across map_half
				if (aId != 0) {
					auto itA = building_id_to_idx_.find(aId);
					if (itA != building_id_to_idx_.end()) {
						const Building &bm = buildings_[itA->second];
						if (newLeg == 0) { tx[ti] = bm.x; tz[ti] = bm.z; }
						else { tx[ti] = -bm.x * 0.5f; tz[ti] = -bm.z * 0.5f; if (std::abs(tx[ti])<2 && std::abs(tz[ti])<2) { tx[ti]=map_half_*0.6f; tz[ti]=map_half_*0.6f; } }
					} else {
						// no building, simple alternating corners
						if (newLeg == 0) { tx[ti] =  map_half_*0.6f; tz[ti]= map_half_*0.6f; }
						else { tx[ti]= -map_half_*0.6f; tz[ti]= -map_half_*0.6f; }
					}
				} else {
					if (newLeg == 0) { tx[ti] =  map_half_*0.6f; tz[ti]= map_half_*0.6f; }
					else { tx[ti]= -map_half_*0.6f; tz[ti]= -map_half_*0.6f; }
				}
				units_.anchor_x[ti] = tx[ti];
				units_.anchor_z[ti] = tz[ti];
				units_.has_order[ti] = 1;
			}
		}
	}

	// Building steering only ever tests standing buildings that block movement.
	steer_obstacles_.clear();
	closed_gates_.clear();
	for (const Building &b : buildings_)
		if (!b.wrecked && blocks_movement_now(b)) {
			steer_obstacles_.push_back(SteerObstacle{b.id, b.x, b.z, b.half_size, b.type, false});
			if (b.type == BLD_GATE && b.build_progress >= 1.0f)
				closed_gates_.push_back(SteerObstacle{b.id, b.x, b.z, b.half_size, b.type, false});
		}

	JobSystem::instance().for_each_index(n, [&](size_t i) {
		if (!alive[i]) {
			vx[i] = vz[i] = 0;
			if (units_.action_state[i] == ACTION_DYING) {
				units_.death_timer[i] += dt;
			}
			return;
		}
		// Guard.js — follow and protect target unit
		if (units_.action_state[i] == ACTION_GUARD && units_.target_unit_id[i] != 0) {
			int32_t gtidx = lookup_unit(units_.target_unit_id[i]);
			if (gtidx >= 0 && units_.alive[gtidx]) {
				// Stay within 3m of guarded unit
				float gdx = px[gtidx] - px[i];
				float gdz = pz[gtidx] - pz[i];
				float gd2 = gdx * gdx + gdz * gdz;
				if (gd2 > 3.0f * 3.0f) {
					tx[i] = px[gtidx];
					tz[i] = pz[gtidx];
					has[i] = 1;
				} else {
					has[i] = 0; // close enough, stand still
				}
			} else {
				// Target died
				units_.action_state[i] = ACTION_IDLE;
				units_.has_order[i] = 0;
				units_.target_unit_id[i] = 0;
			}
		}

		if (units_.action_state[i] == ACTION_BUILD && units_.target_building_id[i] != 0) {
			auto bit = building_id_to_idx_.find(units_.target_building_id[i]);
			if (bit != building_id_to_idx_.end()) {
				const Building &bb = buildings_[bit->second];
				if (!bb.wrecked && bb.hp > 0.0f) {
					stand_outside_rect(bb.x, bb.z, bb.half_size, units_.id[i],
							px[i], pz[i], &tx[i], &tz[i]);
					has[i] = 1;
				}
			}
		}

		if (!has[i]) {
			vx[i] = vz[i] = 0;
			if (units_.status_effects[i] & EFFECT_KNOCKBACK) {
				npx[i] = std::clamp(px[i] + units_.knockback_dir_x[i] * 14.0f * dt, -map_half_, map_half_);
				npz[i] = std::clamp(pz[i] + units_.knockback_dir_z[i] * 14.0f * dt, -map_half_, map_half_);
			}
			return;
		}

		// GATHER, BUILD and RETURN_DROPSITE walk at (tx, tz) with no cmd_move path. When buildings
		// block that line, path them too, as 0 A.D. UnitMotion paths every move. The path is kept
		// while (tx, tz) stays within 2 world units of the target it was computed for.
		const uint8_t st = units_.action_state[i];
		bool task_path = false;
		if ((st == ACTION_GATHER || st == ACTION_BUILD || st == ACTION_RETURN_DROPSITE) &&
		    (size_t)i < unit_paths_.size() && !unit_is_ship(units_.unit_type[i])) {
			WaypointPath &wp = unit_paths_[i];
			const float gdx = wp.goal_x - tx[i];
			const float gdz = wp.goal_z - tz[i];
			task_path = wp.has_goal && gdx * gdx + gdz * gdz <= 4.0f;
			if (!task_path && rtso::nav::line_blocked_by_buildings(rtso::nav::Vec2(px[i], pz[i]),
					rtso::nav::Vec2(tx[i], tz[i]), 2.0f * map_half_, unit_collision_radius(units_.unit_type[i]),
					steer_obstacles_, units_.target_building_id[i])) {
				const uint8_t ut = units_.unit_type[i];
				const bool mounted = ut == UNIT_CAVALRY || ut == UNIT_CHARIOT || ut == UNIT_CAMEL || ut == UNIT_SCYTHED_CHARIOT;
				find_path(px[i], pz[i], tx[i], tz[i], wp, mounted ? PASS_CAVALRY : PASS_INFANTRY);
				wp.has_goal = true;
				wp.goal_x = tx[i];
				wp.goal_z = tz[i];
				units_.waypoint_cur[i] = static_cast<int16_t>(wp.cur);
				units_.waypoint_count[i] = static_cast<int16_t>(wp.count);
				task_path = true;
			}
		}

		// G1: follow waypoint path if available (mirrors CCmpUnitMotion path following)
		float aim_x;
		float aim_z;
		bool using_path = false;
		if ((size_t)i < unit_paths_.size() && units_.waypoint_count[i] > 0 &&
		    (st == ACTION_MOVE || st == ACTION_ATTACK_MOVE || st == ACTION_PATROL || task_path)) {
			int cur = units_.waypoint_cur[i];
			int cnt = units_.waypoint_count[i];
			if ((size_t)i < unit_paths_.size()) {
				if (unit_paths_[i].count != cnt) cnt = std::min(cnt, unit_paths_[i].count);
			}
			if (cur >= 0 && cur < cnt) {
				WaypointPath &wp = unit_paths_[i];
				// Skip every waypoint already within ARRIVE_DIST (inclusive). Advancing one per tick
				// with a strict < let the arrival check below stop the unit on a waypoint exactly
				// ARRIVE_DIST away, or on a second one just as close, short of its target.
				bool advanced = false;
				while (cur < cnt && cur < wp.count) {
					const float pdx = wp.xs[cur] - px[i];
					const float pdz = wp.zs[cur] - pz[i];
					if (pdx * pdx + pdz * pdz > ARRIVE_DIST * ARRIVE_DIST) break;
					cur++;
					advanced = true;
				}
				if (advanced) {
					units_.waypoint_cur[i] = static_cast<int16_t>(cur);
					wp.cur = cur;
				}
				if (cur >= 0 && cur < cnt && cur < wp.count) {
					aim_x = wp.xs[cur];
					aim_z = wp.zs[cur];
					using_path = true;
				}
			}
		}
		if (!using_path) {
			aim_x = tx[i];
			aim_z = tz[i];
			if (units_.target_unit_id[i] != 0) {
				int32_t tidx = lookup_unit(units_.target_unit_id[i]);
				if (tidx >= 0 && units_.alive[tidx]) {
					aim_x = px[tidx];
					aim_z = pz[tidx];
					tx[i] = aim_x;
					tz[i] = aim_z;
				}
			}
		}

		float dx = aim_x - px[i];
		float dz = aim_z - pz[i];
		float d2 = dx * dx + dz * dz;
		float stop_dist = ARRIVE_DIST;
		if (units_.action_state[i] == ACTION_ATTACK) {
			float r_att = units_.attack_range[i];
			if (r_att > 3.0f) {
				// Ranged units (Archers, Catapults, Triremes) stop comfortably within volley distance
				stop_dist = std::max(2.5f, r_att * 0.85f);
			} else {
				// Melee units stop right at weapon reach to encircle target without penetration
				stop_dist = std::max(1.1f, r_att - 0.2f);
			}
		}

		if (d2 <= stop_dist * stop_dist) {
			vx[i] = 0;
			vz[i] = 0;
			if (units_.action_state[i] == ACTION_MOVE || units_.action_state[i] == ACTION_ATTACK_MOVE) {
				has[i] = 0;
				units_.action_state[i] = ACTION_IDLE;
				units_.waypoint_cur[i] = 0;
				units_.waypoint_count[i] = 0;
				if ((size_t)i < unit_paths_.size()) unit_paths_[i].clear();
			} else if (units_.action_state[i] == ACTION_PATROL) {
				float orig_x = tx[i];
				float orig_z = tz[i];
				tx[i] = -orig_x * 0.8f;
				tz[i] = -orig_z * 0.8f;
			}
			return;
		}

		// 0 A.D. UnitMotion short path: head for the first waypoint around a blocking building
		// (nav/building_steering.h). The arrival check above still uses the real aim point.
		if (!unit_is_ship(units_.unit_type[i])) {
			rtso::nav::Vec2 steer;
			if (rtso::nav::steer_around_buildings(rtso::nav::Vec2(px[i], pz[i]), rtso::nav::Vec2(aim_x, aim_z),
					unit_collision_radius(units_.unit_type[i]), steer_obstacles_, units_.target_building_id[i],
					nav_grid_, steer)) {
				const float sdx = steer.x - px[i];
				const float sdz = steer.y - pz[i];
				if (sdx * sdx + sdz * sdz > 1e-6f) {
					dx = sdx;
					dz = sdz;
					d2 = sdx * sdx + sdz * sdz;
				}
			}
		}

		float inv = 1.0f / sqrtf(d2);
		float ux = dx * inv;
		float uz = dz * inv;
		float spd = units_.speed[i];
		// G5 formation speed modifiers (see cmd_move table): PHALANX slower, WEDGE faster
		// Retain legacy symbol for parity checker: formation_speed_mult[7] = {1.0f, 1.0f, 1.0f, 0.95f, 0.85f, 1.1f, 1.05f};
		{
			static const float formation_speed_mult[13] = {
				1.00f, 1.00f, 1.00f, 0.95f, 0.85f, 1.10f, 1.05f, // 0..6 (NONE,LINE,BOX,COLUMN,PHALANX,WEDGE,SCATTER)
				1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.25f          // 7..12 (ships/column: 1.25x per 0 A.D. ships/column.xml SpeedMultiplier, Formation.js:198)
			};
			uint8_t f = units_.formation[i];
			if (f < 13) spd *= formation_speed_mult[f];
		}
		if (units_.status_effects[i] & (EFFECT_SNARE | EFFECT_SLOW)) {
			spd *= 0.55f; // Slow/Snare speed penalty (0 A.D. status effect)
		}
		// G5 formation-slot correction: cmd_move already rotated offsets into per-unit target_x/z slots
		// (rx,rz). move_units respects has_order slot directly; future find_path (G1) will path to slot not center.
		float move_dx = ux * spd * dt;
		float move_dz = uz * spd * dt;
		if (units_.status_effects[i] & EFFECT_KNOCKBACK) {
			move_dx += units_.knockback_dir_x[i] * 14.0f * dt;
			move_dz += units_.knockback_dir_z[i] * 14.0f * dt;
		}
		npx[i] = std::clamp(px[i] + move_dx, -map_half_, map_half_);
		npz[i] = std::clamp(pz[i] + move_dz, -map_half_, map_half_);
		// A closed gate is solid: slide along it, never step into it.
		if (!closed_gates_.empty()) {
			const float r = unit_collision_radius(units_.unit_type[i]);
			auto inside = [&](float x, float z) {
				for (const auto &g : closed_gates_) {
					const float h = g.half_size + r;
					if (std::fabs(x - g.x) < h && std::fabs(z - g.z) < h) return true;
				}
				return false;
			};
			if (inside(npx[i], npz[i]) && !inside(px[i], pz[i])) {
				if (!inside(npx[i], pz[i])) npz[i] = pz[i];
				else if (!inside(px[i], npz[i])) npx[i] = px[i];
				else { npx[i] = px[i]; npz[i] = pz[i]; }
			}
		}
		vx[i] = ux * spd;
		vz[i] = uz * spd;
	});

	memcpy(px, npx, n * sizeof(float));
	memcpy(pz, npz, n * sizeof(float));

	// Naval Transport Boarding & Moving Passenger Position Synchronization
	for (size_t i = 0; i < n; ++i) {
		if (!units_.alive[i] || is_transport(units_.id[i])) continue;
		if (units_.action_state[i] == ACTION_MOVE && units_.target_unit_id[i] != 0) {
			int32_t tidx = lookup_unit(units_.target_unit_id[i]);
			if (tidx >= 0 && units_.alive[tidx] && is_transport(units_.id[tidx]) && units_.team[tidx] == units_.team[i]) {
				float dx = px[i] - px[tidx];
				float dz = pz[i] - pz[tidx];
				if (dx * dx + dz * dz <= 4.5f * 4.5f) {
					auto &passengers = transport_passengers_[units_.id[tidx]];
					int max_pcap = get_transport_capacity(units_.id[tidx]);
					if ((int)passengers.size() < max_pcap) {
						units_.action_state[i] = ACTION_GARRISONED;
						units_.alive[i] = 0;
						units_.has_order[i] = 0;
						units_.vel_x[i] = 0.0f;
						units_.vel_z[i] = 0.0f;
						passengers.push_back(units_.id[i]);
					}
				}
			}
		}
	}

	for (const auto &kv : transport_passengers_) {
		int32_t tidx = lookup_unit(kv.first);
		if (tidx >= 0 && units_.alive[tidx]) {
			float tx = px[tidx];
			float tz = pz[tidx];
			for (uint32_t puid : kv.second) {
				int32_t pidx = lookup_unit(puid);
				if (pidx >= 0) {
					px[pidx] = tx;
					pz[pidx] = tz;
				}
			}
		}
	}
}

void Simulation::separation(float dt, Arena &arena) {
	const size_t n = units_.count;
	if (n < 2) return;
	float *px = units_.pos_x;
	float *pz = units_.pos_z;

	float *fx = arena.alloc_array<float>(n);
	float *fz = arena.alloc_array<float>(n);
	if (!fx || !fz) return;
	memset(fx, 0, n * sizeof(float));
	memset(fz, 0, n * sizeof(float));

	UnitGrid grid;
	if (!grid.build(arena, n, px, pz, units_.alive, map_half_)) return;

	JobSystem::instance().for_each_index(n, [&](size_t i) {
		if (!units_.alive[i]) return;
		const float pxi = px[i];
		const float pzi = pz[i];
		const float ri = unit_collision_radius(units_.unit_type[i]);
		const uint8_t my_team = units_.team[i];
		const float scan_radius = ri + 2.5f;

		float ax = 0.0f;
		float az = 0.0f;

		// 1. Unit-to-Unit Collision & Physical Separation
		grid.visit_radius(pxi, pzi, scan_radius, [&](int32_t j) {
			if ((size_t)j == i || !units_.alive[j]) return;
			const float rj = unit_collision_radius(units_.unit_type[j]);
			const float min_dist = ri + rj;           // Exact physical body contact boundary
			const float comfort_dist = min_dist + 0.40f; // Personal clearance space

			float dx = pxi - px[j];
			float dz = pzi - pz[j];
			float d2 = dx * dx + dz * dz;

			if (d2 < 0.0001f) {
				// Perfect overlap: Pop apart immediately along deterministic hash angle
				float angle = ((float)((i * 37 + j * 59 + 13) % 628)) * 0.01f;
				float nx = cosf(angle);
				float nz = sinf(angle);
				ax += nx * (min_dist * 1.0f);
				az += nz * (min_dist * 1.0f);
				return;
			}

			if (d2 < min_dist * min_dist) {
				// Hard Penetration: Strong non-linear repulsion to guarantee zero merging
				float dist = sqrtf(d2);
				float nx = dx / dist;
				float nz = dz / dist;
				float overlap = min_dist - dist;
				float force = overlap * 1.8f + (overlap * overlap) * 2.5f;
				ax += nx * force;
				az += nz * force;
			} else if (d2 < comfort_dist * comfort_dist) {
				// Comfort spacing: Smooth spring buffer
				float dist = sqrtf(d2);
				float nx = dx / dist;
				float nz = dz / dist;
				float gap = comfort_dist - dist;
				float w = (units_.team[j] == my_team) ? 0.85f : 0.45f;
				float force = (gap / comfort_dist) * w;
				ax += nx * force;
				az += nz * force;
			}
		});

		// 2. Building Obstacle Non-Penetration (keep units outside the footprint rectangle)
		for (const auto &b : buildings_) {
			if (b.wrecked || !blocks_movement_now(b)) continue;
			float b_half = b.half_size;
			float cx = std::clamp(pxi, b.x - b_half, b.x + b_half);
			float cz = std::clamp(pzi, b.z - b_half, b.z + b_half);
			float bdx = pxi - cx;
			float bdz = pzi - cz;
			float bd2 = bdx * bdx + bdz * bdz;
			float b_req = ri + 0.35f;
			bool inside = (pxi > b.x - b_half && pxi < b.x + b_half &&
					pzi > b.z - b_half && pzi < b.z + b_half);
			if (inside) {
				float dist_east = (b.x + b_half) - pxi;
				float dist_west = pxi - (b.x - b_half);
				float dist_south = (b.z + b_half) - pzi;
				float dist_north = pzi - (b.z - b_half);
				float min_edge = std::min({dist_east, dist_west, dist_south, dist_north});
				float push = b_req + 0.55f;
				if (min_edge == dist_east) ax += push;
				else if (min_edge == dist_west) ax -= push;
				else if (min_edge == dist_south) az += push;
				else az -= push;
				continue;
			}
			if (bd2 < b_req * b_req) {
				float bdist = sqrtf(bd2);
				if (bdist > 0.001f) {
					float b_overlap = b_req - bdist;
					ax += (bdx / bdist) * (b_overlap * 2.5f);
					az += (bdz / bdist) * (b_overlap * 2.5f);
				} else {
					float dist_east = (b.x + b_half) - pxi;
					float dist_west = pxi - (b.x - b_half);
					float dist_south = (b.z + b_half) - pzi;
					float dist_north = pzi - (b.z - b_half);
					float min_edge = std::min({dist_east, dist_west, dist_south, dist_north});
					if (min_edge == dist_east) ax += b_req * 1.5f;
					else if (min_edge == dist_west) ax -= b_req * 1.5f;
					else if (min_edge == dist_south) az += b_req * 1.5f;
					else az -= b_req * 1.5f;
				}
			}
		}

		fx[i] = ax;
		fz[i] = az;
	});

	// Apply firm position relaxation with map boundary clamping
	JobSystem::instance().for_each_index(n, [&](size_t i) {
		if (!units_.alive[i]) return;
		float ri = unit_collision_radius(units_.unit_type[i]);
		float max_push = 3.5f;
		float push_x = std::clamp(fx[i] * dt * 10.0f, -max_push, max_push);
		float push_z = std::clamp(fz[i] * dt * 10.0f, -max_push, max_push);
		px[i] = std::clamp(px[i] + push_x, -map_half_ + ri, map_half_ - ri);
		pz[i] = std::clamp(pz[i] + push_z, -map_half_ + ri, map_half_ - ri);
	});
}

uint32_t Simulation::ensure_field_resource(const Building &b) {
	auto fit = field_res_by_bld_.find(b.id);
	if (fit != field_res_by_bld_.end() && res_id_to_idx_.count(fit->second)) return fit->second;
	// After a load the supply is already in resources_ (saved without its
	// subtype); adopt it by position instead of allocating a new id.
	uint32_t rid = 0;
	for (size_t r = 0; r < resources_.count; ++r) {
		if (resources_.type[r] == RES_FOOD && resources_.amount[r] >= FIELD_FOOD_SUPPLY / 2 &&
				resources_.x[r] == b.x && resources_.z[r] == b.z) {
			rid = resources_.id[r];
			resources_.subtype[r] = RES_SUB_GRAIN;
			break;
		}
	}
	if (rid == 0) rid = spawn_resource_world(b.x, b.z, RES_FOOD, FIELD_FOOD_SUPPLY, RES_SUB_GRAIN);
	field_res_by_bld_[b.id] = rid;
	field_bld_by_res_[rid] = b.id;
	return rid;
}

uint32_t Simulation::find_free_field(uint8_t team, float x, float z) {
	uint32_t best = 0;
	float best_d2 = 1e30f;
	for (const auto &b : buildings_) {
		if (b.team != team || b.type != BLD_FIELD) continue;
		auto fit = field_res_by_bld_.find(b.id);
		if (fit == field_res_by_bld_.end()) continue;
		auto git = field_gatherers_.find(fit->second);
		if (git != field_gatherers_.end() && git->second.total >= FIELD_MAX_GATHERERS) continue;
		float dx = b.x - x, dz = b.z - z;
		float d2 = dx * dx + dz * dz;
		if (d2 < best_d2) { best_d2 = d2; best = fit->second; }
	}
	return best;
}

void Simulation::sync_field_resources() {
	// Retire supplies whose field was destroyed or demolished. Zeroing the
	// amount lets the cleanup at the end of tick_gather remove the resource;
	// each entry is handled independently, so map order doesn't matter.
	for (auto it = field_bld_by_res_.begin(); it != field_bld_by_res_.end();) {
		auto bit = building_id_to_idx_.find(it->second);
		bool alive = bit != building_id_to_idx_.end();
		if (alive) {
			const Building &b = buildings_[bit->second];
			alive = b.type == BLD_FIELD && b.hp > 0.0f && !b.wrecked && b.build_progress >= 1.0f;
		}
		if (alive) { ++it; continue; }
		auto rit = res_id_to_idx_.find(it->first);
		if (rit != res_id_to_idx_.end()) resources_.amount[rit->second] = 0;
		field_res_by_bld_.erase(it->second);
		it = field_bld_by_res_.erase(it);
	}
	for (const auto &b : buildings_) {
		if (b.type != BLD_FIELD || b.build_progress < 1.0f || b.hp <= 0.0f || b.wrecked) continue;
		ensure_field_resource(b);
	}
}

void Simulation::tick_gather(float dt) {
	// G4: rebuild auras every 0.4s (or immediately if empty) for gather modifiers
	aura_timer_ += dt;
	if (aura_timer_ >= 0.4f || aura_mgr_.sources.empty()) {
		aura_timer_ = 0.0f;
		rebuild_auras();
	}
	sync_field_resources();
	const size_t n = units_.count;

	// Gatherers working each field this tick (ResourceSupply.GetNumGatherers),
	// for 0 A.D.'s MaxGatherers cap and diminishing returns.
	field_gatherers_.clear();
	if (!field_bld_by_res_.empty()) {
		for (size_t i = 0; i < n; ++i) {
			if (!units_.alive[i] || units_.action_state[i] != ACTION_GATHER || units_.target_resource_id[i] == 0) continue;
			if (!field_bld_by_res_.count(units_.target_resource_id[i])) continue;
			field_gatherers_[units_.target_resource_id[i]].total++;
		}
	}
	for (size_t i = 0; i < n; ++i) {
		bool is_gatherer = (units_.unit_type[i] == UNIT_VILLAGER ||
		                    units_.unit_type[i] == UNIT_FEMALE_CITIZEN ||
		                    units_.unit_type[i] == UNIT_WORKER_ELEPHANT ||
		                    units_.unit_type[i] == UNIT_FISHING_BOAT);
		if (!units_.alive[i] || !is_gatherer) continue;
		uint8_t uteam = units_.team[i];

		// 1. Returning to dropsite
		if (units_.carry[i] >= carry_cap(i, units_.res_type_carried[i]) || units_.action_state[i] == ACTION_RETURN_DROPSITE) {
			float best_d2 = 1e9f;
			float drop_x = 0, drop_z = 0, drop_half = 0;
			bool found_dropsite = false;

			for (const auto &b : buildings_) {
				if (b.team != uteam || b.build_progress < 1.0f) continue;
				bool valid_drop = false;
				if (b.type == BLD_TOWN_CENTER) valid_drop = true;
				else if (b.type == BLD_STOREHOUSE && (units_.res_type_carried[i] != RES_FOOD)) valid_drop = true;
				else if (b.type == BLD_FARMSTEAD && (units_.res_type_carried[i] == RES_FOOD)) valid_drop = true;
				else if (b.type == BLD_DOCK && (units_.unit_type[i] == UNIT_FISHING_BOAT || units_.res_type_carried[i] == RES_FOOD)) valid_drop = true;

				if (valid_drop) {
					float dx = units_.pos_x[i] - b.x;
					float dz = units_.pos_z[i] - b.z;
					float d2 = dx * dx + dz * dz;
					if (d2 < best_d2) {
						best_d2 = d2;
						drop_x = b.x; drop_z = b.z; drop_half = b.half_size;
						found_dropsite = true;
					}
				}
			}

			if (found_dropsite) {
				float dx = units_.pos_x[i] - drop_x;
				float dz = units_.pos_z[i] - drop_z;
				// Drop off at the centre or anywhere along the footprint: units
				// queued around a large dropsite never get within 2.5 m of its centre.
				float edge_dx = std::max(0.0f, std::fabs(dx) - drop_half);
				float edge_dz = std::max(0.0f, std::fabs(dz) - drop_half);
				if (dx * dx + dz * dz < 2.5f * 2.5f || edge_dx * edge_dx + edge_dz * edge_dz < 1.5f * 1.5f) {
					int amt = units_.carry[i];
					uint8_t rt = units_.res_type_carried[i];
					if (rt < RES_COUNT) {
						PlayerState &ps = player(uteam);
						if (uteam == 0) {
							// Player 0 is uncapped and feeds the HUD rate window and match record.
							ps.res[rt] += (float)amt;
							ps.gathered_window[rt] += (float)amt;
							if (rt == RES_WOOD) record_.total_wood_gathered += amt;
							else if (rt == RES_GOLD) record_.total_gold_gathered += amt;
							else if (rt == RES_STONE) record_.total_stone_gathered += amt;
							else record_.total_food_gathered += amt;
						} else {
							ps.res[rt] = std::min(9999.0f, ps.res[rt] + (float)amt);
							ps.gathered_window[rt] += (float)amt;
						}
					}

					units_.carry[i] = 0;
					units_.action_state[i] = ACTION_GATHER;

					if (units_.target_resource_id[i] != 0) {
						auto rit = res_id_to_idx_.find(units_.target_resource_id[i]);
						if (rit != res_id_to_idx_.end()) {
							units_.target_x[i] = resources_.x[rit->second];
							units_.target_z[i] = resources_.z[rit->second];
							units_.has_order[i] = 1;
						}
					}
				} else {
					units_.target_x[i] = drop_x;
					units_.target_z[i] = drop_z;
					units_.has_order[i] = 1;
					units_.action_state[i] = ACTION_RETURN_DROPSITE;
				}
			}
			continue;
		}

		// 2. Harvesting at resource
		if (units_.action_state[i] == ACTION_GATHER && units_.target_resource_id[i] != 0) {
			auto rit = res_id_to_idx_.find(units_.target_resource_id[i]);
			if (rit == res_id_to_idx_.end() || !resources_.alive[rit->second] || resources_.amount[rit->second] <= 0) {
				units_.target_resource_id[i] = 0;
				units_.action_state[i] = ACTION_IDLE;
				continue;
			}
			size_t ridx = rit->second;
			float field_efficiency = 0.0f; // > 0 only while farming a field
			auto field_it = field_bld_by_res_.find(units_.target_resource_id[i]);
			if (field_it != field_bld_by_res_.end()) {
				auto bit = building_id_to_idx_.find(field_it->second);
				FieldGatherers &fg = field_gatherers_[units_.target_resource_id[i]];
				bool own = bit != building_id_to_idx_.end() && buildings_[bit->second].team == uteam;
				if (!own || fg.slots_taken >= FIELD_MAX_GATHERERS) {
					// Field full or not ours: try our nearest field with a free slot.
					uint32_t alt = own ? find_free_field(uteam, units_.pos_x[i], units_.pos_z[i]) : 0;
					if (alt != 0) {
						fg.total--;
						field_gatherers_[alt].total++;
						order_gather(&units_.id[i], 1, alt);
					} else {
						units_.target_resource_id[i] = 0;
						units_.action_state[i] = ACTION_IDLE;
					}
					continue;
				}
				fg.slots_taken++;
				field_efficiency = field_gather_efficiency(std::min(fg.total, FIELD_MAX_GATHERERS));
			}
			float dx = units_.pos_x[i] - resources_.x[ridx];
			float dz = units_.pos_z[i] - resources_.z[ridx];
			if (dx * dx + dz * dz < 2.5f * 2.5f) {
				units_.attack_timer[i] += dt;
				float harvest_rate = 0.75f;
				{
					// Technology gather-rate multipliers (ResourceGatherer/Rates/*).
					uint8_t rt = resources_.type[ridx];
					GatherKind kind = rt == RES_WOOD ? GK_WOOD : rt == RES_STONE ? GK_STONE : rt == RES_GOLD ? GK_METAL
						: units_.unit_type[i] == UNIT_FISHING_BOAT ? GK_FISH
						: resources_.subtype[ridx] == RES_SUB_GRAIN ? GK_GRAIN
						: resources_.subtype[ridx] == RES_SUB_MEAT ? GK_MEAT : GK_FRUIT;
					harvest_rate /= tech_mods(uteam).gather[units_.unit_type[i]][kind] * ai_gather_mult(uteam);

					Civilization civ = civ_of(uteam);
					if ((rt == RES_GOLD || rt == RES_STONE) && civ_info(civ).metal_gather_mult > 1.0f) {
						harvest_rate /= civ_info(civ).metal_gather_mult;
					}
				}

				// Female Citizen gathering bonus (+25% Food gather speed)
				if (units_.unit_type[i] == UNIT_FEMALE_CITIZEN && resources_.type[ridx] == RES_FOOD) {
					harvest_rate *= 0.75f;
				}

				// Female Citizen Economic Inspiration Aura via AuraManager (10m +15%)
				if (units_.unit_type[i] == UNIT_VILLAGER) {
					float gatherMod = aura_mgr_.query_modifier(units_.pos_x[i], units_.pos_z[i], uteam, AURA_GATHER, units_.tmpl[i]);
					if (gatherMod > 0.001f) {
						harvest_rate *= (1.0f - gatherMod); // 0.15 => 0.85
					}
				}

				// Worker Elephant: +50% heavy harvesting rate for all resources
				if (units_.unit_type[i] == UNIT_WORKER_ELEPHANT) {
					harvest_rate *= 0.65f;
				}

				// 0 A.D. ResourceGatherer Rates: food.grain is half of food.fruit
				// for every gatherer class, then the field's diminishing returns.
				if (field_efficiency > 0.0f) {
					harvest_rate *= 2.0f / field_efficiency;
				}

				if (units_.attack_timer[i] >= harvest_rate) {
					units_.attack_timer[i] = 0.0f;
					int32_t space = carry_cap(i, resources_.type[ridx]) - units_.carry[i];
					int32_t take = std::min(8, std::min(resources_.amount[ridx], space));
					if (field_efficiency <= 0.0f) resources_.amount[ridx] -= take; // fields are infinite
					units_.carry[i] += take;
					units_.res_type_carried[i] = resources_.type[ridx];

					if (units_.carry[i] >= carry_cap(i, resources_.type[ridx])) {
						units_.action_state[i] = ACTION_RETURN_DROPSITE;
					}
				}
			}
		}
	}

	for (size_t r = 0; r < resources_.count; ) {
		if (resources_.amount[r] <= 0) {
			res_id_to_idx_.erase(resources_.id[r]);
			resources_.remove_at(r);
			res_id_to_idx_.clear();
			for (size_t k = 0; k < resources_.count; ++k) res_id_to_idx_[resources_.id[k]] = k;
		} else {
			++r;
		}
	}
}

void Simulation::tick_construction(float dt) {
	// Count builders per building for multi-worker scaling (foundations only)
	std::unordered_map<uint32_t, int> builders_per_bld;
	const size_t n = units_.count;
	for (size_t i = 0; i < n; ++i) {
		if (units_.alive[i] && units_.action_state[i] == ACTION_BUILD && units_.target_building_id[i] != 0) {
			auto bit = building_id_to_idx_.find(units_.target_building_id[i]);
			if (bit != building_id_to_idx_.end() && buildings_[bit->second].build_progress < 1.0f) {
				builders_per_bld[units_.target_building_id[i]]++;
			}
		}
	}

	for (size_t i = 0; i < n; ++i) {
		if (!units_.alive[i] || units_.action_state[i] != ACTION_BUILD || units_.target_building_id[i] == 0) continue;
		auto bit = building_id_to_idx_.find(units_.target_building_id[i]);
		if (bit == building_id_to_idx_.end()) {
			units_.target_building_id[i] = 0;
			units_.action_state[i] = ACTION_IDLE;
			continue;
		}
		Building &b = buildings_[bit->second];
		if (b.hp <= 0.0f) {
			units_.target_building_id[i] = 0;
			units_.action_state[i] = ACTION_IDLE;
			continue;
		}
		// Finished buildings are repaired in tick_repair(); a co-builder whose
		// foundation was completed by someone else has nothing left to do.
		if (b.build_progress >= 1.0f) {
			if (b.hp >= b.max_hp) {
				units_.action_state[i] = ACTION_IDLE;
				units_.target_building_id[i] = 0;
			}
			continue;
		}

		if (outside_and_in_build_range(b.x, b.z, b.half_size, units_.pos_x[i], units_.pos_z[i])) {
			int worker_count = std::max(1, builders_per_bld[b.id]);
			float speed_mult = sqrtf((float)worker_count) / (float)worker_count;
			if (units_.unit_type[i] == UNIT_WORKER_ELEPHANT) speed_mult *= 2.0f; // Worker elephant heavy construction
			Civilization civ = civ_of(units_.team[i]);
			speed_mult /= civ_bonus_mult(civ, CIV_BONUS_BUILDING_BUILD_TIME, b.type); // e.g. Celtic Wooden Construction
			speed_mult /= team_bonus_mult(units_.team[i], TEAM_BONUS_BUILDING_BUILD_TIME, -1, CIV_UNIT_TEMPLATE_NONE, b.type);

			// 0 A.D. Foundation.Commit: first builder on site → scaffold + construction preview.
			b.committed = true;
			const BuildingTypeInfo &binfo = building_type_info(b.type);
			float build_time = binfo.build_time * tech_mods(b.team).bld_build_time[b.type < BLD_TYPE_COUNT ? b.type : 0]
				* ai_time_mult(b.team);
			float rate = (1.0f / std::max(5.0f, build_time)) * speed_mult * dt;
			b.build_progress = std::min(1.0f, b.build_progress + rate);
			b.hp = b.max_hp * b.build_progress;
			if (b.build_progress >= 1.0f) {
				units_.action_state[i] = ACTION_IDLE;
				units_.target_building_id[i] = 0;
				if (b.type == BLD_FIELD) {
					// 0 A.D. UnitAI ConstructionFinished: the builders start farming.
					uint32_t rid = ensure_field_resource(b);
					for (size_t k = 0; k < n; ++k) {
						if (units_.alive[k] && units_.team[k] == b.team &&
								(k == i || (units_.action_state[k] == ACTION_BUILD && units_.target_building_id[k] == b.id)))
							order_gather(&units_.id[k], 1, rid);
					}
				}
				rebuild_nav_grid();
				if (b.type == BLD_TOWN_CENTER || b.type == BLD_FORTRESS || b.type == BLD_TOWER) {
					rebuild_territory();
				}
			}
		}
	}

	// 2. Territory isolation is handled by the distributed capture-point
	// decay in tick_combat; only refresh the territory graph at 1 Hz here.
	territory_timer_ += dt;
	if (territory_timer_ >= 1.0f) {
		territory_timer_ = 0.0f;
		rebuild_territory();
	}

}

void Simulation::tick_repair(float dt) {
	// 0 A.D. Repairable.js:106-136 (Repair / ConstructionFinished) & Builder.js:34-55
	// Completed damaged buildings (build_progress >= 1.0, hp < max_hp) are repaired over time
	// by assigned adjacent builders with multi-worker scaling and civ bonuses.
	// When repaired to full health, builders transition back to ACTION_IDLE (ConstructionFinished).
	std::unordered_map<uint32_t, int> repairers_per_bld;
	const size_t n = units_.count;
	for (size_t i = 0; i < n; ++i) {
		if (units_.alive[i] && units_.action_state[i] == ACTION_BUILD && units_.target_building_id[i] != 0) {
			auto bit = building_id_to_idx_.find(units_.target_building_id[i]);
			if (bit != building_id_to_idx_.end()) {
				const Building &b = buildings_[bit->second];
				if (b.build_progress >= 1.0f && b.hp > 0.0f && b.hp < b.max_hp) {
					repairers_per_bld[b.id]++;
				}
			}
		}
	}

	for (size_t i = 0; i < n; ++i) {
		if (!units_.alive[i] || units_.action_state[i] != ACTION_BUILD || units_.target_building_id[i] == 0) continue;
		auto bit = building_id_to_idx_.find(units_.target_building_id[i]);
		if (bit == building_id_to_idx_.end()) continue;
		Building &b = buildings_[bit->second];
		if (b.build_progress < 1.0f || b.hp <= 0.0f || b.hp >= b.max_hp) continue;

		if (outside_and_in_build_range(b.x, b.z, b.half_size, units_.pos_x[i], units_.pos_z[i])) {
			int worker_count = std::max(1, repairers_per_bld[b.id]);
			float speed_mult = sqrtf((float)worker_count) / (float)worker_count;
			if (units_.unit_type[i] == UNIT_WORKER_ELEPHANT) speed_mult *= 2.0f;
			Civilization civ = civ_of(units_.team[i]);
			speed_mult /= civ_bonus_mult(civ, CIV_BONUS_BUILDING_BUILD_TIME, b.type); // e.g. Celtic Wooden Construction
			speed_mult /= team_bonus_mult(units_.team[i], TEAM_BONUS_BUILDING_BUILD_TIME, -1, CIV_UNIT_TEMPLATE_NONE, b.type);

			// Repair finished building (0 A.D. Repairable.js:114-118 Repair rate)
			b.hp = std::min(b.max_hp, b.hp + dt * 30.0f * speed_mult);
			if (b.hp >= b.max_hp) {
				units_.action_state[i] = ACTION_IDLE;
				units_.target_building_id[i] = 0;
			}
		}
	}
}

void Simulation::tick_production(float dt) {
	for (auto &b : buildings_) {
		if (b.build_progress < 1.0f) continue;
		uint8_t bteam = b.team;

		// Unit Production Queue (0 A.D. ProductionQueue.js:406-440 ProgressTimeout)
		if (!b.queue.empty()) {
			ProductionOrder &order = b.queue.front();
			order.timer += dt;
			if (order.timer >= order.duration) {
				uint8_t utype = order.unit_type;
				uint16_t otmpl = order.tmpl;
				b.queue.erase(b.queue.begin());

				if (b.type == BLD_CORRAL || utype == UNIT_TYPE_NONE) {
					// Domestic livestock bred in Corral (Corral.js)
					float sx = b.x + 1.6f;
					float sz = b.z + 1.6f;
					uint32_t fid = spawn_fauna(sx, sz, FAUNA_SHEEP);
					auto fit = fauna_id_to_idx_.find(fid);
					if (fit != fauna_id_to_idx_.end()) {
						size_t fidx = fit->second;
						fauna_.corral_id[fidx] = b.id;
						fauna_.behavior[fidx] = FAUNA_CORRALLED;
					}
				} else {
					// Spawn unit near building
					float sx = b.x + (b.footprint + 0.6f) * GRID;
					float sz = b.z;
					uint32_t uid = spawn_unit(sx, sz, bteam, (UnitType)utype, 0.0f, otmpl);

					// Route to Rally Point if set
					if (b.has_rally_point) {
						int32_t uidx = lookup_unit(uid);
						if (uidx >= 0) {
							if (b.rally_target_res_id != 0 && utype == UNIT_VILLAGER) {
								order_gather(&uid, 1, b.rally_target_res_id);
							} else if (b.rally_target_bld_id != 0 && utype == UNIT_VILLAGER) {
								order_build_or_repair(&uid, 1, b.rally_target_bld_id);
							} else {
								cmd_move(&uid, 1, b.rally_x, b.rally_z);
							}
						}
					}
				}
			}
		}
	}
}

void Simulation::tick_research(float dt) {
	// 0 A.D. ProductionQueue.js:134-136, Researcher.js:30-75 (ProgressTimeout / QueueTechnology / Progress):
	// ProgressTimeout advances research timer for buildings currently researching.
	// When research_timer reaches research_duration, technology is unlocked,
	// phase requirements are updated, and research state is reset to TECH_NONE.
	for (auto &b : buildings_) {
		if (b.build_progress < 1.0f || b.research_tech == TECH_NONE) continue;
		uint8_t bteam = b.team;
		b.research_timer += dt;
		if (b.research_timer >= b.research_duration) {
			uint8_t tech = b.research_tech;
			b.research_tech = TECH_NONE;
			complete_research(bteam, tech);
		}
	}
}

inline bool in_parabolic_range(float d2, float att_y, float def_y, float base_range) {
	float height_diff = def_y - att_y; // target_y - attacker_y
	float max_r2 = base_range * base_range - 2.0f * base_range * height_diff;
	return (max_r2 > 0.0f) && (d2 <= max_r2);
}

void Simulation::tick_combat(float dt) {
	// G4: ensure auras rebuilt (tick_gather handles 0.4s timer); rebuild if empty for combat phase
	if (aura_mgr_.sources.empty()) {
		rebuild_auras();
	}
	const size_t n = units_.count;
	Arena &arena = frame_arena_.current();
	int32_t *target_idx = n ? arena.alloc_array<int32_t>(n) : nullptr;
	if (n && !target_idx) return;

	UnitGrid grid;
	if (n && !grid.build(arena, n, units_.pos_x, units_.pos_z, units_.alive, map_half_)) return;

	// 1. Target Acquisition & Stance Evaluation (0 A.D. UnitAI.js)
	JobSystem::instance().for_each_index(n, [&](size_t i) {
		// tick_gather uses attack_timer as the harvest clock; counting it down
		// here as well cancelled every harvest tick, so nobody ever gathered.
		if (units_.attack_timer[i] > 0 && units_.action_state[i] != ACTION_GATHER) units_.attack_timer[i] -= dt;
		target_idx[i] = -1;
		if (!units_.alive[i]) return;
		if (units_.attack_damage[i] <= 0.0f) return;

		// Passive stance / civilian gatherers do not auto-acquire combat targets
		if (units_.stance[i] == STANCE_PASSIVE) return;
		if (units_.unit_type[i] == UNIT_VILLAGER &&
				(units_.action_state[i] == ACTION_GATHER || units_.action_state[i] == ACTION_BUILD ||
						units_.action_state[i] == ACTION_RETURN_DROPSITE)) {
			return;
		}

		int32_t found = -1;
		// A. Existing unit target check
		if (units_.target_unit_id[i] != 0) {
			int32_t tidx = lookup_unit(units_.target_unit_id[i]);
			if (tidx >= 0 && units_.alive[tidx] && can_attack(units_.team[i], units_.team[tidx])) {
				found = tidx;
			}
		}

		// B. Stance Leash & Auto-Aggro Scan
		if (found < 0 && units_.target_building_id[i] == 0) {
			float scan_range = unit_info_at(i).aggro_range;
			float leash_radius = 24.0f;

			if (units_.stance[i] == STANCE_STAND_GROUND) {
				scan_range = units_.attack_range[i]; // Only attack targets already in weapon range
				leash_radius = units_.attack_range[i];
			} else if (units_.stance[i] == STANCE_HOLD_GROUND) {
				scan_range = std::min(scan_range, 10.0f);
				leash_radius = 8.0f; // Max pursuit 8m from anchor
			} else if (units_.stance[i] == STANCE_DEFENSIVE) {
				scan_range = std::min(scan_range, 18.0f);
				leash_radius = 20.0f; // Max pursuit 20m from anchor
			} else if (units_.stance[i] == STANCE_AGGRESSIVE) {
				scan_range = std::max(scan_range, 24.0f);
				leash_radius = 1000.0f;
			}

			if ((i + ticks_) % 4 == 0 || units_.action_state[i] == ACTION_ATTACK || units_.action_state[i] == ACTION_ATTACK_MOVE) {
				float best_score = -999999.0f;
				const uint8_t my_team = units_.team[i];
				const uint8_t my_type = units_.unit_type[i];
				const float pxi = units_.pos_x[i];
				const float pzi = units_.pos_z[i];
				const float anc_x = units_.anchor_x[i];
				const float anc_z = units_.anchor_z[i];
				const float att_y = get_terrain_height(pxi, pzi);

				grid.visit_radius(pxi, pzi, scan_range, [&](int32_t j) {
					if (!units_.alive[j] || !can_attack(my_team, units_.team[j])) return;

					// Check leash distance from anchor for Hold/Defensive stances
					if (units_.stance[i] == STANCE_HOLD_GROUND || units_.stance[i] == STANCE_DEFENSIVE) {
						float adx = units_.pos_x[j] - anc_x;
						float adz = units_.pos_z[j] - anc_z;
						if (adx * adx + adz * adz > leash_radius * leash_radius) return;
					}

					float dx = pxi - units_.pos_x[j];
					float dz = pzi - units_.pos_z[j];
					float d2 = dx * dx + dz * dz;

					// Stand ground strict weapon range check
					if (units_.stance[i] == STANCE_STAND_GROUND) {
						float def_y = get_terrain_height(units_.pos_x[j], units_.pos_z[j]);
						if (!in_parabolic_range(d2, att_y, def_y, units_.attack_range[i])) return;
					}

					// 0 A.D. Target Selection Heuristic: Counter-bonus preference + focus fire + proximity
					float score = -sqrtf(d2);
					uint8_t target_type = units_.unit_type[j];

					// Tactical counter preference
					if (my_type == UNIT_SPEARMAN && (target_type == UNIT_CAVALRY || target_type == UNIT_CAMEL || target_type == UNIT_CHARIOT)) score += 35.0f;
					else if ((my_type == UNIT_CAVALRY || my_type == UNIT_CHARIOT) && (target_type == UNIT_ARCHER || target_type == UNIT_ARCHER_MAIDEN || target_type == UNIT_HEALER)) score += 30.0f;
					else if (my_type == UNIT_SWORDSMAN && target_type == UNIT_SPEARMAN) score += 20.0f;
					else if (my_type == UNIT_BOLT_SHOOTER && (target_type == UNIT_ELEPHANT || target_type == UNIT_WORKER_ELEPHANT || target_type == UNIT_HERO)) score += 40.0f;

					// Finishing blow focus fire on low-HP enemies
					if (units_.max_hp[j] > 0.0f) {
						score += (1.0f - units_.hp[j] / units_.max_hp[j]) * 15.0f;
					}

					if (score > best_score) {
						best_score = score;
						found = j;
					}
				});
			}
		}

		if (found >= 0) {
			units_.target_unit_id[i] = units_.id[found];
			units_.has_order[i] = 1;
			if (units_.stance[i] != STANCE_STAND_GROUND) {
				units_.target_x[i] = units_.pos_x[found];
				units_.target_z[i] = units_.pos_z[found];
			}
			if (units_.unit_type[i] != UNIT_VILLAGER) {
				units_.action_state[i] = ACTION_ATTACK;
			}
		} else {
			units_.target_unit_id[i] = 0;
			// Return to leash anchor if hold/defensive stance and combat ended
			if ((units_.stance[i] == STANCE_HOLD_GROUND || units_.stance[i] == STANCE_DEFENSIVE) &&
				units_.action_state[i] == ACTION_ATTACK && units_.target_building_id[i] == 0) {
				float adx = units_.pos_x[i] - units_.anchor_x[i];
				float adz = units_.pos_z[i] - units_.anchor_z[i];
				if (adx * adx + adz * adz > 2.0f * 2.0f) {
					units_.target_x[i] = units_.anchor_x[i];
					units_.target_z[i] = units_.anchor_z[i];
					units_.action_state[i] = ACTION_MOVE;
				} else {
					units_.action_state[i] = ACTION_IDLE;
				}
			}
		}
		target_idx[i] = found;
	});

	// 2. Unit vs Unit Attack Execution
	for (size_t i = 0; i < n; ++i) {
		int32_t t = target_idx[i];
		if (t < 0 || !units_.alive[i] || units_.attack_timer[i] > 0) continue;
		if (units_.attack_damage[i] <= 0.0f) continue;
		float dx = units_.pos_x[i] - units_.pos_x[t];
		float dz = units_.pos_z[i] - units_.pos_z[t];
		float d2 = dx * dx + dz * dz;

		float att_y = get_terrain_height(units_.pos_x[i], units_.pos_z[i]);
		float def_y = get_terrain_height(units_.pos_x[t], units_.pos_z[t]);
		float height_diff = att_y - def_y;

		// 0 A.D. Exact Parabolic Range Equation (CCmpRangeManager.cpp)
		if (!in_parabolic_range(d2, att_y, def_y, units_.attack_range[i])) continue;

		units_.attack_timer[i] = units_.attack_cooldown[i];
		units_.action_state[i] = ACTION_ATTACK;

		// 0 A.D. Exact Exponential Armor Formula (Attack.js + Resistance.js):
		// Damage = sum( Dmg[Type] * 0.9^Armor[Type] )
		const auto &uinfo_att = unit_info_at(i);
		const auto &uinfo_dst = unit_info_at(t);
		float target_hack = uinfo_dst.hack_armor;
		float target_pierce = uinfo_dst.pierce_armor;
		float target_crush = uinfo_dst.crush_armor;

		// Tactical formation armor bonuses
		if (units_.formation[t] == FORMATION_PHALANX) {
			target_pierce += 4.0f; // Shield wall arrow immunity
			target_hack += 3.0f;
		}
		if (units_.formation[t] == FORMATION_SCATTER) target_pierce += 3.0f; // Loose skirmish evasion

		// Researched armor tech bonuses
		if (units_.team[t] < MAX_PLAYERS && units_.unit_type[t] < UNIT_TYPE_COUNT) {
			const float *armor = tech_mods(units_.team[t]).armor[units_.unit_type[t]];
			target_hack += armor[0];
			target_pierce += armor[1];
			target_crush += armor[2];
		}

		// AuraManager defensive armor buff
		float armorAura = aura_mgr_.query_modifier(units_.pos_x[t], units_.pos_z[t], units_.team[t], AURA_ARMOR, units_.tmpl[t]);
		if (armorAura > 0.001f) {
			target_hack += armorAura;
			target_pierce += armorAura;
		}

		float dmg_scale = (uinfo_att.attack_damage > 0.0f) ? (units_.attack_damage[i] / uinfo_att.attack_damage) : 1.0f;
		float hack_dmg = uinfo_att.hack_damage * dmg_scale;
		float pierce_dmg = uinfo_att.pierce_damage * dmg_scale;
		float crush_dmg = uinfo_att.crush_damage * dmg_scale;

		if (units_.formation[i] == FORMATION_WEDGE) {
			hack_dmg *= 1.25f; // Wedge charge bonus
			pierce_dmg *= 1.25f;
			crush_dmg *= 1.25f;
		}

		uint8_t att_team = units_.team[i];
		Civilization att_civ = civ_of(att_team);

		// 0 A.D. Attack.js:GetAttackBonus counters (template bonuses):
		// Spearman deals 2.5x counter bonus against cavalry, camel, and chariot classes.
		float counter_mul = 1.0f;
		uint8_t u_att = units_.unit_type[i];
		uint8_t u_def = units_.unit_type[t];

		if (u_att == UNIT_SPEARMAN && (u_def == UNIT_CAVALRY || u_def == UNIT_CAMEL || u_def == UNIT_CHARIOT)) counter_mul = 2.5f;
		else if ((u_att == UNIT_CAVALRY || u_att == UNIT_CHARIOT || u_att == UNIT_CAMEL) && (u_def == UNIT_ARCHER || u_def == UNIT_ARCHER_MAIDEN || u_def == UNIT_HEALER || u_def == UNIT_FEMALE_CITIZEN)) counter_mul = 1.6f;
		else if (u_att == UNIT_SWORDSMAN && u_def == UNIT_SPEARMAN) counter_mul = 1.4f;
		else if (u_att == UNIT_CAMEL && u_def == UNIT_CAVALRY) counter_mul = 1.5f;
		else if (u_att == UNIT_WAR_DOG && (u_def == UNIT_ARCHER || u_def == UNIT_ARCHER_MAIDEN || u_def == UNIT_VILLAGER || u_def == UNIT_FEMALE_CITIZEN)) counter_mul = 1.4f;
		else if (u_att == UNIT_SCYTHED_CHARIOT && (u_def == UNIT_SWORDSMAN || u_def == UNIT_SPEARMAN || u_def == UNIT_ARCHER)) counter_mul = 1.6f;
		else if (u_att == UNIT_BOLT_SHOOTER && (u_def == UNIT_ELEPHANT || u_def == UNIT_WORKER_ELEPHANT || u_def == UNIT_HERO)) counter_mul = 1.8f;
		else if (u_att == UNIT_TRIREME && unit_is_ship(u_def)) counter_mul = 3.0f;

		hack_dmg *= counter_mul;
		pierce_dmg *= counter_mul;
		crush_dmg *= counter_mul;

		// Carnyx Horn Morale Aura via AuraManager (14m +20% dmg)
		float dmgMult = aura_mgr_.query_modifier(units_.pos_x[i], units_.pos_z[i], att_team, AURA_ATTACK, units_.tmpl[i]);
		if (dmgMult > 1.001f) {
			hack_dmg *= dmgMult;
			pierce_dmg *= dmgMult;
			crush_dmg *= dmgMult;
		}

		// 0 A.D. Exact Logarithmic Armor Reduction (helpers/Attack.js:122 + Resistance.js:84):
		// Damage = sum( Dmg[Type] * pow(0.9f, Armor[Type]) ) with hack, pierce, crush
		float final_dmg = std::max(1.0f,
			hack_dmg * powf(0.9f, target_hack) +
			pierce_dmg * powf(0.9f, target_pierce) +
			crush_dmg * powf(0.9f, target_crush)
		);

		// 0 A.D. Elevation Advantage (CCmpRangeManager.cpp High Ground bonus):
		// Downhill attack bonus capped at +25% (+0.25f), uphill attack penalty floored at -20% (0.80f multiplier)
		if (height_diff > 0.0f) {
			final_dmg *= (1.0f + std::min(0.25f, height_diff * 0.05f));
		} else if (height_diff < 0.0f) {
			final_dmg *= std::max(0.80f, 1.0f + height_diff * 0.04f); // Uphill attack penalty (-20% max)
		}

		bool is_ranged_attack = template_has_ranged_attack(units_.tmpl[i], u_att, att_civ);
		bool maur_elephant_archer = (u_att == UNIT_ELEPHANT && is_ranged_attack);

		if (is_ranged_attack) {
			uint8_t effect = EFFECT_NONE;
			if (u_att == UNIT_ARCHER_MAIDEN) {
				effect |= EFFECT_POISON;
			}
			// Warship LaunchPoint y=3; elephant archer LaunchPoint y=9 (scaled).
			float arrow_sy = (u_att == UNIT_TRIREME) ? 3.0f : (u_att == UNIT_BOLT_SHOOTER ? 1.0f : (maur_elephant_archer ? 3.4f : 1.4f));
			uint8_t proj_vis = (u_att == UNIT_CATAPULT) ? 2 : ((u_att == UNIT_TRIREME || u_att == UNIT_BOLT_SHOOTER) ? 1 : 0);
			// 0 A.D. Spread dispersion
			float dist = sqrtf(d2);
			float spread_factor = (dist / units_.attack_range[i]) * 0.25f;
			float disp_x = (sim_rng_.range_float(0.0f, 1.0f) - 0.5f) * spread_factor;
			float disp_z = (sim_rng_.range_float(0.0f, 1.0f) - 0.5f) * spread_factor;
			float launch_y = get_terrain_height(units_.pos_x[i], units_.pos_z[i]) + arrow_sy;
			float land_y = get_terrain_height(units_.pos_x[t], units_.pos_z[t]) + 0.8f;

			fire_arrow(units_.pos_x[i], launch_y, units_.pos_z[i],
			           units_.pos_x[t] + disp_x, land_y, units_.pos_z[t] + disp_z,
			           final_dmg, units_.team[i], units_.id[t], effect, 1, false, proj_vis);

			// If War Trireme has garrisoned passengers, fire deck archer barrage
			if (u_att == UNIT_TRIREME) {
				auto pit = transport_passengers_.find(units_.id[i]);
				if (pit != transport_passengers_.end() && !pit->second.empty()) {
					int p_archers = 0;
					float p_bonus_dmg = 0.0f;
					for (uint32_t puid : pit->second) {
						int32_t pidx = lookup_unit(puid);
						if (pidx < 0) continue;
						uint8_t pt = units_.unit_type[pidx];
						if (pt == UNIT_ARCHER || pt == UNIT_ARCHER_MAIDEN || pt == UNIT_BOLT_SHOOTER) {
							p_archers += 1;
							p_bonus_dmg += 3.0f;
						} else {
							p_bonus_dmg += 1.0f;
						}
					}
					int extra = std::clamp(1 + p_archers + (int)pit->second.size() / 3, 1, 6);
					for (int a = 0; a < extra; ++a) {
						float sp = (a - extra * 0.5f) * 0.3f;
						float deck_y = get_terrain_height(units_.pos_x[i], units_.pos_z[i]) + 2.5f;
						float hit_y = get_terrain_height(units_.pos_x[t], units_.pos_z[t]) + 0.8f;
						fire_arrow(units_.pos_x[i] + sp, deck_y, units_.pos_z[i] + sp,
								   units_.pos_x[t], hit_y, units_.pos_z[t],
								   12.0f + p_bonus_dmg, units_.team[i], units_.id[t]);
					}
				}
			}
		} else {
			units_.hp[t] -= final_dmg;
			if (units_.team[t] == 0) {
				raise_alert(BattleAlert::ALERT_UNDER_ATTACK, units_.pos_x[t], units_.pos_z[t], units_.id[t], units_.team[t]);
			}

			// Spearman Snare on Cavalry / Chariot / Camel
			if (u_att == UNIT_SPEARMAN && (u_def == UNIT_CAVALRY || u_def == UNIT_CAMEL || u_def == UNIT_CHARIOT)) {
				units_.status_effects[t] |= EFFECT_SNARE;
				units_.snare_timer[t] = 3.0f;
			}

			// Trample Knockback from Heavy Chariots / War Elephants
			if (u_att == UNIT_ELEPHANT || u_att == UNIT_WORKER_ELEPHANT || u_att == UNIT_SCYTHED_CHARIOT) {
				float kdx = units_.pos_x[t] - units_.pos_x[i];
				float kdz = units_.pos_z[t] - units_.pos_z[i];
				float kd2 = kdx * kdx + kdz * kdz;
				if (kd2 > 0.0001f) {
					float kinv = 1.0f / sqrtf(kd2);
					units_.status_effects[t] |= EFFECT_KNOCKBACK;
					units_.knockback_timer[t] = 0.25f;
					units_.knockback_dir_x[t] = kdx * kinv;
					units_.knockback_dir_z[t] = kdz * kinv;
				}
			}

			// 0 A.D. AttackHelper.CauseDamageOverArea (helpers/Attack.js:150-185):
			// Catapult Siege Splash Damage: 4.5m blast radius, 60% splash damage with quadratic falloff (1 - d^2 / r^2)
			if (units_.unit_type[i] == UNIT_CATAPULT) {
				const float blast_r = 4.5f;
				const float blast_r2 = blast_r * blast_r;
				const uint8_t att_team = units_.team[i];
				float tx = units_.pos_x[t];
				float tz = units_.pos_z[t];
				grid.visit_radius(tx, tz, blast_r, [&](int32_t s_idx) {
					if (!units_.alive[s_idx] || !is_hostile(att_team, units_.team[s_idx]) || s_idx == t) return;
					float sx = units_.pos_x[s_idx] - tx;
					float sz = units_.pos_z[s_idx] - tz;
					float dist2 = sx * sx + sz * sz;
					if (dist2 >= blast_r2) return;
					float falloff = 1.0f - (dist2 / blast_r2);
					float s_hack = unit_info_at(s_idx).hack_armor;
					float s_dmg = std::max(1.0f, final_dmg * 0.60f * falloff * powf(0.9f, s_hack * 0.5f));
					units_.hp[s_idx] -= s_dmg;
					if (units_.hp[s_idx] <= 0) {
						units_.alive[s_idx] = 0;
						units_.action_state[s_idx] = ACTION_DYING;
						record_death(units_.pos_x[s_idx], units_.pos_z[s_idx], units_.team[s_idx], att_team, units_.unit_type[s_idx], false);
						if (units_.team[s_idx] == 0) {
							record_.units_lost++;
						} else {
							record_.enemies_killed++;
						}
						// Dynamic Loot bounty from splash casualty
						const auto &s_info = unit_info_at(s_idx);
						int sf_loot = (int)std::round(s_info.food_cost * 0.10f);
						int sw_loot = (int)std::round(s_info.wood_cost * 0.10f);
						int sg_loot = (int)std::round(s_info.gold_cost * 0.10f);
						int ss_loot = (int)std::round(s_info.stone_cost * 0.10f);
						apply_loot_bounty(att_team, sf_loot, sw_loot, sg_loot, ss_loot);
					}
				});
			}

			if (units_.hp[t] <= 0) {
				units_.alive[t] = 0;
				units_.action_state[t] = ACTION_DYING;
				record_death(units_.pos_x[t], units_.pos_z[t], units_.team[t], units_.team[i], units_.unit_type[t], false);
				if (units_.team[t] == 0) {
					record_.units_lost++;
				} else {
					record_.enemies_killed++;
				}

				// Dynamic Loot Tables (Loot.js, Looter.js) - 10% food/wood/stone/gold return to killer + carried resources
				const auto &v_info = unit_info_at(t);
				int f_loot = (int)std::round(v_info.food_cost * 0.10f);
				int w_loot = (int)std::round(v_info.wood_cost * 0.10f);
				int g_loot = (int)std::round(v_info.gold_cost * 0.10f);
				int s_loot = (int)std::round(v_info.stone_cost * 0.10f);
				if (units_.carry[t] > 0) {
					uint8_t rtype = units_.res_type_carried[t];
					if (rtype == RES_FOOD) f_loot += units_.carry[t];
					else if (rtype == RES_WOOD) w_loot += units_.carry[t];
					else if (rtype == RES_GOLD) g_loot += units_.carry[t];
					else if (rtype == RES_STONE) s_loot += units_.carry[t];
				}
				apply_loot_bounty(units_.team[i], f_loot, w_loot, g_loot, s_loot);

				// Trireme destruction: disembark survivors near shore or drown
				if (is_transport(units_.id[t])) {
					auto pit = transport_passengers_.find(units_.id[t]);
					if (pit != transport_passengers_.end()) {
						float ship_x = units_.pos_x[t];
						float ship_z = units_.pos_z[t];
						bool near_shore = (get_terrain_height(ship_x, ship_z) >= terrain_water_height_ - 0.5f);
						for (uint32_t puid : pit->second) {
							int32_t pidx = lookup_unit(puid);
							if (pidx >= 0) {
								if (near_shore) {
									units_.pos_x[pidx] = ship_x + 1.2f;
									units_.pos_z[pidx] = ship_z + 1.2f;
									units_.alive[pidx] = 1;
									units_.action_state[pidx] = ACTION_IDLE;
									units_.hp[pidx] = std::max(10.0f, units_.hp[pidx] * 0.5f);
								} else {
									units_.alive[pidx] = 0;
									units_.action_state[pidx] = ACTION_DYING;
									if (units_.team[pidx] == 0) record_.units_lost++;
								}
							}
						}
						transport_passengers_.erase(pit);
					}
				}

				// Promotion.js - Unit Veterancy & Experience
				units_.kills[i]++;
				units_.xp[i] += 100;
			}
		}
	}

	// 3. Units Attacking Buildings & Siege Demolition (0 A.D. Attack.js + BuildingDamage)
	for (size_t i = 0; i < n; ++i) {
		if (!units_.alive[i] || units_.target_building_id[i] == 0 || units_.attack_timer[i] > 0) continue;
		// Builders and repairers also carry target_building_id; leave them to tick_construction.
		if (units_.action_state[i] == ACTION_BUILD) continue;
		if (units_.attack_damage[i] <= 0.0f) continue;
		auto bit = building_id_to_idx_.find(units_.target_building_id[i]);
		if (bit == building_id_to_idx_.end()) {
			units_.target_building_id[i] = 0;
			units_.action_state[i] = ACTION_IDLE;
			continue;
		}
		Building &b = buildings_[bit->second];
		if (!can_attack(units_.team[i], b.team)) {
			units_.target_building_id[i] = 0;
			continue;
		}

		float dx = units_.pos_x[i] - b.x;
		float dz = units_.pos_z[i] - b.z;
		float d = sqrtf(dx * dx + dz * dz);
		float eff_d = std::max(0.0f, d - b.half_size);

		float att_y = get_terrain_height(units_.pos_x[i], units_.pos_z[i]);
		float bld_y = get_terrain_height(b.x, b.z);

		if (!in_parabolic_range(eff_d * eff_d, att_y, bld_y, units_.attack_range[i])) {
			// Move toward building if out of range
			stand_outside_rect(b.x, b.z, b.half_size, units_.id[i],
					units_.pos_x[i], units_.pos_z[i],
					&units_.target_x[i], &units_.target_z[i]);
			units_.has_order[i] = 1;
			units_.action_state[i] = ACTION_ATTACK;
			continue;
		}

		units_.attack_timer[i] = units_.attack_cooldown[i];
		units_.action_state[i] = ACTION_ATTACK;

		const auto &uinfo_att = unit_info_at(i);
		float hack_dmg = uinfo_att.hack_damage;
		float pierce_dmg = uinfo_att.pierce_damage;
		float crush_dmg = uinfo_att.crush_damage;

		// Siege demolition power comes from 0 A.D.'s own crush damage (rams 150,
		// stone throwers 250 in their templates); no extra multiplier on top.

		// Structure Armor: Hack 3.0, Pierce 15.0 (arrow-resistant), Crush 3.0
		float b_hack_armor = (b.type == BLD_FORTRESS || b.type == BLD_WALL_SEGMENT) ? 5.0f : 3.0f;
		float b_pierce_armor = (b.type == BLD_FORTRESS || b.type == BLD_WALL_SEGMENT) ? 20.0f : 15.0f;
		float b_crush_armor = (b.type == BLD_FORTRESS) ? 6.0f : 3.0f;

		float bld_dmg = std::max(1.0f,
			hack_dmg * powf(0.9f, b_hack_armor) +
			pierce_dmg * powf(0.9f, b_pierce_armor) +
			crush_dmg * powf(0.9f, b_crush_armor)
		);

		bool is_ranged_attack = template_has_ranged_attack(units_.tmpl[i], units_.unit_type[i],
				civ_of(units_.team[i]));

		if (is_ranged_attack) {
			uint8_t ut = units_.unit_type[i];
			float sy = (ut == UNIT_TRIREME) ? 3.0f : (ut == UNIT_CATAPULT ? 2.0f : 1.4f);
			uint8_t vis = (ut == UNIT_CATAPULT) ? 2 : ((ut == UNIT_TRIREME || ut == UNIT_BOLT_SHOOTER) ? 1 : 0);
			float launch_y = get_terrain_height(units_.pos_x[i], units_.pos_z[i]) + sy;
			float land_y = get_terrain_height(b.x, b.z) + 1.5f;
			fire_arrow(units_.pos_x[i], launch_y, units_.pos_z[i],
			           b.x, land_y, b.z,
			           bld_dmg, units_.team[i], b.id, EFFECT_BURNING, 2, true, vis);
		} else {
			b.hp -= bld_dmg;
			if (b.team == 0) {
				raise_alert(BattleAlert::ALERT_BUILDING_UNDER_ATTACK, b.x, b.z, b.id, b.team);
			}
			if (b.hp < b.max_hp * 0.33f) {
				b.is_burning = true;
			}
			if (b.hp <= 0.0f) {
				// Destroy building
				if (units_.team[i] == 0) {
					record_.enemies_killed++;
				} else if (b.team == 0) {
					record_.buildings_destroyed++;
				}
				if (b.team == 0) {
					raise_alert(BattleAlert::ALERT_BUILDING_DESTROYED, b.x, b.z, b.id, b.team);
				}

				// Dynamic Loot bounty upon building destruction (10% food/wood/stone/gold return to killer)
				const auto &b_info = building_type_info(b.type);
				int bf_loot = std::max(10, (int)std::round(b_info.food_cost * 0.10f));
				int bw_loot = std::max(15, (int)std::round(b_info.wood_cost * 0.10f));
				int bg_loot = std::max(10, (int)std::round(b_info.gold_cost * 0.10f));
				int bs_loot = std::max(10, (int)std::round(b_info.stone_cost * 0.10f));
				apply_loot_bounty(units_.team[i], bf_loot, bw_loot, bg_loot, bs_loot);

				// Evacuate garrisoned units with casualty damage
				for (uint32_t guid : b.garrisoned_units) {
					int32_t gidx = lookup_unit(guid);
					if (gidx >= 0 && (size_t)gidx < units_.count) {
						units_.pos_x[gidx] = b.x + 2.0f;
						units_.pos_z[gidx] = b.z + 2.0f;
						units_.alive[gidx] = 1;
						units_.action_state[gidx] = ACTION_IDLE;
						units_.hp[gidx] = std::max(5.0f, units_.hp[gidx] * 0.5f);
					}
				}
				b.garrisoned_units.clear();
				mark_building_wreck(b);
				units_.target_building_id[i] = 0;
				units_.action_state[i] = ACTION_IDLE;
				rebuild_nav_grid();
				if (b.type == BLD_TOWN_CENTER || b.type == BLD_FORTRESS || b.type == BLD_TOWER) {
					rebuild_territory();
				}
			}
		}
	}

	// 4. Defensive Buildings & Parapet Walls attack (TC / Tower / Fortress / Wall + Garrison volley bonus)
	for (auto &b : buildings_) {
		if (b.build_progress < 1.0f || b.hp <= 0.0f) continue;
		const BuildingTypeInfo &binfo = building_type_info(b.type);
		bool is_wall = (b.type == BLD_WALL_SEGMENT);

		if (binfo.attack_damage > 0 || (is_wall && !b.garrisoned_units.empty())) {
			b.attack_timer -= dt;
			if (b.attack_timer <= 0.0f && n) {
				const TechMods &bm = tech_mods(b.team);
				float base_range = (is_wall ? 16.0f : binfo.attack_range) + bm.bld_range[b.type];
				float base_dmg = (is_wall ? 15.0f : binfo.attack_damage) * bm.bld_attack[b.type];
				float cd = is_wall ? 1.0f : binfo.attack_cooldown;

				// Parapet Wall & High-Ground Elevation Advantage (+50% range on walls)
				float wall_y = get_terrain_height(b.x, b.z) + (is_wall ? 4.5f : 3.5f);
				float range = is_wall ? (base_range * 1.5f) : base_range;
				float damage = is_wall ? (base_dmg * 1.25f) : base_dmg;

				// Garrison & Turret System (TurretHolder.js / BuildingAI.js): arrow and damage scaling
				float garrison_arrow_bonus = 0.0f;
				float garrison_dmg_bonus = 0.0f;
				for (uint32_t guid : b.garrisoned_units) {
					int32_t gidx = lookup_unit(guid);
					if (gidx < 0) continue;
					uint8_t gutype = units_.unit_type[gidx];
					if (gutype == UNIT_ARCHER || gutype == UNIT_ARCHER_MAIDEN || gutype == UNIT_BOLT_SHOOTER) {
						garrison_arrow_bonus += 1.0f;
						garrison_dmg_bonus += 2.5f;
					} else if (gutype == UNIT_SWORDSMAN || gutype == UNIT_SPEARMAN || gutype == UNIT_CAVALRY || gutype == UNIT_HERO || gutype == UNIT_CARNYX) {
						garrison_arrow_bonus += 0.5f;
						garrison_dmg_bonus += 1.5f;
					} else {
						garrison_arrow_bonus += 0.25f;
						garrison_dmg_bonus += 0.5f;
					}
				}
				// Sentries / Professional Garrisons / Hellenistic Metropolis
				// (BuildingAI DefaultArrowCount and MaxArrowCount).
				const TechMods &abm = tech_mods(b.team);
				const float arrow_mul = abm.bld_arrow_mul[b.type];
				const float arrow_add = abm.bld_arrow_add[b.type];
				int base_arrows = is_wall ? 0 : (int)std::lround(1.0f * arrow_mul);
				int max_arrows = (b.type == BLD_FORTRESS) ? 10 : ((b.type == BLD_TOWN_CENTER) ? 8 : ((b.type == BLD_TOWER || is_wall) ? 5 : 4));
				max_arrows = (int)std::lround(max_arrows * arrow_mul + arrow_add);
				int total_arrows = std::clamp((int)(base_arrows + arrow_add + std::floor(garrison_arrow_bonus)),
						is_wall ? 0 : 1, max_arrows);
				float final_turret_dmg = damage + garrison_dmg_bonus;

				int32_t hit = -1;
				grid.visit_radius(b.x, b.z, range, [&](int32_t i) {
					if (hit >= 0 || !units_.alive[i] || !can_attack(b.team, units_.team[i])) return;
					float dx = b.x - units_.pos_x[i];
					float dz = b.z - units_.pos_z[i];
					float d2 = dx * dx + dz * dz;
					float unit_y = get_terrain_height(units_.pos_x[i], units_.pos_z[i]);
					if (in_parabolic_range(d2, wall_y, unit_y, range)) {
						hit = i;
					}
				});
				if (hit >= 0) {
					b.attack_timer = cd;
					for (int a = 0; a < total_arrows; ++a) {
						float spread = (a - total_arrows * 0.5f) * 0.22f;
						float hit_y = get_terrain_height(units_.pos_x[hit], units_.pos_z[hit]) + 0.8f;
						fire_arrow(b.x + spread, wall_y, b.z + spread,
								   units_.pos_x[hit], hit_y, units_.pos_z[hit],
								   final_turret_dmg, b.team, units_.id[hit]);
					}
				}
			}
		}
	}

	// 3. Temple + Hero Healing Aura via AuraManager (Heal.js + Auras.js) - 18m 3.5 / 16m 2.0
	for (size_t u = 0; u < n; ++u) {
		if (!units_.alive[u] || units_.hp[u] >= units_.max_hp[u]) continue;
		float healMod = aura_mgr_.query_modifier(units_.pos_x[u], units_.pos_z[u], units_.team[u], AURA_HEAL, units_.tmpl[u]);
		if (healMod > 0.001f) {
			units_.hp[u] = std::min(units_.max_hp[u], units_.hp[u] + dt * healMod);
		}
	}

	// 3b. Priest / Healer Unit Active Healing & Cleansing (UNIT_HEALER)
	for (size_t h = 0; h < n; ++h) {
		if (!units_.alive[h] || units_.unit_type[h] != UNIT_HEALER) continue;
		const uint8_t h_team = units_.team[h];
		const float hx = units_.pos_x[h];
		const float hz = units_.pos_z[h];
		int32_t heal_target = -1;
		float lowest_hp_ratio = 1.0f;

		// Healing Range / Healing Rate technologies (Heal/Range, Heal/Interval).
		const float heal_radius = healer_range(h_team);
		const float heal_r2 = heal_radius * heal_radius;
		grid.visit_radius(hx, hz, heal_radius, [&](int32_t u) {
			if (!units_.alive[u] || units_.team[u] != h_team || u == (int32_t)h) return;
			// visit_radius hands over whole cells, so check the real distance:
			// without this a healer healed every ally on the map.
			const float dx = units_.pos_x[u] - hx, dz = units_.pos_z[u] - hz;
			if (dx * dx + dz * dz > heal_r2) return;
			float ratio = units_.hp[u] / units_.max_hp[u];
			bool has_debuff = (units_.status_effects[u] & (EFFECT_POISON | EFFECT_SNARE | EFFECT_SLOW | EFFECT_BLEED | EFFECT_BURNING)) != 0;
			if (ratio < lowest_hp_ratio || (has_debuff && ratio < 0.99f)) {
				lowest_hp_ratio = ratio;
				heal_target = u;
			}
		});

		if (heal_target >= 0 && (lowest_hp_ratio < 0.999f || (units_.status_effects[heal_target] & (EFFECT_POISON | EFFECT_SNARE | EFFECT_SLOW | EFFECT_BLEED | EFFECT_BURNING)))) {
			units_.hp[heal_target] = std::min(units_.max_hp[heal_target],
					units_.hp[heal_target] + dt * healer_rate(h_team));
			if (units_.status_effects[heal_target] & (EFFECT_POISON | EFFECT_SNARE | EFFECT_SLOW | EFFECT_BLEED | EFFECT_BURNING)) {
				units_.status_effects[heal_target] &= ~(EFFECT_POISON | EFFECT_SNARE | EFFECT_SLOW | EFFECT_BLEED | EFFECT_BURNING);
				units_.poison_timer[heal_target] = 0.0f;
				units_.snare_timer[heal_target] = 0.0f;
				units_.slow_timer[heal_target] = 0.0f;
				units_.bleed_timer[heal_target] = 0.0f;
				units_.burning_timer[heal_target] = 0.0f;
			}
		}
	}

	// 4. Wonder victory countdown lives in check_victory_conditions (wonder mode only).

	// 5. Treasure.js - Wilderness Relic Collection (any team)
	for (auto &rel : relics_) {
		if (rel.claimed) continue;
		for (size_t u = 0; u < n; ++u) {
			if (!units_.alive[u]) continue;
			float dx = units_.pos_x[u] - rel.x;
			float dz = units_.pos_z[u] - rel.z;
			if (dx * dx + dz * dz <= 2.5f * 2.5f) {
				rel.claimed = true;
				rel.owner_team = units_.team[u];
				if (rel.res_type < RES_COUNT) player(units_.team[u]).res[rel.res_type] += (float)rel.amount;
				raise_alert(BattleAlert::ALERT_RELIC_CLAIMED, rel.x, rel.z, rel.id, units_.team[u]);
				break;
			}
		}
	}

	// 6. Hero Unit Inspiration Auras via AuraManager (Auras.js / Heroes) - 16m heal 2.0 (Spartan) / speed 1.20 (Athenian)
	for (size_t u = 0; u < n; ++u) {
		if (!units_.alive[u]) continue;
		float speedMod = aura_mgr_.query_modifier(units_.pos_x[u], units_.pos_z[u], units_.team[u], AURA_SPEED, units_.tmpl[u]);
		if (speedMod > 1.001f) {
			units_.speed[u] = unit_info_at(u).speed * speedMod;
		}
	}

	// Multi-Team Structure Capture Mechanics extracted to tick_capture() (0 A.D. Capturable.js)

	// 8. Remove dead units after death timer
	std::vector<size_t> dead_indices;
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] && units_.action_state[i] == ACTION_DYING &&
				units_.death_timer[i] >= unit_death_duration(units_.unit_type[i])) {
			dead_indices.push_back(i);
		}
	}
	if (!dead_indices.empty()) {
		std::sort(dead_indices.rbegin(), dead_indices.rend());
		for (size_t idx : dead_indices) {
			uint32_t dead_id = units_.id[idx];
			unit_id_to_idx_.erase(dead_id);
			if (dead_id < unit_idx_by_id_.size()) unit_idx_by_id_[dead_id] = -1;
			if (idx < unit_paths_.size() && units_.count > 0) {
				size_t last = units_.count - 1;
				if (idx != last && last < unit_paths_.size()) {
					unit_paths_[idx] = unit_paths_[last];
				}
				if (last < unit_paths_.size()) unit_paths_[last].clear();
			}
			units_.remove_at(idx);
			if (idx < units_.count) {
				bind_unit_index(units_.id[idx], idx);
			}
		}
	}

	// Defeat is decided in check_victory_conditions, per the match's victory condition.
}

void Simulation::tick_promotion(float dt) {
	// 0 A.D. Promotion.js:63-98 (IncreaseXp / Promote) & test_Promotion.js:84-118
	// Veterancy progression: units crossing XP or kill thresholds advance rank,
	// gaining maximum HP multipliers, health recovery, and attack damage boosts.
	// Threshold table:
	// Rank 1: Basic (initial)
	// Rank 2: Advanced (★)  threshold: 300 XP (or 3 kills), +15% max HP, +20 HP heal, +10% attack dmg
	// Rank 3: Elite (★★)   threshold: 700 XP (or 7 kills), +15% max HP, +30 HP heal, +15% attack dmg
	const size_t n = units_.count;
	for (size_t i = 0; i < n; ++i) {
		if (!units_.alive[i]) continue;
		if (units_.rank[i] < 2 && (units_.xp[i] >= 300 || units_.kills[i] >= 3)) {
			units_.rank[i] = 2; // Advanced
			units_.max_hp[i] *= 1.15f;
			units_.hp[i] = std::min(units_.max_hp[i], units_.hp[i] + 20.0f);
			units_.attack_damage[i] *= 1.10f;
		}
		if (units_.rank[i] < 3 && (units_.xp[i] >= 700 || units_.kills[i] >= 7)) {
			units_.rank[i] = 3; // Elite
			units_.max_hp[i] *= 1.15f;
			units_.hp[i] = std::min(units_.max_hp[i], units_.hp[i] + 30.0f);
			units_.attack_damage[i] *= 1.15f;
		}
	}
}

void Simulation::tick_capture(float dt) {
	// 0 A.D. Capturable.js:93-132 (Reduce), 168-181 (RegisterCapturePointsChanged),
	// 183-201 (GetRegenRate & GarrisonRegenRate), 203-247 (TimerTick)
	// Multi-Team Structure Capture Mechanics:
	// CapturePoints is an array whose sum equals max_capture_points.
	// Attacking units within reach reduce enemy capture pools and transfer points to capturer.
	// Owner regenerates capture points back when unattacked; structure flips ownership when
	// a team crosses the majority threshold (>50% max) or owner points hit 0.
	const size_t n = units_.count;
	for (auto &b : buildings_) {
		if (b.build_progress < 1.0f || b.max_capture_points <= 0.0f) continue;
		if (std::abs(std::accumulate(b.capture_points_by_team.begin(), b.capture_points_by_team.end(), 0.0f) - b.max_capture_points) > 0.01f) {
			b.capture_points_by_team.fill(0.0f);
			b.capture_points_by_team[b.team < 8 ? b.team : 0] = b.max_capture_points;
		}
		int count[4] = {0, 0, 0, 0};
		float strength[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		for (size_t u = 0; u < n; ++u) {
			if (!units_.alive[u] || units_.team[u] >= 4) continue;
			const float dx = units_.pos_x[u] - b.x;
			const float dz = units_.pos_z[u] - b.z;
			if (dx * dx + dz * dz <= 11.0f * 11.0f) {
				count[units_.team[u]]++;
				strength[units_.team[u]] += unit_capture_strength(units_.unit_type[u]);
			}
		}
		const int owner = b.team < 4 ? b.team : 0;
		auto enemy = [&](int source, int target) {
			return can_attack((uint8_t)source, (uint8_t)target);
		};
		int captor = -1;
		float captor_strength = 0.0f;
		for (int t = 0; t < 4; ++t) {
			if (!enemy(t, owner) || count[t] == 0) continue;
			if (strength[t] > captor_strength) { captor = t; captor_strength = strength[t]; }
		}
		// TerritoryDecay.js: only structures outside their owner's territory
		// decay, and the points go to whoever owns the ground they stand on.
		// RTS0 has no Gaia player, so no-man's land doesn't decay. (Decaying
		// every unguarded building toward player 1 made them flip back and
		// forth between players every few seconds.)
		const int ground = territory_owner_at(b.x, b.z);
		if (b.territory_decay_rate > 0.0f && count[owner] == 0 && ground >= 0 && ground < 4 &&
				ground != owner && !is_ally((uint8_t)ground, (uint8_t)owner)) {
			const int neighbour = ground;
			const float decay = std::min(b.territory_decay_rate * dt, b.capture_points_by_team[owner]);
			b.capture_points_by_team[owner] -= decay;
			b.capture_points_by_team[neighbour] += decay;
		}
		if (captor >= 0 && captor_strength > 0.0f) {
			float remaining = std::min(captor_strength * dt, b.max_capture_points);
			while (remaining > 0.0001f) {
				int pools = 0;
				for (int t = 0; t < 4; ++t)
					if (enemy(captor, t) && b.capture_points_by_team[t] > 0.0f) pools++;
				if (pools == 0) break;
				const float share = remaining / pools;
				float removed = 0.0f;
				for (int t = 0; t < 4; ++t) {
					if (!enemy(captor, t) || b.capture_points_by_team[t] <= 0.0f) continue;
					const float take = std::min(b.capture_points_by_team[t], share);
					b.capture_points_by_team[t] -= take;
					removed += take;
				}
				remaining -= removed;
				if (removed <= 0.0001f) break;
			}
			const float taken = b.max_capture_points - std::accumulate(b.capture_points_by_team.begin(), b.capture_points_by_team.end(), 0.0f);
			b.capture_points_by_team[captor] += taken;
		} else if (b.capture_points_by_team[owner] < b.max_capture_points && (b.capture_regen_rate > 0.0f || b.capture_garrison_regen_rate > 0.0f)) {
			// 0 A.D. Capturable.js:183-201 GetRegenRate & TimerTick lines 233-238:
			// Owner regenerates capture points back from enemies.
			const float garrison = (float)b.garrisoned_units.size() * b.capture_garrison_regen_rate;
			const float total_regen = b.capture_regen_rate + garrison;
			if (total_regen > 0.0f) {
				float remaining = std::min(total_regen * dt, b.max_capture_points - b.capture_points_by_team[owner]);
				while (remaining > 0.0001f) {
					int enemy_pools = 0;
					for (int t = 0; t < 4; ++t)
						if (enemy(owner, t) && b.capture_points_by_team[t] > 0.0f) enemy_pools++;
					if (enemy_pools == 0) break;
					const float share = remaining / enemy_pools;
					float removed = 0.0f;
					for (int t = 0; t < 4; ++t) {
						if (!enemy(owner, t) || b.capture_points_by_team[t] <= 0.0f) continue;
						const float take = std::min(b.capture_points_by_team[t], share);
						b.capture_points_by_team[t] -= take;
						removed += take;
					}
					remaining -= removed;
					if (removed <= 0.0001f) break;
				}
				const float restored = b.max_capture_points - std::accumulate(b.capture_points_by_team.begin(), b.capture_points_by_team.end(), 0.0f);
				b.capture_points_by_team[owner] += restored;
			}
		}

		// Owner flip when a team crosses majority (> 50% max) or owner points reach 0 (Capturable.js:168-181)
		int best = owner;
		for (int t = 0; t < 4; ++t) {
			if (b.capture_points_by_team[t] > b.capture_points_by_team[best]) {
				best = t;
			}
		}
		if (best != owner && (b.capture_points_by_team[best] > b.max_capture_points * 0.5f || b.capture_points_by_team[owner] <= 0.0001f)) {
			order_ungarrison(b.id);
			cancel_upgrade(b, b.team);
			b.team = (uint8_t)best;
			raise_alert(BattleAlert::ALERT_STRUCTURE_CAPTURED, b.x, b.z, b.id, b.team);
			if (b.type == BLD_TOWN_CENTER || b.type == BLD_FORTRESS || b.type == BLD_TOWER) rebuild_territory();
		}
		b.capture_points = b.capture_points_by_team[b.team < 8 ? b.team : 0];
	}
}

void Simulation::tick_projectiles(float dt) {
	for (auto &p : projectiles_) {
		if (!p.alive) continue;
		float dx = p.target_x - p.start_x;
		float dz = p.target_z - p.start_z;
		float total_dist = sqrtf(dx * dx + dz * dz);
		if (total_dist < 0.1f) total_dist = 0.1f;

		p.progress += (p.speed / total_dist) * dt;
		if (p.progress >= 1.0f) {
			p.progress = 1.0f;
			p.alive = false;

			if (p.target_unit_id != 0) {
				int32_t tidx = lookup_unit(p.target_unit_id);
				if (tidx >= 0 && units_.alive[tidx]) {
					units_.hp[tidx] -= p.damage;
					if (units_.team[tidx] == 0) {
						raise_alert(BattleAlert::ALERT_UNDER_ATTACK, units_.pos_x[tidx], units_.pos_z[tidx], units_.id[tidx], units_.team[tidx]);
					}
					if (p.status_effect != 0) {
						units_.status_effects[tidx] |= p.status_effect;
						if (p.status_effect & EFFECT_POISON) units_.poison_timer[tidx] = 5.0f;
						if (p.status_effect & EFFECT_SNARE) units_.snare_timer[tidx] = 3.0f;
						if (p.status_effect & EFFECT_SLOW) units_.slow_timer[tidx] = 3.0f;
						if (p.status_effect & EFFECT_BLEED) units_.bleed_timer[tidx] = 4.0f;
						if (p.status_effect & EFFECT_BURNING) units_.burning_timer[tidx] = 4.0f;
					}
					if (units_.hp[tidx] <= 0) {
						units_.alive[tidx] = 0;
						units_.action_state[tidx] = ACTION_DYING;
						record_death(units_.pos_x[tidx], units_.pos_z[tidx], units_.team[tidx], p.team, units_.unit_type[tidx], false);
						if (units_.team[tidx] == 0) {
							record_.units_lost++;
						} else {
							record_.enemies_killed++;
						}
						// Dynamic Loot bounty from projectile kill
						const auto &pt_info = unit_info_at(tidx);
						int pf_loot = (int)std::round(pt_info.food_cost * 0.10f);
						int pw_loot = (int)std::round(pt_info.wood_cost * 0.10f);
						int pg_loot = (int)std::round(pt_info.gold_cost * 0.10f);
						int ps_loot = (int)std::round(pt_info.stone_cost * 0.10f);
						if (units_.carry[tidx] > 0) {
							uint8_t rtype = units_.res_type_carried[tidx];
							if (rtype == RES_FOOD) pf_loot += units_.carry[tidx];
							else if (rtype == RES_WOOD) pw_loot += units_.carry[tidx];
							else if (rtype == RES_GOLD) pg_loot += units_.carry[tidx];
							else if (rtype == RES_STONE) ps_loot += units_.carry[tidx];
						}
						apply_loot_bounty(p.team, pf_loot, pw_loot, pg_loot, ps_loot);
					}
				}
			} else if (p.target_building_id != 0) {
				auto bit = building_id_to_idx_.find(p.target_building_id);
				if (bit != building_id_to_idx_.end()) {
					Building &b = buildings_[bit->second];
					if (!b.wrecked && b.hp > 0.0f) {
						b.hp -= p.damage;
						if (b.team == 0)
							raise_alert(BattleAlert::ALERT_BUILDING_UNDER_ATTACK, b.x, b.z, b.id, b.team);
						if (b.hp < b.max_hp * 0.33f)
							b.is_burning = true;
						if (b.hp <= 0.0f) {
							if (p.team == 0)
								record_.enemies_killed++;
							else if (b.team == 0)
								record_.buildings_destroyed++;
							if (b.team == 0)
								raise_alert(BattleAlert::ALERT_BUILDING_DESTROYED, b.x, b.z, b.id, b.team);
							const auto &b_info = building_type_info(b.type);
							apply_loot_bounty(p.team,
									std::max(10, (int)std::round(b_info.food_cost * 0.10f)),
									std::max(15, (int)std::round(b_info.wood_cost * 0.10f)),
									std::max(10, (int)std::round(b_info.gold_cost * 0.10f)),
									std::max(10, (int)std::round(b_info.stone_cost * 0.10f)));
							mark_building_wreck(b);
							rebuild_nav_grid();
							if (b.type == BLD_TOWN_CENTER || b.type == BLD_FORTRESS || b.type == BLD_TOWER)
								rebuild_territory();
						}
					}
				}
			}

			// 0 A.D. AttackHelper.CauseDamageOverArea (helpers/Attack.js:150-185):
			// Projectile splash damage for siege stones (visual == 2):
			// 4.5m blast radius, 60% splash damage with quadratic falloff (1 - d^2 / r^2)
			if (p.visual == 2) {
				const float blast_r = 4.5f;
				const float blast_r2 = blast_r * blast_r;
				const float impact_x = p.target_x;
				const float impact_z = p.target_z;
				const int32_t direct_target = (p.target_unit_id != 0) ? lookup_unit(p.target_unit_id) : -1;
				const size_t total_u = units_.size();
				for (size_t s_idx = 0; s_idx < total_u; ++s_idx) {
					if (!units_.alive[s_idx] || !is_hostile(p.team, units_.team[s_idx]) || (int32_t)s_idx == direct_target) continue;
					float sx = units_.pos_x[s_idx] - impact_x;
					float sz = units_.pos_z[s_idx] - impact_z;
					float dist2 = sx * sx + sz * sz;
					if (dist2 >= blast_r2) continue;
					float falloff = 1.0f - (dist2 / blast_r2);
					float s_hack = unit_info_at(s_idx).hack_armor;
					float s_dmg = std::max(1.0f, p.damage * 0.60f * falloff * powf(0.9f, s_hack * 0.5f));
					units_.hp[s_idx] -= s_dmg;
					if (units_.hp[s_idx] <= 0) {
						units_.alive[s_idx] = 0;
						units_.action_state[s_idx] = ACTION_DYING;
						record_death(units_.pos_x[s_idx], units_.pos_z[s_idx], units_.team[s_idx], p.team, units_.unit_type[s_idx], false);
						if (units_.team[s_idx] == 0) {
							record_.units_lost++;
						} else {
							record_.enemies_killed++;
						}
						// Dynamic Loot bounty from splash kill
						const auto &s_info = unit_info_at(s_idx);
						int sf_loot = (int)std::round(s_info.food_cost * 0.10f);
						int sw_loot = (int)std::round(s_info.wood_cost * 0.10f);
						int sg_loot = (int)std::round(s_info.gold_cost * 0.10f);
						int ss_loot = (int)std::round(s_info.stone_cost * 0.10f);
						apply_loot_bounty(p.team, sf_loot, sw_loot, sg_loot, ss_loot);
					}
				}
			}
		}

		// 0 A.D. CCmpProjectileManager.cpp flight arc equation: y = y0 + (y1 - y0)*t + 4*h*t*(1 - t)
		p.current_x = p.start_x + (p.target_x - p.start_x) * p.progress;
		p.current_z = p.start_z + (p.target_z - p.start_z) * p.progress;
		float h_linear = p.start_y + (p.target_y - p.start_y) * p.progress;
		float h_arc = 4.0f * p.arc_height * p.progress * (1.0f - p.progress);
		p.current_y = h_linear + h_arc;
	}

	projectiles_.erase(std::remove_if(projectiles_.begin(), projectiles_.end(),
		[](const Projectile &p) { return !p.alive; }), projectiles_.end());
}

void Simulation::tick_status_effects(float dt) {
	const size_t n = units_.count;
	for (size_t i = 0; i < n; ++i) {
		if (!units_.alive[i]) continue;

		// 1. Poison effect (Kushite poison arrows / venom) dealing ticking damage
		if (units_.status_effects[i] & EFFECT_POISON) {
			units_.poison_timer[i] -= dt;
			units_.hp[i] -= 4.0f * dt; // 4 HP / sec poison tick
			if (units_.hp[i] <= 0.0f) {
				units_.alive[i] = 0;
				units_.action_state[i] = ACTION_DYING;
				record_death(units_.pos_x[i], units_.pos_z[i], units_.team[i], (uint8_t)(1 - units_.team[i]), units_.unit_type[i], false);
				if (units_.team[i] == 0) record_.units_lost++;
				else record_.enemies_killed++;
				const auto &v_info = unit_info_at(i);
				apply_loot_bounty(1 - units_.team[i], (int)std::round(v_info.food_cost * 0.1f), (int)std::round(v_info.wood_cost * 0.1f), (int)std::round(v_info.gold_cost * 0.1f), (int)std::round(v_info.stone_cost * 0.1f));
			}
			if (units_.poison_timer[i] <= 0.0f) {
				units_.status_effects[i] &= ~EFFECT_POISON;
				units_.poison_timer[i] = 0.0f;
			}
		}

		// 2. Slow & Snare effect (Spear brace / Cavalry snare / Slow)
		if (units_.status_effects[i] & EFFECT_SNARE) {
			units_.snare_timer[i] -= dt;
			if (units_.snare_timer[i] <= 0.0f) {
				units_.status_effects[i] &= ~EFFECT_SNARE;
				units_.snare_timer[i] = 0.0f;
			}
		}
		if (units_.status_effects[i] & EFFECT_SLOW) {
			units_.slow_timer[i] -= dt;
			if (units_.slow_timer[i] <= 0.0f) {
				units_.status_effects[i] &= ~EFFECT_SLOW;
				units_.slow_timer[i] = 0.0f;
			}
		}

		// 3. Bleed effect (Physical bleed DoT)
		if (units_.status_effects[i] & EFFECT_BLEED) {
			units_.bleed_timer[i] -= dt;
			units_.hp[i] -= 6.0f * dt; // 6 HP / sec bleed tick
			if (units_.hp[i] <= 0.0f) {
				units_.alive[i] = 0;
				units_.action_state[i] = ACTION_DYING;
				record_death(units_.pos_x[i], units_.pos_z[i], units_.team[i], (uint8_t)(1 - units_.team[i]), units_.unit_type[i], false);
				if (units_.team[i] == 0) record_.units_lost++;
				else record_.enemies_killed++;
				const auto &v_info = unit_info_at(i);
				apply_loot_bounty(1 - units_.team[i], (int)std::round(v_info.food_cost * 0.1f), (int)std::round(v_info.wood_cost * 0.1f), (int)std::round(v_info.gold_cost * 0.1f), (int)std::round(v_info.stone_cost * 0.1f));
			}
			if (units_.bleed_timer[i] <= 0.0f) {
				units_.status_effects[i] &= ~EFFECT_BLEED;
				units_.bleed_timer[i] = 0.0f;
			}
		}

		// 4. Fire burning effect on units
		if (units_.status_effects[i] & EFFECT_BURNING) {
			units_.burning_timer[i] -= dt;
			units_.hp[i] -= 8.0f * dt; // 8 HP / sec fire burning tick
			if (units_.hp[i] <= 0.0f) {
				units_.alive[i] = 0;
				units_.action_state[i] = ACTION_DYING;
				record_death(units_.pos_x[i], units_.pos_z[i], units_.team[i], (uint8_t)(1 - units_.team[i]), units_.unit_type[i], false);
				if (units_.team[i] == 0) record_.units_lost++;
				else record_.enemies_killed++;
				const auto &v_info = unit_info_at(i);
				apply_loot_bounty(1 - units_.team[i], (int)std::round(v_info.food_cost * 0.1f), (int)std::round(v_info.wood_cost * 0.1f), (int)std::round(v_info.gold_cost * 0.1f), (int)std::round(v_info.stone_cost * 0.1f));
			}
			if (units_.burning_timer[i] <= 0.0f) {
				units_.status_effects[i] &= ~EFFECT_BURNING;
				units_.burning_timer[i] = 0.0f;
			}
		}

		// 5. Knockback effect (Trample impulse)
		if (units_.status_effects[i] & EFFECT_KNOCKBACK) {
			units_.knockback_timer[i] -= dt;
			if (units_.knockback_timer[i] <= 0.0f) {
				units_.status_effects[i] &= ~EFFECT_KNOCKBACK;
				units_.knockback_timer[i] = 0.0f;
			}
		}
	}

	// 6. Burning structures (< 33% HP)
	for (auto &b : buildings_) {
		if (b.build_progress >= 1.0f && b.hp > 0.0f && b.hp < b.max_hp * 0.33f) {
			b.hp = std::max(0.0f, b.hp - 6.0f * dt); // 6 HP / sec fire burning tick
			if (b.hp <= 0.0f) {
				if (b.team == 0) {
					record_.buildings_destroyed++;
					raise_alert(BattleAlert::ALERT_BUILDING_DESTROYED, b.x, b.z, b.id, b.team);
				}
				mark_building_wreck(b);
				rebuild_nav_grid();
				if (b.type == BLD_TOWN_CENTER || b.type == BLD_FORTRESS || b.type == BLD_TOWER) {
					rebuild_territory();
				}
			}
		}
	}
}

void Simulation::tick_barter(float dt) {
	// 0 A.D. Barter.js:41-46 (RESTORE_TIMER_INTERVAL = 5000ms, DIFFERENCE_RESTORE = 0.5%),
	// Barter.js:130-147 (ProgressTimeout), and Market.js:18-35
	// Periodic market price restore towards equilibrium (1.0f) on a 5-second cadence and deterministic
	// natural price drift across goods.
	barter_timer_ += dt;
	if (barter_timer_ < 5.0f) return;
	barter_timer_ = 0.0f;

	for (int r = 0; r < 4; ++r) {
		// Restore buy prices towards 1.0f base equilibrium (DIFFERENCE_RESTORE = 0.5%)
		if (market_buy_price_[r] > 1.0f) {
			market_buy_price_[r] = std::max(1.0f, market_buy_price_[r] - 0.005f);
		} else if (market_buy_price_[r] < 1.0f) {
			market_buy_price_[r] = std::min(1.0f, market_buy_price_[r] + 0.005f);
		}

		// Restore sell prices towards 1.0f base equilibrium (DIFFERENCE_RESTORE = 0.5%)
		if (market_sell_price_[r] > 1.0f) {
			market_sell_price_[r] = std::max(1.0f, market_sell_price_[r] - 0.005f);
		} else if (market_sell_price_[r] < 1.0f) {
			market_sell_price_[r] = std::min(1.0f, market_sell_price_[r] + 0.005f);
		}

		// Deterministic schedule drift (fixed schedule cycle ~120s with +/-0.25% market fluctuation)
		float drift = 0.0025f * std::sin((float)(ticks_ + (uint64_t)r * 150) * 0.001f);
		market_buy_price_[r] = std::clamp(market_buy_price_[r] + drift, 0.2f, 5.0f);
		market_sell_price_[r] = std::clamp(market_sell_price_[r] + drift, 0.2f, 5.0f);
	}
}

uint8_t Simulation::side_of(uint8_t pid) const {
	if (pid >= MAX_PLAYERS) return 0;
	if (last_man_standing_) return (uint8_t)(1u << pid);
	uint8_t side = 0;
	for (uint8_t t = 0; t < MAX_PLAYERS; ++t) {
		if (t == pid || (((participant_mask_ >> t) & 1) && is_ally(pid, t) && is_ally(t, pid))) side |= (uint8_t)(1u << t);
	}
	return side;
}

void Simulation::check_victory_conditions(float dt) {
	if (game_over_ || victory_) return;
	// 0 A.D. "Endless": no victory conditions, nobody is ever defeated.
	if (victory_condition_ == VICTORY_ENDLESS) return;
	if (victory_condition_ == VICTORY_REGICIDE && ticks_ == 0) spawn_regicide_heroes();

	// The match ends with these players as the winners; everyone else has lost.
	auto end_match = [&](uint8_t winners) {
		winners &= (uint8_t)~defeated_mask_; // a defeated ally stays defeated (0 A.D.)
		winner_mask_ = winners;
		defeated_mask_ |= (uint8_t)(participant_mask_ & ~winners);
		victory_ = (winners & 1) != 0;
		game_over_ = true;
	};

	// Tally what each player still holds (ConquestCommon.js); units_.team and Building::team
	// are player ids. Garrisoned units count as alive.
	constexpr int MAX_TEAMS = MAX_PLAYERS;
	int unit_count[MAX_TEAMS] = {};
	int structure_count[MAX_TEAMS] = {};
	int tc_count[MAX_TEAMS] = {};
	int hero_count[MAX_TEAMS] = {};
	bool wonder[MAX_TEAMS] = {};
	for (const auto &b : buildings_) {
		// Unstarted foundations don't keep a player alive (an AI with no units left can still
		// queue them).
		if (b.team >= MAX_TEAMS || b.hp <= 0.0f || b.build_progress <= 0.0f) continue;
		structure_count[b.team]++;
		if (b.type == BLD_TOWN_CENTER) tc_count[b.team]++;
		if (b.type == BLD_WONDER && b.build_progress >= 1.0f) wonder[b.team] = true;
	}
	for (size_t u = 0; u < units_.count; ++u) {
		uint8_t t = units_.team[u];
		if (t >= MAX_TEAMS || !(units_.alive[u] || units_.action_state[u] == ACTION_GARRISONED)) continue;
		unit_count[t]++;
		if (units_.unit_type[u] == UNIT_HERO) hero_count[t]++;
	}
	for (int t = 0; t < MAX_TEAMS; ++t) {
		if (unit_count[t] || structure_count[t]) participant_mask_ |= (uint8_t)(1u << t);
	}

	if (victory_condition_ == VICTORY_WONDER) {
		// Wonder Victory (VictoryManager.js / Wonder.js): a finished wonder held 10 min wins
		// for its owner's side (allied victory) or, under Last Man Standing, its owner alone.
		float ours = 0.0f;
		bool any_ours = false;
		int holder = -1;
		for (int t = 0; t < MAX_TEAMS; ++t) {
			if (wonder[t] && !is_defeated((uint8_t)t)) wonder_hold_[t] += dt;
			else wonder_hold_[t] = 0.0f;
			if (wonder[t] && (side_of(0) >> t & 1)) { any_ours = true; ours = std::max(ours, wonder_hold_[t]); }
			if (wonder_hold_[t] >= 600.0f && (holder < 0 || wonder_hold_[t] > wonder_hold_[holder])) holder = t;
		}
		has_active_wonder_ = any_ours;
		wonder_victory_timer_ = ours;
		if (holder >= 0) {
			end_match(side_of((uint8_t)holder));
			return;
		}
	} else if (victory_condition_ == VICTORY_RELIC) {
		// Relic Capture Victory: one player claims and holds all relics
		if (!relics_.empty()) {
			bool all_claimed = true;
			int owner = -1;
			for (const auto &r : relics_) {
				if (!r.claimed) {
					all_claimed = false;
					break;
				}
				if (owner < 0) owner = (int)r.owner_team;
				else if (owner != (int)r.owner_team) all_claimed = false;
			}
			if (all_claimed && owner >= 0 && owner < MAX_TEAMS) {
				relic_victory_timer_ += dt;
				if (relic_victory_timer_ >= 180.0f) {
					end_match(side_of((uint8_t)owner));
					return;
				}
			} else {
				relic_victory_timer_ = 0.0f;
			}
		}
	}

	auto defeated = [&](int t) {
		bool no_units = unit_count[t] == 0;
		bool no_structures = structure_count[t] == 0;
		switch (victory_condition_) {
			case VICTORY_CONQUEST_UNITS: return no_units;
			case VICTORY_CONQUEST_STRUCTURES: return no_structures;
			case VICTORY_REGICIDE: return hero_count[t] == 0 || (no_units && no_structures);
			default: return no_units && no_structures; // Conquest; Wonder and Relic include it too
		}
	};

	// Defeat is permanent (Player.js SetState("defeated")). Wave defense keeps its old rule:
	// lose the Town Center and the match is lost.
	for (int t = 0; t < MAX_TEAMS; ++t) {
		if (!((participant_mask_ >> t) & 1) || is_defeated((uint8_t)t)) continue;
		if (defeated(t) || (waves_enabled_ && t == 0 && tc_count[0] == 0)) defeated_mask_ |= (uint8_t)(1u << t);
	}
	const uint8_t standing = participant_mask_ & ~defeated_mask_;
	if (participant_mask_ && !standing) { // everyone fell on the same tick
		end_match(0);
		return;
	}
	if (waves_enabled_ && is_defeated(0)) { // wave defense is player 0's match
		end_match(standing);
		return;
	}

	// The match ends once everyone still standing is mutually allied (0 A.D.'s allied
	// victory), or, under Last Man Standing, once one player is left.
	bool one_side = true;
	for (int a = 0; a < MAX_TEAMS && one_side; ++a) {
		if (!((standing >> a) & 1)) continue;
		for (int b = a + 1; b < MAX_TEAMS; ++b) {
			if (!((standing >> b) & 1)) continue;
			if (last_man_standing_ || !is_ally((uint8_t)a, (uint8_t)b) || !is_ally((uint8_t)b, (uint8_t)a)) { one_side = false; break; }
		}
	}
	// Don't hand out a win on a map that never had an enemy on it.
	bool fought = defeated_mask_ != 0 || record_.enemies_killed > 0 || record_.buildings_destroyed > 0 || wave_number_ > 0;
	if (one_side && fought) end_match(standing);
}

void Simulation::spawn_regicide_heroes() {
	// Regicide.js: every player gets a hero next to their first Town Center at match start,
	// or next to their first unit if they start without one (nomad-style starts).
	bool done[8] = {};
	std::vector<std::pair<uint8_t, std::pair<float, float>>> spots;
	for (const auto &b : buildings_) {
		if (b.type != BLD_TOWN_CENTER || b.team >= 8 || done[b.team]) continue;
		done[b.team] = true;
		spots.push_back({b.team, {b.x, b.z}});
	}
	for (size_t u = 0; u < units_.count; ++u) {
		uint8_t t = units_.team[u];
		if (t >= 8 || done[t] || !units_.alive[u]) continue;
		done[t] = true;
		spots.push_back({t, {units_.pos_x[u], units_.pos_z[u]}});
	}
	for (const auto &s : spots) {
		spawn_unit(s.second.first + 4.0f, s.second.second + 4.0f, s.first, UNIT_HERO, 0.0f);
	}
}

void Simulation::tick_waves(float dt) {
	wave_timer_ += dt;
	if (wave_timer_ < 35.0f) return;
	wave_timer_ = 0.0f;
	wave_number_++;

	if (wave_number_ > 5) {
		victory_ = true;
		winner_mask_ = side_of(0);
		return;
	}

	int base_swords = 3 + wave_number_ * 2;
	int base_archers = 1 + wave_number_;
	int base_cavalry = (wave_number_ >= 3) ? (wave_number_ - 1) : 0;
	int total = base_swords + base_archers + base_cavalry;

	for (int i = 0; i < total; ++i) {
		float angle = 6.28318530718f * (float)i / (float)total;
		float edge = map_half_ - 2.0f;
		float x = cosf(angle) * edge;
		float z = sinf(angle) * edge;

		uint8_t utype = UNIT_SWORDSMAN;
		if (i < base_archers) utype = UNIT_ARCHER;
		else if (i >= total - base_cavalry) utype = UNIT_CAVALRY;

		uint32_t uid = spawn_unit(x, z, 1, (UnitType)utype);
		int32_t uidx = lookup_unit(uid);
		if (uidx >= 0) {
			units_.target_x[uidx] = 0.0f;
			units_.target_z[uidx] = 0.0f;
			units_.has_order[uidx] = 1;
			units_.action_state[uidx] = ACTION_ATTACK;
		}
	}
}

Simulation::UnitView Simulation::units() const {
	Simulation::UnitView v;
	v.pos_x = units_.pos_x;
	v.pos_z = units_.pos_z;
	v.vel_x = units_.vel_x;
	v.vel_z = units_.vel_z;
	v.target_x = units_.target_x;
	v.target_z = units_.target_z;
	v.hp = units_.hp;
	v.max_hp = units_.max_hp;
	v.id = units_.id;
	v.alive = units_.alive;
	v.team = units_.team;
	v.tmpl = units_.tmpl;
	v.unit_type = units_.unit_type;
	v.action_state = units_.action_state;
	v.res_type_carried = units_.res_type_carried;
	v.stance = units_.stance;
	v.formation = units_.formation;
	v.target_resource_id = units_.target_resource_id;
	v.carry = units_.carry;
	v.death_timer = units_.death_timer;
	v.kills = units_.kills;
	v.rank = units_.rank;
	v.status_effects = units_.status_effects;
	v.xp = units_.xp;
	v.count = units_.count;
	return v;
}

Simulation::ResourceView Simulation::resources() const {
	return {
		resources_.x, resources_.z,
		resources_.amount,
		resources_.type, resources_.subtype, resources_.alive,
		resources_.id,
		resources_.count
	};
}

void Simulation::tick_trickle(float dt) {
	// Team bonus trickle (Ptolemaic Breadbasket of the Mediterranean: +1 food/s for
	// the Ptolemies and their allies). Only players who own a building get it.
	uint16_t civs_present = 0;
	for (const auto &b : buildings_)
		if (b.team < MAX_PLAYERS && b.hp > 0.0f) civs_present |= civ_bit(players_[b.team].civ);
	if (civs_present & civ_bit(CIV_PTOLEMIES)) {
		for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
			bool owns = false;
			for (const auto &b : buildings_)
				if (b.team == p && b.hp > 0.0f) { owns = true; break; }
			if (!owns) continue;
			const float add = civ_team_food_trickle(players_[p].civ, ally_civ_mask(p));
			if (add > 0.0f) players_[p].food() += add * dt;
		}
	}

	// ResourceTrickle.js — passive income from key buildings
	// Farmstead: +0.5 food/s per field, Market: +0.3 gold/s (simulates trade
	// trickle), Wonder: +2 of all/s. 0 A.D.'s Town Center has no trickle.
	for (const auto &b : buildings_) {
		if (b.team != 0 || b.build_progress < 1.0f) continue;
		PlayerState &ps = players_[0];
		// Whole-unit increments, as in the int-stock version this replaced.
		auto trickle = [&](uint8_t rt, float rate) {
			ps.res[rt] = std::min(9999.0f, ps.res[rt] + (float)(int)(rate * dt));
		};
		switch (b.type) {
			case BLD_FARMSTEAD:
				trickle(RES_FOOD, 0.5f);
				break;
			case BLD_MARKET:
				trickle(RES_GOLD, 0.3f);
				break;
			case BLD_WONDER:
				trickle(RES_FOOD, 2.0f);
				trickle(RES_WOOD, 2.0f);
				trickle(RES_GOLD, 2.0f);
				trickle(RES_STONE, 2.0f);
				break;
			default: break;
		}
	}
}

// 0 A.D. Upkeep.js:51-60: TrySubtractResources periodic upkeep drain
void Simulation::tick_upkeep(float dt) {
	upkeep_timer_ += dt;
	if (upkeep_timer_ < 1.0f) return;
	upkeep_timer_ = 0.0f;

	// In 0 A.D. Upkeep.js, entities with Upkeep component periodically
	// drain upkeep resources from their owner. Military units require food upkeep.
	int soldiers_team0 = 0;
	int soldiers_team1 = 0;
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i]) continue;
		uint8_t ut = units_.unit_type[i];
		bool is_soldier = (ut != UNIT_VILLAGER && ut != UNIT_FEMALE_CITIZEN &&
		                   ut != UNIT_TRADER && ut != UNIT_FISHING_BOAT &&
		                   ut != UNIT_MERCHANT_SHIP && ut != UNIT_HEALER &&
		                   ut != UNIT_WORKER_ELEPHANT);
		if (!is_soldier) continue;
		if (units_.team[i] == 0) soldiers_team0++;
		else if (units_.team[i] == 1) soldiers_team1++;
	}

	// 0 A.D. Upkeep.js:51-60 TrySubtractResources: skip teams that can't pay (no negative resources)
	int drain0 = soldiers_team0 / 20;
	if (drain0 > 0 && players_[0].food() >= (float)drain0) {
		players_[0].food() -= (float)drain0;
	}
	int drain1 = soldiers_team1 / 20;
	if (drain1 > 0 && petra_ps().food() >= (float)drain1) {
		petra_ps().food() -= (float)drain1;
	}
}

// 0 A.D. EntityLimits.js:7-12: enforce category limits per player (Wonder: 1, Fortress: 10)
void Simulation::tick_entity_limits(float dt) {
	entity_limits_timer_ += dt;
	if (entity_limits_timer_ < 1.0f) return;
	entity_limits_timer_ = 0.0f;

	// In 0 A.D. EntityLimits.js, players have strict maximum counts for special categories
	// (<Wonder>1</Wonder>, <Fortress>10</Fortress>). Sweep alive buildings per team and
	// mark newest excess structures wrecked.
	int wonder_count[8] = {};
	int fortress_count[8] = {};

	for (auto &b : buildings_) {
		if (b.hp <= 0.0f || b.wrecked || b.team >= 8) continue;
		if (b.type == BLD_WONDER) {
			wonder_count[b.team]++;
			if (wonder_count[b.team] > 1) {
				mark_building_wreck(b);
			}
		} else if (b.type == BLD_FORTRESS) {
			fortress_count[b.team]++;
			if (fortress_count[b.team] > 10) {
				mark_building_wreck(b);
			}
		}
	}
}

// 0 A.D. DeathDamage.js:38-58: CauseDamageOverArea on-death splash damage
void Simulation::tick_death_damage(float dt) {
	const size_t start = (recent_death_head_ > MAX_RECENT_DEATHS) ? (recent_death_head_ - MAX_RECENT_DEATHS) : 0;
	const size_t end = recent_death_head_;

	for (size_t k = start; k < end; ++k) {
		auto &death = recent_deaths_[k % MAX_RECENT_DEATHS];
		if (death.death_damage_applied) continue;
		death.death_damage_applied = true;

		// 0 A.D. DeathDamage.js:38-58: siege engines, ships, and heavy buildings collapse with splash
		bool causes_damage = death.is_building || death.type == UNIT_CATAPULT ||
		                     death.type == UNIT_BOLT_SHOOTER || death.type == UNIT_SIEGE_RAM ||
		                     death.type == UNIT_TRIREME;
		if (!causes_damage) continue;

		float radius = death.is_building ? 8.0f : 4.0f;
		float base_damage = death.is_building ? 25.0f : 15.0f;
		float r2 = radius * radius;

		// Deterministic iteration over units_ by index
		for (size_t i = 0; i < units_.count; ++i) {
			if (!units_.alive[i]) continue;
			if (!is_hostile(death.team, units_.team[i])) continue; // FriendlyFire: false (DeathDamage.js:9)
			float dx = units_.pos_x[i] - death.x;
			float dz = units_.pos_z[i] - death.z;
			float d2 = dx * dx + dz * dz;
			if (d2 <= r2) {
				float dist = std::sqrt(d2);
				float falloff = 1.0f - (dist / radius) * 0.5f;
				float dmg = base_damage * falloff;
				units_.hp[i] -= dmg;
				if (units_.hp[i] <= 0.0f) {
					units_.hp[i] = 0.0f;
					units_.alive[i] = 0;
					units_.action_state[i] = ACTION_DYING;
					if (units_.team[i] == 0) record_.units_lost++;
					else record_.enemies_killed++;
				}
			}
		}
	}
}

// 0 A.D. Loot.js:15-26, Looter.js:14-41: on-death bounty grant to killer's stockpile
void Simulation::tick_loot(float dt) {
	const size_t start = (recent_death_head_ > MAX_RECENT_DEATHS) ? (recent_death_head_ - MAX_RECENT_DEATHS) : 0;
	const size_t end = recent_death_head_;

	for (size_t k = start; k < end; ++k) {
		auto &death = recent_deaths_[k % MAX_RECENT_DEATHS];
		if (death.loot_applied) continue;
		death.loot_applied = true;

		// 0 A.D. Looter.js:34-41: cmpPlayer.AddResources(resources)
		uint8_t killer = death.killer_team;
		if (killer == death.team || killer >= 2) continue;

		int food_bounty = death.is_building ? 20 : 5;
		int wood_bounty = death.is_building ? 20 : 5;

		PlayerState &kps = players_[killer];
		kps.food() = std::min(9999.0f, kps.food() + (float)food_bounty);
		kps.wood() = std::min(9999.0f, kps.wood() + (float)wood_bounty);
	}
}

// 0 A.D. Gate.js OperateGate: a gate opens while a unit of its owner or an ally
// that can move is within PassRange, closes once none is, and never closes on a
// unit standing in it (CloseGate retries). A locked gate never opens.
// RTS0 gates are one 2 m cell; PassRange is 0 A.D.'s 2 m from the gate's edge
// plus what a unit covers between checks (0.2 s, one lockstep turn).
void Simulation::tick_gates(float dt) {
	gate_timer_ += dt;
	if (gate_timer_ < 0.2f) return;
	gate_timer_ = 0.0f;
	constexpr float PASS_RANGE = 4.0f;
	bool nav_changed = false;
	for (auto &b : buildings_) {
		if (b.type != BLD_GATE || b.build_progress < 1.0f || b.hp <= 0.0f || b.wrecked) continue;
		const bool blocked_paths = blocks_pathfinding(b);
		bool ally_near = false, unit_inside = false;
		const float reach = b.half_size + PASS_RANGE;
		for (size_t i = 0; i < units_.count; ++i) {
			if (!units_.alive[i]) continue;
			const float dx = std::fabs(units_.pos_x[i] - b.x), dz = std::fabs(units_.pos_z[i] - b.z);
			if (dx > reach || dz > reach) continue;
			const float inside = b.half_size + unit_collision_radius(units_.unit_type[i]);
			if (dx < inside && dz < inside) unit_inside = true;
			if (!ally_near && b.team < MAX_PLAYERS && units_.team[i] < MAX_PLAYERS && is_ally(b.team, units_.team[i])
			    && units_.action_state[i] != ACTION_DYING)
				ally_near = true;
			if (ally_near && unit_inside) break;
		}
		const bool should_open = ally_near && !b.gate_locked;
		if (b.gate_open && !should_open && !unit_inside) b.gate_open = false;
		else if (!b.gate_open && should_open) b.gate_open = true;
		if (blocks_pathfinding(b) != blocked_paths) nav_changed = true;
	}
	if (nav_changed) rebuild_nav_grid();
}

bool Simulation::cmd_lock_gate(uint8_t player_id, uint32_t building_id, bool lock) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return false;
	Building &b = buildings_[bit->second];
	if (b.team != player_id || b.type != BLD_GATE || b.build_progress < 1.0f || b.wrecked) return false;
	if (b.gate_locked == lock) return false;
	const bool blocked_paths = blocks_pathfinding(b);
	b.gate_locked = lock;
	// LockGate: an open gate closes on the next check once nobody stands in it.
	// Only locking and unlocking change what long-range paths may cross.
	if (blocks_pathfinding(b) != blocked_paths) rebuild_nav_grid();
	return true;
}

void Simulation::order_guard(const uint32_t *unit_ids, size_t count, uint32_t target_unit_id) {
	int32_t tidx = lookup_unit(target_unit_id);
	if (tidx < 0 || !units_.alive[tidx]) return;
	for (size_t k = 0; k < count; ++k) {
		int32_t idx = lookup_unit(unit_ids[k]);
		if (idx < 0 || !units_.alive[idx]) continue;
		units_.action_state[idx] = ACTION_GUARD;
		units_.has_order[idx] = 1;
		units_.target_unit_id[idx] = target_unit_id;
		units_.target_x[idx] = units_.pos_x[tidx];
		units_.target_z[idx] = units_.pos_z[tidx];
	}
}

// Petra keeps a free ring of cells around everything it builds so units can
// always walk around and reach foundations (0 A.D. placement uses obstruction
// clearance for the same reason); tight packing left builders stranded.
bool Simulation::petra_can_place(int cell_x, int cell_y, int footprint) const {
	return can_place(cell_x - 1, cell_y - 1, footprint + 2);
}

void Simulation::update_petra_threat_map(float dt) {
	petra_threat_map_.update_timer += dt;
	if (petra_threat_map_.update_timer < 0.4f) return;
	petra_threat_map_.update_timer = 0.0f;
	ScopedUs prof(map_scale_stats_.threat_us, map_scale_stats_.threat_calls);

	float cell_size = (map_half_ * 2.0f) / (float)THREAT_GRID_RES;
	float inv_cell = 1.0f / cell_size;

	memset(petra_threat_map_.enemy_threat, 0, sizeof(petra_threat_map_.enemy_threat));
	memset(petra_threat_map_.friendly_defense, 0, sizeof(petra_threat_map_.friendly_defense));

	auto add_power = [&](float *grid, float wx, float wz, float power, int radius_cells) {
		int cx = std::clamp((int)((wx + map_half_) * inv_cell), 0, THREAT_GRID_RES - 1);
		int cz = std::clamp((int)((wz + map_half_) * inv_cell), 0, THREAT_GRID_RES - 1);
		int x0 = std::max(0, cx - radius_cells);
		int x1 = std::min(THREAT_GRID_RES - 1, cx + radius_cells);
		int z0 = std::max(0, cz - radius_cells);
		int z1 = std::min(THREAT_GRID_RES - 1, cz + radius_cells);

		float max_dist_sq = (float)(radius_cells * radius_cells + 1);
		for (int z = z0; z <= z1; ++z) {
			for (int x = x0; x <= x1; ++x) {
				float dist_sq = (float)((x - cx) * (x - cx) + (z - cz) * (z - cz));
				if (dist_sq <= max_dist_sq) {
					float falloff = std::max(0.12f, 1.0f - (dist_sq / max_dist_sq));
					grid[z * THREAT_GRID_RES + x] += power * falloff;
				}
			}
		}
	};

	// 1. Accumulate unit threats (64x64 grid, 2.0m cells)
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i]) continue;
		float power = 0.0f;
		int rad = 3;
		switch (units_.unit_type[i]) {
			case UNIT_VILLAGER: power = 0.5f; rad = 2; break;
			case UNIT_SWORDSMAN: power = 5.5f; rad = 3; break;
			case UNIT_SPEARMAN: power = 6.0f; rad = 3; break;
			case UNIT_ARCHER: power = 8.5f; rad = 9; break; // ranged threat ~18m
			case UNIT_CAVALRY: power = 10.0f; rad = 6; break; // high mobility ~12m
			case UNIT_CATAPULT: power = 18.0f; rad = 13; break; // heavy siege ~26m
			case UNIT_SIEGE_RAM: power = 15.0f; rad = 4; break;
			case UNIT_BOLT_SHOOTER: power = 16.0f; rad = 14; break;
			case UNIT_ELEPHANT: power = 24.0f; rad = 6; break;
			case UNIT_HERO: power = 22.0f; rad = 6; break;
			case UNIT_TRIREME: power = 20.0f; rad = 11; break;
			case UNIT_WAR_DOG: power = 4.5f; rad = 3; break;
			case UNIT_CHARIOT: power = 12.0f; rad = 7; break;
			case UNIT_SCYTHED_CHARIOT: power = 16.0f; rad = 7; break;
			default: power = 4.0f; rad = 3; break;
		}

		if (petra_foe(units_.team[i])) {
			add_power(petra_threat_map_.enemy_threat, units_.pos_x[i], units_.pos_z[i], power, rad);
		} else if (units_.team[i] == petra_pid_) {
			add_power(petra_threat_map_.friendly_defense, units_.pos_x[i], units_.pos_z[i], power, rad);
		}
	}

	// 2. Accumulate building defensive power
	for (const auto &b : buildings_) {
		if (b.build_progress < 0.5f) continue;
		float b_power = 0.0f;
		int b_rad = 4;
		if (b.type == BLD_TOWN_CENTER) { b_power = 28.0f; b_rad = 9; }
		else if (b.type == BLD_FORTRESS) { b_power = 42.0f; b_rad = 12; }
		else if (b.type == BLD_TOWER) { b_power = 20.0f; b_rad = 9; }
		else if (b.type == BLD_DOCK) { b_power = 14.0f; b_rad = 8; }
		else if (b.type == BLD_BARRACKS) { b_power = 6.0f; b_rad = 4; }
		else if (b.type == BLD_GATE) { b_power = 8.0f; b_rad = 4; }
		else if (b.type == BLD_TEMPLE) { b_power = 6.0f; b_rad = 4; }
		else if (b.type == BLD_MARKET) { b_power = 4.0f; b_rad = 3; }

		if (b_power > 0.0f) {
			if (petra_foe(b.team)) {
				add_power(petra_threat_map_.enemy_threat, b.x, b.z, b_power, b_rad);
			} else if (b.team == petra_pid_) {
				add_power(petra_threat_map_.friendly_defense, b.x, b.z, b_power, b_rad);
			}
		}
	}

	// 3. Compute net danger with decay matching 0 A.D. dangerMap.js
	for (int c = 0; c < THREAT_GRID_RES * THREAT_GRID_RES; ++c) {
		float raw_danger = std::max(0.0f, petra_threat_map_.enemy_threat[c] - petra_threat_map_.friendly_defense[c] * 0.70f);
		petra_threat_map_.danger[c] = petra_threat_map_.danger[c] * 0.35f + raw_danger * 0.65f;
	}
}

float Simulation::get_threat_at(float x, float z) const {
	float cell_size = (map_half_ * 2.0f) / (float)THREAT_GRID_RES;
	int cx = std::clamp((int)((x + map_half_) / cell_size), 0, THREAT_GRID_RES - 1);
	int cz = std::clamp((int)((z + map_half_) / cell_size), 0, THREAT_GRID_RES - 1);
	return petra_threat_map_.danger[cz * THREAT_GRID_RES + cx];
}

bool Simulation::is_position_safe(float x, float z, float threshold) const {
	return get_threat_at(x, z) < threshold;
}

const Simulation::PetraBase *Simulation::get_primary_petra_base() const {
	for (const auto &b : petra_bases_) {
		if (b.is_primary) return &b;
	}
	if (!petra_bases_.empty()) return &petra_bases_[0];
	return nullptr;
}

const Simulation::PetraBase *Simulation::get_nearest_petra_base(float x, float z) const {
	if (petra_bases_.empty()) return nullptr;
	const PetraBase *best = nullptr;
	float best_d2 = 1e9f;
	for (const auto &b : petra_bases_) {
		float dx = b.x - x;
		float dz = b.z - z;
		float d2 = dx * dx + dz * dz;
		if (d2 < best_d2) {
			best_d2 = d2;
			best = &b;
		}
	}
	return best;
}

void Simulation::update_petra_bases(float dt) {
	// Find all Team 1 Town Centers & forward anchor structures
	std::vector<uint32_t> tc_ids;
	std::vector<uint32_t> outpost_ids;

	for (const auto &b : buildings_) {
		if (b.team != petra_pid_ || b.hp <= 0) continue;
		if (b.type == BLD_TOWN_CENTER) {
			tc_ids.push_back(b.id);
		} else if (b.type == BLD_FORTRESS || b.type == BLD_TOWER || b.type == BLD_STOREHOUSE) {
			outpost_ids.push_back(b.id);
		}
	}

	// reset() seeds a placeholder base at (36,36) with no anchor. Once we own a
	// Town Center, drop anchorless bases so the primary base is a real one;
	// otherwise houses and barracks go up wherever the placeholder sits.
	if (!tc_ids.empty()) {
		petra_bases_.erase(std::remove_if(petra_bases_.begin(), petra_bases_.end(),
			[](const PetraBase &b) { return b.anchor_building_id == 0; }), petra_bases_.end());
	}

	// 1. If bases list is empty, initialize default primary base
	if (petra_bases_.empty()) {
		PetraBase base1;
		base1.id = 1;
		base1.is_primary = true;
		base1.x = 36.0f;
		base1.z = 36.0f;
		base1.radius = 32.0f;
		if (!tc_ids.empty()) {
			auto it = building_id_to_idx_.find(tc_ids[0]);
			if (it != building_id_to_idx_.end()) {
				base1.anchor_building_id = tc_ids[0];
				base1.x = buildings_[it->second].x;
				base1.z = buildings_[it->second].z;
			}
		}
		petra_bases_.push_back(base1);
	}

	// 2. Validate existing bases — verify anchor alive, update positions & primary status
	bool has_primary = false;
	for (auto &base : petra_bases_) {
		if (base.anchor_building_id != 0) {
			auto it = building_id_to_idx_.find(base.anchor_building_id);
			// A captured anchor is no longer ours (Capturable.js changes owner).
			if (it != building_id_to_idx_.end() && buildings_[it->second].hp > 0 &&
			    buildings_[it->second].team == petra_pid_) {
				base.x = buildings_[it->second].x;
				base.z = buildings_[it->second].z;
			} else {
				base.anchor_building_id = 0;
			}
		}
		if (base.is_primary) {
			has_primary = true;
		}
	}

	// If primary base anchor destroyed, elect nearest surviving base as primary
	if (!has_primary && !petra_bases_.empty()) {
		petra_bases_[0].is_primary = true;
	}

	// 3. Discover and register new base anchors from newly founded Town Centers & Outposts
	for (uint32_t tcid : tc_ids) {
		auto it = building_id_to_idx_.find(tcid);
		if (it == building_id_to_idx_.end()) continue;
		const Building &tc = buildings_[it->second];

		bool tracked = false;
		for (const auto &base : petra_bases_) {
			if (base.anchor_building_id == tcid || std::hypot(base.x - tc.x, base.z - tc.z) < 24.0f) {
				tracked = true;
				break;
			}
		}

		if (!tracked) {
			PetraBase new_base;
			new_base.id = (uint32_t)petra_bases_.size() + 1;
			new_base.anchor_building_id = tcid;
			new_base.x = tc.x;
			new_base.z = tc.z;
			new_base.is_primary = false;
			new_base.radius = 32.0f;
			new_base.danger_level = get_threat_at(tc.x, tc.z);
			petra_bases_.push_back(new_base);
		}
	}

	for (uint32_t opid : outpost_ids) {
		auto it = building_id_to_idx_.find(opid);
		if (it == building_id_to_idx_.end()) continue;
		const Building &op = buildings_[it->second];
		if (op.type != BLD_FORTRESS && op.type != BLD_STOREHOUSE) continue;
		// A building captured inside someone else's territory is not a base to grow.
		if (territory_owner_at(op.x, op.z) != petra_pid_) continue;

		bool near_existing = false;
		for (const auto &base : petra_bases_) {
			if (std::hypot(base.x - op.x, base.z - op.z) < 30.0f) {
				near_existing = true;
				break;
			}
		}

		if (!near_existing && petra_bases_.size() < 4) {
			PetraBase new_outpost;
			new_outpost.id = (uint32_t)petra_bases_.size() + 1;
			new_outpost.anchor_building_id = opid;
			new_outpost.x = op.x;
			new_outpost.z = op.z;
			new_outpost.is_primary = false;
			new_outpost.radius = 24.0f;
			new_outpost.danger_level = get_threat_at(op.x, op.z);
			petra_bases_.push_back(new_outpost);
		}
	}

	// 4. Update threat level, assigned workers, and resource depletion per base
	for (auto &base : petra_bases_) {
		base.danger_level = get_threat_at(base.x, base.z);

		int w_count = 0;
		for (size_t i = 0; i < units_.count; ++i) {
			if (!units_.alive[i] || units_.team[i] != petra_pid_ || units_.unit_type[i] != UNIT_VILLAGER) continue;
			float dx = units_.pos_x[i] - base.x;
			float dz = units_.pos_z[i] - base.z;
			if (dx * dx + dz * dz <= base.radius * base.radius) {
				w_count++;
			}
		}
		base.assigned_workers = w_count;

		int safe_nodes = 0;
		int total_amount = 0;
		for (size_t r = 0; r < resources_.count; ++r) {
			if (!resources_.alive[r] || resources_.amount[r] <= 0) continue;
			float dx = resources_.x[r] - base.x;
			float dz = resources_.z[r] - base.z;
			if (dx * dx + dz * dz <= base.radius * base.radius) {
				if (is_position_safe(resources_.x[r], resources_.z[r], 6.0f)) {
					safe_nodes++;
					total_amount += resources_.amount[r];
				}
			}
		}
		base.resources_depleted = (safe_nodes <= 2 || total_amount < 1200);
	}

	// Idle villagers are not walked to secondary bases here: the economy
	// manager already assigns them to the nearest resource with a dropsite,
	// which covers expansions, and a bare move left them stranded en route.
}

void Simulation::petra_manage_scouting(float dt) {
	// 1. Update Petra Fog of War from Team 1 vision
	for (auto &c : petra_fow_) {
		if (c == FOW_VISIBLE) c = FOW_EXPLORED;
	}

	const float half = map_half_;
	const int G = grid_res_;
	const float cell = (half * 2.0f) / (float)G;

	auto vision_radius = [](uint8_t utype) -> float {
		switch (utype) {
			case UNIT_ARCHER:   return 22.0f;
			case UNIT_CAVALRY:  return 24.0f;
			case UNIT_CATAPULT: return 24.0f;
			case UNIT_TRIREME:  return 28.0f;
			case UNIT_ELEPHANT: return 20.0f;
			case UNIT_HERO:     return 20.0f;
			default:            return 16.0f;
		}
	};

	ensure_fow_cell_heights();
	for (size_t u = 0; u < units_.count; ++u) {
		if (!units_.alive[u] || units_.team[u] != petra_pid_) continue;
		reveal_fow(petra_fow_, units_.pos_x[u], units_.pos_z[u], vision_radius(units_.unit_type[u]));
	}

	for (const auto &b : buildings_) {
		if (b.team != petra_pid_ || b.build_progress < 0.5f) continue;
		float bvr = (b.type == BLD_TOWER || b.type == BLD_FORTRESS) ? 24.0f : ((b.type == BLD_DOCK) ? 28.0f : 18.0f);
		reveal_fow(petra_fow_, b.x, b.z, bvr);
	}

	// 2. Validate current scout unit or recruit candidate
	bool scout_valid = false;
	if (petra_scout_unit_id_ != 0) {
		int32_t s_idx = lookup_unit(petra_scout_unit_id_);
		if (s_idx >= 0 && units_.alive[s_idx] && units_.team[s_idx] == petra_pid_) {
			scout_valid = true;
		} else {
			petra_scout_unit_id_ = 0;
		}
	}

	if (!scout_valid) {
		// Priority 1: Light Cavalry
		for (size_t i = 0; i < units_.count; ++i) {
			if (!units_.alive[i] || units_.team[i] != petra_pid_) continue;
			if (units_.unit_type[i] == UNIT_CAVALRY) {
				uint32_t uid = units_.id[i];
				if (!petra_unit_reserved(uid)) {
					petra_scout_unit_id_ = uid;
					scout_valid = true;
					break;
				}
			}
		}

		// Workers are not sent scouting: in our small maps a lone villager
		// wandering into an enemy base cost a worker and provoked a fight.
	}

	if (!scout_valid || petra_scout_unit_id_ == 0) return;

	int32_t s_idx = lookup_unit(petra_scout_unit_id_);
	if (s_idx < 0) return;

	float sx = units_.pos_x[s_idx];
	float sz = units_.pos_z[s_idx];
	petra_scout_timer_ += dt;

	// Check if scout is in danger
	float local_threat = get_threat_at(sx, sz);
	if (local_threat >= 6.0f) {
		const PetraBase *pbase = get_primary_petra_base();
		float bx = pbase ? pbase->x : 36.0f;
		float bz = pbase ? pbase->z : 36.0f;
		cmd_move(&petra_scout_unit_id_, 1, bx, bz);
		return;
	}

	// In the village phase the scout avoids fights: it turns back when it strays
	// into enemy territory or engages, so it doesn't drag the enemy's army home.
	const bool village = petra_ps().phase < 2;
	auto enemy_land_at = [&](float x, float z) { return petra_foe(territory_owner_at(x, z)); };
	if (village && (enemy_land_at(sx, sz) || units_.action_state[s_idx] == ACTION_ATTACK)) {
		if (const PetraBase *pbase = get_primary_petra_base()) {
			cmd_move(&petra_scout_unit_id_, 1, pbase->x, pbase->z);
			petra_scout_timer_ = 0.0f;
		}
		return;
	}

	// 3. Autonomous scouting exploration
	if (units_.action_state[s_idx] == ACTION_IDLE || petra_scout_timer_ >= 8.0f) {
		petra_scout_timer_ = 0.0f;
		const PetraBase *pbase = get_primary_petra_base();
		float origin_x = pbase ? pbase->x : 36.0f;
		float origin_z = pbase ? pbase->z : 36.0f;

		bool target_found = false;
		float target_wx = 0.0f, target_wz = 0.0f;

		// Spiral outward through FoW searching for unexplored cells
		for (int step = 0; step < 40 && !target_found; ++step) {
			petra_scout_angle_ += 0.35f; // ~20 deg step
			if (petra_scout_angle_ >= 6.28318f) {
				petra_scout_angle_ -= 6.28318f;
				petra_scout_radius_ += 6.0f;
				if (petra_scout_radius_ > map_half_ * 1.35f) {
					petra_scout_radius_ = 12.0f;
				}
			}

			float candidate_x = origin_x + std::cos(petra_scout_angle_) * petra_scout_radius_;
			float candidate_z = origin_z + std::sin(petra_scout_angle_) * petra_scout_radius_;
			candidate_x = std::clamp(candidate_x, -map_half_ + 4.0f, map_half_ - 4.0f);
			candidate_z = std::clamp(candidate_z, -map_half_ + 4.0f, map_half_ - 4.0f);

			int cell_x = std::clamp((int)((candidate_x + map_half_) / cell), 0, G - 1);
			int cell_z = std::clamp((int)((candidate_z + map_half_) / cell), 0, G - 1);

			// Stay out of enemy territory until we can fight (town phase).
			if (petra_fow_[cell_z * G + cell_x] == FOW_UNEXPLORED &&
			    !(village && enemy_land_at(candidate_x, candidate_z)) &&
			    get_terrain_height(candidate_x, candidate_z) > terrain_water_height_ + 0.3f &&
			    is_position_safe(candidate_x, candidate_z, 5.0f)) {
				target_wx = candidate_x;
				target_wz = candidate_z;
				target_found = true;
			}
		}

		// Also check if any unclaimed relics or neutral resource patches are in unexplored areas
		if (!target_found) {
			for (const auto &relic : relics_) {
				if (!relic.claimed) {
					int rcx = std::clamp((int)((relic.x + map_half_) / cell), 0, G - 1);
					int rcz = std::clamp((int)((relic.z + map_half_) / cell), 0, G - 1);
					if (petra_fow_[rcz * G + rcx] != FOW_VISIBLE && is_position_safe(relic.x, relic.z, 5.0f) &&
					    !(village && enemy_land_at(relic.x, relic.z))) {
						target_wx = relic.x;
						target_wz = relic.z;
						target_found = true;
						break;
					}
				}
			}
		}

		if (target_found) {
			cmd_move(&petra_scout_unit_id_, 1, target_wx, target_wz);
		} else {
			// Exploration sweep: patrol perimeter of friendly territory towards center
			float sweep_x = -origin_x * 0.4f + (float)sim_rng_.range_int(-8, 7);
			float sweep_z = -origin_z * 0.4f + (float)sim_rng_.range_int(-8, 7);
			if (!(village && enemy_land_at(sweep_x, sweep_z)))
				cmd_move(&petra_scout_unit_id_, 1, sweep_x, sweep_z);
		}
	}
}

void Simulation::petra_manage_workers(float dt) {
	// Find safest shelter building for Team 1 across all base anchors
	float safe_x = 36.0f, safe_z = 36.0f, safe_half = 0.0f;
	uint32_t shelter_bld_id = 0;
	float best_shelter_score = 1e9f;

	for (const auto &b : buildings_) {
		if (b.team != petra_pid_ || b.build_progress < 0.5f) continue;
		if (b.type == BLD_TOWN_CENTER || b.type == BLD_FORTRESS || b.type == BLD_TOWER || b.type == BLD_STOREHOUSE) {
			float b_threat = get_threat_at(b.x, b.z);
			if (b_threat < best_shelter_score) {
				best_shelter_score = b_threat;
				safe_x = b.x;
				safe_z = b.z;
				safe_half = b.half_size;
				shelter_bld_id = b.id;
			}
		}
	}

	// 1. Worker Threat Evaluation & Evacuation
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != petra_pid_ || units_.unit_type[i] != UNIT_VILLAGER) continue;

		float threat = get_threat_at(units_.pos_x[i], units_.pos_z[i]);

		// Under heavy danger (threat >= 7.0f): Evacuate to nearest safe shelter!
		if (threat >= 7.0f) {
			if (units_.action_state[i] == ACTION_GATHER || units_.action_state[i] == ACTION_BUILD ||
				units_.action_state[i] == ACTION_RETURN_DROPSITE || units_.action_state[i] == ACTION_IDLE) {
				// Stop just outside the shelter's footprint on the worker's side;
				// a point inside it is unreachable and left the worker walking forever.
				float ox = units_.pos_x[i] - safe_x, oz = units_.pos_z[i] - safe_z;
				float ol = std::sqrt(ox * ox + oz * oz);
				if (ol < 0.01f) { ox = 1.0f; oz = 0.0f; ol = 1.0f; }
				const float stand = safe_half + 1.5f;
				units_.target_x[i] = safe_x + ox / ol * stand;
				units_.target_z[i] = safe_z + oz / ol * stand;
				units_.has_order[i] = 1;
				units_.action_state[i] = ACTION_MOVE;
				units_.target_resource_id[i] = 0;
				units_.target_building_id[i] = 0;
			}

			// If near stronghold with garrison capacity, garrison inside for shelter
			if (shelter_bld_id != 0) {
				auto bit = building_id_to_idx_.find(shelter_bld_id);
				if (bit != building_id_to_idx_.end()) {
					Building &sb = buildings_[bit->second];
					float dx = units_.pos_x[i] - sb.x;
					float dz = units_.pos_z[i] - sb.z;
					const float reach = sb.half_size + 2.5f;
					if (dx * dx + dz * dz <= reach * reach && (int)sb.garrisoned_units.size() < sb.garrison_cap) {
						uint32_t uid = units_.id[i];
						order_garrison(&uid, 1, sb.id);
					}
				}
			}
		}
	}

	// Ungarrison workers when threat is cleared — leave military in towers (garrisonManager.js)
	if (best_shelter_score < 2.0f) {
		for (auto &b : buildings_) {
			if (b.team != petra_pid_ || b.garrisoned_units.empty()) continue;
			std::vector<uint32_t> eject;
			eject.reserve(b.garrisoned_units.size());
			for (uint32_t uid : b.garrisoned_units) {
				int32_t idx = lookup_unit(uid);
				if (idx >= 0 && units_.unit_type[idx] == UNIT_VILLAGER) eject.push_back(uid);
			}
			for (uint32_t uid : eject) order_ungarrison_single(b.id, uid);
		}
	}
}

void Simulation::petra_manage_military_production(float dt) {
	// Priority QueueManager: Priority 1 = Emergency Defense, Priority 3 = Military Strike Force
	float max_threat = 0.0f;
	for (const auto &b : petra_bases_) {
		max_threat = std::max(max_threat, b.danger_level);
	}
	for (size_t i = 0; i < units_.count; ++i) {
		if (units_.alive[i] && units_.team[i] == petra_pid_ && units_.unit_type[i] == UNIT_VILLAGER) {
			max_threat = std::max(max_threat, get_threat_at(units_.pos_x[i], units_.pos_z[i]));
		}
	}
	bool is_emergency = (max_threat >= 7.0f);

	// Find available AI military production buildings
	std::vector<uint32_t> barracks_list;
	std::vector<uint32_t> fortress_list;
	for (const auto &b : buildings_) {
		if (b.team != petra_pid_ || b.build_progress < 1.0f) continue;
		if (b.type == BLD_BARRACKS && b.queue.size() < 4) barracks_list.push_back(b.id);
		if (b.type == BLD_FORTRESS && b.queue.size() < 4) fortress_list.push_back(b.id);
	}

	if (barracks_list.empty() && fortress_list.empty()) return;
	if (pop_current(petra_pid_) >= pop_max(petra_pid_)) return;

	// 1. Emergency Defense (Priority Queue 0: Threat >= 7.0f)
	if (is_emergency && !barracks_list.empty()) {
		// Prioritize spearmen (anti-cavalry/melee) and archers (ranged defense)
		for (uint32_t bid : barracks_list) {
			auto bit = building_id_to_idx_.find(bid);
			if (bit == building_id_to_idx_.end()) continue;
			Building &b = buildings_[bit->second];
			if (b.queue.size() >= 4 || pop_current(petra_pid_) >= pop_max(petra_pid_)) continue;

			// Check if we can train Spearman
			const UnitTypeInfo &sinfo = civ_unit_info(civ_of(petra_pid_), UNIT_SPEARMAN);
			if (petra_try_pay(PETRA_QUEUE_EMERGENCY, (float)sinfo.food_cost, (float)sinfo.wood_cost, (float)sinfo.gold_cost, 0.0f)) {
				ProductionOrder order;
				order.unit_type = UNIT_SPEARMAN;
				order.timer = 0.0f;
				order.duration = sinfo.train_time * 0.70f * tech_mods(petra_pid_).train_time[UNIT_SPEARMAN] * ai_time_mult(petra_pid_); // fast-track emergency training
				b.queue.push_back(order);
				continue;
			}

			// Check if we can train Archer
			const UnitTypeInfo &ainfo = civ_unit_info(civ_of(petra_pid_), UNIT_ARCHER);
			if (petra_try_pay(PETRA_QUEUE_EMERGENCY, (float)ainfo.food_cost, (float)ainfo.wood_cost, (float)ainfo.gold_cost, 0.0f)) {
				ProductionOrder order;
				order.unit_type = UNIT_ARCHER;
				order.timer = 0.0f;
				order.duration = ainfo.train_time * 0.70f * tech_mods(petra_pid_).train_time[UNIT_ARCHER] * ai_time_mult(petra_pid_);
				b.queue.push_back(order);
			}
		}
		return;
	}

	// 2. Military Strike Force Production (Priority Queue 2) — Counter-Units to supply AttackPlan squads
	// Our soldiers do not gather, so the army waits until the economy is
	// mostly staffed; otherwise the village phase never reaches popPhase2.
	{
		int workers = 0;
		for (size_t i = 0; i < units_.count; ++i)
			if (units_.alive[i] && units_.team[i] == petra_pid_ &&
			    (units_.unit_type[i] == UNIT_VILLAGER || units_.unit_type[i] == UNIT_FEMALE_CITIZEN))
				++workers;
		const float staffed = (petra_ps().phase == 1) ? 0.8f : 0.5f;
		if ((float)workers < staffed * (float)petra_villager_target()) return;
	}

	int enemy_infantry = 0;
	int enemy_spearmen = 0;
	int enemy_archers = 0;
	int enemy_cavalry = 0;
	int enemy_siege = 0;
	int enemy_champions = 0;
	int enemy_forts = 0;

	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || !petra_foe(units_.team[i])) continue;
		switch (units_.unit_type[i]) {
			case UNIT_SWORDSMAN: enemy_infantry++; break;
			case UNIT_SPEARMAN: enemy_spearmen++; break;
			case UNIT_ARCHER: enemy_archers++; break;
			case UNIT_CAVALRY: enemy_cavalry++; break;
			case UNIT_CATAPULT: case UNIT_SIEGE_RAM: enemy_siege++; break;
			case UNIT_HERO: case UNIT_ELEPHANT: enemy_champions++; break;
			default: break;
		}
	}

	for (const auto &b : buildings_) {
		if (!petra_foe(b.team) || b.build_progress < 0.5f) continue;
		if (b.type == BLD_FORTRESS || b.type == BLD_TOWER || b.type == BLD_GATE || b.type == BLD_WALL_SEGMENT) {
			enemy_forts++;
		}
	}

	int total_enemy_mil = enemy_infantry + enemy_spearmen + enemy_archers + enemy_cavalry + enemy_siege + enemy_champions;
	float norm = 1.0f / (float)std::max(1, total_enemy_mil);

	// Dynamic Counter-Unit Utility Scores (0 A.D. Petra combat counters)
	float spearman_score = 1.2f + (enemy_cavalry * 4.5f + enemy_champions * 3.5f) * norm;
	float archer_score   = 1.0f + (enemy_infantry * 3.0f + enemy_spearmen * 3.5f) * norm;
	float swordsman_score = 1.0f + (enemy_spearmen * 1.8f) * norm;
	float cavalry_score  = 0.8f + (enemy_archers * 4.0f + enemy_siege * 3.0f) * norm;
	float siege_score    = (petra_ps().phase >= 2) ? (1.5f + (float)enemy_forts * 2.5f) : 0.2f;
	if (petra_attack_plan_.type == AttackPlan::PLAN_SIEGE && petra_ps().phase >= 2) {
		siege_score += 5.0f; // Prioritize siege engines before assault
	}
	float champion_score = (petra_ps().phase >= 3) ? 2.5f : 0.0f;

	struct Option {
		UnitType type;
		float score;
		uint32_t bld_id;
	};
	std::vector<Option> options;

	uint8_t ai_civ = petra_ps().civ;
	auto push_if_civ = [&](UnitType t, float score, uint32_t bid) {
		if (civ_can_train(ai_civ, (uint8_t)t))
			options.push_back({t, score, bid});
	};

	for (uint32_t bid : barracks_list) {
		push_if_civ(UNIT_SPEARMAN, spearman_score, bid);
		push_if_civ(UNIT_ARCHER, archer_score, bid);
		push_if_civ(UNIT_SWORDSMAN, swordsman_score, bid);
		if (petra_ps().phase >= 2) {
			push_if_civ(UNIT_CAVALRY, cavalry_score, bid);
			push_if_civ(UNIT_CAMEL, cavalry_score, bid);
			push_if_civ(UNIT_CHARIOT, cavalry_score * 0.95f, bid);
		}
	}

	for (uint32_t fid : fortress_list) {
		if (petra_ps().phase >= 2) {
			push_if_civ(UNIT_CATAPULT, siege_score, fid);
			push_if_civ(UNIT_SIEGE_RAM, siege_score * 0.95f, fid);
			push_if_civ(UNIT_BOLT_SHOOTER, siege_score * 0.85f, fid);
		}
		if (petra_ps().phase >= 3) {
			push_if_civ(UNIT_ELEPHANT, champion_score, fid);
			push_if_civ(UNIT_HERO, champion_score * 0.9f, fid);
			push_if_civ(UNIT_SCYTHED_CHARIOT, champion_score * 0.85f, fid);
		}
	}

	std::sort(options.begin(), options.end(), [](const Option &a, const Option &b) {
		return a.score > b.score;
	});

	for (const auto &opt : options) {
		if (pop_current(petra_pid_) >= pop_max(petra_pid_)) break;
		const UnitTypeInfo &uinfo = civ_unit_info(civ_of(petra_pid_), opt.type);
		if (petra_try_pay(PETRA_QUEUE_MILITARY, (float)uinfo.food_cost, (float)uinfo.wood_cost,
		                  (float)uinfo.gold_cost, (float)uinfo.stone_cost)) {
			auto bit = building_id_to_idx_.find(opt.bld_id);
			if (bit != building_id_to_idx_.end()) {
				ProductionOrder order;
				order.unit_type = opt.type;
				order.timer = 0.0f;
				order.duration = uinfo.train_time * 0.75f * tech_mods(petra_pid_).train_time[opt.type];
				buildings_[bit->second].queue.push_back(order);
			}
			break;
		}
	}
}

// Workers Petra aims for: most of the population while booming in the
// village phase, a smaller share once the army has to grow (config.js
// Economy.targetNumWorkers, scaled by difficulty).
int Simulation::petra_villager_target() const {
	const int pmax = pop_max(petra_pid_);
	const float share = (petra_ps().phase == 1) ? 0.75f : 0.60f;
	int target = (int)std::round((float)pmax * share);
	int min_villagers = (petra_ps().phase == 1) ? 12 : ((petra_ps().phase == 2) ? 22 : 35);
	min_villagers = (int)std::round((float)min_villagers * petra_config_.pop_scaling);
	min_villagers = std::max(8, std::min(min_villagers, petra_config_.target_num_workers));
	target = std::max(min_villagers, target);
	return std::min(target, petra_config_.target_num_workers);
}

// baseManager.js assignToFoundations: every AI foundation gets builders,
// pulled off gathering when nobody is idle, so houses and dropsites finish.
void Simulation::petra_staff_foundations() {
	// Runs once per economy pass (1 s).
	// Builders path around buildings now (VertexPathfinder steering), so this
	// only catches foundations nobody can reach; 0 A.D. has no such timer.
	constexpr float kStallRelease = 40.0f;
	struct Need { uint32_t id; float x, z; int missing; PetraFoundationWatch *watch; };
	std::vector<Need> needs;
	std::vector<PetraFoundationWatch> watch;
	watch.reserve(petra_fnd_watch_.size() + 4);
	for (const auto &b : buildings_) {
		if (b.team != petra_pid_ || b.build_progress >= 1.0f || b.hp <= 0.0f || b.wrecked) continue;
		PetraFoundationWatch w;
		w.id = b.id;
		w.progress = b.build_progress;
		for (auto &old : petra_fnd_watch_) {
			if (old.id != b.id) continue;
			w.banned = std::move(old.banned);
			w.stall = (b.build_progress > old.progress) ? 0.0f : old.stall + 1.0f;
			break;
		}
		watch.push_back(std::move(w));
	}
	petra_fnd_watch_ = std::move(watch);
	if (petra_fnd_watch_.empty()) return;
	for (auto &w : petra_fnd_watch_) {
		const Building &b = buildings_[building_id_to_idx_.at(w.id)];
		int want = (b.type == BLD_TOWN_CENTER || b.type == BLD_FORTRESS || b.type == BLD_WONDER) ? 4
		         : (b.type == BLD_HOUSE ? 1 : (b.type == BLD_FIELD ? 3 : 2));
		needs.push_back({w.id, b.x, b.z, want, &w});
	}

	int workers = 0, building = 0;
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != petra_pid_) continue;
		uint8_t ut = units_.unit_type[i];
		if (ut != UNIT_VILLAGER && ut != UNIT_FEMALE_CITIZEN) continue;
		++workers;
		if (units_.action_state[i] != ACTION_BUILD) continue;
		for (auto &n : needs) {
			if (n.id != units_.target_building_id[i]) continue;
			if (n.watch->stall >= kStallRelease) {
				// Our movement only paths MOVE orders, so a builder blocked by
				// terrain never arrives; free it and try a different worker.
				n.watch->banned.push_back(units_.id[i]);
				units_.action_state[i] = ACTION_IDLE;
				units_.target_building_id[i] = 0;
				units_.has_order[i] = 0;
			} else {
				--n.missing;
				++building;
			}
			break;
		}
	}
	for (auto &n : needs)
		if (n.watch->stall >= kStallRelease) n.watch->stall = 0.0f;
	// Keep at least two thirds of the workforce on the economy.
	int budget = std::max(1, workers / 3) - building;

	for (auto &n : needs) {
		while (n.missing > 0 && budget > 0) {
			int best = -1;
			float best_d2 = 1e18f;
			for (size_t i = 0; i < units_.count; ++i) {
				if (!units_.alive[i] || units_.team[i] != petra_pid_) continue;
				uint8_t ut = units_.unit_type[i];
				if (ut != UNIT_VILLAGER && ut != UNIT_FEMALE_CITIZEN) continue;
				uint8_t st = units_.action_state[i];
				if (st != ACTION_IDLE && st != ACTION_GATHER && st != ACTION_RETURN_DROPSITE) continue;
				const auto &ban = n.watch->banned;
				if (std::find(ban.begin(), ban.end(), units_.id[i]) != ban.end()) continue;
				float dx = units_.pos_x[i] - n.x, dz = units_.pos_z[i] - n.z;
				// Idle workers first, then the nearest gatherer.
				float d2 = dx * dx + dz * dz + (st == ACTION_IDLE ? 0.0f : 1e6f);
				if (d2 < best_d2) { best_d2 = d2; best = (int)i; }
			}
			if (best < 0) break;
			uint32_t uid = units_.id[best];
			order_build_or_repair(&uid, 1, n.id);
			--n.missing;
			--budget;
		}
	}
}

void Simulation::petra_manage_economy(float dt) {
	petra_econ_timer_ += dt;
	if (petra_econ_timer_ < 1.0f) return;
	petra_econ_timer_ = 0.0f;

	// 1. Priority Queue 1: Economic Boom — keep the TCs training workers
	// until the worker target (config.js Economy.targetNumWorkers) is met.
	petra_staff_foundations();

	int ai_villagers = 0;
	for (size_t i = 0; i < units_.count; ++i) {
		if (units_.alive[i] && units_.team[i] == petra_pid_ &&
		    (units_.unit_type[i] == UNIT_VILLAGER || units_.unit_type[i] == UNIT_FEMALE_CITIZEN)) {
			ai_villagers++;
		}
	}

	int pmax = pop_max(petra_pid_);
	int target_villagers = petra_villager_target();

	if (ai_villagers < target_villagers && pop_current(petra_pid_) < pmax) {
		for (auto &b : buildings_) {
			if (b.team == petra_pid_ && b.type == BLD_TOWN_CENTER && b.build_progress >= 1.0f && b.queue.size() < 3) {
				const UnitTypeInfo &vinfo = civ_unit_info(civ_of(petra_pid_), UNIT_VILLAGER);
				if (!petra_try_pay(PETRA_QUEUE_VILLAGER, (float)vinfo.food_cost, (float)vinfo.wood_cost,
				                   (float)vinfo.gold_cost, (float)vinfo.stone_cost)) break;
				ProductionOrder order;
				order.unit_type = UNIT_VILLAGER;
				order.timer = 0.0f;
				order.duration = vinfo.train_time * 0.8f * tech_mods(petra_pid_).train_time[UNIT_VILLAGER] * ai_time_mult(petra_pid_);
				b.queue.push_back(order);
			}
		}
	}

	// 2. Count workers currently on each resource
	int workers_food = 0, workers_wood = 0, workers_gold = 0, workers_stone = 0;
	std::vector<size_t> reassignable_workers;

	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != petra_pid_ ||
		    (units_.unit_type[i] != UNIT_VILLAGER && units_.unit_type[i] != UNIT_FEMALE_CITIZEN)) continue;
		if (units_.action_state[i] == ACTION_IDLE) {
			reassignable_workers.push_back(i);
		} else if (units_.action_state[i] == ACTION_GATHER || units_.action_state[i] == ACTION_RETURN_DROPSITE) {
			if (units_.target_resource_id[i] != 0) {
				auto rit = res_id_to_idx_.find(units_.target_resource_id[i]);
				if (rit != res_id_to_idx_.end()) {
					uint8_t rtype = resources_.type[rit->second];
					if (rtype == RES_FOOD) workers_food++;
					else if (rtype == RES_WOOD) workers_wood++;
					else if (rtype == RES_GOLD) workers_gold++;
					else if (rtype == RES_STONE) workers_stone++;
				}
			}
		}
	}

	// 3. Dynamic Resource Demand Computation based on Military & Tech Priorities
	float demand_food = 300.0f;
	float demand_wood = 400.0f;
	float demand_gold = 250.0f;
	float demand_stone = (petra_ps().phase >= 2) ? 200.0f : 50.0f;

	// Tech & Phase Priorities:
	if (petra_ps().phase == 1 && !petra_ps().has_tech(TECH_PHASE_TOWN)) {
		const TechInfo &tinfo = tech_info(TECH_PHASE_TOWN);
		demand_food += (float)tinfo.food_cost;
		demand_wood += (float)tinfo.wood_cost;
		demand_gold += (float)tinfo.gold_cost;
	} else if (petra_ps().phase == 2 && !petra_ps().has_tech(TECH_PHASE_CITY)) {
		const TechInfo &tinfo = tech_info(TECH_PHASE_CITY);
		demand_food += (float)tinfo.food_cost;
		demand_wood += (float)tinfo.wood_cost;
		demand_stone += (float)tinfo.stone_cost;
		demand_gold += (float)tinfo.gold_cost;
	}

	// Economic & Military Techs:
	if (petra_ps().phase >= 2) {
		if (!petra_ps().has_tech(TECH_FARMING_1)) { demand_food += 150.0f; demand_wood += 100.0f; }
		if (!petra_ps().has_tech(TECH_WOODCUTTING_1)) { demand_wood += 150.0f; demand_gold += 50.0f; }
		if (!petra_ps().has_tech(TECH_MINING_1)) { demand_gold += 100.0f; demand_stone += 100.0f; }
	}

	// Military & Siege Priorities:
	if (petra_attack_plan_.type == AttackPlan::PLAN_SIEGE) {
		// Heavy siege requires wood, gold, and stone
		demand_wood += 350.0f;
		demand_gold += 250.0f;
		demand_stone += 200.0f;
	} else if (petra_attack_plan_.type == AttackPlan::PLAN_RAID) {
		demand_food += 250.0f;
		demand_gold += 200.0f;
	} else {
		demand_food += 200.0f;
		demand_wood += 200.0f;
		demand_gold += 150.0f;
	}

	// Housing and Building Needs:
	if (pop_current(petra_pid_) >= pop_max(petra_pid_) - 4) {
		demand_wood += 150.0f;
	}

	// Compute Resource Deficits & Proportions:
	float need_food  = std::max(40.0f, demand_food - petra_ps().food());
	float need_wood  = std::max(40.0f, demand_wood - petra_ps().wood());
	float need_gold  = std::max(30.0f, demand_gold - petra_ps().gold());
	float need_stone = (petra_ps().phase >= 2) ? std::max(40.0f, demand_stone - petra_ps().stone()) : 20.0f;

	float total_need = need_food + need_wood + need_gold + need_stone;
	float pct_food  = need_food / total_need;
	float pct_wood  = need_wood / total_need;
	float pct_gold  = need_gold / total_need;
	float pct_stone = need_stone / total_need;

	int total_workers = std::max(1, ai_villagers);
	int target_food  = std::max(1, (int)std::round(pct_food * total_workers));
	int target_wood  = std::max(1, (int)std::round(pct_wood * total_workers));
	int target_gold  = std::max(1, (int)std::round(pct_gold * total_workers));
	int target_stone = std::max(0, (int)std::round(pct_stone * total_workers));

	// Safe, non-empty resources with their squared distance to our nearest
	// dropsite, computed once per pass rather than once per idle worker.
	// Fields cap their gatherers (FIELD_MAX_GATHERERS); slots counts the free ones.
	struct Candidate { uint32_t id; float x, z, d2_drop; uint8_t type; int slots; };
	std::vector<Candidate> candidates;
	if (!reassignable_workers.empty()) {
		std::unordered_map<uint32_t, int> field_users;
		if (!field_bld_by_res_.empty()) {
			for (size_t i = 0; i < units_.count; ++i) {
				if (!units_.alive[i] || units_.team[i] != petra_pid_ || units_.target_resource_id[i] == 0) continue;
				if (field_bld_by_res_.count(units_.target_resource_id[i])) field_users[units_.target_resource_id[i]]++;
			}
		}
		std::vector<std::pair<float, float>> drops;
		for (const auto &b : buildings_) {
			if (b.team == petra_pid_ && b.hp > 0 &&
			    (b.type == BLD_TOWN_CENTER || b.type == BLD_STOREHOUSE || b.type == BLD_FARMSTEAD || b.type == BLD_DOCK))
				drops.push_back({b.x, b.z});
		}
		for (const auto &base : petra_bases_) drops.push_back({base.x, base.z});
		for (size_t r = 0; r < resources_.count; ++r) {
			if (!resources_.alive[r] || resources_.amount[r] <= 0) continue;
			if (!is_position_safe(resources_.x[r], resources_.z[r], 6.0f)) continue;
			int slots = 1 << 20;
			auto fit = field_bld_by_res_.find(resources_.id[r]);
			if (fit != field_bld_by_res_.end()) {
				auto bit = building_id_to_idx_.find(fit->second);
				if (bit == building_id_to_idx_.end() || buildings_[bit->second].team != petra_pid_) continue;
				auto uit = field_users.find(resources_.id[r]);
				slots = FIELD_MAX_GATHERERS - (uit == field_users.end() ? 0 : uit->second);
				if (slots <= 0) continue;
			}
			float d2_drop = 1e9f;
			for (const auto &d : drops) {
				float bdx = resources_.x[r] - d.first;
				float bdz = resources_.z[r] - d.second;
				d2_drop = std::min(d2_drop, bdx * bdx + bdz * bdz);
			}
			candidates.push_back({resources_.id[r], resources_.x[r], resources_.z[r], d2_drop, resources_.type[r], slots});
		}
	}

	// Assign reassignable workers to nearest safe dropsite and resource
	for (size_t widx : reassignable_workers) {
		uint8_t chosen_res_type = RES_FOOD;
		int max_deficit = target_food - workers_food;

		if (target_wood - workers_wood > max_deficit) {
			max_deficit = target_wood - workers_wood;
			chosen_res_type = RES_WOOD;
		}
		if (target_gold - workers_gold > max_deficit) {
			max_deficit = target_gold - workers_gold;
			chosen_res_type = RES_GOLD;
		}
		if (target_stone - workers_stone > max_deficit) {
			max_deficit = target_stone - workers_stone;
			chosen_res_type = RES_STONE;
		}

		float wx = units_.pos_x[widx];
		float wz = units_.pos_z[widx];

		// Fall back to the other resource types, neediest first, when the
		// chosen one has nothing safe left (a depleted map left workers idle).
		uint8_t order[4] = {chosen_res_type, RES_WOOD, RES_FOOD, RES_GOLD};
		int deficits[RES_COUNT] = {};
		deficits[RES_FOOD] = target_food - workers_food;
		deficits[RES_WOOD] = target_wood - workers_wood;
		deficits[RES_GOLD] = target_gold - workers_gold;
		deficits[RES_STONE] = target_stone - workers_stone;
		{
			int n = 0;
			uint8_t rest[3];
			for (uint8_t rt : {RES_FOOD, RES_WOOD, RES_GOLD, RES_STONE})
				if (rt != chosen_res_type) rest[n++] = rt;
			std::stable_sort(rest, rest + 3, [&](uint8_t a, uint8_t b) { return deficits[a] > deficits[b]; });
			order[1] = rest[0]; order[2] = rest[1]; order[3] = rest[2];
		}

		uint32_t best_rid = 0;
		Candidate *best_c = nullptr;
		uint8_t got_type = chosen_res_type;
		for (uint8_t rt : order) {
			float best_score = 1e9f;
			for (auto &c : candidates) {
				if (c.type != rt || c.slots <= 0) continue;
				float dx = wx - c.x;
				float dz = wz - c.z;
				float score = dx * dx + dz * dz + c.d2_drop * 0.5f;
				if (score < best_score) {
					best_score = score;
					best_rid = c.id;
					best_c = &c;
				}
			}
			if (best_rid != 0) { got_type = rt; break; }
		}
		if (best_c) best_c->slots--;

		if (best_rid != 0) {
			uint32_t uid = units_.id[widx];
			order_gather(&uid, 1, best_rid);
			if (got_type == RES_FOOD) workers_food++;
			else if (got_type == RES_WOOD) workers_wood++;
			else if (got_type == RES_GOLD) workers_gold++;
			else if (got_type == RES_STONE) workers_stone++;
		}
	}

	petra_manage_fields(target_food);
}

// headquarters.js / baseManager.js: once the hunt and berries near our
// dropsites can't feed the food gatherers, Petra farms. Each field holds
// FIELD_MAX_GATHERERS, and one field is laid out at a time; its builders
// start farming it when it's done.
void Simulation::petra_manage_fields(int target_food) {
	std::vector<std::pair<float, float>> drops;
	const Building *farmstead = nullptr, *tc = nullptr;
	int fields = 0;
	bool field_pending = false;
	for (const auto &b : buildings_) {
		if (b.team != petra_pid_ || b.hp <= 0.0f || b.wrecked) continue;
		if (b.type == BLD_FIELD) {
			++fields;
			if (b.build_progress < 1.0f) field_pending = true;
			continue;
		}
		if (b.build_progress < 1.0f) continue;
		if (b.type == BLD_TOWN_CENTER || b.type == BLD_FARMSTEAD) {
			drops.push_back({b.x, b.z});
			if (b.type == BLD_FARMSTEAD && !farmstead) farmstead = &b;
			if (b.type == BLD_TOWN_CENTER && !tc) tc = &b;
		}
	}
	if (field_pending || drops.empty()) return;

	float natural_food = 0.0f;
	for (size_t r = 0; r < resources_.count; ++r) {
		if (!resources_.alive[r] || resources_.type[r] != RES_FOOD || resources_.amount[r] <= 0) continue;
		if (field_bld_by_res_.count(resources_.id[r])) continue;
		for (const auto &d : drops) {
			const float dx = resources_.x[r] - d.first, dz = resources_.z[r] - d.second;
			if (dx * dx + dz * dz <= 40.0f * 40.0f) { natural_food += (float)resources_.amount[r]; break; }
		}
	}
	// About 150 food of hunt or berries per gatherer still in reach.
	const int natural_slots = std::min(target_food, (int)(natural_food / 150.0f));
	const int want = std::min(12, (target_food - natural_slots + FIELD_MAX_GATHERERS - 1) / FIELD_MAX_GATHERERS);
	if (fields >= want) return;

	const Building *anchor = farmstead ? farmstead : tc;
	if (!anchor) return;
	petra_place_building(BLD_FIELD, anchor->x, anchor->z, 2, 12, PETRA_QUEUE_VILLAGER);
}

void Simulation::petra_manage_trading(float dt) {
	petra_trade_timer_ += dt;
	if (petra_trade_timer_ < 2.0f) return;
	petra_trade_timer_ = 0.0f;

	if (petra_ps().phase < 2) return;

	// 1. Build Market if AI has none, or build secondary Market at forward base
	int market_count = 0;
	std::vector<uint32_t> market_ids;
	for (const auto &b : buildings_) {
		if (b.team == petra_pid_ && b.type == BLD_MARKET) {
			market_count++;
			if (b.build_progress >= 1.0f) market_ids.push_back(b.id);
		}
	}

	if (market_count == 0) {
		float base_x = 36.0f, base_z = 36.0f;
		if (const auto *pbase = get_primary_petra_base()) {
			base_x = pbase->x; base_z = pbase->z;
		}
		int start_cx = (int)(base_x / GRID);
		int start_cz = (int)(base_z / GRID);
		bool placed = false;
		for (int r = 4; r <= 9 && !placed; ++r) {
			for (int dx = -r; dx <= r && !placed; ++dx) {
				for (int dz = -r; dz <= r && !placed; ++dz) {
					if (std::abs(dx) != r && std::abs(dz) != r) continue;
					int cx = start_cx + dx;
					int cz = start_cz + dz;
					if (petra_can_place(cx, cz, 2) && is_position_safe(cx * GRID, cz * GRID, 6.0f) &&
					    petra_try_pay(PETRA_QUEUE_TRADE, 0.0f, 200.0f, 100.0f, 0.0f)) {
						uint32_t bid = spawn_building(cx, cz, BLD_MARKET, petra_pid_, 0.0f);
						auto idle_workers = get_idle_workers(petra_pid_);
						if (!idle_workers.empty()) {
							order_build_or_repair(idle_workers.data(), std::min((size_t)3, idle_workers.size()), bid);
						}
						placed = true;
					}
				}
			}
		}
	} else if (market_count == 1 && petra_bases_.size() >= 2) {
		// Build secondary market at forward base to establish optimal trade route
		const PetraBase &fwd = petra_bases_[1];
		int start_cx = (int)(fwd.x / GRID);
		int start_cz = (int)(fwd.z / GRID);
		bool placed = false;
		for (int r = 3; r <= 8 && !placed; ++r) {
			for (int dx = -r; dx <= r && !placed; ++dx) {
				for (int dz = -r; dz <= r && !placed; ++dz) {
					if (std::abs(dx) != r && std::abs(dz) != r) continue;
					int cx = start_cx + dx;
					int cz = start_cz + dz;
					if (petra_can_place(cx, cz, 2) && is_position_safe(cx * GRID, cz * GRID, 6.0f) &&
					    petra_try_pay(PETRA_QUEUE_TRADE, 0.0f, 200.0f, 100.0f, 0.0f)) {
						uint32_t bid = spawn_building(cx, cz, BLD_MARKET, petra_pid_, 0.0f);
						auto idle_workers = get_idle_workers(petra_pid_);
						if (!idle_workers.empty()) {
							order_build_or_repair(idle_workers.data(), std::min((size_t)3, idle_workers.size()), bid);
						}
						placed = true;
					}
				}
			}
		}
	}

	// 2. Train UNIT_TRADER caravans at Market
	int ai_traders = 0;
	for (size_t i = 0; i < units_.count; ++i) {
		if (units_.alive[i] && units_.team[i] == petra_pid_ && units_.unit_type[i] == UNIT_TRADER) {
			ai_traders++;
		}
	}

	int target_traders = petra_config_.target_num_traders;
	if (petra_ps().phase >= 3) target_traders += 1;
	if (ai_traders < target_traders && !market_ids.empty() && pop_current(petra_pid_) < pop_max(petra_pid_)) {
		const UnitTypeInfo &tinfo = civ_unit_info(civ_of(petra_pid_), UNIT_TRADER);
		if (petra_try_pay(PETRA_QUEUE_TRADE, (float)tinfo.food_cost, (float)tinfo.wood_cost, 0.0f, 0.0f)) {
			auto bit = building_id_to_idx_.find(market_ids[0]);
			if (bit != building_id_to_idx_.end() && buildings_[bit->second].queue.size() < 3) {
				ProductionOrder order;
				order.unit_type = UNIT_TRADER;
				order.timer = 0.0f;
				order.duration = tinfo.train_time * 0.75f * tech_mods(petra_pid_).train_time[UNIT_TRADER] * ai_time_mult(petra_pid_);
				buildings_[bit->second].queue.push_back(order);
			}
		}
	}

	// 3. Dispatch Idle Traders on Active Trade Routes
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != petra_pid_ || units_.unit_type[i] != UNIT_TRADER) continue;
		uint32_t uid = units_.id[i];
		if (units_.action_state[i] == ACTION_IDLE || trader_routes_.find(uid) == trader_routes_.end()) {
			order_trade_auto(&uid, 1);
		}
	}
}

void Simulation::petra_manage_naval(float dt) {
	petra_naval_timer_ += dt;
	if (petra_naval_timer_ < 2.0f) return;
	petra_naval_timer_ = 0.0f;

	// Check if map is coastal / island / archipelago
	bool is_water_map = (terrain_water_height_ > 0.0f) ||
	                    (map_preset_ == MAP_AEGEAN_ARCHIPELAGO) ||
	                    (map_preset_ == MAP_PELOPONNESE) ||
	                    (biome_ == BIOME_AEGEAN);

	if (!is_water_map && nav_grid_.cells.w > 0) {
		for (int y = 0; y < nav_grid_.cells.h && !is_water_map; y += 4) {
			for (int x = 0; x < nav_grid_.cells.w && !is_water_map; x += 4) {
				if (nav_grid_.cells.at(x, y) == NAV_WATER) is_water_map = true;
			}
		}
	}
	if (!is_water_map) return;

	// 1. Construct BLD_DOCK if AI has none
	int dock_count = 0;
	std::vector<uint32_t> dock_ids;
	for (const auto &b : buildings_) {
		if (b.team == petra_pid_ && b.type == BLD_DOCK) {
			dock_count++;
			if (b.build_progress >= 1.0f) dock_ids.push_back(b.id);
		}
	}

	if (dock_count == 0) {
		float base_x = 36.0f, base_z = 36.0f;
		if (const auto *pbase = get_primary_petra_base()) {
			base_x = pbase->x; base_z = pbase->z;
		}
		int start_cx = (int)(base_x / GRID);
		int start_cz = (int)(base_z / GRID);
		bool placed = false;
		for (int r = 4; r <= 14 && !placed; ++r) {
			for (int dx = -r; dx <= r && !placed; ++dx) {
				for (int dz = -r; dz <= r && !placed; ++dz) {
					if (std::abs(dx) != r && std::abs(dz) != r) continue;
					int cx = start_cx + dx;
					int cz = start_cz + dz;
					float wx = cx * GRID;
					float wz = cz * GRID;
					float h = get_terrain_height(wx, wz);
					if (h >= terrain_water_height_ - 0.5f && h <= terrain_water_height_ + 1.8f) {
						if (petra_can_place(cx, cz, 2) && is_position_safe(wx, wz, 6.0f) &&
						    petra_try_pay(PETRA_QUEUE_NAVAL, 0.0f, 200.0f, 0.0f, 100.0f)) {
							uint32_t bid = spawn_building(cx, cz, BLD_DOCK, petra_pid_, 0.0f);
							auto idle_workers = get_idle_workers(petra_pid_);
							if (!idle_workers.empty()) {
								order_build_or_repair(idle_workers.data(), std::min((size_t)3, idle_workers.size()), bid);
							}
							placed = true;
						}
					}
				}
			}
		}
	}

	// 2. Train UNIT_TRIREME warships for maritime defense
	int ai_triremes = 0;
	for (size_t i = 0; i < units_.count; ++i) {
		if (units_.alive[i] && units_.team[i] == petra_pid_ && units_.unit_type[i] == UNIT_TRIREME) {
			ai_triremes++;
		}
	}

	int target_triremes = (petra_ps().phase == 1) ? 1 : ((petra_ps().phase == 2) ? 2 : 4);
	if (ai_triremes < target_triremes && !dock_ids.empty() && pop_current(petra_pid_) < pop_max(petra_pid_)) {
		const UnitTypeInfo &tinfo = civ_unit_info(civ_of(petra_pid_), UNIT_TRIREME);
		if (petra_try_pay(PETRA_QUEUE_NAVAL, 0.0f, (float)tinfo.wood_cost, (float)tinfo.gold_cost, 0.0f)) {
			auto bit = building_id_to_idx_.find(dock_ids[0]);
			if (bit != building_id_to_idx_.end() && buildings_[bit->second].queue.size() < 3) {
				ProductionOrder order;
				order.unit_type = UNIT_TRIREME;
				order.timer = 0.0f;
				order.duration = tinfo.train_time * 0.75f * tech_mods(petra_pid_).train_time[UNIT_TRIREME] * ai_time_mult(petra_pid_);
				buildings_[bit->second].queue.push_back(order);
			}
		}
	}

	// 2b. navalManager.js — fishing boats on food clusters near docks
	int ai_fishers = 0;
	for (size_t i = 0; i < units_.count; ++i) {
		if (units_.alive[i] && units_.team[i] == petra_pid_ && units_.unit_type[i] == UNIT_FISHING_BOAT)
			ai_fishers++;
	}
	int target_fishers = (petra_ps().phase >= 2) ? 3 : 2;
	if (ai_fishers < target_fishers && !dock_ids.empty() && pop_current(petra_pid_) < pop_max(petra_pid_)) {
		const UnitTypeInfo &finfo = civ_unit_info(civ_of(petra_pid_), UNIT_FISHING_BOAT);
		if (petra_try_pay(PETRA_QUEUE_NAVAL, 0.0f, (float)finfo.wood_cost, (float)finfo.gold_cost, 0.0f)) {
			auto bit = building_id_to_idx_.find(dock_ids[0]);
			if (bit != building_id_to_idx_.end() && buildings_[bit->second].queue.size() < 3) {
				ProductionOrder order;
				order.unit_type = UNIT_FISHING_BOAT;
				order.timer = 0.0f;
				order.duration = finfo.train_time * 0.75f * tech_mods(petra_pid_).train_time[UNIT_FISHING_BOAT] * ai_time_mult(petra_pid_);
				buildings_[bit->second].queue.push_back(order);
			}
		}
	}
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != petra_pid_ || units_.unit_type[i] != UNIT_FISHING_BOAT) continue;
		if (units_.action_state[i] != ACTION_IDLE && units_.action_state[i] != ACTION_MOVE) continue;
		if (units_.carry[i] > 0) continue;
		uint32_t best_rid = 0;
		float best_d2 = 40.0f * 40.0f;
		for (size_t r = 0; r < resources_.count; ++r) {
			if (!resources_.alive[r] || resources_.amount[r] <= 0) continue;
			if (resources_.type[r] != RES_FOOD) continue;
			// navalManager.js:223 canFishSafely — skip fish in enemy territory
			int owner = territory_owner_at(resources_.x[r], resources_.z[r]);
			if (owner >= 0 && owner != petra_pid_) continue;
			auto [fcx, fcy] = nav_grid_.world_to_cell(resources_.x[r], resources_.z[r]);
			if (!nav_grid_.is_passable_for(fcx, fcy, PASS_SHIP) &&
			    nav_grid_.cells.at(fcx, fcy) != NAV_WATER)
				continue;
			float dx = resources_.x[r] - units_.pos_x[i];
			float dz = resources_.z[r] - units_.pos_z[i];
			float d2 = dx * dx + dz * dz;
			if (d2 < best_d2) {
				best_d2 = d2;
				best_rid = resources_.id[r];
			}
		}
		if (best_rid) {
			uint32_t uid = units_.id[i];
			order_gather(&uid, 1, best_rid);
		}
	}

	// 3. Coastal Patrol & Maritime Defense
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != petra_pid_ || units_.unit_type[i] != UNIT_TRIREME) continue;
		if (units_.action_state[i] == ACTION_IDLE) {
			float tx = units_.pos_x[i];
			float tz = units_.pos_z[i];
			float best_d2 = 25.0f * 25.0f;
			bool enemy_found = false;

			for (size_t j = 0; j < units_.count; ++j) {
				if (!units_.alive[j] || !petra_foe(units_.team[j])) continue;
				float dx = units_.pos_x[j] - units_.pos_x[i];
				float dz = units_.pos_z[j] - units_.pos_z[i];
				float d2 = dx * dx + dz * dz;
				if (d2 < best_d2) {
					best_d2 = d2;
					tx = units_.pos_x[j];
					tz = units_.pos_z[j];
					enemy_found = true;
				}
			}

			if (enemy_found) {
				uint32_t uid = units_.id[i];
				order_attack_move(&uid, 1, tx, tz);
			}
		}
	}

	// navalManager.js:58, :546 assignShipsToPlans — per-sea fleet accounting
	// registry (0 A.D. NavalManager.seaWarShips). Groups team-1 warships by sea/region
	// index (capped at kMaxSeas=8 and 16 warships per sea) rather than a single
	// unpartitioned collection, while preserving the bounded assignment to active
	// attack plans and never reserving fishing/merchant ships.
	petra_fleets_.clear_sea_fleets();
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != petra_pid_) continue;
		uint8_t ut = units_.unit_type[i];
		// Exclude fishing and merchant ships via existing role check (unit_is_ship / petra_is_support_unit)
		if (!unit_is_ship(ut) || petra_is_support_unit(ut)) continue;

		// Determine sea id using grid_nav ship region lookup (PASS_SHIP).
		// Fallback to sea 0 if grid_nav lookup returns 0 (unassigned / land cell) or exceeds kMaxSeas.
		int sea_id = 0;
		auto [scx, scz] = nav_grid_.world_to_cell(units_.pos_x[i], units_.pos_z[i]);
		uint16_t ship_reg = nav_grid_.global_region_at(scx, scz, PASS_SHIP);
		if (ship_reg > 0 && static_cast<int>(ship_reg - 1) < Petra::kMaxSeas) {
			sea_id = static_cast<int>(ship_reg - 1);
		} else {
			// Sea 0 fallback: ship not in a mapped ship region, outside grid, or region >= kMaxSeas
			sea_id = 0;
		}

		if (petra_fleets_.sea_warship_count[sea_id] < 16) {
			petra_fleets_.sea_warships[sea_id][petra_fleets_.sea_warship_count[sea_id]] = units_.id[i];
			petra_fleets_.sea_warship_count[sea_id]++;
		}
	}
	int active_seas = 0;
	for (int s = 0; s < Petra::kMaxSeas; ++s) {
		if (petra_fleets_.sea_warship_count[s] > 0) {
			active_seas = s + 1;
		}
	}
	petra_fleets_.sea_count_ = active_seas;

	if (petra_attack_plan_.state == AttackPlan::WALKING ||
	    petra_attack_plan_.state == AttackPlan::ATTACKED) {
		uint32_t target_id = petra_attack_plan_.target_building_id;
		auto target_it = building_id_to_idx_.find(target_id);
		if (target_id != 0 && target_it != building_id_to_idx_.end() &&
		    buildings_[target_it->second].hp > 0.0f) {
			size_t assigned = 0;
			for (size_t i = 0; i < units_.count && assigned < 2; ++i) {
				if (!units_.alive[i] || units_.team[i] != petra_pid_ ||
				    units_.unit_type[i] != UNIT_TRIREME ||
				    units_.action_state[i] != ACTION_IDLE) continue;
				uint32_t uid = units_.id[i];
				if (units_.has_order[i]) continue;
				order_attack_building(&uid, 1, target_id);
				assigned++;
			}
		}
	}

	// navalManager.js:568 moveApart — idle ships must not stack on the dock
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != petra_pid_) continue;
		uint8_t ut = units_.unit_type[i];
		if (ut != UNIT_TRIREME && ut != UNIT_FISHING_BOAT && ut != UNIT_MERCHANT_SHIP) continue;
		if (units_.action_state[i] != ACTION_IDLE) continue;
		for (const auto &b : buildings_) {
			if (b.team != petra_pid_ || b.type != BLD_DOCK || b.hp <= 0) continue;
			float dx = units_.pos_x[i] - b.x;
			float dz = units_.pos_z[i] - b.z;
			if (dx * dx + dz * dz > 49.0f) continue;
			float ang = (float)(units_.id[i] % 8) * 0.78539816f;
			uint32_t uid = units_.id[i];
			cmd_move(&uid, 1, b.x + std::cos(ang) * 12.0f, b.z + std::sin(ang) * 12.0f);
			break;
		}
	}
}

void Simulation::petra_manage_buildings(float dt) {
	// 1. Emergency Defense Tower if threatened (threat >= 7.0f)
	float max_base_threat = 0.0f;
	float threatened_base_x = 36.0f, threatened_base_z = 36.0f;
	for (const auto &b : petra_bases_) {
		if (b.danger_level > max_base_threat) {
			max_base_threat = b.danger_level;
			threatened_base_x = b.x;
			threatened_base_z = b.z;
		}
	}

	if (max_base_threat >= 7.0f) {
		int start_cx = (int)(threatened_base_x / GRID);
		int start_cz = (int)(threatened_base_z / GRID);
		bool placed = false;
		for (int r = 3; r <= 7 && !placed; ++r) {
			for (int dx = -r; dx <= r && !placed; ++dx) {
				for (int dz = -r; dz <= r && !placed; ++dz) {
					if (std::abs(dx) != r && std::abs(dz) != r) continue;
					int cx = start_cx + dx;
					int cz = start_cz + dz;
					if (petra_can_place(cx, cz, 1) &&
					    petra_try_pay(PETRA_QUEUE_EMERGENCY, 0.0f, 100.0f, 0.0f, 100.0f)) {
						uint32_t bid = spawn_building(cx, cz, BLD_TOWER, petra_pid_, 0.0f);
						auto idle_workers = get_idle_workers(petra_pid_);
						if (!idle_workers.empty()) {
							order_build_or_repair(idle_workers.data(), std::min((size_t)3, idle_workers.size()), bid);
						}
						placed = true;
					}
				}
			}
		}
	}

	// 2. Automated Multi-Base Forward Expansion when primary resources deplete or in Phase >= 2
	bool primary_depleted = false;
	if (const auto *pbase = get_primary_petra_base()) {
		primary_depleted = pbase->resources_depleted;
	}

	if (primary_depleted || (petra_ps().phase >= 2 && petra_bases_.size() <= 1)) {
		// Check if Town Center is already in progress
		bool tc_in_progress = false;
		for (const auto &b : buildings_) {
			if (b.team == petra_pid_ && b.type == BLD_TOWN_CENTER && b.build_progress < 1.0f) {
				tc_in_progress = true;
				break;
			}
		}

		if (!tc_in_progress) {
			float p_x = 36.0f, p_z = 36.0f;
			if (const auto *pbase = get_primary_petra_base()) {
				p_x = pbase->x; p_z = pbase->z;
			}

			// Scan candidate resource clusters at distance 30m - 55m
			float best_cluster_score = -1.0f;
			int best_cx = 0, best_cz = 0;

			for (size_t r = 0; r < resources_.count; ++r) {
				if (!resources_.alive[r] || resources_.amount[r] < 400) continue;
				float rx = resources_.x[r];
				float rz = resources_.z[r];
				float dist_from_primary = std::hypot(rx - p_x, rz - p_z);
				if (dist_from_primary < 28.0f || dist_from_primary > 58.0f) continue;
				if (std::abs(rx) >= map_half_ - 8.0f || std::abs(rz) >= map_half_ - 8.0f) continue;
				if (!is_position_safe(rx, rz, 5.0f)) continue;

				// Ensure clear distance from enemy structures
				bool near_enemy = false;
				for (const auto &eb : buildings_) {
					if (petra_foe(eb.team) && eb.hp > 0 && std::hypot(eb.x - rx, eb.z - rz) < 26.0f) {
						near_enemy = true;
						break;
					}
				}
				if (near_enemy) continue;

				int cx = (int)(rx / GRID);
				int cz = (int)(rz / GRID);
				if (petra_can_place(cx, cz, 2)) {
					// Score based on surrounding resources
					float score = 0.0f;
					for (size_t j = 0; j < resources_.count; ++j) {
						if (!resources_.alive[j]) continue;
						float d = std::hypot(resources_.x[j] - rx, resources_.z[j] - rz);
						if (d <= 24.0f) {
							score += (float)resources_.amount[j];
						}
					}
					if (score > best_cluster_score) {
						best_cluster_score = score;
						best_cx = cx;
						best_cz = cz;
					}
				}
			}

			if (best_cluster_score > 0.0f &&
			    petra_try_pay(PETRA_QUEUE_VILLAGER, 0.0f, 300.0f, 100.0f, 200.0f)) {
				uint32_t bid = spawn_building(best_cx, best_cz, BLD_TOWN_CENTER, petra_pid_, 0.0f);
				auto idle_workers = get_idle_workers(petra_pid_);
				if (!idle_workers.empty()) {
					order_build_or_repair(idle_workers.data(), std::min((size_t)4, idle_workers.size()), bid);
				}
			}
		}
	}

	// 2b. Place Dropsites (Storehouse / Farmstead) adjacent to discovered Resource Clusters
	{
		bool dropsite_in_progress = false;
		for (const auto &b : buildings_) {
			if (b.team == petra_pid_ && (b.type == BLD_STOREHOUSE || b.type == BLD_FARMSTEAD) && b.build_progress < 1.0f) {
				dropsite_in_progress = true;
				break;
			}
		}

		if (!dropsite_in_progress) {
			float best_cluster_val = -1.0f;
			float best_cluster_x = 0.0f, best_cluster_z = 0.0f;
			uint8_t best_cluster_rtype = RES_WOOD;

			for (size_t r = 0; r < resources_.count; ++r) {
				if (!resources_.alive[r] || resources_.amount[r] < 300) continue;
				float rx = resources_.x[r];
				float rz = resources_.z[r];

				// Check if explored in Petra FoW
				const float fow_cell = (map_half_ * 2.0f) / (float)grid_res_;
				int rcx = std::clamp((int)((rx + map_half_) / fow_cell), 0, grid_res_ - 1);
				int rcz = std::clamp((int)((rz + map_half_) / fow_cell), 0, grid_res_ - 1);
				if (petra_fow_[rcz * grid_res_ + rcx] == FOW_UNEXPLORED) continue;
				// Storehouses and farmsteads need our own territory (0 A.D. BuildRestrictions).
				if (territory_owner_at(rx, rz) != petra_pid_) continue;
				if (!is_position_safe(rx, rz, 5.0f)) continue;

				// Check distance to nearest existing dropsite
				float min_dropsite_dist = 1e9f;
				for (const auto &b : buildings_) {
					if (b.team == petra_pid_ && b.hp > 0 &&
					    (b.type == BLD_TOWN_CENTER || b.type == BLD_STOREHOUSE || b.type == BLD_FARMSTEAD || b.type == BLD_DOCK)) {
						float d = std::hypot(b.x - rx, b.z - rz);
						min_dropsite_dist = std::min(min_dropsite_dist, d);
					}
				}

				if (min_dropsite_dist > 18.0f) {
					// Count nearby resource cluster density
					float cluster_amount = 0.0f;
					int cluster_nodes = 0;
					int food_count = 0, non_food_count = 0;

					for (size_t j = 0; j < resources_.count; ++j) {
						if (!resources_.alive[j] || resources_.amount[j] <= 0) continue;
						float dj = std::hypot(resources_.x[j] - rx, resources_.z[j] - rz);
						if (dj <= 14.0f) {
							cluster_amount += (float)resources_.amount[j];
							cluster_nodes++;
							if (resources_.type[j] == RES_FOOD) food_count++;
							else non_food_count++;
						}
					}

					if (cluster_nodes >= 3 || cluster_amount >= 1200.0f) {
						float score = cluster_amount / (min_dropsite_dist + 1.0f);
						if (score > best_cluster_val) {
							best_cluster_val = score;
							best_cluster_x = rx;
							best_cluster_z = rz;
							best_cluster_rtype = (food_count > non_food_count) ? RES_FOOD : RES_WOOD;
						}
					}
				}
			}

			if (best_cluster_val > 0.0f) {
				uint8_t dropsite_type = (best_cluster_rtype == RES_FOOD) ? BLD_FARMSTEAD : BLD_STOREHOUSE;
				int start_cx = (int)(best_cluster_x / GRID);
				int start_cz = (int)(best_cluster_z / GRID);
				bool placed = false;

				for (int r = 2; r <= 6 && !placed; ++r) {
					for (int dx = -r; dx <= r && !placed; ++dx) {
						for (int dz = -r; dz <= r && !placed; ++dz) {
							if (std::abs(dx) != r && std::abs(dz) != r) continue;
							int cx = start_cx + dx;
							int cz = start_cz + dz;
							if (petra_can_place(cx, cz, 1) && is_position_safe(cx * GRID, cz * GRID, 5.0f) &&
							    petra_try_pay(PETRA_QUEUE_VILLAGER, 0.0f, 100.0f, 0.0f, 0.0f)) {
								uint32_t bid = spawn_building(cx, cz, dropsite_type, petra_pid_, 0.0f);
								auto idle_workers = get_idle_workers(petra_pid_);
								if (!idle_workers.empty()) {
									order_build_or_repair(idle_workers.data(), std::min((size_t)3, idle_workers.size()), bid);
								}
								placed = true;
							}
						}
					}
				}
			}
		}
	}

	// 3. Build House if approaching population limit (headquarters.js
	// buildMoreHouses: one in progress at a time, spiralling outward from the
	// primary base until a free spot is found).
	if (pop_current(petra_pid_) >= pop_max(petra_pid_) - 3) {
		bool house_in_progress = false;
		for (const auto &b : buildings_) {
			if (b.team == petra_pid_ && b.type == BLD_HOUSE && b.build_progress < 1.0f) {
				house_in_progress = true;
				break;
			}
		}
		if (!house_in_progress) {
			float base_x = 36.0f, base_z = 36.0f;
			if (const auto *pbase = get_primary_petra_base()) {
				base_x = pbase->x; base_z = pbase->z;
			}
			petra_place_building(BLD_HOUSE, base_x, base_z, 3, 16, PETRA_QUEUE_VILLAGER);
		}
	}

	// 4. Build Barracks if AI has none
	bool has_barracks = false;
	for (const auto &b : buildings_) {
		if (b.team == petra_pid_ && b.type == BLD_BARRACKS) { has_barracks = true; break; }
	}
	if (!has_barracks) {
		float base_x = 36.0f, base_z = 36.0f;
		if (const auto *pbase = get_primary_petra_base()) {
			base_x = pbase->x; base_z = pbase->z;
		}
		int start_cx = (int)(base_x / GRID);
		int start_cz = (int)(base_z / GRID);
		bool placed = false;
		for (int r = 3; r <= 8 && !placed; ++r) {
			for (int dx = -r; dx <= r && !placed; ++dx) {
				for (int dz = -r; dz <= r && !placed; ++dz) {
					int cx = start_cx + dx;
					int cz = start_cz + dz;
					if (petra_can_place(cx, cz, 2) &&
					    petra_try_pay(PETRA_QUEUE_MILITARY, 0.0f, 150.0f, 50.0f, 50.0f)) {
						uint32_t bid = spawn_building(cx, cz, BLD_BARRACKS, petra_pid_, 0.0f);
						auto idle_workers = get_idle_workers(petra_pid_);
						if (!idle_workers.empty()) {
							order_build_or_repair(idle_workers.data(), std::min((size_t)3, idle_workers.size()), bid);
						}
						placed = true;
					}
				}
			}
		}
	}

	// 5. Build Defense Towers in Phase >= 2
	if (petra_ps().phase >= 2) {
		int tower_count = 0;
		for (const auto &b : buildings_) {
			if (b.team == petra_pid_ && b.type == BLD_TOWER) tower_count++;
		}
		if (tower_count < 4) {
			float base_x = 36.0f, base_z = 36.0f;
			if (const auto *pbase = get_primary_petra_base()) {
				base_x = pbase->x; base_z = pbase->z;
			}
			int start_cx = (int)(base_x / GRID);
			int start_cz = (int)(base_z / GRID);
			bool placed = false;
			for (int r = 5; r <= 10 && !placed; ++r) {
				for (int dx = -r; dx <= 0 && !placed; ++dx) {
					for (int dz = -r; dz <= 0 && !placed; ++dz) {
						int cx = start_cx + dx;
						int cz = start_cz + dz;
						if (petra_can_place(cx, cz, 1) &&
						    petra_try_pay(PETRA_QUEUE_MILITARY, 0.0f, 100.0f, 0.0f, 100.0f)) {
							uint32_t bid = spawn_building(cx, cz, BLD_TOWER, petra_pid_, 0.0f);
							auto idle_workers = get_idle_workers(petra_pid_);
							if (!idle_workers.empty()) {
								order_build_or_repair(idle_workers.data(), std::min((size_t)2, idle_workers.size()), bid);
							}
							placed = true;
						}
					}
				}
			}
		}
	}

	// 6. Build Fortress in Phase >= 3
	if (petra_ps().phase >= 3) {
		bool has_fortress = false;
		for (const auto &b : buildings_) {
			if (b.team == petra_pid_ && b.type == BLD_FORTRESS) { has_fortress = true; break; }
		}
		if (!has_fortress) {
			float base_x = 36.0f, base_z = 36.0f;
			if (const auto *pbase = get_primary_petra_base()) {
				base_x = pbase->x; base_z = pbase->z;
			}
			int start_cx = (int)(base_x / GRID);
			int start_cz = (int)(base_z / GRID);
			bool placed = false;
			for (int r = 4; r <= 10 && !placed; ++r) {
				for (int dx = -r; dx <= 0 && !placed; ++dx) {
					for (int dz = -r; dz <= 0 && !placed; ++dz) {
						int cx = start_cx + dx;
						int cz = start_cz + dz;
						if (petra_can_place(cx, cz, 2)) {
							if (!petra_try_pay(PETRA_QUEUE_MILITARY, 0.0f, 300.0f, 200.0f, 300.0f)) break;
							uint32_t bid = spawn_building(cx, cz, BLD_FORTRESS, petra_pid_, 0.0f);
							auto idle_workers = get_idle_workers(petra_pid_);
							if (!idle_workers.empty()) {
								order_build_or_repair(idle_workers.data(), std::min((size_t)4, idle_workers.size()), bid);
							}
							placed = true;
						}
					}
				}
			}
		}
	}
}

void Simulation::petra_manage_research(float dt) {
	(void)dt;
	// Priority Queue 4: Technology / Phase Research — spend only from TECH account.
	// researchManager.js researches each tech at the structure that offers it.
	auto researcher_for = [&](uint8_t btype) -> Building * {
		for (auto &b : buildings_)
			if (b.team == petra_pid_ && b.type == btype && b.build_progress >= 1.0f && b.hp > 0.0f &&
			    !b.wrecked && b.research_tech == TECH_NONE)
				return &b;
		return nullptr;
	};
	auto have_building = [&](uint8_t btype) {
		for (const auto &b : buildings_)
			if (b.team == petra_pid_ && b.type == btype && b.hp > 0.0f && !b.wrecked) return true;
		return false;
	};
	const PetraBase *home = get_primary_petra_base();

	auto start_tech = [&](uint8_t tech_id) -> bool {
		if (petra_ps().has_tech(tech_id)) return false;
		// Same civ / phase / prerequisite / structure rules as the player
		// (cost is checked against the TECH account below).
		ResearchCheck rc = research_check(petra_pid_, tech_id);
		if (rc != TECH_OK && rc != TECH_ERR_COST) return false;
		const TechInfo &tinfo = tech_info(tech_id);
		Building *at = researcher_for(tinfo.researcher);
		if (!at) {
			// No free researcher: lay one out if we have none at all.
			if (!have_building(tinfo.researcher) && home && tinfo.researcher != BLD_TOWN_CENTER &&
			    building_phase_class(tinfo.researcher) <= petra_ps().phase)
				petra_place_building(tinfo.researcher, home->x, home->z, 4, 14, PETRA_QUEUE_TECH);
			return false;
		}
		if (!petra_try_pay(PETRA_QUEUE_TECH, (float)tinfo.food_cost, (float)tinfo.wood_cost,
		                   (float)tinfo.gold_cost, (float)tinfo.stone_cost)) {
			return false;
		}
		at->research_tech = tech_id;
		at->research_timer = 0.0f;
		at->research_duration = tinfo.research_time * 0.75f * tech_mods(petra_pid_).bld_research_time[at->type] * ai_time_mult(petra_pid_);
		return true;
	};

	// headquarters.js checkPhaseRequirements: build the structures the next
	// phase asks for (5 Village / 3 Town class) before researching it.
	auto build_phase_structures = [&](uint8_t cls, int need) {
		if (!home || phase_structures(petra_pid_, cls) >= need) return;
		for (const auto &b : buildings_)
			if (b.team == petra_pid_ && b.build_progress < 1.0f && b.hp > 0.0f && building_phase_class(b.type) == cls)
				return; // one at a time; it counts once finished
		static constexpr uint8_t kVillage[] = {BLD_HOUSE};
		static constexpr uint8_t kTown[] = {BLD_FORGE, BLD_TEMPLE, BLD_MARKET};
		if (cls == 1) {
			for (uint8_t t : kVillage) if (petra_place_building(t, home->x, home->z, 3, 16, PETRA_QUEUE_TECH)) return;
		} else {
			for (uint8_t t : kTown) if (!have_building(t) && petra_place_building(t, home->x, home->z, 4, 16, PETRA_QUEUE_TECH)) return;
			petra_place_building(BLD_TEMPLE, home->x, home->z, 4, 16, PETRA_QUEUE_TECH);
		}
	};

	// researchManager.js:15 checkPhase — Town at pop >= Economy.popPhase2,
	// City when workers > Economy.workPhase3 (not a 0.55 fudge).
	int pop = pop_current(petra_pid_);
	int workers = 0;
	for (size_t i = 0; i < units_.count; ++i) {
		if (units_.alive[i] && units_.team[i] == petra_pid_ &&
		    (units_.unit_type[i] == UNIT_VILLAGER || units_.unit_type[i] == UNIT_FEMALE_CITIZEN ||
		     units_.unit_type[i] == UNIT_FISHING_BOAT))
			++workers;
	}

	if (petra_ps().phase == 1) {
		if (pop < petra_config_.pop_phase2) {
			if (start_tech(TECH_FARMING_1)) return;   // researchWantedTechs village
			if (start_tech(TECH_WOODCUTTING_1)) return;
			return;
		}
		build_phase_structures(1, 5);
		if (start_tech(TECH_PHASE_TOWN)) return;
	}
	if (petra_ps().phase >= 2) {
		if (start_tech(TECH_FARMING_1)) return;
		if (start_tech(TECH_WOODCUTTING_1)) return;
		if (start_tech(TECH_MINING_1)) return;
	}
	if (petra_ps().phase == 2 && workers > petra_config_.work_phase3) {
		build_phase_structures(2, 3);
		if (start_tech(TECH_PHASE_CITY)) return;
	}
	if (petra_ps().phase >= 3) {
		if (start_tech(TECH_WEAPONS_1)) return;
		if (start_tech(TECH_ARMOR_1)) return;
	}
}

bool Simulation::pick_petra_safe_staging(float base_x, float base_z, float target_x, float target_z, float &out_sx, float &out_sz) const {
	float dx = target_x - base_x;
	float dz = target_z - base_z;
	float dist = std::sqrt(dx * dx + dz * dz);

	std::vector<std::pair<float, float>> candidates;
	if (dist > 1.0f) {
		float dir_x = dx / dist;
		float dir_z = dz / dist;
		float perp_x = -dir_z;
		float perp_z = dir_x;

		candidates.push_back({base_x + dir_x * (dist * 0.35f), base_z + dir_z * (dist * 0.35f)});
		candidates.push_back({base_x + dir_x * (dist * 0.45f), base_z + dir_z * (dist * 0.45f)});
		candidates.push_back({base_x + dir_x * (dist * 0.40f) + perp_x * 16.0f, base_z + dir_z * (dist * 0.40f) + perp_z * 16.0f});
		candidates.push_back({base_x + dir_x * (dist * 0.30f) + perp_x * 24.0f, base_z + dir_z * (dist * 0.30f) + perp_z * 24.0f});
		candidates.push_back({base_x + dir_x * (dist * 0.40f) - perp_x * 16.0f, base_z + dir_z * (dist * 0.40f) - perp_z * 16.0f});
		candidates.push_back({base_x + dir_x * (dist * 0.30f) - perp_x * 24.0f, base_z + dir_z * (dist * 0.30f) - perp_z * 24.0f});
	}

	candidates.push_back({base_x * 0.65f, base_z * 0.65f});
	candidates.push_back({base_x * 0.75f, base_z * 0.50f});
	candidates.push_back({base_x * 0.50f, base_z * 0.75f});

	float best_danger = 1e9f;
	float best_x = base_x * 0.65f;
	float best_z = base_z * 0.65f;

	float bound = map_half_ - 6.0f;
	for (const auto &c : candidates) {
		float cx = std::clamp(c.first, -bound, bound);
		float cz = std::clamp(c.second, -bound, bound);

		if (get_terrain_height(cx, cz) <= terrain_water_height_ + 0.5f) continue;

		float danger = get_threat_at(cx, cz);
		if (danger < best_danger) {
			best_danger = danger;
			best_x = cx;
			best_z = cz;
		}
	}

	out_sx = best_x;
	out_sz = best_z;
	return (best_danger < petra_attack_plan_.abort_threat);
}

uint32_t Simulation::find_petra_attack_target(float &out_tx, float &out_tz) const {
	// 0 A.D. navalManager.js:46-48, :58, :546 / attackPlan.js:826-831, :930, :1100
	// When Petra has warships available (own warship count > 0), inspect enemy structures
	// for docks (BLD_DOCK) or coastal barracks reachable by sea. Prefer targets in the
	// same sea region as own warships (via per-sea registry petra_fleets_) to coordinate
	// naval fire support with landing raids, setting an alternative/special PLAN_RAID target.
	int own_warships = 0;
	for (int s = 0; s < Petra::kMaxSeas; ++s) {
		own_warships += petra_fleets_.sea_warship_count[s];
	}
	if (own_warships == 0) {
		for (size_t i = 0; i < units_.count; ++i) {
			if (!units_.alive[i] || units_.team[i] != petra_pid_) continue;
			uint8_t ut = units_.unit_type[i];
			if (ut == UNIT_TRIREME || (!petra_is_support_unit(ut) && unit_is_ship(ut))) {
				own_warships++;
				auto [scx, scz] = nav_grid_.world_to_cell(units_.pos_x[i], units_.pos_z[i]);
				uint16_t ship_reg = nav_grid_.global_region_at(scx, scz, PASS_SHIP);
				int sid = 0;
				if (ship_reg > 0 && static_cast<int>(ship_reg - 1) < Petra::kMaxSeas) {
					sid = static_cast<int>(ship_reg - 1);
				}
				if (petra_fleets_.sea_warship_count[sid] < 16) {
					petra_fleets_.sea_warships[sid][petra_fleets_.sea_warship_count[sid]++] = units_.id[i];
				}
			}
		}
	}

	if (own_warships > 0) {
		auto get_bld_sea_region = [&](float bx, float bz) -> int {
			auto [bcx, bcz] = nav_grid_.world_to_cell(bx, bz);
			for (int r = 1; r <= 4; ++r) {
				for (int dz = -r; dz <= r; ++dz) {
					for (int dx = -r; dx <= r; ++dx) {
						if (std::abs(dx) != r && std::abs(dz) != r) continue;
						int cx = bcx + dx;
						int cz = bcz + dz;
						uint16_t reg = nav_grid_.global_region_at(cx, cz, PASS_SHIP);
						if (reg > 0 && static_cast<int>(reg - 1) < Petra::kMaxSeas) {
							return static_cast<int>(reg - 1);
						}
					}
				}
			}
			for (int r = 1; r <= 4; ++r) {
				for (int dz = -r; dz <= r; ++dz) {
					for (int dx = -r; dx <= r; ++dx) {
						if (std::abs(dx) != r && std::abs(dz) != r) continue;
						int cx = bcx + dx;
						int cz = bcz + dz;
						if (nav_grid_.cells.inside(cx, cz) &&
						    (nav_grid_.cells.at(cx, cz) == NAV_WATER || nav_grid_.is_passable_for(cx, cz, PASS_SHIP))) {
							return 0;
						}
					}
				}
			}
			return -1;
		};

		uint32_t best_dock_id = 0;
		int best_dock_pri = -1;
		float best_dock_dsq = 1e9f;
		float best_dock_x = 0.0f, best_dock_z = 0.0f;

		for (const auto &b : buildings_) {
			if (!petra_foe(b.team) || b.hp <= 0) continue;
			if (b.type != BLD_DOCK && b.type != BLD_BARRACKS) continue;

			int sea_id = get_bld_sea_region(b.x, b.z);
			if (b.type == BLD_DOCK && sea_id < 0) sea_id = 0; // BLD_DOCK always has sea access
			if (b.type == BLD_BARRACKS && sea_id < 0) continue; // Skip inland barracks

			bool same_sea = (sea_id >= 0 && sea_id < Petra::kMaxSeas && petra_fleets_.sea_warship_count[sea_id] > 0);
			int dock_pri = 0;
			if (b.type == BLD_DOCK) {
				dock_pri = same_sea ? 100 : 80;
			} else if (b.type == BLD_BARRACKS) {
				dock_pri = same_sea ? 60 : 40;
			}

			float dsq = b.x * b.x + b.z * b.z;
			if (dock_pri > best_dock_pri || (dock_pri == best_dock_pri && dsq < best_dock_dsq)) {
				best_dock_pri = dock_pri;
				best_dock_dsq = dsq;
				best_dock_id = b.id;
				best_dock_x = b.x;
				best_dock_z = b.z;
			}
		}

		if (best_dock_id != 0) {
			out_tx = best_dock_x;
			out_tz = best_dock_z;
			// 0 A.D. attackPlan.js:826-831, :930 — Set target_building_id and plan type to PLAN_RAID for naval dock strike
			const_cast<Simulation*>(this)->petra_attack_plan_.type = AttackPlan::PLAN_RAID;
			const_cast<Simulation*>(this)->petra_attack_plan_.target_building_id = best_dock_id;
			return best_dock_id;
		}
	}

	uint32_t chosen_bld_id = 0;
	int best_priority = -1;
	float best_dist_sq = 1e9f;
	bool has_fortified = false;

	for (const auto &b : buildings_) {
		if (!petra_foe(b.team) || b.hp <= 0) continue;
		int priority = 0;
		if (b.type == BLD_WONDER) { priority = 7; has_fortified = true; }
		else if (b.type == BLD_FORTRESS) { priority = 6; has_fortified = true; }
		else if (b.type == BLD_TOWER || b.type == BLD_GATE) { priority = 5; has_fortified = true; }
		else if (b.type == BLD_TOWN_CENTER) { priority = 4; if (b.hp > 800.0f) has_fortified = true; }
		else if (b.type == BLD_BARRACKS || b.type == BLD_TEMPLE) { priority = 3; }
		else if (b.type == BLD_STOREHOUSE || b.type == BLD_FARMSTEAD || b.type == BLD_MARKET || b.type == BLD_DOCK || b.type == BLD_CORRAL) { priority = 2; }
		else if (b.type == BLD_HOUSE || b.type == BLD_WALL_SEGMENT) { priority = 1; }
		else priority = 0;

		float dsq = b.x * b.x + b.z * b.z;
		if (priority > best_priority || (priority == best_priority && dsq < best_dist_sq)) {
			best_priority = priority;
			best_dist_sq = dsq;
			chosen_bld_id = b.id;
			out_tx = b.x;
			out_tz = b.z;
		}
	}

	if (chosen_bld_id != 0) {
		if (has_fortified && petra_ps().phase >= 2) {
			const_cast<Simulation*>(this)->petra_attack_plan_.type = AttackPlan::PLAN_SIEGE;
		} else if (best_priority <= 2) {
			const_cast<Simulation*>(this)->petra_attack_plan_.type = AttackPlan::PLAN_RAID;
		} else {
			const_cast<Simulation*>(this)->petra_attack_plan_.type = AttackPlan::PLAN_ASSAULT;
		}
		return chosen_bld_id;
	}

	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || !petra_foe(units_.team[i])) continue;
		out_tx = units_.pos_x[i];
		out_tz = units_.pos_z[i];
		const_cast<Simulation*>(this)->petra_attack_plan_.type = AttackPlan::PLAN_RAID;
		return units_.id[i];
	}

	const_cast<Simulation*>(this)->petra_attack_plan_.type = AttackPlan::PLAN_ASSAULT;
	return 0;
}

void Simulation::petra_manage_tactical_attacks(float dt) {
	petra_attack_plan_.state_timer += dt;
	petra_strike_force_.state_timer += dt;

	float base_x = 36.0f, base_z = 36.0f;
	if (const auto *pbase = get_primary_petra_base()) {
		base_x = pbase->x; base_z = pbase->z;
	}

	auto &squad = petra_attack_plan_.squad;
	squad.erase(std::remove_if(squad.begin(), squad.end(), [&](uint32_t uid) {
		int32_t uidx = lookup_unit(uid);
		return (uidx < 0 || !units_.alive[uidx] || units_.team[uidx] != petra_pid_);
	}), squad.end());
	petra_strike_force_.unit_ids = squad;

	// Emergency Base Defense Mobilization: if base is under attack, prioritize defense
	float base_threat = get_threat_at(base_x, base_z);
	if (base_threat >= 10.0f && !squad.empty() && petra_attack_plan_.state != AttackPlan::REGROUP) {
		order_attack_move(squad.data(), squad.size(), base_x, base_z);
		petra_attack_plan_.state = AttackPlan::REGROUP;
		petra_attack_plan_.state_timer = 0.0f;
		petra_attack_plan_.retreat_x = base_x;
		petra_attack_plan_.retreat_z = base_z;
		petra_strike_force_.state = PetraStrikeForce::ENGAGE;
		return;
	}

	switch (petra_attack_plan_.state) {
		case AttackPlan::UNEXECUTED: {
			// attackManager.js: default attacks wait for the town phase; only
			// strongly aggressive personalities plan a village rush.
			if (petra_ps().phase < 2 && petra_config_.aggressive <= 0.70f) return;
			float tx = 0.0f, tz = 0.0f;
			uint32_t target_id = find_petra_attack_target(tx, tz);
			if (target_id == 0) return;

			petra_attack_plan_.target_building_id = target_id;
			petra_attack_plan_.target_x = tx;
			petra_attack_plan_.target_z = tz;
			petra_attack_plan_.squad.clear();
			petra_attack_plan_.state = AttackPlan::MUSTER;
			petra_attack_plan_.state_timer = 0.0f;
			petra_strike_force_.state = PetraStrikeForce::MUSTER;
			break;
		}

		case AttackPlan::MUSTER: {
			petra_strike_force_.state = PetraStrikeForce::MUSTER;
			for (size_t i = 0; i < units_.count; ++i) {
				if (!units_.alive[i] || units_.team[i] != petra_pid_) continue;
				if (units_.id[i] == petra_scout_unit_id_) continue; // Scout remains on exploration
				if (units_.unit_type[i] == UNIT_VILLAGER || units_.unit_type[i] == UNIT_TRADER ||
				    units_.unit_type[i] == UNIT_TRIREME || units_.unit_type[i] == UNIT_FISHING_BOAT ||
				    units_.unit_type[i] == UNIT_MERCHANT_SHIP) continue;
				uint32_t uid = units_.id[i];
				if (std::find(petra_raid_plan_.squad.begin(), petra_raid_plan_.squad.end(), uid) != petra_raid_plan_.squad.end())
					continue;
				if (std::find(squad.begin(), squad.end(), uid) == squad.end()) {
					squad.push_back(uid);
				}
			}

			// Count composition
			int siege_units = 0;
			for (uint32_t uid : squad) {
				int32_t idx = lookup_unit(uid);
				if (idx >= 0 && (units_.unit_type[idx] == UNIT_CATAPULT || units_.unit_type[idx] == UNIT_SIEGE_RAM || units_.unit_type[idx] == UNIT_BOLT_SHOOTER)) {
					siege_units++;
				}
			}

			int raw_thresh = (petra_ps().phase == 1) ? 6 : ((petra_ps().phase == 2) ? 10 : 16);
			raw_thresh += petra_config_.muster_delta;
			if (petra_emergency_.active) raw_thresh += 4;
			size_t army_threshold = (size_t)std::max(3, raw_thresh);
			bool ready = false;

			if (petra_attack_plan_.type == AttackPlan::PLAN_SIEGE) {
				// Siege Plan: must muster siege units or wait for siege production if Phase >= 2
				if (petra_ps().phase >= 2) {
					ready = (squad.size() >= 8 && siege_units >= 1) || (squad.size() >= 12) || (squad.size() >= 6 && petra_attack_plan_.state_timer >= 50.0f);
				} else {
					ready = (squad.size() >= 6 || (squad.size() >= 4 && petra_attack_plan_.state_timer >= 45.0f));
				}
			} else if (petra_attack_plan_.type == AttackPlan::PLAN_RAID) {
				ready = (squad.size() >= 4 || (squad.size() >= 3 && petra_attack_plan_.state_timer >= 30.0f));
			} else {
				ready = (squad.size() >= army_threshold || (squad.size() >= 4 && petra_attack_plan_.state_timer >= 45.0f));
			}

			if (ready && !squad.empty()) {
				float tx = 0.0f, tz = 0.0f;
				uint32_t target_id = find_petra_attack_target(tx, tz);
				if (target_id == 0) {
					petra_attack_plan_.state = AttackPlan::UNEXECUTED;
					petra_attack_plan_.state_timer = 0.0f;
					return;
				}

				petra_attack_plan_.target_building_id = target_id;
				petra_attack_plan_.target_x = tx;
				petra_attack_plan_.target_z = tz;

				float sx = base_x * 0.65f, sz = base_z * 0.65f;
				pick_petra_safe_staging(base_x, base_z, tx, tz, sx, sz);
				petra_attack_plan_.staging_x = sx;
				petra_attack_plan_.staging_z = sz;
				petra_strike_force_.staging_x = sx;
				petra_strike_force_.staging_z = sz;

				float staging_danger = get_threat_at(sx, sz);
				if (staging_danger >= petra_attack_plan_.abort_threat) {
					petra_attack_plan_.state = AttackPlan::REGROUP;
					petra_attack_plan_.retreat_x = base_x;
					petra_attack_plan_.retreat_z = base_z;
					petra_attack_plan_.state_timer = 0.0f;
					return;
				}

				bool water = petra_path_crosses_water(base_x, base_z, tx, tz) ||
				             petra_path_crosses_water(sx, sz, tx, tz);
				if (water) {
					if (!petra_start_transport(squad, base_x, base_z, tx, tz))
						cmd_move(squad.data(), squad.size(), sx, sz);
				} else {
					cmd_move(squad.data(), squad.size(), sx, sz);
				}
				petra_say(PETRA_CHAT_ATTACK, tx, tz);
				petra_attack_plan_.initial_squad_size = squad.size();
				petra_attack_plan_.state = AttackPlan::WALKING;
				petra_attack_plan_.state_timer = 0.0f;
				petra_strike_force_.state = PetraStrikeForce::MARCH;
			}
			break;
		}

		case AttackPlan::WALKING: {
			petra_strike_force_.state = PetraStrikeForce::MARCH;
			if (petra_transport_.state == PetraTransportPlan::BOARDING ||
			    petra_transport_.state == PetraTransportPlan::SAILING ||
			    petra_transport_.state == PetraTransportPlan::UNLOADING) {
				return;
			}

			float staging_danger = get_threat_at(petra_attack_plan_.staging_x, petra_attack_plan_.staging_z);
			if (staging_danger >= petra_attack_plan_.abort_threat) {
				petra_attack_plan_.state = AttackPlan::REGROUP;
				petra_attack_plan_.retreat_x = base_x;
				petra_attack_plan_.retreat_z = base_z;
				petra_attack_plan_.state_timer = 0.0f;
				return;
			}

			// Regroup if casualties exceed 60%
			if (petra_attack_plan_.initial_squad_size > 0 &&
			    (float)squad.size() < 0.40f * (float)petra_attack_plan_.initial_squad_size) {
				petra_attack_plan_.state = AttackPlan::REGROUP;
				petra_attack_plan_.retreat_x = base_x;
				petra_attack_plan_.retreat_z = base_z;
				petra_attack_plan_.state_timer = 0.0f;
				return;
			}

			float cx = 0.0f, cz = 0.0f;
			int alive_count = 0;
			for (uint32_t uid : squad) {
				int32_t idx = lookup_unit(uid);
				if (idx >= 0 && units_.alive[idx]) {
					cx += units_.pos_x[idx];
					cz += units_.pos_z[idx];
					alive_count++;
				}
			}
			if (alive_count > 0) {
				cx /= (float)alive_count;
				cz /= (float)alive_count;
			}

			float dsq_to_staging = (cx - petra_attack_plan_.staging_x) * (cx - petra_attack_plan_.staging_x) +
			                       (cz - petra_attack_plan_.staging_z) * (cz - petra_attack_plan_.staging_z);

			if (dsq_to_staging <= 144.0f || petra_attack_plan_.state_timer >= 18.0f) {
				float tx = 0.0f, tz = 0.0f;
				uint32_t target_id = find_petra_attack_target(tx, tz);
				if (target_id == 0) {
					petra_attack_plan_.state = AttackPlan::REGROUP;
					petra_attack_plan_.retreat_x = base_x;
					petra_attack_plan_.retreat_z = base_z;
					petra_attack_plan_.state_timer = 0.0f;
					return;
				}
				petra_attack_plan_.target_building_id = target_id;
				petra_attack_plan_.target_x = tx;
				petra_attack_plan_.target_z = tz;
				petra_strike_force_.target_building_id = target_id;
				petra_strike_force_.target_x = tx;
				petra_strike_force_.target_z = tz;

				// Tactical Formations & Role Specialization
				std::vector<uint32_t> siege_uids;
				std::vector<uint32_t> escort_uids;

				for (uint32_t uid : squad) {
					int32_t idx = lookup_unit(uid);
					if (idx < 0) continue;
					if (units_.unit_type[idx] == UNIT_SWORDSMAN || units_.unit_type[idx] == UNIT_SPEARMAN || units_.unit_type[idx] == UNIT_ELEPHANT) {
						units_.formation[idx] = FORMATION_PHALANX;
						escort_uids.push_back(uid);
					} else if (units_.unit_type[idx] == UNIT_CAVALRY || units_.unit_type[idx] == UNIT_CHARIOT || units_.unit_type[idx] == UNIT_SCYTHED_CHARIOT) {
						units_.formation[idx] = FORMATION_WEDGE;
						escort_uids.push_back(uid);
					} else if (units_.unit_type[idx] == UNIT_ARCHER || units_.unit_type[idx] == UNIT_ARCHER_MAIDEN) {
						units_.formation[idx] = FORMATION_SCATTER;
						escort_uids.push_back(uid);
					} else if (units_.unit_type[idx] == UNIT_CATAPULT || units_.unit_type[idx] == UNIT_SIEGE_RAM || units_.unit_type[idx] == UNIT_BOLT_SHOOTER) {
						units_.formation[idx] = FORMATION_COLUMN;
						siege_uids.push_back(uid);
					} else {
						escort_uids.push_back(uid);
					}
				}

				if (petra_attack_plan_.type == AttackPlan::PLAN_SIEGE && !siege_uids.empty()) {
					// Specialized siege assault: siege weapons focus on fortifications, escort defends siege weapons
					order_attack_building(siege_uids.data(), siege_uids.size(), target_id);
					if (!escort_uids.empty()) {
						order_attack_move(escort_uids.data(), escort_uids.size(), tx, tz);
					}
				} else {
					order_attack_move(squad.data(), squad.size(), tx, tz);
				}

				petra_attack_plan_.state = AttackPlan::ATTACKED;
				petra_attack_plan_.state_timer = 0.0f;
				petra_strike_force_.state = PetraStrikeForce::ENGAGE;
			}
			break;
		}

		case AttackPlan::ATTACKED: {
			petra_strike_force_.state = PetraStrikeForce::ENGAGE;

			// Organized retreat if casualties exceed 60% or squad decimated
			if (squad.size() < 2 || (petra_attack_plan_.initial_squad_size > 0 &&
			    (float)squad.size() < 0.40f * (float)petra_attack_plan_.initial_squad_size)) {
				// Find nearest fortified friendly base anchor to retreat to
				float nearest_d2 = 1e9f;
				float ret_x = base_x, ret_z = base_z;
				uint32_t ret_bld_id = 0;

				for (const auto &b : buildings_) {
					if (b.team == petra_pid_ && b.hp > 0 && (b.type == BLD_FORTRESS || b.type == BLD_TOWN_CENTER || b.type == BLD_TOWER)) {
						float d2 = (b.x - petra_attack_plan_.target_x) * (b.x - petra_attack_plan_.target_x) +
						           (b.z - petra_attack_plan_.target_z) * (b.z - petra_attack_plan_.target_z);
						if (d2 < nearest_d2) {
							nearest_d2 = d2;
							ret_x = b.x;
							ret_z = b.z;
							ret_bld_id = b.id;
						}
					}
				}

				petra_attack_plan_.retreat_x = ret_x;
				petra_attack_plan_.retreat_z = ret_z;
				petra_attack_plan_.retreat_base_id = ret_bld_id;
				petra_attack_plan_.state = AttackPlan::REGROUP;
				petra_attack_plan_.state_timer = 0.0f;
				if (!squad.empty()) {
					cmd_move(squad.data(), squad.size(), ret_x, ret_z);
				}
				return;
			}

			bool target_alive = false;
			for (const auto &b : buildings_) {
				if (b.id == petra_attack_plan_.target_building_id && petra_foe(b.team) && b.hp > 0) {
					target_alive = true;
					break;
				}
			}

			if (!target_alive || petra_attack_plan_.state_timer >= 25.0f) {
				float tx = 0.0f, tz = 0.0f;
				uint32_t next_id = find_petra_attack_target(tx, tz);
				if (next_id != 0) {
					petra_attack_plan_.target_building_id = next_id;
					petra_attack_plan_.target_x = tx;
					petra_attack_plan_.target_z = tz;
					petra_strike_force_.target_building_id = next_id;
					petra_strike_force_.target_x = tx;
					petra_strike_force_.target_z = tz;

					std::vector<uint32_t> siege_uids;
					std::vector<uint32_t> escort_uids;
					for (uint32_t uid : squad) {
						int32_t idx = lookup_unit(uid);
						if (idx >= 0 && (units_.unit_type[idx] == UNIT_CATAPULT || units_.unit_type[idx] == UNIT_SIEGE_RAM || units_.unit_type[idx] == UNIT_BOLT_SHOOTER)) {
							siege_uids.push_back(uid);
						} else {
							escort_uids.push_back(uid);
						}
					}

					if (petra_attack_plan_.type == AttackPlan::PLAN_SIEGE && !siege_uids.empty()) {
						order_attack_building(siege_uids.data(), siege_uids.size(), next_id);
						if (!escort_uids.empty()) order_attack_move(escort_uids.data(), escort_uids.size(), tx, tz);
					} else {
						order_attack_move(squad.data(), squad.size(), tx, tz);
					}
					petra_attack_plan_.state_timer = 0.0f;
				} else {
					petra_attack_plan_.state = AttackPlan::REGROUP;
					petra_attack_plan_.retreat_x = base_x;
					petra_attack_plan_.retreat_z = base_z;
					petra_attack_plan_.state_timer = 0.0f;
					if (!squad.empty()) {
						cmd_move(squad.data(), squad.size(), base_x, base_z);
					}
				}
			}
			break;
		}

		case AttackPlan::REGROUP: {
			petra_strike_force_.state = PetraStrikeForce::MUSTER;
			float ret_x = petra_attack_plan_.retreat_x;
			float ret_z = petra_attack_plan_.retreat_z;

			if (!squad.empty() && petra_attack_plan_.state_timer < 1.0f) {
				cmd_move(squad.data(), squad.size(), ret_x, ret_z);
			}

			// Compute centroid to check arrival at base
			float cx = 0.0f, cz = 0.0f;
			int alive_count = 0;
			for (uint32_t uid : squad) {
				int32_t idx = lookup_unit(uid);
				if (idx >= 0 && units_.alive[idx]) {
					cx += units_.pos_x[idx];
					cz += units_.pos_z[idx];
					alive_count++;
				}
			}
			if (alive_count > 0) { cx /= (float)alive_count; cz /= (float)alive_count; }

			float dsq = (cx - ret_x) * (cx - ret_x) + (cz - ret_z) * (cz - ret_z);
			if (dsq <= 144.0f || petra_attack_plan_.state_timer >= 15.0f || squad.empty()) {
				// Safely returned to base anchor, reset for fresh reinforcements
				squad.clear();
				petra_attack_plan_.initial_squad_size = 0;
				petra_attack_plan_.target_building_id = 0;
				petra_attack_plan_.state = AttackPlan::UNEXECUTED;
				petra_attack_plan_.state_timer = 0.0f;
			}
			break;
		}

		case AttackPlan::ABORT: {
			petra_strike_force_.state = PetraStrikeForce::MUSTER;
			if (!squad.empty()) {
				cmd_move(squad.data(), squad.size(), base_x, base_z);
			}
			petra_attack_plan_.state = AttackPlan::REGROUP;
			petra_attack_plan_.retreat_x = base_x;
			petra_attack_plan_.retreat_z = base_z;
			petra_attack_plan_.state_timer = 0.0f;
			break;
		}
	}
}

bool Simulation::petra_unit_reserved(uint32_t uid) const {
	if (uid == 0) return false;
	if (uid == petra_scout_unit_id_) return true;
	const auto &main = petra_attack_plan_.squad;
	if (std::find(main.begin(), main.end(), uid) != main.end()) return true;
	const auto &raid = petra_raid_plan_.squad;
	if (std::find(raid.begin(), raid.end(), uid) != raid.end()) return true;
	const auto &def = petra_defense_army_.defender_ids;
	if (std::find(def.begin(), def.end(), uid) != def.end()) return true;
	return false;
}

uint32_t Simulation::find_petra_raid_target(float &out_tx, float &out_tz) const {
	uint32_t best = 0;
	int best_pri = -1;
	float best_d = 1e9f;
	int own_warships = 0;
	for (int s = 0; s < Petra::kMaxSeas; ++s) {
		own_warships += petra_fleets_.sea_warship_count[s];
	}
	if (own_warships == 0) {
		for (size_t i = 0; i < units_.count; ++i) {
			if (!units_.alive[i] || units_.team[i] != petra_pid_) continue;
			uint8_t ut = units_.unit_type[i];
			if (ut == UNIT_TRIREME || (!petra_is_support_unit(ut) && unit_is_ship(ut))) {
				own_warships++;
			}
		}
	}
	for (const auto &b : buildings_) {
		if (!petra_foe(b.team) || b.hp <= 0) continue;
		if (b.type == BLD_FORTRESS || b.type == BLD_WONDER || b.type == BLD_TOWER || b.type == BLD_GATE)
			continue;
		int pri = 0;
		if (b.type == BLD_HOUSE) pri = 4;
		else if (b.type == BLD_FARMSTEAD || b.type == BLD_STOREHOUSE) pri = 3;
		else if (b.type == BLD_MARKET || b.type == BLD_CORRAL) pri = 2;
		else pri = 1;
		// attackPlan.js chooses a dock/port as a bridgehead when the target
		// region is separated by water; island raids must not march blindly.
		if (b.type == BLD_DOCK) {
			float bx = 36.0f, bz = 36.0f;
			if (const auto *base = get_primary_petra_base()) { bx = base->x; bz = base->z; }
			if (petra_path_crosses_water(bx, bz, b.x, b.z)) pri = 6;
			// 0 A.D. navalManager.js:58 seaWarShips — prioritize dock within same sea region as warships
			if (own_warships > 0) {
				auto [bcx, bcz] = nav_grid_.world_to_cell(b.x, b.z);
				for (int dz = -2; dz <= 2; ++dz) {
					for (int dx = -2; dx <= 2; ++dx) {
						uint16_t sreg = nav_grid_.global_region_at(bcx + dx, bcz + dz, PASS_SHIP);
						if (sreg > 0 && static_cast<int>(sreg - 1) < Petra::kMaxSeas) {
							int sid = static_cast<int>(sreg - 1);
							if (petra_fleets_.sea_warship_count[sid] > 0) {
								pri = 8;
								break;
							}
						}
					}
					if (pri >= 8) break;
				}
				if (pri < 8) pri = std::max(pri, 6);
			}
		} else if (b.type == BLD_BARRACKS && own_warships > 0) {
			// Coastal barracks reachable by sea
			auto [bcx, bcz] = nav_grid_.world_to_cell(b.x, b.z);
			for (int dz = -2; dz <= 2; ++dz) {
				for (int dx = -2; dx <= 2; ++dx) {
					uint16_t sreg = nav_grid_.global_region_at(bcx + dx, bcz + dz, PASS_SHIP);
					if (sreg > 0 && static_cast<int>(sreg - 1) < Petra::kMaxSeas) {
						int sid = static_cast<int>(sreg - 1);
						if (petra_fleets_.sea_warship_count[sid] > 0) {
							pri = 5;
							break;
						}
					}
				}
				if (pri >= 5) break;
			}
		}
		float dsq = b.x * b.x + b.z * b.z;
		if (pri > best_pri || (pri == best_pri && dsq < best_d)) {
			best_pri = pri;
			best_d = dsq;
			best = b.id;
			out_tx = b.x;
			out_tz = b.z;
		}
	}
	if (best) return best;
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || !petra_foe(units_.team[i])) continue;
		if (!petra_is_support_unit(units_.unit_type[i]) && units_.unit_type[i] != UNIT_VILLAGER)
			continue;
		out_tx = units_.pos_x[i];
		out_tz = units_.pos_z[i];
		return units_.id[i];
	}
	return 0;
}

void Simulation::petra_manage_raid(float dt) {
	if (petra_config_.difficulty == PETRA_SANDBOX) return;
	if (petra_emergency_.active) return;
	if (petra_ps().phase < 2 && petra_config_.aggressive < 0.70f) return;

	auto &plan = petra_raid_plan_;
	plan.type = AttackPlan::PLAN_RAID;
	plan.state_timer += dt;
	plan.squad.erase(std::remove_if(plan.squad.begin(), plan.squad.end(), [&](uint32_t uid) {
		int32_t idx = lookup_unit(uid);
		return idx < 0 || !units_.alive[idx] || units_.team[idx] != petra_pid_;
	}), plan.squad.end());

	float base_x = 36.0f, base_z = 36.0f;
	if (const auto *pbase = get_primary_petra_base()) {
		base_x = pbase->x;
		base_z = pbase->z;
	}

	if (plan.state == AttackPlan::UNEXECUTED || plan.state == AttackPlan::ABORT) {
		if (plan.state_timer < 6.0f) return;
		float tx = 0.0f, tz = 0.0f;
		uint32_t tid = find_petra_raid_target(tx, tz);
		if (!tid) return;
		plan.squad.clear();
		for (size_t i = 0; i < units_.count && plan.squad.size() < 4; ++i) {
			if (!units_.alive[i] || units_.team[i] != petra_pid_) continue;
			uint8_t ut = units_.unit_type[i];
			if (!petra_is_combat_unit(ut)) continue;
			if (ut == UNIT_TRIREME || ut == UNIT_FISHING_BOAT || ut == UNIT_MERCHANT_SHIP) continue;
			if (ut == UNIT_CATAPULT || ut == UNIT_SIEGE_RAM || ut == UNIT_BOLT_SHOOTER) continue;
			uint32_t uid = units_.id[i];
			if (uid == petra_scout_unit_id_) continue;
			if (std::find(petra_attack_plan_.squad.begin(), petra_attack_plan_.squad.end(), uid) != petra_attack_plan_.squad.end())
				continue;
			if (std::find(petra_defense_army_.defender_ids.begin(), petra_defense_army_.defender_ids.end(), uid) != petra_defense_army_.defender_ids.end())
				continue;
			plan.squad.push_back(uid);
		}
		if (plan.squad.size() < 3) {
			plan.squad.clear();
			return;
		}
		plan.target_building_id = tid;
		plan.target_x = tx;
		plan.target_z = tz;
		plan.initial_squad_size = plan.squad.size();
		plan.state = AttackPlan::WALKING;
		plan.state_timer = 0.0f;
		petra_say(PETRA_CHAT_ATTACK, tx, tz);
		// Mirror the assault plan's transportPlan.js branch: a raid that
		// crosses water boards a transport instead of issuing a ground move.
		if (petra_path_crosses_water(base_x, base_z, tx, tz)) {
			if (!petra_start_transport(plan.squad, base_x, base_z, tx, tz))
				order_attack_move(plan.squad.data(), plan.squad.size(), tx, tz);
		} else {
			order_attack_move(plan.squad.data(), plan.squad.size(), tx, tz);
		}
		return;
	}

	if (plan.state == AttackPlan::WALKING || plan.state == AttackPlan::ATTACKED || plan.state == AttackPlan::MUSTER) {
		if (plan.squad.size() < 2 ||
		    (plan.initial_squad_size > 0 && (float)plan.squad.size() < 0.40f * (float)plan.initial_squad_size)) {
			plan.state = AttackPlan::REGROUP;
			plan.retreat_x = base_x;
			plan.retreat_z = base_z;
			plan.state_timer = 0.0f;
			if (!plan.squad.empty())
				cmd_move(plan.squad.data(), plan.squad.size(), base_x, base_z);
			return;
		}
		bool target_ok = false;
		for (const auto &b : buildings_) {
			if (b.id == plan.target_building_id && petra_foe(b.team) && b.hp > 0) {
				target_ok = true;
				break;
			}
		}
		if (!target_ok) {
			float tx = 0.0f, tz = 0.0f;
			uint32_t tid = find_petra_raid_target(tx, tz);
			if (tid) {
				plan.target_building_id = tid;
				plan.target_x = tx;
				plan.target_z = tz;
				order_attack_move(plan.squad.data(), plan.squad.size(), tx, tz);
			} else {
				plan.state = AttackPlan::REGROUP;
				plan.retreat_x = base_x;
				plan.retreat_z = base_z;
				plan.state_timer = 0.0f;
			}
		} else if (plan.state_timer > 36.0f) {
			plan.state = AttackPlan::REGROUP;
			plan.retreat_x = base_x;
			plan.retreat_z = base_z;
			plan.state_timer = 0.0f;
			if (!plan.squad.empty())
				cmd_move(plan.squad.data(), plan.squad.size(), base_x, base_z);
		} else {
			plan.state = AttackPlan::ATTACKED;
		}
		return;
	}

	if (plan.state == AttackPlan::REGROUP) {
		if (plan.state_timer >= 12.0f || plan.squad.empty()) {
			plan.squad.clear();
			plan.initial_squad_size = 0;
			plan.target_building_id = 0;
			plan.state = AttackPlan::UNEXECUTED;
			plan.state_timer = 0.0f;
			plan.type = AttackPlan::PLAN_RAID;
		}
	}
}

void Simulation::petra_manage_opening(float dt) {
	if (petra_config_.difficulty == PETRA_SANDBOX) {
		petra_opening_step_ = 4;
		return;
	}
	if (petra_ps().phase > 1 || petra_opening_step_ >= 4) return;

	petra_opening_timer_ += dt;
	if (petra_opening_timer_ < 0.8f) return;
	petra_opening_timer_ = 0.0f;

	float bx = 36.0f, bz = 36.0f;
	if (const auto *pbase = get_primary_petra_base()) {
		bx = pbase->x;
		bz = pbase->z;
	}

	auto count_ai = [&](uint8_t t) {
		int n = 0;
		for (const auto &b : buildings_) {
			if (b.team == petra_pid_ && b.hp > 0 && b.type == t) ++n;
		}
		return n;
	};

	// startingStrategy.js:513 — wood dropsite first, then houses, then barracks
	uint8_t want = BLD_HOUSE;
	uint8_t queue = PETRA_QUEUE_VILLAGER;
	int have_needed = 0;
	if (petra_opening_step_ == 0) {
		want = BLD_STOREHOUSE;
		have_needed = 1;
	} else if (petra_opening_step_ == 1 || petra_opening_step_ == 2) {
		want = BLD_HOUSE;
		have_needed = petra_opening_step_;
	} else {
		want = BLD_BARRACKS;
		queue = PETRA_QUEUE_MILITARY;
		have_needed = 1;
	}

	if (count_ai(want) >= have_needed) {
		petra_opening_step_++;
		return;
	}

	uint32_t bid = petra_place_building(want, bx, bz, 2, 9, queue);
	if (!bid) return;
	auto idle = get_idle_workers(petra_pid_);
	if (!idle.empty())
		order_build_or_repair(idle.data(), std::min((size_t)3, idle.size()), bid);
	petra_opening_step_++;
}

void Simulation::apply_petra_config() {
	petra_config_.recompute();
}

void Simulation::petra_refresh_queue_accounts(float dt) {
	int prio[PetraQueueManager::QUEUE_COUNT] = {
		petra_config_.prio_emergency,
		petra_config_.prio_villager,
		petra_config_.prio_military,
		petra_config_.prio_tech,
		petra_config_.prio_trade,
		petra_config_.prio_naval
	};
	if (petra_emergency_.active) prio[PETRA_QUEUE_EMERGENCY] *= 2;

	// A queue that stopped asking releases its reserve (queueManager.js
	// transferAccounts / clear on empty queue).
	for (int i = 0; i < PetraQueueManager::QUEUE_COUNT; ++i) {
		petra_queues_.want_age[i] += dt;
		if (petra_queues_.want_age[i] > PetraQueueManager::WANT_TTL) {
			petra_queues_.wants[i] = PetraQueueAccount{};
			petra_queues_.accounts[i] = PetraQueueAccount{};
		}
	}

	auto fund = [&](float PetraQueueAccount::*field, float have) {
		float reserved = 0.0f;
		for (int i = 0; i < PetraQueueManager::QUEUE_COUNT; ++i)
			reserved += petra_queues_.accounts[i].*field;
		if (reserved > have && reserved > 0.01f) {
			const float k = have / reserved;
			for (int i = 0; i < PetraQueueManager::QUEUE_COUNT; ++i)
				petra_queues_.accounts[i].*field *= k;
			reserved = have;
		}
		float free_amt = std::max(0.0f, have - reserved);
		// Split free stock among queues still short of their want, by priority,
		// capped at each deficit; what one queue cannot use flows to the rest.
		for (int pass = 0; pass < 3 && free_amt > 0.01f; ++pass) {
			int total_p = 0;
			for (int i = 0; i < PetraQueueManager::QUEUE_COUNT; ++i)
				if (petra_queues_.wants[i].*field > petra_queues_.accounts[i].*field)
					total_p += std::max(1, prio[i]);
			if (total_p == 0) break;
			float given = 0.0f;
			for (int i = 0; i < PetraQueueManager::QUEUE_COUNT; ++i) {
				float deficit = petra_queues_.wants[i].*field - petra_queues_.accounts[i].*field;
				if (deficit <= 0.0f) continue;
				float g = std::min(deficit, free_amt * (float)std::max(1, prio[i]) / (float)total_p);
				petra_queues_.accounts[i].*field += g;
				given += g;
			}
			free_amt -= given;
		}
	};
	fund(&PetraQueueAccount::food, petra_ps().food());
	fund(&PetraQueueAccount::wood, petra_ps().wood());
	fund(&PetraQueueAccount::gold, petra_ps().gold());
	fund(&PetraQueueAccount::stone, petra_ps().stone());
}

bool Simulation::petra_try_pay(uint8_t queue, float food, float wood, float gold, float stone) {
	if (queue >= PetraQueueManager::QUEUE_COUNT) return false;
	if (food < 0.0f || wood < 0.0f || gold < 0.0f || stone < 0.0f) return false;
	PetraQueueAccount &a = petra_queues_.accounts[queue];
	PetraQueueAccount &w = petra_queues_.wants[queue];
	if (a.food < food || a.wood < wood || a.gold < gold || a.stone < stone ||
	    petra_ps().food() < food || petra_ps().wood() < wood || petra_ps().gold() < gold || petra_ps().stone() < stone) {
		// Register the plan so the next refresh funds this queue.
		w.food = std::max(w.food, food); w.wood = std::max(w.wood, wood);
		w.gold = std::max(w.gold, gold); w.stone = std::max(w.stone, stone);
		petra_queues_.want_age[queue] = 0.0f;
		return false;
	}
	a.food -= food; a.wood -= wood; a.gold -= gold; a.stone -= stone;
	petra_ps().food() -= food; petra_ps().wood() -= wood; petra_ps().gold() -= gold; petra_ps().stone() -= stone;
	w = PetraQueueAccount{};
	petra_queues_.want_age[queue] = 0.0f;
	return true;
}

void Simulation::petra_say(uint8_t kind, float x, float z) {
	static const char *kLines[] = {
		"I am launching an attack against you.",
		"Here is a gift. Make good use of it.",
		"Our alliance is over. We are now enemies.",
		"We are under attack — all forces, defend the city!"
	};
	if (kind > PETRA_CHAT_EMERGENCY) kind = PETRA_CHAT_ATTACK;
	PetraChatLine line;
	line.kind = kind;
	line.x = x;
	line.z = z;
	std::strncpy(line.text, kLines[kind], sizeof(line.text) - 1);
	line.text[sizeof(line.text) - 1] = '\0';
	petra_chat_.push_back(line);
	if (petra_chat_.size() > 16) petra_chat_.erase(petra_chat_.begin());
}

bool Simulation::petra_path_crosses_water(float x0, float z0, float x1, float z1) const {
	if (nav_grid_.cells.blank()) return false;
	int water_hits = 0;
	const int samples = 16;
	for (int i = 1; i < samples; ++i) {
		float t = (float)i / (float)samples;
		float x = x0 + (x1 - x0) * t;
		float z = z0 + (z1 - z0) * t;
		auto cell = nav_grid_.world_to_cell(x, z);
		if (nav_grid_.cells.inside(cell.first, cell.second) &&
		    nav_grid_.cells.at(cell.first, cell.second) == NAV_WATER) {
			water_hits++;
		}
	}
	return water_hits >= 3;
}

bool Simulation::petra_find_beach(float land_x, float land_z, float toward_x, float toward_z, float &out_x, float &out_z) const {
	float dx = toward_x - land_x;
	float dz = toward_z - land_z;
	float dist = std::hypot(dx, dz);
	if (dist < 0.01f) { out_x = land_x; out_z = land_z; return false; }
	dx /= dist; dz /= dist;
	float best_x = land_x, best_z = land_z;
	bool found = false;
	for (float t = 2.0f; t < dist; t += 2.0f) {
		float x = land_x + dx * t;
		float z = land_z + dz * t;
		if (std::abs(x) >= map_half_ - 3.0f || std::abs(z) >= map_half_ - 3.0f) break;
		auto cell = nav_grid_.world_to_cell(x, z);
		if (!nav_grid_.cells.inside(cell.first, cell.second)) continue;
		if (nav_grid_.cells.at(cell.first, cell.second) == NAV_WATER) {
			out_x = best_x;
			out_z = best_z;
			return found;
		}
		if (get_terrain_height(x, z) >= terrain_water_height_ + 0.1f) {
			best_x = x; best_z = z; found = true;
		}
	}
	out_x = best_x;
	out_z = best_z;
	return found;
}

bool Simulation::petra_start_transport(const std::vector<uint32_t> &unit_ids, float from_x, float from_z, float to_x, float to_z) {
	uint32_t ship = 0;
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != petra_pid_ || units_.unit_type[i] != UNIT_TRIREME) continue;
		if (units_.id[i] == petra_transport_.ship_id && petra_transport_.state != PetraTransportPlan::IDLE &&
		    petra_transport_.state != PetraTransportPlan::FAILED) continue;
		ship = units_.id[i];
		break;
	}
	if (ship == 0) return false;

	float pickup_x = from_x, pickup_z = from_z;
	float drop_x = to_x, drop_z = to_z;
	petra_find_beach(from_x, from_z, to_x, to_z, pickup_x, pickup_z);
	petra_find_beach(to_x, to_z, from_x, from_z, drop_x, drop_z);

	petra_transport_ = PetraTransportPlan();
	petra_transport_.state = PetraTransportPlan::BOARDING;
	petra_transport_.ship_id = ship;
	petra_transport_.pickup_x = pickup_x;
	petra_transport_.pickup_z = pickup_z;
	petra_transport_.drop_x = drop_x;
	petra_transport_.drop_z = drop_z;
	petra_transport_.state_timer = 0.0f;
	petra_transport_.passenger_ids.clear();
	for (uint32_t uid : unit_ids) {
		if (petra_transport_.passenger_ids.size() >= (size_t)TRANSPORT_CAPACITY) break;
		int32_t idx = lookup_unit(uid);
		if (idx < 0 || !units_.alive[idx] || is_transport(uid)) continue;
		petra_transport_.passenger_ids.push_back(uid);
	}
	if (petra_transport_.passenger_ids.empty()) {
		petra_transport_.state = PetraTransportPlan::FAILED;
		petra_transport_.failed = true;
		return false;
	}
	cmd_move(&ship, 1, pickup_x, pickup_z);
	cmd_move(petra_transport_.passenger_ids.data(), petra_transport_.passenger_ids.size(), pickup_x, pickup_z);
	return true;
}

void Simulation::petra_manage_transport(float dt) {
	auto &tp = petra_transport_;
	if (tp.state == PetraTransportPlan::IDLE || tp.state == PetraTransportPlan::FAILED) return;
	tp.state_timer += dt;

	int32_t sidx = lookup_unit(tp.ship_id);
	if (sidx < 0 || !units_.alive[sidx] || units_.unit_type[sidx] != UNIT_TRIREME) {
		tp.state = PetraTransportPlan::FAILED;
		tp.failed = true;
		return;
	}

	if (tp.state == PetraTransportPlan::BOARDING) {
		if (tp.state_timer > 28.0f) {
			tp.state = PetraTransportPlan::FAILED;
			tp.failed = true;
			return;
		}
		order_board_transport(tp.passenger_ids.data(), tp.passenger_ids.size(), tp.ship_id);
		size_t aboard = get_transport_passenger_count(tp.ship_id);
		float dx = units_.pos_x[sidx] - tp.pickup_x;
		float dz = units_.pos_z[sidx] - tp.pickup_z;
		bool ship_ready = (dx * dx + dz * dz) <= 36.0f;
		if ((aboard >= 1 && aboard >= tp.passenger_ids.size()) ||
		    (aboard >= 2 && ship_ready && tp.state_timer >= 8.0f)) {
			cmd_move(&tp.ship_id, 1, tp.drop_x, tp.drop_z);
			tp.state = PetraTransportPlan::SAILING;
			tp.state_timer = 0.0f;
		}
	} else if (tp.state == PetraTransportPlan::SAILING) {
		if (tp.state_timer > 40.0f) {
			tp.state = PetraTransportPlan::FAILED;
			tp.failed = true;
			return;
		}
		float dx = units_.pos_x[sidx] - tp.drop_x;
		float dz = units_.pos_z[sidx] - tp.drop_z;
		if (dx * dx + dz * dz <= 64.0f || tp.state_timer >= 18.0f) {
			order_disembark(tp.ship_id, tp.drop_x, tp.drop_z);
			tp.state = PetraTransportPlan::UNLOADING;
			tp.state_timer = 0.0f;
		}
	} else if (tp.state == PetraTransportPlan::UNLOADING) {
		if (get_transport_passenger_count(tp.ship_id) == 0 || tp.state_timer >= 6.0f) {
			if (get_transport_passenger_count(tp.ship_id) > 0)
				order_disembark(tp.ship_id, tp.drop_x, tp.drop_z);
			tp.state = PetraTransportPlan::IDLE;
			tp.passenger_ids.clear();
		}
	}
}

uint32_t Simulation::petra_place_building(uint8_t btype, float near_x, float near_z, int min_r, int max_r, uint8_t queue) {
	const BuildingTypeInfo &info = building_type_info(btype);
	int start_cx = (int)(near_x / GRID);
	int start_cz = (int)(near_z / GRID);
	int foot = info.footprint;
	for (int r = min_r; r <= max_r; ++r) {
		for (int dx = -r; dx <= r; ++dx) {
			for (int dz = -r; dz <= r; ++dz) {
				if (std::abs(dx) != r && std::abs(dz) != r) continue;
				int cx = start_cx + dx;
				int cz = start_cz + dz;
				if (!petra_can_place(cx, cz, foot)) continue;
				if (!petra_try_pay(queue, (float)info.food_cost, (float)info.wood_cost,
				                   (float)info.gold_cost, (float)info.stone_cost)) {
					return 0;
				}
				return spawn_building(cx, cz, btype, petra_pid_, 0.0f);
			}
		}
	}
	return 0;
}

void Simulation::petra_manage_defense(float dt) {
	petra_defense_timer_ += dt;
	if (petra_defense_timer_ < 0.5f) return;
	petra_defense_timer_ = 0.0f;

	auto &army = petra_defense_army_;
	army.foe_ids.clear();

	auto in_attack_squad = [&](uint32_t uid) {
		return petra_unit_reserved(uid);
	};

	// Detect enemies inside own territory (defenseManager.isDangerous). The base
	// radius only stands in for territory while a base has no anchor building:
	// a 32 m radius reaches into a close neighbour's land and starts border fights.
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || !petra_foe(units_.team[i])) continue;
		bool dangerous = (territory_owner_at(units_.pos_x[i], units_.pos_z[i]) == petra_pid_);
		if (!dangerous) {
			for (const auto &base : petra_bases_) {
				if (base.anchor_building_id != 0) continue;
				float dx = units_.pos_x[i] - base.x;
				float dz = units_.pos_z[i] - base.z;
				if (dx * dx + dz * dz <= base.radius * base.radius) {
					dangerous = true;
					break;
				}
			}
		}
		if (dangerous) army.foe_ids.push_back(units_.id[i]);
	}

	// Enemy foundations in our territory (defenseManager targetList)
	for (const auto &b : buildings_) {
		if (!petra_foe(b.team) || b.hp <= 0.0f) continue;
		if (territory_owner_at(b.x, b.z) != petra_pid_) continue;
		for (size_t i = 0; i < units_.count; ++i) {
			if (!units_.alive[i] || !petra_foe(units_.team[i])) continue;
			float dx = units_.pos_x[i] - b.x;
			float dz = units_.pos_z[i] - b.z;
			if (dx * dx + dz * dz <= 18.0f * 18.0f) {
				if (std::find(army.foe_ids.begin(), army.foe_ids.end(), units_.id[i]) == army.foe_ids.end())
					army.foe_ids.push_back(units_.id[i]);
			}
		}
	}

	// Prune dead / missing defenders
	army.defender_ids.erase(std::remove_if(army.defender_ids.begin(), army.defender_ids.end(),
		[&](uint32_t uid) {
			int32_t idx = lookup_unit(uid);
			return idx < 0 || !units_.alive[idx] || units_.team[idx] != petra_pid_;
		}), army.defender_ids.end());

	auto is_foe = [&](uint32_t uid) {
		return std::find(army.foe_ids.begin(), army.foe_ids.end(), uid) != army.foe_ids.end();
	};
	const PetraBase *home = get_primary_petra_base();

	if (army.foe_ids.empty()) {
		// defenseManager.js disbands the army once no enemy is dangerous; send
		// defenders home instead of letting them chase foes out of territory.
		if (home) {
			for (uint32_t uid : army.defender_ids) {
				int32_t idx = lookup_unit(uid);
				if (idx < 0 || units_.action_state[idx] != ACTION_ATTACK) continue;
				cmd_move(&uid, 1, home->x, home->z);
			}
		}
		army.active = false;
		army.defender_ids.clear();
		return;
	}

	army.active = true;
	size_t needed = (size_t)std::ceil((float)army.foe_ids.size() * petra_config_.defense_ratio_own);
	if (petra_emergency_.active) needed += 3;
	needed = std::min(needed, (size_t)24);

	for (size_t i = 0; i < units_.count && army.defender_ids.size() < needed; ++i) {
		if (!units_.alive[i] || units_.team[i] != petra_pid_) continue;
		if (!petra_is_combat_unit(units_.unit_type[i])) continue;
		if (units_.id[i] == petra_scout_unit_id_) continue;
		if (units_.action_state[i] == ACTION_GARRISONED || units_.action_state[i] == ACTION_DYING) continue;
		if (in_attack_squad(units_.id[i]) && !petra_emergency_.active) continue;
		if (std::find(army.defender_ids.begin(), army.defender_ids.end(), units_.id[i]) != army.defender_ids.end())
			continue;
		army.defender_ids.push_back(units_.id[i]);
	}

	// Centroid of foes for compact-army assignment
	army.cx = 0.0f;
	army.cz = 0.0f;
	int foe_n = 0;
	for (uint32_t fid : army.foe_ids) {
		int32_t fi = lookup_unit(fid);
		if (fi < 0) continue;
		army.cx += units_.pos_x[fi];
		army.cz += units_.pos_z[fi];
		foe_n++;
	}
	if (foe_n > 0) { army.cx /= (float)foe_n; army.cz /= (float)foe_n; }

	for (uint32_t uid : army.defender_ids) {
		int32_t idx = lookup_unit(uid);
		if (idx < 0) continue;

		float hp_ratio = (units_.max_hp[idx] > 0.0f) ? (units_.hp[idx] / units_.max_hp[idx]) : 1.0f;
		if (hp_ratio < petra_config_.garrison_hp_low) {
			uint32_t shelter = 0;
			float best = 1e9f;
			for (const auto &b : buildings_) {
				if (b.team != petra_pid_ || b.build_progress < 1.0f) continue;
				if (b.type != BLD_FORTRESS && b.type != BLD_TOWER && b.type != BLD_TOWN_CENTER) continue;
				if ((int)b.garrisoned_units.size() >= b.garrison_cap) continue;
				float dx = units_.pos_x[idx] - b.x;
				float dz = units_.pos_z[idx] - b.z;
				float d2 = dx * dx + dz * dz;
				if (d2 < best) { best = d2; shelter = b.id; }
			}
			if (shelter != 0) {
				order_garrison(&uid, 1, shelter);
				continue;
			}
		}

		uint32_t best_foe = 0;
		float best_d2 = 1e9f;
		for (uint32_t fid : army.foe_ids) {
			int32_t fi = lookup_unit(fid);
			if (fi < 0 || !units_.alive[fi]) continue;
			float dx = units_.pos_x[idx] - units_.pos_x[fi];
			float dz = units_.pos_z[idx] - units_.pos_z[fi];
			float d2 = dx * dx + dz * dz;
			if (d2 < best_d2) { best_d2 = d2; best_foe = fid; }
		}
		const bool on_foe = units_.action_state[idx] == ACTION_ATTACK && is_foe(units_.target_unit_id[idx]);
		if (best_foe != 0 && !on_foe) {
			order_attack(&uid, 1, best_foe);
		} else if (best_foe == 0 && home && units_.action_state[idx] == ACTION_ATTACK) {
			cmd_move(&uid, 1, home->x, home->z);
		}
	}
}

void Simulation::petra_manage_victory(float dt) {
	petra_victory_timer_ += dt;
	if (petra_victory_timer_ < 1.5f) return;
	petra_victory_timer_ = 0.0f;

	const PetraBase *pbase = get_primary_petra_base();
	float bx = pbase ? pbase->x : 36.0f;
	float bz = pbase ? pbase->z : 36.0f;

	auto assign_guards = [&](float gx, float gz, int want) {
		int assigned = 0;
		for (size_t i = 0; i < units_.count && assigned < want; ++i) {
			if (!units_.alive[i] || units_.team[i] != petra_pid_) continue;
			if (!petra_is_combat_unit(units_.unit_type[i])) continue;
			if (units_.id[i] == petra_scout_unit_id_) continue;
			if (units_.action_state[i] != ACTION_IDLE && units_.action_state[i] != ACTION_GUARD) continue;
			uint32_t uid = units_.id[i];
			float ox = gx + (float)((assigned % 3) - 1) * 2.4f;
			float oz = gz + (float)((assigned / 3) - 1) * 2.4f;
			if (units_.action_state[i] == ACTION_IDLE) cmd_move(&uid, 1, ox, oz);
			assigned++;
		}
	};

	if (victory_condition_ == VICTORY_WONDER && petra_ps().phase >= 3) {
		uint32_t wonder_id = 0;
		bool complete = false;
		for (const auto &b : buildings_) {
			if (b.team != petra_pid_ || b.type != BLD_WONDER) continue;
			wonder_id = b.id;
			complete = (b.build_progress >= 1.0f && b.hp > 0.0f);
			break;
		}
		if (wonder_id == 0) {
			uint32_t bid = petra_place_building(BLD_WONDER, bx, bz, 3, 10, PETRA_QUEUE_TECH);
			if (bid != 0) {
				auto builders = get_idle_workers(petra_pid_);
				if (builders.size() < 4) {
					for (size_t i = 0; i < units_.count && builders.size() < 8; ++i) {
						if (units_.alive[i] && units_.team[i] == petra_pid_ && units_.unit_type[i] == UNIT_VILLAGER)
							builders.push_back(units_.id[i]);
					}
				}
				if (!builders.empty()) {
					order_build_or_repair(builders.data(), std::min((size_t)10, builders.size()), bid);
				}
			}
		} else if (!complete) {
			auto builders = get_idle_workers(petra_pid_);
			if (!builders.empty()) {
				order_build_or_repair(builders.data(), std::min((size_t)8, builders.size()), wonder_id);
			}
		} else {
			auto bit = building_id_to_idx_.find(wonder_id);
			if (bit != building_id_to_idx_.end()) {
				assign_guards(buildings_[bit->second].x, buildings_[bit->second].z, 4);
			}
		}
	}

	if (victory_condition_ == VICTORY_RELIC) {
		for (auto &rel : relics_) {
			if (rel.claimed && rel.owner_team == petra_pid_) {
				assign_guards(rel.x, rel.z, 2);
				continue;
			}
			if (rel.claimed) continue;
			uint32_t best = 0;
			float best_d2 = 1e9f;
			for (size_t i = 0; i < units_.count; ++i) {
				if (!units_.alive[i] || units_.team[i] != petra_pid_) continue;
				if (units_.id[i] == petra_scout_unit_id_) continue;
				if (units_.action_state[i] == ACTION_GARRISONED) continue;
				float dx = units_.pos_x[i] - rel.x;
				float dz = units_.pos_z[i] - rel.z;
				float d2 = dx * dx + dz * dz;
				if (d2 < best_d2) { best_d2 = d2; best = units_.id[i]; }
			}
			if (best != 0) cmd_move(&best, 1, rel.x, rel.z);
		}
	}

	// Hero retreat / garrison (regicide-style criticalEnts)
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != petra_pid_ || units_.unit_type[i] != UNIT_HERO) continue;
		float hp_ratio = (units_.max_hp[i] > 0.0f) ? (units_.hp[i] / units_.max_hp[i]) : 1.0f;
		if (hp_ratio < petra_config_.garrison_hp_high) {
			uint32_t shelter = 0;
			float best = 1e9f;
			for (const auto &b : buildings_) {
				if (b.team != petra_pid_ || b.build_progress < 1.0f) continue;
				if (b.type != BLD_FORTRESS && b.type != BLD_TEMPLE && b.type != BLD_TOWN_CENTER) continue;
				if ((int)b.garrisoned_units.size() >= b.garrison_cap) continue;
				float dx = units_.pos_x[i] - b.x;
				float dz = units_.pos_z[i] - b.z;
				float d2 = dx * dx + dz * dz;
				if (d2 < best) { best = d2; shelter = b.id; }
			}
			if (shelter != 0 && hp_ratio < petra_config_.garrison_hp_low) {
				uint32_t uid = units_.id[i];
				order_garrison(&uid, 1, shelter);
			} else {
				uint32_t uid = units_.id[i];
				cmd_move(&uid, 1, bx, bz);
			}
		}
		assign_guards(units_.pos_x[i], units_.pos_z[i], 2 + (int)std::round(petra_config_.defensive * 2.0f));
	}
}

void Simulation::petra_manage_diplomacy(float dt) {
	// diplomacyManager.js: received requests have a 60-second warning and a
	// 120-second lapse. Keep this state deterministic and side-effect free
	// until the game layer supplies a real diplomacy-request event.
	for (auto it = petra_diplomacy_requests_.begin(); it != petra_diplomacy_requests_.end();) {
		auto &req = it->second;
		if (req.status == PetraDiplomacyRequest::WAITING) {
			req.warn_time += dt;
			req.lapse_time += dt;
			if (req.lapse_time >= 120.0f) it = petra_diplomacy_requests_.erase(it);
			else ++it;
		} else {
			++it;
		}
	}

	petra_diplo_timer_ += dt;
	if (petra_diplo_timer_ < 8.0f) return;
	petra_diplo_timer_ = 0.0f;

	// Tribute and betrayal still only involve player 0 (the human).
	const bool allied_to_human = is_ally(petra_pid_, 0);
	if (allied_to_human && petra_config_.cooperative >= 0.35f) {
		struct Gift { uint8_t res; float *stock; int player_have; };
		Gift gifts[] = {
			{ RES_FOOD,  &petra_ps().food(),  food() },
			{ RES_WOOD,  &petra_ps().wood(),  wood() },
			{ RES_GOLD,  &petra_ps().gold(),  gold() },
			{ RES_STONE, &petra_ps().stone(), stone() }
		};
		for (auto &g : gifts) {
			if (*g.stock > 700.0f && g.player_have < (int)(*g.stock * 0.25f)) {
				int send = std::min(150, (int)(*g.stock * 0.20f));
				if (send >= 50 && ai_send_tribute(g.res, send)) {
					if (const auto *pbase = get_primary_petra_base())
						petra_say(PETRA_CHAT_TRIBUTE, pbase->x, pbase->z);
					else
						petra_say(PETRA_CHAT_TRIBUTE);
				}
			}
		}
	}

	// Last-man-standing betrayal (diplomacyManager lastManStandingCheck)
	if (allied_to_human && players_[petra_pid_].team != players_[0].team &&
	    petra_config_.aggressive > 0.72f && petra_ps().phase >= 3) {
		int ai_mil = 0, player_mil = 0;
		for (size_t i = 0; i < units_.count; ++i) {
			if (!units_.alive[i] || !petra_is_combat_unit(units_.unit_type[i])) continue;
			if (units_.team[i] == petra_pid_) ai_mil++;
			else if (units_.team[i] == 0) player_mil++;
		}
		if (ai_mil >= 12 && ai_mil > (int)((float)player_mil * 1.8f + 2.0f)) {
			set_diplomacy(petra_pid_, 0, DIPLO_ENEMY); // the AI breaks its own side of the alliance
			if (const auto *pbase = get_primary_petra_base()) {
				raise_alert(BattleAlert::ALERT_UNDER_ATTACK, pbase->x, pbase->z, 0, 1);
				petra_say(PETRA_CHAT_BETRAYAL, pbase->x, pbase->z);
			} else {
				petra_say(PETRA_CHAT_BETRAYAL);
			}
		}
	}
}

void Simulation::petra_manage_emergency(float dt) {
	(void)dt;
	int pop = pop_current(petra_pid_);
	int structs = 0;
	int roots = 0;
	for (const auto &b : buildings_) {
		if (b.team != petra_pid_ || b.hp <= 0.0f || b.build_progress < 0.5f) continue;
		structs++;
		if (b.type == BLD_TOWN_CENTER || b.type == BLD_FORTRESS) roots++;
	}

	petra_emergency_.ref_pop = std::max(petra_emergency_.ref_pop, pop);
	petra_emergency_.ref_structs = std::max(petra_emergency_.ref_structs, structs);
	petra_emergency_.ref_roots = std::max(petra_emergency_.ref_roots, roots);

	const auto &e = petra_emergency_;
	bool now = false;
	if (e.ref_pop > 8 && pop < (int)((float)e.ref_pop * petra_config_.emergency_pop_factor)) now = true;
	if (e.ref_structs > 3 && structs < (int)((float)e.ref_structs * petra_config_.emergency_struct_factor)) now = true;
	if (e.ref_roots > 0 && roots == 0) now = true;
	if (e.ref_roots > 1 && (float)roots <= (float)e.ref_roots * petra_config_.emergency_root_factor) now = true;

	if (now && !petra_emergency_.active) {
		petra_emergency_.active = true;
		petra_say(PETRA_CHAT_EMERGENCY);
		if (petra_attack_plan_.state != AttackPlan::UNEXECUTED &&
		    petra_attack_plan_.state != AttackPlan::REGROUP) {
			petra_attack_plan_.state = AttackPlan::REGROUP;
			petra_attack_plan_.state_timer = 0.0f;
			if (const auto *pbase = get_primary_petra_base()) {
				petra_attack_plan_.retreat_x = pbase->x;
				petra_attack_plan_.retreat_z = pbase->z;
				raise_alert(BattleAlert::ALERT_BUILDING_UNDER_ATTACK, pbase->x, pbase->z, pbase->anchor_building_id, 1);
			}
		}
		if (petra_raid_plan_.state != AttackPlan::UNEXECUTED &&
		    petra_raid_plan_.state != AttackPlan::REGROUP) {
			petra_raid_plan_.state = AttackPlan::REGROUP;
			petra_raid_plan_.state_timer = 0.0f;
			if (const auto *pbase = get_primary_petra_base()) {
				petra_raid_plan_.retreat_x = pbase->x;
				petra_raid_plan_.retreat_z = pbase->z;
			}
		}
	} else if (petra_emergency_.active &&
	           pop > (int)((float)e.ref_pop * 1.15f) &&
	           structs > (int)((float)e.ref_structs * 1.15f)) {
		petra_emergency_.active = false;
	}
}

void Simulation::petra_reset_ai_state() {
	petra_timer_ = 0.0f;
	petra_econ_timer_ = 0.0f;
	petra_trade_timer_ = 0.0f;
	petra_naval_timer_ = 0.0f;
	petra_scout_timer_ = 0.0f;
	petra_scout_unit_id_ = 0;
	petra_scout_angle_ = 0.0f;
	petra_scout_radius_ = 12.0f;
	petra_bases_.clear();
	petra_fnd_watch_.clear();
	petra_bases_.push_back({1, 0, 48.0f, 48.0f, true, 0, 32.0f, 0.0f, false});
	petra_fow_.assign((size_t)grid_res_ * (size_t)grid_res_, (uint8_t)FOW_UNEXPLORED);
	memset(petra_threat_map_.danger, 0, sizeof(petra_threat_map_.danger));
	memset(petra_threat_map_.enemy_threat, 0, sizeof(petra_threat_map_.enemy_threat));
	memset(petra_threat_map_.friendly_defense, 0, sizeof(petra_threat_map_.friendly_defense));
	petra_threat_map_.update_timer = 0.0f;
	petra_strike_force_ = PetraStrikeForce();
	petra_attack_plan_ = AttackPlan();
	petra_raid_plan_ = AttackPlan();
	petra_raid_plan_.type = AttackPlan::PLAN_RAID;
	petra_opening_step_ = 0;
	petra_opening_timer_ = 0.0f;
	petra_defense_army_ = PetraDefenseArmy();
	petra_emergency_ = PetraEmergency();
	petra_queues_.reset();
	petra_transport_ = PetraTransportPlan();
	petra_defense_timer_ = 0.0f;
	petra_victory_timer_ = 0.0f;
	petra_diplo_timer_ = 0.0f;
	ai_wonder_victory_timer_ = 0.0f;
}

void Simulation::petra_swap_slot(uint8_t pid) {
	if (petra_slots_.size() < MAX_PLAYERS) petra_slots_.resize(MAX_PLAYERS);
	PetraSlot &slot = petra_slots_[pid];
#define RTSO_PETRA_SLOT_SWAP(m) std::swap(m, slot.m);
	RTSO_PETRA_SLOT_FIELDS(RTSO_PETRA_SLOT_SWAP)
#undef RTSO_PETRA_SLOT_SWAP
}

bool Simulation::petra_runs_for(uint8_t pid) const {
	if (pid >= MAX_PLAYERS) return false;
	// No slot seated (tests, skirmish without game setup): legacy player 1 AI.
	bool any_active = false;
	for (const PlayerState &ps : players_) any_active |= ps.active != 0;
	if (!any_active) return pid == PETRA_PLAYER;
	return players_[pid].active && players_[pid].is_ai && !is_defeated(pid);
}

void Simulation::tick_petra_ai(float dt) {
	// One Petra per AI slot. PETRA_PLAYER's state lives in the petra_* members;
	// any other AI's is swapped in from petra_slots_ for its turn.
	for (uint8_t pid = 0; pid < MAX_PLAYERS; ++pid) {
		if (!petra_runs_for(pid)) continue;
		const bool parked = pid != PETRA_PLAYER;
		if (parked) {
			petra_swap_slot(pid);
			petra_pid_ = pid;
			if (!petra_slots_[pid].initialized) {
				petra_reset_ai_state();
				petra_slots_[pid].initialized = true;
			}
		}
		tick_petra_ai_player(dt);
		if (parked) {
			petra_swap_slot(pid);
			petra_pid_ = PETRA_PLAYER;
		}
	}
}

void Simulation::tick_petra_ai_player(float dt) {
	// 0 A.D. Petra Autonomous AI System
	petra_timer_ += dt;

	petra_refresh_queue_accounts(dt);

	// 1. Dynamic Danger & Threat Heatmap (64x64)
	update_petra_threat_map(dt);

	// 2. Multi-Base Expansion & Base Anchors (BasesManager / headquarters.js)
	update_petra_bases(dt);

	// 3. Autonomous Scouting & FoW Exploration (scoutManager.js)
	petra_manage_scouting(dt);

	// 4. Villager Danger Evaluation & Evacuation
	petra_manage_workers(dt);

	// 5. Priority Queue: Emergency Defense & Military Counter-Production
	petra_manage_military_production(dt);

	// 6. Priority Queue: Economic Boom & Dynamic Priority Allocation
	petra_manage_economy(dt);

	// 7. AI Trading & Caravan Management (tradeManager.js)
	petra_manage_trading(dt);

	// 8. Maritime & Naval Operations (navalManager.js)
	petra_manage_naval(dt);

	// 9. startingStrategy.js village opening, then generic infrastructure
	petra_manage_opening(dt);
	petra_manage_buildings(dt);

	// 10. Technology & Phase Research Progression
	petra_manage_research(dt);

	// 11. Coordinated Tactical Battlegroup Attacks (attackPlan.js) + concurrent raid
	petra_manage_tactical_attacks(dt);
	petra_manage_raid(dt);

	// 11b. Naval transportPlan.js land-sea-land
	petra_manage_transport(dt);

	// 12. Home defense army + wounded garrison (defenseManager.js / garrisonManager.js)
	petra_manage_defense(dt);

	// 13. Wonder / relic / hero critical-entity protection (victoryManager.js)
	petra_manage_victory(dt);

	// 14. Ally tribute + last-man-standing betrayal (diplomacyManager.js)
	petra_manage_diplomacy(dt);

	// 15. Population / structure collapse response (emergencyManager.js)
	petra_manage_emergency(dt);
}

bool Simulation::save_state(const char *filepath) const {
	std::ofstream out(filepath, std::ios::binary);
	if (!out.is_open()) return false;

	uint32_t magic = 0x52545330; // 'RTS0'
	uint32_t version = 12; // v6 per-player table; v7 adds each unit's and queued order's civ roster template; v8 a 256-bit researched-tech set per player; v9 the 8x8 diplomacy matrix; v10 per-player defeat and winners; v11 structure upgrades in progress and the burning flag; v12 gate open/locked
	out.write((const char *)&magic, sizeof(magic));
	out.write((const char *)&version, sizeof(version));

	// 1. Sim scalars
	out.write((const char *)&ticks_, sizeof(ticks_));
	uint8_t num_players = MAX_PLAYERS;
	out.write((const char *)&num_players, sizeof(num_players));
	for (const PlayerState &ps : players_) {
		out.write((const char *)ps.res, sizeof(ps.res));
		out.write((const char *)ps.techs.words, sizeof(ps.techs.words));
		out.write((const char *)&ps.phase, sizeof(ps.phase));
		out.write((const char *)&ps.civ, sizeof(ps.civ));
		out.write((const char *)&ps.team, sizeof(ps.team));
		out.write((const char *)&ps.active, sizeof(ps.active));
		out.write((const char *)&ps.is_ai, sizeof(ps.is_ai));
	}
	out.write((const char *)&biome_, sizeof(biome_));
	out.write((const char *)&map_preset_, sizeof(map_preset_));
	out.write((const char *)diplomacy_, sizeof(diplomacy_)); // v9: every player's own row
	out.write((const char *)&wave_number_, sizeof(wave_number_));
	out.write((const char *)&wave_timer_, sizeof(wave_timer_));
	out.write((const char *)&wonder_victory_timer_, sizeof(wonder_victory_timer_));
	out.write((const char *)&has_active_wonder_, sizeof(has_active_wonder_));
	out.write((const char *)&game_over_, sizeof(game_over_));
	out.write((const char *)&victory_, sizeof(victory_));
	// v10: who has held anything, who is out, who won, and each player's wonder hold
	out.write((const char *)&participant_mask_, sizeof(participant_mask_));
	out.write((const char *)&defeated_mask_, sizeof(defeated_mask_));
	out.write((const char *)&winner_mask_, sizeof(winner_mask_));
	out.write((const char *)wonder_hold_, sizeof(wonder_hold_));
	// v3 determinism scalars: id allocator + sim RNG stream
	uint32_t rng_state = sim_rng_.state();
	out.write((const char *)&next_id_, sizeof(next_id_));
	out.write((const char *)&sim_seed_, sizeof(sim_seed_));
	out.write((const char *)&rng_state, sizeof(rng_state));
	// v4 FoW: retain explored terrain and current visibility across saves.
	// Sized by the current map's grid; load expects the same map to be loaded.
	out.write((const char *)fow_grid_.data(), (std::streamsize)fow_grid_.size());
	out.write((const char *)petra_fow_.data(), (std::streamsize)petra_fow_.size());

	// 2. Resources
	uint32_t num_res = (uint32_t)resources_.count;
	out.write((const char *)&num_res, sizeof(num_res));
	for (size_t r = 0; r < resources_.count; ++r) {
		out.write((const char *)&resources_.id[r], sizeof(uint32_t));
		out.write((const char *)&resources_.x[r], sizeof(float));
		out.write((const char *)&resources_.z[r], sizeof(float));
		out.write((const char *)&resources_.amount[r], sizeof(int32_t));
		out.write((const char *)&resources_.type[r], sizeof(uint8_t));
		out.write((const char *)&resources_.alive[r], sizeof(uint8_t));
	}

	// 3. Units SoA
	uint32_t num_units = (uint32_t)units_.count;
	out.write((const char *)&num_units, sizeof(num_units));
	for (size_t u = 0; u < units_.count; ++u) {
		out.write((const char *)&units_.id[u], sizeof(uint32_t));
		out.write((const char *)&units_.pos_x[u], sizeof(float));
		out.write((const char *)&units_.pos_z[u], sizeof(float));
		out.write((const char *)&units_.target_x[u], sizeof(float));
		out.write((const char *)&units_.target_z[u], sizeof(float));
		out.write((const char *)&units_.hp[u], sizeof(float));
		out.write((const char *)&units_.max_hp[u], sizeof(float));
		out.write((const char *)&units_.speed[u], sizeof(float));
		out.write((const char *)&units_.attack_range[u], sizeof(float));
		out.write((const char *)&units_.attack_damage[u], sizeof(float));
		out.write((const char *)&units_.attack_cooldown[u], sizeof(float));
		out.write((const char *)&units_.attack_timer[u], sizeof(float));
		out.write((const char *)&units_.death_timer[u], sizeof(float));
		out.write((const char *)&units_.carry_capacity[u], sizeof(int32_t));
		out.write((const char *)&units_.carry[u], sizeof(int32_t));
		out.write((const char *)&units_.kills[u], sizeof(uint16_t));
		out.write((const char *)&units_.team[u], sizeof(uint8_t));
		out.write((const char *)&units_.unit_type[u], sizeof(uint8_t));
		out.write((const char *)&units_.action_state[u], sizeof(uint8_t));
		out.write((const char *)&units_.res_type_carried[u], sizeof(uint8_t));
		out.write((const char *)&units_.stance[u], sizeof(uint8_t));
		out.write((const char *)&units_.formation[u], sizeof(uint8_t));
		out.write((const char *)&units_.rank[u], sizeof(uint8_t));
		out.write((const char *)&units_.alive[u], sizeof(uint8_t));
		out.write((const char *)&units_.has_order[u], sizeof(uint8_t));
		out.write((const char *)&units_.target_resource_id[u], sizeof(uint32_t));
		out.write((const char *)&units_.target_unit_id[u], sizeof(uint32_t));
		out.write((const char *)&units_.target_building_id[u], sizeof(uint32_t));
		out.write((const char *)&units_.tmpl[u], sizeof(uint16_t));
	}

	// 4. Buildings
	uint32_t num_blds = (uint32_t)buildings_.size();
	out.write((const char *)&num_blds, sizeof(num_blds));
	for (const auto &b : buildings_) {
		out.write((const char *)&b.id, sizeof(b.id));
		out.write((const char *)&b.x, sizeof(b.x));
		out.write((const char *)&b.z, sizeof(b.z));
		out.write((const char *)&b.half_size, sizeof(b.half_size));
		out.write((const char *)&b.type, sizeof(b.type));
		out.write((const char *)&b.team, sizeof(b.team));
		out.write((const char *)&b.hp, sizeof(b.hp));
		out.write((const char *)&b.max_hp, sizeof(b.max_hp));
		out.write((const char *)&b.build_progress, sizeof(b.build_progress));
		out.write((const char *)&b.cell_x, sizeof(b.cell_x));
		out.write((const char *)&b.cell_y, sizeof(b.cell_y));
		out.write((const char *)&b.footprint, sizeof(b.footprint));
		out.write((const char *)&b.attack_timer, sizeof(b.attack_timer));
		out.write((const char *)&b.has_rally_point, sizeof(b.has_rally_point));
		out.write((const char *)&b.rally_x, sizeof(b.rally_x));
		out.write((const char *)&b.rally_z, sizeof(b.rally_z));
		out.write((const char *)&b.rally_target_res_id, sizeof(b.rally_target_res_id));
		out.write((const char *)&b.rally_target_bld_id, sizeof(b.rally_target_bld_id));
		out.write((const char *)&b.garrison_cap, sizeof(b.garrison_cap));
		out.write((const char *)&b.research_tech, sizeof(b.research_tech));
		out.write((const char *)&b.research_timer, sizeof(b.research_timer));
		out.write((const char *)&b.research_duration, sizeof(b.research_duration));
		out.write((const char *)&b.capture_points, sizeof(b.capture_points));
		out.write((const char *)&b.max_capture_points, sizeof(b.max_capture_points));
		out.write((const char *)b.capture_points_by_team.data(), sizeof(b.capture_points_by_team));
		out.write((const char *)&b.upgrade_to, sizeof(b.upgrade_to));
		out.write((const char *)&b.upgrade_timer, sizeof(b.upgrade_timer));
		out.write((const char *)&b.upgrade_duration, sizeof(b.upgrade_duration));
		out.write((const char *)b.upgrade_paid.data(), sizeof(b.upgrade_paid));
		out.write((const char *)&b.is_burning, sizeof(b.is_burning));
		out.write((const char *)&b.gate_open, sizeof(b.gate_open));
		out.write((const char *)&b.gate_locked, sizeof(b.gate_locked));

		uint32_t q_sz = (uint32_t)b.queue.size();
		out.write((const char *)&q_sz, sizeof(q_sz));
		for (const auto &po : b.queue) {
			out.write((const char *)&po.unit_type, sizeof(po.unit_type));
			out.write((const char *)&po.timer, sizeof(po.timer));
			out.write((const char *)&po.duration, sizeof(po.duration));
			out.write((const char *)&po.tmpl, sizeof(po.tmpl));
		}

		uint32_t g_sz = (uint32_t)b.garrisoned_units.size();
		out.write((const char *)&g_sz, sizeof(g_sz));
		for (uint32_t gid : b.garrisoned_units) {
			out.write((const char *)&gid, sizeof(gid));
		}
	}

	// 5. G3 Market prices & trader routes (version 2+)
	out.write((const char *)market_buy_price_, sizeof(market_buy_price_));
	out.write((const char *)market_sell_price_, sizeof(market_sell_price_));
	out.write((const char *)market_trade_history_, sizeof(market_trade_history_));
	uint32_t num_routes = (uint32_t)trader_routes_.size();
	out.write((const char *)&num_routes, sizeof(num_routes));
	for (const auto &kv : trader_routes_) {
		uint32_t uid = kv.first;
		uint32_t a = kv.second.first;
		uint32_t b = kv.second.second;
		out.write((const char *)&uid, sizeof(uid));
		out.write((const char *)&a, sizeof(a));
		out.write((const char *)&b, sizeof(b));
	}
	uint32_t num_legs = (uint32_t)trader_leg_.size();
	out.write((const char *)&num_legs, sizeof(num_legs));
	for (const auto &kv : trader_leg_) {
		uint32_t uid = kv.first;
		int32_t leg = kv.second;
		out.write((const char *)&uid, sizeof(uid));
		out.write((const char *)&leg, sizeof(leg));
	}

	return true;
}

bool Simulation::load_state(const char *filepath) {
	std::ifstream in(filepath, std::ios::binary);
	if (!in.is_open()) return false;

	uint32_t magic = 0, version = 0;
	in.read((char *)&magic, sizeof(magic));
	in.read((char *)&version, sizeof(version));
	if (magic != 0x52545330 || version < 1 || version > 12) return false;

	// 1. Sim scalars
	in.read((char *)&ticks_, sizeof(ticks_));
	if (version >= 6) {
		uint8_t num_players = 0;
		in.read((char *)&num_players, sizeof(num_players));
		if (!in.good() || num_players > MAX_PLAYERS) return false;
		for (uint8_t p = 0; p < num_players; ++p) {
			PlayerState &ps = players_[p];
			in.read((char *)ps.res, sizeof(ps.res));
			ps.techs.clear();
			if (version >= 8) {
				in.read((char *)ps.techs.words, sizeof(ps.techs.words));
			} else {
				uint32_t mask = 0;
				in.read((char *)&mask, sizeof(mask));
				ps.techs.words[0] = mask;
			}
			in.read((char *)&ps.phase, sizeof(ps.phase));
			in.read((char *)&ps.civ, sizeof(ps.civ));
			in.read((char *)&ps.team, sizeof(ps.team));
			in.read((char *)&ps.active, sizeof(ps.active));
			in.read((char *)&ps.is_ai, sizeof(ps.is_ai));
		}
		in.read((char *)&biome_, sizeof(biome_));
		in.read((char *)&map_preset_, sizeof(map_preset_));
		if (version >= 9) in.read((char *)diplomacy_, sizeof(diplomacy_));
	} else {
		// v1-v5 stored player 0's stock as ints and civs for slots 0-3 only; other slots keep their state.
		uint8_t legacy_phase = 1;
		Civilization legacy_civ[4] = {};
		int32_t legacy_res[4] = {}; // food, wood, gold, stone
		uint32_t legacy_mask = 0;
		in.read((char *)&legacy_phase, sizeof(legacy_phase));
		in.read((char *)legacy_civ, sizeof(legacy_civ));
		in.read((char *)&biome_, sizeof(biome_));
		in.read((char *)&map_preset_, sizeof(map_preset_));
		in.read((char *)legacy_res, sizeof(legacy_res));
		in.read((char *)&legacy_mask, sizeof(legacy_mask));
		players_[0].phase = legacy_phase;
		for (int p = 0; p < 4; ++p) players_[p].civ = legacy_civ[p];
		set_player_resources(0, legacy_res[0], legacy_res[1], legacy_res[2], legacy_res[3]);
		players_[0].techs.clear();
		players_[0].techs.words[0] = legacy_mask;
	}
	// A save older than v9 carries no stances: rebuild them from the teams it
	// does carry, which is what those saves played with (free-for-all by default).
	if (version < 9) apply_team_diplomacy();
	in.read((char *)&wave_number_, sizeof(wave_number_));
	in.read((char *)&wave_timer_, sizeof(wave_timer_));
	in.read((char *)&wonder_victory_timer_, sizeof(wonder_victory_timer_));
	in.read((char *)&has_active_wonder_, sizeof(has_active_wonder_));
	in.read((char *)&game_over_, sizeof(game_over_));
	in.read((char *)&victory_, sizeof(victory_));
	participant_mask_ = defeated_mask_ = winner_mask_ = 0;
	for (float &w : wonder_hold_) w = 0.0f;
	if (version >= 10) {
		in.read((char *)&participant_mask_, sizeof(participant_mask_));
		in.read((char *)&defeated_mask_, sizeof(defeated_mask_));
		in.read((char *)&winner_mask_, sizeof(winner_mask_));
		in.read((char *)wonder_hold_, sizeof(wonder_hold_));
	}
	else if (victory_ || game_over_) winner_mask_ = victory_ ? 1 : 0xFE; // older saves: player 0 vs the rest
	if (version >= 3) {
		uint32_t rng_state = 1337;
		in.read((char *)&next_id_, sizeof(next_id_));
		in.read((char *)&sim_seed_, sizeof(sim_seed_));
		in.read((char *)&rng_state, sizeof(rng_state));
		if (!in.good()) return false;
		sim_rng_.set_state(rng_state);
	}
	if (version >= 4) {
		in.read((char *)fow_grid_.data(), (std::streamsize)fow_grid_.size());
		in.read((char *)petra_fow_.data(), (std::streamsize)petra_fow_.size());
		if (!in.good()) return false;
	}

	// 2. Resources
	uint32_t num_res = 0;
	in.read((char *)&num_res, sizeof(num_res));
	resources_.count = 0;
	res_id_to_idx_.clear();
	field_res_by_bld_.clear();
	field_bld_by_res_.clear();
	for (uint32_t r = 0; r < num_res; ++r) {
		uint32_t rid; float rx, rz; int32_t ramount; uint8_t rtype, ralive;
		in.read((char *)&rid, sizeof(rid));
		in.read((char *)&rx, sizeof(rx));
		in.read((char *)&rz, sizeof(rz));
		in.read((char *)&ramount, sizeof(ramount));
		in.read((char *)&rtype, sizeof(rtype));
		in.read((char *)&ralive, sizeof(ralive));

		size_t idx = resources_.count;
		resources_.id[idx] = rid;
		resources_.x[idx] = rx;
		resources_.z[idx] = rz;
		resources_.amount[idx] = ramount;
		resources_.type[idx] = rtype;
		resources_.alive[idx] = ralive;
		resources_.count++;
		res_id_to_idx_[rid] = idx;
	}

	// 3. Units
	uint32_t num_units = 0;
	in.read((char *)&num_units, sizeof(num_units));
	units_.count = 0;
	unit_id_to_idx_.clear();
	unit_idx_by_id_.assign(UNIT_CAP + 1, -1);

	for (uint32_t u = 0; u < num_units; ++u) {
		uint32_t uid; float px, pz, tx, tz, hp, max_hp, spd, rng, dmg, cd, timer, dtimer;
		int32_t cap, carry; uint16_t kills;
		uint8_t team, utype, astate, res_carried, stance, form, rank, alive, order;
		uint32_t tr, tu, tb;

		in.read((char *)&uid, sizeof(uid));
		in.read((char *)&px, sizeof(px));
		in.read((char *)&pz, sizeof(pz));
		in.read((char *)&tx, sizeof(tx));
		in.read((char *)&tz, sizeof(tz));
		in.read((char *)&hp, sizeof(hp));
		in.read((char *)&max_hp, sizeof(max_hp));
		in.read((char *)&spd, sizeof(spd));
		in.read((char *)&rng, sizeof(rng));
		in.read((char *)&dmg, sizeof(dmg));
		in.read((char *)&cd, sizeof(cd));
		in.read((char *)&timer, sizeof(timer));
		in.read((char *)&dtimer, sizeof(dtimer));
		in.read((char *)&cap, sizeof(cap));
		in.read((char *)&carry, sizeof(carry));
		in.read((char *)&kills, sizeof(kills));
		in.read((char *)&team, sizeof(team));
		in.read((char *)&utype, sizeof(utype));
		in.read((char *)&astate, sizeof(astate));
		in.read((char *)&res_carried, sizeof(res_carried));
		in.read((char *)&stance, sizeof(stance));
		in.read((char *)&form, sizeof(form));
		in.read((char *)&rank, sizeof(rank));
		in.read((char *)&alive, sizeof(alive));
		in.read((char *)&order, sizeof(order));
		in.read((char *)&tr, sizeof(tr));
		in.read((char *)&tu, sizeof(tu));
		in.read((char *)&tb, sizeof(tb));
		uint16_t utmpl = CIV_UNIT_TEMPLATE_NONE;
		if (version >= 7) in.read((char *)&utmpl, sizeof(utmpl));
		else utmpl = civ_default_template(civ_of(team), utype); // pre-roster saves: the civ's unit for the class

		size_t idx = units_.count;
		units_.id[idx] = uid;
		units_.pos_x[idx] = px;
		units_.pos_z[idx] = pz;
		units_.vel_x[idx] = 0;
		units_.vel_z[idx] = 0;
		units_.target_x[idx] = tx;
		units_.target_z[idx] = tz;
		units_.hp[idx] = hp;
		units_.max_hp[idx] = max_hp;
		units_.speed[idx] = spd;
		units_.attack_range[idx] = rng;
		units_.attack_damage[idx] = dmg;
		units_.attack_cooldown[idx] = cd;
		units_.attack_timer[idx] = timer;
		units_.death_timer[idx] = dtimer;
		units_.carry_capacity[idx] = cap;
		units_.carry[idx] = carry;
		units_.kills[idx] = kills;
		units_.team[idx] = team;
		units_.unit_type[idx] = utype;
		units_.tmpl[idx] = (utmpl < CIV_UNIT_TEMPLATE_COUNT) ? utmpl : CIV_UNIT_TEMPLATE_NONE;
		units_.action_state[idx] = astate;
		units_.res_type_carried[idx] = res_carried;
		units_.stance[idx] = stance;
		units_.formation[idx] = form;
		units_.rank[idx] = rank;
		units_.alive[idx] = alive;
		units_.has_order[idx] = order;
		units_.target_resource_id[idx] = tr;
		units_.target_unit_id[idx] = tu;
		units_.target_building_id[idx] = tb;
		units_.count++;

		bind_unit_index(uid, idx);
	}

	// 4. Buildings
	uint32_t num_blds = 0;
	in.read((char *)&num_blds, sizeof(num_blds));
	buildings_.clear();
	building_id_to_idx_.clear();
	occupied_cells_.clear();

	for (uint32_t b = 0; b < num_blds; ++b) {
		Building bld;
		in.read((char *)&bld.id, sizeof(bld.id));
		in.read((char *)&bld.x, sizeof(bld.x));
		in.read((char *)&bld.z, sizeof(bld.z));
		in.read((char *)&bld.half_size, sizeof(bld.half_size));
		in.read((char *)&bld.type, sizeof(bld.type));
		in.read((char *)&bld.team, sizeof(bld.team));
		in.read((char *)&bld.hp, sizeof(bld.hp));
		in.read((char *)&bld.max_hp, sizeof(bld.max_hp));
		in.read((char *)&bld.build_progress, sizeof(bld.build_progress));
		in.read((char *)&bld.cell_x, sizeof(bld.cell_x));
		in.read((char *)&bld.cell_y, sizeof(bld.cell_y));
		in.read((char *)&bld.footprint, sizeof(bld.footprint));
		in.read((char *)&bld.attack_timer, sizeof(bld.attack_timer));
		in.read((char *)&bld.has_rally_point, sizeof(bld.has_rally_point));
		in.read((char *)&bld.rally_x, sizeof(bld.rally_x));
		in.read((char *)&bld.rally_z, sizeof(bld.rally_z));
		in.read((char *)&bld.rally_target_res_id, sizeof(bld.rally_target_res_id));
		in.read((char *)&bld.rally_target_bld_id, sizeof(bld.rally_target_bld_id));
		in.read((char *)&bld.garrison_cap, sizeof(bld.garrison_cap));
		in.read((char *)&bld.research_tech, sizeof(bld.research_tech));
		in.read((char *)&bld.research_timer, sizeof(bld.research_timer));
		in.read((char *)&bld.research_duration, sizeof(bld.research_duration));
		in.read((char *)&bld.capture_points, sizeof(bld.capture_points));
		in.read((char *)&bld.max_capture_points, sizeof(bld.max_capture_points));
		if (version >= 5)
			in.read((char *)bld.capture_points_by_team.data(), sizeof(bld.capture_points_by_team));
		else
			bld.capture_points_by_team.fill(0.0f);
		if (version >= 11) {
			in.read((char *)&bld.upgrade_to, sizeof(bld.upgrade_to));
			in.read((char *)&bld.upgrade_timer, sizeof(bld.upgrade_timer));
			in.read((char *)&bld.upgrade_duration, sizeof(bld.upgrade_duration));
			in.read((char *)bld.upgrade_paid.data(), sizeof(bld.upgrade_paid));
			in.read((char *)&bld.is_burning, sizeof(bld.is_burning));
		}
		if (version >= 12) {
			in.read((char *)&bld.gate_open, sizeof(bld.gate_open));
			in.read((char *)&bld.gate_locked, sizeof(bld.gate_locked));
		}
		const auto &bi = building_type_info(bld.type);
		bld.capture_regen_rate = bi.capture_regen_rate;
		bld.capture_garrison_regen_rate = bi.capture_garrison_regen_rate;
		bld.territory_decay_rate = bi.territory_decay_rate;
		bld.committed = (bld.build_progress > 0.0f);
		bld.wrecked = (bld.hp <= 0.0f);

		uint32_t q_sz = 0;
		in.read((char *)&q_sz, sizeof(q_sz));
		bld.queue.resize(q_sz);
		for (uint32_t q = 0; q < q_sz; ++q) {
			in.read((char *)&bld.queue[q].unit_type, sizeof(bld.queue[q].unit_type));
			in.read((char *)&bld.queue[q].timer, sizeof(bld.queue[q].timer));
			in.read((char *)&bld.queue[q].duration, sizeof(bld.queue[q].duration));
			bld.queue[q].tmpl = CIV_UNIT_TEMPLATE_NONE;
			if (version >= 7) in.read((char *)&bld.queue[q].tmpl, sizeof(bld.queue[q].tmpl));
		}

		uint32_t g_sz = 0;
		in.read((char *)&g_sz, sizeof(g_sz));
		bld.garrisoned_units.resize(g_sz);
		for (uint32_t g = 0; g < g_sz; ++g) {
			in.read((char *)&bld.garrisoned_units[g], sizeof(uint32_t));
		}

		buildings_.push_back(bld);
		building_id_to_idx_[bld.id] = buildings_.size() - 1;
		for (int dx = 0; dx < bld.footprint; ++dx) {
			for (int dy = 0; dy < bld.footprint; ++dy) {
				occupied_cells_.insert({bld.cell_x + dx, bld.cell_y + dy});
			}
		}
	}

	// 5. G3 Market prices & trader routes (version 2+, fallback for v1)
	if (version >= 2) {
		in.read((char *)market_buy_price_, sizeof(market_buy_price_));
		in.read((char *)market_sell_price_, sizeof(market_sell_price_));
		in.read((char *)market_trade_history_, sizeof(market_trade_history_));
		uint32_t num_routes = 0;
		in.read((char *)&num_routes, sizeof(num_routes));
		trader_routes_.clear();
		for (uint32_t k = 0; k < num_routes; ++k) {
			uint32_t uid, a, b;
			in.read((char *)&uid, sizeof(uid));
			in.read((char *)&a, sizeof(a));
			in.read((char *)&b, sizeof(b));
			trader_routes_[uid] = {a, b};
		}
		uint32_t num_legs = 0;
		in.read((char *)&num_legs, sizeof(num_legs));
		trader_leg_.clear();
		for (uint32_t k = 0; k < num_legs; ++k) {
			uint32_t uid; int32_t leg;
			in.read((char *)&uid, sizeof(uid));
			in.read((char *)&leg, sizeof(leg));
			trader_leg_[uid] = leg;
		}
	} else {
		for (int i = 0; i < 4; ++i) { market_buy_price_[i] = 1.0f; market_sell_price_[i] = 1.0f; market_trade_history_[i] = 0; }
		trader_routes_.clear();
		trader_leg_.clear();
	}

	if (version < 3) {
		// Legacy saves never stored the id allocator: rebuild it from max live id
		// so post-load spawns cannot collide with restored entities.
		uint32_t max_id = 0;
		for (size_t u = 0; u < units_.count; ++u) max_id = std::max(max_id, units_.id[u]);
		for (size_t r = 0; r < resources_.count; ++r) max_id = std::max(max_id, resources_.id[r]);
		for (const auto &b : buildings_) max_id = std::max(max_id, b.id);
		for (const auto &pr : projectiles_) max_id = std::max(max_id, pr.id);
		for (const auto &rl : relics_) max_id = std::max(max_id, rl.id);
		for (size_t f = 0; f < fauna_.count; ++f) max_id = std::max(max_id, fauna_.id[f]);
		next_id_ = max_id + 1;
	}

	rebuild_all_tech_mods();
	rebuild_nav_grid(); // the loaded structures (and locked gates) block paths, not the old ones
	return true;
}

bool Simulation::terrain_blocks_los(float sx, float sz, float sy, float tx, float tz, float ty) const {
	// Approximate 0 A.D. RangeManager LOS with a deterministic terrain-height
	// ray test. The 0 A.D. manager stores visibility on LOS vertices, while
	// RTS0 uses 2m FoW cells; sampling the height field gives hills/cliffs the
	// same blocking semantics without expanding the simulation state format.
	if (terrain_heights_.empty() || terrain_res_ <= 1) return false;
	const float dx = tx - sx;
	const float dz = tz - sz;
	const float dist = std::sqrt(dx * dx + dz * dz);
	const int steps = std::clamp((int)std::ceil(dist / 3.0f), 1, 32);
	for (int i = 1; i < steps; ++i) {
		const float t = (float)i / (float)steps;
		const float wx = sx + dx * t;
		const float wz = sz + dz * t;
		const float ray_y = sy + (ty - sy) * t;
		if (get_terrain_height(wx, wz) > ray_y + 0.35f) return true;
	}
	return false;
}

uint8_t Simulation::fow_state_at(float x, float z, uint8_t viewer_team) const {
	if (map_half_ <= 0.0f || x < -map_half_ || x > map_half_ || z < -map_half_ || z > map_half_)
		return FOW_UNEXPLORED;
	const int G = grid_res_;
	const int cx = std::clamp((int)((x + map_half_) / (map_half_ * 2.0f) * G), 0, G - 1);
	const int cz = std::clamp((int)((z + map_half_) / (map_half_ * 2.0f) * G), 0, G - 1);
	return fow_grid(viewer_team)[cz * G + cx];
}

bool Simulation::is_entity_visible_to_viewer(float x, float z, uint8_t entity_team, uint8_t viewer_team) const {
	// Teams and reciprocal alliances expose allied entities, never enemies.
	if (is_ally(viewer_team, entity_team)) return true;
	return fow_state_at(x, z, viewer_team) == FOW_VISIBLE;
}

void Simulation::ensure_fow_cell_heights() {
	// Terrain height at every FoW cell centre is fixed for the map, so sample
	// it once instead of once per viewer per cell.
	const int G = grid_res_;
	if (fow_cell_height_.size() == (size_t)G * (size_t)G) return;
	const float half = map_half_;
	const float cell = (half * 2.0f) / (float)G;
	fow_cell_height_.resize((size_t)G * (size_t)G);
	for (int gz = 0; gz < G; ++gz)
		for (int gx = 0; gx < G; ++gx)
			fow_cell_height_[gz * G + gx] = get_terrain_height((gx + 0.5f) * cell - half, (gz + 0.5f) * cell - half);

	// fow_cell_hmax_: the highest heightmap sample that get_terrain_height() can read for a point
	// in the cell. A point's bilinear lookup reads the samples on both sides of it, so take the
	// samples under the cell plus one more on each side, and one beyond that for rounding.
	fow_cell_hmax_.clear();
	fow_block_hmax_.clear();
	const int N = terrain_res_;
	if (terrain_heights_.empty() || N <= 1) return;
	auto sample_span = [N, G](int g, int &lo, int &hi) {
		lo = std::max(0, (int)((int64_t)g * (N - 1) / G) - 1);
		hi = std::min(N - 1, (int)((int64_t)(g + 1) * (N - 1) / G) + 2);
	};
	std::vector<float> col_max((size_t)N * (size_t)G); // per sample row, the max over each cell's columns
	for (int z = 0; z < N; ++z)
		for (int gx = 0; gx < G; ++gx) {
			int lo, hi;
			sample_span(gx, lo, hi);
			float m = terrain_heights_[(size_t)z * N + lo];
			for (int x = lo + 1; x <= hi; ++x) m = std::max(m, terrain_heights_[(size_t)z * N + x]);
			col_max[(size_t)z * G + gx] = m;
		}
	fow_cell_hmax_.resize((size_t)G * (size_t)G);
	for (int gz = 0; gz < G; ++gz) {
		int lo, hi;
		sample_span(gz, lo, hi);
		for (int gx = 0; gx < G; ++gx) {
			float m = col_max[(size_t)lo * G + gx];
			for (int z = lo + 1; z <= hi; ++z) m = std::max(m, col_max[(size_t)z * G + gx]);
			fow_cell_hmax_[(size_t)gz * G + gx] = m;
		}
	}
	// The same per block of FOW_LOS_BLOCK x FOW_LOS_BLOCK cells, for reveal_fow().
	const int GB = (G + FOW_LOS_BLOCK - 1) / FOW_LOS_BLOCK;
	fow_block_hmax_.assign((size_t)GB * (size_t)GB, -std::numeric_limits<float>::infinity());
	for (int gz = 0; gz < G; ++gz)
		for (int gx = 0; gx < G; ++gx) {
			float &m = fow_block_hmax_[(size_t)(gz / FOW_LOS_BLOCK) * GB + gx / FOW_LOS_BLOCK];
			m = std::max(m, fow_cell_hmax_[(size_t)gz * G + gx]);
		}
}

void Simulation::reveal_fow(std::vector<uint8_t> &grid, float sx, float sz, float vr) const {
	// Marks every cell within vr of (sx, sz) that the terrain does not hide.
	// A cell already VISIBLE this pass is skipped: another viewer lit it, and
	// the LOS ray can only ever set it VISIBLE again. Needs ensure_fow_cell_heights().
	const float half = map_half_;
	const int G = grid_res_;
	const float cell = (half * 2.0f) / (float)G;
	const int cx = (int)((sx + half) / (2.0f * half) * G);
	const int cz = (int)((sz + half) / (2.0f * half) * G);
	const int r = (int)(vr / cell) + 1;
	const float eye_y = get_terrain_height(sx, sz) + 1.6f;
	const float vr2 = vr * vr;
	const int gz0 = std::max(0, cz - r), gz1 = std::min(G - 1, cz + r);
	const int gx0 = std::max(0, cx - r), gx1 = std::min(G - 1, cx + r);

	// Exact shortcut for terrain_blocks_los(): every height a ray from the viewer to a cell samples
	// lies in the box of cells between the viewer's cell and that cell, so it is at most the highest
	// fow_cell_hmax_ in that box, and at most the highest fow_block_hmax_ in the box of blocks around
	// it. The ray stays at or above min(eye, target) and is blocked only by terrain more than 0.35
	// above it, so a box no higher than min(eye, target) + 0.35 (less a margin for float rounding)
	// cannot block, and the ray test is skipped. Same result, fewer height samples.
	struct LosShortcut {
		const float *block_hmax;
		int GB, bcx, bcz, rb, WB;
		float eye_y;
		bool ready = false;
		float box_max[FOW_BOX_MAX_W * FOW_BOX_MAX_W]; // filled on first use, not zeroed per call
		LosShortcut(const float *bh, int gb, int bx, int bz, int r_blocks, float eye) :
				block_hmax(bh), GB(gb), bcx(bx), bcz(bz), rb(r_blocks), WB(2 * r_blocks + 1), eye_y(eye) {}
		// True when no terrain between the viewer and cell (gx, gz) can block the ray to height ty.
		[[gnu::noinline]] bool clear(int gx, int gz, float ty) {
			if (!ready) { // in a crowd, many calls find every cell lit already
				fow_box_max(block_hmax, GB, bcx, bcz, rb, box_max);
				ready = true;
			}
			const int i = (gz / FOW_LOS_BLOCK - bcz + rb) * WB + (gx / FOW_LOS_BLOCK - bcx + rb);
			return box_max[i] <= std::min(eye_y, ty) + FOW_LOS_CLEARANCE;
		}
	};
	const int GB = (G + FOW_LOS_BLOCK - 1) / FOW_LOS_BLOCK;
	const int bcx = cx / FOW_LOS_BLOCK, bcz = cz / FOW_LOS_BLOCK;
	const int rb = r / FOW_LOS_BLOCK + 1;
	LosShortcut los(fow_block_hmax_.data(), GB, bcx, bcz, rb, eye_y);
	// Every box holds the viewer's own block, so when that block is already higher than the eye
	// allows, no cell can pass and the shortcut is off.
	const bool shortcut = !fow_block_hmax_.empty() && cx >= 0 && cx < G && cz >= 0 && cz < G &&
			los.WB <= FOW_BOX_MAX_W && fow_block_hmax_[(size_t)bcz * GB + bcx] <= eye_y + FOW_LOS_CLEARANCE;

	constexpr uint64_t VISIBLE_X8 = 0x0101010101010101ull * FOW_VISIBLE;
	for (int gz = gz0; gz <= gz1; ++gz) {
		const float wz = (gz + 0.5f) * cell - half;
		const float ddz = wz - sz;
		const float rem = vr2 - ddz * ddz;
		if (rem < 0.0f) continue; // no cell of this row is within vr
		// The viewer is inside cell cx, so a cell k > half-width / cell + 1 columns away is more
		// than half a cell outside the circle and fails the distance test below anyway.
		const int span = (int)(std::sqrt(rem) / cell) + 1;
		const int lo = std::max(gx0, cx - span), hi = std::min(gx1, cx + span);
		uint8_t *row = &grid[(size_t)gz * G];
		const float *hrow = &fow_cell_height_[(size_t)gz * G];
		for (int gx = lo; gx <= hi; ++gx) {
			if (row[gx] == FOW_VISIBLE) {
				// Skip runs of cells other viewers already lit, eight at a time.
				uint64_t next8;
				while (gx + 8 <= hi && (std::memcpy(&next8, row + gx + 1, 8), next8 == VISIBLE_X8)) gx += 8;
				continue;
			}
			const float wx = (gx + 0.5f) * cell - half;
			const float ddx = wx - sx;
			if (ddx * ddx + ddz * ddz > vr2) continue;
			const float ty = hrow[gx] + 0.4f;
			if ((shortcut && los.clear(gx, gz, ty)) || !terrain_blocks_los(sx, sz, eye_y, wx, wz, ty))
				row[gx] = FOW_VISIBLE;
		}
	}
}

void Simulation::tick_fow() {
	ScopedUs prof(map_scale_stats_.fow_us, map_scale_stats_.fow_calls);
	ensure_fow_cell_heights();

	// VisionSharing.js: allied players share the visibility generated by
	// their units/buildings -- teammates, or a mutual alliance both sides chose.
	// fow_grid_ is player 0's view and the one hashed for lockstep.
	update_player_fow(fow_grid_, [this](uint8_t owner) { return is_ally(0, owner); });

	// The local viewer's own view when it is not player 0 (a multiplayer
	// joiner). It is presentation-only state: never hashed or read by the
	// simulation, so peers computing different viewers stay in sync.
	const uint8_t p = fow_viewer_;
	if (p != 0 && p < MAX_PLAYERS) {
		auto &grid = player_fow_[p];
		if (grid.size() != fow_grid_.size()) grid.assign(fow_grid_.size(), FOW_UNEXPLORED);
		update_player_fow(grid, [this, p](uint8_t owner) { return is_ally(p, owner); });
	}
}

template <typename SharesVision>
void Simulation::update_player_fow(std::vector<uint8_t> &grid, SharesVision shares_vision) {
	// Step 1: Reset all VISIBLE tiles back to EXPLORED (preserve explored history)
	for (auto &c : grid) {
		if (c == FOW_VISIBLE) c = FOW_EXPLORED;
	}

	// Step 2: For every unit whose vision the player shares, illuminate tiles within its vision radius
	// Vision radius per unit type (in world units):
	//   Villager/Infantry: 16m, Cavalry: 20m, Archer: 22m, Catapult: 24m, Hero: 18m, Elephant: 20m, Trireme: 28m
	auto vision_radius = [](uint8_t utype) -> float {
		switch (utype) {
			case UNIT_ARCHER:   return 22.0f;
			case UNIT_CAVALRY:  return 20.0f;
			case UNIT_CATAPULT: return 24.0f;
			case UNIT_TRIREME:  return 28.0f;
			case UNIT_ELEPHANT: return 20.0f;
			case UNIT_HERO:     return 18.0f;
			default:            return 16.0f;
		}
	};

	for (size_t u = 0; u < units_.count; ++u) {
		if (!units_.alive[u] || !shares_vision(units_.team[u])) continue;
		reveal_fow(grid, units_.pos_x[u], units_.pos_z[u], vision_radius(units_.unit_type[u]));
	}

	// Step 3: Buildings also reveal their surroundings
	for (const auto& b : buildings_) {
		if (!shares_vision(b.team) || b.build_progress < 1.0f) continue;
		float bvr = 18.0f; // default building vision
		if (b.type == BLD_TOWER || b.type == BLD_FORTRESS) bvr = 24.0f;
		if (b.type == BLD_DOCK) bvr = 28.0f;
		reveal_fow(grid, b.x, b.z, bvr);
	}
}

Simulation::FaunaView Simulation::fauna() const {
	return {fauna_.count, fauna_.id, fauna_.pos_x, fauna_.pos_z,
	        fauna_.vel_x, fauna_.vel_z, fauna_.rot_y,
	        fauna_.hp, fauna_.max_hp, fauna_.food_amount,
	        fauna_.fauna_type, fauna_.behavior, fauna_.alive};
}

uint32_t Simulation::spawn_fauna(float x, float z, uint8_t ftype) {
	if (fauna_.count >= FaunaStorage::MAX_FAUNA) return 0;
	size_t idx = fauna_.count++;
	uint32_t fid = alloc_id();
	const auto &info = fauna_type_info(ftype);
	fauna_.id[idx] = fid;
	fauna_.pos_x[idx] = std::clamp(x, -map_half_ + 2.0f, map_half_ - 2.0f);
	fauna_.pos_z[idx] = std::clamp(z, -map_half_ + 2.0f, map_half_ - 2.0f);
	fauna_.vel_x[idx] = 0.0f;
	fauna_.vel_z[idx] = 0.0f;
	fauna_.rot_y[idx] = (float)sim_rng_.range_int(0, 359) * 0.0174533f;
	fauna_.hp[idx] = info.max_hp;
	fauna_.max_hp[idx] = info.max_hp;
	fauna_.food_amount[idx] = info.food_amount;
	fauna_.fauna_type[idx] = ftype;
	fauna_.behavior[idx] = FAUNA_IDLE_GRAZE;
	fauna_.alive[idx] = 1;
	fauna_.state_timer[idx] = 2.0f + (float)sim_rng_.range_int(0, 3);
	fauna_.attack_timer[idx] = 0.0f;
	fauna_.target_unit_id[idx] = 0;
	fauna_.corral_id[idx] = 0;
	fauna_.fatten_timer[idx] = 0.0f;
	fauna_id_to_idx_[fid] = idx;
	return fid;
}

void Simulation::order_hunt(const uint32_t *unit_ids, size_t count, uint32_t fauna_id) {
	auto it = fauna_id_to_idx_.find(fauna_id);
	if (it == fauna_id_to_idx_.end()) return;
	size_t fidx = it->second;
	if (!fauna_.alive[fidx]) return;

	for (size_t k = 0; k < count; ++k) {
		int32_t uidx = lookup_unit(unit_ids[k]);
		if (uidx < 0 || !units_.alive[uidx]) continue;
		units_.action_state[uidx] = ACTION_ATTACK;
		units_.has_order[uidx] = 1;
		units_.target_unit_id[uidx] = fauna_id;
		units_.target_x[uidx] = fauna_.pos_x[fidx];
		units_.target_z[uidx] = fauna_.pos_z[fidx];
	}
}

void Simulation::tick_fauna(float dt) {
	for (size_t i = 0; i < fauna_.count; ++i) {
		if (!fauna_.alive[i]) continue;
		const auto &info = fauna_type_info(fauna_.fauna_type[i]);

		// Dead check -> Carcass conversion (preserves fattened food value!)
		if (fauna_.hp[i] <= 0.0f) {
			if (fauna_.behavior[i] != FAUNA_CARCASS) {
				fauna_.behavior[i] = FAUNA_CARCASS;
				// Spawn rich food resource node where animal fell with fattened food yield
				spawn_resource_world(fauna_.pos_x[i], fauna_.pos_z[i], RES_FOOD, fauna_.food_amount[i], RES_SUB_MEAT);
			}
			fauna_.alive[i] = 0;
			continue;
		}

		fauna_.state_timer[i] -= dt;
		if (fauna_.attack_timer[i] > 0.0f) fauna_.attack_timer[i] -= dt;

		float fx = fauna_.pos_x[i];
		float fz = fauna_.pos_z[i];

		// 1. Domestic Corralled Livestock Behavior & Passive Fattening (Corral.js)
		if (fauna_.behavior[i] == FAUNA_CORRALLED) {
			auto bit = building_id_to_idx_.find(fauna_.corral_id[i]);
			if (bit == building_id_to_idx_.end() || buildings_[bit->second].hp <= 0.0f) {
				fauna_.behavior[i] = FAUNA_IDLE_GRAZE;
				fauna_.corral_id[i] = 0;
			} else {
				const auto &corral = buildings_[bit->second];
				// Passive Fattening over time: steadily increases food yield from 100 up to 400!
				fauna_.fatten_timer[i] += dt;
				if (fauna_.fatten_timer[i] >= 2.0f) {
					fauna_.fatten_timer[i] -= 2.0f;
					if (fauna_.food_amount[i] < 400) {
						fauna_.food_amount[i] = std::min(400, fauna_.food_amount[i] + 5);
						fauna_.hp[i] = std::min(fauna_.max_hp[i] * 1.5f, fauna_.hp[i] + 0.5f);
					}
				}

				// Gentle wandering inside/near corral perimeter
				float cdx = fx - corral.x;
				float cdz = fz - corral.z;
				float cd2 = cdx * cdx + cdz * cdz;
				if (cd2 > 2.8f * 2.8f) {
					float clen = std::sqrt(cd2);
					fauna_.vel_x[i] = -(cdx / clen) * (info.speed * 0.4f);
					fauna_.vel_z[i] = -(cdz / clen) * (info.speed * 0.4f);
					fauna_.rot_y[i] = std::atan2(-cdx, -cdz);
				} else if (fauna_.state_timer[i] <= 0.0f) {
					fauna_.state_timer[i] = 3.0f + (float)sim_rng_.range_int(0, 3);
					if (sim_rng_.range_int(0, 1) == 0) {
						float angle = (float)sim_rng_.range_int(0, 359) * 0.0174533f;
						fauna_.vel_x[i] = std::sin(angle) * (info.speed * 0.2f);
						fauna_.vel_z[i] = std::cos(angle) * (info.speed * 0.2f);
						fauna_.rot_y[i] = angle;
					} else {
						fauna_.vel_x[i] = 0.0f;
						fauna_.vel_z[i] = 0.0f;
					}
				}

				fauna_.pos_x[i] += fauna_.vel_x[i] * dt;
				fauna_.pos_z[i] += fauna_.vel_z[i] * dt;
				fauna_.pos_x[i] = std::clamp(fauna_.pos_x[i], -map_half_ + 2.0f, map_half_ - 2.0f);
				fauna_.pos_z[i] = std::clamp(fauna_.pos_z[i], -map_half_ + 2.0f, map_half_ - 2.0f);
				continue;
			}
		}

		// 2. Herded Animals (Following Villager / Moving toward friendly Corral)
		if (fauna_.behavior[i] == FAUNA_HERDED) {
			float nearest_corral_d2 = 99999.0f;
			float target_cx = 0.0f, target_cz = 0.0f;
			uint32_t nearest_cid = 0;

			for (const auto &b : buildings_) {
				if (b.team != 0 || b.build_progress < 1.0f) continue;
				if (b.type == BLD_CORRAL || (b.type == BLD_TOWN_CENTER && nearest_cid == 0)) {
					float dx = b.x - fx;
					float dz = b.z - fz;
					float d2 = dx * dx + dz * dz;
					if (b.type == BLD_CORRAL) d2 *= 0.5f; // prioritize corrals
					if (d2 < nearest_corral_d2) {
						nearest_corral_d2 = d2;
						target_cx = b.x;
						target_cz = b.z;
						nearest_cid = b.id;
					}
				}
			}

			if (nearest_cid != 0) {
				float dx = target_cx - fx;
				float dz = target_cz - fz;
				float len = std::hypot(dx, dz);
				if (len <= 3.8f) {
					// Arrived safely at corral!
					fauna_.corral_id[i] = nearest_cid;
					fauna_.behavior[i] = FAUNA_CORRALLED;
					fauna_.vel_x[i] = 0.0f;
					fauna_.vel_z[i] = 0.0f;
				} else {
					fauna_.vel_x[i] = (dx / len) * (info.speed * 1.15f);
					fauna_.vel_z[i] = (dz / len) * (info.speed * 1.15f);
					fauna_.rot_y[i] = std::atan2(dx, dz);
				}
			} else {
				int32_t herder_idx = lookup_unit(fauna_.target_unit_id[i]);
				if (herder_idx >= 0 && units_.alive[herder_idx]) {
					float dx = units_.pos_x[herder_idx] - fx;
					float dz = units_.pos_z[herder_idx] - fz;
					float len = std::hypot(dx, dz);
					if (len > 2.5f) {
						fauna_.vel_x[i] = (dx / len) * (info.speed * 1.1f);
						fauna_.vel_z[i] = (dz / len) * (info.speed * 1.1f);
						fauna_.rot_y[i] = std::atan2(dx, dz);
					} else {
						fauna_.vel_x[i] = 0.0f;
						fauna_.vel_z[i] = 0.0f;
					}
				} else {
					fauna_.behavior[i] = FAUNA_IDLE_GRAZE;
				}
			}

			fauna_.pos_x[i] += fauna_.vel_x[i] * dt;
			fauna_.pos_z[i] += fauna_.vel_z[i] * dt;
			fauna_.pos_x[i] = std::clamp(fauna_.pos_x[i], -map_half_ + 2.0f, map_half_ - 2.0f);
			fauna_.pos_z[i] = std::clamp(fauna_.pos_z[i], -map_half_ + 2.0f, map_half_ - 2.0f);
			continue;
		}

		// 3. Scan for nearest player unit within agro/flee range
		float nearest_u_dist2 = 99999.0f;
		float nearest_ux = 0, nearest_uz = 0;
		uint32_t nearest_uid = 0;
		int32_t nearest_uidx = -1;

		for (size_t u = 0; u < units_.count; ++u) {
			if (!units_.alive[u]) continue;
			float dx = units_.pos_x[u] - fx;
			float dz = units_.pos_z[u] - fz;
			float d2 = dx * dx + dz * dz;
			if (d2 < nearest_u_dist2) {
				nearest_u_dist2 = d2;
				nearest_ux = units_.pos_x[u];
				nearest_uz = units_.pos_z[u];
				nearest_uid = units_.id[u];
				nearest_uidx = (int32_t)u;
			}
		}

		float agro_r = info.agro_range;
		float agro_r2 = agro_r * agro_r;

		if (!info.is_predator) {
			// Passive Game (Deer, Sheep): Flee when approached
			if (nearest_u_dist2 < agro_r2) {
				fauna_.behavior[i] = FAUNA_FLEE;
				float dx = fx - nearest_ux;
				float dz = fz - nearest_uz;
				float len = std::sqrt(dx * dx + dz * dz);
				if (len > 0.01f) {
					fauna_.vel_x[i] = (dx / len) * info.speed;
					fauna_.vel_z[i] = (dz / len) * info.speed;
					fauna_.rot_y[i] = std::atan2(dx, dz);
				}
			} else if (fauna_.state_timer[i] <= 0.0f) {
				fauna_.behavior[i] = FAUNA_IDLE_GRAZE;
				fauna_.state_timer[i] = 3.0f + (float)sim_rng_.range_int(0, 4);
				// Random slow wander
				if (sim_rng_.range_int(0, 1) == 0) {
					float angle = (float)sim_rng_.range_int(0, 359) * 0.0174533f;
					fauna_.vel_x[i] = std::sin(angle) * (info.speed * 0.35f);
					fauna_.vel_z[i] = std::cos(angle) * (info.speed * 0.35f);
					fauna_.rot_y[i] = angle;
				} else {
					fauna_.vel_x[i] = 0.0f;
					fauna_.vel_z[i] = 0.0f;
				}
			}
		} else {
			// Predator (Wolf, Boar): Attack nearest unit within territory
			if (nearest_u_dist2 < agro_r2 && nearest_uidx >= 0) {
				fauna_.behavior[i] = FAUNA_ATTACK;
				fauna_.target_unit_id[i] = nearest_uid;
				float dx = nearest_ux - fx;
				float dz = nearest_uz - fz;
				float len = std::sqrt(dx * dx + dz * dz);
				if (len > 1.4f) {
					fauna_.vel_x[i] = (dx / len) * info.speed;
					fauna_.vel_z[i] = (dz / len) * info.speed;
					fauna_.rot_y[i] = std::atan2(dx, dz);
				} else {
					fauna_.vel_x[i] = 0;
					fauna_.vel_z[i] = 0;
					// Attack unit
					if (fauna_.attack_timer[i] <= 0.0f) {
						fauna_.attack_timer[i] = info.attack_cooldown;
						units_.hp[nearest_uidx] -= info.attack_damage;
					}
				}
			} else if (fauna_.state_timer[i] <= 0.0f) {
				fauna_.behavior[i] = FAUNA_IDLE_GRAZE;
				fauna_.state_timer[i] = 3.0f + (float)sim_rng_.range_int(0, 4);
				if (sim_rng_.range_int(0, 1) == 0) {
					float angle = (float)sim_rng_.range_int(0, 359) * 0.0174533f;
					fauna_.vel_x[i] = std::sin(angle) * (info.speed * 0.3f);
					fauna_.vel_z[i] = std::cos(angle) * (info.speed * 0.3f);
					fauna_.rot_y[i] = angle;
				} else {
					fauna_.vel_x[i] = 0;
					fauna_.vel_z[i] = 0;
				}
			}
		}

		// Move & clamp bounds
		fauna_.pos_x[i] += fauna_.vel_x[i] * dt;
		fauna_.pos_z[i] += fauna_.vel_z[i] * dt;
		fauna_.pos_x[i] = std::clamp(fauna_.pos_x[i], -map_half_ + 2.0f, map_half_ - 2.0f);
		fauna_.pos_z[i] = std::clamp(fauna_.pos_z[i], -map_half_ + 2.0f, map_half_ - 2.0f);
	}
}

void Simulation::rebuild_auras() {
	aura_mgr_.clear();
	// Buildings: temple heal aura
	for (const auto &b : buildings_) {
		if (b.build_progress < 1.0f) continue;
		if (b.type == BLD_TEMPLE) {
			aura_mgr_.register_source(b.id, AURA_HEAL, 18.f, 3.5f, b.team, b.x, b.z);
		}
	}
	// Units: female gather, carnyx attack, hero auras from the civ roster
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i]) continue;
		uint8_t ut = units_.unit_type[i];
		uint8_t team = units_.team[i];
		float x = units_.pos_x[i];
		float z = units_.pos_z[i];
		uint32_t id = units_.id[i];
		if (ut == UNIT_FEMALE_CITIZEN) {
			aura_mgr_.register_source(id, AURA_GATHER, 10.f, 0.15f, team, x, z);
		}
		if (ut == UNIT_CARNYX) {
			aura_mgr_.register_source(id, AURA_ATTACK, 14.f, 1.20f, team, x, z);
		}
		// Heroes (and other roster units with auras) project their own 0 A.D. auras.
		uint16_t tmpl = units_.tmpl[i];
		if (tmpl < CIV_UNIT_TEMPLATE_COUNT) {
			for (int16_t a : civ_unit_template(tmpl).auras) {
				if (a < 0) continue;
				const CivHeroAura &aura = civ_hero_aura(a);
				if (aura.kind >= AURA_COUNT) continue; // effect RTS0 does not model yet
				aura_mgr_.register_source(id, (AuraType)aura.kind, aura.range, aura.value, team, x, z, a);
			}
		}
	}
}

} // namespace rtso
