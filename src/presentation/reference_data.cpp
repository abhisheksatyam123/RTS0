#include "reference_data.h"

#include <cstring>

#include "../simulation/sim_types.h"
#include "../simulation/unit_trainer_data.h"
#include "../simulation/tech_tooltip_data.h"

#include <algorithm>
#include <cmath>

#include <vector>

namespace {

String cost_text(int food, int wood, int stone, int metal) {
	String s;
	auto add = [&](int v, const char *what) {
		if (v <= 0) return;
		if (!s.is_empty()) s += ", ";
		s += String::num_int64(v) + " " + what;
	};
	add(food, "food");
	add(wood, "wood");
	add(stone, "stone");
	add(metal, "metal");
	return s;
}

Dictionary cost_dict(int food, int wood, int stone, int metal) {
	Dictionary c;
	c["food"] = food;
	c["wood"] = wood;
	c["stone"] = stone;
	c["metal"] = metal;
	return c;
}

// 0 A.D. Identity classes the combat tables carry (civ_attack_data.h CombatClass order).
// Trader: the pirate ships' Brigandry bonus (mixins/pirate.xml, x2 against Traders).
static_assert(rtso::COMBAT_CLASS_COUNT == 12, "combat classes changed: update kCombatClassNames");
const char *const kCombatClassNames[rtso::COMBAT_CLASS_COUNT] = {
	"Building", "Cavalry", "Field", "Hero", "Human", "Organic", "Ship", "Siege", "Soldier", "Structure", "Trader", "Unit",
};

String class_list(const rtso::CombatClasses &c) {
	String s;
	for (uint8_t k = 0; k < rtso::COMBAT_CLASS_COUNT; ++k) {
		if (!c.has(k)) continue;
		if (!s.is_empty()) s += " ";
		s += kCombatClassNames[k];
	}
	return s;
}

// 0 A.D. tooltips.js getResistanceTooltip: level and the damage it stops (0.9^level).
Dictionary armour_dict(float hack, float pierce, float crush) {
	Dictionary a;
	a["hack"] = hack;
	a["pierce"] = pierce;
	a["crush"] = crush;
	a["hack_pct"] = (1.0 - std::pow(0.9, (double)hack)) * 100.0;
	a["pierce_pct"] = (1.0 - std::pow(0.9, (double)pierce)) * 100.0;
	a["crush_pct"] = (1.0 - std::pow(0.9, (double)crush)) * 100.0;
	return a;
}

Array bonus_list(const rtso::CombatProfile &p) {
	Array out;
	for (uint8_t k = 0; k < p.bonus_count && k < 4; ++k) {
		const rtso::CombatBonus &b = p.bonus[k];
		Dictionary d;
		String target = class_list(b.classes);
		if (b.civ < rtso::CIV_COUNT) target = String(rtso::civ_info(b.civ).name) + " " + target;
		d["classes"] = target;
		d["multiplier"] = b.multiplier;
		out.push_back(d);
	}
	return out;
}

// RTS0 distances and speeds back in 0 A.D. metres (extract_0ad_rosters.py scales).
constexpr float kSpeedScale = 0.356f;
constexpr float kMeleeRangeScale = 0.6f;

String unit_portrait(uint8_t unit_type) {
	return String("res://assets/textures/ui/portraits/") + rtso::unit_type_portrait(unit_type) + ".png";
}

String building_portrait(uint8_t type) {
	return String("res://assets/textures/ui/portraits/") + rtso::building_type_portrait(type) + ".png";
}

Dictionary unit_entry(uint16_t t) {
	const auto &u = rtso::civ_unit_template(t);
	Dictionary ud;
	ud["tmpl"] = t;
	ud["id"] = String::utf8(u.id);
	ud["name"] = String::utf8(u.name);
	ud["specific_name"] = String::utf8(u.specific_name ? u.specific_name : "");
	ud["portrait"] = unit_portrait(u.unit_type);
	ud["phase"] = u.phase;
	ud["cost"] = cost_text(u.food_cost, u.wood_cost, u.stone_cost, u.gold_cost);
	ud["flags"] = u.flags;
	return ud;
}

// civ: the civ whose specificName to give (TemplateLoader.loadTechnologyTemplate), 0xFF none.
// TemplateLister discovery order: mixins/builder.xml's Builder list, then the civ mixin's
// own structures (mixins/civ/{civ}.xml merges them after), here in BuildingType order.
std::vector<uint8_t> builder_order(uint8_t c) {
	static constexpr uint8_t kBuilderXml[] = {
		rtso::BLD_TOWN_CENTER, rtso::BLD_CRANNOG, rtso::BLD_MILITARY_COLONY, rtso::BLD_WAGON_ENCAMPMENT,
		rtso::BLD_HOUSE, rtso::BLD_ICE_HOUSE, rtso::BLD_PYRAMID_SMALL, rtso::BLD_APARTMENT,
		rtso::BLD_STOREHOUSE, rtso::BLD_FARMSTEAD, rtso::BLD_FIELD, rtso::BLD_CORRAL, rtso::BLD_DOCK,
		rtso::BLD_KENNEL, rtso::BLD_BARRACKS, rtso::BLD_STABLE, rtso::BLD_GREAT_HALL,
		rtso::BLD_ELEPHANT_STABLE, rtso::BLD_FORGE, rtso::BLD_TEMPLE, rtso::BLD_MARKET, rtso::BLD_MINISTRY,
		rtso::BLD_OUTPOST, rtso::BLD_SENTRY_TOWER, rtso::BLD_TOWER, rtso::BLD_ARSENAL, rtso::BLD_ACADEMY,
		rtso::BLD_FORTRESS, rtso::BLD_PALISADE, rtso::BLD_SIEGE_WALL, rtso::BLD_WALL_SEGMENT,
		rtso::BLD_THEATER, rtso::BLD_TACHARA, rtso::BLD_WONDER,
	};
	std::vector<uint8_t> out;
	bool listed[rtso::BLD_TYPE_COUNT] = {};
	for (uint8_t b : kBuilderXml) {
		listed[b] = true;
		if (rtso::civ_can_build(c, b)) out.push_back(b);
	}
	for (uint8_t b = 0; b < rtso::BLD_TYPE_COUNT; ++b)
		if (!listed[b] && rtso::civ_can_build(c, b)) out.push_back(b);
	return out;
}

Dictionary tech_entry(uint8_t id, uint8_t civ = 0xFF) {
	const auto &ti = rtso::tech_info(id);
	Dictionary td;
	td["id"] = id;
	td["name"] = String::utf8(ti.name);
	const char *specific = civ < rtso::CIV_COUNT ? rtso::tech_specific_name(id, civ) : nullptr;
	td["specific_name"] = specific ? String::utf8(specific) : String();
	td["description"] = String::utf8(ti.desc ? ti.desc : "");
	td["phase"] = ti.required_phase;
	td["cost"] = cost_text(ti.food_cost, ti.wood_cost, ti.stone_cost, ti.gold_cost);
	td["portrait"] = RtsoReference::phase_icon(ti.required_phase);
	td["template"] = String(ti.template_name ? ti.template_name : "");
	return td;
}

bool tech_civ_specific(uint8_t id) {
	// 0 A.D. civinfo "Specific Technologies": requirements that name a civ. A
	// tech most civs may research is generic even if a few are excluded.
	int civs = 0;
	for (uint8_t c = 0; c < rtso::CIV_COUNT; ++c)
		if (rtso::is_playable_civ(c) && rtso::civ_structures_offer_tech(id, c)) ++civs;
	return civs * 2 < rtso::PLAYABLE_CIV_COUNT;
}

} // namespace

void RtsoReference::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_civs"), &RtsoReference::get_civs);
	ClassDB::bind_method(D_METHOD("get_civ_info", "civ"), &RtsoReference::get_civ_info);
	ClassDB::bind_method(D_METHOD("get_structure_tree", "civ"), &RtsoReference::get_structure_tree);
	ClassDB::bind_method(D_METHOD("get_trainer_units", "civ"), &RtsoReference::get_trainer_units);
	ClassDB::bind_method(D_METHOD("get_unit", "tmpl"), &RtsoReference::get_unit);
	ClassDB::bind_method(D_METHOD("get_fauna", "fauna_type"), &RtsoReference::get_fauna);
	ClassDB::bind_method(D_METHOD("get_catafalques"), &RtsoReference::get_catafalques);
	ClassDB::bind_method(D_METHOD("get_structure", "type", "civ"), &RtsoReference::get_structure);
	ClassDB::bind_method(D_METHOD("get_tech", "id", "civ"), &RtsoReference::get_tech);
	ClassDB::bind_static_method("RtsoReference", D_METHOD("phase_icon", "phase"), &RtsoReference::phase_icon);
}

Array RtsoReference::get_civs() const {
	Array out;
	for (uint8_t c = 0; c < rtso::CIV_COUNT; ++c) {
		if (!rtso::is_playable_civ(c)) continue;
		const auto &ci = rtso::civ_info(c);
		Dictionary d;
		d["id"] = c;
		d["code"] = ci.code;
		d["name"] = ci.name;
		d["emblem"] = ci.emblem_path;
		out.push_back(d);
	}
	return out;
}

Dictionary RtsoReference::get_civ_info(int civ) const {
	Dictionary d;
	if (!rtso::is_playable_civ(civ)) return d;
	const uint8_t c = (uint8_t)civ;
	const auto &ci = rtso::civ_info(c);
	d["name"] = ci.name;
	d["code"] = ci.code;
	d["emblem"] = ci.emblem_path;
	d["bonus_text"] = String::utf8(rtso::civ_bonus_text(c));
	Array team;
	const rtso::CivTeamBonus *tb = rtso::civ_team_bonus_table();
	String last;
	for (uint16_t i = 0; i < rtso::CIV_TEAM_BONUS_COUNT; ++i) {
		if (tb[i].civ != civ || !tb[i].name) continue;
		const String name = String::utf8(tb[i].name);
		if (name == last) continue; // one bonus can take several rows (one per stat)
		last = name;
		Dictionary b;
		b["name"] = name;
		b["description"] = String::utf8(tb[i].description ? tb[i].description : "");
		team.push_back(b);
	}
	d["team_bonuses"] = team;
	Array heroes;
	for (uint16_t t : rtso::civ_roster_ids(c)) {
		const auto &u = rtso::civ_unit_template(t);
		if (!(u.flags & rtso::ROSTER_HERO)) continue;
		Dictionary h = unit_entry(t);
		Array auras;
		for (int16_t a : u.auras) {
			if (a < 0) continue;
			const auto &aura = rtso::civ_hero_aura(a);
			Dictionary ad;
			ad["name"] = String::utf8(aura.name);
			ad["description"] = String::utf8(aura.description ? aura.description : "");
			auras.push_back(ad);
		}
		h["auras"] = auras;
		heroes.push_back(h);
	}
	d["heroes"] = heroes;
	// civinfo StructuresSubsection ("CivSpecific Structure") and TechnologiesSubsection.
	Array structures;
	// Every structure the civ builds; "unique" marks RTS0's civ rows, the page keeps the
	// ones 0.28 classes "CivSpecific Structure" (templates.json specific_structures).
	for (uint8_t b = 0; b < rtso::BLD_TYPE_COUNT; ++b) {
		if (!rtso::civ_can_build(c, b)) continue;
		Dictionary sd;
		sd["type"] = b;
		sd["unique"] = b >= rtso::CIV_BUILDING_FIRST;
		sd["name"] = String::utf8(rtso::building_type_info(b).name);
		sd["portrait"] = building_portrait(b);
		sd["phase"] = rtso::building_required_phase(b, c);
		structures.push_back(sd);
	}
	d["structures"] = structures;
	Array techs;
	for (uint8_t id = 1; id < rtso::TECH_COUNT; ++id) {
		if (!rtso::civ_structures_offer_tech(id, c)) continue;
		Dictionary td = tech_entry(id, c);
		td["civ_specific"] = tech_civ_specific(id); // when templates.json lacks the tech
		techs.push_back(td);
	}
	d["techs"] = techs;
	return d;
}

Array RtsoReference::get_structure_tree(int civ) const {
	Array out;
	if (civ < 0 || civ >= rtso::CIV_COUNT) return out;
	const uint8_t c = (uint8_t)civ;
	for (uint8_t b : builder_order(c)) { // only the civ's own structures
		const auto &bi = rtso::building_type_info(b);
		Array units, techs, upgrades;
		for (uint16_t t : rtso::civ_roster_ids(c)) {
			if (rtso::civ_unit_template(t).trainers & rtso::bld_bit(b)) units.push_back(unit_entry(t));
		}
		for (uint8_t id = 1; id < rtso::TECH_COUNT; ++id) {
			if (rtso::building_researches_tech(b, id, c) && rtso::tech_available_to_civ(id, c)) techs.push_back(tech_entry(id, c));
		}
		// ProductionRowManager: every Upgrade choice (the Persian tachara's four).
		for (int k = 0, n = rtso::building_upgrade_count(b, c); k < n; ++k) {
			const rtso::BuildingUpgradeInfo up = rtso::building_upgrade(b, c, k);
			Dictionary ud;
			ud["type"] = up.to;
			ud["name"] = String::utf8(rtso::building_type_info(up.to).name);
			ud["portrait"] = building_portrait(up.to);
			ud["phase"] = up.required_phase;
			ud["cost"] = cost_text(up.food_cost, up.wood_cost, up.stone_cost, up.gold_cost);
			ud["upgrade_cost"] = cost_dict(up.food_cost, up.wood_cost, up.stone_cost, up.gold_cost);
			ud["upgrade_time"] = up.time;
			ud["tooltip"] = String::utf8(up.tooltip ? up.tooltip : "");
			upgrades.push_back(ud);
		}
		Dictionary d;
		d["type"] = b;
		d["name"] = String::utf8(bi.name);
		d["portrait"] = building_portrait(b);
		d["phase"] = rtso::building_required_phase(b, c);
		d["cost"] = cost_text(bi.food_cost, bi.wood_cost, bi.stone_cost, bi.gold_cost);
		d["units"] = units;
		d["techs"] = techs;
		d["upgrades"] = upgrades;
		out.push_back(d);
	}
	return out;
}

// TrainerSection: the civ's units that train, research or upgrade (TemplateLister units with
// production or upgrades), each with what it trains, researches and upgrades to.
Array RtsoReference::get_trainer_units(int civ) const {
	Array out;
	if (civ < 0 || civ >= rtso::CIV_COUNT) return out;
	for (uint16_t t : rtso::civ_roster_ids((uint8_t)civ)) {
		const char *id = rtso::civ_unit_template(t).id;
		Array units, techs, upgrades;
		for (const auto &row : rtso::UNIT_TRAINER_ROWS) {
			if (std::strcmp(row.trainer, id) != 0) continue;
			const uint16_t te = rtso::civ_unit_template_by_id(row.trainee);
			if (te < rtso::CIV_UNIT_TEMPLATE_COUNT) units.push_back(unit_entry(te));
		}
		for (const auto &row : rtso::UNIT_RESEARCH_ROWS)
			if (std::strcmp(row.researcher, id) == 0) techs.push_back(tech_entry(row.tech));
		const rtso::CivUnitUpgrade *ups = rtso::civ_unit_upgrade_table();
		for (uint16_t k = 0; ups[k].from != 0xFFFF; ++k) {
			if (ups[k].from != t || ups[k].to >= rtso::CIV_UNIT_TEMPLATE_COUNT) continue;
			Dictionary ud = unit_entry(ups[k].to);
			ud["phase"] = ups[k].phase;
			ud["cost"] = cost_text(ups[k].food_cost, ups[k].wood_cost, ups[k].stone_cost, ups[k].gold_cost);
			ud["upgrade_cost"] = cost_dict(ups[k].food_cost, ups[k].wood_cost, ups[k].stone_cost, ups[k].gold_cost);
			ud["upgrade_time"] = ups[k].time;
			upgrades.push_back(ud);
		}
		if (units.is_empty() && techs.is_empty() && upgrades.is_empty()) continue;
		Dictionary d = unit_entry(t);
		d["units"] = units;
		d["techs"] = techs;
		d["upgrades"] = upgrades;
		out.push_back(d);
	}
	return out;
}

String RtsoReference::phase_icon(int phase) {
	static const char *const kIcons[] = {"village", "village", "town", "city"};
	return String("res://assets/textures/ui/phases/") + kIcons[phase < 0 ? 0 : phase > 3 ? 3 : phase] + ".png";
}

// gui/reference/catafalque Emblem.CatafalqueTemplateMethod: "units/<civ>/catafalque",
// one per civ in civ order, with the auras its tooltip lists.
Array RtsoReference::get_catafalques() const {
	Array out;
	for (uint16_t t = 0; t < rtso::CIV_UNIT_TEMPLATE_COUNT; ++t) {
		const char *id = rtso::civ_unit_template(t).id;
		const size_t n = id ? std::strlen(id) : 0;
		static const char kSuffix[] = "/catafalque";
		if (n < sizeof(kSuffix) - 1 || std::strcmp(id + n - (sizeof(kSuffix) - 1), kSuffix) != 0) continue;
		out.push_back(get_unit(t));
	}
	return out;
}

// A Gaia animal's Template Viewer page (gaia/fauna_*, the corral's trainable ones too): cost and
// time when a corral trains it, health, resistance, melee attack, speed in 0 A.D. metres, its
// food and its corral aura (structures/corral_garrison_*). templates.json "gaia" holds the text.
Dictionary RtsoReference::get_fauna(int fauna_type) const {
	Dictionary d;
	if (fauna_type < 0 || fauna_type >= rtso::FAUNA_COUNT) return d;
	const uint8_t f = (uint8_t)fauna_type;
	const rtso::FaunaTypeInfo &fi = rtso::fauna_type_info(f);
	d["kind"] = "fauna";
	d["id"] = f;
	d["name"] = String::utf8(fi.name);
	d["template"] = String("gaia/") + fi.template_name;
	const rtso::FaunaTrainCost &c = rtso::fauna_train_cost(f);
	if (c.res[0] + c.res[1] + c.res[2] + c.res[3] > 0) {
		d["cost"] = cost_dict(c.res[0], c.res[1], c.res[2], c.res[3]);
		d["time"] = c.time;
	}
	d["hp"] = fi.max_hp;
	d["armour"] = armour_dict(fi.hack_armor, fi.pierce_armor, fi.crush_armor);
	Dictionary vw;
	const float walk = fi.speed / kSpeedScale;
	Dictionary speed;
	speed["walk"] = walk;
	speed["run"] = walk * fi.run_mult;
	speed["acceleration"] = fi.acceleration / kSpeedScale;
	vw["speed"] = speed;
	if (fi.attack_damage() > 0.0f) {
		Dictionary dmg;
		if (fi.hack_damage > 0.0f) dmg["Hack"] = fi.hack_damage;
		if (fi.pierce_damage > 0.0f) dmg["Pierce"] = fi.pierce_damage;
		if (fi.crush_damage > 0.0f) dmg["Crush"] = fi.crush_damage;
		Dictionary a;
		a["type"] = "Melee";
		a["name"] = "Melee";
		a["min_range"] = 0;
		a["max_range"] = fi.attack_range / kMeleeRangeScale;
		a["repeat_time"] = fi.attack_cooldown;
		a["damage"] = dmg;
		Array attacks;
		attacks.push_back(a);
		vw["attacks"] = attacks;
	}
	if (fi.food_amount > 0) {
		Dictionary supply;
		supply["type"] = "food.meat";
		supply["amount"] = fi.food_amount;
		vw["supply"] = supply;
	}
	d["viewer"] = vw;
	Array auras;
	if (const float food = rtso::fauna_corral_food(f); food > 0.0f) {
		Dictionary ad;
		ad["name"] = "Ranching and Corralling";
		ad["description"] = "Garrison within the Corral to gain a slow trickle of +" + String::num_int64((int64_t)std::lround(food)) + " food.";
		ad["radius"] = 0.0f;
		auras.push_back(ad);
	}
	d["auras"] = auras;
	return d;
}

Dictionary RtsoReference::get_unit(int tmpl) const {
	Dictionary d;
	if (tmpl < 0 || tmpl >= rtso::CIV_UNIT_TEMPLATE_COUNT) return d;
	const uint16_t t = (uint16_t)tmpl;
	const auto &u = rtso::civ_unit_template(t);
	const auto &info = rtso::civ_template_unit_info(t, u.unit_type);
	d = unit_entry(t);
	d["kind"] = "unit";
	d["civ"] = u.civ;
	d["civ_name"] = rtso::civ_info(u.civ).name;
	d["cost"] = cost_dict(u.food_cost, u.wood_cost, u.stone_cost, u.gold_cost);
	d["cost_text"] = cost_text(u.food_cost, u.wood_cost, u.stone_cost, u.gold_cost);
	d["time"] = u.train_time;
	d["pop"] = u.pop_cost;
	d["hp"] = u.hp;
	d["armour"] = armour_dict(u.hack_armor, u.pierce_armor, u.crush_armor);
	d["walk_speed"] = u.speed / kSpeedScale;
	d["vision"] = rtso::unit_vision_range(u.unit_type) * 4.0f; // RTS0 units are 4 m tiles
	// The simulation's ResourceGatherer Capacities: the 0.28 gatherer class (a fishing boat carries 40).
	const int32_t class_cap = rtso::gather_class_capacity(rtso::unit_gather_class(u.unit_type, t));
	d["carry"] = class_cap ? class_cap : info.carry_capacity;
	const rtso::CombatProfile &cp = rtso::unit_combat(t, u.unit_type);
	if (u.attack_damage > 0.0f) {
		const bool ranged = (u.flags & rtso::ROSTER_RANGED) != 0;
		Dictionary a;
		a["type"] = ranged ? "Ranged" : "Melee";
		a["hack"] = u.hack_damage;
		a["pierce"] = u.pierce_damage;
		a["crush"] = u.crush_damage;
		a["interval"] = u.attack_cooldown;
		a["range"] = u.attack_range / (ranged ? rtso::COMBAT_DISTANCE_SCALE : kMeleeRangeScale);
		a["spread"] = cp.spread;
		a["bonuses"] = bonus_list(cp);
		a["restricted"] = class_list(cp.restricted);
		d["attack"] = a;
	}
	const float capture = rtso::unit_capture_attack(t, u.unit_type);
	if (capture > 0.0f) d["capture"] = capture;
	if (u.unit_type == rtso::UNIT_HEALER) {
		Dictionary h;
		h["hp"] = rtso::HEAL_HP;
		h["interval"] = rtso::HEAL_INTERVAL_S;
		h["range"] = 12.0f * 4.0f;
		d["heal"] = h;
	}
	// getAurasTooltip's token-merged Auras: template_unit_hero's hero_garrison and the
	// infantry templates' ram_garrison, then the unit's own (seldet::unit_auras_tooltip).
	Array auras;
	auto add_aura = [&auras](const char *name, const char *description, float radius) {
		Dictionary ad;
		ad["name"] = String::utf8(name);
		ad["description"] = String::utf8(description ? description : "");
		ad["radius"] = radius;
		auras.push_back(ad);
	};
	if (u.tech_classes & rtso::TC_HERO)
		add_aura("Inspired Defense", "When garrisoned, the Structure has +1 capture points garrison regeneration rate.", 0.0f);
	if (u.tech_classes & rtso::TC_INFANTRY)
		add_aura("Battering Team", "Battering Rams +0.15 speed for each garrisoned soldier.", 0.0f);
	for (int16_t a : u.auras) {
		if (a < 0) continue;
		const auto &aura = rtso::civ_hero_aura(a);
		add_aura(aura.name, aura.description, aura.radius);
	}
	d["auras"] = auras;
	const rtso::LootInfo loot = rtso::unit_loot(t, u.unit_type);
	d["loot"] = cost_dict(loot.food, loot.wood, loot.stone, loot.metal);
	d["loot_xp"] = loot.xp;
	// ViewerPage getUpgradeText: the unit's Upgrade entries (the Centurion, the Persian
	// champions, Chandragupta, fire ships).
	Array unit_upgrades;
	const rtso::CivUnitUpgrade *ups = rtso::civ_unit_upgrade_table();
	for (uint16_t k = 0; ups[k].from != 0xFFFF; ++k)
		if (ups[k].from == t && ups[k].to < rtso::CIV_UNIT_TEMPLATE_COUNT) unit_upgrades.push_back(unit_entry(ups[k].to));
	d["upgrades"] = unit_upgrades;
	Array trained_at;
	for (uint8_t b = 0; b < rtso::BLD_TYPE_COUNT; ++b) {
		if (!(u.trainers & rtso::bld_bit(b))) continue;
		Dictionary bd;
		bd["type"] = b;
		bd["name"] = String::utf8(rtso::building_type_info(b).name);
		trained_at.push_back(bd);
	}
	d["trained_at"] = trained_at;
	// Trainer/Entities and Researcher/Technologies on units (TemplateLoader.deriveProduction):
	// the units that train this one, and what this one trains and researches.
	Array trained_by_units, unit_trains, unit_researches;
	for (const auto &row : rtso::UNIT_TRAINER_ROWS) {
		if (std::strcmp(row.trainee, u.id) == 0) {
			const uint16_t tr = rtso::civ_unit_template_by_id(row.trainer);
			if (tr < rtso::CIV_UNIT_TEMPLATE_COUNT) trained_by_units.push_back(unit_entry(tr));
		}
		if (std::strcmp(row.trainer, u.id) == 0) {
			const uint16_t te = rtso::civ_unit_template_by_id(row.trainee);
			if (te < rtso::CIV_UNIT_TEMPLATE_COUNT) unit_trains.push_back(unit_entry(te));
		}
	}
	for (const auto &row : rtso::UNIT_RESEARCH_ROWS)
		if (std::strcmp(row.researcher, u.id) == 0) unit_researches.push_back(tech_entry(row.tech));
	d["trained_by_units"] = trained_by_units;
	d["trains"] = unit_trains;
	d["researches"] = unit_researches;
	if (u.unlock_tech != rtso::TECH_NONE) {
		Dictionary td;
		td["id"] = u.unlock_tech;
		td["name"] = String::utf8(rtso::tech_info(u.unlock_tech).name);
		d["requires"] = td;
	}
	Array roles;
	if (u.flags & rtso::ROSTER_HERO) roles.push_back("Hero");
	if (u.flags & rtso::ROSTER_CHAMPION) roles.push_back("Champion");
	if (u.flags & rtso::ROSTER_CITIZEN) roles.push_back("Citizen");
	if (u.flags & rtso::ROSTER_MERCENARY) roles.push_back("Mercenary");
	d["roles"] = roles;
	d["classes"] = class_list(cp.classes);
	return d;
}

Dictionary RtsoReference::get_structure(int type, int civ) const {
	Dictionary d;
	if (type < 0 || type >= rtso::BLD_TYPE_COUNT) return d;
	const uint8_t b = (uint8_t)type;
	const uint8_t c = (civ >= 0 && civ < rtso::CIV_COUNT) ? (uint8_t)civ : 0;
	const rtso::BuildingTypeInfo bi = rtso::civ_building_type_info(c, b); // the big house
	d["kind"] = "structure";
	d["type"] = b;
	d["civ"] = c;
	d["civ_name"] = rtso::civ_info(c).name;
	d["name"] = String::utf8(bi.name);
	d["specific_name"] = "";
	d["portrait"] = building_portrait(b);
	d["phase"] = rtso::building_required_phase(b, c);
	d["cost"] = cost_dict(bi.food_cost, bi.wood_cost, bi.stone_cost, bi.gold_cost);
	d["cost_text"] = cost_text(bi.food_cost, bi.wood_cost, bi.stone_cost, bi.gold_cost);
	d["time"] = bi.build_time;
	d["hp"] = bi.max_hp;
	// The WallSet a wall, palisade or siege wall box stands for (TemplateParser.getEntity and
	// getEntityCostTooltip): its pieces' Health range and the short, medium and long segments'
	// and the tower's costs.
	if (const rtso::WallSetInfo ws = rtso::wallset_info(rtso::wallset_kind_of(b), c); ws.segment == b) {
		const bool stone = rtso::wallset_kind_of(b) == rtso::WALLSET_STONE;
		auto piece = [stone](float cost, float time) {
			Dictionary p;
			p["cost"] = stone ? cost_dict(0, 0, (int)cost, 0) : cost_dict(0, (int)cost, 0, 0);
			p["time"] = time;
			return p;
		};
		Dictionary w;
		Array walls;
		for (int k = 2; k >= 0; --k) walls.push_back(piece(ws.cost[k], ws.build_time[k]));
		w["walls"] = walls;
		const rtso::BuildingTypeInfo ti = rtso::civ_building_type_info(c, ws.tower);
		Dictionary tower;
		tower["cost"] = ws.tower_cost < 0.0f ? cost_dict(ti.food_cost, ti.wood_cost, ti.stone_cost, ti.gold_cost)
				: Dictionary(piece(ws.tower_cost, 0.0f)["cost"]);
		tower["time"] = ws.tower_build_time < 0.0f ? ti.build_time : ws.tower_build_time;
		w["tower"] = tower;
		w["hp_min"] = std::min({ws.health[0], ws.health[1], ws.health[2]});
		w["hp_max"] = std::max({ws.health[0], ws.health[1], ws.health[2]});
		d["wallset"] = w;
	}
	float armour[3];
	rtso::structure_armour(b, c, armour); // the Germans' totem
	d["armour"] = armour_dict(armour[0], armour[1], armour[2]);
	d["pop_bonus"] = bi.pop_bonus;
	d["garrison"] = rtso::structure_garrison_max(b, c); // the civ's own GarrisonHolder/Max
	d["garrison_heal"] = rtso::building_garrison_heal(b);
	d["capture_points"] = bi.capture_max_points;
	d["capture_regen"] = bi.capture_regen_rate;
	d["territory_decay"] = bi.territory_decay_rate;
	// BuildingAI.js: DefaultArrowCount arrows per RepeatTime, plus garrison arrows up to MaxArrowCount.
	if (const rtso::BuildingAIInfo &ai = rtso::building_ai_info(b); ai.shoots()) {
		Dictionary a;
		a["type"] = "Ranged";
		a["hack"] = ai.hack;
		a["pierce"] = ai.pierce;
		a["crush"] = ai.crush;
		a["interval"] = ai.repeat_time;
		a["range"] = ai.max_range / rtso::COMBAT_DISTANCE_SCALE;
		a["arrows"] = ai.default_arrows;
		a["max_arrows"] = ai.max_arrows;
		a["per_unit"] = ai.garrison_arrow_multiplier;
		a["bonuses"] = Array();
		a["restricted"] = "";
		d["attack"] = a;
	}
	const rtso::LootInfo loot = rtso::building_loot(b);
	d["loot"] = cost_dict(loot.food, loot.wood, loot.stone, loot.metal);
	d["loot_xp"] = loot.xp;
	Array trains, researches, upgrades;
	for (uint16_t t : rtso::civ_roster_ids(c))
		if (rtso::civ_unit_template(t).trainers & rtso::bld_bit(b)) trains.push_back(unit_entry(t));
	for (uint8_t id = 1; id < rtso::TECH_COUNT; ++id)
		if (rtso::building_researches_tech(b, id, c) && rtso::tech_available_to_civ(id, c)) researches.push_back(tech_entry(id, c));
	for (int k = 0, n = rtso::building_upgrade_count(b, c); k < n; ++k) {
		const rtso::BuildingUpgradeInfo up = rtso::building_upgrade(b, c, k);
		Dictionary ud;
		ud["type"] = up.to;
		ud["name"] = String::utf8(rtso::building_type_info(up.to).name);
		ud["cost_text"] = cost_text(up.food_cost, up.wood_cost, up.stone_cost, up.gold_cost);
		ud["time"] = up.time;
		ud["tooltip"] = String::utf8(up.tooltip ? up.tooltip : "");
		ud["upgrade_cost"] = cost_dict(up.food_cost, up.wood_cost, up.stone_cost, up.gold_cost);
		ud["upgrade_time"] = up.time;
		upgrades.push_back(ud);
	}
	d["trains"] = trains;
	d["researches"] = researches;
	d["upgrades"] = upgrades;
	d["available"] = rtso::civ_can_build(c, b);
	return d;
}

Dictionary RtsoReference::get_tech(int id, int civ) const {
	Dictionary d;
	if (id <= 0 || id >= rtso::TECH_COUNT) return d;
	const uint8_t tid = (uint8_t)id;
	const auto &ti = rtso::tech_info(tid);
	d = tech_entry(tid, civ >= 0 && civ < rtso::CIV_COUNT ? (uint8_t)civ : 0xFF);
	d["kind"] = "tech";
	d["cost"] = cost_dict(ti.food_cost, ti.wood_cost, ti.stone_cost, ti.gold_cost);
	d["cost_text"] = cost_text(ti.food_cost, ti.wood_cost, ti.stone_cost, ti.gold_cost);
	d["time"] = ti.research_time;
	Dictionary r;
	r["type"] = ti.researcher;
	r["name"] = String::utf8(rtso::building_type_info(ti.researcher).name);
	d["researched_at"] = r;
	// getResearchedByText: every structure of the civ and every unit that researches it.
	Array researchers;
	const uint8_t rc = (civ >= 0 && civ < rtso::CIV_COUNT) ? (uint8_t)civ : 0;
	for (uint8_t b = 0; b < rtso::BLD_TYPE_COUNT; ++b) {
		if (!rtso::civ_can_build(rc, b) || !rtso::building_researches_tech(b, tid, rc)) continue;
		Dictionary bd;
		bd["kind"] = "structure";
		bd["type"] = b;
		bd["name"] = String::utf8(rtso::building_type_info(b).name);
		researchers.push_back(bd);
	}
	for (const auto &row : rtso::UNIT_RESEARCH_ROWS) {
		if (row.tech != tid) continue;
		const uint16_t t = rtso::civ_unit_template_by_id(row.researcher);
		if (t >= rtso::CIV_UNIT_TEMPLATE_COUNT || rtso::civ_unit_template(t).civ != rc) continue;
		Dictionary ud = unit_entry(t);
		ud["kind"] = "unit";
		researchers.push_back(ud);
	}
	d["researchers"] = researchers;
	if (ti.prerequisite != rtso::TECH_NONE) {
		Dictionary p;
		p["id"] = ti.prerequisite;
		p["name"] = String::utf8(rtso::tech_info(ti.prerequisite).name);
		d["requires"] = p;
	}
	Array civs;
	for (uint8_t c = 0; c < rtso::CIV_COUNT; ++c)
		if (rtso::is_playable_civ(c) && rtso::civ_structures_offer_tech(tid, c)) civs.push_back(String(rtso::civ_info(c).name));
	d["civs"] = civs;
	d["civ"] = civ;
	// Technologies this one unlocks (their prerequisite) and the units it unlocks.
	Array unlocks;
	for (uint8_t o = 1; o < rtso::TECH_COUNT; ++o)
		if (rtso::tech_info(o).prerequisite == tid && (civ < 0 || rtso::tech_available_to_civ(o, (uint8_t)civ))) unlocks.push_back(tech_entry(o, civ >= 0 && civ < rtso::CIV_COUNT ? (uint8_t)civ : 0xFF));
	d["unlocks"] = unlocks;
	Array units;
	if (civ >= 0 && civ < rtso::CIV_COUNT)
		for (uint16_t t : rtso::civ_roster_ids((uint8_t)civ))
			if (rtso::civ_unit_template(t).unlock_tech == tid) units.push_back(unit_entry(t));
	d["unlocks_units"] = units;
	return d;
}
