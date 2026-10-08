#ifndef RTSO_SIMULATION_H
#define RTSO_SIMULATION_H

#include "../core/memory/arena.h"
#include "../core/memory/soa_storage.h"
#include "grid_nav.h"
#include "aura_manager.h"
#include "petra_ai.h"
#include "sim_types.h"
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <utility>
#include <algorithm>
#include <string>
#include <array>

namespace rtso {

// Deterministic sim-tick PRNG (xorshift32, same algorithm as RmsRng).
// Replaces libc rand() so two lockstep peers draw identical streams.
// Sequential-call only: all sim-rng call sites are in serial tick regions.
// Persisted in save_state v3 and mixed into TurnManager::compute_sim_hash.
class SimRng {
public:
	explicit SimRng(uint32_t seed = 1337) : state_(seed ? seed : 1337) {}
	void set_seed(uint32_t seed) { state_ = seed ? seed : 1337; }
	uint32_t state() const { return state_; }
	void set_state(uint32_t st) { state_ = st ? st : 1337; }
	uint32_t next_u32() {
		uint32_t x = state_;
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		state_ = x;
		return x;
	}
	int range_int(int min_val, int max_val) {
		if (min_val >= max_val) return min_val;
		return min_val + (int)(next_u32() % (uint32_t)(max_val - min_val + 1));
	}
	float range_float(float min_val, float max_val) {
		return min_val + (next_u32() & 0xFFFFFF) / 16777216.0f * (max_val - min_val);
	}
private:
	uint32_t state_;
};

struct Projectile {
	uint32_t id = 0;
	float start_x = 0, start_y = 1.0f, start_z = 0;
	float target_x = 0, target_y = 0.5f, target_z = 0;
	float current_x = 0, current_y = 0, current_z = 0;
	float progress = 0.0f;
	float speed = 26.0f;
	float arc_height = 2.5f;
	float damage = 15.0f;
	uint8_t damage_type = 1; // 0=hack, 1=pierce, 2=crush
	uint8_t visual = 0; // 0=arrow, 1=bolt (warship projectile prop), 2=stone
	uint8_t status_effect = 0; // bitflags of StatusEffect
	uint8_t team = 0;
	uint32_t target_unit_id = 0;
	uint32_t target_building_id = 0;
	bool alive = true;
};

struct ProductionOrder {
	uint8_t unit_type;
	float timer;
	float duration;
};

// Per-player state (0 A.D. Player.js + ResourceCounter + TechnologyManager).
// One slot per player id; unit/building `team` fields hold the player id.
// Stockpiles are float so AI trickle income accrues fractionally; whole-number
// callers read them through int accessors.
static constexpr uint8_t MAX_PLAYERS = 8;

struct PlayerState {
	float res[RES_COUNT] = {};             // indexed by ResourceType
	float gathered_window[RES_COUNT] = {}; // decaying gather-rate window for the HUD
	uint32_t tech_mask = 0;                // researched TechnologyId bits
	uint8_t phase = 1;                     // 1=Village, 2=Town, 3=City
	Civilization civ = CIV_ATHENIANS;
	uint8_t team = 0;                      // alliance group: same team = allies
	uint8_t active = 0;                    // slot is in the match
	uint8_t is_ai = 0;                     // driven by Petra

	float &food() { return res[RES_FOOD]; }
	float &wood() { return res[RES_WOOD]; }
	float &gold() { return res[RES_GOLD]; }
	float &stone() { return res[RES_STONE]; }
	float food() const { return res[RES_FOOD]; }
	float wood() const { return res[RES_WOOD]; }
	float gold() const { return res[RES_GOLD]; }
	float stone() const { return res[RES_STONE]; }
	bool has_tech(uint8_t tech_id) const { return (tech_mask & (1u << tech_id)) != 0; }
	bool can_afford(float f, float w, float g, float s) const {
		return res[RES_FOOD] >= f && res[RES_WOOD] >= w && res[RES_GOLD] >= g && res[RES_STONE] >= s;
	}
	// Deducts the cost if affordable; returns false and leaves stock untouched otherwise.
	bool pay(float f, float w, float g, float s) {
		if (!can_afford(f, w, g, s)) return false;
		res[RES_FOOD] -= f; res[RES_WOOD] -= w; res[RES_GOLD] -= g; res[RES_STONE] -= s;
		return true;
	}
	void add(float f, float w, float g, float s) {
		res[RES_FOOD] += f; res[RES_WOOD] += w; res[RES_GOLD] += g; res[RES_STONE] += s;
	}
};

class Simulation {
public:
	// Player Petra drives. Area C ("Fix and scale the AI") makes Petra per-player.
	static constexpr uint8_t PETRA_PLAYER = 1;
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
	uint8_t fow_state_at(float x, float z, uint8_t viewer_team = 0) const;
	bool is_entity_visible_to_viewer(float x, float z, uint8_t entity_team, uint8_t viewer_team = 0) const;

	struct Building {
		uint32_t id;
		float x, z;
		float half_size;
		uint8_t type; // BLD_*
		uint8_t team; // 0=player, 1=enemy
		float hp;
		float max_hp;
		float build_progress; // 0.0f to 1.0f (1.0f = finished)
		bool committed = false; // 0 A.D. Foundation.committed: first builder reached the plot
		bool wrecked = false;   // hp already applied; keep entity for collapse + rubble
		int cell_x, cell_y;
		int footprint; // 1 or 2
		float attack_timer; // for defensive arrows
		bool is_burning; // Structure fire under 33% HP

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

		// Structure Capture (Capturable.js): per-player capture points.
		// The scalar capture_points field remains the amount currently held
		// by the building owner for compatibility with presentation code.
		float capture_points;
		std::array<float, 8> capture_points_by_team{};
		float max_capture_points;
		float capture_regen_rate;
		float capture_garrison_regen_rate;
		float territory_decay_rate;
	};

	Simulation();
	~Simulation();

	void reset();
	void set_sim_seed(uint32_t seed) { sim_seed_ = seed ? seed : 1337; sim_rng_.set_seed(sim_seed_); }
	uint32_t sim_seed() const { return sim_seed_; }
	uint32_t sim_rng_state() const { return sim_rng_.state(); }
	uint32_t next_id() const { return next_id_; }
	void spawn_initial();
	void spawn_war(int total_units = 2000);
	void generate_random_map(MapType type = MAP_RANDOM_CONTINENTAL, uint32_t seed = 0, int num_players = 2, float map_half = 64.0f);

	uint32_t spawn_unit(float x, float z, uint8_t team = 0, UnitType u_type = UNIT_VILLAGER, float hp = 100.0f);
	uint32_t spawn_resource(int cell_x, int cell_y, uint8_t type, int32_t amount = 600);
	uint32_t spawn_resource_world(float wx, float wz, uint8_t type, int32_t amount = 600, uint8_t subtype = 0);
	uint32_t spawn_building(int cell_x, int cell_y, uint8_t type, uint8_t team = 0, float progress = 1.0f);
	uint32_t spawn_building_world(float wx, float wz, uint8_t type, uint8_t team = 0, float progress = 1.0f);
	uint32_t spawn_relic(float x, float z, uint8_t res_type, int amount = 300);

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
	uint32_t cmd_build(int cell_x, int cell_y, uint8_t b_type);
	bool cmd_train(uint32_t building_id, UnitType u_type);
	// P3 maintainFleet upkeep seam (0 A.D. navalManager.js:507-543): team-aware
	// naval queue entry used by TurnManager NET_CMD_MAINTAIN_FLEET. Unlike
	// cmd_train (team-0/human only) this honours the building's own team
	// resources (Petra team 1 via petra_try_pay) so Petra docks can train.
	bool cmd_maintain_fleet(uint32_t building_id, UnitType u_type, uint8_t team);
	bool cmd_cancel_train(uint32_t building_id, size_t queue_idx);
	void cmd_set_rally_point(uint32_t building_id, float rx, float rz, uint32_t res_id = 0, uint32_t bld_id = 0);

	void order_gather(const uint32_t *unit_ids, size_t count, uint32_t resource_id);
	void order_attack(const uint32_t *unit_ids, size_t count, uint32_t target_unit_id);
	void order_attack_building(const uint32_t *unit_ids, size_t count, uint32_t target_building_id);
	void order_attack_move(const uint32_t *unit_ids, size_t count, float tx, float tz);
	void order_patrol(const uint32_t *unit_ids, size_t count, float tx, float tz);
	void order_build_or_repair(const uint32_t *unit_ids, size_t count, uint32_t building_id);
	void order_set_stance(const uint32_t *unit_ids, size_t count, uint8_t stance);
	void order_set_formation(const uint32_t *unit_ids, size_t count, uint8_t formation);
	void order_garrison(const uint32_t *unit_ids, size_t count, uint32_t building_id);
	void order_ungarrison(uint32_t building_id);
	void order_ungarrison_single(uint32_t building_id, uint32_t unit_id);
	void order_guard(const uint32_t *unit_ids, size_t count, uint32_t target_unit_id);

	// Livestock & Corrals (Corral.js)
	void order_herd(const uint32_t *unit_ids, size_t count, uint32_t fauna_id);
	bool cmd_breed_livestock(uint32_t building_id);

	// Naval Transport Logistics (GarrisonHolder.js / UnitAI.js)
	static constexpr int TRANSPORT_CAPACITY = 15;
	void order_board_transport(const uint32_t *unit_ids, size_t count, uint32_t transport_id);
	void order_disembark(uint32_t transport_id, float target_x, float target_z);
	void order_disembark_all(uint32_t transport_id);
	void order_disembark_single(uint32_t transport_id, uint32_t unit_id);
	const std::vector<uint32_t> *get_transport_passengers(uint32_t transport_id) const;
	size_t get_transport_passenger_count(uint32_t transport_id) const;
	bool is_transport(uint32_t unit_id) const;
	int get_transport_capacity(uint32_t transport_id) const;

	// Garrison & Turret System (GarrisonHolder.js, TurretHolder.js)
	int get_garrison_count(uint32_t building_id) const;
	int get_garrison_capacity(uint32_t building_id) const;
	const std::vector<uint32_t> *get_garrisoned_units(uint32_t building_id) const;

	// Victory Conditions (VictoryManager.js, EndGameManager.js)
	uint8_t victory_condition() const { return (uint8_t)victory_condition_; }
	void set_victory_condition(uint8_t vc) { victory_condition_ = (VictoryCondition)vc; }
	float relic_victory_timer() const { return relic_victory_timer_; }
	// Legacy wave-defense mode: enemy waves every 35s, survive wave 5 to win. Off by default.
	bool waves_enabled() const { return waves_enabled_; }
	void set_waves_enabled(bool on) { waves_enabled_ = on; }

	// Battle Alerts (AlertRaiser.js)
	void raise_alert(BattleAlert::Type type, float x, float z, uint32_t entity_id, uint8_t team = 0);
	const std::vector<BattleAlert> &pending_alerts() const { return pending_alerts_; }
	void clear_alerts() { pending_alerts_.clear(); }

	bool cmd_research(uint32_t building_id, uint8_t tech_id);
	bool cmd_barter(ResourceType sell, ResourceType buy, int amount = 100);
	bool cmd_set_diplomacy(uint8_t player_id, uint8_t stance);
	// G3 Market/Trader
	int calculate_trader_gain(const Building& mA, const Building& mB, uint8_t team) const;
	void order_trade(const uint32_t *unit_ids, size_t count, uint32_t market_a_id, uint32_t market_b_id);
	void order_trade_auto(const uint32_t *unit_ids, size_t count); // auto-find best route
	float get_market_buy_price(ResourceType t) const { return market_buy_price_[t]; }
	float get_market_sell_price(ResourceType t) const { return market_sell_price_[t]; }
	int get_market_history(ResourceType t) const { return market_trade_history_[t]; }
	float trader_gains(const Building *a, const Building *b) const; // legacy helper
	float trader_gains(Building *a, Building *b) { return trader_gains((const Building*)a,(const Building*)b); }

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
		const uint32_t *target_resource_id;
		const int32_t *carry;
		const float *death_timer;
		const uint16_t *kills;
		const uint8_t *rank;
		const uint8_t *status_effects;
		const uint16_t *xp;
		size_t count;
	};
	UnitView units() const;

	struct ResourceView {
		const float *x, *z;
		const int32_t *amount;
		const uint8_t *type, *subtype, *alive;
		const uint32_t *id;
		size_t count;
	};
	ResourceView resources() const;

	const std::vector<Building> &buildings() const { return buildings_; }
	const std::vector<Projectile> &projectiles() const { return projectiles_; }

	// Per-player table. Out-of-range ids clamp to player 0 so stray team bytes can't index past the array.
	const PlayerState &player(uint8_t pid) const { return players_[pid < MAX_PLAYERS ? pid : 0]; }
	PlayerState &player(uint8_t pid) { return players_[pid < MAX_PLAYERS ? pid : 0]; }
	int player_resource(uint8_t pid, uint8_t res_type) const { return (res_type < RES_COUNT) ? (int)player(pid).res[res_type] : 0; }
	void set_player_resources(uint8_t pid, int food, int wood, int gold, int stone) {
		PlayerState &ps = player(pid);
		ps.food() = (float)food; ps.wood() = (float)wood; ps.gold() = (float)gold; ps.stone() = (float)stone;
	}
	void set_player_team(uint8_t pid, uint8_t team) { if (pid < MAX_PLAYERS) players_[pid].team = team; }
	uint8_t player_team(uint8_t pid) const { return player(pid).team; }
	void set_player_active(uint8_t pid, bool active, bool is_ai) {
		if (pid >= MAX_PLAYERS) return;
		players_[pid].active = active ? 1 : 0;
		players_[pid].is_ai = is_ai ? 1 : 0;
	}
	// Same alliance team (0 A.D. "team" in game setup); a player is always its own ally.
	bool same_team(uint8_t a, uint8_t b) const { return a == b || (a < MAX_PLAYERS && b < MAX_PLAYERS && players_[a].team == players_[b].team); }

	// Player 0 (local human) shorthands kept for the HUD and tests.
	int food() const { return (int)players_[0].food(); }
	int wood() const { return (int)players_[0].wood(); }
	int gold() const { return (int)players_[0].gold(); }
	int stone() const { return (int)players_[0].stone(); }
	// GameSetup starting-resources option (0 A.D. page_gamesetup "Starting Resources").
	// Sets player 0 only (AI slots keep their reset() stock); use set_player_resources per slot.
	// Must be called AFTER map load: reset()/generate() overwrite stocks.
	void set_starting_resources(int food, int wood, int gold, int stone) { set_player_resources(0, food, wood, gold, stone); }

	float food_rate() const { return players_[0].gathered_window[RES_FOOD] * 6.0f; } // per min approx
	float wood_rate() const { return players_[0].gathered_window[RES_WOOD] * 6.0f; }
	float gold_rate() const { return players_[0].gathered_window[RES_GOLD] * 6.0f; }
	float stone_rate() const { return players_[0].gathered_window[RES_STONE] * 6.0f; }

	int pop_current() const;
	int pop_max() const;
	int wave_number() const { return wave_number_; }
	float wave_time_remaining() const { return std::max(0.0f, 35.0f - wave_timer_); }
	bool is_game_over() const { return game_over_; }
	bool is_victory() const { return victory_; }
	uint8_t player_phase() const { return players_[0].phase; }
	void set_player_civ(uint8_t civ) { players_[0].civ = (Civilization)civ; }
	uint8_t player_civ() const { return players_[0].civ; }
	void set_team_civ(uint8_t team, uint8_t civ) { if (team < MAX_PLAYERS) players_[team].civ = (Civilization)civ; }
	uint8_t team_civ(uint8_t team) const { return (team < MAX_PLAYERS) ? players_[team].civ : CIV_ATHENIANS; }
	void set_biome(uint8_t b) { biome_ = b; }
	uint8_t biome() const { return biome_; }
	void set_map_preset(uint8_t m) { map_preset_ = m; }
	uint8_t map_preset() const { return map_preset_; }
	bool is_tech_researched(uint8_t tech_id) const { return players_[0].has_tech(tech_id); }
	uint32_t researched_techs_mask() const { return players_[0].tech_mask; }
	uint32_t ai_researched_techs_mask() const { return players_[PETRA_PLAYER].tech_mask; }
	bool unit_has_order(uint32_t uid) const { int32_t idx = lookup_unit(uid); return idx>=0 && (size_t)idx < units_.count && units_.has_order[idx]; }
	const Grid<uint8_t>& territory_grid() const { return territory_grid_; }
	const Grid<uint8_t>& influence_grid() const { return influence_grid_; }
	const NavGrid& nav_grid() const { return nav_grid_; }
	float market_buy_price(uint8_t res) const { return (res < 4) ? market_buy_price_[res] : 1.0f; }
	float market_sell_price(uint8_t res) const { return (res < 4) ? market_sell_price_[res] : 1.0f; }

	// Diplomacy & Tribute (DiplomacyDialog.js)
	enum DiplomacyStance : uint8_t {
		DIPLO_ALLY = 0,
		DIPLO_NEUTRAL = 1,
		DIPLO_ENEMY = 2
	};
	uint8_t get_diplomacy(uint8_t player_id) const { return (player_id < 8) ? diplomacy_[player_id] : (uint8_t)DIPLO_ENEMY; }
	uint8_t get_diplomacy_from(uint8_t player_id, uint8_t other) const {
		return (other < 8) ? diplomacy_[other] : (uint8_t)DIPLO_ENEMY;
	}
	void set_diplomacy(uint8_t player_id, uint8_t stance) { if (player_id < 8) diplomacy_[player_id] = stance; }
	void set_mutual_ally(uint8_t player_id) {
		if (player_id == 0 || player_id >= 8) return;
		diplomacy_[player_id] = DIPLO_ALLY;
		diplomacy_reverse_[player_id] = DIPLO_ALLY;
	}
	bool is_mutual_ally(uint8_t player_id) const {
		return player_id != 0 && player_id < 8 &&
		       get_diplomacy(player_id) == DIPLO_ALLY &&
		       diplomacy_reverse_[player_id] == DIPLO_ALLY;
	}
	const uint8_t *diplomacy_state() const { return diplomacy_; }
	const uint8_t *diplomacy_reverse_state() const { return diplomacy_reverse_; }
	bool send_tribute(uint8_t player_id, uint8_t res_type, int amount);
	bool ai_send_tribute(uint8_t res_type, int amount);
	// Accept a validated 0 A.D.-style Petra diplomacy request. The request
	// remains in WAITING until the tribute policy is satisfied; callers can
	// inspect petra_diplomacy_requests() for status/timers.
	void receive_petra_diplomacy_request(uint8_t player_id, bool ally, int wanted, uint8_t resource);

	// Petra AI Heuristics & Threat View
	int petra_phase() const { return players_[PETRA_PLAYER].phase; }
	float ai_food() const { return players_[PETRA_PLAYER].food(); }
	float ai_wood() const { return players_[PETRA_PLAYER].wood(); }
	float ai_gold() const { return players_[PETRA_PLAYER].gold(); }
	float ai_stone() const { return players_[PETRA_PLAYER].stone(); }
	const float* petra_threat_grid() const { return petra_threat_map_.danger; }
	float get_threat_at(float x, float z) const;
	bool is_position_safe(float x, float z, float threshold = 6.0f) const;
	int ai_pop_current() const;
	int ai_pop_max() const;

	const uint8_t* petra_fow_grid() const { return petra_fow_; }

	// Petra Multi-Base HQ & BasesManager (0 A.D. ai/petra/headquarters.js / baseManager.js)
	struct PetraBase {
		uint32_t id = 1;
		uint32_t anchor_building_id = 0;
		float x = 36.0f;
		float z = 36.0f;
		bool is_primary = true;
		int assigned_workers = 0;
		float radius = 32.0f;
		float danger_level = 0.0f;
		bool resources_depleted = false;
	};

	enum PetraPriorityQueue : uint8_t {
		PETRA_QUEUE_EMERGENCY = 0, // Threat >= 7.0f: Emergency spearmen, archers, towers
		PETRA_QUEUE_VILLAGER  = 1, // Economic boom: 35% of total pop
		PETRA_QUEUE_MILITARY  = 2, // Military strike force: Counter-units for AttackPlan
		PETRA_QUEUE_TECH      = 3, // Technology & Phase research
		PETRA_QUEUE_TRADE     = 4, // Market and trader caravans
		PETRA_QUEUE_NAVAL     = 5, // Dock and triremes on coastal/water maps
		PETRA_QUEUE_COUNT     = 6
	};

	const std::vector<PetraBase> &petra_bases() const { return petra_bases_; }
	const PetraBase *get_primary_petra_base() const;
	const PetraBase *get_nearest_petra_base(float x, float z) const;

	// Petra AI AttackPlan FSM matching 0 A.D. ai/petra/attackPlan.js
	struct AttackPlan {
		enum PlanType : uint8_t {
			PLAN_RAID     = 0, // Quick cavalry/infantry harass against economy/workers
			PLAN_ASSAULT  = 1, // Standard combined-arms attack against military buildings
			PLAN_SIEGE    = 2  // Heavy siege assault against fortified bases (requires Catapults/Siege Rams)
		} type = PLAN_ASSAULT;

		enum State : uint8_t {
			UNEXECUTED = 0,
			MUSTER     = 1,
			WALKING    = 2,
			ATTACKED   = 3,
			REGROUP    = 4,
			ABORT      = 5
		} state = UNEXECUTED;

		uint32_t target_building_id = 0;
		float staging_x = 0.0f;
		float staging_z = 0.0f;
		float target_x = 0.0f;
		float target_z = 0.0f;
		float abort_threat = 14.0f; // abort if danger at staging exceeds abort threshold
		std::vector<uint32_t> squad; // unit_ids committed to this plan
		size_t initial_squad_size = 0;
		float state_timer = 0.0f;
		uint32_t retreat_base_id = 0;
		float retreat_x = 36.0f;
		float retreat_z = 36.0f;
	};
	const AttackPlan &petra_attack_plan() const { return petra_attack_plan_; }
	const AttackPlan &petra_raid_plan() const { return petra_raid_plan_; }
	int petra_opening_step() const { return petra_opening_step_; }

	void set_petra_difficulty(uint8_t d) {
		petra_config_.difficulty = (d > PETRA_VERY_HARD) ? PETRA_MEDIUM : d;
		apply_petra_config();
	}
	void set_petra_behavior(uint8_t b) {
		petra_config_.behavior = (b > PETRA_BEHAVIOR_AGGRESSIVE) ? PETRA_BEHAVIOR_BALANCED : b;
		apply_petra_config();
	}
	uint8_t petra_difficulty() const { return petra_config_.difficulty; }
	uint8_t petra_behavior() const { return petra_config_.behavior; }
	const PetraConfig &petra_config() const { return petra_config_; }
	bool petra_in_emergency() const { return petra_emergency_.active; }
	const PetraDefenseArmy &petra_defense_army() const { return petra_defense_army_; }
	const PetraQueueManager &petra_queues() const { return petra_queues_; }
	const PetraTransportPlan &petra_transport_plan() const { return petra_transport_; }
	const std::vector<PetraChatLine> &pending_petra_chat() const { return petra_chat_; }
	const std::unordered_map<uint8_t, PetraDiplomacyRequest> &petra_diplomacy_requests() const { return petra_diplomacy_requests_; }
	void clear_petra_chat() { petra_chat_.clear(); }

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
		uint8_t owner_team = 255;
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
	// Fields (0 A.D. template_structure_resource_field): a finished field owns
	// one infinite grain resource at its centre. Derived from buildings_, so it
	// is rebuilt after load rather than saved.
	void sync_field_resources();
	uint32_t ensure_field_resource(const Building &b);
	uint32_t find_free_field(uint8_t team, float x, float z);
	void tick_combat(float dt);
	void tick_promotion(float dt);
	void tick_capture(float dt);
	void tick_status_effects(float dt);
	void tick_projectiles(float dt);
	void tick_construction(float dt);
	void tick_repair(float dt);
	void tick_production(float dt);
	void tick_research(float dt);
	void tick_barter(float dt);
	void tick_trickle(float dt);
	void tick_upkeep(float dt);
	void tick_entity_limits(float dt);
	void tick_death_damage(float dt);
	void tick_loot(float dt);
	void tick_gates(float dt);
	void tick_waves(float dt);
	void tick_petra_ai(float dt);
	void tick_fauna(float dt);
	void tick_fow();  // updates FoW each sim tick
	bool terrain_blocks_los(float sx, float sz, float sy, float tx, float tz, float ty) const;
	void rebuild_nav_grid(); // Phase1: rebuild NavGrid from buildings+water (mirrors CCmpObstructionManager rasterize)
	bool find_path(float sx, float sz, float tx, float tz, WaypointPath &out, uint8_t pass = PASS_INFANTRY) const;
	int territory_owner_at(float x, float z) const; // G2: query territory_grid_
	void rebuild_territory(); // G2: rebuild territory/influence grids from TC/Fortress/Tower
	void rebuild_auras(); // G4: rebuild AuraManager from temples/heroes/carnyx/female citizens
	void fire_arrow(float sx, float sy, float sz, float tx, float ty, float tz, float damage, uint8_t team, uint32_t target_id, uint8_t effect = 0, uint8_t dmg_type = 1, bool hits_building = false, uint8_t visual = 0);
	void mark_building_wreck(Building &b);
	void record_death(float x, float z, uint8_t team, uint8_t killer_team, uint8_t type, bool is_building);
	void bind_unit_index(uint32_t id, size_t idx);
	int32_t lookup_unit(uint32_t id) const;
	void apply_loot_bounty(uint8_t killer_team, int food, int wood, int gold, int stone);
	void check_victory_conditions(float dt);
	void spawn_regicide_heroes();

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
		uint32_t corral_id[MAX_FAUNA] = {};
		float fatten_timer[MAX_FAUNA] = {};
	} fauna_;
	std::unordered_map<uint32_t, size_t> fauna_id_to_idx_;
	std::unordered_map<uint32_t, std::vector<uint32_t>> transport_passengers_;

	UnitSoA units_;
	ResourceSoA resources_;
	std::vector<Building> buildings_;
	std::vector<Projectile> projectiles_;
	std::vector<TreasureRelic> relics_;
	float wonder_victory_timer_ = 0.0f;
	bool has_active_wonder_ = false;
	VictoryCondition victory_condition_ = VICTORY_CONQUEST;
	bool waves_enabled_ = false;
	float relic_victory_timer_ = 0.0f;
	std::vector<BattleAlert> pending_alerts_;
	float last_alert_time_[6] = {};
	std::unordered_map<uint32_t, size_t> unit_id_to_idx_;
	std::vector<int32_t> unit_idx_by_id_;
	std::unordered_map<uint32_t, size_t> res_id_to_idx_;
	std::unordered_map<uint32_t, uint32_t> field_res_by_bld_; // field building id -> grain resource id
	std::unordered_map<uint32_t, uint32_t> field_bld_by_res_; // grain resource id -> field building id
	struct FieldGatherers { int total = 0; int slots_taken = 0; };
	std::unordered_map<uint32_t, FieldGatherers> field_gatherers_; // per tick, keyed by grain resource id
	std::unordered_map<uint32_t, size_t> building_id_to_idx_;

	struct CellHash {
		size_t operator()(const std::pair<int, int> &p) const {
			return (size_t)p.first * 73856093 ^ (size_t)p.second * 19349663;
		}
	};
	std::unordered_set<std::pair<int, int>, CellHash> occupied_cells_;

	uint32_t next_id_ = 1;
	uint32_t sim_seed_ = 1337;
	SimRng sim_rng_;
	std::array<PlayerState, MAX_PLAYERS> players_{};
	PlayerState &petra_ps() { return players_[PETRA_PLAYER]; }
	Civilization civ_of(uint8_t pid) const { return (pid < MAX_PLAYERS) ? players_[pid].civ : CIV_ATHENIANS; }
	const PlayerState &petra_ps() const { return players_[PETRA_PLAYER]; }
	float passive_timer_ = 0.0f;
	float rate_window_timer_ = 0.0f;
	float barter_timer_ = 0.0f;
	float upkeep_timer_ = 0.0f;
	float entity_limits_timer_ = 0.0f;
	float gate_timer_ = 0.0f;
	static constexpr size_t MAX_RECENT_DEATHS = 64;
	std::array<RecentDeath, MAX_RECENT_DEATHS> recent_deaths_{};
	size_t recent_death_head_ = 0;

	// G3 Market / Barter dynamic prices (indexed by ResourceType: 0=WOOD,1=GOLD,2=STONE,3=FOOD)
	float market_buy_price_[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	float market_sell_price_[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	int market_trade_history_[4] = {};
	std::unordered_map<uint32_t, std::pair<uint32_t,uint32_t>> trader_routes_; // unit_id -> (marketA_id, marketB_id)
	std::unordered_map<uint32_t,int> trader_leg_; // unit_id -> 0=A->B, 1=B->A (which target next)

	uint8_t diplomacy_[8] = {0, 2, 2, 2, 2, 2, 2, 2}; // player 0 stance toward each player
	uint8_t diplomacy_reverse_[8] = {0, 2, 2, 2, 2, 2, 2, 2}; // reciprocal stance for shared LOS
	uint8_t biome_ = BIOME_AEGEAN;
	uint8_t map_preset_ = MAP_AEGEAN_ARCHIPELAGO;

	float wave_timer_ = 0.0f;
	int wave_number_ = 0;
	bool game_over_ = false;
	bool victory_ = false;

	MatchRecord record_;

	uint64_t tick_us_ = 0;
	uint64_t ticks_ = 0;
	FrameArena frame_arena_{4 * 1024 * 1024};

	// Petra AI Subsystems & Heuristics (0 A.D. Petra.js / dangerMap.js / military.js / economy.js / attackPlan.js)
	// Phase6/G9: 64x64 high-resolution danger map matching NavGrid and FOW_GRID (2.0m cells)
	static constexpr int THREAT_GRID_RES = 64;
	static constexpr float THREAT_CELL_SIZE = (MAP_HALF * 2.0f) / (float)THREAT_GRID_RES;

	// Phase7: 32-module Petra (config/attack/defense/queue) collapsed to G10 stub - see docs/AI_PETRA.md
	struct PetraThreatMap {
		float danger[THREAT_GRID_RES * THREAT_GRID_RES] = {};
		float enemy_threat[THREAT_GRID_RES * THREAT_GRID_RES] = {};
		float friendly_defense[THREAT_GRID_RES * THREAT_GRID_RES] = {};
		float update_timer = 0.0f;
	} petra_threat_map_; // Phase7 full AI will extract to src/simulation/ai/ (Petra.js / dangerMap.js)

	struct PetraStrikeForce {
		enum State : uint8_t {
			MUSTER = 0,
			MARCH  = 1,
			ENGAGE = 2
		} state = MUSTER;
		std::vector<uint32_t> unit_ids;
		float staging_x = 24.0f;
		float staging_z = 24.0f;
		float target_x = 0.0f;
		float target_z = 0.0f;
		uint32_t target_building_id = 0;
		uint32_t target_unit_id = 0;
		float state_timer = 0.0f;
	} petra_strike_force_;

	AttackPlan petra_attack_plan_;
	AttackPlan petra_raid_plan_;
	int petra_opening_step_ = 0;
	float petra_opening_timer_ = 0.0f;
	PetraConfig petra_config_;
	PetraDefenseArmy petra_defense_army_;
	PetraEmergency petra_emergency_;
	PetraQueueManager petra_queues_;
	PetraTransportPlan petra_transport_;
	std::vector<PetraChatLine> petra_chat_;
	std::unordered_map<uint8_t, PetraDiplomacyRequest> petra_diplomacy_requests_;
	float petra_defense_timer_ = 0.0f;
	float petra_victory_timer_ = 0.0f;
	float petra_diplo_timer_ = 0.0f;
	float ai_wonder_victory_timer_ = 0.0f;
	bool pick_petra_safe_staging(float base_x, float base_z, float target_x, float target_z, float &out_sx, float &out_sz) const;
	uint32_t find_petra_attack_target(float &out_tx, float &out_tz) const;
	uint32_t find_petra_raid_target(float &out_tx, float &out_tz) const;
	bool petra_unit_reserved(uint32_t uid) const;

	std::vector<PetraBase> petra_bases_;
	float petra_timer_  = 0.0f;
	float petra_econ_timer_ = 0.0f;
	float petra_trade_timer_ = 0.0f;
	float petra_naval_timer_ = 0.0f;
	float petra_scout_timer_ = 0.0f;
	uint32_t petra_scout_unit_id_ = 0;
	float petra_scout_angle_ = 0.0f;
	float petra_scout_radius_ = 12.0f;

	void update_petra_bases(float dt);
	void update_petra_threat_map(float dt);
	void petra_manage_scouting(float dt);
	void petra_manage_workers(float dt);
	void petra_manage_economy(float dt);
	void petra_manage_trading(float dt);
	void petra_manage_naval(float dt);
	void petra_manage_buildings(float dt);
	void petra_manage_research(float dt);
	void petra_manage_military_production(float dt);
	void petra_manage_tactical_attacks(float dt);
	void petra_manage_raid(float dt);
	void petra_manage_opening(float dt);
	void petra_manage_defense(float dt);
	void petra_manage_victory(float dt);
	void petra_manage_diplomacy(float dt);
	void petra_manage_emergency(float dt);
	void petra_manage_transport(float dt);
	void apply_petra_config();
	void petra_refresh_queue_accounts();
	bool petra_try_pay(uint8_t queue, float food, float wood, float gold, float stone);
	void petra_say(uint8_t kind, float x = 0.0f, float z = 0.0f);
	bool petra_path_crosses_water(float x0, float z0, float x1, float z1) const;
	bool petra_find_beach(float land_x, float land_z, float toward_x, float toward_z, float &out_x, float &out_z) const;
	bool petra_start_transport(const std::vector<uint32_t> &unit_ids, float from_x, float from_z, float to_x, float to_z);
	uint32_t petra_place_building(uint8_t btype, float near_x, float near_z, int min_r, int max_r, uint8_t queue);

	uint8_t fow_grid_[FOW_GRID * FOW_GRID] = {}; // FoW state per tile
	uint8_t petra_fow_[FOW_GRID * FOW_GRID] = {}; // FoW state per tile for Petra AI
	NavGrid nav_grid_; // Phase1: SoA nav mesh (buildings + water), mirrors 0AD Grid<NavcellData>
	Grid<uint8_t> territory_grid_; // 64x64 player id per cell, G2 (CCmpTerritoryManager)
	Grid<uint8_t> influence_grid_; // 64x64 influence count per cell, G2
	std::vector<WaypointPath> unit_paths_; // per-unit path (UNIT_CAP), SoA waypoint_cur/count sync
	AuraManager aura_mgr_; // G4: unified aura sources (AuraManager)
	float fow_timer_ = 0.0f;
	float territory_timer_ = 0.0f;
	float aura_timer_ = 0.0f; // G4: 0.4s aura rebuild timer

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
