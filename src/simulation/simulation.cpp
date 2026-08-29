#include "simulation.h"
#include "../core/job_system.h"
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <fstream>

namespace rtso {

namespace {
constexpr int kUnitGridRes = 64; // 64x64 grid cells (2.0m per cell over 128m map)

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
} // namespace

Simulation::Simulation() {
	units_.init(UNIT_CAP);
	resources_.init(512);
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
	building_id_to_idx_.clear();
	occupied_cells_.clear();

	next_id_ = 1;
	food_ = 350;
	wood_ = 450;
	gold_ = 300;
	stone_ = 250;
	passive_timer_ = 0.0f;
	rate_window_timer_ = 0.0f;

	food_gathered_window_ = 0.0f;
	wood_gathered_window_ = 0.0f;
	gold_gathered_window_ = 0.0f;
	stone_gathered_window_ = 0.0f;

	player_phase_ = 1;
	player_civ_[0] = CIV_ATHENIANS;
	researched_techs_mask_ = 0;

	wave_timer_ = 0.0f;
	wave_number_ = 0;
	game_over_ = false;
	victory_ = false;
	relics_.clear();
	wonder_victory_timer_ = 0.0f;
	has_active_wonder_ = false;
	record_ = MatchRecord();
	tick_us_ = ticks_ = 0;
	frame_arena_.reset_all();
	memset(fow_grid_, 0, sizeof(fow_grid_));
	fauna_.count = 0;
	fauna_id_to_idx_.clear();
}

int Simulation::pop_current() const {
	int count = 0;
	for (size_t i = 0; i < units_.count; ++i) {
		if (units_.team[i] == 0 && (units_.alive[i] || units_.action_state[i] == ACTION_GARRISONED)) count++;
	}
	return count;
}

int Simulation::pop_max() const {
	int cap = 10;
	for (const auto &b : buildings_) {
		if (b.team == 0 && b.build_progress >= 1.0f) {
			cap += building_type_info(b.type).pop_bonus;
		}
	}
	return cap;
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

	// 3. Initial Units (6 Villagers, 2 Swordsmen, 2 Archers)
	for (int i = 0; i < 6; ++i) {
		float a = 6.28318530718f * i / 6.0f;
		float x = cosf(a) * 5.0f;
		float z = sinf(a) * 5.0f + 5.0f;
		spawn_unit(x, z, 0, UNIT_VILLAGER, 100.0f);
	}
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
}

void Simulation::spawn_war(int total_units) {
	// Reset units and buildings without clearing loaded heightmap
	units_.free_all(); units_.init(UNIT_CAP);
	buildings_.clear();
	projectiles_.clear();
	unit_id_to_idx_.clear();
	unit_idx_by_id_.clear();
	res_id_to_idx_.clear();
	building_id_to_idx_.clear();
	occupied_cells_.clear();
	relics_.clear();
	memset(fow_grid_, 0, sizeof(fow_grid_));

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

uint32_t Simulation::spawn_unit(float x, float z, uint8_t team, UnitType u_type, float hp) {
	uint32_t uid = alloc_id();
	size_t idx = units_.push(x, z, uid, team, u_type);
	units_.hp[idx] = units_.max_hp[idx] = hp;

	// Apply researched military tech bonuses & Civilization traits to player units
	if (team == 0) {
		if (is_tech_researched(TECH_WEAPONS_1)) units_.attack_damage[idx] *= 1.20f;
		if (is_tech_researched(TECH_ARMOR_1)) {
			units_.max_hp[idx] += 30.0f;
			units_.hp[idx] = units_.max_hp[idx];
		}
		// Civ bonuses
		if (player_civ_[0] == CIV_SPARTANS && (u_type == UNIT_SWORDSMAN || u_type == UNIT_SPEARMAN)) {
			units_.max_hp[idx] *= 1.25f;
			units_.hp[idx] = units_.max_hp[idx];
			units_.attack_damage[idx] *= 1.15f;
		} else if (player_civ_[0] == CIV_PERSIANS && u_type == UNIT_CAVALRY) {
			units_.speed[idx] *= 1.20f;
		}
		record_.units_trained++;
	}

	bind_unit_index(uid, idx);
	return uid;
}

uint32_t Simulation::spawn_resource(int cell_x, int cell_y, uint8_t type, int32_t amount) {
	float x = (cell_x + 0.5f) * GRID;
	float z = (cell_y + 0.5f) * GRID;
	return spawn_resource_world(x, z, type, amount);
}

uint32_t Simulation::spawn_resource_world(float wx, float wz, uint8_t type, int32_t amount) {
	uint32_t rid = alloc_id();
	size_t idx = resources_.push(wx, wz, amount, type, rid);
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
	b.max_hp = binfo.max_hp;
	if (team == 0 && player_civ_[0] == CIV_ROMANS) b.max_hp *= 1.20f; // Roman architecture bonus
	b.hp = b.max_hp * progress;
	b.build_progress = progress;
	b.cell_x = cell_x;
	b.cell_y = cell_y;
	b.footprint = footprint;
	b.attack_timer = 0.0f;
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
	b.max_capture_points = std::max(200.0f, binfo.max_hp * 0.4f);

	buildings_.push_back(b);
	building_id_to_idx_[bid] = buildings_.size() - 1;

	for (int dx = 0; dx < footprint; ++dx) {
		for (int dy = 0; dy < footprint; ++dy) {
			occupied_cells_.insert({cell_x + dx, cell_y + dy});
		}
	}
	return bid;
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

bool Simulation::cmd_build(int cell_x, int cell_y, uint8_t b_type) {
	const BuildingTypeInfo &binfo = building_type_info(b_type);
	if (!can_place(cell_x, cell_y, binfo.footprint)) return false;

	if (wood_ < binfo.wood_cost || stone_ < binfo.stone_cost ||
		gold_ < binfo.gold_cost || food_ < binfo.food_cost) {
		return false;
	}

	wood_ -= binfo.wood_cost;
	stone_ -= binfo.stone_cost;
	gold_ -= binfo.gold_cost;
	food_ -= binfo.food_cost;

	spawn_building(cell_x, cell_y, b_type, 0, 0.05f);
	record_.buildings_constructed++;
	return true;
}

bool Simulation::cmd_train(uint32_t building_id, UnitType u_type) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return false;
	Building &b = buildings_[bit->second];
	if (b.team != 0 || b.build_progress < 1.0f) return false;
	if (b.queue.size() >= 8) return false;

	const UnitTypeInfo &uinfo = unit_type_info(u_type);
	if (pop_current() >= pop_max()) return false;

	if (food_ < uinfo.food_cost || wood_ < uinfo.wood_cost ||
		gold_ < uinfo.gold_cost || stone_ < uinfo.stone_cost) {
		return false;
	}

	food_ -= uinfo.food_cost;
	wood_ -= uinfo.wood_cost;
	gold_ -= uinfo.gold_cost;
	stone_ -= uinfo.stone_cost;

	ProductionOrder order;
	order.unit_type = u_type;
	order.timer = 0.0f;
	order.duration = uinfo.train_time;
	if (player_civ_[0] == CIV_PERSIANS && u_type == UNIT_CAVALRY) order.duration *= 0.80f;
	b.queue.push_back(order);
	return true;
}

bool Simulation::cmd_cancel_train(uint32_t building_id, size_t queue_idx) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return false;
	Building &b = buildings_[bit->second];
	if (queue_idx >= b.queue.size()) return false;

	uint8_t utype = b.queue[queue_idx].unit_type;
	const UnitTypeInfo &uinfo = unit_type_info(utype);
	food_ += uinfo.food_cost;
	wood_ += uinfo.wood_cost;
	gold_ += uinfo.gold_cost;
	stone_ += uinfo.stone_cost;

	b.queue.erase(b.queue.begin() + queue_idx);
	return true;
}

void Simulation::cmd_set_rally_point(uint32_t building_id, float rx, float rz, uint32_t res_id, uint32_t bld_id) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return;
	Building &b = buildings_[bit->second];
	b.has_rally_point = true;
	b.rally_x = rx;
	b.rally_z = rz;
	b.rally_target_res_id = res_id;
	b.rally_target_bld_id = bld_id;
}

bool Simulation::cmd_research(uint32_t building_id, uint8_t tech_id) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return false;
	Building &b = buildings_[bit->second];
	if (b.team != 0 || b.build_progress < 1.0f || b.research_tech != TECH_NONE) return false;
	if (is_tech_researched(tech_id)) return false;

	const TechInfo &tinfo = tech_info(tech_id);
	if (tinfo.required_phase > player_phase_) return false;

	if (food_ < tinfo.food_cost || wood_ < tinfo.wood_cost ||
		gold_ < tinfo.gold_cost || stone_ < tinfo.stone_cost) {
		return false;
	}

	food_ -= tinfo.food_cost;
	wood_ -= tinfo.wood_cost;
	gold_ -= tinfo.gold_cost;
	stone_ -= tinfo.stone_cost;

	b.research_tech = tech_id;
	b.research_timer = 0.0f;
	b.research_duration = tinfo.research_time;
	return true;
}

bool Simulation::cmd_barter(ResourceType sell, ResourceType buy, int amount) {
	if (sell == buy || amount <= 0) return false;
	int *src = (sell == RES_FOOD) ? &food_ : ((sell == RES_WOOD) ? &wood_ : ((sell == RES_GOLD) ? &gold_ : &stone_));
	int *dst = (buy == RES_FOOD) ? &food_ : ((buy == RES_WOOD) ? &wood_ : ((buy == RES_GOLD) ? &gold_ : &stone_));
	if (*src < amount) return false;

	*src -= amount;
	float spread = (player_civ_[0] == CIV_CARTHAGINIANS) ? 0.85f : 0.75f; // Carthaginian trade bonus
	int received = (int)(amount * spread);
	*dst += received;
	return true;
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

	for (size_t i = 0; i < count; ++i) {
		int32_t idx = lookup_unit(unit_ids[i]);
		if (idx < 0 || (size_t)idx >= units_.count || !units_.alive[idx]) continue;

		float ox = 0.0f, oz = 0.0f;

		if (form == FORMATION_LINE) {
			// Horizontal Battle Line
			float spacing = 1.4f;
			ox = ((int)i - (int)(count - 1) * 0.5f) * spacing;
			oz = 0.0f;
		} else if (form == FORMATION_COLUMN) {
			// Two parallel marching files
			float spacing = 1.2f;
			ox = ((int)(i % 2) - 0.5f) * spacing;
			oz = -((int)(i / 2)) * spacing;
		} else if (form == FORMATION_PHALANX) {
			// Dense Syntagma Shield Wall (4 ranks wide)
			int ranks = 4;
			float spacing = 0.85f;
			int col = (int)i % ranks;
			int row = (int)i / ranks;
			ox = (col - (ranks - 1) * 0.5f) * spacing;
			oz = -row * spacing;
		} else if (form == FORMATION_WEDGE) {
			// Spearhead Wedge
			int row = (int)std::floor(std::sqrt((float)(2 * i)));
			int row_start = row * (row + 1) / 2;
			int pos_in_row = (int)i - row_start;
			float spacing = 1.3f;
			ox = (pos_in_row - row * 0.5f) * spacing;
			oz = -row * spacing;
		} else if (form == FORMATION_SCATTER) {
			// Dispersed skirmish cloud
			float a = (float)i * 2.3999632f; // Golden ratio spiral
			float r = std::sqrt((float)i + 1.0f) * 2.0f;
			ox = cosf(a) * r;
			oz = sinf(a) * r;
		} else {
			// Standard Square Grid
			int side = std::max(1, (int)std::ceil(std::sqrt((float)count)));
			float spacing = 1.35f;
			int row = (int)i / side;
			int col = (int)i % side;
			ox = (col - (side - 1) * 0.5f) * spacing;
			oz = (row - (side - 1) * 0.5f) * spacing;
		}

		units_.target_x[idx] = tx + ox;
		units_.target_z[idx] = tz + oz;
		units_.has_order[idx] = 1;
		units_.action_state[idx] = ACTION_MOVE;
		units_.target_resource_id[idx] = 0;
		units_.target_unit_id[idx] = 0;
		units_.target_building_id[idx] = 0;
	}
}

void Simulation::cmd_stop(const uint32_t *unit_ids, size_t count) {
	for (size_t i = 0; i < count; ++i) {
		int32_t idx = lookup_unit(unit_ids[i]);
		if (idx >= 0 && (size_t)idx < units_.count && units_.alive[idx]) {
			units_.has_order[idx] = 0;
			units_.action_state[idx] = ACTION_IDLE;
			units_.target_resource_id[idx] = 0;
			units_.target_unit_id[idx] = 0;
			units_.target_building_id[idx] = 0;
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
		if (uidx < 0 || units_.team[uidx] != 0 || !units_.alive[uidx]) continue;

		units_.target_x[uidx] = rx;
		units_.target_z[uidx] = rz;
		units_.has_order[uidx] = 1;
		units_.action_state[uidx] = ACTION_GATHER;
		units_.target_resource_id[uidx] = resource_id;
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
		if (uidx < 0 || units_.team[uidx] != 0 || !units_.alive[uidx]) continue;

		units_.target_x[uidx] = tx;
		units_.target_z[uidx] = tz;
		units_.has_order[uidx] = 1;
		units_.action_state[uidx] = ACTION_ATTACK;
		units_.target_unit_id[uidx] = target_unit_id;
		units_.target_resource_id[uidx] = 0;
		units_.target_building_id[uidx] = 0;
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
		if (uidx < 0 || units_.team[uidx] != 0 || units_.unit_type[uidx] != UNIT_VILLAGER || !units_.alive[uidx]) continue;

		units_.target_x[uidx] = b.x;
		units_.target_z[uidx] = b.z;
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
	for (size_t i = 0; i < count; ++i) {
		int32_t idx = lookup_unit(unit_ids[i]);
		if (idx >= 0) {
			units_.formation[idx] = formation;
		}
	}
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
		}
		b.garrisoned_units.erase(it);
	}
}

void Simulation::fire_arrow(float sx, float sy, float sz, float tx, float ty, float tz, float damage, uint8_t team, uint32_t target_id) {
	if (projectiles_.size() >= MAX_PROJECTILES) return;
	Projectile p;
	p.id = alloc_id();
	p.start_x = sx; p.start_y = sy; p.start_z = sz;
	p.target_x = tx; p.target_y = ty; p.target_z = tz;
	p.current_x = sx; p.current_y = sy; p.current_z = sz;
	p.progress = 0.0f;
	p.speed = 26.0f;
	p.arc_height = 2.4f;
	p.damage = damage;
	p.team = team;
	p.target_unit_id = target_id;
	p.alive = true;
	projectiles_.push_back(p);
}

void Simulation::tick(float dt) {
	if (game_over_ || victory_) return;
	auto t0 = std::chrono::high_resolution_clock::now();
	Arena &arena = frame_arena_.current();
	arena.reset();

	move_units(dt, arena);
	separation(dt, arena);
	tick_gather(dt);
	tick_combat(dt);
	tick_construction(dt);
	tick_production(dt);
	tick_projectiles(dt);
	tick_waves(dt);
	tick_trickle(dt);
	tick_petra_ai(dt);
	tick_fauna(dt);

	// Income rate rolling decay
	rate_window_timer_ += dt;
	if (rate_window_timer_ >= 5.0f) {
		rate_window_timer_ = 0.0f;
		food_gathered_window_ *= 0.5f;
		wood_gathered_window_ *= 0.5f;
		gold_gathered_window_ *= 0.5f;
		stone_gathered_window_ *= 0.5f;
	}

	// Passive income
	passive_timer_ += dt;
	if (passive_timer_ >= 1.0f) {
		passive_timer_ = 0.0f;
		food_ += 2;
		wood_ += 2;
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

	JobSystem::instance().for_each_index(n, [&](size_t i) {
		if (!alive[i]) {
			vx[i] = vz[i] = 0;
			if (units_.action_state[i] == ACTION_DYING) {
				units_.death_timer[i] += dt;
			}
			return;
		}

		// 1. Merchant Trader Automatic Route Traversal
		if (units_.unit_type[i] == UNIT_TRADER && units_.action_state[i] == ACTION_TRADE) {
			float dx = tx[i] - px[i];
			float dz = tz[i] - pz[i];
			if (dx * dx + dz * dz < 2.5f * 2.5f) {
				int rev = (player_civ_[0] == CIV_ATHENIANS) ? 45 : 35;
				gold_ += rev;
				gold_gathered_window_ += rev;
				record_.total_gold_gathered += rev;

				// Swap destination between TC (0,0) and friendly Market
				if (std::abs(tx[i]) < 2.0f && std::abs(tz[i]) < 2.0f) {
					tx[i] = 8.0f; tz[i] = 6.0f;
				} else {
					tx[i] = 0.0f; tz[i] = 0.0f;
				}
			}
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

		if (!has[i]) {
			vx[i] = vz[i] = 0;
			return;
		}

		float aim_x = tx[i];
		float aim_z = tz[i];
		if (units_.target_unit_id[i] != 0) {
			int32_t tidx = lookup_unit(units_.target_unit_id[i]);
			if (tidx >= 0 && units_.alive[tidx]) {
				aim_x = px[tidx];
				aim_z = pz[tidx];
				tx[i] = aim_x;
				tz[i] = aim_z;
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
			} else if (units_.action_state[i] == ACTION_PATROL) {
				float orig_x = tx[i];
				float orig_z = tz[i];
				tx[i] = -orig_x * 0.8f;
				tz[i] = -orig_z * 0.8f;
			}
			return;
		}

		float inv = 1.0f / sqrtf(d2);
		float ux = dx * inv;
		float uz = dz * inv;
		float spd = units_.speed[i];
		npx[i] = std::clamp(px[i] + ux * spd * dt, -map_half_, map_half_);
		npz[i] = std::clamp(pz[i] + uz * spd * dt, -map_half_, map_half_);
		vx[i] = ux * spd;
		vz[i] = uz * spd;
	});

	memcpy(px, npx, n * sizeof(float));
	memcpy(pz, npz, n * sizeof(float));
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

		// 2. Building Obstacle Non-Penetration
		for (const auto &b : buildings_) {
			float b_half = b.half_size;
			float cx = std::clamp(pxi, b.x - b_half, b.x + b_half);
			float cz = std::clamp(pzi, b.z - b_half, b.z + b_half);
			float bdx = pxi - cx;
			float bdz = pzi - cz;
			float bd2 = bdx * bdx + bdz * bdz;
			float b_req = ri + 0.20f;
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

void Simulation::tick_gather(float dt) {
	const size_t n = units_.count;
	for (size_t i = 0; i < n; ++i) {
		if (!units_.alive[i] || units_.team[i] != 0 || units_.unit_type[i] != UNIT_VILLAGER) continue;

		// 1. Returning to dropsite
		if (units_.carry[i] >= units_.carry_capacity[i] || units_.action_state[i] == ACTION_RETURN_DROPSITE) {
			float best_d2 = 1e9f;
			float drop_x = 0, drop_z = 0;
			bool found_dropsite = false;

			for (const auto &b : buildings_) {
				if (b.team != 0 || b.build_progress < 1.0f) continue;
				bool valid_drop = false;
				if (b.type == BLD_TOWN_CENTER) valid_drop = true;
				else if (b.type == BLD_STOREHOUSE && (units_.res_type_carried[i] != RES_FOOD)) valid_drop = true;
				else if (b.type == BLD_FARMSTEAD && (units_.res_type_carried[i] == RES_FOOD)) valid_drop = true;

				if (valid_drop) {
					float dx = units_.pos_x[i] - b.x;
					float dz = units_.pos_z[i] - b.z;
					float d2 = dx * dx + dz * dz;
					if (d2 < best_d2) {
						best_d2 = d2;
						drop_x = b.x; drop_z = b.z;
						found_dropsite = true;
					}
				}
			}

			if (found_dropsite) {
				float dx = units_.pos_x[i] - drop_x;
				float dz = units_.pos_z[i] - drop_z;
				if (dx * dx + dz * dz < 2.5f * 2.5f) {
					int amt = units_.carry[i];
					if (units_.res_type_carried[i] == RES_WOOD) { wood_ += amt; wood_gathered_window_ += amt; record_.total_wood_gathered += amt; }
					else if (units_.res_type_carried[i] == RES_GOLD) { gold_ += amt; gold_gathered_window_ += amt; record_.total_gold_gathered += amt; }
					else if (units_.res_type_carried[i] == RES_STONE) { stone_ += amt; stone_gathered_window_ += amt; record_.total_stone_gathered += amt; }
					else if (units_.res_type_carried[i] == RES_FOOD) { food_ += amt; food_gathered_window_ += amt; record_.total_food_gathered += amt; }

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
			float dx = units_.pos_x[i] - resources_.x[ridx];
			float dz = units_.pos_z[i] - resources_.z[ridx];
			if (dx * dx + dz * dz < 2.2f * 2.2f) {
				units_.attack_timer[i] += dt;
				float harvest_rate = 0.75f;
				if (is_tech_researched(TECH_FARMING_1) && resources_.type[ridx] == RES_FOOD) harvest_rate = 0.55f;
				if (is_tech_researched(TECH_WOODCUTTING_1) && resources_.type[ridx] == RES_WOOD) harvest_rate = 0.55f;
				if (is_tech_researched(TECH_MINING_1) && (resources_.type[ridx] == RES_GOLD || resources_.type[ridx] == RES_STONE)) harvest_rate = 0.55f;

				// Civ gathering bonuses
				if (player_civ_[0] == CIV_ATHENIANS && resources_.type[ridx] == RES_WOOD) harvest_rate *= 0.80f;
				if (player_civ_[0] == CIV_CARTHAGINIANS && resources_.type[ridx] == RES_GOLD) harvest_rate *= 0.75f;

				if (units_.attack_timer[i] >= harvest_rate) {
					units_.attack_timer[i] = 0.0f;
					int32_t space = units_.carry_capacity[i] - units_.carry[i];
					int32_t take = std::min(8, std::min(resources_.amount[ridx], space));
					resources_.amount[ridx] -= take;
					units_.carry[i] += take;
					units_.res_type_carried[i] = resources_.type[ridx];

					if (units_.carry[i] >= units_.carry_capacity[i]) {
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
	// Count builders per building for multi-worker scaling
	std::unordered_map<uint32_t, int> builders_per_bld;
	const size_t n = units_.count;
	for (size_t i = 0; i < n; ++i) {
		if (units_.alive[i] && units_.action_state[i] == ACTION_BUILD && units_.target_building_id[i] != 0) {
			builders_per_bld[units_.target_building_id[i]]++;
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
		float dx = units_.pos_x[i] - b.x;
		float dz = units_.pos_z[i] - b.z;
		if (dx * dx + dz * dz < (b.half_size + 1.6f) * (b.half_size + 1.6f)) {
			int worker_count = std::max(1, builders_per_bld[b.id]);
			float speed_mult = sqrtf((float)worker_count) / (float)worker_count;
			if (player_civ_[0] == CIV_ROMANS) speed_mult *= 1.25f; // Roman fast construction

			if (b.build_progress < 1.0f) {
				const BuildingTypeInfo &binfo = building_type_info(b.type);
				float rate = (1.0f / std::max(5.0f, binfo.build_time)) * speed_mult * dt;
				b.build_progress = std::min(1.0f, b.build_progress + rate);
				b.hp = b.max_hp * b.build_progress;
				if (b.build_progress >= 1.0f) {
					units_.action_state[i] = ACTION_IDLE;
					units_.target_building_id[i] = 0;
				}
			} else if (b.hp < b.max_hp) {
				// Repair finished building
				b.hp = std::min(b.max_hp, b.hp + dt * 30.0f * speed_mult);
			}
		}
	}

	// 2. 0 A.D. Territory Isolation Decay (Paced at 1 Hz)
	territory_timer_ += dt;
	if (territory_timer_ >= 1.0f) {
		territory_timer_ = 0.0f;
		for (auto &b : buildings_) {
			if (b.build_progress < 1.0f) continue;
			if (b.type == BLD_TOWN_CENTER || b.type == BLD_FORTRESS || b.type == BLD_TOWER) continue;

			bool in_territory = false;
			for (const auto &proj : buildings_) {
				if (proj.team != b.team || proj.build_progress < 1.0f) continue;
				float r = 0.0f;
				if (proj.type == BLD_TOWN_CENTER) r = 32.0f;
				else if (proj.type == BLD_FORTRESS) r = 28.0f;
				else if (proj.type == BLD_TOWER) r = 18.0f;

				if (r > 0.0f) {
					float dx = b.x - proj.x;
					float dz = b.z - proj.z;
					if (dx * dx + dz * dz <= r * r) {
						in_territory = true;
						break;
					}
				}
			}

			if (!in_territory) {
				b.hp = std::max(10.0f, b.hp - 1.5f); // Progressive isolation decay per second
			}
		}
	}
}

void Simulation::tick_production(float dt) {
	for (auto &b : buildings_) {
		if (b.team != 0 || b.build_progress < 1.0f) continue;

		// 1. Tech Research
		if (b.research_tech != TECH_NONE) {
			b.research_timer += dt;
			if (b.research_timer >= b.research_duration) {
				researched_techs_mask_ |= (1u << b.research_tech);
				if (b.research_tech == TECH_PHASE_TOWN) player_phase_ = 2;
				else if (b.research_tech == TECH_PHASE_CITY) player_phase_ = 3;
				b.research_tech = TECH_NONE;
			}
		}

		// 2. Unit Production Queue
		if (!b.queue.empty()) {
			ProductionOrder &order = b.queue.front();
			order.timer += dt;
			if (order.timer >= order.duration) {
				uint8_t utype = order.unit_type;
				b.queue.erase(b.queue.begin());

				// Spawn unit near building
				float sx = b.x + (b.footprint + 0.6f) * GRID;
				float sz = b.z;
				uint32_t uid = spawn_unit(sx, sz, 0, (UnitType)utype);

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

void Simulation::tick_combat(float dt) {
	const size_t n = units_.count;
	Arena &arena = frame_arena_.current();
	int32_t *target_idx = n ? arena.alloc_array<int32_t>(n) : nullptr;
	if (n && !target_idx) return;

	UnitGrid grid;
	if (n && !grid.build(arena, n, units_.pos_x, units_.pos_z, units_.alive, map_half_)) return;

	JobSystem::instance().for_each_index(n, [&](size_t i) {
		if (units_.attack_timer[i] > 0) units_.attack_timer[i] -= dt;
		target_idx[i] = -1;
		if (!units_.alive[i]) return;
		if (units_.unit_type[i] == UNIT_VILLAGER &&
				(units_.action_state[i] == ACTION_GATHER || units_.action_state[i] == ACTION_BUILD ||
						units_.action_state[i] == ACTION_RETURN_DROPSITE)) {
			return;
		}

		int32_t found = -1;
		if (units_.target_unit_id[i] != 0) {
			int32_t tidx = lookup_unit(units_.target_unit_id[i]);
			if (tidx >= 0 && units_.alive[tidx] && units_.team[tidx] != units_.team[i]) {
				found = tidx;
			}
		}

		if (found < 0 && units_.stance[i] != STANCE_STAND_GROUND && units_.stance[i] != STANCE_PASSIVE) {
			if ((i + ticks_) % 4 == 0 || units_.action_state[i] == ACTION_ATTACK) {
				float scan_range = unit_type_info(units_.unit_type[i]).aggro_range;
				if (units_.action_state[i] == ACTION_ATTACK || units_.action_state[i] == ACTION_ATTACK_MOVE || units_.action_state[i] == ACTION_PATROL) {
					scan_range = std::max(scan_range, 16.0f);
				}
				float best_scan_d2 = scan_range * scan_range;
				const uint8_t my_team = units_.team[i];
				const float pxi = units_.pos_x[i];
				const float pzi = units_.pos_z[i];
				grid.visit_radius(pxi, pzi, scan_range, [&](int32_t j) {
					if (!units_.alive[j] || units_.team[j] == my_team) return;
					float dx = pxi - units_.pos_x[j];
					float dz = pzi - units_.pos_z[j];
					float d2 = dx * dx + dz * dz;
					if (d2 < best_scan_d2) {
						best_scan_d2 = d2;
						found = j;
					}
				});
			}
		}
		if (found >= 0) {
			units_.target_unit_id[i] = units_.id[found];
			units_.has_order[i] = 1;
			units_.target_x[i] = units_.pos_x[found];
			units_.target_z[i] = units_.pos_z[found];
			if (units_.unit_type[i] == UNIT_VILLAGER &&
					(units_.action_state[i] == ACTION_GATHER || units_.action_state[i] == ACTION_BUILD ||
							units_.action_state[i] == ACTION_RETURN_DROPSITE)) {
			} else {
				units_.action_state[i] = ACTION_ATTACK;
			}
		} else {
			units_.target_unit_id[i] = 0;
		}
		target_idx[i] = found;
	});

	for (size_t i = 0; i < n; ++i) {
		int32_t t = target_idx[i];
		if (t < 0 || !units_.alive[i] || units_.attack_timer[i] > 0) continue;
		float dx = units_.pos_x[i] - units_.pos_x[t];
		float dz = units_.pos_z[i] - units_.pos_z[t];
		float d2 = dx * dx + dz * dz;

		float att_y = get_terrain_height(units_.pos_x[i], units_.pos_z[i]);
		float def_y = get_terrain_height(units_.pos_x[t], units_.pos_z[t]);
		float height_diff = att_y - def_y;

		float eff_range = units_.attack_range[i];
		if (height_diff > 1.0f) {
			eff_range += std::min(height_diff * 1.2f, units_.attack_range[i] * 0.4f); // High-ground range extension
		}
		if (d2 > eff_range * eff_range) continue;

		units_.attack_timer[i] = units_.attack_cooldown[i];
		units_.action_state[i] = ACTION_ATTACK;

		// 0 A.D. Exact Exponential Armor Formula (Attack.js + Resistance.js):
		// Damage = sum( Dmg[Type] * 0.9^Armor[Type] )
		const auto &uinfo_dst = unit_type_info(units_.unit_type[t]);
		float target_hack = uinfo_dst.hack_armor;
		float target_pierce = uinfo_dst.pierce_armor;
		float target_crush = uinfo_dst.crush_armor;

		// Tactical formation armor bonuses
		if (units_.formation[t] == FORMATION_PHALANX) target_pierce += 3.0f; // Shield wall
		if (units_.formation[t] == FORMATION_SCATTER) target_pierce += 2.0f; // Loose skirmish

		float base_dmg = units_.attack_damage[i];
		if (units_.formation[i] == FORMATION_WEDGE) base_dmg *= 1.25f; // Wedge charge bonus

		// Counter Multipliers matching 0 A.D. template bonuses
		float counter_mul = 1.0f;
		if (units_.unit_type[i] == UNIT_SPEARMAN && units_.unit_type[t] == UNIT_CAVALRY) counter_mul = 2.5f;
		else if (units_.unit_type[i] == UNIT_CAVALRY && units_.unit_type[t] == UNIT_ARCHER) counter_mul = 1.6f;
		else if (units_.unit_type[i] == UNIT_SWORDSMAN && units_.unit_type[t] == UNIT_SPEARMAN) counter_mul = 1.4f;

		base_dmg *= counter_mul;

		float final_dmg = 1.0f;
		if (units_.unit_type[i] == UNIT_ARCHER) {
			final_dmg = std::max(1.0f, base_dmg * powf(0.9f, target_pierce));
		} else if (units_.unit_type[i] == UNIT_CATAPULT || units_.unit_type[i] == UNIT_SIEGE_RAM) {
			final_dmg = std::max(1.0f, base_dmg * powf(0.9f, target_crush));
		} else if (units_.unit_type[i] == UNIT_SPEARMAN) {
			final_dmg = std::max(1.0f, (base_dmg * 0.5f * powf(0.9f, target_hack)) + (base_dmg * 0.5f * powf(0.9f, target_pierce)));
		} else {
			final_dmg = std::max(1.0f, base_dmg * powf(0.9f, target_hack));
		}

		// Elevation damage advantage (RangeManager.cpp High Ground bonus)
		if (height_diff > 1.0f) {
			final_dmg *= (1.0f + std::min(0.25f, height_diff * 0.05f));
		} else if (height_diff < -1.0f) {
			final_dmg *= std::max(0.80f, 1.0f + height_diff * 0.04f); // Uphill attack penalty
		}

		if (units_.unit_type[i] == UNIT_ARCHER) {
			fire_arrow(units_.pos_x[i], 1.4f, units_.pos_z[i],
			           units_.pos_x[t], 0.8f, units_.pos_z[t],
			           final_dmg, units_.team[i], units_.id[t]);
		} else {
			units_.hp[t] -= final_dmg;

			// Catapult Siege Splash Damage (0 A.D. Quadratic Falloff: 1 - d^2 / r^2)
			if (units_.unit_type[i] == UNIT_CATAPULT) {
				float blast_r = 4.5f;
				const uint8_t att_team = units_.team[i];
				float tx = units_.pos_x[t];
				float tz = units_.pos_z[t];
				grid.visit_radius(tx, tz, blast_r, [&](int32_t s_idx) {
					if (!units_.alive[s_idx] || units_.team[s_idx] == att_team || s_idx == t) return;
					float sx = units_.pos_x[s_idx] - tx;
					float sz = units_.pos_z[s_idx] - tz;
					float dist2 = sx * sx + sz * sz;
					if (dist2 >= blast_r * blast_r) return;
					float falloff = 1.0f - (dist2 / (blast_r * blast_r));
					float s_hack = unit_type_info(units_.unit_type[s_idx]).hack_armor;
					float s_dmg = std::max(1.0f, final_dmg * falloff * powf(0.9f, s_hack * 0.5f));
					units_.hp[s_idx] -= s_dmg;
					if (units_.hp[s_idx] <= 0) {
						units_.alive[s_idx] = 0;
						units_.action_state[s_idx] = ACTION_DYING;
						if (units_.team[s_idx] == 0) record_.units_lost++;
						else {
							record_.enemies_killed++;
							gold_ += 6; food_ += 5; // Loot.js
						}
					}
				});
			}

			if (units_.hp[t] <= 0) {
				units_.alive[t] = 0;
				units_.action_state[t] = ACTION_DYING;
				if (units_.team[t] == 0) {
					record_.units_lost++;
				} else {
					record_.enemies_killed++;
					// Loot.js - Spoils of War Plunder
					if (units_.team[i] == 0) {
						gold_ += 8;
						food_ += 6;
					}
				}

				// Promotion.js - Unit Veterancy & Experience
				units_.kills[i]++;
				if (units_.kills[i] >= 3 && units_.rank[i] < 2) {
					units_.rank[i] = 2; // Advanced
					units_.max_hp[i] *= 1.15f;
					units_.hp[i] = std::min(units_.max_hp[i], units_.hp[i] + 20.0f);
					units_.attack_damage[i] *= 1.10f;
				} else if (units_.kills[i] >= 7 && units_.rank[i] < 3) {
					units_.rank[i] = 3; // Elite
					units_.max_hp[i] *= 1.15f;
					units_.hp[i] = std::min(units_.max_hp[i], units_.hp[i] + 30.0f);
					units_.attack_damage[i] *= 1.15f;
				}
			}
		}
	}

	// 2. Defensive Buildings attack (TC / Tower / Fortress + Garrison volley bonus)
	for (auto &b : buildings_) {
		if (b.build_progress < 1.0f) continue;
		const BuildingTypeInfo &binfo = building_type_info(b.type);
		if (binfo.attack_damage > 0) {
			b.attack_timer -= dt;
			if (b.attack_timer <= 0.0f && n) {
				float range = binfo.attack_range;
				float range_sq = range * range;
				int32_t hit = -1;
				grid.visit_radius(b.x, b.z, range, [&](int32_t i) {
					if (hit >= 0 || !units_.alive[i] || units_.team[i] == b.team) return;
					float dx = b.x - units_.pos_x[i];
					float dz = b.z - units_.pos_z[i];
					if (dx * dx + dz * dz <= range_sq) hit = i;
				});
				if (hit >= 0) {
					b.attack_timer = binfo.attack_cooldown;
					int arrow_count = 1 + (int)b.garrisoned_units.size();
					for (int a = 0; a < std::min(4, arrow_count); ++a) {
						float spread = (a - arrow_count * 0.5f) * 0.2f;
						fire_arrow(b.x + spread, 3.5f, b.z + spread,
								   units_.pos_x[hit], 0.8f, units_.pos_z[hit],
								   binfo.attack_damage, b.team, units_.id[hit]);
					}
				}
			}
		}
	}

	// 3. Temple Healing Aura (Heal.js)
	for (const auto &b : buildings_) {
		if (b.build_progress < 1.0f || b.type != BLD_TEMPLE) continue;
		for (size_t u = 0; u < n; ++u) {
			if (!units_.alive[u] || units_.team[u] != b.team) continue;
			float dx = units_.pos_x[u] - b.x;
			float dz = units_.pos_z[u] - b.z;
			if (dx * dx + dz * dz <= 18.0f * 18.0f && units_.hp[u] < units_.max_hp[u]) {
				units_.hp[u] = std::min(units_.max_hp[u], units_.hp[u] + dt * 3.5f);
			}
		}
	}

	// 4. Wonder.js - Wonder Victory Countdown
	bool found_wonder = false;
	for (const auto &b : buildings_) {
		if (b.team == 0 && b.type == BLD_WONDER && b.build_progress >= 1.0f) {
			found_wonder = true;
			break;
		}
	}
	has_active_wonder_ = found_wonder;
	if (has_active_wonder_) {
		wonder_victory_timer_ += dt;
		if (wonder_victory_timer_ >= 600.0f) {
			victory_ = true;
			game_over_ = true;
		}
	} else {
		wonder_victory_timer_ = 0.0f;
	}

	// 5. Treasure.js - Wilderness Relic Collection
	for (auto &rel : relics_) {
		if (rel.claimed) continue;
		for (size_t u = 0; u < n; ++u) {
			if (!units_.alive[u] || units_.team[u] != 0) continue;
			float dx = units_.pos_x[u] - rel.x;
			float dz = units_.pos_z[u] - rel.z;
			if (dx * dx + dz * dz <= 2.5f * 2.5f) {
				rel.claimed = true;
				if (rel.res_type == RES_GOLD) gold_ += rel.amount;
				else if (rel.res_type == RES_WOOD) wood_ += rel.amount;
				else if (rel.res_type == RES_FOOD) food_ += rel.amount;
				else if (rel.res_type == RES_STONE) stone_ += rel.amount;
				break;
			}
		}
	}

	// 6. Hero Unit Inspiration Auras (Auras.js / Heroes)
	for (size_t h = 0; h < n; ++h) {
		if (!units_.alive[h] || units_.unit_type[h] != UNIT_HERO) continue;
		const uint8_t h_team = units_.team[h];
		const float hx = units_.pos_x[h];
		const float hz = units_.pos_z[h];
		for (size_t u = 0; u < n; ++u) {
			if (!units_.alive[u] || units_.team[u] != h_team || u == h) continue;
			float dx = units_.pos_x[u] - hx;
			float dz = units_.pos_z[u] - hz;
			if (dx * dx + dz * dz <= 16.0f * 16.0f) {
				if (player_civ_[0] == CIV_SPARTANS) {
					units_.hp[u] = std::min(units_.max_hp[u], units_.hp[u] + dt * 2.0f);
				} else if (player_civ_[0] == CIV_ATHENIANS) {
					units_.speed[u] = unit_type_info(units_.unit_type[u]).speed * 1.20f;
				}
			}
		}
	}

	// 7. Structure Capture Mechanics (Capturable.js)
	for (auto &b : buildings_) {
		if (b.build_progress < 1.0f || b.team == 0) continue;
		int friendly_count = 0;
		int enemy_count = 0;
		for (size_t u = 0; u < n; ++u) {
			if (!units_.alive[u]) continue;
			float dx = units_.pos_x[u] - b.x;
			float dz = units_.pos_z[u] - b.z;
			if (dx * dx + dz * dz <= 10.0f * 10.0f) {
				if (units_.team[u] == 0) friendly_count++;
				else if (units_.team[u] == b.team) enemy_count++;
			}
		}
		if (friendly_count > 0 && enemy_count == 0) {
			b.capture_points += dt * (float)friendly_count * 25.0f;
			if (b.capture_points >= b.max_capture_points) {
				b.team = 0; // Transferred to player!
				b.capture_points = 0.0f;
			}
		} else if (friendly_count == 0 && b.capture_points > 0.0f) {
			b.capture_points = std::max(0.0f, b.capture_points - dt * 15.0f);
		}
	}

	// 8. Remove dead units after death timer
	std::vector<size_t> dead_indices;
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] && units_.action_state[i] == ACTION_DYING && units_.death_timer[i] >= 1.0f) {
			dead_indices.push_back(i);
		}
	}
	if (!dead_indices.empty()) {
		std::sort(dead_indices.rbegin(), dead_indices.rend());
		for (size_t idx : dead_indices) {
			uint32_t dead_id = units_.id[idx];
			unit_id_to_idx_.erase(dead_id);
			if (dead_id < unit_idx_by_id_.size()) unit_idx_by_id_[dead_id] = -1;
			units_.remove_at(idx);
			if (idx < units_.count) {
				bind_unit_index(units_.id[idx], idx);
			}
		}
	}

	// 4. Check base destruction
	bool player_tc_alive = false;
	for (const auto &b : buildings_) {
		if (b.type == BLD_TOWN_CENTER && b.team == 0 && b.hp > 0) {
			player_tc_alive = true;
			break;
		}
	}
	if (!player_tc_alive) game_over_ = true;
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
					if (units_.hp[tidx] <= 0) {
						units_.alive[tidx] = 0;
						units_.action_state[tidx] = ACTION_DYING;
						if (units_.team[tidx] == 0) record_.units_lost++;
						else record_.enemies_killed++;
					}
				}
			}
		}

		p.current_x = p.start_x + (p.target_x - p.start_x) * p.progress;
		p.current_z = p.start_z + (p.target_z - p.start_z) * p.progress;
		float h_linear = p.start_y + (p.target_y - p.start_y) * p.progress;
		float h_arc = 4.0f * p.arc_height * p.progress * (1.0f - p.progress);
		p.current_y = h_linear + h_arc;
	}

	projectiles_.erase(std::remove_if(projectiles_.begin(), projectiles_.end(),
		[](const Projectile &p) { return !p.alive; }), projectiles_.end());
}

void Simulation::tick_waves(float dt) {
	wave_timer_ += dt;
	if (wave_timer_ < 35.0f) return;
	wave_timer_ = 0.0f;
	wave_number_++;

	if (wave_number_ > 5) {
		victory_ = true;
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
	return {
		units_.pos_x, units_.pos_z,
		units_.vel_x, units_.vel_z,
		units_.target_x, units_.target_z,
		units_.hp, units_.max_hp,
		units_.id,
		units_.alive, units_.team, units_.unit_type, units_.action_state, units_.res_type_carried,
		units_.stance, units_.formation,
		units_.carry,
		units_.death_timer,
		units_.kills,
		units_.rank,
		units_.count
	};
}

Simulation::ResourceView Simulation::resources() const {
	return {
		resources_.x, resources_.z,
		resources_.amount,
		resources_.type, resources_.alive,
		resources_.id,
		resources_.count
	};
}

void Simulation::tick_trickle(float dt) {
	// ResourceTrickle.js — passive income from key buildings
	// Town Centers: +1 food/s, Farmstead: +0.5 food/s per field
	// Market: +0.3 gold/s (simulates trade trickle), Wonder: +2 of all/s
	for (const auto &b : buildings_) {
		if (b.team != 0 || b.build_progress < 1.0f) continue;
		switch (b.type) {
			case BLD_TOWN_CENTER:
				food_ = std::min(9999, food_ + (int)(1.0f * dt));
				break;
			case BLD_FARMSTEAD:
				food_ = std::min(9999, food_ + (int)(0.5f * dt));
				break;
			case BLD_MARKET:
				gold_ = std::min(9999, gold_ + (int)(0.3f * dt));
				break;
			case BLD_WONDER:
				food_  = std::min(9999, food_  + (int)(2.0f * dt));
				wood_  = std::min(9999, wood_  + (int)(2.0f * dt));
				gold_  = std::min(9999, gold_  + (int)(2.0f * dt));
				stone_ = std::min(9999, stone_ + (int)(2.0f * dt));
				break;
			default: break;
		}
	}
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

void Simulation::tick_petra_ai(float dt) {
	// Petra.js — Autonomous enemy AI macro strategy
	petra_timer_ += dt;

	// Count enemy units/buildings
	int enemy_villages = 0, enemy_military = 0, enemy_tc = 0, enemy_barracks = 0;
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != 1) continue;
		if (units_.unit_type[i] == UNIT_VILLAGER) enemy_villages++;
		else enemy_military++;
	}
	for (const auto &b : buildings_) {
		if (b.team != 1 || b.build_progress < 1.0f) continue;
		if (b.type == BLD_TOWN_CENTER) enemy_tc++;
		if (b.type == BLD_BARRACKS)    enemy_barracks++;
	}
	(void)enemy_villages; (void)enemy_military;
	(void)enemy_tc; (void)enemy_barracks;

	// AI economic trickle (Petra manages its own virtual economy)
	ai_food_  += 8.0f * dt;
	ai_wood_  += 6.0f * dt;
	ai_gold_  += 4.0f * dt;
	ai_stone_ += 2.0f * dt;
	ai_food_  = std::min(ai_food_,  9999.0f);
	ai_wood_  = std::min(ai_wood_,  9999.0f);
	ai_gold_  = std::min(ai_gold_,  9999.0f);
	ai_stone_ = std::min(ai_stone_, 9999.0f);

	// Update AI phase based on wave number
	if (wave_number_ >= 5)       petra_phase_ = 3;
	else if (wave_number_ >= 3)  petra_phase_ = 2;

	// Petra training cadence — spawn military units every 18-25s based on phase
	float training_interval = (petra_phase_ == 1) ? 22.0f : (petra_phase_ == 2) ? 18.0f : 14.0f;

	if (petra_timer_ >= training_interval) {
		petra_timer_ = 0.0f;

		// Find an enemy Town Center or Barracks to spawn from
		float spawn_x = 28.0f, spawn_z = 28.0f; // default far corner
		for (const auto &b : buildings_) {
			if (b.team == 1 && b.build_progress >= 1.0f &&
				(b.type == BLD_TOWN_CENTER || b.type == BLD_BARRACKS || b.type == BLD_FORTRESS)) {
				spawn_x = b.x + 2.0f + (float)(rand() % 4 - 2);
				spawn_z = b.z + 2.0f + (float)(rand() % 4 - 2);
				break;
			}
		}
		// Clamp to map bounds
		spawn_x = std::clamp(spawn_x, -map_half_ + 2.0f, map_half_ - 2.0f);
		spawn_z = std::clamp(spawn_z, -map_half_ + 2.0f, map_half_ - 2.0f);

		// Train a unit proportional to phase
		UnitType ai_unit = UNIT_SWORDSMAN;
		int roll = rand() % 10;
		if (petra_phase_ == 1) {
			ai_unit = (roll < 4) ? UNIT_SWORDSMAN : (roll < 7) ? UNIT_SPEARMAN : UNIT_ARCHER;
		} else if (petra_phase_ == 2) {
			ai_unit = (roll < 3) ? UNIT_CAVALRY : (roll < 6) ? UNIT_SWORDSMAN : UNIT_ARCHER;
		} else {
			ai_unit = (roll < 2) ? UNIT_CATAPULT : (roll < 4) ? UNIT_CAVALRY :
			          (roll < 6) ? UNIT_ELEPHANT : UNIT_SWORDSMAN;
		}

		const UnitTypeInfo &uinfo = unit_type_info(ai_unit);
		// Deduct from AI economy
		if (ai_food_ >= uinfo.food_cost && ai_wood_ >= uinfo.wood_cost &&
			ai_gold_ >= uinfo.gold_cost) {
			ai_food_ -= uinfo.food_cost;
			ai_wood_ -= uinfo.wood_cost;
			ai_gold_ -= uinfo.gold_cost;
			uint32_t uid = spawn_unit(spawn_x, spawn_z, 1, ai_unit, uinfo.hp);

			// Give new unit an attack-move order towards the player base
			float tx = 0.0f + (float)(rand() % 10 - 5);
			float tz = 0.0f + (float)(rand() % 10 - 5);
			uint32_t arr[1] = {uid};
			order_attack_move(arr, 1, tx, tz);
		}
	}
}

bool Simulation::save_state(const char *filepath) const {
	std::ofstream out(filepath, std::ios::binary);
	if (!out.is_open()) return false;

	uint32_t magic = 0x52545330; // 'RTS0'
	uint32_t version = 1;
	out.write((const char *)&magic, sizeof(magic));
	out.write((const char *)&version, sizeof(version));

	// 1. Sim scalars
	out.write((const char *)&ticks_, sizeof(ticks_));
	out.write((const char *)&player_phase_, sizeof(player_phase_));
	out.write((const char *)&player_civ_, sizeof(player_civ_));
	out.write((const char *)&biome_, sizeof(biome_));
	out.write((const char *)&map_preset_, sizeof(map_preset_));
	out.write((const char *)&food_, sizeof(food_));
	out.write((const char *)&wood_, sizeof(wood_));
	out.write((const char *)&gold_, sizeof(gold_));
	out.write((const char *)&stone_, sizeof(stone_));
	out.write((const char *)&researched_techs_mask_, sizeof(researched_techs_mask_));
	out.write((const char *)&wave_number_, sizeof(wave_number_));
	out.write((const char *)&wave_timer_, sizeof(wave_timer_));
	out.write((const char *)&wonder_victory_timer_, sizeof(wonder_victory_timer_));
	out.write((const char *)&has_active_wonder_, sizeof(has_active_wonder_));
	out.write((const char *)&game_over_, sizeof(game_over_));
	out.write((const char *)&victory_, sizeof(victory_));

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

		uint32_t q_sz = (uint32_t)b.queue.size();
		out.write((const char *)&q_sz, sizeof(q_sz));
		for (const auto &po : b.queue) {
			out.write((const char *)&po.unit_type, sizeof(po.unit_type));
			out.write((const char *)&po.timer, sizeof(po.timer));
			out.write((const char *)&po.duration, sizeof(po.duration));
		}

		uint32_t g_sz = (uint32_t)b.garrisoned_units.size();
		out.write((const char *)&g_sz, sizeof(g_sz));
		for (uint32_t gid : b.garrisoned_units) {
			out.write((const char *)&gid, sizeof(gid));
		}
	}

	return true;
}

bool Simulation::load_state(const char *filepath) {
	std::ifstream in(filepath, std::ios::binary);
	if (!in.is_open()) return false;

	uint32_t magic = 0, version = 0;
	in.read((char *)&magic, sizeof(magic));
	in.read((char *)&version, sizeof(version));
	if (magic != 0x52545330 || version != 1) return false;

	// 1. Sim scalars
	in.read((char *)&ticks_, sizeof(ticks_));
	in.read((char *)&player_phase_, sizeof(player_phase_));
	in.read((char *)&player_civ_, sizeof(player_civ_));
	in.read((char *)&biome_, sizeof(biome_));
	in.read((char *)&map_preset_, sizeof(map_preset_));
	in.read((char *)&food_, sizeof(food_));
	in.read((char *)&wood_, sizeof(wood_));
	in.read((char *)&gold_, sizeof(gold_));
	in.read((char *)&stone_, sizeof(stone_));
	in.read((char *)&researched_techs_mask_, sizeof(researched_techs_mask_));
	in.read((char *)&wave_number_, sizeof(wave_number_));
	in.read((char *)&wave_timer_, sizeof(wave_timer_));
	in.read((char *)&wonder_victory_timer_, sizeof(wonder_victory_timer_));
	in.read((char *)&has_active_wonder_, sizeof(has_active_wonder_));
	in.read((char *)&game_over_, sizeof(game_over_));
	in.read((char *)&victory_, sizeof(victory_));

	// 2. Resources
	uint32_t num_res = 0;
	in.read((char *)&num_res, sizeof(num_res));
	resources_.count = 0;
	res_id_to_idx_.clear();
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

		uint32_t q_sz = 0;
		in.read((char *)&q_sz, sizeof(q_sz));
		bld.queue.resize(q_sz);
		for (uint32_t q = 0; q < q_sz; ++q) {
			in.read((char *)&bld.queue[q].unit_type, sizeof(bld.queue[q].unit_type));
			in.read((char *)&bld.queue[q].timer, sizeof(bld.queue[q].timer));
			in.read((char *)&bld.queue[q].duration, sizeof(bld.queue[q].duration));
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

	return true;
}

void Simulation::tick_fow() {
	// Step 1: Reset all VISIBLE tiles back to EXPLORED (preserve explored history)
	for (int i = 0; i < FOW_GRID * FOW_GRID; ++i) {
		if (fow_grid_[i] == FOW_VISIBLE) fow_grid_[i] = FOW_EXPLORED;
	}

	// Step 2: For every player unit, illuminate tiles within its vision radius
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

	const float half = map_half_;
	const float cell = (half * 2.0f) / (float)FOW_GRID;
	const int G = FOW_GRID;

	for (size_t u = 0; u < units_.count; ++u) {
		if (!units_.alive[u] || units_.team[u] != 0) continue;
		float vr = vision_radius(units_.unit_type[u]);
		float ux = units_.pos_x[u];
		float uz = units_.pos_z[u];

		// Convert world pos to grid coords
		float norm_x = (ux + half) / (2.0f * half); // [0,1]
		float norm_z = (uz + half) / (2.0f * half);
		int cx = (int)(norm_x * G);
		int cz = (int)(norm_z * G);
		int r = (int)(vr / cell) + 1;

		for (int dz = -r; dz <= r; ++dz) {
			for (int dx = -r; dx <= r; ++dx) {
				int gx = cx + dx;
				int gz = cz + dz;
				if (gx < 0 || gx >= G || gz < 0 || gz >= G) continue;
				// World position of grid cell center
				float wx = (gx + 0.5f) * cell - half;
				float wz = (gz + 0.5f) * cell - half;
				float ddx = wx - ux;
				float ddz = wz - uz;
				if (ddx * ddx + ddz * ddz <= vr * vr) {
					fow_grid_[gz * G + gx] = FOW_VISIBLE;
				}
			}
		}
	}

	// Step 3: Player buildings also reveal their surroundings
	for (const auto& b : buildings_) {
		if (b.team != 0 || b.build_progress < 1.0f) continue;
		float bvr = 18.0f; // default building vision
		if (b.type == BLD_TOWER || b.type == BLD_FORTRESS) bvr = 24.0f;
		if (b.type == BLD_DOCK) bvr = 28.0f;
		float norm_x = (b.x + half) / (2.0f * half);
		float norm_z = (b.z + half) / (2.0f * half);
		int cx = (int)(norm_x * G);
		int cz = (int)(norm_z * G);
		int r = (int)(bvr / cell) + 1;
		for (int dz = -r; dz <= r; ++dz) {
			for (int dx = -r; dx <= r; ++dx) {
				int gx = cx + dx;
				int gz = cz + dz;
				if (gx < 0 || gx >= G || gz < 0 || gz >= G) continue;
				float wx = (gx + 0.5f) * cell - half;
				float wz = (gz + 0.5f) * cell - half;
				float ddx = wx - b.x;
				float ddz = wz - b.z;
				if (ddx * ddx + ddz * ddz <= bvr * bvr) {
					fow_grid_[gz * G + gx] = FOW_VISIBLE;
				}
			}
		}
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
	fauna_.rot_y[idx] = (float)(rand() % 360) * 0.0174533f;
	fauna_.hp[idx] = info.max_hp;
	fauna_.max_hp[idx] = info.max_hp;
	fauna_.food_amount[idx] = info.food_amount;
	fauna_.fauna_type[idx] = ftype;
	fauna_.behavior[idx] = FAUNA_IDLE_GRAZE;
	fauna_.alive[idx] = 1;
	fauna_.state_timer[idx] = 2.0f + (float)(rand() % 4);
	fauna_.attack_timer[idx] = 0.0f;
	fauna_.target_unit_id[idx] = 0;
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

		// Dead check -> Carcass conversion
		if (fauna_.hp[i] <= 0.0f) {
			if (fauna_.behavior[i] != FAUNA_CARCASS) {
				fauna_.behavior[i] = FAUNA_CARCASS;
				// Spawn rich food resource node where animal fell
				spawn_resource_world(fauna_.pos_x[i], fauna_.pos_z[i], RES_FOOD, fauna_.food_amount[i]);
			}
			fauna_.alive[i] = 0;
			continue;
		}

		fauna_.state_timer[i] -= dt;
		if (fauna_.attack_timer[i] > 0.0f) fauna_.attack_timer[i] -= dt;

		float fx = fauna_.pos_x[i];
		float fz = fauna_.pos_z[i];

		// Scan for nearest player unit within agro/flee range
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
				fauna_.state_timer[i] = 3.0f + (float)(rand() % 5);
				// Random slow wander
				if (rand() % 2 == 0) {
					float angle = (float)(rand() % 360) * 0.0174533f;
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
				fauna_.state_timer[i] = 3.0f + (float)(rand() % 5);
				if (rand() % 2 == 0) {
					float angle = (float)(rand() % 360) * 0.0174533f;
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

} // namespace rtso
