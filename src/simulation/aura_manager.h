#ifndef RTSO_AURA_MANAGER_H
#define RTSO_AURA_MANAGER_H

#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rtso {

enum AuraType : uint8_t {
    AURA_GATHER = 0,
    AURA_HEAL   = 1,
    AURA_ATTACK = 2,
    AURA_SPEED  = 3,
    AURA_ARMOR  = 4,
    AURA_COUNT  = 5
};

struct AuraSource {
    uint32_t source_id = 0;
    AuraType type = AURA_GATHER;
    float range = 0.0f;
    float value = 0.0f;
    uint8_t team = 0;
    float x = 0.0f;
    float z = 0.0f;
    bool alive = true;
};

class AuraManager {
public:
    std::vector<AuraSource> sources;

    void clear() { sources.clear(); }

    void register_source(uint32_t id, AuraType type, float range, float value, uint8_t team, float x, float z) {
        AuraSource s;
        s.source_id = id;
        s.type = type;
        s.range = range;
        s.value = value;
        s.team = team;
        s.x = x;
        s.z = z;
        s.alive = true;
        sources.push_back(s);
    }

    void unregister(uint32_t id) {
        sources.erase(std::remove_if(sources.begin(), sources.end(),
            [id](const AuraSource &s) { return s.source_id == id; }), sources.end());
    }

    float query_modifier(float qx, float qz, uint8_t qteam, AuraType type) const {
        float mod = 0.0f;
        if (type == AURA_HEAL) {
            // Sum-stacking for healing
            for (const auto &s : sources) {
                if (!s.alive) continue;
                if (s.team != qteam) continue;
                if (s.type != type) continue;
                float dx = qx - s.x;
                float dz = qz - s.z;
                if (dx * dx + dz * dz <= s.range * s.range) {
                    mod += s.value;
                }
            }
        } else {
            // Max-stacking for buffs (GATHER, ATTACK, SPEED, ARMOR)
            for (const auto &s : sources) {
                if (!s.alive) continue;
                if (s.team != qteam) continue;
                if (s.type != type) continue;
                float dx = qx - s.x;
                float dz = qz - s.z;
                if (dx * dx + dz * dz <= s.range * s.range) {
                    if (s.value > mod) mod = s.value;
                }
            }
        }
        return mod;
    }
};

} // namespace rtso

#endif // RTSO_AURA_MANAGER_H
