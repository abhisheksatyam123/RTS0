// In-match technology effects audit: for every technology and every civ that may research
// it, spawn that civ's whole unit roster and every structure type, read each stat through
// the code the match uses (the details panels, gather/train/cost/range functions), research
// the technology with complete_research, and read them again on the same entities and on
// freshly spawned ones. Each change must be what the technology's effect rows give
// (0 A.D. GetTechModifiedProperty: a replace, then every multiply, then every add) on the
// entities whose classes they affect, and nothing else may change.
//   g++ -std=c++20 -O2 -Isrc tools/audit_tech_effects.cpp src/simulation/simulation.cpp \
//       src/simulation/rms_generator.cpp src/core/job_system.cpp src/network/turn_manager.cpp \
//       src/network/net_host.cpp -lpthread -o /tmp/audit_tech_effects && /tmp/audit_tech_effects [tech_name]
// Prints one line per difference and a summary; exit code 1 when any.
#define private public
#include "simulation/simulation.h"
#undef private
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace rtso;

namespace {

enum UStat {
	U_GATHER = 0, // 7 kinds
	U_CARRY = 7,  // food, wood, stone, metal
	U_DMG = 11,   // hack, pierce, crush, fire
	U_ARMOR = 15, // hack, pierce, crush
	U_HP = 18, U_WALK, U_RANGE, U_TRAIN, U_COST /* 4 */, U_HEAL_RANGE = 26, U_HEAL_INTERVAL, U_TRADE, U_PACK,
	U_VISION, U_SPREAD, U_CAPTURE, U_LOOT /* 4 */, U_COUNT = 37
};
const char *kUStat[U_COUNT] = {"gather_grain", "gather_fruit", "gather_meat", "gather_fish", "gather_wood", "gather_stone",
	"gather_metal", "carry_food", "carry_wood", "carry_stone", "carry_metal", "dmg_hack", "dmg_pierce", "dmg_crush", "dmg_fire",
	"armor_hack", "armor_pierce", "armor_crush", "max_hp", "walk", "range", "train_s", "cost_food", "cost_wood", "cost_stone",
	"cost_metal", "heal_range", "heal_interval", "trade_gain", "pack_s", "vision", "spread", "capture", "loot_food", "loot_wood",
	"loot_stone", "loot_metal"};

enum BStat {
	B_HP = 0, B_PIERCE, B_RANGE, B_BUILD, B_RESEARCH, B_TECH_COST /* 4 */, B_BATCH = 9, B_POP, B_ARROWS, B_PER_UNIT,
	B_GHEAL, B_CAPTURE, B_GREGEN, B_DECAY, B_COST /* 4 */, B_VISION = 21, B_MIN_RANGE, B_INTL_MUL, B_INTL_ADD, B_COUNT
};
const char *kBStat[B_COUNT] = {"max_hp", "arrow_pierce", "arrow_range", "build_s", "research_s", "tech_cost_food",
	"tech_cost_wood", "tech_cost_stone", "tech_cost_metal", "batch_exp", "pop_bonus", "default_arrows", "garrison_arrows",
	"garrison_heal", "max_capture", "garrison_regen", "decay", "cost_food", "cost_wood", "cost_stone", "cost_metal", "vision",
	"min_range", "international_mul", "international_add"};

// Where a TechStat lands for a unit / a structure, and what its adds are scaled by (RTS0
// distances: ranges x0.28).
int unit_slot(uint8_t stat, float &scale) {
	scale = 1.0f;
	if (stat >= TSTAT_GATHER_GRAIN && stat <= TSTAT_GATHER_METAL) return U_GATHER + (stat - TSTAT_GATHER_GRAIN);
	if (stat >= TSTAT_CARRY_FOOD && stat <= TSTAT_CARRY_METAL) return U_CARRY + (stat - TSTAT_CARRY_FOOD);
	if (stat >= TSTAT_ARMOR_HACK && stat <= TSTAT_ARMOR_CRUSH) return U_ARMOR + (stat - TSTAT_ARMOR_HACK);
	if (stat >= TSTAT_COST_FOOD && stat <= TSTAT_COST_METAL) return U_COST + (stat - TSTAT_COST_FOOD);
	if (stat >= TSTAT_UNIT_LOOT_FOOD && stat <= TSTAT_UNIT_LOOT_METAL) return U_LOOT + (stat - TSTAT_UNIT_LOOT_FOOD);
	switch (stat) {
		case TSTAT_HEALTH: return U_HP;
		case TSTAT_SPEED: return U_WALK;
		case TSTAT_RANGE: scale = COMBAT_DISTANCE_SCALE; return U_RANGE;
		case TSTAT_BUILD_TIME: return U_TRAIN;
		case TSTAT_HEAL_RANGE: return U_HEAL_RANGE; // details divide the 0.28 back out
		case TSTAT_HEAL_INTERVAL: return U_HEAL_INTERVAL;
		case TSTAT_TRADE_GAIN: return U_TRADE;
		case TSTAT_PACK_TIME: return U_PACK;
		case TSTAT_VISION: scale = 0.28f; return U_VISION;
		case TSTAT_SPREAD: return U_SPREAD;
		case TSTAT_CAPTURE_ATTACK: return U_CAPTURE;
		default: return -1;
	}
}
int dmg_slot(uint8_t stat, bool &ranged_attack) {
	switch (stat) {
		case TSTAT_MELEE_HACK: case TSTAT_MELEE_PIERCE: case TSTAT_MELEE_CRUSH:
			ranged_attack = false; return U_DMG + (stat - TSTAT_MELEE_HACK);
		case TSTAT_RANGED_HACK: case TSTAT_RANGED_PIERCE: case TSTAT_RANGED_CRUSH:
			ranged_attack = true; return U_DMG + (stat - TSTAT_RANGED_HACK);
		case TSTAT_MELEE_FIRE: ranged_attack = false; return U_DMG + 3;
		case TSTAT_RANGED_FIRE: ranged_attack = true; return U_DMG + 3;
		default: return -1;
	}
}
int bld_slot(uint8_t stat, uint8_t op, float &scale) {
	scale = 1.0f;
	if (stat >= TSTAT_TECH_COST_FOOD && stat <= TSTAT_TECH_COST_METAL) return B_TECH_COST + (stat - TSTAT_TECH_COST_FOOD);
	if (stat >= TSTAT_COST_FOOD && stat <= TSTAT_COST_METAL) return B_COST + (stat - TSTAT_COST_FOOD);
	switch (stat) {
		case TSTAT_HEALTH: return B_HP;
		case TSTAT_RANGED_PIERCE: return B_PIERCE;
		case TSTAT_RANGE: scale = COMBAT_DISTANCE_SCALE; return B_RANGE;
		case TSTAT_BUILD_TIME: return B_BUILD;
		case TSTAT_RESEARCH_TIME: return B_RESEARCH;
		case TSTAT_BATCH_TIME: return B_BATCH;
		case TSTAT_POP_BONUS: return B_POP;
		case TSTAT_ARROWS: return B_ARROWS;
		case TSTAT_GARRISON_ARROWS: return B_PER_UNIT;
		case TSTAT_GARRISON_HEAL: return B_GHEAL;
		case TSTAT_CAPTURE_POINTS: return B_CAPTURE;
		case TSTAT_GARRISON_REGEN: return B_GREGEN;
		case TSTAT_TERRITORY_DECAY: return B_DECAY;
		case TSTAT_VISION: scale = 0.28f; return B_VISION;
		case TSTAT_MIN_RANGE: scale = 0.28f; return B_MIN_RANGE;
		case TSTAT_TRADE_INTERNATIONAL: return op == TOP_ADD ? B_INTL_ADD : B_INTL_MUL;
		default: return -1;
	}
}

using UVec = std::vector<float>;

UVec measure_unit(Simulation &s, uint32_t id) {
	UVec v(U_COUNT, 0.0f);
	const int32_t i = s.lookup_unit(id);
	if (i < 0) return v;
	const Simulation::UnitDetails d = s.unit_details((size_t)i);
	const uint8_t ut = s.units_.unit_type[i];
	const uint16_t tmpl = s.units_.tmpl[i];
	const uint8_t team = s.units_.team[i];
	const uint8_t gcls = unit_gather_class(ut, tmpl);
	for (uint8_t k = 0; k < 7; ++k)
		v[U_GATHER + k] = gcls != GATHER_NONE ? s.unit_gather_rate((size_t)i, k, gather_class_rate(gcls, k), false) : 0.0f;
	for (uint8_t r = 0; r < 4; ++r) {
		static constexpr uint8_t kRes[4] = {RES_FOOD, RES_WOOD, RES_STONE, RES_GOLD};
		v[U_CARRY + r] = (float)s.carry_cap((size_t)i, kRes[r]);
	}
	for (int k = 0; k < 4; ++k) v[U_DMG + k] = d.damage[k];
	for (int k = 0; k < 3; ++k) v[U_ARMOR + k] = d.resistance[k];
	v[U_HP] = s.units_.max_hp[i];
	v[U_WALK] = s.unit_walk_speed((size_t)i);
	v[U_RANGE] = d.ranged ? s.units_.attack_range[i] : 0.0f;
	v[U_TRAIN] = s.unit_train_seconds(team, tmpl);
	float cost[RES_COUNT] = {};
	s.unit_train_cost(team, tmpl, s.unit_info_at((size_t)i), cost, false);
	v[U_COST + 0] = cost[RES_FOOD];
	v[U_COST + 1] = cost[RES_WOOD];
	v[U_COST + 2] = cost[RES_STONE];
	v[U_COST + 3] = cost[RES_GOLD];
	v[U_HEAL_RANGE] = d.heal_hp > 0.0f ? d.heal_range : 0.0f;
	v[U_HEAL_INTERVAL] = d.heal_hp > 0.0f ? d.heal_interval : 0.0f;
	v[U_TRADE] = (ut == UNIT_TRADER || ut == UNIT_MERCHANT_SHIP) ? (float)s.trader_gain_at(400.0, 0.0, team, ut) : 0.0f;
	v[U_PACK] = s.pack_time_s((size_t)i);
	v[U_VISION] = s.unit_vision_at((size_t)i);
	v[U_SPREAD] = d.ranged ? s.unit_combat_at((size_t)i).spread * s.unit_tech_mods(team, ut, tmpl).spread : 0.0f;
	v[U_CAPTURE] = d.capture;
	// Loot.js GetResources on this unit's death, read off what a looter of player 1 collects.
	{
		const float before[RES_COUNT] = {s.players_[1].res[RES_FOOD], s.players_[1].res[RES_WOOD], s.players_[1].res[RES_STONE],
			s.players_[1].res[RES_GOLD]};
		uint32_t looter = 0;
		for (size_t u = 0; u < s.units_.count; ++u)
			if (s.units_.team[u] == 1) { looter = s.units_.id[u]; break; }
		if (looter) s.collect_unit_loot(looter, 1, (size_t)i);
		v[U_LOOT + 0] = s.players_[1].res[RES_FOOD] - before[0];
		v[U_LOOT + 1] = s.players_[1].res[RES_WOOD] - before[1];
		v[U_LOOT + 2] = s.players_[1].res[RES_STONE] - before[2];
		v[U_LOOT + 3] = s.players_[1].res[RES_GOLD] - before[3];
		s.players_[1].res[RES_FOOD] = before[0];
		s.players_[1].res[RES_WOOD] = before[1];
		s.players_[1].res[RES_STONE] = before[2];
		s.players_[1].res[RES_GOLD] = before[3];
	}
	return v;
}

UVec measure_bld(Simulation &s, uint32_t id) {
	UVec v(B_COUNT, 0.0f);
	const Simulation::Building *bp = nullptr;
	for (const auto &b : s.buildings()) if (b.id == id) bp = &b;
	if (!bp) return v;
	const Simulation::Building &b = *bp;
	const Simulation::StructureDetails d = s.structure_details(b);
	v[B_HP] = b.max_hp;
	v[B_PIERCE] = d.damage[1];
	v[B_RANGE] = d.shoots ? s.building_arrow_range(b) : 0.0f;
	v[B_BUILD] = s.structure_build_time_for(b.team, b.type);
	// A technology researched there, for Researcher/TechCostMultiplier.
	for (uint8_t t = 1; t < TECH_COUNT; ++t) {
		if (tech_info(t).researcher != b.type) continue;
		float paid[RES_COUNT] = {}, time = 0.0f;
		s.tech_price(b.team, t, b.type, paid, time);
		v[B_RESEARCH] = time / std::max(0.001f, tech_info(t).research_time);
		const int kRes[4] = {RES_FOOD, RES_WOOD, RES_STONE, RES_GOLD};
		const int base[4] = {tech_info(t).food_cost, tech_info(t).wood_cost, tech_info(t).stone_cost, tech_info(t).gold_cost};
		for (int r = 0; r < 4; ++r) v[B_TECH_COST + r] = base[r] > 0 ? paid[kRes[r]] / (float)base[r] : 0.0f;
		break;
	}
	v[B_BATCH] = std::log(s.batch_time_multiplier(b.team, b.type, 5, b.civ)) / std::log(5.0f);
	v[B_POP] = (float)d.pop_bonus;
	v[B_ARROWS] = d.shoots ? (float)d.default_arrows : 0.0f;
	v[B_PER_UNIT] = d.shoots ? d.per_unit : 0.0f;
	v[B_GHEAL] = b.garrison_cap > 0 ? s.structure_garrison_heal(b) : 0.0f;
	v[B_CAPTURE] = s.structure_max_capture(b);
	v[B_GREGEN] = s.structure_capture_garrison_regen(b);
	v[B_DECAY] = s.structure_decay_rate(b);
	float cost[RES_COUNT] = {};
	s.structure_cost(b.team, b.type, -1.0f, cost, b.civ);
	v[B_COST + 0] = cost[RES_FOOD];
	v[B_COST + 1] = cost[RES_WOOD];
	v[B_COST + 2] = cost[RES_STONE];
	v[B_COST + 3] = cost[RES_GOLD];
	v[B_VISION] = s.building_vision_range(b);
	// Murder Holes: the minimum range tick_combat uses (< 0: the structure's own).
	v[B_MIN_RANGE] = s.tech_mods(b.team).bld_min_range[b.type];
	v[B_INTL_MUL] = s.tech_mods(b.team).bld_international_mul[b.type];
	v[B_INTL_ADD] = s.tech_mods(b.team).bld_international_add[b.type];
	return v;
}

struct Fold { bool set = false; float set_v = 0.0f, mul = 1.0f, add = 0.0f; };
float fold(float base, const Fold &f) { return (f.set ? f.set_v : base) * f.mul + f.add; }
void put(Fold &f, uint8_t op, float value, float scale) {
	if (op == TOP_SET) { f.set = true; f.set_v = value * scale; f.mul = 1.0f; f.add = 0.0f; }
	else if (op == TOP_MUL) f.mul *= value;
	else f.add += value * scale;
}

bool close(float a, float b, bool integral) {
	if (integral) return std::fabs(a - b) <= 1.01f;
	return std::fabs(a - b) <= 2e-3f * std::max(1.0f, std::fabs(b));
}
bool unit_integral(int k) { return (k >= U_CARRY && k < U_CARRY + 4) || k == U_TRADE || (k >= U_LOOT && k < U_LOOT + 4); }
bool bld_integral(int k) { return k == B_POP || k == B_ARROWS; }

int diffs = 0, changes = 0;
std::map<std::string, int> per_tech;

void report(const char *tech, const char *civ, const char *who, const char *stat, const char *when, float before, float after,
		float want) {
	std::printf("[diff] %s civ=%s %s %s %s: %.4g -> %.4g, want %.4g\n", tech, civ, who, stat, when, before, after, want);
	++diffs;
	++per_tech[tech];
}

struct Ent { bool bld; uint16_t key; uint32_t id; std::string name; };

// A match of player 0 (civ) with every roster template of its civ and every structure type,
// its technology's prerequisites (and phase) researched, the technology too when `with`.
std::vector<Ent> build(Simulation &s, uint8_t tech, uint8_t civ, bool with) {
	const TechInfo &ti = tech_info(tech);
	s.set_sim_seed(5);
	s.reset();
	s.set_team_civ(0, civ);
	s.set_team_civ(1, civ == CIV_ATHENIANS ? CIV_SPARTANS : CIV_ATHENIANS);
	for (uint8_t p = 0; p < 2; ++p) s.set_player_active(p, true, false);
	for (uint8_t t = ti.prerequisite; t != TECH_NONE; t = tech_info(t).prerequisite) s.complete_research(0, t);
	if (tech != TECH_PHASE_TOWN && tech != TECH_PHASE_CITY) {
		if (ti.required_phase >= 2) s.complete_research(0, TECH_PHASE_TOWN);
		if (ti.required_phase >= 3) s.complete_research(0, TECH_PHASE_CITY);
	} else if (tech == TECH_PHASE_CITY) s.complete_research(0, TECH_PHASE_TOWN);
	if (with) s.complete_research(0, tech);
	s.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
	s.spawn_unit(-60.0f, -60.0f, 1, UNIT_SPEARMAN);
	std::vector<Ent> ents;
	int n = 0;
	for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT; ++t) {
		if (civ_unit_template(t).civ != civ) continue;
		const float x = 30.0f + 6.0f * (float)(n % 20), z = -40.0f + 6.0f * (float)(n / 20);
		++n;
		if (uint32_t id = s.spawn_unit(x, z, 0, (UnitType)civ_unit_template(t).unit_type, 0.0f, t))
			ents.push_back({false, t, id, civ_unit_template(t).id});
	}
	n = 0;
	for (uint8_t b = 1; b < BLD_TYPE_COUNT; ++b) {
		if (!civ_can_build(civ, b)) continue;
		const float x = -200.0f + 40.0f * (float)(n % 10), z = 60.0f + 40.0f * (float)(n / 10);
		++n;
		if (uint32_t id = s.spawn_building_world(x, z, b, 0, 1.0f)) ents.push_back({true, b, id, building_type_info(b).name});
	}
	s.rebuild_all_tech_mods(); // the heroes' global auras
	return ents;
}

bool has_stat(Simulation &s, const Ent &e, int k, float b0) {
	if (e.bld) {
		const Simulation::Building *bp = nullptr;
		for (const auto &b : s.buildings()) if (b.id == e.id) bp = &b;
		if (!bp) return false;
		if (k == B_GHEAL || k == B_PER_UNIT) return bp->garrison_cap > 0;
		if (k == B_PIERCE || k == B_RANGE || k == B_ARROWS) return s.structure_details(*bp).shoots;
		if (k == B_RESEARCH || (k >= B_TECH_COST && k < B_TECH_COST + 4) || k == B_POP || k == B_CAPTURE || k == B_DECAY ||
				k == B_VISION) return b0 != 0.0f;
		return true;
	}
	const int32_t i = s.lookup_unit(e.id);
	if (i < 0) return false;
	const uint8_t ut = s.units_.unit_type[i];
	if (k < U_CARRY + 4)
		return unit_gather_class(ut, e.key) != GATHER_NONE && !(civ_unit_template(e.key).flags & ROSTER_NO_GATHER) &&
			(k >= U_CARRY || b0 != 0.0f);
	if (k >= U_DMG && k < U_DMG + 4) return s.unit_info_at((size_t)i).attack_damage > 0.0f && (k < U_DMG + 3 || b0 != 0.0f);
	if (k == U_HEAL_RANGE || k == U_HEAL_INTERVAL || k == U_RANGE || k == U_SPREAD || k == U_CAPTURE || k == U_TRADE ||
			k == U_PACK) return b0 != 0.0f;
	return true;
}

void audit(uint8_t tech, uint8_t civ) {
	const TechInfo &ti = tech_info(tech);
	const TechEffect *fx = tech_effects_table() + ti.effect_begin;
	const char *civ_name = civ_info(civ).name;
	Simulation a, b;
	std::vector<Ent> ents = build(a, tech, civ, false);
	std::vector<UVec> before;
	for (const Ent &e : ents) before.push_back(e.bld ? measure_bld(a, e.id) : measure_unit(a, e.id));
	a.complete_research(0, tech);
	std::vector<Ent> ents_b = build(b, tech, civ, true);
	for (size_t q = 0; q < ents.size(); ++q) {
		const Ent &e = ents[q];
		const int count = e.bld ? B_COUNT : U_COUNT;
		std::vector<Fold> f(count);
		std::vector<bool> touched(count, false);
		const TechClasses cls = e.bld ? building_tech_classes((uint8_t)e.key) : civ_unit_template(e.key).tech_classes;
		const bool ranged = !e.bld && template_has_ranged_attack(e.key, civ_unit_template(e.key).unit_type, civ);
		for (uint16_t k = 0; k < ti.effect_count; ++k) {
			const TechEffect &r = fx[k];
			if (!tech_effect_applies(r, cls)) continue;
			float scale = 1.0f;
			int slot = -1;
			if (e.bld) slot = bld_slot(r.stat, r.op, scale);
			else {
				bool want_ranged = false;
				slot = dmg_slot(r.stat, want_ranged);
				if (slot >= 0 && want_ranged != ranged) continue; // the attack the unit lacks
				if (slot < 0) slot = unit_slot(r.stat, scale);
				if ((slot == U_RANGE || slot == U_SPREAD) && !ranged) continue;
			}
			if (slot < 0) continue;
			put(f[slot], r.op, r.value, scale);
			touched[slot] = true;
		}
		// The phases' civ forms RTS0 keeps out of the table: phase_{town,city}_athen (Workers
		// metal.ore x1.1), civbonuses/cart_stone_02/03 (Workers stone.rock x1.25) and
		// phase_{town,city}_pers (Stables Trainer/BatchTimeModifier -0.1).
		if (tech == TECH_PHASE_TOWN || tech == TECH_PHASE_CITY) {
			if (!e.bld && (cls & TC_WORKER) && civ == CIV_ATHENIANS) { put(f[U_GATHER + 6], TOP_MUL, 1.1f, 1.0f); touched[U_GATHER + 6] = true; }
			if (!e.bld && (cls & TC_WORKER) && civ == CIV_CARTHAGINIANS) { put(f[U_GATHER + 5], TOP_MUL, 1.25f, 1.0f); touched[U_GATHER + 5] = true; }
			if (e.bld && e.key == BLD_STABLE && civ == CIV_PERSIANS) { put(f[B_BATCH], TOP_ADD, -0.1f, 1.0f); touched[B_BATCH] = true; }
		}
		const UVec now = e.bld ? measure_bld(a, e.id) : measure_unit(a, e.id);
		const bool same = q < ents_b.size() && ents_b[q].key == e.key && ents_b[q].bld == e.bld;
		const UVec fresh = same ? (e.bld ? measure_bld(b, ents_b[q].id) : measure_unit(b, ents_b[q].id)) : now;
		for (int k = 0; k < count; ++k) {
			const float b0 = before[q][k];
			float want = touched[k] ? fold(b0, f[k]) : b0;
			// A stat the entity doesn't have stays as it is (no Heal, no attack, no BuildingAI, no
			// GarrisonHolder, no ResourceGatherer); a 0 one it has takes adds (template Crush 0).
			if (!has_stat(a, e, k, b0)) want = b0;
			if (std::fabs(want - b0) > 1e-5f) ++changes;
			const bool integral = e.bld ? bld_integral(k) : unit_integral(k);
			const char *stat = e.bld ? kBStat[k] : kUStat[k];
			if (!close(now[k], want, integral)) report(ti.template_name, civ_name, e.name.c_str(), stat, "existing", b0, now[k], want);
			else if (!close(fresh[k], want, integral)) report(ti.template_name, civ_name, e.name.c_str(), stat, "new", b0, fresh[k], want);
		}
	}
}

// Effects only time shows: Health/RegenRate and IdleRegenRate (hp a second gained by a unit at
// half health over 4 s), ResourceTrickle (the player's stock over 10 s) and
// TerritoryInfluence/Radius (the cells a lone Civic Center claims; the area goes as the square).
void audit_over_time(uint8_t tech, uint8_t civ) {
	const TechInfo &ti = tech_info(tech);
	const TechEffect *fx = tech_effects_table() + ti.effect_begin;
	bool regen = false, trickle = false, territory = false;
	for (uint16_t k = 0; k < ti.effect_count; ++k) {
		regen |= fx[k].stat == TSTAT_REGEN || fx[k].stat == TSTAT_IDLE_REGEN;
		trickle |= fx[k].stat >= TSTAT_TRICKLE_FOOD && fx[k].stat <= TSTAT_TRICKLE_METAL;
		territory |= fx[k].stat == TSTAT_TERRITORY_RADIUS;
	}
	if (!regen && !trickle && !territory) return;
	const char *civ_name = civ_info(civ).name;
	auto setup = [&](Simulation &s, bool with) {
		s.set_sim_seed(5);
		s.reset();
		s.set_team_civ(0, civ);
		s.set_player_active(0, true, false);
		for (uint8_t t = ti.prerequisite; t != TECH_NONE; t = tech_info(t).prerequisite) s.complete_research(0, t);
		if (ti.required_phase >= 2 && tech != TECH_PHASE_TOWN) s.complete_research(0, TECH_PHASE_TOWN);
		if (ti.required_phase >= 3 && tech != TECH_PHASE_CITY) s.complete_research(0, TECH_PHASE_CITY);
		if (with) s.complete_research(0, tech);
		s.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
	};
	auto run = [](Simulation &s, float sec) {
		for (int t = 0; t < (int)(sec / Simulation::FIXED_STEP + 0.5f); ++t) s.tick(Simulation::FIXED_STEP);
	};
	if (regen) {
		Simulation a, b;
		std::vector<std::pair<uint16_t, uint32_t>> ua, ub;
		for (int w = 0; w < 2; ++w) {
			Simulation &s = w ? b : a;
			auto &ids = w ? ub : ua;
			setup(s, w == 1);
			int n = 0;
			for (uint16_t t = 0; t < CIV_UNIT_TEMPLATE_COUNT; ++t) {
				if (civ_unit_template(t).civ != civ) continue;
				const float x = 40.0f + 8.0f * (float)(n % 20), z = -60.0f + 8.0f * (float)(n / 20);
				++n;
				if (uint32_t id = s.spawn_unit(x, z, 0, (UnitType)civ_unit_template(t).unit_type, 0.0f, t)) ids.push_back({t, id});
			}
			s.rebuild_all_tech_mods();
			for (auto &[t, id] : ids) {
				const int32_t i = s.lookup_unit(id);
				s.units_.hp[i] = s.units_.max_hp[i] * 0.5f;
			}
		}
		std::vector<float> ha, hb;
		for (auto &[t, id] : ua) ha.push_back(a.units_.hp[a.lookup_unit(id)]);
		for (auto &[t, id] : ub) hb.push_back(b.units_.hp[b.lookup_unit(id)]);
		run(a, 4.0f);
		run(b, 4.0f);
		for (size_t q = 0; q < ua.size() && q < ub.size(); ++q) {
			const uint16_t t = ua[q].first;
			float want = 0.0f;
			for (uint16_t k = 0; k < ti.effect_count; ++k)
				if ((fx[k].stat == TSTAT_REGEN || fx[k].stat == TSTAT_IDLE_REGEN) && tech_effect_applies(fx[k], civ_unit_template(t).tech_classes))
					want += fx[k].value;
			const int32_t ia = a.lookup_unit(ua[q].second), ib = b.lookup_unit(ub[q].second);
			if (ia < 0 || ib < 0) continue;
			// Healers heal the others (and are healed): never idle, so no IdleRegenRate.
			if (unit_is_healer(t, civ_unit_template(t).unit_type)) continue;
			const float ra = (a.units_.hp[ia] - ha[q]) / 4.0f, rb = (b.units_.hp[ib] - hb[q]) / 4.0f;
			if (std::fabs(a.units_.hp[ia] - a.units_.max_hp[ia]) < 1e-3f || std::fabs(b.units_.hp[ib] - b.units_.max_hp[ib]) < 1e-3f) continue;
			if (std::fabs(want) > 0.0f) ++changes;
			if (std::fabs((rb - ra) - want) > 0.05f)
				report(ti.template_name, civ_name, civ_unit_template(t).id, "regen_hp_per_s", "over 4 s", ra, rb, ra + want);
		}
	}
	if (trickle) {
		Simulation a, b;
		float ga[RES_COUNT], gb[RES_COUNT];
		for (int w = 0; w < 2; ++w) {
			Simulation &s = w ? b : a;
			setup(s, w == 1);
			int n = 0;
			for (uint8_t bt = 1; bt < BLD_TYPE_COUNT; ++bt) {
				if (!civ_can_build(civ, bt)) continue;
				s.spawn_building_world(-200.0f + 40.0f * (float)(n % 10), 60.0f + 40.0f * (float)(n / 10), bt, 0, 1.0f);
				++n;
			}
			float *g = w ? gb : ga;
			for (int r = 0; r < RES_COUNT; ++r) g[r] = s.players_[0].res[r];
			run(s, 10.0f);
			for (int r = 0; r < RES_COUNT; ++r) g[r] = s.players_[0].res[r] - g[r];
		}
		static const char *kRes[RES_COUNT] = {"wood", "metal", "stone", "food"};
		for (int r = 0; r < RES_COUNT; ++r)
			if (std::fabs(gb[r] - ga[r]) > 1e-3f)
				std::printf("[trickle] %s civ=%s %s over 10 s: %.2f -> %.2f\n", ti.template_name, civ_name, kRes[r], ga[r], gb[r]);
		++changes;
	}
	if (territory) {
		Simulation a, b;
		int cells[2] = {0, 0};
		for (int w = 0; w < 2; ++w) {
			Simulation &s = w ? b : a;
			// The technology's own phase gates nothing here: research the one before it, and it.
			s.set_sim_seed(5);
			s.reset();
			s.set_team_civ(0, civ);
			s.set_player_active(0, true, false);
			if (tech == TECH_PHASE_CITY) s.complete_research(0, TECH_PHASE_TOWN);
			if (w) s.complete_research(0, tech);
			s.spawn_building_world(0.0f, 0.0f, BLD_TOWN_CENTER, 0, 1.0f);
			s.rebuild_territory();
			for (float z = -s.map_half(); z < s.map_half(); z += 1.0f)
				for (float x = -s.map_half(); x < s.map_half(); x += 1.0f)
					if (s.territory_owner_at(x, z) == 0) ++cells[w];
		}
		float want = 1.0f;
		for (uint16_t k = 0; k < ti.effect_count; ++k)
			if (fx[k].stat == TSTAT_TERRITORY_RADIUS && tech_effect_applies(fx[k], building_tech_classes(BLD_TOWN_CENTER))) want *= fx[k].value;
		const float got = std::sqrt((float)cells[1] / std::max(1, cells[0]));
		++changes;
		if (std::fabs(got - want) > 0.04f) report(ti.template_name, civ_name, "Civic Center", "territory_radius", "cells", 1.0f, got, want);
	}
}

} // namespace

int main(int argc, char **argv) {
	const char *only = argc > 1 ? argv[1] : nullptr;
	int audited = 0;
	for (uint8_t t = 1; t < TECH_COUNT; ++t) {
		const TechInfo &ti = tech_info(t);
		if (only && std::strcmp(only, ti.template_name)) continue;
		if (ti.effect_count == 0) continue;
		for (uint8_t c = 0; c < CIV_COUNT; ++c) {
			if (!is_playable_civ(c) || !((ti.civ_mask >> c) & 1u)) continue;
			audit(t, c);
			audit_over_time(t, c);
			++audited;
		}
	}
	std::printf("[summary] %d tech/civ runs, %d changes checked, %d differences\n", audited, changes, diffs);
	for (const auto &[k, v] : per_tech) std::printf("  %s: %d\n", k.c_str(), v);
	return diffs ? 1 : 0;
}
