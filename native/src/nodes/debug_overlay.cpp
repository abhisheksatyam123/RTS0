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
	label->set_position(Vector2(10, 10));
	add_child(label);
}

void RtsoDebugOverlay::update_info(const String &text) {
	if (label) label->set_text(text);
}
