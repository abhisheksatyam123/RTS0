#ifndef RTSO_RANGE_OVERLAYS_H
#define RTSO_RANGE_OVERLAYS_H

// Ground overlays for the selected and highlighted entities (0 A.D.
// gui/session/RangeOverlayManager.js): attack range rings of defensive structures (session.toggleattackrange, Alt+C), aura rings
// (session.toggleaurasrange, Alt+V), healer range rings
// (session.togglehealrange, Alt+B), drawn as one ImmediateMesh that follows the
// terrain (rally points: rally_point_renderer.h); and target
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
	void set_preview(float x, float z, float max_r, float min_r) { preview_ = {x, z, max_r, min_r}; }

	// RangeOverlayManager: the selection and the highlighted (hovered or band-boxed)
	// entities. GuiInterface.SetSelectionHighlight turns the overlays on only for
	// the viewer's own entities (an observer sees everyone's).
	void update(const rtso::Simulation *sim, uint8_t me, const std::vector<uint32_t> &ids) {
		if (!inst_ || !sim) return;
		mesh_->clear_surfaces();
		begun_ = false;
		radii_.clear();
		for (auto &m : markers_) {
			if (m.actor) m.actor->set_position(Vector3(m.x, sim->get_terrain_height(m.x, m.z), m.z));
			else flag(sim, m.x, m.z, m.color);
		}
		ids_ = ids;
		std::sort(ids_.begin(), ids_.end());
		auto listed = [&](uint32_t id) { return std::binary_search(ids_.begin(), ids_.end(), id); };
		auto shown_for = [&](uint8_t team) { return me >= rtso::MAX_PLAYERS || team == me; };
		const Color attack_col(1.0f, 0.3f, 0.25f, 0.7f), aura_col(0.85f, 0.5f, 1.0f, 0.6f);
		if (show_attack && preview_.max_r > 0.0f) {
			ring(sim, preview_.x, preview_.z, preview_.max_r, attack_col);
			if (preview_.min_r > 0.0f) ring(sim, preview_.x, preview_.z, preview_.min_r, attack_col);
		}
		if (ids_.empty()) {
			if (begun_) mesh_->surface_end();
			return;
		}
		// Where each listed entity stands now (the AuraManager keeps the spot of its last rebuild).
		spots_.clear();
		for (const auto &b : sim->buildings()) {
			if (!listed(b.id) || !shown_for(b.team) || b.build_progress < 1.0f) continue;
			spots_.push_back({b.id, b.x, b.z});
			// Attack.js GetRangeOverlays: the Attack's MinRange and MaxRange rings.
			if (show_attack && sim->building_arrow_range(b) > 0.0f) {
				const rtso::Simulation::StructureDetails d = sim->structure_details(b);
				ring(sim, b.x, b.z, d.max_range + b.half_size, attack_col);
				if (d.min_range > 0.0f) ring(sim, b.x, b.z, d.min_range + b.half_size, attack_col);
			}
		}
		auto uv = sim->units();
		for (size_t i = 0; i < uv.count; ++i) {
			if (!uv.alive[i] || !listed(uv.id[i]) || !shown_for(uv.team[i])) continue;
			spots_.push_back({uv.id[i], uv.pos_x[i], uv.pos_z[i]});
			const uint16_t ht = uv.tmpl ? uv.tmpl[i] : rtso::CIV_UNIT_TEMPLATE_NONE;
			if (show_heal && rtso::unit_is_healer(ht, uv.unit_type[i])) {
				const float extra = (rtso::unit_heal_range_base(ht, uv.unit_type[i]) - 12.0f) * rtso::COMBAT_DISTANCE_SCALE;
				ring(sim, uv.pos_x[i], uv.pos_z[i], sim->healer_range(uv.team[i]) + extra, Color(0.3f, 1.0f, 0.4f, 0.6f));
			}
			// The siege templates with an Attack RangeOverlay: stone and bolt throwers, the
			// flamethrower, siege towers and siege warships.
			if (show_attack && has_attack_overlay(ht)) {
				const float reach = sim->attack_reach(i);
				const float min_ratio = rtso::unit_min_range_ratio(uv.unit_type[i], ht);
				if (reach > 0.0f) {
					ring(sim, uv.pos_x[i], uv.pos_z[i], reach, attack_col);
					if (min_ratio > 0.0f) ring(sim, uv.pos_x[i], uv.pos_z[i], reach * min_ratio, attack_col);
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
				ring(sim, it->x, it->z, src.range, aura_col);
			}
		}
		if (begun_) mesh_->surface_end();
	}

private:
	struct Spot {
		uint32_t id;
		float x, z;
	};
	std::vector<Spot> spots_;
	std::vector<float> radii_;
	struct Preview {
		float x = 0.0f, z = 0.0f, max_r = 0.0f, min_r = 0.0f;
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
	void ring(const rtso::Simulation *sim, float cx, float cz, float r, const Color &c) {
		begin();
		radii_.push_back(r);
		const int steps = std::max(24, (int)(r * 3.0f));
		Vector3 prev = ground(sim, cx + r, cz);
		for (int k = 1; k <= steps; ++k) {
			const float t = (float)k / steps * (float)Math_TAU;
			const Vector3 p = ground(sim, cx + r * Math::cos(t), cz + r * Math::sin(t));
			seg(prev, p, c);
			prev = p;
		}
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
