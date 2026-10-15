#include "model_library.h"
#include "procedural.h"
#include "../../core/diagnostics/logger.h"
#include "../../presentation/user_config.h"
#include "../../simulation/sim_types.h"
#include "packed_siege_models.h"
#include "unit_template_models.h"
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/animation_library.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/animation_player.hpp>
#include <godot_cpp/classes/bone_attachment3d.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/classes/skin.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/core/memory.hpp>
#include <algorithm>
#include <cstdio>
#include <unordered_map>
#include <vector>

using namespace godot;

namespace rtso {

HashMap<String, ModelLibrary::Entry> &ModelLibrary::cache() {
	static auto &c = *new HashMap<String, Entry>(); // leaked: no Godot dtor after the engine exits
	return c;
}

float ModelLibrary::target_height(const String &category, const String &id, float natural_h) {
	if (category == "units") {
		if (id == "cavalry") return 2.6f;
		if (id == "camel") return 2.7f;
		if (id == "chariot" || id == "scythed_chariot") return 2.4f;
		if (id == "worker_elephant" || id == "elephant") return 3.2f;
		if (id == "ram" || id == "siege_ram") return 2.0f;
		if (id == "bolt_projectile") return 1.15f;
		if (id == "arrow_projectile") return 1.10f;
		if (id == "stone_projectile") return 0.45f;
		if (id == "bolt_shooter" || id == "ballista" || id == "oxybeles") return 1.8f;
		if (id == "catapult") return 2.2f;
		if (id == "fishing_boat") return 2.0f;
		if (id == "merchant_ship" || id == "trireme" || id == "ship_merchant") return 3.5f;
		if (id == "war_dog" || id == "dog") return 0.85f;
		if (id == "carnyx") return 1.95f;
		if (id == "archer_maiden") return 1.7f;
		if (id == "female_citizen" || id == "female") return 1.68f;
		if (id == "healer") return 1.72f;
		if (id == "deer") return 1.35f;
		if (id == "boar") return 1.15f;
		if (id == "sheep" || id == "sheep_gaia") return 0.95f;
		if (id == "wolf") return 1.05f;
		// Animal GLBs come out of the extractors already sized (a chicken 0.5,
		// a bush elephant 3.2), so they keep their own height.
		for (int f = 0; f < FAUNA_COUNT; ++f) {
			if (id == fauna_type_info((uint8_t)f).model_id) return natural_h;
		}
		// So do the packed siege wagons (tools/extract_0ad_packed_siege.py).
		if (is_packed_siege_model(id.utf8().get_data())) return natural_h;
		// And the Germans' ox wagons (ram-wide, extract_0ad_assets.py ZIP_UNITS),
		// asked for by civ (germ_wagon) and drawn from wagon.glb.
		if (id == "wagon" || id.ends_with("_wagon") || id.ends_with("wagon_house")) return natural_h;
		return 1.8f;
	}
	if (category == "buildings") {
		if (id.begins_with("foundation_") || id.begins_with("scaffold_") ||
				id.begins_with("rubble_") || id.ends_with("_rubble"))
			return natural_h;
		String base = id;
		int us = id.find("_");
		if (us > 0 && us < id.length() - 1)
			base = id.substr(us + 1);
		if (base == "town_center") return 6.0f;
		else if (base == "fortress") return 7.0f;
		else if (base == "temple") return 6.4f;
		else if (base == "wonder") return 8.5f;
		else if (base == "barracks" || base == "market") return 4.4f;
		else if (base == "wall_tower") return 4.6f;
		else if (base == "wall" || base == "wall_segment") return 3.0f;
		else if (base == "gate") return 4.5f;
		else if (base == "dock" || base == "shipyard") return 4.0f;
		else if (base == "corral") return 2.8f;
		else if (base == "house" || base == "farmstead" || base == "storehouse") return 3.4f;
		else if (base == "ruins") return 2.4f;
		// tools/extract_0ad_structures.py sized it already (data/building_models.json).
		if (building_glb_id(id) != id) return natural_h;
		return Math::clamp(natural_h, 2.0f, 8.0f);
	}
	// Props (0 A.D. treasures) are sized by the extractor (ZIP_PROPS).
	if (category == "props") return natural_h;
	if (id == "pine") return 8.0f;
	else if (id == "palm") return 7.0f;
	else if (id == "tree") return 6.0f;
	else if (id == "stone_mine") return 1.4f;
	else if (id == "gold_vein") return 1.1f;
	else if (id == "fruit_bush") return 1.15f;
	return Math::clamp(natural_h, 0.8f, 8.0f);
}

struct MeshXform {
	MeshInstance3D *mi = nullptr;
	Transform3D xf;
};

static AnimationPlayer *find_animation_player(Node *n) {
	if (!n) return nullptr;
	if (AnimationPlayer *ap = Object::cast_to<AnimationPlayer>(n)) return ap;
	for (int i = 0; i < n->get_child_count(); ++i) {
		if (AnimationPlayer *ap = find_animation_player(n->get_child(i))) return ap;
	}
	return nullptr;
}

// 0 A.D. player_trans skins: data/player_masks.json maps an extracted GLB
// image ("units/spearman_tex0") to its player-colour mask and the tint the
// bake left in it. Each such material becomes one shaders/player_color
// ShaderMaterial, shared by every instance.
namespace {
struct PlayerMask {
	String mask;
	Color tint;
};
HashMap<String, PlayerMask> &player_masks() {
	static auto &m = *new HashMap<String, PlayerMask>();
	static bool loaded = false;
	if (!loaded) {
		loaded = true;
		Ref<FileAccess> f = FileAccess::open("res://data/player_masks.json", FileAccess::READ);
		if (f.is_valid()) {
			Dictionary root = JSON::parse_string(f->get_as_text());
			Array keys = root.keys();
			for (int i = 0; i < keys.size(); ++i) {
				Dictionary e = root[keys[i]];
				Array t = e.get("tint", Array());
				PlayerMask pm;
				pm.mask = e.get("mask", String());
				pm.tint = t.size() == 3 ? Color(t[0], t[1], t[2]) : Color(1, 1, 1);
				m.insert(keys[i], pm);
			}
		}
	}
	return m;
}
// {image key: texture path} tables written by tools/extract_normal_maps.py
// (data/normal_maps.json: 0 A.D.'s normTex for that skin, green flipped to
// OpenGL) and tools/extract_specular_maps.py (data/specular_maps.json: its
// specTex as GGX roughness in red and Godot specular in green).
HashMap<String, String> load_texture_table(const char *path) {
	HashMap<String, String> m;
	Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ);
	if (f.is_valid()) {
		Dictionary root = JSON::parse_string(f->get_as_text());
		Array keys = root.keys();
		for (int i = 0; i < keys.size(); ++i) m.insert(keys[i], root[keys[i]]);
	}
	return m;
}
HashMap<String, String> &normal_maps() {
	static auto &m = *new HashMap<String, String>(load_texture_table("res://data/normal_maps.json"));
	return m;
}
HashMap<String, String> &specular_maps() {
	static auto &m = *new HashMap<String, String>(load_texture_table("res://data/specular_maps.json"));
	return m;
}
String image_key(const Ref<Texture2D> &tex) {
	// res://assets/models/units/spearman_tex0.png -> units/spearman_tex0
	return tex.is_valid() ? tex->get_path().trim_prefix("res://assets/models/").get_basename() : String();
}
Ref<Texture2D> table_texture_for(HashMap<String, String> &table, const Ref<Texture2D> &albedo) {
	const String *path = table.getptr(image_key(albedo));
	if (!path || !ResourceLoader::get_singleton()->exists(*path)) return Ref<Texture2D>();
	return ResourceLoader::get_singleton()->load(*path);
}
Ref<Texture2D> normal_map_for(const Ref<Texture2D> &albedo) { return table_texture_for(normal_maps(), albedo); }
// data/parallax_maps.json (tools/extract_normal_maps.py --heights): for skins whose
// actor uses a *_parallax* material, normTex's alpha as a height map and the
// material's effectSettings.z.
struct ParallaxMap {
	String height;
	float scale = 0.0f;
};
HashMap<String, ParallaxMap> &parallax_maps() {
	static auto &m = *new HashMap<String, ParallaxMap>();
	static bool loaded = false;
	if (!loaded) {
		loaded = true;
		Ref<FileAccess> f = FileAccess::open("res://data/parallax_maps.json", FileAccess::READ);
		if (f.is_valid()) {
			Dictionary root = JSON::parse_string(f->get_as_text());
			Array keys = root.keys();
			for (int i = 0; i < keys.size(); ++i) {
				Dictionary e = root[keys[i]];
				ParallaxMap pm;
				pm.height = e.get("height", String());
				pm.scale = (float)(double)e.get("scale", 0.0);
				m.insert(keys[i], pm);
			}
		}
	}
	return m;
}
Ref<Texture2D> specular_map_for(const Ref<Texture2D> &albedo) { return table_texture_for(specular_maps(), albedo); }
Ref<Shader> &player_color_shader() {
	static Ref<Shader> &sh = *new Ref<Shader>();
	return sh;
}
HashMap<uint64_t, Ref<ShaderMaterial>> &player_color_mats() {
	static auto &m = *new HashMap<uint64_t, Ref<ShaderMaterial>>();
	return m;
}

// 0 A.D. MaterialManager: materialmgr.quality (Options "Shader effects", 0-10,
// read once: "REQUIRES GAME RESTART") picks a material's <alternative> when its
// quality attribute is above the setting. art/materials/*_parallax_spec.xml fall
// back to *_norm_spec.xml at quality="8", and those to the plain material at "3".
static float material_quality() {
	static const float q = CLAMP((float)rtso::user_config::get_number("materialmgr.quality"), 0.0f, 10.0f);
	return q;
}
static bool quality_normal_maps() { return material_quality() >= 3.0f; }
static bool quality_parallax() { return material_quality() >= 8.0f; }

// Opaque skins with a specular map or a parallax height map also take the shader
// without a mask, as BaseMaterial3D has no per-pixel specular nor 0 A.D.'s parallax.
Ref<ShaderMaterial> player_color_material(const Ref<BaseMaterial3D> &bm) {
	const uint64_t id = bm->get_instance_id();
	if (Ref<ShaderMaterial> *hit = player_color_mats().getptr(id)) return *hit;
	Ref<ShaderMaterial> out;
	Ref<Texture2D> tex = bm->get_texture(BaseMaterial3D::TEXTURE_ALBEDO);
	if (tex.is_valid()) {
		const PlayerMask *pm = player_masks().getptr(image_key(tex));
		Ref<Texture2D> mask = pm && ResourceLoader::get_singleton()->exists(pm->mask)
				? Ref<Texture2D>(ResourceLoader::get_singleton()->load(pm->mask))
				: Ref<Texture2D>();
		Ref<Texture2D> spec = quality_normal_maps() ? specular_map_for(tex) : Ref<Texture2D>();
		const bool opaque = bm->get_transparency() == BaseMaterial3D::TRANSPARENCY_DISABLED;
		const ParallaxMap *px = parallax_maps().getptr(image_key(tex));
		Ref<Texture2D> height = quality_parallax() && px && px->scale > 0.0f && ResourceLoader::get_singleton()->exists(px->height)
				? Ref<Texture2D>(ResourceLoader::get_singleton()->load(px->height))
				: Ref<Texture2D>();
		Ref<Shader> &sh = player_color_shader();
		if (sh.is_null()) sh = ResourceLoader::get_singleton()->load("res://shaders/player_color.gdshader");
		if ((mask.is_valid() || ((spec.is_valid() || height.is_valid()) && opaque)) && sh.is_valid()) {
			out.instantiate();
			out->set_shader(sh);
			out->set_shader_parameter("albedo_tex", tex);
			if (mask.is_valid()) {
				out->set_shader_parameter("mask_tex", mask);
				out->set_shader_parameter("bake_tint", Vector3(pm->tint.r, pm->tint.g, pm->tint.b));
			}
			out->set_shader_parameter("albedo", bm->get_albedo());
			out->set_shader_parameter("srgb_vertex_color", bm->get_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR));
			out->set_shader_parameter("roughness", bm->get_roughness());
			out->set_shader_parameter("metallic", bm->get_metallic());
			out->set_shader_parameter("specular", bm->get_specular());
			Ref<Texture2D> nm = quality_normal_maps() ? normal_map_for(tex) : Ref<Texture2D>();
			out->set_shader_parameter("use_normal_map", nm.is_valid());
			if (nm.is_valid()) out->set_shader_parameter("normal_tex", nm);
			out->set_shader_parameter("use_specular_map", spec.is_valid());
			if (spec.is_valid()) out->set_shader_parameter("specular_tex", spec);
			// The heights only line up with the bumps the normal map draws.
			const bool parallax = height.is_valid() && nm.is_valid();
			out->set_shader_parameter("use_parallax", parallax);
			if (parallax) {
				out->set_shader_parameter("height_tex", height);
				out->set_shader_parameter("parallax_scale", px->scale);
			}
		}
	}
	player_color_mats().insert(id, out);
	return out;
}
} // namespace

// tools/extract_0ad_structures.py: civs whose 0 A.D. structure actor is the
// same share one GLB, named in data/building_models.json.
String ModelLibrary::building_glb_id(const String &id) {
	static auto &aliases = *new HashMap<String, String>();
	static bool loaded = false;
	if (!loaded) {
		loaded = true;
		Ref<FileAccess> f = FileAccess::open("res://data/building_models.json", FileAccess::READ);
		if (f.is_valid()) {
			Dictionary root = JSON::parse_string(f->get_as_text());
			Array keys = root.keys();
			for (int i = 0; i < keys.size(); ++i) aliases.insert(keys[i], root[keys[i]]);
		}
	}
	if (const String *a = aliases.getptr(id)) return *a;
	return id;
}

bool ModelLibrary::is_player_color_material(const Ref<Material> &mat) {
	Ref<ShaderMaterial> sm = mat;
	return sm.is_valid() && sm->get_shader().is_valid() && sm->get_shader() == player_color_shader();
}

void ModelLibrary::set_player_color(Node *n, const Color &c) {
	if (!n) return;
	if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(n)) {
		const int count = mi->get_mesh().is_valid() ? mi->get_mesh()->get_surface_count() : 0;
		for (int s = 0; s < count; ++s) {
			if (is_player_color_material(mi->get_active_material(s))) {
				mi->set_instance_shader_parameter("player_color", c);
				break;
			}
		}
	}
	for (int i = 0; i < n->get_child_count(); ++i) set_player_color(n->get_child(i), c);
}

void ModelLibrary::apply_imported_material_fixups(Node *n) {
	if (!n) return;
	MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(n);
	if (mi && mi->get_mesh().is_valid()) {
		Ref<Mesh> mesh = mi->get_mesh();
		for (int s = 0; s < mesh->get_surface_count(); ++s) {
			Ref<Material> mat = mi->get_active_material(s);
			if (!mat.is_valid()) mat = mesh->surface_get_material(s);
			Ref<BaseMaterial3D> bm = mat;
			if (!bm.is_valid()) continue;
			// Packed 0 A.D. atlases bleed into noise at RTS camera distance
			// when mipmapped; keep them sharp (0 A.D. uses aniso + padded DDS).
			bm->set_texture_filter(BaseMaterial3D::TEXTURE_FILTER_LINEAR);
			// 0 A.D. UV0 = tiling albedo, UV1 = unique AO. We bake AO into COLOR_0.
			bm->set_feature(BaseMaterial3D::FEATURE_AMBIENT_OCCLUSION, false);
			bool has_vcol = false;
			if (mesh->get_surface_count() > s) {
				Array arrays = mesh->surface_get_arrays(s);
				if (arrays.size() > Mesh::ARRAY_COLOR && arrays[Mesh::ARRAY_COLOR].get_type() != Variant::NIL) {
					PackedColorArray cols = arrays[Mesh::ARRAY_COLOR];
					has_vcol = cols.size() > 0;
				}
			}
			bm->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, has_vcol);
			bm->set_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR, has_vcol);
			Ref<ShaderMaterial> pc = player_color_material(bm);
			if (pc.is_valid()) {
				mi->set_surface_override_material(s, pc);
			} else if (quality_normal_maps() && !bm->get_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING)) {
				Ref<Texture2D> nm = normal_map_for(bm->get_texture(BaseMaterial3D::TEXTURE_ALBEDO));
				if (nm.is_valid()) {
					bm->set_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING, true);
					bm->set_texture(BaseMaterial3D::TEXTURE_NORMAL, nm);
				}
			}
		}
	}
	for (int i = 0; i < n->get_child_count(); ++i) {
		apply_imported_material_fixups(n->get_child(i));
	}
}

static String tool_tag_of(const String &nm) {
	int i = nm.find("tool_");
	if (i < 0) return String();
	String rest = nm.substr(i + 5);
	int us = rest.rfind("_");
	if (us > 0) {
		String tail = rest.substr(us + 1);
		bool digits = tail.length() > 0;
		for (int k = 0; k < tail.length(); ++k) {
			char32_t ch = tail[k];
			if (ch < '0' || ch > '9') {
				digits = false;
				break;
			}
		}
		if (digits) rest = rest.substr(0, us);
	}
	return rest;
}

static void set_tool_drawn(Node3D *n3, bool on) {
	n3->set_visible(on);
	if (GeometryInstance3D *gi = Object::cast_to<GeometryInstance3D>(n3)) {
		// Skinned MeshInstance3D under Skeleton3D often ignores visible;
		// drop it from every camera layer and fade the instance.
		gi->set_transparency(on ? 0.0f : 1.0f);
		gi->set_layer_mask(on ? 1u : 0u);
	}
}

void ModelLibrary::apply_clip_tools(Node *root, const StringName &clip) {
	if (!root) return;
	std::vector<Node3D *> tools;
	std::vector<Node *> stack;
	stack.push_back(root);
	while (!stack.empty()) {
		Node *n = stack.back();
		stack.pop_back();
		for (int i = 0; i < n->get_child_count(); ++i)
			stack.push_back(n->get_child(i));
		Node3D *n3 = Object::cast_to<Node3D>(n);
		if (!n3) continue;
		if (tool_tag_of(n3->get_name()).is_empty()) continue;
		tools.push_back(n3);
		set_tool_drawn(n3, false);
	}
	if (tools.empty()) return;
	String tag = String(clip);
	// 0 A.D. carry_* variants empty weapon_R/weapon_L: the shuttle replaces the tool.
	if (tag.begins_with("Carry")) return;
	// Formation variant clips: testudo_top's empties weapon_R ("FormBare_"), the rest keep it.
	if (tag.begins_with("FormBare_")) return;
	if (tag.begins_with("Form_")) tag = "Weapon";
	if (tag == "Gather") tag = "GatherTree";
	if (tag == "Idle" || tag == "Walk" || tag == "Run" ||
			tag == "Attack" || tag == "Death" || tag == "Slaughter" || tag == "Heal" ||
			tag == "Promotion")
		tag = "Weapon";
	Node3D *pick = nullptr;
	for (Node3D *n3 : tools) {
		if (tool_tag_of(n3->get_name()) == tag) {
			pick = n3;
			break;
		}
	}
	if (!pick) {
		for (Node3D *n3 : tools) {
			if (tool_tag_of(n3->get_name()) == String("Weapon")) {
				pick = n3;
				break;
			}
		}
	}
	if (pick) set_tool_drawn(pick, true);
}

static void collect_mesh_xforms(Node *n, const Transform3D &acc, Vector<MeshXform> &out) {
	Node3D *n3 = Object::cast_to<Node3D>(n);
	if (n3 && !n3->is_visible()) return;
	if (GeometryInstance3D *gi = Object::cast_to<GeometryInstance3D>(n)) {
		if (gi->get_layer_mask() == 0) return;
	}
	Transform3D xf = n3 ? acc * n3->get_transform() : acc;
	MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(n);
	if (mi && mi->get_mesh().is_valid()) {
		MeshXform mx;
		mx.mi = mi;
		mx.xf = xf;
		out.push_back(mx);
	}
	for (int i = 0; i < n->get_child_count(); ++i) {
		collect_mesh_xforms(n->get_child(i), xf, out);
	}
}

static uint64_t material_merge_key(const Ref<Material> &mat) {
	Ref<BaseMaterial3D> bm = mat;
	if (bm.is_valid()) {
		Ref<Texture2D> tex = bm->get_texture(BaseMaterial3D::TEXTURE_ALBEDO);
		if (tex.is_valid()) return (uint64_t)tex->get_rid().get_id();
	}
	if (mat.is_valid()) return (uint64_t)mat->get_rid().get_id();
	return 0;
}

struct SurfBucket {
	Ref<Material> mat;
	bool own_surface = false; // a bone-attached prop keeps its own texture and surface
	PackedVector3Array verts;
	PackedVector3Array norms;
	PackedVector2Array uvs;
	PackedColorArray colors;
	PackedInt32Array indices;
	// 4 floats per vertex, only for normal-mapped materials (tangents_wanted).
	PackedFloat32Array tangents;
};

// A normal map bends lighting along the mesh tangents, so merged and
// CPU-skinned copies keep them; other materials skip the extra array.
static bool tangents_wanted(const Ref<Material> &mat) {
	Ref<BaseMaterial3D> bm = mat;
	if (bm.is_valid()) return bm->get_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING);
	Ref<ShaderMaterial> sm = mat;
	return sm.is_valid() && (bool)sm->get_shader_parameter("use_normal_map");
}

static void push_tangent(PackedFloat32Array &out, const Basis &b, const PackedFloat32Array &src, int v) {
	Vector3 t(1, 0, 0);
	float w = 1.0f;
	if (v * 4 + 3 < src.size()) {
		t = b.xform(Vector3(src[v * 4], src[v * 4 + 1], src[v * 4 + 2])).normalized();
		w = src[v * 4 + 3];
	}
	out.push_back(t.x);
	out.push_back(t.y);
	out.push_back(t.z);
	out.push_back(w);
}

static void append_surface(SurfBucket &b, const Transform3D &xf, const Array &arrays) {
	PackedVector3Array verts = arrays[Mesh::ARRAY_VERTEX];
	if (verts.size() == 0) return;
	PackedVector3Array norms = arrays[Mesh::ARRAY_NORMAL];
	PackedVector2Array uvs = arrays[Mesh::ARRAY_TEX_UV];
	PackedColorArray cols;
	if (arrays.size() > Mesh::ARRAY_COLOR && arrays[Mesh::ARRAY_COLOR].get_type() != Variant::NIL) {
		cols = arrays[Mesh::ARRAY_COLOR];
	}
	PackedInt32Array idx = arrays[Mesh::ARRAY_INDEX];
	const bool tan = tangents_wanted(b.mat);
	PackedFloat32Array tans;
	if (tan && arrays.size() > Mesh::ARRAY_TANGENT && arrays[Mesh::ARRAY_TANGENT].get_type() != Variant::NIL) {
		tans = arrays[Mesh::ARRAY_TANGENT];
	}

	const int base = b.verts.size();
	for (int v = 0; v < verts.size(); ++v) {
		b.verts.push_back(xf.xform(verts[v]));
		if (v < norms.size()) {
			b.norms.push_back(xf.basis.xform(norms[v]).normalized());
		} else {
			b.norms.push_back(Vector3(0, 1, 0));
		}
		if (v < uvs.size()) b.uvs.push_back(uvs[v]);
		else b.uvs.push_back(Vector2());
		if (v < cols.size()) b.colors.push_back(cols[v]);
		else b.colors.push_back(Color(1, 1, 1, 1));
		if (tan) push_tangent(b.tangents, xf.basis, tans, v);
	}
	if (idx.size() > 0) {
		for (int k = 0; k < idx.size(); ++k) {
			b.indices.push_back(idx[k] + base);
		}
	} else {
		for (int v = 0; v < verts.size(); ++v) {
			b.indices.push_back(base + v);
		}
	}
}

static void append_bucket_surface(const Ref<ArrayMesh> &mesh, const SurfBucket &b) {
	if (b.verts.size() == 0) return;
	Array out_arr;
	out_arr.resize(Mesh::ARRAY_MAX);
	out_arr[Mesh::ARRAY_VERTEX] = b.verts;
	out_arr[Mesh::ARRAY_NORMAL] = b.norms;
	out_arr[Mesh::ARRAY_TEX_UV] = b.uvs;
	out_arr[Mesh::ARRAY_COLOR] = b.colors;
	out_arr[Mesh::ARRAY_INDEX] = b.indices;
	if (b.tangents.size() == b.verts.size() * 4) out_arr[Mesh::ARRAY_TANGENT] = b.tangents;
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, out_arr);
	if (b.mat.is_valid()) {
		mesh->surface_set_material(mesh->get_surface_count() - 1, b.mat);
	}
}

static Ref<ArrayMesh> mesh_from_buckets(std::vector<SurfBucket> &buckets, int max_surfaces) {
	std::sort(buckets.begin(), buckets.end(), [](const SurfBucket &a, const SurfBucket &b) {
		return a.verts.size() > b.verts.size();
	});
	Ref<ArrayMesh> merged;
	merged.instantiate();
	int n = (int)buckets.size();
	if (max_surfaces > 0 && n > max_surfaces) n = max_surfaces;
	for (int i = 0; i < n; ++i) append_bucket_surface(merged, buckets[i]);
	return merged;
}

static void collect_buckets(const Vector<MeshXform> &instances, bool skip_tiny,
		std::vector<SurfBucket> &out) {
	std::unordered_map<uint64_t, int> key_to_idx;
	for (int i = 0; i < instances.size(); ++i) {
		MeshInstance3D *mi = instances[i].mi;
		Ref<Mesh> src = mi->get_mesh();
		if (!src.is_valid()) continue;
		const Transform3D &xf = instances[i].xf;
		for (int s = 0; s < src->get_surface_count(); ++s) {
			Array arrays = src->surface_get_arrays(s);
			if (arrays.size() == 0) continue;
			PackedVector3Array verts = arrays[Mesh::ARRAY_VERTEX];
			// Heads, helmets, and rider bits are often <24 verts — do not drop them
			if (skip_tiny && verts.size() < 3) continue;
			Ref<Material> mat = mi->get_active_material(s);
			if (!mat.is_valid()) mat = src->surface_get_material(s);
			uint64_t key = material_merge_key(mat);
			auto it = key_to_idx.find(key);
			int bi;
			if (it == key_to_idx.end()) {
				bi = (int)out.size();
				key_to_idx[key] = bi;
				SurfBucket b;
				b.mat = mat;
				out.push_back(std::move(b));
			} else {
				bi = it->second;
			}
			append_surface(out[bi], xf, arrays);
		}
	}
}

static Ref<ArrayMesh> flatten_to_one_surface(std::vector<SurfBucket> &buckets) {
	if (buckets.empty()) return Ref<ArrayMesh>();
	SurfBucket all;
	all.mat = buckets[0].mat;
	const bool tan = tangents_wanted(all.mat);
	for (SurfBucket &b : buckets) {
		const int base = all.verts.size();
		for (int i = 0; i < b.verts.size(); ++i) {
			all.verts.push_back(b.verts[i]);
			all.norms.push_back(i < b.norms.size() ? b.norms[i] : Vector3(0, 1, 0));
			all.uvs.push_back(i < b.uvs.size() ? b.uvs[i] : Vector2());
			all.colors.push_back(i < b.colors.size() ? b.colors[i] : Color(1, 1, 1, 1));
			if (tan) push_tangent(all.tangents, Basis(), b.tangents, i);
		}
		for (int k = 0; k < b.indices.size(); ++k)
			all.indices.push_back(b.indices[k] + base);
	}
	std::vector<SurfBucket> one;
	one.push_back(std::move(all));
	return mesh_from_buckets(one, 1);
}

static Ref<ArrayMesh> merge_mesh_instances(const Vector<MeshXform> &instances) {
	std::vector<SurfBucket> buckets;
	collect_buckets(instances, false, buckets);
	return mesh_from_buckets(buckets, 0);
}

static Ref<ArrayMesh> merge_crowd_mesh(const Vector<MeshXform> &instances) {
	std::vector<SurfBucket> buckets;
	collect_buckets(instances, false, buckets);
	// Godot MultiMesh draws one surface reliably; flatten so heads/riders stay.
	return flatten_to_one_surface(buckets);
}

static Skeleton3D *find_skeleton(Node *n) {
	if (!n) return nullptr;
	if (Skeleton3D *s = Object::cast_to<Skeleton3D>(n)) return s;
	for (int i = 0; i < n->get_child_count(); ++i) {
		if (Skeleton3D *s = find_skeleton(n->get_child(i))) return s;
	}
	return nullptr;
}

static void force_update_skeletons(Node *n) {
	if (!n) return;
	if (Skeleton3D *s = Object::cast_to<Skeleton3D>(n)) {
		s->force_update_all_bone_transforms();
	}
	for (int i = 0; i < n->get_child_count(); ++i) {
		force_update_skeletons(n->get_child(i));
	}
}

static Transform3D xf_to_ancestor(Node3D *from, Node *root) {
	Transform3D xf;
	Node *cur = from;
	while (cur && cur != root) {
		if (Node3D *n3 = Object::cast_to<Node3D>(cur)) {
			xf = n3->get_transform() * xf;
		}
		cur = cur->get_parent();
	}
	return xf;
}

static PackedInt32Array bone_indices_from_arrays(const Array &arrays, int vert_count) {
	PackedInt32Array out;
	if (arrays.size() <= Mesh::ARRAY_BONES) return out;
	Variant v = arrays[Mesh::ARRAY_BONES];
	if (v.get_type() == Variant::PACKED_INT32_ARRAY) {
		out = v;
	} else if (v.get_type() == Variant::PACKED_FLOAT32_ARRAY) {
		PackedFloat32Array f = v;
		out.resize(f.size());
		for (int i = 0; i < f.size(); ++i) out[i] = (int32_t)f[i];
	}
	(void)vert_count;
	return out;
}

bool ModelLibrary::capture_crowd_skin(Node *root, CrowdSkin &out) {
	out = CrowdSkin();
	if (!root) return false;
	out.root = root;
	force_update_skeletons(root);

	Vector<MeshXform> instances;
	collect_mesh_xforms(root, Transform3D(), instances);
	for (int i = 0; i < instances.size(); ++i) {
		MeshInstance3D *mi = instances[i].mi;
		Ref<Mesh> src = mi->get_mesh();
		if (!src.is_valid()) continue;

		CrowdSkin::Rig rig;
		// A rigid prop under a BoneAttachment3D (0 A.D. carry shuttles on
		// weapon_R) follows that one bone: skin it fully to the bone, with the
		// prop's offset from the bone as its bind pose.
		BoneAttachment3D *attach = nullptr;
		for (Node *p = mi->get_parent(); p && p != root; p = p->get_parent()) {
			attach = Object::cast_to<BoneAttachment3D>(p);
			if (attach) break;
		}
		Skeleton3D *attach_skel = attach ? Object::cast_to<Skeleton3D>(attach->get_parent()) : nullptr;
		const int attach_bone = attach_skel ? attach->get_bone_idx() : -1;
		if (attach_skel && attach_bone >= 0 && attach_bone < attach_skel->get_bone_count()) {
			rig.skel = attach_skel;
			rig.xf = xf_to_ancestor(attach_skel, root);
			rig.bone_index.push_back(attach_bone);
			rig.bind_pose.push_back(xf_to_ancestor(mi, attach));
		} else {
			attach = nullptr;
			NodePath sp = mi->get_skeleton_path();
			if (!sp.is_empty() && mi->has_node(sp)) {
				rig.skel = Object::cast_to<Skeleton3D>(mi->get_node_or_null(sp));
			}
			if (!rig.skel) rig.skel = find_skeleton(mi->get_parent() ? mi->get_parent() : root);
			rig.xf = rig.skel ? xf_to_ancestor(rig.skel, root) : instances[i].xf;
		}
		if (rig.skel && !attach) {
			Ref<Skin> skin = mi->get_skin();
			if (skin.is_valid() && skin->get_bind_count() > 0) {
				const int nb = skin->get_bind_count();
				rig.bone_index.resize((size_t)nb);
				rig.bind_pose.resize((size_t)nb);
				for (int b = 0; b < nb; ++b) {
					int bone = skin->get_bind_bone(b);
					if (bone < 0) {
						StringName bn = skin->get_bind_name(b);
						if (bn != StringName()) bone = rig.skel->find_bone(bn);
					}
					rig.bone_index[b] = bone;
					rig.bind_pose[b] = skin->get_bind_pose(b);
				}
			} else {
				const int nb = rig.skel->get_bone_count();
				rig.bone_index.resize((size_t)nb);
				rig.bind_pose.resize((size_t)nb);
				for (int b = 0; b < nb; ++b) {
					rig.bone_index[b] = b;
					rig.bind_pose[b] = rig.skel->get_bone_global_rest(b).affine_inverse();
				}
			}
		}
		const int rig_i = (int)out.rigs.size();
		out.rigs.push_back(std::move(rig));

		for (int s = 0; s < src->get_surface_count(); ++s) {
			Array arrays = src->surface_get_arrays(s);
			if (arrays.size() == 0) continue;
			PackedVector3Array verts = arrays[Mesh::ARRAY_VERTEX];
			if (verts.size() < 3) continue;
			CrowdSkin::Surf surf;
			Ref<Material> mat = mi->get_active_material(s);
			if (!mat.is_valid()) mat = src->surface_get_material(s);
			surf.mat = mat;
			surf.rigid_xf = instances[i].xf;
			surf.rest_v.resize((size_t)verts.size());
			for (int v = 0; v < verts.size(); ++v) surf.rest_v[v] = verts[v];
			PackedVector3Array norms = arrays[Mesh::ARRAY_NORMAL];
			surf.rest_n.resize((size_t)verts.size());
			for (int v = 0; v < verts.size(); ++v) {
				surf.rest_n[v] = (v < norms.size()) ? norms[v] : Vector3(0, 1, 0);
			}
			if (tangents_wanted(mat) && arrays.size() > Mesh::ARRAY_TANGENT && arrays[Mesh::ARRAY_TANGENT].get_type() != Variant::NIL) {
				PackedFloat32Array tans = arrays[Mesh::ARRAY_TANGENT];
				if (tans.size() >= verts.size() * 4) {
					surf.rest_t.resize((size_t)verts.size() * 4);
					for (int k = 0; k < verts.size() * 4; ++k) surf.rest_t[k] = tans[k];
				}
			}
			PackedVector2Array uvs = arrays[Mesh::ARRAY_TEX_UV];
			surf.uv.resize((size_t)verts.size());
			for (int v = 0; v < verts.size(); ++v) {
				surf.uv[v] = (v < uvs.size()) ? uvs[v] : Vector2();
			}
			PackedColorArray cols;
			if (arrays.size() > Mesh::ARRAY_COLOR && arrays[Mesh::ARRAY_COLOR].get_type() != Variant::NIL) {
				cols = arrays[Mesh::ARRAY_COLOR];
			}
			surf.col.resize((size_t)verts.size());
			for (int v = 0; v < verts.size(); ++v) {
				surf.col[v] = (v < cols.size()) ? cols[v] : Color(1, 1, 1, 1);
			}
			PackedInt32Array idx = arrays[Mesh::ARRAY_INDEX];
			surf.idx.resize((size_t)idx.size());
			for (int k = 0; k < idx.size(); ++k) surf.idx[k] = idx[k];
			PackedInt32Array bones;
			PackedFloat32Array weights;
			if (attach) {
				bones.resize(verts.size());
				bones.fill(0);
				weights.resize(verts.size());
				weights.fill(1.0f);
			} else {
				bones = bone_indices_from_arrays(arrays, verts.size());
				if (arrays.size() > Mesh::ARRAY_WEIGHTS && arrays[Mesh::ARRAY_WEIGHTS].get_type() != Variant::NIL) {
					weights = arrays[Mesh::ARRAY_WEIGHTS];
				}
			}
			surf.bones.resize((size_t)bones.size());
			for (int k = 0; k < bones.size(); ++k) surf.bones[k] = bones[k];
			surf.weights.resize((size_t)weights.size());
			for (int k = 0; k < weights.size(); ++k) surf.weights[k] = weights[k];
			int stride = (verts.size() > 0 && bones.size() > 0) ? bones.size() / verts.size() : 0;
			surf.stride = stride < 1 ? 4 : stride;
			surf.attached = attach != nullptr;
			if (out.rigs[rig_i].skel && bones.size() >= verts.size()) surf.rig_i = rig_i;
			else surf.rig_i = -1;
			out.surfs.push_back(std::move(surf));
		}
	}
	out.captured = !out.surfs.empty();
	return out.captured;
}

Ref<ArrayMesh> ModelLibrary::bake_crowd_skin(CrowdSkin &skin, const Ref<ArrayMesh> &reuse) {
	if (!skin.captured || !skin.root) return Ref<ArrayMesh>();
	force_update_skeletons(skin.root);

	std::vector<std::vector<Transform3D>> palettes(skin.rigs.size());
	for (size_t r = 0; r < skin.rigs.size(); ++r) {
		CrowdSkin::Rig &rig = skin.rigs[r];
		if (!rig.skel) continue;
		palettes[r].resize(rig.bone_index.size());
		for (size_t b = 0; b < rig.bone_index.size(); ++b) {
			int bone = rig.bone_index[b];
			if (bone < 0 || bone >= rig.skel->get_bone_count()) {
				palettes[r][b] = Transform3D();
				continue;
			}
			palettes[r][b] = rig.skel->get_bone_global_pose(bone) * rig.bind_pose[b];
		}
	}

	std::vector<SurfBucket> buckets;
	std::unordered_map<uint64_t, int> key_to_idx;
	auto bucket_for = [&](const Ref<Material> &mat, bool attached) -> SurfBucket & {
		uint64_t key = material_merge_key(mat) ^ (attached ? (1ull << 63) : 0ull);
		auto it = key_to_idx.find(key);
		if (it == key_to_idx.end()) {
			int bi = (int)buckets.size();
			key_to_idx[key] = bi;
			SurfBucket b;
			b.mat = mat;
			b.own_surface = attached;
			buckets.push_back(std::move(b));
			return buckets.back();
		}
		return buckets[it->second];
	};

	for (const CrowdSkin::Surf &surf : skin.surfs) {
		SurfBucket &b = bucket_for(surf.mat, surf.attached);
		const bool want_t = tangents_wanted(surf.mat);
		const int n = (int)surf.rest_v.size();
		const int base = b.verts.size();
		const bool skinned = surf.rig_i >= 0 && surf.rig_i < (int)palettes.size() && !palettes[surf.rig_i].empty();
		const Transform3D xf = skinned ? skin.rigs[surf.rig_i].xf : surf.rigid_xf;
		const std::vector<Transform3D> *pal = skinned ? &palettes[surf.rig_i] : nullptr;
		for (int v = 0; v < n; ++v) {
			Vector3 p = surf.rest_v[v];
			Vector3 nrm = surf.rest_n[v];
			const bool has_t = !surf.rest_t.empty();
			Vector3 tg = has_t ? Vector3(surf.rest_t[v * 4], surf.rest_t[v * 4 + 1], surf.rest_t[v * 4 + 2]) : Vector3(1, 0, 0);
			if (pal) {
				Vector3 skinned_p(0, 0, 0);
				Vector3 sn(0, 0, 0);
				Vector3 st(0, 0, 0);
				float wsum = 0.0f;
				int stride = surf.stride;
				if ((int)surf.bones.size() >= (v + 1) * stride) {
					for (int k = 0; k < stride && k < 8; ++k) {
						int bi = surf.bones[v * stride + k];
						float w = 0.0f;
						int wi = v * stride + k;
						if (wi < (int)surf.weights.size()) w = surf.weights[wi];
						if (w == 0.0f || bi < 0 || bi >= (int)pal->size()) continue;
						skinned_p += (*pal)[bi].xform(p) * w;
						sn += (*pal)[bi].basis.xform(nrm) * w;
						if (has_t) st += (*pal)[bi].basis.xform(tg) * w;
						wsum += w;
					}
				}
				if (wsum < 0.0001f) {
					skinned_p = p;
					sn = nrm;
					st = tg;
				} else if (Math::abs(wsum - 1.0f) > 0.01f) {
					skinned_p /= wsum;
					sn /= wsum;
				}
				p = skinned_p;
				nrm = sn;
				tg = st;
			}
			b.verts.push_back(xf.xform(p));
			b.norms.push_back(xf.basis.xform(nrm).normalized());
			b.uvs.push_back(v < (int)surf.uv.size() ? surf.uv[v] : Vector2());
			b.colors.push_back(v < (int)surf.col.size() ? surf.col[v] : Color(1, 1, 1, 1));
			if (want_t) {
				const Vector3 t = xf.basis.xform(tg).normalized();
				b.tangents.push_back(t.x);
				b.tangents.push_back(t.y);
				b.tangents.push_back(t.z);
				b.tangents.push_back(has_t ? surf.rest_t[v * 4 + 3] : 1.0f);
			}
		}
		if (!surf.idx.empty()) {
			for (int32_t ix : surf.idx) b.indices.push_back(ix + base);
		} else {
			for (int v = 0; v < n; ++v) b.indices.push_back(base + v);
		}
	}
	if (buckets.empty()) return Ref<ArrayMesh>();
	Ref<ArrayMesh> mesh = reuse;
	if (mesh.is_valid()) mesh->clear_surfaces();
	(void)mesh;
	// The unit's own parts share one atlas and flatten to one surface; a
	// carried prop has its own skin, so it stays a second surface.
	std::vector<SurfBucket> body, props;
	for (SurfBucket &b : buckets) (b.own_surface ? props : body).push_back(std::move(b));
	Ref<ArrayMesh> out = flatten_to_one_surface(body);
	if (out.is_valid()) {
		for (const SurfBucket &b : props) append_bucket_surface(out, b);
	}
	return out;
}

Ref<ArrayMesh> ModelLibrary::bake_posed_crowd_mesh(Node *root) {
	CrowdSkin skin;
	if (!capture_crowd_skin(root, skin)) return Ref<ArrayMesh>();
	return bake_crowd_skin(skin);
}

// A mesh-only template model (tools/extract_0ad_assets.py --templates) has its
// class model's bones and plays its clips: rename its model node to the
// donor's, so the donor's track paths resolve, and share the donor's library.
Ref<PackedScene> ModelLibrary::borrow_clips(Node *inst, const String &donor) {
	const Entry &d = get("units", donor);
	if (!d.scene.is_valid() || !d.animated) return Ref<PackedScene>();
	Node *dinst = d.scene->instantiate();
	AnimationPlayer *dap = find_animation_player(dinst);
	Ref<AnimationLibrary> lib = dap ? dap->get_animation_library(StringName()) : Ref<AnimationLibrary>();
	// Track paths start at the donor's model node: "athen_cavalry/Skeleton3D:hip".
	String model_node;
	for (int i = 0; lib.is_valid() && i < dinst->get_child_count() && model_node.is_empty(); ++i) {
		Node *c = dinst->get_child(i);
		if (c != dap && Object::cast_to<Node3D>(c)) model_node = c->get_name();
	}
	memdelete(dinst);
	if (!lib.is_valid() || model_node.is_empty()) return Ref<PackedScene>();
	Node *mine = nullptr;
	for (int i = 0; i < inst->get_child_count() && !mine; ++i) {
		if (Object::cast_to<Node3D>(inst->get_child(i))) mine = inst->get_child(i);
	}
	if (!mine) return Ref<PackedScene>();
	mine->set_name(model_node);
	AnimationPlayer *ap = memnew(AnimationPlayer);
	ap->set_name("AnimationPlayer");
	inst->add_child(ap);
	ap->set_owner(inst);
	ap->set_root_node(NodePath(".."));
	ap->add_animation_library(StringName(), lib);
	inst->set_scene_file_path(String());
	Ref<PackedScene> ps;
	ps.instantiate();
	if (ps->pack(inst) != OK) return Ref<PackedScene>();
	return ps;
}

ModelLibrary::Entry ModelLibrary::load(const String &category, const String &id) {
	String key = category + String("/") + id;
	Entry e;

	Vector<String> candidates;
	if (category == "buildings") {
		const String glb = building_glb_id(id);
		if (glb != id) candidates.push_back(String("res://assets/models/buildings/") + glb + String(".glb"));
	}
	candidates.push_back(String("res://assets/models/") + category + String("/") + id + String(".glb"));
	candidates.push_back(String("res://assets/models/units/") + id + String(".glb"));
	candidates.push_back(String("res://assets/models/buildings/") + id + String(".glb"));
	candidates.push_back(String("res://assets/models/vegetation/") + id + String(".glb"));
	candidates.push_back(String("res://assets/models/") + id + String(".glb"));

	// Fallbacks only for IDs that still share a stand-in mesh
	if (category == "units") {
		int us = id.find("_");
		if (us > 0 && us < id.length() - 1) {
			String base = id.substr(us + 1);
			candidates.push_back(String("res://assets/models/units/") + base + String(".glb"));
		}
		if (id == "healer") {
			candidates.push_back("res://assets/models/units/villager.glb");
		}
	} else if (category == "buildings") {
		int us = id.find("_");
		if (us > 0 && us < id.length() - 1) {
			String base = id.substr(us + 1);
			const String glb = building_glb_id(base);
			if (glb != base) candidates.push_back(String("res://assets/models/buildings/") + glb + String(".glb"));
			candidates.push_back(String("res://assets/models/buildings/") + base + String(".glb"));
		}
		if (id.ends_with("gate") || id == "gate") {
			candidates.push_back("res://assets/models/buildings/wall.glb");
		}
		if (id.ends_with("_rubble") || id.begins_with("rubble_")) {
			candidates.push_back("res://assets/models/buildings/rubble_large.glb");
			candidates.push_back("res://assets/models/buildings/rubble_small.glb");
		}
		if (id.begins_with("foundation_")) {
			candidates.push_back("res://assets/models/buildings/foundation_5x5.glb");
			candidates.push_back("res://assets/models/buildings/foundation_large.glb");
			candidates.push_back("res://assets/models/buildings/foundation_small.glb");
		}
	}

	for (int i = 0; i < candidates.size(); ++i) {
		const String &path = candidates[i];
		if (ResourceLoader::get_singleton()->exists(path)) {
			Ref<PackedScene> ps = ResourceLoader::get_singleton()->load(path);
			if (ps.is_valid()) {
				Node *inst = ps->instantiate();
				if (inst) {
					const rtso::UnitTemplateModel *tm = category == "units"
							? rtso::find_unit_template_model(id.utf8().get_data()) : nullptr;
					if (tm && tm->clip_donor && !find_animation_player(inst)) {
						Ref<PackedScene> with_clips = borrow_clips(inst, String(tm->clip_donor));
						if (with_clips.is_valid()) ps = with_clips;
					}
					apply_imported_material_fixups(inst);
					apply_clip_tools(inst, StringName("Idle"));
					e.scene = ps;
					e.animated = find_animation_player(inst) != nullptr;
					Vector<MeshXform> mesh_instances;
					collect_mesh_xforms(inst, Transform3D(), mesh_instances);
					if (mesh_instances.size() > 0) {
						e.mesh = merge_mesh_instances(mesh_instances);
						if (category == "units") {
							e.crowd_mesh = merge_crowd_mesh(mesh_instances);
						} else {
							e.crowd_mesh = e.mesh;
						}
						if (e.mesh.is_valid()) {
							e.aabb = e.mesh->get_aabb();
							e.scale = 1.0f;
							float natural_h = e.aabb.size.y;
							float want_h = target_height(category, id, natural_h);
							if (natural_h > 0.05f && want_h > 0.05f)
								e.scale = want_h / natural_h;
							// A template model is drawn at its class model's scale, so a
							// hero's crest or a champion's spear doesn't shrink the body.
							if (tm && tm->class_model) {
								const Entry &cls = get(category, String(tm->class_model));
								if (cls.valid && cls.scale > 0.0f) e.scale = cls.scale;
							}
							// Props keep their 0 A.D. actor origin (a wreck sits partly under water).
							e.y_offset = e.animated || category == "props" ? 0.0f : -e.aabb.position.y * e.scale;
							e.valid = true;
							char buf[192];
							std::snprintf(buf, sizeof(buf), "%s/%s surfaces full=%d crowd=%d",
									category.utf8().get_data(), id.utf8().get_data(),
									e.mesh->get_surface_count(),
									e.crowd_mesh.is_valid() ? e.crowd_mesh->get_surface_count() : 0);
							Logger::debug("mesh", buf);
						}
					}
					memdelete(inst);
					if (e.valid) break;
				}
			}
		}
	}

	if (!e.valid) {
		if (category == "units") {
			e.mesh = Procedural::get_unit_mesh(id);
		} else if (category == "buildings") {
			e.mesh = Procedural::get_building_mesh(id);
		} else {
			e.mesh = Procedural::get_vegetation_mesh(id);
		}
		if (e.mesh.is_valid()) {
			e.aabb = e.mesh->get_aabb();
			e.scale = 1.0f;
			e.y_offset = -e.aabb.position.y;
			e.valid = true;
			e.crowd_mesh = e.mesh;
		}
	}

	cache().insert(key, e);
	return e;
}

const ModelLibrary::Entry &ModelLibrary::get(const String &category, const String &id) {
	String key = category + String("/") + id;
	Entry *found = cache().getptr(key);
	if (found) return *found;
	load(category, id);
	found = cache().getptr(key);
	static Entry empty;
	return found ? *found : empty;
}

void ModelLibrary::clear() {
	cache().clear();
	player_color_mats().clear();
}

} // namespace rtso
