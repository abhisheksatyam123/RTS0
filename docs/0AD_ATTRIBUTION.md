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
| `PetraConfig::gather_rate`/`build_time`, `ai_gather_mult`/`ai_time_mult` | `simulation/helpers/InitGame.js` ("AI Bonus" difficulty modifiers) | Port |
| `src/simulation/rms_generator.*` | 0 A.D. random map scripts (`maps/random/`) | Modelled on them, one row per map in the table below |
| `src/simulation/simulation.cpp` (`tick_pack`, `order_pack`), `sim_types.h` (`PackState`, `unit_has_pack`) | `simulation/components/Pack.js`, the pack/unpack rules in `simulation/components/UnitAI.js` (`g_OrdersCancelUnpacking`, `EnsureCorrectPackStateForAttack`, PACKING/UNPACKING states), `Pack/Time` from `simulation/templates/template_unit_siege_{stonethrower,boltshooter}.xml`, and Petra's stand-ground stance for packable siege from `simulation/ai/petra/attackPlan.js` (0 A.D. commit 61a3b95) | Port |
| `src/simulation/simulation.*` (`cmd_tribute`, `cmd_barter`, `tick_barter`, `calculate_trade_gain`, trader routes, merchant ship dock trade, `dock_sea`) | `simulation/components/Player.js` (TributeResource, GetNextTradingGoods), `Barter.js`, `Market.js` (CalculateTraderGain), `Trader.js`, `globalscripts/Trade.js`, `template_unit_support_trader.xml`, `template_structure_economic_market.xml`, `template_structure_military_dock.xml` (Market TradeType naval), `ai/petra/tradeManager.js` (sea routes) | Port (formulas and constants) |
| `src/simulation/simulation.*` (fish gathering, `find_free_fish`, regrowth), `src/simulation/rms_generator.cpp` (`distribute_fish`), `tools/port_0ad_maps.py` (`gaia/fish/`), fish entries in `data/maps/*.json` | `templates/template_gaia_fish.xml` (ResourceSupply food.fish 1000, MaxGatherers 4, regen), `template_unit_ship_fishing.xml` (ResourceGatherer food.fish), `maps/random/rmgen` fish placement (`scaleByMapSize(15, 50)` groups, avoid land 2, other fish 20), the 0 A.D. skirmish maps' fish entities | Port (constants, placement rules and map data) |
| `src/network/turn_manager.*`, `net_types.h` | `source/network/NetServerTurnManager.*`, `NetClientTurnManager.*`, `NetMessages.h` | Port |
| `tools/extract_0ad_assets.py` | `source/graphics/ModelDef.cpp`, `ps/FileIo.cpp`, `shaders/glsl/model_common.fs` | Port of file-format readers |

## Random maps modelled on 0 A.D. (`src/simulation/rms_generator.cpp`)

Each `synth_<id>` function generates RTS0's version of the 0 A.D. map script of the same name. The older RTS0 random maps (Continental, Archipelago, Danubius, Jebel Barkal and others) are RTS0's own and not listed. The scripts are 0 A.D. code (GPL-2.0-or-later).

| RTS0 map | 0 A.D. source | Kind |
|---|---|---|
| Mainland | `maps/random/mainland.js` | Modelled on it, layout from memory |
| Alpine Lakes | `maps/random/alpine_lakes.js` | Modelled on it, layout from memory |
| Lake | `maps/random/lake.js` | Modelled on it, layout from memory |
| Gulf of Bothnia | `maps/random/gulf_of_bothnia.js` | Modelled on it, layout from memory |
| Ardennes Forest | `maps/random/ardennes_forest.js` | Modelled on it, layout from memory |
| Near-Eastern Badlands (`nearestern_badlands`) | `maps/random/neareastern_badlands.js` | Modelled on it, layout from memory |
| Rivers | `maps/random/rivers.js` | Modelled on it, layout from memory |
| Canyon | `maps/random/canyon.js` | Modelled on it, layout from memory |
| Corinthian Isthmus | `maps/random/corinthian_isthmus.js` | Modelled on it, layout from memory |
| Belgian Uplands | `maps/random/belgian_uplands.js` | Modelled on it, layout from memory |
| Lorraine Plain | `maps/random/lorraine_plain.js` | Modelled on it, layout from memory |
| Hyrcanian Shores | `maps/random/hyrcanian_shores.js` | Modelled on it, layout from memory |
| Northern Lights | `maps/random/northern_lights.js` | Layout read from the script: sea shore at f = 0.69, player line at f = 0.45, ice floes, 1-4 lakes per player |
| Kerala | `maps/random/kerala.js` | Layout read from the script: sea shore at f = 0.31, player line at f = 0.55, mountains past f = 0.69 |
| Polar Sea | `maps/random/polar_sea.js` | Layout read from the script: central sea, 10-16 small lakes, players at f = 0.35; RTS0 keeps a few groves |
| Sahel | `maps/random/sahel.js` | Layout read from the script: players at f = 0.35, 1-3 water holes 24 tiles from bases, scarce wood |
| African Plains | `maps/random/african_plains.js` | Layout read from the script: players at f = 0.35, 6-16 hills, 4-10 watering holes |
| Alpine Valley | `maps/random/alpine_valley.js` | Layout read from the script: mountain ranges from f = 0.49 to 0.18 between players, with passages |
| Syria | `maps/random/syria.js` | Layout read from the script: players at f = 0.35, scaleByMapSize(1, 3) * 3 hills per player |
| Cantabrian Highlands | `maps/random/cantabrian_highlands.js` | Layout read from the script: each player on a cliff-ringed hill with a ramp towards the centre, 1-4 lakes per player |
| Pyrenean Sierra | `maps/random/pyrenean_sierra.js` | Layout read from the script: a range f = 0.68 long through the centre, passes at 0.35 and 0.65; RTS0 leaves out the ocean |
| Sahel Watering Holes | `maps/random/sahel_watering_holes.js` | Layout read from the script: water lines from f = 0.15 to 0.49 between neighbours, pools at both ends, shallows |
| Persian Highlands | `maps/random/persian_highlands.js` | Layout read from the script: high land with a sunken central plateau, rocky hills, players at f = 0.35 |
| Volcanic Lands | `maps/random/volcanic_lands.js` | Layout read from the script: a volcano (cone and crater) in the centre, 2-8 hills per player |
| Lion's Den | `maps/random/lions_den.js` | Layout read from the script: high ground, sunken valley ring (f = 0.29) round a central hill (f = 0.21), dens at f = 0.4 with ramps to expansions and neighbours |
| Hell's Pass | `maps/random/hells_pass.js` | Layout read from the script: players on lines out from the centre (f = 0.2), mountain spines from f = 0.075 to 0.42 between them; each player is its own team |
| Deep Forest | `maps/random/deep_forest.js` | Layout read from the script: players at 5/8-3/4 of the radius with jittered angles, sunken paths between bases and to the centre, its middle-hill and rolling-hill height formula, dense wood |
| India | `maps/random/india.js` | Layout read from the script: players at f = 0.35, one half dried-up lake within scaleByMapSize(15, 40) of the centre |
| Guadalquivir River | `maps/random/guadalquivir_river.js` | Layout read from the script: continent of radius f = 0.49 round a point f = 0.2 off centre, a river f = 0.07 wide with shallows at 0.3-0.4, 0.5-0.6 and 0.7-0.8 of its length, players on arcs at f = 0.35 split by the river |
| Fields of Meroe | `maps/random/fields_of_meroe.js` | Layout read from the script: the Nile at f = 0.25 turned by pi/5, two cataract crossings, players placed at random clear of the river, dunes |
| Gear | `maps/random/gear.js` | Layout read from the script: central lake (f = 0.27), land ring (0.21), shallow ring (0.17), island (0.14) and hill (0.12), rivers and paths as spokes with 0 A.D.'s per-size split table; fords where paths cross the shallow ring |
| Atlas Mountains | `maps/random/atlas_mountains.js` | Layout read from the script: players at f = 0.35, scaleByMapSize(20, 120) mountains kept 20 tiles from bases, scarce wood |
| Frontier | `maps/random/frontier.js` | Layout read from the script: random base elevation, a random placement pattern at f = 0.2-0.35, a random mix of hills, mountains, plateaus and lakes; no teams, so one player per spoke |
| Wild Lake | `maps/random/wild_lake.js` | Layout read from the script: diamond-square terrain seeded low in the middle, water level set so about a fifth is under water; RTS0 places players on a circle |

## Data from 0 A.D. (CC BY-SA 3.0)

| RTS0 path | Contents | Produced by |
|---|---|---|
| `assets/models/`, `assets/animations/`, `assets/skeletons/` | Unit, building and flora meshes, PSA clips, skeletons | `tools/extract_0ad_assets.py` |
| `assets/textures/terrain/`, `assets/textures/ui/` | Terrain textures, portraits, icons, emblems | `tools/extract_0ad_assets.py`, `tools/extract_ui_assets.py` |
| `assets/music/`, `assets/sounds/`, `assets/audio/`, `audio/` | Music, sound effects, voice lines | `tools/extract_ui_assets.py`, `tools/extract_combat_sfx.py` |
| `data/maps/` | 0 A.D. skirmish maps (`maps/skirmishes/*_Np.xml` + `.pmp`: Acropolis Bay, Alpine Mountains, Alpine Valleys, Arabian Oases, Atlas Valleys, Bactria, Belgian Bog, Corinthian Isthmus, Cycladic Archipelago, Death Canyon, Deccan Plateau, Farmland, Forest Battle, Gallic Fields, Gambia River, Golden Island, Greek Acropolis, Hindu Kush, Hydaspes River, Isthmus of Corinth, Libyan Oases, Lorraine Plain, Median Oasis, Mediterranean Cove, Miletus Peninsula, Near-Eastern Badlands, Nile River, Northern Island, Oceanside, Persian Highlands, Punjab, Saharan Oases, Sahel, Savanna River, Sicilia, Sporades Islands, Syria, Tuscan Acropolis, Two Seas, Vesuvius, Watering Holes, Zagros Mountains) and scenarios (Arcadia, Battle for the Tiber), converted to JSON | `tools/port_0ad_maps.py` (`--github` fetches from 0 A.D.'s repository) |
| `src/simulation/civ_roster_data.h`, `data/civs/rosters.json` | Per-civ unit rosters, unit stats, hero auras and civ bonus text from `simulation/templates/` and `simulation/data/{civs,auras}/` | `tools/extract_0ad_rosters.py` |
| `src/simulation/civ_roster_data.h` (`civ_team_bonus_table`) | Each civ's team bonus from `simulation/data/auras/teambonuses/*.json`; mercenary costs and train times from `simulation/templates/mixins/{mercenary,merc_*}.xml` | `tools/extract_0ad_rosters.py` |
| `src/simulation/civ_roster_data.h` (siege tower rows, `ROSTER_PACKABLE`) | Siege towers from `simulation/templates/template_unit_siege_tower.xml` and `units/{mace,kush,ptol,sele,han}/siege_tower.xml`; packable siege from each template's `Pack` component | `tools/extract_0ad_rosters.py` |
| `src/simulation/sim_types.h` (building table rows 14-25, field supply constants) | Structure stats from 0 A.D. 0.28 `simulation/templates/template_structure_*.xml` (Health, Cost, GarrisonHolder, Capturable, Identity/Requirements) and the field `ResourceSupply` (MaxGatherers, DiminishingReturns); the diminishing-returns formula follows `simulation/components/ResourceSupply.js` | Read by hand from the 0.28 `public.zip` |
| `src/simulation/tech_data.h` | 83 technologies (names, descriptions, costs, research times, researcher structure, phase, prerequisites, civ limits, modifications) from 0 A.D. 0.28 `simulation/data/technologies/*.json`; the Village/Town structure counts for phasing follow `phase_town_generic.json` / `phase_city_generic.json`; modification paths cover `Cost/BuildTime`, `Researcher/TechCostMultiplier/time`, `Heal/*`, `Trader/GainMultiplier` and `BuildingAI` arrow counts | `tools/extract_0ad_techs.py` |
| `src/simulation/sim_types.h` (`building_upgrade`), `src/simulation/simulation.cpp` (`cmd_upgrade_building`, `finish_upgrade`, `tick_upgrades`) | Structure upgrade costs, times and phase requirements from 0 A.D. 0.28 `template_structure_defensive_tower_sentry.xml`, `structures/iber/sentry_tower.xml` and `template_structure_defensive_wall_long.xml`; the upgrade rules follow `simulation/components/Upgrade.js` and `helpers/Transform.js` (`ChangeEntityTemplate`, GPL-2.0-or-later) | Hand-ported |
| `src/simulation/simulation.cpp` (`tick_gates`, `cmd_lock_gate`), `Simulation::blocks_pathfinding` / `blocks_movement_now` | Gate opening, closing and locking follow 0 A.D. 0.28 `simulation/components/Gate.js` and the gate template's Obstruction flags (GPL-2.0-or-later / CC BY-SA 3.0) | Hand-ported |
| `src/simulation/sim_types.h` (`SIEGE_TOWER_GARRISON`, `SIEGE_RAM_GARRISON`, `SIEGE_TOWER_MAX_ARROWS`, `SIEGE_EJECT_HEALTH`), `src/simulation/simulation.cpp` (`siege_garrison_capacity`, `siege_tower_arrows`, `tick_siege_garrisons`) | Garrison capacity, allowed classes, eject health and garrison arrows from 0 A.D. 0.28 `template_unit_siege_tower.xml` and `template_unit_siege_ram.xml` (GarrisonHolder, BuildingAI) | Hand-ported |
| `src/simulation/tech_data.h` (`TECH_SIEGE_PACK_UNPACK`, generated), `src/simulation/simulation.cpp` (`pack_time_s`, `TSTAT_PACK_TIME`) | Military Engineers from 0 A.D. 0.28 `simulation/data/technologies/siege_pack_unpack.json` (Pack/Time x0.6 for Siege, 800 food, 40 s, City phase) | Generated by tools/extract_0ad_techs.py |
| `src/simulation/sim_types.h` (`building_garrison_heal`, `building_eject_health`), `src/simulation/simulation.cpp` (`tick_garrison_holders`), `src/simulation/tech_data.h` (`TECH_GARRISON_HEAL`, generated) | BuffHeal and EjectHealth from 0 A.D. 0.28 `template_structure_*.xml` GarrisonHolder, heal timer and eject rule from `simulation/components/GarrisonHolder.js` (HealTimeout, HasEnoughHealth), Living Conditions from `simulation/data/technologies/garrison_heal.json` | Hand-ported; tech generated by tools/extract_0ad_techs.py |
| `src/simulation/simulation.cpp` (`petra_set_rushes`, `petra_plan_sizes`, `find_petra_rush_target`, `find_petra_raid_target`, `petra_manage_tactical_attacks`, `petra_manage_raid`), `petra_ai.h` (`PetraAttackManager`) | `simulation/ai/petra/attackManager.js` (`setRushes`, plan choice in `update`, `raidTargetEntity`), `attackPlan.js` (Rush/Attack/HugeAttack/Raid `unitStat` sizes, size variation, `rushTargetFinder`, launch rules in `updatePreparation`), `headquarters.js` (`gameStart` wood check), `defenseManager.js` (`targetList`) (0 A.D. commit 61a3b95) | Port |
| `src/simulation/statistics_tracker.h`, `src/simulation/simulation.cpp` (`stat_*` hooks, `tick_statistics`, `percent_map_explored`, `percent_map_controlled`) | Counters, unit and building classes, values, 30 s samples and map-control peaks from 0 A.D. 0.28 `simulation/components/StatisticsTracker.js` | Port |
| `scenes/session/summary_screen.gd` | Tabs, columns and score formulas from 0 A.D. 0.28 `gui/summary/layout.js`, `counters.js`, `summary.js` | Port |
| `src/presentation/match_archive.cpp`, `scenes/menu/replay_browser.gd` | Replay folder layout (commands plus metadata), replay list and actions from 0 A.D. 0.28 `source/ps/Replay.cpp`, `gui/replaymenu/*` | Port |
| `scenes/session/savegame_dialog.gd` | Save and load dialogs from 0 A.D. 0.28 `gui/savedgames/save.js`, `load.js`, `savegame_list.js` | Port |

Fonts in `assets/fonts/` carry their own licenses; see `assets/LICENSE`.

## Rules when adding more

- Copying or porting more 0 A.D. code: keep or add a note at the top of the file naming
  the 0 A.D. source and GPL-2.0-or-later, and add a row above.
- Pulling more 0 A.D. data: it stays CC BY-SA 3.0; add a row above if it lands in a new folder.
- Shipping a build: include `LICENSE`, `assets/LICENSE` and a credits screen or file
  naming Wildfire Games and the 0 A.D. contributors, and point to RTS0's source.
