# 0 A.D. Vendor - Real Code Pulled

This directory vendors real 0 A.D. C++ source pulled from `../0ad/source` for RTS0 native GDExtension.

Pulled for high-performance deterministic simulation with full memory/cache control:

- `maths/Fixed.h/.cpp`, `Sqrt.h/.cpp`, `MathUtil.h`, `Matrix3D.h/.cpp`, `Vector2D.h`, `Vector3D.h` — fixed-point math (deterministic lockstep)
- `simulation2/helpers/Grid.h`, `Pathfinding.h/.cpp`, `HierarchicalPathfinder.h/.cpp`, `LongPathfinder.h/.cpp`, `VertexPathfinder.h/.cpp`, `Geometry.h/.cpp`, `Rasterize.h/.cpp` — hierarchical pathfinding (as in 0ad)
- `ps/CStr.h` — string interning

Full 0 A.D. source at `/home/abhi/project/0ad` (commit pulled). These files are reference + future build target; current SoA rewrite in `src/core` + `src/simulation` uses the same algorithms but with arena (64B bump) + SoA cache lines for full control.

Assets pulled via `tools/extract_0ad_assets.py` from `../0ad/binaries/data/mods/public/public.zip` (3.3GB):
- PMD v1-5 meshes → GLB (baked player-color AO, 64K tris total)
- DDS DXT → PNG (grass/dirt/rock)
- OGG music

Migration status and todo: `docs/MIGRATION.md`.

See `assets/models/manifest.json` for baked size_meters and `docs/0AD_ATTRIBUTION.md` for CC0.
