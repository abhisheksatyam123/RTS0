#ifndef RTSO_SOA_STORAGE_H
#define RTSO_SOA_STORAGE_H

#include <vector>
#include <cstdint>
#include <cstddef>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include "../../simulation/sim_types.h"

namespace rtso {

template <size_t CacheLine = 64>
inline void *aligned_alloc64(size_t n) {
	void *p = nullptr;
	if (posix_memalign(&p, CacheLine, n) != 0) return nullptr;
	return p;
}

struct UnitSoA {
	// hot — touched every tick
	float *pos_x = nullptr;
	float *pos_z = nullptr;
	float *vel_x = nullptr;
	float *vel_z = nullptr;
	float *target_x = nullptr;
	float *target_z = nullptr;
	float *anchor_x = nullptr;
	float *anchor_z = nullptr;
	uint8_t *has_order = nullptr;
	uint8_t *alive = nullptr;

	// combat & gather state
	float *hp = nullptr;
	float *max_hp = nullptr;
	uint32_t *id = nullptr;
	uint8_t *team = nullptr; // 0=Player, 1=Enemy
	uint8_t *unit_type = nullptr; // UnitType
	uint8_t *action_state = nullptr; // 0=Idle, 1=Move, 2=Attack, 3=Gather, 4=ReturnDropsite, 5=Build, 6=Garrisoned, 7=Dying
	uint8_t *res_type_carried = nullptr; // 0=wood, 1=gold, 2=stone, 3=food
	int32_t *carry = nullptr;
	int32_t *carry_capacity = nullptr;
	float *speed = nullptr;
	float *attack_timer = nullptr;
	float *attack_range = nullptr;
	float *attack_damage = nullptr;
	float *attack_cooldown = nullptr;
	float *death_timer = nullptr;
	uint32_t *target_unit_id = nullptr;
	uint32_t *target_resource_id = nullptr;
	uint32_t *target_building_id = nullptr;
	uint8_t *stance = nullptr; // 0=Aggressive, 1=Defensive, 2=Stand Ground, 3=Hold Ground, 4=Passive
	uint8_t *formation = nullptr; // 0=None, 1=Line, 2=Box, 3=Column, etc.
	uint16_t *kills = nullptr; // Total unit combat kills (Promotion.js)
	uint8_t *rank = nullptr; // 1=Basic, 2=Advanced (★), 3=Elite (★★)
	uint8_t *status_effects = nullptr; // Bitfield of StatusEffect
	uint16_t *xp = nullptr; // G1 Petra/Formation - veterancy XP (Promotion.js), allocation wired below
	int16_t *waypoint_cur = nullptr; // G1 Petra/Formation - current waypoint index (WaypointPath::cur)
	int16_t *waypoint_count = nullptr; // G1 Petra/Formation - waypoint count (WaypointPath::count)
	float *poison_timer = nullptr;
	float *snare_timer = nullptr;
	float *knockback_timer = nullptr;
	float *knockback_dir_x = nullptr;
	float *knockback_dir_z = nullptr;
	float *slow_timer = nullptr;
	float *bleed_timer = nullptr;
	float *burning_timer = nullptr;

	size_t cap = 0;
	size_t count = 0;

	void init(size_t initial_cap = 1024) { reserve(initial_cap); }
	void free_all() {
		#define FREE(p) if(p){ free(p); p=nullptr; }
		FREE(pos_x); FREE(pos_z); FREE(vel_x); FREE(vel_z);
		FREE(target_x); FREE(target_z); FREE(anchor_x); FREE(anchor_z); FREE(has_order); FREE(alive);
		FREE(hp); FREE(max_hp); FREE(id); FREE(team);
		FREE(unit_type); FREE(action_state); FREE(res_type_carried);
		FREE(carry); FREE(carry_capacity); FREE(speed);
		FREE(attack_timer); FREE(attack_range); FREE(attack_damage); FREE(attack_cooldown);
		FREE(death_timer); FREE(target_unit_id); FREE(target_resource_id); FREE(target_building_id);
		FREE(stance); FREE(formation); FREE(kills); FREE(rank);
		FREE(status_effects); FREE(xp); FREE(waypoint_cur); FREE(waypoint_count);
		FREE(poison_timer); FREE(snare_timer); FREE(knockback_timer);
		FREE(knockback_dir_x); FREE(knockback_dir_z);
		FREE(slow_timer); FREE(bleed_timer); FREE(burning_timer);
		#undef FREE
		cap = count = 0;
	}
	~UnitSoA() { free_all(); }

	void reserve(size_t new_cap) {
		if (new_cap <= cap) return;
		size_t old_cap = cap;
		size_t c = 1; while (c < new_cap) c <<= 1;
		auto realloc_aligned = [&](auto *&ptr, size_t bytes) {
			void *np = aligned_alloc64<64>(bytes);
			assert(np);
			if (ptr) {
				if (old_cap > 0) memcpy(np, ptr, old_cap * sizeof(*ptr));
				free(ptr);
			}
			ptr = (decltype(ptr))np;
		};
		realloc_aligned(pos_x, c * sizeof(float));
		realloc_aligned(pos_z, c * sizeof(float));
		realloc_aligned(vel_x, c * sizeof(float));
		realloc_aligned(vel_z, c * sizeof(float));
		realloc_aligned(target_x, c * sizeof(float));
		realloc_aligned(target_z, c * sizeof(float));
		realloc_aligned(anchor_x, c * sizeof(float));
		realloc_aligned(anchor_z, c * sizeof(float));
		realloc_aligned(has_order, c * sizeof(uint8_t));
		realloc_aligned(alive, c * sizeof(uint8_t));
		realloc_aligned(hp, c * sizeof(float));
		realloc_aligned(max_hp, c * sizeof(float));
		realloc_aligned(id, c * sizeof(uint32_t));
		realloc_aligned(team, c * sizeof(uint8_t));
		realloc_aligned(unit_type, c * sizeof(uint8_t));
		realloc_aligned(action_state, c * sizeof(uint8_t));
		realloc_aligned(res_type_carried, c * sizeof(uint8_t));
		realloc_aligned(carry, c * sizeof(int32_t));
		realloc_aligned(carry_capacity, c * sizeof(int32_t));
		realloc_aligned(speed, c * sizeof(float));
		realloc_aligned(attack_timer, c * sizeof(float));
		realloc_aligned(attack_range, c * sizeof(float));
		realloc_aligned(attack_damage, c * sizeof(float));
		realloc_aligned(attack_cooldown, c * sizeof(float));
		realloc_aligned(death_timer, c * sizeof(float));
		realloc_aligned(target_unit_id, c * sizeof(uint32_t));
		realloc_aligned(target_resource_id, c * sizeof(uint32_t));
		realloc_aligned(target_building_id, c * sizeof(uint32_t));
		realloc_aligned(stance, c * sizeof(uint8_t));
		realloc_aligned(formation, c * sizeof(uint8_t));
		realloc_aligned(kills, c * sizeof(uint16_t));
		realloc_aligned(rank, c * sizeof(uint8_t));
		realloc_aligned(status_effects, c * sizeof(uint8_t));
		realloc_aligned(xp, c * sizeof(uint16_t));
		realloc_aligned(waypoint_cur, c * sizeof(int16_t));
		realloc_aligned(waypoint_count, c * sizeof(int16_t));
		realloc_aligned(poison_timer, c * sizeof(float));
		realloc_aligned(snare_timer, c * sizeof(float));
		realloc_aligned(knockback_timer, c * sizeof(float));
		realloc_aligned(knockback_dir_x, c * sizeof(float));
		realloc_aligned(knockback_dir_z, c * sizeof(float));
		realloc_aligned(slow_timer, c * sizeof(float));
		realloc_aligned(bleed_timer, c * sizeof(float));
		realloc_aligned(burning_timer, c * sizeof(float));
		cap = c;
	}

	size_t push(float x, float z, uint32_t uid, uint8_t u_team = 0, uint8_t u_type = UNIT_VILLAGER) {
		if (count >= cap) reserve(cap ? cap * 2 : 1024);
		size_t i = count++;
		pos_x[i] = x; pos_z[i] = z;
		vel_x[i] = vel_z[i] = 0;
		target_x[i] = x; target_z[i] = z;
		anchor_x[i] = x; anchor_z[i] = z;
		has_order[i] = 0; alive[i] = 1;
		id[i] = uid; team[i] = u_team;
		unit_type[i] = u_type;
		action_state[i] = ACTION_IDLE;
		res_type_carried[i] = RES_WOOD;
		const UnitTypeInfo &info = unit_type_info(u_type);
		carry[i] = 0; carry_capacity[i] = info.carry_capacity;
		speed[i] = info.speed;
		death_timer[i] = 0.0f;
		target_unit_id[i] = 0; target_resource_id[i] = 0; target_building_id[i] = 0;
		stance[i] = unit_default_stance(u_type);
		formation[i] = FORMATION_NONE;
		kills[i] = 0;
		rank[i] = 1;
		status_effects[i] = EFFECT_NONE;
		xp[i] = 0;
		waypoint_cur[i] = 0;
		waypoint_count[i] = 0;
		poison_timer[i] = 0.0f;
		snare_timer[i] = 0.0f;
		knockback_timer[i] = 0.0f;
		knockback_dir_x[i] = 0.0f;
		knockback_dir_z[i] = 0.0f;
		slow_timer[i] = 0.0f;
		bleed_timer[i] = 0.0f;
		burning_timer[i] = 0.0f;
		hp[i] = max_hp[i] = info.hp;
		attack_range[i] = info.attack_range;
		attack_damage[i] = info.attack_damage;
		attack_cooldown[i] = info.attack_cooldown;
		attack_timer[i] = (float)(uid % 17) / 17.0f * info.attack_cooldown;
		return i;
	}

	void remove_at(size_t idx) {
		assert(idx < count);
		size_t last = count - 1;
		if (idx != last) {
			pos_x[idx] = pos_x[last]; pos_z[idx] = pos_z[last];
			vel_x[idx] = vel_x[last]; vel_z[idx] = vel_z[last];
			target_x[idx] = target_x[last]; target_z[idx] = target_z[last];
			anchor_x[idx] = anchor_x[last]; anchor_z[idx] = anchor_z[last];
			has_order[idx] = has_order[last]; alive[idx] = alive[last];
			hp[idx] = hp[last]; max_hp[idx] = max_hp[last];
			id[idx] = id[last]; team[idx] = team[last];
			unit_type[idx] = unit_type[last];
			action_state[idx] = action_state[last];
			res_type_carried[idx] = res_type_carried[last];
			carry[idx] = carry[last]; carry_capacity[idx] = carry_capacity[last];
			speed[idx] = speed[last];
			attack_timer[idx] = attack_timer[last]; attack_range[idx] = attack_range[last];
			attack_damage[idx] = attack_damage[last]; attack_cooldown[idx] = attack_cooldown[last];
			death_timer[idx] = death_timer[last];
			target_unit_id[idx] = target_unit_id[last]; target_resource_id[idx] = target_resource_id[last];
			target_building_id[idx] = target_building_id[last];
			stance[idx] = stance[last];
			formation[idx] = formation[last];
			kills[idx] = kills[last];
			rank[idx] = rank[last];
			status_effects[idx] = status_effects[last];
			xp[idx] = xp[last];
			waypoint_cur[idx] = waypoint_cur[last];
			waypoint_count[idx] = waypoint_count[last];
			poison_timer[idx] = poison_timer[last];
			snare_timer[idx] = snare_timer[last];
			knockback_timer[idx] = knockback_timer[last];
			knockback_dir_x[idx] = knockback_dir_x[last];
			knockback_dir_z[idx] = knockback_dir_z[last];
			slow_timer[idx] = slow_timer[last];
			bleed_timer[idx] = bleed_timer[last];
			burning_timer[idx] = burning_timer[last];
		}
		count--;
	}
	size_t size() const { return count; }
};

struct ResourceSoA {
	float *x = nullptr;
	float *z = nullptr;
	int32_t *amount = nullptr;
	uint8_t *type = nullptr; // 0=tree, 1=gold, 2=stone, 3=food
	uint8_t *alive = nullptr;
	uint32_t *id = nullptr;

	size_t cap = 0, count = 0;
	void init(size_t c = 256) { reserve(c); }
	void free_all() {
		if (x) free(x);
		if (z) free(z);
		if (amount) free(amount);
		if (type) free(type);
		if (alive) free(alive);
		if (id) free(id);
		x = z = nullptr; amount = nullptr; type = alive = nullptr; id = nullptr; cap = count = 0;
	}
	~ResourceSoA() { free_all(); }

	void reserve(size_t nc) {
		if (nc <= cap) return;
		size_t old_cap = cap;
		size_t c = 1; while (c < nc) c <<= 1;
		auto ra = [&](auto *&p, size_t b) {
			void *np = aligned_alloc64<64>(b); assert(np);
			if (p) {
				if (old_cap > 0) memcpy(np, p, old_cap * sizeof(*p));
				free(p);
			}
			p = (decltype(p))np;
		};
		ra(x, c * sizeof(float)); ra(z, c * sizeof(float));
		ra(amount, c * sizeof(int32_t)); ra(type, c * sizeof(uint8_t));
		ra(alive, c * sizeof(uint8_t)); ra(id, c * sizeof(uint32_t));
		cap = c;
	}

	size_t push(float rx, float rz, int32_t amt, uint8_t rtype, uint32_t rid) {
		if (count >= cap) reserve(cap ? cap * 2 : 256);
		size_t i = count++;
		x[i] = rx; z[i] = rz; amount[i] = amt;
		type[i] = rtype; alive[i] = 1; id[i] = rid;
		return i;
	}

	void remove_at(size_t idx) {
		assert(idx < count);
		size_t last = count - 1;
		if (idx != last) {
			x[idx] = x[last]; z[idx] = z[last];
			amount[idx] = amount[last]; type[idx] = type[last];
			alive[idx] = alive[last]; id[idx] = id[last];
		}
		count--;
	}
};

} // namespace rtso

#endif
