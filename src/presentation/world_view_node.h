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
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/classes/audio_stream_player3d.hpp>
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <unordered_map>
#include <vector>

#include "../simulation/simulation.h"
#include "fog_mirage.h"
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include "../graphics/assets/assets.h"
#include "../graphics/assets/procedural.h"
#include "../graphics/assets/model_library.h"
#include "../animation/animation_controller.h"
#include "../animation/skeleton.h"

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
		StringName clip;
	};

	struct BuildingVis {
		Node3D *root = nullptr;
		Node *model = nullptr;
		Node *foundation = nullptr;
		Node *scaffold = nullptr;
		MeshInstance3D *hp_bg = nullptr;
		MeshInstance3D *hp_fg = nullptr;
		uint32_t sim_id = 0;
		uint8_t type = 0;
		int overlay_type = -1;
		uint8_t phase = 0; // 0 complete, 1 prebuild, 2 construct, 3 collapse, 4 rubble
		float phase_t = 0.0f;
		float build_progress = 1.0f;
		float fog_dim = 1.0f; // brightness last pushed to the fog overlay
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
	MultiMeshInstance3D *food_mmi = nullptr;   // fruit_bush.glb
	Ref<MultiMesh> tree_a_mm, tree_b_mm, tree_c_mm, gold_mm, stone_mm, food_mm;

	// flying projectiles (arrows + warship bolts + catapult stones)
	MultiMeshInstance3D *arrow_mmi = nullptr;
	Ref<MultiMesh> arrow_mm;
	Ref<CylinderMesh> arrow_mesh;
	Ref<StandardMaterial3D> arrow_mat;
	float arrow_scale = 1.0f;
	MultiMeshInstance3D *bolt_mmi = nullptr;
	Ref<MultiMesh> bolt_mm;
	float bolt_scale = 1.0f;
	MultiMeshInstance3D *stone_proj_mmi = nullptr;
	Ref<MultiMesh> stone_proj_mm;
	float stone_proj_scale = 1.0f;

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

	// Crowd units: MultiMesh per (civ-bank, UnitType, anim clip).
	// Bank 0 = player civ meshes, bank 1 = AI civ meshes.
	static constexpr int CROWD_BANK_COUNT = 2;
	static constexpr int CROWD_CLIP_COUNT = 4;
	static constexpr int CROWD_FRAMES_PER_CLIP = 6;
	MultiMeshInstance3D *unit_mmi[CROWD_BANK_COUNT][rtso::UNIT_TYPE_COUNT][CROWD_CLIP_COUNT] = {};
	Ref<MultiMesh> unit_mm[CROWD_BANK_COUNT][rtso::UNIT_TYPE_COUNT][CROWD_CLIP_COUNT];
	struct CrowdPuppet {
		Node *root = nullptr;
		AnimationPlayer *anim = nullptr;
		String clip;
		Ref<ArrayMesh> posed_frames[CROWD_FRAMES_PER_CLIP];
		int baked_count = 0;
		int last_frame = -1;
		rtso::ModelLibrary::CrowdSkin skin;
	};
	CrowdPuppet unit_puppet[CROWD_BANK_COUNT][rtso::UNIT_TYPE_COUNT][CROWD_CLIP_COUNT] = {};
	bool unit_puppet_baked[CROWD_BANK_COUNT][rtso::UNIT_TYPE_COUNT] = {};
	Node3D *crowd_bake_root = nullptr;

	// wildlife fauna batched (idle / move / attack / death)
	MultiMeshInstance3D *fauna_mmi[rtso::FAUNA_COUNT][CROWD_CLIP_COUNT] = {};
	Ref<MultiMesh> fauna_mm[rtso::FAUNA_COUNT][CROWD_CLIP_COUNT];
	CrowdPuppet fauna_puppet[rtso::FAUNA_COUNT][CROWD_CLIP_COUNT] = {};
	bool fauna_puppet_baked[rtso::FAUNA_COUNT] = {};

	std::unordered_map<uint32_t, float> crowd_yaw;
	std::unordered_map<uint32_t, Vector2> crowd_pos;

	// Skeletal animation controllers per unit type
	rtso::AnimationController anim_controllers[rtso::UNIT_TYPE_COUNT];

	static constexpr int MAX_DETAILED_UNITS = 24;
	float anim_time = 0.0f;
	uint32_t unit_vis_stamp = 1;

	// 3D Positional Combat Acoustics
	static constexpr size_t SPATIAL_AUDIO_POOL_SIZE = 32;
	AudioStreamPlayer3D *spatial_players[SPATIAL_AUDIO_POOL_SIZE] = {nullptr};
	size_t next_spatial_idx = 0;

	Ref<AudioStream> sfx_bow_shoot[2];
	Ref<AudioStream> sfx_arrow_fly;
	Ref<AudioStream> sfx_arrow_hit;
	Ref<AudioStream> sfx_catapult_blast;
	Ref<AudioStream> sfx_sword_hit[3];
	Ref<AudioStream> sfx_spear_hit[2];
	Ref<AudioStream> sfx_building_collapse;
	Ref<AudioStream> sfx_building_fire;

	struct PrevProjState {
		uint32_t id = 0;
		float target_x = 0.0f, target_y = 0.0f, target_z = 0.0f;
		float last_progress = 0.0f;
		bool is_crush = false;
	};
	std::unordered_map<uint32_t, PrevProjState> tracked_projectiles;

	struct PrevBuildingState {
		uint32_t id = 0;
		float x = 0.0f, z = 0.0f;
		float hp = 0.0f;
		float max_hp = 0.0f;
		uint8_t type = 0;
		float fire_timer = 0.0f;
		bool live = true; // last frame showed the live structure, not a mirage
	};
	std::unordered_map<uint32_t, PrevBuildingState> tracked_buildings;

	std::unordered_map<uint32_t, float> unit_clash_cooldown;
	float global_clash_timer = 0.0f;

protected:
	static void _bind_methods();

public:
	RtsoWorldView();
	~RtsoWorldView();

	void _ready() override;
	void sync(const rtso::Simulation *sim, float delta, const PackedInt32Array &selected_ids);
	void rebuild_terrain(const rtso::Simulation *sim);
	// Player whose units draw as "own" (crowd bank 0, player colours).
	void set_local_player(uint8_t p) { local_player_ = p; }

	void play_spatial_sound(const Ref<AudioStream> &stream, Vector3 pos, float volume_db = 0.0f, float max_dist = 75.0f, float pitch_scale = 1.0f);
	void play_spatial_sound_path(const String &path, Vector3 pos, float volume_db = 0.0f, float max_dist = 75.0f, float pitch_scale = 1.0f);

private:
	uint8_t local_player_ = 0;

	// Fog of war (0 A.D. Fogging.js/Mirage.js + CLOSTexture): what the local
	// player remembers, and the smoothed LOS bitmap that shades the world.
	rtso::FogMirages mirages_;
	rtso::LosShading los_;
	Ref<Image> los_image_;
	Ref<ImageTexture> los_tex_;
	PackedByteArray los_bytes_;
	float los_upload_timer_ = 0.0f;
	float los_half_ = 0.0f;
	Ref<ShaderMaterial> fog_overlay_mat_; // multiplies a fogged structure by its LOS brightness
	void update_fog(const rtso::Simulation *sim, float delta);
	void bind_los(const Ref<Material> &mat);
	float los_brightness(float x, float z) const;
	void set_fog_overlay(Node *n, float brightness);
	Ref<MultiMesh> create_multimesh(const Ref<Mesh> &mesh, bool use_colors = false);
	UnitVis &acquire_unit_slot(uint32_t sim_id);
	BuildingVis &acquire_building_slot(uint32_t sim_id);
	void attach_unit_model(UnitVis &v, const String &model_name);
	void attach_building_model(BuildingVis &v, const String &model_name);
	void set_building_site_visible(BuildingVis &v, bool foundation_on, bool scaffold_on, uint8_t btype);
	static Node *instantiate_building_id(const String &id);
	void play_unit_clip(UnitVis &v, const StringName &clip, bool loop);
	void hide_unit_vis(UnitVis &v);
	void ensure_unit_batches(const rtso::Simulation *sim);
	void bake_puppet_clips(CrowdPuppet *row, AnimationPlayer *ap,
			rtso::ModelLibrary::CrowdSkin &skin, bool skinned);
	void cycle_puppet_meshes(CrowdPuppet *row, Ref<MultiMesh> *meshes, const int *counts);
	void ensure_crowd_frames(int bank, uint8_t utype, uint8_t civ);
	void ensure_fauna_frames(uint8_t ftype);
	void init_animation_controllers();
};

#endif
