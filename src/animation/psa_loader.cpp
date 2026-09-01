#include "psa_loader.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/string.hpp>
#include <fstream>
#include <cstring>
#include <cmath>

namespace rtso {

static std::string resolve_psa_alias(const std::string& input) {
	std::string s = input;
	if (s.size() > 4 && s.substr(s.size() - 4) == ".psa") {
		s = s.substr(0, s.size() - 4);
	} else if (s.size() > 5 && s.substr(s.size() - 5) == ".json") {
		s = s.substr(0, s.size() - 5);
	}
	size_t last_slash = s.find_last_of("/\\");
	if (last_slash != std::string::npos) {
		s = s.substr(last_slash + 1);
	}
	if (s == "archer_shoot") return "archer_attack";
	if (s == "villager_gather" || s == "gather") return "gather_wood";
	if (s == "villager_idle") return "citizen_idle";
	if (s == "villager_walk") return "citizen_walk";
	if (s == "villager_run") return "citizen_run";
	if (s == "villager_attack") return "biped_attack";
	if (s == "trader_death") return "trader_death";
	return s;
}

static bool read_file_bytes(const std::string& path, std::vector<uint8_t>& out_bytes) {
	// 1. Try Godot FileAccess
	godot::String gpath = godot::String(path.c_str());
	if (godot::FileAccess::file_exists(gpath)) {
		godot::Ref<godot::FileAccess> f = godot::FileAccess::open(gpath, godot::FileAccess::READ);
		if (f.is_valid()) {
			uint64_t len = f->get_length();
			if (len > 0) {
				out_bytes.resize((size_t)len);
				uint64_t read_bytes = f->get_buffer(out_bytes.data(), len);
				if (read_bytes == len) return true;
			}
		}
	}
	// 2. Try std::ifstream
	std::ifstream is(path, std::ios::binary | std::ios::ate);
	if (is.is_open()) {
		std::streamsize size = is.tellg();
		is.seekg(0, std::ios::beg);
		if (size > 0) {
			out_bytes.resize((size_t)size);
			if (is.read(reinterpret_cast<char*>(out_bytes.data()), size)) {
				return true;
			}
		}
	}
	return false;
}

bool PsaAnim::load_psa(const std::string& path) {
	std::string resolved_alias = resolve_psa_alias(path);
	std::vector<std::string> candidates = {
		path,
		"res://" + path,
		"assets/animations/" + path,
		"res://assets/animations/" + path,
		path + ".psa",
		"res://" + path + ".psa",
		"assets/animations/" + path + ".psa",
		"res://assets/animations/" + path + ".psa",
		"assets/animations/" + resolved_alias + ".psa",
		"res://assets/animations/" + resolved_alias + ".psa",
		"/home/abhi/project/RTS0/assets/animations/" + resolved_alias + ".psa"
	};

	std::vector<uint8_t> buffer;
	bool found = false;
	for (const auto& c : candidates) {
		if (read_file_bytes(c, buffer) && buffer.size() >= 16) {
			found = true;
			break;
		}
	}

	if (!found || buffer.size() < 16) {
		return false;
	}

	const uint8_t* data = buffer.data();
	size_t size = buffer.size();

	if (memcmp(data, "PSSA", 4) != 0) {
		return false;
	}

	uint32_t version = 0, payload_size = 0;
	memcpy(&version, data + 4, 4);
	memcpy(&payload_size, data + 8, 4);

	size_t offset = 12;
	if (offset + 4 > size) return false;

	uint32_t str_len = 0;
	memcpy(&str_len, data + offset, 4);
	offset += 4;
	offset += str_len;

	if (offset + 12 > size) return false;

	float f_time = 0.0f;
	uint32_t n_keys = 0, n_frames = 0;
	memcpy(&f_time, data + offset, 4); offset += 4;
	memcpy(&n_keys, data + offset, 4); offset += 4;
	memcpy(&n_frames, data + offset, 4); offset += 4;

	size_t required_bytes = (size_t)n_keys * (size_t)n_frames * 28;
	if (offset + required_bytes > size) return false;

	name = resolved_alias;
	frameTime = f_time > 0.001f ? f_time : 33.3333f;
	numBones = n_keys;
	numFrames = n_frames;
	keys.resize((size_t)n_keys * (size_t)n_frames);

	for (size_t i = 0; i < (size_t)n_keys * (size_t)n_frames; ++i) {
		float tx, ty, tz, qx, qy, qz, qw;
		memcpy(&tx, data + offset, 4); offset += 4;
		memcpy(&ty, data + offset, 4); offset += 4;
		memcpy(&tz, data + offset, 4); offset += 4;
		memcpy(&qx, data + offset, 4); offset += 4;
		memcpy(&qy, data + offset, 4); offset += 4;
		memcpy(&qz, data + offset, 4); offset += 4;
		memcpy(&qw, data + offset, 4); offset += 4;

		keys[i].t = godot::Vector3(tx, ty, tz);
		keys[i].q = godot::Quaternion(qx, qy, qz, qw);
	}

	return true;
}

void PsaAnim::sample(float time_ms, bool loop, std::vector<PsaKey>& out) const {
	if (numFrames == 0 || numBones == 0 || keys.empty()) {
		out.clear();
		return;
	}
	out.resize(numBones);
	if (numFrames == 1 || frameTime <= 0.0001f) {
		for (size_t i = 0; i < numBones; ++i) {
			out[i] = keys[i];
		}
		return;
	}

	float fstartframe = time_ms / frameTime;
	if (fstartframe < 0.0f) fstartframe = 0.0f;
	size_t startframe = (size_t)(int)fstartframe;
	float deltatime = fstartframe - (float)startframe;

	if (loop) {
		startframe %= numFrames;
		size_t endframe = (startframe + 1) % numFrames;
		for (size_t i = 0; i < numBones; ++i) {
			const PsaKey& k0 = keys[startframe * numBones + i];
			const PsaKey& k1 = keys[endframe * numBones + i];
			out[i].t = k0.t.lerp(k1.t, deltatime);
			out[i].q = k0.q.slerp(k1.q, deltatime);
		}
	} else {
		if (startframe >= numFrames - 1) {
			size_t last = numFrames - 1;
			for (size_t i = 0; i < numBones; ++i) {
				out[i] = keys[last * numBones + i];
			}
		} else {
			size_t endframe = startframe + 1;
			for (size_t i = 0; i < numBones; ++i) {
				const PsaKey& k0 = keys[startframe * numBones + i];
				const PsaKey& k1 = keys[endframe * numBones + i];
				out[i].t = k0.t.lerp(k1.t, deltatime);
				out[i].q = k0.q.slerp(k1.q, deltatime);
			}
		}
	}
}

void PsaAnim::sample_transforms(float time_ms, bool loop, std::vector<godot::Transform3D>& out) const {
	std::vector<PsaKey> pose;
	sample(time_ms, loop, pose);
	out.resize(pose.size());
	for (size_t i = 0; i < pose.size(); ++i) {
		out[i] = godot::Transform3D(godot::Basis(pose[i].q), pose[i].t);
	}
}

void PsaAnim::blend_poses(const std::vector<PsaKey>& a, const std::vector<PsaKey>& b, float weight, std::vector<PsaKey>& out) {
	if (a.empty()) { out = b; return; }
	if (b.empty()) { out = a; return; }
	size_t count = std::min(a.size(), b.size());
	out.resize(count);
	float w = std::clamp(weight, 0.0f, 1.0f);
	for (size_t i = 0; i < count; ++i) {
		out[i].t = a[i].t.lerp(b[i].t, w);
		out[i].q = a[i].q.slerp(b[i].q, w);
	}
}

void PsaAnim::blend_transforms(const std::vector<godot::Transform3D>& a, const std::vector<godot::Transform3D>& b, float weight, std::vector<godot::Transform3D>& out) {
	if (a.empty()) { out = b; return; }
	if (b.empty()) { out = a; return; }
	size_t count = std::min(a.size(), b.size());
	out.resize(count);
	float w = std::clamp(weight, 0.0f, 1.0f);
	for (size_t i = 0; i < count; ++i) {
		godot::Vector3 t = a[i].origin.lerp(b[i].origin, w);
		godot::Quaternion qa = a[i].basis.get_rotation_quaternion();
		godot::Quaternion qb = b[i].basis.get_rotation_quaternion();
		godot::Quaternion q = qa.slerp(qb, w);
		out[i] = godot::Transform3D(godot::Basis(q), t);
	}
}

} // namespace rtso
