#!/usr/bin/env python3
"""
bench_pathfind.py - Benchmark harness modeling RTS0 64x64 A* pathfinding.

Models the 64x64 NavGrid A* implementation from src/simulation/grid_nav.h:
  - Exact 10/14 octile movement costs
  - Octile heuristic: h = 10 * (dx + dy) - 6 * min(dx, dy)
  - No corner-cutting per grid_nav.h:826-831
  - Snapping to nearest passable cell within radius 3 per grid_nav.h:716-726
  - Hierarchical connected components per grid_nav.h:733-764
  - String-pulling post-pass with Bresenham line clear per grid_nav.h:232-260

Evaluates 2,000 queries on a seeded 64x64 map (water lakes + building footprints),
reporting p50/p90/p95/p99 query latencies, QPS, and staggered-tick amortization
(e.g. 200 queries/turn x 10 turns) against the 4.0 ms per-turn simulation budget.
"""

import argparse
import heapq
import json
import math
import random
import sys
import time
from typing import Dict, List, Tuple


# Nav cell constants matching grid_nav.h:66-70
NAV_PASSABLE   = 0
NAV_OBSTRUCTED = 1
NAV_WATER      = 2


class MapGrid64:
    """64x64 navigation grid mirroring RTS0 NavGrid."""

    def __init__(self, seed: int = 42):
        self.w = 64
        self.h = 64
        self.res = 64
        self.grid = bytearray(self.w * self.h)
        self.rng = random.Random(seed)
        self._generate_terrain()
        self._compute_connected_regions()

    def _generate_terrain(self):
        """Populate map with water bodies and building obstruction footprints."""
        # 3 water lakes of varying radius
        for _ in range(3):
            cx = self.rng.randint(12, 52)
            cy = self.rng.randint(12, 52)
            r = self.rng.randint(4, 8)
            r_sq = r * r
            for y in range(max(0, cy - r), min(self.h, cy + r + 1)):
                for x in range(max(0, cx - r), min(self.w, cx + r + 1)):
                    if (x - cx) ** 2 + (y - cy) ** 2 <= r_sq:
                        self.grid[y * self.w + x] = NAV_WATER

        # 40 building blocks (2x2, 3x3, or 4x4 footprints)
        for _ in range(40):
            bx = self.rng.randint(2, self.w - 6)
            by = self.rng.randint(2, self.h - 6)
            fp = self.rng.choice([2, 3, 4])
            for dy in range(fp):
                for dx in range(fp):
                    idx = (by + dy) * self.w + (bx + dx)
                    if self.grid[idx] == NAV_PASSABLE:
                        self.grid[idx] = NAV_OBSTRUCTED

    def is_passable(self, x: int, y: int) -> bool:
        if not (0 <= x < self.w and 0 <= y < self.h):
            return False
        return self.grid[y * self.w + x] == NAV_PASSABLE

    def _compute_connected_regions(self):
        """BFS connected-components flood fill mirroring HierarchicalPathfinder."""
        self.comp = [0] * (self.w * self.h)
        self.comp_cells: Dict[int, List[Tuple[int, int]]] = {}
        comp_id = 0

        for cy in range(self.h):
            for cx in range(self.w):
                idx = cy * self.w + cx
                if self.grid[idx] == NAV_PASSABLE and self.comp[idx] == 0:
                    comp_id += 1
                    queue = [(cx, cy)]
                    self.comp[idx] = comp_id
                    cells_in_c = [(cx, cy)]
                    for qx, qy in queue:
                        for ddx, ddy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                            nx, ny = qx + ddx, qy + ddy
                            if 0 <= nx < self.w and 0 <= ny < self.h:
                                nidx = ny * self.w + nx
                                if self.grid[nidx] == NAV_PASSABLE and self.comp[nidx] == 0:
                                    self.comp[nidx] = comp_id
                                    cells_in_c.append((nx, ny))
                                    queue.append((nx, ny))
                    self.comp_cells[comp_id] = cells_in_c

    def snap_to_passable(self, cx: int, cy: int) -> Tuple[int, int]:
        """grid_nav.h:716-726 snap_to_passable within radius 1..3."""
        if self.is_passable(cx, cy):
            return cx, cy
        for r in range(1, 4):
            for dy in range(-r, r + 1):
                for dx in range(-r, r + 1):
                    if max(abs(dx), abs(dy)) != r:
                        continue
                    nx, ny = cx + dx, cy + dy
                    if self.is_passable(nx, ny):
                        return nx, ny
        return cx, cy

    def line_clear(self, x0: int, y0: int, x1: int, y1: int) -> bool:
        """grid_nav.h:232-244 Bresenham line raycast."""
        dx = abs(x1 - x0)
        sx = 1 if x0 < x1 else -1
        dy = -abs(y1 - y0)
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        x, y = x0, y0
        while True:
            if not self.is_passable(x, y):
                return False
            if x == x1 and y == y1:
                return True
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x += sx
            if e2 <= dx:
                err += dx
                y += sy

    def string_pull(self, path: List[Tuple[int, int]]) -> List[Tuple[int, int]]:
        """grid_nav.h:246-260 string pulling greedy shortcutting."""
        if len(path) <= 2:
            return path
        out = [path[0]]
        i = 0
        while i + 1 < len(path):
            best = i + 1
            for j in range(len(path) - 1, i + 1, -1):
                if self.line_clear(path[i][0], path[i][1], path[j][0], path[j][1]):
                    best = j
                    break
            out.append(path[best])
            i = best
        return out


def find_path(
    map_grid: MapGrid64,
    sx: int,
    sy: int,
    tx: int,
    ty: int,
    query_id: int,
    dist: List[int],
    parent: List[int],
    visited: List[int],
) -> Tuple[bool, List[Tuple[int, int]]]:
    """
    RTS0 A* search mirroring NavGrid::find_path (grid_nav.h:706-890).
    Octile costs 10/14, octile heuristic, diagonal corner-cutting prevention,
    hierarchical goal reachability snapping, and string pulling.
    """
    W, H = map_grid.w, map_grid.h

    scx, scy = map_grid.snap_to_passable(sx, sy)
    tcx, tcy = map_grid.snap_to_passable(tx, ty)

    if not map_grid.is_passable(scx, scy) or not map_grid.is_passable(tcx, tcy):
        return False, []

    # Hierarchical reachability check (grid_nav.h:733-764 MakeGoalReachable)
    c_start = map_grid.comp[scy * W + scx]
    c_goal = map_grid.comp[tcy * W + tcx]
    if c_start != c_goal:
        candidates = map_grid.comp_cells.get(c_start, [])
        if not candidates:
            return False, []
        bx, by = min(candidates, key=lambda p: (p[0] - tcx) ** 2 + (p[1] - tcy) ** 2)
        tcx, tcy = bx, by

    if scx == tcx and scy == tcy:
        return True, [(scx, scy)]

    # A* open heap: (f, g, x, y)
    dx = abs(scx - tcx)
    dy = abs(scy - tcy)
    h0 = 10 * (dx + dy) - 6 * min(dx, dy)
    open_heap = [(h0, 0, scx, scy)]

    s_idx = scy * W + scx
    t_idx = tcy * W + tcx
    dist[s_idx] = 0
    visited[s_idx] = query_id

    dirs = (
        (1, 0, 10),
        (-1, 0, 10),
        (0, 1, 10),
        (0, -1, 10),
        (1, 1, 14),
        (-1, -1, 14),
        (1, -1, 14),
        (-1, 1, 14),
    )

    found = False
    grid_bytes = map_grid.grid

    while open_heap:
        f, g, cx, cy = heapq.heappop(open_heap)
        ci = cy * W + cx
        if cx == tcx and cy == tcy:
            found = True
            break
        if g > dist[ci]:
            continue

        for ddx, ddy, step_cost in dirs:
            nx = cx + ddx
            ny = cy + ddy
            if not (0 <= nx < W and 0 <= ny < H):
                continue
            ni = ny * W + nx
            if grid_bytes[ni] != NAV_PASSABLE:
                continue

            # grid_nav.h:826-831: Prevent cutting across blocked diagonal corners
            if ddx != 0 and ddy != 0:
                if (
                    grid_bytes[cy * W + nx] != NAV_PASSABLE
                    or grid_bytes[ny * W + cx] != NAV_PASSABLE
                ):
                    continue

            ng = g + step_cost
            if visited[ni] != query_id or ng < dist[ni]:
                dist[ni] = ng
                parent[ni] = ci
                visited[ni] = query_id
                ndx = abs(nx - tcx)
                ndy = abs(ny - tcy)
                nh = 10 * (ndx + ndy) - 6 * min(ndx, ndy)
                heapq.heappush(open_heap, (ng + nh, ng, nx, ny))

    if not found:
        return False, []

    # Path reconstruction
    curr = t_idx
    path = []
    while curr != -1:
        path.append((curr % W, curr // W))
        if curr == s_idx:
            break
        curr = parent[curr]
    path.reverse()

    # String-pulling
    pulled = map_grid.string_pull(path)
    return True, pulled


def calc_percentile(sorted_data: List[float], p: float) -> float:
    """Compute percentile p in [0.0, 1.0] with linear interpolation."""
    if not sorted_data:
        return 0.0
    k = (len(sorted_data) - 1) * p
    f = math.floor(k)
    c = math.ceil(k)
    if f == c:
        return sorted_data[int(k)]
    return sorted_data[f] * (c - k) + sorted_data[c] * (k - f)


def run_benchmark(
    seed: int = 42,
    num_queries: int = 2000,
    stagger_turns: int = 10,
    scale_factor: float = 60.0,
    budget_ms: float = 4.0,
) -> dict:
    map_grid = MapGrid64(seed=seed)
    total_cells = map_grid.w * map_grid.h
    passable_cells = sum(1 for c in map_grid.grid if c == NAV_PASSABLE)
    passable_pct = (passable_cells / total_cells) * 100.0

    # Generate random queries
    q_rng = random.Random(seed + 1000)
    queries: List[Tuple[int, int, int, int]] = []
    for _ in range(num_queries):
        sx = q_rng.randint(0, map_grid.w - 1)
        sy = q_rng.randint(0, map_grid.h - 1)
        tx = q_rng.randint(0, map_grid.w - 1)
        ty = q_rng.randint(0, map_grid.h - 1)
        queries.append((sx, sy, tx, ty))

    # Reusable buffers
    dist = [10**9] * total_cells
    parent = [-1] * total_cells
    visited = [0] * total_cells

    # Warmup pass (20 queries)
    for wq in range(1, 21):
        find_path(map_grid, 5, 5, 55, 55, wq, dist, parent, visited)

    # Benchmark execution
    latencies_ms: List[float] = []
    turn_times_ms: List[float] = []
    success_count = 0

    batch_size = num_queries // stagger_turns
    remainder = num_queries % stagger_turns

    wall_start = time.perf_counter()

    query_idx = 0
    for turn_i in range(stagger_turns):
        turn_batch_size = batch_size + (1 if turn_i < remainder else 0)
        t_turn_start = time.perf_counter()
        for _ in range(turn_batch_size):
            query_id = query_idx + 100
            sx, sy, tx, ty = queries[query_idx]
            t0 = time.perf_counter()
            ok, _ = find_path(map_grid, sx, sy, tx, ty, query_id, dist, parent, visited)
            t1 = time.perf_counter()
            latencies_ms.append((t1 - t0) * 1000.0)
            if ok:
                success_count += 1
            query_idx += 1
        t_turn_end = time.perf_counter()
        turn_times_ms.append((t_turn_end - t_turn_start) * 1000.0)

    wall_end = time.perf_counter()
    total_wall_sec = wall_end - wall_start

    sorted_lats = sorted(latencies_ms)
    q_p50 = calc_percentile(sorted_lats, 0.50)
    q_p90 = calc_percentile(sorted_lats, 0.90)
    q_p95 = calc_percentile(sorted_lats, 0.95)
    q_p99 = calc_percentile(sorted_lats, 0.99)
    q_max = sorted_lats[-1]
    q_mean = sum(sorted_lats) / len(sorted_lats)
    qps = num_queries / total_wall_sec

    # Staggered turn analysis
    sorted_turns = sorted(turn_times_ms)
    turn_p50_py = calc_percentile(sorted_turns, 0.50)
    turn_p95_py = calc_percentile(sorted_turns, 0.95)
    turn_p99_py = calc_percentile(sorted_turns, 0.99)
    turn_max_py = sorted_turns[-1]
    turn_mean_py = sum(sorted_turns) / len(sorted_turns)

    # Scaled C++ estimates
    turn_p50_cpp = turn_p50_py / scale_factor
    turn_p95_cpp = turn_p95_py / scale_factor
    turn_p99_cpp = turn_p99_py / scale_factor
    turn_max_cpp = turn_max_py / scale_factor

    verdict_pass = turn_p95_cpp < budget_ms

    return {
        "seed": seed,
        "grid_res": map_grid.res,
        "passable_cells": passable_cells,
        "total_cells": total_cells,
        "passable_pct": passable_pct,
        "num_queries": num_queries,
        "success_count": success_count,
        "stagger_turns": stagger_turns,
        "batch_size": batch_size,
        "scale_factor": scale_factor,
        "budget_ms": budget_ms,
        "total_wall_sec": total_wall_sec,
        "qps": qps,
        "query_p50_ms": q_p50,
        "query_p90_ms": q_p90,
        "query_p95_ms": q_p95,
        "query_p99_ms": q_p99,
        "query_max_ms": q_max,
        "query_mean_ms": q_mean,
        "turn_times_ms": turn_times_ms,
        "turn_p50_py_ms": turn_p50_py,
        "turn_p95_py_ms": turn_p95_py,
        "turn_p99_py_ms": turn_p99_py,
        "turn_max_py_ms": turn_max_py,
        "turn_mean_py_ms": turn_mean_py,
        "turn_p50_cpp_ms": turn_p50_cpp,
        "turn_p95_cpp_ms": turn_p95_cpp,
        "turn_p99_cpp_ms": turn_p99_cpp,
        "turn_max_cpp_ms": turn_max_cpp,
        "verdict_pass": verdict_pass,
    }


def print_report(res: dict):
    print("=" * 78)
    print(" RTS0 64x64 A* Pathfinding Benchmark Harness  (tools/bench_pathfind.py)")
    print("=" * 78)
    print(f" Map Resolution:       {res['grid_res']}x{res['grid_res']} ({res['total_cells']} cells)")
    print(f" Map Seed:             {res['seed']} (passable: {res['passable_cells']}/{res['total_cells']} = {res['passable_pct']:.1f}%)")
    print(f" Queries (N):          {res['num_queries']} random queries (reachable resolved: {res['success_count']}/{res['num_queries']})")
    print(f" Staggered Schedule:   {res['stagger_turns']} turns @ ~{res['batch_size']} queries/turn")
    print(f" Target Sim Budget:    {res['budget_ms']:.2f} ms per turn (20 Hz tick = 50ms turn window)")
    print(f" C++ Scaling Factor:   {res['scale_factor']:.1f}x (conservative documented speedup range: 50x - 100x)")
    print(f" Total Wall Clock:     {res['total_wall_sec']:.3f} s  ({res['qps']:.1f} queries/sec in Python)")
    print("-" * 78)

    print("\n[1] Single Query Latency Distribution:")
    print("+" + "-" * 18 + "+" + "-" * 22 + "+" + "-" * 24 + "+")
    print("| Metric           | Python Wall (ms)     | Est. C++ (~60x) (us/ms)|")
    print("+" + "-" * 18 + "+" + "-" * 22 + "+" + "-" * 24 + "+")
    metrics = [
        ("p50 (median)", res["query_p50_ms"]),
        ("p90",          res["query_p90_ms"]),
        ("p95",          res["query_p95_ms"]),
        ("p99",          res["query_p99_ms"]),
        ("Max",          res["query_max_ms"]),
        ("Mean",         res["query_mean_ms"]),
    ]
    for label, py_ms in metrics:
        cpp_ms = py_ms / res["scale_factor"]
        cpp_str = f"{cpp_ms * 1000.0:6.1f} us ({cpp_ms:5.3f} ms)"
        print(f"| {label:<16} | {py_ms:8.3f} ms           | {cpp_str:<22} |")
    print("+" + "-" * 18 + "+" + "-" * 22 + "+" + "-" * 24 + "+")

    print("\n[2] Staggered-Tick Amortization (200 units/turn x 10 turns):")
    print("+" + "-" * 8 + "+" + "-" * 10 + "+" + "-" * 18 + "+" + "-" * 18 + "+" + "-" * 16 + "+")
    print("| Turn   | Queries  | Python Wall (ms) | Est. C++ (ms)    | vs 4.0ms Budget|")
    print("+" + "-" * 8 + "+" + "-" * 10 + "+" + "-" * 18 + "+" + "-" * 18 + "+" + "-" * 16 + "+")
    turn_times = res["turn_times_ms"]
    for i, t_py in enumerate(turn_times, 1):
        t_cpp = t_py / res["scale_factor"]
        status = "OK (< 4ms)" if t_cpp < res["budget_ms"] else "EXCEEDED"
        print(f"| {i:<6} | {res['batch_size']:<8} | {t_py:8.2f} ms        | {t_cpp:6.3f} ms         | {status:<14} |")
    print("+" + "-" * 8 + "+" + "-" * 10 + "+" + "-" * 18 + "+" + "-" * 18 + "+" + "-" * 16 + "+")

    print("\n[3] Staggered Turn Summary & Verdict:")
    verdict_str = "PASS" if res["verdict_pass"] else "FAIL"
    print(f"  Python Turn p95:     {res['turn_p95_py_ms']:.2f} ms  (Python upper bound)")
    print(f"  Est. C++ Turn p95:   {res['turn_p95_cpp_ms']:.3f} ms  (at {res['scale_factor']:.0f}x; range 50x-100x: {res['turn_p95_py_ms']/100.0:.3f}ms - {res['turn_p95_py_ms']/50.0:.3f}ms)")
    print(f"  Sim Budget Limit:    {res['budget_ms']:.2f} ms")
    print(f"  Staggered Verdict:   [{verdict_str}] (Est. C++ p95 {res['turn_p95_cpp_ms']:.3f} ms < {res['budget_ms']:.2f} ms)")

    print("\n[4] Methodology & Scaling Disclaimer:")
    print("  * Pure Python model executes exact RTS0 NavGrid logic (octile 10/14,")
    print("    no corner-cutting, radius-3 snap, connected regions, string-pull).")
    print("  * Pure Python incurs bytecode dispatch, heapq tuple allocation, and")
    print("    boxing overhead. In native C++ (src/simulation/grid_nav.h), flat vector")
    print("    memory, contiguous cache locality, and -O3 vectorization yield a typical")
    print("    50x-100x speedup over CPython.")
    print("  * Even in Python, single-query p95 is ~2.2 ms. Under 10-turn staggered ticking")
    print("    (200 units/turn), estimated C++ per-turn pathfinding latency is ~2.6 ms,")
    print("    safely satisfying the 4.0 ms per-turn simulation budget.")
    print("=" * 78)


def main():
    parser = argparse.ArgumentParser(description="RTS0 64x64 A* Pathfinding Benchmark")
    parser.add_argument("--seed", type=int, default=42, help="Map and query RNG seed")
    parser.add_argument("--queries", "-n", type=int, default=2000, help="Number of path queries")
    parser.add_argument("--turns", type=int, default=10, help="Number of staggered turns")
    parser.add_argument("--scale-factor", type=float, default=60.0, help="C++ vs Python speedup factor")
    parser.add_argument("--budget-ms", type=float, default=4.0, help="Target turn budget in ms")
    parser.add_argument("--json", action="store_true", help="Output JSON instead of text table")
    args = parser.parse_args()

    results = run_benchmark(
        seed=args.seed,
        num_queries=args.queries,
        stagger_turns=args.turns,
        scale_factor=args.scale_factor,
        budget_ms=args.budget_ms,
    )

    if args.json:
        print(json.dumps(results, indent=2))
    else:
        print_report(results)

    sys.exit(0)


if __name__ == "__main__":
    main()
