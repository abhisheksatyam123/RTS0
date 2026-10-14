#ifndef RTSO_SELECTION_DETAILS_H
#define RTSO_SELECTION_DETAILS_H

// 0 A.D. 0.28 gui/session/selection_details.js and the gui/common/tooltips.js pieces it
// joins (getAttackTooltip, getHealerTooltip, getResistanceTooltip, getGatherTooltip,
// getSpeedTooltip, getGarrisonTooltip, getLootTooltip, getAurasTooltip...), each string
// built from 0.28's msgid through tr0/sprintf0 so a language can reorder it. Resource
// names stand in for 0 A.D.'s inline resource icons; Godot tooltips have no font tags.

#include <godot_cpp/variant/string.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

#include "../simulation/simulation.h"
#include "l10n.h"

namespace rtso::seldet {

using godot::String;

inline String fixed(double v, int digits) { return String::num(v, digits).pad_decimals(digits); }
inline String num(int64_t v) { return String::num_int64(v); }
inline String comma() { return tr0(", "); }

// resourceNameFirstWord: Food, Wood, Stone, Metal (RTS0's RES_GOLD is metal).
inline String resource_name(uint8_t res) {
	switch (res) {
		case RES_FOOD: return tr0("Food");
		case RES_WOOD: return tr0("Wood");
		case RES_STONE: return tr0("Stone");
		case RES_GOLD: return tr0("Metal");
		default: return String();
	}
}

// "%(label)s %(current)s / %(max)s" (getCurrentHealthTooltip, getCurrentCaptureTooltip).
inline String current_of(const char *label_key, const char *label, float current, float max) {
	return sprintf0(tr0(String("%(") + label_key + ")s %(current)s / %(max)s"),
			{{label_key, tr0(label)}, {"current", num((int64_t)std::lround(current))}, {"max", num((int64_t)std::lround(max))}});
}
inline String health(float hp, float max) { return current_of("healthLabel", "Health:", hp, max); }
inline String capture(float points, float max) { return current_of("captureLabel", "Capture points:", points, max); }

// The experience tooltip: "Experience: 40 / 150", or the points alone at the last rank.
inline String experience(int64_t current, float required) {
	if (required > 0.0f && (float)current < required)
		return sprintf0(tr0("%(experience)s %(current)s / %(required)s"),
				{{"experience", tr0("Experience:")}, {"current", num(current)}, {"required", num((int64_t)std::ceil(required))}});
	return sprintf0(tr0("%(experience)s %(current)s"), {{"experience", tr0("Experience:")}, {"current", num(current)}});
}

// "%(amount)s / %(max)s": resource supply, carried resources, gatherers.
inline String amount_of(int64_t amount, int64_t max) {
	return sprintf0(tr0("%(amount)s / %(max)s"), {{"amount", num(amount)}, {"max", num(max)}});
}

// resourceSection: "Wood:" and "150 / 200", "∞" for an infinite supply (fields), or the amount
// alone when RTS0 keeps no Max for the supply.
inline String resource_label(uint8_t res) { return sprintf0(tr0("%(resource)s:"), {{"resource", resource_name(res)}}); }
inline String resource_amount(int64_t amount, int64_t max, bool infinite) {
	if (infinite) return tr0("∞");
	return max > 0 ? amount_of(amount, max) : num(amount);
}

// resistanceLevelToPercentageString.
inline String resistance_percent(float level) {
	return sprintf0(tr0("%(percentage)s%%"), {{"percentage", num(100 - (int64_t)std::lround(std::pow(0.9, level) * 100.0))}})
			.replace("%%", "%");
}

inline String damage_type_name(int k) {
	static const char *const kNames[] = {"Hack", "Pierce", "Crush", "Fire"};
	return tr0(kNames[k]);
}

// getResistanceTooltip: "Resistance:\n  Damage: 5.0 Hack (41%), 3.0 Pierce (27%), 1.0 Crush (10%)".
inline String resistance_tooltip(const float r[3]) {
	String parts;
	for (int k = 0; k < 3; ++k) {
		if (!parts.is_empty()) parts += comma();
		parts += sprintf0(tr0("%(damage)s %(damageType)s %(resistancePercentage)s"),
				{{"damage", fixed(r[k], 1)}, {"damageType", damage_type_name(k)},
				 {"resistancePercentage", sprintf0(tr0("(%(resistancePercentage)s)"), {{"resistancePercentage", resistance_percent(r[k])}})}});
	}
	const String damage = sprintf0(tr0("%(label)s %(details)s"), {{"label", tr0("Damage:")}, {"details", parts}});
	return sprintf0(tr0("%(label)s\n%(details)s"), {{"label", tr0("Resistance:")}, {"details", "  " + damage}});
}

// rangeDetails: "Range: 72 meters", "Range: 26 to 85 meters".
inline String range_details(float min_range, float max_range) {
	const int64_t lo = (int64_t)std::lround(min_range), hi = (int64_t)std::lround(max_range);
	if (hi <= 0) return String();
	const String unit = tr0(lo ? "meters" : (hi == 1 ? "meter" : "meters"));
	if (lo)
		return sprintf0(tr0("%(rangeLabel)s %(minRange)s to %(maxRange)s %(rangeUnit)s"),
				{{"rangeLabel", tr0("Range:")}, {"minRange", num(lo)}, {"maxRange", num(hi)}, {"rangeUnit", unit}});
	return sprintf0(tr0("%(rangeLabel)s %(maxRange)s %(rangeUnit)s"),
			{{"rangeLabel", tr0("Range:")}, {"maxRange", num(hi)}, {"rangeUnit", unit}});
}

// attackRateDetails without projectiles: "Interval: 1.25 seconds".
inline String interval_details(float seconds) {
	const String secs = sprintf0(tr0("%(time)s %(second)s"),
			{{"time", String::num(seconds, 2)}, {"second", tr0(seconds == 1.0f ? "second" : "seconds")}});
	return sprintf0(tr0("%(label)s %(details)s"), {{"label", tr0("Interval:")}, {"details", secs}});
}

// getAttackTooltip for a unit: one line for its Melee or Ranged attack (attackEffectsDetails:
// Capture first, then the damage types) and one for its Capture attack.
inline String attack_tooltip(const Simulation::UnitDetails &d) {
	std::vector<String> lines;
	auto attack_line = [&](const char *name, const String &effects, float lo, float hi) {
		String range = range_details(lo, hi);
		lines.push_back(sprintf0(tr0("%(attackLabel)s: %(effects)s, %(range)s, %(rate)s%(statusEffects)s%(splash)s"),
				{{"attackLabel", tr0(name)}, {"effects", effects}, {"range", range}, {"rate", interval_details(d.interval)},
				 {"statusEffects", ""}, {"splash", ""}}));
	};
	String damage;
	for (int k = 0; k < 4; ++k) {
		if (d.damage[k] <= 0.0f) continue;
		if (!damage.is_empty()) damage += comma();
		damage += sprintf0(tr0("%(damage)s %(damageType)s"), {{"damage", fixed(d.damage[k], 1)}, {"damageType", damage_type_name(k)}});
	}
	if (!damage.is_empty()) attack_line(d.ranged ? "Ranged" : "Melee", damage, d.min_range, d.max_range);
	if (d.capture > 0.0f)
		attack_line("Capture", sprintf0(tr0("%(amount)s %(name)s"), {{"amount", fixed(d.capture, 1)}, {"name", tr0("Capture")}}),
				0.0f, 4.0f);
	if (lines.empty()) return String();
	String details;
	for (const String &l : lines) details += String(details.is_empty() ? "" : "\n") + "  " + l;
	return sprintf0(tr0("%(label)s\n%(details)s"), {{"label", tr0("Attack:")}, {"details", details}});
}

// getHealerTooltip: "Heal: 5 Health, Range: 12 meters, Interval: 2 seconds".
inline String healer_tooltip(const Simulation::UnitDetails &d) {
	if (d.heal_hp <= 0.0f) return String();
	auto part = [](const char *label, const String &val, const char *unit) {
		return sprintf0(tr0("%(label)s %(val)s %(unit)s"), {{"label", tr0(label)}, {"val", val}, {"unit", tr0(unit)}});
	};
	const double range = std::round(d.heal_range);
	return part("Heal:", String::num(d.heal_hp, 1), "Health") + comma() +
			part("Range:", String::num(range, 0), range == 1.0 ? "meter" : "meters") + comma() +
			part("Interval:", String::num(d.heal_interval, 1), d.heal_interval == 1.0f ? "second" : "seconds");
}

// getGatherTooltip: "Gather Rates: Food (grain) 0.25  Wood 0.75 ...". Ruins are left out, as in 0.28.
inline String gather_tooltip(const Simulation::UnitDetails &d) {
	static const char *const kKinds[] = {"Grain", "Fruit", "Meat", "Fish", "Wood", "Stone", "Metal"};
	String rates;
	for (int k = 0; k < 7; ++k) {
		if (d.gather[k] <= 0.0f) continue;
		if (!rates.is_empty()) rates += "  ";
		rates += sprintf0(tr0("%(resourceIcon)s %(rate)s"), {{"resourceIcon", tr0(kKinds[k])}, {"rate", fixed(d.gather[k], 2)}});
	}
	return rates.is_empty() ? rates : sprintf0(tr0("%(label)s %(details)s"), {{"label", tr0("Gather Rates:")}, {"details", rates}});
}

// getSpeedTooltip: "Speed: 9.0 Walk, 15.0 Run" (RTS0 has no Acceleration).
inline String speed_tooltip(const Simulation::UnitDetails &d) {
	if (d.walk <= 0.0f && d.run <= 0.0f) return String();
	auto speed = [](float v, const char *kind) {
		return sprintf0(tr0("%(speed)s %(movementType)s"), {{"speed", fixed(v, 1)}, {"movementType", tr0(kind)}});
	};
	return sprintf0(tr0("%(label)s %(speeds)s"), {{"label", tr0("Speed:")}, {"speeds", speed(d.walk, "Walk") + comma() + speed(d.run, "Run")}});
}

// getGarrisonTooltip for a holder: "Garrison Limit: 20".
inline String garrison_limit_tooltip(int capacity) {
	if (capacity <= 0) return String();
	return sprintf0(tr0("%(label)s: %(garrisonLimit)s"), {{"label", tr0("Garrison Limit")}, {"garrisonLimit", num(capacity)}});
}

// Loot by resource (food, wood, stone, metal) and xp: "Food 2  Metal 5  Experience 10".
struct Amounts {
	int64_t res[RES_COUNT] = {};
	int64_t xp = 0;
	bool any() const {
		for (int64_t v : res)
			if (v) return true;
		return xp != 0;
	}
};
inline String amounts_list(const Amounts &a, bool with_xp) {
	static const uint8_t kOrder[] = {RES_FOOD, RES_WOOD, RES_STONE, RES_GOLD};
	String out;
	auto add = [&](const String &name, int64_t v) {
		if (!v) return;
		if (!out.is_empty()) out += "  ";
		out += sprintf0(tr0("%(component)s %(loot)s"), {{"component", name}, {"loot", num(v)}});
	};
	for (uint8_t r : kOrder) add(resource_name(r), a.res[r]);
	if (with_xp) add(tr0("Experience"), a.xp);
	return out;
}

// getLootTooltip: the template's Loot plus what the unit carries.
inline String loot_tooltip(const Amounts &a) {
	if (!a.any()) return String();
	return sprintf0(tr0("%(label)s %(details)s"), {{"label", tr0("Loot:")}, {"details", amounts_list(a, true)}});
}

// getAurasTooltip: "Name: description Range: 60 meters" per aura.
inline String aura_line(const String &name, const String &description, float radius) {
	String tip = sprintf0(tr0("%(auralabel)s %(aurainfo)s"),
			{{"auralabel", sprintf0(tr0("%(auraname)s:"), {{"auraname", name}})}, {"aurainfo", tr0(description)}});
	const int64_t r = (int64_t)std::lround(radius);
	if (r > 0)
		tip += " " + sprintf0(tr0("%(label)s %(val)s %(unit)s"), {{"label", tr0("Range:")}, {"val", num(r)}, {"unit", tr0(r == 1 ? "meter" : "meters")}});
	return tip;
}

// displayMultiple's numberOfUnits tooltip: garrison size, then what the selection carries
// and its loot.
inline String multiple_tooltip(int64_t garrison_size, const Amounts &carrying, const Amounts &loot) {
	String tip;
	if (garrison_size)
		tip = sprintf0(tr0("%(label)s: %(details)s\n"), {{"label", tr0("Garrison Size")}, {"details", num(garrison_size)}});
	if (carrying.any())
		tip += sprintf0(tr0("%(label)s %(details)s\n"), {{"label", tr0("Carrying:")}, {"details", amounts_list(carrying, false)}});
	if (loot.any())
		tip += sprintf0(tr0("%(label)s %(details)s"), {{"label", tr0("Loot:")}, {"details", amounts_list(loot, true)}});
	return tip.strip_edges();
}

// Joins the non-empty tooltips with newlines.
inline String join_lines(std::initializer_list<String> parts) {
	String out;
	for (const String &p : parts) {
		if (p.is_empty()) continue;
		if (!out.is_empty()) out += "\n";
		out += p;
	}
	return out;
}

} // namespace rtso::seldet

#endif
