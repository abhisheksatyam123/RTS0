#include "camera_rig.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/core/math.hpp>
#include "../simulation/simulation.h"

using namespace godot;

void RtsoCameraRig::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_target_pos", "pos"), &RtsoCameraRig::set_target_pos);
	ClassDB::bind_method(D_METHOD("zoom", "delta_zoom"), &RtsoCameraRig::zoom);
}

RtsoCameraRig::RtsoCameraRig() {}
RtsoCameraRig::~RtsoCameraRig() {}

void RtsoCameraRig::_ready() {
	cam = memnew(Camera3D);
	add_child(cam);
	zoom_height = 28.0f;
	target_zoom = 28.0f;
	cam->set_position(Vector3(0, zoom_height, zoom_height * 0.95f));
	cam->set_rotation_degrees(Vector3(-48, 0, 0));
	target_pos = Vector3(0, 0, 0);
}

void RtsoCameraRig::set_target_pos(Vector3 pos) {
	float half = rtso::Simulation::MAP_HALF;
	target_pos.x = Math::clamp(pos.x, -half, half);
	target_pos.z = Math::clamp(pos.z, -half, half);
	target_pos.y = 0.0f;
}

void RtsoCameraRig::zoom(float delta_zoom) {
	target_zoom = Math::clamp(target_zoom + delta_zoom, 8.0f, 45.0f);
}

void RtsoCameraRig::_unhandled_input(const Ref<InputEvent> &event) {
	Ref<InputEventMouseButton> mb = event;
	if (mb.is_valid() && mb->is_pressed()) {
		if (mb->get_button_index() == MOUSE_BUTTON_WHEEL_UP) {
			zoom(-2.5f);
		} else if (mb->get_button_index() == MOUSE_BUTTON_WHEEL_DOWN) {
			zoom(2.5f);
		}
	}
}

void RtsoCameraRig::_process(double delta) {
	Input *input = Input::get_singleton();
	Vector3 input_dir(0, 0, 0);

	if (input->is_key_pressed(KEY_W) || input->is_key_pressed(KEY_UP)) input_dir.z -= 1.0f;
	if (input->is_key_pressed(KEY_S) || input->is_key_pressed(KEY_DOWN)) input_dir.z += 1.0f;
	if (input->is_key_pressed(KEY_A) || input->is_key_pressed(KEY_LEFT)) input_dir.x -= 1.0f;
	if (input->is_key_pressed(KEY_D) || input->is_key_pressed(KEY_RIGHT)) input_dir.x += 1.0f;

	// Q / E Rotation
	if (input->is_key_pressed(KEY_Q)) target_yaw += 2.0f * (float)delta;
	if (input->is_key_pressed(KEY_E)) target_yaw -= 2.0f * (float)delta;

	// Camera-relative panning
	if (input_dir.length_squared() > 0.001f) {
		input_dir.normalize();
		float cos_y = Math::cos(target_yaw);
		float sin_y = Math::sin(target_yaw);
		Vector3 world_dir(
			input_dir.x * cos_y - input_dir.z * sin_y,
			0.0f,
			input_dir.x * sin_y + input_dir.z * cos_y
		);
		target_pos += world_dir * speed * (float)delta;
		float half = rtso::Simulation::MAP_HALF;
		target_pos.x = Math::clamp(target_pos.x, -half, half);
		target_pos.z = Math::clamp(target_pos.z, -half, half);
	}

	// Smooth interpolations
	Vector3 cur_pos = get_global_position();
	set_global_position(cur_pos.lerp(target_pos, (float)delta * 9.0f));

	yaw_angle = Math::lerp(yaw_angle, target_yaw, (float)delta * 8.0f);
	set_rotation(Vector3(0, yaw_angle, 0));

	zoom_height = Math::lerp(zoom_height, target_zoom, (float)delta * 8.0f);
	if (cam) {
		cam->set_position(Vector3(0, zoom_height, zoom_height * 0.82f));
	}
}
