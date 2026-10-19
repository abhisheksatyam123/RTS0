// RTS0 JPS long pathfinder test (headless: no SCons, no Godot).
// Differential test for src/simulation/nav/long_pathfinder.h, the port of
// 0 A.D. LongPathfinder::ComputeJPSPath, and its wiring in NavGrid::find_path.
// Build and run:
//   g++ -std=c++20 -O2 -Isrc tools/test_long_pathfinder.cpp -o /tmp/test_jps && /tmp/test_jps
//
//  1. On random 64x64 grids (obstacle density 0-45%, blocks and walls), the JPS
//     path cost equals an independent 8-way Dijkstra (orth 10, diag 14, no
//     corner cutting) for every start/goal pair, and reachability agrees.
//  2. Every JPS segment is a straight horizontal, vertical or 45-degree run over
//     passable cells that never cuts an obstructed corner.
//  3. Unreachable goals return false with a legal partial path that ends at the
//     jump point closest to the goal (0 A.D. iBest/jBest).
//  4. NavGrid::find_path reaches exactly the goals Dijkstra reaches, ends at the
//     goal, and is deterministic across runs.
//  5. Timing of the JPS queries (informational).
//  6. String pulling checks its shortcuts with 0 A.D.'s CheckLineMovement: no line it accepts, and
//     no segment of a find_path path, passes through a blocked navcell, not even diagonally between
//     two blocked navcells that share a corner (plain Bresenham steps through those).
//  7. A find_path start or goal on a blocked navcell moves to the nearest open navcell, as 0 A.D.'s
//     FindNearestPassableNavcell and MakeGoalReachable do, not to the first one a scan meets.
//  8. With the open cells as bits (nav::JpsBits, as NavGrid keeps its nodes), the straight scans
//     stop on the same cells: every query gives the same path, reach, cost and expansions on grids
//     whose sides are and aren't whole words, up to 512 a side, and a JpsBuffers kept from search to
//     search (cleared only where the last search wrote) gives what a fresh one does.
//  9. Turned obstructions on the nodes of the tile pass (NavGrid::tile_obs_raster): for rectangles
//     turned every way, thin and wide, grown by each class's clearance, every node inside one is
//     closed; no step between two open nodes (nor a diagonal one past two open nodes, as JPS takes
//     it) passes inside one; and a node beside a wide one's face, more than 1.5 * sqrt(2) steps
//     from its corners, stays open, as it does beside a rectangle square with the map (the nodes
//     are not closed by a margin round every face). Taking each rectangle off again leaves no cover.
//  Exit: 0 = PASS, 1 = FAIL.
#include "simulation/grid_nav.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <queue>
#include <vector>

using namespace rtso;

static int g_failures = 0;
static void fail(const char *what, int seed, int sx, int sy, int tx, int ty, long a, long b) {
	if (getenv("JPS_VERBOSE"))
		std::fprintf(stderr, "  [FAIL] %s seed=%d (%d,%d)->(%d,%d) got=%ld want=%ld\n", what, seed, sx, sy, tx, ty, a, b);
	++g_failures;
}

struct Rng {
	uint32_t s;
	uint32_t next() {
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		return s;
	}
	int range(int n) { return static_cast<int>(next() % static_cast<uint32_t>(n)); }
};

static constexpr int RES = NavGrid::DEFAULT_RES;

static void build_grid(NavGrid &nav, Rng &rng, int density_pct) {
	nav.init(64.f, RES);
	for (int y = 0; y < RES; ++y)
		for (int x = 0; x < RES; ++x)
			if (rng.range(100) < density_pct) nav.cells.at(x, y) = NAV_OBSTRUCTED;
	// A few building blocks and walls with gaps, like a real base.
	const int blocks = rng.range(8);
	for (int b = 0; b < blocks; ++b) nav.mark_building(rng.range(RES), rng.range(RES), 2 + rng.range(5), NAV_OBSTRUCTED);
	const int walls = rng.range(4);
	for (int w = 0; w < walls; ++w) {
		const bool vert = rng.range(2) == 0;
		const int at = rng.range(RES), from = rng.range(RES / 2), len = 8 + rng.range(RES / 2), gap = from + rng.range(len);
		for (int k = from; k < from + len && k < RES; ++k) {
			if (k == gap) continue;
			if (vert) nav.cells.at(at, k) = NAV_OBSTRUCTED;
			else nav.cells.at(k, at) = NAV_OBSTRUCTED;
		}
	}
}

// Does the segment from (ax, ay) to (bx, by), in navcells, pass through the inside of navcell (cx, cy)?
static bool segment_meets_cell(double ax, double ay, double bx, double by, int cx, int cy) {
	double t0 = 0.0, t1 = 1.0;
	auto clip = [&](double p, double d, double lo, double hi) {
		if (d == 0.0) return p > lo && p < hi;
		double ta = (lo - p) / d, tb = (hi - p) / d;
		if (ta > tb) std::swap(ta, tb);
		t0 = std::max(t0, ta);
		t1 = std::min(t1, tb);
		return true;
	};
	if (!clip(ax, bx - ax, cx, cx + 1) || !clip(ay, by - ay, cy, cy + 1)) return false;
	return t0 < t1;
}

// The blocked navcells the segment between two points (in navcells) passes through.
static int blocked_crossed(const NavGrid &nav, double ax, double ay, double bx, double by) {
	int n = 0;
	for (int y = (int)std::floor(std::min(ay, by)); y <= (int)std::floor(std::max(ay, by)); ++y)
		for (int x = (int)std::floor(std::min(ax, bx)); x <= (int)std::floor(std::max(ax, bx)); ++x)
			if (!nav.is_passable(x, y) && segment_meets_cell(ax, ay, bx, by, x, y)) ++n;
	return n;
}

// Independent ground truth: 8-way Dijkstra, no corner cutting.
static long dijkstra(const NavGrid &nav, int sx, int sy, int tx, int ty) {
	const long INF = 1L << 40;
	std::vector<long> d(RES * RES, INF);
	using Item = std::pair<long, int>;
	std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
	d[sy * RES + sx] = 0;
	pq.push({0, sy * RES + sx});
	while (!pq.empty()) {
		auto [dist, id] = pq.top();
		pq.pop();
		if (dist != d[id]) continue;
		const int x = id % RES, y = id / RES;
		if (x == tx && y == ty) return dist;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx) {
				if (!dx && !dy) continue;
				const int nx = x + dx, ny = y + dy;
				if (!nav.is_passable(nx, ny)) continue;
				if (dx && dy && (!nav.is_passable(x + dx, y) || !nav.is_passable(x, y + dy))) continue;
				const long nd = dist + ((dx && dy) ? 14 : 10);
				if (nd < d[ny * RES + nx]) {
					d[ny * RES + nx] = nd;
					pq.push({nd, ny * RES + nx});
				}
			}
	}
	return -1;
}

// Walks each segment cell by cell; returns total octile cost or -1 if illegal.
static long walk_cost(const NavGrid &nav, const std::vector<std::pair<int, int>> &cells) {
	long total = 0;
	for (size_t k = 0; k + 1 < cells.size(); ++k) {
		const int x0 = cells[k].first, y0 = cells[k].second;
		const int x1 = cells[k + 1].first, y1 = cells[k + 1].second;
		const int adx = std::abs(x1 - x0), ady = std::abs(y1 - y0);
		if (adx && ady && adx != ady) return -1;
		if (!adx && !ady) return -1;
		const int sx = (x1 > x0) - (x1 < x0), sy = (y1 > y0) - (y1 < y0);
		int x = x0, y = y0;
		while (x != x1 || y != y1) {
			if (sx && sy && (!nav.is_passable(x + sx, y) || !nav.is_passable(x, y + sy))) return -1;
			x += sx;
			y += sy;
			if (!nav.is_passable(x, y)) return -1;
			total += (sx && sy) ? 14 : 10;
		}
	}
	return total;
}

int main() {
	std::printf("RTS0 JPS long pathfinder test (src/simulation/nav/long_pathfinder.h)\n");
	int compared = 0, unreachable = 0, segments_clipping = 0, lines = 0, lines_through_blocked = 0;
	double t_jps = 0;
	long steps_jps = 0;
	for (int seed = 1; seed <= 400; ++seed) {
		Rng rng{static_cast<uint32_t>(seed) * 2654435761u + 1u};
		NavGrid nav;
		build_grid(nav, rng, (seed * 7) % 46);
		auto passable = [&nav](int x, int y) { return nav.is_passable(x, y); };
		for (int q = 0; q < 25; ++q) {
			int sx, sy, tx, ty;
			do { sx = rng.range(RES); sy = rng.range(RES); } while (!nav.is_passable(sx, sy));
			do { tx = rng.range(RES); ty = rng.range(RES); } while (!nav.is_passable(tx, ty));
			if (sx == tx && sy == ty) continue;

			std::vector<std::pair<int, int>> cells;
			nav::JpsStats st;
			const auto a = std::chrono::steady_clock::now();
			const bool ok = nav::compute_jps_path(RES, RES, passable, sx, sy, tx, ty, cells, &st);
			const auto b = std::chrono::steady_clock::now();
			t_jps += std::chrono::duration<double, std::micro>(b - a).count();
			steps_jps += st.steps;

			const long want = dijkstra(nav, sx, sy, tx, ty);
			const long got = walk_cost(nav, cells);
			if (cells.empty() || cells.front() != std::make_pair(sx, sy)) fail("path does not start at start", seed, sx, sy, tx, ty, 0, 0);
			if (got < 0) fail("illegal segment", seed, sx, sy, tx, ty, got, want);
			if (ok != (want >= 0)) fail("reachability differs from Dijkstra", seed, sx, sy, tx, ty, ok, want >= 0);
			if (ok) {
				if (cells.back() != std::make_pair(tx, ty)) fail("path does not end at goal", seed, sx, sy, tx, ty, 0, 0);
				if (got != want) fail("cost differs from Dijkstra", seed, sx, sy, tx, ty, got, want);
				if (static_cast<long>(st.cost) != want) fail("reported cost differs", seed, sx, sy, tx, ty, st.cost, want);
				++compared;
			} else {
				++unreachable;
			}

			// find_path end to end: reaches the goals Dijkstra reaches (these grids
			// have no regions, so it never moves the goal), ends at the goal and is
			// deterministic across repeated calls.
			const float wx0 = ((sx + 0.5f) / RES) * 128.f - 64.f, wz0 = ((sy + 0.5f) / RES) * 128.f - 64.f;
			const float wx1 = ((tx + 0.5f) / RES) * 128.f - 64.f, wz1 = ((ty + 0.5f) / RES) * 128.f - 64.f;
			WaypointPath p1, p2;
			const bool r1 = nav.find_path(wx0, wz0, wx1, wz1, p1);
			const bool r2 = nav.find_path(wx0, wz0, wx1, wz1, p2);
			if (r1 != (want >= 0)) fail("find_path: reachability differs from Dijkstra", seed, sx, sy, tx, ty, r1, want >= 0);
			if (r1 != r2 || p1.count != p2.count) fail("find_path not deterministic", seed, sx, sy, tx, ty, p1.count, p2.count);
			for (int k = 0; k < p1.count && k < p2.count; ++k)
				if (p1.xs[k] != p2.xs[k] || p1.zs[k] != p2.zs[k]) fail("find_path not deterministic", seed, sx, sy, tx, ty, k, k);
			if (r1 && (p1.count == 0 || std::fabs(p1.xs[p1.count - 1] - wx1) > 0.1f || std::fabs(p1.zs[p1.count - 1] - wz1) > 0.1f))
				fail("find_path does not end at the goal", seed, sx, sy, tx, ty, p1.count, 0);
			// No segment of the path, from the start, passes through a blocked navcell.
			double px = sx + 0.5, py = sy + 0.5;
			for (int k = 0; r1 && k < p1.count; ++k) {
				const double qx = (p1.xs[k] + 64.0) / 128.0 * RES, qy = (p1.zs[k] + 64.0) / 128.0 * RES;
				if (blocked_crossed(nav, px, py, qx, qy)) {
					++segments_clipping;
					break;
				}
				px = qx;
				py = qy;
			}
			// Nor does any line string pulling accepts between passable navcells.
			for (int k = 0; k < 4; ++k) {
				int ax, ay, bx, by;
				do { ax = rng.range(RES); ay = rng.range(RES); } while (!nav.is_passable(ax, ay));
				do { bx = ax + rng.range(13) - 6; by = ay + rng.range(13) - 6; } while (!nav.is_passable(bx, by));
				++lines;
				if (nav.line_clear(ax, ay, bx, by, PASS_INFANTRY) && blocked_crossed(nav, ax + 0.5, ay + 0.5, bx + 0.5, by + 0.5))
					++lines_through_blocked;
			}
		}
	}

	// Walled-off goal: false, partial path to the closest explored cell.
	{
		NavGrid nav;
		nav.init(64.f, RES);
		for (int y = 20; y <= 30; ++y)
			for (int x = 20; x <= 30; ++x)
				if (y == 20 || y == 30 || x == 20 || x == 30) nav.cells.at(x, y) = NAV_OBSTRUCTED;
		auto passable = [&nav](int x, int y) { return nav.is_passable(x, y); };
		std::vector<std::pair<int, int>> cells;
		const bool ok = nav::compute_jps_path(RES, RES, passable, 5, 25, 25, 25, cells);
		if (ok) fail("walled goal reported reachable", 0, 5, 25, 25, 25, 1, 0);
		if (cells.size() < 2 || walk_cost(nav, cells) < 0) fail("walled goal partial path illegal", 0, 5, 25, 25, 25, (long)cells.size(), 2);
		// Like 0 A.D., the fallback is the jump point with the lowest heuristic.
		else if (nav::jps_heuristic(cells.back().first, cells.back().second, 25, 25) >= nav::jps_heuristic(5, 25, 25, 25))
			fail("walled goal partial path gets no closer", 0, 5, 25, 25, 25, cells.back().first * 100 + cells.back().second, 0);
	}

	// Two navcells blocked corner to corner: the diagonal between them is refused, the way round is open.
	{
		NavGrid nav;
		nav.init(64.f, RES);
		nav.cells.at(11, 10) = NAV_OBSTRUCTED;
		nav.cells.at(10, 11) = NAV_OBSTRUCTED;
		if (nav.line_clear(10, 10, 11, 11, PASS_INFANTRY) || nav.line_clear(11, 11, 10, 10, PASS_INFANTRY) ||
				nav.line_clear(9, 9, 12, 12, PASS_INFANTRY))
			fail("a line squeezes between two blocked navcells", 0, 10, 10, 11, 11, 1, 0);
		if (!nav.line_clear(10, 10, 6, 7, PASS_INFANTRY) || !nav.line_clear(12, 10, 12, 14, PASS_INFANTRY))
			fail("an open line is refused", 0, 10, 10, 6, 7, 0, 1);
		// From a blocked navcell a line may leave, but not cross another.
		if (!nav.line_clear(11, 10, 15, 10, PASS_INFANTRY) || nav.line_clear(9, 11, 12, 11, PASS_INFANTRY))
			fail("leaving a blocked navcell", 0, 11, 10, 15, 10, 0, 1);
	}
	// A start on a blocked navcell (a unit pressed on a wall) moves to the nearest open one, as
	// LongPathfinder's FindNearestPassableNavcell does: the row above the wall it stands at, not the
	// first open navcell of a scan from below, which would start its path on the wall's far side.
	{
		NavGrid nav;
		nav.init(64.f, RES);
		for (int x = 4; x <= 40; ++x) nav.cells.at(x, 20) = NAV_OBSTRUCTED;
		WaypointPath wp;
		// Navcell (20, 20) spans z -24 to -22; the unit stands near its upper edge.
		const bool ok = nav.find_path(-23.0f, -22.2f, -23.0f, -15.0f, wp, PASS_INFANTRY);
		if (!ok || wp.count < 1 || wp.zs[0] < -22.0f)
			fail("a start on a wall snaps to its far side", 0, 20, 20, 20, 24, wp.count, 1);
		// A walk to the wall's face from above ends above it (MakeGoalReachable takes the nearest
		// navcell), not round the wall's end below it.
		const bool ok2 = nav.find_path(-23.0f, -15.0f, -23.0f, -22.2f, wp, PASS_INFANTRY);
		bool below = false;
		for (int i = 0; i < wp.count; ++i) below = below || wp.zs[i] < -24.0f;
		if (!ok2 || wp.count < 1 || below)
			fail("a goal on a wall snaps to its far side", 0, 20, 24, 20, 20, wp.count, 1);
	}
	{
		int queries = 0, differ = 0;
		nav::JpsBuffers kept;
		static const int sizes[][2] = {{70, 130}, {128, 128}, {512, 512}, {65, 63}};
		for (int g = 0; g < 24; ++g) {
			const int w = sizes[g % 4][0], h = sizes[g % 4][1];
			Rng rng{static_cast<uint32_t>(g) * 2246822519u + 7u};
			std::vector<uint8_t> open(static_cast<size_t>(w) * static_cast<size_t>(h));
			const int density = 5 + (g * 7) % 40;
			for (uint8_t &o : open) o = rng.range(100) >= density ? 1 : 0;
			for (int k = rng.range(6); k > 0; --k) { // walls with a gap
				const bool vert = rng.range(2) == 0;
				const int at = rng.range(vert ? w : h), len = vert ? h : w, gap = rng.range(len);
				for (int t = 0; t < len; ++t)
					if (t != gap) open[vert ? static_cast<size_t>(t) * w + at : static_cast<size_t>(at) * w + t] = 0;
			}
			const int rw = (w + 63) / 64, cw = (h + 63) / 64;
			std::vector<uint64_t> rows(static_cast<size_t>(h) * rw, 0), cols(static_cast<size_t>(w) * cw, 0);
			for (int y = 0; y < h; ++y)
				for (int x = 0; x < w; ++x)
					if (open[static_cast<size_t>(y) * w + x]) {
						rows[static_cast<size_t>(y) * rw + x / 64] |= uint64_t{1} << (x % 64);
						cols[static_cast<size_t>(x) * cw + y / 64] |= uint64_t{1} << (y % 64);
					}
			nav::JpsBits bits;
			bits.row_words = rw;
			bits.col_words = cw;
			bits.rows = rows.data();
			bits.cols = cols.data();
			auto pass = [&](int x, int y) { return open[static_cast<size_t>(y) * w + x] != 0; };
			for (int q = 0; q < 40; ++q) {
				int sx, sy, tx, ty;
				do { sx = rng.range(w); sy = rng.range(h); } while (!pass(sx, sy));
				tx = rng.range(w);
				ty = rng.range(h); // open or not: a closed goal is never reached
				std::vector<std::pair<int, int>> a, b, c;
				nav::JpsStats sa, sb, sc;
				const bool ra = nav::compute_jps_path(w, h, pass, sx, sy, tx, ty, a, &sa);
				const bool rb = nav::compute_jps_path(w, h, pass, sx, sy, tx, ty, b, &sb, nullptr, &bits);
				const bool rc = nav::compute_jps_path(w, h, pass, sx, sy, tx, ty, c, &sc, &kept, &bits);
				++queries;
				if (ra != rb || ra != rc || a != b || a != c || sa.cost != sb.cost || sa.cost != sc.cost || sa.steps != sb.steps ||
						sa.steps != sc.steps || sa.pushes != sb.pushes || sa.pushes != sc.pushes) {
					++differ;
					fail("bit scans find another path than cell scans", g, sx, sy, tx, ty, static_cast<long>(sb.cost), static_cast<long>(sa.cost));
				}
			}
		}
		std::printf("  bit scans: %d of %d queries differ from the cell scans\n", differ, queries);
	}
	{
		// The nodes: heights a sample a unit on a 128-unit map of 64 navcells a side, so the tiles (1
		// unit) are finer than the navcells (2 units) and their nodes are kept; one steep spot far off.
		NavGrid nav;
		nav.init(64.f, RES);
		const int HR = 2 * RES + 1;
		std::vector<float> heights(static_cast<size_t>(HR) * HR, 0.0f);
		heights[static_cast<size_t>(4) * HR + 4] = 3.0f;
		nav.set_slopes_from_heights(heights, HR);
		const int nw = nav.node_res();
		const float s = nav.node_step();
		long shapes = 0, inside_open = 0, steps_through = 0, face_closed = 0, left_over = 0, closed_outside = 0;
		if (nw != 4 * RES || std::fabs(s - 0.5f) > 1e-6f) fail("tile nodes not kept on a 1-unit heightmap", 0, nw, 0, 0, 0, nw, 4 * RES);
		Rng rng{12345u};
		for (int cls = 0; cls < NavGrid::TILE_CLASSES && nw == 4 * RES; ++cls) {
			const float c = NavGrid::tile_clearance(cls);
			std::vector<uint16_t> &count = nav.tile_obs_count_[cls];
			for (int k = 0; k < 600; ++k) {
				NavGrid::TileObstruction o;
				o.key = static_cast<uint64_t>(k);
				o.x = -20.0f + 40.0f * static_cast<float>(rng.range(10000)) / 10000.0f;
				o.z = -20.0f + 40.0f * static_cast<float>(rng.range(10000)) / 10000.0f;
				const float a = 0.002f + 1.5668f * static_cast<float>(rng.range(10000)) / 10000.0f;
				o.ux = std::cos(a);
				o.uz = -std::sin(a);
				const bool thin = k % 4 == 0;
				o.hu = thin ? 0.02f + 0.2f * static_cast<float>(rng.range(100)) / 100.0f : 0.3f + 3.0f * static_cast<float>(rng.range(100)) / 100.0f;
				o.hv = 0.1f + 3.0f * static_cast<float>(rng.range(100)) / 100.0f;
				count.assign(static_cast<size_t>(nw) * nw, 0);
				nav.tile_obs_raster(o, cls, 1, nullptr);
				++shapes;
				const float hu = o.hu + c, hv = o.hv + c; // grown by the clearance
				auto in_rect = [&](float x, float z) {
					const float dx = x - o.x, dz = z - o.z;
					return std::fabs(dx * o.ux + dz * o.uz) < hu && std::fabs(dz * o.ux - dx * o.uz) < hv;
				};
				auto open = [&](int i, int j) { return i >= 0 && j >= 0 && i < nw && j < nw && count[static_cast<size_t>(j) * nw + i] == 0; };
				// The step from node (i, j) to (i2, j2), sampled every 1/64 of its length.
				auto through = [&](int i, int j, int i2, int j2) {
					for (int t = 1; t < 64; ++t) {
						const float f = static_cast<float>(t) / 64.0f;
						if (in_rect(nav.node_pos(i) + f * (nav.node_pos(i2) - nav.node_pos(i)), nav.node_pos(j) + f * (nav.node_pos(j2) - nav.node_pos(j))))
							return true;
					}
					return false;
				};
				const int i0 = std::max(0, nav.node_of(o.x - 6.0f)), i1 = std::min(nw - 2, nav.node_of(o.x + 6.0f));
				const int j0 = std::max(0, nav.node_of(o.z - 6.0f)), j1 = std::min(nw - 2, nav.node_of(o.z + 6.0f));
				// The half sizes the nodes take the rectangle by (grown, and a hair).
				const float gu = o.hu + c + NavGrid::TILE_HAIR, gv = o.hv + c + NavGrid::TILE_HAIR;
				const bool wide = std::min(gu, gv) >= 0.75f * s;
				for (int j = j0; j <= j1; ++j)
					for (int i = i0; i <= i1; ++i) {
						const float x = nav.node_pos(i), z = nav.node_pos(j);
						if (in_rect(x, z) && open(i, j)) ++inside_open;
						if (!open(i, j)) {
							if (!in_rect(x, z)) ++closed_outside;
							continue;
						}
						if (open(i + 1, j) && through(i, j, i + 1, j)) ++steps_through;
						if (open(i, j + 1) && through(i, j, i, j + 1)) ++steps_through;
						if (open(i + 1, j + 1) && open(i + 1, j) && open(i, j + 1) && through(i, j, i + 1, j + 1)) ++steps_through;
						if (i > 0 && open(i - 1, j + 1) && open(i - 1, j) && open(i, j + 1) && through(i, j, i - 1, j + 1)) ++steps_through;
					}
				// Beside a wide rectangle's faces, more than 1.5 * sqrt(2) steps from its corners
				// (the furthest a step that cuts a corner has a node), the nodes outside it stay open.
				for (int j = j0; wide && j <= j1; ++j)
					for (int i = i0; i <= i1; ++i) {
						if (open(i, j)) continue;
						const float dx = nav.node_pos(i) - o.x, dz = nav.node_pos(j) - o.z;
						const float lu = dx * o.ux + dz * o.uz, lv = dz * o.ux - dx * o.uz;
						if (std::fabs(lu) < gu && std::fabs(lv) < gv) continue; // inside, with the hair
						if (std::fabs(lu) >= gu && std::fabs(lv) >= gv) continue; // off a corner
						const float along = std::fabs(lu) < gu ? gu - std::fabs(lu) : gv - std::fabs(lv); // from the nearer corner
						if (along > 1.5f * s * 1.4143f) ++face_closed;
					}
				nav.tile_obs_raster(o, cls, -1, nullptr);
				for (uint16_t v : count) left_over += v != 0;
			}
		}
		if (inside_open) fail("a node inside a turned obstruction is open", 0, 0, 0, 0, 0, inside_open, 0);
		if (steps_through) fail("a step between open nodes passes through a turned obstruction", 0, 0, 0, 0, 0, steps_through, 0);
		if (face_closed) fail("a node beside a turned obstruction's face is closed", 0, 0, 0, 0, 0, face_closed, 0);
		if (left_over) fail("taking turned obstructions off leaves cover", 0, 0, 0, 0, 0, left_over, 0);
		std::printf("  turned obstructions on the nodes: %ld shapes, %ld nodes inside open, %ld steps through, %ld closed beside a face, "
				"%ld closed outside in all, %ld left covered\n", shapes, inside_open, steps_through, face_closed, closed_outside, left_over);
	}
	if (segments_clipping) fail("find_path segments pass through blocked navcells", 0, 0, 0, 0, 0, segments_clipping, 0);
	if (lines_through_blocked) fail("line_clear accepts lines through blocked navcells", 0, 0, 0, 0, 0, lines_through_blocked, lines);
	std::printf("  string pulling: %d paths with a segment through a blocked navcell, %d of %d lines accepted through one\n",
			segments_clipping, lines_through_blocked, lines);
	std::printf("  optimal vs Dijkstra: %d reachable queries, %d unreachable, 400 grids\n", compared, unreachable);
	std::printf("  avg JPS %.1f us (%.1f pops/query)\n",
			t_jps / (compared + unreachable), double(steps_jps) / (compared + unreachable));
	if (g_failures) {
		std::printf("FAIL (%d)\n", g_failures);
		return 1;
	}
	std::printf("PASS\n");
	return 0;
}
