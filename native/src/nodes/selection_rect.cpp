#include "selection_rect.h"
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void RtsoSelectionRect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_rect", "start", "end", "active"), &RtsoSelectionRect::set_rect);
}

RtsoSelectionRect::RtsoSelectionRect() {
	set_mouse_filter(MOUSE_FILTER_IGNORE);
}

RtsoSelectionRect::~RtsoSelectionRect() {}

void RtsoSelectionRect::set_rect(Vector2 start, Vector2 end, bool active) {
	start_pos = start;
	end_pos = end;
	dragging = active;
	queue_redraw();
}

void RtsoSelectionRect::_draw() {
	if (!dragging) return;
	Vector2 min_pos(std::min(start_pos.x, end_pos.x), std::min(start_pos.y, end_pos.y));
	Vector2 size(std::abs(end_pos.x - start_pos.x), std::abs(end_pos.y - start_pos.y));
	Rect2 rect(min_pos, size);
	draw_rect(rect, Color(0.2f, 0.8f, 0.2f, 0.2f), true);
	draw_rect(rect, Color(0.2f, 0.9f, 0.2f, 0.8f), false, 2.0f);
}
