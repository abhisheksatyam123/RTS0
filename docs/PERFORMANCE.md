# Performance Strategy (multi-core + GPU)

Goal: high frame rates with hundreds→thousands of units on desktop, acceptable on mobile.

## What is in place now

| Technique | Where | Why |
|---|---|---|
| **Fixed-timestep sim (20 Hz)** | `GameSim` + accumulator in `Main._Process` | Decouples sim cost from render rate; deterministic-friendly |
| **Spatial hash grid** | `GameSim.BuildSpatialHash` | Separation/target queries are O(1) per unit instead of O(n²) |
| **Parallel force pass (`Parallel.For`)** | `GameSim.SeparationPass` | Uses all cores above 256 units; two-phase design (parallel read-only accumulate → serial apply) keeps results identical regardless of thread count → netcode-safe |
| **MultiMesh rendering** | `WorldView` | ALL units = one `MultiMeshInstance3D` = one draw call on the GPU; no per-unit Node3D transforms to sync |
| **Zero steady-state allocations** | `_forceX/_forceZ`, cell buckets, ring pool, HUD dirty-checks | Avoids GC spikes (GC pauses = frame drops) |
| **Render interpolation** | `PrevPos → Pos` lerp by alpha | Smooth visuals between 20 Hz ticks even at 144 fps |

## Rules for all future code
1. No `new` / LINQ in hot paths (`_Process`, per-unit loops). Pool or reuse.
2. Gameplay data lives in plain structs/classes inside `core/` — never behind Godot Nodes. Nodes are presentation only.
3. Batch UI updates (dirty flags), never rebuild strings/controls every frame.
4. Prefer adding a second `MultiMesh` (per team/tier) over more node instances.

## Next steps (in order of payoff)
1. **Stagger AI/state ticking** — only N units make decisions per tick (round-robin). Cheapest big win.
2. **Off-thread simulation** — run `GameSim.Tick` on a dedicated thread with double-buffered state; main thread only reads last completed snapshot when writing GPU buffers. Do this AFTER netcode exists (host tick loop and net send share the thread).
3. **GPU compute (RenderingDevice)** — offload e.g. flocking/projectile integration via compute shaders. Caveats: not on Compatibility renderer, flaky on old mobile GPUs → must ship CPU fallback path anyway.
4. **GPUParticles3D everywhere** (never CPUParticles), texture-atlas UI, occlusion culling for dense bases.
5. **Profiling first, always**: Godot debugger → Monitors + Profiler tabs; `dotnet-trace` for C#-side hot spots. Optimize what it says, not what feels slow.

## Mobile renderer note
Handhelds use the Mobile renderer: no Forward+-only effects (SDFGI/volumetric fog). Test Android builds weekly so perf regressions surface early.
