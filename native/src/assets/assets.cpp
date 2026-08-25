#include "assets.h"
#include "procedural.h"

using namespace godot;

namespace rtso {

bool Assets::res_exists(const String &path) {
	return ResourceLoader::get_singleton()->exists(path);
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
	return try_load_mat(TerrainMaterial, TerrainShader);
}

Ref<ShaderMaterial> Assets::load_foliage_material() {
	return try_load_mat(FoliageMaterial, FoliageShader);
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
