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
//   UnalignedEdgesSort ........................ vendor 534-544 (SquareSort is not used, deviation 11)
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
//  9. The open list is a binary heap (std::push_heap and std::pop_heap, as in std::priority_queue) with lazy
//     deletion in place of PriorityQueueHeap::promote (vendor 860 and 884). Ordering is (f, h, id), so ties
//     resolve the same way on every run.
// 10. Vertex overflow (vendor ENSURE at 730, u16 indices) returns false instead of aborting.
// 11. The partial_sort of edge squares (780-781) is dropped and the one of unaligned edges (792-794) kept.
//     Both only change how fast the visibility tests exit early, because visibility is an AND over all
//     edges and so does not depend on edge order. Sorting the squares cost more than it saved: RTS0's
//     building steering spent 2% of a match's ticks on it, and test_vertex_pathfinder's timings did not
//     change with up to 150 obstructions.
// 12. DEVIATION: the perpendicular is (Y, -X), see vperp(). The vendor's visibility tests only block the
//     right crossings with that convention. geometry.h's Vec2::perp() is (-Y, X) and is not used here.
//     With (-Y, X), 427 of 3000 test paths crossed building faces.
// 13. ADDITION: ShortPathRequest::heuristic_weight (0 keeps the vendor 4/3 or 5/3). Tests use 1.0 for exact A*.
//     Also: reached is true when the start is already on the goal shape, with an empty path.
// 14. ADDITION: ShortPathRequest::terrain_clearance. 0 A.D. expands impassable terrain by each class's
//     Clearance in whole 1 m navcells before the search (CCmpPathfinder ExpandImpassableCells), so its
//     terrain edges already keep a unit that far off. RTS0's navcells are 8 m, so the terrain vertices
//     and edges move out by this many world units instead: each edge along its normal into the passable
//     side, grown by as much at both ends, and each vertex diagonally off its corner.
// 15. ADDITION: ShortPathRequest::square_delta sets the obstruction squares' corner offset in world units.
//     0.28's EDGE_EXPAND_DELTA is 1/16 of an entity_pos_t, a metre (0.28 VertexPathfinder.cpp:107, 623-624),
//     and its navcells are 1 m, so it is 1/16 m for squares and navcells alike. Deviation 3 scales it by
//     RTS0's navcell, eight times wider on flat ground (2-unit cells, 0.125 units against 1/64). A
//     caller that needs an offset as small as 0.28's passes it here (Simulation::avoid_idle_units'
//     second search passes 1/16 m at the map's scale, IDLE_SQUARE_DELTA).
// 16. ADDITION: ShortPathRequest::edge closes every vertex past the map's passable edge less the unit's
//     clearance: the circle on a circular map, the square frame on a square one. 0.28 closes the
//     navcells past the edge for every class (CCmpPathfinder.cpp:700-744: the circle at 710-729, the
//     square frame at 730-744) and grows them by each class's clearance, rounded up to whole 1 m
//     navcells (749-761, ExpandImpassableCells on m_TerrainOnlyGrid), so its terrain edges keep a short
//     path on the map (VertexPathfinder.cpp:695-701). RTS0 keeps the exact clearance off the edge.
//     RTS0's terrain rows are 8 m navcells or heightmap tiles, too coarse to draw the edge with:
//     closing whole navcells would keep units up to 11 m short of it. The ground inside the edge is
//     convex, so a path whose corners all lie inside it stays inside it between them, and no edge
//     needs drawing. The start and the goal are never closed.

#include "simulation/nav/geometry.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
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
	// ADDITION (deviation 14): how far terrain edges keep the unit off impassable navcells, in world units.
	float terrain_clearance = 0.0f;
	// ADDITION (deviation 15): how far the obstruction squares' vertices sit off their corners, in world
	// units. Negative keeps EDGE_EXPAND_DELTA_CELLS navcells (deviation 3).
	float square_delta = -1.0f;
	// ADDITION (deviation 16): the map's passable edge less the unit's clearance, in world units: the
	// circle's radius when edge_circular, else the square frame's half size. 0 or less: no edge.
	float edge = 0.0f;
	bool edge_circular = false;
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

// Working storage for add_terrain_edges.
struct TerrainEdgeScratch {
	std::vector<int> segA, segB;
	std::vector<uint8_t> pass;  // pass() over the box and a one-navcell ring around it, by rows
	std::vector<uint8_t> passT; // the same by columns
};

// add_terrain_edges()'s loops on bit rows, for a box (with its ring) at most 64 navcells wide and
// high: bit bi of a row is navcell i0 - 1 + bi of `passBox`'s row. Pushes the same vertices and
// edges in the same order, testing a whole row or column of navcells at once.
inline void add_terrain_edges_bits(std::vector<Edge>& edges, std::vector<Vertex>& vertexes,
		int i0, int j0, int i1, int j1, const uint8_t* passBox, int bw, int bh, float cell, Vec2 origin,
		float e) {
	uint64_t rows[64], cols[64] = {}; // rows[bj] bit bi, cols[bi] bit bj: pass(i0 - 1 + bi, j0 - 1 + bj)
	for (int bj = 0; bj < bh; ++bj) {
		const uint8_t* r = passBox + static_cast<size_t>(bj) * bw;
		uint64_t m = 0;
		for (int bi = 0; bi < bw; ++bi)
			m |= static_cast<uint64_t>(r[bi] != 0) << bi;
		rows[bj] = m;
		for (; m; m &= m - 1)
			cols[std::countr_zero(m)] |= uint64_t{1} << bj;
	}
	const uint64_t innerI = ((uint64_t{1} << (i1 - i0 + 1)) - 1) << 1; // navcells i0..i1
	const uint64_t innerJ = ((uint64_t{1} << (j1 - j0 + 1)) - 1) << 1; // navcells j0..j1
	auto world = [&](float x, float y) { return origin + Vec2(x * cell, y * cell); };
	const float d = EDGE_EXPAND_DELTA_CELLS + e;

	// Vertices at the convex corners of impassable navcells (vendor 293-340). In a row mask,
	// m >> 1 holds navcell i + 1 at bit i, and m << 1 navcell i - 1.
	for (int j = j0; j <= j1; ++j) {
		const uint64_t cur = rows[j - j0 + 1], up = rows[j - j0 + 2], down = rows[j - j0];
		const uint64_t blocked = ~cur & innerI;
		if (!blocked) continue;
		const uint64_t bl = blocked & (cur >> 1) & up & (up >> 1);
		const uint64_t br = blocked & (cur << 1) & up & (up << 1);
		const uint64_t tl = blocked & (cur >> 1) & down & (down >> 1);
		const uint64_t tr = blocked & (cur << 1) & down & (down << 1);
		for (uint64_t any = bl | br | tl | tr; any; any &= any - 1) {
			const int b = std::countr_zero(any);
			const uint64_t bit = uint64_t{1} << b;
			const int i = i0 - 1 + b;
			Vertex vert;
			vert.quadOutward = QUADRANT_ALL;
			if (bl & bit) {
				vert.quadInward = QUADRANT_BL;
				vert.p = world(i + 1 + d, j + 1 + d);
				vertexes.push_back(vert);
			}
			if (br & bit) {
				vert.quadInward = QUADRANT_BR;
				vert.p = world(i - d, j + 1 + d);
				vertexes.push_back(vert);
			}
			if (tl & bit) {
				vert.quadInward = QUADRANT_TL;
				vert.p = world(i + 1 + d, j - d);
				vertexes.push_back(vert);
			}
			if (tr & bit) {
				vert.quadInward = QUADRANT_TR;
				vert.p = world(i - d, j - d);
				vertexes.push_back(vert);
			}
		}
	}

	// Calls emit(first, end) for each run of set bits in m, lowest first, with bit b as navcell
	// base + b. m has no bit 63 set, so the carry that clears a run stays in range.
	auto runs = [](uint64_t m, int base, auto&& emit) {
		while (m) {
			const uint64_t next = m + (m & (~m + 1)); // the lowest run cleared, the bit above it set
			emit(base + std::countr_zero(m), base + std::countr_zero(next));
			m &= next;
		}
	};

	// Edges along passability boundaries, one run per row or column (vendor 342-458).
	for (int j = j0; j < j1; ++j) {
		const uint64_t cur = rows[j - j0 + 1], up = rows[j - j0 + 2];
		runs(cur & ~up & innerI, i0 - 1, [&](int ia, int ib) { edges.push_back(Edge{world(ia - e, j + 1 - e), world(ib + e, j + 1 - e)}); });
		runs(up & ~cur & innerI, i0 - 1, [&](int ia, int ib) { edges.push_back(Edge{world(ib + e, j + 1 + e), world(ia - e, j + 1 + e)}); });
	}
	for (int i = i0; i < i1; ++i) {
		const uint64_t cur = cols[i - i0 + 1], right = cols[i - i0 + 2];
		runs(right & ~cur & innerJ, j0 - 1, [&](int ja, int jb) { edges.push_back(Edge{world(i + 1 + e, ja - e), world(i + 1 + e, jb + e)}); });
		runs(cur & ~right & innerJ, j0 - 1, [&](int ja, int jb) { edges.push_back(Edge{world(i + 1 - e, jb + e), world(i + 1 - e, ja - e)}); });
	}
}

// Vendor AddTerrainEdges (278-459). Navcells i0..i1, j0..j1 are considered. World position of
// navcell corner (x, y) is origin + (x, y) * cell.
// ADDITION: passability comes in by rows. pass_row(i, j, n, out) sets out[k] to 1 when navcell
// (i + k, j) is passable and to 0 when it is not or lies outside the grid (DEVIATION 3). The box
// and a one-navcell ring are read once into ts (the loops below would read most navcells five
// times), a box with no impassable navcell is done there, and rows or columns with nothing to
// add are skipped. The edges and vertices are the same as the vendor's loops give. With e > 0
// they move e navcells out (deviation 14).
template <class PassRowFn>
void add_terrain_edges(std::vector<Edge>& edges, std::vector<Vertex>& vertexes,
		int i0, int j0, int i1, int j1, PassRowFn& pass_row, int W, int H, float cell,
		Vec2 origin, TerrainEdgeScratch& ts, float e = 0.0f) {
	// Vendor 288-291: clamp so the outermost ring (always impassable) is never sampled.
	i0 = std::min(std::max(i0, 1), W - 2);
	j0 = std::min(std::max(j0, 1), H - 2);
	i1 = std::min(std::max(i1, 1), W - 2);
	j1 = std::min(std::max(j1, 1), H - 2);

	// The loops below read navcells i0 - 1 .. i1 + 1, j0 - 1 .. j1 + 1.
	const int bw = i1 - i0 + 3;
	const int bh = j1 - j0 + 3;
	if (bw <= 2 || bh <= 2) return;
	ts.pass.resize(static_cast<size_t>(bw) * static_cast<size_t>(bh));
	for (int bj = 0; bj < bh; ++bj)
		pass_row(i0 - 1, j0 - 1 + bj, bw, ts.pass.data() + static_cast<size_t>(bj) * bw);
	const uint8_t* const passBox = ts.pass.data();
	// Navcells i0..i1 of row j.
	const size_t rowLen = static_cast<size_t>(i1 - i0 + 1);
	auto row = [&](int j) { return passBox + static_cast<size_t>(j - j0 + 1) * bw + 1; };

	// Every vertex below needs an impassable navcell (i, j) of the box, and every edge a pair of
	// navcells of the box that differ.
	bool allPassable = true;
	for (int j = j0; j <= j1 && allPassable; ++j)
		allPassable = !std::memchr(row(j), 0, rowLen);
	if (allPassable) return;
	if (bw <= 64 && bh <= 64) {
		add_terrain_edges_bits(edges, vertexes, i0, j0, i1, j1, passBox, bw, bh, cell, origin, e);
		return;
	}

	ts.passT.resize(ts.pass.size());
	for (int bj = 0; bj < bh; ++bj)
		for (int bi = 0; bi < bw; ++bi)
			ts.passT[static_cast<size_t>(bi) * bh + bj] = passBox[static_cast<size_t>(bj) * bw + bi];
	auto pass = [passBox, bw, i0, j0](int i, int j) {
		return passBox[static_cast<size_t>(j - j0 + 1) * bw + (i - i0 + 1)] != 0;
	};
	// Navcells j0..j1 of column i.
	const size_t colLen = static_cast<size_t>(j1 - j0 + 1);
	auto col = [&](int i) { return ts.passT.data() + static_cast<size_t>(i - i0 + 1) * bh + 1; };

	auto world = [&](float x, float y) { return origin + Vec2(x * cell, y * cell); };
	const float d = EDGE_EXPAND_DELTA_CELLS + e;

	// Vertices at the convex corners of impassable navcells (vendor 293-340).
	for (int j = j0; j <= j1; ++j) {
		if (!std::memchr(row(j), 0, rowLen)) continue; // no impassable navcell in this row
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
	std::vector<int>& segR = ts.segA;
	std::vector<int>& segL = ts.segB;
	for (int j = j0; j < j1; ++j) {
		if (std::memcmp(row(j), row(j + 1), rowLen) == 0) continue; // no boundary between the rows
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
					edges.push_back(Edge{world(ia - e, j + 1 - e), world(ib + e, j + 1 - e)});
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
					edges.push_back(Edge{world(ib + e, j + 1 + e), world(ia - e, j + 1 + e)});
					ia = segL[n];
					ib = ia + 1;
				}
			}
		}
	}

	std::vector<int>& segU = ts.segA;
	std::vector<int>& segD = ts.segB;
	for (int i = i0; i < i1; ++i) {
		if (std::memcmp(col(i), col(i + 1), colLen) == 0) continue; // no boundary between the columns
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
					edges.push_back(Edge{world(i + 1 + e, ja - e), world(i + 1 + e, jb + e)});
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
					edges.push_back(Edge{world(i + 1 - e, jb + e), world(i + 1 - e, ja - e)});
					ja = segD[n];
					jb = ja + 1;
				}
			}
		}
	}
}

// ADDITION: rows read through pass(i, j), a callable bool that must return false outside the grid.
template <class PassFn>
struct PassRowsFrom {
	PassFn& pass;
	void operator()(int i, int j, int n, uint8_t* out) const {
		for (int k = 0; k < n; ++k)
			out[k] = pass(i + k, j) ? 1 : 0;
	}
};

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

// Vendor UnalignedEdgesSort (534-544): proximity order for partial_sort.
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

// ADDITION: working storage for compute_short_path. Callers that search often pass the same
// scratch each time (one per thread) to reuse its allocations; results do not depend on it.
struct ShortPathScratch {
	std::vector<vp::Edge> edges;
	std::vector<vp::Square> edgeSquares;
	std::vector<vp::Vertex> vertexes;
	std::vector<vp::OpenItem> open;
	std::vector<vp::Edge> edgesUnaligned;
	std::vector<vp::EdgeAA> edgesLeft, edgesRight, edgesBottom, edgesTop;
	vp::TerrainEdgeScratch terrain;
	std::vector<Vec2> reversed;
	std::vector<vp::Square> closers;
};

// Compute a short path from request.start towards request.goal over the visibility graph of
// the obstructions, expanded by request.clearance. The search is restricted to a box of
// half-width request.range around the start (shifted slightly towards the goal, vendor 563-579).
//
//   statics, units       obstruction squares (see ShortPathSquare). Units use
//                        clearance - request.unit_clearance_bias.
//   terrain_passable     callable bool(int i, int j) over the navcell grid; cells outside
//                        [0, grid_w) x [0, grid_h) count as impassable. The overload with a
//                        scratch takes terrain_pass_row instead, a callable
//                        void(int i, int j, int n, uint8_t* out) setting out[k] to 1 when
//                        navcell (i + k, j) is passable and 0 when not (or outside the grid).
//   cell_size, grid_origin  navcell size and world position of navcell (0, 0)'s corner.
//
// On success (goal reached) returns true. Otherwise returns false and out_waypoints holds the
// best partial path (vendor: the explored vertex with the lowest heuristic). Waypoints are in
// walking order, start excluded, goal last. An empty result means no movement is possible.
//
// VertexPathfinderDebugOverlay: the last short path search's goal and visibility graph
// (DebugRenderGoal, DebugRenderGraph), kept only while the pathfinder overlay is on
// (RtsoDebugOverlays). The simulation never reads it.
struct ShortPathDebug {
	std::atomic<bool> on{false};
	std::mutex m;
	ShortPathGoal goal;
	std::vector<vp::Vertex> vertexes;
	std::vector<vp::Edge> edges;
	std::vector<vp::Square> squares;
	uint32_t searches = 0;
};
inline ShortPathDebug &short_path_debug() {
	static ShortPathDebug d;
	return d;
}

// This overload works in `scratch`, whose allocations later calls reuse. The overload below,
// with terrain_passable and no scratch, gives the same results.
template <class PassRowFn>
bool compute_short_path(const ShortPathRequest& req,
		const std::vector<ShortPathSquare>& statics,
		const std::vector<ShortPathSquare>& units,
		PassRowFn terrain_pass_row,
		float cell_size, Vec2 grid_origin, int grid_w, int grid_h,
		ShortPathScratch& scratch, std::vector<Vec2>& out_waypoints) {
	using namespace vp;
	out_waypoints.clear();
	if (!(cell_size > 0.0f) || grid_w <= 0 || grid_h <= 0) return false;

	const float delta = EDGE_EXPAND_DELTA_CELLS * cell_size; // DEVIATION 3: world units
	const float sqd = req.square_delta >= 0.0f ? req.square_delta : delta; // deviation 15

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

	std::vector<Edge>& edges = scratch.edges;
	std::vector<Square>& edgeSquares = scratch.edgeSquares;
	std::vector<Vertex>& vertexes = scratch.vertexes;
	edges.clear();
	edgeSquares.clear();
	vertexes.clear();

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
		const Vec2 hd0(hw + pc + sqd, hh + pc + sqd);
		const Vec2 hd1(hw + pc + sqd, -(hh + pc + sqd));
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
		auto nearest_navcell = [&](float x, float z, int& i, int& j) {
			const float fi = std::floor((x - grid_origin.x) / cell_size);
			const float fj = std::floor((z - grid_origin.y) / cell_size);
			i = static_cast<int>(std::min(std::max(fi, 0.0f), static_cast<float>(grid_w - 1)));
			j = static_cast<int>(std::min(std::max(fj, 0.0f), static_cast<float>(grid_h - 1)));
		};
		// Navcells whose edges, moved out by the terrain clearance, reach into the box count too.
		const float tc = std::max(req.terrain_clearance, 0.0f);
		int i0, j0, i1, j1;
		nearest_navcell(rangeXMin - tc, rangeZMin - tc, i0, j0);
		nearest_navcell(rangeXMax + tc, rangeZMax + tc, i1, j1);
		// add_terrain_edges reads navcells 0 .. grid_w - 1 and 0 .. grid_h - 1 only.
		add_terrain_edges(edges, vertexes, i0, j0, i1, j1, terrain_pass_row, grid_w, grid_h, cell_size,
				grid_origin, scratch.terrain, tc / cell_size);
	}

	// Close vertices inside an edge square: they are trivially unreachable (vendor 710-728).
	// DEVIATION 6: vendor starts this loop at 2, skipping the first two squares.
	// The same vertices close whatever order the squares are tried in, so each vertex tries the
	// squares (those not holding the start) by their left side and stops at the first one that
	// starts to its right.
	std::vector<Square>& closers = scratch.closers;
	closers.clear();
	for (const Square& sq : edgeSquares)
		if (!(start.p.x >= sq.p0.x && start.p.y >= sq.p0.y && start.p.x <= sq.p1.x && start.p.y <= sq.p1.y))
			closers.push_back(sq);
	if (!closers.empty()) {
		std::sort(closers.begin(), closers.end(), [](const Square& a, const Square& b) { return a.p0.x < b.p0.x; });
		for (size_t j = 2; j < vertexes.size(); ++j) {
			const Vec2 p = vertexes[j].p;
			for (const Square& sq : closers) {
				if (!(p.x >= sq.p0.x)) break;
				if (p.y >= sq.p0.y && p.x <= sq.p1.x && p.y <= sq.p1.y) {
					vertexes[j].status = VERTEX_CLOSED;
					break;
				}
			}
		}
	}

	// ADDITION (deviation 16): close the vertices past the map's edge; the start and the goal stay.
	if (req.edge > 0.0f) {
		const float e = req.edge, e2 = e * e;
		for (size_t j = 2; j < vertexes.size(); ++j) {
			const Vec2 p = vertexes[j].p;
			if (req.edge_circular ? p.x * p.x + p.y * p.y > e2 : std::fabs(p.x) > e || std::fabs(p.y) > e)
				vertexes[j].status = VERTEX_CLOSED;
		}
	}

	if (vertexes.size() >= MAX_VERTEXES) return false; // DEVIATION 10 (vendor 730)
	if (ShortPathDebug &dbg = short_path_debug(); dbg.on.load(std::memory_order_relaxed)) {
		std::lock_guard<std::mutex> lock(dbg.m);
		dbg.goal = req.goal;
		dbg.vertexes = vertexes;
		dbg.edges = edges;
		dbg.squares = edgeSquares;
		++dbg.searches;
	}

	// A* over the visibility graph (vendor 743-888). Visibility is tested lazily when a vertex is popped.
	const float heuristicWeight = req.heuristic_weight > 0.0f ? req.heuristic_weight
		: ((edges.size() > 100 || edgeSquares.size() > 100) ? HEURISTIC_WEIGHT_LARGE : HEURISTIC_WEIGHT_SMALL);

	// The heap operations std::priority_queue performs, on a reusable vector.
	std::vector<OpenItem>& open = scratch.open;
	open.clear();
	const OpenItemGreater openCmp;
	auto open_push = [&](const OpenItem& item) {
		open.push_back(item);
		std::push_heap(open.begin(), open.end(), openCmp);
	};
	open_push(OpenItem{start.h, start.h, 0.0f, START_VERTEX_ID});

	uint16_t idBest = START_VERTEX_ID;
	float hBest = start.h;
	bool goalPopped = false;

	std::vector<Edge>& edgesUnaligned = scratch.edgesUnaligned;
	std::vector<EdgeAA>& edgesLeft = scratch.edgesLeft;
	std::vector<EdgeAA>& edgesRight = scratch.edgesRight;
	std::vector<EdgeAA>& edgesBottom = scratch.edgesBottom;
	std::vector<EdgeAA>& edgesTop = scratch.edgesTop;

	while (!open.empty()) {
		const OpenItem top = open.front();
		std::pop_heap(open.begin(), open.end(), openCmp);
		open.pop_back();
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

		// No partial sort of edgeSquares (vendor 780-781, deviation 11).
		edgesUnaligned.clear();
		edgesLeft.clear();
		edgesRight.clear();
		edgesBottom.clear();
		edgesTop.clear();
		split_aa_edges(curP, edges, edgeSquares, edgesUnaligned, edgesLeft, edgesRight, edgesBottom, edgesTop);

		// Partial sort for early exits (vendor 792-794). Results do not depend on this order (deviation 11).
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

			// An open vertex is only updated by a shorter path (vendor 873), so its visibility test is
			// skipped when this one is not shorter. The result is the same as testing first.
			const float g = curG + (curP - npos).length();
			Vertex& nv = vertexes[n];
			if (nv.status == VERTEX_OPEN && g >= nv.g)
				continue;

			const bool visible =
				check_visibility_left(curP, npos, edgesLeft) &&
				check_visibility_right(curP, npos, edgesRight) &&
				check_visibility_bottom(curP, npos, edgesBottom) &&
				check_visibility_top(curP, npos, edgesTop) &&
				check_visibility(curP, npos, edgesUnaligned);
			if (!visible)
				continue;

			if (nv.status == VERTEX_UNEXPLORED) {
				nv.status = VERTEX_OPEN;
				nv.g = g;
				nv.h = req.goal.distance(npos) * heuristicWeight;
				nv.pred = top.id;
				if (n == GOAL_VERTEX_ID)
					nv.p = npos; // remember the best goal position
				open_push(OpenItem{g + nv.h, nv.h, g, static_cast<uint16_t>(n)});

				// Remember the heuristically best vertex in case the goal is never reached.
				if (nv.h < hBest) {
					idBest = static_cast<uint16_t>(n);
					hBest = nv.h;
				}
			} else {
				// OPEN and shorter (checked above, vendor 873).
				nv.g = g;
				nv.pred = top.id;
				if (n == GOAL_VERTEX_ID)
					nv.p = npos;
				open_push(OpenItem{g + nv.h, nv.h, g, static_cast<uint16_t>(n)});
			}
		}
	}

	// The start already on the goal shape gives an empty path and counts as reached (vendor
	// returns an empty path there too; no waypoint is needed).
	const bool reached = goalPopped || start.h == 0.0f;

	// Reconstruct backwards, then reverse into walking order (vendor 890-894, deviation 7).
	std::vector<Vec2>& reversed = scratch.reversed;
	reversed.clear();
	for (uint16_t id = idBest; id != START_VERTEX_ID; id = vertexes[id].pred)
		reversed.push_back(vertexes[id].p);
	out_waypoints.assign(reversed.rbegin(), reversed.rend());

	return reached;
}

template <class PassFn>
bool compute_short_path(const ShortPathRequest& req,
		const std::vector<ShortPathSquare>& statics,
		const std::vector<ShortPathSquare>& units,
		PassFn terrain_passable,
		float cell_size, Vec2 grid_origin, int grid_w, int grid_h,
		std::vector<Vec2>& out_waypoints) {
	auto pass = [&](int i, int j) {
		return i >= 0 && j >= 0 && i < grid_w && j < grid_h && terrain_passable(i, j);
	};
	ShortPathScratch scratch;
	return compute_short_path(req, statics, units, vp::PassRowsFrom<decltype(pass)>{pass}, cell_size,
			grid_origin, grid_w, grid_h, scratch, out_waypoints);
}

} // namespace nav
} // namespace rtso

#endif // RTSO_NAV_VERTEX_PATHFINDER_H
