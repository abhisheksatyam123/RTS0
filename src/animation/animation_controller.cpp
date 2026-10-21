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
			// Horse mount rig. 0AD: public.zip art/variants/horse/* (idle/walk/run),
			// source/graphics/UnitAnimation.cpp:113 SetAnimationState.
			load_clip(AnimState::Idle, "horse_idle");
			load_clip(AnimState::Walk, "horse_walk");
			load_clip(AnimState::Run, "horse_run");
			load_clip(AnimState::Attack, "horse_attack");
			load_clip(AnimState::Death, "horse_death");
			skeleton.load_xml("horse");

			// Mounted rider rig (cavalry_*; run folds to walk via alias table).
			mount_rider_controller = std::make_unique<AnimationController>();
			mount_rider_controller->rig_type = RigType::Biped;
			mount_rider_controller->load_clip(AnimState::Idle, "cavalry_idle");
			mount_rider_controller->load_clip(AnimState::Walk, "cavalry_walk");
			mount_rider_controller->load_clip(AnimState::Run, "cavalry_run");
			mount_rider_controller->load_clip(AnimState::Attack, "cavalry_attack");
			// The mounted gather is the mounted strike: 0 A.D. gathers meat from
			// horseback, and the horse's and the rider's clips are one and the
			// same 39-frame action (horse_attack_01 / attack_melee_01). The
			// rider's own "GatherMeat" is the biped gatherer's standing
			// animation, which leaves the rider above the horse's back.
			load_clip(AnimState::Gather, "horse_attack");
			mount_rider_controller->load_clip(AnimState::Death, "cavalry_death");
			mount_rider_controller->load_clip(AnimState::Gather, "cavalry_attack");
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
			// 0AD: public.zip art/variants/*ram*/idle+move; ram_run folds to ram_walk.
			load_clip(AnimState::Idle, "ram_idle");
			load_clip(AnimState::Walk, "ram_walk");
			load_clip(AnimState::Run, "ram_run");
			load_clip(AnimState::Attack, "ram_attack");
			load_clip(AnimState::Death, "ram_death");
			break;
		}
		case RigType::Wagon: {
			// 0AD 0.28: art/actors/structures/germans/wagon.xml (Walk/Run on
			// wagon_walk, idle, death; no attack clip, so Attack holds the idle).
			load_clip(AnimState::Idle, "mechanical_wagon_idle");
			load_clip(AnimState::Walk, "mechanical_wagon_walk");
			load_clip(AnimState::Run, "mechanical_wagon_walk");
			load_clip(AnimState::Attack, "mechanical_wagon_idle");
			load_clip(AnimState::Death, "mechanical_wagon_death");
			break;
		}
	}
	return !anims.empty();
}

// Unit-type -> clip resolution table (canonical clip per AnimState).
// Every entry below resolves to a .psa on disk, either directly or via the
// alias table in psa_loader.cpp (kPsaAliasTable).
// 0AD refs: source/graphics/UnitAnimation.h:60 + UnitAnimation.cpp:113
//   (SetAnimationState state/action model), public.zip
//   art/variants/biped/attack_ranged_archer.xml (ranged attack),
//   art/variants/biped/attack_melee_female.xml (female melee),
//   art/variants/biped/death_infantry.xml (infantry Death).
// Required coverage: citizen, spearman, archer, cavalry, trader, ship each
// resolve idle/walk/run/attack/death.
bool AnimationController::load(const std::string& base) {
	if (base == "biped" || base == "swordsman" || base == "spearman" ||
			base == "carnyx" || base == "hero") {
		return load_rig(RigType::Biped);
	} else if (base == "archer" || base == "archer_maiden") {
		return load_rig(RigType::Archer);
	} else if (base == "villager" || base == "citizen" || base == "female_citizen") {
		if (base == "female_citizen") {
			anims.clear();
			mount_rider_controller.reset();
			rig_type = RigType::Villager;
			load_clip(AnimState::Idle, "female_idle");
			load_clip(AnimState::Walk, "female_walk");
			load_clip(AnimState::Run, "female_run");
			load_clip(AnimState::Attack, "female_attack");
			load_clip(AnimState::Gather, "gather_wood");
			load_clip(AnimState::Build, "citizen_build");
			load_clip(AnimState::Carry, "carry_wood");
			load_clip(AnimState::Death, "female_death");
			skeleton.load_xml("biped");
			return !anims.empty();
		}
		return load_rig(RigType::Villager);
	} else if (base == "mounted" || base == "cavalry" || base == "horse") {
		return load_rig(RigType::Mounted);
	} else if (base == "trader") {
		anims.clear();
		mount_rider_controller.reset();
		load_clip(AnimState::Idle, "trader_idle");
		load_clip(AnimState::Walk, "trader_walk");
		load_clip(AnimState::Run, "trader_run");
		load_clip(AnimState::Attack, "trader_attack");
		load_clip(AnimState::Death, "trader_death");
		skeleton.load_xml("biped");
		return !anims.empty();
	} else if (base == "cart_trader" || base == "pers_trader") {
		anims.clear();
		mount_rider_controller.reset();
		load_clip(AnimState::Idle, base + "_idle");
		load_clip(AnimState::Walk, base + "_walk");
		load_clip(AnimState::Run, base + "_run");
		load_clip(AnimState::Attack, base + "_attack");
		load_clip(AnimState::Death, base + "_death");
		skeleton.load_xml("camel");
		return !anims.empty();
	} else if (base == "maur_trader") {
		anims.clear();
		mount_rider_controller.reset();
		load_clip(AnimState::Idle, "maur_trader_idle");
		load_clip(AnimState::Walk, "maur_trader_walk");
		load_clip(AnimState::Run, "maur_trader_run");
		load_clip(AnimState::Attack, "maur_trader_attack");
		load_clip(AnimState::Death, "maur_trader_death");
		skeleton.load_xml("maur_ch");
		return !anims.empty();
	} else if (base == "fauna" || base == "deer") {
		return load_rig(RigType::Fauna);
	} else if (base == "sheep") {
		anims.clear();
		mount_rider_controller.reset();
		load_clip(AnimState::Idle, "sheep_idle");
		load_clip(AnimState::Walk, "sheep_walk");
		load_clip(AnimState::Run, "sheep_run");
		load_clip(AnimState::Attack, "sheep_attack");
		load_clip(AnimState::Death, "sheep_death");
		skeleton.load_xml("sheep");
		return !anims.empty();
	} else if (base == "boar") {
		anims.clear();
		mount_rider_controller.reset();
		load_clip(AnimState::Idle, "boar_idle");
		load_clip(AnimState::Walk, "boar_walk");
		load_clip(AnimState::Run, "boar_run");
		load_clip(AnimState::Attack, "boar_attack");
		load_clip(AnimState::Death, "boar_death");
		skeleton.load_xml("boar");
		return !anims.empty();
	} else if (base == "wolf" || base == "war_dog" || base == "dog") {
		anims.clear();
		mount_rider_controller.reset();
		const std::string prefix = (base == "wolf") ? "wolf" : (base == "war_dog" ? "war_dog" : "dog");
		load_clip(AnimState::Idle, prefix + "_idle");
		load_clip(AnimState::Walk, prefix + "_walk");
		load_clip(AnimState::Run, prefix + "_run");
		load_clip(AnimState::Attack, prefix + "_attack");
		load_clip(AnimState::Death, prefix + "_death");
		skeleton.load_xml(base == "wolf" ? "wolf" : "mastiff");
		return !anims.empty();
	} else if (base == "healer") {
		return load_rig(RigType::Healer);
	} else if (base == "ram" || base == "siege_ram") {
		return load_rig(RigType::SiegeRam);
	} else if (base == "bolt_shooter") {
		anims.clear();
		mount_rider_controller.reset();
		load_clip(AnimState::Idle, "bolt_shooter_idle");
		load_clip(AnimState::Walk, "bolt_shooter_walk");
		load_clip(AnimState::Run, "bolt_shooter_run");
		load_clip(AnimState::Attack, "bolt_shooter_attack");
		load_clip(AnimState::Death, "bolt_shooter_death");
		skeleton.load_xml("oxybeles");
		return !anims.empty();
	} else if (base == "catapult") {
		anims.clear();
		mount_rider_controller.reset();
		load_clip(AnimState::Idle, "catapult_idle");
		load_clip(AnimState::Walk, "catapult_walk");
		load_clip(AnimState::Run, "catapult_run");
		load_clip(AnimState::Attack, "catapult_attack");
		load_clip(AnimState::Death, "catapult_death");
		skeleton.load_xml("lithobolos");
		return !anims.empty();
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
	} else if (base == "elephant" || base == "worker_elephant") {
		anims.clear();
		mount_rider_controller.reset();
		const std::string prefix = base; // worker_elephant_* folds via alias table
		load_clip(AnimState::Idle, prefix + "_idle");
		load_clip(AnimState::Walk, prefix + "_walk");
		load_clip(AnimState::Run, prefix + "_run");
		load_clip(AnimState::Attack, prefix + "_attack");
		load_clip(AnimState::Death, prefix + "_death");
		skeleton.load_xml("elephant");
		return !anims.empty();
	} else if (base == "fishing_boat" || base == "rowing" || base == "ship") {
		anims.clear();
		mount_rider_controller.reset();
		load_clip(AnimState::Idle, "rowing_idle");
		load_clip(AnimState::Walk, "rowing_walk");
		load_clip(AnimState::Run, "rowing_run");
		load_clip(AnimState::Gather, "rowing_idle");
		load_clip(AnimState::Attack, "rowing_attack");
		load_clip(AnimState::Carry, "rowing_walk");
		load_clip(AnimState::Death, "rowing_death");
		skeleton.load_xml("rowing_boat");
		return !anims.empty();
	} else if (base == "trireme" || base == "warship") {
		anims.clear();
		mount_rider_controller.reset();
		load_clip(AnimState::Idle, "trireme_idle");
		load_clip(AnimState::Walk, "trireme_walk");
		load_clip(AnimState::Run, "trireme_run");
		load_clip(AnimState::Attack, "trireme_attack");
		load_clip(AnimState::Death, "trireme_death");
		skeleton.load_xml("ship_sail");
		return !anims.empty();
	} else if (base == "merchant" || base == "merchant_ship") {
		anims.clear();
		mount_rider_controller.reset();
		const std::string prefix = (base == "merchant_ship") ? "merchant_ship" : "merchant_sail";
		load_clip(AnimState::Idle, prefix + "_idle");
		load_clip(AnimState::Walk, prefix + "_walk");
		load_clip(AnimState::Run, prefix + "_run");
		load_clip(AnimState::Attack, prefix + "_attack");
		load_clip(AnimState::Death, prefix + "_death");
		skeleton.load_xml("celt_sail");
		return !anims.empty();
	} else if (base == "chariot" || base == "scythed_chariot") {
		anims.clear();
		mount_rider_controller.reset();
		const std::string prefix = base; // scythed_chariot_* folds via alias table
		load_clip(AnimState::Idle, prefix + "_idle");
		load_clip(AnimState::Walk, prefix + "_walk");
		load_clip(AnimState::Run, base == "chariot" ? "chariot_run" : prefix + "_run");
		load_clip(AnimState::Attack, prefix + "_attack");
		load_clip(AnimState::Death, prefix + "_death");
		skeleton.load_xml("chariot");
		return !anims.empty();
	}

	// Try loading default biped clips prefixed by base (aliases cover unknowns).
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
