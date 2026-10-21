#include "model_library.h"
#include "instance_params.h"
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
#include <godot_cpp/classes/texture2d_array.hpp>
#include <godot_cpp/classes/image.hpp>
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
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <vector>

using namespace godot;

namespace rtso {

HashMap<String, ModelLibrary::Entry> &ModelLibrary::cache() {
	static auto &c = *new HashMap<String, Entry>(); // leaked: no Godot dtor after the engine exits
	return c;
}

// 0.28 draws every animal at its actor's own size. The extractors fitted each
// animal GLB to a height of its own (a chicken 0.5, a horse 1.7), keeping the
// factor as the GLB root's scale, so an animal drawn at FAUNA_SCALE / that factor
// shows at 0.28's size next to the others: the civilians' scale
// (female_citizen.glb's root is 0.387, drawn at 1.68 tall as fitted).
static constexpr float FAUNA_SCALE = 0.39f;

static bool is_fauna_model(const String &id) {
	for (int f = 0; f < FAUNA_COUNT; ++f) {
		if (id == fauna_type_info((uint8_t)f).model_id) return true;
	}
	return false;
}

// The scale of the GLB's root node (the first scaled node from the scene root).
static float glb_root_scale(Node *n, int depth = 0) {
	for (int i = 0; i < n->get_child_count(); ++i) {
		Node3D *c = Object::cast_to<Node3D>(n->get_child(i));
		if (c && std::fabs(c->get_scale().x - 1.0f) > 1e-3f) return c->get_scale().x;
	}
	if (depth < 2) {
		for (int i = 0; i < n->get_child_count(); ++i) {
			const float r = glb_root_scale(n->get_child(i), depth + 1);
			if (r > 0.0f) return r;
		}
	}
	return 0.0f;
}

// A person on foot. The extractors fitted each 0.28 actor to a height of its own and
// kept the factor as the GLB root's scale; a bare body (a slinger, a citizen) comes out
// PERSON_RS_HEIGHT root-scaled units tall. A held spear, bow or staff reaches past the
// body in the bind pose, so a spearman's bounds run ~60% taller: sizing a person by its
// root scale draws a hoplite as tall as the citizen beside it, as 0.28 does.
static constexpr float PERSON_RS_HEIGHT = 4.98f;
static bool is_person_model(const String &id) {
	static const char *kNot[] = { "cavalry", "camel", "elephant", "eleph_", "chariot", "ram", "siege", "catapult",
		"bolt", "lithobolos", "oxybeles", "ballista", "onager", "mangonel", "ship", "boat", "trireme",
		"projectile", "plane", "wagon", "dog", "packed", "horse", "catafalque", "tower", "_cav" };
	for (const char *k : kNot)
		if (id.contains(k)) return false;
	return !is_fauna_model(id);
}

float ModelLibrary::target_height(const String &category, const String &id, float natural_h) {
	if (category == "units") {
		// Animals keep their height here; load() redraws them at one shared scale.
		for (int f = 0; f < FAUNA_COUNT; ++f) {
			if (id == fauna_type_info((uint8_t)f).model_id) return natural_h;
		}
		// So do the packed siege wagons (tools/extract_0ad_packed_siege.py).
		if (is_packed_siege_model(id.utf8().get_data())) return natural_h;
		// Flight Demo's P-51: 0.28's aeroplane.dae stands 5.5 units to a hoplite's 4.5,
		// so ~2.2 m (a ~9 m wingspan) beside the 1.8 m infantry.
		if (id == "plane") return 2.2f;
		// And the Germans' ox wagons (ram-wide, extract_0ad_assets.py ZIP_UNITS),
		// asked for by civ (germ_wagon) and drawn from wagon.glb.
		if (id == "wagon" || id.ends_with("_wagon") || id.ends_with("wagon_house")) return natural_h;

		String base = id;
		int us = id.find("_");
		if (us > 0 && us < id.length() - 1)
			base = id.substr(us + 1);

		// Infantry (0 A.D. template_unit_infantry: ~1.6-1.9 m eye height).
		if (base == "infantry" || id.contains("infantry_")) return 1.8f;
		if (id.contains("champion_infantry")) return 1.9f;
		if (id.contains("hero_") || id.contains("_hero")) return 2.0f;
		if (id.contains("support_female_citizen") || id.contains("female_citizen") || id.contains("female")) return 1.68f;
		if (id.contains("archer_maiden")) return 1.7f;
		if (id.contains("priestess") || id.contains("priest") || id.contains("healer")) return 1.72f;

		// Cavalry and mounted units.
		if (base == "cavalry" || id.contains("cavalry")) return 2.85f;
		if (base == "camel" || id.contains("camel")) return 2.9f;
		if (id.contains("champion_cavalry")) return 2.95f;
		if (id.contains("hero_cav") || id.contains("_cav_")) return 3.0f;
		if (id.contains("worker_elephant") || id.contains("elephant")) return 3.2f;

		// Traders and merchants.
		if (id.contains("trader") || id.contains("merchant")) return 1.8f;

		// Siege engines.
		if (id.contains("scythed_chariot") || id.contains("chariot")) return 2.4f;
		if (base == "ram" || base == "siege_ram" || id.ends_with("_ram")) return 2.0f;
		if (id.contains("siege_tower")) return 3.5f;
		if (id.contains("bolt_shooter") || id.contains("ballista") || id.contains("oxybeles")) return 1.8f;
		if (id.contains("catapult") || id.contains("onager") || id.contains("lithobolos") || id.contains("mangonel")) return 2.2f;

		// Ships.
		if (id.contains("fishing_boat") || id.contains("fishing_ship")) return 2.0f;
		if (id.contains("merchant_ship") || id.contains("trireme") || id.contains("ship_merchant") || id.contains("warship") || id.contains("ship")) return 3.5f;

		// Animals and special actors.
		if (id.contains("war_dog") || id.contains("dog")) return 0.85f;
		if (id.contains("carnyx")) return 1.95f;

		// Projectiles.
		if (id == "bolt_projectile") return 1.15f;
		if (id == "arrow_projectile") return 1.10f;
		if (id == "stone_projectile") return 0.45f;

		// Default human scale.
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
		if (base == "town_center" || base == "civil_centre") return 6.0f;
		else if (base == "fortress") return 7.0f;
		else if (base == "temple") return 6.4f;
		else if (base == "wonder") return 8.5f;
		else if (base == "barracks" || base == "market" || base == "stable") return 4.4f;
		else if (base == "wall_tower" || base == "tower" || base == "defense_tower") return 4.6f;
		else if (base == "gate") return 4.5f;
		else if (base == "wall" || base == "wall_segment") return 3.0f;
		else if (base == "dock" || base == "shipyard" || base == "blacksmith" || base == "siege_workshop") return 4.0f;
		else if (base == "house" || base == "farmstead" || base == "storehouse") return 3.4f;
		else if (base == "outpost") return 3.0f;
		else if (base == "corral") return 2.8f;
		else if (base == "ruins") return 2.4f;
		// tools/extract_0ad_structures.py sized it already (data/building_models.json).
		if (building_glb_id(id) != id) return natural_h;
		return Math::clamp(natural_h, 2.0f, 8.0f);
	}
	// Props (0 A.D. treasures) are sized by the extractor (ZIP_PROPS).
	if (category == "props") return natural_h;
	if (id.contains("pine")) return 8.0f;
	else if (id.contains("palm")) return 7.0f;
	else if (id.contains("oak")) return 9.0f;
	else if (id.contains("fir")) return 10.0f;
	else if (id.contains("tree")) return 6.0f;
	else if (id.contains("stone_mine") || id.contains("mine")) return 1.4f;
	else if (id.contains("gold_vein")) return 1.1f;
	else if (id.contains("fruit_bush") || id.contains("bush") || id.contains("berry")) return 1.15f;
	else if (id.contains("grain") || id.contains("field")) return 0.4f;
	else if (id.contains("deer") || id.contains("gazelle")) return 1.0f;
	else if (id.contains("sheep") || id.contains("pig") || id.contains("cow") || id.contains("boar")) return 1.0f;
	else if (id.contains("chicken") || id.contains("hen") || id.contains("rooster")) return 0.5f;
	else if (id.contains("goat")) return 0.9f;
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
// data/unit_variants.json (tools/extract_unit_variants.py): a unit body's 0 A.D.
// skins (texture-only variant group) and their frequencies, by baked image; a
// head's objectcolor material adds its hair colours (sRGB) and their frequencies.
struct UnitVariants {
	String salt; // shared by the images one draw picks together (a horse's coat and hair)
	PackedStringArray layers;
	std::vector<float> freq;
	std::vector<Color> colors;
	std::vector<float> color_freq;
};
HashMap<String, UnitVariants> &unit_variants() {
	static auto &m = *new HashMap<String, UnitVariants>();
	static bool loaded = false;
	if (!loaded) {
		loaded = true;
		Ref<FileAccess> f = FileAccess::open("res://data/unit_variants.json", FileAccess::READ);
		if (f.is_valid()) {
			Dictionary root = JSON::parse_string(f->get_as_text());
			Array keys = root.keys();
			for (int i = 0; i < keys.size(); ++i) {
				Dictionary e = root[keys[i]];
				Array layers = e.get("layers", Array()), freq = e.get("freq", Array());
				Array colors = e.get("colors", Array()), color_freq = e.get("color_freq", Array());
				UnitVariants uv;
				uv.salt = e.get("salt", String());
				for (int j = 0; j < layers.size() && j < freq.size() && j < 12; ++j) {
					uv.layers.push_back(layers[j]);
					uv.freq.push_back((float)(double)freq[j]);
				}
				for (int j = 0; j < colors.size() && j < color_freq.size() && j < 8; ++j) {
					Array c = colors[j];
					if (c.size() < 3) continue;
					uv.colors.push_back(Color((float)(double)c[0] / 255.0f, (float)(double)c[1] / 255.0f, (float)(double)c[2] / 255.0f));
					uv.color_freq.push_back((float)(double)color_freq[j]);
				}
				if (uv.layers.size() > 1 || (!uv.layers.is_empty() && !uv.colors.empty())) m.insert(keys[i], uv);
			}
		}
	}
	return m;
}
// Where each choice after the first starts in [0, 1), by frequency (2.0 past the last),
// as the shader's variant_cut (vec4s = 3) and object_cut (2) take them.
Array cumulative_cuts(const std::vector<float> &freq, int vec4s) {
	float total = 0.0f;
	for (float f : freq) total += f;
	float cut[12] = { 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f };
	float run = 0.0f;
	for (size_t i = 0; i + 1 < freq.size() && (int)i < 4 * vec4s && total > 0.0f; ++i) {
		run += freq[i];
		cut[i] = run / total;
	}
	Array cuts;
	for (int v = 0; v < vec4s && v < 3; ++v) cuts.push_back(Vector4(cut[v * 4], cut[v * 4 + 1], cut[v * 4 + 2], cut[v * 4 + 3]));
	return cuts;
}
// The skins as one array texture (RGBA8, the body image's size), shared by
// every body that names the same skins.
Ref<Texture2DArray> unit_variant_array(const UnitVariants &uv) {
	static auto &cache = *new HashMap<String, Ref<Texture2DArray>>();
	const String key = String("|").join(uv.layers);
	if (Ref<Texture2DArray> *hit = cache.getptr(key)) return *hit;
	Ref<Texture2DArray> arr;
	TypedArray<Image> images;
	for (int i = 0; i < uv.layers.size(); ++i) {
		if (!ResourceLoader::get_singleton()->exists(uv.layers[i])) break;
		Ref<Texture2D> t = ResourceLoader::get_singleton()->load(uv.layers[i]);
		Ref<Image> img = t.is_valid() ? t->get_image() : Ref<Image>();
		if (img.is_null()) break;
		if (img->is_compressed()) img->decompress();
		img->convert(Image::FORMAT_RGBA8);
		if (!images.is_empty()) {
			Ref<Image> first = images[0];
			if (img->get_size() != first->get_size()) img->resize(first->get_width(), first->get_height());
		}
		images.push_back(img);
	}
	if (images.size() == uv.layers.size()) {
		arr.instantiate();
		if (arr->create_from_images(images) != OK) arr.unref();
	}
	cache.insert(key, arr);
	return arr;
}
Ref<Shader> &player_color_shader() {
	static Ref<Shader> &sh = *new Ref<Shader>();
	return sh;
}
// The same shader with CUTOUT defined: an alpha-tested skin that varies by unit
// (a horse's mane and tail) keeps its alpha test.
Ref<Shader> &player_color_cutout_shader() {
	static Ref<Shader> &sh = *new Ref<Shader>();
	if (sh.is_null() && player_color_shader().is_valid()) {
		const String code = player_color_shader()->get_code();
		const int at = code.find("shader_type spatial;");
		if (at >= 0) {
			const int end = at + String("shader_type spatial;").length();
			sh.instantiate();
			sh->set_code(code.substr(0, end) + "\n#define CUTOUT" + code.substr(end));
			const TypedArray<StringName> metas = player_color_shader()->get_meta_list();
			for (int i = 0; i < metas.size(); ++i) sh->set_meta(metas[i], player_color_shader()->get_meta(metas[i]));
		}
	}
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
		if (sh.is_null()) sh = load_instanced_shader("res://shaders/player_color.gdshader");
		// A skin that varies by unit (data/unit_variants.json) needs the shader to pick it.
		const UnitVariants *uv = unit_variants().getptr(image_key(tex));
		const bool cutout = uv && !mask.is_valid() && bm->get_transparency() == BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR;
		Ref<Shader> use = cutout ? player_color_cutout_shader() : sh;
		if ((mask.is_valid() || ((spec.is_valid() || height.is_valid() || uv) && opaque) || cutout) && use.is_valid()) {
			out.instantiate();
			out->set_shader(use);
			if (cutout) out->set_shader_parameter("alpha_scissor", bm->get_alpha_scissor_threshold());
			out->set_shader_parameter("albedo_tex", tex);
			if (mask.is_valid()) {
				out->set_shader_parameter("mask_tex", mask);
				out->set_shader_parameter("bake_tint", Vector3(pm->tint.r, pm->tint.g, pm->tint.b));
			}
			out->set_shader_parameter("albedo", bm->get_albedo());
			out->set_shader_parameter("srgb_vertex_color", bm->get_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR));
			out->set_shader_parameter("use_vertex_color", bm->get_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR));
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
			// A unit body, head, helmet or shield whose actor swaps skins by variant:
			// each entity draws one, independently per image (variant_salt).
			if (uv) {
				Ref<Texture2DArray> arr = unit_variant_array(*uv);
				if (arr.is_valid()) {
					out->set_shader_parameter("use_variants", true);
					out->set_shader_parameter("variant_tex", arr);
					out->set_shader_parameter("variant_cut", cumulative_cuts(uv->freq, 3));
					const String salt = uv->salt.is_empty() ? image_key(tex) : uv->salt;
					out->set_shader_parameter("variant_salt", (float)(salt.hash() & 0xFFFFu) / 65536.0f);
					// model_common.fs USE_OBJECTCOLOR: the hair (the layer's alpha) takes a colour
					// the head's actor picks by variant, multiplied in sRGB as 0 A.D. draws it.
					if (!uv->colors.empty()) {
						Array cols;
						for (int i = 0; i < 8; ++i) {
							const Color c = uv->colors[MIN((size_t)i, uv->colors.size() - 1)].srgb_to_linear();
							cols.push_back(Vector4(c.r, c.g, c.b, 1.0f));
						}
						out->set_shader_parameter("use_object_color", true);
						out->set_shader_parameter("object_colors", cols);
						out->set_shader_parameter("object_cut", cumulative_cuts(uv->color_freq, 2));
					}
				}
			}
		}
	}
	player_color_mats().insert(id, out);
	return out;
}
// The texture files the material fixups load for a GLB's images (player mask,
// normal, specular and height maps, skin variants), by GLB ("units/spearman").
HashMap<String, PackedStringArray> &companion_textures() {
	static auto &m = *new HashMap<String, PackedStringArray>();
	static bool built = false;
	if (built) return m;
	built = true;
	auto add = [](const String &image, const String &path) {
		const int t = image.rfind("_tex");
		if (t <= 0 || path.is_empty()) return;
		const String glb = image.substr(0, t);
		if (!m.has(glb)) m.insert(glb, PackedStringArray());
		m[glb].push_back(path);
	};
	for (const KeyValue<String, PlayerMask> &kv : player_masks()) add(kv.key, kv.value.mask);
	for (const KeyValue<String, String> &kv : normal_maps()) add(kv.key, kv.value);
	for (const KeyValue<String, String> &kv : specular_maps()) add(kv.key, kv.value);
	for (const KeyValue<String, ParallaxMap> &kv : parallax_maps()) add(kv.key, kv.value.height);
	for (const KeyValue<String, UnitVariants> &kv : unit_variants())
		for (const String &layer : kv.value.layers) add(kv.key, layer);
	return m;
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
	return sm.is_valid() && sm->get_shader().is_valid() &&
			(sm->get_shader() == player_color_shader() || sm->get_shader() == player_color_cutout_shader());
}

void ModelLibrary::set_player_color(Node *n, const Color &c) {
	if (!n) return;
	if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(n)) {
		const int count = mi->get_mesh().is_valid() ? mi->get_mesh()->get_surface_count() : 0;
		for (int s = 0; s < count; ++s) {
			if (is_player_color_material(mi->get_active_material(s))) {
				set_instance_param(mi, "player_color", c);
				break;
			}
		}
	}
	for (int i = 0; i < n->get_child_count(); ++i) set_player_color(n->get_child(i), c);
}

void ModelLibrary::set_variant(Node *n, float u) {
	if (!n) return;
	if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(n)) {
		const int count = mi->get_mesh().is_valid() ? mi->get_mesh()->get_surface_count() : 0;
		for (int s = 0; s < count; ++s) {
			if (is_player_color_material(mi->get_active_material(s))) {
				set_instance_param(mi, "variant_u", u);
				break;
			}
		}
	}
	for (int i = 0; i < n->get_child_count(); ++i) set_variant(n->get_child(i), u);
}

// 0 A.D. seeds each unit's variant choice at random; RTS0 draws it from the id
// so a unit keeps its skin and every client draws the same.
float ModelLibrary::variant_draw(uint32_t entity_id) {
	uint32_t h = entity_id * 2654435761u;
	h ^= h >> 15;
	h *= 2246822519u;
	h ^= h >> 13;
	return (float)(h >> 8) / 16777216.0f;
}

// data/unit_prop_looks.json (tools/extract_unit_prop_looks.py): per unit GLB, the
// baked prop parts whose 0 A.D. actor swaps meshes by variant, with the others'
// GLBs (in .pmd space), the transform onto the part and the frequencies.
struct PropLookSlot {
	StringName node;
	int joint = 0;
	Transform3D xf;
	PackedStringArray meshes; // [0] "": the baked part
	std::vector<float> cut;   // where each mesh after the first starts in [0, 1)
	uint32_t salt = 0;
	std::vector<Ref<ArrayMesh>> built;
};
static HashMap<String, std::vector<PropLookSlot>> &prop_looks() {
	static auto &m = *new HashMap<String, std::vector<PropLookSlot>>();
	static bool loaded = false;
	if (loaded) return m;
	loaded = true;
	Ref<FileAccess> f = FileAccess::open("res://data/unit_prop_looks.json", FileAccess::READ);
	if (f.is_null()) return m;
	Dictionary root = JSON::parse_string(f->get_as_text());
	Array keys = root.keys();
	for (int i = 0; i < keys.size(); ++i) {
		Array slots = root[keys[i]];
		std::vector<PropLookSlot> out;
		for (int j = 0; j < slots.size(); ++j) {
			Dictionary e = slots[j];
			Array xf = e.get("xf", Array()), meshes = e.get("meshes", Array()), freq = e.get("freq", Array());
			if (xf.size() != 12 || meshes.size() < 2 || freq.size() != meshes.size()) continue;
			PropLookSlot s;
			s.node = String(e.get("node", ""));
			s.joint = (int)e.get("joint", 0);
			auto v = [&](int k) { return Vector3((float)(double)xf[k], (float)(double)xf[k + 1], (float)(double)xf[k + 2]); };
			s.xf = Transform3D(Basis(v(0), v(3), v(6)), v(9)); // columns
			float total = 0.0f;
			for (int k = 0; k < freq.size(); ++k) total += (float)(double)freq[k];
			float run = 0.0f;
			for (int k = 0; k < meshes.size(); ++k) {
				s.meshes.push_back(meshes[k]);
				if (k > 0) s.cut.push_back(total > 0.0f ? run / total : 2.0f);
				run += (float)(double)freq[k];
			}
			s.salt = (uint32_t)String(s.node).hash();
			s.built.resize(meshes.size());
			out.push_back(std::move(s));
		}
		if (!out.empty()) m.insert(keys[i], std::move(out));
	}
	return m;
}

// The look's mesh on the part's joint: .pmd space moved onto the part, every
// vertex weighted fully to the joint the baked part rides.
static Ref<ArrayMesh> build_prop_look(PropLookSlot &s, int k) {
	if (s.built[k].is_valid()) return s.built[k];
	Ref<PackedScene> scene = ResourceLoader::get_singleton()->exists(s.meshes[k])
			? Ref<PackedScene>(ResourceLoader::get_singleton()->load(s.meshes[k]))
			: Ref<PackedScene>();
	Node *inst = scene.is_valid() ? scene->instantiate() : nullptr;
	MeshInstance3D *src = nullptr;
	if (inst) {
		src = Object::cast_to<MeshInstance3D>(inst);
		if (!src) {
			TypedArray<Node> found = inst->find_children("*", "MeshInstance3D", true, false);
			if (!found.is_empty()) src = Object::cast_to<MeshInstance3D>(found[0]);
		}
	}
	Ref<Mesh> mesh = src ? src->get_mesh() : Ref<Mesh>();
	if (mesh.is_valid() && mesh->get_surface_count() > 0) {
		Array arrays = mesh->surface_get_arrays(0);
		PackedVector3Array pos = arrays[Mesh::ARRAY_VERTEX];
		PackedVector3Array nrm = arrays[Mesh::ARRAY_NORMAL];
		const Basis nb = s.xf.basis.inverse().transposed();
		for (int v = 0; v < pos.size(); ++v) pos.set(v, s.xf.xform(pos[v]));
		for (int v = 0; v < nrm.size(); ++v) nrm.set(v, nb.xform(nrm[v]).normalized());
		PackedInt32Array bones;
		PackedFloat32Array weights;
		bones.resize(pos.size() * 4);
		weights.resize(pos.size() * 4);
		for (int v = 0; v < pos.size(); ++v) {
			bones.set(v * 4, s.joint);
			weights.set(v * 4, 1.0f);
			for (int c = 1; c < 4; ++c) {
				bones.set(v * 4 + c, 0);
				weights.set(v * 4 + c, 0.0f);
			}
		}
		arrays[Mesh::ARRAY_VERTEX] = pos;
		arrays[Mesh::ARRAY_NORMAL] = nrm;
		// The importer's tangents (the part's normal map needs them), turned with the mesh.
		if (arrays[Mesh::ARRAY_TANGENT].get_type() == Variant::PACKED_FLOAT32_ARRAY) {
			PackedFloat32Array tan = arrays[Mesh::ARRAY_TANGENT];
			for (int v = 0; v * 4 + 3 < tan.size(); ++v) {
				const Vector3 t = s.xf.basis.xform(Vector3(tan[v * 4], tan[v * 4 + 1], tan[v * 4 + 2])).normalized();
				tan.set(v * 4, t.x);
				tan.set(v * 4 + 1, t.y);
				tan.set(v * 4 + 2, t.z);
			}
			arrays[Mesh::ARRAY_TANGENT] = tan;
		}
		arrays[Mesh::ARRAY_BONES] = bones;
		arrays[Mesh::ARRAY_WEIGHTS] = weights;
		Ref<ArrayMesh> am;
		am.instantiate();
		am->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
		s.built[k] = am;
	}
	if (inst) memdelete(inst);
	return s.built[k];
}

void ModelLibrary::set_prop_looks(Node *root, const String &glb, uint32_t entity_id) {
	if (!root) return;
	std::vector<PropLookSlot> *slots = prop_looks().getptr(glb);
	if (!slots) return;
	for (PropLookSlot &s : *slots) {
		MeshInstance3D *part = Object::cast_to<MeshInstance3D>(root->find_child(String(s.node), true, false));
		if (!part || !part->get_parent()) continue;
		// Drawn per prop, as 0 A.D. picks each prop actor's groups by themselves.
		const float u = variant_draw(entity_id ^ s.salt);
		int pick = 0;
		for (float c : s.cut) pick += u >= c ? 1 : 0;
		part->set_visible(pick == 0);
		Node *parent = part->get_parent();
		for (int k = 1; k < s.meshes.size(); ++k) {
			const String name = String(s.node) + "_look" + String::num_int64(k);
			MeshInstance3D *look = Object::cast_to<MeshInstance3D>(parent->get_node_or_null(NodePath(name)));
			if (!look && k == pick) {
				Ref<ArrayMesh> mesh = build_prop_look(s, k);
				if (mesh.is_null()) {
					part->set_visible(true);
					continue;
				}
				look = memnew(MeshInstance3D);
				look->set_name(name);
				look->set_mesh(mesh);
				look->set_transform(part->get_transform());
				look->set_skin(part->get_skin());
				look->set_skeleton_path(part->get_skeleton_path());
				look->set_cast_shadows_setting(part->get_cast_shadows_setting());
				look->set_layer_mask(part->get_layer_mask());
				look->set_surface_override_material(0, part->get_active_material(0));
				look->set_material_overlay(part->get_material_overlay());
				for (const char *param : { "player_color", "variant_u" }) {
					const Variant val = get_instance_param(part, param);
					if (val.get_type() != Variant::NIL) set_instance_param(look, param, val);
				}
				parent->add_child(look);
			}
			if (look) look->set_visible(k == pick);
		}
	}
}

// data/unit_helmets.json (tools/extract_unit_helmets.py): per unit GLB whose
// 0 A.D. actor picks the helmet actor by variant, the baked helmet's parts (the
// helmet and its crest and mask props), the transform from helmet space onto them
// and the other helmets' GLBs ("none": no helmet) with their frequencies.
struct UnitHelmet {
	std::vector<StringName> parts;
	int joint = 0;
	Transform3D xf;
	PackedStringArray helmets; // [0] "": the baked parts
	std::vector<float> cut;
	uint32_t salt = 0;
	std::vector<Ref<ArrayMesh>> built;
	bool requested = false; // prefetch_unit_helmets asked for its GLBs
};
static HashMap<String, UnitHelmet> &unit_helmets() {
	static auto &m = *new HashMap<String, UnitHelmet>();
	static bool loaded = false;
	if (loaded) return m;
	loaded = true;
	Ref<FileAccess> f = FileAccess::open("res://data/unit_helmets.json", FileAccess::READ);
	if (f.is_null()) return m;
	Dictionary root = JSON::parse_string(f->get_as_text());
	Array keys = root.keys();
	for (int i = 0; i < keys.size(); ++i) {
		Dictionary e = root[keys[i]];
		Array parts = e.get("parts", Array()), xf = e.get("xf", Array());
		Array helmets = e.get("helmets", Array()), freq = e.get("freq", Array());
		if (parts.is_empty() || xf.size() != 12 || helmets.size() < 2 || freq.size() != helmets.size()) continue;
		UnitHelmet h;
		for (int k = 0; k < parts.size(); ++k) h.parts.push_back(StringName(String(parts[k])));
		h.joint = (int)e.get("joint", 0);
		auto v = [&](int k) { return Vector3((float)(double)xf[k], (float)(double)xf[k + 1], (float)(double)xf[k + 2]); };
		h.xf = Transform3D(Basis(v(0), v(3), v(6)), v(9)); // columns
		float total = 0.0f;
		for (int k = 0; k < freq.size(); ++k) total += (float)(double)freq[k];
		float run = 0.0f;
		for (int k = 0; k < helmets.size(); ++k) {
			h.helmets.push_back(helmets[k]);
			if (k > 0) h.cut.push_back(total > 0.0f ? run / total : 2.0f);
			run += (float)(double)freq[k];
		}
		h.salt = (uint32_t)String(e.get("salt", keys[i])).hash();
		h.built.resize(helmets.size());
		m.insert(keys[i], std::move(h));
	}
	return m;
}

static void collect_mesh_xforms(Node *n, const Transform3D &acc, Vector<MeshXform> &out);
static void take_companions(const String &path, std::vector<Ref<Resource>> &keep, HashMap<String, bool> &pending);

// The helmet GLB's surfaces with their own materials, moved onto the baked
// helmet and weighted fully to its joint.
static Ref<ArrayMesh> build_unit_helmet(UnitHelmet &h, int k) {
	if (h.built[k].is_valid() || h.helmets[k] == "none") return h.built[k];
	// prefetch() asks for a unit's helmet GLBs with the unit's own: take them from the worker.
	std::vector<Ref<Resource>> companions; // held through the fixups below
	take_companions(h.helmets[k], companions, ModelLibrary::threaded_paths());
	Ref<PackedScene> scene;
	if (ModelLibrary::threaded_paths().has(h.helmets[k])) {
		ModelLibrary::threaded_paths().erase(h.helmets[k]);
		scene = ResourceLoader::get_singleton()->load_threaded_get(h.helmets[k]);
	}
	if (scene.is_null() && ResourceLoader::get_singleton()->exists(h.helmets[k]))
		scene = ResourceLoader::get_singleton()->load(h.helmets[k]);
	Node *inst = scene.is_valid() ? scene->instantiate() : nullptr;
	if (!inst) return h.built[k];
	ModelLibrary::apply_imported_material_fixups(inst);
	Vector<MeshXform> meshes;
	collect_mesh_xforms(inst, Transform3D(), meshes);
	Ref<ArrayMesh> am;
	am.instantiate();
	for (const MeshXform &mx : meshes) {
		Ref<Mesh> mesh = mx.mi->get_mesh();
		const Transform3D xf = h.xf * mx.xf;
		const Basis nb = xf.basis.inverse().transposed();
		for (int s = 0; s < mesh->get_surface_count(); ++s) {
			Array arrays = mesh->surface_get_arrays(s);
			PackedVector3Array pos = arrays[Mesh::ARRAY_VERTEX];
			PackedVector3Array nrm = arrays[Mesh::ARRAY_NORMAL];
			for (int v = 0; v < pos.size(); ++v) pos.set(v, xf.xform(pos[v]));
			for (int v = 0; v < nrm.size(); ++v) nrm.set(v, nb.xform(nrm[v]).normalized());
			if (arrays[Mesh::ARRAY_TANGENT].get_type() == Variant::PACKED_FLOAT32_ARRAY) {
				PackedFloat32Array tan = arrays[Mesh::ARRAY_TANGENT];
				for (int v = 0; v * 4 + 3 < tan.size(); ++v) {
					const Vector3 t = xf.basis.xform(Vector3(tan[v * 4], tan[v * 4 + 1], tan[v * 4 + 2])).normalized();
					tan.set(v * 4, t.x);
					tan.set(v * 4 + 1, t.y);
					tan.set(v * 4 + 2, t.z);
				}
				arrays[Mesh::ARRAY_TANGENT] = tan;
			}
			PackedInt32Array bones;
			PackedFloat32Array weights;
			bones.resize(pos.size() * 4);
			weights.resize(pos.size() * 4);
			for (int v = 0; v < pos.size(); ++v) {
				bones.set(v * 4, h.joint);
				weights.set(v * 4, 1.0f);
				for (int c = 1; c < 4; ++c) {
					bones.set(v * 4 + c, 0);
					weights.set(v * 4 + c, 0.0f);
				}
			}
			arrays[Mesh::ARRAY_VERTEX] = pos;
			arrays[Mesh::ARRAY_NORMAL] = nrm;
			arrays[Mesh::ARRAY_BONES] = bones;
			arrays[Mesh::ARRAY_WEIGHTS] = weights;
			am->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
			am->surface_set_material(am->get_surface_count() - 1, mx.mi->get_active_material(s));
		}
	}
	memdelete(inst);
	if (am->get_surface_count() > 0) h.built[k] = am;
	return h.built[k];
}

void ModelLibrary::set_unit_helmet(Node *root, const String &glb, uint32_t entity_id) {
	if (!root) return;
	UnitHelmet *h = unit_helmets().getptr(glb);
	if (!h) return;
	std::vector<MeshInstance3D *> parts;
	for (const StringName &nm : h->parts) {
		MeshInstance3D *part = Object::cast_to<MeshInstance3D>(root->find_child(String(nm), true, false));
		if (!part || !part->get_parent()) return;
		parts.push_back(part);
	}
	const float u = variant_draw(entity_id ^ h->salt);
	int pick = 0;
	for (float c : h->cut) pick += u >= c ? 1 : 0;
	Ref<ArrayMesh> mesh = pick > 0 ? build_unit_helmet(*h, pick) : Ref<ArrayMesh>();
	if (pick > 0 && mesh.is_null() && h->helmets[pick] != "none") pick = 0; // missing GLB: keep the baked one
	MeshInstance3D *head = parts[0];
	Node *parent = head->get_parent();
	// set_prop_looks may have drawn a look for the crest or the helmet itself
	// (a "<part>_lookN" sibling); it stays unless another helmet is drawn.
	for (MeshInstance3D *part : parts) {
		const String look = String(part->get_name()) + "_look";
		bool looked = false;
		for (int i = 0; i < parent->get_child_count(); ++i) {
			Node3D *c = Object::cast_to<Node3D>(parent->get_child(i));
			if (!c || !String(c->get_name()).begins_with(look)) continue;
			if (pick > 0) c->set_visible(false);
			looked = looked || c->is_visible();
		}
		part->set_visible(pick == 0 && !looked);
	}
	for (int k = 1; k < h->helmets.size(); ++k) {
		const String name = String(head->get_name()) + "_helmet" + String::num_int64(k);
		MeshInstance3D *node = Object::cast_to<MeshInstance3D>(parent->get_node_or_null(NodePath(name)));
		if (!node && k == pick && mesh.is_valid()) {
			node = memnew(MeshInstance3D);
			node->set_name(name);
			node->set_mesh(mesh);
			node->set_transform(head->get_transform());
			node->set_skin(head->get_skin());
			node->set_skeleton_path(head->get_skeleton_path());
			node->set_cast_shadows_setting(head->get_cast_shadows_setting());
			node->set_layer_mask(head->get_layer_mask());
			node->set_material_overlay(head->get_material_overlay());
			for (const char *param : { "player_color", "variant_u" }) {
				const Variant val = get_instance_param(head, param);
				if (val.get_type() != Variant::NIL) set_instance_param(node, param, val);
			}
			parent->add_child(node);
		}
		if (node) node->set_visible(k == pick);
	}
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
	bool any = false;
	for (Node3D *n3 : tools) any = any || tool_tag_of(n3->get_name()) == tag;
	if (!any) tag = "Weapon";
	// A tool is every node of its tag: tool_Weapon, tool_Weapon_2, ... are the
	// parts of one prop (spear shaft and blade) or props on both hands. The
	// extractor also writes some tools twice; draw one copy of identical parts.
	std::vector<MeshInstance3D *> drawn;
	for (Node3D *n3 : tools) {
		if (tool_tag_of(n3->get_name()) != tag) continue;
		if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(n3)) {
			bool dup = false;
			for (MeshInstance3D *d : drawn)
				dup = dup || (d->get_parent() == mi->get_parent() &&
									 d->get_transform().is_equal_approx(mi->get_transform()) &&
									 d->get_aabb().is_equal_approx(mi->get_aabb()));
			if (dup) continue;
			drawn.push_back(mi);
		}
		set_tool_drawn(n3, true);
	}
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
	Ref<ShaderMaterial> sm = mat;
	if (sm.is_valid()) {
		Ref<Texture2D> tex = sm->get_shader_parameter(StringName("albedo_tex"));
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
	PackedVector2Array uv2s; // crowd part layers (CrowdParts), else empty
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

// push_tangent() into out[0..3] (src: src_n floats), for arrays sized up front: a
// Packed*Array push_back is a call into the engine per element.
static void put_tangent(float *out, const Basis &b, const float *src, int64_t src_n, int v) {
	Vector3 t(1, 0, 0);
	float w = 1.0f;
	if (v * 4 + 3 < src_n) {
		t = b.xform(Vector3(src[v * 4], src[v * 4 + 1], src[v * 4 + 2])).normalized();
		w = src[v * 4 + 3];
	}
	out[0] = t.x;
	out[1] = t.y;
	out[2] = t.z;
	out[3] = w;
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
	if (b.uv2s.size() == b.verts.size()) out_arr[Mesh::ARRAY_TEX_UV2] = b.uv2s;
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

// exact_material buckets by the material itself rather than its albedo texture,
// so a structure drawn from one merged mesh keeps every part's own material.
static void collect_buckets(const Vector<MeshXform> &instances, bool skip_tiny,
		std::vector<SurfBucket> &out, bool exact_material = false) {
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
			uint64_t key = exact_material ? (mat.is_valid() ? (uint64_t)mat->get_rid().get_id() : 0) : material_merge_key(mat);
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

// A crowd draws each unit type as one surface (one instanced draw per batch), so
// its parts' skins go into one array texture: per part its layers (the skin, or
// the skins it varies by), the player-colour or hair mask in alpha, and UV2 =
// (first layer, part) on its vertices. player_color.gdshader picks the layer as a
// close-up part's material does, by the same frequencies and salt.
struct CrowdParts {
	Ref<ShaderMaterial> mat;
	std::unordered_map<uint64_t, int> part_of; // material_merge_key -> part
	std::vector<int> first_layer;
};
static constexpr int CROWD_MAX_PARTS = 8;
static constexpr int CROWD_MAX_LAYERS = 24;

static const uint8_t *srgb_to_linear_lut() {
	static uint8_t lut[256];
	static bool made = false;
	if (!made) {
		for (int i = 0; i < 256; ++i) lut[i] = (uint8_t)Math::round(Color(i / 255.0f, 0, 0).srgb_to_linear().r * 255.0f);
		made = true;
	}
	return lut;
}

// The image as RGBA8 at size x size.
static Ref<Image> crowd_layer_image(Ref<Image> img, int size) {
	if (img.is_null()) return img;
	img = img->duplicate();
	if (img->is_compressed()) img->decompress();
	if (img->has_mipmaps()) img->clear_mipmaps();
	img->convert(Image::FORMAT_RGBA8);
	if (img->get_width() != size || img->get_height() != size) img->resize(size, size, Image::INTERPOLATE_BILINEAR);
	return img;
}

// A skin with its player mask: RGB with the baked tint divided back out (as the
// shader's `baked`), the mask in alpha.
static Ref<Image> crowd_masked_layer(const Ref<Texture2D> &albedo, const Ref<Texture2D> &mask, const Vector3 &tint, int size) {
	Ref<Image> out = crowd_layer_image(albedo.is_valid() ? albedo->get_image() : Ref<Image>(), size);
	if (out.is_null()) return out;
	Ref<Image> m = crowd_layer_image(mask.is_valid() ? mask->get_image() : Ref<Image>(), size);
	PackedByteArray px = out->get_data();
	PackedByteArray mp = m.is_valid() ? m->get_data() : PackedByteArray();
	uint8_t *d = px.ptrw();
	const uint8_t *md = mp.ptr();
	const float t[3] = { tint.x, tint.y, tint.z };
	const uint8_t *lin = srgb_to_linear_lut();
	for (int64_t i = 0; i + 3 < px.size(); i += 4) {
		const float a = md && i < mp.size() ? md[i] / 255.0f : 0.0f;
		if (a > 0.0f) {
			for (int c = 0; c < 3; ++c) {
				const float baked = 1.0f + (t[c] - 1.0f) * a;
				const float v = lin[d[i + c]] / 255.0f / MAX(baked, 0.05f);
				d[i + c] = (uint8_t)CLAMP(Math::round(Color(v, 0, 0).linear_to_srgb().r * 255.0f), 0.0f, 255.0f);
			}
		}
		d[i + 3] = (uint8_t)Math::round(a * 255.0f);
	}
	out->set_data(size, size, false, Image::FORMAT_RGBA8, px);
	return out;
}

static const CrowdParts *crowd_parts(const std::vector<SurfBucket> &buckets) {
	static auto &cache = *new HashMap<String, CrowdParts *>();
	if (buckets.size() < 2 || buckets.size() > (size_t)CROWD_MAX_PARTS) return nullptr;
	std::vector<uint64_t> keys;
	for (const SurfBucket &b : buckets) keys.push_back(material_merge_key(b.mat));
	std::vector<uint64_t> sorted = keys;
	std::sort(sorted.begin(), sorted.end());
	String ck;
	for (uint64_t k : sorted) ck += String::num_uint64(k) + ",";
	if (CrowdParts **hit = cache.getptr(ck)) return *hit;

	struct Part {
		Ref<Texture2D> albedo, mask;
		Vector3 tint = Vector3(1, 1, 1);
		Ref<Texture2DArray> variants;
		Array cut;
		float salt = 0.0f;
		bool obj = false;
		int layers = 1;
	};
	std::vector<Part> parts(buckets.size());
	int total = 0;
	bool owner = false;
	int obj_part = -1;
	Ref<Material> primary;
	for (size_t i = 0; i < buckets.size(); ++i) {
		Part &p = parts[i];
		Ref<ShaderMaterial> sm = buckets[i].mat;
		Ref<BaseMaterial3D> bm = buckets[i].mat;
		if (ModelLibrary::is_player_color_material(sm)) {
			p.albedo = sm->get_shader_parameter("albedo_tex");
			p.mask = sm->get_shader_parameter("mask_tex");
			p.tint = sm->get_shader_parameter("bake_tint");
			if ((bool)sm->get_shader_parameter("use_variants")) {
				p.variants = sm->get_shader_parameter("variant_tex");
				p.cut = sm->get_shader_parameter("variant_cut");
				p.salt = sm->get_shader_parameter("variant_salt");
				p.layers = p.variants.is_valid() ? (int)p.variants->get_layers() : 0;
				if ((bool)sm->get_shader_parameter("use_object_color")) {
					if (obj_part >= 0) return nullptr; // one hair tint per crowd material
					p.obj = true;
					obj_part = (int)i;
				}
			}
			if (p.mask.is_valid()) owner = true;
			if (p.variants.is_valid() && !p.obj) owner = true; // variant layers carry the mask
			if (primary.is_null()) primary = sm;
		} else if (bm.is_valid() && bm->get_transparency() == BaseMaterial3D::TRANSPARENCY_DISABLED) {
			p.albedo = bm->get_texture(BaseMaterial3D::TEXTURE_ALBEDO);
		} else {
			return nullptr;
		}
		if (p.layers <= 0 || (p.variants.is_null() && p.albedo.is_null())) return nullptr;
		total += p.layers;
	}
	if (total > CROWD_MAX_LAYERS) return nullptr;
	// 0.5 M texels at most: 256 for up to eight layers, else 128.
	const int size = total <= 8 ? 256 : 128;
	TypedArray<Image> images;
	auto *cp = new CrowdParts();
	Array cuts;
	PackedFloat32Array salts;
	salts.resize(CROWD_MAX_PARTS);
	for (int i = 0; i < CROWD_MAX_PARTS; ++i) salts.set(i, 0.0f);
	for (int i = 0; i < CROWD_MAX_PARTS * 3; ++i) cuts.push_back(Vector4(2, 2, 2, 2));
	for (size_t i = 0; i < parts.size(); ++i) {
		const Part &p = parts[i];
		cp->part_of[keys[i]] = (int)i;
		cp->first_layer.push_back(images.size());
		if (p.variants.is_valid()) {
			for (int l = 0; l < p.layers; ++l) images.push_back(crowd_layer_image(p.variants->get_layer_data(l), size));
			for (int v = 0; v < p.cut.size() && v < 3; ++v) cuts[i * 3 + v] = p.cut[v];
			salts.set(i, p.salt);
		} else {
			images.push_back(crowd_masked_layer(p.albedo, p.mask, p.tint, size));
		}
	}
	for (int i = 0; i < images.size(); ++i) {
		Ref<Image> img = images[i];
		if (img.is_null()) {
			delete cp;
			cache.insert(ck, nullptr);
			return nullptr;
		}
		img->generate_mipmaps();
	}
	Ref<Texture2DArray> arr;
	arr.instantiate();
	if (arr->create_from_images(images) != OK) {
		delete cp;
		cache.insert(ck, nullptr);
		return nullptr;
	}
	Ref<ShaderMaterial> m;
	m.instantiate();
	m->set_shader(player_color_shader());
	m->set_shader_parameter("use_parts", true);
	m->set_shader_parameter("part_tex", arr);
	m->set_shader_parameter("part_cut", cuts);
	m->set_shader_parameter("part_salt", salts);
	if (obj_part >= 0) {
		Ref<ShaderMaterial> om = buckets[obj_part].mat;
		m->set_shader_parameter("use_object_color", true);
		m->set_shader_parameter("obj_part", obj_part);
		m->set_shader_parameter("object_colors", om->get_shader_parameter("object_colors"));
		m->set_shader_parameter("object_cut", om->get_shader_parameter("object_cut"));
	}
	Ref<ShaderMaterial> pm = primary;
	for (const char *param : { "roughness", "metallic", "specular", "srgb_vertex_color", "use_vertex_color" }) {
		if (pm.is_valid()) m->set_shader_parameter(param, pm->get_shader_parameter(param));
	}
	// world_view_node.cpp crowd_batch: no friend/foe tint over owner-coloured parts.
	m->set_meta("crowd_owner_coloured", owner);
	cp->mat = m;
	cache.insert(ck, cp);
	return cp;
}

static Ref<ArrayMesh> flatten_to_one_surface(std::vector<SurfBucket> &buckets) {
	if (buckets.empty()) return Ref<ArrayMesh>();
	const CrowdParts *parts = crowd_parts(buckets);
	SurfBucket all;
	// Without CrowdParts: the primary body material (player_color shader, else the
	// largest surface) for every part.
	int best_bi = 0;
	for (size_t bi = 0; bi < buckets.size(); ++bi) {
		if (ModelLibrary::is_player_color_material(buckets[bi].mat)) {
			best_bi = (int)bi;
			break;
		}
		if (buckets[bi].verts.size() > buckets[best_bi].verts.size()) {
			best_bi = (int)bi;
		}
	}
	all.mat = parts ? Ref<Material>(parts->mat) : buckets[best_bi].mat;
	int64_t nv = 0, ni = 0;
	for (const SurfBucket &b : buckets) {
		nv += b.verts.size();
		ni += b.indices.size();
	}
	all.verts.resize(nv);
	all.norms.resize(nv);
	all.uvs.resize(nv);
	all.colors.resize(nv);
	all.indices.resize(ni);
	if (parts) all.uv2s.resize(nv);
	const bool tan = !parts && tangents_wanted(all.mat);
	if (tan) all.tangents.resize(nv * 4);
	float *ot = tan ? all.tangents.ptrw() : nullptr;
	Vector3 *ov = all.verts.ptrw();
	Vector3 *on = all.norms.ptrw();
	Vector2 *ouv = all.uvs.ptrw();
	Vector2 *ou2 = parts ? all.uv2s.ptrw() : nullptr;
	Color *oc = all.colors.ptrw();
	int32_t *oi = all.indices.ptrw();
	int64_t base = 0, k_out = 0;
	for (const SurfBucket &b : buckets) {
		const int64_t n = b.verts.size(), nn = b.norms.size(), nu = b.uvs.size(), nc = b.colors.size();
		const Vector3 *bv = b.verts.ptr();
		const Vector3 *bn = b.norms.ptr();
		const Vector2 *bu = b.uvs.ptr();
		const Color *bc = b.colors.ptr();
		Vector2 part;
		if (parts) {
			auto it = parts->part_of.find(material_merge_key(b.mat));
			const int pi = it != parts->part_of.end() ? it->second : 0;
			part = Vector2((float)parts->first_layer[pi], (float)pi);
		}
		for (int64_t i = 0; i < n; ++i) {
			ov[base + i] = bv[i];
			on[base + i] = i < nn ? bn[i] : Vector3(0, 1, 0);
			ouv[base + i] = i < nu ? bu[i] : Vector2();
			oc[base + i] = i < nc ? bc[i] : Color(1, 1, 1, 1);
			if (ou2) ou2[base + i] = part;
			if (tan) put_tangent(ot + (base + i) * 4, Basis(), b.tangents.ptr(), b.tangents.size(), (int)i);
		}
		const int32_t *bi = b.indices.ptr();
		for (int64_t k = 0; k < b.indices.size(); ++k) oi[k_out++] = bi[k] + (int32_t)base;
		base += n;
	}
	std::vector<SurfBucket> one;
	one.push_back(std::move(all));
	return mesh_from_buckets(one, 1);
}

// A crowd unit: its own parts as one surface (flatten_to_one_surface), then the
// alpha-tested ones (manes, tails) and carried props, which keep their materials.
static Ref<ArrayMesh> crowd_mesh_from(std::vector<SurfBucket> &buckets) {
	std::vector<SurfBucket> body, apart;
	for (SurfBucket &b : buckets) {
		Ref<BaseMaterial3D> bm = b.mat;
		Ref<ShaderMaterial> sm = b.mat;
		const bool cutout = (bm.is_valid() && bm->get_transparency() != BaseMaterial3D::TRANSPARENCY_DISABLED) ||
				(sm.is_valid() && sm->get_shader().is_valid() && sm->get_shader() == player_color_cutout_shader());
		(b.own_surface || cutout ? apart : body).push_back(std::move(b));
	}
	if (body.empty()) return mesh_from_buckets(apart, 0);
	Ref<ArrayMesh> out = flatten_to_one_surface(body);
	if (out.is_valid()) {
		for (const SurfBucket &b : apart) append_bucket_surface(out, b);
	}
	return out;
}

static Ref<ArrayMesh> merge_mesh_instances(const Vector<MeshXform> &instances, bool exact_material = false) {
	std::vector<SurfBucket> buckets;
	collect_buckets(instances, false, buckets, exact_material);
	return mesh_from_buckets(buckets, 0);
}

static Ref<ArrayMesh> merge_crowd_mesh(const Vector<MeshXform> &instances) {
	std::vector<SurfBucket> buckets;
	collect_buckets(instances, false, buckets);
	return crowd_mesh_from(buckets);
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
		// By the bucket's material: surfaces sharing a merge key but not normal mapping would
		// otherwise leave the bucket's tangent array short, shifting later surfaces' tangents.
		const bool want_t = tangents_wanted(b.mat);
		const int n = (int)surf.rest_v.size();
		const int base = b.verts.size();
		const bool skinned = surf.rig_i >= 0 && surf.rig_i < (int)palettes.size() && !palettes[surf.rig_i].empty();
		const Transform3D xf = skinned ? skin.rigs[surf.rig_i].xf : surf.rigid_xf;
		const std::vector<Transform3D> *pal = skinned ? &palettes[surf.rig_i] : nullptr;
		b.verts.resize(base + n);
		b.norms.resize(base + n);
		b.uvs.resize(base + n);
		b.colors.resize(base + n);
		const int64_t tbase = b.tangents.size();
		if (want_t) b.tangents.resize(tbase + 4 * (int64_t)n);
		Vector3 *ov = b.verts.ptrw() + base;
		Vector3 *on = b.norms.ptrw() + base;
		Vector2 *ouv = b.uvs.ptrw() + base;
		Color *oc = b.colors.ptrw() + base;
		float *ot = want_t ? b.tangents.ptrw() + tbase : nullptr;
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
			ov[v] = xf.xform(p);
			on[v] = xf.basis.xform(nrm).normalized();
			ouv[v] = v < (int)surf.uv.size() ? surf.uv[v] : Vector2();
			oc[v] = v < (int)surf.col.size() ? surf.col[v] : Color(1, 1, 1, 1);
			if (want_t) {
				const Vector3 t = xf.basis.xform(tg).normalized();
				ot[v * 4] = t.x;
				ot[v * 4 + 1] = t.y;
				ot[v * 4 + 2] = t.z;
				ot[v * 4 + 3] = has_t ? surf.rest_t[v * 4 + 3] : 1.0f;
			}
		}
		const int64_t ibase = b.indices.size();
		const int64_t nidx = surf.idx.empty() ? n : (int64_t)surf.idx.size();
		b.indices.resize(ibase + nidx);
		int32_t *oi = b.indices.ptrw() + ibase;
		if (!surf.idx.empty()) {
			for (int64_t k = 0; k < nidx; ++k) oi[k] = surf.idx[k] + base;
		} else {
			for (int v = 0; v < n; ++v) oi[v] = base + v;
		}
	}
	if (buckets.empty()) return Ref<ArrayMesh>();
	Ref<ArrayMesh> mesh = reuse;
	if (mesh.is_valid()) mesh->clear_surfaces();
	(void)mesh;
	return crowd_mesh_from(buckets);
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

// The GLBs load() tries for a model id, in order.
Vector<String> ModelLibrary::candidate_paths(const String &category, const String &id) {
	Vector<String> candidates;
	if (category == "units") {
		// Civ class models whose 0.28 actor was extracted under another name (pass-3 audit):
		// the Han woman is units/han/female_citizen.xml (han_villager_female), the Han class
		// cavalry the Academy champion's cavalry_spearman_c_m, the Han fishing boat
		// structures/han/fishing_ship.xml.
		static const char *const kAliases[][2] = {
			{"han_female_citizen", "units/han_villager_female"},
			{"han_cavalry", "units/han_cavalry_spearman"},
			{"han_fishing_boat", "buildings/han_fishing_ship"},
			// The Athenian stone thrower is the Hellenic lithobolos_med 0.28 shares with mace and sele.
			{"athen_catapult", "units/mace_catapult"},
		};
		for (const auto &a : kAliases) {
			if (id == a[0]) candidates.push_back(String("res://assets/models/") + a[1] + String(".glb"));
		}
	}
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
	return candidates;
}

HashMap<String, bool> &ModelLibrary::threaded_paths() {
	static auto &m = *new HashMap<String, bool>();
	return m;
}

void ModelLibrary::request_threaded(const String &path) {
	if (threaded_paths().has(path) || ResourceLoader::get_singleton()->has_cached(path) ||
			!ResourceLoader::get_singleton()->exists(path))
		return;
	if (ResourceLoader::get_singleton()->load_threaded_request(path) == OK) threaded_paths().insert(path, true);
}

// Takes the prefetched companion textures of a GLB (they stay cached while `keep` holds them).
static void take_companions(const String &path, std::vector<Ref<Resource>> &keep, HashMap<String, bool> &pending) {
	const PackedStringArray *tex = companion_textures().getptr(path.trim_prefix("res://assets/models/").get_basename());
	if (!tex) return;
	for (const String &t : *tex) {
		if (!pending.has(t)) continue;
		pending.erase(t);
		keep.push_back(ResourceLoader::get_singleton()->load_threaded_get(t));
	}
}

// ResourceLoader's threaded load reads the GLB and the textures it names on a
// worker; load() then takes it with load_threaded_get instead of reading it on
// the frame that first draws the model.
// The helmets set_unit_helmet may draw on a unit GLB (data/unit_helmets.json) are read the
// first time a unit wearing each comes close: ask for them along with the unit's model.
static void prefetch_unit_helmets(const String &id) {
	UnitHelmet *h = unit_helmets().getptr(id);
	if (!h || h->requested) return;
	h->requested = true;
	for (int k = 1; k < h->helmets.size(); ++k) {
		if (h->helmets[k] == "none" || h->built[k].is_valid()) continue;
		ModelLibrary::request_threaded(h->helmets[k]);
		if (const PackedStringArray *tex = companion_textures().getptr(h->helmets[k].trim_prefix("res://assets/models/").get_basename()))
			for (const String &t : *tex) ModelLibrary::request_threaded(t);
	}
}

void ModelLibrary::prefetch(const String &category, const String &id) {
	if (category == "units") prefetch_unit_helmets(id);
	if (cache().has(category + String("/") + id)) return;
	const Vector<String> candidates = candidate_paths(category, id);
	for (int i = 0; i < candidates.size(); ++i) {
		const String &path = candidates[i];
		if (!ResourceLoader::get_singleton()->exists(path)) continue;
		request_threaded(path);
		// And the skins' companion textures the material fixups would read on the frame.
		const String glb = path.trim_prefix("res://assets/models/").get_basename();
		if (const PackedStringArray *tex = companion_textures().getptr(glb))
			for (const String &t : *tex) request_threaded(t);
		break;
	}
	// A template model borrows its class model's scale and, without clips, a donor's.
	if (category == "units") {
		if (const rtso::UnitTemplateModel *tm = rtso::find_unit_template_model(id.utf8().get_data())) {
			if (tm->class_model) prefetch(category, String(tm->class_model));
			if (tm->clip_donor) prefetch(category, String(tm->clip_donor));
		}
	}
}

ModelLibrary::Entry ModelLibrary::load(const String &category, const String &id) {
	String key = category + String("/") + id;
	Entry e;
	const Vector<String> candidates = candidate_paths(category, id);

	for (int i = 0; i < candidates.size(); ++i) {
		const String &path = candidates[i];
		if (ResourceLoader::get_singleton()->exists(path)) {
			Ref<PackedScene> ps;
			std::vector<Ref<Resource>> companions; // held through the fixups below
			take_companions(path, companions, threaded_paths());
			if (threaded_paths().has(path)) {
				threaded_paths().erase(path);
				ps = ResourceLoader::get_singleton()->load_threaded_get(path);
			}
			if (ps.is_null()) ps = ResourceLoader::get_singleton()->load(path);
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
					e.path = path;
					e.animated = find_animation_player(inst) != nullptr;
					Vector<MeshXform> mesh_instances;
					collect_mesh_xforms(inst, Transform3D(), mesh_instances);
					if (mesh_instances.size() > 0) {
						// A static structure is drawn from this mesh (instantiate_building_id):
						// one draw per material instead of one per GLB part.
						e.mesh = merge_mesh_instances(mesh_instances, category == "buildings" && !e.animated);
						if (category == "units") {
							e.crowd_mesh = merge_crowd_mesh(mesh_instances);
						} else {
							e.crowd_mesh = e.mesh;
						}
						if (e.mesh.is_valid()) {
							e.aabb = e.mesh->get_aabb();
							e.scale = 1.0f;
							float natural_h = e.aabb.size.y;
							const float rs = category == "units" ? glb_root_scale(inst) : 0.0f;
							if (rs >= 0.3f && rs <= 0.5f && is_person_model(id)) natural_h = PERSON_RS_HEIGHT * rs;
							float want_h = target_height(category, id, natural_h);
							if (natural_h > 0.05f && want_h > 0.05f)
								e.scale = want_h / natural_h;
							if (category == "units" && is_fauna_model(id) && rs > 0.01f) e.scale = FAUNA_SCALE / rs;
							// A template model is drawn at its class model's scale, so a
							// hero's crest or a champion's spear doesn't shrink the body.
							if (tm && tm->class_model) {
								const Entry &cls = get(category, String(tm->class_model));
								if (cls.valid && cls.scale > 0.0f) e.scale = cls.scale;
							}
							e.height = natural_h;
							// World units per 0.28 metre: the GLB's units are 0.28's times its root scale.
							e.metre = rs > 0.01f ? e.scale * rs : e.scale;
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
			e.metre = 1.0f;
			e.height = e.aabb.size.y;
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
	if (category == "units") prefetch_unit_helmets(id);
	found = cache().getptr(key);
	static Entry empty;
	return found ? *found : empty;
}

void ModelLibrary::clear() {
	for (const KeyValue<String, bool> &kv : threaded_paths()) ResourceLoader::get_singleton()->load_threaded_get(kv.key);
	threaded_paths().clear();
	cache().clear();
	player_color_mats().clear();
}

} // namespace rtso
