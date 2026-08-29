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

## 2026-08-29 Rendering Gaps (G6)

Audit vs 0 A.D. `source/graphics/**`, `source/renderer/**`, `shaders/glsl/**` and RTS0 `src/presentation/world_view_node.cpp`, `project.godot`, `shaders/*.gdshader`, `src/presentation/main_node.cpp`.

| Feature | Present (RTS0) | 0 A.D. Reference | Gap | Cost if shipped as-is |
|---|---|---|---|---|
| Shadows | Disabled – `src/presentation/world_view_node.cpp:212-221` `disable_mesh_shadows()` sets `SHADOW_CASTING_SETTING_OFF` on every `GeometryInstance3D`; `src/presentation/main_node.cpp:152-159` sun `DirectionalLight3D` never enables `shadow_enabled` / CSM | 0 A.D. CSM shadows `source/renderer/ShadowMap.cpp`, `shaders/glsl/model_common.fs` shadowPCF, `art/materials/**` shadow params | No dynamic shadows; flat look, no building/unit occlusion | Correct CSM: +1-2 shadow passes, 2048² cascades ~1.5 ms GPU on desktop, ~4 ms mobile; must be toggleable. Keeping shadows off hides perf delta but breaks parity. |
| LOD / Imposters | Off – `project.godot:32` `meshes/generate_lods=false`, `meshes/light_baking=0`; no `LODMesh` or HLOD; all 24 GLBs rendered full-res at any distance | 0 A.D. auto LOD `source/graphics/MeshManager.cpp` `LODMesh` + `art/meshes/**` LOD chains, `collada/LOD.xml` | CPU vertex throughput ~3-4× higher than needed at distance; overdraw on crowd MultiMesh at 1000 units | Enabling auto LOD: -40% vertex count at far camera, + build-time cost, needs `extract_0ad_assets.py` to emit LOD1/2. False keeps import fast but hurts scale. |
| FoW / LOS sampling | Not bound – `src/presentation/main_node.cpp:911-919` `get_fow_grid()` accessor exists and `RtsoMiniMapView` draws shroud in 2D, but `RtsoWorldView::sync()` never uploads `fow_grid` to `shaders/terrain.gdshader`/`shaders/water.gdshader`; `_draw()` in `minimap_view.cpp:90-184` is 2D only, no 3D fog | 0 A.D. `source/simulation2/components/CCmpRangeManager.cpp` LOS texture + `shaders/glsl/terrain_common.fs` `losTexture` desaturation, `gui/session/MiniMap.js` 3-state shroud (unexplored/explored/visible) | 3D terrain/units ignore FoW – visible through fog; parity break for competitive play | Binding 128×128 `R8` LOS texture + shader branch: ~0.2 ms/frame sampler + desat math; needs `Simulation::FOW_GRID` → `RenderingDevice` texture upload each tick. |
| Skeletal animation | CPU baked – `src/presentation/world_view_node.cpp:292-365` `ensure_unit_batches()` + `src/graphics/assets/model_library.cpp:408-596` `capture_crowd_skin`/`bake_crowd_skin` pre-bakes `CROWD_FRAMES_PER_CLIP=6` frames per clip (`world_view_node.h:101-102`), `sync()` picks frame by `anim_time*6` (`world_view_node.cpp:520`) | 0 A.D. GPU skinning `source/graphics/Skeleton.cpp` `BuildBoneMatrices` + `source/graphics/Model.cpp` GPU vertex shader `skinning_vs.glsl` at 30 fps | Crowd anim is 6 Hz stop-motion vs 30 fps smooth; CPU bake cost on load (6× mesh dup) + memory (≈24 types×4 clips×6 frames meshes); no blending / no per-unit phase | Move to GPU skeletal MultiMesh (custom shader sampling `JOINTS_0`/`WEIGHTS_0` + bone palette SSBO): -5 MB baked meshes, +30 fps smoothness, ~0.6 ms GPU; requires `RenderingDevice` palette upload. |
| Materials (spec/norm/parallax) | Discarded – `src/graphics/assets/model_library.cpp:91-121` `apply_imported_material_fixups` disables `FEATURE_AMBIENT_OCCLUSION`, forces `FILTER_LINEAR`; `shaders/unit_thronefall.gdshader:1-60` samples no maps (flat ramp only); `tools/extract_0ad_assets.py:753-781` `bake_texture` bakes player-color/AO into albedo but drops `specMap`/`normMap`/`parallaxMap` | 0 A.D. `art/materials/player_trans_ao_parallax_spec.xml`, `player_trans_norm_spec.xml` etc. → `shaders/glsl/model_common.fs` spec+norm+parallax, `shaders/glsl/terrain_common.fs` splat | Units/buildings look plastic, no normal detail, no parallax depth on walls/temples; `upstream_material` in `manifest.json` is audit trail only | Shipping PBR maps: +~30 MB textures, + anisotropy + normal decode ~0.3 ms; must add `parallax` shader branch and DDS→PNG norm pipeline. |
| Particles / Effects | None – no `CPUParticles3D`/`GPUParticles3D`; `world_view_node.cpp` has no `art/particles/*.xml` handling; `extract_0ad_assets.py` never parses `art/particles/**` | 0 A.D. `art/particles/*.xml` + `source/graphics/ParticleManager.cpp`, `source/renderer/ParticleRenderer.cpp` (fire, smoke, arrow trails, building dust) | No feedback for combat/destruction; visual parity break | GPUParticles3D per effect: ~0.2-0.5 ms each, pooled; need particle XML→Godot `ParticleProcessMaterial` converter. |
| Water reflection / refraction | Missing – `src/presentation/world_view_node.cpp:442-461` water is flat `PlaneMesh` + `StandardMaterial3D` albedo/roughness only; `shaders/water.gdshader` (if used) has no `SCREEN_TEXTURE` / reflection probe | 0 A.D. `source/renderer/WaterManager.cpp` + `shaders/glsl/water_high.fs` reflection + refraction + Fresnel + foam | Water looks painted, no shoreline foam | Screen-space reflection + depth fade: ~0.8 ms, probe update cost; mobile needs fallback. |
| Terrain splat + LOS | 3-way noise – `shaders/terrain.gdshader:24-45` `noise_tex` × `grass/dirt/rock` `mix` via `splat = n*0.55+n2*0.45` + `smoothstep`; `territory_*` uniforms declared `12-14` but never sampled; no LOS desat | 0 A.D. 8-tile splatmap `source/graphics/Terrain.cpp` + `shaders/glsl/terrain_common.fs` `splatTexture` (RGBA tiles) + `losTexture` fog, `art/textures/terrain/types/**` up to 8 | Biome variety capped at 3, territory invisible in 3D, LOS fog missing; noise tiling obvious at altitude | 8-tile splatmap (2× `RGBA8` splat textures + 8 samplers or atlas): +0.4 ms + memory; LOS texture reuse from FoW row. |

### P5 Action Items (ordered by payoff)

1. **Bind FoW/LOS to terrain** – Upload `Main::get_fow_grid()` (`main_node.cpp:911`) as `R8` texture each tick; patch `shaders/terrain.gdshader` to sample `los_tex` and desaturate/darken `ALBEDO` (unexplored black, explored desat 0.5, visible full). Reuse same texture for minimap 3-state shroud (`minimap_view.cpp:90` `_draw()` currently draws territory circles – replace with shroud rects). Estates: 0.5 day. Verify: `_draw()` samples `fow_grid` (grep `fow_grid` appears in shader).
2. **Enable CSM shadows (toggleable)** – Set `sun.shadow_enabled=true` + `directional_shadow_mode=CSM 2 splits` in `main_node.cpp:152` `_ready()` sun block; remove `disable_mesh_shadows()` for terrain/buildings, keep for crowd MultiMesh until LOD. Add ProjectSettings `rendering/lights_and_shadows/directional_shadow/size=2048` (verify `project.godot`). Mobile: shadow toggle in `debug_overlay`. Estates: 1 day.
3. **Auto LOD + imposters** – Flip `project.godot:32` `meshes/generate_lods=true` (or at least `true` for `assets/models/**` import preset); update `tools/extract_0ad_assets.py:GlbBuilder` to emit `MSFT_lod` or Godot `LOD` accessor if available; add `_ lod` meshes for `cavalry`, `town_center`. Estates: 1.5 days (tool + re-export).
4. **GPU skeletal crowds** – Replace 6-frame `bake_crowd_skin` (`model_library.cpp:509`, `world_view_node.cpp:343-350`) with SSBO bone palette (`RenderingDevice`) + vertex shader sampling `JOINTS_0`/`WEIGHTS_0` (see `manifest.json:mapping.skin` spec). Keep CPU fallback for Compatibility. Estates: 3 days; de-risk by shipping 30 fps only for `MAX_DETAILED_UNITS=16` first.
5. **PBR materials** – Extend `tools/extract_0ad_assets.py:bake_texture` to emit `specMap`/`normMap` as separate PNGs + `GlbBuilder.add_material` with `normalTexture`/`occlusionTexture`; update `shaders/unit_thronefall.gdshader` to branch on `USE_NORMALS` (or switch crowd to `StandardMaterial3D` with maps). Estates: 2 days.
6. **Terrain splatmap** – Replace noise splat with 8-tile splatmap upload from `Simulation` biome (reuse `FULL_TERRAIN` in `extract_0ad_assets.py:84-90`); patch `shaders/terrain.gdshader` to `mix` 8 tiles via `splat_tex` channels. Wire `territory_player/enemy` uniforms in fragment (tint by building influence). Estates: 2 days.
7. **Particles + water** – Port minimal `art/particles/arrow_trail.xml` → `GPUParticles3D`, building smoke on `BuildingVis` death; water `SCREEN_TEXTURE` reflection probe. Estates: 2 days, P6 OK to defer.

Verify each item: `rg -n "shadow|LOD|fow_grid|bake_crowd|parallax|particles|SCREEN_TEXTURE" src/ shaders/ project.godot` should hit new code; `godot --headless --check` must pass.

