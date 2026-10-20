#ifndef RTSO_CAMERA_RIG_H
#define RTSO_CAMERA_RIG_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/dictionary.hpp>

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
	// 0 A.D. camera.togglebirdseyeview: straight down, and back to the pitch before.
	bool birds_eye_ = false;
	bool constrain_ = true;
	float pre_birds_eye_pitch_ = 50.6f;
	// Which keys may scroll and rotate the camera: every key, arrows only (while
	// placing a building, when letters pick the building), or none (typing).
	int key_scroll_mode_ = 0;
	// Unit the camera follows (0 A.D. camera.follow); 0 = none. Any manual pan stops it.
	uint32_t follow_id_ = 0;
	// A cinema path flies the camera (cinema_view.h): the rig leaves it alone and ignores input.
	bool cinema_view_ = false;
	// 0 A.D. camera.{scroll,rotate,zoom}.speed.{increase,decrease}: the keyboard
	// (and edge) scroll, rotate and zoom speeds, as factors of RTS0's defaults.
	Vector3 speed_mul_ = Vector3(1, 1, 1);

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
	void toggle_birds_eye();
	bool is_birds_eye() const { return birds_eye_; }
	float get_pitch_deg() const { return target_pitch_deg; }
	float get_zoom() const { return target_zoom; }
	void set_map_bounds_half(float h) { map_bounds_half = h; }
	// The developer overlay's Restrict camera (GameView_SetConstrainCameraEnabled):
	// off, the camera may leave the map.
	void set_constrain_camera(bool on) { constrain_ = on; }
	bool is_constrain_camera() const { return constrain_; }
	float bounds_half() const { return constrain_ ? map_bounds_half : 1.0e6f; }
	bool is_edge_scroll_enabled() const { return edge_scroll_enabled; }
	enum KeyScrollMode { KEYS_ALL = 0, KEYS_ARROWS_ONLY = 1, KEYS_NONE = 2 };
	void set_key_scroll_mode(int mode) { key_scroll_mode_ = mode; }
	void set_follow_id(uint32_t id) { follow_id_ = id; }
	uint32_t follow_id() const { return follow_id_; }
	// Keeps the followed unit centred; called every frame by the main node.
	void follow_to(Vector3 pos);
	void set_edge_scroll_enabled(bool enabled) { edge_scroll_enabled = enabled; }
	void set_cinema_view(bool on) { cinema_view_ = on; mmb_dragging = false; }
	bool cinema_view() const { return cinema_view_; }
	// session.js getSavedGameData / restoreSavedGameData's camera (Engine.SetCameraData):
	// position, rotation, pitch and zoom, set at once without easing.
	Dictionary camera_data() const {
		Dictionary d;
		d["x"] = target_pos.x; d["z"] = target_pos.z;
		d["yaw"] = target_yaw; d["pitch"] = target_pitch_deg; d["zoom"] = target_zoom;
		return d;
	}
	void set_camera_data(const Dictionary &d) {
		target_yaw = yaw_angle = (float)(double)d.get("yaw", target_yaw);
		target_pitch_deg = pitch_deg = (float)(double)d.get("pitch", target_pitch_deg);
		target_zoom = zoom_height = (float)(double)d.get("zoom", target_zoom);
		follow_id_ = 0;
		set_target_pos(Vector3((float)(double)d.get("x", target_pos.x), target_pos.y, (float)(double)d.get("z", target_pos.z)));
	}
	enum SpeedKind { SPEED_SCROLL = 0, SPEED_ROTATE = 1, SPEED_ZOOM = 2 };
	// CCameraController: multiplies (or divides) by view.<kind>.speed.modifier.
	void change_speed(int kind, bool increase);
	Vector3 get_speed_multipliers() const { return speed_mul_; }
};

#endif
