# RTS0 Realistic Large-Scale In-Engine Performance Analysis & Issue Log

**Date:** 2026-10-31  
**Hardware:** Intel Core i5-1155G7 @ 2.50GHz (4 physical cores, 8 logical threads, SMT) | Intel Iris Xe Graphics GT2 (Vulkan 1.4, Forward+)  
**Test Suite:** `tools/bench_realistic_game.gd` & `tools/run_realistic_perf_test.py`  
**Engine Environment:** Godot 4.3 Forward+ Vulkan, X11 (`DISPLAY=:0`), VSync Disabled  

---

## 1. Executive Summary & Measured Metrics

We simulated two realistic 0 A.D. match scenarios in-engine under Vulkan Forward+:
1. **Realistic Skirmish Match (400 Units + Petra AI Economy & Base Building):** 4 players, Athenian/Spartan/Persian/Carthaginian civs, active gathering, building construction, and expansion.
2. **Massive Frontline Clash (2,000 Units Combat Engagement):** 2,000 mixed units (swordsmen vanguard, phalanx spearmen, archer volleys, flanking cavalry, catapult artillery, war elephants, heroes) colliding at the center with frontline camera angle.

### Empirical Performance Summary

| Metric | Skirmish Match (400 Units + Petra AI) | Massive Clash (2,000 Units Frontline) | Impact & Notes |
|---|---|---|---|
| **Framerate (FPS)** | **60.00 fps** | **14.18 fps** | Rock solid 60 fps in standard play; drops in 2,000-unit clash |
| **Frame Time (Mean)** | **16.67 ms** | **70.51 ms** | Standard 16.6ms frame vs heavy render load |
| **Frame Time (P50)** | **16.66 ms** | **54.49 ms** | Median frame time |
| **Frame Time (P95)** | **16.78 ms** | **108.91 ms** | 95th percentile during high action |
| **Frame Time (P99)** | **16.91 ms** | **245.89 ms** | 99th percentile spike |
| **Frame Time (Max)** | **18.09 ms** | **1,077.04 ms** | 1.07s hitch on initial mesh instantiation/compilation |
| **Sim Physics Time** | **0.35 ms** | **0.088 ms (mean) / 0.26 ms (max)** | **Sim CPU cost is negligible (<0.3ms), proving sim scaling!** |
| **Draw Calls / Frame** | **416** (p95: 424) | **1,198** (p95: 1,927) | MultiMesh sub-surface multiplicity causes high draw calls |
| **Primitives / Frame** | **371,294** | **9,231,001** (~9.23M triangles/frame) | Heavy geometry load on integrated Intel GPU |
| **GPU Clock (MHz)** | **1,325 MHz** (cur) | **870 MHz** (active) | GPU throttles to ~870 MHz due to vertex bandwidth & TDP |
| **Engine Memory (Static)** | **349.3 MB** | **339.5 MB** | Stable memory footprint |

---

## 2. Issues & Bottlenecks Catalogued for Next Loop

### Critical Priority (Rendering & Framerate)

- [ ] **ISSUE-REND-01: Multi-Surface MultiMesh Draw Call Multiplication**
  - *Observation:* During the 2,000-unit clash, draw calls spike to 1,927 per frame despite MultiMesh usage.
  - *Root Cause:* Canonical 0 A.D. unit models have 6–10 separate surfaces per actor (`units/athen_hero`: 9 surfaces, `units/athen_archer`: 8 surfaces). In Godot, each surface requires its own MultiMeshInstance3D draw submission.
  - *Fix Plan:* Implement texture atlasing and mesh surface merging to combine multi-material unit actors into single-surface or two-surface meshes.

- [ ] **ISSUE-REND-02: Extreme Geometry Overhead (9.23 Million Triangles / Frame)**
  - *Observation:* Frontline camera renders 9,231,001 primitives per frame, causing GPU clock throttling (1,350 MHz $\rightarrow$ 870 MHz) from memory bus saturation.
  - *Fix Plan:* Implement Distance-Based Geometry LODs (TODO-REND-02). Units further than 20m from the camera should switch to low-poly crowd meshes (100–300 triangles vs 2,500 triangles).

- [ ] **ISSUE-REND-03: First-Frame Mesh/Shader Instantiation Hitch (1,077 ms)**
  - *Observation:* On first spawn/engagement of new unit classes, a 1.07-second freeze occurs while Vulkan shaders compile and meshes load from disk.
  - *Fix Plan:* Pre-warm and compile all faction unit shaders and mesh buffers during the map loading screen.

### High Priority (Engine Lifecycle & Resource Leaks)

- [ ] **ISSUE-LEAK-01: RID and Shader Leaks at Exit**
  - *Observation:* On session exit, Godot logs:
    - `ERROR: 3 shaders of type SceneForwardClusteredShaderRD were never freed`
    - `ERROR: 12 Mesh RIDs, 37 Material RIDs, 84 Texture RIDs leaked`
    - `WARNING: 37 UniformBuffers, 32 IndexArrays, 96 VertexBuffers leaked`
    - `ERROR: Parameter "RenderingServer::get_singleton()" is null in ~CompressedTexture2D`
  - *Fix Plan:* Explicitly free all allocated RIDs in `WorldViewNode::cleanup()` and `RtsoMain` destructor before the `RenderingServer` singleton uninitializes.

### Medium Priority (Threading & GPU Compute)

- [ ] **ISSUE-THRD-01: Audit Thread-Local Buffers in Parallel Loops**
  - *Observation:* In `FogMirages::blur_fow_image`, accessing `thread_local std::vector<uint8_t> a` inside `JobSystem::for_each_index` caused a worker thread segfault (fixed by converting `a` to `static`).
  - *Fix Plan:* Audit all other files (`src/simulation/simulation.cpp`, `src/presentation/*.cpp`) to ensure no `thread_local` memory is accessed inside worker lambdas.

- [ ] **ISSUE-GPU-01: Hook Live GPU Separation into Simulation Loop (T45)**
  - *Observation:* Vulkan compute separation shader is operational in isolation (`shaders/compute/separation.glsl`, 9.41ms @ 7.6k units), but live simulation still executes CPU separation.
  - *Fix Plan:* Dispatch `GpuSeparation::compute_forces` directly during `Simulation::tick` when Vulkan Forward+ is active.
