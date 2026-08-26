#pragma once
#include <string>
#include <vector>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/quaternion.hpp>
namespace rtso {
struct PsaKey { godot::Vector3 t; godot::Quaternion q; };
struct PsaAnim {
    std::string name;
    float frameTime = 33.3333f;
    uint32_t numFrames = 0;
    uint32_t numBones = 0;
    std::vector<PsaKey> keys; // numFrames * numBones
    float duration() const { return frameTime * numFrames; }
    bool load_psa(const std::string& path);
    void sample(float time_ms, bool loop, std::vector<PsaKey>& out) const;
};
}
