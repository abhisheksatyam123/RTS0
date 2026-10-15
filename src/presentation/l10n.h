#ifndef RTSO_L10N_H
#define RTSO_L10N_H

// 0 A.D.'s translate() and sprintf() for the presentation code: strings built
// from parts look up one 0 A.D. format msgid ("%(player)s has sent you
// %(amounts)s.") and fill its named arguments, so a language can reorder them
// (assets/l10n/<lang>.po, loaded by scenes/menu/user_settings.gd).

#include <godot_cpp/classes/translation_server.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <utility>

#include "user_config.h"

namespace rtso {

// translate(msgid): the current language's string, else the English msgid.
inline godot::String tr0(const godot::String &msgid) {
	godot::TranslationServer *ts = godot::TranslationServer::get_singleton();
	return ts ? godot::String(ts->translate(msgid)) : msgid;
}

// sprintf with named arguments: each "%(key)s" becomes its value.
inline godot::String sprintf0(const godot::String &format,
		std::initializer_list<std::pair<const char *, godot::String>> args) {
	godot::String s = format;
	for (const auto &a : args) s = s.replace(godot::String("%(") + a.first + ")s", a.second);
	return s;
}

// tooltips.js getEntityNames under gui.session.howtoshownames (0 specific
// first, 1 generic first, 2 only specific, 3 only generic): "Primary
// (Secondary)", or the one name.
// g_SpecificNamesPrimary / g_ShowSecondaryNames: the primary name and, when shown, the secondary.
inline void entity_name_parts(const godot::String &generic, const godot::String &specific, godot::String &primary, godot::String &secondary) {
	const std::string mode = user_config::get("gui.session.howtoshownames");
	const bool specific_first = mode != "1" && mode != "3";
	const bool show_secondary = mode != "2" && mode != "3";
	secondary = godot::String();
	if (specific.is_empty() || specific == generic) {
		primary = specific.is_empty() ? tr0(generic) : specific;
	} else {
		primary = specific_first ? specific : tr0(generic);
		if (show_secondary) secondary = specific_first ? tr0(generic) : specific;
	}
}

// selection_panels.js Upgrade: "Upgrade to a %(primaryName)s (%(secondaryName)s). %(tooltip)s".
inline godot::String upgrade_to_text(const godot::String &generic, const godot::String &specific, const godot::String &hint) {
	godot::String primary, secondary;
	entity_name_parts(generic, specific, primary, secondary);
	if (!secondary.is_empty())
		return sprintf0(tr0(hint.is_empty() ? "Upgrade to a %(primaryName)s (%(secondaryName)s)." : "Upgrade to a %(primaryName)s (%(secondaryName)s). %(tooltip)s"),
				{{"primaryName", primary}, {"secondaryName", secondary}, {"tooltip", tr0(hint)}});
	return sprintf0(tr0(hint.is_empty() ? "Upgrade to a %(primaryName)s." : "Upgrade to a %(primaryName)s. %(tooltip)s"),
			{{"primaryName", primary}, {"tooltip", tr0(hint)}});
}

inline godot::String entity_names(const godot::String &generic, const godot::String &specific, bool packed = false) {
	const std::string mode = user_config::get("gui.session.howtoshownames");
	const bool specific_first = mode != "1" && mode != "3";
	const bool show_secondary = mode != "2" && mode != "3";
	godot::String primary, secondary;
	if (specific.is_empty() || specific == generic) {
		primary = specific.is_empty() ? tr0(generic) : specific;
	} else {
		primary = specific_first ? specific : tr0(generic);
		if (show_secondary) secondary = specific_first ? tr0(generic) : specific;
	}
	// selection_details.js: a packed siege engine adds that to the secondary name.
	if (packed)
		secondary = secondary.is_empty() ? tr0("Packed")
				: sprintf0(tr0(godot::String::utf8("%(secondaryName)s — Packed")), {{"secondaryName", secondary}});
	if (secondary.is_empty()) return specific.is_empty() || specific == generic ? primary : sprintf0(tr0("%(primaryName)s"), {{"primaryName", primary}});
	return sprintf0(tr0("%(primaryName)s (%(secondaryName)s)"), {{"primaryName", primary}, {"secondaryName", secondary}});
}

// tooltips.js getSecondsString: "%(time)s %(second)s".
inline godot::String seconds_string(int64_t seconds) {
	return sprintf0(tr0("%(time)s %(second)s"),
			{{"time", godot::String::num_int64(seconds)}, {"second", tr0(seconds == 1 ? "second" : "seconds")}});
}

// "%(label)s %(details)s", the label/value line of tooltips.js.
inline godot::String label_details(const godot::String &label, const godot::String &details) {
	return sprintf0(tr0("%(label)s %(details)s"), {{"label", tr0(label)}, {"details", details}});
}

// g_TooltipTextFormats (tooltips.js) as the BBCode the tooltip layer draws
// (scenes/common/tooltips.gd): objections bold red, headers bold, resource icons
// inline, hotkeys in 255 251 131 (color.js g_HotkeyTags).
inline godot::String objection_font(const godot::String &text) { return "[color=red][b]" + text + "[/b][/color]"; }
inline godot::String header_font(const godot::String &text) { return "[b]" + text + "[/b]"; }
// namePrimaryBig / namePrimarySmall / nameSecondary: sans-bold-16, sans-bold-12, sans-bold-16.
inline godot::String name_big(const godot::String &text) { return "[font_size=16][b]" + text + "[/b][/font_size]"; }
// The primary name in small caps: its first letter big, the rest upper case and small.
inline godot::String name_small_caps(const godot::String &name) {
	if (name.is_empty()) return name;
	return name_big(name.substr(0, 1)) + "[font_size=12][b]" + name.substr(1).to_upper() + "[/b][/font_size]";
}
// tooltips.js getEntityNamesFormatted: the Construction, Training and Research tooltips' first line.
inline godot::String entity_names_formatted(const godot::String &generic, const godot::String &specific) {
	if (specific.is_empty()) return name_big(tr0(generic));
	godot::String primary, secondary;
	entity_name_parts(generic, specific, primary, secondary);
	if (secondary.is_empty()) return sprintf0(tr0("%(primaryName)s"), {{"primaryName", name_small_caps(primary)}});
	return sprintf0(tr0("%(primaryName)s (%(secondaryName)s)"),
			{{"primaryName", name_small_caps(primary)}, {"secondaryName", name_big(secondary)}});
}
inline godot::String resource_icon(const char *code) {
	return godot::String("[img=16x16]res://assets/textures/ui/archive/icons/resources/") + code + "_small.png[/img]";
}
inline godot::String hotkey_tag(const godot::String &key) { return "[color=#fffb83][lb]" + key + "[rb][/color]"; }

// A cost in getCostTypes() order: food, wood, stone, metal, population, time.
struct CostTip {
	int64_t food = 0, wood = 0, stone = 0, metal = 0, population = 0, time = 0;
};

// getEntityCostComponentsTooltipString: one "%(component)s %(cost)s" per cost
// type, the resource's name standing in for its icon; time reads as seconds.
// rich: each component is its resourceIcon, time included, as 0.28 draws it.
inline godot::String cost_components(const CostTip &c, const char *sep = "  ", bool rich = false) {
	godot::String out;
	auto add = [&](const godot::String &entry) {
		if (!out.is_empty()) out += sep;
		out += entry;
	};
	const struct { int64_t v; const char *name; const char *icon; } parts[] = {{c.food, "Food", "food"}, {c.wood, "Wood", "wood"},
			{c.stone, "Stone", "stone"}, {c.metal, "Metal", "metal"}, {c.population, "Population", "population"}};
	for (const auto &p : parts)
		if (p.v > 0)
			add(sprintf0(tr0("%(component)s %(cost)s"),
					{{"component", rich ? resource_icon(p.icon) : tr0(p.name)}, {"cost", godot::String::num_int64(p.v)}}));
	if (c.time > 0)
		add(rich ? sprintf0(tr0("%(component)s %(cost)s"), {{"component", resource_icon("time")}, {"cost", godot::String::num_int64(c.time)}})
				 : seconds_string(c.time));
	return out;
}

// getEntityCostTooltip: "Cost: Food 50  Wood 50  10 seconds", or "".
inline godot::String cost_tooltip(const CostTip &c, bool rich = false) {
	const godot::String costs = cost_components(c, "  ", rich);
	if (costs.is_empty()) return costs;
	return sprintf0(tr0("%(label)s %(costs)s"), {{"label", rich ? header_font(tr0("Cost:")) : tr0("Cost:")}, {"costs", costs}});
}

// getNeededResourcesTooltip: "Insufficient resources: Food 20  Wood 5", or ""
// when the stock covers the cost.
inline godot::String needed_resources_tooltip(const CostTip &cost, const float stock_food, const float stock_wood,
		const float stock_stone, const float stock_metal, bool rich = false) {
	CostTip need;
	auto short_by = [](int64_t c, float s) { return c > s ? (int64_t)std::ceil((double)c - s) : (int64_t)0; };
	need.food = short_by(cost.food, stock_food);
	need.wood = short_by(cost.wood, stock_wood);
	need.stone = short_by(cost.stone, stock_stone);
	need.metal = short_by(cost.metal, stock_metal);
	const godot::String parts = cost_components(need, "  ", rich);
	if (parts.is_empty()) return parts;
	return (rich ? objection_font(tr0("Insufficient resources:")) : tr0("Insufficient resources:")) + " " + parts;
}

// selection_panels_helpers.js formatBatchTrainingString: "Shift-click to
// train 5.", "... 10 (2*5).", "... 27 (15 + 12).", or "" below two units.
inline godot::String batch_training_string(int buildings, int batch, int remainder) {
	const int total = buildings * batch + remainder;
	if (total < 2) return godot::String();
	godot::String full;
	if (buildings > 1)
		full = sprintf0(tr0("%(buildings)s*%(batchSize)s"),
				{{"buildings", godot::String::num_int64(buildings)}, {"batchSize", godot::String::num_int64(batch)}});
	else if (buildings == 1)
		full = godot::String::num_int64(batch);
	const char *format = "%(action)s to train %(number)s.";
	if (buildings > 1 || (buildings == 1 && remainder > 0))
		format = remainder > 0 ? "%(action)s to train %(number)s (%(fullBatch)s + %(remainderBatch)s)."
				: "%(action)s to train %(number)s (%(fullBatch)s).";
	return sprintf0(tr0(format), {{"action", header_font(tr0("Shift-click"))}, {"number", godot::String::num_int64(total)},
			{"fullBatch", full}, {"remainderBatch", godot::String::num_int64(remainder)}});
}

} // namespace rtso

#endif
