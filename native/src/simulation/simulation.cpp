#include "simulation.h"
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace rtso {

Simulation::Simulation() {
	units_.init(1024);
	resources_.init(256);
}

Simulation::~Simulation() = default;

void Simulation::reset() {
	units_.free_all(); units_.init(1024);
	resources_.free_all(); resources_.init(256);
	buildings_.clear();
	unit_id_to_idx_.clear();
	res_id_to_idx_.clear();
	occupied_cells_.clear();
	next_id_ = 1;
	gold_ = 150;
	gold_carry_ = 0.0f;
	wave_timer_ = 0.0f;
	wave_number_ = 0;
	tick_us_ = ticks_ = 0;
	frame_arena_.reset_all();
}

void Simulation::spawn_initial() {
	reset();

	// Town Center footprint 2x2 at (0,0) cell
	uint32_t tc_id = alloc_id();
	buildings_.push_back({tc_id, 0.0f, 0.0f, 2.0f, true, 0, 0});
	occupied_cells_.insert({0, 0});
	occupied_cells_.insert({1, 0});
	occupied_cells_.insert({0, 1});
	occupied_cells_.insert({1, 1});

	// 8 villagers around
	for (int i = 0; i < 8; ++i) {
		float a = 6.28318530718f * i / 8.0f;
		float x = cosf(a) * 5.0f;
		float z = sinf(a) * 5.0f + 7.0f;
		spawn_unit(x, z, 0, 100.0f);
	}

	// Resource nodes scatter
	spawn_resource(5, 4, 0);   // tree
	spawn_resource(-6, 5, 0);  // tree
	spawn_resource(4, -6, 1);  // gold
	spawn_resource(-5, -5, 1); // gold
}

uint32_t Simulation::spawn_unit(float x, float z, uint8_t team, float hp) {
	uint32_t uid = alloc_id();
	size_t idx = units_.push(x, z, uid, team);
	units_.hp[idx] = units_.max_hp[idx] = hp;
	unit_id_to_idx_[uid] = idx;
	return uid;
}

uint32_t Simulation::spawn_resource(int cell_x, int cell_y, uint8_t type) {
	uint32_t rid = alloc_id();
	float x = cell_x * GRID;
	float z = cell_y * GRID;
	int32_t amt = (type == 0) ? 300 : 500;
	size_t idx = resources_.push(x, z, amt, type, rid);
	res_id_to_idx_[rid] = idx;
	return rid;
}

bool Simulation::can_place(int cell_x, int cell_y, bool is_center) const {
	int footprint = is_center ? 2 : 1;
	int limit = (int)(MAP_HALF / GRID) - footprint;
	for (int x = 0; x < footprint; ++x) {
		for (int y = 0; y < footprint; ++y) {
			int cx = cell_x + x;
			int cy = cell_y + y;
			if (std::abs(cx) > limit || std::abs(cy) > limit) return false;
			if (occupied_cells_.find({cx, cy}) != occupied_cells_.end()) return false;
		}
	}
	return true;
}

void Simulation::cmd_move(const uint32_t *unit_ids, size_t count, float tx, float tz) {
	static const float formation_offsets[10][2] = {
		{0.0f, 0.0f}, {1.6f, 0.0f}, {-1.6f, 0.0f}, {0.0f, 1.6f}, {0.0f, -1.6f},
		{2.2f, 2.2f}, {-2.2f, 2.2f}, {2.2f, -2.2f}, {-2.2f, -2.2f}, {0.0f, 3.2f}
	};
	for (size_t i = 0; i < count; ++i) {
		auto it = unit_id_to_idx_.find(unit_ids[i]);
		if (it == unit_id_to_idx_.end()) continue;
		size_t idx = it->second;
		if (idx >= units_.count) continue;

		float ox = formation_offsets[i % 10][0];
		float oz = formation_offsets[i % 10][1];
		units_.target_x[idx] = tx + ox;
		units_.target_z[idx] = tz + oz;
		units_.has_order[idx] = 1;
		units_.target_resource_id[idx] = 0;
		units_.target_unit_id[idx] = 0;
	}
}

bool Simulation::cmd_build_house(int cell_x, int cell_y) {
	if (gold_ < HOUSE_COST || !can_place(cell_x, cell_y, false)) return false;
	gold_ -= HOUSE_COST;

	uint32_t b_id = alloc_id();
	float x = (cell_x + 0.5f) * GRID;
	float z = (cell_y + 0.5f) * GRID;
	buildings_.push_back({b_id, x, z, 0.9f, false, cell_x, cell_y});
	occupied_cells_.insert({cell_x, cell_y});
	return true;
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
		if (units_.team[uidx] != 0) continue; // player only
		units_.target_x[uidx] = rx;
		units_.target_z[uidx] = rz;
		units_.has_order[uidx] = 1;
		units_.target_resource_id[uidx] = resource_id;
		units_.target_unit_id[uidx] = 0;
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
		if (units_.team[uidx] != 0) continue;
		units_.target_x[uidx] = tx;
		units_.target_z[uidx] = tz;
		units_.has_order[uidx] = 1;
		units_.target_unit_id[uidx] = target_unit_id;
		units_.target_resource_id[uidx] = 0;
	}
}

void Simulation::tick(float dt) {
	auto t0 = std::chrono::high_resolution_clock::now();
	Arena &arena = frame_arena_.current();
	arena.reset();

	move_units(dt);
	separation(dt, arena);
	tick_gather(dt);
	tick_combat(dt);
	tick_waves(dt);

	// Fallback gold trickle
	gold_carry_ += dt * 0.2f * units_.count;
	while (gold_carry_ >= 1.0f) {
		gold_carry_ -= 1.0f;
		gold_++;
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
		if (!alive[i] || !has[i]) { vx[i] = vz[i] = 0; continue; }

		// update target position if chasing unit
		if (units_.target_unit_id[i] != 0) {
			auto tit = unit_id_to_idx_.find(units_.target_unit_id[i]);
			if (tit != unit_id_to_idx_.end()) {
				size_t tidx = tit->second;
				tx[i] = px[tidx];
				tz[i] = pz[tidx];
			}
		}

		float dx = tx[i] - px[i];
		float dz = tz[i] - pz[i];
		float d2 = dx * dx + dz * dz;
		if (d2 < ARRIVE_DIST * ARRIVE_DIST) {
			px[i] = tx[i]; pz[i] = tz[i];
			vx[i] = vz[i] = 0;
			has[i] = 0;
			continue;
		}
		float inv = 1.0f / sqrtf(d2);
		float ux = dx * inv;
		float uz = dz * inv;
		float nx = px[i] + ux * UNIT_SPEED * dt;
		float nz = pz[i] + uz * UNIT_SPEED * dt;
		nx = std::clamp(nx, -MAP_HALF, MAP_HALF);
		nz = std::clamp(nz, -MAP_HALF, MAP_HALF);
		px[i] = nx; pz[i] = nz;
		vx[i] = ux * UNIT_SPEED; vz[i] = uz * UNIT_SPEED;
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
	// Find town center dropsite
	float tc_x = 0.0f, tc_z = 0.0f;
	bool has_tc = false;
	for (const auto &b : buildings_) {
		if (b.is_center) { tc_x = b.x; tc_z = b.z; has_tc = true; break; }
	}
	if (!has_tc) return;

	const size_t n = units_.count;
	for (size_t i = 0; i < n; ++i) {
		if (!units_.alive[i] || units_.team[i] != 0) continue;

		if (units_.carry[i] >= units_.carry_capacity[i]) {
			float dx = units_.pos_x[i] - tc_x;
			float dz = units_.pos_z[i] - tc_z;
			if (dx * dx + dz * dz < 2.5f * 2.5f) {
				gold_ += units_.carry[i];
				units_.carry[i] = 0;
			} else if (!units_.has_order[i]) {
				units_.target_x[i] = tc_x;
				units_.target_z[i] = tc_z;
				units_.has_order[i] = 1;
			}
			continue;
		}

		// Find nearest resource
		size_t best_r = (size_t)-1;
		float best_dist_sq = 1e9f;
		for (size_t r = 0; r < resources_.count; ++r) {
			if (!resources_.alive[r] || resources_.amount[r] <= 0) continue;
			float dx = units_.pos_x[i] - resources_.x[r];
			float dz = units_.pos_z[i] - resources_.z[r];
			float d2 = dx * dx + dz * dz;
			if (d2 < best_dist_sq) {
				best_dist_sq = d2;
				best_r = r;
			}
		}

		if (best_r == (size_t)-1) continue;

		if (best_dist_sq < 1.8f * 1.8f) {
			units_.attack_timer[i] += dt;
			if (units_.attack_timer[i] >= 0.8f) {
				units_.attack_timer[i] = 0.0f;
				int32_t space = units_.carry_capacity[i] - units_.carry[i];
				int32_t take = std::min(10, std::min(resources_.amount[best_r], space));
				resources_.amount[best_r] -= take;
				units_.carry[i] += take;
			}
		} else if (!units_.has_order[i]) {
			units_.target_x[i] = resources_.x[best_r];
			units_.target_z[i] = resources_.z[best_r];
			units_.has_order[i] = 1;
		}
	}

	// Clean up empty resources
	for (size_t r = 0; r < resources_.count; ) {
		if (resources_.amount[r] <= 0) {
			res_id_to_idx_.erase(resources_.id[r]);
			resources_.remove_at(r);
			// rebuild index map
			res_id_to_idx_.clear();
			for (size_t k = 0; k < resources_.count; ++k) res_id_to_idx_[resources_.id[k]] = k;
		} else {
			++r;
		}
	}
}

void Simulation::tick_combat(float dt) {
	const size_t n = units_.count;
	for (size_t i = 0; i < n; ++i) {
		if (units_.attack_timer[i] > 0) units_.attack_timer[i] -= dt;
	}

	std::vector<size_t> dead_indices;

	for (size_t i = 0; i < n; ++i) {
		if (!units_.alive[i] || units_.attack_timer[i] > 0) continue;
		size_t target_idx = (size_t)-1;
		float best_d2 = (units_.attack_range[i] + 0.5f) * (units_.attack_range[i] + 0.5f);

		for (size_t j = 0; j < n; ++j) {
			if (!units_.alive[j] || units_.team[i] == units_.team[j]) continue;
			float dx = units_.pos_x[i] - units_.pos_x[j];
			float dz = units_.pos_z[i] - units_.pos_z[j];
			float d2 = dx * dx + dz * dz;
			if (d2 < best_d2) {
				best_d2 = d2;
				target_idx = j;
			}
		}

		if (target_idx != (size_t)-1) {
			units_.hp[target_idx] -= units_.attack_damage[i];
			units_.attack_timer[i] = units_.attack_cooldown[i];
			if (units_.hp[target_idx] <= 0) {
				units_.alive[target_idx] = 0;
				dead_indices.push_back(target_idx);
			}
		}
	}

	if (!dead_indices.empty()) {
		std::sort(dead_indices.rbegin(), dead_indices.rend());
		for (size_t idx : dead_indices) {
			if (idx < units_.count && !units_.alive[idx]) {
				unit_id_to_idx_.erase(units_.id[idx]);
				units_.remove_at(idx);
			}
		}
		// Rebuild unit_id_to_idx map
		unit_id_to_idx_.clear();
		for (size_t k = 0; k < units_.count; ++k) {
			unit_id_to_idx_[units_.id[k]] = k;
		}
	}
}

void Simulation::tick_waves(float dt) {
	wave_timer_ += dt;
	if (wave_timer_ < 25.0f) return;
	wave_timer_ = 0.0f;
	wave_number_++;

	int count = 3 + wave_number_ * 2;
	for (int i = 0; i < count; ++i) {
		float angle = 6.28318530718f * (float)i / (float)count;
		float edge = MAP_HALF - 1.0f;
		float x = cosf(angle) * edge;
		float z = sinf(angle) * edge;
		uint32_t uid = spawn_unit(x, z, 1, 60.0f + wave_number_ * 10.0f);
		size_t uidx = unit_id_to_idx_[uid];
		units_.target_x[uidx] = 0.0f;
		units_.target_z[uidx] = 0.0f;
		units_.has_order[uidx] = 1;
	}
}

Simulation::UnitView Simulation::units() const {
	return {
		units_.pos_x, units_.pos_z,
		units_.vel_x, units_.vel_z,
		units_.hp, units_.max_hp,
		units_.id,
		units_.alive, units_.team,
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
