#include "minimap_view.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/core/math.hpp>

using namespace godot;

void RtsoMiniMapView::_bind_methods() {
	ADD_SIGNAL(MethodInfo("minimap_clicked", PropertyInfo(Variant::VECTOR2, "world_pos")));
}

RtsoMiniMapView::RtsoMiniMapView() {
	set_custom_minimum_size(Vector2(190, 190));
	set_position(Vector2(16, 480));
}

RtsoMiniMapView::~RtsoMiniMapView() {}

void RtsoMiniMapView::sync(const rtso::Simulation *s, Vector3 p_cam_pos) {
	sim = s;
	cam_pos = p_cam_pos;
	queue_redraw();
}

void RtsoMiniMapView::_gui_input(const Ref<InputEvent> &event) {
	Ref<InputEventMouseButton> mb = event;
	if (mb.is_valid()) {
		if (mb->get_button_index() == MOUSE_BUTTON_LEFT) {
			if (mb->is_pressed()) {
				dragging = true;
			} else {
				dragging = false;
			}
			Vector2 sz = get_size();
			Vector2 mpos = mb->get_position();
			float half = rtso::Simulation::MAP_HALF;
			float wx = (mpos.x / sz.x) * (half * 2.0f) - half;
			float wz = (mpos.y / sz.y) * (half * 2.0f) - half;
			emit_signal("minimap_clicked", Vector2(wx, wz));
			accept_event();
		}
	}

	Ref<InputEventMouseMotion> mm = event;
	if (mm.is_valid() && dragging) {
		Vector2 sz = get_size();
		Vector2 mpos = mm->get_position();
		float half = rtso::Simulation::MAP_HALF;
		float wx = (mpos.x / sz.x) * (half * 2.0f) - half;
		float wz = (mpos.y / sz.y) * (half * 2.0f) - half;
		emit_signal("minimap_clicked", Vector2(wx, wz));
		accept_event();
	}
}

void RtsoMiniMapView::_draw() {
	Vector2 sz = get_size();
	// Background Panel
	draw_rect(Rect2(Vector2(0, 0), sz), Color(0.08f, 0.09f, 0.12f, 0.90f), true);
	draw_rect(Rect2(Vector2(0, 0), sz), Color(0.45f, 0.45f, 0.52f, 1.0f), false, 2.0f);

	if (!sim) return;

	float half_map = rtso::Simulation::MAP_HALF;
	auto map_to_mini = [&](float x, float z) -> Vector2 {
		float nx = (x + half_map) / (half_map * 2.0f);
		float nz = (z + half_map) / (half_map * 2.0f);
		return Vector2(nx * sz.x, nz * sz.y);
	};

	// 1. Draw Resource Nodes
	auto res = sim->resources();
	for (size_t i = 0; i < res.count; ++i) {
		if (!res.alive[i] || res.amount[i] <= 0) continue;
		Vector2 pos = map_to_mini(res.x[i], res.z[i]);
		Color col = Color(0.2f, 0.8f, 0.25f, 0.8f); // Wood
		if (res.type[i] == rtso::RES_GOLD) col = Color(1.0f, 0.85f, 0.15f, 0.9f); // Gold
		else if (res.type[i] == rtso::RES_STONE) col = Color(0.65f, 0.65f, 0.70f, 0.8f); // Stone

		draw_rect(Rect2(pos - Vector2(1.5f, 1.5f), Vector2(3.0f, 3.0f)), col, true);
	}

	// 2. Draw Buildings
	for (const auto &b : sim->buildings()) {
		Vector2 pos = map_to_mini(b.x, b.z);
		Color col = Color(0.7f, 0.7f, 0.7f);
		float box_sz = 6.0f;

		if (b.type == rtso::BLD_TOWN_CENTER) {
			col = (b.team == 0) ? Color(1.0f, 0.85f, 0.2f) : Color(0.95f, 0.2f, 0.2f);
			box_sz = 9.0f;
		} else if (b.type == rtso::BLD_BARRACKS) {
			col = (b.team == 0) ? Color(0.95f, 0.55f, 0.2f) : Color(0.85f, 0.3f, 0.3f);
			box_sz = 7.0f;
		} else if (b.type == rtso::BLD_TOWER) {
			col = Color(0.4f, 0.85f, 1.0f);
			box_sz = 5.0f;
		} else if (b.type == rtso::BLD_STOREHOUSE) {
			col = Color(0.75f, 0.55f, 0.35f);
		}

		draw_rect(Rect2(pos - Vector2(box_sz * 0.5f, box_sz * 0.5f), Vector2(box_sz, box_sz)), col, true);
	}

	// 3. Draw Units
	auto units = sim->units();
	for (size_t i = 0; i < units.count; ++i) {
		if (!units.alive[i]) continue;
		Vector2 pos = map_to_mini(units.pos_x[i], units.pos_z[i]);
		Color col = (units.team[i] == 0) ? Color(0.25f, 0.65f, 1.0f) : Color(0.95f, 0.25f, 0.2f);
		float rad = (units.unit_type[i] == rtso::UNIT_CAVALRY) ? 3.0f : 2.0f;
		draw_circle(pos, rad, col);
	}

	// 4. Draw Camera Viewport Frustum Box
	Vector2 cam_mini = map_to_mini(cam_pos.x, cam_pos.z);
	draw_rect(Rect2(cam_mini - Vector2(18, 14), Vector2(36, 28)), Color(1.0f, 1.0f, 1.0f, 0.6f), false, 1.5f);
}
