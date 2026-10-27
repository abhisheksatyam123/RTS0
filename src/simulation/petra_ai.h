#ifndef RTSO_PETRA_AI_H
#define RTSO_PETRA_AI_H

// Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games).
// Native collapse of 0 A.D. Petra JS. Vendor (not compiled):
//   0ad/source/mods/public/simulation/ai/petra/{config,difficultyLevel,
//   startingStrategy,researchManager,navalManager,attackPlan,queueManager,
//   defenseManager,emergencyManager,diplomacyManager,victoryManager}.js
// Wired from Simulation::tick_petra_ai.

#include "sim_types.h"
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace rtso {

enum PetraDifficulty : uint8_t {
	PETRA_SANDBOX   = 0,
	PETRA_VERY_EASY = 1,
	PETRA_EASY      = 2,
	PETRA_MEDIUM    = 3,
	PETRA_HARD      = 4,
	PETRA_VERY_HARD = 5
};

enum PetraBehavior : uint8_t {
	PETRA_BEHAVIOR_RANDOM     = 0,
	PETRA_BEHAVIOR_DEFENSIVE  = 1,
	PETRA_BEHAVIOR_BALANCED   = 2,
	PETRA_BEHAVIOR_AGGRESSIVE = 3
};

inline bool petra_is_support_unit(uint8_t t) {
	return t == UNIT_VILLAGER || t == UNIT_TRADER || t == UNIT_FEMALE_CITIZEN ||
	       t == UNIT_FISHING_BOAT || t == UNIT_MERCHANT_SHIP || t == UNIT_WORKER_ELEPHANT || t == UNIT_WAGON;
}

inline bool petra_is_combat_unit(uint8_t t) {
	return !petra_is_support_unit(t);
}

// config.js garrisonHealthLevel.medium: an attacked support unit below it shelters.
inline constexpr float PETRA_GARRISON_HP_MEDIUM = 0.55f;

// config.js + difficultyLevel.js
struct PetraConfig {
	uint8_t difficulty = PETRA_MEDIUM;
	uint8_t behavior   = PETRA_BEHAVIOR_BALANCED;

	float aggressive   = 0.5f;
	float defensive    = 0.5f;
	float cooperative  = 0.5f;
	float pop_scaling  = 1.0f;
	// 0 A.D. has no AI income; difficulty scales gathering and build times
	// (simulation/helpers/InitGame.js "AI Bonus" modifiers).
	float gather_rate = 1.0f;  // ResourceGatherer/BaseSpeed, Trader/GainMultiplier
	float build_time  = 1.0f;  // Cost/BuildTime (units, structures, technologies)

	float defense_ratio_own = 2.0f;   // Defense.defenseRatio.own
	float army_compact_sq   = 2000.0f; // Defense.armyCompactSize
	int   pop_phase2         = 150;  // config.js Economy.popPhase2 before setConfig's population scaling (petra_pop_phase2)
	int   work_phase3        = 180;  // config.js Economy.workPhase3, likewise (petra_work_phase3); 999 never cities

	float garrison_hp_low  = 0.40f;
	float garrison_hp_high = 0.70f;
	float emergency_pop_factor    = 0.60f;
	float emergency_struct_factor = 0.60f;
	float emergency_root_factor   = 0.50f;

	// Saves before v60 held the six coarse queue priorities here; the queues' priorities now
	// come from Config.priorities (petra_queue_base_priority).
	int reserved_[6] = {};

	// config.js setConfig. `seed` != 0 draws the personality as 0 A.D. does (randFloat within
	// the behaviour's range, cooperative anywhere in [0, 1]); 0 keeps each behaviour's middle.
	void recompute(uint32_t seed = 0, bool teams_locked = false, bool allied_victory = false) {
		switch (behavior) {
			case PETRA_BEHAVIOR_DEFENSIVE:
				aggressive = 0.18f; defensive = 0.82f; cooperative = 0.65f; break;
			case PETRA_BEHAVIOR_AGGRESSIVE:
				aggressive = 0.85f; defensive = 0.15f; cooperative = 0.28f; break;
			case PETRA_BEHAVIOR_RANDOM:
				aggressive = 0.45f; defensive = 0.45f; cooperative = 0.50f; break;
			default:
				aggressive = 0.50f; defensive = 0.50f; cooperative = 0.50f; break;
		}
		// Sandbox keeps the default personality whatever the behaviour (setConfig draws above it).
		if (difficulty == PETRA_SANDBOX) aggressive = defensive = cooperative = 0.5f;
		if (seed != 0 && difficulty > PETRA_SANDBOX) {
			uint64_t st = seed;
			auto rand01 = [&st]() { // splitmix64
				uint64_t z = (st += 0x9E3779B97F4A7C15ull);
				z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
				z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
				return (float)((z ^ (z >> 31)) >> 40) / (float)(1u << 24);
			};
			// personalityList: random 0..1, defensive 0..0.27, balanced 0.37..0.63, aggressive 0.73..1
			static const float kMin[4] = { 0.0f, 0.0f, 0.37f, 0.73f }, kMax[4] = { 1.0f, 0.27f, 0.63f, 1.0f };
			const int b = behavior <= PETRA_BEHAVIOR_AGGRESSIVE ? behavior : PETRA_BEHAVIOR_BALANCED;
			const float x = rand01() - 0.5f;
			const float variation = 0.15f * (2.0f * rand01() - 1.0f) * std::sqrt(std::max(0.0f, 0.25f - x * x));
			const float agg = std::clamp(x + variation, -0.5f, 0.5f) + 0.5f;
			const float def = std::clamp(-x + variation, -0.5f, 0.5f) + 0.5f;
			aggressive = kMin[b] + agg * (kMax[b] - kMin[b]);
			defensive = 1.0f - kMax[b] + def * (kMax[b] - kMin[b]);
			cooperative = rand01();
		}
		if (teams_locked) cooperative = std::min(1.0f, cooperative + 0.30f);
		else if (allied_victory) cooperative = std::min(1.0f, cooperative + 0.15f);

		// difficultyLevel.js scaling (sandbox..veryHard)
		static const float kPop[6]     = { 0.50f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f }; // setConfig popScaling
		// InitGame.js: rate applies to gathering and trading, time to building,
		// training and technologies.
		static const float kRate[6]    = { 0.42f, 0.56f, 0.75f, 1.00f, 1.25f, 1.56f };
		static const float kTime[6]    = { 1.40f, 1.25f, 1.10f, 1.00f, 1.00f, 1.00f };
		// config.js criticalPopulationFactors / criticalStructureFactors / criticalRootFactors
		static const float kEmerg[6]   = { 0.80f, 0.80f, 0.70f, 0.60f, 0.50f, 0.35f };
		static const float kRoots[6]   = { 0.80f, 0.80f, 0.67f, 0.50f, 0.35f, 0.20f };
		uint8_t d = (difficulty > PETRA_VERY_HARD) ? PETRA_MEDIUM : difficulty;
		pop_scaling = kPop[d];
		gather_rate = kRate[d];
		build_time = kTime[d];
		emergency_pop_factor = kEmerg[d];
		emergency_struct_factor = kEmerg[d];
		emergency_root_factor = kRoots[d];

		defense_ratio_own = 2.0f; // config.js Defense.defenseRatio.own (ally 1.4 and neutral 1.8 derive from it)
		// config.js Economy.popPhase2 150 and workPhase3 180 (0.28); from Medium a strongly
		// aggressive personality goes to the town phase at 50. Below Easy it never cities.
		pop_phase2 = (d >= PETRA_MEDIUM && aggressive > 0.70f) ? 50 : 150;
		work_phase3 = d < PETRA_EASY ? 999 : 180;
	}
};

// queueManager.js — per-queue reserved stockpile (food/wood/gold/stone)
struct PetraQueueAccount {
	float food = 0.0f;
	float wood = 0.0f;
	float gold = 0.0f;
	float stone = 0.0f;
};

// queueManager.js queues, Config.priorities order. RTS0 keeps one upcoming attack plan,
// so the "plan_<name>" queues an attackPlan.js adds are three fixed ones here.
enum PetraQueue : uint8_t {
	PETRA_Q_VILLAGER = 0,
	PETRA_Q_CITIZEN_SOLDIER,
	PETRA_Q_TRADER,
	PETRA_Q_HEALER,
	PETRA_Q_SHIPS,
	PETRA_Q_HOUSE,
	PETRA_Q_DROPSITES,
	PETRA_Q_FIELD,
	PETRA_Q_DOCK,
	PETRA_Q_CORRAL,
	PETRA_Q_ECONOMIC_BUILDING,
	PETRA_Q_MILITARY_BUILDING,
	PETRA_Q_DEFENSE_BUILDING,
	PETRA_Q_CIVIL_CENTRE,
	PETRA_Q_MAJOR_TECH,
	PETRA_Q_MINOR_TECH,
	PETRA_Q_WONDER,
	PETRA_Q_EMERGENCY,
	PETRA_Q_PLAN,        // plan_<name>
	PETRA_Q_PLAN_CHAMP,  // plan_<name>_champ
	PETRA_Q_PLAN_SIEGE,  // plan_<name>_siege
	PETRA_Q_COUNT
};

// queueManager.js funds the queues that hold a plan, by priority, up to that plan's cost.
// RTS0's Petra pays where it decides rather than queueing plans: a queue "wants" the cost
// of the item it last failed to pay for, and the want lapses after WANT_TTL seconds without
// a retry, when the account's reserve returns to the free pool (queue emptied). Retries come
// on 0.28's played-turn cadences (every 3rd to 5th 1.6 s turn: up to 8 s), so a want outlives them.
struct PetraQueueManager {
	static constexpr int QUEUE_COUNT = PETRA_Q_COUNT;
	static constexpr float WANT_TTL = 10.0f;
	// Queues retried less often keep their want across one cadence: a new base every 10th
	// turn (checkBaseExpansion), a wonder every 30th (headquarters.js:2314).
	static constexpr float want_ttl(int q) {
		return q == PETRA_Q_WONDER ? 31 * 1.6f : q == PETRA_Q_CIVIL_CENTRE ? 11 * 1.6f : WANT_TTL;
	}
	PetraQueueAccount accounts[QUEUE_COUNT] = {};
	PetraQueueAccount wants[QUEUE_COUNT] = {};
	float want_age[QUEUE_COUNT] = {};
	int16_t priority[QUEUE_COUNT] = {};  // changePriority; 0 = the queue's Config.priorities value
	uint8_t paused[QUEUE_COUNT] = {};    // checkPausedQueues
	uint8_t switched[QUEUE_COUNT] = {};  // switchResource: drawn on once until it starts an item
	uint8_t reset_on_start[QUEUE_COUNT] = {}; // queueToReset: 1 + the queue whose priority resets when this one starts its item
	// researchTradeBonus / researchPopulationBonus: the technology they queued in minorTech,
	// waiting to be paid for (0: none); every other minor technology waits behind it.
	uint8_t special_tech = 0;
	uint8_t pad[2] = {};
	void reset() {
		for (int i = 0; i < QUEUE_COUNT; ++i) {
			accounts[i] = PetraQueueAccount{};
			wants[i] = PetraQueueAccount{};
			want_age[i] = want_ttl(i) + 1.0f;
			priority[i] = 0;
			paused[i] = switched[i] = reset_on_start[i] = 0;
		}
		special_tech = 0;
	}
};
static_assert(sizeof(PetraQueueManager) == 864, "no hidden padding: saved raw");
// The six-queue layout of saves before v60 (emergency, villager, military, tech, trade, naval).
struct PetraQueueManagerV59 {
	PetraQueueAccount accounts[6];
	PetraQueueAccount wants[6];
	float want_age[6];
};

// transportPlan.js — land-sea-land boarding FSM
struct PetraTransportPlan {
	enum State : uint8_t {
		IDLE      = 0,
		BOARDING  = 1,
		SAILING   = 2,
		UNLOADING = 3,
		FAILED    = 4
	} state = IDLE;
	uint32_t ship_id = 0;
	std::vector<uint32_t> passenger_ids;
	float pickup_x = 0.0f, pickup_z = 0.0f;
	float drop_x = 0.0f, drop_z = 0.0f;
	float state_timer = 0.0f;
	bool failed = false;
	uint8_t owner = 0; // PetraTransportOwner: the plan that asked for it
	uint16_t sea = 0;  // the sea it crosses (0: any, from an older save)
	// transportPlan.js onBoarding: each passenger's position at the last 2 s check and how
	// often it was found blocked beside the ship (posGarrison, nTry).
	// best: its nearest approach to the ship so far. A unit sent off a trap that walks back into it
	// has moved but come no nearer, so it keeps its tries.
	struct BoardTry { uint32_t uid = 0; float x = 0.0f, z = 0.0f; uint32_t tries = 0; float best = 0.0f; };
	std::vector<BoardTry> boarding;
	float board_check = 0.0f; // seconds since the last check
};
// navalManager.js runs a transportPlan per ship; each asker here has at most one.
// A crossing asked for again in the same pass (survivors of a sunk ship on another land).
struct PetraReferry {
	std::vector<uint32_t> unit_ids;
	float from_x = 0.0f, from_z = 0.0f, to_x = 0.0f, to_z = 0.0f;
	uint16_t sea = 0;
	uint8_t owner = 0;
};
enum PetraTransportOwner : uint8_t { PETRA_TRANSPORT_ATTACK = 0, PETRA_TRANSPORT_RAID = 1, PETRA_TRANSPORT_BUILDERS = 2, PETRA_TRANSPORT_TRADER = 3,
                                      PETRA_TRANSPORT_WORKERS = 4,
                                      PETRA_TRANSPORT_DEFENSE = 5 }; // defenders for a foe on another land

// chatHelper.js: what a Petra line says; petra_chat.h holds each kind's lines.
enum PetraChatKind : uint8_t {
	PETRA_CHAT_ATTACK = 0,           // launchAttackMessages.other, player = the target (to allies)
	PETRA_CHAT_HUGE_ATTACK,          // launchAttackMessages.hugeAttack
	PETRA_CHAT_ATTACK_JOIN,          // answerRequestAttackMessages.join, player = the target asked for
	PETRA_CHAT_ATTACK_DECLINE,       // answerRequestAttackMessages.decline
	PETRA_CHAT_ATTACK_OTHER,         // answerRequestAttackMessages.other, player2 = our own target
	PETRA_CHAT_TRIBUTE,              // sentTributeMessages, player = who got it (to allies)
	PETRA_CHAT_TRIBUTE_REQUEST,      // requestTributeMessages, resource (to allies)
	PETRA_CHAT_TRADE_ROUTE,          // newTradeRouteMessages, player = the route's other owner (to allies)
	PETRA_CHAT_NEW_DIPLOMACY,        // newDiplomacyMessages[sub: PetraChatStance] (to everyone)
	// answerDiplomacyRequestMessages[sub: 0 ally, 1 neutral][...], to the player who asked
	PETRA_CHAT_ANSWER_DECLINE,
	PETRA_CHAT_ANSWER_DECLINE_SUGGEST_NEUTRAL, // ally only
	PETRA_CHAT_ANSWER_DECLINE_REPEATED,
	PETRA_CHAT_ANSWER_ACCEPT,
	PETRA_CHAT_ANSWER_ACCEPT_WITH_TRIBUTE,     // amount, resource
	PETRA_CHAT_ANSWER_WAITING_FOR_TRIBUTE,     // amount, resource
	// sendDiplomacyRequestMessages[sub][...], to the player asked
	PETRA_CHAT_SEND_REQUEST,
	PETRA_CHAT_REQUEST_EXPIRED,
	PETRA_CHAT_EMERGENCY,            // emergencyMessages.enter (to allies)
	PETRA_CHAT_EMERGENCY_EXIT,       // emergencyMessages.exit
	// Commands.js "attack-request": from the player asking, human or AI, player = the target (to allies)
	PETRA_CHAT_ATTACK_REQUESTED,
	PETRA_CHAT_KIND_COUNT
};
enum PetraChatStance : uint8_t { PETRA_STANCE_ALLY = 0, PETRA_STANCE_NEUTRAL = 1, PETRA_STANCE_ENEMY = 2 };
// PetraChatLine::to besides a player id: "/allies" and the everyone chat.
static constexpr uint8_t PETRA_CHAT_ALLIES = 0xFE;
static constexpr uint8_t PETRA_CHAT_EVERYONE = 0xFF;


// diplomacyManager.js receivedDiplomacyRequests: what one AI decided about a
// player that changed its stance toward it (one entry per player).
struct PetraReceivedRequest {
	enum Status : uint8_t {
		NONE = 0,
		DECLINED_FOREVER = 1,
		WAITING_FOR_TRIBUTE = 2,
		ACCEPTED = 3,
		ALLIANCE_BROKEN = 4 // allianceBroken: it left the alliance we accepted
	} status = NONE;
	uint8_t type = 0;        // 0 = ally, 1 = neutral
	uint8_t resource = 0;
	bool sent_warning = false;
	int wanted = 0;
	float paid = 0.0f;       // tribute of `resource` received since the request
	float warn_time = 0.0f;  // AI elapsed time of the next warning or lapse
};

// Per-AI diplomacy state (swapped with the other Petra slot state).
struct PetraDiplomacy {
	PetraReceivedRequest received[8];
	uint8_t seen_stance[8] = {};   // each player's stance toward us at the last check
	bool seen_init = false;
	bool waiting_to_betray = false;
	// A hash of every player's stances, the defeated players and the ceasefire at our last turn:
	// a change is 0.28's DiplomacyChanged, PlayerDefeated or CeasefireEnded event, on which
	// checkEvents runs lastManStandingCheck. In the padding before betray_lapse (no save bump).
	// Its top bit is PETRA_EVENT_PENDING: a DiplomacyChanged since our last turn, which the hash
	// alone misses when the stance went back before the turn.
	uint16_t events_hash = 0;
	float betray_lapse = -1.0f;
	float elapsed = 0.0f;
	float next_tribute = 90.0f;  // nextTributeUpdate
};

constexpr uint16_t PETRA_EVENT_PENDING = 0x8000;

// PetraSentDiplomacy::current_enemy when currentEnemyPlayer is Gaia (player 0 in 0.28).
constexpr uint8_t PETRA_CURRENT_ENEMY_GAIA = 0xFF;

// diplomacyManager.js sentDiplomacyRequests, sentDiplomacyRequestLapseTime and
// nextTributeRequest (save v27). Padding-free: saved raw.
struct PetraSentDiplomacy {
	float sent_time[8] = {};          // when we asked each player
	float sent_lapse = -1.0f;         // next chance to ask anyone (set on the first update)
	float next_tribute_request_all = 240.0f;
	float next_tribute_request[4] = {};  // per resource
	uint8_t sent_type[8] = {};        // 0 none, 1 ally, 2 neutral
	uint8_t warned_trade = 0;         // tradeManager.js warnedAllies: players told of a route with them, by bit
	uint8_t current_enemy = 0;        // attackManager.js currentEnemyPlayer + 1 (0: none, PETRA_CURRENT_ENEMY_GAIA: Gaia), kept between attacks
	uint8_t pad[6] = {};
};
static_assert(sizeof(PetraSentDiplomacy) == 72, "PetraSentDiplomacy is saved raw: keep it padding-free");

// chatHelper.js — short English taunts (no i18n)
struct PetraDiplomacyRequest {
	enum Status : uint8_t {
		WAITING = 0,
		ACCEPTED = 1,
		DECLINED = 2
	} status = WAITING;
	uint8_t request_type = 0; // 0=ally, 1=neutral
	float warn_time = 0.0f;
	float lapse_time = 0.0f;
	int wanted = 0;
	uint8_t resource = 0;
};

struct PetraChatLine {
	uint8_t kind = PETRA_CHAT_ATTACK;
	uint8_t from = 1;                   // the AI player speaking
	uint8_t to = PETRA_CHAT_EVERYONE;   // a player ("/msg"), PETRA_CHAT_ALLIES or PETRA_CHAT_EVERYONE
	uint8_t variant = 0;                // which of the kind's lines (chatHelper.js pickRandom)
	uint8_t player = 0xFF;              // %(_player_)s
	uint8_t player2 = 0xFF;             // %(_player_2)s
	uint8_t sub = 0;                    // request type (0 ally, 1 neutral) or PetraChatStance
	uint8_t resource = 0;               // %(resource)s / %(_resource_)s, a ResourceType
	int32_t amount = 0;                 // %(_amount_)s
	float x = 0.0f;                     // where a ping points (a tribute, an attack)
	float z = 0.0f;
};
static_assert(sizeof(PetraChatLine) == 20, "PetraChatLine is saved field by field: list a new one in tick_state_io");

// defenseArmy.js: one group of foes close together and the defenders sent against it.
// Its position (foePosition) and strengths are computed from the live units when used.
struct PetraDefenseArmy {
	std::vector<uint32_t> defender_ids;
	std::vector<uint32_t> foe_ids;
	// defenseArmy.js positionLastUpdate: game time of the last position update; breakaways
	// are checked once it is more than 5 s old (0 after a foe joins: checked next update).
	float position_last_update = 0.0f;
};
// config.js Defense, squared metres: a foe joins an army within armyCompactSize of its
// position (PetraConfig::army_compact_sq), breaks away beyond armyBreakawaySize, and two
// armies closer than armyMergeSize merge.
inline constexpr float PETRA_ARMY_BREAKAWAY_SQ = 3500.0f;
inline constexpr float PETRA_ARMY_MERGE_SQ = 1400.0f;

// headquarters.js buildDefenses: when each kind was last queued (seconds of game time) and the
// defenseBuilding queue's plan, the structure type + 1 (0: none).
struct PetraDefenseBuild {
	float tower_start = 0.0f;    // towerStartTime: stone towers
	float fortress_start = 0.0f; // fortressStartTime
	float fort_start = 180.0f;   // fortStartTime: sentry towers, from fortStartTime + towerLapseTime
	uint32_t pending = 0;
};

// baseManager.js gatherers[type] for wood, stone and metal: the played turn of the next check and
// the gatherers counted gathering (used) and walking to a dropsite (lost) since the last one.
struct PetraDropsiteCheck {
	int32_t next_check[3] = {};
	int32_t used[3] = {};
	int32_t lost[3] = {};
	// The storehouse a check decided on that couldn't be paid yet (queues.dropsites keeps the plan
	// until it is funded): its resource + 1, 0 for none. The counts wait while it is pending.
	uint8_t pending = 0;
	uint8_t pad[3] = {};
};

// headquarters.js lastFailedGather: the AI time a worker last found nothing of each resource to
// gather (worker.js startGathering); for 20 s after, no idle worker is sent to it (baseManager.js
// reassignIdleWorkers, setWorkersIdleByPriority). 0 = never, as 0.28's falsy check reads it.
struct PetraFailedGather {
	float time[RES_COUNT] = {};
	bool recent(uint8_t r, float now) const { return time[r] != 0.0f && now - time[r] < 20.0f; }
};

// What 0 A.D. decides once and keeps (saved): startingStrategy.js configFirstBase's needFish and
// needFarm and navalManager.js's wantedFishShips, decided with the first civic centre; the
// wantedTransportShips that only grow (setMinimalTransportShips, checkLevels); and
// tradeManager.js targetNumTraders, set by setTradingGoods. Seas by nav region id.
struct PetraKept {
	bool decided = false;   // configFirstBase has run
	bool need_fish = false; // a small starting land short of food: dock at once, a fishing boat goes on
	bool need_farm = false; // a large one short of food: the first field goes on
	bool need_corral = false; // a starting land under 25000 m²: the first corral goes on
	int16_t target_traders = -1; // -1 until setTradingGoods sets it: the config value
	std::vector<uint8_t> wanted_fish, wanted_transport;
	// startingStrategy.js gameStart: with no civic centre and none it may build, the AI can't
	// produce units (canBuildUnits false): every unit joins the attacks, but those dispatched
	// to an ally's civic centre (dispatchUnits, metadata "allied").
	bool units_checked = false;
	bool no_units = false;
	std::vector<uint32_t> allied;
	// headquarters.js buildMoreHouses: house plans waiting in queues.house (up to 3, 5 above 80
	// population), started once houseNeeded holds (saved v113).
	uint8_t house_plans = 0;
	// headquarters.js handleNewBase, for each base anchor finished or captured after the first:
	// saveResources, saveSpace and maxFields end for good; a finished market ends maxFields too.
	// anchors_seen: the finished anchors met so far, sorted ids (saved v113).
	std::vector<uint32_t> anchors_seen;
	bool space_freed = false, fields_freed = false;
	// basesManager.js update: currentBase, the next base in anchor order to update (saved v115).
	uint16_t current_base = 0;
	// headquarters.js requireHouses: set when the phase needs houses and there is no room for
	// one; petra_find_position then leaves out every structure's influence for houses, until
	// the phase's Village count is met or no phase is waiting (saved v116).
	bool require_houses = false;
	// basesManager.js keeps a base whose civic centre was lost (anchorLost), anchorless, for good:
	// createBase hands the next civic centre on its land that base, through a reset that zeroes
	// its neededDefenders. The lost anchors' positions, until a new one takes the base (saved v127).
	std::vector<std::pair<float, float>> lost_bases;
	static uint8_t at(const std::vector<uint8_t> &v, size_t s) { return s < v.size() ? v[s] : 0; }
	static void raise(std::vector<uint8_t> &v, size_t s, int n) {
		if (s >= v.size()) v.resize(s + 1, 0);
		v[s] = (uint8_t)std::clamp(std::max<int>(v[s], n), 0, 255);
	}
};

// emergencyManager.js
struct PetraEmergency {
	int ref_pop = 0;
	int ref_structs = 0;
	int ref_roots = 0;
	bool active = false;
};

// attackPlan.js plan types, chosen by attackManager.js update().
enum PetraAttackKind : uint8_t {
	PETRA_ATTACK_RUSH   = 0, // village-phase infantry strike on an undefended structure
	PETRA_ATTACK_ATTACK = 1, // "Attack": the default town-phase army
	PETRA_ATTACK_HUGE   = 2, // "HugeAttack": from the third default attack on
	PETRA_ATTACK_RAID   = 3, // cavalry against an enemy structure in our territory
	PETRA_ATTACK_KINDS  = 4,
	// An "Attack" (TYPE_DEFAULT) on one target only, made from a disbanding defense army
	// (defenseManager.js switchToAttack, uniqueTargetId); it ends with its target. Not a
	// plan the manager launches, so launched[] has no slot for it.
	PETRA_ATTACK_UNIQUE = 4
};

// attackManager.js state for one AI. Saved raw (save v18), so it has no
// implicit padding: the uint8 fields fill a whole 4-byte word.
struct PetraAttackManager {
	uint8_t rushes_set = 0;   // setRushes ran (headquarters.js gameStart)
	uint8_t max_rushes = 0;
	uint8_t rush_number = 0;  // rushes planned so far
	uint8_t attack_number = 0; // Attack/HugeAttack plans so far
	uint8_t kind = PETRA_ATTACK_ATTACK; // kind of the main plan
	uint8_t save_resources = 0; // headquarters.js saveResources: little wood at the start
	uint8_t missing_room = 0; // unused since v94 (Simulation::petra_room_); kept for the saved layout
	// victoryManager.js tryCaptureGaiaRelicLapseTime: 2 s steps left before an attack may go
	// for Gaia's relics again (0: it may, tryCaptureGaiaRelic).
	uint8_t gaia_relic_lapse = 0;
	// attackManager.js totalNumber: every plan made so far (rush, raid, attack, huge, unique);
	// a plan's name is the count when it was made. attack_name is the main plan's.
	uint8_t plans_made = 0;
	uint8_t attack_name = 0;
	// attackPlan.js addSiegeUnits SIEGE_ADDED: the main plan's siege class (1-3), 0 until picked.
	uint8_t attack_siege = 0;
	// The raid plan's unitStat FastMoving sizes (3 and 4 before variation and popScaling).
	uint8_t raid_target = 4;
	uint8_t raid_min = 3;
	// startingStrategy.js cuts Config.Economy.popPhase2 to 3/4 for good with saveResources; set when
	// handleNewBase clears saveResources, so the cut outlives it (0 in older saves, which kept
	// the cut only while saving).
	uint8_t phase2_cut_kept = 0;
	// attackPlan.js trainMoreUnits deletes a unitStat that no template of ours trains (HugeAttack's
	// champions and hero before the city phase): a bit per class of the main plan, by its index in
	// petra_plan_unit_stats; cleared when a new plan is made. (Was padding; 0 in older saves.)
	uint16_t dropped_classes = 0;
	int32_t rush_size[3] = {};
	int32_t target_size = 0;  // main plan's unitStat targetSize total
	int32_t min_size = 0;     // and minSize total
	int32_t launched[PETRA_ATTACK_KINDS] = {}; // plans of each kind that marched
	// The raid plan's name (totalNumber when it was made) and its addSiegeUnits state: 0 not
	// tested, 1-3 the siege class added (SIEGE_ADDED), 0xFF no trainer yet (SIEGE_NO_TRAINER).
	// (Save v95; the raid had no siege before.)
	uint8_t raid_name = 0;
	uint8_t raid_siege = 0;
	// headquarters.js findBestTrainableUnit copies the plan's interests only shallowly, so a
	// scarcity penalty lowers the main plan's own costsResource weight for good: 2 bits per
	// (unitStat index 0-3, stone then metal), 0 its table weight, 1 at most 0.6, 2 at most 0.2;
	// cleared when a new plan is made. (Was padding; 0 in older saves.)
	uint16_t cost_lowered = 0;
};
static_assert(sizeof(PetraAttackManager) == 56, "PetraAttackManager is saved raw; keep it padding-free");

// navalManager.js:58 seaWarShips — per-sea fleet accounting registry
struct Petra {
	static constexpr int kMaxSeas = 8;
	uint32_t sea_warships[kMaxSeas][16] = {};
	uint8_t sea_warship_count[kMaxSeas] = {};
	int sea_count_ = 0;

	inline void clear_sea_fleets() {
		for (int s = 0; s < kMaxSeas; ++s) {
			sea_warship_count[s] = 0;
			std::memset(sea_warships[s], 0, sizeof(sea_warships[s]));
		}
		sea_count_ = 0;
	}
};
using PetraFleetRegistry = Petra;
using PetraSeaRegistry = Petra;


} // namespace rtso

#endif
