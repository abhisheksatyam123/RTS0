# 0 A.D. → RTS0 texture, skeleton, and UV mapping

## 2026-09-30 verification addendum (scripts, not guesses)

- Manifest: **507 models / 1223 anims / 83 skels / 652 terrain** vs public.zip
  **4280 PMD** (GLB/PMD 11.8%; mesh-basename overlap 987/4213 = 23.4%;
  actor-XML overlap 1354/5674 = 23.9%). `python3 tools/audit_assets.py` → exit 0
  (242 unit/fauna, 16 static hulls, 0 missing wanted clips).
- Canonical silhouettes: **23/23 units + 14/14 buildings + 4/4 fauna ship unique GLBs**
  (full matrix in `docs/ASSET_MANIFEST_TODO.md`). No `cavalry.glb` stand-in for distinct
  silhouettes; the old `model_library.cpp:616` alias block no longer exists — current
  chain is `model_library.cpp:622-633` direct + `civ_` prefix-strip + procedural.
- Civ variants: 205/299 unit + 182/182 building keys shipped; the 94 missing combos
  (e.g. `athen_elephant`) have no civ-folder actor in `public.zip` and resolve via
  prefix-strip to the base-unit GLB.
- PSA: `biped_attack` (29f/46b), `biped_run` (24f/46b), `archer_attack` (74f/46b) each
  verified extract-map (`tools/extract_0ad_assets.py:625-626,647`) → manifest →
  `assets/animations/*.psa` on disk → `load_clip`
  (`src/animation/animation_controller.cpp:35-36,44-45,53-54`). Caveat: crowd renderer
  bakes GLB-embedded `AnimationPlayer` clips (`src/presentation/world_view_node.cpp:749`);
  `AnimationController` is the CPU sampling path — same clips, no Godot headless bake run.
- Terrain: `shaders/terrain.gdshader:60-62` samples `grass_tex`/`dirt_tex`/`rock_tex`,
  bound `src/graphics/assets/assets.cpp:62-64` to existing PNGs; 653 terrain PNGs on disk.
  Single global triple — no per-biome switching yet (`current_map_biome_` unconsumed by
  `load_terrain_material`).

---

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

## 2026-08-29 Parity Audit (G6+G7)

Audited against `0 A.D. public.zip` (`binaries/data/mods/public/public.zip`) vs `assets/models/manifest.json` and runtime fallback/placeholder paths. See `src/graphics/assets/model_library.cpp:616` fallback chain, `src/presentation/world_view_node.cpp:190` fauna placeholder bug, `shaders/terrain.gdshader:12-14` unused territory uniforms, `shaders/unit_thronefall.gdshader:1-60` baked-only (no spec/norm).

### Coverage Table

| Category | 0AD Archive | RTS0 Shipped | Gap | Next export target |
|---|---|---|---|---|
| Units skeletal (PMD) | ~800 `art/meshes/*.dae.cached.pmd` in `public.zip` | 24 GLBs in `assets/models/manifest.json` (`archer`, `cavalry`, `deer`, `healer`, `ram`, `spearman`, `swordsman`, `villager` + fallbacks) | ~776 PMDs not exported; 16+ UnitTypes still fallback to procedural or alias (`model_library.cpp:616` maps camel→cavalry, war_dog→deer, etc.) | Export missing GLBs: `camel`, `elephant`, `war_dog`, `chariot`, `scythed_chariot`, `carnyx`, `worker_elephant`, `archer_maiden`, `bolt_shooter`, `fishing_boat`, `trireme`, `merchant_ship` (see list below) |
| Buildings structural | ~600 structural actors/meshes `art/actors/structures/**` + `art/meshes/structural/**` in `public.zip` | 11 GLBs (`barracks`, `farmstead`, `fortress`, `house`, `market`, `ruins`, `storehouse`, `temple`, `town_center`, `wall`, `wall_tower`) in `manifest.json` | ~589 not shipped; `gate`, `wonder`, `dock`, `corral` alias or procedural (`model_library.cpp:633-642` corral→farmstead, dock→storehouse, gate→wall, wonder→town_center) | Export: `dock`, `corral`, `gate`, `wonder` (+ variants: `gate_athen`, `wallset`) and proper `wall` segments |
| Vegetation | ~50 flora `art/actors/flora/**`, `art/actors/geology/**`, `art/actors/fauna/**` | 5 GLBs (`tree`, `pine`, `palm`, `gold_vein`, `stone_mine`) | ~45 missing; fauna has no real meshes | Export: `boar`, `wolf` (fauna) + more tree variants; fix `world_view_node.cpp:190` where `fauna_mm[DEER|BOAR|SHEEP|WOLF]` are placeholder vegetation meshes (`tree`/`pine`/`gold_vein`/`stone_mine`) |
| UI session (DDS/PNG) | ~150 `art/textures/ui/session/**`, `art/textures/ui/icons/**` in `public.zip` | 16 icons in `assets/textures/ui/icons/**` (food/wood/metal/stone/pop + emblems/athenians) shipped; `assets/textures/ui/**` otherwise sparse | ~134 DDS icons not converted (auras, minimap glyphs, phase emblems) | Convert & ship: `ui/session/auras/*`, `ui/session/minimap-*`, `ui/session/panel_phase_emblems/*`; add `tools/extract_0ad_assets.py` UI icon pipeline |
| Animations (PSA) | ~60 `art/animation/**/*.psa` (`biped/*`, `quadraped/*`, `mechanical/*`) | 18 PSA sidecars in `manifest.json:animations` + `assets/animations/*.psa` (biped_idle/walk/run/attack/death, citizen_*, archer_attack, horse_*, deer_*, healer_*, ram_*) | ~42 PSA not shipped (camel, elephant, chariot, naval, wonder, building, variants) | Export: `camel_idle/walk`, `elephant_attack`, `chariot_*`, `boar/wolf` quadraped, `ship_row`, `siege_*` to reach ~35 for P5 |
| Skeletons | 71 `art/skeletons/*.xml` in `public.zip` (biped, horse, deer, camel, elephant, etc.) | 3 XML shipped (`assets/skeletons/{biped,horse,deer}.xml` per `manifest.json:skeletons`) | 68 skeletons not shipped; rider vs mount split already handled for cavalry but camel/elephant need new skeletons | Ship: `camel.xml`, `elephant.xml`, `chariot.xml`, `wolf.xml`, `boar.xml` (or at minimum next 4) |
| Textures terrain | ~50 `art/textures/terrain/types/**` (aegean, african_savanna, alpine, etc.) | 7 tiles in `manifest.json:terrain` (`alpine_snow`, `desert_sand`, `dirt`, `grass`, `medit_sand`, `rock`, `savanna_mud`) + `assets/textures/terrain/` | ~43 tiles not shipped; `shaders/terrain.gdshader:12-14` declares `territory_player/enemy/strength` uniforms but fragment never samples them (unused) | Export full tile set (8-tile splatmap) + wire `TERRAIN_TILES`/`FULL_TERRAIN` from `tools/extract_0ad_assets.py:78-90`; fix shader to sample territory/LOS |
| UI auras / minimap | `gui/session/auras/*`, `gui/session/minimap/*`, `gui/session/panel.js` emblems in `public.zip` | Missing entirely; `shaders/terrain.gdshader` LOS/territory unused; minimap uses circles not shroud | 100% gap | Ship `auras/*`, `minimap-*.png`, `panel_phase_emblems` (Village/Town/City), `formations` 14 vs 6, `emblems` 13 vs 2 |

### Concrete Missing GLBs (not in `assets/models/manifest.json:models`)

Units: `camel` (`art/actors/units/*camel*.xml`), `elephant`/`worker_elephant` (`art/actors/units/*elephant*.xml`), `war_dog`/`dog` (`art/actors/fauna/dog.xml` or `art/actors/units/*dog*.xml`), `chariot` (`art/actors/units/*chariot*.xml`), `scythed_chariot`, `carnyx` (`art/actors/units/*carnyx*.xml`), `archer_maiden`, `bolt_shooter`/`ballista`/`oxybeles` (`art/actors/units/*bolt*.xml`), naval: `fishing_boat`, `trireme`, `merchant_ship`/`ship_merchant` (`art/actors/units/*ship*.xml`, `art/actors/units/naval/**`), fauna: `boar` (`art/actors/fauna/boar.xml`), `wolf` (`art/actors/fauna/wolf.xml`).

Buildings: `dock`/`shipyard` (`art/actors/structures/*dock*.xml`), `corral` (`art/actors/structures/*corral*.xml`), `gate` (`art/actors/structures/*gate*.xml`), `wonder` (`art/actors/structures/*wonder*.xml` / `athen_wonder.xml`).

All above currently hit fallback paths in `src/graphics/assets/model_library.cpp:616-642` (units) and `633-642` (buildings) or procedural `Procedural::get_*_mesh` – visible as wrong silhouettes / capsule placeholders. Verify by `grep -n "candidates.push_back" src/graphics/assets/model_library.cpp` and `manifest.json` missing keys.

### Concrete Missing UI (not in `assets/textures/ui/`)

- `auras/*` – `art/textures/ui/session/auras/*.png` + `gui/session/auras/**` (selection aura rings, rally flag aura) – never exported.
- `minimap-*` – `art/textures/ui/session/minimap*.png`, `gui/session/minimap/MiniMap.js` 3-state shroud sprites vs RTS0 `src/presentation/minimap_view.cpp:184` territory circles.
- `panel_phase_emblems` – `art/textures/ui/session/icons/civilisations/phase_*.png` / `gui/session/top_panel/PhaseIcon.js` (Village → Town → City) vs RTS0 static `civ_emblem` only (`src/presentation/main_node.cpp:215`).
- Formations: 0 A.D. `gui/session/formations/*.xml` + `art/textures/ui/session/icons/formations/` = 14 formations; RTS0 ships 6 (`assets/textures/ui/formations/line.png` fallback per `src/presentation/main_node.cpp:128-134` load_tex).
- Emblems/civs: 0 A.D. `art/textures/ui/session/icons/civilisations/` = 13 civ emblems (athen/spart/pers/rome/cart/mace/brit/gaul/iber/maur/kush/ptol/sele); RTS0 ships 2 (`athenians.png` + fallback `swordsman.png` portrait per `src/presentation/main_node.cpp:119-126`).

### Shader / Material Gaps

- `shaders/terrain.gdshader:12-14` `uniform vec3 territory_player/enemy` + `territory_strength` declared but `fragment()` (lines 24-45) never reads them – territory tint is dead code. Also no `LOS`/`FoW` sampler (0 A.D. uses `terrain_common.fs` splatmap + `losTexture`). Current splat is 3-way noise (`noise_tex` × grass/dirt/rock) vs 0 A.D. 8-tile splatmap.
- `shaders/unit_thronefall.gdshader:1-60` is baked-only: flat ramp + outline using `INSTANCE_CUSTOM` team/hp, no `specMap`/`normMap`/`parallaxMap` sampling. Upstream materials like `player_trans_ao_parallax_spec.xml` / `player_trans_norm_spec.xml` (see `manifest.json:models[].upstream_material`) have spec/norm/parallax paths that `apply_imported_material_fixups` in `src/graphics/assets/model_library.cpp:91` disables (`FEATURE_AMBIENT_OCCLUSION=false`, `TEXTURE_FILTER_LINEAR`) and `tools/extract_0ad_assets.py:753-781` bakes away instead of shipping PBR maps.
- `src/presentation/world_view_node.cpp:190-193` `fauna_mm[FAUNA_*]` all point to vegetation meshes (`pine`, `stone_mine`, `tree`, `gold_vein`) – boar/wolf render as plants/rocks.

### How to Reproduce Counts

```bash
# 0 A.D. archive
unzip -l /home/abhi/project/0ad/binaries/data/mods/public/public.zip | grep -c "art/meshes.*cached.pmd"          # ~800
unzip -l public.zip | grep -c "art/actors/structures.*xml"                                   # ~600
unzip -l public.zip | grep -c "art/textures/ui/session"                                     # ~150
unzip -l public.zip | grep -c "art/animation.*cached.psa"                                   # ~60
unzip -l public.zip | grep -c "art/skeletons.*xml"                                           # 71
unzip -l public.zip | grep -c "art/textures/terrain/types"                                  # ~50

# RTS0 shipped
jq '.models | keys | length' assets/models/manifest.json                                     # 24
jq '.animations | keys | length' assets/models/manifest.json                                 # 18
jq '.skeletons | keys | length' assets/models/manifest.json                                  # 3 (biped, horse, deer)
ls assets/models/buildings/*.glb | wc -l                                                     # 11
ls assets/models/vegetation/*.glb | wc -l                                                    # 5
ls assets/textures/ui/icons/*.png | wc -l                                                    # ~16
jq '.terrain | keys | length' assets/models/manifest.json                                    # 7
```

### 2026-08-29 Patch G5+G6/G7 (Formation heading + Asset manifest TODO)

- **G5 Formation heading (sim):** `src/simulation/simulation.cpp:897` `cmd_move` now rotates formation offsets by `heading = atan2(tz-centerZ, tx-centerX)` with `cosH/sinH` (`rx = ox*cosH - oz*sinH`, `rz = ox*sinH + oz*cosH`) per `Formation.js`. Speed table `formation_speed_mult[7] = {1.0,1.0,1.0,0.95,0.85,1.1,1.05}` applied in `move_units:1759` (PHALANX 0.85×, WEDGE 1.1×). Armor/dmg already correct in `tick_combat:2013/2036`. See `docs/REMAINING_GAPS.md:G5` and `docs/UI_PARITY.md:DoD Formation heading`.
- **G7 Asset manifest quick win:** `docs/ASSET_MANIFEST_TODO.md` (new, 2026-08-29) lists 18 missing GLBs + fallback matrix from `src/graphics/assets/model_library.cpp:616-642` (`camel`/`chariot`→`cavalry.glb`, `war_dog`→`deer.glb`, `carnyx`→`spearman.glb`, `bolt_shooter`→`ram.glb`, `corral`→`farmstead.glb`, `dock`→`storehouse.glb`, `gate`→`wall.glb`, `wonder`→`town_center.glb`, `boar`/`wolf`→procedural). `assets/models/manifest.json` still 24 GLBs shipped; fallbacks keep `ModelLibrary::load` stable when GLBs absent. Full bake via `python3 tools/extract_0ad_assets.py --help` / `--full`.
- **G7 Shader TODO:** `shaders/terrain.gdshader:12-14` now declares `uniform sampler2D territory_map` + comment showing how `Simulation::territory_grid_` will be uploaded as `ImageTexture R8` (see patch). `shaders/unit_thronefall.gdshader` remains baked-only; spec/norm TODO tracked in this doc.
- **G6 UI:** `assets/README.md` now documents `extract_0ad_assets.py --help` and `--full` export; no C++ UI rework in this patch per scope.

### Next Export Targets (P5 order)

1. GLBs: `camel`, `elephant`, `chariot`, `war_dog`, `bolt_shooter`, `trireme`/`fishing_boat`, `dock`/`gate`/`wonder`/`corral`, `boar`, `wolf` (18 total; see `tools/extract_0ad_assets.py:40-76` ASSETS/FULL_ASSETS to extend).
2. Animations: next 10 PSA for camel/elephant/chariot/boar/wolf + naval row.
3. Skeletons: `camel`, `elephant`, `wolf`, `boar` XML next.
4. Terrain: remaining tiles from `tools/extract_0ad_assets.py:FULL_TERRAIN` + splatmap wiring.
5. UI: `auras/*`, `minimap-*`, `panel_phase_emblems`, formations 14, emblems 13 (batch DDS→PNG via new UI path in extractor).

