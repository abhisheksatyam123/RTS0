#pragma once
#include "psa_loader.h"
#include "skeleton.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <memory>
#include <godot_cpp/variant/transform3d.hpp>

namespace rtso {

enum class AnimState {
	Idle,
	Walk,
	Run,
	Attack,
	Death,
	Gather,
	Carry,
	Build
};

enum class RigType {
	Biped,
	Mounted,
	Archer,
	Villager,
	Fauna,
	SiegeRam,
	Healer
};

class AnimationController {
private:
	std::unordered_map<AnimState, PsaAnim> anims;
	AnimState current = AnimState::Idle;
	AnimState prev_state = AnimState::Idle;
	float time_ms = 0.0f;
	float prev_time_ms = 0.0f;
	float blend_timer_ms = 0.0f;
	float blend_duration_ms = 150.0f;
	float speed_scale = 1.0f;
	RigType rig_type = RigType::Biped;

	SkeletonDef skeleton;
	std::unique_ptr<AnimationController> mount_rider_controller;

public:
	AnimationController();
	~AnimationController();

	bool load(const std::string& base_path_or_rig);
	bool load_rig(RigType rig);
	bool load_clip(AnimState state, const std::string& clip_name);
	bool set_anim(AnimState state, const PsaAnim& anim);

	void play(AnimState s, bool force = false, float blend_time_ms = 150.0f);
	void update(float dt_ms);

	AnimState get() const { return current; }
	float get_time() const { return time_ms; }
	RigType get_rig_type() const { return rig_type; }
	void set_speed(float speed) { speed_scale = speed; }
	float get_speed() const { return speed_scale; }

	const PsaAnim* get_anim(AnimState s) const;
	bool has_anim(AnimState s) const { return anims.find(s) != anims.end() && anims.at(s).is_valid(); }
	bool is_looping(AnimState s) const;

	void sample_current_pose(std::vector<PsaKey>& out_pose) const;
	void sample_current_transforms(std::vector<godot::Transform3D>& out_transforms) const;
	void evaluate_bone_transforms(std::vector<godot::Transform3D>& out_transforms) const;

	const SkeletonDef& get_skeleton() const { return skeleton; }
	SkeletonDef& get_skeleton() { return skeleton; }

	AnimationController* get_mount_rider_controller() { return mount_rider_controller.get(); }
	const AnimationController* get_mount_rider_controller() const { return mount_rider_controller.get(); }
};

} // namespace rtso
