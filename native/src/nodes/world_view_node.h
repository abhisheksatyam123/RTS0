#ifndef RTSO_WORLD_VIEW_NODE_H
#define RTSO_WORLD_VIEW_NODE_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/plane_mesh.hpp>
#include <godot_cpp/classes/torus_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>

#include "../sim/simulation.h"
#include "../assets/assets.h"
#include "../assets/procedural.h"

using namespace godot;

class RtsoWorldView : public Node3D {
	GDCLASS(RtsoWorldView, Node3D);

	MultiMeshInstance3D *player_mmi = nullptr;
	Ref<MultiMesh> player_mm;
	MultiMeshInstance3D *enemy_mmi = nullptr;
	Ref<MultiMesh> enemy_mm;

	MultiMeshInstance3D *center_mmi = nullptr;
	Ref<MultiMesh> center_mm;
	MultiMeshInstance3D *house_mmi = nullptr;
	Ref<MultiMesh> house_mm;

	MultiMeshInstance3D *tree_mmi = nullptr;
	Ref<MultiMesh> tree_mm;
	MultiMeshInstance3D *gold_mmi = nullptr;
	Ref<MultiMesh> gold_mm;

	MeshInstance3D *terrain = nullptr;

	Ref<Mesh> unit_mesh;
	Ref<Mesh> center_mesh;
	Ref<Mesh> house_mesh;
	Ref<Mesh> tree_mesh;
	Ref<Mesh> gold_mesh;
	Ref<PlaneMesh> plane_mesh;

protected:
	static void _bind_methods();

public:
	RtsoWorldView();
	~RtsoWorldView();

	void _ready() override;
	void sync(const rtso::Simulation *sim, float alpha, const PackedInt32Array &selected_ids);

private:
	Ref<MultiMesh> create_multimesh(const Ref<Mesh> &mesh, bool use_colors = true);
};

#endif
