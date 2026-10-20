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
//     64x64 grid the linear scans are cheaper than rebuilding the cache. A caller
//     that keeps its open cells as bits (JpsBits: NavGrid's nodes, 512 a side on
//     a 256 map) has the straight scans read 64 cells a step instead; they stop
//     on the same cells.
//   - The search's per-tile state and open list can live in a JpsBuffers the
//     caller keeps (NavGrid keeps one per thread), as 0 A.D. keeps one
//     PathfinderState per LongPathfinder: a search reallocates nothing.
//   - Point goals only; PathGoal shapes and MakeGoalReachable are handled by the
//     caller (NavGrid::find_path) before the search runs.
//   - Out-of-bounds cells read as impassable (0 A.D. relies on an impassable
//     map border instead).
//
// Header-only and dependency-free, like grid_nav.h. Passability comes in as a
// callable so the same search serves PASS_INFANTRY / PASS_CAVALRY / PASS_SHIP.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
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

// An open-list entry: PriorityQueueHeap's item, f = g + h.
struct JpsOpenItem {
	uint32_t f;
	uint32_t h;
	uint32_t g;
	int id;
};

// The state of one search (0 A.D. PathfinderState's tiles grid and open list), kept between searches
// so a caller that runs many (NavGrid::find_path, from each worker thread) allocates it once. Every
// search resets what it reads, so a result never depends on the search before it.
struct JpsBuffers {
	std::vector<uint8_t> status;
	std::vector<uint32_t> cost;
	std::vector<int> pred;
	std::vector<JpsOpenItem> open; // a binary heap (std::push_heap / std::pop_heap), as std::priority_queue keeps it
	std::vector<int> touched;      // the tiles whose status the last search set, which the next one clears
};

// A grid's open cells as bits, for the straight scans: cell (x, y) is bit x % 64 of
// rows[y * row_words + x / 64] and bit y % 64 of cols[x * col_words + y / 64]; bits past the
// grid's edge are 0. The caller keeps them as its passability callable answers.
struct JpsBits {
	int row_words = 0, col_words = 0;
	const uint64_t *rows = nullptr;
	const uint64_t *cols = nullptr;
};

inline int jps_lowest_bit(uint64_t v) {
#if defined(__GNUC__) || defined(__clang__)
	return __builtin_ctzll(v);
#else
	int n = 0;
	while (!(v & 1)) v >>= 1, ++n;
	return n;
#endif
}
inline int jps_highest_bit(uint64_t v) {
#if defined(__GNUC__) || defined(__clang__)
	return 63 - __builtin_clzll(v);
#else
	int n = 63;
	while (!(v >> 63)) v <<= 1, --n;
	return n;
#endif
}

// HasJumpedHoriz / HasJumpedVert on a line of cells kept as bits (a row, or a column), words
// words long, beside lines a and b (null past the grid's edge): from `start` in direction dir
// (+1 or -1), the first cell that is closed or past the end gives `start` (no jump point); else the
// first that is the goal (goal: its place on the line, or -1) or has a forced neighbour, a cell on
// line a or b that is open where the one before it is closed, gives its place.
inline int jps_scan_bits(const uint64_t *line, const uint64_t *a, const uint64_t *b, int words, int start, int dir, int goal) {
	if (dir > 0) {
		const int x0 = start + 1;
		for (int k = x0 >> 6; k < words; ++k) {
			const uint64_t l = line[k];
			uint64_t stop = ~l;
			if (a) stop |= a[k] & ~((a[k] << 1) | (k > 0 ? a[k - 1] >> 63 : 0));
			if (b) stop |= b[k] & ~((b[k] << 1) | (k > 0 ? b[k - 1] >> 63 : 0));
			if (goal >= 0 && (goal >> 6) == k) stop |= uint64_t{1} << (goal & 63);
			if (k == (x0 >> 6)) stop &= ~uint64_t{0} << (x0 & 63);
			if (stop) {
				const int x = (k << 6) + jps_lowest_bit(stop);
				return (l >> (x & 63)) & 1 ? x : start;
			}
		}
		return start;
	}
	const int x0 = start - 1;
	if (x0 < 0) return start;
	for (int k = x0 >> 6; k >= 0; --k) {
		const uint64_t l = line[k];
		uint64_t stop = ~l;
		if (a) stop |= a[k] & ~((a[k] >> 1) | (k + 1 < words ? a[k + 1] << 63 : 0));
		if (b) stop |= b[k] & ~((b[k] >> 1) | (k + 1 < words ? b[k + 1] << 63 : 0));
		if (goal >= 0 && (goal >> 6) == k) stop |= uint64_t{1} << (goal & 63);
		if (k == (x0 >> 6) && (x0 & 63) != 63) stop &= (uint64_t{1} << ((x0 & 63) + 1)) - 1;
		if (stop) {
			const int x = (k << 6) + jps_highest_bit(stop);
			return (l >> (x & 63)) & 1 ? x : start;
		}
	}
	return start;
}

template <typename PassFn>
class LongPathfinder {
public:
	LongPathfinder(int w, int h, PassFn passable, JpsBuffers *buffers = nullptr, const JpsBits *bits = nullptr)
		: w_(w), h_(h), passable_(passable), bits_(bits), buf_(buffers ? *buffers : own_) {}

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
		buf_.open.clear();
		steps_ = 0;
		pushes_ = 0;

		const int start = idx(i0, j0);
		iBest_ = i0;
		jBest_ = j0;
		hBest_ = jps_heuristic(i0, j0, iGoal, jGoal);
		buf_.status[start] = STATUS_OPEN;
		buf_.touched.push_back(start);
		buf_.cost[start] = 0;
		buf_.pred[start] = start;
		open_push({hBest_, hBest_, 0, start});

		bool reached = false;
		while (!buf_.open.empty()) {
			const OpenItem cur = buf_.open.front();
			std::pop_heap(buf_.open.begin(), buf_.open.end(), OpenGreater());
			buf_.open.pop_back();
			// Lazy deletion stands in for PriorityQueueHeap::promote().
			if (buf_.status[cur.id] == STATUS_CLOSED || cur.g != buf_.cost[cur.id]) continue;
			++steps_;
			buf_.status[cur.id] = STATUS_CLOSED;
			const int i = cur.id % w_;
			const int j = cur.id / w_;

			if (i == iGoal && j == jGoal) {
				iBest_ = i;
				jBest_ = j;
				hBest_ = 0;
				reached = true;
				break;
			}

			const uint32_t g = buf_.cost[cur.id];
			const int p = buf_.pred[cur.id];
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
			cur = buf_.pred[cur];
		}
		std::reverse(cells.begin(), cells.end());

		if (stats) {
			stats->steps = steps_;
			stats->pushes = pushes_;
			stats->cost = buf_.cost[idx(iBest_, jBest_)];
		}
		return reached;
	}

private:
	enum : uint8_t { STATUS_UNEXPLORED = 0, STATUS_OPEN = 1, STATUS_CLOSED = 2 };

	using OpenItem = JpsOpenItem;
	// PriorityQueueHeap order: lowest f first, ties broken by lowest h; the id
	// tie-break keeps the search fully deterministic across platforms.
	struct OpenGreater {
		bool operator()(const OpenItem &a, const OpenItem &b) const {
			if (a.f != b.f) return a.f > b.f;
			if (a.h != b.h) return a.h > b.h;
			return a.id > b.id;
		}
	};
	// std::priority_queue's push: append, then sift up (pop: std::pop_heap, then drop the last).
	void open_push(const OpenItem &item) {
		buf_.open.push_back(item);
		std::push_heap(buf_.open.begin(), buf_.open.end(), OpenGreater());
	}

	bool inside(int i, int j) const { return i >= 0 && i < w_ && j >= 0 && j < h_; }
	int idx(int i, int j) const { return j * w_ + i; }
	bool pass(int i, int j) const { return inside(i, j) && passable_(i, j); }
	bool is_goal(int i, int j) const { return i == iGoal_ && j == jGoal_; }

	void reset_tiles() {
		const size_t n = static_cast<size_t>(w_) * static_cast<size_t>(h_);
		// Every status the last search set is listed in touched: on a grid of the same size only those
		// need clearing.
		if (buf_.status.size() == n) {
			for (const int t : buf_.touched) buf_.status[static_cast<size_t>(t)] = STATUS_UNEXPLORED;
		} else {
			buf_.status.assign(n, STATUS_UNEXPLORED);
		}
		buf_.touched.clear();
		// cost and pred are only read for tiles whose status was set during this
		// search (the start tile, or a tile that process_neighbour opened), so they
		// need no fill: what an earlier search left there keeps every result identical.
		if (buf_.cost.size() < n) buf_.cost.resize(n);
		if (buf_.pred.size() < n) buf_.pred.resize(n);
	}

	// LongPathfinder::ProcessNeighbour (LongPathfinder.cpp:409).
	void process_neighbour(int pi, int pj, int i, int j, uint32_t pg) {
		if (!pass(i, j)) return;
		const int n = idx(i, j);
		if (buf_.status[n] == STATUS_CLOSED) return;

		uint32_t dg;
		if (pi == i)
			dg = static_cast<uint32_t>(std::abs(pj - j)) * JPS_COST_ORTH;
		else if (pj == j)
			dg = static_cast<uint32_t>(std::abs(pi - i)) * JPS_COST_ORTH;
		else
			dg = static_cast<uint32_t>(std::abs(pi - i)) * JPS_COST_DIAG; // 45 degrees

		const uint32_t g = pg + dg;
		const uint32_t h = jps_heuristic(i, j, iGoal_, jGoal_);

		if (buf_.status[n] == STATUS_UNEXPLORED) {
			// Remember the best tile so far, in case we never reach the target
			if (h < hBest_) {
				hBest_ = h;
				iBest_ = i;
				jBest_ = j;
			}
			buf_.touched.push_back(n);
		} else if (g >= buf_.cost[n]) {
			return;
		}

		buf_.status[n] = STATUS_OPEN;
		buf_.cost[n] = g;
		buf_.pred[n] = idx(pi, pj);
		open_push({g + h, h, g, n});
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
		if (bits_) {
			const uint64_t *row = bits_->rows + static_cast<size_t>(j) * static_cast<size_t>(bits_->row_words);
			return jps_scan_bits(row, j > 0 ? row - bits_->row_words : nullptr, j + 1 < h_ ? row + bits_->row_words : nullptr,
					bits_->row_words, i, di, detectGoal && j == jGoal_ ? iGoal_ : -1);
		}
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
		if (bits_) {
			const uint64_t *col = bits_->cols + static_cast<size_t>(i) * static_cast<size_t>(bits_->col_words);
			return jps_scan_bits(col, i > 0 ? col - bits_->col_words : nullptr, i + 1 < w_ ? col + bits_->col_words : nullptr,
					bits_->col_words, j, dj, detectGoal && i == iGoal_ ? jGoal_ : -1);
		}
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
	const JpsBits *bits_ = nullptr;
	int iGoal_ = 0;
	int jGoal_ = 0;
	int iBest_ = 0;
	int jBest_ = 0;
	uint32_t hBest_ = 0;
	uint32_t steps_ = 0;
	uint32_t pushes_ = 0;
	JpsBuffers own_; // when the caller keeps none (declared before buf_, which may refer to it)
	JpsBuffers &buf_;
};

// LongPathfinder m_Debug (Grid, Steps, set by ComputeJPSPath whichever thread runs it): the
// tiles the last search reached, kept only while the pathfinder overlay is on
// (RtsoDebugOverlays). The simulation never reads it.
struct JpsDebug {
	std::atomic<bool> on{false};
	std::mutex m;
	std::vector<float> x, z;     // each reached tile's centre, in the order the search reached it
	std::vector<uint8_t> closed; // 1: closed, 0: still on the open list
	float size = 0.0f;           // a tile's side
	uint32_t searches = 0;       // searches kept so far
};
inline JpsDebug &jps_debug() {
	static JpsDebug d;
	return d;
}
// Keeps the search just run on `b` while the overlay is on: tile t of a w-wide grid lies at
// (x0 + (t % w) * size, z0 + (t / w) * size).
inline void jps_debug_keep(const JpsBuffers &b, int w, float x0, float z0, float size) {
	JpsDebug &d = jps_debug();
	if (!d.on.load(std::memory_order_relaxed)) return;
	std::lock_guard<std::mutex> lock(d.m);
	d.x.clear();
	d.z.clear();
	d.closed.clear();
	for (const int t : b.touched) {
		d.x.push_back(x0 + (float)(t % w) * size);
		d.z.push_back(z0 + (float)(t / w) * size);
		d.closed.push_back(b.status[(size_t)t] == 2 ? 1 : 0); // STATUS_CLOSED
	}
	d.size = size;
	++d.searches;
}

template <typename PassFn>
inline bool compute_jps_path(int w, int h, PassFn passable, int i0, int j0, int iGoal, int jGoal,
		std::vector<std::pair<int, int>> &cells, JpsStats *stats = nullptr, JpsBuffers *buffers = nullptr,
		const JpsBits *bits = nullptr) {
	LongPathfinder<PassFn> pf(w, h, passable, buffers, bits);
	return pf.compute_path(i0, j0, iGoal, jGoal, cells, stats);
}

} // namespace nav
} // namespace rtso

#endif // RTSO_NAV_LONG_PATHFINDER_H
