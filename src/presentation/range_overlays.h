#ifndef RTSO_RANGE_OVERLAYS_H
#define RTSO_RANGE_OVERLAYS_H

// Ground overlays for the selected and highlighted entities (0 A.D.
// gui/session/RangeOverlayManager.js): attack range rings of defensive structures (session.toggleattackrange, Alt+C), aura rings
// (session.toggleaurasrange, Alt+V), healer range rings
// (session.togglehealrange, Alt+B), drawn as CCmpRangeOverlayRenderer does: a
// textured line round each range (outline_border.png, or heal_overlay_range.png's
// dashes for healers) in the owner's displayed colour, following the terrain
// (rally points: rally_point_renderer.h); and target
// markers, 0 A.D.'s own actors: special/target_marker (four arrows closing on
// where a walk order was given, decaying after 0.5 s) and
// special/flare_target_marker (an exclamation mark in the sender's colour for
// 6 s), each playing its Idle animation (assets/models/props/*.dae).

#include <godot_cpp/classes/animation.hpp>
#include <godot_cpp/classes/animation_player.hpp>
#include <godot_cpp/classes/immediate_mesh.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/core/memory.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <cstdint>
#include <vector>

#include "../simulation/simulation.h"
#include "attack_detection.h" // kMetresPerUnit
#include "display_colors.h"

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
		parent_ = parent;
		// overlayline.fs: the mask picks where the owner's colour replaces the texture;
		// the white rims and alpha come from the texture.
		Ref<Shader> sh;
		sh.instantiate();
		sh->set_code(R"(shader_type spatial;
render_mode unshaded, blend_mix, cull_disabled, depth_draw_never, shadows_disabled, fog_disabled;
uniform sampler2D base_tex : source_color, filter_linear, repeat_enable;
uniform sampler2D mask_tex : filter_linear, repeat_enable;
void fragment() {
	vec4 b = texture(base_tex, UV);
	ALBEDO = mix(b.rgb, COLOR.rgb, texture(mask_tex, UV).r);
	ALPHA = b.a * COLOR.a;
}
)");
		ResourceLoader *rl = ResourceLoader::get_singleton();
		const char *names[LINE_KINDS] = {"outline_border", "heal_overlay_range"};
		for (int k = 0; k < LINE_KINDS; ++k) {
			lines_[k].mesh.instantiate();
			lines_[k].inst = memnew(MeshInstance3D);
			lines_[k].inst->set_mesh(lines_[k].mesh);
			lines_[k].inst->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
			Ref<ShaderMaterial> m;
			m.instantiate();
			m->set_shader(sh);
			m->set_shader_parameter("base_tex", rl->load(String("res://assets/textures/selection/") + names[k] + ".png"));
			m->set_shader_parameter("mask_tex", rl->load(String("res://assets/textures/selection/") + names[k] + "_mask.png"));
			m->set_render_priority(1); // over the water, which is drawn transparent too
			lines_[k].inst->set_material_override(m);
			parent->add_child(lines_[k].inst);
		}
		load_actors();
	}

	// GuiInterface.AddTargetMarker. Decay DelayTime: target_marker 0.5 s, flare_target_marker 6 s.
	static constexpr float kMoveMarkerTime = 0.5f;
	static constexpr float kFlareMarkerTime = 6.0f;
	void add_target_marker(float x, float z) { add(x, z, Color(1, 1, 1, 0.9f), kMoveMarkerTime, false); }
	// flare_target_marker has an Ownership: the sender's colour.
	void add_flare(float x, float z, const Color &c, float ttl) { add(x, z, c, ttl, true); }
	// Ages the markers by real (unscaled) time. Decay SinkRate 10000: gone at once.
	void tick(float dt) {
		for (auto &m : markers_) {
			m.ttl -= dt;
			if (m.ttl <= 0.0f && m.actor) {
				m.actor->queue_free();
				m.actor = nullptr;
			}
		}
		std::erase_if(markers_, [](const Marker &m) { return m.ttl <= 0.0f; });
	}
	// Markers drawn with their 0 A.D. actor (tests).
	int marker_actor_count() const {
		int n = 0;
		for (const auto &m : markers_) n += m.actor != nullptr;
		return n;
	}
	size_t marker_count() const { return markers_.size(); }
	// Radii of the rings the last update drew (tests).
	const std::vector<float> &ring_radii() const { return radii_; }

	// GuiInterface.SetBuildingPlacementPreview enables the preview's RangeOverlayManager:
	// a structure being placed shows its Attack MinRange and MaxRange rings (preview.xml
	// keeps Attack for them). max_r 0 clears it.
	void set_preview(float x, float z, float max_r, float min_r, uint8_t owner) { preview_ = {x, z, max_r, min_r, owner}; }

	// RangeOverlayManager: the selection and the highlighted (hovered or band-boxed)
	// entities. GuiInterface.SetSelectionHighlight turns the overlays on only for
	// the viewer's own entities (an observer sees everyone's).
	void update(const rtso::Simulation *sim, uint8_t me, const std::vector<uint32_t> &ids) {
		if (!inst_ || !sim) return;
		mesh_->clear_surfaces();
		begun_ = false;
		for (auto &l : lines_) {
			l.mesh->clear_surfaces();
			l.begun = false;
		}
		radii_.clear();
		for (auto &m : markers_) {
			if (m.actor) m.actor->set_position(Vector3(m.x, sim->get_terrain_height(m.x, m.z), m.z));
			else flag(sim, m.x, m.z, m.color);
		}
		ids_ = ids;
		std::sort(ids_.begin(), ids_.end());
		auto listed = [&](uint32_t id) { return std::binary_search(ids_.begin(), ids_.end(), id); };
		auto shown_for = [&](uint8_t team) { return me >= rtso::MAX_PLAYERS || team == me; };
		if (show_attack && preview_.max_r > 0.0f) {
			const Color c = owner_color(sim, preview_.owner);
			ring(sim, preview_.x, preview_.z, preview_.max_r, c, BORDER, kAttackThickness);
			if (preview_.min_r > 0.0f) ring(sim, preview_.x, preview_.z, preview_.min_r, c, BORDER, kAttackThickness);
		}
		if (ids_.empty()) {
			end_all();
			return;
		}
		// Where each listed entity stands now (the AuraManager keeps the spot of its last rebuild).
		spots_.clear();
		for (const auto &b : sim->buildings()) {
			if (!listed(b.id) || !shown_for(b.team) || b.build_progress < 1.0f) continue;
			spots_.push_back({b.id, b.x, b.z, b.team});
			// Attack.js GetRangeOverlays: the Attack's MinRange and MaxRange rings.
			if (show_attack && sim->building_arrow_range(b) > 0.0f) {
				const rtso::Simulation::StructureDetails d = sim->structure_details(b);
				const Color c = owner_color(sim, b.team);
				ring(sim, b.x, b.z, d.max_range + b.half_size, c, BORDER, kAttackThickness);
				if (d.min_range > 0.0f) ring(sim, b.x, b.z, d.min_range + b.half_size, c, BORDER, kAttackThickness);
			}
		}
		auto uv = sim->units();
		for (size_t i = 0; i < uv.count; ++i) {
			if (!uv.alive[i] || !listed(uv.id[i]) || !shown_for(uv.team[i])) continue;
			spots_.push_back({uv.id[i], uv.pos_x[i], uv.pos_z[i], uv.team[i]});
			const Color c = owner_color(sim, uv.team[i]);
			const uint16_t ht = uv.tmpl ? uv.tmpl[i] : rtso::CIV_UNIT_TEMPLATE_NONE;
			if (show_heal && rtso::unit_is_healer(ht, uv.unit_type[i])) {
				const float extra = (rtso::unit_heal_range_base(ht, uv.unit_type[i]) - 12.0f) * rtso::COMBAT_DISTANCE_SCALE;
				ring(sim, uv.pos_x[i], uv.pos_z[i], sim->healer_range(uv.team[i]) + extra, c, HEAL, kHealThickness);
			}
			// The siege templates with an Attack RangeOverlay: stone and bolt throwers, the
			// flamethrower, siege towers and siege warships.
			if (show_attack && has_attack_overlay(ht)) {
				const float reach = sim->attack_reach(i);
				const float min_ratio = rtso::unit_min_range_ratio(uv.unit_type[i], ht);
				if (reach > 0.0f) {
					ring(sim, uv.pos_x[i], uv.pos_z[i], reach, c, BORDER, kAttackThickness);
					if (min_ratio > 0.0f) ring(sim, uv.pos_x[i], uv.pos_z[i], reach * min_ratio, c, BORDER, kAttackThickness);
				}
			}
		}
		// Auras.js GetRangeOverlays: a ring for every applied range aura, on structures and
		// units alike, at the range the sim applies it (global auras reach 1000 and get none).
		if (show_auras) {
			std::sort(spots_.begin(), spots_.end(), [](const Spot &a, const Spot &b) { return a.id < b.id; });
			drawn_.clear();
			for (const auto &src : sim->aura_manager().sources) {
				if (!src.alive || src.range <= 0.0f || src.range >= 500.0f || !listed(src.source_id)) continue;
				auto it = std::lower_bound(spots_.begin(), spots_.end(), src.source_id,
						[](const Spot &a, uint32_t id) { return a.id < id; });
				if (it == spots_.end() || it->id != src.source_id) continue;
				// One ring per distinct range of an entity: an aura's several effects share it.
				const std::pair<uint32_t, int> key{src.source_id, (int)std::lround(src.range * 100.0f)};
				if (std::find(drawn_.begin(), drawn_.end(), key) != drawn_.end()) continue;
				drawn_.push_back(key);
				ring(sim, it->x, it->z, src.range, owner_color(sim, it->team), BORDER, aura_thickness(src));
			}
		}
		end_all();
	}

private:
	struct Spot {
		uint32_t id;
		float x, z;
		uint8_t team;
	};
	// RangeOverlay LineThickness, metres either side of the line: Attack's 0.175
	// (template_structure_defensive* and the siege templates), Heal.js's 0.35, and
	// Auras.js's default 0.2 (farmstead_60, the rotary mill's, sets 0.075).
	static constexpr float kAttackThickness = 0.175f, kHealThickness = 0.35f;
	static float aura_thickness(const rtso::AuraSource &src) {
		if (src.type == rtso::AURA_GATHER_RATE) {
			const int row = (int)src.value;
			if (row >= 0 && row < (int)rtso::GATHER_RATE_RANGE_AURA_COUNT &&
					std::strcmp(rtso::gather_rate_range_auras()[row].id, "farmstead_60") == 0)
				return 0.075f;
		}
		return 0.2f;
	}
	// CCmpRangeOverlayRenderer::UpdateColor: the owner's displayed colour.
	static Color owner_color(const rtso::Simulation *sim, uint8_t team) {
		const uint32_t rgb = rtso::display_colors::rgb(*sim, team);
		return Color(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1.0f);
	}
	enum LineKind { BORDER, HEAL, LINE_KINDS };
	struct Line {
		Ref<ImmediateMesh> mesh;
		MeshInstance3D *inst = nullptr;
		bool begun = false;
	};
	Line lines_[LINE_KINDS];
	void end_all() {
		if (begun_) mesh_->surface_end();
		for (auto &l : lines_)
			if (l.begun) l.mesh->surface_end();
	}
	std::vector<Spot> spots_;
	std::vector<float> radii_;
	struct Preview {
		float x = 0.0f, z = 0.0f, max_r = 0.0f, min_r = 0.0f;
		uint8_t owner = 0;
	} preview_;
	std::vector<std::pair<uint32_t, int>> drawn_;
	static bool has_attack_overlay(uint16_t tmpl) {
		if (tmpl >= rtso::CIV_UNIT_TEMPLATE_COUNT) return false;
		const rtso::CivUnitTemplate &t = rtso::civ_unit_template(tmpl);
		if (t.flags & rtso::ROSTER_SIEGE_TOWER) return true;
		const char *id = t.id;
		return std::strstr(id, "ship_siege") || std::strstr(id, "fireraiser") ||
				(std::strstr(id, "/siege_") && !std::strstr(id, "siege_ram") && !std::strstr(id, "siege_tower"));
	}
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
	// SimRender::ConstructTexturedLineCircle: a closed line a third of its thickness
	// outside the range, a point every third of a tile (4/3 m); TexturedLineRData:
	// u across the line, v flipping between 0 and 1 at each point, on the terrain
	// or the water over it.
	void ring(const rtso::Simulation *sim, float cx, float cz, float r, const Color &c, LineKind kind, float thickness_m) {
		radii_.push_back(r);
		Line &l = lines_[kind];
		if (!l.begun) {
			l.mesh->surface_begin(Mesh::PRIMITIVE_TRIANGLES);
			l.begun = true;
		}
		const float half = thickness_m / rtso::AttackDetection::kMetresPerUnit;
		const float rr = r + half / 3.0f;
		const int steps = std::max(12, (int)std::ceil((float)Math_TAU * rr * 3.0f));
		auto at = [&](int k, float rad) {
			const float t = (float)(k % steps) / steps * (float)Math_TAU;
			float y = sim->get_terrain_height(cx + rad * Math::sin(t), cz + rad * Math::cos(t));
			if (sim->water_height() > -0.5f) y = std::max(y, sim->water_height());
			return Vector3(cx + rad * Math::sin(t), y + 0.1f, cz + rad * Math::cos(t));
		};
		Vector3 po = at(0, rr + half), pi = at(0, rr - half);
		for (int k = 1; k <= steps; ++k) {
			const Vector3 no = at(k, rr + half), ni = at(k, rr - half);
			const float v0 = (float)((k - 1) & 1), v1 = (float)(k & 1);
			vert(l, po, 0.0f, v0, c); vert(l, pi, 1.0f, v0, c); vert(l, no, 0.0f, v1, c);
			vert(l, no, 0.0f, v1, c); vert(l, pi, 1.0f, v0, c); vert(l, ni, 1.0f, v1, c);
			po = no;
			pi = ni;
		}
	}
	static void vert(Line &l, const Vector3 &p, float u, float v, const Color &c) {
		l.mesh->surface_set_color(c);
		l.mesh->surface_set_uv(Vector2(u, v));
		l.mesh->surface_add_vertex(p);
	}
	// A pole with a pennant, about 4 m tall.
	void flag(const rtso::Simulation *sim, float x, float z, const Color &c) {
		begin();
		const Vector3 base = ground(sim, x, z);
		const Vector3 top = base + Vector3(0.0f, 1.0f, 0.0f);
		const Vector3 mid = base + Vector3(0.0f, 0.7f, 0.0f);
		const Vector3 tip = base + Vector3(0.45f, 0.85f, 0.0f);
		seg(base, top, c);
		seg(top, tip, c);
		seg(tip, mid, c);
		seg(base + Vector3(-0.15f, 0.0f, 0.0f), base + Vector3(0.15f, 0.0f, 0.0f), c);
	}

	struct Actor {
		Ref<PackedScene> scene;
		Ref<Material> material;
		float scale = 1.0f, speed = 1.0f;
	};

	// Godot scales a Collada mesh by its <unit>; 0 A.D.'s converter ignores it,
	// so undo that, then 0 A.D. metres to RTS0 units (1 unit = 4 m).
	void load_actors() {
		ResourceLoader *rl = ResourceLoader::get_singleton();
		target_.scene = rl->load("res://assets/models/props/target_marker.dae");
		Ref<StandardMaterial3D> tm; // basic_trans_norm_spec: alpha blended
		tm.instantiate();
		tm->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, rl->load("res://assets/models/props/target_marker_red_and_gold.png"));
		tm->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		tm->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		target_.material = tm;
		target_.scale = 0.25f / 0.0254f; // <unit name="inch">
		target_.speed = 1.5f;            // Idle speed="150"
		flare_.scene = rl->load("res://assets/models/props/flare_target_marker.dae");
		// player_trans_norm_spec: USE_PLAYERCOLOR, the texture's alpha keeping its own colour.
		Ref<Shader> sh;
		sh.instantiate();
		sh->set_code(R"(shader_type spatial;
render_mode cull_disabled;
uniform sampler2D tex : source_color;
uniform vec3 player : source_color = vec3(1.0);
void fragment() {
	vec4 t = texture(tex, UV);
	ALBEDO = t.rgb * mix(player, vec3(1.0), t.a);
	ROUGHNESS = 0.5;
}
)");
		Ref<ShaderMaterial> fm;
		fm.instantiate();
		fm->set_shader(sh);
		fm->set_shader_parameter("tex", rl->load("res://assets/models/props/flare_target_marker_diffuse.png"));
		flare_.material = fm;
		flare_.scale = 0.25f;
	}

	void add(float x, float z, const Color &c, float ttl, bool flare) {
		Marker m{ x, z, c, ttl, nullptr };
		const Actor &a = flare ? flare_ : target_;
		if (parent_ && a.scene.is_valid()) {
			if (Node3D *n = Object::cast_to<Node3D>(a.scene->instantiate())) {
				n->set_scale(Vector3(a.scale, a.scale, a.scale));
				Ref<Material> mat = a.material;
				if (flare) {
					Ref<ShaderMaterial> own = a.material->duplicate();
					own->set_shader_parameter("player", Color(c.r, c.g, c.b));
					mat = own;
				}
				TypedArray<Node> meshes = n->find_children("*", "MeshInstance3D", true, false);
				for (int i = 0; i < meshes.size(); ++i) {
					MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(meshes[i]);
					mi->set_material_override(mat);
					mi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
				}
				TypedArray<Node> players = n->find_children("*", "AnimationPlayer", true, false);
				if (players.size() > 0) {
					AnimationPlayer *ap = Object::cast_to<AnimationPlayer>(players[0]);
					PackedStringArray names = ap->get_animation_list();
					if (names.size() > 0) {
						Ref<Animation> anim = ap->get_animation(names[0]);
						if (anim.is_valid()) anim->set_loop_mode(Animation::LOOP_LINEAR);
						ap->set_speed_scale(a.speed);
						ap->play(names[0]);
					}
				}
				parent_->add_child(n);
				m.actor = n;
			}
		}
		markers_.push_back(m);
	}

	struct Marker {
		float x, z;
		Color color;
		float ttl;
		Node3D *actor;
	};
	std::vector<Marker> markers_;
	Node *parent_ = nullptr;
	Actor target_, flare_;
	std::vector<uint32_t> ids_;
	Ref<ImmediateMesh> mesh_;
	MeshInstance3D *inst_ = nullptr;
	bool begun_ = false;
};

#endif
