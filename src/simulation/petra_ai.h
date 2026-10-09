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
	       t == UNIT_FISHING_BOAT || t == UNIT_MERCHANT_SHIP || t == UNIT_WORKER_ELEPHANT;
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
	int   target_num_workers = 60;
	int   target_num_traders = 3;
	int   pop_phase2         = 38;   // config.js Economy.popPhase2
	int   work_phase3        = 40;   // config.js Economy.workPhase3 (scaled down for our pop cap)
	int   muster_delta       = 0;     // +/- army threshold from personality

	float garrison_hp_low  = 0.40f;
	float garrison_hp_high = 0.70f;
	float emergency_pop_factor    = 0.60f;
	float emergency_struct_factor = 0.60f;
	float emergency_root_factor   = 0.50f;

	// queueManager.js Config.priorities
	int prio_emergency = 1000;
	int prio_villager  = 300;
	int prio_military  = 600;
	int prio_tech      = 700;
	int prio_trade     = 1;
	int prio_naval     = 90;

	void recompute() {
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

		// difficultyLevel.js scaling (sandbox..veryHard)
		static const float kPop[6]     = { 0.40f, 0.50f, 0.70f, 1.00f, 1.15f, 1.30f };
		// InitGame.js: rate applies to gathering and trading, time to building,
		// training and technologies.
		static const float kRate[6]    = { 0.42f, 0.56f, 0.75f, 1.00f, 1.25f, 1.56f };
		static const float kTime[6]    = { 1.40f, 1.25f, 1.10f, 1.00f, 1.00f, 1.00f };
		static const int   kWorkers[6] = { 18, 28, 40, 60, 80, 100 };
		static const int   kTraders[6] = { 0, 1, 2, 3, 4, 5 };
		// config.js criticalPopulationFactors / criticalStructureFactors / criticalRootFactors
		static const float kEmerg[6]   = { 0.80f, 0.80f, 0.70f, 0.60f, 0.50f, 0.35f };
		static const float kRoots[6]   = { 0.80f, 0.80f, 0.67f, 0.50f, 0.35f, 0.20f };
		uint8_t d = (difficulty > PETRA_VERY_HARD) ? PETRA_MEDIUM : difficulty;
		pop_scaling = kPop[d];
		gather_rate = kRate[d];
		build_time = kTime[d];
		target_num_workers = kWorkers[d];
		target_num_traders = kTraders[d];
		emergency_pop_factor = kEmerg[d];
		emergency_struct_factor = kEmerg[d];
		emergency_root_factor = kRoots[d];

		defense_ratio_own = 1.6f + 0.8f * defensive + 0.15f * (float)d;
		muster_delta = 0;
		if (aggressive > 0.70f) muster_delta = -2;
		if (defensive > 0.70f) muster_delta = 2;
		// config.js Economy.popPhase2 = 38; strongly aggressive personalities
		// rush and delay the town phase to 50.
		pop_phase2 = (aggressive > 0.70f) ? 50 : 38;
		// config.js:314–319 popScaling clamp; workPhase3 floor 40 in 0 A.D., smaller here
		pop_phase2 = std::max(20, (int)((float)pop_phase2 * std::min(1.0f, pop_scaling)));
		work_phase3 = std::max(18, (int)(40.0f * pop_scaling));
		if (difficulty == PETRA_SANDBOX)
			work_phase3 = 999; // config.js:322 sandbox never cities
		prio_emergency = 1000;
		prio_villager  = 300;
		prio_military  = (aggressive > 0.70f) ? 720 : 600;
		prio_tech      = 700;
		prio_trade     = 1;
		prio_naval     = 90;
		if (defensive > 0.70f) prio_emergency += 200;
	}
};

// queueManager.js — per-queue reserved stockpile (food/wood/gold/stone)
struct PetraQueueAccount {
	float food = 0.0f;
	float wood = 0.0f;
	float gold = 0.0f;
	float stone = 0.0f;
};

// queueManager.js only funds queues that hold a plan, and only up to that
// plan's cost. A queue "wants" the cost of the item it last failed to pay for;
// the want lapses after WANT_TTL seconds without a retry and the account's
// reserve returns to the free pool, so idle queues cannot hoard income.
struct PetraQueueManager {
	static constexpr int QUEUE_COUNT = 6;
	static constexpr float WANT_TTL = 5.0f;
	PetraQueueAccount accounts[QUEUE_COUNT] = {};
	PetraQueueAccount wants[QUEUE_COUNT] = {};
	float want_age[QUEUE_COUNT] = {};
	void reset() {
		for (int i = 0; i < QUEUE_COUNT; ++i) {
			accounts[i] = PetraQueueAccount{};
			wants[i] = PetraQueueAccount{};
			want_age[i] = WANT_TTL + 1.0f;
		}
	}
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
};

enum PetraChatKind : uint8_t {
	PETRA_CHAT_ATTACK    = 0,
	PETRA_CHAT_TRIBUTE   = 1,
	PETRA_CHAT_BETRAYAL  = 2,
	PETRA_CHAT_EMERGENCY = 3,
	// diplomacyManager.js answers to a player who changed its stance toward us
	PETRA_CHAT_DIPLO_ACCEPT   = 4,
	PETRA_CHAT_DIPLO_DECLINE  = 5,
	PETRA_CHAT_DIPLO_TRIBUTE  = 6, // accept once the player pays a tribute
	PETRA_CHAT_DIPLO_WARNING  = 7, // the tribute has not arrived yet
	PETRA_CHAT_DIPLO_LAPSED   = 8,
	PETRA_CHAT_WAR_DECLARED   = 9, // answering a declaration of war
	// diplomacyManager.js sendDiplomacyRequest / checkSentDiplomacyRequests, tributes()
	PETRA_CHAT_ALLY_REQUEST     = 10,
	PETRA_CHAT_NEUTRAL_REQUEST  = 11,
	PETRA_CHAT_ALLY_EXPIRED     = 12,
	PETRA_CHAT_NEUTRAL_EXPIRED  = 13,
	PETRA_CHAT_TRIBUTE_REQUEST  = 14, // to an ally: "I am in need of <res>"
	PETRA_CHAT_EMERGENCY_EXIT   = 15  // emergencyManager.js: the emergency is over
};

// diplomacyManager.js receivedDiplomacyRequests: what one AI decided about a
// player that changed its stance toward it (one entry per player).
struct PetraReceivedRequest {
	enum Status : uint8_t {
		NONE = 0,
		DECLINED_FOREVER = 1,
		WAITING_FOR_TRIBUTE = 2,
		ACCEPTED = 3
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
	float betray_lapse = -1.0f;
	float elapsed = 0.0f;
	float next_tribute = 90.0f;  // nextTributeUpdate
};

// diplomacyManager.js sentDiplomacyRequests, sentDiplomacyRequestLapseTime and
// nextTributeRequest (save v27). Padding-free: saved raw.
struct PetraSentDiplomacy {
	float sent_time[8] = {};          // when we asked each player
	float sent_lapse = -1.0f;         // next chance to ask anyone (set on the first update)
	float next_tribute_request_all = 240.0f;
	float next_tribute_request[4] = {};  // per resource
	uint8_t sent_type[8] = {};        // 0 none, 1 ally, 2 neutral
};
static_assert(sizeof(PetraSentDiplomacy) == 64, "PetraSentDiplomacy is saved raw: keep it padding-free");

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
	uint8_t from = 1;    // the AI player speaking
	uint8_t to = 0xFF;   // addressee, 0xFF = everyone
	float x = 0.0f;
	float z = 0.0f;
	char text[96] = {};
};

// defenseArmy.js — one compact home army
struct PetraDefenseArmy {
	std::vector<uint32_t> defender_ids;
	std::vector<uint32_t> foe_ids;
	float cx = 0.0f;
	float cz = 0.0f;
	bool active = false;
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
	PETRA_ATTACK_KINDS  = 4
};

// attackManager.js state for one AI. Saved raw (save v18), so it has no
// implicit padding: the uint8 fields fill a whole 4-byte word.
struct PetraAttackManager {
	uint8_t rushes_set = 0;   // setRushes ran (headquarters.js gameStart)
	uint8_t max_rushes = 0;
	uint8_t rush_number = 0;  // rushes planned so far
	uint8_t attack_number = 0; // Attack/HugeAttack plans so far
	uint8_t kind = PETRA_ATTACK_ATTACK; // kind of the main plan
	uint8_t pad[3] = {};
	int32_t rush_size[3] = {};
	int32_t target_size = 0;  // main plan's unitStat targetSize total
	int32_t min_size = 0;     // and minSize total
	int32_t launched[PETRA_ATTACK_KINDS] = {}; // plans of each kind that marched
};
static_assert(sizeof(PetraAttackManager) == 44, "PetraAttackManager is saved raw; keep it padding-free");

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
