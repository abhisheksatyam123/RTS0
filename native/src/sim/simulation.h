#ifndef RTSO_SIMULATION_H
#define RTSO_SIMULATION_H

#include "../core/arena.h"
#include "../core/soa_storage.h"
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cmath>

namespace rtso {

class Simulation {
public:
	static constexpr float GRID = 2.0f;
	static constexpr float MAP_HALF = 30.0f;
	static constexpr float FIXED_STEP = 1.0f / 20.0f;
	static constexpr float UNIT_SPEED = 6.0f;
	static constexpr float ARRIVE_DIST = 0.35f;
	static constexpr float UNIT_RADIUS = 0.45f;
	static constexpr int HOUSE_COST = 50;

	struct Building {
		uint32_t id;
		float x, z;
		float half_size;
		bool is_center;
		int cell_x, cell_y;
	};

	Simulation();
	~Simulation();

	void reset();
	void spawn_initial();

	uint32_t spawn_unit(float x, float z, uint8_t team = 0, float hp = 100.0f);
	uint32_t spawn_resource(int cell_x, int cell_y, uint8_t type);

	// Commands
	void cmd_move(const uint32_t *unit_ids, size_t count, float tx, float tz);
	bool cmd_build_house(int cell_x, int cell_y);
	void order_gather(const uint32_t *unit_ids, size_t count, uint32_t resource_id);
	void order_attack(const uint32_t *unit_ids, size_t count, uint32_t target_unit_id);

	bool can_place(int cell_x, int cell_y, bool is_center) const;

	void tick(float dt);

	// Views
	struct UnitView {
		const float *pos_x, *pos_z;
		const float *vel_x, *vel_z;
		const float *hp, *max_hp;
		const uint32_t *id;
		const uint8_t *alive, *team;
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

	int gold() const { return gold_; }
	void add_gold(int amount) { gold_ += amount; }
	int wave_number() const { return wave_number_; }
	size_t unit_count() const { return units_.count; }
	size_t building_count() const { return buildings_.size(); }
	size_t resource_count() const { return resources_.count; }

	struct Stats { uint64_t tick_us; size_t ticks; };
	Stats stats() const { return {tick_us_, ticks_}; }

private:
	void rebuild_passability();
	void move_units(float dt);
	void separation(float dt, Arena &arena);
	void tick_gather(float dt);
	void tick_combat(float dt);
	void tick_waves(float dt);

	UnitSoA units_;
	ResourceSoA resources_;
	std::vector<Building> buildings_;
	std::unordered_map<uint32_t, size_t> unit_id_to_idx_;
	std::unordered_map<uint32_t, size_t> res_id_to_idx_;

	struct CellHash {
		size_t operator()(const std::pair<int, int> &p) const {
			return (size_t)p.first * 73856093 ^ (size_t)p.second * 19349663;
		}
	};
	std::unordered_set<std::pair<int, int>, CellHash> occupied_cells_;

	uint32_t next_id_ = 1;
	int gold_ = 150;
	float gold_carry_ = 0.0f;
	float wave_timer_ = 0.0f;
	int wave_number_ = 0;
	uint64_t tick_us_ = 0;
	uint64_t ticks_ = 0;
	FrameArena frame_arena_;

	uint32_t alloc_id() { return next_id_++; }
};

} // namespace rtso

#endif
