#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/transform3d.hpp>

namespace rtso {

struct PsaKey {
	godot::Vector3 t = godot::Vector3(0, 0, 0);
	godot::Quaternion q = godot::Quaternion(0, 0, 0, 1);
};

struct PsaAnim {
	std::string name;
	float frameTime = 33.3333f;
	uint32_t numFrames = 0;
	uint32_t numBones = 0;
	std::vector<PsaKey> keys; // numFrames * numBones

	float duration() const { return frameTime * (float)numFrames; }
	bool is_valid() const { return numFrames > 0 && numBones > 0 && !keys.empty(); }

	bool load_psa(const std::string& path);
	void sample(float time_ms, bool loop, std::vector<PsaKey>& out) const;
	void sample_transforms(float time_ms, bool loop, std::vector<godot::Transform3D>& out) const;

	static void blend_poses(const std::vector<PsaKey>& a, const std::vector<PsaKey>& b, float weight, std::vector<PsaKey>& out);
	static void blend_transforms(const std::vector<godot::Transform3D>& a, const std::vector<godot::Transform3D>& b, float weight, std::vector<godot::Transform3D>& out);
};

} // namespace rtso
