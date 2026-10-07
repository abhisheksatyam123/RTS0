#include "camera_rig.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_magnify_gesture.hpp>
#include <godot_cpp/classes/input_event_pan_gesture.hpp>
#include <godot_cpp/classes/input_event_screen_drag.hpp>
#include <godot_cpp/classes/input_event_screen_touch.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/math.hpp>
#include "../simulation/simulation.h"

using namespace godot;

void RtsoCameraRig::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_target_pos", "pos"), &RtsoCameraRig::set_target_pos);
	ClassDB::bind_method(D_METHOD("zoom", "delta_zoom"), &RtsoCameraRig::zoom);
	ClassDB::bind_method(D_METHOD("reset_camera"), &RtsoCameraRig::reset_camera);
	ClassDB::bind_method(D_METHOD("set_edge_scroll_enabled", "enabled"), &RtsoCameraRig::set_edge_scroll_enabled);
	ClassDB::bind_method(D_METHOD("is_edge_scroll_enabled"), &RtsoCameraRig::is_edge_scroll_enabled);
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
	float half = map_bounds_half;
	target_pos.x = Math::clamp(pos.x, -half, half);
	target_pos.z = Math::clamp(pos.z, -half, half);
	target_pos.y = 0.0f;
}

void RtsoCameraRig::zoom(float delta_zoom) {
	target_zoom = Math::clamp(target_zoom + delta_zoom, 8.0f, 65.0f);
}

void RtsoCameraRig::reset_camera() {
	target_yaw = 0.0f;
	target_zoom = 28.0f;
}

void RtsoCameraRig::_unhandled_input(const Ref<InputEvent> &event) {
	Ref<InputEventMouseButton> mb = event;
	if (mb.is_valid()) {
		// Middle Mouse Button Drag (0 A.D. pan = MouseMiddle)
		if (mb->get_button_index() == MOUSE_BUTTON_MIDDLE) {
			mmb_dragging = mb->is_pressed();
			return;
		}
		if (mb->is_pressed()) {
			if (mb->get_button_index() == MOUSE_BUTTON_WHEEL_UP) {
				zoom(-2.5f);
				return;
			} else if (mb->get_button_index() == MOUSE_BUTTON_WHEEL_DOWN) {
				zoom(2.5f);
				return;
			}
		}
	}

	// Middle mouse drag motion
	Ref<InputEventMouseMotion> mm = event;
	if (mm.is_valid() && mmb_dragging) {
		Vector2 rel = mm->get_relative();
		float cos_y = Math::cos(target_yaw);
		float sin_y = Math::sin(target_yaw);
		float drag_scale = (zoom_height / 28.0f) * 0.06f;
		Vector3 world_dir(
			(-rel.x * cos_y + rel.y * sin_y) * drag_scale,
			0.0f,
			(-rel.x * sin_y - rel.y * cos_y) * drag_scale
		);
		target_pos += world_dir;
		float half = map_bounds_half;
		target_pos.x = Math::clamp(target_pos.x, -half, half);
		target_pos.z = Math::clamp(target_pos.z, -half, half);
		return;
	}

	// Hotkeys for zoom and reset
	Ref<InputEventKey> key = event;
	if (key.is_valid() && key->is_pressed() && !key->is_echo()) {
		if (key->get_keycode() == KEY_EQUAL || key->get_keycode() == KEY_PLUS || key->get_keycode() == KEY_KP_ADD) {
			zoom(-3.0f);
			return;
		}
		if (key->get_keycode() == KEY_MINUS || key->get_keycode() == KEY_KP_SUBTRACT) {
			zoom(3.0f);
			return;
		}
		if (key->get_keycode() == KEY_R && !key->is_ctrl_pressed()) {
			reset_camera();
			return;
		}
	}

	// Pinch to Zoom (Touch / Trackpad)
	Ref<InputEventMagnifyGesture> mag = event;
	if (mag.is_valid()) {
		zoom((1.0f - mag->get_factor()) * 18.0f);
		return;
	}

	// Trackpad / Touch Pan Gesture
	Ref<InputEventPanGesture> pan = event;
	if (pan.is_valid()) {
		Vector2 delta = pan->get_delta();
		float cos_y = Math::cos(target_yaw);
		float sin_y = Math::sin(target_yaw);
		Vector3 world_dir(
			delta.x * cos_y - delta.y * sin_y,
			0.0f,
			delta.x * sin_y + delta.y * cos_y
		);
		target_pos += world_dir * 0.6f;
		float half = map_bounds_half;
		target_pos.x = Math::clamp(target_pos.x, -half, half);
		target_pos.z = Math::clamp(target_pos.z, -half, half);
		return;
	}
}

void RtsoCameraRig::_process(double delta) {
	Input *input = Input::get_singleton();
	Vector3 input_dir(0, 0, 0);

	// WASD and Arrow key panning
	if (input->is_key_pressed(KEY_W) || input->is_key_pressed(KEY_UP)) input_dir.z -= 1.0f;
	if (input->is_key_pressed(KEY_S) || input->is_key_pressed(KEY_DOWN)) input_dir.z += 1.0f;
	if (input->is_key_pressed(KEY_A) || input->is_key_pressed(KEY_LEFT)) input_dir.x -= 1.0f;
	if (input->is_key_pressed(KEY_D) || input->is_key_pressed(KEY_RIGHT)) input_dir.x += 1.0f;

	// Q / E Rotation (0 A.D. canonical rotate.cw / rotate.ccw)
	if (input->is_key_pressed(KEY_Q)) target_yaw += 2.0f * (float)delta;
	if (input->is_key_pressed(KEY_E)) target_yaw -= 2.0f * (float)delta;

	// Edge scrolling (active when mouse is near viewport boundaries and no key pan active)
	if (edge_scroll_enabled && input_dir.length_squared() < 0.001f) {
		Viewport *vp = get_viewport();
		if (vp) {
			Vector2 mpos = vp->get_mouse_position();
			Rect2 vprect = vp->get_visible_rect();
			const float margin = 14.0f;
			if (vprect.has_point(mpos)) {
				if (mpos.x <= margin) input_dir.x -= 1.0f;
				else if (mpos.x >= vprect.size.x - margin) input_dir.x += 1.0f;
				if (mpos.y <= margin) input_dir.z -= 1.0f;
				else if (mpos.y >= vprect.size.y - margin) input_dir.z += 1.0f;
			}
		}
	}

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
		float half = map_bounds_half;
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
