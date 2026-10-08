#ifndef RTSO_NAV_VERTEX_PATHFINDER_H
#define RTSO_NAV_VERTEX_PATHFINDER_H

// Short-range "points of visibility" pathfinder.
//
// Port of 0 A.D. VertexPathfinder::ComputeShortPath (src/0ad/simulation2/helpers/VertexPathfinder.cpp)
// and its helpers, GPL-2.0-or-later (Copyright (C) 2025 Wildfire Games), same origin as geometry.h.
// Vendor line numbers refer to VertexPathfinder.cpp unless marked otherwise.
// Header-only, depends on geometry.h and the standard library only. Not wired into the simulation.
//
// Ported pieces:
//   quadrant tags and quadrant pruning ........ vendor 65-104, 818-831
//   EDGE_EXPAND_DELTA ......................... vendor 109
//   CheckVisibility (unaligned edges) ......... vendor 115-152
//   CheckVisibility Left/Right/Bottom/Top ..... vendor 159-269
//   AddTerrainEdges (vertices 293-340, edges 342-458) ... vendor 278-459
//   SplitAAEdges .............................. vendor 461-516
//   SquareSort / UnalignedEdgesSort ........... vendor 521-542
//   ComputeShortPath:
//     range box and goal shift ................ vendor 558-579
//     domain edges ............................ vendor 581-586
//     start and goal vertices ................. vendor 589-601
//     obstruction vertices and edges .......... vendor 603-700
//     terrain edges ........................... vendor 702-708
//     clipping of vertices inside edge squares  vendor 710-728
//     A* over the visibility graph ............ vendor 734-888
//     path reconstruction ..................... vendor 890-894
//   Not ported: VertexPathfinderDebugOverlay (vendor 63, 125-141, 909-1054), AndroidPerfSample,
//   Profiler2, and the ICmpObstructionManager queries (vendor 553, 604-609).
//
// PathGoal is not in the vendored tree (src/0ad/simulation2/helpers/PathGoal.h is missing), so
// ShortPathGoal reconstructs its semantics from the call sites in VertexPathfinder.cpp:
// NearestPointOnGoal (591, 806), DistanceToPoint (853), and the x/z/hw/hh/u/v fields.
// Inside a circle or square, the nearest goal point is the query point itself (distance 0).
//
// Deliberate deviations (each is marked DEVIATION at the call site):
//  1. Float instead of CFixedVector2D. Exact-zero and sign comparisons that the vendor makes on fixed
//     values stay exact float comparisons. Results are deterministic for one binary but are not
//     bit-identical to the fixed-point vendor.
//  2. Obstructions are plain input lists instead of ICmpObstructionManager. Statics are buildings and units
//     are unit squares. The vendor's ControlGroupMovementObstructionFilter (avoidMovingUnits, group) has no
//     equivalent here, so every obstruction passed in is used.
//  3. Terrain is a passability callback over the navcell grid, with cell size and origin in world units.
//     Vendor navcells are 1 world unit, so EDGE_EXPAND_DELTA (1/16 navcell) and the 46-navcell goal-shift
//     threshold (vendor 571) are scaled by cell_size. With cell_size = 1 and origin 0 they equal the vendor
//     values. Cells outside the grid count as impassable (the vendor relies on an impassable border).
//  4. Unit squares are shrunk by unit_clearance_bias (vendor 625, a fixed 1/2 world unit). The default 0.5f
//     is the vendor value; set it to 0 to check paths against units expanded by the full clearance.
//  5. Vertex and edge corners are written as c -/+ u*a -/+ v*b. The vendor writes the same values with
//     Dot() products (641-689). Those agree when v = (-u.y, u.x), a proper rotation, which all RTS0 callers supply;
//     the explicit form also holds for any orthonormal u, v.
//  6. Bug fix: the vendor clipping loop (711) starts at i = 2. m_EdgeSquares has no sentinel entries, so the
//     first two edge squares are never used to close vertices. The inner loop (722) correctly starts at 2
//     because vertices 0 and 1 are the start and goal. This port starts the outer loop at 0. The effect in
//     vendor is extra open vertices inside those two squares, which costs search time. Visibility still tests
//     every square, so paths stay collision-free either way.
//  7. Output is in walking order (start excluded, goal last). The vendor stores waypoints reversed (see
//     Pathfinding.h:65-72).
//  8. The return value says whether the goal was reached. The vendor always returns its best partial path.
//     A partial path is still written to out_waypoints when the function returns false.
//  9. The open list is std::priority_queue with lazy deletion in place of PriorityQueueHeap::promote (vendor
//     860 and 884). Ordering is (f, h, id), so ties resolve the same way on every run.
// 10. Vertex overflow (vendor ENSURE at 730, u16 indices) returns false instead of aborting.
// 11. The partial_sort calls (780-794) are kept. They only change how fast the visibility tests exit early,
//     because visibility is an AND over all edges and so does not depend on edge order.
// 12. DEVIATION: the perpendicular is (Y, -X), see vperp(). The vendor's visibility tests only block the
//     right crossings with that convention. geometry.h's Vec2::perp() is (-Y, X) and is not used here.
//     With (-Y, X), 427 of 3000 test paths crossed building faces.
// 13. ADDITION: ShortPathRequest::heuristic_weight (0 keeps the vendor 4/3 or 5/3). Tests use 1.0 for exact A*.
//     Also: reached is true when the start is already on the goal shape, with an empty path.

#include "simulation/nav/geometry.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <utility>
#include <vector>

namespace rtso {
namespace nav {

// Axis-aligned or rotated obstruction square (buildings, or unit shapes treated as squares).
// v must be (-u.y, u.x) (a rotation); see deviation 5.
struct ShortPathSquare {
	Vec2 center;
	Vec2 u;
	Vec2 v;
	Vec2 halfSize; // (hw, hh)
};

// Goal shape, reconstructed from VertexPathfinder call sites (see header comment).
// CIRCLE uses hw as the radius. SQUARE uses hw and hh along the unit axes u and v.
struct ShortPathGoal {
	enum Type { POINT, CIRCLE, SQUARE };
	Type type = POINT;
	Vec2 center;
	float hw = 0.0f;
	float hh = 0.0f;
	Vec2 u{1.0f, 0.0f};
	Vec2 v{0.0f, 1.0f};

	// Vendor PathGoal::NearestPointOnGoal (call sites 591, 806).
	Vec2 nearest_point(Vec2 pos) const {
		switch (type) {
		case POINT:
			return center;
		case CIRCLE: {
			const Vec2 d = pos - center;
			const float len = d.length();
			if (len <= hw) return pos;
			return center + d * (hw / len);
		}
		case SQUARE: {
			const Vec2 rel = pos - center;
			if (std::fabs(rel.dot(u)) <= hw && std::fabs(rel.dot(v)) <= hh) return pos;
			return center + NearestPointOnSquare(rel, u, v, Vec2(hw, hh));
		}
		}
		return center;
	}

	// Vendor PathGoal::DistanceToPoint (call site 853): 0 when pos is on the goal shape.
	float distance(Vec2 pos) const { return (pos - nearest_point(pos)).length(); }
};

struct ShortPathRequest {
	Vec2 start;
	float clearance = 0.0f;
	float range = 0.0f;
	ShortPathGoal goal;
	// DEVIATION 4: vendor 625 uses clearance - 1/2 for unit squares.
	float unit_clearance_bias = 0.5f;
	// ADDITION: A* heuristic weight. 0 keeps the vendor choice (5/3 or 4/3, vendor 743-748), which
	// trades optimality for speed. 1.0 gives exact A* over the visibility graph (used by the tests).
	float heuristic_weight = 0.0f;
};

namespace vp {

enum : uint8_t {
	QUADRANT_NONE = 0,
	QUADRANT_BL = 1,
	QUADRANT_TR = 2,
	QUADRANT_TL = 4,
	QUADRANT_BR = 8,
	QUADRANT_ALL = 15,
};

enum : uint8_t {
	VERTEX_UNEXPLORED = 0,
	VERTEX_OPEN = 1,
	VERTEX_CLOSED = 2,
};

// Vendor EDGE_EXPAND_DELTA = 1/16 (109). Expressed in navcells; scaled by cell_size (deviation 3).
constexpr float EDGE_EXPAND_DELTA_CELLS = 1.0f / 16.0f;
// Vendor 571: goal shift only for ranges below 46 navcells.
constexpr float GOAL_SHIFT_MAX_CELLS = 46.0f;
// Vendor 746 and 748: heuristic weight 5/3 for large edge sets, 4/3 otherwise.
constexpr float HEURISTIC_WEIGHT_LARGE = 5.0f / 3.0f;
constexpr float HEURISTIC_WEIGHT_SMALL = 4.0f / 3.0f;
constexpr uint16_t START_VERTEX_ID = 0;
constexpr uint16_t GOAL_VERTEX_ID = 1;
constexpr size_t MAX_VERTEXES = 65536; // vendor 730: indices are u16

// Vendor Vertex (VertexPathfinder.h:38-53). Quadrant bits are kept as uint8_t masks.
struct Vertex {
	Vec2 p;
	float g = 0.0f;
	float h = 0.0f;
	uint16_t pred = 0;
	uint8_t status = VERTEX_UNEXPLORED;
	uint8_t quadInward = QUADRANT_NONE;
	uint8_t quadOutward = QUADRANT_ALL;
};

// Vendor Edge (VertexPathfinder.h:57): one-sided, paths may cross from back to front only.
struct Edge {
	Vec2 p0;
	Vec2 p1;
};

// Vendor Square (VertexPathfinder.h:65): axis-aligned, p0 <= p1 componentwise.
struct Square {
	Vec2 p0;
	Vec2 p1;
};

// Vendor EdgeAA (VertexPathfinder.h:73): p0 is one end, c1 the other coordinate.
struct EdgeAA {
	Vec2 p0;
	float c1;
};

// DEVIATION 12: vendor CFixedVector2D::Perpendicular() is (Y, -X), a clockwise quarter turn. The one-sided
// visibility tests below (vendor 115-269) only block the right crossings with that convention, so it is used
// here. geometry.h's Vec2::perp() is (-Y, X), so it is not used in this file.
inline Vec2 vperp(Vec2 v) { return Vec2(v.y, -v.x); }

inline bool is_axis_aligned(Vec2 u, Vec2 v) {
	return std::fabs(u.x - 1.0f) < 1e-6f && std::fabs(u.y) < 1e-6f &&
	       std::fabs(v.x) < 1e-6f && std::fabs(v.y - 1.0f) < 1e-6f;
}

// Vendor CheckVisibility (115-152).
inline bool check_visibility(Vec2 a, Vec2 b, const std::vector<Edge>& edges) {
	const Vec2 abn = vperp(b - a);
	for (const Edge& e : edges) {
		const Vec2 d = vperp(e.p1 - e.p0);
		if ((a - e.p0).dot(d) < 0.0f) continue; // a behind the edge
		if ((b - e.p0).dot(d) > 0.0f) continue; // b in front of the edge
		if ((e.p0 - a).dot(abn) > 0.0f) continue;
		if ((e.p1 - a).dot(abn) < 0.0f) continue;
		return false;
	}
	return true;
}

// Vendor CheckVisibilityLeft (159-185): vertical edges, caller ensures a is left of them.
inline bool check_visibility_left(Vec2 a, Vec2 b, const std::vector<EdgeAA>& edges) {
	if (a.x >= b.x) return true;
	const Vec2 abn = vperp(b - a);
	for (const EdgeAA& e : edges) {
		if (b.x < e.p0.x) continue;
		if ((Vec2(e.p0.x, e.c1) - a).dot(abn) > 0.0f) continue;
		if ((Vec2(e.p0.x, e.p0.y) - a).dot(abn) < 0.0f) continue;
		return false;
	}
	return true;
}

// Vendor CheckVisibilityRight (187-213).
inline bool check_visibility_right(Vec2 a, Vec2 b, const std::vector<EdgeAA>& edges) {
	if (a.x <= b.x) return true;
	const Vec2 abn = vperp(b - a);
	for (const EdgeAA& e : edges) {
		if (b.x > e.p0.x) continue;
		if ((Vec2(e.p0.x, e.c1) - a).dot(abn) > 0.0f) continue;
		if ((Vec2(e.p0.x, e.p0.y) - a).dot(abn) < 0.0f) continue;
		return false;
	}
	return true;
}

// Vendor CheckVisibilityBottom (215-241): horizontal edges, caller ensures a is below them.
inline bool check_visibility_bottom(Vec2 a, Vec2 b, const std::vector<EdgeAA>& edges) {
	if (a.y >= b.y) return true;
	const Vec2 abn = vperp(b - a);
	for (const EdgeAA& e : edges) {
		if (b.y < e.p0.y) continue;
		if ((Vec2(e.p0.x, e.p0.y) - a).dot(abn) > 0.0f) continue;
		if ((Vec2(e.c1, e.p0.y) - a).dot(abn) < 0.0f) continue;
		return false;
	}
	return true;
}

// Vendor CheckVisibilityTop (243-269).
inline bool check_visibility_top(Vec2 a, Vec2 b, const std::vector<EdgeAA>& edges) {
	if (a.y <= b.y) return true;
	const Vec2 abn = vperp(b - a);
	for (const EdgeAA& e : edges) {
		if (b.y > e.p0.y) continue;
		if ((Vec2(e.p0.x, e.p0.y) - a).dot(abn) > 0.0f) continue;
		if ((Vec2(e.c1, e.p0.y) - a).dot(abn) < 0.0f) continue;
		return false;
	}
	return true;
}

// Vendor AddTerrainEdges (278-459). Navcells i0..i1, j0..j1 are considered. `pass` must
// return false outside the grid (DEVIATION 3). World position of navcell corner (x, y) is
// origin + (x, y) * cell.
template <class PassFn>
void add_terrain_edges(std::vector<Edge>& edges, std::vector<Vertex>& vertexes,
		int i0, int j0, int i1, int j1, PassFn& pass, int W, int H, float cell, Vec2 origin) {
	// Vendor 288-291: clamp so the outermost ring (always impassable) is never sampled.
	i0 = std::min(std::max(i0, 1), W - 2);
	j0 = std::min(std::max(j0, 1), H - 2);
	i1 = std::min(std::max(i1, 1), W - 2);
	j1 = std::min(std::max(j1, 1), H - 2);

	auto world = [&](float x, float y) { return origin + Vec2(x * cell, y * cell); };
	const float d = EDGE_EXPAND_DELTA_CELLS;

	// Vertices at the convex corners of impassable navcells (vendor 293-340).
	for (int j = j0; j <= j1; ++j) {
		for (int i = i0; i <= i1; ++i) {
			if (pass(i, j)) continue;

			if (pass(i + 1, j) && pass(i, j + 1) && pass(i + 1, j + 1)) {
				Vertex vert;
				vert.quadOutward = QUADRANT_ALL;
				vert.quadInward = QUADRANT_BL;
				vert.p = world(i + 1 + d, j + 1 + d);
				vertexes.push_back(vert);
			}
			if (pass(i - 1, j) && pass(i, j + 1) && pass(i - 1, j + 1)) {
				Vertex vert;
				vert.quadOutward = QUADRANT_ALL;
				vert.quadInward = QUADRANT_BR;
				vert.p = world(i - d, j + 1 + d);
				vertexes.push_back(vert);
			}
			if (pass(i + 1, j) && pass(i, j - 1) && pass(i + 1, j - 1)) {
				Vertex vert;
				vert.quadOutward = QUADRANT_ALL;
				vert.quadInward = QUADRANT_TL;
				vert.p = world(i + 1 + d, j - d);
				vertexes.push_back(vert);
			}
			if (pass(i - 1, j) && pass(i, j - 1) && pass(i - 1, j - 1)) {
				Vertex vert;
				vert.quadOutward = QUADRANT_ALL;
				vert.quadInward = QUADRANT_TR;
				vert.p = world(i - d, j - d);
				vertexes.push_back(vert);
			}
		}
	}

	// Edges along passability boundaries, one run per row or column (vendor 342-458).
	std::vector<int> segR, segL;
	for (int j = j0; j < j1; ++j) {
		segR.clear();
		segL.clear();
		for (int i = i0; i <= i1; ++i) {
			const bool a = pass(i, j + 1);
			const bool b = pass(i, j);
			if (a && !b) segL.push_back(i);
			if (b && !a) segR.push_back(i);
		}
		if (!segR.empty()) {
			segR.push_back(0); // sentinel
			int ia = segR[0];
			int ib = ia + 1;
			for (size_t n = 1; n < segR.size(); ++n) {
				if (segR[n] == ib) {
					++ib;
				} else {
					edges.push_back(Edge{world(ia, j + 1), world(ib, j + 1)});
					ia = segR[n];
					ib = ia + 1;
				}
			}
		}
		if (!segL.empty()) {
			segL.push_back(0);
			int ia = segL[0];
			int ib = ia + 1;
			for (size_t n = 1; n < segL.size(); ++n) {
				if (segL[n] == ib) {
					++ib;
				} else {
					edges.push_back(Edge{world(ib, j + 1), world(ia, j + 1)});
					ia = segL[n];
					ib = ia + 1;
				}
			}
		}
	}

	std::vector<int> segU, segD;
	for (int i = i0; i < i1; ++i) {
		segU.clear();
		segD.clear();
		for (int j = j0; j <= j1; ++j) {
			const bool a = pass(i + 1, j);
			const bool b = pass(i, j);
			if (a && !b) segU.push_back(j);
			if (b && !a) segD.push_back(j);
		}
		if (!segU.empty()) {
			segU.push_back(0);
			int ja = segU[0];
			int jb = ja + 1;
			for (size_t n = 1; n < segU.size(); ++n) {
				if (segU[n] == jb) {
					++jb;
				} else {
					edges.push_back(Edge{world(i + 1, ja), world(i + 1, jb)});
					ja = segU[n];
					jb = ja + 1;
				}
			}
		}
		if (!segD.empty()) {
			segD.push_back(0);
			int ja = segD[0];
			int jb = ja + 1;
			for (size_t n = 1; n < segD.size(); ++n) {
				if (segD[n] == jb) {
					++jb;
				} else {
					edges.push_back(Edge{world(i + 1, jb), world(i + 1, ja)});
					ja = segD[n];
					jb = ja + 1;
				}
			}
		}
	}
}

// Vendor SplitAAEdges (461-516): sorts the obstruction edges by which side of 'a' they can block.
inline void split_aa_edges(Vec2 a, const std::vector<Edge>& edges, const std::vector<Square>& squares,
		std::vector<Edge>& unaligned,
		std::vector<EdgeAA>& left, std::vector<EdgeAA>& right,
		std::vector<EdgeAA>& bottom, std::vector<EdgeAA>& top) {
	for (const Square& s : squares) {
		if (a.x <= s.p0.x) left.push_back(EdgeAA{s.p0, s.p1.y});
		if (a.x >= s.p1.x) right.push_back(EdgeAA{s.p1, s.p0.y});
		if (a.y <= s.p0.y) bottom.push_back(EdgeAA{s.p0, s.p1.x});
		if (a.y >= s.p1.y) top.push_back(EdgeAA{s.p1, s.p0.x});
	}
	for (const Edge& e : edges) {
		if (e.p0.x == e.p1.x) {
			if (e.p1.y < e.p0.y) {
				if (!(a.x <= e.p0.x)) continue;
				left.push_back(EdgeAA{e.p1, e.p0.y});
			} else {
				if (!(a.x >= e.p0.x)) continue;
				right.push_back(EdgeAA{e.p1, e.p0.y});
			}
		} else if (e.p0.y == e.p1.y) {
			if (e.p0.x < e.p1.x) {
				if (!(a.y <= e.p0.y)) continue;
				bottom.push_back(EdgeAA{e.p0, e.p1.x});
			} else {
				if (!(a.y >= e.p0.y)) continue;
				top.push_back(EdgeAA{e.p0, e.p1.x});
			}
		} else {
			unaligned.push_back(e);
		}
	}
}

// Vendor SquareSort (521-531) and UnalignedEdgesSort (534-544): proximity order for partial_sort.
struct SquareSort {
	Vec2 src;
	bool operator()(const Square& a, const Square& b) const {
		return (a.p0 - src).lengthSq() < (b.p0 - src).lengthSq();
	}
};

struct UnalignedEdgesSort {
	Vec2 src;
	bool operator()(const Edge& a, const Edge& b) const {
		return (a.p0 - src).lengthSq() < (b.p0 - src).lengthSq();
	}
};

// Open-list entry. Ordered by lowest f, then lowest h, then lowest id (deterministic ties).
struct OpenItem {
	float f;
	float h;
	float g;
	uint16_t id;
};

struct OpenItemGreater {
	bool operator()(const OpenItem& a, const OpenItem& b) const {
		if (a.f != b.f) return a.f > b.f;
		if (a.h != b.h) return a.h > b.h;
		return a.id > b.id;
	}
};

} // namespace vp

// Compute a short path from request.start towards request.goal over the visibility graph of
// the obstructions, expanded by request.clearance. The search is restricted to a box of
// half-width request.range around the start (shifted slightly towards the goal, vendor 563-579).
//
//   statics, units       obstruction squares (see ShortPathSquare). Units use
//                        clearance - request.unit_clearance_bias.
//   terrain_passable     callable bool(int i, int j) over the navcell grid; cells outside
//                        [0, grid_w) x [0, grid_h) count as impassable.
//   cell_size, grid_origin  navcell size and world position of navcell (0, 0)'s corner.
//
// On success (goal reached) returns true. Otherwise returns false and out_waypoints holds the
// best partial path (vendor: the explored vertex with the lowest heuristic). Waypoints are in
// walking order, start excluded, goal last. An empty result means no movement is possible.
template <class PassFn>
bool compute_short_path(const ShortPathRequest& req,
		const std::vector<ShortPathSquare>& statics,
		const std::vector<ShortPathSquare>& units,
		PassFn terrain_passable,
		float cell_size, Vec2 grid_origin, int grid_w, int grid_h,
		std::vector<Vec2>& out_waypoints) {
	using namespace vp;
	out_waypoints.clear();
	if (!(cell_size > 0.0f) || grid_w <= 0 || grid_h <= 0) return false;

	const float delta = EDGE_EXPAND_DELTA_CELLS * cell_size; // DEVIATION 3: world units

	// Search box (vendor 558-561).
	float rangeXMin = req.start.x - req.range;
	float rangeXMax = req.start.x + req.range;
	float rangeZMin = req.start.y - req.range;
	float rangeZMax = req.start.y + req.range;

	// Shift the box towards a far goal (vendor 570-579). Threshold scaled by cell_size.
	const Vec2 toGoal = req.goal.center - req.start;
	const float toGoalLength = toGoal.length();
	if (toGoalLength > 0.0f && toGoalLength >= req.range && req.range < GOAL_SHIFT_MAX_CELLS * cell_size) {
		const float shift = std::min(toGoalLength / 2.0f, req.range * 3.0f / 5.0f);
		const Vec2 d = toGoal * (shift / toGoalLength);
		rangeXMin += d.x;
		rangeXMax += d.x;
		rangeZMin += d.y;
		rangeZMax += d.y;
	}

	std::vector<Edge> edges;
	std::vector<Square> edgeSquares;
	std::vector<Vertex> vertexes;

	// Domain edges form an inside-out square so the search cannot leave the box (vendor 581-586).
	edges.push_back(Edge{Vec2(rangeXMin, rangeZMin), Vec2(rangeXMin, rangeZMax)});
	edges.push_back(Edge{Vec2(rangeXMin, rangeZMax), Vec2(rangeXMax, rangeZMax)});
	edges.push_back(Edge{Vec2(rangeXMax, rangeZMax), Vec2(rangeXMax, rangeZMin)});
	edges.push_back(Edge{Vec2(rangeXMax, rangeZMin), Vec2(rangeXMin, rangeZMin)});

	// Start vertex (vendor 590-593).
	const Vec2 posStart = req.start;
	const float hStart = (posStart - req.goal.nearest_point(posStart)).length();
	Vertex start;
	start.p = posStart;
	start.h = hStart;
	start.status = VERTEX_OPEN;
	start.quadInward = QUADRANT_NONE;
	start.quadOutward = QUADRANT_ALL;
	vertexes.push_back(start);

	// Goal vertex (vendor 599-601). Its position is moved to the nearest goal point when visited.
	Vertex goal;
	goal.p = req.goal.center;
	vertexes.push_back(goal);

	// Obstruction squares: culled to those whose bounds overlap the search box (vendor 607-609).
	const size_t staticCount = statics.size();
	const size_t allCount = staticCount + units.size();
	const float cullXMin = rangeXMin - req.clearance;
	const float cullZMin = rangeZMin - req.clearance;
	const float cullXMax = rangeXMax + req.clearance;
	const float cullZMax = rangeZMax + req.clearance;

	vertexes.reserve(vertexes.size() + allCount * 4);
	edgeSquares.reserve(allCount);

	for (size_t i = 0; i < allCount; ++i) {
		const ShortPathSquare& s = i < staticCount ? statics[i] : units[i - staticCount];
		const Vec2 bb = GetHalfBoundingBox(s.u, s.v, s.halfSize);
		if (s.center.x + bb.x < cullXMin || s.center.x - bb.x > cullXMax ||
		    s.center.y + bb.y < cullZMin || s.center.y - bb.y > cullZMax)
			continue;

		const Vec2 center = s.center;
		const Vec2 u = s.u;
		const Vec2 v = s.v;
		const float hw = s.halfSize.x;
		const float hh = s.halfSize.y;

		// Unit squares are shrunk by the bias (vendor 624-625, DEVIATION 4).
		const float pc = i >= staticCount ? req.clearance - req.unit_clearance_bias : req.clearance;

		// Vertices at the expanded corners (vendor 630-679). Corner sign pattern:
		//   hd0: (-,+) (BR inward);  hd1: (-,-) (TR inward);  (+,-) (TL inward);  (+,+) (BL inward).
		const Vec2 hd0(hw + pc + delta, hh + pc + delta);
		const Vec2 hd1(hw + pc + delta, -(hh + pc + delta));
		const bool aa = is_axis_aligned(u, v);

		auto push_vertex = [&](Vec2 p, uint8_t inward) {
			if (p.x >= rangeXMin && p.y >= rangeZMin && p.x <= rangeXMax && p.y <= rangeZMax) {
				Vertex vert;
				vert.p = p;
				if (aa) {
					vert.quadInward = inward;
					vert.quadOutward = static_cast<uint8_t>((~inward) & 0xF);
				}
				vertexes.push_back(vert);
			}
		};
		push_vertex(center - u * hd0.x + v * hd0.y, QUADRANT_BR);
		push_vertex(center - u * hd1.x + v * hd1.y, QUADRANT_TR);
		push_vertex(center + u * hd0.x - v * hd0.y, QUADRANT_TL);
		push_vertex(center + u * hd1.x - v * hd1.y, QUADRANT_BL);

		// Edges at the clearance boundary, without the vertex delta (vendor 683-699).
		const Vec2 h0(hw + pc, hh + pc);
		const Vec2 h1(hw + pc, -(hh + pc));
		const Vec2 ev0 = center - u * h0.x + v * h0.y;
		const Vec2 ev1 = center - u * h1.x + v * h1.y;
		const Vec2 ev2 = center + u * h0.x - v * h0.y;
		const Vec2 ev3 = center + u * h1.x - v * h1.y;
		if (aa) {
			edgeSquares.push_back(Square{ev1, ev3});
		} else {
			edges.push_back(Edge{ev0, ev1});
			edges.push_back(Edge{ev1, ev2});
			edges.push_back(Edge{ev2, ev3});
			edges.push_back(Edge{ev3, ev0});
		}
	}

	// Terrain edges within the search box (vendor 702-708).
	if (grid_w >= 3 && grid_h >= 3) {
		auto pass = [&](int i, int j) {
			return i >= 0 && j >= 0 && i < grid_w && j < grid_h && terrain_passable(i, j);
		};
		auto nearest_navcell = [&](float x, float z, int& i, int& j) {
			const float fi = std::floor((x - grid_origin.x) / cell_size);
			const float fj = std::floor((z - grid_origin.y) / cell_size);
			i = static_cast<int>(std::min(std::max(fi, 0.0f), static_cast<float>(grid_w - 1)));
			j = static_cast<int>(std::min(std::max(fj, 0.0f), static_cast<float>(grid_h - 1)));
		};
		int i0, j0, i1, j1;
		nearest_navcell(rangeXMin, rangeZMin, i0, j0);
		nearest_navcell(rangeXMax, rangeZMax, i1, j1);
		add_terrain_edges(edges, vertexes, i0, j0, i1, j1, pass, grid_w, grid_h, cell_size, grid_origin);
	}

	// Close vertices inside an edge square: they are trivially unreachable (vendor 710-728).
	// DEVIATION 6: vendor starts this loop at 2, skipping the first two squares.
	for (size_t i = 0; i < edgeSquares.size(); ++i) {
		const Square& sq = edgeSquares[i];
		if (start.p.x >= sq.p0.x && start.p.y >= sq.p0.y && start.p.x <= sq.p1.x && start.p.y <= sq.p1.y)
			continue;
		for (size_t j = 2; j < vertexes.size(); ++j) {
			const Vec2 p = vertexes[j].p;
			if (p.x >= sq.p0.x && p.y >= sq.p0.y && p.x <= sq.p1.x && p.y <= sq.p1.y)
				vertexes[j].status = VERTEX_CLOSED;
		}
	}

	if (vertexes.size() >= MAX_VERTEXES) return false; // DEVIATION 10 (vendor 730)

	// A* over the visibility graph (vendor 743-888). Visibility is tested lazily when a vertex is popped.
	const float heuristicWeight = req.heuristic_weight > 0.0f ? req.heuristic_weight
		: ((edges.size() > 100 || edgeSquares.size() > 100) ? HEURISTIC_WEIGHT_LARGE : HEURISTIC_WEIGHT_SMALL);

	std::priority_queue<OpenItem, std::vector<OpenItem>, OpenItemGreater> open;
	open.push(OpenItem{start.h, start.h, 0.0f, START_VERTEX_ID});

	uint16_t idBest = START_VERTEX_ID;
	float hBest = start.h;
	bool goalPopped = false;

	std::vector<Edge> edgesUnaligned;
	std::vector<EdgeAA> edgesLeft, edgesRight, edgesBottom, edgesTop;

	while (!open.empty()) {
		const OpenItem top = open.top();
		open.pop();
		// Lazy deletion stands in for PriorityQueueHeap::promote (DEVIATION 9).
		if (vertexes[top.id].status == VERTEX_CLOSED || top.g != vertexes[top.id].g)
			continue;
		vertexes[top.id].status = VERTEX_CLOSED;

		// Vendor 769-774: reaching the goal ends the search (idBest is the goal, not the best-h vertex).
		if (top.id == GOAL_VERTEX_ID) {
			idBest = GOAL_VERTEX_ID;
			goalPopped = true;
			break;
		}

		const Vec2 curP = vertexes[top.id].p;
		const float curG = vertexes[top.id].g;
		const uint8_t curOutward = vertexes[top.id].quadOutward;

		// Partial sort for early exits (vendor 780-794). Results do not depend on this order (deviation 11).
		if (edgeSquares.size() > 8)
			std::partial_sort(edgeSquares.begin(), edgeSquares.begin() + 8, edgeSquares.end(), SquareSort{curP});

		edgesUnaligned.clear();
		edgesLeft.clear();
		edgesRight.clear();
		edgesBottom.clear();
		edgesTop.clear();
		split_aa_edges(curP, edges, edgeSquares, edgesUnaligned, edgesLeft, edgesRight, edgesBottom, edgesTop);

		if (edgesUnaligned.size() > 28)
			std::partial_sort(edgesUnaligned.begin(), edgesUnaligned.begin() + 28, edgesUnaligned.end(),
				UnalignedEdgesSort{curP});

		for (size_t n = 0; n < vertexes.size(); ++n) {
			if (vertexes[n].status == VERTEX_CLOSED)
				continue;

			// The goal vertex is a moving point: visit it at the nearest goal point (vendor 802-813).
			Vec2 npos;
			if (n == GOAL_VERTEX_ID) {
				npos = req.goal.nearest_point(curP);
				// Keep the goal inside the search box (vendor 811-812). Manual min/max so that a box
				// narrower than delta cannot trigger undefined behaviour in std::clamp.
				npos.x = std::min(std::max(npos.x, rangeXMin + delta), rangeXMax - delta);
				npos.y = std::min(std::max(npos.y, rangeZMin + delta), rangeZMax - delta);
			} else {
				npos = vertexes[n].p;
			}

			// Quadrant of the new vertex as seen from the current one (vendor 818-822).
			uint8_t quad = 0;
			if (curP.x <= npos.x && curP.y <= npos.y) quad |= QUADRANT_BL;
			if (curP.x >= npos.x && curP.y >= npos.y) quad |= QUADRANT_TR;
			if (curP.x <= npos.x && curP.y >= npos.y) quad |= QUADRANT_TL;
			if (curP.x >= npos.x && curP.y <= npos.y) quad |= QUADRANT_BR;

			// Pruning by quadrant (vendor 825-831). The goal is always tried (vendor's "always head towards the goal" hack).
			if (!(curOutward & quad) && top.id != START_VERTEX_ID) {
				if (n != GOAL_VERTEX_ID)
					continue;
			}

			const bool visible =
				check_visibility_left(curP, npos, edgesLeft) &&
				check_visibility_right(curP, npos, edgesRight) &&
				check_visibility_bottom(curP, npos, edgesBottom) &&
				check_visibility_top(curP, npos, edgesTop) &&
				check_visibility(curP, npos, edgesUnaligned);
			if (!visible)
				continue;

			const float g = curG + (curP - npos).length();
			Vertex& nv = vertexes[n];
			if (nv.status == VERTEX_UNEXPLORED) {
				nv.status = VERTEX_OPEN;
				nv.g = g;
				nv.h = req.goal.distance(npos) * heuristicWeight;
				nv.pred = top.id;
				if (n == GOAL_VERTEX_ID)
					nv.p = npos; // remember the best goal position
				open.push(OpenItem{g + nv.h, nv.h, g, static_cast<uint16_t>(n)});

				// Remember the heuristically best vertex in case the goal is never reached.
				if (nv.h < hBest) {
					idBest = static_cast<uint16_t>(n);
					hBest = nv.h;
				}
			} else {
				// OPEN: keep the old entry only if it is no worse (vendor 873).
				if (g >= nv.g)
					continue;
				nv.g = g;
				nv.pred = top.id;
				if (n == GOAL_VERTEX_ID)
					nv.p = npos;
				open.push(OpenItem{g + nv.h, nv.h, g, static_cast<uint16_t>(n)});
			}
		}
	}

	// The start already on the goal shape gives an empty path and counts as reached (vendor
	// returns an empty path there too; no waypoint is needed).
	const bool reached = goalPopped || start.h == 0.0f;

	// Reconstruct backwards, then reverse into walking order (vendor 890-894, deviation 7).
	std::vector<Vec2> reversed;
	for (uint16_t id = idBest; id != START_VERTEX_ID; id = vertexes[id].pred)
		reversed.push_back(vertexes[id].p);
	out_waypoints.assign(reversed.rbegin(), reversed.rend());

	return reached;
}

} // namespace nav
} // namespace rtso

#endif // RTSO_NAV_VERTEX_PATHFINDER_H
