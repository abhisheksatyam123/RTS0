#ifndef RTSO_WORLD_VIEW_NODE_H
#define RTSO_WORLD_VIEW_NODE_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/animation_player.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/plane_mesh.hpp>
#include <godot_cpp/classes/torus_mesh.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <unordered_map>
#include <vector>

#include "../simulation/simulation.h"
#include "../graphics/assets/assets.h"
#include "../graphics/assets/procedural.h"
#include "../graphics/assets/model_library.h"

using namespace godot;

class RtsoWorldView : public Node3D {
	GDCLASS(RtsoWorldView, Node3D);

	// Per-unit visual: real 0ad mesh + selection ring + 3D floating health bar
	struct UnitVis {
		Node3D *root = nullptr;
		Node *model = nullptr;
		AnimationPlayer *anim = nullptr;
		MeshInstance3D *ring = nullptr;
		MeshInstance3D *hp_bg = nullptr;
		MeshInstance3D *hp_fg = nullptr;
		uint32_t sim_id = 0;
		float yaw = 0.0f;
		bool ring_on = false;
		uint32_t vis_stamp = 0;
		String model_id;
		String clip;
	};

	struct BuildingVis {
		Node3D *root = nullptr;
		Node *model = nullptr;
		MeshInstance3D *hp_bg = nullptr;
		MeshInstance3D *hp_fg = nullptr;
		uint32_t sim_id = 0;
		uint8_t type = 0;
		float build_progress = 1.0f;
		String model_id;
	};

	std::vector<UnitVis> unit_pool;
	std::vector<size_t> unit_free;
	std::unordered_map<uint32_t, size_t> unit_slot;

	std::vector<BuildingVis> building_pool;
	std::unordered_map<uint32_t, size_t> building_slot;

	// static resources batched
	MultiMeshInstance3D *tree_a_mmi = nullptr; // tree.glb
	MultiMeshInstance3D *tree_b_mmi = nullptr; // pine.glb
	MultiMeshInstance3D *tree_c_mmi = nullptr; // palm.glb
	MultiMeshInstance3D *gold_mmi = nullptr;   // gold_vein.glb
	MultiMeshInstance3D *stone_mmi = nullptr;  // stone_mine.glb
	Ref<MultiMesh> tree_a_mm, tree_b_mm, tree_c_mm, gold_mm, stone_mm;

	// wildlife fauna batched
	MultiMeshInstance3D *fauna_mmi[rtso::FAUNA_COUNT] = {};
	Ref<MultiMesh> fauna_mm[rtso::FAUNA_COUNT];

	// flying projectiles (arrows)
	MultiMeshInstance3D *arrow_mmi = nullptr;
	Ref<MultiMesh> arrow_mm;
	Ref<CylinderMesh> arrow_mesh;
	Ref<StandardMaterial3D> arrow_mat;

	MeshInstance3D *terrain = nullptr;
	Ref<PlaneMesh> plane_mesh;
	Ref<ArrayMesh> terrain_array_mesh;

	MeshInstance3D *water_mesh_node = nullptr;
	Ref<PlaneMesh> water_mesh;
	Ref<StandardMaterial3D> water_mat;

	Ref<TorusMesh> ring_mesh;
	Ref<StandardMaterial3D> ring_sel_mat;
	Ref<StandardMaterial3D> ring_enemy_mat;

	Ref<BoxMesh> hp_bg_mesh;
	Ref<BoxMesh> hp_fg_mesh;
	Ref<StandardMaterial3D> hp_bg_mat;
	Ref<StandardMaterial3D> hp_green_mat;
	Ref<StandardMaterial3D> hp_red_mat;
	Ref<StandardMaterial3D> hp_blue_mat;

	// Crowd units: MultiMesh per (UnitType, anim clip). Pre-baked keyframe cache supplies static meshes.
	static constexpr int CROWD_CLIP_COUNT = 4;
	static constexpr int CROWD_FRAMES_PER_CLIP = 6;
	MultiMeshInstance3D *unit_mmi[rtso::UNIT_TYPE_COUNT][CROWD_CLIP_COUNT] = {};
	Ref<MultiMesh> unit_mm[rtso::UNIT_TYPE_COUNT][CROWD_CLIP_COUNT];
	struct CrowdPuppet {
		Node *root = nullptr;
		AnimationPlayer *anim = nullptr;
		String clip;
		Ref<ArrayMesh> posed_frames[CROWD_FRAMES_PER_CLIP];
		int baked_count = 0;
		rtso::ModelLibrary::CrowdSkin skin;
	};
	CrowdPuppet unit_puppet[rtso::UNIT_TYPE_COUNT][CROWD_CLIP_COUNT] = {};
	bool unit_puppet_baked[rtso::UNIT_TYPE_COUNT] = {};
	std::unordered_map<uint32_t, float> crowd_yaw;

	static constexpr int MAX_DETAILED_UNITS = 16;
	float anim_time = 0.0f;
	uint32_t unit_vis_stamp = 1;

protected:
	static void _bind_methods();

public:
	RtsoWorldView();
	~RtsoWorldView();

	void _ready() override;
	void sync(const rtso::Simulation *sim, float delta, const PackedInt32Array &selected_ids);
	void rebuild_terrain(const rtso::Simulation *sim);

private:
	Ref<MultiMesh> create_multimesh(const Ref<Mesh> &mesh, bool use_colors = false);
	UnitVis &acquire_unit_slot(uint32_t sim_id);
	BuildingVis &acquire_building_slot(uint32_t sim_id);
	void attach_unit_model(UnitVis &v, const String &model_name);
	void attach_building_model(BuildingVis &v, const String &model_name);
	void play_unit_clip(UnitVis &v, const String &clip, bool loop);
	void hide_unit_vis(UnitVis &v);
	void ensure_unit_batches();
};

#endif
