#ifndef RTSO_CAMERA_RIG_H
#define RTSO_CAMERA_RIG_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/variant/vector3.hpp>

using namespace godot;

class RtsoCameraRig : public Node3D {
	GDCLASS(RtsoCameraRig, Node3D);

	Camera3D *cam = nullptr;
	Vector3 target_pos;
	float speed = 28.0f;
	float zoom_height = 22.0f;
	float target_zoom = 22.0f;
	float yaw_angle = 0.0f;
	float target_yaw = 0.0f;
	float map_bounds_half = 64.0f;
	bool mmb_dragging = false;
	bool edge_scroll_enabled = true;
	// Camera pitch in degrees below the horizon (0 A.D. camera.rotate.up/down).
	float pitch_deg = 50.6f;
	float target_pitch_deg = 50.6f;
	// Which keys may scroll and rotate the camera: every key, arrows only (while
	// placing a building, when letters pick the building), or none (typing).
	int key_scroll_mode_ = 0;
	// Unit the camera follows (0 A.D. camera.follow); 0 = none. Any manual pan stops it.
	uint32_t follow_id_ = 0;

protected:
	static void _bind_methods();

public:
	RtsoCameraRig();
	~RtsoCameraRig();

	void _ready() override;
	void _process(double delta) override;
	void _unhandled_input(const Ref<InputEvent> &event) override;

	Camera3D *get_camera() const { return cam; }
	Vector3 get_target_pos() const { return target_pos; }
	void set_target_pos(Vector3 pos);
	void zoom(float delta_zoom);
	void reset_camera();
	void set_map_bounds_half(float h) { map_bounds_half = h; }
	bool is_edge_scroll_enabled() const { return edge_scroll_enabled; }
	enum KeyScrollMode { KEYS_ALL = 0, KEYS_ARROWS_ONLY = 1, KEYS_NONE = 2 };
	void set_key_scroll_mode(int mode) { key_scroll_mode_ = mode; }
	void set_follow_id(uint32_t id) { follow_id_ = id; }
	uint32_t follow_id() const { return follow_id_; }
	// Keeps the followed unit centred; called every frame by the main node.
	void follow_to(Vector3 pos);
	void set_edge_scroll_enabled(bool enabled) { edge_scroll_enabled = enabled; }
};

#endif
