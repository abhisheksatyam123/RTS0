#include "minimap_view.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/core/math.hpp>

using namespace godot;

void RtsoMiniMapView::_bind_methods() {
	ADD_SIGNAL(MethodInfo("minimap_clicked", PropertyInfo(Variant::VECTOR2, "world_pos")));
	ClassDB::bind_method(D_METHOD("add_ping", "world_pos", "color"), &RtsoMiniMapView::add_ping, DEFVAL(Color(1.0f, 0.85f, 0.2f)));
	ClassDB::bind_method(D_METHOD("toggle_territory"), &RtsoMiniMapView::toggle_territory);
	ClassDB::bind_method(D_METHOD("toggle_resources"), &RtsoMiniMapView::toggle_resources);
}

RtsoMiniMapView::RtsoMiniMapView() {
	set_anchor(SIDE_LEFT, 0.0f);
	set_anchor(SIDE_RIGHT, 0.0f);
	set_anchor(SIDE_TOP, 1.0f);
	set_anchor(SIDE_BOTTOM, 1.0f);
	set_offset(SIDE_LEFT, 14.0f);
	set_offset(SIDE_RIGHT, 204.0f);
	set_offset(SIDE_TOP, -204.0f);
	set_offset(SIDE_BOTTOM, -14.0f);
	set_custom_minimum_size(Vector2(190, 190));
}

RtsoMiniMapView::~RtsoMiniMapView() {}

void RtsoMiniMapView::add_ping(Vector2 world_pos, Color col) {
	Vector2 sz = get_size();
	float half_map = sim ? sim->map_half() : 64.0f;
	float nx = (world_pos.x + half_map) / (half_map * 2.0f);
	float nz = (world_pos.y + half_map) / (half_map * 2.0f);
	Vector2 mpos(nx * sz.x, nz * sz.y);

	MapPing p;
	p.mini_pos = mpos;
	p.timer = 3.0f;
	p.color = col;
	pings.push_back(p);
	queue_redraw();
}

void RtsoMiniMapView::sync(const rtso::Simulation *s, Vector3 p_cam_pos) {
	sim = s;
	cam_pos = p_cam_pos;

	// Decay pings
	for (auto &p : pings) {
		p.timer -= 0.05f;
	}
	pings.erase(std::remove_if(pings.begin(), pings.end(),
		[](const MapPing &p) { return p.timer <= 0.0f; }), pings.end());

	queue_redraw();
}

void RtsoMiniMapView::_gui_input(const Ref<InputEvent> &event) {
	Ref<InputEventMouseButton> mb = event;
	if (mb.is_valid()) {
		Vector2 sz = get_size();
		Vector2 mpos = mb->get_position();
		float half = sim ? sim->map_half() : 64.0f;
		float wx = (mpos.x / sz.x) * (half * 2.0f) - half;
		float wz = (mpos.y / sz.y) * (half * 2.0f) - half;

		if (mb->get_button_index() == MOUSE_BUTTON_LEFT) {
			dragging = mb->is_pressed();
			emit_signal("minimap_clicked", Vector2(wx, wz));
			accept_event();
		} else if (mb->get_button_index() == MOUSE_BUTTON_RIGHT && mb->is_pressed()) {
			// Right-click on minimap adds tactical ping!
			add_ping(Vector2(wx, wz), Color(1.0f, 0.4f, 0.2f));
			accept_event();
		}
	}

	Ref<InputEventMouseMotion> mm = event;
	if (mm.is_valid() && dragging) {
		Vector2 sz = get_size();
		Vector2 mpos = mm->get_position();
		float half = sim ? sim->map_half() : 64.0f;
		float wx = (mpos.x / sz.x) * (half * 2.0f) - half;
		float wz = (mpos.y / sz.y) * (half * 2.0f) - half;
		emit_signal("minimap_clicked", Vector2(wx, wz));
		accept_event();
	}
}

void RtsoMiniMapView::_draw() {
	Vector2 sz = get_size();
	// Background Panel
	draw_rect(Rect2(Vector2(0, 0), sz), Color(0.06f, 0.07f, 0.10f, 0.94f), true);
	draw_rect(Rect2(Vector2(0, 0), sz), Color(0.40f, 0.44f, 0.55f, 1.0f), false, 2.0f);

	if (!sim) return;

	float half_map = sim ? sim->map_half() : 64.0f;
	auto map_to_mini = [&](float x, float z) -> Vector2 {
		float nx = (x + half_map) / (half_map * 2.0f);
		float nz = (z + half_map) / (half_map * 2.0f);
		return Vector2(nx * sz.x, nz * sz.y);
	};

	// 1. Territory Influence Circles (0 A.D. Territory System)
	if (show_territory) {
		for (const auto &b : sim->buildings()) {
			if (b.build_progress < 1.0f) continue;
			Vector2 pos = map_to_mini(b.x, b.z);
			float radius_m = 0.0f;
			if (b.type == rtso::BLD_TOWN_CENTER) radius_m = 32.0f;
			else if (b.type == rtso::BLD_FORTRESS) radius_m = 28.0f;
			else if (b.type == rtso::BLD_TOWER) radius_m = 18.0f;

			if (radius_m > 0.0f) {
				float r_px = (radius_m / (half_map * 2.0f)) * sz.x;
				Color territory_fill = (b.team == 0) ? Color(0.2f, 0.55f, 1.0f, 0.12f) : Color(0.95f, 0.25f, 0.2f, 0.12f);
				Color territory_edge = (b.team == 0) ? Color(0.3f, 0.70f, 1.0f, 0.45f) : Color(1.0f, 0.35f, 0.3f, 0.45f);
				draw_circle(pos, r_px, territory_fill);
				draw_arc(pos, r_px, 0.0f, 6.2831853f, 24, territory_edge, 1.0f);
			}
		}
	}

	// 2. Draw Resource Nodes
	if (show_resources) {
		auto res = sim->resources();
		for (size_t i = 0; i < res.count; ++i) {
			if (!res.alive[i] || res.amount[i] <= 0) continue;
			Vector2 pos = map_to_mini(res.x[i], res.z[i]);
			Color col = Color(0.2f, 0.8f, 0.25f, 0.85f); // Wood
			if (res.type[i] == rtso::RES_GOLD) col = Color(1.0f, 0.85f, 0.15f, 0.95f); // Gold
			else if (res.type[i] == rtso::RES_STONE) col = Color(0.70f, 0.70f, 0.78f, 0.85f); // Stone

			draw_rect(Rect2(pos - Vector2(1.5f, 1.5f), Vector2(3.0f, 3.0f)), col, true);
		}
	}

	// 3. Draw Buildings
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
		} else if (b.type == rtso::BLD_STOREHOUSE || b.type == rtso::BLD_FARMSTEAD) {
			col = Color(0.75f, 0.55f, 0.35f);
		}

		draw_rect(Rect2(pos - Vector2(box_sz * 0.5f, box_sz * 0.5f), Vector2(box_sz, box_sz)), col, true);
	}

	// 4. Draw Units
	auto units = sim->units();
	for (size_t i = 0; i < units.count; ++i) {
		if (!units.alive[i]) continue;
		Vector2 pos = map_to_mini(units.pos_x[i], units.pos_z[i]);
		Color col = (units.team[i] == 0) ? Color(0.25f, 0.65f, 1.0f) : Color(0.95f, 0.25f, 0.2f);
		float rad = (units.unit_type[i] == rtso::UNIT_CAVALRY || units.unit_type[i] == rtso::UNIT_TRADER) ? 3.0f : 2.0f;
		draw_rect(Rect2(pos.x - rad * 0.5f, pos.y - rad * 0.5f, rad, rad), col, true);
	}

	// 5. Draw Tactical Map Pings
	for (const auto &p : pings) {
		float alpha = p.timer / 3.0f;
		float pulse_r = (1.0f - alpha) * 16.0f + 4.0f;
		Color c = p.color;
		c.a = alpha;
		draw_circle(p.mini_pos, pulse_r, Color(c.r, c.g, c.b, alpha * 0.3f));
		draw_arc(p.mini_pos, pulse_r, 0.0f, 6.2831853f, 16, c, 2.0f);
	}

	// 6. Draw Camera Viewport Frustum Box
	Vector2 cam_mini = map_to_mini(cam_pos.x, cam_pos.z);
	draw_rect(Rect2(cam_mini - Vector2(18, 14), Vector2(36, 28)), Color(1.0f, 1.0f, 1.0f, 0.6f), false, 1.5f);
}
