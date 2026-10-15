#ifndef RTSO_WORLD_VIEW_NODE_H
#define RTSO_WORLD_VIEW_NODE_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/sprite3d.hpp>
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
#include <godot_cpp/classes/decal.hpp>
#include <godot_cpp/classes/gpu_particles3d.hpp>
#include <godot_cpp/classes/particle_process_material.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/sky.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../simulation/simulation.h"
#include "../graphics/assets/unit_template_models.h"
#include "fog_mirage.h"
#include "status_sprites.h"
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/time.hpp>
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
		MeshInstance3D *xp_bg = nullptr; // StatusBars AddExperienceBar
		MeshInstance3D *xp_fg = nullptr;
		Sprite3D *rank_icon = nullptr;   // StatusBars AddRankIcon
		uint8_t rank_shown = 0;
		float anim_debt = 0.0f; // seconds of clip skipped while out of view
		uint32_t sim_id = 0;
		// The entity and model the body skin variant (variant_u) was set for.
		uint32_t variant_id = 0;
		Node *variant_model = nullptr;
		float yaw = 0.0f;
		bool ring_on = false;
		uint32_t vis_stamp = 0;
		String model_id;
		StringName clip;
		Node3D *carry_prop = nullptr; // BoneAttachment3D holding the carried shuttle
		uint8_t carry_clip = 0;       // ClipId the prop was attached for
		int64_t player_rgba = -1;     // owner colour last pushed to the model (-1: none yet)
		int64_t silhouette_rgba = -1; // silhouette colour last pushed (-1: none yet, -2: silhouettes off)
	};

	// What a structure's decals or particles were made for: its GLB id, scale and
	// seat, compared as numbers (a formatted String key per structure per frame
	// was ~4% of a late-game frame) to the rounding the old key kept.
	struct FxKey {
		String glb;
		float k = 0.0f, x = 0.0f, z = 0.0f;
		bool made = false;
		bool same(const String &g, float nk, float nx, float nz) const {
			return made && g == glb && (g.is_empty() || (std::fabs(nk - k) < 5e-5f && std::fabs(nx - x) < 5e-4f && std::fabs(nz - z) < 5e-4f));
		}
		void set(const String &g, float nk, float nx, float nz) {
			glb = g;
			k = nk;
			x = nx;
			z = nz;
			made = true;
		}
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
		int64_t player_rgba = -1; // owner colour last pushed (-1: none yet)
		String model_id;
		// 0 A.D. damage variants: smoke from mediumdamage, fire from heavydamage.
		GPUParticles3D *fx_smoke = nullptr;
		GPUParticles3D *fx_flame = nullptr;
		uint8_t fx_level = 0;
		MeshInstance3D *flag = nullptr; // 0 A.D. garrison flag, shown while garrisoned
		Node3D *upgrade_scaffold = nullptr; // child of `model`: 0 A.D.'s "upgrading" variant scaffold
		uint8_t production = 0; // FoggedBuilding::production the effects below were made for
		std::vector<Node3D *> production_fx; // the "researching"/"training" variant's particles and animated props
		// 0 A.D.'s construction_dust (the foundation's "scaffold" variant) or
		// destruction_dust_small (the "upgrading" variant) on the actor's root.
		GPUParticles3D *site_dust = nullptr;
		uint8_t site_dust_kind = 0; // 0 none, 1 construction, 2 upgrading
		float site_dust_poll = 0.0f; // seconds until the builders are counted again
		// 0 A.D.'s ground decals (props/structures/decals/*.xml) under the model or foundation drawn.
		std::vector<Decal *> decals;
		FxKey decal_key; // the GLB id, scale and position the decals were laid for
		String foundation_glb; // the foundation GLB set_building_site_visible drew
		// The particles 0 A.D.'s structure or rubble actor always props (chimney smoke,
		// temple fires, burning rubble), with their GLB positions for re-seating.
		std::vector<GPUParticles3D *> ambient_fx;
		std::vector<Vector3> ambient_pos;
		FxKey ambient_key; // the GLB id and scale they were made for
		// The dust and smoke of the actor's "death" variant, raised when it collapses.
		std::vector<GPUParticles3D *> death_fx;
		std::vector<Vector3> death_pos;
		FxKey death_key;
	};

	std::vector<UnitVis> unit_pool;
	std::vector<size_t> unit_free;
	std::unordered_map<uint32_t, size_t> unit_slot;

	std::vector<BuildingVis> building_pool;
	std::unordered_map<uint32_t, size_t> building_slot;

	// static resources batched
	// Up to TREE_KINDS tree models per biome (pick_tree_kinds), chosen per
	// tree by its id like 0 A.D. random maps mix a biome's forest templates.
	// Each draws up to TREE_VARIANTS models of its 0 A.D. actor (<id>.glb, <id>_v2.glb
	// ..., tools/fetch_0ad_trees.py), one per tree by its id as 0 A.D. picks variants.
	static constexpr int TREE_KINDS = 3;
	static constexpr int TREE_VARIANTS = 4;
	MultiMeshInstance3D *tree_mmi[TREE_KINDS][TREE_VARIANTS] = {};
	Ref<MultiMesh> tree_mm[TREE_KINDS][TREE_VARIANTS];
	float tree_scale[TREE_KINDS] = { 1.0f, 1.0f, 1.0f };
	float tree_y[TREE_KINDS][TREE_VARIANTS] = {};
	int tree_variants[TREE_KINDS] = { 1, 1, 1 };
	int tree_kind_count = 1;
	std::string tree_biome_;
	void pick_tree_kinds(const std::string &biome);
	// Trees and fruit bushes with their own FloraKind (a ported map's templates, a
	// random map's rmbiome trees) draw that plant, one batch per kind; FLORA_DEFAULT
	// ones take the biome's pick above.
	static constexpr int FLORA_KINDS = rtso::FLORA_COUNT; // the 27 families, then one per template (flora_data.h)
	MultiMeshInstance3D *flora_mmi[FLORA_KINDS][TREE_VARIANTS] = {};
	Ref<MultiMesh> flora_mm[FLORA_KINDS][TREE_VARIANTS];
	float flora_scale[FLORA_KINDS] = {};
	float flora_y[FLORA_KINDS][TREE_VARIANTS] = {};
	int flora_variants[FLORA_KINDS] = {};
	// Swaying copies of tree meshes (shaders/tree_wind.gdshader), by source mesh.
	std::unordered_map<int64_t, Ref<Mesh>> wind_cache_;
	// 0 A.D. carry shuttles (art/actors/props/units/shuttle_*.xml) and the
	// animation libraries that already hold the clips added from .psa files.
	Ref<PackedScene> carry_scenes_[7];
	std::unordered_set<int64_t> clip_libs_done_;
	// data/garrison_flags.json: the "garrisoned" prop point of each building
	// model and the flag style its 0 A.D. actor props there.
	struct FlagPoint { Vector3 pos; float scale = 1.0f; int style = 0; };
	std::unordered_map<std::string, FlagPoint> flag_points_;
	// Same file: GLB units per 0 A.D. metre and the GLB height of 0 A.D.'s ground
	// plane, by building model (meshes are grounded at their lowest vertex).
	struct ModelGround { float scale = 0.0f; float ground = 0.0f; };
	std::unordered_map<std::string, ModelGround> model_ground_;
	std::vector<Ref<ShaderMaterial>> flag_mats_;
	Ref<Mesh> flag_mesh_;
	bool flags_loaded_ = false;
	void load_garrison_flags();
	// data/upgrade_scaffolds.json (tools/extract_upgrade_scaffolds.py): the scaffold
	// 0 A.D.'s "upgrading" actor variant props on a building GLB, in the GLB's units.
	struct UpgradeScaffold { String id; float scale = 1.0f; Vector3 offset; };
	std::unordered_map<std::string, UpgradeScaffold> upgrade_scaffolds_;
	bool upgrade_scaffolds_loaded_ = false;
	void update_upgrade_scaffold(BuildingVis &v, const String &model_id, bool show);
	// data/production_fx.json (tools/extract_production_fx.py): the particles 0 A.D.'s
	// "researching" and "training" actor variants prop on a building GLB, and the
	// animated actors they prop (the German forger and temple crows).
	struct ProductionFx { uint8_t kind = 0; Vector3 pos; }; // kind: 0 sparks, 1 smoke, 2 flame
	struct ProductionActor { String glb; Vector3 pos; float yaw = 0.0f; }; // an animated unit GLB at 0 A.D. metres
	struct ProductionFxSet { float metre = 0.25f; std::vector<ProductionFx> fx[2]; std::vector<ProductionActor> actors[2]; };
	std::unordered_map<std::string, ProductionFxSet> production_fx_;
	bool production_fx_loaded_ = false;
	Ref<QuadMesh> fx_sparks_quad_, fx_forge_smoke_quad_;
	void update_production_fx(BuildingVis &v, const String &model_id, uint8_t state, float b_scale, float y_offset);
	Ref<QuadMesh> fx_dust_quad_;
	// data/building_decals.json: each building GLB's ground decals, in its units.
	struct BuildingDecal { String tex; Vector2 pos, size; float angle = 0.0f; };
	std::unordered_map<std::string, std::vector<BuildingDecal>> building_decals_;
	std::unordered_map<std::string, Ref<Texture2D>> decal_tex_;
	bool building_decals_loaded_ = false;
	static constexpr uint32_t DECAL_LAYER = 1u << 19; // the terrain's extra render layer: decals land only on it
	void update_building_decals(BuildingVis &v, const String &glb_id, float k, const Vector3 &base, float brightness);
	// data/ambient_fx.json (tools/extract_ambient_fx.py): the particle props of each
	// building GLB's actor, and the 0 A.D. particle files they name.
	struct AmbientFx { std::string particle; Vector3 pos; };
	struct AmbientFxSet { float metre = 0.25f; std::vector<AmbientFx> fx; };
	struct AmbientParticle {
		String tex;
		bool add = false, start_full = false, relative = false;
		float rate = 0.0f, growth = 0.0f;
		Vector2 life, size, color, angle, spin;
		Vector2 pos[3], vel[3];
		Vector3 force;
	};
	std::unordered_map<std::string, AmbientFxSet> ambient_fx_, death_fx_;
	std::unordered_map<std::string, AmbientParticle> ambient_particles_;
	std::unordered_map<std::string, Ref<QuadMesh>> ambient_quads_;
	bool ambient_fx_loaded_ = false;
	// death: the "death" variant's emitters (v.death_fx) instead of the always-on ones.
	void update_ambient_fx(BuildingVis &v, const String &glb_id, float k, const Vector3 &base, bool death = false);
	void load_ambient_fx();
	GPUParticles3D *make_ambient_emitter(const AmbientParticle &d, float m);
	// The dust 0 A.D.'s "Run" variant props (dust_cav) under running cavalry, camels,
	// elephants and animals (per unit GLB its particles), the flaming stones' trail,
	// and one world-space emitter per particle and scale that emit_particle feeds.
	HashMap<String, std::vector<std::string>> run_fx_;
	std::vector<std::string> stone_fx_; // the flaming stones' flame and smoke
	std::map<std::pair<const AmbientParticle *, int>, GPUParticles3D *> world_emitters_;
	void emit_run_dust(const String &glb_id, const Vector3 &at, float sc, float delta);
	void emit_world_fx(const std::vector<std::string> &names, const Vector3 &at, float sc, float delta);
	void emit_world_fx(const std::string &name, const Vector3 &at, float sc, float delta);
	HashMap<String, AmbientFxSet> unit_fx_; // particles a unit actor always props
	void emit_unit_fx(const String &glb_id, const Transform3D &xf, float sc, float delta);
	// Unit models started loading on a worker before their first draw (per civ and crowd key).
	std::vector<bool> prefetched_;
	float prefetch_poll_ = 0.0f;
	void prefetch_unit_models(const rtso::Simulation *sim, const rtso::Simulation::UnitView &uview, float delta);
	void update_site_dust(BuildingVis &v, uint8_t kind, int builders, const String &model_id, float b_scale);
	void update_garrison_flag(BuildingVis &v, const String &model_id, bool show, const Color &owner_color,
			float b_scale, float y_offset);
	// World height of a unit standing on a turret point `offset` above the terrain
	// (sim units), placed on the holder's model as drawn.
	float turret_draw_y(const rtso::Simulation *sim, uint32_t unit_id, float offset, float x, float z);
	Ref<Shader> wind_shader_;
	Ref<Mesh> wind_mesh(const Ref<Mesh> &src);
	// Mines draw the 0.28 large and small metal and stone mine of the map's biome
	// (maps/random/rmbiome metalLarge, metalSmall, stoneLarge, stoneSmall; pick_mine_models),
	// falling back to gold_vein.glb and stone_mine.glb.
	enum { MINE_METAL_LARGE, MINE_METAL_SMALL, MINE_STONE_LARGE, MINE_STONE_SMALL, MINE_KINDS };
	MultiMeshInstance3D *mine_mmi[MINE_KINDS] = {};
	Ref<MultiMesh> mine_mm[MINE_KINDS];
	float mine_y[MINE_KINDS] = {};
	std::string mine_biome_ = "-";
	void pick_mine_models(const std::string &biome);
	// A mine is large (template_gaia_*_large, 5000) or small (1000, a cut quarry 2000) by its
	// supply when first drawn, so a worked large mine keeps its model. Cleared on a new match.
	std::unordered_map<uint32_t, bool> mine_large_;
	// Each supply's ResourceSupply/Max for its status bar: its amount when the match
	// (or this view) began, or when it first appeared. Cleared with mine_large_.
	std::unordered_map<uint32_t, int32_t> supply_max_;
	float mine_large_time_ = -1.0f;
	std::string mine_large_map_;
	MultiMeshInstance3D *food_mmi = nullptr;   // fruit_bush.glb
	MultiMeshInstance3D *fish_mmi = nullptr;   // fish.glb (0 A.D. fauna/fish_generic.xml)
	Ref<MultiMesh> food_mm, fish_mm;
	// Gaia ruins (a stone/metal supply, buildings/ruins.glb) and treasures (a placeholder chest).
	MultiMeshInstance3D *ruins_mmi = nullptr;
	// One multimesh per treasure type: its 0.28 actor (gaia_data.h TreasureTypeInfo::model_id).
	MultiMeshInstance3D *treasure_mmi[rtso::TREASURE_TYPE_COUNT] = {};
	Ref<MultiMesh> treasure_mm[rtso::TREASURE_TYPE_COUNT];
	float treasure_y_offset[rtso::TREASURE_TYPE_COUNT] = {};
	Ref<MultiMesh> ruins_mm;
	// Gaia scenery (fences, columns, obelisks): 0 A.D.'s actor meshes from
	// data/scenery_actors.json, one MultiMesh per type and mesh variant; a box of
	// the obstruction's size for a type without one.
	MultiMeshInstance3D *scenery_mmi = nullptr;
	Ref<MultiMesh> scenery_mm;
	struct SceneryVariant { Ref<MultiMesh> mm; int count = 0; };
	std::vector<SceneryVariant> scenery_variants_[rtso::SCENERY_TYPE_COUNT];
	void load_scenery_actors();
	// 0 A.D. ambient birds (template_bird: birds/buzzard, the hawk actor): no
	// gameplay effect, so they live in the view only, from data/birds.json.
	struct Bird {
		Node3D *node = nullptr;
		Vector2 pos, target;
		float yaw = 0.0f, y = 0.0f;
		uint32_t rng = 1;
	};
	std::vector<Bird> birds_;
	std::string birds_map_;
	void update_birds(const rtso::Simulation *sim, float delta);

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
	void apply_biome_environment(const std::string &biome, const std::string &map_id, uint8_t daytime = 0);
	bool apply_sky_box(const godot::Ref<godot::Sky> &sky, const std::string &set, const std::string &map_id, float sun_yaw, const godot::Color &ground);
	godot::Ref<godot::Shader> sky_box_shader_;

	Ref<TorusMesh> ring_mesh;
	Ref<StandardMaterial3D> ring_sel_mat;
	std::vector<MeshInstance3D *> fauna_rings_; // selection rings of the player's own selected livestock
	Ref<StandardMaterial3D> ring_enemy_mat;
	// selection.js setHighlightList: the hovered or band-boxed entities, ringed at g_HighlightedAlpha.
	Ref<StandardMaterial3D> ring_hl_mat, ring_enemy_hl_mat;
	std::unordered_set<int32_t> highlight_set_;
	std::unordered_set<int32_t> sel_buildings_; // the selected structures (outlined)
	Ref<BoxMesh> outline_mesh_;
	std::vector<MeshInstance3D *> outline_bars_;
	void draw_structure_outlines(const rtso::Simulation *sim);

	Ref<BoxMesh> hp_bg_mesh;
	Ref<BoxMesh> hp_fg_mesh;
	Ref<StandardMaterial3D> hp_bg_mat;
	Ref<StandardMaterial3D> hp_green_mat;
	Ref<StandardMaterial3D> hp_red_mat;
	Ref<StandardMaterial3D> hp_blue_mat;
	Ref<StandardMaterial3D> xp_mat;
	Ref<BoxMesh> xp_bg_mesh;
	Ref<BoxMesh> xp_fg_mesh;

	// Crowd units: one MultiMesh per (civ, UnitType, crowd clip), made the first
	// time a unit of that civ and type shows, so every civ in the match renders
	// with its own 0 A.D. models. Each clip is a few baked poses cycled in place.
	static constexpr int CROWD_CLIP_COUNT = 61; // see crowd_clips() in world_view_node.cpp
	static constexpr int FAUNA_CLIP_COUNT = 6;  // idle, move, attack, death, idle2, death2
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
		// The model's id and the ModelLibrary entry values a frame's unit loop reads.
		String model_id;
		float y_offset = 0.0f, scale = 1.0f, height = 0.0f;
		bool player_color = false; // has 0 A.D. player_trans surfaces (owner colour in custom data)
	};
	// Crowd keys: a unit type, UNIT_TYPE_COUNT + type for a siege engine
	// shown packed (its 0 A.D. wagon, packed_siege_models.h), or
	// 2 * UNIT_TYPE_COUNT + k for a roster template drawn with its own 0 A.D.
	// actor (unit_template_draw_model(k), unit_template_models.h).
	static constexpr int CROWD_KEY_COUNT = 2 * rtso::UNIT_TYPE_COUNT + rtso::UNIT_TEMPLATE_DRAW_MODEL_COUNT;
	std::unique_ptr<CrowdBatch> crowd_[rtso::CIV_COUNT][CROWD_KEY_COUNT];
	uint8_t crowd_key_utype_[CROWD_KEY_COUNT] = {}; // a unit type drawn by each key
	// The (civ << 16 | key) of every crowd batch made, ascending, and of those drawing units
	// this frame (crowd_seen_ marks them), so a frame visits those batches only.
	std::vector<uint32_t> crowd_made_;
	std::vector<uint32_t> crowd_seen_list_;
	uint8_t crowd_seen_[rtso::CIV_COUNT][CROWD_KEY_COUNT] = {};
	Node3D *crowd_bake_root = nullptr;
	// Crowd clip baking is spread over frames: past this frame's deadline (set in sync)
	// a clip waits for a later frame, drawn in the rest pose meanwhile; one clip always
	// bakes per frame, so new clips keep arriving. A late-game army's first new clips
	// otherwise froze the client for 0.2-0.8 s.
	static constexpr uint64_t CROWD_BAKE_BUDGET_US = 4000;
	uint64_t crowd_bake_deadline_us_ = 0;
	bool crowd_baked_this_frame_ = false;
	bool crowd_bake_over_budget();
	// The crowd clip being baked, pose by pose: a clip's poses may span frames, and its
	// model instance stays for the batch's later clips.
	struct CrowdBakeJob;
	std::unique_ptr<CrowdBakeJob> crowd_job_;
	void crowd_job_begin_clip(CrowdBatch &b, int c);
	void crowd_job_bake_poses();
	void crowd_job_release();
	std::vector<uint8_t> crowd_slot_scratch_; // crowd clip slot per unit index this frame
	// Formation.js GetFormationAnimationVariant per formation member this frame, and which
	// variants each crowd key's 0 A.D. actor lists (bit 31: looked up).
	std::unordered_map<uint32_t, uint8_t> form_variant_;
	uint32_t crowd_form_mask_[rtso::CIV_COUNT][CROWD_KEY_COUNT] = {};
	uint32_t crowd_form_mask(uint8_t civ, uint16_t key);

	// wildlife fauna batched (idle / move / attack / death / second idle / second death)
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
	// 0 A.D.'s "silhouettes" option (shaders/silhouette.gdshader): units hidden
	// behind structures, trees or terrain show their outline in the owner's colour.
	Ref<ShaderMaterial> silhouette_mat_;
	bool silhouettes_ = true;
	uint32_t silhouette_gen_ = 0xFFFFFFFFu;
	bool cinema_playing_ = false; // Simulation::cinema_playing() at the last sync
	void refresh_silhouettes();
	void set_silhouette(Node *model, const Color &c);

	// 3D Positional Combat Acoustics
	static constexpr size_t SPATIAL_AUDIO_POOL_SIZE = 32;
	AudioStreamPlayer3D *spatial_players[SPATIAL_AUDIO_POOL_SIZE] = {nullptr};
	size_t next_spatial_idx = 0;
	static constexpr int kMaxSpatialVoices = 16;
	int spatial_dropped_ = 0;
	uint64_t spatial_end_ms_[SPATIAL_AUDIO_POOL_SIZE] = {}; // when each voice's sound ends

public:
	int spatial_voices_playing() const {
		const uint64_t now = Time::get_singleton()->get_ticks_msec();
		int n = 0;
		for (size_t i = 0; i < SPATIAL_AUDIO_POOL_SIZE; ++i) n += spatial_players[i] && now < spatial_end_ms_[i];
		return n;
	}
	int spatial_sounds_dropped() const { return spatial_dropped_; }

private:



	struct PrevBuildingState {
		uint32_t id = 0;
		float x = 0.0f, z = 0.0f;
		float hp = 0.0f;
		float max_hp = 0.0f;
		uint8_t type = 0;
		bool live = true; // last frame showed the live structure, not a mirage
	};
	std::unordered_map<uint32_t, PrevBuildingState> tracked_buildings;


protected:
	static void _bind_methods();

public:
	RtsoWorldView();
	~RtsoWorldView();

	void _ready() override;
	void sync(const rtso::Simulation *sim, float delta, const PackedInt32Array &selected_ids);
	// The entities under the cursor or inside the band box (0 A.D. EntitySelection.setHighlightList).
	void set_selected_buildings(const std::vector<uint32_t> &ids) {
		sel_buildings_.clear();
		for (uint32_t id : ids) sel_buildings_.insert((int32_t)id);
	}
	bool structure_outlined(uint32_t id) const { return sel_buildings_.count((int32_t)id) > 0; }
	// The structures the viewer sees, live or as mirages (what the world draws).
	const std::vector<rtso::FoggedBuilding> &seen_buildings() const { return mirages_.buildings(); }
	// The supplies as the local player last saw them (hover picks these, so a felled
	// tree in the fog stays hoverable as its mirage).
	const std::vector<rtso::FoggedResource> &resource_mirages() const { return mirages_.resources(); }
	void set_highlight_ids(const PackedInt32Array &ids) {
		highlight_set_.clear();
		for (int i = 0; i < ids.size(); ++i) highlight_set_.insert(ids[i]);
	}
	void rebuild_terrain(const rtso::Simulation *sim);
	// Player whose units draw as "own" (crowd bank 0, player colours).
	void set_local_player(uint8_t p) { local_player_ = p; }
	// session.showstatusbars (Tab): bars over every visible unit and structure.
	void set_show_all_status_bars(bool on) { show_all_status_bars_ = on; }
	bool show_all_status_bars() const { return show_all_status_bars_; }
	// StatusBars.js sprites drawn this frame (tests).
	int status_sprite_count() const { return status_sprites_.sprite_count(); }
	const PackedStringArray &status_rank_icons() const { return status_rank_icons_; } // drawn last frame (tests)
	int status_xp_bars() const { return status_xp_bars_; }

	void play_spatial_sound(const Ref<AudioStream> &stream, Vector3 pos, float volume_db = 0.0f, float max_dist = 75.0f, float pitch_scale = 1.0f);
	void play_spatial_sound_path(const String &path, Vector3 pos, float volume_db = 0.0f, float max_dist = 75.0f, float pitch_scale = 1.0f);

	// The building model a structure of this civ, type and (WallSet) half length draws; the
	// wall placement ghost previews its pieces with it.
	static String building_visual_id(uint8_t civ, uint8_t type, float half_size);

private:
	uint8_t local_player_ = 0;
	bool show_all_status_bars_ = false;
	// StatusBars.js sprites of every entity with status bars on (status_sprites.h).
	RtsoStatusSprites status_sprites_;
	bool status_sprites_built_ = false;
	struct AuraIconSource {
		const rtso::AuraSource *src;
		int icon;
	};
	std::vector<AuraIconSource> aura_icon_sources_;
	// StatusBars.js RegenerateSprites: one entity's sprites, bottom up.
	struct StatusStack {
		float xp = -1.0f, pack = -1.0f, upgrade = -1.0f, supply = -1.0f, health = -1.0f;
		Color health_color;
		const float *capture = nullptr; // capture points per player slot
		float capture_max = 0.0f;
		uint8_t owner = 0xFF;
		int rank_icon = -1;
		int icons[8];
		int n_icons = 0;
	};
	void status_aura_icons(float x, float z, uint8_t team, uint16_t tmpl, bool structure, StatusStack &st) const;
	PackedStringArray status_rank_icons_;
	int status_xp_bars_ = 0;
	void draw_status_stack(const rtso::Simulation *sim, const Vector3 &base, float width, float height, const StatusStack &st);

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
	uint64_t border_colors_ = 0; // display_colors::key the borders were drawn with
	float border_half_ = 0.0f;
	void update_territory_borders(const rtso::Simulation *sim);
	Ref<MultiMesh> create_multimesh(const Ref<Mesh> &mesh, bool use_colors = false);
	UnitVis &acquire_unit_slot(uint32_t sim_id);
	BuildingVis &acquire_building_slot(uint32_t sim_id);
	void attach_unit_model(UnitVis &v, const String &model_name);
	void attach_building_model(BuildingVis &v, const String &model_name);
	void set_building_site_visible(BuildingVis &v, bool foundation_on, bool scaffold_on, uint8_t btype,
			uint8_t civ, const String &model_id, float b_scale);
	// data/foundations.json (tools/extract_foundations.py): each structure's 0 A.D.
	// FoundationActor and the scaffold of its "scaffold" variant, and where an
	// actor's origin lies in each building GLB.
	struct ActorFrame { float scale = 0.0f; Vector3 origin; }; // GLB units per metre, actor origin
	struct FoundationArt { String id; ActorFrame frame; String scaffold; ActorFrame scaffold_frame; };
	std::unordered_map<std::string, std::string> foundation_of_; // "<civ code>/<BuildingType>" -> foundation id
	std::unordered_map<std::string, FoundationArt> foundation_art_;
	std::unordered_map<std::string, ActorFrame> building_frames_;
	// data/rubble.json: each civ's SpawnEntityOnDeath rubble per BuildingType and its actor frame.
	std::unordered_map<std::string, std::string> rubble_of_;
	std::unordered_map<std::string, ActorFrame> rubble_frames_;
	bool rubble_loaded_ = false;
	void load_rubble();
	bool foundations_loaded_ = false;
	void load_foundations();
	static Node *instantiate_building_id(const String &id);
	void play_unit_clip(UnitVis &v, const StringName &clip, bool loop);
	void hide_unit_vis(UnitVis &v);
	CrowdBatch &crowd_batch(uint8_t civ, uint16_t key);
	MultiMesh *crowd_mm(CrowdBatch &b, int clip);
	// Bakes the skinned poses of every clip in `want` (array of ClipId) that `done` lacks.
	void bake_puppet_clips(Node *inst, CrowdPuppet *row, bool *done, const uint8_t *want,
			const bool *needed, int count, uint64_t death_slots, bool female = false);
	// Adds the 0 A.D. clips the unit GLBs lack (per-resource carry, promotion)
	// to a biped's animation library from assets/animations/*.psa.
	void ensure_runtime_clips(AnimationPlayer *ap, bool female);
	// Parents the carry shuttle for `clip` (a carry variant) to its 0 A.D. prop point.
	Node3D *attach_carry_prop(Node *model, uint8_t clip, bool female);
	void cycle_puppet_meshes(CrowdPuppet *row, Ref<MultiMesh> *meshes, const int *counts, int count, uint64_t death_slots);
	void ensure_crowd_frames(uint8_t civ, uint16_t key, const int *counts);
	void ensure_fauna_frames(uint8_t ftype);
	void init_animation_controllers();
};

#endif
