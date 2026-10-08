# Assets — RTS0 (0 A.D. baked + Thronefall fallback)

Runtime visuals are **0 A.D. art baked into GLB** (see `tools/extract_0ad_assets.py`) with
procedural primitives as fallback when a GLB is missing. Shaders stay Thronefall-style
where useful; baked models use imported `StandardMaterial3D`.

## Folder layout

```
assets/
  models/units/          villager, swordsman, archer, spearman, cavalry, healer, ram, deer
  models/buildings/      town_center, house, barracks, farmstead, storehouse, market,
                         temple, wall, wall_tower, fortress, ruins
  models/vegetation/     tree, pine, palm, gold_vein, stone_mine
  models/manifest.json   actor sources, size_meters, clips
  animations/            curated PSA clips baked into unit GLBs + sidecar copies
  skeletons/             biped.xml, horse.xml, …
  textures/terrain/      grass, dirt, rock (+ biome tiles with --all)
  music/                 theme.ogg, victory.ogg, defeat.ogg
```

Unknown ids are never written to `vegetation/` (that used to dump unit/building files there).

Quick help:

```bash
python3 tools/extract_0ad_assets.py --help     # shows --all / --full / --zip / --out
```

Regenerate after pulling 0ad:

```bash
python3 tools/extract_0ad_assets.py --all \
  --zip /home/abhi/project/0ad/binaries/data/mods/public/public.zip \
  --out assets
```

## Pipeline (`tools/extract_0ad_assets.py`)

```
0ad repo ../0ad/binaries/data/mods/public/public.zip
  art/meshes/*.dae.cached.pmd  binary meshes (source/graphics/ModelDef.cpp, PSMD, CW front)
  art/actors/*.xml              variant groups + shared variants + prop attach points
  art/textures/skins/*.cached.dds  DXT1/5 -> PNG (fixed G6 channel bug)

-> parse PMD (versions 1-5, bind-pose already), compose actor trees,
   bake player-color (alpha-mask: shaders/glsl/model_common.fs `mix(playerColor,white,tex.a)`)
   + AO multiply, downscale >1024 to 512, flip winding (CW -> CCW), center+ground normalize
-> emit single-file GLB (embedded PNG) per id + manifest.json
```

Regenerate after pulling 0ad:

```bash
python3 tools/extract_0ad_assets.py --all \
  --zip ../0ad/binaries/data/mods/public/public.zip --out assets
```

Import settings (auto on export): `filter=false, fix_handedness`, `msaa_3d=2`, textures `s3tc_bptc` desktop / `etc2_astc` Android.

## Fallback

`WorldView.cs:Make*MultiMesh` checks `Assets.IsRealModel(path)` — if a GLB is missing or
`ResourceLoader` fails (headless CI), it falls back to `Procedural` Capsule/Box/Cylinder
so the game always builds. Real models keep imported `StandardMaterial3D`; fallback uses
`unit_thronefall.gdshader` + `INSTANCE_CUSTOM` team/hp/wind.

## Scale

Targets baked into GLB (manifest `size_meters`): villager 1.7m, swordsman 1.8m,
town_center 5.2m (footprint 4), house 2.1m (footprint 1.8), tree 4.5m, gold_vein 1.8m.
`WorldView.Sync` uses `_real*` flags to pick Y offset: real units 0.02 vs 0.6 (capsule),
real buildings 0 vs size/2 (Box center).

## Phase 5 Export Expansion (G6+G7 parity)

P5 closes the asset gaps audited in `docs/ASSET_MAPPING.md` (`## 2026-08-29 Parity Audit`). The
current `assets/models/manifest.json` ships 24 GLBs / 18 PSAs / 3 skeletons; P5 targets the
18 missing GLBs (`camel`, `elephant`, `war_dog`, `chariot`, `scythed_chariot`, `carnyx`,
`worker_elephant`, `archer_maiden`, `bolt_shooter`, `fishing_boat`, `trireme`, `merchant_ship`,
`dock`, `corral`, `gate`, `wonder`, `boar`, `wolf`) + 4 skeletons + 10 PSAs + terrain tiles
and UI (`auras/*`, `minimap-*`, `panel_phase_emblems`, formations 14, emblems 13).

Run the full extractor against a local 0 A.D. checkout:

```bash
# from repo root
python3 tools/extract_0ad_assets.py --full \
  --zip /home/abhi/project/0ad/binaries/data/mods/public/public.zip \
  --out assets
# or shorthand (tool defaults to --full terrain/UI skeletons)
python3 tools/extract_0ad_assets.py --full
```

This rebuilds `assets/models/{units,buildings,vegetation}/*.glb`, `assets/skeletons/*.xml`,
`assets/animations/*.psa`, `assets/textures/terrain/*`, and `assets/textures/ui/session/**` then
rewrites `assets/models/manifest.json`. Missing ids will fall back via
`src/graphics/assets/model_library.cpp:616` until the GLB exists – the game never crashes.

> **GridNav note:** `src/simulation/grid_nav.cpp` (flow-field / `GridNav`) is simulation-only
> (pathfinding, `can_place`, footprint collision). It does **not** affect asset import or GLB
> layout – navigation meshes are generated from `Simulation::terrain_heights` at runtime, not
> baked into models. Re-exporting assets never requires rebuilding nav data.

## License

Art, audio and maps from 0 A.D. are **CC BY-SA 3.0** (see `assets/LICENSE`). RTS0's
code, including the Thronefall-style shaders, is **GPL-2.0-or-later** (top-level
`LICENSE`). See `docs/0AD_ATTRIBUTION.md` for what came from 0 A.D.
