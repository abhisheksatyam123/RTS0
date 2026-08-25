#ifndef RTSO_SELECTION_RECT_H
#define RTSO_SELECTION_RECT_H

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/rect2.hpp>

using namespace godot;

class RtsoSelectionRect : public Control {
	GDCLASS(RtsoSelectionRect, Control);

	Vector2 start_pos;
	Vector2 end_pos;
	bool dragging = false;

protected:
	static void _bind_methods();

public:
	RtsoSelectionRect();
	~RtsoSelectionRect();

	void _draw() override;

	void set_rect(Vector2 start, Vector2 end, bool active);
};

#endif
