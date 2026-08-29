#ifndef RTSO_PETRA_AI_H
#define RTSO_PETRA_AI_H

// Native collapse of 0 A.D. ai/petra/{config,difficultyLevel,defenseManager,
// defenseArmy,garrisonManager,emergencyManager,diplomacyManager,victoryManager}.js
// Wired from Simulation::tick_petra_ai. Vendor JS remains reference-only.

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

// config.js + difficultyLevel.js
struct PetraConfig {
	uint8_t difficulty = PETRA_MEDIUM;
	uint8_t behavior   = PETRA_BEHAVIOR_BALANCED;

	float aggressive   = 0.5f;
	float defensive    = 0.5f;
	float cooperative  = 0.5f;
	float pop_scaling  = 1.0f;
	float trickle_mult = 1.0f;

	float defense_ratio_own = 2.0f;   // Defense.defenseRatio.own
	float army_compact_sq   = 2000.0f; // Defense.armyCompactSize
	int   target_num_workers = 60;
	int   target_num_traders = 3;
	int   pop_phase2         = 55;
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
		static const float kTrickle[6] = { 0.35f, 0.55f, 0.80f, 1.00f, 1.25f, 1.50f };
		static const int   kWorkers[6] = { 18, 28, 40, 60, 80, 100 };
		static const int   kTraders[6] = { 0, 1, 2, 3, 4, 5 };
		static const float kEmerg[6]   = { 0.80f, 0.80f, 0.70f, 0.60f, 0.50f, 0.35f };
		uint8_t d = (difficulty > PETRA_VERY_HARD) ? PETRA_MEDIUM : difficulty;
		pop_scaling = kPop[d];
		trickle_mult = kTrickle[d];
		target_num_workers = kWorkers[d];
		target_num_traders = kTraders[d];
		emergency_pop_factor = kEmerg[d];
		emergency_struct_factor = kEmerg[d];
		emergency_root_factor = std::max(0.20f, kEmerg[d] - 0.10f);

		defense_ratio_own = 1.6f + 0.8f * defensive + 0.15f * (float)d;
		muster_delta = 0;
		if (aggressive > 0.70f) muster_delta = -2;
		if (defensive > 0.70f) muster_delta = 2;
		pop_phase2 = (aggressive > 0.70f) ? 50 : 55;
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

struct PetraQueueManager {
	static constexpr int QUEUE_COUNT = 6;
	PetraQueueAccount accounts[QUEUE_COUNT] = {};
	void reset() {
		for (int i = 0; i < QUEUE_COUNT; ++i) accounts[i] = PetraQueueAccount{};
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
	PETRA_CHAT_EMERGENCY = 3
};

// chatHelper.js — short English taunts (no i18n)
struct PetraChatLine {
	uint8_t kind = PETRA_CHAT_ATTACK;
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

} // namespace rtso

#endif
