#include "debug_overlay.h"
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void RtsoDebugOverlay::_bind_methods() {
	ClassDB::bind_method(D_METHOD("update_info", "text"), &RtsoDebugOverlay::update_info);
}

RtsoDebugOverlay::RtsoDebugOverlay() {
	set_mouse_filter(MOUSE_FILTER_IGNORE);
}

RtsoDebugOverlay::~RtsoDebugOverlay() {}

void RtsoDebugOverlay::_ready() {
	label = memnew(Label);
	// Bottom-left anchored so it never overlaps the top HUD (Wave/Gold)
	label->set_position(Vector2(10, 690));
	label->add_theme_color_override("font_color", Color(0.85f, 0.9f, 1.0f, 0.9f));
	add_child(label);
}

void RtsoDebugOverlay::_notification(int p_what) {
	if (p_what == NOTIFICATION_RESIZED) {
		if (label && get_viewport()) {
			Vector2 vp = get_viewport_rect().size;
			label->set_position(Vector2(10, vp.y - 28));
		}
	}
}

void RtsoDebugOverlay::update_info(const String &text) {
	if (label) label->set_text(text);
}
