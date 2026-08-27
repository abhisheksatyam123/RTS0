#include "simulation.h"
#include "../core/job_system.h"
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <cstdlib>

namespace rtso {

namespace {
constexpr int kUnitGridRes = 40;

struct UnitGrid {
	int32_t *head = nullptr;
	int32_t *nxt = nullptr;
	int res = kUnitGridRes;
	float inv = 0.0f;

	int cell_of(float x, float z) const {
		int cx = std::clamp((int)((x + Simulation::MAP_HALF) * inv), 0, res - 1);
		int cz = std::clamp((int)((z + Simulation::MAP_HALF) * inv), 0, res - 1);
		return cz * res + cx;
	}

	bool build(Arena &arena, size_t n, const float *px, const float *pz, const uint8_t *alive) {
		inv = (float)res / (Simulation::MAP_HALF * 2.0f);
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
		int cx = std::clamp((int)((x + Simulation::MAP_HALF) * inv), 0, res - 1);
		int cz = std::clamp((int)((z + Simulation::MAP_HALF) * inv), 0, res - 1);
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
	return std::min(2000, cap);
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
	spawn_resource(5, -5, RES_WOOD);
	spawn_resource(6, -5, RES_WOOD);
	spawn_resource(7, -5, RES_WOOD);

	spawn_resource(-5, -5, RES_WOOD);
	spawn_resource(-6, -5, RES_WOOD);
	spawn_resource(-7, -6, RES_WOOD);

	spawn_resource(5, -6, RES_GOLD);    // Gold vein south-east
	spawn_resource(6, -6, RES_GOLD);

	spawn_resource(-6, -6, RES_STONE);  // Stone mine south-west
	spawn_resource(-7, -6, RES_STONE);

	// Outer scatter — keep the north battlefield clear so the war is not fought inside trees.
	for (int i = 0; i < 36; ++i) {
		int cx = (int)((i * 37 + 13) % 27) - 13;
		int cy = (int)((i * 53 + 7) % 27) - 13;
		if (std::abs(cx) < 4 && std::abs(cy) < 4) continue;
		float wx = (cx * 2 + 0.5f) * GRID;
		float wz = (cy * 2 + 0.5f) * GRID;
		if (wz > 2.0f && std::abs(wx) < 28.0f) continue;
		uint8_t rtype = (i % 9 == 0) ? RES_GOLD : ((i % 7 == 0) ? RES_STONE : RES_WOOD);
		spawn_resource(cx * 2, cy * 2, rtype);
	}

	if (const char *war = std::getenv("RTS0_WAR")) {
		int n = std::atoi(war);
		if (n < 2) n = 2000;
		spawn_war(n);
	}
}

void Simulation::spawn_war(int total_units) {
	total_units = std::clamp(total_units, 2, (int)UNIT_CAP - 32);
	const int half = total_units / 2;
	const float spacing = 1.15f;
	const float ox = 13.0f;
	const float oz = 6.0f;
	int cols = std::max(1, (int)std::ceil(std::sqrt((float)half)));
	auto fits = [&](int c) {
		int r = (half + c - 1) / c;
		float hw = c * 0.5f * spacing;
		float hd = r * 0.5f * spacing;
		return ox + hw < MAP_HALF - 1.5f && std::abs(oz) + hd < MAP_HALF - 1.5f;
	};
	while (cols > 6 && !fits(cols)) cols--;
	std::vector<uint32_t> left;
	std::vector<uint32_t> right;
	left.reserve((size_t)half);
	right.reserve((size_t)half);
	auto deploy = [&](uint8_t team, float cx, float cz, std::vector<uint32_t> &ids) {
		for (int i = 0; i < half; ++i) {
			int row = i / cols;
			int col = i % cols;
			float x = std::clamp(cx + (col - cols * 0.5f) * spacing, -MAP_HALF + 1.5f, MAP_HALF - 1.5f);
			float z = std::clamp(cz + (row - cols * 0.5f) * spacing, -MAP_HALF + 1.5f, MAP_HALF - 1.5f);
			UnitType typ = UNIT_SWORDSMAN;
			if (i % 5 == 0) typ = UNIT_ARCHER;
			else if (i % 9 == 0) typ = UNIT_CAVALRY;
			else if (i % 4 == 0) typ = UNIT_SPEARMAN;
			uint32_t uid = spawn_unit(x, z, team, typ);
			int32_t idx = lookup_unit(uid);
			if (idx < 0) continue;
			units_.has_order[idx] = 1;
			units_.action_state[idx] = ACTION_ATTACK;
			units_.stance[idx] = STANCE_AGGRESSIVE;
			ids.push_back(uid);
		}
	};
	deploy(0, -ox, oz, left);
	deploy(1, ox, oz, right);
	const size_t pairs = std::min(left.size(), right.size());
	for (size_t i = 0; i < pairs; ++i) {
		int32_t a = lookup_unit(left[i]);
		int32_t b = lookup_unit(right[i]);
		if (a >= 0) {
			units_.target_unit_id[a] = right[i];
			units_.target_x[a] = units_.pos_x[b >= 0 ? b : a];
			units_.target_z[a] = units_.pos_z[b >= 0 ? b : a];
		}
		if (b >= 0) {
			units_.target_unit_id[b] = left[i];
			units_.target_x[b] = units_.pos_x[a >= 0 ? a : b];
			units_.target_z[b] = units_.pos_z[a >= 0 ? a : b];
		}
	}
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
	bind_unit_index(uid, idx);
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
	b.production_unit_type = UNIT_TYPE_NONE;
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

bool Simulation::cmd_train(uint32_t building_id, UnitType u_type) {
	auto bit = building_id_to_idx_.find(building_id);
	if (bit == building_id_to_idx_.end()) return false;
	Building &b = buildings_[bit->second];
	if (b.team != 0 || b.build_progress < 1.0f || b.production_unit_type != UNIT_TYPE_NONE) return false;

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
	int side = std::max(1, (int)std::ceil(std::sqrt((float)std::max<size_t>(count, 1))));
	const float spacing = 1.35f;
	for (size_t i = 0; i < count; ++i) {
		auto it = unit_id_to_idx_.find(unit_ids[i]);
		if (it == unit_id_to_idx_.end()) continue;
		size_t idx = it->second;
		if (idx >= units_.count || !units_.alive[idx]) continue;

		int row = (int)i / side;
		int col = (int)i % side;
		float ox = (col - (side - 1) * 0.5f) * spacing;
		float oz = (row - (side - 1) * 0.5f) * spacing;
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

	// Two-phase: read current positions for chase, write into scratch, then commit.
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
			stop_dist = std::max(0.35f, units_.attack_range[i] * 0.45f);
		}

		if (d2 <= stop_dist * stop_dist) {
			vx[i] = 0;
			vz[i] = 0;
			if (units_.action_state[i] == ACTION_MOVE) {
				has[i] = 0;
				units_.action_state[i] = ACTION_IDLE;
			}
			return;
		}

		float inv = 1.0f / sqrtf(d2);
		float ux = dx * inv;
		float uz = dz * inv;
		float spd = units_.speed[i];
		npx[i] = std::clamp(px[i] + ux * spd * dt, -MAP_HALF, MAP_HALF);
		npz[i] = std::clamp(pz[i] + uz * spd * dt, -MAP_HALF, MAP_HALF);
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
	if (!grid.build(arena, n, px, pz, units_.alive)) return;

	const float ally_dist = 1.15f;
	const float ally_dist_sq = ally_dist * ally_dist;
	const float foe_dist = 0.40f;
	const float foe_dist_sq = foe_dist * foe_dist;

	JobSystem::instance().for_each_index(n, [&](size_t i) {
		if (!units_.alive[i]) return;
		float pxi = px[i];
		float pzi = pz[i];
		float ax = 0.0f;
		float az = 0.0f;
		const uint8_t my_team = units_.team[i];
		const uint32_t my_target = units_.target_unit_id[i];
		grid.visit_radius(pxi, pzi, ally_dist, [&](int32_t j) {
			if ((size_t)j == i) return;
			if (units_.id[j] == my_target) return;
			float dx = pxi - px[j];
			float dz = pzi - pz[j];
			float d2 = dx * dx + dz * dz;
			const bool ally = units_.team[j] == my_team;
			const float lim_sq = ally ? ally_dist_sq : foe_dist_sq;
			const float lim = ally ? ally_dist : foe_dist;
			if (d2 > 0.0001f && d2 < lim_sq) {
				float dist = sqrtf(d2);
				float overlap = (lim - dist) / dist;
				float w = ally ? 0.85f : 0.25f;
				ax += dx * overlap * w;
				az += dz * overlap * w;
			}
		});
		fx[i] = ax;
		fz[i] = az;
	});

	JobSystem::instance().for_each_index(n, [&](size_t i) {
		if (!units_.alive[i]) return;
		px[i] = std::clamp(px[i] + fx[i] * dt * 6.0f, -MAP_HALF, MAP_HALF);
		pz[i] = std::clamp(pz[i] + fz[i] * dt * 6.0f, -MAP_HALF, MAP_HALF);
	});
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
		if (b.team != 0 || b.build_progress < 1.0f || b.production_unit_type == UNIT_TYPE_NONE) continue;
		b.production_timer -= dt;
		if (b.production_timer <= 0.0f) {
			uint8_t utype = b.production_unit_type;
			b.production_unit_type = UNIT_TYPE_NONE;
			b.production_timer = 0.0f;
			// Spawn unit next to building
			float sx = b.x + (b.footprint + 0.5f) * GRID;
			float sz = b.z;
			spawn_unit(sx, sz, 0, (UnitType)utype);
		}
	}
}

void Simulation::tick_combat(float dt) {
	const size_t n = units_.count;
	Arena &arena = frame_arena_.current();
	int32_t *target_idx = n ? arena.alloc_array<int32_t>(n) : nullptr;
	if (n && !target_idx) return;

	UnitGrid grid;
	if (n && !grid.build(arena, n, units_.pos_x, units_.pos_z, units_.alive)) return;

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

		if (found < 0 && units_.stance[i] != STANCE_STAND_GROUND) {
			float scan_range = unit_type_info(units_.unit_type[i]).aggro_range;
			if (units_.action_state[i] == ACTION_ATTACK) {
				scan_range = std::max(scan_range, 20.0f);
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
		if (found >= 0) {
			units_.target_unit_id[i] = units_.id[found];
			units_.has_order[i] = 1;
			units_.target_x[i] = units_.pos_x[found];
			units_.target_z[i] = units_.pos_z[found];
			if (units_.unit_type[i] == UNIT_VILLAGER &&
					(units_.action_state[i] == ACTION_GATHER || units_.action_state[i] == ACTION_BUILD ||
							units_.action_state[i] == ACTION_RETURN_DROPSITE)) {
				// leave villagers on their job
			} else {
				units_.action_state[i] = ACTION_ATTACK;
			}
		} else {
			units_.target_unit_id[i] = 0;
		}
		target_idx[i] = found;
	});

	// Apply damage / arrows on the sim thread (shared HP, projectile list).
	for (size_t i = 0; i < n; ++i) {
		int32_t t = target_idx[i];
		if (t < 0 || !units_.alive[i] || units_.attack_timer[i] > 0) continue;
		float dx = units_.pos_x[i] - units_.pos_x[t];
		float dz = units_.pos_z[i] - units_.pos_z[t];
		float d2 = dx * dx + dz * dz;
		if (d2 > units_.attack_range[i] * units_.attack_range[i]) continue;

		units_.attack_timer[i] = units_.attack_cooldown[i];
		units_.action_state[i] = ACTION_ATTACK;

		if (units_.unit_type[i] == UNIT_ARCHER) {
			fire_arrow(units_.pos_x[i], 1.4f, units_.pos_z[i],
			           units_.pos_x[t], 0.8f, units_.pos_z[t],
			           units_.attack_damage[i], units_.team[i], units_.id[t]);
		} else {
			units_.hp[t] -= units_.attack_damage[i];
			if (units_.hp[t] <= 0) {
				units_.alive[t] = 0;
				units_.action_state[t] = ACTION_DYING;
			}
		}
	}

	// 2. Defensive Buildings attack (Town Center & Tower)
	for (auto &b : buildings_) {
		if (b.build_progress < 1.0f) continue;
		if (b.type == BLD_TOWN_CENTER || b.type == BLD_TOWER) {
			b.attack_timer -= dt;
				if (b.attack_timer <= 0.0f && n) {
				float range = (b.type == BLD_TOWER) ? 15.0f : 14.0f;
				float range_sq = range * range;
				int32_t hit = -1;
				grid.visit_radius(b.x, b.z, range, [&](int32_t i) {
					if (hit >= 0 || !units_.alive[i] || units_.team[i] == b.team) return;
					float dx = b.x - units_.pos_x[i];
					float dz = b.z - units_.pos_z[i];
					if (dx * dx + dz * dz <= range_sq) hit = i;
				});
				if (hit >= 0) {
					b.attack_timer = (b.type == BLD_TOWER) ? 0.9f : 1.2f;
					fire_arrow(b.x, 3.5f, b.z, units_.pos_x[hit], 0.8f, units_.pos_z[hit], 18.0f, b.team, units_.id[hit]);
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

			// Impact damage on target
			if (p.target_unit_id != 0) {
				int32_t tidx = lookup_unit(p.target_unit_id);
				if (tidx >= 0 && units_.alive[tidx]) {
					units_.hp[tidx] -= p.damage;
					if (units_.hp[tidx] <= 0) {
						units_.alive[tidx] = 0;
						units_.action_state[tidx] = ACTION_DYING;
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

		uint32_t uid = spawn_unit(x, z, 1, (UnitType)utype);
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
