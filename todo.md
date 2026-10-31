## Tasks
- [x] T33 [lead] Benchmark CPU & GPU utilization across unit scales (150–7,600 units) and thread configurations; create comprehensive analysis and roadmap report (`docs/CPU_GPU_UTILIZATION_REPORT.md`).
- [x] T34 [lead] Implement Stage 1 simulation scaling: optimize `distribute_around` 2-opt pairwise swaps with pre-scaled coordinates and cached squared distance updates (`src/simulation/simulation.cpp:7037`); increase `JobSystem` worker spin window `kSpinFor` to 250 µs (`src/core/job_system.cpp:22`); verified 30.6% speedup on 7.6k units (124.95 ms → 86.75 ms) with 100% bit-identical sim hashes.
- [x] T35 [lead] Parallelize `tick_combat` attack resolution (`src/simulation/simulation.cpp:17640`) via `JobSystem::for_each_index`: partitioned unit-vs-unit melee/ranged attack calculations into worker jobs, buffered damage/projectile/status parameters into `pending_unit_attacks_` scratch array, and resolved kills, alerts, loot, and projectile spawns in a deterministic sequential reduction pass; verified 100% bit-identical sim hashes and test_combat PASS.
- [x] T36 [lead] Parallelize `motion_feedback` (`src/simulation/simulation.cpp:14955`) and optimize `finish_ended_walks` (`src/simulation/simulation.cpp:15215`): parallelized non-turn step accumulation via `JobSystem::for_each_index` and optimized early branch filter in ended walks; verified 100% bit-identical sim hashes.
- [x] T37 [lead] Implement adaptive `UnitGrid` spatial resolution scaling (`src/simulation/simulation.cpp:364`): dynamically scale grid from 64x64 to 128x128 on high unit counts ($N \ge 2500$ or $N \ge 1200$ on large maps), reducing neighbor pair collision checks in mass clashing fronts; dropped 3k unit tick from 14.7ms to 4.12ms (3.5x speedup) and 7.6k unit tick to 22.17ms (3.3x speedup, 5.6x total speedup over baseline) with 100% bit-identical sim hashes.
- [x] T38 [lead] Replace per-unit virtual GDExtension calls in `RtsoWorldView::sync` (`src/presentation/world_view_node.cpp:4615`) with bulk `MultiMesh::set_buffer()` streaming: pre-allocate contiguous 20-float instance layout (`Transform3D` 12 floats + `Color` 4 floats + custom data 4 floats) and stream directly to GPU in a single bulk operation per batch, eliminating >22,000 GDExtension virtual boundary crossings per frame.
- [x] T39 [lead] Connect `shaders/compute/separation.glsl` via Godot `RenderingDevice` (`src/graphics/gpu_separation.h` & `.cpp`) for optional GPU compute offload: compiled compute pipeline, mapped double-buffered storage buffers with push constants, and integrated automatic CPU fallback when running headless or on OpenGL Compatibility renderer.
- [x] T40 [lead] Build Vulkan in-game framerate & GPU utilization benchmark tool (`tools/bench_vulkan.gd`): measured baseline rendering performance on Intel Iris Xe GT2 hardware under Vulkan Forward+; discovered that without view frustum culling, 3,000 units burned 106.22 ms/frame even with camera pointing away from the army, dropping GPU clock to 258 MHz idle due to main-thread presentation bottleneck.
- [x] T41 [lead] Implement Track 5 Frustum Culling on MultiMesh instance streaming (TODO-REND-02) in `RtsoWorldView::sync` (`src/presentation/world_view_node.cpp:4360`): enabled active camera frustum rejection with 14.0m conservative margin for units and fauna, preserved smooth visual lerping for off-screen alive units, and dropped 3,000-unit median frame time from 114.39 ms to 64.74 ms (43.4% frame time reduction / 84% FPS gain on median frames).
- [x] T42 [lead] Evaluate Track 4 GPU compute offload (`src/graphics/gpu_separation.cpp`): added runtime GLSL fallback compilation with `RDShaderSource`, resolved exit-time static teardown order in `RtsoMain::~RtsoMain()`, and benchmarked `shaders/compute/separation.glsl` on Intel Iris Xe GT2 hardware: 1,000 units in 0.64 ms, 3,000 units in 1.59 ms, and 7,600 units in 9.41 ms with zero RID leaks and 100% sim determinism.
- [x] T43 [lead] Stage, commit, and push all modified and new files for T33–T42 optimization and profiling roadmap to main branch.


## Systems
- **Hardware Profile:**
  - CPU: 11th Gen Intel(R) Core(TM) i5-1155G7 @ 2.50GHz (4 physical cores, 8 logical threads, 1 socket, NUMA node0: CPUs 0–7).
  - GPU: Intel TigerLake-LP GT2 [Iris Xe Graphics] (`8086:9a49`), driver `i915`, card1 (`/sys/class/drm/card1`), base freq 300 MHz, max freq 1,350 MHz.
  - RAM: 14 GiB total, 9.3 GiB available.
  - Profiling limits: `perf_event_paranoid` is set to 4, no passwordless sudo; PMU counters restricted. Use `/proc/stat`, `/proc/<pid>/task/`, and `/sys/class/drm/card1/` sysfs metrics for monitoring.
- **Key Code & Subsystems:**
  - `src/core/job_system.h` & `src/core/job_system.cpp`: Worker pool with main-thread participation (`parallel_width() = worker_count() + 1`). Worker spin duration set to `kSpinFor = 250us` to prevent premature `sched_yield` context switching.
  - `src/simulation/simulation.cpp`:
    - `distribute_around`: 2-opt spiral coordinate distribution for group moves; optimized with `inv = 1.0f / PUSH_SCALE` pre-scaling and cached `d[i]` array.
    - `UnitGrid` (lines 364–470): Adaptive resolution (64x64 or 128x128 via `words_per_row = (res + 63) / 64`), preventing $O(N^2)$ neighbor collision checks in dense clashes.
    - `separation()` (lines 14071–14750): Parallel push force accumulation followed by serial building push clamp.
    - `tick_combat()` (lines 17258–17920): Target acquisition (`acquire`) is parallelized (`lines 17609`); unit attack resolution (`lines 17640–17758`) parallelized via worker pass + sequential deterministic reduction pass.
    - `motion_feedback()` (lines 14955–15185): Parallel non-turn step accumulation across workers.
    - `finish_ended_walks()` (lines 15215–15225): Early exit filter on active order/action states.
  - `src/presentation/world_view_node.cpp` & `src/presentation/world_view_node.h`: Bulk `MultiMesh::set_buffer()` streaming with contiguous 20-float instance stride.
  - `src/graphics/gpu_separation.h` & `src/graphics/gpu_separation.cpp`: Godot `RenderingDevice` compute pipeline for `shaders/compute/separation.glsl`.
  - `tools/bench_sim.cpp`: Headless simulation benchmark with `--scenario <N>` and `--workers <N>` support.
  - `tools/measure_performance.py`: Profiling script sampling per-core CPU and GPU sysfs frequencies during simulation runs.
  - `docs/CPU_GPU_UTILIZATION_REPORT.md`: Profiling report and 5-track optimization roadmap.
- **Build & Verification Commands:**
  - Build GDExtension library: `scons platform=linux target=template_debug -j8`
  - Compile bench_sim:
    ```bash
    g++ -std=c++20 -O2 -Isrc -c tools/bench_sim.cpp -o /tmp/bench_sim.o
    g++ /tmp/bench_sim.o src/simulation/simulation.os src/simulation/rms_generator.os src/core/job_system.os src/network/turn_manager.os src/network/net_host.os -o bin/bench_sim
    ```
  - Run benchmark: `./bin/bench_sim --scenario 3000 --ticks 100`
  - Run test suite / parity: `python3 tools/check_parity.py --unit netcode` (17/17 PASS), `python3 tools/check_parity.py --unit petra` (34/37 PASS).
- **Constraints & Rules:**
  - Lockstep multiplayer requires bit-identical `sim_hash` and `state_fnv64` hashes across all thread configurations. Parallel worker passes must use deterministic reductions.

## Log
- 2026-10-31 20:40 [lead] T43: Staged, committed, and pushed all modified and new files for T33–T42 optimization and profiling roadmap to origin/main.
- 2026-10-31 15:15 [lead] T40: Built Vulkan in-engine framerate profiling tool (tools/bench_vulkan.gd) with sysfs GPU frequency tracking. Measured 1,000 to 3,000 unit baselines in Vulkan Forward+ mode: found 106.22 ms/frame baseline on 3k units regardless of camera angle because all units streamed to MultiMesh buffers every frame.
- 2026-10-31 15:45 [lead] T41: Implemented Track 5 camera view frustum culling in RtsoWorldView::sync with 14.0m conservative boundary margin; decoupled off-screen lerp history from on-screen visibility. Dropped 3,000-unit median frame time from 114.39 ms to 64.74 ms (43.4% speedup) and 1,000 units to 47.86 ms.
- 2026-10-31 16:15 [lead] T42: Resolved SPIR-V runtime compilation in GpuSeparation with RDShaderSource fallback, fixed teardown lifecycle, and measured Vulkan compute separation: 1,000 units in 0.64 ms, 3,000 units in 1.59 ms, and 7,600 units in 9.41 ms on Intel Iris Xe GT2. Verified 100% bit-identical sim hashes and clean exit.
- 2026-10-31 14:30 [lead] Resumed task; initiated T40-T42 to monitor Vulkan in-game framerates, implement MultiMesh frustum culling (Track 5), and evaluate GPU compute offload (Track 4).
- 2026-10-24 17:30 [lead] Completed parity milestones T0a–T32 (animations, selection overlays, formation auras, naval queues, netcode determinism, mobile safe insets).
- 2026-10-31 09:30 [lead] T33: Built bench_sim with custom scenario and worker scaling (--workers N). Measured CPU/GPU utilization on 11th Gen i5-1155G7: found 2.02x scaling ceiling on 3k units (16.7ms @ 8t vs 33.8ms serial), CPU utilization drops to 29.8% at 7.6k units due to serial Amdahl phases in tick_combat/motion_feedback/UnitGrid fixed 64x64 resolution, and GPU sits idle at 300MHz. Authored complete 5-track optimization roadmap in docs/CPU_GPU_UTILIZATION_REPORT.md.
- 2026-10-31 10:15 [lead] T34: Implemented Stage 1 simulation scaling: cached pairwise distance squares and pre-scaled coordinate math in distribute_around, reducing 1117-unit spread from 402ms to 187ms; tuned JobSystem spin window to 250us to eliminate sched_yield context switch storms. Extreme 7.6k unit battle tick reduced from 124.95ms to 86.75ms (30.6% speedup), CPU core utilization lifted from 29.8% to 38.7%, with 100% bit-identical deterministic sim hashes across all scenarios.
- 2026-10-31 11:00 [lead] Pruned completed legacy tasks from todo.md; focused backlog on future optimization tracks T35–T39.
- 2026-10-31 11:45 [lead] T35: Parallelized tick_combat attack execution via JobSystem::for_each_index with PendingUnitAttack scratch buffer and deterministic sequential reduction pass. 7.6k unit tick dropped to 73.19ms; 3k units down to 14.5ms (2.1x speedup over serial); 100% bit-identical sim hashes; test_combat 31/31 PASS.
- 2026-10-31 12:15 [lead] T36: Parallelized motion_feedback non-turn step accumulation across workers and reordered early exit filters in finish_ended_walks. Verified 100% bit-identical sim hashes across thread counts.
- 2026-10-31 13:00 [lead] T37: Implemented adaptive UnitGrid spatial resolution scaling (dynamically select 64x64 or 128x128 based on unit count and map density). Dropped 3k unit tick from 14.7ms to 4.12ms (3.5x speedup) and 7.6k unit tick from 73.28ms to 22.17ms (3.3x speedup, 5.6x total speedup over baseline). 100% bit-identical sim hashes across all thread counts.
- 2026-10-31 13:30 [lead] T38: Replaced per-unit virtual GDExtension calls in RtsoWorldView::sync with bulk MultiMesh::set_buffer() streaming (contiguous 20-float layout). Completely eliminated >22,000 virtual GDExtension boundary crossings per frame.
- 2026-10-31 14:00 [lead] T39: Created GpuSeparation (src/graphics/gpu_separation.h & .cpp) wiring shaders/compute/separation.glsl through Godot RenderingDevice compute pipeline with automatic fallback to CPU when running headless or with Compatibility renderer.
