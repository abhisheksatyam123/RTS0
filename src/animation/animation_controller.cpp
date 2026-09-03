#include "animation_controller.h"
#include <algorithm>

namespace rtso {

AnimationController::AnimationController() : rig_type(RigType::Biped) {}
AnimationController::~AnimationController() = default;

bool AnimationController::load_clip(AnimState state, const std::string& clip_name) {
	PsaAnim anim;
	if (anim.load_psa(clip_name)) {
		anims[state] = anim;
		return true;
	}
	return false;
}

bool AnimationController::set_anim(AnimState state, const PsaAnim& anim) {
	if (anim.is_valid()) {
		anims[state] = anim;
		return true;
	}
	return false;
}

bool AnimationController::load_rig(RigType rig) {
	rig_type = rig;
	anims.clear();
	mount_rider_controller.reset();

	switch (rig) {
		case RigType::Biped: {
			load_clip(AnimState::Idle, "biped_idle");
			load_clip(AnimState::Walk, "biped_walk");
			load_clip(AnimState::Run, "biped_run");
			load_clip(AnimState::Attack, "biped_attack");
			load_clip(AnimState::Death, "biped_death");
			skeleton.load_xml("biped");
			break;
		}
		case RigType::Archer: {
			load_clip(AnimState::Idle, "biped_idle");
			load_clip(AnimState::Walk, "biped_walk");
			load_clip(AnimState::Run, "biped_run");
			load_clip(AnimState::Attack, "archer_attack"); // archer_shoot
			load_clip(AnimState::Death, "biped_death");
			skeleton.load_xml("biped");
			break;
		}
		case RigType::Villager: {
			load_clip(AnimState::Idle, "citizen_idle");
			load_clip(AnimState::Walk, "citizen_walk");
			load_clip(AnimState::Run, "citizen_run");
			load_clip(AnimState::Attack, "biped_attack");
			load_clip(AnimState::Gather, "gather_wood");
			load_clip(AnimState::Build, "citizen_build");
			load_clip(AnimState::Carry, "carry_wood");
			load_clip(AnimState::Death, "biped_death");
			skeleton.load_xml("biped");
			break;
		}
		case RigType::Mounted: {
			// Horse mount rig
			load_clip(AnimState::Idle, "horse_idle");
			load_clip(AnimState::Walk, "horse_walk");
			load_clip(AnimState::Run, "horse_run");
			load_clip(AnimState::Attack, "horse_idle");
			load_clip(AnimState::Death, "horse_death");
			skeleton.load_xml("horse");

			// Mounted rider rig
			mount_rider_controller = std::make_unique<AnimationController>();
			mount_rider_controller->rig_type = RigType::Biped;
			mount_rider_controller->load_clip(AnimState::Idle, "cavalry_idle");
			mount_rider_controller->load_clip(AnimState::Walk, "cavalry_walk");
			mount_rider_controller->load_clip(AnimState::Run, "cavalry_walk");
			mount_rider_controller->load_clip(AnimState::Attack, "cavalry_attack");
			mount_rider_controller->load_clip(AnimState::Death, "cavalry_death");
			mount_rider_controller->get_skeleton().load_xml("biped");
			break;
		}
		case RigType::Fauna: {
			load_clip(AnimState::Idle, "deer_idle");
			load_clip(AnimState::Walk, "deer_walk");
			load_clip(AnimState::Run, "deer_walk");
			load_clip(AnimState::Death, "deer_death");
			skeleton.load_xml("deer");
			break;
		}
		case RigType::Healer: {
			load_clip(AnimState::Idle, "healer_idle");
			load_clip(AnimState::Walk, "healer_walk");
			load_clip(AnimState::Run, "healer_run");
			load_clip(AnimState::Attack, "healer_heal");
			load_clip(AnimState::Death, "biped_death");
			skeleton.load_xml("biped");
			break;
		}
		case RigType::SiegeRam: {
			load_clip(AnimState::Idle, "ram_idle");
			load_clip(AnimState::Walk, "ram_walk");
			load_clip(AnimState::Run, "ram_walk");
			load_clip(AnimState::Attack, "ram_attack");
			load_clip(AnimState::Death, "ram_death");
			break;
		}
	}
	return !anims.empty();
}

bool AnimationController::load(const std::string& base) {
	if (base == "biped" || base == "swordsman" || base == "spearman") {
		return load_rig(RigType::Biped);
	} else if (base == "archer") {
		return load_rig(RigType::Archer);
	} else if (base == "villager" || base == "citizen") {
		return load_rig(RigType::Villager);
	} else if (base == "mounted" || base == "cavalry" || base == "horse") {
		return load_rig(RigType::Mounted);
	} else if (base == "trader") {
		anims.clear();
		mount_rider_controller.reset();
		load_clip(AnimState::Idle, "trader_idle");
		load_clip(AnimState::Walk, "trader_walk");
		load_clip(AnimState::Run, "trader_walk");
		load_clip(AnimState::Death, "trader_death");
		return !anims.empty();
	} else if (base == "fauna" || base == "deer") {
		return load_rig(RigType::Fauna);
	} else if (base == "healer") {
		return load_rig(RigType::Healer);
	} else if (base == "ram" || base == "siege_ram") {
		return load_rig(RigType::SiegeRam);
	} else if (base == "camel") {
		anims.clear();
		mount_rider_controller.reset();
		load_clip(AnimState::Idle, "camel_idle");
		load_clip(AnimState::Walk, "camel_walk");
		load_clip(AnimState::Run, "camel_run");
		load_clip(AnimState::Attack, "camel_attack");
		load_clip(AnimState::Death, "camel_death");
		skeleton.load_xml("camel");
		return !anims.empty();
	} else if (base == "elephant") {
		anims.clear();
		mount_rider_controller.reset();
		load_clip(AnimState::Idle, "elephant_idle");
		load_clip(AnimState::Walk, "elephant_walk");
		load_clip(AnimState::Run, "elephant_run");
		load_clip(AnimState::Attack, "elephant_attack");
		load_clip(AnimState::Death, "elephant_death");
		skeleton.load_xml("elephant");
		return !anims.empty();
	} else if (base == "fishing_boat" || base == "rowing") {
		anims.clear();
		mount_rider_controller.reset();
		load_clip(AnimState::Idle, "rowing_idle");
		load_clip(AnimState::Walk, "rowing_walk");
		load_clip(AnimState::Run, "rowing_walk");
		load_clip(AnimState::Gather, "rowing_idle");
		load_clip(AnimState::Attack, "rowing_idle");
		load_clip(AnimState::Carry, "rowing_walk");
		load_clip(AnimState::Death, "rowing_idle");
		skeleton.load_xml("rowing_boat");
		return !anims.empty();
	} else if (base == "trireme" || base == "warship") {
		anims.clear();
		mount_rider_controller.reset();
		load_clip(AnimState::Idle, "trireme_idle");
		load_clip(AnimState::Walk, "trireme_walk");
		load_clip(AnimState::Run, "trireme_walk");
		load_clip(AnimState::Attack, "trireme_idle");
		load_clip(AnimState::Death, "trireme_idle");
		skeleton.load_xml("ship_sail");
		return !anims.empty();
	} else if (base == "merchant" || base == "merchant_ship") {
		anims.clear();
		mount_rider_controller.reset();
		load_clip(AnimState::Idle, "merchant_sail_idle");
		load_clip(AnimState::Walk, "merchant_sail_walk");
		load_clip(AnimState::Run, "merchant_sail_run");
		load_clip(AnimState::Death, "merchant_sail_idle");
		skeleton.load_xml("celt_sail");
		return !anims.empty();
	} else if (base == "chariot") {
		anims.clear();
		mount_rider_controller.reset();
		load_clip(AnimState::Idle, "chariot_idle");
		load_clip(AnimState::Walk, "chariot_walk");
		load_clip(AnimState::Run, "chariot_walk");
		load_clip(AnimState::Death, "chariot_death");
		skeleton.load_xml("chariot");
		return !anims.empty();
	}

	// Try loading default biped clips prefixed by base
	load_clip(AnimState::Idle, base + "_idle");
	load_clip(AnimState::Walk, base + "_walk");
	load_clip(AnimState::Run, base + "_run");
	load_clip(AnimState::Attack, base + "_attack");
	load_clip(AnimState::Death, base + "_death");
	return !anims.empty();
}

void AnimationController::play(AnimState s, bool f, float blend_time_ms) {
	if (!f && s == current) return;
	if (blend_time_ms > 0.0f && has_anim(current)) {
		prev_state = current;
		prev_time_ms = time_ms;
		blend_timer_ms = 0.0f;
		blend_duration_ms = blend_time_ms;
	} else {
		blend_timer_ms = blend_duration_ms;
	}
	current = s;
	time_ms = 0.0f;

	if (mount_rider_controller) {
		mount_rider_controller->play(s, f, blend_time_ms);
	}
}

void AnimationController::update(float dt) {
	float scaled_dt = dt * speed_scale;
	time_ms += scaled_dt;
	if (blend_timer_ms < blend_duration_ms) {
		blend_timer_ms += dt;
		prev_time_ms += scaled_dt;
	}
	if (mount_rider_controller) {
		mount_rider_controller->update(dt);
	}
}

const PsaAnim* AnimationController::get_anim(AnimState s) const {
	auto it = anims.find(s);
	if (it != anims.end() && it->second.is_valid()) return &it->second;
	// Fallback to Idle if requested animation is missing
	if (s != AnimState::Idle) {
		auto idle_it = anims.find(AnimState::Idle);
		if (idle_it != anims.end() && idle_it->second.is_valid()) return &idle_it->second;
	}
	return nullptr;
}

bool AnimationController::is_looping(AnimState s) const {
	return s != AnimState::Death;
}

void AnimationController::sample_current_pose(std::vector<PsaKey>& out_pose) const {
	const PsaAnim* cur_anim = get_anim(current);
	if (!cur_anim) {
		out_pose.clear();
		return;
	}

	std::vector<PsaKey> cur_pose;
	cur_anim->sample(time_ms, is_looping(current), cur_pose);

	if (blend_timer_ms < blend_duration_ms && has_anim(prev_state)) {
		const PsaAnim* prev_anim = get_anim(prev_state);
		if (prev_anim) {
			std::vector<PsaKey> prev_pose;
			prev_anim->sample(prev_time_ms, is_looping(prev_state), prev_pose);
			float weight = blend_duration_ms > 0.001f ? (blend_timer_ms / blend_duration_ms) : 1.0f;
			PsaAnim::blend_poses(prev_pose, cur_pose, weight, out_pose);
			return;
		}
	}
	out_pose = cur_pose;
}

void AnimationController::sample_current_transforms(std::vector<godot::Transform3D>& out_transforms) const {
	std::vector<PsaKey> pose;
	sample_current_pose(pose);
	out_transforms.resize(pose.size());
	for (size_t i = 0; i < pose.size(); ++i) {
		out_transforms[i] = godot::Transform3D(godot::Basis(pose[i].q), pose[i].t);
	}
}

void AnimationController::evaluate_bone_transforms(std::vector<godot::Transform3D>& out_transforms) const {
	sample_current_transforms(out_transforms);
}

} // namespace rtso
