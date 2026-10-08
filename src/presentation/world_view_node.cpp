#include "world_view_node.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/classes/animation.hpp>
#include <godot_cpp/classes/animation_mixer.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace godot;

void RtsoWorldView::_bind_methods() {
	// Internal C++ sync call
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

// A MultiMesh instance colour reaches a StandardMaterial3D only through
// COLOR (vertex colour as albedo); a mesh without colours reads white.
static void enable_instance_tint(const Ref<Mesh> &mesh) {
	if (mesh.is_null()) return;
	for (int s = 0; s < mesh->get_surface_count(); ++s) {
		Ref<BaseMaterial3D> bm = mesh->surface_get_material(s);
		if (bm.is_valid()) bm->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	}
}

RtsoWorldView::UnitVis &RtsoWorldView::acquire_unit_slot(uint32_t sim_id) {
	auto it = unit_slot.find(sim_id);
	if (it != unit_slot.end()) return unit_pool[it->second];

	size_t slot;
	if (!unit_free.empty()) {
		slot = unit_free.back();
		unit_free.pop_back();
	} else {
		slot = unit_pool.size();
		unit_pool.emplace_back();

		UnitVis &v = unit_pool[slot];
		v.root = memnew(Node3D);
		add_child(v.root);

		v.ring = memnew(MeshInstance3D);
		v.ring->set_mesh(ring_mesh);
		v.ring->set_scale(Vector3(1.0f, 0.12f, 1.0f));
		add_child(v.ring);

		v.hp_bg = memnew(MeshInstance3D);
		v.hp_bg->set_mesh(hp_bg_mesh);
		v.hp_bg->set_material_override(hp_bg_mat);
		add_child(v.hp_bg);

		v.hp_fg = memnew(MeshInstance3D);
		v.hp_fg->set_mesh(hp_fg_mesh);
		v.hp_fg->set_material_override(hp_green_mat);
		add_child(v.hp_fg);
	}
	UnitVis &v = unit_pool[slot];
	v.sim_id = sim_id;
	v.yaw = 0.0f;
	v.root->set_visible(true);
	v.hp_bg->set_visible(false);
	v.hp_fg->set_visible(false);
	unit_slot[sim_id] = slot;
	return v;
}

RtsoWorldView::BuildingVis &RtsoWorldView::acquire_building_slot(uint32_t sim_id) {
	auto it = building_slot.find(sim_id);
	if (it != building_slot.end()) return building_pool[it->second];

	size_t slot = building_pool.size();
	building_pool.emplace_back();
	BuildingVis &v = building_pool[slot];
	v.sim_id = sim_id;
	v.root = memnew(Node3D);
	add_child(v.root);

	v.hp_bg = memnew(MeshInstance3D);
	v.hp_bg->set_mesh(hp_bg_mesh);
	v.hp_bg->set_material_override(hp_bg_mat);
	v.hp_bg->set_scale(Vector3(2.5f, 1.0f, 1.0f));
	add_child(v.hp_bg);

	v.hp_fg = memnew(MeshInstance3D);
	v.hp_fg->set_mesh(hp_fg_mesh);
	v.hp_fg->set_material_override(hp_blue_mat);
	v.hp_fg->set_scale(Vector3(2.5f, 1.0f, 1.0f));
	add_child(v.hp_fg);

	building_slot[sim_id] = slot;
	return v;
}

void RtsoWorldView::init_animation_controllers() {
	anim_controllers[rtso::UNIT_VILLAGER].load_rig(rtso::RigType::Villager);
	anim_controllers[rtso::UNIT_SWORDSMAN].load_rig(rtso::RigType::Biped);
	anim_controllers[rtso::UNIT_SPEARMAN].load_rig(rtso::RigType::Biped);
	anim_controllers[rtso::UNIT_ARCHER].load_rig(rtso::RigType::Archer);
	anim_controllers[rtso::UNIT_CAVALRY].load_rig(rtso::RigType::Mounted);
	anim_controllers[rtso::UNIT_HEALER].load_rig(rtso::RigType::Healer);
	anim_controllers[rtso::UNIT_SIEGE_RAM].load_rig(rtso::RigType::SiegeRam);
	anim_controllers[rtso::UNIT_HERO].load_rig(rtso::RigType::Biped);
	anim_controllers[rtso::UNIT_TRADER].load("trader");
	anim_controllers[rtso::UNIT_ARCHER_MAIDEN].load_rig(rtso::RigType::Archer);
	anim_controllers[rtso::UNIT_CARNYX].load_rig(rtso::RigType::Biped);
	anim_controllers[rtso::UNIT_ELEPHANT].load("elephant");
	anim_controllers[rtso::UNIT_WORKER_ELEPHANT].load("elephant");
	anim_controllers[rtso::UNIT_CAMEL].load("camel");
	anim_controllers[rtso::UNIT_CHARIOT].load("chariot");
	anim_controllers[rtso::UNIT_SCYTHED_CHARIOT].load("chariot");
	anim_controllers[rtso::UNIT_WAR_DOG].load("dog");
	anim_controllers[rtso::UNIT_CATAPULT].load("catapult");
	anim_controllers[rtso::UNIT_BOLT_SHOOTER].load("bolt");
	anim_controllers[rtso::UNIT_FEMALE_CITIZEN].load("female");
	anim_controllers[rtso::UNIT_TRIREME].load("trireme");
	anim_controllers[rtso::UNIT_FISHING_BOAT].load("fishing_boat");
	anim_controllers[rtso::UNIT_MERCHANT_SHIP].load("merchant_ship");
}

void RtsoWorldView::_ready() {
	// Selection Ring
	ring_mesh.instantiate();
	ring_mesh->set_inner_radius(0.45f);
	ring_mesh->set_outer_radius(0.58f);

	ring_sel_mat.instantiate();
	ring_sel_mat->set_albedo(Color(0.2f, 1.0f, 0.4f));
	ring_sel_mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	ring_sel_mat->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);

	ring_enemy_mat.instantiate();
	ring_enemy_mat->set_albedo(Color(0.92f, 0.25f, 0.2f));
	ring_enemy_mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);

	// Floating Health Bar Meshes and Materials
	hp_bg_mesh.instantiate();
	hp_bg_mesh->set_size(Vector3(0.9f, 0.08f, 0.04f));

	hp_fg_mesh.instantiate();
	hp_fg_mesh->set_size(Vector3(0.88f, 0.07f, 0.05f));

	hp_bg_mat.instantiate();
	hp_bg_mat->set_albedo(Color(0.15f, 0.15f, 0.18f, 0.85f));
	hp_bg_mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	hp_bg_mat->set_flag(BaseMaterial3D::FLAG_BILLBOARD_KEEP_SCALE, true);

	hp_green_mat.instantiate();
	hp_green_mat->set_albedo(Color(0.2f, 0.95f, 0.3f, 0.95f));
	hp_green_mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);

	hp_red_mat.instantiate();
	hp_red_mat->set_albedo(Color(0.95f, 0.25f, 0.2f, 0.95f));
	hp_red_mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);

	hp_blue_mat.instantiate();
	hp_blue_mat->set_albedo(Color(0.25f, 0.65f, 1.0f, 0.95f));
	hp_blue_mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);

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

	// Flying arrows — 0 A.D. arrow_front, else a thick cylinder that reads at RTS range
	{
		auto arrow_entry = rtso::ModelLibrary::get("units", "arrow_projectile");
		Ref<Mesh> arrow = arrow_entry.mesh;
		arrow_scale = arrow_entry.scale > 0.05f ? arrow_entry.scale : 1.0f;
		if (!arrow.is_valid()) {
			arrow_mesh.instantiate();
			arrow_mesh->set_top_radius(0.08f);
			arrow_mesh->set_bottom_radius(0.10f);
			arrow_mesh->set_height(1.10f);
			arrow = arrow_mesh;
			arrow_scale = 1.0f;
			arrow_mat.instantiate();
			arrow_mat->set_albedo(Color(0.85f, 0.75f, 0.5f));
		}
		arrow_mm = create_multimesh(arrow, false);
		arrow_mmi = memnew(MultiMeshInstance3D);
		arrow_mmi->set_multimesh(arrow_mm);
		if (arrow_mat.is_valid())
			arrow_mmi->set_material_override(arrow_mat);
		add_child(arrow_mmi);
	}

	{
		auto bolt_entry = rtso::ModelLibrary::get("units", "bolt_projectile");
		Ref<Mesh> bolt = bolt_entry.mesh;
		bolt_scale = bolt_entry.scale > 0.05f ? bolt_entry.scale : 1.0f;
		if (!bolt.is_valid()) {
			Ref<CylinderMesh> bolt_cyl;
			bolt_cyl.instantiate();
			bolt_cyl->set_top_radius(0.06f);
			bolt_cyl->set_bottom_radius(0.08f);
			bolt_cyl->set_height(1.55f);
			bolt = bolt_cyl;
			bolt_scale = 1.0f;
		}
		bolt_mm = create_multimesh(bolt, false);
		bolt_mmi = memnew(MultiMeshInstance3D);
		bolt_mmi->set_multimesh(bolt_mm);
		add_child(bolt_mmi);
	}

	{
		auto stone_entry = rtso::ModelLibrary::get("units", "stone_projectile");
		Ref<Mesh> stone = stone_entry.mesh;
		stone_proj_scale = stone_entry.scale > 0.05f ? stone_entry.scale : 1.0f;
		if (!stone.is_valid()) {
			Ref<SphereMesh> rock;
			rock.instantiate();
			rock->set_radius(0.28f);
			rock->set_height(0.56f);
			stone = rock;
			stone_proj_scale = 1.0f;
		}
		stone_proj_mm = create_multimesh(stone, false);
		stone_proj_mmi = memnew(MultiMeshInstance3D);
		stone_proj_mmi->set_multimesh(stone_proj_mm);
		add_child(stone_proj_mmi);
	}

	// Resource batches — real 0ad flora & mines with variety
	// Instance colours carry the LOS brightness (fogged supplies read darker).
	auto tinted = [](const Ref<Mesh> &m) {
		enable_instance_tint(m);
		return m;
	};
	tree_a_mm = create_multimesh(tinted(rtso::ModelLibrary::get("vegetation", "tree").mesh), true);
	tree_b_mm = create_multimesh(tinted(rtso::ModelLibrary::get("vegetation", "pine").mesh), true);
	tree_c_mm = create_multimesh(tinted(rtso::ModelLibrary::get("vegetation", "palm").mesh), true);
	gold_mm = create_multimesh(tinted(rtso::ModelLibrary::get("vegetation", "gold_vein").mesh), true);
	stone_mm = create_multimesh(tinted(rtso::ModelLibrary::get("vegetation", "stone_mine").mesh), true);
	food_mm = create_multimesh(tinted(rtso::ModelLibrary::get("vegetation", "fruit_bush").mesh), true);

	tree_a_mmi = memnew(MultiMeshInstance3D); tree_a_mmi->set_multimesh(tree_a_mm); add_child(tree_a_mmi);
	tree_b_mmi = memnew(MultiMeshInstance3D); tree_b_mmi->set_multimesh(tree_b_mm); add_child(tree_b_mmi);
	tree_c_mmi = memnew(MultiMeshInstance3D); tree_c_mmi->set_multimesh(tree_c_mm); add_child(tree_c_mmi);
	gold_mmi = memnew(MultiMeshInstance3D); gold_mmi->set_multimesh(gold_mm); add_child(gold_mmi);
	stone_mmi = memnew(MultiMeshInstance3D); stone_mmi->set_multimesh(stone_mm); add_child(stone_mmi);
	food_mmi = memnew(MultiMeshInstance3D); food_mmi->set_multimesh(food_mm); add_child(food_mmi);
	ruins_mm = create_multimesh(tinted(rtso::ModelLibrary::get("buildings", "ruins").mesh), true);
	treasure_mm = create_multimesh(tinted(rtso::ModelLibrary::get("vegetation", "treasure_chest").mesh), true);
	ruins_mmi = memnew(MultiMeshInstance3D); ruins_mmi->set_multimesh(ruins_mm); add_child(ruins_mmi);
	treasure_mmi = memnew(MultiMeshInstance3D); treasure_mmi->set_multimesh(treasure_mm); add_child(treasure_mmi);

	// Fauna — never fall back to trees/stones (those looked like walking mines)
	{
		auto deer = rtso::ModelLibrary::get("units", "deer");
		Ref<Mesh> deer_mesh = deer.mesh.is_valid() ? deer.mesh : rtso::ModelLibrary::get("units", "war_dog").mesh;
		// Each 0 A.D. species is drawn with its RTS0 model (gaia_data.h model_id), or the deer.
		Ref<Mesh> fauna_rest[rtso::FAUNA_COUNT];
		for (int f = 0; f < rtso::FAUNA_COUNT; ++f) {
			auto m = rtso::ModelLibrary::get("units", rtso::fauna_type_info((uint8_t)f).model_id);
			fauna_rest[f] = m.mesh.is_valid() ? m.mesh : deer_mesh;
		}
		for (int f = 0; f < rtso::FAUNA_COUNT; ++f) {
			for (int c = 0; c < CROWD_CLIP_COUNT; ++c) {
				fauna_mm[f][c] = create_multimesh(fauna_rest[f], false);
				fauna_mmi[f][c] = memnew(MultiMeshInstance3D);
				fauna_mmi[f][c]->set_multimesh(fauna_mm[f][c]);
				add_child(fauna_mmi[f][c]);
			}
		}
	}

	// Initialize 3D Positional Spatial Audio Pool
	for (size_t p = 0; p < SPATIAL_AUDIO_POOL_SIZE; ++p) {
		spatial_players[p] = memnew(AudioStreamPlayer3D);
		spatial_players[p]->set_max_distance(80.0f);
		spatial_players[p]->set_unit_size(8.0f);
		spatial_players[p]->set_volume_db(0.0f);
		add_child(spatial_players[p]);
	}

	// Pre-cache combat and destruction audio streams
	auto load_s = [](const char *path) -> Ref<AudioStream> {
		if (ResourceLoader::get_singleton()->exists(path)) {
			return ResourceLoader::get_singleton()->load(path);
		}
		return Ref<AudioStream>();
	};

	sfx_bow_shoot[0] = load_s("res://assets/sounds/combat/bow_shoot_1.ogg");
	sfx_bow_shoot[1] = load_s("res://assets/sounds/combat/bow_shoot_2.ogg");
	sfx_arrow_fly = load_s("res://assets/sounds/combat/arrow_fly.ogg");
	sfx_arrow_hit = load_s("res://assets/sounds/combat/arrow_hit.ogg");
	sfx_catapult_blast = load_s("res://assets/sounds/combat/catapult_blast.ogg");
	sfx_sword_hit[0] = load_s("res://assets/sounds/combat/sword_hit_1.ogg");
	sfx_sword_hit[1] = load_s("res://assets/sounds/combat/sword_hit_2.ogg");
	sfx_sword_hit[2] = load_s("res://assets/sounds/combat/sword_hit_3.ogg");
	sfx_spear_hit[0] = load_s("res://assets/sounds/combat/spear_hit_1.ogg");
	sfx_spear_hit[1] = load_s("res://assets/sounds/combat/spear_hit_2.ogg");
	sfx_building_collapse = load_s("res://assets/sounds/building/building_collapse.ogg");
	sfx_building_fire = load_s("res://assets/sounds/building/building_fire.ogg");

	// Fog overlay: 0 A.D. model_common.fs multiplies every model by the LOS
	// texture. Imported models keep their StandardMaterial3D, so a structure
	// outside full sight gets a multiply pass with its LOS brightness instead,
	// from a small set of materials (instance uniforms aren't in every renderer).
	{
		Ref<Shader> sh;
		sh.instantiate();
		sh->set_code(
				"shader_type spatial;\n"
				"render_mode blend_mul, unshaded, cull_back, depth_draw_never, shadows_disabled, fog_disabled;\n"
				"uniform float los_dim = 1.0;\n"
				"void fragment() { ALBEDO = vec3(los_dim); }\n");
		for (int k = 0; k < FOG_OVERLAY_LEVELS; ++k) {
			fog_overlay_mats_[k].instantiate();
			fog_overlay_mats_[k]->set_shader(sh);
			fog_overlay_mats_[k]->set_shader_parameter("los_dim", (float)k / (float)(FOG_OVERLAY_LEVELS - 1));
		}
	}

	init_animation_controllers();
}

void RtsoWorldView::bind_los(const Ref<Material> &mat) {
	Ref<ShaderMaterial> sm = mat;
	if (sm.is_null() || los_tex_.is_null()) return;
	sm->set_shader_parameter("los_tex", los_tex_);
	sm->set_shader_parameter("los_map_half", los_half_);
}

float RtsoWorldView::los_brightness(float x, float z) const {
	const int res = los_.res();
	const auto &shown = los_.shown();
	if (res <= 0 || los_half_ <= 0.0f || shown.size() != (size_t)res * (size_t)res) return 1.0f;
	const int cx = Math::clamp((int)((x + los_half_) / (los_half_ * 2.0f) * res), 0, res - 1);
	const int cz = Math::clamp((int)((z + los_half_) / (los_half_ * 2.0f) * res), 0, res - 1);
	return (float)shown[(size_t)cz * res + cx] / 255.0f;
}

static void set_overlay_recursive(Node *n, const Ref<Material> &mat) {
	if (!n) return;
	if (GeometryInstance3D *gi = Object::cast_to<GeometryInstance3D>(n)) gi->set_material_overlay(mat);
	for (int i = 0; i < n->get_child_count(); ++i) set_overlay_recursive(n->get_child(i), mat);
}

void RtsoWorldView::set_fog_overlay(Node *n, float brightness) {
	// Fully lit structures skip the extra pass.
	const int k = Math::clamp((int)Math::round(brightness * (FOG_OVERLAY_LEVELS - 1)), 0, FOG_OVERLAY_LEVELS - 1);
	set_overlay_recursive(n, k >= FOG_OVERLAY_LEVELS - 1 ? Ref<Material>() : Ref<Material>(fog_overlay_mats_[k]));
}

void RtsoWorldView::update_fog(const rtso::Simulation *sim, float delta) {
	mirages_.update(*sim, local_player_);
	const int res = sim->fow_res();
	const bool new_map = res != los_.res() || los_half_ != sim->map_half();
	los_.set_target(sim->fow_grid(local_player_), res);
	if (new_map) {
		los_.snap();
		los_half_ = sim->map_half();
		los_image_ = Image::create(res, res, false, Image::FORMAT_L8);
		los_tex_ = ImageTexture::create_from_image(los_image_);
		if (terrain) bind_los(terrain->get_material_override());
		if (water_mesh_node) bind_los(water_mesh_node->get_material_override());
		los_upload_timer_ = 1.0f;
	}
	// Ease at most 30 times a second; the texture upload is the costly part.
	los_upload_timer_ += delta;
	if (los_upload_timer_ < 1.0f / 30.0f) return;
	const float step = los_upload_timer_;
	los_upload_timer_ = 0.0f;
	if (!los_.interpolate(step) && !new_map) return;
	const auto &shown = los_.shown();
	if (los_bytes_.size() != (int64_t)shown.size()) los_bytes_.resize((int64_t)shown.size());
	memcpy(los_bytes_.ptrw(), shown.data(), shown.size());
	los_image_->set_data(res, res, false, Image::FORMAT_L8, los_bytes_);
	los_tex_->update(los_image_);
}

void RtsoWorldView::play_spatial_sound(const Ref<AudioStream> &stream, Vector3 pos, float volume_db, float max_dist, float pitch_scale) {
	if (!stream.is_valid()) return;
	AudioStreamPlayer3D *player = spatial_players[next_spatial_idx];
	next_spatial_idx = (next_spatial_idx + 1) % SPATIAL_AUDIO_POOL_SIZE;
	if (player) {
		player->set_position(pos);
		player->set_stream(stream);
		player->set_volume_db(volume_db);
		player->set_max_distance(max_dist);
		player->set_pitch_scale(pitch_scale);
		player->play();
	}
}

void RtsoWorldView::play_spatial_sound_path(const String &path, Vector3 pos, float volume_db, float max_dist, float pitch_scale) {
	String res_path = path;
	if (!res_path.begins_with("res://")) {
		if (res_path.begins_with("audio/")) {
			res_path = "res://assets/sounds/" + res_path.substr(6);
		} else if (res_path.begins_with("assets/sounds/")) {
			res_path = "res://" + res_path;
		} else {
			res_path = "res://" + res_path;
		}
	}
	if (ResourceLoader::get_singleton()->exists(res_path)) {
		Ref<AudioStream> s = ResourceLoader::get_singleton()->load(res_path);
		play_spatial_sound(s, pos, volume_db, max_dist, pitch_scale);
	}
}

static AnimationPlayer *find_anim_player(Node *n) {
	if (!n) return nullptr;
	if (AnimationPlayer *ap = Object::cast_to<AnimationPlayer>(n)) return ap;
	for (int i = 0; i < n->get_child_count(); ++i) {
		if (AnimationPlayer *found = find_anim_player(n->get_child(i))) return found;
	}
	return nullptr;
}

// 0 A.D. CCmpDecay.cpp SinkingAnim: exponential sink + randomized tilt.
// template_unit_ship.xml: SinkRate=0.5, SinkAccel=7, DelayTime=0.
static int wrap_mod(int v, int m) {
	int r = v % m;
	return r < 0 ? r + m : r;
}

// Clip names are resolved for every detailed unit every frame. Building a
// godot::String from a literal there allocates, and every has_animation() call
// would re-hash it into a StringName, so keep one interned name per clip and
// pass small ids around instead.
enum ClipId : uint8_t {
	CLIP_NONE = 0,
	CLIP_IDLE,
	CLIP_WALK,
	CLIP_RUN,
	CLIP_ATTACK,
	CLIP_DEATH,
	CLIP_GATHER,
	CLIP_GATHER_TREE,
	CLIP_GATHER_ORE,
	CLIP_GATHER_ROCK,
	CLIP_GATHER_FRUIT,
	CLIP_GATHER_GRAIN,
	CLIP_GATHER_MEAT,
	CLIP_GATHER_FISH,
	CLIP_BUILD,
	CLIP_CARRY,
	CLIP_HEAL,
	CLIP_SLAUGHTER,
	CLIP_COUNT
};

static const StringName &clip_name(ClipId id) {
	static const StringName names[CLIP_COUNT] = {
		StringName(), StringName("Idle"), StringName("Walk"), StringName("Run"),
		StringName("Attack"), StringName("Death"), StringName("Gather"),
		StringName("GatherTree"), StringName("GatherOre"), StringName("GatherRock"),
		StringName("GatherFruit"), StringName("GatherGrain"), StringName("GatherMeat"),
		StringName("GatherFish"),
		StringName("Build"), StringName("Carry"), StringName("Heal"),
		StringName("Slaughter")
	};
	return names[id < CLIP_COUNT ? id : CLIP_NONE];
}

// 0 A.D. ResourceGatherer.js SelectAnimation("gather_" + type).
static ClipId gather_clip_for(uint8_t utype, uint8_t res_type, uint8_t subtype) {
	if (utype == rtso::UNIT_HEALER) return CLIP_HEAL;
	if (utype == rtso::UNIT_FISHING_BOAT) return CLIP_GATHER_FISH;
	switch (res_type) {
		case rtso::RES_WOOD: return CLIP_GATHER_TREE;
		case rtso::RES_GOLD: return CLIP_GATHER_ORE;
		case rtso::RES_STONE: return CLIP_GATHER_ROCK;
		case rtso::RES_FOOD:
			if (subtype == rtso::RES_SUB_MEAT) return CLIP_GATHER_MEAT;
			if (subtype == rtso::RES_SUB_GRAIN) return CLIP_GATHER_GRAIN;
			return CLIP_GATHER_FRUIT;
		default:
			return CLIP_GATHER;
	}
}

// 0 A.D. action → clip (Attack.js, ResourceGatherer.js, CCmpVisualActor, UnitAI carry variant).
struct WantAnim {
	ClipId clip = CLIP_IDLE;
	bool loop = true;
	rtso::AnimState state = rtso::AnimState::Idle;
};

static WantAnim resolve_unit_anim(uint8_t uaction, float speed2, bool alive, int32_t carry,
		uint8_t utype, uint8_t res_type, uint8_t res_sub) {
	WantAnim w;
	// A laden unit keeps the carry variant through any movement clip.
	const bool laden_move = carry > 0 && (speed2 > 0.001f || uaction == rtso::ACTION_MOVE);
	// ~4.24 m/s: infantry walk (~3.2) stays Walk; cavalry/camel/chariot pick Run.
	const bool sprint = !laden_move && speed2 > 18.0f;
	WantAnim w_move;
	w_move.clip = laden_move ? CLIP_CARRY : (sprint ? CLIP_RUN : CLIP_WALK);
	w_move.state = laden_move ? rtso::AnimState::Carry
			: (sprint ? rtso::AnimState::Run : rtso::AnimState::Walk);

	if (!alive || uaction == rtso::ACTION_DYING) {
		w.clip = CLIP_DEATH;
		w.loop = false;
		w.state = rtso::AnimState::Death;
		return w;
	}
	switch (uaction) {
		// Gather/build/attack block movement clips (SelectMovementAnimation early-out).
		case rtso::ACTION_GATHER:
			w.clip = gather_clip_for(utype, res_type, res_sub);
			w.state = rtso::AnimState::Gather;
			return w;
		case rtso::ACTION_BUILD:
			w.clip = CLIP_BUILD;
			w.state = rtso::AnimState::Build;
			return w;
		case rtso::ACTION_ATTACK:
			// Ships: Attack.js keeps attack_ranged until StopAttacking (sail falls back to idle).
			if (speed2 <= 8.0f || rtso::unit_is_ship(utype)) {
				const bool hunt = (utype == rtso::UNIT_VILLAGER || utype == rtso::UNIT_FEMALE_CITIZEN);
				w.clip = hunt ? CLIP_SLAUGHTER : CLIP_ATTACK;
				w.state = rtso::AnimState::Attack;
				return w;
			}
			return w_move;
		// UnitAI.SetDefaultAnimationVariant → carry_food / carry_*; fishing hull uses rowing_move.
		case rtso::ACTION_RETURN_DROPSITE:
			w.clip = CLIP_CARRY;
			w.state = rtso::AnimState::Carry;
			return w;
		case rtso::ACTION_MOVE:
		case rtso::ACTION_ATTACK_MOVE:
		case rtso::ACTION_PATROL:
			return w_move;
		default:
			break;
	}
	if (speed2 > 0.001f) return w_move;
	w.clip = CLIP_IDLE;
	w.state = rtso::AnimState::Idle;
	return w;
}

// Nearest clip a model actually ships, mirroring the extractor's own borrowing
// (Run from Walk, Carry from Walk, Death from Idle for hulls that only float).
static ClipId resolve_clip_fallback(AnimationPlayer *anim, ClipId want) {
	if (!anim || want == CLIP_NONE || anim->has_animation(clip_name(want))) return want;
	ClipId alt = CLIP_NONE;
	switch (want) {
		case CLIP_RUN:       alt = CLIP_WALK; break;
		case CLIP_WALK:      alt = CLIP_RUN; break;
		case CLIP_CARRY:     alt = CLIP_WALK; break;
		case CLIP_GATHER_TREE:
		case CLIP_GATHER_ORE:
		case CLIP_GATHER_ROCK:
		case CLIP_GATHER_FRUIT:
		case CLIP_GATHER_GRAIN:
		case CLIP_GATHER_MEAT:
		case CLIP_GATHER_FISH:
			if (anim->has_animation(clip_name(CLIP_GATHER))) return CLIP_GATHER;
			alt = CLIP_IDLE;
			break;
		case CLIP_GATHER:
			if (anim->has_animation(clip_name(CLIP_GATHER_TREE))) return CLIP_GATHER_TREE;
			break;
		case CLIP_BUILD:     alt = CLIP_GATHER; break;
		case CLIP_HEAL:      alt = CLIP_GATHER; break;
		case CLIP_SLAUGHTER: alt = CLIP_ATTACK; break;
		default:             break;
	}
	if (alt != CLIP_NONE && anim->has_animation(clip_name(alt))) return alt;
	// Death must not loop an idle forever; the caller clears loop for it.
	return anim->has_animation(clip_name(CLIP_IDLE)) ? CLIP_IDLE : CLIP_NONE;
}

// 0 A.D. rigs a sail skeleton on only some hulls (Athenian/Persian/Roman); the
// Briton, Gaulish, Iberian and Mauryan warships ship as unskinned meshes, so
// their swell has to come from the transform. Applied to every hull so the
// fleet reads the same.
static void apply_ship_wave_xform(Transform3D &xf, uint32_t id, float t, float speed2) {
	const float phase = (float)(id % 64) * 0.09817f; // 2*pi/64
	const float w = t * 1.15f + phase;
	const float moving = Math::clamp(speed2 * 4.0f, 0.0f, 1.0f);
	const float amp = 0.75f + 0.75f * moving;
	const float roll = Math::sin(w) * 0.032f * amp;
	const float pitch = Math::sin(w * 1.37f + 1.1f) * 0.021f * amp;
	xf.basis = xf.basis * Basis(Vector3(0, 0, 1), roll) * Basis(Vector3(1, 0, 0), pitch);
	xf.origin.y += Math::sin(w * 0.83f + 0.5f) * 0.055f * amp;
}

// Trireme oars are rigid child nodes (oar_*), not sail-skeleton bones. Stroke
// them in local space while the hull is moving; Idle leaves them at rest.
static void apply_ship_oar_stroke(Node *model, uint32_t id, float t, float speed2) {
	if (!model) return;
	const float moving = Math::clamp(speed2 * 4.0f, 0.0f, 1.0f);
	std::vector<Node *> stack;
	stack.push_back(model);
	int k = 0;
	while (!stack.empty()) {
		Node *n = stack.back();
		stack.pop_back();
		for (int i = 0; i < n->get_child_count(); ++i) {
			stack.push_back(n->get_child(i));
		}
		Node3D *n3 = Object::cast_to<Node3D>(n);
		if (!n3) continue;
		if (!String(n3->get_name()).begins_with("oar_")) continue;
		if (!n3->has_meta("oar_rest")) {
			n3->set_meta("oar_rest", n3->get_transform());
		}
		Transform3D rest = n3->get_meta("oar_rest");
		const float phase = (float)(id % 17) * 0.37f + (float)k * 0.21f;
		++k;
		const float ang = Math::sin(t * 4.2f + phase) * 0.35f * moving;
		rest.basis = rest.basis * Basis(Vector3(1.0f, 0.0f, 0.0f), ang);
		n3->set_transform(rest);
	}
}

static void apply_unit_death_xform(Transform3D &xf, uint8_t utype, float death_t, float px, float pz, float hull_h, bool has_death_clip) {
	if (death_t <= 0.0f) return;
	if (rtso::unit_is_ship(utype)) {
		// CCmpDecay SinkingAnim (SinkRate=0.5, SinkAccel=7) plus a linear floor so
		// the wreck lists immediately — the raw exponential is nearly flat for 1s.
		float t = death_t;
		float ang_x = (float)(wrap_mod((int)Math::round(px), 30) - 15) / 15.0f;
		float ang_z = (float)wrap_mod((int)Math::round(pz), 30) / 40.0f;
		float u = Math::clamp(t / 2.2f, 0.0f, 1.0f);
		float rx = ang_x * u * 0.55f;
		float rz = (ang_z + 0.20f) * u * 0.42f;
		float expo = 0.5f * (Math::exp(t - 1.0f) - 0.54881163609f)
				+ 7.0f * (Math::exp(t - 4.0f) - 0.01831563888f);
		if (expo < 0.0f) expo = 0.0f;
		float depth = Math::max(expo, t * 1.8f);
		xf.basis = Basis(Vector3(1, 0, 0), rx) * Basis(Vector3(0, 0, 1), rz) * xf.basis;
		xf.origin.y -= depth;
		return;
	}
	if (has_death_clip) return;
	if (rtso::unit_is_siege_engine(utype)) {
		float u = Math::clamp(death_t / 1.20f, 0.0f, 1.0f);
		float dy = hull_h > 0.4f ? hull_h : 2.0f;
		xf.basis = Basis(Vector3(1, 0, 0), u * 0.22f) * Basis(Vector3(0, 1, 0), u * 0.40f) * xf.basis;
		xf.origin.y -= u * dy * 0.70f;
	}
}

static void disable_mesh_shadows(Node *n) {
	if (!n) return;
	if (GeometryInstance3D *gi = Object::cast_to<GeometryInstance3D>(n)) {
		gi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	}
	for (int i = 0; i < n->get_child_count(); ++i) {
		disable_mesh_shadows(n->get_child(i));
	}
}

static void apply_unit_tools(Node *root, const StringName &clip) {
	rtso::ModelLibrary::apply_clip_tools(root, clip);
}

void RtsoWorldView::attach_unit_model(UnitVis &v, const String &model_name) {
	if (v.model_id == model_name && v.model) return;
	if (v.anim) {
		v.anim->stop();
		v.anim = nullptr;
	}
	if (v.model) {
		v.root->remove_child(v.model);
		v.model->queue_free();
		v.model = nullptr;
	}
	v.model_id = model_name;
	v.clip = StringName();
	String cat_units = "units";
	auto entry = rtso::ModelLibrary::get(cat_units, model_name);
	if (entry.scene.is_valid()) {
		v.model = entry.scene->instantiate();
		rtso::ModelLibrary::apply_imported_material_fixups(v.model);
		v.root->add_child(v.model);
		disable_mesh_shadows(v.model);
		v.anim = find_anim_player(v.model);
		if (v.anim) {
			v.anim->set_active(true);
			v.anim->set_callback_mode_process(AnimationMixer::ANIMATION_CALLBACK_MODE_PROCESS_MANUAL);
			v.anim->set_speed_scale(1.0);
			v.anim->advance(0.0);
		}
		apply_unit_tools(v.model, StringName("Idle"));
		return;
	}
	MeshInstance3D *mi = memnew(MeshInstance3D);
	if (entry.valid) {
		mi->set_mesh(entry.mesh);
		mi->set_scale(Vector3(entry.scale, entry.scale, entry.scale));
	}
	v.model = mi;
	v.root->add_child(mi);
	disable_mesh_shadows(mi);
	v.anim = nullptr;
}

Node *RtsoWorldView::instantiate_building_id(const String &id) {
	String cat = "buildings";
	const rtso::ModelLibrary::Entry &e = rtso::ModelLibrary::get(cat, id);
	if (e.scene.is_valid()) {
		Node *n = e.scene->instantiate();
		rtso::ModelLibrary::apply_imported_material_fixups(n);
		return n;
	}
	if (e.mesh.is_valid()) {
		MeshInstance3D *mi = memnew(MeshInstance3D);
		mi->set_mesh(e.mesh);
		return mi;
	}
	return nullptr;
}

void RtsoWorldView::set_building_site_visible(BuildingVis &v, bool foundation_on, bool scaffold_on, uint8_t btype) {
	auto drop = [&](Node *&n) {
		if (!n) return;
		if (v.root && n->get_parent() == v.root)
			v.root->remove_child(n);
		n->queue_free();
		n = nullptr;
	};
	auto place = [&](Node *&slot, const char *id, bool show) {
		if (!show) {
			if (Node3D *n3 = Object::cast_to<Node3D>(slot))
				n3->set_visible(false);
			return;
		}
		if (!slot) {
			slot = instantiate_building_id(id);
			if (!slot) return;
			v.root->add_child(slot);
			v.fog_dim = -1.0f;
			String cat = "buildings";
			const auto &se = rtso::ModelLibrary::get(cat, String(id));
			if (Node3D *n3 = Object::cast_to<Node3D>(slot)) {
				float s = se.scale > 0.05f ? se.scale : 1.0f;
				n3->set_scale(Vector3(s, s, s));
				n3->set_position(Vector3(0, se.y_offset, 0));
			}
		}
		if (Node3D *n3 = Object::cast_to<Node3D>(slot))
			n3->set_visible(true);
	};

	if (v.overlay_type != (int)btype) {
		drop(v.foundation);
		drop(v.scaffold);
		v.overlay_type = (int)btype;
	}
	place(v.foundation, rtso::civ_building_foundation(btype), foundation_on);
	place(v.scaffold, rtso::civ_building_scaffold(btype), scaffold_on);
}

void RtsoWorldView::attach_building_model(BuildingVis &v, const String &model_name) {
	if (v.model_id == model_name && v.model) return;
	if (v.model) {
		v.root->remove_child(v.model);
		v.model->queue_free();
		v.model = nullptr;
	}
	v.model_id = model_name;
	v.fog_dim = -1.0f; // new nodes: re-apply the fog overlay
	v.model = instantiate_building_id(model_name);
	if (!v.model) {
		MeshInstance3D *mi = memnew(MeshInstance3D);
		v.model = mi;
	}
	v.root->add_child(v.model);
}

void RtsoWorldView::hide_unit_vis(UnitVis &v) {
	if (v.root) v.root->set_visible(false);
	if (v.ring) v.ring->set_visible(false);
	if (v.hp_bg) v.hp_bg->set_visible(false);
	if (v.hp_fg) v.hp_fg->set_visible(false);
	if (v.anim && UtilityFunctions::is_instance_valid(v.anim))
		v.anim->set_active(false);
}

void RtsoWorldView::ensure_unit_batches(const rtso::Simulation *sim) {
	uint8_t civs[CROWD_BANK_COUNT] = {rtso::CIV_ATHENIANS, rtso::CIV_SPARTANS};
	if (sim) {
		civs[0] = sim->player_civ(local_player_);
		civs[1] = sim->team_civ(local_player_ == 0 ? 1 : 0);
	}
	for (int bank = 0; bank < CROWD_BANK_COUNT; ++bank) {
		for (int t = 0; t < rtso::UNIT_TYPE_COUNT; ++t) {
			String id_str = rtso::civ_unit_model(civs[bank], (uint8_t)t);
			String cat_units2 = "units";
			auto entry = rtso::ModelLibrary::get(cat_units2, id_str);
			Ref<Mesh> rest = entry.crowd_mesh.is_valid() ? entry.crowd_mesh : entry.mesh;

			for (int c = 0; c < CROWD_CLIP_COUNT; ++c) {
				if (unit_mmi[bank][t][c]) continue;
				unit_mm[bank][t][c] = create_multimesh(rest, true);
				unit_mmi[bank][t][c] = memnew(MultiMeshInstance3D);
				unit_mmi[bank][t][c]->set_multimesh(unit_mm[bank][t][c]);
				unit_mmi[bank][t][c]->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
				add_child(unit_mmi[bank][t][c]);
			}
		}
	}
}

void RtsoWorldView::bake_puppet_clips(CrowdPuppet *row, AnimationPlayer *ap,
		rtso::ModelLibrary::CrowdSkin &skin, bool skinned) {
	static const ClipId kWant[CROWD_CLIP_COUNT] = {
		CLIP_IDLE, CLIP_WALK, CLIP_ATTACK, CLIP_DEATH
	};
	bool fauna_row = false;
	for (int fi = 0; fi < rtso::FAUNA_COUNT; ++fi) {
		if (row == fauna_puppet[fi]) {
			fauna_row = true;
			break;
		}
	}
	for (int c = 0; c < CROWD_CLIP_COUNT; ++c) {
		ClipId want = kWant[c];
		if (c == 1 && fauna_row && ap && ap->has_animation(clip_name(CLIP_RUN))) {
			want = CLIP_RUN;
		}
		ClipId play = resolve_clip_fallback(ap, want);
		int valid = 0;
		if (skinned && ap && play != CLIP_NONE) {
			const StringName &name = clip_name(play);
			double len = 0.0;
			Ref<Animation> a = ap->get_animation(name);
			if (a.is_valid()) len = a->get_length();
			ap->play(name);
			const int n = CROWD_FRAMES_PER_CLIP;
			for (int f = 0; f < n; ++f) {
				double t = 0.0;
				if (len > 0.0) {
					if (c == 3) {
						t = (n > 1) ? len * (double)f / (double)(n - 1) : 0.0;
					} else {
						t = len * (double)f / (double)n;
					}
				}
				ap->seek(t, true);
				ap->advance(0.0);
				Ref<ArrayMesh> baked = rtso::ModelLibrary::bake_crowd_skin(skin);
				if (baked.is_valid()) {
					row[c].posed_frames[valid] = baked;
					valid++;
				}
			}
		}
		row[c].baked_count = valid;
		row[c].last_frame = -1;
	}
	if (ap) ap->stop();
}

void RtsoWorldView::cycle_puppet_meshes(CrowdPuppet *row, Ref<MultiMesh> *meshes, const int *counts) {
	for (int c = 0; c < CROWD_CLIP_COUNT; ++c) {
		if (!meshes[c].is_valid() || row[c].baked_count <= 0 || counts[c] <= 0) continue;
		int frame;
		if (c == 3) {
			frame = (int)(anim_time * 5.0f);
			if (frame > row[c].baked_count - 1) frame = row[c].baked_count - 1;
		} else {
			frame = wrap_mod((int)(anim_time * 8.0f), row[c].baked_count);
		}
		if (frame != row[c].last_frame && row[c].posed_frames[frame].is_valid()) {
			meshes[c]->set_mesh(row[c].posed_frames[frame]);
			row[c].last_frame = frame;
		}
	}
}

void RtsoWorldView::ensure_crowd_frames(int bank, uint8_t utype, uint8_t civ) {
	if (bank < 0 || bank >= CROWD_BANK_COUNT) return;
	if (utype >= rtso::UNIT_TYPE_COUNT) return;
	if (unit_puppet_baked[bank][utype]) return;
	unit_puppet_baked[bank][utype] = true;

	String cat_units = "units";
	String id_str = rtso::civ_unit_model(civ, utype);
	auto entry = rtso::ModelLibrary::get(cat_units, id_str);
	if (!entry.scene.is_valid()) return;

	if (!crowd_bake_root) {
		crowd_bake_root = memnew(Node3D);
		crowd_bake_root->set_visible(false);
		add_child(crowd_bake_root);
	}
	Node *inst = entry.scene->instantiate();
	if (!inst) return;
	crowd_bake_root->add_child(inst);
	if (Node3D *n3 = Object::cast_to<Node3D>(inst)) {
		n3->set_position(Vector3(0, -1000, 0));
	}
	rtso::ModelLibrary::apply_imported_material_fixups(inst);
	apply_unit_tools(inst, StringName("Idle"));

	AnimationPlayer *ap = find_anim_player(inst);
	if (ap) {
		ap->set_active(true);
		ap->set_callback_mode_process(AnimationMixer::ANIMATION_CALLBACK_MODE_PROCESS_MANUAL);
	}
	rtso::ModelLibrary::CrowdSkin skin;
	bool captured = rtso::ModelLibrary::capture_crowd_skin(inst, skin);
	bake_puppet_clips(unit_puppet[bank][utype], ap, skin, captured);
	for (int c = 0; c < CROWD_CLIP_COUNT; ++c) {
		if (unit_mm[bank][utype][c].is_valid() && unit_puppet[bank][utype][c].posed_frames[0].is_valid()) {
			unit_mm[bank][utype][c]->set_mesh(unit_puppet[bank][utype][c].posed_frames[0]);
		}
		unit_puppet[bank][utype][c].skin = rtso::ModelLibrary::CrowdSkin();
		unit_puppet[bank][utype][c].root = nullptr;
		unit_puppet[bank][utype][c].anim = nullptr;
	}
	crowd_bake_root->remove_child(inst);
	inst->queue_free();
}

void RtsoWorldView::ensure_fauna_frames(uint8_t ftype) {
	if (ftype >= rtso::FAUNA_COUNT) return;
	if (fauna_puppet_baked[ftype]) return;
	fauna_puppet_baked[ftype] = true;

	String cat_units = "units";
	String id_str = rtso::fauna_type_info(ftype).model_id;
	auto entry = rtso::ModelLibrary::get(cat_units, id_str);
	if (!entry.scene.is_valid()) return;

	if (!crowd_bake_root) {
		crowd_bake_root = memnew(Node3D);
		crowd_bake_root->set_visible(false);
		add_child(crowd_bake_root);
	}
	Node *inst = entry.scene->instantiate();
	if (!inst) return;
	crowd_bake_root->add_child(inst);
	if (Node3D *n3 = Object::cast_to<Node3D>(inst)) {
		n3->set_position(Vector3(0, -1000, 0));
	}
	rtso::ModelLibrary::apply_imported_material_fixups(inst);
	apply_unit_tools(inst, StringName("Idle"));

	AnimationPlayer *ap = find_anim_player(inst);
	if (ap) {
		ap->set_active(true);
		ap->set_callback_mode_process(AnimationMixer::ANIMATION_CALLBACK_MODE_PROCESS_MANUAL);
	}
	rtso::ModelLibrary::CrowdSkin skin;
	bool captured = rtso::ModelLibrary::capture_crowd_skin(inst, skin);
	bake_puppet_clips(fauna_puppet[ftype], ap, skin, captured);
	for (int c = 0; c < CROWD_CLIP_COUNT; ++c) {
		if (fauna_mm[ftype][c].is_valid() && fauna_puppet[ftype][c].posed_frames[0].is_valid()) {
			fauna_mm[ftype][c]->set_mesh(fauna_puppet[ftype][c].posed_frames[0]);
		}
		fauna_puppet[ftype][c].skin = rtso::ModelLibrary::CrowdSkin();
		fauna_puppet[ftype][c].root = nullptr;
		fauna_puppet[ftype][c].anim = nullptr;
	}
	crowd_bake_root->remove_child(inst);
	inst->queue_free();
}

void RtsoWorldView::play_unit_clip(UnitVis &v, const StringName &clip, bool loop) {
	if (!v.anim || !UtilityFunctions::is_instance_valid(v.anim) || clip == StringName()) return;
	if (v.clip == clip) return;
	if (!v.anim->has_animation(clip)) return;
	v.clip = clip;
	Ref<Animation> a = v.anim->get_animation(clip);
	if (a.is_valid()) {
		a->set_loop_mode(loop ? Animation::LOOP_LINEAR : Animation::LOOP_NONE);
	}
	v.anim->play(clip);
	v.anim->advance(0.0);
	apply_unit_tools(v.model, clip);
}

void RtsoWorldView::rebuild_terrain(const rtso::Simulation *sim) {
	if (!sim || !terrain) return;
	float half = sim->map_half();
	const auto &heights = sim->terrain_heights();
	int res = sim->terrain_res();

	if (heights.empty() || res <= 1) {
		plane_mesh->set_size(Vector2(half * 2.0f, half * 2.0f));
		terrain->set_mesh(plane_mesh);
	} else {
		terrain_array_mesh.instantiate();
		PackedVector3Array vertices;
		PackedVector3Array normals;
		PackedVector2Array uvs;
		PackedInt32Array indices;

		float step = (half * 2.0f) / (float)(res - 1);
		// Texture repeats 8 times across the default 128m map; keep that
		// density on larger maps instead of stretching it.
		const float uv_tiles = 8.0f * half / 64.0f;
		for (int z = 0; z < res; ++z) {
			float wz = -half + z * step;
			for (int x = 0; x < res; ++x) {
				float wx = -half + x * step;
				float wy = heights[z * res + x];
				vertices.push_back(Vector3(wx, wy, wz));
				uvs.push_back(Vector2((float)x / (float)(res - 1) * uv_tiles, (float)z / (float)(res - 1) * uv_tiles));

				float h_left  = (x > 0) ? heights[z * res + (x - 1)] : wy;
				float h_right = (x < res - 1) ? heights[z * res + (x + 1)] : wy;
				float h_down  = (z > 0) ? heights[(z - 1) * res + x] : wy;
				float h_up    = (z < res - 1) ? heights[(z + 1) * res + x] : wy;
				Vector3 n(-(h_right - h_left) / (2.0f * step), 1.0f, -(h_up - h_down) / (2.0f * step));
				normals.push_back(n.normalized());
			}
		}

		for (int z = 0; z < res - 1; ++z) {
			for (int x = 0; x < res - 1; ++x) {
				int i00 = z * res + x;
				int i10 = z * res + (x + 1);
				int i01 = (z + 1) * res + x;
				int i11 = (z + 1) * res + (x + 1);

				indices.push_back(i00);
				indices.push_back(i01);
				indices.push_back(i10);

				indices.push_back(i10);
				indices.push_back(i01);
				indices.push_back(i11);
			}
		}

		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = vertices;
		arrays[Mesh::ARRAY_NORMAL] = normals;
		arrays[Mesh::ARRAY_TEX_UV] = uvs;
		arrays[Mesh::ARRAY_INDEX] = indices;

		terrain_array_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
		terrain->set_mesh(terrain_array_mesh);
	}

	// Ground textures follow the map's biome.
	rtso::Assets::apply_terrain_biome(terrain->get_material_override(),
	                                  String(sim->current_map_biome().c_str()));

	// Water Body (water_height < -0.5 means a dry map, as in NavGrid::set_water_from_heights)
	if (sim->water_height() > -0.5f) {
		if (!water_mesh_node) {
			water_mesh.instantiate();
			water_mesh->set_subdivide_width(48);
			water_mesh->set_subdivide_depth(48);
			Ref<ShaderMaterial> water_shader_mat = rtso::Assets::load_water_material();
			water_mesh_node = memnew(MeshInstance3D);
			water_mesh_node->set_mesh(water_mesh);
			if (water_shader_mat.is_valid()) {
				water_mesh_node->set_material_override(water_shader_mat);
			} else {
				water_mat.instantiate();
				water_mat->set_albedo(Color(0.08f, 0.35f, 0.55f, 0.82f));
				water_mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
				water_mat->set_roughness(0.15f);
				water_mat->set_metallic(0.2f);
				water_mesh_node->set_material_override(water_mat);
			}
			add_child(water_mesh_node);
			bind_los(water_mesh_node->get_material_override());
		}
		water_mesh->set_size(Vector2(half * 2.2f, half * 2.2f));
		water_mesh_node->set_position(Vector3(0.0f, sim->water_height(), 0.0f));
		water_mesh_node->set_visible(true);
	} else if (water_mesh_node) {
		water_mesh_node->set_visible(false);
	}
}

void RtsoWorldView::sync(const rtso::Simulation *sim, float delta, const PackedInt32Array &selected_ids) {
	if (!sim) return;
	if (delta < 0.0f) delta = 0.0f;
	if (delta > 0.05f) delta = 0.05f;
	anim_time += delta;
	unit_vis_stamp++;
	if (unit_vis_stamp == 0) unit_vis_stamp = 1;

	update_fog(sim, delta);

	// Update skeletal animation controllers
	for (int t = 0; t < rtso::UNIT_TYPE_COUNT; ++t) {
		anim_controllers[t].update(delta * 1000.0f);
	}

	std::unordered_set<int32_t> sel_set;
	if (selected_ids.size() > 0) sel_set.reserve((size_t)selected_ids.size());
	for (int i = 0; i < selected_ids.size(); ++i) sel_set.insert(selected_ids[i]);

	Vector3 cam_pos;
	if (Viewport *vp = get_viewport()) {
		if (Camera3D *cam = vp->get_camera_3d()) {
			cam_pos = cam->get_global_position();
		}
	}
	const float anim_dist_sq = 24.0f * 24.0f;
	(void)anim_dist_sq;

	// 0 A.D. CCmpVisualActor::SelectMovementAnimation only swaps idle/walk/run.
	// Attack.js SelectAnimation("attack_*") and ResourceGatherer "gather_*" stay put.
	auto crowd_clip_of = [](uint8_t uaction, float speed2, bool alive) -> int {
		if (!alive || uaction == rtso::ACTION_DYING) return 3;
		switch (uaction) {
			case rtso::ACTION_GATHER:
			case rtso::ACTION_BUILD:
				return 0;
			case rtso::ACTION_ATTACK:
				return 2;
			case rtso::ACTION_MOVE:
			case rtso::ACTION_RETURN_DROPSITE:
			case rtso::ACTION_ATTACK_MOVE:
			case rtso::ACTION_PATROL:
				return 1;
			default:
				return speed2 > 0.001f ? 1 : 0;
		}
	};

	// =========================================================================
	// 1. Units Presentation: Real 0 A.D. models, 3D animations, and Health Bars
	// =========================================================================
	ensure_unit_batches(sim);
	auto uview = sim->units();
	auto rview_g = sim->resources();
	auto entity_visible = [sim, this](float x, float z, uint8_t team) {
		return sim->is_entity_visible_to_viewer(x, z, team, local_player_);
	};
	std::unordered_map<uint32_t, uint8_t> res_sub;
	res_sub.reserve(rview_g.count);
	for (size_t r = 0; r < rview_g.count; ++r) {
		if (rview_g.subtype && rview_g.subtype[r])
			res_sub[rview_g.id[r]] = rview_g.subtype[r];
	}

	std::unordered_set<uint32_t> detailed;
	// Ships always need the real GLB (sail clips + oar nodes). Crowd is rest-pose.
	for (size_t i = 0; i < uview.count; ++i) {
		if (rtso::unit_is_ship(uview.unit_type[i]))
			detailed.insert(uview.id[i]);
	}
	for (size_t i = 0; i < uview.count && (int)detailed.size() < MAX_DETAILED_UNITS; ++i) {
		bool dying = !uview.alive[i] || uview.action_state[i] == rtso::ACTION_DYING;
		if (dying)
			detailed.insert(uview.id[i]);
	}
	for (size_t i = 0; i < uview.count && (int)detailed.size() < MAX_DETAILED_UNITS; ++i) {
		if (uview.action_state[i] == rtso::ACTION_GATHER)
			detailed.insert(uview.id[i]);
	}
	for (int s = 0; s < selected_ids.size() && (int)detailed.size() < MAX_DETAILED_UNITS; ++s) {
		detailed.insert((uint32_t)selected_ids[s]);
	}

	int counts[CROWD_BANK_COUNT][rtso::UNIT_TYPE_COUNT][CROWD_CLIP_COUNT] = {};
	for (size_t i = 0; i < uview.count; ++i) {
		if (!entity_visible(uview.pos_x[i], uview.pos_z[i], uview.team[i])) continue;
		uint8_t t = uview.unit_type[i];
		if (t >= rtso::UNIT_TYPE_COUNT) t = rtso::UNIT_VILLAGER;
		int bank = (uview.team[i] == local_player_) ? 0 : 1;
		float vx = uview.vel_x[i];
		float vz = uview.vel_z[i];
		int clip = crowd_clip_of(uview.action_state[i], vx * vx + vz * vz, uview.alive[i] != 0);
		counts[bank][t][clip]++;
	}
	uint8_t civs[CROWD_BANK_COUNT] = { sim->player_civ(local_player_), sim->team_civ(local_player_ == 0 ? 1 : 0) };
	for (int bank = 0; bank < CROWD_BANK_COUNT; ++bank) {
		for (int t = 0; t < rtso::UNIT_TYPE_COUNT; ++t) {
			int any = 0;
			for (int c = 0; c < CROWD_CLIP_COUNT; ++c) any += counts[bank][t][c];
			if (any > 0) ensure_crowd_frames(bank, (uint8_t)t, civs[bank]);
			cycle_puppet_meshes(unit_puppet[bank][t], unit_mm[bank][t], counts[bank][t]);
		}
	}
	int written[CROWD_BANK_COUNT][rtso::UNIT_TYPE_COUNT][CROWD_CLIP_COUNT] = {};
	for (int bank = 0; bank < CROWD_BANK_COUNT; ++bank) {
		for (int t = 0; t < rtso::UNIT_TYPE_COUNT; ++t) {
			for (int c = 0; c < CROWD_CLIP_COUNT; ++c) {
				if (unit_mm[bank][t][c].is_valid()) {
					unit_mm[bank][t][c]->set_instance_count(counts[bank][t][c]);
				}
			}
		}
	}

	const Color col_player(0.55f, 0.72f, 1.0f);
	const Color col_enemy(1.0f, 0.52f, 0.42f);

	std::unordered_set<uint32_t> active_unit_ids;

	for (size_t i = 0; i < uview.count; ++i) {
		if (!entity_visible(uview.pos_x[i], uview.pos_z[i], uview.team[i])) continue;
		uint32_t id = uview.id[i];
		active_unit_ids.insert(id);

		bool is_player = (uview.team[i] == local_player_);
		uint8_t utype = uview.unit_type[i];
		if (utype >= rtso::UNIT_TYPE_COUNT) utype = rtso::UNIT_VILLAGER;
		uint8_t uaction = uview.action_state[i];
		float hp = uview.hp[i];
		float max_hp = uview.max_hp[i];
		float tx = uview.pos_x[i];
		float tz = uview.pos_z[i];
		float vx = uview.vel_x[i];
		float vz = uview.vel_z[i];
		float speed2 = vx * vx + vz * vz;

		// Smooth visual position interpolation to prevent snapping
		float x = tx;
		float z = tz;
		auto pos_it = crowd_pos.find(id);
		if (pos_it == crowd_pos.end()) {
			crowd_pos[id] = Vector2(tx, tz);
		} else {
			Vector2 cur = pos_it->second;
			Vector2 target(tx, tz);
			float dist_sq = cur.distance_squared_to(target);
			if (dist_sq > 25.0f) {
				cur = target;
			} else {
				float factor = Math::clamp(delta * 10.0f, 0.0f, 1.0f);
				cur = cur.lerp(target, factor);
			}
			crowd_pos[id] = cur;
			x = cur.x;
			z = cur.y;
		}

		// Smooth visual rotation interpolation to prevent snapping
		float yaw = 0.0f;
		auto yaw_it = crowd_yaw.find(id);
		if (yaw_it != crowd_yaw.end()) yaw = yaw_it->second;

		float target_yaw = yaw;
		if (speed2 > 0.001f) {
			// In Godot, forward is -Z. Local -Z rotated by atan2(-vx, -vz) points along (vx, vz)
			target_yaw = Math::atan2(-vx, -vz);
		} else if (uaction == rtso::ACTION_ATTACK || uaction == rtso::ACTION_GATHER || uaction == rtso::ACTION_BUILD) {
			// Face active combat or harvest target
			float tdx = uview.target_x[i] - x;
			float tdz = uview.target_z[i] - z;
			if (tdx * tdx + tdz * tdz > 0.01f) {
				target_yaw = Math::atan2(-tdx, -tdz);
			}
		}

		float diff = Math::wrapf(target_yaw - yaw, -(float)Math_PI, (float)Math_PI);
		yaw += diff * Math::clamp(delta * 14.0f, 0.0f, 1.0f);
		crowd_yaw[id] = yaw;

		WantAnim want = resolve_unit_anim(uaction, speed2, uview.alive[i] != 0, uview.carry[i], utype,
				uview.res_type_carried[i],
				(uview.target_resource_id && uview.target_resource_id[i])
					? (res_sub.count(uview.target_resource_id[i]) ? res_sub[uview.target_resource_id[i]] : 0)
					: 0);
		anim_controllers[utype].play(want.state);

		int bank = is_player ? 0 : 1;
		uint8_t uciv = sim->team_civ(uview.team[i]);
		String utype_str = rtso::civ_unit_model(uciv, utype);
		String cat_units3 = "units";
		auto entry = rtso::ModelLibrary::get(cat_units3, utype_str);
		float y = sim->get_terrain_height(x, z) + entry.y_offset;
		float sc = entry.scale > 0.05f ? entry.scale : 1.0f;
		Transform3D xf(Basis(Vector3(0, 1, 0), yaw).scaled(Vector3(sc, sc, sc)), Vector3(x, y, z));
		const bool dying = !uview.alive[i] || uaction == rtso::ACTION_DYING;
		const float death_t = dying ? uview.death_timer[i] : 0.0f;
		const bool ship_wave = !dying && rtso::unit_is_ship(utype);
		if (ship_wave) {
			apply_ship_wave_xform(xf, id, anim_time, speed2);
		}
		if (dying) {
			apply_unit_death_xform(xf, utype, death_t, x, z, entry.aabb.size.y * sc, false);
		}
		int clip = crowd_clip_of(uaction, speed2, uview.alive[i] != 0);
		int slot = written[bank][utype][clip]++;
		if (unit_mm[bank][utype][clip].is_valid()) {
			if (detailed.count(id) > 0) {
				Transform3D hidden;
				hidden.origin = Vector3(x, y, z);
				hidden.basis = hidden.basis.scaled(Vector3(0.001f, 0.001f, 0.001f));
				unit_mm[bank][utype][clip]->set_instance_transform(slot, hidden);
			} else {
				unit_mm[bank][utype][clip]->set_instance_transform(slot, xf);
			}
			// model_common.fs: color *= LOS. Own units in the fog read darker.
			const float lit = los_brightness(x, z);
			Color tint = is_player ? col_player : col_enemy;
			tint.r *= lit;
			tint.g *= lit;
			tint.b *= lit;
			unit_mm[bank][utype][clip]->set_instance_color(slot, tint);
		}

		if (detailed.count(id) == 0) continue;

		UnitVis &v = acquire_unit_slot(id);
		v.vis_stamp = unit_vis_stamp;
		v.yaw = yaw;
		attach_unit_model(v, rtso::civ_unit_model(uciv, utype));

		if (v.anim && UtilityFunctions::is_instance_valid(v.anim)) {
			v.anim->set_active(true);
			v.anim->set_callback_mode_process(AnimationMixer::ANIMATION_CALLBACK_MODE_PROCESS_MANUAL);
			v.anim->set_speed_scale(1.0);
		} else {
			v.anim = nullptr;
		}
		bool has_death_clip = v.anim && v.anim->has_animation(clip_name(CLIP_DEATH));
		if (v.anim) {
			ClipId play = resolve_clip_fallback(v.anim, want.clip);
			// A borrowed Death is an idle standing in for a corpse; never loop it.
			bool loop_clip = want.loop && play == want.clip;
			if (play != CLIP_NONE) {
				play_unit_clip(v, clip_name(play), loop_clip);
				v.anim->advance((double)delta);
				apply_unit_tools(v.model, clip_name(play));
			}
		}

		float dsc = entry.scale > 0.05f ? entry.scale : 1.0f;
		Transform3D dxf(Basis(Vector3(0, 1, 0), v.yaw).scaled(Vector3(dsc, dsc, dsc)),
		               Vector3(x, y, z));
		if (ship_wave) {
			apply_ship_wave_xform(dxf, id, anim_time, speed2);
		}
		if (dying) {
			apply_unit_death_xform(dxf, utype, death_t, x, z, entry.aabb.size.y * dsc, has_death_clip);
		}
		v.root->set_transform(dxf);
		if (ship_wave) {
			apply_ship_oar_stroke(v.model, id, anim_time, speed2);
		}

		bool is_selected = (sel_set.count((int32_t)id) > 0);
		if (is_selected) {
			v.ring->set_material_override(is_player ? ring_sel_mat : ring_enemy_mat);
			v.ring->set_visible(true);
			v.ring->set_transform(Transform3D(Basis(), Vector3(x, 0.04f, z)));
		} else if (v.ring_on) {
			v.ring->set_visible(false);
		}
		v.ring_on = is_selected;

		bool show_hp = is_selected && uview.alive[i];
		v.hp_bg->set_visible(show_hp);
		v.hp_fg->set_visible(show_hp);
		if (show_hp) {
			float hp_ratio = Math::clamp(hp / max_hp, 0.0f, 1.0f);
			float bar_y = entry.aabb.size.y + 0.4f;
			if (bar_y < 1.2f) bar_y = entry.scale * 1.8f + 0.4f;
			Vector3 hp_pos(x, bar_y, z);
			v.hp_bg->set_transform(Transform3D(Basis(), hp_pos));
			v.hp_fg->set_material_override(is_player ? hp_green_mat : hp_red_mat);
			Basis fg_basis;
			fg_basis.scale(Vector3(hp_ratio, 1.0f, 1.0f));
			Vector3 fg_offset((hp_ratio - 1.0f) * 0.44f, 0.0f, 0.01f);
			v.hp_fg->set_transform(Transform3D(fg_basis, hp_pos + fg_offset));
		}
	}

	for (size_t s = 0; s < unit_pool.size(); ++s) {
		if (unit_pool[s].root && unit_pool[s].vis_stamp != unit_vis_stamp) {
			hide_unit_vis(unit_pool[s]);
			unit_slot.erase(unit_pool[s].sim_id);
			unit_free.push_back(s);
		}
	}

	// Clean up stale visual interpolation entries
	if (crowd_pos.size() > uview.count + 64) {
		for (auto it = crowd_pos.begin(); it != crowd_pos.end(); ) {
			if (active_unit_ids.find(it->first) == active_unit_ids.end()) {
				it = crowd_pos.erase(it);
			} else {
				++it;
			}
		}
	}
	if (crowd_yaw.size() > uview.count + 64) {
		for (auto it = crowd_yaw.begin(); it != crowd_yaw.end(); ) {
			if (active_unit_ids.find(it->first) == active_unit_ids.end()) {
				it = crowd_yaw.erase(it);
			} else {
				++it;
			}
		}
	}

	// 3D Positional Combat Acoustics: Melee Weapon Clash (Swords, Spears)
	for (auto it = unit_clash_cooldown.begin(); it != unit_clash_cooldown.end(); ) {
		it->second -= delta;
		if (it->second <= 0.0f) {
			it = unit_clash_cooldown.erase(it);
		} else {
			++it;
		}
	}
	global_clash_timer -= delta;

	int clashes_this_frame = 0;
	for (size_t i = 0; i < uview.count && clashes_this_frame < 4; ++i) {
		if (!uview.alive[i] || uview.action_state[i] != rtso::ACTION_ATTACK) continue;
		uint8_t utype = uview.unit_type[i];
		bool is_spear = (utype == rtso::UNIT_SPEARMAN);
		bool is_sword = (utype == rtso::UNIT_SWORDSMAN || utype == rtso::UNIT_CAVALRY || utype == rtso::UNIT_HERO);
		if (!is_spear && !is_sword) continue;

		uint32_t uid = uview.id[i];
		if (unit_clash_cooldown.find(uid) == unit_clash_cooldown.end()) {
			float tx = uview.target_x[i] - uview.pos_x[i];
			float tz = uview.target_z[i] - uview.pos_z[i];
			float dist_sq = tx * tx + tz * tz;
			if (dist_sq <= 14.0f) { // Within melee contact range
				unit_clash_cooldown[uid] = 0.75f + ((float)(rand() % 40) / 100.0f);
				Vector3 clash_pos(uview.pos_x[i], sim->get_terrain_height(uview.pos_x[i], uview.pos_z[i]) + 1.0f, uview.pos_z[i]);
				float pitch = 0.92f + ((float)(rand() % 20) / 100.0f);
				if (is_spear) {
					int s_idx = rand() % 2;
					play_spatial_sound(sfx_spear_hit[s_idx], clash_pos, -2.0f, 65.0f, pitch);
				} else {
					int s_idx = rand() % 3;
					play_spatial_sound(sfx_sword_hit[s_idx], clash_pos, -1.0f, 65.0f, pitch);
				}
				clashes_this_frame++;
			}
		}
	}

	// =========================================================================
	// 2. Buildings: Town Center, Barracks, Houses, Towers, Storehouses, Farms
	// =========================================================================
	// Fogging.js: enemy structures out of sight are drawn as the viewer last saw
	// them (mirages); ones it never saw stay hidden.
	const auto &buildings = mirages_.buildings();
	std::unordered_set<uint32_t> alive_buildings;

	for (const auto &b : buildings) {
		alive_buildings.insert(b.id);
		BuildingVis &v = acquire_building_slot(b.id);
		if (v.root) v.root->set_visible(true);

		uint8_t bciv = b.civ;
		const char *mid = rtso::civ_building_model(bciv, b.type);
		const char *rubble_id = rtso::civ_building_rubble(bciv, b.type);
		float b_progress = b.build_progress;
		bool destroyed = (b.hp <= 0.0f || b.wrecked);
		bool prebuild = !destroyed && (b_progress < 1.0f) && !b.committed;
		bool constructing = !destroyed && (b_progress < 1.0f) && b.committed;
		float base_y = sim->get_terrain_height(b.x, b.z);

		if (destroyed) {
			if (v.phase != 3 && v.phase != 4) {
				v.phase = 3;
				v.phase_t = 0.0f;
				play_spatial_sound(sfx_building_collapse, Vector3(b.x, base_y + 1.0f, b.z), 3.0f, 95.0f, 0.95f);
			}
			v.phase_t += delta;
			if (v.phase == 3 && v.phase_t >= 0.70f)
				v.phase = 4;
		} else if (prebuild) {
			v.phase = 1;
			v.phase_t = 0.0f;
		} else if (constructing) {
			v.phase = 2;
			v.phase_t = b_progress;
		} else {
			v.phase = 0;
			v.phase_t = 1.0f;
		}

		String cat_b = "buildings";
		// Collapse = house sinks (no skeletal death clip). Then SpawnEntityOnDeath rubble.
		String visual_id = (v.phase == 4) ? String(rubble_id) : String(mid);
		attach_building_model(v, visual_id);
		auto entry = rtso::ModelLibrary::get(cat_b, visual_id);
		float b_scale = entry.scale > 0.05f ? entry.scale : 1.0f;

		v.root->set_transform(Transform3D(Basis(), Vector3(b.x, base_y, b.z)));
		set_building_site_visible(v, v.phase == 1 || v.phase == 2, v.phase == 2, b.type);

		if (Node3D *n3 = Object::cast_to<Node3D>(v.model)) {
			n3->set_scale(Vector3(b_scale, b_scale, b_scale));
			n3->set_rotation(Vector3());
			if (v.phase == 1) {
				n3->set_visible(false);
			} else if (v.phase == 2) {
				n3->set_visible(true);
				float dy = entry.aabb.size.y * b_scale;
				if (dy < 1.0f)
					dy = 4.0f;
				n3->set_position(Vector3(0, entry.y_offset + (b_progress - 1.0f) * dy, 0));
			} else if (v.phase == 3) {
				n3->set_visible(true);
				float t = Math::clamp(v.phase_t / 0.70f, 0.0f, 1.0f);
				float dy = entry.aabb.size.y * b_scale;
				if (dy < 1.0f)
					dy = 4.0f;
				n3->set_rotation(Vector3(t * 0.22f, t * 0.40f, t * -0.14f));
				n3->set_position(Vector3((t - 0.5f) * 0.45f, entry.y_offset - t * dy * 0.90f, (0.5f - t) * 0.35f));
			} else if (v.phase == 4) {
				n3->set_visible(true);
				float sink = Math::clamp((v.phase_t - 0.70f) / 2.40f, 0.0f, 1.0f);
				n3->set_position(Vector3(0, entry.y_offset - sink * 1.20f, 0));
			} else {
				n3->set_visible(true);
				n3->set_position(Vector3(0, entry.y_offset, 0));
			}
		}

		// Out of full sight (a mirage, or an own structure in the fog): darken it
		// by the LOS brightness, as model_common.fs does.
		{
			const float dim = los_brightness(b.x, b.z);
			const float q = Math::round(dim * (FOG_OVERLAY_LEVELS - 1));
			if (q != v.fog_dim) {
				set_fog_overlay(v.root, dim);
				v.fog_dim = q;
			}
		}

		// Building Health & Construction Progress Bar
		bool show_bhp = !destroyed && (b_progress < 1.0f || b.hp < b.max_hp * 0.98f);
		v.hp_bg->set_visible(show_bhp);
		v.hp_fg->set_visible(show_bhp);

		if (show_bhp) {
			float bar_ratio = (b_progress < 1.0f) ? b_progress : Math::clamp(b.hp / b.max_hp, 0.0f, 1.0f);
			float bar_y = base_y + 5.0f;
			Vector3 hp_pos(b.x, bar_y, b.z);

			v.hp_bg->set_transform(Transform3D(Basis().scaled(Vector3(2.5f, 1.0f, 1.0f)), hp_pos));
			v.hp_fg->set_material_override((b_progress < 1.0f) ? hp_blue_mat : hp_green_mat);

			Basis fg_basis;
			fg_basis.scale(Vector3(2.5f * bar_ratio, 1.0f, 1.0f));
			Vector3 fg_offset((bar_ratio - 1.0f) * 1.1f, 0.0f, 0.02f);
			v.hp_fg->set_transform(Transform3D(fg_basis, hp_pos + fg_offset));
		}

		// 3D Positional Building Fire Acoustics
		auto bit = tracked_buildings.find(b.id);
		if (bit == tracked_buildings.end()) {
			PrevBuildingState st;
			st.id = b.id;
			st.x = b.x;
			st.z = b.z;
			st.hp = b.hp;
			st.max_hp = b.max_hp;
			st.type = b.type;
			st.fire_timer = ((float)(rand() % 100) / 100.0f) * 2.0f;
			st.live = !b.miraged;
			tracked_buildings[b.id] = st;
		} else if (b.miraged) {
			// A memory: no live hit points to crackle over.
			bit->second.live = false;
		} else {
			bit->second.live = true;
			bit->second.hp = b.hp;
			bit->second.max_hp = b.max_hp;
			bit->second.fire_timer += delta;

			// If damaged below 50% HP, periodically trigger fire crackle acoustics
			if (b.hp < b.max_hp * 0.5f && b.hp > 0.0f && bit->second.fire_timer >= 2.8f) {
				bit->second.fire_timer = 0.0f;
				float by = sim->get_terrain_height(b.x, b.z);
				play_spatial_sound(sfx_building_fire, Vector3(b.x, by + 1.5f, b.z), -4.0f, 55.0f, 0.95f + ((float)(rand() % 15) / 100.0f));
			}
		}
	}

	// 3D Positional Building Collapse / Destruction Acoustics
	for (auto it = tracked_buildings.begin(); it != tracked_buildings.end(); ) {
		if (alive_buildings.find(it->first) == alive_buildings.end()) {
			// A mirage clearing when the viewer looks again is silent.
			if (it->second.live) {
				float by = sim->get_terrain_height(it->second.x, it->second.z);
				play_spatial_sound(sfx_building_collapse, Vector3(it->second.x, by + 1.0f, it->second.z), 3.0f, 95.0f, 0.95f);
			}
			it = tracked_buildings.erase(it);
		} else {
			++it;
		}
	}

	for (size_t s = 0; s < building_pool.size(); ++s) {
		if (building_pool[s].root && alive_buildings.find(building_pool[s].sim_id) == alive_buildings.end()) {
			remove_child(building_pool[s].root);
			memdelete(building_pool[s].root);
			building_pool[s].root = nullptr;
			building_pool[s].model = nullptr;
			building_pool[s].foundation = nullptr;
			building_pool[s].scaffold = nullptr;
			building_slot.erase(building_pool[s].sim_id);
		}
	}

	// =========================================================================
	// 3. Flying Projectiles (Ballistic Arrows & Catapult Stones)
	// =========================================================================
	const auto &projectiles = sim->projectiles();
	int arrow_n = 0, bolt_n = 0, stone_n = 0;
	for (const auto &proj : projectiles) {
		if (!entity_visible(proj.current_x, proj.current_z, proj.team)) continue;
		if (proj.visual == 2) stone_n++;
		else if (proj.visual == 1) bolt_n++;
		else arrow_n++;
	}
	if (arrow_mm.is_valid()) arrow_mm->set_instance_count(arrow_n);
	if (bolt_mm.is_valid()) bolt_mm->set_instance_count(bolt_n);
	if (stone_proj_mm.is_valid()) stone_proj_mm->set_instance_count(stone_n);

	std::unordered_set<uint32_t> current_proj_ids;
	int arrow_i = 0, bolt_i = 0, stone_i = 0;

	for (size_t p = 0; p < projectiles.size(); ++p) {
		const auto &proj = projectiles[p];
		if (!entity_visible(proj.current_x, proj.current_z, proj.team)) continue;
		current_proj_ids.insert(proj.id);
		Vector3 pos(proj.current_x, proj.current_y, proj.current_z);

		// Calculate direction of flight
		Vector3 dir(proj.target_x - proj.start_x, 1.2f * (1.0f - 2.0f * proj.progress), proj.target_z - proj.start_z);
		if (dir.length_squared() > 0.001f) dir.normalize();

		// In Godot, CylinderMesh is built along +Y/-Y.
		// Align column 1 (+Y) with 'dir' so the arrow/bolt points along its flight path.
		Vector3 up(0, 1, 0);
		if (Math::abs(dir.dot(up)) > 0.95f) up = Vector3(0, 0, 1);
		Vector3 right = dir.cross(up).normalized();
		Vector3 forward = right.cross(dir).normalized();
		Basis rot(right, dir, forward);

		Transform3D xf(rot, pos);
		if (proj.visual == 2 && stone_proj_mm.is_valid()) {
			xf.basis = xf.basis.scaled(Vector3(stone_proj_scale, stone_proj_scale, stone_proj_scale));
			stone_proj_mm->set_instance_transform(stone_i++, xf);
		} else if (proj.visual == 1 && bolt_mm.is_valid()) {
			xf.basis = xf.basis.scaled(Vector3(bolt_scale, bolt_scale, bolt_scale));
			bolt_mm->set_instance_transform(bolt_i++, xf);
		} else if (arrow_mm.is_valid()) {
			xf.basis = xf.basis.scaled(Vector3(arrow_scale, arrow_scale, arrow_scale));
			arrow_mm->set_instance_transform(arrow_i++, xf);
		}

		// 3D Positional Projectile Release Acoustics (Bow string whoosh / catapult launch)
		auto pit = tracked_projectiles.find(proj.id);
		if (pit == tracked_projectiles.end()) {
			bool is_crush = (proj.damage_type == 2 || proj.arc_height > 4.0f);
			PrevProjState st;
			st.id = proj.id;
			st.target_x = proj.target_x;
			st.target_y = proj.target_y;
			st.target_z = proj.target_z;
			st.last_progress = proj.progress;
			st.is_crush = is_crush;
			tracked_projectiles[proj.id] = st;

			Vector3 launch_pos(proj.start_x, proj.start_y, proj.start_z);
			float pitch = 0.95f + ((float)(rand() % 15) / 100.0f);
			if (is_crush) {
				play_spatial_sound(sfx_catapult_blast, launch_pos, -3.0f, 90.0f, 0.75f);
			} else if (proj.visual == 1 && sfx_arrow_fly.is_valid()) {
				// template_unit_ship SoundGroups attack_ranged → arrowfly.xml
				play_spatial_sound(sfx_arrow_fly, launch_pos, -1.0f, 80.0f, pitch);
			} else {
				int s_idx = rand() % 2;
				play_spatial_sound(sfx_bow_shoot[s_idx], launch_pos, -2.0f, 70.0f, pitch);
			}
		} else {
			pit->second.last_progress = proj.progress;
			pit->second.target_x = proj.target_x;
			pit->second.target_y = proj.target_y;
			pit->second.target_z = proj.target_z;
		}
	}

	// 3D Positional Projectile Impact Acoustics (Arrow hit / Catapult stone blast)
	for (auto it = tracked_projectiles.begin(); it != tracked_projectiles.end(); ) {
		if (current_proj_ids.find(it->first) == current_proj_ids.end()) {
			Vector3 hit_pos(it->second.target_x, it->second.target_y, it->second.target_z);
			float pitch = 0.92f + ((float)(rand() % 18) / 100.0f);
			if (it->second.is_crush) {
				play_spatial_sound(sfx_catapult_blast, hit_pos, 2.0f, 100.0f, pitch);
			} else {
				play_spatial_sound(sfx_arrow_hit, hit_pos, 0.0f, 65.0f, pitch);
			}
			it = tracked_projectiles.erase(it);
		} else {
			++it;
		}
	}

	// =========================================================================
	// 4. Resources: Tree Varieties, Gold Veins, Stone Mines, Fruit Bushes
	// =========================================================================
	// Explored supplies show as last seen (Fogging.js); unexplored ones not at all.
	const auto &fres = mirages_.resources();
	size_t n_tree = 0, g = 0, st = 0, fd = 0, nru = 0;
	for (const auto &r : fres) {
		if (r.subtype == rtso::RES_SUB_GRAIN) continue; // a field's grain; the field model shows it
		if (r.subtype == rtso::RES_SUB_RUINS) nru++;
		else if (r.type == rtso::RES_WOOD) n_tree++;
		else if (r.type == rtso::RES_GOLD) g++;
		else if (r.type == rtso::RES_STONE) st++;
		else if (r.type == rtso::RES_FOOD) fd++;
	}

	// All wood on tree.glb — pine/palm were easy to mistake for walking fauna
	tree_a_mm->set_instance_count((int)n_tree);
	tree_b_mm->set_instance_count(0);
	tree_c_mm->set_instance_count(0);
	gold_mm->set_instance_count((int)g);
	stone_mm->set_instance_count((int)st);
	if (food_mm.is_valid())
		food_mm->set_instance_count((int)fd);
	ruins_mm->set_instance_count((int)nru);

	size_t ia = 0, ig = 0, ist = 0, ifd = 0, iru = 0;
	auto e_ruins = rtso::ModelLibrary::get("buildings", "ruins");
	String cat_veg = "vegetation";
	String id_tree = "tree"; auto e_tree = rtso::ModelLibrary::get(cat_veg, id_tree);
	String id_gold = "gold_vein"; auto e_gold = rtso::ModelLibrary::get(cat_veg, id_gold);
	String id_stone = "stone_mine"; auto e_stone = rtso::ModelLibrary::get(cat_veg, id_stone);
	String id_food = "fruit_bush"; auto e_food = rtso::ModelLibrary::get(cat_veg, id_food);

	for (const auto &r : fres) {
		if (r.subtype == rtso::RES_SUB_GRAIN) continue;
		float rx = r.x, rz = r.z;
		float ry = sim->get_terrain_height(rx, rz);
		uint32_t h = r.id;
		Basis rot(Vector3(0, 1, 0), (float)(h % 12) * 0.52f);
		const float lit = los_brightness(rx, rz);
		const Color tint(lit, lit, lit, 1.0f);

		if (r.subtype == rtso::RES_SUB_RUINS) {
			Transform3D xf(rot, Vector3(rx, ry + e_ruins.y_offset, rz));
			ruins_mm->set_instance_transform((int)iru, xf);
			ruins_mm->set_instance_color((int)iru++, tint);
		} else if (r.type == rtso::RES_WOOD) {
			Transform3D xf(rot, Vector3(rx, ry + e_tree.y_offset, rz));
			tree_a_mm->set_instance_transform((int)ia, xf);
			tree_a_mm->set_instance_color((int)ia++, tint);
		} else if (r.type == rtso::RES_GOLD) {
			Transform3D xf(rot, Vector3(rx, ry + e_gold.y_offset, rz));
			gold_mm->set_instance_transform((int)ig, xf);
			gold_mm->set_instance_color((int)ig++, tint);
		} else if (r.type == rtso::RES_STONE) {
			Transform3D xf(rot, Vector3(rx, ry + e_stone.y_offset, rz));
			stone_mm->set_instance_transform((int)ist, xf);
			stone_mm->set_instance_color((int)ist++, tint);
		} else if (r.type == rtso::RES_FOOD && food_mm.is_valid()) {
			// Fish float at the water surface (0 A.D. Position/Floating); the fruit-bush mesh
			// stands in until the asset pipeline brings 0 A.D.'s fish shoal actor.
			if (r.subtype == rtso::RES_SUB_FISH) ry = std::max(ry, sim->water_height());
			Transform3D xf(rot, Vector3(rx, ry + e_food.y_offset, rz));
			food_mm->set_instance_transform((int)ifd, xf);
			food_mm->set_instance_color((int)ifd++, tint);
		}
	}

	// Treasures sit still, so an explored one stays drawn (darker) in the fog.
	{
		const auto &tv = sim->treasures();
		auto e_chest = rtso::ModelLibrary::get("vegetation", "treasure_chest");
		int nt = 0;
		for (const auto &t : tv) if (los_brightness(t.x, t.z) > 0.0f) ++nt;
		treasure_mm->set_instance_count(nt);
		int it = 0;
		for (const auto &t : tv) {
			const float lit = los_brightness(t.x, t.z);
			if (lit <= 0.0f) continue;
			Basis rot(Vector3(0, 1, 0), (float)(t.id % 12) * 0.52f);
			Transform3D xf(rot, Vector3(t.x, sim->get_terrain_height(t.x, t.z) + e_chest.y_offset, t.z));
			treasure_mm->set_instance_transform(it, xf);
			treasure_mm->set_instance_color(it++, Color(lit, lit * 0.85f, lit * 0.4f, 1.0f));
		}
	}

	// =========================================================================
	// 5. Wildlife Fauna MultiMeshes
	// =========================================================================
	auto fview = sim->fauna();
	auto fauna_clip_of = [](uint8_t behavior, float speed2, bool alive) -> int {
		if (!alive || behavior == rtso::FAUNA_CARCASS) return 3;
		if (behavior == rtso::FAUNA_ATTACK) return 2;
		if (behavior == rtso::FAUNA_FLEE) return 1;
		if (speed2 > 0.001f) return 1;
		return 0;
	};
	int f_counts[rtso::FAUNA_COUNT][CROWD_CLIP_COUNT] = {};
	for (size_t f = 0; f < fview.count; ++f) {
		if (!entity_visible(fview.pos_x[f], fview.pos_z[f], 255)) continue;
		uint8_t ft = fview.fauna_type[f];
		if (ft >= rtso::FAUNA_COUNT) continue;
		float vx = fview.vel_x[f];
		float vz = fview.vel_z[f];
		int clip = fauna_clip_of(fview.behavior[f], vx * vx + vz * vz, fview.alive[f] != 0);
		f_counts[ft][clip]++;
	}
	for (int ft = 0; ft < rtso::FAUNA_COUNT; ++ft) {
		int any = 0;
		for (int c = 0; c < CROWD_CLIP_COUNT; ++c) any += f_counts[ft][c];
		if (any > 0) ensure_fauna_frames((uint8_t)ft);
		cycle_puppet_meshes(fauna_puppet[ft], fauna_mm[ft], f_counts[ft]);
		for (int c = 0; c < CROWD_CLIP_COUNT; ++c) {
			if (fauna_mm[ft][c].is_valid()) {
				fauna_mm[ft][c]->set_instance_count(f_counts[ft][c]);
			}
		}
	}
	int f_written[rtso::FAUNA_COUNT][CROWD_CLIP_COUNT] = {};
	for (size_t f = 0; f < fview.count; ++f) {
		if (!entity_visible(fview.pos_x[f], fview.pos_z[f], 255)) continue;
		uint8_t ft = fview.fauna_type[f];
		if (ft >= rtso::FAUNA_COUNT) continue;
		float vx = fview.vel_x[f];
		float vz = fview.vel_z[f];
		int clip = fauna_clip_of(fview.behavior[f], vx * vx + vz * vz, fview.alive[f] != 0);
		float fx = fview.pos_x[f];
		float fz = fview.pos_z[f];
		float fy = sim->get_terrain_height(fx, fz);
		if (rtso::fauna_type_info(ft).is_naval()) fy = std::max(fy, sim->water_height()); // whales and sharks swim
		float frot = fview.rot_y[f];
		const char *fid = rtso::fauna_type_info(ft).model_id;
		auto fent = rtso::ModelLibrary::get("units", String(fid));
		float fsc = fent.scale > 0.05f ? fent.scale : 1.0f;
		Transform3D xf(Basis(Vector3(0, 1, 0), frot).scaled(Vector3(fsc, fsc, fsc)),
		               Vector3(fx, fy + fent.y_offset, fz));
		int fslot = f_written[ft][clip]++;
		if (fauna_mm[ft][clip].is_valid()) {
			fauna_mm[ft][clip]->set_instance_transform(fslot, xf);
		}
	}
}
