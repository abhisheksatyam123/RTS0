#include "reference_data.h"

#include "../simulation/sim_types.h"

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

// 0 A.D. structree: a structure's own phase is the earliest phase of anything
// RTS0 lets it build; the building table has no phase column.
int building_phase(uint8_t type) {
	switch (type) {
		case rtso::BLD_MARKET: case rtso::BLD_TEMPLE: case rtso::BLD_TOWER: case rtso::BLD_FORGE:
		case rtso::BLD_STABLE: case rtso::BLD_RANGE: case rtso::BLD_GATE: case rtso::BLD_WALL_SEGMENT:
		case rtso::BLD_WALL_TOWER: case rtso::BLD_DOCK: case rtso::BLD_THEATER:
			return 2;
		case rtso::BLD_FORTRESS: case rtso::BLD_WONDER: case rtso::BLD_ARSENAL: case rtso::BLD_ELEPHANT_STABLE:
		case rtso::BLD_EMBASSY:
			return 3;
		default:
			return 1;
	}
}

} // namespace

void RtsoReference::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_civs"), &RtsoReference::get_civs);
	ClassDB::bind_method(D_METHOD("get_civ_info", "civ"), &RtsoReference::get_civ_info);
	ClassDB::bind_method(D_METHOD("get_structure_tree", "civ"), &RtsoReference::get_structure_tree);
}

Array RtsoReference::get_civs() const {
	Array out;
	for (uint8_t c = 0; c < rtso::CIV_COUNT; ++c) {
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
	if (civ < 0 || civ >= rtso::CIV_COUNT) return d;
	const auto &ci = rtso::civ_info((uint8_t)civ);
	d["name"] = ci.name;
	d["code"] = ci.code;
	d["emblem"] = ci.emblem_path;
	d["bonus_text"] = String::utf8(rtso::civ_bonus_text((uint8_t)civ));
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
	for (uint16_t t = rtso::civ_roster_begin((uint8_t)civ); t < rtso::civ_roster_end((uint8_t)civ); ++t) {
		const auto &u = rtso::civ_unit_template(t);
		if (!(u.flags & rtso::ROSTER_HERO)) continue;
		Dictionary h;
		h["name"] = String::utf8(u.name);
		h["specific_name"] = String::utf8(u.specific_name ? u.specific_name : "");
		h["phase"] = u.phase;
		h["cost"] = cost_text(u.food_cost, u.wood_cost, u.stone_cost, u.gold_cost);
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
	return d;
}

Array RtsoReference::get_structure_tree(int civ) const {
	Array out;
	if (civ < 0 || civ >= rtso::CIV_COUNT) return out;
	for (uint8_t b = 0; b < rtso::BLD_TYPE_COUNT; ++b) {
		const auto &bi = rtso::building_type_info(b);
		Array units, techs;
		for (uint16_t t = rtso::civ_roster_begin((uint8_t)civ); t < rtso::civ_roster_end((uint8_t)civ); ++t) {
			const auto &u = rtso::civ_unit_template(t);
			if (!(u.trainers & rtso::bld_bit(b))) continue;
			Dictionary ud;
			ud["name"] = String::utf8(u.name);
			ud["specific_name"] = String::utf8(u.specific_name ? u.specific_name : "");
			ud["phase"] = u.phase;
			ud["cost"] = cost_text(u.food_cost, u.wood_cost, u.stone_cost, u.gold_cost);
			ud["flags"] = u.flags;
			units.push_back(ud);
		}
		for (uint8_t id = 1; id < rtso::TECH_COUNT; ++id) {
			const auto &ti = rtso::tech_info(id);
			if (ti.researcher != b || !(ti.civ_mask & rtso::civ_bit((uint8_t)civ))) continue;
			Dictionary td;
			td["name"] = String::utf8(ti.name);
			td["description"] = String::utf8(ti.desc ? ti.desc : "");
			td["phase"] = ti.required_phase;
			td["cost"] = cost_text(ti.food_cost, ti.wood_cost, ti.stone_cost, ti.gold_cost);
			techs.push_back(td);
		}
		Dictionary d;
		d["type"] = b;
		d["name"] = bi.name;
		d["portrait"] = String("res://assets/textures/ui/portraits/") + bi.portrait_id + ".png";
		d["phase"] = building_phase(b);
		d["cost"] = cost_text(bi.food_cost, bi.wood_cost, bi.stone_cost, bi.gold_cost);
		d["units"] = units;
		d["techs"] = techs;
		out.push_back(d);
	}
	return out;
}
