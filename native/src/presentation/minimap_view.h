#ifndef RTSO_MINIMAP_VIEW_H
#define RTSO_MINIMAP_VIEW_H

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/color.hpp>
#include "../simulation/simulation.h"

using namespace godot;

class RtsoMiniMapView : public Control {
	GDCLASS(RtsoMiniMapView, Control);

	const rtso::Simulation *sim = nullptr;
	Vector3 cam_pos;
	bool dragging = false;

protected:
	static void _bind_methods();

public:
	RtsoMiniMapView();
	~RtsoMiniMapView();

	void _draw() override;
	void _gui_input(const Ref<InputEvent> &event) override;
	void sync(const rtso::Simulation *s, Vector3 p_cam_pos);
};

#endif
