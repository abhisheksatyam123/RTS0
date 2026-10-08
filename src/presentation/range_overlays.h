#ifndef RTSO_RANGE_OVERLAYS_H
#define RTSO_RANGE_OVERLAYS_H

// Ground overlays for the selection (0 A.D. gui/session/RangeOverlayManager.js
// and the rally point line of RallyPointRenderer): attack range rings of
// selected defensive structures (session.toggleattackrange, Alt+C), aura rings
// (session.toggleaurasrange, Alt+V), healer range rings
// (session.togglehealrange, Alt+B), and a line from a selected structure to its
// rally point. Drawn as one ImmediateMesh that follows the terrain.

#include <godot_cpp/classes/immediate_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/core/memory.hpp>

#include <cstdint>

#include "../simulation/simulation.h"
#include "attack_detection.h" // kMetresPerUnit

using namespace godot;

class RtsoRangeOverlays {
public:
	// 0 A.D. [gui.session] attackrange / aurasrange / healrange default to true.
	bool show_attack = true;
	bool show_auras = true;
	bool show_heal = true;

	void build(Node *parent) {
		mesh_.instantiate();
		inst_ = memnew(MeshInstance3D);
		inst_->set_mesh(mesh_);
		Ref<StandardMaterial3D> mat;
		mat.instantiate();
		mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		mat->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);
		inst_->set_material_override(mat);
		inst_->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		parent->add_child(inst_);
	}

	void update(const rtso::Simulation *sim, uint8_t me, uint32_t building_id, const int32_t *unit_ids, int n_units) {
		if (!inst_ || !sim) return;
		mesh_->clear_surfaces();
		begun_ = false;
		if (building_id != 0) {
			for (const auto &b : sim->buildings()) {
				if (b.id != building_id) continue;
				const auto &info = rtso::building_type_info(b.type);
				if (show_attack && info.attack_range > 0.0f && b.build_progress >= 1.0f)
					ring(sim, b.x, b.z, info.attack_range, Color(1.0f, 0.3f, 0.25f, 0.7f));
				if (b.team == me && b.has_rally_point)
					line(sim, b.x, b.z, b.rally_x, b.rally_z, Color(1.0f, 1.0f, 1.0f, 0.75f));
				break;
			}
		}
		if (n_units > 0 && (show_auras || show_heal)) {
			auto uv = sim->units();
			for (int s = 0; s < n_units; ++s) {
				for (size_t i = 0; i < uv.count; ++i) {
					if (uv.id[i] != (uint32_t)unit_ids[s] || !uv.alive[i]) continue;
					if (show_heal && uv.unit_type[i] == rtso::UNIT_HEALER)
						ring(sim, uv.pos_x[i], uv.pos_z[i], sim->healer_range(uv.team[i]), Color(0.3f, 1.0f, 0.4f, 0.6f));
					if (show_auras && uv.tmpl && uv.tmpl[i] != rtso::CIV_UNIT_TEMPLATE_NONE) {
						for (int16_t a : rtso::civ_unit_template(uv.tmpl[i]).auras) {
							if (a < 0) continue;
							const auto &aura = rtso::civ_hero_aura(a);
							const float r = aura.range > 0.0f ? aura.range : aura.radius / rtso::AttackDetection::kMetresPerUnit;
							if (r > 0.0f) ring(sim, uv.pos_x[i], uv.pos_z[i], r, Color(0.85f, 0.5f, 1.0f, 0.6f));
						}
					}
					break;
				}
			}
		}
		if (begun_) mesh_->surface_end();
	}

private:
	void begin() {
		if (begun_) return;
		mesh_->surface_begin(Mesh::PRIMITIVE_LINES);
		begun_ = true;
	}
	Vector3 ground(const rtso::Simulation *sim, float x, float z) const {
		return Vector3(x, sim->get_terrain_height(x, z) + 0.25f, z);
	}
	void seg(const Vector3 &a, const Vector3 &b, const Color &c) {
		mesh_->surface_set_color(c);
		mesh_->surface_add_vertex(a);
		mesh_->surface_set_color(c);
		mesh_->surface_add_vertex(b);
	}
	void ring(const rtso::Simulation *sim, float cx, float cz, float r, const Color &c) {
		begin();
		const int steps = std::max(24, (int)(r * 3.0f));
		Vector3 prev = ground(sim, cx + r, cz);
		for (int k = 1; k <= steps; ++k) {
			const float t = (float)k / steps * (float)Math_TAU;
			const Vector3 p = ground(sim, cx + r * Math::cos(t), cz + r * Math::sin(t));
			seg(prev, p, c);
			prev = p;
		}
	}
	// Dashed like 0 A.D.'s rally line.
	void line(const rtso::Simulation *sim, float x0, float z0, float x1, float z1, const Color &c) {
		begin();
		const float len = Math::sqrt((x1 - x0) * (x1 - x0) + (z1 - z0) * (z1 - z0));
		const int dashes = std::max(1, (int)(len / 1.5f));
		for (int k = 0; k < dashes; ++k) {
			const float t0 = (float)k / dashes, t1 = ((float)k + 0.6f) / dashes;
			seg(ground(sim, x0 + (x1 - x0) * t0, z0 + (z1 - z0) * t0), ground(sim, x0 + (x1 - x0) * t1, z0 + (z1 - z0) * t1), c);
		}
	}

	Ref<ImmediateMesh> mesh_;
	MeshInstance3D *inst_ = nullptr;
	bool begun_ = false;
};

#endif
