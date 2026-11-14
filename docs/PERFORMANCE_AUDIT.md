# Performance Audit: techniques checklist

Status of each performance technique that fits a data-oriented RTS like this one. Written 2026-11-07 against `origin/main` at 7d21897a (round 9 merged).

Status values: **done** (in place and used), **partial** (some of it is in place, the rest is missing), **missing** (not in the code), **not measured** (in place but no number we trust).

Companion docs: [PERFORMANCE.md](PERFORMANCE.md) (what is in place and the rules for new code), [REALISTIC_PERF_ANALYSIS.md](REALISTIC_PERF_ANALYSIS.md) (2026-10-31 in-engine numbers), [CPU_GPU_UTILIZATION_REPORT.md](CPU_GPU_UTILIZATION_REPORT.md).

## Summary

- Simulation: the data layout and spatial structures are in place and paid off (the 2026-10-15 late-game work cut worst turn 120 ms to 52 ms with the same hash). The gaps are a per-tick time budget for AI and paths, cache-miss measurement, and a p99 tick number in the stress run.
- Rendering: crowds have three detail tiers, MultiMesh instancing covers most of the world, and shadows and fog are bounded. The gaps are auto-LOD and visibility ranges for props and buildings, any device detection, and dynamic quality that reacts to frame time.
- Measurement: frame and tick benches exist, but they report different percentiles, and no run covers zoomed-in versus zoomed-out battles of 1000+ units.
- 0 A.D. comparison: there is no like-for-like run. The baseline files compare RTS0 with itself. See the last section.

## Rendering

| Technique | Status | Where | Notes / measured impact |
|---|---|---|---|
| LOD (model detail by distance) | partial | `src/presentation/world_view_node.cpp:5006-5007` (crowd tiers: under 20 m full, 20-50 m decimated, beyond 50 m lowest); `load_mesh` decimation at `world_view_node.cpp:116-184`; constants `world_view_node.h:432-437` | Crowd tiers exist. Godot's automatic LOD generation on import is not used. Buildings and props have one mesh each. |
| Visibility ranges (hide small detail far away) | missing | none (`visibility_range` appears nowhere in `src/`, `scenes/` or `tools/`) | Props, flora decals and small details draw at any distance. Cheap to add with `GeometryInstance3D.visibility_range_*`. |
| MultiMesh instancing | done | `world_view_node.h:181-358, 425-516` (trees, flora, mines, food, fish, treasures, ruins, scenery, projectiles, fauna, crowds) | Crowds stream through `MultiMesh::set_buffer()`. Minimap merge 1484 to 627 draws (#1305). Buildings are not instanced. |
| Frustum culling | done for units | `world_view_node.cpp:4932, 5006` (`in_view`, `unit_cull_radius`); off-screen units skip the crowd build | Measured earlier at 43% lower median frame time on 3,000 units (PERFORMANCE.md). Not re-measured on current main. |
| Fog culling | done for units | `world_view_node.cpp:4931` (`entity_visible`) | Hidden units skip detailed animation. Per-instance LOS brightness is at `world_view_node.cpp:666`. |
| Occlusion culling | missing | none | Low value for a top-down camera with few tall occluders. Not planned. |
| Shadow distance and cascades | done | `scenes/menu/user_settings.gd:718, 746-752` (cascade count 1-4, cut-off distance, atlas size by quality); units cast shadows only within `LOD_SHADOW_DIST` 30 m (`world_view_node.h:434`) | Cut-off distance and cascades are player options. No automatic change. |
| Draw call and material counts | partial | `src/core/diagnostics/perf_tracker.cpp:51` (`RENDER_TOTAL_DRAW_CALLS_IN_FRAME`); debug panel `scenes/menu/user_settings.gd:350-357` | Counters are visible. Nothing records a budget or fails a run when it is exceeded. |
| Pixel-art render scale | done (off by default) | `pixel_art_post.gdshader`, `pixel_art_post_depth.gdshader`; #1935/#1939/#1940 | Off by default until the Performance thread confirms cost. Not yet measured on the reference devices. |
| 3D render scale and upscale | done | `scenes/menu/user_settings.gd:621-625` (`scaling_3d_scale`, FSR or bilinear); Options "Upscale technique" | Player setting only. |
| Device auto-detect presets (Low/Med/High from GPU, RAM, screen) | missing | no `get_video_adapter`, `get_memory`, `get_processor_count` or equivalent in the repo | The Options screen has manual knobs only. First-launch detection is the next step. |
| Dynamic quality when frames drop | missing | `adaptivefps.session` (`scenes/menu/options.gd:46`) only throttles the render rate, it does not change quality | Needs a frame-time controller that lowers render scale and shadow distance, then steps back up. |
| Render interpolation | done | `world_view_node.cpp:1150-1158` | Smooth motion between 20 Hz ticks. |
| Asset streaming and prefetch | done | `ModelLibrary::prefetch` and `loads_in_flight()` (`model_library.cpp:1955`, cap 24); `session_setup.cpp:1212` (`warm_models` behind the loading page); threaded GLB reads (#1394) | First close-up draw of a new unit type used to take 80-225 ms; now prefetched. Save load first-frame stall fixed in #1545/#1214. |
| GPU compute offload for separation | removed | #1813, #1822 | Measured 9.4 ms on Intel Iris Xe, but the sim must stay bit-identical, so it was dropped. |

## Simulation (CPU, cache, determinism)

| Technique | Status | Where | Notes / measured impact |
|---|---|---|---|
| Data-oriented layout (SoA) | done for units and nav | `src/core/memory/soa_storage.h` (`UnitSoA`, included at `simulation.h:5`); `grid_nav.h` SoA navigation grid; packed per-tick arrays (`sep_obstacles_`) | The design is in place. Cache-miss counts have never been measured (see below). |
| Cache misses (cachegrind) | measured, flat | `valgrind --tool=cachegrind --cache-sim=yes` on `tools/bench_late.cpp` (Giant 512, 8 Petra, minute-30 save, 2 game minutes, `--threads 0`). `perf` is not installed in the container. | LL misses are negligible (0.8M of 237M LL refs), so the working set fits the last-level cache and the cost is L1 data misses: 174M D1 read misses. The largest share is the separation pair loop (8.9%), then `move_units` (3.5%) and `tick_gather` (2.8%). A pass that packed `builder_stand_point`'s candidates (#2019) and a flood visit record (#2026) each cut their own misses, but the wall-time gain stayed under the 5% noise floor. A dense-array rewrite of `tick_gather`'s gatherer maps was judged not worth the gather-order risk. |
| Spatial grids | done | `UnitGrid` (`simulation.cpp:364`, 64 to 128 cells per side on large maps); `nav::ObstacleGrid`; `CellSet`; `LosGrid`; 8 m capture grid (`tick_capture`) | Per-tick neighbour and capture queries no longer scan every unit or building. |
| Per-tick time budgets, leftover work spread over later ticks | partial | `BEYOND_SEARCH_BUDGET = 1000` (`simulation.h:2946`), leftover searches carried to later turns (`simulation.cpp:16277-16506`); `grid_nav.h:316` `TREE_BFS_CELLS` | Long-range tree searches are budgeted. Petra's AI is cadence-based (`ai/petra-cadence-audit.md`), not time-budgeted. There is no general "ms per tick" budget. |
| Path cost (long paths) | done | JPS in `nav/long_pathfinder.h` (#1612 fast passable lambda: Red Sea worst turn 300 to 158 ms); tree-aware routing (round 9, #1947, +15% tick, being won back by the Speed up thread) | Round 9 added cost; tracked by the Speed up pathfinding thread. |
| Allocation in hot loops | partial | rules at `docs/PERFORMANCE.md` ("Rules for all future code"); arena and pool (`src/core/memory/arena.h`, `pool.h`); per-thread path scratch | Not enforced. No allocation counter in the benches. |
| Multithreading that keeps determinism | done | `JobSystem::for_each_index` (`src/core/job_system.h`) in `move_units`, `separation`, `tick_combat`, `motion_feedback`; sequential deterministic reduction afterwards | Spin window 250 us (`job_system.cpp:22`). Parallel work passes results through buffers, so hashes stay identical. |
| Hash identity check on every perf change | done | `tools/bench_sim.cpp`, `tools/bench_late.cpp` (end hash), `tools/test_save_hash.cpp` | Any perf PR must keep the end hash. |

## Memory, load times

| Technique | Status | Where | Notes |
|---|---|---|---|
| Load time | partial | Corinthian Isthmus headless load 5.2 s, of which librtso is under 1% (`performance` notes, #1545 era) | Not tracked per map on a schedule. |
| Texture memory budget | not measured | none | Textures and atlases are not budgeted. |
| Memory growth over a long match | not measured | `tools/soak_match.cpp` checks determinism, not RSS | Needs an RSS line in the soak. |

## Stress benchmark (1000+ units, zoomed in and out)

| Item | Status | Where | Notes |
|---|---|---|---|
| Headless sim stress with 1000+ units | done | `tools/bench_sim.cpp` (150, 600, 1500 units); `stress_units.gd` (400-unit groups, 60 s) | `bench_sim` prints tick mean; `stress_units.gd` prints spawn and tick rate, not percentiles. |
| p99 tick | partial | `tools/bench_match.py` reports `tick_ms_p95` and `tick_ms_p99` (`bench_match.py:63-64`); `bench_late.cpp` prints worst turns | Percentiles exist for match runs. `bench_sim` does not print p99. |
| p99 frame time, client side | partial | `tools/bench_client.gd` prints mean, p50, p95, max (not p99), CPU side only, headless | Needs a p99 column and a GPU run on reference hardware. |
| Zoomed in versus zoomed out in one scenario | missing | none | No camera-path bench. Zoom-out cost is draw cost (LOD, fog and cull) and needs a GPU run. |
| Scripted camera path over a busy battle | missing | none | Planned with the rendering reference runs. |

## Reference numbers already on record

- Late game, Giant 512, 8 Petra Hard, 40 minutes (`docs/PERFORMANCE.md`, `performance/baseline.md`): turn mean 14.1 ms to 8.7 ms, worst 189 to 75 ms, same end hash (2141246103).
- Client frame, Giant 8-Petra minute 32, about 874 units (headless CPU side): mean 21-24 ms to about 11 ms after bake and prefetch work (October).
- Realistic in-engine run on Intel i5-1155G7 and Iris Xe (`REALISTIC_PERF_ANALYSIS.md`, 2026-10-31).

## 0 A.D. comparison

No like-for-like run exists. `performance/baseline.md` compares RTS0 with its own earlier builds. A fair comparison needs the same map, seed, player count and camera path run in 0 A.D. 0.28 and in RTS0, on the same machine, with both frame and tick times. The repo does not contain a built 0 A.D. binary, and building 0.28 in the container is a separate piece of work. Recommendation: build a 0.28 C++ engine on the reference machine and script the same battle there. Until then, any "faster than 0 A.D." claim is unverified.

## Next steps, highest impact first

1. Device auto-detect presets and dynamic quality on frame time (presentation only, no sim change).
2. Visibility ranges and LOD for props and flora (presentation only).
3. A zoomed-in and zoomed-out stress run that prints p99 tick and p99 frame time.
4. Re-run the cachegrind profile after any large sim change; the sim profile is flat, so the next gains are in presentation (see Rendering).
5. A general per-tick time budget for Petra and path work, with leftover work spread across ticks (sim change: needs hash and lockstep checks).
