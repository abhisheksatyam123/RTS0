// Formations, ported from 0 A.D. (GPL-2.0-or-later code, CC BY-SA 3.0 data, Wildfire Games):
//   simulation/components/Formation.js   ComputeFormationOffsets, TakeClosestOffset,
//                                        GetRealOffsetPositions, DoesAngleDifferenceAllowTurning,
//                                        ShapeUpdate (column/box switch), ComputeMotionParameters
//   simulation/helpers/Commands.js       GetFormationUnitAIs, ClusterEntities, CanMoveEntsIntoFormation
//   simulation/components/UnitAI.js      CanUseFormation (UnitAI/Formations)
//   simulation/components/Player.js      GetFormations (which formations a civ may use)
//   simulation/templates/special/formations/*.xml  the 17 formation templates (formation_data.h)
//
// A formation is a controller (0 A.D.'s formation entity) that walks its own path at its
// slowest member's speed times the template SpeedMultiplier. Each member has a slot (an
// offset in the controller's frame); every tick the members are sent to their slot's world
// position, so move_units stays the one mover. Included from sim_types.h inside namespace rtso.
#ifndef RTSO_FORMATION_H
#define RTSO_FORMATION_H

enum FormationShape : uint8_t { FSHAPE_SQUARE = 0, FSHAPE_TRIANGLE = 1, FSHAPE_SPECIAL = 2 };
enum FormationSortingOrder : uint8_t { FSORT_DEFAULT = 0, FSORT_FROM_THE_SIDES = 1, FSORT_TO_THE_CENTER = 2 };

// Identity classes Formation.js sorts members by (plus each unit's Rank).
enum FormationClass : uint16_t {
	FCLASS_HERO = 1u << 0,
	FCLASS_CHAMPION = 1u << 1,
	FCLASS_CAVALRY = 1u << 2,
	FCLASS_MELEE = 1u << 3,
	FCLASS_RANGED = 1u << 4,
	FCLASS_INFANTRY = 1u << 5,
	FCLASS_ELITE = 1u << 6,
	FCLASS_ADVANCED = 1u << 7,
	FCLASS_BASIC = 1u << 8,
	FCLASS_MEDIUM = 1u << 9,
	FCLASS_LIGHT = 1u << 10,
	FCLASS_HEAVY = 1u << 11,
	FCLASS_RANKS = FCLASS_ELITE | FCLASS_ADVANCED | FCLASS_BASIC
};

struct FormationTemplate {
	const char *path;             // 0 A.D. template, e.g. "special/formations/box"
	const char *name;             // Identity/GenericName
	const char *tooltip;          // Identity/Tooltip
	const char *disabled_tooltip; // Formation/DisabledTooltip
	const char *icon;             // Identity/Icon (under art/textures/ui/session/icons/)
	uint8_t required_members;     // RequiredMemberCount
	float speed_multiplier;       // SpeedMultiplier
	uint8_t shape;                // FormationShape
	float max_turning_angle;      // MaxTurningAngle (radians)
	uint16_t sorting_classes[5];  // SortingClasses, most important first
	uint8_t sorting_class_count;
	uint8_t sorting_order;        // SortingOrder
	bool shift_rows;              // ShiftRows
	float width_mult;             // UnitSeparationWidthMultiplier
	float depth_mult;             // UnitSeparationDepthMultiplier
	float width_depth_ratio;      // WidthDepthRatio
	float sloppiness;             // Sloppiness (0 A.D. metres)
	uint8_t min_columns;          // MinColumns
	uint8_t max_columns;          // MaxColumns
	uint8_t max_rows;             // MaxRows
	float center_gap;             // CenterGap (0 A.D. metres)
	bool can_attack_as_formation; // FormationAttack/CanAttackAsFormation
};

struct FormationUnitInfo {
	uint32_t formations; // bit f = the unit may use UnitFormation f (UnitAI/Formations)
	uint16_t classes;    // FormationClass bits
};

#include "core/math/det_math.h"
#include "formation_data.h"

inline const FormationTemplate &formation_template(uint8_t f) {
	return formation_template_table()[f < FORMATION_COUNT ? f : FORMATION_NONE];
}

// 0 A.D. distances in formation templates and Formation.js are metres; RTS0's world is smaller.
// Same scale the civ rosters use for distances and speeds (extract_0ad_rosters.py SPEED_SCALE).
inline constexpr float FORMATION_METRE = 0.356f;
// Formation.js g_ColumnDistanceThreshold: farther than this, the formation marches in column.
inline constexpr float FORMATION_COLUMN_DISTANCE = 128.0f * FORMATION_METRE;
// Formation.js g_RotateDistanceThreshold (compared squared, as Formation.js does).
inline constexpr float FORMATION_ROTATE_DISTANCE = 1.0f * FORMATION_METRE;
// Commands.js GetFormationUnitAIs: formationSeparation = 60 (cluster link distance).
inline constexpr float FORMATION_CLUSTER_DISTANCE = 60.0f * FORMATION_METRE;
// template_formation.xml UnitAI/PatrolWaitTime: seconds a formation surveys each patrol end.
inline constexpr uint8_t FORMATION_PATROL_WAIT_S = 2;
// Formation.js Init: ShapeUpdate runs every 1000 ms.
inline constexpr float FORMATION_SHAPE_UPDATE_S = 1.0f;
// UnitAI FORMATIONCONTROLLER.IDLE: its timer re-forms idle members every 2000 ms.
inline constexpr float FORMATION_IDLE_REFORM_S = 2.0f;
// A member this far from its slot counts as wandered off (smaller ones finish at once in 0.28).
inline constexpr float FORMATION_IDLE_DRIFT = 2.0f * FORMATION_METRE;

// One member's place in the formation: offset (x across, y along the facing; rows behind
// the front are negative y), and its 1-based row and column (GetFormationAnimationVariant).
struct FormationSlot {
	uint32_t ent = 0;
	float x = 0.0f;
	float y = 0.0f;
	uint8_t row = 0;
	uint8_t column = 0;
	uint8_t following = 0; // walking with the controller (cleared once given another task)
	uint8_t pad_ = 0;
};
static_assert(sizeof(FormationSlot) == 16, "FormationSlot is saved raw: keep it padding-free");

// Input to compute_formation_offsets: one member.
struct FormationMember {
	uint32_t ent;
	uint16_t classes;
	float x, z;      // world position
	float width;     // footprint width (diameter)
	float depth;     // footprint depth
};

// Deterministic stand-in for Formation.js randFloat(-1, 1) (sloppiness); xorshift32.
struct FormationRng {
	uint32_t s;
	float next_signed() {
		s ^= s << 13; s ^= s >> 17; s ^= s << 5;
		return ((s & 0xFFFFFF) / 16777216.0f) * 2.0f - 1.0f;
	}
	float next_float() {
		s ^= s << 13; s ^= s >> 17; s ^= s << 5;
		return ((s & 0xFFFFFF) / 16777216.0f);
	}
};

// Formation.js GetRealOffsetPositions for one offset.
inline void formation_slot_world(float cx, float cz, float rot, float ox, float oy, float &wx, float &wz) {
	const float s = det::sin(rot), c = det::cos(rot);
	wx = cx + oy * s + ox * c;
	wz = cz + oy * c - ox * s;
}

// Formation.js DoesAngleDifferenceAllowTurning.
inline bool formation_turn_allowed(const FormationTemplate &t, float a1, float a2) {
	const float two_pi = 6.28318530718f;
	const float d = std::fmod(std::fabs(a1 - a2), two_pi);
	return d < t.max_turning_angle || d > two_pi - t.max_turning_angle;
}

// Formation.js ComputeFormationOffsets: slots for `members` around (cx, cz) facing `rot`,
// each member given the free slot nearest to it within its sorting class. `columnar`
// is the marching column used for long walks. Scatter (FSHAPE_SPECIAL) is gaia-only in
// 0 A.D. and uses randomized offsets (Formation.js:724-735).
inline std::vector<FormationSlot> compute_formation_offsets(const FormationTemplate &t, bool columnar,
		const std::vector<FormationMember> &members, float cx, float cz, float rot, FormationRng rng,
		uint8_t *max_rows_used = nullptr) {
	std::vector<FormationSlot> result;
	const int count = (int)members.size();
	if (count == 0) return result;

	// GetAvgFootprint, times the separation multipliers.
	float sep_w = 0.0f, sep_d = 0.0f;
	for (const FormationMember &m : members) { sep_w += m.width; sep_d += m.depth; }
	sep_w = sep_w / count * t.width_mult;
	sep_d = sep_d / count * t.depth_mult;

	// Sorting classes (columnar: Cavalry then Infantry), then "Unknown".
	uint16_t sorting[6];
	int n_sort = 0;
	if (columnar) {
		sorting[n_sort++] = FCLASS_CAVALRY;
		sorting[n_sort++] = FCLASS_INFANTRY;
	} else {
		for (int i = 0; i < t.sorting_class_count && i < 5; ++i) sorting[n_sort++] = t.sorting_classes[i];
	}
	const int unknown = n_sort; // index of the catch-all group
	std::vector<std::vector<int>> types(n_sort + 1);
	for (int i = 0; i < count; ++i) {
		int g = unknown;
		for (int c = 0; c < n_sort; ++c)
			if (members[i].classes & sorting[c]) { g = c; break; }
		types[g].push_back(i);
	}

	uint8_t shape = t.shape == FSHAPE_TRIANGLE ? FSHAPE_TRIANGLE : FSHAPE_SQUARE;
	bool shift_rows = t.shift_rows;
	float center_gap = t.center_gap * FORMATION_METRE;
	uint8_t sorting_order = t.sorting_order;
	int cols;
	if (columnar) {
		shape = FSHAPE_SQUARE;
		cols = std::min(count, 3);
		shift_rows = false;
		center_gap = 0.0f;
		sorting_order = FSORT_DEFAULT;
	} else {
		float depth = std::sqrt((float)count / t.width_depth_ratio);
		if (t.max_rows && depth > t.max_rows) depth = t.max_rows;
		cols = (int)std::ceil((float)count / std::ceil(depth) + (t.shift_rows ? 0.5f : 0.0f));
		if (cols < t.min_columns) cols = std::min(count, (int)t.min_columns);
		if (t.max_columns && cols > t.max_columns && (float)t.max_rows != depth) cols = t.max_columns;
	}

	struct Off { float x, y; uint8_t row, column; };
	std::vector<Off> offsets;
	offsets.reserve(count);

	// 0 A.D. Formation.js:724-735: Define special formations here (Scatter).
	if (!columnar && t.shape == FSHAPE_SPECIAL) {
		const float width = std::sqrt((float)count) * (sep_w + sep_d) * 2.5f;
		for (int i = 0; i < count; ++i) {
			offsets.push_back(Off{rng.next_float() * width, rng.next_float() * width, 1, (uint8_t)std::clamp(i + 1, 0, 255)});
		}
		if (max_rows_used) *max_rows_used = 1;
	} else {
		int r = 0;
		int left = count;
		const float slop = t.sloppiness * FORMATION_METRE;
		while (left > 0) {
		const float z = -r * sep_d;
		int side = 1;
		int n;
		if (shape == FSHAPE_SQUARE) {
			n = cols;
			if (shift_rows) n -= r % 2;
		} else {
			n = shift_rows ? r + 1 : r * 2 + 1;
		}
		if (!shift_rows && n > left) n = left;
		for (int c = 0; c < n && left > 0; ++c) {
			side *= -1;
			float x;
			if (n % 2 == 0)
				x = side * (std::floor(c / 2.0f) + 0.5f) * sep_w;
			else
				x = side * std::ceil(c / 2.0f) * sep_w;
			if (center_gap > 0.0f) {
				if (x == 0.0f) continue; // no centre position with a centre gap
				x += side * center_gap / 2.0f;
			}
			const int column = (int)std::ceil(n / 2.0f) + (int)std::ceil(c / 2.0f) * side;
			const float r1 = rng.next_signed() * slop;
			const float r2 = rng.next_signed() * slop;
			offsets.push_back(Off{x + r1, z + r2, (uint8_t)std::min(r + 1, 255), (uint8_t)std::clamp(column, 0, 255)});
			left--;
		}
		++r;
		// One column with a centre gap skips every place (Formation.js would loop forever).
		if (r > 2 * count + 4) center_gap = 0.0f;
	}
		if (max_rows_used) *max_rows_used = (uint8_t)std::min(r, 255);
	}

	// Zero the average offset so the formation is centred on its controller.
	float ax = 0.0f, ay = 0.0f;
	for (const Off &o : offsets) { ax += o.x; ay += o.y; }
	ax /= offsets.size();
	ay /= offsets.size();
	for (Off &o : offsets) { o.x -= ax; o.y -= ay; }

	// Places first in the list go to the most important classes. Formation.js sorts with a
	// boolean comparator; its intent (and V8's result) is a stable descending sort.
	if (sorting_order == FSORT_FROM_THE_SIDES)
		std::stable_sort(offsets.begin(), offsets.end(),
				[](const Off &a, const Off &b) { return std::fabs(a.x) > std::fabs(b.x); });
	else if (sorting_order == FSORT_TO_THE_CENTER)
		std::stable_sort(offsets.begin(), offsets.end(), [](const Off &a, const Off &b) {
			return std::max(std::fabs(a.x), std::fabs(a.y)) > std::max(std::fabs(b.x), std::fabs(b.y));
		});

	// Realistic place assignment: least important class first takes places from the end of
	// the list; each member takes the closest free place of its class's share (TakeClosestOffset).
	result.reserve(count);
	for (int g = unknown; g >= 0; --g) {
		const std::vector<int> &grp = types[g];
		if (grp.empty()) continue;
		std::vector<Off> used(offsets.end() - grp.size(), offsets.end());
		offsets.resize(offsets.size() - grp.size());
		std::vector<float> rx(used.size()), rz(used.size());
		for (size_t k = 0; k < used.size(); ++k) formation_slot_world(cx, cz, rot, used[k].x, used[k].y, rx[k], rz[k]);
		for (int mi : grp) {
			const FormationMember &m = members[mi];
			size_t best = 0;
			float best_d = INFINITY;
			for (size_t k = 0; k < used.size(); ++k) {
				const float dx = m.x - rx[k], dz = m.z - rz[k];
				const float d = dx * dx + dz * dz;
				if (d < best_d) { best_d = d; best = k; }
			}
			FormationSlot s;
			s.ent = m.ent;
			s.x = used[best].x;
			s.y = used[best].y;
			s.row = used[best].row;
			s.column = used[best].column;
			result.push_back(s);
			used.erase(used.begin() + best);
			rx.erase(rx.begin() + best);
			rz.erase(rz.begin() + best);
		}
	}
	return result;
}

#endif // RTSO_FORMATION_H
