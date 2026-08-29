#pragma once
#include <string>
#include <vector>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/transform3d.hpp>

namespace rtso {

struct BoneDef {
	std::string name;
	int parent = -1;
	godot::Vector3 bind_pos = godot::Vector3(0, 0, 0);
	godot::Quaternion bind_rot = godot::Quaternion(0, 0, 0, 1);
};

class SkeletonDef {
public:
	std::string name;
	std::vector<BoneDef> bones;

	bool load_xml(const std::string& path);
	size_t bone_count() const { return bones.size(); }
	int find(const std::string& name) const;
	void compute_global_transforms(const std::vector<godot::Transform3D>& local_xfs, std::vector<godot::Transform3D>& out_globals) const;
};

} // namespace rtso
