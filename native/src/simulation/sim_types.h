#ifndef RTSO_SIM_TYPES_H
#define RTSO_SIM_TYPES_H

#include <cstdint>

namespace rtso {

enum UnitType : uint8_t {
	UNIT_VILLAGER = 0,
	UNIT_SWORDSMAN = 1,
	UNIT_ARCHER = 2,
	UNIT_SPEARMAN = 3,
	UNIT_CAVALRY = 4,
	UNIT_TYPE_COUNT = 5,
	UNIT_TYPE_NONE = 255
};

enum UnitAction : uint8_t {
	ACTION_IDLE = 0,
	ACTION_MOVE = 1,
	ACTION_ATTACK = 2,
	ACTION_GATHER = 3,
	ACTION_RETURN_DROPSITE = 4,
	ACTION_BUILD = 5,
	ACTION_DYING = 6
};

enum BuildingType : uint8_t {
	BLD_TOWN_CENTER = 0,
	BLD_HOUSE = 1,
	BLD_BARRACKS = 2,
	BLD_STOREHOUSE = 3,
	BLD_TOWER = 4,
	BLD_FARMSTEAD = 5
};

enum ResourceType : uint8_t {
	RES_WOOD = 0,
	RES_GOLD = 1,
	RES_STONE = 2,
	RES_FOOD = 3
};

enum UnitStance : uint8_t {
	STANCE_AGGRESSIVE = 0,
	STANCE_DEFENSIVE = 1,
	STANCE_STAND_GROUND = 2
};

struct UnitTypeInfo {
	const char *name;
	const char *model_id;
	float hp;
	float speed;
	float attack_range;
	float attack_damage;
	float attack_cooldown;
	int32_t carry_capacity;
	float aggro_range;
};

inline const UnitTypeInfo &unit_type_info(uint8_t type) {
	static const UnitTypeInfo kTable[UNIT_TYPE_COUNT] = {
		{"Villager", "villager", 100.0f, 6.0f, 1.6f, 10.0f, 1.0f, 20, 3.0f},
		{"Swordsman", "swordsman", 160.0f, 6.0f, 2.0f, 22.0f, 0.9f, 0, 8.0f},
		{"Archer", "archer", 90.0f, 6.0f, 12.0f, 15.0f, 1.2f, 0, 14.0f},
		{"Spearman", "spearman", 130.0f, 6.0f, 2.4f, 18.0f, 1.0f, 0, 8.0f},
		{"Cavalry", "cavalry", 190.0f, 8.5f, 2.2f, 25.0f, 0.85f, 0, 8.0f},
	};
	if (type >= UNIT_TYPE_COUNT) return kTable[UNIT_VILLAGER];
	return kTable[type];
}

inline const char *unit_type_name(uint8_t type) { return unit_type_info(type).name; }
inline const char *unit_type_model(uint8_t type) { return unit_type_info(type).model_id; }

} // namespace rtso

#endif
