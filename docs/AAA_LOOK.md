# AAA look plan (lighting, materials, LOD, budget)

Goal (stan, 2026-11-03): every animation, unit, building and piece of terrain should look like a finished AAA game, with lighting and beauty, and the game should stay playable. This doc is the ordered plan for the parts no other doc owns. Shipped 0 A.D. fidelity stays in [REMAINING_GAPS.md](REMAINING_GAPS.md); the post-processing and tonemapping rows (G92, G93) and shaders belong to Rendering, and the animation audit belongs to Animal models. The performance rules are in [PERFORMANCE.md](PERFORMANCE.md).

## What exists today (checked 2026-11-03)

- **Lighting:** one directional sun with one shadow cascade out to 75 m (`scenes/main.tscn`), 300 m cut-off in `src/presentation/user_config.cpp`. No global illumination: the sky supplies ambient light (`scenes/env.tres`, `ambient_light_sky_contribution = 0.65`). SSAO, SSIL and glow are off.
- **Post:** the 0 A.D. hdr/bloom pass (G92), linear output (G93).
- **Materials:** terrain (`shaders/terrain.gdshader`: tile array, slope blend, paint, territory), water (`shaders/water.gdshader`: depth, shore foam, fresnel, reflection and refraction behind the 0.28 water options), foliage and trees (wind), player colour, specular and normal maps (`player_color.gdshader`), silhouettes.
- **LOD:** crowd tiers (`world_view_node.cpp`: full, decimated, proxy MultiMesh at distance); mesh LOD on import is off (`project.godot`).
- **Renderers:** desktop is Forward+; phones and tablets are GL Compatibility (`project.godot`, `docs/BUILD.md`).

## What the Godot 4 docs say (4.3 source)

- **SDFGI and VoxelGI** are supported only on Forward+. Neither runs on Mobile or Compatibility. SDFGI lights from dynamic lights but not dynamic occluders: units cast no indirect shadow. Its cost scales with cascades, probe ray count and how fast the camera moves, and it needs a GPU. VoxelGI needs baked data, and dynamic VoxelGI is much slower.
- **Visibility ranges (HLOD)** work on MeshInstance3D and MultiMeshInstance3D. Use the "Dependencies" fade mode for the cheapest transition; "Self" alpha-fades and costs more.
- **MultiMesh** and occlusion culling are the documented ways to cut draw calls. Mesh LOD is generated on import, which we have turned off.
- **Shadows** are the biggest cost on Compatibility too (`user_config.cpp` mobile defaults turn them off).

## Plan (ordered; each item is one PR with its own before/after numbers)

1. **Budget and counters (first).** Add the Godot rendering counters (draw calls, primitives, shadow draws) to `tools/bench_client.gd` output, and write the per-look budget table into this doc: desktop 60 fps at 1080p on a mid GPU, phones 30 fps at 720p under Compatibility. Container frame times are CPU-side only (llvmpipe), so GPU budgets are checked on a real GPU by stan; the counters are exact everywhere. *Owner: Performance.*
2. **Blob shadows and contact darkening (all renderers).** A cheap projected or decal shadow under each unit, structure and tree, sized by footprint, so objects sit on the ground. Works on Compatibility; it replaces the shadow-map cost on phones where shadows are off. *Owner: Rendering (decals), Lighting.*
3. **Terrain detail.** Macro colour variation and a detail normal blended by distance, so tiling disappears from the ground near the camera and the terrain stops looking flat. Triplanar only on cliffs. Keep the 0 A.D. tile colours as the base, so parity views are unchanged when the detail option is off. *Owner: Lighting.*
4. **Global illumination option (Forward+ desktop only).** A `Global illumination: off / low / high` setting in Options. Low = SDFGI with 1 cascade and reduced probe rays, high = 2 cascades. Off on Mobile and Compatibility; they keep the sky ambient. Default: off until item 1 measures its cost on a real GPU. Units do not occlude GI, which the docs list as a known limit. *Owner: Lighting.*
5. **Foliage and prop LOD.** Visibility ranges (Dependencies fade) on trees, props and structures, with a billboard or a decimated mesh at distance. Measure draw calls before and after. *Owner: Lighting, with Animal models for the art.*
6. **Water and shoreline.** Use the existing depth and foam; add a wave normal layer and a cheap shoreline wet band. Keep it on under Compatibility (no reflection there). *Owner: Rendering.*
7. **Look parity check.** Each item above has to look right under the pixel-art style too (the Switch to 3D pixel-art style thread). Materials use the shared colour tokens so both looks read the same lighting.
8. **Animation blending.** Blend walk, run and idle, and smooth attack and gather transitions. Owned by Animal models (the animation audit); this doc only lists it so the look work does not duplicate it.

## Rules

- Every item lists a before and after frame time and draw-call count from `tools/bench_client.gd` on the same save, and a headless check that the Compatibility renderer still loads (`tools/check_gl_shaders.py`).
- Lighting that changes the 0 A.D.-fit baseline goes behind a setting, so parity runs (G92, G93) stay reproducible.
- No new per-tick or hashed simulation state comes from this work. It is presentation only.
- Keep each item to one PR. Merge to main when the local gate passes.
