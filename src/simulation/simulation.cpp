#include "simulation.h"
#include "entity_limits_data.h"
#include "promotion_data.h"
#include "rms_generator.h"
#include "nav/building_steering.h"
#include "nav/rasterize.h"
#include "../core/job_system.h"
#include <cstdio>
#include <chrono>
#include <cmath>
#include <numeric>
#include <bit>
#include <algorithm>
#include <memory>
#include <limits>
#include <cstring>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <type_traits>
#include <queue>
#include <iostream>

namespace rtso {

// Unit promotion data (promotion_data.h); see the Promotion.js section above tick_promotion.
namespace {

struct PromotionLink {
	uint16_t xp = 0;
	uint16_t target = CIV_UNIT_TEMPLATE_NONE; // NONE: the same unit at target_rank
	uint8_t target_rank = 0; // 0: no Promotion at this rank
};

struct PromotionTable {
	PromotionLink step[CIV_UNIT_TEMPLATE_COUNT][4];
	uint8_t start_rank[CIV_UNIT_TEMPLATE_COUNT];
};

const PromotionTable &promotion_table() {
	static const std::unique_ptr<PromotionTable> table = [] {
		auto t = std::make_unique<PromotionTable>();
		std::fill(std::begin(t->start_rank), std::end(t->start_rank), (uint8_t)1);
		std::unordered_map<std::string, uint16_t> by_id;
		for (uint16_t i = 0; i < CIV_UNIT_TEMPLATE_COUNT; ++i) by_id.emplace(civ_unit_template(i).id, i);
		auto find = [&](const char *id) {
			auto it = id ? by_id.find(id) : by_id.end();
			return it == by_id.end() ? CIV_UNIT_TEMPLATE_NONE : it->second;
		};
		for (const PromotionStep &s : PROMOTION_STEPS) {
			const uint16_t from = find(s.id);
			if (from == CIV_UNIT_TEMPLATE_NONE || s.rank > 3) continue;
			PromotionLink &l = t->step[from][s.rank];
			l.xp = s.required_xp;
			l.target = s.target ? find(s.target) : CIV_UNIT_TEMPLATE_NONE;
			l.target_rank = (s.target && l.target == CIV_UNIT_TEMPLATE_NONE) ? 0 : s.target_rank;
		}
		for (const PromotionStart &s : PROMOTION_STARTS) {
			const uint16_t i = find(s.id);
			if (i != CIV_UNIT_TEMPLATE_NONE) t->start_rank[i] = s.rank;
		}
		return t;
	}();
	return *table;
}

// The next Promotion of a unit, or null. Units without a roster template use their
// class's template: template_unit_infantry RequiredXp 100, cavalry and healers 150.
const PromotionLink *promotion_link(uint16_t tmpl, uint8_t type, uint8_t rank) {
	if (rank < 1 || rank > 3) return nullptr;
	if (tmpl < CIV_UNIT_TEMPLATE_COUNT) {
		const PromotionLink &l = promotion_table().step[tmpl][rank];
		return l.target_rank ? &l : nullptr;
	}
	static const PromotionLink kInfantry[3] = {{100, CIV_UNIT_TEMPLATE_NONE, 2}, {100, CIV_UNIT_TEMPLATE_NONE, 3}, {}};
	static const PromotionLink kMounted[3] = {{150, CIV_UNIT_TEMPLATE_NONE, 2}, {150, CIV_UNIT_TEMPLATE_NONE, 3}, {}};
	if (rank >= 3) return nullptr;
	switch (type) {
		case UNIT_SWORDSMAN: case UNIT_ARCHER: case UNIT_SPEARMAN: return &kInfantry[rank - 1];
		case UNIT_CAVALRY: case UNIT_CAMEL: case UNIT_HEALER: return &kMounted[rank - 1];
		default: return nullptr;
	}
}

uint8_t promotion_start_rank(uint16_t tmpl) {
	return tmpl < CIV_UNIT_TEMPLATE_COUNT ? promotion_table().start_rank[tmpl] : 1;
}

const RankBonus &rank_bonus(uint8_t rank) { return rank >= 3 ? RANK_BONUS_ELITE : RANK_BONUS_ADVANCED; }

uint64_t unit_classes(uint16_t tmpl, uint8_t type) {
	return tmpl < CIV_UNIT_TEMPLATE_COUNT ? civ_unit_template(tmpl).tech_classes : unit_tech_classes(type);
}

} // namespace

// First save version with projectile sources and partial xp (0 A.D. Loot.js port).
static constexpr uint32_t SAVE_VERSION_LOOT = 19;

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

// The save version that added UnitAI orders (tick_state_io section 4).
static constexpr uint32_t SAVE_VERSION_UNIT_ORDERS = 20;

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
	static_assert(kUnitGridRes <= 64, "occupied[] holds a grid row in one word");
	int32_t *head = nullptr;
	int32_t *nxt = nullptr;
	uint64_t *occupied = nullptr; // by row: bit xx set when cell (xx, row) holds a unit
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
		occupied = arena.alloc_array<uint64_t>((size_t)res);
		if (!head || !nxt || !occupied) return false;
		std::memset(head, 0xFF, sizeof(int32_t) * (size_t)(res * res)); // every head -1
		std::memset(occupied, 0, sizeof(uint64_t) * (size_t)res);
		for (size_t i = 0; i < n; ++i) {
			if (!alive[i]) {
				nxt[i] = -1;
				continue;
			}
			int c = cell_of(px[i], pz[i]);
			nxt[i] = head[c];
			head[c] = (int32_t)i;
			occupied[c / res] |= uint64_t{1} << (c % res);
		}
		return true;
	}

	template <typename Fn>
	void visit_radius(float x, float z, float radius, Fn &&fn) const {
		visit_radius_within(x, z, radius, std::numeric_limits<float>::infinity(), fn);
	}

	// visit_radius(), skipping the cells that hold only units more than `reach` from (x, z) on
	// either axis: for callers that ignore those units anyway. The cell mapping is monotonic, so
	// the other units are visited as before, in the same order.
	template <typename Fn>
	void visit_radius_within(float x, float z, float radius, float reach, Fn &&fn) const {
		int cr = std::max(1, (int)std::ceil(radius * inv) + 1);
		int cx = std::clamp((int)((x + half) * inv), 0, res - 1);
		int cz = std::clamp((int)((z + half) * inv), 0, res - 1);
		int x0 = std::max(0, cx - cr);
		int x1 = std::min(res - 1, cx + cr);
		int z0 = std::max(0, cz - cr);
		int z1 = std::min(res - 1, cz + cr);
		if (reach < radius + (float)cr / inv) {
			auto axis = [this](float f) { return std::clamp((int)((f + half) * inv), 0, res - 1); };
			x0 = std::max(x0, axis(x - reach));
			x1 = std::min(x1, axis(x + reach));
			z0 = std::max(z0, axis(z - reach));
			z1 = std::min(z1, axis(z + reach));
		}
		if (x0 > x1) return;
		// Cells x0..x1 of a row, taken in ascending order and skipping the empty ones.
		const uint64_t span = (~uint64_t{0} >> (63 - x1)) & (~uint64_t{0} << x0);
		for (int zz = z0; zz <= z1; ++zz) {
			int row = zz * res;
			for (uint64_t m = occupied[zz] & span; m; m &= m - 1) {
				for (int32_t j = head[row + std::countr_zero(m)]; j >= 0; j = nxt[j]) {
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
// kFowByteMask[b]: 0xFF in byte i for each bit i set in b, for reveal_fow()'s FowView copy.
constexpr std::array<uint64_t, 256> kFowByteMask = [] {
	std::array<uint64_t, 256> t{};
	for (int b = 0; b < 256; ++b)
		for (int i = 0; i < 8; ++i)
			if (b >> i & 1) t[b] |= 0xFFull << (8 * i);
	return t;
}();
// For each block of the (2rb+1)^2 window centred on block (bcx, bcz), the highest block_hmax in
// the box of blocks from the centre to that block. Fills the blocks inside the GB x GB grid; the
// window is row-major in `out`.
[[gnu::noinline]] void fow_box_max(const float *block_hmax, int GB, int bcx, int bcz, int rb, float *out) {
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
	territory_regions_dirty_ = true;
	// A new game brings its own terrain (set_terrain_data or load_state); the
	// previous map's heights would otherwise move projectiles and water.
	terrain_heights_.clear();
	terrain_res_ = 64;
	terrain_water_height_ = -1.0f;
	fow_cell_height_.clear();
	fow_cell_hmax_.clear();
	fow_block_hmax_.clear();
	triggers_.clear();
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
	std::fill(std::begin(regicide_hero_), std::end(regicide_hero_), 0u);

	next_id_ = 1;
	sim_rng_.set_seed(sim_seed_);
	// Per-player economy reset. Civ, team and active/AI flags come from game setup and survive reset().
	// Player 0 starts with the human stock, every other slot with Petra's.
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		PlayerState &ps = players_[p];
		if (p == 0) { ps.food() = 350.0f; ps.wood() = 450.0f; ps.gold() = 300.0f; ps.stone() = 250.0f; }
		else        { ps.food() = 400.0f; ps.wood() = 600.0f; ps.gold() = 300.0f; ps.stone() = 200.0f; }
		for (float &w : ps.gathered_window) w = 0.0f;
		for (float &u : ps.used) u = 0.0f;
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
	treasures_.clear();
	ruins_caps_.clear();
	scenery_.clear();
	wonder_victory_timer_ = 0.0f;
	has_active_wonder_ = false;
	record_ = MatchRecord();
	stats_.fill(PlayerStatistics{});
	stat_samples_.clear();
	stats_explore_res_ = 0;
	for (auto &g : stats_explored_) g.clear();
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
	turret_height_.clear();
	for (double &d : battle_damage_) d = 0.0;
	turret_prev_stance_.clear();
	formations_.clear();
	next_formation_id_ = 1;
	formation_group_.assign(UNIT_CAP, 0u);

	// G3 Market dynamic pricing reset
	for (int i = 0; i < 4; ++i) { barter_price_diff_[i] = 0.0f; market_trade_history_[i] = 0; }
	for (auto &row : trading_goods_) std::fill(std::begin(row), std::end(row), (uint8_t)25);
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
	std::memset(petra_tribute_inbox_, 0, sizeof(petra_tribute_inbox_));
	std::memset(petra_diplo_request_inbox_, 0, sizeof(petra_diplo_request_inbox_));
	std::memset(petra_tribute_request_wanted_, 0, sizeof(petra_tribute_request_wanted_));
	std::memset(petra_tribute_request_res_, 0, sizeof(petra_tribute_request_res_));
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
	// Wolves are aggressive (0 A.D. fauna_wolf), so their pack sits away from both bases.
	spawn_fauna(38.0f, -36.0f, FAUNA_WOLF);
	spawn_fauna(40.0f, -33.0f, FAUNA_WOLF);

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
	treasures_.clear();
	ruins_caps_.clear();
	scenery_.clear();
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
	// Templates trained above Basic (Identity/Rank): Roman Army Camp veterans, Legionaries.
	if (const uint8_t r = promotion_start_rank(tmpl); r > 1) {
		apply_rank_bonus(idx, 1, r);
		units_.rank[idx] = r;
		units_.hp[idx] = units_.max_hp[idx];
	}

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
	if (type == BLD_DOCK) { // GuiInterface GetFoundationSnapData: a dock faces the open water
		const float a = dock_angle(wx, wz, half);
		if (!std::isnan(a)) b.rot = a;
	}
	b.attack_timer = 0.0f;
	b.is_burning = false;
	b.has_rally_point = false;
	b.rally_x = wx;
	b.rally_z = wz + half + 2.0f;
	b.rally_target_res_id = 0;
	b.rally_target_bld_id = 0;
	b.garrison_cap = binfo.garrison_cap;
	b.garrison_cap = structure_garrison_cap(b);
	b.research_tech = TECH_NONE;
	b.research_timer = 0.0f;
	b.research_duration = 0.0f;
	b.capture_points = 0.0f;
	// 0 A.D. Capturable/TerritoryDecay template values.
	b.max_capture_points = binfo.capture_max_points;
	if (team < MAX_PLAYERS)
		b.max_capture_points *= civ_bonus_mult(civ_of(team), CIV_BONUS_BUILDING_CAPTURE_POINTS, type) *
		                        tech_mods(team).bld_capture[type];
	b.capture_regen_rate = binfo.capture_regen_rate;
	b.capture_garrison_regen_rate = binfo.capture_garrison_regen_rate;
	b.territory_decay_rate = binfo.territory_decay_rate;
	b.capture_points_by_team.fill(0.0f);
	// A Gaia structure's points are Gaia's pool (what no player holds), not player 0's;
	// giving them to player 0 handed it every Gaia tower on the first tick.
	if (b.team < MAX_PLAYERS) b.capture_points_by_team[b.team] = b.max_capture_points;

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

void Simulation::generate_random_map(MapType type, uint32_t seed, int num_players, float map_half, const uint8_t *teams) {
	RmsGenerator::Config cfg;
	cfg.map_type = type;
	cfg.seed = seed;
	cfg.num_players = num_players;
	if (teams)
		for (int i = 0; i < std::min(num_players, (int)MAX_PLAYERS); ++i) cfg.teams[i] = teams[i];
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
	else if (std::strcmp(minfo.biome, "desert") == 0 || std::strcmp(minfo.biome, "persia") == 0) cfg.biome = BIOME_OASIS;
	else if (std::strcmp(minfo.biome, "volcanic") == 0) cfg.biome = BIOME_STEPPE;
	else cfg.biome = BIOME_AEGEAN;
	if (biome_override_ >= 0) cfg.biome = (BiomeType)biome_override_;

	set_sim_seed(seed);
	RmsGenerator::generate(*this, cfg);
	if (biome_override_ >= 0) {
		// The ground textures follow the chosen biome too (Assets::apply_terrain_biome).
		static const char *kBiomeNames[BIOME_COUNT] = {"aegean", "latium", "alpine", "oasis", "temperate",
				"steppe", "savanna", "polar", "india"};
		current_map_biome_ = kBiomeNames[biome_override_];
	}
	if (nomad_) remove_player_structures();
	register_map_triggers(type);
	rebuild_nav_grid();
	rebuild_territory();
}

void Simulation::remove_player_structures() {
	// rmgen-common placePlayerBase returns early for Nomad: no Civic Centre, walls or other
	// starting structures; the starting units stay. Runs right after generation, before any tick.
	std::vector<Building> kept;
	kept.reserve(buildings_.size());
	for (Building &b : buildings_)
		if (b.team >= MAX_PLAYERS) kept.push_back(std::move(b));
	buildings_ = std::move(kept);
	nomad_start_ = true;
	building_id_to_idx_.clear();
	occupied_cells_.clear();
	for (size_t i = 0; i < buildings_.size(); ++i) {
		const Building &b = buildings_[i];
		building_id_to_idx_[b.id] = i;
		for (int dx = 0; dx < b.footprint; ++dx)
			for (int dy = 0; dy < b.footprint; ++dy) occupied_cells_.insert({b.cell_x + dx, b.cell_y + dy});
	}
}

void Simulation::spawn_nomad_treasures() {
	// rmgen-common placePlayersNomad: next to each player's starting units, enough treasure of
	// each resource to pay for a Civic Centre on top of the starting resources.
	const BuildingTypeInfo &cc = building_type_info(BLD_TOWN_CENTER);
	const int cost[RES_COUNT] = {cc.wood_cost, cc.gold_cost, cc.stone_cost, cc.food_cost}; // by ResourceType
	static const int kAmountIdx[RES_COUNT] = {1, 3, 2, 0}; // TreasureTypeInfo::amount is food, wood, stone, metal
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		if (!players_[p].active) continue;
		float sx = 0.0f, sz = 0.0f;
		int n = 0;
		for (size_t u = 0; u < units_.count; ++u)
			if (units_.alive[u] && units_.team[u] == p) { sx += units_.pos_x[u]; sz += units_.pos_z[u]; ++n; }
		if (n == 0) continue;
		sx /= (float)n; sz /= (float)n;
		int placed = 0;
		for (uint8_t r = 0; r < RES_COUNT; ++r) {
			const int amount = treasure_type_info(treasure_type_for_resource(r)).amount[kAmountIdx[r]];
			const int need = cost[r] - (int)players_[p].res[r];
			const int count = (amount > 0 && need > 0) ? (need + amount - 1) / amount : 0;
			for (int k = 0; k < count; ++k, ++placed) {
				const float a = 2.39996323f * (float)placed; // golden-angle spiral, 3-5 m out (SimpleObject 3..5)
				const float d = 3.0f + (float)(placed % 3);
				const float x = std::clamp(sx + std::cos(a) * d, -map_half_ + 2.0f, map_half_ - 2.0f);
				const float z = std::clamp(sz + std::sin(a) * d, -map_half_ + 2.0f, map_half_ - 2.0f);
				spawn_resource_treasure(x, z, r);
			}
		}
	}
}

uint32_t Simulation::player_color(uint8_t pid) const {
	return pid < MAX_PLAYERS ? PLAYER_COLORS[player_color_[pid]] : 0xFFFFFFu;
}

bool Simulation::unit_garrisonable(size_t unit_idx) const {
	if (regicide_garrison_ || victory_condition_ != VICTORY_REGICIDE) return true;
	const uint8_t t = units_.team[unit_idx];
	return t >= MAX_PLAYERS || regicide_hero_[t] == 0 || units_.id[unit_idx] != regicide_hero_[t];
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
			if (nav_grid_.cells.at(x, y) != NAV_WATER && nav_grid_.cells.at(x, y) != NAV_SHALLOW)
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
		// A committed foundation blocks movement too (Foundation.js Commit sets its obstruction).
		if ((b.build_progress < 0.99f && !b.committed) || !blocks_pathfinding(b)) continue;
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
	// Gaia scenery: oriented rectangles (CCmpObstruction::GetObstructionSquare: u = (cos a, -sin a),
	// v = (sin a, cos a)), rasterized the same way.
	for (const Scenery &sc : scenery_) {
		const float c = std::cos(sc.angle), sn = std::sin(sc.angle);
		int covered = 0;
		rtso::nav::rasterize_rect_with_clearance(
			rtso::nav::Vec2(sc.x, sc.z), rtso::nav::Vec2(c, -sn), rtso::nav::Vec2(sn, c),
			rtso::nav::Vec2(sc.half_w, sc.half_d), 0.0f, nav_cell, nav_origin,
			nav_grid_.cells.w, nav_grid_.cells.h,
			[&](int x, int y) {
				nav_grid_.cells.at(x, y) = NAV_OBSTRUCTED;
				++covered;
			});
		if (covered == 0) {
			// Thinner than a navcell (a 1.5 m fence): block the cells along its centre line.
			const float step = 0.5f * nav_cell;
			const int nu = std::max(1, (int)std::ceil(2.0f * sc.half_w / step));
			const int nv = std::max(1, (int)std::ceil(2.0f * sc.half_d / step));
			for (int a = 0; a <= nu; ++a)
				for (int b = 0; b <= nv; ++b) {
					const float lu = -sc.half_w + 2.0f * sc.half_w * (float)a / (float)nu;
					const float lv = -sc.half_d + 2.0f * sc.half_d * (float)b / (float)nv;
					auto [cx, cy] = nav_grid_.world_to_cell(sc.x + lu * c + lv * sn, sc.z - lu * sn + lv * c);
					if (nav_grid_.cells.inside(cx, cy) && nav_grid_.cells.at(cx, cy) != NAV_WATER)
						nav_grid_.cells.at(cx, cy) = NAV_OBSTRUCTED;
				}
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
		else if ((radius = civ_building_territory_radius(b.type)) <= 0.0f) continue;
		if (b.team >= MAX_PLAYERS) continue; // Gaia holds no territory
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
	territory_regions_dirty_ = true;
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

void Simulation::ensure_territory_regions() {
	const int W = territory_grid_.w, Hh = territory_grid_.h;
	if (!territory_regions_dirty_ && territory_region_.w == W && territory_region_.h == Hh) return;
	territory_regions_dirty_ = false;
	territory_regions_.clear();
	territory_region_.resize(W, Hh, 0);
	territory_region_.fill(0);
	if (W <= 0 || Hh <= 0) return;
	// Label 4-connected patches of one owner (region ids from 1; 0 = no territory).
	std::vector<int> stack;
	for (int y = 0; y < Hh; ++y) {
		for (int x = 0; x < W; ++x) {
			const uint8_t v = territory_grid_.at(x, y);
			if (v == 0 || territory_region_.at(x, y) != 0) continue;
			territory_regions_.push_back(TerritoryRegion{(uint8_t)(v - 1), false, {}});
			const uint16_t id = (uint16_t)territory_regions_.size();
			territory_region_.at(x, y) = id;
			stack.assign(1, y * W + x);
			while (!stack.empty()) {
				const int c = stack.back();
				stack.pop_back();
				const int cx = c % W, cy = c / W;
				const int nx[4] = {cx - 1, cx + 1, cx, cx};
				const int ny[4] = {cy, cy, cy - 1, cy + 1};
				for (int k = 0; k < 4; ++k) {
					if (nx[k] < 0 || ny[k] < 0 || nx[k] >= W || ny[k] >= Hh) continue;
					if (territory_grid_.at(nx[k], ny[k]) != v || territory_region_.at(nx[k], ny[k]) != 0) continue;
					territory_region_.at(nx[k], ny[k]) = id;
					stack.push_back(ny[k] * W + nx[k]);
				}
			}
		}
	}
	// Roots: template_structure_civic_civil_centre.xml and template_structure_wonder.xml
	// TerritoryInfluence/Root.
	for (const auto &b : buildings_) {
		if (b.build_progress < 0.99f || b.team >= MAX_PLAYERS || b.wrecked) continue;
		if (b.type != BLD_TOWN_CENTER && b.type != BLD_WONDER) continue;
		const int tx = std::clamp((int)((b.x + map_half_) / (map_half_ * 2.0f) * (float)W), 0, W - 1);
		const int tz = std::clamp((int)((b.z + map_half_) / (map_half_ * 2.0f) * (float)Hh), 0, Hh - 1);
		const uint16_t id = territory_region_.at(tx, tz);
		if (id && territory_regions_[id - 1].owner == b.team) territory_regions_[id - 1].connected = true;
	}
	// Bordering tiles of connected foreign regions, for each unconnected region.
	for (int y = 0; y < Hh; ++y) {
		for (int x = 0; x < W; ++x) {
			const uint16_t id = territory_region_.at(x, y);
			if (!id || territory_regions_[id - 1].connected) continue;
			TerritoryRegion &r = territory_regions_[id - 1];
			const int nx[4] = {x - 1, x + 1, x, x};
			const int ny[4] = {y, y, y - 1, y + 1};
			for (int k = 0; k < 4; ++k) {
				if (nx[k] < 0 || ny[k] < 0 || nx[k] >= W || ny[k] >= Hh) continue;
				const uint16_t n = territory_region_.at(nx[k], ny[k]);
				if (!n || n == id || !territory_regions_[n - 1].connected) continue;
				const uint8_t o = territory_regions_[n - 1].owner;
				if (o != r.owner && o < MAX_PLAYERS) r.neighbours[o]++;
			}
		}
	}
}

bool Simulation::territory_decay_targets(const Building &b, std::array<float, MAX_PLAYERS> &to, float &to_gaia) {
	to.fill(0.0f);
	to_gaia = 0.0f;
	if (b.territory_decay_rate <= 0.0f) return false;
	// template_structure.xml decays in "neutral enemy" territory; outposts and docks only in "enemy".
	const bool neutral = b.type != BLD_OUTPOST && b.type != BLD_DOCK;
	const bool gaia = b.team >= MAX_PLAYERS;
	ensure_territory_regions();
	const int W = territory_grid_.w, Hh = territory_grid_.h;
	if (W <= 0 || Hh <= 0) return false;
	const int tx = std::clamp((int)((b.x + map_half_) / (map_half_ * 2.0f) * (float)W), 0, W - 1);
	const int tz = std::clamp((int)((b.z + map_half_) / (map_half_ * 2.0f) * (float)Hh), 0, Hh - 1);
	const uint16_t id = territory_region_.at(tx, tz);
	if (!id) { // neutral ground: Gaia's own structures don't decay there
		// 0 A.D. only lets outposts (which don't decay there) and military colonies be placed in
		// neutral land (BuildRestrictions/Territory). Until RTS0's placement follows those rules
		// (gap U5), structures standing there keep their points rather than all turning Gaia.
		if (!TERRITORY_DECAY_IN_NEUTRAL_LAND) return false;
		if (gaia || !neutral) return false;
		to_gaia = 1.0f;
		return true;
	}
	const TerritoryRegion &r = territory_regions_[id - 1];
	if (r.connected && (gaia || !is_ally(b.team, r.owner))) {
		to[r.owner] = 1.0f;
		return true;
	}
	if (r.connected) return false;
	if (gaia || r.owner != b.team) { // unconnected foreign territory: towards Gaia
		if (gaia) return false;
		to_gaia = 1.0f;
		return true;
	}
	// Own unconnected territory: safe next to a connected ally, else towards the neighbours.
	uint32_t total = 0;
	for (int p = 0; p < MAX_PLAYERS; ++p) {
		if (!r.neighbours[p]) continue;
		if (is_ally(b.team, (uint8_t)p)) return false;
		total += r.neighbours[p];
	}
	if (!total) { to_gaia = 1.0f; return true; }
	for (int p = 0; p < MAX_PLAYERS; ++p) to[p] = (float)r.neighbours[p] / (float)total;
	return true;
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

namespace {
// 0 A.D. BuildRestrictions per structure template. template_structure.xml
// defaults to land placement in own territory; the overrides are
// civil_centre (own neutral, 200 m from a CivilCentre), dock (shore, own ally
// neutral), defensive_wall and its gate and tower (land-shore), palisade
// (land-shore, own neutral), outpost (own neutral, 50 m from an Outpost),
// defensive_tower (60 m from a Tower) and fortress (80 m from a Fortress).
enum : uint8_t { PLACE_LAND, PLACE_SHORE_TYPE, PLACE_LAND_SHORE };
enum : uint8_t { TERR_OWN = 1, TERR_ALLY = 2, TERR_NEUTRAL = 4, TERR_ENEMY = 8 };
enum : uint8_t { DIST_NONE, DIST_CIVIL_CENTRE, DIST_TOWER, DIST_FORTRESS, DIST_OUTPOST };
struct BuildRestriction {
	uint8_t placement, territory, distance_class;
	float min_distance; // world units (one tile, 4 m)
};
BuildRestriction build_restriction(uint8_t type) {
	switch (type) {
	case BLD_TOWN_CENTER: return {PLACE_LAND, TERR_OWN | TERR_NEUTRAL, DIST_CIVIL_CENTRE, 200.0f / 4.0f};
	case BLD_DOCK: return {PLACE_SHORE_TYPE, TERR_OWN | TERR_ALLY | TERR_NEUTRAL, DIST_NONE, 0.0f};
	case BLD_WALL_SEGMENT: case BLD_GATE: case BLD_WALL_TOWER: return {PLACE_LAND_SHORE, TERR_OWN, DIST_NONE, 0.0f};
	case BLD_PALISADE: return {PLACE_LAND_SHORE, TERR_OWN | TERR_NEUTRAL, DIST_NONE, 0.0f};
	case BLD_OUTPOST: return {PLACE_LAND, TERR_OWN | TERR_NEUTRAL, DIST_OUTPOST, 50.0f / 4.0f};
	case BLD_TOWER: case BLD_SENTRY_TOWER: return {PLACE_LAND, TERR_OWN, DIST_TOWER, 60.0f / 4.0f};
	case BLD_FORTRESS: return {PLACE_LAND, TERR_OWN, DIST_FORTRESS, 80.0f / 4.0f};
	// Civ-specific: civil_centre_military_colony (own neutral, 120 m from a
	// CivilCentre) and ptol/lighthouse (shore, own ally neutral).
	case BLD_MILITARY_COLONY: return {PLACE_LAND, TERR_OWN | TERR_NEUTRAL, DIST_CIVIL_CENTRE, 120.0f / 4.0f};
	case BLD_LIGHTHOUSE: return {PLACE_SHORE_TYPE, TERR_OWN | TERR_ALLY | TERR_NEUTRAL, DIST_NONE, 0.0f};
	default: return {PLACE_LAND, TERR_OWN, DIST_NONE, 0.0f};
	}
}
uint8_t distance_class_of(uint8_t type) {
	switch (type) {
	case BLD_TOWN_CENTER: case BLD_MILITARY_COLONY: return DIST_CIVIL_CENTRE; // a colony is a CivilCentre
	case BLD_TOWER: case BLD_SENTRY_TOWER: return DIST_TOWER; // both carry the Tower class
	case BLD_FORTRESS: return DIST_FORTRESS;
	case BLD_OUTPOST: return DIST_OUTPOST;
	default: return DIST_NONE;
	}
}
} // namespace

Simulation::PlacementResult Simulation::check_placement(uint8_t player, int cell_x, int cell_y, uint8_t b_type) const {
	if (player >= MAX_PLAYERS || b_type >= BLD_TYPE_COUNT) return PLACE_OBSTRUCTED;
	const BuildingTypeInfo &binfo = building_type_info(b_type);
	const BuildRestriction rule = build_restriction(b_type);
	const int foot = binfo.footprint;
	// 1. Obstruction (CheckFoundation): the map edge, other buildings, and resources
	// (trees, mines, bushes; a field's grain and carcasses don't block).
	if (!can_place(cell_x, cell_y, foot)) return PLACE_OBSTRUCTED;
	const float x0 = cell_x * GRID, z0 = cell_y * GRID;
	const float x1 = x0 + foot * GRID, z1 = z0 + foot * GRID;
	const float cxw = 0.5f * (x0 + x1), czw = 0.5f * (z0 + z1);
	{
		const int w = std::max(1, (int)std::ceil(2.0f * map_half_ / GRID));
		if (resource_block_tick_ != ticks_ || resource_block_count_ != resources_.count || resource_block_w_ != w) {
			resource_block_tick_ = ticks_;
			resource_block_count_ = resources_.count;
			resource_block_w_ = w;
			resource_block_cells_.assign((size_t)w * w, 0);
			for (size_t r = 0; r < resources_.count; ++r) {
				if (!resources_.alive[r] || resources_.amount[r] <= 0) continue;
				const uint8_t sub = resources_.subtype[r];
				if (sub == RES_SUB_GRAIN || sub == RES_SUB_MEAT) continue;
				// Gaia resource obstructions are about 1.5 m (3/8 tile) across their centre.
				const int gx0 = std::max(0, (int)std::floor((resources_.x[r] - 0.375f + map_half_) / GRID));
				const int gx1 = std::min(w - 1, (int)std::floor((resources_.x[r] + 0.375f + map_half_) / GRID));
				const int gz0 = std::max(0, (int)std::floor((resources_.z[r] - 0.375f + map_half_) / GRID));
				const int gz1 = std::min(w - 1, (int)std::floor((resources_.z[r] + 0.375f + map_half_) / GRID));
				for (int z = gz0; z <= gz1; ++z)
					for (int x = gx0; x <= gx1; ++x) resource_block_cells_[(size_t)z * w + x] = 1;
			}
		}
		const int off = (int)std::lround(map_half_ / GRID);
		for (int dz = 0; dz < foot; ++dz)
			for (int dx = 0; dx < foot; ++dx) {
				const int x = cell_x + dx + off, z = cell_y + dz + off;
				if (x >= 0 && z >= 0 && x < w && z < w && resource_block_cells_[(size_t)z * w + x]) return PLACE_OBSTRUCTED;
			}
	}

	// Gaia scenery (fences, columns) blocks foundations too: its rotated rectangle's bounds.
	for (const Scenery &sc : scenery_) {
		const float c = std::fabs(std::cos(sc.angle)), sn = std::fabs(std::sin(sc.angle));
		const float ex = c * sc.half_w + sn * sc.half_d, ez = sn * sc.half_w + c * sc.half_d;
		if (sc.x + ex > x0 && sc.x - ex < x1 && sc.z + ez > z0 && sc.z - ez < z1) return PLACE_OBSTRUCTED;
	}

	// 2. Terrain passability class: building-land has no water under the
	// footprint, a wall (default-terrain-only, centre point only) stands where
	// land units can wade (NAV_SHALLOW, 2 m), and a dock needs land under it and
	// a sea beside it.
	const bool have_nav = !nav_grid_.cells.blank();
	const float nav_cell = have_nav ? 2.0f * nav_grid_.map_half / (float)nav_grid_.res : GRID;
	auto is_water = [&](int nx, int ny) { // any water, shallow included (building-land MaxWaterDepth 0)
		if (nx < 0 || ny < 0 || nx >= nav_grid_.cells.w || ny >= nav_grid_.cells.h) return false;
		const uint8_t c = nav_grid_.cells.at(nx, ny);
		return c == NAV_WATER || c == NAV_SHALLOW;
	};
	if (have_nav) {
		auto [nx0, ny0] = nav_grid_.world_to_cell(x0 + 0.01f, z0 + 0.01f);
		auto [nx1, ny1] = nav_grid_.world_to_cell(x1 - 0.01f, z1 - 0.01f);
		if (rule.placement == PLACE_LAND) {
			for (int y = ny0; y <= ny1; ++y)
				for (int x = nx0; x <= nx1; ++x)
					if (is_water(x, y)) return PLACE_TERRAIN;
		} else if (rule.placement == PLACE_LAND_SHORE) {
			auto [ncx, ncy] = nav_grid_.world_to_cell(cxw, czw);
			if (nav_grid_.cells.inside(ncx, ncy) && nav_grid_.cells.at(ncx, ncy) == NAV_WATER) return PLACE_TERRAIN;
		} else {
			// CheckShorePlacement: part of the footprint on land, a sea cell at its edge.
			bool land = false, sea = false;
			for (int y = ny0; y <= ny1 && !land; ++y)
				for (int x = nx0; x <= nx1 && !land; ++x)
					land = !is_water(x, y);
			const int ring = std::max(1, (int)std::ceil(GRID / nav_cell));
			for (int y = ny0 - ring; y <= ny1 + ring && !sea; ++y)
				for (int x = nx0 - ring; x <= nx1 + ring && !sea; ++x)
					sea = nav_grid_.is_passable_for(x, y, PASS_SHIP) &&
							nav_grid_.global_region_at(x, y, PASS_SHIP) != 0;
			if (!land) return PLACE_TERRAIN;
			if (!sea) return PLACE_SHORE;
		}
	} else if (rule.placement == PLACE_SHORE_TYPE) {
		return PLACE_SHORE;
	}

	// 3. Territory at the centre. Every RTS0 territory cell comes from a root
	// building, so none is unconnected. Before the first territory pass sizes the
	// grid (a scenario still being set up) there is nothing to check against.
	if (!territory_grid_.blank()) {
		const int owner = territory_owner_at(cxw, czw);
		uint8_t kind;
		if (owner == (int)player) kind = TERR_OWN;
		else if (owner < 0 || owner >= MAX_PLAYERS) kind = TERR_NEUTRAL;
		else if (is_ally(player, (uint8_t)owner) && is_ally((uint8_t)owner, player)) kind = TERR_ALLY;
		else kind = TERR_ENEMY;
		if (!(rule.territory & kind)) return PLACE_TERRITORY;
	}

	// 4. Distance from the player's own structures (foundations included) of the
	// same class, measured between footprint edges.
	if (rule.distance_class != DIST_NONE) {
		const float half = 0.5f * foot * GRID;
		for (const Building &b : buildings_) {
			if (b.team != player || b.hp <= 0.0f || distance_class_of(b.type) != rule.distance_class) continue;
			const float d = std::hypot(b.x - cxw, b.z - czw) - half - b.half_size;
			if (d < rule.min_distance) return PLACE_DISTANCE;
		}
	}
	return PLACE_OK;
}

float Simulation::dock_angle(float x, float z, float half_size) const {
	// Commands.js GetDockAngle: sample 16 points on a circle, find the longest run of
	// consecutive water points and face its middle; widen the circle while every
	// point (or none) is water.
	constexpr int NUM_POINTS = 16;
	constexpr float TWO_PI = 6.28318530718f;
	if (terrain_heights_.empty()) return NAN;
	for (int dist = 0; dist < 4; ++dist) {
		int water[NUM_POINTS];
		int n = 0;
		const float d = half_size * (float)(dist + 1);
		for (int i = 0; i < NUM_POINTS; ++i) {
			const float a = (float)i / NUM_POINTS * TWO_PI;
			if (get_terrain_height(x - d * std::sin(a), z + d * std::cos(a)) < terrain_water_height_) water[n++] = i;
		}
		if (!n) continue;
		int start = 0, count = 0;
		for (int i = 0; i < n; ++i) {
			int c = 0;
			for (int j = 0; j < n - 1; ++j) {
				if ((water[(i + j) % n] + 1) % NUM_POINTS == water[(i + j + 1) % n]) ++c;
				else break;
			}
			if (c > count) { start = i; count = c; }
		}
		if (count != NUM_POINTS - 1)
			return -std::fmod((float)water[start] + count * 0.5f, (float)NUM_POINTS) / NUM_POINTS * TWO_PI;
	}
	return NAN;
}

uint32_t Simulation::pack_angle(float angle) {
	constexpr float TWO_PI = 6.28318530718f;
	float t = std::fmod(angle - DEFAULT_BUILDING_ROT, TWO_PI);
	if (t < 0.0f) t += TWO_PI;
	return (uint32_t)std::lround(t / TWO_PI * 65536.0f) & 0xFFFFu;
}

float Simulation::unpack_angle(uint32_t packed) {
	return DEFAULT_BUILDING_ROT + (float)(packed & 0xFFFFu) / 65536.0f * 6.28318530718f;
}

uint32_t Simulation::cmd_build(uint8_t player, int cell_x, int cell_y, uint8_t b_type, float angle) {
	if (player >= MAX_PLAYERS || b_type >= BLD_TYPE_COUNT) return 0;
	if (!civ_can_build(players_[player].civ, b_type)) return 0; // civ-specific structures
	const BuildingTypeInfo &binfo = building_type_info(b_type);
	rebuild_territory(); // territory from every finished root so far (a no-op when nothing changed)
	if (check_placement(player, cell_x, cell_y, b_type) != PLACE_OK) return 0;
	if (building_required_phase(b_type) > players_[player].phase) return 0;
	if (!entity_limit_allows_building(player, b_type)) return 0; // EntityLimits.js AllowedToBuild

	// 0 A.D. civ structure cost bonuses (e.g. Iberian walls, Han stone discount).
	const Civilization civ = civ_of(player);
	// Team bonuses (e.g. Persian Training Regimes, Mauryan temples) from the player and its allies.
	const uint16_t allies = ally_civ_mask(player);
	// ... and Cost/Resources technologies (Carthage's Colonization).
	const float *tech_cost = tech_mods(player).bld_cost[b_type];
	auto team = [&](ResourceType r) {
		return civ_team_bonus_mult(civ, allies, TEAM_BONUS_BUILDING_COST, (int8_t)r, CIV_UNIT_TEMPLATE_NONE, b_type) *
		       tech_cost[r];
	};
	const float wood = binfo.wood_cost * civ_bonus_mult(civ, CIV_BONUS_BUILDING_WOOD_COST, b_type) * team(RES_WOOD);
	const float stone = binfo.stone_cost * civ_bonus_mult(civ, CIV_BONUS_BUILDING_STONE_COST, b_type) * team(RES_STONE);
	const float food = binfo.food_cost * team(RES_FOOD);
	const float gold = binfo.gold_cost * team(RES_GOLD);
	if (!players_[player].pay(std::round(food), std::round(wood), std::round(gold), std::round(stone)))
		return 0;

	uint32_t bid = spawn_building(cell_x, cell_y, b_type, player, 0.0f);
	if (b_type != BLD_DOCK) buildings_[building_id_to_idx_[bid]].rot = unpack_angle(pack_angle(angle));
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

// template_unit_support_healer.xml Heal values (Range 12, Health 5 every 2 s) after the
// player's Healing Range / Healing Rate technologies.
float Simulation::healer_range(uint8_t player_id) const {
	return 12.0f + tech_mods(player_id).heal_range[UNIT_HEALER];
}

float Simulation::healer_rate(uint8_t player_id) const {
	return HEAL_HP / (HEAL_INTERVAL_S * std::max(0.1f, tech_mods(player_id).heal_interval[UNIT_HEALER]));
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

// 0 A.D. EntityLimits.js: the limit category of a unit (TrainingRestrictions/Category
// of its roster row, else of its class's generic template; a corral's queued
// livestock is an Animal).
static uint8_t unit_limit_category(uint8_t unit_type, uint16_t tmpl) {
	if (unit_type == UNIT_TYPE_NONE) return LIMIT_ANIMAL; // `tmpl` is then the fauna type
	if (tmpl < CIV_UNIT_TEMPLATE_COUNT) return UNIT_LIMITS[tmpl].category;
	return unit_type < UNIT_TYPE_COUNT ? UNIT_TYPE_LIMIT_CATEGORY[unit_type] : LIMIT_NONE;
}

// EntityLimits.js counts every entity the player owns with the category
// (OnGlobalOwnershipChanged, foundations included) plus what its Trainers have
// queued (Trainer.js ChangeCount when a batch is queued).
int Simulation::entity_limit_count(uint8_t player_id, uint8_t category) const {
	if (category == LIMIT_NONE || category >= LIMIT_CATEGORY_COUNT) return 0;
	int n = 0;
	for (size_t i = 0; i < units_.count; ++i)
		if (units_.alive[i] && units_.team[i] == player_id &&
		    unit_limit_category(units_.unit_type[i], units_.tmpl[i]) == category)
			++n;
	for (const auto &b : buildings_) {
		if (b.team != player_id || b.hp <= 0.0f || b.wrecked) continue;
		if (b.type < BLD_TYPE_COUNT && BUILDING_LIMIT_CATEGORY[b.type] == category) ++n;
		for (const auto &o : b.queue)
			if (unit_limit_category(o.unit_type, o.tmpl) == category) ++n;
	}
	if (category == LIMIT_ANIMAL) // livestock penned in the player's corrals
		for (size_t f = 0; f < fauna_.count; ++f) {
			if (!fauna_.alive[f] || fauna_.behavior[f] != FAUNA_CORRALLED) continue;
			auto bit = building_id_to_idx_.find(fauna_.corral_id[f]);
			if (bit != building_id_to_idx_.end() && buildings_[bit->second].team == player_id) ++n;
		}
	return n;
}

int Simulation::entity_limit(uint8_t player_id, uint8_t category) const {
	if (player_id >= MAX_PLAYERS || category == LIMIT_NONE || category >= LIMIT_CATEGORY_COUNT) return -1;
	const uint8_t civ = civ_of(player_id);
	int limit = CIV_ENTITY_LIMITS[civ][category];
	if (limit < 0) return -1;
	// LimitRemovers: lifted once every RequiredTechs is in and the player owns an
	// entity of every RequiredClasses class.
	for (const LimitRemover &r : LIMIT_REMOVERS) {
		if (r.civ != civ || r.category != category) continue;
		const PlayerState &ps = players_[player_id];
		bool lifted = ps.phase >= r.phase && (r.tech == TECH_NONE || ps.has_tech(r.tech));
		if (lifted && r.flags) {
			bool owned = false;
			for (size_t i = 0; i < units_.count && !owned; ++i)
				owned = units_.alive[i] && units_.team[i] == player_id && units_.tmpl[i] < CIV_UNIT_TEMPLATE_COUNT &&
				        (civ_unit_template(units_.tmpl[i]).flags & r.flags) == r.flags;
			lifted = owned;
		}
		if (lifted) return -1;
	}
	// LimitChangers: each finished entity with the class adds its value.
	for (const LimitChanger &c : LIMIT_CHANGERS) {
		if (c.civ != civ || c.category != category) continue;
		if (c.bld < BLD_TYPE_COUNT) {
			for (const auto &b : buildings_)
				if (b.team == player_id && b.type == c.bld && b.hp > 0.0f && !b.wrecked && b.build_progress >= 1.0f)
					limit += c.value;
		} else {
			for (size_t i = 0; i < units_.count; ++i)
				if (units_.alive[i] && units_.team[i] == player_id && units_.tmpl[i] == c.tmpl) limit += c.value;
		}
	}
	return limit;
}

bool Simulation::entity_limit_allows_unit(uint8_t player_id, uint8_t unit_type, uint16_t tmpl, int count) const {
	const uint8_t cat = unit_limit_category(unit_type, tmpl);
	if (cat == LIMIT_NONE) return true;
	const int limit = entity_limit(player_id, cat);
	return limit < 0 || entity_limit_count(player_id, cat) + count <= limit;
}

bool Simulation::entity_limit_allows_building(uint8_t player_id, uint8_t bld_type) const {
	const uint8_t cat = bld_type < BLD_TYPE_COUNT ? BUILDING_LIMIT_CATEGORY[bld_type] : LIMIT_NONE;
	if (cat == LIMIT_NONE) return true;
	const int limit = entity_limit(player_id, cat);
	return limit < 0 || entity_limit_count(player_id, cat) + 1 <= limit;
}

bool Simulation::can_train_at(uint8_t player_id, uint8_t unit_type, uint8_t bld_type, uint16_t tmpl) const {
	if (player_id >= MAX_PLAYERS) return false;
	if (!entity_limit_allows_unit(player_id, unit_type, tmpl)) return false; // EntityLimits (Hero 1, WarDog, ...)
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
	// The civ's unit of this class that this building trains (0 A.D. Trainer
	// lists), cheapest phase first; else the class's standard unit, except that
	// heroes and champions only ever come from their own structures.
	if (!(civ_unit_template(tmpl).trainers & bld_bit(b.type))) {
		const uint8_t civ = civ_of(b.team);
		uint16_t best = CIV_UNIT_TEMPLATE_NONE;
		for (uint16_t i : civ_roster_ids(civ)) {
			const CivUnitTemplate &u = civ_unit_template(i);
			if (u.unit_type != (uint8_t)u_type || (u.flags & ROSTER_SIEGE_TOWER) || !(u.trainers & bld_bit(b.type))) continue;
			if (best == CIV_UNIT_TEMPLATE_NONE || u.phase < civ_unit_template(best).phase) best = i;
		}
		if (best != CIV_UNIT_TEMPLATE_NONE) tmpl = best;
		else if (civ_unit_template(tmpl).flags & (ROSTER_HERO | ROSTER_CHAMPION)) return false;
		else return cmd_train_template_at(player_id, building_id, tmpl, false);
	}
	return cmd_train_template(player_id, building_id, tmpl);
}

bool Simulation::cmd_train_template(uint8_t player_id, uint32_t building_id, uint16_t tmpl) {
	return cmd_train_template_at(player_id, building_id, tmpl, true);
}

// `exclusive`: the building must be one 0 A.D. trains the unit at (by-name
// training); cmd_train's class requests keep generic buildings working.
bool Simulation::cmd_train_template_at(uint8_t player_id, uint32_t building_id, uint16_t tmpl, bool exclusive) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return false;
	Building &b = buildings_[bit->second];
	if (b.team != player_id || player_id >= MAX_PLAYERS || b.build_progress < 1.0f) return false;
	if (b.queue.size() >= 8 || b.upgrade_to != BLD_TYPE_COUNT) return false;
	if (tmpl >= CIV_UNIT_TEMPLATE_COUNT) return false;

	PlayerState &ps = players_[player_id];
	const CivUnitTemplate &t = civ_unit_template(tmpl);
	if (t.civ != ps.civ) return false;
	if (exclusive && !(t.trainers & bld_bit(b.type))) return false; // 0 A.D. Trainer list
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
		* unit_tech_mods(player_id, t.unit_type, tmpl).train_time * ai_time_mult(player_id) * promotion_train_time_mult(tmpl);
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
	// Cost/Resources technologies (e.g. Hippocratic Oath's free healers) by the template's own classes.
	const float *tech_cost = tmpl < CIV_UNIT_TEMPLATE_COUNT ? tech_mods(pid).tmpl[tmpl].cost : nullptr;
	for (int r = 0; r < RES_COUNT; ++r)
		out[r] = std::round(out[r] * civ_team_bonus_mult(civ, allies, TEAM_BONUS_UNIT_COST, (int8_t)r, tmpl) *
		                    (tech_cost ? tech_cost[r] : 1.0f));
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
		const FaunaTrainCost &c = fauna_train_cost(corral_animal_for(civ_of(b.team), b.queue[queue_idx].tmpl));
		ps.refund((float)c.res[0], (float)c.res[1], (float)c.res[3], (float)c.res[2]);
		b.queue.erase(b.queue.begin() + queue_idx);
		return true;
	}

	uint16_t qtmpl = b.queue[queue_idx].tmpl;
	const UnitTypeInfo &uinfo = (qtmpl < CIV_UNIT_TEMPLATE_COUNT) ? civ_template_unit_info(qtmpl, utype)
		: civ_unit_info(civ_of(b.team), utype);
	float cost[RES_COUNT];
	unit_train_cost(player_id, qtmpl, uinfo, cost);
	ps.refund(cost[RES_FOOD], cost[RES_WOOD], cost[RES_GOLD], cost[RES_STONE]);

	b.queue.erase(b.queue.begin() + queue_idx);
	return true;
}

bool Simulation::cmd_tribute(uint8_t from, uint8_t to, const int amounts[RES_COUNT]) {
	// 0 A.D. Player.js TributeResource: both players must still be in the
	// match, amounts are non-negative integers, and the payment is all or
	// nothing (SubtractResourcesOrNotify).
	if (!amounts || from >= MAX_PLAYERS || to >= MAX_PLAYERS || from == to) return false;
	if (!player_in_match(from) || !player_in_match(to)) return false;
	int total = 0;
	for (int r = 0; r < RES_COUNT; ++r) {
		if (amounts[r] < 0) return false;
		if ((float)amounts[r] > players_[from].res[r]) return false;
		total += amounts[r];
	}
	if (total == 0) return false;
	for (int r = 0; r < RES_COUNT; ++r) {
		players_[from].res[r] -= (float)amounts[r];
		players_[to].res[r] += (float)amounts[r];
		stats_[from].tributes_sent += (uint32_t)amounts[r];
		stats_[to].tributes_received += (uint32_t)amounts[r];
		note_tribute(from, to, (uint8_t)r, amounts[r]); // Petra's TributeExchanged event
	}
	return true;
}

bool Simulation::cmd_tribute(uint8_t from, uint8_t to, uint8_t res_type, int amount) {
	if (res_type >= RES_COUNT || amount <= 0) return false;
	int amounts[RES_COUNT] = {};
	amounts[res_type] = amount;
	return cmd_tribute(from, to, amounts);
}

bool Simulation::send_tribute(uint8_t player_id, uint8_t res_type, int amount) {
	return cmd_tribute(0, player_id, res_type, amount);
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
	return cmd_tribute(petra_pid_, 0, res_type, amount);
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
	// 0 A.D. pair_*.json (Researcher.js, TechnologyManager.js): the player picks one of the
	// two; once either is researched or started, the other is gone.
	if (const uint8_t partner = tech_pair_partner(tech_id); partner != TECH_NONE) {
		if (ps.has_tech(partner)) return TECH_ERR_PAIRED;
		for (const auto &b : buildings_)
			if (b.team == player_id && b.research_tech == partner) return TECH_ERR_PAIRED;
	}
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
	// han_minister_garrison.json (Excellency of Works): each minister garrisoned in a
	// structure other than the Imperial Ministry cuts its technologies' cost and time by 2%.
	const float ministers = b.type == BLD_MINISTRY ? 1.0f : std::pow(0.98f, (float)garrisoned_ministers(b));
	auto team = [&](uint8_t stat, int8_t r) {
		return civ_team_bonus_mult(civ, allies, stat, r, CIV_UNIT_TEMPLATE_NONE, b.type) * ministers;
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
		players_[refund_to].refund(b.upgrade_paid[RES_FOOD], b.upgrade_paid[RES_WOOD], b.upgrade_paid[RES_GOLD], b.upgrade_paid[RES_STONE]);
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
	if (b.team < MAX_PLAYERS)
		new_capture *= civ_bonus_mult(civ_of(b.team), CIV_BONUS_BUILDING_CAPTURE_POINTS, to) * tech_mods(b.team).bld_capture[to];
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
	b.garrison_cap = structure_garrison_cap(b);
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
		m.train_time[t] = m.heal_interval[t] = m.trade_gain[t] = m.pack_time[t] = 1.0f;
		m.range[t] = m.heal_range[t] = m.regen[t] = m.idle_regen[t] = 0.0f;
		m.armor[t][0] = m.armor[t][1] = m.armor[t][2] = 0.0f;
	}
	for (auto &tm : m.tmpl) {
		for (float &g : tm.gather) g = 1.0f;
		for (float &c : tm.cost) c = 1.0f;
		tm.attack = tm.health = tm.speed = tm.train_time = 1.0f;
		tm.range = tm.armor[0] = tm.armor[1] = tm.armor[2] = 0.0f;
	}

	for (int b = 0; b < BLD_TYPE_COUNT; ++b) {
		m.bld_health[b] = m.bld_attack[b] = m.bld_build_time[b] = m.bld_pop_bonus[b] = 1.0f;
		m.bld_research_time[b] = m.bld_arrow_mul[b] = 1.0f;
		m.bld_range[b] = m.bld_arrow_add[b] = m.bld_garrison_heal[b] = 0.0f;
		m.bld_capture[b] = 1.0f;
		for (float &c : m.bld_cost[b]) c = 1.0f;
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
			auto apply = [&](float &v) {
				v = (e.op == TOP_MUL) ? v * e.value : (e.op == TOP_SET) ? e.value : v + e.value;
			};
			// Cost/Resources/<res>: TechStat order is food, wood, stone, metal.
			const int cost_res = e.stat == TSTAT_COST_FOOD ? RES_FOOD : e.stat == TSTAT_COST_WOOD ? RES_WOOD
				: e.stat == TSTAT_COST_STONE ? RES_STONE : e.stat == TSTAT_COST_METAL ? RES_GOLD : -1;
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
					case TSTAT_PACK_TIME: apply(m.pack_time[t]); break;
					case TSTAT_REGEN: apply(m.regen[t]); break;
					case TSTAT_IDLE_REGEN: apply(m.idle_regen[t]); break;
					default: break;
				}
			}
			for (uint16_t ti = 0; ti < CIV_UNIT_TEMPLATE_COUNT; ++ti) {
				const CivUnitTemplate &ct = civ_unit_template(ti);
				if (!tech_effect_applies(e, ct.tech_classes)) continue;
				TechMods::Template &tm = m.tmpl[ti];
				bool ranged = template_has_ranged_attack(ti, ct.unit_type, ct.civ);
				uint8_t main_dmg = (ct.crush_damage > ct.hack_damage && ct.crush_damage > ct.pierce_damage) ? 2
					: (ct.pierce_damage > ct.hack_damage) ? 1 : 0;
				switch (e.stat) {
					case TSTAT_GATHER_GRAIN: case TSTAT_GATHER_FRUIT: case TSTAT_GATHER_MEAT: case TSTAT_GATHER_FISH:
					case TSTAT_GATHER_WOOD: case TSTAT_GATHER_STONE: case TSTAT_GATHER_METAL:
						apply(tm.gather[e.stat - TSTAT_GATHER_GRAIN]); break;
					case TSTAT_MELEE_HACK: case TSTAT_MELEE_PIERCE: case TSTAT_MELEE_CRUSH:
						if (!ranged && e.stat - TSTAT_MELEE_HACK == main_dmg) apply(tm.attack);
						break;
					case TSTAT_RANGED_HACK: case TSTAT_RANGED_PIERCE: case TSTAT_RANGED_CRUSH:
						if (ranged && e.stat - TSTAT_RANGED_HACK == main_dmg) apply(tm.attack);
						break;
					case TSTAT_ARMOR_HACK: case TSTAT_ARMOR_PIERCE: case TSTAT_ARMOR_CRUSH:
						apply(tm.armor[e.stat - TSTAT_ARMOR_HACK]); break;
					case TSTAT_HEALTH: apply(tm.health); break;
					case TSTAT_SPEED: apply(tm.speed); break;
					case TSTAT_RANGE: if (ranged) apply(tm.range); break;
					case TSTAT_BUILD_TIME: apply(tm.train_time); break;
					case TSTAT_COST_FOOD: case TSTAT_COST_WOOD: case TSTAT_COST_STONE: case TSTAT_COST_METAL:
						apply(tm.cost[cost_res]); break;
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
					case TSTAT_GARRISON_HEAL: apply(m.bld_garrison_heal[b]); break;
					case TSTAT_CAPTURE_POINTS: apply(m.bld_capture[b]); break;
					case TSTAT_COST_FOOD: case TSTAT_COST_WOOD: case TSTAT_COST_STONE: case TSTAT_COST_METAL:
						apply(m.bld_cost[b][cost_res]); break;
					default: break;
				}
			}
		}
	}
}

void Simulation::rebuild_all_tech_mods() {
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) compute_tech_mods(p, tech_mods_[p]);
}

Simulation::UnitTechMods Simulation::unit_tech_mods(const TechMods &m, uint8_t unit_type, uint16_t tmpl) {
	if (tmpl < CIV_UNIT_TEMPLATE_COUNT) {
		const TechMods::Template &tm = m.tmpl[tmpl];
		return {tm.attack, tm.health, tm.speed, tm.range, tm.train_time, tm.armor, tm.gather};
	}
	const uint8_t t = unit_type < UNIT_TYPE_COUNT ? unit_type : 0;
	return {m.attack[t], m.health[t], m.speed[t], m.range[t], m.train_time[t], m.armor[t], m.gather[t]};
}

void Simulation::apply_tech_mods_to_unit(size_t idx) {
	uint8_t team = units_.team[idx];
	uint8_t t = units_.unit_type[idx];
	if (team >= MAX_PLAYERS || t >= UNIT_TYPE_COUNT) return;
	const UnitTechMods m = unit_tech_mods(team, t, units_.tmpl[idx]);
	units_.attack_damage[idx] *= m.attack;
	units_.max_hp[idx] *= m.health;
	units_.hp[idx] = units_.max_hp[idx];
	units_.speed[idx] *= m.speed;
	units_.attack_range[idx] += m.range;
}

// 0 A.D. ModificationsManager: a finished technology changes every existing
// entity it affects, not only new ones. Rescale by the new/old modifier ratio.
void Simulation::complete_research(uint8_t player_id, uint8_t tech_id) {
	if (player_id >= MAX_PLAYERS) return;
	PlayerState &ps = players_[player_id];
	ps.techs.set(tech_id);
	if (tech_id == TECH_PHASE_TOWN) ps.phase = std::max<uint8_t>(ps.phase, 2);
	else if (tech_id == TECH_PHASE_CITY) ps.phase = 3;
	auto old_ptr = std::make_unique<TechMods>(tech_mods_[player_id]); // tens of KB, keep it off the stack
	const TechMods &old = *old_ptr;
	compute_tech_mods(player_id, tech_mods_[player_id]);
	const TechMods &m = tech_mods_[player_id];
	for (size_t i = 0; i < units_.count; ++i) {
		if (units_.team[i] != player_id) continue;
		uint8_t t = units_.unit_type[i];
		if (t >= UNIT_TYPE_COUNT) continue;
		const UnitTechMods now = unit_tech_mods(m, t, units_.tmpl[i]);
		const UnitTechMods was = unit_tech_mods(old, t, units_.tmpl[i]);
		if (now.attack != was.attack) units_.attack_damage[i] *= now.attack / was.attack;
		if (now.health != was.health) {
			float r = now.health / was.health;
			units_.max_hp[i] *= r;
			units_.hp[i] *= r;
		}
		if (now.speed != was.speed) units_.speed[i] *= now.speed / was.speed;
		units_.attack_range[i] += now.range - was.range;
	}
	for (auto &b : buildings_) {
		if (b.team != player_id || b.type >= BLD_TYPE_COUNT) continue;
		float r = m.bld_health[b.type] / old.bld_health[b.type];
		if (r != 1.0f) {
			b.max_hp *= r;
			b.hp *= r;
		}
		// Capturable.js OnValueModification: every side's points scale with the new maximum.
		const float cr = m.bld_capture[b.type] / old.bld_capture[b.type];
		if (cr != 1.0f) {
			b.max_capture_points *= cr;
			b.capture_points *= cr;
			for (float &p : b.capture_points_by_team) p *= cr;
		}
	}
}

int32_t Simulation::carry_cap(size_t i, uint8_t res) const {
	uint8_t team = units_.team[i], t = units_.unit_type[i];
	if (team >= MAX_PLAYERS || t >= UNIT_TYPE_COUNT || res >= RES_COUNT) return units_.carry_capacity[i];
	return units_.carry_capacity[i] + tech_mods_[team].carry[t][res];
}

// 0 A.D. Barter.js constants.
static constexpr int BARTER_DEAL_AMOUNT = 100;
static constexpr int BARTER_BATCH_SIZE = 5;
static constexpr float BARTER_CONSTANT_DIFFERENCE = 10.0f;
static constexpr float BARTER_DIFFERENCE_PER_DEAL = 2.0f;
static constexpr float BARTER_DIFFERENCE_RESTORE = 0.5f;
static constexpr float BARTER_RESTORE_INTERVAL = 5.0f; // RESTORE_TIMER_INTERVAL, 5000 ms
// The overall difference (dynamic + constant) stays within +-99% of the true price.
static constexpr float BARTER_MAX_DIFFERENCE = BARTER_DEAL_AMOUNT * 0.99f - BARTER_CONSTANT_DIFFERENCE;

bool Simulation::can_barter(uint8_t player_id) const {
	// Player.js CanBarter: the player owns an entity with the Barter class (the Market).
	if (player_id >= MAX_PLAYERS) return false;
	for (const auto &b : buildings_)
		if (b.team == player_id && b.type == BLD_MARKET && b.build_progress >= 1.0f && b.hp > 0.0f) return true;
	return false;
}

int Simulation::barter_quote(ResourceType sell, ResourceType buy, int amount) const {
	// Barter.js ExchangeResources: round(sell price / buy price * amount), with
	// every resource's true price 100 and the player BarterMultiplier 1.0.
	if (sell == buy || sell >= RES_COUNT || buy >= RES_COUNT) return 0;
	if (amount != BARTER_DEAL_AMOUNT && amount != BARTER_BATCH_SIZE * BARTER_DEAL_AMOUNT) return 0;
	return (int)std::lround(market_sell_price(sell) / market_buy_price(buy) * (float)amount);
}

bool Simulation::cmd_barter(uint8_t player_id, ResourceType sell, ResourceType buy, int amount) {
	if (player_id >= MAX_PLAYERS) return false;
	const int received = barter_quote(sell, buy, amount);
	if (received <= 0 || !can_barter(player_id)) return false;
	PlayerState &ps = players_[player_id];
	if (ps.res[sell] < (float)amount) return false;
	ps.res[sell] -= (float)amount;
	ps.res[buy] += (float)received;
	stats_[player_id].resources_sold[sell] += (uint32_t)amount;
	stats_[player_id].resources_bought[buy] += (uint32_t)received;

	// Each deal of 100 moves the sold resource's price down and the bought one's up by 2%.
	const float difference = BARTER_DIFFERENCE_PER_DEAL * (float)amount / (float)BARTER_DEAL_AMOUNT;
	barter_price_diff_[sell] = std::clamp(barter_price_diff_[sell] - difference, -BARTER_MAX_DIFFERENCE, BARTER_MAX_DIFFERENCE);
	barter_price_diff_[buy] = std::clamp(barter_price_diff_[buy] + difference, -BARTER_MAX_DIFFERENCE, BARTER_MAX_DIFFERENCE);
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

// globalscripts/Trade.js. Distances and the map size are in metres; one RTS0
// world unit is one 0 A.D. terrain tile, 4 m.
static constexpr double TRADE_TILE_METRES = 4.0;
static double trade_gain_curve(double distance_sq, double map_size) {
	return distance_sq / (1.0 + 0.25 * std::sqrt(distance_sq) / map_size);
}
static double trade_gain_normalization(double map_size) {
	// 1 resource for a 100 m route on a 1024 m map, before the trader's GainMultiplier.
	return std::sqrt(1024.0 / map_size) / trade_gain_curve(10000.0, map_size);
}
// template_unit_support_trader.xml Trader/GainMultiplier.
static constexpr double TRADER_GAIN_MULTIPLIER = 0.75;
// template_structure_economic_market.xml Market/InternationalBonus.
static constexpr double MARKET_INTERNATIONAL_BONUS = 0.2;

Simulation::TradeGain Simulation::calculate_trade_gain(const Building &from, const Building &to, uint8_t trader_owner,
		uint8_t trader_type) const {
	// 0 A.D. Market.js CalculateTraderGain: the trader's owner gets
	// round(norm * GainMultiplier * TradeGain(d^2, mapSize)), which grows with
	// the square of the distance. When the two markets have different owners,
	// each owner also gets that gain times its market's InternationalBonus.
	TradeGain g;
	const double map_size = std::max(1.0, 2.0 * (double)map_half_ * TRADE_TILE_METRES);
	const double dx = ((double)from.x - (double)to.x) * TRADE_TILE_METRES;
	const double dz = ((double)from.z - (double)to.z) * TRADE_TILE_METRES;
	// Trader/GainMultiplier with the civ bonus (Persian Darics +25% on land
	// traders, Carthaginian +25% on merchant ships only), the Handicraft /
	// Advanced Handicraft techs and an AI player's difficulty applied.
	const CivInfo &ci = civ_info(civ_of(trader_owner));
	const float civ_mult = trader_type == UNIT_MERCHANT_SHIP ? ci.ship_trade_gain_mult : ci.trade_gain_mult;
	const double mult = TRADER_GAIN_MULTIPLIER * (double)civ_mult *
	                    (double)tech_mods(trader_owner).trade_gain[UNIT_TRADER] * (double)ai_gather_mult(trader_owner);
	g.trader = (int)std::lround(trade_gain_normalization(map_size) * mult * trade_gain_curve(dx * dx + dz * dz, map_size));
	if (from.team != to.team) {
		// Han "Silk Road": allies' markets get +20% international bonus.
		auto bonus = [&](uint8_t owner) {
			return MARKET_INTERNATIONAL_BONUS * (double)team_bonus_mult(owner, TEAM_BONUS_TRADE_INTERNATIONAL);
		};
		g.market1 = (int)std::lround(g.trader * bonus(from.team));
		g.market2 = (int)std::lround(g.trader * bonus(to.team));
	}
	return g;
}

float Simulation::trader_gains(const Building *a, const Building *b) const {
	if (!a || !b) return 0.0f;
	return (float)calculate_trader_gain(*a, *b, a->team);
}

bool Simulation::can_trade_at(uint8_t trader_owner, const Building &b, uint8_t trader_type) const {
	// Trader.js CanTrade: a finished market whose owner the trader's owner is not
	// at war with. Land traders use Markets, merchant ships Docks (Market TradeType).
	const uint8_t kind = trader_type == UNIT_MERCHANT_SHIP ? BLD_DOCK : BLD_MARKET;
	return b.type == kind && b.build_progress >= 1.0f && b.hp > 0.0f &&
	       b.team < MAX_PLAYERS && trader_owner < MAX_PLAYERS && !is_hostile(trader_owner, b.team);
}

uint16_t Simulation::dock_sea(const Building &b) const {
	float x, z;
	uint16_t sea = 0;
	return nearest_ship_cell(b, x, z, &sea) ? sea : 0;
}

bool Simulation::nearest_ship_cell(const Building &b, float &out_x, float &out_z, uint16_t *out_sea) const {
	// The nearest water cell within two cells of the footprint that belongs to a sea.
	if (nav_grid_.cells.blank()) return false;
	auto [cx, cy] = nav_grid_.world_to_cell(b.x, b.z);
	const float cell = 2.0f * nav_grid_.map_half / (float)nav_grid_.res;
	const int r = (int)std::ceil(b.half_size / cell) + 2;
	int best_d2 = INT32_MAX, bx = 0, by = 0;
	uint16_t best = 0;
	for (int dy = -r; dy <= r; ++dy)
		for (int dx = -r; dx <= r; ++dx) {
			const int x = cx + dx, y = cy + dy;
			if (x < 0 || y < 0 || x >= nav_grid_.cells.w || y >= nav_grid_.cells.h) continue;
			if (!nav_grid_.is_passable_for(x, y, PASS_SHIP)) continue;
			const int d2 = dx * dx + dy * dy;
			if (d2 >= best_d2) continue;
			const uint16_t reg = nav_grid_.global_region_at(x, y, PASS_SHIP);
			if (reg) { best = reg; best_d2 = d2; bx = x; by = y; }
		}
	if (!best) return false;
	out_x = ((float)bx + 0.5f) * cell - nav_grid_.map_half;
	out_z = ((float)by + 0.5f) * cell - nav_grid_.map_half;
	if (out_sea) *out_sea = best;
	return true;
}

bool Simulation::has_trade_route(uint8_t player_id, uint8_t trader_type) const {
	if (trader_type == UNIT_MERCHANT_SHIP) {
		// Two docks on the same sea.
		std::vector<uint16_t> seas;
		for (const auto &b : buildings_) {
			if (!can_trade_at(player_id, b, trader_type)) continue;
			const uint16_t sea = dock_sea(b);
			if (!sea) continue;
			if (std::find(seas.begin(), seas.end(), sea) != seas.end()) return true;
			seas.push_back(sea);
		}
		return false;
	}
	int n = 0;
	for (const auto &b : buildings_)
		if (can_trade_at(player_id, b, trader_type) && ++n >= 2) return true;
	return false;
}

uint8_t Simulation::next_trading_goods(uint8_t player) {
	// Player.js GetNextTradingGoods: a random draw per load against the player's
	// tradingGoods, in 0 A.D.'s order (food, wood, stone, metal), the last one
	// taking whatever is left.
	const uint8_t *pct = trading_goods(player);
	const uint32_t r = sim_rng_.next_u32() % 100u;
	uint32_t sum = 0;
	for (uint8_t res : {RES_FOOD, RES_WOOD, RES_STONE}) {
		sum += pct[res];
		if (r < sum) return res;
	}
	return RES_GOLD;
}

bool Simulation::cmd_set_trading_goods(uint8_t player, const uint8_t pct[RES_COUNT]) {
	if (player >= MAX_PLAYERS || !pct) return false;
	int sum = 0;
	for (int r = 0; r < RES_COUNT; ++r) sum += pct[r];
	if (sum != 100) return false; // Player.js: "Invalid trading goods probability"
	for (int r = 0; r < RES_COUNT; ++r) trading_goods_[player][r] = pct[r];
	return true;
}

void Simulation::credit_trade(uint8_t player_id, uint8_t res, int amount) {
	if (player_id >= MAX_PLAYERS || res >= RES_COUNT || amount <= 0) return;
	PlayerState &ps = players_[player_id];
	stats_[player_id].trade_income += (uint32_t)amount;
	// Same accounting as a gather drop-off (tick_gather): player 0 feeds the match record.
	if (player_id == 0) {
		ps.res[res] += (float)amount;
		if (res == RES_WOOD) record_.total_wood_gathered += amount;
		else if (res == RES_GOLD) record_.total_gold_gathered += amount;
		else if (res == RES_STONE) record_.total_stone_gathered += amount;
		else record_.total_food_gathered += amount;
	} else {
		ps.res[res] = std::min(9999.0f, ps.res[res] + (float)amount);
	}
	ps.gathered_window[res] += (float)amount;
}

bool Simulation::trade_route_ok(size_t i, uint32_t market_a_id, uint32_t market_b_id) const {
	if (market_a_id == 0 || market_b_id == 0 || market_a_id == market_b_id) return false;
	if (!units_.alive[i] || !unit_is_trader(units_.unit_type[i])) return false;
	auto itA = building_id_to_idx_.find(market_a_id);
	auto itB = building_id_to_idx_.find(market_b_id);
	if (itA == building_id_to_idx_.end() || itB == building_id_to_idx_.end()) return false;
	const Building &bA = buildings_[itA->second];
	const Building &bB = buildings_[itB->second];
	const uint8_t owner = units_.team[i];
	const uint8_t ttype = units_.unit_type[i];
	if (!can_trade_at(owner, bA, ttype) || !can_trade_at(owner, bB, ttype)) return false;
	return ttype != UNIT_MERCHANT_SHIP || (dock_sea(bA) != 0 && dock_sea(bA) == dock_sea(bB));
}

// UnitAI Trade order: target is the first market, x2 carries the second market's id bits.
static float trade_market_bits(uint32_t id) { float f; std::memcpy(&f, &id, sizeof f); return f; }
static uint32_t trade_market_id(float bits) { uint32_t id; std::memcpy(&id, &bits, sizeof id); return id; }

void Simulation::order_trade(const uint32_t *unit_ids, size_t count, uint32_t market_a_id, uint32_t market_b_id,
		bool queued) {
	if (count == 0 || market_a_id == 0 || market_b_id == 0 || market_a_id == market_b_id) return;
	auto itA = building_id_to_idx_.find(market_a_id);
	auto itB = building_id_to_idx_.find(market_b_id);
	if (itA == building_id_to_idx_.end() || itB == building_id_to_idx_.end()) return;
	const Building &bA = buildings_[itA->second];
	std::vector<uint32_t> traders;
	for (size_t k = 0; k < count; ++k) {
		const int32_t uidx = lookup_unit(unit_ids[k]);
		if (uidx >= 0 && trade_route_ok((size_t)uidx, market_a_id, market_b_id)) traders.push_back(unit_ids[k]);
	}
	UnitOrder o; o.kind = ORDER_TRADE; o.target = market_a_id; o.x = bA.x; o.z = bA.z;
	o.x2 = trade_market_bits(market_b_id);
	const std::vector<uint32_t> now = split_queued(traders.data(), traders.size(), o, queued);
	for (uint32_t uid : now) {
		const int32_t uidx = lookup_unit(uid);
		if (uidx < 0) continue;
		// A trader already on this route (back from a flight) keeps its leg and goods.
		auto rit = trader_routes_.find(uid);
		if (rit != trader_routes_.end() && rit->second.first == market_a_id && rit->second.second == market_b_id &&
				applying_queued_) {
			const int leg = trader_leg_.count(uid) ? trader_leg_[uid] : 0;
			auto dit = building_id_to_idx_.find(leg == 0 ? market_a_id : market_b_id);
			units_.action_state[uidx] = ACTION_TRADE;
			units_.has_order[uidx] = 1;
			// The order it resumes may follow another (a queued walk): it is trading again.
			set_order(uidx, ORDER_TRADE, 0, market_a_id, bA.x, bA.z);
			units_.order_x2[uidx] = o.x2;
			units_.target_unit_id[uidx] = 0;
			units_.target_building_id[uidx] = 0;
			units_.waypoint_cur[uidx] = units_.waypoint_count[uidx] = 0;
			units_.target_x[uidx] = buildings_[dit->second].x;
			units_.target_z[uidx] = buildings_[dit->second].z;
			continue;
		}
		trader_routes_[uid] = {market_a_id, market_b_id};
		trader_leg_[uid] = 0;
		units_.action_state[uidx] = ACTION_TRADE;
		units_.has_order[uidx] = 1;
		set_order(uidx, ORDER_TRADE, 0, market_a_id, bA.x, bA.z);
		units_.order_x2[uidx] = o.x2;
		// Empty until it loads goods at the first market (Trader.js: no gain on the first arrival).
		units_.carry[uidx] = 0;
		units_.target_x[uidx] = bA.x;
		units_.target_z[uidx] = bA.z;
		units_.anchor_x[uidx] = bA.x;
		units_.anchor_z[uidx] = bA.z;
	}
}

void Simulation::order_trade_auto(const uint32_t *unit_ids, size_t count) {
	if (count == 0) return;
	std::vector<const Building *> markets;
	for (size_t k = 0; k < count; ++k) {
		int32_t uidx = lookup_unit(unit_ids[k]);
		if (uidx < 0 || !units_.alive[uidx] || !unit_is_trader(units_.unit_type[uidx])) continue;
		const uint8_t owner = units_.team[uidx];
		const uint8_t ttype = units_.unit_type[uidx];
		const bool ship = ttype == UNIT_MERCHANT_SHIP;
		// The gain grows with distance, so the best route is the farthest pair of
		// markets the owner can trade at, own or not. Starting at the nearer end
		// of that pair saves the empty first walk. A merchant ship's docks must
		// share its own sea.
		uint16_t my_sea = 0;
		if (ship) {
			auto [ucx, ucy] = nav_grid_.world_to_cell(units_.pos_x[uidx], units_.pos_z[uidx]);
			my_sea = nav_grid_.global_region_at(ucx, ucy, PASS_SHIP);
		}
		markets.clear();
		for (const auto &b : buildings_)
			if (can_trade_at(owner, b, ttype) && (!ship || (my_sea && dock_sea(b) == my_sea))) markets.push_back(&b);
		const Building *best_a = nullptr, *best_b = nullptr;
		float best_d2 = -1.0f;
		for (size_t i = 0; i < markets.size(); ++i) {
			for (size_t j = i + 1; j < markets.size(); ++j) {
				const Building &bi = *markets[i], &bj = *markets[j];
				const float d2 = (bi.x - bj.x) * (bi.x - bj.x) + (bi.z - bj.z) * (bi.z - bj.z);
				if (d2 > best_d2) { best_d2 = d2; best_a = &bi; best_b = &bj; }
			}
		}
		if (!best_a) continue; // no route: the trader waits until two markets exist
		const float px = units_.pos_x[uidx], pz = units_.pos_z[uidx];
		const float da = (best_a->x - px) * (best_a->x - px) + (best_a->z - pz) * (best_a->z - pz);
		const float db = (best_b->x - px) * (best_b->x - px) + (best_b->z - pz) * (best_b->z - pz);
		if (db < da) std::swap(best_a, best_b);
		uint32_t uid = units_.id[uidx];
		order_trade(&uid, 1, best_a->id, best_b->id);
	}
}

void Simulation::cmd_move(const uint32_t *unit_ids, size_t count, float tx, float tz) {
	if (count == 0) return;
	// 0 A.D. Commands.js "walk": GetFormationUnitAIs splits the units into formation
	// controllers and individuals. One unit on its own always leaves its formation.
	if (count == 1) {
		formation_remove_units(unit_ids, count);
		cmd_move_individual(unit_ids, count, tx, tz);
		return;
	}
	std::vector<uint32_t> individual;
	const std::vector<size_t> ctrls = formation_controllers_for(unit_ids, count, FORMATION_NONE, false, individual);
	for (size_t ci : ctrls) {
		FormationController &fc = formations_[ci];
		fc.goal_x = std::clamp(tx, -map_half_ + 1.0f, map_half_ - 1.0f);
		fc.goal_z = std::clamp(tz, -map_half_ + 1.0f, map_half_ - 1.0f);
		fc.order = FORDER_WALK;
		formation_move_members_into(ci, true, true);
		for (uint32_t id : fc.members) { // the members carry the walk order (order_walk adds force)
			const int32_t idx = lookup_unit(id);
			if (idx >= 0) begin_walk_order((size_t)idx);
		}
	}
	if (!individual.empty()) cmd_move_individual(individual.data(), individual.size(), tx, tz);
}

void Simulation::cmd_move_individual(const uint32_t *unit_ids, size_t count, float tx, float tz) {
	if (count == 0) return;

	// Units not in a formation spread over a square grid around the target, turned to face
	// the walk (RTS0's group move; 0 A.D. sends them all to the point and lets them push).
	const float spacing = 1.35f;
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
	float cosH = 1.0f, sinH = 0.0f;
	if (validCount > 0) {
		centerX /= (float)validCount;
		centerZ /= (float)validCount;
		const float heading = atan2f(tz - centerZ, tx - centerX);
		cosH = cosf(heading);
		sinH = sinf(heading);
	}

	for (size_t i = 0; i < count; ++i) {
		int32_t idx = lookup_unit(unit_ids[i]);
		if (idx < 0 || (size_t)idx >= units_.count || !units_.alive[idx]) continue;

		const int side = std::max(1, (int)std::ceil(std::sqrt((float)count)));
		const int row = (int)i / side;
		const int col = (int)i % side;
		const float ox = (col - (side - 1) * 0.5f) * spacing;
		const float oz = (row - (side - 1) * 0.5f) * spacing;
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
		if (beach_goal_ && pass != PASS_SHIP && wp.count > 0) {
			// A player's walk out to sea ends on the beach the path leads to (MakeGoalReachable).
			auto [gcx, gcy] = nav_grid_.world_to_cell(dest_x, dest_z);
			if (nav_grid_.cells.inside(gcx, gcy) && nav_grid_.cells.at(gcx, gcy) == NAV_WATER) {
				units_.target_x[idx] = units_.anchor_x[idx] = wp.xs[wp.count - 1];
				units_.target_z[idx] = units_.anchor_z[idx] = wp.zs[wp.count - 1];
			}
		}
		unit_paths_[idx] = wp;
		units_.waypoint_cur[idx] = static_cast<int16_t>(wp.cur);
		units_.waypoint_count[idx] = static_cast<int16_t>(wp.count);
		units_.has_order[idx] = 1;
		units_.action_state[idx] = ACTION_MOVE;
		units_.target_resource_id[idx] = 0;
		units_.target_unit_id[idx] = 0;
		units_.target_building_id[idx] = 0;
		begin_walk_order(idx);
	}
}

void Simulation::cmd_stop(const uint32_t *unit_ids, size_t count) {
	formation_stop(unit_ids, count); // a stopped formation stops walking (UnitAI Order.Stop)
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
			// UnitAI Stop drops the whole order queue.
			set_order(idx, ORDER_NONE, 0, 0, units_.pos_x[idx], units_.pos_z[idx]);
			order_queues_.erase(units_.id[idx]);
		}
	}
}

void Simulation::order_gather(const uint32_t *unit_ids_in, size_t count_in, uint32_t resource_id, bool queued) {
	auto rit = res_id_to_idx_.find(resource_id);
	if (rit == res_id_to_idx_.end()) return;
	size_t ridx = rit->second;
	float rx = resources_.x[ridx];
	float rz = resources_.z[ridx];
	const uint8_t sub_bits = (uint8_t)((resources_.subtype[ridx] << ORDER_FLAG_SUB_SHIFT) & ORDER_FLAG_SUB_MASK);
	UnitOrder o; o.kind = ORDER_GATHER; o.flags = sub_bits; o.target = resource_id; o.x = rx; o.z = rz;
	o.res_type = (uint8_t)(resources_.type[ridx] + 1);
	const std::vector<uint32_t> now = split_queued(unit_ids_in, count_in, o, queued);
	const uint32_t *unit_ids = now.data();
	const size_t count = now.size();

	for (size_t i = 0; i < count; ++i) {
		int32_t uidx = lookup_unit(unit_ids[i]);
		if (uidx < 0 || !units_.alive[uidx]) continue;
		// ResourceGatherer Rates: fishing boats only have food.fish, land gatherers none.
		if ((units_.unit_type[uidx] == UNIT_FISHING_BOAT) != (resources_.subtype[ridx] == RES_SUB_FISH)) continue;
		// The Han minister has no ResourceGatherer (units/han/support_minister).
		if (units_.unit_type[uidx] == UNIT_MINISTER) continue;
		// Citizen cavalry gather meat only (template_unit_cavalry); champion cavalry gather nothing.
		if (units_.unit_type[uidx] == UNIT_CAVALRY &&
		    (resources_.subtype[ridx] != RES_SUB_MEAT || units_.carry_capacity[uidx] <= 0)) continue;

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
		set_order(uidx, ORDER_GATHER, sub_bits, resource_id, rx, rz);
	}
}

void Simulation::order_attack(const uint32_t *unit_ids_in, size_t count_in, uint32_t target_unit_id, bool queued) {
	int32_t tidx = lookup_unit(target_unit_id);
	if (tidx < 0) return;
	float tx = units_.pos_x[tidx];
	float tz = units_.pos_z[tidx];
	UnitOrder o; o.kind = ORDER_ATTACK; o.flags = ORDER_FLAG_FORCE; o.target = target_unit_id; o.x = tx; o.z = tz;
	const std::vector<uint32_t> now = split_queued(unit_ids_in, count_in, o, queued);
	const uint32_t *unit_ids = now.data();
	const size_t count = now.size();

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
		set_order(uidx, ORDER_ATTACK, ORDER_FLAG_FORCE, target_unit_id, tx, tz);
	}
}

void Simulation::order_attack_building(const uint32_t *unit_ids_in, size_t count_in, uint32_t target_building_id,
		bool queued, bool allow_capture) {
	auto bit = building_id_to_idx_.find(target_building_id);
	if (bit == building_id_to_idx_.end()) return;
	const Building &b = buildings_[bit->second];
	const uint8_t flags = ORDER_FLAG_FORCE | (allow_capture ? ORDER_FLAG_ALLOW_CAPTURE : 0);
	UnitOrder o; o.kind = ORDER_ATTACK_BUILDING; o.flags = flags; o.target = target_building_id; o.x = b.x; o.z = b.z;
	const std::vector<uint32_t> now = split_queued(unit_ids_in, count_in, o, queued);
	const uint32_t *unit_ids = now.data();
	const size_t count = now.size();

	for (size_t i = 0; i < count; ++i) {
		int32_t uidx = lookup_unit(unit_ids[i]);
		if (uidx < 0 || !units_.alive[uidx]) continue;
		const bool captures = allow_capture && unit_capture_strength(units_.unit_type[uidx]) > 0.0f;
		if (units_.attack_damage[uidx] <= 0.0f && !captures) continue;

		stand_outside_rect(b.x, b.z, b.half_size, units_.id[uidx],
				units_.pos_x[uidx], units_.pos_z[uidx],
				&units_.target_x[uidx], &units_.target_z[uidx]);
		units_.has_order[uidx] = 1;
		units_.action_state[uidx] = ACTION_ATTACK;
		units_.target_building_id[uidx] = target_building_id;
		units_.target_unit_id[uidx] = 0;
		units_.target_resource_id[uidx] = 0;
		set_order(uidx, ORDER_ATTACK_BUILDING, flags, target_building_id, b.x, b.z);
	}
}

void Simulation::order_walk(const uint32_t *unit_ids, size_t count, float tx, float tz, bool queued, bool beach_goal) {
	UnitOrder o; o.kind = ORDER_WALK; o.flags = ORDER_FLAG_FORCE; o.x = tx; o.z = tz;
	const std::vector<uint32_t> now = split_queued(unit_ids, count, o, queued);
	beach_goal_ = beach_goal;
	cmd_move(now.data(), now.size(), tx, tz);
	beach_goal_ = false;
	for (uint32_t uid : now) {
		const int32_t idx = lookup_unit(uid);
		if (idx >= 0 && units_.alive[idx] && units_.order_kind[idx] == ORDER_WALK) units_.order_flags[idx] = ORDER_FLAG_FORCE;
	}
}

// 0 A.D. UnitAI WalkAndFight. The stance is the player's: attack-move no longer
// switches it to aggressive (nor patrol to defensive).
void Simulation::order_attack_move(const uint32_t *unit_ids, size_t count, float tx, float tz,
		bool queued, bool units_only, bool allow_capture) {
	const uint8_t flags = ORDER_FLAG_FORCE | (units_only ? ORDER_FLAG_UNITS_ONLY : 0) |
			(allow_capture ? ORDER_FLAG_ALLOW_CAPTURE : 0);
	UnitOrder o; o.kind = ORDER_WALK_AND_FIGHT; o.flags = flags; o.x = tx; o.z = tz;
	const std::vector<uint32_t> now = split_queued(unit_ids, count, o, queued);
	cmd_move(now.data(), now.size(), tx, tz);
	// Formations attack-walk as a whole (UnitAI FORMATIONCONTROLLER.WALKING with attack-move).
	for (FormationController &fc : formations_)
		if (fc.order == FORDER_WALK)
			for (uint32_t uid : now)
				if (std::find(fc.members.begin(), fc.members.end(), uid) != fc.members.end()) {
					fc.order = FORDER_ATTACK_WALK;
					break;
				}
	for (uint32_t uid : now) {
		int32_t idx = lookup_unit(uid);
		if (idx >= 0 && (size_t)idx < units_.count && units_.alive[idx]) {
			units_.action_state[idx] = ACTION_ATTACK_MOVE;
			set_order(idx, ORDER_WALK_AND_FIGHT, flags, 0, units_.target_x[idx], units_.target_z[idx]);
		}
	}
}

// 0 A.D. UnitAI Patrol: walk-and-fight between where the unit stands and (tx, tz).
void Simulation::order_patrol(const uint32_t *unit_ids, size_t count, float tx, float tz, bool queued) {
	UnitOrder o; o.kind = ORDER_PATROL; o.flags = ORDER_FLAG_FORCE; o.x = tx; o.z = tz;
	const std::vector<uint32_t> now = split_queued(unit_ids, count, o, queued);
	formation_remove_units(now.data(), now.size());
	cmd_move_individual(now.data(), now.size(), tx, tz); // patrol has no formation controller here
	for (uint32_t uid : now) {
		int32_t idx = lookup_unit(uid);
		if (idx >= 0 && (size_t)idx < units_.count && units_.alive[idx]) {
			units_.action_state[idx] = ACTION_PATROL;
			set_order(idx, ORDER_PATROL, ORDER_FLAG_FORCE, 0, units_.target_x[idx], units_.target_z[idx]);
		}
	}
}

void Simulation::order_build_or_repair(const uint32_t *unit_ids_in, size_t count_in, uint32_t building_id, bool queued) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return;
	const Building &b = buildings_[bit->second];
	UnitOrder o; o.kind = ORDER_BUILD; o.target = building_id; o.x = b.x; o.z = b.z;
	const std::vector<uint32_t> now = split_queued(unit_ids_in, count_in, o, queued);
	const uint32_t *unit_ids = now.data();
	const size_t count = now.size();

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
		set_order(uidx, ORDER_BUILD, 0, building_id, b.x, b.z);
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

void Simulation::order_pack(const uint32_t *unit_ids, size_t count, bool pack) {
	for (size_t i = 0; i < count; ++i) {
		int32_t idx = lookup_unit(unit_ids[i]);
		if (idx < 0 || !units_.alive[idx]) continue;
		const uint8_t ps = units_.pack_state[idx];
		if (ps == PACK_NONE) continue;
		// Order.Pack / Order.Unpack replace the unit's orders (UnitAI ReplaceOrder).
		cmd_stop(&unit_ids[i], 1);
		if (pack) {
			if (ps == PACK_UNPACKED) { units_.pack_state[idx] = PACK_PACKING; units_.pack_elapsed[idx] = 0.0f; }
			else if (ps == PACK_UNPACKING) { units_.pack_state[idx] = PACK_PACKED; units_.pack_elapsed[idx] = 0.0f; }
		} else {
			if (ps == PACK_PACKED) { units_.pack_state[idx] = PACK_UNPACKING; units_.pack_elapsed[idx] = 0.0f; }
			else if (ps == PACK_PACKING) { units_.pack_state[idx] = PACK_UNPACKED; units_.pack_elapsed[idx] = 0.0f; }
		}
	}
}

// Saves before v16 numbered techs without siege_pack_unpack (id 55); later ids moved up one.
static uint8_t shift_tech_before_v16(uint8_t id) {
	return id >= TECH_SIEGE_PACK_UNPACK && id + 1 < TECH_COUNT ? (uint8_t)(id + 1) : id;
}

static TechSet shift_techs_before_v16(const TechSet &old) {
	TechSet out;
	for (int id = 0; id < TECH_COUNT; ++id)
		if (old.has((uint8_t)id)) out.set(shift_tech_before_v16((uint8_t)id));
	return out;
}

float Simulation::pack_time_s(size_t unit_idx) const {
	// 0 A.D. Military Engineers (siege_pack_unpack.json): Pack/Time x0.6 for Siege.
	const uint8_t team = units_.team[unit_idx], t = units_.unit_type[unit_idx];
	if (team >= MAX_PLAYERS || t >= UNIT_TYPE_COUNT) return PACK_TIME_S;
	return PACK_TIME_S * tech_mods_[team].pack_time[t];
}

// ---------------------------------------------------------------------------
// Formations: 0 A.D. Formation.js controllers and Commands.js formation commands
// (layout and templates in formation.h / formation_data.h).
// ---------------------------------------------------------------------------

static bool formation_unit_mounted(uint8_t t) {
	return t == UNIT_CAVALRY || t == UNIT_CHARIOT || t == UNIT_CAMEL || t == UNIT_SCYTHED_CHARIOT;
}

FormationUnitInfo Simulation::formation_unit_info(size_t i) const {
	const uint16_t t = units_.tmpl[i];
	FormationUnitInfo info = t < CIV_UNIT_TEMPLATE_COUNT ? formation_unit_table()[t] : formation_generic_info(units_.unit_type[i]);
	// Promotion.js swaps in the next rank's template, and its Identity/Rank class with it.
	const uint8_t rank = units_.rank[i];
	if ((rank == 2 || rank == 3) && (info.classes & FCLASS_RANKS))
		info.classes = (uint16_t)((info.classes & ~FCLASS_RANKS) | (rank == 3 ? FCLASS_ELITE : FCLASS_ADVANCED));
	return info;
}

uint32_t Simulation::player_formation_mask(uint8_t player) const {
	return player < MAX_PLAYERS ? civ_formation_mask((uint8_t)civ_of(player)) : 0u;
}

bool Simulation::unit_idx_can_use_formation(size_t i, uint8_t f) const {
	if (f == FORMATION_NONE || f >= FORMATION_COUNT || i >= units_.count || !units_.alive[i]) return false;
	// UnitAI.js CanUseFormation (the unit's UnitAI/Formations), limited to the formations its
	// player has (Player.js GetFormations, which the 0 A.D. GUI offers).
	return ((formation_unit_info(i).formations >> f) & 1u) && ((player_formation_mask(units_.team[i]) >> f) & 1u);
}

bool Simulation::unit_can_use_formation(uint32_t unit_id, uint8_t f) const {
	const int32_t idx = lookup_unit(unit_id);
	return idx >= 0 && unit_idx_can_use_formation((size_t)idx, f);
}

bool Simulation::can_move_into_formation(const uint32_t *unit_ids, size_t count, uint8_t f) const {
	if (f == FORMATION_NONE || f >= FORMATION_COUNT) return false;
	size_t n = 0;
	for (size_t i = 0; i < count; ++i)
		if (unit_can_use_formation(unit_ids[i], f)) ++n;
	return n >= formation_template(f).required_members;
}

void Simulation::formation_refresh_groups() {
	formation_group_.assign(UNIT_CAP, 0u);
	for (const FormationController &fc : formations_)
		for (uint32_t id : fc.members) {
			const int32_t idx = lookup_unit(id);
			if (idx >= 0 && (size_t)idx < formation_group_.size()) formation_group_[idx] = fc.id;
		}
}

uint32_t Simulation::formation_of_unit(uint32_t unit_id) const {
	for (const FormationController &fc : formations_)
		if (std::find(fc.members.begin(), fc.members.end(), unit_id) != fc.members.end()) return fc.id;
	return 0;
}

float Simulation::formation_walking_distance(const FormationController &fc) const {
	// UnitAI.js ComputeWalkingDistance: along the remaining path, then to the goal.
	float d = 0.0f, px = fc.x, pz = fc.z;
	for (int k = fc.path.cur; k < fc.path.count; ++k) {
		d += std::hypot(fc.path.xs[k] - px, fc.path.zs[k] - pz);
		px = fc.path.xs[k];
		pz = fc.path.zs[k];
	}
	return d + std::hypot(fc.goal_x - px, fc.goal_z - pz);
}

void Simulation::formation_disband(size_t ci) {
	// Formation.js Disband: every member leaves; their current orders carry on.
	for (uint32_t id : formations_[ci].members) {
		const int32_t idx = lookup_unit(id);
		if (idx >= 0) units_.formation[idx] = FORMATION_NONE;
	}
	formations_.erase(formations_.begin() + ci);
	formation_refresh_groups();
}

void Simulation::formation_remove_units(const uint32_t *unit_ids, size_t count) {
	// Commands.js RemoveFromFormation -> Formation.js RemoveMembers: below
	// RequiredMemberCount the formation disbands, else the rest re-form.
	for (size_t ci = formations_.size(); ci-- > 0;) {
		FormationController &fc = formations_[ci];
		bool changed = false;
		for (size_t i = 0; i < count; ++i) {
			auto it = std::find(fc.members.begin(), fc.members.end(), unit_ids[i]);
			if (it == fc.members.end()) continue;
			fc.members.erase(it);
			const int32_t idx = lookup_unit(unit_ids[i]);
			if (idx >= 0) units_.formation[idx] = FORMATION_NONE;
			changed = true;
		}
		if (!changed) continue;
		if (fc.members.size() < formation_template(fc.tmpl).required_members) {
			formation_disband(ci);
			continue;
		}
		fc.has_offsets = 0;
		formation_move_members_into(ci, true, false);
	}
	formation_refresh_groups();
}

void Simulation::formation_stop(const uint32_t *unit_ids, size_t count) {
	for (FormationController &fc : formations_) {
		if (fc.order == FORDER_IDLE) continue;
		for (size_t i = 0; i < count; ++i)
			if (std::find(fc.members.begin(), fc.members.end(), unit_ids[i]) != fc.members.end()) {
				fc.order = FORDER_IDLE;
				fc.path.clear();
				for (FormationSlot &s : fc.slots) s.following = 0;
				break;
			}
	}
}

void Simulation::formation_move_members_into(size_t ci, bool move_center, bool force) {
	// Formation.js MoveMembersIntoFormation. With `force` every member's order is replaced;
	// without, members busy with another task (fighting, gathering) are left to it.
	FormationController &fc = formations_[ci];
	const FormationTemplate &t = formation_template(fc.tmpl);
	std::vector<FormationMember> active;
	float ax = 0.0f, az = 0.0f;
	bool mounted = false;
	for (uint32_t id : fc.members) {
		const int32_t idx = lookup_unit(id);
		if (idx < 0 || !units_.alive[idx]) continue;
		const FormationUnitInfo info = formation_unit_info((size_t)idx);
		const float w = 2.0f * unit_collision_radius(units_.unit_type[idx]); // GetAvgFootprint
		active.push_back(FormationMember{id, info.classes, units_.pos_x[idx], units_.pos_z[idx], w, w});
		ax += units_.pos_x[idx];
		az += units_.pos_z[idx];
		mounted = mounted || formation_unit_mounted(units_.unit_type[idx]);
	}
	if (active.empty()) return;
	fc.pass = mounted ? PASS_CAVALRY : PASS_INFANTRY; // ComputeMotionParameters: the widest member's class
	if (move_center) {
		ax /= active.size();
		az /= active.size();
		const bool walking = fc.order != FORDER_IDLE;
		const float gdx = fc.goal_x - ax, gdz = fc.goal_z - az;
		const float new_rot = walking && gdx * gdx + gdz * gdz > FORMATION_ROTATE_DISTANCE * FORMATION_ROTATE_DISTANCE
				? std::atan2(gdx, gdz) : fc.rot; // Vector2D.angleTo
		if (!formation_turn_allowed(t, new_rot, fc.offsets_rot)) fc.has_offsets = 0;
		fc.x = ax;
		fc.z = az;
		fc.rot = new_rot;
		if (walking) {
			find_path(fc.x, fc.z, fc.goal_x, fc.goal_z, fc.path, fc.pass);
			// Navcell-centre waypoints next to the start or the goal would turn the whole
			// formation round for a step; the controller walks straight there instead.
			WaypointPath &p = fc.path;
			int first = p.cur, last = p.count;
			while (last > first && std::hypot(p.xs[last - 1] - fc.goal_x, p.zs[last - 1] - fc.goal_z) < 3.0f) --last;
			while (first < last && std::hypot(p.xs[first] - fc.x, p.zs[first] - fc.z) < 2.0f) ++first;
			p.cur = first;
			p.count = last;
		} else {
			fc.path.clear();
		}
	}
	const bool columnar = fc.order != FORDER_IDLE && formation_walking_distance(fc) > FORMATION_COLUMN_DISTANCE;
	if (columnar != (fc.columnar != 0)) {
		fc.columnar = columnar;
		fc.has_offsets = 0;
	}
	if (!fc.has_offsets || fc.slots.size() != active.size()) {
		// Sloppiness draws from a stream seeded by the controller and tick, so it is
		// deterministic for lockstep without touching the shared sim RNG.
		FormationRng rng{(fc.id * 2654435761u) ^ (uint32_t)(ticks_ * 40503u) ^ 0x9E3779B9u};
		if (!rng.s) rng.s = 1;
		fc.slots = compute_formation_offsets(t, fc.columnar != 0, active, fc.x, fc.z, fc.rot, rng, &fc.max_rows_used);
		fc.offsets_rot = fc.rot;
		fc.has_offsets = 1;
	}
	// Members walk to their slots: each tick in tick_formations while the controller walks
	// (UnitAI FormationWalk), or straight away when forming up in place.
	for (FormationSlot &s : fc.slots) {
		const int32_t idx = lookup_unit(s.ent);
		s.following = 0;
		if (idx < 0 || !units_.alive[idx]) continue;
		const uint8_t st = units_.action_state[idx];
		if (!force && st != ACTION_IDLE && st != ACTION_MOVE && st != ACTION_ATTACK_MOVE) continue;
		s.following = 1;
		float wx, wz;
		formation_slot_world(fc.x, fc.z, fc.rot, s.x, s.y, wx, wz);
		wx = std::clamp(wx, -map_half_ + 1.0f, map_half_ - 1.0f);
		wz = std::clamp(wz, -map_half_ + 1.0f, map_half_ - 1.0f);
		units_.target_x[idx] = wx;
		units_.target_z[idx] = wz;
		units_.anchor_x[idx] = wx;
		units_.anchor_z[idx] = wz;
		units_.has_order[idx] = 1;
		units_.action_state[idx] = fc.order == FORDER_ATTACK_WALK ? ACTION_ATTACK_MOVE : ACTION_MOVE;
		units_.target_resource_id[idx] = 0;
		units_.target_unit_id[idx] = 0;
		units_.target_building_id[idx] = 0;
		if (unit_paths_.size() < UNIT_CAP) unit_paths_.resize(UNIT_CAP);
		WaypointPath &wp = unit_paths_[idx];
		if (fc.order != FORDER_IDLE) {
			wp.clear(); // tick_formations paths members that are far from their slot
			units_.waypoint_cur[idx] = units_.waypoint_count[idx] = 0;
			continue;
		}
		find_path(units_.pos_x[idx], units_.pos_z[idx], wx, wz, wp,
				formation_unit_mounted(units_.unit_type[idx]) ? PASS_CAVALRY : PASS_INFANTRY);
		wp.has_goal = true;
		wp.goal_x = wx;
		wp.goal_z = wz;
		units_.waypoint_cur[idx] = static_cast<int16_t>(wp.cur);
		units_.waypoint_count[idx] = static_cast<int16_t>(wp.count);
	}
	// Forming up in place has nothing more to steer.
	if (fc.order == FORDER_IDLE)
		for (FormationSlot &s : fc.slots) s.following = 0;
}

std::vector<size_t> Simulation::formation_controllers_for(const uint32_t *unit_ids, size_t count, uint8_t tmpl,
		bool force_template, std::vector<uint32_t> &individual) {
	// Commands.js GetFormationUnitAIs. Without force_template the units keep the formation
	// they are in (a plain walk); with it they are put into `tmpl` (the formation command).
	std::vector<size_t> result;
	std::vector<uint32_t> sel;
	sel.reserve(count);
	for (size_t i = 0; i < count; ++i) {
		const int32_t idx = lookup_unit(unit_ids[i]);
		if (idx >= 0 && units_.alive[idx] && std::find(sel.begin(), sel.end(), unit_ids[i]) == sel.end())
			sel.push_back(unit_ids[i]);
	}
	if (sel.empty()) return result;
	constexpr uint8_t UNDEFINED = 0xFF;
	uint8_t want = force_template ? tmpl : UNDEFINED;
	auto ctrl_of = [&](uint32_t id) -> int {
		for (size_t ci = 0; ci < formations_.size(); ++ci)
			if (std::find(formations_[ci].members.begin(), formations_[ci].members.end(), id) != formations_[ci].members.end())
				return (int)ci;
		return -1;
	};
	if (want != FORMATION_NONE) {
		// ExtractFormations.
		std::vector<int> ctrls;
		size_t in_formation = 0;
		for (uint32_t id : sel) {
			const int ci = ctrl_of(id);
			if (ci < 0) continue;
			++in_formation;
			if (std::find(ctrls.begin(), ctrls.end(), ci) == ctrls.end()) ctrls.push_back(ci);
		}
		if (ctrls.size() == 1) {
			FormationController &fc = formations_[ctrls[0]];
			if (fc.members.size() == in_formation && fc.members.size() == sel.size()) {
				// The whole formation was selected, so its controller takes the command.
				if (!force_template || want == fc.tmpl) {
					want = fc.tmpl;
					result.push_back((size_t)ctrls[0]);
				} else if (can_move_into_formation(sel.data(), sel.size(), want)) {
					// Formation.js LoadFormation: same controller, new template. Members that
					// cannot use it leave.
					std::vector<uint32_t> leave;
					for (uint32_t id : fc.members)
						if (!unit_can_use_formation(id, want)) leave.push_back(id);
					fc.tmpl = want;
					fc.has_offsets = 0;
					formation_refresh_groups();
					for (uint32_t id : fc.members) {
						const int32_t idx = lookup_unit(id);
						if (idx >= 0) units_.formation[idx] = want;
					}
					const size_t keep = (size_t)ctrls[0];
					const uint32_t keep_id = fc.id;
					if (!leave.empty()) formation_remove_units(leave.data(), leave.size());
					for (size_t ci = 0; ci < formations_.size(); ++ci)
						if (formations_[ci].id == keep_id) { result.push_back(ci); break; }
					(void)keep;
					for (uint32_t id : leave) individual.push_back(id);
					return result;
				}
			} else if (!force_template) {
				want = fc.tmpl;
			}
		} else if (ctrls.size() > 1 && !force_template) {
			uint8_t common = formations_[ctrls[0]].tmpl;
			for (size_t k = 1; k < ctrls.size(); ++k)
				if (formations_[ctrls[k]].tmpl != common) { common = UNDEFINED; break; }
			if (common != UNDEFINED) want = common;
		}
	}

	// Separate out the units that don't support the chosen formation.
	std::vector<uint32_t> formed;
	std::vector<uint32_t> leaving;
	for (uint32_t id : sel) {
		const int ci = ctrl_of(id);
		const uint8_t current = ci >= 0 ? formations_[ci].tmpl : FORMATION_NONE;
		const uint8_t f = want != UNDEFINED ? want : current;
		const bool null_formation = f == FORMATION_NONE;
		if (null_formation || !unit_can_use_formation(id, f)) {
			if (null_formation && ci >= 0) leaving.push_back(id); // UnitAI LeaveFormation
			individual.push_back(id);
		} else {
			formed.push_back(id);
		}
	}
	if (!leaving.empty()) formation_remove_units(leaving.data(), leaving.size());
	if (individual.size() == sel.size()) return {};
	if (!result.empty()) {
		// The reused controller may have moved in formations_ when others disbanded.
		return result;
	}

	// New controllers, one per cluster of units closer than FORMATION_CLUSTER_DISTANCE
	// (Commands.js ClusterEntities: single-link clusters, i.e. connected components).
	const size_t m = formed.size();
	std::vector<int> comp(m, -1);
	std::vector<float> fx(m), fz(m);
	for (size_t i = 0; i < m; ++i) {
		const int32_t idx = lookup_unit(formed[i]);
		fx[i] = units_.pos_x[idx];
		fz[i] = units_.pos_z[idx];
	}
	const float link2 = FORMATION_CLUSTER_DISTANCE * FORMATION_CLUSTER_DISTANCE;
	int n_comp = 0;
	for (size_t i = 0; i < m; ++i) {
		if (comp[i] >= 0) continue;
		comp[i] = n_comp;
		std::vector<size_t> stack{i};
		while (!stack.empty()) {
			const size_t a = stack.back();
			stack.pop_back();
			for (size_t b = 0; b < m; ++b) {
				if (comp[b] >= 0) continue;
				const float dx = fx[a] - fx[b], dz = fz[a] - fz[b];
				if (dx * dx + dz * dz < link2) { comp[b] = n_comp; stack.push_back(b); }
			}
		}
		++n_comp;
	}
	std::vector<uint32_t> new_ids;
	for (int c = 0; c < n_comp; ++c) {
		std::vector<uint32_t> cluster;
		for (size_t i = 0; i < m; ++i)
			if (comp[i] == c) cluster.push_back(formed[i]);
		formation_remove_units(cluster.data(), cluster.size());
		if (want == UNDEFINED || !can_move_into_formation(cluster.data(), cluster.size(), want)) {
			individual.insert(individual.end(), cluster.begin(), cluster.end());
			continue;
		}
		FormationController fc;
		fc.id = next_formation_id_++;
		const int32_t idx0 = lookup_unit(cluster[0]);
		fc.owner = units_.team[idx0];
		fc.tmpl = want;
		fc.members = cluster;
		// Formation.js MoveToMembersCenter: the members' centre and average facing.
		float vx = 0.0f, vz = 0.0f;
		for (uint32_t id : cluster) {
			const int32_t idx = lookup_unit(id);
			fc.x += units_.pos_x[idx];
			fc.z += units_.pos_z[idx];
			vx += units_.vel_x[idx];
			vz += units_.vel_z[idx];
			units_.formation[idx] = want;
		}
		fc.x /= cluster.size();
		fc.z /= cluster.size();
		fc.rot = (vx * vx + vz * vz > 1e-6f) ? std::atan2(vx, vz) : 0.0f;
		fc.offsets_rot = fc.rot;
		fc.goal_x = fc.x;
		fc.goal_z = fc.z;
		formations_.push_back(std::move(fc));
		new_ids.push_back(formations_.back().id);
	}
	for (size_t ci = 0; ci < formations_.size(); ++ci)
		if (std::find(new_ids.begin(), new_ids.end(), formations_[ci].id) != new_ids.end()) result.push_back(ci);
	formation_refresh_groups();
	return result;
}

void Simulation::order_set_formation(const uint32_t *unit_ids, size_t count, uint8_t formation) {
	// Commands.js "formation" -> GetFormationUnitAIs(forceTemplate) -> UnitAI MoveIntoFormation:
	// the units form up where they stand. The null formation (or one unit) leaves formations.
	if (count == 0 || formation >= FORMATION_COUNT) return;
	if (formation == FORMATION_NONE || count == 1) {
		formation_remove_units(unit_ids, count);
		return;
	}
	std::vector<uint32_t> individual;
	const std::vector<size_t> ctrls = formation_controllers_for(unit_ids, count, formation, true, individual);
	std::vector<uint32_t> ids;
	for (size_t ci : ctrls) ids.push_back(formations_[ci].id);
	for (uint32_t fid : ids)
		for (size_t ci = 0; ci < formations_.size(); ++ci)
			if (formations_[ci].id == fid) {
				FormationController &fc = formations_[ci];
				fc.order = FORDER_IDLE;
				fc.goal_x = fc.x;
				fc.goal_z = fc.z;
				formation_move_members_into(ci, true, true);
				break;
			}
}

void Simulation::tick_formations(float dt) {
	for (size_t ci = formations_.size(); ci-- > 0;) {
		FormationController &fc = formations_[ci];
		const FormationTemplate &t = formation_template(fc.tmpl);

		// Formation.js OnGlobalOwnershipChanged / RemoveMembers: the dead, the captured and
		// the garrisoned leave.
		bool changed = false;
		for (size_t k = fc.members.size(); k-- > 0;) {
			const int32_t idx = lookup_unit(fc.members[k]);
			if (idx >= 0 && units_.alive[idx] && units_.team[idx] == fc.owner &&
					units_.action_state[idx] != ACTION_GARRISONED)
				continue;
			if (idx >= 0) units_.formation[idx] = FORMATION_NONE;
			fc.members.erase(fc.members.begin() + k);
			changed = true;
		}
		if (fc.members.size() < t.required_members) {
			formation_disband(ci);
			continue;
		}
		if (changed) {
			formation_refresh_groups();
			fc.has_offsets = 0;
			formation_move_members_into(ci, true, false);
		}

		// Formation.js ShapeUpdate (every second): column for long walks, the real shape near the goal.
		fc.shape_timer += dt;
		if (fc.shape_timer >= FORMATION_SHAPE_UPDATE_S) {
			fc.shape_timer -= FORMATION_SHAPE_UPDATE_S;
			if (fc.order != FORDER_IDLE) {
				const bool columnar = formation_walking_distance(fc) > FORMATION_COLUMN_DISTANCE;
				if (columnar != (fc.columnar != 0)) {
					fc.columnar = columnar;
					fc.has_offsets = 0;
					formation_move_members_into(ci, false, false);
				}
			}
		}
		if (fc.order == FORDER_IDLE) continue;

		// Which members still walk with the controller. Any other task (an attack, a gather,
		// a garrison) takes a member out of the walk; an attack-walk takes back its idle fighters.
		float speed = INFINITY;
		float max_lag = 0.0f;
		bool any_following = false, any_fighting = false;
		for (FormationSlot &s : fc.slots) {
			const int32_t idx = lookup_unit(s.ent);
			if (idx < 0 || !units_.alive[idx]) { s.following = 0; continue; }
			const uint8_t st = units_.action_state[idx];
			// tick_combat leaves a unit whose foe died in ACTION_ATTACK with no target.
			const bool fighting = st == ACTION_ATTACK && (units_.target_unit_id[idx] || units_.target_building_id[idx]);
			if (fighting) any_fighting = true;
			if (s.following) {
				if (st != ACTION_IDLE && st != ACTION_MOVE && st != ACTION_ATTACK_MOVE) s.following = 0;
			} else if (fc.order == FORDER_ATTACK_WALK && !fighting &&
					((st == ACTION_IDLE && !units_.has_order[idx]) || st == ACTION_ATTACK)) {
				s.following = 1; // done fighting: back into the attack-walk
			}
			if (!s.following) continue;
			any_following = true;
			speed = std::min(speed, units_.speed[idx]);
			float wx, wz;
			formation_slot_world(fc.x, fc.z, fc.rot, s.x, s.y, wx, wz);
			max_lag = std::max(max_lag, std::hypot(units_.pos_x[idx] - wx, units_.pos_z[idx] - wz));
		}
		if (!any_following && !(fc.order == FORDER_ATTACK_WALK && any_fighting)) {
			fc.order = FORDER_IDLE;
			fc.path.clear();
			continue;
		}

		// Formation.js ComputeMotionParameters: the slowest member's walk speed times the
		// template SpeedMultiplier. RTS0 also holds the controller back while members lag
		// their slots, so the shape stays together. An attack-walk halts while members fight.
		float step = 0.0f;
		if (any_following && !(fc.order == FORDER_ATTACK_WALK && any_fighting)) {
			const float slack = 2.0f;
			const float keep_up = max_lag <= slack ? 1.0f : std::max(0.0f, 1.0f - (max_lag - slack) / (2.0f * slack));
			step = speed * t.speed_multiplier * keep_up * dt;
		}
		bool arrived = false;
		while (step > 0.0f) {
			const bool on_path = fc.path.cur < fc.path.count;
			const float ax = on_path ? fc.path.xs[fc.path.cur] : fc.goal_x;
			const float az = on_path ? fc.path.zs[fc.path.cur] : fc.goal_z;
			const float dx = ax - fc.x, dz = az - fc.z;
			const float d = std::sqrt(dx * dx + dz * dz);
			if (d > 0.5f) fc.rot = std::atan2(dx, dz); // the controller faces its walk
			if (d > step) {
				fc.x += dx / d * step;
				fc.z += dz / d * step;
				break;
			}
			fc.x = ax;
			fc.z = az;
			step -= d;
			if (on_path) ++fc.path.cur;
			else { arrived = true; break; }
		}
		{
			const float gdx = fc.goal_x - fc.x, gdz = fc.goal_z - fc.z;
			if (fc.path.cur >= fc.path.count && gdx * gdx + gdz * gdz <= ARRIVE_DIST * ARRIVE_DIST) arrived = true;
		}

		// Send each walking member to its slot (UnitAI FormationWalk).
		const uint8_t walk_state = fc.order == FORDER_ATTACK_WALK ? ACTION_ATTACK_MOVE : ACTION_MOVE;
		for (FormationSlot &s : fc.slots) {
			if (!s.following) continue;
			const int32_t idx = lookup_unit(s.ent);
			if (idx < 0) continue;
			float wx, wz;
			formation_slot_world(fc.x, fc.z, fc.rot, s.x, s.y, wx, wz);
			wx = std::clamp(wx, -map_half_ + 1.0f, map_half_ - 1.0f);
			wz = std::clamp(wz, -map_half_ + 1.0f, map_half_ - 1.0f);
			units_.target_x[idx] = wx;
			units_.target_z[idx] = wz;
			units_.anchor_x[idx] = wx;
			units_.anchor_z[idx] = wz;
			units_.has_order[idx] = 1;
			units_.action_state[idx] = walk_state;
			units_.target_unit_id[idx] = 0;
			units_.target_building_id[idx] = 0;
			units_.target_resource_id[idx] = 0;
			// Near its slot a member heads straight for it; farther off (or once the controller
			// has arrived) it paths there, refreshed when the slot has moved on.
			if (unit_paths_.size() < UNIT_CAP) unit_paths_.resize(UNIT_CAP);
			WaypointPath &wp = unit_paths_[idx];
			const float mdx = wx - units_.pos_x[idx], mdz = wz - units_.pos_z[idx];
			const bool far = mdx * mdx + mdz * mdz > 6.0f * 6.0f;
			if (!far && !arrived) {
				wp.clear();
				units_.waypoint_cur[idx] = units_.waypoint_count[idx] = 0;
				continue;
			}
			const float gdx = wp.goal_x - wx, gdz = wp.goal_z - wz;
			const bool stale = !wp.has_goal || wp.count == 0 || gdx * gdx + gdz * gdz > 4.0f * 4.0f;
			if (arrived || (stale && (ticks_ + s.ent) % 10 == 0)) {
				find_path(units_.pos_x[idx], units_.pos_z[idx], wx, wz, wp,
						formation_unit_mounted(units_.unit_type[idx]) ? PASS_CAVALRY : PASS_INFANTRY);
				wp.has_goal = true;
				wp.goal_x = wx;
				wp.goal_z = wz;
				units_.waypoint_cur[idx] = static_cast<int16_t>(wp.cur);
				units_.waypoint_count[idx] = static_cast<int16_t>(wp.count);
			}
		}
		if (arrived) {
			// The controller is at the goal: members finish walking to their last slot on
			// their own, and the formation goes idle (FORMATIONCONTROLLER.WALKING MovementUpdate).
			fc.order = FORDER_IDLE;
			fc.path.clear();
			for (FormationSlot &s : fc.slots) s.following = 0;
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
	if (unit_is_ship(utype)) return ship_garrison_capacity((size_t)idx);
	return siege_garrison_capacity((size_t)idx);
}

// 0 A.D. GarrisonHolder Max on ships: template_unit_ship.xml 10, merchant 15,
// fishing boat 1, warship_arrow / warship_ram 30, warship_siege 50,
// warship_scout 10; fire ships have none (GarrisonHolder disable).
int Simulation::ship_garrison_capacity(size_t i) const {
	return ship_template_capacity(units_.unit_type[i], units_.tmpl[i]);
}

int Simulation::ship_template_capacity(uint8_t unit_type, uint16_t tmpl) {
	switch (unit_type) {
		case UNIT_FISHING_BOAT: return 1;
		case UNIT_MERCHANT_SHIP: return 15;
		case UNIT_TRIREME: break;
		default: return 0;
	}
	if (tmpl == CIV_UNIT_TEMPLATE_NONE || tmpl >= CIV_UNIT_TEMPLATE_COUNT) return TRANSPORT_CAPACITY;
	const char *id = civ_unit_template(tmpl).id;
	const char *slash = std::strrchr(id, '/');
	const char *name = slash ? slash + 1 : id;
	if (std::strcmp(name, "ship_fire") == 0) return 0;
	if (std::strcmp(name, "ship_scout") == 0) return 10;
	if (std::strcmp(name, "ship_siege") == 0) return 50;
	return TRANSPORT_CAPACITY; // ship_arrow, ship_ram, ship_bireme...
}

// GarrisonHolder List: every ship takes FemaleCitizen Infantry Healer Dog
// (template_unit_ship.xml); merchant ships add Support and Cavalry, warships
// add Support and Soldier, and arrow, ram and siege ships add Siege.
bool Simulation::ship_garrison_allowed(size_t ship, uint8_t unit_type) const {
	const uint64_t c = unit_tech_classes(unit_type);
	uint64_t allowed = TC_FEMALECITIZEN | TC_INFANTRY | TC_HEALER | TC_DOG;
	if (units_.unit_type[ship] == UNIT_MERCHANT_SHIP) allowed |= TC_CIVILIAN | TC_CAVALRY;
	if (units_.unit_type[ship] == UNIT_TRIREME) {
		allowed |= TC_CIVILIAN | TC_SOLDIER | TC_HERO;
		if (ship_garrison_capacity(ship) != 10) allowed |= TC_SIEGE; // not the scout ship
	}
	return (c & allowed) != 0;
}

// GarrisonHolder LoadingRange (10 m on ships), measured between the ship's and
// the unit's edges as 0 A.D. does.
float Simulation::ship_loading_reach(size_t ship, size_t unit) const {
	return SHIP_LOADING_RANGE + unit_collision_radius(units_.unit_type[ship]) + unit_collision_radius(units_.unit_type[unit]);
}

// The water cell of the ship's own sea nearest to (x, z), searched out to 16
// cells: where a ship stops to meet units on the shore.
bool Simulation::nearest_water_for_ship(size_t ship, float x, float z, float &out_x, float &out_z) const {
	if (nav_grid_.cells.blank()) return false;
	auto [sx, sy] = nav_grid_.world_to_cell(units_.pos_x[ship], units_.pos_z[ship]);
	if (!nav_grid_.cells.inside(sx, sy)) return false;
	const uint16_t sea = nav_grid_.global_region_at(sx, sy, PASS_SHIP);
	auto [cx, cy] = nav_grid_.world_to_cell(x, z);
	const float cell = 2.0f * nav_grid_.map_half / (float)nav_grid_.res;
	constexpr int R = 16;
	int best_d2 = INT32_MAX, bx = 0, by = 0;
	for (int dy = -R; dy <= R; ++dy)
		for (int dx = -R; dx <= R; ++dx) {
			const int gx = cx + dx, gy = cy + dy, d2 = dx * dx + dy * dy;
			if (d2 >= best_d2 || !nav_grid_.cells.inside(gx, gy) || !nav_grid_.is_passable_for(gx, gy, PASS_SHIP)) continue;
			if (sea && nav_grid_.global_region_at(gx, gy, PASS_SHIP) != sea) continue;
			best_d2 = d2; bx = gx; by = gy;
		}
	if (best_d2 == INT32_MAX) return false;
	out_x = ((float)bx + 0.5f) * cell - nav_grid_.map_half;
	out_z = ((float)by + 0.5f) * cell - nav_grid_.map_half;
	return true;
}

// PickSpawnPoint for a ship: the nearest dry ground within loading reach of its
// hull, scanned in a fixed order so every peer picks the same spot.
bool Simulation::ship_unload_point(size_t ship, float &out_x, float &out_z) const {
	const float sx = units_.pos_x[ship], sz = units_.pos_z[ship];
	const float reach = SHIP_LOADING_RANGE + unit_collision_radius(units_.unit_type[ship]) + 0.5f;
	for (float r = 0.5f; r <= reach; r += 0.5f) {
		for (int a = 0; a < 16; ++a) {
			const float ang = (float)a * 0.39269908f;
			const float cx = sx + std::cos(ang) * r, cz = sz + std::sin(ang) * r;
			if (std::fabs(cx) >= map_half_ - 1.0f || std::fabs(cz) >= map_half_ - 1.0f) continue;
			if (get_terrain_height(cx, cz) < terrain_water_height_ + 0.15f) continue;
			auto [nx, ny] = nav_grid_.world_to_cell(cx, cz);
			if (!nav_grid_.cells.blank() && nav_grid_.cells.inside(nx, ny) && nav_grid_.cells.at(nx, ny) != NAV_PASSABLE &&
					nav_grid_.cells.at(nx, ny) != NAV_SHALLOW) continue;
			out_x = cx;
			out_z = cz;
			return true;
		}
	}
	return false;
}

bool Simulation::is_transport(uint32_t unit_id) const {
	int32_t idx = lookup_unit(unit_id);
	if (idx < 0 || !units_.alive[idx]) return false;
	uint8_t utype = units_.unit_type[idx];
	if (unit_is_ship(utype)) return ship_garrison_capacity((size_t)idx) > 0;
	return siege_garrison_capacity((size_t)idx) > 0;
}

// Ships float: 0 A.D. entities sit on the water surface (Position FloatOnWater), so range
// and launch heights for a ship are the water level, not the seabed under it.
float Simulation::unit_height(size_t i) const {
	const float ground = get_terrain_height(units_.pos_x[i], units_.pos_z[i]);
	if (!unit_is_ship(units_.unit_type[i]) || terrain_water_height_ < -0.5f) return ground + unit_height_offset(i);
	return std::max(ground, terrain_water_height_);
}

bool Simulation::is_siege_tower_at(size_t i) const {
	const uint16_t tmpl = units_.tmpl[i];
	return tmpl != CIV_UNIT_TEMPLATE_NONE && tmpl < CIV_UNIT_TEMPLATE_COUNT
		&& (civ_unit_template(tmpl).flags & ROSTER_SIEGE_TOWER) != 0;
}

int Simulation::siege_garrison_capacity(size_t i) const {
	if (is_siege_tower_at(i)) return SIEGE_TOWER_GARRISON;
	if (units_.unit_type[i] == UNIT_SIEGE_RAM) return SIEGE_RAM_GARRISON;
	return 0;
}

int Simulation::siege_tower_arrows(size_t i) const {
	auto pit = transport_passengers_.find(units_.id[i]);
	if (pit == transport_passengers_.end()) return 0;
	int infantry = 0;
	for (uint32_t puid : pit->second) {
		int32_t p = lookup_unit(puid);
		if (p >= 0 && (unit_tech_classes(units_.unit_type[p]) & TC_INFANTRY)) ++infantry;
	}
	return std::min(infantry, SIEGE_TOWER_MAX_ARROWS);
}

// Who may board: same owner, not a transport itself; land siege only takes
// 0 A.D.'s Support and Infantry classes.
bool Simulation::can_board(size_t transport_idx, size_t unit_idx) const {
	if (units_.team[unit_idx] != units_.team[transport_idx]) return false;
	if (!unit_garrisonable(unit_idx)) return false; // an ungarrisonable Regicide hero boards nothing
	// Ships carry rams and siege towers too (Siege on warship Lists), never other ships.
	if (unit_is_ship(units_.unit_type[transport_idx]))
		return !unit_is_ship(units_.unit_type[unit_idx]) && ship_garrison_allowed(transport_idx, units_.unit_type[unit_idx]);
	if (is_transport(units_.id[unit_idx])) return false;
	if (siege_garrison_capacity(transport_idx) > 0 && !siege_garrison_allowed(units_.unit_type[unit_idx])) return false;
	return true;
}

void Simulation::tick_siege_garrisons() {
	if (transport_passengers_.empty()) return;
	// Index order, not map order, so every peer unloads identically.
	for (size_t i = 0; i < units_.count; ++i) {
		if (unit_is_ship(units_.unit_type[i])) {
			if (!units_.alive[i]) unload_sunk_ship(i);
			continue;
		}
		if (siege_garrison_capacity(i) == 0) continue;
		if (units_.action_state[i] == ACTION_GARRISONED) continue;
		const bool dead = !units_.alive[i];
		if (!dead && units_.hp[i] >= units_.max_hp[i] * SIEGE_EJECT_HEALTH) continue;
		auto pit = transport_passengers_.find(units_.id[i]);
		if (pit == transport_passengers_.end() || pit->second.empty()) continue;
		const float cx = units_.pos_x[i], cz = units_.pos_z[i];
		const float step = 6.2831853f / (float)pit->second.size();
		int k = 0;
		for (uint32_t puid : pit->second) {
			int32_t p = lookup_unit(puid);
			if (p < 0) continue;
			const float a = step * (float)k++;
			units_.pos_x[p] = std::clamp(cx + cosf(a) * 2.0f, -map_half_ + 1.0f, map_half_ - 1.0f);
			units_.pos_z[p] = std::clamp(cz + sinf(a) * 2.0f, -map_half_ + 1.0f, map_half_ - 1.0f);
			units_.alive[p] = 1;
			units_.action_state[p] = ACTION_IDLE;
			units_.has_order[p] = 0;
			units_.vel_x[p] = units_.vel_z[p] = 0.0f;
		}
		transport_passengers_.erase(pit);
	}
}

// A ship's GarrisonHolder OnDestroy (EjectHealth 0): EjectClassesOnDestroy
// (FemaleCitizen Infantry Healer Dog) swim to the nearest dry ground within its
// loading reach; everyone else, and everyone out at sea, goes down with it.
void Simulation::unload_sunk_ship(size_t ship) {
	auto pit = transport_passengers_.find(units_.id[ship]);
	if (pit == transport_passengers_.end()) return;
	float lx = 0.0f, lz = 0.0f;
	const bool shore = ship_unload_point(ship, lx, lz);
	const uint64_t ejected = TC_FEMALECITIZEN | TC_INFANTRY | TC_HEALER | TC_DOG;
	const float step = 6.2831853f / (float)std::max<size_t>(1, pit->second.size());
	int k = 0;
	for (uint32_t puid : pit->second) {
		const int32_t p = lookup_unit(puid);
		if (p < 0) continue;
		if (shore && (unit_tech_classes(units_.unit_type[p]) & ejected)) {
			const float a = step * (float)k++;
			units_.pos_x[p] = lx + std::cos(a) * 0.6f;
			units_.pos_z[p] = lz + std::sin(a) * 0.6f;
			units_.alive[p] = 1;
			units_.action_state[p] = ACTION_IDLE;
			units_.has_order[p] = 0;
			units_.vel_x[p] = units_.vel_z[p] = 0.0f;
		} else {
			units_.hp[p] = 0.0f;
			units_.alive[p] = 0;
			units_.action_state[p] = ACTION_DYING;
			stat_unit_lost((size_t)p, GAIA_PLAYER); // drowned: lost, but nobody's kill
			if (units_.team[p] == 0) record_.units_lost++;
		}
	}
	transport_passengers_.erase(pit);
}

void Simulation::order_garrison(const uint32_t *unit_ids_in, size_t count_in, uint32_t building_id, bool queued) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return;
	Building &b = buildings_[bit->second];
	if (b.build_progress < 1.0f || !structure_has_enough_health(b)) return;
	UnitOrder o; o.kind = ORDER_GARRISON; o.target = building_id; o.x = b.x; o.z = b.z;
	const std::vector<uint32_t> now = split_queued(unit_ids_in, count_in, o, queued);
	const uint32_t *unit_ids = now.data();
	const size_t count = now.size();

	// UnitAI GARRISON.APPROACHING: walk up to the structure (a forced order, so enemies on
	// the way are ignored) and get in once within its LoadingRange (tick_unit_ai).
	for (size_t i = 0; i < count; ++i) {
		int32_t uidx = lookup_unit(unit_ids[i]);
		if (uidx < 0 || !units_.alive[uidx] || units_.team[uidx] != b.team) continue;
		if (!unit_garrisonable((size_t)uidx)) continue;
		if (turret_points_of(b) && !unit_turretable((size_t)uidx)) continue; // TurretHolder.CanOccupy
		if (garrison_in_range((size_t)uidx, b)) {
			if ((int)b.garrisoned_units.size() >= b.garrison_cap) continue;
			garrison_now((size_t)uidx, b);
			continue;
		}
		const bool was = applying_queued_;
		applying_queued_ = true; // keep the unit's queue: this is the garrison order, not a walk
		cmd_move(&unit_ids[i], 1, b.x, b.z);
		applying_queued_ = was;
		set_order((size_t)uidx, ORDER_GARRISON, ORDER_FLAG_FORCE, building_id, b.x, b.z);
	}
}

bool Simulation::garrison_in_range(size_t i, const Building &b) const {
	const float ex = std::max(0.0f, std::fabs(units_.pos_x[i] - b.x) - b.half_size);
	const float ez = std::max(0.0f, std::fabs(units_.pos_z[i] - b.z) - b.half_size);
	// LoadingRange counts from where a unit can stand: its radius plus the navcell
	// clearance extension around the structure (0 A.D. measures from the obstruction).
	const float reach = GARRISON_LOADING_RANGE + unit_collision_radius(units_.unit_type[i]) +
			nav::kClearanceExtensionRadius;
	return ex * ex + ez * ez <= reach * reach;
}

void Simulation::garrison_now(size_t i, Building &b) {
	if (turret_points_of(b)) { // walls, gates and outposts hold turrets, not a garrison
		if (unit_turretable(i)) occupy_turret(i, b);
		return;
	}
	units_.action_state[i] = ACTION_GARRISONED;
	units_.alive[i] = 0;
	units_.has_order[i] = 0;
	units_.vel_x[i] = 0.0f;
	units_.vel_z[i] = 0.0f;
	units_.waypoint_count[i] = 0;
	set_order(i, ORDER_NONE, 0, 0, b.x, b.z); // inside: the order is done
	b.garrisoned_units.push_back(units_.id[i]);
}

// ---------------------------------------------------------------------------
// 0 A.D. TurretHolder.js / Turretable.js. A wall segment, gate or outpost holds its civ's
// turret points (turret_data.h) instead of a garrison. Ranged infantry (Turretable on
// template_unit_infantry_ranged and the ranged infantry champions and heroes) ordered to
// garrison there stand on a free point: they stay alive and visible, shoot with their own
// attack from the point's height, hold their ground (SetTurretStance), and step down beside
// the structure when given any order other than an attack (UnitAI LeaveTurret). Turret
// units are kept in Building::garrisoned_units, alive, so nothing new is saved.
// ---------------------------------------------------------------------------

bool Simulation::unit_turretable(size_t i) const {
	const uint64_t c = unit_classes(units_.tmpl[i], units_.unit_type[i]);
	return (c & TC_INFANTRY) && (c & TC_RANGED);
}

const TurretPointSet *Simulation::turret_points_of(const Building &b) const {
	const int kind = b.type == BLD_WALL_SEGMENT ? 0 : b.type == BLD_GATE ? 1 : b.type == BLD_OUTPOST ? 2 : -1;
	if (kind < 0) return nullptr;
	const uint8_t civ = b.team < MAX_PLAYERS ? (uint8_t)civ_of(b.team) : (uint8_t)CIV_ATHENIANS;
	const TurretPointSet &set = TURRET_POINTS[civ < CIV_COUNT ? civ : 0][kind];
	return set.count ? &set : nullptr;
}

int Simulation::structure_garrison_cap(const Building &b) const {
	const TurretPointSet *pts = turret_points_of(b);
	return pts ? pts->count : building_type_info(b.type).garrison_cap;
}

uint32_t Simulation::turret_holder_of(uint32_t unit_id) const {
	if (!turret_height_.count(unit_id)) return 0;
	for (const Building &b : buildings_)
		if (turret_points_of(b) &&
				std::find(b.garrisoned_units.begin(), b.garrisoned_units.end(), unit_id) != b.garrisoned_units.end())
			return b.id;
	return 0;
}

// The point a turret unit stands on: the free one nearest its position.
static int nearest_turret_point(const TurretPointSet &pts, float bx, float bz, float x, float z,
		const bool *taken = nullptr) {
	int best = 0;
	float best_d2 = 1e30f;
	for (int k = 0; k < pts.count; ++k) {
		if (taken && taken[k]) continue;
		const float dx = bx + pts.pt[k].x - x, dz = bz + pts.pt[k].z - z;
		if (dx * dx + dz * dz < best_d2) { best_d2 = dx * dx + dz * dz; best = k; }
	}
	return best;
}

void Simulation::occupy_turret(size_t i, Building &b) {
	const TurretPointSet *pts = turret_points_of(b);
	if (!pts || (int)b.garrisoned_units.size() >= pts->count) return;
	bool taken[TURRET_POINTS_MAX] = {};
	for (uint32_t uid : b.garrisoned_units) {
		const int32_t u = lookup_unit(uid);
		if (u >= 0) taken[nearest_turret_point(*pts, b.x, b.z, units_.pos_x[u], units_.pos_z[u])] = true;
	}
	int k = 0;
	while (k < pts->count && taken[k]) ++k;
	if (k == pts->count) return;
	const float x = b.x + pts->pt[k].x, z = b.z + pts->pt[k].z;
	set_order(i, ORDER_NONE, 0, 0, x, z); // up there: the garrison order is done
	units_.pos_x[i] = units_.target_x[i] = units_.anchor_x[i] = x;
	units_.pos_z[i] = units_.target_z[i] = units_.anchor_z[i] = z;
	units_.vel_x[i] = units_.vel_z[i] = 0.0f;
	units_.has_order[i] = 0;
	units_.waypoint_count[i] = 0;
	units_.action_state[i] = ACTION_IDLE;
	// SetTurretStance: a turret holds its ground, keeping the stance it had for later.
	turret_prev_stance_[units_.id[i]] = units_.stance[i];
	if (units_.stance[i] != STANCE_STAND_GROUND && units_.stance[i] != STANCE_PASSIVE)
		units_.stance[i] = STANCE_STAND_GROUND;
	b.garrisoned_units.push_back(units_.id[i]);
	turret_height_[units_.id[i]] = pts->pt[k].y;
}

// Turretable.LeaveTurret: down beside the structure, on the side of the unit's point,
// with its stance back (ResetTurretStance). Its new order, if any, stands.
void Simulation::leave_turret(size_t i, const Building &b) {
	float dx = units_.pos_x[i] - b.x, dz = units_.pos_z[i] - b.z;
	const float len = std::sqrt(dx * dx + dz * dz);
	if (len < 1e-3f) { dx = 0.0f; dz = 1.0f; } else { dx /= len; dz /= len; }
	const float r = b.half_size + 1.2f;
	units_.pos_x[i] = std::clamp(b.x + dx * r, -map_half_ + 1.0f, map_half_ - 1.0f);
	units_.pos_z[i] = std::clamp(b.z + dz * r, -map_half_ + 1.0f, map_half_ - 1.0f);
	auto it = turret_prev_stance_.find(units_.id[i]);
	units_.stance[i] = it != turret_prev_stance_.end() ? it->second : unit_default_stance(units_.unit_type[i]);
	if (it != turret_prev_stance_.end()) turret_prev_stance_.erase(it);
	turret_height_.erase(units_.id[i]);
}

void Simulation::tick_turrets() {
	turret_height_.clear();
	for (Building &b : buildings_) {
		if (b.garrisoned_units.empty()) continue;
		const TurretPointSet *pts = turret_points_of(b);
		if (!pts) continue;
		auto &list = b.garrisoned_units;
		bool taken[TURRET_POINTS_MAX] = {};
		for (size_t k = 0; k < list.size();) {
			const int32_t u = lookup_unit(list[k]);
			if (u < 0 || (!units_.alive[u] && units_.action_state[u] != ACTION_GARRISONED)) { // died up there
				turret_prev_stance_.erase(list[k]);
				list.erase(list.begin() + (std::ptrdiff_t)k);
				continue;
			}
			if (!units_.alive[u]) { ++k; continue; } // garrisoned before turrets: left as it was
			// Any order but an attack takes the unit down (UnitAI orders call LeaveTurret);
			// so does an owner that is no longer a mutual ally, or a point that is gone.
			const uint8_t ok = units_.order_kind[u];
			const bool stays = (ok == ORDER_NONE || ok == ORDER_ATTACK || ok == ORDER_ATTACK_BUILDING) &&
					(units_.team[u] == b.team || is_ally(units_.team[u], b.team)) && (int)k < pts->count;
			if (!stays) {
				leave_turret((size_t)u, b);
				list.erase(list.begin() + (std::ptrdiff_t)k);
				continue;
			}
			const int pi = nearest_turret_point(*pts, b.x, b.z, units_.pos_x[u], units_.pos_z[u], taken);
			taken[pi] = true;
			const TurretPoint &p = pts->pt[pi];
			units_.pos_x[u] = units_.target_x[u] = b.x + p.x;
			units_.pos_z[u] = units_.target_z[u] = b.z + p.z;
			units_.vel_x[u] = units_.vel_z[u] = 0.0f;
			units_.has_order[u] = 0; // SetImmobile: shoots what is in range, never walks
			turret_height_[list[k]] = p.y;
			++k;
		}
	}
}

bool Simulation::garrison_unit_now(uint32_t unit_id, uint32_t building_id) {
	const int32_t i = lookup_unit(unit_id);
	auto it = building_id_to_idx_.find(building_id);
	if (i < 0 || !units_.alive[i] || it == building_id_to_idx_.end()) return false;
	Building &b = buildings_[it->second];
	if (b.team != units_.team[i] || (int)b.garrisoned_units.size() >= b.garrison_cap) return false;
	garrison_now((size_t)i, b);
	return true;
}

uint32_t Simulation::spawn_gaia_unit(float x, float z, UnitType u_type, const char *template_id, int rank) {
	uint16_t tmpl = CIV_UNIT_TEMPLATE_NONE;
	if (template_id && *template_id) {
		std::string id(template_id), base = id;
		if (base.size() > 2 && (base.ends_with("_a") || base.ends_with("_e"))) base.back() = 'b';
		for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT && tmpl == CIV_UNIT_TEMPLATE_NONE; ++t)
			if (id == civ_unit_template(t).id) tmpl = t;
		for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT && tmpl == CIV_UNIT_TEMPLATE_NONE; ++t)
			if (base == civ_unit_template(t).id) tmpl = t;
	}
	const uint32_t id = spawn_unit(x, z, GAIA_PLAYER, u_type, 0.0f, tmpl);
	const int32_t i = lookup_unit(id);
	// The map's _a/_e template: promote now through the unit's own chain.
	if (i >= 0)
		for (uint8_t r = units_.rank[i]; r < std::min(rank, 3); ++r) {
			const PromotionLink *l = promotion_link(units_.tmpl[i], units_.unit_type[i], units_.rank[i]);
			if (!l || l->target != CIV_UNIT_TEMPLATE_NONE) break;
			promote_unit((size_t)i, CIV_UNIT_TEMPLATE_NONE, l->target_rank);
		}
	return id;
}

bool Simulation::structure_has_enough_health(const Building &b) const {
	return b.hp > std::floor(building_eject_health(b.type) * b.max_hp);
}

float Simulation::structure_garrison_heal(const Building &b) const {
	const float add = b.team < MAX_PLAYERS ? tech_mods_[b.team].bld_garrison_heal[b.type] : 0.0f;
	return building_garrison_heal(b.type) + add;
}

void Simulation::tick_garrison_holders() {
	// HealTimeout runs every HEAL_TIMEOUT = 1000 ms while something is garrisoned.
	const bool heal_now = ticks_ % (uint64_t)std::lround(1.0f / FIXED_STEP) == 0;
	for (size_t bi = 0; bi < buildings_.size(); ++bi) {
		Building &b = buildings_[bi];
		if (b.garrisoned_units.empty()) continue;
		if (!structure_has_enough_health(b)) { // OnHealthChanged -> EjectOrKill
			order_ungarrison(b.id);
			continue;
		}
		if (!heal_now) continue;
		const float rate = structure_garrison_heal(b);
		if (rate <= 0.0f) continue;
		for (uint32_t uid : b.garrisoned_units) {
			const int32_t i = lookup_unit(uid);
			if (i < 0 || !(unit_tech_classes(units_.unit_type[i]) & TC_ORGANIC)) continue; // Health.IsUnhealable
			units_.hp[i] = std::min(units_.max_hp[i], units_.hp[i] + rate);
		}
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
			const bool turret = turret_height_.count(uid) != 0;
			if (!turret) {
				units_.pos_x[idx] = ex;
				units_.pos_z[idx] = ez;
			}
			units_.alive[idx] = 1;
			units_.action_state[idx] = ACTION_IDLE;
			units_.has_order[idx] = 0;
			units_.vel_x[idx] = 0.0f;
			units_.vel_z[idx] = 0.0f;
			if (turret) leave_turret((size_t)idx, b);
		}
	}
	const std::vector<uint32_t> out = std::move(b.garrisoned_units);
	b.garrisoned_units.clear();
	for (uint32_t uid : out) rally_after_ungarrison(uid, b.id);
}

// Garrisonable.UnGarrison: UnitAI.Ungarrison replaces whatever was queued behind the
// garrison order, then RallyPoint.OrderToRallyPoint(ent, ["garrison"]) has the unit
// obey its owner's rally point, unless that only says to garrison back in here.
void Simulation::rally_after_ungarrison(uint32_t uid, uint32_t building_id) {
	order_queues_.erase(uid);
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return;
	const Building &b = buildings_[bit->second];
	const int32_t i = lookup_unit(uid);
	if (i < 0 || !b.has_rally_point || units_.team[i] != b.team) return;
	apply_rally_point(uid, b, true);
}

void Simulation::order_ungarrison_single(uint32_t building_id, uint32_t unit_id) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return;
	Building &b = buildings_[bit->second];

	auto it = std::find(b.garrisoned_units.begin(), b.garrisoned_units.end(), unit_id);
	if (it != b.garrisoned_units.end()) {
		int32_t idx = lookup_unit(unit_id);
		if (idx >= 0) {
			const bool turret = turret_height_.count(unit_id) != 0;
			if (!turret) {
				units_.pos_x[idx] = b.x;
				units_.pos_z[idx] = b.z + b.half_size + 1.2f;
			}
			units_.alive[idx] = 1;
			units_.action_state[idx] = ACTION_IDLE;
			units_.has_order[idx] = 0;
			units_.vel_x[idx] = 0.0f;
			units_.vel_z[idx] = 0.0f;
			if (turret) leave_turret((size_t)idx, b);
		}
		b.garrisoned_units.erase(it);
		rally_after_ungarrison(unit_id, building_id);
	}
}

void Simulation::cmd_unload(uint8_t player, uint32_t building_id, const uint32_t *unit_ids, size_t count) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return;
	const Building &b = buildings_[bit->second];
	const bool owner = b.team == player;
	if (!owner && !is_ally(player, b.team)) return;
	if (owner && count == 0) {
		order_ungarrison(building_id);
		return;
	}
	std::vector<uint32_t> out;
	if (count == 0) {
		out = b.garrisoned_units;
	} else {
		out.assign(unit_ids, unit_ids + count);
	}
	for (uint32_t uid : out) {
		const int32_t i = lookup_unit(uid);
		if (i < 0 || (!owner && units_.team[i] != player)) continue;
		order_ungarrison_single(building_id, uid);
	}
}

// Range-manager distance from a structure's obstruction to a point.
static float edge_distance(const Simulation::Building &b, float x, float z) {
	const float ex = std::max(0.0f, std::fabs(x - b.x) - b.half_size);
	const float ez = std::max(0.0f, std::fabs(z - b.z) - b.half_size);
	return std::sqrt(ex * ex + ez * ez);
}

static bool alert_filter(uint8_t unit_type, bool traders) {
	const uint64_t c = unit_tech_classes(unit_type);
	return traders ? (c & TC_TRADER) && !(c & TC_SHIP) : (c & TC_FEMALECITIZEN) != 0;
}

bool Simulation::cmd_raise_alert(uint8_t player, uint32_t building_id) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return false;
	const Building raiser = buildings_[bit->second];
	AlertRaiserInfo info;
	if (raiser.team != player || raiser.wrecked || raiser.build_progress < 1.0f ||
			!building_alert_raiser(raiser.type, info))
		return false;
	// Free slots per holder, so units don't all head for one that will be full.
	std::unordered_map<uint32_t, int> reserved;
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != player || !alert_filter(units_.unit_type[i], info.traders)) continue;
		const float ux = units_.pos_x[i], uz = units_.pos_z[i];
		if (info.raise_range >= 0.0f && edge_distance(raiser, ux, uz) > info.raise_range) continue;
		// The nearest structure of ours that can take it (queries sort by distance).
		int32_t best = -1;
		float best_d = info.search_range;
		for (size_t h = 0; h < buildings_.size(); ++h) {
			const Building &b = buildings_[h];
			if (b.team != player || b.wrecked || b.build_progress < 1.0f || b.garrison_cap <= 0 ||
					turret_points_of(b) || !structure_has_enough_health(b)) // turrets aren't a GarrisonHolder
				continue;
			const float d = edge_distance(b, ux, uz);
			if (d > best_d || (d == best_d && best >= 0)) continue;
			if (info.end_range > 0.0f && std::hypot(b.x - raiser.x, b.z - raiser.z) > info.end_range) continue;
			auto r = reserved.find(b.id);
			const int room = r != reserved.end() ? r->second : b.garrison_cap - (int)b.garrisoned_units.size();
			if (room < 1) continue;
			best = (int32_t)h;
			best_d = d;
		}
		uint32_t uid = units_.id[i];
		if (best >= 0) {
			const Building &h = buildings_[best];
			auto r = reserved.find(h.id);
			if (r == reserved.end()) r = reserved.emplace(h.id, h.garrison_cap - (int)h.garrisoned_units.size()).first;
			--r->second;
			order_garrison(&uid, 1, h.id);
		} else {
			cmd_stop(&uid, 1); // no room: stop moving
		}
	}
	return true;
}

bool Simulation::cmd_end_alert(uint8_t player, uint32_t building_id) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return false;
	const Building raiser = buildings_[bit->second];
	AlertRaiserInfo info;
	if (raiser.team != player || raiser.wrecked || !building_alert_raiser(raiser.type, info)) return false;
	auto in_range = [&](float x, float z) { return info.end_range < 0.0f || edge_distance(raiser, x, z) <= info.end_range; };
	// Units outside: back to work if idle or still heading for shelter, else they stop.
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != player || !alert_filter(units_.unit_type[i], info.traders)) continue;
		if (!in_range(units_.pos_x[i], units_.pos_z[i])) continue;
		const uint8_t k = units_.order_kind[i];
		if (k != ORDER_NONE && k != ORDER_GARRISON) continue; // ShouldRespondToEndOfAlert
		uint32_t uid = units_.id[i];
		cmd_stop(&uid, 1);
		if (units_.work_kind[i] != ORDER_NONE) order_back_to_work(&uid, 1);
	}
	// Units inside nearby structures (and the raiser): unload, then back to work or stop.
	std::vector<std::pair<uint32_t, uint32_t>> out;
	for (const Building &b : buildings_) {
		if (b.team != player || b.garrisoned_units.empty()) continue;
		const float gap = std::hypot(b.x - raiser.x, b.z - raiser.z) - b.half_size - raiser.half_size;
		if (b.id != raiser.id && info.end_range >= 0.0f && gap > info.end_range) continue;
		for (uint32_t uid : b.garrisoned_units) {
			const int32_t i = lookup_unit(uid);
			if (i >= 0 && units_.team[i] == player && alert_filter(units_.unit_type[i], info.traders)) out.push_back({b.id, uid});
		}
	}
	for (auto [bid, uid] : out) {
		order_ungarrison_single(bid, uid);
		cmd_stop(&uid, 1); // rather than walk to the rally point
		const int32_t i = lookup_unit(uid);
		if (i >= 0 && units_.work_kind[i] != ORDER_NONE) order_back_to_work(&uid, 1);
	}
	return true;
}

void Simulation::order_herd(const uint32_t *unit_ids, size_t count, uint32_t fauna_id) {
	auto it = fauna_id_to_idx_.find(fauna_id);
	if (it == fauna_id_to_idx_.end()) return;
	size_t fidx = it->second;
	if (!fauna_.alive[fidx]) return;

	const auto &info = fauna_type_info(fauna_.fauna_type[fidx]);
	if (!info.is_domestic() || fauna_.behavior[fidx] == FAUNA_CORRALLED) {
		order_hunt(unit_ids, count, fauna_id); // wild game, or slaughtering penned livestock
		return;
	}

	// The first living land unit herds it to its own player's corral.
	uint32_t herder = 0;
	for (size_t k = 0; k < count && herder == 0; ++k) {
		int32_t uidx = lookup_unit(unit_ids[k]);
		if (uidx >= 0 && units_.alive[uidx] && units_.team[uidx] < MAX_PLAYERS && !unit_is_ship(units_.unit_type[uidx]))
			herder = unit_ids[k];
	}
	if (herder == 0) return;
	fauna_.behavior[fidx] = FAUNA_HERDED;
	fauna_.target_unit_id[fidx] = herder;

	for (size_t k = 0; k < count; ++k) {
		int32_t uidx = lookup_unit(unit_ids[k]);
		if (uidx < 0 || !units_.alive[uidx] || unit_is_ship(units_.unit_type[uidx])) continue;
		units_.action_state[uidx] = ACTION_MOVE;
		units_.has_order[uidx] = 1;
		drop_orders(uidx);
		units_.target_unit_id[uidx] = fauna_id;
		units_.target_x[uidx] = fauna_.pos_x[fidx];
		units_.target_z[uidx] = fauna_.pos_z[fidx];
	}
}

// The animal a corral of `civ` trains for `ftype`: ftype itself when the civ's corral
// lists it (structures/<civ>/corral.xml Trainer/Entities), else the civ's sheep, else its
// first animal. Corral queue entries keep their animal in ProductionOrder::tmpl.
uint8_t Simulation::corral_animal_for(uint8_t civ, uint16_t ftype) {
	const CorralTrainable &ct = CORRAL_TRAINABLE[civ < CIV_COUNT ? civ : 0];
	for (uint8_t k = 0; k < ct.count; ++k)
		if (ct.fauna[k] == ftype) return ct.fauna[k];
	for (uint8_t k = 0; k < ct.count; ++k)
		if (ct.fauna[k] == FAUNA_SHEEP_TRAINABLE) return ct.fauna[k];
	return ct.count ? ct.fauna[0] : (uint8_t)FAUNA_SHEEP_TRAINABLE;
}

// Cost/BuildTime of a trained animal, after Stockbreeding (Domestic Cost/BuildTime x0.75).
float Simulation::livestock_train_time(uint8_t player_id, uint8_t ftype) const {
	float t = fauna_train_cost(ftype).time;
	if (player_id >= MAX_PLAYERS) return t;
	const PlayerState &ps = players_[player_id];
	for (uint8_t id = 1; id < TECH_COUNT; ++id) {
		if (!ps.has_tech(id)) continue;
		const TechInfo &ti = tech_info(id);
		const TechEffect *fx = tech_effects_table() + ti.effect_begin;
		for (uint16_t k = 0; k < ti.effect_count; ++k) {
			const TechEffect &e = fx[k];
			if (e.stat != TSTAT_BUILD_TIME || !tech_effect_applies(e, TC_DOMESTIC)) continue;
			t = (e.op == TOP_MUL) ? t * e.value : (e.op == TOP_SET) ? e.value : t + e.value;
		}
	}
	return t;
}

bool Simulation::cmd_breed_livestock(uint8_t player_id, uint32_t building_id, uint8_t ftype) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return false;
	Building &b = buildings_[bit->second];
	if (b.team != player_id || b.build_progress < 1.0f || b.type != BLD_CORRAL) return false;
	if (b.queue.size() >= 8) return false;
	if (!entity_limit_allows_unit(player_id, UNIT_TYPE_NONE, CIV_UNIT_TEMPLATE_NONE)) return false; // Animal 50
	const uint8_t animal = corral_animal_for(civ_of(b.team), ftype);
	const FaunaTrainCost &c = fauna_train_cost(animal);
	if (!player(b.team).pay((float)c.res[0], (float)c.res[1], (float)c.res[3], (float)c.res[2])) return false;
	ProductionOrder o{(uint8_t)UNIT_TYPE_NONE, 0.0f, livestock_train_time(player_id, animal)};
	o.tmpl = animal;
	b.queue.push_back(o);
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
	float pick_x = 0.0f, pick_z = 0.0f;
	int walking = 0;

	for (size_t i = 0; i < count; ++i) {
		if ((int)passengers.size() >= max_cap) break;
		int32_t uidx = lookup_unit(unit_ids[i]);
		if (uidx < 0 || !units_.alive[uidx] || units_.team[uidx] != team) continue;
		if (!can_board((size_t)tidx, (size_t)uidx)) continue;

		float dx = units_.pos_x[uidx] - tx;
		float dz = units_.pos_z[uidx] - tz;
		float d2 = dx * dx + dz * dz;
		const float reach = unit_is_ship(units_.unit_type[tidx]) ? ship_loading_reach((size_t)tidx, (size_t)uidx) : 4.5f;

		if (d2 <= reach * reach) {
			units_.action_state[uidx] = ACTION_GARRISONED;
			units_.alive[uidx] = 0;
			units_.has_order[uidx] = 0;
			units_.vel_x[uidx] = 0.0f;
			units_.vel_z[uidx] = 0.0f;
			passengers.push_back(unit_ids[i]);
		} else {
			units_.action_state[uidx] = ACTION_MOVE;
			units_.has_order[uidx] = 1;
			drop_orders(uidx);
			units_.target_unit_id[uidx] = transport_id;
			units_.target_x[uidx] = tx;
			units_.target_z[uidx] = tz;
			pick_x += units_.pos_x[uidx];
			pick_z += units_.pos_z[uidx];
			++walking;
		}
	}
	// GarrisonHolder Pickup: an idle ship sails toward the units coming aboard,
	// as close to them as the water allows.
	// Petra's transport plan steers its own ship to the pickup point.
	const bool petra_ship = petra_transport_.ship_id == transport_id && petra_transport_.state != PetraTransportPlan::IDLE &&
			petra_transport_.state != PetraTransportPlan::FAILED;
	if (walking && !petra_ship && unit_is_ship(units_.unit_type[tidx]) && !units_.has_order[tidx]) {
		float wx = pick_x / (float)walking, wz = pick_z / (float)walking;
		if (nearest_water_for_ship((size_t)tidx, wx, wz, wx, wz)) {
			const uint32_t ship = transport_id;
			cmd_move(&ship, 1, wx, wz);
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
	// A ship unloads onto dry ground beside its hull (PickSpawnPoint); with no
	// shore in reach the passengers stay aboard, as in 0 A.D.
	if (unit_is_ship(units_.unit_type[tidx])) {
		if (!ship_unload_point((size_t)tidx, land_x, land_z)) return;
		found_land = true;
	}

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
			if (unit_is_ship(units_.unit_type[tidx])) {
				if (!ship_unload_point((size_t)tidx, land_x, land_z)) return; // no shore in reach: stays aboard
			} else
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

// 0 A.D. Promotion.js IncreaseXp. Xp is whole points; what healing grants below a
// point waits in xp_partial.
void Simulation::gain_xp(size_t idx, float amount) {
	if (idx >= units_.count || !(amount > 0.0f)) return;
	float total = units_.xp_partial[idx] + amount;
	float whole = std::floor(total);
	units_.xp_partial[idx] = total - whole;
	units_.xp[idx] = (uint16_t)std::min(65535.0f, (float)units_.xp[idx] + whole);
}

// 0 A.D. Health.js TakeDamage: the attacker gains Loot/xp * health taken / max health
// (the health actually lost, so overkill gives nothing). Call before applying damage.
// 0 A.D. BattleDetection.js OnGlobalAttacked: a player's detector counts the damage its
// own entities deal to another player's (not Gaia's, not its own). The total feeds the
// battle music; it is presentation state, so it is neither saved nor hashed.
void Simulation::note_attacked(uint32_t attacker_id, uint8_t attacker_owner, uint32_t target_id, uint8_t target_owner,
		bool target_is_building, float damage) {
	(void)attacker_id, (void)target_id, (void)target_is_building; // for the session's AttackDetection events
	if (attacker_owner >= MAX_PLAYERS || target_owner >= MAX_PLAYERS || attacker_owner == target_owner) return;
	if (damage > 0.0f) battle_damage_[attacker_owner] += damage;
}

void Simulation::award_damage_xp(uint32_t attacker_id, size_t victim, float damage) {
	if (attacker_id == 0 || victim >= units_.count) return;
	int32_t a = lookup_unit(attacker_id);
	if (a < 0 || !units_.alive[a]) return;
	float lost = std::min(damage, std::max(0.0f, units_.hp[victim]));
	if (!(lost > 0.0f) || !(units_.max_hp[victim] > 0.0f)) return;
	float xp = unit_loot(units_.tmpl[victim], units_.unit_type[victim]).xp * loot_rank_multiplier(units_.rank[victim]);
	gain_xp((size_t)a, xp * lost / units_.max_hp[victim]);
}

// 0 A.D. Attack.js HandleAttackEffects: loot goes to the attacking entity's Looter, so
// none is collected once the attacker is gone. Structures are looters too.
bool Simulation::looter_present(uint32_t looter_id, bool *infantry) const {
	*infantry = false;
	int32_t u = lookup_unit(looter_id);
	if (u >= 0) {
		if (!units_.alive[u]) return false;
		*infantry = (unit_tech_classes(units_.unit_type[u]) & TC_INFANTRY) != 0;
		return true;
	}
	auto bit = building_id_to_idx_.find(looter_id);
	return bit != building_id_to_idx_.end() && !buildings_[bit->second].wrecked;
}

// 0 A.D. Looter.js Collect: the victim's Loot (times its rank's Loot modifiers) with
// the looter's Looter/Resource modifiers, plus whatever the victim carried, to the
// looter's owner. Britons infantry add 1 of each (civbonuses/brit_woad_warriors.json).
// looter_id 0 means an older save's projectile with no recorded source.
void Simulation::collect_loot(uint32_t looter_id, uint8_t looter_player, LootInfo loot, float mult,
		uint8_t carried_type, int32_t carried) {
	if (looter_player >= MAX_PLAYERS) return;
	bool infantry = false;
	if (looter_id != 0 && !looter_present(looter_id, &infantry)) return;
	float res[RES_COUNT] = {};
	res[RES_FOOD] = loot.food * mult;
	res[RES_WOOD] = loot.wood * mult;
	res[RES_STONE] = loot.stone * mult;
	res[RES_GOLD] = loot.metal * mult;
	if (infantry && civ_of(looter_player) == CIV_BRITONS)
		for (float &r : res) r += 1.0f;
	if (carried > 0 && carried_type < RES_COUNT) res[carried_type] += (float)carried;
	players_[looter_player].add(res[RES_FOOD], res[RES_WOOD], res[RES_GOLD], res[RES_STONE]);
	stats_[looter_player].loot_collected += (uint32_t)std::lround(res[RES_FOOD] + res[RES_WOOD] + res[RES_GOLD] + res[RES_STONE]);
	if (looter_player == 0) {
		record_.total_food_gathered += (int)std::lround(res[RES_FOOD]);
		record_.total_wood_gathered += (int)std::lround(res[RES_WOOD]);
		record_.total_gold_gathered += (int)std::lround(res[RES_GOLD]);
		record_.total_stone_gathered += (int)std::lround(res[RES_STONE]);
	}
}

void Simulation::collect_unit_loot(uint32_t looter_id, uint8_t looter_player, size_t victim) {
	if (victim >= units_.count) return;
	collect_loot(looter_id, looter_player, unit_loot(units_.tmpl[victim], units_.unit_type[victim]),
			loot_rank_multiplier(units_.rank[victim]), units_.res_type_carried[victim], units_.carry[victim]);
}

void Simulation::collect_building_loot(uint32_t looter_id, uint8_t looter_player, const Building &b) {
	collect_loot(looter_id, looter_player, building_loot(b.type), 1.0f, RES_COUNT, 0);
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

void Simulation::mark_building_wreck(Building &b, uint8_t killer) {
	if (b.wrecked) return;
	stat_building_lost(b, killer);
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

void Simulation::record_unit_death(size_t i, uint8_t killer_team) {
	record_death(units_.pos_x[i], units_.pos_z[i], units_.team[i], killer_team, units_.unit_type[i], false);
	recent_deaths_[(recent_death_head_ - 1) % MAX_RECENT_DEATHS].death_damage = unit_death_damage(i) != nullptr;
}

// --- 0 A.D. StatisticsTracker.js ---------------------------------------------
// Counters per player by class (statistics_tracker.h), each entity's resource
// value (the sum of its costs, as GetEntityValue... in StatisticsTracker.js),
// and a sample of everything every 30 s for the summary charts.

static uint32_t stat_cost_value(int f, int w, int g, int st) { return (uint32_t)std::max(0, f + w + g + st); }

void Simulation::stat_unit_trained(size_t idx) {
	const uint8_t p = units_.team[idx];
	if (p >= MAX_PLAYERS) return;
	const uint16_t tmpl = units_.tmpl[idx];
	const uint32_t cls = stat_unit_classes(units_.unit_type[idx], tmpl < CIV_UNIT_TEMPLATE_COUNT ? civ_unit_template(tmpl).flags : 0);
	PlayerStatistics &st = stats_[p];
	for (uint8_t c = 0; c < STAT_UNIT_TOTAL; ++c)
		if (cls & (1u << c)) st.units_trained[c]++;
	st.units_trained[STAT_UNIT_TOTAL]++;
}

void Simulation::stat_unit_lost(size_t idx, uint8_t killer) {
	const uint8_t p = units_.team[idx];
	const uint16_t tmpl = units_.tmpl[idx];
	const uint32_t cls = stat_unit_classes(units_.unit_type[idx], tmpl < CIV_UNIT_TEMPLATE_COUNT ? civ_unit_template(tmpl).flags : 0);
	const UnitTypeInfo &info = unit_info_at(idx);
	const uint32_t value = stat_cost_value(info.food_cost, info.wood_cost, info.gold_cost, info.stone_cost);
	// LostEntity for the owner, KilledEntity for an enemy that killed it.
	if (p < MAX_PLAYERS) {
		PlayerStatistics &st = stats_[p];
		for (uint8_t c = 0; c < STAT_UNIT_TOTAL; ++c)
			if (cls & (1u << c)) st.units_lost[c]++;
		st.units_lost[STAT_UNIT_TOTAL]++;
		st.units_lost_value += value;
	}
	if (killer < MAX_PLAYERS && killer != p) {
		PlayerStatistics &st = stats_[killer];
		for (uint8_t c = 0; c < STAT_UNIT_TOTAL; ++c)
			if (cls & (1u << c)) st.enemy_units_killed[c]++;
		st.enemy_units_killed[STAT_UNIT_TOTAL]++;
		st.enemy_units_killed_value += value;
	}
}

void Simulation::stat_building_constructed(const Building &b) {
	if (b.team >= MAX_PLAYERS) return;
	PlayerStatistics &st = stats_[b.team];
	const uint8_t c = stat_building_class(b.type);
	if (c < STAT_BLD_TOTAL) st.buildings_constructed[c]++;
	st.buildings_constructed[STAT_BLD_TOTAL]++;
}

void Simulation::stat_building_lost(const Building &b, uint8_t killer) {
	// Foundations are not counted (StatisticsTracker.js KilledEntity / LostEntity skip them).
	if (b.build_progress < 1.0f) return;
	const uint8_t c = stat_building_class(b.type);
	const BuildingTypeInfo &info = building_type_info(b.type);
	const uint32_t value = stat_cost_value(info.food_cost, info.wood_cost, info.gold_cost, info.stone_cost);
	if (b.team < MAX_PLAYERS) {
		PlayerStatistics &st = stats_[b.team];
		if (c < STAT_BLD_TOTAL) st.buildings_lost[c]++;
		st.buildings_lost[STAT_BLD_TOTAL]++;
		st.buildings_lost_value += value;
	}
	if (killer < MAX_PLAYERS && killer != b.team) {
		PlayerStatistics &st = stats_[killer];
		if (c < STAT_BLD_TOTAL) st.enemy_buildings_destroyed[c]++;
		st.enemy_buildings_destroyed[STAT_BLD_TOTAL]++;
		st.enemy_buildings_destroyed_value += value;
	}
}

void Simulation::stat_building_captured(const Building &b, uint8_t from) {
	// The new owner captured it (CapturedBuilding); the old one lost it (LostEntity).
	const uint8_t c = stat_building_class(b.type);
	const BuildingTypeInfo &info = building_type_info(b.type);
	const uint32_t value = stat_cost_value(info.food_cost, info.wood_cost, info.gold_cost, info.stone_cost);
	if (b.team < MAX_PLAYERS) {
		PlayerStatistics &st = stats_[b.team];
		if (c < STAT_BLD_TOTAL) st.buildings_captured[c]++;
		st.buildings_captured[STAT_BLD_TOTAL]++;
		st.buildings_captured_value += value;
	}
	if (from < MAX_PLAYERS && b.build_progress >= 1.0f) {
		PlayerStatistics &st = stats_[from];
		if (c < STAT_BLD_TOTAL) st.buildings_lost[c]++;
		st.buildings_lost[STAT_BLD_TOTAL]++;
		st.buildings_lost_value += value;
	}
}

void Simulation::stat_gathered(uint8_t player_id, uint8_t res, int amount, bool vegetarian) {
	if (player_id >= MAX_PLAYERS || res >= RES_COUNT || amount <= 0) return;
	stats_[player_id].resources_gathered[res] += (uint32_t)amount;
	if (vegetarian) stats_[player_id].vegetarian_food_gathered += (uint32_t)amount;
}

PlayerStatistics Simulation::statistics(uint8_t player_id) const {
	if (player_id >= MAX_PLAYERS) return PlayerStatistics{};
	PlayerStatistics st = stats_[player_id];
	for (int r = 0; r < RES_COUNT; ++r) st.resources_used[r] = (int32_t)std::lround(players_[player_id].used[r]);
	st.peak_percent_map_controlled = std::max(st.peak_percent_map_controlled, percent_map_controlled(player_id));
	st.team_peak_percent_map_controlled = std::max(st.team_peak_percent_map_controlled, team_percent_map_controlled(player_id));
	return st;
}

// Each player's explored cells, refreshed once a second from what its own units
// and structures can see. RTS0 keeps full fog only for player 0 (fow_grid_),
// so this coarse grid gives every player GetPercentMapExplored cheaply.
void Simulation::update_statistics_explored() {
	const int res = std::max(1, (int)std::ceil(map_half_ * 2.0f / STATISTICS_EXPLORE_CELL));
	if (res != stats_explore_res_) {
		stats_explore_res_ = res;
		for (auto &g : stats_explored_) g.assign((size_t)res * res, 0);
	}
	const float cell = map_half_ * 2.0f / (float)res;
	auto mark = [&](uint8_t p, float x, float z, float radius) {
		if (p >= MAX_PLAYERS) return;
		std::vector<uint8_t> &g = stats_explored_[p];
		const int cx0 = std::clamp((int)((x - radius + map_half_) / cell), 0, res - 1);
		const int cx1 = std::clamp((int)((x + radius + map_half_) / cell), 0, res - 1);
		const int cz0 = std::clamp((int)((z - radius + map_half_) / cell), 0, res - 1);
		const int cz1 = std::clamp((int)((z + radius + map_half_) / cell), 0, res - 1);
		const float r2 = (radius + cell * 0.5f) * (radius + cell * 0.5f);
		for (int cz = cz0; cz <= cz1; ++cz) {
			const float dz = (cz + 0.5f) * cell - map_half_ - z;
			for (int cx = cx0; cx <= cx1; ++cx) {
				const float dx = (cx + 0.5f) * cell - map_half_ - x;
				if (dx * dx + dz * dz <= r2) g[(size_t)cz * res + cx] = 1;
			}
		}
	};
	// Vision ranges as tick_fow's update_player_fow uses them.
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] && units_.action_state[i] != ACTION_GARRISONED) continue;
		const uint8_t t = units_.unit_type[i];
		const float r = (t == UNIT_TRIREME) ? 28.0f : (t == UNIT_CATAPULT) ? 24.0f : (t == UNIT_ARCHER) ? 22.0f
			: (t == UNIT_CAVALRY || t == UNIT_ELEPHANT) ? 20.0f : 16.0f;
		mark(units_.team[i], units_.pos_x[i], units_.pos_z[i], r);
	}
	for (const Building &b : buildings_)
		if (b.hp > 0.0f && !b.wrecked) mark(b.team, b.x, b.z, b.build_progress >= 1.0f ? 24.0f : 8.0f);
}

float Simulation::percent_map_explored(uint8_t player_id) const {
	if (player_id >= MAX_PLAYERS) return 0.0f;
	if (reveal_map_ || explore_map_) return 100.0f;
	const auto &g = stats_explored_[player_id];
	if (g.empty()) return 0.0f;
	size_t n = 0;
	for (uint8_t c : g) n += c;
	return 100.0f * (float)n / (float)g.size();
}

float Simulation::team_percent_map_explored(uint8_t player_id) const {
	if (player_id >= MAX_PLAYERS) return 0.0f;
	if (reveal_map_ || explore_map_) return 100.0f;
	const auto &mine = stats_explored_[player_id];
	if (mine.empty()) return 0.0f;
	size_t n = 0;
	for (size_t c = 0; c < mine.size(); ++c) {
		uint8_t seen = 0;
		for (uint8_t q = 0; q < MAX_PLAYERS && !seen; ++q)
			if (q == player_id || (is_ally(player_id, q) && is_ally(q, player_id)))
				seen = stats_explored_[q].size() == mine.size() ? stats_explored_[q][c] : 0;
		n += seen;
	}
	return 100.0f * (float)n / (float)mine.size();
}

float Simulation::percent_map_controlled(uint8_t player_id) const {
	if (player_id >= MAX_PLAYERS || territory_grid_.data.empty()) return 0.0f;
	size_t n = 0;
	for (uint8_t v : territory_grid_.data) n += (v == player_id + 1);
	return 100.0f * (float)n / (float)territory_grid_.data.size();
}

float Simulation::team_percent_map_controlled(uint8_t player_id) const {
	if (player_id >= MAX_PLAYERS || territory_grid_.data.empty()) return 0.0f;
	bool team[MAX_PLAYERS] = {};
	for (uint8_t q = 0; q < MAX_PLAYERS; ++q)
		team[q] = q == player_id || (is_ally(player_id, q) && is_ally(q, player_id));
	size_t n = 0;
	for (uint8_t v : territory_grid_.data) n += (v >= 1 && v <= MAX_PLAYERS && team[v - 1]);
	return 100.0f * (float)n / (float)territory_grid_.data.size();
}

StatisticsSample Simulation::statistics_snapshot() const {
	StatisticsSample row;
	row.time = (float)ticks_ * FIXED_STEP;
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		row.players[p] = statistics(p);
		row.percent_map_explored[p] = percent_map_explored(p);
		row.team_percent_map_explored[p] = team_percent_map_explored(p);
		row.percent_map_controlled[p] = percent_map_controlled(p);
		row.team_percent_map_controlled[p] = team_percent_map_controlled(p);
	}
	return row;
}

void Simulation::tick_statistics() {
	// Counted in ticks so the 30 s rows land on exact game times. ticks_ counts
	// the ticks before this one.
	const uint64_t tick = ticks_ + 1;
	if (tick % (uint64_t)std::lround(1.0f / FIXED_STEP) == 0) update_statistics_explored();
	// StatisticsTracker.updateSequences, every updateTimeInterval.
	if (tick % (uint64_t)std::lround(STATISTICS_SAMPLE_INTERVAL / FIXED_STEP) != 0) return;
	if (stat_samples_.size() >= MAX_STATISTICS_SAMPLES) return;
	stat_samples_.push_back(statistics_snapshot());
	stat_samples_.back().time = (float)tick * FIXED_STEP;
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		stats_[p].peak_percent_map_controlled = stat_samples_.back().players[p].peak_percent_map_controlled;
		stats_[p].team_peak_percent_map_controlled = stat_samples_.back().players[p].team_peak_percent_map_controlled;
	}
}

// fire_arrow's horizontal speed by projectile look (arrow, warship bolt, stone).
float Simulation::projectile_speed(uint8_t visual) {
	return visual == 1 ? 56.0f : (visual == 2 ? 22.0f : 26.0f);
}

// 0 A.D. globalscripts randomNormal2D: two independent standard normals (Box-Muller)
// from the sim's own seeded generator, so every peer scatters the same way.
void Simulation::random_normal_2d(float &x, float &z) {
	const float u1 = std::max(1.0e-7f, sim_rng_.range_float(0.0f, 1.0f));
	const float u2 = sim_rng_.range_float(0.0f, 1.0f);
	const float r = std::sqrt(-2.0f * std::log(u1));
	x = r * std::cos(6.28318530718f * u2);
	z = r * std::sin(6.28318530718f * u2);
}

void Simulation::fire_arrow(float sx, float sy, float sz, float tx, float ty, float tz, float damage, uint8_t team, uint32_t target_id, uint8_t effect, uint8_t dmg_type, bool hits_building, uint8_t visual, uint32_t source_id, uint8_t splash) {
	if (projectiles_.size() >= MAX_PROJECTILES) return;
	Projectile p;
	p.id = alloc_id();
	p.start_x = sx; p.start_y = sy; p.start_z = sz;
	p.target_x = tx; p.target_y = ty; p.target_z = tz;
	p.current_x = sx; p.current_y = sy; p.current_z = sz;
	p.progress = 0.0f;
	p.speed = projectile_speed(visual); // warship: template_unit_ship_warship Ranged/Projectile Speed=80 Gravity=50
	p.arc_height = visual == 1 ? 3.8f : (visual == 2 ? 5.5f : 2.4f);
	p.visual = visual;
	p.damage = damage;
	p.damage_type = dmg_type;
	p.status_effect = effect;
	p.team = team;
	p.source_id = source_id;
	p.splash = splash;
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
	// 0. Map triggers (0 A.D. Timer.js runs its timers at the start of a turn)
	tick_triggers(dt);

	// 1. Movement & spatial separation
	tick_pack(dt);           // 0 A.D. Pack.js: siege engines pack to move, unpack to fire
	if (!formations_.empty()) formation_refresh_groups(); // unit indices move when the dead are removed
	tick_unit_ai(dt);        // 0 A.D. UnitAI.js: answer attacks, flee, patrol, resume and queue orders
	move_units(dt, arena);
	separation(dt, arena);
	tick_formations(dt);     // 0 A.D. Formation.js: controllers walk, members follow their slots
	tick_turrets();          // 0 A.D. TurretHolder.js / Turretable.js: units on walls and outposts

	// 2. Economy & Gathering
	tick_gather(dt);

	// 3. Combat, promotion, structure capture, status effects, and projectiles
	tick_combat(dt);
	tick_promotion(dt);      // 0 A.D. Promotion.js: ranks from xp, and the barracks xp trickle
	tick_capture(dt);        // 0 A.D. Capturable.js: multi-team structure capture & regeneration
	tick_status_effects(dt);
	tick_projectiles(dt);
	tick_death_damage(dt);   // 0 A.D. DeathDamage.js: fire ships explode on death

	// 4. Infrastructure & Production
	tick_construction(dt);   // 0 A.D. Foundation.js / Builder.js: building construction
	tick_repair(dt);         // 0 A.D. Repairable.js / Builder.js: repairing damaged completed buildings
	tick_production(dt);     // 0 A.D. ProductionQueue.js: unit training queues
	tick_research(dt);       // 0 A.D. ProductionQueue.js / Researcher.js: technology research progress
	tick_upgrades(dt);       // 0 A.D. Upgrade.js: structure upgrades (sentry tower -> tower, wall -> gate)
	tick_siege_garrisons();  // 0 A.D. GarrisonHolder EjectHealth / OnDestroy for siege towers and rams
	tick_garrison_holders(); // 0 A.D. GarrisonHolder BuffHeal / EjectHealth for structures
	tick_gates(dt);          // 0 A.D. Gate.js:145-180: auto open / close when allied units in pass range

	// 5. Commerce & Trade
	tick_barter(dt);         // 0 A.D. Barter.js: prices drift back after deals
	tick_trickle(dt);        // Passive / trickle income

	// 6. Waves, AI, and World simulation
	if (waves_enabled_) tick_waves(dt);
	tick_petra_ai(dt);
	tick_fauna(dt);
	tick_treasures(dt);      // 0 A.D. Treasure.js / TreasureCollector.js
	check_victory_conditions(dt);
	tick_statistics();       // 0 A.D. StatisticsTracker.js: per-player counters and 30 s sequences

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

	// G3 Trader routes (0 A.D. Trader.js PerformTrade, UnitAI TRADE), run
	// sequentially before the parallel movement pass.
	for (size_t ti = 0; ti < n; ++ti) {
		if (!alive[ti] || !unit_is_trader(units_.unit_type[ti])) continue;
		uint32_t uid = units_.id[ti];
		const uint8_t owner = units_.team[ti];
		const uint8_t ttype = units_.unit_type[ti];
		auto rit = trader_routes_.find(uid);
		if (units_.action_state[ti] != ACTION_TRADE || rit == trader_routes_.end()) {
			// An idle trader looks for a route, at most every 20 ticks (2 s).
			if (units_.action_state[ti] == ACTION_IDLE && !units_.has_order[ti] && (ticks_ + uid) % 20 == 0)
				order_trade_auto(&uid, 1);
			continue;
		}
		auto itA = building_id_to_idx_.find(rit->second.first);
		auto itB = building_id_to_idx_.find(rit->second.second);
		const Building *bA = (itA != building_id_to_idx_.end()) ? &buildings_[itA->second] : nullptr;
		const Building *bB = (itB != building_id_to_idx_.end()) ? &buildings_[itB->second] : nullptr;
		if (!bA || !bB || !can_trade_at(owner, *bA, ttype) || !can_trade_at(owner, *bB, ttype)) {
			// Market.js UpdateTraders: a market destroyed, captured by an enemy or
			// at war with the trader's owner ends the route and drops the goods.
			trader_routes_.erase(rit);
			trader_leg_.erase(uid);
			units_.carry[ti] = 0;
			units_.action_state[ti] = ACTION_IDLE;
			units_.has_order[ti] = 0;
			continue;
		}
		int leg = 0;
		auto lit = trader_leg_.find(uid);
		if (lit != trader_leg_.end()) leg = lit->second;
		const Building &dest = leg == 0 ? *bA : *bB;
		const Building &origin = leg == 0 ? *bB : *bA;
		// Keep walking toward the destination market.
		if (!has[ti] && units_.action_state[ti] == ACTION_TRADE) {
			tx[ti] = dest.x; tz[ti] = dest.z; has[ti] = 1;
		}
		const float dx = dest.x - px[ti];
		const float dz = dest.z - pz[ti];
		const float edge_dx = std::max(0.0f, std::fabs(dx) - dest.half_size);
		const float edge_dz = std::max(0.0f, std::fabs(dz) - dest.half_size);
		// A ship stops in the water beside the dock, a little further out.
		const float reach = ttype == UNIT_MERCHANT_SHIP ? 4.0f : 2.0f;
		if (dx * dx + dz * dz >= 2.5f * 2.5f && edge_dx * edge_dx + edge_dz * edge_dz >= reach * reach) continue;

		// Arrived. Goods loaded at the other market pay out here (Trader.js
		// GenerateResources): the trader's owner, and with markets of two
		// different owners, each market's owner its international bonus.
		if (units_.carry[ti] > 0) {
			const uint8_t goods = units_.res_type_carried[ti];
			const TradeGain g = calculate_trade_gain(origin, dest, owner, ttype);
			credit_trade(owner, goods, g.trader);
			credit_trade(origin.team, goods, g.market1);
			credit_trade(dest.team, goods, g.market2);
		}
		// Load the next goods (Player.js GetNextTradingGoods) and head back.
		units_.res_type_carried[ti] = next_trading_goods(owner);
		units_.carry[ti] = 10; // shows the caravan as loaded
		const int new_leg = 1 - leg;
		trader_leg_[uid] = new_leg;
		const Building &next = new_leg == 0 ? *bA : *bB;
		tx[ti] = next.x; tz[ti] = next.z;
		units_.anchor_x[ti] = next.x;
		units_.anchor_z[ti] = next.z;
		has[ti] = 1;
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
				// A wide unit (siege, elephants) can't get within ARRIVE_DIST of a waypoint closer to a
				// building than separation() lets it stand (its radius + 0.35), such as a navcell in a
				// 2-unit gap between houses: it would push against the buildings there for good. Like
				// 0 A.D. UnitMotion, which moves on to the next long-path waypoint once its short path
				// gets as close as it can, it moves on once within WAYPOINT_NEAR of such a waypoint.
				const float crowd = unit_collision_radius(units_.unit_type[i]) + 0.35f - ARRIVE_DIST;
				auto crowded = [&](float wx, float wz) {
					if (crowd <= 0.0f) return false; // ships too: a transport's path can start beside a shore house
					for (const SteerObstacle &b : steer_obstacles_) {
						if (b.id == units_.target_building_id[i]) continue;
						const float ex = std::max(0.0f, std::fabs(wx - b.x) - b.half_size);
						const float ez = std::max(0.0f, std::fabs(wz - b.z) - b.half_size);
						if (ex < crowd && ez < crowd && ex * ex + ez * ez < crowd * crowd) return true;
					}
					return false;
				};
				constexpr float WAYPOINT_NEAR = 4.0f;
				// Skip every waypoint already within ARRIVE_DIST (inclusive). Advancing one per tick
				// with a strict < let the arrival check below stop the unit on a waypoint exactly
				// ARRIVE_DIST away, or on a second one just as close, short of its target.
				bool advanced = false;
				while (cur < cnt && cur < wp.count) {
					const float pdx = wp.xs[cur] - px[i];
					const float pdz = wp.zs[cur] - pz[i];
					const float pd2 = pdx * pdx + pdz * pdz;
					if (pd2 > ARRIVE_DIST * ARRIVE_DIST &&
					    (cur + 1 >= cnt || pd2 > WAYPOINT_NEAR * WAYPOINT_NEAR || !crowded(wp.xs[cur], wp.zs[cur])))
						break;
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
		// Attack.js MinRange: a siege engine too close to its target walks away from it
		// (UnitMotion MoveToTargetRange with a minimum); tick_pack unpacks it once past MinRange.
		bool backing_off = false;
		if (units_.action_state[i] == ACTION_ATTACK && units_.target_unit_id[i] != 0 && !using_path) {
			const float minr = units_.attack_range[i] * unit_min_range_ratio(units_.unit_type[i]);
			if (minr > 0.0f && d2 < minr * minr) {
				backing_off = true;
				if (d2 < 1e-6f) { dx = 1.0f; dz = 0.0f; }
				dx = -dx; dz = -dz;
				d2 = dx * dx + dz * dz;
			}
		}
		bool at_building = false;
		if (units_.action_state[i] == ACTION_ATTACK && !backing_off && units_.target_unit_id[i] == 0 &&
				units_.target_building_id[i] != 0) {
			// The aim is a stand point beside the structure (stand_outside_rect): walk to it,
			// stopping once the structure's edge is within reach (the same eff_d as tick_combat).
			auto bit = building_id_to_idx_.find(units_.target_building_id[i]);
			if (bit != building_id_to_idx_.end()) {
				const Building &b = buildings_[bit->second];
				const bool capture = (units_.order_flags[i] & ORDER_FLAG_ALLOW_CAPTURE) &&
						unit_capture_attack(units_.tmpl[i], units_.unit_type[i]) > 0.0f;
				const float reach = capture ? CAPTURE_RANGE : units_.attack_range[i];
				const float eff_d = std::hypot(px[i] - b.x, pz[i] - b.z) - b.half_size;
				at_building = eff_d <= reach * 0.9f;
				if (!at_building) stop_dist = ARRIVE_DIST * 0.5f;
			}
		}
		if (at_building) {
			d2 = 0.0f; // in reach: stop here
		} else if (units_.action_state[i] == ACTION_ATTACK && !backing_off && units_.target_building_id[i] == 0) {
			float r_att = units_.attack_range[i];
			if (r_att > 3.0f) {
				// Ranged units (Archers, Catapults, Triremes) stop comfortably within volley distance
				stop_dist = std::max(2.5f, r_att * 0.85f);
			} else {
				// Melee units stop right at weapon reach to encircle target without penetration
				stop_dist = std::max(1.1f, r_att - 0.2f);
			}
		}

		if (!backing_off && d2 <= stop_dist * stop_dist) {
			vx[i] = 0;
			vz[i] = 0;
			if (units_.action_state[i] == ACTION_MOVE || units_.action_state[i] == ACTION_ATTACK_MOVE) {
				has[i] = 0;
				units_.action_state[i] = ACTION_IDLE;
				units_.waypoint_cur[i] = 0;
				units_.waypoint_count[i] = 0;
				if ((size_t)i < unit_paths_.size()) unit_paths_[i].clear();
			}
			// A patrol turns round in tick_unit_ai(), which runs first.
			return;
		}

		// Pack.js: a siege engine moves only while packed (unpacked templates have WalkSpeed 0.001).
		if (!pack_state_can_move(units_.pack_state[i])) {
			vx[i] = vz[i] = 0;
			return;
		}

		// 0 A.D. UnitMotion short path: head for the first waypoint around a blocking building
		// (nav/building_steering.h). The arrival check above still uses the real aim point.
		if (!unit_is_ship(units_.unit_type[i]) && !backing_off) {
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
		if (units_.status_effects[i] & (EFFECT_SNARE | EFFECT_SLOW)) {
			spd *= 0.55f; // Slow/Snare speed penalty (0 A.D. status effect)
		}
		// Formation members walk to the slot tick_formations gives them (Formation.js FormationWalk).
		float move_dx = ux * spd * dt;
		float move_dz = uz * spd * dt;
		if (units_.status_effects[i] & EFFECT_KNOCKBACK) {
			move_dx += units_.knockback_dir_x[i] * 14.0f * dt;
			move_dz += units_.knockback_dir_z[i] * 14.0f * dt;
		}
		npx[i] = std::clamp(px[i] + move_dx, -map_half_, map_half_);
		npz[i] = std::clamp(pz[i] + move_dz, -map_half_, map_half_);
		// Deep water is impassable for land units (template_unit.xml passability classes).
		// Walks get a goal on the beach from find_path; a land unit following a ship
		// (boarding it, or chasing it) has no path, so it stops at the shore and slides
		// along it instead of stepping off. One already in deep water may still move.
		const int32_t followed = units_.target_unit_id[i] != 0 ? lookup_unit(units_.target_unit_id[i]) : -1;
		if (followed >= 0 && !unit_is_ship(units_.unit_type[i]) && unit_is_ship(units_.unit_type[followed]) &&
				!nav_grid_.cells.blank()) {
			auto deep = [&](float x, float z) {
				auto [cx, cy] = nav_grid_.world_to_cell(x, z);
				return nav_grid_.cells.inside(cx, cy) && nav_grid_.cells.at(cx, cy) == NAV_WATER;
			};
			if (deep(npx[i], npz[i]) && !deep(px[i], pz[i])) {
				if (!deep(npx[i], pz[i])) npz[i] = pz[i];
				else if (!deep(px[i], npz[i])) npx[i] = px[i];
				else { npx[i] = px[i]; npz[i] = pz[i]; }
			}
		}
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
			if (tidx >= 0 && units_.alive[tidx] && is_transport(units_.id[tidx]) && can_board((size_t)tidx, i)) {
				float dx = px[i] - px[tidx];
				float dz = pz[i] - pz[tidx];
				const float reach = unit_is_ship(units_.unit_type[tidx]) ? ship_loading_reach((size_t)tidx, i) : 4.5f;
				if (dx * dx + dz * dz <= reach * reach) {
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

	// The building pass below only reads standing buildings that block movement now.
	sep_obstacles_.clear();
	for (const Building &b : buildings_)
		if (!b.wrecked && blocks_movement_now(b))
			sep_obstacles_.push_back(SteerObstacle{b.id, b.x, b.z, b.half_size, b.type, false});
	sep_grid_.build(sep_obstacles_, map_half_);

	// The widest unit this tick: no pair is pushed apart beyond ri + r_max + 0.4 on either axis.
	float r_max = 0.0f;
	for (size_t i = 0; i < n; ++i)
		if (units_.alive[i]) r_max = std::max(r_max, unit_collision_radius(units_.unit_type[i]));

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
		const uint32_t my_group = (size_t)i < formation_group_.size() ? formation_group_[i] : 0u;
		// A unit beyond comfort_dist on either axis pushes nothing, so the grid skips cells that
		// hold only such units (0.5 more for float rounding).
		grid.visit_radius_within(pxi, pzi, scan_radius, ri + r_max + 0.40f + 0.5f, [&](int32_t j) {
			if ((size_t)j == i || !units_.alive[j]) return;
			// 0 A.D. gives a formation's members one obstruction group: they never push each other.
			if (my_group && (size_t)j < formation_group_.size() && formation_group_[j] == my_group) return;
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
		// A building more than b_req from the unit on either axis pushes nothing; the 0.5
		// margin keeps that test clear of float rounding, so skipping it changes no result.
		const float b_skip = ri + 0.35f + 0.5f;
		auto push_out = [&](const SteerObstacle &b) {
			if (std::fabs(pxi - b.x) > b.half_size + b_skip || std::fabs(pzi - b.z) > b.half_size + b_skip) return false;
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
				return false;
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
			return false;
		};
		// sep_grid_ hands over the buildings near enough to pass push_out()'s first test, in list
		// order, so the pushes add up exactly as over the whole list.
		if (!sep_grid_.for_each_near(pxi, pzi, b_skip, [&](uint32_t k) { return push_out(sep_obstacles_[k]); }))
			for (const SteerObstacle &b : sep_obstacles_) push_out(b);

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

uint32_t Simulation::find_free_fish(uint16_t sea, float x, float z, float max_d, uint32_t skip) const {
	if (!sea) return 0;
	uint32_t best = 0;
	float best_d2 = max_d * max_d;
	for (size_t r = 0; r < resources_.count; ++r) {
		if (resources_.subtype[r] != RES_SUB_FISH || !resources_.alive[r] || resources_.amount[r] <= 0) continue;
		if (resources_.id[r] == skip) continue;
		auto git = fish_gatherers_.find(resources_.id[r]);
		if (git != fish_gatherers_.end() && git->second.total >= FISH_MAX_GATHERERS) continue;
		const float dx = resources_.x[r] - x, dz = resources_.z[r] - z;
		const float d2 = dx * dx + dz * dz;
		if (d2 >= best_d2) continue;
		auto [cx, cy] = nav_grid_.world_to_cell(resources_.x[r], resources_.z[r]);
		if (nav_grid_.global_region_at(cx, cy, PASS_SHIP) != sea) continue;
		best_d2 = d2;
		best = resources_.id[r];
	}
	return best;
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
	// Boats fishing each fish this tick (MaxGatherers 4, and fish regrow only while nobody fishes them).
	fish_gatherers_.clear();
	for (size_t i = 0; i < n; ++i) {
		if (!units_.alive[i] || units_.unit_type[i] != UNIT_FISHING_BOAT || units_.action_state[i] != ACTION_GATHER) continue;
		if (units_.target_resource_id[i] != 0) fish_gatherers_[units_.target_resource_id[i]].total++;
	}
	ruins_gatherers_.clear();
	for (size_t i = 0; i < n; ++i) {
		bool is_gatherer = (units_.unit_type[i] == UNIT_VILLAGER ||
		                    units_.unit_type[i] == UNIT_FEMALE_CITIZEN ||
		                    units_.unit_type[i] == UNIT_WORKER_ELEPHANT ||
		                    units_.unit_type[i] == UNIT_FISHING_BOAT ||
		                    (units_.unit_type[i] == UNIT_CAVALRY && units_.carry_capacity[i] > 0)); // meat only, below
		if (!units_.alive[i] || !is_gatherer) continue;
		uint8_t uteam = units_.team[i];

		// 1. Returning to dropsite
		// Gathering, idle or building workers with a full load return it (Petra's builders
		// rely on this, as 0 A.D. builders drop carried resources at a dropsite); a full
		// worker the player walks elsewhere or sends to fight keeps its order.
		const uint8_t gst = units_.action_state[i];
		if (((gst == ACTION_GATHER || gst == ACTION_IDLE || gst == ACTION_BUILD) &&
					units_.carry[i] >= carry_cap(i, units_.res_type_carried[i])) ||
				units_.action_state[i] == ACTION_RETURN_DROPSITE) {
			float best_d2 = 1e9f;
			float drop_x = 0, drop_z = 0, drop_half = 0;
			bool found_dropsite = false;

			for (const auto &b : buildings_) {
				if (b.team != uteam || b.build_progress < 1.0f) continue;
				bool valid_drop = false;
				if (units_.unit_type[i] == UNIT_FISHING_BOAT) valid_drop = b.type == BLD_DOCK; // boats can only reach docks
				else if (b.type == BLD_TOWN_CENTER || civ_building_is_dropsite(b.type)) valid_drop = true;
				else if (b.type == BLD_STOREHOUSE && (units_.res_type_carried[i] != RES_FOOD)) valid_drop = true;
				else if (b.type == BLD_FARMSTEAD && (units_.res_type_carried[i] == RES_FOOD)) valid_drop = true;
				else if (b.type == BLD_DOCK && units_.res_type_carried[i] == RES_FOOD) valid_drop = true;

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
				// A boat unloads from the water beside the dock, a little further out.
				const float drop_reach = units_.unit_type[i] == UNIT_FISHING_BOAT ? 4.0f : 1.5f;
				if (dx * dx + dz * dz < 2.5f * 2.5f || edge_dx * edge_dx + edge_dz * edge_dz < drop_reach * drop_reach) {
					int amt = units_.carry[i];
					uint8_t rt = units_.res_type_carried[i];
					if (rt < RES_COUNT) {
						// StatisticsTracker vegetarianFood: food from fruit or grain, not meat or fish.
						bool vegetarian = false;
						if (rt == RES_FOOD && units_.target_resource_id[i] != 0) {
							auto vit = res_id_to_idx_.find(units_.target_resource_id[i]);
							vegetarian = vit != res_id_to_idx_.end() &&
								(resources_.subtype[vit->second] == RES_SUB_NONE || resources_.subtype[vit->second] == RES_SUB_GRAIN);
						}
						stat_gathered(uteam, rt, amt, vegetarian);
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
			const bool boat = units_.unit_type[i] == UNIT_FISHING_BOAT;
			auto next_fish = [&]() {
				// UnitAI: a boat whose fish is gone or full moves on to the nearest fish on its sea.
				auto [cx, cy] = nav_grid_.world_to_cell(units_.pos_x[i], units_.pos_z[i]);
				const uint32_t alt = find_free_fish(nav_grid_.global_region_at(cx, cy, PASS_SHIP),
				                                    units_.pos_x[i], units_.pos_z[i], 40.0f, units_.target_resource_id[i]);
				if (alt) {
					fish_gatherers_[alt].total++;
					order_gather(&units_.id[i], 1, alt);
				} else {
					units_.target_resource_id[i] = 0;
					units_.action_state[i] = ACTION_IDLE;
				}
			};
			if (rit == res_id_to_idx_.end() || !resources_.alive[rit->second] || resources_.amount[rit->second] <= 0) {
				if (boat) { next_fish(); continue; }
				units_.target_resource_id[i] = 0;
				units_.action_state[i] = ACTION_IDLE;
				continue;
			}
			size_t ridx = rit->second;
			const bool fish = resources_.subtype[ridx] == RES_SUB_FISH;
			if (boat != fish) { // only fishing boats fish, and they only fish
				units_.target_resource_id[i] = 0;
				units_.action_state[i] = ACTION_IDLE;
				continue;
			}
			// template_unit_cavalry Rates: food.meat only.
			if (units_.unit_type[i] == UNIT_CAVALRY && resources_.subtype[ridx] != RES_SUB_MEAT) {
				units_.target_resource_id[i] = 0;
				units_.action_state[i] = ACTION_IDLE;
				continue;
			}
			if (fish) {
				FieldGatherers &fg = fish_gatherers_[units_.target_resource_id[i]];
				if (fg.slots_taken >= FISH_MAX_GATHERERS) {
					fg.total--;
					next_fish();
					continue;
				}
				fg.slots_taken++;
			}
			if (resources_.subtype[ridx] == RES_SUB_RUINS) {
				// ResourceSupply MaxGatherers: a full ruin sends the worker to the nearest other
				// ruin with room, else to a rock or mine of the same resource (FINDINGNEWTARGET).
				const uint32_t rid = units_.target_resource_id[i];
				FieldGatherers &rg = ruins_gatherers_[rid];
				if (rg.slots_taken >= ruins_cap(rid)) {
					const uint8_t rt = resources_.type[ridx];
					uint32_t alt = 0;
					float best = NEARBY_SEARCH_RANGE * NEARBY_SEARCH_RANGE;
					for (size_t r = 0; r < resources_.count; ++r) {
						if (!resources_.alive[r] || resources_.amount[r] <= 0 || resources_.type[r] != rt || r == ridx) continue;
						if (resources_.subtype[r] == RES_SUB_RUINS) {
							auto g = ruins_gatherers_.find(resources_.id[r]);
							if (g != ruins_gatherers_.end() && g->second.slots_taken >= ruins_cap(resources_.id[r])) continue;
						} else if (resources_.subtype[r] != RES_SUB_NONE) {
							continue;
						}
						const float ex = resources_.x[r] - units_.pos_x[i], ez = resources_.z[r] - units_.pos_z[i];
						if (ex * ex + ez * ez < best) { best = ex * ex + ez * ez; alt = resources_.id[r]; }
					}
					if (alt) {
						order_gather(&units_.id[i], 1, alt);
					} else {
						units_.target_resource_id[i] = 0;
						units_.action_state[i] = ACTION_IDLE;
					}
					continue;
				}
				rg.slots_taken++;
			}
			float field_efficiency = 0.0f; // > 0 only while farming a field
			auto field_it = field_bld_by_res_.find(units_.target_resource_id[i]);
			if (field_it != field_bld_by_res_.end()) {
				auto bit = building_id_to_idx_.find(field_it->second);
				FieldGatherers &fg = field_gatherers_[units_.target_resource_id[i]];
				// Our own fields, and Gaia's: 0 A.D.'s ResourceGatherer has no owner check, so a map's
				// Gaia field (structures/*/field owned by Player 0) is anyone's to farm.
				bool own = bit != building_id_to_idx_.end() &&
						(buildings_[bit->second].team == uteam || buildings_[bit->second].team == GAIA_PLAYER);
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
			const float reach = fish ? FISH_GATHER_REACH : 2.5f;
			if (dx * dx + dz * dz < reach * reach) {
				units_.attack_timer[i] += dt;
				float harvest_rate = 0.75f;
				{
					// Technology gather-rate multipliers (ResourceGatherer/Rates/*).
					uint8_t rt = resources_.type[ridx];
					GatherKind kind = rt == RES_WOOD ? GK_WOOD : rt == RES_STONE ? GK_STONE : rt == RES_GOLD ? GK_METAL
						: units_.unit_type[i] == UNIT_FISHING_BOAT ? GK_FISH
						: resources_.subtype[ridx] == RES_SUB_GRAIN ? GK_GRAIN
						: resources_.subtype[ridx] == RES_SUB_MEAT ? GK_MEAT : GK_FRUIT;
					harvest_rate /= unit_tech_mods(uteam, units_.unit_type[i], units_.tmpl[i]).gather[kind] * ai_gather_mult(uteam);

					Civilization civ = civ_of(uteam);
					if ((rt == RES_GOLD || rt == RES_STONE) && civ_info(civ).metal_gather_mult > 1.0f) {
						harvest_rate /= civ_info(civ).metal_gather_mult;
					}
				}

				// unit_advanced.json / unit_elite.json: ResourceGatherer/BaseSpeed x0.7 per rank.
				for (uint8_t r = 2; r <= std::min<uint8_t>(units_.rank[i], 3); ++r) harvest_rate /= rank_bonus(r).gather_mul;

				// Cavalry butcher meat 5x as fast as infantry (template_unit_cavalry food.meat 5 vs 1).
				if (units_.unit_type[i] == UNIT_CAVALRY) harvest_rate *= 0.2f;

				// template_gaia_ruins: stone.ruins / metal.ruins gather at 2 against a rock's 0.5
				// (citizen soldiers) or 0.35 (women).
				if (resources_.subtype[ridx] == RES_SUB_RUINS)
					harvest_rate *= (units_.unit_type[i] == UNIT_FEMALE_CITIZEN ? 0.35f : 0.5f) / 2.0f;

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

	// Fish nobody is fishing regrow 1 food every 2 s, up to their supply (ResourceSupply Change/Spawn).
	const uint64_t regrow_ticks = (uint64_t)std::lround(FISH_REGROW_INTERVAL_S / FIXED_STEP);
	if (ticks_ % regrow_ticks == 0) {
		for (size_t r = 0; r < resources_.count; ++r) {
			if (resources_.subtype[r] != RES_SUB_FISH || !resources_.alive[r] || resources_.amount[r] <= 0) continue;
			if (resources_.amount[r] >= FISH_FOOD_SUPPLY || fish_gatherers_.count(resources_.id[r])) continue;
			resources_.amount[r] += 1;
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

// Foundation.js / Repairable.js CalculateBuildMultiplier: n builders at work
// each do n^0.7 / n of their rate (BuildTimeModifier / buildTimePenalty 0.7).
float Simulation::foundation_build_multiplier(int builders) {
	return builders < 2 ? 1.0f : std::pow((float)builders, 0.7f) / (float)builders;
}

// Builder.js GetRate: 1 for citizens; worker elephants build twice as fast.
float Simulation::builder_rate(size_t unit_idx) const {
	return units_.unit_type[unit_idx] == UNIT_WORKER_ELEPHANT ? 2.0f : 1.0f;
}

// Cost.js GetBuildTime for the builder's player: the template time after
// technologies, civ and team bonuses (Celtic Wooden Construction, ...) and the
// AI difficulty multiplier.
float Simulation::structure_build_time(const Building &b, uint8_t builder_team) const {
	const uint8_t type = b.type < BLD_TYPE_COUNT ? b.type : 0;
	float t = building_type_info(type).build_time * tech_mods(b.team).bld_build_time[type] * ai_time_mult(b.team);
	t *= civ_bonus_mult(civ_of(builder_team), CIV_BONUS_BUILDING_BUILD_TIME, b.type);
	t *= team_bonus_mult(builder_team, TEAM_BONUS_BUILDING_BUILD_TIME, -1, CIV_UNIT_TEMPLATE_NONE, b.type);
	return std::max(1.0f, t);
}

void Simulation::tick_construction(float dt) {
	// Builders at work on each foundation (Foundation.js AddBuilder: those in range).
	std::unordered_map<uint32_t, int> builders_per_bld;
	const size_t n = units_.count;
	for (size_t i = 0; i < n; ++i) {
		if (units_.alive[i] && units_.action_state[i] == ACTION_BUILD && units_.target_building_id[i] != 0) {
			auto bit = building_id_to_idx_.find(units_.target_building_id[i]);
			if (bit == building_id_to_idx_.end()) continue;
			const Building &b = buildings_[bit->second];
			if (b.build_progress < 1.0f && outside_and_in_build_range(b.x, b.z, b.half_size, units_.pos_x[i], units_.pos_z[i]))
				builders_per_bld[b.id]++;
		}
	}

	std::unordered_set<uint32_t> blocked_sites; // foundations found occupied this tick: one scan each
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
			// 0 A.D. Foundation.Commit: the first builder on a clear site starts it; until the
			// units standing on it have left, nobody builds.
			if (!b.committed) {
				if (!blocked_sites.empty() && blocked_sites.count(b.id)) continue;
				if (!try_commit_foundation(b)) { blocked_sites.insert(b.id); continue; }
			}
			// Foundation.js Build: each builder adds work x MaxHitpoints / BuildTime x
			// buildMultiplier HP, with buildMultiplier = n^0.7 / n for n builders at work
			// (BuildTimeModifier 0.7). Progress is the best health fraction reached, so
			// damage to a foundation has to be built back.
			const float hp_rate = b.max_hp / structure_build_time(b, units_.team[i]);
			const float work = foundation_build_multiplier(builders_per_bld[b.id]) * builder_rate(i);
			b.hp = std::min(b.max_hp, b.hp + work * hp_rate * dt);
			b.build_progress = std::min(1.0f, std::max(b.build_progress, b.hp / b.max_hp));
			if (b.build_progress >= 1.0f) {
				stat_building_constructed(b);
				b.hp = std::max(b.hp, b.max_hp);
				on_construction_finished(b, i); // 0 A.D. UnitAI ConstructionFinished
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

// UnitAI LeaveFoundation: unless it is already walking off, the unit puts its current
// order back on the front of its queue and walks to the nearest point clear of the
// footprint (PushOrderFront), then carries on with what it was doing.
void Simulation::leave_foundation(size_t i, const Building &b) {
	const float r = unit_collision_radius(units_.unit_type[i]);
	const float reach = b.half_size + r;
	auto outside = [&](float x, float z) {
		return std::fabs(x - b.x) >= reach || std::fabs(z - b.z) >= reach;
	};
	if (units_.order_kind[i] == ORDER_WALK && units_.has_order[i] && outside(units_.order_x[i], units_.order_z[i]))
		return;
	const float dx = units_.pos_x[i] - b.x, dz = units_.pos_z[i] - b.z;
	const float out = reach + 0.5f;
	float tx = units_.pos_x[i], tz = units_.pos_z[i];
	if (reach - std::fabs(dx) <= reach - std::fabs(dz)) tx = b.x + (dx < 0.0f ? -out : out);
	else tz = b.z + (dz < 0.0f ? -out : out);
	tx = std::clamp(tx, -map_half_ + 1.0f, map_half_ - 1.0f);
	tz = std::clamp(tz, -map_half_ + 1.0f, map_half_ - 1.0f);
	const uint32_t uid = units_.id[i];
	const UnitOrder cur = current_order(i);
	if (cur.kind != ORDER_NONE && cur.kind != ORDER_WALK) {
		std::vector<UnitOrder> &queue = order_queues_[uid];
		queue.insert(queue.begin(), cur);
	}
	const bool was = applying_queued_;
	applying_queued_ = true; // keep the queue: the order resumes once the unit is off the site
	cmd_move(&uid, 1, tx, tz);
	applying_queued_ = was;
	if (units_.order_kind[i] == ORDER_WALK) units_.order_flags[i] |= ORDER_FLAG_FORCE;
}

bool Simulation::try_commit_foundation(Building &b) {
	bool blocked = false;
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || unit_is_ship(units_.unit_type[i])) continue;
		// A unit stands on the site when its centre is inside the footprint (builders work
		// from just outside it, outside_and_in_build_range).
		if (std::fabs(units_.pos_x[i] - b.x) >= b.half_size || std::fabs(units_.pos_z[i] - b.z) >= b.half_size) continue;
		if (!turret_height_.empty() && turret_height_.count(units_.id[i])) continue;
		blocked = true;
		// WillMoveFromFoundation: only units allied to the foundation's owner make way.
		if (units_.team[i] < MAX_PLAYERS && is_ally(units_.team[i], b.team)) leave_foundation(i, b);
	}
	if (blocked) return false;
	b.committed = true;
	rebuild_nav_grid();
	return true;
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
				if (b.build_progress >= 1.0f && b.hp > 0.0f && b.hp < b.max_hp &&
						outside_and_in_build_range(b.x, b.z, b.half_size, units_.pos_x[i], units_.pos_z[i])) {
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
			// Repairable.js Repair: work x buildMultiplier (n^0.7 / n) x GetRepairRate,
			// GetRepairRate = MaxHitpoints / (RepairTimeRatio 2.0 x BuildTime).
			const float repair_rate = b.max_hp / (REPAIR_TIME_RATIO * structure_build_time(b, units_.team[i]));
			const float work = foundation_build_multiplier(repairers_per_bld[b.id]) * builder_rate(i);
			b.hp = std::min(b.max_hp, b.hp + work * repair_rate * dt);
			// Repairable sends ConstructionFinished too: the repairers move on to nearby work.
			if (b.hp >= b.max_hp) on_construction_finished(b, i);
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
					// An animal from the corral's Trainer list; it stays penned while there is room.
					float sx = b.x + 1.6f;
					float sz = b.z + 1.6f;
					const bool room = corral_count(b.id) < CORRAL_GARRISON_MAX;
					uint32_t fid = spawn_fauna(sx, sz, corral_animal_for(civ_of(b.team), otmpl));
					auto fit = fauna_id_to_idx_.find(fid);
					if (room && fit != fauna_id_to_idx_.end()) {
						size_t fidx = fit->second;
						fauna_.corral_id[fidx] = b.id;
						fauna_.behavior[fidx] = FAUNA_CORRALLED;
					}
				} else {
					// Spawn unit near building
					float sx = b.x + (b.footprint + 0.6f) * GRID;
					float sz = b.z;
					// Ships launch from the dock's water side (ProductionQueue.js FindSpawnPoint, ship passability).
					if (unit_is_ship(utype)) nearest_ship_cell(b, sx, sz);
					uint32_t uid = spawn_unit(sx, sz, bteam, (UnitType)utype, 0.0f, otmpl);
					if (int32_t ti = lookup_unit(uid); ti >= 0) stat_unit_trained((size_t)ti);

					// Route to Rally Point if set
					if (b.has_rally_point) apply_rally_point(uid, b);
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

// 0 A.D. Pack.js + UnitAI's pack rules. A packed siege engine that has its target in range
// unpacks before it fires; an unpacked one that has to move (a walk order, or a target out of
// range) packs first. An order that needs the other state cancels a pack in progress and
// resets it, as Pack.CancelPack does. move_units holds still anything not packed, and
// tick_combat only lets unpacked engines fire.
void Simulation::tick_pack(float dt) {
	const size_t n = units_.count;
	for (size_t i = 0; i < n; ++i) {
		uint8_t ps = units_.pack_state[i];
		if (ps == PACK_NONE || !units_.alive[i]) continue;
		const uint8_t st = units_.action_state[i];
		if (st == ACTION_GARRISONED || st == ACTION_DYING) continue;

		// Is the current target in weapon range?
		bool target_in_range = false;
		const float att_y = unit_height(i);
		if (units_.target_unit_id[i] != 0) {
			int32_t t = lookup_unit(units_.target_unit_id[i]);
			if (t >= 0 && units_.alive[t] && is_hostile(units_.team[i], units_.team[t])) {
				float dx = units_.pos_x[i] - units_.pos_x[t];
				float dz = units_.pos_z[i] - units_.pos_z[t];
				const float d2 = dx * dx + dz * dz;
				const float minr = units_.attack_range[i] * unit_min_range_ratio(units_.unit_type[i]);
				target_in_range = d2 >= minr * minr && in_parabolic_range(d2, att_y,
						unit_height(t), units_.attack_range[i]);
			}
		} else if (units_.target_building_id[i] != 0 && st != ACTION_BUILD) {
			auto bit = building_id_to_idx_.find(units_.target_building_id[i]);
			if (bit != building_id_to_idx_.end()) {
				const Building &b = buildings_[bit->second];
				if (is_hostile(units_.team[i], b.team)) {
					float dx = units_.pos_x[i] - b.x;
					float dz = units_.pos_z[i] - b.z;
					float eff_d = std::max(0.0f, sqrtf(dx * dx + dz * dz) - b.half_size);
					target_in_range = in_parabolic_range(eff_d * eff_d, att_y, get_terrain_height(b.x, b.z),
							units_.attack_range[i]);
				}
			}
		}

		// 0 = keep the current state, 1 = must be packed, 2 = must be unpacked
		int want = 0;
		if (target_in_range) want = 2;
		else if (units_.has_order[i]) want = 1;

		if (ps == PACK_PACKED && want == 2) {
			ps = PACK_UNPACKING;
			units_.pack_elapsed[i] = 0.0f;
		} else if (ps == PACK_UNPACKED && want == 1) {
			ps = PACK_PACKING;
			units_.pack_elapsed[i] = 0.0f;
		} else if (ps == PACK_UNPACKING && want == 1) {
			ps = PACK_PACKED; // g_OrdersCancelUnpacking: a walk cancels unpacking at once
			units_.pack_elapsed[i] = 0.0f;
		} else if (ps == PACK_PACKING && want == 2) {
			ps = PACK_UNPACKED; // EnsureCorrectPackStateForAttack: back in range, stop packing
			units_.pack_elapsed[i] = 0.0f;
		} else if (ps == PACK_PACKING || ps == PACK_UNPACKING) {
			units_.pack_elapsed[i] += dt;
			if (units_.pack_elapsed[i] >= pack_time_s(i)) {
				ps = (ps == PACK_PACKING) ? PACK_PACKED : PACK_UNPACKED;
				units_.pack_elapsed[i] = 0.0f;
			}
		}
		units_.pack_state[i] = ps;
	}
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

		// 0 A.D. UnitAI target choice, by stance (g_Stances) and order:
		//  - passive units never pick targets (an explicit attack order still stands);
		//  - workers and fleeing units only answer attacks (tick_unit_ai);
		//  - a walk ignores enemies it sees (INDIVIDUAL.WALKING has no LosRangeUpdate);
		//    only an unforced one answers attacks (respond_to_attack);
		//  - a target the unit picked itself is dropped once the stance leash says so
		//    (ShouldAbandonChase); a player's attack target never is.
		const uint8_t st = units_.action_state[i];
		const uint8_t stance = units_.stance[i];
		const bool explicit_target = units_.order_kind[i] == ORDER_ATTACK && units_.target_unit_id[i] != 0 &&
				units_.target_unit_id[i] == units_.order_target[i];
		if (stance == STANCE_PASSIVE && !explicit_target) return;
		// Walking to board a friendly ship or siege engine (0 A.D. Garrison order):
		// keep the order instead of dropping target_unit_id for want of an enemy.
		if (st == ACTION_MOVE && units_.target_unit_id[i] != 0) {
			const int32_t c = lookup_unit(units_.target_unit_id[i]);
			if (c >= 0 && units_.alive[c] && units_.team[c] == units_.team[i]) return;
		}
		if (st == ACTION_GATHER || st == ACTION_BUILD || st == ACTION_RETURN_DROPSITE || st == ACTION_TRADE ||
				st == ACTION_FLEE || st == ACTION_GARRISONED)
			return;
		if (st == ACTION_MOVE && (units_.order_kind[i] == ORDER_WALK || units_.order_kind[i] == ORDER_GARRISON))
			return;
		const bool guarding = st == ACTION_GUARD; // target_unit_id is the guarded unit
		const bool walk_and_fight = st == ACTION_ATTACK_MOVE || st == ACTION_PATROL;

		const float pxi = units_.pos_x[i];
		const float pzi = units_.pos_z[i];
		// The held position (UnitAI heldPosition): where the unit stands while it walks.
		const bool on_the_move = st == ACTION_MOVE || walk_and_fight || guarding;
		const float anc_x = on_the_move ? pxi : units_.anchor_x[i];
		const float anc_z = on_the_move ? pzi : units_.anchor_z[i];
		const float range = units_.attack_range[i];
		// Attack.js MinRange (catapults, bolt shooters): the range query skips anything closer.
		const float minr = range * unit_min_range_ratio(units_.unit_type[i]);
		const float vision = unit_vision_range(units_.unit_type[i]);
		const float held_lim = vision * 0.5f + range; // CheckTargetDistanceFromHeldPosition
		const float att_y = unit_height(i);
		// Would the stance let this unit (keep) fight j? (GetQueryRange / ShouldAbandonChase)
		auto within_stance = [&](int32_t j) {
			const float dx = units_.pos_x[j] - pxi, dz = units_.pos_z[j] - pzi;
			const float d2 = dx * dx + dz * dz;
			if (stance == STANCE_STAND_GROUND)
				return in_parabolic_range(d2, att_y, unit_height(j), range);
			if (stance == STANCE_DEFENSIVE) {
				const float hx = units_.pos_x[j] - anc_x, hz = units_.pos_z[j] - anc_z;
				return hx * hx + hz * hz < held_lim * held_lim;
			}
			return d2 <= vision * vision;
		};

		int32_t found = -1;
		// A. Existing unit target
		if (!guarding && units_.target_unit_id[i] != 0) {
			int32_t tidx = lookup_unit(units_.target_unit_id[i]);
			// Hunting (tick_hunters) or collecting a treasure (tick_treasures) drives it.
			if (tidx < 0 && (is_fauna_id(units_.target_unit_id[i]) || is_treasure_id(units_.target_unit_id[i]))) return;
			if (tidx >= 0 && units_.alive[tidx] && can_attack(units_.team[i], units_.team[tidx]) &&
					unit_can_target(i, (size_t)tidx) && (explicit_target || within_stance(tidx))) {
				const float tx = units_.pos_x[tidx] - pxi, tz = units_.pos_z[tidx] - pzi;
				// A target it picked itself that came too close is dropped; a player's
				// target stays and move_units backs away from it.
				if (explicit_target || tx * tx + tz * tz >= minr * minr) found = tidx;
			}
		}

		// B. Look for a target in range of the stance (GetQueryRange)
		int32_t found_bld = -1;
		if (found < 0 && units_.target_building_id[i] == 0 && stance != STANCE_PASSIVE &&
				((i + ticks_) % 4 == 0 || st == ACTION_ATTACK || walk_and_fight)) {
			const float scan_range = stance == STANCE_STAND_GROUND ? range
					: stance == STANCE_DEFENSIVE ? std::min(range + vision * 0.5f, vision) : vision;
			const uint8_t my_team = units_.team[i];
			const PrefProfile &my_prefs = unit_preferences(units_.tmpl[i], units_.unit_type[i]).prefs;
			uint16_t best_pref = 0x100; // above PREF_NONE: any target beats none
			float best_unit_d2 = 0.0f;

			grid.visit_radius(pxi, pzi, scan_range, [&](int32_t j) {
				if (!units_.alive[j] || !can_attack(my_team, units_.team[j])) return;
				if (!unit_can_target(i, (size_t)j)) return; // Attack.js RestrictedClasses
				float dx = pxi - units_.pos_x[j];
				float dz = pzi - units_.pos_z[j];
				float d2 = dx * dx + dz * dz;
				if (d2 > scan_range * scan_range || d2 < minr * minr) return;
				if (!within_stance(j)) return;

				// UnitAI AttackEntitiesByPreference: the best PreferredClasses match
				// (Attack.js GetPreference), then the closest; no match comes last.
				const uint8_t pref = preference_of(my_prefs, unit_preferences(units_.tmpl[j], units_.unit_type[j]).classes);
				if (pref < best_pref || (pref == best_pref && d2 < best_unit_d2)) {
					best_pref = pref;
					best_unit_d2 = d2;
					found = j;
				}
			});

			// Attack-move and patrol also take structures when no unit is about
			// (UnitAI FindWalkAndFightTargets, targetClasses Unit + Structure).
			if (found < 0 && walk_and_fight && !(units_.order_flags[i] & ORDER_FLAG_UNITS_ONLY)) {
				const bool capture = (units_.order_flags[i] & ORDER_FLAG_ALLOW_CAPTURE) &&
						unit_capture_strength(units_.unit_type[i]) > 0.0f;
				float best_d2 = vision * vision;
				for (size_t bi = 0; bi < buildings_.size(); ++bi) {
					const Building &b = buildings_[bi];
					if (b.wrecked || b.hp <= 0.0f || b.team >= MAX_PLAYERS) continue;
					if (!can_attack(my_team, b.team) && !(capture && can_capture_building(my_team, b))) continue;
					if (!capture && !unit_can_target_building(i, b.type)) continue;
					const float ex = std::max(0.0f, std::fabs(b.x - pxi) - b.half_size);
					const float ez = std::max(0.0f, std::fabs(b.z - pzi) - b.half_size);
					const float d2 = ex * ex + ez * ez;
					if (d2 < best_d2) { best_d2 = d2; found_bld = (int32_t)bi; }
				}
			}
		}

		if (found >= 0) {
			if (units_.target_unit_id[i] != units_.id[found] && !explicit_target)
				units_.order_flags[i] |= ORDER_FLAG_INTERRUPTED; // go back to the order afterwards
			units_.target_unit_id[i] = units_.id[found];
			units_.has_order[i] = stance != STANCE_STAND_GROUND;
			if (stance != STANCE_STAND_GROUND) {
				units_.target_x[i] = units_.pos_x[found];
				units_.target_z[i] = units_.pos_z[found];
			}
			if (st == ACTION_CHEER) units_.attack_timer[i] = 0.0f; // RespondToSightedEntities ends the cheer
			if (st != ACTION_ATTACK) {
				units_.waypoint_count[i] = 0; // chase directly, not along the walk path
				if (on_the_move) { units_.anchor_x[i] = pxi; units_.anchor_z[i] = pzi; }
			}
			units_.action_state[i] = ACTION_ATTACK;
		} else if (found_bld >= 0) {
			units_.order_flags[i] |= ORDER_FLAG_INTERRUPTED;
			units_.anchor_x[i] = pxi; units_.anchor_z[i] = pzi;
			units_.target_building_id[i] = buildings_[found_bld].id;
			units_.target_unit_id[i] = 0;
			units_.waypoint_count[i] = 0;
			units_.has_order[i] = 1;
			units_.action_state[i] = ACTION_ATTACK;
		} else if (!guarding && st == ACTION_ATTACK) {
			units_.target_unit_id[i] = 0; // fight over: tick_unit_ai sends the unit back to its order
		}
		target_idx[i] = found;
	});

	// 2. Unit vs Unit Attack Execution
	for (size_t i = 0; i < n; ++i) {
		int32_t t = target_idx[i];
		if (t < 0 || !units_.alive[i] || units_.attack_timer[i] > 0) continue;
		if (units_.attack_damage[i] <= 0.0f) continue;
		if (!pack_state_can_attack(units_.pack_state[i])) continue; // Pack.js: unpack before firing
		// Siege tower BuildingAI: DefaultArrowCount 0, one arrow per garrisoned Infantry.
		const int volley = is_siege_tower_at(i) ? siege_tower_arrows(i) : 1;
		if (volley <= 0) continue;
		float dx = units_.pos_x[i] - units_.pos_x[t];
		float dz = units_.pos_z[i] - units_.pos_z[t];
		float d2 = dx * dx + dz * dz;

		float att_y = unit_height(i);
		float def_y = unit_height(t);

		// 0 A.D. Exact Parabolic Range Equation (CCmpRangeManager.cpp)
		if (!in_parabolic_range(d2, att_y, def_y, units_.attack_range[i])) continue;
		const float minr = units_.attack_range[i] * unit_min_range_ratio(units_.unit_type[i]);
		if (d2 < minr * minr) continue; // Attack.js MinRange

		units_.attack_timer[i] = units_.attack_cooldown[i];
		units_.action_state[i] = ACTION_ATTACK;
		units_.attacked_by[t] = units_.id[i]; // UnitAI "Attacked", answered by tick_unit_ai

		// 0 A.D. Exact Exponential Armor Formula (Attack.js + Resistance.js):
		// Damage = sum( Dmg[Type] * 0.9^Armor[Type] )
		const auto &uinfo_att = unit_info_at(i);
		const auto &uinfo_dst = unit_info_at(t);
		float target_hack = uinfo_dst.hack_armor;
		float target_pierce = uinfo_dst.pierce_armor;
		float target_crush = uinfo_dst.crush_armor;

		// Researched armor tech bonuses
		if (units_.team[t] < MAX_PLAYERS && units_.unit_type[t] < UNIT_TYPE_COUNT) {
			const float *armor = unit_tech_mods(units_.team[t], units_.unit_type[t], units_.tmpl[t]).armor;
			target_hack += armor[0];
			target_pierce += armor[1];
			target_crush += armor[2];
		}

		// unit_advanced.json / unit_elite.json: melee units +1 resistance per rank
		if (const float ra = rank_melee_armor((size_t)t); ra > 0.0f) {
			target_hack += ra;
			target_pierce += ra;
			target_crush += ra;
		}

		// AuraManager defensive armor buff
		float armorAura = aura_mgr_.query_modifier(units_.pos_x[t], units_.pos_z[t], units_.team[t], AURA_ARMOR,
			units_.tmpl[t], &unit_combat_at(t).classes);
		if (armorAura != 0.0f) {
			target_hack += armorAura;
			target_pierce += armorAura;
		}

		float dmg_scale = (uinfo_att.attack_damage > 0.0f) ? (units_.attack_damage[i] / uinfo_att.attack_damage) : 1.0f;
		float hack_dmg = uinfo_att.hack_damage * dmg_scale;
		float pierce_dmg = uinfo_att.pierce_damage * dmg_scale;
		float crush_dmg = uinfo_att.crush_damage * dmg_scale;

		uint8_t att_team = units_.team[i];
		Civilization att_civ = civ_of(att_team);

		// 0 A.D. helpers/Attack.js GetAttackBonus: the attacker's template Bonuses
		// (spearmen x3 against cavalry and the like).
		uint8_t u_att = units_.unit_type[i];
		uint8_t u_def = units_.unit_type[t];
		const float bonus_mul = combat_bonus(unit_combat_at(i), unit_combat_at(t), civ_of(units_.team[t]));
		hack_dmg *= bonus_mul;
		pierce_dmg *= bonus_mul;
		crush_dmg *= bonus_mul;

		// Attack auras (hero bonuses, enemy carnyx and hero penalties); 0 = none in range
		float dmgMult = aura_mgr_.query_modifier(units_.pos_x[i], units_.pos_z[i], att_team, AURA_ATTACK,
			units_.tmpl[i], &unit_combat_at(i).classes);
		if (dmgMult > 0.0f) {
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

		// Height gives range only (in_parabolic_range above): 0 A.D. has no damage bonus for it.

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
			// 0 A.D. Attack.js PerformAttack: aim where the target will be when the
			// projectile lands (PredictTimeToTarget), then scatter the landing point by a
			// bivariate normal with Spread metres of deviation per 100 m of distance
			// (x0.8 advanced, x0.8 again elite). Whether it hits is decided where it
			// lands (tick_projectiles, DelayedDamage.js).
			float dist = sqrtf(d2);
			const float flight = dist / projectile_speed(proj_vis);
			const float aim_x = units_.pos_x[t] + units_.vel_x[t] * flight;
			const float aim_z = units_.pos_z[t] + units_.vel_z[t] * flight;
			const float sigma = unit_combat_at(i).spread * spread_rank_multiplier(units_.rank[i]) * dist / 100.0f;
			float launch_y = unit_height(i) + arrow_sy;
			float land_y = (unit_is_ship(units_.unit_type[t]) ? unit_height(t)
					: get_terrain_height(aim_x, aim_z) + unit_height_offset((size_t)t)) + 0.8f;
			for (int v = 0; v < volley; ++v) {
				float disp_x = 0.0f, disp_z = 0.0f;
				random_normal_2d(disp_x, disp_z);
				fire_arrow(units_.pos_x[i], launch_y, units_.pos_z[i],
				           aim_x + disp_x * sigma, land_y, aim_z + disp_z * sigma,
				           final_dmg, units_.team[i], units_.id[t], effect, 1, false, proj_vis, units_.id[i],
				           unit_splash(units_.tmpl[i], u_att));
			}

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
						float deck_y = unit_height(i) + 2.5f;
						float hit_y = unit_height(t) + 0.8f;
						fire_arrow(units_.pos_x[i] + sp, deck_y, units_.pos_z[i] + sp,
								   units_.pos_x[t], hit_y, units_.pos_z[t],
								   12.0f + p_bonus_dmg, units_.team[i], units_.id[t], 0, 1, false, 0, units_.id[i]);
					}
				}
			}
		} else {
			award_damage_xp(units_.id[i], (size_t)t, final_dmg);
			note_attacked(units_.id[i], units_.team[i], units_.id[t], units_.team[t], false, final_dmg);
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

			if (units_.hp[t] <= 0) {
				units_.alive[t] = 0;
				units_.action_state[t] = ACTION_DYING;
				note_kill(units_.id[i], units_.id[t]);
				record_unit_death(t, units_.team[i]);
				stat_unit_lost(t, units_.team[i]);
				if (units_.team[t] == 0) {
					record_.units_lost++;
				} else {
					record_.enemies_killed++;
				}

				// 0 A.D. Looter.js: template loot plus carried resources to the killer's owner
				collect_unit_loot(units_.id[i], units_.team[i], (size_t)t);

				// A sunk ship's passengers swim ashore or drown in tick_siege_garrisons.

				// Promotion.js xp came with each hit (award_damage_xp); count the kill.
				units_.kills[i]++;
			}
			// Attack.js PerformAttack: a melee Splash lands on the target's position
			// (DelayedDamage.js Hit), after the hit itself here.
			if (const uint8_t sp = unit_splash(units_.tmpl[i], u_att)) {
				cause_damage_over_area(sp, units_.id[i], units_.team[i], units_.pos_x[t], units_.pos_z[t],
						units_.pos_x[t] - units_.pos_x[i], units_.pos_z[t] - units_.pos_z[i]);
			}
		}
	}

	// 3. Units Attacking Buildings & Siege Demolition (0 A.D. Attack.js + BuildingDamage)
	// A unit allowed to capture uses its Capture attack (Attack.js Capture: MaxRange 4,
	// RepeatTime 1 s) on a structure it can capture; tick_capture spends the points.
	capture_attack_.assign(buildings_.size(), std::array<float, MAX_PLAYERS>{});
	for (size_t i = 0; i < n; ++i) {
		if (!units_.alive[i] || units_.target_building_id[i] == 0 || units_.attack_timer[i] > 0) continue;
		// Builders and repairers also carry target_building_id; leave them to tick_construction.
		if (units_.action_state[i] == ACTION_BUILD) continue;
		const float capture_strength = (units_.order_flags[i] & ORDER_FLAG_ALLOW_CAPTURE) ?
				unit_capture_attack_at(i) : 0.0f;
		if (units_.attack_damage[i] <= 0.0f && capture_strength <= 0.0f) continue;
		auto bit = building_id_to_idx_.find(units_.target_building_id[i]);
		if (bit == building_id_to_idx_.end()) {
			units_.target_building_id[i] = 0;
			units_.action_state[i] = ACTION_IDLE;
			continue;
		}
		Building &b = buildings_[bit->second];
		const bool capture = capture_strength > 0.0f && units_.team[i] < MAX_PLAYERS && can_capture_building(units_.team[i], b);
		if (!capture && (!can_attack(units_.team[i], b.team) || units_.attack_damage[i] <= 0.0f ||
				!unit_can_target_building(i, b.type))) { // dogs never attack structures, rams never fields
			units_.target_building_id[i] = 0;
			continue;
		}

		float dx = units_.pos_x[i] - b.x;
		float dz = units_.pos_z[i] - b.z;
		float d = sqrtf(dx * dx + dz * dz);
		// 0 A.D. ObstructionManager IsInTargetRange: the gap between the two obstructions,
		// so the attacker's own radius counts (melee units could not reach a wall otherwise).
		float eff_d = std::max(0.0f, d - b.half_size - unit_collision_radius(units_.unit_type[i]));

		float att_y = unit_height(i);
		float bld_y = get_terrain_height(b.x, b.z);

		const bool in_range = capture ? eff_d <= CAPTURE_RANGE
				: in_parabolic_range(eff_d * eff_d, att_y, bld_y, units_.attack_range[i]);
		if (!in_range) {
			// Move toward building if out of range
			stand_outside_rect(b.x, b.z, b.half_size, units_.id[i],
					units_.pos_x[i], units_.pos_z[i],
					&units_.target_x[i], &units_.target_z[i]);
			units_.has_order[i] = 1;
			units_.action_state[i] = ACTION_ATTACK;
			continue;
		}
		if (capture) {
			units_.attack_timer[i] = CAPTURE_REPEAT_S;
			units_.action_state[i] = ACTION_ATTACK;
			capture_attack_[bit->second][units_.team[i]] += capture_strength;
			if (b.team == 0) raise_alert(BattleAlert::ALERT_BUILDING_UNDER_ATTACK, b.x, b.z, b.id, b.team);
			continue;
		}
		if (!pack_state_can_attack(units_.pack_state[i])) continue; // Pack.js: unpack before firing

		units_.attack_timer[i] = units_.attack_cooldown[i];
		units_.action_state[i] = ACTION_ATTACK;

		const auto &uinfo_att = unit_info_at(i);
		float hack_dmg = uinfo_att.hack_damage;
		float pierce_dmg = uinfo_att.pierce_damage;
		float crush_dmg = uinfo_att.crush_damage;

		// Siege demolition power comes from 0 A.D.'s own crush damage (rams 150,
		// stone throwers 250 in their templates); no extra multiplier on top.

		// The structure template's own Resistance/Entity/Damage levels (a civic centre
		// is 25 hack, 35 pierce, 3 crush) and the attacker's Bonuses against it.
		const CombatProfile &b_combat = building_combat(b.type);
		const float b_bonus = combat_bonus(unit_combat_at(i), b_combat, civ_of(b.team));
		hack_dmg *= b_bonus;
		pierce_dmg *= b_bonus;
		crush_dmg *= b_bonus;
		const float b_hack_armor = b_combat.hack_armor;
		const float b_pierce_armor = b_combat.pierce_armor;
		const float b_crush_armor = b_combat.crush_armor;

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
			float launch_y = unit_height(i) + sy;
			float land_y = get_terrain_height(b.x, b.z) + 1.5f;
			fire_arrow(units_.pos_x[i], launch_y, units_.pos_z[i],
			           b.x, land_y, b.z,
			           bld_dmg, units_.team[i], b.id, EFFECT_BURNING, 2, true, vis, units_.id[i],
			           unit_splash(units_.tmpl[i], ut));
		} else {
			note_attacked(units_.id[i], units_.team[i], b.id, b.team, true, bld_dmg);
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

				collect_building_loot(units_.id[i], units_.team[i], b); // 0 A.D. Looter.js

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
				mark_building_wreck(b, units_.team[i]);
				units_.target_building_id[i] = 0;
				units_.action_state[i] = ACTION_IDLE;
				rebuild_nav_grid();
				if (b.type == BLD_TOWN_CENTER || b.type == BLD_FORTRESS || b.type == BLD_TOWER) {
					rebuild_territory();
				}
			}
			if (const uint8_t sp = unit_splash(units_.tmpl[i], units_.unit_type[i]))
				cause_damage_over_area(sp, units_.id[i], units_.team[i], b.x, b.z, -dx, -dz); // melee Splash
		}
	}

	// 4. Defensive Buildings & Parapet Walls attack (TC / Tower / Fortress / Wall + Garrison volley bonus)
	for (auto &b : buildings_) {
		if (b.build_progress < 1.0f || b.hp <= 0.0f) continue;
		const BuildingTypeInfo &binfo = building_type_info(b.type);
		bool is_wall = (b.type == BLD_WALL_SEGMENT) && !turret_points_of(b); // turret walls: the units shoot

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
					float unit_y = unit_height(i);
					if (in_parabolic_range(d2, wall_y, unit_y, range)) {
						hit = i;
					}
				});
				if (hit >= 0) {
					b.attack_timer = cd;
					for (int a = 0; a < total_arrows; ++a) {
						float spread = (a - total_arrows * 0.5f) * 0.22f;
						float hit_y = unit_height(hit) + 0.8f;
						fire_arrow(b.x + spread, wall_y, b.z + spread,
								   units_.pos_x[hit], hit_y, units_.pos_z[hit],
								   final_turret_dmg, b.team, units_.id[hit], 0, 1, false, 0, b.id);
					}
				}
			}
		}
	}

	// 2b. Health.js regeneration: RegenRate always, IdleRegenRate while the unit is idle
	// (technologies such as Battlefield Medicine). 0 A.D. adds it once a second; RTS0
	// adds the same rate every tick.
	for (size_t u = 0; u < n; ++u) {
		if (!units_.alive[u] || units_.hp[u] <= 0.0f || units_.hp[u] >= units_.max_hp[u]) continue;
		uint8_t owner = units_.team[u], t = units_.unit_type[u];
		if (owner >= MAX_PLAYERS || t >= UNIT_TYPE_COUNT) continue;
		const TechMods &m = tech_mods_[owner];
		float rate = m.regen[t] + (units_.action_state[u] == ACTION_IDLE ? m.idle_regen[t] : 0.0f);
		if (rate > 0.0f) units_.hp[u] = std::min(units_.max_hp[u], units_.hp[u] + dt * rate);
	}
	// A negative template RegenRate (fire ships) wears the unit down, even at full health.
	for (size_t u = 0; u < n; ++u) {
		if (!units_.alive[u] || units_.hp[u] <= 0.0f) continue;
		const float burn = unit_template_regen(u);
		if (burn >= 0.0f) continue;
		units_.hp[u] += dt * burn;
		if (units_.hp[u] > 0.0f) continue;
		units_.hp[u] = 0.0f;
		units_.alive[u] = 0;
		units_.action_state[u] = ACTION_DYING;
		stat_unit_lost(u, units_.team[u]);
		record_unit_death(u, units_.team[u]);
	}

	// 3. Temple + Hero Healing Aura via AuraManager (Heal.js + Auras.js) - 18m 3.5 / 16m 2.0
	for (size_t u = 0; u < n; ++u) {
		if (!units_.alive[u] || units_.hp[u] >= units_.max_hp[u]) continue;
		float healMod = aura_mgr_.query_modifier(units_.pos_x[u], units_.pos_z[u], units_.team[u], AURA_HEAL,
			units_.tmpl[u], &unit_combat_at(u).classes);
		if (healMod > 0.001f) {
			units_.hp[u] = std::min(units_.max_hp[u], units_.hp[u] + dt * healMod);
		}
	}

	// 3b. Healers heal through UnitAI's HEAL order (tick_unit_ai -> tick_heal).

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
				if (rel.res_type < RES_COUNT && units_.team[u] < MAX_PLAYERS) {
					player(units_.team[u]).res[rel.res_type] += (float)rel.amount;
					// TreasureCollector.js: a treasure counts as collected and as gathered.
					stats_[units_.team[u]].treasures_collected++;
					stat_gathered(units_.team[u], rel.res_type, rel.amount, false);
				}
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

// ---------------------------------------------------------------------------
// 0 A.D. Promotion.js: a unit with a Promotion component promotes once its xp reaches
// RequiredXp, keeping the xp left over (and promoting again while that still covers
// the next rank). Ranks come from the _b -> _a -> _e templates, so RTS0 keeps one
// roster template and applies unit_advanced.json / unit_elite.json itself; a chain that
// ends in another unit (Roman soldiers -> Marian Legionary) swaps the template.
// Data: promotion_data.h (tools/extract_0ad_promotion.py).
// ---------------------------------------------------------------------------

// Cost/BuildTime x1.2 per rank a unit is trained at (Roman Army Camp veterans, Kushite priests).
float Simulation::promotion_train_time_mult(uint16_t tmpl) {
	float m = 1.0f;
	for (uint8_t r = 2; r <= promotion_start_rank(tmpl); ++r) m *= rank_bonus(r).train_time_mul;
	return m;
}

float Simulation::promotion_required_xp(size_t i) const {
	if (i >= units_.count) return -1.0f;
	const PromotionLink *l = promotion_link(units_.tmpl[i], units_.unit_type[i], units_.rank[i]);
	if (!l) return -1.0f;
	float xp = l->xp;
	const uint8_t team = units_.team[i];
	if (team < MAX_PLAYERS) {
		const uint64_t cls = unit_classes(units_.tmpl[i], units_.unit_type[i]);
		for (const PromotionXpTech &t : PROMOTION_XP_TECHS)
			if (players_[team].has_tech(t.tech) && (cls & t.need) == t.need && !(cls & t.forbid))
				xp = t.replace_zero ? 0.0f : xp * t.multiply;
	}
	return xp;
}

float Simulation::rank_melee_armor(size_t i) const {
	if (units_.rank[i] < 2 || !(unit_classes(units_.tmpl[i], units_.unit_type[i]) & TC_MELEE)) return 0.0f;
	float a = 0.0f;
	for (uint8_t r = 2; r <= std::min<uint8_t>(units_.rank[i], 3); ++r) a += rank_bonus(r).melee_armor_add;
	return a;
}

float Simulation::unit_capture_attack_at(size_t i) const {
	float c = unit_capture_attack(units_.tmpl[i], units_.unit_type[i]);
	if (c > 0.0f)
		for (uint8_t r = 2; r <= std::min<uint8_t>(units_.rank[i], 3); ++r) c += rank_bonus(r).capture_add;
	return c;
}

float Simulation::healer_range_of(size_t h) const {
	float range = healer_range(units_.team[h]);
	for (uint8_t r = 2; r <= std::min<uint8_t>(units_.rank[h], 3); ++r) range += rank_bonus(r).heal_range_add;
	return range;
}

float Simulation::heal_amount_of(size_t h) const {
	float hp = HEAL_HP;
	for (uint8_t r = 2; r <= std::min<uint8_t>(units_.rank[h], 3); ++r) hp += rank_bonus(r).heal_add;
	return hp;
}

// Health/Max x1.25 and, for melee units, Attack/Melee damage x1.2 for each rank gained.
void Simulation::apply_rank_bonus(size_t i, uint8_t from_rank, uint8_t to_rank) {
	const bool melee = unit_classes(units_.tmpl[i], units_.unit_type[i]) & TC_MELEE;
	for (uint8_t r = std::max<uint8_t>(from_rank, 1) + 1; r <= std::min<uint8_t>(to_rank, 3); ++r) {
		const RankBonus &b = rank_bonus(r);
		units_.max_hp[i] *= b.health_mul;
		if (melee) units_.attack_damage[i] *= b.melee_damage_mul;
	}
}

// helpers/Transform.js ChangeEntityTemplate: the promoted unit keeps its health fraction.
void Simulation::promote_unit(size_t i, uint16_t target_tmpl, uint8_t target_rank) {
	const float frac = units_.max_hp[i] > 0.0f ? units_.hp[i] / units_.max_hp[i] : 1.0f;
	if (target_tmpl < CIV_UNIT_TEMPLATE_COUNT && target_tmpl != units_.tmpl[i]) {
		// Another unit: its template's stats, the owner's technologies, then its rank.
		const uint8_t type = civ_unit_template(target_tmpl).unit_type;
		const UnitTypeInfo &info = civ_template_unit_info(target_tmpl, type);
		units_.tmpl[i] = target_tmpl;
		units_.unit_type[i] = type;
		units_.max_hp[i] = info.hp;
		units_.speed[i] = info.speed;
		units_.attack_range[i] = info.attack_range;
		units_.attack_damage[i] = info.attack_damage;
		units_.attack_cooldown[i] = info.attack_cooldown;
		units_.carry_capacity[i] = info.carry_capacity;
		apply_tech_mods_to_unit(i);
		apply_rank_bonus(i, 1, target_rank);
	} else {
		apply_rank_bonus(i, units_.rank[i], target_rank);
	}
	units_.rank[i] = target_rank;
	units_.hp[i] = frac * units_.max_hp[i];
}

void Simulation::promote_if_due(size_t i) {
	// Promote: a dead unit does not change template (garrisoned units are not alive here).
	if (!units_.alive[i] && units_.action_state[i] != ACTION_GARRISONED) return;
	if (!(units_.hp[i] > 0.0f)) return;
	for (int guard = 0; guard < 4; ++guard) {
		const float need = promotion_required_xp(i);
		if (need < 0.0f) return;
		const float have = (float)units_.xp[i] + units_.xp_partial[i];
		if (have < need) return;
		const float left = have - need;
		units_.xp[i] = (uint16_t)std::min(65535.0f, std::floor(left));
		units_.xp_partial[i] = left - std::floor(left);
		const PromotionLink *l = promotion_link(units_.tmpl[i], units_.unit_type[i], units_.rank[i]);
		promote_unit(i, l->target, l->target_rank);
	}
}

// Promotion/TrickleRate: units garrisoned in a barracks, stable or elephant stable gain
// 1 xp a second (data/auras/structures/xp_trickle.json "Rigorous Training").
void Simulation::tick_promotion(float dt) {
	for (const Building &b : buildings_) {
		if (b.garrisoned_units.empty() || b.build_progress < 1.0f) continue;
		if (b.type != BLD_BARRACKS && b.type != BLD_STABLE && b.type != BLD_ELEPHANT_STABLE) continue;
		for (uint32_t uid : b.garrisoned_units) {
			const int32_t u = lookup_unit(uid);
			if (u < 0 || !promotion_link(units_.tmpl[u], units_.unit_type[u], units_.rank[u])) continue;
			gain_xp((size_t)u, dt);
			promote_if_due((size_t)u);
		}
	}
	const size_t n = units_.count;
	for (size_t i = 0; i < n; ++i)
		if (units_.alive[i]) promote_if_due(i);
}

void Simulation::tick_capture(float dt) {
	// 0 A.D. Capturable.js:93-132 (Reduce), 168-181 (RegisterCapturePointsChanged),
	// 183-201 (GetRegenRate & GarrisonRegenRate), 203-247 (TimerTick)
	// Multi-Team Structure Capture Mechanics:
	// CapturePoints is an array whose sum equals max_capture_points.
	// Attacking units within reach reduce enemy capture pools and transfer points to capturer.
	// Owner regenerates capture points back when unattacked; structure flips ownership when
	// a team crosses the majority threshold (>50% max) or owner points hit 0.
	// Count and strength of the capturers (alive, teams 0-3) within 11 of each building, per team.
	// Units go in the outer loop, so each building's strength sums in unit order. A grid of building
	// reach (11, plus 1 to spare) limits each unit to the buildings near its cell.
	auto tally_capturers = [this] {
		const float half = map_half_;
		constexpr float CELL = 8.0f, REACH = 12.0f;
		const int W = std::max(1, (int)std::ceil(2.0f * half / CELL) + 1);
		auto cell_of = [half, W](float v) { // monotonic in v, so nearby units can't fall outside a building's cells
			const float f = (v + half) / CELL;
			return f >= 0.0f ? (f < (float)W ? (int)f : W - 1) : 0;
		};
		capture_cell_start_.assign((size_t)W * W + 1, 0);
		for (int pass = 0; pass < 2; ++pass) {
			for (uint32_t i = 0; i < (uint32_t)buildings_.size(); ++i) {
				const Building &b = buildings_[i];
				const int x0 = cell_of(b.x - REACH), x1 = cell_of(b.x + REACH);
				const int z0 = cell_of(b.z - REACH), z1 = cell_of(b.z + REACH);
				for (int cz = z0; cz <= z1; ++cz)
					for (int cx = x0; cx <= x1; ++cx) {
						if (pass == 0) capture_cell_start_[(size_t)cz * W + cx + 1]++;
						else capture_cell_buildings_[capture_cell_start_[(size_t)cz * W + cx]++] = CaptureReach{i, b.x, b.z};
					}
			}
			if (pass == 0) {
				for (size_t c = 1; c < capture_cell_start_.size(); ++c) capture_cell_start_[c] += capture_cell_start_[c - 1];
				capture_cell_buildings_.resize(capture_cell_start_.back());
			} else {
				// Filling advanced each cell's start to the next cell's; shift back.
				for (size_t c = capture_cell_start_.size() - 1; c > 0; --c) capture_cell_start_[c] = capture_cell_start_[c - 1];
				capture_cell_start_[0] = 0;
			}
		}
		capture_tally_.assign(buildings_.size(), CaptureTally{});
		for (size_t u = 0; u < units_.count; ++u) {
			if (!units_.alive[u] || units_.team[u] >= MAX_PLAYERS) continue;
			const size_t c = (size_t)cell_of(units_.pos_z[u]) * W + cell_of(units_.pos_x[u]);
			for (uint32_t k = capture_cell_start_[c]; k < capture_cell_start_[c + 1]; ++k) {
				const CaptureReach &b = capture_cell_buildings_[k];
				const float dx = units_.pos_x[u] - b.x;
				const float dz = units_.pos_z[u] - b.z;
				if (dx * dx + dz * dz <= 11.0f * 11.0f) {
					CaptureTally &t = capture_tally_[b.building];
					t.count[units_.team[u]]++;
					t.strength[units_.team[u]] += unit_capture_strength(units_.unit_type[u]);
				}
			}
		}
	};
	tally_capturers();
	if (capture_attack_.size() != buildings_.size()) capture_attack_.resize(buildings_.size(), std::array<float, MAX_PLAYERS>{});
	for (size_t bi = 0; bi < buildings_.size(); ++bi) {
		Building &b = buildings_[bi];
		if (b.build_progress < 1.0f || b.max_capture_points <= 0.0f) continue;
		if (b.team >= MAX_PLAYERS) {
			// TerritoryDecay: a Gaia structure in a player's connected territory decays to that player.
			std::array<float, MAX_PLAYERS> decay_to;
			float decay_gaia = 0.0f;
			if (territory_decay_targets(b, decay_to, decay_gaia)) {
				const float held = std::accumulate(b.capture_points_by_team.begin(), b.capture_points_by_team.end(), 0.0f);
				const float decay = std::min(b.territory_decay_rate * dt, std::max(0.0f, b.max_capture_points - held));
				for (int p = 0; p < MAX_PLAYERS; ++p) b.capture_points_by_team[p] += decay * decay_to[p];
			}
			capture_points_tick(b, bi, dt);
			b.capture_points = std::max(0.0f, b.max_capture_points -
					std::accumulate(b.capture_points_by_team.begin(), b.capture_points_by_team.end(), 0.0f));
			continue;
		}
		// Points no player holds are Gaia's (territory decay), so only more than the maximum is invalid.
		if (std::accumulate(b.capture_points_by_team.begin(), b.capture_points_by_team.end(), 0.0f) > b.max_capture_points + 0.01f) {
			b.capture_points_by_team.fill(0.0f);
			b.capture_points_by_team[b.team < 8 ? b.team : 0] = b.max_capture_points;
		}
		const int pl = b.team; // the owner, any of the 8 players
		// TerritoryDecay.js + Capturable.js TimerTick: a structure in enemy territory, or in its
		// owner's territory cut off from every civic centre, loses DecayRate points a second to
		// the territory's owner, to the connected neighbours of its region, or to Gaia.
		std::array<float, MAX_PLAYERS> decay_to;
		float decay_gaia = 0.0f;
		if (territory_decay_targets(b, decay_to, decay_gaia)) {
			const float decay = std::min(b.territory_decay_rate * dt, b.capture_points_by_team[pl]);
			b.capture_points_by_team[pl] -= decay;
			for (int p = 0; p < MAX_PLAYERS; ++p) b.capture_points_by_team[p] += decay * decay_to[p];
			// decay * decay_gaia stays unheld: Gaia's.
		}
		capture_points_tick(b, bi, dt);
		b.capture_points = b.team < MAX_PLAYERS ? b.capture_points_by_team[b.team] : std::max(0.0f, b.max_capture_points -
				std::accumulate(b.capture_points_by_team.begin(), b.capture_points_by_team.end(), 0.0f));
	}
	capture_attack_.clear(); // spent
}

// 0 A.D. Capturable.js for one structure, owned by a player or by Gaia. Pools 0-7 are
// the players' points, pool 8 is Gaia's (whatever no player holds). Reduce takes the
// points of the source's enemies, evenly, and hands them to the source; the owner
// regenerates RegenRate plus GarrisonRegenRate per garrisoned unit's Capture attack
// (GetRegenRate) from its enemies; the structure changes hands once its owner holds
// no points left, to whoever holds most (RegisterCapturePointsChanged).
void Simulation::capture_points_tick(Building &b, size_t bi, float dt) {
	constexpr int GAIA = MAX_PLAYERS; // pool index
	const bool gaia_owned = b.team >= MAX_PLAYERS;
	int owner = gaia_owned ? GAIA : b.team;
	std::array<float, MAX_PLAYERS + 1> pts{};
	float held = 0.0f;
	for (int t = 0; t < MAX_PLAYERS; ++t) { pts[t] = b.capture_points_by_team[t]; held += pts[t]; }
	pts[GAIA] = std::max(0.0f, b.max_capture_points - held);
	auto enemy = [&](int source, int t) {
		if (source == t) return false;
		if (source == GAIA || t == GAIA) return true; // everyone is Gaia's enemy
		return can_attack((uint8_t)source, (uint8_t)t);
	};
	auto reduce = [&](int to, float amount) {
		if (amount <= 0.0f) return;
		float before = 0.0f;
		for (int t = 0; t <= GAIA; ++t) if (t != to) before += pts[t];
		int pools = 0;
		for (int t = 0; t <= GAIA; ++t) if (pts[t] > 0.0f && enemy(to, t)) pools++;
		if (pools == 0) return;
		float removed = 0.0f;
		float share = amount / (float)pools;
		while (share > 0.0001f) {
			pools = 0;
			for (int t = 0; t <= GAIA; ++t) {
				if (pts[t] <= 0.0f || !enemy(to, t)) continue;
				if (pts[t] > share) { removed += share; pts[t] -= share; ++pools; }
				else { removed += pts[t]; pts[t] = 0.0f; }
			}
			share = pools ? (amount - removed) / (float)pools : 0.0f;
		}
		float after = 0.0f;
		for (int t = 0; t <= GAIA; ++t) if (t != to) after += pts[t];
		pts[to] += before - after;
	};
	// RegisterCapturePointsChanged: the owner left with no points loses the structure to
	// whoever holds most, at once (before it can regenerate).
	auto check_owner = [&]() {
		if (pts[owner] > 0.0001f) return false;
		int best = 0;
		for (int t = 1; t <= GAIA; ++t) if (pts[t] > pts[best]) best = t;
		if (best == owner) return false;
		b.has_rally_point = false; // RallyPoint.OnOwnershipChanged -> Reset
		order_ungarrison(b.id);
		cancel_upgrade(b, b.team);
		const uint8_t from = b.team;
		b.team = best == GAIA ? GAIA_PLAYER : (uint8_t)best;
		stat_building_captured(b, from);
		owner = best;
		raise_alert(BattleAlert::ALERT_STRUCTURE_CAPTURED, b.x, b.z, b.id, b.team);
		if (b.type == BLD_TOWN_CENTER || b.type == BLD_FORTRESS || b.type == BLD_TOWER) rebuild_territory();
		return true;
	};
	check_owner(); // territory decay (tick_capture) may have emptied the owner
	// Capture attacks that landed this tick (tick_combat), in player order.
	if (bi < capture_attack_.size())
		for (int p = 0; p < MAX_PLAYERS; ++p)
			if (capture_attack_[bi][p] > 0.0f) { reduce(p, capture_attack_[bi][p]); check_owner(); }
	// GetRegenRate: RegenRate plus GarrisonRegenRate x each garrisoned unit's Capture attack.
	float regen = b.capture_regen_rate;
	for (uint32_t uid : b.garrisoned_units) {
		const int32_t u = lookup_unit(uid);
		if (u >= 0 && units_.action_state[u] == ACTION_GARRISONED)
			regen += b.capture_garrison_regen_rate * unit_capture_attack_at((size_t)u);
	}
	if (regen > 0.0f && pts[owner] < b.max_capture_points) reduce(owner, regen * dt);
	check_owner();
	for (int t = 0; t < MAX_PLAYERS; ++t) b.capture_points_by_team[t] = pts[t];
}

// 0 A.D. DelayedDamage.js Hit: a unit's projectile damages its target only if it lands
// inside the target's footprint (PositionHelper.TestCollision); otherwise the nearest
// enemy whose footprint holds the landing point takes it, and
// failing that it misses. Structure arrows (BuildingAI, the buildings thread's) still
// strike their target. Returns the unit hit, or -1.
int32_t Simulation::projectile_hit_unit(const Projectile &p) const {
	const int32_t target = lookup_unit(p.target_unit_id);
	if (p.source_id != 0 && building_id_to_idx_.count(p.source_id)) return target;
	auto inside = [&](int32_t u) {
		const float dx = units_.pos_x[u] - p.target_x, dz = units_.pos_z[u] - p.target_z;
		const float r = combat_footprint(unit_combat_at((size_t)u));
		return dx * dx + dz * dz < r * r;
	};
	if (target >= 0 && units_.alive[target] && inside(target)) return target;
	int32_t best = -1;
	float best_d2 = 0.0f;
	for (size_t u = 0; u < units_.count; ++u) {
		if (!units_.alive[u] || units_.action_state[u] == ACTION_GARRISONED || !is_hostile(p.team, units_.team[u]))
			continue;
		// EntitiesNearPoint measures to the obstruction, so an entity whose footprint
		// holds the point is always within MISSILE_HIT_RADIUS of it.
		if (!inside((int32_t)u)) continue;
		const float dx = units_.pos_x[u] - p.target_x, dz = units_.pos_z[u] - p.target_z;
		const float d2 = dx * dx + dz * dz;
		if (best >= 0 && d2 >= best_d2) continue;
		best = (int32_t)u;
		best_d2 = d2;
	}
	return best;
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
				int32_t tidx = projectile_hit_unit(p);
				if (tidx >= 0 && units_.alive[tidx]) {
					award_damage_xp(p.source_id, (size_t)tidx, p.damage);
					note_attacked(p.source_id, p.team, units_.id[tidx], units_.team[tidx], false, p.damage);
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
						note_kill(p.source_id, units_.id[tidx]);
						record_unit_death(tidx, p.team);
						stat_unit_lost(tidx, p.team);
						if (units_.team[tidx] == 0) {
							record_.units_lost++;
						} else {
							record_.enemies_killed++;
						}
						collect_unit_loot(p.source_id, p.team, (size_t)tidx);
					}
				}
			} else if (p.target_building_id != 0) {
				auto bit = building_id_to_idx_.find(p.target_building_id);
				if (bit != building_id_to_idx_.end()) {
					Building &b = buildings_[bit->second];
					if (!b.wrecked && b.hp > 0.0f) {
						note_attacked(p.source_id, p.team, b.id, b.team, true, p.damage);
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
							collect_building_loot(p.source_id, p.team, b);
							mark_building_wreck(b, p.team);
							rebuild_nav_grid();
							if (b.type == BLD_TOWN_CENTER || b.type == BLD_FORTRESS || b.type == BLD_TOWER)
								rebuild_territory();
						}
					}
				}
			}

			// DelayedDamage.js Hit: the attack's Splash where the projectile lands.
			if (p.splash != 0)
				cause_damage_over_area(p.splash, p.source_id, p.team, p.target_x, p.target_z, dx, dz);
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
				record_unit_death(i, (uint8_t)(1 - units_.team[i]));
				stat_unit_lost(i, 0xFF); // the effect's source is not kept
				if (units_.team[i] == 0) record_.units_lost++;
				else record_.enemies_killed++;
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
				record_unit_death(i, (uint8_t)(1 - units_.team[i]));
				stat_unit_lost(i, 0xFF); // the effect's source is not kept
				if (units_.team[i] == 0) record_.units_lost++;
				else record_.enemies_killed++;
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
				record_unit_death(i, (uint8_t)(1 - units_.team[i]));
				stat_unit_lost(i, 0xFF); // the effect's source is not kept
				if (units_.team[i] == 0) record_.units_lost++;
				else record_.enemies_killed++;
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

	// Structures below a third of their health show fire (is_burning) but, as in
	// 0 A.D., take no damage from it.
}

void Simulation::tick_barter(float dt) {
	// 0 A.D. Barter.js ProgressTimeout: every 5 s each price difference moves
	// back toward 0 by at most 0.5%. The timer runs only while a difference is
	// left, so it starts with the first deal as in Barter.js.
	bool any = false;
	for (float d : barter_price_diff_) any |= d != 0.0f;
	if (!any) { barter_timer_ = 0.0f; return; }
	barter_timer_ += dt;
	if (barter_timer_ < BARTER_RESTORE_INTERVAL) return;
	barter_timer_ -= BARTER_RESTORE_INTERVAL;
	for (float &d : barter_price_diff_)
		d -= std::clamp(d, -BARTER_DIFFERENCE_RESTORE, BARTER_DIFFERENCE_RESTORE);
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
	if (ticks_ == 0) {
		if (disable_treasures_) clear_treasures();
		if (nomad_start_) spawn_nomad_treasures();
		nomad_start_ = false;
	}
	if (victory_condition_ == VICTORY_REGICIDE && ticks_ == 0) spawn_regicide_heroes();
	if (victory_condition_ == VICTORY_RELIC && ticks_ == 0 && relic_count_ > 0) apply_relic_count();

	// The match ends with these players as the winners; everyone else has lost.
	auto end_match = [&](uint8_t winners) {
		winners &= (uint8_t)~defeated_mask_; // a defeated ally stays defeated (0 A.D.)
		winner_mask_ = winners;
		convert_to_gaia((uint8_t)(participant_mask_ & ~winners & ~defeated_mask_));
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
			if (wonder_hold_[t] >= wonder_duration_ && (holder < 0 || wonder_hold_[t] > wonder_hold_[holder])) holder = t;
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
				if (relic_victory_timer_ >= relic_duration_) {
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
	uint8_t fallen = 0;
	for (int t = 0; t < MAX_TEAMS; ++t) {
		if (!((participant_mask_ >> t) & 1) || is_defeated((uint8_t)t)) continue;
		if (defeated(t) || (waves_enabled_ && t == 0 && tc_count[0] == 0)) fallen |= (uint8_t)(1u << t);
	}
	if (fallen) {
		convert_to_gaia(fallen);
		defeated_mask_ |= fallen;
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

void Simulation::convert_to_gaia(uint8_t players) {
	if (!players) return;
	auto out = [players](uint8_t t) { return t < MAX_PLAYERS && ((players >> t) & 1); };
	for (size_t u = 0; u < units_.count; ++u) {
		if (!out(units_.team[u])) continue;
		units_.team[u] = GAIA_PLAYER;
		units_.has_order[u] = 0;
		units_.target_unit_id[u] = 0;
		units_.target_building_id[u] = 0;
		units_.target_resource_id[u] = 0;
		if (units_.action_state[u] != ACTION_GARRISONED) units_.action_state[u] = ACTION_IDLE;
		trader_routes_.erase(units_.id[u]);
		trader_leg_.erase(units_.id[u]);
	}
	bool roots = false;
	for (auto &b : buildings_) {
		if (!out(b.team)) continue;
		cancel_upgrade(b, b.team);
		b.queue.clear();
		b.research_tech = TECH_NONE;
		b.research_timer = 0.0f;
		b.has_rally_point = false;
		b.team = GAIA_PLAYER;
		b.capture_points_by_team.fill(0.0f); // every point is Gaia's
		b.capture_points = 0.0f;
		roots |= b.type == BLD_TOWN_CENTER || b.type == BLD_FORTRESS || b.type == BLD_TOWER;
	}
	if (roots) rebuild_territory();
}

void Simulation::apply_relic_count() {
	// CaptureTheRelic.js "relicCount": the map's relics are cut down to the setting, or
	// topped up with copies turned about the map centre (or on a ring if it has none).
	const size_t want = (size_t)relic_count_;
	if (relics_.size() > want) relics_.resize(want);
	const size_t have = relics_.size();
	const size_t rounds = have ? (want - std::min(want, have) + have - 1) / have : 0;
	for (size_t k = 0; relics_.size() < want; ++k) {
		float x, z;
		if (have > 0) {
			// Round r turns every original relic by 2*pi*(r+1)/(rounds+1): one extra round is
			// the mirror through the centre, and no copy lands back on an original.
			const TreasureRelic src = relics_[k % have];
			const float turn = 6.28318530718f * (float)(k / have + 1) / (float)(rounds + 1);
			x = src.x * cosf(turn) - src.z * sinf(turn);
			z = src.x * sinf(turn) + src.z * cosf(turn);
		} else {
			const float a = 6.28318530718f * (float)k / (float)want;
			x = cosf(a) * map_half_ * 0.45f;
			z = sinf(a) * map_half_ * 0.45f;
		}
		spawn_relic(x, z, RES_GOLD, 250);
	}
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
		regicide_hero_[s.first] = spawn_unit(s.second.first + 4.0f, s.second.second + 4.0f, s.first, UNIT_HERO, 0.0f);
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
	v.pack_state = units_.pack_state;
	v.pack_elapsed = units_.pack_elapsed;
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
	(void)dt;
	// 0 A.D. ResourceTrickle.js: each trickling entity adds its Rates to its owner every
	// Interval. RTS0 times every interval off the match clock, so payouts land on the same
	// ticks for every peer and need no saved timer.
	const uint64_t elapsed = ticks_ + 1;
	auto due = [&](float interval_s) {
		const uint64_t n = (uint64_t)std::lround(interval_s / FIXED_STEP);
		return n > 0 && elapsed % n == 0;
	};

	// template_player.xml: Interval 1000, Rates 0, raised by the Ptolemaic team bonus
	// (ptol_player_teambonus.json, Breadbasket of the Mediterranean: +1 food for the
	// Ptolemies and their mutual allies; only players who own a building get it) and by
	// technologies that affect "Player" (Serapis Cult: +2 metal). Their trickle effects
	// are the unfiltered ones (tools/extract_0ad_techs.py).
	static constexpr uint8_t kTrickleRes[4] = {RES_FOOD, RES_WOOD, RES_STONE, RES_GOLD};
	auto is_player_trickle = [](const TechEffect &e) {
		return e.stat >= TSTAT_TRICKLE_FOOD && e.stat <= TSTAT_TRICKLE_METAL && e.affects_count == 0;
	};
	if (due(1.0f)) {
		for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
			PlayerState &ps = players_[p];
			if (is_defeated(p)) continue;
			for (uint8_t id = 1; id < TECH_COUNT; ++id) {
				if (!ps.has_tech(id)) continue;
				const TechInfo &ti = tech_info(id);
				const TechEffect *fx = tech_effects_table() + ti.effect_begin;
				for (uint16_t k = 0; k < ti.effect_count; ++k)
					if (is_player_trickle(fx[k]) && fx[k].op == TOP_ADD)
						ps.res[kTrickleRes[fx[k].stat - TSTAT_TRICKLE_FOOD]] += fx[k].value;
			}
		}
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
				if (add > 0.0f) players_[p].food() += add;
			}
		}
	}

	// Structures, every 2 s (their templates' Interval 2000): template_structure_wonder.xml
	// 1 food, wood, stone and metal; structures/han/ministry.xml 0.1 wood, stone and metal;
	// structures/pers/ice_house.xml 1 food. Technologies add to a structure's Rates
	// (Subterranean Aqueducts: ice houses +1 food). A corral's own rate is 0; each
	// garrisoned animal adds 1, 2 or 3 food every 5 s by its size
	// (corral_garrison_{small,medium,large}.json).
	const bool two_s = due(2.0f), corral_due = due(CORRAL_TRICKLE_INTERVAL);
	if (two_s) {
		for (const auto &b : buildings_) {
			if (b.team >= MAX_PLAYERS || b.build_progress < 1.0f || b.hp <= 0.0f) continue;
			float rate[RES_COUNT] = {};
			if (b.type == BLD_WONDER) {
				rate[RES_FOOD] = rate[RES_WOOD] = rate[RES_STONE] = rate[RES_GOLD] = 1.0f;
			} else if (b.type == BLD_MINISTRY) {
				// han_minister_garrison_ministry.json (Nationalized Industries): +0.1 wood,
				// stone and metal for each minister garrisoned inside.
				rate[RES_WOOD] = rate[RES_STONE] = rate[RES_GOLD] = 0.1f * (1 + garrisoned_ministers(b));
			} else if (b.type == BLD_ICE_HOUSE) {
				rate[RES_FOOD] = 1.0f;
			} else {
				continue;
			}
			PlayerState &ps = players_[b.team];
			const uint64_t classes = building_tech_classes(b.type);
			for (uint8_t id = 1; id < TECH_COUNT; ++id) {
				if (!ps.has_tech(id)) continue;
				const TechInfo &ti = tech_info(id);
				const TechEffect *fx = tech_effects_table() + ti.effect_begin;
				for (uint16_t k = 0; k < ti.effect_count; ++k) {
					const TechEffect &e = fx[k];
					if (e.stat < TSTAT_TRICKLE_FOOD || e.stat > TSTAT_TRICKLE_METAL || is_player_trickle(e)
					    || !tech_effect_applies(e, classes)) continue;
					float &v = rate[kTrickleRes[e.stat - TSTAT_TRICKLE_FOOD]];
					v = (e.op == TOP_MUL) ? v * e.value : (e.op == TOP_SET) ? e.value : v + e.value;
				}
			}
			for (int r = 0; r < RES_COUNT; ++r) ps.res[r] += rate[r];
		}
	}
	if (corral_due) {
		for (size_t f = 0; f < fauna_.count; ++f) {
			if (!fauna_.alive[f] || fauna_.behavior[f] != FAUNA_CORRALLED) continue;
			auto it = building_id_to_idx_.find(fauna_.corral_id[f]);
			if (it == building_id_to_idx_.end()) continue;
			const Building &b = buildings_[it->second];
			if (b.team >= MAX_PLAYERS || b.build_progress < 1.0f || b.hp <= 0.0f) continue;
			players_[b.team].res[RES_FOOD] += fauna_corral_food(fauna_.fauna_type[f]);
		}
	}
}

int Simulation::garrisoned_ministers(const Building &b) const {
	int n = 0;
	for (uint32_t id : b.garrisoned_units) {
		const int32_t u = lookup_unit(id);
		n += u >= 0 && units_.unit_type[u] == UNIT_MINISTER;
	}
	return n;
}

// Animals garrisoned in a corral (GarrisonHolder/Max CORRAL_GARRISON_MAX).
int Simulation::corral_count(uint32_t corral_id) const {
	int n = 0;
	for (size_t f = 0; f < fauna_.count; ++f)
		n += fauna_.alive[f] && fauna_.behavior[f] == FAUNA_CORRALLED && fauna_.corral_id[f] == corral_id;
	return n;
}

// 0 A.D. DeathDamage.js: only templates with a DeathDamage element hurt anything when
// they die. Of the trained ones that is mixins/fireship.xml (brit, gaul and iber
// ship_fire): 500 Fire over 20 m, friendly fire on. template_unit_siege_flamethrower
// has one too but no civ trains it.
static const DeathDamageInfo FIRESHIP_DEATH_DAMAGE = {20.0f * AREA_DAMAGE_SCALE, 500.0f, true};

static bool template_is(uint16_t tmpl, const char *name) {
	if (tmpl == CIV_UNIT_TEMPLATE_NONE || tmpl >= CIV_UNIT_TEMPLATE_COUNT) return false;
	const char *id = civ_unit_template(tmpl).id;
	const char *slash = std::strrchr(id, '/');
	return std::strcmp(slash ? slash + 1 : id, name) == 0;
}

// The fire ship templates (template_unit_ship_fire.xml), looked up once: the
// Health regen loop asks this for every unit every tick.
static bool is_fire_ship(uint16_t tmpl) {
	static const auto table = [] {
		std::array<bool, CIV_UNIT_TEMPLATE_COUNT> t{};
		for (uint16_t k = 0; k < CIV_UNIT_TEMPLATE_COUNT; ++k) t[k] = template_is(k, "ship_fire");
		return t;
	}();
	return tmpl < CIV_UNIT_TEMPLATE_COUNT && table[tmpl];
}

const DeathDamageInfo *Simulation::unit_death_damage(size_t i) const {
	return is_fire_ship(units_.tmpl[i]) ? &FIRESHIP_DEATH_DAMAGE : nullptr;
}

// Health RegenRate from the template; tech modifiers (TSTAT_REGEN) add to it.
float Simulation::unit_template_regen(size_t i) const {
	return is_fire_ship(units_.tmpl[i]) ? -5.0f : 0.0f; // mixins/fireship.xml: it burns down
}

// DeathDamage.CauseDeathDamage -> AttackHelper.CauseDamageOverArea: every entity of the
// players it may hurt within Range takes the damage times 1 - d^2 / Range^2, d measured
// from its obstruction. Nothing resists Fire (no template has a Fire resistance).
void Simulation::tick_death_damage(float dt) {
	(void)dt;
	const size_t start = (recent_death_head_ > MAX_RECENT_DEATHS) ? (recent_death_head_ - MAX_RECENT_DEATHS) : 0;
	// A unit killed here records its own death, so a fire ship blowing up next to
	// another sets that one off too, within this tick.
	for (size_t k = start; k < recent_death_head_; ++k) {
		RecentDeath &death = recent_deaths_[k % MAX_RECENT_DEATHS];
		if (death.death_damage_applied) continue;
		death.death_damage_applied = true;
		if (!death.death_damage || death.is_building) continue;
		const DeathDamageInfo &dd = FIRESHIP_DEATH_DAMAGE;
		const float ox = death.x, oz = death.z, r = dd.range, r2 = r * r;
		const uint8_t owner = death.team;
		auto hurts = [&](uint8_t team) { return dd.friendly_fire || is_hostile(owner, team); };
		for (size_t i = 0; i < units_.count; ++i) {
			if (!units_.alive[i] || units_.action_state[i] == ACTION_GARRISONED || !hurts(units_.team[i])) continue;
			const float dx = units_.pos_x[i] - ox, dz = units_.pos_z[i] - oz;
			if (std::fabs(dx) > r + 2.0f || std::fabs(dz) > r + 2.0f) continue;
			const float d = std::max(0.0f, std::sqrt(dx * dx + dz * dz) - unit_collision_radius(units_.unit_type[i]));
			if (d >= r) continue;
			note_attacked(0, owner, units_.id[i], units_.team[i], false, dd.damage * (1.0f - d * d / r2));
			units_.hp[i] -= dd.damage * (1.0f - d * d / r2);
			if (units_.hp[i] > 0.0f) continue;
			units_.hp[i] = 0.0f;
			units_.alive[i] = 0;
			units_.action_state[i] = ACTION_DYING;
			stat_unit_lost(i, owner); // the dead entity's owner deals it
			if (units_.team[i] == 0) record_.units_lost++;
			else record_.enemies_killed++;
			record_unit_death(i, owner);
		}
		for (Building &b : buildings_) {
			if (b.wrecked || b.hp <= 0.0f || (b.team < MAX_PLAYERS && !hurts(b.team))) continue;
			if (b.team >= MAX_PLAYERS && !dd.friendly_fire && !is_hostile(owner, b.team)) continue;
			const float ex = std::max(0.0f, std::fabs(b.x - ox) - b.half_size);
			const float ez = std::max(0.0f, std::fabs(b.z - oz) - b.half_size);
			const float d2 = ex * ex + ez * ez;
			if (d2 >= r2) continue;
			note_attacked(0, owner, b.id, b.team, true, dd.damage * (1.0f - d2 / r2));
			b.hp -= dd.damage * (1.0f - d2 / r2);
			if (b.hp <= 0.0f) mark_building_wreck(b, owner);
		}
	}
}

void Simulation::unit_resistance(size_t i, float out[3]) const {
	const auto &info = unit_info_at(i);
	out[0] = info.hack_armor;
	out[1] = info.pierce_armor;
	out[2] = info.crush_armor;
	if (units_.team[i] < MAX_PLAYERS && units_.unit_type[i] < UNIT_TYPE_COUNT) {
		const float *armor = unit_tech_mods(units_.team[i], units_.unit_type[i], units_.tmpl[i]).armor;
		for (int k = 0; k < 3; ++k) out[k] += armor[k];
	}
	if (const float ra = rank_melee_armor(i); ra > 0.0f)
		for (int k = 0; k < 3; ++k) out[k] += ra;
	const float aura = aura_mgr_.query_modifier(units_.pos_x[i], units_.pos_z[i], units_.team[i], AURA_ARMOR,
			units_.tmpl[i], &unit_combat_at(i).classes);
	out[0] += aura;
	out[1] += aura;
}

// 0 A.D. helpers/Attack.js CauseDamageOverArea: every entity of a player the
// attack hurts (its enemies, everyone with FriendlyFire) within the Splash Range
// of where the attack landed, the attacked target included, takes the Splash
// Damage less its resistance, times a falloff from the distance to its
// obstruction's edge: 1 - d^2/r^2 for Circular, and for Linear a strip Range
// long and Range/5 wide ahead along the missile's direction, falling off
// quadratically along and across it. Garrisoned units are out of the world.
void Simulation::cause_damage_over_area(uint8_t splash, uint32_t attacker_id, uint8_t attacker_team,
		float ox, float oz, float dir_x, float dir_z) {
	if (splash == 0 || splash >= SPLASH_PROFILE_COUNT) return;
	const SplashProfile &sp = splash_profiles()[splash];
	const float r = sp.range * AREA_DAMAGE_SCALE;
	if (r <= 0.0f) return;
	const float r2 = r * r;
	const bool linear = sp.shape == SPLASH_LINEAR;
	const float width = r / 5.0f;
	const float dl = std::sqrt(dir_x * dir_x + dir_z * dir_z);
	const float fx = dl > 1e-6f ? dir_x / dl : 1.0f, fz = dl > 1e-6f ? dir_z / dl : 0.0f;
	auto hurts = [&](uint8_t team) { return sp.friendly_fire || is_hostile(attacker_team, team); };
	// The falloff at an entity whose edge is d from the origin, in direction (px, pz).
	auto falloff = [&](float d, float px, float pz) {
		if (!linear) return 1.0f - d * d / r2;
		const float pl = std::sqrt(px * px + pz * pz);
		const float rx = pl > 1e-6f ? px / pl * d : 0.0f, rz = pl > 1e-6f ? pz / pl * d : 0.0f;
		const float along = rx * fx + rz * fz;
		const float across = rx * fz - rz * fx;
		if (along < 0.0f || std::fabs(across) >= width) return 0.0f;
		return (1.0f - along * along / r2) * (1.0f - across * across / (width * width));
	};
	for (size_t u = 0; u < units_.count; ++u) {
		if (!units_.alive[u] || units_.action_state[u] == ACTION_GARRISONED || !hurts(units_.team[u])) continue;
		const float px = units_.pos_x[u] - ox, pz = units_.pos_z[u] - oz;
		if (std::fabs(px) > r + 2.0f || std::fabs(pz) > r + 2.0f) continue;
		const float d = std::max(0.0f, std::sqrt(px * px + pz * pz) - unit_collision_radius(units_.unit_type[u]));
		if (d >= r) continue;
		const float m = falloff(d, px, pz);
		if (m <= 0.0f) continue;
		float res[3];
		unit_resistance(u, res);
		const float dmg = m * (sp.hack * powf(0.9f, res[0]) + sp.pierce * powf(0.9f, res[1]) +
				sp.crush * powf(0.9f, res[2]));
		if (dmg <= 0.0f) continue;
		award_damage_xp(attacker_id, u, dmg);
		note_attacked(attacker_id, attacker_team, units_.id[u], units_.team[u], false, dmg);
		units_.hp[u] -= dmg;
		if (units_.attacked_by[u] == 0 && attacker_id != 0) units_.attacked_by[u] = attacker_id;
		if (units_.team[u] == 0)
			raise_alert(BattleAlert::ALERT_UNDER_ATTACK, units_.pos_x[u], units_.pos_z[u], units_.id[u], units_.team[u]);
		if (units_.hp[u] > 0.0f) continue;
		units_.alive[u] = 0;
		units_.action_state[u] = ACTION_DYING;
		note_kill(attacker_id, units_.id[u]);
		record_unit_death(u, attacker_team);
		stat_unit_lost(u, attacker_team);
		if (units_.team[u] == 0) record_.units_lost++;
		else record_.enemies_killed++;
		collect_unit_loot(attacker_id, attacker_team, u);
	}
	bool nav_changed = false, territory_changed = false;
	for (Building &b : buildings_) {
		if (b.wrecked || b.hp <= 0.0f || !hurts(b.team)) continue;
		const float ex = std::max(0.0f, std::fabs(b.x - ox) - b.half_size);
		const float ez = std::max(0.0f, std::fabs(b.z - oz) - b.half_size);
		const float d2 = ex * ex + ez * ez;
		if (d2 >= r2) continue;
		const float m = falloff(std::sqrt(d2), b.x - ox, b.z - oz);
		if (m <= 0.0f) continue;
		const CombatProfile &bc = building_combat(b.type);
		const float dmg = m * (sp.hack * powf(0.9f, bc.hack_armor) + sp.pierce * powf(0.9f, bc.pierce_armor) +
				sp.crush * powf(0.9f, bc.crush_armor));
		if (dmg <= 0.0f) continue;
		note_attacked(attacker_id, attacker_team, b.id, b.team, true, dmg);
		b.hp -= dmg;
		if (b.team == 0) raise_alert(BattleAlert::ALERT_BUILDING_UNDER_ATTACK, b.x, b.z, b.id, b.team);
		if (b.hp > 0.0f) continue;
		if (attacker_team == 0) record_.enemies_killed++;
		else if (b.team == 0) record_.buildings_destroyed++;
		if (b.team == 0) raise_alert(BattleAlert::ALERT_BUILDING_DESTROYED, b.x, b.z, b.id, b.team);
		collect_building_loot(attacker_id, attacker_team, b);
		mark_building_wreck(b, attacker_team);
		nav_changed = true;
		if (b.type == BLD_TOWN_CENTER || b.type == BLD_FORTRESS || b.type == BLD_TOWER) territory_changed = true;
	}
	if (nav_changed) rebuild_nav_grid();
	if (territory_changed) rebuild_territory();
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

void Simulation::order_guard(const uint32_t *unit_ids_in, size_t count_in, uint32_t target_unit_id, bool queued) {
	int32_t tidx = lookup_unit(target_unit_id);
	if (tidx < 0 || !units_.alive[tidx]) return;
	UnitOrder o; o.kind = ORDER_GUARD; o.target = target_unit_id;
	const std::vector<uint32_t> now = split_queued(unit_ids_in, count_in, o, queued);
	for (uint32_t uid : now) {
		int32_t idx = lookup_unit(uid);
		if (idx < 0 || !units_.alive[idx] || uid == target_unit_id) continue;
		set_order(idx, ORDER_GUARD, 0, target_unit_id, units_.pos_x[tidx], units_.pos_z[tidx]);
		units_.action_state[idx] = ACTION_GUARD;
		units_.has_order[idx] = 1;
		units_.target_unit_id[idx] = target_unit_id;
		units_.target_building_id[idx] = 0;
		units_.target_resource_id[idx] = 0;
		units_.waypoint_count[idx] = 0;
		units_.target_x[idx] = units_.pos_x[tidx];
		units_.target_z[idx] = units_.pos_z[tidx];
	}
}

// ---------------------------------------------------------------------------
// 0 A.D. UnitAI orders (UnitAI.js orderQueue, FinishOrder, PushOrderFront,
// RespondToTargetedEntities, Flee, Patrol, Guard; helpers/Commands.js
// "delete-entities" and "back-to-work"). A unit's current order lives in the
// SoA (order_*); its Shift-queued orders in order_queues_. An automatic fight
// or a flight marks the order INTERRUPTED; tick_unit_ai sends the unit back to
// it afterwards, and moves on to the next queued order once it is done.
// ---------------------------------------------------------------------------

void Simulation::set_order(size_t i, uint8_t kind, uint8_t flags, uint32_t target, float x, float z) {
	units_.order_kind[i] = kind;
	units_.order_flags[i] = flags;
	units_.order_target[i] = target;
	units_.order_x[i] = x;
	units_.order_z[i] = z;
	units_.order_x2[i] = units_.pos_x[i]; // a patrol starts where the unit stands
	units_.order_z2[i] = units_.pos_z[i];
	if (kind == ORDER_GATHER || kind == ORDER_BUILD) { // UnitAI workOrders, for Back to Work
		units_.work_kind[i] = kind;
		units_.work_target[i] = target;
	}
}

void Simulation::begin_walk_order(size_t i) {
	set_order(i, ORDER_WALK, 0, 0, units_.target_x[i], units_.target_z[i]);
	if (!applying_queued_) order_queues_.erase(units_.id[i]);
}

void Simulation::drop_orders(size_t i) {
	set_order(i, ORDER_NONE, 0, 0, units_.pos_x[i], units_.pos_z[i]);
	order_queues_.erase(units_.id[i]);
}

UnitOrder Simulation::current_order(size_t i) const {
	UnitOrder o;
	o.kind = units_.order_kind[i];
	o.flags = units_.order_flags[i];
	o.target = units_.order_target[i];
	o.x = units_.order_x[i]; o.z = units_.order_z[i];
	o.x2 = units_.order_x2[i]; o.z2 = units_.order_z2[i];
	return o;
}

UnitOrder Simulation::unit_order(uint32_t unit_id) const {
	const int32_t i = lookup_unit(unit_id);
	return i >= 0 ? current_order((size_t)i) : UnitOrder{};
}

const std::vector<UnitOrder> *Simulation::unit_order_queue(uint32_t unit_id) const {
	auto it = order_queues_.find(unit_id);
	return it == order_queues_.end() ? nullptr : &it->second;
}

std::vector<uint32_t> Simulation::split_queued(const uint32_t *ids, size_t count, const UnitOrder &o, bool queued) {
	std::vector<uint32_t> now;
	now.reserve(count);
	if (!queued) {
		for (size_t k = 0; k < count; ++k) {
			const int32_t i = lookup_unit(ids[k]);
			if (i < 0) continue;
			if (!applying_queued_) order_queues_.erase(ids[k]);
			now.push_back(ids[k]);
		}
		return now;
	}
	// Walks keep the group's shape: each unit's point is offset from the click as
	// its previous destination was from the group's.
	const bool walk = o.kind == ORDER_WALK || o.kind == ORDER_WALK_AND_FIGHT || o.kind == ORDER_PATROL;
	auto last_dest = [&](size_t i, float &x, float &z) {
		auto it = order_queues_.find(units_.id[i]);
		if (it != order_queues_.end() && !it->second.empty()) { x = it->second.back().x; z = it->second.back().z; }
		else if (units_.order_kind[i] != ORDER_NONE) { x = units_.order_x[i]; z = units_.order_z[i]; }
		else { x = units_.pos_x[i]; z = units_.pos_z[i]; }
	};
	float cx = 0.0f, cz = 0.0f;
	int busy_n = 0;
	auto busy = [&](size_t i) {
		return units_.alive[i] && (units_.order_kind[i] != ORDER_NONE || units_.action_state[i] != ACTION_IDLE);
	};
	for (size_t k = 0; k < count; ++k) {
		const int32_t i = lookup_unit(ids[k]);
		if (i < 0 || !busy((size_t)i)) continue;
		float x, z;
		last_dest((size_t)i, x, z);
		cx += x; cz += z; ++busy_n;
	}
	if (busy_n) { cx /= (float)busy_n; cz /= (float)busy_n; }
	for (size_t k = 0; k < count; ++k) {
		const int32_t i = lookup_unit(ids[k]);
		if (i < 0) continue;
		if (!busy((size_t)i)) { now.push_back(ids[k]); continue; }
		UnitOrder q = o;
		float lx, lz;
		last_dest((size_t)i, lx, lz);
		if (walk && busy_n > 1) {
			q.x = std::clamp(o.x + lx - cx, -map_half_ + 1.0f, map_half_ - 1.0f);
			q.z = std::clamp(o.z + lz - cz, -map_half_ + 1.0f, map_half_ - 1.0f);
		}
		if (o.kind == ORDER_PATROL) { q.x2 = lx; q.z2 = lz; } // a queued patrol starts where the previous order ends
		std::vector<UnitOrder> &queue = order_queues_[ids[k]];
		if (queue.size() < 64) queue.push_back(q);
	}
	return now;
}

void Simulation::apply_order(uint32_t uid, const UnitOrder &o) {
	const bool was = applying_queued_;
	applying_queued_ = true;
	const bool capture = (o.flags & ORDER_FLAG_ALLOW_CAPTURE) != 0;
	switch (o.kind) {
		case ORDER_WALK: cmd_move(&uid, 1, o.x, o.z); break;
		case ORDER_WALK_AND_FIGHT:
			order_attack_move(&uid, 1, o.x, o.z, false, (o.flags & ORDER_FLAG_UNITS_ONLY) != 0, capture);
			break;
		case ORDER_PATROL: order_patrol(&uid, 1, o.x, o.z); break;
		case ORDER_ATTACK: order_attack(&uid, 1, o.target); break;
		case ORDER_ATTACK_BUILDING: order_attack_building(&uid, 1, o.target, false, capture); break;
		case ORDER_GATHER: {
			uint32_t r = o.target;
			// Order.Gather on a resource that is gone -> FINDINGNEWTARGET (GatherNearPosition):
			// the same kind of resource near where it was.
			const int32_t i = lookup_unit(uid);
			auto rit = res_id_to_idx_.find(r);
			const bool gone = rit == res_id_to_idx_.end() || !resources_.alive[rit->second] || resources_.amount[rit->second] <= 0;
			if (gone && o.res_type != 0 && i >= 0)
				r = find_nearby_resource(o.x, o.z, (uint8_t)(o.res_type - 1), units_.team[i],
						(o.flags & ORDER_FLAG_SUB_MASK) >> ORDER_FLAG_SUB_SHIFT);
			order_gather(&uid, 1, r);
			break;
		}
		case ORDER_BUILD: order_build_or_repair(&uid, 1, o.target); break;
		case ORDER_GARRISON: order_garrison(&uid, 1, o.target); break;
		case ORDER_GUARD: order_guard(&uid, 1, o.target); break;
		case ORDER_HEAL: order_heal(&uid, 1, o.target); break;
		case ORDER_TRADE: order_trade(&uid, 1, o.target, trade_market_id(o.x2)); break;
		case ORDER_RETURN_RESOURCE: order_return_resource(&uid, 1, o.target); break;
		case ORDER_COLLECT_TREASURE: order_collect_treasure(&uid, 1, o.target); break;
		default: break;
	}
	applying_queued_ = was;
	const int32_t i = lookup_unit(uid);
	if (i < 0 || units_.order_kind[i] != o.kind) return;
	units_.order_flags[i] = o.flags & (uint8_t)~ORDER_FLAG_INTERRUPTED;
	if (o.kind == ORDER_PATROL) { units_.order_x2[i] = o.x2; units_.order_z2[i] = o.z2; }
	if (o.kind == ORDER_TRADE) units_.order_x2[i] = o.x2;
}

bool Simulation::order_still_valid(size_t i, const UnitOrder &o) const {
	const uint8_t team = units_.team[i];
	auto building = [&](uint32_t id) -> const Building * {
		auto it = building_id_to_idx_.find(id);
		if (it == building_id_to_idx_.end()) return nullptr;
		const Building &b = buildings_[it->second];
		return b.wrecked || b.hp <= 0.0f ? nullptr : &b;
	};
	switch (o.kind) {
		case ORDER_WALK: case ORDER_WALK_AND_FIGHT: case ORDER_PATROL: return true;
		case ORDER_ATTACK: {
			const int32_t t = lookup_unit(o.target);
			return t >= 0 && units_.alive[t] && can_attack(team, units_.team[t]);
		}
		case ORDER_ATTACK_BUILDING: {
			const Building *b = building(o.target);
			return b && (can_attack(team, b->team) ||
					((o.flags & ORDER_FLAG_ALLOW_CAPTURE) && can_capture_building(team, *b)));
		}
		case ORDER_GATHER: {
			auto it = res_id_to_idx_.find(o.target);
			return it != res_id_to_idx_.end() && resources_.alive[it->second] && resources_.amount[it->second] > 0;
		}
		case ORDER_BUILD: {
			const Building *b = building(o.target);
			return b && (b->team == team || is_ally(team, b->team)) && (b->build_progress < 1.0f || b->hp < b->max_hp ||
					b->type == BLD_FIELD);
		}
		case ORDER_GARRISON: {
			const Building *b = building(o.target);
			return b && b->team == team && b->build_progress >= 1.0f && (int)b->garrisoned_units.size() < b->garrison_cap;
		}
		case ORDER_GUARD: {
			const int32_t t = lookup_unit(o.target);
			return t >= 0 && units_.alive[t];
		}
		case ORDER_HEAL: return can_heal(i, lookup_unit(o.target));
		case ORDER_TRADE: return trade_route_ok(i, o.target, trade_market_id(o.x2));
		case ORDER_RETURN_RESOURCE: {
			const Building *b = building(o.target);
			return b && units_.carry[i] > 0 && dropsite_accepts(i, *b);
		}
		case ORDER_COLLECT_TREASURE: return is_treasure_id(o.target);
		default: return false;
	}
}

// UnitAI FinishOrder: a gatherer whose resource ran out first looks for more of the
// same type nearby (GATHER "ResourceExhausted"); then the next queued order runs.
void Simulation::finish_order(size_t i) {
	const uint32_t uid = units_.id[i];
	if (units_.order_kind[i] == ORDER_GATHER && !order_still_valid(i, current_order(i))) {
		// FINDINGNEWTARGET: the same kind of resource, near the unit, else near where it was.
		const int sub = (units_.order_flags[i] & ORDER_FLAG_SUB_MASK) >> ORDER_FLAG_SUB_SHIFT;
		uint32_t r = find_nearby_resource(units_.pos_x[i], units_.pos_z[i], units_.res_type_carried[i], units_.team[i], sub);
		if (r == 0)
			r = find_nearby_resource(units_.order_x[i], units_.order_z[i], units_.res_type_carried[i], units_.team[i], sub);
		if (r != 0) {
			UnitOrder o = current_order(i);
			o.target = r;
			apply_order(uid, o);
			return;
		}
		// GATHER FINDINGNEWTARGET: nothing left to gather nearby; drop off what it carries.
		if (units_.carry[i] > 0 && !order_queues_.count(uid)) {
			set_order(i, ORDER_NONE, 0, 0, units_.pos_x[i], units_.pos_z[i]);
			units_.target_resource_id[i] = 0;
			units_.action_state[i] = ACTION_RETURN_DROPSITE;
			return;
		}
	}
	auto it = order_queues_.find(uid);
	if (it != order_queues_.end() && !it->second.empty()) {
		const UnitOrder next = it->second.front();
		it->second.erase(it->second.begin());
		if (it->second.empty()) order_queues_.erase(it);
		apply_order(uid, next);
		return;
	}
	set_order(i, ORDER_NONE, 0, 0, units_.pos_x[i], units_.pos_z[i]);
	if (units_.action_state[i] == ACTION_ATTACK || units_.action_state[i] == ACTION_FLEE) {
		units_.action_state[i] = ACTION_IDLE;
		units_.has_order[i] = 0;
		units_.target_unit_id[i] = 0;
		units_.target_building_id[i] = 0;
	}
}

// Back to the order an automatic fight or a flight interrupted.
void Simulation::resume_order(size_t i) {
	units_.order_flags[i] &= (uint8_t)~ORDER_FLAG_INTERRUPTED;
	units_.target_unit_id[i] = 0;
	units_.target_building_id[i] = 0;
	const UnitOrder o = current_order(i);
	if (o.kind == ORDER_NONE) {
		units_.action_state[i] = ACTION_IDLE;
		units_.has_order[i] = 0;
		// UnitAI WalkToHeldPosition: a defensive unit goes back where it stood.
		const float dx = units_.pos_x[i] - units_.anchor_x[i], dz = units_.pos_z[i] - units_.anchor_z[i];
		if (units_.stance[i] == STANCE_DEFENSIVE && dx * dx + dz * dz > 2.0f * 2.0f) {
			const uint32_t uid = units_.id[i];
			cmd_move(&uid, 1, units_.anchor_x[i], units_.anchor_z[i]);
		}
		return;
	}
	if (order_still_valid(i, o)) apply_order(units_.id[i], o);
	else finish_order(i);
}

// UnitAI "Attacked" -> RespondToTargetedEntities: fight back as the stance allows,
// or flee (passive stance, or a unit that can't attack).
void Simulation::respond_to_attack(size_t i) {
	const int32_t a = lookup_unit(units_.attacked_by[i]);
	if (a < 0 || !units_.alive[a] || !is_hostile(units_.team[i], units_.team[a])) return;
	const uint8_t st = units_.action_state[i];
	if (st == ACTION_ATTACK && (units_.target_unit_id[i] != 0 || units_.target_building_id[i] != 0)) return;
	const uint8_t stance = units_.stance[i];
	// Forced orders ignore attackers, unless the stance is violent (targetAttackersAlways).
	if ((units_.order_flags[i] & ORDER_FLAG_FORCE) && stance != STANCE_VIOLENT && st != ACTION_IDLE && st != ACTION_FLEE) return;
	const float dx = units_.pos_x[i] - units_.pos_x[a], dz = units_.pos_z[i] - units_.pos_z[a];
	const float d2 = dx * dx + dz * dz;
	const bool can_fight = units_.attack_damage[i] > 0.0f && stance != STANCE_PASSIVE;
	if (!can_fight) {
		if (unit_is_siege_engine(units_.unit_type[i]) || !pack_state_can_move(units_.pack_state[i])) return;
		// UnitAI Flee: run FleeDistance away from the attacker, then carry on.
		const float d = sqrtf(d2);
		const float nx = d > 1e-3f ? dx / d : 1.0f, nz = d > 1e-3f ? dz / d : 0.0f;
		units_.target_x[i] = std::clamp(units_.pos_x[i] + nx * FLEE_DISTANCE, -map_half_ + 1.0f, map_half_ - 1.0f);
		units_.target_z[i] = std::clamp(units_.pos_z[i] + nz * FLEE_DISTANCE, -map_half_ + 1.0f, map_half_ - 1.0f);
		units_.has_order[i] = 1;
		units_.action_state[i] = ACTION_FLEE;
		units_.target_unit_id[i] = 0;
		units_.target_building_id[i] = 0;
		units_.waypoint_cur[i] = units_.waypoint_count[i] = 0;
		if (i < unit_paths_.size()) unit_paths_[i].clear();
		units_.attack_timer[i] = 4.0f; // give up a flight that is stuck
		units_.order_flags[i] |= ORDER_FLAG_INTERRUPTED;
		return;
	}
	const float range = units_.attack_range[i];
	const float vision = unit_vision_range(units_.unit_type[i]);
	if (stance == STANCE_STAND_GROUND && d2 > range * range) return;
	if (stance == STANCE_DEFENSIVE) {
		const float hx = units_.pos_x[a] - units_.anchor_x[i], hz = units_.pos_z[a] - units_.anchor_z[i];
		const float lim = vision * 0.5f + range;
		if (hx * hx + hz * hz >= lim * lim) return;
	}
	units_.target_unit_id[i] = units_.attacked_by[i];
	units_.target_building_id[i] = 0;
	units_.action_state[i] = ACTION_ATTACK;
	units_.has_order[i] = stance != STANCE_STAND_GROUND;
	units_.target_x[i] = units_.pos_x[a];
	units_.target_z[i] = units_.pos_z[a];
	units_.waypoint_count[i] = 0;
	units_.order_flags[i] |= ORDER_FLAG_INTERRUPTED;
}

uint32_t Simulation::find_nearby_resource(float x, float z, uint8_t res_type, uint8_t team, int subtype) const {
	uint32_t best = 0;
	float best_d2 = NEARBY_SEARCH_RANGE * NEARBY_SEARCH_RANGE;
	for (size_t r = 0; r < resources_.count; ++r) {
		if (!resources_.alive[r] || resources_.amount[r] <= 0 || resources_.type[r] != res_type) continue;
		if (subtype >= 0 && resources_.subtype[r] != subtype) continue; // the same kind (type.specific)
		auto f = field_bld_by_res_.find(resources_.id[r]);
		if (f != field_bld_by_res_.end()) { // only our own fields, or Gaia's
			auto b = building_id_to_idx_.find(f->second);
			if (b == building_id_to_idx_.end() || (buildings_[b->second].team != team && buildings_[b->second].team != GAIA_PLAYER)) continue;
		}
		const float dx = resources_.x[r] - x, dz = resources_.z[r] - z;
		const float d2 = dx * dx + dz * dz;
		if (d2 < best_d2) { best_d2 = d2; best = resources_.id[r]; }
	}
	return best;
}

uint32_t Simulation::find_nearby_foundation(float x, float z, uint8_t team) const {
	uint32_t best = 0;
	float best_d2 = NEARBY_SEARCH_RANGE * NEARBY_SEARCH_RANGE;
	for (const Building &b : buildings_) {
		if (b.wrecked || b.team != team || b.build_progress >= 1.0f || b.hp < 0.0f) continue;
		const float dx = b.x - x, dz = b.z - z;
		const float d2 = dx * dx + dz * dz;
		if (d2 < best_d2) { best_d2 = d2; best = b.id; }
	}
	return best;
}

static bool can_gather_type(uint8_t t) {
	return t == UNIT_VILLAGER || t == UNIT_FEMALE_CITIZEN || t == UNIT_WORKER_ELEPHANT;
}

// 0 A.D. RallyPoint + Commands.js: a trained unit does what the rally target asks
// for, if it can (gather, build, garrison, attack), and walks there otherwise.
void Simulation::apply_rally_point(uint32_t uid, const Building &b, bool ignore_self_garrison) {
	const int32_t i = lookup_unit(uid);
	if (i < 0) return;
	const uint8_t ut = units_.unit_type[i];
	if (b.rally_target_res_id != 0 && can_gather_type(ut) && res_id_to_idx_.count(b.rally_target_res_id)) {
		order_gather(&uid, 1, b.rally_target_res_id);
		return;
	}
	if (b.rally_target_bld_id != 0) {
		auto it = building_id_to_idx_.find(b.rally_target_bld_id);
		if (it != building_id_to_idx_.end() && !buildings_[it->second].wrecked) {
			const Building &t = buildings_[it->second];
			if (t.team == b.team) {
				if (can_gather_type(ut) && (t.build_progress < 1.0f || t.hp < t.max_hp || t.type == BLD_FIELD)) {
					order_build_or_repair(&uid, 1, t.id);
					return;
				}
				if (ignore_self_garrison && t.id == b.id) return;
				if ((int)t.garrisoned_units.size() < t.garrison_cap) {
					order_garrison(&uid, 1, t.id);
					return;
				}
			} else if (can_attack(b.team, t.team)) {
				order_attack_building(&uid, 1, t.id, false, true);
				return;
			}
		}
	}
	order_walk(&uid, 1, b.rally_x, b.rally_z);
}

// UnitAI ConstructionFinished for the builders of `b`: farm a field, gather the
// nearest resource a new dropsite takes, else help with a foundation nearby.
void Simulation::on_construction_finished(const Building &b, size_t builder) {
	const size_t n = units_.count;
	std::vector<uint32_t> builders;
	for (size_t k = 0; k < n; ++k)
		if (units_.alive[k] && units_.team[k] == b.team &&
				(k == builder || (units_.action_state[k] == ACTION_BUILD && units_.target_building_id[k] == b.id)))
			builders.push_back(units_.id[k]);
	const bool food = b.type == BLD_FARMSTEAD || b.type == BLD_DOCK;
	const bool other = b.type == BLD_STOREHOUSE;
	for (uint32_t uid : builders) {
		const int32_t k = lookup_unit(uid);
		if (k < 0) continue;
		units_.action_state[k] = ACTION_IDLE;
		units_.target_building_id[k] = 0;
		// A queued order goes first (FinishOrder); fields and dropsites only apply without one.
		auto q = order_queues_.find(uid);
		if (q != order_queues_.end() && !q->second.empty()) { finish_order((size_t)k); continue; }
		if (b.type == BLD_FIELD) {
			order_gather(&uid, 1, ensure_field_resource(b));
			continue;
		}
		if (food || other || b.type == BLD_TOWN_CENTER) {
			// Prefer what the unit gathered last (lastGathered), if the dropsite takes it.
			uint8_t pref = units_.res_type_carried[k];
			const bool takes_pref = b.type == BLD_TOWN_CENTER || (food ? pref == RES_FOOD : pref != RES_FOOD);
			uint32_t r = takes_pref ? find_nearby_resource(b.x, b.z, pref, b.team) : 0;
			if (r == 0) {
				if (food) r = find_nearby_resource(b.x, b.z, RES_FOOD, b.team);
				else for (uint8_t t : {(uint8_t)RES_WOOD, (uint8_t)RES_STONE, (uint8_t)RES_GOLD})
					if (r == 0) r = find_nearby_resource(b.x, b.z, t, b.team);
			}
			if (r != 0) { order_gather(&uid, 1, r); continue; }
		}
		const uint32_t f = find_nearby_foundation(b.x, b.z, b.team);
		if (f != 0) { order_build_or_repair(&uid, 1, f); continue; }
		set_order((size_t)k, ORDER_NONE, 0, 0, units_.pos_x[k], units_.pos_z[k]);
	}
}

void Simulation::tick_unit_ai(float dt) {
	(void)dt;
	const size_t n = units_.count;
	for (size_t i = 0; i < n && i < units_.count; ++i) {
		if (!units_.alive[i]) continue;
		const uint8_t st = units_.action_state[i];
		if (st == ACTION_DYING || st == ACTION_GARRISONED) continue;
		if (units_.attacked_by[i] != 0) respond_to_attack(i); // traders flee, then resume_order
		if (units_.action_state[i] == ACTION_TRADE) continue;
		// Guard.js: guards answer whoever attacks the unit they guard (GuardedAttacked).
		if (units_.order_kind[i] == ORDER_GUARD && units_.action_state[i] == ACTION_GUARD) {
			const int32_t g = lookup_unit(units_.order_target[i]);
			if (g >= 0 && units_.alive[g] && units_.attacked_by[g] != 0 && units_.attack_damage[i] > 0.0f &&
					units_.stance[i] != STANCE_PASSIVE) {
				const int32_t a = lookup_unit(units_.attacked_by[g]);
				if (a >= 0 && units_.alive[a] && can_attack(units_.team[i], units_.team[a])) {
					units_.target_unit_id[i] = units_.id[a];
					units_.action_state[i] = ACTION_ATTACK;
					units_.has_order[i] = 1;
					units_.target_x[i] = units_.pos_x[a];
					units_.target_z[i] = units_.pos_z[a];
					units_.order_flags[i] |= ORDER_FLAG_INTERRUPTED;
				}
			}
		}
		const uint8_t now = units_.action_state[i];
		const bool interrupted = (units_.order_flags[i] & ORDER_FLAG_INTERRUPTED) != 0;
		if (now == ACTION_HEAL) { tick_heal(i); continue; }
		if (now == ACTION_CHEER) {
			if (units_.attack_timer[i] <= 0.0f) units_.action_state[i] = ACTION_IDLE;
			continue;
		}
		// Formation members take their orders from the controller (UnitAI FORMATIONMEMBER):
		// tick_formations brings them back after a fight or a flight, so they don't resume their own.
		if (!formations_.empty() && formation_group_[i] != 0) {
			if (now == ACTION_FLEE) {
				const float dx = units_.target_x[i] - units_.pos_x[i], dz = units_.target_z[i] - units_.pos_z[i];
				if (dx * dx + dz * dz <= 1.0f || units_.attack_timer[i] <= 0.0f) {
					units_.attack_timer[i] = 0.0f;
					units_.order_flags[i] &= (uint8_t)~ORDER_FLAG_INTERRUPTED;
					units_.action_state[i] = ACTION_IDLE;
					units_.has_order[i] = 0;
				}
			}
			continue;
		}
		// RETURNRESOURCE: next to the dropsite (or the walk ended short of it), drop the load;
		// tick_gather commits it at the nearest dropsite, which is now this one.
		if (units_.order_kind[i] == ORDER_RETURN_RESOURCE && !interrupted && (now == ACTION_MOVE || now == ACTION_IDLE)) {
			auto bit = building_id_to_idx_.find(units_.order_target[i]);
			if (bit == building_id_to_idx_.end() || !order_still_valid(i, current_order(i))) { finish_order(i); continue; }
			const Building &b = buildings_[bit->second];
			const float ex = std::max(0.0f, std::fabs(units_.pos_x[i] - b.x) - b.half_size);
			const float ez = std::max(0.0f, std::fabs(units_.pos_z[i] - b.z) - b.half_size);
			const float reach = units_.unit_type[i] == UNIT_FISHING_BOAT ? 4.0f : 1.5f;
			if (now == ACTION_IDLE || ex * ex + ez * ez < reach * reach) {
				units_.action_state[i] = ACTION_RETURN_DROPSITE;
				units_.target_resource_id[i] = 0;
			}
			continue;
		}
		// Garrison: get in once within LoadingRange; give up if the walk ends short of it.
		if (units_.order_kind[i] == ORDER_GARRISON && !interrupted && (now == ACTION_MOVE || now == ACTION_IDLE)) {
			auto bit = building_id_to_idx_.find(units_.order_target[i]);
			Building *b = bit == building_id_to_idx_.end() ? nullptr : &buildings_[bit->second];
			if (!b || !order_still_valid(i, current_order(i))) { finish_order(i); continue; }
			if (garrison_in_range(i, *b)) { garrison_now(i, *b); continue; }
			if (now == ACTION_IDLE) finish_order(i);
			continue;
		}
		// A worker back from the dropsite with nothing left to gather.
		if (now == ACTION_GATHER && units_.target_resource_id[i] == 0) {
			units_.action_state[i] = ACTION_IDLE;
			units_.has_order[i] = 0;
			if (!interrupted && units_.order_kind[i] != ORDER_NONE) {
				const UnitOrder o = current_order(i);
				if (order_still_valid(i, o)) apply_order(units_.id[i], o); // back to the work it left
				else finish_order(i);
			}
			continue;
		}
		if (now == ACTION_FLEE) {
			const float dx = units_.target_x[i] - units_.pos_x[i], dz = units_.target_z[i] - units_.pos_z[i];
			if (dx * dx + dz * dz <= 1.0f || units_.attack_timer[i] <= 0.0f) {
				units_.attack_timer[i] = 0.0f;
				resume_order(i);
			}
			continue;
		}
		// Patrol: turn round at each end (the queue's next order goes first).
		if (now == ACTION_PATROL && units_.order_kind[i] == ORDER_PATROL) {
			const float dx = units_.order_x[i] - units_.pos_x[i], dz = units_.order_z[i] - units_.pos_z[i];
			if (dx * dx + dz * dz <= 1.0f) {
				auto q = order_queues_.find(units_.id[i]);
				if (q != order_queues_.end() && !q->second.empty()) { finish_order(i); continue; }
				UnitOrder back = current_order(i);
				std::swap(back.x, back.x2);
				std::swap(back.z, back.z2);
				apply_order(units_.id[i], back);
			}
			continue;
		}
		if (now == ACTION_ATTACK && units_.target_unit_id[i] == 0 && units_.target_building_id[i] == 0) {
			// COMBAT.FINDINGNEWTARGET: queued orders first, then cheer if the unit killed its target.
			const bool cheer = (units_.order_flags[i] & ORDER_FLAG_CHEER) != 0;
			units_.order_flags[i] &= (uint8_t)~ORDER_FLAG_CHEER;
			if (interrupted) resume_order(i);
			else if (units_.order_kind[i] != ORDER_NONE) finish_order(i);
			else { units_.action_state[i] = ACTION_IDLE; units_.has_order[i] = 0; }
			if (cheer && units_.action_state[i] == ACTION_IDLE && units_.order_kind[i] == ORDER_NONE &&
					!order_queues_.count(units_.id[i]))
				start_cheer(i);
			continue;
		}
		if (now == ACTION_IDLE) {
			if (interrupted) resume_order(i);
			else if (units_.order_kind[i] != ORDER_NONE) finish_order(i);
			else if (units_.unit_type[i] == UNIT_HEALER && (i + ticks_) % 4 == 0) {
				// IDLE LosHealRangeUpdate -> RespondToHealableEntities.
				const uint32_t t = find_heal_target(i);
				if (t != 0) start_heal(i, t, 0);
			}
		}
	}
	for (size_t i = 0; i < units_.count; ++i) units_.attacked_by[i] = 0;
}

bool Simulation::can_capture_building(uint8_t player, const Building &b) const {
	if (b.wrecked || b.hp <= 0.0f || b.build_progress < 1.0f || b.max_capture_points <= 0.0f) return false;
	if (player >= MAX_PLAYERS || ceasefire_active()) return false;
	// Attack.js Capture RestrictedClasses "Field Palisade Wall" (gates and wall towers are Wall too).
	if (b.type == BLD_FIELD || b.type == BLD_PALISADE || b.type == BLD_WALL_SEGMENT || b.type == BLD_GATE ||
			b.type == BLD_WALL_TOWER)
		return false;
	if (b.team < MAX_PLAYERS && (b.team == player || is_ally(player, b.team))) return false; // UnitAI CanAttack: not our own
	// Capturable.CanCapture: some of the points belong to an enemy of the capturer (or to Gaia).
	float held = 0.0f;
	for (int t = 0; t < MAX_PLAYERS; ++t) {
		const float p = b.capture_points_by_team[t];
		held += p;
		if (p > 0.0f && t != player && is_hostile(player, (uint8_t)t)) return true;
	}
	return b.team >= MAX_PLAYERS && held < b.max_capture_points;
}

void Simulation::destroy_building(Building &b) {
	order_ungarrison(b.id);
	b.garrisoned_units.clear();
	cancel_upgrade(b, b.team);
	stat_building_lost(b, 0xFF);
	b.wrecked = true;
	b.hp = 0.0f;
	b.build_progress = 0.0f;
	record_death((float)b.cell_x, (float)b.cell_y, b.team, b.team, b.type, true); // no killer, no loot
	rebuild_nav_grid();
	if (b.type == BLD_TOWN_CENTER || b.type == BLD_FORTRESS || b.type == BLD_TOWER) rebuild_territory();
}

void Simulation::cmd_delete(uint8_t player, const uint32_t *ids, size_t count) {
	for (size_t k = 0; k < count; ++k) {
		const uint32_t id = ids[k];
		const int32_t i = lookup_unit(id);
		if (i >= 0 && units_.alive[i] && units_.team[i] == player) {
			units_.hp[i] = 0.0f;
			units_.alive[i] = 0;
			units_.action_state[i] = ACTION_DYING;
			order_queues_.erase(id);
			record_unit_death(i, player);
			stat_unit_lost((size_t)i, 0xFF);
			continue;
		}
		auto it = building_id_to_idx_.find(id);
		if (it == building_id_to_idx_.end()) continue;
		Building &b = buildings_[it->second];
		if (b.team != player || b.wrecked) continue;
		// Commands.js delete-entities: not while the owner holds under half the capture points.
		if (b.max_capture_points > 0.0f && b.build_progress >= 1.0f &&
				b.capture_points_by_team[player] < b.max_capture_points * 0.5f)
			continue;
		destroy_building(b);
	}
}

void Simulation::order_back_to_work(const uint32_t *unit_ids, size_t count) {
	for (size_t k = 0; k < count; ++k) {
		const int32_t i = lookup_unit(unit_ids[k]);
		if (i < 0 || !units_.alive[i] || units_.work_kind[i] == ORDER_NONE) continue;
		UnitOrder o;
		o.kind = units_.work_kind[i];
		o.target = units_.work_target[i];
		order_queues_.erase(unit_ids[k]);
		if (o.kind == ORDER_GATHER && !order_still_valid((size_t)i, o)) {
			o.target = find_nearby_resource(units_.pos_x[i], units_.pos_z[i], units_.res_type_carried[i], units_.team[i]);
			if (o.target == 0) continue;
		}
		if (!order_still_valid((size_t)i, o)) continue;
		apply_order(unit_ids[k], o);
	}
}

// ---------------------------------------------------------------------------
// 0 A.D. UnitAI HEAL (Heal.js): a healer heals HEAL_HP every HEAL_INTERVAL_S (after a
// HEAL_PREPARE_S wind-up) to one injured allied Human within its heal range, walking
// up to it first. An idle healer looks for injured allies within its query range;
// once its target is healed it looks again (FINDINGNEWTARGET), queued orders first.
// ---------------------------------------------------------------------------

bool Simulation::can_heal(size_t h, int32_t t) const {
	if (t < 0 || (size_t)t == h || !units_.alive[t]) return false;
	if (!unit_is_human(units_.unit_type[t]) || units_.hp[t] >= units_.max_hp[t]) return false; // IsInjured
	const uint8_t a = units_.team[h], b = units_.team[t];
	return a == b || is_ally(a, b); // IsOwnedByAllyOfPlayer
}

uint32_t Simulation::find_heal_target(size_t h) const {
	// GetQueryRange(IID_Heal): passive healers search their vision too.
	const float vision = unit_vision_range(units_.unit_type[h]);
	const float range = healer_range_of(h);
	float lim = vision;
	if (units_.stance[h] == STANCE_STAND_GROUND) lim = std::min(range, vision);
	else if (units_.stance[h] == STANCE_DEFENSIVE) lim = std::min(range + vision * 0.5f, vision);
	uint32_t best = 0;
	float best_d2 = lim * lim;
	for (size_t t = 0; t < units_.count; ++t) {
		if (!can_heal(h, (int32_t)t)) continue;
		const float dx = units_.pos_x[t] - units_.pos_x[h], dz = units_.pos_z[t] - units_.pos_z[h];
		const float d2 = dx * dx + dz * dz;
		if (d2 <= best_d2) { best_d2 = d2; best = units_.id[t]; }
	}
	return best;
}

void Simulation::start_heal(size_t h, uint32_t target, uint8_t flags) {
	const int32_t t = lookup_unit(target);
	if (t < 0) return;
	set_order(h, ORDER_HEAL, flags, target, units_.pos_x[t], units_.pos_z[t]);
	units_.action_state[h] = ACTION_HEAL;
	units_.target_unit_id[h] = target;
	units_.target_building_id[h] = 0;
	units_.target_resource_id[h] = 0;
	units_.target_x[h] = units_.pos_x[t];
	units_.target_z[h] = units_.pos_z[t];
	units_.waypoint_count[h] = 0;
	units_.has_order[h] = 1;
	units_.attack_timer[h] = std::max(units_.attack_timer[h], HEAL_PREPARE_S);
}

void Simulation::set_unit_hp(uint32_t unit_id, float hp) {
	const int32_t i = lookup_unit(unit_id);
	if (i >= 0 && units_.alive[i]) units_.hp[i] = std::clamp(hp, 1.0f, units_.max_hp[i]);
}

// The dropsite rule tick_gather uses: the unit's own finished Civic Centre or civ dropsite,
// a storehouse for wood/stone/metal, a farmstead or dock for food; boats only at docks.
bool Simulation::dropsite_accepts(size_t i, const Building &b) const {
	if (b.wrecked || b.hp <= 0.0f || b.team != units_.team[i] || b.build_progress < 1.0f) return false;
	const uint8_t rt = units_.res_type_carried[i];
	if (units_.unit_type[i] == UNIT_FISHING_BOAT) return b.type == BLD_DOCK;
	if (b.type == BLD_TOWN_CENTER || civ_building_is_dropsite(b.type)) return true;
	if (b.type == BLD_STOREHOUSE) return rt != RES_FOOD;
	return (b.type == BLD_FARMSTEAD || b.type == BLD_DOCK) && rt == RES_FOOD;
}

bool Simulation::can_return_resource(uint32_t unit_id, uint32_t dropsite_id) const {
	const int32_t i = lookup_unit(unit_id);
	auto bit = building_id_to_idx_.find(dropsite_id);
	return i >= 0 && units_.alive[i] && units_.carry[i] > 0 && bit != building_id_to_idx_.end() &&
			dropsite_accepts((size_t)i, buildings_[bit->second]);
}

void Simulation::order_return_resource(const uint32_t *unit_ids_in, size_t count_in, uint32_t dropsite_id, bool queued) {
	auto bit = building_id_to_idx_.find(dropsite_id);
	if (bit == building_id_to_idx_.end()) return;
	const Building &b = buildings_[bit->second];
	std::vector<uint32_t> carriers;
	for (size_t k = 0; k < count_in; ++k) {
		const int32_t i = lookup_unit(unit_ids_in[k]);
		// Queued: the load may only be picked up by an earlier order.
		if (i >= 0 && units_.alive[i] && (queued || (units_.carry[i] > 0 && dropsite_accepts((size_t)i, b))))
			carriers.push_back(unit_ids_in[k]);
	}
	UnitOrder o; o.kind = ORDER_RETURN_RESOURCE; o.target = dropsite_id; o.x = b.x; o.z = b.z;
	const std::vector<uint32_t> now = split_queued(carriers.data(), carriers.size(), o, queued);
	for (uint32_t uid : now) {
		const int32_t i = lookup_unit(uid);
		if (i < 0 || units_.carry[i] <= 0 || !dropsite_accepts((size_t)i, b)) continue;
		// RETURNRESOURCE.APPROACHING: walk to this dropsite; tick_unit_ai drops the load there.
		uint32_t id = uid;
		cmd_move(&id, 1, b.x, b.z);
		set_order((size_t)i, ORDER_RETURN_RESOURCE, 0, dropsite_id, b.x, b.z);
		units_.target_resource_id[i] = 0;
	}
}

void Simulation::order_heal(const uint32_t *unit_ids_in, size_t count_in, uint32_t target_unit_id, bool queued) {
	const int32_t t = lookup_unit(target_unit_id);
	if (t < 0 || !units_.alive[t]) return;
	UnitOrder o; o.kind = ORDER_HEAL; o.flags = ORDER_FLAG_FORCE; o.target = target_unit_id;
	o.x = units_.pos_x[t]; o.z = units_.pos_z[t];
	const std::vector<uint32_t> now = split_queued(unit_ids_in, count_in, o, queued);
	for (uint32_t uid : now) {
		const int32_t h = lookup_unit(uid);
		if (h < 0 || !units_.alive[h] || units_.unit_type[h] != UNIT_HEALER) continue;
		if (!can_heal((size_t)h, t)) continue;
		start_heal((size_t)h, target_unit_id, ORDER_FLAG_FORCE);
	}
}

void Simulation::tick_heal(size_t h) {
	const int32_t t = lookup_unit(units_.target_unit_id[h]);
	const bool forced = (units_.order_flags[h] & ORDER_FLAG_FORCE) != 0;
	const float vision = unit_vision_range(units_.unit_type[h]);
	bool keep = can_heal(h, t);
	float d2 = 0.0f;
	if (keep) {
		const float dx = units_.pos_x[t] - units_.pos_x[h], dz = units_.pos_z[t] - units_.pos_z[h];
		d2 = dx * dx + dz * dz;
		// ShouldAbandonChase: an automatic target that wanders out of sight is dropped.
		if (!forced && d2 > vision * vision) keep = false;
	}
	if (!keep) { // FINDINGNEWTARGET
		units_.target_unit_id[h] = 0;
		units_.action_state[h] = ACTION_IDLE;
		units_.has_order[h] = 0;
		auto q = order_queues_.find(units_.id[h]);
		if (q != order_queues_.end() && !q->second.empty()) { finish_order(h); return; }
		const uint32_t next = find_heal_target(h);
		if (next != 0) start_heal(h, next, 0);
		else set_order(h, ORDER_NONE, 0, 0, units_.pos_x[h], units_.pos_z[h]);
		return;
	}
	units_.order_x[h] = units_.pos_x[t];
	units_.order_z[h] = units_.pos_z[t];
	const float range = healer_range_of(h);
	if (d2 > range * range) { // APPROACHING
		units_.has_order[h] = 1;
		units_.target_x[h] = units_.pos_x[t];
		units_.target_z[h] = units_.pos_z[t];
		units_.attack_timer[h] = std::max(units_.attack_timer[h], HEAL_PREPARE_S);
		return;
	}
	units_.has_order[h] = 0; // HEALING
	units_.vel_x[h] = units_.vel_z[h] = 0.0f;
	if (units_.attack_timer[h] > 0.0f) return;
	units_.attack_timer[h] = HEAL_INTERVAL_S * std::max(0.1f, tech_mods(units_.team[h]).heal_interval[UNIT_HEALER]);
	const float before = units_.hp[t];
	units_.hp[t] = std::min(units_.max_hp[t], units_.hp[t] + heal_amount_of(h));
	// Heal.js PerformHeal: health healed / max health * the target's Loot/xp.
	if (units_.max_hp[t] > 0.0f)
		gain_xp(h, (units_.hp[t] - before) / units_.max_hp[t] *
				unit_loot(units_.tmpl[t], units_.unit_type[t]).xp * loot_rank_multiplier(units_.rank[t]));
}

// UnitAI CHEERING: CheeringTime standing still after a kill; idle units of the same
// player within notifyToCheerInRange cheer too (CallPlayerOwnedEntitiesFunctionInRange).
void Simulation::start_cheer(size_t i) {
	auto cheer = [&](size_t k) {
		units_.action_state[k] = ACTION_CHEER;
		units_.has_order[k] = 0;
		units_.vel_x[k] = units_.vel_z[k] = 0.0f;
		units_.attack_timer[k] = CHEER_TIME_S;
	};
	cheer(i);
	const uint8_t team = units_.team[i];
	for (size_t k = 0; k < units_.count; ++k) {
		if (k == i || !units_.alive[k] || units_.team[k] != team || units_.action_state[k] != ACTION_IDLE ||
				units_.order_kind[k] != ORDER_NONE || unit_is_siege_engine(units_.unit_type[k]) || unit_is_ship(units_.unit_type[k]))
			continue;
		const float dx = units_.pos_x[k] - units_.pos_x[i], dz = units_.pos_z[k] - units_.pos_z[i];
		if (dx * dx + dz * dz <= CHEER_NOTIFY_RANGE * CHEER_NOTIFY_RANGE) cheer(k);
	}
}

void Simulation::note_kill(uint32_t killer_id, uint32_t victim_id) {
	const int32_t k = lookup_unit(killer_id);
	if (k < 0 || !units_.alive[k] || units_.target_unit_id[k] != victim_id) return;
	if (unit_is_siege_engine(units_.unit_type[k]) || unit_is_ship(units_.unit_type[k])) return; // no CheeringTime
	units_.order_flags[k] |= ORDER_FLAG_CHEER;
}

// Petra keeps a free ring of cells around everything it builds so units can
// always walk around and reach foundations (0 A.D. placement uses obstruction
// clearance for the same reason); tight packing left builders stranded.
bool Simulation::petra_can_place(int cell_x, int cell_y, int footprint, uint8_t btype) const {
	if (!can_place(cell_x - 1, cell_y - 1, footprint + 2)) return false;
	return btype >= BLD_TYPE_COUNT || (check_placement(petra_pid_, cell_x, cell_y, btype) == PLACE_OK &&
	                                   petra_limit_allows_building(btype)); // EntityLimits
}

// EntityLimits for a placement search: counting the player's entities walks every
// unit and structure, so an AI turn asks once per structure type (a new
// foundation changes the count, so it asks again). Cleared each AI turn.
bool Simulation::petra_limit_allows_building(uint8_t btype) const {
	const uint64_t key = ((uint64_t)buildings_.size() << 8) | ((uint64_t)petra_pid_ << 1) | 0x1;
	if (petra_limit_cache_key_ != key) {
		petra_limit_cache_key_ = key;
		petra_limit_cache_.fill(0);
	}
	uint8_t &v = petra_limit_cache_[btype]; // 0 unknown, 1 refused, 2 allowed
	if (v == 0) v = entity_limit_allows_building(petra_pid_, btype) ? 2 : 1;
	return v == 2;
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

	// falloff[r][dz + r][dx + r]: the share of a radius-r source's power at cell offset (dx, dz),
	// max(0.12, 1 - d^2 / (r^2 + 1)) inside the circle d^2 <= r^2 + 1. Computed once, with the same
	// float operations add_power() used per cell.
	static constexpr int FALLOFF_MAX_R = 15; // above every radius below
	struct FalloffTable {
		std::vector<float> cells[FALLOFF_MAX_R + 1];
		std::vector<int> half_width[FALLOFF_MAX_R + 1]; // by dz + r: the circle's |dx| limit, -1 if none
	};
	static const FalloffTable falloff = [] {
		FalloffTable t;
		for (int r = 0; r <= FALLOFF_MAX_R; ++r) {
			const int w = 2 * r + 1;
			const float max_dist_sq = (float)(r * r + 1);
			t.cells[r].assign((size_t)(w * w), 0.0f);
			t.half_width[r].assign((size_t)w, -1);
			for (int dz = -r; dz <= r; ++dz)
				for (int dx = -r; dx <= r; ++dx) {
					float dist_sq = (float)(dx * dx + dz * dz);
					if (dist_sq <= max_dist_sq) {
						t.cells[r][(size_t)((dz + r) * w + dx + r)] = std::max(0.12f, 1.0f - (dist_sq / max_dist_sq));
						t.half_width[r][(size_t)(dz + r)] = std::max(t.half_width[r][(size_t)(dz + r)], dx);
					}
				}
		}
		return t;
	}();

	auto add_power = [&](float *grid, float wx, float wz, float power, int radius_cells) {
		int cx = std::clamp((int)((wx + map_half_) * inv_cell), 0, THREAT_GRID_RES - 1);
		int cz = std::clamp((int)((wz + map_half_) * inv_cell), 0, THREAT_GRID_RES - 1);
		const int r = std::min(radius_cells, FALLOFF_MAX_R), w = 2 * r + 1;
		int x0 = std::max(0, cx - r);
		int x1 = std::min(THREAT_GRID_RES - 1, cx + r);
		int z0 = std::max(0, cz - r);
		int z1 = std::min(THREAT_GRID_RES - 1, cz + r);
		for (int z = z0; z <= z1; ++z) {
			// Each cell inside the circle gets its own share, so the cells' order is free.
			const int hw = falloff.half_width[r][(size_t)(z - cz + r)];
			const int xa = std::max(x0, cx - hw), xb = std::min(x1, cx + hw);
			const float *share = falloff.cells[r].data() + (size_t)((z - cz + r) * w + r); // dx = 0
			float *row = grid + z * THREAT_GRID_RES;
			for (int x = xa; x <= xb; ++x)
				row[x] += power * share[x - cx];
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
	// 1. Update Petra Fog of War from Team 1 vision. A revealed map shows Petra
	// everything too; an explored one starts it with the terrain known.
	const bool revealed = reveal_map_;
	if (revealed) {
		if (!petra_fow_.empty() && petra_fow_[0] != FOW_VISIBLE) std::fill(petra_fow_.begin(), petra_fow_.end(), (uint8_t)FOW_VISIBLE);
	} else {
		const uint8_t floor = explore_map_ ? FOW_EXPLORED : FOW_UNEXPLORED;
		for (auto &c : petra_fow_) c = c == FOW_VISIBLE ? (uint8_t)FOW_EXPLORED : std::max(c, floor);
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

	if (!revealed) {
		ensure_fow_cell_heights();
		reveal_viewers(petra_fow_, petra_fow_views_,
				[&](size_t u) { return units_.alive[u] && units_.team[u] == petra_pid_ ? vision_radius(units_.unit_type[u]) : 0.0f; },
				[&](const Building &b) {
					if (b.team != petra_pid_ || b.build_progress < 0.5f) return 0.0f;
					if (const float cv = civ_building_vision(b.type); cv > 0.0f) return cv; // e.g. the Ptolemaic Lighthouse
					return (b.type == BLD_TOWER || b.type == BLD_FORTRESS) ? 24.0f : ((b.type == BLD_DOCK) ? 28.0f : 18.0f);
				});
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
			// Only an idle rider: hunters keep hunting (petra_manage_hunters).
			if (units_.unit_type[i] == UNIT_CAVALRY && units_.action_state[i] == ACTION_IDLE) {
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

	// Head home with a forced walk, ordered once: re-ordering every turn cost a
	// fresh path search each time.
	auto head_home = [&](float bx, float bz) {
		if (units_.order_kind[s_idx] == ORDER_WALK && units_.action_state[s_idx] == ACTION_MOVE &&
		    std::fabs(units_.order_x[s_idx] - bx) < 1.0f && std::fabs(units_.order_z[s_idx] - bz) < 1.0f) return;
		if ((sx - bx) * (sx - bx) + (sz - bz) * (sz - bz) < 9.0f) return; // home already
		order_walk(&petra_scout_unit_id_, 1, bx, bz, false);
	};

	// Check if scout is in danger
	float local_threat = get_threat_at(sx, sz);
	if (local_threat >= 6.0f) {
		const PetraBase *pbase = get_primary_petra_base();
		head_home(pbase ? pbase->x : 36.0f, pbase ? pbase->z : 36.0f);
		return;
	}

	// In the village phase the scout avoids fights: it turns back when it strays
	// into enemy territory or engages, so it doesn't drag the enemy's army home.
	const bool village = petra_ps().phase < 2;
	auto enemy_land_at = [&](float x, float z) { return petra_foe(territory_owner_at(x, z)); };
	if (village && (enemy_land_at(sx, sz) || units_.action_state[s_idx] == ACTION_ATTACK)) {
		if (const PetraBase *pbase = get_primary_petra_base()) {
			head_home(pbase->x, pbase->z);
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
			order_walk(&petra_scout_unit_id_, 1, target_wx, target_wz, false);
		} else {
			// Exploration sweep: patrol perimeter of friendly territory towards center
			float sweep_x = -origin_x * 0.4f + (float)sim_rng_.range_int(-8, 7);
			float sweep_z = -origin_z * 0.4f + (float)sim_rng_.range_int(-8, 7);
			if (!(village && enemy_land_at(sweep_x, sweep_z)))
				order_walk(&petra_scout_unit_id_, 1, sweep_x, sweep_z, false);
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
					if (dx * dx + dz * dz <= reach * reach && (int)sb.garrisoned_units.size() < sb.garrison_cap &&
					    unit_garrisonable(i) && petra_around(sb).unit) { // else garrisonManager would let it straight out
						uint32_t uid = units_.id[i];
						order_garrison(&uid, 1, sb.id);
					}
				}
			}
		}
	}

	// petra_manage_garrisons lets them out once no enemy is near (garrisonManager.js).
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

	// Our finished buildings that train soldiers (roster `trainers`: barracks,
	// fortress, civ structures such as the gymnasium, and the Town Center and
	// temple for heroes).
	const uint8_t ai_civ = petra_ps().civ;
	uint64_t trainer_types = 0;
	for (uint16_t k : civ_roster_ids(ai_civ))
		trainer_types |= civ_unit_template(k).trainers;
	std::vector<uint32_t> trainers;
	for (const auto &b : buildings_) {
		if (b.team != petra_pid_ || b.build_progress < 1.0f || b.queue.size() >= 4) continue;
		if (trainer_types & bld_bit(b.type)) trainers.push_back(b.id);
	}
	if (trainers.empty()) return;
	if (pop_current(petra_pid_) >= pop_max(petra_pid_)) return;
	const uint8_t phase = petra_ps().phase;

	// A building of ours that trains roster unit `tmpl` (heroes and soldiers
	// only at the Town Center or temple when no barracks or fortress can).
	auto trainer_for = [&](uint16_t tmpl) -> uint32_t {
		const CivUnitTemplate &t = civ_unit_template(tmpl);
		const bool hero = (t.flags & ROSTER_HERO) != 0;
		for (uint32_t bid : trainers) {
			auto bit = building_id_to_idx_.find(bid);
			if (bit == building_id_to_idx_.end()) continue;
			const Building &b = buildings_[bit->second];
			if (!(t.trainers & bld_bit(b.type))) continue;
			if (!hero && (b.type == BLD_TOWN_CENTER || b.type == BLD_TEMPLE)) continue;
			if (b.queue.size() >= 4 || !can_train_at(petra_pid_, t.unit_type, b.type, tmpl)) continue;
			return bid;
		}
		return 0;
	};

	// 1. Emergency Defense (Priority Queue 0: Threat >= 7.0f): the civ's
	// spearmen, then its archers, from any barracks.
	if (is_emergency) {
		for (UnitType type : { UNIT_SPEARMAN, UNIT_ARCHER }) {
			const uint16_t tmpl = civ_default_template(ai_civ, type);
			if (tmpl == CIV_UNIT_TEMPLATE_NONE || civ_unit_template(tmpl).phase > phase) continue;
			for (int n = 0; n < 4; ++n) {
				const uint32_t bid = trainer_for(tmpl);
				if (!bid || !petra_train_template(PETRA_QUEUE_EMERGENCY, bid, tmpl)) break;
			}
		}
		return;
	}

	// 2. Military Strike Force Production (Priority Queue 2) — Counter-Units to supply AttackPlan squads
	// A mustering rush or raid trains its own units (attackPlan.js
	// trainMoreUnits); a raid wants cavalry citizen soldiers.
	const bool rush_wants = petra_attack_plan_.state == AttackPlan::MUSTER &&
		petra_attack_mgr_.kind == PETRA_ATTACK_RUSH &&
		petra_attack_plan_.squad.size() < (size_t)std::max(1, petra_attack_mgr_.target_size);
	const bool raid_wants = petra_raid_plan_.state == AttackPlan::MUSTER && petra_raid_plan_.squad.size() < 4 &&
		petra_raid_target_ok(petra_raid_plan_.target_building_id);
	// Our soldiers do not gather, so the army otherwise waits until the economy
	// is mostly staffed; else the village phase never reaches popPhase2.
	if (!rush_wants && !raid_wants) {
		int workers = 0;
		for (size_t i = 0; i < units_.count; ++i)
			if (units_.alive[i] && units_.team[i] == petra_pid_ &&
			    (units_.unit_type[i] == UNIT_VILLAGER || units_.unit_type[i] == UNIT_FEMALE_CITIZEN))
				++workers;
		const float staffed = (phase == 1) ? 0.8f : 0.5f;
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

	// What we field already, queued orders included (attackPlan.js unitStat
	// targetSize: one hero, a few siege engines).
	int own_heroes = 0, own_siege = 0, own_towers = 0;
	auto is_tower = [](uint16_t tmpl) {
		return tmpl < CIV_UNIT_TEMPLATE_COUNT && (civ_unit_template(tmpl).flags & ROSTER_SIEGE_TOWER);
	};
	auto is_siege = [](uint8_t t) { return t == UNIT_CATAPULT || t == UNIT_SIEGE_RAM || t == UNIT_BOLT_SHOOTER; };
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != petra_pid_) continue;
		if (units_.unit_type[i] == UNIT_HERO) ++own_heroes;
		if (is_siege(units_.unit_type[i])) ++own_siege;
		if (is_tower(units_.tmpl[i])) ++own_towers;
	}
	for (const auto &b : buildings_) {
		if (b.team != petra_pid_) continue;
		for (const auto &o : b.queue) {
			if (o.unit_type == UNIT_HERO) ++own_heroes;
			if (is_siege(o.unit_type)) ++own_siege;
			if (is_tower(o.tmpl)) ++own_towers;
		}
	}
	const int siege_cap = (phase >= 3) ? 4 : 2;

	int total_enemy_mil = enemy_infantry + enemy_spearmen + enemy_archers + enemy_cavalry + enemy_siege + enemy_champions;
	float norm = 1.0f / (float)std::max(1, total_enemy_mil);

	// Dynamic Counter-Unit Utility Scores (0 A.D. Petra combat counters)
	float spearman_score = 1.2f + (enemy_cavalry * 4.5f + enemy_champions * 3.5f) * norm;
	float archer_score   = 1.0f + (enemy_infantry * 3.0f + enemy_spearmen * 3.5f) * norm;
	float swordsman_score = 1.0f + (enemy_spearmen * 1.8f) * norm;
	float cavalry_score  = 0.8f + (enemy_archers * 4.0f + enemy_siege * 3.0f) * norm;
	if (raid_wants) cavalry_score += 4.0f;
	float siege_score    = (phase >= 2) ? (1.5f + (float)enemy_forts * 2.5f) : 0.2f;
	if (petra_attack_plan_.type == AttackPlan::PLAN_SIEGE && phase >= 2) {
		siege_score += 5.0f; // Prioritize siege engines before assault
	}
	float champion_score = (phase >= 3) ? 2.5f : 0.0f;
	auto class_score = [&](uint8_t t) -> float {
		switch (t) {
			case UNIT_SPEARMAN: return spearman_score;
			case UNIT_ARCHER: case UNIT_ARCHER_MAIDEN: return archer_score;
			case UNIT_SWORDSMAN: case UNIT_WAR_DOG: return swordsman_score;
			case UNIT_CAVALRY: case UNIT_CAMEL: return cavalry_score;
			case UNIT_CHARIOT: return cavalry_score * 0.95f;
			case UNIT_ELEPHANT: case UNIT_SCYTHED_CHARIOT: return champion_score;
			case UNIT_CATAPULT: return siege_score;
			case UNIT_SIEGE_RAM: return siege_score * 0.95f;
			case UNIT_BOLT_SHOOTER: return siege_score * 0.85f;
			default: return -1.0f; // support units, ships, healers: trained elsewhere
		}
	};

	// Score every roster unit our buildings can train now: citizen soldiers by
	// counters, champions and the hero in the city phase, siege up to a cap.
	struct Option {
		uint16_t tmpl;
		float score;
		uint32_t bld_id;
	};
	std::vector<Option> options;
	for (uint16_t tmpl : civ_roster_ids(ai_civ)) {
		const CivUnitTemplate &t = civ_unit_template(tmpl);
		if (t.phase > phase) continue;
		float score;
		if (t.flags & ROSTER_HERO) {
			if (own_heroes > 0 || phase < 3) continue;
			score = champion_score * 1.2f;
		} else if (t.flags & ROSTER_SIEGE_TOWER) {
			// One tower to carry soldiers over walls, once the siege train has started.
			if (own_siege >= siege_cap || own_towers > 0) continue;
			score = siege_score * (own_siege > 0 ? 1.1f : 0.8f);
		} else {
			score = class_score(t.unit_type);
			if (score < 0.0f) continue;
			// Village-phase armies stay infantry; mounted units come with the town phase.
			if (phase < 2 && !raid_wants && (t.unit_type == UNIT_CAVALRY || t.unit_type == UNIT_CAMEL || t.unit_type == UNIT_CHARIOT)) continue;
			if (is_siege(t.unit_type) && own_siege >= siege_cap) continue;
			if (t.flags & ROSTER_CHAMPION) {
				if (phase < 3) continue;
				score = champion_score + 0.3f * score;
			}
			if (t.flags & ROSTER_MERCENARY) score *= 0.9f;
			// Among units of one class, the civ's standard one first.
			if (tmpl == civ_default_template(ai_civ, t.unit_type)) score += 0.05f;
		}
		const uint32_t bid = trainer_for(tmpl);
		if (bid) options.push_back({tmpl, score, bid});
	}

	std::stable_sort(options.begin(), options.end(), [](const Option &a, const Option &b) {
		return a.score > b.score;
	});

	// A siege tower we cannot pay for yet holds the siege slot: cheaper siege
	// engines don't take it in the meantime (queueManager waits for its plan).
	bool siege_waiting = false;
	for (const auto &opt : options) {
		if (pop_current(petra_pid_) >= pop_max(petra_pid_)) break;
		const bool siege_opt = is_siege(civ_unit_template(opt.tmpl).unit_type);
		if (siege_opt && siege_waiting) continue;
		if (petra_train_template(PETRA_QUEUE_MILITARY, opt.bld_id, opt.tmpl)) break;
		if (is_tower(opt.tmpl)) siege_waiting = true;
	}
}

// Queue roster unit `tmpl` at one of our buildings, paid from Petra's queue
// account: cmd_train_template's checks, prices and training time.
bool Simulation::petra_train_template(uint8_t queue, uint32_t building_id, uint16_t tmpl) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end() || tmpl >= CIV_UNIT_TEMPLATE_COUNT) return false;
	Building &b = buildings_[bit->second];
	const uint8_t pid = petra_pid_;
	if (b.team != pid || b.build_progress < 1.0f || b.queue.size() >= 8 || b.upgrade_to != BLD_TYPE_COUNT) return false;
	const CivUnitTemplate &t = civ_unit_template(tmpl);
	if (t.civ != players_[pid].civ || t.phase > players_[pid].phase) return false;
	if (pop_current(pid) >= pop_max(pid) || !can_train_at(pid, t.unit_type, b.type, tmpl)) return false;
	const UnitTypeInfo &uinfo = civ_template_unit_info(tmpl, t.unit_type);
	float cost[RES_COUNT];
	unit_train_cost(pid, tmpl, uinfo, cost);
	if (!petra_try_pay(queue, cost[RES_FOOD], cost[RES_WOOD], cost[RES_GOLD], cost[RES_STONE])) return false;
	ProductionOrder order;
	order.unit_type = t.unit_type;
	order.tmpl = tmpl;
	order.timer = 0.0f;
	order.duration = uinfo.train_time * team_bonus_mult(pid, TEAM_BONUS_UNIT_TRAIN_TIME, -1, tmpl)
		* unit_tech_mods(pid, t.unit_type, tmpl).train_time * ai_time_mult(pid) * promotion_train_time_mult(tmpl);
	b.queue.push_back(order);
	return true;
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

// What Petra wants in stock for its next plans (phase, techs, attack, houses):
// petra_manage_economy splits its workers by the shortfall against it.
void Simulation::petra_resource_demand(float out[RES_COUNT]) const {
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
	out[RES_FOOD] = demand_food;
	out[RES_WOOD] = demand_wood;
	out[RES_GOLD] = demand_gold;
	out[RES_STONE] = demand_stone;
}

void Simulation::petra_manage_economy(float dt) {
	petra_econ_timer_ += dt;
	if (petra_econ_timer_ < 1.0f) return;
	petra_econ_timer_ = 0.0f;

	// 1. Priority Queue 1: Economic Boom — keep the TCs training workers
	// until the worker target (config.js Economy.targetNumWorkers) is met.
	petra_staff_foundations();
	PetraHuntScan hunt_scan = petra_hunt_scan();

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
		} else if (units_.action_state[i] == ACTION_ATTACK && is_fauna_id(units_.target_unit_id[i])) {
			workers_food++; // hunting: the carcass is gathered next
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
	float demand[RES_COUNT];
	petra_resource_demand(demand);
	const float demand_food = demand[RES_FOOD], demand_wood = demand[RES_WOOD];
	const float demand_gold = demand[RES_GOLD], demand_stone = demand[RES_STONE];

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
			if (resources_.subtype[r] == RES_SUB_FISH) continue; // fishing boats' food, not villagers'
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

		// worker.js startGathering: any gatherer takes a treasure nearby first, and a food
		// gatherer hunts next (livestock and carcasses near a dropsite).
		if (petra_gather_treasure(widx, hunt_scan)) continue;
		if (chosen_res_type == RES_FOOD && petra_start_hunting(widx, hunt_scan)) {
			workers_food++;
			continue;
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

	petra_manage_hunters(hunt_scan);
	petra_manage_fields(target_food);
}

// 0 A.D. distances are metres squared; one RTS0 unit is 4 m, so they divide by 16.
static constexpr float PETRA_HUNT_FAR_D2 = 25000.0f / 16.0f;      // only FastMoving hunt further
static constexpr float PETRA_HUNT_DROP_FAST_D2 = 35000.0f / 16.0f; // supply to food dropsite, FastMoving
static constexpr float PETRA_HUNT_DROP_FLEE_D2 = 7000.0f / 16.0f;  // ... others on an animal that flees
static constexpr float PETRA_HUNT_DROP_D2 = 12000.0f / 16.0f;      // ... others on livestock or a carcass

Simulation::PetraHuntScan Simulation::petra_hunt_scan() const {
	PetraHuntScan scan;
	for (const auto &b : buildings_) {
		if (b.team != petra_pid_ || b.hp <= 0 || b.build_progress < 1.0f) continue;
		if (b.type == BLD_TOWN_CENTER || b.type == BLD_FARMSTEAD || b.type == BLD_DOCK || civ_building_is_dropsite(b.type))
			scan.food_drops.push_back({b.x, b.z});
	}
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i]) continue;
		const uint32_t tid = units_.target_unit_id[i];
		if (tid != 0 && units_.action_state[i] == ACTION_ATTACK && is_fauna_id(tid) && lookup_unit(tid) < 0)
			scan.on_animal[tid]++;
		else if (units_.target_resource_id[i] != 0)
			scan.on_carcass[units_.target_resource_id[i]]++;
		if (units_.team[i] == petra_pid_ && units_.order_kind[i] == ORDER_COLLECT_TREASURE)
			scan.treasures_taken.push_back(units_.order_target[i]);
	}
	return scan;
}

// entityExtend.js gatherTreasure: the nearest treasure on the unit's land outside
// enemy territory, within 120000 m² (14000 m² outside our own territory, as the
// AI has no line of sight). One collector per treasure stands in for 0 A.D.'s
// 20 s "lastGathered" wait between tries.
bool Simulation::petra_gather_treasure(size_t u, PetraHuntScan &scan) {
	if (treasures_.empty()) return false;
	const uint8_t ut = units_.unit_type[u];
	if (unit_is_ship(ut) || ut == UNIT_WAR_DOG) return false; // isTreasureCollector
	const uint8_t pass = formation_unit_mounted(ut) ? PASS_CAVALRY : PASS_INFANTRY;
	const float px = units_.pos_x[u], pz = units_.pos_z[u];
	auto [ucx, ucz] = nav_grid_.world_to_cell(px, pz);
	uint32_t best = 0;
	float best_d2 = 1e18f;
	for (const auto &t : treasures_) {
		if (std::find(scan.treasures_taken.begin(), scan.treasures_taken.end(), t.id) != scan.treasures_taken.end()) continue;
		const int terr = territory_owner_at(t.x, t.z);
		if (terr >= 0 && !is_ally(petra_pid_, (uint8_t)terr)) continue;
		const float d2 = (t.x - px) * (t.x - px) + (t.z - pz) * (t.z - pz);
		if (d2 > 120000.0f / 16.0f || (terr != (int)petra_pid_ && d2 > 14000.0f / 16.0f)) continue;
		if (d2 > best_d2) continue;
		auto [cx, cz] = nav_grid_.world_to_cell(t.x, t.z);
		if (nav_grid_.is_passable_for(cx, cz, pass) && !nav_grid_.reachable(ucx, ucz, cx, cz, pass)) continue;
		best_d2 = d2;
		best = t.id;
	}
	if (best == 0) return false;
	order_collect_treasure(&units_.id[u], 1, best);
	scan.treasures_taken.push_back(best);
	return true;
}

// worker.js startHunting: the nearest live animal or carcass from (px, pz) that
// is worth one more gatherer, on the hunter's land, outside enemy territory and
// close enough to one of our food dropsites. Only FastMoving (cavalry) and
// ranged units chase animals that flee, and only FastMoving hunt far away.
uint32_t Simulation::petra_find_hunt(size_t u, float px, float pz, const PetraHuntScan &scan, bool &live) const {
	if (scan.food_drops.empty()) return 0;
	const uint8_t ut = units_.unit_type[u];
	const bool fast = formation_unit_mounted(ut);
	const bool ranged = template_has_ranged_attack(units_.tmpl[u], ut, civ_of(petra_pid_));
	const uint8_t pass = fast ? PASS_CAVALRY : PASS_INFANTRY;
	auto [ucx, ucz] = nav_grid_.world_to_cell(units_.pos_x[u], units_.pos_z[u]);

	uint32_t best = 0;
	float best_d2 = 1e18f;
	auto consider = [&](uint32_t id, float x, float z, int amount, int max_gatherers, int gatherers, bool can_flee,
	                    int owner, bool is_live) {
		if (max_gatherers > 0 && gatherers >= max_gatherers) return; // IsSupplyFull
		if (gatherers > 0 && amount / (1 + gatherers) < 30) return;
		if (can_flee && !fast && !ranged) return;
		const float d2 = (x - px) * (x - px) + (z - pz) * (z - pz);
		if (d2 > best_d2) return;
		if (!fast && d2 > PETRA_HUNT_FAR_D2) return;
		const int terr = territory_owner_at(x, z);
		if (terr >= 0 && !is_ally(petra_pid_, (uint8_t)terr)) return; // avoid enemy territory
		if (terr >= 0 && terr != (int)petra_pid_ && owner == terr) return; // nor an ally's cattle
		if (!fast && can_flee && terr < 0) return;
		const float drop_d2 = fast ? PETRA_HUNT_DROP_FAST_D2 : (can_flee ? PETRA_HUNT_DROP_FLEE_D2 : PETRA_HUNT_DROP_D2);
		bool near_drop = false;
		for (const auto &d : scan.food_drops)
			if ((x - d.first) * (x - d.first) + (z - d.second) * (z - d.second) < drop_d2) { near_drop = true; break; }
		if (!near_drop) return;
		auto [cx, cz] = nav_grid_.world_to_cell(x, z);
		// getLandAccess; a supply on a blocked cell (by a tree, a wreck) is reached from beside it.
		if (nav_grid_.is_passable_for(cx, cz, pass) && !nav_grid_.reachable(ucx, ucz, cx, cz, pass)) return;
		best_d2 = d2;
		best = id;
		live = is_live;
	};

	for (size_t f = 0; f < fauna_.count; ++f) {
		if (!fauna_.alive[f] || fauna_.hp[f] <= 0.0f || fauna_.food_amount[f] <= 0) continue;
		const auto &info = fauna_type_info(fauna_.fauna_type[f]);
		if (info.is_naval() || (info.flags & FAUNA_F_FISH)) continue; // isHuntable skips SeaCreatures
		if (fauna_.behavior[f] == FAUNA_HERDED) continue;
		int owner = -1;
		if (fauna_.behavior[f] == FAUNA_CORRALLED) {
			auto bit = building_id_to_idx_.find(fauna_.corral_id[f]);
			if (bit != building_id_to_idx_.end()) owner = buildings_[bit->second].team;
			if (owner >= 0 && owner != (int)petra_pid_) continue; // someone else's livestock
		}
		auto git = scan.on_animal.find(fauna_.id[f]);
		consider(fauna_.id[f], fauna_.pos_x[f], fauna_.pos_z[f], fauna_.food_amount[f], info.max_gatherers,
		         git == scan.on_animal.end() ? 0 : git->second, !info.is_domestic(), owner, true);
	}
	for (size_t r = 0; r < resources_.count; ++r) {
		if (!resources_.alive[r] || resources_.amount[r] <= 0 || resources_.subtype[r] != RES_SUB_MEAT) continue;
		auto git = scan.on_carcass.find(resources_.id[r]);
		consider(resources_.id[r], resources_.x[r], resources_.z[r], resources_.amount[r], 0,
		         git == scan.on_carcass.end() ? 0 : git->second, false, -1, false);
	}
	return best;
}

bool Simulation::petra_start_hunting(size_t u, PetraHuntScan &scan) {
	if (petra_gather_treasure(u, scan)) return true; // "First look for possible treasure if any"
	bool live = false;
	const uint32_t target = petra_find_hunt(u, units_.pos_x[u], units_.pos_z[u], scan, live);
	if (target == 0) return false;
	uint32_t uid = units_.id[u];
	if (live) {
		order_hunt(&uid, 1, target);
		scan.on_animal[target]++;
	} else {
		order_gather(&uid, 1, target);
		scan.on_carcass[target]++;
	}
	return true;
}

// startingStrategy.js buildFirstBase / headquarters.js checkBaseExpansion: with
// no civic centre (a nomad start, or the last one lost) Petra founds one where
// most of its workers stand. Short of resources, it first waits on treasures
// its units can collect, else lays out a dock to gather from if it has none.
void Simulation::petra_build_first_base() {
	for (const auto &b : buildings_)
		if (b.team == petra_pid_ && b.hp > 0.0f && !b.wrecked && b.type == BLD_TOWN_CENTER) return; // hasPotentialBase
	PetraHuntScan scan = petra_hunt_scan();
	uint8_t goal = BLD_TOWN_CENTER;
	const BuildingTypeInfo &cc = building_type_info(BLD_TOWN_CENTER);
	auto affords = [&](const BuildingTypeInfo &t, float f, float w, float g, float st) {
		return f >= (float)t.food_cost && w >= (float)t.wood_cost && g >= (float)t.gold_cost && st >= (float)t.stone_cost;
	};
	if (!affords(cc, petra_ps().food(), petra_ps().wood(), petra_ps().gold(), petra_ps().stone())) {
		float ef = petra_ps().food(), ew = petra_ps().wood(), eg = petra_ps().gold(), es = petra_ps().stone();
		for (size_t i = 0; i < units_.count; ++i) {
			if (!units_.alive[i] || units_.team[i] != petra_pid_) continue;
			if (units_.action_state[i] == ACTION_IDLE) petra_gather_treasure(i, scan);
			if (units_.order_kind[i] != ORDER_COLLECT_TREASURE) continue;
			for (const auto &t : treasures_) {
				if (t.id != units_.order_target[i]) continue;
				const auto &a = treasure_type_info(t.type).amount; // food, wood, stone, metal
				ef += (float)a[0]; ew += (float)a[1]; es += (float)a[2]; eg += (float)a[3];
			}
			if (affords(cc, ef, ew, eg, es)) return; // wait for the treasures
		}
		for (const auto &b : buildings_)
			if (b.team == petra_pid_ && b.hp > 0.0f && b.type == BLD_DOCK) return;
		if (!affords(building_type_info(BLD_DOCK), petra_ps().food(), petra_ps().wood(), petra_ps().gold(), petra_ps().stone()))
			return;
		goal = BLD_DOCK;
	}

	// The starting point: where most workers stand (on one land, within 2500 m²).
	struct Point { float x, z; uint16_t land; int weight; };
	std::vector<Point> points;
	std::vector<uint32_t> workers;
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != petra_pid_ || carry_cap(i, RES_WOOD) <= 0) continue; // Worker
		const uint8_t ut = units_.unit_type[i];
		if (formation_unit_mounted(ut) || unit_is_ship(ut)) continue; // FastMoving
		workers.push_back(units_.id[i]);
		const float x = units_.pos_x[i], z = units_.pos_z[i];
		auto [cx, cz] = nav_grid_.world_to_cell(x, z);
		const uint16_t land = nav_grid_.global_region_at(cx, cz, PASS_INFANTRY);
		bool found = false;
		for (auto &p : points) {
			if (p.land != land || (p.x - x) * (p.x - x) + (p.z - z) * (p.z - z) > 2500.0f / 16.0f) continue;
			++p.weight;
			found = true;
			break;
		}
		if (!found) points.push_back({x, z, land, 1});
	}
	if (points.empty()) return;
	size_t best = 0;
	for (size_t k = 1; k < points.size(); ++k)
		if (points[k].weight > points[best].weight) best = k;

	// The civilCentre queue outranks every other (queueManager.js priority 950): pay
	// from the stockpile at once, ahead of this tick's other spending.
	const BuildingTypeInfo &info = building_type_info(goal);
	const int sx = (int)(points[best].x / GRID), sz = (int)(points[best].z / GRID);
	const int max_r = goal == BLD_DOCK ? 24 : 12;
	for (int r = 0; r <= max_r; ++r) {
		for (int dx = -r; dx <= r; ++dx) {
			for (int dz = -r; dz <= r; ++dz) {
				if (std::abs(dx) != r && std::abs(dz) != r) continue;
				if (!petra_can_place(sx + dx, sz + dz, info.footprint, goal)) continue;
				petra_ps().food() -= (float)info.food_cost; petra_ps().wood() -= (float)info.wood_cost;
				petra_ps().gold() -= (float)info.gold_cost; petra_ps().stone() -= (float)info.stone_cost;
				const uint32_t bid = spawn_building(sx + dx, sz + dz, goal, petra_pid_, 0.0f);
				if (bid != 0 && !workers.empty()) order_build_or_repair(workers.data(), workers.size(), bid);
				return;
			}
		}
	}
}

// worker.js "hunter" subrole: Petra's citizen cavalry hunt (baseManager.js
// reassignIdleWorkers gives it to FastMoving units that gather food and can
// attack animals). An idle hunter looks around itself, then around each base
// and rides there; one that drifted into enemy territory looks again.
void Simulation::petra_manage_hunters(PetraHuntScan &scan) {
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != petra_pid_ || units_.unit_type[i] != UNIT_CAVALRY) continue;
		if (carry_cap(i, RES_FOOD) <= 0) continue; // champion cavalry gather nothing
		const uint32_t uid = units_.id[i];
		const uint8_t st = units_.action_state[i];
		const bool idle = st == ACTION_IDLE;
		if (!idle) {
			// Hunting units that drifted into enemy territory during the chase go home.
			const bool hunting = (st == ACTION_ATTACK && is_fauna_id(units_.target_unit_id[i])) ||
			                     ((st == ACTION_GATHER || st == ACTION_RETURN_DROPSITE) && units_.res_type_carried[i] == RES_FOOD);
			if (hunting && petra_foe(territory_owner_at(units_.pos_x[i], units_.pos_z[i]))) petra_start_hunting(i, scan);
			if (uid != petra_scout_unit_id_) continue;
		}
		if (uid != petra_scout_unit_id_ && petra_unit_reserved(uid)) continue;
		if (uid == petra_scout_unit_id_) {
			// The hunt comes first (0 A.D. Petra has no scout); the scout goes back to it.
			bool live = false;
			bool any = petra_find_hunt(i, units_.pos_x[i], units_.pos_z[i], scan, live) != 0;
			for (size_t b = 0; !any && b < petra_bases_.size(); ++b)
				any = petra_find_hunt(i, petra_bases_[b].x, petra_bases_[b].z, scan, live) != 0;
			if (!any) continue;
			petra_scout_unit_id_ = 0;
		}
		if (petra_start_hunting(i, scan)) continue;
		// Nothing around: try another base.
		for (const auto &base : petra_bases_) {
			bool live = false;
			if (petra_find_hunt(i, base.x, base.z, scan, live) == 0) continue;
			order_walk(&units_.id[i], 1, base.x, base.z, false);
			break;
		}
	}
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
		if (resources_.subtype[r] == RES_SUB_FISH) continue; // villagers cannot gather fish
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

// tradeManager.js performBarter, at most one deal a pass. Necessity: buy a
// resource the queues wait on that our gather rate won't bring in 30 s, selling
// one we have 500 spare of and don't need, if the rate beats a floor that drops
// with the stock. Contingency: sell spare food for the scarcest finite resource.
bool Simulation::petra_perform_barter() {
	if (!can_barter(petra_pid_)) return false;
	static constexpr uint8_t kCodes[RES_COUNT] = {RES_FOOD, RES_WOOD, RES_STONE, RES_GOLD}; // Resources.GetCodes()
	float avail[RES_COUNT], needs[RES_COUNT] = {}, rates[RES_COUNT];
	const PlayerState &ps = petra_ps();
	for (int r = 0; r < RES_COUNT; ++r) {
		avail[r] = ps.res[r];
		rates[r] = ps.gathered_window[r] / 10.0f; // per second (the HUD's per-minute rate / 60)
	}
	for (int q = 0; q < PetraQueueManager::QUEUE_COUNT; ++q) {
		const PetraQueueAccount &a = petra_queues_.accounts[q], &w = petra_queues_.wants[q];
		avail[RES_FOOD] -= a.food; avail[RES_WOOD] -= a.wood; avail[RES_GOLD] -= a.gold; avail[RES_STONE] -= a.stone;
		needs[RES_FOOD] += w.food; needs[RES_WOOD] += w.wood; needs[RES_GOLD] += w.gold; needs[RES_STONE] += w.stone;
	}
	for (int r = 0; r < RES_COUNT; ++r) needs[r] = std::max(0.0f, needs[r] - ps.res[r]); // currentNeeds
	auto barter_rate = [&](uint8_t buy, uint8_t sell) {
		return (int)std::lround(100.0f * market_sell_price(sell) / market_buy_price(buy));
	};
	for (uint8_t buy : kCodes) {
		if (needs[buy] == 0.0f || needs[buy] < rates[buy] * 30.0f) continue;
		int best_sell = -1, best_rate = 0;
		for (uint8_t sell : kCodes) {
			if (sell == buy || needs[sell] > 0.0f || avail[sell] < 500.0f) continue;
			int rate_min;
			if (sell == RES_FOOD) {
				rate_min = avail[sell] > 40000.0f ? 0 : avail[sell] > 15000.0f ? 5 : avail[sell] > 1000.0f ? 10 : 30;
			} else {
				rate_min = avail[sell] > 5000.0f ? 30 : avail[sell] > 1000.0f ? 50 : 70;
				if (buy == RES_FOOD) rate_min += 20;
			}
			const int rate = barter_rate(buy, sell);
			if (rate > best_rate && rate > rate_min) { best_rate = rate; best_sell = sell; }
		}
		if (best_sell >= 0) {
			const int amount = avail[best_sell] > 5000.0f ? 500 : 100;
			return cmd_barter(petra_pid_, (ResourceType)best_sell, (ResourceType)buy, amount);
		}
	}
	if (avail[RES_FOOD] < 1000.0f || needs[RES_FOOD] > 0.0f) return false;
	int best_buy = -1;
	float best_choice = 0.0f;
	for (uint8_t buy : kCodes) {
		if (buy == RES_FOOD) continue;
		int rate_min = 80;
		if (avail[buy] < 5000.0f && avail[RES_FOOD] > 5000.0f) rate_min -= 20 - (int)std::floor(avail[buy] / 250.0f);
		const int rate = barter_rate(buy, RES_FOOD);
		if (rate < rate_min) continue;
		const float choice = (float)rate / (100.0f + avail[buy]);
		if (choice > best_choice) { best_choice = choice; best_buy = buy; }
	}
	if (best_buy < 0) return false;
	return cmd_barter(petra_pid_, RES_FOOD, (ResourceType)best_buy, avail[RES_FOOD] > 5000.0f ? 500 : 100);
}

// tradeManager.js setTradingGoods: a share for each finite resource we hold
// little of (more when our plans want it), then 70% of the rest for the most
// needed resource and 30% for the next one if it is wanted too.
void Simulation::petra_set_trading_goods() {
	float demand[RES_COUNT];
	petra_resource_demand(demand);
	const PlayerState &ps = petra_ps();
	float short_of[RES_COUNT];
	for (int r = 0; r < RES_COUNT; ++r) short_of[r] = demand[r] - ps.res[r]; // > 0: wanted
	uint8_t goods[RES_COUNT] = {};
	int remaining = 100;
	for (uint8_t r : {RES_WOOD, RES_STONE, RES_GOLD}) {
		const bool wanted = short_of[r] > 0.0f;
		if (ps.res[r] < 200.0f) goods[r] = wanted ? 20 : 10;
		else if (ps.res[r] < 500.0f) goods[r] = wanted ? 15 : 10;
		else if (ps.res[r] < 1000.0f) goods[r] = 10;
		remaining -= goods[r];
	}
	// pickMostNeededResources: the largest shortfall first.
	uint8_t order[RES_COUNT] = {RES_FOOD, RES_WOOD, RES_STONE, RES_GOLD};
	std::stable_sort(order, order + RES_COUNT, [&](uint8_t a, uint8_t b) { return short_of[a] > short_of[b]; });
	const int main_need = remaining * 70 / 100;
	goods[order[0]] += main_need;
	goods[short_of[order[1]] > 0.0f ? order[1] : order[0]] += remaining - main_need;
	cmd_set_trading_goods(petra_pid_, goods);
}

void Simulation::petra_manage_trading(float dt) {
	petra_trade_timer_ += dt;
	if (petra_trade_timer_ < 2.0f) return;
	petra_trade_timer_ = 0.0f;

	if (petra_ps().phase < 2) return;
	petra_perform_barter();
	// Every 60 AI turns in 0 A.D.; here every 20 s of game time while a route exists.
	if (has_trade_route(petra_pid_) && (int)(petra_timer_ / 20.0f) != (int)((petra_timer_ - 2.0f) / 20.0f))
		petra_set_trading_goods();

	// 1. Build Market if AI has none, or build secondary Market at forward base
	int market_count = 0;
	std::vector<uint32_t> market_ids;
	for (const auto &b : buildings_) {
		if (b.team == petra_pid_ && b.type == BLD_MARKET) {
			market_count++;
			if (b.build_progress >= 1.0f) market_ids.push_back(b.id);
		}
	}

	// Allied markets we could trade with (tradeManager.js counts allies' markets as route ends).
	std::vector<const Building *> ally_markets;
	for (const auto &b : buildings_)
		if (b.team != petra_pid_ && b.type == BLD_MARKET && b.team < MAX_PLAYERS && is_ally(petra_pid_, b.team) &&
		    can_trade_at(petra_pid_, b))
			ally_markets.push_back(&b);

	if (market_count == 0 && !ally_markets.empty()) {
		// findMarketLocation: in our territory, as far as we can get from the
		// allied market we would trade with (the gain grows with distance).
		float base_x = 36.0f, base_z = 36.0f;
		if (const auto *pbase = get_primary_petra_base()) {
			base_x = pbase->x; base_z = pbase->z;
		}
		const int start_cx = (int)(base_x / GRID), start_cz = (int)(base_z / GRID);
		int best_cx = 0, best_cz = 0;
		float best_d2 = -1.0f;
		for (int dx = -14; dx <= 14; ++dx) {
			for (int dz = -14; dz <= 14; ++dz) {
				if (std::abs(dx) < 4 && std::abs(dz) < 4) continue;
				const int cx = start_cx + dx, cz = start_cz + dz;
				const float wx = cx * GRID, wz = cz * GRID;
				if (territory_owner_at(wx, wz) != petra_pid_ || !petra_can_place(cx, cz, 2, BLD_MARKET) ||
				    !is_position_safe(wx, wz, 6.0f)) continue;
				float far2 = 0.0f;
				for (const Building *m : ally_markets)
					far2 = std::max(far2, (m->x - wx) * (m->x - wx) + (m->z - wz) * (m->z - wz));
				if (far2 > best_d2) { best_d2 = far2; best_cx = cx; best_cz = cz; }
			}
		}
		if (best_d2 >= 0.0f && petra_try_pay(PETRA_QUEUE_TRADE, 0.0f, 200.0f, 100.0f, 0.0f)) {
			uint32_t bid = spawn_building(best_cx, best_cz, BLD_MARKET, petra_pid_, 0.0f);
			auto idle_workers = get_idle_workers(petra_pid_);
			if (!idle_workers.empty())
				order_build_or_repair(idle_workers.data(), std::min((size_t)3, idle_workers.size()), bid);
		}
	} else if (market_count == 0) {
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
					if (petra_can_place(cx, cz, 2, BLD_MARKET) && is_position_safe(cx * GRID, cz * GRID, 6.0f) &&
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
	} else if (market_count == 1 && ally_markets.empty() && petra_bases_.size() >= 2) {
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
					if (petra_can_place(cx, cz, 2, BLD_MARKET) && is_position_safe(cx * GRID, cz * GRID, 6.0f) &&
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
	// Traders need two markets to trade between (own or allied); without a route they would idle.
	if (ai_traders < target_traders && !market_ids.empty() && has_trade_route(petra_pid_) && pop_current(petra_pid_) < pop_max(petra_pid_)) {
		const uint16_t tmpl = civ_default_template(civ_of(petra_pid_), UNIT_TRADER);
		auto bit = building_id_to_idx_.find(market_ids[0]);
		if (tmpl != CIV_UNIT_TEMPLATE_NONE && bit != building_id_to_idx_.end() && buildings_[bit->second].queue.size() < 3)
			petra_train_template(PETRA_QUEUE_TRADE, market_ids[0], tmpl);
	}

	// 2b. tradeManager.js on a sea: with two docks on one sea, merchant ships trade
	// between them. Land routes (step 3) are scored separately, so ships only get
	// their own block: train up to the trader target, and send idle ones out.
	if (has_trade_route(petra_pid_, UNIT_MERCHANT_SHIP)) {
		int ai_ships = 0;
		for (size_t i = 0; i < units_.count; ++i)
			if (units_.alive[i] && units_.team[i] == petra_pid_ && units_.unit_type[i] == UNIT_MERCHANT_SHIP) ai_ships++;
		for (const Building &b : buildings_) // ships already queued count toward the target
			if (b.team == petra_pid_ && b.type == BLD_DOCK)
				for (const ProductionOrder &o : b.queue) if (o.unit_type == UNIT_MERCHANT_SHIP) ai_ships++;
		const Building *dock = nullptr;
		for (const Building &b : buildings_)
			if (b.team == petra_pid_ && can_trade_at(petra_pid_, b, UNIT_MERCHANT_SHIP) && dock_sea(b) && b.queue.size() < 3) { dock = &b; break; }
		const uint16_t stmpl = civ_default_template(civ_of(petra_pid_), UNIT_MERCHANT_SHIP);
		if (dock && stmpl != CIV_UNIT_TEMPLATE_NONE && ai_ships < target_traders && pop_current(petra_pid_) < pop_max(petra_pid_))
			petra_train_template(PETRA_QUEUE_TRADE, dock->id, stmpl);
		for (size_t i = 0; i < units_.count; ++i) {
			if (!units_.alive[i] || units_.team[i] != petra_pid_ || units_.unit_type[i] != UNIT_MERCHANT_SHIP) continue;
			uint32_t uid = units_.id[i];
			if (units_.action_state[i] == ACTION_IDLE || trader_routes_.find(uid) == trader_routes_.end())
				order_trade_auto(&uid, 1);
		}
	}

	// 3. tradeManager.js checkRoutes: the best route starts at one of our
	// markets and ends at ours or an ally's, scored by what we earn per leg:
	// the trader's gain plus the 20% international bonus our market gets on a
	// route with an ally.
	auto building_of = [&](uint32_t id) -> const Building * {
		auto it = building_id_to_idx_.find(id);
		return it == building_id_to_idx_.end() ? nullptr : &buildings_[it->second];
	};
	auto route_gain = [&](const Building &m1, const Building &m2) {
		const TradeGain g = calculate_trade_gain(m1, m2, petra_pid_);
		return g.trader + (m1.team == petra_pid_ ? g.market1 : 0) + (m2.team == petra_pid_ ? g.market2 : 0);
	};
	const Building *best_a = nullptr, *best_b = nullptr;
	int best_gain = 0;
	for (const auto &m1 : buildings_) {
		if (m1.team != petra_pid_ || !can_trade_at(petra_pid_, m1)) continue;
		for (const auto &m2 : buildings_) {
			if (&m2 == &m1 || !can_trade_at(petra_pid_, m2)) continue;
			if (m2.team != petra_pid_ && !is_ally(petra_pid_, m2.team)) continue;
			const int gain = route_gain(m1, m2);
			if (gain > best_gain) { best_gain = gain; best_a = &m1; best_b = &m2; }
		}
	}
	if (!best_a) return;
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != petra_pid_ || units_.unit_type[i] != UNIT_TRADER) continue;
		uint32_t uid = units_.id[i];
		auto rt = trader_routes_.find(uid);
		bool reassign = units_.action_state[i] == ACTION_IDLE || rt == trader_routes_.end();
		if (!reassign) {
			const Building *a = building_of(rt->second.first), *b = building_of(rt->second.second);
			// Move to a clearly better route (an ally's market further away).
			reassign = !a || !b || !can_trade_at(petra_pid_, *a) || !can_trade_at(petra_pid_, *b) ||
			           (float)route_gain(*a, *b) < 0.8f * (float)best_gain;
		}
		if (!reassign) continue;
		const Building *a = best_a, *b = best_b;
		const float px = units_.pos_x[i], pz = units_.pos_z[i];
		if ((b->x - px) * (b->x - px) + (b->z - pz) * (b->z - pz) < (a->x - px) * (a->x - px) + (a->z - pz) * (a->z - pz))
			std::swap(a, b); // start at the nearer end
		order_trade(&uid, 1, a->id, b->id);
	}
}

// accessibility.js for navalManager.js: the land (PASS_INFANTRY) and sea
// (PASS_SHIP) regions of the nav grid, their sizes, and which seas touch which
// lands (regionLinks). Rebuilt on each use, as region ids change with buildings.
void Simulation::petra_naval_topology(PetraNavalTopo &t) const {
	t = PetraNavalTopo();
	if (nav_grid_.cells.blank()) return;
	const int w = nav_grid_.cells.w, h = nav_grid_.cells.h;
	const float cell_m = 2.0f * nav_grid_.map_half / (float)nav_grid_.res * 4.0f; // 1 world unit = 4 m
	t.cell_m2 = cell_m * cell_m;
	t.total = (uint32_t)w * (uint32_t)h;
	std::vector<uint16_t> land((size_t)w * h), sea((size_t)w * h);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			const size_t k = (size_t)y * w + x;
			land[k] = nav_grid_.global_region_at(x, y, PASS_INFANTRY);
			sea[k] = nav_grid_.global_region_at(x, y, PASS_SHIP);
			auto bump = [](std::vector<uint32_t> &v, uint16_t id) {
				if (!id) return;
				if (v.size() <= id) v.resize((size_t)id + 1, 0);
				++v[id];
			};
			bump(t.land_size, land[k]);
			bump(t.sea_size, sea[k]);
		}
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			const uint16_t l = land[(size_t)y * w + x];
			if (!l) continue;
			const int nb[5][2] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}};
			for (const auto &d : nb) {
				const int nx = x + d[0], ny = y + d[1];
				if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
				const uint16_t s = sea[(size_t)ny * w + nx];
				if (s) t.links.push_back(((uint32_t)l << 16) | s);
			}
		}
	std::sort(t.links.begin(), t.links.end());
	t.links.erase(std::unique(t.links.begin(), t.links.end()), t.links.end());
}

// The topology of the nav grid as it is now, rebuilt only when a cell or a region
// id changed: attack plans ask about it on every AI tick. A pure function of the
// nav grid, so the cache needs no saving.
const Simulation::PetraNavalTopo &Simulation::petra_topo() const {
	uint64_t key = 1469598103934665603ull ^ nav_grid_.cells.data.size() ^ (uint64_t)nav_grid_.regions_ready_;
	for (uint8_t c : nav_grid_.cells.data) key = (key ^ c) * 1099511628211ull;
	// Region ids too: a full rebuild (a loaded game) may number them afresh.
	for (const GlobalRegionIds *g : {&nav_grid_.global_region_map_, &nav_grid_.ship_region_map_}) {
		for (uint32_t f : g->first) key = (key ^ f) * 1099511628211ull;
		for (uint16_t id : g->id) key = (key ^ id) * 1099511628211ull;
	}
	if (!petra_topo_cache_valid_ || key != petra_topo_cache_key_) {
		petra_naval_topology(petra_topo_cache_);
		petra_topo_cache_key_ = key;
		petra_topo_cache_valid_ = true;
	}
	return petra_topo_cache_;
}

// headquarters.js getSeaBetweenIndices: a sea both lands touch.
uint16_t Simulation::PetraNavalTopo::sea_between(uint16_t a, uint16_t b) const {
	if (!a || !b || a == b) return 0;
	auto it = std::lower_bound(links.begin(), links.end(), (uint32_t)a << 16);
	for (; it != links.end() && (*it >> 16) == a; ++it)
		if (linked(b, (uint16_t)(*it & 0xFFFF))) return (uint16_t)(*it & 0xFFFF);
	return 0;
}

// startingStrategy.js regionsInit, from the land of our first base: the map is
// naval when a sea covers a fifth of it, or joins our land to another land
// (over 320 m²) that is big itself or across a big sea. navalRegions are those
// seas and any other over 3600 m².
void Simulation::PetraNavalTopo::classify(uint16_t home) {
	naval_map = false;
	naval.assign(sea_size.size(), false);
	const float min_land = 0.1f * (float)total, min_water = 0.2f * (float)total;
	for (size_t s = 1; s < sea_size.size(); ++s) {
		if ((float)sea_size[s] > min_water) { naval_map = true; naval[s] = true; }
		else if (cell_m2 * (float)sea_size[s] > 3600.0f) naval[s] = true;
	}
	if (!home) return;
	for (size_t l = 1; l < land_size.size(); ++l) {
		if (l == home || cell_m2 * (float)land_size[l] <= 320.0f) continue;
		const uint16_t s = sea_between(home, (uint16_t)l);
		if (s && ((float)land_size[l] > min_land || (float)sea_size[s] > min_water)) {
			naval_map = true;
			naval[s] = true;
		}
	}
}

// PETRA.getLandAccess: the land region at (x, z), or of the nearest land cell
// within a few cells when it stands on a blocked one.
uint16_t Simulation::petra_land_at(float x, float z) const {
	auto [cx, cz] = nav_grid_.world_to_cell(x, z);
	for (int r = 0; r <= 4; ++r)
		for (int dz = -r; dz <= r; ++dz)
			for (int dx = -r; dx <= r; ++dx) {
				if (std::abs(dx) != r && std::abs(dz) != r) continue;
				const uint16_t l = nav_grid_.global_region_at(cx + dx, cz + dz, PASS_INFANTRY);
				if (l) return l;
			}
	return 0;
}

// A structure's land: the land just outside its footprint (a dock's shore side).
// RTS0's land regions, unlike 0 A.D.'s accessibility map, are cut by structures,
// so with `topo` it is the biggest land around it, not a pocket between houses.
uint16_t Simulation::petra_building_land(const Building &b, const PetraNavalTopo *topo, uint16_t prefer) const {
	const float r = b.half_size + GRID;
	const float off[8][2] = {{r, 0.0f}, {-r, 0.0f}, {0.0f, r}, {0.0f, -r}, {r, r}, {-r, r}, {r, -r}, {-r, -r}};
	uint16_t best = 0;
	for (const auto &o : off) {
		auto [cx, cz] = nav_grid_.world_to_cell(b.x + o[0], b.z + o[1]);
		const uint16_t l = nav_grid_.global_region_at(cx, cz, PASS_INFANTRY);
		if (!l) continue;
		if (l == prefer) return l;
		if (!topo) return l;
		const uint32_t size = l < topo->land_size.size() ? topo->land_size[l] : 0;
		const uint32_t best_size = best < topo->land_size.size() ? topo->land_size[best] : 0;
		if (!best || size > best_size) best = l;
	}
	return best ? best : petra_land_at(b.x, b.z);
}

// The land of our first base (an anchored base's structure), else of (x, z).
uint16_t Simulation::petra_home_land(float x, float z, const PetraNavalTopo *topo) const {
	if (const PetraBase *pb = get_primary_petra_base()) {
		auto it = building_id_to_idx_.find(pb->anchor_building_id);
		if (it != building_id_to_idx_.end()) return petra_building_land(buildings_[it->second], topo);
		return petra_land_at(pb->x, pb->z);
	}
	return petra_land_at(x, z);
}

// attackPlan.js overseas: the sea an army from (from_x, from_z) crosses to reach
// target_id, or 0 when it walks there (or no sea joins the two lands).
uint16_t Simulation::petra_overseas(const PetraNavalTopo &t, float from_x, float from_z, uint32_t target_id) const {
	auto it = building_id_to_idx_.find(target_id);
	if (it == building_id_to_idx_.end()) return 0;
	const uint16_t from = petra_home_land(from_x, from_z, &t);
	const uint16_t to = petra_building_land(buildings_[it->second], &t, from);
	return from == to ? 0 : t.sea_between(from, to);
}

uint16_t Simulation::petra_plan_overseas(float base_x, float base_z, uint32_t target_id) const {
	// Cheap first: a land touching both our first base and the target means no sea
	// to cross, and needs no map scan (attack plans ask on every AI tick).
	auto it = building_id_to_idx_.find(target_id);
	if (it == building_id_to_idx_.end()) return 0;
	auto lands_around = [&](const Building &b, uint16_t out[8]) {
		const float r = b.half_size + GRID;
		const float off[8][2] = {{r, 0.0f}, {-r, 0.0f}, {0.0f, r}, {0.0f, -r}, {r, r}, {-r, r}, {r, -r}, {-r, -r}};
		for (int k = 0; k < 8; ++k) {
			auto [cx, cz] = nav_grid_.world_to_cell(b.x + off[k][0], b.z + off[k][1]);
			out[k] = nav_grid_.global_region_at(cx, cz, PASS_INFANTRY);
		}
	};
	uint16_t home[8] = {}, there[8] = {};
	lands_around(buildings_[it->second], there);
	const PetraBase *pb = get_primary_petra_base();
	auto ait = pb ? building_id_to_idx_.find(pb->anchor_building_id) : building_id_to_idx_.end();
	if (ait != building_id_to_idx_.end()) lands_around(buildings_[ait->second], home);
	else home[0] = petra_land_at(base_x, base_z);
	for (uint16_t h : home)
		for (uint16_t t : there)
			if (h && h == t) return 0;
	const PetraNavalTopo &topo = petra_topo();
	return petra_overseas(topo, base_x, base_z, target_id);
}

// A unit of ours aboard Petra's transport ship (alive again once it lands).
bool Simulation::petra_aboard(uint32_t uid) const {
	if (petra_transport_.state == PetraTransportPlan::IDLE || petra_transport_.state == PetraTransportPlan::FAILED) return false;
	const auto *p = get_transport_passengers(petra_transport_.ship_id);
	return p && std::find(p->begin(), p->end(), uid) != p->end();
}

// navalManager.js transportShips: own ships that carry units (warships, which
// are all of them but the fire ship; not fishing boats nor merchantmen).
bool Simulation::petra_is_transport_ship(size_t i) const {
	return units_.unit_type[i] == UNIT_TRIREME && ship_garrison_capacity(i) >= 2;
}

// The sea a ship is on (or beside, moored at a dock on a shore cell).
uint16_t Simulation::petra_ship_sea(size_t i) const {
	auto [cx, cz] = nav_grid_.world_to_cell(units_.pos_x[i], units_.pos_z[i]);
	for (int r = 0; r <= 3; ++r)
		for (int dz = -r; dz <= r; ++dz)
			for (int dx = -r; dx <= r; ++dx) {
				if (std::abs(dx) != r && std::abs(dz) != r) continue;
				const uint16_t s = nav_grid_.global_region_at(cx + dx, cz + dz, PASS_SHIP);
				if (s) return s;
			}
	return 0;
}

int Simulation::petra_transport_ships_on(uint16_t sea) const {
	int n = 0;
	for (size_t i = 0; i < units_.count; ++i)
		if (units_.alive[i] && units_.team[i] == petra_pid_ && petra_is_transport_ship(i) && petra_ship_sea(i) == sea) ++n;
	return n;
}

// navalManager.js getBestShip: for "transport" the most GarrisonHolder room,
// +50 when it carries siege engines (no RTS0 ship has BuildingAI arrows, as in
// 0 A.D. now); for "fishing" the fishing boat. Ties go to the later template.
uint16_t Simulation::petra_best_ship(bool fishing) const {
	const uint8_t pid = petra_pid_;
	uint16_t best_tmpl = CIV_UNIT_TEMPLATE_NONE;
	int best = 0;
	for (uint16_t tmpl = 0; tmpl < CIV_UNIT_TEMPLATE_COUNT; ++tmpl) {
		const CivUnitTemplate &t = civ_unit_template(tmpl);
		if (t.civ != players_[pid].civ || t.phase > players_[pid].phase || !unit_is_ship(t.unit_type)) continue;
		if (!can_train_at(pid, t.unit_type, BLD_DOCK, tmpl)) continue;
		if (fishing) {
			if (t.unit_type == UNIT_FISHING_BOAT) best_tmpl = tmpl;
			continue;
		}
		if (t.unit_type != UNIT_TRIREME && t.unit_type != UNIT_MERCHANT_SHIP) continue;
		int capacity = ship_template_capacity(t.unit_type, tmpl);
		if (capacity < 2) continue;
		if (t.unit_type == UNIT_TRIREME && capacity != 10) capacity += 50; // List Siege (not the scout ship)
		if (best > capacity) continue;
		best = capacity;
		best_tmpl = tmpl;
	}
	return best_tmpl;
}

// navalManager.js getFishSea "opensea" and canFishSafely: fish with open water
// of its sea 120 m around is safe; any other only without enemy territory at
// 120 m and 60 m in eight directions.
bool Simulation::petra_can_fish_safely(float x, float z, uint16_t sea) const {
	static const float around[8][2] = {{-0.7f, 0.7f}, {0.0f, 1.0f}, {0.7f, 0.7f}, {1.0f, 0.0f},
	                                   {0.7f, -0.7f}, {0.0f, -1.0f}, {-0.7f, -0.7f}, {-1.0f, 0.0f}};
	constexpr float R = 120.0f / 4.0f;
	const float edge = map_half_;
	bool open = true;
	for (const auto &a : around) {
		bool ok = true;
		for (int t = 0; t < 4; ++t) {
			const float r = R * (float)(4 - t) / 4.0f;
			const float px = x + a[0] * r, pz = z + a[1] * r;
			if (std::fabs(px) >= edge || std::fabs(pz) >= edge) continue;
			auto [cx, cz] = nav_grid_.world_to_cell(px, pz);
			if (nav_grid_.global_region_at(cx, cz, PASS_INFANTRY) != 0) { ok = false; break; } // land
			const uint16_t s = nav_grid_.global_region_at(cx, cz, PASS_SHIP);
			if (s == sea) break;
			if (s != 0) { ok = false; break; }
		}
		if (!ok) { open = false; break; }
	}
	if (open) return true;
	for (const auto &a : around)
		for (int t = 0; t < 2; ++t) {
			const float r = R * (float)(2 - t) / 2.0f;
			const float px = x + a[0] * r, pz = z + a[1] * r;
			if (std::fabs(px) >= edge || std::fabs(pz) >= edge) continue;
			const int owner = territory_owner_at(px, pz);
			if (owner >= 0 && owner != GAIA_PLAYER && petra_foe(owner)) return false;
		}
	return true;
}

// navalManager.js update (checkLevels, maintainFleet, buildNavalStructures,
// moveApart) with worker.js fishing. Petra builds a dock per sea its bases touch
// once its population passes popForDock, trains the transport ships its attacks
// across water need and one fishing boat per sea with fish, and fights no naval
// war of its own (defenseManager.js leaves ships alone).
void Simulation::petra_manage_naval(float dt) {
	petra_naval_timer_ += dt;
	if (petra_naval_timer_ < 2.0f) return;
	petra_naval_timer_ = 0.0f;
	if (nav_grid_.cells.blank()) return;

	bool any_water = false;
	for (int y = 0; y < nav_grid_.cells.h && !any_water; y += 2)
		for (int x = 0; x < nav_grid_.cells.w && !any_water; x += 2)
			any_water = nav_grid_.cells.at(x, y) == NAV_WATER;
	if (!any_water) return;

	PetraNavalTopo topo = petra_topo(); // classify() below marks this AI's naval seas
	float home_x = 0.0f, home_z = 0.0f;
	if (const PetraBase *pb = get_primary_petra_base()) { home_x = pb->x; home_z = pb->z; }
	const uint16_t home = petra_home_land(home_x, home_z, &topo);
	topo.classify(home);
	auto naval = [&](uint16_t s) { return s < topo.naval.size() && topo.naval[s]; };
	const size_t nseas = topo.sea_size.size();

	// startingStrategy.js: a starting land under 25000 m² docks at 16 population
	// and wants two fishing boats per sea (config.js popForDock 25, targetNumFishers 1).
	const bool small_land = home && home < topo.land_size.size() && topo.cell_m2 * (float)topo.land_size[home] < 25000.0f;
	const int pop_for_dock = small_land ? 16 : 25;
	const int target_fishers = small_land ? 2 : 1;

	// Our docks by sea; ships by sea.
	std::vector<uint32_t> sea_dock(nseas, 0); // a finished dock on each sea
	bool dock_foundation = false;
	for (const Building &b : buildings_) {
		if (b.team != petra_pid_ || b.type != BLD_DOCK || b.wrecked || b.hp <= 0.0f) continue;
		if (b.build_progress < 1.0f) { dock_foundation = true; continue; }
		const uint16_t s = dock_sea(b);
		if (s && s < nseas && !sea_dock[s]) sea_dock[s] = b.id;
	}
	std::vector<int> transports(nseas, 0), fishers(nseas, 0);
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != petra_pid_ || !unit_is_ship(units_.unit_type[i])) continue;
		const uint16_t s = petra_ship_sea(i);
		if (!s || s >= nseas) continue;
		if (units_.unit_type[i] == UNIT_FISHING_BOAT) ++fishers[s];
		else if (petra_is_transport_ship(i)) ++transports[s];
	}

	// 1. buildNavalStructures: a dock on the first sea a base's land touches
	// without one of ours, once the population passes popForDock.
	if (topo.naval_map && !dock_foundation && pop_current(petra_pid_) > pop_for_dock) {
		const BuildingTypeInfo &info = building_type_info(BLD_DOCK);
		bool started = false;
		for (const PetraBase &base : petra_bases_) {
			if (started) break;
			auto ait = building_id_to_idx_.find(base.anchor_building_id);
			if (ait == building_id_to_idx_.end() || buildings_[ait->second].build_progress < 1.0f) continue;
			const uint16_t land = petra_building_land(buildings_[ait->second], &topo);
			auto lo = std::lower_bound(topo.links.begin(), topo.links.end(), (uint32_t)land << 16);
			for (; lo != topo.links.end() && (*lo >> 16) == land && !started; ++lo) {
				const uint16_t sea = (uint16_t)(*lo & 0xFFFF);
				if (!naval(sea)) continue;
				bool connected = false; // getUnconnectedSeas: a dock of ours on this land and sea
				for (const Building &b : buildings_)
					if (b.team == petra_pid_ && b.type == BLD_DOCK && !b.wrecked && b.hp > 0.0f &&
					    petra_building_land(b, &topo, land) == land && dock_sea(b) == sea) { connected = true; break; }
				if (connected) continue;
				started = true; // the first unconnected sea of the first base without a dock there
				// queueplanBuilding.js findDockPosition: the nearest shore spot on that sea.
				const int sx = (int)std::floor(base.x / GRID), sz = (int)std::floor(base.z / GRID);
				bool placed = false;
				for (int r = 2; r <= 30 && !placed; ++r)
					for (int dx = -r; dx <= r && !placed; ++dx)
						for (int dz = -r; dz <= r && !placed; ++dz) {
							if (std::abs(dx) != r && std::abs(dz) != r) continue;
							const int cx = sx + dx, cz = sz + dz;
							if (!petra_can_place(cx, cz, info.footprint, BLD_DOCK)) continue;
							Building probe;
							probe.half_size = info.footprint * GRID * 0.5f;
							probe.x = (cx + info.footprint * 0.5f) * GRID;
							probe.z = (cz + info.footprint * 0.5f) * GRID;
							if (dock_sea(probe) != sea || petra_building_land(probe, &topo, land) != land) continue;
							placed = true;
							if (!petra_try_pay(PETRA_QUEUE_NAVAL, (float)info.food_cost, (float)info.wood_cost,
							                   (float)info.gold_cost, (float)info.stone_cost)) break;
							const uint32_t bid = spawn_building(cx, cz, BLD_DOCK, petra_pid_, 0.0f);
							auto idle = get_idle_workers(petra_pid_);
							if (bid && !idle.empty()) order_build_or_repair(idle.data(), std::min((size_t)3, idle.size()), bid);
						}
			}
		}
	}

	// 2. checkLevels / maintainFleet: transport ships for attacks across water
	// (attackPlan.js setMinimalTransportShips: neededShips 1 for rushes and raids,
	// 3 for attacks, 5 for huge attacks, times popScaling), then fishing boats
	// for each sea holding over 1000 food of fish. One ship in training at a time.
	std::vector<int> want_transport(nseas, 0), want_fish(nseas, 0);
	auto need_ships = [&](uint8_t kind) {
		const int n = (kind == PETRA_ATTACK_HUGE) ? 5 : (kind == PETRA_ATTACK_ATTACK ? 3 : 1);
		return std::max(1, (int)std::ceil(petra_config_.pop_scaling * (float)n));
	};
	auto plan_wants = [&](const AttackPlan &plan, uint8_t kind) {
		if (plan.state != AttackPlan::MUSTER && plan.state != AttackPlan::WALKING) return;
		const uint16_t s = petra_overseas(topo, home_x, home_z, plan.target_building_id);
		if (s && s < nseas) want_transport[s] = std::max(want_transport[s], need_ships(kind));
	};
	plan_wants(petra_attack_plan_, petra_attack_mgr_.kind);
	plan_wants(petra_raid_plan_, PETRA_ATTACK_RAID);
	std::vector<int> fish_food(nseas, 0);
	for (size_t r = 0; r < resources_.count; ++r) {
		if (!resources_.alive[r] || resources_.amount[r] <= 0 || resources_.subtype[r] != RES_SUB_FISH) continue;
		auto [fx, fz] = nav_grid_.world_to_cell(resources_.x[r], resources_.z[r]);
		const uint16_t s = nav_grid_.global_region_at(fx, fz, PASS_SHIP);
		if (s && s < nseas) fish_food[s] += resources_.amount[r];
	}
	for (size_t s = 1; s < nseas; ++s)
		if (naval((uint16_t)s) && fish_food[s] > 1000) want_fish[s] = target_fishers;

	bool training = false; // queues.ships.hasQueuedUnits()
	for (const Building &b : buildings_)
		if (b.team == petra_pid_ && b.type == BLD_DOCK)
			for (const ProductionOrder &o : b.queue)
				if (o.unit_type == UNIT_TRIREME || o.unit_type == UNIT_FISHING_BOAT) training = true;
	for (size_t s = 1; s < nseas && !training; ++s) {
		if (!sea_dock[s]) continue;
		uint16_t tmpl = CIV_UNIT_TEMPLATE_NONE;
		if (transports[s] < want_transport[s]) tmpl = petra_best_ship(false);
		if (tmpl == CIV_UNIT_TEMPLATE_NONE && fishers[s] < want_fish[s]) tmpl = petra_best_ship(true);
		if (tmpl != CIV_UNIT_TEMPLATE_NONE && petra_train_template(PETRA_QUEUE_NAVAL, sea_dock[s], tmpl)) training = true;
	}

	// 3. worker.js fishing: an idle boat takes the nearest fish of its sea that
	// it can fish safely.
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != petra_pid_ || units_.unit_type[i] != UNIT_FISHING_BOAT) continue;
		if (units_.action_state[i] != ACTION_IDLE) continue;
		const uint16_t sea = petra_ship_sea(i);
		uint32_t best_rid = 0;
		float best_d2 = 1e18f;
		for (size_t r = 0; r < resources_.count; ++r) {
			if (!resources_.alive[r] || resources_.amount[r] <= 0 || resources_.subtype[r] != RES_SUB_FISH) continue;
			const float dx = resources_.x[r] - units_.pos_x[i], dz = resources_.z[r] - units_.pos_z[i];
			const float d2 = dx * dx + dz * dz;
			if (d2 >= best_d2) continue;
			auto [fx, fz] = nav_grid_.world_to_cell(resources_.x[r], resources_.z[r]);
			if (nav_grid_.global_region_at(fx, fz, PASS_SHIP) != sea) continue;
			if (!petra_can_fish_safely(resources_.x[r], resources_.z[r], sea)) continue;
			best_d2 = d2;
			best_rid = resources_.id[r];
		}
		if (best_rid) {
			const uint32_t uid = units_.id[i];
			order_gather(&uid, 1, best_rid);
		}
	}

	// navalManager.js:58 seaWarShips: our warships by sea, for the attack targets.
	petra_fleets_.clear_sea_fleets();
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != petra_pid_) continue;
		const uint8_t ut = units_.unit_type[i];
		if (!unit_is_ship(ut) || petra_is_support_unit(ut)) continue;
		const uint16_t reg = petra_ship_sea(i);
		const int sea_id = (reg > 0 && (int)(reg - 1) < Petra::kMaxSeas) ? (int)(reg - 1) : 0;
		if (petra_fleets_.sea_warship_count[sea_id] < 16)
			petra_fleets_.sea_warships[sea_id][petra_fleets_.sea_warship_count[sea_id]++] = units_.id[i];
	}
	int active_seas = 0;
	for (int s = 0; s < Petra::kMaxSeas; ++s)
		if (petra_fleets_.sea_warship_count[s] > 0) active_seas = s + 1;
	petra_fleets_.sea_count_ = active_seas;

	// 4. moveApart: an idle ship within 70 m of an allied dock on its sea moves
	// out to 70-75 m so it leaves the dock free.
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != petra_pid_ || !unit_is_ship(units_.unit_type[i])) continue;
		if (units_.action_state[i] != ACTION_IDLE || units_.has_order[i]) continue;
		if (units_.id[i] == petra_transport_.ship_id && petra_transport_.state != PetraTransportPlan::IDLE &&
		    petra_transport_.state != PetraTransportPlan::FAILED) continue;
		const uint16_t sea = petra_ship_sea(i);
		for (const Building &b : buildings_) {
			if (b.type != BLD_DOCK || b.wrecked || b.hp <= 0.0f || !is_ally(petra_pid_, b.team)) continue;
			float dx = units_.pos_x[i] - b.x, dz = units_.pos_z[i] - b.z;
			const float d2 = dx * dx + dz * dz;
			if (d2 > (70.0f / 4.0f) * (70.0f / 4.0f) || dock_sea(b) != sea) continue;
			const float d = std::sqrt(d2);
			if (d > 0.01f) { dx /= d; dz /= d; }
			else { const float ang = (float)(units_.id[i] % 8) * 0.78539816f; dx = std::cos(ang); dz = std::sin(ang); }
			const float to = 72.5f / 4.0f;
			float wx = b.x + dx * to, wz = b.z + dz * to;
			const size_t ship = i;
			if (!nearest_water_for_ship(ship, wx, wz, wx, wz)) break;
			const uint32_t uid = units_.id[i];
			order_walk(&uid, 1, wx, wz, false);
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
					if (petra_can_place(cx, cz, 1, BLD_TOWER) &&
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

		if (!tc_in_progress && petra_limit_allows_building(BLD_TOWN_CENTER)) { // EntityLimits CivilCentre
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
				if (petra_can_place(cx, cz, 2, BLD_TOWN_CENTER)) {
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
				if (resources_.subtype[r] == RES_SUB_FISH) continue; // the dock serves fish
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
						if (resources_.subtype[j] == RES_SUB_FISH) continue; // a land dropsite cannot take fish
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
							if (petra_can_place(cx, cz, 1, dropsite_type) && is_position_safe(cx * GRID, cz * GRID, 5.0f) &&
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
					if (petra_can_place(cx, cz, 2, BLD_BARRACKS) &&
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
						if (petra_can_place(cx, cz, 1, BLD_TOWER) &&
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
						if (petra_can_place(cx, cz, 2, BLD_FORTRESS)) {
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

	// 7. headquarters.js constructTrainingBuildings, city phase: an elephant
	// stable and an arsenal, then (from 80 pop) the civ's own advanced
	// structures from config.js buildings, the next one only past 110 pop.
	if (petra_ps().phase >= 3) petra_build_advanced();
}

// config.js `buildings`: each civ's advanced structures, in Petra's order
// (Carthage's three embassies are RTS0's one). Petra never builds Rome's
// Temple of Mars: it is in no list.
static std::vector<uint8_t> petra_advanced_buildings(uint8_t civ) {
	switch (civ) {
		case CIV_ATHENIANS: return {BLD_GYMNASIUM, BLD_PRYTANEION, BLD_THEATER};
		case CIV_CARTHAGINIANS: return {BLD_EMBASSY};
		case CIV_GAULS: return {BLD_ASSEMBLY};
		case CIV_HAN: return {BLD_ACADEMY};
		case CIV_IBERIANS: return {BLD_MONUMENT};
		case CIV_KUSHITES: return {BLD_CAMP_BLEMMYE, BLD_CAMP_NOBA, BLD_PYRAMID_LARGE, BLD_PYRAMID_SMALL, BLD_TEMPLE_AMUN};
		case CIV_MACEDONIANS: case CIV_SELEUCIDS: return {BLD_THEATER};
		case CIV_MAURYANS: return {BLD_PALACE, BLD_PILLAR};
		case CIV_PERSIANS: return {BLD_TACHARA};
		case CIV_PTOLEMIES: return {BLD_LIBRARY, BLD_THEATER};
		case CIV_ROMANS: return {BLD_ARMY_CAMP, BLD_TEMPLE_VESTA};
		case CIV_SPARTANS: return {BLD_SYSSITON, BLD_THEATER};
		default: return {};
	}
}

void Simulation::petra_build_advanced() {
	const PetraBase *home = get_primary_petra_base();
	if (!home) return;
	const uint8_t civ = petra_ps().civ;
	auto count = [&](uint8_t t) { // countEntitiesAndQueuedByType: foundations too
		int n = 0;
		for (const auto &b : buildings_)
			if (b.team == petra_pid_ && b.type == t && b.hp > 0.0f && !b.wrecked) ++n;
		return n;
	};
	auto build = [&](uint8_t t) {
		const uint32_t bid = petra_place_building(t, home->x, home->z, 4, 16, PETRA_QUEUE_MILITARY);
		if (!bid) return;
		auto idle = get_idle_workers(petra_pid_);
		if (!idle.empty()) order_build_or_repair(idle.data(), std::min((size_t)3, idle.size()), bid);
	};
	// canBuild: RTS0 lets every civ lay any generic structure, so the stable and
	// arsenal count only when the civ's roster trains something there.
	auto trains_at = [civ](uint8_t t) {
		for (uint16_t k = 0; k < CIV_UNIT_TEMPLATE_COUNT; ++k)
			if (civ_unit_template(k).civ == civ && (civ_unit_template(k).trainers & bld_bit(t))) return true;
		return false;
	};
	for (uint8_t t : {BLD_ELEPHANT_STABLE, BLD_ARSENAL}) {
		if (civ_can_build(civ, t) && trains_at(t) && count(t) == 0) {
			build(t);
			return;
		}
	}
	std::vector<uint8_t> advanced;
	for (uint8_t t : petra_advanced_buildings(civ))
		if (civ_can_build(civ, t)) advanced.push_back(t);
	// canBuild: one its EntityLimits refuse is skipped (the Mauryan Pillar only
	// stands while Ashoka lives), so Petra goes on to the next.
	const int pop = pop_current(petra_pid_);
	if (pop < 80 || advanced.empty()) return;
	int n = 0;
	for (uint8_t t : advanced) n += count(t);
	if (n && !(n < (int)advanced.size() && pop > 110)) return;
	for (uint8_t t : advanced) {
		if (count(t) > 0 || !petra_limit_allows_building(t)) continue;
		build(t);
		return;
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
		// researchWantedTechs: any unlock_champion_* first, at raised priority.
		for (uint8_t t : {TECH_UNLOCK_CHAMPION_INFANTRY, TECH_UNLOCK_CHAMPION_CAVALRY, TECH_UNLOCK_CHAMPION_CHARIOTS})
			if (tech_available_to_civ(t, petra_ps().civ) && start_tech(t)) return;
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

// headquarters.js gameStart + attackManager.js setRushes: Petra plans village
// rushes only when its base has wood to spare (one per 3000 above 8000) and
// its personality is aggressive enough; a long ceasefire rules them out.
void Simulation::petra_set_rushes() {
	auto &m = petra_attack_mgr_;
	m.rushes_set = 1;
	m.max_rushes = 0;
	const PetraBase *base = get_primary_petra_base();
	if (!base || petra_config_.difficulty < PETRA_EASY) return;
	float wood = petra_ps().res[RES_WOOD];
	for (size_t r = 0; r < resources_.count; ++r) {
		if (!resources_.alive[r] || resources_.type[r] != RES_WOOD) continue;
		const float dx = resources_.x[r] - base->x, dz = resources_.z[r] - base->z;
		if (dx * dx + dz * dz <= 60.0f * 60.0f) wood += resources_.amount[r];
	}
	int allowed = (int)std::ceil((wood - 8000.0f) / 3000.0f);
	if (ceasefire_remaining() > 900.0f) allowed = 0;
	else if (ceasefire_remaining() > 600.0f && allowed > 1) allowed = 1;
	// config.js personalityCut: weak 0.3, medium 0.5, strong 0.7.
	const float a = petra_config_.aggressive;
	if (a > 0.7f && allowed > 2) {
		m.max_rushes = 3;
		m.rush_size[0] = 16; m.rush_size[1] = 20; m.rush_size[2] = 24;
	} else if (a > 0.5f && allowed > 1) {
		m.max_rushes = 2;
		m.rush_size[0] = 18; m.rush_size[1] = 22;
	} else if (a > 0.3f && allowed > 0) {
		m.max_rushes = 1;
		m.rush_size[0] = 20;
	}
}

// attackPlan.js constructor: the plan's unitStat totals for its type, varied
// by +/-20% and shrunk at the lower difficulties.
void Simulation::petra_plan_sizes(uint8_t kind) {
	auto &m = petra_attack_mgr_;
	int target = 52, minimum = 12;           // Attack: citizen infantry and cavalry, a few champions, a hero
	if (kind == PETRA_ATTACK_RUSH) {
		target = m.rush_size[std::min<int>(m.rush_number, 2)];
		if (target <= 0) target = 20;
		minimum = 10;                        // Infantry only
	} else if (kind == PETRA_ATTACK_HUGE) {
		target = 147; minimum = 28;          // every class, champions included
	} else if (kind == PETRA_ATTACK_RAID) {
		target = 4; minimum = 3;             // Cavalry only
	}
	if (kind != PETRA_ATTACK_RAID) {
		float variation = sim_rng_.range_float(0.8f, 1.2f);
		if (petra_config_.difficulty < PETRA_EASY) variation *= 0.5f;
		else if (petra_config_.difficulty < PETRA_MEDIUM) variation *= 0.8f;
		target = std::max(1, (int)std::round(variation * (float)target));
	}
	m.kind = kind;
	m.target_size = target;
	m.min_size = std::min(minimum, target);
}

// attackPlan.js rushTargetFinder: the nearest enemy structure that does not
// shoot and stands more than 80 m (20 world units of 4 m) from every structure that does.
uint32_t Simulation::find_petra_rush_target(float &out_tx, float &out_tz) const {
	float bx = 36.0f, bz = 36.0f;
	if (const auto *base = get_primary_petra_base()) { bx = base->x; bz = base->z; }
	auto shoots = [](uint8_t t) {
		return t == BLD_TOWN_CENTER || t == BLD_TOWER || t == BLD_SENTRY_TOWER || t == BLD_WALL_TOWER || t == BLD_FORTRESS;
	};
	uint32_t best = 0;
	float best_d = 1e18f;
	for (const auto &b : buildings_) {
		if (!petra_foe(b.team) || b.hp <= 0 || shoots(b.type)) continue;
		if (b.type == BLD_FIELD || b.type == BLD_WALL_SEGMENT || b.type == BLD_PALISADE || b.type == BLD_GATE) continue;
		bool defended = false;
		for (const auto &d : buildings_) {
			if (!petra_foe(d.team) || d.hp <= 0 || !shoots(d.type)) continue;
			const float dx = d.x - b.x, dz = d.z - b.z;
			if (dx * dx + dz * dz < 20.0f * 20.0f) { defended = true; break; } // 80 m: 20 tiles
		}
		if (defended) continue;
		const float dx = b.x - bx, dz = b.z - bz;
		const float dsq = dx * dx + dz * dz;
		if (dsq < best_d) {
			best_d = dsq;
			best = b.id;
			out_tx = b.x;
			out_tz = b.z;
		}
	}
	return best;
}

// defenseManager.js targetList: an enemy structure standing in our territory
// is a raid target until it falls or the territory changes hands.
bool Simulation::petra_raid_target_ok(uint32_t bld_id) const {
	auto it = building_id_to_idx_.find(bld_id);
	if (it == building_id_to_idx_.end()) return false;
	const Building &b = buildings_[it->second];
	return petra_foe(b.team) && b.hp > 0 && territory_owner_at(b.x, b.z) == (int)petra_pid_;
}

// entityExtend.js allowCapture: capture a structure only when the squad's
// capture strength beats its regeneration plus a share of its points (1/50 of
// them for a garrisoned structure that shoots, 1/80 otherwise); a damaged
// structure is easier. (0 A.D.'s always-recapture-from-an-ally case can't arise:
// can_capture_building refuses allied structures.) The strength is the summed
// Capture attack of the squad's units on the target or capturing within 20 m of
// it (HQ.capturableTargets), with no capture bonuses in RTS0.
bool Simulation::petra_allow_capture(const std::vector<uint32_t> &squad, uint32_t uid, const Building &b) const {
	const int32_t idx = lookup_unit(uid);
	if (idx < 0 || !units_.alive[idx] || unit_capture_strength(units_.unit_type[idx]) <= 0.0f) return false;
	if (!can_capture_building(petra_pid_, b)) return false;
	const int ground = territory_owner_at(b.x, b.z);
	const bool decaying = b.territory_decay_rate > 0.0f && b.team < MAX_PLAYERS && ground >= 0 && ground < MAX_PLAYERS &&
			ground != b.team && !is_ally((uint8_t)ground, b.team);

	float anti = b.capture_regen_rate + b.capture_garrison_regen_rate * (float)b.garrisoned_units.size();
	if (decaying) anti -= b.territory_decay_rate;
	float capture = unit_capture_strength(units_.unit_type[idx]);
	for (uint32_t other : squad) {
		const int32_t j = lookup_unit(other);
		if (other == uid || j < 0 || !units_.alive[j]) continue;
		// Squad mates on the structure, or capturing ones closing in on it.
		const float dx = units_.pos_x[j] - b.x, dz = units_.pos_z[j] - b.z;
		const bool near = (units_.order_flags[j] & ORDER_FLAG_ALLOW_CAPTURE) && dx * dx + dz * dz <= 400.0f;
		if (units_.target_building_id[j] == b.id || near) capture += unit_capture_strength(units_.unit_type[j]);
	}
	const float health = b.max_hp > 0.0f ? std::clamp(b.hp / b.max_hp, 0.0f, 1.0f) : 1.0f;
	capture /= 0.1f + 0.9f * health;
	const float points = std::accumulate(b.capture_points_by_team.begin(), b.capture_points_by_team.end(), 0.0f);
	const bool fires = building_type_info(b.type).attack_damage > 0.0f && !b.garrisoned_units.empty();
	return capture > anti + points / (fires ? 50.0f : 80.0f);
}

// attackPlan.js update: a unit capturing a structure it can't take switches to
// damaging it (ent.attack(target, false)), then carries on to the attack target.
void Simulation::petra_check_captures(const std::vector<uint32_t> &squad, float tx, float tz) {
	for (uint32_t uid : squad) {
		const int32_t idx = lookup_unit(uid);
		if (idx < 0 || !units_.alive[idx] || !(units_.order_flags[idx] & ORDER_FLAG_ALLOW_CAPTURE)) continue;
		const uint32_t target = units_.target_building_id[idx];
		if (target == 0 || units_.action_state[idx] == ACTION_BUILD) continue;
		auto it = building_id_to_idx_.find(target);
		if (it == building_id_to_idx_.end() || petra_allow_capture(squad, uid, buildings_[it->second])) continue;
		order_attack_building(&uid, 1, target, false, false);
		order_attack_move(&uid, 1, tx, tz, true, false, true);
	}
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
		return (uidx < 0 || (!units_.alive[uidx] && !petra_aboard(uid)) || units_.team[uidx] != petra_pid_);
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

	// A rush keeps to undefended structures (attackPlan.js rushTargetFinder).
	auto petra_next_target = [&](float &tx, float &tz) {
		return petra_attack_mgr_.kind == PETRA_ATTACK_RUSH ? find_petra_rush_target(tx, tz)
		                                                   : find_petra_attack_target(tx, tz);
	};

	switch (petra_attack_plan_.state) {
		case AttackPlan::UNEXECUTED: {
			// attackManager.js update(): rushes first while any are left and a
			// barracks stands, then from the town phase an "Attack" plan, and
			// from the third on a "HugeAttack".
			auto &mgr = petra_attack_mgr_;
			bool have_barracks = false, have_tc = false;
			for (const auto &b : buildings_) {
				if (b.team != petra_pid_ || b.build_progress < 1.0f) continue;
				if (b.type == BLD_BARRACKS) have_barracks = true;
				if (b.type == BLD_TOWN_CENTER) have_tc = true;
			}
			if (!mgr.rushes_set && have_tc) petra_set_rushes();
			if (petra_config_.difficulty == PETRA_SANDBOX || !have_barracks) return;
			float tx = 0.0f, tz = 0.0f;
			uint32_t target_id = 0;
			uint8_t kind;
			if (mgr.rush_number < mgr.max_rushes && petra_ps().phase < 2) {
				kind = PETRA_ATTACK_RUSH;
				target_id = find_petra_rush_target(tx, tz);
				// A rush that finds no undefended target fails, and counts as planned.
				if (target_id == 0) { ++mgr.rush_number; return; }
				petra_attack_plan_.type = AttackPlan::PLAN_ASSAULT;
			} else {
				if (petra_ps().phase < 2) return;
				kind = mgr.attack_number < 2 ? PETRA_ATTACK_ATTACK : PETRA_ATTACK_HUGE;
				target_id = find_petra_attack_target(tx, tz);
				if (target_id == 0) return;
			}
			petra_plan_sizes(kind);
			if (kind == PETRA_ATTACK_RUSH) ++mgr.rush_number;
			else ++mgr.attack_number;

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
			const auto &mgr = petra_attack_mgr_;
			// Rushes march with infantry only (attackPlan.js Rush unitStat "Infantry").
			auto mounted = [](uint8_t t) {
				return t == UNIT_CAVALRY || t == UNIT_CAMEL || t == UNIT_CHARIOT || t == UNIT_SCYTHED_CHARIOT || t == UNIT_ELEPHANT;
			};
			for (size_t i = 0; i < units_.count; ++i) {
				if (squad.size() >= (size_t)std::max(1, mgr.target_size)) break;
				if (!units_.alive[i] || units_.team[i] != petra_pid_) continue;
				if (units_.id[i] == petra_scout_unit_id_) continue; // Scout remains on exploration
				if (petra_is_support_unit(units_.unit_type[i]) || units_.unit_type[i] == UNIT_TRIREME) continue;
				if (mgr.kind == PETRA_ATTACK_RUSH && (mounted(units_.unit_type[i]) || units_.unit_type[i] == UNIT_CATAPULT ||
				    units_.unit_type[i] == UNIT_SIEGE_RAM || units_.unit_type[i] == UNIT_BOLT_SHOOTER)) continue;
				// attackPlan.js isAvailableUnit: not garrisoning, not guarding a hero,
				// and a regicide hero only while above 80% health.
				if (units_.order_kind[i] == ORDER_GARRISON || petra_guards_critical_ent(i)) continue;
				if (petra_is_critical_ent(i) && units_.hp[i] < 0.8f * units_.max_hp[i]) continue;
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

			// attackPlan.js updatePreparation: march once the army reaches its
			// target size, or its minimum when the population is capped. RTS0
			// also marches at the minimum after three minutes, since a plan
			// here cannot reinforce on the way.
			const size_t target = (size_t)std::max(1, mgr.target_size);
			const size_t minimum = (size_t)std::max(1, std::min(mgr.min_size, mgr.target_size));
			const bool capped = pop_max(petra_pid_) - pop_current(petra_pid_) < 5;
			bool ready = squad.size() >= target ||
			             (squad.size() >= minimum && (capped || petra_attack_plan_.state_timer >= 180.0f));
			// A siege plan waits for its siege engines in the town phase on.
			if (ready && petra_attack_plan_.type == AttackPlan::PLAN_SIEGE && petra_ps().phase >= 2 &&
			    siege_units == 0 && petra_attack_plan_.state_timer < 240.0f)
				ready = false;
			// A rush is a village-phase plan: one not ready by the town phase is dropped.
			if (mgr.kind == PETRA_ATTACK_RUSH && !ready && petra_ps().phase >= 2) {
				squad.clear();
				petra_attack_plan_.state = AttackPlan::UNEXECUTED;
				petra_attack_plan_.state_timer = 0.0f;
				return;
			}

			if (ready && !squad.empty()) {
				float tx = 0.0f, tz = 0.0f;
				uint32_t target_id = petra_next_target(tx, tz);
				if (target_id == 0) {
					petra_attack_plan_.state = AttackPlan::UNEXECUTED;
					petra_attack_plan_.state_timer = 0.0f;
					return;
				}

				petra_attack_plan_.target_building_id = target_id;
				petra_attack_plan_.target_x = tx;
				petra_attack_plan_.target_z = tz;
				// attackPlan.js updatePreparation: an army bound overseas waits for a
				// transport ship on that sea (navalManager trains it meanwhile).
				const uint16_t overseas = petra_plan_overseas(base_x, base_z, target_id);
				if (overseas && petra_transport_ships_on(overseas) == 0) return;

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

				if (overseas) {
					// attackPlan.js "transporting": the army sails, and gathers where it lands.
					if (!petra_start_transport(squad, base_x, base_z, tx, tz, overseas)) return; // the ship is busy
					petra_attack_plan_.staging_x = petra_strike_force_.staging_x = petra_transport_.drop_x;
					petra_attack_plan_.staging_z = petra_strike_force_.staging_z = petra_transport_.drop_z;
				} else {
					order_walk(squad.data(), squad.size(), sx, sz, false);
				}
				petra_say(PETRA_CHAT_ATTACK, tx, tz);
				++petra_attack_mgr_.launched[petra_attack_mgr_.kind];
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
			// navalManager.js requireTransport: the ship comes back for those still on
			// our land (a failed transport leaves them, as a plan without ships does).
			if (petra_transport_.state == PetraTransportPlan::IDLE &&
			    petra_plan_overseas(base_x, base_z, petra_attack_plan_.target_building_id)) {
				const PetraNavalTopo &topo = petra_topo();
				const uint32_t target_id = petra_attack_plan_.target_building_id;
				const uint16_t overseas = petra_overseas(topo, base_x, base_z, target_id);
				auto tit = building_id_to_idx_.find(target_id);
				if (overseas && tit != building_id_to_idx_.end()) {
					const uint16_t there = petra_building_land(buildings_[tit->second], &topo);
					std::vector<uint32_t> left;
					for (uint32_t uid : squad) {
						const int32_t idx = lookup_unit(uid);
						if (idx >= 0 && units_.alive[idx] && petra_land_at(units_.pos_x[idx], units_.pos_z[idx]) != there)
							left.push_back(uid);
					}
					if (!left.empty() && petra_start_transport(left, base_x, base_z, petra_attack_plan_.target_x,
					                                           petra_attack_plan_.target_z, overseas)) {
						petra_attack_plan_.state_timer = 0.0f;
						return;
					}
				}
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
			int alive_count = 0, attacked = 0;
			for (uint32_t uid : squad) {
				int32_t idx = lookup_unit(uid);
				if (idx >= 0 && units_.alive[idx]) {
					cx += units_.pos_x[idx];
					cz += units_.pos_z[idx];
					alive_count++;
					const int32_t a = units_.attacked_by[idx] ? lookup_unit(units_.attacked_by[idx]) : -1;
					if (a >= 0 && units_.alive[a] && is_hostile(petra_pid_, units_.team[a])) ++attacked;
				}
			}
			if (alive_count > 0) {
				cx /= (float)alive_count;
				cz /= (float)alive_count;
			}

			float dsq_to_staging = (cx - petra_attack_plan_.staging_x) * (cx - petra_attack_plan_.staging_x) +
			                       (cz - petra_attack_plan_.staging_z) * (cz - petra_attack_plan_.staging_z);

			// The march is a forced walk that answers no one on its own; attackPlan.js
			// UpdateWalking stops it to fight once more than one of the army is hit.
			if (dsq_to_staging <= 144.0f || petra_attack_plan_.state_timer >= 18.0f || attacked > 1) {
				float tx = 0.0f, tz = 0.0f;
				uint32_t target_id = petra_next_target(tx, tz);
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

				// Siege engines and their escort (0 A.D. Petra uses no formations).
				std::vector<uint32_t> siege_uids;
				std::vector<uint32_t> escort_uids;

				for (uint32_t uid : squad) {
					int32_t idx = lookup_unit(uid);
					if (idx < 0) continue;
					if (units_.unit_type[idx] == UNIT_CATAPULT || units_.unit_type[idx] == UNIT_SIEGE_RAM || units_.unit_type[idx] == UNIT_BOLT_SHOOTER) {
						// attackPlan.js: packable siege attacks on stand ground so it doesn't pack up to chase.
						if (units_.pack_state[idx] != PACK_NONE) units_.stance[idx] = STANCE_STAND_GROUND;
						siege_uids.push_back(uid);
					} else {
						escort_uids.push_back(uid);
					}
				}

				if (petra_attack_plan_.type == AttackPlan::PLAN_SIEGE && !siege_uids.empty()) {
					// Specialized siege assault: siege weapons focus on fortifications, escort defends siege weapons
					order_attack_building(siege_uids.data(), siege_uids.size(), target_id);
					if (!escort_uids.empty()) {
						order_attack_move(escort_uids.data(), escort_uids.size(), tx, tz, false, false, true);
					}
				} else {
					order_attack_move(squad.data(), squad.size(), tx, tz, false, false, true);
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
					order_walk(squad.data(), squad.size(), ret_x, ret_z, false);
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

			petra_check_captures(squad, petra_attack_plan_.target_x, petra_attack_plan_.target_z);
			if (!target_alive || petra_attack_plan_.state_timer >= 25.0f) {
				float tx = 0.0f, tz = 0.0f;
				uint32_t next_id = petra_next_target(tx, tz);
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
						if (!escort_uids.empty()) order_attack_move(escort_uids.data(), escort_uids.size(), tx, tz, false, false, true);
					} else {
						order_attack_move(squad.data(), squad.size(), tx, tz, false, false, true);
					}
					petra_attack_plan_.state_timer = 0.0f;
				} else {
					petra_attack_plan_.state = AttackPlan::REGROUP;
					petra_attack_plan_.retreat_x = base_x;
					petra_attack_plan_.retreat_z = base_z;
					petra_attack_plan_.state_timer = 0.0f;
					if (!squad.empty()) {
						order_walk(squad.data(), squad.size(), base_x, base_z, false);
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
				order_walk(squad.data(), squad.size(), ret_x, ret_z, false);
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
				order_walk(squad.data(), squad.size(), base_x, base_z, false);
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
	float bx = 36.0f, bz = 36.0f;
	if (const auto *base = get_primary_petra_base()) { bx = base->x; bz = base->z; }
	// defenseManager.js targetList: the enemy structures in our territory,
	// nearest to the base first (attackManager.js raids them with cavalry).
	uint32_t best = 0;
	float best_d = 1e18f;
	for (const auto &b : buildings_) {
		if (!petra_foe(b.team) || b.hp <= 0 || territory_owner_at(b.x, b.z) != (int)petra_pid_) continue;
		const float dsq = (b.x - bx) * (b.x - bx) + (b.z - bz) * (b.z - bz);
		if (dsq < best_d) {
			best_d = dsq;
			best = b.id;
			out_tx = b.x;
			out_tz = b.z;
		}
	}
	if (best) return best;

	// With warships at sea, a dock (or a coastal barracks) across the water is
	// a raid target too: attackPlan.js lands at a dock as a bridgehead.
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
	if (own_warships == 0) return 0;
	int best_pri = -1;
	best_d = 1e9f;
	auto near_fleet = [&](const Building &b) {
		auto [bcx, bcz] = nav_grid_.world_to_cell(b.x, b.z);
		for (int dz = -2; dz <= 2; ++dz) {
			for (int dx = -2; dx <= 2; ++dx) {
				uint16_t sreg = nav_grid_.global_region_at(bcx + dx, bcz + dz, PASS_SHIP);
				if (sreg > 0 && static_cast<int>(sreg - 1) < Petra::kMaxSeas &&
				    petra_fleets_.sea_warship_count[sreg - 1] > 0)
					return true;
			}
		}
		return false;
	};
	for (const auto &b : buildings_) {
		if (!petra_foe(b.team) || b.hp <= 0) continue;
		int pri = -1;
		// 0 A.D. navalManager.js:58 seaWarShips — prioritize a dock in the same sea region as our warships
		if (b.type == BLD_DOCK) {
			pri = near_fleet(b) ? 8 : (petra_path_crosses_water(bx, bz, b.x, b.z) ? 6 : -1);
		} else if (b.type == BLD_BARRACKS && near_fleet(b)) {
			pri = 5;
		}
		if (pri < 0) continue;
		float dsq = b.x * b.x + b.z * b.z;
		if (pri > best_pri || (pri == best_pri && dsq < best_d)) {
			best_pri = pri;
			best_d = dsq;
			best = b.id;
			out_tx = b.x;
			out_tz = b.z;
		}
	}
	return best;
}

// attackManager.js raidTargetEntity / attackPlan.js "Raid": a few cavalry
// citizen soldiers (minSize 3, targetSize 4) sent against one enemy
// structure in our territory; military production trains the cavalry.
void Simulation::petra_manage_raid(float dt) {
	if (petra_config_.difficulty == PETRA_SANDBOX) return;

	auto &plan = petra_raid_plan_;
	plan.type = AttackPlan::PLAN_RAID;
	plan.state_timer += dt;
	plan.squad.erase(std::remove_if(plan.squad.begin(), plan.squad.end(), [&](uint32_t uid) {
		int32_t idx = lookup_unit(uid);
		return idx < 0 || (!units_.alive[idx] && !petra_aboard(uid)) || units_.team[idx] != petra_pid_;
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
		plan.target_building_id = tid;
		plan.target_x = tx;
		plan.target_z = tz;
		plan.state = AttackPlan::MUSTER;
		plan.state_timer = 0.0f;
		return;
	}

	if (plan.state == AttackPlan::MUSTER) {
		// The target fell or the land changed hands: drop the plan.
		const bool naval = !petra_raid_target_ok(plan.target_building_id);
		bool target_alive = false;
		for (const auto &b : buildings_)
			if (b.id == plan.target_building_id && petra_foe(b.team) && b.hp > 0) { target_alive = true; break; }
		if (!target_alive || plan.state_timer > 150.0f) {
			plan.squad.clear();
			plan.target_building_id = 0;
			plan.state = AttackPlan::UNEXECUTED;
			plan.state_timer = 0.0f;
			return;
		}
		// Cavalry citizen soldiers; a raid over water takes any land soldier.
		for (size_t i = 0; i < units_.count && plan.squad.size() < 4; ++i) {
			if (!units_.alive[i] || units_.team[i] != petra_pid_) continue;
			uint8_t ut = units_.unit_type[i];
			if (!petra_is_combat_unit(ut) || unit_is_ship(ut) || ut == UNIT_TRIREME) continue;
			if (ut == UNIT_CATAPULT || ut == UNIT_SIEGE_RAM || ut == UNIT_BOLT_SHOOTER || ut == UNIT_HERO) continue;
			const bool cavalry = ut == UNIT_CAVALRY || ut == UNIT_CAMEL || ut == UNIT_CHARIOT;
			if (!naval) {
				if (!cavalry) continue;
				if (units_.tmpl[i] < CIV_UNIT_TEMPLATE_COUNT && (civ_unit_template(units_.tmpl[i]).flags & ROSTER_CHAMPION)) continue;
			}
			uint32_t uid = units_.id[i];
			if (uid == petra_scout_unit_id_) continue;
			if (units_.order_kind[i] == ORDER_GARRISON || petra_guards_critical_ent(i)) continue;
			if (std::find(plan.squad.begin(), plan.squad.end(), uid) != plan.squad.end()) continue;
			if (petra_attack_plan_.state != AttackPlan::MUSTER &&
			    std::find(petra_attack_plan_.squad.begin(), petra_attack_plan_.squad.end(), uid) != petra_attack_plan_.squad.end())
				continue;
			if (std::find(petra_defense_army_.defender_ids.begin(), petra_defense_army_.defender_ids.end(), uid) != petra_defense_army_.defender_ids.end())
				continue;
			plan.squad.push_back(uid);
		}
		if (petra_attack_plan_.state == AttackPlan::MUSTER) {
			// A mustering main army gives up the riders the raid takes.
			auto &main = petra_attack_plan_.squad;
			main.erase(std::remove_if(main.begin(), main.end(), [&](uint32_t uid) {
				return std::find(plan.squad.begin(), plan.squad.end(), uid) != plan.squad.end();
			}), main.end());
		}
		const bool ready = plan.squad.size() >= 4 || (plan.squad.size() >= 3 && plan.state_timer >= 30.0f);
		if (!ready) return;
		const float tx = plan.target_x, tz = plan.target_z;
		// A raid overseas waits for a transport ship there, and for it to be free.
		const uint16_t overseas = petra_plan_overseas(base_x, base_z, plan.target_building_id);
		if (overseas && (petra_transport_ships_on(overseas) == 0 ||
		                 (petra_transport_.state != PetraTransportPlan::IDLE && petra_transport_.state != PetraTransportPlan::FAILED)))
			return;
		plan.initial_squad_size = plan.squad.size();
		plan.state = AttackPlan::WALKING;
		plan.state_timer = 0.0f;
		++petra_attack_mgr_.launched[PETRA_ATTACK_RAID];
		petra_say(PETRA_CHAT_ATTACK, tx, tz);
		// Mirror the assault plan's transportPlan.js branch: a raid that
		// crosses water boards a transport instead of issuing a ground move.
		if (overseas) {
			if (!petra_start_transport(plan.squad, base_x, base_z, tx, tz, overseas))
				order_attack_move(plan.squad.data(), plan.squad.size(), tx, tz, false, false, true);
		} else {
			order_attack_move(plan.squad.data(), plan.squad.size(), tx, tz, false, false, true);
		}
		return;
	}

	if (plan.state == AttackPlan::WALKING || plan.state == AttackPlan::ATTACKED) {
		if (plan.squad.size() < 2 ||
		    (plan.initial_squad_size > 0 && (float)plan.squad.size() < 0.40f * (float)plan.initial_squad_size)) {
			plan.state = AttackPlan::REGROUP;
			plan.retreat_x = base_x;
			plan.retreat_z = base_z;
			plan.state_timer = 0.0f;
			if (!plan.squad.empty())
				order_walk(plan.squad.data(), plan.squad.size(), base_x, base_z, false);
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
				order_attack_move(plan.squad.data(), plan.squad.size(), tx, tz, false, false, true);
			} else {
				plan.state = AttackPlan::REGROUP;
				plan.retreat_x = base_x;
				plan.retreat_z = base_z;
				plan.state_timer = 0.0f;
				if (!plan.squad.empty())
					order_walk(plan.squad.data(), plan.squad.size(), base_x, base_z, false);
			}
		} else if (plan.state_timer > 36.0f) {
			plan.state = AttackPlan::REGROUP;
			plan.retreat_x = base_x;
			plan.retreat_z = base_z;
			plan.state_timer = 0.0f;
			if (!plan.squad.empty())
				order_walk(plan.squad.data(), plan.squad.size(), base_x, base_z, false);
		} else {
			plan.state = AttackPlan::ATTACKED;
			petra_check_captures(plan.squad, plan.target_x, plan.target_z);
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
	// Until a slot gets its own setting, every AI plays at the shared one.
	for (PetraSlot &slot : petra_slots_) slot.petra_config_ = petra_config_;
}

// Where player pid's PetraConfig lives right now: in the members while that
// AI runs (or between ticks for PETRA_PLAYER), else in its parked slot. While
// another AI runs, PETRA_PLAYER's state sits in that AI's slot.
PetraConfig &Simulation::petra_config_slot(uint8_t pid) {
	// Seed the slots with the shared setting before any one AI's edit.
	if (petra_slots_.size() < MAX_PLAYERS) {
		petra_slots_.resize(MAX_PLAYERS);
		for (PetraSlot &slot : petra_slots_) slot.petra_config_ = petra_config_;
	}
	if (pid == petra_pid_) return petra_config_;
	if (pid == PETRA_PLAYER) return petra_slots_[petra_pid_].petra_config_;
	return petra_slots_[pid].petra_config_;
}

const PetraConfig &Simulation::petra_config_for(uint8_t pid) const {
	if (pid >= MAX_PLAYERS || pid == petra_pid_ || petra_slots_.size() < MAX_PLAYERS) return petra_config_;
	if (pid == PETRA_PLAYER) return petra_slots_[petra_pid_].petra_config_;
	return petra_slots_[pid].petra_config_;
}

void Simulation::set_petra_difficulty(uint8_t d) {
	petra_config_.difficulty = petra_clamp_difficulty(d);
	apply_petra_config();
}

void Simulation::set_petra_behavior(uint8_t b) {
	petra_config_.behavior = petra_clamp_behavior(b);
	apply_petra_config();
}

void Simulation::set_petra_difficulty(uint8_t pid, uint8_t d) {
	if (pid >= MAX_PLAYERS) return;
	PetraConfig &c = petra_config_slot(pid);
	c.difficulty = petra_clamp_difficulty(d);
	c.recompute();
}

void Simulation::set_petra_behavior(uint8_t pid, uint8_t b) {
	if (pid >= MAX_PLAYERS) return;
	PetraConfig &c = petra_config_slot(pid);
	c.behavior = petra_clamp_behavior(b);
	c.recompute();
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
	petra_ps().pay(food, wood, gold, stone); // affordable: checked above
	w = PetraQueueAccount{};
	petra_queues_.want_age[queue] = 0.0f;
	return true;
}

void Simulation::petra_say(uint8_t kind, float x, float z, uint8_t to, int amount, uint8_t res) {
	// chatHelper.js lines (English only).
	static const char *kLines[] = {
		"I am launching an attack against you.",
		"Here is a gift. Make good use of it.",
		"Our alliance is over. We are now enemies.",
		"My armies failed while defending my empire. Please honor our alliance and send help!",
		"I will accept your offer.",
		"I cannot accept your offer.",
		"I will accept your offer if you send me a tribute of %d %s.",
		"I am still waiting for your tribute of %d %s.",
		"You did not send the tribute in time. I decline your offer.",
		"You have declared war on me. So be it.",
		// The two requests are shortened to fit PetraChatLine's 96 characters.
		"An alliance would help both our civilizations. Ally with me and I will respond in kind.",
		"I would like a neutrality pact with you. Become neutral with me and I will respond in kind.",
		"My offer for an alliance has expired.",
		"I have decided to revoke my offer for a neutrality pact.",
		"I am in need of %s, can you help? I will make it up to you.",
		"My empire regained its old strength, now it is time to seek revenge together!"
	};
	static const char *kRes[] = { "wood", "metal", "stone", "food" }; // ResourceType order
	if (kind > PETRA_CHAT_EMERGENCY_EXIT) kind = PETRA_CHAT_ATTACK;
	PetraChatLine line;
	line.kind = kind;
	line.from = petra_pid_;
	line.to = to;
	line.x = x;
	line.z = z;
	if (kind == PETRA_CHAT_TRIBUTE_REQUEST)
		std::snprintf(line.text, sizeof(line.text), kLines[kind], kRes[res < RES_COUNT ? res : 0]);
	else
		std::snprintf(line.text, sizeof(line.text), kLines[kind], amount, kRes[res < RES_COUNT ? res : 0]);
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

bool Simulation::petra_start_transport(const std::vector<uint32_t> &unit_ids, float from_x, float from_z, float to_x, float to_z, uint16_t sea) {
	float pickup_x = from_x, pickup_z = from_z;
	float drop_x = to_x, drop_z = to_z;
	petra_find_beach(from_x, from_z, to_x, to_z, pickup_x, pickup_z);
	petra_find_beach(to_x, to_z, from_x, from_z, drop_x, drop_z);

	// transportPlan.js assignShip: a free transport ship of that sea, the
	// roomiest (+50 for siege, as getBestShip), then the nearest to the pickup.
	uint32_t ship = 0;
	int best_room = -1;
	float best_d2 = 0.0f;
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != petra_pid_ || !petra_is_transport_ship(i)) continue;
		if (units_.id[i] == petra_transport_.ship_id && petra_transport_.state != PetraTransportPlan::IDLE &&
		    petra_transport_.state != PetraTransportPlan::FAILED) continue;
		if (sea && petra_ship_sea(i) != sea) continue;
		const int cap = ship_garrison_capacity(i);
		const int room = cap + (cap != 10 ? 50 : 0);
		const float dx = units_.pos_x[i] - pickup_x, dz = units_.pos_z[i] - pickup_z;
		const float d2 = dx * dx + dz * dz;
		if (room < best_room || (room == best_room && d2 >= best_d2)) continue;
		best_room = room;
		best_d2 = d2;
		ship = units_.id[i];
	}
	if (ship == 0) return false;

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
		if (petra_transport_.passenger_ids.size() >= (size_t)get_transport_capacity(ship)) break;
		int32_t idx = lookup_unit(uid);
		if (idx < 0 || !units_.alive[idx] || is_transport(uid)) continue;
		const int32_t sidx = lookup_unit(ship);
		if (sidx < 0 || !can_board((size_t)sidx, (size_t)idx)) continue;
		petra_transport_.passenger_ids.push_back(uid);
	}
	if (petra_transport_.passenger_ids.empty()) {
		petra_transport_.state = PetraTransportPlan::FAILED;
		petra_transport_.failed = true;
		return false;
	}
	order_walk(&ship, 1, pickup_x, pickup_z, false);
	order_walk(petra_transport_.passenger_ids.data(), petra_transport_.passenger_ids.size(), pickup_x, pickup_z, false);
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
	// transportPlan.js sails to the water beside the landing point: a ship
	// ordered onto the beach itself finds no path and stays where it is.
	auto sail_point = [&](float &x, float &z) {
		if (!nearest_water_for_ship((size_t)sidx, tp.drop_x, tp.drop_z, x, z)) { x = tp.drop_x; z = tp.drop_z; }
	};
	auto sail = [&]() {
		// Those not aboard stay behind; their boarding order would pull the ship back.
		const auto *aboard = get_transport_passengers(tp.ship_id);
		std::vector<uint32_t> left;
		for (uint32_t uid : tp.passenger_ids)
			if (!aboard || std::find(aboard->begin(), aboard->end(), uid) == aboard->end()) left.push_back(uid);
		for (uint32_t uid : left) {
			const int32_t i = lookup_unit(uid);
			if (i >= 0 && units_.alive[i] && units_.target_unit_id[i] == tp.ship_id) cmd_stop(&uid, 1);
		}
		float x, z;
		sail_point(x, z);
		order_walk(&tp.ship_id, 1, x, z, false);
		tp.state = PetraTransportPlan::SAILING;
		tp.state_timer = 0.0f;
	};

	if (tp.state == PetraTransportPlan::BOARDING) {
		size_t aboard = get_transport_passenger_count(tp.ship_id);
		if (tp.state_timer > 28.0f) {
			if (aboard == 0) {
				tp.state = PetraTransportPlan::FAILED;
				tp.failed = true;
				return;
			}
			sail(); // with whoever made it aboard
			return;
		}
		order_board_transport(tp.passenger_ids.data(), tp.passenger_ids.size(), tp.ship_id);
		aboard = get_transport_passenger_count(tp.ship_id);
		float dx = units_.pos_x[sidx] - tp.pickup_x;
		float dz = units_.pos_z[sidx] - tp.pickup_z;
		bool ship_ready = (dx * dx + dz * dz) <= 36.0f;
		if ((aboard >= 1 && aboard >= tp.passenger_ids.size()) ||
		    (aboard >= 2 && ship_ready && tp.state_timer >= 8.0f))
			sail();
	} else if (tp.state == PetraTransportPlan::SAILING) {
		float x, z;
		sail_point(x, z);
		const float dx = units_.pos_x[sidx] - x, dz = units_.pos_z[sidx] - z;
		const bool stopped = units_.action_state[sidx] == ACTION_IDLE && tp.state_timer > 2.0f;
		if (dx * dx + dz * dz <= 9.0f || stopped || tp.state_timer > 90.0f) {
			order_disembark(tp.ship_id, units_.pos_x[sidx], units_.pos_z[sidx]);
			tp.state = PetraTransportPlan::UNLOADING;
			tp.state_timer = 0.0f;
		}
	} else if (tp.state == PetraTransportPlan::UNLOADING) {
		if (get_transport_passenger_count(tp.ship_id) == 0) {
			tp.state = PetraTransportPlan::IDLE;
			tp.passenger_ids.clear();
		} else if (tp.state_timer >= 20.0f) {
			tp.state = PetraTransportPlan::FAILED; // no shore to unload onto: they stay aboard
			tp.failed = true;
		} else if (std::fmod(tp.state_timer, 3.0f) < dt) {
			order_disembark(tp.ship_id, units_.pos_x[sidx], units_.pos_z[sidx]);
		}
	}
}

uint32_t Simulation::petra_place_building(uint8_t btype, float near_x, float near_z, int min_r, int max_r, uint8_t queue) {
	// queueManager/canBuild: no search for a structure the EntityLimits refuse.
	if (btype < BLD_TYPE_COUNT && !petra_limit_allows_building(btype)) return 0;
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
				if (!petra_can_place(cx, cz, foot, btype)) continue;
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

// garrisonManager.js update: the enemies within a holder's arrow range (80 m,
// 20 world units, for a holder that doesn't shoot): defensive structures, melee or ranged
// siege, or other units (Gaia's only while fighting; Gaia is no foe here).
Simulation::PetraAround Simulation::petra_around(const Building &holder) const {
	PetraAround around;
	const BuildingTypeInfo &info = building_type_info(holder.type);
	const float range = info.attack_damage > 0.0f ? info.attack_range + tech_mods(holder.team).bld_range[holder.type] : 20.0f;
	for (const Building &b : buildings_) {
		if (!petra_foe(b.team) || b.hp <= 0.0f || b.build_progress < 1.0f || building_type_info(b.type).attack_damage <= 0.0f) continue;
		const float dx = b.x - holder.x, dz = b.z - holder.z;
		if (dx * dx + dz * dz <= range * range) { around.defense_structure = true; break; }
	}
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || !petra_foe(units_.team[i])) continue;
		const float dx = units_.pos_x[i] - holder.x, dz = units_.pos_z[i] - holder.z;
		if (dx * dx + dz * dz > range * range) continue;
		const uint64_t c = unit_tech_classes(units_.unit_type[i]);
		if (c & TC_SIEGE) {
			if (c & TC_MELEE) around.melee_siege = true;
			else around.range_siege = true;
		} else {
			around.unit = true;
			break;
		}
	}
	return around;
}

// garrisonManager.js keepGarrisoned. Petra garrisons for protection (a hurt
// unit healing, infantry adding arrows, support units and siege sheltering),
// and its hero for an emergency.
bool Simulation::petra_keep_garrisoned(size_t i, const Building &holder, const PetraAround &around) const {
	const uint8_t type = units_.unit_type[i];
	const uint64_t c = unit_tech_classes(type);
	const float health = units_.max_hp[i] > 0.0f ? units_.hp[i] / units_.max_hp[i] : 1.0f;
	const bool heals = structure_garrison_heal(holder) > 0.0f && unit_is_human(type);
	const bool needs_heal = heals && health < 1.0f;
	if (heals && health < petra_config_.garrison_hp_high) return true;
	if (type == UNIT_HERO) // TYPE_EMERGENCY
		return around.unit || around.defense_structure || around.melee_siege ||
			(around.range_siege && health < petra_config_.garrison_hp_high) || needs_heal;
	if (c & TC_INFANTRY) { // the holder's GarrisonArrowClasses
		if (around.unit || around.defense_structure) return true;
		if (around.melee_siege || around.range_siege) return !(c & TC_MELEE) || health < petra_config_.garrison_hp_low;
		return false;
	}
	if (c & TC_MELEE) return false;
	if (around.unit) return (c & (TC_CIVILIAN | TC_SIEGE)) != 0;
	if (c & TC_SIEGE) return around.melee_siege;
	return needs_heal;
}

// defenseManager.js garrisonUnitsInside: our nearest infantry (melee too unless
// only siege threatens it) fill a shooting structure an enemy is in range of.
// Units in an attack or raid plan stay with it; defenders leave the army.
void Simulation::petra_garrison_units_inside(size_t holder_idx, bool allow_melee, std::vector<int> &incoming) {
	const Building &holder = buildings_[holder_idx];
	if (!structure_has_enough_health(holder)) return;
	int slots = (int)holder.garrisoned_units.size() + incoming[holder_idx];
	if (slots >= holder.garrison_cap) return;
	std::vector<std::pair<float, uint32_t>> near;
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != petra_pid_) continue;
		const uint64_t c = unit_tech_classes(units_.unit_type[i]);
		if (!(c & TC_INFANTRY) || units_.unit_type[i] == UNIT_HERO || (!allow_melee && (c & TC_MELEE)) || !unit_garrisonable(i)) continue;
		if (units_.order_kind[i] == ORDER_GARRISON || units_.id[i] == petra_scout_unit_id_) continue;
		const uint32_t uid = units_.id[i];
		const auto &main = petra_attack_plan_.squad, &raid = petra_raid_plan_.squad;
		if (std::find(main.begin(), main.end(), uid) != main.end() || std::find(raid.begin(), raid.end(), uid) != raid.end()) continue;
		const float dx = units_.pos_x[i] - holder.x, dz = units_.pos_z[i] - holder.z;
		near.push_back({dx * dx + dz * dz, uid});
	}
	std::sort(near.begin(), near.end());
	auto &def = petra_defense_army_.defender_ids;
	for (const auto &[d2, uid] : near) {
		if (slots >= holder.garrison_cap) break;
		def.erase(std::remove(def.begin(), def.end(), uid), def.end());
		order_garrison(&uid, 1, holder.id);
		++slots;
		++incoming[holder_idx];
	}
}

// defenseManager.js garrisonAttackedUnit (the nearest healing structure) and
// garrisonSiegeUnit (the nearest fortress) with room and enough health.
uint32_t Simulation::petra_garrison_holder_for(size_t i, bool heals_only, bool siege, std::vector<int> &incoming) const {
	size_t best = SIZE_MAX;
	float best_d2 = 1e18f;
	for (size_t bi = 0; bi < buildings_.size(); ++bi) {
		const Building &b = buildings_[bi];
		if (b.team != petra_pid_ || b.wrecked || b.build_progress < 1.0f || b.garrison_cap <= 0) continue;
		if (heals_only && structure_garrison_heal(b) <= 0.0f) continue;
		if (siege && b.type != BLD_FORTRESS) continue;
		if (!structure_has_enough_health(b) || (int)b.garrisoned_units.size() + incoming[bi] >= b.garrison_cap) continue;
		const float dx = units_.pos_x[i] - b.x, dz = units_.pos_z[i] - b.z;
		const float d2 = dx * dx + dz * dz;
		if (d2 < best_d2 && petra_same_land(i, b)) { best_d2 = d2; best = bi; }
	}
	if (best == SIZE_MAX) return 0;
	++incoming[best];
	return buildings_[best].id;
}

// garrisonManager.js numberOfGarrisonedSlots counts ours on the way too.
std::vector<int> Simulation::petra_garrison_incoming() const {
	std::vector<int> incoming(buildings_.size(), 0);
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != petra_pid_ || units_.order_kind[i] != ORDER_GARRISON) continue;
		auto it = building_id_to_idx_.find(units_.order_target[i]);
		if (it != building_id_to_idx_.end()) ++incoming[it->second];
	}
	return incoming;
}

// PETRA.getLandAccess: the structure's land is the one next to its obstruction.
bool Simulation::petra_same_land(size_t i, const Building &b) const {
	auto [ux, uz] = nav_grid_.world_to_cell(units_.pos_x[i], units_.pos_z[i]);
	const uint16_t land = nav_grid_.global_region_at(ux, uz, PASS_INFANTRY);
	if (land == 0) return true; // no nav data, or the unit stands on a blocked cell
	const float r = b.half_size + GRID;
	const float off[4][2] = {{r, 0.0f}, {-r, 0.0f}, {0.0f, r}, {0.0f, -r}};
	for (const auto &o : off) {
		auto [cx, cz] = nav_grid_.world_to_cell(b.x + o[0], b.z + o[1]);
		if (nav_grid_.global_region_at(cx, cz, PASS_INFANTRY) == land) return true;
	}
	return false;
}

size_t Simulation::petra_attacked_unit_holder(size_t i, bool emergency, const std::vector<int> &incoming) const {
	size_t best = SIZE_MAX;
	float best_d2 = 1e18f;
	for (size_t bi = 0; bi < buildings_.size(); ++bi) {
		const Building &b = buildings_[bi];
		// Allied holders too in 0 A.D.; RTS0 garrisons only into its own.
		if (b.team != petra_pid_ || b.wrecked || b.build_progress < 1.0f || b.garrison_cap <= 0) continue;
		if (!emergency && structure_garrison_heal(b) <= 0.0f) continue;
		const int slots = (int)b.garrisoned_units.size() + (bi < incoming.size() ? incoming[bi] : 0);
		if (slots >= b.garrison_cap && (!emergency || b.garrisoned_units.empty())) continue;
		if (!structure_has_enough_health(b)) continue;
		const float dx = units_.pos_x[i] - b.x, dz = units_.pos_z[i] - b.z;
		const float d2 = dx * dx + dz * dz;
		if (d2 > best_d2 || !petra_same_land(i, b)) continue;
		best_d2 = d2;
		best = bi;
	}
	return best;
}

bool Simulation::petra_garrison_attacked_unit(size_t i, bool emergency, std::vector<int> &incoming) {
	// A Regicide hero with RegicideGarrison off can't garrison (no canGarrison):
	// the caller walks it home instead of ordering a garrison that is refused.
	if (!unit_garrisonable(i)) return false;
	if (incoming.size() < buildings_.size()) incoming.resize(buildings_.size(), 0);
	const size_t best = petra_attacked_unit_holder(i, emergency, incoming);
	if (best == SIZE_MAX) return false;
	const Building &b = buildings_[best];
	if (emergency && (int)b.garrisoned_units.size() >= b.garrison_cap) // make room for this one
		order_ungarrison_single(b.id, b.garrisoned_units.front());
	++incoming[best];
	const uint32_t uid = units_.id[i];
	order_garrison(&uid, 1, b.id);
	return true;
}

void Simulation::petra_manage_garrisons() {
	std::vector<int> incoming = petra_garrison_incoming();
	// garrisonManager.js update: unload whoever no longer needs the shelter, then
	// defenseManager.js checkEvents: arrows from a structure with an enemy in range.
	for (size_t bi = 0; bi < buildings_.size(); ++bi) {
		const Building &b = buildings_[bi];
		if (b.team != petra_pid_ || b.wrecked || b.build_progress < 1.0f || b.garrison_cap <= 0) continue;
		const bool shoots = building_type_info(b.type).attack_damage > 0.0f;
		if (b.garrisoned_units.empty() && !shoots) continue;
		const PetraAround around = petra_around(b);
		std::vector<uint32_t> leave;
		for (uint32_t uid : b.garrisoned_units) {
			const int32_t idx = lookup_unit(uid);
			if (idx >= 0 && units_.team[idx] == petra_pid_ && !petra_keep_garrisoned((size_t)idx, b, around)) leave.push_back(uid);
		}
		for (uint32_t uid : leave) order_ungarrison_single(b.id, uid);
		if (shoots && (around.unit || around.melee_siege || around.range_siege))
			petra_garrison_units_inside(bi, around.defense_structure || around.unit, incoming);
	}

	// defenseManager.js checkEvents: a support unit under attack below medium
	// health shelters in a healing structure; an attacked stone thrower in a fortress.
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || !petra_foe(units_.team[i]) || units_.target_unit_id[i] == 0) continue;
		const int32_t t = lookup_unit(units_.target_unit_id[i]);
		if (t < 0 || !units_.alive[t] || units_.team[t] != petra_pid_ || units_.order_kind[t] == ORDER_GARRISON) continue;
		if (petra_unit_reserved(units_.id[t])) continue;
		const uint64_t c = unit_tech_classes(units_.unit_type[t]);
		const float health = units_.max_hp[t] > 0.0f ? units_.hp[t] / units_.max_hp[t] : 1.0f;
		if (petra_is_critical_ent((size_t)t)) continue; // the victoryManager handles our hero
		uint32_t holder = 0;
		if ((c & TC_CIVILIAN) && health < PETRA_GARRISON_HP_MEDIUM) {
			petra_garrison_attacked_unit((size_t)t, false, incoming);
			continue;
		}
		if (c & TC_STONETHROWER) holder = petra_garrison_holder_for((size_t)t, false, true, incoming);
		if (holder) {
			const uint32_t uid = units_.id[t];
			order_garrison(&uid, 1, holder);
		}
	}
}

void Simulation::petra_manage_defense(float dt) {
	petra_defense_timer_ += dt;
	if (petra_defense_timer_ < 0.5f) return;
	petra_defense_timer_ = 0.0f;
	petra_manage_garrisons();

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
		// defenseManager.js disbands the army once no enemy is dangerous
		// (defenseArmy.js clear): a defender below low health goes to heal in the
		// nearest healing structure; the others come home instead of chasing foes
		// out of territory.
		std::vector<int> incoming;
		for (uint32_t uid : army.defender_ids) {
			int32_t idx = lookup_unit(uid);
			if (idx < 0) continue;
			const float hp_ratio = units_.max_hp[idx] > 0.0f ? units_.hp[idx] / units_.max_hp[idx] : 1.0f;
			if (hp_ratio < petra_config_.garrison_hp_low) {
				if (incoming.empty()) incoming = petra_garrison_incoming();
				if (petra_garrison_attacked_unit((size_t)idx, false, incoming)) continue;
			}
			if (home && units_.action_state[idx] == ACTION_ATTACK) order_walk(&uid, 1, home->x, home->z, false);
		}
		army.active = false;
		army.defender_ids.clear();
		return;
	}

	army.active = true;
	size_t needed = (size_t)std::ceil((float)army.foe_ids.size() * petra_config_.defense_ratio_own);
	needed = std::min(needed, (size_t)24);

	for (size_t i = 0; i < units_.count && army.defender_ids.size() < needed; ++i) {
		if (!units_.alive[i] || units_.team[i] != petra_pid_) continue;
		if (!petra_is_combat_unit(units_.unit_type[i])) continue;
		if (units_.id[i] == petra_scout_unit_id_) continue;
		if (units_.action_state[i] == ACTION_GARRISONED || units_.action_state[i] == ACTION_DYING) continue;
		if (units_.order_kind[i] == ORDER_GARRISON) continue; // garrisonManager has it
		if (in_attack_squad(units_.id[i]) || petra_is_critical_ent(i) || petra_guards_critical_ent(i)) continue;
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
			order_walk(&uid, 1, home->x, home->z, false);
		}
	}
}

void Simulation::petra_manage_victory(float dt) {
	petra_victory_timer_ += dt;
	if (petra_victory_timer_ < 0.5f) return;
	petra_victory_timer_ = 0.0f;

	// victoryManager.js checkEvents (every turn): a hero attacked below high
	// health retreats to heal; below low health to the closest holder at all.
	if (victory_condition_ == VICTORY_REGICIDE) {
		std::vector<size_t> attacked;
		for (size_t f = 0; f < units_.count; ++f) {
			if (!units_.alive[f] || !petra_foe(units_.team[f]) || units_.target_unit_id[f] == 0) continue;
			const int32_t t = lookup_unit(units_.target_unit_id[f]);
			if (t >= 0 && petra_is_critical_ent((size_t)t) && std::find(attacked.begin(), attacked.end(), (size_t)t) == attacked.end())
				attacked.push_back((size_t)t);
		}
		std::vector<int> incoming;
		for (size_t t : attacked) {
			const float health = units_.max_hp[t] > 0.0f ? units_.hp[t] / units_.max_hp[t] : 1.0f;
			if (health > petra_config_.garrison_hp_high) continue;
			if (incoming.empty()) incoming = petra_garrison_incoming();
			const bool emergency = health < petra_config_.garrison_hp_low;
			if (units_.order_kind[t] == ORDER_GARRISON) {
				// Severely wounded on the way to heal: the closest holder at all instead.
				if (!emergency) continue;
				auto it = building_id_to_idx_.find(units_.order_target[t]);
				if (it == building_id_to_idx_.end()) continue;
				if (it->second < incoming.size() && incoming[it->second] > 0) --incoming[it->second];
				const size_t best = petra_attacked_unit_holder(t, true, incoming);
				if (best == it->second || best == SIZE_MAX) { ++incoming[it->second]; continue; }
			}
			petra_retreat_critical_ent(t, emergency, incoming);
		}
	}

	// update(): the rest every 10 AI turns.
	if ((int)(petra_timer_ / 2.0f) == (int)((petra_timer_ - 0.5f) / 2.0f)) return;

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
			if (units_.action_state[i] == ACTION_IDLE) order_walk(&uid, 1, ox, oz, false);
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

	if (victory_condition_ == VICTORY_REGICIDE) {
		petra_manage_critical_ent_guards();
		// A hero back above high health fights again.
		for (size_t i = 0; i < units_.count; ++i) {
			if (!petra_is_critical_ent(i) || units_.stance[i] == STANCE_AGGRESSIVE) continue;
			if (units_.max_hp[i] > 0.0f && units_.hp[i] / units_.max_hp[i] <= petra_config_.garrison_hp_high) continue;
			const uint32_t uid = units_.id[i];
			order_set_stance(&uid, 1, STANCE_AGGRESSIVE);
		}
		petra_manage_critical_ent_healers();
	}

	// RTS0 relics are claimed for good by the first unit to reach them, so there
	// is nothing to guard once ours; Petra only goes after Gaia's.
	if (victory_condition_ == VICTORY_RELIC) petra_capture_gaia_relics();
}

// victoryManager.js criticalEnts: in regicide, our heroes.
bool Simulation::petra_is_critical_ent(size_t i) const {
	return victory_condition_ == VICTORY_REGICIDE && i < units_.count && units_.alive[i] &&
		units_.team[i] == petra_pid_ && units_.unit_type[i] == UNIT_HERO;
}

// guardEnts: ours guarding a hero (a military guard, or a healer).
bool Simulation::petra_guards_critical_ent(size_t i) const {
	if (!units_.alive[i] || units_.team[i] != petra_pid_ || units_.order_kind[i] != ORDER_GUARD) return false;
	const int32_t t = lookup_unit(units_.order_target[i]);
	return t >= 0 && petra_is_critical_ent((size_t)t);
}

// pickCriticalEntRetreatLocation: garrison, else turn passive (flee attacks)
// and walk to the closest base on the same land.
void Simulation::petra_retreat_critical_ent(size_t i, bool emergency, std::vector<int> &incoming) {
	const uint32_t uid = units_.id[i];
	// It leaves its attack plan and defense army.
	for (auto *squad : {&petra_attack_plan_.squad, &petra_raid_plan_.squad, &petra_defense_army_.defender_ids})
		squad->erase(std::remove(squad->begin(), squad->end(), uid), squad->end());
	if (petra_garrison_attacked_unit(i, emergency, incoming)) return;
	order_set_stance(&uid, 1, STANCE_PASSIVE);
	const PetraBase *best = nullptr;
	float best_d2 = 1e18f;
	for (const auto &base : petra_bases_) {
		auto it = building_id_to_idx_.find(base.anchor_building_id);
		if (it == building_id_to_idx_.end() || !petra_same_land(i, buildings_[it->second])) continue;
		const float dx = units_.pos_x[i] - base.x, dz = units_.pos_z[i] - base.z;
		if (dx * dx + dz * dz < best_d2) { best_d2 = dx * dx + dz * dz; best = &base; }
	}
	if (best) order_walk(&uid, 1, best->x, best->z, false);
}

// manageCriticalEntGuards: 4 + 5 * defensive soldiers guard each hero,
// champions first; citizen soldiers only while there are workers to spare.
void Simulation::petra_manage_critical_ent_guards() {
	int workers = 0;
	for (size_t i = 0; i < units_.count; ++i)
		if (units_.alive[i] && units_.team[i] == petra_pid_ && carry_cap(i, RES_WOOD) > 0 &&
		    !(unit_tech_classes(units_.unit_type[i]) & TC_SHIP) && !petra_guards_critical_ent(i)) ++workers;
	if (workers < 20) { // short of workers: send citizen soldier guards back to work
		for (size_t i = 0; i < units_.count && workers < 20; ++i) {
			if (!petra_guards_critical_ent(i) || !(unit_tech_classes(units_.unit_type[i]) & TC_CITIZENSOLDIER)) continue;
			const uint32_t uid = units_.id[i];
			cmd_stop(&uid, 1);
			++workers;
		}
	}
	const int want = 4 + (int)std::round(petra_config_.defensive * 5.0f);
	const int min_workers = 25, delta_workers = 3;
	for (size_t h = 0; h < units_.count; ++h) {
		if (!petra_is_critical_ent(h)) continue;
		const uint32_t hero = units_.id[h];
		int assigned = 0;
		for (size_t i = 0; i < units_.count; ++i)
			if (units_.order_kind[i] == ORDER_GUARD && units_.order_target[i] == hero && units_.alive[i] &&
			    units_.team[i] == petra_pid_ && units_.unit_type[i] != UNIT_HEALER) ++assigned;
		if (assigned >= want) continue;
		// tryAssignMilitaryGuard: units in no plan, on the hero's land (overseas guards need transports).
		auto free_for_guard = [&](size_t i) {
			if (!units_.alive[i] || units_.team[i] != petra_pid_ || i == h) return false;
			if (units_.order_kind[i] == ORDER_GUARD || units_.order_kind[i] == ORDER_GARRISON) return false;
			if (units_.action_state[i] == ACTION_GARRISONED || petra_unit_reserved(units_.id[i]) || petra_is_critical_ent(i)) return false;
			auto [hx, hz] = nav_grid_.world_to_cell(units_.pos_x[h], units_.pos_z[h]);
			auto [ux, uz] = nav_grid_.world_to_cell(units_.pos_x[i], units_.pos_z[i]);
			return nav_grid_.global_region_at(hx, hz, PASS_INFANTRY) == nav_grid_.global_region_at(ux, uz, PASS_INFANTRY);
		};
		auto champion = [&](size_t i) {
			return (unit_tech_classes(units_.unit_type[i]) & TC_CHAMPION) ||
				(units_.tmpl[i] < CIV_UNIT_TEMPLATE_COUNT && (civ_unit_template(units_.tmpl[i]).flags & ROSTER_CHAMPION));
		};
		for (int pass = 0; pass < 2 && assigned < want; ++pass) {
			for (size_t i = 0; i < units_.count && assigned < want; ++i) {
				if (pass == 1 && workers <= min_workers + delta_workers * assigned) break;
				if (!(unit_tech_classes(units_.unit_type[i]) & TC_SOLDIER) || champion(i) != (pass == 0)) continue;
				if (unit_is_ship(units_.unit_type[i]) || !free_for_guard(i)) continue;
				const uint32_t uid = units_.id[i];
				order_guard(&uid, 1, hero);
				++assigned;
				if (pass == 1) --workers;
			}
		}
	}
}

// manageCriticalEntHealers: with a temple, train 2 + 2 * defensive healers per
// hero, one at a time; healers with nothing to do guard the hero with fewest.
void Simulation::petra_manage_critical_ent_healers() {
	const int want = 2 + (int)std::round(petra_config_.defensive * 2.0f);
	std::vector<std::pair<uint32_t, int>> heroes; // hero id, healers guarding it
	for (size_t h = 0; h < units_.count; ++h)
		if (petra_is_critical_ent(h)) heroes.push_back({units_.id[h], 0});
	if (heroes.empty()) return;
	int guards = 0;
	for (size_t i = 0; i < units_.count; ++i) {
		if (!petra_guards_critical_ent(i)) continue;
		++guards;
		if (units_.unit_type[i] != UNIT_HEALER) continue;
		for (auto &hh : heroes) if (hh.first == units_.order_target[i]) ++hh.second;
	}
	// assignGuardToCriticalEnt for a healer: the hero with the fewest healers.
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != petra_pid_ || units_.unit_type[i] != UNIT_HEALER) continue;
		if (units_.action_state[i] != ACTION_IDLE || units_.order_kind[i] == ORDER_GUARD) continue;
		auto it = std::min_element(heroes.begin(), heroes.end(), [](const auto &a, const auto &b) { return a.second < b.second; });
		if (it->second >= want) break;
		const uint32_t uid = units_.id[i];
		order_guard(&uid, 1, it->first);
		++it->second;
		++guards;
	}
	const auto fewest = std::min_element(heroes.begin(), heroes.end(), [](const auto &a, const auto &b) { return a.second < b.second; });
	if (fewest->second >= want) return;
	if ((float)guards > std::min((float)pop_max(petra_pid_) / 10.0f, (float)pop_current(petra_pid_) / 4.0f)) return;
	const uint16_t tmpl = civ_default_template(players_[petra_pid_].civ, UNIT_HEALER);
	if (tmpl >= CIV_UNIT_TEMPLATE_COUNT) return;
	uint32_t temple = 0;
	for (const auto &b : buildings_) {
		if (b.team != petra_pid_ || b.build_progress < 1.0f || b.wrecked) continue;
		for (const auto &q : b.queue)
			if (q.unit_type == UNIT_HEALER) return; // queues.healer.hasQueuedUnits()
		if (!temple && b.type == BLD_TEMPLE) temple = b.id;
	}
	if (temple) petra_train_template(PETRA_QUEUE_MILITARY, temple, tmpl);
}

// victoryManager.js update / captureGaiaRelic: go for a Gaia relic in our
// territory, or one our units can see outside enemy territory, one new
// relic per update; send the nearest soldiers on its land until two are on the way.
void Simulation::petra_capture_gaia_relics() {
	bool started = false;
	for (const auto &rel : relics_) {
		if (rel.claimed) continue;
		int heading = 0;
		for (size_t i = 0; i < units_.count; ++i) {
			if (!units_.alive[i] || units_.team[i] != petra_pid_ || !units_.has_order[i]) continue;
			const float dx = units_.target_x[i] - rel.x, dz = units_.target_z[i] - rel.z;
			if (dx * dx + dz * dz <= 1.0f) ++heading;
		}
		if (heading == 0) { // not targeted yet
			if (started) continue;
			const int terr = territory_owner_at(rel.x, rel.z);
			bool go = terr == (int)petra_pid_;
			if (!go && (terr < 0 || !is_hostile(petra_pid_, (uint8_t)terr))) {
				for (size_t i = 0; i < units_.count && !go; ++i) {
					if (!units_.alive[i] || units_.team[i] != petra_pid_) continue;
					const float dx = units_.pos_x[i] - rel.x, dz = units_.pos_z[i] - rel.z;
					const float v = unit_vision_range(units_.unit_type[i]);
					go = dx * dx + dz * dz <= v * v;
				}
			}
			if (!go) continue;
			started = true;
		}
		auto [rx, rz] = nav_grid_.world_to_cell(rel.x, rel.z);
		const uint16_t land = nav_grid_.global_region_at(rx, rz, PASS_INFANTRY);
		std::vector<std::pair<float, uint32_t>> near;
		for (size_t i = 0; i < units_.count; ++i) {
			if (!units_.alive[i] || units_.team[i] != petra_pid_) continue;
			if (!(unit_tech_classes(units_.unit_type[i]) & TC_SOLDIER) || unit_is_ship(units_.unit_type[i])) continue; // canCapture
			if (units_.order_kind[i] == ORDER_GARRISON || units_.action_state[i] == ACTION_GARRISONED) continue;
			if (petra_unit_reserved(units_.id[i]) || petra_guards_critical_ent(i)) continue;
			const float dx = units_.pos_x[i] - rel.x, dz = units_.pos_z[i] - rel.z;
			if (dx * dx + dz * dz <= 1.0f) continue; // already there
			auto [ux, uz] = nav_grid_.world_to_cell(units_.pos_x[i], units_.pos_z[i]);
			if (land != 0 && nav_grid_.global_region_at(ux, uz, PASS_INFANTRY) != land) continue;
			near.push_back({dx * dx + dz * dz, units_.id[i]});
		}
		std::sort(near.begin(), near.end());
		for (size_t k = 0; k < near.size() && heading < 2; ++k, ++heading) {
			const uint32_t uid = near[k].second;
			order_walk(&uid, 1, rel.x, rel.z, false);
		}
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

	PetraDiplomacy &dip = petra_diplo_;
	dip.elapsed += dt;
	petra_diplo_timer_ += dt;
	if (petra_diplo_timer_ < 8.0f) return;
	petra_diplo_timer_ = 0.0f;
	const uint8_t me = petra_pid_;

	// checkEvents TributeExchanged: a tribute of the asked resource and amount
	// from a player we are waiting on seals the requested stance.
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		PetraReceivedRequest &req = dip.received[p];
		if (req.status == PetraReceivedRequest::WAITING_FOR_TRIBUTE) {
			req.paid += petra_tribute_inbox_[me][p][req.resource];
			if (req.paid >= (float)req.wanted) {
				req.status = PetraReceivedRequest::ACCEPTED;
				petra_change_diplomacy(p, req.type == 0 ? DIPLO_ALLY : DIPLO_NEUTRAL);
				petra_say(PETRA_CHAT_DIPLO_ACCEPT, 0.0f, 0.0f, p);
			}
		}
		// Tribute from an ally slowly raises our cooperative trait (food counts once, the rest twice).
		if (p != me && is_ally(me, p)) {
			float tributes = 0.0f;
			for (uint8_t r = 0; r < RES_COUNT; ++r) tributes += petra_tribute_inbox_[me][p][r] * (r == RES_FOOD ? 1.0f : 2.0f);
			petra_config_.cooperative = std::min(1.0f, petra_config_.cooperative + 0.0001f * tributes);
		}
		for (uint8_t r = 0; r < RES_COUNT; ++r) petra_tribute_inbox_[me][p][r] = 0.0f;
	}

	// checkEvents Attacked: so does an ally fighting an enemy inside our territory.
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] == me || !is_ally(me, units_.team[i]) || units_.target_unit_id[i] == 0) continue;
		const int32_t t = lookup_unit(units_.target_unit_id[i]);
		if (t < 0 || !units_.alive[t] || !is_hostile(me, units_.team[t])) continue;
		if (territory_owner_at(units_.pos_x[t], units_.pos_z[t]) != (int)me) continue;
		petra_config_.cooperative = std::min(1.0f, petra_config_.cooperative + 0.003f);
	}

	// checkEvents DiplomacyChanged / init: another player's stance toward us
	// changed. Allying with us or going neutral is a request we answer; RTS0
	// also answers a declaration of war in kind, so our units fight back.
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		if (p == me || players_[p].team == players_[me].team) continue;
		const uint8_t theirs = get_diplomacy(p, me);
		if (dip.seen_init && theirs == dip.seen_stance[p]) continue;
		dip.seen_stance[p] = theirs;
		if (!players_[p].active && !((participant_mask_ >> p) & 1)) continue;
		if (is_defeated(p)) continue;
		// A player we asked answered: match the stance it took, else drop the request.
		if (petra_sent_diplo_.sent_type[p]) {
			const uint8_t asked = petra_sent_diplo_.sent_type[p] == 1 ? DIPLO_ALLY : DIPLO_NEUTRAL;
			if (theirs == asked) petra_change_diplomacy(p, asked);
			petra_sent_diplo_.sent_type[p] = 0;
			continue;
		}
		PetraReceivedRequest &req = dip.received[p];
		// A player that had asked for a stance changed its mind.
		if ((req.status == PetraReceivedRequest::WAITING_FOR_TRIBUTE) &&
		    ((theirs != DIPLO_ALLY && req.type == 0) || (theirs == DIPLO_ENEMY && req.type == 1)))
			req = PetraReceivedRequest{};
		if (theirs == DIPLO_ALLY && get_diplomacy(me, p) != DIPLO_ALLY)
			petra_handle_diplomacy_request(p, 0);
		else if (theirs == DIPLO_NEUTRAL && get_diplomacy(me, p) == DIPLO_ENEMY)
			petra_handle_diplomacy_request(p, 1);
		else if (theirs == DIPLO_ENEMY && dip.seen_init && get_diplomacy(me, p) != DIPLO_ENEMY) {
			petra_change_diplomacy(p, DIPLO_ENEMY);
			petra_say(PETRA_CHAT_WAR_DECLARED, 0.0f, 0.0f, p);
		}
	}
	dip.seen_init = true;

	// checkEvents DiplomacyRequest (sent only between AIs): answer it, and ask the
	// requester for the tribute we want in return.
	for (uint8_t from = 0; from < MAX_PLAYERS; ++from) {
		const uint8_t asked = petra_diplo_request_inbox_[me][from];
		if (!asked) continue;
		petra_diplo_request_inbox_[me][from] = 0;
		if (from == me || is_defeated(from) || players_[from].team == players_[me].team) continue;
		petra_handle_diplomacy_request(from, asked - 1);
		const PetraReceivedRequest &req = dip.received[from];
		if (req.status == PetraReceivedRequest::WAITING_FOR_TRIBUTE) {
			petra_tribute_request_wanted_[from][me] = req.wanted - (int)req.paid;
			petra_tribute_request_res_[from][me] = req.resource;
		}
	}
	// checkEvents TributeRequest: an AI we asked wants a tribute; pay it if we can.
	for (uint8_t from = 0; from < MAX_PLAYERS; ++from) {
		const int wanted = petra_tribute_request_wanted_[me][from];
		if (wanted <= 0) continue;
		const uint8_t res = petra_tribute_request_res_[me][from];
		petra_tribute_request_wanted_[me][from] = 0;
		if (res >= RES_COUNT) continue;
		float available = petra_ps().res[res];
		for (const auto &acc : petra_queues_.accounts) {
			const float held[RES_COUNT] = {acc.wood, acc.gold, acc.stone, acc.food};
			available -= held[res];
		}
		if ((float)wanted < available) {
			int amounts[RES_COUNT] = {};
			amounts[res] = wanted;
			if (cmd_tribute(me, from, amounts)) dip.next_tribute = dip.elapsed + 15.0f;
		}
	}

	// checkRequestedTributes: warn once after 60 s, drop the offer 60 s later.
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		PetraReceivedRequest &req = dip.received[p];
		if (req.status != PetraReceivedRequest::WAITING_FOR_TRIBUTE || dip.elapsed < req.warn_time) continue;
		if (req.sent_warning) {
			req = PetraReceivedRequest{};
			petra_say(PETRA_CHAT_DIPLO_LAPSED, 0.0f, 0.0f, p);
		} else {
			req.sent_warning = true;
			req.warn_time = dip.elapsed + 60.0f;
			petra_say(PETRA_CHAT_DIPLO_WARNING, 0.0f, 0.0f, p, req.wanted - (int)req.paid, req.resource);
		}
	}

	// lastManStandingCheck, re-run on its timer.
	petra_last_man_standing_check();

	// update(): now and then ask a player for an alliance or a neutrality pact
	// (never during a ceasefire), and let an unanswered request lapse.
	if (!ceasefire_active()) {
		PetraSentDiplomacy &sd = petra_sent_diplo_;
		int enemies = 0, mutual_allies = 0;
		for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
			if (p == me || !((participant_mask_ >> p) & 1) || is_defeated(p)) continue; // seated players only
			if (get_diplomacy(me, p) == DIPLO_ENEMY) ++enemies;
			else if (is_ally(me, p) && is_ally(p, me)) ++mutual_allies;
		}
		// 0 A.D. counts Gaia among the enemies and us among the mutual allies. With
		// fewer than two enemies no request can go out, so the timer (and its
		// random draws) only starts once there are.
		if (enemies + 1 > 2) {
			if (sd.sent_lapse < 0.0f) sd.sent_lapse = 120.0f + sim_rng_.range_float(10.0f, 100.0f);
			if (dip.elapsed > sd.sent_lapse) {
				sd.sent_lapse = dip.elapsed + 300.0f + sim_rng_.range_float(10.0f, 100.0f);
				if (mutual_allies + 1 < enemies && sim_rng_.range_float(0.0f, 1.0f) < 0.1f)
					petra_send_diplomacy_request();
			}
		}
		for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
			if (!sd.sent_type[p] || dip.elapsed <= sd.sent_time[p] + 60.0f || pop_current(me) <= 70) continue;
			petra_say(sd.sent_type[p] == 1 ? PETRA_CHAT_ALLY_EXPIRED : PETRA_CHAT_NEUTRAL_EXPIRED, 0.0f, 0.0f, p);
			sd.sent_type[p] = 0;
		}
	}

	// tributes(), every 30 s: help allies that are short of a resource we have
	// plenty of. We give when we are the stronger one, or always under allied
	// victory, and never a resource our own queues are short of.
	if (dip.elapsed < dip.next_tribute) return;
	dip.next_tribute = dip.elapsed + 30.0f;
	float available[RES_COUNT];
	for (uint8_t r = 0; r < RES_COUNT; ++r) available[r] = petra_ps().res[r];
	for (const auto &acc : petra_queues_.accounts) {
		available[RES_FOOD] -= acc.food;
		available[RES_WOOD] -= acc.wood;
		available[RES_GOLD] -= acc.gold;
		available[RES_STONE] -= acc.stone;
	}
	// pickMostNeededResources: our two scarcest resources stay home.
	uint8_t order[RES_COUNT] = { 0, 1, 2, 3 };
	std::sort(order, order + RES_COUNT, [&](uint8_t x, uint8_t y) {
		return available[x] != available[y] ? available[x] < available[y] : x < y;
	});
	const int own_entities = petra_player_entities(me);
	const int own_pop = pop_current(me);
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		if (p == me || !is_ally(me, p) || !player_in_match(p)) continue;
		const bool donor = !last_man_standing() || petra_player_entities(p) < own_entities;
		int amounts[RES_COUNT] = {};
		bool any = false;
		for (uint8_t r = 0; r < RES_COUNT; ++r) {
			const float ally = players_[p].res[r];
			const bool needed = r == order[0] || r == order[1];
			if (donor && !needed && available[r] >= 200.0f && ally < 0.2f * available[r]) {
				amounts[r] = std::max(0, (int)std::floor(0.3f * available[r] - ally));
				any |= amounts[r] > 0;
			} else if (donor && (float)pop_current(p) < std::min(30.0f, 0.5f * (float)own_pop) &&
			           petra_ps().res[r] > 500.0f && ally < 100.0f) {
				amounts[r] = 100; // a weak ally gets a little of everything we have plenty of
				any = true;
			} else if (available[r] <= 0.0f && ally > petra_ps().res[r] + 600.0f && needed &&
			           dip.elapsed >= petra_sent_diplo_.next_tribute_request_all &&
			           dip.elapsed >= petra_sent_diplo_.next_tribute_request[r]) {
				// Ask the rich ally for a resource we need (chatRequestTribute).
				petra_sent_diplo_.next_tribute_request_all = dip.elapsed + 90.0f;
				petra_sent_diplo_.next_tribute_request[r] = dip.elapsed + 240.0f;
				petra_say(PETRA_CHAT_TRIBUTE_REQUEST, 0.0f, 0.0f, p, 0, r);
			}
		}
		if (any && cmd_tribute(me, p, amounts)) {
			for (uint8_t r = 0; r < RES_COUNT; ++r) available[r] -= (float)amounts[r];
			if (const auto *pbase = get_primary_petra_base())
				petra_say(PETRA_CHAT_TRIBUTE, pbase->x, pbase->z, p);
			else
				petra_say(PETRA_CHAT_TRIBUTE, 0.0f, 0.0f, p);
		}
	}
}

int Simulation::petra_player_entities(uint8_t player) const {
	int n = 0;
	for (size_t i = 0; i < units_.count; ++i)
		if (units_.alive[i] && units_.team[i] == player) ++n;
	for (const auto &b : buildings_)
		if (b.team == player && b.hp > 0.0f) ++n;
	return n;
}

void Simulation::petra_change_diplomacy(uint8_t player, uint8_t stance) {
	// diplomacyManager.js changePlayerDiplomacy: the AI only writes its own row.
	cmd_set_diplomacy(petra_pid_, player, stance);
}

// diplomacyManager.js sendDiplomacyRequest: ask the player we share the most
// enemies with (not one we turned down or are attacking) to ally if we are
// neutral to it, else for a neutrality pact. An AI gets the request directly.
void Simulation::petra_send_diplomacy_request() {
	const uint8_t me = petra_pid_;
	uint8_t current_enemy = 0xFF;
	if (petra_attack_plan_.state != AttackPlan::UNEXECUTED)
		if (auto it = building_id_to_idx_.find(petra_attack_plan_.target_building_id); it != building_id_to_idx_.end())
			current_enemy = buildings_[it->second].team;
	int player = -1, best = 0;
	for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
		if (i == me || !((participant_mask_ >> i) & 1) || is_defeated(i) || players_[i].team == players_[me].team) continue;
		if ((is_ally(me, i) && is_ally(i, me)) || i == current_enemy || petra_sent_diplo_.sent_type[i] ||
		    petra_diplo_.received[i].status == PetraReceivedRequest::DECLINED_FOREVER) continue;
		int mutual_enemies = 0;
		for (uint8_t j = 0; j < MAX_PLAYERS; ++j)
			if (j != i && j != me && ((participant_mask_ >> j) & 1) && !is_defeated(j) &&
			    get_diplomacy(i, j) == DIPLO_ENEMY && get_diplomacy(me, j) == DIPLO_ENEMY) ++mutual_enemies;
		if (mutual_enemies < best) continue;
		best = mutual_enemies;
		player = i;
	}
	if (player < 0) return;
	const bool ally = get_diplomacy(me, (uint8_t)player) == DIPLO_NEUTRAL;
	petra_sent_diplo_.sent_type[player] = ally ? 1 : 2;
	petra_sent_diplo_.sent_time[player] = petra_diplo_.elapsed;
	if (petra_runs_for((uint8_t)player)) petra_diplo_request_inbox_[player][me] = ally ? 1 : 2;
	petra_say(ally ? PETRA_CHAT_ALLY_REQUEST : PETRA_CHAT_NEUTRAL_REQUEST, 0.0f, 0.0f, (uint8_t)player);
}

void Simulation::petra_handle_diplomacy_request(uint8_t player, uint8_t type) {
	// diplomacyManager.js handleDiplomacyRequest. type 0 = ally, 1 = neutral.
	const uint8_t me = petra_pid_;
	PetraReceivedRequest &req = petra_diplo_.received[player];
	int enemies = 1, allies = 0; // 0 A.D. counts Gaia as an enemy and us as an ally
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		if (p != me && (!((participant_mask_ >> p) & 1) || is_defeated(p))) continue;
		if (is_ally(me, p)) ++allies;
		else if (is_hostile(me, p)) ++enemies;
	}
	const bool more_enemies_than_allies = enemies > allies;
	uint8_t current_enemy = 0xFF;
	if (petra_attack_plan_.state != AttackPlan::UNEXECUTED)
		if (auto it = building_id_to_idx_.find(petra_attack_plan_.target_building_id); it != building_id_to_idx_.end())
			current_enemy = buildings_[it->second].team;
	auto rand_bool = [&](float chance) { return sim_rng_.range_float(0.0f, 1.0f) < chance; };

	if (req.status == PetraReceivedRequest::DECLINED_FOREVER) {
		petra_say(PETRA_CHAT_DIPLO_DECLINE, 0.0f, 0.0f, player);
		return;
	}
	if (req.status == PetraReceivedRequest::WAITING_FOR_TRIBUTE) {
		petra_say(PETRA_CHAT_DIPLO_WARNING, 0.0f, 0.0f, player, req.wanted - (int)req.paid, req.resource);
		return;
	}
	// Be likely to decline for good, and always when it would leave us with
	// no one to fight or when we are attacking that player.
	if ((req.status == PetraReceivedRequest::NONE && civ_of(me) != civ_of(player) && rand_bool(0.6f)) ||
	    !more_enemies_than_allies || player == current_enemy) {
		req = PetraReceivedRequest{};
		req.status = PetraReceivedRequest::DECLINED_FOREVER;
		req.type = type;
		petra_say(PETRA_CHAT_DIPLO_DECLINE, 0.0f, 0.0f, player);
		return;
	}
	if ((type == 0 && petra_player_entities(player) < petra_player_entities(me) && rand_bool(0.4f)) ||
	    (type == 1 && more_enemies_than_allies && rand_bool(0.8f))) {
		req = PetraReceivedRequest{};
		req.status = PetraReceivedRequest::ACCEPTED;
		req.type = type;
		petra_change_diplomacy(player, type == 0 ? DIPLO_ALLY : DIPLO_NEUTRAL);
		petra_say(PETRA_CHAT_DIPLO_ACCEPT, 0.0f, 0.0f, player);
		return;
	}
	// Accept for a tribute of the resource we are shortest of.
	uint8_t need = RES_FOOD;
	for (uint8_t r = 0; r < RES_COUNT; ++r)
		if (petra_ps().res[r] < petra_ps().res[need]) need = r;
	int own_units = 0;
	for (size_t i = 0; i < units_.count; ++i)
		if (units_.alive[i] && units_.team[i] == me) ++own_units;
	req = PetraReceivedRequest{};
	req.status = PetraReceivedRequest::WAITING_FOR_TRIBUTE;
	req.type = type;
	req.resource = need;
	req.wanted = std::max(1000, own_units * (type == 0 ? 10 : 5));
	req.warn_time = petra_diplo_.elapsed + 60.0f;
	petra_say(PETRA_CHAT_DIPLO_TRIBUTE, 0.0f, 0.0f, player, req.wanted, need);
}

void Simulation::petra_last_man_standing_check() {
	// diplomacyManager.js lastManStandingCheck: once no enemy is left, turn
	// against the strongest remaining ally or neutral player. Teammates are
	// locked (RTS0 teams always are), and nothing happens during a ceasefire.
	PetraDiplomacy &dip = petra_diplo_;
	const uint8_t me = petra_pid_;
	if (ceasefire_active()) return;
	bool has_enemies = false, has_others = false;
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		if (p == me || !((participant_mask_ >> p) & 1) || is_defeated(p)) continue;
		if (is_hostile(me, p)) has_enemies = true;
		else if (players_[p].team != players_[me].team) has_others = true;
	}
	if (has_enemies || !has_others) {
		dip.waiting_to_betray = false;
		return;
	}
	// Wait a while before turning (randFloat(30, 60)).
	if (!dip.waiting_to_betray) {
		dip.betray_lapse = dip.elapsed + sim_rng_.range_float(30.0f, 60.0f);
		dip.waiting_to_betray = true;
		return;
	}
	if (dip.elapsed < dip.betray_lapse) return;
	// Not yet strong enough.
	int own_units = 0;
	for (size_t i = 0; i < units_.count; ++i)
		if (units_.alive[i] && units_.team[i] == me) ++own_units;
	if (own_units < 50) {
		dip.betray_lapse += 60.0f;
		return;
	}
	int target = -1, best = 0;
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		if (p == me || !((participant_mask_ >> p) & 1) || is_defeated(p)) continue;
		if (players_[p].team == players_[me].team) continue;
		int factor = petra_player_entities(p);
		if (get_diplomacy(me, p) == DIPLO_NEUTRAL) factor += 150; // turn on neutrals first
		if (factor < best) continue;
		best = factor;
		target = p;
	}
	if (target >= 0) {
		petra_change_diplomacy((uint8_t)target, DIPLO_ENEMY);
		dip.received[target] = PetraReceivedRequest{};
		dip.received[target].status = PetraReceivedRequest::DECLINED_FOREVER;
		if (const auto *pbase = get_primary_petra_base()) {
			if (target == 0) raise_alert(BattleAlert::ALERT_UNDER_ATTACK, pbase->x, pbase->z, 0, 1);
			petra_say(PETRA_CHAT_BETRAYAL, pbase->x, pbase->z, (uint8_t)target);
		} else {
			petra_say(PETRA_CHAT_BETRAYAL, 0.0f, 0.0f, (uint8_t)target);
		}
	}
	dip.betray_lapse = -1.0f;
	dip.waiting_to_betray = false;
}

// emergencyManager.js: an emergency starts when both the population and the
// structure count fall below a share of their best so far, or the territory
// roots (civil centres, military colonies) fall to a share of theirs. Petra
// only tells its allies; it ends once both counts grow 20% past what they were
// when it started, or a root is added.
void Simulation::petra_manage_emergency(float dt) {
	(void)dt;
	const int pop = pop_current(petra_pid_);
	int structs = 0;
	int roots = 0;
	for (const auto &b : buildings_) {
		if (b.team != petra_pid_ || b.hp <= 0.0f || b.wrecked) continue;
		structs++; // getOwnStructures counts foundations too
		if (b.build_progress >= 1.0f && (b.type == BLD_TOWN_CENTER || b.type == BLD_MILITARY_COLONY)) roots++;
	}
	auto &e = petra_emergency_;
	// init(): nothing to measure before the match has put anything down.
	if (!e.active && pop == 0 && structs == 0 && e.ref_pop == 0 && e.ref_structs == 0 && e.ref_roots == 0) return;
	// Each AI tells its own allies (chatHelper.js "/allies").
	auto tell_allies = [&](uint8_t kind) {
		for (uint8_t p = 0; p < MAX_PLAYERS; ++p)
			if (p != petra_pid_ && (players_[p].active || (participant_mask_ >> p & 1)) && !is_defeated(p) && is_ally(petra_pid_, p))
				petra_say(kind, 0.0f, 0.0f, p);
	};

	if (e.active) { // emergencyUpdate
		if ((pop > (float)e.ref_pop * 1.2f && structs > (float)e.ref_structs * 1.2f) || roots > e.ref_roots) {
			e.active = false;
			tell_allies(PETRA_CHAT_EMERGENCY_EXIT);
			e.ref_pop = pop;
			e.ref_structs = structs;
			e.ref_roots = roots;
		}
		return;
	}
	// A ratio against a reference of 0 is infinite in JS, never below a factor.
	auto below = [](int v, int ref, float f) { return v == 0 || (ref > 0 && (float)v / (float)ref < f); };
	if ((below(pop, e.ref_pop, petra_config_.emergency_pop_factor) && below(structs, e.ref_structs, petra_config_.emergency_struct_factor)) ||
	    (e.ref_roots > 0 && (float)roots / (float)e.ref_roots <= petra_config_.emergency_root_factor)) {
		e.active = true;
		tell_allies(PETRA_CHAT_EMERGENCY);
	}
	if (pop > e.ref_pop || e.active) e.ref_pop = pop;
	if (structs > e.ref_structs || e.active) e.ref_structs = structs;
	if (roots > e.ref_roots || e.active) e.ref_roots = roots;
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
	petra_attack_mgr_ = PetraAttackManager{};
	petra_opening_step_ = 0;
	petra_opening_timer_ = 0.0f;
	petra_defense_army_ = PetraDefenseArmy();
	petra_emergency_ = PetraEmergency();
	petra_queues_.reset();
	petra_transport_ = PetraTransportPlan();
	petra_defense_timer_ = 0.0f;
	petra_victory_timer_ = 0.0f;
	petra_diplo_timer_ = 0.0f;
	petra_diplo_ = PetraDiplomacy{};
	petra_sent_diplo_ = PetraSentDiplomacy{};
	ai_wonder_victory_timer_ = 0.0f;
}

void Simulation::petra_swap_slot(uint8_t pid) {
	if (petra_slots_.size() < MAX_PLAYERS) {
		petra_slots_.resize(MAX_PLAYERS);
		for (PetraSlot &s : petra_slots_) s.petra_config_ = petra_config_; // the shared setting
	}
	PetraSlot &slot = petra_slots_[pid];
#define RTSO_PETRA_SLOT_SWAP(m) std::swap(m, slot.m);
	RTSO_PETRA_SLOT_FIELDS(RTSO_PETRA_SLOT_SWAP)
#undef RTSO_PETRA_SLOT_SWAP
}

bool Simulation::player_in_match(uint8_t pid) const {
	if (pid >= MAX_PLAYERS || is_defeated(pid)) return false;
	// No slot seated (tests, skirmish without game setup): every slot counts.
	bool any_active = false;
	for (const PlayerState &ps : players_) any_active |= ps.active != 0;
	return !any_active || players_[pid].active;
}

bool Simulation::petra_runs_for(uint8_t pid) const {
	if (pid >= MAX_PLAYERS || is_defeated(pid)) return false;
	// No slot seated (tests, skirmish without game setup): legacy player 1 AI.
	bool any_active = false;
	for (const PlayerState &ps : players_) any_active |= ps.active != 0;
	if (!any_active) return pid == PETRA_PLAYER;
	return players_[pid].active && players_[pid].is_ai;
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
	petra_limit_cache_key_ = 0; // EntityLimits answers last one AI turn

	petra_refresh_queue_accounts(dt);

	// 1. Dynamic Danger & Threat Heatmap (64x64)
	update_petra_threat_map(dt);

	// 2. Multi-Base Expansion & Base Anchors (BasesManager / headquarters.js)
	update_petra_bases(dt);

	// 2b. No civic centre (nomad, or the last one lost): found one first (startingStrategy.js)
	petra_build_first_base();

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
	uint32_t version = SAVE_VERSION; // v6 per-player table; v7 adds each unit's and queued order's civ roster template; v8 a 256-bit researched-tech set per player; v9 the 8x8 diplomacy matrix; v10 per-player defeat and winners; v11 structure upgrades in progress and each building's structure fire; v12 0 A.D. barter price differences; v13 gate open/locked; v14 siege pack state (#40); v15 the rest of the per-tick state (save_tick_state); v16 tech ids from 55 up shifted by one for siege_pack_unpack; v17 each resource's food kind (fish); v18 Petra's attackManager.js state; v19 each projectile's source and each unit's partial xp (Loot.js); v20 UnitAI orders, order queues and work orders; v21 a Petra config per AI slot; v22 each player's trading goods shares; v23 formation controllers (save_tick_state); v24 the map extent (map_half_), so the fog grids load at their own size; v25 1024 animals with a held position (0 A.D. fauna); v26 map triggers and the water level (save_tick_state); v27 Petra's sent diplomacy requests and AI-to-AI request inboxes; v28 Gaia treasures and each ruin's gatherer cap; v29 Gaia scenery (fences, columns, obelisks); v30 every player's StatisticsTracker; v31 the terrain heightmap and each building's rotation; v32 Nomad, RegicideGarrison, Regicide heroes, player colours and Disable Treasures
	out.write((const char *)&magic, sizeof(magic));
	out.write((const char *)&version, sizeof(version));
	// v24: the map extent first, so a load into a sim at another size (a fresh
	// reset(), a replay, a rejoin) sizes the FoW, nav and territory grids before reading them.
	out.write((const char *)&map_half_, sizeof(map_half_));
	// v31: the heightmap, so a load restores the saved map's terrain (projectile
	// heights, water, line of sight) instead of keeping whatever the sim held.
	{
		const int32_t res = terrain_res_;
		const uint32_t n = (uint32_t)terrain_heights_.size();
		out.write((const char *)&res, sizeof(res));
		out.write((const char *)&n, sizeof(n));
		if (n) out.write((const char *)terrain_heights_.data(), (std::streamsize)(n * sizeof(float)));
	}

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
		out.write((const char *)&units_.pack_state[u], sizeof(uint8_t));
		out.write((const char *)&units_.pack_elapsed[u], sizeof(float));
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
		out.write((const char *)&b.rot, sizeof(b.rot));

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
	out.write((const char *)barter_price_diff_, sizeof(barter_price_diff_));
	out.write((const char *)&barter_timer_, sizeof(barter_timer_));
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

	// 6. v15: everything else a tick reads (tools/test_save_hash.cpp checks it)
	save_tick_state(out);
	return true;
}

bool Simulation::load_state(const char *filepath) {
	std::ifstream in(filepath, std::ios::binary);
	if (!in.is_open()) return false;

	uint32_t magic = 0, version = 0;
	in.read((char *)&magic, sizeof(magic));
	in.read((char *)&version, sizeof(version));
	if (magic != 0x52545330 || version < 1 || version > SAVE_VERSION) return false;
	if (version >= 24) {
		float half = 0.0f;
		in.read((char *)&half, sizeof(half));
		if (!in.good() || !(half > 0.0f) || half > 4096.0f) return false;
		if (half != map_half_) set_map_half(half);
	}
	if (version >= 31) {
		int32_t res = 0;
		uint32_t n = 0;
		in.read((char *)&res, sizeof(res));
		in.read((char *)&n, sizeof(n));
		if (!in.good() || res < 0 || res > 8193 || (n && (uint64_t)n != (uint64_t)res * (uint64_t)res)) return false;
		std::vector<float> heights(n);
		if (n) in.read((char *)heights.data(), (std::streamsize)(n * sizeof(float)));
		if (!in.good()) return false;
		set_terrain_data(heights, res, terrain_water_height_); // the water level comes with the tick state
	}

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
				if (version < 16) ps.techs = shift_techs_before_v16(ps.techs);
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
	if (!in.good() || num_res > (1u << 22)) return false;
	resources_.count = 0;
	resources_.reserve(num_res); // a reset() sim holds 512; bigger maps have more
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
	if (!in.good() || num_units > UNIT_CAP) return false;
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
		uint8_t pstate = unit_has_pack(utmpl, utype) ? PACK_PACKED : PACK_NONE;
		float pelapsed = 0.0f;
		if (version >= 14) {
			in.read((char *)&pstate, sizeof(pstate));
			in.read((char *)&pelapsed, sizeof(pelapsed));
		}

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
		units_.pack_state[idx] = pstate;
		units_.pack_elapsed[idx] = pelapsed;
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
		if (version < 16) bld.research_tech = shift_tech_before_v16(bld.research_tech);
		in.read((char *)&bld.research_timer, sizeof(bld.research_timer));
		in.read((char *)&bld.research_duration, sizeof(bld.research_duration));
		in.read((char *)&bld.capture_points, sizeof(bld.capture_points));
		in.read((char *)&bld.max_capture_points, sizeof(bld.max_capture_points));
		if (version >= 5)
			in.read((char *)bld.capture_points_by_team.data(), sizeof(bld.capture_points_by_team));
		else
			bld.capture_points_by_team.fill(0.0f);
		bld.is_burning = false; // not saved before v11
		if (version >= 11) {
			in.read((char *)&bld.upgrade_to, sizeof(bld.upgrade_to));
			in.read((char *)&bld.upgrade_timer, sizeof(bld.upgrade_timer));
			in.read((char *)&bld.upgrade_duration, sizeof(bld.upgrade_duration));
			in.read((char *)bld.upgrade_paid.data(), sizeof(bld.upgrade_paid));
			in.read((char *)&bld.is_burning, sizeof(bld.is_burning));
		}
		if (version >= 13) {
			in.read((char *)&bld.gate_open, sizeof(bld.gate_open));
			in.read((char *)&bld.gate_locked, sizeof(bld.gate_locked));
		}
		if (version >= 31) in.read((char *)&bld.rot, sizeof(bld.rot));
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
		if (version >= 12) {
			in.read((char *)barter_price_diff_, sizeof(barter_price_diff_));
			in.read((char *)&barter_timer_, sizeof(barter_timer_));
		} else {
			// v2-v11 kept invented multiplicative buy and sell prices; start 0 A.D. prices fresh.
			float old_prices[2][4];
			in.read((char *)old_prices, sizeof(old_prices));
			for (float &d : barter_price_diff_) d = 0.0f;
			barter_timer_ = 0.0f;
		}
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
		for (int i = 0; i < 4; ++i) { barter_price_diff_[i] = 0.0f; market_trade_history_[i] = 0; }
		barter_timer_ = 0.0f;
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
		for (const auto &t : treasures_) max_id = std::max(max_id, t.id);
		for (const auto &sc : scenery_) max_id = std::max(max_id, sc.id);
		next_id_ = max_id + 1;
	}

	if (version >= 15) {
		if (!load_tick_state(in, version)) return false;
		territory_regions_dirty_ = true;
		if (version < 23)
			for (size_t u = 0; u < units_.count; ++u) units_.formation[u] = FORMATION_NONE; // no controllers saved
		formation_refresh_groups();
	} else {
		// Older saves carry none of it: start the per-tick state fresh.
		for (size_t u = 0; u < units_.count; ++u) {
			units_.anchor_x[u] = units_.target_x[u];
			units_.anchor_z[u] = units_.target_z[u];
			units_.status_effects[u] = 0;
			units_.xp[u] = 0;
			units_.waypoint_cur[u] = units_.waypoint_count[u] = 0;
			units_.poison_timer[u] = units_.snare_timer[u] = units_.knockback_timer[u] = 0.0f;
			units_.knockback_dir_x[u] = units_.knockback_dir_z[u] = 0.0f;
			units_.slow_timer[u] = units_.bleed_timer[u] = units_.burning_timer[u] = 0.0f;
		}
		for (WaypointPath &wp : unit_paths_) wp.clear();
		projectiles_.clear();
	}

	rebuild_all_tech_mods();
	rebuild_nav_grid(); // the loaded structures (and locked gates) block paths, not the old ones
	if (version >= 26) trigger_event(TRIGGER_ON_DESERIALIZED); // Trigger.js OnGlobalDeserialized
	return true;
}

// Per-tick state (save v15). One field list serves save and load, so the two
// can't drift apart. Plain values and padding-free structs are copied raw; a
// struct with padding bytes goes through ar.padded(), which writes a copy built
// field by field in zeroed memory, so the file never carries uninitialised
// padding (two identical runs write identical saves; test_save_hash checks it).
// The file layout is the raw layout either way. A static_assert on each padded
// struct's size fails the build when a field is added without listing it here.
struct TickSaveAr {
	std::ofstream &out;
	uint32_t version = Simulation::SAVE_VERSION;
	bool ok = true;
	static constexpr bool loading = false;
	template <class T> void pod(T &v) {
		static_assert(std::is_trivially_copyable_v<T>, "raw save needs a plain value");
		out.write((const char *)&v, sizeof(T));
	}
	template <class T, class F> void padded(T &v, F &&copy_fields) {
		static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>);
		alignas(T) unsigned char buf[sizeof(T)] = {};
		T *z = new (buf) T; // default member initialisers only; the padding stays zero
		copy_fields(*z, v);
		out.write((const char *)buf, sizeof(T));
	}
};
struct TickLoadAr {
	std::ifstream &in;
	uint32_t version; // the file's
	bool ok = true;
	static constexpr bool loading = true;
	template <class T> void pod(T &v) {
		static_assert(std::is_trivially_copyable_v<T>, "raw load needs a plain value");
		in.read((char *)&v, sizeof(T));
		ok = ok && in.good();
	}
	template <class T, class F> void padded(T &v, F &&) { pod(v); }
};
template <class Ar, class T, class F> static void io_vec(Ar &ar, std::vector<T> &v, uint32_t max_n, F &&each) {
	uint32_t n = (uint32_t)v.size();
	ar.pod(n);
	if constexpr (Ar::loading) {
		if (!ar.ok || n > max_n) { ar.ok = false; return; }
		v.assign(n, T{});
	}
	for (T &x : v) each(x);
}
template <class Ar, class T> static void io_vec_pod(Ar &ar, std::vector<T> &v, uint32_t max_n) {
	io_vec(ar, v, max_n, [&](T &x) { ar.pod(x); });
}

template <class Ar> void tick_state_io(Ar &ar, Simulation &s) {
	using S = Simulation;
	auto ids = [&](std::vector<uint32_t> &v) { io_vec_pod(ar, v, S::UNIT_CAP); };
	auto arr = [](auto &d, const auto &src) { std::memcpy(&d, &src, sizeof(d)); };

	// Padded structs, field by field.
	static_assert(sizeof(WaypointPath) == 532, "list the new WaypointPath field below");
	auto path = [&](WaypointPath &p) {
		ar.padded(p, [&](WaypointPath &d, const WaypointPath &v) {
			arr(d.xs, v.xs); arr(d.zs, v.zs); d.count = v.count; d.cur = v.cur;
			d.has_goal = v.has_goal; d.goal_x = v.goal_x; d.goal_z = v.goal_z;
		});
	};
	static_assert(sizeof(Projectile) == 76, "list the new Projectile field below");
	auto projectile = [&](Projectile &p) {
		if constexpr (Ar::loading) {
			// Before v19 a projectile ended at alive (72 bytes) and had no source.
			if (ar.version < SAVE_VERSION_LOOT) {
				ar.in.read((char *)&p, offsetof(Projectile, source_id));
				p.source_id = 0;
				ar.ok = ar.ok && ar.in.good();
				return;
			}
		}
		ar.padded(p, [&](Projectile &d, const Projectile &v) {
			d.id = v.id; d.start_x = v.start_x; d.start_y = v.start_y; d.start_z = v.start_z;
			d.target_x = v.target_x; d.target_y = v.target_y; d.target_z = v.target_z;
			d.current_x = v.current_x; d.current_y = v.current_y; d.current_z = v.current_z;
			d.progress = v.progress; d.speed = v.speed; d.arc_height = v.arc_height; d.damage = v.damage;
			d.damage_type = v.damage_type; d.visual = v.visual; d.status_effect = v.status_effect; d.team = v.team;
			d.target_unit_id = v.target_unit_id; d.target_building_id = v.target_building_id; d.alive = v.alive;
			d.splash = v.splash; d.source_id = v.source_id;
		});
	};
	static_assert(sizeof(S::TreasureRelic) == 24, "list the new TreasureRelic field below");
	auto relic = [&](S::TreasureRelic &r) {
		ar.padded(r, [&](S::TreasureRelic &d, const S::TreasureRelic &v) {
			d.id = v.id; d.x = v.x; d.z = v.z; d.res_type = v.res_type; d.amount = v.amount;
			d.claimed = v.claimed; d.owner_team = v.owner_team;
		});
	};
	static_assert(sizeof(RecentDeath) == 16, "list the new RecentDeath field below");
	auto death = [&](RecentDeath &r) {
		ar.padded(r, [&](RecentDeath &d, const RecentDeath &v) {
			d.x = v.x; d.z = v.z; d.team = v.team; d.killer_team = v.killer_team; d.type = v.type;
			d.is_building = v.is_building; d.death_damage_applied = v.death_damage_applied; d.death_damage = v.death_damage;
		});
	};
	static_assert(sizeof(S::TerritoryRoot) == 16, "list the new TerritoryRoot field below");
	auto root = [&](S::TerritoryRoot &r) {
		ar.padded(r, [&](S::TerritoryRoot &d, const S::TerritoryRoot &v) { d.x = v.x; d.z = v.z; d.radius = v.radius; d.team = v.team; });
	};
	static_assert(sizeof(S::PetraBase) == 36, "list the new PetraBase field below");
	auto base = [&](S::PetraBase &b) {
		ar.padded(b, [&](S::PetraBase &d, const S::PetraBase &v) {
			d.id = v.id; d.anchor_building_id = v.anchor_building_id; d.x = v.x; d.z = v.z;
			d.is_primary = v.is_primary; d.assigned_workers = v.assigned_workers; d.radius = v.radius;
			d.danger_level = v.danger_level; d.resources_depleted = v.resources_depleted;
		});
	};

	// 1. Unit columns the main unit record leaves out, in unit order.
	if constexpr (Ar::loading)
		if (s.unit_paths_.size() < S::UNIT_CAP) s.unit_paths_.resize(S::UNIT_CAP);
	auto &U = s.units_;
	for (size_t i = 0; i < U.count; ++i) {
		ar.pod(U.vel_x[i]); ar.pod(U.vel_z[i]); ar.pod(U.anchor_x[i]); ar.pod(U.anchor_z[i]);
		ar.pod(U.status_effects[i]); ar.pod(U.xp[i]); ar.pod(U.waypoint_cur[i]); ar.pod(U.waypoint_count[i]);
		ar.pod(U.poison_timer[i]); ar.pod(U.snare_timer[i]); ar.pod(U.knockback_timer[i]);
		ar.pod(U.knockback_dir_x[i]); ar.pod(U.knockback_dir_z[i]);
		ar.pod(U.slow_timer[i]); ar.pod(U.bleed_timer[i]); ar.pod(U.burning_timer[i]);
		if (ar.version >= SAVE_VERSION_LOOT) ar.pod(U.xp_partial[i]);
		else U.xp_partial[i] = 0.0f;
		uint8_t has_path = i < s.unit_paths_.size() && s.unit_paths_[i].count > 0;
		ar.pod(has_path);
		if (!ar.ok) return;
		if constexpr (Ar::loading) s.unit_paths_[i].clear();
		if (has_path) path(s.unit_paths_[i]);
	}
	if constexpr (Ar::loading)
		for (size_t i = U.count; i < s.unit_paths_.size(); ++i) s.unit_paths_[i].clear();
	// Load guesses these from build_progress and hp; entity limits wreck structures with hp left.
	for (S::Building &b : s.buildings_) { ar.pod(b.committed); ar.pod(b.wrecked); }
	io_vec(ar, s.projectiles_, 1u << 20, projectile);
	io_vec(ar, s.relics_, 1u << 16, relic);
	static_assert(sizeof(S::Treasure) == 24, "Treasure is saved raw (no padding: pad[] is explicit and zeroed); list a new field here");
	static_assert(sizeof(S::RuinsCap) == 8, "RuinsCap is saved raw; list a new field here");
	if (ar.version >= 28) {
		io_vec_pod(ar, s.treasures_, 1u << 16);
		io_vec_pod(ar, s.ruins_caps_, 1u << 20);
	} else if constexpr (Ar::loading) {
		s.treasures_.clear();
		s.ruins_caps_.clear();
	}
	static_assert(sizeof(S::Scenery) == 32, "Scenery is saved raw (pad[] explicit and zeroed); list a new field here");
	if (ar.version >= 29) io_vec_pod(ar, s.scenery_, 1u << 16);
	else if constexpr (Ar::loading) s.scenery_.clear();
	static_assert(sizeof(S::FaunaStorage) == 68616, "list the new FaunaStorage field below");
	if constexpr (Ar::loading) {
		if (ar.version < 25) {
			// Saves before v25 held 256 animals and no held position.
			struct Fauna256 {
				size_t count;
				uint32_t id[256]; float pos_x[256], pos_z[256], vel_x[256], vel_z[256], rot_y[256], hp[256], max_hp[256];
				int food_amount[256]; uint8_t fauna_type[256], behavior[256], alive[256];
				float state_timer[256], attack_timer[256]; uint32_t target_unit_id[256], corral_id[256]; float fatten_timer[256];
			};
			static_assert(sizeof(Fauna256) == 15112);
			auto old = std::make_unique<Fauna256>();
			ar.pod(*old);
			if (!ar.ok || old->count > 256) { ar.ok = false; return; }
			S::FaunaStorage &F = s.fauna_;
			F = S::FaunaStorage{};
			F.count = old->count;
			for (size_t f = 0; f < old->count; ++f) {
				F.id[f] = old->id[f]; F.pos_x[f] = old->pos_x[f]; F.pos_z[f] = old->pos_z[f];
				F.vel_x[f] = old->vel_x[f]; F.vel_z[f] = old->vel_z[f]; F.rot_y[f] = old->rot_y[f];
				F.hp[f] = old->hp[f]; F.max_hp[f] = old->max_hp[f]; F.food_amount[f] = old->food_amount[f];
				F.fauna_type[f] = old->fauna_type[f]; F.behavior[f] = old->behavior[f]; F.alive[f] = old->alive[f];
				F.state_timer[f] = old->state_timer[f]; F.attack_timer[f] = old->attack_timer[f];
				F.target_unit_id[f] = old->target_unit_id[f]; F.corral_id[f] = old->corral_id[f];
				F.fatten_timer[f] = old->fatten_timer[f]; F.home_x[f] = F.pos_x[f]; F.home_z[f] = F.pos_z[f];
			}
		}
	}
	if (!Ar::loading || ar.version >= 25) ar.padded(s.fauna_, [&](S::FaunaStorage &d, const S::FaunaStorage &v) {
		d.count = v.count;
		arr(d.id, v.id); arr(d.pos_x, v.pos_x); arr(d.pos_z, v.pos_z); arr(d.vel_x, v.vel_x); arr(d.vel_z, v.vel_z);
		arr(d.rot_y, v.rot_y); arr(d.hp, v.hp); arr(d.max_hp, v.max_hp); arr(d.food_amount, v.food_amount);
		arr(d.fauna_type, v.fauna_type); arr(d.behavior, v.behavior); arr(d.alive, v.alive);
		arr(d.state_timer, v.state_timer); arr(d.attack_timer, v.attack_timer); arr(d.target_unit_id, v.target_unit_id);
		arr(d.corral_id, v.corral_id); arr(d.fatten_timer, v.fatten_timer);
		arr(d.home_x, v.home_x); arr(d.home_z, v.home_z);
	});
	if constexpr (Ar::loading) {
		if (s.fauna_.count > S::FaunaStorage::MAX_FAUNA) { ar.ok = false; return; }
		s.fauna_id_to_idx_.clear();
		for (size_t f = 0; f < s.fauna_.count; ++f) s.fauna_id_to_idx_[s.fauna_.id[f]] = f;
	}
	// Transport ships' passengers, sorted so the file doesn't depend on hash-map order.
	std::vector<std::pair<uint32_t, std::vector<uint32_t>>> ships(s.transport_passengers_.begin(), s.transport_passengers_.end());
	std::sort(ships.begin(), ships.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
	io_vec(ar, ships, S::UNIT_CAP, [&](std::pair<uint32_t, std::vector<uint32_t>> &kv) { ar.pod(kv.first); ids(kv.second); });
	if constexpr (Ar::loading) {
		s.transport_passengers_.clear();
		for (auto &kv : ships) s.transport_passengers_[kv.first] = std::move(kv.second);
	}

	// 2. Timers and counters that ticks read.
	ar.pod(s.rate_window_timer_); ar.pod(s.upkeep_timer_); ar.pod(s.entity_limits_timer_);
	ar.pod(s.gate_timer_); ar.pod(s.fow_timer_); ar.pod(s.territory_timer_);
	// Territory is recomputed once a second, so the saved grid can't be rebuilt from the buildings on load.
	for (Grid<uint8_t> *g : {&s.territory_grid_, &s.influence_grid_}) {
		int32_t w = g->w, h = g->h;
		ar.pod(w); ar.pod(h);
		if constexpr (Ar::loading) {
			if (!ar.ok || w < 0 || h < 0 || w > S::MAX_GRID_RES || h > S::MAX_GRID_RES) { ar.ok = false; return; }
			g->resize(w, h, 0);
		}
		io_vec_pod(ar, g->data, (uint32_t)(S::MAX_GRID_RES * S::MAX_GRID_RES));
		if (g->data.size() != (size_t)w * (size_t)h) { ar.ok = false; return; }
	}
	io_vec(ar, s.territory_roots_, 1u << 16, root);
	ar.pod(s.territory_roots_res_); ar.pod(s.territory_roots_half_);
	ar.pod(s.aura_timer_); ar.pod(s.relic_victory_timer_); ar.pod(s.last_alert_time_);
	for (RecentDeath &d : s.recent_deaths_) death(d);
	ar.pod(s.recent_death_head_); ar.pod(s.record_);
	for (PlayerState &ps : s.players_) ar.pod(ps.gathered_window);
	// Match settings from Game Setup; ticks read them, so a save carries its own rules.
	ar.pod(s.victory_condition_); ar.pod(s.waves_enabled_); ar.pod(s.last_man_standing_); ar.pod(s.pop_cap_);
	ar.pod(s.ceasefire_seconds_); ar.pod(s.reveal_map_); ar.pod(s.explore_map_);
	ar.pod(s.wonder_duration_); ar.pod(s.relic_duration_); ar.pod(s.relic_count_);
	// v32: Nomad, RegicideGarrison and the Regicide heroes, player colours, Disable Treasures.
	if (ar.version >= 32) {
		ar.pod(s.nomad_); ar.pod(s.regicide_garrison_); ar.pod(s.disable_treasures_);
		for (uint32_t &h : s.regicide_hero_) ar.pod(h);
		for (uint8_t &c : s.player_color_) {
			ar.pod(c);
			if constexpr (Ar::loading)
				if (c >= PLAYER_COLOR_COUNT) ar.ok = false;
		}
	} else if constexpr (Ar::loading) {
		s.nomad_ = false; s.regicide_garrison_ = false; s.disable_treasures_ = false;
		std::fill(std::begin(s.regicide_hero_), std::end(s.regicide_hero_), 0u);
		for (uint8_t p = 0; p < MAX_PLAYERS; ++p) s.player_color_[p] = p;
	}

	// 3. Every AI's Petra state.
	static_assert(sizeof(PetraConfig) == 100, "list the new PetraConfig field below");
	auto io_config = [&](PetraConfig &c) {
		ar.padded(c, [](PetraConfig &d, const PetraConfig &v) {
			d.difficulty = v.difficulty; d.behavior = v.behavior; d.aggressive = v.aggressive; d.defensive = v.defensive;
			d.cooperative = v.cooperative; d.pop_scaling = v.pop_scaling; d.gather_rate = v.gather_rate; d.build_time = v.build_time;
			d.defense_ratio_own = v.defense_ratio_own; d.army_compact_sq = v.army_compact_sq;
			d.target_num_workers = v.target_num_workers; d.target_num_traders = v.target_num_traders;
			d.pop_phase2 = v.pop_phase2; d.work_phase3 = v.work_phase3; d.muster_delta = v.muster_delta;
			d.garrison_hp_low = v.garrison_hp_low; d.garrison_hp_high = v.garrison_hp_high;
			d.emergency_pop_factor = v.emergency_pop_factor; d.emergency_struct_factor = v.emergency_struct_factor;
			d.emergency_root_factor = v.emergency_root_factor;
			d.prio_emergency = v.prio_emergency; d.prio_villager = v.prio_villager; d.prio_military = v.prio_military;
			d.prio_tech = v.prio_tech; d.prio_trade = v.prio_trade; d.prio_naval = v.prio_naval;
		});
	};
	auto field = [&](auto &v) {
		using T = std::decay_t<decltype(v)>;
		if constexpr (std::is_same_v<T, S::AttackPlan>) {
			ar.pod(v.type); ar.pod(v.state); ar.pod(v.target_building_id);
			ar.pod(v.staging_x); ar.pod(v.staging_z); ar.pod(v.target_x); ar.pod(v.target_z);
			ar.pod(v.abort_threat); ids(v.squad); ar.pod(v.initial_squad_size); ar.pod(v.state_timer);
			ar.pod(v.retreat_base_id); ar.pod(v.retreat_x); ar.pod(v.retreat_z);
		} else if constexpr (std::is_same_v<T, S::PetraStrikeForce>) {
			ar.pod(v.state); ids(v.unit_ids); ar.pod(v.staging_x); ar.pod(v.staging_z);
			ar.pod(v.target_x); ar.pod(v.target_z); ar.pod(v.target_building_id);
			ar.pod(v.target_unit_id); ar.pod(v.state_timer);
		} else if constexpr (std::is_same_v<T, PetraDefenseArmy>) {
			ids(v.defender_ids); ids(v.foe_ids); ar.pod(v.cx); ar.pod(v.cz); ar.pod(v.active);
		} else if constexpr (std::is_same_v<T, PetraTransportPlan>) {
			ar.pod(v.state); ar.pod(v.ship_id); ids(v.passenger_ids);
			ar.pod(v.pickup_x); ar.pod(v.pickup_z); ar.pod(v.drop_x); ar.pod(v.drop_z);
			ar.pod(v.state_timer); ar.pod(v.failed);
		} else if constexpr (std::is_same_v<T, PetraEmergency>) {
			static_assert(sizeof(PetraEmergency) == 16, "list the new PetraEmergency field below");
			ar.padded(v, [](PetraEmergency &d, const PetraEmergency &x) {
				d.ref_pop = x.ref_pop; d.ref_structs = x.ref_structs; d.ref_roots = x.ref_roots; d.active = x.active;
			});
		} else if constexpr (std::is_same_v<T, PetraDiplomacy>) {
			static_assert(sizeof(PetraDiplomacy) == 152 && sizeof(PetraReceivedRequest) == 16,
			              "list the new PetraDiplomacy field below");
			ar.padded(v, [&](PetraDiplomacy &d, const PetraDiplomacy &x) {
				for (int p = 0; p < 8; ++p) {
					const PetraReceivedRequest &r = x.received[p];
					PetraReceivedRequest &o = d.received[p];
					o.status = r.status; o.type = r.type; o.resource = r.resource; o.sent_warning = r.sent_warning;
					o.wanted = r.wanted; o.paid = r.paid; o.warn_time = r.warn_time;
				}
				arr(d.seen_stance, x.seen_stance); d.seen_init = x.seen_init; d.waiting_to_betray = x.waiting_to_betray;
				d.betray_lapse = x.betray_lapse; d.elapsed = x.elapsed; d.next_tribute = x.next_tribute;
			});
		} else if constexpr (std::is_same_v<T, std::vector<S::PetraFoundationWatch>>) {
			io_vec(ar, v, 1u << 16, [&](S::PetraFoundationWatch &w) { ar.pod(w.id); ar.pod(w.progress); ar.pod(w.stall); ids(w.banned); });
		} else if constexpr (std::is_same_v<T, std::vector<S::PetraBase>>) {
			io_vec(ar, v, 1u << 16, base);
		} else if constexpr (std::is_same_v<T, PetraConfig>) {
			if (ar.version >= 21) io_config(v); // v21: one per AI slot
		} else if constexpr (std::is_same_v<T, PetraAttackManager>) {
			if (ar.version >= 18) ar.pod(v);
			else v = PetraAttackManager{};
		} else if constexpr (std::is_same_v<T, PetraSentDiplomacy>) {
			if (ar.version >= 27) ar.pod(v);
			else v = PetraSentDiplomacy{};
		} else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
			io_vec_pod(ar, v, (uint32_t)S::MAX_GRID_RES * S::MAX_GRID_RES);
		} else {
			// Raw: only plain values and structs without padding (check with -Wpadded).
			static_assert(std::is_trivially_copyable_v<T>, "give this Petra field its own case in tick_state_io");
			ar.pod(v);
		}
	};
	// The AI being run between ticks (PETRA_PLAYER) lives in the members, every other AI in its slot.
#define RTSO_PETRA_IO_LIVE(m) field(s.m);
	RTSO_PETRA_SLOT_FIELDS(RTSO_PETRA_IO_LIVE)
#undef RTSO_PETRA_IO_LIVE
	io_vec(ar, s.petra_slots_, MAX_PLAYERS, [&](S::PetraSlot &slot) {
		ar.pod(slot.initialized);
#define RTSO_PETRA_IO_SLOT(m) field(slot.m);
		RTSO_PETRA_SLOT_FIELDS(RTSO_PETRA_IO_SLOT)
#undef RTSO_PETRA_IO_SLOT
	});
	if (ar.version < 21) {
		// Saves before v21 hold one config that every AI shared.
		io_config(s.petra_config_);
		for (S::PetraSlot &slot : s.petra_slots_) slot.petra_config_ = s.petra_config_;
	}
	ar.pod(s.petra_pid_);
	ar.pod(s.petra_tribute_inbox_);
	if (ar.version >= 27) { // AI-to-AI diplomacy and tribute requests in flight
		ar.pod(s.petra_diplo_request_inbox_);
		ar.pod(s.petra_tribute_request_wanted_);
		ar.pod(s.petra_tribute_request_res_);
	} else if constexpr (Ar::loading) {
		std::memset(s.petra_diplo_request_inbox_, 0, sizeof(s.petra_diplo_request_inbox_));
		std::memset(s.petra_tribute_request_wanted_, 0, sizeof(s.petra_tribute_request_wanted_));
		std::memset(s.petra_tribute_request_res_, 0, sizeof(s.petra_tribute_request_res_));
	}
	static_assert(sizeof(PetraChatLine) == 108, "list the new PetraChatLine field below");
	io_vec(ar, s.petra_chat_, 1u << 16, [&](PetraChatLine &c) {
		ar.padded(c, [&](PetraChatLine &d, const PetraChatLine &v) { d.kind = v.kind; d.x = v.x; d.z = v.z; arr(d.text, v.text); });
	});
	// Diplomacy requests by player, in player order.
	std::vector<std::pair<uint8_t, PetraDiplomacyRequest>> reqs(s.petra_diplomacy_requests_.begin(), s.petra_diplomacy_requests_.end());
	std::sort(reqs.begin(), reqs.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
	static_assert(sizeof(PetraDiplomacyRequest) == 20, "list the new PetraDiplomacyRequest field below");
	io_vec(ar, reqs, MAX_PLAYERS, [&](std::pair<uint8_t, PetraDiplomacyRequest> &r) {
		ar.pod(r.first);
		ar.padded(r.second, [](PetraDiplomacyRequest &d, const PetraDiplomacyRequest &v) {
			d.status = v.status; d.request_type = v.request_type; d.warn_time = v.warn_time;
			d.lapse_time = v.lapse_time; d.wanted = v.wanted; d.resource = v.resource;
		});
	});
	if constexpr (Ar::loading) {
		s.petra_diplomacy_requests_.clear();
		for (const auto &r : reqs) s.petra_diplomacy_requests_[r.first] = r.second;
	}

	// 4. v17: each resource's food kind (meat, grain, fish), which gather rates and fishing read.
	if (ar.version >= 17)
		for (size_t r = 0; r < s.resources_.count; ++r) ar.pod(s.resources_.subtype[r]);

	// 4. UnitAI orders (save v20): each unit's order, last attacker and work order,
	// then the order queues by unit id.
	if (ar.version >= SAVE_VERSION_UNIT_ORDERS) {
		for (size_t i = 0; i < U.count; ++i) {
			ar.pod(U.order_kind[i]); ar.pod(U.order_flags[i]); ar.pod(U.order_target[i]);
			ar.pod(U.order_x[i]); ar.pod(U.order_z[i]); ar.pod(U.order_x2[i]); ar.pod(U.order_z2[i]);
			ar.pod(U.attacked_by[i]); ar.pod(U.work_kind[i]); ar.pod(U.work_target[i]);
		}
		std::vector<std::pair<uint32_t, std::vector<UnitOrder>>> queues(s.order_queues_.begin(), s.order_queues_.end());
		std::sort(queues.begin(), queues.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
		io_vec(ar, queues, S::UNIT_CAP, [&](std::pair<uint32_t, std::vector<UnitOrder>> &kv) {
			ar.pod(kv.first);
			io_vec_pod(ar, kv.second, 64);
		});
		if constexpr (Ar::loading) {
			s.order_queues_.clear();
			for (auto &kv : queues) s.order_queues_[kv.first] = std::move(kv.second);
		}
	} else if constexpr (Ar::loading) {
		for (size_t i = 0; i < U.count; ++i) {
			U.order_kind[i] = ORDER_NONE; U.order_flags[i] = 0; U.order_target[i] = 0;
			U.order_x[i] = U.order_x2[i] = U.pos_x[i]; U.order_z[i] = U.order_z2[i] = U.pos_z[i];
			U.attacked_by[i] = 0; U.work_kind[i] = ORDER_NONE; U.work_target[i] = 0;
		}
		s.order_queues_.clear();
	}

	// 5. v22: each player's trading goods shares (Player.js tradingGoods).
	if (ar.version >= 22)
		for (auto &row : s.trading_goods_) for (uint8_t &v : row) ar.pod(v);
	else if constexpr (Ar::loading)
		for (auto &row : s.trading_goods_) std::fill(std::begin(row), std::end(row), (uint8_t)25);

	// 6. Formation controllers (v23).
	if (ar.version >= 23) {
		ar.pod(s.next_formation_id_);
		io_vec(ar, s.formations_, 1u << 16, [&](S::FormationController &f) {
			ar.pod(f.id); ar.pod(f.owner); ar.pod(f.tmpl); ar.pod(f.columnar); ar.pod(f.order);
			ar.pod(f.x); ar.pod(f.z); ar.pod(f.rot); ar.pod(f.offsets_rot); ar.pod(f.goal_x); ar.pod(f.goal_z);
			ar.pod(f.shape_timer); ar.pod(f.pass); ar.pod(f.has_offsets); ar.pod(f.max_rows_used);
			ids(f.members);
			io_vec_pod(ar, f.slots, S::UNIT_CAP);
			path(f.path);
			if constexpr (Ar::loading)
				if (f.tmpl >= FORMATION_COUNT) ar.ok = false;
		});
	} else if constexpr (Ar::loading) {
		s.formations_.clear();
		s.next_formation_id_ = 1;
	}

	// 8. v26: map triggers (Trigger.js and Timer.js state, triggers.h), the water level
	// the rising-water triggers move, and the two Game Setup options they read.
	if (ar.version >= 26) {
		ar.pod(s.terrain_water_height_);
		ar.pod(s.water_level_mode_); ar.pod(s.sea_level_rise_min_);
		TriggerState &t = s.triggers_;
		ar.pod(t.time_ms); ar.pod(t.next_timer_id); ar.pod(t.next_notification_id);
		ar.pod(t.init_game_done); ar.pod(t.difficulty); ar.pod(t.reserved);
		io_vec_pod(ar, t.triggers, 1u << 12);
		io_vec_pod(ar, t.timers, 1u << 16);
		io_vec_pod(ar, t.points, 1u << 12);
		io_vec_pod(ar, t.notifications, 1u << 8);
	}

	// 10. StatisticsTracker (save v30). Older saves start the counters at zero.
	if (ar.version < S::SAVE_VERSION_STATISTICS) return;
	static_assert(sizeof(PlayerStatistics) == 4 * 91 && sizeof(StatisticsSample) == 4 + 8 * sizeof(PlayerStatistics) + 4 * 32,
	              "PlayerStatistics holds 4-byte fields only (saved raw); update the sizes when adding one");
	ar.pod(s.stats_);
	for (PlayerState &ps : s.players_) ar.pod(ps.used);
	io_vec_pod(ar, s.stat_samples_, (uint32_t)S::MAX_STATISTICS_SAMPLES);
	ar.pod(s.stats_explore_res_);
	if constexpr (Ar::loading)
		if (!ar.ok || s.stats_explore_res_ < 0 || s.stats_explore_res_ > S::MAX_GRID_RES) { ar.ok = false; return; }
	for (auto &g : s.stats_explored_) {
		io_vec_pod(ar, g, (uint32_t)(S::MAX_GRID_RES * S::MAX_GRID_RES));
		if (!g.empty() && g.size() != (size_t)s.stats_explore_res_ * (size_t)s.stats_explore_res_) { ar.ok = false; return; }
	}
}

void Simulation::save_tick_state(std::ofstream &out) const {
	TickSaveAr ar{out};
	tick_state_io(ar, const_cast<Simulation &>(*this));
}

bool Simulation::load_tick_state(std::ifstream &in, uint32_t version) {
	TickLoadAr ar{in, version};
	tick_state_io(ar, *this);
	return ar.ok && in.good();
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
	auto blocked_at = [&](int i) {
		const float t = (float)i / (float)steps;
		const float wx = sx + dx * t;
		const float wz = sz + dz * t;
		const float ray_y = sy + (ty - sy) * t;
		return get_terrain_height(wx, wz) > ray_y + 0.35f;
	};
	const int G = grid_res_;
	if (fow_cell_hmax_.size() != (size_t)G * (size_t)G) {
		for (int i = 1; i < steps; ++i)
			if (blocked_at(i)) return true;
		return false;
	}
	// Exact shortcut: a sample can only block when the highest height get_terrain_height() can read
	// in its FoW cell (fow_cell_hmax_) clears the ray by less than FOW_LOS_CLEARANCE, so only those
	// samples are taken. The cell and ray height come from stepping along the ray, which drifts from
	// blocked_at()'s values by far less than the heightmap sample of slack fow_cell_hmax_ allows on
	// each side and the 0.01 margin in FOW_LOS_CLEARANCE.
	const float *hmax = fow_cell_hmax_.data();
	const float to_cell = (float)G / (2.0f * map_half_);
	const float inv_steps = 1.0f / (float)steps;
	const float step_x = dx * inv_steps * to_cell, step_z = dz * inv_steps * to_cell, step_y = (ty - sy) * inv_steps;
	float cell_x = (sx + map_half_) * to_cell, cell_z = (sz + map_half_) * to_cell, ray_y = sy + FOW_LOS_CLEARANCE;
	for (int i = 1; i < steps; ++i) {
		cell_x += step_x;
		cell_z += step_z;
		ray_y += step_y;
		const int gx = std::clamp((int)cell_x, 0, G - 1);
		const int gz = std::clamp((int)cell_z, 0, G - 1);
		if (hmax[(size_t)gz * G + gx] > ray_y && blocked_at(i)) return true;
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
	if (++fow_view_gen_ == 0) ++fow_view_gen_; // 0 marks a FowView never filled
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

void Simulation::reveal_fow(std::vector<uint8_t> &grid, float sx, float sz, float vr, FowView *view) const {
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
	const int GB = (G + FOW_LOS_BLOCK - 1) / FOW_LOS_BLOCK;
	const int bcx = cx / FOW_LOS_BLOCK, bcz = cz / FOW_LOS_BLOCK;
	const int rb = r / FOW_LOS_BLOCK + 1;
	const int WB = 2 * rb + 1;
	// box_max[(block row - bcz + rb) * WB + (block column - bcx + rb)]: the highest fow_block_hmax_
	// between the viewer's block and that block. Filled the first time a cell needs it (in a crowd,
	// many calls find every cell lit already), not zeroed per call.
	float box_max[FOW_BOX_MAX_W * FOW_BOX_MAX_W];
	bool box_ready = false;
	// Every box holds the viewer's own block, so when that block is already higher than the eye
	// allows, no cell can pass and the shortcut is off.
	const bool shortcut = !fow_block_hmax_.empty() && cx >= 0 && cx < G && cz >= 0 && cz < G &&
			WB <= FOW_BOX_MAX_W && fow_block_hmax_[(size_t)bcz * GB + bcx] <= eye_y + FOW_LOS_CLEARANCE;

	// Calls visit(gx, lit) for each cell of row gz within vr, lit: the viewer sees it. With
	// skip_lit, cells already VISIBLE in `row` are skipped.
	constexpr uint64_t VISIBLE_X8 = 0x0101010101010101ull * FOW_VISIBLE;
	auto scan_row = [&](int gz, auto skip_lit, const uint8_t *row, auto &&visit) {
		const float wz = (gz + 0.5f) * cell - half;
		const float ddz = wz - sz;
		const float rem = vr2 - ddz * ddz;
		if (rem < 0.0f) return; // no cell of this row is within vr
		// The viewer is inside cell cx, so a cell k > half-width / cell + 1 columns away is more
		// than half a cell outside the circle and fails the distance test below anyway.
		const int span = (int)(std::sqrt(rem) / cell) + 1;
		const int lo = std::max(gx0, cx - span), hi = std::min(gx1, cx + span);
		const float *hrow = &fow_cell_height_[(size_t)gz * G];
		const int box_row = shortcut ? ((int)((unsigned)gz / FOW_LOS_BLOCK) - bcz + rb) * WB - bcx + rb : 0; // + column / FOW_LOS_BLOCK
		for (int gx = lo; gx <= hi; ++gx) {
			if constexpr (decltype(skip_lit)::value) {
				if (row[gx] == FOW_VISIBLE) {
					// Skip runs of cells other viewers already lit, eight at a time.
					uint64_t next8;
					while (gx + 8 <= hi && (std::memcpy(&next8, row + gx + 1, 8), next8 == VISIBLE_X8)) gx += 8;
					continue;
				}
			}
			const float wx = (gx + 0.5f) * cell - half;
			const float ddx = wx - sx;
			if (ddx * ddx + ddz * ddz > vr2) continue;
			const float ty = hrow[gx] + 0.4f;
			bool clear = false; // no terrain between the viewer and this cell can block the ray
			if (shortcut) {
				if (!box_ready) [[unlikely]] {
					fow_box_max(fow_block_hmax_.data(), GB, bcx, bcz, rb, box_max);
					box_ready = true;
				}
				clear = box_max[box_row + (int)((unsigned)gx / FOW_LOS_BLOCK)] <= std::min(eye_y, ty) + FOW_LOS_CLEARANCE;
			}
			visit(gx, clear || !terrain_blocks_los(sx, sz, eye_y, wx, wz, ty));
		}
	};

	// A viewer that has not moved since its last call lights the same cells, so the second call
	// from the same spot keeps them as bit rows and later ones copy them back.
	if (view) {
		const uint32_t kx = std::bit_cast<uint32_t>(sx), kz = std::bit_cast<uint32_t>(sz);
		const uint32_t kv = std::bit_cast<uint32_t>(vr);
		if (view->gen != fow_view_gen_ || view->sx != kx || view->sz != kz || view->vr != kv) {
			view->sx = kx;
			view->sz = kz;
			view->vr = kv;
			view->gen = fow_view_gen_;
			view->stored = false;
		} else if (r <= FOW_VIEW_MAX_R) { // same spot: the same r
			if (!view->stored) {
				std::fill(view->rows, view->rows + 2 * r + 1, 0ull);
				for (int gz = gz0; gz <= gz1; ++gz) {
					uint64_t &bits = view->rows[gz - cz + r];
					scan_row(gz, std::false_type{}, nullptr, [&](int gx, bool lit) {
						if (lit) bits |= 1ull << (gx - cx + r);
					});
				}
				view->stored = true;
			}
			const int x0 = cx - r, chunks = (2 * r + 1 + 7) / 8;
			if (x0 >= 0 && x0 + chunks * 8 <= G) {
				// Eight cells at a time: FOW_VISIBLE in the bytes whose bits are set, the rest kept.
				for (int gz = gz0; gz <= gz1; ++gz) {
					uint8_t *row = &grid[(size_t)gz * G + x0];
					const uint64_t bits = view->rows[gz - cz + r];
					for (int k = 0; k < chunks && bits >> (8 * k); ++k) {
						const uint64_t mask = kFowByteMask[(bits >> (8 * k)) & 0xFF];
						if (!mask) continue;
						uint64_t cells;
						std::memcpy(&cells, row + 8 * k, 8);
						cells = (cells & ~mask) | (VISIBLE_X8 & mask);
						std::memcpy(row + 8 * k, &cells, 8);
					}
				}
			} else {
				for (int gz = gz0; gz <= gz1; ++gz) {
					uint8_t *row = &grid[(size_t)gz * G];
					for (uint64_t bits = view->rows[gz - cz + r]; bits; bits &= bits - 1)
						row[x0 + std::countr_zero(bits)] = FOW_VISIBLE;
				}
			}
			return;
		}
	}

	for (int gz = gz0; gz <= gz1; ++gz) {
		uint8_t *row = &grid[(size_t)gz * G];
		scan_row(gz, std::true_type{}, row, [&](int gx, bool lit) {
			if (lit) row[gx] = FOW_VISIBLE;
		});
	}
}

void Simulation::tick_fow() {
	ScopedUs prof(map_scale_stats_.fow_us, map_scale_stats_.fow_calls);
	// 0 A.D. "Revealed map": everyone sees everything, so there is no per-cell work.
	// The grids are filled once (a game-setup option, so every peer hashes the same).
	if (reveal_map_) {
		if (!fow_grid_.empty() && fow_grid_[0] != FOW_VISIBLE) std::fill(fow_grid_.begin(), fow_grid_.end(), (uint8_t)FOW_VISIBLE);
		const uint8_t v = fow_viewer_;
		if (v != 0 && v < MAX_PLAYERS && (player_fow_[v].size() != fow_grid_.size() || player_fow_[v][0] != FOW_VISIBLE))
			player_fow_[v].assign(fow_grid_.size(), FOW_VISIBLE);
		return;
	}
	ensure_fow_cell_heights();

	// VisionSharing.js: allied players share the visibility generated by
	// their units/buildings -- teammates, or a mutual alliance both sides chose.
	// fow_grid_ is player 0's view and the one hashed for lockstep.
	update_player_fow(fow_grid_, [this](uint8_t owner) { return is_ally(0, owner); }, player_fow_views_);

	// The local viewer's own view when it is not player 0 (a multiplayer
	// joiner). It is presentation-only state: never hashed or read by the
	// simulation, so peers computing different viewers stay in sync.
	const uint8_t p = fow_viewer_;
	if (p != 0 && p < MAX_PLAYERS) {
		auto &grid = player_fow_[p];
		if (grid.size() != fow_grid_.size()) grid.assign(fow_grid_.size(), FOW_UNEXPLORED);
		update_player_fow(grid, [this, p](uint8_t owner) { return is_ally(p, owner); }, player_fow_views_);
	}
}

template <typename SharesVision>
void Simulation::update_player_fow(std::vector<uint8_t> &grid, SharesVision shares_vision, FowViews &views) {
	// Step 1: Reset all VISIBLE tiles back to EXPLORED (preserve explored history).
	// 0 A.D. "Explored map": nothing is ever unexplored (terrain known, units hidden).
	const uint8_t floor = explore_map_ ? FOW_EXPLORED : FOW_UNEXPLORED;
	for (auto &c : grid) c = c == FOW_VISIBLE ? (uint8_t)FOW_EXPLORED : std::max(c, floor);

	// Steps 2 and 3: every unit and finished building whose vision the player shares lights the
	// tiles within its vision radius (unit_vision_range, 0 A.D. Vision/Range).

	reveal_viewers(grid, views,
			[&](size_t u) { return units_.alive[u] && shares_vision(units_.team[u]) ? unit_vision_range(units_.unit_type[u]) : 0.0f; },
			[&](const Building &b) {
				if (!shares_vision(b.team) || b.build_progress < 1.0f) return 0.0f;
				float bvr = 18.0f; // default building vision
				if (b.type == BLD_TOWER || b.type == BLD_FORTRESS) bvr = 24.0f;
				if (b.type == BLD_DOCK) bvr = 28.0f;
				if (const float cv = civ_building_vision(b.type); cv > 0.0f) bvr = cv; // e.g. the Ptolemaic Lighthouse
				return bvr;
			});
}

template <typename UnitVision, typename BuildingVision>
void Simulation::reveal_viewers(std::vector<uint8_t> &grid, FowViews &views, UnitVision unit_vr, BuildingVision building_vr) {
	// Calls reveal_fow() for each building and unit with a vision radius above 0. The result is the
	// union of their views whatever the order, so buildings and units that have not moved go first
	// (copies of their stored views), and the units that moved last: their LOS rays then skip
	// every cell the others lit.
	if (views.buildings.size() < buildings_.size()) views.buildings.resize(buildings_.size());
	for (size_t bi = 0; bi < buildings_.size(); ++bi) {
		const auto &b = buildings_[bi];
		const float vr = building_vr(b);
		if (vr > 0.0f) reveal_fow(grid, b.x, b.z, vr, &views.buildings[bi]);
	}
	if (views.units.size() < units_.count) views.units.resize(units_.count);
	fow_moved_units_.clear();
	for (size_t u = 0; u < units_.count; ++u) {
		const float vr = unit_vr(u);
		if (vr <= 0.0f) continue;
		if (views.units[u].same_spot(units_.pos_x[u], units_.pos_z[u], vr, fow_view_gen_))
			reveal_fow(grid, units_.pos_x[u], units_.pos_z[u], vr, &views.units[u]);
		else
			fow_moved_units_.push_back((uint32_t)u);
	}
	for (const uint32_t u : fow_moved_units_)
		reveal_fow(grid, units_.pos_x[u], units_.pos_z[u], unit_vr(u), &views.units[u]);
}

Simulation::FaunaView Simulation::fauna() const {
	return {fauna_.count, fauna_.id, fauna_.pos_x, fauna_.pos_z,
	        fauna_.vel_x, fauna_.vel_z, fauna_.rot_y,
	        fauna_.hp, fauna_.max_hp, fauna_.food_amount,
	        fauna_.fauna_type, fauna_.behavior, fauna_.alive};
}

uint32_t Simulation::spawn_fauna(float x, float z, uint8_t ftype) {
	if (fauna_.count >= FaunaStorage::MAX_FAUNA || ftype >= FAUNA_COUNT) return 0;
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
	// UnitAI IDLE: "Go linger first to prevent all roaming entities to move all at the same time on map init."
	fauna_.state_timer[idx] = info.feed_time_min + (float)sim_rng_.range_int(0, (int)std::max(0.0f, info.feed_time_max - info.feed_time_min));
	fauna_.attack_timer[idx] = 0.0f;
	fauna_.target_unit_id[idx] = 0;
	fauna_.corral_id[idx] = 0;
	fauna_.fatten_timer[idx] = 0.0f;
	fauna_.home_x[idx] = fauna_.pos_x[idx];
	fauna_.home_z[idx] = fauna_.pos_z[idx];
	fauna_id_to_idx_[fid] = idx;
	return fid;
}

// 0 A.D. UnitAI Gather order on a KillBeforeGather supply (an animal): the unit attacks it,
// then gathers the carcass (tick_hunters). Any unit with an attack can hunt.
void Simulation::order_hunt(const uint32_t *unit_ids, size_t count, uint32_t fauna_id) {
	auto it = fauna_id_to_idx_.find(fauna_id);
	if (it == fauna_id_to_idx_.end()) return;
	size_t fidx = it->second;
	if (!fauna_.alive[fidx]) return;

	for (size_t k = 0; k < count; ++k) {
		int32_t uidx = lookup_unit(unit_ids[k]);
		if (uidx < 0 || !units_.alive[uidx] || units_.attack_damage[uidx] <= 0.0f) continue;
		if (units_.action_state[uidx] == ACTION_GARRISONED) continue;
		units_.action_state[uidx] = ACTION_ATTACK;
		units_.has_order[uidx] = 1;
		drop_orders(uidx);
		units_.target_unit_id[uidx] = fauna_id;
		units_.target_building_id[uidx] = 0;
		units_.target_resource_id[uidx] = 0;
		units_.target_x[uidx] = fauna_.pos_x[fidx];
		units_.target_z[uidx] = fauna_.pos_z[fidx];
	}
}

// Animals walk on land (or swim, for SeaCreatures) and stay inside the map.
bool Simulation::fauna_can_step(size_t f, float x, float z) const {
	if (x < -map_half_ + 2.0f || x > map_half_ - 2.0f || z < -map_half_ + 2.0f || z > map_half_ - 2.0f) return false;
	if (nav_grid_.res <= 0) return true;
	auto [cx, cy] = nav_grid_.world_to_cell(x, z);
	const bool naval = fauna_type_info(fauna_.fauna_type[f]).is_naval();
	return nav_grid_.is_passable_for(cx, cy, naval ? PASS_SHIP : PASS_INFANTRY);
}

// UnitAI "Attacked" for an animal: passive and skittish animals flee (respondFlee),
// the rest fight back (respondHoldGround / respondChase).
void Simulation::fauna_attacked(size_t f, uint32_t attacker_id) {
	if (!fauna_.alive[f]) return;
	const uint8_t b = fauna_.behavior[f];
	if (b == FAUNA_CORRALLED || b == FAUNA_HERDED) return; // livestock stays put while slaughtered
	const auto &info = fauna_type_info(fauna_.fauna_type[f]);
	if (info.attack_damage() > 0.0f && info.stance != FAUNA_STANCE_PASSIVE && info.stance != FAUNA_STANCE_SKITTISH) {
		if (b != FAUNA_ATTACK) {
			fauna_.behavior[f] = FAUNA_ATTACK;
			fauna_.target_unit_id[f] = attacker_id;
			fauna_.home_x[f] = fauna_.pos_x[f];
			fauna_.home_z[f] = fauna_.pos_z[f];
		}
		return;
	}
	fauna_.behavior[f] = FAUNA_FLEE;
	fauna_.target_unit_id[f] = attacker_id;
	fauna_.state_timer[f] = 8.0f; // give up a flee that can't open the distance
}

// An animal's Attack/Melee lands on a unit: 0 A.D. Attack.js damage, sum(Dmg * 0.9^Armor).
void Simulation::fauna_hit_unit(size_t f, size_t u) {
	const auto &info = fauna_type_info(fauna_.fauna_type[f]);
	const auto &uinfo = unit_info_at(u);
	const float ra = rank_melee_armor(u); // unit_advanced.json / unit_elite.json
	float hack_a = uinfo.hack_armor + ra, pierce_a = uinfo.pierce_armor + ra, crush_a = uinfo.crush_armor + ra;
	if (units_.team[u] < MAX_PLAYERS && units_.unit_type[u] < UNIT_TYPE_COUNT) {
		const float *armor = tech_mods(units_.team[u]).armor[units_.unit_type[u]];
		hack_a += armor[0];
		pierce_a += armor[1];
		crush_a += armor[2];
	}
	const float dmg = info.hack_damage * powf(0.9f, hack_a) + info.pierce_damage * powf(0.9f, pierce_a) +
	                  info.crush_damage * powf(0.9f, crush_a);
	units_.hp[u] -= std::max(1.0f, dmg);
	if (units_.hp[u] <= 0.0f) {
		units_.alive[u] = 0;
		units_.action_state[u] = ACTION_DYING;
		record_unit_death(u, GAIA_PLAYER);
		stat_unit_lost(u, 0xFF);
		if (units_.team[u] == 0) record_.units_lost++;
		return;
	}
	// The victim's UnitAI "Attacked": soldiers turn on the animal; women and other passive units keep going.
	if (units_.attack_damage[u] > 0.0f && units_.stance[u] != STANCE_PASSIVE &&
	    (units_.action_state[u] == ACTION_IDLE || (units_.action_state[u] == ACTION_ATTACK && units_.target_unit_id[u] == 0))) {
		units_.action_state[u] = ACTION_ATTACK;
		units_.has_order[u] = 1;
		units_.target_unit_id[u] = fauna_.id[f];
		units_.target_x[u] = fauna_.pos_x[f];
		units_.target_z[u] = fauna_.pos_z[f];
	}
}

// Units whose attack target is an animal (order_hunt, or fighting back): chase it, strike it with
// their own damage against its armour, and gather the carcass once it falls (ResourceGatherer).
void Simulation::tick_hunters() {
	if (fauna_.count == 0) return;
	const size_t n = units_.count;
	for (size_t i = 0; i < n; ++i) {
		const uint32_t tid = units_.target_unit_id[i];
		if (tid == 0 || !units_.alive[i] || lookup_unit(tid) >= 0) continue;
		const uint8_t st = units_.action_state[i];
		if (st != ACTION_ATTACK && st != ACTION_MOVE) continue;
		auto it = fauna_id_to_idx_.find(tid);
		if (it == fauna_id_to_idx_.end()) continue;
		const size_t f = it->second;
		if (st == ACTION_MOVE) continue; // herding: order_herd walks the herder over
		if (!fauna_.alive[f] || fauna_.hp[f] <= 0.0f) {
			units_.target_unit_id[i] = 0;
			units_.action_state[i] = ACTION_IDLE;
			units_.has_order[i] = 0;
			continue;
		}
		const float fx = fauna_.pos_x[f], fz = fauna_.pos_z[f];
		units_.target_x[i] = fx;
		units_.target_z[i] = fz;
		units_.has_order[i] = 1;
		if (units_.attack_timer[i] > 0.0f || !pack_state_can_attack(units_.pack_state[i])) continue;
		const auto &finfo = fauna_type_info(fauna_.fauna_type[f]);
		const uint8_t ut = units_.unit_type[i];
		// Attack/Slaughter (infantry, cavalry, women): 1000 hack at 2 m, every second, Domestic animals only.
		const bool slaughter = finfo.is_domestic() && !unit_is_ship(ut) && !unit_is_siege_engine(ut);
		const float dx = units_.pos_x[i] - fx, dz = units_.pos_z[i] - fz;
		// The animal's footprint; a penned animal is reached over the corral fence.
		float reach = (slaughter ? std::max(units_.attack_range[i], 2.0f * 0.6f) : units_.attack_range[i]) + 1.0f;
		if (fauna_.behavior[f] == FAUNA_CORRALLED) reach += 3.0f;
		if (dx * dx + dz * dz > reach * reach) continue;

		const auto &uinfo = unit_info_at(i);
		float dmg;
		if (slaughter) {
			units_.attack_timer[i] = 1.0f;
			dmg = 1000.0f * powf(0.9f, finfo.hack_armor);
		} else {
			units_.attack_timer[i] = units_.attack_cooldown[i];
			const float scale = uinfo.attack_damage > 0.0f ? units_.attack_damage[i] / uinfo.attack_damage : 1.0f;
			dmg = uinfo.hack_damage * scale * powf(0.9f, finfo.hack_armor) +
			      uinfo.pierce_damage * scale * powf(0.9f, finfo.pierce_armor) +
			      uinfo.crush_damage * scale * powf(0.9f, finfo.crush_armor);
			if (uinfo.hack_damage + uinfo.pierce_damage + uinfo.crush_damage <= 0.0f) dmg = units_.attack_damage[i];
		}
		fauna_.hp[f] -= std::max(1.0f, dmg);
		fauna_attacked(f, units_.id[i]);
		if (fauna_.hp[f] > 0.0f) continue;

		// The kill: Looter.js grants the animal's Loot xp; the carcass becomes a meat supply.
		units_.xp[i] = (uint16_t)std::min(65535.0f, (float)units_.xp[i] + finfo.loot_xp);
		fauna_.alive[f] = 0;
		fauna_.behavior[f] = FAUNA_CARCASS;
		uint32_t rid = 0;
		if (fauna_.food_amount[f] > 0)
			rid = spawn_resource_world(fx, fz, RES_FOOD, fauna_.food_amount[f], RES_SUB_MEAT);
		for (size_t h = 0; h < n; ++h) {
			if (units_.target_unit_id[h] != tid || !units_.alive[h]) continue;
			units_.target_unit_id[h] = 0;
			if (rid != 0 && carry_cap(h, RES_FOOD) > 0 && !unit_is_ship(units_.unit_type[h])) {
				order_gather(&units_.id[h], 1, rid);
			} else {
				units_.action_state[h] = ACTION_IDLE;
				units_.has_order[h] = 0;
			}
		}
	}
}

void Simulation::tick_fauna(float dt) {
	tick_hunters();
	if (fauna_.count == 0) return;

	// Animals react to player units (Gaia is every player's enemy); Gaia's own units are their side.
	UnitGrid grid;
	Arena &arena = frame_arena_.current();
	const bool have_grid = units_.count > 0 && grid.build(arena, units_.count, units_.pos_x, units_.pos_z, units_.alive, map_half_);
	auto foe = [&](int32_t u) {
		return units_.alive[u] && units_.team[u] < MAX_PLAYERS && units_.action_state[u] != ACTION_GARRISONED;
	};
	// Attack/Melee RestrictedClasses "Structure Ship Siege".
	auto can_bite = [&](int32_t u) {
		return foe(u) && !unit_is_ship(units_.unit_type[u]) && !unit_is_siege_engine(units_.unit_type[u]);
	};
	auto nearest_foe = [&](float x, float z, float range, bool biteable) -> int32_t {
		int32_t best = -1;
		float best_d2 = range * range;
		if (!have_grid || range <= 0.0f) return -1;
		grid.visit_radius(x, z, range, [&](int32_t u) {
			if (biteable ? !can_bite(u) : !foe(u)) return;
			const float dx = units_.pos_x[u] - x, dz = units_.pos_z[u] - z;
			const float d2 = dx * dx + dz * dz;
			if (d2 < best_d2 || (d2 == best_d2 && best >= 0 && units_.id[u] < units_.id[best])) {
				best_d2 = d2;
				best = u;
			}
		});
		return best;
	};
	auto step = [&](size_t i, float vx, float vz) {
		const float nx = fauna_.pos_x[i] + vx * dt, nz = fauna_.pos_z[i] + vz * dt;
		if (fauna_can_step(i, nx, nz)) {
			fauna_.pos_x[i] = nx;
			fauna_.pos_z[i] = nz;
		} else {
			fauna_.vel_x[i] = fauna_.vel_z[i] = 0.0f;
		}
	};
	auto timer = [&](float lo, float hi) {
		return lo + (float)sim_rng_.range_int(0, (int)std::max(0.0f, hi - lo));
	};

	bool any_dead = false;
	for (size_t i = 0; i < fauna_.count; ++i) {
		if (!fauna_.alive[i]) { any_dead = true; continue; }
		const auto &info = fauna_type_info(fauna_.fauna_type[i]);

		// Killed by something other than a hunter (splash, a status effect): leave the carcass.
		if (fauna_.hp[i] <= 0.0f) {
			if (fauna_.food_amount[i] > 0)
				spawn_resource_world(fauna_.pos_x[i], fauna_.pos_z[i], RES_FOOD, fauna_.food_amount[i], RES_SUB_MEAT);
			fauna_.behavior[i] = FAUNA_CARCASS;
			fauna_.alive[i] = 0;
			any_dead = true;
			continue;
		}

		fauna_.state_timer[i] -= dt;
		if (fauna_.attack_timer[i] > 0.0f) fauna_.attack_timer[i] -= dt;

		float fx = fauna_.pos_x[i];
		float fz = fauna_.pos_z[i];

		// 1. Corralled livestock wanders inside its corral; tick_trickle pays its food trickle.
		if (fauna_.behavior[i] == FAUNA_CORRALLED) {
			auto bit = building_id_to_idx_.find(fauna_.corral_id[i]);
			if (bit == building_id_to_idx_.end() || buildings_[bit->second].hp <= 0.0f) {
				fauna_.behavior[i] = FAUNA_IDLE_GRAZE;
				fauna_.corral_id[i] = 0;
			} else {
				const auto &corral = buildings_[bit->second];

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

		// 2. Herded livestock follows its herder to the herder's own nearest corral (or town centre).
		if (fauna_.behavior[i] == FAUNA_HERDED) {
			const int32_t herder_idx = lookup_unit(fauna_.target_unit_id[i]);
			if (herder_idx < 0 || !units_.alive[herder_idx] || units_.team[herder_idx] >= MAX_PLAYERS) {
				fauna_.behavior[i] = FAUNA_IDLE_GRAZE;
				fauna_.target_unit_id[i] = 0;
				fauna_.state_timer[i] = timer(info.feed_time_min, info.feed_time_max);
				continue;
			}
			const uint8_t owner = units_.team[herder_idx];
			float nearest_corral_d2 = 1e18f;
			float target_cx = 0.0f, target_cz = 0.0f;
			uint32_t nearest_cid = 0;

			for (const auto &b : buildings_) {
				if (b.team != owner || b.build_progress < 1.0f || b.hp <= 0.0f) continue;
				if (b.type == BLD_CORRAL && corral_count(b.id) >= CORRAL_GARRISON_MAX) continue; // GarrisonHolder/Max
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
			}

			step(i, fauna_.vel_x[i], fauna_.vel_z[i]);
			continue;
		}

		// 3. Combat: an animal fights back or hunts per its stance (UnitAI g_Stances).
		if (fauna_.behavior[i] == FAUNA_ATTACK) {
			const int32_t t = lookup_unit(fauna_.target_unit_id[i]);
			bool keep = t >= 0 && can_bite(t);
			if (keep) {
				const float tdx = units_.pos_x[t] - fx, tdz = units_.pos_z[t] - fz;
				const float td2 = tdx * tdx + tdz * tdz;
				if (info.stance == FAUNA_STANCE_AGGRESSIVE) {
					keep = td2 <= info.vision * info.vision; // respondChase, not beyond vision
				} else if (info.stance != FAUNA_STANCE_VIOLENT) {
					// respondHoldGround: fight near the held position, back home when drawn > 10 m away.
					const float hdx = fx - fauna_.home_x[i], hdz = fz - fauna_.home_z[i];
					const float leash = 10.0f * FAUNA_DISTANCE_SCALE + info.vision;
					keep = hdx * hdx + hdz * hdz <= leash * leash;
				}
			}
			if (!keep) {
				fauna_.behavior[i] = FAUNA_ROAMING;
				fauna_.target_unit_id[i] = 0;
				const float hdx = fauna_.home_x[i] - fx, hdz = fauna_.home_z[i] - fz;
				const float hl = std::hypot(hdx, hdz);
				fauna_.vel_x[i] = hl > 0.5f ? hdx / hl * info.speed : 0.0f;
				fauna_.vel_z[i] = hl > 0.5f ? hdz / hl * info.speed : 0.0f;
				fauna_.state_timer[i] = info.speed > 0.0f ? std::min(10.0f, hl / info.speed) : 0.0f;
				continue;
			}
			const float dx = units_.pos_x[t] - fx, dz = units_.pos_z[t] - fz;
			const float len = std::sqrt(dx * dx + dz * dz);
			if (len > info.attack_range + 0.6f) {
				const float sp = info.speed * info.run_mult;
				fauna_.vel_x[i] = dx / len * sp;
				fauna_.vel_z[i] = dz / len * sp;
				fauna_.rot_y[i] = std::atan2(dx, dz);
				step(i, fauna_.vel_x[i], fauna_.vel_z[i]);
			} else {
				fauna_.vel_x[i] = fauna_.vel_z[i] = 0.0f;
				if (fauna_.attack_timer[i] <= 0.0f) {
					fauna_.attack_timer[i] = info.attack_cooldown > 0.0f ? info.attack_cooldown : 1.0f;
					fauna_hit_unit(i, (size_t)t);
				}
			}
			continue;
		}

		// 4. Fleeing (INDIVIDUAL.FLEEING): run from the attacker until FleeDistance away.
		if (fauna_.behavior[i] == FAUNA_FLEE) {
			const int32_t t = lookup_unit(fauna_.target_unit_id[i]);
			float away_x = 0.0f, away_z = 0.0f, dist = 1e9f;
			if (t >= 0 && units_.alive[t]) {
				away_x = fx - units_.pos_x[t];
				away_z = fz - units_.pos_z[t];
				dist = std::sqrt(away_x * away_x + away_z * away_z);
			}
			if (dist >= info.flee_distance || fauna_.state_timer[i] <= 0.0f) {
				fauna_.behavior[i] = FAUNA_IDLE_GRAZE;
				fauna_.target_unit_id[i] = 0;
				fauna_.vel_x[i] = fauna_.vel_z[i] = 0.0f;
				fauna_.state_timer[i] = timer(info.feed_time_min, info.feed_time_max);
				continue;
			}
			if (dist < 0.01f) { away_x = std::sin(fauna_.rot_y[i]); away_z = std::cos(fauna_.rot_y[i]); dist = 1.0f; }
			const float sp = info.speed * info.run_mult;
			fauna_.vel_x[i] = away_x / dist * sp;
			fauna_.vel_z[i] = away_z / dist * sp;
			fauna_.rot_y[i] = std::atan2(away_x, away_z);
			const float before_x = fauna_.pos_x[i], before_z = fauna_.pos_z[i];
			step(i, fauna_.vel_x[i], fauna_.vel_z[i]);
			if (fauna_.pos_x[i] == before_x && fauna_.pos_z[i] == before_z) {
				// Cornered (water, cliff or map edge): slide sideways.
				const float sx = -away_z / dist * sp, sz = away_x / dist * sp;
				step(i, sx, sz);
			}
			continue;
		}

		// 5. Idle (UnitAI ANIMAL IDLE): react to units in sight per stance, otherwise linger and roam.
		if ((i + ticks_) % 4 == 0) {
			if (info.stance == FAUNA_STANCE_SKITTISH) {
				// respondFleeOnSight inside Vision/Range.
				const int32_t u = nearest_foe(fx, fz, info.vision, false);
				if (u >= 0) { fauna_attacked(i, units_.id[u]); continue; }
			} else if (info.attack_damage() > 0.0f &&
			           (info.stance == FAUNA_STANCE_AGGRESSIVE || info.stance == FAUNA_STANCE_VIOLENT ||
			            info.stance == FAUNA_STANCE_DEFENSIVE)) {
				// GetQueryRange(IID_Attack): vision when chasing, min(range + vision / 2, vision) when holding ground.
				const float q = info.stance == FAUNA_STANCE_DEFENSIVE ? std::min(info.attack_range + info.vision * 0.5f, info.vision) : info.vision;
				const int32_t u = nearest_foe(fx, fz, q, true);
				if (u >= 0) {
					fauna_.behavior[i] = FAUNA_ATTACK;
					fauna_.target_unit_id[i] = units_.id[u];
					fauna_.home_x[i] = fx;
					fauna_.home_z[i] = fz;
					continue;
				}
			}
		}

		if (fauna_.state_timer[i] <= 0.0f) {
			if (fauna_.behavior[i] == FAUNA_ROAMING) {
				// ROAMING Timer -> LINGERING (feeding).
				fauna_.behavior[i] = FAUNA_IDLE_GRAZE;
				fauna_.vel_x[i] = fauna_.vel_z[i] = 0.0f;
				fauna_.state_timer[i] = timer(info.feed_time_min, info.feed_time_max);
			} else {
				// LINGERING Timer -> ROAMING: MoveRandomly(RoamDistance) for RoamTime.
				fauna_.behavior[i] = FAUNA_ROAMING;
				const float angle = (float)sim_rng_.range_int(0, 359) * 0.0174533f;
				const float roam_t = timer(info.roam_time_min, info.roam_time_max);
				const float sp = info.speed;
				fauna_.vel_x[i] = std::sin(angle) * sp;
				fauna_.vel_z[i] = std::cos(angle) * sp;
				fauna_.rot_y[i] = angle;
				fauna_.state_timer[i] = sp > 0.0f ? std::min(roam_t, info.roam_distance / sp) : roam_t;
			}
		}
		if (fauna_.behavior[i] == FAUNA_ROAMING) step(i, fauna_.vel_x[i], fauna_.vel_z[i]);
	}

	// Compact dead animals out (their carcasses live on as resources); keeps spawn order.
	if (any_dead) {
		size_t w = 0;
		for (size_t r = 0; r < fauna_.count; ++r) {
			if (!fauna_.alive[r]) continue;
			if (w != r) {
				fauna_.id[w] = fauna_.id[r]; fauna_.pos_x[w] = fauna_.pos_x[r]; fauna_.pos_z[w] = fauna_.pos_z[r];
				fauna_.vel_x[w] = fauna_.vel_x[r]; fauna_.vel_z[w] = fauna_.vel_z[r]; fauna_.rot_y[w] = fauna_.rot_y[r];
				fauna_.hp[w] = fauna_.hp[r]; fauna_.max_hp[w] = fauna_.max_hp[r]; fauna_.food_amount[w] = fauna_.food_amount[r];
				fauna_.fauna_type[w] = fauna_.fauna_type[r]; fauna_.behavior[w] = fauna_.behavior[r]; fauna_.alive[w] = 1;
				fauna_.state_timer[w] = fauna_.state_timer[r]; fauna_.attack_timer[w] = fauna_.attack_timer[r];
				fauna_.target_unit_id[w] = fauna_.target_unit_id[r]; fauna_.corral_id[w] = fauna_.corral_id[r];
				fauna_.fatten_timer[w] = fauna_.fatten_timer[r]; fauna_.home_x[w] = fauna_.home_x[r]; fauna_.home_z[w] = fauna_.home_z[r];
			}
			++w;
		}
		for (size_t r = w; r < fauna_.count; ++r) fauna_.alive[r] = 0;
		fauna_.count = w;
		fauna_id_to_idx_.clear();
		for (size_t f = 0; f < fauna_.count; ++f) fauna_id_to_idx_[fauna_.id[f]] = f;
	}
}

uint32_t Simulation::spawn_treasure(float x, float z, uint8_t type) {
	if (type >= TREASURE_TYPE_COUNT) return 0;
	Treasure t{};
	t.id = alloc_id();
	t.x = std::clamp(x, -map_half_ + 2.0f, map_half_ - 2.0f);
	t.z = std::clamp(z, -map_half_ + 2.0f, map_half_ - 2.0f);
	t.type = type;
	treasures_.push_back(t);
	return t.id;
}

uint32_t Simulation::spawn_scenery(float x, float z, uint8_t scenery_type, float angle, float scale) {
	if (scenery_type >= SCENERY_TYPE_COUNT) return 0;
	const SceneryTypeInfo &info = scenery_type_info(scenery_type);
	Scenery sc{};
	sc.id = next_id_++;
	sc.x = x;
	sc.z = z;
	sc.angle = angle;
	sc.half_w = 0.5f * info.width * scale;
	sc.half_d = 0.5f * info.depth * scale;
	sc.hp = info.max_hp;
	sc.type = scenery_type;
	scenery_.push_back(sc);
	if (!bulk_spawn_) rebuild_nav_grid();
	return sc.id;
}

uint32_t Simulation::spawn_ruins(float x, float z, uint8_t ruins_type) {
	const RuinsTypeInfo &r = ruins_type_info(ruins_type);
	const uint32_t id = spawn_resource_world(x, z, r.res_type, r.amount, RES_SUB_RUINS);
	if (id) ruins_caps_.push_back({id, (uint16_t)std::max<int>(1, r.max_gatherers), 0});
	return id;
}

// UnitAI Order.CollectTreasure: walk to the treasure. Ships, war dogs and animals have no
// TreasureCollector (template_unit_ship.xml, template_unit_dog.xml disable it).
void Simulation::order_collect_treasure(const uint32_t *unit_ids, size_t count, uint32_t treasure_id, bool queued) {
	const Treasure *t = nullptr;
	for (const auto &tr : treasures_) if (tr.id == treasure_id) t = &tr;
	if (!t) return;
	std::vector<uint32_t> collectors;
	for (size_t k = 0; k < count; ++k) {
		int32_t u = lookup_unit(unit_ids[k]);
		if (u < 0 || !units_.alive[u] || units_.team[u] >= MAX_PLAYERS) continue;
		const uint8_t ut = units_.unit_type[u];
		if (unit_is_ship(ut) || ut == UNIT_WAR_DOG || units_.action_state[u] == ACTION_GARRISONED) continue;
		collectors.push_back(unit_ids[k]);
	}
	UnitOrder o; o.kind = ORDER_COLLECT_TREASURE; o.target = treasure_id; o.x = t->x; o.z = t->z;
	for (uint32_t uid : split_queued(collectors.data(), collectors.size(), o, queued)) {
		const int32_t u = lookup_unit(uid);
		if (u < 0) continue;
		set_order((size_t)u, ORDER_COLLECT_TREASURE, 0, treasure_id, t->x, t->z);
		units_.action_state[u] = ACTION_MOVE;
		units_.has_order[u] = 1;
		units_.target_unit_id[u] = treasure_id;
		units_.target_building_id[u] = 0;
		units_.target_resource_id[u] = 0;
		units_.target_x[u] = t->x;
		units_.target_z[u] = t->z;
		units_.waypoint_cur[u] = units_.waypoint_count[u] = 0;
	}
}

// TreasureCollector.js: a collector within MaxDistance (2 m, plus the treasure's footprint) takes
// CollectTime to collect; Treasure.Reward gives its owner the resources and the treasure is gone.
void Simulation::tick_treasures(float dt) {
	if (treasures_.empty()) return;
	const float reach = 2.0f * 0.6f + 1.0f;
	const size_t n = units_.count;
	std::vector<uint8_t> in_range(treasures_.size(), 0);
	struct NextTreasure { uint32_t uid; float x, z; };
	std::vector<NextTreasure> next_hunt; // collectors whose treasure is gone
	for (size_t i = 0; i < n; ++i) {
		const uint32_t tid = units_.target_unit_id[i];
		if (tid == 0 || !units_.alive[i] || lookup_unit(tid) >= 0) continue;
		const uint8_t st = units_.action_state[i];
		if (st != ACTION_MOVE && st != ACTION_IDLE) continue;
		for (size_t k = 0; k < treasures_.size(); ++k) {
			Treasure &t = treasures_[k];
			if (t.id != tid) continue;
			const float dx = units_.pos_x[i] - t.x, dz = units_.pos_z[i] - t.z;
			if (dx * dx + dz * dz > reach * reach) break;
			// One collector at a time; another in range takes over if it stops.
			if (t.collector_id == 0 || lookup_unit(t.collector_id) < 0 || !units_.alive[lookup_unit(t.collector_id)] ||
			    units_.target_unit_id[lookup_unit(t.collector_id)] != t.id) {
				t.collector_id = units_.id[i];
				t.collect_progress = 0.0f;
			}
			if (t.collector_id == units_.id[i] && !in_range[k]) {
				in_range[k] = 1;
				t.collect_progress += dt;
			}
			break;
		}
	}
	for (size_t k = 0; k < treasures_.size();) {
		Treasure &t = treasures_[k];
		if (!in_range[k]) {
			t.collect_progress = 0.0f;
			t.collector_id = 0;
		}
		const TreasureTypeInfo &info = treasure_type_info(t.type);
		const int32_t c = t.collector_id ? lookup_unit(t.collector_id) : -1;
		if (c < 0 || t.collect_progress + 1e-4f < info.collect_time) { ++k; continue; }
		const uint8_t owner = units_.team[c];
		next_hunt.push_back({units_.id[c], t.x, t.z});
		if (owner < MAX_PLAYERS) {
			static const uint8_t kRes[4] = {RES_FOOD, RES_WOOD, RES_STONE, RES_GOLD};
			PlayerState &ps = player(owner);
			for (int r = 0; r < 4; ++r) ps.res[kRes[r]] += (float)info.amount[r];
			// TreasureCollector.js: a treasure counts as collected and as gathered.
			stats_[owner].treasures_collected++;
			for (int r = 0; r < 4; ++r) stat_gathered(owner, kRes[r], (int)info.amount[r], false);
			raise_alert(BattleAlert::ALERT_RELIC_CLAIMED, t.x, t.z, t.id, owner);
		}
		treasures_.erase(treasures_.begin() + (std::ptrdiff_t)k);
		in_range.erase(in_range.begin() + (std::ptrdiff_t)k);
	}
	// Collected (by anyone): its collectors stop and take their next order.
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.order_kind[i] != ORDER_COLLECT_TREASURE) continue;
		if (units_.order_flags[i] & ORDER_FLAG_INTERRUPTED) continue;
		if (is_treasure_id(units_.order_target[i])) {
			// A walk that ended short of the treasure (blocked) gives up, as UnitAI does.
			if (units_.action_state[i] == ACTION_IDLE && units_.target_unit_id[i] != units_.order_target[i]) finish_order(i);
			continue;
		}
		if (units_.target_unit_id[i] == units_.order_target[i]) units_.target_unit_id[i] = 0;
		if (units_.action_state[i] == ACTION_MOVE) units_.action_state[i] = ACTION_IDLE;
		units_.has_order[i] = 0;
		const float ox = units_.order_x[i], oz = units_.order_z[i];
		finish_order(i);
		if (units_.order_kind[i] == ORDER_NONE) next_hunt.push_back({units_.id[i], ox, oz});
	}
	// COLLECTTREASURE.FINDINGNEWTARGET: with no next order, the nearest treasure within
	// 64 m of the one that is gone (FindNearbyTreasure).
	for (const NextTreasure &nt : next_hunt) {
		const int32_t i = lookup_unit(nt.uid);
		if (i < 0 || !units_.alive[i] || units_.order_kind[i] != ORDER_NONE) continue;
		auto q = order_queues_.find(nt.uid);
		if (q != order_queues_.end() && !q->second.empty()) continue;
		uint32_t best = 0;
		float best_d2 = (64.0f * COMBAT_DISTANCE_SCALE) * (64.0f * COMBAT_DISTANCE_SCALE);
		for (const Treasure &t : treasures_) {
			const float d2 = (t.x - nt.x) * (t.x - nt.x) + (t.z - nt.z) * (t.z - nt.z);
			if (d2 < best_d2) { best_d2 = d2; best = t.id; }
		}
		if (best != 0) order_collect_treasure(&nt.uid, 1, best);
	}
}

// 0 A.D. Auras.js GetAffectedPlayers: the aura_player_bit() mask of the players an
// aura of `owner` reaches, from its AuraPlayers bits and the current diplomacy.
uint16_t Simulation::aura_affected_players(uint8_t owner, uint8_t players) const {
	uint16_t mask = 0;
	for (uint8_t p = 0; p <= MAX_PLAYERS; ++p) {
		uint8_t pid = p < MAX_PLAYERS ? p : GAIA_PLAYER;
		uint8_t rel = pid == owner ? AURA_PLAYERS_OWNER
			: is_ally(owner, pid) ? AURA_PLAYERS_ALLIES
			: is_hostile(owner, pid) ? AURA_PLAYERS_ENEMIES : 0;
		if (players & rel) mask |= aura_player_bit(pid);
	}
	return mask;
}

void Simulation::rebuild_auras() {
	aura_mgr_.clear();
	// Buildings: 0 A.D. structures/temple_heal (own Humans +1 hp/s within 40 m)
	for (const auto &b : buildings_) {
		if (b.build_progress < 1.0f) continue;
		if (b.type == BLD_TEMPLE) {
			aura_mgr_.register_source(b.id, AURA_HEAL, 40.f * COMBAT_DISTANCE_SCALE, 1.0f, aura_player_bit(b.team),
				b.x, b.z, -1, CC_HUMAN);
		}
	}
	// Units: female gather, carnyx, hero auras from the civ roster
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i]) continue;
		uint8_t ut = units_.unit_type[i];
		uint8_t team = units_.team[i];
		float x = units_.pos_x[i];
		float z = units_.pos_z[i];
		uint32_t id = units_.id[i];
		if (ut == UNIT_FEMALE_CITIZEN) {
			aura_mgr_.register_source(id, AURA_GATHER, 10.f, 0.15f, aura_player_bit(team), x, z);
		}
		if (ut == UNIT_CARNYX) {
			// 0 A.D. units/carnyx: enemy Soldiers within 20 m -10% attack damage
			aura_mgr_.register_source(id, AURA_ATTACK, 20.f * COMBAT_DISTANCE_SCALE, 0.9f,
				aura_affected_players(team, AURA_PLAYERS_ENEMIES), x, z, -1, CC_SOLDIER);
		}
		// Heroes (and other roster units with auras) project their own 0 A.D. auras.
		uint16_t tmpl = units_.tmpl[i];
		if (tmpl < CIV_UNIT_TEMPLATE_COUNT) {
			for (int16_t a : civ_unit_template(tmpl).auras) {
				if (a < 0) continue;
				const CivHeroAura &aura = civ_hero_aura(a);
				if (aura.kind >= AURA_COUNT) continue; // effect RTS0 does not model yet
				aura_mgr_.register_source(id, (AuraType)aura.kind, aura.range, aura.value,
					aura_affected_players(team, aura.players), x, z, a);
			}
		}
	}
}


// =========================================================================
// Map triggers (0 A.D. Trigger.js and Timer.js; engine state in triggers.h)
// =========================================================================

void Simulation::trigger_event(uint8_t event, const TriggerEventData &data) {
	// Trigger.js CallEvent: every enabled trigger listening on the event, in
	// registration order. An action may register more triggers; those wait
	// for the next event.
	const size_t n = triggers_.triggers.size();
	for (size_t i = 0; i < n && i < triggers_.triggers.size(); ++i) {
		const TriggerRegistration r = triggers_.triggers[i];
		if (r.event == event && r.enabled) run_trigger_action(r.action, 0.0f, data);
	}
}

std::vector<const TimeNotification *> Simulation::time_notifications_for(uint8_t player) const {
	std::vector<const TimeNotification *> out;
	for (const TimeNotification &n : triggers_.notifications)
		if (n.end_ms > triggers_.time_ms && (player >= 8 || ((n.players >> player) & 1))) out.push_back(&n);
	return out;
}

void Simulation::tick_triggers(float dt) {
	if (!triggers_.active() && triggers_.notifications.empty()) return;
	// Trigger.js OnGlobalInitGame runs once Game Setup is applied, before the first turn.
	if (!triggers_.init_game_done) {
		triggers_.init_game_done = 1;
		trigger_event(TRIGGER_ON_INIT_GAME);
	}
	// Timer.js OnUpdate: advance the clock, then run every timer due, by id;
	// a repeating timer that is due again runs again in the same update.
	triggers_.time_ms += (uint32_t)std::lround(dt * 1000.0f);
	std::vector<uint32_t> run;
	for (const TriggerTimer &t : triggers_.timers)
		if (t.time_ms <= triggers_.time_ms) run.push_back(t.id);
	for (size_t k = 0; k < run.size(); ++k) {
		int idx = triggers_.find_timer(run[k]);
		if (idx < 0) continue; // an earlier timer cancelled it
		const TriggerTimer t = triggers_.timers[(size_t)idx];
		run_trigger_action(t.action, t.arg, TriggerEventData{});
		idx = triggers_.find_timer(t.id);
		if (idx < 0) continue;
		if (!t.repeat_ms) {
			triggers_.timers.erase(triggers_.timers.begin() + idx);
			continue;
		}
		triggers_.timers[(size_t)idx].time_ms += t.repeat_ms;
		if (triggers_.timers[(size_t)idx].time_ms <= triggers_.time_ms) run.push_back(t.id);
	}
	triggers_.expire_notifications();
}

void Simulation::register_map_triggers(MapType type) {
	// A map's TriggerScripts (its .json "TriggerScripts"); each script's top
	// level runs from OnInitGame so it sees the final Game Setup.
	switch (type) {
		case MAP_FLOOD:
			triggers_.register_trigger(TRIGGER_ON_INIT_GAME, TRIGGER_ACTION_FLOOD_INIT, true);
			break;
		case MAP_EXTINCT_VOLCANO:
			triggers_.register_trigger(TRIGGER_ON_INIT_GAME, TRIGGER_ACTION_VOLCANO_INIT, true);
			break;
		default:
			break;
	}
}

namespace {
// The rising-water maps' heights are 0 A.D.'s metres times this (rms_generator.cpp kTriggerMapHeight).
constexpr float kTriggerHeight = 0.5f;

// maps/random/flood_triggers.js
constexpr float kFloodStepSeconds = 2.4f;   // deltaTime
constexpr float kFloodStepLevel = 0.5f;     // deltaWaterLevel [m]
constexpr float kFloodWarningSeconds = 14.4f;
constexpr float kFloodDrownDepth = 2.0f;    // drownDepth [m]
const char *const kFloodWarning =
	"The flood continues. Soon the waters will swallow the land. You should evacuate the units.";

// maps/random/extinct_volcano_triggers.js
constexpr float kVolcanoNotifyMinutes = 1.0f;   // waterRiseNotificationDuration
constexpr float kVolcanoStepMinMinutes = 0.5f;  // waterIncreaseTime
constexpr float kVolcanoStepMaxMinutes = 1.0f;
constexpr float kVolcanoStepLevel = 1.0f;       // waterLevelIncreaseHeight [m]
constexpr float kVolcanoMaxLevel = 70.0f;       // maxWaterLevel [m]
constexpr float kVolcanoDrownHeight = 1.0f;     // drownHeight [m]
const char *const kVolcanoWarnings[4] = {
	"It keeps on raining, we will have to evacuate soon!",
	"The rivers are standing high, we need to find a safe place!",
	"We have to find dry ground, our lands will drown soon!",
	"The lakes start swallowing the land, we have to find shelter!",
};
// garrisonedUnits = "units/rome/champion_infantry_swordsman_02"
const char *const kVolcanoTowerGarrison = "rome/champion_infantry_swordsman";

uint16_t roster_template_named(const char *id) {
	for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT; ++t)
		if (std::strcmp(civ_unit_template(t).id, id) == 0) return t;
	return CIV_UNIT_TEMPLATE_NONE;
}
} // namespace

void Simulation::run_trigger_action(uint16_t action, float arg, const TriggerEventData &data) {
	(void)data;
	switch (action) {
		case TRIGGER_ACTION_FLOOD_INIT: {
			// flood_triggers.js, top level.
			if (water_level_mode_ == WATER_LEVEL_SHALLOW) return;
			const float level = terrain_water_height_;
			if (water_level_mode_ == WATER_LEVEL_DEEP) {
				set_water_level(level + 1.0f * kTriggerHeight, kFloodDrownDepth * kTriggerHeight);
				return;
			}
			auto schedule = [&](float target, float rise_start_s) {
				triggers_.set_timeout((uint32_t)std::lround(rise_start_s * 1000.0f), TRIGGER_ACTION_FLOOD_RISE_STEP, target);
				triggers_.set_timeout((uint32_t)std::lround((rise_start_s - kFloodWarningSeconds) * 1000.0f), TRIGGER_ACTION_FLOOD_WARNING);
			};
			schedule(level + 1.0f * kTriggerHeight, 260.0f);
			schedule(level + 5.0f * kTriggerHeight, 1560.0f);
			break;
		}
		case TRIGGER_ACTION_FLOOD_RISE_STEP:
			trigger_flood_rise_step(arg);
			break;
		case TRIGGER_ACTION_FLOOD_WARNING:
			// GuiInterface.AddTimeNotification's default duration is 10 s.
			triggers_.add_notification(kFloodWarning, 10000);
			break;
		case TRIGGER_ACTION_VOLCANO_INIT: {
			// extinct_volcano_triggers.js, top level.
			trigger_volcano_garrison_towers();
			const float rise_s = (float)sea_level_rise_min_ * 60.0f;
			const float notify_s = std::max(0.0f, rise_s - kVolcanoNotifyMinutes * 60.0f);
			triggers_.set_timeout((uint32_t)std::lround(notify_s * 1000.0f), TRIGGER_ACTION_VOLCANO_NOTIFY);
			triggers_.set_timeout((uint32_t)std::lround(rise_s * 1000.0f), TRIGGER_ACTION_VOLCANO_RISE_STEP);
			break;
		}
		case TRIGGER_ACTION_VOLCANO_NOTIFY:
			// pickRandom(waterWarningTexts), shown for waterRiseNotificationDuration.
			triggers_.add_notification(kVolcanoWarnings[sim_rng_.range_int(0, 3)],
			                           (uint32_t)(kVolcanoNotifyMinutes * 60000.0f));
			break;
		case TRIGGER_ACTION_VOLCANO_RISE_STEP:
			trigger_volcano_rise_step();
			break;
		default:
			break;
	}
}

void Simulation::trigger_flood_rise_step(float final_level) {
	// flood_triggers.js RiseWaterLevelStep: half a metre every 2.4 s up to the target.
	const float next = terrain_water_height_ + kFloodStepLevel * kTriggerHeight;
	if (next >= final_level - 1e-4f) {
		set_water_level(final_level, kFloodDrownDepth * kTriggerHeight);
		return;
	}
	set_water_level(next, kFloodDrownDepth * kTriggerHeight);
	triggers_.set_timeout((uint32_t)std::lround(kFloodStepSeconds * 1000.0f), TRIGGER_ACTION_FLOOD_RISE_STEP, final_level);
}

void Simulation::trigger_volcano_rise_step() {
	// extinct_volcano_triggers.js RaiseWaterLevelStep: a metre at a time, every
	// half to one minute, until maxWaterLevel.
	const float level = terrain_water_height_ + kVolcanoStepLevel * kTriggerHeight;
	if (level < kVolcanoMaxLevel * kTriggerHeight) {
		const float minutes = sim_rng_.range_float(kVolcanoStepMinMinutes, kVolcanoStepMaxMinutes);
		triggers_.set_timeout((uint32_t)std::lround(minutes * 60000.0f), TRIGGER_ACTION_VOLCANO_RISE_STEP);
	}
	set_water_level(level, kVolcanoDrownHeight * kTriggerHeight);
}

void Simulation::trigger_volcano_garrison_towers() {
	// extinct_volcano_triggers.js GarrisonWoodenTowers: every Gaia tower is
	// filled with Roman champion swordsmen (TriggerHelper.SpawnGarrisonedUnits).
	const uint16_t tmpl = roster_template_named(kVolcanoTowerGarrison);
	std::vector<uint32_t> towers;
	for (const Building &b : buildings_)
		if (b.team == GAIA_PLAYER && b.type == BLD_TOWER && b.build_progress >= 1.0f && b.hp > 0.0f) towers.push_back(b.id);
	for (uint32_t id : towers) {
		const int cap = get_garrison_capacity(id);
		auto it = building_id_to_idx_.find(id);
		if (it == building_id_to_idx_.end()) continue;
		const float x = buildings_[it->second].x, z = buildings_[it->second].z;
		std::vector<uint32_t> garrison;
		for (int i = 0; i < cap; ++i) garrison.push_back(spawn_unit(x, z, GAIA_PLAYER, UNIT_SWORDSMAN, 0.0f, tmpl));
		order_garrison(garrison.data(), garrison.size(), id);
	}
}

void Simulation::set_water_level(float h, float drown_depth) {
	// WaterManager.SetWaterLevel plus the rising-water scripts' drowning pass
	// (flood_triggers.js riseWaterLevel, extinct_volcano_triggers.js RaiseWaterLevelStep).
	terrain_water_height_ = h;
	auto submerged = [&](float x, float z) { return get_terrain_height(x, z) + drown_depth < h; };
	// Organic units and animals drown (cmpHealth.Kill()); ships, siege and
	// units inside ships or siege are kept afloat.
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || unit_is_ship(units_.unit_type[i]) || unit_is_siege_engine(units_.unit_type[i])) continue;
		if (!submerged(units_.pos_x[i], units_.pos_z[i])) continue;
		units_.hp[i] = 0.0f;
		units_.alive[i] = 0;
		units_.action_state[i] = ACTION_DYING;
		units_.has_order[i] = 0;
		record_unit_death(i, units_.team[i]);
		stat_unit_lost(i, 0xFF);
	}
	for (size_t f = 0; f < fauna_.count; ++f)
		if (fauna_.alive[f] && submerged(fauna_.pos_x[f], fauna_.pos_z[f])) {
			fauna_.alive[f] = 0;
			fauna_.hp[f] = 0.0f;
		}
	// Structures and resources under water become decorative actors in 0 A.D.:
	// here they are gone. Docks float with the water.
	bool lost_building = false, lost_root = false;
	for (Building &b : buildings_) {
		if (b.wrecked || b.hp <= 0.0f || b.type == BLD_DOCK || !submerged(b.x, b.z)) continue;
		for (uint32_t uid : b.garrisoned_units) {
			const int32_t u = lookup_unit(uid);
			if (u < 0) continue;
			units_.hp[u] = 0.0f;
			units_.alive[u] = 0;
			units_.action_state[u] = ACTION_DYING;
			units_.pos_x[u] = b.x;
			units_.pos_z[u] = b.z;
		}
		b.garrisoned_units.clear();
		b.queue.clear();
		// mark_building_wreck, but nobody killed it: no loot for anyone.
		stat_building_lost(b, 0xFF);
		b.wrecked = true;
		b.hp = 0.0f;
		b.build_progress = 0.0f;
		record_death(b.x, b.z, b.team, b.team, b.type, true);
		lost_building = true;
		lost_root |= b.type == BLD_TOWN_CENTER || b.type == BLD_FORTRESS || b.type == BLD_TOWER;
	}
	for (size_t r = 0; r < resources_.count; ++r) {
		if (!resources_.alive[r] || resources_.subtype[r] == RES_SUB_FISH) continue;
		if (!submerged(resources_.x[r], resources_.z[r])) continue;
		resources_.alive[r] = 0;
		resources_.amount[r] = 0;
	}
	(void)lost_building;
	rebuild_nav_grid(); // new water cells, and the drowned structures' footprints
	if (lost_root) rebuild_territory();
}

} // namespace rtso
