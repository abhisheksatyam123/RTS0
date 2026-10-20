#include "instance_params.h"

#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/templates/hash_map.hpp>

namespace rtso {

namespace {

const StringName &instanced_meta() {
	static const StringName *n = new StringName("rtso_instanced"); // never freed: outlives Godot's shutdown
	return *n;
}
const StringName &owner_meta() {
	static const StringName *n = new StringName("rtso_instance_owner"); // never freed: outlives Godot's shutdown
	return *n;
}
const StringName &base_meta() {
	static const StringName *n = new StringName("rtso_instance_base"); // never freed: outlives Godot's shutdown
	return *n;
}
StringName value_meta(const StringName &name) { return StringName(String("rtso_ip_") + String(name)); }

bool instanced(const Ref<Material> &m) {
	Ref<ShaderMaterial> sm = m;
	return sm.is_valid() && sm->get_shader().is_valid() && sm->get_shader()->has_meta(instanced_meta());
}

// Sets the parameter on the node's own copy of `m`, made on first use (`assign`
// puts it in place); an existing copy that already has the value is left alone.
template <typename Assign>
void set_on_own(GeometryInstance3D *gi, const Ref<Material> &m, bool unchanged, const StringName &name, const Variant &value, Assign assign) {
	Ref<ShaderMaterial> sm = m;
	const int64_t id = (int64_t)gi->get_instance_id();
	if ((int64_t)sm->get_meta(owner_meta(), (int64_t)0) == id) {
		if (!unchanged) sm->set_shader_parameter(name, value);
		return;
	}
	Ref<ShaderMaterial> copy = sm->duplicate();
	copy->set_meta(owner_meta(), id);
	copy->set_meta(base_meta(), sm->get_meta(base_meta(), (int64_t)sm->get_instance_id()));
	copy->set_shader_parameter(name, value);
	assign(copy);
}

} // namespace

bool gl_compatibility() {
	static int compat = -1;
	// No RenderingDevice means OpenGL, or the dummy renderer of a headless run,
	// which compiles no shaders and keeps the Vulkan path.
	if (compat < 0)
		compat = RenderingServer::get_singleton()->get_rendering_device() == nullptr &&
						DisplayServer::get_singleton()->get_name() != "headless"
				? 1
				: 0;
	return compat == 1;
}

Ref<Shader> load_instanced_shader(const String &path) {
	// Never freed: a static Ref<Shader> would be destroyed after Godot shuts down (crash on exit).
	static HashMap<String, Ref<Shader>> &cache = *new HashMap<String, Ref<Shader>>();
	if (const Ref<Shader> *s = cache.getptr(path)) return *s;
	Ref<Shader> sh;
	if (!gl_compatibility()) {
		sh = ResourceLoader::get_singleton()->load(path);
	} else {
		const String code = FileAccess::get_file_as_string(path);
		if (!code.is_empty()) {
			sh.instantiate();
			sh->set_code(code.replace("instance uniform ", "uniform "));
		}
	}
	if (sh.is_valid()) sh->set_meta(instanced_meta(), true);
	cache[path] = sh;
	return sh;
}

void set_instance_param(GeometryInstance3D *gi, const StringName &name, const Variant &value) {
	if (!gi) return;
	if (!gl_compatibility()) {
		gi->set_instance_shader_parameter(name, value);
		return;
	}
	const StringName key = value_meta(name);
	// get_meta with a nil default reports an error when the key is missing.
	const bool unchanged = gi->has_meta(key) && gi->get_meta(key) == value;
	if (!unchanged) gi->set_meta(key, value);
	if (Ref<Material> m = gi->get_material_override(); instanced(m)) {
		set_on_own(gi, m, unchanged, name, value, [gi](const Ref<ShaderMaterial> &c) { gi->set_material_override(c); });
	} else if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(gi)) {
		const int count = mi->get_mesh().is_valid() ? mi->get_mesh()->get_surface_count() : 0;
		for (int s = 0; s < count; ++s) {
			Ref<Material> sm = mi->get_active_material(s);
			if (instanced(sm))
				set_on_own(gi, sm, unchanged, name, value, [mi, s](const Ref<ShaderMaterial> &c) { mi->set_surface_override_material(s, c); });
		}
	}
	if (Ref<Material> m = gi->get_material_overlay(); instanced(m))
		set_on_own(gi, m, unchanged, name, value, [gi](const Ref<ShaderMaterial> &c) { gi->set_material_overlay(c); });
}

bool is_material_or_copy(const Ref<Material> &current, const Ref<Material> &base) {
	if (current == base) return true;
	return current.is_valid() && base.is_valid() &&
			(int64_t)current->get_meta(base_meta(), (int64_t)0) == (int64_t)base->get_instance_id();
}

Variant get_instance_param(GeometryInstance3D *gi, const StringName &name) {
	if (!gi) return Variant();
	if (!gl_compatibility()) return gi->get_instance_shader_parameter(name);
	const StringName key = value_meta(name);
	return gi->has_meta(key) ? gi->get_meta(key) : Variant();
}

} // namespace rtso
