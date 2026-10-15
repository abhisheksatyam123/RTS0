#include "hotkey_config.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>

#include "hotkeys_godot.h"

namespace hk = rtso::hotkeys;

static std::string to_std(const String &s) { return s.utf8().get_data(); }
static String to_godot(const std::string &s) { return String::utf8(s.c_str()); }

static PackedStringArray combos_to_strings(const std::vector<hk::Combo> *combos) {
	PackedStringArray out;
	if (combos)
		for (const auto &c : *combos) out.push_back(to_godot(hk::combo_to_string(c)));
	return out;
}

void RtsoHotkeyConfig::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_hotkey_map"), &RtsoHotkeyConfig::get_hotkey_map);
	ClassDB::bind_method(D_METHOD("get_default", "name"), &RtsoHotkeyConfig::get_default);
	ClassDB::bind_method(D_METHOD("set_hotkey", "name", "combos"), &RtsoHotkeyConfig::set_hotkey);
	ClassDB::bind_method(D_METHOD("get_conflicts", "combo"), &RtsoHotkeyConfig::get_conflicts);
	ClassDB::bind_method(D_METHOD("save_user"), &RtsoHotkeyConfig::save_user);
	ClassDB::bind_method(D_METHOD("reset_user"), &RtsoHotkeyConfig::reset_user);
	ClassDB::bind_method(D_METHOD("reload"), &RtsoHotkeyConfig::reload);
	ClassDB::bind_method(D_METHOD("event_key_name", "event"), &RtsoHotkeyConfig::event_key_name);
	ClassDB::bind_method(D_METHOD("fired", "event"), &RtsoHotkeyConfig::fired);
	ClassDB::bind_method(D_METHOD("format_keys", "keys"), &RtsoHotkeyConfig::format_keys);
	ClassDB::bind_method(D_METHOD("display", "name"), &RtsoHotkeyConfig::display);
	ClassDB::bind_method(D_METHOD("display_combo", "combo"), &RtsoHotkeyConfig::display_combo);
	ClassDB::bind_method(D_METHOD("is_held", "name"), &RtsoHotkeyConfig::is_held);
}

Dictionary RtsoHotkeyConfig::get_hotkey_map() const {
	Dictionary d;
	const auto &t = hk::table();
	for (const auto &name : t.names()) d[to_godot(name)] = combos_to_strings(t.get(name));
	return d;
}

PackedStringArray RtsoHotkeyConfig::get_default(const String &name) const {
	hk::HotkeyTable base;
	base.load_cfg(hk::default_cfg());
	return combos_to_strings(base.get(to_std(name)));
}

void RtsoHotkeyConfig::set_hotkey(const String &name, const PackedStringArray &combos) {
	std::vector<hk::Combo> v;
	for (int i = 0; i < combos.size(); ++i) {
		hk::Combo c = hk::parse_combo(to_std(combos[i]));
		if (!c.keys.empty()) v.push_back(std::move(c));
	}
	hk::table().set(to_std(name), v);
}

PackedStringArray RtsoHotkeyConfig::get_conflicts(const String &combo) const {
	PackedStringArray out;
	for (const auto &n : hk::table().bound_to(hk::parse_combo(to_std(combo)))) out.push_back(to_godot(n));
	return out;
}

bool RtsoHotkeyConfig::save_user() const { return hk::save_user_cfg(); }

void RtsoHotkeyConfig::reset_user() {
	// Back to default.cfg's hotkeys; the options page's settings stay in user.cfg.
	hk::HotkeyTable fresh;
	fresh.load_cfg(hk::default_cfg());
	hk::table() = fresh;
	hk::save_user_cfg();
	reload();
}

void RtsoHotkeyConfig::reload() {
	hk::HotkeyTable fresh;
	fresh.load_cfg(hk::default_cfg());
	hk::table() = fresh;
	hk::load_user_cfg();
}

String RtsoHotkeyConfig::event_key_name(const Ref<InputEvent> &event) const {
	Ref<InputEventKey> key = event;
	if (key.is_valid()) {
		Key code = key->get_physical_keycode();
		if (code == KEY_NONE) code = key->get_keycode();
		return hk::key_name(code);
	}
	Ref<InputEventMouseButton> mb = event;
	if (mb.is_valid()) return hk::mouse_name(mb->get_button_index());
	return String();
}

// The hotkeys a key or mouse press fires, for GUI pages that handle their
// own keys while the session is paused (the in-game menu).
PackedStringArray RtsoHotkeyConfig::fired(const Ref<InputEvent> &event) const {
	PackedStringArray out;
	std::vector<std::string> names;
	Ref<InputEventKey> key = event;
	Ref<InputEventMouseButton> mb = event;
	if (key.is_valid()) names = hk::fired(key);
	else if (mb.is_valid()) names = hk::fired(mb);
	for (const auto &n : names) out.push_back(to_godot(n));
	return out;
}

// colorizeHotkey / formatHotkeyCombinations: every binding as 0.28 shows it, "" when unbound.
String RtsoHotkeyConfig::display(const String &name) const {
	return String::utf8(hk::table().label(to_std(name)).c_str());
}

// formatHotkeyCombination for one stored combination ("LeftArrow" -> the arrow glyph).
String RtsoHotkeyConfig::display_combo(const String &combo) const {
	return String::utf8(hk::combo_display(hk::parse_combo(to_std(combo))).c_str());
}

String RtsoHotkeyConfig::format_keys(const PackedStringArray &keys) const {
	std::string text;
	for (int i = 0; i < keys.size(); ++i) {
		if (i) text += "+";
		text += to_std(keys[i]);
	}
	return to_godot(hk::combo_to_string(hk::parse_combo(text)));
}

bool RtsoHotkeyConfig::is_held(const String &name) const { return hk::held(to_std(name).c_str()); }
