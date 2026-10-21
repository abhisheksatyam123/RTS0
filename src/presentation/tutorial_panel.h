#ifndef RTSO_TUTORIAL_PANEL_H
#define RTSO_TUTORIAL_PANEL_H

// In-game tutorial panel (0 A.D. gui/session/session.xml "tutorialPanel" with
// gui/session/messages.js updateTutorial / onTutorialReady): every instruction so far, the
// newest in 255 226 149 (g_TutorialNewMessageTags), the status line ("Follow the
// instructions.", "Click when ready.", "Click to quit this tutorial." or the orange
// warning), and a Ready button while the goal waits on it ("Quit" on the last goal).
// Toggled with session.gui.tutorial.toggle.

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/panel.hpp>
#include <godot_cpp/classes/rich_text_label.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <string>

#include "../simulation/simulation.h"
#include "hotkeys.h"
#include "l10n.h"
#include "session_skin.h"

using namespace godot;

namespace rtso {

// A tutorial line, translated (messages.js translate / colorizeHotkey(translate(text))),
// with "%(hotkey)s" filled by the player's binding.
inline std::string tutorial_line_text(const TutorialLine &l) {
	std::string s;
	if (l.text) {
		const String en = String::utf8(l.text);
		String t = tr0(en);
		// One 0.28 msgid reads "%(hotkey)" without its "s" (the walkthrough's two-Civilians
		// reminder); RTS0's line has the "s", so look the translation up under 0.28's spelling.
		if (t == en && en.contains("%(hotkey)s")) {
			const String alt = en.replace("%(hotkey)s", "%(hotkey)");
			const String ta = tr0(alt);
			if (ta != alt) t = ta.replace("%(hotkey)s", "%(hotkey)").replace("%(hotkey)", "%(hotkey)s");
		}
		s = t.utf8().get_data();
	}
	if (!l.hotkey) return s;
	const std::string tag = "%(hotkey)s";
	std::string key = hotkeys::table().label(l.hotkey);
	if (key.empty()) key = "(unbound)";
	key = "[color=#ffd966]" + key + "[/color]";
	// sprintf fills every "%(hotkey)s" of the line.
	for (size_t at = s.find(tag); at != std::string::npos; at = s.find(tag, at + key.size())) s.replace(at, tag.size(), key);
	return s;
}

// A goal's instructions ("" past the last goal).
inline std::string tutorial_goal_text(uint8_t tutorial, int goal) {
	const TutorialGoal *g = tutorial_goal(tutorial, goal);
	if (!g) return "";
	std::string out;
	for (const TutorialLine &l : g->lines) {
		if (!l.text) break;
		out += tutorial_line_text(l); // the instructions concatenate (messages.js reduce)
	}
	return out;
}

// The panel's text for the sim's current goal: the earlier goals, then the newest in
// 255 226 149 ("" when no tutorial runs).
inline std::string tutorial_panel_text(const Simulation &sim) {
	const int goal = sim.tutorial_goal_index();
	if (!tutorial_goal(sim.tutorial_id(), goal)) return "";
	std::string out;
	for (int i = 0; i < goal; ++i) out += tutorial_goal_text(sim.tutorial_id(), i) + "\n";
	return out + "[color=#ffe295]" + tutorial_goal_text(sim.tutorial_id(), goal) + "[/color]";
}

class RtsoTutorialPanel {
public:
	void build(Node *parent, Object *owner, const char *ready_method) {
		// tutorial_panel.xml: a ModernFade at "50%-380 38 50%+380 292", the text in a
		// TutorialPanel box at "10 10 100%-20 100%-48", the warning (ModernLeftLabelText) at
		// "10 100%-42 100%-20 100%-12" and a ModernButtonRed Ready at
		// "100%-160 100%-42 100%-20 100%-12".
		panel_ = memnew(Panel);
		panel_->set_name("TutorialPanel");
		panel_->set_theme(session_skin());
		panel_->set_theme_type_variation("ModernFade");
		place(panel_, 0.5f, 0, 0.5f, 0, -380, 38, 380, 292);
		panel_->set_mouse_filter(Control::MOUSE_FILTER_STOP);

		text_ = memnew(RichTextLabel);
		text_->set_theme_type_variation("TutorialPanel");
		text_->set_use_bbcode(true);
		text_->set_scroll_follow(true); // scroll_bottom: the newest instruction stays in view
		panel_->add_child(text_);
		place(text_, 0, 0, 1, 1, 10, 10, -20, -48);

		status_ = memnew(RichTextLabel); // tutorialWarning
		status_->set_use_bbcode(true);
		status_->set_scroll_active(false);
		status_->add_theme_font_size_override("normal_font_size", 14);
		status_->add_theme_color_override("default_color", Color(1, 1, 1));
		status_->add_theme_color_override("font_outline_color", Color(0, 0, 0));
		status_->add_theme_constant_override("outline_size", 3); // sans-bold-stroke-14
		panel_->add_child(status_);
		place(status_, 0, 1, 1, 1, 10, -36, -20, -12);

		ready_ = memnew(Button);
		ready_->set_theme(modern_skin()); // ModernButtonRed
		ready_->set_text("Ready");
		ready_->connect("pressed", Callable(owner, ready_method));
		panel_->add_child(ready_);
		place(ready_, 1, 1, 1, 1, -160, -42, -20, -12);

		panel_->set_visible(false);
		parent->add_child(panel_);
	}

	void toggle() { hidden_ = !hidden_; }

	// The last goal's button leaves the tutorial instead of advancing.
	bool on_last_goal(const Simulation &sim) const {
		return sim.tutorial_goal_index() + 1 >= tutorial_goal_count(sim.tutorial_id());
	}

	// Called every frame.
	void refresh(const Simulation *sim) {
		if (!panel_) return;
		const bool running = sim && sim->tutorial_id() != TUTORIAL_NONE && sim->tutorial_goal_index() >= 0;
		panel_->set_visible(running && !hidden_);
		if (!running) {
			last_.clear();
			last_key_ = String();
			last_shown_ = -1;
			return;
		}
		// The text is rebuilt only for a new goal or language (not every frame).
		const String key = String::num_int64(sim->tutorial_id() * 1000 + sim->tutorial_goal_index()) + ":" +
				TranslationServer::get_singleton()->get_locale();
		if (key != last_key_) {
			last_key_ = key;
			const std::string t = tutorial_panel_text(*sim);
			if (t != last_) {
				last_ = t;
				text_->set_text(String::utf8(t.c_str()));
			}
		}
		const bool ready = sim->tutorial_ready_button(), leave = on_last_goal(*sim);
		const uint8_t w = sim->tutorial_warning();
		// updateTutorial: a new goal or warning shows the panel again, even if toggled off.
		const int shown = sim->tutorial_goal_index() * 256 + w;
		if (shown != last_shown_) {
			if (last_shown_ >= 0) hidden_ = false;
			last_shown_ = shown;
		}
		panel_->set_visible(!hidden_);
		ready_->set_visible(ready);
		ready_->set_text(tr0(leave ? "Quit" : "Ready"));
		const std::string status = w != TUTORIAL_WARN_NONE && w < TUTORIAL_WARN_COUNT
				? "[color=orange]" + tutorial_line_text(tutorial_warning(w)) + "[/color]"
				: std::string(tr0(!ready ? "Follow the instructions."
				: leave ? "Click to quit this tutorial." : "Click when ready.").utf8().get_data());
		if (status != last_status_) {
			last_status_ = status;
			status_->set_text(String::utf8(status.c_str()));
		}
	}

	// Tests: the panel as shown.
	Dictionary debug_state() const {
		Dictionary d;
		d["visible"] = panel_ && panel_->is_visible();
		d["text"] = text_ ? text_->get_text() : String();
		d["status"] = status_ ? status_->get_parsed_text() : String();
		d["button"] = ready_ ? ready_->get_text() : String();
		d["button_visible"] = ready_ && ready_->is_visible();
		return d;
	}

private:
	static void place(Control *c, float al, float at, float ar, float ab, float l, float t, float r, float b) {
		c->set_anchor(SIDE_LEFT, al);
		c->set_anchor(SIDE_TOP, at);
		c->set_anchor(SIDE_RIGHT, ar);
		c->set_anchor(SIDE_BOTTOM, ab);
		c->set_offset(SIDE_LEFT, l);
		c->set_offset(SIDE_TOP, t);
		c->set_offset(SIDE_RIGHT, r);
		c->set_offset(SIDE_BOTTOM, b);
	}
	Panel *panel_ = nullptr;
	RichTextLabel *text_ = nullptr;
	RichTextLabel *status_ = nullptr;
	Button *ready_ = nullptr;
	std::string last_, last_status_;
	int last_shown_ = -1; // goal * 256 + warning last shown
	String last_key_;      // tutorial, goal and locale of the text shown
	bool hidden_ = false;
};

} // namespace rtso

#endif
