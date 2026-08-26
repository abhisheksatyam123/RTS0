#include "simulation.h"
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace rtso {

Simulation::Simulation() {
	units_.init(1024);
	resources_.init(512);
}

Simulation::~Simulation() = default;

void Simulation::reset() {
	units_.free_all(); units_.init(1024);
	resources_.free_all(); resources_.init(512);
	buildings_.clear();
	projectiles_.clear();
	unit_id_to_idx_.clear();
	res_id_to_idx_.clear();
	building_id_to_idx_.clear();
	occupied_cells_.clear();

	next_id_ = 1;
	food_ = 250;
	wood_ = 350;
	gold_ = 250;
	stone_ = 150;
	passive_timer_ = 0.0f;
	wave_timer_ = 0.0f;
	wave_number_ = 0;
	game_over_ = false;
	victory_ = false;
	tick_us_ = ticks_ = 0;
	frame_arena_.reset_all();
}

int Simulation::pop_current() const {
	int count = 0;
	for (size_t i = 0; i < units_.count; ++i) {
		if (units_.alive[i] && units_.team[i] == 0) count++;
	}
	return count;
}

int Simulation::pop_max() const {
	int cap = 10; // Base TC capacity
	for (const auto &b : buildings_) {
		if (b.team == 0 && b.build_progress >= 1.0f) {
			if (b.type == BLD_HOUSE) cap += 5;
			else if (b.type == BLD_TOWN_CENTER) cap += 10;
		}
	}
	return std::min(200, cap);
}

void Simulation::spawn_initial() {
	reset();

	// 1. Athenian Town Center (2x2) at center (0,0)
	spawn_building(0, 0, BLD_TOWN_CENTER, 0, 1.0f);

	// 2. Initial Barracks (2x2) at (4, -1)
	spawn_building(4, -1, BLD_BARRACKS, 0, 1.0f);

	// 3. Initial Houses (1x1) at (-4, 3) and (-4, -3)
	spawn_building(-4, 3, BLD_HOUSE, 0, 1.0f);
	spawn_building(-4, -3, BLD_HOUSE, 0, 1.0f);

	// 4. Initial Units (6 Villagers, 2 Swordsmen, 2 Archers)
	for (int i = 0; i < 6; ++i) {
		float a = 6.28318530718f * i / 6.0f;
		float x = cosf(a) * 4.5f;
		float z = sinf(a) * 4.5f + 4.0f;
		spawn_unit(x, z, 0, UNIT_VILLAGER, 100.0f);
	}
	spawn_unit(7.0f, 2.0f, 0, UNIT_SWORDSMAN, 160.0f);
	spawn_unit(7.0f, -2.0f, 0, UNIT_SWORDSMAN, 160.0f);
	spawn_unit(6.0f, 4.0f, 0, UNIT_ARCHER, 90.0f);
	spawn_unit(6.0f, -4.0f, 0, UNIT_ARCHER, 90.0f);

	// 5. Rich Resource Fields (Wood, Gold, Stone, Food)
	// Near Base Resources
	spawn_resource(5, 5, RES_WOOD);     // Tree grove north-east
	spawn_resource(6, 6, RES_WOOD);
	spawn_resource(7, 5, RES_WOOD);

	spawn_resource(-5, 6, RES_WOOD);    // Pine forest north-west
	spawn_resource(-6, 6, RES_WOOD);
	spawn_resource(-7, 7, RES_WOOD);

	spawn_resource(5, -6, RES_GOLD);    // Gold vein south-east
	spawn_resource(6, -6, RES_GOLD);

	spawn_resource(-6, -6, RES_STONE);  // Stone mine south-west
	spawn_resource(-7, -6, RES_STONE);

	// Outer scatter
	for (int i = 0; i < 36; ++i) {
		int cx = (int)((i * 37 + 13) % 27) - 13;
		int cy = (int)((i * 53 + 7) % 27) - 13;
		if (std::abs(cx) < 4 && std::abs(cy) < 4) continue; // keep base clear
		uint8_t rtype = (i % 9 == 0) ? RES_GOLD : ((i % 7 == 0) ? RES_STONE : RES_WOOD);
		spawn_resource(cx * 2, cy * 2, rtype);
	}
}

uint32_t Simulation::spawn_unit(float x, float z, uint8_t team, uint8_t u_type, float hp) {
	uint32_t uid = alloc_id();
	size_t idx = units_.push(x, z, uid, team, u_type);
	units_.hp[idx] = units_.max_hp[idx] = hp;
	unit_id_to_idx_[uid] = idx;
	return uid;
}

uint32_t Simulation::spawn_resource(int cell_x, int cell_y, uint8_t type) {
	uint32_t rid = alloc_id();
	float x = (cell_x + 0.5f) * GRID;
	float z = (cell_y + 0.5f) * GRID;
	int32_t amt = (type == RES_WOOD) ? 400 : ((type == RES_GOLD) ? 600 : ((type == RES_STONE) ? 500 : 300));
	size_t idx = resources_.push(x, z, amt, type, rid);
	res_id_to_idx_[rid] = idx;
	return rid;
}

uint32_t Simulation::spawn_building(int cell_x, int cell_y, uint8_t type, uint8_t team, float progress) {
	uint32_t bid = alloc_id();
	int footprint = (type == BLD_TOWN_CENTER || type == BLD_BARRACKS) ? 2 : 1;
	float half = footprint * GRID * 0.5f;
	float x = (cell_x + footprint * 0.5f) * GRID;
	float z = (cell_y + footprint * 0.5f) * GRID;

	float max_h = 500.0f;
	if (type == BLD_TOWN_CENTER) max_h = 2400.0f;
	else if (type == BLD_BARRACKS) max_h = 1400.0f;
	else if (type == BLD_TOWER) max_h = 900.0f;
	else if (type == BLD_STOREHOUSE || type == BLD_FARMSTEAD) max_h = 750.0f;

	Building b;
	b.id = bid;
	b.x = x;
	b.z = z;
	b.half_size = half;
	b.type = type;
	b.team = team;
	b.hp = max_h * progress;
	b.max_hp = max_h;
	b.build_progress = progress;
	b.cell_x = cell_x;
	b.cell_y = cell_y;
	b.footprint = footprint;
	b.production_timer = 0.0f;
	b.production_unit_type = 255;
	b.attack_timer = 0.0f;

	buildings_.push_back(b);
	building_id_to_idx_[bid] = buildings_.size() - 1;

	for (int dx = 0; dx < footprint; ++dx) {
		for (int dy = 0; dy < footprint; ++dy) {
			occupied_cells_.insert({cell_x + dx, cell_y + dy});
		}
	}
	return bid;
}

bool Simulation::can_place(int cell_x, int cell_y, int footprint) const {
	int limit = (int)(MAP_HALF / GRID) - footprint;
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
	int footprint = (b_type == BLD_TOWN_CENTER || b_type == BLD_BARRACKS) ? 2 : 1;
	if (!can_place(cell_x, cell_y, footprint)) return false;

	// Check costs
	int cost_w = 0, cost_s = 0, cost_g = 0;
	if (b_type == BLD_HOUSE) { cost_w = 50; }
	else if (b_type == BLD_BARRACKS) { cost_w = 120; cost_s = 50; }
	else if (b_type == BLD_TOWER) { cost_w = 80; cost_s = 60; }
	else if (b_type == BLD_STOREHOUSE) { cost_w = 60; }
	else if (b_type == BLD_FARMSTEAD) { cost_w = 60; }
	else if (b_type == BLD_TOWN_CENTER) { cost_w = 300; cost_s = 150; }

	if (wood_ < cost_w || stone_ < cost_s || gold_ < cost_g) return false;

	wood_ -= cost_w;
	stone_ -= cost_s;
	gold_ -= cost_g;

	// Spawn building foundation (0% progress)
	spawn_building(cell_x, cell_y, b_type, 0, 0.05f);
	return true;
}

bool Simulation::cmd_train(uint32_t building_id, uint8_t u_type) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return false;
	Building &b = buildings_[bit->second];
	if (b.team != 0 || b.build_progress < 1.0f || b.production_unit_type != 255) return false;

	if (pop_current() >= pop_max()) return false;

	int cost_f = 0, cost_w = 0, cost_g = 0;
	if (u_type == UNIT_VILLAGER) { cost_f = 50; }
	else if (u_type == UNIT_SWORDSMAN) { cost_g = 60; cost_w = 30; }
	else if (u_type == UNIT_ARCHER) { cost_g = 40; cost_w = 50; }
	else if (u_type == UNIT_SPEARMAN) { cost_w = 50; cost_f = 30; }
	else if (u_type == UNIT_CAVALRY) { cost_g = 80; cost_f = 50; }

	if (food_ < cost_f || wood_ < cost_w || gold_ < cost_g) return false;

	food_ -= cost_f;
	wood_ -= cost_w;
	gold_ -= cost_g;

	b.production_unit_type = u_type;
	b.production_timer = (u_type == UNIT_VILLAGER) ? 6.0f : ((u_type == UNIT_CAVALRY) ? 10.0f : 8.0f);
	return true;
}

void Simulation::cmd_move(const uint32_t *unit_ids, size_t count, float tx, float tz) {
	static const float formation_offsets[12][2] = {
		{0.0f, 0.0f}, {1.6f, 0.0f}, {-1.6f, 0.0f}, {0.0f, 1.6f}, {0.0f, -1.6f},
		{1.6f, 1.6f}, {-1.6f, 1.6f}, {1.6f, -1.6f}, {-1.6f, -1.6f},
		{3.2f, 0.0f}, {-3.2f, 0.0f}, {0.0f, 3.2f}
	};
	for (size_t i = 0; i < count; ++i) {
		auto it = unit_id_to_idx_.find(unit_ids[i]);
		if (it == unit_id_to_idx_.end()) continue;
		size_t idx = it->second;
		if (idx >= units_.count || !units_.alive[idx]) continue;

		float ox = formation_offsets[i % 12][0];
		float oz = formation_offsets[i % 12][1];
		units_.target_x[idx] = tx + ox;
		units_.target_z[idx] = tz + oz;
		units_.has_order[idx] = 1;
		units_.action_state[idx] = ACTION_MOVE;
		units_.target_resource_id[idx] = 0;
		units_.target_unit_id[idx] = 0;
		units_.target_building_id[idx] = 0;
	}
}

void Simulation::order_gather(const uint32_t *unit_ids, size_t count, uint32_t resource_id) {
	auto rit = res_id_to_idx_.find(resource_id);
	if (rit == res_id_to_idx_.end()) return;
	size_t ridx = rit->second;
	float rx = resources_.x[ridx];
	float rz = resources_.z[ridx];

	for (size_t i = 0; i < count; ++i) {
		auto uit = unit_id_to_idx_.find(unit_ids[i]);
		if (uit == unit_id_to_idx_.end()) continue;
		size_t uidx = uit->second;
		if (units_.team[uidx] != 0 || !units_.alive[uidx]) continue;

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
	auto tit = unit_id_to_idx_.find(target_unit_id);
	if (tit == unit_id_to_idx_.end()) return;
	size_t tidx = tit->second;
	float tx = units_.pos_x[tidx];
	float tz = units_.pos_z[tidx];

	for (size_t i = 0; i < count; ++i) {
		auto uit = unit_id_to_idx_.find(unit_ids[i]);
		if (uit == unit_id_to_idx_.end()) continue;
		size_t uidx = uit->second;
		if (units_.team[uidx] != 0 || !units_.alive[uidx]) continue;

		units_.target_x[uidx] = tx;
		units_.target_z[uidx] = tz;
		units_.has_order[uidx] = 1;
		units_.action_state[uidx] = ACTION_ATTACK;
		units_.target_unit_id[uidx] = target_unit_id;
		units_.target_resource_id[uidx] = 0;
		units_.target_building_id[uidx] = 0;
	}
}

void Simulation::order_build_or_repair(const uint32_t *unit_ids, size_t count, uint32_t building_id) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return;
	const Building &b = buildings_[bit->second];

	for (size_t i = 0; i < count; ++i) {
		auto uit = unit_id_to_idx_.find(unit_ids[i]);
		if (uit == unit_id_to_idx_.end()) continue;
		size_t uidx = uit->second;
		if (units_.team[uidx] != 0 || units_.unit_type[uidx] != UNIT_VILLAGER || !units_.alive[uidx]) continue;

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
		auto uit = unit_id_to_idx_.find(unit_ids[i]);
		if (uit != unit_id_to_idx_.end()) {
			units_.stance[uit->second] = stance;
		}
	}
}

void Simulation::fire_arrow(float sx, float sy, float sz, float tx, float ty, float tz, float damage, uint8_t team, uint32_t target_id) {
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

	move_units(dt);
	separation(dt, arena);
	tick_gather(dt);
	tick_combat(dt);
	tick_construction(dt);
	tick_production(dt);
	tick_projectiles(dt);
	tick_waves(dt);

	// Passive income (e.g. farms/citizens)
	passive_timer_ += dt;
	if (passive_timer_ >= 1.0f) {
		passive_timer_ = 0.0f;
		food_ += 2;
		wood_ += 2;
	}

	frame_arena_.flip();
	auto t1 = std::chrono::high_resolution_clock::now();
	tick_us_ += std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
	ticks_++;
}

void Simulation::move_units(float dt) {
	const size_t n = units_.count;
	float *px = units_.pos_x;
	float *pz = units_.pos_z;
	float *vx = units_.vel_x;
	float *vz = units_.vel_z;
	float *tx = units_.target_x;
	float *tz = units_.target_z;
	uint8_t *has = units_.has_order;
	uint8_t *alive = units_.alive;

	for (size_t i = 0; i < n; ++i) {
		if (!alive[i]) {
			vx[i] = vz[i] = 0;
			if (units_.action_state[i] == ACTION_DYING) {
				units_.death_timer[i] += dt;
			}
			continue;
		}
		if (!has[i]) { vx[i] = vz[i] = 0; continue; }

		// Dynamic target tracking when chasing unit
		if (units_.target_unit_id[i] != 0) {
			auto tit = unit_id_to_idx_.find(units_.target_unit_id[i]);
			if (tit != unit_id_to_idx_.end() && units_.alive[tit->second]) {
				tx[i] = px[tit->second];
				tz[i] = pz[tit->second];
			}
		}

		float dx = tx[i] - px[i];
		float dz = tz[i] - pz[i];
		float d2 = dx * dx + dz * dz;
		float stop_dist = (units_.action_state[i] == ACTION_ATTACK) ? (units_.attack_range[i] * 0.85f) : ARRIVE_DIST;

		if (d2 <= stop_dist * stop_dist) {
			vx[i] = 0; vz[i] = 0;
			if (units_.action_state[i] == ACTION_MOVE) {
				has[i] = 0;
				units_.action_state[i] = ACTION_IDLE;
			}
			continue;
		}

		float inv = 1.0f / sqrtf(d2);
		float ux = dx * inv;
		float uz = dz * inv;
		float spd = units_.speed[i];
		float nx = px[i] + ux * spd * dt;
		float nz = pz[i] + uz * spd * dt;
		nx = std::clamp(nx, -MAP_HALF, MAP_HALF);
		nz = std::clamp(nz, -MAP_HALF, MAP_HALF);
		px[i] = nx; pz[i] = nz;
		vx[i] = ux * spd; vz[i] = uz * spd;
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

	const float min_dist = UNIT_RADIUS * 2.0f;
	const float min_dist_sq = min_dist * min_dist;

	for (size_t i = 0; i < n; ++i) {
		if (!units_.alive[i]) continue;
		for (size_t j = i + 1; j < n; ++j) {
			if (!units_.alive[j]) continue;
			float dx = px[i] - px[j];
			float dz = pz[i] - pz[j];
			float d2 = dx * dx + dz * dz;
			if (d2 > 0.0001f && d2 < min_dist_sq) {
				float dist = sqrtf(d2);
				float overlap = (min_dist - dist) / dist;
				float push_x = dx * overlap * 0.5f;
				float push_z = dz * overlap * 0.5f;
				fx[i] += push_x; fz[i] += push_z;
				fx[j] -= push_x; fz[j] -= push_z;
			}
		}
	}

	for (size_t i = 0; i < n; ++i) {
		if (!units_.alive[i]) continue;
		px[i] = std::clamp(px[i] + fx[i] * dt * 4.0f, -MAP_HALF, MAP_HALF);
		pz[i] = std::clamp(pz[i] + fz[i] * dt * 4.0f, -MAP_HALF, MAP_HALF);
	}
}

void Simulation::tick_gather(float dt) {
	const size_t n = units_.count;
	for (size_t i = 0; i < n; ++i) {
		if (!units_.alive[i] || units_.team[i] != 0 || units_.unit_type[i] != UNIT_VILLAGER) continue;

		// 1. Returning to dropsite
		if (units_.carry[i] >= units_.carry_capacity[i] || units_.action_state[i] == ACTION_RETURN_DROPSITE) {
			// Find nearest valid dropsite building
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
					// Deposit resources
					if (units_.res_type_carried[i] == RES_WOOD) wood_ += units_.carry[i];
					else if (units_.res_type_carried[i] == RES_GOLD) gold_ += units_.carry[i];
					else if (units_.res_type_carried[i] == RES_STONE) stone_ += units_.carry[i];
					else if (units_.res_type_carried[i] == RES_FOOD) food_ += units_.carry[i];

					units_.carry[i] = 0;
					units_.action_state[i] = ACTION_GATHER;

					// Return to harvest if target resource still valid
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
			if (dx * dx + dz * dz < 2.0f * 2.0f) {
				units_.attack_timer[i] += dt;
				if (units_.attack_timer[i] >= 0.75f) {
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

	// Clean up empty resources
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
	const size_t n = units_.count;
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
		if (dx * dx + dz * dz < (b.half_size + 1.5f) * (b.half_size + 1.5f)) {
			if (b.build_progress < 1.0f) {
				b.build_progress = std::min(1.0f, b.build_progress + dt * 0.15f);
				b.hp = b.max_hp * b.build_progress;
				if (b.build_progress >= 1.0f) {
					units_.action_state[i] = ACTION_IDLE;
					units_.target_building_id[i] = 0;
				}
			} else if (b.hp < b.max_hp) {
				b.hp = std::min(b.max_hp, b.hp + dt * 25.0f);
			}
		}
	}
}

void Simulation::tick_production(float dt) {
	for (auto &b : buildings_) {
		if (b.team != 0 || b.build_progress < 1.0f || b.production_unit_type == 255) continue;
		b.production_timer -= dt;
		if (b.production_timer <= 0.0f) {
			uint8_t utype = b.production_unit_type;
			b.production_unit_type = 255;
			b.production_timer = 0.0f;
			// Spawn unit next to building
			float sx = b.x + (b.footprint + 0.5f) * GRID;
			float sz = b.z;
			spawn_unit(sx, sz, 0, utype);
		}
	}
}

void Simulation::tick_combat(float dt) {
	const size_t n = units_.count;
	for (size_t i = 0; i < n; ++i) {
		if (units_.attack_timer[i] > 0) units_.attack_timer[i] -= dt;
	}

	// 1. Units combat
	for (size_t i = 0; i < n; ++i) {
		if (!units_.alive[i] || units_.attack_timer[i] > 0) continue;

		size_t target_idx = (size_t)-1;
		float best_d2 = units_.attack_range[i] * units_.attack_range[i];

		// If has explicit target unit
		if (units_.target_unit_id[i] != 0) {
			auto tit = unit_id_to_idx_.find(units_.target_unit_id[i]);
			if (tit != unit_id_to_idx_.end() && units_.alive[tit->second]) {
				float dx = units_.pos_x[i] - units_.pos_x[tit->second];
				float dz = units_.pos_z[i] - units_.pos_z[tit->second];
				if (dx * dx + dz * dz <= best_d2) {
					target_idx = tit->second;
				}
			}
		}

		// Auto-aggro within range
		if (target_idx == (size_t)-1 && units_.stance[i] != 2) {
			float scan_range = (units_.unit_type[i] == UNIT_ARCHER) ? 14.0f : ((units_.unit_type[i] == UNIT_VILLAGER) ? 3.0f : 8.0f);
			float best_scan_d2 = scan_range * scan_range;
			for (size_t j = 0; j < n; ++j) {
				if (!units_.alive[j] || units_.team[i] == units_.team[j]) continue;
				float dx = units_.pos_x[i] - units_.pos_x[j];
				float dz = units_.pos_z[i] - units_.pos_z[j];
				float d2 = dx * dx + dz * dz;
				if (d2 < best_scan_d2) {
					best_scan_d2 = d2;
					target_idx = j;
				}
			}
		}

		if (target_idx != (size_t)-1) {
			float dx = units_.pos_x[i] - units_.pos_x[target_idx];
			float dz = units_.pos_z[i] - units_.pos_z[target_idx];
			float d2 = dx * dx + dz * dz;

			if (d2 <= units_.attack_range[i] * units_.attack_range[i]) {
				units_.attack_timer[i] = units_.attack_cooldown[i];
				units_.action_state[i] = ACTION_ATTACK;

				if (units_.unit_type[i] == UNIT_ARCHER) {
					// Ranged attack — fire ballistic arrow
					fire_arrow(units_.pos_x[i], 1.4f, units_.pos_z[i],
					           units_.pos_x[target_idx], 0.8f, units_.pos_z[target_idx],
					           units_.attack_damage[i], units_.team[i], units_.id[target_idx]);
				} else {
					// Melee strike
					units_.hp[target_idx] -= units_.attack_damage[i];
					if (units_.hp[target_idx] <= 0) {
						units_.alive[target_idx] = 0;
						units_.action_state[target_idx] = ACTION_DYING;
					}
				}
			}
		}
	}

	// 2. Defensive Buildings attack (Town Center & Tower)
	for (auto &b : buildings_) {
		if (b.build_progress < 1.0f) continue;
		if (b.type == BLD_TOWN_CENTER || b.type == BLD_TOWER) {
			b.attack_timer -= dt;
			if (b.attack_timer <= 0.0f) {
				float range = (b.type == BLD_TOWER) ? 15.0f : 14.0f;
				float range_sq = range * range;
				for (size_t i = 0; i < n; ++i) {
					if (!units_.alive[i] || units_.team[i] == b.team) continue;
					float dx = b.x - units_.pos_x[i];
					float dz = b.z - units_.pos_z[i];
					if (dx * dx + dz * dz <= range_sq) {
						b.attack_timer = (b.type == BLD_TOWER) ? 0.9f : 1.2f;
						fire_arrow(b.x, 3.5f, b.z, units_.pos_x[i], 0.8f, units_.pos_z[i], 18.0f, b.team, units_.id[i]);
						break;
					}
				}
			}
		}
	}

	// 3. Remove dead units after death timer completes
	std::vector<size_t> dead_indices;
	for (size_t i = 0; i < units_.count; ++i) {
		if (!units_.alive[i] && units_.death_timer[i] >= 1.0f) {
			dead_indices.push_back(i);
		}
	}
	if (!dead_indices.empty()) {
		std::sort(dead_indices.rbegin(), dead_indices.rend());
		for (size_t idx : dead_indices) {
			unit_id_to_idx_.erase(units_.id[idx]);
			units_.remove_at(idx);
		}
		unit_id_to_idx_.clear();
		for (size_t k = 0; k < units_.count; ++k) unit_id_to_idx_[units_.id[k]] = k;
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

			// Impact damage on target
			if (p.target_unit_id != 0) {
				auto tit = unit_id_to_idx_.find(p.target_unit_id);
				if (tit != unit_id_to_idx_.end() && units_.alive[tit->second]) {
					units_.hp[tit->second] -= p.damage;
					if (units_.hp[tit->second] <= 0) {
						units_.alive[tit->second] = 0;
						units_.action_state[tit->second] = ACTION_DYING;
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

	// Remove finished projectiles
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
		float edge = MAP_HALF - 2.0f;
		float x = cosf(angle) * edge;
		float z = sinf(angle) * edge;

		uint8_t utype = UNIT_SWORDSMAN;
		if (i < base_archers) utype = UNIT_ARCHER;
		else if (i >= total - base_cavalry) utype = UNIT_CAVALRY;

		uint32_t uid = spawn_unit(x, z, 1, utype);
		size_t uidx = unit_id_to_idx_[uid];
		units_.target_x[uidx] = 0.0f;
		units_.target_z[uidx] = 0.0f;
		units_.has_order[uidx] = 1;
		units_.action_state[uidx] = ACTION_ATTACK;
	}
}

Simulation::UnitView Simulation::units() const {
	return {
		units_.pos_x, units_.pos_z,
		units_.vel_x, units_.vel_z,
		units_.hp, units_.max_hp,
		units_.id,
		units_.alive, units_.team, units_.unit_type, units_.action_state, units_.res_type_carried,
		units_.carry,
		units_.death_timer,
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

} // namespace rtso
