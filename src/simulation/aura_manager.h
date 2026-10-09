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
    AURA_HEALTH = 5, // Health/Max multiplier
    AURA_VISION = 6, // Vision/Range multiplier
    AURA_REQUIRED_XP = 7, // Promotion/RequiredXp multiplier
    AURA_COUNT  = 8
};

// 0 A.D. Auras.js affectedPlayers as bits over the source's owner (CivHeroAura::players).
enum AuraPlayers : uint8_t {
    AURA_PLAYERS_OWNER = 1,
    AURA_PLAYERS_ALLIES = 2, // the owner's allies other than itself
    AURA_PLAYERS_ENEMIES = 4,
    AURA_PLAYERS_ALLY = AURA_PLAYERS_OWNER | AURA_PLAYERS_ALLIES // "Ally" / "MutualAlly"
};

// Bit of player p in AuraSource::affected; Gaia and other ids past the slots share bit 8.
inline uint16_t aura_player_bit(uint8_t p) { return (uint16_t)(1u << (p < 8 ? p : 8)); }

struct AuraSource {
    uint32_t source_id = 0;
    AuraType type = AURA_GATHER;
    float range = 0.0f;
    float value = 0.0f;
    uint16_t affected = 0; // aura_player_bit() of every player the aura reaches
    float x = 0.0f;
    float z = 0.0f;
    bool alive = true;
    int16_t aura = -1; // civ_hero_aura_table() index: reaches only the roster units that aura affects
    uint8_t need_class = 0xFF; // CombatClass the unit must have (0xFF = any), for non-roster auras
};

class AuraManager {
public:
    std::vector<AuraSource> sources;
    uint16_t type_count[AURA_COUNT] = {}; // sources per AuraType, so rare kinds cost nothing

    void clear() {
        sources.clear();
        for (auto &c : type_count) c = 0;
    }

    // affected: aura_player_bit() mask of the players the aura reaches (the caller
    // resolves 0 A.D. affectedPlayers against diplomacy).
    void register_source(uint32_t id, AuraType type, float range, float value, uint16_t affected, float x, float z,
            int16_t aura = -1, uint8_t need_class = 0xFF) {
        AuraSource s;
        s.source_id = id;
        s.type = type;
        s.range = range;
        s.value = value;
        s.affected = affected;
        s.x = x;
        s.z = z;
        s.alive = true;
        s.aura = aura;
        s.need_class = need_class;
        sources.push_back(s);
        if (type < AURA_COUNT) ++type_count[type];
    }

    void unregister(uint32_t id) {
        sources.erase(std::remove_if(sources.begin(), sources.end(),
            [id](const AuraSource &s) { return s.source_id == id; }), sources.end());
        for (auto &c : type_count) c = 0;
        for (const auto &s : sources)
            if (s.type < AURA_COUNT) ++type_count[s.type];
    }

    // Combined modifier on a unit of player qowner at (qx, qz). qtmpl is its roster
    // template (hero auras skip units their 0 A.D. "affects" classes exclude) and
    // qclasses its 0 A.D. classes.
    //   AURA_HEAL: sum of hp/s. AURA_GATHER: strongest bonus.
    //   AURA_ATTACK / AURA_SPEED / AURA_HEALTH / AURA_VISION / AURA_REQUIRED_XP: strongest bonus times
    //     strongest penalty
    //     (0 when none reaches the unit).
    //   AURA_ARMOR: strongest bonus plus strongest penalty.
    float query_modifier(float qx, float qz, uint8_t qowner, AuraType type,
            uint16_t qtmpl = CIV_UNIT_TEMPLATE_NONE, const CombatClasses *qclasses = nullptr) const {
        if (type >= AURA_COUNT || type_count[type] == 0) return 0.0f;
        const uint16_t qbit = aura_player_bit(qowner);
        bool any = false;
        float sum = 0.0f, up = 0.0f, down = 0.0f;
        const bool mul = type == AURA_ATTACK || type == AURA_SPEED || type == AURA_HEALTH || type == AURA_VISION
            || type == AURA_REQUIRED_XP;
        if (mul) up = down = 1.0f;
        for (const auto &s : sources) {
            if (!s.alive || s.type != type || !(s.affected & qbit)) continue;
            if (s.aura >= 0 && !civ_aura_affects(s.aura, qtmpl)) continue;
            if (s.need_class != 0xFF && !(qclasses && qclasses->has(s.need_class))) continue;
            float dx = qx - s.x;
            float dz = qz - s.z;
            if (dx * dx + dz * dz > s.range * s.range) continue;
            any = true;
            if (type == AURA_HEAL) {
                sum += s.value;
            } else {
                if (s.value > up) up = s.value;
                if (s.value < down) down = s.value;
            }
        }
        if (!any) return 0.0f;
        if (type == AURA_HEAL) return sum;
        if (mul) return up * down;
        return up + down;
    }
};

} // namespace rtso

#endif // RTSO_AURA_MANAGER_H
