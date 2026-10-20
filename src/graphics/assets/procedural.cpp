#include "procedural.h"

using namespace godot;

namespace rtso {

Ref<Mesh> Procedural::get_unit_mesh(const String &id) {
	if (id == "fishing_boat") {
		Ref<BoxMesh> mesh;
		mesh.instantiate();
		mesh->set_size(Vector3(1.2f, 0.7f, 2.2f));
		return mesh;
	} else if (id == "merchant_ship" || id == "trireme" || id == "ship_merchant") {
		Ref<BoxMesh> mesh;
		mesh.instantiate();
		mesh->set_size(Vector3(1.8f, 1.2f, 3.6f));
		return mesh;
	} else if (id == "bolt_shooter" || id == "ballista" || id == "oxybeles") {
		Ref<BoxMesh> mesh;
		mesh.instantiate();
		mesh->set_size(Vector3(1.4f, 0.9f, 1.6f));
		return mesh;
	} else if (id == "ram" || id == "siege_ram") {
		Ref<BoxMesh> mesh;
		mesh.instantiate();
		mesh->set_size(Vector3(1.2f, 1.0f, 2.2f));
		return mesh;
	} else if (id == "catapult") {
		Ref<BoxMesh> mesh;
		mesh.instantiate();
		mesh->set_size(Vector3(1.6f, 1.3f, 2.0f));
		return mesh;
	} else if (id == "chariot" || id == "scythed_chariot") {
		Ref<BoxMesh> mesh;
		mesh.instantiate();
		mesh->set_size(Vector3(1.4f, 1.1f, 2.4f));
		return mesh;
	}

	Ref<CapsuleMesh> mesh;
	mesh.instantiate();
	float h = 1.2f;
	float r = 0.38f;
	if (id == "archer" || id == "archer_maiden") { h = 1.15f; r = 0.33f; }
	else if (id == "cavalry") { h = 1.6f; r = 0.45f; }
	else if (id == "camel") { h = 1.75f; r = 0.48f; }
	else if (id == "worker_elephant" || id == "elephant") { h = 2.4f; r = 0.85f; }
	else if (id == "war_dog" || id == "dog") { h = 0.65f; r = 0.26f; }
	else if (id == "carnyx") { h = 1.4f; r = 0.35f; }
	else if (id == "female_citizen" || id == "female") { h = 1.15f; r = 0.34f; }
	else if (id == "swordsman") { h = 1.25f; r = 0.38f; }
	else if (id == "spearman") { h = 1.35f; r = 0.38f; }
	else if (id == "healer") { h = 1.2f; r = 0.36f; }
	else if (id == "deer") { h = 1.1f; r = 0.32f; }
	else if (id == "boar") { h = 0.85f; r = 0.38f; }
	else if (id == "sheep") { h = 0.7f; r = 0.30f; }
	else if (id == "wolf") { h = 0.8f; r = 0.28f; }
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
	else if (id == "tower" || id == "wall_tower" || id == "defense_tower") size = Vector3(1.6f, 3.5f, 1.6f);
	else if (id == "wall") size = Vector3(2.0f, 1.8f, 0.6f);
	else if (id == "gate") size = Vector3(3.0f, 2.4f, 1.0f);
	else if (id == "market") size = Vector3(3.2f, 2.2f, 2.5f);
	else if (id == "farmstead") size = Vector3(2.5f, 1.8f, 2.5f);
	else if (id == "storehouse") size = Vector3(2.4f, 1.8f, 2.4f);
	else if (id == "corral") size = Vector3(3.2f, 1.4f, 3.2f);
	else if (id == "temple") size = Vector3(3.6f, 3.0f, 3.6f);
	else if (id == "wonder") size = Vector3(5.0f, 5.0f, 5.0f);
	else if (id == "dock" || id == "shipyard") size = Vector3(3.5f, 1.8f, 3.0f);
	else if (id == "house") size = Vector3(2.0f, 1.8f, 2.0f);
	else if (id == "ruins") size = Vector3(2.0f, 1.2f, 2.0f);
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
