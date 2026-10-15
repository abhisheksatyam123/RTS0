# Asset Manifest — Coverage & Status

> This doc owns asset coverage and status numbers. The technique (how 0 A.D. textures, UVs,
> skeletons and clips map onto RTS0) is in [ASSET_MAPPING.md](ASSET_MAPPING.md). Running the
> extractor and the `assets/` layout are in [assets/README.md](../assets/README.md). Licensing is in
> [0AD_ATTRIBUTION.md](0AD_ATTRIBUTION.md).

## Headline (checked 2026-10-09)

Manifest counts come from `assets/models/manifest.json`. The disk column was counted on the same checkout.

| Item | Manifest | On disk |
|---|---|---|
| models | **1192** (units 565, buildings 536, vegetation 91) | 565 / 536 / 91 `.glb` (vegetation 91 includes the 0 A.D. fish shoal, `fish.glb`, from `tools/dae_to_glb.gd`, and 15 trees plus 64 `_v2`..`_v4` tree variants from `tools/fetch_0ad_trees.py`) |
| props | not in the manifest | 8 `.glb` in `assets/models/props/` (7 carried-resource shuttles, the garrison flag) from `tools/dae_to_glb.gd` with `prop`; 11 flag skins in `assets/textures/garrison_flags/` |
| scenery | not in the manifest | 22 `.glb` in `assets/models/scenery/` for the 19 Gaia scenery types (`data/scenery_actors.json`), from `tools/extract_0ad_scenery.py` |
| player masks | not in the manifest | 472 L8 `.png` in `assets/textures/player_masks/` (1 - alpha of 0 A.D.'s `player_trans` skins) and `data/player_masks.json` (1986 GLB images), from `tools/extract_player_masks.py` and `tools/extract_0ad_structures.py` |
| particles | **11** | `dust_cav.png`, `flame.png`, `smoke_128a.png`, `smoke_64a.png`, `dust_256a.png`, `sparks.png`, `smoke_64a_blacksmith.png`, `smoke_128b.png`, `smoke_128d_light.png`, `smoke_curved.png`, `smoke_256a.png` in `assets/textures/particles/` |
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
- **Units.** Every roster template whose 0 A.D. actor exists draws that actor (360 of 391; checked 2026-10-09). `tools/extract_0ad_assets.py --templates` adds 134 template GLBs and writes `src/graphics/assets/unit_template_models.h`; `world_view_node` draws 207 templates with their own model and the rest with the civ class model, which is already their actor. 41 template GLBs carry no clips and play their class model's (same bones, `clips_from` in the manifest); the others got Capture, Idle2 and Death2 from `tools/add_0ad_clips.py` like the class models. Not drawn per template: the 14 `rts0_villager` rows (no 0 A.D. template) and packed siege (wagon models). Template ship rams and scouts have no clips, like the class ships. The map-only scenario rows 463-481 (2026-10-15) draw their own 0.28 actor: 14 new GLBs (the Spartan and Carthaginian pike champions, the Kardakes hoplite and skirmisher, the Roman slave swordsman, Xerxes' chariot, Pyrrhus, Boudicca's cavalry, and the foot forms of Marcellus, Maximus, Craterus, Philip II, Cunobelin and Indibil) with Capture, Idle2 and Death2 and their masks, the marine archer reuses `athen_infantry_archer_b`, and Scipio, Boudicca, Vercingetorix and Ashoka on foot keep their civ's hero model, which is that actor.
- **Buildings (53 types × 14 civs).** Civ structures with their own 0 A.D. actor use it (civ buildings PR): `tools/extract_0ad_structures.py` converted 75 distinct actors (stables, ranges, arsenals, forges, outposts, sentry towers, Han walls and gate, elephant stables, theaters, the Spartan syssiton, Gallic assembly and Persian hall, libraries, the Kushite pyramid, the Persian palace) plus the palisade and the Han bamboo palisade. `data/building_models.json` maps 95 civ model ids onto them. Civs whose 0 A.D. actor is the generic one keep it. The civ special buildings PR gives 16 civ-specific BuildingTypes that drew a stand-in their own model (the Spartan gerousia and syssiton, Gallic assembly, Persian tachara and ice house, Roman army camp and temples of Mars and Vesta, Iberian monument, Ashoka pillar, Ptolemaic mercenary camp, Ptolemaic and Seleucid military colonies, Kushite Blemmye and Noba camps, the temple of Amun, and the Carthaginian Cothon) through 14 more converted actors; a civ that owns one without having the template draws the original civ's model. Carthage's Celtic, Iberian and Italic embassies and its apartment draw their own 0.28 actors (`s_carthaginians_embassy_*`, `s_carthaginians_apartment`, converted from 0.28's `public.zip` by `extract_0ad_assets.py --zip`; 2026-10-13). Walls laid as 0 A.D.'s WallSet (#233) draw each civ's wall_short, wall_medium or wall_long by the segment's length and a gate made from a long segment its wall_gate, at 0 A.D.'s 1 unit = 4 m and on 0 A.D.'s ground plane (wall lengths PR). Wall towers still use the one-cell tower model, sized and grounded as before. Palisade WallSets draw 0 A.D.'s palisades_short, _medium, _long, _tower and _gate by piece (palisade pieces PR). Field plots are generic in 0 A.D. too.
- **Germans (0.28's 15th civ, checked 2026-10-13).** Their art first ships in the 0.28 release, so it comes from that release's `public.zip` (Ubuntu's `0ad-data_0.28.0` archive; see `/mnt/project-files/d-civ-rosters/0ad-028.md`), not GitHub. `extract_0ad_assets.py --zip <public.zip>` writes 14 `germ_*` unit GLBs (citizens, swordsman, spearman, slinger, cavalry, Teutobod, healer, trader, fishing boat, arrow ship, karvi, rams) and 13 `germ_*` buildings, and 8 `s_germans_*` structures (stable, range, workshop, forge, the wooden wall pieces and gate) mapped in `data/building_models.json`, plus the generic `wagon` and `wagon_house` unit GLBs (the supply wagon, which `UNIT_WAGON` now draws with 0 A.D.'s wagon clips (`RigType::Wagon`), and the house wagon it promotes to; ox-drawn, rigid oxen on the cart rig, 3.5 units long like the template obstruction); the outpost is the Celtic one, as in 0 A.D. Their emblem is `assets/textures/ui/emblems/germans.png` (`extract_ui_assets.py`). The Germans draw them by their own civ code. `--templates` added 10 per-template GLBs (Boiorix, Lugius, the seeress, the axe and club champions, the noble cavalry, the javelin cavalry and infantry, the scout ship, the pivot ram), spliced into `unit_template_models.h` by hand, since a full regeneration from 0.28 would change other civs' rows; the German biped and horse GLBs got Capture, Idle2 and Death2 (`add_0ad_clips.py`, with 0.28's German actors and variants laid over the git checkout). The Wagon Encampment and its fortified Upgrade draw 0.28's `structures/germans/encampment.xml` and `encampment_fortified.xml` (`s_germans_encampment*`, as wide as the Roman army camp they stood in for; their buried prop-point base stays out of the fit), and the wagon house's roster row (456) draws `wagon_house.glb` (2026-10-14). The Cerannus totem (their Revered Monument, BLD_MONUMENT) draws 0.28's `structures/germans/cult_statue.xml` (`s_germans_cult_statue`, 0.21 per metre) instead of the Iberian monument, and their Gate and Stone Wall Segment rows draw the wooden wall gate and long wall, as their wallset does.
- **Gaia shrines and trading posts (BLD 69/70, checked 2026-10-14).** They draw the 0.28 actors their templates name instead of their civ's temple or market: the Greek shrine `hellenes/shrine_01.xml` (Macedonian row), Celtic `celts/small_stone_monument.xml`, Egyptian `ptolemies/kiosk.xml`, Persian `persians/shrine.xml`, Chinese `han/pagoda.xml`, Italian the Roman temple of Vesta, Kushite `kushites/shrine.xml`; the Celtic trading post `celts/tavern.xml` and the desert one `persians/market.xml`. `extract_0ad_assets.py --zip` writes the seven `s_*` GLBs at 0.21 per metre (the temple of Vesta's scale); `data/building_models.json` maps the `{civ}_shrine` / `{civ}_trading_post` ids to them. The Greek `shrine_02` variant draws `shrine_01`, since both share one type and civ. The map-only Persian inn (BLD 75) and Roman tent (76) draw 0.28's `props/structures/persians/alt_building_04.xml` and `props/structures/romans/rome_tent.xml` (`s_persians_alt_building_04`, `s_romans_rome_tent`, same scale) instead of the house.
- **Map-only Persian warehouse (BLD 79, 2026-10-14).** 0.28's `pers/warehouse` names `props/structures/persians/alt_building_01.xml`; RTS0 draws that actor (`s_persians_alt_building_01.glb`, 2026-10-15). The hall, tavern, rampart tower, Kushite shrine, old Han fortress and Laozi gate (Han `shrine.xml`) use GLBs already in `assets/models/buildings/`.
- **Upgrade scaffolds (2026-10-15).** 0.28's structure actors prop a construction scaffold in their `upgrading` variant (`props/structures/construction/scaf_{3x3_tower,4x4,8x8,9x3_wall,cart_defense_tower,cart_wall_long,cart_wooden_tower}.xml`). These are extracted as `assets/models/buildings/scaffold_up_*.glb` (ZIP_STRUCTURES, 0.21 m scale). `tools/extract_upgrade_scaffolds.py --zip` composes each building GLB's structure actor and the scaffold actor as `compose_actor` does and writes the scale and offset that seat the scaffold on the GLB (`data/upgrade_scaffolds.json`, 79 GLBs; `--check` compares). The variant's `destruction_dust_small` particle rises from the structure while the scaffold stands (50 puffs a second, `dust_256a.png`); the one `destruction_dust_large_gray` variant uses the small dust too.
- **Forge effects (2026-10-15).** 0.28's forges show their `researching` variant while a technology is first in their queue (ProductionQueue.js SetAnimation): blacksmith sparks, forge smoke and flames on the forge's prop points, and the Han arsenal smokes while it trains as well. `tools/extract_production_fx.py --zip` writes the points and each GLB's metre for 21 building GLBs to `data/production_fx.json` (`--check` compares), and copies `sparks.png` and `smoke_64a_blacksmith.png` into `assets/textures/particles/`. The German forge's `researching` variant also props its forger, hammering at the anvil (`units/germans/forger.xml`), and the German temple's `researching` and `training` variants a murder of crows circling 27 m above it (`fauna/crow_circle.xml`). Both are extracted as animated unit GLBs at 0 A.D. metres (`germ_forger`, `crow_circle`) and placed by the same tool (`"glb"` entries, with the prop point's yaw), playing their Idle in a loop. The crows keep their wings in one pose: 0.28 animates each crow (crow_flight) inside the circling skeleton, and the GLB holds one skeleton. The Germans' wonder still bakes its crows in place.
- **Foundations and construction scaffolds (2026-10-15).** A structure being built draws its own 0.28 template's `VisualActor/FoundationActor` and, once building starts, the scaffold its `scaffold` variant props (Foundation.js). Before this it drew one of eight generic foundations and two scaffolds by building type. `tools/extract_foundations.py --zip` converts the 58 foundation actors the 15 civs' structures name (`fndn_*.glb`) and the scaffolds they prop (15 more `scaffold_up_*.glb`, shared with the upgrade scaffolds), at 0.21 GLB units per metre. It writes `data/foundations.json`: each civ's foundation per BuildingType (from `data/reference/templates.json`), each foundation's and scaffold's scale and actor origin, and the scale and actor origin of 594 building GLBs, so a foundation sits under whichever model is drawn. `--check` compares. Structures with no such frame (generic models with no structure actor) keep the generic foundation, and so does the placement ghost. The `scaffold` variant's `construction_dust` particle rises from the foundation at 2 puffs a second per builder at work (`numbuilders`, at most 16), with the particle file's lifetimes, box, speeds, sizes, colour and downward force in metres. Its dirt decal is drawn too (see the next bullet).
- **Ground decals (2026-10-15).** 0.28's structures and foundations prop a ground decal (`props/structures/decals/*.xml`: sand, dirt, gravel, paving), which CModelDecal lays on the terrain under them. The model extractors drop those props. `tools/extract_building_decals.py --zip` finds the decals of each building GLB's structure actor and the actors it props, maps their rectangles (width, depth, angle, offset) into the GLB's units, and writes `data/building_decals.json` (375 building GLBs with the rubble; `--check` compares). It also copies the 23 decal textures they use into `assets/textures/decals/` (at most 512 px). The 3D view draws them as Godot Decal nodes that land only on the terrain (render layer 20), darkened with the fog of war like the terrain under them. A site draws its foundation's decal. Not drawn: the decals' normal and specular maps.
- **Rubble (2026-10-15).** A destroyed 0.28 structure leaves the rubble entity its template's `Health/SpawnEntityOnDeath` names: stone rubble sized to its footprint (`destruct_stone_2x2` to `6x6`, wall long and tower), the civic centres' and docks' own ruins and the Roman army camp's palisade. RTS0 drew one of three generic rubble models. `tools/extract_rubble.py --zip` converts the 26 rubble actors the 15 civs' structures name (`rubble_*.glb`, 0.21 GLB units per metre) and writes `data/rubble.json` with each civ's rubble per BuildingType and each rubble's scale and actor origin (`--check` compares). The 3D view lays it where the structure stood, through the building GLB's frame in `data/foundations.json`, with its ground decal (three more decal textures). As in `template_rubble.xml` Decay, it stands for 15 s and then sinks at 0.2 m/s. Fields keep the old look, because their rubble (`plot_field_fallow`) is only a decal.
- **Ambient smoke and fire (2026-10-15).** Many 0.28 structure actors prop particle emitters in the variants they always show: chimney smoke on the Briton and German houses, the temples' and camps' fires, the braziers of the Roman temple, Ptolemaic Civic Center and Persian shrine, the lighthouse's beacon, and the flames and smoke of the civic centres' and stone rubble. The model extractors drop those props. `tools/extract_ambient_fx.py --zip` finds the `art/particles/*.xml` props of each building GLB's structure actor and the actors it props, maps them into the GLB's units, reads the 13 particle files they name (rate, lifetimes, emitter offset and box, velocities, size and growth, force, colour, blending) and writes `data/ambient_fx.json` (66 building GLBs; `--check` compares). It copies `smoke_128b.png`, `smoke_128d_light.png` and `smoke_curved.png` into `assets/textures/particles/`. The 3D view turns each into a GPUParticles3D on the structure or its rubble (which carries them as it sinks). The foundations' construction dust stays per builder. Not drawn: particle hue variation (each emitter takes its colour range's middle grey) and the prop points' own rotation for `use_relative_velocity` smoke (it turns with the structure).
- **Destruction dust (2026-10-15).** A 0.28 structure actor's `death` variant props the dust and smoke that rise while it collapses (`art/variants/structures/destruction_{small,medium,large}.xml`: `destruction_dust_*`, `destruction_dust_*_gray`, `destruction_smoke_*`). `tools/extract_ambient_fx.py` also writes those props for each building GLB under `"death"` in `data/ambient_fx.json` (471 GLBs) and copies `smoke_256a.png`. The 3D view starts them when a structure is destroyed or deleted, emits for 2 seconds, and lets the cloud drift and fade over the next 15 while the structure sinks into its rubble.
- **Dust under runners (2026-10-15).** 0.28's horse, camel and elephant `Run` variants (`art/variants/quadraped/base_{horse,camel,elephant}_run.xml`) and the running fauna actors prop `dust_cav` on the root. `tools/extract_ambient_fx.py` lists them per unit GLB under `"run"` in `data/ambient_fx.json` (224 GLBs: cavalry, camels, elephants, chariots, traders, packed siege and the horses, camels, elephants, giraffes and tigers of Gaia) and copies `dust_cav.png`. While such a unit plays its Run clip (or an animal flees), the 3D view puffs the dust at its feet at the file's 6 a second, through one world-space emitter per scale. A flying catapult stone trails 0.28's `flame_catapult` and `smoke_catapult` the same way (`"projectiles"`, from `props/units/weapons/rock_flaming.xml` and `siege_artillery/stone_projectile_{med,large}_flaming.xml`, which every 0.28 stone thrower flings but the Han mangonel), with `smoke_128a.png`.
- **0.28 roster additions (checked 2026-10-13).** Every roster row's 0.28 template actor was compared with the model it draws. The Roman conscript, auxiliary spear and javelin cavalry and First Cohort champion and the Carthaginian Italic spearman (rows 450-454) drew their civ's class model and now draw their own 0.28 actor; Carthage's Iberian javelineer draws 0.28's fire-javelin actor (`infantry_javelinist_b_fire.xml`). `unit_template_models.py` no longer reads 0.28's mercenary `infantry_*_m.xml` as mounted. The 10 German template models also join the template table, so they take their class model's scale and the three without clips play their class model's. Checked again against 0.28 on 2026-10-14: the Iberians' own javelineer (`iberians/infantry_javelinist_b.xml`), which had shared Carthage's fire-javelin model, and the burning fire ship (`iberians/fireship_fire.xml`, the four `ship_fire_fire` rows, which drew the unburnt ship) were the last two rows without their own 0.28 actor; both now draw it (`iber_infantry_javelineer_b`, `brit_ship_fire_fire`).
- **Map-only units, rows 482-501 (2026-10-15).** The map-only 0.28 units Triggers added (#906) draw their own 0.28 actor. Eight new GLBs: the Han crossbow cavalryman `han_cavalry_crossbowman_b`, the Samnite skirmisher and spearman, the pirates' scout, arrow and merchant ships, the Noldor warship and the Viking longship. The Gaia rows take the generic class models' scale. The Han hero class model `han_hero` (0.28's infantry Han Xin) is converted for the first time. The other twelve rows reuse models that already carry their actor: Bindusara's and Chandragupta's infantry forms, Harsiotef, Liu Bang, Wei Qing and his horse, the Kushite and Han class models, the Roman quinquereme for the Ptolemaic juggernaut, the Thracian swordsman for the Black Cloak, and the Carthaginian swordsman for the Samnite swordsman. Compare covers rows 463-481.
- **The Theban fire-raiser, row 506 (2026-10-15).** Survival of the Fittest's Gaia fire-raiser (`units/theb_siege_fireraiser`) draws its own 0.28 actor, `units/thebans/siege_fireraiser.xml`: `theb_siege_fireraiser.glb`, the fire cart with its two Athenian operators (oxybeles rig, their Idle, Walk, Run, Attack and Death clips), converted from 0.28's `public.zip` by `extract_0ad_assets.py --zip --templates` and spliced into `unit_template_models.h` by hand, at the generic bolt shooter's scale; its cart skin takes the player colour (`extract_player_masks.py --zip`). Its siphon's smoke and flame props burn on it (`data/ambient_fx.json` `units`, `RtsoWorldView::emit_unit_fx`); the flaming pig carries its own `flame_pig` and smoke the same way.
- **Advanced and elite ranks (2026-10-14).** 0 A.D. promotes a `_b` template to its `_a` and `_e` templates, and 0.28 gives most of them their own actor (helmets, armour, crests). RTS0 keeps the roster row and raises the rank, so `unit_template_models.h` now has a rank table (`unit_template_rank_models`, written by `tools/unit_template_models.py` `rank_plan` through `extract_0ad_assets.py --templates`), and the world view draws an advanced (rank 2) or elite (rank 3) unit with its rank's model. The Romans are done (12 new GLBs; the advanced spearman and swordsman reuse the army camp's `_a` models). Compare's nine civs followed (athen, spart, mace, sele, ptol, pers, han, maur, kush: 114 new GLBs, the rest reuse existing `_a`/`_e` models). The Carthaginians, Britons, Gauls, Iberians and Germans followed (64 new GLBs, 2026-10-15), so every civ's promoted units now draw their rank's 0.28 actor.
- **0.28 art refresh, Han, Persians, Ptolemies, Seleucids and Spartans (checked 2026-10-14).** Each of these civs' unit GLBs was re-extracted from 0.28's `public.zip` and compared with main: meshes, skins, bones and clips. Seven differed and were replaced, with Capture, Idle2 and Death2 re-added. Brasidas gets 0.28's crested pilos and two more parts; Agis, the Ptolemaic mercenary and Seleucid thureophoros spearmen (with 0.28's tunic masks) and the Ptolemaic and Seleucid scout and siege ships get 0.28's meshes. The rest, Han Xin, the Han swordsman, the Persian ram, merchant ships and archers, the Han, Ptolemaic and Seleucid siege towers and others, match 0.28 apart from texture re-encoding, and their actor changes are invisible (formation anim variants, `selectable` on garrison flags).
- **Fauna.** 61 species use 51 models (checked 2026-10-09). The 10 remaining pairs share one 0 A.D. actor too (for example both sheep, both camels, the wild and domestic zebu). The 29 converted with `tools/extract_0ad_fauna_models.py` (Collada source, PR #179) have 0 A.D.'s own mesh, texture and Idle/Walk/Run/Attack/Death clips. 23 of them also carry the actor's next most frequent idle (`Idle2`) and 10 its second death (`Death2`); `world_view_node` gives a quarter of the animals the second idle and half the second death, by id, as for units.
- **Props and scenery.** 19 Gaia scenery types now use 0 A.D. actors (22 GLBs); ambient hawks fly on 34 maps. Turreted units stand at 0 A.D.'s turret point.

Animations (363 unit GLBs):
- 23 unit GLBs have no clips: projectiles, and hulls and animals whose 0 A.D. actors have none either (Celtic, Han, Iberian and Mauryan warships, the merchant ships, the Iberian fishing ship, muskox, walrus, humpback whale; checked 2026-10-09). Every hull rocks in the world view (`apply_ship_wave_xform`). Oars: 29 ship GLBs (the 14 sailing warships, 7 rams, the Han tower ship, and the Germans' arrow ship, scout ship, karvi, fishing boat, knarr and snekkja from 0.28's public.zip) key each oar with its 0 A.D. prop actor's idle and Walk clips (`other/ship_oar_*.dae`, `tools/add_0ad_oar_clips.py`; keyed nodes are `oarx_*`). Template ships that borrow their class model's clips still stroke their `oar_*` nodes with the runtime sine (`apply_ship_oar_stroke`). The rams and the tower ship were exported static with every oar baked into the hull; the tool gives each oar node its transform back first.
- One `Attack` clip per unit: no melee/ranged split per weapon. `Slaughter` is separate, and the 166 biped GLBs carry 0 A.D.'s `Capture` clip (`biped/infantry/capturing_a.dae`, added by `tools/add_0ad_clips.py`). Cavalry, siege and ships fall back to `Attack` when capturing.
- Body skin variants (2026-10-15): 0 A.D. picks each unit's actor variants by frequency, and most unit actors have a group that only swaps the body skin (the Athenian spearman's six tunics). The GLBs bake one. `tools/extract_unit_variants.py --zip` finds that group for each unit GLB, matches its embedded images to the skins to find the body (as `extract_player_masks.py` does), and copies the skins at the body image's size with the player-colour mask in alpha to `assets/textures/unit_variants/` (1008 skins for 1295 GLB images in `data/unit_variants.json`, at most 8 per image; `--check` compares). The props' actors (heads, helmets, shields, capes) have such groups too, and each GLB image takes the group its skin is in; skins without player colour are written with alpha 0 (`*_plain.png`). `ModelLibrary` gives the image's material the skins as a Texture2DArray and the cumulative frequencies, and `shaders/player_color.gdshader` draws the layer each unit's id picks (crowds pass the draw in INSTANCE_CUSTOM alpha, close-ups in `variant_u`), offset per image (`variant_salt`) so head, helmet and shield pick independently, as 0 A.D.'s groups do. Not drawn: variant groups that swap meshes or props.
- Idle and death variants: 0 A.D. picks one of an actor's same-named clips at random. 151 biped GLBs carry a second idle (`Idle2`) and 192 a second death (`Death2`), the actor's next most frequent (`tools/add_0ad_clips.py --variants`), and each unit keeps one by its id. The horse, camel and elephant GLBs add 65 `Idle2` and 43 `Death2` (`--mounts`): the mount's next clip, with the rider playing its own on the mount's prop point. Horse manes and tails are skinned to the horse as in 0 A.D. on 73 cavalry GLBs (`tools/skin_0ad_mount_props.py`; new extracts skin them directly). Still rigid on the `hair` bone: the chariot and trader horses, which are props on a cart rig. The 19 Han, Kushite and Mauryan cavalry GLBs seat their own `*_r` rider, animated by the cavalry rider clips, and the 4 Kushite and Mauryan war elephants seat their driver and archers in the sit pose; before, those riders stood rigid on the saddle (`_mount_crew` in `tools/extract_0ad_assets.py`).
- Siege: the 25 unpacked engines have no `Walk`, as in 0 A.D., where an engine only moves packed. A packed engine shows its 0 A.D. `*_packed` actor (a wagon with horses or oxen, crew and cargo) in 13 GLBs from `tools/extract_0ad_packed_siege.py`, mapped in `src/graphics/assets/packed_siege_models.h`. 0 A.D. has no pack/unpack clips: the model swaps when packing or unpacking ends. `GatherFish` is on 3 GLBs only.
- Done: citizen-soldier gather/build clips, per-resource carry walks and the promotion clip (#149, added at runtime from `.psa`).

Textures:
- **Player colour: done (player colour PR).** `extract_0ad_assets.py` bakes a fixed tint into each skin where the `player_trans` alpha is below 250. `tools/extract_player_masks.py` recovers 0 A.D.'s mask for 1424 GLB images (328 unit and 338 building models) and `shaders/player_color.gdshader` swaps the baked tint for the owner's colour. Not coloured: fauna, rubble and foundations, the Gaul and Briton archers (their 0.28 skins carry no mask either), and 10 structures (Gaul walls and towers, the sentry and defense towers, pyramids, siege wall, rowboat) whose skins on 0 A.D.'s current master have no mask or no longer match the extracted ones. The Gaul, Briton and Carthaginian citizens, the Seleucid javelineer, the catapults and the bolt shooter take their masks from 0.28's skins (`extract_player_masks.py --zip <0.28 public.zip>`, 2026-10-13); master lacks the `skeletal/germ/` skins they use.
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
ls assets/models/vegetation/*.glb | wc -l  # 91
ls assets/animations/*.psa | wc -l         # 1316
ls assets/skeletons/*.xml | wc -l          # 98
ls assets/textures/terrain/*.png | wc -l   # 653 (652 manifest entries + splat.png)
```

To re-export assets, see the export command in [assets/README.md](../assets/README.md).
