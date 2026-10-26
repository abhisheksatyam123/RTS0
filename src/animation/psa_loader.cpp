#include "psa_loader.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/string.hpp>
#include <fstream>
#include <cstring>
#include <cmath>

namespace rtso {

// Unit-type -> clip alias table. Canonical per-variant clip names that have no
// dedicated .psa on disk are folded onto the shipped generic clips.
// 0AD refs: source/graphics/UnitAnimation.h:60 (SetAnimationState state names),
//   source/graphics/UnitAnimation.cpp:113 (SetAnimationState),
//   public.zip art/variants/biped/attack_ranged_archer.xml (archer attack_ranged),
//   public.zip art/variants/biped/attack_melee_female.xml (female attack_melee),
//   public.zip art/variants/biped/death_infantry.xml (infantry Death).
static const std::pair<const char*, const char*> kPsaAliasTable[] = {
	// legacy aliases (kept)
	{"archer_shoot", "archer_attack"}, // art/variants/biped/attack_ranged_archer.xml
	{"villager_gather", "gather_wood"},
	{"gather", "gather_wood"},
	{"villager_idle", "citizen_idle"},
	{"villager_walk", "citizen_walk"},
	{"villager_run", "citizen_run"},
	{"villager_attack", "biped_attack"}, // art/variants/biped/attack_melee_female.xml
	// citizen / spearman / swordsman generic infantry (death_infantry.xml)
	{"citizen_attack", "biped_attack"},
	{"citizen_death", "biped_death"},
	{"spearman_idle", "biped_idle"},
	{"spearman_walk", "biped_walk"},
	{"spearman_run", "biped_run"},
	{"spearman_attack", "biped_attack"},
	{"spearman_death", "biped_death"},
	{"swordsman_idle", "biped_idle"},
	{"swordsman_walk", "biped_walk"},
	{"swordsman_run", "biped_run"},
	{"swordsman_attack", "biped_attack"},
	{"swordsman_death", "biped_death"},
	// female citizen (female_* movement exists; combat falls back to infantry)
	{"female_attack", "biped_attack"}, // art/variants/biped/attack_melee_female.xml
	{"female_death", "biped_death"}, // art/variants/biped/death_infantry.xml
	{"female_citizen_idle", "female_idle"},
	{"female_citizen_walk", "female_walk"},
	{"female_citizen_run", "female_run"},
	{"female_citizen_attack", "biped_attack"},
	{"female_citizen_death", "biped_death"},
	// archer maiden: female locomotion + ranged attack
	{"archer_maiden_idle", "female_idle"},
	{"archer_maiden_walk", "female_walk"},
	{"archer_maiden_run", "female_run"},
	{"archer_maiden_attack", "archer_attack"}, // attack_ranged_archer.xml
	{"archer_maiden_death", "biped_death"},
	// carnyx / hero: plain infantry clips
	{"carnyx_idle", "biped_idle"},
	{"carnyx_walk", "biped_walk"},
	{"carnyx_run", "biped_run"},
	{"carnyx_attack", "biped_attack"},
	{"carnyx_death", "biped_death"},
	{"hero_idle", "biped_idle"},
	{"hero_walk", "biped_walk"},
	{"hero_run", "biped_run"},
	{"hero_attack", "biped_attack"},
	{"hero_death", "biped_death"},
	// cavalry rider (only walk/idle/attack/death shipped)
	{"cavalry_run", "cavalry_walk"},
	{"cavalry_carry", "biped_rider_cavalry_generic_carry_meat"},
	{"cavalry_idle_carry", "biped_rider_cavalry_generic_idle_carry_meat"},
	// traders on foot (only idle/walk/death shipped)
	{"trader_run", "trader_walk"},
	{"trader_attack", "biped_attack"},
	// camel-borne traders use camel clips
	{"cart_trader_idle", "camel_idle"},
	{"cart_trader_walk", "camel_walk"},
	{"cart_trader_run", "camel_run"},
	{"cart_trader_attack", "camel_attack"},
	{"cart_trader_death", "camel_death"},
	{"pers_trader_idle", "camel_idle"},
	{"pers_trader_walk", "camel_walk"},
	{"pers_trader_run", "camel_run"},
	{"pers_trader_attack", "camel_attack"},
	{"pers_trader_death", "camel_death"},
	// maurya chariot trader uses maur_ch clips
	{"maur_trader_idle", "maur_ch_idle"},
	{"maur_trader_walk", "maur_ch_move"},
	{"maur_trader_run", "maur_ch_move"},
	{"maur_trader_attack", "maur_ch_idle"},
	{"maur_trader_death", "maur_ch_death"},
	// ships: trireme/rowing only ship idle+walk; sail ships use merchant_sail_*
	{"trireme_run", "trireme_walk"},
	{"trireme_attack", "trireme_idle"},
	{"trireme_death", "trireme_idle"},
	{"rowing_run", "rowing_walk"},
	{"rowing_attack", "rowing_idle"},
	{"rowing_death", "rowing_idle"},
	{"ship_idle", "trireme_idle"},
	{"ship_walk", "trireme_walk"},
	{"ship_run", "trireme_walk"},
	{"ship_attack", "trireme_idle"},
	{"ship_death", "trireme_idle"},
	{"merchant_ship_idle", "merchant_sail_idle"},
	{"merchant_ship_walk", "merchant_sail_walk"},
	{"merchant_ship_run", "merchant_sail_run"},
	{"merchant_ship_attack", "merchant_sail_idle"},
	{"merchant_ship_death", "merchant_sail_idle"},
	// fauna: sheep has no attack clip; war_dog reuses dog clips
	{"sheep_attack", "sheep_idle"},
	{"war_dog_idle", "dog_idle"},
	{"war_dog_walk", "dog_walk"},
	{"war_dog_run", "dog_run"},
	{"war_dog_attack", "dog_attack"},
	{"war_dog_death", "dog_death"},
	// siege: bolt only ships idle+attack; catapult is static
	{"bolt_shooter_idle", "bolt_idle"},
	{"bolt_shooter_walk", "bolt_idle"},
	{"bolt_shooter_run", "bolt_idle"},
	{"bolt_shooter_attack", "bolt_attack"},
	{"bolt_shooter_death", "bolt_idle"},
	{"catapult_walk", "catapult_idle"},
	{"catapult_run", "catapult_idle"},
	{"catapult_death", "catapult_idle"},
	// elephant crew / rams / chariots / healer / deer
	{"worker_elephant_idle", "elephant_idle"},
	{"worker_elephant_walk", "elephant_walk"},
	{"worker_elephant_run", "elephant_run"},
	{"worker_elephant_attack", "elephant_attack"},
	{"worker_elephant_death", "elephant_death"},
	{"ram_run", "ram_walk"},
	{"chariot_attack", "chariot_idle"},
	{"scythed_chariot_idle", "chariot_idle"},
	{"scythed_chariot_walk", "chariot_walk"},
	{"scythed_chariot_run", "chariot_run"},
	{"scythed_chariot_attack", "chariot_idle"},
	{"scythed_chariot_death", "chariot_death"},
	{"healer_attack", "healer_heal"},
	{"healer_death", "biped_death"},
	{"deer_run", "deer_walk"},
};

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
	for (const auto& e : kPsaAliasTable) {
		if (s == e.first) return e.second;
	}
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
