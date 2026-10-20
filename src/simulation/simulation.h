#ifndef RTSO_SIMULATION_H
#define RTSO_SIMULATION_H

#include "../core/memory/arena.h"
#include "../core/memory/soa_storage.h"
#include "grid_nav.h"
#include "id_index.h"
#include "los_grid.h"
#include "nav/footprint.h"
#include "nav/obstacle_grid.h"
#include "aura_manager.h"
#include "cell_set.h"
#include "petra_ai.h"
#include "sim_types.h"
#include "map_biomes_data.h"
#include "statistics_tracker.h"
#include "triggers.h"
#include "cinema.h"
#include "tutorial_data.h"
#include "turret_data.h"
#include "wall_builder.h"
#include <cstdint>
#include <map>
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
	// 0 A.D. Attack.js PerformAttack GetAttackEffectsData at fire time: the shooter's
	// Damage strengths (hack, pierce, crush, fire) after its technologies, rank and
	// attack auras. DelayedDamage.js Hit meets them with the victim's Resistance as it
	// is at impact, times GetAttackBonus against the victim (the shooter's base bonus
	// multipliers; a destroyed shooter applies no tech or aura modification to them).
	float dmg_parts[4] = {};
	uint16_t src_tmpl = CIV_UNIT_TEMPLATE_NONE; // shooter's roster template for its bonuses
	uint8_t src_type = 0; // shooter's UnitType for its bonuses (generic classes when tmpl is NONE)
	uint8_t src_kind = 0; // 0=unknown (older saves), 1=unit, 2=structure (no bonuses, as BuildingAI)
	uint8_t visual = 0; // 0=arrow, 1=bolt (warship projectile prop), 2=stone
	uint8_t status_effect = 0; // bitflags of StatusEffect
	uint8_t team = 0;
	uint32_t target_unit_id = 0;
	uint32_t target_building_id = 0;
	bool alive = true;
	uint8_t splash = 0; // splash_profiles() row of the attack that fired it (0 A.D. DelayedDamage data.splash)
	uint32_t source_id = 0; // firing unit or building: Promotion xp and Looter.js loot go to it
};

// One item of a structure's production queue (0 A.D. ProductionQueue.js Item): a batch of
// `count` units (Trainer.js) or one technology (Researcher.js), worked on front first.
struct ProductionOrder {
	uint8_t unit_type; // UNIT_TYPE_NONE = corral livestock; UNIT_TYPE_TECH for a technology
	float timer;
	float duration;
	uint16_t tmpl = CIV_UNIT_TEMPLATE_NONE; // civ roster unit to spawn (NONE = civ default for unit_type)
	uint8_t count = 1;          // batch size
	uint8_t tech = TECH_NONE;   // the technology this item researches
	bool started = false;       // Trainer.js Item.Start: the batch's population is reserved
	uint8_t petra_plan = 0;     // PetraOrderPlan: what Petra queued the batch for (saved SAVE_VERSION_PETRA_ORDER_PLAN)
	uint8_t batch = 0;          // the batch size queued (ProductionQueue.js originalItem.count) once part of it spawned; 0 = count
	std::array<float, RES_COUNT> paid{}; // refunded when the item is removed (RemoveItem)
	bool is_tech() const { return tech != TECH_NONE; }
};
// The metadata Petra trains a batch with (queueplanTraining.js): a worker (role worker, no
// plan), a soldier for the attack in preparation or for the raid (plan), or none.
// PETRA_ORDER_EMERGENCY: headquarters.js trainEmergencyUnits queues them with metadata plan -1.
enum PetraOrderPlan : uint8_t { PETRA_ORDER_NONE = 0, PETRA_ORDER_WORKER, PETRA_ORDER_ATTACK, PETRA_ORDER_RAID, PETRA_ORDER_EMERGENCY };
// ProductionQueue.js MaxQueueSize.
static constexpr size_t PRODUCTION_QUEUE_MAX = 16;

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
	uint8_t controls_all = 0;              // developer overlay's Control all units (Player.js controlAllUnits)

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
	// Gives back a cancelled payment (Trainer / TechnologyManager RefundResources): the
	// statistics count it as never used. Upgrade.CancelUpgrade uses add(), so a cancelled
	// upgrade stays in Resources used.
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
	static constexpr uint32_t SAVE_VERSION = 109; // save_state's format; see the history beside it
	static constexpr uint32_t SAVE_VERSION_FAUNA_ROAM = 108; // each animal's MoveRandomly roamAngle and startAngle
	static constexpr uint32_t SAVE_VERSION_PETRA_TILE_BASES = 107; // the base that claimed each of Petra's territory tiles
	static constexpr uint32_t SAVE_VERSION_CHASE_TARGET = 105; // the target each unit was last in attack range of (UnitAI CHASING)
	static constexpr uint32_t SAVE_VERSION_PETRA_TARGET_LIST = 109; // each Petra's defenseManager targetList
	static constexpr uint32_t SAVE_VERSION_WAGON_SHARING = 103; // the supply wagons their owners locked
	static constexpr uint32_t SAVE_VERSION_PETRA_TARGETED_RELICS = 104; // each Petra's targetedGaiaRelics; each structure's petra_decay_hold
	static constexpr uint32_t SAVE_VERSION_PETRA_OUT_OF_PLAN = 106; // Petra's units out of a plan (metadata plan -1); emergency batches
	static constexpr uint32_t SAVE_VERSION_AUTOQUEUE_BATCH = 102; // each queued batch's original size once part of it spawned
	static constexpr uint32_t SAVE_VERSION_CARCASS_OWNER = 101; // each carcass's animal's owner
	static constexpr uint32_t SAVE_VERSION_PETRA_PROSPECT = 100; // Petra's routeProspection and the Trade structures it last saw
	static constexpr uint32_t SAVE_VERSION_TRADE_ROUTE = 99; // each trader's route waypoints and leg progress (SetupTradeRoute route, reversed on the way back)
	static constexpr uint32_t SAVE_VERSION_PROJECTILE_DAMAGE = 98; // each projectile's fire-time strengths and shooter (GetTotalAttackEffects at impact); each unit's and animal's previous-turn position (aim velocity); each player's control-all-units flag
	static constexpr uint32_t SAVE_VERSION_RALLY_COMMANDS = 97; // each rally position's command and unit or treasure
	static constexpr uint32_t SAVE_VERSION_PETRA_RAID_SIEGE = 95; // the Petra raid's name and siege class
	static constexpr uint32_t SAVE_VERSION_PETRA_SUPPLY_ROOM = 94; // each Petra gatherer's supply; structures with no room, for 90 s; the AIs' own random stream
	static constexpr uint32_t SAVE_VERSION_PETRA_STUCK = 93; // each Petra attack plan's position 5 turns ago (siege stuck check)
	static constexpr uint32_t SAVE_VERSION_GUARD_OF = 92; // each unit's isGuardOf, kept through other orders
	static constexpr uint32_t SAVE_VERSION_WORK_ORDERS = 96; // each unit's workOrders queue (Back to Work)
	static constexpr uint32_t SAVE_VERSION_PETRA_MOVE_TO_GATHERER = 91; // when each Petra mobile dropsite next looks for a gatherer
	static constexpr uint32_t SAVE_VERSION_DECAY_DISABLED = 89; // each structure's map-set no-decay flag
	static constexpr uint32_t SAVE_VERSION_PETRA_KEPT = 88; // Petra's needFish/needFarm, wanted fishing and transport ships, targetNumTraders
	static constexpr uint32_t SAVE_VERSION_PETRA_NO_UNITS = 90; // Petra's canBuildUnits and the units it sent to allies (dispatchUnits)
	static constexpr uint32_t SAVE_VERSION_PETRA_RALLY = 87; // each Petra plan's completing step, march waypoints and last position
	static constexpr uint32_t SAVE_VERSION_CINEMA = 82; // CinemaManager's paths, queue and timers
	static constexpr uint32_t SAVE_VERSION_TURRET_UNITS = 85; // the rampart tower's turret units beside its garrison
	static constexpr uint32_t SAVE_VERSION_ATTACK_DETECTION = 86; // each player's suppressed attacks (OnAttackDetected)
	static constexpr uint32_t SAVE_VERSION_STATUS_STACKS = 80; // each Poisoned/Burning hit its own status, on units, structures and animals
	static constexpr uint32_t SAVE_VERSION_PETRA_PLAN_NAMES = 83; // Petra's plan counter, the main plan's siege class, the raid's sizes
	static constexpr uint32_t SAVE_VERSION_SEEN_MASK = 84; // which players have seen each structure (Fogging seen)
	static constexpr uint32_t SAVE_VERSION_HEADING = 77; // each unit's heading (Position rotation, turned at TurnRate)
	static constexpr uint32_t SAVE_VERSION_PETRA_INACCESSIBLE = 81; // Petra workers' approach watch and supplies flagged inaccessible
	static constexpr uint32_t SAVE_VERSION_PETRA_HUNT_SEARCH = 79; // each Petra hunter's last fruitless hunt search; storehouse checks per base
	static constexpr uint32_t SAVE_VERSION_PETRA_FAILED_GATHER = 75; // Petra's lastFailedGather; PetraConfig drops three unused fields
	static constexpr uint32_t SAVE_VERSION_PETRA_TURNS = 73; // Petra's events kept between its 1.6 s turns
	static constexpr uint32_t SAVE_VERSION_SCENERY_CAPTURE = 69; // each scenery entity's owner and capture points
	static constexpr uint32_t SAVE_VERSION_HUNT_SPECIES = 70; // each carcass's and hunter's animal template
	static constexpr uint32_t SAVE_VERSION_TRIGGER_RANGE = 72; // OnRange queries and their collections
	static constexpr uint32_t SAVE_VERSION_WONDER_TIMERS = 78; // each wonder's own countdown, owner and allies
	static constexpr uint32_t SAVE_VERSION_FAUNA_OWNER = 61; // each animal's owner (fauna_owner_)
	static constexpr uint32_t SAVE_VERSION_TEMPLATE_CIV = 62; // each building's template civ
	static constexpr uint32_t SAVE_VERSION_DROPSITE_SHARING = 51; // each building's dropsite_shared
	static constexpr uint32_t SAVE_VERSION_RELIC_CP = 53; // each relic's capture points per player
	static constexpr uint32_t SAVE_VERSION_TRADER_GOODS = 59; // each trader's goods amounts
	static constexpr uint32_t SAVE_VERSION_DISABLED_BUILDINGS_128 = 63; // triggers' disabled_buildings as 128-bit BldMasks
	static constexpr uint32_t SAVE_VERSION_STATISTICS = 30; // per-player StatisticsTracker in tick_state_io
	static constexpr uint32_t SAVE_VERSION_STATISTICS_COUNTS = 65; // resource and population counts, livestock, bribes
	static constexpr uint32_t SAVE_VERSION_UNITS_CAPTURED = 71; // StatisticsTracker unitsCaptured and its value
	static constexpr size_t MAX_PROJECTILES = 384;

	// Spatial grids (nav, fog of war, territory) keep a fixed 2m cell, so their
	// resolution grows with the map: 64 cells on the default 128m map, 256 on a
	// 512m 0 A.D. "Giant" map, 344 on 0.28's largest map, Corinthian Isthmus (688 tiles).
	// grid_res_for_map() gives the resolution.
	static constexpr float SPATIAL_CELL = 2.0f;
	static constexpr int MIN_GRID_RES = 64;
	static constexpr int MAX_GRID_RES = 344;
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
		// So does a multiplayer observer, which has no player (0xFF).
		if (player == 0xFF) return revealed_fow_.data();
		// Player.js: the winners' map is revealed too (SetLosRevealAll).
		if (player < MAX_PLAYERS && is_victory_for(player) && !revealed_fow_.empty()) return revealed_fow_.data();
		if (player == 0 || player >= MAX_PLAYERS) return fow_grid_.data();
		if (player_fow_[player].size() == fow_grid_.size()) return player_fow_[player].data();
		return unexplored_fow_.data();
	}
	int fow_res() const { return grid_res_; }
	// Which non-zero player's fog tick_fow also computes (the local client's
	// player in multiplayer). Presentation-only: not hashed, not used by the sim.
	void set_fow_viewer(uint8_t player) { fow_viewer_ = player; }
	uint8_t fow_state_at(float x, float z, uint8_t viewer_team = 0) const;
	// Per-player line of sight (0 A.D. CCmpRangeManager LOS; see los_grid.h),
	// hashed and saved. Player p sees what its own sources and those of the
	// players it shares vision with (VisionSharing.js, los_shared_mask) see;
	// "Revealed map" shows everything and "Explored map" explores everything.
	uint8_t los_shared_mask(uint8_t player) const;
	// Player.js UpdateSharedLos: a viewer sees through its mutual allies once it has researched
	// Cartography (unlock_shared_los); its own entities always.
	bool shares_los(uint8_t viewer, uint8_t owner) const;
	bool los_visible(uint8_t player, float x, float z) const;
	bool los_explored(uint8_t player, float x, float z) const;
	// UnitAI CheckTargetVisible (UnitAI.js:4922-4941) for a player's command: an entity the
	// player can't see and has no mirage of can't be targeted. Own and allied ones always can;
	// a unit or animal only while in sight, a structure once seen (Fogging), a supply where the
	// land is explored. True when the game keeps no line of sight.
	bool player_can_target_unit(uint8_t player, uint32_t unit_id) const;
	bool player_can_target_building(uint8_t player, uint32_t building_id) const;
	bool player_can_target_resource(uint8_t player, uint32_t resource_id) const;
	bool player_can_target_fauna(uint8_t player, uint32_t fauna_id) const;
	const LosGrid &los() const { return los_; }
	uint32_t los_hash() const { return los_.hash(); }
	bool is_entity_visible_to_viewer(float x, float z, uint8_t entity_team, uint8_t viewer_team = 0) const;

	struct Building {
		uint32_t id;
		float x, z;
		float half_size; // half the footprint's width, along its u axis
		float half_d = 0.0f; // half its depth, along v (building_half_depth); set both with set_extents
		uint8_t type; // BLD_*
		uint8_t team; // 0=player, 1=enemy
		// The civ whose template it was placed as (its builder's; CIV_COUNT for Gaia's). 0 A.D. keeps an
		// entity's template through captures, so GarrisonHolder Max and List follow this, not the owner.
		uint8_t civ = CIV_COUNT;
		float hp;
		float max_hp;
		float aura_hp_mult = 1.0f; // Health/Max auras applied to max_hp and hp (Antiochus IV, the Persian catafalque)
		float build_progress; // 0.0f to 1.0f (1.0f = finished)
		bool committed = false; // 0 A.D. Foundation.committed: first builder reached the plot
		bool wrecked = false;   // hp already applied; keep entity for collapse + rubble
		int cell_x, cell_y;
		int footprint; // 1 or 2
		float rot = DEFAULT_BUILDING_ROT; // 0 A.D. Position rotation: 0 faces +z, pi/2 faces +x; set with set_rot
		// The footprint's u axis and half extents along u and v (nav/footprint.h), turned with rot. A
		// quarter turn swaps width and depth.
		float ux = 1.0f, uz = 0.0f, hu = 0.0f, hv = 0.0f;
		void set_rot(float a) {
			rot = a;
			bool swapped;
			nav::footprint_axis(a, ux, uz, swapped);
			hu = swapped ? half_d : half_size;
			hv = swapped ? half_size : half_d;
		}
		void set_extents(float half_w, float half_depth) {
			half_size = half_w;
			half_d = half_depth;
			set_rot(rot);
		}
		// 0 A.D. BuildingAI.js FireArrows: attack_timer counts down to the next of the ten
		// rounds per RepeatTime; the timer runs only while enemies are in range.
		float attack_timer;
		uint8_t arrow_round = 0;      // currentRound
		int16_t arrows_left = 0;      // arrowsLeft of this RepeatTime
		bool arrow_timer_on = false;  // a FireArrows timer is set
		// focusTargets (0 A.D. "focus-fire", AddFocusTarget): entity ids its arrows go to first,
		// in order; empty: FireArrows picks its own. Saved in save_tick_state (SAVE_VERSION_FOCUS_FIRE).
		std::vector<uint32_t> focus_targets;
		bool is_burning = false; // Structure fire under 33% HP
		// WonderVictory.js wonderVictoryMessages[ent]: this wonder's own countdown, the player it
		// runs for (255: none) and that player's mutual allies when it started (allied victory).
		float wonder_hold = 0.0f;
		uint8_t wonder_owner = 255;
		uint8_t wonder_allies = 0;
		// Fogging.js seen[player]: the players who have seen this structure (in their own or shared
		// sight, or in land explored at the start), so their mirage of it outlives a load or rejoin.
		uint8_t seen_mask = 0;

		// Multi-slot production queue
		std::vector<ProductionOrder> queue;

		// Rally point
		bool has_rally_point;
		float rally_x, rally_z;
		uint32_t rally_target_res_id;
		uint32_t rally_target_bld_id;
		// 0 A.D. RallyPoint keeps the resource type (generic + specific) of a gather
		// target, so a trained unit gathers near the spot once the target is gone
		// (gather-near-position). 0xFF: no resource target.
		uint8_t rally_res_type = 0xFF, rally_res_sub = 0xFF;
		// RallyPoint data.command where the target alone doesn't say it (RallyCommand), and the
		// unit or treasure it names (attack, collect-treasure).
		uint8_t rally_cmd = 0;
		uint32_t rally_target_ent_id = 0;
		// Shift-added positions after the first (RallyPoint.AddPosition), in order.
		struct RallyStep {
			float x, z;
			uint32_t res_id, bld_id;
			uint8_t res_type, res_sub;
			uint8_t cmd = 0;
			uint32_t ent_id = 0;
		};
		std::vector<RallyStep> rally_more;

		// Garrison
		std::vector<uint32_t> garrisoned_units;
		// The units on the turret points of a holder that also has a garrison (the Mauryan
		// rampart tower); a wall, gate or outpost keeps its turret units in garrisoned_units.
		std::vector<uint32_t> turret_units;
		int garrison_cap;

		// ProductionQueue.js autoqueuing: a finished batch is queued again.
		bool autoqueue = false;

		// Structure Capture (Capturable.js): per-player capture points.
		// The scalar capture_points field remains the amount currently held
		// by the building owner for compatibility with presentation code.
		float capture_points;
		std::array<float, 8> capture_points_by_team{};
		float max_capture_points;
		float capture_regen_rate;
		float capture_garrison_regen_rate;
		float territory_decay_rate;
		// A map piece whose template has <TerritoryDecay disable=""/> (palisades_spike_*, the
		// Roman siege walls): territory_decay_rate stays 0 whatever its type's.
		bool decay_disabled = false;

		// 0 A.D. Upgrade.js: the structure this one is turning into
		// (BLD_TYPE_COUNT = not upgrading) and the resources paid for it.
		uint8_t upgrade_to = BLD_TYPE_COUNT;
		float upgrade_timer = 0.0f;
		float upgrade_duration = 0.0f;
		std::array<float, RES_COUNT> upgrade_paid{};

		// 0 A.D. Gate.js (gates: is_gate_type). An unlocked gate never blocks long-range
		// pathfinding and blocks movement only while closed; a locked gate stays
		// closed and blocks both.
		bool gate_open = false;
		bool gate_locked = false;
		// 0 A.D. ResourceDropsite IsShared: allies with Diaspora may drop resources here. Sharable
		// dropsites (Civic Centres, storehouses, farmsteads, docks) start shared.
		bool dropsite_shared = true;
		// garrisonManager.js addDecayingStructure(justCaptured): a Barracks-class structure
		// captured while decaying, which its owner's AI garrisons until the decay stops.
		bool petra_decay_hold = false;
	};
	// Whether `b` is rasterized into the nav grid (0 A.D. Obstruction BlockPathfinding).
	static bool blocks_pathfinding(const Building &b) {
		return building_blocks_movement(b.type) && (!is_gate_type(b.type) || b.gate_locked);
	}
	// Whether units collide with `b` right now (0 A.D. Obstruction BlockMovement).
	// A foundation blocks nothing until it is committed (Foundation.js Commit).
	static bool blocks_movement_now(const Building &b) {
		return building_blocks_movement(b.type) && !(is_gate_type(b.type) && b.gate_open) &&
				(b.committed || b.build_progress >= 0.99f);
	}

	Simulation();
	~Simulation();

	void reset();
	void set_sim_seed(uint32_t seed) {
		sim_seed_ = seed ? seed : 1337;
		sim_rng_.set_seed(sim_seed_);
		petra_rng_.set_seed(petra_rng_seed());
		reroll_petra_personalities();
	}
	uint32_t sim_seed() const { return sim_seed_; }
	// 0 A.D. -autostart-aiseed: the AIs' personality seed, 0 = the match seed. Not saved: the
	// drawn personalities are (PetraConfig).
	void set_ai_seed(uint32_t seed) { ai_seed_ = seed; petra_rng_.set_seed(petra_rng_seed()); reroll_petra_personalities(); }
	uint32_t ai_seed() const { return ai_seed_; }
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
	// Walk speed of the unit at SoA index i: its speed with technologies times any speed aura it stands in.
	float unit_walk_speed(size_t i) const;
	// The same before running (a fleeing, chasing or slot-walking unit runs at RunMultiplier times it).
	float unit_base_walk_speed(size_t i) const;
	// The speed it moves at now: its walk speed, slowed by the pushing pressure it is under.
	float unit_move_speed(size_t i) const;
	// How far it moves this tick heading (ux, uz) at up to max_speed, starting from velocity (vx, vz),
	// with its template's acceleration, turn rate and turn rule; turns its heading (heading_x/z) and
	// sets *speed to its speed at the end of the tick.
	float accelerated_step(size_t i, float vx, float vz, float ux, float uz, float max_speed, float dt,
			float *speed);
	// How far it goes in time t from speed s along its heading, gaining speed at its template's
	// acceleration up to max_speed; *speed is its speed at the end.
	float accelerate(size_t i, float s, float max_speed, float t, float *speed) const;
	// Trainer.js SpawnUnits and Garrisonable.UnLoad: a unit put out of a structure at (x, z) faces away
	// from it.
	void face_away_from(size_t i, float x, float z);
	float unit_run_multiplier(size_t i) const;
	bool unit_running(size_t i) const;
	// 0 A.D. Identity classes, RestrictedClasses, Bonuses and Spread of the unit at SoA index i.
	const CombatProfile &unit_combat_at(size_t i) const { return unit_combat(units_.tmpl[i], units_.unit_type[i]); }
	// The passability class Clearance of the unit at SoA index i from buildings (unit_building_clearance).
	float unit_clearance_at(size_t i) const { return unit_building_clearance(units_.tmpl[i], units_.unit_type[i]); }
	// The long-path class of land unit i: PASS_CAVALRY when mounted, else PASS_INFANTRY, plus
	// PASS_LARGE for 0 A.D.'s "large" class (unit_is_large).
	uint8_t unit_land_pass(size_t i) const {
		const uint8_t t = units_.unit_type[i];
		const bool mounted = t == UNIT_CAVALRY || t == UNIT_CHARIOT || t == UNIT_CAMEL || t == UNIT_SCYTHED_CHARIOT;
		return (mounted ? PASS_CAVALRY : PASS_INFANTRY) | (unit_is_large(units_.tmpl[i], t) ? PASS_LARGE : 0);
	}
	// Attack MaxRange of the unit at SoA index i with technologies (the developer overlay's range overlay).
	float unit_attack_range_at(size_t i) const { return i < units_.count ? units_.attack_range[i] : 0.0f; }
	// The waypoints of its long path it has still to reach, *xs / *zs at the next one (the unit
	// motion overlay); 0 when it has none.
	int unit_waypoints_left(size_t i, const float **xs, const float **zs) const {
		if (i >= units_.count || i >= unit_paths_.size()) return 0;
		const int cur = units_.waypoint_cur[i], n = units_.waypoint_count[i];
		if (cur < 0 || n > WaypointPath::MAX_WP || cur >= n) return 0;
		*xs = unit_paths_[i].xs + cur;
		*zs = unit_paths_[i].zs + cur;
		return n - cur;
	}
	// The next waypoint of the short path unit i walks round something on toward (aim_x, aim_z)
	// (UnitMotion m_ShortPath, the unit motion overlay): what its ShortPathMemo keeps for that aim.
	bool unit_short_waypoint(size_t i, float aim_x, float aim_z, float *wx, float *wz) const;
	// Unit i's range queries (CCmpRangeManager's m_Queries, the range overlay): UnitAI's
	// LosRangeQuery over GetQueryRange(IID_Attack) by stance, enabled while the unit is idle,
	// attack-walking, patrolling or guarding, and a healer's LosHealRangeQuery
	// (GetQueryRange(IID_Heal)). max 0: no query.
	struct RangeQuery {
		float min = 0.0f, max = 0.0f; // the attack query
		float heal_max = 0.0f;        // the heal query (healers)
		bool enabled = false;
	};
	RangeQuery unit_range_query(size_t i) const;
	// Cells a side of the grid the range queries look units up in (CCmpRangeManager's
	// m_Subdivision), spread over the whole map.
	static int unit_grid_res();
	// Attack.js CanAttack: may unit i attack unit j / building b at all (RestrictedClasses)?
	// A relic (catafalque) has no Health, so no attack but Capture reaches it.
	bool unit_can_target(size_t i, size_t j) const {
		if (units_.unit_type[j] == UNIT_CATAFALQUE || !combat_can_target(unit_combat_at(i), unit_combat_at(j))) return false;
		// Attack.js CanAttack: no attack whose MaxRange is less than the height offset between
		// the two (an archer on a wall's turret point is out of a swordsman's reach).
		return turret_height_.empty() || std::fabs(unit_height_offset(i) - unit_height_offset(j)) <= units_.attack_range[i];
	}
	// Capturable.js CanCapture / Attack.js Capture: may unit i capture relic unit r?
	bool can_capture_relic(size_t i, int32_t r) const;
	bool unit_can_target_building(size_t i, uint8_t building_type) const {
		return structure_has_health(building_type) && combat_can_target(unit_combat_at(i), building_combat(building_type));
	}
	uint16_t unit_template(uint32_t unit_id) const;
	// 0 A.D. Promotion.js GetRequiredXp for unit index i after the owner's technologies,
	// or -1 when the unit has no next rank.
	float promotion_required_xp(size_t i) const;
	// Promotion.js IncreaseXp's loop: the skipped ranks' RequiredXp comes from the promoted
	// template (technologies and global auras), without the range auras at the unit.
	float promotion_required_xp_template(uint8_t team, uint16_t tmpl, uint8_t type, uint8_t rank) const;
	// 0 A.D. Identity Rank: only units whose template has a rank (citizen infantry,
	// cavalry, elephants, healers; the ones with Promotion) show the Basic rank icon.
	bool unit_has_rank(size_t i) const;
	void debug_add_xp(uint32_t unit_id, float xp) { if (const int32_t i = lookup_unit(unit_id); i >= 0) gain_xp((size_t)i, xp); } // tests
	// 0 A.D. TurretHolder / Turretable: ranged infantry stand on the turret points of
	// walls, gates and outposts, visible and fighting from the structure's height.
	bool unit_turretable(size_t i) const;
	uint32_t turret_holder_of(uint32_t unit_id) const;
	// The turret points of `b`, whether it holds only turrets or a garrison too (null: none).
	const TurretPointSet *turret_holder_points(const Building &b) const;
	// Its turret units: garrisoned_units for a turret-only holder, else turret_units.
	const std::vector<uint32_t> &turret_list(const Building &b) const {
		return turret_points_of(b) ? b.garrisoned_units : b.turret_units;
	}
	// A wall, gate or outpost: a TurretHolder with no GarrisonHolder in 0.28, whose turret
	// units RTS0 keeps as its garrison.
	bool turret_only_holder(const Building &b) const { return turret_points_of(b) != nullptr; }
	// unit_actions.js "occupy-turret": the unit may stand on a turret point of this own or
	// mutually allied holder (Turretable, TurretHolder.CanOccupy). Room is not checked.
	bool can_occupy_turret_in(uint32_t unit_id, uint32_t building_id) const;
	// Commands.js "leave-turret": `player`'s units among `unit_ids` step down from their turret points.
	void cmd_leave_turret(uint8_t player, const uint32_t *unit_ids, size_t count);
	// Commands.js "unload-turrets": everyone `player` may unload steps down from the holder's turret points.
	void cmd_unload_turrets(uint8_t player, uint32_t building_id);
	// Total damage `player`'s entities have dealt to other (non-Gaia) players this session
	// (0 A.D. BattleDetection input); not saved, starts again at 0 after a load.
	// One hit that cost an entity health: 0 A.D.'s MT_Attacked message, read by the
	// session's AttackDetection (under-attack alerts). Presentation only: never saved,
	// hashed or read by the simulation; at most kMaxAttackedEvents wait between reads
	// so a headless sim without a reader stays bounded.
	struct AttackedEvent {
		uint32_t attacker_id; // unit, structure or animal; 0 for none (DeathDamage)
		uint32_t target_id;
		uint8_t attacker_owner;
		uint8_t target_owner;
		uint8_t target_is_building;
		float x, z; // the target's position
		float damage;
		uint8_t melee; // a melee hit (the attacker's attack_melee sound plays on it)
	};
	static constexpr size_t kMaxAttackedEvents = 4096;
	// Hands over the hits since the last call.
	std::vector<AttackedEvent> take_attacked_events() {
		std::vector<AttackedEvent> out;
		out.swap(attacked_events_);
		return out;
	}
	// An AI's order: in 0.28 Petra posts commands like a player, so Commands.js sends a
	// "playercommand" notification for each and the session's Follow Player follows them.
	// Presentation only like AttackedEvent, and recorded only while record_ai_commands is
	// on; an order another order gives on its way (attack-walk walking) is not recorded.
	struct AiCommandEvent {
		enum Kind : uint8_t { WALK, ATTACK_WALK, PATROL, GATHER, ATTACK, REPAIR, GARRISON, GUARD, HEAL, RETURN_RESOURCE,
			TRADE, STOP, DELETE, TRAIN, RESEARCH, OTHER };
		uint8_t player;
		uint8_t kind;
		std::vector<uint32_t> ids; // cmd.entities: the units ordered, or the trainer
		uint32_t target;           // cmd.target, 0 for none
		float x, z;                // cmd.x, cmd.z for a position order
	};
	static constexpr size_t kMaxAiCommandEvents = 1024;
	void set_record_ai_commands(bool on) {
		record_ai_commands_ = on;
		if (!on) ai_command_events_.clear();
	}
	std::vector<AiCommandEvent> take_ai_command_events() {
		std::vector<AiCommandEvent> out;
		out.swap(ai_command_events_);
		return out;
	}
	// At the top of an order an AI may give: notes it once, for the outermost order only.
	struct AiCommandScope {
		Simulation &sim;
		AiCommandScope(Simulation &s, uint8_t kind, const uint32_t *ids, size_t n, uint32_t target = 0, float x = 0.0f,
				float z = 0.0f) : sim(s) {
			if (sim.ai_command_depth_++ == 0 && sim.record_ai_commands_ && sim.petra_in_turn_) sim.note_ai_command(kind, ids, n, target, x, z);
		}
		~AiCommandScope() { --sim.ai_command_depth_; }
	};
	void note_ai_command(uint8_t kind, const uint32_t *ids, size_t n, uint32_t target, float x, float z) {
		if (n == 0 || ai_command_events_.size() >= kMaxAiCommandEvents) return;
		ai_command_events_.push_back({petra_pid_, kind, std::vector<uint32_t>(ids, ids + n), target, x, z});
	}
	// A unit's melee or Slaughter strike on an animal: presentation only like
	// AttackedEvent (the striker's attack_melee or attack_slaughter sound plays on it).
	struct FaunaStrikeEvent {
		uint32_t attacker_id;
		uint8_t slaughter;
	};
	std::vector<FaunaStrikeEvent> take_fauna_strikes() {
		std::vector<FaunaStrikeEvent> out;
		out.swap(fauna_strikes_);
		return out;
	}
	// 0 A.D. session chat notifications (gui/session/messages.js "tribute",
	// "barter" and "phase"): presentation only like AttackedEvent, never saved,
	// hashed or read by the simulation, at most kMaxSessionNotices between reads.
	struct SessionNotice {
		enum Kind : uint8_t { TRIBUTE = 0, BARTER = 1, PHASE_STARTED = 2, PHASE_ABORTED = 3, PHASE_COMPLETED = 4, DIPLOMACY = 5,
			DEFEATED = 6,   // player is out; tech: the EndReason
			WON = 7,        // other: the winners' mask; tech: the EndReason
			LOST = 8,       // other: the players a win defeated (wonder, relic, scenario); tech: the EndReason
			TEXT = 9,       // GuiInterface PushNotification "text": tech: the TextMessage; amounts[0]: the entity
			CHEAT = 10 };   // Commands.js "aichat" from `player`: tech 0 "(Cheat - reveal map)", 1 "(Cheat - promoted units)", 2 "(Cheat - control all units)"
		// The "text" time notifications (10 s, centre top) the sim pushes for `player`.
		enum TextMessage : uint8_t {
			TEXT_QUEUE_FULL = 0,       // ProductionQueue.js AddItem: "The production queue is full."
			TEXT_UPGRADING,            // AddItem: "Entity is being upgraded. Cannot start production."
			TEXT_AUTOQUEUE_OFF,        // ProductionQueue.js: "Could not auto-queue unit, de-activating."
			TEXT_SPAWN_BLOCKED,        // Trainer.js: "Can't find free space to spawn trained units."
			TEXT_PRODUCING,            // Upgrade.js: "Entity is producing. Cannot start upgrading."
			TEXT_UNLOAD_FAILED,        // Commands.js notifyUnloadFailure: "Unable to unload unit(s)."
			TEXT_BACK_TO_WORK_FAILED,  // notifyBackToWorkFailure: "Some unit(s) can't go back to work"
			TEXT_NO_BRIBABLE,          // Commands.js spy-request: "There are no bribable units"
			TEXT_INSUFFICIENT_RESOURCES, // Player.js SubtractResourcesOrNotify: amounts[] per resource missing
		};
		// The reason in 0 A.D.'s playerstate chat line ("... has been defeated (lost hero).").
		enum EndReason : uint8_t { END_CONQUEST = 0, END_UNITS, END_STRUCTURES, END_CIVIC_CENTRES, END_HERO, END_RESIGNED,
			END_OBJECTIVE, END_LAST_ALIVE, END_WONDER, END_RELIC, END_GAME_MODE, END_LOST_CIVIC_CENTER,
			END_TREASURE, END_STRUCTURES_CRITICAL, END_CIVIC_CENTRES_CRITICAL, END_CHEAT };
		uint8_t kind;
		uint8_t player; // who tributed, bartered, advances or changed its stance
		uint8_t other;  // TRIBUTE: the recipient; BARTER: the resource sold; DIPLOMACY: the target
		uint8_t tech;   // PHASE_*: the phase technology; BARTER: the resource bought; DIPLOMACY: the stance
		int32_t amounts[RES_COUNT]; // TRIBUTE: per resource; BARTER: [0] sold, [1] bought
	};
	static constexpr size_t kMaxSessionNotices = 256;
	std::vector<SessionNotice> take_session_notices() {
		std::vector<SessionNotice> out;
		out.swap(session_notices_);
		return out;
	}
	double battle_damage_dealt(uint8_t player) const { return player < MAX_PLAYERS ? battle_damage_[player] : 0.0; } // 0 when not on a turret
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
	bool loot_denied_unit(size_t i) const;
	bool loot_denied_building(const Building &b) const;
	float unit_loot_xp(size_t victim) const;
	// Read-only, for the renderer: unit i is attacking a structure it captures
	// rather than damages (the tick_combat check), so it plays 0 A.D.'s capture clip.
	bool unit_capturing(size_t i) const;
	bool unit_heals(size_t i) const;
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
	// rounded: the whole per-unit cost (tooltips, Cost.GetResourceCosts); Trainer.js keeps it
	// unrounded and floors count * cost once (rounded = false).
	void unit_train_cost(uint8_t pid, uint16_t tmpl, const UnitTypeInfo &info, float out[RES_COUNT], bool rounded = true) const;
	// Trainer.js GetUpgradedTemplate: the template a queued `tmpl` is trained as, following its
	// Promotion while the player's RequiredXp for the step is 0 (Marian Reforms' Legionaries,
	// mercenaries trained Advanced), and the rank it ends on. Cost and time are that template's.
	uint16_t trained_as(uint8_t pid, uint16_t tmpl, uint8_t &rank) const;
	// One unit's training time before the batch multiplier: the trained-as template's
	// Cost/BuildTime with team bonuses, technologies, AI difficulty and x1.2 per rank above Basic.
	float unit_train_seconds(uint8_t pid, uint16_t tmpl) const;
	uint32_t spawn_resource(int cell_x, int cell_y, uint8_t type, int32_t amount = 600);
	// max_gatherers 0: the gaia template default (resource_default_max_gatherers).
	// flora: the FloraKind the supply shows (FLORA_DEFAULT: the biome's pick).
	uint32_t spawn_resource_world(float wx, float wz, uint8_t type, int32_t amount = 600, uint8_t subtype = 0, uint8_t max_gatherers = 0,
			uint8_t flora = 0);
	void set_resource_flora(uint32_t res_id, uint8_t flora);
	// Map generation only: moves a resource before the nav grid is built.
	void set_resource_position(uint32_t res_id, float x, float z);
	// Map generation only: one tree per terrain tile, at the tile's centre (0.28's rmgen paints
	// forests tile by tile). Returns how many trees had no free tile beside theirs and went.
	size_t snap_trees_to_tiles();
	// Map generation only: moves an animal (and the spot it roams round).
	void set_fauna_position(uint32_t fauna_id, float x, float z);
	// Sets a resource's amount, as map generation does for a tree whose 0 A.D. template holds more or
	// less than TREE_WOOD_SUPPLY. A regrowing supply (fruit) moves its Max with it.
	void set_resource_amount(uint32_t res_id, int32_t amount);
	// Sets a resource's ResourceSupply/MaxGatherers, as map generation does for a tree whose 0 A.D.
	// template allows other than 8 (tree_slot_gatherers.h).
	void set_resource_max_gatherers(uint32_t res_id, int max_gatherers);
	// ResourceSupply/MaxGatherers of a resource (0 if there is none with that id).
	int resource_max_gatherers(uint32_t res_id) const {
		auto it = res_id_to_idx_.find(res_id);
		return it == res_id_to_idx_.end() ? 0 : resources_.max_gatherers[it->second];
	}
	// ResourceSupply/Max a regrowing supply (fruit) returns to, 0 when RTS0 keeps none.
	int32_t resource_supply_max(uint32_t res_id) const {
		auto it = res_id_to_idx_.find(res_id);
		return it == res_id_to_idx_.end() ? 0 : resources_.max_amount[it->second];
	}
	// A mine's facing (radians), fixed by its id as a treasure's is: the world view turns each
	// resource's model by it, and a mine's Obstruction/Static square lies along it.
	static float mine_angle(uint32_t id) { return (float)(id % 12) * 0.52f; }
	// Resource r's Obstruction/Static when it is a mine (ore, rock or ruins), turned by mine_angle:
	// template_gaia_ore and _rock 7 x 7 m, their _large 13 x 13 m (24 gatherers or more),
	// temperate_large_02 22 x 14 m (48), template_gaia_ruins 7 x 7 m. Mines block movement,
	// pathfinding and foundations (template_gaia). False for any other supply.
	bool mine_footprint(size_t r, nav::Footprint &f) const;
	// Half the width of any other supply's square foundation obstruction, in world units: a
	// tree's or fruit's template Obstruction/Static (flora_obstruction_m), else 1.5 m.
	float resource_block_half(size_t r) const;
	// `rot` is the building's Position angle (docks ignore it and face the sea, dock_angle).
	uint32_t spawn_building(int cell_x, int cell_y, uint8_t type, uint8_t team = 0, float progress = 1.0f,
			float rot = DEFAULT_BUILDING_ROT);
	// Test hook: a unit carries `amount` of resource `res`.
	void debug_set_carry(uint32_t unit_id, uint8_t res, int32_t amount) {
		const int32_t idx = lookup_unit(unit_id);
		if (idx < 0 || res >= RES_COUNT) return;
		units_.res_type_carried[idx] = res;
		units_.carry[idx] = amount;
	}
	uint32_t spawn_building_world(float wx, float wz, uint8_t type, uint8_t team = 0, float progress = 1.0f,
			float rot = DEFAULT_BUILDING_ROT);
	// The civ of a structure's own template (Identity/Civ): a map's Gaia shrine or trading post.
	void set_building_civ(uint32_t building_id, uint8_t civ);
	// CaptureTheRelic.js: a Gaia catafalque of a civ picked from its id; returns the unit id.
	uint32_t spawn_relic(float x, float z, int civ_pick = -1); // civ_pick: which catafalque (-1: from the id)
	std::vector<std::pair<float, float>> relic_spawn_points() const; // TriggerHelper.GetLandSpawnPoints
	// Right-click on a relic that is not ours or an ally's: walk up and Capture it.
	void order_capture_relic(const uint32_t *unit_ids, size_t count, uint32_t relic_id, bool queued = false);
	bool is_relic_unit(int32_t i) const { return i >= 0 && units_.unit_type[i] == UNIT_CATAFALQUE; }

	float get_terrain_height(float x, float z) const;
	float map_half() const { return map_half_; }
	float water_height() const { return terrain_water_height_; }
	const std::vector<float>& terrain_heights() const { return terrain_heights_; }
	int terrain_res() const { return terrain_res_; }
	// 0 A.D. per-tile terrain textures (CTerrain tiles, STileDesc tex1): the
	// map's terrain names (0 A.D. ids, drawn from assets/textures/terrain/<name>.png)
	// and one name index per tile, res x res tiles over the whole map, row-major
	// from the -x/-z corner. res 0 = the map has none (the view paints by biome).
	struct TerrainTiles {
		int res = 0;
		std::vector<std::string> names;
		std::vector<uint8_t> tiles;
		bool empty() const { return res <= 0 || names.empty(); }
		uint8_t at(int tx, int tz) const { return tiles[(size_t)tz * (size_t)res + (size_t)tx]; }
	};
	static constexpr int MAX_TERRAIN_TILE_RES = 1024;
	static constexpr size_t MAX_TERRAIN_NAMES = 255;
	// Takes the tiles when they are consistent (res x res indices below the
	// name count, res and names within the limits); otherwise clears them.
	void set_terrain_tiles(TerrainTiles t);
	const TerrainTiles &terrain_tiles() const { return terrain_tiles_; }
	// Map JSON "terrain" (tools/port_0ad_maps.py extract_pmp_terrain): base64 of
	// (run - 1, name index) byte pairs over the res x res tiles. Empty on bad data.
	static TerrainTiles terrain_tiles_from_rle(int res, std::vector<std::string> names, const std::string &rle_base64);
	// The terrain name index of the tile under (x, z), or -1 with no tiles.
	int terrain_tile_at(float x, float z) const;
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
		terrain_lowest_known_ = false;
	}
	void set_map_info(const std::string &id, const std::string &name, const std::string &biome) {
		current_map_id_ = id;
		current_map_name_ = name;
		current_map_biome_ = biome;
	}

	// Commands
	void cmd_move(const uint32_t *unit_ids, size_t count, float tx, float tz);
	void cmd_stop(const uint32_t *unit_ids, size_t count);
	void reset_unit_for_new_owner(size_t u);
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
	// `count` units in one batch (0 A.D. Trainer.js QueueBatch): count x the cost, trained in
	// BuildTime * count^BatchTimeModifier.
	bool cmd_train(uint8_t player, uint32_t building_id, UnitType u_type, int count = 1);
	// Train a specific roster unit (a hero, a champion, a second unit of the same class).
	bool cmd_train_template(uint8_t player, uint32_t building_id, uint16_t tmpl, int count = 1, bool push_front = false);
	bool cmd_train_template_at(uint8_t player, uint32_t building_id, uint16_t tmpl, bool exclusive, int count = 1,
			bool push_front = false);
	// Trainer.js GetBatchTime: count^BatchTimeModifier of the building (civ version and techs).
	// bld_civ: the structure's civ, whose template sets Trainer/BatchTimeModifier (the owner's when unknown).
	float batch_time_multiplier(uint8_t player, uint8_t bld_type, int count, uint8_t bld_civ = 0xFF) const;
	// The same for a training unit (a trireme, Cyrus II): its Trainer/BatchTimeModifier.
	float unit_trainer_batch_mult(uint16_t trainer_tmpl, uint16_t trainee, int count) const;
	// ProductionQueue.js EnableAutoQueue / DisableAutoQueue.
	bool cmd_set_autoqueue(uint8_t player, uint32_t building_id, bool on);
	// The technology a structure is researching now (the front of its queue), else TECH_NONE.
	static uint8_t researching(const Building &b) { return b.queue.empty() ? TECH_NONE : b.queue.front().tech; }
	// Whether `tech` is anywhere in one of the player's production queues.
	bool tech_queued(uint8_t player, uint8_t tech) const;
	bool cmd_cancel_train(uint8_t player, uint32_t building_id, size_t queue_idx);
	// Units that train (0 A.D. Trainer on a unit: Athenian and Persian triremes, Cyrus II;
	// unit_trainer_data.h). Each trainer unit keeps its own production queue; the units it
	// trains step out beside it (Footprint.PickSpawnPoint).
	bool cmd_train_from_unit(uint8_t player, uint32_t unit_id, uint16_t tmpl, int count = 1);
	bool cmd_cancel_unit_train(uint8_t player, uint32_t unit_id, size_t queue_idx);
	// Roster templates the unit `tmpl` trains for `player` now (Trainer/Entities with their
	// requirements met), and every one it could train (`all`).
	std::vector<uint16_t> unit_trainables(uint8_t player, uint16_t tmpl, bool all = false) const;
	// The technology a training unit's Trainer list entry requires (unit_trainer_data.h), or TECH_NONE.
	static uint8_t unit_trainee_tech(uint16_t trainer_tmpl, uint16_t trainee);
	static bool unit_is_trainer(uint16_t tmpl); // trains or researches (Trainer or Researcher)
	// 0 A.D. Researcher on a unit (Athenian triremes: Iphicratean Reforms): the technologies the
	// roster unit `tmpl` researches, and queueing one (research_check's rules, without a structure).
	static std::vector<uint8_t> unit_researchables(uint16_t tmpl);
	bool cmd_research_from_unit(uint8_t player, uint32_t unit_id, uint8_t tech);
	// A training unit's queue. `player` paid; Trainer.js OnOwnershipChanged refunds them when
	// the unit dies or changes hands.
	struct UnitQueue {
		uint8_t player = 0;
		std::vector<ProductionOrder> orders; // never empty
	};
	const std::map<uint32_t, UnitQueue> &unit_queues() const { return unit_queues_; }
	const std::vector<ProductionOrder> *unit_queue(uint32_t unit_id) const {
		auto it = unit_queues_.find(unit_id);
		return it == unit_queues_.end() ? nullptr : &it->second.orders;
	}
	// queued (Shift) adds a position after the ones already set (RallyPoint.AddPosition).
	// cmd (RallyCommand) and ent_id: the commands a target alone doesn't give (attack-walk,
	// attack a unit, trade, collect a treasure, occupy a turret).
	void cmd_set_rally_point(uint8_t player, uint32_t building_id, float rx, float rz, uint32_t res_id = 0, uint32_t bld_id = 0,
			bool queued = false, uint8_t cmd = 0, uint32_t ent_id = 0);
	// 0 A.D. "unset-rallypoint": the structure forgets every rally position.
	void cmd_unset_rally_point(uint8_t player, uint32_t building_id);

	// Player orders (0 A.D. Commands.js / UnitAI.js). `queued` is the Shift key: the order
	// goes to the end of each busy unit's order queue instead of replacing its orders.
	// Player orders are forced (order.data.force); Petra's cmd_move walks are not, so its
	// units still turn to fight on the way.
	// beach_goal: a player's walk, which ends on the beach when (tx, tz) is out at sea
	// (MakeGoalReachable). Internal walks (Petra, rally points) keep their goal.
	void order_walk(const uint32_t *unit_ids, size_t count, float tx, float tz, bool queued = false,
			bool beach_goal = false);
	// A player's Gather and Repair are forced (UnitAI Gather / Repair) until work starts;
	// UnitAI's own follow-up gathers and repairs (PerformGather force false) are not.
	void order_gather(const uint32_t *unit_ids, size_t count, uint32_t resource_id, bool queued = false, bool force = true);
	// Gather from a supply that is gone (its mirage clicked): the nearest of the same kind near
	// (x, z), else a walk there (UnitAI FINDINGNEWTARGET).
	void order_gather_near(const uint32_t *unit_ids, size_t count, float x, float z, uint8_t res_type, uint8_t res_sub,
			bool queued = false);
	void order_attack(const uint32_t *unit_ids, size_t count, uint32_t target_unit_id, bool queued = false);
	// allow_capture: capture the structure (0 A.D.'s default right-click) instead of
	// damaging it (Ctrl). Units that can't capture, or structures that can't be
	// captured, fall back to damage.
	void order_attack_building(const uint32_t *unit_ids, size_t count, uint32_t target_building_id,
			bool queued = false, bool allow_capture = false);
	// Attack-move (UnitAI WalkAndFight): fight units, then structures unless units_only,
	// on the way, and carry on to (tx, tz) afterwards.
	// `avoid` holds ORDER_AVOID_* bits: structures the walk does not stop for.
	void order_attack_move(const uint32_t *unit_ids, size_t count, float tx, float tz,
			bool queued = false, bool units_only = false, bool allow_capture = false, uint8_t avoid = 0);
	void order_patrol(const uint32_t *unit_ids, size_t count, float tx, float tz, bool queued = false);
	bool unit_can_patrol(size_t i) const;
	void order_build_or_repair(const uint32_t *unit_ids, size_t count, uint32_t building_id, bool queued = false, bool force = true);
	// 0 A.D. "delete-entities": kills the player's own units and destroys its own
	// structures (not ones it holds under half the capture points of).
	void cmd_delete(uint8_t player, const uint32_t *ids, size_t count);
	// 0 A.D. Cheat.js, for a player when Game Setup's Cheats is on. `parameter` is the
	// number typed after the code; `extra` is the resource (ADD_RESOURCE) or the tech id
	// (RESEARCH); `selected` is the player's selection, whoever owns it. CONVERT_UNITS and
	// DEFEAT_PLAYER take an RTS0 player id (GAIA_PLAYER for Gaia, 0xFF for yourself).
	enum CheatAction : uint8_t {
		CHEAT_ADD_RESOURCE, CHEAT_REVEAL_MAP, CHEAT_CONVERT_UNITS, CHEAT_KILL_UNITS, CHEAT_DEFEAT_PLAYER,
		CHEAT_CREATE_UNITS, CHEAT_CHANGE_PHASE, CHEAT_RESEARCH, CHEAT_META,
		CHEAT_MAX_POPULATION, CHEAT_CHANGE_MAX_POPULATION, CHEAT_FAST_ACTIONS,
		// The developer overlay's commands (Commands.js "reveal-map" with its enable flag, "promote").
		// CHEAT_DEV_PERSPECTIVE (Change perspective: Engine.SetPlayerID) is applied by the
		// TurnManager, which seats the sender as `parameter` (0xFF: observer).
		CHEAT_DEV_REVEAL_MAP, CHEAT_DEV_PROMOTE, CHEAT_DEV_PERSPECTIVE, CHEAT_DEV_CONTROL_ALL, CHEAT_COUNT
	};
	// Per-player state of the population and fast-actions cheats (padding-free for saves).
	struct CheatMods {
		int32_t pop_bonus;   // "the hive master": added to the player's house population bonuses
		int32_t max_pop_add; // "TARDIS": cheat/maxpopulation, Player/MaxPopulation +500
		uint8_t fast_actions; // "i am too busy": cheat/fastactions is on
		uint8_t pad[3];
	};
	const CheatMods &cheat_mods(uint8_t player) const { return cheat_mods_[player < MAX_PLAYERS ? player : 0]; }
	bool cmd_cheat(uint8_t player, uint8_t action, int32_t parameter, uint32_t extra,
			const uint32_t *selected, size_t count);
	// 0 A.D. "spy-request" (Commands.js, VisionSharing.AddSpy): with Espionage researched,
	// bribe a random trader or merchant ship of `target` for its vision (500 metal, x1.5
	// against Counterintelligence; a quarter of that is lost when nothing can be bribed).
	enum BribeResult : uint8_t { BRIBE_OK, BRIBE_NOT_ALLOWED, BRIBE_NO_UNITS, BRIBE_TOO_EXPENSIVE };
	// Commands.js "attack-request": `player` asks its allies to attack `target`; their
	// Petras answer at the end of the AI turn (attackManager.js checkEvents).
	void cmd_attack_request(uint8_t player, uint8_t target);
	BribeResult cmd_bribe(uint8_t player, uint8_t target);
	int bribe_cost(uint8_t player, uint8_t target, bool failed = false) const; // metal
	bool can_bribe(uint8_t player, uint8_t target) const;
	struct Spy { uint32_t unit_id; uint8_t player; uint8_t pad[3]; float remaining; }; // padding-free for saves
	const std::vector<Spy> &spies() const { return spies_; }
	// 0 A.D. "back-to-work" (UnitAI BackToWork): resume the last gather or build order.
	void order_back_to_work(const uint32_t *unit_ids, size_t count);
	// 0 A.D. Commands.js "remove-guard" (UnitAI RemoveGuard): end a Guard order, or drop
	// the queued ones.
	void order_remove_guard(const uint32_t *unit_ids, size_t count);
	// 0 A.D. Commands.js "call-to-arms": Soldiers, Warships, Siege and Healers drop their load
	// at the nearest dropsite (DropAtNearestDropSite), then attack-walk to (x, z): units only with
	// session.attackmoveUnit, capturing with session.capture.
	void order_call_to_arms(const uint32_t *unit_ids, size_t count, float x, float z, bool queued = false,
			bool units_only = false, bool allow_capture = false);
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
		// v37: the attack order (UnitAI FORMATIONCONTROLLER.COMBAT / MEMBER) and patrol.
		uint32_t target_unit = 0;       // Order.Attack/Heal target unit, Order.Gather resource
		uint32_t target_building = 0;   // Order.Attack/Repair/Garrison target structure
		uint8_t task_flags = 0;         // FormationTask << FTASK_SHIFT | FTASK_CAPTURE (Attack.js allowCapture)
		uint8_t returning = 0;          // order.data.returningState: where MEMBER goes back to
		float member_timer = 0.0f;      // MEMBER / CHECKINGWAYPOINT one-second timer
		float patrol_x = 0.0f, patrol_z = 0.0f; // patrolStartPosOrder: the other end of the patrol
		uint8_t patrol_wait = 0;        // CHECKINGWAYPOINT stopSurveying
		std::vector<uint32_t> twins;    // Formation.js twinFormations (controller ids)
		// v39: the controller's orderQueue after its current order (Shift-queued orders).
		std::vector<UnitOrder> queue;
	};
	// UnitAI FORMATIONCONTROLLER states: WALKING, WALKINGANDFIGHTING, COMBAT.APPROACHING,
	// MEMBER (members fight on their own), PATROL.PATROLLING and PATROL.CHECKINGWAYPOINT.
	enum FormationOrder : uint8_t {
		FORDER_IDLE = 0, FORDER_WALK = 1, FORDER_ATTACK_WALK = 2, FORDER_APPROACH = 3,
		FORDER_MEMBER = 4, FORDER_PATROL = 5, FORDER_PATROL_WAIT = 6
	};
	// The order the controller walks up for (APPROACH) and then hands to the members:
	// FORMATIONCONTROLLER Order.Attack, Order.Gather, Order.Repair, Order.Garrison, Order.Heal.
	enum FormationTask : uint8_t {
		FTASK_ATTACK = 0, FTASK_GATHER = 1, FTASK_REPAIR = 2, FTASK_GARRISON = 3, FTASK_HEAL = 4
	};
	static constexpr uint8_t FTASK_CAPTURE = 1, FTASK_SHIFT = 4;
	static uint8_t formation_task(const FormationController &fc) { return fc.task_flags >> FTASK_SHIFT; }
	const std::vector<FormationController> &formations() const { return formations_; }
	// Controller id of the formation unit_id belongs to, 0 when none.
	uint32_t formation_of_unit(uint32_t unit_id) const;
	// turret: Commands.js "occupy-turret" (session.occupyturret), the holder's turret points
	// instead of its garrison.
	void order_garrison(const uint32_t *unit_ids, size_t count, uint32_t building_id, bool queued = false,
			bool turret = false);
	// forced_turrets: turret units step down even with no free spot (GetSpawnPosition forced).
	void order_ungarrison(uint32_t building_id, bool forced_turrets = false);
	// GarrisonHolder.EjectOrKill. With `keep_allies_of` a player, only units not owned by that
	// player or a mutual ally of theirs leave (OnGlobalOwnershipChanged, OnDiplomacyChanged).
	void eject_or_kill(Building &b, uint8_t keep_allies_of = 0xFF);
	// GarrisonHolder.IsAllowedToBeGarrisoned: the unit's owner owns `b` or is its mutual ally
	// (IsOwnedByMutualAllyOfEntity) and the unit matches the holder's List (a TurretHolder:
	// Turretable). Room is not checked.
	bool structure_accepts_garrison(const Building &b, size_t unit_idx) const;
	bool structure_accepts_classes(const Building &b, TechClasses classes) const;
	bool can_garrison_in(uint32_t unit_id, uint32_t building_id) const;
	// GarrisonHolder.HasEnoughHealth: above EjectHealth of max health, so it may hold a garrison.
	bool structure_has_enough_health(const Building &b) const;
	// Hitpoints per second each unit garrisoned in `b` regains (BuffHeal plus Living Conditions).
	float structure_garrison_heal(const Building &b) const;
	void order_ungarrison_single(uint32_t building_id, uint32_t unit_id);
	// Commands.js unload / unload-all-by-owner: the holder's owner may unload anyone; an
	// ally only its own units. count 0 unloads every unit `player` may unload.
	void cmd_unload(uint8_t player, uint32_t building_id, const uint32_t *unit_ids, size_t count);
	// The units in a structure's garrison or aboard a ship, ram or siege tower (GarrisonHolder
	// GetEntities), and the holder's owner; empty if holder_id is neither.
	std::vector<uint32_t> holder_contents(uint32_t holder_id, uint8_t *holder_owner = nullptr) const;
	// What `player` may unload from a holder (Commands.js unload): all of it from its own, its
	// own units from a mutual ally's.
	std::vector<uint32_t> unloadable_by(uint8_t player, uint32_t holder_id) const;
	// Animals garrisoned in a corral (GarrisonHolder/Max CORRAL_GARRISON_MAX).
	int corral_count(uint32_t corral_id) const;
	// AlertRaiser.js on a structure of `player`: RaiseAlert sends its civilians (women; a
	// market's land traders) into the nearest structures with room, or stops them;
	// EndOfAlert unloads them and sends them back to work. False when it has no alert.
	// 0 A.D. "focus-fire" (BuildingAI.AddFocusTarget): `player`'s structures in `building_ids`
	// that shoot arrows aim at `target` (a unit, structure or dangerous animal they can
	// attack and that is in their range) before anything else: it replaces the focus list,
	// joins its end (queued) or goes first (push_front). Returns whether any structure took it.
	static constexpr size_t MAX_FOCUS_TARGETS = 64;
	bool cmd_focus_fire(uint8_t player, const uint32_t *building_ids, size_t count, uint32_t target,
			bool queued, bool push_front = false);
	bool cmd_raise_alert(uint8_t player, uint32_t building_id);
	bool cmd_end_alert(uint8_t player, uint32_t building_id);
	// target_unit_id: a unit or a structure, one's own or an ally's (Guard.js).
	void order_guard(const uint32_t *unit_ids, size_t count, uint32_t target_unit_id, bool queued = false);
	// UnitAI Heal: healers walk to an injured allied unit and heal it (Heal.js).
	void order_heal(const uint32_t *unit_ids, size_t count, uint32_t target_unit_id, bool queued = false);
	// Repairable on a unit (siege engines and ships but fire ships): a builder can repair it.
	bool is_repairable_unit(uint32_t unit_id) const;
	// 0 A.D. "returnresource" (UnitAI ReturnResource): carriers take their load to this
	// dropsite, drop it, then take the next queued order or idle.
	// force: a player's or Petra's ReturnResource; UnitAI's own drop-offs (PushOrderFront) are unforced.
	void order_return_resource(const uint32_t *unit_ids, size_t count, uint32_t dropsite_id, bool queued = false, bool force = true);
	void order_return_to_unit(const uint32_t *unit_ids, size_t count, uint32_t dropsite_id, bool queued, bool force);
	bool dropsite_accepts(size_t unit_idx, const Building &b) const; // carried type, owner, finished
	bool can_return_resource(uint32_t unit_id, uint32_t dropsite_id) const;
	// ResourceGatherer CanReturnResource's owner test: `player`'s own dropsite, or a shared one of a
	// mutual ally once `player` has researched Diaspora (Player/sharedDropsites).
	bool dropsite_usable_by(uint8_t player, const Building &b) const;
	// The same test for unit `e` when it is a mobile dropsite (worker elephant, supply wagon).
	bool mobile_dropsite_usable_by(uint8_t player, size_t e) const;
	bool unit_dropsite_shared(uint32_t unit_id) const; // ResourceDropsite IsShared on a unit
	// 0 A.D. "set-dropsite-sharing" (ResourceDropsite.SetSharing) on one of `player`'s dropsites;
	// only an owner with Diaspora sees the toggle. False if nothing changed.
	bool cmd_set_dropsite_sharing(uint8_t player, uint32_t building_id, bool shared);
	static bool is_dropsite_type(uint8_t type) {
		return is_civic_centre(type) || type == BLD_DOCK || type == BLD_STOREHOUSE || is_food_dropsite(type) ||
			civ_building_is_dropsite(type);
	} // carries a load b accepts
	void set_unit_hp(uint32_t unit_id, float hp); // scenarios and tests: clamped to 0..max

	// Livestock & Corrals (Corral.js)
	void order_herd(const uint32_t *unit_ids, size_t count, uint32_t fauna_id);
	// 0 A.D. owned livestock is a unit its owner selects and orders (UnitAI "walk",
	// "garrison" into a corral, "stop"). Ids that aren't player's living, unpenned
	// animals are ignored, so a command's whole selection can be passed.
	void order_fauna_walk(uint8_t player, const uint32_t *ids, size_t count, float x, float z, uint32_t corral_id = 0);
	void order_fauna_stop(uint8_t player, const uint32_t *ids, size_t count);
	bool fauna_selectable_by(uint8_t player, uint32_t fauna_id) const;
	bool cmd_breed_livestock(uint8_t player, uint32_t building_id, uint8_t ftype = FAUNA_SHEEP_TRAINABLE, int count = 1);
	static uint8_t corral_animal_for(uint8_t civ, uint16_t ftype);
	float livestock_train_time(uint8_t player, uint8_t ftype) const;

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
	// Auras.js "garrison" auras on BuildingAI (Caros' Valiant Defender) from the units `ids`
	// that `owner` garrisons: what GarrisonArrowMultiplier and MaxArrowCount are multiplied by.
	void garrison_arrow_auras_of(const std::vector<uint32_t> &ids, uint8_t owner, float &arrows, float &max_arrows) const;
	// 0 A.D. BuildingAI.js GetArrowCount: arrows per RepeatTime for a finished structure.
	int building_arrow_count(const Building &b) const;
	// Attack/Ranged/MaxRange with technologies, measured from the structure's edge (0 = never shoots).
	float building_arrow_range(const Building &b) const;
	// building_arrow_range before the structure exists: the type's MaxRange and the player's range technologies.
	float structure_arrow_range(uint8_t player, uint8_t type) const;
	// GuiInterface GetAverageRangeForBuildings (RangeManager GetElevationAdaptedRange over a full
	// circle): the parabolic reach of `range` fired from y_origin above the ground at (x, z),
	// averaged over 16 directions of the terrain around it.
	float average_elevation_range(float x, float z, float range, float y_origin) const;
	// The same reach along one direction (ux, uz), as a unit's GetElevationAdaptedRange at
	// angle 0 takes the range in front of it.
	float elevation_range_toward(float x, float z, float ux, float uz, float range, float y_origin) const;
	// What 0.28's detailed tooltip of a structure reads from its template after the owner's
	// modifiers (selection_details.js: getAttackTooltip, getGarrisonTooltip, getTurretsTooltip,
	// getPopulationBonusTooltip, getProjectilesTooltip, getResourceTrickleTooltip).
	struct StructureDetails {
		bool shoots = false;
		float damage[3] = {};       // BuildingAI's Attack/Ranged/Damage: hack, pierce, crush
		float min_range = 0.0f, max_range = 0.0f; // RTS0 units, from the structure's edge
		float repeat_time = 0.0f;   // seconds
		int arrows = 0;             // BuildingAI.GetArrowCount now
		int default_arrows = 0;     // DefaultArrowCount
		float per_unit = 0.0f;      // GarrisonArrowMultiplier
		int projectile_limit = 0;   // min(MaxArrowCount, default + round(per unit x capacity)); 0 = none
		float heal = 0.0f;          // GarrisonHolder BuffHeal, hitpoints per second
		int pop_bonus = 0;          // Population/Bonus
		int turrets = 0;            // TurretHolder points
		bool trickles = false;
		float trickle[RES_COUNT] = {}; // ResourceTrickle Rates per trickle_interval
		float trickle_interval = TRICKLE_STRUCTURE_INTERVAL; // a corral's 5 s (CORRAL_TRICKLE_INTERVAL)
	};
	StructureDetails structure_details(const Building &b) const;
	// Foundation.js / Repairable.js GetBuildTime for the details panel: the builders at work on a
	// foundation (or repairers on a damaged structure), the seconds left at their pace and with
	// one more builder (totalBuilderRate + 1). False when it is neither building nor damaged.
	struct BuildWork {
		bool repair = false;
		int builders = 0;
		float remaining = 0.0f, remaining_new = 0.0f;
	};
	bool structure_build_work(const Building &b, BuildWork &out) const;
	// The same for a damaged Repairable unit (siege engines and ships): its repairers at work and
	// the time left at tick_repair_unit's rate; false when unit t needs no repair.
	bool unit_repair_work(size_t t, BuildWork &out) const;
	// The statuses on an entity, as selection_details.js lists entState.statusEffects: each
	// stacked copy with its Damage, Interval and the Duration still to run (seconds). Returns
	// how many there are; at most `max` are written to `out`.
	static constexpr uint8_t STATUS_ON_UNIT = 0, STATUS_ON_STRUCTURE = 1, STATUS_ON_FAUNA = 2;
	struct StatusView {
		uint8_t kind = 0; // 1 Poisoned, 2 Burning
		float damage = 0.0f, interval = 0.0f, remaining = 0.0f;
	};
	int statuses_on(uint8_t target_kind, uint32_t target, StatusView *out, int max) const;
	static constexpr float TRICKLE_STRUCTURE_INTERVAL = 2.0f; // the wonder's, ice house's and tachara's Interval 2000
	// ResourceTrickle.js Rates of `b` after its owner's technologies and global auras; false
	// when it has no ResourceTrickle.
	bool structure_trickle_rates(const Building &b, float rate[RES_COUNT]) const;
	// A unit's hack, pierce and crush resistance levels (template, techs, rank, armour auras).
	void structure_resistance(const Building &b, float out[3]) const;
	void unit_resistance(size_t i, float out[3]) const;
	// GetEntityState for the selection details (selection_details.js, tooltips.js): unit i's
	// live numbers in 0 A.D.'s own units (metres, metres a second), technologies, rank and
	// auras applied.
	struct UnitDetails {
		float damage[4] = {};     // hack, pierce, crush, fire
		float capture = 0.0f;     // Attack/Capture
		float interval = 0.0f;    // seconds between attacks
		float min_range = 0.0f, max_range = 0.0f;
		bool ranged = false;
		float resistance[3] = {}; // hack, pierce, crush levels
		float walk = 0.0f, run = 0.0f, acceleration = 0.0f; // m/s, m/s^2
		float gather[7] = {};     // grain, fruit, meat, fish, wood, stone, metal a second
		int32_t carry_max = 0;    // what it can carry of the resource it holds
		float heal_hp = 0.0f, heal_range = 0.0f, heal_interval = 0.0f; // 0 hp: no Heal
		LootInfo loot{};
	};
	UnitDetails unit_details(size_t i) const;
	int get_transport_capacity(uint32_t transport_id) const;
	// GarrisonHolder.OccupiedSlots of a ship or siege engine, and IsAllowedToGarrison for one unit.
	int get_transport_occupied(uint32_t transport_id) const {
		auto it = transport_passengers_.find(transport_id);
		return it == transport_passengers_.end() ? 0 : occupied_slots(it->second);
	}
	bool can_board_unit(uint32_t transport_id, uint32_t unit_id) const;
	// Ships as GarrisonHolders (template_unit_ship*.xml): capacity and List per
	// template, LoadingRange 10 m, Pickup, EjectClassesOnDestroy. `i` is a SoA index.
	int ship_garrison_capacity(size_t i) const;
	static int ship_template_capacity(uint8_t unit_type, uint16_t tmpl);
	bool ship_garrison_allowed(size_t ship, size_t unit) const;
	float ship_loading_reach(size_t ship, size_t unit) const;
	float transport_loading_reach(size_t holder, size_t unit) const;
	// PickSpawnPoint per passenger: a spot nobody stands on (the passengers already set down in
	// this unload are in `taken`), false when there is none; without a passenger, the nearest dry ground.
	struct SpawnSpot { float x, z, r; };
	bool ship_unload_point(size_t ship, float &out_x, float &out_z, size_t passenger = SIZE_MAX,
			std::vector<SpawnSpot> *taken = nullptr) const;
	// PickSpawnPoint for one passenger of `type` leaving a siege tower or ram: the spots round the
	// engine's square footprint, its front toward (toward_x, toward_z), dry ground only. skip_uid is
	// the passenger itself. With no spot the caller keeps it aboard or, forced, puts it at the centre.
	bool siege_spawn_point(size_t holder, uint8_t type, uint32_t skip_uid, float toward_x, float toward_z,
			float &out_x, float &out_z) const;
	bool nearest_water_for_ship(size_t ship, float x, float z, float &out_x, float &out_z) const;
	void unload_sunk_ship(size_t ship);

	// Garrison & Turret System (GarrisonHolder.js, TurretHolder.js)
	int get_garrison_count(uint32_t building_id) const;
	int get_garrison_capacity(uint32_t building_id) const;
	// GarrisonHolder.OccupiedSlots of a structure: its units' Garrisonable sizes.
	int get_garrison_occupied(uint32_t building_id) const {
		auto bit = building_id_to_idx_.find(building_id);
		return bit == building_id_to_idx_.end() ? 0 : occupied_slots(buildings_[bit->second].garrisoned_units);
	}
	const std::vector<uint32_t> *get_garrisoned_units(uint32_t building_id) const;

	// Victory Conditions (VictoryManager.js, EndGameManager.js)
	uint8_t victory_condition() const { return (uint8_t)victory_condition_; }
	void set_victory_condition(uint8_t vc) { victory_condition_ = (VictoryCondition)vc; }
	uint8_t victory_conditions() const { return victory_mask(victory_condition_); }
	void set_victory_conditions(uint8_t mask) { victory_condition_ = (VictoryCondition)victory_code(mask); }
	bool has_victory(uint8_t kind) const { return (victory_mask(victory_condition_) & kind) != 0; }
	float relic_victory_timer() const { return relic_victory_timer_; }
	// CaptureTheRelic.js relicsVictoryCountdownPlayers: who the running countdown would make win.
	uint8_t relic_countdown_players() const { return relic_countdown_players_; }
	// CheckCaptureTheRelicCountdown's winningPlayers from the current relic owners and
	// diplomacy, or 0 while Gaia holds a relic, nobody holds one or the owners are not allies.
	uint8_t relic_winners() const;
	// Legacy wave-defense mode: enemy waves every 35s, survive wave 5 to win. Off by default.
	bool waves_enabled() const { return waves_enabled_; }
	void set_waves_enabled(bool on) { waves_enabled_ = on; }
	// 0 A.D. "Last Man Standing": allies don't share a win; the match goes on until one player is left.
	bool last_man_standing() const { return last_man_standing_; }
	void set_last_man_standing(bool on) { last_man_standing_ = on; reroll_petra_personalities(); }
	// 0 A.D. population limit per player (game setup); 0 means unlimited. A negative
	// setting is 0 A.D.'s World Population: -setting shared evenly by the players
	// still in the game (PlayerManager.RedistributeWorldPopulation), so each
	// defeat raises everyone else's share. A setting at or below -TEAM_POP_FLAG is
	// 0 A.D. 0.28's Team Population (PopulationCapManager "team"): each team gets
	// the cap, split evenly by its members still in the game, and a teamless
	// player keeps all of it. Stored in pop_cap_, so no save change.
	static constexpr int MAX_WORLD_POP = 63000; // 0 A.D. world_population_capacities.json
	static constexpr int MAX_TEAM_POP = 10000;  // 0 A.D. team_population_capacities.json
	static constexpr int TEAM_POP_FLAG = 100000;
	int pop_cap() const { return pop_cap(PETRA_PLAYER); }
	int pop_cap(uint8_t player) const; // this player's share of the population limit, 0 = unlimited
	int pop_cap_setting() const { return pop_cap_; }
	void set_pop_cap_setting(int setting); // restores pop_cap_setting(), clamped to a valid one
	int world_pop_cap() const { return pop_cap_ < 0 && pop_cap_ > -TEAM_POP_FLAG ? -pop_cap_ : 0; }
	int team_pop_cap() const { return pop_cap_ <= -TEAM_POP_FLAG ? -pop_cap_ - TEAM_POP_FLAG : 0; }
	void set_pop_cap(int cap) { pop_cap_ = std::max(0, cap); }
	void set_world_pop_cap(int world) { pop_cap_ = world > 0 ? -std::min(world, MAX_WORLD_POP) : 0; }
	// A scenario's own PopulationLimit for one player (0.28 PlayerData; InitGame.js gives the
	// other players the setup's cap and PopulationCapManager.SetPerPlayerPopulationCaps). 0
	// clears it; reset() clears every player's. Saved (SAVE_VERSION_PLAYER_POP_CAP).
	static constexpr int MAX_PLAYER_POP_CAP = 1000000;
	void set_player_pop_cap(uint8_t player, int cap) {
		if (player < MAX_PLAYERS) player_pop_cap_[player] = std::clamp(cap, 0, MAX_PLAYER_POP_CAP);
	}
	int player_pop_cap(uint8_t player) const { return player < MAX_PLAYERS ? player_pop_cap_[player] : 0; }
	void set_team_pop_cap(int team) { pop_cap_ = team > 0 ? -TEAM_POP_FLAG - std::min(team, MAX_TEAM_POP) : 0; }
	// 0 A.D. ceasefire: no player can attack another for the first N seconds of the match.
	// 0 A.D. map visibility options: everything visible, or terrain known from the start.
	bool reveal_map() const { return reveal_map_; }
	void set_reveal_map(bool on) { reveal_map_ = on; }
	// Player.js controlAllUnits (the developer overlay's Control all units): the
	// player orders every live unit. Saved (SAVE_VERSION_PROJECTILE_DAMAGE).
	bool controls_all(uint8_t player) const { return player < MAX_PLAYERS && players_[player].controls_all; }
	uint8_t entity_owner(uint32_t id) const { return petra_owner_of(id); } // a structure's or unit's owner, 0xFF if gone
	// Game Setup biome for random maps: BIOME_* or -1 for the map's own.
	void set_biome_override(int b) { biome_override_ = (b >= 0 && b < BIOME_COUNT) ? b : -1; }
	int biome_override() const { return biome_override_; }
	// The Game Setup biome a random map takes: the override when the map's 0.28 SupportedBiomes
	// list it (map_biomes_data.h), else -1 and the map keeps its own biome.
	int map_biome_pick(int type) const {
		return biome_override_ >= 0 && (map_supported_biomes(type) >> biome_override_ & 1u) ? biome_override_ : -1;
	}
	// 0 A.D. Team Placement (TEAM_PLACEMENT_*) for the random maps that offer it
	// (map_has_team_placement); setup-time only, like the biome.
	void set_team_placement(int t) { team_placement_ = (uint8_t)((t >= 0 && t < TEAM_PLACEMENT_COUNT) ? t : TEAM_PLACEMENT_RANDOM); }
	uint8_t team_placement() const { return team_placement_; }
	// The pattern the map generator placed players by (Random resolved, as 0.28's
	// PlayerPlacement.pickRandomItems); TEAM_PLACEMENT_RANDOM on maps without the setting.
	void set_played_team_placement(uint8_t t) { played_team_placement_ = t; }
	uint8_t played_team_placement() const { return played_team_placement_; }
	// 0 A.D. Landscape for the Unknown map (RmsGenerator::UNKNOWN_LANDSCAPE_*); setup-time only.
	void set_map_landscape(int l) { map_landscape_ = (uint8_t)((l >= 0 && l < 16) ? l : 0); }
	uint8_t map_landscape() const { return map_landscape_; }
	// 0 A.D. Game Setup Trigger Difficulty (1 Very Easy .. 5 Very Hard, 3 Medium) for maps
	// whose script reads getDifficulty(): random map generation uses it and hands it to
	// the map's triggers (TriggerState::difficulty, saved with them).
	void set_trigger_difficulty(int d) { trigger_difficulty_ = (uint8_t)((d >= 1 && d <= 5) ? d : 3); }
	uint8_t trigger_difficulty() const { return trigger_difficulty_; }
	// 0 A.D. Game Setup Daytime for the maps whose json lists one (danubius.json: day,
	// night; polar_sea.json: dawn, daylight): 0 lets the map pick as its script does
	// (Danubius day 2/3 of the time, Polar Sea dawn 1/3), else the option, 1-based.
	// map_daytime() is the option the last random map used (0: the map has none);
	// presentation only (the renderer's sky, sun and water).
	void set_map_daytime_choice(int d) { map_daytime_choice_ = (uint8_t)((d >= 0 && d <= 2) ? d : 0); }
	uint8_t map_daytime_choice() const { return map_daytime_choice_; }
	uint8_t map_daytime() const { return map_daytime_; }
	// 0 A.D. Nomad (gamesettings Nomad.js, rmgen-common placePlayerBase): random maps start
	// without any player structures, only the starting units.
	void set_nomad(bool on) { nomad_ = on; }
	bool nomad() const { return nomad_; }
	// 0 A.D. DisableTreasures: every treasure is removed when the match starts.
	void set_disable_treasures(bool on) { disable_treasures_ = on; }
	bool disable_treasures() const { return disable_treasures_; }
	// 0 A.D. RegicideGarrison: the Regicide hero may garrison (else it spawns "ungarrisonable|").
	void set_regicide_garrison(bool on) { set_setup_flag(SETUP_REGICIDE_GARRISON, on); }
	bool regicide_garrison() const { return setup_flags_ & SETUP_REGICIDE_GARRISON; }
	// 0 A.D. LockTeams (Diplomacy.LockTeam): no player can change diplomacy all match.
	void set_lock_teams(bool on) { set_setup_flag(SETUP_LOCK_TEAMS, on); reroll_petra_personalities(); }
	bool teams_locked() const { return setup_flags_ & SETUP_LOCK_TEAMS; }
	// 0 A.D. CheatsEnabled: chat cheat codes work for every player (cmd_cheat).
	void set_cheats_enabled(bool on) { set_setup_flag(SETUP_CHEATS, on); }
	bool cheats_enabled() const { return setup_flags_ & SETUP_CHEATS; }
	// 0 A.D. DisableSpies: Espionage can't be researched, so nobody can bribe.
	void set_disable_spies(bool on) { set_setup_flag(SETUP_DISABLE_SPIES, on); }
	bool spies_disabled() const { return setup_flags_ & SETUP_DISABLE_SPIES; }
	// 0 A.D. AllyView (InitGame.js): every player starts with Diplomacy's SharedLosTech researched.
	void set_ally_view(bool on) { set_setup_flag(SETUP_ALLY_VIEW, on); }
	bool ally_view() const { return setup_flags_ & SETUP_ALLY_VIEW; }
	// 0 A.D. "Circular map" (settings.CircularMap): the map is the circle inside the square.
	// The choice is 0 = the map's own (map_circular), 1 = circular, 2 = square; generate_random_map
	// applies it. Hooks: LOS (los_grid off_world), passability and the minimap read circular_map().
	void set_circular_map_choice(int c) { circular_map_choice_ = (uint8_t)((c >= 0 && c <= 2) ? c : 0); }
	uint8_t circular_map_choice() const { return circular_map_choice_; }
	bool circular_map() const { return setup_flags_ & SETUP_CIRCULAR_MAP; }
	void set_circular_map(bool on);
	// Off the world (CCmpRangeManager.cpp:2145 LosIsOffWorld): on a circular map outside
	// the LOS circle 8 m inside the edge (verts/2 - MAP_EDGE_TILES + 1); on a square map in
	// the outer 3 tiles (12 m, MAP_EDGE_DIST) on any side.
	bool off_world(float x, float z) const {
		if (circular_map()) {
			const float r = map_half_ - MAP_EDGE_LOS_INSET_CIRCULAR;
			return r <= 0.0f || x * x + z * z >= r * r;
		}
		return std::fabs(x) >= map_half_ - MAP_EDGE_DIST || std::fabs(z) >= map_half_ - MAP_EDGE_DIST;
	}
	// Past a map's passable edge, MAP_EDGE_DIST (12 m) inside the circle on a circular map
	// (CCmpPathfinder.cpp:711 UpdateGrid, MAP_EDGE_TILES) or inside the square on a square
	// map (its else branch: the 12-cell border), less a clearance c (0.28 grows the edge by
	// each class's Clearance).
	bool past_map_edge(float x, float z, float c = 0.0f) const {
		const float r = map_half_ - MAP_EDGE_DIST - c;
		if (r <= 0.0f) return true;
		if (circular_map()) return x * x + z * z > r * r;
		return std::fabs(x) > r || std::fabs(z) > r;
	}
	// Within `b` of the map's edge, its circle on a circular map (Petra's mapModule.js
	// createBorderMap fullBorder).
	bool near_map_border(float x, float z, float b) const {
		if (circular_map()) return x * x + z * z >= (map_half_ - b) * (map_half_ - b);
		return std::abs(x) >= map_half_ - b || std::abs(z) >= map_half_ - b;
	}
	// A footprint reaching past a map's passable edge (CheckBuildingPlacement).
	template <class F>
	bool footprint_past_map_edge(const F &f) const {
		const nav::Vec2 u = nav::footprint_u(f), v = nav::footprint_v(f);
		for (float su : {-1.0f, 1.0f})
			for (float sv : {-1.0f, 1.0f})
				if (past_map_edge(f.x + su * f.hu * u.x + sv * f.hv * v.x, f.z + su * f.hu * u.y + sv * f.hv * v.y)) return true;
		return false;
	}
	uint32_t regicide_hero(uint8_t pid) const { return pid < MAX_PLAYERS ? regicide_hero_[pid] : 0; }
	// False for a Regicide hero while RegicideGarrison is off: it can't enter structures,
	// siege engines or ships.
	bool unit_garrisonable(size_t unit_idx) const;
	// 0 A.D. PlayerColor: an index into PLAYER_COLORS; player p defaults to p (player_defaults.json).
	void set_player_color(uint8_t pid, uint8_t idx) { if (pid < MAX_PLAYERS && idx < PLAYER_COLOR_COUNT) player_color_[pid] = idx; }
	uint8_t player_color_index(uint8_t pid) const { return pid < MAX_PLAYERS ? player_color_[pid] : 0; }
	// 0xRRGGBB; Gaia (and anything outside the 8 slots) is 0 A.D.'s white.
	uint32_t player_color(uint8_t pid) const;
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
	uint32_t trigger_hash() const;
	void trigger_event(uint8_t event, const TriggerEventData &data = {});
	// Player.js SetState / Diplomacy.js SetDiplomacyIndex: the masks and stances change and
	// OnPlayerDefeated, OnPlayerWon and OnDiplomacyChanged fire for each change.
	void mark_players_defeated(uint8_t mask);
	// How often each event was called since the start or the last load: for tests and
	// debugging only, never saved or hashed.
	uint32_t trigger_event_calls(uint8_t event) const { return event < TRIGGER_EVENT_COUNT ? trigger_event_calls_[event] : 0; }
	// GuiInterface PushNotification {"type": "dialog"}: a dialog a trigger script asks the
	// GUI to show (triggers_demo's yes-no). Handed to the GUI once, never saved, as 0 A.D.'s
	// notifications live in the GUI.
	struct TriggerDialog {
		uint8_t players = 0xFF; // bit p: player p + 1 sees it
		std::string caption, button1, tooltip1, button2, tooltip2;
	};
	std::vector<TriggerDialog> take_trigger_dialogs() {
		std::vector<TriggerDialog> out;
		out.swap(trigger_dialogs_);
		return out;
	}
	void mark_players_won(uint8_t mask);
	void note_diplomacy_changed(uint8_t player, uint8_t other, uint8_t stance);
	// A map's TriggerScripts by name ("polar_sea_triggers", "treasure_islands"): registers
	// the ported script's triggers. Random maps do it in generate_random_map; data/maps
	// JSON maps list theirs under "trigger_scripts". Returns false for an unported script.
	bool register_trigger_script(const std::string &name);
	// CCmpCinemaManager (cinema.h): the camera paths a map has (its <Paths>) and the queue
	// trigger scripts play. While a queue plays the map is revealed and the GUI shows the
	// camera flying the front path instead of the session.
	const CinemaState &cinema() const { return cinema_; }
	bool cinema_playing() const { return cinema_.enabled != 0; }
	void add_cinema_path(const CinemaPath &path); // AddPath: a name already used is ignored
	bool cinema_has_path(const char *name) const { return cinema_.find(name) >= 0; }
	void cinema_queue_path(const char *name);     // AddCinemaPathToQueue
	void cinema_play() { set_cinema_enabled(true); }
	void cinema_stop() { set_cinema_enabled(false); }
	void cinema_clear_queue() { cinema_.queue.clear(); }
	void cinema_delete_path(const char *name);
	// TriggerPoint.js: a trigger point the map places (trigger/trigger_point_<ref>).
	void explore_circle(uint8_t player, float x, float z, float range_units); // a VisionRevealer's area, explored at the start
	void add_trigger_point(uint8_t ref, float x, float z, uint8_t owner = GAIA_PLAYER) { triggers_.add_point(ref, x, z, owner); }
	// TriggerHelper.SetPlayerWon: pid and its allies win, everyone else is defeated.
	void set_player_won(uint8_t pid, uint8_t reason = SessionNotice::END_GAME_MODE);
	// TriggerHelper.DefeatPlayer (cmpPlayer.SetState("defeated")): pid's units and
	// structures turn Gaia and it watches the rest of the match.
	// `reason` is the chat line's (DefeatPlayer's message): objective unless the script names one.
	void defeat_player(uint8_t pid, uint8_t reason = SessionNotice::END_OBJECTIVE);
	// 0 A.D. "resign" (Commands.js: cmpPlayer.Defeat("%(player)s has resigned.")): the
	// issuing player is defeated as by conquest. Its units and structures turn Gaia, its
	// allies play on, and it watches the rest of the match. False when it is already out.
	bool cmd_resign(uint8_t pid);
	// Map scripts' SetDisabledTemplates (structures) and SetInvulnerability.
	bool building_disabled_by_trigger(uint8_t player, uint8_t type) const { return triggers_.building_disabled(player, type); }
	bool invulnerable(uint32_t unit_id) const { return !triggers_.invulnerable.empty() && triggers_.is_invulnerable(unit_id); }
	// The map scripts' only invulnerable units are Survival of the Fittest's treasure seekers,
	// placed as "nonbuilder|undeletable|...": no Builder, Identity/Undeletable.
	bool nonbuilder(uint32_t unit_id) const { return invulnerable(unit_id); }
	bool undeletable_unit(uint32_t unit_id) const { return invulnerable(unit_id); }
	// unit_can_build (Builder) for unit i, less a nonbuilder.
	bool unit_builds(size_t i) const { return unit_can_build(units_.unit_type[i], units_.tmpl[i]) && !nonbuilder(units_.id[i]); }
	bool has_map_triggers() const { return !triggers_.triggers.empty(); }
	// Commands.js ProcessCommand's OnPlayerCommand (kind: TriggerCommand). target and other
	// are the command's targets (see TriggerCommand), hunt 1 when target is an animal,
	// count a constructed BuildingType.
	void note_player_command(uint8_t player, uint16_t kind, uint32_t target, uint32_t other, uint8_t hunt, uint16_t count);
	// The running tutorial (maps/scripts/Tutorial.js): which one (TutorialId), its current
	// goal (tutorial_goal(), the last one when finished), whether the goal waits for the
	// Ready button (NET_CMD_TUTORIAL_READY) and the WarningMessage shown (TutorialWarning).
	uint8_t tutorial_id() const { return (uint8_t)triggers_.vars[7]; }
	int tutorial_goal_index() const { return triggers_.vars[0]; }
	bool tutorial_ready_button() const { return triggers_.vars[1] != 0; }
	uint8_t tutorial_warning() const { return (uint8_t)triggers_.vars[6]; }
	// The player the tutorial teaches (playerID 1) and its enemy (2).
	static constexpr uint8_t TUTORIAL_PLAYER = 0, TUTORIAL_ENEMY = 1;
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
	float game_time() const { return (float)ticks_ * FIXED_STEP; } // seconds since the match started
	float aim_clock() const { return aim_clock_; } // sim seconds since the last aim snapshot
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

	bool cmd_research(uint8_t player, uint32_t building_id, uint8_t tech_id, bool push_front = false);
	// The unit orders given while this is set go first (UnitAI PushOrderFront): the current
	// order and the queue resume after. Set by the command dispatch around one command.
	void set_push_front_order(bool on) { push_front_order_ = on; }
	// 0 A.D. Upgrade.js: turn a finished structure into building_upgrade()'s
	// target in place, keeping its health fraction, capture share and garrison.
	enum UpgradeCheck : uint8_t {
		UPGRADE_OK = 0, UPGRADE_ERR_NONE, UPGRADE_ERR_OWNER, UPGRADE_ERR_BUSY, UPGRADE_ERR_PHASE, UPGRADE_ERR_COST,
		UPGRADE_ERR_LIMIT // the target's EntityLimits category is full
	};
	// `choice`: which of building_upgrade()'s choices (the Tachara has four).
	// The civ whose Upgrade choices a structure offers: its own template's (Upgrade.js reads
	// the entity's template, so a captured Tachara keeps its four forms), else its owner's.
	uint8_t structure_upgrade_civ(const Building &b) const { return structure_template_civ(b); }
	// The civ of the template a structure was placed as (a captured house keeps its builder's
	// Population/Bonus and BuildTime), else its owner's.
	uint8_t structure_template_civ(const Building &b) const {
		return b.civ < CIV_COUNT && b.civ != CIV_GAIA ? b.civ : (uint8_t)civ_of(b.team);
	}
	// Upgrade.js DetermineUpgrades: that template's choices, its "{civ}" targets resolved
	// with the owner's civ (the ones the owner's civ lacks dropped).
	BuildingUpgradeInfo structure_upgrade(const Building &b, int choice) const {
		return building_upgrade(b.type, structure_upgrade_civ(b), choice, structure_owner_civ(b));
	}
	int structure_upgrade_count(const Building &b) const {
		return building_upgrade_count(b.type, structure_upgrade_civ(b), structure_owner_civ(b));
	}
	uint8_t structure_owner_civ(const Building &b) const {
		return b.team < MAX_PLAYERS ? (uint8_t)civ_of(b.team) : (uint8_t)CIV_GAIA;
	}
	UpgradeCheck upgrade_check(uint8_t player_id, uint32_t building_id, int choice = 0) const;
	bool cmd_upgrade_building(uint8_t player_id, uint32_t building_id, int choice = 0);
	// Stops an upgrade in progress and refunds what was paid (Upgrade.js CancelUpgrade).
	bool cmd_cancel_upgrade(uint8_t player_id, uint32_t building_id);
	// 0 A.D. Upgrade.js on units (civ_unit_upgrade_table): the Roman elite infantry's
	// Centurion rank, the Immortals' spear-and-bow switch. The owner pays up front; the
	// unit keeps fighting while the timer runs, then becomes the `to` roster row in
	// place (health fraction, xp and orders kept). Death refunds the cost.
	struct UnitUpgrade {
		uint32_t unit_id;
		uint16_t to; // civ_unit_template_table() row
		uint8_t player; // who paid (refunded on cancel or death)
		uint8_t pad = 0;
		float elapsed;
		float time;
		float paid[4]; // food, wood, gold, stone
	};
	static_assert(sizeof(UnitUpgrade) == 32, "UnitUpgrade is saved raw: keep it padding-free");
	// The table row that upgrades unit index i to `to`, or nullptr.
	const CivUnitUpgrade *unit_upgrade_choice(size_t i, uint16_t to) const;
	UpgradeCheck unit_upgrade_check(uint8_t player_id, uint32_t unit_id, uint16_t to) const;
	// Starts every listed unit's upgrade to `to` that unit_upgrade_check allows, one
	// payment each (Commands.js "upgrade"); true if any started.
	bool cmd_upgrade_units(uint8_t player_id, const uint32_t *unit_ids, size_t n, uint16_t to);
	// Commands.js "cancel-upgrade": stops the listed units' upgrades and refunds them.
	bool cmd_cancel_unit_upgrades(uint8_t player_id, const uint32_t *unit_ids, size_t n);
	// The upgrade a unit is going through, or nullptr.
	const UnitUpgrade *unit_upgrade_of(uint32_t unit_id) const;
	// 0 A.D. Gate.js LockGate / UnlockGate: the owner keeps its own units out too.
	bool cmd_lock_gate(uint8_t player_id, uint32_t building_id, bool lock);
	// Maximum hit points of a `type` structure owned by `team`: template x civ bonus x techs.
	float structure_max_hp(uint8_t type, uint8_t team) const;
	float structure_hp_mult(uint8_t type, uint8_t team) const; // owner civ bonus x technologies on Health/Max
	void rescale_structure_hp(Building &b, uint8_t from);
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
		int market1 = 0; // the starting market's international bonus (paid to the arrival market's owner)
		int market2 = 0; // the arrival market's international bonus (paid to the starting market's owner)
	};
	// `trader_type` UNIT_MERCHANT_SHIP takes the civ's merchant ship bonus instead of the land trader's.
	static constexpr double MERCHANT_SHIP_GARRISON_GAIN = 0.2; // template_unit_ship_merchant Trader/GarrisonGainMultiplier
	// The gain with a merchant ship's garrisoned traders' bonus (Trader.js CalculateGain).
	TradeGain garrisoned_trade_gain(size_t trader_idx, TradeGain g) const;
	// 0 A.D. Trader.js:303 OnGarrisonedUnitsChanged: a merchant ship working a route
	// re-works its leg's gain when traders board or leave it mid-voyage.
	void refresh_ship_trade_gain(uint32_t transport_id);
	TradeGain calculate_trade_gain(const Building &from, const Building &to, uint8_t trader_owner,
			uint8_t trader_type = UNIT_TRADER) const;
	// The trader's gain between two points (dx, dz) world units apart (calculate_trade_gain's `trader`).
	int trader_gain_at(double dx, double dz, uint8_t trader_owner, uint8_t trader_type = UNIT_TRADER) const;
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
	// The same for any passability class; building_region is that cell's region (0 = none).
	bool nearest_pass_cell(const Building &b, uint8_t pass, float &out_x, float &out_z, uint16_t *out_region = nullptr) const;
	uint16_t building_region(const Building &b, uint8_t pass) const;
	// 0 A.D. Footprint.PickSpawnPoint: a free spot for a unit of `type` in rows around the
	// footprint, the front (or the side facing the rally point) first; skip_uid is the unit left out
	// of the overlap check (the one being placed).
	bool pick_spawn_point(const Building &b, uint8_t type, uint32_t skip_uid, float &out_x, float &out_z) const;
	void order_trade(const uint32_t *unit_ids, size_t count, uint32_t market_a_id, uint32_t market_b_id,
			bool queued = false, const std::vector<std::pair<float, float>> &route = {});
	void order_trade_auto(const uint32_t *unit_ids, size_t count); // farthest pair of tradable markets
	// Read-only: the trader's route (Trader.js markets) and which market it heads to
	// next (0 = market_b, 1 = market_a). False when it has no route.
	bool trader_route(uint32_t unit_id, uint32_t &market_a, uint32_t &market_b, int &leg) const {
		auto it = trader_routes_.find(unit_id);
		if (it == trader_routes_.end()) return false;
		market_a = it->second.first;
		market_b = it->second.second;
		auto lit = trader_leg_.find(unit_id);
		leg = lit == trader_leg_.end() ? 0 : lit->second;
		return true;
	}
	// Trader.js goods.amount: the gains of the goods a trader carries, worked out when they were
	// loaded. False while it carries none.
	bool trader_goods(uint32_t unit_id, TradeGain &out) const {
		auto it = trader_goods_.find(unit_id);
		if (it == trader_goods_.end()) return false;
		const int32_t u = lookup_unit(unit_id);
		if (u < 0 || units_.carry[u] <= 0) return false;
		out = it->second;
		return true;
	}
	// P6 trade-route waypoints (0 A.D. UnitAI.js SetupTradeRoute route, Trader.js TRADING
	// waypoints): the clicked points between market A and market B, in A-to-B order. A leg
	// toward B walks them in order, a leg toward A reversed. Empty: the straight line as before.
	static constexpr size_t TRADE_ROUTE_MAX_POINTS = 64;
	bool trader_route_points(uint32_t unit_id, std::vector<std::pair<float, float>> &out) const {
		auto it = trader_route_points_.find(unit_id);
		if (it == trader_route_points_.end() || it->second.empty()) return false;
		out = it->second;
		return true;
	}
	// Waypoints of the current leg already walked past (MoveToMarket popping this.waypoints).
	int trader_route_idx(uint32_t unit_id) const {
		auto it = trader_route_idx_.find(unit_id);
		return it == trader_route_idx_.end() ? 0 : it->second;
	}
	// Lockstep hash inputs (TurnManager::compute_sim_hash): every trader's markets, leg,
	// loaded goods, route waypoints and leg progress, in no particular map order.
	const std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> &trader_routes_map() const { return trader_routes_; }
	const std::unordered_map<uint32_t, int> &trader_legs() const { return trader_leg_; }
	const std::unordered_map<uint32_t, TradeGain> &trader_goods_map() const { return trader_goods_; }
	const std::unordered_map<uint32_t, std::vector<std::pair<float, float>>> &trader_route_points_map() const {
		return trader_route_points_;
	}
	const std::unordered_map<uint32_t, int> &trader_route_idx_map() const { return trader_route_idx_; }
	float get_market_buy_price(ResourceType t) const { return market_buy_price(t); }
	float get_market_sell_price(ResourceType t) const { return market_sell_price(t); }
	int get_market_history(ResourceType t) const { return market_trade_history_[t]; }
	float barter_price_diff(uint8_t res) const { return res < RES_COUNT ? barter_price_diff_[res] : 0.0f; }
	float trader_gains(const Building *a, const Building *b) const; // legacy helper
	float trader_gains(Building *a, Building *b) { return trader_gains((const Building*)a,(const Building*)b); }

	bool save_state(const char *filepath) const;
	bool load_state(const char *filepath);

	bool can_place(int cell_x, int cell_y, int footprint) const;
	// 0 A.D. BuildRestrictions.js CheckPlacement for `player` placing `b_type` at cell_x/cell_y, turned
	// by `angle` (a dock by dock_angle): obstruction, terrain (land, shore, land-shore), territory, dock
	// shoreline and distance rules.
	enum PlacementResult : uint8_t {
		PLACE_OK = 0,
		PLACE_OBSTRUCTED, // off the map, on another building or on a resource
		PLACE_TERRAIN,    // water under a land building, deep water under a wall, a slope too steep
		PLACE_TERRITORY,  // territory the template does not allow
		PLACE_SHORE,      // a dock not on a shoreline facing a sea
		PLACE_DISTANCE,   // too close to the player's own building of the same class
		PLACE_UNEXPLORED, // land the (human) player has never explored
	};
	bool placement_unexplored(uint8_t player, float x, float z) const;
	PlacementResult check_placement(uint8_t player, int cell_x, int cell_y, uint8_t b_type,
			float angle = DEFAULT_BUILDING_ROT) const;
	// What BuildRestrictions.CheckPlacement's GUI message names for `player` placing `b_type`
	// with its centre at (x, z): the territories the template allows and the one found there
	// (bits 1 own, 2 ally, 4 neutral, 8 enemy; `connected` false where an own or allied tile
	// is cut off from its roots), and its Distance FromClass (0.28's class name, or null)
	// with MinDistance in metres.
	struct PlacementDetails {
		uint8_t territory_allowed = 0, territory_found = 0;
		bool connected = true;
		const char *distance_class = nullptr;
		float min_distance_m = 0.0f;
	};
	PlacementDetails placement_details(uint8_t player, uint8_t b_type, float x, float z) const;

	// 0 A.D. WallSet walls (helpers/Walls.js GetWallPlacement, Commands.js "construct-wall").
	struct WallPiece {
		uint8_t type;   // BLD_WALL_TOWER or BLD_WALL_SEGMENT
		float x, z;     // centre
		float rot;      // Position angle: a segment's length lies along its u axis
		float half_w;   // half its length (a segment) or its half size (a tower)
		uint8_t civ = CIV_COUNT; // the template civ whose WallSet it is from (CIV_COUNT: the generic one)
	};
	// The pieces of a `kind` (WallSetKind) wall from (sx, sz) to (ex, ez), in order along it: a
	// tower at each end, none where that end snaps onto one of `player`'s towers of that WallSet
	// (its id goes in *start_tower or *end_tower, else 0), and segments joined by towers. Empty
	// when the ends are too close.
	std::vector<WallPiece> wall_placement(uint8_t player, float sx, float sz, float ex, float ez,
			uint32_t *start_tower = nullptr, uint32_t *end_tower = nullptr, uint8_t kind = WALLSET_STONE) const;
	// The tower of `player`'s `kind` WallSet that a wall end at (x, z) snaps onto (input.js snaps
	// to towers under the cursor), or 0.
	uint32_t wall_snap_tower(uint8_t player, float x, float z, uint8_t kind = WALLSET_STONE) const;
	// check_placement for one wall piece, with the buildings in `ignore` (the towers it joins and
	// the pieces between them) left out of the obstruction test, as 0 A.D. obstruction control
	// groups do.
	PlacementResult check_wall_piece(uint8_t player, const WallPiece &p, const uint32_t *ignore = nullptr,
			size_t ignore_n = 0) const;
	// Commands.js TryConstructWall: lays the pieces of the wall from (sx, sz) to (ex, ez) as
	// foundations in order, paying for each, and stops at the first that is blocked or unaffordable;
	// `builders` build them one after another. Returns the number of pieces laid.
	size_t cmd_build_wall(uint8_t player, float sx, float sz, float ex, float ez, const uint32_t *builders = nullptr,
			size_t builder_n = 0, bool queued = false, uint8_t kind = WALLSET_STONE);
	// Lockstep cmd NET_CMD_BUILD_WALL carries a wall's start point in target_id: x and z in
	// 1/32 world units from -1024, 16 bits each.
	static uint32_t pack_wall_point(float x, float z);
	static void unpack_wall_point(uint32_t packed, float &x, float &z);
	// The GRID cells a building takes in occupied_cells_: every cell its turned rectangle overlaps
	// (for an unturned building on the grid, its footprint's square of cells).
	std::vector<std::pair<int, int>> building_cells(const Building &b) const;

	void tick(float dt);

	// Views
	struct UnitView {
		const float *pos_x, *pos_z;
		const float *vel_x, *vel_z;
		const float *prev_x, *prev_z; // previous-turn positions (CCmpPosition m_PrevX/m_PrevZ)
		const float *target_x, *target_z;
		const float *hp, *max_hp;
		const uint32_t *id;
		const uint8_t *alive, *team, *unit_type, *action_state, *res_type_carried, *stance, *formation;
		const uint32_t *target_resource_id;
		const int32_t *carry;
		const float *death_timer;
		const float *speed, *attack_damage; // current walk speed and damage (owner civ bonuses applied)
		const uint16_t *kills;
		const uint8_t *rank;
		const uint8_t *status_effects;
		const uint16_t *xp;
		const uint16_t *tmpl; // civ roster template (civ_unit_template), CIV_UNIT_TEMPLATE_NONE = generic
		const uint8_t *pack_state; // PackState: siege engines pack to move and unpack to fire
		const float *pack_elapsed; // seconds into the current pack/unpack
		const float *heading_x, *heading_z; // the way each unit faces (UnitSoA heading)
		size_t count;
	};
	UnitView units() const;

	struct ResourceView {
		const float *x, *z;
		const int32_t *amount;
		const uint8_t *type, *subtype, *alive;
		const uint8_t *flora; // FloraKind
		const uint32_t *id;
		size_t count;
		const uint8_t *fauna; // a carcass's animal: fauna type + 1, 0 none
		const uint8_t *owner; // a carcass's owner (the animal's), else GAIA_PLAYER or 0xFF
	};
	ResourceView resources() const;

	const std::vector<Building> &buildings() const { return buildings_; }
	float building_vision_range(const Building &b) const; // 0 when b gives no vision
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
	// 0 A.D. settings/starting_resources.json: Very Low, Low (default), Medium, High,
	// Very High, Deathmatch; every resource starts at the same amount.
	static constexpr int STARTING_RESOURCE_PRESETS[] = {100, 300, 500, 1000, 3000, 50000};
	static constexpr int STARTING_RESOURCE_PRESET_COUNT = 6;
	static constexpr int STARTING_RESOURCE_PRESET_DEFAULT = 1;
	void set_starting_resources_preset(int preset) {
		if (preset < 0 || preset >= STARTING_RESOURCE_PRESET_COUNT) preset = STARTING_RESOURCE_PRESET_DEFAULT;
		const int r = STARTING_RESOURCE_PRESETS[preset];
		set_starting_resources(r, r, r, r);
	}
	// Game Setup teams (0 A.D. Diplomacy ChangeTeam): setup[p] is 0 for "None" or
	// team 1-4. A player with no team stays alone (team = its own id); a team's id is
	// its lowest member, so it can never match a teamless player's id.
	void set_setup_teams(const uint8_t setup[MAX_PLAYERS]) {
		for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
			uint8_t id = p;
			if (setup[p] != 0)
				for (uint8_t q = 0; q < p; ++q)
					if (setup[q] == setup[p]) { id = q; break; }
			set_player_team(p, id);
		}
	}

	float food_rate(uint8_t pid = 0) const { return player(pid).gathered_window[RES_FOOD] * 6.0f; } // per min approx
	float wood_rate(uint8_t pid = 0) const { return player(pid).gathered_window[RES_WOOD] * 6.0f; }
	float gold_rate(uint8_t pid = 0) const { return player(pid).gathered_window[RES_GOLD] * 6.0f; }
	float stone_rate(uint8_t pid = 0) const { return player(pid).gathered_window[RES_STONE] * 6.0f; }

	int order_pop(const ProductionOrder &o, uint8_t player) const;
	// Player.IsTrainingBlocked: a batch at the head of one of the player's queues waits
	// for population room (Trainer.js Item.Start, Player.BlockTraining).
	bool training_blocked(uint8_t player) const;
	// `bld_civ`: the structure's template civ (structure_template_civ); 0xFF for the player's own.
	int structure_pop_bonus(uint8_t type, uint8_t player, uint8_t bld_civ = 0xFF) const;
	// GarrisonHolder/BuffHeal of a structure type for a player: its template's plus garrison_heal.
	float structure_garrison_heal_for(uint8_t type, uint8_t player) const {
		return building_garrison_heal(type) + (player < MAX_PLAYERS && type < BLD_TYPE_COUNT ? tech_mods_[player].bld_garrison_heal[type] : 0.0f);
	}
	// What `player`'s technologies and bonuses do to a roster template's stats, as 0.28's
	// GetTemplateData gives the detailed training tooltip: Health and Speed multipliers,
	// MaxRange x range_mul + range_add metres, resistance levels added, each damage type's mul/add.
	struct TemplateStatMods {
		float health = 1.0f, speed = 1.0f, range_mul = 1.0f, range_add = 0.0f;
		float armor[3] = {}, atk_mul[4] = {1.0f, 1.0f, 1.0f, 1.0f}, atk_add[4] = {};
	};
	TemplateStatMods template_stat_mods(uint8_t player, uint16_t tmpl) const;
	// A structure of `type` as `player`'s build button shows it (0.28 GetTemplateData): its
	// cost after civ, team and technology modifiers, and its BuildTime for one builder.
	void structure_cost_for(uint8_t player, uint8_t type, float out[RES_COUNT]) const { structure_cost(player, type, -1.0f, out); }
	// A WallSet piece of half length `half_w` (its civ's set): the price and build time `player`
	// pays, as cmd_build_wall charges it and the foundation takes.
	float wall_piece_price(uint8_t player, uint8_t type, float half_w, float out[RES_COUNT]) const;
	// What cmd_build_wall pays for a planned WallSet piece (its length's own cost).
	void wall_piece_cost(uint8_t player, const WallPiece &p, float out[RES_COUNT]) const {
		structure_cost(player, p.type, wallset_piece_value(p.type, p.half_w, 1, p.civ), out);
	}
	float structure_build_time_for(uint8_t player, uint8_t type) const;
	int pop_current(uint8_t player = 0) const;
	int pop_max(uint8_t player = 0) const;
	// Houses and other population bonuses (plus the hive master cheat), before the population limit.
	int pop_max_uncapped(uint8_t player) const;
	// Recounts unit_pop_bonus_ (the units' Population/Bonus per player): at the end of every
	// tick and after a load or reset, so a unit's bonus counts from the tick after it appears.
	void refresh_unit_pop_bonus();
	// The population limit after Player/MaxPopulation modifiers; 0 when unlimited.
	int pop_limit(uint8_t player) const;
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
	// A corral animal's Identity/Requirements: fauna_pig_trainable needs phase_town and
	// template_unit_fauna_herd_domestic_cattle (cow, sanga, zebu) phase_city.
	static uint8_t livestock_required_phase(uint8_t animal) {
		if (animal == FAUNA_PIG_TRAINABLE) return 2;
		if (animal == FAUNA_CATTLE_COW_TRAINABLE || animal == FAUNA_CATTLE_SANGA_TRAINABLE || animal == FAUNA_CATTLE_ZEBU_TRAINABLE) return 3;
		return 1;
	}
	void set_player_civ(uint8_t civ) { set_team_civ(0, civ); }
	uint8_t player_civ(uint8_t pid = 0) const { return player(pid).civ; }
	// A field's MaxGatherers, from the civ it was placed as (its owner's when that is unset).
	int field_cap(const Building &b) const { return field_max_gatherers(b.civ != CIV_COUNT ? b.civ : player(b.team).civ); }
	// A civ chosen after the player's starting structures were placed re-templates them too.
	void set_team_civ(uint8_t team, uint8_t civ);
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
		TECH_ERR_PHASE, TECH_ERR_PREREQUISITE, TECH_ERR_STRUCTURES, TECH_ERR_BUILDING, TECH_ERR_COST,
		TECH_ERR_PAIRED, // its pair_*.json partner is researched or being researched
		TECH_ERR_DISABLED // the map's trigger script disabled it (cmpPlayer.SetDisabledTechnologies)
	};
	ResearchCheck research_check(uint8_t player_id, uint8_t tech_id, uint32_t building_id = 0) const;
	// What researching `tech_id` costs this player at a researcher of `bld_type` (BLD_TYPE_COUNT:
	// a unit): Researcher/TechCostMultiplier, team bonuses and the Library of Alexandria, as paid.
	void tech_price(uint8_t player_id, uint8_t tech_id, uint8_t bld_type, float paid[RES_COUNT], float &time) const;
	// 0 A.D. Heal/Range and Heal/Interval after the player's technologies.
	float healer_range(uint8_t player_id) const;
	float healer_rate(uint8_t player_id) const; // hit points per second
	// 0 A.D. unlock_champion_* / unlock_civilians_house_*: a unit the owner's
	// civ gates behind an unlock technology can only be trained once it is in.
	// With `tmpl` (a roster row) the row's own 0 A.D. unlock tech decides.
	uint8_t train_unlock_tech(uint8_t player_id, uint8_t unit_type, uint8_t bld_type,
			uint16_t tmpl = CIV_UNIT_TEMPLATE_NONE) const;
	// 0 A.D. EntityLimits (entity_limits_data.h): a player's entities of a limit
	// category, alive or queued, foundations included; its limit with the
	// LimitChangers it owns, or -1 while a LimitRemover lifts it (or there is none).
	int entity_limit_count(uint8_t player_id, uint8_t category) const;
	int entity_limit(uint8_t player_id, uint8_t category) const;
	// AllowedToTrain / AllowedToBuild: room for `count` more of the unit (roster
	// row `tmpl`, else the class) or of the structure.
	// TrainingRestrictions MatchLimit: heroes train once a match (match_trained_ plus queues).
	bool match_limit_allows(uint8_t player_id, uint16_t tmpl, int count = 1) const;
	static constexpr int HERO_MATCH_LIMIT = 1;
	std::array<std::vector<uint16_t>, MAX_PLAYERS> match_trained_; // hero templates each player trained (saved v42)
	// Petra's army soldiers (0 A.D. units trained without metadata role "worker"): the citizen
	// infantry an AI player trained outside its civic centres, sorted ids (saved v52). Every
	// other citizen soldier of an AI is a worker (trainMoreWorkers, the starting units).
	std::array<std::vector<uint32_t>, MAX_PLAYERS> petra_army_ids_;
	// Petra's units with metadata plan -1, sorted ids (saved v106): every unit an attack or
	// raid squad held (attackPlan.js removeUnit) and the emergency units (trainEmergencyUnits).
	// 0.28 never clears it, so trainMoreWorkers doesn't count them as workers again.
	std::array<std::vector<uint32_t>, MAX_PLAYERS> petra_out_of_plan_;
	bool petra_is_worker(size_t i) const;
	int petra_target_workers() const;
	int petra_work_phase3() const;
	int petra_pop_phase2() const; // config.js Economy.popPhase2 after setConfig's population scaling
	int petra_config_pop_phase2() const; // the same before startingStrategy.js's saveResources cut
	int petra_accounted_population() const;
	int petra_pop_for_barracks1() const;
	int petra_pop_for_barracks2() const;
	void petra_build_defenses();
	void petra_check_resource_levels(int32_t turn);
	// baseManager.js findBestDropsiteLocation for res; no_site: no spot reached min_quality.
	// fund_above: past this quality, set the dropsites account to the storehouse's cost first
	// (startingStrategy.js buildFirstBase setAccounts); negative: never.
	void petra_build_farmstead();
	bool entity_limit_allows_unit(uint8_t player_id, uint8_t unit_type, uint16_t tmpl, int count = 1) const;
	bool entity_limit_allows_building(uint8_t player_id, uint8_t bld_type) const;
	bool can_train_at(uint8_t player_id, uint8_t unit_type, uint8_t bld_type,
			uint16_t tmpl = CIV_UNIT_TEMPLATE_NONE) const;
	// 0 A.D. phase_town / phase_city requirement: finished Village (1) or Town (2) class structures.
	int phase_structures(uint8_t player_id, uint8_t phase) const;
	// UnitAI.IsGuardOf: the unit's current order is to guard someone (remove-guard button).
	// Also true while it does something else and will go back to guarding (isGuardOf is kept).
	bool unit_is_guarding(uint32_t uid) const { return unit_guard_target(uid) != 0; }
	// UnitAI.isGuardOf: the entity the unit guards (a unit or structure id), or 0.
	uint32_t unit_guard_target(uint32_t uid) const;
	bool unit_has_work_order(uint32_t uid) const { int32_t idx = lookup_unit(uid); return idx>=0 && (size_t)idx < units_.count && units_.work_kind[idx] != ORDER_NONE; } // UnitAI hasWorkOrders
	bool unit_has_order(uint32_t uid) const { int32_t idx = lookup_unit(uid); return idx>=0 && (size_t)idx < units_.count && units_.has_order[idx]; }
	const Grid<uint8_t>& territory_grid() const { return territory_grid_; }
	// ICmpTerritoryManager::IsTerritoryBlinking for territory_grid() cell (tx, tz): owned land in a
	// region holding none of its owner's territory roots. For the renderer's border lines.
	bool territory_blinking_cell(int tx, int tz) const;
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
	// chose. Teams win over stances; without Lock Teams a player who stops allying
	// a teammate leaves the team (cmd_set_diplomacy), so stances decide from then on.
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
	// Petra's gameState.getPopulationMax(): the population cap after its modifiers (300 by
	// default; 10000 when unlimited), not the housing-limited pop_max.
	int petra_population_max() const;


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
		// baseManager.js neededDefenders: emergency units still to train for a civic centre we
		// built or captured (in what was padding, so saves keep their layout).
		uint8_t needed_defenders = 0;
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
		uint8_t kind = 1; // PetraAttackKind of the plan (PETRA_ATTACK_ATTACK until a plan is made)

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
		uint8_t target_player = 0xFF; // attackPlan.js targetPlayer (0xFF until a target is chosen)
		bool forced = false;    // forceStart: march with what it has, take any target
		bool requested = false; // started at an ally's request: asks no one to join
		// attackPlan.js "completing": the army walks to its rally point (staging_x/z, at our
		// border) and sets off once none of it has a forced order left, or after
		// completing_time seconds (maxCompletingTime).
		bool completing = false;
		float completing_time = 0.0f;
		// attackPlan.js path: the march's waypoints still ahead, the next one first.
		std::vector<float> path_x, path_z;
		float last_x = 0.0f, last_z = 0.0f; // lastPosition: the army's centre on the last turn
		// position5TurnsAgo: the army's centre at the last turn that was a multiple of 5 (far off
		// until the march's first such turn, as 0.28's [0, 0] map corner).
		float pos5_x = 1e6f, pos5_z = 1e6f;
	};
	const AttackPlan &petra_attack_plan() const { return petra_attack_plan_; }
	const std::vector<AttackPlan> &petra_started_attacks() const { return petra_started_attacks_; }
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
	const std::vector<PetraDefenseArmy> &petra_defense_armies() const { return petra_defense_armies_; }
	bool petra_is_defender(uint32_t uid) const;
	const PetraQueueManager &petra_queues() const { return petra_queues_; }
	const std::vector<PetraTransportPlan> &petra_transport_plans() const { return petra_transports_; }
	const std::vector<PetraChatLine> &pending_petra_chat() const { return petra_chat_; }
	// Whether `viewer` reads the line: its addressee, the speaker and its allies for "/allies", or anyone.
	bool petra_chat_reaches(const PetraChatLine &l, uint8_t viewer) const {
		if (l.to == PETRA_CHAT_EVERYONE) return true;
		if (l.to == PETRA_CHAT_ALLIES) return viewer < MAX_PLAYERS && (viewer == l.from || is_ally(l.from, viewer));
		return l.to == viewer;
	}
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
		const uint8_t *owner; // GAIA_PLAYER, or the player whose corral trained it
	};
	FaunaView fauna() const;

	uint32_t spawn_fauna(float x, float z, uint8_t ftype, uint8_t owner = GAIA_PLAYER);
	uint8_t fauna_owner(uint32_t fauna_id) const {
		auto it = fauna_id_to_idx_.find(fauna_id);
		return it == fauna_id_to_idx_.end() ? GAIA_PLAYER : fauna_owner_[it->second];
	}
	void order_hunt(const uint32_t *unit_ids, size_t count, uint32_t fauna_id);
	uint32_t find_nearby_prey(size_t i, float x, float z, float max_d2, uint8_t fauna = 0, uint32_t exclude = 0) const;
	bool is_fauna_id(uint32_t id) const { return fauna_id_to_idx_.count(id) != 0; }

	// A Capture the Relic catafalque (template_unit_catafalque.xml). id is the catafalque
	// unit's; x, z and the owner follow the unit each tick.
	struct Relic {
		uint32_t id;
		float x, z;
		uint8_t civ;            // whose catafalque
		int capture_cp;         // the owner's Capturable points x100 (0..RELIC_CAPTURE_POINTS*100)
		bool claimed;           // owned by a player, not Gaia
		uint8_t owner_team = 255;
		std::array<float, MAX_PLAYERS + 1> cp{}; // Capturable points per player, then Gaia's
	};

	// Treasure.js / TreasureCollector.js: a Gaia treasure any land unit can be ordered to
	// collect; after CollectTime in range its owner gets the treasure's resources.
	struct Treasure {
		uint32_t id;
		float x, z;
		float collect_progress;   // the longest collector timer (collector_id), each kept in its pack_elapsed
		uint32_t collector_id;
		uint8_t type;             // TreasureType (gaia_data.h)
		uint8_t pad[3];
	};
	uint32_t spawn_treasure(float x, float z, uint8_t type);
	// A treasure's facing (radians), fixed by its id; the world view draws it so and its
	// Obstruction/Static rectangle blocks foundations along it.
	static float treasure_angle(uint32_t id) { return (float)(id % 12) * 0.52f; }
	void order_collect_treasure(const uint32_t *unit_ids, size_t count, uint32_t treasure_id, bool queued = false);
	// TreasureCollector MaxDistance of unit i in world units (edge to edge), or a
	// negative value when its template has no TreasureCollector (dogs, siege, catafalques).
	float treasure_reach(size_t i) const;
	bool treasure_within(size_t i, const Treasure &t, float reach) const; // edge to edge
	// Whether unit i stands within treasure_reach of treasure `id` it collects, where tick_treasures runs its timer.
	bool treasure_in_reach(size_t i, uint32_t id) const;
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
		uint8_t owner;        // Ownership: a player, or GAIA_PLAYER (0xFE)
		uint8_t pad[2];
		std::array<float, MAX_PLAYERS + 1> cp; // Capturable points per player, then Gaia's
	};
	uint32_t spawn_scenery(float x, float z, uint8_t scenery_type, float angle, float scale);
	// The scenery_ index of entity `id`, or -1.
	int32_t scenery_index(uint32_t id) const {
		for (size_t k = 0; k < scenery_.size(); ++k) if (scenery_[k].id == id) return (int32_t)k;
		return -1;
	}
	bool is_scenery_id(uint32_t id) const { return scenery_index(id) >= 0; }
	// Attack a Gaia scenery structure (a fence, column, obelisk; template_structure):
	// the units walk up to it and strike it until it falls (tick_scenery_attackers).
	// Capturable scenery (benches, tables, obelisks, wooden fences) is captured instead, as a
	// structure is, unless `allow_capture` is false (Ctrl: session.attack).
	void order_attack_scenery(const uint32_t *unit_ids, size_t count, uint32_t scenery_id, bool allow_capture = true);
	bool scenery_capturable(const Scenery &sc) const { return scenery_type_info(sc.type).capture_points > 0; }
	// unit_actions.js "attack" and "capture" on a structure: may one of unit_ids damage it (Attack.js
	// CanAttack "!Capture": an enemy's, with hitpoints, outside the attack's RestrictedClasses) or take
	// its capture points (CanAttack "Capture": a Capture attack and Capturable.CanCapture)?
	bool any_can_damage_building(const uint32_t *unit_ids, size_t count, uint32_t building_id) const;
	bool any_can_capture_building(const uint32_t *unit_ids, size_t count, uint32_t building_id) const;
	// unit_actions.js "heal" firstAbleEntity: the first of unit_ids that heals and may heal the unit
	// target_id (Heal.js CanHeal: an injured own or allied Human, not itself), else 0.
	uint32_t first_able_healer(const uint32_t *unit_ids, size_t count, uint32_t target_id) const;
	// unit_actions.js "guard": may one of unit_ids guard the unit or structure target_id (UnitAI
	// CanGuard, not itself, its own player's or an ally's)?
	bool any_can_guard(const uint32_t *unit_ids, size_t count, uint32_t target_id) const;
	bool unit_can_guard(size_t i) const;
	void tick_scenery_capture(); // Capturable.js TimerTick: territory decay, then regeneration
	void scenery_check_owner(Scenery &sc); // RegisterCapturePointsChanged
	float unit_damage_to_scenery(size_t i) const;
	void tick_scenery_attackers();
	void destroy_scenery(size_t k, int32_t killer);
	// The grain supply of a finished field (0 if none): a Gaia field is farmed through it.
	uint32_t field_resource_id(uint32_t building_id) const {
		auto it = field_res_by_bld_.find(building_id);
		return it != field_res_by_bld_.end() && res_id_to_idx_.count(it->second) ? it->second : 0;
	}
	bool is_resource_id(uint32_t id) const { return res_id_to_idx_.count(id) != 0; }
	// Whether any of these units can gather this supply (unit_actions "gather" getActionInfo).
	bool any_can_gather(const uint32_t *unit_ids, size_t count, uint32_t resource_id) const {
		auto rit = res_id_to_idx_.find(resource_id);
		if (rit == res_id_to_idx_.end()) return false;
		for (size_t k = 0; k < count; ++k) {
			const int32_t u = lookup_unit(unit_ids[k]);
			if (u >= 0 && units_.alive[u] && can_gather((size_t)u, rit->second)) return true;
		}
		return false;
	}
	const std::vector<Scenery> &scenery() const { return scenery_; }
	// A Gaia-owned unit from a 0 A.D. map (Player 0): roster template `template_id`
	// ("gaul/infantry_javelineer_a"; a rank suffix falls back to the _b template), else the
	// u_type class. rank 2/3 = Advanced/Elite, reached through Promotion xp on the next tick.
	uint32_t spawn_gaia_unit(float x, float z, UnitType u_type, const char *template_id, int rank = 1);
	uint32_t spawn_map_unit(float x, float z, uint8_t team, UnitType u_type, const char *template_id, int rank = 0);
	static uint16_t resolve_map_template(const char *template_id, int *rank);
	// Puts a unit inside a structure of its owner at once, as a map's <Garrison> does.
	bool garrison_unit_now(uint32_t unit_id, uint32_t building_id);
	// A map piece whose template disables TerritoryDecay (palisades_spike_*): it never decays.
	void set_structure_no_decay(uint32_t building_id) {
		auto it = building_id_to_idx_.find(building_id);
		if (it == building_id_to_idx_.end()) return;
		buildings_[it->second].decay_disabled = true;
		buildings_[it->second].territory_decay_rate = 0.0f;
	}
	// An "uncapturable|" template (elephantine.js's Kushite towers): no capture points at all.
	void set_structure_uncapturable(uint32_t building_id) {
		auto it = building_id_to_idx_.find(building_id);
		if (it == building_id_to_idx_.end()) return;
		Building &b = buildings_[it->second];
		b.max_capture_points = 0.0f;
		b.capture_points = 0.0f;
		b.capture_points_by_team.fill(0.0f);
	}
	// A map script's Gaia wall (placeCustomFortress): the `kind` WallSet's pieces from (sx, sz)
	// to (ex, ez), built and uncapturable, joined to Gaia towers already at the ends. Pieces
	// that would stand in water are left out.
	void spawn_gaia_wall(float sx, float sz, float ex, float ez, uint8_t kind);
	// A map script's Gaia gate of the `kind` WallSet (its 0 A.D. WallPiece length: kush wall_gate
	// 36 m, palisades_gate 14 m) at (x, z) across a wall running at rot (WallPiece::rot),
	// built and uncapturable.
	void spawn_gaia_gate(float x, float z, float rot, uint8_t kind);
	// A ported map's WallSet piece (tools/port_0ad_maps.py wall_piece): `player`'s built `kind`
	// (WallSetKind) wall, tower or gate (MapWallPiece) at (x, z), turned to its 0 A.D. Position
	// `angle`; a wall or gate is `length` long. Gaia's can't be captured. Returns its id.
	enum MapWallPiece : uint8_t { MAP_WALL_SEGMENT, MAP_WALL_TOWER, MAP_WALL_GATE };
	uint32_t spawn_map_wall_piece(uint8_t player, uint8_t kind, uint8_t piece, float x, float z, float angle, float length);
	// rmgen-common player.js placeStartingWalls, the Iberian "Starting Walls" civ bonus: on a map
	// over 128 tiles across (`map_tiles`), an Iberian player's base at (x, z) gets built stone
	// walls, either a ring of long walls, gates and towers (STARTING_WALLS_FORTRESS,
	// placeGenericFortress at radius 20, its shape drawn from `seed`) or only seven towers
	// (STARTING_WALLS_TOWERS, placePolygonalWall at radius 15 facing `orientation`). Other civs
	// and smaller maps get none. Returns the number of pieces placed.
	enum StartingWalls : uint8_t { STARTING_WALLS_NONE, STARTING_WALLS_FORTRESS, STARTING_WALLS_TOWERS };
	size_t place_starting_walls(uint8_t player, float x, float z, int map_tiles, uint8_t walls, float orientation,
			uint32_t seed);
	// wall_builder.js placeGenericFortress: `player`'s built stone walls round (x, z) at about
	// `radius`, long walls between towers with every `gate_every`-th wall a gate, the outline's
	// corners pushed in or out by up to `irregularity` of a wall's length (the best closing of
	// `max_tries` tries). Pieces in water or off the map are left out.
	// With a `style` (WallStyle) the pieces are that style's, laid as place_wall lays them; without
	// one, the generic stone set's, as the Iberian starting walls have always had them.
	size_t place_generic_fortress(uint8_t player, float x, float z, float radius, uint32_t seed,
			float irregularity = 0.5f, int gate_every = 3, int max_tries = 100, uint8_t style = WALL_STYLE_COUNT);
	// wall_builder.js placeCustomFortress (getWallAlignment with the stone WallSet): `player`'s
	// built closed wall of `n` elements laid one after the next round (x, z), each a long wall, a
	// gate, a wall tower, a cornerIn (a tower turned 45 degrees where the wall turns a quarter
	// left) or a structure set into the wall (FORT_BUILDING: 3.5 towers inside the wall line).
	// The outline is centred on (x, z) and turned so the first element faces the world angle
	// `facing` from it. Pieces off the map, in water, or where `skip` says are left out.
	enum FortressElement : uint8_t { FORT_LONG, FORT_GATE, FORT_TOWER, FORT_CORNER_IN, FORT_BUILDING };
	struct FortressPart {
		uint8_t element;  // FortressElement
		uint8_t building; // the BuildingType of a FORT_BUILDING
	};
	size_t place_custom_fortress(uint8_t player, float x, float z, float facing, const FortressPart *wall, size_t n,
			bool (*skip)(float x, float z, const void *ctx) = nullptr, const void *ctx = nullptr);
	// The rest of wall_builder.js (wall_builder.h has g_WallStyles and getWallElement). Positions are
	// RTS0 world coordinates (rmgen's tile coordinates less half the map's size), orientations and
	// angles rmgen's (wall_builder.h). Like placeEntityPassable they leave out what stands outside
	// the map's passable area (MAP_BORDER_WIDTH); each returns the number of entities placed.
	// placeWall: the elements one after the next from (x, z), the first facing `orientation`.
	size_t place_wall(double x, double z, const std::vector<WallElementName> &wall, uint8_t style, uint8_t player,
			double orientation = 0.0);
	// placeCustomFortress: a closed wall round (x, z), its first element (the main gate) facing
	// `orientation` from the centre.
	size_t place_wall_fortress(double x, double z, const std::vector<WallElementName> &wall, uint8_t style, uint8_t player,
			double orientation = 0.0);
	// placeFortress: one of createDefaultFortressTypes' (wall_fortress_type), "medium" if `type` is none.
	size_t place_fortress(double x, double z, const char *type, uint8_t style, uint8_t player, double orientation = 0.0);
	// placeLinearWall: `part` again and again from (sx, sz) to (ex, ez), stretched to fit, and the
	// first element again at the end if `end_with_first`.
	size_t place_linear_wall(double sx, double sz, double ex, double ez, const std::vector<WallElementName> &part, uint8_t style,
			uint8_t player, bool end_with_first = true);
	// placeCircularWall: `part` round (cx, cz) at `radius` over `max_angle` (a full circle, else an
	// arc whose opening faces `orientation`); end_with_first -1 is 0.28's default (true for an arc).
	size_t place_circular_wall(double cx, double cz, double radius, const std::vector<WallElementName> &part, uint8_t style,
			uint8_t player, double orientation = 0.0, double max_angle = 2.0 * kWallPi<double>, int end_with_first = -1);
	// placePolygonalWall: `corner` elements at the corners of a regular polygon round (cx, cz), linear
	// walls of `part` between them, the first side left open if `skip_first_wall`.
	size_t place_polygonal_wall(double cx, double cz, double radius, const std::vector<WallElementName> &part,
			const WallElementName &corner, uint8_t style, uint8_t player, double orientation = 0.0, int num_corners = 8,
			bool skip_first_wall = true);
	// placeIrregularPolygonalWall with its default wallPartsAssortment: the corners `irregularity`
	// apart at random (from `seed`), each side the assortment's part that fits it best.
	size_t place_irregular_polygonal_wall(double cx, double cz, double radius, const WallElementName &corner, uint8_t style,
			uint8_t player, double orientation, int num_corners, double irregularity, bool skip_first_wall, uint32_t seed);
	// placeEntityPassable for a wall_builder element at (x, z), turned to the rmgen `angle`: false,
	// placing nothing, where it has no template or stands outside the passable map.
	bool place_wall_element(const WallElement &e, double x, double z, double angle, uint8_t player);
	// rmgen's inMapBounds and validTilePassable (MAP_BORDER_WIDTH 3 tiles) at world (x, z).
	bool wall_builder_passable(double x, double z) const;
	// Removes the trees, mines and bushes within r of (x, z): a map script placing a structure
	// where RTS0's own scatter has put wood.
	void clear_resources_near(float x, float z, float r);
	// template_gaia_ruins: a stone (or metal) supply gathered 4x as fast as a rock.
	uint32_t spawn_ruins(float x, float z, uint8_t ruins_type);

	float wonder_time_remaining() const { return std::max(0.0f, wonder_duration_ - wonder_victory_timer_); }
	// Seconds player p has held a finished wonder (0 when it holds none), for WonderVictory.js's countdowns.
	float wonder_hold(uint8_t p) const { return p < MAX_PLAYERS ? wonder_hold_[p] : 0.0f; }
	bool is_wonder_active() const { return has_active_wonder_; }
	const std::vector<Relic> &relics() const { return relics_; }

	std::vector<uint32_t> get_idle_workers(uint8_t team = 0) const;
	std::vector<uint32_t> petra_idle_builders() const;

	size_t unit_count() const { return units_.count; }
	size_t building_count() const { return buildings_.size(); }
	size_t resource_count() const { return resources_.count; }

	struct Stats { uint64_t tick_us; size_t ticks; };
	Stats stats() const { return {tick_us_, ticks_}; }
	// Wall-clock cost of the map-size-sensitive subsystems (tools/bench_map_scale.cpp).
	// Diagnostic only: never read by the simulation, so lockstep is unaffected.
	struct MapScaleStats {
		uint64_t fow_us = 0, fow_calls = 0;
		uint64_t los_us = 0, los_calls = 0;
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
	// Each unit's own step this tick (arena memory), for motion_feedback().
	struct MoveSteps {
		const float *from_x = nullptr, *from_z = nullptr; // where it stood before move_units()
		const float *want_x = nullptr, *want_z = nullptr; // the step it tried to take
		const uint8_t *blocked = nullptr; // a closed gate or a unit standing still stopped the step (CheckMovement failed)
		const uint8_t *moving = nullptr;  // WALK_* flags: WALK_MOVING is 0 A.D. MotionState::isMoving
		size_t count = 0;
	};
	enum : uint8_t { WALK_MOVING = 1, WALK_STEER = 2 };
	void move_units(float dt, Arena &arena, MoveSteps &steps);
	// move_units' second pass: a walker's step that runs into a unit standing still stops, and the
	// walker takes a short path round the idle units (0 A.D. CheckMovement and HandleObstructedMove).
	void avoid_idle_units(float dt, Arena &arena, const uint8_t *walk, const float *aim_x, const float *aim_z,
			float *npx, float *npz, float *want_x, float *want_z, uint8_t *blocked);
	// A walker at (x, z) already within reach of the task it walks for (a resource it gathers, a
	// foundation it builds, a unit it attacks, a structure it garrisons in, a relic it captures):
	// 0 A.D.'s move to the target's range is done there.
	bool walk_in_reach(size_t i, float x, float z) const;
	// tick_gather's reach of unit i at (x, z) to resource r: 2.5 units from its centre (FISH_GATHER_REACH
	// for fish), and for a mine, which no unit stands on, MINE_GATHER_REACH from its square.
	bool gather_in_reach(size_t i, size_t r, float x, float z) const;
	bool gathering_mine(size_t i) const; // unit i is gathering from a mine (mine_footprint)
	bool gather_mine_footprint(size_t i, nav::Footprint &m) const; // and that mine's footprint
	// Unit i's control group (0 A.D. ControlGroup) for avoid_idle_units and separation: its formation,
	// or the mine it works (0x80000000 | the mine's id) while it gathers from or returns from one.
	uint32_t control_group(size_t i) const;
	// The key of a short path kept round idle units (ShortPathMemo): a short path among this tick's
	// buildings (short_path_key_) that also went round units.
	uint32_t idle_path_key() const { const uint32_t k = short_path_key_ ^ 0x5BD1E995u; return k ? k : 1u; }
	// The key of a short path round idle units whose waypoint the unit reached last tick: it searches
	// on from that corner, as 0 A.D. follows the whole short path.
	uint32_t idle_corner_key() const { const uint32_t k = short_path_key_ ^ 0x2F6B3A51u; return k ? k : 2u; }
	// The key of a search round idle units that found no way from where the unit stands (the memo
	// holds that spot): it searches again there only every fourth turn.
	uint32_t idle_stuck_key() const { const uint32_t k = short_path_key_ ^ 0x6C8E9CF5u; return k ? k : 3u; }
	void separation(float dt, Arena &arena, const MoveSteps &steps);
	void motion_feedback(float dt, const MoveSteps &steps);
	void patrol_arrive(size_t i);
	void patrol_next_leg(size_t i);
	// A chase (UnitAI COMBAT.APPROACHING / CHASING): the target (the unit, or the centre of the
	// structure) and the point the unit walks to (the unit, or tick_combat's stand point beside the
	// structure). False when unit i chases no unit or structure.
	bool chase_goal(size_t i, float &key_x, float &key_z, float &to_x, float &to_z) const;
	bool chase_path_kept(size_t i) const;
	bool chase_falls_short(size_t i) const;
	// The same for a hunter: its path to the animal ends out of reach of where it was asked to go.
	bool hunt_falls_short(size_t i) const;
	// MovementUpdate likelyFailure for a hunter (GATHER APPROACHING / COMBAT): FINDINGNEWTARGET.
	void give_up_hunt(size_t i);
	bool walk_falls_short(size_t i) const;
	// Unit i follows a long path that obstructed_long_path works on: the one move_units or cmd_move
	// gave it, with waypoints left, more than LONG_PATH_MIN_DIST from where it is going.
	bool follows_long_path(size_t i) const;
	void obstructed_long_path(size_t i, uint8_t failed);
	void abandon_chase(size_t i);
	void give_up_attack(size_t i);
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
		// The unit's damage total after technologies over its template's (attack_damage's scale);
		// each damage type is its own value, x atk_mul + atk_add (Attack/<type>/Damage/<kind>).
		float attack[UNIT_TYPE_COUNT];
		float atk_mul[UNIT_TYPE_COUNT][4], atk_add[UNIT_TYPE_COUNT][4]; // hack, pierce, crush, fire
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
		float range_mul[UNIT_TYPE_COUNT];   // Attack/Ranged/MaxRange multiplier (range adds after it)
		float vision[UNIT_TYPE_COUNT];      // Vision/Range multiplier
		float vision_add[UNIT_TYPE_COUNT];  // ... then added metres (Healing Range)
		float spread[UNIT_TYPE_COUNT];      // Attack/Ranged/Projectile/Spread multiplier
		float capture[UNIT_TYPE_COUNT];     // Attack/Capture/Capture multiplier
		float required_xp[UNIT_TYPE_COUNT]; // Promotion/RequiredXp multiplier
		float bld_health[BLD_TYPE_COUNT];
		float bld_attack[BLD_TYPE_COUNT];
		float bld_range[BLD_TYPE_COUNT];
		float bld_build_time[BLD_TYPE_COUNT];
		float bld_research_time[BLD_TYPE_COUNT];
		float bld_batch_time_add[BLD_TYPE_COUNT]; // added to Trainer/BatchTimeModifier (Conscription)
		float bld_pop_bonus[BLD_TYPE_COUNT];
		float bld_pop_bonus_add[BLD_TYPE_COUNT]; // added after the multiplier (Wagon Trains: Colony +10)
		float bld_arrow_mul[BLD_TYPE_COUNT];
		float bld_arrow_add[BLD_TYPE_COUNT];
		float bld_garrison_heal[BLD_TYPE_COUNT]; // added hp per second for garrisoned units
		float bld_capture[BLD_TYPE_COUNT];        // Capturable/CapturePoints multiplier
		float bld_garrison_regen_add[BLD_TYPE_COUNT]; // added to Capturable/GarrisonRegenRate (the phases)
		float bld_decay[BLD_TYPE_COUNT];              // TerritoryDecay/DecayRate multiplier (Migratory Resettlement)
		float bld_cost[BLD_TYPE_COUNT][RES_COUNT]; // Cost/Resources multipliers
		float bld_international_mul[BLD_TYPE_COUNT]; // Market/InternationalBonus (Silk Road)
		float bld_international_add[BLD_TYPE_COUNT]; // ... added before it (Commercial Treaty)
		float bld_territory[BLD_TYPE_COUNT];       // TerritoryInfluence/Radius multiplier
		float bld_tech_cost[BLD_TYPE_COUNT][RES_COUNT]; // Researcher/TechCostMultiplier of what it researches
		float bld_vision[BLD_TYPE_COUNT];          // Vision/Range multiplier (Carrier Pigeons)
		float bld_vision_add[BLD_TYPE_COUNT];      // ... then added metres (Arrow Shooters)
		float bld_garrison_arrows[BLD_TYPE_COUNT]; // BuildingAI/GarrisonArrowMultiplier multiplier (Crenellations)
		float bld_min_range[BLD_TYPE_COUNT];       // Attack/Ranged/MinRange replaced (Murder Holes), < 0 unchanged
		// Player-wide modifiers from global relic auras.
		float loot_add[RES_COUNT];    // Looter/Resource/<res> added to every kill's loot
		float trickle_add[RES_COUNT]; // ResourceTrickle/Rates/<res> of the player, per second
		float max_pop;                // Player/MaxPopulation multiplier
		// The same unit multipliers per civ roster template, matched against the
		// template's own 0 A.D. classes (Champion Infantry Spearman, Javelineer, ...),
		// which its RTS0 unit_type only approximates.
		struct Template {
			float gather[GK_COUNT];
			float attack, health, speed, range, train_time;
			float atk_mul[4], atk_add[4]; // hack, pierce, crush, fire
			float range_mul, vision, vision_add, spread, capture, required_xp;
			float armor[3];
			float cost[RES_COUNT];
			float cost_add[RES_COUNT]; // Cost/Resources added after the multipliers (Nastasen's Gift of Warships)
			float loot_mul[RES_COUNT], loot_add[RES_COUNT]; // the unit's own Loot/<res> (Celtic Auxiliaries)
			int16_t carry[RES_COUNT]; // ResourceGatherer/Capacities added (Wicker Baskets: Worker, infantry too)
		} tmpl[CIV_UNIT_TEMPLATE_COUNT];
	};
	void compute_tech_mods(uint8_t player_id, TechMods &out) const;
	void rebuild_all_tech_mods();
	// Recompute a player's modifiers and rescale its existing units and structures
	// (0 A.D. ModificationsManager: a modifier changes every entity it affects).
	void refresh_tech_mods(uint8_t player_id);
	// Bit g set: relic_global_auras()[g] reaches `player` (a relic it or a partner owns).
	uint64_t relic_aura_mask(uint8_t player) const;
	void global_aura_masks(uint64_t masks[MAX_PLAYERS]) const;
	void tick_global_auras();
	std::array<uint64_t, MAX_PLAYERS> relic_aura_masks_{}; // derived, as last applied to tech_mods_
	void complete_research(uint8_t player_id, uint8_t tech_id);
	void apply_tech_mods_to_unit(size_t idx);
	const TechMods &tech_mods(uint8_t player_id) const { return tech_mods_[player_id < MAX_PLAYERS ? player_id : 0]; }
	// A unit's technology multipliers: its roster template's when it has one, else its unit_type's.
	struct UnitTechMods { float attack, health, speed, range, train_time, range_mul, vision, vision_add, spread, capture, required_xp; const float *armor, *gather, *atk_mul, *atk_add; };
	static UnitTechMods unit_tech_mods(const TechMods &m, uint8_t unit_type, uint16_t tmpl);
	UnitTechMods unit_tech_mods(uint8_t player_id, uint8_t unit_type, uint16_t tmpl) const {
		return unit_tech_mods(tech_mods(player_id), unit_type, tmpl);
	}
	int32_t carry_cap(size_t unit_idx, uint8_t res) const;
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
	// Commands.js "attack" through GetFormationUnitAIs: units in formations go to their
	// controller (Order.Attack); the ids returned are the ones to order one by one.
	std::vector<uint32_t> formation_take_attack(const std::vector<uint32_t> &ids, uint32_t target_unit,
			uint32_t target_building, bool allow_capture);
	std::vector<uint32_t> formation_take_patrol(const std::vector<uint32_t> &ids, float tx, float tz); // Order.Patrol
	bool formation_target_in_range(const FormationController &fc) const; // CheckFormationTargetAttackRange
	bool formation_target_pos(const FormationController &fc, float &x, float &z) const;
	void formation_members_attack(size_t ci);   // CallMemberFunction("Attack"/"Gather"/"Repair"/...)
	// Commands.js "gather"/"repair"/"garrison"/"heal" through GetFormationUnitAIs: whole
	// formations in ids walk up as one (APPROACH), then their members do the work (MEMBER).
	std::vector<uint32_t> formation_take_task(const std::vector<uint32_t> &ids, uint8_t task, uint32_t target);
	// Controllers whose every member is in ids (a player ordering the whole formation).
	std::vector<uint32_t> formation_whole_ids(const uint32_t *ids, size_t count) const;
	void formation_apply_order(const std::vector<uint32_t> &members, const UnitOrder &o); // FinishOrder's next order
	bool formation_acting_ = false; // the members are being handed the controller's order
	// A member at work on an order of its own (gathering, building, garrisoning, healing):
	// UnitAI runs it, and the formation waits for it in MEMBER.
	bool member_working(size_t i) const {
		const uint8_t k = units_.order_kind[i];
		return k == ORDER_GATHER || k == ORDER_BUILD || k == ORDER_GARRISON || k == ORDER_HEAL ||
				k == ORDER_RETURN_RESOURCE;
	}
	void formation_merge_twins();               // Formation.js ShapeUpdate twin merge
	void formation_delete_twins(FormationController &fc); // Formation.js DeleteTwinFormations
	void formation_size(const FormationController &fc, float &width, float &depth) const; // GetSize
	void attack_unit_one_by_one(const uint32_t *ids, size_t count, uint32_t target_unit_id);
	void walk_to_attack_target(size_t i, float tx, float tz, float reach, float wx, float wz, uint32_t follow = 0);
	bool follow_walk_target(size_t i);
	void attack_building_one_by_one(const uint32_t *ids, size_t count, uint32_t target_building_id, bool allow_capture);
	void cmd_move_individual(const uint32_t *unit_ids, size_t count, float tx, float tz, bool spread = true);
	// CCmpPathfinder::DistributeAround: a point round (x, z) for each unit.
	std::vector<std::pair<float, float>> distribute_around(const uint32_t *unit_ids, size_t count, float x, float z) const;
	void tick_promotion(float dt);
	void promote_if_due(size_t i); // Promotion.js IncreaseXp: promote while xp covers RequiredXp
	void promote_unit(size_t i, uint16_t target_tmpl, uint8_t target_rank); // Promotion.js Promote
	void apply_rank_bonus(size_t i, uint8_t from_rank, uint8_t to_rank);
	void tick_capture(float dt);
	// 0 A.D. UnitAI per-unit order logic, before movement: answers attacks (fight back or
	// flee), returns fighters to their order, turns patrols, follows the order queue.
	void tick_unit_ai(float dt);
	void tick_status_effects(float dt);
	// Attack/*/ApplyStatus from an attacker with profile `att` on unit t, structure b or animal f.
	void apply_status(size_t t, const CombatProfile &att, uint32_t src_id = 0, uint8_t src_owner = 0xFF);
	void apply_status_structure(Building &b, const CombatProfile &att, uint32_t src_id, uint8_t src_owner);
	void apply_status_fauna(size_t f, const CombatProfile &att, uint32_t src_id, uint8_t src_owner);
	void add_status(uint8_t target_kind, uint32_t target, const CombatProfile &att, uint32_t src_id, uint8_t src_owner);
	// Structure b takes `dmg` from src_id of player src_team (Health.js TakeDamage): it catches
	// fire below a third of its health and is razed at none. False once it is razed.
	bool structure_takes_damage(Building &b, float dmg, uint32_t src_id, uint8_t src_team);
	static constexpr float ATTACK_DUE = 1.0e-6f; // attack_timer: the hit lands this tick
	void tick_attack_timer(size_t i, float dt);
	bool attack_under_way(size_t i) const;
	void stop_attack(size_t i);
	bool attack_ready(size_t i, bool in_range, float prepare);
	void attack_done(size_t i, bool in_range, float repeat);
	bool range_check_due(size_t i, float dt) const; // Attack.js RepeatRangeCheck fell in this tick
	float unit_attack_damage(size_t i, size_t t); // Attack ApplyStatus (Poisoned, Burning)
	void unit_attack_effects(size_t i, float out[4]) const;
	float unit_repeat_time(size_t i) const; // hack, pierce, crush, fire
	void unit_tech_damage(size_t i, float out[4]) const;
	void tick_projectiles(float dt);
	void tick_construction(float dt);
	// Foundation.js Commit: own and allied units standing on the foundation are told to
	// leave (UnitAI LeaveFoundation); enemies and Gaia keep it from starting. True once
	// the site is clear and committed, after which the foundation blocks movement.
	bool try_commit_foundation(Building &b);
	void foundation_exit_point(const Building &b, float x, float z, float r, float &tx, float &tz) const;
	void leave_foundation(size_t i, const Building &b);
	void tick_repair(float dt);
	static constexpr float REPAIR_TIME_RATIO = 2.0f; // template_structure.xml Repairable/RepairTimeRatio
	static constexpr float WALL_REPAIR_TIME_RATIO = 4.5f; // template_structure_defensive_wall.xml
	static constexpr float UNIT_REPAIR_TIME_RATIO = 4.0f; // template_unit_siege.xml, template_unit_ship.xml
	static float foundation_build_multiplier(int builders);
	float structure_repair_rate(const Building &b) const;
	float builder_rate(size_t unit_idx) const;
	float structure_build_time(const Building &b) const;
	void tick_production(float dt);
	void tick_unit_production(float dt); // units' Trainer queues (unit_queues_)
	void tick_upgrades(float dt);
	void tick_unit_upgrades(float dt);
	std::vector<UnitUpgrade> unit_upgrades_; // in start order (saved v45)
	// Production queues of units that train, by unit id (saved SAVE_VERSION_UNIT_TRAINERS).
	std::map<uint32_t, UnitQueue> unit_queues_;
	// Unloads land siege below SIEGE_EJECT_HEALTH or dead (GarrisonHolder EjectHealth / OnDestroy).
	void tick_siege_garrisons();
	void tick_ship_pickups();
	void tick_ship_pickup(size_t ship);
	bool transport_walkers_none() const;
	bool ship_reachable_by(size_t ship, size_t walker) const;
	bool petra_steers_ship(uint32_t ship_id) const;
	void eject_foreign_passengers(size_t transport); // GarrisonHolder OnDiplomacyChanged
	void kill_passengers_of(size_t holder); // EjectOrKill on a holder killed out of the world
	void drop_from_holder(uint32_t unit_id); // GarrisonHolder OnGlobalOwnershipChanged for one destroyed inside
	int garrison_size(size_t unit) const;                        // Garrisonable.TotalSize
	int occupied_slots(const std::vector<uint32_t> &ids) const;  // GarrisonHolder.OccupiedSlots
	bool garrison_room(const Building &b, size_t unit) const;
	bool corral_has_room(const Building &b) const; // penned animals and garrisoned units under GarrisonHolder/Max
	// The search behind pick_spawn_point and siege_spawn_point: the spots round a footprint centred
	// on (cx, cz) and turned by rot, a square with half extents along_u (the front) and along_v, or a
	// circle of radius circle_r when that is above 0.
	bool pick_spawn_point_box(float cx, float cz, float rot, float along_u, float along_v, float circle_r,
			bool has_rally, float rally_x, float rally_z, uint8_t type, uint32_t skip_uid,
			float &out_x, float &out_z) const;
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
	// unallied_only: those whose owner is not allied with the corral's (GarrisonHolder.OnGlobalOwnershipChanged).
	void eject_corral_animals(const Building &b, bool unallied_only = false);
	void tick_death_damage(float dt);
	// 0 A.D. helpers/Attack.js CauseDamageOverArea for an Attack Splash landing at (ox, oz).
	void cause_damage_over_area(uint8_t splash, uint32_t attacker_id, uint8_t attacker_team,
			float ox, float oz, float dir_x, float dir_z, bool melee = false);
	void tick_gates(float dt);
	void tick_waves(float dt);
	void tick_petra_ai(float dt);
	void tick_petra_ai_player(float dt);
	void tick_fauna(float dt);
	void tick_hunters();                                  // units attacking animals (UnitAI Gather on KillBeforeGather)
	// UnitAI "Attacked" for an animal; the attacker is a unit or a structure (its arrows).
	void fauna_attacked(size_t f, uint32_t attacker_id, bool melee = false);
	// Where the unit or (standing) structure an animal flees from is; false if it is gone.
	bool fauna_threat_pos(uint32_t id, float &x, float &z) const;
	void fauna_start_attack(size_t f, uint32_t target);  // UnitAI COMBAT for an animal (PrepareTime)
	bool slaughters_target(size_t i) const; // unit i's target is a Domestic animal it slaughters
	static constexpr float SLAUGHTER_RANGE = 2.0f * 0.6f; // Attack/Slaughter MaxRange 2 m, melee scale
	static constexpr float SLAUGHTER_PREPARE = 0.9f; // Attack/Slaughter PrepareTime 900 ms
	void fauna_hit_unit(size_t f, size_t u);              // an animal's melee attack lands on unit u
	void fauna_hit_fauna(size_t f, size_t v);             // ... or on a player's livestock v
	bool fauna_can_step(size_t f, float x, float z) const;
	// UnitMotion for animal f heading to (tx, tz): where to steer now. The first waypoint of
	// find_path when the navcell line there is blocked, then the short path round the structures
	// in the way. A function of the map and the two points only, so nothing is kept or saved.
	void fauna_path_aim(size_t f, float tx, float tz, float &ax, float &az, uint32_t ignore_building_id = 0) const;
	void tick_fow();  // updates FoW each sim tick
	void tick_los();  // moves every LOS source that changed since the last tick
	void explore_territories(); // InitGame.js ExploreTerritories, before the first turn
	void mark_buildings_seen(); // Fogging seen from los_ (tick_los) and from cells explored at the start
	void reset_los();
	void rebuild_revealed_fow(); // revealed_fow_: all visible but a circular map's off-world cells
	// The fog a player sees (FOW_* per cell) from its line of sight, los_shared_mask included.
	void fill_fow_from_los(std::vector<uint8_t> &grid, uint8_t player) const;
	void rebuild_nav_grid(); // Phase1: rebuild NavGrid from buildings+water (mirrors CCmpObstructionManager rasterize)
	// The static obstructions of the long paths on the terrain tiles (NavGrid::set_tile_obstructions):
	// with `structures` the structures, scenery and mines; the trees, bushes and treasures when they
	// changed.
	void sync_tile_obstructions(bool structures);
	std::vector<NavGrid::TileObstruction> tile_obs_scratch_;
	bool line_crosses_impassable(float x0, float z0, float x1, float z1, uint8_t ship_pass = 0) const;
	// Deep water within c of (x, z) on both axes: 0 A.D. expands it by a land unit's class clearance.
	bool near_deep_water(float x, float z, float c) const;
	bool near_impassable_terrain(float x, float z, float c) const; // deep water or a steep tile
	// A land unit with clearance c may not step from (x0, z0) to (x1, z1) (move_units, pushes).
	bool land_step_barred(float x0, float z0, float x1, float z1, float c) const;
	// A unit with clearance c may not step past a map's edge (past_map_edge); one already
	// past it may step back in.
	bool edge_step_barred(float x0, float z0, float x1, float z1, float c) const {
		if (!past_map_edge(x1, z1, c)) return false;
		if (circular_map()) return x1 * x1 + z1 * z1 > x0 * x0 + z0 * z0;
		return std::max(std::fabs(x1), std::fabs(z1)) > std::max(std::fabs(x0), std::fabs(z0));
	}
	bool find_path(float sx, float sz, float tx, float tz, WaypointPath &out, uint8_t pass = PASS_INFANTRY) const;
	int territory_owner_at(float x, float z) const; // G2: query territory_grid_
	void rebuild_territory(); // G2: rebuild territory/influence grids from TC/Fortress/Tower
	void rebuild_auras(); // G4: rebuild AuraManager from temples/heroes/carnyx/female citizens
public:
	// Read-only, for the status bars' aura icons.
	const AuraManager &aura_manager() const { return aura_mgr_; }
	float attack_reach(size_t i) const; // Attack MaxRange with MaxRange auras (Boiorix's High Ground)
private:
	void apply_health_auras(); // Health/Max auras: rescale max_hp and hp as units enter or leave them
	float structure_max_capture(const Building &b) const; // Capturable/CapturePoints with its owner's modifiers and auras
	void refresh_capture_max(Building &b); // Capturable.js UpdateCachedValuesAndNotify: rescale every side's points
	float structure_capture_garrison_regen(const Building &b) const; // Capturable/GarrisonRegenRate with modifiers
	void register_capture_aura(uint32_t id, uint8_t team, uint16_t k, float x, float z);
	float unit_base_vision(size_t i) const; // the template's Vision/Range x0.28 (class default if none)
	float unit_vision_at(size_t i) const; // Vision/Range with Vision auras (0 A.D. Vision.js)
	float structure_decay_rate(const Building &b) const; // TerritoryDecay/DecayRate with the owner's techs
	uint16_t aura_affected_players(uint8_t owner, uint8_t players) const;
	void fire_arrow(float sx, float sy, float sz, float tx, float ty, float tz, float damage, uint8_t team, uint32_t target_id, uint8_t effect = 0, uint8_t dmg_type = 1, bool hits_building = false, uint8_t visual = 0, uint32_t source_id = 0, uint8_t splash = 0);
	static float projectile_speed(uint8_t visual);
	const CombatProfile *projectile_profile(uint32_t source_id) const;
	float projectile_speed_of(uint32_t source_id, uint8_t visual) const;
	// Attack.js targetVelocity at fire: the target's average velocity over the last
	// lockstep turn ((position - GetPreviousPosition()) / turnLength, CCmpPosition
	// m_PrevX/m_PrevZ; a 0.2 s turn here, TICKS_PER_TURN FIXED_STEP ticks).
	void aim_velocity_unit(size_t t, float &vx, float &vz) const;
	void aim_velocity_fauna(size_t f, float &vx, float &vz) const;
	int32_t projectile_hit_unit(const Projectile &p) const;
	// helpers/Attack.js GetTotalAttackEffects at impact: the projectile's fire-time
	// strengths against the victim's current Resistance, times the shooter's
	// GetAttackBonus against it (base multipliers; RTS0 models no tech modification
	// of bonus multipliers, dead shooter or not, matching every other attack).
	float projectile_impact_damage(const Projectile &p, const CombatProfile &victim, uint8_t victim_civ, const float res[3]) const;
	int32_t projectile_hit_fauna(const Projectile &p, bool aimed_only = false) const;
	const CombatProfile *projectile_status_profile(const Projectile &p) const;
	static float predict_time_to_target(float sx, float sz, float speed, float tx, float tz, float vx, float vz);
	void projectile_hits_fauna(const Projectile &p, size_t f);
	float unit_damage_to_fauna(size_t i, size_t f) const;
	void fire_at_fauna(size_t i, size_t f);
	void fauna_killed(size_t f, size_t i);
	// The dead animal f's ResourceSupply: food.meat, or food.fish for a whale (fishing ships
	// gather it, and it does not regrow like a shoal). 0 if it carries no food.
	uint32_t spawn_carcass(size_t f);
	void award_fauna_xp(size_t i, size_t f, float damage);
	bool projectile_lands_in(const Projectile &p, const Building &b) const;
	int32_t projectile_hit_structure(const Projectile &p) const;
	void projectile_hits_structure(const Projectile &p, Building &b);
	float projectile_damage_to_unit(const Projectile &p, size_t u);
	float projectile_damage_to_structure(const Projectile &p, const Building &b) const;
	float unit_damage_to_structure(size_t i, const Building &b) const;
	float structure_arrow_damage(const Building &shooter, const float res[3]) const;
	void random_normal_2d(float &x, float &z);
	void mark_building_wreck(Building &b, uint8_t killer = 0xFF); // killer: StatisticsTracker credit
	void record_death(float x, float z, uint8_t team, uint8_t killer_team, uint8_t type, bool is_building);
	void record_unit_death(size_t i, uint8_t killer_team); // record_death for unit i, noting its DeathDamage
	const DeathDamageInfo *unit_death_damage(size_t i) const;
	static uint8_t unit_death_damage_row(uint16_t tmpl); // 1 + its death damage table row, 0 none
	static const DeathDamageInfo *death_damage_info(uint8_t row); // that row's DeathDamage, or nullptr
	float unit_template_regen(size_t i) const;
	// StatisticsTracker hooks. `killer` is the player credited with the kill
	// (out of range or the victim's own owner credits nobody).
	uint32_t stat_unit_classes_of(size_t idx) const;
	void stat_unit_trained(size_t idx);
	void stat_unit_lost(size_t idx, uint8_t killer);
	void stat_building_constructed(const Building &b);
	void stat_building_lost(const Building &b, uint8_t killer);
	void stat_building_captured(const Building &b, uint8_t from);
	void stat_unit_captured(size_t idx, uint8_t from); // unit idx now belongs to its captor
	void stat_gathered(uint8_t player, uint8_t res, int amount, bool vegetarian);
	void tick_statistics();
	uint8_t stat_team_mask(uint8_t player) const;
	float explored_percent(uint8_t players) const;
	std::array<uint8_t, MAX_PLAYERS> territory_percentages() const;
	void bind_unit_index(uint32_t id, size_t idx);
	int32_t lookup_unit(uint32_t id) const;
	// 0 A.D. Loot.js / Looter.js / Promotion.js (see simulation.cpp).
	void gain_xp(size_t idx, float amount);
	void award_damage_xp(uint32_t attacker_id, size_t victim, float damage);
	void award_structure_damage_xp(uint32_t attacker_id, const Building &b, float damage);
	// attacker_id is 0 when no entity is left to name (a fire ship's death damage).
	void note_attacked(uint32_t attacker_id, uint8_t attacker_owner, uint32_t target_id, uint8_t target_owner,
			bool target_is_building, float damage, bool melee = false, bool capture = false);
	double battle_damage_[MAX_PLAYERS] = {};
	std::vector<AttackedEvent> attacked_events_;
	std::vector<AiCommandEvent> ai_command_events_;
	bool record_ai_commands_ = false;
	int ai_command_depth_ = 0;
	std::vector<TriggerEventData> pending_attack_alerts_; // OnAttackDetected, raised at the end of the tick that detected them
	std::vector<FaunaStrikeEvent> fauna_strikes_;
	std::vector<SessionNotice> session_notices_;
	void note_session(const SessionNotice &n) { if (session_notices_.size() < kMaxSessionNotices) session_notices_.push_back(n); }
	void note_text(uint8_t player, uint8_t message, uint32_t entity = 0) {
		SessionNotice n{SessionNotice::TEXT, player, 0, message, {}};
		n.amounts[0] = (int32_t)entity;
		note_session(n);
	}
	void note_phase(uint8_t kind, uint8_t player, uint8_t tech) {
		if (tech == TECH_PHASE_TOWN || tech == TECH_PHASE_CITY) note_session({kind, player, 0, tech, {}});
	}
	void reset_production_queue(Building &b); // ResetQueue: refund a lost or captured structure's queue
	bool looter_present(uint32_t looter_id, bool *infantry) const;
	void collect_loot(uint32_t looter_id, uint8_t looter_player, LootInfo loot, float mult, uint8_t carried_type, int32_t carried);
	void collect_unit_loot(uint32_t looter_id, uint8_t looter_player, size_t victim);
	void collect_building_loot(uint32_t looter_id, uint8_t looter_player, const Building &b);
	void check_victory_conditions(float dt);
	// The match ends with these players as the winners; everyone else has lost.
	void end_match(uint8_t winners, uint8_t reason = SessionNotice::END_LAST_ALIVE);
	void spawn_regicide_heroes();
	// Footprint.PickSpawnPoint round a point: the nearest passable spot no unit stands on.
	void free_spot_near(float px, float pz, uint8_t utype, float &x, float &z) const;

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
		float flee_distance[MAX_FAUNA] = {}; // UnitAI FLEEING distanceToFlee (was the unused fatten timer)
		float home_x[MAX_FAUNA] = {}; // UnitAI heldPosition: where a hold-ground animal returns
		float home_z[MAX_FAUNA] = {};
		// Each animal's position at the end of the previous lockstep turn, for the
		// same last-turn aim velocity as units (CCmpPosition m_PrevX/m_PrevZ). At the
		// end, so saves before v98 are a prefix of the struct.
		float prev_x[MAX_FAUNA] = {};
		float prev_z[MAX_FAUNA] = {};
	} fauna_;
	// Each animal's owner (Ownership): GAIA_PLAYER, or the player whose corral trained it
	// (0 A.D. Trainer spawns gaia/fauna_*_trainable owned by that player). Beside fauna_, not
	// in it, so the saved FaunaStorage layout stays; save v58 adds it.
	uint8_t fauna_owner_[FaunaStorage::MAX_FAUNA] = {};
	// UnitAI MoveRandomly's roamAngle (+-pi/6, 0 until the first roam) and startAngle, beside
	// fauna_ like fauna_owner_; save v108 adds them.
	float fauna_roam_angle_[FaunaStorage::MAX_FAUNA] = {};
	float fauna_start_angle_[FaunaStorage::MAX_FAUNA] = {};
	IdIndex fauna_id_to_idx_;
	std::unordered_map<uint32_t, std::vector<uint32_t>> transport_passengers_;
	// Turret units (unit id -> point height), rebuilt from the holders' garrison lists each
	// tick, and the stance each had before it went up (UnitAI previousStance).
	std::unordered_map<uint32_t, float> turret_height_;
	// The turret_height_ units up on a Mauryan rampart tower: its TurretHolder has no
	// wall_garrisoned aura. Rebuilt with turret_height_.
	std::unordered_set<uint32_t> rampart_turrets_;
	bool beach_goal_ = false; // set by order_walk(..., beach_goal) around its cmd_move
	std::unordered_map<uint32_t, uint8_t> turret_prev_stance_;
	const TurretPointSet *turret_points_of(const Building &b) const;
	int structure_garrison_cap(const Building &b) const;
	void occupy_turret(size_t i, Building &b);
	bool structure_accepts_turret(const Building &b, size_t i) const;
	bool turret_room(const Building &b) const;
	void step_down_turret(Building &b, uint32_t unit_id);
	bool leave_turret(size_t i, const Building &b, bool forced);
	void tick_turrets();
	void builder_stand_point(size_t i, const Building &b, float x, float z, float &tx, float &tz) const;
	std::vector<FormationController> formations_; // in creation order
	uint32_t next_formation_id_ = 1;
	// Controller id per unit index, 0 outside formations: members of one formation share an
	// obstruction group and don't push each other in separation(). Derived from formations_
	// by formation_refresh_groups() whenever membership changes, so it is never saved.
	std::vector<uint32_t> formation_group_;
	// Per unit index, 1 while it walks to its slot with a walking controller: UnitMotion's
	// IsMovingAsFormation, so it runs there. Derived with formation_group_.
	std::vector<uint8_t> formation_slot_walk_;
	void formation_refresh_groups();

	UnitSoA units_;
	ResourceSoA resources_;
	std::vector<Building> buildings_;
	std::vector<Projectile> projectiles_;
	// 0 A.D. StatusEffectsReceiver (Stackability Stack): every Poisoned or Burning hit is its own
	// status with its own timer, credited to whoever applied it. It deals its Damage on the next
	// turn and then every Interval, and ends once Duration has passed (ExecuteEffect), so it
	// ticks floor(Duration / Interval) + 1 times. Saved (SAVE_VERSION_STATUS_STACKS) in order.
	struct StatusInstance {
		uint32_t target = 0; // unit, building or animal id (target_kind)
		uint32_t src = 0;    // the attacker (status.source.entity)
		float damage = 0.0f, interval = 0.0f, duration = 0.0f; // Damage per tick, Interval, Duration (s)
		float elapsed = 0.0f; // _timeElapsed
		float next = 0.0f;    // seconds to the next tick
		uint8_t target_kind = 0; // STATUS_ON_*
		uint8_t kind = 0;        // 1 Poisoned, 2 Burning
		uint8_t src_owner = 0xFF; // status.source.owner
		uint8_t first = 1;        // _firstTime: the first tick adds no Interval
	};
	static constexpr uint32_t MAX_STATUSES = 1u << 16;
	std::vector<StatusInstance> statuses_;
	bool status_tick(const StatusInstance &st); // one ExecuteEffect; false once its target is gone
	// A status keeps its own timer while its unit is garrisoned or aboard (StatusEffectsReceiver).
	bool status_holds_unit(size_t i) const {
		return units_.alive[i] || units_.action_state[i] == ACTION_GARRISONED;
	}
	std::vector<Relic> relics_;
	std::vector<Treasure> treasures_;
	void tick_treasures(float dt);
	void tick_relics();
	float wonder_victory_timer_ = 0.0f;
	bool has_active_wonder_ = false;
	VictoryCondition victory_condition_ = VICTORY_CONQUEST;
	bool waves_enabled_ = false;
	bool last_man_standing_ = false;
	int pop_cap_ = 300; // 0.28 population_capacities.json default
	int32_t player_pop_cap_[MAX_PLAYERS] = {}; // scenario PopulationLimit per player; 0 = pop_cap()
	float ceasefire_seconds_ = 0.0f;
	float relic_victory_timer_ = 0.0f;
	// Derived, not saved: relic_winners() as the end of the last tick saw it (a load rebuilds
	// it), and whether a relic changed hands this tick (tick_relics, read by the victory check).
	uint8_t relic_countdown_players_ = 0;
	bool relic_changed_hands_ = false;
	std::vector<BattleAlert> pending_alerts_;
	float last_alert_time_[6] = {};
	std::unordered_map<uint32_t, size_t> unit_id_to_idx_;
	std::vector<int32_t> unit_idx_by_id_;
	IdIndex res_id_to_idx_;
	std::array<TechMods, MAX_PLAYERS> tech_mods_;
	std::unordered_map<uint32_t, uint32_t> field_res_by_bld_; // field building id -> grain resource id
	std::unordered_map<uint32_t, uint32_t> field_bld_by_res_; // grain resource id -> field building id
	struct FieldGatherers { int total = 0; int slots_taken = 0; };
	std::unordered_map<uint32_t, FieldGatherers> field_gatherers_; // per tick, keyed by grain resource id
	std::unordered_map<uint32_t, FieldGatherers> fish_gatherers_; // per tick, keyed by fish resource id
	// Per tick, every other resource: units ordered to gather it (total) and the ones working it (slots_taken, up to MaxGatherers).
	std::unordered_map<uint32_t, FieldGatherers> supply_gatherers_;
	// v28 saves kept each ruin's MaxGatherers in a list; resources_.max_gatherers holds it since.
	struct RuinsCap { uint32_t res_id; uint16_t max_gatherers; uint16_t pad; };
	std::vector<Scenery> scenery_; // saved
	// Nearest fish on `sea` (ship region) within max_d of (x, z) with a free gatherer slot; 0 if none.
	uint32_t find_free_fish(uint16_t sea, float x, float z, float max_d, uint32_t skip = 0, uint8_t team = 0xFF) const;
	IdIndex building_id_to_idx_;

	// Every standing building's footprint cells (can_place): FULL where a footprint covers the cell,
	// PART where one only reaches into it. A wreck no longer obstructs (0 A.D. rubble has no
	// Obstruction), so wrecking a building marks the set stale, and ensure_occupied_cells rebuilds it.
	mutable CellSet occupied_cells_;
	mutable bool occupied_stale_ = false;
	void ensure_occupied_cells() const;
	void occupy_cells(const Building &b) const;
	// Whether the footprint `f` overlaps a standing building's (CheckFoundation's TestStaticShape):
	// the exact test for a turned footprint where occupied_cells_ marks a cell PART.
	bool footprint_hits_building(const nav::Footprint &f) const;
	// check_placement's cells (GRID-sized, from -map_half_) that a Gaia resource's
	// obstruction touches. A cache of resources_, rebuilt when the tick or the
	// resource count changes; never saved or hashed.
	mutable std::vector<uint8_t> resource_block_cells_;
	mutable uint64_t resource_block_tick_ = UINT64_MAX;
	mutable size_t resource_block_count_ = SIZE_MAX;
	mutable int resource_block_w_ = 0;
	void ensure_resource_block_cells() const;
	// check_placement over the cells `cells` of a footprint whose bounds are [x0, x1] x [z0, z1]
	// and centre (cx, cz); `rect` is a turned footprint (a WallSet piece) or null for a square, and
	// occupied cells in `ignore_cells` don't obstruct it. With `ignore_ids` (a wall piece), other
	// structures obstruct it where their footprints overlap its own, as 0 A.D.'s obstructions do.
	PlacementResult check_placement_cells(uint8_t player, uint8_t b_type, const std::vector<std::pair<int, int>> &cells,
			float x0, float z0, float x1, float z1, float cx, float cz, const Building *rect,
			const std::vector<std::pair<int, int>> *ignore_cells, const std::vector<uint32_t> *ignore_ids = nullptr) const;
	// Whether a wall piece's footprint `f` overlaps a structure, leaving out `ignore` (the towers it
	// joins and the pieces laid since) and, of the segments already joined to one of those towers,
	// the part inside the tower (their shared obstruction control group).
	bool wall_piece_hits_building(const nav::Footprint &f, const std::vector<uint32_t> &ignore) const;
	// survivalofthefittest.js's placement obstructors: whether one covers part of [x0, x1] x [z0, z1].
	bool survival_placement_obstructed(float x0, float z0, float x1, float z1) const;
	// A wall piece laid as a foundation (progress 0) or a finished one, with its WallSet extents.
	uint32_t spawn_wall_piece(const WallPiece &p, uint8_t team, float progress);
	// A map script's built wall piece for `player` (placeGenericFortress, placeCustomFortress): left
	// out off the map or in water; the trees and bushes under it go (DeleteUponConstruction).
	bool place_map_wall_piece(const WallPiece &p, uint8_t player);
	// Cost.js: pays `b_type`'s cost after civ, team and technology bonuses (stone_override, when
	// not negative, in place of its template stone). False, paying nothing, if unaffordable.
	bool pay_for_structure(uint8_t player, uint8_t b_type, float cost_override = -1.0f);
	// `template_civ`: the civ whose template the structure is (a captured one keeps its builder's);
	// CIV_COUNT for the player's own.
	void structure_cost(uint8_t player, uint8_t b_type, float cost_override, float out[RES_COUNT], uint8_t template_civ = CIV_COUNT) const;
	uint32_t stat_unit_value(size_t unit_idx) const;      // StatisticsTracker's entity value (Cost)
	uint32_t stat_building_value(const Building &b) const;
	void refund_foundation(const Building &b);
	void record_foundation_cost(Building &b) const;
	// petra_place_building searches that found no spot, with the signature of what
	// they read (petra_placement_signature). Exact, so derived state: never saved.
	struct PetraPlaceMiss {
		uint64_t sig;
		int cx, cz;
		int16_t min_r, max_r;
		uint8_t pid, btype;
	};
	std::vector<PetraPlaceMiss> petra_place_misses_;
	// headquarters.js requireHouses: a house the phase needs and no room for one, so
	// petra_find_position leaves out every structure's influence for houses (set for one call).
	bool petra_require_houses_ = false;
	bool petra_place_no_room_ = false; // last petra_place_building found no spot (read at once, not saved)
	// queueplanBuilding.js canStart: one foundation per AI turn (HQ.turnCache.buildingBuilt).
	// Both are set only inside one AI's turn (tick_petra_ai), so a save between ticks never
	// holds them (not saved); a call from outside a turn isn't limited.
	bool petra_in_turn_ = false;
	bool petra_turn_built_ = false;
	bool petra_building_built() const { return petra_in_turn_ && petra_turn_built_; }
	uint32_t petra_spawn_foundation(int cx, int cz, uint8_t btype);
	uint64_t petra_placement_signature(uint8_t btype, int start_cx, int start_cz, int max_r) const;

	uint32_t next_id_ = 1;
	uint32_t sim_seed_ = 1337;
	uint32_t ai_seed_ = 0; // set_ai_seed
	SimRng sim_rng_;
	// CCmpAIManager's m_RNG: every AI's draws (Math.random in the AI scripts) come from a stream of
	// their own, seeded from the AI seed (-autostart-aiseed, else the match seed), not the match's.
	SimRng petra_rng_;
	uint32_t petra_rng_seed() const { return ((ai_seed_ ? ai_seed_ : sim_seed_) * 2246822519u) ^ 0x7F4A7C15u; }
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
	float entity_limits_timer_ = 0.0f; // unused since EntityLimits checks creation; kept for the save format
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
	// unit_id -> the goods loaded at the last market (Trader.js goods.amount, fixed at loading).
	std::unordered_map<uint32_t, TradeGain> trader_goods_;
	// unit_id -> waypoints between market A and market B, in A-to-B order, and how many of
	// this leg's are already walked past (saved from SAVE_VERSION_TRADE_ROUTE, in the hash).
	std::unordered_map<uint32_t, std::vector<std::pair<float, float>>> trader_route_points_;
	std::unordered_map<uint32_t, int> trader_route_idx_;

	// Row a = player a's stances; set in reset() (self ally, everyone else enemy).
	uint32_t trigger_event_calls_[TRIGGER_EVENT_COUNT] = {}; // trigger_event_calls
	std::vector<TriggerDialog> trigger_dialogs_; // take_trigger_dialogs
	uint8_t diplomacy_[MAX_PLAYERS][MAX_PLAYERS] = {};
	uint8_t biome_ = BIOME_AEGEAN;
	uint8_t map_preset_ = MAP_AEGEAN_ARCHIPELAGO;

	float wave_timer_ = 0.0f;
	int wave_number_ = 0;
	bool game_over_ = false;
	bool victory_ = false;
	bool reveal_map_ = false;
	int biome_override_ = -1; // setup-time only: applied when a random map is generated
	uint8_t team_placement_ = TEAM_PLACEMENT_RANDOM; // setup-time only, like biome_override_
	uint8_t played_team_placement_ = TEAM_PLACEMENT_RANDOM; // setup-time only, like team_placement_
	uint8_t map_landscape_ = 0;                      // setup-time only, like team_placement_
	uint8_t trigger_difficulty_ = 3;                 // setup-time; the match's copy is triggers_.difficulty
	uint8_t circular_map_choice_ = 0; // setup-time Circular map choice (set_circular_map_choice)
	uint8_t map_daytime_choice_ = 0, map_daytime_ = 0; // setup-time Daytime and the one the map used
	bool nomad_ = false;              // setup-time only: applied when a random map is generated
	bool nomad_start_ = false;        // the map was generated Nomad: tick 0 places the top-up treasures
	bool disable_treasures_ = false;
	void spawn_nomad_treasures();
	// Game Setup switches kept in the byte v32 saves as RegicideGarrison (old saves read as bit 0).
	static constexpr uint8_t SETUP_REGICIDE_GARRISON = 1, SETUP_LOCK_TEAMS = 2, SETUP_CHEATS = 4, SETUP_DISABLE_SPIES = 8,
		SETUP_ALLY_VIEW = 16, SETUP_CIRCULAR_MAP = 32, SETUP_FLAGS_ALL = 63;
	std::vector<Spy> spies_; // VisionSharing spies, oldest first
	CheatMods cheat_mods_[MAX_PLAYERS] = {};
	int32_t unit_pop_bonus_[MAX_PLAYERS] = {}; // Population/Bonus of each player's units (refresh_unit_pop_bonus)
	void tick_spies(float dt);
	void cheat_convert(uint8_t to, const uint32_t *ids, size_t count);
	uint8_t setup_flags_ = 0;
	void set_setup_flag(uint8_t flag, bool on) { setup_flags_ = on ? (setup_flags_ | flag) : (setup_flags_ & ~flag); }
	uint32_t regicide_hero_[MAX_PLAYERS] = {};
	bool regicide_hero_lost(int t) const;
	uint8_t player_color_[MAX_PLAYERS] = {0, 1, 2, 3, 4, 5, 6, 7};
	void remove_player_structures(); // Nomad
	bool explore_map_ = false;
	float wonder_duration_ = 600.0f;
	float relic_duration_ = 180.0f;
	int relic_count_ = 0;
	void apply_relic_count();
	uint8_t water_level_mode_ = WATER_LEVEL_RISING; // saved with the trigger state (v18)
	uint8_t sea_level_rise_min_ = 25;
	TriggerState triggers_;
	CinemaState cinema_; // saved from SAVE_VERSION_CINEMA
	void tick_triggers(float dt);
	void tick_cinema(float dt);
	// Where TriggerHelper.SpawnUnits puts a unit of `type` at trigger point p (false: on the point).
	bool trigger_point_spawn_spot(const TriggerPoint &p, uint8_t type, float &x, float &z) const;
	void cinema_drop_deleted_path(uint16_t idx); // a deleted path no longer queued
	void set_cinema_enabled(bool enabled);
	void register_map_triggers(MapType type);
	void run_trigger_action(uint16_t action, float arg, const TriggerEventData &data);
	// Ported map scripts (simulation.cpp, "Map trigger scripts").
	void trigger_flood_rise_step(float final_level);
	void trigger_volcano_rise_step();
	void trigger_volcano_garrison_towers();
	void trigger_elephantine_init();
	void random_template_composition(const std::vector<uint16_t> &names, int count, std::vector<std::pair<uint16_t, int>> &counts);
	// A TriggerHelper.BalancedTemplateComposition entry: a fixed count or a frequency weight.
	struct TemplateBalance {
		std::vector<uint16_t> templates;
		int count = 0;
		float frequency = 0.0f;
	};
	void balanced_template_composition(const std::vector<TemplateBalance> &groups, int total, std::vector<std::pair<uint16_t, int>> &counts);
	// maps/random/danubius_triggers.js
	TechClasses unit_classes_at(size_t i) const; // the unit's 0 A.D. classes (its roster template's, else its class's)
	uint32_t danubius_spawn(uint16_t key, float x, float z);
	bool danubius_is_left(float x, float z) const;
	void danubius_active_riversides(bool &left, bool &right) const;
	void danubius_composition(float minutes, bool siege, int total, std::vector<std::pair<uint16_t, int>> &counts);
	std::vector<uint32_t> danubius_spawn_counts(const std::vector<std::pair<uint16_t, int>> &counts, float x, float z, float r0, size_t first = 0);
	void danubius_attack_and_patrol(const std::vector<uint32_t> &ids, uint8_t target, uint8_t ref, bool attack);
	void trigger_danubius_init();
	void trigger_danubius_cc_attackers();
	void trigger_danubius_ships();
	void trigger_danubius_fill_ships();
	void trigger_danubius_unload_order();
	void trigger_danubius_ship_range();
	void trigger_danubius_ownership(const TriggerEventData &data);
	// maps/random/jebel_barkal_triggers.js
	std::vector<uint16_t> jebel_barkal_free_heroes() const;
	void trigger_jebel_barkal_init();
	void trigger_jebel_barkal_city_patrol();
	void trigger_jebel_barkal_attack();
	void trigger_jebel_barkal_ownership(const TriggerEventData &data);
	void trigger_polar_sea_wave();
	void trigger_treasure_islands_collected(const TriggerEventData &data);
	void tick_trigger_attack_orders();
	void tick_trigger_watches();
	void tick_trigger_range_queries();
	void trigger_survival_init();
	void trigger_survival_wave();
	void trigger_gallic_fields(bool attack);
	void trigger_pickup_test(int step);
	void trigger_unit_chasing(int step);
	void trigger_unit_chasing_ownership(const TriggerEventData &data);
	void trigger_unit_dancing(int step);
	void trigger_unit_dancing_event(const TriggerEventData &data);
	void trigger_unit_pushing(int step);
	void trigger_unit_motion(int step);
	void trigger_cinema_demo(int step);
	uint32_t script_structure(float mx, float mz, uint8_t type, uint8_t team, float progress);
	uint32_t script_palisade(float mx, float mz, float length_m, float angle);
	uint32_t script_tree(float mx, float mz, int32_t amount = 200);
	// The unit motion test scripts' helpers: 0 A.D. metres from the map corner to world units,
	// QuickSpawn (followed as TriggerWatch role/label unless role is 0xFF), WalkTo, and removing
	// every unit an experiment (role) followed.
	std::pair<float, float> script_metres(float mx, float mz) const;
	uint32_t script_spawn(float mx, float mz, const char *tmpl_id, uint8_t team, uint8_t role = 0xFF, uint8_t label = 0);
	void script_walk(uint32_t id, float mx, float mz, bool queued);
	void script_delete_role(uint8_t role);
	// Their ModifiersManager.AddModifiers (no promotions; the dancing test's 5 m vision and
	// spread off; the pushing test's gather speed), from the script that is running: kept out of the save as they follow from it.
	uint16_t unit_test_script() const;
	void script_modifiers(uint8_t player_id, TechMods &m) const;
	void trigger_survival_treasures();
	void trigger_survival_ownership(const TriggerEventData &data);
	// Tutorial.js NextGoal and the goals of maps/tutorials/*.js (simulation.cpp, "Tutorials").
	void tutorial_next_goal();
	void tutorial_event(const TriggerEventData &data);
	bool tutorial_goal_done(int goal) const;
	void tutorial_goal_init(int goal);
	bool tutorial_goal_listens(int goal) const;
	void tutorial_reset_queue(uint32_t building_id);
	void tutorial_launch_attack();
	void tutorial_remove_champions();
	bool tech_dealt_with(uint8_t player, uint8_t tech) const; // TriggerHelper.HasDealtWithTech
	uint8_t tutorial_resource_kind(uint32_t id, bool animal) const;
	PlayerState gaia_stock_; // scratch for player(GAIA_PLAYER); never saved or hashed
	// Player.js SetState("defeated"): hand every unit and structure of these players to Gaia.
	void convert_to_gaia(uint8_t players);
	void capture_points_tick(Building &b, size_t bi, bool timer_tick); // Capturable.js Reduce, regen and owner change
	using CapturePools = std::array<float, MAX_PLAYERS + 1>; // per player, then Gaia's
	bool capture_enemy(int source, int t) const;
	void capture_reduce(CapturePools &pts, int to, float amount) const; // Capturable.js Reduce
	uint8_t participant_mask_ = 0; // save v10
	uint8_t defeated_mask_ = 0;
	uint8_t winner_mask_ = 0;
	float wonder_hold_[MAX_PLAYERS] = {}; // the longest countdown of each player's wonders (Building::wonder_hold)
	// GetPlayersByDiplomacy("IsExclusiveMutualAlly") under allied victory, else nobody.
	uint8_t wonder_allies_of(uint8_t p) const { return (uint8_t)(side_of(p) & ~(1u << p)); }

	MatchRecord record_;
	// StatisticsTracker state, saved through tick_state_io (save v19+).
	std::array<PlayerStatistics, MAX_PLAYERS> stats_{};
	std::vector<StatisticsSample> stat_samples_;
	static constexpr size_t MAX_STATISTICS_SAMPLES = 4096; // 34 hours of 30 s rows
	// Saves before map exploration was read from the LOS grid (los_) kept their own
	// explored cells here; still in the save layout, no longer filled.
	int32_t stats_explore_res_ = 0;
	std::array<std::vector<uint8_t>, MAX_PLAYERS> stats_explored_;

	uint64_t tick_us_ = 0;
	uint64_t ticks_ = 0;
	// Sim seconds since the last aim snapshot (CCmpPosition m_PrevX/m_PrevZ are a
	// turn old): a lockstep turn here, TICKS_PER_TURN FIXED_STEP ticks, whatever
	// dt the tick ran with. Saved (SAVE_VERSION_PROJECTILE_DAMAGE).
	float aim_clock_ = 0.0f;
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

	AttackPlan petra_attack_plan_; // attackManager.js upcomingAttacks: the plan being prepared
	std::vector<AttackPlan> petra_started_attacks_; // attackManager.js startedAttacks: armies on their way or fighting
	AttackPlan petra_raid_plan_;
	PetraAttackManager petra_attack_mgr_; // attackManager.js plan counters and sizes
	int petra_opening_step_ = 0;
	float petra_opening_timer_ = 0.0f;
	PetraConfig petra_config_;
	std::vector<PetraDefenseArmy> petra_defense_armies_; // defenseManager.js armies
	PetraEmergency petra_emergency_;
	PetraDefenseBuild petra_defense_build_;
	PetraDropsiteCheck petra_dropsite_check_;
	PetraQueueManager petra_queues_;
	std::vector<PetraTransportPlan> petra_transports_; // under way (BOARDING to UNLOADING), several per owner (navalManager.js transportPlans)
	const PetraTransportPlan *petra_transport_for(uint8_t owner) const;
	// navalManager.js requireTransport: the smallest BOARDING plan of `owner` between the same
	// lands with room for one more (at most 3 siege engines per plan), else nullptr.
	PetraTransportPlan *petra_transport_join(uint8_t owner, uint16_t from_land, uint16_t to_land, bool is_siege);
	void petra_require_defense_transport(uint32_t uid, uint16_t from_land, uint16_t to_land, float to_x, float to_z);
	uint32_t petra_defender_dropsite(size_t i) const; // entityExtend.js returnResources: 0 when none
	// entityExtend.js isSiegeUnit: Siege, or Elephant+Melee (war elephants knock down structures).
	bool petra_is_siege_unit(size_t i) const {
		const TechClasses c = unit_classes_at(i);
		return unit_is_siege_engine(units_.unit_type[i]) || ((c & TC_ELEPHANT).any() && (c & TC_MELEE).any());
	}
	void petra_order_build(uint32_t uid, uint32_t bid); // worker.js: drop the load, then build
	void petra_fight_back(size_t i, int32_t attacker, const std::vector<PetraDefenseArmy> &armies, std::vector<uint32_t> &helped);
	bool petra_ship_in_plan(uint32_t ship_id) const;
	size_t petra_go_home(const std::vector<uint32_t> &squad, float x, float z, uint8_t owner, bool order_here, bool attack = false);
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
	// headquarters.js isDangerousLocation: near an army invading us or within `radius` of an
	// enemy shooting structure's range.
	bool petra_dangerous_location(float x, float z, float radius) const;
	void petra_invading_army_centres(std::vector<std::array<float, 2>> &out) const;
	static bool petra_near_invading_army(float x, float z, const std::vector<std::array<float, 2>> &centres);
	// attackPlan.js getPathToTarget and setRallyPoint: the plan's march path from (rx, rz) to
	// its target, and its rally point where that path leaves our territory.
	void petra_plan_path(AttackPlan &plan, float rx, float rz);
	// attackPlan.js StartAttack: the army sets off along its path.
	void petra_start_march(AttackPlan &plan);
	// Whether some unit of the army still walks under a forced order (hasForceOrder).
	bool petra_plan_has_force_order(const AttackPlan &plan) const;
	// must_start: the pick when a plan in preparation sets off (attackPlan.js chooseTarget at
	// mustStart), where an Attack without siege takes only an undefended structure.
	uint32_t find_petra_attack_target(float &out_tx, float &out_tz, uint16_t same_land = 0, const AttackPlan *plan = nullptr, bool must_start = false) const;
	uint8_t petra_enemy_player(uint8_t kind, bool first = true, bool *gaia_relics = nullptr) const;
	uint32_t petra_nearest_target(uint8_t kind, bool forced, uint8_t enemy, float px, float pz, uint16_t same_land, float &out_tx, float &out_tz) const;
	uint32_t find_petra_raid_target(float &out_tx, float &out_tz) const;
	bool petra_defense_target(const Building &b) const; // defenseManager.js targetList
	uint8_t petra_target_owner(uint32_t id) const;
	bool petra_defendable(float x, float z) const; // headquarters.js isDefendable
	void petra_scuttle_undefendable();
	uint32_t petra_help_attack_target(const AttackPlan &plan, uint16_t land, float &tx, float &tz) const;
	uint32_t find_petra_rush_target(float &out_tx, float &out_tz, uint8_t prefer = 0xFF, const float *from = nullptr, uint16_t land = 0) const;
	bool petra_squad_centre(const std::vector<uint32_t> &squad, float &cx, float &cz) const;
	void petra_set_rushes();
	float petra_starting_wood(float x, float z) const;
	void petra_plan_sizes(uint8_t kind);
	void petra_unit_stat_sizes(uint8_t kind, int &target, int &minimum);
	// attackPlan.js unitStat: a plan's classes with their priority, minSize, targetSize,
	// batchSize and interests (the sizes petra_plan_sizes drew), siege engines included
	// (addSiegeUnits); count is left for the caller.
	struct PetraUnitStat {
		TechClasses classes;
		float priority;
		int min, target, batch;
		float strength, siege_strength, stone, metal; // interests; 1 = no costsResource penalty
		int count = 0;
		uint8_t index = 0; // its place in the plan's full list (PetraAttackManager::dropped_classes)
	};
	// The plan's unitStat classes, less those whose bit is set in `dropped`.
	std::vector<PetraUnitStat> petra_plan_unit_stats(uint8_t kind, uint16_t dropped = 0);
	// Whether a building of ours trains some roster template of the class now (trainMoreUnits
	// drops a class without one from the plan).
	bool petra_unit_stat_trainable(const PetraUnitStat &st) const;
	bool petra_raid_target_ok(uint32_t bld_id) const;
	bool petra_allow_capture(const std::vector<uint32_t> &squad, uint32_t uid, const Building &b) const;
	void petra_update_capture_strength();
	void petra_check_captures(const std::vector<uint32_t> &squad, float tx, float tz);
	void petra_answer_attackers(const std::vector<uint32_t> &squad, float tx, float tz, uint8_t avoid = 0);
	uint8_t petra_attack_avoid(const AttackPlan &plan, bool siege) const;
	// attackPlan.js checkTargetObstruction: the enemy structure (a wall, palisade or gate) that
	// blocks the path from (px, pz) to target `id` on the same land, else `id` itself.
	uint32_t petra_target_obstruction(uint32_t id, float px, float pz, float &tx, float &tz) const;
	// attackPlan.js isBlocked: the plan's target is a wall piece that blocked its path.
	bool petra_plan_blocked(const AttackPlan &plan) const;
	bool petra_unit_reserved(uint32_t uid) const;
	std::unordered_set<uint32_t> petra_launched_units() const;
	bool petra_builder_worker(size_t unit_idx, const std::unordered_set<uint32_t> &launched) const;
	bool petra_mustering(uint32_t uid) const;
	void petra_leave_mustering(uint32_t uid);

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
	PetraFailedGather petra_failed_gather_; // headquarters.js lastFailedGather, in petra_timer_ time
	PetraKept petra_kept_; // decided once or only raised (configFirstBase, navalManager.js, targetNumTraders)
	void petra_config_first_base();
	void petra_check_can_build_units(); // startingStrategy.js gameStart canBuildUnits
	void petra_dispatch_units();        // startingStrategy.js dispatchUnits
	bool petra_dispatched_to_ally(uint32_t uid) const; // metadata "allied"
	bool petra_can_build_units() const { return !petra_kept_.no_units; }
	void petra_attack_home(float &x, float &z) const; // the base an attack starts from, else one of our units // startingStrategy.js configFirstBase: needFish, needFarm, wantedFishShips
	// worker.js lastHuntSearch: hunters that found nothing to hunt anywhere, and when (petra_timer_);
	// an idle one searches again only 20 s later. Entries past 20 s are dropped.
	struct PetraHuntSearch { uint32_t id; float time; };
	std::vector<PetraHuntSearch> petra_hunt_search_;
	// worker.js nextMoveToGatherer: when (petra_timer_) each mobile dropsite next looks for a
	// gatherer to walk to; one with no entry looks from 5 s on.
	struct PetraNextMove { uint32_t id; float time; };
	std::vector<PetraNextMove> petra_move_to_gatherer_;
	// worker.js metadata "supply": the supply each gatherer was last sent to or confirmed on
	// (worker.update rechecks a gatherer UnitAI moved to another), by unit id.
	struct PetraSupplyMark { uint32_t unit, supply; };
	std::vector<PetraSupplyMark> petra_supply_; // sorted by unit
	uint32_t petra_supply_of(uint32_t unit) const;
	void petra_set_supply(uint32_t unit, uint32_t supply);
	// buildManager.js unbuildables for reason "room": a structure no spot was found for is not
	// tried again until `until` (petra_timer_), 90 s on; any growth of our territory clears them
	// (headquarters.js:2042). `tiles`: our territory when last looked at.
	struct PetraRoom {
		struct Mark { uint8_t type; float until; };
		std::vector<Mark> marks;
		uint32_t tiles = 0;
	};
	PetraRoom petra_room_;
	// tradeManager.js routeProspection: set when an ally's (or our) market or dock is finished,
	// destroyed or captured, diplomacy changes (checkEvents) or our territory grows by over 960 m²
	// (headquarters.js:2044); cleared once prospectForNewMarket has looked. RTS0 sees those
	// events as changes to `sig` (the finished Trade structures we and our allies hold, and the
	// diplomacy) and to `foundations` (an ally Trade foundation gone, finished or cancelled).
	struct PetraProspect {
		bool active = false;
		bool seen = false; // sig and foundations taken once, so the map's own markets don't count
		uint32_t sig = 0;
		std::vector<uint32_t> foundations; // sorted ids
	};
	PetraProspect petra_prospect_;
	// victoryManager.js targetedGaiaRelics: the Gaia relics this AI goes for, reinforced every
	// 10th turn until their owner changes. `sent` once a plan has gone for it (plans.length).
	struct PetraTargetedRelic { uint32_t relic = 0; uint32_t sent = 0; };
	std::vector<PetraTargetedRelic> petra_targeted_relics_;
	// defenseManager.js targetList: enemy structures seen being built in our territory, or
	// enemy civic centres going up near our structures; kept until gone or no longer an enemy's.
	struct PetraDefenseTargets { std::vector<uint32_t> ids; }; // sorted
	PetraDefenseTargets petra_defense_targets_;
	// basesManager.js basesMap kept between turns: the anchor (civic centre or colony id) that
	// claimed each of petra_bases_map's tiles (w x w), 0 none. A tile stays with the base that
	// first claimed it (addTerritoryIndexToBase) until the territory or that anchor is lost.
	struct PetraTileBases {
		int w = 0;
		std::vector<uint32_t> anchor;
	};
	PetraTileBases petra_tile_bases_;
	void petra_check_trade_events();
	void petra_set_no_room(uint8_t type);
	bool petra_no_room(uint8_t type) const;
	int petra_missing_room_count() const;
	void petra_clear_no_room(uint8_t type);
	// worker.js isInaccessibleSupply: each gatherer's or hunter's approach (approachingTarget, the
	// petra_timer_ time and spot it was last seen moving from, time < 0 none yet, carriedBefore,
	// alreadyTried), and the supplies it flagged (inaccessibleTime: skipped until `until`).
	struct PetraApproach { uint32_t unit, target, tried; float time, x, z; int32_t carried; };
	std::vector<PetraApproach> petra_approach_;
	struct PetraInaccessible { uint32_t id; float until; };
	std::vector<PetraInaccessible> petra_inaccessible_;
	bool petra_supply_inaccessible(uint32_t id) const;
	// baseManager.js gatherers[type]: each base's storehouse check, by its civic centre or colony's id
	// (petra_dropsite_check_ keeps only a pending storehouse from saves before v78).
	struct PetraBaseDropsiteCheck { uint32_t anchor_id; PetraDropsiteCheck check; };
	std::vector<PetraBaseDropsiteCheck> petra_base_dropsite_checks_;
	float petra_econ_timer_ = 0.0f;
	// GetCurrentGatherRates and queueManager wantedGatherRates as petra_manage_economy found them
	// this AI turn, for the trade manager (setTradingGoods, performBarter) later in the same turn.
	// Not saved: the economy recomputes them before every use (petra_rates_this_turn).
	float petra_cur_rate_[RES_COUNT] = {}, petra_want_rate_[RES_COUNT] = {};
	uint32_t petra_rates_stamp_ = 0;
	uint32_t petra_rates_key() const { return (petra_played_turn() << 4) | (uint32_t)(petra_pid_ + 1); }
	bool petra_rates_this_turn() const { return petra_rates_stamp_ == petra_rates_key(); }
	// headquarters.js pickMostNeededResources over those rates.
	static void petra_most_needed(const float want_rate[RES_COUNT], const float cur_rate[RES_COUNT], uint8_t out[RES_COUNT]);
	// pickMostNeededResources for diplomacy: wanted > 0 per resource, and the most needed one;
	// from this turn's gather rates when in, else from what our queues are short of.
	void petra_wanted_resources(bool out[RES_COUNT]);
	uint8_t petra_most_needed_resource();
	float petra_trade_timer_ = 0.0f;
	float petra_naval_timer_ = 0.0f;
	float petra_scout_timer_ = 0.0f;
	uint32_t petra_scout_unit_id_ = 0;
	float petra_scout_angle_ = 0.0f;
	float petra_scout_radius_ = 12.0f;

	void update_petra_bases(float dt);
	void update_petra_threat_map(float dt);
	void petra_manage_scouting(float dt);
	void petra_manage_economy(float dt);
	void petra_manage_trading(float dt);
	void petra_manage_naval(float dt);
	// navalManager.js: lands, seas and the seas between them (accessibility.js).
	struct PetraNavalTopo {
		float cell_m2 = 0.0f;
		uint32_t total = 0;                         // nav cells on the map
		std::vector<uint32_t> land_size, sea_size;  // cells per region id
		std::vector<uint32_t> links;                // land << 16 | sea, sorted (regionLinks)
		bool naval_map = false;                     // headquarters.js navalMap
		std::vector<bool> naval;                    // navalRegions, by sea id
		std::vector<bool> land_ok;                  // landRegions, by land id (classify)
		bool land_region(uint16_t l) const { return l < land_ok.size() && land_ok[l]; }
		bool linked(uint16_t land, uint16_t sea) const {
			return std::binary_search(links.begin(), links.end(), ((uint32_t)land << 16) | sea);
		}
		uint16_t sea_between(uint16_t a, uint16_t b) const;
		void classify(uint16_t home_land);
	};
	void petra_naval_topology(PetraNavalTopo &t) const;
	const PetraNavalTopo &petra_topo() const;
	mutable PetraNavalTopo petra_topo_cache_; // derived from nav_grid_.cells, not saved
	mutable uint64_t petra_topo_cache_key_ = 0; // bumped each time petra_topo_cache_ is worked out again
	// The nav grid petra_topo_cache_ was worked out from (cells, region ids, regions_ready_):
	// comparing these is cheaper than hashing the grid on every call.
	mutable std::vector<uint8_t> petra_topo_cells_;
	mutable std::vector<uint32_t> petra_topo_first_[2];
	mutable std::vector<uint16_t> petra_topo_ids_[2];
	mutable bool petra_topo_regions_ready_ = false;
	mutable bool petra_topo_cache_valid_ = false;
	uint16_t petra_land_at(float x, float z) const;
	// terrain-analysis.js landPassMap (accessibility.js getAccessValue): the land at (x, z) by
	// terrain alone, deep water, steep slopes and a circular map's edge splitting it and structures
	// not, unlike petra_land_at's regions. 0 off land. Derived from the nav grid, not saved.
	uint16_t petra_access_at(float x, float z) const;
	struct PetraAccess {
		std::vector<uint8_t> pass;  // cell (terrain tile, else nav cell) is land by terrain
		std::vector<uint16_t> land; // its land id from 1, 0 none
		std::vector<uint16_t> near; // an off-land cell's nearest land within 8 m, once looked up (0xFFFF before)
		int res = 0;
		uint32_t stamp = 0;
	};
	mutable PetraAccess petra_access_;
	uint32_t nav_terrain_stamp_ = 1; // bumped whenever the nav grid's terrain may have changed
	// This AI's topology classified from its home land (headquarters.js navalMap, landRegions).
	const PetraNavalTopo &petra_classified_topo(uint16_t &home) const;
	mutable PetraNavalTopo petra_class_topo_; // per AI turn, derived: not saved
	mutable uint16_t petra_class_home_ = 0;
	mutable int petra_class_topo_pid_ = -1;
	mutable uint64_t petra_class_key_ = 0; // petra_topo_cache_key_ it was classified from
	mutable uint8_t petra_class_for_ = 0;
	mutable bool petra_class_valid_ = false;
	void petra_assign_repairs();
	void petra_order_builders(const std::vector<uint32_t> &workers, size_t max, uint32_t bid);
	uint16_t petra_building_land(const Building &b, const PetraNavalTopo *topo = nullptr, uint16_t prefer = 0) const;
	uint16_t petra_home_land(float x, float z, const PetraNavalTopo *topo = nullptr) const;
	uint16_t petra_overseas(const PetraNavalTopo &t, float from_x, float from_z, uint32_t target_id) const;
	uint16_t petra_plan_overseas(float base_x, float base_z, uint32_t target_id, bool any_base = false) const;
	void petra_lands_around(float x, float z, float half, uint16_t out[8]) const; // a structure's lands, or a unit's
	void petra_attack_from_lands(uint16_t same_land, uint16_t out[8]) const; // same_land, or the home base's lands
	bool petra_lands_reach(const uint16_t from[8], const uint16_t to[8], bool by_sea, const PetraNavalTopo *&topo) const;
	bool petra_aboard(uint32_t uid) const;
	bool petra_is_transport_ship(size_t unit_idx) const;
	uint16_t petra_ship_sea(size_t unit_idx) const;
	int petra_transport_ships_on(uint16_t sea) const;
	uint16_t petra_best_ship(bool fishing) const;
	bool petra_can_fish_safely(float x, float z, uint16_t sea) const;
	void petra_manage_buildings(float dt);
	void petra_manage_research(float dt);
	bool petra_skips_random_tech(uint8_t tech_id, bool has_ally) const;
	// researchManager.js: the techs Petra could research now at a structure it owns
	// (gamestate.js findAvailableTech, phases left out), and one research start.
	void petra_available_techs(std::vector<uint8_t> &out) const;
	int petra_accounted_workers(bool queued = true) const; // false: units alive only (getOwnEntitiesByRole)
	bool petra_phasing() const;
	bool petra_has_active_base() const; // basesManager.js hasActiveBase: a finished base anchor of ours
	bool petra_start_research(uint8_t tech_id);
	void petra_research_population_bonus();
	bool petra_minor_tech_waiting() const;
	bool petra_has_temple_class() const;
	bool petra_has_builder() const;
	void petra_queue_special_tech(uint8_t t);
	void petra_research_trade_bonus();
	void petra_manage_military_production(float dt);
	void petra_manage_tactical_attacks(float dt);
	void petra_retake_civic_centres(); // defenseManager.js OwnershipChanged -> switchDefenseToAttack
	void petra_update_attack_plan(AttackPlan &plan, float base_x, float base_z); // one plan's attackPlan.js step
	void petra_update_started_raid(AttackPlan &plan, float base_x, float base_z); // a raid once it set off
	void petra_attack_units(AttackPlan &plan); // attackPlan.js update while attacking: each unit's target
	uint32_t petra_plan_next_target(AttackPlan &plan, float &tx, float &tz, uint16_t same_land = 0, bool must_start = false);
	bool petra_in_attack_squad(uint32_t uid) const; // in the prepared or a started attack's army
	bool petra_attack_can_spare(uint32_t uid, bool decay) const; // garrisonUnitsInside's attack plan rule
	void petra_attack_remove_unit(uint32_t uid); // attackPlan.js removeUnit, from every plan
	void petra_out_of_plan(uint32_t uid); // attackPlan.js removeUnit: plan -1 (outOfPlan)
	void petra_release_squad(std::vector<uint32_t> &squad); // every unit leaves the plan
	uint8_t petra_attack_enemy() const; // the player Petra's attacks are aimed at, 0xFF for none
	void petra_note_enemy(uint8_t enemy) const; // remember getEnemyPlayer's pick as currentEnemyPlayer
	// This tick's civic centre captures (OwnershipChanged), appended in tick_capture and
	// read by Petra later in the same tick; cleared at the end of tick(), so never saved.
	struct CivicCentreCapture { uint32_t building_id; uint8_t from, to; };
	std::vector<CivicCentreCapture> cc_captures_;
	void petra_manage_raid(float dt);
	void petra_manage_opening(float dt);
	void petra_manage_defense(float dt);
	// garrisonManager.js / defenseManager.js garrisons: what is near a holder of ours.
	struct PetraAround { bool defense_structure = false, melee_siege = false, range_siege = false, unit = false; };
	PetraAround petra_around(const Building &holder) const;
	bool petra_keep_garrisoned(size_t unit_idx, const Building &holder, const PetraAround &around) const;
	// incoming[building index]: our units walking to garrison there.
	// min_garrison < 0 fills the holder; >= 0 stops there and takes only units that can capture (TYPE_DECAY).
	void petra_garrison_units_inside(size_t holder_idx, bool allow_melee, std::vector<int> &incoming, int min_garrison = -1);
	// garrisonManager.js addDecayingStructure: the garrison a decaying structure of ours needs to stop
	// its decay (0 when Petra leaves it to decay).
	int petra_decay_garrison_min(const Building &b);
	// headquarters.js checkEvents: a decaying structure Petra is about to lose to an enemy is destroyed.
	void petra_check_decaying_structures();
	// Structures captured or starting to decay this tick (OwnershipChanged, TerritoryDecayChanged):
	// filled by tick_capture and rebuild_territory, read by every Petra this tick and cleared after
	// it, so never saved.
	std::vector<uint32_t> petra_decay_events_, petra_decay_scratch_;
	// This tick's hits on structures and animals (target, attacker): 0.28's Attacked events
	// for them, kept with the units' attacked_by ones. Cleared every tick, so never saved.
	std::vector<std::pair<uint32_t, uint32_t>> petra_hit_events_;
	void rebuild_territory_grids();
	uint32_t petra_garrison_holder_for(size_t unit_idx, bool heals_only, std::vector<int> &incoming) const;
	std::vector<int> petra_garrison_incoming() const;
	bool petra_same_land(size_t unit_idx, const Building &b) const; // PETRA.getLandAccess match
	// The unit's land is a pocket of at most 16 navcells (shut in among trees or structures).
	bool stands_in_pocket(size_t unit_idx) const;
	bool petra_mobile_dropsite_on_land(size_t unit_idx) const; // a supply wagon or worker elephant it may drop at, on its land
	// defenseManager.js garrisonAttackedUnit: the nearest healing structure, or with
	// emergency the nearest holder at all (unloading one unit when it is full).
	bool petra_garrison_attacked_unit(size_t unit_idx, bool emergency, std::vector<int> &incoming);
	size_t petra_attacked_unit_holder(size_t unit_idx, bool emergency, const std::vector<int> &incoming) const; // building index or SIZE_MAX
	// victoryManager.js: regicide heroes and the units guarding them.
	// victoryManager.js Garrison events: the regicide hero's guards when it is garrisoned.
	void petra_critical_ent_garrisoned();
	bool petra_is_critical_ent(size_t unit_idx) const;
	bool petra_guards_critical_ent(size_t unit_idx) const;
	bool petra_is_critical_wonder(uint32_t building_id) const;
	bool petra_is_critical_relic(size_t unit_idx) const;
	float petra_unit_strength(size_t i) const; // getMaxStrength
	bool petra_army_position(const PetraDefenseArmy &army, float &x, float &z) const; // foePosition
	// A defense army's foe that is still about: a living unit, or an animal neither dead nor
	// penned (defenseManager.js checkEnemyUnits keeps animals attacking us or our allies).
	bool petra_army_foe_pos(uint32_t fid, float &x, float &z) const;
	float petra_fauna_strength(size_t f) const; // getMaxStrength, x3 for an elephant (evaluateStrength)
	bool petra_army_capturing(const PetraDefenseArmy &army) const; // getType() == "capturing"
	float petra_enemy_capture_points(const Building &b) const;
	bool petra_near_invading_army(float x, float z, float dist2) const;
	void petra_remove_defender(uint32_t uid);
	float petra_builder_ratio() const;         // assignToFoundations builderRatio
	float petra_available_resource(uint8_t r) const; // queueManager.getAvailableResources: stock less every queue's account
	bool petra_passenger(uint32_t uid) const;   // booked on one of our transport plans
	void petra_assign_bombers(); // attackManager.js assignBombers
	void petra_move_dropsites_to_gatherers(); // worker.js moveToGatherer for mobile dropsites
	void petra_cancel_attacks_against(uint8_t player); // attackManager.js cancelAttacksAgainstPlayer
	// navalManager.requireTransport for a worker: ship it from its land to a supply's on another.
	bool petra_require_worker_transport(size_t unit_idx, uint16_t from_land, uint16_t to_land, float to_x, float to_z,
	                                    uint8_t owner = PETRA_TRANSPORT_WORKERS);
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
	// config.js setConfig personality draw: per-player seed from the match seed, + teamsLocked.
	uint32_t petra_personality_seed(uint8_t pid) const { return ((ai_seed_ ? ai_seed_ : sim_seed_) ^ ((uint32_t)(pid + 1) * 2654435761u)) | 1u; }
	void reroll_petra_personalities();
	void petra_refresh_queue_accounts(float dt);
	int petra_queue_base_priority(uint8_t q) const; // config.js priorities after setConfig (attackPlan.js for plan queues)
	int petra_queue_priority(uint8_t q) const;      // with changePriority's
	// queueManager.changePriority; `reset_when` names the queue whose next started item resets
	// it to Config.priorities (a plan's queueToReset), PETRA_Q_COUNT for none.
	void petra_change_priority(uint8_t q, int priority, uint8_t reset_when = PETRA_Q_COUNT);
	void petra_check_paused_queues();               // queueManager.checkPausedQueues
	void petra_fund_queue(uint8_t q, const float cost[RES_COUNT]); // setAccounts, then transferAccounts from the others
	void petra_staff_foundations();
	void petra_manage_fields();
	float petra_gather_rate(size_t i, uint8_t kind) const;
	uint8_t petra_gather_kind(size_t r) const;
	// worker.js startHunting: food dropsites and each supply's gatherers, gathered once per pass.
	struct PetraHuntScan {
		struct FoodDrop { float x, z; uint16_t land[4]; }; // the lands it borders (getLandAccess)
		std::vector<FoodDrop> food_drops;
		std::unordered_map<uint32_t, int> on_animal;  // live animal id -> units hunting it
		std::unordered_map<uint32_t, int> on_carcass; // meat resource id -> units gathering it
		std::vector<uint32_t> treasures_taken;        // treasures one of ours is already on its way to
	};
	PetraHuntScan petra_hunt_scan() const;
	bool petra_food_drop_near(const PetraHuntScan &scan, uint16_t land, float x, float z, float d2) const;
	uint32_t petra_find_hunt(size_t u, float px, float pz, const PetraHuntScan &scan, bool &live) const;
	void petra_check_inaccessible_supplies(PetraHuntScan &scan);
	bool petra_start_hunting(size_t u, PetraHuntScan &scan);
	bool petra_gather_treasure(size_t u, PetraHuntScan &scan, bool water = false); // entityExtend.js gatherTreasure
	void petra_treasures_taken(PetraHuntScan &scan) const;
	void petra_manage_hunters(PetraHuntScan &scan);
	void petra_check_hunt_drift(PetraHuntScan &scan); // worker.js: a chase that left our land is given up
	void petra_build_first_base(); // startingStrategy.js buildFirstBase
	void petra_check_base_expansion(); // headquarters.js checkBaseExpansion
	bool petra_build_new_base(int res = -1); // headquarters.js buildNewBase (res: RES_* or none)
	bool petra_find_strategic_cc_location(uint8_t btype, float &out_x, float &out_z);
	bool petra_find_economic_cc_location(uint8_t btype, uint8_t res, bool from_strategic, float &out_x, float &out_z);
	bool petra_cc_tile_ok(uint8_t btype, float x, float z) const;
	// queueplanBuilding.js findDockPosition: a dock site on `land` by `sea` (cell of its corner).
	// findDockPosition's other searches, over the whole map: buildOverseaDock's (on any of
	// `lands`, by land id, facing any sea) and buildFirstBase's (nearest to (px, pz)).
	struct PetraDockSearch {
		const std::vector<uint8_t> *lands = nullptr; // oversea
		bool proximity = false;
		float px = 0.0f, pz = 0.0f;
	};
	bool petra_find_dock_position(uint16_t land, uint16_t sea, int &out_cx, int &out_cz,
	                              const PetraDockSearch *search = nullptr) const;
	bool petra_build_oversea_dock();
	void petra_transport_garrisons_to(uint32_t foundation_id);
	void petra_resource_maps(std::vector<uint8_t> (&maps)[3], float x0, float x1, float z0, float z1) const;
	void petra_resource_demand(float out[RES_COUNT]) const;
	bool petra_perform_barter();      // tradeManager.js performBarter
	void petra_set_trading_goods();   // tradeManager.js setTradingGoods
	// tradeManager.js checkRoutes: a route between two of our or our allies' markets
	// (land) or docks on one sea (sea), and its gain per leg for our trader.
	struct PetraTradeRoute { uint32_t source = 0, target = 0; int gain = 0; uint16_t land = 0, sea = 0; };
	struct PetraTradeRoutes {
		PetraTradeRoute best;      // tradeRoute: both ends finished
		PetraTradeRoute potential; // potentialTradeRoute: foundations count
		std::vector<PetraTradeRoute> finished; // every finished pair, for checkRoutes(accessIndex)
	};
	void petra_check_routes(PetraTradeRoutes &out) const;
	// checkRoutes(accessIndex): the best route for a trader on that land, or a ship on that
	// sea; for a land trader the best land route anywhere when its own land has none.
	const PetraTradeRoute *petra_route_for(const PetraTradeRoutes &r, uint16_t access, bool ship) const;
	int petra_trade_minimal_gain() const; // tradeManager.js minimalGain
	int petra_target_traders() const;     // targetNumTraders: the config value until setTradingGoods sets it
	int petra_compute_target_traders() const; // what setTradingGoods sets it to
	bool petra_line_in_enemy_territory(float x1, float z1, float x2, float z2) const;
	// headquarters.js findMarketLocation: 1 with a spot (out_x, out_z) and its expected gain,
	// 0 when there are no markets to measure from (place it anywhere), -1 when nothing will do.
	int petra_find_market_location(float &out_x, float &out_z, int &out_gain) const;
	uint32_t petra_build_market(float home_x, float home_z, uint8_t queue); // ConstructionPlan for a Market
	bool petra_find_defensive_location(uint8_t btype, int &out_cx, int &out_cz) const; // headquarters.js findDefensiveLocation
	void petra_prospect_market(const PetraTradeRoutes &routes); // prospectForNewMarket
	int petra_unplanned_workers(int &supports);
	int petra_train_more_workers();
	bool petra_wood_starved() const;
	bool petra_wood_kept_for(int queue) const;
	void petra_clear_blocked_spawns();
	int petra_pop_room() const; // population left after Petra's queued batches
	float petra_template_strength(const UnitTypeInfo &ui, uint16_t tmpl, uint8_t type, float attack_mult = 1.0f) const;
	float petra_plan_pop_scaling() const; // config.js popScaling as attackPlan.js uses it
	int petra_plan_siege_target(bool huge) const; // addSiegeUnits targetSize
	void petra_raid_add_siege();                  // addSiegeUnits for the raid plan
	int petra_config_max_pop(int unlimited = 300) const; // config.js setConfig's getPopulationMax
	float petra_config_pop_scaling() const; // Config.popScaling: pop_scaling times sqrt(maxPop/300) under 300
	bool petra_can_place(int cell_x, int cell_y, int footprint, uint8_t btype = BLD_TYPE_COUNT, int clearance = -1) const; // btype: also 0 A.D.'s placement rules; clearance -1: petra_clearance
	bool petra_spot_free_but_distance(int cell_x, int cell_y, int footprint, uint8_t btype, int clearance = -1) const;
	bool petra_ring_clear(int cell_x, int cell_y, int footprint, uint8_t clearance = 1) const;
	static uint8_t petra_clearance(uint8_t btype); // findGoodPosition's 0, 4 or 8 m gap, as 0, 1, 2
	bool petra_limit_allows_building(uint8_t btype) const;
	mutable uint64_t petra_limit_cache_key_ = 0; // derived per tick, never saved
	mutable std::array<uint8_t, BLD_TYPE_COUNT> petra_limit_cache_{};
	bool petra_try_pay(uint8_t queue, float food, float wood, float gold, float stone);
	// What a structure costs this AI (structure_cost: its civ's template and cost bonuses), and paying it.
	void petra_structure_cost(uint8_t btype, float &food, float &wood, float &gold, float &stone) const;
	bool petra_try_pay_structure(uint8_t queue, uint8_t btype);
	// chatHelper.js: queue a line of `kind` (PetraChatKind) to `to` (a player, PETRA_CHAT_ALLIES or
	// PETRA_CHAT_EVERYONE) about `player`; sub is the request type or PetraChatStance.
	void petra_say(uint8_t kind, uint8_t to, uint8_t player = 0xFF, uint8_t sub = 0, int amount = 0, uint8_t res = 0,
	               float x = 0.0f, float z = 0.0f, uint8_t player2 = 0xFF);
	uint8_t petra_owner_of(uint32_t id) const; // a building's or unit's owner, 0xFF if gone
	void petra_handle_diplomacy_request(uint8_t player, uint8_t type);
	// wait_for_pop: queue it even with no population room, to wait in the trainer's queue as
	// 0 A.D.'s ProductionQueue holds an item until there is room.
	bool petra_train_template(uint8_t queue, uint32_t building_id, uint16_t tmpl, int count = 1, bool wait_for_pop = false,
	                          uint8_t plan = PETRA_ORDER_NONE);
	uint32_t petra_best_trainer(uint16_t tmpl, uint8_t unit_type, size_t max_queue) const;
	bool petra_relic_targeted(uint32_t relic_id) const;
	bool petra_unit_target_ok(int32_t t) const;
	// headquarters.js trainEmergencyUnits; a civic centre of ours rallying to itself is training them.
	bool petra_train_emergency_units(const std::vector<std::pair<float, float>> &positions);
	bool petra_emergency_rally(const Building &b) const {
		return b.team == petra_pid_ && b.has_rally_point && b.rally_target_bld_id == b.id;
	}
	void petra_end_emergency_rallies();
	void petra_change_diplomacy(uint8_t player, uint8_t stance);
	void petra_last_man_standing_check();
	int petra_player_units(uint8_t player) const;
	int petra_player_entities(uint8_t player) const;
	bool petra_path_crosses_water(float x0, float z0, float x1, float z1) const;
	bool petra_find_beach(float land_x, float land_z, float toward_x, float toward_z, float &out_x, float &out_z) const;
	bool petra_boarding_pos(uint16_t land, uint16_t sea, float ship_x, float ship_z, float dest_x, float dest_z,
	                        bool avoid_enemy, float &out_x, float &out_z, const float *skip_near = nullptr) const;
	bool petra_start_transport(const std::vector<uint32_t> &unit_ids, float from_x, float from_z, float to_x, float to_z, uint16_t sea,
	                           uint8_t owner);
	void petra_step_transport(PetraTransportPlan &tp, float dt);
	uint32_t petra_free_transport_ship(uint16_t sea, float x, float z) const;
	uint32_t petra_place_building(uint8_t btype, float near_x, float near_z, int min_r, int max_r, uint8_t queue);
	// queueplanBuilding.js findPosition (basesMap, influences, findBestTile).
	struct PetraBasesMap {
		struct Anchor { float x, z; uint16_t land; uint32_t id; }; // land: petra_access_at; id: the civic centre or colony
		std::vector<Anchor> anchors;
		std::vector<uint8_t> base; // territory tiles x0..x1, z0..z1: base number from 1, 0 none
		int w = 0, x0 = 0, z0 = 0, x1 = -1, z1 = -1;
		bool naval = false;
		int at(float x, float z, float half) const;
	};
	void petra_bases_map(PetraBasesMap &m) const;
	void petra_update_tile_bases();
	mutable std::vector<int> petra_bases_tile_scratch_; // petra_bases_map's territory tile per column, then per row
	// `bmap` and `base` (from 1) limit the search to that base's territory.
	uint32_t petra_place_dropsite(uint8_t res, float min_quality = 50.0f, bool *no_site = nullptr, float fund_above = -1.0f,
	                              const PetraBasesMap *bmap = nullptr, int base = 0);
	int petra_military_base(const PetraBasesMap &m) const;
	bool petra_find_position(uint8_t btype, const float *base_at, const float *favor_at, bool military_base, int &out_cx,
	                         int &out_cz);
	uint32_t petra_place_generic(uint8_t btype, uint8_t queue, const float *base_at = nullptr, const float *favor_at = nullptr,
	                             bool military_base = false);

	bool bulk_spawn_ = false;
	int grid_res_ = MIN_GRID_RES; // nav/FoW/territory resolution, grid_res_for_map(map_half_)
	std::vector<uint8_t> fow_grid_ = std::vector<uint8_t>(MIN_GRID_RES * MIN_GRID_RES, 0); // FoW state per tile
	uint8_t fow_viewer_ = 0;
	std::array<std::vector<uint8_t>, MAX_PLAYERS> player_fow_; // per-player FoW for players 1..7 (index 0 unused: fow_grid_)
	std::vector<uint8_t> unexplored_fow_ = std::vector<uint8_t>((size_t)MAX_GRID_RES * MAX_GRID_RES, 0);
	std::vector<uint8_t> revealed_fow_ = std::vector<uint8_t>((size_t)MAX_GRID_RES * MAX_GRID_RES, FOW_VISIBLE);
	LosGrid los_;                         // per-player LOS counts and visible/explored bits (saved v42)
	std::vector<LosSource> los_unit_src_; // the circle each unit index last added
	std::vector<LosSource> los_bld_src_;  // the circle each buildings_ index last added
	std::vector<LosSource> los_fauna_src_; // the circle each fauna_ index's owned animal adds (rebuilt on load)
	float owned_fauna_vision(size_t f) const;
	void update_fauna_los();
	// VisionSharing.AddSpy: a bribed unit sees for the briber (its allies sharing that LOS)
	// while the spy lasts. Rebuilt from spies_ each LOS update and on load, never saved.
	std::vector<LosSource> los_spy_src_;
	std::vector<LosSource> los_garrison_src_; // VisionSharing of garrisoned allies, rebuilt each tick
	void update_spy_los();
	void update_garrison_los();

	// Per-AI Petra state. The petra_* members above hold the state of the AI being
	// run (PETRA_PLAYER between ticks); every other AI slot's state is parked here
	// and swapped in while that AI thinks (0 A.D. runs one Petra instance per AI).
#define RTSO_PETRA_SLOT_FIELDS(X) \
	X(petra_threat_map_) X(petra_strike_force_) X(petra_attack_plan_) X(petra_raid_plan_) \
	X(petra_opening_step_) X(petra_opening_timer_) X(petra_defense_armies_) X(petra_emergency_) \
	X(petra_queues_) X(petra_transports_) X(petra_defense_timer_) X(petra_victory_timer_) \
	X(petra_diplo_timer_) X(ai_wonder_victory_timer_) X(petra_bases_) X(petra_fnd_watch_) \
	X(petra_timer_) X(petra_econ_timer_) X(petra_trade_timer_) X(petra_naval_timer_) \
	X(petra_scout_timer_) X(petra_scout_unit_id_) X(petra_scout_angle_) X(petra_scout_radius_) \
	X(petra_fleets_) X(petra_diplo_) X(petra_attack_mgr_) X(petra_config_) \
	X(petra_sent_diplo_) X(petra_started_attacks_) X(petra_defense_build_) X(petra_dropsite_check_) \
	X(petra_failed_gather_) X(petra_hunt_search_) X(petra_base_dropsite_checks_) X(petra_approach_) X(petra_inaccessible_) \
	X(petra_kept_) X(petra_move_to_gatherer_) X(petra_supply_) X(petra_room_) X(petra_prospect_) X(petra_targeted_relics_) X(petra_tile_bases_) X(petra_defense_targets_)
	struct PetraSlot {
		bool initialized = false;
#define RTSO_PETRA_SLOT_DECL(m) decltype(Simulation::m) m{};
		RTSO_PETRA_SLOT_FIELDS(RTSO_PETRA_SLOT_DECL)
#undef RTSO_PETRA_SLOT_DECL
	};
	std::vector<PetraSlot> petra_slots_; // indexed by player id, sized on first use
	void petra_swap_slot(uint8_t pid);
	// 0.28 PetraBot plays one turn every PETRA_TURN_TURNS sim turns, offset by player
	// (_petrabot.js:110 `(turn + player) % 8 == 5`, the first turn at once), so with 0 A.D.'s
	// 200 ms turns an AI thinks every 1.6 s. What happens in between reaches it as the
	// events saved since its last turn (savedEvents): here tick-stamped buffers kept for
	// PETRA_TURN_TICKS ticks and read by each AI over its own window (saved v73).
	static constexpr uint32_t PETRA_TURN_TURNS = 8;
	static constexpr uint32_t PETRA_TURN_TICKS = PETRA_TURN_TURNS * 4; // 4 ticks a 200 ms sim turn
	static constexpr float PETRA_TURN_S = PETRA_TURN_TICKS * FIXED_STEP; // 1.6 s
	bool petra_turn_due(uint8_t pid) const; // this tick is pid's AI turn
	// gameState.ai.playedTurn of the AI being run: its turns so far, this one included.
	uint32_t petra_played_turn() const;
	// playedTurn % n == r, the 0.28 "every nth turn" gates.
	bool petra_turn_mod(uint32_t n, uint32_t r = 0) const { return petra_played_turn() % n == r; }
	struct PetraAttackedEvent { uint32_t tick, target, attacker; }; // units_.attacked_by of that tick
	struct PetraStampedCapture { uint32_t tick; CivicCentreCapture cap; };
	struct PetraStampedId { uint32_t tick, id; };
	struct PetraAttackRequest { uint32_t tick; uint8_t source, target, pad[2]; };
	std::vector<PetraAttackedEvent> petra_ev_attacked_;
	std::vector<PetraStampedCapture> petra_ev_captures_;
	std::vector<PetraStampedId> petra_ev_decay_;
	std::vector<PetraAttackRequest> petra_attack_requests_; // AttackRequest events (diplomacy and plan launches)
	void petra_record_events(); // this tick's events into the buffers, the stale ones out
	// The window (from, to] of ticks the AI being run reads events over, and those events,
	// gathered at the start of its turn (not saved: rebuilt from the buffers).
	uint32_t petra_window_from_ = 0;
	float petra_turn_dt_ = 0.0f; // the game time this turn covers (set each turn, not saved)
	std::vector<std::pair<uint32_t, uint32_t>> petra_turn_attacks_; // (target, attacker) in order
	std::unordered_map<uint32_t, uint32_t> petra_turn_attacker_;    // target -> its last attacker
	std::vector<CivicCentreCapture> petra_turn_captures_;
	std::vector<uint32_t> petra_turn_decay_;
	void petra_gather_turn_events();
	uint32_t petra_attacker_of(size_t unit_idx) const; // who attacked this unit since the last turn, 0 = nobody
	void petra_process_attack_requests();
	void petra_answer_attack_request(uint8_t source, uint8_t target);
	// Saves or loads save_tick_state's fields, every AI's Petra state included (save v15); simulation.cpp.
	template <class Ar> friend void tick_state_io(Ar &ar, Simulation &s);
	void petra_reset_ai_state();
	PetraConfig &petra_config_slot(uint8_t pid);
	NavGrid nav_grid_; // Phase1: SoA nav mesh (buildings + water), mirrors 0AD Grid<NavcellData>
	Grid<uint8_t> territory_grid_; // grid_res_^2 player id per cell, G2 (CCmpTerritoryManager)
	Grid<uint8_t> influence_grid_; // grid_res_^2 influence count per cell, G2
	// CCmpTerritoryManager connectivity, derived from territory_grid_ and the territory roots
	// (civic centres and wonders): each 8-connected patch of one player's territory is a region,
	// connected when it holds one of that player's roots. Unconnected regions keep the count of
	// bordering tiles of connected foreign regions and of neutral land (GetNeighbours(filterConnected)).
	// Rebuilt on demand after the grid changes; not saved or hashed.
	struct TerritoryRegion {
		uint8_t owner;
		bool connected;
		std::array<uint32_t, MAX_PLAYERS> neighbours;
		uint32_t neutral; // bordering neutral tiles (Gaia's share of the decay)
	};
	mutable Grid<uint16_t> territory_region_;
	mutable std::vector<TerritoryRegion> territory_regions_;
	mutable bool territory_regions_dirty_ = true;
	void ensure_territory_regions() const;
	// !IsTerritoryBlinking: the tile at (x, z) is neutral or in a region holding one of its owner's roots.
	bool territory_connected_at(float x, float z) const;
	// TerritoryDecay.IsConnected + GetConnectedNeighbours for a structure: false when it does not
	// decay; otherwise `to` gets the weight each player receives, and `to_gaia` Gaia's.
	bool territory_decay_targets(const Building &b, std::array<float, MAX_PLAYERS> &to, float &to_gaia);
	bool territory_decay_targets(float x, float z, uint8_t team, bool neutral, std::array<float, MAX_PLAYERS> &to, float &to_gaia);
	// rebuild_territory()'s inputs the last time it filled the grids: each claiming structure's
	// TerritoryInfluence in buildings_ order, the grid size and the map extent. Same inputs, same
	// grids, so the 1 Hz refresh skips.
	struct TerritoryRoot {
		float x, z, radius;
		uint8_t team;
		uint16_t weight; // after team: saves before it kept padding (0) here
		bool operator==(const TerritoryRoot &) const = default;
	};
	std::vector<TerritoryRoot> territory_roots_, territory_roots_scratch_;
	// rebuild_territory_grids() scratch: CalculateCostGrid's tile costs and CalculateTerritories'
	// per-structure weights, flood queue and the tiles one structure reached.
	static constexpr uint8_t TERRITORY_IMPASSABLE_COST = 4; // territorymanager.xml ImpassableCost
	static constexpr uint8_t TERRITORY_OFF_WORLD_COST = 255; // CCmpTerritoryManager: past the map edge
	Grid<uint8_t> territory_cost_;
	std::vector<uint32_t> territory_entity_w_, territory_open_, territory_touched_;
	int territory_roots_res_ = 0;
	float territory_roots_half_ = 0.0f;
	// The cost grid's inputs when it was last worked out: the nav grid (nav_terrain_stamp_), the
	// grid size, the map extent and a circular map's edge.
	uint32_t territory_cost_stamp_ = 0;
	int territory_cost_res_ = 0;
	float territory_cost_half_ = 0.0f;
	bool territory_cost_circular_ = false;
	// What CalculateTerritories works out, kept per player so that a capture, which moves one
	// structure between two players, floods only that structure again. One structure's flood
	// depends on nothing but itself and the cost grid; a player's weight on a tile is its floods'
	// sum, except on a structure's own tile, where the weights before it are dropped (the replay in
	// rebuild_territory_grids). Not saved: kept only while the cost grid and the influence grid
	// are those it made (territory_last_influence_), so a loaded game floods everything once.
	struct TerritoryFlood {
		TerritoryRoot root{};
		int x0 = 0, z0 = 0, w = 0, h = 0; // the tiles it reached
		std::vector<uint32_t> v;           // its weight on each of them, row by row (0: not reached)
		int origin = -1;                   // its own tile, -1 when it floods nothing
		uint32_t at(int x, int z) const {
			x -= x0;
			z -= z0;
			return (unsigned)x < (unsigned)w && (unsigned)z < (unsigned)h ? v[(size_t)z * w + x] : 0;
		}
	};
	struct TerritoryTeam {
		std::vector<TerritoryFlood> floods; // the player's claiming structures, in buildings_ order
		std::vector<uint32_t> sum;          // all its floods' weights per tile
		std::vector<uint32_t> max_w;        // the highest weight its floods gave each tile, as they ran
	};
	std::array<TerritoryTeam, MAX_PLAYERS> territory_teams_;
	bool territory_teams_valid_ = false;
	std::vector<uint8_t> territory_last_influence_;
	// Territory tiles each player holds, counted as the grid is rebuilt (on load too): not saved.
	std::array<uint32_t, MAX_PLAYERS> territory_tiles_{};
	std::vector<WaypointPath> unit_paths_; // per-unit path (UNIT_CAP), SoA waypoint_cur/count sync
	// move_units' copy of the buildings that block movement, in buildings_ order, and the mines, with
	// only the fields nav/building_steering.h reads, so its per-unit scans read a short packed list.
	struct SteerObstacle {
		uint32_t id;
		float x, z;
		float ux, uz, hu, hv; // Building::ux, uz, hu, hv
		uint8_t type;
		bool wrecked;
		float ext; // nav::footprint_extent, which the steering loops test every obstacle by
	};
	static SteerObstacle steer_obstacle(uint32_t id, float x, float z, float ux, float uz, float hu, float hv, uint8_t type) {
		SteerObstacle o{id, x, z, ux, uz, hu, hv, type, false, 0.0f};
		o.ext = nav::footprint_extent(nav::Footprint{x, z, ux, uz, hu, hv});
		return o;
	}
	// An open gate's Left and Right parts, which keep blocking (gate_door_fraction): f(x, z, hu, hv)
	// for each, along the gate's width either side of its Door. The template's obstruction cluster
	// lines Left, Door and Right up along the gate's width (structures/{civ}/wall_gate.xml). That
	// width lies along u, or along v when the footprint is turned an odd number of quarter turns
	// (Building::set_rot swaps hu and hv then): parts laid along u there would be two slabs the
	// gate's whole length, shutting the open gate.
	template <class F>
	static void for_each_gate_side(const Building &b, F &&f) {
		const bool along_v = b.hv > b.hu;
		const float half_w = along_v ? b.hv : b.hu;
		const float door = half_w * gate_door_fraction(b.type, b.civ), side = 0.5f * (half_w - door);
		if (side <= 0.0f) return;
		const nav::Vec2 w = along_v ? nav::footprint_v(b) : nav::Vec2(b.ux, b.uz);
		for (const float sgn : {-1.0f, 1.0f})
			f(b.x + w.x * sgn * (door + side), b.z + w.y * sgn * (door + side), along_v ? b.hu : side, along_v ? side : b.hv);
	}
	// A finished gate that is open now: only its door lets units through.
	static bool gate_open_now(const Building &b) {
		return is_gate_type(b.type) && !b.wrecked && b.build_progress >= 0.99f && !blocks_movement_now(b);
	}
	std::vector<SteerObstacle> steer_obstacles_;
	uint32_t short_path_key_ = 1; // this tick's steering obstacles and water level (ShortPathMemo::key)
	// Each unit's last short path search among them (ShortPathSearchMemo), by unit index; not
	// saved, as a search finds the same again.
	std::vector<ShortPathSearchMemo> short_path_search_;
	std::vector<SteerObstacle> sep_obstacles_; // separation()'s list, built the same way
	// SteerObstacle::type of a Gaia mine (mine_footprint), after the buildings in both lists.
	static constexpr uint8_t MINE_OBSTACLE = 0xFF;
	void append_mine_obstacles(std::vector<SteerObstacle> &out) const;
	// This tick's mines (append_mine_obstacles), listed once for move_units and separation.
	std::vector<SteerObstacle> mine_obstacles_;
	uint64_t mine_obstacles_tick_ = ~uint64_t{0};
	const std::vector<SteerObstacle> &tick_mine_obstacles();
	// Trees, fruit bushes and treasures, which block land units' movement (template_gaia_tree,
	// _fruit and _treasure Obstruction/Static: flora_obstruction_m, TreasureTypeInfo width x depth)
	// but not paths (RTS0's navcells are wider than a tree). Too many for the per-unit scans of
	// steer_obstacles_, so units steer round only those near them (gaia_obstacles_near) and
	// separation() pushes them out of these. Rebuilt with gaia_grid_ when the set changes.
	static constexpr uint8_t GAIA_OBSTACLE = 0xFE;
	static constexpr float GAIA_STEER_REACH = 2.5f; // world units round a walker whose trees it steers by
	std::vector<SteerObstacle> gaia_obstacles_;
	std::vector<uint32_t> gaia_res_idx_; // each one's supply index in resources_, UINT32_MAX for a treasure
	std::vector<uint32_t> mine_candidates_; // the supplies that are mines, for append_mine_obstacles
	// A tree or bush stays listed once cut down (ensure_gaia_obstacles); a treasure leaves at once.
	bool gaia_obstacle_live(uint32_t k) const {
		const uint32_t r = gaia_res_idx_[k];
		return r == UINT32_MAX || (resources_.alive[r] && resources_.amount[r] > 0);
	}
	nav::BucketGrid gaia_grid_;
	uint64_t gaia_obstacles_sig_ = 0;
	void ensure_gaia_obstacles();
	// Appends to `out` the Gaia obstacles that may reach into the box (x, z) +- reach.
	void gaia_obstacles_near(float x, float z, float reach, std::vector<SteerObstacle> &out) const;
	// Whether the trees in the way matter to a walker at (x, z) heading for (ax, az): one of them
	// crosses the first `reach` of that line, kept `radius` off, or holds the walker. Only then does
	// it need gaia_obstacles_near; cheaper, as it looks along the line only.
	bool gaia_in_the_way(float x, float z, float ax, float az, float reach, float radius, uint32_t ignore) const;
	// Ships keep off floating treasures (template_gaia_treasure shipwrecks: Obstruction/Static,
	// BlockMovement): when the first SHIP_WRECK_STEER_REACH of ship i's line from (x, z) to (ax, az)
	// crosses one, grown by its hull, `out` is the nearer way round, a corner of that square.
	static constexpr float SHIP_WRECK_STEER_REACH = 12.0f;
	bool ship_wreck_steer(size_t i, float x, float z, float ax, float az, nav::Vec2 &out) const;
	// Whether ship i's step from (x, z) to (nx, nz) enters a floating treasure's square grown by its
	// hull (not the one it sails to at (ax, az)): move_units refuses it, and the ship slides along.
	bool ship_step_into_wreck(size_t i, float x, float z, float nx, float nz, float ax, float az) const;
	// Navcells a wood closes to long paths: FOREST_CLOSED where trees and bushes leave no way
	// across the cell (forest_cell_closed), FOREST_CLOSED_LARGE where only the large class's
	// clearance finds none. rebuild_nav_grid marks the first obstructed and the second
	// NavGrid::large_closed. Recomputed when the Gaia obstacles change (update_forest_cells), and
	// the nav grid rebuilt when a cell changes.
	enum : uint8_t { FOREST_OPEN = 0, FOREST_CLOSED = 1, FOREST_CLOSED_LARGE = 2 };
	std::vector<uint8_t> forest_cells_;
	uint64_t forest_sig_ = 0;
	bool forest_removals_only_ = false; // tick_gather felled trees from up-to-date forest_cells_
	uint64_t forest_sig_of_gaia() const { return gaia_obstacles_sig_ ^ ((uint64_t)nav_grid_.res << 48); }
	bool forest_cell_closed(int cx, int cy, float clr = CLEARANCE_DEFAULT) const;
	bool update_forest_cells();
	nav::ObstacleGrid sep_grid_; // sep_obstacles_ by cell, rebuilt with it
	// steer_obstacles_ by cell, rebuilt in move_units when they change, and the line test that
	// looks only at those under the line (the same answer as nav::line_blocked_by_buildings over
	// all of them).
	nav::ObstacleGrid steer_grid_;
	uint32_t steer_grid_key_ = 0;
	float steer_grid_half_ = 0.0f;
	bool steer_line_blocked(nav::Vec2 pos, nav::Vec2 aim, float max_len, float radius, uint32_t ignore) const;
	// tick_capture()'s count and strength of nearby capturers per building and team, and the grid of
	// building reach it finds them with. Rebuilt every tick.
	struct CaptureTally { int count[MAX_PLAYERS]; float strength[MAX_PLAYERS]; };
	struct CaptureReach { uint32_t building; float x, z; };
	std::vector<CaptureTally> capture_tally_;
	std::vector<uint32_t> capture_cell_start_;
	std::vector<CaptureReach> capture_cell_buildings_;
	// What capture_cell_* were built from (map half-size, then each building's x, z): the grid
	// is rebuilt only when these change.
	std::vector<float> capture_cell_key_;
	// Capture points each player's capture attacks took from each building this tick
	// (tick_combat fills it, tick_capture spends it). Indexed like buildings_.
	std::vector<std::array<float, MAX_PLAYERS>> capture_attack_;

	// UnitAI orders (0 A.D. UnitAI.js orderQueue). The current order lives in the unit
	// SoA; these are the Shift-queued ones after it, by unit id.
	std::unordered_map<uint32_t, std::vector<UnitOrder>> order_queues_;
	bool push_front_order_ = false; // transient: set only while one command runs (not saved)
	// UnitAI isGuardOf by unit id (save v92): the charge a unit took up guarding. Other orders
	// keep it and the IDLE Timer goes back to guarding; remove-guard, a guard order for
	// someone else or a charge that is gone or no longer an ally ends it.
	std::unordered_map<uint32_t, uint32_t> guard_of_;
	std::vector<uint32_t> locked_dropsite_units_; // sorted ids of supply wagons whose owner locked them
	// UnitAI workOrders: the queue from its first Gather, Trade, Repair or ReturnResource order,
	// kept when an order that isn't work replaces it (UpdateWorkOrders), for Back to Work.
	std::unordered_map<uint32_t, std::vector<UnitOrder>> work_orders_;
	static bool is_work_order_kind(uint8_t k) {
		return k == ORDER_GATHER || k == ORDER_TRADE || k == ORDER_BUILD || k == ORDER_RETURN_RESOURCE;
	}
	void update_work_orders(size_t i, uint8_t kind);
	bool guard_charge_ok(size_t i, uint32_t charge) const;
	bool applying_queued_ = false; // set while an order from a queue is applied: keep the rest
	UnitOrder current_order(size_t i) const;
	void set_order(size_t i, uint8_t kind, uint8_t flags, uint32_t target, float x, float z);
	void begin_walk_order(size_t i); // cmd_move's walk: not forced, replaces the queue
	void drop_orders(size_t i);      // orders UnitAI doesn't model (trade, hunt, board): no order, no queue
	// Splits a Shift order: busy units get it queued, the rest are returned to run now.
	// Without Shift every unit's queue is dropped and all of them run it now.
	float unit_height(size_t i) const; // terrain, or the water surface under a ship
	float unit_attack_origin_y(size_t i) const;
	bool trade_route_ok(size_t i, uint32_t market_a_id, uint32_t market_b_id) const;
	std::vector<uint32_t> split_queued(const uint32_t *ids, size_t count, const UnitOrder &o, bool queued);
	void apply_order(uint32_t uid, const UnitOrder &o);
	bool order_still_valid(size_t i, const UnitOrder &o) const;
	int32_t structure_attacker(uint32_t building_id, uint8_t team) const;
	uint32_t nearest_dropsite(size_t i) const;
	bool attack_target_hidden(size_t i, size_t t) const;
	void deposit_carry(size_t i);
	void finish_order(size_t i, bool find_new_target = false);
	void resume_order(size_t i);
	bool guard_keeps_target(size_t i, size_t t) const;
	// UnitAI IDLE Timer with isGuardOf: guard the charge again; false when there is none.
	bool resume_guard(size_t i);
	void guard_answer(size_t i, size_t g, uint32_t attacker);
	void walk_to_held_position(size_t i);
	void walk_back_if_drifted(size_t i);
	void find_targets_then_walk_back(size_t i);
	void respond_to_attack(size_t i);
	bool entity_xz(uint32_t id, float &x, float &z) const;
	bool flee_done(size_t i);
	void respond_to_attacker(size_t i, uint32_t attacker_id, float ax, float az, bool melee, bool structure = false);
	std::vector<uint16_t> combat_wild_scratch_; // tick_combat: dangerous animals this tick
	std::vector<uint16_t> combat_herd_scratch_; // tick_combat: players' animals out of their corrals (structure targets)
	std::vector<uint32_t> combat_look_again_scratch_; // tick_combat: units that hit a non-preferred target
	std::vector<uint8_t> status_bits_scratch_; // tick_status_effects: each unit's Poisoned/Burning bits
	// ResourceGatherer.CanGather: unit index i has a Rates entry for resource index r.
	bool can_gather(size_t i, size_t r) const;
	// With `busy`, skips `skip` and resources whose MaxGatherers are all taken (ResourceSupply.IsAvailable).
	uint32_t find_nearby_resource(float x, float z, uint8_t res_type, uint8_t team, int subtype = -1,
			const std::unordered_map<uint32_t, FieldGatherers> *busy = nullptr, uint32_t skip = 0, uint8_t fauna = 0) const;
	uint32_t find_nearby_foundation(float x, float z, uint8_t team) const;
	bool self_garrison_rally(const Building &b, uint8_t unit_type, uint16_t tmpl) const;
	void apply_rally_point(uint32_t uid, const Building &b, bool ignore_self_garrison = false);
	void apply_rally_step(uint32_t uid, const Building &b, const Building::RallyStep &st, bool queued, bool ignore_self_garrison);
	void rally_after_ungarrison(uint32_t uid, uint32_t building_id);
	void on_construction_finished(const Building &b, size_t builder);
	void enter_repairing(size_t i);
	void next_after_construction(size_t k, const Building &b, bool food, bool other);
	void destroy_building(Building &b);
	bool can_capture_building(uint8_t player, const Building &b) const;
	bool can_heal(size_t healer, int32_t target) const;         // Heal.js CanHeal
	uint32_t find_heal_target(size_t healer) const;             // UnitAI FindNewHealTargets
	void start_heal(size_t healer, uint32_t target, uint8_t flags);
	void tick_heal(size_t healer);                              // UnitAI HEAL.APPROACHING / HEALING
	// ORDER_BUILD on a Repairable unit (UnitAI Repair with a unit target, Builder.js CanRepair).
	float unit_repair_time_ratio(size_t t) const; // 0: not Repairable
	bool can_repair_unit(size_t builder, int32_t target) const;
	bool repair_unit_in_range(size_t builder, size_t target) const; // Builder.js IsTargetInRange
	void order_repair_unit(const uint32_t *unit_ids, size_t count, uint32_t target_unit_id, bool queued, bool force);
	void start_repair_unit(size_t builder, uint32_t target, uint8_t flags);
	void tick_repair_unit(size_t builder);                      // UnitAI REPAIR.APPROACHING / REPAIRING
	bool garrison_in_range(size_t i, const Building &b) const { return garrison_in_range(i, b, units_.pos_x[i], units_.pos_z[i]); }
	bool garrison_in_range(size_t i, const Building &b, float x, float z) const; // as if unit i stood at (x, z)
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
	// The lowest terrain height (0 on a flat map), found once per heightmap (structure arrows' reach).
	mutable float terrain_lowest_ = 0.0f;
	mutable bool terrain_lowest_known_ = false;
	float lowest_terrain_height() const;
	TerrainTiles terrain_tiles_; // saved v49, after the heightmap
	float terrain_water_height_ = -1.0f;
	std::string current_map_id_ = "default";
	std::string current_map_name_ = "Standard Arena";
	std::string current_map_biome_ = "mediterranean";

	uint32_t alloc_id() { return next_id_++; }
};

} // namespace rtso

#endif
