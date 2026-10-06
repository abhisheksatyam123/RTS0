# Asset Manifest — Fallback Matrix (verified 2026-09-30)

> All numbers below were produced by scripts run on this checkout (no guesses).
> Sources: `src/simulation/sim_types.h` enums/tables, `assets/models/manifest.json`
> (`models`/`animations`/`skeletons`/`terrain`), GLB files on disk under `assets/models/`,
> `src/graphics/assets/model_library.cpp` candidate chain, `src/animation/animation_controller.cpp`
> clip wiring, `shaders/terrain.gdshader` splat sampling, `tools/check_parity.py` output.

## Headline (from `python3 tools/check_parity.py`, re-ran 2026-09-30)

- `manifest models: 507  anims: 1223  skels: 83  terrain: 652`
- `public.zip PMDs: 4280  shipped GLBs: 507  gap: 3773  coverage: 11.8%`
- HEAD `manifest.json` already has 504 models; worktree adds 3
  (`arrow_projectile`, `stone_projectile`, `fruit_bush` — all on disk, verified with `ls`).
- The 11.8% is a GLB-per-PMD ratio, NOT a silhouette gap: one RTS0 GLB composes
  many PMDs (actor + props). Mesh-basename overlap is 987/4213 = 23.4% of PMD basenames
  composed into a GLB; actor-XML overlap is 1354/5674 = 23.9%.
- `python3 tools/audit_assets.py` → exit 0, `242 unit/fauna assets, 16 static hulls, 0 missing wanted clips`.

## Canonical silhouette coverage (the G7 criterion)

- Units: **23/23 unique** (no `cavalry.glb` fallback for distinct silhouettes — `camel`,
  `elephant`/`worker_elephant`, `chariot`/`scythed_chariot`, `war_dog`, `carnyx`,
  `archer_maiden`, `bolt_shooter`, naval trio all ship their own GLB).
- Buildings: **14/14 unique** (`town_center`…`corral`, incl. `gate`, `wonder`, `dock`, `wall_segment`).
- Fauna: **4/4 unique** (`deer`, `boar`, `sheep`, `wolf`).
- Civ-prefixed variants present: 205/299 unit keys + 182/182 building keys = 387 total.
  The 94 "missing" `civ_unit` combos (e.g. `athen_elephant`, `brit_catapult`) have NO
  matching civ-folder actor in `public.zip` (checked per-folder for the keyword) — i.e.
  0 A.D. itself has no Athenian elephant actor. At runtime `ModelLibrary::load`
  (`src/graphics/assets/model_library.cpp:631-633`) strips the `civ_` prefix and loads
  the base-unit GLB (`athen_elephant` → `units/elephant.glb`), then procedural mesh.
  So every trainable (civ, type) pair resolves to a real mesh, never a wrong-silhouette alias.

## Runtime fallback chain (no wrong-silhouette alias)

`src/graphics/assets/model_library.cpp:622-633` candidates in order:
`assets/models/{category}/{id}.glb` → `units/{id}.glb` → `buildings/{id}.glb` →
`vegetation/{id}.glb` → `{id}.glb` → (units only) prefix-strip `{base}.glb`
(e.g. `athen_spearman` → `spearman.glb`) → `healer` → `villager.glb` →
`Procedural::get_*_mesh`. The old `camel→cavalry` / `war_dog→deer` alias block
referenced by the 2026-08-29 version of this doc no longer exists in the code.

## Unit matrix — 23/23 canonical ids ship a unique GLB (`UNIT_TYPE_COUNT=23`, `src/simulation/sim_types.h:116`)

| # | UnitType | model_id | GLB on disk | manifest | embedded clips Attack/Run (n) |
|---|---|---|---|---|---|
| 0 | Villager | `villager` | `assets/models/units/villager.glb` UNIQUE | yes | Attack=Y / Run=Y (15) |
| 1 | Swordsman | `swordsman` | `assets/models/units/swordsman.glb` UNIQUE | yes | Attack=Y / Run=Y (6) |
| 2 | Archer | `archer` | `assets/models/units/archer.glb` UNIQUE | yes | Attack=Y / Run=Y (15) |
| 3 | Spearman | `spearman` | `assets/models/units/spearman.glb` UNIQUE | yes | Attack=Y / Run=Y (15) |
| 4 | Cavalry | `cavalry` | `assets/models/units/cavalry.glb` UNIQUE | yes | Attack=Y / Run=Y (7) |
| 5 | Merchant Trader | `trader` | `assets/models/units/trader.glb` UNIQUE | yes | Attack=Y / Run=Y (5) |
| 6 | Catapult Artillery | `catapult` | `assets/models/units/catapult.glb` UNIQUE | yes | Attack=Y / Run=Y (4) |
| 7 | Historical Champion Hero | `hero` | `assets/models/units/hero.glb` UNIQUE | yes | Attack=Y / Run=Y (16) |
| 8 | War Trireme | `trireme` | `assets/models/units/trireme.glb` UNIQUE | yes | Attack=Y / Run=Y (5) |
| 9 | Carthaginian War Elephant | `elephant` | `assets/models/units/elephant.glb` UNIQUE | yes | Attack=Y / Run=Y (5) |
| 10 | Siege Battering Ram | `ram` | `assets/models/units/ram.glb` UNIQUE | yes | Attack=Y / Run=Y (5) |
| 11 | Temple Healer | `healer` | `assets/models/units/healer.glb` UNIQUE | yes | Attack=Y / Run=Y (6) |
| 12 | Celtic War Chariot | `chariot` | `assets/models/units/chariot.glb` UNIQUE | yes | Attack=Y / Run=Y (5) |
| 13 | Celtic War Dog | `war_dog` | `assets/models/units/war_dog.glb` UNIQUE | yes | Attack=Y / Run=Y (5) |
| 14 | Bolt Shooter Ballista | `bolt_shooter` | `assets/models/units/bolt_shooter.glb` UNIQUE | yes | Attack=Y / Run=Y (4) |
| 15 | Fishing Boat | `fishing_boat` | `assets/models/units/fishing_boat.glb` UNIQUE | yes | Attack=Y / Run=Y (8) |
| 16 | Merchant Cargo Ship | `merchant_ship` | `assets/models/units/merchant_ship.glb` UNIQUE | yes | Attack=Y / Run=Y (5) |
| 17 | Female Citizen | `female_citizen` | `assets/models/units/female_citizen.glb` UNIQUE | yes | Attack=Y / Run=Y (15) |
| 18 | Gallic Carnyx Herald | `carnyx` | `assets/models/units/carnyx.glb` UNIQUE | yes | Attack=Y / Run=Y (7) |
| 19 | Mauryan Worker Elephant | `worker_elephant` | `assets/models/units/worker_elephant.glb` UNIQUE | yes | Attack=Y / Run=Y (6) |
| 20 | Mauryan Archer Maiden | `archer_maiden` | `assets/models/units/archer_maiden.glb` UNIQUE | yes | Attack=Y / Run=Y (6) |
| 21 | Persian/Arabian Camel | `camel` | `assets/models/units/camel.glb` UNIQUE | yes | Attack=Y / Run=Y (8) |
| 22 | Seleucid Scythed Chariot | `scythed_chariot` | `assets/models/units/scythed_chariot.glb` UNIQUE | yes | Attack=Y / Run=Y (5) |

## Building matrix — 14/14 canonical ids ship a unique GLB (`BLD_TYPE_COUNT=14`, `src/simulation/sim_types.h:150`)

| # | BuildingType | model_id | GLB on disk | manifest |
|---|---|---|---|---|
| 0 | Town Center | `town_center` | `assets/models/buildings/town_center.glb` UNIQUE | yes |
| 1 | House | `house` | `assets/models/buildings/house.glb` UNIQUE | yes |
| 2 | Barracks | `barracks` | `assets/models/buildings/barracks.glb` UNIQUE | yes |
| 3 | Storehouse | `storehouse` | `assets/models/buildings/storehouse.glb` UNIQUE | yes |
| 4 | Defense Tower | `wall_tower` | `assets/models/buildings/wall_tower.glb` UNIQUE | yes |
| 5 | Farmstead | `farmstead` | `assets/models/buildings/farmstead.glb` UNIQUE | yes |
| 6 | Market | `market` | `assets/models/buildings/market.glb` UNIQUE | yes |
| 7 | Fortress | `fortress` | `assets/models/buildings/fortress.glb` UNIQUE | yes |
| 8 | Gate | `gate` | `assets/models/buildings/gate.glb` UNIQUE | yes |
| 9 | Temple | `temple` | `assets/models/buildings/temple.glb` UNIQUE | yes |
| 10 | Wonder Monument | `wonder` | `assets/models/buildings/wonder.glb` UNIQUE | yes |
| 11 | Naval Shipyard Dock | `dock` | `assets/models/buildings/dock.glb` UNIQUE | yes |
| 12 | Stone Wall Segment | `wall_segment` | `assets/models/buildings/wall_segment.glb` UNIQUE | yes |
| 13 | Livestock Corral | `corral` | `assets/models/buildings/corral.glb` UNIQUE | yes |

## Fauna matrix — 4/4 ship a unique GLB (`FAUNA_COUNT=4`, `src/simulation/sim_types.h:692`)

| # | FaunaType | model_id | GLB on disk | manifest | loader |
|---|---|---|---|---|---|
| 0 | Red Deer | `deer` | `assets/models/units/deer.glb` UNIQUE | yes | `ModelLibrary::get("units","deer")` (`src/presentation/world_view_node.cpp:267-276`, deer fallback if missing) |
| 1 | Wild Boar | `boar` | `assets/models/units/boar.glb` UNIQUE | yes | `ModelLibrary::get("units","boar")` (`src/presentation/world_view_node.cpp:267-276`, deer fallback if missing) |
| 2 | Mountain Sheep | `sheep` | `assets/models/units/sheep.glb` UNIQUE | yes | `ModelLibrary::get("units","sheep")` (`src/presentation/world_view_node.cpp:267-276`, deer fallback if missing) |
| 3 | Gray Wolf | `wolf` | `assets/models/units/wolf.glb` UNIQUE | yes | `ModelLibrary::get("units","wolf")` (`src/presentation/world_view_node.cpp:267-276`, deer fallback if missing) |

## `.psa` → animation wiring (biped_attack, biped_run, archer_attack) — PASS

All three clips verified at four levels (script output, not guesses):

| Clip | extract map (`tools/extract_0ad_assets.py`) | manifest entry | file on disk | runtime `load_clip` |
|---|---|---|---|---|
| `biped_attack` | line 626: `art/animation/biped/infantry/swordsman/attack_melee_shield_01.dae.cached.psa` | frames=29, bones=46 | `assets/animations/biped_attack.psa` (37380 B) + `.json` ✔ | `src/animation/animation_controller.cpp:36` (Biped/Attack), `:54` (Villager/Attack) ✔ |
| `biped_run` | line 625: `art/animation/biped/infantry/swordsman/run_relax_shield.dae.cached.psa` | frames=24, bones=46 | `assets/animations/biped_run.psa` (30940 B) + `.json` ✔ | `src/animation/animation_controller.cpp:35` (Biped/Run), `:44` (Archer/Run), `:53` (Villager/Run) ✔ |
| `archer_attack` | line 647: `art/animation/biped/infantry/archer/attack_ranged.dae.cached.psa` | frames=74, bones=46 | `assets/animations/archer_attack.psa` (95340 B) + `.json` ✔ | `src/animation/animation_controller.cpp:45` (Archer/Attack) ✔ |

PSA loader: `PsaAnim::load_psa` (`src/animation/psa_loader.cpp:60`, alias map `:10-31`)
resolves `archer_shoot→archer_attack` etc. and reads `assets/animations/<clip>.psa`.
`AnimationController::load_clip` (`src/animation/animation_controller.cpp:9-15`) stores
parsed `PsaAnim` per `AnimState`; rig presets wire Biped/Archer/Villager as above.
PSA sidecar totals: 1220 `.cached.psa` in `public.zip` vs 1223 extracted
`assets/animations/*.psa` and 1223 `manifest.animations` entries (superset: extractor
also bakes GLB-embedded clips — every canonical unit GLB reports `animated=true` with
`Attack=Y/Run=Y`, see matrix).

Caveat (honest): in-game crowd rendering bakes **GLB-embedded** `AnimationPlayer` clips
(`Idle/Walk/Run/Attack/Death`, `RtsoWorldView::bake_puppet_clips`,
`src/presentation/world_view_node.cpp:749`; clip select `clip_name`/`resolve_clip_fallback`
`:391-514`; `find_anim_player` `:351`), while `AnimationController`/`PsaAnim` is the
CPU-side sampling path (`sample_current_pose`, `animation_controller.h:72-74`) with no
cross-consumer call found outside the animation module. Both paths carry the same three
clips, but a headless Godot bake was NOT run here (no Godot binary assertion) — G7 asks
for "wiring verified", which the file:line chain above satisfies; full visual proof needs
a Godot run.

## terrain.gdshader splat sampling — PASS (re-verified 2026-10-01, static)

Shader side (all lines re-read this session):
- `shaders/terrain.gdshader:6-8` declares `grass_tex`/`dirt_tex`/`rock_tex`;
  `:60-62` samples all three and `:65` composites
  `grass*grass_weight + dirt*dirt_weight + rock*rock_weight` with slope
  (`:40-41`), height (`:45-47`) and noise (`:34-35,53`) weights. No referenced
  texture file is missing — every `texture(<uniform>…)` in the shader resolves
  to a bound uniform below.
- Heights reach the shader as vertex data: `src/presentation/world_view_node.cpp:919-922`
  (`rebuild_terrain` reads `sim->terrain_heights()`, `simulation.h:168`) builds the
  mesh with per-vertex heights (`v_height = VERTEX.y`, shader `:26`) and slope normals,
  so the splat weights are genuinely driven per-vertex by `terrain_heights_`.

Binder side (C++ + .tres):
- `src/graphics/assets/assets.cpp:54-67` (`load_terrain_material`) binds
  `grass_tex→savanna_grass.png` (`:62`), `dirt_tex→sand.png` (`:63`),
  `rock_tex→snow.png` (`:64`) — all three PNGs verified on disk this session
  (`ls -la`: `sand.png` 557139 B, `savanna_grass.png` 740671 B, `snow.png` 324017 B).
- `assets/materials/thronefall/terrain.tres:11-14` (preferred path — `TerrainMaterial`,
  `assets.h:23`, checked first by `try_load_mat`) binds the same three slots plus
  `noise_tex→terrain_noise.png`; the `.tres` triple
  (`grass.png`/`dirt.png`/`rock.png`) also verified on disk (manifest `terrain`
  entries `grass`/`dirt`/`rock` → `types/aegean_anatolia/*`, all present).
- Findings (honest, no missing files): C++ fallback binds only grass/dirt/rock —
  `noise_tex` (`:34-35`) and `territory_map` (`:15`) are NOT bound in `assets.cpp`
  (shader defaults apply: noise=black, territory inert). `noise_tex` IS covered via
  the `.tres`; `territory_map` (incl. `territory_player/enemy/strength`, `:12-14`)
  is unbound anywhere (`grep noise_tex|territory_map src/ scenes/` → only shader +
  `.tres:11`). No texture file referenced by the shader is MISSING on disk.
- Per-biome breadth: `assets/textures/terrain/*.png` = 653 files,
  manifest `terrain` = 652 entries (delta = `splat.png`, helper with no manifest entry;
  zero manifest entries lack a file — script-checked both directions).
- Known limit (unchanged): single global triple, no per-biome switching —
  `current_map_biome_` (`src/simulation/simulation.h:727`, set
  `src/presentation/main_node.cpp:3178` via `set_map_info`, `simulation.h:180-183`;
  map-type table `src/simulation/simulation.cpp:657-663`) is not consulted by
  `load_terrain_material`. Splat math itself is PASS; biome switching is a follow-up.

Verify: `grep -n "texture(grass_tex\|texture(dirt_tex\|texture(rock_tex" shaders/terrain.gdshader`
(`:60-62,65`); `sed -n '54,67p' src/graphics/assets/assets.cpp`;
`ls -la assets/textures/terrain/{savanna_grass,sand,snow}.png`.

## manifest.json vs public.zip PMD diff — runnable check (`tools/audit_assets.py --pmd-diff`)

New this session: `tools/audit_assets.py --pmd-diff` (read-only; counts manifest
`models` vs `.pmd` entries in
`/home/abhi/project/0ad/binaries/data/mods/public/public.zip`, i.e. `art/meshes/`
inside the zip — there is no unpacked `art/` dir in the 0 A.D. checkout) plus the
mesh-stem and actor-XML overlap. Real output, ran 2026-10-01:

```text
[pmd-diff] zip: /home/abhi/project/0ad/binaries/data/mods/public/public.zip
[pmd-diff] manifest models: 507  public.zip PMD: 4280  gap: 3773  coverage: 11.8%
[pmd-diff] mesh-basename overlap: 987/4213 (23.4% of PMD stems composed into a GLB)
[pmd-diff] actor-XML overlap: 1354/5674 (23.9% of actor XMLs referenced as sources)
```

- Method: PMD stems = `art/meshes/**/*.dae.cached.pmd` basenames minus the
  `.dae.cached.pmd` suffix (4280 entries → 4213 unique stems); mesh stems = manifest
  `sources[]` entries with the `mesh:` prefix (`mesh:props/xxx.dae` → `xxx`, 987 unique);
  actor hits = exact `actor:art/actors/….xml` source-path matches (1354 of 5674).
- Worktree vs HEAD: HEAD `manifest.json` has 504 models, worktree 507
  (`arrow_projectile`, `stone_projectile` under `models/units/`,
  `fruit_bush` under `models/vegetation/` — all three GLBs + manifest entries
  verified on disk; they pre-date this session, no extraction was run for them here).
- Remaining PMDs are overwhelmingly civ-variant actor combinations (13 civs × shared
  prop/mesh pool), prop-library meshes referenced but not top-level units, and
  gaia/flora/geology variety — covered by the prefix-strip + procedural fallback
  (`model_library.cpp:629-656`), and by 205+182 civ-prefixed GLB variants shipped.

## G7-H2 push (2026-10-05) — HIGH-VISIBILITY class batch, +31 models (507 -> 538)

> Ran `python3 tools/extract_0ad_assets.py --class g7h2` (new `--class`/`--pattern`
> options on the extractor; `--class g7h2` expands to 31 curated ids, see
> `G7_CLASSSETS` in `tools/extract_0ad_assets.py`). Baseline audit first:
> `242 unit/fauna assets, 16 static hulls, 0 missing` (RC=0), 507 GLB / 4280 PMD.

| # | Class (HIGH-VISIBILITY only) | New ids (31) | GLB dir | Clips verified |
|---|---|---|---|---|
| 1 | Map fauna — animated gaia animals placed on maps (15) | `goat`, `zebra`, `chicken`, `crocodile`, `giraffe`, `rabbit`, `gazelle`, `bear`, `cow`, `pig`, `lion`, `lioness`, `donkey`, `fox`, `hawk` | `models/units/` | all 5 (`Idle/Walk/Run/Attack/Death`) — actor-harvested, `audit_assets.py` ok |
| 2 | Landmarks / civic extras — wonders + athenian set + lighthouse (11) | `pyramid`, `pyramid_minor`, `ishtar_gate`, `greek_stoa`, `greek_stoa_great`, `theater`, `gymnasium`, `stoa`, `prytaneion`, `propylaea`, `lighthouse` | `models/buildings/` | static (no skeleton — correct, buildings carry no clips) |
| 3 | Flora variety — biome trees (5) | `oak`, `acacia`, `baobab`, `bamboo`, `apple` | `models/vegetation/` | static (correct, flora carries no clips) |

Before/after (all numbers from rerun scripts, this session):

```text
[pmd-diff] BEFORE: manifest models: 507  public.zip PMD: 4280  gap: 3773  coverage: 11.8%
[pmd-diff] BEFORE: mesh-basename overlap: 987/4213 (23.4%)  actor-XML overlap: 1354/5674 (23.9%)
[pmd-diff] AFTER:  manifest models: 538  public.zip PMD: 4280  gap: 3742  coverage: 12.6%
[pmd-diff] AFTER:  mesh-basename overlap: 1047/4213 (24.9%)  actor-XML overlap: 1417/5674 (25.0%)
audit BEFORE: 242 unit/fauna assets, 16 static hulls, 0 missing wanted clips, RC=0
audit AFTER:  257 unit/fauna assets, 16 static hulls, 0 missing wanted clips, RC=0
manifest: models 507->538  anims 1223->1279 (+56 fauna PSAs)  skels 83->89 (+6: goat/zebra/chicken/crocodile/giraffe/bear/cow/pig/fox/hawk/lion/donkey refs)
GLB on disk: units 245->260  buildings 256->267  vegetation 6->11  (260+267+11 = 538)
```

- Method: new ids registered in `ASSETS` + `UNIT_IDS`/`BUILDING_IDS`/`VEGETATION_IDS`
  + `UNIT_CFG` fallbacks + `SKELETONS`/`ANIMATIONS` rows (all PSA `numKeys ==`
  skeleton bones == mesh bones, checked per id — 0 A.D. `CSkeletonAnimDef` ENSURE).
  Actor `<animation>` tags are harvested first (`collect_actor_clips`); the
  `UNIT_CFG` rows are fallback only. New fauna also added to `_KIND_WANTED_CLIPS`
  so the audit verifies their 5 clips (257 rows, 0 missing).
- Verify: `python3 tools/audit_assets.py` (RC=0) + `python3 tools/audit_assets.py --pmd-diff`
  + `ls assets/models/units/goat.glb assets/models/buildings/pyramid.glb assets/models/vegetation/oak.glb`.
- Skipped with reason (NOT converted this session, cap 20-40 kept at 31):
  - Civ-skin duplicates of already-converted models (e.g. per-civ horse color
    variants, `*_top` tree LOD splits, `decal_*` ground decals) — same silhouette,
    no new coverage.
  - `special/old/*` + `campaigns/*` settlement actors (outdated/duplicate geometry).
  - `particle/*`, `props/*` weapon/armor library parts (already composed INTO the
    538 GLBs as `sources[]`, not standalone models), `fungi/*` + `temp/*` stubs.
  - Fauna without a key-matched skeleton+PSA triple this session (e.g.
    `elephant_african_bush` 58-bone mesh vs available PSAs, `hippopotamus` no
    matching PSA keys) — would fail the bone-count ENSURE; left for a later pass
    with per-id rigging work.

## G7-Q push (2026-10-05) — HIGH-VISIBILITY class batch, +32 models (567 -> 599)

> Ran `python3 tools/extract_0ad_assets.py --class g7q` (new `g7q` entry in
> `G7_CLASSSETS`: warship hulls + smith/forge/field/harbor statics + hound,
> elephant and chariot variants, 32 curated ids). Baseline audit first:
> `268 unit/fauna assets, 16 static hulls, 0 missing` (RC=0), 567 GLB / 4280 PMD.

| # | Class (HIGH-VISIBILITY only) | New ids (32) | GLB dir | Clips verified |
|---|---|---|---|---|
| 1 | Warship hulls — bireme/quinquereme/pentekonter + han + nordic/celtic boats (14) | `bireme`, `bireme_rome`, `quinquereme`, `quinquereme_rome`, `pentekonter`, `trireme_han`, `bireme_han`, `ship_knarr`, `ship_snekkja`, `ship_karvi`, `skiff`, `rowboat`, `merchant_han`, `fishing_han` | `models/buildings/` (hulls) + `models/units/pentekonter.glb` (animated) | `pentekonter` actor-harvested `Idle/Walk/Run` (+`Attack`/`Death` fallbacks, 3-bone `ship_one_row` rig); 0-bone hulls static-correct (rest pose, no invented sail clips) |
| 2 | Smith / forge / field / harbor statics (13) | `blacksmith_brit`, `blacksmith_kart`, `forge_germ`, `forge_han`, `field_plot`, `orchard_plot`, `crannog`, `wooden_tower`, `scout_tower`, `tower_bolt`, `tower_artillery`, `stable_elephant`, `kennel` | `models/buildings/` | static (no skeleton — correct, buildings carry no clips) |
| 3 | Hound / elephant / chariot animated variants (5) | `wolfhound`, `mastiff_gaia`, `elephant_bush`, `elephant_forest`, `chariot_han` | `models/units/` | all 5 (`Idle/Walk/Run/Attack/Death`) — actor-harvested, `audit_assets.py` ok |

Before/after (all numbers from rerun scripts, this session):

```text
[pmd-diff] BEFORE: manifest models: 567  public.zip PMD: 4280  gap: 3713  coverage: 13.2%
[pmd-diff] BEFORE: mesh-basename overlap: 1113/4213 (26.4%)  actor-XML overlap: 1503/5674 (26.5%)
[pmd-diff] AFTER:  manifest models: 599  public.zip PMD: 4280  gap: 3681  coverage: 14.0%
[pmd-diff] AFTER:  mesh-basename overlap: 1215/4213 (28.8%)  actor-XML overlap: 1632/5674 (28.8%)
audit BEFORE: 268 unit/fauna assets, 16 static hulls, 0 missing wanted clips, RC=0
audit AFTER:  274 unit/fauna assets, 16 static hulls, 0 missing wanted clips, RC=0
manifest: models 567->599  anims 1302->1316 (+14: hound/elephant-bush/forest PSAs)  skels 96->98 (+4: wolfhound/elephant_bush/elephant_forest/ship_one_row refs; mastiff/chariot rigs reused)
GLB on disk: units 271->277 (+6: pentekonter/wolfhound/mastiff_gaia/elephant_bush/elephant_forest/chariot_han)  buildings 285->311 (+26)  vegetation 11->11  (277+311+11 = 599)
```

- Method: new ids registered in `ASSETS` + `UNIT_IDS` (6 animated) /
  `BUILDING_IDS` (26 statics) + `UNIT_CFG` fallbacks + `SKELETONS`/`ANIMATIONS`
  rows (mesh bones == skeleton bones == PSA `numKeys` per id — 0 A.D.
  `CSkeletonAnimDef` ENSURE: wolfhound 40==40==40, mastiff 42==42==42,
  elephant bush/forest 58==58==58, han chariot 15==15==15 on the `chariot` rig,
  pentekonter 3==3==3 on `ship_one_row`). Actor `<animation>` tags are harvested
  first (`collect_actor_clips`); the `UNIT_CFG` rows are fallback only. New
  animated ids also added to `_KIND_WANTED_CLIPS` so the audit verifies their
  clips (274 rows, 0 missing).
- Why these 32: per the pre-extract gap survey, each hull mesh (`kart/rome_ship`
  bireme/quinquereme, `han_ship_*`, `germans/ship_*`, `celt_skiff/rowboat`) and
  each building mesh (`brit/kart_blacksmith`, `germans/forge`, `han_forge`,
  `field_plot`, `plot_orchard`, `brit_crannog`, `brit_wooden/scout_tower`,
  `kart_tower_bolt/artillery`, `kart_stable_elephant`, `celt_kennel`) had zero
  `mesh:` composition in any of the 567 GLBs; `wolfhound` (40-bone),
  `mastiff_no_spikes` gaia dog (42-bone), `elephant_african_bush/forest`
  (58-bone) and `han_chariot` (15-bone) each pass the bone-count triple with
  dedicated PSAs+skeletons. Already-composed silhouettes
  (`athen/kart/rome_ship_trireme`, `celt/kart/maur/ptol` merchants,
  `celt/kart` docks, `farmstead/corral/gate/wall_tower` civ sets, `war_dog`
  mastiff-A, asian/forest elephants inside `worker_elephant`/`elephant`) were
  NOT re-extracted as dupes.
- Verify: `python3 tools/audit_assets.py` (RC=0) + `python3 tools/audit_assets.py --pmd-diff`
  + `ls assets/models/units/wolfhound.glb assets/models/units/elephant_bush.glb assets/models/units/chariot_han.glb assets/models/buildings/bireme.glb assets/models/buildings/kennel.glb assets/models/buildings/forge_han.glb`.
- Skipped with reason (NOT converted this session, cap 25-35 kept at 32):
  - Civ-skin duplicates of already-converted hulls/buildings (e.g. per-civ
    `trireme_ram`/`bireme_scout`/`pentekonter_scout` actors reusing the same
    `kart/rome/hele_ship_*` meshes, `*_top` tree/ship LOD splits, `decal_*`
    ground decals, `found_*_chains` construction frames) — same silhouette,
    no new coverage.
  - `destruction_*` / `destruct_*` damage-state actors + `fndn_*` foundation
    frames (transient build/destroy states, not placeable models).
  - `particle/*`, `props/*` sail/oar/shield/prop library parts (already composed
    INTO the 599 GLBs as `sources[]`, not standalone models), `campaigns/*` +
    `special/old/*` duplicates, `fungi/*` + `temp/*` stubs.
  - `rome_trireme_corvus` / `rome_quinquereme_corvus` (corvus-bridge prop swap on
    the already-composed `rome_ship_trireme/quinquereme` hulls — prop-level
    difference only, deferred to a fittings pass).

## Low-risk extraction — NOT RUN this session (nothing to close, reported honestly)

- Capped-extraction criterion was "distinct silhouettes falling back". Re-checked
  this session: all 23 unit + 14 building + 4 fauna canonical ids have their OWN
  GLB in the manifest AND on disk (`ls` loop over all 41 paths → zero MISSING),
  and every `healer`/`worker_elephant`/`gate`/`wall`/`foundation_*`/`rubble_*`
  fallback target also ships its own GLB. The old `camel→cavalry` alias block is
  gone from `model_library.cpp` (candidates `:622-626`, prefix-strip `:629-636`,
  procedural tail — no wrong-silhouette alias).
- So: 0 extractions, 0 new files, 0 manifest changes — correctly, since any further
  bake would be variant breadth (94 no-source `civ_unit` combos have no matching
  0 A.D. actor at all, e.g. `athen_elephant`), not gap closure. `~10 min` cap unspent.
- Command had it been needed: `python3 tools/extract_0ad_assets.py --only <ids>`
  (source = `public.zip` `art/actors/…` + `art/meshes/…`, same zip path as above).

## Reproduce (every number above)

```bash
python3 tools/check_parity.py            # 507 models / 4280 PMD / 11.8%
python3 tools/audit_assets.py           # 242 unit/fauna, 0 missing wanted clips, exit 0
python3 tools/audit_assets.py --pmd-diff  # 507 vs 4280, gap 3773, 987/4213 mesh, 1354/5674 actor
git show HEAD:assets/models/manifest.json | python3 -c "import json,sys;print(len(json.load(sys.stdin)['models']))"  # 504
jq '.models | keys | length' assets/models/manifest.json   # 507
ls assets/models/units/*.glb | wc -l    # 245 ; ../buildings = 256 (245+256+6 vegetation = 507)
ls assets/animations/*.psa | wc -l      # 1223
ls assets/textures/terrain/*.png | wc -l  # 653
```

*Supersedes the 2026-08-29 snapshot below, which described a 24-GLB manifest and
`model_library.cpp:616` alias fallbacks that no longer match the tree.*

---

## Previous snapshot (2026-08-29, STALE — kept for history)

# Asset Manifest TODO — Missing GLBs & Fallback Matrix (2026-08-29)

> Generated for G7 quick-win without Godot extract. Tracks what `assets/models/manifest.json` ships vs what `src/graphics/assets/model_library.cpp:616-642` falls back to.

## Summary

- **Shipped GLBs:** 24 in `assets/models/manifest.json` (units 8, buildings 11, vegetation 5) – see `jq '.models | keys'` audit in `docs/ASSET_MAPPING.md`.
- **Missing GLBs:** 18 distinct IDs not in manifest (15 unit/fauna + 4 buildings, where `camel` covers both `camel`/`camel_archer` etc.; count varies by grouping).
- **Runtime fallback:** `ModelLibrary::load` `model_library.cpp:616` pushes archetypal candidates then `Procedural::get_*_mesh` so game never crashes; missing silhouettes are visible as wrong mesh (e.g. `chariot` looks like `cavalry`).

This doc satisfies `docs/REMAINING_GAPS.md:G7` and `G5+G6/G7` task: placeholder manifest entries + fallback note + export command.

---

## Missing Units / Fauna (15+2 naval)

| Missing ID | 0 A.D. source (`public.zip`) | Fallback GLB (`model_library.cpp:616`) | Procedural fallback if GLB also missing | Notes |
|---|---|---|---|---|
| `camel` | `art/actors/units/*camel*.xml` (`pers_camel_archer.xml` etc.) | `res://assets/models/units/cavalry.glb` (`model_library.cpp:617` `id=="camel"||"worker_elephant"||"chariot"... -> cavalry`) | `Procedural::get_unit_mesh("camel")` capsule+head | Also covers `camel_archer` variant |
| `elephant` (`war_elephant`) | `art/actors/units/*elephant*.xml` (`maur_elephant_archer.xml`, `cart_elephant.xml`) | *no explicit fallback* → `Procedural::get_unit_mesh` (elephant currently procedural) | Capsule scaled 1.4× | `worker_elephant` maps to `cavalry.glb` currently, should be elephant |
| `worker_elephant` | `art/actors/units/maur_worker_elephant.xml` | `cavalry.glb` (`617`) | procedural elephant | Mauryan econ elephant 1.35m radius |
| `chariot` | `art/actors/units/*chariot*.xml` (`brit_chariot.xml`, `egy_chariot.xml`) | `cavalry.glb` (`617`) | procedural |  |
| `scythed_chariot` | `art/actors/units/sele_chariot_scythed.xml` | `cavalry.glb` (`617`) | procedural | Seleucid |
| `war_dog` / `dog` | `art/actors/fauna/dog.xml`, `art/actors/units/*dog*.xml` | `deer.glb` (`619-620`) | procedural small | `UNIT_WAR_DOG` 0.40m radius |
| `carnyx` | `art/actors/units/*carnyx*.xml` (`gaul_carnyx.xml`) | `spearman.glb` (`621-622`) | procedural | Gallic herald 0.55m |
| `archer_maiden` | `art/actors/units/maur_archer_maiden.xml` | `archer.glb` (`623-624`) | procedural | Mauryan 0.50m |
| `bolt_shooter` / `ballista` / `oxybeles` | `art/actors/units/*bolt*.xml` (`hele_bolt_shooter.xml`, `rome_ballista.xml`) | `ram.glb` (`630-631`) | procedural | 1.10m |
| `fishing_boat` | `art/actors/units/naval/fishing_boat.xml`, `art/actors/units/*ship*` | *no fallback* → procedural | `Procedural::get_unit_mesh` boat box | 1.10m |
| `trireme` | `art/actors/units/naval/trireme.xml` (`athen_trireme.xml`) | *no fallback* → procedural | procedural 1.80m | `UNIT_TRIREME` |
| `merchant_ship` / `ship_merchant` | `art/actors/units/naval/merchant_ship.xml` | *no fallback* → procedural | procedural 1.60m |  |
| `boar` | `art/actors/fauna/boar.xml` | *no fallback* → procedural (currently `gold_vein` placeholder in `world_view_node.cpp:190`) | procedural | fauna 12 dmg, 120 HP |
| `wolf` | `art/actors/fauna/wolf.xml` | *no fallback* → procedural (currently `stone_mine` placeholder) | procedural | fauna 10 dmg, 80 HP |
| `sheep` (sheep/goat) | `art/actors/fauna/sheep.xml`, `art/actors/fauna/goat.xml` | *fauna_mm[SHEEP] → tree* placeholder | procedural | domestic herd |
| `female_citizen` (alias `female`) | `art/actors/units/athenians/female_citizen.xml` | `villager.glb` (`625-626`) | procedural | 0.48m |
| `healer` (exists but verify) | `art/actors/units/athenians/healer.xml` | `healer.glb` + `villager.glb` (`627-629`) | – | Already shipped as `healer.glb` in manifest ✔ |

*Count:* 15 core missing + `sheep`/`female_citizen` aliases = 17; plus `elephant` distinct from `worker_elephant` = 18. Task says “15 GLBs” – we document 18 to be exhaustive.

## Missing Buildings (4)

| Missing ID | 0 A.D. source | Fallback GLB (`model_library.cpp:633-642`) | Procedural |
|---|---|---|---|
| `dock` / `shipyard` | `art/actors/structures/*dock*.xml`, `art/actors/structures/hellenes/dock.xml` | `storehouse.glb` (`636-637`) | `Procedural::get_building_mesh` dock |
| `corral` | `art/actors/structures/*corral*.xml` (`celt_corral.xml`) | `farmstead.glb` (`634-635`) | procedural corral 2×2 |
| `gate` | `art/actors/structures/*gate*.xml` (`athen_gate.xml`) | `wall.glb` (`638-639`) | procedural |
| `wonder` | `art/actors/structures/*wonder*.xml` (`athen_wonder.xml`, `hele_wonder.xml`) | `town_center.glb` (`640-641`) | procedural 2×2 5000 HP |

All above hit `ModelLibrary::load` fallback chain or `Procedural::get_building_mesh` – visible as wrong silhouette but never crash. Verify with: `grep -n "candidates.push_back" src/graphics/assets/model_library.cpp`.

## Fallback Matrix (compact)

```
units:
  chariot, scythed_chariot, camel, worker_elephant -> cavalry.glb
  war_dog, dog                                     -> deer.glb
  carnyx                                           -> spearman.glb
  archer_maiden                                    -> archer.glb
  female_citizen, female                            -> villager.glb
  healer                                           -> healer.glb -> villager.glb
  bolt_shooter, ballista, oxybeles                 -> ram.glb
  elephant, fishing_boat, trireme, merchant_ship, boar, wolf, sheep -> Procedural (no GLB fallback yet)

buildings:
  corral  -> farmstead.glb
  dock, shipyard -> storehouse.glb
  gate    -> wall.glb
  wonder  -> town_center.glb
  others missing -> Procedural

vegetation/fauna placeholders buggy:
  fauna_mm[DEER]=pine? actually tree? see world_view_node.cpp:190-193
    FAUNA_DEER  -> tree? (should be deer.glb)
    FAUNA_BOAR  -> stone_mine (should be boar.glb)
    FAUNA_SHEEP -> tree (should be sheep.glb)
    FAUNA_WOLF  -> gold_vein (should be wolf.glb)
```

## Export Command (how to fill gaps)

```bash
# Full 0 A.D. bake (needs 0 A.D. checkout at /home/abhi/project/0ad)
python3 tools/extract_0ad_assets.py --help          # show options
python3 tools/extract_0ad_assets.py --full \
  --zip /home/abhi/project/0ad/binaries/data/mods/public/public.zip \
  --out assets

# Shorthand (tool defaults to --full terrain/UI skeletons)
python3 tools/extract_0ad_assets.py --full

# Verify after export
jq '.models | keys | length' assets/models/manifest.json   # should be ~42 after full (24 + 18)
ls assets/models/units/*.glb | wc -l
ls assets/models/buildings/*.glb | wc -l
grep -c "candidates.push_back" src/graphics/assets/model_library.cpp
godot --headless --check   # no missing GLB crash
```

`assets/README.md` documents same export and `model_library.cpp:616` fallback paragraph.

## Manifest placeholder note

Instead of writing dummy GLBs, we keep `assets/models/manifest.json` at 24 entries and document missing here. If you want JSON placeholders, add to `manifest.json`:

```json
"_pending_fallbacks": {
  "camel": "cavalry.glb",
  "elephant": "procedural",
  "war_dog": "deer.glb",
  "chariot": "cavalry.glb",
  "scythed_chariot": "cavalry.glb",
  "carnyx": "spearman.glb",
  "archer_maiden": "archer.glb",
  "bolt_shooter": "ram.glb",
  "fishing_boat": "procedural",
  "trireme": "procedural",
  "merchant_ship": "procedural",
  "boar": "procedural",
  "wolf": "procedural",
  "dock": "storehouse.glb",
  "corral": "farmstead.glb",
  "gate": "wall.glb",
  "wonder": "town_center.glb"
}
```

Tool `tools/check_parity.py` reports `manifest models: 24  anims: 15?` – after full export coverage rises to `~42/800 5% → 15%`.

---

*See `docs/ASSET_MAPPING.md:2026-08-29 Patch G5+G6/G7` and `src/graphics/assets/model_library.cpp:616` fallback chain.*
