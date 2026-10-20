#ifndef RTSO_SOA_STORAGE_H
#define RTSO_SOA_STORAGE_H

#include <vector>
#include <cstdint>
#include <cstddef>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include "../../simulation/sim_types.h"
#include "aligned.h"

namespace rtso {

template <size_t CacheLine = 64>
inline void *aligned_alloc64(size_t n) {
	return aligned_malloc(CacheLine, n);
}

struct UnitSoA {
	// hot — touched every tick
	float *pos_x = nullptr;
	float *pos_z = nullptr;
	float *vel_x = nullptr;
	float *vel_z = nullptr;
	// CCmpPosition m_PrevX/m_PrevZ: each unit's position at the end of the previous
	// lockstep turn, so ranged aim predicts with the last turn's average velocity
	// (Attack.js targetVelocity), not the instantaneous one.
	float *prev_x = nullptr;
	float *prev_z = nullptr;
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
	uint16_t *tmpl = nullptr; // civ roster template (civ_unit_template), CIV_UNIT_TEMPLATE_NONE = generic class
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
	uint8_t *pack_state = nullptr; // PackState (0 A.D. Pack.js); PACK_NONE for units without Pack
	float *pack_elapsed = nullptr; // seconds into the current pack/unpack
	// 0 A.D. UnitAI order state (see UnitOrderKind): the current order, who hit the
	// unit last (UnitAI "Attacked" message, answered next tick) and the last work
	// order for Back to Work.
	uint8_t *order_kind = nullptr;
	uint8_t *order_flags = nullptr;
	uint32_t *order_target = nullptr;
	float *order_x = nullptr;
	float *order_z = nullptr;
	float *order_x2 = nullptr;
	float *order_z2 = nullptr;
	uint32_t *attacked_by = nullptr;
	uint8_t *work_kind = nullptr;
	uint32_t *work_target = nullptr;
	float *xp_partial = nullptr; // Promotion.js xp below one point (healing grants a little each tick)
	// UnitAI GATHER order.data.template for a KillBeforeGather meat supply: the animal's
	// FaunaType + 1 the unit hunts or butchers (0 = none), so it goes on only to that species.
	uint8_t *hunt_fauna = nullptr;
	float *aura_hp_mult = nullptr; // Health/Max multiplier from auras now applied to max_hp (1 = none)
	uint32_t *flee_from = nullptr; // UnitAI FLEEING order.data.target: the unit, structure or animal fled from
	float *flee_dist = nullptr; // FLEEING distanceToFlee: done once this far from flee_from
	// ResourceGatherer.StartGathering: the rate fixed when the unit starts on gather_res (0: not
	// gathering), kept until it leaves; a field's diminishing returns are those at the start.
	float *gather_rate = nullptr;
	uint32_t *gather_res = nullptr;
	float *patrol_wait = nullptr; // PATROL.CHECKINGWAYPOINT: seconds surveyed at the waypoint (0 = walking)
	// UnitAI COMBAT: the unit or animal this unit was last in attack range of (ATTACKING), 0 for none.
	// Chasing that target after it (CHASING) runs while it flees; approaching a new one walks.
	uint32_t *chase_target = nullptr;
	// Position's rotation: the way the unit faces, as a unit vector (x, z). UnitMotion turns it at the
	// template's TurnRate; (0, 1) faces +z, 0 A.D.'s rotation 0.
	float *heading_x = nullptr;
	float *heading_z = nullptr;
	ShortPathMemo *short_path = nullptr; // the short path around buildings the unit is walking
	MotionTurn *motion = nullptr; // UnitMotion's failed movements and this turn's move

	size_t cap = 0;
	size_t count = 0;

	void init(size_t initial_cap = 1024) { reserve(initial_cap); }
	void free_all() {
		#define FREE(p) if(p){ aligned_free(p); p=nullptr; }
		FREE(pos_x); FREE(pos_z); FREE(vel_x); FREE(vel_z); FREE(prev_x); FREE(prev_z);
		FREE(target_x); FREE(target_z); FREE(anchor_x); FREE(anchor_z); FREE(has_order); FREE(alive);
		FREE(hp); FREE(max_hp); FREE(id); FREE(team);
		FREE(unit_type); FREE(tmpl); FREE(action_state); FREE(res_type_carried);
		FREE(carry); FREE(carry_capacity); FREE(speed);
		FREE(attack_timer); FREE(attack_range); FREE(attack_damage); FREE(attack_cooldown);
		FREE(death_timer); FREE(target_unit_id); FREE(target_resource_id); FREE(target_building_id);
		FREE(stance); FREE(formation); FREE(kills); FREE(rank);
		FREE(status_effects); FREE(xp); FREE(waypoint_cur); FREE(waypoint_count);
		FREE(pack_state); FREE(pack_elapsed);
		FREE(order_kind); FREE(order_flags); FREE(order_target); FREE(order_x); FREE(order_z); FREE(order_x2); FREE(order_z2); FREE(attacked_by); FREE(work_kind); FREE(work_target);
		FREE(xp_partial); FREE(hunt_fauna); FREE(short_path); FREE(motion);
		FREE(aura_hp_mult); FREE(flee_from); FREE(flee_dist); FREE(gather_rate); FREE(gather_res); FREE(patrol_wait); FREE(chase_target);
		FREE(heading_x); FREE(heading_z);
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
				aligned_free(ptr);
			}
			ptr = (decltype(ptr))np;
		};
		realloc_aligned(pos_x, c * sizeof(float));
		realloc_aligned(pos_z, c * sizeof(float));
		realloc_aligned(vel_x, c * sizeof(float));
		realloc_aligned(vel_z, c * sizeof(float));
		realloc_aligned(prev_x, c * sizeof(float));
		realloc_aligned(prev_z, c * sizeof(float));
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
		realloc_aligned(tmpl, c * sizeof(uint16_t));
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
		realloc_aligned(pack_state, c * sizeof(uint8_t));
		realloc_aligned(pack_elapsed, c * sizeof(float));
		realloc_aligned(order_kind, c * sizeof(uint8_t));
		realloc_aligned(order_flags, c * sizeof(uint8_t));
		realloc_aligned(order_target, c * sizeof(uint32_t));
		realloc_aligned(order_x, c * sizeof(float));
		realloc_aligned(order_z, c * sizeof(float));
		realloc_aligned(order_x2, c * sizeof(float));
		realloc_aligned(order_z2, c * sizeof(float));
		realloc_aligned(attacked_by, c * sizeof(uint32_t));
		realloc_aligned(work_kind, c * sizeof(uint8_t));
		realloc_aligned(work_target, c * sizeof(uint32_t));
		realloc_aligned(xp_partial, c * sizeof(float));
		realloc_aligned(hunt_fauna, c * sizeof(uint8_t));
		realloc_aligned(aura_hp_mult, c * sizeof(float));
		realloc_aligned(flee_from, c * sizeof(uint32_t));
		realloc_aligned(flee_dist, c * sizeof(float));
		realloc_aligned(gather_rate, c * sizeof(float));
		realloc_aligned(gather_res, c * sizeof(uint32_t));
		realloc_aligned(patrol_wait, c * sizeof(float));
		realloc_aligned(chase_target, c * sizeof(uint32_t));
		realloc_aligned(heading_x, c * sizeof(float));
		realloc_aligned(heading_z, c * sizeof(float));
		realloc_aligned(short_path, c * sizeof(ShortPathMemo));
		realloc_aligned(motion, c * sizeof(MotionTurn));
		cap = c;
	}

	size_t push(float x, float z, uint32_t uid, uint8_t u_team = 0, uint8_t u_type = UNIT_VILLAGER,
			uint16_t u_tmpl = CIV_UNIT_TEMPLATE_NONE) {
		if (count >= cap) reserve(cap ? cap * 2 : 1024);
		size_t i = count++;
		pos_x[i] = x; pos_z[i] = z;
		vel_x[i] = vel_z[i] = 0;
		prev_x[i] = x; prev_z[i] = z;
		target_x[i] = x; target_z[i] = z;
		anchor_x[i] = x; anchor_z[i] = z;
		has_order[i] = 0; alive[i] = 1;
		id[i] = uid; team[i] = u_team;
		unit_type[i] = u_type;
		tmpl[i] = u_tmpl;
		action_state[i] = ACTION_IDLE;
		res_type_carried[i] = RES_WOOD;
		const UnitTypeInfo &info = civ_template_unit_info(u_tmpl, u_type);
		carry[i] = 0; carry_capacity[i] = info.carry_capacity;
		speed[i] = info.speed;
		death_timer[i] = 0.0f;
		target_unit_id[i] = 0; target_resource_id[i] = 0; target_building_id[i] = 0;
		stance[i] = unit_default_stance(u_type, u_tmpl);
		formation[i] = FORMATION_NONE;
		kills[i] = 0;
		rank[i] = 1;
		status_effects[i] = EFFECT_NONE;
		xp[i] = 0;
		waypoint_cur[i] = 0;
		waypoint_count[i] = 0;
		// 0 A.D. trains siege engines as their "_packed" template.
		pack_state[i] = unit_has_pack(u_tmpl, u_type) ? PACK_PACKED : PACK_NONE;
		pack_elapsed[i] = 0.0f;
		order_kind[i] = 0;
		order_flags[i] = 0;
		order_target[i] = 0;
		order_x[i] = x;
		order_z[i] = z;
		order_x2[i] = x;
		order_z2[i] = z;
		attacked_by[i] = 0;
		work_kind[i] = 0;
		work_target[i] = 0;
		xp_partial[i] = 0.0f;
		hunt_fauna[i] = 0;
		aura_hp_mult[i] = 1.0f;
		flee_from[i] = 0;
		flee_dist[i] = 0.0f;
		gather_rate[i] = 0.0f;
		gather_res[i] = 0;
		patrol_wait[i] = 0.0f;
		chase_target[i] = 0;
		heading_x[i] = 0.0f; heading_z[i] = 1.0f;
		short_path[i] = ShortPathMemo{};
		motion[i] = MotionTurn{};
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
			prev_x[idx] = prev_x[last]; prev_z[idx] = prev_z[last];
			target_x[idx] = target_x[last]; target_z[idx] = target_z[last];
			anchor_x[idx] = anchor_x[last]; anchor_z[idx] = anchor_z[last];
			has_order[idx] = has_order[last]; alive[idx] = alive[last];
			hp[idx] = hp[last]; max_hp[idx] = max_hp[last];
			id[idx] = id[last]; team[idx] = team[last];
			unit_type[idx] = unit_type[last];
			tmpl[idx] = tmpl[last];
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
			pack_state[idx] = pack_state[last];
			pack_elapsed[idx] = pack_elapsed[last];
			order_kind[idx] = order_kind[last];
			order_flags[idx] = order_flags[last];
			order_target[idx] = order_target[last];
			order_x[idx] = order_x[last];
			order_z[idx] = order_z[last];
			order_x2[idx] = order_x2[last];
			order_z2[idx] = order_z2[last];
			attacked_by[idx] = attacked_by[last];
			work_kind[idx] = work_kind[last];
			work_target[idx] = work_target[last];
			xp_partial[idx] = xp_partial[last];
			hunt_fauna[idx] = hunt_fauna[last];
			aura_hp_mult[idx] = aura_hp_mult[last];
			flee_from[idx] = flee_from[last];
			flee_dist[idx] = flee_dist[last];
			gather_rate[idx] = gather_rate[last];
			gather_res[idx] = gather_res[last];
			patrol_wait[idx] = patrol_wait[last];
			chase_target[idx] = chase_target[last];
			heading_x[idx] = heading_x[last]; heading_z[idx] = heading_z[last];
			short_path[idx] = short_path[last];
			motion[idx] = motion[last];
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
	uint8_t *subtype = nullptr; // ResourceSubtype: meat carcass, grain field, …
	uint8_t *max_gatherers = nullptr; // ResourceSupply/MaxGatherers
	int32_t *max_amount = nullptr; // ResourceSupply/Max a regrowing supply returns to (fruit), 0 = none
	uint8_t *flora = nullptr; // FloraKind: the plant a tree or fruit supply shows
	uint8_t *fauna = nullptr; // a carcass's FaunaType + 1 (its template), 0 for any other supply
	uint8_t *owner = nullptr; // a carcass's animal's owner (Health.js corpse keeps it), 0xFF for none
	// ResourceSupply Change timer (fruit Regrowth, fish Spawn): seconds to its next tick, < 0 stopped.
	float *regrow_left = nullptr;
	uint8_t *alive = nullptr;
	uint32_t *id = nullptr;

	size_t cap = 0, count = 0;
	void init(size_t c = 256) { reserve(c); }
	void free_all() {
		if (x) aligned_free(x);
		if (z) aligned_free(z);
		if (amount) aligned_free(amount);
		if (type) aligned_free(type);
		if (subtype) aligned_free(subtype);
		if (max_gatherers) aligned_free(max_gatherers);
		if (max_amount) aligned_free(max_amount);
		if (flora) aligned_free(flora);
		if (fauna) aligned_free(fauna);
		if (owner) aligned_free(owner);
		if (regrow_left) aligned_free(regrow_left);
		if (alive) aligned_free(alive);
		if (id) aligned_free(id);
		x = z = nullptr; regrow_left = nullptr; amount = max_amount = nullptr; type = subtype = max_gatherers = flora = fauna = owner = alive = nullptr; id = nullptr; cap = count = 0;
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
				aligned_free(p);
			}
			p = (decltype(p))np;
		};
		ra(x, c * sizeof(float)); ra(z, c * sizeof(float));
		ra(amount, c * sizeof(int32_t)); ra(type, c * sizeof(uint8_t));
		ra(subtype, c * sizeof(uint8_t)); ra(max_gatherers, c * sizeof(uint8_t));
		ra(max_amount, c * sizeof(int32_t)); ra(flora, c * sizeof(uint8_t)); ra(fauna, c * sizeof(uint8_t)); ra(owner, c * sizeof(uint8_t)); ra(regrow_left, c * sizeof(float));
		ra(alive, c * sizeof(uint8_t)); ra(id, c * sizeof(uint32_t));
		cap = c;
	}

	size_t push(float rx, float rz, int32_t amt, uint8_t rtype, uint32_t rid, uint8_t rsub = 0, uint8_t rmax = 8) {
		if (count >= cap) reserve(cap ? cap * 2 : 256);
		size_t i = count++;
		x[i] = rx; z[i] = rz; amount[i] = amt;
		type[i] = rtype; subtype[i] = rsub; max_gatherers[i] = rmax; max_amount[i] = 0; flora[i] = 0; fauna[i] = 0; owner[i] = 0xFF; regrow_left[i] = -1.0f; alive[i] = 1; id[i] = rid;
		return i;
	}

	void remove_at(size_t idx) {
		assert(idx < count);
		size_t last = count - 1;
		if (idx != last) {
			x[idx] = x[last]; z[idx] = z[last];
			amount[idx] = amount[last]; type[idx] = type[last];
			subtype[idx] = subtype[last]; max_gatherers[idx] = max_gatherers[last];
			max_amount[idx] = max_amount[last]; flora[idx] = flora[last]; fauna[idx] = fauna[last]; owner[idx] = owner[last]; regrow_left[idx] = regrow_left[last];
			alive[idx] = alive[last]; id[idx] = id[last];
		}
		count--;
	}
};

} // namespace rtso

#endif
