#include "procedural.h"

using namespace godot;

namespace rtso {

Ref<Mesh> Procedural::get_unit_mesh(const String &id) {
	Ref<CapsuleMesh> mesh;
	mesh.instantiate();
	float h = 1.2f;
	float r = 0.38f;
	if (id == "archer") h = 1.1f;
	else if (id == "cavalry") { h = 1.6f; r = 0.45f; }
	else if (id == "swordsman") h = 1.25f;
	else if (id == "spearman") h = 1.3f;
	else if (id == "healer") h = 1.15f;
	mesh->set_radius(r);
	mesh->set_height(h);
	mesh->set_radial_segments(8);
	mesh->set_rings(1);
	return mesh;
}

Ref<Mesh> Procedural::get_building_mesh(const String &id) {
	Ref<BoxMesh> mesh;
	mesh.instantiate();
	Vector3 size(1.8f, 1.8f, 1.8f);
	if (id == "town_center") size = Vector3(4.0f, 3.5f, 4.0f);
	else if (id == "barracks") size = Vector3(3.0f, 2.5f, 3.0f);
	else if (id == "fortress") size = Vector3(4.5f, 4.0f, 4.5f);
	else if (id == "tower") size = Vector3(1.6f, 3.5f, 1.6f);
	else if (id == "wall") size = Vector3(2.0f, 1.8f, 0.6f);
	else if (id == "market") size = Vector3(3.2f, 2.2f, 2.5f);
	else if (id == "farmstead") size = Vector3(2.5f, 1.8f, 2.5f);
	mesh->set_size(size);
	return mesh;
}

Ref<Mesh> Procedural::get_vegetation_mesh(const String &id) {
	if (id == "tree") {
		Ref<CylinderMesh> mesh;
		mesh.instantiate();
		mesh->set_top_radius(0.18f);
		mesh->set_bottom_radius(0.24f);
		mesh->set_height(2.2f);
		return mesh;
	} else if (id == "fruit_bush" || id == "fish") {
		Ref<SphereMesh> mesh;
		mesh.instantiate();
		mesh->set_radius(id == "fruit_bush" ? 0.6f : 0.3f);
		mesh->set_height(id == "fruit_bush" ? 0.8f : 0.6f);
		return mesh;
	} else {
		Ref<BoxMesh> mesh;
		mesh.instantiate();
		if (id == "stone_mine") mesh->set_size(Vector3(1.4f, 0.9f, 1.4f));
		else if (id == "gold_vein") mesh->set_size(Vector3(1.2f, 0.8f, 1.2f));
		else if (id == "treasure_chest") mesh->set_size(Vector3(0.8f, 0.6f, 1.0f));
		else mesh->set_size(Vector3(1.0f, 1.0f, 1.0f));
		return mesh;
	}
}

} // namespace rtso
