#include "minimap_view.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/rect2.hpp>

using namespace godot;

void RtsoMiniMapView::_bind_methods() {
	// sync takes raw C++ pointer, internal call from C++ RtsoMain
}

RtsoMiniMapView::RtsoMiniMapView() {
	set_custom_minimum_size(Vector2(160, 160));
	set_position(Vector2(10, 500));
}

RtsoMiniMapView::~RtsoMiniMapView() {}

void RtsoMiniMapView::sync(const rtso::Simulation *s) {
	sim = s;
	queue_redraw();
}

void RtsoMiniMapView::_draw() {
	Vector2 sz = get_size();
	draw_rect(Rect2(Vector2(0, 0), sz), Color(0.1f, 0.1f, 0.12f, 0.85f), true);
	draw_rect(Rect2(Vector2(0, 0), sz), Color(0.4f, 0.4f, 0.45f, 1.0f), false, 2.0f);

	if (!sim) return;

	float half_map = rtso::Simulation::MAP_HALF;
	auto map_to_mini = [&](float x, float z) -> Vector2 {
		float nx = (x + half_map) / (half_map * 2.0f);
		float nz = (z + half_map) / (half_map * 2.0f);
		return Vector2(nx * sz.x, nz * sz.y);
	};

	// Draw buildings
	for (const auto &b : sim->buildings()) {
		Vector2 pos = map_to_mini(b.x, b.z);
		Color col = b.is_center ? Color(0.9f, 0.8f, 0.2f) : Color(0.7f, 0.7f, 0.7f);
		draw_rect(Rect2(pos - Vector2(3, 3), Vector2(6, 6)), col, true);
	}

	// Draw resources
	auto res = sim->resources();
	for (size_t i = 0; i < res.count; ++i) {
		if (!res.alive[i] || res.amount[i] <= 0) continue;
		Vector2 pos = map_to_mini(res.x[i], res.z[i]);
		Color col = (res.type[i] == 0) ? Color(0.2f, 0.8f, 0.2f) : Color(1.0f, 0.85f, 0.1f);
		draw_rect(Rect2(pos - Vector2(1.5f, 1.5f), Vector2(3, 3)), col, true);
	}

	// Draw units
	auto units = sim->units();
	for (size_t i = 0; i < units.count; ++i) {
		if (!units.alive[i]) continue;
		Vector2 pos = map_to_mini(units.pos_x[i], units.pos_z[i]);
		Color col = (units.team[i] == 0) ? Color(0.3f, 0.6f, 1.0f) : Color(0.9f, 0.2f, 0.2f);
		draw_circle(pos, 2.0f, col);
	}
}
