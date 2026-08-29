#pragma once
#include <string>
#include <vector>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/quaternion.hpp>

namespace rtso {
struct BoneDef {
    std::string name;
    int parent = -1;
    godot::Vector3 bind_pos;
    godot::Quaternion bind_rot;
};
class SkeletonDef {
public:
    std::vector<BoneDef> bones;
    bool load_xml(const std::string& path);
    size_t bone_count() const { return bones.size(); }
    int find(const std::string& name) const;
};
}
