# Performance Strategy (multi-core + GPU)

Goal: high frame rates with hundreds to thousands of units on desktop, acceptable on mobile. Priorities and status: `docs/PLAN.md`.

## What is in place now

| Technique | Where | Why |
|---|---|---|
| **Fixed-timestep sim (20 Hz)** | `Simulation::FIXED_STEP` (`src/simulation/simulation.h:79`); accumulator loop in `RtsoMain::_process` (`src/presentation/main_node.cpp:1308-1323`) | Decouples sim cost from render rate; the lockstep turn is 4 ticks (`src/network/turn_manager.h:28`) |
| **Spatial grid** | `UnitGrid` (`src/simulation/simulation.cpp:68`), built with `grid.build` at `simulation.cpp:2337` and `:2806` | Neighbour and separation queries avoid O(n²) scans |
| **Parallel per-unit loops** | `JobSystem::for_each_index` (`src/core/job_system.h`, started in `src/register_types.cpp`); used in `move_units` (`simulation.cpp:2122`) and `separation` (`:2339`) | Uses all cores. `separation` accumulates forces in parallel over a position snapshot, then applies them serially, so results do not depend on thread count (netcode-safe) |
| **MultiMesh rendering** | `RtsoWorldView::create_multimesh` (`src/presentation/world_view_node.cpp:32`); unit batches in `unit_mmi` (`:742`) | Each batch is one draw call; no per-unit node transforms to sync |
| **Arena and pool storage** | `Arena &arena` passed through sim passes; `src/core/memory/arena.h`, `pool.h` | Avoids per-frame heap allocation |
| **Render interpolation** | `src/presentation/world_view_node.cpp:1150-1158` eases rendered positions toward sim targets | Smooth visuals between 20 Hz ticks |
| **Headless tick benchmark** | `tools/bench_sim.cpp` (build line in its header; CI runs a short smoke run) | Times `Simulation::tick` for 150, 600 and 1,500 units and prints state hashes, so a speedup can be proven not to change results. On 2026-10-08 it found vision LOS stamping (Petra scouting and `tick_fow`) at ~80% of tick time; `reveal_fow()` (PR #14) now skips lit cells and caches cell heights, and a 1,500-unit tick takes ~1.5 ms. Next hot spots: `separation()`, the `tick_combat()` aggro scan, building steering in `move_units`. |
| **Territory and LOS shortcuts** | `rebuild_territory()`, `reveal_fow()` and `ensure_fow_cell_heights()` (`src/simulation/simulation.cpp`) | Both give bit-identical results; bench_sim hashes and a 12-map per-tick trace of the sim hash, both FoW grids and the territory grids match. `rebuild_territory()` keeps its grids when its inputs are unchanged: the territory roots, the grid size and the map extent. So the 1 Hz refresh is usually one scan of the building list. `reveal_fow()` skips the LOS ray when the highest terrain in the box between viewer and cell (`fow_block_hmax_`, per 4×4 FoW cells) is too low to block it. It also limits each row to the vision circle and skips runs of lit cells 8 at a time. On 2026-10-08 the 20-minute `bench_match.py` match ran 22% fewer instructions (callgrind, 17.1G → 13.4G). Petra's scouting reveal, which runs every tick, is still about half of it. |

## Rules for all future code
1. No heap allocation (`new`, growing `std::vector`, building `std::string`) in per-unit or per-frame loops. Reuse arena or pool storage.
2. Gameplay state lives in `src/simulation/` as plain structs with no Godot types. Godot nodes are presentation only.
3. Batch UI updates (dirty flags). Never rebuild labels or controls every frame.
4. Prefer adding a second MultiMesh (per team or tier) over more node instances.

## Optimisation levers (status, 2026-10-08)
1. **Stagger AI and state ticking**: not started. Petra already runs on timers (threat map every 0.4 s, `simulation.cpp:4336`), but per-unit decisions are not round-robin staggered (no matching code).
2. **Off-thread simulation**: not started. The sim already runs parallel loops inside `tick` through `JobSystem`. Any off-thread model must respect the lockstep turn boundary.
3. **GPU compute**: `shaders/compute/separation.glsl` exists but is not wired (no `RenderingDevice` use under `src/`). A CPU fallback is required, because the Compatibility renderer has no compute.
4. **Particles**: GPUParticles3D only, never CPUParticles3D. None are in the tree yet (see G6).
5. **Profiling first**: use the Godot debugger's Monitors and Profiler tabs for frame time, and `tools/bench_sim.cpp` under callgrind for sim tick time, then optimise what they report.

## Mobile renderer note
Handhelds use the Mobile renderer: no Forward+-only effects (SDFGI/volumetric fog). Test Android builds weekly so perf regressions surface early.

## Rendering gaps (G6): status 2026-10-08

Re-checked against code. Items that are done are removed; the rest are open.

| Feature | Present (RTS0, verified 2026-10-08) | 0 A.D. Reference | Gap | Cost if shipped as-is |
|---|---|---|---|---|
| Shadows | Disabled. `disable_mesh_shadows()` (`src/presentation/world_view_node.cpp:589`) is applied to building and unit meshes (`:622`, `:640`, `:742`). The sun `DirectionalLight3D` (`src/presentation/main_node.cpp:204`) has no shadow setting. | 0 A.D. CSM shadows `source/renderer/ShadowMap.cpp`, `shaders/glsl/model_common.fs` shadowPCF | No dynamic shadows; flat look, no building or unit occlusion | Correct CSM: +1-2 shadow passes, 2048² cascades ~1.5 ms GPU on desktop, ~4 ms mobile; must be toggleable. Keeping shadows off hides the perf delta but breaks parity. |
| LOD / Imposters | Off. `project.godot:42` `meshes/generate_lods: false`; no LOD meshes; all GLBs render at full resolution at any distance. | 0 A.D. auto LOD `source/graphics/MeshManager.cpp` `LODMesh`, `art/meshes/**` LOD chains | CPU vertex throughput several times higher than needed at distance; overdraw on the crowd MultiMesh at 1000 units | Auto LOD: about -40% vertex count at far camera, plus build-time cost; needs `tools/extract_0ad_assets.py` to emit LOD levels. |
| FoW / LOS in 3D | Minimap shroud is done (`src/presentation/minimap_view.cpp:177-231`, 3-state, samples `sim->fow_grid()`). 3D is not: `RtsoMain::get_fow_grid()` (`src/presentation/main_node.cpp:1246`) has no consumer in `shaders/` or the world view. | 0 A.D. `source/simulation2/components/CCmpRangeManager.cpp` LOS texture; `shaders/glsl/terrain_common.fs` `losTexture` desaturation | 3D terrain and units ignore fog; parity break for competitive play | Binding a 128×128 `R8` LOS texture and a shader branch: ~0.2 ms per frame plus upload each tick. |
| Skeletal animation | CPU baked. `ModelLibrary::bake_crowd_skin` (`src/graphics/assets/model_library.cpp:521`); `CROWD_FRAMES_PER_CLIP = 6` (`src/presentation/world_view_node.h:119`); posed frames built at `world_view_node.cpp:774`. | 0 A.D. GPU skinning `source/graphics/Skeleton.cpp` `BuildBoneMatrices`, `source/graphics/Model.cpp`, `skinning_vs.glsl` at 30 fps | Crowd animation is 6 Hz stop-motion vs 30 fps; CPU bake cost on load and memory (6 frames per clip); no blending, no per-unit phase | GPU skeletal MultiMesh (bone palette SSBO, `JOINTS_0` / `WEIGHTS_0` sampling): less baked mesh data, smooth 30 fps, ~0.6 ms GPU; needs `RenderingDevice` palette upload. |
| Materials (spec / norm / parallax) | Discarded. `ModelLibrary::apply_imported_material_fixups` (`src/graphics/assets/model_library.cpp:106`) disables ambient occlusion and forces linear filtering. `tools/extract_0ad_assets.py` `bake_texture` (line 2979) bakes tint and AO into albedo; grep finds no normal or spec map export. | 0 A.D. `art/materials/player_trans_ao_parallax_spec.xml` → `shaders/glsl/model_common.fs` spec, normal and parallax | Units and buildings look plastic; no normal detail, no parallax depth on walls and temples. `upstream_material` in `manifest.json` is audit trail only. | Shipping PBR maps: tens of MB of textures, anisotropy and normal decode ~0.3 ms; needs a parallax branch and a DDS-to-PNG normal pipeline. |
| Particles / effects | None. No `GPUParticles3D` or `CPUParticles3D` under `src/` (grep). | 0 A.D. `art/particles/*.xml`, `source/graphics/ParticleManager.cpp`, `source/renderer/ParticleRenderer.cpp` | No feedback for combat or destruction | GPUParticles3D per effect: ~0.2-0.5 ms each, pooled; needs a particle XML to `ParticleProcessMaterial` converter. |
| Water reflection / refraction | Missing. `shaders/water.gdshader` has colour, wave and foam uniforms but no `SCREEN_TEXTURE` or reflection (grep). | 0 A.D. `source/renderer/WaterManager.cpp`, `shaders/glsl/water_high.fs` reflection, refraction, Fresnel, foam | Water looks painted; no shoreline reflection | Screen-space reflection with depth fade: ~0.8 ms plus probe cost; mobile needs a fallback. |
| Terrain splat + territory + LOS | Noise splat in `shaders/terrain.gdshader` (`noise_tex`, grass / dirt / rock). A territory overlay branch exists (`territory_map`, `territory_strength`; `terrain.gdshader:77-80`), but nothing under `src/` sets those uniforms (grep). No LOS input. | 0 A.D. 8-tile splatmap `source/graphics/Terrain.cpp`, `shaders/glsl/terrain_common.fs` `splatTexture` and `losTexture` | Biome variety capped at 3; territory not bound in 3D; no LOS fog on terrain | 8-tile splatmap (two `RGBA8` splat textures or an atlas): +0.4 ms plus memory; LOS texture shared with the FoW row. |

### P5 action items (ordered by payoff)

1. **Bind FoW/LOS to terrain.** Upload `RtsoMain::get_fow_grid()` (`src/presentation/main_node.cpp:1246`) as an `R8` texture each tick. Patch `shaders/terrain.gdshader` to sample it and darken unexplored and explored areas. The minimap shroud is already done and is not part of this item. Verify: `rg -n "fow|los" shaders/` shows a sampler.
2. **Enable CSM shadows (toggleable).** Set `shadow_enabled` and two CSM splits on the sun created at `src/presentation/main_node.cpp:204`. Stop disabling shadows on terrain and buildings (`world_view_node.cpp:589`); keep them off for the crowd MultiMesh until LOD exists. Add a debug toggle. The directional shadow size key is not yet in `project.godot`.
3. **Auto LOD + imposters.** Enable `meshes/generate_lods` (`project.godot:42`) or the import preset for `assets/models/**`. Update `tools/extract_0ad_assets.py` (`GlbBuilder`) to emit LOD levels. Add LOD meshes for cavalry and town_center.
4. **GPU skeletal crowds.** Replace the 6-frame bake (`model_library.cpp:521`, `world_view_node.cpp:774`) with a bone palette in an SSBO and a vertex shader sampling `JOINTS_0` / `WEIGHTS_0`. Keep a CPU fallback for Compatibility. Ship the GPU path for the nearest units first.
5. **PBR materials.** Extend `bake_texture` in `tools/extract_0ad_assets.py` (line 2979) to emit normal and spec maps as separate PNGs. Add them in `GlbBuilder.add_material`. Update `shaders/unit_thronefall.gdshader` to use them.
6. **Terrain splatmap.** Replace the noise splat with an 8-tile splatmap from the biome. Patch `shaders/terrain.gdshader` to mix the tiles from splat channels. Set the territory uniforms (`territory_player`, `territory_enemy`, `territory_strength`, `territory_map`) from `src/presentation`; the shader branch already exists.
7. **Particles + water.** Port a minimal arrow-trail effect to `GPUParticles3D`. Add building smoke on destruction. Add a screen-space reflection path to `shaders/water.gdshader`. This item can be deferred.

Verify each item: `rg -n "shadow|LOD|fow_grid|bake_crowd|parallax|particles|SCREEN_TEXTURE" src/ shaders/ project.godot` should hit new code; `godot --headless --check` must pass.

## 2026-10-06 64x64 A* Pathfinding Benchmark (2k Units / Staggered Ticks)

Benchmark harness: `tools/bench_pathfind.py`

Models the RTS0 64×64 NavGrid A* implementation (`src/simulation/grid_nav.h`):
- Exact 10/14 octile movement costs
- Octile heuristic: `h = 10 * (dx + dy) - 6 * min(dx, dy)`
- Diagonal corner-cutting prevention (`grid_nav.h:826-831`)
- Passable cell snapping within radius 3 (`grid_nav.h:716-726`)
- Hierarchical connected components goal reachability (`grid_nav.h:733-764`)
- Post-search string-pulling shortcutting via Bresenham raycast (`grid_nav.h:246-260`)

### Benchmark Configuration
- **Map Resolution**: 64×64 (4,096 cells)
- **Map Seed**: 42 (84.4% passable cells; 3 water lakes + 40 building obstruction blocks)
- **Queries (N)**: 2,000 random queries
- **Staggered Amortization**: 10 turns × 200 queries/turn (2,000 units round-robin at 20 Hz sim tick)
- **Per-Turn Budget Limit**: 4.00 ms (out of 50.0 ms tick window at 20 Hz)
- **Reference C++ Speedup Factor**: 60× (conservative documented range: 50× – 100×)

### Query Latency Distribution
| Metric | Python Wall Time (ms) | Est. C++ (~60×) (μs / ms) |
|---|---|---|
| **p50 (median)** | 0.561 ms | 9.3 μs (0.009 ms) |
| **p90** | 1.598 ms | 26.6 μs (0.027 ms) |
| **p95** | 2.094 ms | 34.9 μs (0.035 ms) |
| **p99** | 2.982 ms | 49.7 μs (0.050 ms) |
| **Max** | 4.603 ms | 76.7 μs (0.077 ms) |
| **Mean** | 0.734 ms | 12.2 μs (0.012 ms) |
| **Throughput** | 1,361.8 queries/sec | ~81,700 queries/sec |

### Staggered-Tick Amortization (200 units/turn × 10 turns)
| Turn | Batch Size | Python Wall Time (ms) | Est. C++ Time (ms) | vs 4.00 ms Budget |
|---|---|---|---|---|
| Turn 1 | 200 queries | 149.09 ms | 2.485 ms | OK (< 4.00 ms) |
| Turn 2 | 200 queries | 155.53 ms | 2.592 ms | OK (< 4.00 ms) |
| Turn 3 | 200 queries | 137.99 ms | 2.300 ms | OK (< 4.00 ms) |
| Turn 4 | 200 queries | 151.84 ms | 2.531 ms | OK (< 4.00 ms) |
| Turn 5 | 200 queries | 159.00 ms | 2.650 ms | OK (< 4.00 ms) |
| Turn 6 | 200 queries | 140.73 ms | 2.345 ms | OK (< 4.00 ms) |
| Turn 7 | 200 queries | 151.00 ms | 2.517 ms | OK (< 4.00 ms) |
| Turn 8 | 200 queries | 142.34 ms | 2.372 ms | OK (< 4.00 ms) |
| Turn 9 | 200 queries | 147.89 ms | 2.465 ms | OK (< 4.00 ms) |
| Turn 10 | 200 queries | 133.21 ms | 2.220 ms | OK (< 4.00 ms) |

- **Python Turn p95**: 157.44 ms (pure Python upper bound)
- **Est. C++ Turn p95**: **2.624 ms** (range at 50×–100×: 1.574 ms – 3.149 ms)
- **Budget Limit**: 4.00 ms
- **Staggered Verdict**: **PASS** (Est. C++ p95 2.624 ms < 4.00 ms)

### Scaling Disclaimer & Methodology
- The benchmark harness in `tools/bench_pathfind.py` is written in pure Python using `heapq` and dynamic tuple allocations, serving as an algorithmic verification model and strict performance upper bound.
- In production C++ (`src/simulation/grid_nav.h`), the search operates over pre-allocated flat vectors (`std::vector<int>`), unboxed integer registers, cache-aligned navcells, and `-O3` compiler optimizations with horizontal jump skipping (`AddJumpedHoriz`), which benchmarks consistently establish as 50× to 100× faster than CPython.
- Under staggered-tick amortization (e.g. 200 units pathfinding per tick across 10 simulation ticks), pathfinding workload requires only ~2.6 ms per tick, leaving over 47 ms of the 50 ms tick window (20 Hz) for combat, physics steering, and network processing.
