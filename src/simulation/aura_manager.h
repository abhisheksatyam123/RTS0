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
    AURA_HEAL_AMOUNT = 7, // Heal/Health added to what a healer restores per heal
    AURA_REQUIRED_XP = 8, // Promotion/RequiredXp multiplier
    AURA_CAPTURE_POINTS = 9, // Capturable/CapturePoints multiplier of structures (capture_range_auras)
    AURA_GARRISON_REGEN = 10, // Capturable/GarrisonRegenRate multiplier of structures
    AURA_CAPTURE_MUL = 11, // Attack/Capture/Capture multiplier of units (capture_attack_range_auras)
    AURA_CAPTURE_ADD = 12, // Attack/Capture/Capture added to units
    AURA_LOOTER = 13, // Looter/Resource of units; value = looter_range_auras() row
    AURA_NO_LOOT = 14, // Loot replaced by 0 on victims (no_loot_range_auras)
    AURA_BUILD = 15, // Builder/Rate multiplier
    AURA_GATHER_RATE = 16, // ResourceGatherer/Rates of units; value = gather_rate_range_auras() row
    AURA_RANGE_ADD = 17, // Attack/*/MaxRange added to units; value = range_add_range_auras() row
    AURA_VISION_ADD = 18, // Vision/Range metres added (0 A.D. metres; Lugius' Terror Cimbricus)
    AURA_ATTACK_RATE = 19, // Attack/*/RepeatTime multiplier (Cleopatra's Patriot, Chandragupta's Elephant Corps)
    AURA_COUNT  = 20
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

    void clear() {
        sources.clear();
        for (auto &v : of_type_) v.clear();
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
        if (type < AURA_COUNT) of_type_[type].push_back((uint32_t)sources.size());
        sources.push_back(s);
    }

    void unregister(uint32_t id) {
        sources.erase(std::remove_if(sources.begin(), sources.end(),
            [id](const AuraSource &s) { return s.source_id == id; }), sources.end());
        for (auto &v : of_type_) v.clear();
        for (uint32_t k = 0; k < sources.size(); ++k)
            if (sources[k].type < AURA_COUNT) of_type_[sources[k].type].push_back(k);
    }

    bool has(AuraType type) const { return type < AURA_COUNT && !of_type_[type].empty(); }
    bool has_negative(AuraType type) const {
        if (type >= AURA_COUNT) return false;
        for (uint32_t k : of_type_[type])
            if (sources[k].alive && sources[k].value < 0.0f) return true;
        return false;
    }

    // AURA_CAPTURE_POINTS / AURA_GARRISON_REGEN on a structure of qowner at (qx, qz): the
    // product of every capture_range_auras() aura reaching it, each aura once however many
    // sources project it (Auras.js), 1 when none does. A source's need_class 1 reaches only
    // Civil Centres.
    float query_capture(float qx, float qz, uint8_t qowner, AuraType type, bool civic_centre) const {
        if (type >= AURA_COUNT || of_type_[type].empty()) return 1.0f;
        const uint16_t qbit = aura_player_bit(qowner);
        uint32_t seen = 0; // capture_range_auras() rows already applied
        float m = 1.0f;
        for (uint32_t k : of_type_[type]) {
            const AuraSource &s = sources[k];
            if (!s.alive || !(s.affected & qbit) || s.aura < 0 || s.aura >= 32) continue;
            if (s.need_class == 1 && !civic_centre) continue;
            if (seen & (1u << s.aura)) continue;
            const float dx = qx - s.x, dz = qz - s.z;
            if (dx * dx + dz * dz > s.range * s.range) continue;
            seen |= 1u << s.aura;
            m *= s.value;
        }
        return m;
    }

    // Calls f(source) for every aura of this type reaching an entity of player qowner at
    // (qx, qz), each aura once however many sources project it (Auras.js: an aura is not
    // stackable, so two carnyxes weaken an enemy no more than one). qtmpl and qclasses filter
    // units as in query_modifier; structure: the entity is a structure, which only auras
    // naming the Structure class reach.
    template <class F>
    void for_each_reaching(float qx, float qz, uint8_t qowner, AuraType type, uint16_t qtmpl,
            const CombatClasses *qclasses, bool structure, F &&f) const {
        if (type >= AURA_COUNT || of_type_[type].empty()) return;
        const uint16_t qbit = aura_player_bit(qowner);
        int16_t seen[16];
        int n_seen = 0;
        for (uint32_t k : of_type_[type]) {
            const AuraSource &s = sources[k];
            if (!s.alive || !(s.affected & qbit)) continue;
            if (structure) {
                if (s.aura < 0 || !civ_hero_aura(s.aura).structures) continue;
            } else {
                if (s.aura >= 0 && !civ_aura_affects(s.aura, qtmpl)) continue;
                if (s.need_class != 0xFF && !(qclasses && qclasses->has(s.need_class))) continue;
            }
            const float dx = qx - s.x, dz = qz - s.z;
            if (dx * dx + dz * dz > s.range * s.range) continue;
            bool dup = false;
            for (int j = 0; j < n_seen && !dup; ++j) dup = seen[j] == s.aura;
            if (dup) continue;
            if (n_seen < 16) seen[n_seen++] = s.aura;
            f(s);
        }
    }

    // AURA_CAPTURE_MUL (product, 1 when none) or AURA_CAPTURE_ADD (sum, 0 when none) on a
    // unit, as ModifiersManager stacks modifications.
    float query_stacked(float qx, float qz, uint8_t qowner, AuraType type, uint16_t qtmpl,
            const CombatClasses *qclasses) const {
        const bool add = type == AURA_CAPTURE_ADD;
        float r = add ? 0.0f : 1.0f;
        for_each_reaching(qx, qz, qowner, type, qtmpl, qclasses, false,
            [&](const AuraSource &s) { r = add ? r + s.value : r * s.value; });
        return r;
    }

    // Combined modifier on a unit of player qowner at (qx, qz). qtmpl is its roster
    // template (hero auras skip units their 0 A.D. "affects" classes exclude) and
    // qclasses its 0 A.D. classes.
    //   AURA_HEAL: sum of hp/s. AURA_GATHER: strongest bonus plus strongest penalty.
    //   AURA_ATTACK / AURA_SPEED / AURA_HEALTH / AURA_VISION / AURA_REQUIRED_XP / AURA_ATTACK_RATE: the
    //     product of every aura reaching the unit (0 when none does).
    //   AURA_ARMOR / AURA_HEAL_AMOUNT / AURA_VISION_ADD: the sum of every aura reaching it.
    //   As Auras.js and ModifiersManager do, each aura counts once however many sources
    //   project it (a second Marcellus or carnyx adds nothing) and different auras stack
    //   (a hero's attack aura with the Iberian monument's).
    //   AURA_BUILD: the product of every aura reaching the unit, a stackable one (the Han
    //     minister's) once per source and any other once (0 when none reaches the unit).
    // structure: the query is for a structure, which only auras naming the Structure
    // class reach (CivHeroAura::structures).
    float query_modifier(float qx, float qz, uint8_t qowner, AuraType type,
            uint16_t qtmpl = CIV_UNIT_TEMPLATE_NONE, const CombatClasses *qclasses = nullptr,
            bool structure = false) const {
        if (type >= AURA_COUNT || of_type_[type].empty()) return 0.0f;
        const uint16_t qbit = aura_player_bit(qowner);
        bool any = false;
        float sum = 0.0f, up = 0.0f, down = 0.0f, product = 1.0f;
        int16_t once[16]; // auras already counted (non-stackable AURA_BUILD ones for AURA_BUILD)
        int once_n = 0;
        const bool mul = type == AURA_ATTACK || type == AURA_SPEED || type == AURA_HEALTH || type == AURA_VISION
            || type == AURA_REQUIRED_XP || type == AURA_ATTACK_RATE;
        // Only this type's sources, in the order they were registered.
        for (uint32_t k : of_type_[type]) {
            const AuraSource &s = sources[k];
            if (!s.alive || !(s.affected & qbit)) continue;
            if (structure) {
                if (s.aura < 0 || !civ_hero_aura(s.aura).structures) continue;
            } else {
                if (s.aura >= 0 && !civ_aura_affects(s.aura, qtmpl)) continue;
                if (s.need_class != 0xFF && !(qclasses && qclasses->has(s.need_class))) continue;
            }
            float dx = qx - s.x;
            float dz = qz - s.z;
            if (dx * dx + dz * dz > s.range * s.range) continue;
            any = true;
            if (type == AURA_HEAL) {
                // Non-stackable, like every aura here: each aura once, however many sources.
                const int16_t key = s.aura >= 0 ? s.aura : (int16_t)(-1 - (int16_t)s.need_class);
                bool seen = false;
                for (int k = 0; k < once_n; ++k) seen |= once[k] == key;
                if (seen) continue;
                if (once_n < 16) once[once_n++] = key;
                sum += s.value;
            } else if (type == AURA_BUILD) {
                const int row = builder_rate_aura_index(s.aura);
                if (row < 0 || !builder_rate_range_auras()[row].stackable) {
                    bool seen = false;
                    for (int k = 0; k < once_n; ++k) seen |= once[k] == s.aura;
                    if (seen) continue;
                    if (once_n < 16) once[once_n++] = s.aura;
                }
                product *= s.value;
            } else if (type == AURA_GATHER) {
                if (s.value > up) up = s.value;
                if (s.value < down) down = s.value;
            } else {
                // A roster aura by its index; an unlisted one (the carnyx) by its class filter.
                const int16_t key = s.aura >= 0 ? s.aura : (int16_t)(-1 - (int16_t)s.need_class);
                bool seen = false;
                for (int k = 0; k < once_n; ++k) seen |= once[k] == key;
                if (seen) continue;
                if (once_n < 16) once[once_n++] = key;
                product *= s.value;
                sum += s.value;
            }
        }
        if (!any) return 0.0f;
        if (type == AURA_HEAL) return sum;
        if (type == AURA_BUILD || mul) return product;
        if (type == AURA_GATHER) return up + down;
        return sum;
    }

private:
    std::vector<uint32_t> of_type_[AURA_COUNT]; // indices into sources, per AuraType
};

} // namespace rtso

#endif // RTSO_AURA_MANAGER_H
