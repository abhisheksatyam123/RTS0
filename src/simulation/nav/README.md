# nav — Active SoA Navigation vs Vendor Reference

## Boundary

| Path | Role | Built? |
|------|------|--------|
| `src/0ad/simulation2/helpers/*` | **Vendor reference** — exact 0 A.D. GPL sources (`Grid.h:412`, `HierarchicalPathfinder.h/.cpp:865`, `LongPathfinder.h/.cpp`, `Pathfinding.h`, `PathGoal.h`). Keep verbatim for `diff -u` porting. **Never `#include` from active sim** and never added to `SConstruct` walk (it skips `src/0ad/**`). | No |
| `src/simulation/grid_nav.h` | **Active SoA port** — header-only, `std::vector`-backed `Grid<T>`, `NavCell` (`PASSABLE/OBSTRUCTED/WATER`), `NavGrid` (`cells`+`dirty`, `world_to_cell`, `mark_building`, `set_water_from_heights`, `blur_territory`), `WaypointPath` (`MAX_WP=64` fixed arrays). Owned by `Simulation::nav_grid_` + `territory_grid_` (`simulation.h`). | Yes |
| `src/simulation/nav/long_pathfinder.h` | **Active JPS port** of `LongPathfinder::ComputeJPSPath` (forced-neighbour rules, diagonal jumps, closest-tile fallback). `NavGrid::find_path` runs it by default (`NavGrid::use_jps`); legacy A* kept as `search_cells_astar`. Tested by `tools/test_long_pathfinder.cpp`. | Yes |
| `src/simulation/nav/geometry.h` | **Active header-only port** of 0 A.D. `helpers/Geometry.h/.cpp` (GPL-2.0-or-later; `Fixed`→`float`, `Vec2`). Square/segment/ray tests for VertexPathfinder. Deviations (sign fix in `TestSquareSquare` v-edge calls; `MaxDistanceSquareToSquare` nested-square quirk kept) documented in-file; tested by `tools/test_geometry.cpp`. | Header-only (no `.cpp`) |
| `src/simulation/nav/rasterize.h` | **Active header-only port** of 0 A.D. `helpers/Rasterize.cpp` `RasterizeRectWithClearance` (GPL-2.0-or-later; navcell covered iff fully inside the rounded clearance-expanded rectangle; per-cell callback clipped to the grid, lattice anchored at a grid origin). Used for building footprints in `Simulation::rebuild_nav_grid()`. Deviations (extension kept at 1 world unit; centre-cell fallback in the caller) documented in-file; tested by `tools/test_rasterize.cpp`. | Header-only (no `.cpp`) |
| `src/simulation/nav/vertex_pathfinder.h` | **Active header-only port** of 0 A.D. `VertexPathfinder::ComputeShortPath` (short-range visibility-graph A*; GPL-2.0-or-later). Obstructions and terrain come in as plain inputs, not `ICmpObstructionManager`. Deviations (float, clip-loop fix, `(Y,-X)` perpendicular, goal clamp, added `heuristic_weight`) documented in-file; tested by `tools/test_vertex_pathfinder.cpp`. Not wired into the simulation. | Header-only (no `.cpp`) |
| `src/simulation/nav/` (this folder) | Home of the remaining `VertexPathfinder` short-range port. | Planned |

## Why keep vendor separate?

- `src/0ad/**` pulls in `lib/*`, `ps/*`, `graphics/*`, SpiderMonkey JS and `Fixed` — cannot build without 0 A.D. stubs. `src/simulation/grid_nav.h` is deliberately dependency-free (only `<cstdint> <vector> <algorithm> <cstring> <cmath> <utility>`).
- License boundary: vendor files are GPL-2.0-or-later (see `docs/0AD_ATTRIBUTION.md`); active SoA code is RTS0-native.

## 0 A.D. concepts replicated

- **Hierarchical regions** (`HierarchicalPathfinder.h:177`, `.cpp:58, 140-280`): 0 A.D. uses `CHUNK_SIZE = 96` navcells; RTS0 uses 8 on the 64x64 grid, with per-chunk flood fill, region adjacency and global region ids for infantry, cavalry and ships (`grid_nav.h` `rebuild_regions`, `reachable`).
- **Long octile heuristic** (`LongPathfinder.cpp:400`): `di=abs(i-iGoal)`, `dj=abs(j-jGoal)`, `diag=min(di,dj)`, `return PathCost(di-diag+dj-diag, diag)` where `PathCost(hv,diag).data = hv*65536 + diag*92682` (`Pathfinding.h:85`, `2^16*sqrt2`). Stub comment in `grid_nav.h:WaypointPath` preserves the `10*(dx+dy)+(14-10)*min` integer approximation for future A*.
- **WaypointPath** (`Pathfinding.h:69`) reversed list → `WaypointPath::xs/zs[MAX_WP]` SoA arrays, no heap per unit.
- **Dirtiness** (`Grid.h:372` `GridUpdateInformation::dirtinessGrid`) → `NavGrid::dirty` (`Grid<uint8_t>` 0/1) and `NavGrid::blur_territory()` mirroring `TerritoryManager` influence blur.

## Wiring checklist (do not break build)

1. `simulation.h` owns `NavGrid nav_grid_; Grid<uint8_t> territory_grid_;` and exposes `rebuild_nav_grid()`, `find_path()`, `territory_owner_at()`.
2. `soa_storage.h` `UnitSoA` carries `xp`, `waypoint_cur`, `waypoint_count` (G1 Petra/Formation, zeroed in `push()`, copied in `remove_at()`, `reserve()`/`free_all()` wired).
3. No `.cpp` yet for `grid_nav.h` — header-only so `SConstruct` (which skips `src/0ad`) still builds.

## Next ports

Tracked, in priority order, in [`docs/MIGRATION.md`](../../../docs/MIGRATION.md#todo-in-priority-order).

## How to diff against vendor

```bash
diff -u src/0ad/simulation2/helpers/Grid.h src/simulation/grid_nav.h
diff -u src/0ad/simulation2/helpers/HierarchicalPathfinder.cpp src/simulation/nav/hierarchical_pathfinder.cpp
```
