# Assets — RTS0

Runtime visuals are 0 A.D. art baked into GLB files by `tools/extract_0ad_assets.py`. When a GLB is
missing, `ModelLibrary::load` falls back to procedural meshes, so the game still runs.

- Coverage and status: [docs/ASSET_MANIFEST_TODO.md](../docs/ASSET_MANIFEST_TODO.md)
- How the art maps (textures, UVs, skeletons, clips): [docs/ASSET_MAPPING.md](../docs/ASSET_MAPPING.md)
- Licensing and attribution: [docs/0AD_ATTRIBUTION.md](../docs/0AD_ATTRIBUTION.md)

## Folder layout

```
assets/
  models/
    units/           one GLB per unit or fauna id
    buildings/       one GLB per building id
    vegetation/      one GLB per flora id
    manifest.json    per-id entries (file, origin, size_meters, sources, upstream_material),
                     plus animations, skeletons, terrain and mapping (clip and skeleton map)
  animations/        <clip>.psa with a <clip>.json sidecar
  skeletons/         <name>.xml bone hierarchies from 0 A.D.
  textures/
    terrain/         terrain tiles as flat PNG files
    noise/           terrain_noise.png
    ui/              icons, emblems, formations, phases, portraits, stances, ...
  materials/thronefall/  foliage.tres, terrain.tres, unit.tres
  themes/            zeroad_theme.tres
  audio/, sounds/, music/, fonts/
```

## Running the extractor

```bash
python3 tools/extract_0ad_assets.py --help
python3 tools/extract_0ad_assets.py --all \
  --zip /path/to/0ad/binaries/data/mods/public/public.zip \
  --out assets
```

- `--all` rebuilds everything. `--full` is a hidden alias for it.
- `--only <ids>` extracts specific ids. `--class <name>` expands a named batch (see `G7_CLASSSETS` in the extractor). `--pattern` selects ids by pattern.
- `--audit` prints clip coverage against the wanted runtime states and exits. `tools/audit_assets.py` does the same check with a fuller report.
- `--zip` defaults to `../../0ad/binaries/data/mods/public/public.zip` relative to `tools/`.

What the extractor does:

```
public.zip
  art/meshes/*.dae.cached.pmd    binary meshes (PMD)
  art/actors/*.xml               actor variants, props, attach points
  art/skeletons, art/animation   skeleton XML and PSA clips
  art/textures/skins/*.cached.dds skins, converted to PNG
-> compose actor trees, bake player color and AO, emit one GLB per id
-> write assets/models/manifest.json (including the mapping block)
```

## Runtime notes

- Model loader: `src/graphics/assets/model_library.cpp` (`ModelLibrary::load`, candidate chain at `:622-654`). Procedural fallbacks live in `src/graphics/assets/procedural.h`.
- Imported models use `StandardMaterial3D`. `apply_imported_material_fixups` turns off ambient occlusion on them (`model_library.cpp:106-134`).
- Each id's real-world size and origin are in `models/manifest.json` (`size_meters`, `origin`).

## License

Art, audio and maps from 0 A.D. are **CC BY-SA 3.0** (see `assets/LICENSE`). RTS0's
code, including the Thronefall-style shaders, is **GPL-2.0-or-later** (top-level
`LICENSE`). See `docs/0AD_ATTRIBUTION.md` for what came from 0 A.D.
