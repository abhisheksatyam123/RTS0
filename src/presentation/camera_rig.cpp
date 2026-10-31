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
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include "../simulation/simulation.h"
#include "hotkeys_godot.h"

using namespace godot;

void RtsoCameraRig::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_target_pos", "pos"), &RtsoCameraRig::set_target_pos);
	ClassDB::bind_method(D_METHOD("camera_data"), &RtsoCameraRig::camera_data);
	ClassDB::bind_method(D_METHOD("set_camera_data", "data"), &RtsoCameraRig::set_camera_data);
	ClassDB::bind_method(D_METHOD("set_cinema_view", "on"), &RtsoCameraRig::set_cinema_view);
	ClassDB::bind_method(D_METHOD("cinema_view"), &RtsoCameraRig::cinema_view);
	ClassDB::bind_method(D_METHOD("get_camera"), &RtsoCameraRig::get_camera);
	ClassDB::bind_method(D_METHOD("zoom", "delta_zoom"), &RtsoCameraRig::zoom);
	ClassDB::bind_method(D_METHOD("reset_camera"), &RtsoCameraRig::reset_camera);
	ClassDB::bind_method(D_METHOD("toggle_birds_eye"), &RtsoCameraRig::toggle_birds_eye);
	ClassDB::bind_method(D_METHOD("is_birds_eye"), &RtsoCameraRig::is_birds_eye);
	ClassDB::bind_method(D_METHOD("get_pitch_deg"), &RtsoCameraRig::get_pitch_deg);
	ClassDB::bind_method(D_METHOD("get_speed_multipliers"), &RtsoCameraRig::get_speed_multipliers);
	ClassDB::bind_method(D_METHOD("set_edge_scroll_enabled", "enabled"), &RtsoCameraRig::set_edge_scroll_enabled);
	ClassDB::bind_method(D_METHOD("is_edge_scroll_enabled"), &RtsoCameraRig::is_edge_scroll_enabled);
	ClassDB::bind_method(D_METHOD("set_map_bounds_half", "h"), &RtsoCameraRig::set_map_bounds_half);
	ClassDB::bind_method(D_METHOD("get_map_bounds_half"), &RtsoCameraRig::get_map_bounds_half);
	ClassDB::bind_method(D_METHOD("set_map_circular", "on"), &RtsoCameraRig::set_map_circular);
	ClassDB::bind_method(D_METHOD("is_map_circular"), &RtsoCameraRig::is_map_circular);
}

RtsoCameraRig::RtsoCameraRig() {}
RtsoCameraRig::~RtsoCameraRig() {}

void RtsoCameraRig::_ready() {
	// Negative priority ensures camera rig processes before standard priority (0) nodes,
	// eliminating 1-frame latency between camera transforms and WorldView frustum culling.
	set_process_priority(-100);
	cam = memnew(Camera3D);
	cam->set_fov(45.0f); // 0 A.D. default.cfg: [view] fov = 45.0
	add_child(cam);
	namespace uc = rtso::user_config;
	pitch_min_ = (float)uc::get_number("view.rotate.x.min");
	pitch_max_ = std::max(pitch_min_, (float)uc::get_number("view.rotate.x.max"));
	pitch_default_ = (float)uc::get_number("view.rotate.x.default");
	constexpr float kMetresPerUnit = 4.0f;
	zoom_min_ = (float)uc::get_number("view.zoom.min") / kMetresPerUnit;
	zoom_max_ = std::max(zoom_min_, (float)uc::get_number("view.zoom.max") / kMetresPerUnit);
	zoom_default_ = (float)uc::get_number("view.zoom.default") / kMetresPerUnit;
	zoom_speed_ = (float)uc::get_number("view.zoom.speed") / kMetresPerUnit;
	zoom_speed_wheel_ = (float)uc::get_number("view.zoom.speed.wheel") / kMetresPerUnit;
	zoom_height = target_zoom = zoom_default_;
	pitch_deg = target_pitch_deg = pre_birds_eye_pitch_ = pitch_default_;
	target_pos = Vector3(0, 0, 0);
}

// CCameraController::Update: with the camera constrained, its look-at point stays
// CAMERA_EDGE_MARGIN inside the map's square edges, or inside its circle on a circular map.
void RtsoCameraRig::clamp_to_map(Vector3 &p) const {
	if (!constrain_) {
		if (sim_) p.y = sim_->get_terrain_height(p.x, p.z);
		return;
	}
	const float r = std::max(0.0f, map_bounds_half - kEdgeMargin);
	if (map_circular_) {
		const float d = Math::sqrt(p.x * p.x + p.z * p.z);
		if (d > r && d > 0.0001f) {
			p.x *= r / d;
			p.z *= r / d;
		}
	} else {
		p.x = Math::clamp(p.x, -r, r);
		p.z = Math::clamp(p.z, -r, r);
	}
	if (sim_) p.y = sim_->get_terrain_height(p.x, p.z);
}

void RtsoCameraRig::set_target_pos(Vector3 pos) {
	float y = pos.y;
	if (sim_) {
		y = sim_->get_terrain_height(pos.x, pos.z);
	}
	target_pos = Vector3(pos.x, y, pos.z);
	clamp_to_map(target_pos);
}

void RtsoCameraRig::zoom(float delta_zoom) {
	// CameraController: the zoom limits hold only while the camera is constrained.
	target_zoom = constrain_ ? Math::clamp(target_zoom + delta_zoom, zoom_min_, zoom_max_) : Math::clamp(target_zoom + delta_zoom, 1.0f, 1000.0f);
}

// CCameraController::ResetCameraAngleZoom: the default zoom, tilt and rotation (view.rotate.y.default 0).
void RtsoCameraRig::reset_camera() {
	target_yaw = 0.0f;
	target_zoom = zoom_default_;
	target_pitch_deg = pitch_default_;
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
				zoom(-zoom_speed_wheel_);
				return;
			}
			if (rtso::hotkeys::has(hk, "camera.zoom.wheel.out")) {
				zoom(zoom_speed_wheel_);
				return;
			}
		}
	}

	// Middle mouse drag motion
	Ref<InputEventMouseMotion> mm = event;
	if (mm.is_valid() && mmb_dragging) {
		if (!rtso::hotkeys::held("camera.pan")) {
			mmb_dragging = false;
			return;
		}
		follow_id_ = 0;
		Vector2 rel = mm->get_relative();
		// Bounded delta protects against extreme mouse jump spikes or cursor wrapping
		rel.x = Math::clamp(rel.x, -100.0f, 100.0f);
		rel.y = Math::clamp(rel.y, -100.0f, 100.0f);
		float cos_y = Math::cos(yaw_angle);
		float sin_y = Math::sin(yaw_angle);
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
		clamp_to_map(target_pos);
		return;
	}

	// Pinch to Zoom (Touch / Trackpad)
	Ref<InputEventMagnifyGesture> mag = event;
	if (mag.is_valid()) {
		follow_id_ = 0;
		float f = std::clamp((float)mag->get_factor(), 0.1f, 10.0f);
		zoom((1.0f - f) * 18.0f);
		return;
	}

	// Trackpad / Touch Pan Gesture
	Ref<InputEventPanGesture> pan = event;
	if (pan.is_valid()) {
		follow_id_ = 0;
		Vector2 delta = pan->get_delta();
		delta.x = Math::clamp(delta.x, -40.0f, 40.0f);
		delta.y = Math::clamp(delta.y, -40.0f, 40.0f);
		float cos_y = Math::cos(yaw_angle);
		float sin_y = Math::sin(yaw_angle);
		Vector3 world_dir(
			delta.x * cos_y - delta.y * sin_y,
			0.0f,
			delta.x * sin_y + delta.y * cos_y
		);
		target_pos += world_dir * 0.6f;
		clamp_to_map(target_pos);
		return;
	}
}

void RtsoCameraRig::_process(double delta) {
	if (cinema_view_) return; // back at the rig's own view when the path ends

	if (mmb_dragging && !rtso::hotkeys::held("camera.pan")) {
		mmb_dragging = false;
	}

	// Directly update target_pos from the followed unit with zero latency
	if (follow_id_ != 0 && sim_) {
		bool alive = false;
		auto fview = sim_->units();
		for (size_t i = 0; i < fview.count; ++i) {
			if (fview.id[i] == follow_id_ && fview.alive[i]) {
				const float th = sim_->get_terrain_height(fview.pos_x[i], fview.pos_z[i]);
				target_pos = Vector3(fview.pos_x[i], th, fview.pos_z[i]);
				clamp_to_map(target_pos);
				alive = true;
				break;
			}
		}
		if (!alive) follow_id_ = 0;
	}

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
		// ViewRotateXMin/Max hold only while constrained (straight down at most unconstrained).
		const float pmin = constrain_ ? pitch_min_ : 1.0f, pmax = constrain_ ? pitch_max_ : 89.9f;
		if (!birds_eye_ && on("camera.rotate.up")) target_pitch_deg = Math::clamp(target_pitch_deg - 40.0f * rot, pmin, pmax);
		if (!birds_eye_ && on("camera.rotate.down")) target_pitch_deg = Math::clamp(target_pitch_deg + 40.0f * rot, pmin, pmax);
		if (on("camera.zoom.in")) zoom(-zoom_speed_ * zm);
		if (on("camera.zoom.out")) zoom(zoom_speed_ * zm);
	}

	// Edge scrolling (active when window has focus, mouse is near viewport boundaries, and no key pan or MMB drag active)
	bool window_has_focus = true;
	DisplayServer *ds = DisplayServer::get_singleton();
	if (ds && !ds->window_is_focused()) {
		window_has_focus = false;
	}

	if (edge_scroll_enabled && window_has_focus && !mmb_dragging && input_dir.length_squared() < 0.001f) {
		Viewport *vp = get_viewport();
		if (vp) {
			Vector2 mpos = vp->get_mouse_position();
			Rect2 vprect = vp->get_visible_rect();
			const float margin = 14.0f;
			if (vprect.has_point(mpos)) {
				if (mpos.x <= margin) {
					float factor = std::clamp((margin - mpos.x) / margin, 0.0f, 1.0f);
					input_dir.x -= factor;
				} else if (mpos.x >= vprect.size.x - margin) {
					float factor = std::clamp((mpos.x - (vprect.size.x - margin)) / margin, 0.0f, 1.0f);
					input_dir.x += factor;
				}
				if (mpos.y <= margin) {
					float factor = std::clamp((margin - mpos.y) / margin, 0.0f, 1.0f);
					input_dir.z -= factor;
				} else if (mpos.y >= vprect.size.y - margin) {
					float factor = std::clamp((mpos.y - (vprect.size.y - margin)) / margin, 0.0f, 1.0f);
					input_dir.z += factor;
				}
			}
		}
	}

	// Camera-relative panning
	if (input_dir.length_squared() > 0.001f) {
		follow_id_ = 0;
		float len = input_dir.length();
		if (len > 1.0f) input_dir /= len;
		float cos_y = Math::cos(yaw_angle);
		float sin_y = Math::sin(yaw_angle);
		Vector3 world_dir(
			input_dir.x * cos_y - input_dir.z * sin_y,
			0.0f,
			input_dir.x * sin_y + input_dir.z * cos_y
		);
		target_pos += world_dir * (speed * speed_mul_[SPEED_SCROLL] * (float)delta);
		clamp_to_map(target_pos);
	}

	// Smooth interpolations: unconditionally stable exponential approach (1 - exp(-k * dt))
	const float dt = std::clamp((float)delta, 0.0f, 0.1f);
	const float t_pos   = 1.0f - Math::exp(-9.0f * dt);
	const float t_rot   = 1.0f - Math::exp(-8.0f * dt);
	const float t_zoom  = 1.0f - Math::exp(-8.0f * dt);
	const float t_pitch = 1.0f - Math::exp(-8.0f * dt);

	Vector3 cur_pos = get_global_position();
	if (cur_pos.distance_squared_to(target_pos) < 1e-6f) {
		cur_pos = target_pos;
	} else {
		cur_pos = cur_pos.lerp(target_pos, t_pos);
	}
	set_global_position(cur_pos);

	target_yaw = Math::wrapf(target_yaw, -(float)Math_PI, (float)Math_PI);
	yaw_angle = Math::lerp_angle(yaw_angle, target_yaw, t_rot);
	yaw_angle = Math::wrapf(yaw_angle, -(float)Math_PI, (float)Math_PI);
	if (Math::abs(target_yaw - yaw_angle) < 1e-4f) yaw_angle = target_yaw;
	set_rotation(Vector3(0, yaw_angle, 0));

	if (constrain_) { // ClampSmoothly each frame: re-constraining brings zoom and tilt back in range
		target_zoom = Math::clamp(target_zoom, zoom_min_, zoom_max_);
		if (!birds_eye_) target_pitch_deg = Math::clamp(target_pitch_deg, pitch_min_, pitch_max_);
	}
	zoom_height = Math::lerp(zoom_height, target_zoom, t_zoom);
	if (Math::abs(target_zoom - zoom_height) < 1e-4f) zoom_height = target_zoom;

	pitch_deg = Math::lerp(pitch_deg, target_pitch_deg, t_pitch);
	if (Math::abs(target_pitch_deg - pitch_deg) < 1e-4f) pitch_deg = target_pitch_deg;

	if (cam) {
		const float pitch = Math::deg_to_rad(pitch_deg);
		const float cos_p = Math::cos(pitch);
		const float sin_p = Math::sin(pitch);
		const float cam_local_y = zoom_height * sin_p;
		const float cam_local_z = zoom_height * cos_p;

		// SetupCameraMatrix: zoom_height away from the look-at point along the view.
		cam->set_position(Vector3(0, cam_local_y, cam_local_z));
		cam->set_rotation(Vector3(-pitch, 0, 0));
		cam->force_update_transform();
	}
}
