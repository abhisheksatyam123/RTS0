#ifndef RTSO_AURA_MANAGER_H
#define RTSO_AURA_MANAGER_H

#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "sim_types.h"

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
    int16_t aura = -1; // civ_hero_aura_table() index: buffs only the roster units that aura affects
};

class AuraManager {
public:
    std::vector<AuraSource> sources;

    void clear() { sources.clear(); }

    void register_source(uint32_t id, AuraType type, float range, float value, uint8_t team, float x, float z,
            int16_t aura = -1) {
        AuraSource s;
        s.source_id = id;
        s.type = type;
        s.range = range;
        s.value = value;
        s.team = team;
        s.x = x;
        s.z = z;
        s.alive = true;
        s.aura = aura;
        sources.push_back(s);
    }

    void unregister(uint32_t id) {
        sources.erase(std::remove_if(sources.begin(), sources.end(),
            [id](const AuraSource &s) { return s.source_id == id; }), sources.end());
    }

    // qtmpl is the querying unit's roster template; hero auras skip units their
    // 0 A.D. "affects" classes exclude.
    float query_modifier(float qx, float qz, uint8_t qteam, AuraType type,
            uint16_t qtmpl = CIV_UNIT_TEMPLATE_NONE) const {
        float mod = 0.0f;
        if (type == AURA_HEAL) {
            // Sum-stacking for healing
            for (const auto &s : sources) {
                if (!s.alive) continue;
                if (s.team != qteam) continue;
                if (s.type != type) continue;
                if (s.aura >= 0 && !civ_aura_affects(s.aura, qtmpl)) continue;
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
                if (s.aura >= 0 && !civ_aura_affects(s.aura, qtmpl)) continue;
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
