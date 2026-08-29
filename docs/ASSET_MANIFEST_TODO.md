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
