#ifndef RTSO_MODEL_LIBRARY_H
#define RTSO_MODEL_LIBRARY_H

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <cstdint>
#include <vector>

using namespace godot;

namespace rtso {

// Loads real 0 A.D. GLB models extracted from art/meshes (*.dae.cached.pmd)
// and normalizes them to world scale (meters). Falls back to procedural
// primitives when a GLB is missing so the game always renders.
class ModelLibrary {
public:
	struct Entry {
		Ref<Mesh> mesh;
		Ref<Mesh> crowd_mesh; // fewer surfaces/parts for MultiMesh armies
		Ref<PackedScene> scene;
		AABB aabb;
		float scale = 1.0f;
		float y_offset = 0.0f; // add to instance Y so the model base sits on ground
		float metre = 1.0f; // world units per 0.28 metre at the drawn size (StatusBars offsets)
		float height = 0.0f; // the body height it was sized by, in GLB units (a person's excludes a held spear)
		bool valid = false;
		bool animated = false;
		godot::String path; // the GLB it was loaded from (empty for a procedural stand-in)
	};

	struct CrowdSkin {
		bool captured = false;
		godot::Node *root = nullptr;
		struct Rig {
			godot::Skeleton3D *skel = nullptr;
			std::vector<int32_t> bone_index;
			std::vector<godot::Transform3D> bind_pose;
			godot::Transform3D xf;
		};
		struct Surf {
			int rig_i = -1;
			godot::Transform3D rigid_xf;
			std::vector<godot::Vector3> rest_v;
			std::vector<godot::Vector3> rest_n;
			std::vector<float> rest_t; // tangents (4 per vertex) for normal-mapped materials
			std::vector<godot::Vector2> uv;
			std::vector<godot::Color> col;
			std::vector<int32_t> idx;
			std::vector<int32_t> bones;
			std::vector<float> weights;
			int stride = 4;
			bool attached = false; // rigid prop under a BoneAttachment3D
			godot::Ref<godot::Material> mat;
		};
		std::vector<Rig> rigs;
		std::vector<Surf> surfs;
	};

	static const Entry &get(const godot::String &category, const godot::String &id);
	// Read the model's GLB on a worker thread ahead of its first get().
	static void prefetch(const godot::String &category, const godot::String &id);
	static HashMap<godot::String, bool> &threaded_paths(); // prefetch requests not yet taken
	static void request_threaded(const godot::String &path);
	static void clear();
	static bool capture_crowd_skin(godot::Node *root, CrowdSkin &out);
	static godot::Ref<godot::ArrayMesh> bake_crowd_skin(CrowdSkin &skin, const godot::Ref<godot::ArrayMesh> &reuse = godot::Ref<godot::ArrayMesh>());
	// CPU-skin a posed unit scene into a low-surface mesh for MultiMesh crowds.
	static godot::Ref<godot::ArrayMesh> bake_posed_crowd_mesh(godot::Node *root);
	// 0 A.D. stores tiling albedo on UV0 and unique AO on UV1. Godot's glTF
	// importer enables AO but samples it with UV0 unless FLAG_AO_ON_UV2 is set.
	static void apply_imported_material_fixups(godot::Node *root);
	// Owner colour for the 0 A.D. player_trans surfaces under root (the
	// instance uniform of shaders/player_color.gdshader).
	static void set_player_color(godot::Node *root, const godot::Color &c);
	// The entity's draw in [0, 1) that picks its body skin (variant_u of
	// shaders/player_color.gdshader, data/unit_variants.json).
	static void set_variant(godot::Node *root, float u);
	static float variant_draw(uint32_t entity_id);
	// The entity's head, helmet, shield and cape meshes, each drawn by its id from
	// the prop's 0 A.D. variants (data/unit_prop_looks.json); the other meshes are
	// added beside the baked part on first use. glb is the unit GLB's id.
	static void set_prop_looks(godot::Node *root, const godot::String &glb, uint32_t entity_id);
	// The entity's helmet, drawn by its id from the 0 A.D. unit actor's helmet
	// choices (data/unit_helmets.json), after set_prop_looks. Crowds keep the baked one.
	static void set_unit_helmet(godot::Node *root, const godot::String &glb, uint32_t entity_id);
	static bool is_player_color_material(const godot::Ref<godot::Material> &mat);
	// The GLB a building model id draws with (data/building_models.json), or id itself.
	static godot::String building_glb_id(const godot::String &id);
	// Hide every tool_* mesh except this clip's (or tool_Weapon's) pieces.
	static void apply_clip_tools(godot::Node *root, const godot::StringName &clip);

private:
	static Entry load(const godot::String &category, const godot::String &id);
	static godot::Vector<godot::String> candidate_paths(const godot::String &category, const godot::String &id);
	static godot::Ref<godot::PackedScene> borrow_clips(godot::Node *inst, const godot::String &donor);
	static float target_height(const godot::String &category, const godot::String &id, float natural_h);
	static HashMap<godot::String, Entry> &cache();
};

} // namespace rtso

#endif // RTSO_MODEL_LIBRARY_H
