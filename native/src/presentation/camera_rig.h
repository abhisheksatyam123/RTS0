#ifndef RTSO_CAMERA_RIG_H
#define RTSO_CAMERA_RIG_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

using namespace godot;

class RtsoCameraRig : public Node3D {
	GDCLASS(RtsoCameraRig, Node3D);

	Camera3D *cam = nullptr;
	Vector3 target_pos;
	float speed = 25.0f;

protected:
	static void _bind_methods();

public:
	RtsoCameraRig();
	~RtsoCameraRig();

	void _ready() override;
	void _process(double delta) override;

	Camera3D *get_camera() const { return cam; }
	void set_target_pos(Vector3 pos) { target_pos = pos; }
};

#endif
