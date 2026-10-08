# 0 A.D. code migration: status and todo

This file tracks the migration of the vendored 0 A.D. C++ in `src/0ad/` (reference only; `SConstruct` skips it) into live RTS0 code. Update it in every PR that ports, changes or drops a piece. Gameplay parity with 0 A.D.'s JS systems is tracked separately in `docs/REMAINING_GAPS.md` and `docs/PLAN.md`.

Last updated: 2026-10-08 (after PR #1).

Status key: **Done**, ported and in use · **Ported**, ported and tested but not used by the sim yet · **Partial** · **Todo** · **Not planned**

## Status by vendored file

### Pathfinding (`src/0ad/simulation2/helpers/`)

| Vendored file | Status | RTS0 location | Notes |
|---|---|---|---|
| `LongPathfinder.h/.cpp` | Done | `src/simulation/nav/long_pathfinder.h` | JPS search, used by `NavGrid::find_path`. Uses 10/14 costs and no JumpPointCache, and pushes the goal instead of clearing the open list. `ImprovePathWaypoints` is replaced by `NavGrid::string_pull`. Tested by `tools/test_long_pathfinder.cpp` and `check_parity.py --unit nav-live`. |
| `HierarchicalPathfinder.h/.cpp` | Done | `src/simulation/grid_nav.h` (`rebuild_regions`, `reachable`, MakeGoalReachable snap) | `CHUNK_SIZE` is 8 on the 64x64 grid (0 A.D. uses 96). Separate region maps for infantry, cavalry and ships. |
| `Grid.h` | Partial | `src/simulation/grid_nav.h` `Grid<T>` | `std::vector`-backed. No `SparseGrid` or serialization. |
| `Pathfinding.h/.cpp` | Partial | `src/simulation/grid_nav.h` | Covers passability classes, the octile `PathCost` model, `WaypointPath` and the cavalry slope limit. `CheckLineMovement` is replaced by the Bresenham `line_clear`. |
| `Geometry.h/.cpp` | Ported | `src/simulation/nav/geometry.h` | Float port. Fixes a vendor sign bug in `TestSquareSquare` (containment). Tested by `tools/test_geometry.cpp`. Waiting on VertexPathfinder. |
| `VertexPathfinder.h/.cpp` | Todo | `src/simulation/nav/vertex_pathfinder.h` (planned) | Short-range steering around units and buildings. Units currently rely on `separation()` pushing. Blocked on an obstruction manager (see todo 1). |
| `Rasterize.h/.cpp` | Todo | (planned) | Rasterizes obstruction shapes into the nav grid with clearance. Needed with the obstruction manager. `NavGrid::mark_building` is a square-footprint stand-in today. |

### Maths (`src/0ad/maths/`)

| Vendored file | Status | RTS0 location | Notes |
|---|---|---|---|
| `Fixed.h/.cpp`, `Sqrt.h/.cpp` | Ported | `src/core/math/fixed.h` | `rtso::Fixed` (16.16), `isqrt64`, `atan2_approx` and `sincos_approx`. Bit-identical to vendor across 7.4M checks. Tested by `tools/test_fixed.cpp`. The sim still uses `float` (see todo 2). |
| `MathUtil.h` | Not planned | | Small helpers that `<algorithm>`/`<cmath>` already cover. |
| `Vector2D.h`, `Vector3D.h`, `Matrix3D.h/.cpp` | Not planned | | Presentation uses Godot's `Vector2`/`Vector3`/`Transform3D`. A fixed-point sim would need `CFixedVector2D`, which isn't vendored. |

### Networking (`src/0ad/network/`)

| Vendored file | Status | RTS0 location | Notes |
|---|---|---|---|
| `NetClientTurnManager`, `NetServerTurnManager` | Partial | `src/network/turn_manager.h/.cpp` | 200ms lockstep turns, state hashing and replay. Owned by the "Test two-player multiplayer" thread. See `docs/MULTIPLAYER.md`. |
| `NetHost`, `NetServer`, `NetClient`, `NetSession`, `NetMessage(s)`, `NetProtocol`, `NetEnet` | Partial | `src/network/net_host.h/.cpp`, `net_types.h` | UDP transport and handshake instead of ENet. Owned by the multiplayer thread. |
| `NetStats` | Partial | `src/network/net_host.h` `NetStats` | Owned by the multiplayer thread. |
| `NetFileTransfer` | Todo | | Map and savegame transfer for rejoining players. `turn_manager.h:318` notes this gap. Owned by the multiplayer thread. |
| `lobby/XmppClient` | Not planned | `src/network/lobby_client.h` | RTS0 uses an HTTP lobby (`tools/lobby_server.py`) instead of XMPP. |

### Other

| Vendored file | Status | Notes |
|---|---|---|
| `ps/CStr.h` | Not planned | `std::string` and Godot `String` cover it. |

## Todo, in priority order

1. **Obstruction manager and rasterization.** Track unit and building footprints (0 A.D. `CCmpObstructionManager`, which isn't vendored) and port `Rasterize.cpp` so buildings get correct clearance in the nav grid. This unblocks todo 3.
2. **Fixed-point simulation.** Move determinism-critical sim state (positions, speeds, HP, ranges) from `float` to `rtso::Fixed`, starting with movement. The ~900 `float` uses in `simulation.cpp` are a cross-platform lockstep risk. Coordinate with the multiplayer thread, because this changes the state hash.
3. **VertexPathfinder.** Port short-range steering on top of `nav/geometry.h` and the obstruction manager, replacing push-only `separation()` for units with an order.
4. **Fixed-point vectors.** Port `CFixedVector2D` (from upstream 0 A.D.; it isn't vendored) once todo 2 starts.
5. **JumpPointCache.** Only if maps grow past 64x64 and pathfinding shows up in profiles.
6. **Cleanup.** Remove the legacy A* (`NavGrid::search_cells_astar`, `use_jps`) once JPS has run in real matches for a while.

## History

| Date | PR | Change |
|---|---|---|
| 2026-10-08 | #1 | JPS long pathfinder (fixes the A* missing ~5% of reachable goals). Ported `Fixed`/`Sqrt` and `Geometry`. Rewrote the nav-live parity probe. Fixed CI. |

## License

`src/0ad/` is GPL-2.0-or-later. Ported files say so in their headers, but the repo `LICENSE` is MIT. The "Sort out licensing" thread is resolving this.
