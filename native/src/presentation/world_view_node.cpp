#include "world_view_node.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/color.hpp>
#include <unordered_set>

using namespace godot;

void RtsoWorldView::_bind_methods() {
	// sync is internal C++ call from RtsoMain
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

void RtsoWorldView::_ready() {
	unit_mesh = rtso::Procedural::get_unit_mesh("villager");
	center_mesh = rtso::Procedural::get_building_mesh("town_center");
	house_mesh = rtso::Procedural::get_building_mesh("house");
	tree_mesh = rtso::Procedural::get_vegetation_mesh("tree");
	gold_mesh = rtso::Procedural::get_vegetation_mesh("gold_vein");

	player_mm = create_multimesh(unit_mesh, true);
	enemy_mm = create_multimesh(unit_mesh, true);
	center_mm = create_multimesh(center_mesh, false);
	house_mm = create_multimesh(house_mesh, false);
	tree_mm = create_multimesh(tree_mesh, false);
	gold_mm = create_multimesh(gold_mesh, false);

	player_mmi = memnew(MultiMeshInstance3D); player_mmi->set_multimesh(player_mm); add_child(player_mmi);
	enemy_mmi = memnew(MultiMeshInstance3D); enemy_mmi->set_multimesh(enemy_mm); add_child(enemy_mmi);
	center_mmi = memnew(MultiMeshInstance3D); center_mmi->set_multimesh(center_mm); add_child(center_mmi);
	house_mmi = memnew(MultiMeshInstance3D); house_mmi->set_multimesh(house_mm); add_child(house_mmi);
	tree_mmi = memnew(MultiMeshInstance3D); tree_mmi->set_multimesh(tree_mm); add_child(tree_mmi);
	gold_mmi = memnew(MultiMeshInstance3D); gold_mmi->set_multimesh(gold_mm); add_child(gold_mmi);

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
}

void RtsoWorldView::sync(const rtso::Simulation *sim, float alpha, const PackedInt32Array &selected_ids) {
	if (!sim) return;

	std::unordered_set<int32_t> sel_set;
	for (int i = 0; i < selected_ids.size(); ++i) sel_set.insert(selected_ids[i]);

	// Units sync
	auto uview = sim->units();
	size_t player_count = 0;
	size_t enemy_count = 0;

	for (size_t i = 0; i < uview.count; ++i) {
		if (!uview.alive[i]) continue;
		if (uview.team[i] == 0) player_count++;
		else enemy_count++;
	}

	player_mm->set_instance_count((int)player_count);
	enemy_mm->set_instance_count((int)enemy_count);

	size_t p_idx = 0, e_idx = 0;
	for (size_t i = 0; i < uview.count; ++i) {
		if (!uview.alive[i]) continue;
		float x = uview.pos_x[i];
		float z = uview.pos_z[i];
		Vector3 pos(x, 0.6f, z);
		Transform3D xform(Basis(), pos);

		bool is_selected = sel_set.find((int32_t)uview.id[i]) != sel_set.end();
		Color col = (uview.team[i] == 0)
			? (is_selected ? Color(0.2f, 1.0f, 0.4f) : Color(0.36f, 0.62f, 0.94f))
			: Color(0.92f, 0.32f, 0.28f);

		if (uview.team[i] == 0) {
			player_mm->set_instance_transform((int)p_idx, xform);
			player_mm->set_instance_color((int)p_idx, col);
			p_idx++;
		} else {
			enemy_mm->set_instance_transform((int)e_idx, xform);
			enemy_mm->set_instance_color((int)e_idx, col);
			e_idx++;
		}
	}

	// Buildings sync
	const auto &buildings = sim->buildings();
	size_t center_cnt = 0, house_cnt = 0;
	for (const auto &b : buildings) {
		if (b.is_center) center_cnt++;
		else house_cnt++;
	}

	center_mm->set_instance_count((int)center_cnt);
	house_mm->set_instance_count((int)house_cnt);

	size_t c_idx = 0, h_idx = 0;
	for (const auto &b : buildings) {
		Vector3 pos(b.x, b.is_center ? 1.75f : 0.9f, b.z);
		Transform3D xform(Basis(), pos);
		if (b.is_center) {
			center_mm->set_instance_transform((int)c_idx++, xform);
		} else {
			house_mm->set_instance_transform((int)h_idx++, xform);
		}
	}

	// Resources sync
	auto rview = sim->resources();
	size_t tree_cnt = 0, gold_cnt = 0;
	for (size_t i = 0; i < rview.count; ++i) {
		if (!rview.alive[i] || rview.amount[i] <= 0) continue;
		if (rview.type[i] == 0) tree_cnt++;
		else gold_cnt++;
	}

	tree_mm->set_instance_count((int)tree_cnt);
	gold_mm->set_instance_count((int)gold_cnt);

	size_t t_idx = 0, g_idx = 0;
	for (size_t i = 0; i < rview.count; ++i) {
		if (!rview.alive[i] || rview.amount[i] <= 0) continue;
		Vector3 pos(rview.x[i], rview.type[i] == 0 ? 1.1f : 0.4f, rview.z[i]);
		Transform3D xform(Basis(), pos);
		if (rview.type[i] == 0) {
			tree_mm->set_instance_transform((int)t_idx++, xform);
		} else {
			gold_mm->set_instance_transform((int)g_idx++, xform);
		}
	}
}
