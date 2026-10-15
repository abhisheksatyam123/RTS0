#ifndef RTSO_CINEMA_VIEW_H
#define RTSO_CINEMA_VIEW_H

// The session's side of a playing cinema path (0 A.D. 0.28 graphics/CinemaManager.cpp
// Update and gui/session/session.js updateCinemaPath / updateCinemaOverlay): while
// Simulation::cinema_playing() the camera flies the path at the front of the queue, the
// session GUI is hidden behind 2.39:1 letterbox bars and input is ignored (input.js
// handleInputBeforeGui returns early). The world view drops silhouettes and territory.

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/memory.hpp>

#include <algorithm>
#include <memory>

#include "../simulation/cinema.h"
#include "../simulation/simulation.h"
#include "camera_rig.h"

using namespace godot;

namespace rtso {

class RtsoCinemaView {
public:
	bool playing() const { return playing_; }

	// Every frame. ui_layer is the session GUI to hide while a path plays.
	void update(const Simulation &sim, RtsoCameraRig *rig, CanvasLayer *ui_layer, Node *parent, double delta) {
		const bool on = sim.cinema_playing() && !sim.cinema().queue.empty();
		if (on != playing_) {
			playing_ = on;
			if (on) {
				gui_was_visible_ = ui_layer && ui_layer->is_visible();
				if (ui_layer) ui_layer->set_visible(false);
			} else {
				if (ui_layer) ui_layer->set_visible(gui_was_visible_);
				path_index_ = -1;
			}
			if (rig) rig->set_cinema_view(on);
			show_letterbox(parent, on);
		}
		if (!on) return;
		update_letterbox(parent);

		const CinemaState &c = sim.cinema();
		const uint16_t idx = c.queue.front();
		if (idx >= c.paths.size()) return;
		const CinemaPath &path = c.paths[idx];
		if ((int)idx != path_index_ || c.path_elapsed_ms < last_sim_ms_) {
			// A new path at the front (CCmpCinemaManager Reset): build its splines once.
			path_index_ = (int)idx;
			pos_ = std::make_unique<cinema::Spline>(path.nodes);
			target_ = std::make_unique<cinema::Spline>(path.targets);
			clock_s_ = 0.0f;
		}
		last_sim_ms_ = c.path_elapsed_ms;
		// 0 A.D. advances the camera by timescale x real time and lets the simulation end
		// the path. Here the camera runs on real time but stays within a few frames of the
		// simulation's own time, so it holds still while the game is paused.
		const float ts = path.info.timescale;
		const float sim_s = (float)c.path_elapsed_ms / 1000.0f * ts;
		clock_s_ = std::clamp(clock_s_ + (float)delta * ts, sim_s, sim_s + 0.15f * ts);
		float eye[3], look[3];
		if (!rig || !rig->get_camera() || !cinema::camera_at(path, *pos_, *target_, clock_s_, eye, look)) return;
		const Vector3 e(eye[0], eye[1], eye[2]), l(look[0], look[1], look[2]);
		if ((l - e).length_squared() < 1e-6f) return;
		Vector3 up(0, 1, 0);
		if (std::abs((l - e).normalized().dot(up)) > 0.999f) up = Vector3(0, 0, -1);
		rig->get_camera()->look_at_from_position(e, l, up);
	}

private:
	void show_letterbox(Node *parent, bool on) {
		if (on && !layer_ && parent) {
			layer_ = memnew(CanvasLayer);
			layer_->set_layer(20);
			layer_->set_name("CinemaOverlay");
			for (ColorRect *&bar : bars_) {
				bar = memnew(ColorRect);
				bar->set_color(Color(0, 0, 0, 1));
				bar->set_mouse_filter(Control::MOUSE_FILTER_STOP);
				layer_->add_child(bar);
			}
			parent->add_child(layer_);
		}
		if (layer_) layer_->set_visible(on);
	}
	// session.js updateCinemaOverlay: bars of (height - width / 2.39) / 2 each.
	void update_letterbox(Node *parent) {
		if (!layer_ || !parent) return;
		Viewport *vp = parent->get_viewport();
		if (!vp) return;
		const Vector2 size = vp->get_visible_rect().size;
		const float bar = std::max(0.0f, (size.y - size.x / 2.39f) / 2.0f);
		bars_[0]->set_position(Vector2(0, 0));
		bars_[0]->set_size(Vector2(size.x, bar));
		bars_[1]->set_position(Vector2(0, size.y - bar));
		bars_[1]->set_size(Vector2(size.x, bar));
	}

	bool playing_ = false;
	bool gui_was_visible_ = true;
	int path_index_ = -1;
	uint32_t last_sim_ms_ = 0;
	float clock_s_ = 0.0f;
	std::unique_ptr<cinema::Spline> pos_, target_;
	CanvasLayer *layer_ = nullptr;
	ColorRect *bars_[2] = {nullptr, nullptr};
};

} // namespace rtso

#endif
