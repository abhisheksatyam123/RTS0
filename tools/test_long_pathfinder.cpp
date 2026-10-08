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
//  4. NavGrid::find_path with JPS reaches every goal the legacy A* reaches, ends
//     at the same waypoint, and is deterministic across runs. Also counts how
//     many reachable goals the legacy A* misses (informational).
//  5. Timing: JPS vs legacy A* on the same queries (informational).
//  Exit: 0 = PASS, 1 = FAIL.
#include "simulation/grid_nav.h"

#include <chrono>
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
	int compared = 0, unreachable = 0, astar_missed = 0;
	double t_jps = 0, t_astar = 0;
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
			std::vector<std::pair<int, int>> ref;
			const bool ok_astar = nav.search_cells_astar(sx, sy, tx, ty, PASS_INFANTRY, ref);
			const auto c = std::chrono::steady_clock::now();
			t_jps += std::chrono::duration<double, std::micro>(b - a).count();
			t_astar += std::chrono::duration<double, std::micro>(c - b).count();
			steps_jps += st.steps;

			const long want = dijkstra(nav, sx, sy, tx, ty);
			const long got = walk_cost(nav, cells);
			if (cells.empty() || cells.front() != std::make_pair(sx, sy)) fail("path does not start at start", seed, sx, sy, tx, ty, 0, 0);
			if (got < 0) fail("illegal segment", seed, sx, sy, tx, ty, got, want);
			if (ok != (want >= 0)) fail("reachability differs from Dijkstra", seed, sx, sy, tx, ty, ok, want >= 0);
			if (ok && !ok_astar) ++astar_missed;
			if (ok_astar && !ok) fail("legacy A* found a path JPS missed", seed, sx, sy, tx, ty, ok, ok_astar);
			if (ok) {
				if (cells.back() != std::make_pair(tx, ty)) fail("path does not end at goal", seed, sx, sy, tx, ty, 0, 0);
				if (got != want) fail("cost differs from Dijkstra", seed, sx, sy, tx, ty, got, want);
				if (static_cast<long>(st.cost) != want) fail("reported cost differs", seed, sx, sy, tx, ty, st.cost, want);
				++compared;
			} else {
				++unreachable;
			}

			// find_path end to end: same reachability and end point as legacy A*,
			// deterministic across repeated calls.
			const float wx0 = ((sx + 0.5f) / RES) * 128.f - 64.f, wz0 = ((sy + 0.5f) / RES) * 128.f - 64.f;
			const float wx1 = ((tx + 0.5f) / RES) * 128.f - 64.f, wz1 = ((ty + 0.5f) / RES) * 128.f - 64.f;
			WaypointPath p1, p2, p3;
			const bool r1 = nav.find_path(wx0, wz0, wx1, wz1, p1);
			const bool r2 = nav.find_path(wx0, wz0, wx1, wz1, p2);
			nav.use_jps = false;
			const bool r3 = nav.find_path(wx0, wz0, wx1, wz1, p3);
			nav.use_jps = true;
			if (r3 && !r1) fail("find_path: legacy A* reached a goal JPS missed", seed, sx, sy, tx, ty, r1, r3);
			if (r1 != r2 || p1.count != p2.count) fail("find_path not deterministic", seed, sx, sy, tx, ty, p1.count, p2.count);
			for (int k = 0; k < p1.count && k < p2.count; ++k)
				if (p1.xs[k] != p2.xs[k] || p1.zs[k] != p2.zs[k]) fail("find_path not deterministic", seed, sx, sy, tx, ty, k, k);
			if (r1 && r3 && (p1.xs[p1.count - 1] != p3.xs[p3.count - 1] || p1.zs[p1.count - 1] != p3.zs[p3.count - 1]))
				fail("find_path end waypoint JPS vs A*", seed, sx, sy, tx, ty, 0, 0);
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

	std::printf("  optimal vs Dijkstra: %d reachable queries, %d unreachable, 400 grids\n", compared, unreachable);
	std::printf("  legacy A* failed on %d of those reachable queries (cardinal run skip never expands the cell next to a turn)\n", astar_missed);
	std::printf("  avg JPS %.1f us (%.1f pops/query), legacy A* %.1f us, speedup %.2fx\n",
			t_jps / (compared + unreachable), double(steps_jps) / (compared + unreachable),
			t_astar / (compared + unreachable), t_astar / t_jps);
	if (g_failures) {
		std::printf("FAIL (%d)\n", g_failures);
		return 1;
	}
	std::printf("PASS\n");
	return 0;
}
