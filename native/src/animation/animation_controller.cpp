#include "animation_controller.h"
namespace rtso {
bool AnimationController::load(const std::string& base) { (void)base; return true; }
void AnimationController::play(AnimState s, bool f) { if (!f && s==current) return; current=s; time_ms=0; }
void AnimationController::update(float dt) { time_ms += dt; }
const PsaAnim* AnimationController::get_anim(AnimState s) const {
    auto it=anims.find(s); return it==anims.end()?nullptr:&it->second;
}
}
