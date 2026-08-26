#ifndef RTSO_MINIMAP_VIEW_H
#define RTSO_MINIMAP_VIEW_H

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/color.hpp>
#include "../simulation/simulation.h"

using namespace godot;

class RtsoMiniMapView : public Control {
	GDCLASS(RtsoMiniMapView, Control);

	const rtso::Simulation *sim = nullptr;

protected:
	static void _bind_methods();

public:
	RtsoMiniMapView();
	~RtsoMiniMapView();

	void _draw() override;
	void sync(const rtso::Simulation *s);
};

#endif
