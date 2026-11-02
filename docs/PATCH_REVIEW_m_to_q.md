# Review: `rts0_tracks_m_to_q.patch`

Applied to `8d7f99a60` (uncommitted working tree), built with `scons target=template_debug -j8`,
benchmarked with `tools/bench_realistic_game.gd` on an 8-core Vulkan Forward+ machine (Godot 4.3).
Every claim below is backed by a command output or a `file:line`.

## P1 — Blocker: patch does not compile

`src/presentation/world_view_node.cpp:5609`

```cpp
mix_prop((uint64_t)sim->current_map_biome());
```

`Simulation::current_map_biome()` returns `const std::string &` (`src/simulation/simulation.h:875`).
Error: `invalid cast from type 'const std::string' to type 'uint64_t'`.

Local workaround used for testing (hash the bytes like the map-id line above it):

```cpp
for (char c : sim->current_map_biome()) mix_prop((uint8_t)c);
```

## P2 — Bench harness is nonreproducible; the "regression" it reports is its own camera path

`tools/run_realistic_perf_test.py` defaults to `--camera dynamic`. Same build, same seed, same
1500 units:

| build | camera | fps | draws | prims |
|---|---|---|---|---|
| patched | dynamic | 1.9 | 371 | 297,870 |
| patched | dynamic | 8.5 | 1,236 | 3,818,108 |
| **base (patch stashed)** | dynamic | **6.6** | **1,312** | **3,582,412** |

The base build shows the same collapse, so `dynamic` camera is the variance source, not the patch.

With `--camera clash` (fixed), 9 interleaved runs of three builds:

| build | fps (3 runs) | draws | prims |
|---|---|---|---|
| base | 54.5, 52.5, 52.5 | 451–452 | 583k–590k |
| patched | 54.7, 54.0, 53.3 | 450–452 | 575k–588k |
| expC (see P4) | 54.2, 53.5, 55.3 | 450–451 | 571k–583k |

**No measurable regression.** Recommendation: make the bench deterministic (fixed camera profile,
median of N runs), or it will keep producing false regressions.

## P3 — Leak detection reports 0 leaks while Godot leaks RIDs and shaders at exit

Runner output: `Leaks Detected: 0`.
`logs/game_20261101_041308.log` at exit:

```
ERROR: 2 resources still in use at exit
ERROR: 2 RID allocations of type 'N10RendererRD14TextureStorage7TextureE' were leaked at exit.
ERROR: 2 RID allocations of type 'N10RendererRD15MaterialStorage6ShaderE' were leaked at exit.
ERROR: 2 shaders of type SceneForwardClusteredShaderRD were never freed
WARNING: 4 RIDs of type "Texture" were leaked.
WARNING: ObjectDB instances leaked at exit
ERROR: Parameter "RenderingServer::get_singleton()" is null.   (x2)
```

The detector only scans a subset of patterns. Please parse `RID allocations`, `never freed`,
`resources still in use`, `ObjectDB instances leaked`, and the null `RenderingServer` error.

## P4 — Static-props rebuild runs ~every frame while fog animates

`world_view_node.cpp:5611` mixes `los_.generation()` into `props_key`, and `fog_mirage.cpp`
increments `gen_` on **every** `interpolate()` step, so the "static props dirty" key changes each
frame while the fog is settling.

Measured with temporary instrumentation (`RTS0_PROF_SYNC=1`, 1500 units, clash):

```
[PROF] sync avg 129.2 ms | props 0.1 ms | props rebuilds/frame 0.93
```

The rebuild itself is cheap (0.1 ms), and removing `los_.generation()` from the key ("expC",
keyed only when `los_.settled()`) gave no fps change (54.2/53.5/55.3 vs 54.7/54.0/53.3).
It is wasted work, not a regression — but it defeats the whole point of the dirty check.

## P5 — `MultiMesh::set_buffer` is used without a stride assertion (correctness risk)

Projectile buffers are sized `n * 12` (`world_view_node.cpp:5532-5540`) and their MultiMeshes are
created with `create_multimesh(arrow, false)` → `set_use_colors(false)` (`world_view_node.cpp:314,574`),
so the 12-float stride is valid today. Fauna uses `n * 16`. Godot requires the buffer to match
`instance_count * (12 | 16)` exactly; a future `set_use_colors(true)` on those MMs silently
reinterprets the buffer and produces garbage transforms. Suggest a debug assert on
`buf.size() == count * (use_colors ? 16 : 12)`.

## P6 — `job_system.cpp:197`: `nq` can still be 0 → division by zero

```cpp
const uint32_t nq = (uint32_t)std::min(width, chunks);
const uint32_t base_per_q = (uint32_t)chunks / nq;   // line 198
```

`count == 0` returns early and `chunks >= 2` for the parallel path, so the `min()` change itself is
safe and is a genuine improvement. It does not remove the pre-existing hazard that `width` (and now
`nq`) is 0 if `parallel_width()` returns 0. Use `std::max<size_t>(1, ...)`.

## P7 — Benchmarks are not comparable across checkouts (gitignored assets)

`assets/models` has 2,086 tracked `.glb` but 8,403 untracked/gitignored `.png` textures (1.4 GB);
`assets/` is 5.1 GB in this workspace vs 3.8 GB in a fresh `git worktree` of the same commit.
A/B runs must be taken in the canonical checkout, or CI needs an explicit asset fetch step.

## What I ran

- `scons target=template_debug -j8` — base and patched .so both build (after P1 fix).
- `tools/run_realistic_perf_test.py` — 4 full runs (patched x2, base x2, plus control with patch stashed).
- `godot ... tools/bench_realistic_game.gd --units 1500 --frames 180 --mode clash --camera clash`
  — 9 interleaved runs across base / patched / expC.
- `perf record -e cpu-clock` — CPU never exceeded ~13% overall (CPU-bound ruled out; the bad runs
  are GPU-bound: GPU clock dropped to 398–677 MHz while drawing 1.1M–3.8M prims).
