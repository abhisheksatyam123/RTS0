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

