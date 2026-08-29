#ifndef RTSO_SIM_TYPES_H
#define RTSO_SIM_TYPES_H

#include <cstdint>
#include <vector>

namespace rtso {

enum BiomeType : uint8_t {
	BIOME_AEGEAN = 0,
	BIOME_LATIUM = 1,
	BIOME_ALPINE = 2,
	BIOME_OASIS = 3,
	BIOME_COUNT = 4
};

enum Civilization : uint8_t {
	CIV_ATHENIANS = 0,
	CIV_SPARTANS = 1,
	CIV_PERSIANS = 2,
	CIV_ROMANS = 3,
	CIV_CARTHAGINIANS = 4,
	CIV_MACEDONIANS = 5,
	CIV_COUNT = 6
};

enum MapType : uint8_t {
	MAP_AEGEAN_ARCHIPELAGO = 0,
	MAP_PELOPONNESE = 1,
	MAP_ACROPOLIS = 2,
	MAP_SAHARA = 3,
	MAP_SPARTAN_VALLEY = 4,
	MAP_TYPE_COUNT = 5
};

enum UnitType : uint8_t {
	UNIT_VILLAGER = 0,
	UNIT_SWORDSMAN = 1,
	UNIT_ARCHER = 2,
	UNIT_SPEARMAN = 3,
	UNIT_CAVALRY = 4,
	UNIT_TRADER = 5,
	UNIT_CATAPULT = 6,
	UNIT_HERO = 7,
	UNIT_TRIREME = 8,
	UNIT_ELEPHANT = 9,
	UNIT_SIEGE_RAM = 10,
	UNIT_TYPE_COUNT = 11,
	UNIT_TYPE_NONE = 255
};

enum UnitAction : uint8_t {
	ACTION_IDLE = 0,
	ACTION_MOVE = 1,
	ACTION_ATTACK = 2,
	ACTION_GATHER = 3,
	ACTION_RETURN_DROPSITE = 4,
	ACTION_BUILD = 5,
	ACTION_GARRISONED = 6,
	ACTION_TRADE = 7,
	ACTION_DYING = 8,
	ACTION_ATTACK_MOVE = 9,
	ACTION_PATROL = 10,
	ACTION_GUARD  = 11
};

enum BuildingType : uint8_t {
	BLD_TOWN_CENTER = 0,
	BLD_HOUSE = 1,
	BLD_BARRACKS = 2,
	BLD_STOREHOUSE = 3,
	BLD_TOWER = 4,
	BLD_FARMSTEAD = 5,
	BLD_MARKET = 6,
	BLD_FORTRESS = 7,
	BLD_GATE = 8,
	BLD_TEMPLE = 9,
	BLD_WONDER = 10,
	BLD_DOCK = 11,
	BLD_WALL_SEGMENT = 12,
	BLD_TYPE_COUNT = 13
};

enum ResourceType : uint8_t {
	RES_WOOD = 0,
	RES_GOLD = 1,
	RES_STONE = 2,
	RES_FOOD = 3,
	RES_COUNT = 4
};

enum UnitStance : uint8_t {
	STANCE_AGGRESSIVE = 0,
	STANCE_DEFENSIVE = 1,
	STANCE_STAND_GROUND = 2,
	STANCE_HOLD_GROUND = 3,
	STANCE_PASSIVE = 4
};

enum UnitFormation : uint8_t {
	FORMATION_NONE = 0,
	FORMATION_LINE = 1,
	FORMATION_BOX = 2,
	FORMATION_COLUMN = 3,
	FORMATION_PHALANX = 4,
	FORMATION_WEDGE = 5,
	FORMATION_SCATTER = 6
};

enum TechnologyId : uint8_t {
	TECH_NONE = 0,
	TECH_PHASE_TOWN = 1,
	TECH_PHASE_CITY = 2,
	TECH_FARMING_1 = 3,
	TECH_WOODCUTTING_1 = 4,
	TECH_MINING_1 = 5,
	TECH_WEAPONS_1 = 6,
	TECH_ARMOR_1 = 7,
	TECH_COUNT = 8
};

struct UnitTypeInfo {
	const char *name;
	const char *model_id;
	const char *portrait_id;
	float hp;
	float speed;
	float attack_range;
	float attack_damage;
	float attack_cooldown;
	int32_t carry_capacity;
	float aggro_range;
	float hack_armor;
	float pierce_armor;
	float crush_armor;
	int food_cost;
	int wood_cost;
	int gold_cost;
	int stone_cost;
	int pop_cost;
	float train_time;
};

inline const UnitTypeInfo &unit_type_info(uint8_t type) {
	static const UnitTypeInfo kTable[UNIT_TYPE_COUNT] = {
		// name, model, portrait, hp, speed, range, dmg, cd, carry, aggro, hack, pierce, crush, food, wood, gold, stone, pop, train_time
		{"Villager", "villager", "villager", 80.0f, 3.0f, 1.6f, 5.0f, 1.0f, 20, 3.0f, 2.0f, 4.0f, 15.0f, 50, 0, 0, 0, 1, 10.0f},
		{"Swordsman", "swordsman", "swordsman", 100.0f, 3.4f, 2.0f, 8.0f, 0.75f, 0, 10.0f, 3.0f, 3.0f, 15.0f, 0, 30, 60, 0, 1, 14.0f},
		{"Archer", "archer", "archer", 80.0f, 3.3f, 16.0f, 7.2f, 1.25f, 0, 16.0f, 1.0f, 1.0f, 15.0f, 0, 50, 40, 0, 1, 12.0f},
		{"Spearman", "spearman", "spearman", 100.0f, 3.2f, 2.4f, 8.5f, 1.0f, 0, 10.0f, 3.0f, 3.0f, 15.0f, 30, 40, 20, 0, 1, 12.0f},
		{"Cavalry", "cavalry", "cavalry", 180.0f, 5.5f, 2.2f, 11.5f, 1.25f, 0, 12.0f, 3.0f, 3.0f, 15.0f, 50, 0, 80, 0, 2, 18.0f},
		{"Merchant Trader", "cavalry", "market", 120.0f, 4.2f, 1.0f, 0.0f, 1.0f, 50, 0.0f, 2.0f, 2.0f, 10.0f, 60, 60, 0, 0, 1, 12.0f},
		{"Catapult Artillery", "swordsman", "fortress", 220.0f, 2.0f, 24.0f, 45.0f, 3.5f, 0, 24.0f, 2.0f, 5.0f, 20.0f, 0, 150, 100, 50, 3, 28.0f},
		{"Historical Champion Hero", "cavalry", "swordsman", 350.0f, 4.2f, 2.8f, 18.0f, 0.8f, 0, 16.0f, 6.0f, 6.0f, 25.0f, 200, 0, 200, 0, 3, 30.0f},
		{"War Trireme", "storehouse", "fortress", 600.0f, 4.5f, 20.0f, 25.0f, 1.2f, 0, 20.0f, 5.0f, 8.0f, 20.0f, 0, 200, 150, 0, 3, 35.0f},
		{"Carthaginian War Elephant", "cavalry", "fortress", 900.0f, 3.4f, 2.5f, 35.0f, 1.4f, 0, 16.0f, 5.0f, 8.0f, 20.0f, 250, 0, 200, 0, 4, 40.0f},
		{"Siege Battering Ram", "swordsman", "fortress", 850.0f, 2.2f, 2.4f, 75.0f, 2.5f, 0, 12.0f, 2.0f, 12.0f, 15.0f, 0, 200, 100, 0, 3, 25.0f}
	};
	if (type >= UNIT_TYPE_COUNT) return kTable[UNIT_VILLAGER];
	return kTable[type];
}

inline float unit_collision_radius(uint8_t type) {
	switch (type) {
		case UNIT_VILLAGER: return 0.50f;
		case UNIT_SWORDSMAN: return 0.55f;
		case UNIT_ARCHER: return 0.50f;
		case UNIT_SPEARMAN: return 0.55f;
		case UNIT_CAVALRY: return 0.85f;
		case UNIT_TRADER: return 0.80f;
		case UNIT_CATAPULT: return 1.30f;
		case UNIT_HERO: return 0.85f;
		case UNIT_TRIREME: return 1.80f;
		case UNIT_ELEPHANT: return 1.40f;
		case UNIT_SIEGE_RAM: return 1.20f;
		default: return 0.55f;
	}
}

inline const char *unit_type_name(uint8_t type) { return unit_type_info(type).name; }
inline const char *unit_type_model(uint8_t type) { return unit_type_info(type).model_id; }
inline const char *unit_type_portrait(uint8_t type) { return unit_type_info(type).portrait_id; }

struct BuildingTypeInfo {
	const char *name;
	const char *model_id;
	const char *portrait_id;
	float max_hp;
	int footprint; // 1 or 2
	int food_cost;
	int wood_cost;
	int gold_cost;
	int stone_cost;
	int pop_bonus;
	int garrison_cap;
	float build_time;
	float attack_range;
	float attack_damage;
	float attack_cooldown;
};

inline const BuildingTypeInfo &building_type_info(uint8_t type) {
	static const BuildingTypeInfo kTable[BLD_TYPE_COUNT] = {
		// name, model, portrait, hp, foot, f, w, g, s, pop, garrison, build_time, range, dmg, cd
		{"Town Center", "town_center", "town_center", 2500.0f, 2, 0, 300, 100, 200, 10, 20, 60.0f, 16.0f, 18.0f, 1.0f},
		{"House", "house", "house", 500.0f, 1, 0, 80, 0, 0, 5, 5, 20.0f, 0.0f, 0.0f, 0.0f},
		{"Barracks", "barracks", "barracks", 1500.0f, 2, 0, 150, 50, 50, 0, 10, 35.0f, 0.0f, 0.0f, 0.0f},
		{"Storehouse", "storehouse", "storehouse", 800.0f, 1, 0, 100, 0, 0, 0, 5, 25.0f, 0.0f, 0.0f, 0.0f},
		{"Defense Tower", "wall_tower", "tower", 1200.0f, 1, 0, 100, 0, 100, 0, 5, 30.0f, 18.0f, 20.0f, 1.0f},
		{"Farmstead", "farmstead", "farmstead", 800.0f, 1, 0, 100, 0, 0, 0, 5, 25.0f, 0.0f, 0.0f, 0.0f},
		{"Market", "market", "market", 1200.0f, 2, 0, 200, 100, 0, 0, 5, 40.0f, 0.0f, 0.0f, 0.0f},
		{"Fortress", "fortress", "fortress", 3500.0f, 2, 0, 300, 200, 300, 0, 30, 80.0f, 22.0f, 30.0f, 0.8f},
		{"Gate", "gate", "gate", 1500.0f, 1, 0, 100, 0, 100, 0, 0, 25.0f, 0.0f, 0.0f, 0.0f},
		{"Temple", "farmstead", "storehouse", 1200.0f, 2, 0, 150, 100, 100, 0, 5, 40.0f, 0.0f, 0.0f, 0.0f},
		{"Wonder Monument", "town_center", "town_center", 5000.0f, 2, 500, 500, 500, 500, 0, 10, 120.0f, 0.0f, 0.0f, 0.0f},
		{"Naval Shipyard Dock", "storehouse", "storehouse", 1400.0f, 2, 0, 200, 0, 100, 0, 5, 40.0f, 16.0f, 15.0f, 1.2f},
		{"Stone Wall Segment", "wall_tower", "tower", 1800.0f, 1, 0, 0, 0, 40, 0, 0, 15.0f, 0.0f, 0.0f, 0.0f}
	};
	if (type >= BLD_TYPE_COUNT) return kTable[BLD_HOUSE];
	return kTable[type];
}

inline const char *building_type_name(uint8_t type) { return building_type_info(type).name; }
inline const char *building_type_model(uint8_t type) { return building_type_info(type).model_id; }
inline const char *building_type_portrait(uint8_t type) { return building_type_info(type).portrait_id; }

struct TechInfo {
	const char *name;
	const char *desc;
	int food_cost;
	int wood_cost;
	int gold_cost;
	int stone_cost;
	float research_time;
	uint8_t required_phase;
};

inline const TechInfo &tech_info(uint8_t tech_id) {
	static const TechInfo kTable[TECH_COUNT] = {
		{"None", "", 0, 0, 0, 0, 0.0f, 0},
		{"Town Phase", "Unlocks Barracks, Defense Towers, and Market technologies.", 200, 200, 100, 0, 30.0f, 1},
		{"City Phase", "Unlocks Fortresses, Siege weapons, and Imperial tech.", 400, 400, 300, 200, 50.0f, 2},
		{"Farming Tools", "+25% Food gather rate from fields and forage.", 100, 100, 0, 0, 20.0f, 1},
		{"Iron Axes", "+25% Wood harvesting speed.", 50, 150, 50, 0, 20.0f, 1},
		{"Shaft Mining", "+25% Gold and Stone mining rate.", 50, 50, 150, 100, 20.0f, 1},
		{"Iron Weapons", "+20% Attack damage for infantry and cavalry.", 100, 100, 150, 0, 25.0f, 2},
		{"Plate Armor", "+3 Hack and Pierce armor for all military units.", 100, 50, 200, 100, 25.0f, 2},
	};
	if (tech_id >= TECH_COUNT) return kTable[TECH_NONE];
	return kTable[tech_id];
}

struct CivInfo {
	Civilization civ;
	const char *code;
	const char *name;
	const char *emblem_path;
	const char *description;
	float metal_gather_mult;
	float melee_damage_mult;
	float hp_mult;
	float build_speed_mult;
	float trade_gain_mult;
};

inline const CivInfo &civ_info(uint8_t civ) {
	static const CivInfo kTable[CIV_COUNT] = {
		// civ, code, name, emblem, desc, metal_mult, melee_dmg_mult, hp_mult, build_speed_mult, trade_mult
		{CIV_ATHENIANS, "athen", "Athenians", "res://assets/textures/ui/emblems/athenians.png",
		 "Silver Owls: +15% metal gather rate. Classical Greek democracy with balanced hoplites and strong navy.",
		 1.15f, 1.0f, 1.0f, 1.0f, 1.0f},
		{CIV_SPARTANS, "spart", "Spartans", "res://assets/textures/ui/emblems/spartans.png",
		 "Ritualistic Exercise & Agoge: +35% HP and +25% melee damage for Citizen-Soldiers and Champions.",
		 1.0f, 1.25f, 1.35f, 1.0f, 1.0f},
		{CIV_PERSIANS, "pers", "Persians", "res://assets/textures/ui/emblems/persians.png",
		 "Achaemenid Trade & Immortals: +25% Market trade gain and faster cavalry production.",
		 1.0f, 1.0f, 1.0f, 1.0f, 1.25f},
		{CIV_ROMANS, "rome", "Romans", "res://assets/textures/ui/emblems/romans.png",
		 "Legionary Discipline & Rapid Engineering: +30% building construction rate and heavy legionary armor.",
		 1.0f, 1.10f, 1.15f, 1.30f, 1.0f},
		{CIV_CARTHAGINIANS, "cart", "Carthaginians", "res://assets/textures/ui/emblems/athenians.png",
		 "Phoenician Naval Mastery & War Elephants: Elite mercenary forces and formidable war elephants.",
		 1.0f, 1.15f, 1.20f, 1.0f, 1.20f},
		{CIV_MACEDONIANS, "mace", "Macedonians", "res://assets/textures/ui/emblems/spartans.png",
		 "Sarissa Phalanx & Companion Cavalry: Extended spear attack reach and rapid cavalry shock charges.",
		 1.0f, 1.20f, 1.10f, 1.0f, 1.0f}
	};
	if (civ >= CIV_COUNT) return kTable[CIV_ATHENIANS];
	return kTable[civ];
}

enum FaunaType : uint8_t {
	FAUNA_DEER   = 0, // 150 Food, Passive Flee
	FAUNA_BOAR   = 1, // 300 Food, Aggressive Defense
	FAUNA_SHEEP  = 2, // 100 Food, Domesticable Herd
	FAUNA_WOLF   = 3, // 200 Food, Hostile Predator
	FAUNA_COUNT  = 4
};

enum FaunaBehavior : uint8_t {
	FAUNA_IDLE_GRAZE = 0,
	FAUNA_FLEE       = 1,
	FAUNA_ATTACK     = 2,
	FAUNA_HERDED     = 3,
	FAUNA_CARCASS    = 4
};

struct FaunaTypeInfo {
	const char *name;
	const char *model_id;
	float max_hp;
	float speed;
	int food_amount;
	float agro_range;
	float attack_damage;
	float attack_cooldown;
	bool is_predator;
};

inline const FaunaTypeInfo &fauna_type_info(uint8_t ftype) {
	static const FaunaTypeInfo kTable[FAUNA_COUNT] = {
		// name, model, hp, speed, food, agro, dmg, cd, predator
		{"Red Deer", "villager", 45.0f, 4.5f, 150, 12.0f, 0.0f, 1.0f, false},
		{"Wild Boar", "cavalry", 120.0f, 3.8f, 300, 8.0f, 12.0f, 1.2f, true},
		{"Mountain Sheep", "villager", 35.0f, 2.5f, 100, 10.0f, 0.0f, 1.0f, false},
		{"Gray Wolf", "swordsman", 80.0f, 4.8f, 200, 10.0f, 10.0f, 1.0f, true}
	};
	if (ftype >= FAUNA_COUNT) return kTable[FAUNA_DEER];
	return kTable[ftype];
}

} // namespace rtso

#endif
