# 0 A.D. attribution and licensing

RTS0 builds on **0 A.D.** (https://play0ad.com) by Wildfire Games.

- 0 A.D. source code is **GPL-2.0-or-later**. RTS0 copies and ports parts of it, so
  RTS0's code is licensed **GPL-2.0-or-later** as well (top-level `LICENSE`).
- 0 A.D. art, audio and maps are **CC BY-SA 3.0**. RTS0's converted copies keep that
  license (`assets/LICENSE`).

## Code copied or ported from 0 A.D.

| RTS0 path | 0 A.D. source | Kind |
|---|---|---|
| `src/0ad/**` | `source/maths`, `source/simulation2/helpers`, `source/network`, `source/ps/CStr.h` | Verbatim copy, original headers kept, not compiled (reference only) |
| `src/core/math/fixed.h` | `source/maths/Fixed.h`, `Sqrt.cpp` | Port |
| `src/simulation/nav/long_pathfinder.h` | `source/simulation2/helpers/LongPathfinder.*` | Port (JPS) |
| `src/simulation/nav/geometry.h` | `source/simulation2/helpers/Geometry.*` | Port |
| `src/simulation/nav/rasterize.h` | `source/simulation2/helpers/Rasterize.*` | Port |
| `src/simulation/nav/vertex_pathfinder.h` | `source/simulation2/helpers/VertexPathfinder.*` | Port |
| `src/simulation/grid_nav.h` | `Grid.h`, `HierarchicalPathfinder.*`, `Pathfinding.h` | Port |
| `src/simulation/petra_ai.h`, `simulation.*` | `simulation/ai/petra/*.js`, `simulation/components/UnitAI.js` and other components | Port / native rewrite of game logic |
| `src/simulation/rms_generator.*` | 0 A.D. random map scripts (`maps/random/`) | Modeled on them; how closely is not yet checked |
| `src/network/turn_manager.*`, `net_types.h` | `source/network/NetServerTurnManager.*`, `NetClientTurnManager.*`, `NetMessages.h` | Port |
| `tools/extract_0ad_assets.py` | `source/graphics/ModelDef.cpp`, `ps/FileIo.cpp`, `shaders/glsl/model_common.fs` | Port of file-format readers |

## Data from 0 A.D. (CC BY-SA 3.0)

| RTS0 path | Contents | Produced by |
|---|---|---|
| `assets/models/`, `assets/animations/`, `assets/skeletons/` | Unit, building and flora meshes, PSA clips, skeletons | `tools/extract_0ad_assets.py` |
| `assets/textures/terrain/`, `assets/textures/ui/` | Terrain textures, portraits, icons, emblems | `tools/extract_0ad_assets.py`, `tools/extract_ui_assets.py` |
| `assets/music/`, `assets/sounds/`, `assets/audio/`, `audio/` | Music, sound effects, voice lines | `tools/extract_ui_assets.py`, `tools/extract_combat_sfx.py` |
| `data/maps/` | Scenario maps converted to JSON | `tools/port_0ad_maps.py` |
| `src/simulation/civ_roster_data.h`, `data/civs/rosters.json` | Per-civ unit rosters, unit stats, hero auras and civ bonus text from `simulation/templates/` and `simulation/data/{civs,auras}/` | `tools/extract_0ad_rosters.py` |
| `src/simulation/sim_types.h` (building table rows 14-25, field supply constants) | Structure stats from 0 A.D. 0.28 `simulation/templates/template_structure_*.xml` (Health, Cost, GarrisonHolder, Capturable, Identity/Requirements) and the field `ResourceSupply` (MaxGatherers, DiminishingReturns); the diminishing-returns formula follows `simulation/components/ResourceSupply.js` | Read by hand from the 0.28 `public.zip` |
| `src/simulation/tech_data.h` | 83 technologies (names, descriptions, costs, research times, researcher structure, phase, prerequisites, civ limits, modifications) from 0 A.D. 0.28 `simulation/data/technologies/*.json`; the Village/Town structure counts for phasing follow `phase_town_generic.json` / `phase_city_generic.json`; modification paths cover `Cost/BuildTime`, `Researcher/TechCostMultiplier/time`, `Heal/*`, `Trader/GainMultiplier` and `BuildingAI` arrow counts | `tools/extract_0ad_techs.py` |

Fonts in `assets/fonts/` carry their own licenses; see `assets/LICENSE`.

## Rules when adding more

- Copying or porting more 0 A.D. code: keep or add a note at the top of the file naming
  the 0 A.D. source and GPL-2.0-or-later, and add a row above.
- Pulling more 0 A.D. data: it stays CC BY-SA 3.0; add a row above if it lands in a new folder.
- Shipping a build: include `LICENSE`, `assets/LICENSE` and a credits screen or file
  naming Wildfire Games and the 0 A.D. contributors, and point to RTS0's source.
