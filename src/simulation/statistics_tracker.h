#ifndef RTSO_STATISTICS_TRACKER_H
#define RTSO_STATISTICS_TRACKER_H

// Per-player match statistics, ported from 0 A.D.
// binaries/data/mods/public/simulation/components/StatisticsTracker.js
// (unit and structure counters by class, their resource values, resources
// gathered / used / sold / bought, tributes, trade income, treasures, loot,
// map explored and controlled) and the 30-second sequences it keeps for the
// summary screen's charts (StatisticsTracker.prototype.updateTimeInterval).
//
// Every struct here holds 4-byte fields only, so it has no padding and is
// saved raw through tick_state_io.

#include <cstdint>
#include "sim_types.h"

namespace rtso {

// StatisticsTracker.prototype.unitsClasses. A unit can be in several classes
// (a citizen spearman is Infantry and Worker); the last slot is the total,
// which counts each unit once.
enum StatUnitClass : uint8_t {
	STAT_UNIT_INFANTRY = 0,
	STAT_UNIT_WORKER,
	STAT_UNIT_FEMALE_CITIZEN,
	STAT_UNIT_CAVALRY,
	STAT_UNIT_CHAMPION,
	STAT_UNIT_HERO,
	STAT_UNIT_SIEGE,
	STAT_UNIT_SHIP,
	STAT_UNIT_TRADER,
	STAT_UNIT_TOTAL,
	STAT_UNIT_SLOTS
};

// StatisticsTracker.prototype.buildingsClasses, plus the total.
enum StatBuildingClass : uint8_t {
	STAT_BLD_HOUSE = 0,
	STAT_BLD_ECONOMIC,
	STAT_BLD_OUTPOST,
	STAT_BLD_MILITARY,
	STAT_BLD_FORTRESS,
	STAT_BLD_CIV_CENTRE,
	STAT_BLD_WONDER,
	STAT_BLD_TOTAL,
	STAT_BLD_SLOTS
};

struct PlayerStatistics {
	uint32_t units_trained[STAT_UNIT_SLOTS] = {};
	uint32_t units_lost[STAT_UNIT_SLOTS] = {};
	uint32_t enemy_units_killed[STAT_UNIT_SLOTS] = {};
	uint32_t units_lost_value = 0;
	uint32_t enemy_units_killed_value = 0;
	uint32_t buildings_constructed[STAT_BLD_SLOTS] = {};
	uint32_t buildings_lost[STAT_BLD_SLOTS] = {};
	uint32_t enemy_buildings_destroyed[STAT_BLD_SLOTS] = {};
	uint32_t buildings_captured[STAT_BLD_SLOTS] = {};
	uint32_t buildings_lost_value = 0;
	uint32_t enemy_buildings_destroyed_value = 0;
	uint32_t buildings_captured_value = 0;
	// Indexed by ResourceType (wood, metal, stone, food).
	uint32_t resources_gathered[RES_COUNT] = {};
	uint32_t vegetarian_food_gathered = 0; // fruit and grain
	int32_t resources_used[RES_COUNT] = {}; // refunds (cancelled training/upgrades) take it back
	uint32_t resources_sold[RES_COUNT] = {};
	uint32_t resources_bought[RES_COUNT] = {};
	uint32_t tributes_sent = 0;
	uint32_t tributes_received = 0;
	uint32_t trade_income = 0;
	uint32_t treasures_collected = 0;
	uint32_t loot_collected = 0;
	float peak_percent_map_controlled = 0.0f;
	float team_peak_percent_map_controlled = 0.0f;
};

// One row of the sequences: every player's counters at `time` seconds.
struct StatisticsSample {
	float time = 0.0f;
	PlayerStatistics players[8];
	float percent_map_explored[8] = {};
	float team_percent_map_explored[8] = {};
	float percent_map_controlled[8] = {};
	float team_percent_map_controlled[8] = {};
};

// StatisticsTracker.prototype.updateTimeInterval (30000 ms).
inline constexpr float STATISTICS_SAMPLE_INTERVAL = 30.0f;
// Map-explored cells are this many metres wide: coarse enough to stay cheap,
// fine enough for a percentage.
inline constexpr float STATISTICS_EXPLORE_CELL = 8.0f;

inline const char *stat_unit_class_name(uint8_t c) {
	static const char *k[STAT_UNIT_SLOTS] = {"Infantry", "Worker", "FemaleCitizen", "Cavalry", "Champion",
	                                         "Hero", "Siege", "Ship", "Trader", "total"};
	return c < STAT_UNIT_SLOTS ? k[c] : "";
}

inline const char *stat_building_class_name(uint8_t c) {
	static const char *k[STAT_BLD_SLOTS] = {"House", "Economic", "Outpost", "Military", "Fortress",
	                                        "CivCentre", "Wonder", "total"};
	return c < STAT_BLD_SLOTS ? k[c] : "";
}

// The 0 A.D. classes a unit of this behaviour type and roster flags carries
// (template_unit_*.xml VisibleClasses), as a bit mask over StatUnitClass.
inline uint32_t stat_unit_classes(uint8_t unit_type, uint8_t roster_flags) {
	uint32_t m = 0;
	auto set = [&](StatUnitClass c) { m |= 1u << c; };
	switch (unit_type) {
	case UNIT_VILLAGER: set(STAT_UNIT_WORKER); break;
	case UNIT_FEMALE_CITIZEN: set(STAT_UNIT_WORKER); set(STAT_UNIT_FEMALE_CITIZEN); break;
	case UNIT_WORKER_ELEPHANT: set(STAT_UNIT_WORKER); break;
	case UNIT_SWORDSMAN: case UNIT_ARCHER: case UNIT_SPEARMAN: case UNIT_ARCHER_MAIDEN: case UNIT_CARNYX:
		set(STAT_UNIT_INFANTRY); break;
	case UNIT_CAVALRY: case UNIT_CHARIOT: case UNIT_CAMEL: case UNIT_SCYTHED_CHARIOT: set(STAT_UNIT_CAVALRY); break;
	case UNIT_ELEPHANT: set(STAT_UNIT_CHAMPION); break;
	case UNIT_HERO: set(STAT_UNIT_HERO); break;
	case UNIT_CATAPULT: case UNIT_SIEGE_RAM: case UNIT_BOLT_SHOOTER: set(STAT_UNIT_SIEGE); break;
	case UNIT_TRIREME: case UNIT_FISHING_BOAT: set(STAT_UNIT_SHIP); break;
	case UNIT_MERCHANT_SHIP: set(STAT_UNIT_SHIP); set(STAT_UNIT_TRADER); break;
	case UNIT_TRADER: set(STAT_UNIT_TRADER); break;
	default: break;
	}
	if (roster_flags & ROSTER_HERO) set(STAT_UNIT_HERO);
	if (roster_flags & ROSTER_CHAMPION) set(STAT_UNIT_CHAMPION);
	// Citizen soldiers gather and build (template_unit_infantry/cavalry: Worker).
	if ((roster_flags & ROSTER_CITIZEN) && (m & ((1u << STAT_UNIT_INFANTRY) | (1u << STAT_UNIT_CAVALRY)))) set(STAT_UNIT_WORKER);
	if (roster_flags & ROSTER_SIEGE_TOWER) set(STAT_UNIT_SIEGE);
	// 0 A.D. champions and heroes are not counted as citizen infantry or cavalry.
	if (m & ((1u << STAT_UNIT_HERO) | (1u << STAT_UNIT_CHAMPION))) m &= ~(1u << STAT_UNIT_WORKER);
	return m;
}

// The 0 A.D. class of a structure type (template_structure_*.xml), or
// STAT_BLD_TOTAL when it is in none of buildingsClasses (walls, fields...).
inline uint8_t stat_building_class(uint8_t type) {
	switch (type) {
	case BLD_HOUSE: return STAT_BLD_HOUSE;
	case BLD_STOREHOUSE: case BLD_FARMSTEAD: case BLD_MARKET: case BLD_DOCK: case BLD_CORRAL: case BLD_FIELD:
		return STAT_BLD_ECONOMIC;
	case BLD_OUTPOST: return STAT_BLD_OUTPOST;
	case BLD_BARRACKS: case BLD_TOWER: case BLD_STABLE: case BLD_RANGE: case BLD_ARSENAL: case BLD_SENTRY_TOWER:
	case BLD_ELEPHANT_STABLE: case BLD_EMBASSY: case BLD_TEMPLE: case BLD_FORGE:
		return STAT_BLD_MILITARY;
	case BLD_FORTRESS: return STAT_BLD_FORTRESS;
	case BLD_TOWN_CENTER: return STAT_BLD_CIV_CENTRE;
	case BLD_WONDER: return STAT_BLD_WONDER;
	default: return STAT_BLD_TOTAL;
	}
}

} // namespace rtso

#endif
