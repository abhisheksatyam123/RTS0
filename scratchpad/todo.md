## Tasks
- [x] T33–T43 [lead] Completed CPU/GPU profiling and scaling roadmap: adaptive UnitGrid 128x128, parallel tick_combat, parallel motion_feedback, 20-float MultiMesh contiguous streaming, active camera frustum culling (43.4% frame time reduction), Vulkan compute separation shader dispatch (9.41ms @ 7.6k units), bench_vulkan & bench_sim tools (`docs/CPU_GPU_UTILIZATION_REPORT.md`, commit `3d8e99a10`).
- [ ] T44 [lead] Petra scouting parity closure: address `los_explored` and `petra_manage_scouting` in `src/simulation/petra_ai.h` to reach 37/37 (100%) on `python3 tools/check_parity.py --unit petra`.
- [ ] T45 [lead] Runtime GPU compute separation integration: conditionally hook Vulkan compute separation dispatch into live `Simulation::tick` / `separation()` when supported.
- [ ] T46 [lead] In-game HUD & UI ergonomics: review minimap alert pings, tech tree tooltip hover latency, and mobile touch control button hitboxes.
- [ ] T47 [lead] Skirmish scenario expansion: add dedicated battle & siege challenge presets in MatchSettings for high-unit clash playtesting.
- [x] T48 [lead] Clean and refresh docs and task index (`docs/README.md`, `docs/PERFORMANCE.md`, `todo.md`).
- [x] T49 [lead] Applied gemini-code-1793462338767.txt: SMT-aware physical core worker count JobSystem::physical_core_count() (TODO-JOB-03), octagonal bounding filter in Simulation::separation, parallelized clear_attacked_by in tick_unit_ai, and linear tonemapping in scenes/env.tres.

## Systems
- **Hardware Profile:**
  - CPU: 11th Gen Intel(R) Core(TM) i5-1155G7 @ 2.50GHz (4 physical cores, 8 logical threads, 1 socket, NUMA node0: CPUs 0–7).
  - GPU: Intel TigerLake-LP GT2 [Iris Xe Graphics] (`8086:9a49`), driver `i915`, card1 (`/sys/class/drm/card1`), base freq 300 MHz, max freq 1,350 MHz.
  - RAM: 14 GiB total, 9.3 GiB available.
  - Profiling limits: `perf_event_paranoid` is set to 4, no passwordless sudo; PMU counters restricted. Use `/proc/stat`, `/proc/<pid>/task/`, and `/sys/class/drm/card1/` sysfs metrics for monitoring.
- **Key Code & Subsystems:**
  - `src/core/job_system.h` & `src/core/job_system.cpp`: Worker pool with main-thread participation (`parallel_width() = worker_count() + 1`). Worker spin duration set to `kSpinFor = 500us` and `kWatchFor = 2000us`. Worker pool bounded to physical cores via `JobSystem::physical_core_count()` (`phys - 1` workers + 1 main thread) to eliminate SMT hyperthread cache thrashing (TODO-JOB-03).
  - `src/simulation/simulation.cpp`:
    - `distribute_around`: 2-opt spiral coordinate distribution for group moves; optimized with `inv = 1.0f / PUSH_SCALE` pre-scaling and cached `d[i]` array.
    - `UnitGrid` (lines 364–470): Adaptive resolution (64x64 or 128x128 via `words_per_row = (res + 63) / 64`), preventing $O(N^2)$ neighbor collision checks in dense clashes.
    - `separation()` (lines 14071–14750): Parallel push force accumulation followed by serial building push clamp.
    - `tick_combat()` (lines 17258–17920): Target acquisition (`acquire`) is parallelized; unit attack resolution (`lines 17640–17758`) parallelized via worker pass + sequential deterministic reduction pass.
    - `motion_feedback()` (lines 14955–15185): Parallel non-turn step accumulation across workers.
    - `finish_ended_walks()` (lines 15215–15225): Early exit filter on active order/action states.
  - `src/presentation/world_view_node.cpp` & `src/presentation/world_view_node.h`: Bulk `MultiMesh::set_buffer()` streaming with contiguous 20-float instance stride; active camera frustum culling with 14.0m conservative boundary margin.
  - `src/graphics/gpu_separation.h` & `src/graphics/gpu_separation.cpp`: Godot `RenderingDevice` compute pipeline for `shaders/compute/separation.glsl` with runtime `RDShaderSource` GLSL compilation fallback.
  - `tools/bench_sim.cpp`: Headless simulation benchmark with `--scenario <N>` and `--workers <N>` support.
  - `tools/bench_vulkan.gd`: In-engine Vulkan framerate & sysfs GPU frequency benchmark.
  - `tools/measure_performance.py`: Per-core CPU and GPU sysfs frequency sampling utility.
  - `docs/CPU_GPU_UTILIZATION_REPORT.md`: Comprehensive 5-track profiling report and scaling curves.
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
- 2026-10-31 21:30 [lead] T49: Applied gemini-code-1793462338767.txt: added JobSystem::physical_core_count() bounding default workers to physical cores (TODO-JOB-03), added octagonal bounding filter in Simulation::separation, parallelized clear_attacked_by in tick_unit_ai, updated tonemapping in scenes/env.tres, recompiled GDExtension and bench_sim; verified 100% bit-identical sim hashes and PASS on test_combat, test_job_system, and check_parity netcode.
- 2026-10-24 17:30 [lead] Completed parity milestones T0a–T32 (animations, selection overlays, formation auras, naval queues, netcode determinism, mobile safe insets).
- 2026-10-31 09:30 [lead] T33: Built bench_sim with worker scaling. Profiled 11th Gen i5-1155G7 across 150-7.6k units; authored 5-track optimization roadmap in docs/CPU_GPU_UTILIZATION_REPORT.md.
- 2026-10-31 10:15 [lead] T34: Optimized distribute_around 2-opt coordinates and increased JobSystem kSpinFor to 250us (30.6% speedup at 7.6k units).
- 2026-10-31 11:45 [lead] T35: Parallelized tick_combat attack execution with deterministic reduction pass (test_combat 31/31 PASS).
- 2026-10-31 12:15 [lead] T36: Parallelized motion_feedback non-turn step accumulation; optimized finish_ended_walks early exit filters.
- 2026-10-31 13:00 [lead] T37: Implemented adaptive UnitGrid (64x64/128x128); dropped 3k unit tick from 14.7ms to 4.12ms (3.5x speedup) and 7.6k unit tick to 22.17ms (5.6x total speedup).
- 2026-10-31 13:30 [lead] T38: Replaced per-unit virtual GDExtension calls with bulk MultiMesh::set_buffer() 20-float streaming.
- 2026-10-31 14:00 [lead] T39: Created GpuSeparation compute shader pipeline via RenderingDevice with CPU fallback.
- 2026-10-31 15:15 [lead] T40: Built tools/bench_vulkan.gd with sysfs GPU frequency tracking; measured 106.22 ms/frame Vulkan baseline.
- 2026-10-31 15:45 [lead] T41: Implemented Track 5 camera frustum culling with 14m margin; dropped 3k unit frame time by 43.4% (114.39ms -> 64.74ms).
- 2026-10-31 16:15 [lead] T42: Resolved SPIR-V runtime compilation in GpuSeparation; verified 7.6k unit compute dispatch in 9.41ms on Intel Iris Xe GT2.
- 2026-10-31 20:40 [lead] T43: Staged, committed, and pushed all modified and new files for T33–T42 optimization roadmap to origin/main (commit 3d8e99a10).
- 2026-10-31 21:00 [lead] T48: Cleaned and updated docs/README.md, docs/PERFORMANCE.md, todo.md, and scratchpad/todo.md with current status and optimization links.
