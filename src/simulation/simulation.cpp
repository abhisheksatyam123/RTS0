#include "simulation.h"
#include "rms_generator.h"
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
	transport_passengers_.clear();

	// Reset Petra AI State
	petra_timer_ = 0.0f;
	petra_econ_timer_ = 0.0f;
	petra_phase_ = 1;
	ai_food_ = 400.0f;
	ai_wood_ = 600.0f;
	ai_gold_ = 300.0f;
	ai_stone_ = 200.0f;
	ai_researched_techs_mask_ = 0;
	memset(petra_threat_map_.danger, 0, sizeof(petra_threat_map_.danger));
	memset(petra_threat_map_.enemy_threat, 0, sizeof(petra_threat_map_.enemy_threat));
	memset(petra_threat_map_.friendly_defense, 0, sizeof(petra_threat_map_.friendly_defense));
	petra_threat_map_.update_timer = 0.0f;
	petra_strike_force_ = PetraStrikeForce();
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

int Simulation::ai_pop_current() const {
	int count = 0;
	for (size_t i = 0; i < units_.count; ++i) {
		if (units_.team[i] == 1 && (units_.alive[i] || units_.action_state[i] == ACTION_GARRISONED)) count++;
	}
	return count;
}

int Simulation::ai_pop_max() const {
	int cap = 10;
	for (const auto &b : buildings_) {
		if (b.team == 1 && b.build_progress >= 1.0f) {
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

	// 7. Enemy Base (Team 1) — Spartan Base for Petra Autonomous AI
	spawn_building(18, 18, BLD_TOWN_CENTER, 1, 1.0f);
	spawn_building(15, 17, BLD_BARRACKS, 1, 1.0f);
	spawn_building(21, 19, BLD_HOUSE, 1, 1.0f);
	spawn_building(19, 21, BLD_HOUSE, 1, 1.0f);
	spawn_building(17, 21, BLD_STOREHOUSE, 1, 1.0f);

	for (int i = 0; i < 6; ++i) {
		float a = 6.28318530718f * i / 6.0f;
		float x = 37.0f + cosf(a) * 4.0f;
		float z = 37.0f + sinf(a) * 4.0f;
		spawn_unit(x, z, 1, UNIT_VILLAGER, 100.0f);
	}
	spawn_unit(32.0f, 32.0f, 1, UNIT_SWORDSMAN, 160.0f);
	spawn_unit(34.0f, 30.0f, 1, UNIT_SPEARMAN, 160.0f);
	spawn_unit(30.0f, 34.0f, 1, UNIT_ARCHER, 90.0f);
	spawn_unit(28.0f, 36.0f, 1, UNIT_ARCHER, 90.0f);

	// Dedicated economic resources near AI base
	spawn_resource(21, 21, RES_WOOD, 1200);
	spawn_resource(22, 21, RES_WOOD, 1200);
	spawn_resource(21, 16, RES_GOLD, 1200);
	spawn_resource(16, 22, RES_STONE, 1200);
	spawn_resource(15, 15, RES_FOOD, 1000);
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
	const UnitTypeInfo &uinfo = unit_type_info(u_type);
	if (hp > 0.0f) {
		units_.hp[idx] = units_.max_hp[idx] = hp;
	} else {
		units_.hp[idx] = units_.max_hp[idx] = uinfo.hp;
	}

	// Apply researched military tech bonuses & Civilization traits
	uint32_t tech_mask = (team == 0) ? researched_techs_mask_ : ai_researched_techs_mask_;
	Civilization civ = (team < 4) ? player_civ_[team] : CIV_ATHENIANS;
	const CivInfo &cinfo = civ_info(civ);

	units_.max_hp[idx] *= cinfo.hp_mult;
	units_.hp[idx] = units_.max_hp[idx];

	if ((tech_mask & (1u << TECH_WEAPONS_1)) != 0) units_.attack_damage[idx] *= 1.20f;
	if ((tech_mask & (1u << TECH_ARMOR_1)) != 0) {
		units_.max_hp[idx] += 30.0f;
		units_.hp[idx] = units_.max_hp[idx];
	}
	// Civ bonuses
	if (civ == CIV_SPARTANS && (u_type == UNIT_SWORDSMAN || u_type == UNIT_SPEARMAN)) {
		units_.max_hp[idx] *= 1.25f;
		units_.hp[idx] = units_.max_hp[idx];
		units_.attack_damage[idx] *= 1.15f;
	} else if (civ == CIV_PERSIANS && (u_type == UNIT_CAVALRY || u_type == UNIT_CAMEL)) {
		units_.speed[idx] *= 1.20f;
	} else if (civ == CIV_BRITONS && (u_type == UNIT_CHARIOT || u_type == UNIT_WAR_DOG)) {
		units_.speed[idx] *= 1.15f;
	}

	if (team == 0) {
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
	const auto &minfo = map_type_info(type);
	if (std::strcmp(minfo.biome, "alpine") == 0) cfg.biome = BIOME_ALPINE;
	else if (std::strcmp(minfo.biome, "oasis") == 0) cfg.biome = BIOME_OASIS;
	else if (std::strcmp(minfo.biome, "latium") == 0) cfg.biome = BIOME_LATIUM;
	else cfg.biome = BIOME_AEGEAN;

	RmsGenerator::generate(*this, cfg);
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

	if (b.type == BLD_CORRAL || u_type == UNIT_TYPE_NONE) {
		return cmd_breed_livestock(building_id);
	}

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
	if (utype == UNIT_TYPE_NONE) {
		food_ += 60;
		wood_ += 30;
		b.queue.erase(b.queue.begin() + queue_idx);
		return true;
	}

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
		units_.anchor_x[idx] = tx + ox;
		units_.anchor_z[idx] = tz + oz;
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
			units_.anchor_x[idx] = units_.pos_x[idx];
			units_.anchor_z[idx] = units_.pos_z[idx];
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
		if (uidx < 0 || !units_.alive[uidx]) continue;

		units_.target_x[uidx] = rx;
		units_.target_z[uidx] = rz;
		units_.anchor_x[uidx] = rx;
		units_.anchor_z[uidx] = rz;
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
		if (uidx < 0 || !units_.alive[uidx]) continue;

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

		units_.target_x[uidx] = b.x;
		units_.target_z[uidx] = b.z;
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
		if (uidx < 0 || units_.unit_type[uidx] != UNIT_VILLAGER || !units_.alive[uidx]) continue;

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

bool Simulation::cmd_breed_livestock(uint32_t building_id) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return false;
	Building &b = buildings_[bit->second];
	if (b.team != 0 || b.build_progress < 1.0f || b.type != BLD_CORRAL) return false;
	if (b.queue.size() >= 8) return false;
	if (food_ < 60 || wood_ < 30) return false;

	food_ -= 60;
	wood_ -= 30;
	b.queue.push_back({(uint8_t)UNIT_TYPE_NONE, 0.0f, 15.0f});
	return true;
}

bool Simulation::is_transport(uint32_t unit_id) const {
	int32_t idx = lookup_unit(unit_id);
	if (idx < 0 || !units_.alive[idx]) return false;
	return units_.unit_type[idx] == UNIT_TRIREME;
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
	if (tidx < 0 || !units_.alive[tidx] || units_.unit_type[tidx] != UNIT_TRIREME) return;
	float tx = units_.pos_x[tidx];
	float tz = units_.pos_z[tidx];
	uint8_t team = units_.team[tidx];

	auto &passengers = transport_passengers_[transport_id];

	for (size_t i = 0; i < count; ++i) {
		if ((int)passengers.size() >= TRANSPORT_CAPACITY) break;
		int32_t uidx = lookup_unit(unit_ids[i]);
		if (uidx < 0 || !units_.alive[uidx] || units_.team[uidx] != team) continue;
		if (units_.unit_type[uidx] == UNIT_TRIREME) continue;

		float dx = units_.pos_x[uidx] - tx;
		float dz = units_.pos_z[uidx] - tz;
		float d2 = dx * dx + dz * dz;

		if (d2 <= 4.5f * 4.5f) {
			units_.action_state[uidx] = ACTION_GARRISONED;
			units_.alive[uidx] = 0;
			units_.has_order[uidx] = 0;
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
	if (tidx < 0 || units_.unit_type[tidx] != UNIT_TRIREME) return;

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
		}
		pit->second.erase(it);
	}
}

void Simulation::fire_arrow(float sx, float sy, float sz, float tx, float ty, float tz, float damage, uint8_t team, uint32_t target_id, uint8_t effect, uint8_t dmg_type) {
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
	p.damage_type = dmg_type;
	p.status_effect = effect;
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
	tick_status_effects(dt);
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
			if (units_.status_effects[i] & EFFECT_KNOCKBACK) {
				npx[i] = std::clamp(px[i] + units_.knockback_dir_x[i] * 14.0f * dt, -map_half_, map_half_);
				npz[i] = std::clamp(pz[i] + units_.knockback_dir_z[i] * 14.0f * dt, -map_half_, map_half_);
			}
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
		if (units_.status_effects[i] & EFFECT_SNARE) {
			spd *= 0.65f; // Snare 35% speed penalty (0 A.D. cavalry snare)
		}
		float move_dx = ux * spd * dt;
		float move_dz = uz * spd * dt;
		if (units_.status_effects[i] & EFFECT_KNOCKBACK) {
			move_dx += units_.knockback_dir_x[i] * 14.0f * dt;
			move_dz += units_.knockback_dir_z[i] * 14.0f * dt;
		}
		npx[i] = std::clamp(px[i] + move_dx, -map_half_, map_half_);
		npz[i] = std::clamp(pz[i] + move_dz, -map_half_, map_half_);
		vx[i] = ux * spd;
		vz[i] = uz * spd;
	});

	memcpy(px, npx, n * sizeof(float));
	memcpy(pz, npz, n * sizeof(float));

	// Naval Transport Boarding & Moving Passenger Position Synchronization
	for (size_t i = 0; i < n; ++i) {
		if (!units_.alive[i] || units_.unit_type[i] == UNIT_TRIREME) continue;
		if (units_.action_state[i] == ACTION_MOVE && units_.target_unit_id[i] != 0) {
			int32_t tidx = lookup_unit(units_.target_unit_id[i]);
			if (tidx >= 0 && units_.alive[tidx] && units_.unit_type[tidx] == UNIT_TRIREME && units_.team[tidx] == units_.team[i]) {
				float dx = px[i] - px[tidx];
				float dz = pz[i] - pz[tidx];
				if (dx * dx + dz * dz <= 4.5f * 4.5f) {
					auto &passengers = transport_passengers_[units_.id[tidx]];
					if (passengers.size() < TRANSPORT_CAPACITY) {
						units_.action_state[i] = ACTION_GARRISONED;
						units_.alive[i] = 0;
						units_.has_order[i] = 0;
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
		bool is_gatherer = (units_.unit_type[i] == UNIT_VILLAGER ||
		                    units_.unit_type[i] == UNIT_FEMALE_CITIZEN ||
		                    units_.unit_type[i] == UNIT_WORKER_ELEPHANT ||
		                    units_.unit_type[i] == UNIT_FISHING_BOAT);
		if (!units_.alive[i] || !is_gatherer) continue;
		uint8_t uteam = units_.team[i];

		// 1. Returning to dropsite
		if (units_.carry[i] >= units_.carry_capacity[i] || units_.action_state[i] == ACTION_RETURN_DROPSITE) {
			float best_d2 = 1e9f;
			float drop_x = 0, drop_z = 0;
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
					if (uteam == 0) {
						if (units_.res_type_carried[i] == RES_WOOD) { wood_ += amt; wood_gathered_window_ += amt; record_.total_wood_gathered += amt; }
						else if (units_.res_type_carried[i] == RES_GOLD) { gold_ += amt; gold_gathered_window_ += amt; record_.total_gold_gathered += amt; }
						else if (units_.res_type_carried[i] == RES_STONE) { stone_ += amt; stone_gathered_window_ += amt; record_.total_stone_gathered += amt; }
						else if (units_.res_type_carried[i] == RES_FOOD) { food_ += amt; food_gathered_window_ += amt; record_.total_food_gathered += amt; }
					} else {
						if (units_.res_type_carried[i] == RES_WOOD) ai_wood_ = std::min(9999.0f, ai_wood_ + amt);
						else if (units_.res_type_carried[i] == RES_GOLD) ai_gold_ = std::min(9999.0f, ai_gold_ + amt);
						else if (units_.res_type_carried[i] == RES_STONE) ai_stone_ = std::min(9999.0f, ai_stone_ + amt);
						else if (units_.res_type_carried[i] == RES_FOOD) ai_food_ = std::min(9999.0f, ai_food_ + amt);
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
			float dx = units_.pos_x[i] - resources_.x[ridx];
			float dz = units_.pos_z[i] - resources_.z[ridx];
			if (dx * dx + dz * dz < 2.5f * 2.5f) {
				units_.attack_timer[i] += dt;
				float harvest_rate = 0.75f;
				if (uteam == 0) {
					if (is_tech_researched(TECH_FARMING_1) && resources_.type[ridx] == RES_FOOD) harvest_rate = 0.55f;
					if (is_tech_researched(TECH_WOODCUTTING_1) && resources_.type[ridx] == RES_WOOD) harvest_rate = 0.55f;
					if (is_tech_researched(TECH_MINING_1) && (resources_.type[ridx] == RES_GOLD || resources_.type[ridx] == RES_STONE)) harvest_rate = 0.55f;

					Civilization civ = (uteam < 4) ? player_civ_[uteam] : CIV_ATHENIANS;
					if ((resources_.type[ridx] == RES_GOLD || resources_.type[ridx] == RES_STONE) && civ_info(civ).metal_gather_mult > 1.0f) {
						harvest_rate /= civ_info(civ).metal_gather_mult;
					}
				} else {
					if ((ai_researched_techs_mask_ & (1u << TECH_FARMING_1)) && resources_.type[ridx] == RES_FOOD) harvest_rate = 0.55f;
					if ((ai_researched_techs_mask_ & (1u << TECH_WOODCUTTING_1)) && resources_.type[ridx] == RES_WOOD) harvest_rate = 0.55f;
					if ((ai_researched_techs_mask_ & (1u << TECH_MINING_1)) && (resources_.type[ridx] == RES_GOLD || resources_.type[ridx] == RES_STONE)) harvest_rate = 0.55f;
				}

				// Female Citizen gathering bonus (+25% Food gather speed)
				if (units_.unit_type[i] == UNIT_FEMALE_CITIZEN && resources_.type[ridx] == RES_FOOD) {
					harvest_rate *= 0.75f;
				}

				// Female Citizen Economic Inspiration Aura: +15% gathering rate for male villagers within 10m
				if (units_.unit_type[i] == UNIT_VILLAGER) {
					bool has_female_aura = false;
					for (size_t fc = 0; fc < n; ++fc) {
						if (!units_.alive[fc] || units_.team[fc] != uteam || units_.unit_type[fc] != UNIT_FEMALE_CITIZEN) continue;
						float fdx = units_.pos_x[i] - units_.pos_x[fc];
						float fdz = units_.pos_z[i] - units_.pos_z[fc];
						if (fdx * fdx + fdz * fdz <= 10.0f * 10.0f) {
							has_female_aura = true;
							break;
						}
					}
					if (has_female_aura) {
						harvest_rate *= 0.85f;
					}
				}

				// Worker Elephant: +50% heavy harvesting rate for all resources
				if (units_.unit_type[i] == UNIT_WORKER_ELEPHANT) {
					harvest_rate *= 0.65f;
				}

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
			if (units_.unit_type[i] == UNIT_WORKER_ELEPHANT) speed_mult *= 2.0f; // Worker elephant heavy construction
			Civilization civ = (units_.team[i] < 4) ? player_civ_[units_.team[i]] : CIV_ATHENIANS;
			speed_mult *= civ_info(civ).build_speed_mult;

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
		if (b.build_progress < 1.0f) continue;
		uint8_t bteam = b.team;

		// 1. Tech Research
		if (b.research_tech != TECH_NONE) {
			b.research_timer += dt;
			if (b.research_timer >= b.research_duration) {
				if (bteam == 0) {
					researched_techs_mask_ |= (1u << b.research_tech);
					if (b.research_tech == TECH_PHASE_TOWN) player_phase_ = 2;
					else if (b.research_tech == TECH_PHASE_CITY) player_phase_ = 3;
				} else {
					ai_researched_techs_mask_ |= (1u << b.research_tech);
					if (b.research_tech == TECH_PHASE_TOWN) petra_phase_ = 2;
					else if (b.research_tech == TECH_PHASE_CITY) petra_phase_ = 3;
				}
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
					uint32_t uid = spawn_unit(sx, sz, bteam, (UnitType)utype);

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

inline bool in_parabolic_range(float d2, float att_y, float def_y, float base_range) {
	float height_diff = def_y - att_y; // target_y - attacker_y
	float max_r2 = base_range * base_range - 2.0f * base_range * height_diff;
	return (max_r2 > 0.0f) && (d2 <= max_r2);
}

void Simulation::tick_combat(float dt) {
	const size_t n = units_.count;
	Arena &arena = frame_arena_.current();
	int32_t *target_idx = n ? arena.alloc_array<int32_t>(n) : nullptr;
	if (n && !target_idx) return;

	UnitGrid grid;
	if (n && !grid.build(arena, n, units_.pos_x, units_.pos_z, units_.alive, map_half_)) return;

	// 1. Target Acquisition & Stance Evaluation (0 A.D. UnitAI.js)
	JobSystem::instance().for_each_index(n, [&](size_t i) {
		if (units_.attack_timer[i] > 0) units_.attack_timer[i] -= dt;
		target_idx[i] = -1;
		if (!units_.alive[i]) return;

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
			if (tidx >= 0 && units_.alive[tidx] && units_.team[tidx] != units_.team[i]) {
				found = tidx;
			}
		}

		// B. Stance Leash & Auto-Aggro Scan
		if (found < 0 && units_.target_building_id[i] == 0) {
			float scan_range = unit_type_info(units_.unit_type[i]).aggro_range;
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
					if (!units_.alive[j] || units_.team[j] == my_team) return;

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
		const auto &uinfo_att = unit_type_info(units_.unit_type[i]);
		const auto &uinfo_dst = unit_type_info(units_.unit_type[t]);
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
		if (units_.team[t] == 0 && is_tech_researched(TECH_ARMOR_1)) {
			target_hack += 3.0f;
			target_pierce += 3.0f;
		}

		// Iberian Devotio Bodyguard Vow: +2 armor
		if (player_civ_[units_.team[t] < 4 ? units_.team[t] : 0] == CIV_IBERIANS) {
			target_hack += 2.0f;
			target_pierce += 2.0f;
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
		Civilization att_civ = (att_team < 4) ? player_civ_[att_team] : CIV_ATHENIANS;
		float civ_melee_mult = civ_info(att_civ).melee_damage_mult;
		hack_dmg *= civ_melee_mult;

		// Counter Multipliers matching 0 A.D. template bonuses
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

		hack_dmg *= counter_mul;
		pierce_dmg *= counter_mul;
		crush_dmg *= counter_mul;

		// Carnyx Horn Morale Aura check (Gauls)
		bool carnyx_aura = false;
		grid.visit_radius(units_.pos_x[i], units_.pos_z[i], 14.0f, [&](int32_t c_idx) {
			if (carnyx_aura || !units_.alive[c_idx] || units_.team[c_idx] != att_team) return;
			if (units_.unit_type[c_idx] == UNIT_CARNYX) carnyx_aura = true;
		});
		if (carnyx_aura) {
			hack_dmg *= 1.20f;
			pierce_dmg *= 1.20f;
			crush_dmg *= 1.20f;
		}

		// 0 A.D. Exact Logarithmic Armor Reduction: Damage = Base * pow(0.9f, armor) with hack, pierce, crush
		float final_dmg = std::max(1.0f,
			hack_dmg * powf(0.9f, target_hack) +
			pierce_dmg * powf(0.9f, target_pierce) +
			crush_dmg * powf(0.9f, target_crush)
		);

		// Elevation damage advantage (RangeManager.cpp High Ground bonus)
		if (height_diff > 1.0f) {
			final_dmg *= (1.0f + std::min(0.25f, height_diff * 0.05f));
		} else if (height_diff < -1.0f) {
			final_dmg *= std::max(0.80f, 1.0f + height_diff * 0.04f); // Uphill attack penalty
		}

		bool is_ranged_attack = (u_att == UNIT_ARCHER || u_att == UNIT_ARCHER_MAIDEN ||
		                         u_att == UNIT_BOLT_SHOOTER || u_att == UNIT_CHARIOT ||
		                         u_att == UNIT_TRIREME);

		if (is_ranged_attack) {
			uint8_t effect = EFFECT_NONE;
			if (att_civ == CIV_KUSHITES || u_att == UNIT_ARCHER_MAIDEN) {
				effect |= EFFECT_POISON;
			}
			float arrow_sy = (u_att == UNIT_TRIREME) ? 2.2f : (u_att == UNIT_BOLT_SHOOTER ? 1.0f : 1.4f);
			// 0 A.D. Spread dispersion
			float dist = sqrtf(d2);
			float spread_factor = (dist / units_.attack_range[i]) * 0.25f;
			float disp_x = ((float)(rand() % 100) / 100.0f - 0.5f) * spread_factor;
			float disp_z = ((float)(rand() % 100) / 100.0f - 0.5f) * spread_factor;

			fire_arrow(units_.pos_x[i], arrow_sy, units_.pos_z[i],
			           units_.pos_x[t] + disp_x, 0.8f, units_.pos_z[t] + disp_z,
			           final_dmg, units_.team[i], units_.id[t], effect, 1);

			// If War Trireme has garrisoned passengers, fire deck archer barrage
			if (u_att == UNIT_TRIREME) {
				auto pit = transport_passengers_.find(units_.id[i]);
				if (pit != transport_passengers_.end() && !pit->second.empty()) {
					int extra = std::min(4, (int)pit->second.size());
					for (int a = 0; a < extra; ++a) {
						float sp = (a - extra * 0.5f) * 0.3f;
						fire_arrow(units_.pos_x[i] + sp, 2.5f, units_.pos_z[i] + sp,
								   units_.pos_x[t], 0.8f, units_.pos_z[t],
								   12.0f, units_.team[i], units_.id[t]);
					}
				}
			}
		} else {
			units_.hp[t] -= final_dmg;

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

				// Trireme destruction: disembark survivors near shore or drown
				if (units_.unit_type[t] == UNIT_TRIREME) {
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

	// 3. Units Attacking Buildings & Siege Demolition (0 A.D. Attack.js + BuildingDamage)
	for (size_t i = 0; i < n; ++i) {
		if (!units_.alive[i] || units_.target_building_id[i] == 0 || units_.attack_timer[i] > 0) continue;
		auto bit = building_id_to_idx_.find(units_.target_building_id[i]);
		if (bit == building_id_to_idx_.end()) {
			units_.target_building_id[i] = 0;
			units_.action_state[i] = ACTION_IDLE;
			continue;
		}
		Building &b = buildings_[bit->second];
		if (b.team == units_.team[i]) {
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
			units_.target_x[i] = b.x;
			units_.target_z[i] = b.z;
			units_.has_order[i] = 1;
			units_.action_state[i] = ACTION_ATTACK;
			continue;
		}

		units_.attack_timer[i] = units_.attack_cooldown[i];
		units_.action_state[i] = ACTION_ATTACK;

		const auto &uinfo_att = unit_type_info(units_.unit_type[i]);
		float hack_dmg = uinfo_att.hack_damage;
		float pierce_dmg = uinfo_att.pierce_damage;
		float crush_dmg = uinfo_att.crush_damage;

		// Battering Ram & Elephant Siege Demolition Bonus
		if (units_.unit_type[i] == UNIT_SIEGE_RAM) {
			crush_dmg *= 4.0f; // 4x Crush damage vs fortifications
		} else if (units_.unit_type[i] == UNIT_ELEPHANT || units_.unit_type[i] == UNIT_CATAPULT) {
			crush_dmg *= 2.0f;
		}

		// Structure Armor: Hack 3.0, Pierce 15.0 (arrow-resistant), Crush 3.0
		float b_hack_armor = (b.type == BLD_FORTRESS || b.type == BLD_WALL_SEGMENT) ? 5.0f : 3.0f;
		float b_pierce_armor = (b.type == BLD_FORTRESS || b.type == BLD_WALL_SEGMENT) ? 20.0f : 15.0f;
		float b_crush_armor = (b.type == BLD_FORTRESS) ? 6.0f : 3.0f;

		float bld_dmg = std::max(1.0f,
			hack_dmg * powf(0.9f, b_hack_armor) +
			pierce_dmg * powf(0.9f, b_pierce_armor) +
			crush_dmg * powf(0.9f, b_crush_armor)
		);

		bool is_ranged_attack = (units_.unit_type[i] == UNIT_ARCHER || units_.unit_type[i] == UNIT_ARCHER_MAIDEN ||
		                         units_.unit_type[i] == UNIT_BOLT_SHOOTER || units_.unit_type[i] == UNIT_CATAPULT);

		if (is_ranged_attack) {
			fire_arrow(units_.pos_x[i], 1.4f, units_.pos_z[i],
			           b.x, 1.5f, b.z,
			           bld_dmg, units_.team[i], b.id, EFFECT_BURNING, 2);
		} else {
			b.hp -= bld_dmg;
			if (b.hp < b.max_hp * 0.33f) {
				b.is_burning = true;
			}
			if (b.hp <= 0.0f) {
				// Destroy building
				if (units_.team[i] == 0) {
					record_.enemies_killed++;
					gold_ += 25; stone_ += 25; wood_ += 25; // Loot.js Plunder
				}
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
				b.build_progress = 0.0f; // Marks collapsed/destroyed
				units_.target_building_id[i] = 0;
				units_.action_state[i] = ACTION_IDLE;
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
				float base_range = is_wall ? 16.0f : binfo.attack_range;
				float base_dmg = is_wall ? 15.0f : binfo.attack_damage;
				float cd = is_wall ? 1.0f : binfo.attack_cooldown;

				// Parapet Wall & High-Ground Elevation Advantage (+50% range on walls)
				float wall_y = get_terrain_height(b.x, b.z) + (is_wall ? 4.5f : 3.5f);
				float range = is_wall ? (base_range * 1.5f) : base_range;
				float damage = is_wall ? (base_dmg * 1.25f) : base_dmg;

				int32_t hit = -1;
				grid.visit_radius(b.x, b.z, range, [&](int32_t i) {
					if (hit >= 0 || !units_.alive[i] || units_.team[i] == b.team) return;
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
					int arrow_count = is_wall ? (int)b.garrisoned_units.size() : (1 + (int)b.garrisoned_units.size());
					for (int a = 0; a < std::min(5, arrow_count); ++a) {
						float spread = (a - arrow_count * 0.5f) * 0.22f;
						fire_arrow(b.x + spread, wall_y, b.z + spread,
								   units_.pos_x[hit], 0.8f, units_.pos_z[hit],
								   damage, b.team, units_.id[hit]);
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

	// 3b. Priest / Healer Unit Active Healing & Cleansing (UNIT_HEALER)
	for (size_t h = 0; h < n; ++h) {
		if (!units_.alive[h] || units_.unit_type[h] != UNIT_HEALER) continue;
		const uint8_t h_team = units_.team[h];
		const float hx = units_.pos_x[h];
		const float hz = units_.pos_z[h];
		int32_t heal_target = -1;
		float lowest_hp_ratio = 1.0f;

		grid.visit_radius(hx, hz, 12.0f, [&](int32_t u) {
			if (!units_.alive[u] || units_.team[u] != h_team || u == (int32_t)h) return;
			float ratio = units_.hp[u] / units_.max_hp[u];
			bool has_debuff = (units_.status_effects[u] & (EFFECT_POISON | EFFECT_SNARE)) != 0;
			if (ratio < lowest_hp_ratio || (has_debuff && ratio < 0.99f)) {
				lowest_hp_ratio = ratio;
				heal_target = u;
			}
		});

		if (heal_target >= 0 && (lowest_hp_ratio < 0.999f || (units_.status_effects[heal_target] & (EFFECT_POISON | EFFECT_SNARE)))) {
			units_.hp[heal_target] = std::min(units_.max_hp[heal_target], units_.hp[heal_target] + dt * 8.0f);
			if (units_.status_effects[heal_target] & EFFECT_POISON) {
				units_.status_effects[heal_target] &= ~EFFECT_POISON;
				units_.poison_timer[heal_target] = 0.0f;
			}
			if (units_.status_effects[heal_target] & EFFECT_SNARE) {
				units_.status_effects[heal_target] &= ~EFFECT_SNARE;
				units_.snare_timer[heal_target] = 0.0f;
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
					if (p.status_effect != 0) {
						units_.status_effects[tidx] |= p.status_effect;
						if (p.status_effect & EFFECT_POISON) units_.poison_timer[tidx] = 5.0f;
						if (p.status_effect & EFFECT_SNARE) units_.snare_timer[tidx] = 3.0f;
					}
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
				if (units_.team[i] == 0) record_.units_lost++;
				else record_.enemies_killed++;
			}
			if (units_.poison_timer[i] <= 0.0f) {
				units_.status_effects[i] &= ~EFFECT_POISON;
				units_.poison_timer[i] = 0.0f;
			}
		}

		// 2. Snare effect (Spear brace / Cavalry snare)
		if (units_.status_effects[i] & EFFECT_SNARE) {
			units_.snare_timer[i] -= dt;
			if (units_.snare_timer[i] <= 0.0f) {
				units_.status_effects[i] &= ~EFFECT_SNARE;
				units_.snare_timer[i] = 0.0f;
			}
		}

		// 3. Knockback effect (Trample impulse)
		if (units_.status_effects[i] & EFFECT_KNOCKBACK) {
			units_.knockback_timer[i] -= dt;
			if (units_.knockback_timer[i] <= 0.0f) {
				units_.status_effects[i] &= ~EFFECT_KNOCKBACK;
				units_.knockback_timer[i] = 0.0f;
			}
		}
	}

	// 4. Burning structures (< 33% HP)
	for (auto &b : buildings_) {
		if (b.build_progress >= 1.0f && b.hp > 0.0f && b.hp < b.max_hp * 0.33f) {
			b.hp = std::max(0.0f, b.hp - 6.0f * dt); // 6 HP / sec fire burning tick
			if (b.hp <= 0.0f) {
				if (b.team == 0) record_.buildings_destroyed++;
			}
		}
	}
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
	v.unit_type = units_.unit_type;
	v.action_state = units_.action_state;
	v.res_type_carried = units_.res_type_carried;
	v.stance = units_.stance;
	v.formation = units_.formation;
	v.carry = units_.carry;
	v.death_timer = units_.death_timer;
	v.kills = units_.kills;
	v.rank = units_.rank;
	v.status_effects = units_.status_effects;
	v.count = units_.count;
	return v;
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

void Simulation::update_petra_threat_map(float dt) {
	petra_threat_map_.update_timer += dt;
	if (petra_threat_map_.update_timer < 0.4f) return;
	petra_threat_map_.update_timer = 0.0f;

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

		for (int z = z0; z <= z1; ++z) {
			for (int x = x0; x <= x1; ++x) {
				float dist_sq = (float)((x - cx) * (x - cx) + (z - cz) * (z - cz));
				float max_dist_sq = (float)(radius_cells * radius_cells + 1);
				float falloff = std::max(0.2f, 1.0f - (dist_sq / max_dist_sq));
				grid[z * THREAT_GRID_RES + x] += power * falloff;
			}
		}
	};

	// 1. Accumulate unit threats
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i]) continue;
		float power = 0.0f;
		int rad = 1;
		switch (units_.unit_type[i]) {
			case UNIT_VILLAGER: power = 0.5f; rad = 1; break;
			case UNIT_SWORDSMAN: power = 5.0f; rad = 1; break;
			case UNIT_SPEARMAN: power = 5.5f; rad = 1; break;
			case UNIT_ARCHER: power = 7.5f; rad = 2; break; // ranged threat
			case UNIT_CAVALRY: power = 9.0f; rad = 2; break; // high mobility
			case UNIT_CATAPULT: power = 14.0f; rad = 3; break; // heavy siege
			case UNIT_SIEGE_RAM: power = 12.0f; rad = 1; break;
			case UNIT_ELEPHANT: power = 20.0f; rad = 2; break;
			case UNIT_HERO: power = 18.0f; rad = 2; break;
			default: power = 4.0f; rad = 1; break;
		}

		if (units_.team[i] == 0) {
			add_power(petra_threat_map_.enemy_threat, units_.pos_x[i], units_.pos_z[i], power, rad);
		} else if (units_.team[i] == 1) {
			add_power(petra_threat_map_.friendly_defense, units_.pos_x[i], units_.pos_z[i], power, rad);
		}
	}

	// 2. Accumulate building defensive power
	for (const auto &b : buildings_) {
		if (b.build_progress < 0.5f) continue;
		float b_power = 0.0f;
		int b_rad = 1;
		if (b.type == BLD_TOWN_CENTER) { b_power = 22.0f; b_rad = 2; }
		else if (b.type == BLD_FORTRESS) { b_power = 35.0f; b_rad = 3; }
		else if (b.type == BLD_TOWER) { b_power = 16.0f; b_rad = 2; }
		else if (b.type == BLD_BARRACKS) { b_power = 5.0f; b_rad = 1; }

		if (b_power > 0.0f) {
			if (b.team == 0) {
				add_power(petra_threat_map_.enemy_threat, b.x, b.z, b_power, b_rad);
			} else if (b.team == 1) {
				add_power(petra_threat_map_.friendly_defense, b.x, b.z, b_power, b_rad);
			}
		}
	}

	// 3. Compute net danger with decay
	for (int c = 0; c < THREAT_GRID_RES * THREAT_GRID_RES; ++c) {
		float raw_danger = std::max(0.0f, petra_threat_map_.enemy_threat[c] - petra_threat_map_.friendly_defense[c] * 0.65f);
		petra_threat_map_.danger[c] = petra_threat_map_.danger[c] * 0.3f + raw_danger * 0.7f;
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

void Simulation::petra_manage_workers(float dt) {
	// Find safest shelter building for Team 1
	float safe_x = 36.0f, safe_z = 36.0f;
	uint32_t shelter_bld_id = 0;
	float best_shelter_score = 1e9f;

	for (const auto &b : buildings_) {
		if (b.team != 1 || b.build_progress < 0.5f) continue;
		if (b.type == BLD_TOWN_CENTER || b.type == BLD_FORTRESS || b.type == BLD_TOWER || b.type == BLD_STOREHOUSE) {
			float b_threat = get_threat_at(b.x, b.z);
			if (b_threat < best_shelter_score) {
				best_shelter_score = b_threat;
				safe_x = b.x;
				safe_z = b.z;
				shelter_bld_id = b.id;
			}
		}
	}

	// 1. Worker Threat Evaluation & Evacuation
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != 1 || units_.unit_type[i] != UNIT_VILLAGER) continue;

		float threat = get_threat_at(units_.pos_x[i], units_.pos_z[i]);

		// Under heavy danger: Evacuate!
		if (threat >= 7.0f) {
			if (units_.action_state[i] == ACTION_GATHER || units_.action_state[i] == ACTION_BUILD ||
				units_.action_state[i] == ACTION_RETURN_DROPSITE || units_.action_state[i] == ACTION_IDLE) {
				units_.target_x[i] = safe_x + (float)(rand() % 5 - 2);
				units_.target_z[i] = safe_z + (float)(rand() % 5 - 2);
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
					if (dx * dx + dz * dz <= 4.0f * 4.0f && (int)sb.garrisoned_units.size() < sb.garrison_cap) {
						uint32_t uid = units_.id[i];
						order_garrison(&uid, 1, sb.id);
					}
				}
			}
		}
	}

	// Ungarrison workers when threat is cleared
	if (best_shelter_score < 2.0f) {
		for (auto &b : buildings_) {
			if (b.team == 1 && !b.garrisoned_units.empty()) {
				order_ungarrison(b.id);
			}
		}
	}
}

void Simulation::petra_manage_military_production(float dt) {
	// Scan Player (Team 0) forces
	int enemy_infantry = 0;
	int enemy_spearmen = 0;
	int enemy_archers = 0;
	int enemy_cavalry = 0;
	int enemy_siege = 0;
	int enemy_champions = 0;
	int enemy_forts = 0;

	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != 0) continue;
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
		if (b.team != 0 || b.build_progress < 0.5f) continue;
		if (b.type == BLD_FORTRESS || b.type == BLD_TOWER || b.type == BLD_GATE || b.type == BLD_WALL_SEGMENT) {
			enemy_forts++;
		}
	}

	int total_enemy_mil = enemy_infantry + enemy_spearmen + enemy_archers + enemy_cavalry + enemy_siege + enemy_champions;
	float norm = 1.0f / (float)std::max(1, total_enemy_mil);

	// Dynamic Counter-Unit Utility Scores (0 A.D. Petra combat counters)
	// Spearmen (2.5x vs Cavalry / 2.0x vs Elephants)
	float spearman_score = 1.2f + (enemy_cavalry * 4.5f + enemy_champions * 3.5f) * norm;

	// Archers (Pierce kiting vs Swordsmen & Spearmen)
	float archer_score = 1.0f + (enemy_infantry * 3.0f + enemy_spearmen * 3.5f) * norm;

	// Swordsmen (Hack melee, general frontline)
	float swordsman_score = 1.0f + (enemy_spearmen * 1.8f) * norm;

	// Cavalry (Speed & Flanking counter vs Archers and Siege)
	float cavalry_score = 0.8f + (enemy_archers * 4.0f + enemy_siege * 3.0f) * norm;

	// Siege (Crush damage vs Fortresses and Towers)
	float siege_score = 0.2f;
	if (petra_phase_ >= 2) {
		siege_score = 0.5f + (float)enemy_forts * 2.0f;
	}

	// Champion / Elephant (Elite shock)
	float champion_score = (petra_phase_ >= 3) ? 2.5f : 0.0f;

	// Find available AI military production buildings
	uint32_t barracks_id = 0;
	uint32_t fortress_id = 0;
	for (const auto &b : buildings_) {
		if (b.team != 1 || b.build_progress < 1.0f) continue;
		if (b.type == BLD_BARRACKS && b.queue.size() < 4 && barracks_id == 0) barracks_id = b.id;
		if (b.type == BLD_FORTRESS && b.queue.size() < 4 && fortress_id == 0) fortress_id = b.id;
	}

	if (barracks_id == 0 && fortress_id == 0) return;
	if (ai_pop_current() >= ai_pop_max()) return;

	struct Option {
		UnitType type;
		float score;
		uint32_t bld_id;
	};
	std::vector<Option> options;

	if (barracks_id != 0) {
		options.push_back({UNIT_SPEARMAN, spearman_score, barracks_id});
		options.push_back({UNIT_ARCHER, archer_score, barracks_id});
		options.push_back({UNIT_SWORDSMAN, swordsman_score, barracks_id});
		if (petra_phase_ >= 2) {
			options.push_back({UNIT_CAVALRY, cavalry_score, barracks_id});
		}
	}
	if (fortress_id != 0) {
		if (petra_phase_ >= 2) {
			options.push_back({UNIT_CATAPULT, siege_score, fortress_id});
			options.push_back({UNIT_SIEGE_RAM, siege_score * 0.9f, fortress_id});
		}
		if (petra_phase_ >= 3) {
			options.push_back({UNIT_ELEPHANT, champion_score, fortress_id});
			options.push_back({UNIT_HERO, champion_score * 0.9f, fortress_id});
		}
	}

	std::sort(options.begin(), options.end(), [](const Option &a, const Option &b) {
		return a.score > b.score;
	});

	for (const auto &opt : options) {
		const UnitTypeInfo &uinfo = unit_type_info(opt.type);
		if (ai_food_ >= uinfo.food_cost && ai_wood_ >= uinfo.wood_cost &&
			ai_gold_ >= uinfo.gold_cost && ai_stone_ >= uinfo.stone_cost) {
			ai_food_ -= uinfo.food_cost;
			ai_wood_ -= uinfo.wood_cost;
			ai_gold_ -= uinfo.gold_cost;
			ai_stone_ -= uinfo.stone_cost;

			auto bit = building_id_to_idx_.find(opt.bld_id);
			if (bit != building_id_to_idx_.end()) {
				ProductionOrder order;
				order.unit_type = opt.type;
				order.timer = 0.0f;
				order.duration = uinfo.train_time * 0.75f;
				buildings_[bit->second].queue.push_back(order);
			}
			break;
		}
	}
}

void Simulation::petra_manage_economy(float dt) {
	petra_econ_timer_ += dt;
	if (petra_econ_timer_ < 1.0f) return;
	petra_econ_timer_ = 0.0f;

	// 1. Check AI Villager Population Goal
	int ai_villagers = 0;
	for (size_t i = 0; i < units_.count; ++i) {
		if (units_.alive[i] && units_.team[i] == 1 && units_.unit_type[i] == UNIT_VILLAGER) {
			ai_villagers++;
		}
	}

	int target_villagers = (petra_phase_ == 1) ? 10 : ((petra_phase_ == 2) ? 18 : 25);
	if (ai_villagers < target_villagers && ai_pop_current() < ai_pop_max() && ai_food_ >= 50.0f) {
		for (auto &b : buildings_) {
			if (b.team == 1 && b.type == BLD_TOWN_CENTER && b.build_progress >= 1.0f && b.queue.size() < 3) {
				ai_food_ -= 50.0f;
				ProductionOrder order;
				order.unit_type = UNIT_VILLAGER;
				order.timer = 0.0f;
				order.duration = unit_type_info(UNIT_VILLAGER).train_time * 0.8f;
				b.queue.push_back(order);
				break;
			}
		}
	}

	// 2. Count workers currently on each resource
	int workers_food = 0, workers_wood = 0, workers_gold = 0, workers_stone = 0;
	std::vector<size_t> reassignable_workers;

	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != 1 || units_.unit_type[i] != UNIT_VILLAGER) continue;
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

	// 3. Compute Resource Deficits & Target Allocations
	float need_food = std::max(50.0f, 300.0f - ai_food_);
	float need_wood = std::max(50.0f, 400.0f - ai_wood_);
	float need_gold = std::max(30.0f, 250.0f - ai_gold_);
	float need_stone = (petra_phase_ >= 2) ? std::max(50.0f, 250.0f - ai_stone_) : 30.0f;

	float total_need = need_food + need_wood + need_gold + need_stone;
	float pct_food  = need_food / total_need;
	float pct_wood  = need_wood / total_need;
	float pct_gold  = need_gold / total_need;
	float pct_stone = need_stone / total_need;

	int total_workers = std::max(1, ai_villagers);
	int target_food  = (int)std::round(pct_food * total_workers);
	int target_wood  = (int)std::round(pct_wood * total_workers);
	int target_gold  = (int)std::round(pct_gold * total_workers);
	int target_stone = total_workers - (target_food + target_wood + target_gold);
	if (target_stone < 0) target_stone = 0;

	// Assign reassignable workers to the most under-allocated resource
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

		// Find nearest safe resource of chosen type
		float wx = units_.pos_x[widx];
		float wz = units_.pos_z[widx];
		float best_d2 = 1e9f;
		uint32_t best_rid = 0;

		for (size_t r = 0; r < resources_.count; ++r) {
			if (!resources_.alive[r] || resources_.amount[r] <= 0 || resources_.type[r] != chosen_res_type) continue;
			if (!is_position_safe(resources_.x[r], resources_.z[r], 6.0f)) continue;

			float dx = wx - resources_.x[r];
			float dz = wz - resources_.z[r];
			float d2 = dx * dx + dz * dz;
			if (d2 < best_d2) {
				best_d2 = d2;
				best_rid = resources_.id[r];
			}
		}

		if (best_rid != 0) {
			uint32_t uid = units_.id[widx];
			order_gather(&uid, 1, best_rid);
			if (chosen_res_type == RES_FOOD) workers_food++;
			else if (chosen_res_type == RES_WOOD) workers_wood++;
			else if (chosen_res_type == RES_GOLD) workers_gold++;
			else if (chosen_res_type == RES_STONE) workers_stone++;
		}
	}
}

void Simulation::petra_manage_buildings(float dt) {
	// 1. Build House if approaching population limit
	if (ai_pop_current() >= ai_pop_max() - 3 && ai_wood_ >= 80.0f) {
		bool house_in_progress = false;
		for (const auto &b : buildings_) {
			if (b.team == 1 && b.type == BLD_HOUSE && b.build_progress < 1.0f) {
				house_in_progress = true;
				break;
			}
		}
		if (!house_in_progress) {
			float base_x = 36.0f, base_z = 36.0f;
			for (const auto &b : buildings_) {
				if (b.team == 1 && b.type == BLD_TOWN_CENTER) {
					base_x = b.x; base_z = b.z; break;
				}
			}
			int start_cx = (int)(base_x / GRID);
			int start_cz = (int)(base_z / GRID);
			bool placed = false;
			for (int r = 3; r <= 8 && !placed; ++r) {
				for (int dx = -r; dx <= r && !placed; ++dx) {
					for (int dz = -r; dz <= r && !placed; ++dz) {
						if (std::abs(dx) != r && std::abs(dz) != r) continue;
						int cx = start_cx + dx;
						int cz = start_cz + dz;
						if (can_place(cx, cz, 1)) {
							ai_wood_ -= 80.0f;
							uint32_t bid = spawn_building(cx, cz, BLD_HOUSE, 1, 0.05f);
							auto idle_workers = get_idle_workers(1);
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

	// 2. Build Barracks if AI has none
	bool has_barracks = false;
	for (const auto &b : buildings_) {
		if (b.team == 1 && b.type == BLD_BARRACKS) { has_barracks = true; break; }
	}
	if (!has_barracks && ai_wood_ >= 150.0f && ai_gold_ >= 50.0f && ai_stone_ >= 50.0f) {
		float base_x = 36.0f, base_z = 36.0f;
		for (const auto &b : buildings_) {
			if (b.team == 1 && b.type == BLD_TOWN_CENTER) {
				base_x = b.x; base_z = b.z; break;
			}
		}
		int start_cx = (int)(base_x / GRID);
		int start_cz = (int)(base_z / GRID);
		bool placed = false;
		for (int r = 3; r <= 8 && !placed; ++r) {
			for (int dx = -r; dx <= r && !placed; ++dx) {
				for (int dz = -r; dz <= r && !placed; ++dz) {
					int cx = start_cx + dx;
					int cz = start_cz + dz;
					if (can_place(cx, cz, 2)) {
						ai_wood_ -= 150.0f;
						ai_gold_ -= 50.0f;
						ai_stone_ -= 50.0f;
						uint32_t bid = spawn_building(cx, cz, BLD_BARRACKS, 1, 0.05f);
						auto idle_workers = get_idle_workers(1);
						if (!idle_workers.empty()) {
							order_build_or_repair(idle_workers.data(), std::min((size_t)3, idle_workers.size()), bid);
						}
						placed = true;
					}
				}
			}
		}
	}

	// 3. Build Defense Tower in Phase >= 2
	if (petra_phase_ >= 2 && ai_wood_ >= 100.0f && ai_stone_ >= 100.0f) {
		int tower_count = 0;
		for (const auto &b : buildings_) {
			if (b.team == 1 && b.type == BLD_TOWER) tower_count++;
		}
		if (tower_count < 3) {
			float base_x = 36.0f, base_z = 36.0f;
			int start_cx = (int)(base_x / GRID);
			int start_cz = (int)(base_z / GRID);
			bool placed = false;
			for (int r = 5; r <= 10 && !placed; ++r) {
				for (int dx = -r; dx <= 0 && !placed; ++dx) {
					for (int dz = -r; dz <= 0 && !placed; ++dz) {
						int cx = start_cx + dx;
						int cz = start_cz + dz;
						if (can_place(cx, cz, 1)) {
							ai_wood_ -= 100.0f;
							ai_stone_ -= 100.0f;
							uint32_t bid = spawn_building(cx, cz, BLD_TOWER, 1, 0.05f);
							auto idle_workers = get_idle_workers(1);
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

	// 4. Build Fortress in Phase >= 3
	if (petra_phase_ >= 3 && ai_wood_ >= 300.0f && ai_gold_ >= 200.0f && ai_stone_ >= 300.0f) {
		bool has_fortress = false;
		for (const auto &b : buildings_) {
			if (b.team == 1 && b.type == BLD_FORTRESS) { has_fortress = true; break; }
		}
		if (!has_fortress) {
			float base_x = 36.0f, base_z = 36.0f;
			int start_cx = (int)(base_x / GRID);
			int start_cz = (int)(base_z / GRID);
			bool placed = false;
			for (int r = 4; r <= 10 && !placed; ++r) {
				for (int dx = -r; dx <= 0 && !placed; ++dx) {
					for (int dz = -r; dz <= 0 && !placed; ++dz) {
						int cx = start_cx + dx;
						int cz = start_cz + dz;
						if (can_place(cx, cz, 2)) {
							ai_wood_ -= 300.0f;
							ai_gold_ -= 200.0f;
							ai_stone_ -= 300.0f;
							uint32_t bid = spawn_building(cx, cz, BLD_FORTRESS, 1, 0.05f);
							auto idle_workers = get_idle_workers(1);
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
	Building *ai_tc = nullptr;
	for (auto &b : buildings_) {
		if (b.team == 1 && b.type == BLD_TOWN_CENTER && b.build_progress >= 1.0f) {
			ai_tc = &b;
			break;
		}
	}
	if (!ai_tc || ai_tc->research_tech != TECH_NONE) return;

	// Phase 1 -> Town Phase
	if (petra_phase_ == 1 && (ai_researched_techs_mask_ & (1u << TECH_PHASE_TOWN)) == 0) {
		const TechInfo &tinfo = tech_info(TECH_PHASE_TOWN);
		if (ai_food_ >= tinfo.food_cost && ai_wood_ >= tinfo.wood_cost && ai_gold_ >= tinfo.gold_cost) {
			ai_food_ -= tinfo.food_cost;
			ai_wood_ -= tinfo.wood_cost;
			ai_gold_ -= tinfo.gold_cost;
			ai_tc->research_tech = TECH_PHASE_TOWN;
			ai_tc->research_timer = 0.0f;
			ai_tc->research_duration = tinfo.research_time * 0.75f;
			return;
		}
	}

	// Phase 2 Economic Techs
	if (petra_phase_ >= 2) {
		if ((ai_researched_techs_mask_ & (1u << TECH_FARMING_1)) == 0) {
			const TechInfo &tinfo = tech_info(TECH_FARMING_1);
			if (ai_food_ >= tinfo.food_cost && ai_wood_ >= tinfo.wood_cost) {
				ai_food_ -= tinfo.food_cost;
				ai_wood_ -= tinfo.wood_cost;
				ai_tc->research_tech = TECH_FARMING_1;
				ai_tc->research_timer = 0.0f;
				ai_tc->research_duration = tinfo.research_time * 0.75f;
				return;
			}
		}
		if ((ai_researched_techs_mask_ & (1u << TECH_WOODCUTTING_1)) == 0) {
			const TechInfo &tinfo = tech_info(TECH_WOODCUTTING_1);
			if (ai_food_ >= tinfo.food_cost && ai_wood_ >= tinfo.wood_cost && ai_gold_ >= tinfo.gold_cost) {
				ai_food_ -= tinfo.food_cost;
				ai_wood_ -= tinfo.wood_cost;
				ai_gold_ -= tinfo.gold_cost;
				ai_tc->research_tech = TECH_WOODCUTTING_1;
				ai_tc->research_timer = 0.0f;
				ai_tc->research_duration = tinfo.research_time * 0.75f;
				return;
			}
		}
		if ((ai_researched_techs_mask_ & (1u << TECH_MINING_1)) == 0) {
			const TechInfo &tinfo = tech_info(TECH_MINING_1);
			if (ai_food_ >= tinfo.food_cost && ai_wood_ >= tinfo.wood_cost && ai_gold_ >= tinfo.gold_cost && ai_stone_ >= tinfo.stone_cost) {
				ai_food_ -= tinfo.food_cost;
				ai_wood_ -= tinfo.wood_cost;
				ai_gold_ -= tinfo.gold_cost;
				ai_stone_ -= tinfo.stone_cost;
				ai_tc->research_tech = TECH_MINING_1;
				ai_tc->research_timer = 0.0f;
				ai_tc->research_duration = tinfo.research_time * 0.75f;
				return;
			}
		}
	}

	// Town Phase -> City Phase
	if (petra_phase_ == 2 && (ai_researched_techs_mask_ & (1u << TECH_PHASE_CITY)) == 0) {
		const TechInfo &tinfo = tech_info(TECH_PHASE_CITY);
		if (ai_food_ >= tinfo.food_cost && ai_wood_ >= tinfo.wood_cost && ai_gold_ >= tinfo.gold_cost && ai_stone_ >= tinfo.stone_cost) {
			ai_food_ -= tinfo.food_cost;
			ai_wood_ -= tinfo.wood_cost;
			ai_gold_ -= tinfo.gold_cost;
			ai_stone_ -= tinfo.stone_cost;
			ai_tc->research_tech = TECH_PHASE_CITY;
			ai_tc->research_timer = 0.0f;
			ai_tc->research_duration = tinfo.research_time * 0.75f;
			return;
		}
	}

	// Phase 3 Military Techs
	if (petra_phase_ >= 3) {
		if ((ai_researched_techs_mask_ & (1u << TECH_WEAPONS_1)) == 0) {
			const TechInfo &tinfo = tech_info(TECH_WEAPONS_1);
			if (ai_food_ >= tinfo.food_cost && ai_wood_ >= tinfo.wood_cost && ai_gold_ >= tinfo.gold_cost) {
				ai_food_ -= tinfo.food_cost;
				ai_wood_ -= tinfo.wood_cost;
				ai_gold_ -= tinfo.gold_cost;
				ai_tc->research_tech = TECH_WEAPONS_1;
				ai_tc->research_timer = 0.0f;
				ai_tc->research_duration = tinfo.research_time * 0.75f;
				return;
			}
		}
		if ((ai_researched_techs_mask_ & (1u << TECH_ARMOR_1)) == 0) {
			const TechInfo &tinfo = tech_info(TECH_ARMOR_1);
			if (ai_food_ >= tinfo.food_cost && ai_wood_ >= tinfo.wood_cost && ai_gold_ >= tinfo.gold_cost && ai_stone_ >= tinfo.stone_cost) {
				ai_food_ -= tinfo.food_cost;
				ai_wood_ -= tinfo.wood_cost;
				ai_gold_ -= tinfo.gold_cost;
				ai_stone_ -= tinfo.stone_cost;
				ai_tc->research_tech = TECH_ARMOR_1;
				ai_tc->research_timer = 0.0f;
				ai_tc->research_duration = tinfo.research_time * 0.75f;
				return;
			}
		}
	}
}

void Simulation::petra_manage_tactical_attacks(float dt) {
	petra_strike_force_.state_timer += dt;

	// 1. Staging Point determination
	float base_x = 36.0f, base_z = 36.0f;
	for (const auto &b : buildings_) {
		if (b.team == 1 && b.type == BLD_TOWN_CENTER) {
			base_x = b.x; base_z = b.z; break;
		}
	}
	petra_strike_force_.staging_x = base_x * 0.65f;
	petra_strike_force_.staging_z = base_z * 0.65f;

	// Clean up dead units from strike force
	auto &sf_units = petra_strike_force_.unit_ids;
	sf_units.erase(std::remove_if(sf_units.begin(), sf_units.end(), [&](uint32_t uid) {
		int32_t uidx = lookup_unit(uid);
		return (uidx < 0 || !units_.alive[uidx] || units_.team[uidx] != 1);
	}), sf_units.end());

	// Collect unassigned / newly produced AI military units into strike force
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] || units_.team[i] != 1 || units_.unit_type[i] == UNIT_VILLAGER) continue;
		uint32_t uid = units_.id[i];
		if (std::find(sf_units.begin(), sf_units.end(), uid) == sf_units.end()) {
			sf_units.push_back(uid);
			if (petra_strike_force_.state == PetraStrikeForce::MUSTER) {
				// Move to staging muster point
				cmd_move(&uid, 1, petra_strike_force_.staging_x, petra_strike_force_.staging_z);
			}
		}
	}

	// Check if base is under attack (Emergency Defense Mobilization)
	float base_threat = get_threat_at(base_x, base_z);
	if (base_threat >= 10.0f && !sf_units.empty()) {
		order_attack_move(sf_units.data(), sf_units.size(), base_x, base_z);
		petra_strike_force_.state = PetraStrikeForce::ENGAGE;
		return;
	}

	// 2. State Machine for Strike Force
	size_t army_threshold = (petra_phase_ == 1) ? 6 : ((petra_phase_ == 2) ? 10 : 16);

	if (petra_strike_force_.state == PetraStrikeForce::MUSTER) {
		if (sf_units.size() >= army_threshold || petra_strike_force_.state_timer >= 60.0f) {
			petra_strike_force_.state = PetraStrikeForce::MARCH;
			petra_strike_force_.state_timer = 0.0f;
		}
	}

	if (petra_strike_force_.state == PetraStrikeForce::MARCH) {
		// 3. Strategic Target Selection
		// Priority 1: Defense Towers / Fortresses guarding approaches
		// Priority 2: Economic dropsites (Storehouses, Farmsteads) and exposed Villagers
		// Priority 3: Military Barracks
		// Priority 4: Town Center
		float target_x = 0.0f, target_z = 0.0f;
		uint32_t chosen_bld_id = 0;
		int best_priority = -1;
		float best_dist_sq = 1e9f;

		for (const auto &b : buildings_) {
			if (b.team != 0 || b.hp <= 0) continue;
			int priority = 0;
			if (b.type == BLD_TOWER || b.type == BLD_FORTRESS) priority = 4; // High threat defense
			else if (b.type == BLD_STOREHOUSE || b.type == BLD_FARMSTEAD) priority = 3; // Economic dropsite
			else if (b.type == BLD_BARRACKS) priority = 2;
			else if (b.type == BLD_TOWN_CENTER) priority = 1;

			if (priority > best_priority || (priority == best_priority && (b.x * b.x + b.z * b.z) < best_dist_sq)) {
				best_priority = priority;
				best_dist_sq = b.x * b.x + b.z * b.z;
				chosen_bld_id = b.id;
				target_x = b.x;
				target_z = b.z;
			}
		}

		petra_strike_force_.target_building_id = chosen_bld_id;
		petra_strike_force_.target_x = target_x;
		petra_strike_force_.target_z = target_z;

		// 4. Tactical Formations Assignment
		for (uint32_t uid : sf_units) {
			int32_t idx = lookup_unit(uid);
			if (idx < 0) continue;
			if (units_.unit_type[idx] == UNIT_SWORDSMAN || units_.unit_type[idx] == UNIT_SPEARMAN || units_.unit_type[idx] == UNIT_ELEPHANT) {
				units_.formation[idx] = FORMATION_PHALANX; // Frontline Shield Wall / Phalanx
			} else if (units_.unit_type[idx] == UNIT_CAVALRY) {
				units_.formation[idx] = FORMATION_WEDGE; // Flanking Wedge Shock
			} else if (units_.unit_type[idx] == UNIT_ARCHER || units_.unit_type[idx] == UNIT_CATAPULT) {
				units_.formation[idx] = FORMATION_SCATTER; // Dispersed Skirmish Volley
			}
		}

		// Coordinated Attack-Move order towards target
		order_attack_move(sf_units.data(), sf_units.size(), target_x, target_z);
		petra_strike_force_.state = PetraStrikeForce::ENGAGE;
		petra_strike_force_.state_timer = 0.0f;
	}

	if (petra_strike_force_.state == PetraStrikeForce::ENGAGE) {
		if (sf_units.size() < 2) {
			petra_strike_force_.state = PetraStrikeForce::MUSTER;
			petra_strike_force_.state_timer = 0.0f;
		} else if (petra_strike_force_.state_timer >= 25.0f) {
			petra_strike_force_.state = PetraStrikeForce::MARCH;
		}
	}
}

void Simulation::tick_petra_ai(float dt) {
	// 0 A.D. Petra Autonomous AI System
	petra_timer_ += dt;

	// AI economic passive baseline trickle
	ai_food_  += 3.0f * dt;
	ai_wood_  += 3.0f * dt;
	ai_gold_  += 2.0f * dt;
	ai_stone_ += 1.5f * dt;
	ai_food_  = std::min(ai_food_,  9999.0f);
	ai_wood_  = std::min(ai_wood_,  9999.0f);
	ai_gold_  = std::min(ai_gold_,  9999.0f);
	ai_stone_ = std::min(ai_stone_, 9999.0f);

	// 1. Dynamic Danger & Threat Heatmap
	update_petra_threat_map(dt);

	// 2. Villager Danger Evaluation & Retreat
	petra_manage_workers(dt);

	// 3. Dynamic Counter-Unit Military Composition & Production
	petra_manage_military_production(dt);

	// 4. Economic Balance, Housing & Phase Progression
	petra_manage_economy(dt);
	petra_manage_buildings(dt);
	petra_manage_research(dt);

	// 5. Coordinated Tactical Battlegroup Attacks
	petra_manage_tactical_attacks(dt);
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
				spawn_resource_world(fauna_.pos_x[i], fauna_.pos_z[i], RES_FOOD, fauna_.food_amount[i]);
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
					fauna_.state_timer[i] = 3.0f + (float)(rand() % 4);
					if (rand() % 2 == 0) {
						float angle = (float)(rand() % 360) * 0.0174533f;
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
