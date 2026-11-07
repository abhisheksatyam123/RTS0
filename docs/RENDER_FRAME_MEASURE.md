# Render frame-time measurement (zoomed-out / zoomed-in battle)

`tools/bench_vulkan.gd` spawns N spearmen on a fixed seed, places the camera
(`zoomed-out` or `zoomed-in`), warms up, then records per-frame wall times and
the viewport draw-call count. It prints p50/p95/p99/max frame ms and draw calls
per frame.

Numbers from the software renderer (lavapipe) are relative only. Compare runs
on the same machine, same build settings, and the same command line.

## Command

From the repo root, with a built `bin/librtso.linux.template_debug.x86_64.so`
(copy of the release build; the `.gdextension` loads the debug name):

```sh
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
xvfb-run -a -s "-screen 0 1280x720x24" \
  /home/claude/godot/Godot_v4.3-stable_linux.x86_64 --path . \
  --rendering-driver vulkan --rendering-method forward_plus \
  -s tools/bench_vulkan.gd -- \
  --units 1000 --camera zoomed-out --frames 300 --warmup 60 --seed 42 \
  --json "$SCRATCH/out.json"
```

Arguments (all optional):

| flag | default | meaning |
|---|---|---|
| `--units` | 1000 | spearmen spawned |
| `--camera` | center | `zoomed-out` (zoom 120), `zoomed-in` (zoom 25), `center` (35), `away` |
| `--zoom` | preset | override the zoom for the chosen camera |
| `--frames` | 300 | measured frames (p99 needs at least a few hundred) |
| `--warmup` | 60 | frames discarded before measuring |
| `--seed` | 42 | match seed |
| `--json` | none | absolute path; writes the full result including the sorted frame list |

The last line of stdout is `JSON_RESULT: {...}` with `p50_ms`, `p99_ms`,
`draw_calls_median`, `draw_calls_max`, and the other summary fields.

## Required setup

Rendering runs need the `pixelart` user config key off. Before the run, set it
to `false` in `~/.local/share/godot/app_userdata/RTS0/config/user.cfg`
(back the file up first and restore it afterwards):

```sh
U=~/.local/share/godot/app_userdata/RTS0/config/user.cfg
cp "$U" "$SCRATCH/user.cfg.bak"
printf 'pixelart = "false"\n' >> "$U"
# ... run the benchmark ...
cp "$SCRATCH/user.cfg.bak" "$U"
```

## Protocol

Run each camera at 1000 and 3000 units, three times each, and compare the
spread between repeats before reading any difference as real. Keep the seed,
frame count, and warmup identical across the compared builds.
