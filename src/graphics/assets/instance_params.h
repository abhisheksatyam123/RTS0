#ifndef RTSO_INSTANCE_PARAMS_H
#define RTSO_INSTANCE_PARAMS_H

// Per-node shader parameters that also work under Godot's Compatibility
// renderer (OpenGL: Android devices without Vulkan, the web), which rejects
// `instance uniform`. There the shader is loaded with its instance uniforms as
// plain uniforms, and each node draws a copy of the material of its own.

#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

using namespace godot;

namespace rtso {

// True when the renderer is the Compatibility (OpenGL) one.
bool gl_compatibility();

// Loads a shader that may declare `instance uniform`s; under Compatibility its
// source is loaded with those as plain uniforms. Cached per path.
Ref<Shader> load_instanced_shader(const String &path);

// Sets an instance uniform of `gi`'s materials whose shader came from
// load_instanced_shader: set_instance_shader_parameter, or under Compatibility
// the parameter on the node's own copy of each such material (surface
// overrides, material override and overlay).
void set_instance_param(GeometryInstance3D *gi, const StringName &name, const Variant &value);

// Whether `current` is `base` or a node's copy of it made by set_instance_param:
// a node keeping `base` assigns it only when this is false, so it keeps its copy.
bool is_material_or_copy(const Ref<Material> &current, const Ref<Material> &base);

// The value set_instance_param last gave `gi` (NIL when it gave none).
Variant get_instance_param(GeometryInstance3D *gi, const StringName &name);

} // namespace rtso

#endif
