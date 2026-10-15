#ifndef RTSO_NAV_LONG_PATHFINDER_H
#define RTSO_NAV_LONG_PATHFINDER_H

// Jump Point Search long-range pathfinder.
//
// Port of 0 A.D. LongPathfinder::ComputeJPSPath and its helpers
// (src/0ad/simulation2/helpers/LongPathfinder.cpp:400-860, LongPathfinder.h).
// Same algorithm, same neighbour pruning and forced-neighbour rules (the
// "no corner cutting" JPS variant), same open-list ordering (f, then lower h),
// same closest-tile fallback when the goal is never popped.
//
// Deliberate differences from the vendor code:
//   - Costs are RTS0's integer octile weights (orth 10, diag 14, grid_nav.h:125)
//     instead of PathCost's hv*65536 + diag*92682, so results stay comparable
//     with tools/check_parity.py's Dijkstra ground truth.
//   - Goal detection on a straight scan pushes the goal as a jump point instead
//     of clearing the open list (e.g. LongPathfinder.cpp:537 `state.open.clear()`).
//     The vendor shortcut can return a longer path; pushing keeps JPS optimal.
//   - No JumpPointCache. 0 A.D. ships with m_UseJPSCache = false too, and on a
//     64x64 grid the linear scans are cheaper than rebuilding the cache.
//   - Point goals only; PathGoal shapes and MakeGoalReachable are handled by the
//     caller (NavGrid::find_path) before the search runs.
//   - Out-of-bounds cells read as impassable (0 A.D. relies on an impassable
//     map border instead).
//
// Header-only and dependency-free, like grid_nav.h. Passability comes in as a
// callable so the same search serves PASS_INFANTRY / PASS_CAVALRY / PASS_SHIP.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <queue>
#include <utility>
#include <vector>

namespace rtso {
namespace nav {

static constexpr uint32_t JPS_COST_ORTH = 10;
static constexpr uint32_t JPS_COST_DIAG = 14;

struct JpsStats {
	uint32_t steps = 0;   // open-list pops (PathfinderState::steps)
	uint32_t pushes = 0;  // ProcessNeighbour calls that reached the open list
	uint32_t cost = 0;    // g of the returned end tile
};

// Octile heuristic, LongPathfinder::CalculateHeuristic (LongPathfinder.cpp:400).
inline uint32_t jps_heuristic(int i, int j, int iGoal, int jGoal) {
	const int di = std::abs(i - iGoal);
	const int dj = std::abs(j - jGoal);
	const int diag = std::min(di, dj);
	return static_cast<uint32_t>(di - diag + dj - diag) * JPS_COST_ORTH + static_cast<uint32_t>(diag) * JPS_COST_DIAG;
}

// OnTheWay (LongPathfinder.cpp:489): is a scan from (i,j) in (di,dj) heading
// towards the goal? Only those scans need to watch for the goal cell.
inline bool jps_on_the_way(int i, int j, int di, int dj, int iGoal, int jGoal) {
	if (dj != 0) {
		if ((jGoal - j) * dj < 0) return false;
	} else if (j != jGoal) {
		return false;
	}
	if (di != 0) {
		if ((iGoal - i) * di < 0) return false;
	} else if (i != iGoal) {
		return false;
	}
	return true;
}

// The per-tile arrays of a search, kept per thread between searches: allocated afresh each time
// they cost more than many searches (a Giant map's 256 x 256 navcells are 0.6 MB, mapped and
// faulted in again on every call).
struct JpsScratch {
	std::vector<uint8_t> status;
	std::unique_ptr<uint32_t[]> cost;
	std::unique_ptr<int[]> pred;
	size_t capacity = 0;
};
inline JpsScratch &jps_scratch() {
	thread_local JpsScratch s;
	return s;
}

template <typename PassFn>
class LongPathfinder {
public:
	LongPathfinder(int w, int h, PassFn passable) : w_(w), h_(h), passable_(passable) {}

	// Finds a path from (i0,j0) to (iGoal,jGoal). On success `cells` holds the
	// start cell, every jump point, and the goal cell, in walking order; each
	// consecutive pair is a straight horizontal, vertical or 45-degree run.
	// If the goal is unreachable, `cells` ends at the explored tile with the
	// lowest heuristic (0 A.D. iBest/jBest) and false is returned.
	bool compute_path(int i0, int j0, int iGoal, int jGoal, std::vector<std::pair<int, int>> &cells, JpsStats *stats = nullptr) {
		cells.clear();
		if (!inside(i0, j0) || !inside(iGoal, jGoal)) return false;

		iGoal_ = iGoal;
		jGoal_ = jGoal;
		reset_tiles();
		open_ = OpenList();
		steps_ = 0;
		pushes_ = 0;

		const int start = idx(i0, j0);
		iBest_ = i0;
		jBest_ = j0;
		hBest_ = jps_heuristic(i0, j0, iGoal, jGoal);
		status_[start] = STATUS_OPEN;
		cost_[start] = 0;
		pred_[start] = start;
		open_.push({hBest_, hBest_, 0, start});

		bool reached = false;
		while (!open_.empty()) {
			const OpenItem cur = open_.top();
			open_.pop();
			// Lazy deletion stands in for PriorityQueueHeap::promote().
			if (status_[cur.id] == STATUS_CLOSED || cur.g != cost_[cur.id]) continue;
			++steps_;
			status_[cur.id] = STATUS_CLOSED;
			const int i = cur.id % w_;
			const int j = cur.id / w_;

			if (i == iGoal && j == jGoal) {
				iBest_ = i;
				jBest_ = j;
				hBest_ = 0;
				reached = true;
				break;
			}

			const uint32_t g = cost_[cur.id];
			const int p = pred_[cur.id];
			int dpi = p % w_ - i;
			int dpj = p / w_ - j;
			dpi = dpi < 0 ? -1 : (dpi > 0 ? 1 : 0);
			dpj = dpj < 0 ? -1 : (dpj > 0 ? 1 : 0);

			if (dpi != 0 && dpj == 0) {
				// Moving horizontally from predecessor
				if (!pass(i + dpi, j - 1)) {
					add_jumped_diag(i, j, -dpi, -1, g);
					add_jumped_vert(i, j, -1, g, jps_on_the_way(i, j, 0, -1, iGoal, jGoal));
				}
				if (!pass(i + dpi, j + 1)) {
					add_jumped_diag(i, j, -dpi, +1, g);
					add_jumped_vert(i, j, +1, g, jps_on_the_way(i, j, 0, +1, iGoal, jGoal));
				}
				add_jumped_horiz(i, j, -dpi, g, jps_on_the_way(i, j, -dpi, 0, iGoal, jGoal));
			} else if (dpi == 0 && dpj != 0) {
				// Moving vertically from predecessor
				if (!pass(i - 1, j + dpj)) {
					add_jumped_diag(i, j, -1, -dpj, g);
					add_jumped_horiz(i, j, -1, g, jps_on_the_way(i, j, -1, 0, iGoal, jGoal));
				}
				if (!pass(i + 1, j + dpj)) {
					add_jumped_diag(i, j, +1, -dpj, g);
					add_jumped_horiz(i, j, +1, g, jps_on_the_way(i, j, +1, 0, iGoal, jGoal));
				}
				add_jumped_vert(i, j, -dpj, g, jps_on_the_way(i, j, 0, -dpj, iGoal, jGoal));
			} else if (dpi != 0 && dpj != 0) {
				// Moving diagonally from predecessor
				add_jumped_horiz(i, j, -dpi, g, jps_on_the_way(i, j, -dpi, 0, iGoal, jGoal));
				add_jumped_vert(i, j, -dpj, g, jps_on_the_way(i, j, 0, -dpj, iGoal, jGoal));
				add_jumped_diag(i, j, -dpi, -dpj, g);
			} else {
				// Start tile: expand all eight neighbours
				const bool passl = pass(i - 1, j);
				const bool passr = pass(i + 1, j);
				const bool passd = pass(i, j - 1);
				const bool passu = pass(i, j + 1);
				if (passl && passd) process_neighbour(i, j, i - 1, j - 1, g);
				if (passr && passd) process_neighbour(i, j, i + 1, j - 1, g);
				if (passl && passu) process_neighbour(i, j, i - 1, j + 1, g);
				if (passr && passu) process_neighbour(i, j, i + 1, j + 1, g);
				if (passl) process_neighbour(i, j, i - 1, j, g);
				if (passr) process_neighbour(i, j, i + 1, j, g);
				if (passd) process_neighbour(i, j, i, j - 1, g);
				if (passu) process_neighbour(i, j, i, j + 1, g);
			}
		}

		// Reconstruct (in reverse) from the goal or the closest tile seen.
		int cur = idx(iBest_, jBest_);
		while (true) {
			cells.emplace_back(cur % w_, cur / w_);
			if (cur == start) break;
			cur = pred_[cur];
		}
		std::reverse(cells.begin(), cells.end());

		if (stats) {
			stats->steps = steps_;
			stats->pushes = pushes_;
			stats->cost = cost_[idx(iBest_, jBest_)];
		}
		return reached;
	}

private:
	enum : uint8_t { STATUS_UNEXPLORED = 0, STATUS_OPEN = 1, STATUS_CLOSED = 2 };

	struct OpenItem {
		uint32_t f;
		uint32_t h;
		uint32_t g;
		int id;
	};
	// PriorityQueueHeap order: lowest f first, ties broken by lowest h; the id
	// tie-break keeps the search fully deterministic across platforms.
	struct OpenGreater {
		bool operator()(const OpenItem &a, const OpenItem &b) const {
			if (a.f != b.f) return a.f > b.f;
			if (a.h != b.h) return a.h > b.h;
			return a.id > b.id;
		}
	};
	using OpenList = std::priority_queue<OpenItem, std::vector<OpenItem>, OpenGreater>;

	bool inside(int i, int j) const { return i >= 0 && i < w_ && j >= 0 && j < h_; }
	int idx(int i, int j) const { return j * w_ + i; }
	bool pass(int i, int j) const { return inside(i, j) && passable_(i, j); }
	bool is_goal(int i, int j) const { return i == iGoal_ && j == jGoal_; }

	void reset_tiles() {
		const size_t n = static_cast<size_t>(w_) * static_cast<size_t>(h_);
		JpsScratch &sc = jps_scratch();
		sc.status.assign(n, STATUS_UNEXPLORED);
		// cost_ and pred_ are only read for tiles whose status was set during this
		// search (the start tile, or a tile that process_neighbour opened), so they
		// need no zero fill: what an earlier search left there is never read.
		if (sc.capacity < n) {
			sc.cost.reset(new uint32_t[n]);
			sc.pred.reset(new int[n]);
			sc.capacity = n;
		}
		status_ = sc.status.data();
		cost_ = sc.cost.get();
		pred_ = sc.pred.get();
	}

	// LongPathfinder::ProcessNeighbour (LongPathfinder.cpp:409).
	void process_neighbour(int pi, int pj, int i, int j, uint32_t pg) {
		if (!pass(i, j)) return;
		const int n = idx(i, j);
		if (status_[n] == STATUS_CLOSED) return;

		uint32_t dg;
		if (pi == i)
			dg = static_cast<uint32_t>(std::abs(pj - j)) * JPS_COST_ORTH;
		else if (pj == j)
			dg = static_cast<uint32_t>(std::abs(pi - i)) * JPS_COST_ORTH;
		else
			dg = static_cast<uint32_t>(std::abs(pi - i)) * JPS_COST_DIAG; // 45 degrees

		const uint32_t g = pg + dg;
		const uint32_t h = jps_heuristic(i, j, iGoal_, jGoal_);

		if (status_[n] == STATUS_UNEXPLORED) {
			// Remember the best tile so far, in case we never reach the target
			if (h < hBest_) {
				hBest_ = h;
				iBest_ = i;
				jBest_ = j;
			}
		} else if (g >= cost_[n]) {
			return;
		}

		status_[n] = STATUS_OPEN;
		cost_[n] = g;
		pred_[n] = idx(pi, pj);
		open_.push({g + h, h, g, n});
		++pushes_;
	}

	// Forced neighbour while scanning horizontally (LongPathfinder.cpp:542).
	bool forced_horiz(int ni, int j, int di) const {
		return (!pass(ni - di, j - 1) && pass(ni, j - 1)) ||
		       (!pass(ni - di, j + 1) && pass(ni, j + 1));
	}

	// Forced neighbour while scanning vertically (LongPathfinder.cpp:620).
	bool forced_vert(int i, int nj, int dj) const {
		return (!pass(i - 1, nj - dj) && pass(i - 1, nj)) ||
		       (!pass(i + 1, nj - dj) && pass(i + 1, nj));
	}

	// HasJumpedHoriz: i-coordinate of the next jump point, or i if none.
	int has_jumped_horiz(int i, int j, int di, bool detectGoal) const {
		int ni = i + di;
		while (true) {
			if (!pass(ni, j)) return i;
			if (detectGoal && is_goal(ni, j)) return ni;
			if (forced_horiz(ni, j, di)) return ni;
			ni += di;
		}
	}

	// HasJumpedVert: j-coordinate of the next jump point, or j if none.
	int has_jumped_vert(int i, int j, int dj, bool detectGoal) const {
		int nj = j + dj;
		while (true) {
			if (!pass(i, nj)) return j;
			if (detectGoal && is_goal(i, nj)) return nj;
			if (forced_vert(i, nj, dj)) return nj;
			nj += dj;
		}
	}

	void add_jumped_horiz(int i, int j, int di, uint32_t g, bool detectGoal) {
		const int ni = has_jumped_horiz(i, j, di, detectGoal);
		if (ni != i) process_neighbour(i, j, ni, j, g);
	}

	void add_jumped_vert(int i, int j, int dj, uint32_t g, bool detectGoal) {
		const int nj = has_jumped_vert(i, j, dj, detectGoal);
		if (nj != j) process_neighbour(i, j, i, nj, g);
	}

	// LongPathfinder::AddJumpedDiag (LongPathfinder.cpp:673). Diagonal jump
	// points are never cached in 0 A.D. either.
	void add_jumped_diag(int i, int j, int di, int dj, uint32_t g) {
		int ni = i + di;
		int nj = j + dj;
		const bool detectGoal = jps_on_the_way(i, j, di, dj, iGoal_, jGoal_);
		while (true) {
			// Stop at an obstructed cell
			if (!pass(ni, nj)) return;
			// Stop if this step touched the corner of an obstructed cell
			if (!pass(ni - di, nj) || !pass(ni, nj - dj)) return;
			if (detectGoal && is_goal(ni, nj)) {
				process_neighbour(i, j, ni, nj, g);
				return;
			}
			const int fi = has_jumped_horiz(ni, nj, di, detectGoal && jps_on_the_way(ni, nj, di, 0, iGoal_, jGoal_));
			const int fj = has_jumped_vert(ni, nj, dj, detectGoal && jps_on_the_way(ni, nj, 0, dj, iGoal_, jGoal_));
			if (fi != ni || fj != nj) {
				process_neighbour(i, j, ni, nj, g);
				const uint32_t gd = g + static_cast<uint32_t>(std::abs(ni - i)) * JPS_COST_DIAG;
				if (fi != ni) process_neighbour(ni, nj, fi, nj, gd);
				if (fj != nj) process_neighbour(ni, nj, ni, fj, gd);
				return;
			}
			ni += di;
			nj += dj;
		}
	}

	int w_;
	int h_;
	PassFn passable_;
	int iGoal_ = 0;
	int jGoal_ = 0;
	int iBest_ = 0;
	int jBest_ = 0;
	uint32_t hBest_ = 0;
	uint32_t steps_ = 0;
	uint32_t pushes_ = 0;
	uint8_t *status_ = nullptr; // jps_scratch()'s arrays, set by reset_tiles()
	uint32_t *cost_ = nullptr;
	int *pred_ = nullptr;
	OpenList open_;
};

template <typename PassFn>
inline bool compute_jps_path(int w, int h, PassFn passable, int i0, int j0, int iGoal, int jGoal,
		std::vector<std::pair<int, int>> &cells, JpsStats *stats = nullptr) {
	LongPathfinder<PassFn> pf(w, h, passable);
	return pf.compute_path(i0, j0, iGoal, jGoal, cells, stats);
}

} // namespace nav
} // namespace rtso

#endif // RTSO_NAV_LONG_PATHFINDER_H
