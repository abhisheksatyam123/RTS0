#ifndef RTSO_PROCEDURAL_H
#define RTSO_PROCEDURAL_H

#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/capsule_mesh.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/plane_mesh.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/classes/ref.hpp>

using namespace godot;

namespace rtso {

class Procedural {
public:
	static Ref<Mesh> get_unit_mesh(const String &id);
	static Ref<Mesh> get_building_mesh(const String &id);
	static Ref<Mesh> get_vegetation_mesh(const String &id);
};

} // namespace rtso

#endif
