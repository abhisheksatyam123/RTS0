#pragma once
#include "psa_loader.h"
#include "skeleton.h"
#include <unordered_map>
#include <string>
namespace rtso {
enum class AnimState { Idle, Walk, Run, Attack, Death, Gather, Carry };
class AnimationController {
    std::unordered_map<AnimState, PsaAnim> anims;
    AnimState current = AnimState::Idle;
    float time_ms = 0;
public:
    bool load(const std::string& base_path);
    void play(AnimState s, bool force=false);
    void update(float dt_ms);
    AnimState get() const { return current; }
    float get_time() const { return time_ms; }
    const PsaAnim* get_anim(AnimState s) const;
};
}
