#ifndef RTSO_SELECTION_DETAILS_H
#define RTSO_SELECTION_DETAILS_H

// 0 A.D. 0.28 gui/session/selection_details.js and the gui/common/tooltips.js pieces it
// joins (getAttackTooltip, getHealerTooltip, getResistanceTooltip, getGatherTooltip,
// getSpeedTooltip, getGarrisonTooltip, getLootTooltip, getAurasTooltip...), each string
// built from 0.28's msgid through tr0/sprintf0 so a language can reorder it. Resource
// names stand in for 0 A.D.'s inline resource icons; Godot tooltips have no font tags.

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
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

// getCurrentHealthTooltip, getCurrentCaptureTooltip: each its own whole msgid.
inline String health(float hp, float max) {
	return sprintf0(tr0("%(healthLabel)s %(current)s / %(max)s"),
			{{"healthLabel", tr0("Health:")}, {"current", num((int64_t)std::lround(hp))}, {"max", num((int64_t)std::lround(max))}});
}
// label: displayMultiple passes "Capture Points:"; getCurrentCaptureTooltip's own is "Capture points:".
inline String capture(float points, float max, const char *label = "Capture points:") {
	return sprintf0(tr0("%(captureLabel)s %(current)s / %(max)s"),
			{{"captureLabel", tr0(label)}, {"current", num((int64_t)std::lround(points))}, {"max", num((int64_t)std::lround(max))}});
}

// The single-selection bar captions (healthStats, captureStats): "%(hitpoints)s / %(maxHitpoints)s"
// rounded up, no label (selection_details.js displaySingle); tooltips keep health() / capture().
inline String health_stats(float hp, float max) {
	return sprintf0(tr0("%(hitpoints)s / %(maxHitpoints)s"),
			{{"hitpoints", num((int64_t)std::ceil(hp))}, {"maxHitpoints", num((int64_t)std::ceil(max))}});
}
inline String capture_stats(float points, float max) {
	return sprintf0(tr0("%(capturePoints)s / %(maxCapturePoints)s"),
			{{"capturePoints", num((int64_t)std::ceil(points))}, {"maxCapturePoints", num((int64_t)std::ceil(max))}});
}

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
	if (infinite) return tr0(String::utf8("∞"));
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

// getResistanceTooltip: "Resistance:\n  Damage: 5.0 Hack (41%), 3.0 Pierce (27%), 1.0 Crush (10%)",
// and getStatusEffectsResistanceTooltip's "Status Effects: Blocks Poisoned" for the structures,
// siege engines and ships whose Resistance/Entity/ApplyStatus blocks Poisoned (BlockChance 1).
// No 0.28 template has a Capture resistance, so its line never shows.
inline String resistance_tooltip(const float r[3], bool blocks_poison = false) {
	String parts;
	for (int k = 0; k < 3; ++k) {
		if (!parts.is_empty()) parts += comma();
		parts += sprintf0(tr0("%(damage)s %(damageType)s %(resistancePercentage)s"),
				{{"damage", fixed(r[k], 1)}, {"damageType", damage_type_name(k)},
				 {"resistancePercentage", sprintf0(tr0("(%(resistancePercentage)s)"), {{"resistancePercentage", resistance_percent(r[k])}})}});
	}
	const String damage = sprintf0(tr0("%(label)s %(details)s"), {{"label", tr0("Damage:")}, {"details", parts}});
	String details = "  " + damage;
	if (blocks_poison)
		details += "\n  " + sprintf0(tr0("%(label)s %(details)s"), {{"label", tr0("Status Effects:")},
				{"details", sprintf0(tr0("Blocks %(name)s"), {{"name", tr0("Poisoned")}})}});
	return sprintf0(tr0("%(label)s\n%(details)s"), {{"label", tr0("Resistance:")}, {"details", details}});
}

// rangeDetails: "Range: 72 meters", "Range: 26 to 85 meters", and with the range the ground
// there gives (elevationAdaptedRange, 0 = none) "Range: 72 (+6) meters".
inline String range_details(float min_range, float max_range, float elevated_range = 0.0f) {
	const int64_t lo = (int64_t)std::lround(min_range), hi = (int64_t)std::lround(max_range);
	if (hi <= 0) return String();
	const int64_t rel = elevated_range > 0.0f ? (int64_t)std::lround(elevated_range - (float)hi) : 0;
	const String unit = tr0(lo || rel ? "meters" : (hi == 1 ? "meter" : "meters"));
	const String relative = rel > 0 ? sprintf0(tr0("+%(number)s"), {{"number", num(rel)}}) : num(rel);
	if (rel)
		return lo ? sprintf0(tr0("%(rangeLabel)s %(minRange)s to %(maxRange)s (%(relativeRange)s) %(rangeUnit)s"),
						{{"rangeLabel", tr0("Range:")}, {"minRange", num(lo)}, {"maxRange", num(hi)}, {"relativeRange", relative}, {"rangeUnit", unit}})
				  : sprintf0(tr0("%(rangeLabel)s %(maxRange)s (%(relativeRange)s) %(rangeUnit)s"),
						{{"rangeLabel", tr0("Range:")}, {"maxRange", num(hi)}, {"relativeRange", relative}, {"rangeUnit", unit}});
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

// getSpeedTooltip: "Speed: 9.0 Walk, 15.0 Run, 35.0 Acceleration".
inline String speed_tooltip(const Simulation::UnitDetails &d) {
	if (d.walk <= 0.0f && d.run <= 0.0f) return String();
	auto speed = [](float v, const char *kind) {
		return sprintf0(tr0("%(speed)s %(movementType)s"), {{"speed", fixed(v, 1)}, {"movementType", tr0(kind)}});
	};
	// An animal has no acceleration here: walk and run only.
	return sprintf0(tr0("%(label)s %(speeds)s"), {{"label", tr0("Speed:")}, {"speeds", speed(d.walk, "Walk") + comma() + speed(d.run, "Run") +
			(d.acceleration > 0.0f ? comma() + speed(d.acceleration, "Acceleration") : String())}});
}

// getGarrisonTooltip for a holder: "Garrison Limit: 20".
// getAttackTooltip for a structure's BuildingAI: "Bow: 12.0 Pierce, Range: 60 meters, Rate: 3
// arrows / 2 seconds", or "Garrison to fire arrows" while it has none (attackRateDetails).
inline String structure_attack_tooltip(const float damage[3], float min_m, float max_m, float repeat_s, int projectiles,
		float elevated_m = 0.0f) {
	String effects;
	for (int k = 0; k < 3; ++k) {
		if (damage[k] <= 0.0f) continue;
		if (!effects.is_empty()) effects += comma();
		effects += sprintf0(tr0("%(damage)s %(damageType)s"), {{"damage", fixed(damage[k], 1)}, {"damageType", damage_type_name(k)}});
	}
	String rate;
	if (projectiles == 0) {
		rate = tr0("Garrison to fire arrows");
	} else {
		const String secs = sprintf0(tr0("%(time)s %(second)s"),
				{{"time", String::num(repeat_s, 2)}, {"second", tr0(repeat_s == 1.0f ? "second" : "seconds")}});
		if (projectiles > 1)
			rate = sprintf0(tr0("%(label)s %(details)s"), {{"label", tr0("Rate:")},
					{"details", sprintf0(tr0("%(projectileString)s / %(attackRateString)s"),
							{{"projectileString", sprintf0(tr0("%(projectileCount)s %(projectileName)s"),
									{{"projectileCount", num(projectiles)}, {"projectileName", tr0(projectiles == 1 ? "arrow" : "arrows")}})},
							 {"attackRateString", secs}})}});
		else
			rate = sprintf0(tr0("%(label)s %(details)s"), {{"label", tr0("Interval:")}, {"details", secs}});
	}
	const String line = sprintf0(tr0("%(attackLabel)s: %(effects)s, %(range)s, %(rate)s%(statusEffects)s%(splash)s"),
			{{"attackLabel", tr0("Bow")}, {"effects", effects}, {"range", range_details(min_m, max_m, elevated_m)}, {"rate", rate},
			 {"statusEffects", ""}, {"splash", ""}});
	return sprintf0(tr0("%(label)s\n%(details)s"), {{"label", tr0("Attack:")}, {"details", "  " + line}});
}

// getGarrisonTooltip's heal line: "Heal: 1 Health / second".
inline String garrison_heal_tooltip(float heal) {
	if (heal <= 0.0f) return String();
	return sprintf0(tr0("%(healRateLabel)s %(value)s %(health)s / %(second)s"),
			{{"healRateLabel", tr0("Heal:")}, {"value", num((int64_t)std::lround(heal))}, {"health", tr0("Health")}, {"second", tr0("second")}});
}

// getTurretsTooltip: "Turret Positions: 4".
inline String turrets_tooltip(int count) {
	if (count <= 0) return String();
	return sprintf0(tr0("%(label)s: %(turretsLimit)s"), {{"label", tr0("Turret Positions")}, {"turretsLimit", num(count)}});
}

// getPopulationBonusTooltip: "Population Bonus: 10".
inline String population_bonus_tooltip(int bonus) {
	if (bonus <= 0) return String();
	return sprintf0(tr0("%(label)s %(bonus)s"), {{"label", tr0("Population Bonus:")}, {"bonus", num(bonus)}});
}

// getProjectilesTooltip: "Projectile Limit: 23, Default: 3, Per Unit: 1".
inline String projectiles_tooltip(int limit, int default_arrows, float per_unit) {
	if (limit <= 0) return String();
	auto part = [](const char *label, const String &v) { return sprintf0(tr0("%(label)s: %(value)s"), {{"label", tr0(label)}, {"value", v}}); };
	return part("Projectile Limit", num(limit)) + comma() + part("Default", num(default_arrows)) + comma() +
			part("Per Unit", String::num(std::round(per_unit * 100.0f) / 100.0f));
}

// getResourceTrickleTooltip: "Resource Trickle: Food 1  Wood 1 / 2 seconds".
inline String trickle_tooltip(const float rate[RES_COUNT], float interval_s) {
	static const uint8_t kOrder[] = {RES_FOOD, RES_WOOD, RES_STONE, RES_GOLD};
	String res;
	for (uint8_t r : kOrder) {
		if (rate[r] == 0.0f) continue;
		if (!res.is_empty()) res += "  ";
		res += sprintf0(tr0("%(resourceIcon)s %(rate)s"), {{"resourceIcon", resource_name(r)}, {"rate", String::num(rate[r])}});
	}
	if (res.is_empty()) return String();
	const String secs = sprintf0(tr0("%(time)s %(second)s"),
			{{"time", String::num(interval_s, 2)}, {"second", tr0(interval_s == 1.0f ? "second" : "seconds")}});
	return sprintf0(tr0("%(label)s %(details)s"), {{"label", tr0("Resource Trickle:")},
			{"details", sprintf0(tr0("%(resources)s / %(time)s"), {{"resources", res}, {"time", secs}})}});
}

// getGarrisonTooltip's Garrisonable part: "Garrison Size: 1 + 3" when the unit is larger than
// one slot or itself carries units (GarrisonHolder.occupiedSlots).
inline String garrison_size_tooltip(int size, int extra) {
	if (size <= 1 && extra <= 0) return String();
	return sprintf0(tr0("%(label)s: %(garrisonSize)s %(extraSize)s"), {{"label", tr0("Garrison Size")},
			{"garrisonSize", num(size)}, {"extraSize", extra > 0 ? tr0("+ ") + num(extra) : String()}}).strip_edges();
}

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

// getSecondsString: "1 second", "7.5 seconds".
inline String seconds_string(float seconds) {
	return sprintf0(tr0("%(time)s %(second)s"),
			{{"time", String::num(seconds, 3)}, {"second", tr0(std::fabs(seconds - 1.0f) < 1e-4f ? "second" : "seconds")}});
}

// getStatusEffectsTooltip for a receiver (a status icon of selection_details.js):
// "Burning: 5.0 Fire, Interval: 1 second, Duration: 7.5 seconds, This unit is burning."
// with the Duration still to run (Duration - _timeElapsed).
inline String status_tooltip(uint8_t kind, float damage, float interval, float remaining) {
	const bool burning = kind == 2;
	String info;
	if (damage > 0.0f)
		info = sprintf0(tr0("%(damage)s %(damageType)s"), {{"damage", fixed(damage, 1)}, {"damageType", tr0(burning ? "Fire" : "Poison")}});
	if (interval > 0.0f)
		info += (info.is_empty() ? String() : comma()) +
				sprintf0(tr0("%(label)s %(details)s"), {{"label", tr0("Interval:")}, {"details", seconds_string(interval)}});
	info += (info.is_empty() ? String() : comma()) +
			sprintf0(tr0("%(durName)s: %(duration)s"), {{"durName", tr0("Duration")}, {"duration", seconds_string(remaining)}});
	info += comma() + tr0(burning ? "This unit is burning." : "This unit is poisoned.");
	return sprintf0(tr0("%(statusName)s: %(statusInfo)s"), {{"statusName", tr0(burning ? "Burning" : "Poisoned")}, {"statusInfo", info}});
}

// The resourceCarrying caption of a foundation or a structure being repaired: "(3) 0:12" (0.28
// puts the builders and the m:ss on two lines beside the repair icon), empty with no builders.
inline String build_work_caption(int builders, float remaining) {
	if (builders <= 0) return String();
	const int64_t secs = (int64_t)remaining;
	return sprintf0(tr0("(%(number)s)\n%(time)s"), {{"number", num(builders)},
			{"time", num(secs / 60) + ":" + String::num_int64(secs % 60).pad_zeros(2)}}).replace("\n", " ");
}

// getBuildTimeTooltip / getRepairTimeTooltip.
inline String build_work_tooltip(bool repair, int builders, float remaining, float remaining_new) {
	String tip = sprintf0(tr0("%(label)s %(details)s"),
			{{"label", tr0(repair ? "Number of repairers:" : "Number of builders:")}, {"details", num(builders)}});
	if (builders > 0) {
		tip += "\n" + sprintf0(tr0("%(label)s %(details)s"), {{"label", tr0(repair ? "Remaining repair time:" : "Remaining build time:")},
				{"details", seconds_string(std::floor(remaining))}});
		const int64_t cut = (int64_t)std::lround(remaining - remaining_new);
		tip += "\n" + sprintf0(tr0(repair
				? (cut == 1 ? "Add another worker to speed up the repairs by %(second)s second." : "Add another worker to speed up the repairs by %(second)s seconds.")
				: (cut == 1 ? "Add another worker to speed up the construction by %(second)s second." : "Add another worker to speed up the construction by %(second)s seconds.")),
				{{"second", num(cut)}});
	} else {
		const int64_t t = (int64_t)std::lround(remaining_new);
		tip += "\n" + sprintf0(tr0(repair
				? (t == 1 ? "Add a worker to finish the repairs in %(second)s second." : "Add a worker to finish the repairs in %(second)s seconds.")
				: (t == 1 ? "Add a worker to finish the construction in %(second)s second." : "Add a worker to finish the construction in %(second)s seconds.")),
				{{"second", num(t)}});
	}
	return tip;
}

// damageDetails over a templates.json damage map ({"Pierce": 15.5}), in 0.28's damage type order.
inline String damage_map_details(const godot::Dictionary &dmg) {
	static const char *const kTypes[] = {"Hack", "Pierce", "Crush", "Fire", "Poison"};
	String out;
	for (const char *t : kTypes) {
		const float v = (float)(double)dmg.get(t, 0.0);
		if (v <= 0.0f) continue;
		if (!out.is_empty()) out += comma();
		out += sprintf0(tr0("%(damage)s %(damageType)s"), {{"damage", fixed(v, 1)}, {"damageType", tr0(t)}});
	}
	return out;
}

// getStatusEffectsTooltip for an applier: "Poisoned: 2.0 Poison, Interval: 0.6 seconds,
// Duration: 3.6 seconds, This unit causes poison damage. (stacks)".
inline String applied_status_tooltip(const String &code, const godot::Dictionary &st) {
	String info = damage_map_details(st.get("damage", godot::Dictionary()));
	const float interval = (float)(double)st.get("interval", 0.0), duration = (float)(double)st.get("duration", 0.0);
	if (interval > 0.0f)
		info += (info.is_empty() ? String() : comma()) +
				sprintf0(tr0("%(label)s %(details)s"), {{"label", tr0("Interval:")}, {"details", seconds_string(interval)}});
	if (duration > 0.0f)
		info += (info.is_empty() ? String() : comma()) +
				sprintf0(tr0("%(durName)s: %(duration)s"), {{"durName", tr0("Duration")}, {"duration", seconds_string(duration)}});
	const String tip = st.get("tooltip", String());
	if (!tip.is_empty()) info += (info.is_empty() ? String() : comma()) + tr0(tip);
	const String stack = st.get("stackability", String());
	const char *stacking = stack == "Stack" ? "(stacks)" : stack == "Replace" ? "(replaces)" : stack == "Extend" ? "(extends)" : "";
	return sprintf0(tr0("%(statusName)s: %(statusInfo)s %(stackability)s"),
			{{"statusName", tr0(String(st.get("name", code)))}, {"statusInfo", info}, {"stackability", tr0(stacking)}}).strip_edges();
}

// getAttackTooltip for a unit: one line per attack of its template, in the template's order and
// under the weapon's name ("Spear: 4.5 Hack, 4.0 Pierce, Range: 4 meters, Interval: 1 second"),
// attackEffectsDetails' "gives Poisoned", each applied status and the splash on lines below.
// `attacks` is the template's templates.json viewer.attacks; the damage, range and interval
// come from the simulation (technologies applied). Without a template, one Melee or Ranged
// line and the Capture line. `elevated_m`: the Ranged attack's range where the unit stands
// (GuiInterface elevationAdaptedRange), in metres, 0 for none.
inline String attack_tooltip(const Simulation::UnitDetails &d, const godot::Array &attacks = godot::Array(), float elevated_m = 0.0f) {
	const String indent2 = "    "; // the details are indented two spaces, these lines four
	std::vector<String> lines;
	auto attack_line = [&](const String &name, const String &effects, float lo, float hi, float interval, const String &statuses,
							   const String &splash, float elevated = 0.0f) {
		lines.push_back(sprintf0(tr0("%(attackLabel)s: %(effects)s, %(range)s, %(rate)s%(statusEffects)s%(splash)s"),
				{{"attackLabel", tr0(name)}, {"effects", effects}, {"range", range_details(lo, hi, elevated)}, {"rate", interval_details(interval)},
				 {"statusEffects", statuses}, {"splash", splash}}));
	};
	String damage;
	for (int k = 0; k < 4; ++k) {
		if (d.damage[k] <= 0.0f) continue;
		if (!damage.is_empty()) damage += comma();
		damage += sprintf0(tr0("%(damage)s %(damageType)s"), {{"damage", fixed(d.damage[k], 1)}, {"damageType", damage_type_name(k)}});
	}
	const String capture = d.capture > 0.0f
			? sprintf0(tr0("%(amount)s %(name)s"), {{"amount", fixed(d.capture, 1)}, {"name", tr0("Capture")}}) : String();
	bool listed_damage = false, listed_capture = false;
	for (int a = 0; a < attacks.size(); ++a) {
		const godot::Dictionary at = attacks[a];
		const String type = at.get("type", String());
		if (type == "Slaughter") continue;
		const String name = at.get("name", type);
		if (type == "Capture") {
			if (capture.is_empty() || listed_capture) continue;
			listed_capture = true;
			attack_line(name, capture, (float)(double)at.get("min_range", 0.0), (float)(double)at.get("max_range", 4.0),
					(float)(double)at.get("repeat_time", 1.0), String(), String());
			continue;
		}
		if (damage.is_empty() || listed_damage || (type != "Melee" && type != "Ranged")) continue;
		listed_damage = true;
		String effects = damage, statuses;
		const godot::Dictionary status = at.get("status", godot::Dictionary());
		if (!status.is_empty()) {
			String names;
			const godot::Array codes = status.keys();
			for (int c = 0; c < codes.size(); ++c) {
				const godot::Dictionary st = status[codes[c]];
				names += (names.is_empty() ? String() : comma()) + tr0(String(st.get("name", codes[c])));
				statuses += "\n" + indent2 + applied_status_tooltip(codes[c], st);
			}
			effects += comma() + sprintf0(tr0("gives %(name)s"), {{"name", names}});
		}
		String splash;
		const godot::Dictionary sp = at.get("splash", godot::Dictionary());
		if (!sp.is_empty()) {
			// splashDetails; Friendly Fire only when it is on (g_AlwaysDisplayFriendlyFire is off).
			splash = "\n" + indent2 + sprintf0(tr0("%(label)s: %(effects)s"),
					{{"label", sprintf0(tr0("%(splashShape)s Splash"), {{"splashShape", tr0(String(sp.get("shape", "Circular")))}})},
					 {"effects", damage_map_details(sp.get("damage", godot::Dictionary()))}});
			if ((bool)sp.get("friendly_fire", false))
				splash += comma() + sprintf0(tr0("Friendly Fire: %(enabled)s"), {{"enabled", tr0("Yes")}});
		}
		attack_line(name, effects, d.min_range, d.max_range, d.interval, statuses, splash, d.ranged ? elevated_m : 0.0f);
	}
	if (!damage.is_empty() && !listed_damage)
		attack_line(d.ranged ? "Ranged" : "Melee", damage, d.min_range, d.max_range, d.interval, String(), String(), d.ranged ? elevated_m : 0.0f);
	if (!capture.is_empty() && !listed_capture) attack_line("Capture", capture, 0.0f, 4.0f, 1.0f, String(), String());
	if (lines.empty()) return String();
	String details;
	for (const String &l : lines) details += String(details.is_empty() ? "" : "\n") + "  " + l;
	return sprintf0(tr0("%(label)s\n%(details)s"), {{"label", tr0("Attack:")}, {"details", details}});
}

// getVisibleEntityClassesFormatted: "Classes: Infantry, Melee, Spearman".
inline String classes_line(const godot::Array &classes) {
	if (classes.is_empty()) return String();
	String list;
	for (int i = 0; i < classes.size(); ++i) list += (i ? tr0(", ") : String()) + tr0(String(classes[i]));
	return tr0("Classes:") + " " + list;
}

// getTreasureTooltip: "Reward: Food 100  Wood 50" (resource names for the icons), from
// amounts in 0 A.D.'s order: food, wood, stone, metal.
inline String reward_tooltip(const int amount[4]) {
	static const uint8_t kRes[] = {RES_FOOD, RES_WOOD, RES_STONE, RES_GOLD};
	String details;
	for (int k = 0; k < 4; ++k)
		if (amount[k] > 0)
			details += (details.is_empty() ? "" : "  ") +
					sprintf0(tr0("%(resourceIcon)s %(reward)s"), {{"resourceIcon", resource_name(kRes[k])}, {"reward", num(amount[k])}});
	if (details.is_empty()) return String();
	return sprintf0(tr0("%(label)s %(details)s"), {{"label", tr0("Reward:")}, {"details", details}});
}

// The portrait's last line: getFollowOnLeftClickTooltip (a unit) or getFocusOnLeftClickTooltip,
// then getTemplateViewerOnRightClickTooltip.
inline String portrait_click_line(bool unit) {
	return tr0(unit ? "Left-click to follow." : "Left-click to focus.") + " " + tr0("Right-click for more information.");
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

// getAurasTooltip over a unit template's resolved Auras. The token merge lists the class
// auras of its parent templates first: template_unit_hero's units/heroes/hero_garrison,
// then the infantry templates' units/ram_garrison. Their effects are simulated in
// structure_capture_garrison_regen and unit_base_walk_speed.
inline String unit_auras_tooltip(uint16_t tmpl) {
	if (tmpl >= CIV_UNIT_TEMPLATE_COUNT) return String();
	const CivUnitTemplate &u = civ_unit_template(tmpl);
	String out;
	if (u.tech_classes & TC_HERO)
		out = aura_line(tr0("Inspired Defense"), "When garrisoned, the Structure has +1 capture points garrison regeneration rate.", 0.0f);
	if (u.tech_classes & TC_INFANTRY)
		out = join_lines({out, aura_line(tr0("Battering Team"), "Battering Rams +0.15 speed for each garrisoned soldier.", 0.0f)});
	for (int16_t a : u.auras) {
		if (a < 0) continue;
		const auto &aura = civ_hero_aura(a);
		out = join_lines({out, aura_line(tr0(String::utf8(aura.name)), String::utf8(aura.description ? aura.description : ""), aura.radius)});
	}
	return out;
}

// getTradingTooltip: the trader's owner's gain plus what their own markets earn, then
// each other market owner's, "20+5 (Alice), 5 (Bob)". market1 pays the arrival market's
// owner and market2 the starting market's (CalculateTraderGain).
template <class NameOf>
inline String trading_gain(int trader, uint8_t owner, int market1, uint8_t market1_owner, int market2, uint8_t market2_owner, NameOf &&name_of) {
	const struct { int gain; uint8_t owner; } markets[] = {{market1, market1_owner}, {market2, market2_owner}};
	String primary = num(trader);
	for (const auto &m : markets)
		if (m.gain && m.owner == owner) primary += tr0("+") + num(m.gain);
	auto gain_of = [&](const String &gain, uint8_t pid) {
		return sprintf0(tr0("%(gain)s (%(player)s)"), {{"gain", gain}, {"player", name_of(pid)}});
	};
	String out = gain_of(primary, owner);
	for (const auto &m : markets)
		if (m.gain && m.owner != owner) out += tr0(", ") + gain_of(num(m.gain), m.owner);
	return out;
}

} // namespace rtso::seldet

#endif
