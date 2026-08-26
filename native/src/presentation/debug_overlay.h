#ifndef RTSO_DEBUG_OVERLAY_H
#define RTSO_DEBUG_OVERLAY_H

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/label.hpp>

using namespace godot;

class RtsoDebugOverlay : public Control {
	GDCLASS(RtsoDebugOverlay, Control);

	Label *label = nullptr;

protected:
	static void _bind_methods();

public:
	RtsoDebugOverlay();
	~RtsoDebugOverlay();

	void _ready() override;
	void _notification(int p_what);
	void update_info(const String &text);
};

#endif
