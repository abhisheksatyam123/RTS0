# RTS0 Architecture

Deterministic RTS core + Godot 4.3 presentation built with pure C++20 GDExtension and data-oriented Structure-of-Arrays (SoA) simulation. This doc owns the code layout and the invariants new code must keep; status and todo live elsewhere (see [docs/README.md](README.md)).

## Layout

```
src/
  register_types.h / .cpp       GDExtension module entry point & class registration
  simulation/                   Deterministic sim; never touches Godot nodes
    simulation.h / .cpp         SoA simulation: units, combat, gathering, formations, FoW, territory, ticks
    sim_types.h                 Enums (civs, biomes, units, buildings) and stats tables
    grid_nav.h                  NavGrid: passability classes, hierarchical regions, territory blur
    nav/long_pathfinder.h       JPS long-range pathfinder (port of 0 A.D. LongPathfinder)
    nav/geometry.h              Shape/geometry helpers (port of 0 A.D. Geometry)
    aura_manager.h              Aura stacking (0 A.D. Auras.js)
    petra_ai.h                  Petra AI managers (0 A.D. ai/petra)
    rms_generator.h / .cpp      Random map generation
  network/
    turn_manager.h / .cpp       Lockstep turns, sim hash, replay record/load
    net_host.h / .cpp           Host/peer transport and NMT_* handshake
    net_types.h                 Wire enums and packet structs (append-only)
    lobby_client.h              HTTP lobby client (server: tools/lobby_server.py)
  presentation/
    main_node.h / .cpp          RtsoMain: scene lifecycle, input, order routing, HUD coordinator
    top_panel.h, production_queue.h, chat_window.h, diplomacy_dialog.h
                                HUD widgets extracted from main_node (PLAN.md P4)
    world_view_node.h / .cpp    MultiMesh rendering of units, buildings, resources, health bars
    camera_rig.h / .cpp         RTS camera (pan, zoom, rotate, terrain raycast)
    minimap_view.h / .cpp       Minimap with FoW shroud & frustum overlay
    asset_viewer.h / .cpp       In-game asset viewer
    debug_overlay, selection_rect, safe_select, popup_safe, sim_node
  animation/                    PSA loader, skeletons, clip controller
  graphics/assets/              Model library (GLB + procedural fallback), texture loading
  core/
    job_system.h / .cpp         Worker pool
    math/fixed.h                16.16 fixed point (port of 0 A.D. Fixed/Sqrt; not used by the sim yet)
    memory/                     SoA storage, arenas, pools
    diagnostics/                Logger, perf tracker
  0ad/                          Vendored 0 A.D. C++ — reference only, not built (see docs/MIGRATION.md)
shaders/                        terrain, water, foliage_wind, unit_thronefall, compute/
scenes/                         main.tscn, env.tres, menu/, session/, test scenes
tools/                          build, asset extraction, parity checks, playtest harnesses, tests
```

## Invariants

- **Sim / presentation split:** `src/simulation/` never references Godot nodes, so it can run headless and in lockstep. Presentation reads sim state and never mutates it directly.
- **Every player action is a command** routed through `TurnManager`, so lockstep, replay and OOS hashing see it. Rules for this are in [MULTIPLAYER.md](MULTIPLAYER.md).
- **Determinism:** the sim uses the seeded `SimRng` (`simulation.h`), never `rand()` or wall-clock time.
- **Wire compatibility:** enums in `net_types.h` are append-only, and the game-start gate must be respected.
- **Vendor boundary:** `SConstruct` skips `src/0ad/`. Ported code lives under `src/` and cites the 0 A.D. `file:line` it follows.
- **SoA + MultiMesh:** entity data stays in contiguous SoA columns; units render as MultiMesh instances in a handful of draw calls. Hot-path rules are in [PERFORMANCE.md](PERFORMANCE.md).
