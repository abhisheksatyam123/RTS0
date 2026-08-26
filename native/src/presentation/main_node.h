#ifndef RTSO_MAIN_NODE_H
#define RTSO_MAIN_NODE_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/world_environment.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/time.hpp>

#include "../simulation/simulation.h"
#include "world_view_node.h"
#include "camera_rig.h"
#include "selection_rect.h"
#include "debug_overlay.h"
#include "minimap_view.h"

using namespace godot;

class RtsoMain : public Node3D {
	GDCLASS(RtsoMain, Node3D);

	rtso::Simulation *sim = nullptr;
	RtsoWorldView *view = nullptr;
	RtsoCameraRig *cam_rig = nullptr;
	RtsoSelectionRect *sel_rect = nullptr;
	RtsoDebugOverlay *debug_overlay = nullptr;
	RtsoMiniMapView *minimap_view = nullptr;

	Label *hud_label = nullptr;
	MeshInstance3D *ghost_mesh = nullptr;
	Ref<StandardMaterial3D> ghost_mat_ok;
	Ref<StandardMaterial3D> ghost_mat_bad;

	bool build_mode = false;
	bool dragging = false;
	Vector2 drag_start;
	PackedInt32Array selected_ids;

	double accumulator = 0.0;

protected:
	static void _bind_methods();

public:
	RtsoMain();
	~RtsoMain();

	void _ready() override;
	void _process(double delta) override;
	void _unhandled_input(const Ref<InputEvent> &event) override;

private:
	Vector3 get_ground_click_pos(Vector2 mouse_pos) const;
	std::pair<int, int> mouse_cell(Vector2 mouse_pos) const;
	void clear_selection();
	void update_hud();
	void enter_build_mode(bool enable);
};

#endif
