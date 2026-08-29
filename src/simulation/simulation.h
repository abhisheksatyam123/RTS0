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
#include <algorithm>
#include <string>

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

struct ProductionOrder {
	uint8_t unit_type;
	float timer;
	float duration;
};

class Simulation {
public:
	static constexpr float GRID = 2.0f;
	static constexpr float MAP_HALF = 64.0f; // 128x128m large battlefield arena
	static constexpr float FIXED_STEP = 1.0f / 20.0f;
	static constexpr float UNIT_SPEED = 6.0f;
	static constexpr float ARRIVE_DIST = 0.5f;
	static constexpr float UNIT_RADIUS = 0.55f;
	static constexpr size_t UNIT_CAP = 4096;
	static constexpr size_t MAX_PROJECTILES = 384;

	// Fog of War (64x64 grid over the 128x128m map)
	static constexpr int FOW_GRID = 64;
	static constexpr float FOW_CELL_SIZE = (MAP_HALF * 2.0f) / (float)FOW_GRID; // 2.0m per cell

	enum FowState : uint8_t {
		FOW_UNEXPLORED = 0, // black — never seen
		FOW_EXPLORED   = 1, // dark shroud — seen before, not currently
		FOW_VISIBLE    = 2, // fully visible — currently in unit LOS
	};

	// Public accessor for the FoW grid (FOW_GRID*FOW_GRID flat array, row-major)
	const uint8_t* fow_grid() const { return fow_grid_; }

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
		float attack_timer; // for defensive arrows

		// Multi-slot production queue
		std::vector<ProductionOrder> queue;

		// Rally point
		bool has_rally_point;
		float rally_x, rally_z;
		uint32_t rally_target_res_id;
		uint32_t rally_target_bld_id;

		// Garrison
		std::vector<uint32_t> garrisoned_units;
		int garrison_cap;

		// Tech research
		uint8_t research_tech;
		float research_timer;
		float research_duration;

		// Structure Capture (Capturable.js)
		float capture_points;
		float max_capture_points;
	};

	Simulation();
	~Simulation();

	void reset();
	void spawn_initial();
	void spawn_war(int total_units = 2000);

	uint32_t spawn_unit(float x, float z, uint8_t team = 0, UnitType u_type = UNIT_VILLAGER, float hp = 100.0f);
	uint32_t spawn_resource(int cell_x, int cell_y, uint8_t type, int32_t amount = 600);
	uint32_t spawn_resource_world(float wx, float wz, uint8_t type, int32_t amount = 600);
	uint32_t spawn_building(int cell_x, int cell_y, uint8_t type, uint8_t team = 0, float progress = 1.0f);
	uint32_t spawn_building_world(float wx, float wz, uint8_t type, uint8_t team = 0, float progress = 1.0f);

	float get_terrain_height(float x, float z) const;
	float map_half() const { return map_half_; }
	float water_height() const { return terrain_water_height_; }
	const std::vector<float>& terrain_heights() const { return terrain_heights_; }
	int terrain_res() const { return terrain_res_; }
	const std::string& current_map_id() const { return current_map_id_; }
	const std::string& current_map_name() const { return current_map_name_; }
	const std::string& current_map_biome() const { return current_map_biome_; }

	void set_map_half(float h) { map_half_ = h; }
	void set_terrain_data(const std::vector<float> &heights, int res, float water_h) {
		terrain_heights_ = heights;
		terrain_res_ = res;
		terrain_water_height_ = water_h;
	}
	void set_map_info(const std::string &id, const std::string &name, const std::string &biome) {
		current_map_id_ = id;
		current_map_name_ = name;
		current_map_biome_ = biome;
	}

	// Commands
	void cmd_move(const uint32_t *unit_ids, size_t count, float tx, float tz);
	void cmd_stop(const uint32_t *unit_ids, size_t count);
	bool cmd_build(int cell_x, int cell_y, uint8_t b_type);
	bool cmd_train(uint32_t building_id, UnitType u_type);
	bool cmd_cancel_train(uint32_t building_id, size_t queue_idx);
	void cmd_set_rally_point(uint32_t building_id, float rx, float rz, uint32_t res_id = 0, uint32_t bld_id = 0);

	void order_gather(const uint32_t *unit_ids, size_t count, uint32_t resource_id);
	void order_attack(const uint32_t *unit_ids, size_t count, uint32_t target_unit_id);
	void order_attack_move(const uint32_t *unit_ids, size_t count, float tx, float tz);
	void order_patrol(const uint32_t *unit_ids, size_t count, float tx, float tz);
	void order_build_or_repair(const uint32_t *unit_ids, size_t count, uint32_t building_id);
	void order_set_stance(const uint32_t *unit_ids, size_t count, uint8_t stance);
	void order_set_formation(const uint32_t *unit_ids, size_t count, uint8_t formation);
	void order_garrison(const uint32_t *unit_ids, size_t count, uint32_t building_id);
	void order_ungarrison(uint32_t building_id);
	void order_ungarrison_single(uint32_t building_id, uint32_t unit_id);
	void order_guard(const uint32_t *unit_ids, size_t count, uint32_t target_unit_id);

	bool cmd_research(uint32_t building_id, uint8_t tech_id);
	bool cmd_barter(ResourceType sell, ResourceType buy, int amount = 100);

	bool save_state(const char *filepath) const;
	bool load_state(const char *filepath);

	bool can_place(int cell_x, int cell_y, int footprint) const;

	void tick(float dt);

	// Views
	struct UnitView {
		const float *pos_x, *pos_z;
		const float *vel_x, *vel_z;
		const float *target_x, *target_z;
		const float *hp, *max_hp;
		const uint32_t *id;
		const uint8_t *alive, *team, *unit_type, *action_state, *res_type_carried, *stance, *formation;
		const int32_t *carry;
		const float *death_timer;
		const uint16_t *kills;
		const uint8_t *rank;
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

	float food_rate() const { return food_gathered_window_ * 6.0f; } // per min approx
	float wood_rate() const { return wood_gathered_window_ * 6.0f; }
	float gold_rate() const { return gold_gathered_window_ * 6.0f; }
	float stone_rate() const { return stone_gathered_window_ * 6.0f; }

	int pop_current() const;
	int pop_max() const;
	int wave_number() const { return wave_number_; }
	float wave_time_remaining() const { return std::max(0.0f, 30.0f - wave_timer_); }
	bool is_game_over() const { return game_over_; }
	bool is_victory() const { return victory_; }
	uint8_t player_phase() const { return player_phase_; }
	void set_player_civ(uint8_t civ) { player_civ_[0] = (Civilization)civ; }
	uint8_t player_civ() const { return player_civ_[0]; }
	void set_team_civ(uint8_t team, uint8_t civ) { if (team < 4) player_civ_[team] = (Civilization)civ; }
	uint8_t team_civ(uint8_t team) const { return (team < 4) ? player_civ_[team] : CIV_ATHENIANS; }
	void set_biome(uint8_t b) { biome_ = b; }
	uint8_t biome() const { return biome_; }
	void set_map_preset(uint8_t m) { map_preset_ = m; }
	uint8_t map_preset() const { return map_preset_; }
	bool is_tech_researched(uint8_t tech_id) const { return (researched_techs_mask_ & (1u << tech_id)) != 0; }

	// Fauna (Wildlife) View & Spawner
	struct FaunaView {
		size_t count;
		const uint32_t *id;
		const float *pos_x, *pos_z;
		const float *vel_x, *vel_z;
		const float *rot_y;
		const float *hp, *max_hp;
		const int *food_amount;
		const uint8_t *fauna_type;
		const uint8_t *behavior;
		const uint8_t *alive;
	};
	FaunaView fauna() const;

	uint32_t spawn_fauna(float x, float z, uint8_t ftype);
	void order_hunt(const uint32_t *unit_ids, size_t count, uint32_t fauna_id);

	struct TreasureRelic {
		uint32_t id;
		float x, z;
		uint8_t res_type;
		int amount;
		bool claimed;
	};

	float wonder_time_remaining() const { return std::max(0.0f, 600.0f - wonder_victory_timer_); }
	bool is_wonder_active() const { return has_active_wonder_; }
	const std::vector<TreasureRelic> &relics() const { return relics_; }

	std::vector<uint32_t> get_idle_workers(uint8_t team = 0) const;

	size_t unit_count() const { return units_.count; }
	size_t building_count() const { return buildings_.size(); }
	size_t resource_count() const { return resources_.count; }

	struct Stats { uint64_t tick_us; size_t ticks; };
	Stats stats() const { return {tick_us_, ticks_}; }

	struct MatchRecord {
		int units_trained = 0;
		int units_lost = 0;
		int enemies_killed = 0;
		int buildings_constructed = 0;
		int buildings_destroyed = 0;
		int total_food_gathered = 0;
		int total_wood_gathered = 0;
		int total_gold_gathered = 0;
		int total_stone_gathered = 0;
	};
	const MatchRecord &record() const { return record_; }

private:
	void move_units(float dt, Arena &arena);
	void separation(float dt, Arena &arena);
	void tick_gather(float dt);
	void tick_combat(float dt);
	void tick_construction(float dt);
	void tick_production(float dt);
	void tick_projectiles(float dt);
	void tick_waves(float dt);
	void tick_trickle(float dt);
	void tick_petra_ai(float dt);
	void tick_fauna(float dt);
	void tick_fow();  // updates FoW each sim tick
	void fire_arrow(float sx, float sy, float sz, float tx, float ty, float tz, float damage, uint8_t team, uint32_t target_id);
	void bind_unit_index(uint32_t id, size_t idx);
	int32_t lookup_unit(uint32_t id) const;

	struct FaunaStorage {
		static constexpr size_t MAX_FAUNA = 256;
		size_t count = 0;
		uint32_t id[MAX_FAUNA] = {};
		float pos_x[MAX_FAUNA] = {};
		float pos_z[MAX_FAUNA] = {};
		float vel_x[MAX_FAUNA] = {};
		float vel_z[MAX_FAUNA] = {};
		float rot_y[MAX_FAUNA] = {};
		float hp[MAX_FAUNA] = {};
		float max_hp[MAX_FAUNA] = {};
		int food_amount[MAX_FAUNA] = {};
		uint8_t fauna_type[MAX_FAUNA] = {};
		uint8_t behavior[MAX_FAUNA] = {};
		uint8_t alive[MAX_FAUNA] = {};
		float state_timer[MAX_FAUNA] = {};
		float attack_timer[MAX_FAUNA] = {};
		uint32_t target_unit_id[MAX_FAUNA] = {};
	} fauna_;
	std::unordered_map<uint32_t, size_t> fauna_id_to_idx_;

	UnitSoA units_;
	ResourceSoA resources_;
	std::vector<Building> buildings_;
	std::vector<Projectile> projectiles_;
	std::vector<TreasureRelic> relics_;
	float wonder_victory_timer_ = 0.0f;
	bool has_active_wonder_ = false;
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
	int food_ = 300;
	int wood_ = 400;
	int gold_ = 300;
	int stone_ = 200;
	float passive_timer_ = 0.0f;
	float rate_window_timer_ = 0.0f;

	float food_gathered_window_ = 0.0f;
	float wood_gathered_window_ = 0.0f;
	float gold_gathered_window_ = 0.0f;
	float stone_gathered_window_ = 0.0f;

	uint8_t player_phase_ = 1; // 1=Village, 2=Town, 3=City
	Civilization player_civ_[4] = {CIV_ATHENIANS, CIV_SPARTANS, CIV_ROMANS, CIV_PERSIANS};
	uint8_t biome_ = BIOME_AEGEAN;
	uint8_t map_preset_ = MAP_AEGEAN_ARCHIPELAGO;
	uint32_t researched_techs_mask_ = 0;

	float wave_timer_ = 0.0f;
	int wave_number_ = 0;
	bool game_over_ = false;
	bool victory_ = false;

	MatchRecord record_;

	uint64_t tick_us_ = 0;
	uint64_t ticks_ = 0;
	FrameArena frame_arena_{4 * 1024 * 1024};

	// Petra AI state (Petra.js)
	float petra_timer_  = 0.0f;
	int   petra_phase_  = 1; // 1=early, 2=mid, 3=late
	float ai_food_  = 400.0f;
	float ai_wood_  = 600.0f;
	float ai_gold_  = 300.0f;
	float ai_stone_ = 200.0f;

	uint8_t fow_grid_[FOW_GRID * FOW_GRID] = {}; // FoW state per tile
	float fow_timer_ = 0.0f;
	float territory_timer_ = 0.0f;

	float map_half_ = 64.0f;
	int terrain_res_ = 64;
	std::vector<float> terrain_heights_;
	float terrain_water_height_ = -1.0f;
	std::string current_map_id_ = "default";
	std::string current_map_name_ = "Standard Arena";
	std::string current_map_biome_ = "mediterranean";

	uint32_t alloc_id() { return next_id_++; }
};

} // namespace rtso

#endif
