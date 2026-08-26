#include "model_library.h"
#include "procedural.h"
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
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
		return 1.8f; // infantry — matches 0ad biped ~1.7m
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
	// vegetation / resources
	if (id == "pine") return 8.0f;
	else if (id == "palm") return 7.0f;
	else if (id == "tree") return 6.0f;
	else if (id == "stone_mine") return 1.4f;
	else if (id == "gold_vein") return 1.1f;
	return Math::clamp(natural_h, 0.8f, 8.0f);
}

static MeshInstance3D *find_mesh_instance(Node *n) {
	MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(n);
	if (mi) return mi;
	for (int i = 0; i < n->get_child_count(); ++i) {
		mi = find_mesh_instance(n->get_child(i));
		if (mi) return mi;
	}
	return nullptr;
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
					MeshInstance3D *mi = find_mesh_instance(inst);
					if (mi && mi->get_mesh().is_valid()) {
						e.mesh = mi->get_mesh();
						e.aabb = e.mesh->get_aabb();
						float h = e.aabb.size.y;
						if (h < 0.01f) h = 1.0f;
						float th = target_height(category, id, h);
						e.scale = th / h;
						e.y_offset = -e.aabb.position.y * e.scale;
						e.valid = true;
					}
					memdelete(inst);
					if (e.valid) break;
				}
			}
		}
	}

	if (!e.valid) {
		// procedural fallback keeps the game playable without assets
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
