#ifndef RTSO_NAV_BUILDING_STEERING_H
#define RTSO_NAV_BUILDING_STEERING_H

// Short-range steering of land units around buildings, using the VertexPathfinder port.
//
// Mirrors how 0 A.D. CCmpUnitMotion falls back to a short path (VertexPathfinder::ComputeShortPath)
// when the straight line to its next long-path waypoint is obstructed. RTS0 keeps no short-path
// state per unit: move_units calls this every tick, and a short path is only searched while the
// straight line is actually blocked, so results depend only on the current positions (safe for
// lockstep, save/load and rejoin). Units are not passed as obstructions; separation() still keeps
// units apart.
//
// Buildings are axis-aligned squares (Building::x/z/half_size). A building whose clearance-expanded
// square contains the aim point is the unit's destination (dropsite, construction site, attack
// target), so it is never treated as an obstacle, and neither is `ignore_building_id`.

#include "simulation/grid_nav.h"
#include "simulation/nav/vertex_pathfinder.h"

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

namespace detail {

// A building to steer around: standing, not ignored, within `reach` of `pos` on both axes, and not
// the destination (its square expanded by `radius` does not contain `aim`).
template <class B>
bool is_steering_obstacle(const B& b, Vec2 pos, Vec2 aim, float radius, uint32_t ignore_building_id, float reach)
{
	if (b.wrecked || b.id == ignore_building_id) return false;
	const float r = reach + b.half_size + radius;
	if (std::fabs(b.x - pos.x) > r || std::fabs(b.z - pos.y) > r) return false;
	const Vec2 half(b.half_size + radius, b.half_size + radius);
	return !PointIsInSquare(aim - Vec2(b.x, b.z), Vec2(1.0f, 0.0f), Vec2(0.0f, 1.0f), half);
}

} // namespace detail

// True when the straight line from `pos` towards `aim`, cut at `max_len`, crosses a building expanded
// by `radius`. False when the unit stands inside one (separation() pushes it out first).
// `Buildings` is a range of Simulation::Building (fields id, wrecked, x, z, half_size). No allocation.
template <class Buildings>
bool line_blocked_by_buildings(Vec2 pos, Vec2 aim, float max_len, float radius,
		const Buildings& buildings, uint32_t ignore_building_id)
{
	const Vec2 to_aim = aim - pos;
	const float len = to_aim.length();
	if (len < 1e-3f) return false;
	const Vec2 end = len > max_len ? pos + to_aim * (max_len / len) : aim;
	const float reach = std::min(len, max_len);
	const Vec2 u(1.0f, 0.0f), v(0.0f, 1.0f);
	bool blocked = false;
	for (const auto& b : buildings) {
		if (!detail::is_steering_obstacle(b, pos, aim, radius, ignore_building_id, reach)) continue;
		const Vec2 c(b.x, b.z);
		const Vec2 half(b.half_size + radius, b.half_size + radius);
		if (PointIsInSquare(pos - c, u, v, half)) return false;
		if (TestRaySquare(pos - c, end - c, u, v, half)) blocked = true;
	}
	return blocked;
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

	std::vector<ShortPathSquare> statics;
	for (const auto& b : buildings)
		if (detail::is_steering_obstacle(b, pos, aim, radius, ignore_building_id, BUILDING_STEER_CULL))
			statics.push_back(ShortPathSquare{Vec2(b.x, b.z), Vec2(1.0f, 0.0f), Vec2(0.0f, 1.0f),
					Vec2(b.half_size, b.half_size)});

	ShortPathRequest req;
	req.start = pos;
	req.clearance = radius;
	req.range = BUILDING_STEER_RANGE;
	req.goal.type = ShortPathGoal::POINT;
	req.goal.center = aim;

	// Terrain edges: water only. Building navcells are covered by `statics`, and the destination
	// building's own navcells must stay open.
	auto land = [&nav](int i, int j) { return nav.cells.inside(i, j) && nav.cells.at(i, j) != NAV_WATER; };
	const float cell = 2.0f * nav.map_half / static_cast<float>(nav.res);
	std::vector<Vec2> path;
	compute_short_path(req, statics, std::vector<ShortPathSquare>{}, land, cell,
			Vec2(-nav.map_half, -nav.map_half), nav.res, nav.res, path);
	if (path.empty()) return false;
	out = path.front();
	return true;
}

} // namespace nav
} // namespace rtso

#endif // RTSO_NAV_BUILDING_STEERING_H
