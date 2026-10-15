#include "assets.h"
#include "procedural.h"
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/templates/hash_map.hpp>

#include <mutex>

using namespace godot;

namespace rtso {

bool Assets::res_exists(const String &path) {
	// The packed res:// files don't change while the game runs, and the world view asks about
	// the same few hundred paths every frame (decals, ambient effects, building variants), each
	// a file system lookup: answers for res:// paths are kept. Others (user://) are asked afresh.
	if (!path.begins_with("res://")) return ResourceLoader::get_singleton()->exists(path);
	static std::mutex mu;
	static HashMap<String, bool> known;
	{
		std::lock_guard<std::mutex> lock(mu);
		if (const bool *b = known.getptr(path)) return *b;
	}
	const bool found = ResourceLoader::get_singleton()->exists(path);
	std::lock_guard<std::mutex> lock(mu);
	known.insert(path, found);
	return found;
}

bool Assets::is_real_model(const String &path) {
	return res_exists(path);
}

Ref<Mesh> Assets::try_load_mesh(const String &path, const Ref<Mesh> &fallback) {
	if (res_exists(path)) {
		Ref<PackedScene> scene = ResourceLoader::get_singleton()->load(path);
		if (scene.is_valid()) {
			Node *inst = scene->instantiate();
			if (inst) {
				MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(inst);
				if (!mi) {
					TypedArray<Node> children = inst->get_children();
					for (int i = 0; i < children.size(); i++) {
						mi = Object::cast_to<MeshInstance3D>(children[i]);
						if (mi) break;
					}
				}
				if (mi && mi->get_mesh().is_valid()) {
					Ref<Mesh> m = mi->get_mesh();
					inst->queue_free();
					return m;
				}
				inst->queue_free();
			}
		}
		Ref<Mesh> mesh = ResourceLoader::get_singleton()->load(path);
		if (mesh.is_valid()) return mesh;
	}

	String file_name = path.get_file().get_basename();
	if (path.contains("units/")) return Procedural::get_unit_mesh(file_name);
	if (path.contains("buildings/")) return Procedural::get_building_mesh(file_name);
	if (path.contains("vegetation/")) return Procedural::get_vegetation_mesh(file_name);
	return fallback;
}

Ref<ShaderMaterial> Assets::load_unit_material() {
	return try_load_mat(UnitMaterial, UnitShader);
}

Ref<ShaderMaterial> Assets::load_terrain_material() {
	Ref<ShaderMaterial> mat = try_load_mat(TerrainMaterial, TerrainShader);
	if (mat.is_valid()) {
		auto bind = [&](const char *param, const char *path) {
			if (!res_exists(path)) return;
			Ref<Texture2D> tex = ResourceLoader::get_singleton()->load(path);
			if (tex.is_valid()) mat->set_shader_parameter(param, tex);
		};
		bind("grass_tex", "res://assets/textures/terrain/grass.png");
		bind("dirt_tex", "res://assets/textures/terrain/dirt.png");
		bind("rock_tex", "res://assets/textures/terrain/rock.png");
	}
	return mat;
}

void Assets::apply_terrain_biome(const Ref<ShaderMaterial> &mat, const String &biome) {
	if (mat.is_null()) return;
	// {biome, grass (lowlands), dirt (slopes), rock (cliffs and peaks), shore,
	// forest floor}: 0 A.D. terrain textures from art/textures/terrains/,
	// picked to match the biome's 0 A.D. random-map palettes (rmbiome/*.json
	// shore and forestFloor1). "mediterranean" keeps the defaults.
	static const char *const kSets[][6] = {
		{"mediterranean", "grass", "dirt", "rock", "medit_sand", "medit_forestfloor_a"},
		{"aegean", "aegean_anatolia_grass_02", "aegean_anatolia_dirt_01", "aegean_anatolia_cliff_02", "aegean_anatolia_sand_01", "aegean_anatolia_forestfloor_01"},
		{"latium", "medit_grass_wild", "medit_dirt_c", "medit_cliff_italia", "medit_sand", "medit_forestfloor_a"},
		{"alpine", "alpine_grass_b", "alpine_dirt", "alpine_cliff_snow", "alpine_shore_rocks_grass_50", "alpine_arctic_forestfloor_01"},
		{"oasis", "desert_sand_smooth", "desert_dirt_rough", "desert_cliff_2", "desert_sand_wet", "desert_forestfloor_palms"},
		{"temperate", "temperate_grass_02", "temperate_grass_dirt_01", "temperate_cliff_01", "temperate_grass_mud_01", "temperate_forestfloor_003"},
		{"steppe", "steppe_grass_b", "steppe_dirt_a", "temp_cliff_b", "steppe_grass_mud_01", "steppe_grass_dirt_01"},
		{"savanna", "savanna_grass_a", "savanna_dirt_a_red", "savanna_cliff_a_red", "savanna_mud_a", "savanna_forestfloor_a"},
		{"polar", "polar_snow_a", "polar_tundra_snow", "polar_cliff_snow", "alpine_shore_rocks_icy", "alpine_arctic_forestfloor_02"},
		{"india", "india_grass_01", "india_dirt_01", "india_cliff_02", "india_sand_01", "india_forestfloor_001"},
		{"desert", "desert_grass_a", "desert_dirt_rocks_1", "desert_cliff_persia_1", "desert_sand_wet", "desert_forestfloor_palms"},
		{"persia", "desert_dirt_persia_1", "desert_dirt_persia_rocky", "desert_cliff_persia_2", "desert_shore_stones", "desert_forestfloor_palms"},
		{"volcanic", "cliff volcanic light", "ocean_rock_a", "cliff volcanic coarse", "tropic_beach_volcanic", "tropic_forestfloor_a"},
		{"tropical", "tropic_grass_b", "tropic_dirt_a", "tropic_cliff_a", "tropic_beach_wet", "tropic_forestfloor_a"},
		{"autumn", "temperate_autumn_grass_02", "temperate_autumn_grass_dirt_01", "temperate_cliff_01", "temperate_autumn_grass_mud_01", "temperate_autumn_forestfloor_003"},
		{"nubia", "nubia_sand_01", "nubia_dirt_01", "nubia_cliff_01", "nubia_sand_02", "nubia_forestfloor_01"},
	};
	const char *const *set = kSets[0];
	for (const auto &s : kSets)
		if (biome == s[0]) { set = s; break; }
	const char *params[5] = {"grass_tex", "dirt_tex", "rock_tex", "shore_tex", "forest_tex"};
	for (int i = 0; i < 5; ++i) {
		String path = String("res://assets/textures/terrain/") + set[i + 1] + ".png";
		if (!res_exists(path)) continue;
		Ref<Texture2D> tex = ResourceLoader::get_singleton()->load(path);
		if (tex.is_valid()) mat->set_shader_parameter(params[i], tex);
	}
}

Ref<ShaderMaterial> Assets::load_foliage_material() {
	return try_load_mat(FoliageMaterial, FoliageShader);
}

Ref<ShaderMaterial> Assets::load_water_material() {
	return try_load_mat("", WaterShader);
}

Ref<ShaderMaterial> Assets::try_load_mat(const String &tres, const String &shader_fallback) {
	if (res_exists(tres)) {
		Ref<ShaderMaterial> m = ResourceLoader::get_singleton()->load(tres);
		if (m.is_valid()) return m;
	}
	if (res_exists(shader_fallback)) {
		Ref<Shader> sh = ResourceLoader::get_singleton()->load(shader_fallback);
		if (sh.is_valid()) {
			Ref<ShaderMaterial> mat;
			mat.instantiate();
			mat->set_shader(sh);
			return mat;
		}
	}
	return Ref<ShaderMaterial>();
}

} // namespace rtso
