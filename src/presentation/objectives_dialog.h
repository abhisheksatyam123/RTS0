#ifndef RTSO_OBJECTIVES_DIALOG_H
#define RTSO_OBJECTIVES_DIALOG_H

// In-game Match Settings dialog (0 A.D. 0.28 gui/session/match_settings/MatchSettingsDialog.js,
// hotkey session.gui.matchsettings.toggle; RTS0's older session.gui.objectives.toggle
// still opens it): the player's state ("You have won!" / "You have been defeated!")
// over the match's getGameDescription text, which scenes/session/game_description.gd
// builds and the owner passes in.

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/panel.hpp>
#include <godot_cpp/classes/rich_text_label.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/string.hpp>

using namespace godot;

namespace rtso {

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
		panel_->set_offset(SIDE_TOP, -260.0f);
		panel_->set_offset(SIDE_BOTTOM, 260.0f);
		Ref<StyleBoxFlat> st;
		st.instantiate();
		st->set_bg_color(Color(0.08f, 0.09f, 0.12f, 0.96f));
		st->set_border_width_all(2);
		st->set_border_color(Color(0.90f, 0.75f, 0.31f));
		st->set_corner_radius_all(4);
		panel_->add_theme_stylebox_override("panel", st);

		Label *title = memnew(Label);
		title->set_text("Match Settings");
		title->set_position(Vector2(0, 10));
		title->set_size(Vector2(600, 24));
		title->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		title->add_theme_font_size_override("font_size", 18);
		title->add_theme_color_override("font_color", Color(1.0f, 0.88f, 0.4f));
		panel_->add_child(title);

		// matchSettingsPlayerstate: shown only once the player has won or lost.
		state_ = memnew(Label);
		state_->set_position(Vector2(20, 40));
		state_->set_size(Vector2(560, 40));
		state_->add_theme_font_size_override("font_size", 18);
		state_->set_visible(false);
		panel_->add_child(state_);

		text_ = memnew(RichTextLabel);
		text_->set_use_bbcode(true);
		text_->set_position(Vector2(20, 44));
		text_->set_size(Vector2(560, 420));
		text_->add_theme_font_size_override("normal_font_size", 13);
		text_->add_theme_font_size_override("bold_font_size", 13);
		panel_->add_child(text_);

		Button *close = memnew(Button);
		close->set_text("Close");
		close->set_position(Vector2(250, 476));
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
	// MatchSettingsDialog.toggle; the description is fixed for the match.
	void open(const String &description, const String &player_state) {
		if (!panel_) return;
		text_->set_text(description);
		set_player_state(player_state);
		panel_->set_visible(true);
	}
	// MatchSettingsDialog.rebuild: the state line, with the description below it.
	void set_player_state(const String &player_state) {
		if (!state_ || player_state == state_->get_text()) return;
		state_->set_text(player_state);
		state_->set_visible(!player_state.is_empty());
		const float top = player_state.is_empty() ? 44.0f : 84.0f;
		text_->set_position(Vector2(20, top));
		text_->set_size(Vector2(560, 464.0f - top));
	}
	String text() const { return text_ ? text_->get_text() : String(); }
	String player_state() const { return state_ ? state_->get_text() : String(); }

private:
	Panel *panel_ = nullptr;
	Label *state_ = nullptr;
	RichTextLabel *text_ = nullptr;
};

} // namespace rtso

#endif
