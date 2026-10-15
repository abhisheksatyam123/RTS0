#ifndef RTSO_RALLY_POINT_RENDERER_H
#define RTSO_RALLY_POINT_RENDERER_H

// Rally points of the selected structures, 0 A.D.'s CCmpRallyPointRenderer
// (the RallyPointRenderer of template_structure.xml). GuiInterface
// DisplayRallyPoint shows them for every selected structure the player owns.
// Each position gets a special/rallypoint marker: the waving flag of
// art/actors/props/special/common/waypoint_flag.xml with the structure's civ
// banner (Idle at speed 30). A textured line (rallypoint_line.png and its
// mask, LineThickness 0.25 m on each side) runs from the structure to the
// first flag and on from flag to flag. Over explored land it takes the
// owner's displayed colour; across unexplored land it becomes dashes of
// LineDashColor (158, 11, 15), 3 m long with 3 m gaps, fitted to end on a
// full dash. Each leg has a square cap where it starts (LineStartCap) and a
// round cap at its flag (LineEndCap). 0 A.D. bends the line along a
// short-range path around obstructions; RTS0 draws straight legs that follow
// the terrain.

#include <godot_cpp/classes/animation.hpp>
#include <godot_cpp/classes/animation_player.hpp>
#include <godot_cpp/classes/immediate_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/core/memory.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "../simulation/grid_nav.h"
#include "../simulation/simulation.h"

using namespace godot;

class RtsoRallyPoints {
public:
	// RTS0 units (1 unit = 4 m).
	static constexpr float kHalfWidth = 0.25f / 4.0f;  // LineThickness
	static constexpr float kDash = 3.0f / 4.0f;        // maxDashSize, maxClearSize
	static constexpr float kStep = 0.5f;               // terrain samples along a leg
	static constexpr float kLift = 0.05f;              // OVERLAY_VOFFSET 0.2 m
	static constexpr int kCapPoints = 8;

	void build(Node *parent) {
		parent_ = parent;
		ResourceLoader *rl = ResourceLoader::get_singleton();
		flag_scene_ = rl->load("res://assets/models/props/waypoint_flag.dae");
		mesh_.instantiate();
		line_ = memnew(MeshInstance3D);
		line_->set_mesh(mesh_);
		line_->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		// overlayline.fs: the mask picks where the line colour replaces the
		// texture (its dark core); the white rims and alpha come from the texture.
		Ref<Shader> sh;
		sh.instantiate();
		sh->set_code(R"(shader_type spatial;
render_mode unshaded, blend_mix, cull_disabled, depth_draw_never, shadows_disabled, fog_disabled;
uniform sampler2D base_tex : source_color, filter_linear, repeat_enable;
uniform sampler2D mask_tex : filter_linear, repeat_enable;
void fragment() {
	vec4 b = texture(base_tex, UV);
	ALBEDO = mix(b.rgb, COLOR.rgb, texture(mask_tex, UV).r);
	ALPHA = b.a;
}
)");
		Ref<ShaderMaterial> m;
		m.instantiate();
		m->set_shader(sh);
		m->set_shader_parameter("base_tex", rl->load("res://assets/textures/misc/rallypoint_line.png"));
		m->set_shader_parameter("mask_tex", rl->load("res://assets/textures/misc/rallypoint_line_mask.png"));
		line_->set_material_override(m);
		parent->add_child(line_);
	}

	// Draws the rally points of the listed structures that `me` owns, the line in `color`.
	void update(const rtso::Simulation *sim, uint8_t me, const std::vector<uint32_t> &structures, const Color &color) {
		if (!line_) return;
		mesh_->clear_surfaces();
		begun_ = false;
		size_t used = 0;
		if (sim && me < rtso::MAX_PLAYERS && !structures.empty()) {
			for (const auto &b : sim->buildings()) {
				if (b.team != me || !b.has_rally_point) continue;
				if (std::find(structures.begin(), structures.end(), b.id) == structures.end()) continue;
				const uint8_t civ = b.civ < rtso::CIV_COUNT ? b.civ : sim->team_civ(b.team);
				Vector2 from(b.x, b.z);
				for (size_t k = 0; k <= b.rally_more.size(); ++k) {
					const Vector2 to = k == 0 ? Vector2(b.rally_x, b.rally_z) : Vector2(b.rally_more[k - 1].x, b.rally_more[k - 1].z);
					draw_leg(sim, me, from, to, color);
					place_flag(sim, used++, to, civ);
					from = to;
				}
			}
		}
		for (size_t i = used; i < flags_.size(); ++i)
			if (flags_[i].node) flags_[i].node->set_visible(false);
		shown_ = (int)used;
		if (begun_) mesh_->surface_end();
	}

	// Flags drawn this frame (tests).
	int flag_count() const { return shown_; }

private:
	struct Flag {
		Node3D *node = nullptr;
		uint8_t civ = 0xFF;
		std::vector<MeshInstance3D *> meshes;
	};

	// The civ banners of the waypoint flag (0.28 has one rally point template
	// per civ, special/rallypoints/{civ}). The Germans fly the Celtic banner
	// the Britons and Gauls share; Gaia takes the first one, the Athenian.
	static const char *banner_of(uint8_t civ) {
		switch (civ) {
			case rtso::CIV_SPARTANS: return "spartans";
			case rtso::CIV_PERSIANS: return "persian";
			case rtso::CIV_ROMANS: return "romans";
			case rtso::CIV_CARTHAGINIANS: return "carthage";
			case rtso::CIV_MACEDONIANS: return "macedonians";
			case rtso::CIV_BRITONS:
			case rtso::CIV_GAULS:
			case rtso::CIV_GERMANS: return "celt";
			case rtso::CIV_IBERIANS: return "iberians";
			case rtso::CIV_MAURYANS: return "mauryas";
			case rtso::CIV_KUSHITES: return "kushites";
			case rtso::CIV_PTOLEMIES: return "ptolemies";
			case rtso::CIV_SELEUCIDS: return "seleucids";
			case rtso::CIV_HAN: return "han";
			default: return "athenians";
		}
	}

	// basic_trans_norm_spec: the banner's alpha cuts the cloth's outline.
	Ref<Material> banner_material(uint8_t civ) {
		const char *name = banner_of(civ);
		for (const auto &e : banner_mats_)
			if (e.first == name) return e.second;
		Ref<StandardMaterial3D> m;
		m.instantiate();
		m->set_texture(BaseMaterial3D::TEXTURE_ALBEDO,
				ResourceLoader::get_singleton()->load(String("res://assets/textures/waypoint_flags/banner_") + name + ".png"));
		m->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
		m->set_alpha_scissor_threshold(0.5f);
		m->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		m->set_roughness(0.8f);
		banner_mats_.push_back({ name, m });
		return m;
	}

	// The terrain under a point, or the water over it (TexturedLineRData clamps
	// to the water level; the marker actor has <float/>).
	static float surface(const rtso::Simulation *sim, float x, float z) {
		float y = sim->get_terrain_height(x, z);
		if (sim->water_height() > -0.5f) y = std::max(y, sim->water_height() + rtso::NAV_WATER_MARGIN);
		return y;
	}

	void place_flag(const rtso::Simulation *sim, size_t i, const Vector2 &p, uint8_t civ) {
		if (!parent_ || flag_scene_.is_null()) return;
		if (i >= flags_.size()) flags_.resize(i + 1);
		Flag &f = flags_[i];
		if (!f.node) {
			f.node = Object::cast_to<Node3D>(flag_scene_->instantiate());
			if (!f.node) return;
			// Godot scales a Collada mesh by its <unit> (inch); 0 A.D.'s converter
			// ignores it. Then 0 A.D. metres to RTS0 units.
			const float s = 0.25f / 0.0254f;
			f.node->set_scale(Vector3(s, s, s));
			TypedArray<Node> meshes = f.node->find_children("*", "MeshInstance3D", true, false);
			for (int m = 0; m < meshes.size(); ++m) {
				MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(meshes[m]);
				mi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_ON); // <castshadow/>
				f.meshes.push_back(mi);
			}
			TypedArray<Node> players = f.node->find_children("*", "AnimationPlayer", true, false);
			if (players.size() > 0) {
				AnimationPlayer *ap = Object::cast_to<AnimationPlayer>(players[0]);
				PackedStringArray names = ap->get_animation_list();
				if (names.size() > 0) {
					Ref<Animation> anim = ap->get_animation(names[0]);
					if (anim.is_valid()) anim->set_loop_mode(Animation::LOOP_LINEAR);
					ap->set_speed_scale(0.3); // Idle speed="30"
					ap->play(names[0]);
					ap->seek(0.37 * (double)i, true); // out of step
				}
			}
			parent_->add_child(f.node);
		}
		if (f.civ != civ) {
			const Ref<Material> mat = banner_material(civ);
			for (MeshInstance3D *mi : f.meshes) mi->set_material_override(mat);
			f.civ = civ;
		}
		f.node->set_position(Vector3(p.x, surface(sim, p.x, p.y), p.y));
		f.node->set_visible(true);
	}

	void begin() {
		if (begun_) return;
		mesh_->surface_begin(Mesh::PRIMITIVE_TRIANGLES);
		begun_ = true;
	}
	void vert(const Vector3 &p, float u, float v, const Color &c) {
		mesh_->surface_set_color(c);
		mesh_->surface_set_uv(Vector2(u, v));
		mesh_->surface_add_vertex(p);
	}
	Vector3 ground(const rtso::Simulation *sim, const Vector2 &p) const {
		return Vector3(p.x, surface(sim, p.x, p.y) + kLift, p.y);
	}

	// One leg: solid runs over explored land, dashed runs over unexplored land
	// (GetVisibilitySegments uses IsExplored at each path point).
	void draw_leg(const rtso::Simulation *sim, uint8_t me, const Vector2 &a, const Vector2 &b, const Color &color) {
		const float len = a.distance_to(b);
		if (len < 1e-3f) return;
		const int n = std::max(1, (int)std::ceil(len / kStep));
		std::vector<Vector2> pts(n + 1);
		std::vector<bool> seen(n + 1);
		for (int i = 0; i <= n; ++i) {
			pts[i] = a.lerp(b, (float)i / n);
			seen[i] = sim->los_explored(me, pts[i].x, pts[i].y);
		}
		static const Color dash_color(158 / 255.0f, 11 / 255.0f, 15 / 255.0f);
		int s = 0;
		while (s < n) {
			int e = s + 1;
			while (e < n && seen[e] == seen[s]) ++e;
			// A run takes the points s..e; it starts the leg with its square cap
			// and ends it with the round one, and borders a dashed run with round caps.
			if (seen[s]) {
				ribbon(sim, std::vector<Vector2>(pts.begin() + s, pts.begin() + e + 1), color, s == 0, true);
			} else {
				dashes(sim, pts[s], pts[e], dash_color);
			}
			s = e;
		}
	}

	// SimRender::ConstructDashedLine with the dash fitting of ConstructOverlayLines.
	void dashes(const rtso::Simulation *sim, const Vector2 &a, const Vector2 &b, const Color &c) {
		const float dist = a.distance_to(b);
		float dash = kDash, clear = kDash;
		const float ratio = dash / (dash + clear);
		int fit = (int)std::floor(dist / (dash + clear));
		const float rem = dist - fit * (dash + clear);
		fit = std::max(1, fit);
		const float diff = (rem - kDash) / fit;
		dash += ratio * diff;
		clear += (1.0f - ratio) * diff;
		if (dash <= 0.0f) dash = dist;
		const Vector2 dir = (b - a) / dist;
		for (float t = 0.0f; t < dist - 1e-4f; t += dash + std::max(clear, 0.0f)) {
			const float t1 = std::min(dist, t + dash);
			const int k = std::max(1, (int)std::ceil((t1 - t) / kStep));
			std::vector<Vector2> d(k + 1);
			for (int i = 0; i <= k; ++i) d[i] = a + dir * (t + (t1 - t) * i / k);
			ribbon(sim, d, c, false, true);
			if (clear <= 0.0f) break;
		}
	}

	// TexturedLineRData::Update: a strip whose u runs across the line (0 on
	// one side, 1 on the other), with caps.
	void ribbon(const rtso::Simulation *sim, const std::vector<Vector2> &p, const Color &c, bool square_start, bool round_end) {
		const size_t n = p.size();
		if (n < 2) return;
		begin();
		std::vector<Vector3> l(n), r(n);
		for (size_t i = 0; i < n; ++i) {
			const Vector2 d0 = (i > 0 ? p[i] - p[i - 1] : p[1] - p[0]).normalized();
			const Vector2 d1 = (i + 1 < n ? p[i + 1] - p[i] : p[i] - p[i - 1]).normalized();
			Vector2 side = Vector2(-(d0.y + d1.y), d0.x + d1.x);
			const float sl = side.length();
			side = sl > 1e-5f ? side / sl : Vector2(-d1.y, d1.x);
			const float cosv = std::max(0.3f, side.dot(Vector2(-d1.y, d1.x)));
			const Vector2 off = side * (kHalfWidth / cosv);
			l[i] = ground(sim, p[i] + off);
			r[i] = ground(sim, p[i] - off);
		}
		for (size_t i = 0; i + 1 < n; ++i) {
			const float v0 = (float)(i & 1), v1 = (float)((i + 1) & 1);
			vert(l[i], 0.0f, v0, c); vert(r[i], 1.0f, v0, c); vert(l[i + 1], 0.0f, v1, c);
			vert(l[i + 1], 0.0f, v1, c); vert(r[i], 1.0f, v0, c); vert(r[i + 1], 1.0f, v1, c);
		}
		const Vector2 d_start = (p[1] - p[0]).normalized();
		const Vector2 d_end = (p[n - 1] - p[n - 2]).normalized();
		if (square_start) {
			// LINECAP_SQUARE: the strip pushed back by its half-width.
			const Vector3 back(-d_start.x * kHalfWidth, 0.0f, -d_start.y * kHalfWidth);
			const Vector3 lb = ground(sim, Vector2(l[0].x + back.x, l[0].z + back.z));
			const Vector3 rb = ground(sim, Vector2(r[0].x + back.x, r[0].z + back.z));
			vert(lb, 0.0f, 1.0f, c); vert(rb, 1.0f, 1.0f, c); vert(l[0], 0.0f, 0.0f, c);
			vert(l[0], 0.0f, 0.0f, c); vert(rb, 1.0f, 1.0f, c); vert(r[0], 1.0f, 0.0f, c);
		} else {
			round_cap(sim, p[0], -d_start, c);
		}
		if (round_end) round_cap(sim, p[n - 1], d_end, c);
	}

	// LINECAP_ROUND: a half disc fanned from the line's centre (u 0.5) out to
	// its rim (u 0), so the texture's white edge rounds the end too.
	void round_cap(const rtso::Simulation *sim, const Vector2 &at, const Vector2 &dir, const Color &c) {
		const Vector2 side(-dir.y, dir.x);
		const Vector3 centre = ground(sim, at);
		Vector3 prev = ground(sim, at + side * kHalfWidth);
		for (int k = 1; k <= kCapPoints; ++k) {
			const float a = (float)Math_PI * k / kCapPoints;
			const Vector2 q = at + (side * Math::cos(a) + dir * Math::sin(a)) * kHalfWidth;
			const Vector3 cur = ground(sim, q);
			vert(centre, 0.5f, 0.5f, c); vert(prev, 0.0f, 0.5f, c); vert(cur, 0.0f, 0.5f, c);
			prev = cur;
		}
	}

	Node *parent_ = nullptr;
	Ref<PackedScene> flag_scene_;
	std::vector<Flag> flags_;
	std::vector<std::pair<std::string, Ref<Material>>> banner_mats_;
	Ref<ImmediateMesh> mesh_;
	MeshInstance3D *line_ = nullptr;
	bool begun_ = false;
	int shown_ = 0;
};

#endif
