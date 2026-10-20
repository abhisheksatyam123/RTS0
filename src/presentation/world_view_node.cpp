#include <godot_cpp/classes/image_texture.hpp>
#include "world_view_node.h"
#include "display_colors.h"
#include "territory_boundary.h"
#include "terrain_blend.h"
#include "session_audio.h"
#include "user_config.h"
#include "formation_variants.h"
#include "../graphics/assets/instance_params.h"
#include "../graphics/assets/packed_siege_models.h"
#include "../animation/psa_loader.h"
#include <chrono>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/templates/hash_map.hpp>
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
#include <godot_cpp/classes/sprite3d.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/texture2d_array.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace godot;

static float drawn_water_height(const rtso::Simulation *sim);

// 0 A.D. 0.28 template_unit*.xml StatusBars, in metres: BarWidth, BarHeight, HeightOffset.
static rtso::StatusBarsSize unit_status_bars(uint8_t type, uint16_t tmpl) {
	rtso::TechClasses c = rtso::unit_tech_classes(type);
	if (tmpl < rtso::CIV_UNIT_TEMPLATE_COUNT) c = rtso::civ_unit_template(tmpl).tech_classes;
	if (rtso::unit_is_siege_engine(type)) return {nullptr, 4.0f, 0.5f, 5.0f}; // template_unit_siege
	if (type == rtso::UNIT_TRIREME || type == rtso::UNIT_MERCHANT_SHIP) return {nullptr, 6.0f, 0.5f, 6.0f}; // template_unit_ship
	if (type == rtso::UNIT_WORKER_ELEPHANT) return {nullptr, 2.0f, 0.333f, 9.0f}; // template_unit_support_elephant
	if (c & rtso::TC_ELEPHANT) return {nullptr, 2.0f, 0.333f, 10.0f}; // template_unit_champion_elephant
	if (c & rtso::TC_CAVALRY) return {nullptr, 2.0f, 0.333f, 7.0f}; // template_unit_cavalry
	if (type == rtso::UNIT_WAR_DOG) return {nullptr, 2.0f, 0.333f, 2.5f}; // template_unit_dog
	return {nullptr, 2.0f, 0.333f, 5.0f}; // template_unit (and template_unit_ship_fishing)
}

// 0 A.D. 0.28 StatusBars/HeightOffset of a structure, in metres: template_structure's 12
// unless its template sets another (towers and walls: the middle of the civs' values).
static float structure_status_bar_offset(uint8_t type) {
	switch (type) {
		case rtso::BLD_HOUSE: case rtso::BLD_APARTMENT: case rtso::BLD_KENNEL: case rtso::BLD_FIELD: return 8.0f;
		case rtso::BLD_OUTPOST: return 14.0f;
		case rtso::BLD_SENTRY_TOWER: return 18.0f;
		case rtso::BLD_TOWER: case rtso::BLD_WALL_TOWER: return 21.0f;
		case rtso::BLD_TOWER_ARTILLERY: case rtso::BLD_TOWER_BOLT: case rtso::BLD_TOWER_DOUBLE: case rtso::BLD_TOWER_GREAT: return 19.0f;
		case rtso::BLD_WALL_SEGMENT: return 12.6f;
		case rtso::BLD_GATE: return 17.5f;
		case rtso::BLD_SIEGE_WALL: return 7.7f;
		case rtso::BLD_SIEGE_WALL_GATE: return 13.5f;
		case rtso::BLD_PYRAMID_LARGE: return 15.0f;
		default: return 12.0f;
	}
}

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

// The models of one tree family, at most `max`: <id>.glb, then <id>_v2.glb, <id>_v3.glb
// ... while they exist (tools/fetch_0ad_trees.py --variants). 0 when <id> has no model.
static int tree_family_models(const char *id, rtso::ModelLibrary::Entry *out, int max) {
	int n = 0;
	for (int v = 0; v < max; ++v) {
		const String vid = v == 0 ? String(id) : String(id) + String("_v") + String::num_int64(v + 1);
		if (v > 0 && !ResourceLoader::get_singleton()->exists(String("res://assets/models/vegetation/") + vid + String(".glb")))
			break;
		const auto &e = rtso::ModelLibrary::get("vegetation", vid);
		if (e.mesh.is_null()) break;
		out[n++] = e;
	}
	return n;
}

// 0.28 variant_diversity: of an actor's n variations, "none" draws only the first,
// "limited" at most two, "full" (the default) any of them.
static int diverse_variants(int n) {
	static uint32_t gen = ~0u;
	static int cap = 1 << 30;
	if (rtso::user_config::generation() != gen) {
		gen = rtso::user_config::generation();
		const std::string v = rtso::user_config::get("variant_diversity");
		cap = v == "none" ? 1 : v == "limited" ? 2 : 1 << 30;
	}
	return std::min(n, cap);
}

// Which of a family's n models a tree draws: a hash of its id, apart from the
// kind (id % kinds) and the rotation (id % 12) so variants mix within every kind.
static int tree_variant_of(uint32_t id, int n) {
	n = diverse_variants(n);
	return n > 1 ? (int)(((id * 2654435761u) >> 16) % (uint32_t)n) : 0;
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



		v.hp_bg = memnew(MeshInstance3D);
		v.hp_bg->set_mesh(hp_bg_mesh);
		v.hp_bg->set_material_override(hp_bg_mat);
		add_child(v.hp_bg);

		v.hp_fg = memnew(MeshInstance3D);
		v.hp_fg->set_mesh(hp_fg_mesh);
		v.hp_fg->set_material_override(hp_green_mat);
		add_child(v.hp_fg);

		v.xp_bg = memnew(MeshInstance3D);
		v.xp_bg->set_mesh(xp_bg_mesh);
		v.xp_bg->set_material_override(hp_bg_mat);
		add_child(v.xp_bg);

		v.xp_fg = memnew(MeshInstance3D);
		v.xp_fg->set_mesh(xp_fg_mesh);
		v.xp_fg->set_material_override(xp_mat);
		add_child(v.xp_fg);

		v.rank_icon = memnew(Sprite3D);
		v.rank_icon->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
		v.rank_icon->set_draw_flag(SpriteBase3D::FLAG_SHADED, false);
		v.rank_icon->set_alpha_cut_mode(SpriteBase3D::ALPHA_CUT_DISCARD);
		add_child(v.rank_icon);
	}
	UnitVis &v = unit_pool[slot];
	v.sim_id = sim_id;
	v.yaw = 0.0f;
	v.anim_debt = 0.0f;
	v.root->set_visible(true);
	v.hp_bg->set_visible(false);
	v.hp_fg->set_visible(false);
	v.xp_bg->set_visible(false);
	v.xp_fg->set_visible(false);
	v.rank_icon->set_visible(false);
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
	anim_controllers[rtso::UNIT_WAGON].load_rig(rtso::RigType::Wagon);
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
	// Selection Ring: create procedural smooth ring texture in memory so it never relies on disk import
	{
		const int sz = 256;
		Ref<Image> img = Image::create_empty(sz, sz, false, Image::FORMAT_RGBA8);
		const float center = (sz - 1) * 0.5f;
		const float radius = sz * 0.42f;
		const float half_w = sz * 0.045f;
		const float feather = 1.5f;
		for (int y = 0; y < sz; ++y) {
			for (int x = 0; x < sz; ++x) {
				const float dx = (float)x - center;
				const float dy = (float)y - center;
				const float dist = Math::sqrt(dx * dx + dy * dy);
				const float diff = Math::abs(dist - radius);
				const float a = Math::clamp((half_w - diff) / feather, 0.0f, 1.0f);
				img->set_pixel(x, y, Color(1.0f, 1.0f, 1.0f, a));
			}
		}
		ring_decal_tex_ = ImageTexture::create_from_image(img);
	}

	ring_sel_mat.instantiate();
	ring_sel_mat->set_albedo(Color(0.2f, 1.0f, 0.4f));
	ring_sel_mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	ring_sel_mat->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);

	ring_enemy_mat.instantiate();
	ring_enemy_mat->set_albedo(Color(0.92f, 0.25f, 0.2f));
	ring_enemy_mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	// selection.js g_HighlightedAlpha = 0.75: the same ring, partly transparent.
	for (Ref<StandardMaterial3D> *m : {&ring_hl_mat, &ring_enemy_hl_mat}) {
		m->instantiate();
		(*m)->set_albedo(m == &ring_hl_mat ? Color(0.2f, 1.0f, 0.4f, 0.75f) : Color(0.92f, 0.25f, 0.2f, 0.75f));
		(*m)->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		(*m)->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		(*m)->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);
	}

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

	// The experience bar: 2/3 of a health bar's height (StatusBars AddExperienceBar).
	xp_bg_mesh.instantiate();
	xp_bg_mesh->set_size(Vector3(0.9f, 0.055f, 0.04f));
	xp_fg_mesh.instantiate();
	xp_fg_mesh->set_size(Vector3(0.88f, 0.045f, 0.05f));
	xp_mat.instantiate();
	xp_mat->set_albedo(Color(0.95f, 0.75f, 0.2f, 0.95f));
	xp_mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);

	// Terrain Plane
	plane_mesh.instantiate();
	plane_mesh->set_size(Vector2(rtso::Simulation::MAP_HALF * 2.0f, rtso::Simulation::MAP_HALF * 2.0f));

	terrain = memnew(MeshInstance3D);
	terrain->set_mesh(plane_mesh);
	terrain->set_position(Vector3(0, -0.01f, 0));
	terrain->set_layer_mask(1u | DECAL_LAYER);
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
		for (int v = 0; v < TREE_VARIANTS; ++v) {
			tree_mm[k][v] = create_multimesh(tinted(rtso::ModelLibrary::get("vegetation", "tree").mesh), true);
			tree_mmi[k][v] = memnew(MultiMeshInstance3D);
			tree_mmi[k][v]->set_multimesh(tree_mm[k][v]);
			add_child(tree_mmi[k][v]);
		}
	}
	pick_tree_kinds("mediterranean");
	{
		// FloraKind -> vegetation model (tools/fetch_0ad_trees.py ports one per 0 A.D. species
		// family). Bushes and saplings are small leafy trees; a date fruit supply is a short
		// date palm. Scales as in pick_tree_kinds.
		static_assert(FLORA_KINDS == rtso::FLORA_COUNT, "one batch per FloraKind");
		static_assert(rtso::FLORA_FAMILY_COUNT == 27, "kKinds lists the families");
		struct Kind { const char *id; float scale; bool tree; };
		static const Kind kKinds[rtso::FLORA_FAMILY_COUNT] = {
			{ nullptr, 1.0f, true }, { "tree", 1.0f, true }, { "pine", 1.0f, true }, { "palm", 1.0f, true },
			{ "oak", 1.15f, true }, { "baobab", 0.45f, true }, { "acacia", 0.6f, true }, { "bamboo", 1.0f, true },
			{ "tree", 0.45f, true }, { "fruit_bush", 1.0f, false }, { "apple", 1.0f, false }, { "palm", 0.7f, false },
			// fetch_0ad_trees.py keeps 0 A.D.'s metres; 0.3 brings them to the trees above
			// (a 17 m fir is 5 units), the tallest a little lower and the small cypress higher.
			{ "carob", 0.3f, true }, { "poplar", 0.3f, true }, { "fan_palm", 0.3f, true }, { "beech", 0.3f, true },
			{ "dead_tree", 0.4f, true }, { "cypress", 0.45f, true }, { "aleppo_pine", 0.3f, true }, { "fir", 0.3f, true },
			{ "tropic_bush", 0.3f, true }, { "olive", 0.3f, true }, { "banyan", 0.28f, true }, { "tropic_palm", 0.23f, true },
			{ "maple", 0.3f, true }, { "juniper", 0.3f, true }, { "teak", 0.21f, true },
		};
		for (int k = 1; k < FLORA_KINDS; ++k) {
			// A template kind draws its own 0.28 actor (extract_0ad_assets.py ZIP_FLORA keeps
			// 0 A.D.'s metres, as fetch_0ad_trees.py does); a fruit supply does not sway.
			Kind kd{ nullptr, 0.3f, true };
			if (k < rtso::FLORA_FAMILY_COUNT) kd = kKinds[k];
			else {
				const auto &t = rtso::flora_template_info(k - rtso::FLORA_TEMPLATE_FIRST);
				kd = Kind{ t.model_id, 0.3f, !t.fruit };
			}
			rtso::ModelLibrary::Entry models[TREE_VARIANTS];
			flora_variants[k] = tree_family_models(kd.id, models, TREE_VARIANTS);
			flora_scale[k] = kd.scale;
			for (int v = 0; v < flora_variants[k]; ++v) {
				const auto &e = models[v];
				enable_instance_tint(e.mesh);
				flora_mm[k][v] = create_multimesh(kd.tree ? wind_mesh(e.mesh) : e.mesh, true);
				flora_y[k][v] = e.y_offset * kd.scale;
				flora_mmi[k][v] = memnew(MultiMeshInstance3D);
				flora_mmi[k][v]->set_multimesh(flora_mm[k][v]);
				add_child(flora_mmi[k][v]);
			}
		}
	}
	for (int k = 0; k < MINE_KINDS; ++k) {
		const bool metal = k == MINE_METAL_LARGE || k == MINE_METAL_SMALL;
		mine_mm[k] = create_multimesh(tinted(rtso::ModelLibrary::get("vegetation", metal ? "gold_vein" : "stone_mine").mesh), true);
		mine_mmi[k] = memnew(MultiMeshInstance3D);
		mine_mmi[k]->set_multimesh(mine_mm[k]);
		add_child(mine_mmi[k]);
	}
	food_mm = create_multimesh(tinted(rtso::ModelLibrary::get("vegetation", "fruit_bush").mesh), true);

	food_mmi = memnew(MultiMeshInstance3D); food_mmi->set_multimesh(food_mm); add_child(food_mmi);
	ruins_mm = create_multimesh(tinted(rtso::ModelLibrary::get("buildings", "ruins").mesh), true);
	ruins_mmi = memnew(MultiMeshInstance3D); ruins_mmi->set_multimesh(ruins_mm); add_child(ruins_mmi);
	for (int k = 0; k < rtso::TREASURE_TYPE_COUNT; ++k) {
		auto e = rtso::ModelLibrary::get("props", rtso::treasure_type_info((uint8_t)k).model_id);
		if (!e.mesh.is_valid()) e = rtso::ModelLibrary::get("vegetation", "treasure_chest");
		treasure_y_offset[k] = e.y_offset;
		treasure_mm[k] = create_multimesh(tinted(e.mesh), true);
		treasure_mmi[k] = memnew(MultiMeshInstance3D);
		treasure_mmi[k]->set_multimesh(treasure_mm[k]);
		add_child(treasure_mmi[k]);
	}
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
		// Each 0 A.D. species is drawn with its RTS0 model (gaia_data.h model_id), or the deer,
		// and its other bodies (<model_id>_v2.glb ..., tools/add_0ad_mesh_looks.py) in rows after.
		for (int f = 0; f < rtso::FAUNA_COUNT; ++f) {
			const String id = rtso::fauna_type_info((uint8_t)f).model_id;
			fauna_row_model_[f] = id;
			fauna_look_first_[f] = (uint8_t)fauna_rows_;
			for (int v = 2; v <= 4 && fauna_rows_ < FAUNA_ROWS; ++v) {
				const String look = id + String("_v") + String::num_int64(v);
				if (!ResourceLoader::get_singleton()->exists(String("res://assets/models/units/") + look + String(".glb"))) break;
				fauna_row_model_[fauna_rows_++] = look;
				fauna_looks_[f]++;
			}
		}
		Ref<Mesh> fauna_rest[FAUNA_ROWS];
		for (int f = 0; f < fauna_rows_; ++f) {
			auto m = rtso::ModelLibrary::get("units", fauna_row_model_[f]);
			fauna_rest[f] = m.mesh.is_valid() ? m.mesh : deer_mesh;
		}
		for (int f = 0; f < fauna_rows_; ++f) {
			for (int c = 0; c < FAUNA_CLIP_COUNT; ++c) {
				fauna_mm[f][c] = create_multimesh(fauna_rest[f], false);
				fauna_mm[f][c]->set_use_custom_data(true); // the coat draw (player_color.gdshader variant_u)
				fauna_mmi[f][c] = memnew(MultiMeshInstance3D);
				fauna_mmi[f][c]->set_multimesh(fauna_mm[f][c]);
				if (silhouettes_ && silhouette_mat_.is_valid()) fauna_mmi[f][c]->set_material_overlay(silhouette_mat_);
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

// The player's Game Setup colour (0 A.D. PlayerColor, rtso::PLAYER_COLORS), or
// its diplomacy colour while those are on (display_colors.h).
static Color player_display_color(const rtso::Simulation *sim, uint8_t pid) {
	const uint32_t rgb = rtso::display_colors::rgb(*sim, pid);
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
	Ref<Shader> shader = rtso::load_instanced_shader("res://shaders/garrison_flag.gdshader");
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
	// Under Compatibility the flag draws its own copy of the style's material.
	if (!rtso::is_material_or_copy(v.flag->get_material_override(), flag_mats_[fp->style]))
		v.flag->set_material_override(flag_mats_[fp->style]);
	rtso::set_instance_param(v.flag, "player_color", owner_color);
	const float fs = b_scale * fp->scale;
	v.flag->set_transform(Transform3D(Basis().scaled(Vector3(fs, fs, fs)),
			Vector3(0, y_offset, 0) + fp->pos * b_scale));
	v.flag->set_visible(true);
}

void RtsoWorldView::update_territory_borders(const rtso::Simulation *sim) {
	const auto &grid = sim->territory_grid();
	if (grid.w <= 0 || grid.h <= 0) return;
	const uint64_t colors = rtso::display_colors::key(*sim);
	if (grid.data == border_grid_cache_ && border_half_ == sim->map_half() && border_colors_ == colors) return;
	border_colors_ = colors;
	border_grid_cache_ = grid.data;
	border_half_ = sim->map_half();

	if (!border_mi_) {
		Ref<Shader> sh;
		sh.instantiate();
		// A player-coloured line (0 A.D. territory_border.png is not in RTS0's assets); darkened by LOS
		// and hidden in unexplored land like 0 A.D.'s textured overlay lines.
		sh->set_code(
				"shader_type spatial;\n"
				"render_mode blend_mix, unshaded, cull_disabled, depth_draw_never, shadows_disabled, fog_disabled;\n"
				"uniform sampler2D los_tex : hint_default_white, filter_linear, repeat_disable;\n"
				"uniform float los_map_half = 0.0;\n"
				"varying vec3 wpos;\n"
				"void vertex() { wpos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz; }\n"
				"void fragment() {\n"
				// Across the line (UV.y 0..1, centred on the path): solid with soft edges.
				"  float d = abs(UV.y - 0.5) * 2.0;\n"
				"  float core = 1.0 - smoothstep(0.6, 1.0, d);\n"
				"  float dash = 0.8 + 0.2 * step(0.5, fract(UV.x));\n"
				"  float los = 1.0;\n"
				"  if (los_map_half > 0.0) los = texture(los_tex, (wpos.xz + vec2(los_map_half)) / (2.0 * los_map_half)).r;\n"
				"  ALBEDO = COLOR.rgb * mix(0.5, 1.0, los) * dash;\n"
				// CCmpTerritoryManager::Interpolate: a region cut off from its owner's
				// civic centres (COLOR.a 0) blinks, alpha * (0.2 + 0.8 |cos(t pi)|).
				"  float blink = mix(0.2 + 0.8 * abs(cos(TIME * PI)), 1.0, COLOR.a);\n"
				"  ALPHA = core * smoothstep(0.02, 0.25, los) * blink;\n"
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
	// mods/mod/simulation/data/territorymanager.xml.
	const float thickness = 0.75f * kScale;  // BorderThickness
	const float separation = 0.85f * kScale; // BorderSeparation
	// CTexturedLineRData: points on water float on its surface.
	const float water = sim->water_height() > -0.5f ? drawn_water_height(sim) : -1e9f;
	auto line_y = [&](float x, float z) { return std::max(sim->get_terrain_height(x, z), water) + 0.12f; };
	const float cell = (border_half_ * 2.0f) / (float)grid.w;

	PackedVector3Array verts;
	PackedColorArray cols;
	PackedVector2Array uvs;
	PackedInt32Array idx;
	// TERRITORY_BLINKING_MASK: cells of a region holding none of its owner's
	// territory roots get their own outline, which blinks.
	constexpr uint8_t kBlinking = 0x40;
	std::vector<uint8_t> cells(grid.data.begin(), grid.data.end());
	for (int j = 0; j < grid.h; ++j) {
		for (int i = 0; i < grid.w; ++i) {
			uint8_t &c = cells[(size_t)j * grid.w + i];
			if (c && c < kBlinking && sim->territory_blinking_cell(i, j)) c |= kBlinking;
		}
	}
	auto boundaries = rtso::compute_territory_boundaries(cells.data(), grid.w, grid.h);
	for (auto &b : boundaries) {
		if (b.points.size() < 3) continue;
		rtso::smooth_points_average(b.points);
		rtso::interpolate_points_rns(b.points, separation / cell);
		const bool blinking = (b.owner & kBlinking) != 0;
		Color col = player_display_color(sim, (uint8_t)((b.owner & ~kBlinking) - 1));
		col.a = blinking ? 0.0f : 1.0f;
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
			// CTexturedLineRData: the line is centred on the path, `thickness` to each side.
			// Owner's land is on the left of the walk: (-t.z, t.x) points inward.
			const float ox = wx + tz * thickness, oz = wz - tx * thickness;
			const float ix = wx - tz * thickness, iz = wz + tx * thickness;
			if (k > 0) {
				const rtso::BoundaryPoint &q = b.points[(k - 1) % n];
				const float dx = (p.x - q.x) * cell, dz = (p.y - q.y) * cell;
				run += Math::sqrt(dx * dx + dz * dz);
			}
			verts.push_back(Vector3(ox, line_y(ox, oz), oz));
			verts.push_back(Vector3(ix, line_y(ix, iz), iz));
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
	// 0.28 SoundManager: a fixed set of voices. Take a free player from the pool; with
	// kMaxSpatialVoices already sounding, the new sound is dropped rather than cutting
	// one off (a stop and restart costs as much as a play).
	// A voice is busy until its sound's length has passed (the audio thread updates
	// is_playing() on its own time).
	const uint64_t now = Time::get_singleton()->get_ticks_msec();
	AudioStreamPlayer3D *player = nullptr;
	size_t slot = 0;
	int playing = 0;
	for (size_t k = 0; k < SPATIAL_AUDIO_POOL_SIZE; ++k) {
		const size_t i = (next_spatial_idx + k) % SPATIAL_AUDIO_POOL_SIZE;
		if (!spatial_players[i]) continue;
		if (now < spatial_end_ms_[i]) {
			++playing;
		} else if (!player) {
			player = spatial_players[i];
			slot = i;
		}
	}
	if (playing >= kMaxSpatialVoices) {
		++spatial_dropped_;
		return;
	}
	if (player) {
		player->set_position(pos);
		next_spatial_idx = (slot + 1) % SPATIAL_AUDIO_POOL_SIZE;
		const double len = stream->get_length();
		spatial_end_ms_[slot] = now + (uint64_t)(1000.0 * (len > 0.0 ? len / std::max(0.05f, pitch_scale) : 1.0));
		if (player->get_stream() != stream) player->set_stream(stream);
		player->set_volume_db(volume_db);
		player->set_max_distance(max_dist);
		player->set_pitch_scale(pitch_scale);
		player->play();
	}
}

// Health.js PlaySound("death") of a structure: template_structure.xml's
// attack/destruction/building_collapse_large.xml (attack_sound_data.h).
static void play_structure_death(RtsoWorldView &view, uint8_t type, Vector3 pos) {
	static std::mt19937 rng(std::random_device{}());
	const uint8_t g = rtso::building_attack_sounds(type).c;
	float pitch = 1.0f;
	const String path = rtso::pick_attack_sound(g, rng, &pitch);
	if (path.is_empty()) return;
	const float gain = rtso::attack_sound_group(g).gain;
	view.play_spatial_sound(rtso::load_audio(path), pos, gain > 0.001f ? 20.0f * std::log10(gain) : -80.0f, 95.0f, pitch);
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
	CLIP_FEEDING, // 0.28 UnitAI animal IDLE.LINGERING (add_0ad_feeding_clips.py)
	// Formation variant clips (kFormClips), loaded from their PSA on first use.
	CLIP_FORM_FIRST,
	CLIP_COUNT = CLIP_FORM_FIRST + 36
};

// The clips the formation variants name (formation_variants.h variant_clips), each
// once. "bare" clips belong to testudo_top, whose variant empties weapon_R.
struct FormClip {
	const char *psa;
	bool bare;
};
static constexpr FormClip kFormClips[CLIP_COUNT - CLIP_FORM_FIRST] = {
	{"biped_formations_anti_cavalry_back_idle", false},
	{"biped_formations_anti_cavalry_front_idle", false},
	{"biped_infantry_spearman_walk_relax", false},
	{"biped_infantry_hoplite_idle_ready_back", false},
	{"biped_infantry_hoplite_idle_ready_front", false},
	{"biped_infantry_hoplite_idle_ready_01", false},
	{"biped_infantry_hoplite_walk_ready", false},
	{"biped_infantry_hoplite_walk_ready_front", false},
	{"biped_infantry_hoplite_jog_ready", false},
	{"biped_infantry_hoplite_run_ready", false},
	{"biped_formations_syntagma_idle_ready_01", false},
	{"biped_formations_syntagma_idle_ready_02", false},
	{"biped_formations_syntagma_idle_ready_03", false},
	{"biped_formations_syntagma_idle_ready_04", false},
	{"biped_formations_syntagma_idle_ready_back", false},
	{"biped_formations_syntagma_idle_ready_front", false},
	{"biped_formations_syntagma_walk_ready_01", false},
	{"biped_formations_syntagma_walk_ready_02", false},
	{"biped_formations_syntagma_walk_ready_03", false},
	{"biped_formations_syntagma_walk_ready_04", false},
	{"biped_formations_syntagma_walk_relax_back", false},
	{"biped_infantry_pikeman_walk_ready", false},
	{"biped_infantry_pikeman_run_ready", false},
	{"biped_infantry_pikeman_jog_ready", false},
	{"biped_formations_testudo_front_idle", false},
	{"biped_formations_testudo_front_walk", false},
	{"biped_formations_testudo_front_left_idle", false},
	{"biped_formations_testudo_front_left_walk", false},
	{"biped_formations_testudo_front_right_idle", false},
	{"biped_formations_testudo_front_right_walk", false},
	{"biped_formations_testudo_left_idle", false},
	{"biped_formations_testudo_left_walk", false},
	{"biped_formations_testudo_right_idle", false},
	{"biped_formations_testudo_right_walk", false},
	{"biped_formations_testudo_top_idle", true},
	{"biped_formations_testudo_top_walk", true},
};

static bool is_form_clip(int c) {
	return c >= CLIP_FORM_FIRST && c < CLIP_COUNT;
}

// The formation clip playing `psa` (with testudo_top's empty weapon_R when bare).
static ClipId form_clip_of(const char *psa, bool bare) {
	if (!psa) return CLIP_NONE;
	for (int i = 0; i < CLIP_COUNT - CLIP_FORM_FIRST; ++i)
		if (kFormClips[i].bare == bare && std::strcmp(kFormClips[i].psa, psa) == 0) return (ClipId)(CLIP_FORM_FIRST + i);
	return CLIP_NONE;
}

static const StringName &clip_name(ClipId id) {
	// Leaked on purpose: Godot objects in statics would be destroyed after the
	// engine has gone, crashing the exit (here and in the caches below).
	static const StringName *names = [] {
		StringName *n = new StringName[CLIP_COUNT]{
		StringName(), StringName("Idle"), StringName("Walk"), StringName("Run"),
		StringName("Attack"), StringName("Death"), StringName("Gather"),
		StringName("GatherTree"), StringName("GatherOre"), StringName("GatherRock"),
		StringName("GatherFruit"), StringName("GatherGrain"), StringName("GatherMeat"),
		StringName("GatherFish"),
		StringName("Build"), StringName("Carry"), StringName("Heal"),
		StringName("Slaughter"), StringName("Promotion"),
		StringName("CarryFood"), StringName("CarryMeat"), StringName("CarryWood"),
		StringName("CarryStone"), StringName("CarryMetal"), StringName("Capture"),
		StringName("Idle2"), StringName("Death2"), StringName("Feeding")
		};
		// ModelLibrary::apply_clip_tools: "Form" clips show the weapon, "FormBare" none.
		for (int i = 0; i < CLIP_COUNT - CLIP_FORM_FIRST; ++i)
			n[CLIP_FORM_FIRST + i] = StringName(String(kFormClips[i].bare ? "FormBare_" : "Form_") + kFormClips[i].psa);
		return n;
	}();
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
			if (speed2 > 0.05f) return w_move;
			w.clip = gather_clip_for(utype, res_type, res_sub);
			w.state = rtso::AnimState::Gather;
			return w;
		case rtso::ACTION_BUILD:
			if (speed2 > 0.05f) return w_move;
			w.clip = CLIP_BUILD;
			w.state = rtso::AnimState::Build;
			return w;
		// UnitAI INDIVIDUAL.REPAIR on a unit: "build" once in range, the move clip on the way.
		case rtso::ACTION_REPAIR:
			if (speed2 > 0.001f) return w_move;
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
			if (speed2 <= 0.05f || rtso::unit_is_ship(utype)) {
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
	if (diverse_variants(2) < 2) return w; // variant_diversity "none": every unit alike
	const uint32_t h = id * 2654435761u;
	if (w.clip == CLIP_DEATH && (h & 0x80000000u)) w.clip = CLIP_DEATH2;
	else if (w.clip == CLIP_IDLE && (h >> 29) == 3u) w.clip = CLIP_IDLE2;
	return w;
}

// UnitAI FORMATIONMEMBER: a member standing or walking in its slot plays its slot's
// variant (Formation.js GetFormationAnimationVariant); a variant without a Run clip
// keeps its Walk. Any other order takes the member out of that state, and its own
// clips back.
static WantAnim with_formation_variant(WantAnim w, uint8_t uaction, uint8_t variant) {
	if (uaction != rtso::ACTION_IDLE && uaction != rtso::ACTION_MOVE && uaction != rtso::ACTION_ATTACK_MOVE &&
			uaction != rtso::ACTION_PATROL)
		return w;
	const rtso::formvar::VariantClips &vc = rtso::formvar::variant_clips(variant);
	const char *psa = nullptr;
	if (w.clip == CLIP_IDLE || w.clip == CLIP_IDLE2) psa = vc.idle;
	else if (w.clip == CLIP_WALK) psa = vc.walk;
	else if (w.clip == CLIP_RUN) psa = vc.run ? vc.run : vc.walk;
	const ClipId c = form_clip_of(psa, vc.drops_weapon);
	if (c != CLIP_NONE) w.clip = c;
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
		case CLIP_GATHER_FISH:
			if (anim->has_animation(clip_name(CLIP_GATHER))) return CLIP_GATHER;
			alt = CLIP_IDLE;
			break;
		case CLIP_GATHER_MEAT:
			if (anim->has_animation(clip_name(CLIP_GATHER))) return CLIP_GATHER;
			if (anim->has_animation(clip_name(CLIP_ATTACK))) return CLIP_ATTACK;
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
		case CLIP_FEEDING:   alt = CLIP_IDLE; break;
		default:
			if (is_form_clip(want)) {
				const char *psa = kFormClips[want - CLIP_FORM_FIRST].psa;
				alt = std::strstr(psa, "idle") ? CLIP_IDLE : CLIP_WALK;
			}
			break;
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

// apply_unit_tools on a detailed unit's model, skipped when it already shows the clip's tools.
void RtsoWorldView::apply_vis_tools(UnitVis &v, const StringName &clip) {
	if (v.tools_model == v.model && v.tools_clip == clip) return;
	apply_unit_tools(v.model, clip);
	v.tools_model = v.model;
	v.tools_clip = clip;
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
	v.silhouette_rgba = -1;
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
		v.tools_model = nullptr;
		apply_vis_tools(v, StringName("Idle"));
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

// The building GLB ModelLibrary::load draws for `id` (its alias, its own GLB, or
// its base model after the civ code), as the key of the tables written per GLB.
static std::string building_table_key_uncached(const String &id) {
	const String glb = rtso::ModelLibrary::building_glb_id(id);
	if (glb != id || rtso::Assets::res_exists("res://assets/models/buildings/" + id + ".glb"))
		return glb.utf8().get_data();
	const int us = id.find("_");
	if (us <= 0 || us >= id.length() - 1) return glb.utf8().get_data();
	return rtso::ModelLibrary::building_glb_id(id.substr(us + 1)).utf8().get_data();
}

// The key of a building model in the per-model tables, asked for every building every frame: kept
// per id, as the answer depends only on the id and the packed files.
static const std::string &building_table_key(const String &id) {
	static HashMap<String, std::string> keys;
	if (const std::string *k = keys.getptr(id)) return *k;
	return keys.insert(id, building_table_key_uncached(id))->value;
}

void RtsoWorldView::load_foundations() {
	foundations_loaded_ = true;
	auto actor_frame_of = [](const Dictionary &e, const char *scale_key) {
		ActorFrame f;
		f.scale = (float)e.get(scale_key, 0.0);
		Array o = e.get("origin", Array());
		if (o.size() == 3) f.origin = Vector3((float)o[0], (float)o[1], (float)o[2]);
		return f;
	};
	Ref<FileAccess> f = FileAccess::open("res://data/foundations.json", FileAccess::READ);
	if (!f.is_valid()) return;
	Dictionary root = JSON::parse_string(f->get_as_text());
	Dictionary structures = root.get("structures", Dictionary());
	Array civs = structures.keys();
	for (int i = 0; i < civs.size(); ++i) {
		Dictionary types = structures[civs[i]];
		Array keys = types.keys();
		for (int j = 0; j < keys.size(); ++j)
			foundation_of_[(String(civs[i]) + "/" + String(keys[j])).utf8().get_data()] = String(types[keys[j]]).utf8().get_data();
	}
	Dictionary arts = root.get("foundations", Dictionary());
	Array ids = arts.keys();
	for (int i = 0; i < ids.size(); ++i) {
		Dictionary e = arts[ids[i]];
		FoundationArt a;
		a.id = ids[i];
		a.frame = actor_frame_of(e, "scale");
		Dictionary s = e.get("scaffold", Dictionary());
		if (!s.is_empty()) {
			a.scaffold = s.get("id", "");
			a.scaffold_frame = actor_frame_of(s, "scale");
		}
		foundation_art_[String(ids[i]).utf8().get_data()] = a;
	}
	Dictionary frames = root.get("frames", Dictionary());
	Array glbs = frames.keys();
	for (int i = 0; i < glbs.size(); ++i)
		building_frames_[String(glbs[i]).utf8().get_data()] = actor_frame_of(frames[glbs[i]], "metre");
}

void RtsoWorldView::load_rubble() {
	rubble_loaded_ = true;
	Ref<FileAccess> f = FileAccess::open("res://data/rubble.json", FileAccess::READ);
	if (!f.is_valid()) return;
	Dictionary root = JSON::parse_string(f->get_as_text());
	Dictionary structures = root.get("structures", Dictionary());
	Array civs = structures.keys();
	for (int i = 0; i < civs.size(); ++i) {
		Dictionary rows = structures[civs[i]];
		Array types = rows.keys();
		for (int j = 0; j < types.size(); ++j)
			rubble_of_[String(String(civs[i]) + "/" + String(types[j])).utf8().get_data()] = String(rows[types[j]]).utf8().get_data();
	}
	Dictionary rubble = root.get("rubble", Dictionary());
	Array ids = rubble.keys();
	for (int i = 0; i < ids.size(); ++i) {
		Dictionary e = rubble[ids[i]];
		ActorFrame fr;
		fr.scale = (float)e.get("scale", 0.0);
		Array o = e.get("origin", Array());
		if (o.size() == 3) fr.origin = Vector3((float)o[0], (float)o[1], (float)o[2]);
		rubble_frames_[String(ids[i]).utf8().get_data()] = fr;
	}
}

// 0 A.D. Foundation.js: a structure being built draws its template's
// FoundationActor, switched to the "scaffold" variant once building starts. Both
// sit on the entity's position: under the building model as drawn, the actor
// origin of its GLB (data/foundations.json frames), at the model's scale.
void RtsoWorldView::set_building_site_visible(BuildingVis &v, bool foundation_on, bool scaffold_on, uint8_t btype,
		uint8_t civ, const String &model_id, float b_scale) {
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
	if (!foundations_loaded_ && (foundation_on || scaffold_on)) load_foundations();
	const FoundationArt *art = nullptr;
	const ActorFrame *frame = nullptr;
	if (foundation_on || scaffold_on || v.foundation) {
		const auto fo = foundation_of_.find(std::string(rtso::civ_info(civ < rtso::CIV_COUNT ? civ : rtso::CIV_ATHENIANS).code) + "/" + std::to_string(btype));
		const auto fa = fo == foundation_of_.end() ? foundation_art_.end() : foundation_art_.find(fo->second);
		const auto fr = building_frames_.find(building_table_key(model_id));
		if (fa != foundation_art_.end() && fr != building_frames_.end() && fr->second.scale > 0.0f) {
			art = &fa->second;
			frame = &fr->second;
		}
	}
	v.foundation_glb = art ? art->id : String(rtso::civ_building_foundation(btype));
	if (!art) {
		place(v.foundation, rtso::civ_building_foundation(btype), foundation_on);
		place(v.scaffold, rtso::civ_building_scaffold(btype), scaffold_on);
		return;
	}
	// The actor origin in the root's space, on the ground (the root's height; the
	// model is lifted until its lowest vertex meets it), and world units per metre.
	const Vector3 origin(frame->origin.x * b_scale, 0.0f, frame->origin.z * b_scale);
	const float metre = frame->scale * b_scale;
	auto seat = [&](Node *&slot, const String &id, const ActorFrame &f, bool show) {
		if (id.is_empty() || f.scale <= 0.0f) return;
		if (!slot && show) {
			slot = instantiate_building_id(id);
			if (!slot) return;
			v.root->add_child(slot);
			v.fog_dim = -1.0f;
			v.player_rgba = -1;
		}
		if (Node3D *n3 = Object::cast_to<Node3D>(slot)) {
			const float k = metre / f.scale;
			n3->set_scale(Vector3(k, k, k));
			n3->set_position(origin - f.origin * k);
			n3->set_visible(show);
		}
	};
	seat(v.foundation, art->id, art->frame, foundation_on);
	seat(v.scaffold, art->scaffold, art->scaffold_frame, scaffold_on);
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
	// Palisade WallSet pieces (WALLSET_PALISADE) draw 0 A.D.'s palisades_short, _medium,
	// _long, _tower and _gate; a one-cell palisade from a map keeps its model.
	// Rome's siege WallSet (rome/wallset_siege) draws its own siege_wall_long, _medium,
	// _short, _tower and _gate actors; segments are 36, 24 and 12 m like the stone walls'.
	if (type == rtso::BLD_SIEGE_WALL_GATE) return "siege_wall_gate";
	if (type == rtso::BLD_SIEGE_WALL) {
		if (rtso::is_siege_wall_tower(type, half_size)) return "siege_wall_tower";
		return half_size >= 3.75f ? "siege_wall_long" : half_size >= 2.25f ? "siege_wall_medium" : "siege_wall_short";
	}
	if (type == rtso::BLD_PALISADE_GATE) return "palisade_gate";
	if (type == rtso::BLD_PALISADE && rtso::is_wallset_piece(type, half_size)) {
		if (rtso::is_palisade_tower(type, half_size)) return "palisade_tower";
		return half_size >= 1.5f ? "palisade_long" : half_size >= 0.9f ? "palisade_medium" : "palisade_short";
	}
	return String(rtso::civ_building_model(civ, type));
}

void RtsoWorldView::set_building_pose(BuildingVis &v, Node3D *n3, const Vector3 &scale, const Vector3 &rot, const Vector3 &pos) {
	if (v.pose_set && v.pose_scale == scale && v.pose_rot == rot && v.pose_pos == pos) return;
	n3->set_scale(scale);
	n3->set_rotation(rot);
	n3->set_position(pos);
	v.pose_set = true;
	v.pose_scale = scale;
	v.pose_rot = rot;
	v.pose_pos = pos;
}

void RtsoWorldView::attach_building_model(BuildingVis &v, const String &model_name) {
	if (v.model_id == model_name && v.model) return;
	if (v.model) {
		v.root->remove_child(v.model);
		v.model->queue_free();
		v.model = nullptr;
	}
	v.model_id = model_name;
	v.upgrade_scaffold = nullptr; // freed with the old model
	v.fog_dim = -1.0f; // new nodes: re-apply the fog overlay
	v.player_rgba = -1;
	v.pose_set = false;
	v.model = instantiate_building_id(model_name);
	if (!v.model) {
		MeshInstance3D *mi = memnew(MeshInstance3D);
		v.model = mi;
	}
	v.root->add_child(v.model);
}

// 0 A.D. Upgrade.js SetUpgradeAnimationVariant: while a structure upgrades its
// actor shows the "upgrading" variant, a construction scaffold propped on its root.
void RtsoWorldView::update_upgrade_scaffold(BuildingVis &v, const String &model_id, bool show) {
	if (!show) {
		if (v.upgrade_scaffold) v.upgrade_scaffold->set_visible(false);
		return;
	}
	if (!v.upgrade_scaffold) {
		Node3D *model = Object::cast_to<Node3D>(v.model);
		if (!model) return;
		if (!upgrade_scaffolds_loaded_) {
			upgrade_scaffolds_loaded_ = true;
			Ref<FileAccess> f = FileAccess::open("res://data/upgrade_scaffolds.json", FileAccess::READ);
			Dictionary root = f.is_valid() ? Dictionary(JSON::parse_string(f->get_as_text())) : Dictionary();
			Array keys = root.keys();
			for (int i = 0; i < keys.size(); ++i) {
				Dictionary e = root[keys[i]];
				Array off = e.get("offset", Array());
				UpgradeScaffold s;
				s.id = e.get("scaffold", "");
				s.scale = (float)e.get("scale", 1.0);
				if (off.size() == 3) s.offset = Vector3((float)off[0], (float)off[1], (float)off[2]);
				upgrade_scaffolds_[String(keys[i]).utf8().get_data()] = s;
			}
		}
		const auto it = upgrade_scaffolds_.find(building_table_key(model_id));
		if (it == upgrade_scaffolds_.end()) return;
		Node3D *scaf = Object::cast_to<Node3D>(instantiate_building_id(it->second.id));
		if (!scaf) return;
		// The scaffold GLB is centred and grounded like the building's, so the
		// offset and scale place it as the actor prop sat on the structure.
		scaf->set_scale(Vector3(it->second.scale, it->second.scale, it->second.scale));
		scaf->set_position(it->second.offset);
		model->add_child(scaf);
		v.upgrade_scaffold = scaf;
		v.fog_dim = -1.0f;
		v.player_rgba = -1;
	}
	v.upgrade_scaffold->set_visible(true);
}

// 0 A.D. ProductionQueue.js SetAnimation: while the first item of a structure's
// queue runs, its actor shows the "researching" or "training" variant; the
// forges' prop blacksmith sparks, forge smoke and flames (art/particles/
// sparks_blacksmith.xml, smoke_blacksmith.xml, flame.xml: rates, lifetimes,
// speeds and sizes in metres, scaled to the model).
void RtsoWorldView::update_production_fx(BuildingVis &v, const String &model_id, uint8_t state, float b_scale, float y_offset) {
	if (state == v.production || !v.root) return;
	v.production = state;
	for (Node3D *p : v.production_fx) {
		if (p->get_parent()) p->get_parent()->remove_child(p);
		p->queue_free();
	}
	v.production_fx.clear();
	if (state == 0) return;
	if (!production_fx_loaded_) {
		production_fx_loaded_ = true;
		Ref<FileAccess> f = FileAccess::open("res://data/production_fx.json", FileAccess::READ);
		Dictionary root = f.is_valid() ? Dictionary(JSON::parse_string(f->get_as_text())) : Dictionary();
		Array keys = root.keys();
		for (int i = 0; i < keys.size(); ++i) {
			Dictionary e = root[keys[i]];
			ProductionFxSet set;
			set.metre = (float)e.get("metre", 0.25);
			const char *states[2] = { "researching", "training" };
			for (int s = 0; s < 2; ++s) {
				Array list = e.get(states[s], Array());
				for (int j = 0; j < list.size(); ++j) {
					Dictionary fx = list[j];
					Array pos = fx.get("pos", Array());
					const String kind = fx.get("fx", "");
					if (pos.size() != 3) continue;
					if (fx.has("glb")) {
						set.actors[s].push_back({ String(fx["glb"]), Vector3((float)pos[0], (float)pos[1], (float)pos[2]),
								(float)fx.get("yaw", 0.0) });
						continue;
					}
					set.fx[s].push_back({ (uint8_t)(kind == "sparks" ? 0 : kind == "smoke" ? 1 : 2),
							Vector3((float)pos[0], (float)pos[1], (float)pos[2]) });
				}
			}
			production_fx_[String(keys[i]).utf8().get_data()] = set;
		}
		auto quad = [](const char *tex_path, bool additive) {
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
			Ref<QuadMesh> q;
			q.instantiate();
			q->set_size(Vector2(1.0f, 1.0f));
			q->set_material(m);
			return q;
		};
		fx_sparks_quad_ = quad("res://assets/textures/particles/sparks.png", true);
		fx_forge_smoke_quad_ = quad("res://assets/textures/particles/smoke_64a_blacksmith.png", false);
	}
	const auto it = production_fx_.find(building_table_key(model_id));
	if (it == production_fx_.end()) return;
	const ProductionFxSet &set = it->second;
	const float m = set.metre * b_scale; // world units per 0 A.D. metre
	for (const ProductionFx &fx : set.fx[state == 1 ? 0 : 1]) {
		// rate /s, lifetime s, upward speed m/s, sideways speed m/s, size m
		struct Def { float rate, life_min, life_max, up_min, up_max, side, size_min, size_max; };
		static const Def kDefs[3] = {
			{ 3.0f, 2.0f, 6.0f, 1.5f, 2.0f, 0.5f, 0.1f, 0.2f }, // sparks_blacksmith
			{ 9.0f, 14.0f, 16.0f, 2.0f, 2.5f, 0.5f, 1.5f, 2.5f }, // smoke_blacksmith
			{ 20.0f, 1.0f, 1.5f, 2.0f, 2.5f, 0.3f, 1.0f, 2.0f }, // flame
		};
		const Def &d = kDefs[fx.kind];
		Ref<ParticleProcessMaterial> pm;
		pm.instantiate();
		pm->set_direction(Vector3(0, 1, 0));
		pm->set_spread(Math::rad_to_deg(Math::atan2(d.side, d.up_min)));
		pm->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, d.up_min * m);
		pm->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, d.up_max * m);
		pm->set_gravity(Vector3(0, 0, 0));
		pm->set_param_min(ParticleProcessMaterial::PARAM_SCALE, d.size_min * m);
		pm->set_param_max(ParticleProcessMaterial::PARAM_SCALE, d.size_max * m);
		pm->set_param_min(ParticleProcessMaterial::PARAM_ANGLE, fx.kind == 1 ? -180.0f : -28.0f);
		pm->set_param_max(ParticleProcessMaterial::PARAM_ANGLE, fx.kind == 1 ? 180.0f : 28.0f);
		if (fx.kind != 0 && fx_flame_ppm_.is_valid()) // smoke and flames fade like the damage fire
			pm->set_color_ramp(fx.kind == 1 ? fx_smoke_ppm_->get_color_ramp() : fx_flame_ppm_->get_color_ramp());
		if (fx.kind == 1) pm->set_color(Color(0.8f, 0.8f, 0.8f, 1.0f)); // color.r 0.6-1.0
		GPUParticles3D *p = memnew(GPUParticles3D);
		p->set_process_material(pm);
		p->set_draw_pass_mesh(0, fx.kind == 0 ? fx_sparks_quad_ : fx.kind == 1 ? fx_forge_smoke_quad_ : fx_flame_quad_);
		p->set_amount(std::max(1, (int)std::ceil(d.rate * d.life_max)));
		p->set_lifetime(d.life_max);
		p->set_randomness_ratio(1.0f - d.life_min / d.life_max);
		p->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		p->set_visibility_aabb(AABB(Vector3(-8, -1, -8), Vector3(16, 40, 16)));
		// The model node sits at (0, y_offset, 0) under the root, scaled by b_scale.
		p->set_position(Vector3(fx.pos.x * b_scale, y_offset + fx.pos.y * b_scale, fx.pos.z * b_scale));
		v.root->add_child(p);
		v.production_fx.push_back(p);
	}
	// The German forge's forger and the German temple's crows: animated actor
	// props, playing their Idle (blacksmith_idle, bird_circle_idle_01) in a loop.
	for (const ProductionActor &a : set.actors[state == 1 ? 0 : 1]) {
		auto entry = rtso::ModelLibrary::get("units", a.glb);
		if (!entry.scene.is_valid()) continue;
		Node3D *n = Object::cast_to<Node3D>(entry.scene->instantiate());
		if (!n) continue;
		rtso::ModelLibrary::apply_imported_material_fixups(n);
		n->set_position(Vector3(a.pos.x * b_scale, y_offset + a.pos.y * b_scale, a.pos.z * b_scale));
		n->set_rotation(Vector3(0.0f, a.yaw, 0.0f));
		n->set_scale(Vector3(m, m, m));
		v.root->add_child(n);
		TypedArray<Node> aps = n->find_children("*", "AnimationPlayer", true, false);
		if (AnimationPlayer *ap = aps.is_empty() ? nullptr : Object::cast_to<AnimationPlayer>(aps[0])) {
			const StringName idle("Idle");
			if (ap->has_animation(idle)) {
				if (Ref<Animation> an = ap->get_animation(idle); an.is_valid()) an->set_loop_mode(Animation::LOOP_LINEAR);
				ap->play(idle);
			}
		}
		v.production_fx.push_back(n);
	}
}

// 0 A.D. Foundation.js sets the foundation actor's "numbuilders" and its
// "scaffold" variant props art/particles/construction_dust.xml, which emits 2
// puffs a second per builder (at most 16); the structures' "upgrading" variant
// props destruction_dust_small.xml (50 a second). Rates, lifetimes, the emission
// box, speeds, sizes and the downward force are the files' values in metres, from
// the actor origin (data/foundations.json frames) at the model's scale.
void RtsoWorldView::update_site_dust(BuildingVis &v, uint8_t kind, int builders, const String &model_id, float b_scale) {
	if (!v.root) return;
	struct Def { float rate, life_min, life_max, box, y, side, up_min, up_max, size_min, size_max, force, spin_min, spin_max; Color color; };
	static const Def kDefs[2] = {
		{ 16.0f, 3.0f, 5.0f, 8.0f, 1.0f, 1.25f, 2.0f, 3.5f, 5.0f, 7.5f, -2.5f, -2.0f, 3.0f, Color(0.75f, 0.7f, 0.55f) }, // construction_dust
		{ 50.0f, 3.0f, 8.0f, 7.0f, 4.0f, 1.0f, 1.5f, 2.0f, 5.0f, 10.0f, -0.15f, -1.5f, 1.5f, Color(1.0f, 1.0f, 1.0f) }, // destruction_dust_small
	};
	if (kind != v.site_dust_kind) {
		if (v.site_dust) {
			if (v.site_dust->get_parent()) v.site_dust->get_parent()->remove_child(v.site_dust);
			v.site_dust->queue_free();
			v.site_dust = nullptr;
		}
		v.site_dust_kind = kind;
		if (kind == 0) return;
		if (fx_dust_quad_.is_null()) {
			Ref<StandardMaterial3D> mat;
			mat.instantiate();
			mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
			mat->set_billboard_mode(BaseMaterial3D::BILLBOARD_PARTICLES);
			mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
			mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
			mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
			const char *tex_path = "res://assets/textures/particles/dust_256a.png";
			if (rtso::Assets::res_exists(tex_path)) {
				Ref<Texture2D> t = ResourceLoader::get_singleton()->load(tex_path);
				if (t.is_valid()) mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, t);
			}
			fx_dust_quad_.instantiate();
			fx_dust_quad_->set_size(Vector2(1.0f, 1.0f));
			fx_dust_quad_->set_material(mat);
		}
		if (!foundations_loaded_) load_foundations();
		Vector3 origin;
		float m = 0.25f; // world units per metre (1 unit = 4 m) when the GLB has no actor frame
		const auto fr = building_frames_.find(building_table_key(model_id));
		if (fr != building_frames_.end() && fr->second.scale > 0.0f) {
			origin = Vector3(fr->second.origin.x * b_scale, 0.0f, fr->second.origin.z * b_scale);
			m = fr->second.scale * b_scale;
		}
		const Def &d = kDefs[kind - 1];
		Ref<ParticleProcessMaterial> pm;
		pm.instantiate();
		pm->set_emission_shape(ParticleProcessMaterial::EMISSION_SHAPE_BOX);
		pm->set_emission_box_extents(Vector3(d.box * m, 0.0f, d.box * m));
		pm->set_direction(Vector3(0, 1, 0));
		pm->set_spread(Math::rad_to_deg(Math::atan2(d.side, d.up_min)));
		pm->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, d.up_min * m);
		pm->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, d.up_max * m);
		pm->set_gravity(Vector3(0, d.force * m, 0));
		pm->set_param_min(ParticleProcessMaterial::PARAM_SCALE, d.size_min * m);
		pm->set_param_max(ParticleProcessMaterial::PARAM_SCALE, d.size_max * m);
		pm->set_param_min(ParticleProcessMaterial::PARAM_ANGLE, -180.0f);
		pm->set_param_max(ParticleProcessMaterial::PARAM_ANGLE, 180.0f);
		pm->set_param_min(ParticleProcessMaterial::PARAM_ANGULAR_VELOCITY, Math::rad_to_deg(d.spin_min));
		pm->set_param_max(ParticleProcessMaterial::PARAM_ANGULAR_VELOCITY, Math::rad_to_deg(d.spin_max));
		if (fx_smoke_ppm_.is_valid()) pm->set_color_ramp(fx_smoke_ppm_->get_color_ramp());
		pm->set_color(d.color);
		GPUParticles3D *p = memnew(GPUParticles3D);
		p->set_process_material(pm);
		p->set_draw_pass_mesh(0, fx_dust_quad_);
		p->set_amount(std::max(1, (int)std::ceil(d.rate * d.life_max)));
		p->set_lifetime(d.life_max);
		p->set_randomness_ratio(1.0f - d.life_min / d.life_max);
		p->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		p->set_visibility_aabb(AABB(Vector3(-12, -1, -12), Vector3(24, 30, 24)));
		p->set_position(origin + Vector3(0, d.y * m, 0));
		v.root->add_child(p);
		v.site_dust = p;
	}
	if (kind == 1 && v.site_dust && builders >= 0) {
		const float ratio = std::min(16, 2 * builders) / 16.0f; // emissionrate numbuilders x 2, max 16
		v.site_dust->set_amount_ratio(ratio);
		v.site_dust->set_emitting(ratio > 0.0f);
	}
}

// 0 A.D. CModelDecal: a structure's or foundation's decal prop lays a textured
// rectangle (width x depth metres, turned by angle degrees) on the terrain under
// it. Godot Decal nodes on the root project them onto the terrain's DECAL_LAYER
// only; `k` and `base` place the GLB's units under the root (the model's scale,
// or the foundation's seat). The terrain shader darkens out-of-sight ground
// before decals blend in, so they take the LOS brightness as their modulate.
void RtsoWorldView::update_building_decals(const rtso::Simulation *sim, BuildingVis &v, const String &glb_id, float k, const Vector3 &base, float brightness) {
	if (!v.root) return;
	if (!v.decal_key.same(glb_id, k, base.x, base.z)) {
		for (Decal *d : v.decals) {
			if (d->get_parent()) d->get_parent()->remove_child(d);
			d->queue_free();
		}
		v.decals.clear();
		v.decal_key.set(glb_id, k, base.x, base.z);
		if (glb_id.is_empty()) return;
		if (!building_decals_loaded_) {
			building_decals_loaded_ = true;
			Ref<FileAccess> f = FileAccess::open("res://data/building_decals.json", FileAccess::READ);
			Dictionary root = f.is_valid() ? Dictionary(Dictionary(JSON::parse_string(f->get_as_text())).get("decals", Dictionary())) : Dictionary();
			Array keys = root.keys();
			for (int i = 0; i < keys.size(); ++i) {
				Array list = root[keys[i]];
				std::vector<BuildingDecal> &out = building_decals_[String(keys[i]).utf8().get_data()];
				for (int j = 0; j < list.size(); ++j) {
					Dictionary e = list[j];
					Array pos = e.get("pos", Array()), size = e.get("size", Array());
					if (pos.size() != 2 || size.size() != 2) continue;
					out.push_back({ e.get("tex", ""), Vector2((float)pos[0], (float)pos[1]), Vector2((float)size[0], (float)size[1]),
							(float)e.get("angle", 0.0) });
				}
			}
		}
		const auto it = building_decals_.find(building_table_key(glb_id));
		if (it == building_decals_.end()) return;
		for (const BuildingDecal &bd : it->second) {
			Ref<Texture2D> &tex = decal_tex_[bd.tex.utf8().get_data()];
			if (tex.is_null()) {
				const String path = "res://assets/textures/decals/" + bd.tex + ".png";
				if (rtso::Assets::res_exists(path)) tex = ResourceLoader::get_singleton()->load(path);
				if (tex.is_null()) continue;
			}
			const Vector2 sz = bd.size * k;
			Decal *d = memnew(Decal);
			d->set_texture(Decal::TEXTURE_ALBEDO, tex);
			// Tall enough to reach ground the structure does not flatten.
			d->set_size(Vector3(sz.x, std::max(2.0f, 0.5f * std::max(sz.x, sz.y)), sz.y));
			d->set_cull_mask(DECAL_LAYER);
			d->set_normal_fade(0.3f);
			d->set_transform(Transform3D(Basis(Vector3(0, 1, 0), Math::deg_to_rad(bd.angle)),
					Vector3(base.x + bd.pos.x * k, 0.0f, base.z + bd.pos.y * k)));
			v.root->add_child(d);
			v.decals.push_back(d);
		}
	}
	const Color mod(brightness, brightness, brightness, 1.0f);
	for (Decal *d : v.decals) {
		if (d->get_modulate() != mod) d->set_modulate(mod);
		sync_gl_decal(sim, d);
	}
}

// 0 A.D. structure actors prop particle emitters in the variants they always show
// (chimney smoke, temple and camp fires, the lighthouse beacon, the flames and
// smoke of civic-centre rubble). Each art/particles/*.xml file's emission rate,
// lifetimes, emitter offset and box, velocities, sizes, growth, force, colour and
// blending become a GPUParticles3D on the root, at the prop's GLB position; `k`
// and `base` place the GLB's units under the root, as the decals'. Emitters with
// world-space velocities are turned back to the world's axes; use_relative_velocity
// ones turn with the structure.
// data/ambient_fx.json (tools/extract_ambient_fx.py), read once.
void RtsoWorldView::load_ambient_fx() {
	if (ambient_fx_loaded_) return;
	ambient_fx_loaded_ = true;
	Ref<FileAccess> f = FileAccess::open("res://data/ambient_fx.json", FileAccess::READ);
	Dictionary root = f.is_valid() ? Dictionary(JSON::parse_string(f->get_as_text())) : Dictionary();
	auto pair = [](const Variant &a) {
		Array r = a;
		return r.size() == 2 ? Vector2((float)r[0], (float)r[1]) : Vector2();
	};
	Dictionary parts = root.get("particles", Dictionary());
	Array names = parts.keys();
	for (int i = 0; i < names.size(); ++i) {
		Dictionary e = parts[names[i]];
		AmbientParticle d;
		d.tex = e.get("tex", "");
		d.add = e.get("add", false);
		d.start_full = e.get("start_full", false);
		d.relative = e.get("relative", false);
		d.rate = (float)e.get("rate", 0.0);
		d.growth = (float)e.get("growth", 0.0);
		d.life = pair(e.get("life", Array()));
		d.size = pair(e.get("size", Array()));
		d.color = pair(e.get("color", Array()));
		d.angle = pair(e.get("angle", Array()));
		d.spin = pair(e.get("spin", Array()));
		Array pos = e.get("pos", Array()), vel = e.get("vel", Array()), force = e.get("force", Array());
		for (int a = 0; a < 3; ++a) {
			d.pos[a] = pos.size() == 3 ? pair(pos[a]) : Vector2();
			d.vel[a] = vel.size() == 3 ? pair(vel[a]) : Vector2();
		}
		if (force.size() == 3) d.force = Vector3((float)force[0], (float)force[1], (float)force[2]);
		ambient_particles_[String(names[i]).utf8().get_data()] = d;
	}
	for (int t = 0; t < 2; ++t) {
		Dictionary structures = root.get(t == 0 ? "structures" : "death", Dictionary());
		Array keys = structures.keys();
		for (int i = 0; i < keys.size(); ++i) {
			Dictionary e = structures[keys[i]];
			AmbientFxSet set;
			set.metre = (float)e.get("metre", 0.25);
			Array list = e.get("fx", Array());
			for (int j = 0; j < list.size(); ++j) {
				Dictionary fx = list[j];
				Array pos = fx.get("pos", Array());
				if (pos.size() != 3) continue;
				set.fx.push_back({ String(fx.get("p", "")).utf8().get_data(), Vector3((float)pos[0], (float)pos[1], (float)pos[2]) });
			}
			(t == 0 ? ambient_fx_ : death_fx_)[String(keys[i]).utf8().get_data()] = set;
		}
	}
	Dictionary units = root.get("units", Dictionary());
	Array unit_keys = units.keys();
	for (int i = 0; i < unit_keys.size(); ++i) {
		Dictionary e = units[unit_keys[i]];
		AmbientFxSet set;
		set.metre = (float)e.get("metre", 1.0);
		Array list = e.get("fx", Array());
		for (int j = 0; j < list.size(); ++j) {
			Dictionary fx = list[j];
			Array pos = fx.get("pos", Array());
			if (pos.size() == 3)
				set.fx.push_back({ String(fx.get("p", "")).utf8().get_data(), Vector3((float)pos[0], (float)pos[1], (float)pos[2]) });
		}
		unit_fx_.insert(String(unit_keys[i]), set);
	}
	Array stone = Dictionary(root.get("projectiles", Dictionary())).get("stone", Array());
	for (int j = 0; j < stone.size(); ++j) stone_fx_.push_back(String(stone[j]).utf8().get_data());
	Dictionary run = root.get("run", Dictionary());
	Array run_keys = run.keys();
	for (int i = 0; i < run_keys.size(); ++i) {
		Array names = run[run_keys[i]];
		std::vector<std::string> list;
		for (int j = 0; j < names.size(); ++j) list.push_back(String(names[j]).utf8().get_data());
		run_fx_.insert(String(run_keys[i]), list);
	}
}

// One 0 A.D. particle file as a GPUParticles3D, `m` world units per metre.
GPUParticles3D *RtsoWorldView::make_ambient_emitter(const AmbientParticle &d, float m) {
	Ref<QuadMesh> &quad = ambient_quads_[d.tex.utf8().get_data()];
	if (quad.is_null()) {
		Ref<StandardMaterial3D> mat;
		mat.instantiate();
		mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		mat->set_billboard_mode(BaseMaterial3D::BILLBOARD_PARTICLES);
		mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		if (d.add) mat->set_blend_mode(BaseMaterial3D::BLEND_MODE_ADD);
		mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		const String path = "res://assets/textures/particles/" + d.tex;
		if (rtso::Assets::res_exists(path)) {
			Ref<Texture2D> t = ResourceLoader::get_singleton()->load(path);
			if (t.is_valid()) mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, t);
		}
		quad.instantiate();
		quad->set_size(Vector2(1.0f, 1.0f));
		quad->set_material(mat);
	}
	auto mid = [](const Vector2 &r) { return 0.5f * (r.x + r.y); };
	auto half = [](const Vector2 &r) { return 0.5f * std::abs(r.y - r.x); };
	const Vector3 vmean(mid(d.vel[0]), mid(d.vel[1]), mid(d.vel[2]));
	const float speed = vmean.length();
	// The velocity box's widest axis across the mean direction sets the spread.
	const int along = std::abs(vmean.z) > std::abs(vmean.y) ? 2 : 1;
	const float along_half = half(d.vel[along]);
	const float across = std::max(half(d.vel[0]), along == 1 ? half(d.vel[2]) : half(d.vel[1]));
	Ref<ParticleProcessMaterial> pm;
	pm.instantiate();
	pm->set_direction(speed > 1e-4f ? vmean / speed : Vector3(0, 1, 0));
	pm->set_spread(speed > 1e-4f ? Math::rad_to_deg(Math::atan2(across, speed)) : 180.0f);
	pm->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, std::max(0.0f, speed - along_half) * m);
	pm->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, (speed + along_half) * m);
	pm->set_gravity(d.force * m);
	pm->set_emission_shape(ParticleProcessMaterial::EMISSION_SHAPE_BOX);
	pm->set_emission_box_extents(Vector3(half(d.pos[0]), half(d.pos[1]), half(d.pos[2])) * m);
	pm->set_param_min(ParticleProcessMaterial::PARAM_SCALE, d.size.x * m);
	pm->set_param_max(ParticleProcessMaterial::PARAM_SCALE, d.size.y * m);
	if (d.growth > 0.0f && mid(d.size) > 0.0f) {
		// size.growthRate metres a second over the particle's life.
		const float g = 1.0f + d.growth * mid(d.life) / mid(d.size);
		Ref<Curve> grow;
		grow.instantiate();
		grow->set_max_value(g);
		grow->add_point(Vector2(0.0f, 1.0f));
		grow->add_point(Vector2(1.0f, g));
		Ref<CurveTexture> ct;
		ct.instantiate();
		ct->set_curve(grow);
		pm->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, ct);
	}
	pm->set_param_min(ParticleProcessMaterial::PARAM_ANGLE, Math::rad_to_deg(d.angle.x));
	pm->set_param_max(ParticleProcessMaterial::PARAM_ANGLE, Math::rad_to_deg(d.angle.y));
	pm->set_param_min(ParticleProcessMaterial::PARAM_ANGULAR_VELOCITY, Math::rad_to_deg(d.spin.x));
	pm->set_param_max(ParticleProcessMaterial::PARAM_ANGULAR_VELOCITY, Math::rad_to_deg(d.spin.y));
	const float c = mid(d.color);
	pm->set_color(Color(c, c, c, 1.0f));
	{
		Ref<Gradient> gr;
		gr.instantiate();
		gr->set_offsets(PackedFloat32Array({ 0.0f, 0.15f, 1.0f }));
		gr->set_colors(PackedColorArray({ Color(1, 1, 1, 0), Color(1, 1, 1, 1), Color(1, 1, 1, 0) }));
		Ref<GradientTexture1D> gt;
		gt.instantiate();
		gt->set_gradient(gr);
		pm->set_color_ramp(gt);
	}
	GPUParticles3D *p = memnew(GPUParticles3D);
	p->set_process_material(pm);
	p->set_draw_pass_mesh(0, quad);
	p->set_amount(std::max(1, (int)std::ceil(d.rate * d.life.y)));
	p->set_lifetime(d.life.y);
	p->set_randomness_ratio(Math::clamp(1.0f - d.life.x / d.life.y, 0.0f, 1.0f));
	if (d.start_full) p->set_pre_process_time(d.life.y);
	p->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	{
		const float t = d.life.y;
		const float rise = ((speed + along_half) * t + 0.5f * std::abs(d.force.y) * t * t) * m + d.size.y * m * (1.0f + d.growth * t);
		const float wide = std::min(40.0f, (across + 0.5f * Vector2(d.force.x, d.force.z).length() * t) * t * m + d.size.y * m * (1.0f + d.growth * t));
		const float h = std::min(60.0f, rise);
		p->set_visibility_aabb(AABB(Vector3(-wide - 1.0f, -1.0f, -wide - 1.0f), Vector3(2.0f * wide + 2.0f, h + 2.0f, 2.0f * wide + 2.0f)));
	}
	return p;
}

// 0 A.D. art/variants/quadraped/base_{horse,camel,elephant}_run.xml and the fauna
// actors' "Run" variant prop dust_cav on the root while the unit runs. A world-space
// emitter per particle and scale takes emit_particle at each runner's feet, at the
// particle file's rate.
void RtsoWorldView::emit_run_dust(const String &glb_id, const Vector3 &at, float sc, float delta) {
	load_ambient_fx();
	if (const std::vector<std::string> *names = run_fx_.getptr(glb_id)) emit_world_fx(*names, at, sc, delta);
}

// The fire a unit actor always carries (the Theban fire-raiser's cart, the flaming
// pig), at its GLB points under the model's transform `xf`.
void RtsoWorldView::emit_unit_fx(const String &glb_id, const Transform3D &xf, float sc, float delta) {
	load_ambient_fx();
	const AmbientFxSet *set = unit_fx_.getptr(glb_id);
	if (!set) return;
	for (const AmbientFx &fx : set->fx)
		emit_world_fx(fx.particle, xf.xform(fx.pos), set->metre * sc, delta);
}

// Puffs of each named particle at `at` (world space) at the files' rates over `delta`
// seconds, `sc` world units per metre: one emitter per particle and scale that
// emit_particle feeds, so a moving source leaves a trail and needs no node of its own.
void RtsoWorldView::emit_world_fx(const std::vector<std::string> &names, const Vector3 &at, float sc, float delta) {
	for (const std::string &name : names) emit_world_fx(name, at, sc, delta);
}

void RtsoWorldView::emit_world_fx(const std::string &name, const Vector3 &at, float sc, float delta) {
	{
		const auto pd = ambient_particles_.find(name);
		if (pd == ambient_particles_.end() || pd->second.rate <= 0.0f || pd->second.life.y <= 0.0f) return;
		const AmbientParticle &d = pd->second;
		GPUParticles3D *&p = world_emitters_[{ &d, (int)std::lround(sc * 1000.0f) }];
		if (!p) {
			p = make_ambient_emitter(d, sc);
			p->set_amount(4096);
			p->set_use_local_coordinates(false);
			p->set_emitting(false); // only emit_particle adds particles
			p->set_visibility_aabb(AABB(Vector3(-4096, -64, -4096), Vector3(8192, 256, 8192)));
			add_child(p);
		}
		float n = d.rate * std::min(delta, 0.25f);
		int count = (int)n;
		if (UtilityFunctions::randf() < n - (float)count) ++count;
		const auto mid = [](const Vector2 &r) { return 0.5f * (r.x + r.y); };
		const auto half = [](const Vector2 &r) { return 0.5f * std::abs(r.y - r.x); };
		for (int k = 0; k < count; ++k) {
			const Vector3 off(mid(d.pos[0]) + half(d.pos[0]) * (float)UtilityFunctions::randf_range(-1.0, 1.0),
					mid(d.pos[1]) + half(d.pos[1]) * (float)UtilityFunctions::randf_range(-1.0, 1.0),
					mid(d.pos[2]) + half(d.pos[2]) * (float)UtilityFunctions::randf_range(-1.0, 1.0));
			p->emit_particle(Transform3D(Basis(), at + off * sc), Vector3(), Color(1, 1, 1, 1), Color(),
					GPUParticles3D::EMIT_FLAG_POSITION);
		}
	}
}

void RtsoWorldView::update_ambient_fx(BuildingVis &v, const String &glb_id, float k, const Vector3 &base, bool death) {
	if (!v.root) return;
	std::vector<GPUParticles3D *> &emitters = death ? v.death_fx : v.ambient_fx;
	std::vector<Vector3> &emitter_pos = death ? v.death_pos : v.ambient_pos;
	FxKey &vkey = death ? v.death_key : v.ambient_key;
	if (!vkey.same(glb_id, k, 0.0f, 0.0f)) {
		for (GPUParticles3D *p : emitters) {
			if (p->get_parent()) p->get_parent()->remove_child(p);
			p->queue_free();
		}
		emitters.clear();
		emitter_pos.clear();
		vkey.set(glb_id, k, 0.0f, 0.0f);
		if (glb_id.is_empty()) return;
		load_ambient_fx();
		const auto &table = death ? death_fx_ : ambient_fx_;
		const auto it = table.find(building_table_key(glb_id));
		if (it == table.end()) return;
		const AmbientFxSet &set = it->second;
		const float m = set.metre * k; // world units per 0 A.D. metre
		for (const AmbientFx &fx : set.fx) {
			const auto pd = ambient_particles_.find(fx.particle);
			if (pd == ambient_particles_.end() || pd->second.rate <= 0.0f || pd->second.life.y <= 0.0f) continue;
			const AmbientParticle &d = pd->second;
			const auto mid = [](const Vector2 &r) { return 0.5f * (r.x + r.y); };
			GPUParticles3D *p = make_ambient_emitter(d, m);
			p->set_meta("relative", d.relative);
			v.root->add_child(p);
			emitters.push_back(p);
			emitter_pos.push_back(fx.pos + Vector3(mid(d.pos[0]), mid(d.pos[1]), mid(d.pos[2])) * set.metre);
		}
	}
	const Basis unturn = v.root->get_transform().basis.inverse();
	for (size_t i = 0; i < emitters.size(); ++i) {
		GPUParticles3D *p = emitters[i];
		const Transform3D t(bool(p->get_meta("relative")) ? Basis() : unturn, base + emitter_pos[i] * k);
		if (p->get_transform() != t) p->set_transform(t);
	}
}

void RtsoWorldView::hide_unit_vis(UnitVis &v) {
	if (v.root) v.root->set_visible(false);
	if (v.hp_bg) v.hp_bg->set_visible(false);
	if (v.hp_fg) v.hp_fg->set_visible(false);
	if (v.xp_bg) v.xp_bg->set_visible(false);
	if (v.xp_fg) v.xp_fg->set_visible(false);
	if (v.rank_icon) v.rank_icon->set_visible(false);
	if (v.anim && UtilityFunctions::is_instance_valid(v.anim))
		v.anim->set_active(false);
}

// Crowd clip slots. 0 A.D. plays the real clip for every unit (gather_*,
// build, carry, heal...); the crowd keeps one baked MultiMesh per slot.
// The formation variant clips take the slots after CROWD_FORM_SLOT, in kFormClips order.
static constexpr int CROWD_FORM_SLOT = 25;
static const uint8_t *crowd_clips() {
	static const uint8_t *clips = [] {
		static uint8_t c[CROWD_FORM_SLOT + (CLIP_COUNT - CLIP_FORM_FIRST)] = {
			CLIP_IDLE, CLIP_WALK, CLIP_ATTACK, CLIP_DEATH, CLIP_RUN, CLIP_CARRY, CLIP_BUILD,
			CLIP_GATHER_TREE, CLIP_GATHER_ORE, CLIP_GATHER_ROCK, CLIP_GATHER_FRUIT, CLIP_GATHER_GRAIN,
			CLIP_GATHER_MEAT, CLIP_GATHER_FISH, CLIP_HEAL, CLIP_SLAUGHTER,
			CLIP_CARRY_FOOD, CLIP_CARRY_MEAT, CLIP_CARRY_WOOD, CLIP_CARRY_STONE, CLIP_CARRY_METAL,
			CLIP_PROMOTION, CLIP_CAPTURE, CLIP_IDLE2, CLIP_DEATH2,
		};
		for (int i = 0; i < CLIP_COUNT - CLIP_FORM_FIRST; ++i) c[CROWD_FORM_SLOT + i] = (uint8_t)(CLIP_FORM_FIRST + i);
		return c;
	}();
	return clips;
}
// Death clips play once and hold their last frame (slots 3 and 24).
static constexpr uint64_t CROWD_DEATH_SLOTS = (1ull << 3) | (1ull << 24);
// Fauna clip slots that hold their last frame: death (3) and the second death (5).
static constexpr uint32_t FAUNA_DEATH_SLOTS = (1u << 3) | (1u << 5);

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
		default: return is_form_clip(clip) ? CROWD_FORM_SLOT + (clip - CLIP_FORM_FIRST) : 0;
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

// The sea is drawn on the nav water line (grid_nav.h NAV_WATER_MARGIN above
// water_height()), where cells turn to water: ported maps store 0.28's sea
// minus the margin, so this is 0.28's own level, and every shore looks the
// way it behaves. Depth shading, floating ships, fish and naval fauna measure
// from it too.
static float drawn_water_height(const rtso::Simulation *sim) {
	return sim->water_height() + rtso::NAV_WATER_MARGIN;
}

// A male 0.28 Civilian draws and animates as its civ's citizen_male actor, the villager
// model (rtso::civilian_is_male); a female one keeps the female citizen model, or her
// own citizen_female actor where 0.28 has one (Han, Mauryans: unit_template_draw_index).
static uint8_t drawn_unit_type(uint8_t utype, uint16_t tmpl, uint32_t id) {
	return rtso::civilian_is_male(tmpl, id) ? (uint8_t)rtso::UNIT_VILLAGER : utype;
}

// The unit_template_draw_model of the wagon a template's own model packs into
// (packed_siege_models.h), or -1.
static int packed_draw_index(int own) {
	static const auto table = [] {
		std::array<int16_t, rtso::UNIT_TEMPLATE_DRAW_MODEL_COUNT> t;
		t.fill(-1);
		for (int k = 0; k < rtso::UNIT_TEMPLATE_DRAW_MODEL_COUNT; ++k) {
			const char *packed = rtso::packed_siege_model(rtso::unit_template_draw_model(k));
			for (int j = 0; packed && j < rtso::UNIT_TEMPLATE_DRAW_MODEL_COUNT; ++j)
				if (std::strcmp(rtso::unit_template_draw_model(j), packed) == 0) t[k] = (int16_t)j;
		}
		return t;
	}();
	return own >= 0 && own < rtso::UNIT_TEMPLATE_DRAW_MODEL_COUNT ? table[own] : -1;
}

// An advanced or elite unit draws its _a or _e template's actor (0 A.D. Promotion
// changes the template; RTS0 keeps the roster row and raises the rank).
static uint16_t crowd_key_of(uint8_t civ, uint8_t utype, uint8_t pack_state, uint16_t tmpl, uint32_t id, uint8_t rank) {
	if (rtso::civilian_is_male(tmpl, id)) return rtso::UNIT_VILLAGER;
	if (shows_packed(pack_state)) {
		// A siege engine with its own 0.28 actor packs into that actor's wagon (Roman
		// scorpio, ballista and onager, the Han mangonel); the rest into their class model's.
		const int own_packed = packed_draw_index(rtso::unit_template_draw_index(tmpl));
		if (own_packed >= 0) return (uint16_t)(2 * rtso::UNIT_TYPE_COUNT + own_packed);
		if (rtso::packed_siege_model(rtso::civ_unit_model(civ, utype))) return (uint16_t)(rtso::UNIT_TYPE_COUNT + utype);
	}
	const int ranked = rtso::unit_template_rank_draw_index(tmpl, rank);
	if (ranked >= 0) return (uint16_t)(2 * rtso::UNIT_TYPE_COUNT + ranked);
	if (ranked == rtso::UNIT_TEMPLATE_RANK_CLASS_MODEL) return utype;
	const int own = rtso::unit_template_draw_index(tmpl);
	if (own >= 0) return (uint16_t)(2 * rtso::UNIT_TYPE_COUNT + own);
	return utype;
}

RtsoWorldView::CrowdBatch &RtsoWorldView::crowd_batch(uint8_t civ, uint16_t key) {
	if (civ >= rtso::CIV_COUNT) civ = rtso::CIV_ATHENIANS;
	if (key >= CROWD_KEY_COUNT) key = rtso::UNIT_VILLAGER;
	std::unique_ptr<CrowdBatch> &slot = crowd_[civ][key];
	if (!slot) {
		const uint32_t ck = (uint32_t)civ << 16 | key;
		crowd_made_.insert(std::lower_bound(crowd_made_.begin(), crowd_made_.end(), ck), ck);
		slot = std::make_unique<CrowdBatch>();
		slot->model_id = crowd_key_model(civ, key);
		auto entry = rtso::ModelLibrary::get("units", slot->model_id);
		slot->y_offset = entry.y_offset;
		slot->scale = entry.scale;
		slot->height = entry.aabb.size.y;
		slot->rest = entry.crowd_mesh.is_valid() ? entry.crowd_mesh : entry.mesh;
		slot->no_scene = !entry.scene.is_valid();
		// Owner-coloured: a player_color.gdshader surface with a mask (the shader also
		// draws unmasked skins that vary by unit or carry specular or height maps).
		for (int s = 0; slot->rest.is_valid() && s < slot->rest->get_surface_count(); ++s) {
			Ref<ShaderMaterial> sm = slot->rest->surface_get_material(s);
			if (rtso::ModelLibrary::is_player_color_material(sm) && sm->get_shader_parameter("mask_tex").get_type() != Variant::NIL)
				slot->player_color = true;
		}
	}
	return *slot;
}

MultiMesh *RtsoWorldView::crowd_mm(CrowdBatch &b, int clip, int phase) {
	if (phase < 0 || phase >= CROWD_PHASES) phase = 0;
	if (!b.mmi[clip][phase]) {
		Ref<Mesh> m = b.puppet[clip].posed_frames[0].is_valid() ? Ref<Mesh>(b.puppet[clip].posed_frames[0]) : b.rest;
		b.mm[clip][phase] = create_multimesh(m, true);
		b.mm[clip][phase]->set_use_custom_data(true); // owner colour for player_color.gdshader
		b.mmi[clip][phase] = memnew(MultiMeshInstance3D);
		b.mmi[clip][phase]->set_multimesh(b.mm[clip][phase]);
		// 0 A.D. unit actors carry <castshadow/>.
		b.mmi[clip][phase]->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_ON);
		if (silhouettes_ && silhouette_mat_.is_valid()) b.mmi[clip][phase]->set_material_overlay(silhouette_mat_);
		add_child(b.mmi[clip][phase]);
	}
	return b.mm[clip][phase].ptr();
}

// The skeleton a clip library's bone tracks drive, with their path from the player's root.
static Skeleton3D *clip_skeleton(AnimationPlayer *ap, const Ref<AnimationLibrary> &lib, String &skel_path) {
	Ref<Animation> ref = lib->has_animation(clip_name(CLIP_IDLE)) ? lib->get_animation(clip_name(CLIP_IDLE)) : Ref<Animation>();
	for (int t = 0; ref.is_valid() && t < ref->get_track_count(); ++t) {
		NodePath tp = ref->track_get_path(t);
		if (tp.get_subname_count() > 0) {
			skel_path = tp.get_concatenated_names();
			break;
		}
	}
	Node *anim_root = ap->get_node_or_null(ap->get_root_node());
	return (anim_root && !skel_path.is_empty())
			? Object::cast_to<Skeleton3D>(anim_root->get_node_or_null(NodePath(skel_path))) : nullptr;
}

// One 0 A.D. PSA clip into `lib` as `name`, when its bones match the skeleton's.
static bool add_psa_clip(const Ref<AnimationLibrary> &lib, Skeleton3D *sk, const String &skel_path,
		const StringName &name, const char *psa_name) {
	rtso::PsaAnim psa;
	const int bones = sk->get_bone_count();
	if (!psa.load_psa(psa_name) || !psa.is_valid() || (int)psa.numBones != bones) return false;
	Ref<Animation> a;
	a.instantiate();
	const double dt = psa.frameTime / 1000.0;
	a->set_length(dt * psa.numFrames);
	// PSA keys are root-relative bone transforms, the same space as the
	// extractor's flat bone nodes, so they are the bone poses as they stand.
	for (int b = 0; b < bones; ++b) {
		NodePath path(skel_path + String(":") + sk->get_bone_name(b));
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
	lib->add_animation(name, a);
	return true;
}

// A formation variant clip loads from its PSA the first time a model plays it; a
// model whose skeleton it does not fit is not asked again.
static void ensure_form_clip(AnimationPlayer *ap, ClipId clip) {
	if (!ap || !is_form_clip(clip) || ap->has_animation(clip_name(clip))) return;
	Ref<AnimationLibrary> lib = ap->get_animation_library(StringName());
	if (!lib.is_valid()) return;
	static std::unordered_set<uint64_t> &failed = *new std::unordered_set<uint64_t>();
	const uint64_t key = ((uint64_t)lib->get_instance_id() << 8) ^ (uint64_t)clip;
	if (failed.count(key)) return;
	String skel_path;
	Skeleton3D *sk = clip_skeleton(ap, lib, skel_path);
	if (!sk || !add_psa_clip(lib, sk, skel_path, clip_name(clip), kFormClips[clip - CLIP_FORM_FIRST].psa))
		failed.insert(key);
}

// Which formation variants a unit model's 0 A.D. actor lists (data/formation_variants.json,
// tools/extract_formation_variants.py), as bits by rtso::formvar::Variant.
static uint32_t formation_variant_mask(const char *model) {
	static std::unordered_map<std::string, uint32_t> &table = *new std::unordered_map<std::string, uint32_t>();
	static bool loaded = false;
	if (!loaded) {
		loaded = true;
		Ref<FileAccess> f = FileAccess::open("res://data/formation_variants.json", FileAccess::READ);
		if (f.is_valid()) {
			Dictionary root = JSON::parse_string(f->get_as_text());
			Array keys = root.keys();
			for (int i = 0; i < keys.size(); ++i) {
				Array names = root[keys[i]];
				uint32_t m = 0;
				for (int n = 0; n < names.size(); ++n)
					m |= 1u << rtso::formvar::variant_by_name(String(names[n]).utf8().get_data());
				table[String(keys[i]).utf8().get_data()] = m & ~1u;
			}
		}
	}
	auto it = model ? table.find(model) : table.end();
	return it != table.end() ? it->second : 0u;
}

uint32_t RtsoWorldView::crowd_form_mask(uint8_t civ, uint16_t key) {
	if (civ >= rtso::CIV_COUNT || key >= CROWD_KEY_COUNT) return 0;
	uint32_t &m = crowd_form_mask_[civ][key];
	if (!(m & 0x80000000u)) m = 0x80000000u | formation_variant_mask(crowd_key_model(civ, key));
	return m;
}

static uint64_t bake_now_us() {
	return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool RtsoWorldView::crowd_bake_over_budget() {
	return crowd_baked_this_frame_ && bake_now_us() >= crowd_bake_deadline_us_;
}

void RtsoWorldView::bake_puppet_clips(Node *inst, CrowdPuppet *row, bool *done, const uint8_t *want,
		const bool *needed, int count, uint64_t death_slots, bool female) {
	AnimationPlayer *ap = find_anim_player(inst);
	if (ap) {
		ap->set_active(true);
		ap->set_callback_mode_process(AnimationMixer::ANIMATION_CALLBACK_MODE_PROCESS_MANUAL);
	}
	for (int c = 0; c < count; ++c) {
		if (!needed[c] || done[c]) continue;
		done[c] = true;
		ensure_form_clip(ap, (ClipId)want[c]);
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
				for (int p = 0; p < CROWD_PHASES; ++p) row[c].last_frame[p] = -1;
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
					if (death_slots & (1ull << c)) {
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
		for (int p = 0; p < CROWD_PHASES; ++p) row[c].last_frame[p] = -1;
	}
	if (ap) ap->stop();
}

void RtsoWorldView::cycle_puppet_meshes(CrowdPuppet *row, Ref<MultiMesh> meshes[][CROWD_PHASES], const int counts[][CROWD_PHASES], int count, uint64_t death_slots) {
	for (int c = 0; c < count; ++c) {
		if (row[c].baked_count <= 0) continue;
		const bool is_death = (death_slots & (1ull << c)) != 0;
		for (int p = 0; p < CROWD_PHASES; ++p) {
			if (!meshes[c][p].is_valid() || counts[c][p] <= 0) continue;
			int frame;
			if (is_death) {
				frame = (int)(anim_time * 10.0f);
				if (frame > row[c].baked_count - 1) frame = row[c].baked_count - 1;
			} else {
				// Desynchronize animation phases across sub-batches
				const float phase_offset = (float)p * (row[c].baked_count / (float)CROWD_PHASES);
				frame = wrap_mod((int)(anim_time * 16.0f + phase_offset), row[c].baked_count);
			}
			if (frame != row[c].last_frame[p] && row[c].posed_frames[frame].is_valid()) {
				meshes[c][p]->set_mesh(row[c].posed_frames[frame]);
				row[c].last_frame[p] = frame;
			}
		}
	}
}

void RtsoWorldView::cycle_fauna_meshes(CrowdPuppet *row, Ref<MultiMesh> *meshes, const int *counts, int count, uint64_t death_slots) {
	for (int c = 0; c < count; ++c) {
		if (!meshes[c].is_valid() || row[c].baked_count <= 0 || counts[c] <= 0) continue;
		int frame;
		if (death_slots & (1ull << c)) {
			frame = (int)(anim_time * 10.0f);
			if (frame > row[c].baked_count - 1) frame = row[c].baked_count - 1;
		} else {
			frame = wrap_mod((int)(anim_time * 16.0f), row[c].baked_count);
		}
		if (frame != row[c].last_frame[0] && row[c].posed_frames[frame].is_valid()) {
			meshes[c]->set_mesh(row[c].posed_frames[frame]);
			row[c].last_frame[0] = frame;
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

struct RtsoWorldView::CrowdBakeJob {
	Node *inst = nullptr;
	AnimationPlayer *ap = nullptr;
	uint32_t batch = 0;	 // civ << 16 | key
	bool female = false;
	int clip = -1;		 // crowd clip slot mid-bake, -1 between clips
	int frame = 0, valid = 0;
	double len = 0.0;
	Node3D *prop = nullptr;
	rtso::ModelLibrary::CrowdSkin skin;
	Ref<ArrayMesh> poses[CROWD_FRAMES_PER_CLIP];
};

// Readies clip slot c of the job's batch for posing, as bake_puppet_clips does per clip.
void RtsoWorldView::crowd_job_begin_clip(CrowdBatch &b, int c) {
	CrowdBakeJob &j = *crowd_job_;
	crowd_baked_this_frame_ = true;
	b.clip_baked[c] = true;
	b.puppet[c].baked_count = 0;
	for (int p = 0; p < CROWD_PHASES; ++p) b.puppet[c].last_frame[p] = -1;
	AnimationPlayer *ap = j.ap;
	const ClipId want = (ClipId)crowd_clips()[c];
	ensure_form_clip(ap, want);
	const ClipId play = resolve_clip_fallback(ap, want);
	if (!ap || play == CLIP_NONE) return;
	const StringName &name = clip_name(play);
	// Show the clip's tool (axe, pick, hammer...) before capturing, as
	// capture skips hidden props.
	apply_unit_tools(j.inst, name);
	// The carry shuttle bakes into the clip's frames, riding its bone.
	j.prop = is_carry_variant(play) ? attach_carry_prop(j.inst, play, j.female) : nullptr;
	j.skin = rtso::ModelLibrary::CrowdSkin();
	if (!rtso::ModelLibrary::capture_crowd_skin(j.inst, j.skin)) {
		if (j.prop) detach_carry_props(find_skeleton3d(j.inst));
		j.prop = nullptr;
		return;
	}
	j.len = 0.0;
	Ref<Animation> a = ap->get_animation(name);
	if (a.is_valid()) j.len = a->get_length();
	ap->play(name);
	j.clip = c;
	j.frame = 0;
	j.valid = 0;
}

// Bakes the job's clip pose by pose until it completes (then shows it) or the frame's
// budget runs out (it resumes next frame, the units drawn in the rest pose meanwhile).
void RtsoWorldView::crowd_job_bake_poses() {
	CrowdBakeJob &j = *crowd_job_;
	if (j.clip < 0) return;
	const int c = j.clip;
	const int n = CROWD_FRAMES_PER_CLIP;
	while (j.frame < n) {
		if (crowd_bake_over_budget()) return;
		crowd_baked_this_frame_ = true;
		const int f = j.frame++;
		double t = 0.0;
		if (j.len > 0.0) {
			if (CROWD_DEATH_SLOTS & (1ull << c)) {
				t = (n > 1) ? j.len * (double)f / (double)(n - 1) : 0.0;
			} else {
				t = j.len * (double)f / (double)n;
			}
		}
		j.ap->seek(t, true);
		j.ap->advance(0.0);
		Ref<ArrayMesh> baked = rtso::ModelLibrary::bake_crowd_skin(j.skin);
		if (baked.is_valid()) j.poses[j.valid++] = baked;
	}
	if (j.prop) detach_carry_props(find_skeleton3d(j.inst));
	j.prop = nullptr;
	j.skin = rtso::ModelLibrary::CrowdSkin();
	CrowdBatch &b = crowd_batch((uint8_t)(j.batch >> 16), (uint16_t)(j.batch & 0xFFFF));
	CrowdPuppet &row = b.puppet[c];
	for (int f = 0; f < j.valid; ++f) row.posed_frames[f] = j.poses[f];
	for (int f = 0; f < n; ++f) j.poses[f].unref();
	row.baked_count = j.valid;
	for (int p = 0; p < CROWD_PHASES; ++p) row.last_frame[p] = -1;
	for (int p = 0; p < CROWD_PHASES; ++p) {
		if (b.mm[c][p].is_valid() && row.posed_frames[0].is_valid()) b.mm[c][p]->set_mesh(row.posed_frames[0]);
	}
	j.clip = -1;
}

// Frees the job's model instance (between clips only).
void RtsoWorldView::crowd_job_release() {
	CrowdBakeJob &j = *crowd_job_;
	if (!j.inst) return;
	if (j.ap) j.ap->stop();
	crowd_bake_root->remove_child(j.inst);
	j.inst->queue_free();
	j.inst = nullptr;
	j.ap = nullptr;
}

void RtsoWorldView::ensure_crowd_frames(uint8_t civ, uint16_t key, const int *counts) {
	static_assert(CROWD_CLIP_COUNT == CROWD_FORM_SLOT + (CLIP_COUNT - CLIP_FORM_FIRST),
			"CROWD_CLIP_COUNT must cover the formation clips (crowd_clips)");
	CrowdBatch &b = crowd_batch(civ, key);
	const uint8_t utype = crowd_key_utype_[key < CROWD_KEY_COUNT ? key : 0];
	if (b.no_scene) return;
	bool any = false;
	for (int c = 0; c < CROWD_CLIP_COUNT && !any; ++c) any = counts[c] > 0 && !b.clip_baked[c];
	if (!any || crowd_bake_over_budget()) return;
	if (!crowd_job_) crowd_job_ = std::make_unique<CrowdBakeJob>();
	CrowdBakeJob &j = *crowd_job_;
	// A clip mid-bake finishes first (sync resumes it before the batches).
	if (j.clip >= 0) return;
	const uint32_t id = (uint32_t)civ << 16 | key;
	if (j.inst && j.batch != id) crowd_job_release();
	// A fresh instance starts its first clip whatever the budget, so batches cannot take
	// the instance from each other frame after frame without baking.
	bool fresh = false;
	if (!j.inst) {
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
		j.inst = inst;
		j.batch = id;
		j.female = unit_is_female(utype);
		j.ap = find_anim_player(inst);
		ensure_runtime_clips(j.ap, j.female);
		if (j.ap) {
			j.ap->set_active(true);
			j.ap->set_callback_mode_process(AnimationMixer::ANIMATION_CALLBACK_MODE_PROCESS_MANUAL);
		}
		fresh = true;
	}
	for (int c = 0; c < CROWD_CLIP_COUNT; ++c) {
		if (counts[c] <= 0 || b.clip_baked[c]) continue;
		if (!fresh && crowd_bake_over_budget()) return;
		fresh = false;
		crowd_job_begin_clip(b, c);
		crowd_job_bake_poses();
		if (j.clip >= 0) return;
	}
	crowd_job_release();
}

// 0.28 picks an animal's mesh variant by frequency (equal for every actor with
// looks); each animal keeps one by its id, apart from its coat's draw.
int RtsoWorldView::fauna_row(uint8_t ftype, uint32_t id) const {
	const uint32_t n = 1u + fauna_looks_[ftype];
	const uint32_t pick = ((id * 0x9E3779B1u) >> 16) % n;
	return pick == 0 ? ftype : fauna_look_first_[ftype] + (int)pick - 1;
}

void RtsoWorldView::ensure_fauna_frames(int row) {
	if (row < 0 || row >= fauna_rows_) return;
	if (fauna_puppet_baked[row]) return;
	fauna_puppet_baked[row] = true;

	String cat_units = "units";
	auto entry = rtso::ModelLibrary::get(cat_units, fauna_row_model_[row]);
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
	uint8_t want[FAUNA_CLIP_COUNT] = { CLIP_IDLE, CLIP_WALK, CLIP_ATTACK, CLIP_DEATH, CLIP_IDLE2, CLIP_DEATH2, CLIP_FEEDING };
	if (ap && ap->has_animation(clip_name(CLIP_RUN))) want[1] = CLIP_RUN;
	// The second idle and death, and feeding, only when the model has them (with_fauna_variant).
	const bool needed[FAUNA_CLIP_COUNT] = { true, true, true, true,
			ap && ap->has_animation(clip_name(CLIP_IDLE2)), ap && ap->has_animation(clip_name(CLIP_DEATH2)),
			ap && ap->has_animation(clip_name(CLIP_FEEDING)) };
	bool done[FAUNA_CLIP_COUNT] = {};
	bake_puppet_clips(inst, fauna_puppet[row], done, want, needed, FAUNA_CLIP_COUNT, FAUNA_DEATH_SLOTS);
	for (int c = 0; c < FAUNA_CLIP_COUNT; ++c) {
		if (fauna_mm[row][c].is_valid() && fauna_puppet[row][c].posed_frames[0].is_valid()) {
			fauna_mm[row][c]->set_mesh(fauna_puppet[row][c].posed_frames[0]);
		}
	}
	crowd_bake_root->remove_child(inst);
	inst->queue_free();
}

// A unit model's first draw reads its GLB, tens of milliseconds on the frame. Twice a
// second, the models of units that exist but aren't drawn yet (in the fog, in a
// structure) and of units in the production queues start loading on a worker.
void RtsoWorldView::prefetch_unit_models(const rtso::Simulation *sim, const rtso::Simulation::UnitView &uview, float delta) {
	prefetch_poll_ -= delta;
	if (prefetch_poll_ > 0.0f) return;
	prefetch_poll_ = 0.5f;
	if (prefetched_.size() != (size_t)rtso::CIV_COUNT * CROWD_KEY_COUNT) prefetched_.assign((size_t)rtso::CIV_COUNT * CROWD_KEY_COUNT, false);
	auto want = [&](uint8_t civ, uint16_t key) {
		if (civ >= rtso::CIV_COUNT) civ = rtso::CIV_ATHENIANS;
		if (key >= CROWD_KEY_COUNT || crowd_[civ][key]) return;
		std::vector<bool>::reference done = prefetched_[(size_t)civ * CROWD_KEY_COUNT + key];
		if (done) return;
		done = true;
		rtso::ModelLibrary::prefetch("units", crowd_key_model(civ, key));
	};
	for (size_t i = 0; i < uview.count; ++i) {
		uint8_t t = uview.unit_type[i];
		if (t >= rtso::UNIT_TYPE_COUNT) t = rtso::UNIT_VILLAGER;
		const uint8_t civ = sim->team_civ(uview.team[i]);
		const uint8_t dt = drawn_unit_type(t, uview.tmpl[i], uview.id[i]);
		want(civ, crowd_key_of(civ, dt, uview.pack_state[i], uview.tmpl[i], uview.id[i], uview.rank[i]));
		// Its promotion draws the _a or _e actor.
		if (uview.rank[i] < 3) want(civ, crowd_key_of(civ, dt, uview.pack_state[i], uview.tmpl[i], uview.id[i], uview.rank[i] + 1));
	}
	// Structures, drawn first when they come into sight: each one's model, its kind's
	// foundation and scaffold (its owner builds more of that kind), and a damaged one's rubble.
	if (!rubble_loaded_) load_rubble();
	if (!foundations_loaded_) load_foundations();
	auto want_building = [&](const String &id) {
		if (id.is_empty() || !building_prefetched_.insert(id.utf8().get_data()).second) return;
		rtso::ModelLibrary::prefetch("buildings", id);
	};
	for (const auto &b : sim->buildings()) {
		if (b.wrecked) continue;
		const uint8_t civ = sim->team_civ(b.team);
		const uint32_t kind = (uint32_t)civ << 16 | b.type << 1;
		const bool damaged = b.hp < b.max_hp;
		want_building(building_visual_id(civ, b.type, b.half_size));
		if (!building_kinds_prefetched_.insert(kind | (uint32_t)damaged).second) continue;
		const std::string civ_type = std::string(rtso::civ_info(civ < rtso::CIV_COUNT ? civ : rtso::CIV_ATHENIANS).code) + "/" + std::to_string(b.type);
		if (!damaged) {
			const auto fo = foundation_of_.find(civ_type);
			const auto fa = fo == foundation_of_.end() ? foundation_art_.end() : foundation_art_.find(fo->second);
			if (fa != foundation_art_.end()) {
				want_building(fa->second.id);
				want_building(fa->second.scaffold);
			}
			if (const char *f = rtso::civ_building_foundation(b.type)) want_building(String(f));
			if (const char *f = rtso::civ_building_scaffold(b.type)) want_building(String(f));
		} else {
			const auto ro = rubble_of_.find(civ_type);
			if (ro != rubble_of_.end()) want_building(String(ro->second.c_str()));
			if (const char *r = rtso::civ_building_rubble(civ, b.type)) want_building(String(r));
		}
	}
	for (const auto &b : sim->buildings()) {
		for (const rtso::ProductionOrder &o : b.queue) {
			if (o.is_tech() || o.unit_type >= rtso::UNIT_TYPE_COUNT) continue;
			const uint8_t civ = sim->team_civ(b.team);
			want(civ, crowd_key_of(civ, o.unit_type, 0, o.tmpl, 0, 1));
		}
	}
}

// 0.28 loads a match's models behind the loading page. Loads the ones the first frame
// draws (the units the viewer sees, the structures it has seen), reporting how far it got,
// so the first frame after a load (a save, a rewind) finds them cached. The rest (units in
// the fog, next ranks, foundations, rubble) are prefetch_unit_models' to read ahead once
// the match runs: requested all at once here, those worker loads slowed these ones ~3x.
void RtsoWorldView::model_boxes(const std::vector<uint32_t> &ids, std::vector<ModelBoxes> &out) const {
	out.clear();
	for (uint32_t id : ids) {
		Node3D *root = nullptr;
		Node *models[3] = {};
		if (auto it = unit_slot.find(id); it != unit_slot.end() && it->second < unit_pool.size()) {
			root = unit_pool[it->second].root;
			models[0] = unit_pool[it->second].model;
		} else if (auto bt = building_slot.find(id); bt != building_slot.end() && bt->second < building_pool.size()) {
			const BuildingVis &v = building_pool[bt->second];
			root = v.root;
			models[0] = v.model;
			models[1] = v.foundation;
			models[2] = v.scaffold;
		}
		if (!root || !root->is_visible_in_tree()) continue;
		const Transform3D xf = root->get_global_transform();
		const Transform3D to_local = xf.affine_inverse();
		ModelBoxes b{ AABB(), xf, AABB() };
		bool any = false;
		for (Node *m : models) {
			if (!m) continue;
			TypedArray<Node> meshes = m->find_children("*", "MeshInstance3D", true, false);
			if (MeshInstance3D *self = Object::cast_to<MeshInstance3D>(m)) meshes.push_back(self);
			for (int k = 0; k < meshes.size(); ++k) {
				MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(meshes[k]);
				if (!mi || !mi->is_visible_in_tree()) continue;
				const AABB a = mi->get_aabb();
				if (a.size == Vector3()) continue;
				const Transform3D g = mi->get_global_transform();
				const AABB w = g.xform(a), l = (to_local * g).xform(a);
				b.world = any ? b.world.merge(w) : w;
				b.local = any ? b.local.merge(l) : l;
				any = true;
			}
		}
		if (any) out.push_back(b);
	}
}

void RtsoWorldView::warm_models(const rtso::Simulation *sim, const std::function<void(int, int)> &progress) {
	if (!sim) return;
	const auto uview = sim->units();
	std::vector<std::pair<const char *, String>> wanted;
	std::unordered_set<std::string> seen;
	auto add = [&](const char *cat, const String &id) {
		if (!id.is_empty() && seen.insert(std::string(cat) + "/" + id.utf8().get_data()).second) wanted.emplace_back(cat, id);
	};
	for (size_t i = 0; i < uview.count; ++i) {
		if (!rtso::los_sees_unit(*sim, local_player_, uview.pos_x[i], uview.pos_z[i], uview.team[i])) continue;
		uint8_t t = uview.unit_type[i];
		if (t >= rtso::UNIT_TYPE_COUNT) t = rtso::UNIT_VILLAGER;
		uint8_t civ = sim->team_civ(uview.team[i]);
		if (civ >= rtso::CIV_COUNT) civ = rtso::CIV_ATHENIANS;
		const uint16_t key = crowd_key_of(civ, drawn_unit_type(t, uview.tmpl[i], uview.id[i]), uview.pack_state[i], uview.tmpl[i], uview.id[i], uview.rank[i]);
		if (key < CROWD_KEY_COUNT && !crowd_[civ][key]) add("units", crowd_key_model(civ, key));
	}
	mirages_.update(*sim, local_player_);
	for (const auto &fb : mirages_.buildings())
		add("buildings", building_visual_id(fb.civ, fb.type, fb.half_size));
	for (size_t i = 0; i < wanted.size(); ++i) {
		rtso::ModelLibrary::get(String(wanted[i].first), wanted[i].second);
		if (progress && (i % 8 == 7 || i + 1 == wanted.size())) progress((int)i + 1, (int)wanted.size());
	}
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
	apply_vis_tools(v, clip);
}

// Biped skeletons (46 bones) share 0 A.D.'s gatherer and promotion clips;
// art/variants/biped/{,female_}carry_*.xml and infantry promotion.
void RtsoWorldView::ensure_runtime_clips(AnimationPlayer *ap, bool female) {
	if (!ap) return;
	Ref<AnimationLibrary> lib = ap->get_animation_library(StringName());
	if (!lib.is_valid() || !clip_libs_done_.insert((int64_t)lib->get_instance_id()).second) return;
	// The bone tracks of a shipped clip give the skeleton's path from the player's root.
	String skel_path;
	Skeleton3D *sk = clip_skeleton(ap, lib, skel_path);
	if (!sk) return;
	struct Clip { ClipId id; const char *male; const char *female; };
	static const Clip kClips[] = {
		{ CLIP_CARRY_FOOD, "biped_gatherer_carry_grain_m", "biped_gatherer_carry_grain_f" },
		{ CLIP_CARRY_MEAT, "biped_gatherer_carry_meat_m", "biped_gatherer_carry_meat_f" },
		{ CLIP_CARRY_WOOD, "carry_wood", "biped_gatherer_carry_wood_f" },
		{ CLIP_CARRY_STONE, "carry_ore", "biped_gatherer_carry_ore_f" },
		{ CLIP_CARRY_METAL, "carry_ore", "biped_gatherer_carry_ore_f" },
		{ CLIP_PROMOTION, "biped_infantry_promotion", "biped_infantry_promotion" },
		{ CLIP_GATHER_MEAT, "gather_meat", "gather_meat" },
		{ CLIP_SLAUGHTER, "slaughter", "slaughter" },
	};
	for (const Clip &c : kClips) {
		if (lib->has_animation(clip_name(c.id))) continue;
		add_psa_clip(lib, sk, skel_path, clip_name(c.id), female ? c.female : c.male);
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

// Mine models per biome: the stoneLarge, stoneSmall, metalLarge and metalSmall templates
// actors of the 0 A.D. random-map biome each RTS0 biome follows (maps/random/rmbiome/generic/
// *.json, named after each row; apply_biome_environment uses the same pairing). Models are
// assets/models/props/mine_<actor>.glb (tools/extract_0ad_assets.py ZIP_PROPS), at 0.28 size.
void RtsoWorldView::pick_mine_models(const std::string &biome) {
	struct Set { const char *biome; const char *ids[MINE_KINDS]; }; // metal large, metal small, stone large, stone small
	static const Set kSets[] = {
		{ "mediterranean", { "metal_aegean_round", "metal_aegean_small", "stonemine_aegean_large", "stonemine_aegean_small" } }, // aegean
		{ "aegean", { "metal_aegean_round", "metal_aegean_small", "stonemine_aegean_large", "stonemine_aegean_small" } }, // aegean
		{ "latium", { "metal_temperate_round", "metal_temperate_small", "stonemine_temperate_quarry_01", "stonemine_granite" } }, // temperate
		{ "temperate", { "metal_temperate_round", "metal_temperate_small", "stonemine_temperate_quarry_01", "stonemine_granite" } }, // temperate
		{ "alpine", { "metal_temperate_square", "metal_temperate_small", "stonemine_temperate_quarry_01", "stonemine_granite" } }, // alpine
		{ "polar", { "metalmine_polar_round_snow", "metal_aegean_small", "stonemine_granite_large", "stonemine_granite" } }, // arctic
		{ "volcanic", { "metal_temperate_round", "metal_temperate_small", "stonemine_temperate_quarry", "stonemine_granite" } }, // autumn
		{ "autumn", { "metal_temperate_round", "metal_temperate_small", "stonemine_temperate_quarry", "stonemine_granite" } }, // autumn
		{ "steppe", { "metal_temperate_square", "metal_temperate_small", "stonemine_temperate_quarry", "stonemine_granite" } }, // steppe
		{ "india", { "metal_india_round", "metal_india_small", "stonemine_india_large", "stonemine_india_small" } }, // india
		{ "tropical", { "metal_india_round", "metal_india_small", "stonemine_india_large", "stonemine_india_small" } }, // india
		{ "savanna", { "metal_savanna_round", "metal_savanna_small", "stonemine_savanna_formations", "stonemine_savanna_small" } }, // savanna
		{ "persia", { "metal_savanna_round", "metal_savanna_small", "stonemine_savanna_formations", "stonemine_savanna_small" } }, // nubia
		{ "nubia", { "metal_savanna_round", "metal_savanna_small", "stonemine_savanna_formations", "stonemine_savanna_small" } }, // nubia
		{ "desert", { "metal_sahara_round", "metal_sahara_small", "stonemine_desert_quarry", "stonemine_desert_small" } }, // sahara
		{ "oasis", { "metal_sahara_round", "metal_sahara_small", "stonemine_desert_quarry", "stonemine_desert_small" } }, // sahara
	};
	const Set *set = &kSets[0];
	for (const auto &x : kSets)
		if (biome == x.biome) { set = &x; break; }
	mine_biome_ = biome;
	for (int k = 0; k < MINE_KINDS; ++k) {
		auto e = rtso::ModelLibrary::get("props", String("mine_") + set->ids[k]);
		if (!e.mesh.is_valid()) {
			const bool metal = k == MINE_METAL_LARGE || k == MINE_METAL_SMALL;
			e = rtso::ModelLibrary::get("vegetation", metal ? "gold_vein" : "stone_mine");
		}
		if (!e.mesh.is_valid()) continue;
		enable_instance_tint(e.mesh);
		mine_mm[k]->set_instance_count(0);
		mine_mm[k]->set_mesh(e.mesh);
		mine_y[k] = e.y_offset;
	}
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
		{ "autumn", { { "oak", 1.15f }, { "tree", 1.0f }, { "oak", 1.0f } } },
		{ "nubia", { { "acacia", 0.6f }, { "baobab", 0.45f }, { "palm", 1.0f } } },
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
		rtso::ModelLibrary::Entry models[TREE_VARIANTS];
		const int n = tree_family_models(kd.id, models, TREE_VARIANTS);
		if (n == 0) continue;
		const int slot = tree_kind_count++;
		tree_scale[slot] = kd.scale;
		tree_variants[slot] = n;
		for (int v = 0; v < n; ++v) {
			enable_instance_tint(models[v].mesh);
			tree_mm[slot][v]->set_instance_count(0);
			tree_mm[slot][v]->set_mesh(wind_mesh(models[v].mesh));
			tree_y[slot][v] = models[v].y_offset * kd.scale;
		}
	}
	if (tree_kind_count == 0) {
		tree_kind_count = 1;
		tree_scale[0] = 1.0f;
		tree_variants[0] = 1;
		tree_y[0][0] = rtso::ModelLibrary::get("vegetation", "tree").y_offset;
	}
	for (int k = 0; k < TREE_KINDS; ++k)
		for (int v = k < tree_kind_count ? tree_variants[k] : 0; v < TREE_VARIANTS; ++v) tree_mm[k][v]->set_instance_count(0);
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

// 0 A.D.'s sky box for the SkySet (assets/textures/skies, written by
// tools/extract_sky_boxes.py), turned so its bright "left" face lies toward the
// sun as SkyManager::RenderSky does. "default" names no folder in 0 A.D.; it
// takes one of the everyday sets, fixed per map. False keeps the procedural sky.
bool RtsoWorldView::apply_sky_box(const Ref<Sky> &sky, const std::string &set, const std::string &map_id, float sun_yaw, const Color &ground) {
	String name = String(set.c_str()).replace(" ", "_");
	if (name.is_empty() || name == "default") {
		static const char *const kEveryday[] = { "cirrus", "cumulus", "sunny" };
		name = kEveryday[String(map_id.c_str()).hash() % 3];
	}
	const String dir = "res://assets/textures/skies/" + name + "/";
	if (!ResourceLoader::get_singleton()->exists(dir + String("top.png"))) return false;
	if (sky_box_shader_.is_null()) sky_box_shader_ = ResourceLoader::get_singleton()->load("res://shaders/sky_box.gdshader");
	if (sky_box_shader_.is_null()) return false;
	Ref<ShaderMaterial> mat;
	mat.instantiate();
	mat->set_shader(sky_box_shader_);
	for (const char *face : { "front", "back", "left", "right", "top" }) {
		Ref<Texture2D> tex = ResourceLoader::get_singleton()->load(dir + String(face) + String(".png"));
		if (tex.is_null()) return false;
		mat->set_shader_parameter(String("face_") + face, tex);
	}
	mat->set_shader_parameter("sky_yaw", (float)Math_PI * 0.5f - sun_yaw);
	mat->set_shader_parameter("ground_color", ground);
	sky->set_material(mat);
	return true;
}

// Per-map sun, ambient light, fog, sky and water, from the Environment block
// of the 0 A.D. random-map biome each RTS0 biome follows
// (maps/random/rmbiome/generic/*.json, named after each row).
void RtsoWorldView::apply_biome_environment(const std::string &biome, const std::string &map_id, uint8_t daytime) {
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
		{ "autumn", "default", { 1.032f, 0.990f, 0.866f }, 0.446f, -0.488f, { 0.365f, 0.376f, 0.420f }, { 0.420f, 0.365f, 0.314f }, { 0.502f, 0.502f, 0.247f }, 0.97f, { 0.894f, 0.894f, 0.894f } }, // autumn
		{ "nubia", "default", { 1.100f, 1.060f, 0.992f }, 0.700f, -1.700f, { 0.354f, 0.362f, 0.389f }, { 0.337f, 0.247f, 0.078f }, { 0.337f, 0.247f, 0.078f }, 0.96f, { 0.929f, 0.875f, 0.839f } }, // nubia
	};
	const Env *e = &kEnvs[0];
	for (const auto &x : kEnvs)
		if (biome == x.biome) { e = &x; break; }
	// A ported 0 A.D. scenario carries its own Environment block
	// (tools/port_0ad_maps.py "environment"); it wins over the biome's.
	Env own = *e;
	std::string own_sky;
	{
		const String path = "res://data/maps/" + String(map_id.c_str()) + ".json";
		Ref<FileAccess> f = FileAccess::file_exists(path) ? FileAccess::open(path, FileAccess::READ) : Ref<FileAccess>();
		const Variant root = f.is_valid() ? JSON::parse_string(f->get_as_text()) : Variant();
		const Dictionary env = root.get_type() == Variant::DICTIONARY ? Dictionary(Dictionary(root).get("environment", Dictionary())) : Dictionary();
		if (!env.is_empty()) {
			auto rgb = [&](const char *key, Rgb &out) {
				const Array a = env.get(key, Array());
				if (a.size() == 3) out = { (float)a[0], (float)a[1], (float)a[2] };
			};
			rgb("sun_color", own.sun);
			rgb("ambient_color", own.ambient);
			rgb("fog_color", own.fog);
			rgb("water_color", own.water_color);
			rgb("water_tint", own.water_tint);
			own.sun_elevation = (float)env.get("sun_elevation", own.sun_elevation);
			own.sun_rotation = (float)env.get("sun_rotation", own.sun_rotation);
			own.murkiness = (float)env.get("water_murkiness", own.murkiness);
			own_sky = String(env.get("sky_set", String(own.sky))).utf8().get_data();
			own.sky = own_sky.c_str();
			e = &own;
		}
	}
	// Game Setup Daytime (Simulation::map_daytime): the themes danubius.js and
	// polar_sea.js set for each option; the sky set comes before apply_sky_box.
	if (map_id == "random_danubius" && daytime == 1) { // day
		own = *e;
		own.sky = "cumulus";
		own.sun = { 0.9f, 0.8f, 0.5f };
		own.water_color = { 0.317f, 0.396f, 0.294f };
		own.water_tint = { 0.439f, 0.403f, 0.262f };
		e = &own;
	} else if (map_id == "random_danubius" && daytime == 2) { // night
		own = *e;
		own.sky = "dark";
		own.sun = { 0.4f, 0.9f, 1.2f };
		own.sun_elevation = 0.13499f;
		own.sun_rotation = -2.5f;
		own.ambient = { 0.25f, 0.3f, 0.45f };
		own.fog = { 0.35f, 0.45f, 0.5f };
		own.water_color = { 0.074f, 0.101f, 0.090f };
		own.water_tint = { 0.129f, 0.160f, 0.137f };
		e = &own;
	} else if (map_id == "random_polar_sea" && daytime == 1) { // dawn
		own = *e;
		own.sky = "sunset 1";
		own.sun = { 0.8f, 0.7f, 0.6f };
		own.ambient = { 0.7f, 0.6f, 0.7f };
		own.sun_elevation = (float)Math_PI * (1.0f / 24.0f + 1.0f / 7.0f) * 0.5f; // randFloat(1/24, 1/7)'s middle
		e = &own;
	}
	auto col = [](const Rgb &c, float k = 1.0f) { return Color(c.r * k, c.g * k, c.b * k, 1.0f); };

	// 0 A.D. gives the sun's height above the horizon and its bearing;
	// above 90 degrees it shines from the other side. Bearings are taken
	// relative to the Aegean sun, which matches the scene's default light.
	float sun_elev = e->sun_elevation, sun_rot = e->sun_rotation;
	if (sun_elev > Math_PI * 0.5f) {
		sun_elev = (float)Math_PI - sun_elev;
		sun_rot += (float)Math_PI;
	}
	sun_elev = Math::clamp(sun_elev, 0.35f, 1.3f);
	const float sun_yaw = Math::deg_to_rad(45.0f) + (sun_rot - (-0.909f));

	Node *scene = get_parent();
	if (scene) {
		for (int i = 0; i < scene->get_child_count(); ++i) {
			if (DirectionalLight3D *sun = Object::cast_to<DirectionalLight3D>(scene->get_child(i))) {
				const float peak = std::max(e->sun.r, std::max(e->sun.g, e->sun.b));
				sun->set_color(col(e->sun, 1.0f / peak));
				sun->set_param(Light3D::PARAM_ENERGY, 1.25f * peak);
				sun->set_rotation(Vector3(-sun_elev, sun_yaw, 0.0f));
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
				if (sky.is_valid() && apply_sky_box(sky, e->sky, map_id, sun_yaw, col(e->fog))) {
					// 0 A.D. draws its sky unfogged; the faces carry their own haze.
					env->set_fog_sky_affect(0.1f);
					continue;
				}
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

namespace {

// 0 A.D. terrain name -> texture key (data/terrain/terrain_textures.json, written
// by tools/map_0ad_terrains.py); the textures are assets/textures/terrain/<key>.png.
const Dictionary &terrain_texture_table() {
	static Dictionary &table = *new Dictionary();
	static bool loaded = false;
	if (!loaded) {
		loaded = true;
		Ref<FileAccess> f = FileAccess::open("res://data/terrain/terrain_textures.json", FileAccess::READ);
		if (f.is_valid()) {
			const Variant v = JSON::parse_string(f->get_as_text());
			if (v.get_type() == Variant::DICTIONARY) table = v;
		}
	}
	return table;
}

// One layer of the map's terrain texture array: the named 0 A.D. terrain at
// kTerrainLayerSize with mipmaps (every layer of a Texture2DArray shares a size
// and format), a plain earth tone when the texture is missing.
constexpr int kTerrainLayerSize = 256;
Ref<Image> terrain_layer_image(const String &name) {
	static auto &cache = *new std::unordered_map<std::string, Ref<Image>>();
	const std::string key_s = name.utf8().get_data();
	if (auto it = cache.find(key_s); it != cache.end()) return it->second;
	const String key = terrain_texture_table().get(name, name);
	const String path = "res://assets/textures/terrain/" + key + ".png";
	Ref<Image> img;
	if (ResourceLoader::get_singleton()->exists(path)) {
		Ref<Texture2D> tex = ResourceLoader::get_singleton()->load(path);
		if (tex.is_valid()) img = tex->get_image();
	}
	if (img.is_null() || img->is_empty()) img = Image::load_from_file(ProjectSettings::get_singleton()->globalize_path(path));
	if (img.is_null() || img->is_empty()) {
		img = Image::create_empty(kTerrainLayerSize, kTerrainLayerSize, false, Image::FORMAT_RGBA8);
		img->fill(Color(0.46f, 0.42f, 0.33f));
	} else {
		img = img->duplicate();
		if (img->is_compressed()) img->decompress();
		img->clear_mipmaps();
		img->convert(Image::FORMAT_RGBA8);
		if (img->get_width() != kTerrainLayerSize || img->get_height() != kTerrainLayerSize)
			img->resize(kTerrainLayerSize, kTerrainLayerSize, Image::INTERPOLATE_BILINEAR);
	}
	img->generate_mipmaps();
	cache[key_s] = img;
	return img;
}

// 0 A.D.'s standard terrain alpha maps (art/textures/terrain/alphamaps/standard),
// in terrain_blend::ALPHA_MAP_NAMES order, as one array; null if any is missing.
Ref<Texture2DArray> terrain_alpha_maps() {
	TypedArray<Image> maps;
	for (const char *name : rtso::terrain_blend::ALPHA_MAP_NAMES) {
		const String path = String("res://assets/textures/terrain/alphamaps/standard/") + name + ".png";
		Ref<Image> img;
		if (ResourceLoader::get_singleton()->exists(path)) {
			Ref<Texture2D> tex = ResourceLoader::get_singleton()->load(path);
			if (tex.is_valid()) img = tex->get_image();
		}
		if (img.is_null() || img->is_empty()) img = Image::load_from_file(ProjectSettings::get_singleton()->globalize_path(path));
		if (img.is_null() || img->is_empty()) return Ref<Texture2DArray>();
		img = img->duplicate();
		if (img->is_compressed()) img->decompress();
		img->clear_mipmaps();
		img->convert(Image::FORMAT_L8);
		if (img->get_width() != 64 || img->get_height() != 64) img->resize(64, 64, Image::INTERPOLATE_BILINEAR);
		maps.push_back(img);
	}
	Ref<Texture2DArray> arr;
	arr.instantiate();
	return arr->create_from_images(maps) == OK ? arr : Ref<Texture2DArray>();
}

// Hands the shader the sim's per-tile terrain (0 A.D. CTerrain tiles): a texture
// array of the map's terrains and an R8 texture of each tile's layer.
// tile_res 0 keeps the biome painting.
void apply_terrain_tiles(const Ref<ShaderMaterial> &mat, const rtso::Simulation *sim) {
	if (mat.is_null()) return;
	const auto &tt = sim->terrain_tiles();
	if (tt.empty()) {
		mat->set_shader_parameter("tile_res", 0.0f);
		return;
	}
	TypedArray<Image> layers;
	for (const std::string &n : tt.names) layers.push_back(terrain_layer_image(String::utf8(n.c_str())));
	Ref<Texture2DArray> arr;
	arr.instantiate();
	if (arr->create_from_images(layers) != OK) {
		mat->set_shader_parameter("tile_res", 0.0f);
		return;
	}
	PackedByteArray bytes;
	bytes.resize((int64_t)tt.tiles.size());
	std::memcpy(bytes.ptrw(), tt.tiles.data(), tt.tiles.size());
	Ref<Image> idx = Image::create_from_data(tt.res, tt.res, false, Image::FORMAT_R8, bytes);
	const std::vector<uint8_t> blends = rtso::terrain_blend::build(tt.res, tt.tiles.data());
	PackedByteArray blend_bytes;
	blend_bytes.resize((int64_t)blends.size());
	std::memcpy(blend_bytes.ptrw(), blends.data(), blends.size());
	Ref<Image> blend_img = Image::create_from_data(tt.res * rtso::terrain_blend::MAX_BLENDS, tt.res, false, Image::FORMAT_RGBA8, blend_bytes);
	mat->set_shader_parameter("tile_layers", arr);
	mat->set_shader_parameter("tile_index", ImageTexture::create_from_image(idx));
	mat->set_shader_parameter("tile_blends", ImageTexture::create_from_image(blend_img));
	mat->set_shader_parameter("alpha_maps", terrain_alpha_maps());
	mat->set_shader_parameter("tile_res", (float)tt.res);
	mat->set_shader_parameter("tile_half", sim->map_half());
}

} // namespace

// The minimap's terrain colours read the same layers (minimap_view.cpp).
Ref<Image> rtso_terrain_layer_image(const String &name) { return terrain_layer_image(name); }

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
		tm->set_shader_parameter("water_level", sim->water_height() > -0.5f ? drawn_water_height(sim) : -1000.0f);
	apply_terrain_tiles(terrain->get_material_override(), sim);
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
				// Mipmapped copies of 0 A.D.'s lake normals: the import keeps
				// none, and distant water then aliases into a regular grid.
				for (const char *slot : { "wave_normal_a", "wave_normal_b" }) {
					const String path = String(slot) == "wave_normal_a" ? "res://assets/textures/water/lake_normal0001.png"
					                                                      : "res://assets/textures/water/lake_normal0031.png";
					Ref<Texture2D> src = ResourceLoader::get_singleton()->exists(path) ? Ref<Texture2D>(ResourceLoader::get_singleton()->load(path)) : Ref<Texture2D>();
					Ref<Image> img = src.is_valid() ? src->get_image() : Ref<Image>();
					if (img.is_null()) continue;
					if (img->is_compressed()) img->decompress();
					img->generate_mipmaps();
					water_shader_mat->set_shader_parameter(slot, ImageTexture::create_from_image(img));
				}
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
		water_mesh_node->set_position(Vector3(0.0f, drawn_water_height(sim), 0.0f));
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
			wsm->set_shader_parameter("water_level", drawn_water_height(sim));
		}
		water_mesh_node->set_visible(true);
	} else if (water_mesh_node) {
		water_mesh_node->set_visible(false);
	}
	apply_biome_environment(sim->current_map_biome(), sim->current_map_id(), sim->map_daytime());
}

// The silhouettes option, read again whenever the Options page or its hotkey
// changes a setting: every unit batch and close-up model gets or drops the overlay.
void RtsoWorldView::refresh_silhouettes() {
	if (silhouette_gen_ == rtso::user_config::generation()) return;
	silhouette_gen_ = rtso::user_config::generation();
	if (!silhouette_mat_.is_valid()) {
		Ref<Shader> sh = rtso::load_instanced_shader("res://shaders/silhouette.gdshader");
		if (sh.is_valid()) {
			silhouette_mat_.instantiate();
			silhouette_mat_->set_shader(sh);
			silhouette_mat_->set_shader_parameter("gl_depth", rtso::gl_compatibility());
		}
	}
	const bool on = rtso::user_config::get_bool("silhouettes") && silhouette_mat_.is_valid() && !cinema_playing_;
	silhouettes_ = on;
	const Ref<Material> overlay = on ? Ref<Material>(silhouette_mat_) : Ref<Material>();
	for (auto &per_civ : crowd_)
		for (auto &b : per_civ)
			if (b)
				for (int c = 0; c < CROWD_CLIP_COUNT; ++c)
					for (int p = 0; p < CROWD_PHASES; ++p)
						if (b->mmi[c][p]) b->mmi[c][p]->set_material_overlay(overlay);
	for (auto &per_fauna : fauna_mmi)
		for (MultiMeshInstance3D *mmi : per_fauna)
			if (mmi) mmi->set_material_overlay(overlay);
	for (UnitVis &v : unit_pool) v.silhouette_rgba = -1;
}

void RtsoWorldView::set_silhouette(Node *n, const Color &c) {
	if (!n) return;
	if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(n)) {
		const Ref<Material> overlay = silhouettes_ ? Ref<Material>(silhouette_mat_) : Ref<Material>();
		if (!rtso::is_material_or_copy(mi->get_material_overlay(), overlay)) mi->set_material_overlay(overlay);
		if (silhouettes_) rtso::set_instance_param(mi, "silhouette_color", c);
	}
	for (int i = 0; i < n->get_child_count(); ++i) set_silhouette(n->get_child(i), c);
}

// MultiMesh::set_instance_count goes to the RenderingServer every call; most batches keep their
// count from one frame to the next, and every slot is written again after.
static void set_mm_count(const Ref<MultiMesh> &mm, int n) {
	if (mm->get_instance_count() != n) mm->set_instance_count(n);
}

void RtsoWorldView::sync(const rtso::Simulation *sim, float delta, const PackedInt32Array &selected_ids) {
	if (!sim) return;
	if (delta < 0.0f) delta = 0.0f;
	if (delta > 0.05f) delta = 0.05f;
	anim_time += delta;
	unit_vis_stamp++;
	// Map triggers (Flood, Extinct Volcano) raise the water during the match.
	if (water_mesh_node && water_mesh_node->is_visible() &&
	    water_mesh_node->get_position().y != drawn_water_height(sim)) {
		water_mesh_node->set_position(Vector3(0.0f, drawn_water_height(sim), 0.0f));
	}
	if (unit_vis_stamp == 0) unit_vis_stamp = 1;
	// A cinema path playing (CCmpCinemaManager::SetEnabled) turns silhouettes and the
	// territory borders off (session.js: renderer.silhouettes, TerritoryManager SetVisibility).
	if (cinema_playing_ != sim->cinema_playing()) {
		cinema_playing_ = sim->cinema_playing();
		silhouette_gen_ = 0xFFFFFFFFu;
	}
	refresh_silhouettes();

	update_fog(sim, delta);
	if (paint_dirty_ && sim->resources().count > 0) build_ground_paint(sim);
	update_territory_borders(sim);
	if (border_mi_ && border_mi_->is_visible() == cinema_playing_) border_mi_->set_visible(!cinema_playing_);

	// Update skeletal animation controllers
	for (int t = 0; t < rtso::UNIT_TYPE_COUNT; ++t) {
		anim_controllers[t].update(delta * 1000.0f);
	}

	std::unordered_set<int32_t> sel_set;
	if (selected_ids.size() > 0) sel_set.reserve((size_t)selected_ids.size());
	for (int i = 0; i < selected_ids.size(); ++i) sel_set.insert(selected_ids[i]);

	// StatusBars.js AddAuraIcons: an aura of an entity whose status bars show
	// (GuiInterface GetEntitiesWithStatusBars) marks what it reaches with its icon.
	if (!status_sprites_built_) {
		status_sprites_.build(this);
		status_sprites_built_ = true;
	}
	status_sprites_.begin_frame(get_viewport() ? get_viewport()->get_camera_3d() : nullptr);
	status_rank_icons_.clear();
	status_xp_bars_ = 0;
	aura_icon_sources_.clear();
	for (const rtso::AuraSource &as : sim->aura_manager().sources) {
		if (!as.alive || as.aura < 0) continue;
		const int32_t sid = (int32_t)as.source_id;
		if (!show_all_status_bars_ && !sel_set.count(sid) && !sel_buildings_.count(sid) && !highlight_set_.count(sid)) continue;
		const int icon = status_sprites_.aura_icon(rtso::civ_hero_aura(as.aura).id);
		if (icon >= 0) aura_icon_sources_.push_back({ &as, icon });
	}

	Vector3 cam_pos;
	std::vector<Plane> frustum; // outward planes: inside has distance <= 0
	if (!locked_cull_frustum_.empty() && anim_cull_enabled_) {
		// Lock cull camera (GameView.cpp m_CullCamera): cull against the frozen frustum.
		frustum = locked_cull_frustum_;
	} else if (Viewport *vp = get_viewport()) {
		if (Camera3D *cam = vp->get_camera_3d()) {
			cam_pos = cam->get_global_position();
			const TypedArray<Plane> planes = cam->get_frustum();
			for (int p = 0; p < planes.size(); ++p) frustum.push_back(planes[p]);
		}
	}
	// A sphere of `radius` at `at` is (maybe) in view; true with no camera, with
	// culling off (GameView.cpp Culling), or with a locked frustum cleared.
	auto in_view = [&](const Vector3 &at, float radius) {
		if (!anim_cull_enabled_) return true;
		for (const Plane &pl : frustum)
			if (pl.distance_to(at) > radius) return false;
		return true;
	};
	const float anim_dist_sq = 24.0f * 24.0f;
	(void)anim_dist_sq;

	// =========================================================================
	// 1. Units Presentation: Real 0 A.D. models, 3D animations, and Health Bars
	// =========================================================================
	auto uview = sim->units();
	prefetch_unit_models(sim, uview, delta);
	auto rview_g = sim->resources();
	// Lock cull camera (GameView.cpp m_CullCamera): what lies outside the frozen frustum is
	// not drawn, so moving the view camera shows what the cull camera culled.
	const bool cull_locked = !locked_cull_frustum_.empty() && anim_cull_enabled_;
	auto locked_out = [&](float x, float z, float radius) {
		return cull_locked && !in_view(Vector3(x, sim->get_terrain_height(x, z), z), radius);
	};
	auto entity_visible = [&](float x, float z, uint8_t team) {
		return rtso::los_sees_unit(*sim, local_player_, x, z, team) && !locked_out(x, z, 4.0f);
	};
	// template_unit_fauna.xml RetainInFog: an animal stays drawn where it is in explored fog
	// (CCmpRangeManager VIS_FOGGED with no Fogging mirage), hidden only in unexplored land.
	auto fauna_visible = [sim, this](float x, float z) {
		return rtso::los_sees_all(*sim, local_player_) || mirages_.los_at(x, z) != rtso::Simulation::FOW_UNEXPLORED;
	};
	std::unordered_map<uint32_t, uint8_t> res_sub;
	res_sub.reserve(rview_g.count);
	for (size_t r = 0; r < rview_g.count; ++r) {
		if (rview_g.subtype && rview_g.subtype[r])
			res_sub[rview_g.id[r]] = rview_g.subtype[r];
	}

	std::unordered_set<uint32_t> detailed;
	detailed.reserve(MAX_DETAILED_UNITS * 2);
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
	// Give full skeletal animation to heroes and active units in view
	for (size_t i = 0; i < uview.count && (int)detailed.size() < MAX_DETAILED_UNITS; ++i) {
		if (entity_visible(uview.pos_x[i], uview.pos_z[i], uview.team[i])) {
			uint8_t ut = uview.unit_type[i];
			if (ut == rtso::UNIT_HERO || in_view(Vector3(uview.pos_x[i], 0.0f, uview.pos_z[i]), 8.0f)) {
				detailed.insert(uview.id[i]);
			}
		}
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
		w = with_variant(w, uview.id[i]);
		auto fv = form_variant_.find(uview.id[i]);
		if (fv != form_variant_.end()) {
			uint8_t civ = sim->team_civ(uview.team[i]);
			if (civ >= rtso::CIV_COUNT) civ = rtso::CIV_ATHENIANS;
			const uint8_t dt = drawn_unit_type(ut, uview.tmpl[i], uview.id[i]);
			const uint16_t key = crowd_key_of(civ, dt, uview.pack_state[i], uview.tmpl[i], uview.id[i], uview.rank[i]);
			if (crowd_form_mask(civ, key) & (1u << fv->second))
				w = with_formation_variant(w, uview.action_state[i], fv->second);
		}
		return w;
	};
	form_variant_.clear();
	for (const auto &fc : sim->formations())
		rtso::formvar::for_each_member_variant(fc, [this](uint32_t ent, uint8_t v) { form_variant_[ent] = v; });
	crowd_slot_scratch_.assign(uview.count, 0xFF);
	crowd_bake_deadline_us_ = bake_now_us() + CROWD_BAKE_BUDGET_US;
	crowd_baked_this_frame_ = false;
	if (crowd_job_ && crowd_job_->clip >= 0) {
		crowd_job_bake_poses();
		if (crowd_job_->clip < 0) crowd_job_release();
	}
	// Per batch and clip: units drawn (counts) and placed so far (written). Only the rows of
	// batches with units this frame are cleared and read; the rest count none.
	static int counts[rtso::CIV_COUNT][CROWD_KEY_COUNT][CROWD_CLIP_COUNT][CROWD_PHASES];
	static int total_counts[rtso::CIV_COUNT][CROWD_KEY_COUNT][CROWD_CLIP_COUNT];
	static int written[rtso::CIV_COUNT][CROWD_KEY_COUNT][CROWD_CLIP_COUNT][CROWD_PHASES];
	static const int no_units[CROWD_CLIP_COUNT][CROWD_PHASES] = {};
	static const int no_total[CROWD_CLIP_COUNT] = {};
	for (const uint32_t ck : crowd_seen_list_) crowd_seen_[ck >> 16][ck & 0xFFFF] = 0;
	crowd_seen_list_.clear();
	for (size_t i = 0; i < uview.count; ++i) {
		if (!entity_visible(uview.pos_x[i], uview.pos_z[i], uview.team[i])) continue;
		uint8_t t = uview.unit_type[i];
		if (t >= rtso::UNIT_TYPE_COUNT) t = rtso::UNIT_VILLAGER;
		t = drawn_unit_type(t, uview.tmpl[i], uview.id[i]);
		uint8_t civ = sim->team_civ(uview.team[i]);
		if (civ >= rtso::CIV_COUNT) civ = rtso::CIV_ATHENIANS;
		const int slot = crowd_slot_of(anim_of(i).clip);
		crowd_slot_scratch_[i] = (uint8_t)slot;
		const uint16_t key = crowd_key_of(civ, t, uview.pack_state[i], uview.tmpl[i], uview.id[i], uview.rank[i]);
		crowd_key_utype_[key] = t;
		if (!crowd_seen_[civ][key]) {
			crowd_seen_[civ][key] = 1;
			crowd_seen_list_.push_back((uint32_t)civ << 16 | key);
			std::memset(counts[civ][key], 0, sizeof(counts[civ][key]));
			std::memset(total_counts[civ][key], 0, sizeof(total_counts[civ][key]));
			std::memset(written[civ][key], 0, sizeof(written[civ][key]));
		}
		const int phase = (int)(uview.id[i] % (uint32_t)CROWD_PHASES);
		counts[civ][key][slot][phase]++;
		total_counts[civ][key][slot]++;
	}
	// Every batch with units this frame exists before the walk, which goes civ by civ, key by key.
	for (const uint32_t ck : crowd_seen_list_) crowd_batch((uint8_t)(ck >> 16), (uint16_t)(ck & 0xFFFF));
	for (const uint32_t ck : crowd_made_) {
		const int civ = (int)(ck >> 16), t = (int)(ck & 0xFFFF);
		const bool any = crowd_seen_[civ][t] != 0;
		const auto &row = any ? counts[civ][t] : no_units;
		const int *t_row = any ? total_counts[civ][t] : no_total;
		CrowdBatch &cb = *crowd_[civ][t];
		if (any) {
			ensure_crowd_frames((uint8_t)civ, (uint16_t)t, t_row);
			for (int c = 0; c < CROWD_CLIP_COUNT; ++c) {
				for (int p = 0; p < CROWD_PHASES; ++p) {
					if (row[c][p] > 0) crowd_mm(cb, c, p);
				}
			}
		}
		cycle_puppet_meshes(cb.puppet, cb.mm, row, CROWD_CLIP_COUNT, CROWD_DEATH_SLOTS);
		for (int c = 0; c < CROWD_CLIP_COUNT; ++c) {
			for (int p = 0; p < CROWD_PHASES; ++p) {
				if (cb.mm[c][p].is_valid()) set_mm_count(cb.mm[c][p], row[c][p]);
			}
		}
	}

	const Color col_player(0.55f, 0.72f, 1.0f);
	const Color col_enemy(1.0f, 0.52f, 0.42f);

	std::unordered_set<uint32_t> active_unit_ids;
	active_unit_ids.reserve(uview.count);

	size_t unit_rings_used = 0;
	const bool status_rank_icon = rtso::user_config::get_bool("gui.session.rankabovestatusbar");
	const bool status_xp_bar = rtso::user_config::get_bool("gui.session.experiencestatusbar");
	for (size_t i = 0; i < uview.count; ++i) {
		if (!entity_visible(uview.pos_x[i], uview.pos_z[i], uview.team[i])) continue;
		uint32_t id = uview.id[i];
		active_unit_ids.insert(id);

		bool is_player = (uview.team[i] == local_player_);
		uint8_t utype = uview.unit_type[i];
		if (utype >= rtso::UNIT_TYPE_COUNT) utype = rtso::UNIT_VILLAGER;
		utype = drawn_unit_type(utype, uview.tmpl[i], uview.id[i]);
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

		// Smooth visual rotation interpolation to prevent snapping. The unit turns towards its heading
		// (Position rotation, which the sim turns at the template's TurnRate: a unit turning on the spot,
		// or a ship drifting backwards, faces its heading). In Godot, forward is -Z: local -Z rotated by
		// atan2(-hx, -hz) points along (hx, hz).
		const float head_yaw = Math::atan2(-uview.heading_x[i], -uview.heading_z[i]);
		float yaw = head_yaw;
		auto yaw_it = crowd_yaw.find(id);
		if (yaw_it != crowd_yaw.end()) yaw = yaw_it->second;

		float target_yaw = head_yaw;
		if (speed2 <= 0.001f && (uaction == rtso::ACTION_ATTACK || uaction == rtso::ACTION_GATHER || uaction == rtso::ACTION_BUILD)) {
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
		const uint16_t ckey = crowd_key_of(uciv, utype, uview.pack_state[i], uview.tmpl[i], uview.id[i], uview.rank[i]);
		// The batch holds the model's entry values (every unit drawn this frame has one).
		const CrowdBatch &model_b = crowd_batch(uciv, ckey);
		const String &utype_str = model_b.model_id;
		const float turret_h = sim->unit_height_offset(i); // turret points stand on walls
		float ground = sim->get_terrain_height(x, z);
		// 0 A.D. ships are Position Floating: they ride the water surface, not the seabed.
		if (rtso::unit_is_ship(utype) && sim->water_height() > -0.5f) ground = std::max(ground, drawn_water_height(sim));
		float y = (turret_h > 0.0f ? turret_draw_y(sim, id, turret_h, x, z) : ground) + model_b.y_offset;
		float sc = model_b.scale > 0.05f ? model_b.scale : 1.0f;
		Transform3D xf(Basis(Vector3(0, 1, 0), yaw).scaled(Vector3(sc, sc, sc)), Vector3(x, y, z));
		const bool dying = !uview.alive[i] || uaction == rtso::ACTION_DYING;
		const float death_t = dying ? uview.death_timer[i] : 0.0f;
		const bool ship_wave = !dying && rtso::unit_is_ship(utype);
		if (ship_wave) {
			apply_ship_wave_xform(xf, id, anim_time, speed2);
		}
		if (dying) {
			apply_unit_death_xform(xf, utype, death_t, x, z, model_b.height * sc, false);
		}
		if (want.clip == CLIP_RUN && !dying) emit_run_dust(utype_str, Vector3(x, ground, z), sc, delta);
		if (!dying) emit_unit_fx(utype_str, xf, sc, delta);
		const int clip = crowd_slot_scratch_[i];
		const int phase = (int)(id % (uint32_t)CROWD_PHASES);
		MultiMesh *cmm = crowd_[uciv][ckey] ? crowd_[uciv][ckey]->mm[clip][phase].ptr() : nullptr;
		if (cmm) {
			const int slot = written[uciv][ckey][clip][phase]++;
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
			// Alpha 1 + the unit's body skin draw (player_color.gdshader variant_u).
			cmm->set_instance_custom_data(slot, Color(pc.r, pc.g, pc.b, 1.0f + 0.999f * rtso::ModelLibrary::variant_draw(id)));
		}

		// StatusBars.js on a selected, highlighted (hovered or band-boxed) or, with
		// Tab, any unit: experience, pack and upgrade progress, health, the icons of
		// the shown auras reaching it, and its rank.
		if (!dying) {
			const int32_t sid = (int32_t)id;
			const bool bars = show_all_status_bars_ || sel_set.count(sid) || highlight_set_.count(sid);
			StatusStack st;
			if (bars) {
				st.health = max_hp > 0.0f ? hp / max_hp : 1.0f;
				const float need_xp = status_xp_bar ? sim->promotion_required_xp(i) : 0.0f;
				if (need_xp > 0.0f && uview.xp[i] > 0) st.xp = (float)uview.xp[i] / need_xp; // none at 0 XP
				if (uview.pack_state[i] == rtso::PACK_PACKING || uview.pack_state[i] == rtso::PACK_UNPACKING) {
					const float pt = sim->pack_time_s(i);
					st.pack = pt > 0.0f ? uview.pack_elapsed[i] / pt : 0.0f;
				}
				if (const auto *up = sim->unit_upgrade_of(id); up && up->time > 0.0f) st.upgrade = up->elapsed / up->time;
				// AddRankIcon: Basic too, for units whose template has a rank (Identity GetRank).
				if (status_rank_icon && sim->unit_has_rank(i)) {
					const uint8_t rank = std::max<uint8_t>(uview.rank[i], 1);
					st.rank_icon = status_sprites_.icon_index(rank >= 3 ? "res://assets/textures/ui/archive/icons/ranks/Elite.png"
							: rank == 2 ? "res://assets/textures/ui/archive/icons/ranks/Advanced.png"
										: "res://assets/textures/ui/archive/icons/ranks/Basic.png");
				}
			}
			if (!aura_icon_sources_.empty())
				status_aura_icons(uview.id[i], x, z, uview.team[i], uview.tmpl ? uview.tmpl[i] : rtso::CIV_UNIT_TEMPLATE_NONE, false, st);
			if (bars || st.n_icons > 0) {
				// The GLBs are in metres, so the model's scale is world units per metre.
				const rtso::StatusBarsSize sb = unit_status_bars(utype, uview.tmpl ? uview.tmpl[i] : rtso::CIV_UNIT_TEMPLATE_NONE);
				draw_status_stack(sim, Vector3(x, y - model_b.y_offset + sb.height_offset * sc, z), sb.bar_width * sc, sb.bar_height * sc, sc, st);
			}
		}

				if (detailed.count(id) == 0) {
			bool is_selected = (sel_set.count((int32_t)id) > 0);
			const bool ringed = is_selected || highlight_set_.count((int32_t)id) > 0;
			if (ringed) {
				Decal *d = get_or_create_ring_decal(unit_rings_used++);
				const Color &mod_col = is_selected ? (is_player ? RING_SEL_COL : RING_ENEMY_COL)
				                                   : (is_player ? RING_HL_COL : RING_ENEMY_HL_COL);
				d->set_modulate(mod_col);
				const float rsize = std::max(1.1f, 1.25f * model_b.height * sc);
				d->set_size(Vector3(rsize, 2.5f, rsize));
				d->set_transform(Transform3D(Basis(), Vector3(x, ground + 0.1f, z)));
				d->set_visible(true);
			}
			continue;
		}

		UnitVis &v = acquire_unit_slot(id);
		v.vis_stamp = unit_vis_stamp;
		v.yaw = yaw;
		attach_unit_model(v, utype_str);
		if (v.variant_model != v.model || v.variant_id != id) {
			// Props first: a look added for this entity takes the colour and skin draw below.
			rtso::ModelLibrary::set_prop_looks(v.model, v.model_id, id);
			rtso::ModelLibrary::set_variant(v.model, rtso::ModelLibrary::variant_draw(id));
			v.variant_model = v.model;
			v.variant_id = id;
			v.player_rgba = -1;
			v.silhouette_rgba = -1;
		}
		push_player_color(v.model, v.player_rgba, player_display_color(sim, uview.team[i]));
		{
			const Color sc = player_display_color(sim, uview.team[i]);
			const int64_t key = silhouettes_ ? (int64_t)sc.to_rgba32() : -2;
			if (v.silhouette_rgba != key) {
				set_silhouette(v.model, sc);
				v.silhouette_rgba = key;
			}
		}
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
			ensure_form_clip(v.anim, want.clip);
			ClipId play = resolve_clip_fallback(v.anim, want.clip);
			// A borrowed Death is an idle standing in for a corpse; never loop it.
			bool loop_clip = want.loop && play == want.clip;
			if (play != CLIP_NONE) {
				play_unit_clip(v, clip_name(play), loop_clip);
				// A detailed unit out of view (a ship, a dying or selected unit
				// off screen) banks its time and catches up when seen again, so a
				// death or sail clip is where it would be. The mixers were ~11% of a
				// late-game frame with every corpse and fleet animating.
				if (in_view(Vector3(x, y, z), std::max(2.0f, 4.0f * model_b.height * sc))) {
					v.anim->advance((double)(delta + v.anim_debt));
					v.anim_debt = 0.0f;
					apply_vis_tools(v, clip_name(play));
				} else {
					v.anim_debt = std::min(v.anim_debt + delta, 10.0f);
				}
			}
			const uint8_t prop_clip = is_carry_variant(play) ? (uint8_t)play : 0;
			if (prop_clip != v.carry_clip) {
				v.carry_prop = prop_clip ? attach_carry_prop(v.model, prop_clip, unit_is_female(utype))
						: (detach_carry_props(find_skeleton3d(v.model)), nullptr);
				v.carry_clip = prop_clip;
			}
		}

		float dsc = model_b.scale > 0.05f ? model_b.scale : 1.0f;
		Transform3D dxf(Basis(Vector3(0, 1, 0), v.yaw).scaled(Vector3(dsc, dsc, dsc)),
		               Vector3(x, y, z));
		if (ship_wave) {
			apply_ship_wave_xform(dxf, id, anim_time, speed2);
		}
		if (dying) {
			apply_unit_death_xform(dxf, utype, death_t, x, z, model_b.height * dsc, has_death_clip);
		}
		v.root->set_transform(dxf);
		if (ship_wave) {
			apply_ship_oar_stroke(v.model, id, anim_time, speed2);
		}

		bool is_selected = (sel_set.count((int32_t)id) > 0);
		const bool ringed = is_selected || highlight_set_.count((int32_t)id) > 0;
		if (ringed) {
			Decal *d = get_or_create_ring_decal(unit_rings_used++);
			const Color &mod_col = is_selected ? (is_player ? RING_SEL_COL : RING_ENEMY_COL)
			                                   : (is_player ? RING_HL_COL : RING_ENEMY_HL_COL);
			d->set_modulate(mod_col);
			const float rsize = std::max(1.1f, 1.25f * model_b.height * sc);
			d->set_size(Vector3(rsize, 2.5f, rsize));
			d->set_transform(Transform3D(Basis(), Vector3(x, ground + 0.1f, z)));
			d->set_visible(true);
		}
		v.ring_on = ringed;

		// The status bars are StatusBars.js sprites (draw_status_stack).
		v.hp_bg->set_visible(false);
		v.hp_fg->set_visible(false);
		v.xp_bg->set_visible(false);
		v.xp_fg->set_visible(false);
		v.rank_icon->set_visible(false);
	}

	// Release the slots of units not drawn close up this frame. A slot already released keeps its
	// old stamp and sim id, so only the slot the id still maps to is released: a second release
	// would list it twice in unit_free (two units then share one model and rebuild it every
	// frame), and would drop the id's mapping to the slot it has taken since.
	for (size_t s = 0; s < unit_pool.size(); ++s) {
		if (unit_pool[s].root && unit_pool[s].vis_stamp != unit_vis_stamp) {
			auto it = unit_slot.find(unit_pool[s].sim_id);
			if (it == unit_slot.end() || it->second != s) continue;
			hide_unit_vis(unit_pool[s]);
			unit_slot.erase(it);
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

	// Melee, shot and impact sounds: RtsoSessionAudio::tick_combat (session_audio.h).

	// =========================================================================
	// 2. Buildings: Town Center, Barracks, Houses, Towers, Storehouses, Farms
	// =========================================================================
	// Fogging.js: enemy structures out of sight are drawn as the viewer last saw
	// them (mirages); ones it never saw stay hidden.
	const auto &buildings = mirages_.buildings();
	std::unordered_set<uint32_t> alive_buildings;
	// The live structure behind a FoggedBuilding, for its capture points and upgrade.
	std::unordered_map<uint32_t, const rtso::Simulation::Building *> live_buildings;
	auto live_building = [&](uint32_t bid) -> const rtso::Simulation::Building * {
		if (live_buildings.empty())
			for (const auto &lb : sim->buildings()) live_buildings[lb.id] = &lb;
		auto it = live_buildings.find(bid);
		return it == live_buildings.end() ? nullptr : it->second;
	};

	for (const auto &b : buildings) {
		alive_buildings.insert(b.id);
		BuildingVis &v = acquire_building_slot(b.id);
		if (v.root) v.root->set_visible(!locked_out(b.x, b.z, b.half_size * 1.5f + 4.0f));

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
				play_structure_death(*this, b.type, Vector3(b.x, base_y + 1.0f, b.z));
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
		// 0 A.D. Health/SpawnEntityOnDeath: the structure's own rubble actor (data/rubble.json),
		// laid on the entity's position through the building GLB's actor frame.
		bool own_rubble = false;
		float rubble_k = 1.0f, rubble_metre = 0.25f;
		Vector3 rubble_pos;
		if (v.phase == 4) {
			if (!rubble_loaded_) load_rubble();
			if (!foundations_loaded_) load_foundations();
			const auto ro = rubble_of_.find(std::string(rtso::civ_info(bciv < rtso::CIV_COUNT ? bciv : rtso::CIV_ATHENIANS).code) + "/" + std::to_string(b.type));
			const auto fr = building_frames_.find(building_table_key(mid));
			const auto rf = ro == rubble_of_.end() ? rubble_frames_.end() : rubble_frames_.find(ro->second);
			if (rf != rubble_frames_.end() && rf->second.scale > 0.0f && fr != building_frames_.end() && fr->second.scale > 0.0f) {
				const auto &me = rtso::ModelLibrary::get(cat_b, mid);
				const float ms = me.scale > 0.05f ? me.scale : 1.0f;
				rubble_metre = fr->second.scale * ms;
				rubble_k = rubble_metre / rf->second.scale;
				rubble_pos = Vector3(fr->second.origin.x * ms, 0.0f, fr->second.origin.z * ms) - rf->second.origin * rubble_k;
				visual_id = String(ro->second.c_str());
				own_rubble = true;
			}
		}
		attach_building_model(v, visual_id);
		auto entry = rtso::ModelLibrary::get(cat_b, visual_id);
		float b_scale = entry.scale > 0.05f ? entry.scale : 1.0f;

		// Models face -z unrotated, so the sim's 0 A.D. rotation is offset by DEFAULT_BUILDING_ROT.
		{
			const Transform3D xf(Basis(Vector3(0, 1, 0), b.rot - rtso::Simulation::DEFAULT_BUILDING_ROT), Vector3(b.x, base_y, b.z));
			if (!v.root_xf_set || !(xf == v.root_xf)) {
				v.root->set_transform(xf);
				v.root_xf = xf;
				v.root_xf_set = true;
			}
		}
		{
			const auto &site = rtso::ModelLibrary::get(cat_b, mid);
			set_building_site_visible(v, v.phase == 1 || v.phase == 2, v.phase == 2, b.type, bciv, mid,
					site.scale > 0.05f ? site.scale : 1.0f);
		}

		if (Node3D *n3 = Object::cast_to<Node3D>(v.model)) {
			Vector3 scale(b_scale, b_scale, b_scale), rot, pos;
			if (v.phase == 1) {
				n3->set_visible(false);
				pos = v.pose_set ? v.pose_pos : n3->get_position();
			} else if (v.phase == 2) {
				n3->set_visible(true);
				float dy = entry.aabb.size.y * b_scale;
				if (dy < 1.0f)
					dy = 4.0f;
				pos = Vector3(0, entry.y_offset + (b_progress - 1.0f) * dy, 0);
			} else if (v.phase == 3) {
				n3->set_visible(true);
				float t = Math::clamp(v.phase_t / 0.70f, 0.0f, 1.0f);
				float dy = entry.aabb.size.y * b_scale;
				if (dy < 1.0f)
					dy = 4.0f;
				rot = Vector3(t * 0.22f, t * 0.40f, t * -0.14f);
				pos = Vector3((t - 0.5f) * 0.45f, entry.y_offset - t * dy * 0.90f, (0.5f - t) * 0.35f);
			} else if (v.phase == 4 && own_rubble) {
				// template_rubble.xml Decay: it stands DelayTime 15 s, then sinks at SinkRate 0.2 m/s.
				n3->set_visible(true);
				scale = Vector3(rubble_k, rubble_k, rubble_k);
				const float sink = std::max(0.0f, v.phase_t - 0.70f - 15.0f) * 0.2f * rubble_metre;
				pos = rubble_pos - Vector3(0, sink, 0);
			} else if (v.phase == 4) {
				n3->set_visible(true);
				float sink = Math::clamp((v.phase_t - 0.70f) / 2.40f, 0.0f, 1.0f);
				pos = Vector3(0, entry.y_offset - sink * 1.20f, 0);
			} else {
				n3->set_visible(true);
				pos = Vector3(0, entry.y_offset, 0);
			}
			set_building_pose(v, n3, scale, rot, pos);
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
			// The ground decal of the model drawn: the foundation's while it is a site.
			String decal_glb;
			float dk = b_scale;
			Vector3 dbase;
			if (v.phase == 0) {
				decal_glb = visual_id;
			} else if (v.phase == 4 && own_rubble) {
				decal_glb = visual_id;
				dk = rubble_k;
				dbase = rubble_pos;
			} else if (v.phase == 1 || v.phase == 2) {
				if (Node3D *fn = Object::cast_to<Node3D>(v.foundation); fn && fn->is_visible()) {
					decal_glb = v.foundation_glb;
					dk = fn->get_scale().x;
					dbase = fn->get_position();
				}
			}
			update_building_decals(sim, v, decal_glb, dk, dbase, Math::round(los_brightness(b.x, b.z) * (FOG_OVERLAY_LEVELS - 1)) / (FOG_OVERLAY_LEVELS - 1));
			// The particles the structure's actor, or its rubble's, always props.
			if (v.phase == 0)
				update_ambient_fx(v, visual_id, b_scale, Vector3(0.0f, entry.y_offset, 0.0f));
			else if (v.phase == 4 && own_rubble)
				update_ambient_fx(v, visual_id, rubble_k, Object::cast_to<Node3D>(v.model) ? Object::cast_to<Node3D>(v.model)->get_position() : rubble_pos);
			else
				update_ambient_fx(v, String(), 1.0f, Vector3());
			// The "death" variant's dust and smoke (destruction_{small,medium,large}.xml)
			// rise while the corpse sinks: template_structure.xml Decay sinks it at
			// 3 m/s plus 9.8 m/s², about 2 s for a structure 15-30 m tall; the
			// puffs then live out their lifetimes (up to 15 s).
			static constexpr float DEATH_EMIT = 2.0f, DEATH_END = DEATH_EMIT + 15.0f;
			if ((v.phase == 3 || v.phase == 4) && v.phase_t < DEATH_END) {
				const auto &own = rtso::ModelLibrary::get(cat_b, mid);
				update_ambient_fx(v, mid, own.scale > 0.05f ? own.scale : 1.0f, Vector3(0.0f, own.y_offset, 0.0f), true);
				if (v.phase_t >= DEATH_EMIT)
					for (GPUParticles3D *p : v.death_fx)
						if (p->is_emitting()) p->set_emitting(false);
			} else {
				update_ambient_fx(v, String(), 1.0f, Vector3(), true);
			}
		}

		{
			const float frac = b.max_hp > 0.0f ? b.hp / b.max_hp : 1.0f;
			uint8_t lvl = 0;
			if (v.phase == 0) {
				if (frac <= 0.35f || b.is_burning) lvl = 2;
				else if (frac <= 0.65f) lvl = 1;
			}
			update_damage_fx(v, lvl, b.half_size, entry.aabb.size.y * b_scale);
		}
		update_upgrade_scaffold(v, visual_id, v.phase == 0 && b.upgrading);
		update_production_fx(v, visual_id, v.phase == 0 && !b.miraged ? b.production : 0, b_scale, entry.y_offset);
		{
			// Dust while builders work on the foundation, or while the upgrade's scaffold stands.
			const uint8_t dust = b.miraged ? 0 : v.phase == 2 ? 1 : (v.upgrade_scaffold && v.upgrade_scaffold->is_visible()) ? 2 : 0;
			int builders = -1;
			if (dust == 1) {
				v.site_dust_poll -= delta;
				if (v.site_dust_poll <= 0.0f || v.site_dust_kind != 1) {
					v.site_dust_poll = 0.5f;
					builders = 0;
					for (const auto &rb : sim->buildings()) {
						if (rb.id != b.id) continue;
						rtso::Simulation::BuildWork work;
						if (sim->structure_build_work(rb, work) && !work.repair) builders = work.builders;
						break;
					}
				}
			}
			update_site_dust(v, dust, builders, visual_id, b_scale);
		}
		update_garrison_flag(v, visual_id, v.phase == 0 && !b.miraged && (sim->get_garrison_count(b.id) > 0 || (b.type == rtso::BLD_CORRAL && sim->corral_count(b.id) > 0)),
				player_display_color(sim, b.team), b_scale, entry.y_offset);

		// StatusBars.js on a selected, highlighted or (Tab) any structure: upgrade
		// progress, a field's resource supply, the capture bar, health, and the
		// icons of the shown auras that reach structures.
		v.hp_bg->set_visible(false);
		v.hp_fg->set_visible(false);
		if (!destroyed) {
			const int32_t sid = (int32_t)b.id;
			const bool bars = show_all_status_bars_ || sel_buildings_.count(sid) || highlight_set_.count(sid);
			StatusStack st;
			if (bars) {
				// A foundation's Health grows as it is built (Foundation.js).
				st.health = b_progress < 1.0f ? b_progress : (b.max_hp > 0.0f ? b.hp / b.max_hp : 1.0f);
				if (b.type == rtso::BLD_FIELD && b_progress >= 1.0f) st.supply = 1.0f; // an infinite ResourceSupply
				if (const auto *lb = b.miraged ? nullptr : live_building(b.id)) {
					if (lb->upgrade_to != rtso::BLD_TYPE_COUNT && lb->upgrade_duration > 0.0f) st.upgrade = lb->upgrade_timer / lb->upgrade_duration;
					if (lb->max_capture_points > 0.0f && b_progress >= 1.0f) {
						st.capture = lb->capture_points_by_team.data();
						st.capture_max = lb->max_capture_points;
						st.owner = lb->team;
					}
				}
			}
			if (!aura_icon_sources_.empty()) status_aura_icons(b.id, b.x, b.z, b.team, rtso::CIV_UNIT_TEMPLATE_NONE, true, st);
			if (bars || st.n_icons > 0) {
				// In the model's metres (its actor frame), as the GLBs are not drawn 1 unit = 4 m.
				float m = 0.25f;
				const auto fr = building_frames_.find(building_table_key(mid));
				if (fr != building_frames_.end() && fr->second.scale > 0.0f) m = fr->second.scale * b_scale;
				draw_status_stack(sim, Vector3(b.x, base_y + structure_status_bar_offset(b.type) * m, b.z), 6.0f * m, 0.6f * m, m, st);
			}
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
			st.live = !b.miraged;
			tracked_buildings[b.id] = st;
		} else if (b.miraged) {
			// A memory: no live hit points.
			bit->second.live = false;
		} else {
			bit->second.live = true;
			bit->second.hp = b.hp;
			bit->second.max_hp = b.max_hp;
			// 0.28 plays no sound for a damaged structure: Health.js only switches its
			// damage variant (template_structure DamageVariants).
		}
	}

	// 3D Positional Building Collapse / Destruction Acoustics
	for (auto it = tracked_buildings.begin(); it != tracked_buildings.end(); ) {
		if (alive_buildings.find(it->first) == alive_buildings.end()) {
			// A mirage clearing when the viewer looks again is silent.
			if (it->second.live) {
				float by = sim->get_terrain_height(it->second.x, it->second.z);
				play_structure_death(*this, it->second.type, Vector3(it->second.x, by + 1.0f, it->second.z));
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
			building_pool[s].upgrade_scaffold = nullptr;
			building_pool[s].production = 0;
			building_pool[s].production_fx.clear();
			building_pool[s].site_dust = nullptr;
			building_pool[s].site_dust_kind = 0;
			building_pool[s].site_dust_poll = 0.0f;
			building_pool[s].decals.clear();
			building_pool[s].decal_key = FxKey();
			building_pool[s].ambient_fx.clear();
			building_pool[s].ambient_pos.clear();
			building_pool[s].ambient_key = FxKey();
			building_pool[s].death_fx.clear();
			building_pool[s].death_pos.clear();
			building_pool[s].death_key = FxKey();
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
	if (arrow_mm.is_valid()) set_mm_count(arrow_mm, arrow_n);
	if (bolt_mm.is_valid()) set_mm_count(bolt_mm, bolt_n);
	if (stone_proj_mm.is_valid()) set_mm_count(stone_proj_mm, stone_n);

	int arrow_i = 0, bolt_i = 0, stone_i = 0;

	for (size_t p = 0; p < projectiles.size(); ++p) {
		const auto &proj = projectiles[p];
		if (!entity_visible(proj.current_x, proj.current_z, proj.team)) continue;
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
			// 0.28's stone throwers fling flaming rocks (props/units/weapons/rock_flaming.xml,
			// stone_projectile_*_flaming.xml); the Han mangonel's rock.xml has no fire.
			if (sim->team_civ(proj.team) != rtso::CIV_HAN) {
				load_ambient_fx();
				emit_world_fx(stone_fx_, pos, stone_proj_scale, delta);
			}
			xf.basis = xf.basis.scaled(Vector3(stone_proj_scale, stone_proj_scale, stone_proj_scale));
			stone_proj_mm->set_instance_transform(stone_i++, xf);
		} else if (proj.visual == 1 && bolt_mm.is_valid()) {
			xf.basis = xf.basis.scaled(Vector3(bolt_scale, bolt_scale, bolt_scale));
			bolt_mm->set_instance_transform(bolt_i++, xf);
		} else if (arrow_mm.is_valid()) {
			xf.basis = xf.basis.scaled(Vector3(arrow_scale, arrow_scale, arrow_scale));
			arrow_mm->set_instance_transform(arrow_i++, xf);
		}

	}

	// =========================================================================
	// 4. Resources: Tree Varieties, Gold Veins, Stone Mines, Fruit Bushes
	// =========================================================================
	// Explored supplies show as last seen (Fogging.js); unexplored ones not at all.
	const auto &fres = mirages_.resources();
	size_t n_tree = 0, fd = 0, nru = 0, fish_n = 0, n_mine[MINE_KINDS] = {};
	if (sim->current_map_biome() != mine_biome_) pick_mine_models(sim->current_map_biome());
	if (sim->game_time() < mine_large_time_ || sim->current_map_id() != mine_large_map_) {
		mine_large_.clear();
		supply_max_.clear();
		auto rv = sim->resources();
		for (size_t r = 0; r < rv.count; ++r)
			if (rv.alive[r]) supply_max_.emplace(rv.id[r], rv.amount[r]);
	}
	mine_large_time_ = sim->game_time();
	mine_large_map_ = sim->current_map_id();
	auto mine_of = [&](const rtso::FoggedResource &r) -> int {
		auto it = mine_large_.find(r.id);
		if (it == mine_large_.end()) it = mine_large_.emplace(r.id, r.amount > 2000).first;
		const bool metal = r.type == rtso::RES_GOLD;
		return it->second ? (metal ? MINE_METAL_LARGE : MINE_STONE_LARGE) : (metal ? MINE_METAL_SMALL : MINE_STONE_SMALL);
	};
	for (const auto &r : fres) {
		if (r.subtype == rtso::RES_SUB_GRAIN || r.subtype == rtso::RES_SUB_MEAT) continue; // a field's grain or animal carcass
		if (r.subtype == rtso::RES_SUB_RUINS) nru++;
		else if (r.type == rtso::RES_WOOD) n_tree++;
		else if (r.type == rtso::RES_GOLD || r.type == rtso::RES_STONE) n_mine[mine_of(r)]++;
		else if (r.subtype == rtso::RES_SUB_FISH) fish_n++;
		else if (r.type == rtso::RES_FOOD) fd++;
	}

	if (sim->current_map_biome() != tree_biome_) pick_tree_kinds(sim->current_map_biome());
	// A tree or fruit bush with a FloraKind whose model loaded draws in that kind's batch.
	auto flora_of = [&](const rtso::FoggedResource &r) -> int {
		if (r.flora == 0 || r.flora >= FLORA_KINDS || flora_variants[r.flora] == 0) return 0;
		if (r.subtype != rtso::RES_SUB_NONE || (r.type != rtso::RES_WOOD && r.type != rtso::RES_FOOD)) return 0;
		return r.flora;
	};
	size_t n_kind[TREE_KINDS][TREE_VARIANTS] = {}, n_flora[FLORA_KINDS][TREE_VARIANTS] = {};
	for (const auto &r : fres) {
		if (const int f = flora_of(r)) {
			n_flora[f][tree_variant_of(r.id, flora_variants[f])]++;
			if (r.type == rtso::RES_FOOD) fd--;
			continue;
		}
		if (r.type == rtso::RES_WOOD && r.subtype != rtso::RES_SUB_GRAIN) {
			const int k = (int)(r.id % (uint32_t)tree_kind_count);
			n_kind[k][tree_variant_of(r.id, tree_variants[k])]++;
		}
	}
	for (int k = 0; k < tree_kind_count; ++k)
		for (int v = 0; v < tree_variants[k]; ++v) set_mm_count(tree_mm[k][v], (int)n_kind[k][v]);
	for (int k = 1; k < FLORA_KINDS; ++k)
		for (int v = 0; v < flora_variants[k]; ++v) set_mm_count(flora_mm[k][v], (int)n_flora[k][v]);
	for (int k = 0; k < MINE_KINDS; ++k) set_mm_count(mine_mm[k], (int)n_mine[k]);
	if (food_mm.is_valid())
		set_mm_count(food_mm, (int)fd);
	set_mm_count(ruins_mm, (int)nru);
	set_mm_count(fish_mm, (int)fish_n);

	size_t ia[TREE_KINDS][TREE_VARIANTS] = {}, iflora[FLORA_KINDS][TREE_VARIANTS] = {}, imine[MINE_KINDS] = {}, ifd = 0, iru = 0, ifish = 0;
	auto e_ruins = rtso::ModelLibrary::get("buildings", "ruins");
	String cat_veg = "vegetation";
	String id_food = "fruit_bush"; auto e_food = rtso::ModelLibrary::get(cat_veg, id_food);

	for (const auto &r : fres) {
		if (r.subtype == rtso::RES_SUB_GRAIN) continue;
		float rx = r.x, rz = r.z;
		float ry = sim->get_terrain_height(rx, rz);
		uint32_t h = r.id;
		Basis rot(Vector3(0, 1, 0), (float)(h % 12) * 0.52f);
		const float lit = los_brightness(rx, rz);
		const Color tint(lit, lit, lit, 1.0f);

		if (const int f = flora_of(r)) {
			const int v = tree_variant_of(r.id, flora_variants[f]);
			const float sc = flora_scale[f];
			Transform3D xf(rot.scaled(Vector3(sc, sc, sc)), Vector3(rx, ry + flora_y[f][v], rz));
			flora_mm[f][v]->set_instance_transform((int)iflora[f][v], xf);
			flora_mm[f][v]->set_instance_color((int)iflora[f][v]++, tint);
		} else if (r.subtype == rtso::RES_SUB_RUINS) {
			Transform3D xf(rot, Vector3(rx, ry + e_ruins.y_offset, rz));
			ruins_mm->set_instance_transform((int)iru, xf);
			ruins_mm->set_instance_color((int)iru++, tint);
		} else if (r.type == rtso::RES_WOOD) {
			const int k = (int)(h % (uint32_t)tree_kind_count);
			const int v = tree_variant_of(r.id, tree_variants[k]);
			const float sc = tree_scale[k];
			Transform3D xf(rot.scaled(Vector3(sc, sc, sc)), Vector3(rx, ry + tree_y[k][v], rz));
			tree_mm[k][v]->set_instance_transform((int)ia[k][v], xf);
			tree_mm[k][v]->set_instance_color((int)ia[k][v]++, tint);
		} else if (r.type == rtso::RES_GOLD || r.type == rtso::RES_STONE) {
			const int k = mine_of(r);
			Transform3D xf(rot, Vector3(rx, ry + mine_y[k], rz));
			mine_mm[k]->set_instance_transform((int)imine[k], xf);
			mine_mm[k]->set_instance_color((int)imine[k]++, tint);
		} else if (r.subtype == rtso::RES_SUB_FISH) {
			// 0 A.D.'s fish shoal swims just under the surface (Position/Floating).
			// The water has no refraction pass, so the top fish break the
			// surface to stay visible.
			ry = std::max(ry, drawn_water_height(sim));
			Transform3D xf(rot, Vector3(rx, ry + 0.3f, rz));
			fish_mm->set_instance_transform((int)ifish, xf);
			fish_mm->set_instance_color((int)ifish++, tint);
		} else if (r.type == rtso::RES_FOOD && r.subtype != rtso::RES_SUB_MEAT && food_mm.is_valid()) {
			Transform3D xf(rot, Vector3(rx, ry + e_food.y_offset, rz));
			food_mm->set_instance_transform((int)ifd, xf);
			food_mm->set_instance_color((int)ifd++, tint);
		}
	}

	// Treasures sit still, so an explored one stays drawn (darker) in the fog, and one
	// taken out of sight stays there as a mirage until the spot is seen again.
	{
		const auto &tv = mirages_.treasures();
		int nt[rtso::TREASURE_TYPE_COUNT] = {};
		for (const auto &t : tv)
			if (t.type < rtso::TREASURE_TYPE_COUNT && los_brightness(t.x, t.z) > 0.0f) ++nt[t.type];
		for (int k = 0; k < rtso::TREASURE_TYPE_COUNT; ++k) set_mm_count(treasure_mm[k], nt[k]);
		int it[rtso::TREASURE_TYPE_COUNT] = {};
		for (const auto &t : tv) {
			const float lit = t.type < rtso::TREASURE_TYPE_COUNT ? los_brightness(t.x, t.z) : 0.0f;
			if (lit <= 0.0f) continue;
			// Wrecks and floating cargo sit on the water, the rest on the ground.
			const float ground = std::max(sim->get_terrain_height(t.x, t.z), sim->water_height());
			Basis rot(Vector3(0, 1, 0), rtso::Simulation::treasure_angle(t.id));
			Transform3D xf(rot, Vector3(t.x, ground + treasure_y_offset[t.type], t.z));
			treasure_mm[t.type]->set_instance_transform(it[t.type], xf);
			treasure_mm[t.type]->set_instance_color(it[t.type]++, Color(lit, lit, lit, 1.0f));
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
			else ++kinds[sc.id % diverse_variants((int)kinds.size())].count;
		}
		set_mm_count(scenery_mm, ns);
		for (auto &kinds : scenery_variants_)
			for (auto &k : kinds) {
				set_mm_count(k.mm, k.count);
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
				SceneryVariant &k = kinds[sc.id % diverse_variants((int)kinds.size())];
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
		// 0.28 UnitAI animal IDLE.LINGERING: SelectAnimation("feeding").
		if (behavior == rtso::FAUNA_IDLE_GRAZE) return 6;
		return 0;
	};
	// 0 A.D. plays one of an actor's same-named clips at random; as with units
	// (with_variant), each animal keeps one by its id: half fall with the second
	// death and a quarter stand with the second idle, when the model has them.
	// An actor without a feeding clip lingers on its idle, as CModel falls back.
	auto with_fauna_variant = [this](int clip, int ft, uint32_t id) -> int {
		const uint32_t h = id * 2654435761u;
		if (clip == 6) {
			if (fauna_puppet[ft][6].baked_count > 0) return 6;
			clip = 0;
		}
		if (clip == 3 && (h & 0x80000000u) && fauna_puppet[ft][5].baked_count > 0) return 5;
		if (clip == 0 && (h >> 29) == 3u && fauna_puppet[ft][4].baked_count > 0) return 4;
		return clip;
	};
	int f_counts[FAUNA_ROWS][FAUNA_CLIP_COUNT] = {};
	for (size_t f = 0; f < fview.count; ++f) {
		if (fview.behavior[f] == rtso::FAUNA_CORRALLED) continue; // garrisoned: hidden, the corral flies its flag
		if (!fauna_visible(fview.pos_x[f], fview.pos_z[f])) continue;
		if (fview.fauna_type[f] >= rtso::FAUNA_COUNT) continue;
		const int ft = fauna_row(fview.fauna_type[f], fview.id[f]);
		// Baked before the first count, so both passes pick the same clips.
		ensure_fauna_frames(ft);
		float vx = fview.vel_x[f];
		float vz = fview.vel_z[f];
		int clip = fauna_clip_of(fview.behavior[f], vx * vx + vz * vz, fview.alive[f] != 0);
		f_counts[ft][with_fauna_variant(clip, ft, fview.id[f])]++;
	}
	for (int ft = 0; ft < fauna_rows_; ++ft) {
		cycle_fauna_meshes(fauna_puppet[ft], fauna_mm[ft], f_counts[ft], FAUNA_CLIP_COUNT, FAUNA_DEATH_SLOTS);
		for (int c = 0; c < FAUNA_CLIP_COUNT; ++c) {
			if (fauna_mm[ft][c].is_valid()) {
				set_mm_count(fauna_mm[ft][c], f_counts[ft][c]);
			}
		}
	}
	int f_written[FAUNA_ROWS][FAUNA_CLIP_COUNT] = {};
	for (size_t f = 0; f < fview.count; ++f) {
		if (fview.behavior[f] == rtso::FAUNA_CORRALLED) continue; // garrisoned: hidden, the corral flies its flag
		if (!fauna_visible(fview.pos_x[f], fview.pos_z[f])) continue;
		uint8_t ft = fview.fauna_type[f];
		if (ft >= rtso::FAUNA_COUNT) continue;
		const int row = fauna_row(ft, fview.id[f]);
		float vx = fview.vel_x[f];
		float vz = fview.vel_z[f];
		int clip = with_fauna_variant(fauna_clip_of(fview.behavior[f], vx * vx + vz * vz, fview.alive[f] != 0),
				row, fview.id[f]);
		float fx = fview.pos_x[f];
		float fz = fview.pos_z[f];
		float fy = sim->get_terrain_height(fx, fz);
		if (rtso::fauna_type_info(ft).is_naval()) fy = std::max(fy, drawn_water_height(sim)); // whales and sharks swim
		float frot = fview.rot_y[f];
		const char *fid = rtso::fauna_type_info(ft).model_id;
		auto fent = rtso::ModelLibrary::get("units", fauna_row_model_[row]);
		float fsc = fent.scale > 0.05f ? fent.scale : 1.0f;
		Transform3D xf(Basis(Vector3(0, 1, 0), frot).scaled(Vector3(fsc, fsc, fsc)),
		               Vector3(fx, fy + fent.y_offset, fz));
		// Fleeing (or moving at a run) on the Run clip: the actor's dust.
		if (clip == 1 && (fview.behavior[f] == rtso::FAUNA_FLEE || vx * vx + vz * vz > 18.0f))
			emit_run_dust(String(fid), Vector3(fx, fy, fz), fsc, delta);
		if (fview.alive[f]) emit_unit_fx(String(fid), xf, fsc, delta);
		int fslot = f_written[row][clip]++;
		if (fauna_mm[row][clip].is_valid()) {
			fauna_mm[row][clip]->set_instance_transform(fslot, xf);
			// 0.28 picks each animal's coat by its variant frequencies (data/unit_variants.json).
			fauna_mm[row][clip]->set_instance_custom_data(fslot, Color(1.0f, 1.0f, 1.0f, 1.0f + 0.999f * rtso::ModelLibrary::variant_draw(fview.id[f])));
		}
		const bool f_sel = !sel_set.empty() && sel_set.count((int32_t)fview.id[f]);
		if (f_sel || (!highlight_set_.empty() && highlight_set_.count((int32_t)fview.id[f]))) {
			// The player's own selected livestock (0 A.D. livestock are units), or a hovered animal.
			Decal *r = get_or_create_ring_decal(unit_rings_used++);
			r->set_modulate(f_sel ? RING_SEL_COL : RING_HL_COL);
			const float rs = std::max(1.1f, fsc * 1.6f);
			r->set_size(Vector3(rs, 2.5f, rs));
			r->set_transform(Transform3D(Basis(), Vector3(fx, fy + 0.1f, fz)));
			r->set_visible(true);
			// StatusBars.js: an animal's food supply and, alive, its health.
			StatusStack st;
			const int food = rtso::fauna_type_info(ft).food_amount;
			if (food > 0) st.supply = (float)fview.food_amount[f] / (float)food;
			if (fview.alive[f] && fview.max_hp[f] > 0.0f) {
				st.health = fview.hp[f] / fview.max_hp[f];
			}
			// The animal GLBs are in metres (fsc is world units per metre).
			const rtso::StatusBarsSize sb = rtso::fauna_status_bars(rtso::fauna_type_info(ft).template_name);
			draw_status_stack(sim, Vector3(fx, fy + sb.height_offset * fsc, fz), sb.bar_width * fsc, sb.bar_height * fsc, fsc, st);
		}
	}
	// A hovered tree, mine, bush or shoal (as last seen): its selection ring and
	// StatusBars.js ResourceSupply bar.
	if (!highlight_set_.empty()) {
		for (const auto &r : mirages_.resources()) {
			if (r.subtype == rtso::RES_SUB_GRAIN || !highlight_set_.count((int32_t)r.id)) continue;
			const float ry = r.subtype == rtso::RES_SUB_FISH ? std::max(sim->get_terrain_height(r.x, r.z), drawn_water_height(sim))
			                                                : sim->get_terrain_height(r.x, r.z);
			// The StatusBars of template_gaia (2 m wide, 0.333 m tall, 5 m up), _tree,
			// _fruit, _fish and _ore/_rock_large, at 4 m a unit.
			const bool mine = (r.type == rtso::RES_GOLD || r.type == rtso::RES_STONE) && r.subtype != rtso::RES_SUB_RUINS;
			float radius = 0.7f, bar_w = 2.0f, bar_h = 0.333f, offset = 5.0f;
			if (mine) {
				auto it = mine_large_.find(r.id);
				const bool large = it != mine_large_.end() && it->second;
				radius = large ? 2.2f : 1.3f;
				if (large) bar_w = 5.0f, bar_h = 0.5f;
			} else if (r.type == rtso::RES_WOOD) {
				bar_w = 3.0f, bar_h = 0.5f, offset = 10.0f;
			} else if (r.subtype == rtso::RES_SUB_FISH) {
				radius = 1.0f, bar_w = 4.0f, bar_h = 0.666f;
			} else if (r.type == rtso::RES_FOOD) {
				bar_w = 3.0f, bar_h = 0.5f, offset = 4.0f;
			}
			Decal *ring = get_or_create_ring_decal(unit_rings_used++);
			ring->set_modulate(RING_HL_COL);
			const float rs = 2.0f * radius;
			ring->set_size(Vector3(rs, 2.5f, rs));
			ring->set_transform(Transform3D(Basis(), Vector3(r.x, ry + 0.1f, r.z)));
			ring->set_visible(true);
			const int32_t max = supply_max_.emplace(r.id, r.amount).first->second;
			StatusStack st;
			st.supply = max > 0 ? (float)r.amount / (float)max : 1.0f;
			draw_status_stack(sim, Vector3(r.x, ry + 0.25f * offset, r.z), 0.25f * bar_w, 0.25f * bar_h, 0.25f, st);
		}
	}
	for (size_t k = 0; k < unit_rings_used; ++k) sync_gl_decal(sim, unit_ring_decals_[k]);
	for (size_t k = unit_rings_used; k < unit_ring_decals_.size(); ++k) unit_ring_decals_[k]->set_visible(false);
	status_sprites_.end_frame();
	draw_structure_outlines(sim);
}

// The icons of the shown auras (aura_icon_sources_) reaching an entity of `team` at
// (x, z): a unit of roster row tmpl the aura affects, or a structure when the aura
// names the Structure class. Each icon once. A range aura's query leaves its own source
// out (Auras.js CreateActiveQuery), so a hero doesn't wear its own range aura's icon.
void RtsoWorldView::status_aura_icons(uint32_t id, float x, float z, uint8_t team, uint16_t tmpl, bool structure, StatusStack &st) const {
	const uint16_t bit = rtso::aura_player_bit(team);
	for (const AuraIconSource &a : aura_icon_sources_) {
		const rtso::AuraSource &s = *a.src;
		if (!(s.affected & bit)) continue;
		if (s.source_id == id && rtso::civ_hero_aura(s.aura).radius > 0.0f) continue;
		if (structure ? !rtso::civ_hero_aura(s.aura).structures : !rtso::civ_aura_affects(s.aura, tmpl)) continue;
		const float dx = x - s.x, dz = z - s.z;
		if (dx * dx + dz * dz > s.range * s.range) continue;
		bool dup = false;
		for (int k = 0; k < st.n_icons && !dup; ++k) dup = st.icons[k] == a.icon;
		if (!dup && st.n_icons < 8) st.icons[st.n_icons++] = a.icon;
	}
}

// StatusBars.js RegenerateSprites: experience, pack, upgrade, resource supply,
// capture, health, aura icons and rank, from `base` up, each bar BarHeight * 1.2
// above the one under it (the experience bar 2/3 as tall, using the pack textures).
// `base` is the entity's position raised by HeightOffset; `metre` is world units per
// 0 A.D. metre at the entity's drawn size.
void RtsoWorldView::draw_status_stack(const rtso::Simulation *sim, const Vector3 &base, float width, float height, float metre, const StatusStack &st) {
	float y = 0.0f;
	auto at = [&](float h) { return base + Vector3(0.0f, y + 0.5f * h, 0.0f); };
	auto bar = [&](RtsoStatusSprites::Bar type, float amount, float h) {
		if (amount < 0.0f) return;
		status_sprites_.bar(type, at(h), width, h, amount);
		y += h * 1.2f;
	};
	if (st.xp >= 0.0f) ++status_xp_bars_;
	bar(RtsoStatusSprites::BAR_PACK, st.xp, height * 2.0f / 3.0f);
	bar(RtsoStatusSprites::BAR_PACK, st.pack, height);
	bar(RtsoStatusSprites::BAR_UPGRADE, st.upgrade, height);
	bar(RtsoStatusSprites::BAR_SUPPLY, st.supply, height);
	if (st.capture && st.capture_max > 0.0f) {
		// The owner's points first, on the left, then each other player's.
		const Vector3 c = at(height);
		float from = 0.0f;
		auto part = [&](uint8_t p) {
			const float to = from + st.capture[p] / st.capture_max;
			status_sprites_.capture_part(c, width, height, from, std::min(to, 1.0f), player_display_color(sim, p));
			from = to;
		};
		if (st.owner < 8) part(st.owner);
		for (uint8_t p = 0; p < 8; ++p)
			if (p != st.owner && st.capture[p] > 0.0f) part(p);
		y += height * 1.2f;
	}
	bar(RtsoStatusSprites::BAR_HEALTH, st.health, height);
	const float icon = width * 0.5f;
	if (st.n_icons > 0) {
		// AddAuraIcons adds yoffset to both the sprite and its offset, so the icons
		// sit twice as high as the bars under them.
		float dx = -icon * (st.n_icons - 1) * 0.6f;
		const Vector3 c = base + Vector3(0.0f, 2.0f * y + 0.5f * icon, 0.0f);
		for (int k = 0; k < st.n_icons; ++k, dx += icon * 1.2f) status_sprites_.icon(st.icons[k], c, dx, icon);
		y += icon + height * 0.5f;
	}
	if (st.rank_icon >= 0) {
		// AddRankIcon: HeightOffset + 0.1.
		status_sprites_.icon(st.rank_icon, at(icon) + Vector3(0.0f, 0.1f * metre, 0.0f), 0.0f, icon);
		status_rank_icons_.push_back(status_sprites_.icon_path(st.rank_icon));
	}
}

// Selectable.js overlay for structures: an outline round the footprint of each selected
// structure, and a fainter one round a highlighted (hovered or band-boxed) one.
void RtsoWorldView::sync_gl_decal(const rtso::Simulation *sim, Decal *d) {
	if (!rtso::gl_compatibility() || !sim || !d || !d->is_inside_tree()) return;
	Ref<Texture2D> tex = d->get_texture(Decal::TEXTURE_ALBEDO);
	if (tex.is_null()) return;
	MeshInstance3D *mi = d->get_child_count() > 0 ? Object::cast_to<MeshInstance3D>(d->get_child(0)) : nullptr;
	if (!mi) {
		mi = memnew(MeshInstance3D);
		mi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		d->add_child(mi);
	}
	// Rebuilt only when the decal moves, turns, resizes or changes colour.
	const Transform3D xf = d->get_global_transform();
	const Vector3 size = d->get_size();
	const Color mod = d->get_modulate();
	const Array key = Array::make(xf, size, mod);
	if (mi->get_meta("gl_key", Variant()) == Variant(key)) return;
	mi->set_meta("gl_key", key);
	Ref<StandardMaterial3D> &mat = gl_decal_mats_[tex.ptr()];
	if (mat.is_null()) {
		mat.instantiate();
		mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		mat->set_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR, true);
		mat->set_flag(BaseMaterial3D::FLAG_DISABLE_FOG, true);
		mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
	}
	// A grid about a world unit (4 m) apart, each vertex on the terrain under it,
	// UVs as the Decal maps its albedo (u along x, v along z).
	const int n = Math::clamp((int)Math::ceil(std::max(size.x, size.z)), 2, 16);
	const Transform3D inv = xf.affine_inverse();
	PackedVector3Array verts;
	PackedVector2Array uvs;
	PackedColorArray cols;
	PackedInt32Array idx;
	verts.resize((n + 1) * (n + 1));
	uvs.resize(verts.size());
	cols.resize(verts.size());
	for (int j = 0; j <= n; ++j) {
		for (int i = 0; i <= n; ++i) {
			const float u = (float)i / n, v = (float)j / n;
			Vector3 w = xf.xform(Vector3((u - 0.5f) * size.x, 0.0f, (v - 0.5f) * size.z));
			w.y = sim->get_terrain_height(w.x, w.z) + 0.03f;
			const int k = j * (n + 1) + i;
			verts.set(k, inv.xform(w));
			uvs.set(k, Vector2(u, v));
			cols.set(k, mod);
		}
	}
	for (int j = 0; j < n; ++j) {
		for (int i = 0; i < n; ++i) {
			const int a = j * (n + 1) + i, b = a + 1, c = a + n + 1, e = c + 1;
			idx.append(a), idx.append(b), idx.append(c);
			idx.append(b), idx.append(e), idx.append(c);
		}
	}
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = verts;
	arrays[Mesh::ARRAY_TEX_UV] = uvs;
	arrays[Mesh::ARRAY_COLOR] = cols;
	arrays[Mesh::ARRAY_INDEX] = idx;
	Ref<ArrayMesh> mesh = mi->get_mesh();
	if (mesh.is_null()) {
		mesh.instantiate();
		mi->set_mesh(mesh);
	}
	mesh->clear_surfaces();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	mesh->surface_set_material(0, mat);
}

Decal *RtsoWorldView::get_or_create_ring_decal(size_t index) {
	while (unit_ring_decals_.size() <= index) {
		Decal *d = memnew(Decal);
		if (ring_decal_tex_.is_valid()) {
			d->set_texture(Decal::TEXTURE_ALBEDO, ring_decal_tex_);
		}
		d->set_cull_mask(DECAL_LAYER);
		d->set_normal_fade(0.2f);
		d->set_visible(false);
		add_child(d);
		unit_ring_decals_.push_back(d);
	}
	return unit_ring_decals_[index];
}

void RtsoWorldView::draw_structure_outlines(const rtso::Simulation *sim) {
	size_t used = 0;
	if (!sel_buildings_.empty() || !highlight_set_.empty()) {
		if (outline_mesh_.is_null()) {
			outline_mesh_.instantiate();
			outline_mesh_->set_size(Vector3(1.0f, 0.04f, 1.0f));
		}
		for (const auto &b : sim->buildings()) {
			const bool sel = sel_buildings_.count((int32_t)b.id) > 0;
			if (!sel && !highlight_set_.count((int32_t)b.id)) continue;
			if (b.wrecked || b.hp <= 0.0f) continue;
			float hu = b.hu > 0.0f ? b.hu : b.half_size, hv = b.hv > 0.0f ? b.hv : b.half_size;
			// RTS0's grid footprints are coarser than 0 A.D.'s template Footprint, which the
			// outline follows: take the drawn model's ground extent where it is larger.
			if (auto it = building_slot.find(b.id); it != building_slot.end() && !building_pool[it->second].model_id.is_empty()) {
				const auto entry = rtso::ModelLibrary::get("buildings", building_pool[it->second].model_id);
				if (entry.valid) {
					const float sc = entry.scale > 0.05f ? entry.scale : 1.0f;
					const float mx = 0.5f * entry.aabb.size.x * sc, mz = 0.5f * entry.aabb.size.z * sc;
					const bool turned = std::fabs(b.uz) > std::fabs(b.ux);
					hu = std::max(hu, turned ? mz : mx);
					hv = std::max(hv, turned ? mx : mz);
				}
			}
			hu += 0.25f;
			hv += 0.25f;
			const float y = sim->get_terrain_height(b.x, b.z) + 0.06f;
			const bool own = b.team == local_player_;
			const Ref<StandardMaterial3D> &mat = sel ? (own ? ring_sel_mat : ring_enemy_mat) : (own ? ring_hl_mat : ring_enemy_hl_mat);
			const Vector3 u(b.ux, 0.0f, b.uz), v(-b.uz, 0.0f, b.ux);
			const float w = 0.15f;
			// Two bars along u (at +-hv) and two along v (at +-hu).
			const struct { Vector3 c; Vector3 axis; float len; } bars[4] = {
				{u * 0.0f + v * hv, u, 2.0f * hu + w}, {v * -hv, u, 2.0f * hu + w},
				{u * hu, v, 2.0f * hv + w}, {u * -hu, v, 2.0f * hv + w},
			};
			for (const auto &bar : bars) {
				if (used == outline_bars_.size()) {
					MeshInstance3D *m = memnew(MeshInstance3D);
					m->set_mesh(outline_mesh_);
					m->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
					add_child(m);
					outline_bars_.push_back(m);
				}
				MeshInstance3D *m = outline_bars_[used++];
				const Vector3 across = bar.axis == u ? v : u;
				const Basis basis(bar.axis * bar.len, Vector3(0, 1, 0), across * w);
				m->set_transform(Transform3D(basis, Vector3(b.x, y, b.z) + bar.c));
				if (m->get_material_override() != mat) m->set_material_override(mat);
				m->set_visible(true);
			}
		}
	}
	for (size_t k = used; k < outline_bars_.size(); ++k) outline_bars_[k]->set_visible(false);
}
