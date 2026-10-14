#ifndef RTSO_DEV_OVERLAY_VIEW_H
#define RTSO_DEV_OVERLAY_VIEW_H

// 0 A.D. 0.28 gui/session/developer_overlay (GPL-2.0-or-later): the panel the
// session.devcommands.toggle hotkey (Alt+D) opens on the right, a row per
// developer command with its label and tick box (DeveloperOverlay.xml,
// DeveloperOverlayControlCheckbox.js), and DeveloperOverlayEntityState's yellow
// text on the left with the state of the first selected entity. RTS0 ports the
// commands it has something behind: Display selection state, Reveal map,
// Restrict camera and Promote selected units.

#include <godot_cpp/classes/check_box.hpp>
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/callable.hpp>

namespace rtso {

class RtsoDevOverlay {
public:
	enum Command { DEV_SELECTION_STATE, DEV_REVEAL_MAP, DEV_RESTRICT_CAMERA, DEV_PROMOTE, DEV_COUNT };
	static constexpr float kRow = 24.0f;

	// parent: the HUD layer; owner gets `method`(command, checked) when a box is pressed.
	void build(godot::Node *parent, godot::Object *owner, const char *method) {
		using namespace godot;
		// DeveloperOverlay.xml size="100%-250 50%-280 100%-8 50%", sessionOverlayBackground.
		panel_ = memnew(ColorRect);
		panel_->set_name("DevCommandsOverlay");
		panel_->set_color(Color(0.0f, 0.0f, 0.0f, 0.6f));
		panel_->set_anchor(SIDE_LEFT, 1.0f);
		panel_->set_anchor(SIDE_RIGHT, 1.0f);
		panel_->set_anchor(SIDE_TOP, 0.5f);
		panel_->set_anchor(SIDE_BOTTOM, 0.5f);
		panel_->set_offset(SIDE_LEFT, -250.0f);
		panel_->set_offset(SIDE_RIGHT, -8.0f);
		panel_->set_offset(SIDE_TOP, -280.0f);
		panel_->set_offset(SIDE_BOTTOM, -280.0f + DEV_COUNT * kRow); // DeveloperOverlay.resize
		panel_->set_visible(false);
		parent->add_child(panel_);
		static const char *kLabels[DEV_COUNT] = {
			"Display selection state", "Reveal map", "Restrict camera", "Promote selected units",
		};
		for (int i = 0; i < DEV_COUNT; ++i) {
			Label *l = memnew(Label); // devCommandsText
			l->set_text(kLabels[i]);
			l->set_position(Vector2(4, i * kRow));
			l->set_size(Vector2(250 - 8 - 30, kRow));
			l->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
			l->add_theme_font_size_override("font_size", 13);
			l->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			panel_->add_child(l);
			CheckBox *cb = memnew(CheckBox); // ModernTickBox, "100%-24 1 100% 100%"
			cb->set_name(String("DevCommand") + String::num_int64(i));
			cb->set_position(Vector2(250 - 8 - 26, i * kRow + 1));
			cb->set_size(Vector2(24, kRow - 1));
			cb->connect("pressed", Callable(owner, method).bind(i));
			panel_->add_child(cb);
			boxes_[i] = cb;
		}
		// DeveloperOverlayEntityState.xml: "0 70 80% 100%", yellow mono, ghost.
		state_ = memnew(Label);
		state_->set_name("DeveloperOverlayEntityState");
		state_->set_anchor(SIDE_RIGHT, 0.8f);
		state_->set_anchor(SIDE_BOTTOM, 1.0f);
		state_->set_offset(SIDE_LEFT, 0.0f);
		state_->set_offset(SIDE_TOP, 70.0f);
		state_->add_theme_color_override("font_color", Color(1, 1, 0));
		state_->add_theme_color_override("font_outline_color", Color(0, 0, 0));
		state_->add_theme_constant_override("outline_size", 3);
		state_->add_theme_font_size_override("font_size", 10);
		state_->set_autowrap_mode(TextServer::AUTOWRAP_ARBITRARY);
		state_->set_clip_text(true);
		state_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		state_->set_visible(false);
		parent->add_child(state_);
	}

	bool is_open() const { return panel_ && panel_->is_visible(); }
	void set_open(bool open) {
		if (panel_) panel_->set_visible(open);
	}
	bool checked(int c) const { return c >= 0 && c < DEV_COUNT && boxes_[c] && boxes_[c]->is_pressed(); }
	// The `checked` handlers, synchronised while the panel is open.
	void set_checked(int c, bool on) {
		if (c >= 0 && c < DEV_COUNT && boxes_[c] && boxes_[c]->is_pressed() != on) boxes_[c]->set_pressed_no_signal(on);
	}
	void show_state(bool on) {
		if (state_) state_->set_visible(on);
	}
	bool state_shown() const { return state_ && state_->is_visible(); }
	void set_state_text(const godot::String &t) {
		if (state_ && state_->get_text() != t) state_->set_text(t);
	}
	godot::String state_text() const { return state_ ? state_->get_text() : godot::String(); }

private:
	godot::ColorRect *panel_ = nullptr;
	godot::CheckBox *boxes_[DEV_COUNT] = {};
	godot::Label *state_ = nullptr;
};

} // namespace rtso

#endif
