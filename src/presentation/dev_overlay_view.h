#ifndef RTSO_DEV_OVERLAY_VIEW_H
#define RTSO_DEV_OVERLAY_VIEW_H

// 0 A.D. 0.28 gui/session/developer_overlay (GPL-2.0-or-later): the panel the
// session.devcommands.toggle hotkey (Alt+D) opens on the right, a row per
// developer command with its label and tick box (DeveloperOverlay.xml,
// DeveloperOverlayControlCheckbox.js), and DeveloperOverlayEntityState's yellow
// text on the left with the state of the first selected entity. RTS0 ports the
// commands it has something behind, in 0.28's order: Change perspective, Display
// selection state, the pathfinder, hierarchical pathfinder, obstruction, unit motion, range and
// bounding box overlays
// (debug_overlays.h), Restrict camera, Reveal map, Enable time warp, Activate Rejoin
// Test and Promote selected units; then DeveloperOverlayControlDropDowns' Render Debug Mode
// dropdown, a row of its own under the boxes.

#include <godot_cpp/classes/check_box.hpp>
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/callable.hpp>

#include "safe_select.h"

namespace rtso {

class RtsoDevOverlay {
public:
	enum Command {
		DEV_CHANGE_PERSPECTIVE,
		DEV_SELECTION_STATE,
		DEV_PATHFINDER_OVERLAY,
		DEV_HIER_PATHFINDER_OVERLAY,
		DEV_OBSTRUCTION_OVERLAY,
		DEV_UNIT_MOTION_OVERLAY,
		DEV_RANGE_OVERLAY,
		DEV_BOUNDING_BOX_OVERLAY,
		DEV_RESTRICT_CAMERA,
		DEV_REVEAL_MAP,
		DEV_TIME_WARP,
		DEV_REJOIN_TEST,
		DEV_PROMOTE,
		DEV_COUNT
	};
	static constexpr const char *kLabels[DEV_COUNT] = {
		"Change perspective", "Display selection state", "Pathfinder overlay", "Hierarchical pathfinder overlay", "Obstruction overlay", "Unit motion overlay",
		"Range overlay", "Bounding box overlay", "Restrict camera", "Reveal map", "Enable time warp", "Activate Rejoin Test",
		"Promote selected units",
	};
	static constexpr float kRow = 24.0f;
	// RenderDebugMode values(): RENDER_DEBUG_MODE_NONE, _AO, _ALPHA, _CUSTOM.
	enum RenderDebugMode { RENDER_DEBUG_NONE, RENDER_DEBUG_AO, RENDER_DEBUG_ALPHA, RENDER_DEBUG_CUSTOM, RENDER_DEBUG_COUNT };
	static constexpr const char *kRenderDebugLabels[RENDER_DEBUG_COUNT] = {
		"Render Debug Mode Disabled", "Render Debug Mode AO", "Render Debug Mode Alpha", "Render Debug Mode Custom",
	};

	// parent: the HUD layer; owner gets `method`(command) when a box is pressed and
	// `dropdown_method`(index) when a Render Debug Mode is picked.
	void build(godot::Node *parent, godot::Object *owner, const char *method, const char *dropdown_method) {
		using namespace godot;
		// DeveloperOverlay.xml size="100%-250 50%-280 100%-8 50%", sessionOverlayBackground.
		panel_ = memnew(ColorRect);
		panel_->set_name("DevCommandsOverlay");
		panel_->set_color(Color(0.0f, 0.0f, 0.0f, 185.0f / 255.0f)); // sessionOverlayBackground "0 0 0 185"
		panel_->set_anchor(SIDE_LEFT, 1.0f);
		panel_->set_anchor(SIDE_RIGHT, 1.0f);
		panel_->set_anchor(SIDE_TOP, 0.5f);
		panel_->set_anchor(SIDE_BOTTOM, 0.5f);
		panel_->set_offset(SIDE_LEFT, -250.0f);
		panel_->set_offset(SIDE_RIGHT, -8.0f);
		panel_->set_offset(SIDE_TOP, -280.0f);
		panel_->set_offset(SIDE_BOTTOM, -280.0f + (DEV_COUNT + 1) * kRow); // DeveloperOverlay.resize: boxes and dropdown
		panel_->set_visible(false);
		parent->add_child(panel_);
		for (int i = 0; i < DEV_COUNT; ++i) {
			Label *l = memnew(Label); // devCommandsText
			l->set_text(kLabels[i]);
			l->set_position(Vector2(0, i * kRow)); // "0 0 100%-26 100%", sans-12, text_align right
			l->set_size(Vector2(250 - 8 - 30, kRow));
			l->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
			l->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
			l->add_theme_font_size_override("font_size", 12);
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
		// dev_command_dropdown: "0 0 100% 100%" of the row after the boxes, devCommandsDropDown.
		render_debug_ = memnew(RtsoSafeSelect); // an OptionButton's popup Window is unsafe here (safe_select.h)
		render_debug_->set_name("RenderDebugMode");
		for (int i = 0; i < RENDER_DEBUG_COUNT; ++i) render_debug_->add_item(kRenderDebugLabels[i], i);
		render_debug_->set_position(Vector2(0, DEV_COUNT * kRow));
		render_debug_->set_size(Vector2(250 - 8, kRow));
		render_debug_->add_theme_font_size_override("font_size", 13);
		render_debug_->select(RENDER_DEBUG_NONE);
		render_debug_->connect("item_selected", Callable(owner, dropdown_method));
		panel_->add_child(render_debug_);
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
		// mono-stroke-10: DejaVu Sans Mono, as 0.28's mono fonts.
		godot::Ref<godot::Font> mono = godot::ResourceLoader::get_singleton()->load("res://assets/fonts/DejaVuSansMono.ttf");
		if (mono.is_valid()) state_->add_theme_font_override("font", mono);
		state_->set_autowrap_mode(TextServer::AUTOWRAP_ARBITRARY);
		state_->set_clip_text(true);
		state_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		state_->set_visible(false);
		parent->add_child(state_);
	}

	bool is_open() const { return panel_ && panel_->is_visible(); }
	// The handler's enabled(): a box that can't be pressed.
	void set_enabled(int c, bool on) {
		if (c >= 0 && c < DEV_COUNT && boxes_[c] && boxes_[c]->is_disabled() == on) boxes_[c]->set_disabled(!on);
	}
	void set_open(bool open) {
		if (panel_) panel_->set_visible(open);
	}
	bool checked(int c) const { return c >= 0 && c < DEV_COUNT && boxes_[c] && boxes_[c]->is_pressed(); }
	// The `checked` handlers, synchronised while the panel is open.
	void set_checked(int c, bool on) {
		if (c >= 0 && c < DEV_COUNT && boxes_[c] && boxes_[c]->is_pressed() != on) boxes_[c]->set_pressed_no_signal(on);
	}
	// RenderDebugMode.selected, synchronised while the panel is open.
	void set_render_debug(int mode) {
		if (render_debug_ && render_debug_->get_selected() != mode) render_debug_->select(mode);
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
	RtsoSafeSelect *render_debug_ = nullptr;
};

} // namespace rtso

#endif
