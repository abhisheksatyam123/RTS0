#ifndef RTSO_OBJECTIVES_DIALOG_H
#define RTSO_OBJECTIVES_DIALOG_H

// In-game objectives page (0 A.D. gui/session/objectives/ObjectivesDialog.js,
// hotkey session.gui.objectives.toggle = Ctrl+O): the player's state and the
// match rules, worded like gui/common/gamedescription.js getGameDescription()
// with the victory condition texts of simulation/data/settings/victory_conditions/*.json.

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/panel.hpp>
#include <godot_cpp/classes/rich_text_label.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cmath>
#include <string>

#include "../simulation/simulation.h"

using namespace godot;

namespace rtso {

// "[b]Label:[/b] value" lines for the match, from player `pid`'s seat.
inline std::string objectives_text(const Simulation &sim, uint8_t pid) {
	std::string out;
	auto line = [&](const std::string &label, const std::string &value) {
		out += "[b]" + label + ":[/b] " + value + "\n";
	};
	auto minutes = [](float s) { return (int)std::lround(s / 60.0f); };
	auto plural_min = [](int m) { return std::to_string(m) + (m == 1 ? " minute" : " minutes"); };

	if (sim.is_defeated(pid)) out += "[color=#ff6644]You have been defeated.[/color]\n\n";
	else if (sim.is_victory_for(pid)) out += "[color=#66ff66]You have won![/color]\n\n";

	switch (sim.victory_condition()) {
		case VICTORY_ENDLESS:
			line("Endless Game", "No winner will be determined, even if everyone is defeated.");
			break;
		case VICTORY_CONQUEST_UNITS:
			line("Conquest Units", "Defeat opponents by killing all their units.");
			break;
		case VICTORY_CONQUEST_STRUCTURES:
			line("Conquest Structures", "Defeat opponents by destroying all their structures.");
			break;
		case VICTORY_REGICIDE:
			line("Regicide", "Defeat opponents by killing their hero.");
			break;
		case VICTORY_WONDER:
			line("Conquest", "Defeat opponents by killing all their units and destroying all their structures.");
			line("Wonder (" + plural_min(minutes(sim.wonder_duration())) + ")",
					"Be the first to build or capture a Wonder and keep it for a certain time to win the game. The timer will be reset when the Wonder is destroyed or captured.");
			break;
		case VICTORY_RELIC:
			line("Conquest", "Defeat opponents by killing all their units and destroying all their structures.");
			line("Capture the Relic (" + plural_min(minutes(sim.relic_duration())) + ")",
					"Capture all relics spread across the map and keep them for a certain time to win the game.");
			line("Relic Count", std::to_string(sim.relics().size()));
			break;
		default:
			line("Conquest", "Defeat opponents by killing all their units and destroying all their structures.");
			break;
	}
	line("Diplomacy", "Players can make alliances and declare war on allies.");
	if (sim.last_man_standing())
		line("Last Man Standing", "Only one player can win the game. If the remaining players are allies, the game continues until only one remains.");
	else
		line("Allied Victory", "If one player wins, his or her allies win too. If one group of allies remains, they win.");
	const int cf = minutes(sim.ceasefire_seconds());
	line("Ceasefire", cf == 0 ? "disabled"
			: cf == 1 ? "For the first minute, other players will stay neutral."
			: "For the first " + std::to_string(cf) + " minutes, other players will stay neutral.");
	if (sim.ceasefire_active())
		out += "    (" + std::to_string((int)std::ceil(sim.ceasefire_remaining())) + " s of ceasefire left)\n";
	if (sim.world_pop_cap() > 0 && !sim.player_pop_cap(pid))
		line("World Population", std::to_string(sim.world_pop_cap()) + " shared by the players still in the game (" +
				std::to_string(sim.pop_cap()) + " each)");
	else
		line("Population Limit", sim.pop_cap(pid) > 0 ? std::to_string(sim.pop_cap(pid)) : "Unlimited");
	if (sim.reveal_map()) line("Revealed Map", "Map will be completely visible.");
	else if (sim.explore_map()) line("Explored Map", "The map is explored from the start.");
	return out;
}

class RtsoObjectivesDialog {
public:
	void build(Node *parent, Object *owner, const char *close_method) {
		panel_ = memnew(Panel);
		panel_->set_anchor(SIDE_LEFT, 0.5f);
		panel_->set_anchor(SIDE_RIGHT, 0.5f);
		panel_->set_anchor(SIDE_TOP, 0.5f);
		panel_->set_anchor(SIDE_BOTTOM, 0.5f);
		panel_->set_offset(SIDE_LEFT, -300.0f);
		panel_->set_offset(SIDE_RIGHT, 300.0f);
		panel_->set_offset(SIDE_TOP, -220.0f);
		panel_->set_offset(SIDE_BOTTOM, 220.0f);
		Ref<StyleBoxFlat> st;
		st.instantiate();
		st->set_bg_color(Color(0.08f, 0.09f, 0.12f, 0.96f));
		st->set_border_width_all(2);
		st->set_border_color(Color(0.90f, 0.75f, 0.31f));
		st->set_corner_radius_all(4);
		panel_->add_theme_stylebox_override("panel", st);

		Label *title = memnew(Label);
		title->set_text("Objectives");
		title->set_position(Vector2(0, 10));
		title->set_size(Vector2(600, 24));
		title->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		title->add_theme_font_size_override("font_size", 18);
		title->add_theme_color_override("font_color", Color(1.0f, 0.88f, 0.4f));
		panel_->add_child(title);

		text_ = memnew(RichTextLabel);
		text_->set_use_bbcode(true);
		text_->set_position(Vector2(20, 44));
		text_->set_size(Vector2(560, 340));
		text_->add_theme_font_size_override("normal_font_size", 13);
		text_->add_theme_font_size_override("bold_font_size", 13);
		panel_->add_child(text_);

		Button *close = memnew(Button);
		close->set_text("Close");
		close->set_position(Vector2(250, 396));
		close->set_size(Vector2(100, 30));
		close->connect("pressed", Callable(owner, close_method));
		panel_->add_child(close);

		panel_->set_visible(false);
		parent->add_child(panel_);
	}

	bool is_open() const { return panel_ && panel_->is_visible(); }
	void close() {
		if (panel_) panel_->set_visible(false);
	}
	void toggle(const Simulation *sim, uint8_t pid) {
		if (!panel_) return;
		if (is_open()) {
			close();
			return;
		}
		refresh(sim, pid);
		panel_->set_visible(true);
	}
	// Called each frame while open, so the ceasefire countdown stays current.
	void refresh(const Simulation *sim, uint8_t pid) {
		if (!text_ || !sim) return;
		const std::string t = objectives_text(*sim, pid);
		if (t == last_) return;
		last_ = t;
		text_->set_text(String::utf8(t.c_str()));
	}

private:
	Panel *panel_ = nullptr;
	RichTextLabel *text_ = nullptr;
	std::string last_;
};

} // namespace rtso

#endif
