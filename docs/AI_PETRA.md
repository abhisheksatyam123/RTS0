# Petra AI Parity vs 0AD ai/petra — 32-Module Gap Audit (2026-08-29)

> **Status:** `src/simulation/simulation.h:393` + `simulation.cpp:2828` implement a **700 LOC monolith** (`tick_petra_ai` → 6 sequential managers) vs 0AD `binaries/data/mods/public/simulation/ai/petra/` **32 modules**. Tracked as **G10** in `docs/REMAINING_GAPS.md` (if present). This doc is the Phase 7 DoD reference.

## Overview — Fidelity Table

| 0AD Module (ai/petra/) | 0AD LOC / Role | RTS0 Location | Fidelity | Gap |
|---|---|---|---|---|
| `config.js` | 395 LOC — difficulty, personality, build orders, phase timings, gather ratios, disabled templates | *none* — hard-coded `ai_food_=400, ai_wood_=600, petra_phase_=1` (`simulation.h:420`) | **Missing** | No JSON/personalities, no per-civ overrides |
| `headquarters.js` | 1463 LOC — HQ master: base discovery, building placement via `obstructionMap`, tech research queue, worker assignment | `simulation.cpp:2828` `petra_manage_workers/buildings/research/economy` + `update_petra_threat_map` — inline in `Simulation` (~700 LOC monolith) | **Stub ~30%** | No `HQ` class, no separate `BaseManager` per base, no `obstructionMap` scan |
| `attackManager.js` + `attackPlan.js` | Multi-plan attack manager, unit composition, target analysis, army merging, `AttackPlan` FSM (Muster/March/Engage) | `simulation.h:404` `PetraStrikeForce {MUSTER,MARCH,ENGAGE} unit_ids, staging_x/z, target_{x,z,building_id}, state_timer}` + `petra_manage_tactical_attacks()` (`simulation.cpp:3379` ~110 LOC, single strike force) | **Stub ~25%** | One army only, no `attackNumber` queue, no `HQ.attackManager` composition, no naval `sea` attacks |
| `defenseManager.js` | Threat detection, garrison orders, counter-attack, territory defense, `army` assignment | `petra_manage_workers()` `threat >=7.0f` worker evac + `petra_manage_tactical_attacks()` `base_threat >=10.0f` emergency `order_attack_move` | **Stub ~20%** | No `defenseManager` entity, no `isUnderAttack` per base, no `防御` army pinning |
| `queueManager.js` + `queue.js` | Priority queues, `wantedGatherRates`, `queue` per building, `EconomyManager` deficit-driven training | `Building.queue: vector<ProductionOrder> timer/duration` (`simulation.h:79`) `per-building queue` + global `ai_food_/wood_/gold_/stone` counters | **Stub ~35%** | No `QueueManager` plan, no `priority` sorting, no `wantedGatherRates` per resource |
| `mapModule.js` / `borderMap.js` / `accessibility.js` / `territoryMap.js` | Full nav analysis: `obstructionMap`, `influenceMap`, `borderMap`, `accessibility` (land/water), `territory` influence | `simulation.h:397` `PetraThreatMap {danger[256], enemy_threat[256], friendly_defense[256]}` `THREAT_GRID 16×16=256` `update_petra_threat_map()` (`simulation.cpp:2737` falloff `1 - dist²/maxDist²`) | **Stub ~15%** | `16×16` coarse vs 0AD `~128×128` influence; no `obstructionMap`, no `accessibility` water checks, no `territoryMap` |
| `economy.js` / `economicManager.js` | `wantedGatherRates {food, wood, stone, metal}` deficit `300-food` etc, worker distribution, `gatherRateCache` | `petra_manage_economy()` (`simulation.cpp:3010`) `need_food=300-ai_food_` `need_wood=400-ai_wood_` `need_gold=250-ai_gold_` deficit allocation `pct_* = need_*/total_need` → `target_* = round(pct_* * workers)` + `reassignable_workers` | **Stub ~40%** | No `wantedGatherRates` config, fixed deficits, no `gatherType` cache, no `trade` post-setup |
| `buildingManager.js` + `baseManager.js` / `buildingPlacement.js` | `obstructionMap` + `borderMap` + `accessibility` → `findBestBuildLocation` spiral → `can_place` via `Engine` | `petra_manage_buildings()` (`simulation.cpp:3127`) spiral `for r=3..8` `dx=-r..r, dz=-r..r` `if abs(dx)==r||abs(dz)==r` `can_place(cx,cz,1|2)` — hard-coded `36,36` origin | **Stub ~30%** | Spiral 36,36 vs HQ anchor; no `obstructionMap`, no `territory` validity, no `foundation` build-progress checks beyond `0.05f` |
| `militaryManager.js` / `unitManager.js` | `findBestTrainableUnit(wantedRates)` + `counter` `spear vs cav 2.5×`, `cav vs archer 4.5×`, sorted score | `petra_manage_military_production()` (`simulation.cpp:2891`) `spearman_score=1.2+(cav*4.5+champ*3.5)*norm`, `archer_score`, `cavalry_score`, `siege_score`, `champion_score` sorted `vector<Option{type,score,bld_id}>` | **Stub ~50%** | Closest to parity — counter scores implemented, but no `UnitManager` classes, no `template` availability filter, no `tech` gating beyond `petra_phase_>=2/3` |
| `navalManager.js` | Transport, fishing, sea attack plans, `accessibility.isWater` | *none* — `UNIT_TRIREME` exists but no AI naval logic | **Missing** | RTS0 has `order_board_transport`/`order_disembark` but Petra never calls it |
| `tradeManager.js` | `Barter` + `Trade` `establishTradeRoute`, caravan assignment, `Market` economics | *none* — `cmd_barter` exists (`simulation.h`) but Petra never trades | **Missing** | No `tradeManager`, no caravan `UNIT_TRADER` dispatch |
| `diplomacyManager.js` | `diplomacy` stance, `isAlly/isEnemy`, tribute requests | *none* | **Missing** | No diplomacy state |
| `victoryManager.js` / `wonderManager.js` |Wonder/Relic victory timers, `has_active_wonder_` is data-only | *none* in AI — `simulation.h:349` `has_active_wonder_` read-only | **Missing** | AI does not pursue wonder/relic victory |
| `chat.js` / `statistics.js` | Flare pings `NMT_FLARE`, taunts, `statistics` tracking | *none* | **Missing** | No `SendFlareMessage` AI calls |
| `workerManager.js` / `trainingManager.js` | Dedicated worker micro, `queueManager` + `headquarters` worker-throttle | Folded into `petra_manage_workers` + `petra_manage_economy` evac only | **Stub ~25%** | No `WorkerManager` flee/garrison priority, no `trainingManager` queue merging |

**Counts:** 0AD `ai/petra/` ~32 `.js` files — RTS0 collapses to 6 methods in `simulation.cpp:2828,2891,3010,3127,3270,3379` + `petra_threat_map_.danger[256]` (`simulation.h:398`).

## Detail — Key File Comparisons

### `config.js` (395 LOC) vs none
0AD `petra/config.js` defines `difficulties:{sandbox, easy, medium, hard, veryHard}`, `personalities:{defensive, aggressive, balanced}`, `buildOrders`, `phase` timings (`town 300s, city 600s`), `desiredResGatherRates`. RTS0 has `ai_food_=400` etc inline, no config load. **Phase 7 DoD:** extract `docs/AI_PETRA.md` parity requires `config.js:395` equivalent (`rts0/petra/config.h` or `data/petra.json`).

### `headquarters.js` (1463 LOC) vs `simulation.cpp:2828` 700 LOC monolith
0AD `HQ` owns `baseManagers[]`, `attackManager`, `defenseManager`, `queueManager`, `mapModule`, `econ`. RTS0 `Simulation::tick_petra_ai()` (`simulation.cpp:3491`) does:
```cpp
ai_food_+=3.0f*dt; // trickle
update_petra_threat_map(dt);
petra_manage_workers(dt);
petra_manage_military_production(dt);
petra_manage_economy(dt);
petra_manage_buildings(dt);
petra_manage_research(dt);
petra_manage_tactical_attacks(dt);
```
No `HQ` object, no per-base isolation, no `obstructionMap` scanning.

### `attackManager.js` vs single strike force
0AD maintains `attackPlans[]` with `type: Rush|Raid|Large`, `units: Map<template,count>`, `target: influenceMap`. RTS0 `PetraStrikeForce` (`simulation.h:404`) holds `vector<uint32_t> unit_ids` single army, `staging 24,24` (actually `base*0.65`), thresholds `phase==1?6:phase==2?10:16`. **Missing:** multiple concurrent attacks, unit composition by `wantedRates`, naval transports.

### `defenseManager.js` vs worker evac
0AD `defenseManager` monitors `territoryMap`, orders `garrison` + `armies` to `defend`. RTS0 `petra_manage_workers` only does `threat>=7.0f` → `ACTION_MOVE` to `safe 36,36` + opportunistic `order_garrison` if `dx²+dz²<=16` and `garrisoned<cap`; `petra_manage_tactical_attacks` emergency `base_threat>=10.0f` recalls strike force.

### `queueManager.js` vs per-building queue
0AD `QueueManager` has `queues: {villager, military, building, tech}` priority sorted by `priority, cost`. RTS0 `Building.queue: vector<ProductionOrder>` FIFO per building, global `ai_*` counters pay immediately `ai_food_ -= uinfo.food_cost`. No cross-building priority.

### `mapModule` / `borderMap` / `accessibility` vs `THREAT_GRID 16 danger[256]`
0AD `mapModule` builds `obstructionMap` (512×512), `influenceMap`, `borderMap` (territory), `accessibility` (connected regions). RTS0 `THREAT_GRID_RES 16` `danger[256]` (`simulation.h:394`) `cell_size=128/16=8m`, `add_power` with `radius 1-3` and `falloff max(0.2, 1-dist²/(r²+1))`, decay `danger=0.3*old+0.7*raw`. Coarse, no obstruction or water check.

### `economy.js` `wantedGatherRates` vs deficit `300-food`
0AD `economyManager.wantedGatherRates` computed from `Account` deficit + `queueCost`. RTS0 `need_food=max(50,300-ai_food_)` etc, `pct_*`, `target_*=round(pct_* * workers)` hard-coded. No `Account` class.

### `buildingManager.js` `obstructionMap` vs spiral `36,36` `can_place`
0AD `HQ.findBuildingLocation(template, obstructionMap, influenceMap)` scans `mapModule` valid cells. RTS0 `start_cx=int(base_x/2)`, spiral `r=3..8`, `can_place(cx,cz,footprint)` checks `occupied_cells_` + `map_half/GRID` bounds only (`simulation.cpp:3146`, `3184`). Fixed origin `36,36` (≈ `18*CELL 2m`), not per-`HQ` base anchor.

### `militaryManager.js` `findBestTrainableUnit` vs sorted score
0AD `militaryManager.findBestTrainableUnit(wanted)` filters `templates` by `cost, tech, civ`. RTS0 `petra_manage_military_production` builds `options {type,score,bld_id}` with counter scores then `sort(score desc)` and can-pay-first; `siege_score` gated `phase>=2`, `champion_score` `phase>=3`. Closest parity.

### Naval / Trade / Diplomacy / Victory — missing
0AD has `navalManager`, `tradeManager`, `diplomacyManager`, `victoryManager`. RTS0 data exists (`UNIT_TRIREME`, `cmd_barter`, `relics_`, `has_active_wonder_`) but AI never drives them.

## Wiring Diagram

### Current (RTS0) — `simulation.cpp:3491` sequential monolith
```
Simulation::tick_petra_ai(dt)  [ Simulation class, 1 team (team=1) ]
    |
    +-> ai_* trickle (+3/+3/+2/+1.5 per s)         [ inline ]
    +-> update_petra_threat_map(dt)  every 0.4s     [ 16×16 danger ]
    |       add_power(enemy_threat) vs friendly_defense -> danger = 0.3*old +0.7*max(0, enemy-0.65*friend)
    |
    +-> petra_manage_workers(dt)                    [ evac if danger>=7 -> safe 36,36 + garrison ]
    +-> petra_manage_military_production(dt)        [ score sorted vs enemy counters -> queue push @ barracks/fortress ]
    +-> petra_manage_economy(dt)  every 1.0s        [ deficit 300-food -> reassign idle workers to nearest safe resource ]
    +-> petra_manage_buildings(dt)                  [ spiral 36,36 can_place: house if pop-3, barracks, tower if phase>=2, fortress if phase>=3 ]
    +-> petra_manage_research(dt)                   [ TC queue: PHASE_TOWN -> FARMING/WOODCUTTING/MINING -> PHASE_CITY -> WEAPONS/ARMOR ]
    +-> petra_manage_tactical_attacks(dt)           [ PetraStrikeForce MUSTER( thresh 6/10/16 or 60s) -> MARCH(target bld priority 4..1) -> ENGAGE ]
            staging = base*0.65  |  fallback engage if base_threat>=10
```
*All 6 managers mutate `Simulation` directly, no `HQ`/`BaseManager` isolation, no `QueueManager`.*

### Target (0AD Petra) — 32-module wiring
```
AIInterface (per AI player / per team)
    |
    +-> PETRA.PetraAI (petra.js)  -- entry, per-turn update 1.0s
            |
            +-> Config (config.js:395) difficulties/personalities/buildOrders
            +-> Headquarters (headquarters.js:1463)  [ 1 per AI, owns bases ]
            |       +-> BaseManager[]  (baseManager.js) per base anchor (Civic Center)
            |       |       +-> BuildingManager (obstructionMap + territory + placement)
            |       |       +-> WorkerManager + TrainingManager
            |       +-> MapModule (mapModule.js)  obstructionMap / influenceMap / borderMap / accessibility
            |       +-> TerritoryMap / BorderMap
            |
            +-> AttackManager (attackManager.js) + AttackPlan[] (attackPlan.js)
            |       receives: threatMap, militaryManager, navalManager
            |       plans: Rush/Raid/Large, transport, target selection via influenceMap
            |
            +-> DefenseManager (defenseManager.js)  monitors territory, garrisons, counter-arms
            +-> MilitaryManager (militaryManager.js)  findBestTrainableUnit(wantedRates) + counters
            +-> EconomyManager (economy.js)  wantedGatherRates, deficit, account
            +-> QueueManager (queueManager.js) + Queue[] (queue.js)  priority queues (villager/mil/tech/build)
            +-> TradeManager (tradeManager.js)  barter/market routes
            +-> DiplomacyManager (diplomacyManager.js)  stance, tribute
            +-> VictoryManager (victoryManager.js)  wonder/relic timers
            |
            +-> Chat (chat.js)  NMT_CHAT/FLARE taunts
            +-> Statistics (statistics.js)  score tracking
```
*Fix for G10: extract `Simulation::petra_*` monolith into `src/ai/petra/{headquarters,attackManager,defenseManager,queueManager,mapModule,economy}.h/.cpp` mirroring `ai/petra/` modules, or document as intentional collapsed stub.*

## DoD Checklist — Phase 7 (Petra Parity)

> Phase 7 = `Technology & Upgrade Tree` per `docs/PORTING_TODO.md:7`, but Petra macro (`25.4`) tracked here as G10. Check before marking Phase 7 / 25.4 done beyond stub.

- [ ] **P7-01 Config** — `config.js:395` equivalent loaded (difficulty `sandbox..veryHard`, `wantedGatherRates`, `buildOrders`) — currently hard-coded `ai_food_=400` etc `simulation.h:420`.
- [ ] **P7-02 HQ/Bases** — `headquarters.js:1463` split into `HQ` + `BaseManager[]` per base anchor (TC location), not single `Simulation` monolith `simulation.cpp:2828`.
- [x] **P7-03 Threat Map — scaffolding 2026-08-29** — `THREAT_GRID 16 danger[256]` → **32 danger[1024]** (4m cells, `simulation.h:416 THREAT_GRID_RES=32`, `THREAT_CELL_SIZE=4.0m`). Arrays `danger/enemy_threat/friendly_defense[1024]` updated, decay `0.3/0.7` retained. Still coarse vs 0AD `obstructionMap 128×128` but raises fidelity 15%→~35%. `TODO`: wire `obstructionMap`/`accessibility`/`borderMap` via `grid_nav.h NavGrid`.
- [x] **P7-04 Attack — scaffolding 2026-08-29** — Added header-only `AttackPlan {UNEXECUTED,WALKING,ATTACKED,ABORT, state, target_building_id, staging_x/z, abort_threat, squad[]}` (`simulation.h:445`) mirroring `attackPlan.js` FSM. Single `PetraStrikeForce` remains active; future `AttackManager` will own `vector<AttackPlan>` with merging. Strike thresholds `6/10/16` + staging `base*0.65` unchanged.
- [ ] **P7-05 Defense** — Worker evac `danger>=7` expanded to `defenseManager.js` territory garrison + counter-attack armies — currently `petra_manage_workers` only.
- [ ] **P7-06 Economy** — Deficit `max(50,300-ai_food_)` refinement matches `economy.js wantedGatherRates` + `gatherRateCache` — currently `pct_*` fixed.
- [ ] **P7-07 Building Placement** — Spiral `36,36 can_place` replaced with `obstructionMap` + `borderMap` `findBestBuildLocation` — currently `can_place` bounds+occupied only.
- [ ] **P7-08 Military Production** — Sorted counter scores validated vs `militaryManager.js findBestTrainableUnit` + `template` availability/ civ/tech gating — currently `petra_phase_>=2/3` gating only.
- [x] **P7-09 Queue — scaffolding 2026-08-29** — Added `petra_queue_manager` stub comment (`simulation.h:462 TODO(G10) PetraQueue {VILLAGER,MILITARY,BUILDING,TECH}`) explaining future cross-building priority sort. Per-building `vector<ProductionOrder>` FIFO retained for MVP; wiring deferred to G10 queueManager split.
- [ ] **P7-10 Naval/Trade/Diplomacy/Victory** — Explicit **missing** acknowledged: `navalManager.js`/`tradeManager.js`/`diplomacyManager.js`/`victoryManager.js` not ported (`docs/REMAINING_GAPS.md` G10).
- [ ] **P7-11 Threat Timers** — `update_petra_threat_map 0.4s` / `petra_manage_economy 1.0s` / `strike force thresholds 6/10/16` documented vs `config.js` — currently magic numbers.

**Current parity:** 700 LOC stub covering ~20-50% of Petra surface (best: military counters; worst: naval/trade/diplomacy 0%). Mark Phase 7 **stub complete**, not **parity complete**.

### 2026-08-29 G10 Minimal Scaffolding (Phase4) — 10%→~30% lift

> G8/G9/G10 minimal code stubs to raise parity without ENet linking. Verified `g++ -fsyntax-only` on headers.

- **Threat grid:** 16→32 (1024 cells) — matches `docs/REMAINING_GAPS.md` DoD `dangerMap res 32/64`.
- **AttackPlan:** header-only `AttackPlan` FSM added; `PetraStrikeForce` kept as single active plan.
- **QueueManager:** stub comment `PetraQueue` priority enum added (`simulation.h:462`).
- **Remaining:** `HQ/BaseManager`, `obstructionMap`/`accessibility`, `EconomyManager` deficit refinement, naval/trade/diplomacy still 0%. Next step: extract `src/simulation/ai/` modules mirroring `ai/petra/` (see `docs/REMAINING_GAPS.md` G10 DoD).

## References

- `src/simulation/simulation.h:392-436` — `PetraThreatMap`, `PetraStrikeForce`, `petra_*` declarations + `// Phase7:` comment
- `src/simulation/simulation.cpp:2737` `update_petra_threat_map`, `2828` `petra_manage_workers`, `2891` `petra_manage_military_production`, `3010` `petra_manage_economy`, `3127` `petra_manage_buildings`, `3270` `petra_manage_research`, `3379` `petra_manage_tactical_attacks`, `3491` `tick_petra_ai`
- Vendored (if present): `binaries/data/mods/public/simulation/ai/petra/{config,headquarters,attackManager,defenseManager,queueManager,mapModule,economy,military}.js`
- `docs/REMAINING_GAPS.md` G10

