#include "world_view_node.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/classes/animation.hpp>
#include <godot_cpp/classes/animation_mixer.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace godot;

void RtsoWorldView::_bind_methods() {
	// Internal C++ sync call
}

RtsoWorldView::RtsoWorldView() {}
RtsoWorldView::~RtsoWorldView() {}

Ref<MultiMesh> RtsoWorldView::create_multimesh(const Ref<Mesh> &mesh, bool use_colors) {
	Ref<MultiMesh> mm;
	mm.instantiate();
	mm->set_transform_format(MultiMesh::TRANSFORM_3D);
	mm->set_use_colors(use_colors);
	if (mesh.is_valid()) {
		mm->set_mesh(mesh);
	}
	return mm;
}

RtsoWorldView::UnitVis &RtsoWorldView::acquire_unit_slot(uint32_t sim_id) {
	auto it = unit_slot.find(sim_id);
	if (it != unit_slot.end()) return unit_pool[it->second];

	size_t slot;
	if (!unit_free.empty()) {
		slot = unit_free.back();
		unit_free.pop_back();
	} else {
		slot = unit_pool.size();
		unit_pool.emplace_back();

		UnitVis &v = unit_pool[slot];
		v.root = memnew(Node3D);
		add_child(v.root);

		v.ring = memnew(MeshInstance3D);
		v.ring->set_mesh(ring_mesh);
		v.ring->set_scale(Vector3(1.0f, 0.12f, 1.0f));
		add_child(v.ring);

		v.hp_bg = memnew(MeshInstance3D);
		v.hp_bg->set_mesh(hp_bg_mesh);
		v.hp_bg->set_material_override(hp_bg_mat);
		add_child(v.hp_bg);

		v.hp_fg = memnew(MeshInstance3D);
		v.hp_fg->set_mesh(hp_fg_mesh);
		v.hp_fg->set_material_override(hp_green_mat);
		add_child(v.hp_fg);
	}
	UnitVis &v = unit_pool[slot];
	v.sim_id = sim_id;
	v.yaw = 0.0f;
	v.root->set_visible(true);
	v.hp_bg->set_visible(false);
	v.hp_fg->set_visible(false);
	unit_slot[sim_id] = slot;
	return v;
}

RtsoWorldView::BuildingVis &RtsoWorldView::acquire_building_slot(uint32_t sim_id) {
	auto it = building_slot.find(sim_id);
	if (it != building_slot.end()) return building_pool[it->second];

	size_t slot = building_pool.size();
	building_pool.emplace_back();
	BuildingVis &v = building_pool[slot];
	v.sim_id = sim_id;
	v.root = memnew(Node3D);
	add_child(v.root);

	v.hp_bg = memnew(MeshInstance3D);
	v.hp_bg->set_mesh(hp_bg_mesh);
	v.hp_bg->set_material_override(hp_bg_mat);
	v.hp_bg->set_scale(Vector3(2.5f, 1.0f, 1.0f));
	add_child(v.hp_bg);

	v.hp_fg = memnew(MeshInstance3D);
	v.hp_fg->set_mesh(hp_fg_mesh);
	v.hp_fg->set_material_override(hp_blue_mat);
	v.hp_fg->set_scale(Vector3(2.5f, 1.0f, 1.0f));
	add_child(v.hp_fg);

	building_slot[sim_id] = slot;
	return v;
}

void RtsoWorldView::_ready() {
	// Selection Ring
	ring_mesh.instantiate();
	ring_mesh->set_inner_radius(0.45f);
	ring_mesh->set_outer_radius(0.58f);

	ring_sel_mat.instantiate();
	ring_sel_mat->set_albedo(Color(0.2f, 1.0f, 0.4f));
	ring_sel_mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	ring_sel_mat->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);

	ring_enemy_mat.instantiate();
	ring_enemy_mat->set_albedo(Color(0.92f, 0.25f, 0.2f));
	ring_enemy_mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);

	// Floating Health Bar Meshes and Materials
	hp_bg_mesh.instantiate();
	hp_bg_mesh->set_size(Vector3(0.9f, 0.08f, 0.04f));

	hp_fg_mesh.instantiate();
	hp_fg_mesh->set_size(Vector3(0.88f, 0.07f, 0.05f));

	hp_bg_mat.instantiate();
	hp_bg_mat->set_albedo(Color(0.15f, 0.15f, 0.18f, 0.85f));
	hp_bg_mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	hp_bg_mat->set_flag(BaseMaterial3D::FLAG_BILLBOARD_KEEP_SCALE, true);

	hp_green_mat.instantiate();
	hp_green_mat->set_albedo(Color(0.2f, 0.95f, 0.3f, 0.95f));
	hp_green_mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);

	hp_red_mat.instantiate();
	hp_red_mat->set_albedo(Color(0.95f, 0.25f, 0.2f, 0.95f));
	hp_red_mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);

	hp_blue_mat.instantiate();
	hp_blue_mat->set_albedo(Color(0.25f, 0.65f, 1.0f, 0.95f));
	hp_blue_mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);

	// Terrain Plane
	plane_mesh.instantiate();
	plane_mesh->set_size(Vector2(rtso::Simulation::MAP_HALF * 2.0f, rtso::Simulation::MAP_HALF * 2.0f));

	terrain = memnew(MeshInstance3D);
	terrain->set_mesh(plane_mesh);
	terrain->set_position(Vector3(0, -0.01f, 0));
	add_child(terrain);

	Ref<ShaderMaterial> terrain_mat = rtso::Assets::load_terrain_material();
	if (terrain_mat.is_valid()) {
		terrain->set_material_override(terrain_mat);
	}

	// Flying Arrows MultiMesh
	arrow_mesh.instantiate();
	arrow_mesh->set_top_radius(0.03f);
	arrow_mesh->set_bottom_radius(0.04f);
	arrow_mesh->set_height(0.65f);

	arrow_mat.instantiate();
	arrow_mat->set_albedo(Color(0.85f, 0.75f, 0.5f));

	arrow_mm = create_multimesh(arrow_mesh, false);
	arrow_mmi = memnew(MultiMeshInstance3D);
	arrow_mmi->set_multimesh(arrow_mm);
	arrow_mmi->set_material_override(arrow_mat);
	add_child(arrow_mmi);

	// Resource batches — real 0ad flora & mines with variety
	tree_a_mm = create_multimesh(rtso::ModelLibrary::get("vegetation", "tree").mesh, false);
	tree_b_mm = create_multimesh(rtso::ModelLibrary::get("vegetation", "pine").mesh, false);
	tree_c_mm = create_multimesh(rtso::ModelLibrary::get("vegetation", "palm").mesh, false);
	gold_mm = create_multimesh(rtso::ModelLibrary::get("vegetation", "gold_vein").mesh, false);
	stone_mm = create_multimesh(rtso::ModelLibrary::get("vegetation", "stone_mine").mesh, false);

	tree_a_mmi = memnew(MultiMeshInstance3D); tree_a_mmi->set_multimesh(tree_a_mm); add_child(tree_a_mmi);
	tree_b_mmi = memnew(MultiMeshInstance3D); tree_b_mmi->set_multimesh(tree_b_mm); add_child(tree_b_mmi);
	tree_c_mmi = memnew(MultiMeshInstance3D); tree_c_mmi->set_multimesh(tree_c_mm); add_child(tree_c_mmi);
	gold_mmi = memnew(MultiMeshInstance3D); gold_mmi->set_multimesh(gold_mm); add_child(gold_mmi);
	stone_mmi = memnew(MultiMeshInstance3D); stone_mmi->set_multimesh(stone_mm); add_child(stone_mmi);

	ensure_unit_batches();
}

static AnimationPlayer *find_anim_player(Node *n) {
	if (!n) return nullptr;
	if (AnimationPlayer *ap = Object::cast_to<AnimationPlayer>(n)) return ap;
	for (int i = 0; i < n->get_child_count(); ++i) {
		if (AnimationPlayer *found = find_anim_player(n->get_child(i))) return found;
	}
	return nullptr;
}

static void disable_mesh_shadows(Node *n) {
	if (!n) return;
	if (GeometryInstance3D *gi = Object::cast_to<GeometryInstance3D>(n)) {
		gi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	}
	for (int i = 0; i < n->get_child_count(); ++i) {
		disable_mesh_shadows(n->get_child(i));
	}
}

void RtsoWorldView::attach_unit_model(UnitVis &v, const String &model_name) {
	if (v.model_id == model_name && v.model) return;
	if (v.model) {
		v.root->remove_child(v.model);
		v.model->queue_free();
		v.model = nullptr;
		v.anim = nullptr;
	}
	v.model_id = model_name;
	v.clip = String();
	const auto &entry = rtso::ModelLibrary::get("units", model_name);
	if (entry.scene.is_valid()) {
		v.model = entry.scene->instantiate();
		rtso::ModelLibrary::apply_imported_material_fixups(v.model);
		v.root->add_child(v.model);
		disable_mesh_shadows(v.model);
		v.anim = find_anim_player(v.model);
		if (v.anim) {
			v.anim->set_active(true);
			v.anim->set_callback_mode_process(AnimationMixer::ANIMATION_CALLBACK_MODE_PROCESS_MANUAL);
			v.anim->set_speed_scale(1.0);
			v.anim->advance(0.0);
		}
		return;
	}
	MeshInstance3D *mi = memnew(MeshInstance3D);
	if (entry.valid) {
		mi->set_mesh(entry.mesh);
		mi->set_scale(Vector3(entry.scale, entry.scale, entry.scale));
	}
	v.model = mi;
	v.root->add_child(mi);
	disable_mesh_shadows(mi);
	v.anim = nullptr;
}

void RtsoWorldView::attach_building_model(BuildingVis &v, const String &model_name) {
	if (v.model_id == model_name && v.model) return;
	if (v.model) {
		v.root->remove_child(v.model);
		v.model->queue_free();
		v.model = nullptr;
	}
	v.model_id = model_name;
	const auto &entry = rtso::ModelLibrary::get("buildings", model_name);
	if (entry.scene.is_valid()) {
		v.model = entry.scene->instantiate();
		rtso::ModelLibrary::apply_imported_material_fixups(v.model);
		v.root->add_child(v.model);
		return;
	}
	MeshInstance3D *mi = memnew(MeshInstance3D);
	if (entry.valid) {
		mi->set_mesh(entry.mesh);
	}
	v.model = mi;
	v.root->add_child(mi);
}

void RtsoWorldView::hide_unit_vis(UnitVis &v) {
	if (v.root) v.root->set_visible(false);
	if (v.ring) v.ring->set_visible(false);
	if (v.hp_bg) v.hp_bg->set_visible(false);
	if (v.hp_fg) v.hp_fg->set_visible(false);
	if (v.anim) v.anim->set_active(false);
}

void RtsoWorldView::ensure_unit_batches() {
	static const char *kWantClip[CROWD_CLIP_COUNT] = { "Idle", "Walk", "Attack", "Death" };
	static const char *kFallback[CROWD_CLIP_COUNT] = { "Idle", "Run", "Attack", "Idle" };

	for (int t = 0; t < rtso::UNIT_TYPE_COUNT; ++t) {
		const char *id = rtso::unit_type_model((uint8_t)t);
		const auto &entry = rtso::ModelLibrary::get("units", id);
		Ref<Mesh> rest = entry.crowd_mesh.is_valid() ? entry.crowd_mesh : entry.mesh;

		if (!unit_puppet[t][0].root && entry.scene.is_valid()) {
			for (int c = 0; c < CROWD_CLIP_COUNT; ++c) {
				CrowdPuppet &p = unit_puppet[t][c];
				p.root = entry.scene->instantiate();
				if (!p.root) continue;
				rtso::ModelLibrary::apply_imported_material_fixups(p.root);
				if (Node3D *n3 = Object::cast_to<Node3D>(p.root)) {
					n3->set_visible(false);
					n3->set_position(Vector3(0.0f, -200.0f, 0.0f));
				}
				p.root->set_process_mode(Node::PROCESS_MODE_ALWAYS);
				add_child(p.root);
				p.anim = find_anim_player(p.root);
				if (!p.anim) continue;
				p.anim->set_active(true);
				p.anim->set_callback_mode_process(AnimationMixer::ANIMATION_CALLBACK_MODE_PROCESS_MANUAL);
				p.anim->set_speed_scale(1.0);
				String clip = kWantClip[c];
				if (!p.anim->has_animation(clip) && kFallback[c]) {
					clip = kFallback[c];
				}
				if (!p.anim->has_animation(clip)) {
					if (p.anim->has_animation("Idle")) clip = "Idle";
					else if (p.anim->has_animation("Walk")) clip = "Walk";
					else {
						PackedStringArray names = p.anim->get_animation_list();
						if (names.size() > 0) clip = names[0];
						else clip = String();
					}
				}
				if (clip.is_empty()) continue;
				p.clip = clip;
				Ref<Animation> a = p.anim->get_animation(clip);
				if (a.is_valid()) {
					a->set_loop_mode(c == 3 ? Animation::LOOP_NONE : Animation::LOOP_LINEAR);
				}
				p.anim->play(clip);
				p.anim->advance(0.12 * (double)((t + c) % 4));
				rtso::ModelLibrary::capture_crowd_skin(p.root, p.skin);
			}
		}

		for (int c = 0; c < CROWD_CLIP_COUNT; ++c) {
			if (unit_mmi[t][c]) continue;
			unit_mm[t][c] = create_multimesh(rest, true);
			unit_mmi[t][c] = memnew(MultiMeshInstance3D);
			unit_mmi[t][c]->set_multimesh(unit_mm[t][c]);
			unit_mmi[t][c]->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
			add_child(unit_mmi[t][c]);
		}
	}
}

void RtsoWorldView::play_unit_clip(UnitVis &v, const String &clip, bool loop) {
	if (!v.anim || clip.is_empty()) return;
	if (!v.anim->has_animation(clip)) return;
	if (v.clip == clip) return;
	v.clip = clip;
	Ref<Animation> a = v.anim->get_animation(clip);
	if (a.is_valid()) {
		a->set_loop_mode(loop ? Animation::LOOP_LINEAR : Animation::LOOP_NONE);
	}
	v.anim->play(clip);
	v.anim->advance(0.0);
}

void RtsoWorldView::sync(const rtso::Simulation *sim, float delta, const PackedInt32Array &selected_ids) {
	if (!sim) return;
	if (delta < 0.0f) delta = 0.0f;
	if (delta > 0.05f) delta = 0.05f;
	anim_time += delta;
	unit_vis_stamp++;
	if (unit_vis_stamp == 0) unit_vis_stamp = 1;

	std::unordered_set<int32_t> sel_set;
	if (selected_ids.size() > 0) sel_set.reserve((size_t)selected_ids.size());
	for (int i = 0; i < selected_ids.size(); ++i) sel_set.insert(selected_ids[i]);

	Vector3 cam_pos;
	if (Viewport *vp = get_viewport()) {
		if (Camera3D *cam = vp->get_camera_3d()) {
			cam_pos = cam->get_global_position();
		}
	}
	const float anim_dist_sq = 24.0f * 24.0f;
	(void)anim_dist_sq;

	auto crowd_clip_of = [](uint8_t uaction, float speed2, bool alive) -> int {
		if (!alive || uaction == rtso::ACTION_DYING) return 3;
		if (uaction == rtso::ACTION_ATTACK) return speed2 > 8.0f ? 1 : 2;
		if (uaction == rtso::ACTION_GATHER || uaction == rtso::ACTION_BUILD) return 0;
		if (speed2 > 0.001f || uaction == rtso::ACTION_MOVE || uaction == rtso::ACTION_RETURN_DROPSITE) return 1;
		return 0;
	};

	// =========================================================================
	// 1. Units Presentation: Real 0 A.D. models, 3D animations, and Health Bars
	// =========================================================================
	ensure_unit_batches();
	auto uview = sim->units();

	std::unordered_set<uint32_t> detailed;
	detailed.reserve((size_t)MAX_DETAILED_UNITS * 2);
	for (int s = 0; s < selected_ids.size() && (int)detailed.size() < MAX_DETAILED_UNITS; ++s) {
		detailed.insert((uint32_t)selected_ids[s]);
	}
	if ((int)detailed.size() < MAX_DETAILED_UNITS) {
		struct Near {
			float d2;
			uint32_t id;
		};
		std::vector<Near> near;
		near.reserve(uview.count);
		for (size_t i = 0; i < uview.count; ++i) {
			if (!uview.alive[i] && uview.action_state[i] != rtso::ACTION_DYING) continue;
			float dx = uview.pos_x[i] - cam_pos.x;
			float dz = uview.pos_z[i] - cam_pos.z;
			near.push_back({dx * dx + dz * dz, uview.id[i]});
		}
		int keep = MAX_DETAILED_UNITS;
		if ((int)near.size() > keep) {
			std::partial_sort(near.begin(), near.begin() + keep, near.end(),
					[](const Near &a, const Near &b) { return a.d2 < b.d2; });
			near.resize((size_t)keep);
		}
		for (const Near &n : near) {
			if ((int)detailed.size() >= MAX_DETAILED_UNITS) break;
			detailed.insert(n.id);
		}
	}

	int counts[rtso::UNIT_TYPE_COUNT][CROWD_CLIP_COUNT] = {};
	for (size_t i = 0; i < uview.count; ++i) {
		uint8_t t = uview.unit_type[i];
		if (t >= rtso::UNIT_TYPE_COUNT) t = rtso::UNIT_VILLAGER;
		float vx = uview.vel_x[i];
		float vz = uview.vel_z[i];
		int clip = crowd_clip_of(uview.action_state[i], vx * vx + vz * vz, uview.alive[i] != 0);
		counts[t][clip]++;
	}
	int written[rtso::UNIT_TYPE_COUNT][CROWD_CLIP_COUNT] = {};
	for (int t = 0; t < rtso::UNIT_TYPE_COUNT; ++t) {
		for (int c = 0; c < CROWD_CLIP_COUNT; ++c) {
			if (unit_mm[t][c].is_valid()) unit_mm[t][c]->set_instance_count(counts[t][c]);
			CrowdPuppet &p = unit_puppet[t][c];
			if (!p.anim) continue;
			if (counts[t][c] <= 0) continue;
			p.anim->advance((double)delta);
			if (!p.skin.captured) rtso::ModelLibrary::capture_crowd_skin(p.root, p.skin);
			Ref<ArrayMesh> posed = rtso::ModelLibrary::bake_crowd_skin(p.skin, p.posed);
			if (posed.is_valid() && posed->get_surface_count() > 0) {
				p.posed = posed;
				if (unit_mm[t][c].is_valid()) unit_mm[t][c]->set_mesh(p.posed);
			}
		}
	}

	const Color col_player(0.55f, 0.72f, 1.0f);
	const Color col_enemy(1.0f, 0.52f, 0.42f);

	for (size_t i = 0; i < uview.count; ++i) {
		uint32_t id = uview.id[i];
		bool is_player = (uview.team[i] == 0);
		uint8_t utype = uview.unit_type[i];
		if (utype >= rtso::UNIT_TYPE_COUNT) utype = rtso::UNIT_VILLAGER;
		uint8_t uaction = uview.action_state[i];
		float hp = uview.hp[i];
		float max_hp = uview.max_hp[i];
		float x = uview.pos_x[i];
		float z = uview.pos_z[i];
		float vx = uview.vel_x[i];
		float vz = uview.vel_z[i];
		float speed2 = vx * vx + vz * vz;
		float yaw = 0.0f;
		auto yaw_it = crowd_yaw.find(id);
		if (yaw_it != crowd_yaw.end()) yaw = yaw_it->second;
		if (speed2 > 0.0004f) {
			yaw = Math::atan2(-vx, vz);
			crowd_yaw[id] = yaw;
		} else if (yaw_it == crowd_yaw.end()) {
			crowd_yaw[id] = 0.0f;
		}

		const auto &entry = rtso::ModelLibrary::get("units", rtso::unit_type_model(utype));
		float y = entry.y_offset;
		Transform3D xf(Basis(Vector3(0, 1, 0), yaw), Vector3(x, y, z));
		int clip = crowd_clip_of(uaction, speed2, uview.alive[i] != 0);
		int slot = written[utype][clip]++;
		if (unit_mm[utype][clip].is_valid()) {
			if (detailed.count(id) > 0) {
				Transform3D hidden;
				hidden.origin = Vector3(x, y, z);
				hidden.basis = hidden.basis.scaled(Vector3(0.001f, 0.001f, 0.001f));
				unit_mm[utype][clip]->set_instance_transform(slot, hidden);
			} else {
				unit_mm[utype][clip]->set_instance_transform(slot, xf);
			}
			unit_mm[utype][clip]->set_instance_color(slot, is_player ? col_player : col_enemy);
		}

		if (detailed.count(id) == 0) continue;

		UnitVis &v = acquire_unit_slot(id);
		v.vis_stamp = unit_vis_stamp;
		attach_unit_model(v, rtso::unit_type_model(utype));

		if (speed2 > 0.001f) {
			float target_yaw = Math::atan2(-vx, vz);
			float diff = Math::wrapf(target_yaw - v.yaw, -(float)Math_PI, (float)Math_PI);
			v.yaw += diff * 0.28f;
		}

		if (v.anim) {
			v.anim->set_active(true);
			v.anim->set_callback_mode_process(AnimationMixer::ANIMATION_CALLBACK_MODE_PROCESS_MANUAL);
			v.anim->set_speed_scale(1.0);
		}
		String want_clip;
		bool loop_clip = true;
		if (!uview.alive[i] || uaction == rtso::ACTION_DYING) {
			want_clip = "Death";
			loop_clip = false;
		} else if (uaction == rtso::ACTION_ATTACK && speed2 <= 8.0f) {
			want_clip = "Attack";
		} else if (speed2 > 0.001f || uaction == rtso::ACTION_MOVE || uaction == rtso::ACTION_RETURN_DROPSITE ||
				uaction == rtso::ACTION_ATTACK) {
			want_clip = "Walk";
		} else if (uaction == rtso::ACTION_GATHER) {
			want_clip = "Gather";
		} else if (uaction == rtso::ACTION_BUILD) {
			want_clip = "Build";
		} else {
			want_clip = "Idle";
		}
		if (v.anim) {
			if (!v.anim->has_animation(want_clip)) {
				if (want_clip == "Run" && v.anim->has_animation("Walk")) want_clip = "Walk";
				else if (want_clip == "Walk" && v.anim->has_animation("Run")) want_clip = "Run";
				else if (want_clip == "Build" && v.anim->has_animation("Gather")) want_clip = "Gather";
				else if (v.anim->has_animation("Idle")) want_clip = "Idle";
			}
			play_unit_clip(v, want_clip, loop_clip);
			v.anim->advance((double)delta);
		}

		Transform3D dxf(Basis(Vector3(0, 1, 0), v.yaw), Vector3(x, entry.y_offset, z));
		v.root->set_transform(dxf);

		bool is_selected = (sel_set.count((int32_t)id) > 0);
		if (is_selected) {
			v.ring->set_material_override(is_player ? ring_sel_mat : ring_enemy_mat);
			v.ring->set_visible(true);
			v.ring->set_transform(Transform3D(Basis(), Vector3(x, 0.04f, z)));
		} else if (v.ring_on) {
			v.ring->set_visible(false);
		}
		v.ring_on = is_selected;

		bool show_hp = is_selected && uview.alive[i];
		v.hp_bg->set_visible(show_hp);
		v.hp_fg->set_visible(show_hp);
		if (show_hp) {
			float hp_ratio = Math::clamp(hp / max_hp, 0.0f, 1.0f);
			float bar_y = entry.aabb.size.y + 0.4f;
			if (bar_y < 1.2f) bar_y = entry.scale * 1.8f + 0.4f;
			Vector3 hp_pos(x, bar_y, z);
			v.hp_bg->set_transform(Transform3D(Basis(), hp_pos));
			v.hp_fg->set_material_override(is_player ? hp_green_mat : hp_red_mat);
			Basis fg_basis;
			fg_basis.scale(Vector3(hp_ratio, 1.0f, 1.0f));
			Vector3 fg_offset((hp_ratio - 1.0f) * 0.44f, 0.0f, 0.01f);
			v.hp_fg->set_transform(Transform3D(fg_basis, hp_pos + fg_offset));
		}
	}

	for (size_t s = 0; s < unit_pool.size(); ++s) {
		if (unit_pool[s].root && unit_pool[s].vis_stamp != unit_vis_stamp) {
			hide_unit_vis(unit_pool[s]);
			unit_slot.erase(unit_pool[s].sim_id);
			unit_free.push_back(s);
		}
	}

	// =========================================================================
	// 2. Buildings: Town Center, Barracks, Houses, Towers, Storehouses, Farms
	// =========================================================================
	const auto &buildings = sim->buildings();
	std::unordered_set<uint32_t> alive_buildings;

	for (const auto &b : buildings) {
		alive_buildings.insert(b.id);
		BuildingVis &v = acquire_building_slot(b.id);

		const char *mid = "town_center";
		if (b.type == rtso::BLD_HOUSE) mid = "house";
		else if (b.type == rtso::BLD_BARRACKS) mid = "barracks";
		else if (b.type == rtso::BLD_TOWER) mid = "wall_tower";
		else if (b.type == rtso::BLD_STOREHOUSE) mid = "storehouse";
		else if (b.type == rtso::BLD_FARMSTEAD) mid = "farmstead";

		attach_building_model(v, mid);
		const auto &entry = rtso::ModelLibrary::get("buildings", mid);

		// Construction progress scale / height
		float b_scale = entry.scale;
		float b_progress = b.build_progress;
		float y_pos = entry.y_offset;

		if (b_progress < 1.0f) {
			y_pos -= (1.0f - b_progress) * 2.0f;
		}

		Transform3D xf(Basis().scaled(Vector3(b_scale, b_scale, b_scale)), Vector3(b.x, y_pos, b.z));
		v.root->set_transform(xf);

		// Building Health & Construction Progress Bar
		bool show_bhp = (b_progress < 1.0f || b.hp < b.max_hp * 0.98f);
		v.hp_bg->set_visible(show_bhp);
		v.hp_fg->set_visible(show_bhp);

		if (show_bhp) {
			float bar_ratio = (b_progress < 1.0f) ? b_progress : Math::clamp(b.hp / b.max_hp, 0.0f, 1.0f);
			float bar_y = 5.0f;
			Vector3 hp_pos(b.x, bar_y, b.z);

			v.hp_bg->set_transform(Transform3D(Basis().scaled(Vector3(2.5f, 1.0f, 1.0f)), hp_pos));
			v.hp_fg->set_material_override((b_progress < 1.0f) ? hp_blue_mat : hp_green_mat);

			Basis fg_basis;
			fg_basis.scale(Vector3(2.5f * bar_ratio, 1.0f, 1.0f));
			Vector3 fg_offset((bar_ratio - 1.0f) * 1.1f, 0.0f, 0.02f);
			v.hp_fg->set_transform(Transform3D(fg_basis, hp_pos + fg_offset));
		}
	}

	for (size_t s = 0; s < building_pool.size(); ++s) {
		if (building_pool[s].root && alive_buildings.find(building_pool[s].sim_id) == alive_buildings.end()) {
			remove_child(building_pool[s].root);
			memdelete(building_pool[s].root);
			building_pool[s].root = nullptr;
			building_pool[s].model = nullptr;
			building_slot.erase(building_pool[s].sim_id);
		}
	}

	// =========================================================================
	// 3. Flying Projectiles (Ballistic Arrows)
	// =========================================================================
	const auto &projectiles = sim->projectiles();
	arrow_mm->set_instance_count((int)projectiles.size());

	for (size_t p = 0; p < projectiles.size(); ++p) {
		const auto &proj = projectiles[p];
		Vector3 pos(proj.current_x, proj.current_y, proj.current_z);

		// Calculate direction of flight
		Vector3 dir(proj.target_x - proj.start_x, 0.8f * (1.0f - 2.0f * proj.progress), proj.target_z - proj.start_z);
		if (dir.length_squared() > 0.001f) dir.normalize();

		Basis rot;
		Vector3 up(0, 1, 0);
		if (Math::abs(dir.dot(up)) < 0.95f) {
			Vector3 right = dir.cross(up).normalized();
			Vector3 actual_up = right.cross(dir).normalized();
			rot = Basis(right, actual_up, -dir);
		}

		Transform3D xf(rot, pos);
		arrow_mm->set_instance_transform((int)p, xf);
	}

	// =========================================================================
	// 4. Resources: Tree Varieties, Gold Veins, Stone Mines
	// =========================================================================
	auto rview = sim->resources();
	size_t ta = 0, tb = 0, tc = 0, g = 0, st = 0;

	for (size_t i = 0; i < rview.count; ++i) {
		if (!rview.alive[i] || rview.amount[i] <= 0) continue;
		uint32_t h = rview.id[i];
		if (rview.type[i] == rtso::RES_WOOD) {
			if (h % 3 == 0) ta++;
			else if (h % 3 == 1) tb++;
			else tc++;
		} else if (rview.type[i] == rtso::RES_GOLD) {
			g++;
		} else if (rview.type[i] == rtso::RES_STONE) {
			st++;
		}
	}

	tree_a_mm->set_instance_count((int)ta);
	tree_b_mm->set_instance_count((int)tb);
	tree_c_mm->set_instance_count((int)tc);
	gold_mm->set_instance_count((int)g);
	stone_mm->set_instance_count((int)st);

	size_t ia = 0, ib = 0, ic = 0, ig = 0, ist = 0;
	const auto &e_tree = rtso::ModelLibrary::get("vegetation", "tree");
	const auto &e_pine = rtso::ModelLibrary::get("vegetation", "pine");
	const auto &e_palm = rtso::ModelLibrary::get("vegetation", "palm");
	const auto &e_gold = rtso::ModelLibrary::get("vegetation", "gold_vein");
	const auto &e_stone = rtso::ModelLibrary::get("vegetation", "stone_mine");

	for (size_t i = 0; i < rview.count; ++i) {
		if (!rview.alive[i] || rview.amount[i] <= 0) continue;
		float x = rview.x[i], z = rview.z[i];
		uint32_t h = rview.id[i];
		Basis rot(Vector3(0, 1, 0), (float)(h % 12) * 0.52f);

		if (rview.type[i] == rtso::RES_WOOD) {
			float y_off = (h % 3 == 0) ? e_tree.y_offset : ((h % 3 == 1) ? e_pine.y_offset : e_palm.y_offset);
			Transform3D xf(rot, Vector3(x, y_off, z));
			if (h % 3 == 0) tree_a_mm->set_instance_transform((int)ia++, xf);
			else if (h % 3 == 1) tree_b_mm->set_instance_transform((int)ib++, xf);
			else tree_c_mm->set_instance_transform((int)ic++, xf);
		} else if (rview.type[i] == rtso::RES_GOLD) {
			Transform3D xf(rot, Vector3(x, e_gold.y_offset, z));
			gold_mm->set_instance_transform((int)ig++, xf);
		} else if (rview.type[i] == rtso::RES_STONE) {
			Transform3D xf(rot, Vector3(x, e_stone.y_offset, z));
			stone_mm->set_instance_transform((int)ist++, xf);
		}
	}
}

