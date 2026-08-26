#ifndef RTSO_MODEL_LIBRARY_H
#define RTSO_MODEL_LIBRARY_H

#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/templates/hash_map.hpp>

using namespace godot;

namespace rtso {

// Loads real 0 A.D. GLB models extracted from art/meshes (*.dae.cached.pmd)
// and normalizes them to world scale (meters). Falls back to procedural
// primitives when a GLB is missing so the game always renders.
class ModelLibrary {
public:
	struct Entry {
		Ref<Mesh> mesh;
		AABB aabb;
		float scale = 1.0f;
		float y_offset = 0.0f; // add to instance Y so the model base sits on ground
		bool valid = false;
	};

	// category: "units" | "buildings" | "vegetation"
	static const Entry &get(const godot::String &category, const godot::String &id);
	static void clear();

private:
	static Entry load(const godot::String &category, const godot::String &id);
	static float target_height(const godot::String &category, const godot::String &id, float natural_h);
	static HashMap<godot::String, Entry> &cache();
};

} // namespace rtso

#endif // RTSO_MODEL_LIBRARY_H
