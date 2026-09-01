#ifndef RTSO_ASSET_VIEWER_H
#define RTSO_ASSET_VIEWER_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/animation_player.hpp>
#include <godot_cpp/classes/option_button.hpp>
#include <godot_cpp/classes/item_list.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/check_box.hpp>
#include <godot_cpp/classes/h_slider.hpp>
#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <vector>

using namespace godot;

class RtsoAssetViewer : public Node3D {
	GDCLASS(RtsoAssetViewer, Node3D);

	Camera3D *cam = nullptr;
	Node3D *preview_root = nullptr;
	Node *preview_scene = nullptr;
	Node *site_foundation = nullptr;
	Node *site_scaffold = nullptr;
	Node *site_rubble = nullptr;
	MeshInstance3D *crowd_mi = nullptr;
	MeshInstance3D *gizmo_mi = nullptr;
	AnimationPlayer *anim = nullptr;

	OptionButton *cat_box = nullptr;
	OptionButton *civ_box = nullptr;
	ItemList *item_list = nullptr;
	ItemList *clip_list = nullptr;
	OptionButton *clip_box = nullptr;
	Label *now_playing = nullptr;
	Label *diag = nullptr;
	CheckBox *chk_full = nullptr;
	CheckBox *chk_crowd = nullptr;
	CheckBox *chk_loop = nullptr;
	HSlider *speed_sl = nullptr;
	HSlider *seek_sl = nullptr;
	Label *status = nullptr;

	String cur_cat = "units";
	String cur_id;
	String cur_clip;
	PackedStringArray clip_names;
	int clip_index = -1;
	float orbit_yaw = 0.6f;
	float orbit_pitch = 0.45f;
	float orbit_dist = 8.0f;
	Vector3 orbit_target = Vector3(0, 0.9f, 0);
	bool dragging = false;
	bool seeking = false;

	struct CatalogItem {
		String cat;
		String id;
		String label;
	};
	std::vector<CatalogItem> items;

protected:
	static void _bind_methods();

public:
	RtsoAssetViewer();
	~RtsoAssetViewer();

	void _ready() override;
	void _process(double delta) override;
	void _unhandled_input(const Ref<InputEvent> &event) override;

	void _on_cat_selected(int index);
	void _on_civ_selected(int index);
	void _on_item_selected(int index);
	void _on_clip_selected(int index);
	void _on_clip_box_selected(int index);
	void _on_clip_clicked(int index, Vector2 at_position, int mouse_button_index);
	void _on_clip_prev();
	void _on_clip_next();
	void _on_play();
	void _on_pause();
	void _on_rest();
	void _on_back();
	void _on_toggle_view(bool _pressed);
	void _on_speed_changed(float v);
	void _on_seek_started();
	void _on_seek_changed(float v);

private:
	void build_ui();
	void build_stage();
	void rebuild_item_list();
	void load_preview();
	void release_preview();
	bool anim_ok() const;
	void apply_visibility();
	void apply_building_clip_visuals();
	void refresh_clips();
	void play_clip_at(int index);
	void refresh_now_playing();
	void refresh_diag();
	void frame_camera();
	void update_camera();
	bool pointer_over_ui() const;
	static AnimationPlayer *find_anim(Node *n);
	static int count_meshes(Node *n);
};

#endif
