#ifndef RTSO_HOTKEYS_GODOT_H
#define RTSO_HOTKEYS_GODOT_H

// Godot side of the hotkey table (hotkeys.h): maps Godot keys and mouse buttons
// to 0 A.D. key names and loads user://config/user.cfg over the defaults.
// Like 0 A.D. (keys.txt: "those names map to scancodes"), letters are matched by
// physical key position, so WASD stays WASD on AZERTY.

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/variant/string.hpp>

#include <string>
#include <unordered_map>
#include <vector>

#include "hotkeys.h"
#include "user_config.h"

namespace rtso {
namespace hotkeys {

inline const char *key_name(godot::Key k) {
	using namespace godot;
	if (k >= KEY_A && k <= KEY_Z) {
		static const char *letters[26] = {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
				"n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z"};
		return letters[k - KEY_A];
	}
	if (k >= KEY_0 && k <= KEY_9) {
		static const char *digits[10] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
		return digits[k - KEY_0];
	}
	if (k >= KEY_F1 && k <= KEY_F24) {
		static const char *fk[24] = {"f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10", "f11", "f12",
				"f13", "f14", "f15", "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23", "f24"};
		return fk[k - KEY_F1];
	}
	if (k >= KEY_KP_0 && k <= KEY_KP_9) {
		static const char *np[10] = {"num0", "num1", "num2", "num3", "num4", "num5", "num6", "num7", "num8", "num9"};
		return np[k - KEY_KP_0];
	}
	switch (k) {
		case KEY_ESCAPE: return "escape";
		case KEY_ENTER: return "return";
		case KEY_KP_ENTER: return "numenter";
		case KEY_TAB: return "tab";
		case KEY_BACKSPACE: return "backspace";
		case KEY_SPACE: return "space";
		case KEY_DELETE: return "delete";
		case KEY_INSERT: return "insert";
		case KEY_HOME: return "home";
		case KEY_END: return "end";
		case KEY_PAGEUP: return "pageup";
		case KEY_PAGEDOWN: return "pagedown";
		case KEY_LEFT: return "leftarrow";
		case KEY_RIGHT: return "rightarrow";
		case KEY_UP: return "uparrow";
		case KEY_DOWN: return "downarrow";
		case KEY_MINUS: return "minus";
		case KEY_EQUAL: case KEY_PLUS: return "plus";
		case KEY_BRACKETLEFT: return "leftbracket";
		case KEY_BRACKETRIGHT: return "rightbracket";
		case KEY_BACKSLASH: return "backslash";
		case KEY_SEMICOLON: return "semicolon";
		case KEY_APOSTROPHE: return "quote";
		case KEY_QUOTELEFT: return "backquote";
		case KEY_COMMA: return "comma";
		case KEY_PERIOD: return "period";
		case KEY_SLASH: return "slash";
		case KEY_KP_ADD: return "numplus";
		case KEY_KP_SUBTRACT: return "numminus";
		case KEY_KP_MULTIPLY: return "nummultiply";
		case KEY_KP_DIVIDE: return "numdivide";
		case KEY_KP_PERIOD: return "numdecimal";
		case KEY_PAUSE: return "pause";
		case KEY_PRINT: return "printscreen";
		case KEY_SCROLLLOCK: return "scrolllock";
		case KEY_NUMLOCK: return "numlock";
		case KEY_SHIFT: return "shift";
		case KEY_CTRL: return "ctrl";
		case KEY_ALT: return "alt";
		case KEY_META: return "super";
		default: return "";
	}
}

inline const char *mouse_name(godot::MouseButton b) {
	using namespace godot;
	switch (b) {
		case MOUSE_BUTTON_LEFT: return "mouseleft";
		case MOUSE_BUTTON_RIGHT: return "mouseright";
		case MOUSE_BUTTON_MIDDLE: return "mousemiddle";
		case MOUSE_BUTTON_XBUTTON1: return "mousex1";
		case MOUSE_BUTTON_XBUTTON2: return "mousex2";
		case MOUSE_BUTTON_WHEEL_UP: return "wheelup";
		case MOUSE_BUTTON_WHEEL_DOWN: return "wheeldown";
		case MOUSE_BUTTON_WHEEL_LEFT: return "wheelleft";
		case MOUSE_BUTTON_WHEEL_RIGHT: return "wheelright";
		default: return "";
	}
}

// Godot key for a canonical key name (reverse of key_name), KEY_NONE if unknown.
inline godot::Key key_from_name(const std::string &name) {
	using namespace godot;
	static std::unordered_map<std::string, Key> rev;
	if (rev.empty()) {
		for (int64_t k = KEY_SPECIAL; k <= KEY_F35 + 1; ++k) {
			const char *n = key_name((Key)k);
			if (*n && !rev.count(n)) rev[n] = (Key)k;
		}
		for (int64_t k = 32; k < 256; ++k) {
			const char *n = key_name((Key)k);
			if (*n && !rev.count(n)) rev[n] = (Key)k;
		}
	}
	auto it = rev.find(name);
	return it == rev.end() ? KEY_NONE : it->second;
}

// Is the named key (or mouse button) held right now?
inline bool is_down_now(const std::string &name) {
	using namespace godot;
	Input *in = Input::get_singleton();
	if (!in) return false;
	if (name == "shift") return in->is_key_pressed(KEY_SHIFT);
	if (name == "ctrl") return in->is_key_pressed(KEY_CTRL);
	if (name == "alt") return in->is_key_pressed(KEY_ALT);
	if (name == "super") return in->is_key_pressed(KEY_META);
	if (name == "mouseleft") return in->is_mouse_button_pressed(MOUSE_BUTTON_LEFT);
	if (name == "mouseright") return in->is_mouse_button_pressed(MOUSE_BUTTON_RIGHT);
	if (name == "mousemiddle") return in->is_mouse_button_pressed(MOUSE_BUTTON_MIDDLE);
	if (name == "mousex1") return in->is_mouse_button_pressed(MOUSE_BUTTON_XBUTTON1);
	if (name == "mousex2") return in->is_mouse_button_pressed(MOUSE_BUTTON_XBUTTON2);
	const Key k = key_from_name(name);
	return k != KEY_NONE && in->is_physical_key_pressed(k);
}

// Hotkeys fired by a key press (empty for releases, and for repeats unless
// `with_repeats`: 0 A.D.'s held keys repeat "KeyDown" actions, Hotkey.cpp).
inline std::vector<std::string> fired(const godot::Ref<godot::InputEventKey> &key, bool with_repeats = false) {
	using namespace godot;
	if (key.is_null() || !key->is_pressed() || (key->is_echo() && !with_repeats)) return {};
	Key code = key->get_physical_keycode();
	if (code == KEY_NONE) code = key->get_keycode();
	const char *n = key_name(code);
	if (!*n) return {};
	const bool shift = key->is_shift_pressed(), ctrl = key->is_ctrl_pressed(), alt = key->is_alt_pressed(),
			   meta = key->is_meta_pressed();
	const std::string primary = n;
	return table().on_press(primary, [&](const std::string &k) {
		if (k == primary) return true;
		if (k == "shift") return shift;
		if (k == "ctrl") return ctrl;
		if (k == "alt") return alt;
		if (k == "super") return meta;
		return is_down_now(k);
	});
}

// Hotkeys fired by a mouse button press (wheel steps included).
inline std::vector<std::string> fired(const godot::Ref<godot::InputEventMouseButton> &mb) {
	using namespace godot;
	if (mb.is_null() || !mb->is_pressed()) return {};
	const char *n = mouse_name(mb->get_button_index());
	if (!*n) return {};
	const bool shift = mb->is_shift_pressed(), ctrl = mb->is_ctrl_pressed(), alt = mb->is_alt_pressed(),
			   meta = mb->is_meta_pressed();
	const std::string primary = n;
	return table().on_press(primary, [&](const std::string &k) {
		if (k == primary) return true;
		if (k == "shift") return shift;
		if (k == "ctrl") return ctrl;
		if (k == "alt") return alt;
		if (k == "super") return meta;
		return is_down_now(k);
	});
}

inline bool has(const std::vector<std::string> &names, const char *name) {
	for (const auto &n : names)
		if (n == name) return true;
	return false;
}

inline bool held(const char *name) { return table().is_held(name, is_down_now); }
inline bool held_exclusive(const char *name) { return table().is_held_exclusive(name, is_down_now); }

inline const char *kUserCfgPath = "user://config/user.cfg";

// Loads user.cfg's hotkey lines over the defaults (0 A.D. keeps user hotkeys
// in config/user.cfg too). Returns false when there is no user.cfg.
inline bool load_user_cfg() {
	using namespace godot;
	if (!FileAccess::file_exists(kUserCfgPath)) return false;
	Ref<FileAccess> f = FileAccess::open(kUserCfgPath, FileAccess::READ);
	if (f.is_null()) return false;
	table().load_cfg(f->get_as_text().utf8().get_data());
	return true;
}

// Writes every binding that differs from the defaults to user.cfg, after the
// options page's settings (user_config.h), which share the file.
inline bool save_user_cfg() {
	using namespace godot;
	HotkeyTable base;
	base.load_cfg(default_cfg());
	DirAccess::make_dir_recursive_absolute("user://config");
	Ref<FileAccess> f = FileAccess::open(kUserCfgPath, FileAccess::WRITE);
	if (f.is_null()) return false;
	f->store_string(String::utf8((user_config::settings_cfg() + table().diff_cfg(base)).c_str()));
	return true;
}

} // namespace hotkeys
} // namespace rtso

#endif
