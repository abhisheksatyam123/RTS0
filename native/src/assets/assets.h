#ifndef RTSO_ASSETS_H
#define RTSO_ASSETS_H

#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/variant/string.hpp>

using namespace godot;

namespace rtso {

class Assets {
public:
	static constexpr const char *UnitShader = "res://shaders/unit_thronefall.gdshader";
	static constexpr const char *TerrainShader = "res://shaders/terrain.gdshader";
	static constexpr const char *FoliageShader = "res://shaders/foliage_wind.gdshader";

	static constexpr const char *UnitMaterial = "res://assets/materials/thronefall/unit.tres";
	static constexpr const char *TerrainMaterial = "res://assets/materials/thronefall/terrain.tres";
	static constexpr const char *FoliageMaterial = "res://assets/materials/thronefall/foliage.tres";

	static String unit_model(const String &id) { return String("res://assets/models/units/") + id + ".glb"; }
	static String building_model(const String &id) { return String("res://assets/models/buildings/") + id + ".glb"; }
	static String vegetation_model(const String &id) { return String("res://assets/models/vegetation/") + id + ".glb"; }
	static String music_path(const String &id) { return String("res://assets/music/") + id + ".ogg"; }

	static bool res_exists(const String &path);
	static bool is_real_model(const String &path);

	static Ref<Mesh> try_load_mesh(const String &path, const Ref<Mesh> &fallback);
	static Ref<ShaderMaterial> load_unit_material();
	static Ref<ShaderMaterial> load_terrain_material();
	static Ref<ShaderMaterial> load_foliage_material();
	static Ref<ShaderMaterial> try_load_mat(const String &tres, const String &shader_fallback);
};

} // namespace rtso

#endif
