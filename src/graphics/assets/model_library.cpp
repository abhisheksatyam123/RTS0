#include "model_library.h"
#include "procedural.h"
#include "../../core/diagnostics/logger.h"
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/animation_player.hpp>
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
	static HashMap<String, Entry> c;
	return c;
}

float ModelLibrary::target_height(const String &category, const String &id, float natural_h) {
	if (category == "units") {
		if (id == "cavalry") return 2.6f;
		if (id == "camel") return 2.7f;
		if (id == "chariot" || id == "scythed_chariot") return 2.4f;
		if (id == "worker_elephant" || id == "elephant") return 3.2f;
		if (id == "ram" || id == "siege_ram") return 2.0f;
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
		if (id == "sheep") return 0.95f;
		if (id == "wolf") return 1.05f;
		return 1.8f;
	}
	if (category == "buildings") {
		if (id == "town_center") return 6.0f;
		else if (id == "fortress") return 7.0f;
		else if (id == "temple") return 6.4f;
		else if (id == "wonder") return 8.5f;
		else if (id == "barracks" || id == "market") return 4.4f;
		else if (id == "wall_tower") return 4.6f;
		else if (id == "wall") return 3.0f;
		else if (id == "gate") return 4.5f;
		else if (id == "dock" || id == "shipyard") return 4.0f;
		else if (id == "corral") return 2.8f;
		else if (id == "house" || id == "farmstead" || id == "storehouse") return 3.4f;
		else if (id == "ruins") return 2.4f;
		return Math::clamp(natural_h, 2.0f, 8.0f);
	}
	if (id == "pine") return 8.0f;
	else if (id == "palm") return 7.0f;
	else if (id == "tree") return 6.0f;
	else if (id == "stone_mine") return 1.4f;
	else if (id == "gold_vein") return 1.1f;
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
		}
	}
	for (int i = 0; i < n->get_child_count(); ++i) {
		apply_imported_material_fixups(n->get_child(i));
	}
}

static void collect_mesh_xforms(Node *n, const Transform3D &acc, Vector<MeshXform> &out) {
	Node3D *n3 = Object::cast_to<Node3D>(n);
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
	PackedVector3Array verts;
	PackedVector3Array norms;
	PackedVector2Array uvs;
	PackedColorArray colors;
	PackedInt32Array indices;
};

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

static Ref<ArrayMesh> mesh_from_buckets(std::vector<SurfBucket> &buckets, int max_surfaces) {
	std::sort(buckets.begin(), buckets.end(), [](const SurfBucket &a, const SurfBucket &b) {
		return a.verts.size() > b.verts.size();
	});
	Ref<ArrayMesh> merged;
	merged.instantiate();
	int n = (int)buckets.size();
	if (max_surfaces > 0 && n > max_surfaces) n = max_surfaces;
	for (int i = 0; i < n; ++i) {
		SurfBucket &b = buckets[i];
		if (b.verts.size() == 0) continue;
		Array out_arr;
		out_arr.resize(Mesh::ARRAY_MAX);
		out_arr[Mesh::ARRAY_VERTEX] = b.verts;
		out_arr[Mesh::ARRAY_NORMAL] = b.norms;
		out_arr[Mesh::ARRAY_TEX_UV] = b.uvs;
		out_arr[Mesh::ARRAY_COLOR] = b.colors;
		out_arr[Mesh::ARRAY_INDEX] = b.indices;
		merged->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, out_arr);
		if (b.mat.is_valid()) {
			merged->surface_set_material(merged->get_surface_count() - 1, b.mat);
		}
	}
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
	for (SurfBucket &b : buckets) {
		const int base = all.verts.size();
		for (int i = 0; i < b.verts.size(); ++i) {
			all.verts.push_back(b.verts[i]);
			all.norms.push_back(i < b.norms.size() ? b.norms[i] : Vector3(0, 1, 0));
			all.uvs.push_back(i < b.uvs.size() ? b.uvs[i] : Vector2());
			all.colors.push_back(i < b.colors.size() ? b.colors[i] : Color(1, 1, 1, 1));
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
		NodePath sp = mi->get_skeleton_path();
		if (!sp.is_empty() && mi->has_node(sp)) {
			rig.skel = Object::cast_to<Skeleton3D>(mi->get_node_or_null(sp));
		}
		if (!rig.skel) rig.skel = find_skeleton(mi->get_parent() ? mi->get_parent() : root);
		rig.xf = rig.skel ? xf_to_ancestor(rig.skel, root) : instances[i].xf;
		if (rig.skel) {
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
			PackedInt32Array bones = bone_indices_from_arrays(arrays, verts.size());
			PackedFloat32Array weights;
			if (arrays.size() > Mesh::ARRAY_WEIGHTS && arrays[Mesh::ARRAY_WEIGHTS].get_type() != Variant::NIL) {
				weights = arrays[Mesh::ARRAY_WEIGHTS];
			}
			surf.bones.resize((size_t)bones.size());
			for (int k = 0; k < bones.size(); ++k) surf.bones[k] = bones[k];
			surf.weights.resize((size_t)weights.size());
			for (int k = 0; k < weights.size(); ++k) surf.weights[k] = weights[k];
			int stride = (verts.size() > 0 && bones.size() > 0) ? bones.size() / verts.size() : 0;
			surf.stride = stride < 1 ? 4 : stride;
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
	auto bucket_for = [&](const Ref<Material> &mat) -> SurfBucket & {
		uint64_t key = material_merge_key(mat);
		auto it = key_to_idx.find(key);
		if (it == key_to_idx.end()) {
			int bi = (int)buckets.size();
			key_to_idx[key] = bi;
			SurfBucket b;
			b.mat = mat;
			buckets.push_back(std::move(b));
			return buckets.back();
		}
		return buckets[it->second];
	};

	for (const CrowdSkin::Surf &surf : skin.surfs) {
		SurfBucket &b = bucket_for(surf.mat);
		const int n = (int)surf.rest_v.size();
		const int base = b.verts.size();
		const bool skinned = surf.rig_i >= 0 && surf.rig_i < (int)palettes.size() && !palettes[surf.rig_i].empty();
		const Transform3D xf = skinned ? skin.rigs[surf.rig_i].xf : surf.rigid_xf;
		const std::vector<Transform3D> *pal = skinned ? &palettes[surf.rig_i] : nullptr;
		for (int v = 0; v < n; ++v) {
			Vector3 p = surf.rest_v[v];
			Vector3 nrm = surf.rest_n[v];
			if (pal) {
				Vector3 skinned_p(0, 0, 0);
				Vector3 sn(0, 0, 0);
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
						wsum += w;
					}
				}
				if (wsum < 0.0001f) {
					skinned_p = p;
					sn = nrm;
				} else if (Math::abs(wsum - 1.0f) > 0.01f) {
					skinned_p /= wsum;
					sn /= wsum;
				}
				p = skinned_p;
				nrm = sn;
			}
			b.verts.push_back(xf.xform(p));
			b.norms.push_back(xf.basis.xform(nrm).normalized());
			b.uvs.push_back(v < (int)surf.uv.size() ? surf.uv[v] : Vector2());
			b.colors.push_back(v < (int)surf.col.size() ? surf.col[v] : Color(1, 1, 1, 1));
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
	return flatten_to_one_surface(buckets);
}

Ref<ArrayMesh> ModelLibrary::bake_posed_crowd_mesh(Node *root) {
	CrowdSkin skin;
	if (!capture_crowd_skin(root, skin)) return Ref<ArrayMesh>();
	return bake_crowd_skin(skin);
}

ModelLibrary::Entry ModelLibrary::load(const String &category, const String &id) {
	String key = category + String("/") + id;
	Entry e;

	Vector<String> candidates;
	candidates.push_back(String("res://assets/models/") + category + String("/") + id + String(".glb"));
	candidates.push_back(String("res://assets/models/units/") + id + String(".glb"));
	candidates.push_back(String("res://assets/models/buildings/") + id + String(".glb"));
	candidates.push_back(String("res://assets/models/vegetation/") + id + String(".glb"));
	candidates.push_back(String("res://assets/models/") + id + String(".glb"));

	// Fallbacks only for IDs that still share a stand-in mesh
	if (category == "units") {
		if (id == "healer") {
			candidates.push_back("res://assets/models/units/villager.glb");
		}
	} else if (category == "buildings") {
		if (id == "gate") {
			candidates.push_back("res://assets/models/buildings/wall.glb");
		}
	}

	for (int i = 0; i < candidates.size(); ++i) {
		const String &path = candidates[i];
		if (ResourceLoader::get_singleton()->exists(path)) {
			Ref<PackedScene> ps = ResourceLoader::get_singleton()->load(path);
			if (ps.is_valid()) {
				Node *inst = ps->instantiate();
				if (inst) {
					apply_imported_material_fixups(inst);
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
							e.y_offset = e.animated ? 0.0f : -e.aabb.position.y * e.scale;
							e.valid = true;
							char buf[192];
							std::snprintf(buf, sizeof(buf), "%s/%s surfaces full=%d crowd=%d",
									category.utf8().get_data(), id.utf8().get_data(),
									e.mesh->get_surface_count(),
									e.crowd_mesh.is_valid() ? e.crowd_mesh->get_surface_count() : 0);
							Logger::info("mesh", buf);
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
}

} // namespace rtso
