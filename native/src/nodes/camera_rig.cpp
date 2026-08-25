#include "camera_rig.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/input.hpp>

using namespace godot;

void RtsoCameraRig::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_target_pos", "pos"), &RtsoCameraRig::set_target_pos);
}

RtsoCameraRig::RtsoCameraRig() {}
RtsoCameraRig::~RtsoCameraRig() {}

void RtsoCameraRig::_ready() {
	cam = memnew(Camera3D);
	add_child(cam);
	cam->set_position(Vector3(0, 22, 18));
	cam->set_rotation_degrees(Vector3(-50, 0, 0));
	target_pos = Vector3(0, 0, 0);
}

void RtsoCameraRig::_process(double delta) {
	Input *input = Input::get_singleton();
	Vector3 dir(0, 0, 0);
	if (input->is_key_pressed(KEY_W) || input->is_key_pressed(KEY_UP)) dir.z -= 1.0f;
	if (input->is_key_pressed(KEY_S) || input->is_key_pressed(KEY_DOWN)) dir.z += 1.0f;
	if (input->is_key_pressed(KEY_A) || input->is_key_pressed(KEY_LEFT)) dir.x -= 1.0f;
	if (input->is_key_pressed(KEY_D) || input->is_key_pressed(KEY_RIGHT)) dir.x += 1.0f;

	if (dir.length_squared() > 0.001f) {
		dir.normalize();
		target_pos += dir * speed * (float)delta;
	}

	Vector3 cur = get_global_position();
	set_global_position(cur.lerp(target_pos, (float)delta * 8.0f));
}
