#ifndef RTSO_DEBUG_OVERLAYS_H
#define RTSO_DEBUG_OVERLAYS_H

// The developer overlay's simulation overlays (0 A.D. 0.28
// gui/session/developer_overlay/DeveloperOverlayControlCheckboxes.js, GPL-2.0-or-later):
// Pathfinder overlay (CCmpPathfinder's passability grid: the navcells land units can't
// cross), Obstruction overlay (CCmpObstructionManager::RenderSubmit: static shapes and
// still units blue, moving units magenta, the world bounds yellow), Unit motion overlay
// (CCmpUnitMotion::RenderSubmit: each unit's long path in white) and Range overlay
// (CCmpRangeManager::RenderSubmit: each attacker's range ring green, its MinRange ring
// red). Drawn for every entity whatever the line of sight, as 0 A.D. does, on one
// ImmediateMesh that follows the terrain.

#include <godot_cpp/classes/immediate_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/core/memory.hpp>

#include <algorithm>
#include <cstdint>

#include "../simulation/simulation.h"

namespace rtso {

class RtsoDebugOverlays {
public:
	enum Kind { PATHFINDER, OBSTRUCTION, UNIT_MOTION, RANGE, KIND_COUNT };

	void build(godot::Node *parent) {
		using namespace godot;
		mesh_.instantiate();
		inst_ = memnew(MeshInstance3D);
		inst_->set_name("DebugOverlays");
		inst_->set_mesh(mesh_);
		Ref<StandardMaterial3D> mat;
		mat.instantiate();
		mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		mat->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);
		mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		inst_->set_material_override(mat);
		inst_->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		parent->add_child(inst_);
	}

	void set(Kind k, bool on) { on_[k] = on; }
	bool on(Kind k) const { return on_[k]; }
	bool any() const { return on_[PATHFINDER] || on_[OBSTRUCTION] || on_[UNIT_MOTION] || on_[RANGE]; }
	// Shapes drawn by each overlay last update (tests).
	int drawn(Kind k) const { return drawn_[k]; }

	void update(const Simulation *sim) {
		if (!inst_ || (!any() && !inst_->is_visible())) return;
		mesh_->clear_surfaces();
		for (int &n : drawn_) n = 0;
		inst_->set_visible(sim && any());
		if (!sim || !any()) return;
		sim_ = sim;
		if (on_[PATHFINDER]) pathfinder();
		begun_ = false;
		if (on_[OBSTRUCTION]) obstruction();
		if (on_[UNIT_MOTION]) unit_motion();
		if (on_[RANGE]) range();
		if (begun_) mesh_->surface_end();
		sim_ = nullptr;
	}

private:
	using Color = godot::Color;
	using Vector3 = godot::Vector3;

	// Navcells a land unit (PASS_INFANTRY, 0 A.D.'s "default" class) can't enter, filled red,
	// deep water blue.
	void pathfinder() {
		const NavGrid &g = sim_->nav_grid();
		const float cell = g.map_half * 2.0f / (float)g.res;
		bool open = false;
		for (int cy = 0; cy < g.res; ++cy) {
			for (int cx = 0; cx < g.res; ++cx) {
				if (g.is_passable_for(cx, cy, PASS_INFANTRY)) continue;
				if (!open) {
					mesh_->surface_begin(godot::Mesh::PRIMITIVE_TRIANGLES);
					open = true;
				}
				const Color c = g.cells.at(cx, cy) == NAV_WATER ? Color(0.2f, 0.4f, 1.0f, 0.3f) : Color(1.0f, 0.2f, 0.1f, 0.35f);
				const float x0 = -g.map_half + cx * cell, z0 = -g.map_half + cy * cell;
				const Vector3 a = ground(x0, z0), b = ground(x0 + cell, z0), d = ground(x0, z0 + cell), e = ground(x0 + cell, z0 + cell);
				tri(a, b, e, c);
				tri(a, e, d, c);
				++drawn_[PATHFINDER];
			}
		}
		if (open) mesh_->surface_end();
	}

	void obstruction() {
		const Color still(0.0f, 0.0f, 1.0f, 1.0f), moving(1.0f, 0.0f, 1.0f, 1.0f), bounds(1.0f, 1.0f, 0.0f, 1.0f);
		for (const auto &b : sim_->buildings()) {
			if (b.wrecked) continue;
			box(b.x, b.z, b.ux, b.uz, b.hu, b.hv, still);
			++drawn_[OBSTRUCTION];
		}
		auto rv = sim_->resources();
		for (size_t r = 0; r < rv.count; ++r) {
			if (!rv.alive[r]) continue;
			nav::Footprint f;
			if (sim_->mine_footprint(r, f)) box(f.x, f.z, f.ux, f.uz, f.hu, f.hv, still);
			else {
				const float h = sim_->resource_block_half(r);
				box(rv.x[r], rv.z[r], 1.0f, 0.0f, h, h, still);
			}
			++drawn_[OBSTRUCTION];
		}
		auto uv = sim_->units();
		for (size_t i = 0; i < uv.count; ++i) {
			if (!uv.alive[i]) continue;
			const float h = unit_collision_radius(uv.unit_type[i]);
			const bool mv = uv.vel_x[i] * uv.vel_x[i] + uv.vel_z[i] * uv.vel_z[i] > 1e-6f;
			box(uv.pos_x[i], uv.pos_z[i], 1.0f, 0.0f, h, h, mv ? moving : still);
			++drawn_[OBSTRUCTION];
		}
		const float m = sim_->map_half();
		if (sim_->circular_map()) ring(0.0f, 0.0f, m, bounds);
		else box(0.0f, 0.0f, 1.0f, 0.0f, m, m, bounds);
	}

	void unit_motion() {
		const Color long_path(1.0f, 1.0f, 1.0f, 1.0f);
		auto uv = sim_->units();
		for (size_t i = 0; i < uv.count; ++i) {
			if (!uv.alive[i]) continue;
			const float *xs = nullptr, *zs = nullptr;
			const int n = sim_->unit_waypoints_left(i, &xs, &zs);
			if (n <= 0) continue;
			float px = uv.pos_x[i], pz = uv.pos_z[i];
			for (int k = 0; k < n; ++k) {
				line(px, pz, xs[k], zs[k], long_path);
				px = xs[k];
				pz = zs[k];
			}
			++drawn_[UNIT_MOTION];
		}
	}

	void range() {
		const Color enabled(0.0f, 1.0f, 0.0f, 1.0f), min_ring(1.0f, 0.0f, 0.0f, 1.0f);
		auto uv = sim_->units();
		for (size_t i = 0; i < uv.count; ++i) {
			if (!uv.alive[i]) continue;
			const float r = sim_->unit_attack_range_at(i);
			if (r <= 0.0f) continue;
			ring(uv.pos_x[i], uv.pos_z[i], r, enabled);
			const uint16_t t = uv.tmpl ? uv.tmpl[i] : CIV_UNIT_TEMPLATE_NONE;
			const float mr = r * unit_min_range_ratio(uv.unit_type[i], t);
			if (mr > 0.0f) ring(uv.pos_x[i], uv.pos_z[i], mr, min_ring);
			++drawn_[RANGE];
		}
		for (const auto &b : sim_->buildings()) {
			if (b.wrecked || b.build_progress < 1.0f) continue;
			const float r = sim_->building_arrow_range(b);
			if (r <= 0.0f) continue;
			ring(b.x, b.z, r + b.half_size, enabled);
			++drawn_[RANGE];
		}
	}

	Vector3 ground(float x, float z) const { return Vector3(x, sim_->get_terrain_height(x, z) + 0.2f, z); }
	void begin() {
		if (begun_) return;
		mesh_->surface_begin(godot::Mesh::PRIMITIVE_LINES);
		begun_ = true;
	}
	void tri(const Vector3 &a, const Vector3 &b, const Vector3 &c, const Color &col) {
		for (const Vector3 *v : { &a, &b, &c }) {
			mesh_->surface_set_color(col);
			mesh_->surface_add_vertex(*v);
		}
	}
	void seg(const Vector3 &a, const Vector3 &b, const Color &c) {
		begin();
		mesh_->surface_set_color(c);
		mesh_->surface_add_vertex(a);
		mesh_->surface_set_color(c);
		mesh_->surface_add_vertex(b);
	}
	// A straight line, split so it follows the ground.
	void line(float x0, float z0, float x1, float z1, const Color &c) {
		const float len = godot::Math::sqrt((x1 - x0) * (x1 - x0) + (z1 - z0) * (z1 - z0));
		const int n = std::clamp((int)(len / 1.5f), 1, 64);
		Vector3 prev = ground(x0, z0);
		for (int k = 1; k <= n; ++k) {
			const float t = (float)k / n;
			const Vector3 p = ground(x0 + (x1 - x0) * t, z0 + (z1 - z0) * t);
			seg(prev, p, c);
			prev = p;
		}
	}
	// A rectangle centred on (x, z), half extents hu along (ux, uz) and hv across it.
	void box(float x, float z, float ux, float uz, float hu, float hv, const Color &c) {
		const float vx = -uz, vz = ux;
		const float px[4] = { x + ux * hu + vx * hv, x - ux * hu + vx * hv, x - ux * hu - vx * hv, x + ux * hu - vx * hv };
		const float pz[4] = { z + uz * hu + vz * hv, z - uz * hu + vz * hv, z - uz * hu - vz * hv, z + uz * hu - vz * hv };
		for (int k = 0; k < 4; ++k) line(px[k], pz[k], px[(k + 1) % 4], pz[(k + 1) % 4], c);
	}
	void ring(float cx, float cz, float r, const Color &c) {
		const int steps = std::clamp((int)(r * 3.0f), 16, 256);
		Vector3 prev = ground(cx + r, cz);
		for (int k = 1; k <= steps; ++k) {
			const float t = (float)k / steps * (float)Math_TAU;
			const Vector3 p = ground(cx + r * godot::Math::cos(t), cz + r * godot::Math::sin(t));
			seg(prev, p, c);
			prev = p;
		}
	}

	bool on_[KIND_COUNT] = {};
	int drawn_[KIND_COUNT] = {};
	const Simulation *sim_ = nullptr;
	godot::Ref<godot::ImmediateMesh> mesh_;
	godot::MeshInstance3D *inst_ = nullptr;
	bool begun_ = false;
};

} // namespace rtso

#endif
