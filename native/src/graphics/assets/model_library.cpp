#include "model_library.h"
#include "procedural.h"
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/animation_player.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/core/memory.hpp>

using namespace godot;

namespace rtso {

HashMap<String, ModelLibrary::Entry> &ModelLibrary::cache() {
	static HashMap<String, Entry> c;
	return c;
}

float ModelLibrary::target_height(const String &category, const String &id, float natural_h) {
	if (category == "units") {
		if (id == "cavalry") return 2.6f;
		if (id == "ram") return 2.0f;
		if (id == "deer") return 1.5f;
		return 1.8f;
	}
	if (category == "buildings") {
		if (id == "town_center") return 6.0f;
		else if (id == "fortress") return 7.0f;
		else if (id == "temple") return 6.4f;
		else if (id == "barracks" || id == "market") return 4.4f;
		else if (id == "wall_tower") return 4.6f;
		else if (id == "wall") return 3.0f;
		else if (id == "house" || id == "farmstead" || id == "storehouse") return 3.4f;
		else if (id == "ruins") return 2.4f;
		return Math::clamp(natural_h, 2.0f, 8.0f);
	}
	if (id == "pine") return 8.0f;
	else if (id == "palm") return 7.0f;
	else if (id == "tree") return 6.0f;
	else if (id == "stone_mine") return 1.4f;
	else if (id == "gold_vein") return 1.1f;
	return Math::clamp(natural_h, 0.8f, 8.0f);
}

struct MeshXform {
	MeshInstance3D *mi = nullptr;
	Transform3D xf;
};

static AnimationPlayer *find_animation_player(Node *n) {
	if (!n) return nullptr;
	if (AnimationPlayer *ap = Object::cast_to<AnimationPlayer>(n)) return ap;
	for (int i = 0; i < n->get_child_count(); ++i) {
		if (AnimationPlayer *ap = find_animation_player(n->get_child(i))) return ap;
	}
	return nullptr;
}

void ModelLibrary::apply_imported_material_fixups(Node *n) {
	if (!n) return;
	MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(n);
	if (mi && mi->get_mesh().is_valid()) {
		Ref<Mesh> mesh = mi->get_mesh();
		for (int s = 0; s < mesh->get_surface_count(); ++s) {
			Ref<Material> mat = mi->get_active_material(s);
			if (!mat.is_valid()) mat = mesh->surface_get_material(s);
			Ref<BaseMaterial3D> bm = mat;
			if (!bm.is_valid()) continue;
			// Packed 0 A.D. atlases bleed into noise at RTS camera distance
			// when mipmapped; keep them sharp (0 A.D. uses aniso + padded DDS).
			bm->set_texture_filter(BaseMaterial3D::TEXTURE_FILTER_LINEAR);
			// AO is baked into COLOR_0. Never sample an occlusion map with atlas UVs.
			bm->set_feature(BaseMaterial3D::FEATURE_AMBIENT_OCCLUSION, false);
			bm->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		}
	}
	for (int i = 0; i < n->get_child_count(); ++i) {
		apply_imported_material_fixups(n->get_child(i));
	}
}

static void collect_mesh_xforms(Node *n, const Transform3D &acc, Vector<MeshXform> &out) {
	Node3D *n3 = Object::cast_to<Node3D>(n);
	Transform3D xf = n3 ? acc * n3->get_transform() : acc;
	MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(n);
	if (mi && mi->get_mesh().is_valid()) {
		MeshXform mx;
		mx.mi = mi;
		mx.xf = xf;
		out.push_back(mx);
	}
	for (int i = 0; i < n->get_child_count(); ++i) {
		collect_mesh_xforms(n->get_child(i), xf, out);
	}
}

static Ref<ArrayMesh> merge_mesh_instances(const Vector<MeshXform> &instances) {
	Ref<ArrayMesh> merged;
	merged.instantiate();
	for (int i = 0; i < instances.size(); ++i) {
		MeshInstance3D *mi = instances[i].mi;
		Ref<Mesh> src = mi->get_mesh();
		if (!src.is_valid()) continue;
		const Transform3D &xf = instances[i].xf;
		for (int s = 0; s < src->get_surface_count(); ++s) {
			Array arrays = src->surface_get_arrays(s);
			if (arrays.size() == 0) continue;

			PackedVector3Array verts = arrays[Mesh::ARRAY_VERTEX];
			PackedVector3Array norms = arrays[Mesh::ARRAY_NORMAL];
			for (int v = 0; v < verts.size(); ++v) {
				verts.set(v, xf.xform(verts[v]));
			}
			if (norms.size() == verts.size()) {
				for (int v = 0; v < norms.size(); ++v) {
					norms.set(v, xf.basis.xform(norms[v]).normalized());
				}
			}

			Array out_arr;
			out_arr.resize(Mesh::ARRAY_MAX);
			out_arr[Mesh::ARRAY_VERTEX] = verts;
			if (norms.size() > 0) out_arr[Mesh::ARRAY_NORMAL] = norms;
			out_arr[Mesh::ARRAY_TEX_UV] = arrays[Mesh::ARRAY_TEX_UV];
			if (arrays.size() > Mesh::ARRAY_TEX_UV2 && arrays[Mesh::ARRAY_TEX_UV2].get_type() != Variant::NIL) {
				out_arr[Mesh::ARRAY_TEX_UV2] = arrays[Mesh::ARRAY_TEX_UV2];
			}
			out_arr[Mesh::ARRAY_INDEX] = arrays[Mesh::ARRAY_INDEX];

			merged->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, out_arr);

			Ref<Material> mat = mi->get_active_material(s);
			if (!mat.is_valid()) mat = src->surface_get_material(s);
			if (mat.is_valid()) {
				merged->surface_set_material(merged->get_surface_count() - 1, mat);
			}
		}
	}
	return merged;
}

ModelLibrary::Entry ModelLibrary::load(const String &category, const String &id) {
	String key = category + String("/") + id;
	Entry e;

	Vector<String> candidates;
	candidates.push_back(String("res://assets/models/") + category + String("/") + id + String(".glb"));
	candidates.push_back(String("res://assets/models/units/") + id + String(".glb"));
	candidates.push_back(String("res://assets/models/buildings/") + id + String(".glb"));
	candidates.push_back(String("res://assets/models/vegetation/") + id + String(".glb"));
	candidates.push_back(String("res://assets/models/") + id + String(".glb"));

	for (int i = 0; i < candidates.size(); ++i) {
		const String &path = candidates[i];
		if (ResourceLoader::get_singleton()->exists(path)) {
			Ref<PackedScene> ps = ResourceLoader::get_singleton()->load(path);
			if (ps.is_valid()) {
				Node *inst = ps->instantiate();
				if (inst) {
					apply_imported_material_fixups(inst);
					e.scene = ps;
					e.animated = find_animation_player(inst) != nullptr;
					Vector<MeshXform> mesh_instances;
					collect_mesh_xforms(inst, Transform3D(), mesh_instances);
					if (mesh_instances.size() > 0) {
						e.mesh = merge_mesh_instances(mesh_instances);
						if (e.mesh.is_valid()) {
							e.aabb = e.mesh->get_aabb();
							e.scale = 1.0f;
							e.y_offset = e.animated ? 0.0f : -e.aabb.position.y;
							e.valid = true;
						}
					}
					memdelete(inst);
					if (e.valid) break;
				}
			}
		}
	}

	if (!e.valid) {
		if (category == "units") {
			e.mesh = Procedural::get_unit_mesh(id);
		} else if (category == "buildings") {
			e.mesh = Procedural::get_building_mesh(id);
		} else {
			e.mesh = Procedural::get_vegetation_mesh(id);
		}
		if (e.mesh.is_valid()) {
			e.aabb = e.mesh->get_aabb();
			e.scale = 1.0f;
			e.y_offset = -e.aabb.position.y;
			e.valid = true;
		}
	}

	cache().insert(key, e);
	return e;
}

const ModelLibrary::Entry &ModelLibrary::get(const String &category, const String &id) {
	String key = category + String("/") + id;
	Entry *found = cache().getptr(key);
	if (found) return *found;
	load(category, id);
	found = cache().getptr(key);
	static Entry empty;
	return found ? *found : empty;
}

void ModelLibrary::clear() {
	cache().clear();
}

} // namespace rtso
