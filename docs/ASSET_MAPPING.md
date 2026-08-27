# 0 A.D. → RTS0 texture, skeleton, and UV mapping

Extractor: `tools/extract_0ad_assets.py`  
Runtime: `native/src/graphics/assets/model_library.cpp` (`apply_imported_material_fixups`)  
Clip names: `native/src/presentation/world_view_node.cpp`

## Texture / UV (0 A.D. `CModelDef` + `model_common.fs`)

| 0 A.D. | PMD / actor | RTS0 GLB | Godot |
|---|---|---|---|
| `baseTex` | UV set 0 (tiling atlas) | `TEXCOORD_0` | albedo |
| `aoTex` | UV set 1 (unique unwrap) | sampled into `COLOR_0` | multiply albedo (vertex color) |
| player color | `baseTex.a` | baked into RGB, alpha=1 | team tint already in texture |
| OpenGL UV origin | (0,0) bottom-left | V flipped (`v' = 1-v`) | (0,0) top-left |

Never multiply `aoTex` onto the albedo **image** using UV0. That tiles the lightmap and looks like dirt/noise on armor.

Runtime: disable `FEATURE_AMBIENT_OCCLUSION` (Godot would sample AO with UV0). Enable `FLAG_ALBEDO_FROM_VERTEX_COLOR` only when `COLOR_0` exists.

## Skeleton / skin

| 0 A.D. | RTS0 |
|---|---|
| `art/skeletons/*.xml` bone names (display + IBM order) | glTF joint nodes |
| PMD bind pose `CBoneState` (root-relative TRS) | rest pose + inverse bind matrices |
| PMD `SVertexBlend` 4×(u8 bone, f32 weight); `0xFF` = none | `JOINTS_0` / `WEIGHTS_0` |
| Actor prop `attachpoint` / bone index | rigid 100% weight on that joint |
| PSA `numKeys` must equal PMD bone count | clip tracks; extra bones stay bind pose |

PSA keys are already **root-relative** (`CSkeletonAnimDef` / `BuildBoneMatrices`). They are not parent-local.

## Unit clip map (`UNIT_CFG`)

| RTS0 id | Skeleton XML | Idle / Walk / Run / Attack / Death |
|---|---|---|
| villager | `biped.xml` (46) | citizen idle/walk, swordsman attack/death, gather_wood |
| swordsman / spearman | `biped.xml` (46) | swordsman shield set |
| archer | `biped.xml` (46) | same walk; `archer_attack` |
| cavalry | `horse.xml` (49) | `quadraped/horse_*`; rider actor `*_r` rigid-bound to horse `rider` |
| deer | `deer.xml` (29) | `quadraped/deer_*` |
| healer | `biped.xml` (46) | citizen healer idle/walk/jog + `healing_m` |
| ram | PMD 8 bones | `mechanical/rome_ram_*` |

Godot `AnimationPlayer` clip names: `Idle`, `Walk`, `Run`, `Attack`, `Death`, `Gather`, `Build`.

## Folders

`assets/models/units|buildings|vegetation/{id}.glb`  
`assets/skeletons/{biped,horse,deer}.xml`  
`assets/animations/{psa_key}.psa`  
Machine-readable copy: `assets/models/manifest.json` → `mapping`.
