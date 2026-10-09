#include "world_view_node.h"
#include "territory_boundary.h"
#include "../graphics/assets/packed_siege_models.h"
#include "../animation/psa_loader.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/classes/animation.hpp>
#include <godot_cpp/classes/animation_mixer.hpp>
#include <godot_cpp/classes/animation_library.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/bone_attachment3d.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/procedural_sky_material.hpp>
#include <godot_cpp/classes/sky.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/world_environment.hpp>
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/gradient.hpp>
#include <godot_cpp/classes/gradient_texture1_d.hpp>
#include <godot_cpp/classes/curve.hpp>
#include <godot_cpp/classes/curve_texture.hpp>
#include <godot_cpp/classes/texture2d.hpp>
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

// Flora sways like 0 A.D.'s basic_trans_wind materials: a copy of the mesh
// whose surfaces use shaders/tree_wind.gdshader with the original skin.
Ref<Mesh> RtsoWorldView::wind_mesh(const Ref<Mesh> &src) {
	auto &cache = wind_cache_;
	Ref<ArrayMesh> am = src;
	if (am.is_null()) return src;
	auto it = cache.find(am->get_instance_id());
	if (it != cache.end()) return it->second;
	Ref<Shader> &sh = wind_shader_;
	if (sh.is_null()) sh = ResourceLoader::get_singleton()->load("res://shaders/tree_wind.gdshader");
	if (sh.is_null()) return src;
	Ref<ArrayMesh> out = am->duplicate(false);
	for (int s = 0; s < out->get_surface_count(); ++s) {
		Ref<BaseMaterial3D> bm = am->surface_get_material(s);
		Ref<ShaderMaterial> m;
		m.instantiate();
		m->set_shader(sh);
		if (bm.is_valid()) {
			m->set_shader_parameter("albedo_tex", bm->get_texture(BaseMaterial3D::TEXTURE_ALBEDO));
			m->set_shader_parameter("albedo", bm->get_albedo());
			m->set_shader_parameter("srgb_vertex_color", bm->get_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR));
			if (bm->get_transparency() == BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR)
				m->set_shader_parameter("alpha_scissor", bm->get_alpha_scissor_threshold());
		}
		out->surface_set_material(s, m);
	}
	cache[am->get_instance_id()] = out;
	return out;
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
	anim_controllers[rtso::UNIT_MINISTER].load_rig(rtso::RigType::Healer); // robed, like the healer model it borrows
	anim_controllers[rtso::UNIT_SIEGE_RAM].load_rig(rtso::RigType::SiegeRam);
	anim_controllers[rtso::UNIT_CATAFALQUE].load_rig(rtso::RigType::SiegeRam); // a wheeled bier, on the ram model
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
	for (int k = 0; k < TREE_KINDS; ++k) {
		tree_mm[k] = create_multimesh(tinted(rtso::ModelLibrary::get("vegetation", "tree").mesh), true);
		tree_mmi[k] = memnew(MultiMeshInstance3D);
		tree_mmi[k]->set_multimesh(tree_mm[k]);
		add_child(tree_mmi[k]);
	}
	pick_tree_kinds("mediterranean");
	gold_mm = create_multimesh(tinted(rtso::ModelLibrary::get("vegetation", "gold_vein").mesh), true);
	stone_mm = create_multimesh(tinted(rtso::ModelLibrary::get("vegetation", "stone_mine").mesh), true);
	food_mm = create_multimesh(tinted(rtso::ModelLibrary::get("vegetation", "fruit_bush").mesh), true);

	gold_mmi = memnew(MultiMeshInstance3D); gold_mmi->set_multimesh(gold_mm); add_child(gold_mmi);
	stone_mmi = memnew(MultiMeshInstance3D); stone_mmi->set_multimesh(stone_mm); add_child(stone_mmi);
	food_mmi = memnew(MultiMeshInstance3D); food_mmi->set_multimesh(food_mm); add_child(food_mmi);
	ruins_mm = create_multimesh(tinted(rtso::ModelLibrary::get("buildings", "ruins").mesh), true);
	treasure_mm = create_multimesh(tinted(rtso::ModelLibrary::get("vegetation", "treasure_chest").mesh), true);
	ruins_mmi = memnew(MultiMeshInstance3D); ruins_mmi->set_multimesh(ruins_mm); add_child(ruins_mmi);
	treasure_mmi = memnew(MultiMeshInstance3D); treasure_mmi->set_multimesh(treasure_mm); add_child(treasure_mmi);
	{
		Ref<BoxMesh> box;
		box.instantiate();
		box->set_size(Vector3(1.0f, 1.0f, 1.0f));
		Ref<StandardMaterial3D> mat;
		mat.instantiate();
		mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		mat->set_roughness(0.9f);
		scenery_mm = create_multimesh(box, true);
		scenery_mmi = memnew(MultiMeshInstance3D);
		scenery_mmi->set_multimesh(scenery_mm);
		scenery_mmi->set_material_override(mat);
		add_child(scenery_mmi);
	}
	load_scenery_actors();
	fish_mm = create_multimesh(tinted(rtso::ModelLibrary::get("vegetation", "fish").mesh), true);
	fish_mmi = memnew(MultiMeshInstance3D); fish_mmi->set_multimesh(fish_mm); add_child(fish_mmi);
	fish_mmi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);

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
			for (int c = 0; c < FAUNA_CLIP_COUNT; ++c) {
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
		spatial_players[p]->set_bus("Action"); // sound.actiongain
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

	init_damage_fx();
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
	if (Object::cast_to<GPUParticles3D>(n)) return; // fire and smoke keep their own light
	if (GeometryInstance3D *gi = Object::cast_to<GeometryInstance3D>(n)) gi->set_material_overlay(mat);
	for (int i = 0; i < n->get_child_count(); ++i) set_overlay_recursive(n->get_child(i), mat);
}

void RtsoWorldView::set_fog_overlay(Node *n, float brightness) {
	// Fully lit structures skip the extra pass.
	const int k = Math::clamp((int)Math::round(brightness * (FOG_OVERLAY_LEVELS - 1)), 0, FOG_OVERLAY_LEVELS - 1);
	set_overlay_recursive(n, k >= FOG_OVERLAY_LEVELS - 1 ? Ref<Material>() : Ref<Material>(fog_overlay_mats_[k]));
}

// Damaged-structure fire and smoke, from 0 A.D. art/particles/flame.xml and
// smoke.xml (rates, lifetimes, speeds and sizes), shown by the Health
// DamageVariants of template_structure.xml: mediumdamage (65% hit points)
// smokes, heavydamage (35%) burns. Smoke lives 6 s instead of 14-16 s so a
// burning town does not fill the smaller RTS0 maps.
void RtsoWorldView::init_damage_fx() {
	auto particle_mat = [](const char *tex_path, bool additive) {
		Ref<StandardMaterial3D> m;
		m.instantiate();
		m->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		m->set_billboard_mode(BaseMaterial3D::BILLBOARD_PARTICLES);
		m->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		if (additive) m->set_blend_mode(BaseMaterial3D::BLEND_MODE_ADD);
		m->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		m->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		if (rtso::Assets::res_exists(tex_path)) {
			Ref<Texture2D> t = ResourceLoader::get_singleton()->load(tex_path);
			if (t.is_valid()) m->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, t);
		}
		return m;
	};
	auto fade = [](float peak) {
		Ref<Gradient> g;
		g.instantiate();
		g->set_offsets(PackedFloat32Array({ 0.0f, 0.15f, 1.0f }));
		g->set_colors(PackedColorArray({ Color(1, 1, 1, 0), Color(1, 1, 1, peak), Color(1, 1, 1, 0) }));
		Ref<GradientTexture1D> gt;
		gt.instantiate();
		gt->set_gradient(g);
		return gt;
	};

	fx_flame_quad_.instantiate();
	fx_flame_quad_->set_size(Vector2(1.0f, 1.0f));
	fx_flame_quad_->set_material(particle_mat("res://assets/textures/particles/flame.png", true));
	fx_flame_ppm_.instantiate();
	fx_flame_ppm_->set_direction(Vector3(0, 1, 0));
	fx_flame_ppm_->set_spread(8.0f);
	fx_flame_ppm_->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 2.0f);
	fx_flame_ppm_->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 2.5f);
	fx_flame_ppm_->set_gravity(Vector3(0, 0, 0));
	fx_flame_ppm_->set_param_min(ParticleProcessMaterial::PARAM_SCALE, 1.0f);
	fx_flame_ppm_->set_param_max(ParticleProcessMaterial::PARAM_SCALE, 2.0f);
	fx_flame_ppm_->set_param_min(ParticleProcessMaterial::PARAM_ANGLE, -28.0f);
	fx_flame_ppm_->set_param_max(ParticleProcessMaterial::PARAM_ANGLE, 28.0f);
	fx_flame_ppm_->set_emission_shape(ParticleProcessMaterial::EMISSION_SHAPE_BOX);
	fx_flame_ppm_->set_color_ramp(fade(1.0f));

	fx_smoke_quad_.instantiate();
	fx_smoke_quad_->set_size(Vector2(1.0f, 1.0f));
	fx_smoke_quad_->set_material(particle_mat("res://assets/textures/particles/smoke_64a.png", false));
	fx_smoke_ppm_.instantiate();
	fx_smoke_ppm_->set_direction(Vector3(0.1f, 1, 0));
	fx_smoke_ppm_->set_spread(12.0f);
	fx_smoke_ppm_->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 2.0f);
	fx_smoke_ppm_->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 2.5f);
	fx_smoke_ppm_->set_gravity(Vector3(0.15f, 0, 0));
	fx_smoke_ppm_->set_param_min(ParticleProcessMaterial::PARAM_SCALE, 1.5f);
	fx_smoke_ppm_->set_param_max(ParticleProcessMaterial::PARAM_SCALE, 2.5f);
	fx_smoke_ppm_->set_param_min(ParticleProcessMaterial::PARAM_ANGLE, -180.0f);
	fx_smoke_ppm_->set_param_max(ParticleProcessMaterial::PARAM_ANGLE, 180.0f);
	fx_smoke_ppm_->set_param_min(ParticleProcessMaterial::PARAM_ANGULAR_VELOCITY, -60.0f);
	fx_smoke_ppm_->set_param_max(ParticleProcessMaterial::PARAM_ANGULAR_VELOCITY, 60.0f);
	{
		// size.growthRate 1.0 m/s: 2 m at birth, about 8 m after 6 s.
		Ref<Curve> grow;
		grow.instantiate();
		grow->set_max_value(4.0f);
		grow->add_point(Vector2(0.0f, 1.0f));
		grow->add_point(Vector2(1.0f, 4.0f));
		Ref<CurveTexture> ct;
		ct.instantiate();
		ct->set_curve(grow);
		fx_smoke_ppm_->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, ct);
	}
	fx_smoke_ppm_->set_emission_shape(ParticleProcessMaterial::EMISSION_SHAPE_BOX);
	fx_smoke_ppm_->set_color_ramp(fade(0.75f));
	fx_smoke_ppm_->set_color(Color(0.6f, 0.6f, 0.6f, 1.0f)); // color.r 0.6-1.0, grey
}

void RtsoWorldView::update_damage_fx(BuildingVis &v, uint8_t level, float half_size, float height) {
	if (level == v.fx_level || !v.root) return;
	v.fx_level = level;
	// On the roof, where 0 A.D.'s damage variants attach their fire props;
	// inside the walls the flames would be hidden.
	const float roof = height > 1.0f ? height : 4.0f;
	auto make = [&](const Ref<QuadMesh> &quad, const Ref<ParticleProcessMaterial> &ppm, int amount, float life, float y) {
		GPUParticles3D *p = memnew(GPUParticles3D);
		Ref<ParticleProcessMaterial> pm = ppm->duplicate();
		const float spread = std::max(0.5f, half_size * 0.5f);
		pm->set_emission_box_extents(Vector3(spread, 0.3f, spread));
		p->set_process_material(pm);
		p->set_draw_pass_mesh(0, quad);
		p->set_amount(amount);
		p->set_lifetime(life);
		p->set_randomness_ratio(0.2f);
		p->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		p->set_visibility_aabb(AABB(Vector3(-8, -1, -8), Vector3(16, 40, 16)));
		p->set_position(Vector3(0, y, 0));
		v.root->add_child(p);
		return p;
	};
	if (level >= 1 && !v.fx_smoke && fx_smoke_ppm_.is_valid())
		v.fx_smoke = make(fx_smoke_quad_, fx_smoke_ppm_, 54, 6.0f, roof + 0.5f); // 9/s
	if (level >= 2 && !v.fx_flame && fx_flame_ppm_.is_valid())
		v.fx_flame = make(fx_flame_quad_, fx_flame_ppm_, 26, 1.25f, roof * 0.75f); // 20/s
	if (v.fx_smoke) v.fx_smoke->set_emitting(level >= 1);
	if (v.fx_flame) v.fx_flame->set_emitting(level >= 2);
}

// The player's Game Setup colour (0 A.D. PlayerColor, rtso::PLAYER_COLORS).
static Color player_display_color(const rtso::Simulation *sim, uint8_t pid) {
	const uint32_t rgb = sim->player_color(pid);
	return Color(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1.0f);
}

// 0 A.D. player_trans surfaces take the owner's colour (model_common.fs).
static void push_player_color(Node *model, int64_t &last, const Color &c) {
	const int64_t key = (int64_t)c.to_rgba32();
	if (!model || last == key) return;
	rtso::ModelLibrary::set_player_color(model, c);
	last = key;
}

void RtsoWorldView::load_garrison_flags() {
	flags_loaded_ = true;
	Ref<FileAccess> f = FileAccess::open("res://data/garrison_flags.json", FileAccess::READ);
	if (!f.is_valid()) return;
	Dictionary root = JSON::parse_string(f->get_as_text());
	Dictionary models = root.get("models", Dictionary());
	Ref<Shader> shader = ResourceLoader::get_singleton()->load("res://shaders/garrison_flag.gdshader");
	Ref<PackedScene> scene = ResourceLoader::get_singleton()->load("res://assets/models/props/garrison_flag.glb");
	if (!shader.is_valid() || !scene.is_valid()) return;
	if (Node *inst = scene->instantiate()) {
		TypedArray<Node> meshes = inst->find_children("*", "MeshInstance3D", true, false);
		if (meshes.size() > 0) flag_mesh_ = Object::cast_to<MeshInstance3D>(meshes[0])->get_mesh();
		memdelete(inst);
	}
	std::unordered_map<std::string, int> styles;
	Array keys = models.keys();
	for (int i = 0; i < keys.size(); ++i) {
		Dictionary e = models[keys[i]];
		const std::string mid = String(keys[i]).utf8().get_data();
		model_ground_[mid] = ModelGround{ (float)e.get("scale", 0.0), (float)e.get("ground", 0.0) };
		Array pos = e.get("pos", Array());
		String style = e.get("flag", "hele");
		if (pos.size() != 3) continue;
		auto it = styles.find(style.utf8().get_data());
		if (it == styles.end()) {
			Ref<Texture2D> tex = ResourceLoader::get_singleton()->load("res://assets/textures/garrison_flags/" + style + ".png");
			Ref<ShaderMaterial> m;
			m.instantiate();
			m->set_shader(shader);
			m->set_shader_parameter("albedo_tex", tex);
			flag_mats_.push_back(m);
			it = styles.emplace(style.utf8().get_data(), (int)flag_mats_.size() - 1).first;
		}
		FlagPoint fp;
		fp.pos = Vector3((float)pos[0], (float)pos[1], (float)pos[2]);
		fp.scale = (float)e.get("scale", 1.0);
		fp.style = it->second;
		flag_points_[mid] = fp;
	}
}

void RtsoWorldView::load_scenery_actors() {
	Ref<FileAccess> f = FileAccess::open("res://data/scenery_actors.json", FileAccess::READ);
	if (!f.is_valid()) return;
	Dictionary types = Dictionary(JSON::parse_string(f->get_as_text())).get("types", Dictionary());
	Array keys = types.keys();
	for (int i = 0; i < keys.size(); ++i) {
		const uint8_t t = rtso::scenery_type_by_name(String(keys[i]).utf8().get_data());
		if (t >= rtso::SCENERY_TYPE_COUNT) continue;
		Array files = types[keys[i]];
		for (int v = 0; v < files.size(); ++v) {
			// "scenery/fence_long_0.glb" -> ModelLibrary id "fence_long_0" in category "scenery".
			const String id = String(files[v]).get_file().get_basename();
			Ref<Mesh> mesh = rtso::ModelLibrary::get("scenery", id).mesh;
			if (mesh.is_null()) continue;
			enable_instance_tint(mesh);
			SceneryVariant k;
			k.mm = create_multimesh(mesh, true);
			MultiMeshInstance3D *mmi = memnew(MultiMeshInstance3D);
			mmi->set_multimesh(k.mm);
			add_child(mmi);
			scenery_variants_[t].push_back(k);
		}
	}
}

// UnitMotionFlying of template_bird.xml, in RTS0 units (1 unit = 4 m):
// MaxSpeed 15 m/s, TurnRate 0.5 rad/s, FlyingHeight 40 m, ClimbRate 4 m/s.
// UnitAI roams them over the whole map (RoamDistance 1000 m).
static constexpr float BIRD_SPEED = 15.0f / 4.0f;
static constexpr float BIRD_TURN_RATE = 0.5f;
static constexpr float BIRD_HEIGHT = 40.0f / 4.0f;
static constexpr float BIRD_CLIMB = 4.0f / 4.0f;
static constexpr float BIRD_SCALE = 0.25f; // the hawk mesh is in metres

static float bird_rand(uint32_t &s) { // xorshift32 in [0, 1); view only, never hashed
	s ^= s << 13; s ^= s >> 17; s ^= s << 5;
	return (float)(s & 0xFFFFFF) / (float)0x1000000;
}

void RtsoWorldView::update_birds(const rtso::Simulation *sim, float delta) {
	const float half = sim->map_half();
	if (sim->current_map_id() != birds_map_) {
		birds_map_ = sim->current_map_id();
		for (Bird &b : birds_)
			if (b.node) b.node->queue_free();
		birds_.clear();
		Ref<FileAccess> f = FileAccess::open("res://data/birds.json", FileAccess::READ);
		auto entry = rtso::ModelLibrary::get("units", "hawk");
		if (!f.is_valid() || !entry.scene.is_valid()) return;
		Dictionary root = JSON::parse_string(f->get_as_text());
		const String mid = birds_map_.c_str();
		uint32_t seed = 2166136261u;
		for (char c : birds_map_) seed = (seed ^ (uint8_t)c) * 16777619u;
		std::vector<Vector2> spawn;
		Array placed = Dictionary(root.get("maps", Dictionary())).get(mid, Array());
		for (int i = 0; i < placed.size(); ++i) {
			Array uv = placed[i];
			if (uv.size() == 2) spawn.push_back(Vector2((float)uv[0], (float)uv[1]) * (2.0f * half));
		}
		Dictionary rnd = Dictionary(root.get("random", Dictionary())).get(mid, Dictionary());
		if (!rnd.is_empty()) {
			int n = 0;
			if (rnd.has("per_player")) {
				uint32_t teams = 0;
				for (const auto &b : sim->buildings())
					if (b.team < rtso::MAX_PLAYERS) teams |= 1u << b.team;
				n = (int)rnd["per_player"] * std::max(1, __builtin_popcount(teams));
			} else {
				// rmgen scaleByMapSize(min, max): linear from 128 to 512 tiles (1 tile = 1 unit).
				const float t = Math::clamp((2.0f * half - 128.0f) / 384.0f, 0.0f, 1.0f);
				n = (int)std::ceil((float)rnd["min"] + ((float)rnd["max"] - (float)rnd["min"]) * t);
			}
			uint32_t s = seed | 1u;
			for (int i = 0; i < n; ++i)
				spawn.push_back(Vector2(bird_rand(s) - 0.5f, bird_rand(s) - 0.5f) * (1.6f * half));
		}
		for (size_t i = 0; i < spawn.size(); ++i) {
			Node3D *n = Object::cast_to<Node3D>(entry.scene->instantiate());
			if (!n) continue;
			rtso::ModelLibrary::apply_imported_material_fixups(n);
			add_child(n);
			TypedArray<Node> aps = n->find_children("*", "AnimationPlayer", true, false);
			if (AnimationPlayer *ap = aps.is_empty() ? nullptr : Object::cast_to<AnimationPlayer>(aps[0])) {
				const StringName walk("Walk"); // hawk_flight
				if (ap->has_animation(walk)) {
					if (Ref<Animation> a = ap->get_animation(walk); a.is_valid()) a->set_loop_mode(Animation::LOOP_LINEAR);
					ap->play(walk);
					ap->seek(0.37 * (double)i, true); // out of step
				}
			}
			Bird b;
			b.node = n;
			b.pos = b.target = spawn[i];
			b.rng = (seed + 0x9E3779B9u * (uint32_t)(i + 1)) | 1u;
			b.yaw = bird_rand(b.rng) * (float)Math_TAU;
			b.y = sim->get_terrain_height(b.pos.x, b.pos.y) + BIRD_HEIGHT;
			birds_.push_back(b);
		}
	}
	for (Bird &b : birds_) {
		// Roam: fly toward a target anywhere on the map, pick a new one on arrival.
		if (b.pos.distance_squared_to(b.target) < 16.0f)
			b.target = Vector2(bird_rand(b.rng) - 0.5f, bird_rand(b.rng) - 0.5f) * (1.8f * half);
		const Vector2 d = b.target - b.pos;
		const float want = Math::atan2(-d.x, -d.y);
		const float turn = Math::wrapf(want - b.yaw, -(float)Math_PI, (float)Math_PI);
		b.yaw += Math::clamp(turn, -BIRD_TURN_RATE * delta, BIRD_TURN_RATE * delta);
		b.pos += Vector2(-Math::sin(b.yaw), -Math::cos(b.yaw)) * (BIRD_SPEED * delta);
		const float want_y = sim->get_terrain_height(b.pos.x, b.pos.y) + BIRD_HEIGHT;
		b.y += Math::clamp(want_y - b.y, -BIRD_CLIMB * delta, BIRD_CLIMB * delta);
		// Banks into the turn like the hawk actor's glide.
		const float bank = Math::clamp(turn, -0.6f, 0.6f) * 0.5f;
		b.node->set_transform(Transform3D(Basis(Vector3(0, 1, 0), b.yaw) * Basis(Vector3(0, 0, 1), -bank) *
						Basis().scaled(Vector3(BIRD_SCALE, BIRD_SCALE, BIRD_SCALE)),
				Vector3(b.pos.x, b.y, b.pos.y)));
		b.node->set_visible(los_brightness(b.pos.x, b.pos.y) > 0.0f);
	}
}

// 0 A.D. turret points (TurretHolder) are metres above the holder's ground
// plane, and the sim scales them by COMBAT_DISTANCE_SCALE. Buildings are drawn
// fitted to a target height and grounded at their lowest vertex (wall meshes
// reach metres below 0 A.D.'s ground), so map the point onto the drawn model.
float RtsoWorldView::turret_draw_y(const rtso::Simulation *sim, uint32_t unit_id, float offset, float x, float z) {
	const uint32_t holder = sim->turret_holder_of(unit_id);
	const rtso::Simulation::Building *hb = nullptr;
	if (holder)
		for (const rtso::Simulation::Building &b : sim->buildings())
			if (b.id == holder) { hb = &b; break; }
	if (!hb) return sim->get_terrain_height(x, z) + offset;
	if (!flags_loaded_) load_garrison_flags();
	const String mid = building_visual_id(sim->team_civ(hb->team), hb->type, hb->half_size);
	auto entry = rtso::ModelLibrary::get("buildings", mid);
	const float b_scale = entry.scale > 0.05f ? entry.scale : 1.0f;
	const float base = sim->get_terrain_height(hb->x, hb->z) + entry.y_offset;
	// ModelLibrary falls back to the civ-less model ("athen_outpost" -> "outpost").
	auto it = model_ground_.find(rtso::ModelLibrary::building_glb_id(mid).utf8().get_data());
	if (it == model_ground_.end()) {
		const int us = mid.find("_");
		if (us > 0) it = model_ground_.find(mid.substr(us + 1).utf8().get_data());
	}
	if (it != model_ground_.end() && it->second.scale > 0.0f) {
		const float metres = offset / rtso::COMBAT_DISTANCE_SCALE;
		return base + (it->second.ground + metres * it->second.scale) * b_scale;
	}
	return base + std::min(offset, entry.aabb.size.y * b_scale);
}

// Structures prop their civ's flag on the "garrisoned" point while anyone is
// inside (the actors' garrisoned variant, picked by GarrisonHolder.js).
void RtsoWorldView::update_garrison_flag(BuildingVis &v, const String &model_id, bool show, const Color &owner_color,
		float b_scale, float y_offset) {
	if (!flags_loaded_ && show) load_garrison_flags();
	const FlagPoint *fp = nullptr;
	if (show && flag_mesh_.is_valid()) {
		auto it = flag_points_.find(rtso::ModelLibrary::building_glb_id(model_id).utf8().get_data());
		if (it != flag_points_.end()) fp = &it->second;
	}
	if (!fp) {
		if (v.flag) v.flag->set_visible(false);
		return;
	}
	if (!v.flag) {
		v.flag = memnew(MeshInstance3D);
		v.flag->set_mesh(flag_mesh_);
		v.flag->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_ON);
		v.root->add_child(v.flag);
	}
	v.flag->set_material_override(flag_mats_[fp->style]);
	v.flag->set_instance_shader_parameter("player_color", owner_color);
	const float fs = b_scale * fp->scale;
	v.flag->set_transform(Transform3D(Basis().scaled(Vector3(fs, fs, fs)),
			Vector3(0, y_offset, 0) + fp->pos * b_scale));
	v.flag->set_visible(true);
}

void RtsoWorldView::update_territory_borders(const rtso::Simulation *sim) {
	const auto &grid = sim->territory_grid();
	if (grid.w <= 0 || grid.h <= 0) return;
	if (grid.data == border_grid_cache_ && border_half_ == sim->map_half()) return;
	border_grid_cache_ = grid.data;
	border_half_ = sim->map_half();

	if (!border_mi_) {
		Ref<Shader> sh;
		sh.instantiate();
		// Player-coloured stripe on the outer edge fading inward; darkened by LOS
		// and hidden in unexplored land like 0 A.D.'s textured overlay lines.
		sh->set_code(
				"shader_type spatial;\n"
				"render_mode blend_mix, unshaded, cull_disabled, depth_draw_never, shadows_disabled, fog_disabled;\n"
				"uniform sampler2D los_tex : hint_default_white, filter_linear, repeat_disable;\n"
				"uniform float los_map_half = 0.0;\n"
				"varying vec3 wpos;\n"
				"void vertex() { wpos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz; }\n"
				"void fragment() {\n"
				"  float v = UV.y;\n"
				"  float core = smoothstep(0.0, 0.10, v) * (1.0 - smoothstep(0.28, 0.40, v));\n"
				"  float glow = (1.0 - smoothstep(0.28, 1.0, v)) * 0.40;\n"
				"  float dash = 0.8 + 0.2 * step(0.5, fract(UV.x));\n"
				"  float los = 1.0;\n"
				"  if (los_map_half > 0.0) los = texture(los_tex, (wpos.xz + vec2(los_map_half)) / (2.0 * los_map_half)).r;\n"
				"  ALBEDO = COLOR.rgb * mix(0.5, 1.0, los) * dash;\n"
				"  ALPHA = max(core, glow) * smoothstep(0.02, 0.25, los);\n"
				"}\n");
		border_mat_.instantiate();
		border_mat_->set_shader(sh);
		border_mi_ = memnew(MeshInstance3D);
		border_mi_->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		border_mi_->set_material_override(border_mat_);
		add_child(border_mi_);
		bind_los(border_mat_);
	}

	// RTS0 maps are a quarter of 0 A.D.'s scale (Tiny: 128 m against 512 m).
	const float kScale = 0.25f;
	const float thickness = 3.5f * kScale;  // BorderThickness
	const float separation = 0.5f * kScale; // BorderSeparation
	const float cell = (border_half_ * 2.0f) / (float)grid.w;

	PackedVector3Array verts;
	PackedColorArray cols;
	PackedVector2Array uvs;
	PackedInt32Array idx;
	auto boundaries = rtso::compute_territory_boundaries(grid.data.data(), grid.w, grid.h);
	for (auto &b : boundaries) {
		if (b.points.size() < 3) continue;
		rtso::smooth_points_average(b.points);
		rtso::interpolate_points_rns(b.points, separation / cell);
		const Color col = player_display_color(sim, (uint8_t)(b.owner - 1));
		const int n = (int)b.points.size();
		const int base = verts.size();
		float run = 0.0f;
		for (int k = 0; k <= n; ++k) {
			const rtso::BoundaryPoint &p = b.points[k % n];
			const rtso::BoundaryPoint &pn = b.points[(k + 1) % n];
			const rtso::BoundaryPoint &pp = b.points[(k + n - 1) % n];
			const float wx = -border_half_ + p.x * cell;
			const float wz = -border_half_ + p.y * cell;
			float tx = pn.x - pp.x, tz = pn.y - pp.y;
			const float tl = Math::sqrt(tx * tx + tz * tz);
			if (tl > 1e-5f) { tx /= tl; tz /= tl; }
			// Owner's land is on the left of the walk: (-t.z, t.x) points inward.
			const float ix = wx - tz * thickness, iz = wz + tx * thickness;
			if (k > 0) {
				const rtso::BoundaryPoint &q = b.points[(k - 1) % n];
				const float dx = (p.x - q.x) * cell, dz = (p.y - q.y) * cell;
				run += Math::sqrt(dx * dx + dz * dz);
			}
			verts.push_back(Vector3(wx, sim->get_terrain_height(wx, wz) + 0.12f, wz));
			verts.push_back(Vector3(ix, sim->get_terrain_height(ix, iz) + 0.12f, iz));
			uvs.push_back(Vector2(run / 1.5f, 0.0f));
			uvs.push_back(Vector2(run / 1.5f, 1.0f));
			cols.push_back(col);
			cols.push_back(col);
			if (k > 0) {
				const int a = base + (k - 1) * 2;
				idx.push_back(a);
				idx.push_back(a + 1);
				idx.push_back(a + 2);
				idx.push_back(a + 1);
				idx.push_back(a + 3);
				idx.push_back(a + 2);
			}
		}
	}
	border_mesh_.instantiate();
	if (verts.size() > 0) {
		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = verts;
		arrays[Mesh::ARRAY_COLOR] = cols;
		arrays[Mesh::ARRAY_TEX_UV] = uvs;
		arrays[Mesh::ARRAY_INDEX] = idx;
		border_mesh_->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	}
	border_mi_->set_mesh(border_mesh_);
}

void RtsoWorldView::update_fog(const rtso::Simulation *sim, float delta) {
	mirages_.update(*sim, local_player_);
	const int res = sim->fow_res();
	const bool new_map = res != los_.res() || los_half_ != sim->map_half();
	// The sim's per-player LOS (Simulation::los), as FogMirages built it this frame.
	los_.set_target(mirages_.view().size() == (size_t)res * (size_t)res ? mirages_.view().data() : nullptr, res);
	if (new_map) {
		los_.snap();
		los_half_ = sim->map_half();
		los_image_ = Image::create(res, res, false, Image::FORMAT_L8);
		los_tex_ = ImageTexture::create_from_image(los_image_);
		if (terrain) bind_los(terrain->get_material_override());
		if (water_mesh_node) bind_los(water_mesh_node->get_material_override());
		bind_los(border_mat_);
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
	CLIP_PROMOTION,
	CLIP_CARRY_FOOD,
	CLIP_CARRY_MEAT,
	CLIP_CARRY_WOOD,
	CLIP_CARRY_STONE,
	CLIP_CARRY_METAL,
	CLIP_CAPTURE,
	CLIP_IDLE2,
	CLIP_DEATH2,
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
		StringName("Slaughter"), StringName("Promotion"),
		StringName("CarryFood"), StringName("CarryMeat"), StringName("CarryWood"),
		StringName("CarryStone"), StringName("CarryMetal"), StringName("Capture"),
		StringName("Idle2"), StringName("Death2")
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

static Skeleton3D *find_skeleton3d(Node *n) {
	if (!n) return nullptr;
	if (Skeleton3D *sk = Object::cast_to<Skeleton3D>(n)) return sk;
	for (int i = 0; i < n->get_child_count(); ++i) {
		if (Skeleton3D *found = find_skeleton3d(n->get_child(i))) return found;
	}
	return nullptr;
}

static void detach_carry_props(Skeleton3D *sk) {
	if (!sk) return;
	for (int i = sk->get_child_count() - 1; i >= 0; --i) {
		Node *c = sk->get_child(i);
		if (String(c->get_name()).begins_with("rtso_carry")) {
			sk->remove_child(c);
			c->queue_free();
		}
	}
}

// The citizen women (and RTS0's villager, drawn as one) take the female_* variants.
static bool unit_is_female(uint8_t utype) {
	return utype == rtso::UNIT_FEMALE_CITIZEN || utype == rtso::UNIT_VILLAGER;
}

static bool is_carry_variant(ClipId c) {
	return c >= CLIP_CARRY_FOOD && c <= CLIP_CARRY_METAL;
}

// UnitAI.SetDefaultAnimationVariant: "carry_" + generic type, with meat split
// from grain and fruit (art/variants/biped/carry_*.xml).
static ClipId carry_clip_for(uint8_t res_type, uint8_t subtype) {
	switch (res_type) {
		case rtso::RES_WOOD: return CLIP_CARRY_WOOD;
		case rtso::RES_GOLD: return CLIP_CARRY_METAL;
		case rtso::RES_STONE: return CLIP_CARRY_STONE;
		case rtso::RES_FOOD: return subtype == rtso::RES_SUB_MEAT ? CLIP_CARRY_MEAT : CLIP_CARRY_FOOD;
		default: return CLIP_CARRY;
	}
}

// 0 A.D. action → clip (Attack.js, ResourceGatherer.js, CCmpVisualActor, UnitAI carry variant).
struct WantAnim {
	ClipId clip = CLIP_IDLE;
	bool loop = true;
	rtso::AnimState state = rtso::AnimState::Idle;
};

static WantAnim resolve_unit_anim(uint8_t uaction, float speed2, bool alive, int32_t carry,
		uint8_t utype, uint8_t res_type, uint8_t res_sub, bool capturing) {
	WantAnim w;
	// A laden unit keeps the carry variant through any movement clip.
	const bool laden_move = carry > 0 && (speed2 > 0.001f || uaction == rtso::ACTION_MOVE);
	// ~4.24 m/s: infantry walk (~3.2) stays Walk; cavalry/camel/chariot pick Run.
	const bool sprint = !laden_move && speed2 > 18.0f;
	WantAnim w_move;
	const ClipId carry_clip = carry_clip_for(res_type, res_sub);
	w_move.clip = laden_move ? carry_clip : (sprint ? CLIP_RUN : CLIP_WALK);
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
		// UnitAI INDIVIDUAL.HEAL: "heal" once in range, the move clip on the way.
		case rtso::ACTION_HEAL:
			if (speed2 > 0.001f) return w_move;
			w.clip = CLIP_HEAL;
			w.state = rtso::AnimState::Gather;
			return w;
		// UnitAI INDIVIDUAL.CHEERING: SelectAnimation("promotion").
		case rtso::ACTION_CHEER:
			w.clip = CLIP_PROMOTION;
			w.loop = false;
			w.state = rtso::AnimState::Idle;
			return w;
		case rtso::ACTION_ATTACK:
			// Ships: Attack.js keeps attack_ranged until StopAttacking (sail falls back to idle).
			if (speed2 <= 8.0f || rtso::unit_is_ship(utype)) {
				const bool hunt = (utype == rtso::UNIT_VILLAGER || utype == rtso::UNIT_FEMALE_CITIZEN);
				// variants/biped/attack_capture.xml: "capture" while taking a structure.
				w.clip = capturing ? CLIP_CAPTURE : (hunt ? CLIP_SLAUGHTER : CLIP_ATTACK);
				w.state = rtso::AnimState::Attack;
				return w;
			}
			return w_move;
		// UnitAI.SetDefaultAnimationVariant → carry_food / carry_*; fishing hull uses rowing_move.
		case rtso::ACTION_RETURN_DROPSITE:
			w.clip = carry_clip;
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

// 0 A.D. picks one of an actor's same-named animations at random; here each
// unit keeps one by its id: half fall with the second death, and a quarter
// stand with the second idle (the first is the most frequent in 0 A.D.).
static WantAnim with_variant(WantAnim w, uint32_t id) {
	const uint32_t h = id * 2654435761u;
	if (w.clip == CLIP_DEATH && (h & 0x80000000u)) w.clip = CLIP_DEATH2;
	else if (w.clip == CLIP_IDLE && (h >> 29) == 3u) w.clip = CLIP_IDLE2;
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
		case CLIP_CARRY_FOOD:
		case CLIP_CARRY_MEAT:
		case CLIP_CARRY_WOOD:
		case CLIP_CARRY_STONE:
		case CLIP_CARRY_METAL:
			if (anim->has_animation(clip_name(CLIP_CARRY))) return CLIP_CARRY;
			alt = CLIP_WALK;
			break;
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
		case CLIP_CAPTURE:   alt = CLIP_ATTACK; break;
		case CLIP_IDLE2:     alt = CLIP_IDLE; break;
		case CLIP_DEATH2:    alt = CLIP_DEATH; break;
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

// 0 A.D. unit actors declare <castshadow/>: units cast shadows.
static void enable_mesh_shadows(Node *n) {
	if (!n) return;
	if (GeometryInstance3D *gi = Object::cast_to<GeometryInstance3D>(n)) {
		gi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_ON);
	}
	for (int i = 0; i < n->get_child_count(); ++i) {
		enable_mesh_shadows(n->get_child(i));
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
	v.player_rgba = -1;
	v.clip = StringName();
	v.carry_prop = nullptr; // freed with the old model
	v.carry_clip = 0;
	String cat_units = "units";
	auto entry = rtso::ModelLibrary::get(cat_units, model_name);
	if (entry.scene.is_valid()) {
		v.model = entry.scene->instantiate();
		rtso::ModelLibrary::apply_imported_material_fixups(v.model);
		v.root->add_child(v.model);
		enable_mesh_shadows(v.model);
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
	enable_mesh_shadows(mi);
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
			v.player_rgba = -1;
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

// 0 A.D. WallSet segments (Simulation::cmd_build_wall) are 12, 24 or 36 m long and
// draw the civ's wall_short, wall_medium or wall_long actor (a gate made from a long
// one its wall_gate); one-cell pieces from maps and older saves keep their model.
String RtsoWorldView::building_visual_id(uint8_t civ, uint8_t type, float half_size) {
	if (type == rtso::BLD_WALL_SEGMENT && half_size >= 1.25f) {
		const char *len = half_size >= 3.75f ? "wall_long" : half_size >= 2.25f ? "wall_medium" : "wall_short";
		return String(rtso::civ_info(civ < rtso::CIV_COUNT ? civ : rtso::CIV_ATHENIANS).code) + "_" + len;
	}
	// Only a long segment becomes a gate; it keeps the segment's length.
	if (type == rtso::BLD_GATE && half_size >= 3.75f)
		return String(rtso::civ_info(civ < rtso::CIV_COUNT ? civ : rtso::CIV_ATHENIANS).code) + "_wall_gate";
	return String(rtso::civ_building_model(civ, type));
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
	v.player_rgba = -1;
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

// Crowd clip slots. 0 A.D. plays the real clip for every unit (gather_*,
// build, carry, heal...); the crowd keeps one baked MultiMesh per slot.
static const uint8_t kCrowdClips[] = {
	CLIP_IDLE, CLIP_WALK, CLIP_ATTACK, CLIP_DEATH, CLIP_RUN, CLIP_CARRY, CLIP_BUILD,
	CLIP_GATHER_TREE, CLIP_GATHER_ORE, CLIP_GATHER_ROCK, CLIP_GATHER_FRUIT, CLIP_GATHER_GRAIN,
	CLIP_GATHER_MEAT, CLIP_GATHER_FISH, CLIP_HEAL, CLIP_SLAUGHTER,
	CLIP_CARRY_FOOD, CLIP_CARRY_MEAT, CLIP_CARRY_WOOD, CLIP_CARRY_STONE, CLIP_CARRY_METAL,
	CLIP_PROMOTION, CLIP_CAPTURE, CLIP_IDLE2, CLIP_DEATH2,
};
static_assert(sizeof(kCrowdClips) == 25, "kCrowdClips must match CROWD_CLIP_COUNT");
// Death clips play once and hold their last frame (slots 3 and 24).
static constexpr uint32_t CROWD_DEATH_SLOTS = (1u << 3) | (1u << 24);

static int crowd_slot_of(ClipId clip) {
	switch (clip) {
		case CLIP_WALK: return 1;
		case CLIP_ATTACK: return 2;
		case CLIP_DEATH: return 3;
		case CLIP_RUN: return 4;
		case CLIP_CARRY: return 5;
		case CLIP_BUILD: return 6;
		case CLIP_GATHER:
		case CLIP_GATHER_TREE: return 7;
		case CLIP_GATHER_ORE: return 8;
		case CLIP_GATHER_ROCK: return 9;
		case CLIP_GATHER_FRUIT: return 10;
		case CLIP_GATHER_GRAIN: return 11;
		case CLIP_GATHER_MEAT: return 12;
		case CLIP_GATHER_FISH: return 13;
		case CLIP_HEAL: return 14;
		case CLIP_SLAUGHTER: return 15;
		case CLIP_CARRY_FOOD: return 16;
		case CLIP_CARRY_MEAT: return 17;
		case CLIP_CARRY_WOOD: return 18;
		case CLIP_CARRY_STONE: return 19;
		case CLIP_CARRY_METAL: return 20;
		case CLIP_PROMOTION: return 21;
		case CLIP_CAPTURE: return 22;
		case CLIP_IDLE2: return 23;
		case CLIP_DEATH2: return 24;
		default: return 0;
	}
}

// 0 A.D. Pack.js swaps to the packed template once packing ends and back once
// unpacking ends, so the wagon shows from PACKED through UNPACKING.
static bool shows_packed(uint8_t pack_state) {
	return pack_state == rtso::PACK_PACKED || pack_state == rtso::PACK_UNPACKING;
}

// The model a crowd key draws: the unit's own, its packed wagon, or its
// roster template's own model.
static const char *crowd_key_model(uint8_t civ, uint16_t key) {
	if (key >= 2 * rtso::UNIT_TYPE_COUNT) {
		if (const char *own = rtso::unit_template_draw_model(key - 2 * rtso::UNIT_TYPE_COUNT)) return own;
		key = rtso::UNIT_VILLAGER;
	}
	const uint8_t utype = (uint8_t)(key % rtso::UNIT_TYPE_COUNT);
	const char *model = rtso::civ_unit_model(civ, utype);
	if (key >= rtso::UNIT_TYPE_COUNT) {
		if (const char *packed = rtso::packed_siege_model(model)) return packed;
	}
	return model;
}

static uint16_t crowd_key_of(uint8_t civ, uint8_t utype, uint8_t pack_state, uint16_t tmpl) {
	if (shows_packed(pack_state) && rtso::packed_siege_model(rtso::civ_unit_model(civ, utype)))
		return (uint16_t)(rtso::UNIT_TYPE_COUNT + utype);
	const int own = rtso::unit_template_draw_index(tmpl);
	if (own >= 0) return (uint16_t)(2 * rtso::UNIT_TYPE_COUNT + own);
	return utype;
}

RtsoWorldView::CrowdBatch &RtsoWorldView::crowd_batch(uint8_t civ, uint16_t key) {
	if (civ >= rtso::CIV_COUNT) civ = rtso::CIV_ATHENIANS;
	if (key >= CROWD_KEY_COUNT) key = rtso::UNIT_VILLAGER;
	std::unique_ptr<CrowdBatch> &slot = crowd_[civ][key];
	if (!slot) {
		slot = std::make_unique<CrowdBatch>();
		auto entry = rtso::ModelLibrary::get("units", crowd_key_model(civ, key));
		slot->rest = entry.crowd_mesh.is_valid() ? entry.crowd_mesh : entry.mesh;
		slot->no_scene = !entry.scene.is_valid();
		for (int s = 0; slot->rest.is_valid() && s < slot->rest->get_surface_count(); ++s)
			if (rtso::ModelLibrary::is_player_color_material(slot->rest->surface_get_material(s))) slot->player_color = true;
	}
	return *slot;
}

MultiMesh *RtsoWorldView::crowd_mm(CrowdBatch &b, int clip) {
	if (!b.mmi[clip]) {
		Ref<Mesh> m = b.puppet[clip].posed_frames[0].is_valid() ? Ref<Mesh>(b.puppet[clip].posed_frames[0]) : b.rest;
		b.mm[clip] = create_multimesh(m, true);
		b.mm[clip]->set_use_custom_data(true); // owner colour for player_color.gdshader
		b.mmi[clip] = memnew(MultiMeshInstance3D);
		b.mmi[clip]->set_multimesh(b.mm[clip]);
		// 0 A.D. unit actors carry <castshadow/>.
		b.mmi[clip]->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_ON);
		add_child(b.mmi[clip]);
	}
	return b.mm[clip].ptr();
}

void RtsoWorldView::bake_puppet_clips(Node *inst, CrowdPuppet *row, bool *done, const uint8_t *want,
		const bool *needed, int count, uint32_t death_slots, bool female) {
	AnimationPlayer *ap = find_anim_player(inst);
	if (ap) {
		ap->set_active(true);
		ap->set_callback_mode_process(AnimationMixer::ANIMATION_CALLBACK_MODE_PROCESS_MANUAL);
	}
	for (int c = 0; c < count; ++c) {
		if (!needed[c] || done[c]) continue;
		done[c] = true;
		ClipId play = resolve_clip_fallback(ap, (ClipId)want[c]);
		int valid = 0;
		if (ap && play != CLIP_NONE) {
			const StringName &name = clip_name(play);
			// Show the clip's tool (axe, pick, hammer...) before capturing, as
			// capture skips hidden props.
			apply_unit_tools(inst, name);
			// The carry shuttle bakes into the clip's frames, riding its bone.
			Node3D *prop = is_carry_variant(play) ? attach_carry_prop(inst, play, female) : nullptr;
			rtso::ModelLibrary::CrowdSkin skin;
			const bool captured = rtso::ModelLibrary::capture_crowd_skin(inst, skin);
			if (!captured) {
				if (prop) detach_carry_props(find_skeleton3d(inst));
				row[c].baked_count = 0;
				row[c].last_frame = -1;
				continue;
			}
			double len = 0.0;
			Ref<Animation> a = ap->get_animation(name);
			if (a.is_valid()) len = a->get_length();
			ap->play(name);
			const int n = CROWD_FRAMES_PER_CLIP;
			for (int f = 0; f < n; ++f) {
				double t = 0.0;
				if (len > 0.0) {
					if (death_slots & (1u << c)) {
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
			if (prop) detach_carry_props(find_skeleton3d(inst));
		}
		row[c].baked_count = valid;
		row[c].last_frame = -1;
	}
	if (ap) ap->stop();
}

void RtsoWorldView::cycle_puppet_meshes(CrowdPuppet *row, Ref<MultiMesh> *meshes, const int *counts, int count, uint32_t death_slots) {
	for (int c = 0; c < count; ++c) {
		if (!meshes[c].is_valid() || row[c].baked_count <= 0 || counts[c] <= 0) continue;
		int frame;
		if (death_slots & (1u << c)) {
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

static Node *instantiate_for_bake(Node3D *bake_root, const Ref<PackedScene> &scene) {
	Node *inst = scene->instantiate();
	if (!inst) return nullptr;
	bake_root->add_child(inst);
	if (Node3D *n3 = Object::cast_to<Node3D>(inst)) {
		n3->set_position(Vector3(0, -1000, 0));
	}
	rtso::ModelLibrary::apply_imported_material_fixups(inst);
	rtso::ModelLibrary::apply_clip_tools(inst, StringName("Idle"));
	return inst;
}

void RtsoWorldView::ensure_crowd_frames(uint8_t civ, uint16_t key, const int *counts) {
	CrowdBatch &b = crowd_batch(civ, key);
	const uint8_t utype = crowd_key_utype_[key < CROWD_KEY_COUNT ? key : 0];
	if (b.no_scene) return;
	bool needed[CROWD_CLIP_COUNT] = {};
	bool any = false;
	for (int c = 0; c < CROWD_CLIP_COUNT; ++c) {
		needed[c] = counts[c] > 0 && !b.clip_baked[c];
		any = any || needed[c];
	}
	if (!any) return;

	auto entry = rtso::ModelLibrary::get("units", crowd_key_model(civ, key));
	if (!entry.scene.is_valid()) {
		b.no_scene = true;
		return;
	}
	if (!crowd_bake_root) {
		crowd_bake_root = memnew(Node3D);
		crowd_bake_root->set_visible(false);
		add_child(crowd_bake_root);
	}
	Node *inst = instantiate_for_bake(crowd_bake_root, entry.scene);
	if (!inst) return;
	const bool female = unit_is_female(utype);
	ensure_runtime_clips(find_anim_player(inst), female);
	bake_puppet_clips(inst, b.puppet, b.clip_baked, kCrowdClips, needed, CROWD_CLIP_COUNT, CROWD_DEATH_SLOTS, female);
	for (int c = 0; c < CROWD_CLIP_COUNT; ++c) {
		if (needed[c] && b.mm[c].is_valid() && b.puppet[c].posed_frames[0].is_valid())
			b.mm[c]->set_mesh(b.puppet[c].posed_frames[0]);
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
	Node *inst = instantiate_for_bake(crowd_bake_root, entry.scene);
	if (!inst) return;
	// Animals flee at a run when they have one.
	AnimationPlayer *ap = find_anim_player(inst);
	uint8_t want[FAUNA_CLIP_COUNT] = { CLIP_IDLE, CLIP_WALK, CLIP_ATTACK, CLIP_DEATH };
	if (ap && ap->has_animation(clip_name(CLIP_RUN))) want[1] = CLIP_RUN;
	const bool needed[FAUNA_CLIP_COUNT] = { true, true, true, true };
	bool done[FAUNA_CLIP_COUNT] = {};
	bake_puppet_clips(inst, fauna_puppet[ftype], done, want, needed, FAUNA_CLIP_COUNT, 1u << 3);
	for (int c = 0; c < FAUNA_CLIP_COUNT; ++c) {
		if (fauna_mm[ftype][c].is_valid() && fauna_puppet[ftype][c].posed_frames[0].is_valid()) {
			fauna_mm[ftype][c]->set_mesh(fauna_puppet[ftype][c].posed_frames[0]);
		}
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

// Biped skeletons (46 bones) share 0 A.D.'s gatherer and promotion clips;
// art/variants/biped/{,female_}carry_*.xml and infantry promotion.
void RtsoWorldView::ensure_runtime_clips(AnimationPlayer *ap, bool female) {
	if (!ap) return;
	Ref<AnimationLibrary> lib = ap->get_animation_library(StringName());
	if (!lib.is_valid() || !clip_libs_done_.insert((int64_t)lib->get_instance_id()).second) return;
	// The bone tracks of a shipped clip give the skeleton's path from the player's root.
	String skel_path;
	Ref<Animation> ref = lib->has_animation(clip_name(CLIP_IDLE)) ? lib->get_animation(clip_name(CLIP_IDLE)) : Ref<Animation>();
	for (int t = 0; ref.is_valid() && t < ref->get_track_count(); ++t) {
		NodePath tp = ref->track_get_path(t);
		if (tp.get_subname_count() > 0) {
			skel_path = tp.get_concatenated_names();
			break;
		}
	}
	Node *anim_root = ap->get_node_or_null(ap->get_root_node());
	Skeleton3D *sk = (anim_root && !skel_path.is_empty())
			? Object::cast_to<Skeleton3D>(anim_root->get_node_or_null(NodePath(skel_path))) : nullptr;
	if (!sk) return;
	struct Clip { ClipId id; const char *male; const char *female; };
	static const Clip kClips[] = {
		{ CLIP_CARRY_FOOD, "biped_gatherer_carry_grain_m", "biped_gatherer_carry_grain_f" },
		{ CLIP_CARRY_MEAT, "biped_gatherer_carry_meat_m", "biped_gatherer_carry_meat_f" },
		{ CLIP_CARRY_WOOD, "carry_wood", "biped_gatherer_carry_wood_f" },
		{ CLIP_CARRY_STONE, "carry_ore", "biped_gatherer_carry_ore_f" },
		{ CLIP_CARRY_METAL, "carry_ore", "biped_gatherer_carry_ore_f" },
		{ CLIP_PROMOTION, "biped_infantry_promotion", "biped_infantry_promotion" },
	};
	const int bones = sk->get_bone_count();
	for (const Clip &c : kClips) {
		if (lib->has_animation(clip_name(c.id))) continue;
		rtso::PsaAnim psa;
		if (!psa.load_psa(female ? c.female : c.male) || !psa.is_valid() || (int)psa.numBones != bones) continue;
		Ref<Animation> a;
		a.instantiate();
		const double dt = psa.frameTime / 1000.0;
		a->set_length(dt * psa.numFrames);
		// PSA keys are root-relative bone transforms, the same space as the
		// extractor's flat bone nodes, so they are the bone poses as they stand.
		for (int b = 0; b < bones; ++b) {
			NodePath path(skel_path + ":" + sk->get_bone_name(b));
			const int tp = a->add_track(Animation::TYPE_POSITION_3D);
			a->track_set_path(tp, path);
			const int tr = a->add_track(Animation::TYPE_ROTATION_3D);
			a->track_set_path(tr, path);
			for (uint32_t f = 0; f < psa.numFrames; ++f) {
				const rtso::PsaKey &k = psa.keys[(size_t)f * psa.numBones + b];
				a->position_track_insert_key(tp, dt * f, k.t);
				a->rotation_track_insert_key(tr, dt * f, k.q);
			}
		}
		lib->add_animation(clip_name(c.id), a);
	}
}

Node3D *RtsoWorldView::attach_carry_prop(Node *model, uint8_t clip, bool female) {
	Skeleton3D *sk = find_skeleton3d(model);
	detach_carry_props(sk);
	if (!sk || !is_carry_variant((ClipId)clip)) return nullptr;
	// carry_food: shuttle_basket_back on "back", a prop point under chest
	// (f_dress.dae prop-back, in the unit GLBs' axes); meat: one slab in each
	// hand for men, one for women; wood, stone, metal: weapon_R.
	struct Prop { int scene; const char *bone; bool back; };
	Prop props[2] = { { -1, "weapon_R", false }, { -1, "weapon_L", false } };
	switch ((ClipId)clip) {
		case CLIP_CARRY_FOOD: props[0] = { 0, "chest", true }; break;
		case CLIP_CARRY_MEAT:
			props[0].scene = female ? 6 : 1;
			if (!female) props[1].scene = 5;
			break;
		case CLIP_CARRY_WOOD: props[0].scene = 2; break;
		case CLIP_CARRY_STONE: props[0].scene = 3; break;
		case CLIP_CARRY_METAL: props[0].scene = 4; break;
		default: return nullptr;
	}
	static const char *kFiles[7] = {
		"carry_food", "carry_meat_r", "carry_wood", "carry_stone", "carry_metal", "carry_meat_l", "carry_meat_f",
	};
	Node3D *first = nullptr;
	for (const Prop &p : props) {
		if (p.scene < 0) continue;
		const int bone = sk->find_bone(p.bone);
		if (bone < 0) continue;
		Ref<PackedScene> &scene = carry_scenes_[p.scene];
		if (!scene.is_valid()) {
			scene = ResourceLoader::get_singleton()->load(String("res://assets/models/props/") + kFiles[p.scene] + ".glb");
			if (!scene.is_valid()) continue;
		}
		Node3D *prop = Object::cast_to<Node3D>(scene->instantiate());
		if (!prop) continue;
		rtso::ModelLibrary::apply_imported_material_fixups(prop);
		enable_mesh_shadows(prop);
		if (p.back) {
			prop->set_transform(Transform3D(
					Basis(Vector3(1, 0, 0), Vector3(0, 0.1222f, 0.9925f), Vector3(0, -0.9925f, 0.1222f)),
					Vector3(0, -0.3333f, 0.3811f)));
		}
		BoneAttachment3D *att = memnew(BoneAttachment3D);
		att->set_name(first ? "rtso_carry_l" : "rtso_carry");
		sk->add_child(att);
		att->set_bone_name(p.bone);
		att->set_bone_idx(bone);
		att->add_child(prop);
		if (!first) first = att;
	}
	return first;
}

// Tree models per biome, from the 0 A.D. flora each biome's random maps
// plant (maps/random/rmbiome/*.json forest templates). Scales bring the wide
// savanna trees down and the shorter oak up to the height of the other trees.
void RtsoWorldView::pick_tree_kinds(const std::string &biome) {
	struct Kind { const char *id; float scale; };
	struct Set { const char *biome; Kind kinds[TREE_KINDS]; };
	static const Set kSets[] = {
		{ "mediterranean", { { "tree", 1.0f }, { "pine", 1.0f }, { "oak", 1.15f } } },
		{ "aegean", { { "tree", 1.0f }, { "pine", 1.0f }, { "palm", 1.0f } } },
		{ "latium", { { "pine", 1.0f }, { "tree", 1.0f }, { "oak", 1.15f } } },
		{ "alpine", { { "pine", 1.0f }, { "pine", 0.85f }, { "tree", 1.0f } } },
		{ "polar", { { "pine", 1.0f }, { "pine", 0.8f }, { nullptr, 1.0f } } },
		{ "temperate", { { "oak", 1.15f }, { "tree", 1.0f }, { "pine", 1.0f } } },
		{ "steppe", { { "pine", 1.0f }, { "tree", 0.9f }, { nullptr, 1.0f } } },
		{ "savanna", { { "baobab", 0.45f }, { "acacia", 0.6f }, { "acacia", 0.5f } } },
		{ "oasis", { { "palm", 1.0f }, { "palm", 1.2f }, { "acacia", 0.5f } } },
		{ "desert", { { "palm", 1.0f }, { "acacia", 0.5f }, { nullptr, 1.0f } } },
		{ "persia", { { "palm", 1.0f }, { "acacia", 0.5f }, { "tree", 0.9f } } },
		{ "india", { { "palm", 1.0f }, { "bamboo", 1.0f }, { "tree", 1.0f } } },
		{ "tropical", { { "palm", 1.0f }, { "bamboo", 1.0f }, { "palm", 1.2f } } },
		{ "volcanic", { { "pine", 1.0f }, { "tree", 0.9f }, { nullptr, 1.0f } } },
	};
	const Set *set = &kSets[0];
	for (const auto &s : kSets)
		if (biome == s.biome) { set = &s; break; }
	tree_biome_ = biome;
	tree_kind_count = 0;
	for (int k = 0; k < TREE_KINDS; ++k) {
		const Kind &kd = set->kinds[k];
		if (!kd.id) break;
		auto e = rtso::ModelLibrary::get("vegetation", kd.id);
		if (e.mesh.is_null()) continue;
		enable_instance_tint(e.mesh);
		const int slot = tree_kind_count++;
		tree_mm[slot]->set_instance_count(0);
		tree_mm[slot]->set_mesh(wind_mesh(e.mesh));
		tree_scale[slot] = kd.scale;
		tree_y[slot] = e.y_offset * kd.scale;
	}
	if (tree_kind_count == 0) {
		tree_kind_count = 1;
		tree_scale[0] = 1.0f;
		tree_y[0] = rtso::ModelLibrary::get("vegetation", "tree").y_offset;
	}
	for (int k = tree_kind_count; k < TREE_KINDS; ++k) tree_mm[k]->set_instance_count(0);
}

// 0 A.D. random maps paint forest floor under every forest and dirt round
// every mine (rmgen createForests / createMines tile painting). RTS0 maps carry
// no tile textures, so the view paints the same from where the map put them.
void RtsoWorldView::build_ground_paint(const rtso::Simulation *sim) {
	Ref<ShaderMaterial> tm = terrain ? Ref<ShaderMaterial>(terrain->get_material_override()) : Ref<ShaderMaterial>();
	if (tm.is_null()) return;
	const float half = sim->map_half();
	const int res = std::clamp(sim->terrain_res() > 1 ? sim->terrain_res() : 128, 32, 1024);
	if (half <= 0.0f) return;
	const float cell = (half * 2.0f) / (float)res;
	std::vector<float> forest((size_t)res * res, 0.0f), mine((size_t)res * res, 0.0f);
	auto splat = [&](std::vector<float> &dst, float x, float z, float radius) {
		const int cx = (int)((x + half) / cell), cz = (int)((z + half) / cell);
		const int r = (int)std::ceil(radius / cell);
		for (int dz = -r; dz <= r; ++dz) {
			const int zz = cz + dz;
			if (zz < 0 || zz >= res) continue;
			for (int dx = -r; dx <= r; ++dx) {
				const int xx = cx + dx;
				if (xx < 0 || xx >= res) continue;
				const float d = std::sqrt((float)(dx * dx + dz * dz)) * cell;
				const float w = 1.0f - d / radius;
				float &v = dst[(size_t)zz * res + xx];
				if (w > v) v = w;
			}
		}
	};
	const auto rv = sim->resources();
	for (size_t i = 0; i < rv.count; ++i) {
		if (!rv.alive[i]) continue;
		const uint8_t sub = rv.subtype ? rv.subtype[i] : 0;
		if (rv.type[i] == rtso::RES_WOOD) splat(forest, rv.x[i], rv.z[i], 3.0f);
		else if ((rv.type[i] == rtso::RES_GOLD || rv.type[i] == rtso::RES_STONE) && sub != rtso::RES_SUB_FISH)
			splat(mine, rv.x[i], rv.z[i], 2.6f);
	}
	PackedByteArray bytes;
	bytes.resize((int64_t)res * res * 3);
	uint8_t *p = bytes.ptrw();
	for (size_t k = 0; k < (size_t)res * res; ++k) {
		p[k * 3 + 0] = 0;
		p[k * 3 + 1] = (uint8_t)std::lround(std::min(1.0f, forest[k] * 1.6f) * 255.0f);
		p[k * 3 + 2] = (uint8_t)std::lround(std::min(1.0f, mine[k] * 1.6f) * 255.0f);
	}
	Ref<Image> img = Image::create_from_data(res, res, false, Image::FORMAT_RGB8, bytes);
	paint_tex_ = ImageTexture::create_from_image(img);
	tm->set_shader_parameter("paint_tex", paint_tex_);
	tm->set_shader_parameter("paint_half", half);
	paint_dirty_ = false;
}

// Per-map sun, ambient light, fog, sky and water, from the Environment block
// of the 0 A.D. random-map biome each RTS0 biome follows
// (maps/random/rmbiome/generic/*.json, named after each row).
void RtsoWorldView::apply_biome_environment(const std::string &biome) {
	struct Rgb { float r, g, b; };
	struct Env {
		const char *biome;
		const char *sky; // SkySet
		Rgb sun;
		float sun_elevation, sun_rotation; // radians
		Rgb ambient, water_color, water_tint;
		float murkiness;
		Rgb fog;
	};
	static const Env kEnvs[] = {
		{ "mediterranean", "default", { 1.023f, 0.923f, 0.714f }, 0.800f, -0.909f, { 0.400f, 0.400f, 0.400f }, { 0.024f, 0.212f, 0.212f }, { 0.133f, 0.725f, 0.855f }, 0.80f, { 0.800f, 0.850f, 0.900f } }, // aegean
		{ "aegean", "default", { 1.023f, 0.923f, 0.714f }, 0.800f, -0.909f, { 0.400f, 0.400f, 0.400f }, { 0.024f, 0.212f, 0.212f }, { 0.133f, 0.725f, 0.855f }, 0.80f, { 0.800f, 0.850f, 0.900f } }, // aegean
		{ "latium", "default", { 1.032f, 0.990f, 0.866f }, 0.846f, -0.488f, { 0.365f, 0.376f, 0.420f }, { 0.420f, 0.365f, 0.314f }, { 0.502f, 0.502f, 0.247f }, 0.97f, { 0.894f, 0.894f, 0.894f } }, // temperate
		{ "alpine", "cloudless", { 1.019f, 1.040f, 0.973f }, 0.589f, -1.443f, { 0.390f, 0.374f, 0.524f }, { 0.078f, 0.247f, 0.420f }, { 0.471f, 1.000f, 0.502f }, 0.88f, { 0.800f, 0.800f, 0.900f } }, // alpine
		{ "oasis", "default", { 0.976f, 0.859f, 0.749f }, 0.650f, -0.300f, { 0.231f, 0.282f, 0.357f }, { 0.376f, 0.267f, 0.122f }, { 0.502f, 0.502f, 0.000f }, 0.90f, { 0.992f, 0.949f, 0.839f } }, // sahara
		{ "temperate", "default", { 1.032f, 0.990f, 0.866f }, 0.846f, -0.488f, { 0.365f, 0.376f, 0.420f }, { 0.420f, 0.365f, 0.314f }, { 0.502f, 0.502f, 0.247f }, 0.97f, { 0.894f, 0.894f, 0.894f } }, // temperate
		{ "steppe", "sunrise", { 0.859f, 0.851f, 0.733f }, 0.421f, -1.526f, { 0.504f, 0.462f, 0.604f }, { 0.420f, 0.365f, 0.344f }, { 0.502f, 0.502f, 0.307f }, 0.97f, { 0.800f, 0.850f, 0.900f } }, // steppe
		{ "savanna", "cloudless", { 1.246f, 1.109f, 0.989f }, 0.600f, -0.450f, { 0.351f, 0.402f, 0.477f }, { 0.060f, 0.205f, 0.224f }, { 0.202f, 0.302f, 0.047f }, 0.92f, { 0.847f, 0.737f, 0.482f } }, // savanna
		{ "polar", "stormy", { 0.980f, 1.010f, 1.045f }, 0.502f, -0.626f, { 0.294f, 0.392f, 0.494f }, { 0.078f, 0.247f, 0.420f }, { 0.471f, 1.000f, 0.502f }, 0.88f, { 0.784f, 0.886f, 0.976f } }, // arctic
		{ "india", "default", { 1.032f, 0.995f, 0.866f }, 1.950f, 0.000f, { 0.365f, 0.376f, 0.420f }, { 0.584f, 0.824f, 0.929f }, { 0.569f, 0.965f, 0.945f }, 0.45f, { 0.875f, 0.875f, 0.875f } }, // india
		{ "desert", "default", { 0.976f, 0.859f, 0.749f }, 0.650f, -0.300f, { 0.231f, 0.282f, 0.357f }, { 0.376f, 0.267f, 0.122f }, { 0.502f, 0.502f, 0.000f }, 0.90f, { 0.992f, 0.949f, 0.839f } }, // sahara
		{ "persia", "default", { 1.100f, 1.060f, 0.992f }, 0.700f, -1.700f, { 0.354f, 0.362f, 0.389f }, { 0.337f, 0.247f, 0.078f }, { 0.337f, 0.247f, 0.078f }, 0.96f, { 0.929f, 0.875f, 0.839f } }, // nubia
		{ "volcanic", "default", { 1.032f, 0.990f, 0.866f }, 0.446f, -0.488f, { 0.365f, 0.376f, 0.420f }, { 0.420f, 0.365f, 0.314f }, { 0.502f, 0.502f, 0.247f }, 0.97f, { 0.894f, 0.894f, 0.894f } }, // autumn
		{ "tropical", "default", { 1.032f, 0.995f, 0.866f }, 1.950f, 0.000f, { 0.365f, 0.376f, 0.420f }, { 0.584f, 0.824f, 0.929f }, { 0.569f, 0.965f, 0.945f }, 0.45f, { 0.875f, 0.875f, 0.875f } }, // india
	};
	const Env *e = &kEnvs[0];
	for (const auto &x : kEnvs)
		if (biome == x.biome) { e = &x; break; }
	auto col = [](const Rgb &c, float k = 1.0f) { return Color(c.r * k, c.g * k, c.b * k, 1.0f); };

	Node *scene = get_parent();
	if (scene) {
		for (int i = 0; i < scene->get_child_count(); ++i) {
			if (DirectionalLight3D *sun = Object::cast_to<DirectionalLight3D>(scene->get_child(i))) {
				// 0 A.D. gives the sun's height above the horizon and its bearing;
				// above 90 degrees it shines from the other side. Bearings are taken
				// relative to the Aegean sun, which matches the scene's default light.
				float elev = e->sun_elevation, rot = e->sun_rotation;
				if (elev > Math_PI * 0.5f) {
					elev = (float)Math_PI - elev;
					rot += (float)Math_PI;
				}
				elev = Math::clamp(elev, 0.35f, 1.3f);
				const float peak = std::max(e->sun.r, std::max(e->sun.g, e->sun.b));
				sun->set_color(col(e->sun, 1.0f / peak));
				sun->set_param(Light3D::PARAM_ENERGY, 1.25f * peak);
				sun->set_rotation(Vector3(-elev, Math::deg_to_rad(45.0f) + (rot - (-0.909f)), 0.0f));
			} else if (WorldEnvironment *we = Object::cast_to<WorldEnvironment>(scene->get_child(i))) {
				Ref<Environment> env = we->get_environment();
				if (env.is_null()) continue;
				if (!env_base_.is_valid()) env_base_ = env->duplicate(true);
				env = env_base_->duplicate(true);
				we->set_environment(env);
				// AmbientColor 0.4 grey is 0 A.D.'s neutral; scale to the scene's level.
				env->set_ambient_light_color(col(e->ambient, 0.85f / 0.4f));
				env->set_fog_light_color(col(e->fog));
				Ref<Sky> sky = env->get_sky();
				Ref<ProceduralSkyMaterial> psm = sky.is_valid() ? Ref<ProceduralSkyMaterial>(sky->get_material()) : Ref<ProceduralSkyMaterial>();
				if (psm.is_valid()) {
					const Color fog = col(e->fog);
					psm->set_sky_horizon_color(psm->get_sky_horizon_color().lerp(fog, 0.5f));
					if (std::strcmp(e->sky, "stormy") == 0) {
						psm->set_sky_top_color(Color(0.38f, 0.45f, 0.55f));
						psm->set_sky_horizon_color(Color(0.66f, 0.70f, 0.75f));
					} else if (std::strcmp(e->sky, "sunrise") == 0) {
						psm->set_sky_horizon_color(Color(0.95f, 0.78f, 0.60f));
					} else if (std::strcmp(e->sky, "cloudless") == 0) {
						psm->set_sky_top_color(Color(0.16f, 0.45f, 0.86f));
					}
				}
			}
		}
	}
	if (water_mesh_node) {
		Ref<ShaderMaterial> wsm = water_mesh_node->get_material_override();
		if (wsm.is_valid()) {
			const Color deep = col(e->water_color);
			const Color tint = col(e->water_tint);
			wsm->set_shader_parameter("deep_color", Color(deep.r, deep.g, deep.b, 0.94f));
			const Color shallow = deep.lerp(tint, 0.5f);
			wsm->set_shader_parameter("shallow_color", Color(shallow.r, shallow.g, shallow.b, 0.78f));
			wsm->set_shader_parameter("murkiness", e->murkiness);
		}
	}
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
	if (Ref<ShaderMaterial> tm = terrain->get_material_override(); tm.is_valid())
		tm->set_shader_parameter("water_level", sim->water_height() > -0.5f ? sim->water_height() : -1000.0f);
	paint_dirty_ = true;

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
		// Seabed heights for depth-tinted water and shore foam.
		Ref<ShaderMaterial> wsm = water_mesh_node->get_material_override();
		if (wsm.is_valid() && !heights.empty() && res > 1) {
			PackedByteArray bytes;
			bytes.resize((int64_t)res * res * sizeof(float));
			std::memcpy(bytes.ptrw(), heights.data(), (size_t)res * res * sizeof(float));
			Ref<Image> him = Image::create_from_data(res, res, false, Image::FORMAT_RF, bytes);
			water_height_tex_ = ImageTexture::create_from_image(him);
			wsm->set_shader_parameter("height_tex", water_height_tex_);
			// Height samples sit on the cell corners; map them across the full extent.
			wsm->set_shader_parameter("terrain_half", half * (float)res / (float)(res - 1));
			wsm->set_shader_parameter("water_level", sim->water_height());
		}
		water_mesh_node->set_visible(true);
	} else if (water_mesh_node) {
		water_mesh_node->set_visible(false);
	}
	apply_biome_environment(sim->current_map_biome());
}

void RtsoWorldView::sync(const rtso::Simulation *sim, float delta, const PackedInt32Array &selected_ids) {
	if (!sim) return;
	if (delta < 0.0f) delta = 0.0f;
	if (delta > 0.05f) delta = 0.05f;
	anim_time += delta;
	unit_vis_stamp++;
	// Map triggers (Flood, Extinct Volcano) raise the water during the match.
	if (water_mesh_node && water_mesh_node->is_visible() &&
	    water_mesh_node->get_position().y != sim->water_height()) {
		water_mesh_node->set_position(Vector3(0.0f, sim->water_height(), 0.0f));
	}
	if (unit_vis_stamp == 0) unit_vis_stamp = 1;

	update_fog(sim, delta);
	if (paint_dirty_ && sim->resources().count > 0) build_ground_paint(sim);
	update_territory_borders(sim);

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

	// =========================================================================
	// 1. Units Presentation: Real 0 A.D. models, 3D animations, and Health Bars
	// =========================================================================
	auto uview = sim->units();
	auto rview_g = sim->resources();
	auto entity_visible = [sim, this](float x, float z, uint8_t team) {
		return rtso::los_sees_unit(*sim, local_player_, x, z, team);
	};
	std::unordered_map<uint32_t, uint8_t> res_sub;
	res_sub.reserve(rview_g.count);
	for (size_t r = 0; r < rview_g.count; ++r) {
		if (rview_g.subtype && rview_g.subtype[r])
			res_sub[rview_g.id[r]] = rview_g.subtype[r];
	}

	std::unordered_set<uint32_t> detailed;
	// Ships always need the real GLB (sail clips + oar nodes). Everyone else is
	// crowd, which bakes the clip each unit plays (gather, build, carry...).
	for (size_t i = 0; i < uview.count; ++i) {
		if (rtso::unit_is_ship(uview.unit_type[i]))
			detailed.insert(uview.id[i]);
	}
	for (size_t i = 0; i < uview.count && (int)detailed.size() < MAX_DETAILED_UNITS; ++i) {
		bool dying = !uview.alive[i] || uview.action_state[i] == rtso::ACTION_DYING;
		if (dying)
			detailed.insert(uview.id[i]);
	}
	for (int s = 0; s < selected_ids.size() && (int)detailed.size() < MAX_DETAILED_UNITS; ++s) {
		detailed.insert((uint32_t)selected_ids[s]);
	}

	// The clip each visible unit plays (0 A.D. VisualActor: the real clip for
	// every unit, not just the nearby ones), and how many share each batch.
	auto anim_of = [&](size_t i) -> WantAnim {
		uint8_t ut = uview.unit_type[i];
		if (ut >= rtso::UNIT_TYPE_COUNT) ut = rtso::UNIT_VILLAGER;
		const float vx = uview.vel_x[i], vz = uview.vel_z[i];
		uint8_t sub = 0;
		if (uview.target_resource_id && uview.target_resource_id[i]) {
			auto it = res_sub.find(uview.target_resource_id[i]);
			if (it != res_sub.end()) sub = it->second;
		}
		WantAnim w = resolve_unit_anim(uview.action_state[i], vx * vx + vz * vz, uview.alive[i] != 0,
				uview.carry[i], ut, uview.res_type_carried[i], sub, sim->unit_capturing(i));
		return with_variant(w, uview.id[i]);
	};
	crowd_slot_scratch_.assign(uview.count, 0xFF);
	static int counts[rtso::CIV_COUNT][CROWD_KEY_COUNT][CROWD_CLIP_COUNT];
	std::memset(counts, 0, sizeof(counts));
	for (size_t i = 0; i < uview.count; ++i) {
		if (!entity_visible(uview.pos_x[i], uview.pos_z[i], uview.team[i])) continue;
		uint8_t t = uview.unit_type[i];
		if (t >= rtso::UNIT_TYPE_COUNT) t = rtso::UNIT_VILLAGER;
		uint8_t civ = sim->team_civ(uview.team[i]);
		if (civ >= rtso::CIV_COUNT) civ = rtso::CIV_ATHENIANS;
		const int slot = crowd_slot_of(anim_of(i).clip);
		crowd_slot_scratch_[i] = (uint8_t)slot;
		const uint16_t key = crowd_key_of(civ, t, uview.pack_state[i], uview.tmpl[i]);
		crowd_key_utype_[key] = t;
		counts[civ][key][slot]++;
	}
	for (int civ = 0; civ < rtso::CIV_COUNT; ++civ) {
		for (int t = 0; t < CROWD_KEY_COUNT; ++t) {
			int any = 0;
			for (int c = 0; c < CROWD_CLIP_COUNT; ++c) any += counts[civ][t][c];
			if (any == 0 && !crowd_[civ][t]) continue;
			if (any > 0) {
				CrowdBatch &cb = crowd_batch((uint8_t)civ, (uint16_t)t);
				ensure_crowd_frames((uint8_t)civ, (uint16_t)t, counts[civ][t]);
				for (int c = 0; c < CROWD_CLIP_COUNT; ++c)
					if (counts[civ][t][c] > 0) crowd_mm(cb, c);
			}
			CrowdBatch &cb = *crowd_[civ][t];
			cycle_puppet_meshes(cb.puppet, cb.mm, counts[civ][t], CROWD_CLIP_COUNT, CROWD_DEATH_SLOTS);
			for (int c = 0; c < CROWD_CLIP_COUNT; ++c)
				if (cb.mm[c].is_valid()) cb.mm[c]->set_instance_count(counts[civ][t][c]);
		}
	}
	static int written[rtso::CIV_COUNT][CROWD_KEY_COUNT][CROWD_CLIP_COUNT];
	std::memset(written, 0, sizeof(written));

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

		WantAnim want = anim_of(i);
		anim_controllers[utype].play(want.state);

		uint8_t uciv = sim->team_civ(uview.team[i]);
		if (uciv >= rtso::CIV_COUNT) uciv = rtso::CIV_ATHENIANS;
		const uint16_t ckey = crowd_key_of(uciv, utype, uview.pack_state[i], uview.tmpl[i]);
		String utype_str = crowd_key_model(uciv, ckey);
		String cat_units3 = "units";
		auto entry = rtso::ModelLibrary::get(cat_units3, utype_str);
		const float turret_h = sim->unit_height_offset(i); // turret points stand on walls
		float y = (turret_h > 0.0f ? turret_draw_y(sim, id, turret_h, x, z) : sim->get_terrain_height(x, z)) + entry.y_offset;
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
		const int clip = crowd_slot_scratch_[i];
		MultiMesh *cmm = crowd_[uciv][ckey] ? crowd_[uciv][ckey]->mm[clip].ptr() : nullptr;
		if (cmm) {
			const int slot = written[uciv][ckey][clip]++;
			if (detailed.count(id) > 0) {
				Transform3D hidden;
				hidden.origin = Vector3(x, y, z);
				hidden.basis = hidden.basis.scaled(Vector3(0.001f, 0.001f, 0.001f));
				cmm->set_instance_transform(slot, hidden);
			} else {
				cmm->set_instance_transform(slot, xf);
			}
			// model_common.fs: color *= LOS. Own units in the fog read darker.
			// Models without player_trans skins keep a friend/foe tint.
			const float lit = los_brightness(x, z);
			const bool owner_coloured = crowd_[uciv][ckey]->player_color;
			Color tint = owner_coloured ? Color(1, 1, 1) : (is_player ? col_player : col_enemy);
			tint.r *= lit;
			tint.g *= lit;
			tint.b *= lit;
			cmm->set_instance_color(slot, tint);
			// Custom data reaches the shader raw; the close-up path's source_color
			// instance uniform is converted to linear, so match it here.
			const Color pc = player_display_color(sim, uview.team[i]).srgb_to_linear();
			cmm->set_instance_custom_data(slot, Color(pc.r, pc.g, pc.b, 1.0f));
		}

		if (detailed.count(id) == 0) continue;

		UnitVis &v = acquire_unit_slot(id);
		v.vis_stamp = unit_vis_stamp;
		v.yaw = yaw;
		attach_unit_model(v, utype_str);
		push_player_color(v.model, v.player_rgba, player_display_color(sim, uview.team[i]));
		if (v.anim && UtilityFunctions::is_instance_valid(v.anim))
			ensure_runtime_clips(v.anim, unit_is_female(utype));

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
			const uint8_t prop_clip = is_carry_variant(play) ? (uint8_t)play : 0;
			if (prop_clip != v.carry_clip) {
				v.carry_prop = prop_clip ? attach_carry_prop(v.model, prop_clip, unit_is_female(utype))
						: (detach_carry_props(find_skeleton3d(v.model)), nullptr);
				v.carry_clip = prop_clip;
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

		// Selected units, or every visible one while Tab is toggled (0 A.D. g_ShowAllStatusBars).
		bool show_hp = (is_selected || show_all_status_bars_) && uview.alive[i];
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
		const String mid = building_visual_id(bciv, b.type, b.half_size);
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

		// Models face -z unrotated, so the sim's 0 A.D. rotation is offset by DEFAULT_BUILDING_ROT.
		v.root->set_transform(Transform3D(Basis(Vector3(0, 1, 0), b.rot - rtso::Simulation::DEFAULT_BUILDING_ROT),
				Vector3(b.x, base_y, b.z)));
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
		push_player_color(v.root, v.player_rgba, player_display_color(sim, b.team));

		{
			const float frac = b.max_hp > 0.0f ? b.hp / b.max_hp : 1.0f;
			uint8_t lvl = 0;
			if (v.phase == 0) {
				if (frac <= 0.35f || b.is_burning) lvl = 2;
				else if (frac <= 0.65f) lvl = 1;
			}
			update_damage_fx(v, lvl, b.half_size, entry.aabb.size.y * b_scale);
		}
		update_garrison_flag(v, visual_id, v.phase == 0 && !b.miraged && sim->get_garrison_count(b.id) > 0,
				player_display_color(sim, b.team), b_scale, entry.y_offset);

		// Building Health & Construction Progress Bar
		bool show_bhp = !destroyed && (show_all_status_bars_ || b_progress < 1.0f || b.hp < b.max_hp * 0.98f);
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
			building_pool[s].fx_smoke = nullptr;
			building_pool[s].fx_flame = nullptr;
			building_pool[s].fx_level = 0;
			building_pool[s].flag = nullptr;
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
	size_t n_tree = 0, g = 0, st = 0, fd = 0, nru = 0, fish_n = 0;
	for (const auto &r : fres) {
		if (r.subtype == rtso::RES_SUB_GRAIN) continue; // a field's grain; the field model shows it
		if (r.subtype == rtso::RES_SUB_RUINS) nru++;
		else if (r.type == rtso::RES_WOOD) n_tree++;
		else if (r.type == rtso::RES_GOLD) g++;
		else if (r.type == rtso::RES_STONE) st++;
		else if (r.subtype == rtso::RES_SUB_FISH) fish_n++;
		else if (r.type == rtso::RES_FOOD) fd++;
	}

	if (sim->current_map_biome() != tree_biome_) pick_tree_kinds(sim->current_map_biome());
	size_t n_kind[TREE_KINDS] = {};
	for (const auto &r : fres)
		if (r.type == rtso::RES_WOOD && r.subtype != rtso::RES_SUB_GRAIN) n_kind[r.id % (uint32_t)tree_kind_count]++;
	for (int k = 0; k < TREE_KINDS; ++k) tree_mm[k]->set_instance_count((int)n_kind[k]);
	gold_mm->set_instance_count((int)g);
	stone_mm->set_instance_count((int)st);
	if (food_mm.is_valid())
		food_mm->set_instance_count((int)fd);
	ruins_mm->set_instance_count((int)nru);
	fish_mm->set_instance_count((int)fish_n);

	size_t ia[TREE_KINDS] = {}, ig = 0, ist = 0, ifd = 0, iru = 0, ifish = 0;
	auto e_ruins = rtso::ModelLibrary::get("buildings", "ruins");
	String cat_veg = "vegetation";
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
			const int k = (int)(h % (uint32_t)tree_kind_count);
			const float sc = tree_scale[k];
			Transform3D xf(rot.scaled(Vector3(sc, sc, sc)), Vector3(rx, ry + tree_y[k], rz));
			tree_mm[k]->set_instance_transform((int)ia[k], xf);
			tree_mm[k]->set_instance_color((int)ia[k]++, tint);
		} else if (r.type == rtso::RES_GOLD) {
			Transform3D xf(rot, Vector3(rx, ry + e_gold.y_offset, rz));
			gold_mm->set_instance_transform((int)ig, xf);
			gold_mm->set_instance_color((int)ig++, tint);
		} else if (r.type == rtso::RES_STONE) {
			Transform3D xf(rot, Vector3(rx, ry + e_stone.y_offset, rz));
			stone_mm->set_instance_transform((int)ist, xf);
			stone_mm->set_instance_color((int)ist++, tint);
		} else if (r.subtype == rtso::RES_SUB_FISH) {
			// 0 A.D.'s fish shoal swims just under the surface (Position/Floating).
			// The water has no refraction pass, so the top fish break the
			// surface to stay visible.
			ry = std::max(ry, sim->water_height());
			Transform3D xf(rot, Vector3(rx, ry + 0.3f, rz));
			fish_mm->set_instance_transform((int)ifish, xf);
			fish_mm->set_instance_color((int)ifish++, tint);
		} else if (r.type == rtso::RES_FOOD && food_mm.is_valid()) {
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

	update_birds(sim, delta);

	// Gaia scenery: the 0 A.D. actor mesh (metres, 0 A.D. axes, origin on the
	// ground) scaled like its obstruction and turned by its map orientation
	// (Basis about y by `angle` takes local x to (cos, -sin), CCmpObstruction's
	// u axis). A type without a mesh is a box the size of its obstruction.
	{
		const auto &sv = sim->scenery();
		int ns = 0;
		for (auto &kinds : scenery_variants_)
			for (auto &k : kinds) k.count = 0;
		for (const auto &sc : sv) {
			if (los_brightness(sc.x, sc.z) <= 0.0f) continue;
			auto &kinds = scenery_variants_[sc.type < rtso::SCENERY_TYPE_COUNT ? sc.type : 0];
			if (kinds.empty()) ++ns;
			else ++kinds[sc.id % kinds.size()].count;
		}
		scenery_mm->set_instance_count(ns);
		for (auto &kinds : scenery_variants_)
			for (auto &k : kinds) {
				k.mm->set_instance_count(k.count);
				k.count = 0;
			}
		int is = 0;
		for (const auto &sc : sv) {
			const float lit = los_brightness(sc.x, sc.z);
			if (lit <= 0.0f) continue;
			const rtso::SceneryTypeInfo &info = rtso::scenery_type_info(sc.type);
			const float scale = info.width > 0.0f ? 2.0f * sc.half_w / info.width : 0.25f;
			auto &kinds = scenery_variants_[sc.type < rtso::SCENERY_TYPE_COUNT ? sc.type : 0];
			if (!kinds.empty()) {
				SceneryVariant &k = kinds[sc.id % kinds.size()];
				const Basis b = Basis(Vector3(0, 1, 0), sc.angle).scaled_local(Vector3(scale, scale, scale));
				k.mm->set_instance_transform(k.count, Transform3D(b, Vector3(sc.x, sim->get_terrain_height(sc.x, sc.z), sc.z)));
				k.mm->set_instance_color(k.count++, Color(lit, lit, lit, 1.0f));
				continue;
			}
			const float h = std::max(0.2f, info.height * scale * (info.wooden ? 0.35f : 0.6f));
			Basis b = Basis(Vector3(0, 1, 0), sc.angle).scaled_local(Vector3(2.0f * sc.half_w, h, 2.0f * sc.half_d));
			scenery_mm->set_instance_transform(is, Transform3D(b, Vector3(sc.x, sim->get_terrain_height(sc.x, sc.z) + 0.5f * h, sc.z)));
			const Color base = info.wooden ? Color(0.45f, 0.32f, 0.2f) : Color(0.72f, 0.69f, 0.62f);
			scenery_mm->set_instance_color(is++, Color(base.r * lit, base.g * lit, base.b * lit, 1.0f));
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
	int f_counts[rtso::FAUNA_COUNT][FAUNA_CLIP_COUNT] = {};
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
		for (int c = 0; c < FAUNA_CLIP_COUNT; ++c) any += f_counts[ft][c];
		if (any > 0) ensure_fauna_frames((uint8_t)ft);
		cycle_puppet_meshes(fauna_puppet[ft], fauna_mm[ft], f_counts[ft], FAUNA_CLIP_COUNT, 1u << 3);
		for (int c = 0; c < FAUNA_CLIP_COUNT; ++c) {
			if (fauna_mm[ft][c].is_valid()) {
				fauna_mm[ft][c]->set_instance_count(f_counts[ft][c]);
			}
		}
	}
	int f_written[rtso::FAUNA_COUNT][FAUNA_CLIP_COUNT] = {};
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
