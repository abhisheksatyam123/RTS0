#include "camera_rig.h"
#include "user_config.h"
#include <algorithm>
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
#include <godot_cpp/variant/utility_functions.hpp>
#include "../simulation/simulation.h"
#include "hotkeys_godot.h"

using namespace godot;

void RtsoCameraRig::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_target_pos", "pos"), &RtsoCameraRig::set_target_pos);
	ClassDB::bind_method(D_METHOD("zoom", "delta_zoom"), &RtsoCameraRig::zoom);
	ClassDB::bind_method(D_METHOD("reset_camera"), &RtsoCameraRig::reset_camera);
	ClassDB::bind_method(D_METHOD("toggle_birds_eye"), &RtsoCameraRig::toggle_birds_eye);
	ClassDB::bind_method(D_METHOD("is_birds_eye"), &RtsoCameraRig::is_birds_eye);
	ClassDB::bind_method(D_METHOD("get_pitch_deg"), &RtsoCameraRig::get_pitch_deg);
	ClassDB::bind_method(D_METHOD("get_speed_multipliers"), &RtsoCameraRig::get_speed_multipliers);
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
	float half = bounds_half();
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
	target_pitch_deg = 50.6f;
	birds_eye_ = false;
}

// 0.28 camera.togglebirdseyeview (Shift+Tab): "Toggle between regular and bird's
// eye (top-down) views". Just short of 90 degrees keeps the camera's offset finite.
void RtsoCameraRig::toggle_birds_eye() {
	birds_eye_ = !birds_eye_;
	if (birds_eye_) {
		pre_birds_eye_pitch_ = target_pitch_deg;
		target_pitch_deg = 89.9f;
	} else {
		target_pitch_deg = pre_birds_eye_pitch_;
	}
}

void RtsoCameraRig::change_speed(int kind, bool increase) {
	static const char *kModifier[3] = { "view.scroll.speed.modifier", "view.rotate.speed.modifier", "view.zoom.speed.modifier" };
	static const char *kName[3] = { "Scroll", "Rotate", "Zoom" };
	if (kind < 0 || kind > 2) return;
	const float m = std::max(1.0f, (float)rtso::user_config::get_number(kModifier[kind]));
	speed_mul_[kind] = increase ? speed_mul_[kind] * m : speed_mul_[kind] / m;
	UtilityFunctions::print(String(kName[kind]) + (increase ? " speed increased to x" : " speed decreased to x") + String::num(speed_mul_[kind], 3));
}

void RtsoCameraRig::follow_to(Vector3 pos) {
	set_target_pos(pos);
}

void RtsoCameraRig::_unhandled_input(const Ref<InputEvent> &event) {
	if (cinema_view_) return;
	Ref<InputEventMouseButton> mb = event;
	if (mb.is_valid() || Ref<InputEventKey>(event).is_valid()) {
		// CameraController camera.pan (MouseMiddle by default): drag the view while it is held.
		const bool pan = rtso::hotkeys::held("camera.pan");
		if (pan != mmb_dragging) {
			mmb_dragging = pan;
			if (mb.is_valid()) return;
		}
	}
	if (mb.is_valid()) {
		if (mb->is_pressed()) {
			// 0 A.D. camera.zoom.wheel.* and camera.rotate.wheel.* (Shift+wheel rotates).
			const auto hk = rtso::hotkeys::fired(mb);
			if (rtso::hotkeys::has(hk, "camera.rotate.wheel.cw")) {
				target_yaw += 0.2f;
				return;
			}
			if (rtso::hotkeys::has(hk, "camera.rotate.wheel.ccw")) {
				target_yaw -= 0.2f;
				return;
			}
			if (rtso::hotkeys::has(hk, "camera.zoom.wheel.in")) {
				zoom(-2.5f);
				return;
			}
			if (rtso::hotkeys::has(hk, "camera.zoom.wheel.out")) {
				zoom(2.5f);
				return;
			}
		}
	}

	// Middle mouse drag motion
	Ref<InputEventMouseMotion> mm = event;
	if (mm.is_valid() && mmb_dragging) {
		follow_id_ = 0;
		Vector2 rel = mm->get_relative();
		float cos_y = Math::cos(target_yaw);
		float sin_y = Math::sin(target_yaw);
		// 0.28 view.drag.speed (default 0.5) and view.drag.inverted.
		float drag_scale = (zoom_height / 28.0f) * 0.06f *
				(float)std::clamp(rtso::user_config::get_number("view.drag.speed"), 0.01, 0.6) / 0.5f;
		if (rtso::user_config::get_bool("view.drag.inverted"))
			drag_scale = -drag_scale;
		Vector3 world_dir(
			(-rel.x * cos_y + rel.y * sin_y) * drag_scale,
			0.0f,
			(-rel.x * sin_y - rel.y * cos_y) * drag_scale
		);
		target_pos += world_dir;
		float half = bounds_half();
		target_pos.x = Math::clamp(target_pos.x, -half, half);
		target_pos.z = Math::clamp(target_pos.z, -half, half);
		return;
	}

	// camera.reset is dispatched by the main node, since R also picks the
	// barracks while placing a building.

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
		float half = bounds_half();
		target_pos.x = Math::clamp(target_pos.x, -half, half);
		target_pos.z = Math::clamp(target_pos.z, -half, half);
		return;
	}
}

void RtsoCameraRig::_process(double delta) {
	if (cinema_view_) return; // back at the rig's own view when the path ends
	Vector3 input_dir(0, 0, 0);

	// Held camera hotkeys (0 A.D. [hotkey.camera]: WASD/arrows scroll, Q/E and
	// Ctrl+A/D/arrows rotate, Ctrl+W/S tilt, +/- zoom). While placing a building
	// the letters pick the building, so only the arrows scroll; while typing,
	// no key moves the camera.
	if (key_scroll_mode_ != KEYS_NONE) {
		const bool arrows_only = key_scroll_mode_ == KEYS_ARROWS_ONLY;
		const rtso::hotkeys::IsDown down = [arrows_only](const std::string &k) {
			if (arrows_only && k.size() == 1 && k[0] >= 'a' && k[0] <= 'z') return false;
			return rtso::hotkeys::is_down_now(k);
		};
		const auto &hk = rtso::hotkeys::table();
		auto on = [&](const char *name) { return hk.is_held_exclusive(name, down); };
		if (on("camera.up")) input_dir.z -= 1.0f;
		if (on("camera.down")) input_dir.z += 1.0f;
		if (on("camera.left")) input_dir.x -= 1.0f;
		if (on("camera.right")) input_dir.x += 1.0f;
		const float rot = speed_mul_[SPEED_ROTATE] * (float)delta, zm = speed_mul_[SPEED_ZOOM] * (float)delta;
		if (on("camera.rotate.cw")) target_yaw += 2.0f * rot;
		if (on("camera.rotate.ccw")) target_yaw -= 2.0f * rot;
		if (!birds_eye_ && on("camera.rotate.up")) target_pitch_deg = Math::clamp(target_pitch_deg - 40.0f * rot, 25.0f, 85.0f);
		if (!birds_eye_ && on("camera.rotate.down")) target_pitch_deg = Math::clamp(target_pitch_deg + 40.0f * rot, 25.0f, 85.0f);
		if (on("camera.zoom.in")) zoom(-30.0f * zm);
		if (on("camera.zoom.out")) zoom(30.0f * zm);
	}

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
		follow_id_ = 0;
		input_dir.normalize();
		float cos_y = Math::cos(target_yaw);
		float sin_y = Math::sin(target_yaw);
		Vector3 world_dir(
			input_dir.x * cos_y - input_dir.z * sin_y,
			0.0f,
			input_dir.x * sin_y + input_dir.z * cos_y
		);
		target_pos += world_dir * speed * speed_mul_[SPEED_SCROLL] * (float)delta;
		float half = bounds_half();
		target_pos.x = Math::clamp(target_pos.x, -half, half);
		target_pos.z = Math::clamp(target_pos.z, -half, half);
	}

	// Smooth interpolations
	Vector3 cur_pos = get_global_position();
	set_global_position(cur_pos.lerp(target_pos, (float)delta * 9.0f));

	yaw_angle = Math::lerp(yaw_angle, target_yaw, (float)delta * 8.0f);
	set_rotation(Vector3(0, yaw_angle, 0));

	zoom_height = Math::lerp(zoom_height, target_zoom, (float)delta * 8.0f);
	pitch_deg = Math::lerp(pitch_deg, target_pitch_deg, (float)delta * 8.0f);
	if (cam) {
		const float pitch = Math::deg_to_rad(pitch_deg);
		cam->set_position(Vector3(0, zoom_height, zoom_height / Math::tan(pitch)));
		cam->set_rotation(Vector3(-pitch, 0, 0));
	}
}
