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
	// MatchSettingsDialog.xml: "50%-300 24%-70 50%+300 76%-70" (ModernDialog), the title
	// "50%-128 -16 50%+128 16" on the top edge, the state line "20 40 100%-20 80", the
	// description in a gold-edged dark box from the title's (or the state line's) bottom to
	// 100%-60, and a 128 px StoneButton Close at "50%-64 100%-50 50%+64 100%-22".
	void build(Node *parent, Object *owner, const char *close_method) {
		panel_ = memnew(Panel);
		panel_->set_anchor(SIDE_LEFT, 0.5f);
		panel_->set_anchor(SIDE_RIGHT, 0.5f);
		panel_->set_anchor(SIDE_TOP, 0.24f);
		panel_->set_anchor(SIDE_BOTTOM, 0.76f);
		panel_->set_offset(SIDE_LEFT, -300.0f);
		panel_->set_offset(SIDE_RIGHT, 300.0f);
		panel_->set_offset(SIDE_TOP, -70.0f);
		panel_->set_offset(SIDE_BOTTOM, -70.0f);
		Ref<StyleBoxFlat> st;
		st.instantiate();
		st->set_bg_color(Color(0.08f, 0.09f, 0.12f, 0.96f));
		st->set_border_width_all(2);
		st->set_border_color(Color(0.55f, 0.5f, 0.42f));
		st->set_corner_radius_all(4);
		panel_->add_theme_stylebox_override("panel", st);

		Label *title = memnew(Label);
		title->set_text(title->tr("Match Settings"));
		title->set_anchor(SIDE_LEFT, 0.5f);
		title->set_anchor(SIDE_RIGHT, 0.5f);
		title->set_offset(SIDE_LEFT, -128.0f);
		title->set_offset(SIDE_RIGHT, 128.0f);
		title->set_offset(SIDE_TOP, -16.0f);
		title->set_offset(SIDE_BOTTOM, 16.0f);
		title->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		title->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
		title->add_theme_font_size_override("font_size", 18);
		title->add_theme_color_override("font_color", Color(1.0f, 0.88f, 0.4f));
		panel_->add_child(title);

		// matchSettingsPlayerstate: shown only once the player has won or lost.
		state_ = memnew(Label);
		state_->set_anchor(SIDE_RIGHT, 1.0f);
		state_->set_offset(SIDE_LEFT, 20.0f);
		state_->set_offset(SIDE_RIGHT, -20.0f);
		state_->set_offset(SIDE_TOP, 40.0f);
		state_->set_offset(SIDE_BOTTOM, 80.0f);
		state_->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
		state_->add_theme_font_size_override("font_size", 18);
		state_->set_visible(false);
		panel_->add_child(state_);

		// gameDescription (ModernDarkBoxGold) holding gameDescriptionText.
		box_ = memnew(Panel);
		box_->set_anchor(SIDE_RIGHT, 1.0f);
		box_->set_anchor(SIDE_BOTTOM, 1.0f);
		box_->set_offset(SIDE_LEFT, 20.0f);
		box_->set_offset(SIDE_RIGHT, -20.0f);
		box_->set_offset(SIDE_TOP, 16.0f);
		box_->set_offset(SIDE_BOTTOM, -60.0f);
		Ref<StyleBoxFlat> bs;
		bs.instantiate();
		bs->set_bg_color(Color(0.0f, 0.0f, 0.0f, 0.6f));
		bs->set_border_width_all(1);
		bs->set_border_color(Color(0.90f, 0.75f, 0.31f));
		box_->add_theme_stylebox_override("panel", bs);
		panel_->add_child(box_);
		text_ = memnew(RichTextLabel);
		text_->set_use_bbcode(true);
		text_->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
		text_->set_offset(SIDE_LEFT, 6.0f);
		text_->set_offset(SIDE_TOP, 6.0f);
		text_->set_offset(SIDE_RIGHT, -6.0f);
		text_->set_offset(SIDE_BOTTOM, -6.0f);
		text_->add_theme_font_size_override("normal_font_size", 13);
		text_->add_theme_font_size_override("bold_font_size", 13);
		box_->add_child(text_);

		Button *close = memnew(Button);
		close->set_text(close->tr("Close"));
		close->set_anchor(SIDE_LEFT, 0.5f);
		close->set_anchor(SIDE_RIGHT, 0.5f);
		close->set_anchor(SIDE_TOP, 1.0f);
		close->set_anchor(SIDE_BOTTOM, 1.0f);
		close->set_offset(SIDE_LEFT, -64.0f);
		close->set_offset(SIDE_RIGHT, 64.0f);
		close->set_offset(SIDE_TOP, -50.0f);
		close->set_offset(SIDE_BOTTOM, -22.0f);
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
		// MatchSettingsDialog.rebuild: the box starts under the title, or under the state line.
		box_->set_offset(SIDE_TOP, player_state.is_empty() ? 16.0f : 80.0f);
	}
	String text() const { return text_ ? text_->get_text() : String(); }
	String player_state() const { return state_ ? state_->get_text() : String(); }

private:
	Panel *panel_ = nullptr;
	Label *state_ = nullptr;
	Panel *box_ = nullptr;
	RichTextLabel *text_ = nullptr;
};

} // namespace rtso

#endif
