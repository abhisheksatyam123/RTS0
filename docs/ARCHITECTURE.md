# RTS0 Architecture

Deterministic RTS core + Godot 4.3 presentation built with pure C++20 GDExtension and data-oriented Structure-of-Arrays (SoA) simulation.

## Layout

```
src/
  register_types.h / .cpp       GDExtension module entry point & class registration
  simulation/
    simulation.h / .cpp         Deterministic SoA simulation (units, combat, gathering, formations, FoW)
    sim_types.h                 Sim types, enums (Civs, Biomes, Units, Buildings), stats tables
  presentation/
    main_node.h / .cpp          UI menus, HUD, game orchestration, command dispatch, audio
    world_view_node.h / .cpp    MultiMesh 3D rendering (units, buildings, resources, health bars)
    camera_rig.h / .cpp         RTS camera rig (pan, zoom, rotate, terrain raycast)
    minimap_view.h / .cpp       Interactive 2D minimap with FoW shroud & frustum overlay
    debug_overlay.h / .cpp      In-game performance metrics and unit/fps counters
    selection_rect.h / .cpp     2D box selection marquee
    sim_node.h / .cpp           Node wrapping deterministic simulation ticks
  animation/
    animation_controller.h/.cpp Skeletal / clip animation manager
    psa_loader.h / .cpp         0 A.D. PSA animation format loader
    skeleton.h / .cpp           Skeleton bone hierarchy & transforms
  graphics/assets/
    model_library.h / .cpp      Mesh library & GLB/procedural asset caching
    assets.h / .cpp             Texture and resource loading utilities
    procedural.h / .cpp         Procedural unit/building geometry generators
  network/
    turn_manager.h / .cpp       Deterministic lockstep turn manager & replay recording
    net_host.h / .cpp           ENet networking host wrapper
    net_types.h                 Network packet and peer structures
  core/
    job_system.h / .cpp         Multi-threaded worker pool
    memory/soa_storage.h        Structure-of-Arrays columnar unit storage
    memory/arena.h              Frame and tick linear arena allocators
    memory/pool.h               Fixed-size object pools
    diagnostics/logger.h/.cpp   Diagnostics file/console logger
    diagnostics/perf_tracker.*  Performance profiler and tick timers
  0ad/                          Vendored 0 A.D. C++ reference code
shaders/
  unit_thronefall.gdshader      Flat ramp + outline
  foliage_wind.gdshader         Tree sway (VERTEX wind)
  terrain.gdshader              3-way splat + territory tint + LOS fog
  water.gdshader                Water surface shading

scenes/
  main.tscn                     Root scene
  env.tres                      Environment configuration
```

## Performance & Invariants

- **Structure-of-Arrays (SoA)**: Entity attributes (positions, velocities, HP, targets, states) are contiguous arrays in memory (`SoAStorage` & `Arena`), maximizing CPU L1/L2 cache hit rates.
- **Pure C++ GDExtension**: No C# / Mono runtime overhead.
- **Separation of Sim and Presentation**: Simulation logic never references Godot nodes.
- **Rendering**: MultiMesh instances render thousands of units in a handful of draw calls.

### Fidelity & Vendor Boundary (2026-08-29)

> **Audit:** `docs/REMAINING_GAPS.md` G1-G10 is source of truth for parity. `docs/PORTING_TODO.md` all `[x]` are MVP stubs (combat ~80%, pathfinding 5%, territory 15%, market 20%, auras 10%, formation 25%, UI 40%, assets 35%, network 3-5%, Petra 10%). Native SoA is `src/simulation/simulation.cpp:4194`; vendor `src/0ad/` is reference only (`SConstruct:28`).

- **SConstruct:28 — Vendor not built**  
  ```python
  for root, dirs, files in os.walk("src"):
      if "0ad" in root.split(os.sep):
          continue  # 0ad vendor is reference only — not built
      for f in files:
          if f.endswith(".cpp"):
              sources.append(os.path.join(root, f))
  ```  
  `src/0ad/` is excluded from the GDExtension build. It requires 0 A.D. lib stubs, SpiderMonkey JS, and `Fixed` toolchain. See `src/0ad/README.md:11` — `These files are reference + future build target; current SoA rewrite in src/core + src/simulation uses same algorithms but with arena (64B bump) + SoA cache lines for full control.` Compare via `diff -u ../0ad/source/simulation2/helpers/Grid.h src/simulation/grid_nav.h`.

- **SoA vs CCmp + JS — Single-class collapse**  
  0 A.D. has `31 CCmp*` (`CCmpObstruction`, `CCmpPathfinder`, `CCmpTerritoryManager`, `CCmpVision`, …) + `84 JS` components (`UnitAI.js` 6k+ FSM, `Market.js:42`, `Auras.js`, `Formation.js`, `Trader.js`, `Heal.js`, `Promotion.js`, `TerritoryDecay.js`, `ai/petra/` 32 modules …) dispatched via `Engine.QueryInterface`.  
  RTS0 collapses them into a single `class Simulation` (`src/simulation/simulation.h:40` `class Simulation`, `src/simulation/simulation.cpp:4194` lines) with SoA columnar storage (`src/core/memory/soa_storage.h:21` `UnitSoA` `64B` aligned `pos_x/z`, `vel_x/z`, `target_x/z`, `anchor_x/z`, `hp/max_hp`, `kills:uint16_t`, `rank:uint8_t`, `status_effects`, `waypoint` planned via `grid_nav.h:200` `WaypointPath::MAX_WP=64`). This maximizes cache hits (`tick_combat:1853` grid `visit_radius`, `move_units:1253`, `separation:1432`) but means `PORTING_TODO.md` `[x]` is a **feature checklist** (button exists) not a **parity checklist** (math matches). `REMAINING_GAPS.md` is the parity list.

- **Float vs Fixed.h:408 — Determinism gap**  
  0 A.D. uses `CFixed_15_16` (`src/0ad/maths/Fixed.h:408` `Fixed<15,16>`, `isqrt64`, `Sincos_approx`/`atan2_approx` with overflow semantics) for deterministic lockstep across x86 vs ARM.  
  RTS0 `Simulation` uses `float` (`sim_types.h`, `soa_storage.h:21` `float pos_x/z`, `vel_x/z`, `speed`, `attack_range`, etc., `simulation.cpp:1253` `dx * inv` `sqrtf`, `separation:1432` `cosf`/`sinf` hash). This is why `HashSerializer` OOS would diverge on cross-platform `float` (`src/network/turn_manager.cpp:183` `compute_sim_hash` is FNV-1a `pos+hp` only, vs 0 A.D. `HashSerializer.h/.cpp` full `StdSerializer` hash). `grid_nav.h:5` notes `Vendor boundary: src/0ad/simulation2/helpers/Grid.h, HierarchicalPathfinder.cpp:865, LongPathfinder.cpp:400 are REFERENCE ONLY.`  
  **Decision preserved:** `docs/MULTIPLAYER.md` `## Netcode model` chose **host-authoritative snapshots** for MVP (co-op/wave defense, hundreds of units, mobile jitter tolerance, avoids `Fixed` rewrite). Lockstep door stays open via the **sim/presentation split** (`core/` pure logic, `presentation/` visuals, serializable `NetCommand` `MoveCommand(unitIds[],target)` idiom, `TURN_DURATION=0.200f` 5 turns/sec already lockstep-shaped in `src/network/turn_manager.h:30`). If ranked 1v1 PvP is added, port `Fixed.h:408` and `StdSerializer` + `HashSerializer` fully and make `turn_manager.cpp` hash cover all SoA fields.

- **Vendor lineage — What was pulled**  
  - `src/0ad/maths/Fixed.h/.cpp`, `Sqrt.h/.cpp`, `MathUtil.h`, `Matrix3D.h/.cpp`, `Vector2D/3D.h` — fixed-point math (reference)  
  - `src/0ad/simulation2/helpers/Grid.h:412`, `Pathfinding.h:85`, `HierarchicalPathfinder.h:177/.h:353`, `HierarchicalPathfinder.cpp:865`, `LongPathfinder.cpp:400` (octile), `VertexPathfinder.h/.cpp`, `Geometry.h/.cpp`, `Rasterize.h/.cpp` — hierarchical pathfinding (reference)  
  - `src/0ad/network/NetClientTurnManager.cpp:188`, `NetServer.cpp:457`, `NetServerTurnManager.cpp`, `NetMessages.h`, `lobby/XmppClient.cpp`, `ps/CStr.h` — networking (reference)  
  - Build driver: `tools/build.py` `scons` → `godot --headless --import && --export-release`. `SConstruct:28` is the boundary; `src/simulation/simulation.cpp:4194` is the active runtime; `src/0ad/` stays for `diff -u` and future Phase 0-8 ports (see `REMAINING_GAPS.md` Phase 0 `grid_nav.h`, `territory_grid_`, SoA `xp/waypoint`, `tools/check_parity.py`).

