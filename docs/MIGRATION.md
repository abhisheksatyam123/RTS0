# 0 A.D. code migration: status and todo

This file tracks the migration of the vendored 0 A.D. C++ in `src/0ad/` (reference only; `SConstruct` skips it) into live RTS0 code. Update it in every PR that ports, changes or drops a piece. Gameplay parity with 0 A.D.'s JS systems is tracked separately in `docs/REMAINING_GAPS.md` and `docs/PLAN.md`. 0 A.D. data imported by tools rather than ported code (assets, maps, unit rosters and heroes from `tools/extract_0ad_rosters.py`) is listed in `docs/0AD_ATTRIBUTION.md`.

Last updated: 2026-10-09 (turned building footprints).

Status key: **Done**, ported and in use · **Ported**, ported and tested but not used by the sim yet · **Partial** · **Todo** · **Not planned**

## Status by vendored file

### Pathfinding (`src/0ad/simulation2/helpers/`)

| Vendored file | Status | RTS0 location | Notes |
|---|---|---|---|
| `LongPathfinder.h/.cpp` | Done | `src/simulation/nav/long_pathfinder.h` | JPS search, used by `NavGrid::find_path`. Uses 10/14 costs and no JumpPointCache, and pushes the goal instead of clearing the open list. `ImprovePathWaypoints` is replaced by `NavGrid::string_pull`. It is the only long-range search (the legacy A* is gone). Tested against an 8-way Dijkstra by `tools/test_long_pathfinder.cpp` and `check_parity.py --unit nav-live`. |
| `HierarchicalPathfinder.h/.cpp` | Done | `src/simulation/grid_nav.h` (`rebuild_regions`, `reachable`, MakeGoalReachable snap) | `CHUNK_SIZE` is 8 navcells (0 A.D. uses 96). Separate region maps for infantry, cavalry and ships. Global region ids sit in flat per-chunk arrays (`GlobalRegionIds`) and are numbered in the vendor's `Recompute` order (chunk rows, then local regions), so they no longer depend on `std::unordered_map` iteration order, which differs between standard libraries (libc++ on Android). Petra uses ship region ids as sea indices. |
| `Grid.h` | Partial | `src/simulation/grid_nav.h` `Grid<T>` | `std::vector`-backed. No `SparseGrid` or serialization. |
| `Pathfinding.h/.cpp` | Partial | `src/simulation/grid_nav.h` | Covers passability classes, the octile `PathCost` model, `WaypointPath` and the cavalry slope limit. `CheckLineMovement` is replaced by the Bresenham `line_clear`. |
| `Geometry.h/.cpp` | Ported | `src/simulation/nav/geometry.h` | Float port. Fixes a vendor sign bug in `TestSquareSquare` (containment). Tested by `tools/test_geometry.cpp`. Used by VertexPathfinder and building steering. |
| `VertexPathfinder.h/.cpp` | Done | `src/simulation/nav/vertex_pathfinder.h`, used through `nav/building_steering.h` | `ComputeShortPath`: visibility-graph A* around squares given as plain lists (RTS0 has no obstruction manager), plus terrain edges from the nav grid. `move_units` uses it when a land unit's straight line to its next waypoint crosses a building, like 0 A.D. `UnitMotion`, and the unit keeps the short path's first waypoint (`ShortPathMemo`, saved with the unit) as `CCmpUnitMotion` keeps `m_ShortPath`, searching again only when it reaches it, the way there is blocked, its aim moves more than 1 m or the buildings change. Buildings only for now; `separation()` still keeps units apart. Fixes a vendor loop bound that skipped two edge squares when clipping vertices. Searches reuse per-thread storage, read terrain once per row and skip boxes with no water, and skip the visibility test of an open vertex a path can't improve; the vendor's partial sort of edge squares is dropped (deviation 11 in the header). None of this changes a path. Tested by `tools/test_vertex_pathfinder.cpp` (collision-free, optimal vs an independent Dijkstra, deterministic) and `tools/test_building_steering.cpp` (scenarios in the sim). |
| `Rasterize.h/.cpp` | Done | `src/simulation/nav/rasterize.h` | `RasterizeRectWithClearance`, used by `Simulation::rebuild_nav_grid` for building footprints. A navcell is blocked only if it lies fully inside the footprint plus the vendor's 1-unit extension (half an RTS0 navcell), so grid-aligned buildings block the same cells as before. An off-grid footprint-1 building that covers no full navcell falls back to the navcell under its centre. Tested by `tools/test_rasterize.cpp`. |

### Maths (`src/0ad/maths/`)

| Vendored file | Status | RTS0 location | Notes |
|---|---|---|---|
| `Fixed.h/.cpp`, `Sqrt.h/.cpp` | Ported | `src/core/math/fixed.h` | `rtso::Fixed` (16.16), `isqrt64`, `atan2_approx` and `sincos_approx`. Bit-identical to vendor across 7.4M checks. Tested by `tools/test_fixed.cpp`. The sim still uses `float` (see todo 5). |
| `MathUtil.h` | Not planned | | Small helpers that `<algorithm>`/`<cmath>` already cover. |
| `Vector2D.h`, `Vector3D.h`, `Matrix3D.h/.cpp` | Not planned | | Presentation uses Godot's `Vector2`/`Vector3`/`Transform3D`. |

### Networking (`src/0ad/network/`)

| Vendored file | Status | RTS0 location | Notes |
|---|---|---|---|
| `NetClientTurnManager`, `NetServerTurnManager` | Partial | `src/network/turn_manager.h/.cpp` | Owned by the "Test two-player multiplayer" thread. Design and status are in `docs/MULTIPLAYER.md`. |
| `NetHost`, `NetServer`, `NetClient`, `NetSession`, `NetMessage(s)`, `NetProtocol`, `NetEnet` | Partial | `src/network/net_host.h/.cpp`, `net_types.h` | UDP transport and handshake instead of ENet. Owned by the multiplayer thread. |
| `NetStats` | Partial | `src/network/net_host.h` `NetStats` | Owned by the multiplayer thread. |
| `NetFileTransfer` | Todo | | Map and savegame transfer for rejoining players. `turn_manager.h:318` notes this gap. Owned by the multiplayer thread. |
| `lobby/XmppClient` | Not planned | `src/network/lobby_client.h` | RTS0 uses an HTTP lobby (`tools/lobby_server.py`) instead of XMPP. |

### Other

| Vendored file | Status | Notes |
|---|---|---|
| `ps/CStr.h` | Not planned | `std::string` and Godot `String` cover it. |

## Todo, in priority order

This list covers porting the vendored C++ only. Overall project priorities and gameplay-parity work are in `docs/PLAN.md` and `docs/NEXT_STEPS.md`; this list doesn't repeat them.

1. **Obstruction shapes.** Rasterization is done, and building footprints turn with the building (`nav/footprint.h`, the axes of `CCmpObstruction::GetObstructionSquare`) in the nav grid, short paths, `separation()`, gates and reach checks. 0 A.D.'s `CCmpObstructionManager` isn't vendored, and RTS0 doesn't need a full one: `buildings_` and the `UnitGrid` in `separation()` give VertexPathfinder its shapes. Left: placement (`check_placement`, `can_place`) still tests the unturned grid cells until it uses `footprint_overlaps_box`, and units collide with buildings by their own radius rather than 0 A.D.'s per-class clearances (a "large" class unit such as a catapult keeps about 3 m), so siege can't take some gaps 0 A.D. lets it through.
2. **Units as short-path obstructions.** Steering passes only buildings to VertexPathfinder. 0 A.D. also passes nearby units (minus the moving group) when a unit is stuck. Add that if crowds jam at chokepoints, within a per-tick query budget (`test_vertex_pathfinder` times a query at ~8 µs near 10 squares, ~380 µs near 150).
3. **JumpPointCache.** Not needed yet. Maps now reach 512, but on 2026-10-08 JPS long paths were only about 3% of a 20-minute Mainland match on a 512 map (callgrind). Revisit if that share grows.
4. **Fixed-point simulation (optional).** Whether the sim moves from `float` to `rtso::Fixed` is a multiplayer-model decision. See `docs/PLAN.md` P5 and `docs/MULTIPLAYER.md`. `src/core/math/fixed.h` is ready if that decision is made. It would also need `CFixedVector2D`, which isn't vendored.

## History

| Date | PR | Change |
|---|---|---|
| 2026-10-09 | #103 | Removed the legacy grid A* (`search_cells_astar`, `use_jps`): `find_path` always runs the ported JPS, and `test_long_pathfinder` checks it against an 8-way Dijkstra. Units keep their short path around buildings between ticks like `CCmpUnitMotion::m_ShortPath` (65% fewer searches). Building footprints turn with the building's rotation (`nav/footprint.h`). |
| 2026-10-08 | #94 | VertexPathfinder searches for building steering close vertices inside squares by trying the squares sorted by their left edge, and build terrain edges from 64-bit navcell rows. Bit-identical results, part of an 11% cheaper match. |
| 2026-10-08 | #53 | HierarchicalPathfinder global regions in flat arrays, numbered in the vendor's order instead of hash-map order. Same regions; a 512-map match runs 4% fewer instructions. |
| 2026-10-08 | #42 | Made VertexPathfinder searches cheaper for building steering (reused storage, terrain read by rows, no edge-square sort) with bit-identical results. |
| 2026-10-08 | #8 | Ported `Rasterize` and used it for building footprints in the nav grid. Ported `VertexPathfinder` and used it to steer land units around buildings. GATHER, BUILD and RETURN_DROPSITE orders now get long paths when buildings block them. Added `tools/bench_sim.cpp`, a headless tick benchmark that CI smoke-runs. |
| 2026-10-08 | #2 | Added this file. |
| 2026-10-08 | #1 | JPS long pathfinder (fixes the A* missing ~5% of reachable goals). Ported `Fixed`/`Sqrt` and `Geometry`. Rewrote the nav-live parity probe. Fixed CI. |

## License

RTS0's code is GPL-2.0-or-later, the same as `src/0ad/`. Every port names its 0 A.D. source in its header and gets a row in `docs/0AD_ATTRIBUTION.md` in the same PR.
