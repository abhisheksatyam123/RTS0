#ifndef RTSO_MAIN_NODE_H
#define RTSO_MAIN_NODE_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/panel.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
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
#include <godot_cpp/classes/audio_stream_player.hpp>
#include <godot_cpp/classes/time.hpp>

#include "../simulation/simulation.h"
#include "../animation/animation_controller.h"
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
	AudioStreamPlayer *music_player = nullptr;

	// UI Panels & Labels
	Panel *top_hud_panel = nullptr;
	Label *top_hud_label = nullptr;

	Panel *inspector_panel = nullptr;
	Label *inspector_title = nullptr;
	Label *inspector_info = nullptr;
	Label *inspector_actions = nullptr;

	Panel *banner_panel = nullptr;
	Label *banner_label = nullptr;

	MeshInstance3D *ghost_mesh = nullptr;
	Ref<StandardMaterial3D> ghost_mat_ok;
	Ref<StandardMaterial3D> ghost_mat_bad;

	bool build_mode = false;
	uint8_t current_build_type = rtso::BLD_HOUSE;
	uint32_t selected_building_id = 0;

	bool dragging = false;
	bool paused_via_menu = false;
	Vector2 drag_start;
	PackedInt32Array selected_ids;

	double accumulator = 0.0;
	rtso::AnimationController anim_ctrl;

protected:
	static void _bind_methods();

public:
	RtsoMain();
	~RtsoMain();

	void _ready() override;
	void _process(double delta) override;
	void _unhandled_input(const Ref<InputEvent> &event) override;

	void _on_minimap_clicked(Vector2 world_pos);

private:
	Vector3 get_ground_click_pos(Vector2 mouse_pos) const;
	std::pair<int, int> mouse_cell(Vector2 mouse_pos) const;
	void clear_selection();
	void update_hud();
	void enter_build_mode(bool enable, uint8_t btype = rtso::BLD_HOUSE);
	void update_ghost_mesh();
	void try_train_unit(rtso::UnitType utype);
};

#endif
