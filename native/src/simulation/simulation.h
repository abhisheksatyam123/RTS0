#ifndef RTSO_SIMULATION_H
#define RTSO_SIMULATION_H

#include "../core/memory/arena.h"
#include "../core/memory/soa_storage.h"
#include "sim_types.h"
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <utility>

namespace rtso {

struct Projectile {
	uint32_t id = 0;
	float start_x = 0, start_y = 1.0f, start_z = 0;
	float target_x = 0, target_y = 0.5f, target_z = 0;
	float current_x = 0, current_y = 0, current_z = 0;
	float progress = 0.0f;
	float speed = 26.0f;
	float arc_height = 2.5f;
	float damage = 15.0f;
	uint8_t team = 0;
	uint32_t target_unit_id = 0;
	bool alive = true;
};

class Simulation {
public:
	static constexpr float GRID = 2.0f;
	static constexpr float MAP_HALF = 32.0f;
	static constexpr float FIXED_STEP = 1.0f / 20.0f;
	static constexpr float UNIT_SPEED = 6.0f;
	static constexpr float ARRIVE_DIST = 0.4f;
	static constexpr float UNIT_RADIUS = 0.45f;
	static constexpr size_t UNIT_CAP = 4096;
	static constexpr size_t MAX_PROJECTILES = 384;

	struct Building {
		uint32_t id;
		float x, z;
		float half_size;
		uint8_t type; // BLD_*
		uint8_t team; // 0=player, 1=enemy
		float hp;
		float max_hp;
		float build_progress; // 0.0f to 1.0f (1.0f = finished)
		int cell_x, cell_y;
		int footprint; // 1 or 2
		float production_timer;
		uint8_t production_unit_type; // UNIT_TYPE_NONE = idle
		float attack_timer; // for towers / town center defense
	};

	Simulation();
	~Simulation();

	void reset();
	void spawn_initial();
	// Two armies that clash (default 500 vs 500). Bypasses pop/train costs.
	void spawn_war(int total_units = 2000);

	uint32_t spawn_unit(float x, float z, uint8_t team = 0, UnitType u_type = UNIT_VILLAGER, float hp = 100.0f);
	uint32_t spawn_resource(int cell_x, int cell_y, uint8_t type);
	uint32_t spawn_building(int cell_x, int cell_y, uint8_t type, uint8_t team = 0, float progress = 1.0f);

	// Commands
	void cmd_move(const uint32_t *unit_ids, size_t count, float tx, float tz);
	bool cmd_build(int cell_x, int cell_y, uint8_t b_type);
	bool cmd_train(uint32_t building_id, UnitType u_type);
	void order_gather(const uint32_t *unit_ids, size_t count, uint32_t resource_id);
	void order_attack(const uint32_t *unit_ids, size_t count, uint32_t target_unit_id);
	void order_build_or_repair(const uint32_t *unit_ids, size_t count, uint32_t building_id);
	void order_set_stance(const uint32_t *unit_ids, size_t count, uint8_t stance);

	bool can_place(int cell_x, int cell_y, int footprint) const;

	void tick(float dt);

	// Views
	struct UnitView {
		const float *pos_x, *pos_z;
		const float *vel_x, *vel_z;
		const float *hp, *max_hp;
		const uint32_t *id;
		const uint8_t *alive, *team, *unit_type, *action_state, *res_type_carried;
		const int32_t *carry;
		const float *death_timer;
		size_t count;
	};
	UnitView units() const;

	struct ResourceView {
		const float *x, *z;
		const int32_t *amount;
		const uint8_t *type, *alive;
		const uint32_t *id;
		size_t count;
	};
	ResourceView resources() const;

	const std::vector<Building> &buildings() const { return buildings_; }
	const std::vector<Projectile> &projectiles() const { return projectiles_; }

	int food() const { return food_; }
	int wood() const { return wood_; }
	int gold() const { return gold_; }
	int stone() const { return stone_; }
	int pop_current() const;
	int pop_max() const;
	int wave_number() const { return wave_number_; }
	float wave_time_remaining() const { return std::max(0.0f, 30.0f - wave_timer_); }
	bool is_game_over() const { return game_over_; }
	bool is_victory() const { return victory_; }

	size_t unit_count() const { return units_.count; }
	size_t building_count() const { return buildings_.size(); }
	size_t resource_count() const { return resources_.count; }

	struct Stats { uint64_t tick_us; size_t ticks; };
	Stats stats() const { return {tick_us_, ticks_}; }

private:
	void move_units(float dt, Arena &arena);
	void separation(float dt, Arena &arena);
	void tick_gather(float dt);
	void tick_combat(float dt);
	void tick_construction(float dt);
	void tick_production(float dt);
	void tick_projectiles(float dt);
	void tick_waves(float dt);
	void fire_arrow(float sx, float sy, float sz, float tx, float ty, float tz, float damage, uint8_t team, uint32_t target_id);
	void bind_unit_index(uint32_t id, size_t idx);
	int32_t lookup_unit(uint32_t id) const;

	UnitSoA units_;
	ResourceSoA resources_;
	std::vector<Building> buildings_;
	std::vector<Projectile> projectiles_;
	std::unordered_map<uint32_t, size_t> unit_id_to_idx_;
	std::vector<int32_t> unit_idx_by_id_;
	std::unordered_map<uint32_t, size_t> res_id_to_idx_;
	std::unordered_map<uint32_t, size_t> building_id_to_idx_;

	struct CellHash {
		size_t operator()(const std::pair<int, int> &p) const {
			return (size_t)p.first * 73856093 ^ (size_t)p.second * 19349663;
		}
	};
	std::unordered_set<std::pair<int, int>, CellHash> occupied_cells_;

	uint32_t next_id_ = 1;
	int food_ = 200;
	int wood_ = 300;
	int gold_ = 200;
	int stone_ = 100;
	float passive_timer_ = 0.0f;

	float wave_timer_ = 0.0f;
	int wave_number_ = 0;
	bool game_over_ = false;
	bool victory_ = false;

	uint64_t tick_us_ = 0;
	uint64_t ticks_ = 0;
	FrameArena frame_arena_{4 * 1024 * 1024};

	uint32_t alloc_id() { return next_id_++; }
};

} // namespace rtso

#endif
