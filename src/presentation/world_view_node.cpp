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
#include <godot_cpp/variant/utility_functions.hpp>
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

void RtsoWorldView::init_animation_controllers() {
	anim_controllers[rtso::UNIT_VILLAGER].load_rig(rtso::RigType::Villager);
	anim_controllers[rtso::UNIT_SWORDSMAN].load_rig(rtso::RigType::Biped);
	anim_controllers[rtso::UNIT_SPEARMAN].load_rig(rtso::RigType::Biped);
	anim_controllers[rtso::UNIT_ARCHER].load_rig(rtso::RigType::Archer);
	anim_controllers[rtso::UNIT_CAVALRY].load_rig(rtso::RigType::Mounted);
	anim_controllers[rtso::UNIT_HEALER].load_rig(rtso::RigType::Healer);
	anim_controllers[rtso::UNIT_SIEGE_RAM].load_rig(rtso::RigType::SiegeRam);
	anim_controllers[rtso::UNIT_HERO].load_rig(rtso::RigType::Biped);
	anim_controllers[rtso::UNIT_TRADER].load("trader");
	anim_controllers[rtso::UNIT_ARCHER_MAIDEN].load_rig(rtso::RigType::Archer);
	anim_controllers[rtso::UNIT_CARNYX].load_rig(rtso::RigType::Biped);
	anim_controllers[rtso::UNIT_ELEPHANT].load("elephant");
	anim_controllers[rtso::UNIT_WORKER_ELEPHANT].load("elephant");
	anim_controllers[rtso::UNIT_CAMEL].load("camel");
	anim_controllers[rtso::UNIT_CHARIOT].load("chariot");
	anim_controllers[rtso::UNIT_SCYTHED_CHARIOT].load("chariot");
	anim_controllers[rtso::UNIT_WAR_DOG].load("dog");
	anim_controllers[rtso::UNIT_CATAPULT].load("catapult");
	anim_controllers[rtso::UNIT_BOLT_SHOOTER].load("bolt");
	anim_controllers[rtso::UNIT_FEMALE_CITIZEN].load("female");
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

	// Fauna — never fall back to trees/stones (those looked like walking mines)
	{
		auto deer = rtso::ModelLibrary::get("units", "deer");
		Ref<Mesh> deer_mesh = deer.mesh.is_valid() ? deer.mesh : rtso::ModelLibrary::get("units", "war_dog").mesh;
		fauna_mm[rtso::FAUNA_DEER] = create_multimesh(deer_mesh, false);
		auto boar = rtso::ModelLibrary::get("units", "boar");
		fauna_mm[rtso::FAUNA_BOAR] = create_multimesh(boar.mesh.is_valid() ? boar.mesh : deer_mesh, false);
		auto sheep = rtso::ModelLibrary::get("units", "sheep");
		fauna_mm[rtso::FAUNA_SHEEP] = create_multimesh(sheep.mesh.is_valid() ? sheep.mesh : deer_mesh, false);
		auto wolf = rtso::ModelLibrary::get("units", "wolf");
		fauna_mm[rtso::FAUNA_WOLF] = create_multimesh(wolf.mesh.is_valid() ? wolf.mesh : deer_mesh, false);
	}

	for (int f = 0; f < rtso::FAUNA_COUNT; ++f) {
		fauna_mmi[f] = memnew(MultiMeshInstance3D);
		fauna_mmi[f]->set_multimesh(fauna_mm[f]);
		add_child(fauna_mmi[f]);
	}

	// Initialize 3D Positional Spatial Audio Pool
	for (size_t p = 0; p < SPATIAL_AUDIO_POOL_SIZE; ++p) {
		spatial_players[p] = memnew(AudioStreamPlayer3D);
		spatial_players[p]->set_max_distance(80.0f);
		spatial_players[p]->set_unit_size(8.0f);
		spatial_players[p]->set_volume_db(0.0f);
		add_child(spatial_players[p]);
	}

	// Pre-cache combat and destruction audio streams
	auto load_s = [](const char *path) -> Ref<AudioStream> {
		if (ResourceLoader::get_singleton()->exists(path)) {
			return ResourceLoader::get_singleton()->load(path);
		}
		return Ref<AudioStream>();
	};

	sfx_bow_shoot[0] = load_s("res://assets/sounds/combat/bow_shoot_1.ogg");
	sfx_bow_shoot[1] = load_s("res://assets/sounds/combat/bow_shoot_2.ogg");
	sfx_arrow_hit = load_s("res://assets/sounds/combat/arrow_hit.ogg");
	sfx_catapult_blast = load_s("res://assets/sounds/combat/catapult_blast.ogg");
	sfx_sword_hit[0] = load_s("res://assets/sounds/combat/sword_hit_1.ogg");
	sfx_sword_hit[1] = load_s("res://assets/sounds/combat/sword_hit_2.ogg");
	sfx_sword_hit[2] = load_s("res://assets/sounds/combat/sword_hit_3.ogg");
	sfx_spear_hit[0] = load_s("res://assets/sounds/combat/spear_hit_1.ogg");
	sfx_spear_hit[1] = load_s("res://assets/sounds/combat/spear_hit_2.ogg");
	sfx_building_collapse = load_s("res://assets/sounds/building/building_collapse.ogg");
	sfx_building_fire = load_s("res://assets/sounds/building/building_fire.ogg");

	init_animation_controllers();
}

void RtsoWorldView::play_spatial_sound(const Ref<AudioStream> &stream, Vector3 pos, float volume_db, float max_dist, float pitch_scale) {
	if (!stream.is_valid()) return;
	AudioStreamPlayer3D *player = spatial_players[next_spatial_idx];
	next_spatial_idx = (next_spatial_idx + 1) % SPATIAL_AUDIO_POOL_SIZE;
	if (player) {
		player->set_position(pos);
		player->set_stream(stream);
		player->set_volume_db(volume_db);
		player->set_max_distance(max_dist);
		player->set_pitch_scale(pitch_scale);
		player->play();
	}
}

void RtsoWorldView::play_spatial_sound_path(const String &path, Vector3 pos, float volume_db, float max_dist, float pitch_scale) {
	String res_path = path;
	if (!res_path.begins_with("res://")) {
		if (res_path.begins_with("audio/")) {
			res_path = "res://assets/sounds/" + res_path.substr(6);
		} else if (res_path.begins_with("assets/sounds/")) {
			res_path = "res://" + res_path;
		} else {
			res_path = "res://" + res_path;
		}
	}
	if (ResourceLoader::get_singleton()->exists(res_path)) {
		Ref<AudioStream> s = ResourceLoader::get_singleton()->load(res_path);
		play_spatial_sound(s, pos, volume_db, max_dist, pitch_scale);
	}
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
	if (v.anim) {
		v.anim->stop();
		v.anim = nullptr;
	}
	if (v.model) {
		v.root->remove_child(v.model);
		v.model->queue_free();
		v.model = nullptr;
	}
	v.model_id = model_name;
	v.clip = String();
	String cat_units = "units";
	auto entry = rtso::ModelLibrary::get(cat_units, model_name);
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

Node *RtsoWorldView::instantiate_building_id(const String &id) {
	String cat = "buildings";
	const rtso::ModelLibrary::Entry &e = rtso::ModelLibrary::get(cat, id);
	if (e.scene.is_valid()) {
		Node *n = e.scene->instantiate();
		rtso::ModelLibrary::apply_imported_material_fixups(n);
		return n;
	}
	if (e.mesh.is_valid()) {
		MeshInstance3D *mi = memnew(MeshInstance3D);
		mi->set_mesh(e.mesh);
		return mi;
	}
	return nullptr;
}

void RtsoWorldView::set_building_site_visible(BuildingVis &v, bool foundation_on, bool scaffold_on, uint8_t btype) {
	auto drop = [&](Node *&n) {
		if (!n) return;
		if (v.root && n->get_parent() == v.root)
			v.root->remove_child(n);
		n->queue_free();
		n = nullptr;
	};
	auto place = [&](Node *&slot, const char *id, bool show) {
		if (!show) {
			if (Node3D *n3 = Object::cast_to<Node3D>(slot))
				n3->set_visible(false);
			return;
		}
		if (!slot) {
			slot = instantiate_building_id(id);
			if (!slot) return;
			v.root->add_child(slot);
			String cat = "buildings";
			const auto &se = rtso::ModelLibrary::get(cat, String(id));
			if (Node3D *n3 = Object::cast_to<Node3D>(slot)) {
				float s = se.scale > 0.05f ? se.scale : 1.0f;
				n3->set_scale(Vector3(s, s, s));
				n3->set_position(Vector3(0, se.y_offset, 0));
			}
		}
		if (Node3D *n3 = Object::cast_to<Node3D>(slot))
			n3->set_visible(true);
	};

	if (v.overlay_type != (int)btype) {
		drop(v.foundation);
		drop(v.scaffold);
		v.overlay_type = (int)btype;
	}
	place(v.foundation, rtso::civ_building_foundation(btype), foundation_on);
	place(v.scaffold, rtso::civ_building_scaffold(btype), scaffold_on);
}

void RtsoWorldView::attach_building_model(BuildingVis &v, const String &model_name) {
	if (v.model_id == model_name && v.model) return;
	if (v.model) {
		v.root->remove_child(v.model);
		v.model->queue_free();
		v.model = nullptr;
	}
	v.model_id = model_name;
	v.model = instantiate_building_id(model_name);
	if (!v.model) {
		MeshInstance3D *mi = memnew(MeshInstance3D);
		v.model = mi;
	}
	v.root->add_child(v.model);
}

void RtsoWorldView::hide_unit_vis(UnitVis &v) {
	if (v.root) v.root->set_visible(false);
	if (v.ring) v.ring->set_visible(false);
	if (v.hp_bg) v.hp_bg->set_visible(false);
	if (v.hp_fg) v.hp_fg->set_visible(false);
	if (v.anim && UtilityFunctions::is_instance_valid(v.anim))
		v.anim->set_active(false);
}

void RtsoWorldView::ensure_unit_batches(const rtso::Simulation *sim) {
	uint8_t civs[CROWD_BANK_COUNT] = {rtso::CIV_ATHENIANS, rtso::CIV_SPARTANS};
	if (sim) {
		civs[0] = sim->player_civ();
		civs[1] = sim->team_civ(1);
	}
	for (int bank = 0; bank < CROWD_BANK_COUNT; ++bank) {
		for (int t = 0; t < rtso::UNIT_TYPE_COUNT; ++t) {
			String id_str = rtso::civ_unit_model(civs[bank], (uint8_t)t);
			String cat_units2 = "units";
			auto entry = rtso::ModelLibrary::get(cat_units2, id_str);
			Ref<Mesh> rest = entry.crowd_mesh.is_valid() ? entry.crowd_mesh : entry.mesh;
			unit_puppet_baked[bank][t] = true;

			for (int c = 0; c < CROWD_CLIP_COUNT; ++c) {
				if (unit_mmi[bank][t][c]) continue;
				unit_mm[bank][t][c] = create_multimesh(rest, true);
				unit_mmi[bank][t][c] = memnew(MultiMeshInstance3D);
				unit_mmi[bank][t][c]->set_multimesh(unit_mm[bank][t][c]);
				unit_mmi[bank][t][c]->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
				add_child(unit_mmi[bank][t][c]);
			}
		}
	}
}

void RtsoWorldView::play_unit_clip(UnitVis &v, const String &clip, bool loop) {
	if (!v.anim || !UtilityFunctions::is_instance_valid(v.anim) || clip.is_empty()) return;
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

void RtsoWorldView::rebuild_terrain(const rtso::Simulation *sim) {
	if (!sim || !terrain) return;
	float half = sim->map_half();
	const auto &heights = sim->terrain_heights();
	int res = sim->terrain_res();

	if (heights.empty() || res <= 1) {
		plane_mesh->set_size(Vector2(half * 2.0f, half * 2.0f));
		terrain->set_mesh(plane_mesh);
	} else {
		terrain_array_mesh.instantiate();
		PackedVector3Array vertices;
		PackedVector3Array normals;
		PackedVector2Array uvs;
		PackedInt32Array indices;

		float step = (half * 2.0f) / (float)(res - 1);
		for (int z = 0; z < res; ++z) {
			float wz = -half + z * step;
			for (int x = 0; x < res; ++x) {
				float wx = -half + x * step;
				float wy = heights[z * res + x];
				vertices.push_back(Vector3(wx, wy, wz));
				uvs.push_back(Vector2((float)x / (float)(res - 1) * 8.0f, (float)z / (float)(res - 1) * 8.0f));

				float h_left  = (x > 0) ? heights[z * res + (x - 1)] : wy;
				float h_right = (x < res - 1) ? heights[z * res + (x + 1)] : wy;
				float h_down  = (z > 0) ? heights[(z - 1) * res + x] : wy;
				float h_up    = (z < res - 1) ? heights[(z + 1) * res + x] : wy;
				Vector3 n(-(h_right - h_left) / (2.0f * step), 1.0f, -(h_up - h_down) / (2.0f * step));
				normals.push_back(n.normalized());
			}
		}

		for (int z = 0; z < res - 1; ++z) {
			for (int x = 0; x < res - 1; ++x) {
				int i00 = z * res + x;
				int i10 = z * res + (x + 1);
				int i01 = (z + 1) * res + x;
				int i11 = (z + 1) * res + (x + 1);

				indices.push_back(i00);
				indices.push_back(i01);
				indices.push_back(i10);

				indices.push_back(i10);
				indices.push_back(i01);
				indices.push_back(i11);
			}
		}

		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = vertices;
		arrays[Mesh::ARRAY_NORMAL] = normals;
		arrays[Mesh::ARRAY_TEX_UV] = uvs;
		arrays[Mesh::ARRAY_INDEX] = indices;

		terrain_array_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
		terrain->set_mesh(terrain_array_mesh);
	}

	// Water Body
	if (sim->water_height() > 0.0f) {
		if (!water_mesh_node) {
			water_mesh.instantiate();
			water_mat.instantiate();
			water_mat->set_albedo(Color(0.08f, 0.35f, 0.55f, 0.82f));
			water_mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
			water_mat->set_roughness(0.15f);
			water_mat->set_metallic(0.2f);
			water_mesh_node = memnew(MeshInstance3D);
			water_mesh_node->set_mesh(water_mesh);
			water_mesh_node->set_material_override(water_mat);
			add_child(water_mesh_node);
		}
		water_mesh->set_size(Vector2(half * 2.2f, half * 2.2f));
		water_mesh_node->set_position(Vector3(0.0f, sim->water_height(), 0.0f));
		water_mesh_node->set_visible(true);
	} else if (water_mesh_node) {
		water_mesh_node->set_visible(false);
	}
}

void RtsoWorldView::sync(const rtso::Simulation *sim, float delta, const PackedInt32Array &selected_ids) {
	if (!sim) return;
	if (delta < 0.0f) delta = 0.0f;
	if (delta > 0.05f) delta = 0.05f;
	anim_time += delta;
	unit_vis_stamp++;
	if (unit_vis_stamp == 0) unit_vis_stamp = 1;

	// Update skeletal animation controllers
	for (int t = 0; t < rtso::UNIT_TYPE_COUNT; ++t) {
		anim_controllers[t].update(delta * 1000.0f);
	}

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
	ensure_unit_batches(sim);
	auto uview = sim->units();

	std::unordered_set<uint32_t> detailed;
	for (int s = 0; s < selected_ids.size() && (int)detailed.size() < MAX_DETAILED_UNITS; ++s) {
		detailed.insert((uint32_t)selected_ids[s]);
	}

	int counts[CROWD_BANK_COUNT][rtso::UNIT_TYPE_COUNT][CROWD_CLIP_COUNT] = {};
	for (size_t i = 0; i < uview.count; ++i) {
		uint8_t t = uview.unit_type[i];
		if (t >= rtso::UNIT_TYPE_COUNT) t = rtso::UNIT_VILLAGER;
		int bank = (uview.team[i] == 0) ? 0 : 1;
		float vx = uview.vel_x[i];
		float vz = uview.vel_z[i];
		int clip = crowd_clip_of(uview.action_state[i], vx * vx + vz * vz, uview.alive[i] != 0);
		counts[bank][t][clip]++;
	}
	int written[CROWD_BANK_COUNT][rtso::UNIT_TYPE_COUNT][CROWD_CLIP_COUNT] = {};
	for (int bank = 0; bank < CROWD_BANK_COUNT; ++bank) {
		for (int t = 0; t < rtso::UNIT_TYPE_COUNT; ++t) {
			for (int c = 0; c < CROWD_CLIP_COUNT; ++c) {
				if (unit_mm[bank][t][c].is_valid()) {
					unit_mm[bank][t][c]->set_instance_count(counts[bank][t][c]);
				}
			}
		}
	}

	const Color col_player(0.55f, 0.72f, 1.0f);
	const Color col_enemy(1.0f, 0.52f, 0.42f);

	std::unordered_set<uint32_t> active_unit_ids;

	for (size_t i = 0; i < uview.count; ++i) {
		uint32_t id = uview.id[i];
		active_unit_ids.insert(id);

		bool is_player = (uview.team[i] == 0);
		uint8_t utype = uview.unit_type[i];
		if (utype >= rtso::UNIT_TYPE_COUNT) utype = rtso::UNIT_VILLAGER;
		uint8_t uaction = uview.action_state[i];
		float hp = uview.hp[i];
		float max_hp = uview.max_hp[i];
		float tx = uview.pos_x[i];
		float tz = uview.pos_z[i];
		float vx = uview.vel_x[i];
		float vz = uview.vel_z[i];
		float speed2 = vx * vx + vz * vz;

		// Smooth visual position interpolation to prevent snapping
		float x = tx;
		float z = tz;
		auto pos_it = crowd_pos.find(id);
		if (pos_it == crowd_pos.end()) {
			crowd_pos[id] = Vector2(tx, tz);
		} else {
			Vector2 cur = pos_it->second;
			Vector2 target(tx, tz);
			float dist_sq = cur.distance_squared_to(target);
			if (dist_sq > 25.0f) {
				cur = target;
			} else {
				float factor = Math::clamp(delta * 10.0f, 0.0f, 1.0f);
				cur = cur.lerp(target, factor);
			}
			crowd_pos[id] = cur;
			x = cur.x;
			z = cur.y;
		}

		// Smooth visual rotation interpolation to prevent snapping
		float yaw = 0.0f;
		auto yaw_it = crowd_yaw.find(id);
		if (yaw_it != crowd_yaw.end()) yaw = yaw_it->second;

		float target_yaw = yaw;
		if (speed2 > 0.001f) {
			// In Godot, forward is -Z. Local -Z rotated by atan2(-vx, -vz) points along (vx, vz)
			target_yaw = Math::atan2(-vx, -vz);
		} else if (uaction == rtso::ACTION_ATTACK || uaction == rtso::ACTION_GATHER || uaction == rtso::ACTION_BUILD) {
			// Face active combat or harvest target
			float tdx = uview.target_x[i] - x;
			float tdz = uview.target_z[i] - z;
			if (tdx * tdx + tdz * tdz > 0.01f) {
				target_yaw = Math::atan2(-tdx, -tdz);
			}
		}

		float diff = Math::wrapf(target_yaw - yaw, -(float)Math_PI, (float)Math_PI);
		yaw += diff * Math::clamp(delta * 14.0f, 0.0f, 1.0f);
		crowd_yaw[id] = yaw;

		// Evaluate skeletal bone pose via AnimationController based on action_state
		rtso::AnimState want_anim_state = rtso::AnimState::Idle;
		if (!uview.alive[i] || uaction == rtso::ACTION_DYING) {
			want_anim_state = rtso::AnimState::Death;
		} else if (uaction == rtso::ACTION_ATTACK) {
			want_anim_state = (speed2 > 8.0f) ? rtso::AnimState::Walk : rtso::AnimState::Attack;
		} else if (uaction == rtso::ACTION_GATHER) {
			want_anim_state = rtso::AnimState::Gather;
		} else if (uaction == rtso::ACTION_BUILD) {
			want_anim_state = rtso::AnimState::Build;
		} else if (speed2 > 0.001f || uaction == rtso::ACTION_MOVE || uaction == rtso::ACTION_RETURN_DROPSITE) {
			want_anim_state = rtso::AnimState::Walk;
		} else {
			want_anim_state = rtso::AnimState::Idle;
		}

		anim_controllers[utype].play(want_anim_state);

		int bank = is_player ? 0 : 1;
		uint8_t uciv = is_player ? sim->player_civ() : sim->team_civ(uview.team[i]);
		String utype_str = rtso::civ_unit_model(uciv, utype);
		String cat_units3 = "units";
		auto entry = rtso::ModelLibrary::get(cat_units3, utype_str);
		float y = sim->get_terrain_height(x, z) + entry.y_offset;
		float sc = entry.scale > 0.05f ? entry.scale : 1.0f;
		Transform3D xf(Basis(Vector3(0, 1, 0), yaw).scaled(Vector3(sc, sc, sc)), Vector3(x, y, z));
		int clip = crowd_clip_of(uaction, speed2, uview.alive[i] != 0);
		int slot = written[bank][utype][clip]++;
		if (unit_mm[bank][utype][clip].is_valid()) {
			if (detailed.count(id) > 0) {
				Transform3D hidden;
				hidden.origin = Vector3(x, y, z);
				hidden.basis = hidden.basis.scaled(Vector3(0.001f, 0.001f, 0.001f));
				unit_mm[bank][utype][clip]->set_instance_transform(slot, hidden);
			} else {
				unit_mm[bank][utype][clip]->set_instance_transform(slot, xf);
			}
			unit_mm[bank][utype][clip]->set_instance_color(slot, is_player ? col_player : col_enemy);
		}

		if (detailed.count(id) == 0) continue;

		UnitVis &v = acquire_unit_slot(id);
		v.vis_stamp = unit_vis_stamp;
		v.yaw = yaw;
		attach_unit_model(v, rtso::civ_unit_model(uciv, utype));

		if (v.anim && UtilityFunctions::is_instance_valid(v.anim)) {
			v.anim->set_active(true);
			v.anim->set_callback_mode_process(AnimationMixer::ANIMATION_CALLBACK_MODE_PROCESS_MANUAL);
			v.anim->set_speed_scale(1.0);
		} else {
			v.anim = nullptr;
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
		if (v.anim && UtilityFunctions::is_instance_valid(v.anim)) {
			if (!v.anim->has_animation(want_clip)) {
				if (want_clip == "Run" && v.anim->has_animation("Walk")) want_clip = "Walk";
				else if (want_clip == "Walk" && v.anim->has_animation("Run")) want_clip = "Run";
				else if (want_clip == "Build" && v.anim->has_animation("Gather")) want_clip = "Gather";
				else if (v.anim->has_animation("Idle")) want_clip = "Idle";
			}
			play_unit_clip(v, want_clip, loop_clip);
			v.anim->advance((double)delta);
		}

		float dsc = entry.scale > 0.05f ? entry.scale : 1.0f;
		Transform3D dxf(Basis(Vector3(0, 1, 0), v.yaw).scaled(Vector3(dsc, dsc, dsc)),
		               Vector3(x, y, z));
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

	// Clean up stale visual interpolation entries
	if (crowd_pos.size() > uview.count + 64) {
		for (auto it = crowd_pos.begin(); it != crowd_pos.end(); ) {
			if (active_unit_ids.find(it->first) == active_unit_ids.end()) {
				it = crowd_pos.erase(it);
			} else {
				++it;
			}
		}
	}
	if (crowd_yaw.size() > uview.count + 64) {
		for (auto it = crowd_yaw.begin(); it != crowd_yaw.end(); ) {
			if (active_unit_ids.find(it->first) == active_unit_ids.end()) {
				it = crowd_yaw.erase(it);
			} else {
				++it;
			}
		}
	}

	// 3D Positional Combat Acoustics: Melee Weapon Clash (Swords, Spears)
	for (auto it = unit_clash_cooldown.begin(); it != unit_clash_cooldown.end(); ) {
		it->second -= delta;
		if (it->second <= 0.0f) {
			it = unit_clash_cooldown.erase(it);
		} else {
			++it;
		}
	}
	global_clash_timer -= delta;

	int clashes_this_frame = 0;
	for (size_t i = 0; i < uview.count && clashes_this_frame < 4; ++i) {
		if (!uview.alive[i] || uview.action_state[i] != rtso::ACTION_ATTACK) continue;
		uint8_t utype = uview.unit_type[i];
		bool is_spear = (utype == rtso::UNIT_SPEARMAN);
		bool is_sword = (utype == rtso::UNIT_SWORDSMAN || utype == rtso::UNIT_CAVALRY || utype == rtso::UNIT_HERO);
		if (!is_spear && !is_sword) continue;

		uint32_t uid = uview.id[i];
		if (unit_clash_cooldown.find(uid) == unit_clash_cooldown.end()) {
			float tx = uview.target_x[i] - uview.pos_x[i];
			float tz = uview.target_z[i] - uview.pos_z[i];
			float dist_sq = tx * tx + tz * tz;
			if (dist_sq <= 14.0f) { // Within melee contact range
				unit_clash_cooldown[uid] = 0.75f + ((float)(rand() % 40) / 100.0f);
				Vector3 clash_pos(uview.pos_x[i], sim->get_terrain_height(uview.pos_x[i], uview.pos_z[i]) + 1.0f, uview.pos_z[i]);
				float pitch = 0.92f + ((float)(rand() % 20) / 100.0f);
				if (is_spear) {
					int s_idx = rand() % 2;
					play_spatial_sound(sfx_spear_hit[s_idx], clash_pos, -2.0f, 65.0f, pitch);
				} else {
					int s_idx = rand() % 3;
					play_spatial_sound(sfx_sword_hit[s_idx], clash_pos, -1.0f, 65.0f, pitch);
				}
				clashes_this_frame++;
			}
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

		uint8_t bciv = sim->team_civ(b.team);
		const char *mid = rtso::civ_building_model(bciv, b.type);
		const char *rubble_id = rtso::civ_building_rubble(bciv, b.type);
		float b_progress = b.build_progress;
		bool destroyed = (b.hp <= 0.0f || b.wrecked);
		bool prebuild = !destroyed && (b_progress < 1.0f) && !b.committed;
		bool constructing = !destroyed && (b_progress < 1.0f) && b.committed;
		float base_y = sim->get_terrain_height(b.x, b.z);

		if (destroyed) {
			if (v.phase != 3 && v.phase != 4) {
				v.phase = 3;
				v.phase_t = 0.0f;
				play_spatial_sound(sfx_building_collapse, Vector3(b.x, base_y + 1.0f, b.z), 3.0f, 95.0f, 0.95f);
			}
			v.phase_t += delta;
			if (v.phase == 3 && v.phase_t >= 0.70f)
				v.phase = 4;
		} else if (prebuild) {
			v.phase = 1;
			v.phase_t = 0.0f;
		} else if (constructing) {
			v.phase = 2;
			v.phase_t = b_progress;
		} else {
			v.phase = 0;
			v.phase_t = 1.0f;
		}

		String cat_b = "buildings";
		// Collapse = house sinks (no skeletal death clip). Then SpawnEntityOnDeath rubble.
		String visual_id = (v.phase == 4) ? String(rubble_id) : String(mid);
		attach_building_model(v, visual_id);
		auto entry = rtso::ModelLibrary::get(cat_b, visual_id);
		float b_scale = entry.scale > 0.05f ? entry.scale : 1.0f;

		v.root->set_transform(Transform3D(Basis(), Vector3(b.x, base_y, b.z)));
		set_building_site_visible(v, v.phase == 1 || v.phase == 2, v.phase == 2, b.type);

		if (Node3D *n3 = Object::cast_to<Node3D>(v.model)) {
			n3->set_scale(Vector3(b_scale, b_scale, b_scale));
			n3->set_rotation(Vector3());
			if (v.phase == 1) {
				n3->set_visible(false);
			} else if (v.phase == 2) {
				n3->set_visible(true);
				float dy = entry.aabb.size.y * b_scale;
				if (dy < 1.0f)
					dy = 4.0f;
				n3->set_position(Vector3(0, entry.y_offset + (b_progress - 1.0f) * dy, 0));
			} else if (v.phase == 3) {
				n3->set_visible(true);
				float t = Math::clamp(v.phase_t / 0.70f, 0.0f, 1.0f);
				float dy = entry.aabb.size.y * b_scale;
				if (dy < 1.0f)
					dy = 4.0f;
				n3->set_rotation(Vector3(t * 0.22f, t * 0.40f, t * -0.14f));
				n3->set_position(Vector3((t - 0.5f) * 0.45f, entry.y_offset - t * dy * 0.90f, (0.5f - t) * 0.35f));
			} else if (v.phase == 4) {
				n3->set_visible(true);
				float sink = Math::clamp((v.phase_t - 0.70f) / 2.40f, 0.0f, 1.0f);
				n3->set_position(Vector3(0, entry.y_offset - sink * 1.20f, 0));
			} else {
				n3->set_visible(true);
				n3->set_position(Vector3(0, entry.y_offset, 0));
			}
		}

		// Building Health & Construction Progress Bar
		bool show_bhp = !destroyed && (b_progress < 1.0f || b.hp < b.max_hp * 0.98f);
		v.hp_bg->set_visible(show_bhp);
		v.hp_fg->set_visible(show_bhp);

		if (show_bhp) {
			float bar_ratio = (b_progress < 1.0f) ? b_progress : Math::clamp(b.hp / b.max_hp, 0.0f, 1.0f);
			float bar_y = base_y + 5.0f;
			Vector3 hp_pos(b.x, bar_y, b.z);

			v.hp_bg->set_transform(Transform3D(Basis().scaled(Vector3(2.5f, 1.0f, 1.0f)), hp_pos));
			v.hp_fg->set_material_override((b_progress < 1.0f) ? hp_blue_mat : hp_green_mat);

			Basis fg_basis;
			fg_basis.scale(Vector3(2.5f * bar_ratio, 1.0f, 1.0f));
			Vector3 fg_offset((bar_ratio - 1.0f) * 1.1f, 0.0f, 0.02f);
			v.hp_fg->set_transform(Transform3D(fg_basis, hp_pos + fg_offset));
		}

		// 3D Positional Building Fire Acoustics
		auto bit = tracked_buildings.find(b.id);
		if (bit == tracked_buildings.end()) {
			PrevBuildingState st;
			st.id = b.id;
			st.x = b.x;
			st.z = b.z;
			st.hp = b.hp;
			st.max_hp = b.max_hp;
			st.type = b.type;
			st.fire_timer = ((float)(rand() % 100) / 100.0f) * 2.0f;
			tracked_buildings[b.id] = st;
		} else {
			bit->second.hp = b.hp;
			bit->second.max_hp = b.max_hp;
			bit->second.fire_timer += delta;

			// If damaged below 50% HP, periodically trigger fire crackle acoustics
			if (b.hp < b.max_hp * 0.5f && b.hp > 0.0f && bit->second.fire_timer >= 2.8f) {
				bit->second.fire_timer = 0.0f;
				float by = sim->get_terrain_height(b.x, b.z);
				play_spatial_sound(sfx_building_fire, Vector3(b.x, by + 1.5f, b.z), -4.0f, 55.0f, 0.95f + ((float)(rand() % 15) / 100.0f));
			}
		}
	}

	// 3D Positional Building Collapse / Destruction Acoustics
	for (auto it = tracked_buildings.begin(); it != tracked_buildings.end(); ) {
		if (alive_buildings.find(it->first) == alive_buildings.end()) {
			float by = sim->get_terrain_height(it->second.x, it->second.z);
			play_spatial_sound(sfx_building_collapse, Vector3(it->second.x, by + 1.0f, it->second.z), 3.0f, 95.0f, 0.95f);
			it = tracked_buildings.erase(it);
		} else {
			++it;
		}
	}

	for (size_t s = 0; s < building_pool.size(); ++s) {
		if (building_pool[s].root && alive_buildings.find(building_pool[s].sim_id) == alive_buildings.end()) {
			remove_child(building_pool[s].root);
			memdelete(building_pool[s].root);
			building_pool[s].root = nullptr;
			building_pool[s].model = nullptr;
			building_pool[s].foundation = nullptr;
			building_pool[s].scaffold = nullptr;
			building_slot.erase(building_pool[s].sim_id);
		}
	}

	// =========================================================================
	// 3. Flying Projectiles (Ballistic Arrows & Catapult Stones)
	// =========================================================================
	const auto &projectiles = sim->projectiles();
	arrow_mm->set_instance_count((int)projectiles.size());

	std::unordered_set<uint32_t> current_proj_ids;

	for (size_t p = 0; p < projectiles.size(); ++p) {
		const auto &proj = projectiles[p];
		current_proj_ids.insert(proj.id);
		Vector3 pos(proj.current_x, proj.current_y, proj.current_z);

		// Calculate direction of flight
		Vector3 dir(proj.target_x - proj.start_x, 1.2f * (1.0f - 2.0f * proj.progress), proj.target_z - proj.start_z);
		if (dir.length_squared() > 0.001f) dir.normalize();

		// In Godot, CylinderMesh is built along +Y/-Y.
		// Align column 1 (+Y) with 'dir' so the arrow points forward along its flight path.
		Vector3 up(0, 1, 0);
		if (Math::abs(dir.dot(up)) > 0.95f) up = Vector3(0, 0, 1);
		Vector3 right = dir.cross(up).normalized();
		Vector3 forward = right.cross(dir).normalized();
		Basis rot(right, dir, forward);

		Transform3D xf(rot, pos);
		arrow_mm->set_instance_transform((int)p, xf);

		// 3D Positional Projectile Release Acoustics (Bow string whoosh / catapult launch)
		auto pit = tracked_projectiles.find(proj.id);
		if (pit == tracked_projectiles.end()) {
			bool is_crush = (proj.damage_type == 2 || proj.arc_height > 4.0f);
			PrevProjState st;
			st.id = proj.id;
			st.target_x = proj.target_x;
			st.target_y = proj.target_y;
			st.target_z = proj.target_z;
			st.last_progress = proj.progress;
			st.is_crush = is_crush;
			tracked_projectiles[proj.id] = st;

			Vector3 launch_pos(proj.start_x, proj.start_y, proj.start_z);
			float pitch = 0.95f + ((float)(rand() % 15) / 100.0f);
			if (is_crush) {
				play_spatial_sound(sfx_catapult_blast, launch_pos, -3.0f, 90.0f, 0.75f);
			} else {
				int s_idx = rand() % 2;
				play_spatial_sound(sfx_bow_shoot[s_idx], launch_pos, -2.0f, 70.0f, pitch);
			}
		} else {
			pit->second.last_progress = proj.progress;
			pit->second.target_x = proj.target_x;
			pit->second.target_y = proj.target_y;
			pit->second.target_z = proj.target_z;
		}
	}

	// 3D Positional Projectile Impact Acoustics (Arrow hit / Catapult stone blast)
	for (auto it = tracked_projectiles.begin(); it != tracked_projectiles.end(); ) {
		if (current_proj_ids.find(it->first) == current_proj_ids.end()) {
			Vector3 hit_pos(it->second.target_x, it->second.target_y, it->second.target_z);
			float pitch = 0.92f + ((float)(rand() % 18) / 100.0f);
			if (it->second.is_crush) {
				play_spatial_sound(sfx_catapult_blast, hit_pos, 2.0f, 100.0f, pitch);
			} else {
				play_spatial_sound(sfx_arrow_hit, hit_pos, 0.0f, 65.0f, pitch);
			}
			it = tracked_projectiles.erase(it);
		} else {
			++it;
		}
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

	// All wood on tree.glb — pine/palm were easy to mistake for walking fauna
	tree_a_mm->set_instance_count((int)(ta + tb + tc));
	tree_b_mm->set_instance_count(0);
	tree_c_mm->set_instance_count(0);
	gold_mm->set_instance_count((int)g);
	stone_mm->set_instance_count((int)st);

	size_t ia = 0, ig = 0, ist = 0;
	String cat_veg = "vegetation";
	String id_tree = "tree"; auto e_tree = rtso::ModelLibrary::get(cat_veg, id_tree);
	String id_gold = "gold_vein"; auto e_gold = rtso::ModelLibrary::get(cat_veg, id_gold);
	String id_stone = "stone_mine"; auto e_stone = rtso::ModelLibrary::get(cat_veg, id_stone);

	for (size_t i = 0; i < rview.count; ++i) {
		if (!rview.alive[i] || rview.amount[i] <= 0) continue;
		float rx = rview.x[i], rz = rview.z[i];
		float ry = sim->get_terrain_height(rx, rz);
		uint32_t h = rview.id[i];
		Basis rot(Vector3(0, 1, 0), (float)(h % 12) * 0.52f);

		if (rview.type[i] == rtso::RES_WOOD) {
			Transform3D xf(rot, Vector3(rx, ry + e_tree.y_offset, rz));
			tree_a_mm->set_instance_transform((int)ia++, xf);
		} else if (rview.type[i] == rtso::RES_GOLD) {
			Transform3D xf(rot, Vector3(rx, ry + e_gold.y_offset, rz));
			gold_mm->set_instance_transform((int)ig++, xf);
		} else if (rview.type[i] == rtso::RES_STONE) {
			Transform3D xf(rot, Vector3(rx, ry + e_stone.y_offset, rz));
			stone_mm->set_instance_transform((int)ist++, xf);
		}
	}

	// =========================================================================
	// 5. Wildlife Fauna MultiMeshes
	// =========================================================================
	auto fview = sim->fauna();
	int f_counts[rtso::FAUNA_COUNT] = {};
	for (size_t f = 0; f < fview.count; ++f) {
		if (fview.alive[f] && fview.fauna_type[f] < rtso::FAUNA_COUNT) {
			f_counts[fview.fauna_type[f]]++;
		}
	}
	for (int ft = 0; ft < rtso::FAUNA_COUNT; ++ft) {
		if (fauna_mm[ft].is_valid()) {
			fauna_mm[ft]->set_instance_count(f_counts[ft]);
		}
	}
	int f_written[rtso::FAUNA_COUNT] = {};
	for (size_t f = 0; f < fview.count; ++f) {
		if (!fview.alive[f]) continue;
		uint8_t ft = fview.fauna_type[f];
		if (ft >= rtso::FAUNA_COUNT) continue;
		float fx = fview.pos_x[f];
		float fz = fview.pos_z[f];
		float fy = sim->get_terrain_height(fx, fz);
		float frot = fview.rot_y[f];
		const char *fid = rtso::fauna_type_info(ft).model_id;
		auto fent = rtso::ModelLibrary::get("units", String(fid));
		float fsc = fent.scale > 0.05f ? fent.scale : 1.0f;
		Transform3D xf(Basis(Vector3(0, 1, 0), frot).scaled(Vector3(fsc, fsc, fsc)),
		               Vector3(fx, fy + fent.y_offset, fz));
		int fslot = f_written[ft]++;
		if (fauna_mm[ft].is_valid() && fslot < f_counts[ft]) {
			fauna_mm[ft]->set_instance_transform(fslot, xf);
		}
	}
}
