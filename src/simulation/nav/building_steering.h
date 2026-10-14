#ifndef RTSO_NAV_BUILDING_STEERING_H
#define RTSO_NAV_BUILDING_STEERING_H

// Short-range steering of land units around buildings, using the VertexPathfinder port.
//
// Mirrors how 0 A.D. CCmpUnitMotion falls back to a short path (VertexPathfinder::ComputeShortPath)
// when the straight line to its next long-path waypoint is obstructed. move_units calls this every
// tick, and a short path is only searched while the straight line is actually blocked. Like
// CCmpUnitMotion's m_ShortPath, a unit keeps heading for its short path's first waypoint
// (ShortPathMemo, saved with the unit) until it reaches it, the way there is blocked, the aim moves
// or the buildings change, instead of searching every tick. Units are obstructions only to
// search_short_path_with_units, which Simulation::avoid_idle_units calls when a step runs into a
// unit standing still; separation() pushes units apart (CCmpUnitMotionManager).
//
// Buildings are rectangles turned by their footprint axes (nav/footprint.h). A building whose
// clearance-expanded footprint contains the aim point is the unit's destination (dropsite, construction site, attack
// target), so it is never treated as an obstacle, and neither is `ignore_building_id`. Buildings
// that don't block movement (fields, building_blocks_movement) are walked across.

#include "simulation/grid_nav.h"
#include "simulation/nav/footprint.h"
#include "simulation/nav/obstacle_grid.h"
#include "simulation/nav/vertex_pathfinder.h"
#include "simulation/sim_types.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace rtso {
namespace nav {

// World units the short path may search around the unit (VertexPathfinder range). The search box
// shifts up to 3/5 of this towards a far aim point, which BUILDING_STEER_CULL accounts for.
inline constexpr float BUILDING_STEER_RANGE = 12.0f;
inline constexpr float BUILDING_STEER_CULL = BUILDING_STEER_RANGE * 1.6f;
// How far the aim may move (a chased unit, a nudged waypoint) before a kept short path is searched again.
inline constexpr float SHORT_PATH_AIM_SLACK = 1.0f;

namespace detail {

// Whether building `b`'s bounding box expanded by `radius` comes within `reach` of `pos` on both axes.
// Small enough to inline, so the loops below reject far buildings without a call.
template <class B>
bool within_reach(const B& b, Vec2 pos, float radius, float reach)
{
	const float r = reach + footprint_extent(b) + radius;
	return !(std::fabs(b.x - pos.x) > r || std::fabs(b.z - pos.y) > r);
}

// A building to steer around: standing, blocks movement, not ignored, within `reach` of `pos` on both
// axes, and not the destination (its footprint expanded by `radius` does not contain `aim`).
template <class B>
#if defined(__GNUC__)
[[gnu::always_inline]] // called per obstacle per moving unit, from every loop below
#endif
inline bool is_steering_obstacle(const B& b, Vec2 pos, Vec2 aim, float radius, uint32_t ignore_building_id, float reach)
{
	if (!within_reach(b, pos, radius, reach)) return false;
	if (b.wrecked || b.id == ignore_building_id || !building_blocks_movement(b.type)) return false;
	const Vec2 half(b.hu + radius, b.hv + radius);
	return !PointIsInSquare(aim - Vec2(b.x, b.z), footprint_u(b), footprint_v(b), half);
}

} // namespace detail

// True when the straight line from `pos` towards `aim`, cut at `max_len`, crosses a building expanded
// by `radius`. False when the unit stands inside one (separation() pushes it out first).
// `Buildings` is a range of Simulation::Building, or of anything with its fields id, type, wrecked, x, z,
// ux, uz, hu and hv. No allocation.
template <class Buildings>
bool line_blocked_by_buildings(Vec2 pos, Vec2 aim, float max_len, float radius,
		const Buildings& buildings, uint32_t ignore_building_id)
{
	const Vec2 to_aim = aim - pos;
	const float len = to_aim.length();
	if (len < 1e-3f) return false;
	const Vec2 end = len > max_len ? pos + to_aim * (max_len / len) : aim;
	const float reach = std::min(len, max_len);
	bool blocked = false;
	for (const auto& b : buildings) {
		if (!detail::within_reach(b, pos, radius, reach) ||
		    !detail::is_steering_obstacle(b, pos, aim, radius, ignore_building_id, reach)) continue;
		const Vec2 c(b.x, b.z), u = footprint_u(b), v = footprint_v(b);
		const Vec2 half(b.hu + radius, b.hv + radius);
		if (PointIsInSquare(pos - c, u, v, half)) return false;
		if (TestRaySquare(pos - c, end - c, u, v, half)) blocked = true;
	}
	return blocked;
}

// True when the segment from `pos` to `end` crosses a building expanded by `radius`. As in
// line_blocked_by_buildings, buildings whose expanded footprint contains `aim` (the destination) and
// `ignore_building_id` don't count, and standing inside one is not blocked.
template <class Buildings>
bool segment_blocked_by_buildings(Vec2 pos, Vec2 end, Vec2 aim, float radius,
		const Buildings& buildings, uint32_t ignore_building_id)
{
	const float reach = (end - pos).length();
	bool blocked = false;
	for (const auto& b : buildings) {
		if (!detail::within_reach(b, pos, radius, reach) ||
		    !detail::is_steering_obstacle(b, pos, aim, radius, ignore_building_id, reach)) continue;
		const Vec2 c(b.x, b.z), u = footprint_u(b), v = footprint_v(b);
		const Vec2 half(b.hu + radius, b.hv + radius);
		if (PointIsInSquare(pos - c, u, v, half)) return false;
		if (TestRaySquare(pos - c, end - c, u, v, half)) blocked = true;
	}
	return blocked;
}

// Searches a short path from `pos` towards `aim` around the nearby buildings and returns its first
// waypoint in `out`. False when no path was found. Searches reuse per-thread storage, so this is safe
// to call from parallel jobs.
template <class Buildings>
bool search_short_path(Vec2 pos, Vec2 aim, float radius,
		const Buildings& buildings, uint32_t ignore_building_id,
		const NavGrid& nav, Vec2& out, float range = BUILDING_STEER_RANGE)
{
	thread_local std::vector<ShortPathSquare> statics;
	thread_local std::vector<Vec2> path;
	thread_local ShortPathScratch scratch;
	static const std::vector<ShortPathSquare> no_units;
	statics.clear();
	const float cull = range * (BUILDING_STEER_CULL / BUILDING_STEER_RANGE);
	for (const auto& b : buildings)
		if (detail::within_reach(b, pos, radius, cull) &&
		    detail::is_steering_obstacle(b, pos, aim, radius, ignore_building_id, cull))
			statics.push_back(ShortPathSquare{Vec2(b.x, b.z), footprint_u(b), footprint_v(b),
					Vec2(b.hu, b.hv)});

	ShortPathRequest req;
	req.start = pos;
	req.clearance = radius;
	req.range = range;
	req.goal.type = ShortPathGoal::POINT;
	req.goal.center = aim;
	req.terrain_clearance = radius;

	// Terrain edges: water only, kept `radius` off (0 A.D. expands deep water by the class
	// clearance). Building navcells are covered by `statics`, and the destination building's own
	// navcells must stay open. Rows hold 1 for land, 0 for water and outside the grid (cells is
	// res x res).
	auto land_rows = [&nav](int i, int j, int n, uint8_t* out) {
		const Grid<uint8_t>& cells = nav.cells;
		if (j >= 0 && j < cells.h && i >= 0 && i + n <= cells.w) {
			const uint8_t* c = &cells.at(i, j);
			for (int k = 0; k < n; ++k)
				out[k] = c[k] != NAV_WATER;
		} else {
			for (int k = 0; k < n; ++k)
				out[k] = cells.inside(i + k, j) && cells.at(i + k, j) != NAV_WATER;
		}
	};
	const float cell = 2.0f * nav.map_half / static_cast<float>(nav.res);
	compute_short_path(req, statics, no_units, land_rows, cell,
			Vec2(-nav.map_half, -nav.map_half), nav.res, nav.res, scratch, path);
	if (path.empty()) return false;
	out = path.front();
	return true;
}

// search_short_path among the nearby buildings and the unit squares in `units` (axis-aligned, half
// size each unit's own clearance), within `range` of `pos`, to `goal` (a point, or a task's range
// round its target). `aim` picks the buildings that don't count, as above. A unit square is
// expanded by `unit_radius` where a building is expanded by `radius` (0 A.D. gives every unit
// obstruction the moving unit's pathfinding clearance; RTS0 keeps units further from units than
// from buildings). `out` is the first waypoint and `next` the second (the first again on a
// one-waypoint path).
template <class Buildings>
bool search_short_path_with_units(Vec2 pos, Vec2 aim, const ShortPathGoal& goal, float radius, float unit_radius,
		float range, const Buildings& buildings, uint32_t ignore_building_id,
		const std::vector<ShortPathSquare>& units, const NavGrid& nav, Vec2& out, Vec2& next)
{
	thread_local std::vector<ShortPathSquare> statics;
	thread_local std::vector<Vec2> path;
	thread_local ShortPathScratch scratch;
	statics.clear();
	const float cull = range * 1.6f;
	for (const auto& b : buildings)
		if (detail::within_reach(b, pos, radius, cull) &&
		    detail::is_steering_obstacle(b, pos, aim, radius, ignore_building_id, cull))
			statics.push_back(ShortPathSquare{Vec2(b.x, b.z), footprint_u(b), footprint_v(b),
					Vec2(b.hu, b.hv)});

	ShortPathRequest req;
	req.start = pos;
	req.clearance = radius;
	req.range = range;
	req.unit_clearance_bias = radius - unit_radius; // unit squares grow by unit_radius
	req.goal = goal;
	req.terrain_clearance = radius;
	auto land_rows = [&nav](int i, int j, int n, uint8_t* out) {
		const Grid<uint8_t>& cells = nav.cells;
		if (j >= 0 && j < cells.h && i >= 0 && i + n <= cells.w) {
			const uint8_t* c = &cells.at(i, j);
			for (int k = 0; k < n; ++k)
				out[k] = c[k] != NAV_WATER;
		} else {
			for (int k = 0; k < n; ++k)
				out[k] = cells.inside(i + k, j) && cells.at(i + k, j) != NAV_WATER;
		}
	};
	const float cell = 2.0f * nav.map_half / static_cast<float>(nav.res);
	compute_short_path(req, statics, units, land_rows, cell,
			Vec2(-nav.map_half, -nav.map_half), nav.res, nav.res, scratch, path);
	if (path.empty()) return false;
	out = path.front();
	next = path.size() > 1 ? path[1] : path[0];
	return true;
}

// If the line from `pos` towards `aim` is blocked within BUILDING_STEER_RANGE, searches a short path
// around the nearby buildings and returns its first waypoint in `out`. Returns false when the line is
// clear, the unit is inside a building or no path was found; the caller then moves straight at `aim`.
template <class Buildings>
bool steer_around_buildings(Vec2 pos, Vec2 aim, float radius,
		const Buildings& buildings, uint32_t ignore_building_id,
		const NavGrid& nav, Vec2& out)
{
	if (!line_blocked_by_buildings(pos, aim, BUILDING_STEER_RANGE, radius, buildings, ignore_building_id))
		return false;
	return search_short_path(pos, aim, radius, buildings, ignore_building_id, nav, out);
}

// The same, for a unit that keeps its short path in `memo` between ticks. `key` names the obstacles
// (buildings and water) this tick; it must not be 0. While the line to `aim` is blocked, the memo's
// waypoint is reused if it was found among the same obstacles, for an aim within
// SHORT_PATH_AIM_SLACK, and the way to it is clear; otherwise a new short path is searched and kept.
// The caller clears the memo (key = 0) once the unit reaches the waypoint.
template <class Buildings>
bool steer_around_buildings(Vec2 pos, Vec2 aim, float radius,
		const Buildings& buildings, uint32_t ignore_building_id,
		const NavGrid& nav, uint32_t key, ShortPathMemo& memo, Vec2& out)
{
	if (!line_blocked_by_buildings(pos, aim, BUILDING_STEER_RANGE, radius, buildings, ignore_building_id)) {
		memo.key = 0;
		return false;
	}
	const Vec2 aim_moved = aim - Vec2(memo.aim_x, memo.aim_z);
	if (memo.key == key && aim_moved.lengthSq() <= SHORT_PATH_AIM_SLACK * SHORT_PATH_AIM_SLACK) {
		const Vec2 w(memo.x, memo.z);
		if (!segment_blocked_by_buildings(pos, w, aim, radius, buildings, ignore_building_id)) {
			out = w;
			return true;
		}
	}
	if (!search_short_path(pos, aim, radius, buildings, ignore_building_id, nav, out)) {
		memo.key = 0;
		return false;
	}
	memo = ShortPathMemo{out.x, out.y, aim.x, aim.y, key};
	return true;
}

// The memo key steer_around_obstacles keeps for a search that found no way, and how far the unit
// moves before it searches again.
inline uint32_t no_short_path_key(uint32_t key)
{
	const uint32_t k = key ^ 0x9E3779B9u;
	return k ? k : 4u;
}
inline constexpr float NO_SHORT_PATH_MOVE = 0.25f;

// steer_around_buildings with a second, short list of small obstacles near the unit (trees,
// bushes, treasures: Simulation::gaia_obstacles_near) that only count within `small_range`. A line
// blocked by a building is searched round both lists within BUILDING_STEER_RANGE; one blocked by
// small obstacles alone within `small_range`, which is much cheaper, as 0 A.D.'s short paths
// round a tree are short.
template <class Buildings, class T>
bool steer_around_obstacles(Vec2 pos, Vec2 aim, float radius,
		const Buildings& buildings, const std::vector<T>& small, float small_range, uint32_t ignore_building_id,
		const NavGrid& nav, uint32_t key, ShortPathMemo& memo, Vec2& out)
{
	if (small.empty())
		return steer_around_buildings(pos, aim, radius, buildings, ignore_building_id, nav, key, memo, out);
	const bool by_buildings = line_blocked_by_buildings(pos, aim, BUILDING_STEER_RANGE, radius, buildings, ignore_building_id);
	if (!by_buildings && !line_blocked_by_buildings(pos, aim, small_range, radius, small, ignore_building_id)) {
		memo.key = 0;
		return false;
	}
	const ConcatRange<T> both{buildings, small};
	const Vec2 aim_moved = aim - Vec2(memo.aim_x, memo.aim_z);
	const bool same_aim = aim_moved.lengthSq() <= SHORT_PATH_AIM_SLACK * SHORT_PATH_AIM_SLACK;
	if (memo.key == key && same_aim) {
		const Vec2 w(memo.x, memo.z);
		if (!segment_blocked_by_buildings(pos, w, aim, radius, both, ignore_building_id)) {
			out = w;
			return true;
		}
	}
	// A search from about here that found no way is not repeated until the unit moves, the aim
	// moves or the obstacles change (in a wood most failed searches would fail again).
	const uint32_t failed_key = no_short_path_key(key);
	if (memo.key == failed_key && same_aim && (pos - Vec2(memo.x, memo.z)).lengthSq() < NO_SHORT_PATH_MOVE * NO_SHORT_PATH_MOVE)
		return false;
	// Blocked by small obstacles alone: the short path round one is usually just the way to a
	// corner of the first one in the way, as the visibility graph would find it. Take the corner
	// that is shortest to the aim through it and in view, on land; search only when none is.
	if (!by_buildings) {
		const Vec2 dir = aim - pos;
		const float len = dir.length();
		const Vec2 end = len > small_range ? pos + dir * (small_range / len) : aim;
		const T* first = nullptr;
		float first_t = 0.0f;
		for (const T& b : small) {
			if (!detail::is_steering_obstacle(b, pos, aim, radius, ignore_building_id, small_range)) continue;
			const Vec2 c(b.x, b.z), half(b.hu + radius, b.hv + radius);
			if (!TestRaySquare(pos - c, end - c, footprint_u(b), footprint_v(b), half)) continue;
			const float t = (c - pos).dot(dir);
			if (!first || t < first_t) {
				first = &b;
				first_t = t;
			}
		}
		if (first) {
			constexpr float CORNER_MARGIN = 0.05f; // off the clearance edge, as the vertex pathfinder's delta
			const Vec2 c(first->x, first->z), u = footprint_u(*first), v = footprint_v(*first);
			const float hu = first->hu + radius + CORNER_MARGIN, hv = first->hv + radius + CORNER_MARGIN;
			bool found = false;
			float best = 0.0f;
			for (int k = 0; k < 4; ++k) {
				const Vec2 w = c + u * ((k & 1) ? hu : -hu) + v * ((k & 2) ? hv : -hv);
				// A unit standing on a corner goes on to the next one, not back to this one.
				if ((w - pos).lengthSq() < 4.0f * CORNER_MARGIN * CORNER_MARGIN) continue;
				const auto [ci, cj] = nav.world_to_cell(w.x, w.y);
				if (!nav.cells.inside(ci, cj) || nav.cells.at(ci, cj) == NAV_WATER) continue;
				const float cost = (w - pos).length() + (aim - w).length();
				if (found && cost >= best) continue;
				if (segment_blocked_by_buildings(pos, w, aim, radius, both, ignore_building_id)) continue;
				// The corner may lie inside a neighbour's clearance (trees close together).
				bool inside = false;
				for (const T& o : small)
					if (o.id != first->id && detail::within_reach(o, w, radius, 0.0f) &&
					    PointIsInSquare(w - Vec2(o.x, o.z), footprint_u(o), footprint_v(o), Vec2(o.hu + radius, o.hv + radius))) {
						inside = true;
						break;
					}
				if (inside) continue;
				found = true;
				best = cost;
				out = w;
			}
			if (found) {
				memo = ShortPathMemo{out.x, out.y, aim.x, aim.y, key};
				return true;
			}
		}
	}
	if (!search_short_path(pos, aim, radius, both, ignore_building_id, nav, out,
			by_buildings ? BUILDING_STEER_RANGE : small_range)) {
		memo = ShortPathMemo{pos.x, pos.y, aim.x, aim.y, failed_key};
		return false;
	}
	memo = ShortPathMemo{out.x, out.y, aim.x, aim.y, key};
	return true;
}

} // namespace nav
} // namespace rtso

#endif // RTSO_NAV_BUILDING_STEERING_H
