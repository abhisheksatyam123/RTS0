#include "psa_loader.h"
#include <godot_cpp/classes/file_access.hpp>
namespace rtso {
bool PsaAnim::load_psa(const std::string& path) {
    // stub: in real, parse PSSA binary via FileAccess, fill keys
    // For now, return true and create dummy 1-frame
    if (keys.empty()) {
        keys.push_back({{0,0,0},{0,0,0,1}});
        numFrames = 1; numBones = 1; frameTime = 33.33f;
    }
    return true;
}
void PsaAnim::sample(float t, bool loop, std::vector<PsaKey>& out) const {
    out = keys;
    (void)t; (void)loop;
}
}
