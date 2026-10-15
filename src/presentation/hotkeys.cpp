// Port of 0 A.D. source/ps/Hotkey.cpp matching and source/ps/ConfigDB.cpp's cfg
// parsing for [hotkey.*] sections (0 A.D. 61a3b95). See hotkeys.h.

#include "hotkeys.h"
#include "hotkey_defaults.h"

#include <algorithm>
#include <cctype>

namespace rtso {
namespace hotkeys {

namespace {

std::string lower(std::string s) {
	for (char &c : s) c = (char)std::tolower((unsigned char)c);
	return s;
}

std::string trim(const std::string &s) {
	size_t a = 0, b = s.size();
	while (a < b && std::isspace((unsigned char)s[a])) ++a;
	while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
	return s.substr(a, b - a);
}

// keys.txt aliases. Single-character names map to their word form.
const std::unordered_map<std::string, std::string> &aliases() {
	static const std::unordered_map<std::string, std::string> m = {
		{"enter", "return"}, {"esc", "escape"}, {"del", "delete"},
		{"right", "rightarrow"}, {"left", "leftarrow"}, {"up", "uparrow"}, {"down", "downarrow"},
		{"-", "minus"}, {"=", "plus"}, {"equals", "plus"}, {"[", "leftbracket"}, {"]", "rightbracket"},
		{"\\", "backslash"}, {"\\\\", "backslash"}, {";", "semicolon"}, {"'", "quote"},
		{"`", "backquote"}, {",", "comma"}, {".", "period"}, {"/", "slash"},
		{"keypad /", "numdivide"}, {"keypad *", "nummultiply"}, {"keypad -", "numminus"},
		{"keypad =", "numequals"}, {"keypad enter", "numenter"}, {"keypad .", "numdecimal"},
		{"keypad +", "numplus"},
		{"left ctrl", "ctrl"}, {"right ctrl", "ctrl"}, {"leftctrl", "ctrl"}, {"rightctrl", "ctrl"}, {"control", "ctrl"},
		{"left shift", "shift"}, {"right shift", "shift"}, {"leftshift", "shift"}, {"rightshift", "shift"},
		{"left alt", "alt"}, {"right alt", "alt"}, {"leftalt", "alt"}, {"rightalt", "alt"},
		{"left gui", "super"}, {"right gui", "super"}, {"win", "super"}, {"meta", "super"}, {"cmd", "super"},
		{"prior", "pageup"}, {"next", "pagedown"}, {"break", "pause"},
	};
	return m;
}

bool is_modifier(const std::string &k) {
	return k == "shift" || k == "ctrl" || k == "alt" || k == "super";
}

// Splits a cfg value list: Pause, "Shift+Space" -> {Pause, Shift+Space}.
std::vector<std::string> split_values(const std::string &v) {
	std::vector<std::string> out;
	std::string cur;
	bool quoted = false, any = false;
	for (char c : v) {
		if (c == '"') {
			quoted = !quoted;
			any = true;
		} else if (c == ',' && !quoted) {
			out.push_back(trim(cur));
			cur.clear();
			any = false;
		} else {
			cur.push_back(c);
			any = true;
		}
	}
	if (any || !cur.empty()) out.push_back(trim(cur));
	return out;
}

// Drops a ';' comment outside quotes.
std::string strip_comment(const std::string &line) {
	bool quoted = false;
	for (size_t i = 0; i < line.size(); ++i) {
		if (line[i] == '"') quoted = !quoted;
		else if (line[i] == ';' && !quoted) return line.substr(0, i);
	}
	return line;
}

} // namespace

std::string canonical_key(const std::string &name) {
	std::string k = lower(trim(name));
	auto it = aliases().find(k);
	if (it != aliases().end()) return it->second;
	if (k.rfind("keypad ", 0) == 0 && k.size() == 8 && std::isdigit((unsigned char)k[7]))
		return std::string("num") + k[7];
	return k;
}

Combo parse_combo(const std::string &text) {
	Combo c;
	const std::string t = trim(text);
	if (t.empty() || lower(t) == "unused") return c;
	// '+' separates keys; a lone '+' is not a key name in keys.txt ("Plus" is).
	size_t start = 0;
	while (start <= t.size()) {
		size_t plus = t.find('+', start);
		std::string part = t.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
		part = canonical_key(part);
		if (!part.empty()) c.keys.push_back(part);
		if (plus == std::string::npos) break;
		start = plus + 1;
	}
	// Hotkey.cpp treats the last listed key as the trigger; keep modifiers
	// first so "F5+Ctrl" behaves like "Ctrl+F5".
	std::stable_partition(c.keys.begin(), c.keys.end(), [](const std::string &k) { return is_modifier(k); });
	return c;
}

std::string combo_to_string(const Combo &c) {
	static const std::unordered_map<std::string, std::string> pretty = {
		{"ctrl", "Ctrl"}, {"shift", "Shift"}, {"alt", "Alt"}, {"super", "Super"},
		{"return", "Return"}, {"escape", "Escape"}, {"space", "Space"}, {"tab", "Tab"},
		{"backspace", "Backspace"}, {"delete", "Delete"}, {"pause", "Pause"},
		{"leftarrow", "LeftArrow"}, {"rightarrow", "RightArrow"}, {"uparrow", "UpArrow"}, {"downarrow", "DownArrow"},
		{"leftbracket", "LeftBracket"}, {"rightbracket", "RightBracket"}, {"backslash", "BackSlash"},
		{"semicolon", "Semicolon"}, {"backquote", "BackQuote"}, {"comma", "Comma"}, {"period", "Period"},
		{"slash", "Slash"}, {"minus", "Minus"}, {"plus", "Plus"}, {"pageup", "PageUp"}, {"pagedown", "PageDown"},
		{"mouseleft", "MouseLeft"}, {"mouseright", "MouseRight"}, {"mousemiddle", "MouseMiddle"},
		{"mousex1", "MouseX1"}, {"mousex2", "MouseX2"}, {"wheelup", "WheelUp"}, {"wheeldown", "WheelDown"},
	};
	std::string out;
	for (size_t i = 0; i < c.keys.size(); ++i) {
		if (i) out += "+";
		auto it = pretty.find(c.keys[i]);
		if (it != pretty.end()) {
			out += it->second;
		} else if (c.keys[i].rfind("num", 0) == 0 && c.keys[i].size() > 3) {
			std::string rest = c.keys[i].substr(3);
			rest[0] = (char)std::toupper((unsigned char)rest[0]);
			out += "Num" + rest;
		} else {
			std::string k = c.keys[i];
			k[0] = (char)std::toupper((unsigned char)k[0]);
			out += k;
		}
	}
	return out;
}

std::string combo_display(const Combo &c) {
	static const std::unordered_map<std::string, std::string> names = {
		{"uparrow", "\xe2\x86\x91"}, {"downarrow", "\xe2\x86\x93"}, {"leftarrow", "\xe2\x86\x90"},
		{"rightarrow", "\xe2\x86\x92"}, {"backslash", "Backslash"}, {"plus", "Equals"}, {"quote", "SingleQuote"},
		{"mousex1", "WheelLeft"}, {"mousex2", "WheelRight"},
	};
	static const char *const kMods[] = {"alt", "ctrl", "shift", "super"};
	std::vector<std::string> parts;
	for (const char *m : kMods)
		if (std::find(c.keys.begin(), c.keys.end(), m) != c.keys.end()) parts.push_back(combo_to_string(Combo{{m}}));
	for (const std::string &k : c.keys) {
		if (is_modifier(k)) continue;
		auto it = names.find(k);
		parts.push_back(it != names.end() ? it->second : combo_to_string(Combo{{k}}));
	}
	std::string out;
	for (size_t i = 0; i < parts.size(); ++i) out += (i ? "+" : "") + parts[i];
	return out;
}

std::string combos_display(const std::vector<Combo> &combos) {
	std::vector<std::string> v;
	for (const Combo &c : combos)
		if (!c.keys.empty()) v.push_back(combo_display(c));
	std::stable_sort(v.begin(), v.end(), [](const std::string &a, const std::string &b) { return a.size() < b.size(); });
	std::string out;
	for (size_t i = 0; i < v.size(); ++i) out += (i ? ", " : "") + v[i];
	return out;
}

int HotkeyTable::load_cfg(const std::string &text) {
	int set_count = 0;
	std::string section;
	size_t pos = 0;
	while (pos <= text.size()) {
		size_t nl = text.find('\n', pos);
		std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
		pos = nl == std::string::npos ? text.size() + 1 : nl + 1;
		line = trim(strip_comment(line));
		if (line.empty()) continue;
		if (line.front() == '[') {
			size_t close = line.find(']');
			section = lower(trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1)));
			continue;
		}
		size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		std::string key = lower(trim(line.substr(0, eq)));
		// "=" as a value (keys.txt "=, Plus") would split here; 0 A.D. never binds it.
		std::string full = section.empty() ? key : section + "." + key;
		if (full.rfind("hotkey.", 0) != 0) continue;
		std::string name = full.substr(7);
		std::vector<Combo> combos;
		for (const std::string &v : split_values(line.substr(eq + 1))) {
			Combo c = parse_combo(v);
			if (!c.keys.empty()) combos.push_back(std::move(c));
		}
		bindings_[name] = std::move(combos);
		++set_count;
	}
	rebuild_index();
	return set_count;
}

void HotkeyTable::set(const std::string &name, const std::vector<Combo> &combos) {
	bindings_[lower(name)] = combos;
	rebuild_index();
}

void HotkeyTable::rebuild_index() {
	by_primary_.clear();
	for (const auto &kv : bindings_)
		for (const Combo &c : kv.second)
			if (!c.keys.empty()) by_primary_[c.primary()].push_back({kv.first, c});
}

const std::vector<Combo> *HotkeyTable::get(const std::string &name) const {
	// Names load lower-cased; callers may spell them as default.cfg does (session.attackmoveUnit).
	auto it = bindings_.find(name);
	if (it == bindings_.end()) it = bindings_.find(lower(name));
	return it == bindings_.end() ? nullptr : &it->second;
}

std::vector<std::string> HotkeyTable::names() const {
	std::vector<std::string> out;
	out.reserve(bindings_.size());
	for (const auto &kv : bindings_) out.push_back(kv.first);
	std::sort(out.begin(), out.end());
	return out;
}

std::vector<std::string> HotkeyTable::on_press(const std::string &primary, const IsDown &is_down) const {
	std::vector<std::string> fired;
	auto it = by_primary_.find(canonical_key(primary));
	if (it == by_primary_.end()) return fired;
	size_t best = 0;
	for (const Entry &e : it->second) {
		bool accept = true;
		for (size_t k = 0; k + 1 < e.combo.keys.size() && accept; ++k)
			accept = is_down(e.combo.keys[k]);
		if (!accept) continue;
		const size_t spec = e.combo.keys.size();
		if (spec < best) continue;
		if (spec > best) {
			fired.clear();
			best = spec;
		}
		if (std::find(fired.begin(), fired.end(), e.name) == fired.end()) fired.push_back(e.name);
	}
	std::sort(fired.begin(), fired.end());
	return fired;
}

bool HotkeyTable::is_held(const std::string &name, const IsDown &is_down) const {
	const auto *combos = get(name);
	if (!combos) return false;
	for (const Combo &c : *combos) {
		bool all = !c.keys.empty();
		for (const std::string &k : c.keys)
			if (!(all = is_down(k))) break;
		if (all) return true;
	}
	return false;
}

bool HotkeyTable::is_held_exclusive(const std::string &name, const IsDown &is_down) const {
	const auto *combos = get(name);
	if (!combos) return false;
	for (const Combo &c : *combos) {
		bool all = !c.keys.empty();
		for (const std::string &k : c.keys)
			if (!(all = is_down(k))) break;
		if (!all) continue;
		bool superseded = false;
		auto it = by_primary_.find(c.primary());
		if (it != by_primary_.end()) {
			for (const Entry &e : it->second) {
				if (e.combo.keys.size() <= c.keys.size() || e.name == name) continue;
				bool held = true;
				for (const std::string &k : e.combo.keys)
					if (!(held = is_down(k))) break;
				if (held) {
					superseded = true;
					break;
				}
			}
		}
		if (!superseded) return true;
	}
	return false;
}

std::string HotkeyTable::diff_cfg(const HotkeyTable &base) const {
	std::string out;
	for (const std::string &name : names()) {
		const auto &mine = bindings_.at(name);
		const auto *theirs = base.get(name);
		bool same = theirs && theirs->size() == mine.size();
		for (size_t i = 0; same && i < mine.size(); ++i) same = (*theirs)[i].keys == mine[i].keys;
		if (same) continue;
		out += "hotkey." + name + " = ";
		if (mine.empty()) out += "\"\"";
		for (size_t i = 0; i < mine.size(); ++i) {
			if (i) out += ", ";
			out += "\"" + combo_to_string(mine[i]) + "\"";
		}
		out += "\n";
	}
	return out;
}

std::vector<std::string> HotkeyTable::bound_to(const Combo &combo) const {
	std::vector<std::string> out;
	if (combo.keys.empty()) return out;
	std::vector<std::string> want = combo.keys;
	std::sort(want.begin(), want.end());
	auto it = by_primary_.find(combo.primary());
	if (it == by_primary_.end()) return out;
	for (const Entry &e : it->second) {
		std::vector<std::string> keys = e.combo.keys;
		std::sort(keys.begin(), keys.end());
		if (keys == want && std::find(out.begin(), out.end(), e.name) == out.end()) out.push_back(e.name);
	}
	std::sort(out.begin(), out.end());
	return out;
}

std::string HotkeyTable::label(const std::string &name) const {
	const auto *c = get(name);
	return c ? combos_display(*c) : std::string();
}

const char *default_cfg() { return detail::kDefaultCfg; }

HotkeyTable &table() {
	static HotkeyTable t = [] {
		HotkeyTable d;
		d.load_cfg(default_cfg());
		return d;
	}();
	return t;
}

} // namespace hotkeys
} // namespace rtso
