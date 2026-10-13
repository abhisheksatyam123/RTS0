# Asset Manifest — Coverage & Status

> This doc owns asset coverage and status numbers. The technique (how 0 A.D. textures, UVs,
> skeletons and clips map onto RTS0) is in [ASSET_MAPPING.md](ASSET_MAPPING.md). Running the
> extractor and the `assets/` layout are in [assets/README.md](../assets/README.md). Licensing is in
> [0AD_ATTRIBUTION.md](0AD_ATTRIBUTION.md).

## Headline (checked 2026-10-09)

Manifest counts come from `assets/models/manifest.json`. The disk column was counted on the same checkout.

| Item | Manifest | On disk |
|---|---|---|
| models | **1076** (units 539, buildings 510, vegetation 27) | 539 / 510 / 27 `.glb` (vegetation 27 includes the 0 A.D. fish shoal, `fish.glb`, from `tools/dae_to_glb.gd`, and 15 trees from `tools/fetch_0ad_trees.py`) |
| props | not in the manifest | 8 `.glb` in `assets/models/props/` (7 carried-resource shuttles, the garrison flag) from `tools/dae_to_glb.gd` with `prop`; 11 flag skins in `assets/textures/garrison_flags/` |
| scenery | not in the manifest | 22 `.glb` in `assets/models/scenery/` for the 19 Gaia scenery types (`data/scenery_actors.json`), from `tools/extract_0ad_scenery.py` |
| player masks | not in the manifest | 472 L8 `.png` in `assets/textures/player_masks/` (1 - alpha of 0 A.D.'s `player_trans` skins) and `data/player_masks.json` (1986 GLB images), from `tools/extract_player_masks.py` and `tools/extract_0ad_structures.py` |
| particles | **3** | `flame.png`, `smoke_64a.png`, `dust_256a.png` in `assets/textures/particles/` |
| animations | **1316** | 1316 `.psa` + 1316 `.json` in `assets/animations/` |
| skeletons | **98** | 98 `.xml` in `assets/skeletons/` |
| terrain | **652** | 653 `.png` in `assets/textures/terrain/` (`splat.png` has no manifest entry) |

- `python3 tools/audit_assets.py` exits 0: `282 unit/fauna assets, 17 static hulls, 0 missing wanted clips` (2026-10-08).
- The 0 A.D. PMD comparison (`audit_assets.py --pmd-diff`, `check_parity.py`) cannot run on 2026-10-08: `public.zip` is not present in this checkout. The last measured PMD coverage (2026-10-05, 599 models / 4280 PMD) is superseded by the table above.
- The manifest grew from 599 models (G7-Q log, 2026-10-05) to 918 with no dated log entry in this file (the 134 unit template GLBs among them). 2026-10-09: the civ buildings PR adds 77 building GLBs (75 civ structures, the palisade, the Han bamboo palisade) for 995, the civ special buildings PR 14 more for 1009, the wall lengths PR 52 (each civ's short, medium and long wall and wall gate) for 1061, and the palisade pieces PR 5 (palisade short, medium, long, tower and gate).

### Silhouette coverage

- The 23 unit classes and the first 14 building types each have their own GLB on disk (checked 2026-10-08). Per-template, per-civ and fauna coverage is much lower: see "Gaps against 0 A.D." below (checked 2026-10-09).
- Civ-prefixed ids (e.g. `athen_elephant`) have no GLB of their own in most cases. They resolve at runtime to the base-unit GLB (see the fallback chain below).

## Unit matrix: 23/23 canonical ids ship a unique GLB (`UNIT_TYPE_COUNT = 23`, `src/simulation/sim_types.h:128`)

Clip count `(n)` is the number of embedded GLB clips reported by `audit_assets.py`. Every unit has `Attack=Y` and `Run=Y`.

| # | UnitType | model_id | GLB | Attack/Run (n) |
|---|---|---|---|---|
| 0 | Villager | `villager` | `assets/models/units/villager.glb` | Y/Y (15) |
| 1 | Swordsman | `swordsman` | `assets/models/units/swordsman.glb` | Y/Y (6) |
| 2 | Archer | `archer` | `assets/models/units/archer.glb` | Y/Y (15) |
| 3 | Spearman | `spearman` | `assets/models/units/spearman.glb` | Y/Y (15) |
| 4 | Cavalry | `cavalry` | `assets/models/units/cavalry.glb` | Y/Y (7) |
| 5 | Merchant Trader | `trader` | `assets/models/units/trader.glb` | Y/Y (5) |
| 6 | Catapult Artillery | `catapult` | `assets/models/units/catapult.glb` | Y/Y (4) |
| 7 | Historical Champion Hero | `hero` | `assets/models/units/hero.glb` | Y/Y (16) |
| 8 | War Trireme | `trireme` | `assets/models/units/trireme.glb` | Y/Y (5) |
| 9 | Carthaginian War Elephant | `elephant` | `assets/models/units/elephant.glb` | Y/Y (5) |
| 10 | Siege Battering Ram | `ram` | `assets/models/units/ram.glb` | Y/Y (5) |
| 11 | Temple Healer | `healer` | `assets/models/units/healer.glb` | Y/Y (6) |
| 12 | Celtic War Chariot | `chariot` | `assets/models/units/chariot.glb` | Y/Y (5) |
| 13 | Celtic War Dog | `war_dog` | `assets/models/units/war_dog.glb` | Y/Y (5) |
| 14 | Bolt Shooter Ballista | `bolt_shooter` | `assets/models/units/bolt_shooter.glb` | Y/Y (4) |
| 15 | Fishing Boat | `fishing_boat` | `assets/models/units/fishing_boat.glb` | Y/Y (8) |
| 16 | Merchant Cargo Ship | `merchant_ship` | `assets/models/units/merchant_ship.glb` | Y/Y (5) |
| 17 | Female Citizen | `female_citizen` | `assets/models/units/female_citizen.glb` | Y/Y (15) |
| 18 | Gallic Carnyx Herald | `carnyx` | `assets/models/units/carnyx.glb` | Y/Y (7) |
| 19 | Mauryan Worker Elephant | `worker_elephant` | `assets/models/units/worker_elephant.glb` | Y/Y (6) |
| 20 | Mauryan Archer Maiden | `archer_maiden` | `assets/models/units/archer_maiden.glb` | Y/Y (6) |
| 21 | Persian/Arabian Camel | `camel` | `assets/models/units/camel.glb` | Y/Y (8) |
| 22 | Seleucid Scythed Chariot | `scythed_chariot` | `assets/models/units/scythed_chariot.glb` | Y/Y (5) |

## Building matrix: the first 14 types (`BLD_TYPE_COUNT` is now 52; the other 38 are listed under "Gaps against 0 A.D.")

| # | BuildingType | model_id | GLB |
|---|---|---|---|
| 0 | Town Center | `town_center` | `assets/models/buildings/town_center.glb` |
| 1 | House | `house` | `assets/models/buildings/house.glb` |
| 2 | Barracks | `barracks` | `assets/models/buildings/barracks.glb` |
| 3 | Storehouse | `storehouse` | `assets/models/buildings/storehouse.glb` |
| 4 | Defense Tower | `wall_tower` | `assets/models/buildings/wall_tower.glb` |
| 5 | Farmstead | `farmstead` | `assets/models/buildings/farmstead.glb` |
| 6 | Market | `market` | `assets/models/buildings/market.glb` |
| 7 | Fortress | `fortress` | `assets/models/buildings/fortress.glb` |
| 8 | Gate | `gate` | `assets/models/buildings/gate.glb` |
| 9 | Temple | `temple` | `assets/models/buildings/temple.glb` |
| 10 | Wonder Monument | `wonder` | `assets/models/buildings/wonder.glb` |
| 11 | Naval Shipyard Dock | `dock` | `assets/models/buildings/dock.glb` |
| 12 | Stone Wall Segment | `wall_segment` | `assets/models/buildings/wall_segment.glb` |
| 13 | Livestock Corral | `corral` | `assets/models/buildings/corral.glb` |

Buildings carry no clips (static GLBs).

## Fauna matrix: the original 4 (61 species now use 51 models; see "Gaps against 0 A.D.")

| # | FaunaType | model_id | GLB | Clips (audit) |
|---|---|---|---|---|
| 0 | Red Deer | `deer` | `assets/models/units/deer.glb` | Idle/Walk/Run/Attack/Death |
| 1 | Wild Boar | `boar` | `assets/models/units/boar.glb` | Idle/Walk/Run/Attack/Death |
| 2 | Mountain Sheep | `sheep` | `assets/models/units/sheep.glb` | Idle/Walk/Run/Attack/Death |
| 3 | Gray Wolf | `wolf` | `assets/models/units/wolf.glb` | Idle/Walk/Run/Attack/Death |

Loaded via `ModelLibrary::get("units", …)` in `src/presentation/world_view_node.cpp:267-275`. If the deer GLB is missing, the code falls back to the `war_dog` GLB (`:268`).

## Runtime fallback chain (no wrong-silhouette alias)

`ModelLibrary::load` in `src/graphics/assets/model_library.cpp:622-654` tries these candidates in order:

1. `res://assets/models/{category}/{id}.glb` (`:622`)
2. `units/{id}`, `buildings/{id}`, `vegetation/{id}`, then `models/{id}` (`:623-626`)
3. Units only: strip the leading `<civ>_` segment and try `units/{base}.glb` (`:629-634`). `healer` also tries `villager.glb` (`:636`).
4. Buildings only: strip the prefix the same way (`:638-642`), `gate` tries `wall.glb` (`:644-645`), rubble and foundation ids try their named GLBs (`:647-654`).
5. Otherwise `Procedural::get_*_mesh` (`src/graphics/assets/procedural.h:17`).

The old `camel→cavalry` and `war_dog→deer` alias block from the 2026-08-29 version of this doc no longer exists in the code.

## `.psa` → animation wiring (biped_attack, biped_run, archer_attack): PASS (static check, 2026-10-08)

| Clip | extract map (`tools/extract_0ad_assets.py`) | manifest (frames / bones) | file on disk | runtime `load_clip` |
|---|---|---|---|---|
| `biped_attack` | `:1250` `art/animation/biped/infantry/swordsman/attack_melee_shield_01.dae.cached.psa` | 29 / 46 | `assets/animations/biped_attack.psa` + `.json` | `src/animation/animation_controller.cpp:36` (Biped/Attack), `:54` (Villager/Attack) |
| `biped_run` | `:1249` `art/animation/biped/infantry/swordsman/run_relax_shield.dae.cached.psa` | 24 / 46 | `assets/animations/biped_run.psa` + `.json` | `animation_controller.cpp:35` (Biped/Run), `:44` (Archer/Run), `:53` (Villager/Run) |
| `archer_attack` | `:1271` `art/animation/biped/infantry/archer/attack_ranged.dae.cached.psa` | 74 / 46 | `assets/animations/archer_attack.psa` + `.json` | `animation_controller.cpp:45` (Archer/Attack) |

- PSA loader: `PsaAnim::load_psa` (`src/animation/psa_loader.cpp`). The alias map at `:19` resolves `archer_shoot → archer_attack`.
- `AnimationController::load_clip` stores the parsed `PsaAnim` per `AnimState`.

Caveat: the crowd renderer bakes **GLB-embedded** `AnimationPlayer` clips (`RtsoWorldView::bake_puppet_clips`, `src/presentation/world_view_node.cpp:749`). `AnimationController` is the CPU sampling path (`sample_current_pose`, `src/animation/animation_controller.cpp:356`). Both carry the same three clips. No headless Godot bake has been run, so full visual proof still needs a Godot run.

## terrain.gdshader splat sampling: wired (static check, 2026-10-08)

Shader side:
- `shaders/terrain.gdshader:6-9` declares `noise_tex`, `grass_tex`, `dirt_tex`, `rock_tex`.
- `:62`, `:64` and `:67-69` sample grass, dirt and rock. The weights are driven by slope, height and noise, and `v_height` comes from per-vertex heights (`src/presentation/world_view_node.cpp`, `rebuild_terrain`).

Binder side:
- `assets/materials/thronefall/terrain.tres:11-14` (preferred path, checked first by `try_load_mat`) binds `noise_tex → terrain_noise.png` and `grass/dirt/rock_tex → grass.png / dirt.png / rock.png`. All four PNGs are on disk.
- C++ fallback `src/graphics/assets/assets.cpp:62-64` (`Assets::load_terrain_material`) binds grass, dirt and rock only. It does not bind `noise_tex`.

Open terrain items:
- **Single global triple.** `Assets::load_terrain_material()` (`assets.cpp:54`) takes no biome input, so there is no per-biome switching. Biome switching is a follow-up.
- **`territory_map` is never bound.** `shaders/terrain.gdshader:18` declares it, and `:78-81` samples it only when `territory_strength > 0` (default 0). No C++ code binds it, so the territory overlay is inert.

## Gaps against 0 A.D. (checked 2026-10-09, in code)

Models:
- **Units.** Every roster template whose 0 A.D. actor exists draws that actor (360 of 391; checked 2026-10-09). `tools/extract_0ad_assets.py --templates` adds 134 template GLBs and writes `src/graphics/assets/unit_template_models.h`; `world_view_node` draws 207 templates with their own model and the rest with the civ class model, which is already their actor. 41 template GLBs carry no clips and play their class model's (same bones, `clips_from` in the manifest); the others got Capture, Idle2 and Death2 from `tools/add_0ad_clips.py` like the class models. Not drawn per template: the 14 `rts0_villager` rows (no 0 A.D. template) and packed siege (wagon models). Template ship rams and scouts have no clips, like the class ships.
- **Buildings (53 types × 14 civs).** Civ structures with their own 0 A.D. actor use it (civ buildings PR): `tools/extract_0ad_structures.py` converted 75 distinct actors (stables, ranges, arsenals, forges, outposts, sentry towers, Han walls and gate, elephant stables, theaters, the Spartan syssiton, Gallic assembly and Persian hall, libraries, the Kushite pyramid, the Persian palace) plus the palisade and the Han bamboo palisade. `data/building_models.json` maps 95 civ model ids onto them. Civs whose 0 A.D. actor is the generic one keep it. The civ special buildings PR gives 16 civ-specific BuildingTypes that drew a stand-in their own model (the Spartan gerousia and syssiton, Gallic assembly, Persian tachara and ice house, Roman army camp and temples of Mars and Vesta, Iberian monument, Ashoka pillar, Ptolemaic mercenary camp, Ptolemaic and Seleucid military colonies, Kushite Blemmye and Noba camps, the temple of Amun, and the Carthaginian Cothon) through 14 more converted actors; a civ that owns one without having the template draws the original civ's model. Still generic: the Carthaginian Celtic, Iberian and Italic embassies (one model), Walls laid as 0 A.D.'s WallSet (#233) draw each civ's wall_short, wall_medium or wall_long by the segment's length and a gate made from a long segment its wall_gate, at 0 A.D.'s 1 unit = 4 m and on 0 A.D.'s ground plane (wall lengths PR). Wall towers still use the one-cell tower model, sized and grounded as before. Palisade WallSets draw 0 A.D.'s palisades_short, _medium, _long, _tower and _gate by piece (palisade pieces PR). Field plots are generic in 0 A.D. too.
- **Fauna.** 61 species use 51 models (checked 2026-10-09). The 10 remaining pairs share one 0 A.D. actor too (for example both sheep, both camels, the wild and domestic zebu). The 29 converted with `tools/extract_0ad_fauna_models.py` (Collada source, PR #179) have 0 A.D.'s own mesh, texture and Idle/Walk/Run/Attack/Death clips. Still missing: per-species idle and death variants.
- **Props and scenery.** 19 Gaia scenery types now use 0 A.D. actors (22 GLBs); ambient hawks fly on 34 maps. Turreted units stand at 0 A.D.'s turret point.

Animations (363 unit GLBs):
- 23 unit GLBs have no clips: projectiles, and hulls and animals whose 0 A.D. actors have none either (Celtic, Han, Iberian and Mauryan warships, the merchant ships, the Iberian fishing ship, muskox, walrus, humpback whale; checked 2026-10-09). Every hull rocks in the world view (`apply_ship_wave_xform`).
- One `Attack` clip per unit: no melee/ranged split per weapon. `Slaughter` is separate, and the 166 biped GLBs carry 0 A.D.'s `Capture` clip (`biped/infantry/capturing_a.dae`, added by `tools/add_0ad_clips.py`). Cavalry, siege and ships fall back to `Attack` when capturing.
- Idle and death variants: 0 A.D. picks one of an actor's same-named clips at random. 151 biped GLBs carry a second idle (`Idle2`) and 192 a second death (`Death2`), the actor's next most frequent (`tools/add_0ad_clips.py --variants`), and each unit keeps one by its id. The horse, camel and elephant GLBs add 65 `Idle2` and 43 `Death2` (`--mounts`): the mount's next clip, with the rider playing its own on the mount's prop point. Horse manes and tails are skinned to the horse as in 0 A.D. on 73 cavalry GLBs (`tools/skin_0ad_mount_props.py`; new extracts skin them directly). Still rigid on the `hair` bone: the chariot and trader horses, which are props on a cart rig. The Han cavalry GLBs draw their rider standing and rigid on `spine1` instead of seated.
- Siege: the 25 unpacked engines have no `Walk`, as in 0 A.D., where an engine only moves packed. A packed engine shows its 0 A.D. `*_packed` actor (a wagon with horses or oxen, crew and cargo) in 13 GLBs from `tools/extract_0ad_packed_siege.py`, mapped in `src/graphics/assets/packed_siege_models.h`. 0 A.D. has no pack/unpack clips: the model swaps when packing or unpacking ends. `GatherFish` is on 3 GLBs only.
- Done: citizen-soldier gather/build clips, per-resource carry walks and the promotion clip (#149, added at runtime from `.psa`).

Textures:
- **Player colour: done (player colour PR).** `extract_0ad_assets.py` bakes a fixed tint into each skin where the `player_trans` alpha is below 250. `tools/extract_player_masks.py` recovers 0 A.D.'s mask for 1424 GLB images (328 unit and 338 building models) and `shaders/player_color.gdshader` swaps the baked tint for the owner's colour. Not coloured: fauna, rubble and foundations, plus 12 unit models (Gaul and Briton citizens and archers, catapults, the bolt shooter) and 10 structures (Gaul walls and towers, the sentry and defense towers, pyramids, siege wall, rowboat) whose skins on 0 A.D.'s current master have no mask or no longer match the extracted ones.
- **Normal maps: done (normal maps PR).** `tools/extract_normal_maps.py` pairs each skin with the `normTex` its 0 A.D. actor names when the actor's material is `*_norm*` or `*_parallax*`, and writes 268 maps (`assets/textures/normal_maps/`, 42 MB) for 2432 GLB images (1155 building, 1277 unit; unit bodies use 0 A.D.'s flat `default_norm`, so mostly shields, helmets and props). 175 images matched no skin and stay flat. No specular or parallax maps.
- **Sky boxes: done (sky box PR).** `tools/extract_sky_boxes.py` writes all 20 of 0 A.D.'s sky sets (`assets/textures/skies/`, 22 MB, five 512x512 faces each). Maps naming `default`, which has no folder in 0 A.D., take cirrus, cumulus or sunny by map id.

## Open todos (moved here from ASSET_MAPPING.md, checked 2026-10-08)

- **Specular maps: done (specular maps PR).** `tools/extract_specular_maps.py` pairs each skin with the `specTex` its 0 A.D. actor names when the actor's material is `*_spec*`, and writes 361 maps (`assets/textures/specular_maps/`, 8 MB: brightness in red, the material's specular power in green) for 3123 GLB images (1185 building, 1938 unit). `shaders/player_color.gdshader` draws model_common.fs's Blinn-Phong highlight from them, and opaque skins with a specular map but no player colour use that shader too.
- **No parallax maps.** The parallax height in `normTex` alpha is not used.
- **UI coverage.** Files on disk under `assets/textures/ui/`:
  - Emblems: 13 civs (`emblems/`, athenians through seleucids).
  - Phase icons: 3 (`phases/`: village, town, city).
  - Loading screen: 57 (`loading/`: 47 tip pictures, 6 progress bar pieces, parchment, tip gradient, background tile). 0 A.D.'s `gradient_4x3.png` is not used.
  - Reference portraits: `portraits/catafalque.png` for the Catafalque Overview.
  - Formations: 12 PNG (`formations/`). The 0 A.D. formation count of 14 comes from the 2026-08-29 audit and was not re-verified (zip absent).
  - Auras and minimap: 19 and 22 files matched by name. Runtime wiring of these was not re-audited.
  - Cursors: 23 (`assets/textures/cursors/`: 22 `action-*` and `cursor-rally`), wired by `update_cursor`. 0 A.D.'s remaining cursors (the remaining set-rally variants, the disabled variants, the arrow) are not copied yet.
- **Translations.** `assets/l10n/`: 25 merged .po files (the 0 A.D. languages at least 75% translated, about 18 MB), written by `tools/fetch_0ad_l10n.py`. The other 0 A.D. languages and the CJK fonts 0 A.D. ships are not copied; Chinese, Japanese and Korean text uses the system's font fallback.
- **2026-08-29 gap list: closed.** The 18 missing GLBs from the 2026-08-29 gap list are all shipped (see the matrices above). Re-export a single id with `python3 tools/extract_0ad_assets.py --only <ids>` (see [assets/README.md](../assets/README.md)).

## G7-H2 push (2026-10-05): HIGH-VISIBILITY class batch, +31 models (507 → 538)

Ran `python3 tools/extract_0ad_assets.py --class g7h2` (`G7_CLASSSETS` in the extractor, 31 curated ids).

| # | Class | New ids (31) | GLB dir | Clips verified |
|---|---|---|---|---|
| 1 | Map fauna (15) | `goat`, `zebra`, `chicken`, `crocodile`, `giraffe`, `rabbit`, `gazelle`, `bear`, `cow`, `pig`, `lion`, `lioness`, `donkey`, `fox`, `hawk` | `models/units/` | all 5 (`Idle/Walk/Run/Attack/Death`) |
| 2 | Landmarks / civic extras (11) | `pyramid`, `pyramid_minor`, `ishtar_gate`, `greek_stoa`, `greek_stoa_great`, `theater`, `gymnasium`, `stoa`, `prytaneion`, `propylaea`, `lighthouse` | `models/buildings/` | static |
| 3 | Flora variety (5) | `oak`, `acacia`, `baobab`, `bamboo`, `apple` | `models/vegetation/` | static |

```text
[pmd-diff] BEFORE: manifest models: 507  public.zip PMD: 4280  gap: 3773  coverage: 11.8%
[pmd-diff] AFTER:  manifest models: 538  public.zip PMD: 4280  gap: 3742  coverage: 12.6%
audit BEFORE: 242 unit/fauna assets, 16 static hulls, 0 missing wanted clips, RC=0
audit AFTER:  257 unit/fauna assets, 16 static hulls, 0 missing wanted clips, RC=0
manifest: models 507->538  anims 1223->1279 (+56)  skels 83->89
```

- Skipped with reason: civ-skin duplicates of already-converted models, `special/old/*` and `campaigns/*` actors, `particle/*`, `props/*` parts (already composed into GLBs), `fungi/*` and `temp/*` stubs, and fauna without a key-matched skeleton and PSA triple (see Open todos).

## G7-Q push (2026-10-05): HIGH-VISIBILITY class batch, +32 models (567 → 599)

Ran `python3 tools/extract_0ad_assets.py --class g7q` (`G7_CLASSSETS` entry: warship hulls, smith/forge/field/harbor statics, hound/elephant/chariot variants, 32 ids).

| # | Class | New ids (32) | GLB dir | Clips verified |
|---|---|---|---|---|
| 1 | Warship and boat hulls (14) | `bireme`, `bireme_rome`, `quinquereme`, `quinquereme_rome`, `pentekonter`, `trireme_han`, `bireme_han`, `ship_knarr`, `ship_snekkja`, `ship_karvi`, `skiff`, `rowboat`, `merchant_han`, `fishing_han` | `models/buildings/` (hulls) + `models/units/pentekonter.glb` (animated) | `pentekonter` actor-harvested `Idle/Walk/Run` (3-bone `ship_one_row` rig); 0-bone hulls static |
| 2 | Smith / forge / field / harbor statics (13) | `blacksmith_brit`, `blacksmith_kart`, `forge_germ`, `forge_han`, `field_plot`, `orchard_plot`, `crannog`, `wooden_tower`, `scout_tower`, `tower_bolt`, `tower_artillery`, `stable_elephant`, `kennel` | `models/buildings/` | static |
| 3 | Hound / elephant / chariot variants (5) | `wolfhound`, `mastiff_gaia`, `elephant_bush`, `elephant_forest`, `chariot_han` | `models/units/` | all 5 |

```text
[pmd-diff] BEFORE: manifest models: 567  public.zip PMD: 4280  gap: 3713  coverage: 13.2%
[pmd-diff] AFTER:  manifest models: 599  public.zip PMD: 4280  gap: 3681  coverage: 14.0%
audit BEFORE: 268 unit/fauna assets, 16 static hulls, 0 missing wanted clips, RC=0
audit AFTER:  274 unit/fauna assets, 16 static hulls, 0 missing wanted clips, RC=0
manifest: models 567->599  anims 1302->1316 (+14: hound/elephant-bush/forest PSAs)  skels 96->98 (+4 refs: wolfhound/elephant_bush/elephant_forest/ship_one_row; mastiff/chariot rigs reused)
```

- Skipped with reason: civ-skin hull and building duplicates, `destruction_*` / `fndn_*` transient states, `particle/*` and `props/*` library parts, `campaigns/*` and `special/old/*` duplicates, `fungi/*` and `temp/*` stubs, and `rome_trireme_corvus` / `rome_quinquereme_corvus` (prop-level difference, deferred).

## Reproduce

```bash
python3 tools/check_parity.py              # manifest counts; PMD comparison needs public.zip
python3 tools/audit_assets.py              # exit 0; unit/fauna, static hulls, clip check
python3 tools/audit_assets.py --pmd-diff   # needs public.zip (not present 2026-10-08)
jq '{models:(.models|length), animations:(.animations|length), skeletons:(.skeletons|length), terrain:(.terrain|length)}' assets/models/manifest.json
ls assets/models/units/*.glb | wc -l       # 363
ls assets/models/buildings/*.glb | wc -l   # 367
ls assets/models/vegetation/*.glb | wc -l  # 27
ls assets/animations/*.psa | wc -l         # 1316
ls assets/skeletons/*.xml | wc -l          # 98
ls assets/textures/terrain/*.png | wc -l   # 653 (652 manifest entries + splat.png)
```

To re-export assets, see the export command in [assets/README.md](../assets/README.md).
