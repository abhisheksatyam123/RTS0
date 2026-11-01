#ifndef RTSO_SESSION_SKIN_H
#define RTSO_SESSION_SKIN_H

// The session dialogs' 0.28 skin, built by scenes/common/stone_theme.gd session_theme():
// the modern widgets with StoneButton buttons and the GenericPanel, ModernDialog,
// ModernDarkBoxGold and ChatPanel type variations.

#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/theme.hpp>

namespace rtso {

inline godot::Ref<godot::Theme> session_skin() {
	using namespace godot;
	Ref<Script> s = ResourceLoader::get_singleton()->load("res://scenes/common/stone_theme.gd");
	if (s.is_null()) return Ref<Theme>();
	return s->call("session_theme");
}

// The modern skin alone (scenes/common/modern_theme.gd): ModernButtonRed buttons.
inline godot::Ref<godot::Theme> modern_skin() {
	using namespace godot;
	Ref<Script> s = ResourceLoader::get_singleton()->load("res://scenes/common/modern_theme.gd");
	if (s.is_null()) return Ref<Theme>();
	return s->call("theme");
}

inline void clear_session_skins() {
	using namespace godot;
	ResourceLoader *rl = ResourceLoader::get_singleton();
	if (!rl) return;
	Ref<Script> st = rl->load("res://scenes/common/stone_theme.gd");
	if (st.is_valid()) st->call("clear_cache");
	Ref<Script> mt = rl->load("res://scenes/common/modern_theme.gd");
	if (mt.is_valid()) mt->call("clear_cache");
}

} // namespace rtso

#endif
