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
#include <godot_cpp/classes/gpu_particles3d.hpp>
#include <godot_cpp/classes/particle_process_material.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <memory>
#include <unordered_map>
#include <vector>

#include "../simulation/simulation.h"
#include "fog_mirage.h"
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/shader.hpp>
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
		float fog_dim = -1.0f; // overlay level last pushed (-1: none yet)
		String model_id;
		// 0 A.D. damage variants: smoke from mediumdamage, fire from heavydamage.
		GPUParticles3D *fx_smoke = nullptr;
		GPUParticles3D *fx_flame = nullptr;
		uint8_t fx_level = 0;
	};

	std::vector<UnitVis> unit_pool;
	std::vector<size_t> unit_free;
	std::unordered_map<uint32_t, size_t> unit_slot;

	std::vector<BuildingVis> building_pool;
	std::unordered_map<uint32_t, size_t> building_slot;

	// static resources batched
	// Up to TREE_KINDS tree models per biome (pick_tree_kinds), chosen per
	// tree by its id like 0 A.D. random maps mix a biome's forest templates.
	static constexpr int TREE_KINDS = 3;
	MultiMeshInstance3D *tree_mmi[TREE_KINDS] = {};
	Ref<MultiMesh> tree_mm[TREE_KINDS];
	float tree_scale[TREE_KINDS] = { 1.0f, 1.0f, 1.0f };
	float tree_y[TREE_KINDS] = {};
	int tree_kind_count = 1;
	std::string tree_biome_;
	void pick_tree_kinds(const std::string &biome);
	// Swaying copies of tree meshes (shaders/tree_wind.gdshader), by source mesh.
	std::unordered_map<int64_t, Ref<Mesh>> wind_cache_;
	Ref<Shader> wind_shader_;
	Ref<Mesh> wind_mesh(const Ref<Mesh> &src);
	MultiMeshInstance3D *gold_mmi = nullptr;   // gold_vein.glb
	MultiMeshInstance3D *stone_mmi = nullptr;  // stone_mine.glb
	MultiMeshInstance3D *food_mmi = nullptr;   // fruit_bush.glb
	MultiMeshInstance3D *fish_mmi = nullptr;   // fish.glb (0 A.D. fauna/fish_generic.xml)
	Ref<MultiMesh> gold_mm, stone_mm, food_mm, fish_mm;

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
	Ref<ImageTexture> water_height_tex_;
	// Ground painted under forests and mines (terrain.gdshader paint_tex).
	Ref<ImageTexture> paint_tex_;
	bool paint_dirty_ = true;
	void build_ground_paint(const rtso::Simulation *sim);
	// Per-biome sun, ambient, fog, sky and water (0 A.D. rmbiome Environment).
	Ref<Environment> env_base_;
	void apply_biome_environment(const std::string &biome);

	Ref<TorusMesh> ring_mesh;
	Ref<StandardMaterial3D> ring_sel_mat;
	Ref<StandardMaterial3D> ring_enemy_mat;

	Ref<BoxMesh> hp_bg_mesh;
	Ref<BoxMesh> hp_fg_mesh;
	Ref<StandardMaterial3D> hp_bg_mat;
	Ref<StandardMaterial3D> hp_green_mat;
	Ref<StandardMaterial3D> hp_red_mat;
	Ref<StandardMaterial3D> hp_blue_mat;

	// Crowd units: one MultiMesh per (civ, UnitType, crowd clip), made the first
	// time a unit of that civ and type shows, so every civ in the match renders
	// with its own 0 A.D. models. Each clip is a few baked poses cycled in place.
	static constexpr int CROWD_CLIP_COUNT = 16; // see kCrowdClips in world_view_node.cpp
	static constexpr int FAUNA_CLIP_COUNT = 4;  // idle, move, attack, death
	static constexpr int CROWD_FRAMES_PER_CLIP = 6;
	struct CrowdPuppet {
		String clip;
		Ref<ArrayMesh> posed_frames[CROWD_FRAMES_PER_CLIP];
		int baked_count = 0;
		int last_frame = -1;
	};
	struct CrowdBatch {
		MultiMeshInstance3D *mmi[CROWD_CLIP_COUNT] = {};
		Ref<MultiMesh> mm[CROWD_CLIP_COUNT];
		CrowdPuppet puppet[CROWD_CLIP_COUNT];
		bool clip_baked[CROWD_CLIP_COUNT] = {};
		Ref<Mesh> rest;
		bool no_scene = false; // model has no skinned scene: rest pose only
	};
	std::unique_ptr<CrowdBatch> crowd_[rtso::CIV_COUNT][rtso::UNIT_TYPE_COUNT];
	Node3D *crowd_bake_root = nullptr;
	std::vector<uint8_t> crowd_slot_scratch_; // crowd clip slot per unit index this frame

	// wildlife fauna batched (idle / move / attack / death)
	MultiMeshInstance3D *fauna_mmi[rtso::FAUNA_COUNT][FAUNA_CLIP_COUNT] = {};
	Ref<MultiMesh> fauna_mm[rtso::FAUNA_COUNT][FAUNA_CLIP_COUNT];
	CrowdPuppet fauna_puppet[rtso::FAUNA_COUNT][FAUNA_CLIP_COUNT] = {};
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
	static constexpr int FOG_OVERLAY_LEVELS = 17;
	Ref<ShaderMaterial> fog_overlay_mats_[FOG_OVERLAY_LEVELS]; // multiply a fogged structure by its LOS brightness
	void update_fog(const rtso::Simulation *sim, float delta);
	void bind_los(const Ref<Material> &mat);
	float los_brightness(float x, float z) const;
	void set_fog_overlay(Node *n, float brightness);

	// Damaged-structure particles (art/particles/smoke.xml, flame.xml).
	Ref<QuadMesh> fx_smoke_quad_, fx_flame_quad_;
	Ref<ParticleProcessMaterial> fx_smoke_ppm_, fx_flame_ppm_;
	void init_damage_fx();
	void update_damage_fx(BuildingVis &v, uint8_t level, float half_size, float height);

	// Territory borders draped on the terrain (0 A.D. CCmpTerritoryManager
	// boundary lines), rebuilt when the territory grid changes.
	MeshInstance3D *border_mi_ = nullptr;
	Ref<ArrayMesh> border_mesh_;
	Ref<ShaderMaterial> border_mat_;
	std::vector<uint8_t> border_grid_cache_;
	float border_half_ = 0.0f;
	void update_territory_borders(const rtso::Simulation *sim);
	Ref<MultiMesh> create_multimesh(const Ref<Mesh> &mesh, bool use_colors = false);
	UnitVis &acquire_unit_slot(uint32_t sim_id);
	BuildingVis &acquire_building_slot(uint32_t sim_id);
	void attach_unit_model(UnitVis &v, const String &model_name);
	void attach_building_model(BuildingVis &v, const String &model_name);
	void set_building_site_visible(BuildingVis &v, bool foundation_on, bool scaffold_on, uint8_t btype);
	static Node *instantiate_building_id(const String &id);
	void play_unit_clip(UnitVis &v, const StringName &clip, bool loop);
	void hide_unit_vis(UnitVis &v);
	CrowdBatch &crowd_batch(uint8_t civ, uint8_t utype);
	MultiMesh *crowd_mm(CrowdBatch &b, int clip);
	// Bakes the skinned poses of every clip in `want` (array of ClipId) that `done` lacks.
	void bake_puppet_clips(Node *inst, CrowdPuppet *row, bool *done, const uint8_t *want,
			const bool *needed, int count, int death_slot);
	void cycle_puppet_meshes(CrowdPuppet *row, Ref<MultiMesh> *meshes, const int *counts, int count, int death_slot);
	void ensure_crowd_frames(uint8_t civ, uint8_t utype, const int *counts);
	void ensure_fauna_frames(uint8_t ftype);
	void init_animation_controllers();
};

#endif
