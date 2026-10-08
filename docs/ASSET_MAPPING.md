# 0 A.D. → RTS0 texture, skeleton, and UV mapping

> This doc covers technique only: how 0 A.D. textures, UVs, skeletons and animation clips map onto
> RTS0 GLBs and the runtime. For coverage counts and status see
> [ASSET_MANIFEST_TODO.md](ASSET_MANIFEST_TODO.md). For running the extractor and the `assets/`
> layout see [assets/README.md](../assets/README.md).

Extractor: `tools/extract_0ad_assets.py`. Runtime material fixups: `src/graphics/assets/model_library.cpp`
(`apply_imported_material_fixups`). Clip names: `src/presentation/world_view_node.cpp` and
`src/animation/animation_controller.cpp`.

## Texture / UV (0 A.D. `CModelDef` + `model_common.fs`)

| 0 A.D. | PMD / actor | RTS0 GLB | Godot |
|---|---|---|---|
| `baseTex` | UV set 0 (tiling atlas) | `TEXCOORD_0` | albedo |
| `aoTex` | UV set 1 (unique unwrap) | sampled into `COLOR_0` | multiply albedo (vertex color) |
| player color | `baseTex.a` | baked into RGB, alpha=1 | team tint already in texture |
| OpenGL UV origin | (0,0) bottom-left | V flipped (`v' = 1-v`) | (0,0) top-left |

Never multiply `aoTex` onto the albedo **image** using UV0. That tiles the lightmap and looks like dirt or noise on armor.

Runtime: `apply_imported_material_fixups` disables `FEATURE_AMBIENT_OCCLUSION` (`model_library.cpp:120`), because Godot would otherwise sample AO with UV0. It enables `FLAG_ALBEDO_FROM_VERTEX_COLOR` only when `COLOR_0` exists (`:129`).

## Skeleton / skin

| 0 A.D. | RTS0 |
|---|---|
| `art/skeletons/*.xml` bone names (display + IBM order) | glTF joint nodes |
| PMD bind pose `CBoneState` (root-relative TRS) | rest pose + inverse bind matrices |
| PMD `SVertexBlend` 4×(u8 bone, f32 weight); `0xFF` = none | `JOINTS_0` / `WEIGHTS_0` |
| Actor prop `attachpoint` / bone index | rigid 100% weight on that joint |
| PSA `numKeys` must equal PMD bone count | clip tracks; extra bones stay bind pose |

PSA keys are already **root-relative** (`CSkeletonAnimDef` / `BuildBoneMatrices`). They are not parent-local.

## Clip map

The per-id clip and skeleton map is written to `assets/models/manifest.json` under `mapping` by the extractor (`tools/extract_0ad_assets.py`, the `manifest["mapping"]` block). Its keys:

- `clips`: RTS0 clip name → PSA key. Example: `villager.Gather → gather_wood`, `archer.Attack → archer_attack`.
- `skeleton`: RTS0 id → skeleton XML name. Example: `cavalry → horse`, `villager → biped`. An empty string means no skeleton entry.
- `player_color`, `skin`, `uv0`, `uv1_ao`: the rules in the tables above.

Clip names used by `mapping.clips`: `Idle`, `Walk`, `Run`, `Attack`, `Death`, `Heal`, `Carry`, `Build`, `Slaughter`, `Gather`, `GatherTree`, `GatherGrain`, `GatherFruit`, `GatherMeat`, `GatherRock`, `GatherOre`.

Mounted units: the rider actor (`*_r`) is composed onto the mount actor (`*_m`), as described in the extractor comments near `tools/extract_0ad_assets.py:590` and `:646`.

## Folders (0 A.D. source → RTS0 destination)

| 0 A.D. source | RTS0 destination |
|---|---|
| `art/meshes/**/*.dae.cached.pmd` + `art/actors/**/*.xml` | composed into `assets/models/{units,buildings,vegetation}/{id}.glb`; the source list is recorded in the manifest `sources` field |
| `art/skeletons/*.xml` | `assets/skeletons/<name>.xml` |
| `art/animation/**/*.cached.psa` | `assets/animations/<clip>.psa` + `.json` sidecar |
| `art/textures/skins/*.cached.dds` | PNG embedded in the GLB (player color baked) |
| `art/textures/terrain/types/*.dds` | `assets/textures/terrain/<name>.png` |

The full `assets/` tree is in [assets/README.md](../assets/README.md).

## Related technique (status details in ASSET_MANIFEST_TODO.md)

- PSA wiring for `biped_attack`, `biped_run`, `archer_attack`: section ".psa → animation wiring" in [ASSET_MANIFEST_TODO.md](ASSET_MANIFEST_TODO.md).
- Terrain splat binding (grass/dirt/rock/noise): section "terrain.gdshader splat sampling" in [ASSET_MANIFEST_TODO.md](ASSET_MANIFEST_TODO.md).
- Runtime model fallback chain: section "Runtime fallback chain" in [ASSET_MANIFEST_TODO.md](ASSET_MANIFEST_TODO.md).
