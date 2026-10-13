#ifndef RTSO_TUTORIAL_PANEL_H
#define RTSO_TUTORIAL_PANEL_H

// In-game tutorial panel (0 A.D. gui/session/session.xml "tutorialPanel" with
// gui/session/messages.js displayTutorial / onTutorialReady): the current goal's
// instructions, the warning line, and a Ready button while the goal waits on it.
// The last goal's button reads "Leave". Toggled with session.gui.tutorial.toggle.

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/panel.hpp>
#include <godot_cpp/classes/rich_text_label.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/string.hpp>

#include <string>

#include "../simulation/simulation.h"
#include "hotkeys.h"

using namespace godot;

namespace rtso {

// A tutorial line with "%(hotkey)s" filled by the player's binding.
inline std::string tutorial_line_text(const TutorialLine &l) {
	std::string s = l.text ? l.text : "";
	if (!l.hotkey) return s;
	const std::string tag = "%(hotkey)s";
	const size_t at = s.find(tag);
	if (at == std::string::npos) return s;
	std::string key = hotkeys::table().label(l.hotkey);
	if (key.empty()) key = "(unbound)";
	return s.replace(at, tag.size(), "[color=#ffd966]" + key + "[/color]");
}

// The panel's text for the sim's current goal ("" when no tutorial runs).
inline std::string tutorial_panel_text(const Simulation &sim) {
	const TutorialGoal *g = tutorial_goal(sim.tutorial_id(), sim.tutorial_goal_index());
	if (!g) return "";
	std::string out;
	for (const TutorialLine &l : g->lines) {
		if (!l.text) break;
		if (!out.empty()) out += "\n";
		out += tutorial_line_text(l);
	}
	const uint8_t w = sim.tutorial_warning();
	if (w != TUTORIAL_WARN_NONE && w < TUTORIAL_WARN_COUNT)
		out += "\n[color=#ff8040]" + tutorial_line_text(tutorial_warning(w)) + "[/color]";
	return out;
}

class RtsoTutorialPanel {
public:
	void build(Node *parent, Object *owner, const char *ready_method) {
		panel_ = memnew(Panel);
		panel_->set_anchor(SIDE_LEFT, 0.5f);
		panel_->set_anchor(SIDE_RIGHT, 0.5f);
		panel_->set_offset(SIDE_LEFT, -310.0f);
		panel_->set_offset(SIDE_RIGHT, 310.0f);
		panel_->set_offset(SIDE_TOP, 40.0f);
		panel_->set_offset(SIDE_BOTTOM, 230.0f);
		panel_->set_mouse_filter(Control::MOUSE_FILTER_STOP);
		Ref<StyleBoxFlat> st;
		st.instantiate();
		st->set_bg_color(Color(0.06f, 0.07f, 0.10f, 0.88f));
		st->set_border_width_all(1);
		st->set_border_color(Color(0.90f, 0.75f, 0.31f));
		st->set_corner_radius_all(4);
		panel_->add_theme_stylebox_override("panel", st);

		text_ = memnew(RichTextLabel);
		text_->set_use_bbcode(true);
		text_->set_position(Vector2(12, 8));
		text_->set_size(Vector2(596, 140));
		text_->add_theme_font_size_override("normal_font_size", 13);
		panel_->add_child(text_);

		ready_ = memnew(Button);
		ready_->set_text("Click to Proceed");
		ready_->set_position(Vector2(240, 150));
		ready_->set_size(Vector2(140, 30));
		ready_->connect("pressed", Callable(owner, ready_method));
		panel_->add_child(ready_);

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
			return;
		}
		const std::string t = tutorial_panel_text(*sim);
		if (t != last_) {
			last_ = t;
			text_->set_text(String::utf8(t.c_str()));
		}
		ready_->set_visible(sim->tutorial_ready_button());
		ready_->set_text(on_last_goal(*sim) ? "Leave" : "Click to Proceed");
	}

private:
	Panel *panel_ = nullptr;
	RichTextLabel *text_ = nullptr;
	Button *ready_ = nullptr;
	std::string last_;
	bool hidden_ = false;
};

} // namespace rtso

#endif
