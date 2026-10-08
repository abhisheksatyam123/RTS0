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
	uint16_t tmpl = CIV_UNIT_TEMPLATE_NONE; // civ roster unit to spawn (NONE = civ default for unit_type)
};

// Per-player state (0 A.D. Player.js + ResourceCounter + TechnologyManager).
// One slot per player id; unit/building `team` fields hold the player id.
// Stockpiles are float so AI trickle income accrues fractionally; whole-number
// callers read them through int accessors.
static constexpr uint8_t MAX_PLAYERS = 8;

struct PlayerState {
	float res[RES_COUNT] = {};             // indexed by ResourceType
	float gathered_window[RES_COUNT] = {}; // decaying gather-rate window for the HUD
	TechSet techs;                         // researched TechnologyIds
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
	bool has_tech(uint8_t tech_id) const { return techs.has(tech_id); }
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

	// Spatial grids (nav, fog of war, territory) keep a fixed 2m cell, so their
	// resolution grows with the map: 64 cells on the default 128m map, 256 on a
	// 512m 0 A.D. "Giant" map. grid_res_for_map() gives the resolution.
	static constexpr float SPATIAL_CELL = 2.0f;
	static constexpr int MIN_GRID_RES = 64;
	static constexpr int MAX_GRID_RES = 256;
	static int grid_res_for_map(float map_half);

	enum FowState : uint8_t {
		FOW_UNEXPLORED = 0, // black — never seen
		FOW_EXPLORED   = 1, // dark shroud — seen before, not currently
		FOW_VISIBLE    = 2, // fully visible — currently in unit LOS
	};

	// Public accessor for the FoW grid (fow_res()*fow_res() flat array, row-major)
	// player 0's grid is fow_grid_ (hashed for lockstep). Another player's grid
	// exists once it is the FoW viewer (set_fow_viewer); until then player 1
	// falls back to the Petra AI's grid when Petra plays it, and others to
	// all-unexplored.
	const uint8_t* fow_grid(uint8_t player = 0) const {
		// A defeated player observes: the whole map is revealed (0 A.D. parity).
		if (player < MAX_PLAYERS && ((defeated_mask_ >> player) & 1)) return revealed_fow_.data();
		if (player == 0 || player >= MAX_PLAYERS) return fow_grid_.data();
		if (player_fow_[player].size() == fow_grid_.size()) return player_fow_[player].data();
		if (player == PETRA_PLAYER && petra_runs_for(PETRA_PLAYER)) return petra_fow_.data();
		return unexplored_fow_.data();
	}
	int fow_res() const { return grid_res_; }
	// Which non-zero player's fog tick_fow also computes (the local client's
	// player in multiplayer). Presentation-only: not hashed, not used by the sim.
	void set_fow_viewer(uint8_t player) { fow_viewer_ = player; }
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
		bool is_burning = false; // Structure fire under 33% HP

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

		// 0 A.D. Upgrade.js: the structure this one is turning into
		// (BLD_TYPE_COUNT = not upgrading) and the resources paid for it.
		uint8_t upgrade_to = BLD_TYPE_COUNT;
		float upgrade_timer = 0.0f;
		float upgrade_duration = 0.0f;
		std::array<float, RES_COUNT> upgrade_paid{};

		// 0 A.D. Gate.js (BLD_GATE only). An unlocked gate never blocks long-range
		// pathfinding and blocks movement only while closed; a locked gate stays
		// closed and blocks both.
		bool gate_open = false;
		bool gate_locked = false;
	};
	// Whether `b` is rasterized into the nav grid (0 A.D. Obstruction BlockPathfinding).
	static bool blocks_pathfinding(const Building &b) {
		return building_blocks_movement(b.type) && (b.type != BLD_GATE || b.gate_locked);
	}
	// Whether units collide with `b` right now (0 A.D. Obstruction BlockMovement).
	static bool blocks_movement_now(const Building &b) {
		return building_blocks_movement(b.type) && !(b.type == BLD_GATE && b.gate_open);
	}

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

	// Spawns the owner civ's unit for u_type (or roster template `tmpl`) with its
	// 0 A.D. stats. hp > 0 overrides the template's health.
	uint32_t spawn_unit(float x, float z, uint8_t team = 0, UnitType u_type = UNIT_VILLAGER, float hp = 0.0f,
			uint16_t tmpl = CIV_UNIT_TEMPLATE_NONE);
	// Stats of the unit at SoA index i (its civ roster template, else its class).
	const UnitTypeInfo &unit_info_at(size_t i) const { return civ_template_unit_info(units_.tmpl[i], units_.unit_type[i]); }
	uint16_t unit_template(uint32_t unit_id) const;
	// 0 A.D. team bonuses (data/auras/teambonuses): the product a player gets on `stat`
	// from its own civ's bonus and its allies' (same team, still in the match).
	// See civ_team_bonus_mult() for the arguments.
	float team_bonus_mult(uint8_t pid, uint8_t stat, int8_t res = -1, uint16_t tmpl = CIV_UNIT_TEMPLATE_NONE,
		uint8_t building = BLD_TYPE_COUNT) const;
	// civ_bit() mask of the civs of `pid`'s allies still in the match (owning a building or unit).
	uint16_t ally_civ_mask(uint8_t pid) const;
	// Resource cost of training template `tmpl` for `pid` after team bonuses, indexed by ResourceType.
	void unit_train_cost(uint8_t pid, uint16_t tmpl, const UnitTypeInfo &info, float out[RES_COUNT]) const;
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

	// Sets the map extent and resizes the nav, FoW and territory grids to match.
	// Call before spawning entities (after reset()).
	void set_map_half(float h);
	// Map loading: defer the nav-grid and territory rebuild that every finished
	// building triggers until end_bulk_spawn(), which does it once. end also
	// marks water on the nav grid, so call it even for maps with no buildings.
	void begin_bulk_spawn() { bulk_spawn_ = true; }
	void end_bulk_spawn();
	void set_terrain_data(const std::vector<float> &heights, int res, float water_h) {
		terrain_heights_ = heights;
		terrain_res_ = res;
		terrain_water_height_ = water_h;
		fow_cell_height_.clear();
		fow_cell_hmax_.clear();
		fow_block_hmax_.clear();
	}
	void set_map_info(const std::string &id, const std::string &name, const std::string &biome) {
		current_map_id_ = id;
		current_map_name_ = name;
		current_map_biome_ = biome;
	}

	// Commands
	void cmd_move(const uint32_t *unit_ids, size_t count, float tx, float tz);
	void cmd_stop(const uint32_t *unit_ids, size_t count);
	// Player commands (0 A.D. Commands.js): `player` is the issuing player id. Each one
	// rejects buildings the player doesn't own and charges/refunds that player's stock.
	uint32_t cmd_build(uint8_t player, int cell_x, int cell_y, uint8_t b_type);
	bool cmd_train(uint8_t player, uint32_t building_id, UnitType u_type);
	// Train a specific roster unit (a hero, a champion, a second unit of the same class).
	bool cmd_train_template(uint8_t player, uint32_t building_id, uint16_t tmpl);
	// P3 maintainFleet upkeep seam (0 A.D. navalManager.js:507-543): team-aware
	// naval queue entry used by TurnManager NET_CMD_MAINTAIN_FLEET. Unlike
	// cmd_train (team-0/human only) this honours the building's own team
	// resources (Petra team 1 via petra_try_pay) so Petra docks can train.
	bool cmd_maintain_fleet(uint32_t building_id, UnitType u_type, uint8_t team);
	bool cmd_cancel_train(uint8_t player, uint32_t building_id, size_t queue_idx);
	void cmd_set_rally_point(uint8_t player, uint32_t building_id, float rx, float rz, uint32_t res_id = 0, uint32_t bld_id = 0);

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
	bool cmd_breed_livestock(uint8_t player, uint32_t building_id);

	// Naval Transport Logistics (GarrisonHolder.js / UnitAI.js)
	static constexpr int TRANSPORT_CAPACITY = 15;
	void order_board_transport(const uint32_t *unit_ids, size_t count, uint32_t transport_id);
	void order_disembark(uint32_t transport_id, float target_x, float target_z);
	void order_disembark_all(uint32_t transport_id);
	void order_disembark_single(uint32_t transport_id, uint32_t unit_id);
	const std::vector<uint32_t> *get_transport_passengers(uint32_t transport_id) const;
	const std::unordered_map<uint32_t, std::vector<uint32_t>> &transport_passengers() const { return transport_passengers_; }
	size_t get_transport_passenger_count(uint32_t transport_id) const;
	bool is_transport(uint32_t unit_id) const;
	// Land siege that carries troops (0 A.D. GarrisonHolder): siege towers and rams.
	// Returns its capacity, 0 for anything else. `i` is a SoA index.
	int siege_garrison_capacity(size_t i) const;
	bool is_siege_tower_at(size_t i) const;
	// Garrisoned Infantry in a siege tower = arrows per volley (capped).
	int siege_tower_arrows(size_t i) const;
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
	// 0 A.D. "Last Man Standing": allies don't share a win; the match goes on until one player is left.
	bool last_man_standing() const { return last_man_standing_; }
	void set_last_man_standing(bool on) { last_man_standing_ = on; }
	// 0 A.D. population limit per player (game setup); 0 means unlimited.
	int pop_cap() const { return pop_cap_; }
	void set_pop_cap(int cap) { pop_cap_ = std::max(0, cap); }
	// 0 A.D. ceasefire: no player can attack another for the first N seconds of the match.
	float ceasefire_seconds() const { return ceasefire_seconds_; }
	void set_ceasefire_seconds(float s) { ceasefire_seconds_ = std::max(0.0f, s); }
	float ceasefire_remaining() const { return std::max(0.0f, ceasefire_seconds_ - (float)ticks_ * FIXED_STEP); }
	bool ceasefire_active() const { return ceasefire_remaining() > 0.0f; }
	// Whether a unit or structure of player a may attack one of player b right now.
	bool can_attack(uint8_t a, uint8_t b) const { return !ceasefire_active() && is_hostile(a, b); }
	// Player 0's side for victory: itself, game-setup teammates, and mutual diplomatic allies.
	bool is_ally_of_player0(uint8_t pid) const { return pid == 0 || same_team(0, pid) || is_mutual_ally(pid); }
	// Match result from pid's seat: pid is one of the winners once the match has ended.
	bool is_victory_for(uint8_t pid) const {
		if (!victory_ && !game_over_) return false;
		if (winner_mask_ || game_over_) return pid < MAX_PLAYERS && ((winner_mask_ >> pid) & 1);
		return victory_ == is_ally_of_player0(pid); // wave win set without a mask
	}
	// 0 A.D. Player.js "defeated": a player out of the match. The match goes on until
	// one side (or, under Last Man Standing, one player) is left; the defeated watch.
	bool is_defeated(uint8_t pid) const { return pid < MAX_PLAYERS && ((defeated_mask_ >> pid) & 1); }
	uint8_t defeated_mask() const { return defeated_mask_; }
	uint8_t winner_mask() const { return winner_mask_; }
	// Players that have held units or structures this match (only they can be defeated).
	uint8_t participant_mask() const { return participant_mask_; }
	// pid and every participant allied with it, or pid alone under Last Man Standing.
	uint8_t side_of(uint8_t pid) const;

	// Battle Alerts (AlertRaiser.js)
	void raise_alert(BattleAlert::Type type, float x, float z, uint32_t entity_id, uint8_t team = 0);
	const std::vector<BattleAlert> &pending_alerts() const { return pending_alerts_; }
	void clear_alerts() { pending_alerts_.clear(); }

	bool cmd_research(uint8_t player, uint32_t building_id, uint8_t tech_id);
	// 0 A.D. Upgrade.js: turn a finished structure into building_upgrade()'s
	// target in place, keeping its health fraction, capture share and garrison.
	enum UpgradeCheck : uint8_t {
		UPGRADE_OK = 0, UPGRADE_ERR_NONE, UPGRADE_ERR_OWNER, UPGRADE_ERR_BUSY, UPGRADE_ERR_PHASE, UPGRADE_ERR_COST
	};
	UpgradeCheck upgrade_check(uint8_t player_id, uint32_t building_id) const;
	bool cmd_upgrade_building(uint8_t player_id, uint32_t building_id);
	// Stops an upgrade in progress and refunds what was paid (Upgrade.js CancelUpgrade).
	bool cmd_cancel_upgrade(uint8_t player_id, uint32_t building_id);
	// 0 A.D. Gate.js LockGate / UnlockGate: the owner keeps its own units out too.
	bool cmd_lock_gate(uint8_t player_id, uint32_t building_id, bool lock);
	// Maximum hit points of a `type` structure owned by `team`: template x civ bonus x techs.
	float structure_max_hp(uint8_t type, uint8_t team) const;
	// 0 A.D. Barter.js: sell 100 or 500 (DEAL_AMOUNT x 1 or BATCH_SIZE) of one
	// resource for another. Needs a finished Market owned by `player`.
	bool cmd_barter(uint8_t player, ResourceType sell, ResourceType buy, int amount = 100);
	bool can_barter(uint8_t player) const;
	// What selling `amount` of `sell` for `buy` would give right now (0 if invalid).
	int barter_quote(ResourceType sell, ResourceType buy, int amount) const;
	bool cmd_set_diplomacy(uint8_t player_id, uint8_t target, uint8_t stance);
	// 0 A.D. Player.js TributeResource: `from` sends `amounts` (indexed by
	// ResourceType) to `to`. All or nothing; both players must be in the
	// match. The one tribute path for the UI, lockstep cmd 25 and Petra.
	bool cmd_tribute(uint8_t from, uint8_t to, const int amounts[RES_COUNT]);
	bool cmd_tribute(uint8_t from, uint8_t to, uint8_t res_type, int amount);
	// G3 Market/Trader (0 A.D. Market.js CalculateTraderGain, Trader.js, globalscripts/Trade.js)
	struct TradeGain {
		int trader = 0;  // paid to the trader's owner
		int market1 = 0; // international bonus to the owner of the market the leg starts at
		int market2 = 0; // international bonus to the owner of the market the leg ends at
	};
	TradeGain calculate_trade_gain(const Building &from, const Building &to, uint8_t trader_owner) const;
	int calculate_trader_gain(const Building& mA, const Building& mB, uint8_t team) const { return calculate_trade_gain(mA, mB, team).trader; }
	// Trader.js CanTrade: a finished Market whose owner `trader_owner` is not hostile to.
	bool can_trade_at(uint8_t trader_owner, const Building &b) const;
	// True when `player` has two markets it can trade between (own, allied or neutral).
	bool has_trade_route(uint8_t player) const;
	void order_trade(const uint32_t *unit_ids, size_t count, uint32_t market_a_id, uint32_t market_b_id);
	void order_trade_auto(const uint32_t *unit_ids, size_t count); // farthest pair of tradable markets
	float get_market_buy_price(ResourceType t) const { return market_buy_price(t); }
	float get_market_sell_price(ResourceType t) const { return market_sell_price(t); }
	int get_market_history(ResourceType t) const { return market_trade_history_[t]; }
	float barter_price_diff(uint8_t res) const { return res < RES_COUNT ? barter_price_diff_[res] : 0.0f; }
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
		const uint16_t *tmpl; // civ roster template (civ_unit_template), CIV_UNIT_TEMPLATE_NONE = generic
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
	// Seated and not defeated (every slot counts when none is seated, as in tests).
	bool player_in_match(uint8_t pid) const;
	bool same_team(uint8_t a, uint8_t b) const { return a == b || (a < MAX_PLAYERS && b < MAX_PLAYERS && players_[a].team == players_[b].team); }

	// Whole-unit stock for the HUD; defaults to player 0 for single-player callers and tests.
	int food(uint8_t pid = 0) const { return (int)player(pid).food(); }
	int wood(uint8_t pid = 0) const { return (int)player(pid).wood(); }
	int gold(uint8_t pid = 0) const { return (int)player(pid).gold(); }
	int stone(uint8_t pid = 0) const { return (int)player(pid).stone(); }
	// GameSetup starting-resources option (0 A.D. page_gamesetup "Starting Resources").
	// The lobby setting is one global amount in 0 A.D., so every player slot gets
	// it, not just player 0; use set_player_resources for one slot on its own.
	// Must be called AFTER map load: reset()/generate() overwrite stocks.
	void set_starting_resources(int food, int wood, int gold, int stone) {
		for (uint8_t p = 0; p < MAX_PLAYERS; ++p) set_player_resources(p, food, wood, gold, stone);
	}

	float food_rate(uint8_t pid = 0) const { return player(pid).gathered_window[RES_FOOD] * 6.0f; } // per min approx
	float wood_rate(uint8_t pid = 0) const { return player(pid).gathered_window[RES_WOOD] * 6.0f; }
	float gold_rate(uint8_t pid = 0) const { return player(pid).gathered_window[RES_GOLD] * 6.0f; }
	float stone_rate(uint8_t pid = 0) const { return player(pid).gathered_window[RES_STONE] * 6.0f; }

	int pop_current(uint8_t player = 0) const;
	int pop_max(uint8_t player = 0) const;
	// Ownership checks for player commands (0 A.D. Commands.js FilterEntityList).
	bool owns_unit(uint8_t player, uint32_t unit_id) const;
	bool owns_building(uint8_t player, uint32_t building_id) const;
	// Copies the ids of live units `player` owns into `out` (cleared first), keeping order.
	void filter_owned_units(uint8_t player, const uint32_t *unit_ids, size_t count, std::vector<uint32_t> &out) const;
	int wave_number() const { return wave_number_; }
	float wave_time_remaining() const { return std::max(0.0f, 35.0f - wave_timer_); }
	bool is_game_over() const { return game_over_; }
	bool is_victory() const { return victory_; }
	uint8_t player_phase(uint8_t pid = 0) const { return player(pid).phase; }
	void set_player_civ(uint8_t civ) { players_[0].civ = (Civilization)civ; }
	uint8_t player_civ(uint8_t pid = 0) const { return player(pid).civ; }
	void set_team_civ(uint8_t team, uint8_t civ) { if (team < MAX_PLAYERS) players_[team].civ = (Civilization)civ; }
	uint8_t team_civ(uint8_t team) const { return (team < MAX_PLAYERS) ? players_[team].civ : CIV_ATHENIANS; }
	void set_biome(uint8_t b) { biome_ = b; }
	uint8_t biome() const { return biome_; }
	void set_map_preset(uint8_t m) { map_preset_ = m; }
	uint8_t map_preset() const { return map_preset_; }
	bool is_tech_researched(uint8_t tech_id) const { return players_[0].has_tech(tech_id); }
	// Why cmd_research would refuse: TECH_OK when `player` may start `tech_id`
	// now (building_id 0 skips the building checks).
	enum ResearchCheck : uint8_t {
		TECH_OK = 0, TECH_ERR_INVALID, TECH_ERR_RESEARCHED, TECH_ERR_IN_PROGRESS, TECH_ERR_CIV,
		TECH_ERR_PHASE, TECH_ERR_PREREQUISITE, TECH_ERR_STRUCTURES, TECH_ERR_BUILDING, TECH_ERR_COST
	};
	ResearchCheck research_check(uint8_t player_id, uint8_t tech_id, uint32_t building_id = 0) const;
	// 0 A.D. Heal/Range and Heal/Interval after the player's technologies.
	float healer_range(uint8_t player_id) const;
	float healer_rate(uint8_t player_id) const; // hit points per second
	// 0 A.D. unlock_champion_* / unlock_civilians_house_*: a unit the owner's
	// civ gates behind an unlock technology can only be trained once it is in.
	// With `tmpl` (a roster row) the row's own 0 A.D. unlock tech decides.
	uint8_t train_unlock_tech(uint8_t player_id, uint8_t unit_type, uint8_t bld_type,
			uint16_t tmpl = CIV_UNIT_TEMPLATE_NONE) const;
	bool can_train_at(uint8_t player_id, uint8_t unit_type, uint8_t bld_type,
			uint16_t tmpl = CIV_UNIT_TEMPLATE_NONE) const;
	// 0 A.D. phase_town / phase_city requirement: finished Village (1) or Town (2) class structures.
	int phase_structures(uint8_t player_id, uint8_t phase) const;
	bool unit_has_order(uint32_t uid) const { int32_t idx = lookup_unit(uid); return idx>=0 && (size_t)idx < units_.count && units_.has_order[idx]; }
	const Grid<uint8_t>& territory_grid() const { return territory_grid_; }
	const Grid<uint8_t>& influence_grid() const { return influence_grid_; }
	const NavGrid& nav_grid() const { return nav_grid_; }
	// Barter.js GetPrices as a fraction of a resource's true price (100):
	// buy = (100 + CONSTANT_DIFFERENCE + diff) / 100, sell = (100 - CONSTANT_DIFFERENCE + diff) / 100.
	float market_buy_price(uint8_t res) const { return (res < RES_COUNT) ? (110.0f + barter_price_diff_[res]) / 100.0f : 1.0f; }
	float market_sell_price(uint8_t res) const { return (res < RES_COUNT) ? (90.0f + barter_price_diff_[res]) / 100.0f : 1.0f; }

	// Diplomacy & Tribute (DiplomacyDialog.js)
	enum DiplomacyStance : uint8_t {
		DIPLO_ALLY = 0,
		DIPLO_NEUTRAL = 1,
		DIPLO_ENEMY = 2
	};
	// diplomacy_[a][b] is player a's own stance toward player b, the 0 A.D.
	// Diplomacy.js model (every player keeps its own row; the diagonal is self).
	// The one-argument calls below are player 0's row, which is what the
	// single-player HUD, the capture rules and Petra still ask for.
	uint8_t get_diplomacy(uint8_t from, uint8_t to) const {
		return (from < MAX_PLAYERS && to < MAX_PLAYERS) ? diplomacy_[from][to] : (uint8_t)DIPLO_ENEMY;
	}
	uint8_t get_diplomacy(uint8_t player_id) const { return get_diplomacy(0, player_id); }
	uint8_t get_diplomacy_from(uint8_t player_id, uint8_t other) const { return get_diplomacy(player_id, other); }
	void set_diplomacy(uint8_t from, uint8_t to, uint8_t stance) {
		if (from < MAX_PLAYERS && to < MAX_PLAYERS && from != to && stance <= DIPLO_ENEMY)
			diplomacy_[from][to] = stance;
	}
	void set_diplomacy(uint8_t player_id, uint8_t stance) { set_diplomacy(0, player_id, stance); }
	void set_mutual_ally(uint8_t a, uint8_t b) {
		set_diplomacy(a, b, DIPLO_ALLY);
		set_diplomacy(b, a, DIPLO_ALLY);
	}
	void set_mutual_ally(uint8_t player_id) { set_mutual_ally(0, player_id); }
	// Allied: the same alliance team from game setup, or a stance both players
	// chose. Teams win over stances, as 0 A.D.'s locked teams do.
	bool is_ally(uint8_t a, uint8_t b) const {
		if (a == b) return true;
		if (a >= MAX_PLAYERS || b >= MAX_PLAYERS) return false;
		if (players_[a].team == players_[b].team) return true;
		return diplomacy_[a][b] == DIPLO_ALLY && diplomacy_[b][a] == DIPLO_ALLY;
	}
	bool is_mutual_ally(uint8_t player_id) const { return player_id != 0 && is_ally(0, player_id); }
	// Hostile: whom a may shoot at. a's own stance decides, so a one-sided
	// alliance leaves a holding fire while b keeps attacking (0 A.D. parity).
	// Teammates are never hostile whatever the stances say.
	bool is_hostile(uint8_t a, uint8_t b) const {
		if (a == b) return false;
		if (a >= MAX_PLAYERS || b >= MAX_PLAYERS) return true;
		if (players_[a].team == players_[b].team) return false;
		return diplomacy_[a][b] == DIPLO_ENEMY;
	}
	// Rewrite every stance from the alliance teams: teammates ally, everyone
	// else enemy. For game setup, once the lobby's teams are assigned.
	void apply_team_diplomacy() {
		for (uint8_t a = 0; a < MAX_PLAYERS; ++a)
			for (uint8_t b = 0; b < MAX_PLAYERS; ++b)
				diplomacy_[a][b] = (a == b || players_[a].team == players_[b].team) ? DIPLO_ALLY : DIPLO_ENEMY;
	}
	// Player 0's row, 8 contiguous bytes, for the HUD and the minimap.
	const uint8_t *diplomacy_state() const { return diplomacy_[0]; }
	const uint8_t *diplomacy_row(uint8_t player_id) const { return diplomacy_[player_id < MAX_PLAYERS ? player_id : 0]; }
	// Player 0 tributes `player_id` (cmd_tribute(0, ...)).
	bool send_tribute(uint8_t player_id, uint8_t res_type, int amount);
	// The current Petra player tributes player 0 (cmd_tribute(petra_pid_, 0, ...)).
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

	const uint8_t* petra_fow_grid() const { return petra_fow_.data(); }

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
	// True when Petra drives this player: seated active AI slots, or player 1 when no slot is seated.
	bool petra_runs_for(uint8_t pid) const;
	// InitGame.js "AI Bonus": Petra players gather and trade at the difficulty's
	// rate and build, train and research at its time multiplier; 1 for humans.
	float ai_gather_mult(uint8_t pid) const { return petra_runs_for(pid) ? petra_config_.gather_rate : 1.0f; }
	float ai_time_mult(uint8_t pid) const { return petra_runs_for(pid) ? petra_config_.build_time : 1.0f; }
	uint8_t petra_difficulty() const { return petra_config_.difficulty; }
	uint8_t petra_behavior() const { return petra_config_.behavior; }
	const PetraConfig &petra_config() const { return petra_config_; }
	// Record a tribute so the receiving AI can answer a pending diplomacy request.
	void note_tribute(uint8_t from, uint8_t to, uint8_t res, int amount) {
		if (from < MAX_PLAYERS && to < MAX_PLAYERS && res < RES_COUNT && amount > 0)
			petra_tribute_inbox_[to][from][res] += (float)amount;
	}
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
	// Wall-clock cost of the map-size-sensitive subsystems (tools/bench_map_scale.cpp).
	// Diagnostic only: never read by the simulation, so lockstep is unaffected.
	struct MapScaleStats {
		uint64_t fow_us = 0, fow_calls = 0;
		uint64_t path_us = 0, path_calls = 0;
		uint64_t territory_us = 0, territory_calls = 0;
		uint64_t threat_us = 0, threat_calls = 0;
	};
	const MapScaleStats &map_scale_stats() const { return map_scale_stats_; }

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
	// Technology modifiers per player, derived from PlayerState::techs.
	enum GatherKind : uint8_t { GK_GRAIN, GK_FRUIT, GK_MEAT, GK_FISH, GK_WOOD, GK_STONE, GK_METAL, GK_COUNT };
	struct TechMods {
		float gather[UNIT_TYPE_COUNT][GK_COUNT];
		int16_t carry[UNIT_TYPE_COUNT][RES_COUNT];
		float attack[UNIT_TYPE_COUNT];
		float health[UNIT_TYPE_COUNT];
		float speed[UNIT_TYPE_COUNT];
		float range[UNIT_TYPE_COUNT];
		float armor[UNIT_TYPE_COUNT][3]; // hack, pierce, crush
		float train_time[UNIT_TYPE_COUNT];
		float heal_range[UNIT_TYPE_COUNT];    // added metres
		float heal_interval[UNIT_TYPE_COUNT]; // < 1 heals faster
		float trade_gain[UNIT_TYPE_COUNT];
		float bld_health[BLD_TYPE_COUNT];
		float bld_attack[BLD_TYPE_COUNT];
		float bld_range[BLD_TYPE_COUNT];
		float bld_build_time[BLD_TYPE_COUNT];
		float bld_research_time[BLD_TYPE_COUNT];
		float bld_pop_bonus[BLD_TYPE_COUNT];
		float bld_arrow_mul[BLD_TYPE_COUNT];
		float bld_arrow_add[BLD_TYPE_COUNT];
	};
	void compute_tech_mods(uint8_t player_id, TechMods &out) const;
	void rebuild_all_tech_mods();
	void complete_research(uint8_t player_id, uint8_t tech_id);
	void apply_tech_mods_to_unit(size_t idx);
	const TechMods &tech_mods(uint8_t player_id) const { return tech_mods_[player_id < MAX_PLAYERS ? player_id : 0]; }
	int32_t carry_cap(size_t unit_idx, uint8_t res) const;
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
	void tick_upgrades(float dt);
	// Unloads land siege below SIEGE_EJECT_HEALTH or dead (GarrisonHolder EjectHealth / OnDestroy).
	void tick_siege_garrisons();
	bool can_board(size_t transport_idx, size_t unit_idx) const;
	void cancel_upgrade(Building &b, uint8_t refund_to);
	void finish_upgrade(Building &b);
	void tick_barter(float dt);
	// Player.js GetNextTradingGoods with the default equal split (25% each).
	uint8_t next_trading_goods();
	// Pays a trade gain into `player`'s stock and its gather statistics.
	void credit_trade(uint8_t player, uint8_t res, int amount);
	void tick_trickle(float dt);
	void tick_upkeep(float dt);
	void tick_entity_limits(float dt);
	void tick_death_damage(float dt);
	void tick_loot(float dt);
	void tick_gates(float dt);
	void tick_waves(float dt);
	void tick_petra_ai(float dt);
	void tick_petra_ai_player(float dt);
	void tick_fauna(float dt);
	void tick_fow();  // updates FoW each sim tick
	template <typename SharesVision>
	void update_player_fow(std::vector<uint8_t> &grid, SharesVision shares_vision);
	void ensure_fow_cell_heights();
	void reveal_fow(std::vector<uint8_t> &grid, float sx, float sz, float vr) const;
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
	bool last_man_standing_ = false;
	int pop_cap_ = 200;
	float ceasefire_seconds_ = 0.0f;
	float relic_victory_timer_ = 0.0f;
	std::vector<BattleAlert> pending_alerts_;
	float last_alert_time_[6] = {};
	std::unordered_map<uint32_t, size_t> unit_id_to_idx_;
	std::vector<int32_t> unit_idx_by_id_;
	std::unordered_map<uint32_t, size_t> res_id_to_idx_;
	std::array<TechMods, MAX_PLAYERS> tech_mods_;
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
	PlayerState &petra_ps() { return players_[petra_pid_]; }
	Civilization civ_of(uint8_t pid) const { return (pid < MAX_PLAYERS) ? players_[pid].civ : CIV_ATHENIANS; }
	const PlayerState &petra_ps() const { return players_[petra_pid_]; }
	// Player Petra is currently thinking for. tick_petra_ai sets it for each AI
	// slot in turn and leaves it at PETRA_PLAYER between ticks.
	uint8_t petra_pid_ = PETRA_PLAYER;
	// Hostile to the AI being run (is_hostile: teams, then its own stance).
	bool petra_foe(int t) const { return t >= 0 && t < MAX_PLAYERS && is_hostile(petra_pid_, (uint8_t)t); }
	float rate_window_timer_ = 0.0f;
	float barter_timer_ = 0.0f;
	float upkeep_timer_ = 0.0f;
	float entity_limits_timer_ = 0.0f;
	float gate_timer_ = 0.0f;
	static constexpr size_t MAX_RECENT_DEATHS = 64;
	std::array<RecentDeath, MAX_RECENT_DEATHS> recent_deaths_{};
	size_t recent_death_head_ = 0;

	// G3 Market / Barter dynamic prices (indexed by ResourceType: 0=WOOD,1=GOLD,2=STONE,3=FOOD)
	// Barter.js priceDifferences: per resource, in percent of the true price, within +-89.
	float barter_price_diff_[RES_COUNT] = {};
	int market_trade_history_[4] = {};
	std::unordered_map<uint32_t, std::pair<uint32_t,uint32_t>> trader_routes_; // unit_id -> (marketA_id, marketB_id)
	std::unordered_map<uint32_t,int> trader_leg_; // unit_id -> 0=A->B, 1=B->A (which target next)

	// Row a = player a's stances; set in reset() (self ally, everyone else enemy).
	uint8_t diplomacy_[MAX_PLAYERS][MAX_PLAYERS] = {};
	uint8_t biome_ = BIOME_AEGEAN;
	uint8_t map_preset_ = MAP_AEGEAN_ARCHIPELAGO;

	float wave_timer_ = 0.0f;
	int wave_number_ = 0;
	bool game_over_ = false;
	bool victory_ = false;
	uint8_t participant_mask_ = 0; // save v10
	uint8_t defeated_mask_ = 0;
	uint8_t winner_mask_ = 0;
	float wonder_hold_[MAX_PLAYERS] = {}; // seconds each player has held a finished wonder

	MatchRecord record_;

	uint64_t tick_us_ = 0;
	uint64_t ticks_ = 0;
	mutable MapScaleStats map_scale_stats_;
	FrameArena frame_arena_{4 * 1024 * 1024};

	// Petra AI Subsystems & Heuristics (0 A.D. Petra.js / dangerMap.js / military.js / economy.js / attackPlan.js)
	// Phase6/G9: 64x64 danger map; cells stretch with the map (2m on the default 128m map)
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
	PetraDiplomacy petra_diplo_;
	// Tribute paid to each player since its Petra last looked: [to][from][resource].
	float petra_tribute_inbox_[MAX_PLAYERS][MAX_PLAYERS][RES_COUNT] = {};
	float petra_defense_timer_ = 0.0f;
	float petra_victory_timer_ = 0.0f;
	float petra_diplo_timer_ = 0.0f;
	float ai_wonder_victory_timer_ = 0.0f;
	bool pick_petra_safe_staging(float base_x, float base_z, float target_x, float target_z, float &out_sx, float &out_sz) const;
	uint32_t find_petra_attack_target(float &out_tx, float &out_tz) const;
	uint32_t find_petra_raid_target(float &out_tx, float &out_tz) const;
	bool petra_unit_reserved(uint32_t uid) const;

	std::vector<PetraBase> petra_bases_;
	// Foundations whose build progress has stalled; builders that could not
	// reach one are banned from it so the next attempt uses someone else.
	struct PetraFoundationWatch {
		uint32_t id = 0;
		float progress = 0.0f;
		float stall = 0.0f;
		std::vector<uint32_t> banned;
	};
	std::vector<PetraFoundationWatch> petra_fnd_watch_;
	mutable Petra petra_fleets_; // per-sea warship registry (navalManager.seaWarShips)
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
	void petra_refresh_queue_accounts(float dt);
	void petra_staff_foundations();
	void petra_manage_fields(int target_food);
	int petra_villager_target() const;
	bool petra_can_place(int cell_x, int cell_y, int footprint) const;
	bool petra_try_pay(uint8_t queue, float food, float wood, float gold, float stone);
	void petra_say(uint8_t kind, float x = 0.0f, float z = 0.0f, uint8_t to = 0xFF, int amount = 0, uint8_t res = 0);
	void petra_handle_diplomacy_request(uint8_t player, uint8_t type);
	void petra_change_diplomacy(uint8_t player, uint8_t stance);
	void petra_last_man_standing_check();
	int petra_player_entities(uint8_t player) const;
	bool petra_path_crosses_water(float x0, float z0, float x1, float z1) const;
	bool petra_find_beach(float land_x, float land_z, float toward_x, float toward_z, float &out_x, float &out_z) const;
	bool petra_start_transport(const std::vector<uint32_t> &unit_ids, float from_x, float from_z, float to_x, float to_z);
	uint32_t petra_place_building(uint8_t btype, float near_x, float near_z, int min_r, int max_r, uint8_t queue);

	bool bulk_spawn_ = false;
	int grid_res_ = MIN_GRID_RES; // nav/FoW/territory resolution, grid_res_for_map(map_half_)
	std::vector<uint8_t> fow_grid_ = std::vector<uint8_t>(MIN_GRID_RES * MIN_GRID_RES, 0); // FoW state per tile
	std::vector<uint8_t> petra_fow_ = std::vector<uint8_t>(MIN_GRID_RES * MIN_GRID_RES, 0); // FoW state per tile for Petra AI
	uint8_t fow_viewer_ = 0;
	std::array<std::vector<uint8_t>, MAX_PLAYERS> player_fow_; // per-player FoW for players 1..7 (index 0 unused: fow_grid_)
	std::vector<uint8_t> unexplored_fow_ = std::vector<uint8_t>((size_t)MAX_GRID_RES * MAX_GRID_RES, 0);
	std::vector<uint8_t> revealed_fow_ = std::vector<uint8_t>((size_t)MAX_GRID_RES * MAX_GRID_RES, FOW_VISIBLE);

	// Per-AI Petra state. The petra_* members above hold the state of the AI being
	// run (PETRA_PLAYER between ticks); every other AI slot's state is parked here
	// and swapped in while that AI thinks (0 A.D. runs one Petra instance per AI).
#define RTSO_PETRA_SLOT_FIELDS(X) \
	X(petra_threat_map_) X(petra_strike_force_) X(petra_attack_plan_) X(petra_raid_plan_) \
	X(petra_opening_step_) X(petra_opening_timer_) X(petra_defense_army_) X(petra_emergency_) \
	X(petra_queues_) X(petra_transport_) X(petra_defense_timer_) X(petra_victory_timer_) \
	X(petra_diplo_timer_) X(ai_wonder_victory_timer_) X(petra_bases_) X(petra_fnd_watch_) \
	X(petra_timer_) X(petra_econ_timer_) X(petra_trade_timer_) X(petra_naval_timer_) \
	X(petra_scout_timer_) X(petra_scout_unit_id_) X(petra_scout_angle_) X(petra_scout_radius_) \
	X(petra_fow_) X(petra_fleets_) X(petra_diplo_)
	struct PetraSlot {
		bool initialized = false;
#define RTSO_PETRA_SLOT_DECL(m) decltype(Simulation::m) m{};
		RTSO_PETRA_SLOT_FIELDS(RTSO_PETRA_SLOT_DECL)
#undef RTSO_PETRA_SLOT_DECL
	};
	std::vector<PetraSlot> petra_slots_; // indexed by player id, sized on first use
	void petra_swap_slot(uint8_t pid);
	void petra_reset_ai_state();
	std::vector<float> fow_cell_height_; // terrain height at each FoW cell centre, lazily filled by tick_fow
	std::vector<float> fow_cell_hmax_; // highest heightmap sample an LOS ray can read in each FoW cell, filled with fow_cell_height_
	static constexpr int FOW_LOS_BLOCK = 4; // FoW cells per side of a fow_block_hmax_ block
	std::vector<float> fow_block_hmax_; // fow_cell_hmax_ max per block, filled with fow_cell_height_
	NavGrid nav_grid_; // Phase1: SoA nav mesh (buildings + water), mirrors 0AD Grid<NavcellData>
	Grid<uint8_t> territory_grid_; // grid_res_^2 player id per cell, G2 (CCmpTerritoryManager)
	Grid<uint8_t> influence_grid_; // grid_res_^2 influence count per cell, G2
	// rebuild_territory()'s inputs the last time it filled the grids: territory roots in buildings_
	// order, the grid size and the map extent. Same inputs, same grids, so the 1 Hz refresh skips.
	struct TerritoryRoot {
		float x, z, radius;
		uint8_t team;
		bool operator==(const TerritoryRoot &) const = default;
	};
	std::vector<TerritoryRoot> territory_roots_, territory_roots_scratch_;
	int territory_roots_res_ = 0;
	float territory_roots_half_ = 0.0f;
	std::vector<WaypointPath> unit_paths_; // per-unit path (UNIT_CAP), SoA waypoint_cur/count sync
	// move_units' copy of the buildings that block movement, in buildings_ order, with only the
	// fields nav/building_steering.h reads, so its per-unit scans read a short packed list.
	struct SteerObstacle {
		uint32_t id;
		float x, z, half_size;
		uint8_t type;
		bool wrecked;
	};
	std::vector<SteerObstacle> steer_obstacles_;
	// Closed gates this tick: the only movement blockers the nav grid leaves out (0 A.D.
	// gate Obstruction BlockMovement without BlockPathfinding), so move_units stops units
	// at their edge instead of relying on the soft separation push.
	std::vector<SteerObstacle> closed_gates_;
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
