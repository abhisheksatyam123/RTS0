#ifndef RTSO_L10N_H
#define RTSO_L10N_H

// 0 A.D.'s translate() and sprintf() for the presentation code: strings built
// from parts look up one 0 A.D. format msgid ("%(player)s has sent you
// %(amounts)s.") and fill its named arguments, so a language can reorder them
// (assets/l10n/<lang>.po, loaded by scenes/menu/user_settings.gd).

#include <godot_cpp/classes/translation_server.hpp>
#include <godot_cpp/variant/string.hpp>

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
inline godot::String entity_names(const godot::String &generic, const godot::String &specific) {
	if (specific.is_empty()) return tr0(generic);
	if (specific == generic) return specific;
	const std::string mode = user_config::get("gui.session.howtoshownames");
	const bool specific_first = mode != "1" && mode != "3";
	const bool secondary = mode != "2" && mode != "3";
	const godot::String primary = specific_first ? specific : tr0(generic);
	if (!secondary) return sprintf0(tr0("%(primaryName)s"), {{"primaryName", primary}});
	return sprintf0(tr0("%(primaryName)s (%(secondaryName)s)"),
			{{"primaryName", primary}, {"secondaryName", specific_first ? tr0(generic) : specific}});
}

} // namespace rtso

#endif
