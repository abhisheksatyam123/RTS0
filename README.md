# RTS0

Gray-box RTS starter (Age of Empires gameplay direction, Thronefall-style art direction) built with **Godot 4.3 (Pure C++ GDExtension / SoA Simulation)**.

**Cross-platform multiplayer target: Windows, Linux, Android, iOS.**
See `docs/MULTIPLAYER.md` for the netcode model, platform matrix, and the architecture rules (sim/presentation split, command pattern) that all gameplay code must follow.

## Requirements
- Godot 4.3+ **Standard edition** (no .NET SDK or Mono required): https://godotengine.org/download
- C++20 compiler (`gcc` 11+, `clang` 14+, or MSVC 2022)
- SCons (for compiling the native C++ GDExtension library)
- Standard Godot 4.3 export templates
- For device testing later: Android export templates + keystore; iOS requires macOS/Xcode

## Run / Build
1. Build GDExtension: `scons platform=linux target=template_release` (or `template_debug`)
2. Open in Godot 4.3 (Standard edition) and press **F5**, or run `python3 tools/build.py linux`

## Controls
| Input | Action |
|---|---|
| WASD | Pan camera |
| Q / E | Rotate camera |
| Mouse wheel | Zoom |
| LMB click / drag | Select unit / box-select |
| RMB | Move order (spread formation) |
| B, LMB, Shift | Build mode; place; keep placing |
| Esc | Cancel build / clear selection |

Passive gold trickles from your workers each second (placeholder economy).

## Building & CI
See **`docs/BUILD.md`** — export presets for Windows/Linux/Android/iOS, `tools/build.sh` + `tools/build.ps1` wrappers, and GitHub Actions that auto-export Windows+Linux on every push (Android via manual dispatch).

**Current status & priorities: see `docs/NEXT_STEPS.md`.**

## Architecture

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
scenes/
  main.tscn                     Root scene (res://scenes/main.tscn)
rts0.gdextension                Godot 4.3 GDExtension configuration
docs/
  0AD_PORTING.md                Porting notes from 0 A.D.
  MULTIPLAYER.md                Lockstep model and platform matrix
  PERFORMANCE.md                SoA, multi-threading, and MultiMesh rendering
  BUILD.md / NEXT_STEPS.md
```

Key invariants (from 0 A.D.):
- `Core/` never references Godot Nodes -> threadable, testable off-engine, lockstep-friendly.
- Every player action is a serializable `GameCommand` via `GameSim.Apply` -> network `TurnManager` will replay identically.
- Units = GPU `MultiMesh` (1–2 draw calls) regardless of count — thronefall look via clean lighting, not extra geometry.

## Roadmap (suggested order)
1. **Pathfinding** — A*/flow-field on the sim grid inside `GameSim` (deterministic; NOT `NavigationAgent3D`, which is engine-stateful and lockstep-hostile).
2. **Unit state machine** — `enum State { Idle, Move, Gather, Return, Attack }` in `core/`.
3. **Economy** — resource nodes (gold/wood), villager gather loop, drop-off at town center.
4. **Buildings** — production queues, rally points, population cap via houses.
5. **Combat** — HP, attack range/cooldown, projectiles, auto-aggro.
6. **Netcode M2/M3** — host/join UI, ENet command sync (see `docs/MULTIPLAYER.md`).
7. **Touch controls** — tap/drag/pinch scheme feeding the same command pipeline.
8. **Art pass (Thronefall look)** — swap primitives for low-poly assets:
   - Free: Kenney.nl, Quaternius.com; Paid: Synty Studios
   - Pastel palette, flat shading, filmic tonemap, soft shadows, subtle fog/glow in `Environment`
9. **Juice** — AnimationPlayer, sounds, minimap (`SubViewport`), health bars.

See `docs/PERFORMANCE.md` for the multi-core/GPU plan (staggered AI, off-thread ticks, compute shaders) and hot-path rules.
