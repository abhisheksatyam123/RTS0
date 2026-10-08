# nav — Active SoA Navigation vs Vendor Reference

## Boundary

| Path | Role | Built? |
|------|------|--------|
| `src/0ad/simulation2/helpers/*` | **Vendor reference** — exact 0 A.D. GPL sources (`Grid.h:412`, `HierarchicalPathfinder.h/.cpp:865`, `LongPathfinder.h/.cpp`, `Pathfinding.h`, `PathGoal.h`). Keep verbatim for `diff -u` porting. **Never `#include` from active sim** and never added to `SConstruct` walk (it skips `src/0ad/**`). | No |
| `src/simulation/grid_nav.h` | **Active SoA port** — header-only, `std::vector`-backed `Grid<T>`, `NavCell` (`PASSABLE/OBSTRUCTED/WATER`), `NavGrid` (`cells`+`dirty`, `world_to_cell`, `mark_building`, `set_water_from_heights`, `blur_territory` stub), `WaypointPath` (`MAX_WP=64` fixed arrays). Owned by `Simulation::nav_grid_` + `territory_grid_` (`simulation.h`). | Yes |
| `src/simulation/nav/long_pathfinder.h` | **Active JPS port** of `LongPathfinder::ComputeJPSPath` (forced-neighbour rules, diagonal jumps, closest-tile fallback). `NavGrid::find_path` runs it by default (`NavGrid::use_jps`); legacy A* kept as `search_cells_astar`. Tested by `tools/test_long_pathfinder.cpp`. | Yes |
| `src/simulation/nav/geometry.h` | **Active header-only port** of 0 A.D. `helpers/Geometry.h/.cpp` (GPL-2.0-or-later; `Fixed`→`float`, `Vec2`). Square/segment/ray tests for VertexPathfinder. Deviations (sign fix in `TestSquareSquare` v-edge calls; `MaxDistanceSquareToSquare` nested-square quirk kept) documented in-file; tested by `tools/test_geometry.cpp`. | Header-only (no `.cpp`) |
| `src/simulation/nav/` (this folder) | Home of the remaining `VertexPathfinder` short-range port. | Planned |

## Why keep vendor separate?

- `src/0ad/**` pulls in `lib/*`, `ps/*`, `graphics/*`, SpiderMonkey JS and `Fixed` — cannot build without 0 A.D. stubs. `src/simulation/grid_nav.h` is deliberately dependency-free (only `<cstdint> <vector> <algorithm> <cstring> <cmath> <utility>`).
- License boundary: vendor files are GPL-2.0-or-later (see `docs/0AD_ATTRIBUTION.md`); active SoA code is RTS0-native.

## 0AD concepts replicated (stub today, full later)

- **Hierarchical CHUNK_SIZE = 96** navcells (`HierarchicalPathfinder.h:177`). Phase 1 `NavGrid::DEFAULT_RES=64` is a single chunk; Phase 7 will flood-fill regions per chunk, build `EdgesMap` adjacency, and `GlobalRegionID` connectivity (see `HierarchicalPathfinder.cpp:58, 140-280`).
- **Long octile heuristic** (`LongPathfinder.cpp:400`): `di=abs(i-iGoal)`, `dj=abs(j-jGoal)`, `diag=min(di,dj)`, `return PathCost(di-diag+dj-diag, diag)` where `PathCost(hv,diag).data = hv*65536 + diag*92682` (`Pathfinding.h:85`, `2^16*sqrt2`). Stub comment in `grid_nav.h:WaypointPath` preserves the `10*(dx+dy)+(14-10)*min` integer approximation for future A*.
- **WaypointPath** (`Pathfinding.h:69`) reversed list → `WaypointPath::xs/zs[MAX_WP]` SoA arrays, no heap per unit.
- **Dirtiness** (`Grid.h:372` `GridUpdateInformation::dirtinessGrid`) → `NavGrid::dirty` (`Grid<uint8_t>` 0/1) and `NavGrid::blur_territory()` stub mirroring `TerritoryManager` influence blur.

## Wiring checklist (do not break build)

1. `simulation.h` owns `NavGrid nav_grid_; Grid<uint8_t> territory_grid_;` + declares `rebuild_nav_grid()`, `find_path()`, `territory_owner_at()` (stubs returning false/-1 today).
2. `soa_storage.h` `UnitSoA` carries `xp`, `waypoint_cur`, `waypoint_count` (G1 Petra/Formation, zeroed in `push()`, copied in `remove_at()`, `reserve()`/`free_all()` wired).
3. No `.cpp` yet for `grid_nav.h` — header-only so `SConstruct` (which skips `src/0ad`) still builds.

## Next ports (tracked in `docs/PORTING_TODO.md` Phases 7,9,13)

- `HierarchicalPathfinder::InitRegions` / `ComputeNeighbors` / `RecomputeAllEdges` → `src/simulation/nav/hierarchical_pathfinder.h`
- ~~`LongPathfinder::ComputeJPSPath` + `ProcessNeighbour` → `src/simulation/nav/long_pathfinder.h`~~ landed (no JumpPointCache; 0 A.D. ships with it off)
- `VertexPathfinder` short-range steering → `src/simulation/nav/vertex_pathfinder.h`
- Wire `Simulation::rebuild_nav_grid()` to call hierarchical recompute on dirty cells. (`find_path()` already returns JPS waypoints into `WaypointPath`.)

## How to diff against vendor

```bash
diff -u src/0ad/simulation2/helpers/Grid.h src/simulation/grid_nav.h
diff -u src/0ad/simulation2/helpers/HierarchicalPathfinder.cpp src/simulation/nav/hierarchical_pathfinder.cpp
```
