// Hotkey table tests: 0 A.D. default.cfg parsing and Hotkey.cpp matching rules
// (src/presentation/hotkeys.*). Build:
//   g++ -std=c++20 -O2 -Isrc tools/test_hotkeys.cpp src/presentation/hotkeys.cpp -o /tmp/test_hotkeys

#include "presentation/hotkeys.h"

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <set>
#include <string>

using namespace rtso::hotkeys;

static int g_fail = 0;
#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                    \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			++g_fail;                                                     \
		}                                                                 \
	} while (0)

static IsDown held(std::initializer_list<const char *> keys) {
	std::set<std::string> s;
	for (const char *k : keys) s.insert(canonical_key(k));
	return [s](const std::string &k) { return s.count(k) > 0; };
}

static bool fires(const HotkeyTable &t, const char *primary, std::initializer_list<const char *> down, const char *name) {
	std::set<std::string> s;
	for (const char *k : down) s.insert(canonical_key(k));
	s.insert(canonical_key(primary));
	auto f = t.on_press(primary, [s](const std::string &k) { return s.count(k) > 0; });
	for (const auto &n : f)
		if (n == name) return true;
	return false;
}

int main() {
	// Key names follow keys.txt aliases.
	CHECK(canonical_key("Esc") == "escape");
	CHECK(canonical_key("Enter") == "return");
	CHECK(canonical_key("Right Ctrl") == "ctrl");
	CHECK(canonical_key("[") == "leftbracket");
	CHECK(canonical_key("Keypad 5") == "num5");
	CHECK(parse_combo("Ctrl+F5").keys == (std::vector<std::string>{"ctrl", "f5"}));
	CHECK(parse_combo("F5+Ctrl").keys == (std::vector<std::string>{"ctrl", "f5"}));
	CHECK(parse_combo("").keys.empty());
	CHECK(combo_to_string(parse_combo("shift+f5")) == "Shift+F5");

	const HotkeyTable &t = table();
	// 0 A.D. default.cfg bindings loaded.
	CHECK(t.label("quicksave") == "Shift+F5");
	CHECK(t.label("quickload") == "Shift+F8");
	CHECK(t.label("session.stop") == "H");
	CHECK(t.label("camera.jump.set.1") == "Ctrl+F5");
	CHECK(t.label("session.gui.diplomacy.toggle") == "Ctrl+H");
	CHECK(t.label("session.queueunit.8") == "Comma");
	CHECK(t.get("selection.group.select.10") && t.get("selection.group.select.10")->size() == 2);
	CHECK(t.get("exit") && t.get("exit")->empty());
	CHECK(t.names().size() > 120);

	// Specificity: Ctrl+F5 sets a bookmark, F5 alone jumps, Shift+F5 quicksaves.
	CHECK(fires(t, "F5", {}, "camera.jump.1"));
	CHECK(fires(t, "F5", {"Ctrl"}, "camera.jump.set.1"));
	CHECK(!fires(t, "F5", {"Ctrl"}, "camera.jump.1"));
	CHECK(fires(t, "F5", {"Shift"}, "quicksave"));
	CHECK(!fires(t, "F5", {"Shift"}, "camera.jump.1"));
	// Extra held keys don't block a hotkey (Hotkey.cpp spec).
	CHECK(fires(t, "H", {"Alt"}, "session.stop"));
	CHECK(fires(t, "1", {"Shift"}, "selection.group.add.1"));
	CHECK(fires(t, "1", {}, "selection.group.select.1"));
	CHECK(fires(t, "0", {"Ctrl"}, "selection.group.save.10"));
	// RTS0's placement keys are unbound by default (0.28 has none): B is the 5th trainee only.
	CHECK(!fires(t, "B", {}, "rts0.build.menu") && fires(t, "B", {}, "session.queueunit.5"));
	CHECK(fires(t, "B", {"Ctrl"}, "session.gui.barter.toggle") && !fires(t, "B", {"Ctrl"}, "session.queueunit.5"));
	CHECK(!fires(t, "M", {}, "rts0.build.market") && t.get("rts0.build.market") && t.get("rts0.build.market")->empty());
	CHECK(fires(t, "Escape", {}, "cancel"));
	CHECK(fires(t, "Space", {"Shift"}, "pause"));

	// Modifier hotkeys are held queries; camera.left yields to Ctrl+A (rotate).
	CHECK(t.is_held("session.queue", held({"Shift"})));
	CHECK(t.is_held("session.attackmove", held({"Ctrl"})));
	// default.cfg's mixed-case names (attackmoveUnit) load lower-cased; lookups take either spelling.
	CHECK(t.is_held("session.attackmoveUnit", held({"Ctrl", "Q"})) && t.is_held("session.attackmoveunit", held({"Ctrl", "Q"})));
	CHECK(!t.is_held("session.attackmoveUnit", held({"Ctrl"})));
	CHECK(t.is_held_exclusive("camera.left", held({"A"})));
	CHECK(!t.is_held_exclusive("camera.left", held({"A", "Ctrl"})));
	CHECK(t.is_held_exclusive("camera.rotate.cw", held({"A", "Ctrl"})));
	CHECK(t.is_held_exclusive("camera.left", held({"LeftArrow"})));

	// 0.28: F is the attack and focus-fire modifier, the arrows also page items.
	CHECK(t.label("session.attack") == "F" && t.label("session.focusfire") == "F");
	CHECK(t.get("item.next")->size() == 1 && combo_to_string(t.get("item.next")->front()) == "RightArrow");
	// Labels as 0.28 shows them (KeyName.cpp, hotkeys.js): arrows as glyphs, every combination
	// shortest first, modifiers in Alt, Ctrl, Shift order.
	CHECK(t.label("item.next") == "\xe2\x86\x92" && t.label("item.prev") == "\xe2\x86\x90");
	CHECK(t.label("pause") == "Pause, Shift+Space");
	CHECK(combo_display(parse_combo("Shift+Ctrl+Plus")) == "Ctrl+Shift+Equals");
	CHECK(combo_display(parse_combo("BackSlash")) == "Backslash" && combo_display(parse_combo("MouseX1")) == "WheelLeft");
	CHECK(t.get("camera.follow")->empty() && t.get("session.showstatusbars")->empty());
	CHECK(t.label("session.gui.matchsettings.toggle") == "Ctrl+O");

	// user.cfg overrides replace a whole binding list, and diff_cfg writes them back.
	HotkeyTable user = t;
	CHECK(user.load_cfg("hotkey.session.stop = \"Ctrl+X\"\n[hotkey.camera]\nreset = \"\" ; unbind\n") == 2);
	CHECK(user.label("session.stop") == "Ctrl+X");
	CHECK(user.get("camera.reset")->empty());
	CHECK(!fires(user, "H", {}, "session.stop"));
	const std::string diff = user.diff_cfg(t);
	CHECK(diff == "hotkey.camera.reset = \"\"\nhotkey.session.stop = \"Ctrl+X\"\n");
	HotkeyTable again = t;
	again.load_cfg(diff);
	CHECK(again.diff_cfg(t) == diff);

	// Conflicts (0 A.D. GetConflicts): every hotkey on the same key set.
	const auto b = t.bound_to(parse_combo("B"));
	CHECK(std::find(b.begin(), b.end(), "rts0.build.menu") == b.end());
	CHECK(std::find(b.begin(), b.end(), "session.queueunit.5") != b.end());
	CHECK(t.bound_to(parse_combo("Ctrl+F5")) == std::vector<std::string>{"camera.jump.set.1"});
	CHECK(t.bound_to(parse_combo("F5+Ctrl")) == std::vector<std::string>{"camera.jump.set.1"});

	if (g_fail) {
		std::printf("test_hotkeys: %d failure(s)\n", g_fail);
		return 1;
	}
	std::printf("test_hotkeys: all checks passed (%zu hotkeys)\n", t.names().size());
	return 0;
}
