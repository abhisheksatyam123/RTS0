#ifndef RTSO_HOTKEYS_H
#define RTSO_HOTKEYS_H

// Named hotkeys read from 0 A.D. config files (port of source/ps/Hotkey.cpp and
// the [hotkey.*] sections of binaries/data/config/default.cfg, 0 A.D. 61a3b95).
//
// A hotkey name ("session.stop", "camera.jump.1") maps to one or more key
// combinations ("H", "Ctrl+F5"). Key names follow 0 A.D. config/keys.txt and are
// case-insensitive. This file has no Godot dependency so tools/test_hotkeys.cpp
// can test it headless; hotkeys_godot.h maps Godot input onto these names.
//
// Matching follows Hotkey.cpp's spec:
//  - a hotkey fires when its last key is pressed while all its other keys are
//    held; extra held keys don't stop it (Ctrl+F+A still fires Ctrl+F);
//  - when several match, only the most specific (most keys) fire, so Ctrl+F5
//    (camera.jump.set.1) wins over F5 (camera.jump.1);
//  - hotkeys of the same specificity all fire, and the caller picks by context.

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rtso {
namespace hotkeys {

// Lower-case canonical key name ("ctrl", "f5", "leftbracket"), resolving the
// aliases in 0 A.D. keys.txt ("Esc" -> "escape", "Enter" -> "return",
// "Right Ctrl" -> "ctrl", "[" -> "leftbracket"). Empty for an empty name.
std::string canonical_key(const std::string &name);

// One key combination: every key in `keys` held, `keys.back()` pressed last.
struct Combo {
	std::vector<std::string> keys; // canonical names, primary key last
	const std::string &primary() const { return keys.back(); }
};

// "Ctrl+F5" -> {ctrl, f5}. Empty combos ("" or "unused") parse to no keys.
Combo parse_combo(const std::string &text);
std::string combo_to_string(const Combo &c); // "Ctrl+F5"
// The combination as 0.28 shows it (KeyName.cpp FindKeyName, hotkeys.js
// formatHotkeyCombination): modifiers first in Alt, Ctrl, Shift, Super order,
// arrows as glyphs, "Equals", "Backslash", "SingleQuote", "WheelLeft".
std::string combo_display(const Combo &c);
// Every combination of a hotkey for display, shortest first, joined by ", "
// (formatHotkeyCombinations); empty when unbound.
std::string combos_display(const std::vector<Combo> &combos);

using IsDown = std::function<bool(const std::string &key)>;

class HotkeyTable {
public:
	// Parses 0 A.D. cfg text. Keys in a [hotkey] or [hotkey.x] section, or a
	// top-level "hotkey.x.y = ..." line (user.cfg style), set the hotkey's whole
	// binding list, replacing any earlier binding of the same name. Other
	// sections are ignored. Returns the number of hotkeys set.
	int load_cfg(const std::string &text);

	void set(const std::string &name, const std::vector<Combo> &combos);
	void clear() { bindings_.clear(); }

	const std::vector<Combo> *get(const std::string &name) const;
	std::vector<std::string> names() const; // sorted

	// Names that fire when `primary` goes down, given the keys currently held
	// (the most specific matches only).
	std::vector<std::string> on_press(const std::string &primary, const IsDown &is_down) const;

	// True while every key of one of the hotkey's combos is held (0 A.D.
	// Engine.HotkeyIsPressed for modifier hotkeys such as session.queue = Shift).
	bool is_held(const std::string &name, const IsDown &is_down) const;

	// Like is_held, but false when a more specific hotkey with the same primary
	// key is also held: with Ctrl down, A is camera.rotate.cw (Ctrl+A), not
	// camera.left (A). Used for keys polled every frame.
	bool is_held_exclusive(const std::string &name, const IsDown &is_down) const;

	// "hotkey.<name> = "Ctrl+F5", "F5"" lines for every hotkey whose binding
	// differs from `base` (the user.cfg a hotkey editor writes).
	std::string diff_cfg(const HotkeyTable &base) const;

	// Hotkeys bound to exactly this key set (0 A.D. Engine.GetConflicts).
	std::vector<std::string> bound_to(const Combo &combo) const;

	// Every binding as 0.28's tooltips show it (colorizeHotkey: "Pause, Shift+Space"); "" when unbound.
	std::string label(const std::string &name) const;

private:
	struct Entry {
		std::string name;
		Combo combo;
	};
	void rebuild_index();

	std::unordered_map<std::string, std::vector<Combo>> bindings_;
	std::unordered_map<std::string, std::vector<Entry>> by_primary_;
};

// 0 A.D.'s default.cfg [hotkey.*] sections plus RTS0's own keys (hotkey_defaults.h).
const char *default_cfg();

// The table every UI node reads. Starts with default_cfg(); the game loads
// user.cfg over it at startup.
HotkeyTable &table();

} // namespace hotkeys
} // namespace rtso

#endif
