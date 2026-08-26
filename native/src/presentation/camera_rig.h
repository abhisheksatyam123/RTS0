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
};

#endif
