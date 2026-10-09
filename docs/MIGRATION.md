# 0 A.D. code migration: status and todo

This file tracks the migration of the vendored 0 A.D. C++ in `src/0ad/` (reference only; `SConstruct` skips it) into live RTS0 code. Update it in every PR that ports, changes or drops a piece. Gameplay parity with 0 A.D.'s JS systems is tracked separately in `docs/REMAINING_GAPS.md` and `docs/PLAN.md`. 0 A.D. data imported by tools rather than ported code (assets, maps, unit rosters and heroes from `tools/extract_0ad_rosters.py`) is listed in `docs/0AD_ATTRIBUTION.md`.

Last updated: 2026-10-09 (rectangular building footprints, walk endings).

Status key: **Done**, ported and in use · **Ported**, ported and tested but not used by the sim yet · **Partial** · **Todo** · **Not planned**

## Status by vendored file

### Pathfinding (`src/0ad/simulation2/helpers/`)

| Vendored file | Status | RTS0 location | Notes |
|---|---|---|---|
| `LongPathfinder.h/.cpp` | Done | `src/simulation/nav/long_pathfinder.h` | JPS search, used by `NavGrid::find_path`. Uses 10/14 costs and no JumpPointCache, and pushes the goal instead of clearing the open list. `ImprovePathWaypoints` is replaced by `NavGrid::string_pull`. It is the only long-range search (the legacy A* is gone). As in `ComputeJPSPath`, a path leaves out the navcell the unit stands in and ends at the goal itself, not the goal navcell's centre. Tested against an 8-way Dijkstra by `tools/test_long_pathfinder.cpp` and `check_parity.py --unit nav-live`. |
| `HierarchicalPathfinder.h/.cpp` | Done | `src/simulation/grid_nav.h` (`rebuild_regions`, `reachable`, MakeGoalReachable snap) | `CHUNK_SIZE` is 8 navcells (0 A.D. uses 96). Separate region maps for land units and ships (0 A.D.'s "default" and "large" classes cross the same navcells). A goal on impassable navcells (land for a ship, water for a land unit) also goes to the nearest navcell the unit can reach, and `cmd_move` makes that the walk's end. Global region ids sit in flat per-chunk arrays (`GlobalRegionIds`) and are numbered in the vendor's `Recompute` order (chunk rows, then local regions), so they no longer depend on `std::unordered_map` iteration order, which differs between standard libraries (libc++ on Android). Petra uses ship region ids as sea indices. |
| `Grid.h` | Partial | `src/simulation/grid_nav.h` `Grid<T>` | `std::vector`-backed. No `SparseGrid` or serialization. |
| `Pathfinding.h/.cpp` | Partial | `src/simulation/grid_nav.h` | Covers passability classes, the octile `PathCost` model, `WaypointPath` and `MaxTerrainSlope` for every land unit and building (`pathfinder.xml`: 1.0 for default, large and building-land, 1.25 for building-shore, at random maps' height scale; `LAND_MAX_TERRAIN_SLOPE`), where only cavalry had an invented limit. `CheckLineMovement` is replaced by the Bresenham `line_clear`. |
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

1. **Obstruction shapes.** Rasterization is done, and building footprints are rectangles that turn with the building (`nav/footprint.h`, the axes of `CCmpObstruction::GetObstructionSquare`) in the nav grid, short paths, `separation()`, gates and reach checks. A building has a half width (`half_size`) and a half depth (`half_d`, from `building_half_depth`, equal for every building so far), and a quarter turn swaps them (`Building::hu`, `hv`). 0 A.D.'s `CCmpObstructionManager` isn't vendored, and RTS0 doesn't need a full one: `buildings_` and the `UnitGrid` in `separation()` give VertexPathfinder its shapes. Left: placement (`check_placement`, `can_place`) still tests the unturned grid cells until it uses `footprint_overlaps_box`. Units keep their 0 A.D. passability class Clearance from buildings (`unit_building_clearance`: 0.8 m, 3 m for the "large" class of siege and elephants), but between units RTS0 keeps its own `unit_collision_radius`, where 0 A.D. gives the obstruction the clearance too. Range checks against structures (attack, capture, build and repair, dropsite returns, trade arrival) measure IsInTargetRange's gap between the unit's circle and the turned footprint (`structure_gap`), not the distance to the centre.
2. **Unit pushing.** 0 A.D.'s `CCmpUnitMotionManager` pushes moving units apart from moving ones and idle units from idle ones, slows crowds by pushing pressure, and makes idle units obstructions for a moving unit's short path. RTS0's `separation()` is its own spring model between all units; as in 0 A.D., a push never carries a unit onto a navcell its passability class can't cross. `motion_feedback()` ports the failed-movement rules on top of it, judged once per 200 ms turn as in 0 A.D., without pushing pressure (any push back counts, and a walk counts as free again only at half its steps). A full port would also take 0 A.D.'s clearance between units (0.2 units for infantry where `unit_collision_radius` is 0.5), which changes how close units stand for rendering and melee reach, so it needs those owners' agreement first. Steering passes only buildings to VertexPathfinder (`test_vertex_pathfinder` times a query at ~8 µs near 10 squares, ~380 µs near 150).
3. **JumpPointCache.** Not needed yet. Maps now reach 512, but on 2026-10-08 JPS long paths were only about 3% of a 20-minute Mainland match on a 512 map (callgrind). Revisit if that share grows.
4. **Fixed-point simulation (optional).** Whether the sim moves from `float` to `rtso::Fixed` is a multiplayer-model decision. See `docs/PLAN.md` P5 and `docs/MULTIPLAYER.md`. `src/core/math/fixed.h` is ready if that decision is made. It would also need `CFixedVector2D`, which isn't vendored.

## History

| Date | PR | Change |
|---|---|---|
| 2026-10-09 | #103 | Removed the legacy grid A* (`search_cells_astar`, `use_jps`): `find_path` always runs the ported JPS, and `test_long_pathfinder` checks it against an 8-way Dijkstra. Units keep their short path around buildings between ticks like `CCmpUnitMotion::m_ShortPath` (65% fewer searches). Building footprints are rectangles that turn with the building's rotation (`nav/footprint.h`), and #207's `structure_gap` measures to the turned footprint, ready for 0 A.D.'s long, thin walls. Units keep 0 A.D.'s class clearance from buildings, so siege takes 8 m gaps. Walks end as in 0 A.D.: obstructed within 12 m of their point, or after 35 turns of failed movement, judged per 200 ms turn (`motion_feedback`, `CCmpUnitMotion::HandleObstructedMove`, UnitAI's relaxed range). Long paths leave out the start navcell, and an unreachable walk point gives way to the nearest reachable one. Every land unit and building keeps off slopes over `pathfinder.xml`'s MaxTerrainSlope: walks and task walks go round cliffs, Cantabrian Highlands keeps its ramps, and where a port's terrain would wall a base off (`ease_routes_between_bases`) the random map eases a ramp along the gentlest route. |
| 2026-10-08 | #94 | VertexPathfinder searches for building steering close vertices inside squares by trying the squares sorted by their left edge, and build terrain edges from 64-bit navcell rows. Bit-identical results, part of an 11% cheaper match. |
| 2026-10-08 | #53 | HierarchicalPathfinder global regions in flat arrays, numbered in the vendor's order instead of hash-map order. Same regions; a 512-map match runs 4% fewer instructions. |
| 2026-10-08 | #42 | Made VertexPathfinder searches cheaper for building steering (reused storage, terrain read by rows, no edge-square sort) with bit-identical results. |
| 2026-10-08 | #8 | Ported `Rasterize` and used it for building footprints in the nav grid. Ported `VertexPathfinder` and used it to steer land units around buildings. GATHER, BUILD and RETURN_DROPSITE orders now get long paths when buildings block them. Added `tools/bench_sim.cpp`, a headless tick benchmark that CI smoke-runs. |
| 2026-10-08 | #2 | Added this file. |
| 2026-10-08 | #1 | JPS long pathfinder (fixes the A* missing ~5% of reachable goals). Ported `Fixed`/`Sqrt` and `Geometry`. Rewrote the nav-live parity probe. Fixed CI. |

## License

RTS0's code is GPL-2.0-or-later, the same as `src/0ad/`. Every port names its 0 A.D. source in its header and gets a row in `docs/0AD_ATTRIBUTION.md` in the same PR.
