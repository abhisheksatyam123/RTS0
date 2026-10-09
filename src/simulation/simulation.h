#ifndef RTSO_SIMULATION_H
#define RTSO_SIMULATION_H

#include "../core/memory/arena.h"
#include "../core/memory/soa_storage.h"
#include "grid_nav.h"
#include "nav/obstacle_grid.h"
#include "aura_manager.h"
#include "petra_ai.h"
#include "sim_types.h"
#include "statistics_tracker.h"
#include "triggers.h"
#include "turret_data.h"
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <cmath>
#include <utility>
#include <algorithm>
#include <string>
#include <array>
#include <bit>
#include <iosfwd>

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
	uint32_t source_id = 0; // firing unit or building: Promotion xp and Looter.js loot go to it
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
// Owner of a defeated player's leftovers (0 A.D.'s Gaia): hostile to everyone, owned by no slot.
static constexpr uint8_t GAIA_PLAYER = 0xFE;
// A replay watched as an observer (0 A.D. replay "Observer" perspective): the
// presentation's viewer id that sees the whole map. Never a sim owner.
static constexpr uint8_t OBSERVER_VIEW = 0xFD;

struct PlayerState {
	float res[RES_COUNT] = {};             // indexed by ResourceType
	float gathered_window[RES_COUNT] = {}; // decaying gather-rate window for the HUD
	float used[RES_COUNT] = {};            // everything pay() took (StatisticsTracker resourcesUsed)
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
		used[RES_FOOD] += f; used[RES_WOOD] += w; used[RES_GOLD] += g; used[RES_STONE] += s;
		return true;
	}
	// Gives back a cancelled payment (ProductionQueue.RemoveItem, Upgrade.CancelUpgrade):
	// the statistics count it as never used.
	void refund(float f, float w, float g, float s) {
		add(f, w, g, s);
		used[RES_FOOD] -= f; used[RES_WOOD] -= w; used[RES_GOLD] -= g; used[RES_STONE] -= s;
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
	// Facing of a building nobody rotated: toward -z, the way every building was drawn
	// before rotation was simulated (0 A.D.'s placement default is 3pi/4).
	static constexpr float DEFAULT_BUILDING_ROT = 3.14159265359f;
	static constexpr uint32_t SAVE_VERSION = 31; // save_state's format; see the history beside it
	static constexpr uint32_t SAVE_VERSION_STATISTICS = 30; // per-player StatisticsTracker in tick_state_io
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
		if (player == OBSERVER_VIEW) return revealed_fow_.data();
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
		float rot = DEFAULT_BUILDING_ROT; // 0 A.D. Position rotation: 0 faces +z, pi/2 faces +x (the obstruction stays an axis-aligned square)
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
	// teams[i] is player i's 1-based game-setup team (0 = none, nullptr = all none),
	// for maps that place teams together (Stronghold).
	void generate_random_map(MapType type = MAP_RANDOM_CONTINENTAL, uint32_t seed = 0, int num_players = 2, float map_half = 64.0f,
	                         const uint8_t *teams = nullptr);

	// Spawns the owner civ's unit for u_type (or roster template `tmpl`) with its
	// 0 A.D. stats. hp > 0 overrides the template's health.
	uint32_t spawn_unit(float x, float z, uint8_t team = 0, UnitType u_type = UNIT_VILLAGER, float hp = 0.0f,
			uint16_t tmpl = CIV_UNIT_TEMPLATE_NONE);
	// Stats of the unit at SoA index i (its civ roster template, else its class).
	const UnitTypeInfo &unit_info_at(size_t i) const { return civ_template_unit_info(units_.tmpl[i], units_.unit_type[i]); }
	// 0 A.D. Identity classes, RestrictedClasses, Bonuses and Spread of the unit at SoA index i.
	const CombatProfile &unit_combat_at(size_t i) const { return unit_combat(units_.tmpl[i], units_.unit_type[i]); }
	// Attack.js CanAttack: may unit i attack unit j / building b at all (RestrictedClasses)?
	bool unit_can_target(size_t i, size_t j) const { return combat_can_target(unit_combat_at(i), unit_combat_at(j)); }
	bool unit_can_target_building(size_t i, uint8_t building_type) const {
		return combat_can_target(unit_combat_at(i), building_combat(building_type));
	}
	uint16_t unit_template(uint32_t unit_id) const;
	// 0 A.D. Promotion.js GetRequiredXp for unit index i after the owner's technologies,
	// or -1 when the unit has no next rank.
	float promotion_required_xp(size_t i) const;
	// 0 A.D. TurretHolder / Turretable: ranged infantry stand on the turret points of
	// walls, gates and outposts, visible and fighting from the structure's height.
	bool unit_turretable(size_t i) const;
	uint32_t turret_holder_of(uint32_t unit_id) const; // 0 when not on a turret
	float unit_height_offset(size_t i) const { // above the terrain (a turret point's height)
		if (turret_height_.empty()) return 0.0f;
		auto it = turret_height_.find(units_.id[i]);
		return it == turret_height_.end() ? 0.0f : it->second;
	}
	// Training time multiplier of a template trained above Basic (Cost/BuildTime per rank).
	static float promotion_train_time_mult(uint16_t tmpl);
	// Rank effects (unit_advanced.json, unit_elite.json) for unit index i.
	float rank_melee_armor(size_t i) const;
	float unit_capture_attack_at(size_t i) const;
	float healer_range_of(size_t h) const;
	float heal_amount_of(size_t h) const;
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
	// `angle` is the placement rotation (0 A.D. placementSupport.angle); docks ignore it and face the sea.
	uint32_t cmd_build(uint8_t player, int cell_x, int cell_y, uint8_t b_type, float angle = DEFAULT_BUILDING_ROT);
	// 0 A.D. Commands.js GetDockAngle: the facing of a dock at (x, z) toward the most open water,
	// or NAN when no shoreline is in reach.
	float dock_angle(float x, float z, float half_size) const;
	// Lockstep cmd 5 carries the angle in target_id, in 1/65536ths of a turn from
	// DEFAULT_BUILDING_ROT, so older commands (target_id 0) keep the default facing.
	static uint32_t pack_angle(float angle);
	static float unpack_angle(uint32_t packed);
	bool cmd_train(uint8_t player, uint32_t building_id, UnitType u_type);
	// Train a specific roster unit (a hero, a champion, a second unit of the same class).
	bool cmd_train_template(uint8_t player, uint32_t building_id, uint16_t tmpl);
	bool cmd_train_template_at(uint8_t player, uint32_t building_id, uint16_t tmpl, bool exclusive);
	// P3 maintainFleet upkeep seam (0 A.D. navalManager.js:507-543): team-aware
	// naval queue entry used by TurnManager NET_CMD_MAINTAIN_FLEET. Unlike
	// cmd_train (team-0/human only) this honours the building's own team
	// resources (Petra team 1 via petra_try_pay) so Petra docks can train.
	bool cmd_maintain_fleet(uint32_t building_id, UnitType u_type, uint8_t team);
	bool cmd_cancel_train(uint8_t player, uint32_t building_id, size_t queue_idx);
	void cmd_set_rally_point(uint8_t player, uint32_t building_id, float rx, float rz, uint32_t res_id = 0, uint32_t bld_id = 0);

	// Player orders (0 A.D. Commands.js / UnitAI.js). `queued` is the Shift key: the order
	// goes to the end of each busy unit's order queue instead of replacing its orders.
	// Player orders are forced (order.data.force); Petra's cmd_move walks are not, so its
	// units still turn to fight on the way.
	void order_walk(const uint32_t *unit_ids, size_t count, float tx, float tz, bool queued = false);
	void order_gather(const uint32_t *unit_ids, size_t count, uint32_t resource_id, bool queued = false);
	void order_attack(const uint32_t *unit_ids, size_t count, uint32_t target_unit_id, bool queued = false);
	// allow_capture: capture the structure (0 A.D.'s default right-click) instead of
	// damaging it (Ctrl). Units that can't capture, or structures that can't be
	// captured, fall back to damage.
	void order_attack_building(const uint32_t *unit_ids, size_t count, uint32_t target_building_id,
			bool queued = false, bool allow_capture = false);
	// Attack-move (UnitAI WalkAndFight): fight units, then structures unless units_only,
	// on the way, and carry on to (tx, tz) afterwards.
	void order_attack_move(const uint32_t *unit_ids, size_t count, float tx, float tz,
			bool queued = false, bool units_only = false, bool allow_capture = false);
	void order_patrol(const uint32_t *unit_ids, size_t count, float tx, float tz, bool queued = false);
	void order_build_or_repair(const uint32_t *unit_ids, size_t count, uint32_t building_id, bool queued = false);
	// 0 A.D. "delete-entities": kills the player's own units and destroys its own
	// structures (not ones it holds under half the capture points of).
	void cmd_delete(uint8_t player, const uint32_t *ids, size_t count);
	// 0 A.D. "back-to-work" (UnitAI BackToWork): resume the last gather or build order.
	void order_back_to_work(const uint32_t *unit_ids, size_t count);
	// The unit's current order and its Shift-queued ones (empty if none).
	UnitOrder unit_order(uint32_t unit_id) const;
	const std::vector<UnitOrder> *unit_order_queue(uint32_t unit_id) const;
	void order_set_stance(const uint32_t *unit_ids, size_t count, uint8_t stance);
	// 0 A.D. Pack/Unpack command (UnitAI Order.Pack / Order.Unpack): drops the units' current
	// orders and packs (pack=true) or unpacks them. Units without Pack are ignored.
	void order_pack(const uint32_t *unit_ids, size_t count, bool pack);
	// Seconds a pack or unpack takes for this unit (0 A.D. Pack/Time).
	float pack_time_s(size_t unit_idx) const;
	// 0 A.D. "formation" command (Commands.js): the units form up in `formation` where they
	// stand, in clusters (Commands.js ClusterEntities); FORMATION_NONE disbands. Units that may
	// not use it (UnitAI/Formations, Player formations) are left out.
	void order_set_formation(const uint32_t *unit_ids, size_t count, uint8_t formation);
	// Formation.js / Player.js queries for the formation panel.
	uint32_t player_formation_mask(uint8_t player) const; // Player.js GetFormations
	bool unit_can_use_formation(uint32_t unit_id, uint8_t formation) const; // UnitAI.js CanUseFormation
	bool can_move_into_formation(const uint32_t *unit_ids, size_t count, uint8_t formation) const; // Commands.js
	// Formation controllers (0 A.D. formation entities), for the HUD and tests.
	struct FormationController {
		uint32_t id = 0;
		uint8_t owner = 0;
		uint8_t tmpl = FORMATION_NONE;  // UnitFormation
		uint8_t columnar = 0;           // marching in column (long walk)
		uint8_t order = 0;              // FormationOrder
		float x = 0.0f, z = 0.0f;      // controller position (members' centre when formed)
		float rot = 0.0f;               // facing (0 A.D. rotation: 0 faces +z, pi/2 faces +x)
		float offsets_rot = 0.0f;       // facing the slots were assigned for
		float goal_x = 0.0f, goal_z = 0.0f;
		float shape_timer = 0.0f;       // Formation.js ShapeUpdate interval
		uint8_t pass = 0;               // PassClass the controller paths with
		uint8_t has_offsets = 0;        // slots valid (Formation.js this.offsets)
		uint8_t max_rows_used = 0;
		uint8_t pad_ = 0;
		std::vector<uint32_t> members;
		std::vector<FormationSlot> slots;
		WaypointPath path;
	};
	enum FormationOrder : uint8_t { FORDER_IDLE = 0, FORDER_WALK = 1, FORDER_ATTACK_WALK = 2 };
	const std::vector<FormationController> &formations() const { return formations_; }
	// Controller id of the formation unit_id belongs to, 0 when none.
	uint32_t formation_of_unit(uint32_t unit_id) const;
	void order_garrison(const uint32_t *unit_ids, size_t count, uint32_t building_id, bool queued = false);
	void order_ungarrison(uint32_t building_id);
	// GarrisonHolder.HasEnoughHealth: above EjectHealth of max health, so it may hold a garrison.
	bool structure_has_enough_health(const Building &b) const;
	// Hitpoints per second each unit garrisoned in `b` regains (BuffHeal plus Living Conditions).
	float structure_garrison_heal(const Building &b) const;
	void order_ungarrison_single(uint32_t building_id, uint32_t unit_id);
	// Commands.js unload / unload-all-by-owner: the holder's owner may unload anyone; an
	// ally only its own units. count 0 unloads every unit `player` may unload.
	void cmd_unload(uint8_t player, uint32_t building_id, const uint32_t *unit_ids, size_t count);
	// AlertRaiser.js on a structure of `player`: RaiseAlert sends its civilians (women; a
	// market's land traders) into the nearest structures with room, or stops them;
	// EndOfAlert unloads them and sends them back to work. False when it has no alert.
	bool cmd_raise_alert(uint8_t player, uint32_t building_id);
	bool cmd_end_alert(uint8_t player, uint32_t building_id);
	void order_guard(const uint32_t *unit_ids, size_t count, uint32_t target_unit_id, bool queued = false);
	// UnitAI Heal: healers walk to an injured allied unit and heal it (Heal.js).
	void order_heal(const uint32_t *unit_ids, size_t count, uint32_t target_unit_id, bool queued = false);
	// 0 A.D. "returnresource" (UnitAI ReturnResource): carriers take their load to this
	// dropsite, drop it, then take the next queued order or idle.
	void order_return_resource(const uint32_t *unit_ids, size_t count, uint32_t dropsite_id, bool queued = false);
	bool dropsite_accepts(size_t unit_idx, const Building &b) const; // carried type, owner, finished
	bool can_return_resource(uint32_t unit_id, uint32_t dropsite_id) const; // carries a load b accepts
	void set_unit_hp(uint32_t unit_id, float hp); // scenarios and tests: clamped to 0..max

	// Livestock & Corrals (Corral.js)
	void order_herd(const uint32_t *unit_ids, size_t count, uint32_t fauna_id);
	bool cmd_breed_livestock(uint8_t player, uint32_t building_id);

	// Naval Transport Logistics (GarrisonHolder.js / UnitAI.js)
	static constexpr int TRANSPORT_CAPACITY = 30; // 0 A.D. template_unit_ship_warship_arrow GarrisonHolder Max
	static constexpr float SHIP_LOADING_RANGE = 10.0f * 0.28f; // GarrisonHolder LoadingRange 10 m in RTS0 scale
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
	// Ships as GarrisonHolders (template_unit_ship*.xml): capacity and List per
	// template, LoadingRange 10 m, Pickup, EjectClassesOnDestroy. `i` is a SoA index.
	int ship_garrison_capacity(size_t i) const;
	bool ship_garrison_allowed(size_t ship, uint8_t unit_type) const;
	float ship_loading_reach(size_t ship, size_t unit) const;
	bool ship_unload_point(size_t ship, float &out_x, float &out_z) const;
	bool nearest_water_for_ship(size_t ship, float x, float z, float &out_x, float &out_z) const;
	void unload_sunk_ship(size_t ship);

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
	// 0 A.D. map visibility options: everything visible, or terrain known from the start.
	bool reveal_map() const { return reveal_map_; }
	void set_reveal_map(bool on) { reveal_map_ = on; }
	// Game Setup biome for random maps: BIOME_* or -1 for the map's own.
	void set_biome_override(int b) { biome_override_ = (b >= 0 && b < BIOME_COUNT) ? b : -1; }
	int biome_override() const { return biome_override_; }
	bool explore_map() const { return explore_map_; }
	void set_explore_map(bool on) { explore_map_ = on; }
	// Wonder and Capture the Relic: how long the wonder or all relics must be held, and how
	// many relics a Capture the Relic match has (0 = the map's own). Game Setup passes
	// 0 A.D.'s defaults (20 min, 20 min, 2 relics); the sim keeps RTS0's older 10 min / 3 min
	// for matches started without it.
	float wonder_duration() const { return wonder_duration_; }
	void set_wonder_duration(float s) { wonder_duration_ = std::max(0.0f, s); }
	float relic_duration() const { return relic_duration_; }
	void set_relic_duration(float s) { relic_duration_ = std::max(0.0f, s); }
	int relic_count() const { return relic_count_; }
	void set_relic_count(int n) { relic_count_ = std::clamp(n, 0, 16); }
	// Rising-water maps (0 A.D. Flood's "WaterLevels" and Extinct Volcano's "SeaLevelRise"
	// Game Setup options): Flood's water rises (0, default), stays (1, Shallow) or starts a
	// metre higher (2, Deep); Extinct Volcano's water starts rising after N minutes (default 25).
	enum WaterLevelMode : uint8_t { WATER_LEVEL_RISING = 0, WATER_LEVEL_SHALLOW = 1, WATER_LEVEL_DEEP = 2 };
	uint8_t water_level_mode() const { return water_level_mode_; }
	void set_water_level_mode(uint8_t m) { water_level_mode_ = m <= WATER_LEVEL_DEEP ? m : WATER_LEVEL_RISING; }
	int sea_level_rise_minutes() const { return sea_level_rise_min_; }
	void set_sea_level_rise_minutes(int m) { sea_level_rise_min_ = (uint8_t)std::clamp(m, 0, 60); }

	// Map trigger scripts (0 A.D. Trigger.js; see triggers.h). Map generation registers
	// the map's own triggers; tick() runs their timers and the sim calls trigger_event().
	const TriggerState &trigger_state() const { return triggers_; }
	uint32_t trigger_hash() const { return triggers_.hash(); }
	void trigger_event(uint8_t event, const TriggerEventData &data = {});
	// Time notifications (GuiInterface.AddTimeNotification) shown to `player` right now.
	std::vector<const TimeNotification *> time_notifications_for(uint8_t player) const;
	// WaterManager.SetWaterLevel as the rising-water scripts use it: moves the water
	// to `h`, drowns organic units and animals more than `drown_depth` below it, and
	// takes away submerged structures and resources (0 A.D. turns them into
	// decorative actors). Ships and docks float.
	void set_water_level(float h, float drown_depth);
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
	// match. The one tribute path for the UI, lockstep cmd 27 and Petra.
	// 0 A.D. Player.js SetTradingGoods / GetTradingGoods: the share (percent) of
	// each resource, by ResourceType, that `player`'s traders bring back. A set
	// that doesn't sum to 100 is refused.
	bool cmd_set_trading_goods(uint8_t player, const uint8_t pct[RES_COUNT]);
	const uint8_t *trading_goods(uint8_t player) const {
		return trading_goods_[player < MAX_PLAYERS ? player : 0];
	}
	bool cmd_tribute(uint8_t from, uint8_t to, const int amounts[RES_COUNT]);
	bool cmd_tribute(uint8_t from, uint8_t to, uint8_t res_type, int amount);
	// G3 Market/Trader (0 A.D. Market.js CalculateTraderGain, Trader.js, globalscripts/Trade.js)
	struct TradeGain {
		int trader = 0;  // paid to the trader's owner
		int market1 = 0; // international bonus to the owner of the market the leg starts at
		int market2 = 0; // international bonus to the owner of the market the leg ends at
	};
	// `trader_type` UNIT_MERCHANT_SHIP takes the civ's merchant ship bonus instead of the land trader's.
	TradeGain calculate_trade_gain(const Building &from, const Building &to, uint8_t trader_owner,
			uint8_t trader_type = UNIT_TRADER) const;
	int calculate_trader_gain(const Building& mA, const Building& mB, uint8_t team) const { return calculate_trade_gain(mA, mB, team).trader; }
	// Trader.js CanTrade: a finished market of the trader's kind (a Market for land
	// traders, a Dock for merchant ships) whose owner `trader_owner` is not hostile to.
	bool can_trade_at(uint8_t trader_owner, const Building &b, uint8_t trader_type = UNIT_TRADER) const;
	// True when `player` has two markets of that kind it can trade between (own, allied or
	// neutral); for merchant ships, two docks on the same sea.
	bool has_trade_route(uint8_t player, uint8_t trader_type = UNIT_TRADER) const;
	// Ship region of the water next to a dock (0 = no water beside it).
	uint16_t dock_sea(const Building &b) const;
	// Centre of the nearest ship-passable cell around a building's footprint, and its
	// ship region; false when none is near (ProductionQueue.js spawns ships there).
	bool nearest_ship_cell(const Building &b, float &out_x, float &out_z, uint16_t *out_sea = nullptr) const;
	void order_trade(const uint32_t *unit_ids, size_t count, uint32_t market_a_id, uint32_t market_b_id,
			bool queued = false);
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
	// 0 A.D. BuildRestrictions.js CheckPlacement for `player` placing `b_type` at cell_x/cell_y:
	// obstruction, terrain (land, shore, land-shore), territory, dock shoreline and distance rules.
	enum PlacementResult : uint8_t {
		PLACE_OK = 0,
		PLACE_OBSTRUCTED, // off the map, on another building or on a resource
		PLACE_TERRAIN,    // water under a land building, deep water under a wall
		PLACE_TERRITORY,  // territory the template does not allow
		PLACE_SHORE,      // a dock not on a shoreline facing a sea
		PLACE_DISTANCE,   // too close to the player's own building of the same class
	};
	PlacementResult check_placement(uint8_t player, int cell_x, int cell_y, uint8_t b_type) const;

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
		const uint8_t *pack_state; // PackState: siege engines pack to move and unpack to fire
		const float *pack_elapsed; // seconds into the current pack/unpack
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
	// Gaia (and any other non-slot owner) gets an empty stock: nothing it earns or spends
	// reaches a real player.
	const PlayerState &player(uint8_t pid) const { return pid < MAX_PLAYERS ? players_[pid] : gaia_stock_; }
	PlayerState &player(uint8_t pid) {
		if (pid < MAX_PLAYERS) return players_[pid];
		gaia_stock_ = PlayerState();
		return gaia_stock_;
	}
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
	int heroes_of(uint8_t player_id) const; // alive or queued (EntityLimits Hero: 1)
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
	const PetraAttackManager &petra_attack_manager() const { return petra_attack_mgr_; }
	int petra_opening_step() const { return petra_opening_step_; }

	// Out-of-range settings fall back to Medium and Balanced.
	static uint8_t petra_clamp_difficulty(uint8_t d) { return d > PETRA_VERY_HARD ? PETRA_MEDIUM : d; }
	static uint8_t petra_clamp_behavior(uint8_t b) { return b > PETRA_BEHAVIOR_AGGRESSIVE ? PETRA_BEHAVIOR_BALANCED : b; }
	// Every AI's difficulty or behaviour (0 A.D. game setup's AI settings).
	void set_petra_difficulty(uint8_t d);
	void set_petra_behavior(uint8_t b);
	// One AI slot's difficulty or behaviour; set them after reset(), which
	// gives every slot the shared setting.
	void set_petra_difficulty(uint8_t pid, uint8_t d);
	void set_petra_behavior(uint8_t pid, uint8_t b);
	// The config of the AI on player `pid`, wherever its slot state is parked.
	const PetraConfig &petra_config_for(uint8_t pid) const;
	// True when Petra drives this player: seated active AI slots, or player 1 when no slot is seated.
	bool petra_runs_for(uint8_t pid) const;
	// InitGame.js "AI Bonus": Petra players gather and trade at the difficulty's
	// rate and build, train and research at its time multiplier; 1 for humans.
	float ai_gather_mult(uint8_t pid) const { return petra_runs_for(pid) ? petra_config_for(pid).gather_rate : 1.0f; }
	float ai_time_mult(uint8_t pid) const { return petra_runs_for(pid) ? petra_config_for(pid).build_time : 1.0f; }
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
	bool is_fauna_id(uint32_t id) const { return fauna_id_to_idx_.count(id) != 0; }

	struct TreasureRelic {
		uint32_t id;
		float x, z;
		uint8_t res_type;
		int amount;
		bool claimed;
		uint8_t owner_team = 255;
	};

	// Treasure.js / TreasureCollector.js: a Gaia treasure any land unit can be ordered to
	// collect; after CollectTime in range its owner gets the treasure's resources.
	struct Treasure {
		uint32_t id;
		float x, z;
		float collect_progress;   // seconds spent collecting by collector_id
		uint32_t collector_id;
		uint8_t type;             // TreasureType (gaia_data.h)
		uint8_t pad[3];
	};
	uint32_t spawn_treasure(float x, float z, uint8_t type);
	void order_collect_treasure(const uint32_t *unit_ids, size_t count, uint32_t treasure_id, bool queued = false);
	const std::vector<Treasure> &treasures() const { return treasures_; }
	bool is_treasure_id(uint32_t id) const {
		for (const auto &t : treasures_) if (t.id == id) return true;
		return false;
	}
	// One plain treasure of a resource (rmgen placePlayersNomad's food_bin / wood / stone / metal);
	// its amount is the kind's, treasure_type_info(treasure_type_for_resource(res)).amount.
	uint32_t spawn_resource_treasure(float x, float z, uint8_t res_type) {
		return spawn_treasure(x, z, treasure_type_for_resource(res_type));
	}
	void clear_treasures() { treasures_.clear(); } // Game Setup "Treasures" off
	// Gaia scenery from a 0 A.D. map (structures/fence_*, column_*, obelisk; gaia_data.h):
	// an oriented obstruction. angle is the map's Orientation y (radians); scale is RTS0
	// units per 0 A.D. metre on this map.
	struct Scenery {
		uint32_t id;
		float x, z;
		float angle;
		float half_w, half_d; // RTS0 units along the rotated x and z axes
		float hp;
		uint8_t type;         // SceneryType (scenery_type_info)
		uint8_t pad[3];
	};
	uint32_t spawn_scenery(float x, float z, uint8_t scenery_type, float angle, float scale);
	// The grain supply of a finished field (0 if none): a Gaia field is farmed through it.
	uint32_t field_resource_id(uint32_t building_id) const {
		auto it = field_res_by_bld_.find(building_id);
		return it != field_res_by_bld_.end() && res_id_to_idx_.count(it->second) ? it->second : 0;
	}
	const std::vector<Scenery> &scenery() const { return scenery_; }
	// A Gaia-owned unit from a 0 A.D. map (Player 0): roster template `template_id`
	// ("gaul/infantry_javelineer_a"; a rank suffix falls back to the _b template), else the
	// u_type class. rank 2/3 = Advanced/Elite, reached through Promotion xp on the next tick.
	uint32_t spawn_gaia_unit(float x, float z, UnitType u_type, const char *template_id, int rank = 1);
	// Puts a unit inside a structure of its owner at once, as a map's <Garrison> does.
	bool garrison_unit_now(uint32_t unit_id, uint32_t building_id);
	// template_gaia_ruins: a stone (or metal) supply gathered 4x as fast as a rock.
	uint32_t spawn_ruins(float x, float z, uint8_t ruins_type);

	float wonder_time_remaining() const { return std::max(0.0f, wonder_duration_ - wonder_victory_timer_); }
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

	// 0 A.D. StatisticsTracker.js, one per player (statistics_tracker.h).
	PlayerStatistics statistics(uint8_t player) const;
	// GetPercentMapExplored / GetTeamPercentMapExplored, 0..100.
	float percent_map_explored(uint8_t player) const;
	float team_percent_map_explored(uint8_t player) const;
	// GetPercentMapControlled / GetTeamPercentMapControlled, 0..100.
	float percent_map_controlled(uint8_t player) const;
	float team_percent_map_controlled(uint8_t player) const;
	// One row every STATISTICS_SAMPLE_INTERVAL seconds of game time (the summary charts).
	const std::vector<StatisticsSample> &statistics_samples() const { return stat_samples_; }
	// The row a sample would hold right now (the summary's final column).
	StatisticsSample statistics_snapshot() const;

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
		float pack_time[UNIT_TYPE_COUNT];   // siege pack/unpack time multiplier
		float regen[UNIT_TYPE_COUNT];       // Health.js RegenRate, hp per second
		float idle_regen[UNIT_TYPE_COUNT];  // Health.js IdleRegenRate, added while idle
		float bld_health[BLD_TYPE_COUNT];
		float bld_attack[BLD_TYPE_COUNT];
		float bld_range[BLD_TYPE_COUNT];
		float bld_build_time[BLD_TYPE_COUNT];
		float bld_research_time[BLD_TYPE_COUNT];
		float bld_pop_bonus[BLD_TYPE_COUNT];
		float bld_arrow_mul[BLD_TYPE_COUNT];
		float bld_arrow_add[BLD_TYPE_COUNT];
		float bld_garrison_heal[BLD_TYPE_COUNT]; // added hp per second for garrisoned units
		float bld_capture[BLD_TYPE_COUNT];        // Capturable/CapturePoints multiplier
		float bld_cost[BLD_TYPE_COUNT][RES_COUNT]; // Cost/Resources multipliers
		// The same unit multipliers per civ roster template, matched against the
		// template's own 0 A.D. classes (Champion Infantry Spearman, Javelineer, ...),
		// which its RTS0 unit_type only approximates.
		struct Template {
			float gather[GK_COUNT];
			float attack, health, speed, range, train_time;
			float armor[3];
			float cost[RES_COUNT];
		} tmpl[CIV_UNIT_TEMPLATE_COUNT];
	};
	void compute_tech_mods(uint8_t player_id, TechMods &out) const;
	void rebuild_all_tech_mods();
	void complete_research(uint8_t player_id, uint8_t tech_id);
	void apply_tech_mods_to_unit(size_t idx);
	const TechMods &tech_mods(uint8_t player_id) const { return tech_mods_[player_id < MAX_PLAYERS ? player_id : 0]; }
	// A unit's technology multipliers: its roster template's when it has one, else its unit_type's.
	struct UnitTechMods { float attack, health, speed, range, train_time; const float *armor, *gather; };
	static UnitTechMods unit_tech_mods(const TechMods &m, uint8_t unit_type, uint16_t tmpl);
	UnitTechMods unit_tech_mods(uint8_t player_id, uint8_t unit_type, uint16_t tmpl) const {
		return unit_tech_mods(tech_mods(player_id), unit_type, tmpl);
	}
	int32_t carry_cap(size_t unit_idx, uint8_t res) const;
	uint32_t find_free_field(uint8_t team, float x, float z);
	void tick_combat(float dt);
	void tick_pack(float dt);
	// Formation.js port (formation.h): controllers walk, members follow their slots.
	void tick_formations(float dt);
	FormationUnitInfo formation_unit_info(size_t unit_idx) const;
	bool unit_idx_can_use_formation(size_t unit_idx, uint8_t formation) const;
	// Commands.js GetFormationUnitAIs: puts the units that can into formation controllers
	// (reusing a whole selected formation) and returns the controllers; `individual` gets the rest.
	std::vector<size_t> formation_controllers_for(const uint32_t *unit_ids, size_t count, uint8_t tmpl,
			bool force_template, std::vector<uint32_t> &individual);
	void formation_remove_units(const uint32_t *unit_ids, size_t count); // Commands.js RemoveFromFormation
	void formation_disband(size_t ci);
	void formation_move_members_into(size_t ci, bool move_center, bool force); // Formation.js MoveMembersIntoFormation
	float formation_walking_distance(const FormationController &fc) const; // UnitAI.js ComputeWalkingDistance
	void formation_stop(const uint32_t *unit_ids, size_t count);
	void cmd_move_individual(const uint32_t *unit_ids, size_t count, float tx, float tz);
	void tick_promotion(float dt);
	void promote_if_due(size_t i); // Promotion.js IncreaseXp: promote while xp covers RequiredXp
	void promote_unit(size_t i, uint16_t target_tmpl, uint8_t target_rank); // Promotion.js Promote
	void apply_rank_bonus(size_t i, uint8_t from_rank, uint8_t to_rank);
	void tick_capture(float dt);
	// 0 A.D. UnitAI per-unit order logic, before movement: answers attacks (fight back or
	// flee), returns fighters to their order, turns patrols, follows the order queue.
	void tick_unit_ai(float dt);
	void tick_status_effects(float dt);
	void tick_projectiles(float dt);
	void tick_construction(float dt);
	void tick_repair(float dt);
	static constexpr float REPAIR_TIME_RATIO = 2.0f; // template_structure.xml Repairable/RepairTimeRatio
	static float foundation_build_multiplier(int builders);
	float builder_rate(size_t unit_idx) const;
	float structure_build_time(const Building &b, uint8_t builder_team) const;
	void tick_production(float dt);
	void tick_research(float dt);
	void tick_upgrades(float dt);
	// Unloads land siege below SIEGE_EJECT_HEALTH or dead (GarrisonHolder EjectHealth / OnDestroy).
	void tick_siege_garrisons();
	// Structures heal their garrison each second and unload it below EjectHealth (GarrisonHolder.js).
	void tick_garrison_holders();
	bool can_board(size_t transport_idx, size_t unit_idx) const;
	void cancel_upgrade(Building &b, uint8_t refund_to);
	void finish_upgrade(Building &b);
	void tick_barter(float dt);
	// v15: per-tick state the main save records leave out (velocities, paths, projectiles, timers...).
	void save_tick_state(std::ofstream &out) const;
	bool load_tick_state(std::ifstream &in, uint32_t version);
	// Player.js GetNextTradingGoods with the default equal split (25% each).
	uint8_t next_trading_goods(uint8_t player);
	// Pays a trade gain into `player`'s stock and its gather statistics.
	void credit_trade(uint8_t player, uint8_t res, int amount);
	void tick_trickle(float dt);
	int corral_count(uint32_t corral_id) const;
	void tick_entity_limits(float dt);
	void tick_death_damage(float dt);
	void tick_gates(float dt);
	void tick_waves(float dt);
	void tick_petra_ai(float dt);
	void tick_petra_ai_player(float dt);
	void tick_fauna(float dt);
	void tick_hunters();                                  // units attacking animals (UnitAI Gather on KillBeforeGather)
	void fauna_attacked(size_t f, uint32_t attacker_id);  // UnitAI "Attacked" for an animal
	void fauna_hit_unit(size_t f, size_t u);              // an animal's melee attack lands on unit u
	bool fauna_can_step(size_t f, float x, float z) const;
	void tick_fow();  // updates FoW each sim tick
	void ensure_fow_cell_heights();
	// The cells one viewer lit, kept while it stands still (CCmpRangeManager likewise redoes a
	// viewer's LOS only when it moves). Derived from the terrain alone: never saved or hashed, and
	// a viewer at another position, or terrain rebuilt since, recomputes. Row k of `rows` holds the
	// lit cells of FoW row cz - r + k, bit i for column cx - r + i.
	struct FowView {
		uint32_t sx = 0, sz = 0, vr = 0; // the bits of the last call's sx, sz and vr
		uint32_t gen = 0; // fow_view_gen_ of that call; 0: no call yet
		bool stored = false; // rows filled for that call
		uint64_t rows[64];
		// Whether a call with these arguments would find this view's last call.
		bool same_spot(float x, float z, float r, uint32_t now) const {
			return gen == now && sx == std::bit_cast<uint32_t>(x) && sz == std::bit_cast<uint32_t>(z) && vr == std::bit_cast<uint32_t>(r);
		}
	};
	static constexpr int FOW_VIEW_MAX_R = 31; // widest view the rows hold: 2r + 1 <= 63
	struct FowViews {
		std::vector<FowView> units, buildings;
	};
	void reveal_fow(std::vector<uint8_t> &grid, float sx, float sz, float vr, FowView *view = nullptr) const;
	template <typename SharesVision>
	void update_player_fow(std::vector<uint8_t> &grid, SharesVision shares_vision, FowViews &views);
	template <typename UnitVision, typename BuildingVision>
	void reveal_viewers(std::vector<uint8_t> &grid, FowViews &views, UnitVision unit_vr, BuildingVision building_vr);
	bool terrain_blocks_los(float sx, float sz, float sy, float tx, float tz, float ty) const;
	void rebuild_nav_grid(); // Phase1: rebuild NavGrid from buildings+water (mirrors CCmpObstructionManager rasterize)
	bool find_path(float sx, float sz, float tx, float tz, WaypointPath &out, uint8_t pass = PASS_INFANTRY) const;
	int territory_owner_at(float x, float z) const; // G2: query territory_grid_
	void rebuild_territory(); // G2: rebuild territory/influence grids from TC/Fortress/Tower
	void rebuild_auras(); // G4: rebuild AuraManager from temples/heroes/carnyx/female citizens
	uint16_t aura_affected_players(uint8_t owner, uint8_t players) const;
	void fire_arrow(float sx, float sy, float sz, float tx, float ty, float tz, float damage, uint8_t team, uint32_t target_id, uint8_t effect = 0, uint8_t dmg_type = 1, bool hits_building = false, uint8_t visual = 0, uint32_t source_id = 0);
	static float projectile_speed(uint8_t visual);
	int32_t projectile_hit_unit(const Projectile &p) const;
	void random_normal_2d(float &x, float &z);
	void mark_building_wreck(Building &b, uint8_t killer = 0xFF); // killer: StatisticsTracker credit
	void record_death(float x, float z, uint8_t team, uint8_t killer_team, uint8_t type, bool is_building);
	void record_unit_death(size_t i, uint8_t killer_team); // record_death for unit i, noting its DeathDamage
	const DeathDamageInfo *unit_death_damage(size_t i) const;
	float unit_template_regen(size_t i) const;
	// StatisticsTracker hooks. `killer` is the player credited with the kill
	// (out of range or the victim's own owner credits nobody).
	void stat_unit_trained(size_t idx);
	void stat_unit_lost(size_t idx, uint8_t killer);
	void stat_building_constructed(const Building &b);
	void stat_building_lost(const Building &b, uint8_t killer);
	void stat_building_captured(const Building &b, uint8_t from);
	void stat_gathered(uint8_t player, uint8_t res, int amount, bool vegetarian);
	void tick_statistics();
	void update_statistics_explored();
	void bind_unit_index(uint32_t id, size_t idx);
	int32_t lookup_unit(uint32_t id) const;
	// 0 A.D. Loot.js / Looter.js / Promotion.js (see simulation.cpp).
	void gain_xp(size_t idx, float amount);
	void award_damage_xp(uint32_t attacker_id, size_t victim, float damage);
	bool looter_present(uint32_t looter_id, bool *infantry) const;
	void collect_loot(uint32_t looter_id, uint8_t looter_player, LootInfo loot, float mult, uint8_t carried_type, int32_t carried);
	void collect_unit_loot(uint32_t looter_id, uint8_t looter_player, size_t victim);
	void collect_building_loot(uint32_t looter_id, uint8_t looter_player, const Building &b);
	void check_victory_conditions(float dt);
	void spawn_regicide_heroes();

	struct FaunaStorage {
		static constexpr size_t MAX_FAUNA = 1024; // dead animals are compacted out each tick
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
		float home_x[MAX_FAUNA] = {}; // UnitAI heldPosition: where a hold-ground animal returns
		float home_z[MAX_FAUNA] = {};
	} fauna_;
	std::unordered_map<uint32_t, size_t> fauna_id_to_idx_;
	std::unordered_map<uint32_t, std::vector<uint32_t>> transport_passengers_;
	// Turret units (unit id -> point height), rebuilt from the holders' garrison lists each
	// tick, and the stance each had before it went up (UnitAI previousStance).
	std::unordered_map<uint32_t, float> turret_height_;
	std::unordered_map<uint32_t, uint8_t> turret_prev_stance_;
	const TurretPointSet *turret_points_of(const Building &b) const;
	int structure_garrison_cap(const Building &b) const;
	void occupy_turret(size_t i, Building &b);
	void leave_turret(size_t i, const Building &b);
	void tick_turrets();
	std::vector<FormationController> formations_; // in creation order
	uint32_t next_formation_id_ = 1;
	// Controller id per unit index, 0 outside formations: members of one formation share an
	// obstruction group and don't push each other in separation(). Derived from formations_
	// by formation_refresh_groups() whenever membership changes, so it is never saved.
	std::vector<uint32_t> formation_group_;
	void formation_refresh_groups();

	UnitSoA units_;
	ResourceSoA resources_;
	std::vector<Building> buildings_;
	std::vector<Projectile> projectiles_;
	std::vector<TreasureRelic> relics_;
	std::vector<Treasure> treasures_;
	void tick_treasures(float dt);
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
	std::unordered_map<uint32_t, FieldGatherers> fish_gatherers_; // per tick, keyed by fish resource id
	std::unordered_map<uint32_t, FieldGatherers> ruins_gatherers_; // per tick, keyed by ruins resource id
	// Each ruins resource's ResourceSupply MaxGatherers (gaia/ruins/*.xml); saved.
	struct RuinsCap { uint32_t res_id; uint16_t max_gatherers; uint16_t pad; };
	std::vector<RuinsCap> ruins_caps_;
	std::vector<Scenery> scenery_; // saved
	int ruins_cap(uint32_t res_id) const {
		for (const RuinsCap &c : ruins_caps_) if (c.res_id == res_id) return c.max_gatherers;
		return 1;
	}
	// Nearest fish on `sea` (ship region) within max_d of (x, z) with a free gatherer slot; 0 if none.
	uint32_t find_free_fish(uint16_t sea, float x, float z, float max_d, uint32_t skip = 0) const;
	std::unordered_map<uint32_t, size_t> building_id_to_idx_;

	struct CellHash {
		size_t operator()(const std::pair<int, int> &p) const {
			return (size_t)p.first * 73856093 ^ (size_t)p.second * 19349663;
		}
	};
	std::unordered_set<std::pair<int, int>, CellHash> occupied_cells_;
	// check_placement's cells (GRID-sized, from -map_half_) that a Gaia resource's
	// obstruction touches. A cache of resources_, rebuilt when the tick or the
	// resource count changes; never saved or hashed.
	mutable std::vector<uint8_t> resource_block_cells_;
	mutable uint64_t resource_block_tick_ = UINT64_MAX;
	mutable size_t resource_block_count_ = SIZE_MAX;
	mutable int resource_block_w_ = 0;

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
	float upkeep_timer_ = 0.0f; // unused since soldier upkeep was removed (no 0 A.D. template has Upkeep); kept for the save layout
	float entity_limits_timer_ = 0.0f;
	float gate_timer_ = 0.0f;
	static constexpr size_t MAX_RECENT_DEATHS = 64;
	std::array<RecentDeath, MAX_RECENT_DEATHS> recent_deaths_{};
	size_t recent_death_head_ = 0;

	// G3 Market / Barter dynamic prices (indexed by ResourceType: 0=WOOD,1=GOLD,2=STONE,3=FOOD)
	// Barter.js priceDifferences: per resource, in percent of the true price, within +-89.
	float barter_price_diff_[RES_COUNT] = {};
	// Player.js tradingGoods: the percent chance of each resource a player's
	// traders load per trip (sums to 100; 25 each by default). Indexed by ResourceType.
	uint8_t trading_goods_[MAX_PLAYERS][RES_COUNT] = {};
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
	bool reveal_map_ = false;
	int biome_override_ = -1; // setup-time only: applied when a random map is generated
	bool explore_map_ = false;
	float wonder_duration_ = 600.0f;
	float relic_duration_ = 180.0f;
	int relic_count_ = 0;
	void apply_relic_count();
	uint8_t water_level_mode_ = WATER_LEVEL_RISING; // saved with the trigger state (v18)
	uint8_t sea_level_rise_min_ = 25;
	TriggerState triggers_;
	void tick_triggers(float dt);
	void register_map_triggers(MapType type);
	void run_trigger_action(uint16_t action, float arg, const TriggerEventData &data);
	// Ported map scripts (simulation.cpp, "Map trigger scripts").
	void trigger_flood_rise_step(float final_level);
	void trigger_volcano_rise_step();
	void trigger_volcano_garrison_towers();
	PlayerState gaia_stock_; // scratch for player(GAIA_PLAYER); never saved or hashed
	// Player.js SetState("defeated"): hand every unit and structure of these players to Gaia.
	void convert_to_gaia(uint8_t players);
	void capture_points_tick(Building &b, size_t bi, float dt); // Capturable.js Reduce, regen and owner change
	uint8_t participant_mask_ = 0; // save v10
	uint8_t defeated_mask_ = 0;
	uint8_t winner_mask_ = 0;
	float wonder_hold_[MAX_PLAYERS] = {}; // seconds each player has held a finished wonder

	MatchRecord record_;
	// StatisticsTracker state, saved through tick_state_io (save v19+).
	std::array<PlayerStatistics, MAX_PLAYERS> stats_{};
	std::vector<StatisticsSample> stat_samples_;
	static constexpr size_t MAX_STATISTICS_SAMPLES = 4096; // 34 hours of 30 s rows
	// Cells each player has seen, STATISTICS_EXPLORE_CELL wide (1 = explored).
	int32_t stats_explore_res_ = 0;
	std::array<std::vector<uint8_t>, MAX_PLAYERS> stats_explored_;

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
	PetraAttackManager petra_attack_mgr_; // attackManager.js plan counters and sizes
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
	// AI-to-AI diplomacy (0 A.D. DiplomacyRequest / TributeRequest events), [to][from], save v27:
	// the stance asked for (0 none, 1 ally, 2 neutral) and a tribute asked for in return.
	uint8_t petra_diplo_request_inbox_[MAX_PLAYERS][MAX_PLAYERS] = {};
	int32_t petra_tribute_request_wanted_[MAX_PLAYERS][MAX_PLAYERS] = {};
	uint8_t petra_tribute_request_res_[MAX_PLAYERS][MAX_PLAYERS] = {};
	PetraSentDiplomacy petra_sent_diplo_;
	void petra_send_diplomacy_request();
	float petra_defense_timer_ = 0.0f;
	float petra_victory_timer_ = 0.0f;
	float petra_diplo_timer_ = 0.0f;
	float ai_wonder_victory_timer_ = 0.0f;
	bool pick_petra_safe_staging(float base_x, float base_z, float target_x, float target_z, float &out_sx, float &out_sz) const;
	uint32_t find_petra_attack_target(float &out_tx, float &out_tz) const;
	uint32_t find_petra_raid_target(float &out_tx, float &out_tz) const;
	uint32_t find_petra_rush_target(float &out_tx, float &out_tz) const;
	void petra_set_rushes();
	void petra_plan_sizes(uint8_t kind);
	bool petra_raid_target_ok(uint32_t bld_id) const;
	bool petra_allow_capture(const std::vector<uint32_t> &squad, uint32_t uid, const Building &b) const;
	void petra_check_captures(const std::vector<uint32_t> &squad, float tx, float tz);
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
	// garrisonManager.js / defenseManager.js garrisons: what is near a holder of ours.
	struct PetraAround { bool defense_structure = false, melee_siege = false, range_siege = false, unit = false; };
	PetraAround petra_around(const Building &holder) const;
	bool petra_keep_garrisoned(size_t unit_idx, const Building &holder, const PetraAround &around) const;
	// incoming[building index]: our units walking to garrison there.
	void petra_garrison_units_inside(size_t holder_idx, bool allow_melee, std::vector<int> &incoming);
	uint32_t petra_garrison_holder_for(size_t unit_idx, bool heals_only, bool siege, std::vector<int> &incoming) const;
	std::vector<int> petra_garrison_incoming() const;
	bool petra_same_land(size_t unit_idx, const Building &b) const; // PETRA.getLandAccess match
	// defenseManager.js garrisonAttackedUnit: the nearest healing structure, or with
	// emergency the nearest holder at all (unloading one unit when it is full).
	bool petra_garrison_attacked_unit(size_t unit_idx, bool emergency, std::vector<int> &incoming);
	size_t petra_attacked_unit_holder(size_t unit_idx, bool emergency, const std::vector<int> &incoming) const; // building index or SIZE_MAX
	// victoryManager.js: regicide heroes and the units guarding them.
	bool petra_is_critical_ent(size_t unit_idx) const;
	bool petra_guards_critical_ent(size_t unit_idx) const;
	void petra_retreat_critical_ent(size_t unit_idx, bool emergency, std::vector<int> &incoming);
	void petra_manage_critical_ent_guards();
	void petra_manage_critical_ent_healers();
	void petra_capture_gaia_relics();
	void petra_manage_garrisons();
	void petra_build_advanced();
	void petra_manage_victory(float dt);
	void petra_manage_diplomacy(float dt);
	void petra_manage_emergency(float dt);
	void petra_manage_transport(float dt);
	void apply_petra_config();
	void petra_refresh_queue_accounts(float dt);
	void petra_staff_foundations();
	void petra_manage_fields(int target_food);
	// worker.js startHunting: food dropsites and each supply's gatherers, gathered once per pass.
	struct PetraHuntScan {
		std::vector<std::pair<float, float>> food_drops;
		std::unordered_map<uint32_t, int> on_animal;  // live animal id -> units hunting it
		std::unordered_map<uint32_t, int> on_carcass; // meat resource id -> units gathering it
		std::vector<uint32_t> treasures_taken;        // treasures one of ours is already on its way to
	};
	PetraHuntScan petra_hunt_scan() const;
	uint32_t petra_find_hunt(size_t u, float px, float pz, const PetraHuntScan &scan, bool &live) const;
	bool petra_start_hunting(size_t u, PetraHuntScan &scan);
	bool petra_gather_treasure(size_t u, PetraHuntScan &scan); // entityExtend.js gatherTreasure
	void petra_manage_hunters(PetraHuntScan &scan);
	void petra_build_first_base(); // startingStrategy.js buildFirstBase
	void petra_resource_demand(float out[RES_COUNT]) const;
	bool petra_perform_barter();      // tradeManager.js performBarter
	void petra_set_trading_goods();   // tradeManager.js setTradingGoods
	int petra_villager_target() const;
	bool petra_can_place(int cell_x, int cell_y, int footprint, uint8_t btype = BLD_TYPE_COUNT) const; // btype: also 0 A.D.'s placement rules
	bool petra_try_pay(uint8_t queue, float food, float wood, float gold, float stone);
	void petra_say(uint8_t kind, float x = 0.0f, float z = 0.0f, uint8_t to = 0xFF, int amount = 0, uint8_t res = 0);
	void petra_handle_diplomacy_request(uint8_t player, uint8_t type);
	bool petra_train_template(uint8_t queue, uint32_t building_id, uint16_t tmpl);
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
	X(petra_fow_) X(petra_fleets_) X(petra_diplo_) X(petra_attack_mgr_) X(petra_config_) \
	X(petra_sent_diplo_)
	struct PetraSlot {
		bool initialized = false;
#define RTSO_PETRA_SLOT_DECL(m) decltype(Simulation::m) m{};
		RTSO_PETRA_SLOT_FIELDS(RTSO_PETRA_SLOT_DECL)
#undef RTSO_PETRA_SLOT_DECL
	};
	std::vector<PetraSlot> petra_slots_; // indexed by player id, sized on first use
	void petra_swap_slot(uint8_t pid);
	// Saves or loads save_tick_state's fields, every AI's Petra state included (save v15); simulation.cpp.
	template <class Ar> friend void tick_state_io(Ar &ar, Simulation &s);
	void petra_reset_ai_state();
	PetraConfig &petra_config_slot(uint8_t pid);
	std::vector<float> fow_cell_height_; // terrain height at each FoW cell centre, lazily filled by tick_fow
	std::vector<float> fow_cell_hmax_; // highest heightmap sample an LOS ray can read in each FoW cell, filled with fow_cell_height_
	static constexpr int FOW_LOS_BLOCK = 4; // FoW cells per side of a fow_block_hmax_ block
	std::vector<float> fow_block_hmax_; // fow_cell_hmax_ max per block, filled with fow_cell_height_
	uint32_t fow_view_gen_ = 0; // bumped each time fow_cell_height_ is filled, so FowViews from older terrain miss
	// reveal_fow() views of units (by index) and buildings (by buildings_ index), for Petra's
	// scouting and for update_player_fow(); their vision radii differ, so each keeps its own.
	FowViews petra_fow_views_, player_fow_views_;
	std::vector<uint32_t> fow_moved_units_; // reveal_viewers() scratch: units revealed last
	NavGrid nav_grid_; // Phase1: SoA nav mesh (buildings + water), mirrors 0AD Grid<NavcellData>
	Grid<uint8_t> territory_grid_; // grid_res_^2 player id per cell, G2 (CCmpTerritoryManager)
	Grid<uint8_t> influence_grid_; // grid_res_^2 influence count per cell, G2
	// CCmpTerritoryManager connectivity, derived from territory_grid_ and the territory roots
	// (civic centres and wonders): each 4-connected patch of one player's territory is a region,
	// connected when it holds one of that player's roots. Unconnected regions keep the count of
	// tiles of connected foreign regions bordering them (GetNeighbours(filterConnected)).
	// Rebuilt on demand after the grid changes; not saved or hashed.
	struct TerritoryRegion {
		uint8_t owner;
		bool connected;
		std::array<uint32_t, MAX_PLAYERS> neighbours;
	};
	Grid<uint16_t> territory_region_;
	std::vector<TerritoryRegion> territory_regions_;
	bool territory_regions_dirty_ = true;
	// Off until build placement keeps structures out of neutral land (see territory_decay_targets).
	static constexpr bool TERRITORY_DECAY_IN_NEUTRAL_LAND = false;
	void ensure_territory_regions();
	// TerritoryDecay.IsConnected + GetConnectedNeighbours for a structure: false when it does not
	// decay; otherwise `to` gets the weight each player receives, and `to_gaia` Gaia's.
	bool territory_decay_targets(const Building &b, std::array<float, MAX_PLAYERS> &to, float &to_gaia);
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
	std::vector<SteerObstacle> sep_obstacles_; // separation()'s list, built the same way
	nav::ObstacleGrid sep_grid_; // sep_obstacles_ by cell, rebuilt with it
	// tick_capture()'s count and strength of nearby capturers per building and team, and the grid of
	// building reach it finds them with. Rebuilt every tick.
	struct CaptureTally { int count[MAX_PLAYERS]; float strength[MAX_PLAYERS]; };
	struct CaptureReach { uint32_t building; float x, z; };
	std::vector<CaptureTally> capture_tally_;
	std::vector<uint32_t> capture_cell_start_;
	std::vector<CaptureReach> capture_cell_buildings_;
	// Capture points each player's capture attacks took from each building this tick
	// (tick_combat fills it, tick_capture spends it). Indexed like buildings_.
	std::vector<std::array<float, MAX_PLAYERS>> capture_attack_;

	// UnitAI orders (0 A.D. UnitAI.js orderQueue). The current order lives in the unit
	// SoA; these are the Shift-queued ones after it, by unit id.
	std::unordered_map<uint32_t, std::vector<UnitOrder>> order_queues_;
	bool applying_queued_ = false; // set while an order from a queue is applied: keep the rest
	UnitOrder current_order(size_t i) const;
	void set_order(size_t i, uint8_t kind, uint8_t flags, uint32_t target, float x, float z);
	void begin_walk_order(size_t i); // cmd_move's walk: not forced, replaces the queue
	void drop_orders(size_t i);      // orders UnitAI doesn't model (trade, hunt, board): no order, no queue
	// Splits a Shift order: busy units get it queued, the rest are returned to run now.
	// Without Shift every unit's queue is dropped and all of them run it now.
	float unit_height(size_t i) const; // terrain, or the water surface under a ship
	bool trade_route_ok(size_t i, uint32_t market_a_id, uint32_t market_b_id) const;
	std::vector<uint32_t> split_queued(const uint32_t *ids, size_t count, const UnitOrder &o, bool queued);
	void apply_order(uint32_t uid, const UnitOrder &o);
	bool order_still_valid(size_t i, const UnitOrder &o) const;
	void finish_order(size_t i);
	void resume_order(size_t i);
	void respond_to_attack(size_t i);
	uint32_t find_nearby_resource(float x, float z, uint8_t res_type, uint8_t team, int subtype = -1) const;
	uint32_t find_nearby_foundation(float x, float z, uint8_t team) const;
	void apply_rally_point(uint32_t uid, const Building &b, bool ignore_self_garrison = false);
	void rally_after_ungarrison(uint32_t uid, uint32_t building_id);
	void on_construction_finished(const Building &b, size_t builder);
	void destroy_building(Building &b);
	bool can_capture_building(uint8_t player, const Building &b) const;
	bool can_heal(size_t healer, int32_t target) const;         // Heal.js CanHeal
	uint32_t find_heal_target(size_t healer) const;             // UnitAI FindNewHealTargets
	void start_heal(size_t healer, uint32_t target, uint8_t flags);
	void tick_heal(size_t healer);                              // UnitAI HEAL.APPROACHING / HEALING
	bool garrison_in_range(size_t i, const Building &b) const;
	void garrison_now(size_t i, Building &b);
	void start_cheer(size_t i);                                 // UnitAI Cheer + notifyToCheerInRange
	void note_kill(uint32_t killer_id, uint32_t victim_id);       // UnitAI shouldCheer

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
