#ifndef RTSO_SIM_TYPES_H
#define RTSO_SIM_TYPES_H

#include <cstdint>
#include <cstdio>
#include <vector>

namespace rtso {


// 0 A.D. DeathDamage.js & Loot.js: track recent death events for area splash and bounty
struct RecentDeath {
	float x = 0.0f;
	float z = 0.0f;
	uint8_t team = 0;
	uint8_t killer_team = 0;
	uint8_t type = 0;
	bool is_building = false;
	bool death_damage_applied = false;
	bool loot_applied = false;
};
enum BiomeType : uint8_t {
	BIOME_AEGEAN = 0,
	BIOME_LATIUM = 1,
	BIOME_ALPINE = 2,
	BIOME_OASIS = 3,
	BIOME_TEMPERATE = 4,
	BIOME_STEPPE = 5,
	BIOME_SAVANNA = 6,
	BIOME_COUNT = 7
};

enum Civilization : uint8_t {
	CIV_ATHENIANS = 0,
	CIV_SPARTANS = 1,
	CIV_PERSIANS = 2,
	CIV_ROMANS = 3,
	CIV_CARTHAGINIANS = 4,
	CIV_MACEDONIANS = 5,
	CIV_BRITONS = 6,
	CIV_GAULS = 7,
	CIV_IBERIANS = 8,
	CIV_MAURYANS = 9,
	CIV_KUSHITES = 10,
	CIV_PTOLEMIES = 11,
	CIV_SELEUCIDS = 12,
	CIV_HAN = 13, // appended so saved games and network messages keep civ ids
	CIV_COUNT = 14
};

enum MapType : uint8_t {
	MAP_AEGEAN_ARCHIPELAGO = 0,
	MAP_PELOPONNESE = 1,
	MAP_ACROPOLIS = 2,
	MAP_SAHARA = 3,
	MAP_SPARTAN_VALLEY = 4,
	MAP_RANDOM_CONTINENTAL = 5,
	MAP_RANDOM_ARCHIPELAGO = 6,
	MAP_RANDOM_OASIS = 7,
	MAP_RANDOM_HIGHLANDS = 8,
	MAP_RANDOM_VALLEY = 9,
	MAP_RANDOM_MEDITERRANEAN = 10,
	MAP_DANUBIUS = 11,
	MAP_SAHARAN_OASES = 12,
	MAP_ANATOLIAN_PLATEAU = 13,
	MAP_JEBEL_BARKAL = 14,
	MAP_TYPE_COUNT = 15
};

struct MapTypeInfo {
	MapType type;
	const char *id;
	const char *name;
	const char *biome;
	bool is_procedural;
	const char *description;
};

inline const MapTypeInfo &map_type_info(uint8_t type) {
	static const MapTypeInfo kTable[MAP_TYPE_COUNT] = {
		{MAP_AEGEAN_ARCHIPELAGO, "aegean_archipelago", "Aegean Archipelago", "aegean", true, "Scattered Greek islands surrounded by crystal waters, rich in coastal fishing and maritime routes."},
		{MAP_PELOPONNESE, "peloponnese", "Peloponnese Peninsula", "mediterranean", true, "Vast peninsula with jagged coastlines, defensive hills, and fertile coastal lowlands."},
		{MAP_ACROPOLIS, "acropolis", "Acropolis Heights", "mediterranean", true, "Elevated central plateaus offering formidable defensive high-ground over resource valleys."},
		{MAP_SAHARA, "sahara", "Sahara Dunes", "oasis", true, "Arid desert sands with scarce timber, contested watering holes, and rich mineral outcroppings."},
		{MAP_SPARTAN_VALLEY, "spartan_valley", "Spartan Valley", "alpine", true, "Narrow pass flanked by impassable mountain ranges, ideal for phalanx warfare and tactical choke points."},
		{MAP_RANDOM_CONTINENTAL, "random_continental", "Continental (RMS)", "mediterranean", true, "A grand landmass surrounded by ocean with winding inland rivers and rolling resource woodlands."},
		{MAP_RANDOM_ARCHIPELAGO, "random_archipelago", "Archipelago (RMS)", "aegean", true, "Deep-water island cluster requiring maritime expansion, docks, and naval supremacy."},
		{MAP_RANDOM_OASIS, "random_oasis", "Oasis (RMS)", "oasis", true, "Densely forested desert borders surrounding a lush, resource-heavy central lake basin."},
		{MAP_RANDOM_HIGHLANDS, "random_highlands", "Highlands (RMS)", "alpine", true, "Rugged alpine terrain with sheer mountain cliffs, gorge rivers, and strategic passes."},
		{MAP_RANDOM_VALLEY, "random_valley", "Fertile Valley (RMS)", "latium", true, "A lush river valley bounded by towering mountain ridges with balanced contested resources."},
		{MAP_RANDOM_MEDITERRANEAN, "random_mediterranean", "Mediterranean Sea (RMS)", "mediterranean", true, "A central sea ringed by continuous fertile mainland coasts for all civilizations."},
		{MAP_DANUBIUS, "danubius", "Danubius (RMS)", "temperate", true, "Meandering dual-channel river with fords, sandbars, and central island fortress."},
		{MAP_SAHARAN_OASES, "saharan_oases", "Saharan Oases (RMS)", "oasis", true, "Arid desert with sinusoidal sand dunes, date palm groves, and freshwater oases."},
		{MAP_ANATOLIAN_PLATEAU, "anatolian_plateau", "Anatolian Plateau (RMS)", "steppe", true, "High plateau steppe surrounded by perimeter canyon ravines and rich mineral outcrops."},
		{MAP_JEBEL_BARKAL, "jebel_barkal", "Jebel Barkal (RMS)", "savanna", true, "Iconic central sandstone mesa landmark with high-ground elevation advantage and surrounding savanna quarries."}
	};
	if (type >= MAP_TYPE_COUNT) return kTable[MAP_RANDOM_CONTINENTAL];
	return kTable[type];
}

inline bool is_procedural_map(uint8_t type) {
	return type < MAP_TYPE_COUNT;
}

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
	UNIT_HEALER = 11,
	UNIT_CHARIOT = 12,
	UNIT_WAR_DOG = 13,
	UNIT_BOLT_SHOOTER = 14,
	UNIT_FISHING_BOAT = 15,
	UNIT_MERCHANT_SHIP = 16,
	UNIT_FEMALE_CITIZEN = 17,
	UNIT_CARNYX = 18,
	UNIT_WORKER_ELEPHANT = 19,
	UNIT_ARCHER_MAIDEN = 20,
	UNIT_CAMEL = 21,
	UNIT_SCYTHED_CHARIOT = 22,
	UNIT_TYPE_COUNT = 23,
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
	BLD_CORRAL = 13,
	BLD_TYPE_COUNT = 14
};

enum ResourceType : uint8_t {
	RES_WOOD = 0,
	RES_GOLD = 1,
	RES_STONE = 2,
	RES_FOOD = 3,
	RES_COUNT = 4
};

// Food supply flavour for ResourceGatherer gather_* clips. Wood/gold/stone
// ignore this; food defaults to fruit (berry bush) unless marked meat/grain.
enum ResourceSubtype : uint8_t {
	RES_SUB_NONE = 0,
	RES_SUB_MEAT = 1,
	RES_SUB_GRAIN = 2
};

enum UnitStance : uint8_t {
	STANCE_AGGRESSIVE = 0,
	STANCE_DEFENSIVE = 1,
	STANCE_STAND_GROUND = 2,
	STANCE_HOLD_GROUND = 3,
	STANCE_PASSIVE = 4
};

// 0 A.D. Formations (source: template_player.xml:84-96, exactly 13 entries indexed 0..12)
// Appended AFTER existing 6 so network/replay order stays wire-compatible (values 0..6 unchanged).
// 0 A.D. template_player.xml:84-96 token list:
//   [84] special/formations/null                 -> FORMATION_NONE = 0
//   [85] special/formations/box                  -> FORMATION_BOX = 2
//   [86] special/formations/column_closed        -> FORMATION_COLUMN = 3
//   [87] special/formations/line_closed          -> FORMATION_LINE = 1
//   [88] special/formations/column_open          -> FORMATION_COLUMN_OPEN = 8
//   [89] special/formations/line_open            -> FORMATION_LINE_OPEN = 7
//   [90] special/formations/flank                -> FORMATION_FLANK = 9
//   [91] special/formations/battle_line          -> FORMATION_BATTLE_LINE = 10
//   [92] special/formations/skirmish             -> FORMATION_SCATTER = 6
//   [93] special/formations/wedge                -> FORMATION_WEDGE = 5
//   [94] special/formations/ships/line_staggered -> FORMATION_SHIPS_LINE = 11
//   [95] special/formations/ships/flank          -> FORMATION_SHIPS_FLANK = 9 (alias FORMATION_FLANK)
//   [96] special/formations/ships/column         -> FORMATION_SHIPS_COLUMN = 12
// Note: FORMATION_PHALANX = 4 preserved from 0 A.D. special/formations/phalanx.xml (civ-specific).
enum UnitFormation : uint8_t {
	FORMATION_NONE = 0,
	FORMATION_LINE = 1,
	FORMATION_BOX = 2,
	FORMATION_COLUMN = 3,
	FORMATION_PHALANX = 4,
	FORMATION_WEDGE = 5,
	FORMATION_SCATTER = 6,
	FORMATION_LINE_OPEN = 7,
	FORMATION_COLUMN_OPEN = 8,
	FORMATION_FLANK = 9,
	FORMATION_BATTLE_LINE = 10,
	FORMATION_SHIPS_LINE = 11,
	FORMATION_SHIPS_COLUMN = 12,
	FORMATION_COUNT = 13,
	FORMATION_SHIPS_FLANK = FORMATION_FLANK
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

enum StatusEffect : uint8_t {
	EFFECT_NONE = 0,
	EFFECT_POISON = 1,
	EFFECT_BURNING = 2,
	EFFECT_SNARE = 4,
	EFFECT_KNOCKBACK = 8,
	EFFECT_SLOW = 16,
	EFFECT_BLEED = 32
};

enum VictoryCondition : uint8_t {
	VICTORY_CONQUEST = 0,
	VICTORY_WONDER = 1,
	VICTORY_RELIC = 2,
	VICTORY_ENDLESS = 3
};

struct BattleAlert {
	enum Type : uint8_t {
		ALERT_UNDER_ATTACK = 0,
		ALERT_BUILDING_UNDER_ATTACK = 1,
		ALERT_BUILDING_DESTROYED = 2,
		ALERT_STRUCTURE_CAPTURED = 3,
		ALERT_WONDER_COUNTDOWN = 4,
		ALERT_RELIC_CLAIMED = 5
	};
	Type type = ALERT_UNDER_ATTACK;
	float x = 0.0f;
	float z = 0.0f;
	uint32_t entity_id = 0;
	uint8_t team = 0;
	float time_stamp = 0.0f;
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
	float hack_damage;
	float pierce_damage;
	float crush_damage;
};

inline const UnitTypeInfo &unit_type_info(uint8_t type) {
	static const UnitTypeInfo kTable[UNIT_TYPE_COUNT] = {
		// name, model, portrait, hp, speed, range, dmg, cd, carry, aggro, hack, pierce, crush, food, wood, gold, stone, pop, train_time, hack_dmg, pierce_dmg, crush_dmg
		{"Villager", "villager", "villager", 80.0f, 3.0f, 1.6f, 5.0f, 1.0f, 20, 3.0f, 2.0f, 4.0f, 15.0f, 50, 0, 0, 0, 1, 10.0f, 5.0f, 0.0f, 0.0f},
		{"Swordsman", "swordsman", "swordsman", 100.0f, 3.4f, 2.0f, 8.0f, 0.75f, 0, 10.0f, 3.0f, 3.0f, 15.0f, 0, 30, 60, 0, 1, 14.0f, 8.0f, 0.0f, 0.0f},
		{"Archer", "archer", "archer", 80.0f, 3.3f, 16.0f, 7.2f, 1.25f, 0, 16.0f, 1.0f, 1.0f, 15.0f, 0, 50, 40, 0, 1, 12.0f, 0.0f, 7.2f, 0.0f},
		{"Spearman", "spearman", "spearman", 100.0f, 3.2f, 2.4f, 8.5f, 1.0f, 0, 10.0f, 3.0f, 3.0f, 15.0f, 30, 40, 20, 0, 1, 12.0f, 4.25f, 4.25f, 0.0f},
		{"Cavalry", "cavalry", "cavalry", 180.0f, 5.5f, 2.2f, 11.5f, 1.25f, 0, 12.0f, 3.0f, 3.0f, 15.0f, 50, 0, 80, 0, 2, 18.0f, 11.5f, 0.0f, 0.0f},
		{"Merchant Trader", "trader", "trader", 120.0f, 4.2f, 1.0f, 0.0f, 1.0f, 50, 0.0f, 2.0f, 2.0f, 10.0f, 60, 60, 0, 0, 1, 12.0f, 0.0f, 0.0f, 0.0f},
		{"Catapult Artillery", "catapult", "catapult", 220.0f, 2.0f, 24.0f, 45.0f, 3.5f, 0, 24.0f, 2.0f, 5.0f, 20.0f, 0, 150, 100, 50, 3, 28.0f, 0.0f, 0.0f, 45.0f},
		{"Historical Champion Hero", "hero", "hero", 350.0f, 4.2f, 2.8f, 18.0f, 0.8f, 0, 16.0f, 6.0f, 6.0f, 25.0f, 200, 0, 200, 0, 3, 30.0f, 12.0f, 6.0f, 0.0f},
		{"War Trireme", "trireme", "trireme", 600.0f, 4.5f, 20.0f, 32.0f, 1.0f, 0, 20.0f, 5.0f, 8.0f, 20.0f, 0, 200, 150, 0, 3, 35.0f, 0.0f, 32.0f, 0.0f},
		{"Carthaginian War Elephant", "elephant", "elephant", 900.0f, 3.4f, 2.5f, 35.0f, 1.4f, 0, 16.0f, 5.0f, 8.0f, 20.0f, 250, 0, 200, 0, 4, 40.0f, 20.0f, 0.0f, 15.0f},
		{"Siege Battering Ram", "ram", "ram", 850.0f, 2.2f, 2.4f, 75.0f, 2.5f, 0, 12.0f, 2.0f, 12.0f, 15.0f, 0, 200, 100, 0, 3, 25.0f, 0.0f, 0.0f, 75.0f},
		{"Temple Healer", "healer", "healer", 90.0f, 3.2f, 12.0f, 0.0f, 1.0f, 0, 10.0f, 1.0f, 1.0f, 10.0f, 80, 0, 60, 0, 1, 14.0f, 0.0f, 0.0f, 0.0f},
		{"Celtic War Chariot", "chariot", "chariot", 280.0f, 5.8f, 14.0f, 14.0f, 1.1f, 0, 14.0f, 4.0f, 4.0f, 20.0f, 80, 100, 60, 0, 2, 22.0f, 4.0f, 10.0f, 0.0f},
		{"Celtic War Dog", "war_dog", "war_dog", 75.0f, 5.2f, 1.2f, 9.0f, 0.7f, 0, 12.0f, 1.0f, 2.0f, 8.0f, 60, 0, 0, 0, 1, 8.0f, 9.0f, 0.0f, 0.0f},
		{"Bolt Shooter Ballista", "bolt_shooter", "bolt_shooter", 200.0f, 2.2f, 28.0f, 40.0f, 3.0f, 0, 28.0f, 2.0f, 4.0f, 15.0f, 0, 120, 80, 0, 2, 22.0f, 0.0f, 40.0f, 0.0f},
		{"Fishing Boat", "fishing_boat", "fishing_boat", 150.0f, 3.8f, 1.5f, 0.0f, 1.0f, 30, 0.0f, 1.0f, 2.0f, 10.0f, 0, 75, 0, 0, 1, 12.0f, 0.0f, 0.0f, 0.0f},
		{"Merchant Cargo Ship", "merchant_ship", "merchant_ship", 300.0f, 4.0f, 1.0f, 0.0f, 1.0f, 100, 0.0f, 3.0f, 4.0f, 15.0f, 0, 120, 40, 0, 1, 18.0f, 0.0f, 0.0f, 0.0f},
		{"Female Citizen", "female_citizen", "female_citizen", 65.0f, 3.2f, 1.4f, 4.0f, 1.0f, 25, 3.0f, 1.0f, 3.0f, 10.0f, 40, 0, 0, 0, 1, 8.0f, 4.0f, 0.0f, 0.0f},
		{"Gallic Carnyx Herald", "carnyx", "carnyx", 140.0f, 3.5f, 2.0f, 7.0f, 1.0f, 0, 12.0f, 3.0f, 3.0f, 15.0f, 70, 40, 30, 0, 1, 15.0f, 7.0f, 0.0f, 0.0f},
		{"Mauryan Worker Elephant", "worker_elephant", "worker_elephant", 600.0f, 3.2f, 2.0f, 12.0f, 1.2f, 100, 6.0f, 4.0f, 6.0f, 20.0f, 180, 80, 0, 0, 2, 25.0f, 8.0f, 0.0f, 4.0f},
		{"Mauryan Archer Maiden", "archer_maiden", "archer_maiden", 95.0f, 3.6f, 17.0f, 8.5f, 1.1f, 0, 17.0f, 2.0f, 2.0f, 15.0f, 40, 60, 40, 0, 1, 14.0f, 0.0f, 8.5f, 0.0f},
		{"Persian/Arabian Camel", "camel", "camel", 190.0f, 5.2f, 2.4f, 12.0f, 1.2f, 0, 12.0f, 3.0f, 3.0f, 15.0f, 60, 0, 70, 0, 2, 16.0f, 12.0f, 0.0f, 0.0f},
		{"Seleucid Scythed Chariot", "scythed_chariot", "scythed_chariot", 320.0f, 5.6f, 2.2f, 24.0f, 1.0f, 0, 14.0f, 5.0f, 5.0f, 25.0f, 100, 80, 120, 0, 3, 26.0f, 18.0f, 0.0f, 6.0f}
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
		case UNIT_HEALER: return 0.50f;
		case UNIT_CHARIOT: return 1.25f;
		case UNIT_WAR_DOG: return 0.40f;
		case UNIT_BOLT_SHOOTER: return 1.10f;
		case UNIT_FISHING_BOAT: return 1.10f;
		case UNIT_MERCHANT_SHIP: return 1.60f;
		case UNIT_FEMALE_CITIZEN: return 0.48f;
		case UNIT_CARNYX: return 0.55f;
		case UNIT_WORKER_ELEPHANT: return 1.35f;
		case UNIT_ARCHER_MAIDEN: return 0.50f;
		case UNIT_CAMEL: return 0.90f;
		case UNIT_SCYTHED_CHARIOT: return 1.35f;
		default: return 0.55f;
	}
}

// 0 A.D. capture attack strengths (Attack.js / template_unit_*).
// Values are per capture effect; the simulation applies them per second for
// the bounded nearby-capturer model.
inline float unit_capture_strength(uint8_t type) {
	switch (type) {
		case UNIT_VILLAGER: case UNIT_HEALER: case UNIT_TRADER:
		case UNIT_FEMALE_CITIZEN: case UNIT_WORKER_ELEPHANT:
			return 1.0f;
		case UNIT_CAVALRY: case UNIT_CHARIOT: case UNIT_CAMEL:
		case UNIT_SCYTHED_CHARIOT: return 1.75f;
		case UNIT_SPEARMAN: case UNIT_SWORDSMAN: case UNIT_ARCHER:
		case UNIT_ARCHER_MAIDEN: case UNIT_ELEPHANT: case UNIT_CARNYX:
		case UNIT_WAR_DOG: return 2.5f;
		case UNIT_HERO: return 10.0f;
		default: return 0.0f;
	}
}

inline const char *unit_type_name(uint8_t type) { return unit_type_info(type).name; }
inline const char *unit_type_model(uint8_t type) { return unit_type_info(type).model_id; }
inline const char *unit_type_portrait(uint8_t type) { return unit_type_info(type).portrait_id; }

inline bool unit_is_ship(uint8_t type) {
	return type == UNIT_TRIREME || type == UNIT_FISHING_BOAT || type == UNIT_MERCHANT_SHIP;
}

inline bool unit_is_siege_engine(uint8_t type) {
	return type == UNIT_SIEGE_RAM || type == UNIT_CATAPULT || type == UNIT_BOLT_SHOOTER;
}

// 0 A.D. Attack.js CanAttack: fishing/merchant/trader/healer ship no (or
// RestrictedClasses !SeaCreature). Fish are gathered, not fought.
inline bool unit_can_attack(uint8_t type) {
	return unit_type_info(type).attack_damage > 0.0f;
}

// 0 A.D. ship attack kinds for the three hulls RTS0 trains:
//   fishing  → Melee Harpoon vs SeaCreature only (gather in RTS0)
//   merchant → no Attack
//   trireme  → Ranged Arrow (template_unit_ship_warship_arrow; the ram /
//              siege / fire hulls are separate 0 A.D. units we do not train)
inline bool unit_has_ranged_attack(uint8_t type, uint8_t civ = CIV_ATHENIANS) {
	switch (type) {
		case UNIT_ARCHER:
		case UNIT_ARCHER_MAIDEN:
		case UNIT_BOLT_SHOOTER:
		case UNIT_CHARIOT:
		case UNIT_CATAPULT:
		case UNIT_TRIREME:
			return true;
		case UNIT_ELEPHANT:
			return civ == CIV_MAURYANS;
		default:
			return false;
	}
}

inline uint8_t unit_default_stance(uint8_t type) {
	switch (type) {
		case UNIT_FISHING_BOAT:
		case UNIT_MERCHANT_SHIP:
		case UNIT_TRADER:
			return STANCE_PASSIVE;
		default:
			return STANCE_AGGRESSIVE;
	}
}

// Corpse lifetime before removal. Ships use 0 A.D. CCmpDecay SinkingAnim (~4–5s).
inline float unit_death_duration(uint8_t type) {
	if (unit_is_ship(type)) return 5.0f;
	if (unit_is_siege_engine(type)) return 1.6f;
	return 1.4f;
}

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
	// 0 A.D. Capturable.js / TerritoryDecay.js template values.
	float capture_max_points;
	float capture_regen_rate;
	float capture_garrison_regen_rate;
	float territory_decay_rate;
};

inline const BuildingTypeInfo &building_type_info(uint8_t type) {
	static const BuildingTypeInfo kTable[BLD_TYPE_COUNT] = {
		// name, model, portrait, hp, foot, f, w, g, s, pop, garrison, build_time, range, dmg, cd,
		// capture max, regen, garrison regen, territory decay
		{"Town Center", "town_center", "town_center", 2500.0f, 2, 0, 300, 100, 200, 10, 20, 60.0f, 16.0f, 18.0f, 1.0f, 2500.0f, 30.0f, 1.0f, 20.0f},
		{"House", "house", "house", 500.0f, 1, 0, 80, 0, 0, 5, 5, 20.0f, 0.0f, 0.0f, 0.0f, 500.0f, 5.0f, 1.0f, 20.0f},
		{"Barracks", "barracks", "barracks", 1500.0f, 2, 0, 150, 50, 50, 0, 10, 35.0f, 0.0f, 0.0f, 0.0f, 500.0f, 5.0f, 1.0f, 20.0f},
		{"Storehouse", "storehouse", "storehouse", 800.0f, 1, 0, 100, 0, 0, 0, 5, 25.0f, 0.0f, 0.0f, 0.0f, 500.0f, 5.0f, 1.0f, 20.0f},
		{"Defense Tower", "wall_tower", "tower", 1200.0f, 1, 0, 100, 0, 100, 0, 5, 30.0f, 18.0f, 20.0f, 1.0f, 500.0f, 10.0f, 1.0f, 20.0f},
		{"Farmstead", "farmstead", "farmstead", 800.0f, 1, 0, 100, 0, 0, 0, 5, 25.0f, 0.0f, 0.0f, 0.0f, 300.0f, 5.0f, 1.0f, 20.0f},
		{"Market", "market", "market", 1200.0f, 2, 0, 200, 100, 0, 0, 5, 40.0f, 0.0f, 0.0f, 0.0f, 500.0f, 5.0f, 1.0f, 20.0f},
		{"Fortress", "fortress", "fortress", 3500.0f, 2, 0, 300, 200, 300, 0, 30, 80.0f, 22.0f, 30.0f, 0.8f, 4000.0f, 45.0f, 1.0f, 40.0f},
		{"Gate", "gate", "gate", 1500.0f, 1, 0, 100, 0, 100, 0, 0, 25.0f, 0.0f, 0.0f, 0.0f, 500.0f, 5.0f, 1.0f, 20.0f},
		{"Temple", "temple", "temple", 1200.0f, 2, 0, 150, 100, 100, 0, 5, 40.0f, 0.0f, 0.0f, 0.0f, 500.0f, 15.0f, 1.0f, 20.0f},
		{"Wonder Monument", "wonder", "wonder", 5000.0f, 2, 500, 500, 500, 500, 0, 10, 120.0f, 0.0f, 0.0f, 0.0f, 2000.0f, 5.0f, 1.0f, 20.0f},
		{"Naval Shipyard Dock", "dock", "dock", 1400.0f, 2, 0, 200, 0, 100, 0, 5, 40.0f, 16.0f, 15.0f, 1.2f, 500.0f, 5.0f, 1.0f, 20.0f},
		{"Stone Wall Segment", "wall_segment", "wall", 1800.0f, 1, 0, 0, 0, 40, 0, 5, 15.0f, 0.0f, 0.0f, 0.0f, 500.0f, 5.0f, 1.0f, 20.0f},
		{"Livestock Corral", "corral", "corral", 800.0f, 2, 0, 100, 0, 0, 5, 10, 25.0f, 0.0f, 0.0f, 0.0f, 500.0f, 5.0f, 1.0f, 20.0f},
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

inline constexpr uint32_t unit_bit(uint8_t type) { return (type < 32) ? (1u << type) : 0u; }

inline constexpr uint16_t bld_bit(uint8_t type) { return (type < 16) ? (uint16_t)(1u << type) : 0u; }

// ---------------------------------------------------------------------------
// Per-civ unit rosters, generated from 0 A.D.'s templates by
// tools/extract_0ad_rosters.py into civ_roster_data.h.
// Each civ unit (Athenian Hoplite, Han Crossbowman, Pericles, ...) carries
// 0 A.D.'s own stats and names the RTS0 UnitType that drives its behaviour,
// model fallback and AI role.
// ---------------------------------------------------------------------------

enum CivRosterFlag : uint8_t {
	ROSTER_HERO = 1,
	ROSTER_CHAMPION = 2,
	ROSTER_RANGED = 4,
	ROSTER_CITIZEN = 8,
	ROSTER_MERCENARY = 16
};

inline constexpr uint16_t civ_bit(uint8_t civ) { return (civ < 16) ? (uint16_t)(1u << civ) : 0u; }

// Roster tables hold at most 64 * CIV_TEMPLATE_WORDS_MAX unit templates.
inline constexpr uint16_t CIV_TEMPLATE_WORDS_MAX = 8;

struct CivHeroAura {
	const char *id;
	const char *name;
	const char *description;
	float radius; // 0 A.D. metres; 0 = applies everywhere
	// Strongest effect RTS0's AuraManager can model (AuraType; 255 = none yet).
	uint8_t kind;
	float value;
	float range; // RTS0 units
	uint64_t affects[CIV_TEMPLATE_WORDS_MAX]; // bitset of roster rows the aura buffs
};

enum CivBonusStat : uint8_t {
	CIV_BONUS_BUILDING_HP = 1,
	CIV_BONUS_BUILDING_BUILD_TIME = 2,
	CIV_BONUS_BUILDING_WOOD_COST = 3,
	CIV_BONUS_BUILDING_STONE_COST = 4,
	CIV_BONUS_BUILDING_CAPTURE_POINTS = 5,
	CIV_BONUS_POP_CAP = 6
};

// An auto-researched 0 A.D. civ bonus acting on buildings or the player.
struct CivPlayerBonus {
	uint16_t civs; // civ_bit() mask
	uint8_t stat; // CivBonusStat
	uint16_t buildings; // bld_bit() mask (0 for player-wide stats)
	float mult;
	const char *id;
	const char *tooltip;
};

struct CivUnitTemplate {
	const char *id; // 0 A.D. template path under units/, e.g. "athen/infantry_spearman_b"
	const char *name;
	const char *specific_name;
	uint8_t civ;
	uint8_t unit_type; // RTS0 behaviour class (UnitType)
	uint8_t phase; // 1 village, 2 town, 3 city
	uint8_t flags; // CivRosterFlag bits
	uint16_t trainers; // bld_bit() of each BuildingType that trains it
	float hp;
	float speed;
	float attack_range;
	float attack_damage;
	float attack_cooldown;
	float hack_damage;
	float pierce_damage;
	float crush_damage;
	float hack_armor;
	float pierce_armor;
	float crush_armor;
	int16_t food_cost;
	int16_t wood_cost;
	int16_t gold_cost;
	int16_t stone_cost;
	uint8_t pop_cost;
	float train_time;
	int16_t carry_capacity;
	int16_t auras[3]; // index into civ_hero_aura_table(), -1 = none
};

#include "civ_roster_data.h"

inline constexpr uint16_t CIV_UNIT_TEMPLATE_NONE = 0xFFFF;
static_assert(CIV_UNIT_TEMPLATE_COUNT <= 64 * CIV_TEMPLATE_WORDS_MAX, "raise CIV_TEMPLATE_WORDS_MAX");

// Product of the civ's 0 A.D. bonuses on `stat` for a building type
// (pass BLD_TYPE_COUNT for player-wide stats such as CIV_BONUS_POP_CAP).
inline float civ_bonus_mult(uint8_t civ, uint8_t stat, uint8_t building = BLD_TYPE_COUNT) {
	float m = 1.0f;
	const CivPlayerBonus *b = civ_player_bonus_table();
	for (uint16_t i = 0; i < CIV_PLAYER_BONUS_COUNT; ++i) {
		if (b[i].stat != stat || !(b[i].civs & civ_bit(civ))) continue;
		if (building < BLD_TYPE_COUNT && !(b[i].buildings & bld_bit(building))) continue;
		m *= b[i].mult;
	}
	return m;
}

inline bool civ_aura_affects(int16_t aura, uint16_t tmpl) {
	if (aura < 0 || aura >= (int16_t)CIV_HERO_AURA_COUNT || tmpl >= CIV_UNIT_TEMPLATE_COUNT) return false;
	return (civ_hero_aura_table()[aura].affects[tmpl >> 6] >> (tmpl & 63)) & 1u;
}

inline const CivUnitTemplate &civ_unit_template(uint16_t id) {
	const CivUnitTemplate *t = civ_unit_template_table();
	return t[id < CIV_UNIT_TEMPLATE_COUNT ? id : 0];
}

inline const CivHeroAura &civ_hero_aura(int16_t idx) {
	const CivHeroAura *a = civ_hero_aura_table();
	return a[(idx >= 0 && idx < CIV_HERO_AURA_COUNT) ? idx : 0];
}

// Roster of a civ is the template id range [civ_roster_begin, civ_roster_end).
inline uint16_t civ_roster_begin(uint8_t civ) {
	if (civ >= CIV_COUNT) civ = CIV_ATHENIANS;
	return civ_roster_offsets()[civ];
}

inline uint16_t civ_roster_end(uint8_t civ) {
	if (civ >= CIV_COUNT) civ = CIV_ATHENIANS;
	return civ_roster_offsets()[civ + 1];
}

// The civ's standard unit for an RTS0 class: the citizen-soldier or support
// unit a generic "train a spearman" request should produce. Prefers non-hero,
// non-champion units with the earliest phase. CIV_UNIT_TEMPLATE_NONE if the
// civ has no unit of that class.
struct CivDefaultTemplates {
	uint16_t id[CIV_COUNT][UNIT_TYPE_COUNT];
};

inline uint16_t civ_default_template(uint8_t civ, uint8_t type) {
	static const CivDefaultTemplates cache = [] {
		CivDefaultTemplates out{};
		for (uint8_t c = 0; c < CIV_COUNT; ++c) {
			for (uint8_t t = 0; t < UNIT_TYPE_COUNT; ++t) {
				uint16_t best = CIV_UNIT_TEMPLATE_NONE;
				int best_score = 1 << 30;
				for (uint16_t i = civ_roster_begin(c); i < civ_roster_end(c); ++i) {
					const CivUnitTemplate &u = civ_unit_template(i);
					if (u.unit_type != t) continue;
					int score = u.phase * 4;
					if (u.flags & ROSTER_HERO) score += 100;
					if (u.flags & ROSTER_CHAMPION) score += 20;
					if (u.flags & ROSTER_MERCENARY) score += 2;
					if (!(u.trainers & (bld_bit(BLD_TOWN_CENTER) | bld_bit(BLD_BARRACKS) | bld_bit(BLD_DOCK) |
							bld_bit(BLD_TEMPLE) | bld_bit(BLD_MARKET) | bld_bit(BLD_FORTRESS)))) score += 1;
					if (score < best_score) {
						best_score = score;
						best = i;
					}
				}
				out.id[c][t] = best;
			}
		}
		return out;
	}();
	if (civ >= CIV_COUNT) civ = CIV_ATHENIANS;
	if (type >= UNIT_TYPE_COUNT) return CIV_UNIT_TEMPLATE_NONE;
	return cache.id[civ][type];
}

// UnitTypeInfo view of a roster unit: 0 A.D.'s stats for the civ unit, with
// the class's model, portrait and aggro range. CIV_UNIT_TEMPLATE_NONE (or an
// out-of-range id) falls back to the generic class row.
struct CivTemplateInfos {
	UnitTypeInfo info[CIV_UNIT_TEMPLATE_COUNT];
};

inline const UnitTypeInfo &civ_template_unit_info(uint16_t tmpl, uint8_t type) {
	static const CivTemplateInfos table = [] {
		CivTemplateInfos out{};
		for (uint16_t i = 0; i < CIV_UNIT_TEMPLATE_COUNT; ++i) {
			const CivUnitTemplate &t = civ_unit_template(i);
			UnitTypeInfo u = unit_type_info(t.unit_type);
			u.name = t.name;
			u.hp = t.hp;
			u.speed = t.speed;
			u.attack_range = t.attack_range;
			u.attack_damage = t.attack_damage;
			u.attack_cooldown = t.attack_cooldown;
			u.carry_capacity = t.carry_capacity;
			if (t.attack_range > u.aggro_range) u.aggro_range = t.attack_range;
			u.hack_armor = t.hack_armor;
			u.pierce_armor = t.pierce_armor;
			u.crush_armor = t.crush_armor;
			u.food_cost = t.food_cost;
			u.wood_cost = t.wood_cost;
			u.gold_cost = t.gold_cost;
			u.stone_cost = t.stone_cost;
			u.pop_cost = t.pop_cost;
			u.train_time = t.train_time;
			u.hack_damage = t.hack_damage;
			u.pierce_damage = t.pierce_damage;
			u.crush_damage = t.crush_damage;
			out.info[i] = u;
		}
		return out;
	}();
	if (tmpl < CIV_UNIT_TEMPLATE_COUNT) return table.info[tmpl];
	return unit_type_info(type);
}

// Stats of the unit a civ fields for an RTS0 class (its default template).
inline const UnitTypeInfo &civ_unit_info(uint8_t civ, uint8_t type) {
	return civ_template_unit_info(civ_default_template(civ, type), type);
}

// True if the unit attacks at range: the roster's 0 A.D. attack when the
// unit has a template, else the class rule.
inline bool template_has_ranged_attack(uint16_t tmpl, uint8_t type, uint8_t civ) {
	if (tmpl < CIV_UNIT_TEMPLATE_COUNT) {
		const CivUnitTemplate &t = civ_unit_template(tmpl);
		return (t.flags & ROSTER_RANGED) != 0 && t.attack_damage > 0.0f;
	}
	return unit_has_ranged_attack(type, civ);
}

// Units this civilization can train: every RTS0 class its 0 A.D. roster has.
struct CivTrainMasks {
	uint32_t mask[CIV_COUNT];
};

inline uint32_t civ_train_mask(uint8_t civ) {
	static const CivTrainMasks masks = [] {
		CivTrainMasks out{};
		for (uint8_t c = 0; c < CIV_COUNT; ++c)
			for (uint16_t i = civ_roster_begin(c); i < civ_roster_end(c); ++i)
				out.mask[c] |= unit_bit(civ_unit_template(i).unit_type);
		return out;
	}();
	if (civ >= CIV_COUNT) civ = CIV_ATHENIANS;
	return masks.mask[civ];
}

inline bool civ_can_train(uint8_t civ, uint8_t type) {
	return (civ_train_mask(civ) & unit_bit(type)) != 0;
}

struct CivInfo {
	Civilization civ;
	const char *code;
	const char *name;
	const char *emblem_path;
	const char *description;
	// 0 A.D. civ bonuses that act on gathering and trade. Unit bonuses are
	// folded into each civ's roster; building and population bonuses are in
	// civ_player_bonus_table(). Both come from tools/extract_0ad_rosters.py.
	float metal_gather_mult; // Athenian "Silver Owls"
	float trade_gain_mult; // Persian "Darics", Carthaginian "Commercial Acumen"
};

inline const CivInfo &civ_info(uint8_t civ) {
	static const CivInfo kTable[CIV_COUNT] = {
		// civ, code, name, emblem, desc, metal_mult, trade_mult
		{CIV_ATHENIANS, "athen", "Athenians", "res://assets/textures/ui/emblems/athenians.png",
		 civ_bonus_text(CIV_ATHENIANS),
		 1.10f, 1.0f},
		{CIV_SPARTANS, "spart", "Spartans", "res://assets/textures/ui/emblems/spartans.png",
		 civ_bonus_text(CIV_SPARTANS),
		 1.0f, 1.0f},
		{CIV_PERSIANS, "pers", "Persians", "res://assets/textures/ui/emblems/persians.png",
		 civ_bonus_text(CIV_PERSIANS),
		 1.0f, 1.25f},
		{CIV_ROMANS, "rome", "Romans", "res://assets/textures/ui/emblems/romans.png",
		 civ_bonus_text(CIV_ROMANS),
		 1.0f, 1.0f},
		{CIV_CARTHAGINIANS, "cart", "Carthaginians", "res://assets/textures/ui/emblems/carthaginians.png",
		 civ_bonus_text(CIV_CARTHAGINIANS),
		 1.0f, 1.25f},
		{CIV_MACEDONIANS, "mace", "Macedonians", "res://assets/textures/ui/emblems/macedonians.png",
		 civ_bonus_text(CIV_MACEDONIANS),
		 1.0f, 1.0f},
		{CIV_BRITONS, "brit", "Britons", "res://assets/textures/ui/emblems/britons.png",
		 civ_bonus_text(CIV_BRITONS),
		 1.0f, 1.0f},
		{CIV_GAULS, "gaul", "Gauls", "res://assets/textures/ui/emblems/gauls.png",
		 civ_bonus_text(CIV_GAULS),
		 1.0f, 1.0f},
		{CIV_IBERIANS, "iber", "Iberians", "res://assets/textures/ui/emblems/iberians.png",
		 civ_bonus_text(CIV_IBERIANS),
		 1.0f, 1.0f},
		{CIV_MAURYANS, "maur", "Mauryans", "res://assets/textures/ui/emblems/mauryans.png",
		 civ_bonus_text(CIV_MAURYANS),
		 1.0f, 1.0f},
		{CIV_KUSHITES, "kush", "Kushites", "res://assets/textures/ui/emblems/kushites.png",
		 civ_bonus_text(CIV_KUSHITES),
		 1.0f, 1.0f},
		{CIV_PTOLEMIES, "ptol", "Ptolemies", "res://assets/textures/ui/emblems/ptolemies.png",
		 civ_bonus_text(CIV_PTOLEMIES),
		 1.0f, 1.0f},
		{CIV_SELEUCIDS, "sele", "Seleucids", "res://assets/textures/ui/emblems/seleucids.png",
		 civ_bonus_text(CIV_SELEUCIDS),
		 1.0f, 1.0f},
		{CIV_HAN, "han", "Han Chinese", "res://assets/textures/ui/emblems/han.png",
		 civ_bonus_text(CIV_HAN),
		 1.0f, 1.0f}
	};
	if (civ >= CIV_COUNT) return kTable[CIV_ATHENIANS];
	return kTable[civ];
}

// Prefer extracted `{civcode}_{model}` GLB; ModelLibrary falls back to the generic mesh.
inline const char *civ_unit_model(uint8_t civ, uint8_t type) {
	static char names[CIV_COUNT][UNIT_TYPE_COUNT][40];
	static bool ready = false;
	if (!ready) {
		for (uint8_t c = 0; c < CIV_COUNT; ++c) {
			const char *code = civ_info(c).code;
			for (uint8_t t = 0; t < UNIT_TYPE_COUNT; ++t) {
				std::snprintf(names[c][t], sizeof(names[c][t]), "%s_%s", code, unit_type_info(t).model_id);
			}
		}
		ready = true;
	}
	if (civ >= CIV_COUNT) civ = CIV_ATHENIANS;
	if (type >= UNIT_TYPE_COUNT) type = UNIT_VILLAGER;
	return names[civ][type];
}

inline const char *civ_building_model(uint8_t civ, uint8_t type) {
	static char names[CIV_COUNT][BLD_TYPE_COUNT][48];
	static bool ready = false;
	if (!ready) {
		for (uint8_t c = 0; c < CIV_COUNT; ++c) {
			const char *code = civ_info(c).code;
			for (uint8_t t = 0; t < BLD_TYPE_COUNT; ++t) {
				std::snprintf(names[c][t], sizeof(names[c][t]), "%s_%s", code, building_type_info(t).model_id);
			}
		}
		ready = true;
	}
	if (civ >= CIV_COUNT) civ = CIV_ATHENIANS;
	if (type >= BLD_TYPE_COUNT) type = BLD_HOUSE;
	return names[civ][type];
}

inline const char *civ_building_rubble(uint8_t civ, uint8_t type) {
	static char cc_rubble[CIV_COUNT][48];
	static bool ready = false;
	if (!ready) {
		for (uint8_t c = 0; c < CIV_COUNT; ++c)
			std::snprintf(cc_rubble[c], sizeof(cc_rubble[c]), "%s_town_center_rubble", civ_info(c).code);
		ready = true;
	}
	if (civ >= CIV_COUNT) civ = CIV_ATHENIANS;
	if (type == BLD_TOWN_CENTER)
		return cc_rubble[civ];
	switch (type) {
		case BLD_HOUSE:
		case BLD_STOREHOUSE:
		case BLD_CORRAL:
		case BLD_TOWER:
		case BLD_FARMSTEAD:
			return "rubble_small";
		default:
			return "rubble_large";
	}
}

// 0 A.D. VisualActor/FoundationActor per structure class.
inline const char *civ_building_foundation(uint8_t type) {
	switch (type) {
		case BLD_HOUSE:
		case BLD_STOREHOUSE:
		case BLD_CORRAL:
			return "foundation_3x3";
		case BLD_TOWER:
			return "foundation_3x3_tower";
		case BLD_FARMSTEAD:
			return "foundation_5x5";
		case BLD_BARRACKS:
			return "foundation_6x6";
		case BLD_TEMPLE:
			return "foundation_4x6";
		case BLD_WALL_SEGMENT:
		case BLD_GATE:
			return "foundation_wall";
		case BLD_DOCK:
			return "foundation_dock";
		case BLD_TOWN_CENTER:
		case BLD_MARKET:
		case BLD_FORTRESS:
		case BLD_WONDER:
			return "foundation_8x8";
		default:
			return "foundation_5x5";
	}
}

inline const char *civ_building_scaffold(uint8_t type) {
	switch (type) {
		case BLD_HOUSE:
		case BLD_STOREHOUSE:
		case BLD_CORRAL:
		case BLD_TOWER:
		case BLD_WALL_SEGMENT:
		case BLD_GATE:
			return "scaffold_small";
		default:
			return "scaffold_large";
	}
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
	FAUNA_CARCASS    = 4,
	FAUNA_CORRALLED  = 5
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
	bool is_domestic;
};

inline const FaunaTypeInfo &fauna_type_info(uint8_t ftype) {
	static const FaunaTypeInfo kTable[FAUNA_COUNT] = {
		// name, model, hp, speed, food, agro, dmg, cd, predator, domestic
		{"Red Deer", "deer", 45.0f, 4.5f, 150, 12.0f, 0.0f, 1.0f, false, false},
		{"Wild Boar", "boar", 120.0f, 3.8f, 300, 8.0f, 12.0f, 1.2f, true, false},
		{"Mountain Sheep", "sheep", 35.0f, 2.5f, 100, 10.0f, 0.0f, 1.0f, false, true},
		{"Gray Wolf", "wolf", 80.0f, 4.8f, 200, 10.0f, 10.0f, 1.0f, true, false}
	};
	if (ftype >= FAUNA_COUNT) return kTable[FAUNA_DEER];
	return kTable[ftype];
}

} // namespace rtso

#endif
