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
#include <cstring>
#include <vector>

namespace rtso {
namespace nav {

// World units the short path may search around the unit (VertexPathfinder range). The search box
// shifts up to 3/5 of this towards a far aim point, which BUILDING_STEER_CULL accounts for.
inline constexpr float BUILDING_STEER_RANGE = 12.0f;
inline constexpr float BUILDING_STEER_CULL = BUILDING_STEER_RANGE * 1.6f;
// How far the aim may move (a chased unit, a nudged waypoint) before a kept short path is searched again.
inline constexpr float SHORT_PATH_AIM_SLACK = 1.0f;
// How far inside a unit's clearance a line may pass a steep tile without counting as blocked.
inline constexpr float TERRAIN_EDGE_SLACK = 0.01f;

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

// Whether the segment from `a` to `b` comes within `radius` of a terrain tile too steep to stand on
// (NavGrid::near_steep_tile), tested every quarter tile along it (NavGrid::segment_near_steep_tile,
// which long paths test their legs with too). RTS0's long paths keep off steep tiles where they
// pass near one (NavGrid::find_path_on plans those on the tiles), but a walk with no long path, or a
// push or a crowd off its path, can still run into a cliff edge or a rock inside a navcell, and takes
// a short path round it. The start doesn't count.
inline bool segment_near_steep(const NavGrid& nav, Vec2 a, Vec2 b, float radius)
{
	return nav.segment_near_steep_tile(a.x, a.y, b.x, b.y, radius);
}

// Whether the segment from `a` to `b` comes within `radius` of a deep-water navcell, where
// Simulation::land_step_barred refuses a land unit's steps, tested every tenth of a unit along it.
// A start that close already doesn't count: the unit may walk on from there.
inline bool segment_near_deep_water(const NavGrid& nav, Vec2 a, Vec2 b, float radius)
{
	auto water = [&](float x0, float z0, float x1, float z1) { return nav.water_cell_in(x0, z0, x1, z1, true); };
	if (!water(std::min(a.x, b.x) - radius, std::min(a.y, b.y) - radius, std::max(a.x, b.x) + radius,
	           std::max(a.y, b.y) + radius) ||
	    water(a.x - radius, a.y - radius, a.x + radius, a.y + radius))
		return false;
	const Vec2 d = b - a;
	const int n = std::max(1, static_cast<int>(std::ceil(d.length() / 0.1f)));
	for (int k = 1; k <= n; ++k) {
		const Vec2 p = a + d * (static_cast<float>(k) / static_cast<float>(n));
		if (water(p.x - radius, p.y - radius, p.x + radius, p.y + radius)) return true;
	}
	return false;
}

// How steep ground meets the line from `pos` towards `aim` within BUILDING_STEER_RANGE, a hair
// inside the clearance, so a unit stopped at a cliff's margin by land_step_barred, or a short-path
// waypoint at an expanded corner, doesn't count as on it: not at all, only in the margin round an
// aim that lies within it (a tree or a mine up a slope), which the line enters and doesn't leave,
// or across the way.
enum TerrainLine { TERRAIN_LINE_CLEAR, TERRAIN_LINE_AIM_MARGIN, TERRAIN_LINE_BLOCKED };
inline TerrainLine terrain_line(const NavGrid& nav, Vec2 pos, Vec2 aim, float radius)
{
	const float r = radius - TERRAIN_EDGE_SLACK;
	const Vec2 to = aim - pos;
	const float len = to.length();
	if (len > BUILDING_STEER_RANGE)
		return segment_near_steep(nav, pos, pos + to * (BUILDING_STEER_RANGE / len), r) ? TERRAIN_LINE_BLOCKED : TERRAIN_LINE_CLEAR;
	if (!segment_near_steep(nav, pos, aim, r)) return TERRAIN_LINE_CLEAR;
	if (!nav.near_steep_tile(aim.x, aim.y, r)) return TERRAIN_LINE_BLOCKED;
	// Back from the aim, on segment_near_steep's samples, to where the line enters its margin.
	const float step = 0.5f * nav.map_half / static_cast<float>(nav.steep_tile_res());
	const int n = std::max(1, static_cast<int>(std::ceil(len / step)));
	int k = n - 1;
	while (k >= 1) {
		const Vec2 p = pos + to * (static_cast<float>(k) / static_cast<float>(n));
		if (!nav.near_steep_tile(p.x, p.y, r)) break;
		--k;
	}
	if (k < 1) return TERRAIN_LINE_AIM_MARGIN;
	return segment_near_steep(nav, pos, pos + to * (static_cast<float>(k) / static_cast<float>(n)), r)
			? TERRAIN_LINE_BLOCKED : TERRAIN_LINE_AIM_MARGIN;
}

inline bool line_blocked_by_terrain(const NavGrid& nav, Vec2 pos, Vec2 aim, float radius)
{
	return terrain_line(nav, pos, aim, radius) == TERRAIN_LINE_BLOCKED;
}

// A unit whose line to `aim` meets steep ground only in the aim's own margin walks straight at it:
// a short path search would find no corner nearer the aim than the way in, and the unit would
// search again at every step. Within a quarter tile of the margin, where land_step_barred stops it,
// it notes the search that finds no way from there (no_terrain_path_key's note, which
// motion_feedback ends the walk on soon); otherwise its memo is cleared.
inline void note_aim_margin(const NavGrid& nav, Vec2 pos, Vec2 aim, float radius, uint32_t note, ShortPathMemo& memo)
{
	const Vec2 to = aim - pos;
	const float len = to.length();
	const float step = 0.5f * nav.map_half / static_cast<float>(nav.steep_tile_res());
	const Vec2 p = len > step ? pos + to * (step / len) : aim;
	if (nav.near_steep_tile(p.x, p.y, radius - TERRAIN_EDGE_SLACK))
		memo = ShortPathMemo{pos.x, pos.y, aim.x, aim.y, note};
	else
		memo.key = 0;
}

// The map's passable edge for the short path of a unit with clearance `clearance`, as
// past_map_edge draws it for its steps: the circle of radius NavGrid::edge_r on a circular map, else
// the square frame MAP_EDGE_DIST in from each side, less the clearance. 0.28 closes both for every
// class (CCmpPathfinder.cpp:700-744: the circle at 710-729, the square frame at 730-744) and grows
// them by the class clearance rounded up to whole 1 m navcells (749-761); this keeps the exact
// clearance. compute_short_path closes the vertices past it (ShortPathRequest::edge).
inline void set_map_edge(ShortPathRequest& req, const NavGrid& nav, float clearance)
{
	req.edge_circular = nav.edge_r > 0.0f;
	req.edge = (req.edge_circular ? nav.edge_r : nav.map_half - MAP_EDGE_DIST) - clearance;
}

// Calls fn(rows, cell, origin, w, h) with the short path's terrain grid: rows(i, j, n, out) fills
// out[0..n) with 1 for ground a land unit may cross and 0 for deep water, steep ground and outside
// the grid. Building navcells are left to the statics, as the destination building's own navcells
// must stay open. Where no steep terrain tile lies within reach of a search from `pos` over `range`
// (the box compute_short_path may shift towards its goal, grown by the clearance) the grid is the
// navcells (deep water alone), four times fewer cells; otherwise it is the heightmap's tiles, a
// navcell's deep water and the tile's own slope (NavGrid::near_steep_tile).
template <class Fn>
auto with_terrain_rows(const NavGrid& nav, Vec2 pos, float range, float clearance, Fn&& fn)
{
	const Vec2 origin(-nav.map_half, -nav.map_half);
	const Grid<uint8_t>& cells = nav.cells;
	const float reach = range * 1.6f + clearance + 4.0f * nav.map_half / static_cast<float>(std::max(nav.res, 1));
	if (nav.steep_tile_in(pos.x - reach, pos.y - reach, pos.x + reach, pos.y + reach)) {
		const int tw = nav.steep_tile_res();
		const float tile = 2.0f * nav.map_half / static_cast<float>(tw);
		const float per = static_cast<float>(nav.res) / static_cast<float>(tw); // navcells a tile
		auto rows = [&nav, &cells, tw, per](int i, int j, int n, uint8_t* out) {
			const int cy = static_cast<int>((static_cast<float>(j) + 0.5f) * per);
			for (int k = 0; k < n; ++k) {
				const int ti = i + k;
				out[k] = j >= 0 && j < tw && ti >= 0 && ti < tw && !nav.tile_steep(ti, j) &&
				         cells.at(static_cast<int>((static_cast<float>(ti) + 0.5f) * per), cy) != NAV_WATER;
			}
		};
		return fn(rows, tile, origin, tw, tw);
	}
	auto rows = [&cells](int i, int j, int n, uint8_t* out) {
		if (j >= 0 && j < cells.h && i >= 0 && i + n <= cells.w) {
			const uint8_t* c = &cells.at(i, j);
			for (int k = 0; k < n; ++k)
				out[k] = c[k] != NAV_WATER;
		} else {
			for (int k = 0; k < n; ++k)
				out[k] = cells.inside(i + k, j) && cells.at(i + k, j) != NAV_WATER;
		}
	};
	return fn(rows, 2.0f * nav.map_half / static_cast<float>(nav.res), origin, nav.res, nav.res);
}

// The bounding box of the segment from `pos` to `end`. A building whose footprint, grown by
// `radius` along its own axes, misses it can neither cross the segment nor hold `pos`, so the loops
// below skip it before their exact tests, with the same result. Grown along turned axes, the
// footprint's bounding box grows by up to radius * sqrt(2); 1.5 and a hair keep the test loose.
struct SegmentBox {
	float x0, z0, x1, z1, radius;
	SegmentBox(Vec2 pos, Vec2 end, float r)
		: x0(std::min(pos.x, end.x)), z0(std::min(pos.y, end.y)), x1(std::max(pos.x, end.x)), z1(std::max(pos.y, end.y)),
		  radius(1.5f * r + 0.01f) {}
	template <class B>
	bool may_touch(const B& b) const
	{
		const float e = footprint_extent(b) + radius;
		return b.x + e >= x0 && b.x - e <= x1 && b.z + e >= z0 && b.z - e <= z1;
	}
};

// How building `b` meets the line from `pos` to `end` (cut from the line to `aim` at `reach`), as
// line_blocked_by_buildings tests each one.
enum LineHit { LINE_MISSES, LINE_CROSSES, LINE_HOLDS_POS };
template <class B>
#if defined(__GNUC__)
[[gnu::always_inline]]
#endif
inline int line_hit(const B& b, Vec2 pos, Vec2 end, Vec2 aim, float radius, uint32_t ignore_building_id, float reach,
		const SegmentBox& box)
{
	if (!box.may_touch(b) || !within_reach(b, pos, radius, reach) ||
	    !is_steering_obstacle(b, pos, aim, radius, ignore_building_id, reach)) return LINE_MISSES;
	const Vec2 c(b.x, b.z), u = footprint_u(b), v = footprint_v(b);
	const Vec2 half(b.hu + radius, b.hv + radius);
	if (PointIsInSquare(pos - c, u, v, half)) return LINE_HOLDS_POS;
	return TestRaySquare(pos - c, end - c, u, v, half) ? LINE_CROSSES : LINE_MISSES;
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
	const detail::SegmentBox box(pos, end, radius);
	bool blocked = false;
	for (const auto& b : buildings) {
		const int hit = detail::line_hit(b, pos, end, aim, radius, ignore_building_id, reach, box);
		if (hit == detail::LINE_HOLDS_POS) return false;
		if (hit == detail::LINE_CROSSES) blocked = true;
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
	const detail::SegmentBox box(pos, end, radius);
	bool blocked = false;
	for (const auto& b : buildings) {
		if (!box.may_touch(b) || !detail::within_reach(b, pos, radius, reach) ||
		    !detail::is_steering_obstacle(b, pos, aim, radius, ignore_building_id, reach)) continue;
		const Vec2 c(b.x, b.z), u = footprint_u(b), v = footprint_v(b);
		const Vec2 half(b.hu + radius, b.hv + radius);
		if (PointIsInSquare(pos - c, u, v, half)) return false;
		if (TestRaySquare(pos - c, end - c, u, v, half)) blocked = true;
	}
	return blocked;
}

// A short path's first waypoints (at most SHORT_PATH_KEPT_MAX), as a search hands them on.
struct KeptPath {
	int n = 0;
	Vec2 p[SHORT_PATH_KEPT_MAX];
};

// Searches a short path from `pos` towards `aim` around the nearby buildings and returns its first
// waypoint in `out`, and its first waypoints in `kept` when given. False when no path was found.
// Searches reuse per-thread storage, so this is safe to call from parallel jobs.
template <class Buildings>
bool search_short_path(Vec2 pos, Vec2 aim, float radius,
		const Buildings& buildings, uint32_t ignore_building_id,
		const NavGrid& nav, Vec2& out, float range = BUILDING_STEER_RANGE, KeptPath* kept = nullptr)
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
	detail::set_map_edge(req, nav, radius);

	// Terrain edges: deep water and steep ground, kept `radius` off (0 A.D. expands impassable
	// terrain by the class clearance).
	detail::with_terrain_rows(nav, pos, range, radius, [&](auto& rows, float cell, Vec2 origin, int w, int h) {
		return compute_short_path(req, statics, no_units, rows, cell, origin, w, h, scratch, path);
	});
	if (kept) {
		kept->n = static_cast<int>(std::min<size_t>(path.size(), SHORT_PATH_KEPT_MAX));
		for (int k = 0; k < kept->n; ++k) kept->p[k] = path[static_cast<size_t>(k)];
	}
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
// one-waypoint path). `square_delta` is the squares' corner offset (ShortPathRequest::square_delta).
// `reached`, when given, says whether the path reaches the goal: a search that cannot reach it returns
// the path to the point nearest it, and that counts as found too.
template <class Buildings>
bool search_short_path_with_units(Vec2 pos, Vec2 aim, const ShortPathGoal& goal, float radius, float unit_radius,
		float range, const Buildings& buildings, uint32_t ignore_building_id,
		const std::vector<ShortPathSquare>& units, const NavGrid& nav, Vec2& out, Vec2& next,
		float square_delta = -1.0f, bool* reached = nullptr)
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
	req.square_delta = square_delta;
	detail::set_map_edge(req, nav, radius);
	const bool got = detail::with_terrain_rows(nav, pos, range, radius, [&](auto& rows, float cell, Vec2 origin, int w, int h) {
		return compute_short_path(req, statics, units, rows, cell, origin, w, h, scratch, path);
	});
	if (reached) *reached = got;
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
	if (!line_blocked_by_buildings(pos, aim, BUILDING_STEER_RANGE, radius, buildings, ignore_building_id) &&
	    !detail::line_blocked_by_terrain(nav, pos, aim, radius))
		return false;
	return search_short_path(pos, aim, radius, buildings, ignore_building_id, nav, out);
}

namespace detail {
inline uint32_t float_bits(float f)
{
	uint32_t w;
	std::memcpy(&w, &f, sizeof(w));
	return w;
}

// What a search's small obstacles are, every field it reads; never 0.
template <class T>
uint64_t small_obstacles_hash(const std::vector<T>& small)
{
	uint64_t h = 1469598103934665603ull;
	auto mix = [&h](uint32_t w) { h = (h ^ w) * 1099511628211ull; };
	for (const T& o : small) {
		mix(o.id); mix(float_bits(o.x)); mix(float_bits(o.z)); mix(float_bits(o.ux)); mix(float_bits(o.uz));
		mix(float_bits(o.hu)); mix(float_bits(o.hv)); mix(static_cast<uint32_t>(o.type)); mix(o.wrecked ? 1u : 0u);
	}
	return h ? h : 1u;
}

// search_short_path among `obstacles` (the buildings `key` names, and the small ones `small` hashes),
// answered from `memo` when it holds the same search; its first waypoints go to `kept` when given.
template <class Obstacles>
bool search_short_path_memo(ShortPathSearchMemo* memo, uint32_t key, uint64_t small, Vec2 pos, Vec2 aim, float radius,
		const Obstacles& obstacles, uint32_t ignore_building_id, const NavGrid& nav, Vec2& out, float range,
		KeptPath* kept = nullptr)
{
	ShortPathSearchMemo m;
	m.pos_x = float_bits(pos.x); m.pos_z = float_bits(pos.y);
	m.aim_x = float_bits(aim.x); m.aim_z = float_bits(aim.y);
	m.radius = float_bits(radius); m.range = float_bits(range);
	m.key = key; m.ignore = ignore_building_id; m.terrain = nav.terrain_version(); m.small = small;
	if (memo && memo->valid && memo->pos_x == m.pos_x && memo->pos_z == m.pos_z && memo->aim_x == m.aim_x &&
	    memo->aim_z == m.aim_z && memo->radius == m.radius && memo->range == m.range && memo->key == m.key &&
	    memo->ignore == m.ignore && memo->terrain == m.terrain && memo->small == m.small) {
		if (memo->found) out = Vec2(memo->out_x, memo->out_z);
		if (kept) {
			kept->n = memo->path_n;
			for (int k = 0; k < kept->n; ++k) kept->p[k] = Vec2(memo->path_x[k], memo->path_z[k]);
		}
		return memo->found;
	}
	KeptPath whole;
	m.found = search_short_path(pos, aim, radius, obstacles, ignore_building_id, nav, out, range, &whole);
	if (kept) *kept = whole;
	if (memo) {
		m.out_x = m.found ? out.x : 0.0f;
		m.out_z = m.found ? out.y : 0.0f;
		m.path_n = static_cast<uint8_t>(whole.n);
		for (int k = 0; k < whole.n; ++k) {
			m.path_x[k] = whole.p[k].x;
			m.path_z[k] = whole.p[k].y;
		}
		m.valid = true;
		*memo = m;
	}
	return m.found;
}
} // namespace detail

// The memo key kept for a search that found no way among the obstacles `key` names, and how far
// the unit moves before it searches again.
inline uint32_t no_short_path_key(uint32_t key)
{
	const uint32_t k = key ^ 0x9E3779B9u;
	return k ? k : 4u;
}
inline constexpr float NO_SHORT_PATH_MOVE = 0.25f;
// The same for a search that found no way where steep ground blocks the line (a resource up a
// cliff or in a pocket the long path can't reach, a point beyond a ridge): motion_feedback fails
// such a walk soon, as UnitMotion's short-path hack does.
inline uint32_t no_terrain_path_key(uint32_t key)
{
	const uint32_t k = key ^ 0x7F4A7C15u;
	return k ? k : 5u;
}

// The memo key of a short path of several waypoints that a unit keeps and follows: 0.28's
// CCmpUnitMotion::PerformMove (CCmpUnitMotion.h:1238-1250, 1299-1326) takes its path's waypoints one
// by one, moving on to the next as it reaches each, and TryGoingStraightToTarget
// (CCmpUnitMotion.h:1503-1507) leaves m_ShortPath alone while it has waypoints. RTS0 saves only the
// memo (ShortPathMemo), so the memo holds where the unit searched from and the aim it searched for,
// this key names the obstacles the search was among (the buildings `key` names, the small ones
// `small` hashes, the building not steered round) and the unit's clearance and the range, and its low
// bits (KEPT_PATH_INDEX_MASK) are the waypoint the unit heads for. The same search, run again from the
// memo (and answered from ShortPathSearchMemo), gives the same waypoints, in a game loaded or joined
// too. The terrain is left out: NavGrid's terrain_version counts changes in this process and isn't
// saved, and the search runs on the terrain as it stands, the way to each waypoint checked against it
// as the unit goes. Its top four bits are those of `key` with KEPT_PATH_TAG's flipped, which no other
// memo key has (is_kept_path_key): `key` itself, its no-way notes (no_short_path_key,
// no_terrain_path_key) and the keys of paths round idle units (Simulation::idle_path_key,
// idle_corner_key, idle_stuck_key, idle_relaxed_path_key, idle_relaxed_corner_key) flip other bits,
// and their stand-ins for 0 are below 16. Never 0.
inline constexpr uint32_t KEPT_PATH_INDEX_MASK = 0xFu;
inline constexpr uint32_t KEPT_PATH_TAG = 0xA0000000u;
static_assert(SHORT_PATH_KEPT_MAX <= static_cast<int>(KEPT_PATH_INDEX_MASK) + 1, "a kept waypoint's index fits the key");
inline uint32_t kept_path_key(uint32_t key, uint64_t small, uint32_t ignore_building_id, float radius, float range)
{
	uint64_t h = 1469598103934665603ull;
	auto mix = [&h](uint32_t w) { h = (h ^ w) * 1099511628211ull; };
	mix(key ^ 0x3C6EF372u); mix(static_cast<uint32_t>(small)); mix(static_cast<uint32_t>(small >> 32));
	mix(ignore_building_id); mix(detail::float_bits(radius)); mix(detail::float_bits(range));
	const uint32_t k = (static_cast<uint32_t>(h ^ (h >> 32)) & 0x0FFFFFF0u) | ((key ^ KEPT_PATH_TAG) & 0xF0000000u);
	return k ? k : KEPT_PATH_INDEX_MASK + 1;
}

// Whether `k` is the memo key of a short path kept among the obstacles `key` names (kept_path_key).
inline bool is_kept_path_key(uint32_t k, uint32_t key)
{
	return k != 0 && ((k ^ key) & 0xF0000000u) == KEPT_PATH_TAG;
}

// What steer_around_obstacles hands move_units besides the waypoint: whether that waypoint is one of
// a kept short path with more after it, and the next one. PerformMove moves a unit whose step reaches
// a waypoint onto it, drops it and spends the rest of the step heading for the next
// (CCmpUnitMotion.h:1299-1326).
struct SteerNext {
	bool more = false;
	Vec2 next;
};

namespace detail {
// The memo kept for the short path `kept` searched from `pos` towards `aim`: its first waypoint and
// `key` for a path of one waypoint or one the unit doesn't `keep` (steer_around_obstacles says which),
// else the path itself (kept_path_key), the unit heading for its first waypoint.
inline ShortPathMemo kept_memo(Vec2 pos, Vec2 aim, const KeptPath& kept, Vec2 out, bool keep, uint32_t key, uint64_t small,
		uint32_t ignore_building_id, float radius, float range, SteerNext* next)
{
	if (!keep || kept.n < 2) return ShortPathMemo{out.x, out.y, aim.x, aim.y, key};
	if (next) {
		next->more = true;
		next->next = kept.p[1];
	}
	return ShortPathMemo{pos.x, pos.y, aim.x, aim.y, kept_path_key(key, small, ignore_building_id, radius, range)};
}

// Whether `memo` notes a search from within NO_SHORT_PATH_MOVE of `pos` towards an aim within
// SHORT_PATH_AIM_SLACK of `aim` that found no way among the obstacles `key` names.
inline bool no_way_noted(const ShortPathMemo& memo, Vec2 pos, Vec2 aim, uint32_t key)
{
	return (memo.key == no_short_path_key(key) || memo.key == no_terrain_path_key(key)) &&
	       (aim - Vec2(memo.aim_x, memo.aim_z)).lengthSq() <= SHORT_PATH_AIM_SLACK * SHORT_PATH_AIM_SLACK &&
	       (pos - Vec2(memo.x, memo.z)).lengthSq() < NO_SHORT_PATH_MOVE * NO_SHORT_PATH_MOVE;
}

// steer_around_buildings once the line to `aim` is known to be blocked by a building, or by steep
// ground; `by_terrain` when steep ground meets the line, whose failed search gets the terrain note.
// A unit that `follow`s its short path keeps one that steep ground meets (steer_around_obstacles).
template <class Buildings>
bool steer_round_buildings(Vec2 pos, Vec2 aim, float radius,
		const Buildings& buildings, uint32_t ignore_building_id,
		const NavGrid& nav, bool by_terrain, uint32_t key, ShortPathMemo& memo, Vec2& out,
		ShortPathSearchMemo* search = nullptr, bool follow = false, SteerNext* next = nullptr)
{
	if (no_way_noted(memo, pos, aim, key)) return false;
	const Vec2 aim_moved = aim - Vec2(memo.aim_x, memo.aim_z);
	if (memo.key == key && aim_moved.lengthSq() <= SHORT_PATH_AIM_SLACK * SHORT_PATH_AIM_SLACK) {
		const Vec2 w(memo.x, memo.z);
		if (!segment_blocked_by_buildings(pos, w, aim, radius, buildings, ignore_building_id) &&
		    !segment_near_steep(nav, pos, w, radius - TERRAIN_EDGE_SLACK)) {
			out = w;
			return true;
		}
	}
	KeptPath kept;
	if (!search_short_path_memo(search, key, 0, pos, aim, radius, buildings, ignore_building_id, nav, out,
			BUILDING_STEER_RANGE, &kept)) {
		memo = ShortPathMemo{pos.x, pos.y, aim.x, aim.y, by_terrain ? no_terrain_path_key(key) : no_short_path_key(key)};
		return false;
	}
	memo = kept_memo(pos, aim, kept, out, follow && by_terrain, key, 0, ignore_building_id, radius, BUILDING_STEER_RANGE, next);
	return true;
}

// The waypoint the unit heads for on the short path its memo keeps (kept_path_key), and the one
// after it in `next`: the search the memo names, run again from where the unit searched towards the
// aim it searched for, among the buildings and the small obstacles `fetch` finds round that point
// (answered from `search` while it holds that search). False when the memo keeps no such path, the
// obstacles have changed since, or the unit has used up its waypoints.
template <class Buildings, class T, class Fetch>
bool kept_waypoint(const ShortPathMemo& memo, uint32_t key, float radius, float small_range, uint32_t ignore_building_id,
		const Buildings& buildings, Fetch& fetch, const NavGrid& nav, ShortPathSearchMemo* search, Vec2& out, SteerNext* next)
{
	const Vec2 from(memo.x, memo.z), aim(memo.aim_x, memo.aim_z);
	thread_local std::vector<T> near_from;
	near_from.clear();
	fetch(from, near_from);
	const uint32_t base = memo.key & ~KEPT_PATH_INDEX_MASK;
	const int k = static_cast<int>(memo.key & KEPT_PATH_INDEX_MASK);
	KeptPath kept;
	Vec2 first;
	if (near_from.empty()) {
		if (base != kept_path_key(key, 0, ignore_building_id, radius, BUILDING_STEER_RANGE)) return false;
		search_short_path_memo(search, key, 0, from, aim, radius, buildings, ignore_building_id, nav, first, BUILDING_STEER_RANGE,
				&kept);
	} else {
		const uint64_t small = small_obstacles_hash(near_from);
		float range = BUILDING_STEER_RANGE;
		if (base != kept_path_key(key, small, ignore_building_id, radius, range)) {
			range = small_range;
			if (base != kept_path_key(key, small, ignore_building_id, radius, range)) return false;
		}
		const ConcatRange<T> both{buildings, near_from};
		search_short_path_memo(search, key, small, from, aim, radius, both, ignore_building_id, nav, first, range, &kept);
	}
	if (k >= kept.n) return false;
	out = kept.p[k];
	if (next && k + 1 < kept.n) {
		next->more = true;
		next->next = kept.p[k + 1];
	}
	return true;
}

} // namespace detail

// The same, for a unit that keeps its short path in `memo` between ticks. `key` names the obstacles
// (buildings, water and steep ground) this tick; it must not be 0. While the line to `aim` is blocked, the memo's
// waypoint is reused if it was found among the same obstacles, for an aim within
// SHORT_PATH_AIM_SLACK, and the way to it is clear; otherwise a new short path is searched and kept.
// The caller clears the memo (key = 0) once the unit reaches the waypoint. A search that found no
// way is noted too (no_short_path_key, no_terrain_path_key) and not run again until the unit or the
// aim moves: the unit walks straight on and the building or the cliff's margin stops it.
template <class Buildings>
bool steer_around_buildings(Vec2 pos, Vec2 aim, float radius,
		const Buildings& buildings, uint32_t ignore_building_id,
		const NavGrid& nav, uint32_t key, ShortPathMemo& memo, Vec2& out)
{
	const detail::TerrainLine tl = detail::terrain_line(nav, pos, aim, radius);
	if (tl != detail::TERRAIN_LINE_BLOCKED &&
	    !line_blocked_by_buildings(pos, aim, BUILDING_STEER_RANGE, radius, buildings, ignore_building_id)) {
		if (tl == detail::TERRAIN_LINE_AIM_MARGIN) detail::note_aim_margin(nav, pos, aim, radius, no_terrain_path_key(key), memo);
		else memo.key = 0;
		return false;
	}
	return detail::steer_round_buildings(pos, aim, radius, buildings, ignore_building_id, nav,
			tl != detail::TERRAIN_LINE_CLEAR, key, memo, out);
}

// steer_around_buildings with a second, short list of small obstacles near the unit (trees,
// bushes, treasures: Simulation::gaia_obstacles_near) that only count within `small_range`. A line
// blocked by a building is searched round both lists within BUILDING_STEER_RANGE; one blocked by
// small obstacles alone within `small_range`, which is much cheaper, as 0 A.D.'s short paths
// round a tree are short. The list is gathered (`fetch(pos, small)`) only when it may matter: when a
// building blocks the line, or `small_maybe` says a small obstacle may (one crosses the line within
// `small_range` or holds the unit), or the unit has a short path to check the way along; the result
// is the same as with the whole list each time. `fetch(centre, out)` adds the small obstacles round
// `centre` to `out`. `by_buildings` is line_blocked_by_buildings over `buildings` within
// BUILDING_STEER_RANGE, which the caller may have a quicker way to find.
//
// A unit that `follow`s its short path (all but a formation's members) keeps it whole, as 0.28 keeps
// its paths, where trees or steep ground shape it: steep ground meets the line to the aim
// (terrain_line), or small obstacles round the unit count in the search. 0.28's long path goes round
// those itself: CCmpPathfinder rasterizes each tree's obstruction square, grown by the clearance, onto
// 1 m navcells with the terrain's passability (CCmpPathfinder.cpp:583, CCmpObstructionManager.cpp:
// 1166-1185), and PerformMove heads for the waypoints of the unit's paths one by one
// (CCmpUnitMotion.h:1238-1250, 1299-1326), while TryGoingStraightToTarget leaves a short path alone
// as long as it has waypoints (save for a formation member, whose offset moves are short and walk
// straight; CCmpUnitMotion.h:1503-1507). RTS0's 8 m navcells leave the trees out of its long paths
// (Simulation::gaia_obstacles_ block movement only), and steep ground finer than a navcell, so this
// short path stands in for that part of 0.28's: the unit heads for its waypoints while the way there
// is clear, even once the line to the aim has cleared, and follows it to its end (kept_path_key),
// `next` telling move_units the waypoint after this one. Searched again from each corner instead, a
// path round a cliff or a wood could turn back at a corner the line to the aim clears from, or a
// search from the next corner send the unit back to the first, and the unit would walk between them
// for good, every turn a free move. Round buildings on open ground, which RTS0's long paths go round
// as well (Simulation::rebuild_nav_grid rasterizes them), the unit keeps only the first waypoint and
// decides again from it, as before. `final_aim`: `aim` is the walk's end, with no long-path waypoint
// after it.
template <class Buildings, class T, class Fetch>
bool steer_around_obstacles(Vec2 pos, Vec2 aim, float radius,
		const Buildings& buildings, bool by_buildings, std::vector<T>& small, bool small_maybe, Fetch&& fetch, float small_range,
		uint32_t ignore_building_id, const NavGrid& nav, uint32_t key, ShortPathMemo& memo, Vec2& out,
		ShortPathSearchMemo* search = nullptr, bool follow = false, SteerNext* next = nullptr, bool final_aim = true)
{
	const detail::TerrainLine tl = detail::terrain_line(nav, pos, aim, radius);
	// A search that finds no way where steep ground meets the line is noted as such.
	const bool by_terrain = tl != detail::TERRAIN_LINE_CLEAR;
	by_buildings = by_buildings || tl == detail::TERRAIN_LINE_BLOCKED; // a line steep ground blocks is searched as one a building blocks
	auto straight = [&] {
		if (tl == detail::TERRAIN_LINE_AIM_MARGIN) detail::note_aim_margin(nav, pos, aim, radius, no_terrain_path_key(key), memo);
		else memo.key = 0;
		return false;
	};
	bool fetched = false;
	auto fetch_small = [&] {
		if (fetched) return;
		fetched = true;
		fetch(pos, small);
	};
	const Vec2 aim_moved = aim - Vec2(memo.aim_x, memo.aim_z);
	const bool same_aim = aim_moved.lengthSq() <= SHORT_PATH_AIM_SLACK * SHORT_PATH_AIM_SLACK;
	if (follow && same_aim && is_kept_path_key(memo.key, key)) {
		Vec2 w;
		SteerNext after;
		// A kept path whose waypoints are used up is done, and the unit decides again at once, as
		// PerformMove goes on along the long path when the short one runs out (CCmpUnitMotion.h:
		// 1238-1250); so does one searched among obstacles that have changed since, which can't be
		// found again.
		if (detail::kept_waypoint<Buildings, T>(memo, key, radius, small_range, ignore_building_id, buildings, fetch, nav, search,
				w, &after)) {
			// The way to the waypoint must be clear of buildings, the small obstacles round the unit,
			// steep ground and deep water.
			fetch_small();
			const ConcatRange<T> both{buildings, small};
			if (!segment_blocked_by_buildings(pos, w, aim, radius, both, ignore_building_id) &&
			    !detail::segment_near_steep(nav, pos, w, radius - TERRAIN_EDGE_SLACK) &&
			    !detail::segment_near_deep_water(nav, pos, w, radius - TERRAIN_EDGE_SLACK)) {
				out = w;
				if (next) *next = after;
				return true;
			}
		}
		// Done, or something now stands in the way: the unit decides again from here at once, as the
		// memo of a single waypoint does below, where 0.28's HandleObstructedMove asks for a new short
		// path (CCmpUnitMotion.h:1436-1441).
		memo.key = 0;
	}
	if (!by_buildings && !small_maybe) return straight();
	fetch_small();
	if (small.empty()) {
		if (!by_buildings) return straight();
		return detail::steer_round_buildings(pos, aim, radius, buildings, ignore_building_id, nav, by_terrain, key, memo, out,
				search, follow, next);
	}
	if (!by_buildings && !line_blocked_by_buildings(pos, aim, small_range, radius, small, ignore_building_id)) return straight();
	const ConcatRange<T> both{buildings, small};
	if (memo.key == key && same_aim) {
		const Vec2 w(memo.x, memo.z);
		if (!segment_blocked_by_buildings(pos, w, aim, radius, both, ignore_building_id) &&
		    !detail::segment_near_steep(nav, pos, w, radius - TERRAIN_EDGE_SLACK) &&
		    !detail::segment_near_deep_water(nav, pos, w, radius - TERRAIN_EDGE_SLACK)) {
			out = w;
			return true;
		}
	}
	// A search from about here that found no way is not repeated until the unit moves, the aim
	// moves or the obstacles change (in a wood most failed searches would fail again).
	if (detail::no_way_noted(memo, pos, aim, key)) return false;
	// Blocked by small obstacles alone: the short path round one is usually just the way to a
	// corner of the first one in the way, as the visibility graph would find it. Take the corner
	// that is shortest to the aim through it and in view, on land; search only when none is.
	// (A line steep ground blocks is searched in full, as one a building blocks.)
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
				// Nor into or across a cliff's or deep water's clearance, where the unit's steps are refused
				// (a corner by a shore, off a tree in a gap narrower than the unit, would hold it there).
				if (detail::segment_near_steep(nav, pos, w, radius - TERRAIN_EDGE_SLACK) ||
				    detail::segment_near_deep_water(nav, pos, w, radius - TERRAIN_EDGE_SLACK)) continue;
				// The corner may lie inside a neighbour's clearance (trees close together).
				bool inside = false;
				for (const T& o : small)
					if (o.id != first->id && detail::within_reach(o, w, radius, 0.0f) &&
					    PointIsInSquare(w - Vec2(o.x, o.z), footprint_u(o), footprint_v(o), Vec2(o.hu + radius, o.hv + radius))) {
						inside = true;
						break;
					}
				if (inside) continue;
				const Vec2 on = aim - w;
				const float on_len = on.length();
				const Vec2 on_end = on_len > small_range ? w + on * (small_range / on_len) : aim;
				// A corner into a gap between trees that others close behind is a dead end the next
				// search would turn back from: the way on from it must be open too.
				{
					bool closed = false;
					for (const T& o : small) {
						if (o.id == first->id || !detail::is_steering_obstacle(o, w, aim, radius, ignore_building_id, small_range)) continue;
						const Vec2 oc(o.x, o.z);
						if (TestRaySquare(w - oc, on_end - oc, footprint_u(o), footprint_v(o), Vec2(o.hu + radius, o.hv + radius))) {
							closed = true;
							break;
						}
					}
					if (closed) continue;
				}
				// CCmpUnitMotion::RejectFartherPaths (CCmpUnitMotion.h:904-914): a path whose end (its
				// m_Waypoints.front(), as 0.28 keeps waypoints last first) is no nearer the goal than the unit
				// is rejected, as it would take the unit between two places for good without ever failing a
				// move. PathResult does so for a short path only when no long waypoint is left
				// (pathedTowardsGoal, CCmpUnitMotion.h:932-933, 944-951), as a sub-goal may be farther than
				// the unit is: here, only when `aim` is the walk's end. Where the way on from the corner still
				// crosses this obstacle, the unit decides again at the corner, so the corner is the whole of
				// this path and its end: such a corner no nearer the aim than the unit is rejected, and with
				// none left the full search below decides (a way round of several corners, or none: the unit
				// then walks on into the tree, and motion_feedback counts the turns it gets nowhere as failed
				// movements).
				if (final_aim) {
					const Vec2 fc(first->x, first->z);
					if ((aim - w).lengthSq() >= dir.lengthSq() &&
					    TestRaySquare(w - fc, on_end - fc, u, v, Vec2(first->hu + radius, first->hv + radius))) continue;
				}
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
	const uint64_t small_hash = detail::small_obstacles_hash(small);
	const float range = by_buildings ? BUILDING_STEER_RANGE : small_range;
	KeptPath kept;
	if (!detail::search_short_path_memo(search, key, small_hash, pos, aim, radius, both, ignore_building_id, nav, out, range,
			&kept)) {
		memo = ShortPathMemo{pos.x, pos.y, aim.x, aim.y, by_terrain ? no_terrain_path_key(key) : no_short_path_key(key)};
		return false;
	}
	memo = detail::kept_memo(pos, aim, kept, out, follow, key, small_hash, ignore_building_id, radius, range, next);
	return true;
}

} // namespace nav
} // namespace rtso

#endif // RTSO_NAV_BUILDING_STEERING_H
