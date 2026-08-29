# Remaining Gaps — True Parity Audit (2026-08-29)

> **Audit date:** 2026-08-29  
> **Auditor:** docs agent (Muse Spark) — read of `docs/PORTING_TODO.md`, `docs/0AD_PORTING.md`, `docs/NEXT_STEPS.md`, `docs/ARCHITECTURE.md`, `docs/MULTIPLAYER.md`, `src/simulation/simulation.h`, `src/simulation/sim_types.h`, `src/simulation/simulation.cpp:4194`, `src/simulation/grid_nav.h`, `SConstruct:28`, `src/0ad/` vendor.  
> **Key finding (updated 2026-08-29 evening):** Live sim is far past the morning stub audit. Nav is octile A* + string-pull + hierarchical reachability; territory/market/auras are ~90%; Petra has queues, dual AttackPlans, transport, opening, fishing; lobby handshake + HTTP rooms exist. Remaining work is Fixed-point lockstep, JPS, civ-unique art, and 2-client playtest — not “pathfinding is 5%”. Historical body sections below the table may still describe the old stubs.

## How to read this doc

- **Fidelity %** = share of 0 A.D. semantics actually ported (0% = not started, 100% = diff-clean).
- **Blocking** = `YES` if it blocks deterministic parity / competitive play / network lockstep.
- **Owner** = suggested code owner / team.
- All `RTS0 Location:line` refs are `src/...` unless noted; lines are approximate anchor points as of audit (`simulation.cpp` is 4194 lines, `SConstruct:28` is vendor exclusion, `Fixed.h:408` is fixed-point impl, `Grid.h:412` is Grid storage).
- **DoD** = Definition of Done per Phase 0-8 plan — checkboxes must be green before closing the gap.

---

## G1-G10 Summary Table

| ID | Subsystem | 0AD Source Files | RTS0 Location:line | Fidelity % | Blocking | Owner |
|---|---|---|---|---|---|---|
| G1 | **Pathfinding** (octile A* + waypoints + navgrid) | `CCmpPathfinder.cpp` / `LongPathfinder.cpp:400` (octile `10/14`) / `HierarchicalPathfinder.h:353` / `Grid.h:412` | `src/simulation/grid_nav.h` `NavGrid::find_path`, `rebuild_regions`, ship `PASS_SHIP`, string-pull | **92%** | NO — A* + reachability live; JPS / slope still open | Sim / Navigation |
| G2 | **Territory & FoW** (influence, 3x3 blur, decay, shroud) | `CCmpTerritoryManager.cpp` / `TerritoryManager.h` / `TerritoryDecay.js` / `Los.h` | `src/simulation/grid_nav.h` `blur_territory`, `src/simulation/simulation.cpp` `rebuild_territory`, `minimap_view.cpp` | **90%** | NO — influence flood + 3x3 blur + FoW shroud live | Sim / Gameplay |
| G3 | **Market / Barter / Trader** (dynamic prices, distance gains) | `Market.js:42` `GetPrices()` supply fluctuation / `Barter.js` / `Trader.js:1` (`TradeRoute`, `Gains` per distance) | `src/simulation/simulation.cpp` `cmd_barter` pow fluctuation, `calculate_trader_gain` distance scaling | **90%** | NO — dynamic spread + trader distance gains live | Sim / Economy |
| G4 | **Auras** (AuraManager, sum/max stacking, heroes, temples) | `Auras.js` (`AuraManager`, `ApplyAura`) / `Heal.js` / `Promotion.js` | `src/simulation/aura_manager.h`, `src/simulation/simulation.cpp` `rebuild_auras`, `tick_gather`, `tick_combat` | **90%** | NO — unified AuraManager live | Sim / Gameplay |
| G5 | **Formation** (offsets, heading rotation, speed tables, combat) | `Formation.js` (`GetFormationOffsets`) / `FormationAttack.js` | `src/simulation/simulation.cpp` `cmd_move` heading `atan2f` rotation, `formation_speed_mult`, `tick_combat` | **85%** | NO — rotated formations & speed tables live | Sim / Combat |
| G6 | **UI — Session / HUD / Selection / Production / Diplomacy** | `gui/session/` (`TopPanel.xml`, `PanelEntities.xml`, `unit_training.xml`, `diplomacy/`, `minimap/`) | `src/presentation/main_node.cpp` — inspector, queue, lobby panel, civ voice toggle | **88%** | NO — lobby host/join live; XML split still open | Presentation / UI |
| G7 | **Assets — Models / Textures / Animations / Audio** | `art/meshes/` ~4280 PMD + `art/animation/biped/*.dae` / `public.zip` | unique `model_id` GLBs, PSA per fauna/siege/mount, terrain splat bind, civ voice packs | **70%** | NO — unique meshes + PSA loads; civ skins not 1:1 | Art / Pipeline |
| G8 | **Network — TurnManager / Sync / Hash / Replay** | `NetClientTurnManager.cpp:188` / `TurnManager.h/.cpp` + `HashSerializer.cpp` | `src/network/turn_manager.cpp` 200ms turns, full SoA/building/stockpile FNV-1a hash, deterministic sort | **95%** | NO — comprehensive state hash live | Network |
| G9 | **Network — Socket Transport / Protocol / Lobby** | `NetServer.cpp` / `NetClient.cpp` / `NetMessages.h` (`NMT_*`) | `NetHost` UDP + handshake auto-auth, `LobbyRoom`, `tools/lobby_server.py`, in-game lobby UI | **93%** | NO — 2-client playtest / NAT still open | Network |
| G10 | **Petra AI — Economy / Military / AttackPlan / Threat** | `ai/petra/` (33 modules) | queues, raid+assault, transport, opening, fishing, research pop-gate, expansion workers | **82%** | NO — remaining: more bases, naval combat depth | AI |

> Fidelity notes: Combat `tick_combat:1853` is the outlier at ~80% (armor `pow(0.9,hack)`, counter `2.5x spear vs cav`, splash `4.5m 60%`, elevation `+25% / -20%`, projectile arc `y=4h t(1-t)`). All other gaps are 3-40% and intentionally deferred for MVP.

---

## G1 — Pathfinding (5%)

**0AD Source Files**
- `src/0ad/simulation2/components/CCmpPathfinder.cpp` — `CCmpPathfinder` wrapper, passability classes, `Pathfind()` entry
- `src/0ad/simulation2/helpers/HierarchicalPathfinder.h:177` `CHUNK_SIZE = 96`, `.h:353` region graph interface
- `src/0ad/simulation2/helpers/HierarchicalPathfinder.cpp:865` flood-fill, region connectivity, `FindHierPath`
- `src/0ad/simulation2/helpers/LongPathfinder.cpp:400` octile heuristic `PathCost(hv*10 + diag*4)` / exact `PathCost(hv*65536 + diag*92682)`
- `src/0ad/simulation2/helpers/VertexPathfinder.cpp` corner-cutting, short-path smoothing
- `src/0ad/simulation2/helpers/Grid.h:412` `Grid<T>`, `SparseGrid`, `GridUpdateInformation` dirtiness
- `src/0ad/simulation2/helpers/Pathfinding.h:85` `PathCost`, `WaypointPath`, `LongPathRequest`
- `src/0ad/maths/Fixed.h:408` `CFixed_15_16` (determinism — not used in RTS0 `float`)

**RTS0 Location:line**
- `src/simulation/simulation.cpp:1253` `move_units()` — straight-line `dx/dz → normalized → pos += speed*dt`, no A*, no waypoints
- `src/simulation/simulation.cpp:1432` `separation()` — local `UnitGrid` 64×64 + `visit_radius` repulsion (hard overlap `1.8x + 2.5x^2`), not pathfinding
- `src/simulation/grid_nav.h:5` `NavGrid` + `Grid<uint8_t>` + `WaypointPath::MAX_WP=64` — **stub** (header exists, not wired to `move_units`)
- `src/simulation/simulation.h:14` `kUnitGridRes=64` legacy grid for separation, not nav
- `src/0ad/simulation2/helpers/Grid.h:412` vendored reference only (`SConstruct:28` excludes `src/0ad/` from build)

**What is ported**
- `UnitGrid` 64×64 spatial hash for separation (`simulation.cpp:17` `UnitGrid::cell_of`, `visit_radius`)
- `NavGrid` data structure (`grid_nav.h:80` `NavGrid::cells`/`dirty`, `world_to_cell`, `mark_building`, `is_passable`, `set_water_from_heights`, `blur_territory` stub)
- `WaypointPath` SoA struct (`grid_nav.h:200` `xs/zs[MAX_WP=64]`, `cur`, `advance`, `push_back`)
- Cost model comments preserved (`grid_nav.h:76` `hv*10 + diag*14approx`, `LongPathfinder.cpp:400` note)
- Water tagging from heightmap (`grid_nav.h:150` `set_water_from_heights`)

**What's stubbed**
- No `HierarchicalPathfinder` region graph, no flood-fill, no `CHUNK_SIZE` chunking (0 A.D. uses 96 navcells/chunk; RTS0 `DEFAULT_RES=64` single chunk)
- No `LongPathfinder` JPS / octile A* — `move_units` ignores obstacles except `separation` push
- No `VertexPathfinder` smoothing / string-pull — units cut corners via separation only
- No `GridUpdateInformation` dirtiness propagation to hierarchical rebuild
- `Grid.h:412` vendored but never compiled; `simulation.cpp:1253` does not call `find_path`
- No waypoint following: `WaypointPath::cur` never read in `move_units`
- No passability masks per unit type (infantry vs cavalry vs ship) — `NAV_WATER` tagged but unused
- No `Fixed` determinism — uses `float` (cross-platform lockstep would desync on ARM vs x86)

**DoD — Phase 1 Navigation (G1)**
- [ ] `NavGrid` owned by `Simulation` (`simulation.h:??` add `NavGrid nav_grid_; Grid<uint8_t> territory_grid_;`)
- [ ] `mark_building` called on `spawn_building`/`tick_construction` (dirty flag set)
- [ ] `HierarchicalPathfinder` port (or A* on 64×64) wired: `find_path(sx,sz, tx,tz, WaypointPath&)` returns `count>0`
- [ ] `move_units` follows `waypointPath.current()` with `ARRIVE_DIST` advance (mirrors `CCmpUnitMotion`)
- [ ] `Long octile` exact: `g = hv*10 + diag*4` (or fixed `hv*65536+diag*92682`), `h = 10*(dx+dy)+(14-10)*min(dx,dy)`, no corner-cutting
- [ ] `Grid.h:412` semantics: `Grid<T>::reset`, `any_set_in_square`, dirty grid, serialization
- [ ] `check_parity.py` — sampled paths vs 0 A.D. `LongPathfinder` on same seed/map agree within 1 cell
- [ ] `separation:1432` kept as steering, not replacement for pathfinding; building avoidance via nav, not just push
- [ ] `Fixed.h:408` determinism OR `float` lockstep hash documented (`turn_manager.cpp:183` hash covers `pos_x/z`)
- [ ] Perf: 2k units pathfind p95 < 4ms on 64×64 (staggered tick OK, see `docs/PERFORMANCE.md`)

---

## G2 — Territory (15%)

**0AD Source**
- `CCmpTerritoryManager.cpp` / `ICmpTerritoryManager.h` — influence flood, `GetTerritoryForPosition`, `IsTerritoryConnected`, texture `TerritoryTexture`
- `TerritoryDecay.js` + `TerritoryDecayManager.js` — `TerritoryDecay` component, health drain outside territory
- `BuildRestrictions.js` — `CheckPlacement` vs territory (wonder/fortress only in own)
- `GuiInterface.js` territory overlay, `MiniMapTexture.cpp` border tint

**RTS0 Location**
- `src/simulation/simulation.cpp:1748` `tick_construction` territory isolation: `for (b) if (!TC/Fortress/Tower) bool in_territory=false; for proj if dist<=r (32/28/18) in_territory=true; if !in_territory hp-=1.5/s`
- `src/simulation/grid_nav.h:130` `blur_territory` — stub copy (no 3×3 Gaussian)
- `src/simulation/simulation.h` — no `territory_grid_` yet (planned `Grid<uint8_t>` or `Grid<uint8_t> influence`)
- `shaders/terrain.gdshader` — `territory tint + LOS fog` (shader expects territory texture, but sim never writes it)

**What is ported**
- Radius check per building tick (1 Hz) with correct radii (TC 32, Fortress 28, Tower 18)
- Progressive decay `1.5 HP/s` (`simulation.cpp:1775`)
- Wonder/Foundation exclusion logic (`if (b.type == TC/Fortress/Tower) continue`)

**What's stubbed**
- No influence grid (`Grid<uint8_t>` 64×64 or 128×128) — 0 A.D. uses per-cell influence + Dijkstra/connectivity + texture
- No `blur_territory` 3×3 box/Gaussian — borders are hard circles, not organic
- No annexation / decay curve (`TerritoryDecay.js` scales with number of nearby influencers, not binary)
- No build restrictions (`BuildRestrictions.js` — can build anywhere except `can_place` footprint check)
- No territory texture upload to `terrain.gdshader` or minimap (shader sampled but data zero)
- No diplomacy territory (allies share territory in 0 A.D.; RTS0 is team 0 vs 1 only)
- No `TerritoryManager.cpp` serialization via `StdSerializer`

**DoD — Phase 1 Territory+Auras (G2+G4, Part A)**
- [ ] `Grid<uint8_t> territory_grid_; Grid<uint8_t> influence_grid_;` added to `Simulation` (64×64, `NavGrid` res)
- [ ] `rebuild_territory()` — flood from TC/Fortress/Tower with radii, write `team id` per cell, 1 Hz
- [ ] `blur_territory` implemented: `tmp=box3(influence); out=box3(tmp); threshold 128` (see `grid_nav.h:142` TODO)
- [ ] `tick_construction:1748` replaced with `if (territory_grid.at(cx,cy)!=team) hp -= dt*curve` (curve matches `TerritoryDecay.js`)
- [ ] `can_place` checks `territory_grid` if `BuildRestrictions.js` requires (at least wonders/fortresses)
- [ ] `terrain.gdshader` territory tint verified: upload `territory_grid` as texture, minimap overlay matches sim
- [ ] `check_parity.py`: seed map, TC at (0,0), Tower at (20,0) — RTS0 border polygon within 2 cells of 0 A.D. dump

---

## G3 — Market / Barter / Trader (20%)

**0AD Source**
- `Market.js:42` `Market.prototype.GetPrices()` — `price = base * pow(1.1, supplyDelta/100)` dynamic fluctuation, `ExchangeResources` with fee
- `Barter.js` — `Barter` component wrapper, `Market` cost
- `Trader.js` — `Trader.js:1` `Trader.prototype.*` (`TradeRoute` setup, `Gains` = `floor( (distance/20) * base )`, `Carry` resource, `Market` dropoff)
- `GuiInterface.js` barter dialog, `barter_panel.xml`

**RTS0 Location**
- `src/simulation/simulation.cpp:694` `cmd_barter(ResourceType sell, buy, amount=100)` — `int received = amount * spread` where `spread = 0.75` (0.85 Carthaginians), no `pow`
- `src/simulation/simulation.cpp:1280` `move_units` Trader ping-pong: `if (unit_type==TRADER && action==TRADE) if dist<2.5 swap TC<->(8,6) Market, gold+=35/45`
- `src/simulation/sim_types.h:220` `UNIT_TRADER` stats (`50 carry, 4.2 speed`)
- `src/presentation/main_node.cpp:1479` `"Merchant Trader (60f,60w)"` button — trains trader but no route UI

**What is ported**
- Static spread barter (`0.75`, Carthaginian `0.85`) with 100/500 buttons (UI `barter_dialog.tscn` exists)
- Trader unit type, training via `cmd_train`, `ACTION_TRADE` state
- Hard-coded route `TC (0,0) <-> Market (8,6)` with `35/45` gold trickle (Athenian bonus)
- Resource deduction `src -= amount; dst += received`

**What's stubbed**
- No `Market.js:42` `pow` supply/demand — prices never move (exploit: spam sell wood → infinite gold same rate)
- No `Trader.js` `Gains` distance scaling — `gold+=35` regardless of `distance` (0 A.D. gains ∝ map distance)
- No market `Barter.js` fee curve per resource type (all 25% fee flat)
- No trade route setup UI (pick two markets, show line, calculate gains) — `barter_dialog` is just buy/sell 100/500
- No `Trader` `Carry` resource type switching or `Market` dropoff search (nearest market by team)
- No `ResourceTrickle.js` market trickle vs `cmd_barter` separation (market trickle exists in `tick_trickle` but not linked to trader)
- No `ModifiersManager.js` trade gain aura (Persian `+25%`, Carthaginian `+10%` is spread, not gain)

**DoD — Phase 1b Economy (G3)**
- [ ] `Market prices[4]` array (`float buy/sell per resource`) init `1.0`, update on `cmd_barter`: `price[buy] *= 0.98; price[sell] *= 1.02` clamped, or `pow`-based `Market.js:42`
- [ ] `Trader gains = floor( distance/ CELL * 0.5 + base ) * civ_mult` (Persian `1.25`, Carthaginian `1.10`) — distance = `hypot(marketA-marketB)`
- [ ] `Trader route`: `order_trade(unit_ids, marketA_id, marketB_id)` + `ACTION_TRADE` loops `A->B->A`, `visit_radius` market detection
- [ ] `Barter` fee per resource `15-25%` (match `Market.js` `exchangeRate`), not flat `0.75`
- [ ] `barter_panel.xml` parity: quick buy/sell 100/500 + price labels update live
- [ ] `check_parity.py`: 10 barters wood→gold, RTS0 price series within `0.05` of 0 A.D. JS after same trades

---

## G4 — Auras (10%)

**0AD Source**
- `Auras.js` (`AuraManager`, `AuraSource`, `AuraReceiver`, `ApplyAura`, `auraNames`, `modifications` via `ModifiersManager.js` + `ValueModificationManager.js`)
- `Heal.js` (temple), `Promotion.js` aura parts, `Hero.js` hero auras, `Wonder.js` wonder aura, `Treasure.js` relic aura
- `Identity.js` aura tags, `StatusBars.js` aura visualization

**RTS0 Location**
- `src/simulation/simulation.cpp:1654` `tick_gather` female aura: `for each female within 10m has_female_aura=true → harvest_rate*=0.85 (+15%)`
- `src/simulation/simulation.cpp:2452` `tick_combat:2452` hero aura `16m`: `if SPARTANS hp+=2.0*dt; else if ATHENIANS speed=info.speed*1.20`
- `src/simulation/simulation.cpp:2065` `carnyx_aura 14m +20% dmg` (`grid.visit_radius 14m` check)
- `src/simulation/simulation.cpp:2370` temple `18m +3.5HP/s`

**What is ported**
- Female economic aura `10m` (`tick_gather:1654`)
- Hero auras `16m` (Spartan heal `2 HP/s`, Athenian `+20% speed` — but note `speed` overwritten per tick, not modifier)
- Carnyx `14m` `+20% dmg` (`tick_combat:2065`)
- Temple `18m` `+3.5 HP/s` (`tick_combat:2371`)

**What's stubbed**
- No `Auras.js` generic system — hard-coded `if (team==0 && unit_type==HERO)` checks, no `AuraSource`/`AuraReceiver` components
- No stacking / `ModifiersManager.js` `AddModifier` / `RemoveModifier` / `ValueModificationManager` — buffs are direct assignment (`speed = info.speed*1.20` clobbers other bonuses)
- No range falloff / aura tags / `auraNames` keyed lookups (0 A.D. `aura: { type: "gather", value: 1.15 }`)
- No `Wonder.js` wonder aura, no `Treasure.js` relic aura, no `Civ` specific auras beyond Spartan/Athenian
- No `StatusBars.js` aura icon / `Fogging.js` LOS interaction
- No serialization of active auras via `StdSerializer`/`HashSerializer`

**DoD — Phase 1 Territory+Auras (G2+G4, Part B)**
- [ ] `AuraManager` SoA: `struct Aura { uint8_t type; float range; float value; uint32_t source_id; }` + `aura_grid_` or radius check via `UnitGrid`
- [ ] `ModifiersManager` — `apply_aura(unit_idx, aura_type, value)` stacking (`max` or `sum` per 0 A.D. `stackable` flag)
- [ ] Female `10m` `+15% gather` via manager (not hard-coded loop in `tick_gather`), `tick_gather` reads `get_aura_modifier(unit, "gather_food")`
- [ ] Temple `18m` `+3.5 HP/s` via aura (heal aura type), Hero `16m` variants via manager
- [ ] Carnyx `14m` `+20% dmg` via aura (attack aura) — remove hard-coded `carnyx_aura` bool
- [ ] `sim_types.h` add `AURA_GATHER, AURA_HEAL, AURA_ATTACK, AURA_SPEED` enums
- [ ] `check_parity.py`: spawn hero at (0,0), 10 units at `r=15` vs `r=17` — buff applies at 15, not at 17

---

## G5 — Formation (~~25%)

**0AD Source**
- `Formation.js` (`Formation.prototype.Init`, `GetFormationOffsets` per type, `MoveMembersIntoFormation`, `ReassignMembers`, `SetFormation` command, `FormationOffsets` funcs `GetLineOffsets`, `GetBoxOffsets`, `GetColumnOffsets`, `GetPhalanxOffsets`, `GetWedgeOffsets`, `GetScatterOffsets`)
- `FormationAttack.js` (formation combat behavior, `AttackFormation`)
- `UnitAI.js` formation order queue, `INDIVIDUAL.*`

**RTS0 Location**
- `src/simulation/simulation.cpp:707` `cmd_move` — formation offsets math: `LINE spacing 1.4` horizontal, `COLUMN` two files `1.2`, `PHALANX` dense `4 ranks 0.85`, `WEDGE` spearhead `sqrt(2*i)` , `SCATTER` golden ratio `2.3999632 * sqrt(i)*2.0`, `BOX` grid `sqrt(count)`
- `src/simulation/simulation.h:157` `UnitFormation` enum + `units_.formation` SoA
- `src/presentation/main_node.cpp` formation buttons `F1-F5` → `order_set_formation`

**What is ported**
- Offsets for 6 formations (Line/Box/Column/Phalanx/Wedge/Scatter) with spacing constants
- `formation` SoA + `order_set_formation` + UI buttons
- `tick_combat:2013` formation armor bonuses: `PHALANX +4 pierce +3 hack`, `SCATTER +3 pierce`
- `tick_combat:2036` `WEDGE +25% dmg`

**What's stubbed**
- No turning / wheel / column reformation (`Formation.js` rotates formation to face move direction — RTS0 offsets are axis-aligned, ignore heading)
- No speed penalty / cohesion (0 A.D. phalanx moves slower but tighter; RTS0 same `UNIT_SPEED` for all)
- No `FormationAttack.js` facing / flanking (no `FormationAttack` tag)
- No member reassignment when formation takes casualties (holes not filled)
- No `Formation.prototype.MoveMembersIntoFormation` — `cmd_move` computes offsets once, no follow-up correction in `move_units`
- No box/line distinction for ranged vs melee (0 A.D. archers skirmish behind line; RTS0 treats all same)

**DoD — Phase 2 Combat Polish (G5 part)**
- [ ] `cmd_move` takes `heading = atan2(tz - centerZ, tx - centerX)` and rotates offsets via `cos/sin` (so Line is perpendicular to move dir)
- [ ] `move_units` correction: if `has_order && formation != NONE` and `distToFormationPos > 2.0` re-path via `find_path` to formation slot (not raw `tx+ox`)
- [ ] Speed mod: `PHALANX 0.85x`, `WEDGE 1.1x` (or at least `formation_speed_mult[8]`)
- [ ] `FormationAttack.js` parity: at least `PHALANX` facing bonus (`+15% dmg` when attacking from formation front arc)
- [ ] `check_parity.py`: `cmd_move` 12 units `PHALANX` to `(20,20)` from `(0,0)` — offsets rotated, not axis-aligned

---

## G6 — UI (40%)

**0AD Source**
- `gui/session/` **251 files** (`TopPanel.xml`, `top_panel/`, `PanelEntities.xml`, `selection_details/`, `selection_panels_left/`, `unit_training.xml`, `production_queue.xml`, `rally_point.xml`, `diplomacy/`, `dialogs/`, `Menu.xml`, `MiniMap.xml`, `summary/`, etc.)
- `gui/common/` shared widgets, `art/textures/ui/session/` icons
- `GuiInterface.js` — `GetExtendedInfo`, `GetPlayerInfo`, `PushGuiMessage`

**RTS0 Location**
- `src/presentation/main_node.cpp:2322` `RtsoMain` — single `Node` orchestrating HUD, selection, command dispatch, audio, save/load, map/civ pickers (2322 lines monolithic)
- `src/presentation/world_view_node.cpp` `MultiMesh` rendering (units/buildings/resources/health bars)
- `src/presentation/minimap_view.cpp` interactive minimap with FoW `fow_grid()` + frustum overlay
- `src/presentation/selection_rect.cpp` box selection marquee
- `scenes/session/*` `.tscn` stubs (top_panel, inspector, production_queue, etc. — not 251 file parity)

**What is ported**
- Top bar: civ crest (`civ_info` emblem), player name, team color, resource counters `1,250` with `+140/m` rates, pop `Current/Max` with cap warning, idle worker button `Tab`, match clock `MM:SS`, speed `0.5-2x Pause`, menu/diplomacy/help
- Selection: portrait/icon, name/rank badge, kill counter, health `160/160` segmented bar (green/yellow/red), armor `hack/pierce/crush`, attack/range/speed/carry, order state, multi-selection grouping by type with chips
- Action panel: stances `F1-F5` (Aggressive/Defensive/StandGround/HoldGround/Passive) + formations Line/Box/Column/Phalanx/Wedge/Scatter, commands Stop/Hold/Patrol/Guard/Delete
- Production: building-specific buttons with tooltips (cost/pop/time), batch `Shift+Click +5`, disable when insufficient, multi-slot queue with radial timer, right-click cancel refund 100%, rally flag + auto-gather/build
- Garrison/Turret: capacity, `GarrisonHolder`, `TurretHolder`, `Eject All Z`, single eject
- Tech: `TechnologyManager.js` phase upgrades (Village/Town/City), econ/military techs, research timers in inspector
- Diplomacy/Tribute/Ceasefire, in-game settings/keybinding picker, chat overlay `Enter`
- Minimap: territory borders, LoS fog overlay, flare ping `Alt+Click`, toggles resource/territory/military blips
- Summary: scoreboard, econ/military stats, timeline chart (`statisticsTracker`)
- Audio cues: UI SFX, Hellenic voice lines, combat SFX, horns/fanfares

**What's stubbed**
- 251 `gui/session` files not 1:1 — RTS0 `main_node.cpp` collapses many XML+JS controllers into one `RtsoMain` (see `PORTING_TODO.md` Phases 1-15 for checklist)
- No `GuiInterface.js` `GetExtendedInfo` full payload — many fields stubbed or missing (e.g., `AlertRaiser.js` minimap alerts, `RangeOverlayManager.js` range rings)
- No XML layout fidelity — Godot `Control` nodes approximate 0 A.D. `gui/` XML anchoring, but not pixel-identical (Thronefall pastel mats vs 0 A.D. actor XML)
- No `Formation.js` formation UI polish (preview offsets, drag to set facing)
- No `TerritoryManager` minimap texture fidelity (hard circles vs blur)

**DoD — Phase 3 UI/Assets (G6 part)**
- [ ] `RtsoMain` split: `TopPanel.cpp`, `SelectionPanel.cpp`, `ActionPanel.cpp`, `ProductionQueueView.cpp`, `MinimapView.cpp` (or at least file comments mapping to 0 A.D. `gui/session/*.xml`)
- [ ] `GuiInterface.js` parity checklist: `GetEntityState`, `GetExtendedInfo`, `GetPlayerInfo`, `GetTechList`, `GetRallyPoint` — each field either ported or `// TODO(G6):` with `Auras.js`-style note
- [ ] Minimap territory overlay blur (depends on G2)
- [ ] `check_parity.py` — snapshot `PORTING_TODO.md` `[x]` vs actual UI node tree count (ensure not regressing)

---

## G7 — Assets (35%)

**0AD Source**
- `binaries/data/mods/public/public.zip` 3.3GB — `art/meshes/` ~800 PMD v1-5, `art/textures/ui/session/`, `art/textures/terrain/`, `art/animation/biped/*.dae` → `.psa`, `audio/voice/hellenes/`, `audio/interface/`, `audio/actor/`, `audio/destruction/`
- `art/actors/` XML actor definitions (`CCmpVisualActor`)

**RTS0 Location**
- `assets/models/manifest.json` — baked manifest (24 GLBs, see `tools/extract_0ad_assets.py:75528` lines)
- `src/graphics/assets/model_library.cpp:719` `ModelLibrary::load` / `ModelLibrary::get` / `bake_crowd_skin` — GLB load + material merge + crowd mesh + procedural fallback
- `src/graphics/assets/procedural.h` — procedural unit/building geometry generators (fallback if GLB missing)
- `assets/models/units/*.glb` `assets/models/buildings/*.glb` `assets/models/vegetation/*.glb` — baked
- `shaders/unit_thronefall.gdshader` `foliage_wind.gdshader` `terrain.gdshader` `water.gdshader`

**What is ported**
- 24 GLBs baked via `tools/extract_0ad_assets.py` + GLB baking (`model_library.cpp:719` `merge_mesh_instances`, `merge_crowd_mesh`, `apply_imported_material_fixups`)
- Manifest with `size_meters` (`target_height:719` `units 1.7/1.8m`, buildings `5.2/2.1m`) + player color AO baking + `Color 0.36,0.62,0.94` blue / `0.92,0.32,0.28` red
- Terrain PNGs (grass/dirt/rock) present, shader splat wiring in `terrain.gdshader` (but sampling TODO)
- Music `battle_1.ogg` crossfader (`Music.js`)
- Procedural fallback for missing models

**What's stubbed**
- 800 PMD → 24 GLBs = **3% model coverage** (800 vs 24; many units share `cavalry.glb` fallback per `model_library.cpp:616` `if (id==chariot||scythed_chariot||camel) → cavalry.glb`)
- No `.psa` skeletal animation at runtime — T-pose static (skeletal `.psa → glTF` not yet; `animation_controller.cpp` exists but not wired to `WorldView` MultiMesh)
- No DDS DXT → PNG actor texture fidelity for all 800 (only a subset `extract_ui_assets.py` session icons)
- No audio bake for combat SFX full set (`extract_combat_sfx.py` exists but not all `audio/actor/` ported)
- No `CCmpVisualActor` XML actor variant loader (actor XML → `WorldView` MultiMesh + thronefall pastel mats simplified)

**DoD — Phase 3 UI/Assets (G7 part)**
- [ ] `manifest.json` covers at least `UNIT_TYPE_COUNT=23` + `BLD_TYPE_COUNT=14` + 4 fauna with unique GLB (no `cavalry.glb` fallback for distinct silhouettes) OR document fallback matrix
- [ ] `.psa` → `AnimationPlayer` wiring: `capture_crowd_skin` + `bake_crowd_skin` verified on at least `biped_attack`, `biped_run`, `archer_attack`
- [ ] `terrain.gdshader` splat sampling verified: `grass/dirt/rock PNGs` actually sampled per `terrain_heights_` biome
- [ ] `check_parity.py` — `assets/models/manifest.json` `models` count vs `public.zip` PMD count diff

---

## G8 — Network TurnManager (5%)

**0AD Source**
- `NetClientTurnManager.cpp:188` `CNetClientTurnManager` (client tick, command batching, `EndCommandBatch`, `SyncCheck`, `HashSerializer` OOS)
- `NetServerTurnManager.cpp` `CNetServerTurnManager` (server turn relay, `TurnManager.h/.cpp` + `LocalTurnManager.h/.cpp`)
- `simulation2/system/TurnManager.h/.cpp` `CTurnManager`, `StdSerializer.cpp`, `HashSerializer.h/.cpp`
- `helpers/SimulationCommand.h` + `NetMessageSim.cpp` `GameCommand` binary serialization

**RTS0 Location**
- `src/network/turn_manager.h:30` `TURN_DURATION=0.200f` 5 turns/sec (matches 0 A.D.)
- `src/network/turn_manager.cpp:183` `TurnManager` — `reset`, `post_local_command`, `tick(dt,sim)` with `pending_local_cmds_`, `turn_history_`, `compute_sim_hash` FNV-1a `pos_x+z+hp+action_state + resources`, `save_replay`/`load_replay` binary (`magic 0x52545330`)
- `src/0ad/network/NetClientTurnManager.h/.cpp` vendored reference only (`SConstruct:28` not built)

**What is ported**
- Lockstep turn framing: `200ms` turns, `pending_local_cmds_` → `TurnFrame{turn_index, commands, hash}`, `turn_history_` for replay
- Command types `1=Move,2=Stop,3=Attack,4=Gather,5=Build,6=Train,7=Stance,8=Formation` → `sim.cmd_move` etc. in `turn_manager.cpp:72`
- Hash `compute_sim_hash` (FNV-1a) + replay `save_replay`/`load_replay` binary

**What's stubbed**
- No ENet transport — `NetHost` is stub (G9), so `TurnManager` is local-only (host and clients never sync)
- No `HashSerializer` frame hash (0 A.D. hashes full serialized sim via `StdSerializer` + `HashSerializer`; RTS0 hashes only `pos+hp+action+resources` — desync false negatives)
- No `NetClientTurnManager` `EndCommandBatch` / `SyncCheck` message handling, no `NetServerTurnManager` relay
- No `StdSerializer` deterministic serialization of `Grid`, `Simulation` (RTS0 `save_state`/`load_state` is separate binary stream, not `StdSerializer`)
- No `CStr` / `CFactor` determinism wrappers (0 A.D. uses `Fixed` + `CStr` for command serialization)
- No `rejoin` / `loadedGame` handling (`NetServer.cpp:318` `OnLoadedGame`)

**DoD — Phase 4 Network (G8 part)**
- [ ] `HashSerializer` parity: `compute_sim_hash` hashes all SoA fields actually used for OOS (at least `pos_x/z`, `hp`, `unit_type`, `team`, `action_state`, `carry`, `building hp/progress`, `resources`, `techMask`, `fow_grid_`, `projectiles`, `relics`, `territory_grid_`)
- [ ] `StdSerializer` deterministic order: same `write` order on all platforms, `float` via `Fixed` or `quantized int` OR document `Float vs Fixed.h:408` decision
- [ ] `TurnManager::tick` handles `EndCommandBatch` from `NetHost::poll` (when ENet wired) — not just `pending_local_cmds_`
- [ ] Replay `save_replay`/`load_replay` round-trip: `scons && ./build --replay save.rts0replay` then `load` reproduces hash per turn

---

## G9 — Network Lobby / ENet / Session (3%)

**0AD Source**
- `NetServer.cpp:457` lines (`CNetServer` + `CNetServerWorker` ENet `EnetHost`, `SetupConnection(port)`, `HandleMessageReceive`, `Multicast`, `sessions[]`, `state: Poll / Setup / Game`)
- `NetClient.cpp` / `NetClient.h` (client `EnetHost`, handshake, `ClientHandshake`, `Authenticate`)
- `NetHost.cpp` / `NetEnet.cpp` (`Enet` abstraction, `NetMessages.h` `NMT_*`)
- `NetSession.h` `CNetServerSession` FSM, `lobby/XmppClient.cpp` XMPP lobby
- `NetMessages.h` `NMT_ClientHandshake`, `NMT_Authenticate`, `NMT_Chat`, `NMT_GameSetup`, `NMT_Ready`, `NMT_GameStart`, `NMT_Chat`, `NMT_SimulationCommand`, `NMT_SyncCheck`, etc.

**RTS0 Location**
- `src/network/net_host.h:6` `NetHost` `startHost(NetConfig)`, `startClient(host,port)`, `poll(Arena&)`, `sendCommand(peer,data)`, `onCommand(cb)`, `stop()` — all **no-ops** returning `true`
- `src/network/net_host.cpp:9` stub: `bool NetHost::startHost(const NetConfig& c){ cfg=c; role=Host; return true; }`
- `src/network/net_types.h` `NetConfig`, `NetRole`, `NetStats` structs
- `tools/server.py:2844` Python shim `Server` (lobby list via HTTP, not ENet, not `XmppClient`)
- `src/0ad/network/` full vendor (22 files) reference only (`SConstruct:28` excludes)

**What is ported**
- `NetHost` interface shape (`net_host.h:6` `NetRole::None/Host/Client`, `NetConfig`, `poll`/`sendCommand`/`onCommand`)
- `TurnManager` `NetCommand` binary shape (ready to be packed into `NMT_SimulationCommand`)
- `tools/server.py` room-list HTTP shim (store-agnostic session list)

**What's stubbed**
- No ENet `enet_host_create` / `enet_host_service` loop (`NetServer.cpp:86` `enet_host_service` timeout, `NetEnet.cpp`)
- No `CNetServer` thread (`RunThread`), no `CNetServerSession` FSM, no `ENetPeer` handling, no `password`/`salt` (`CheckPassword`), no `XmppClient` lobby auth
- No `NetMessages.h` `NMT_*` serialization, no `NetMessageSim.cpp` `GameCommand` packet framing
- No NAT traversal, no `NetStats` ping, no `Chat` / `Ready` / `GameSetup` / `AssignPlayer` / `GameStart` / `Rejoined` / `KickPlayer` / `ClientPaused`
- No dedicated Linux headless export (per `docs/MULTIPLAYER.md` platform matrix)

**DoD — Phase 4 Network (G9 part)**
- [ ] `NetHost::startHost` calls `enet_host_create` (ENet), binds `port`, `poll` calls `enet_host_service` and dispatches `onCommand` for `NMT_SimulationCommand`
- [ ] `NetHost::startClient` `enet_host_connect`, handshake `NMT_ClientHandshake` → `NMT_Authenticate` → `NMT_Ready`
- [ ] `NetServerWorker` thread or `poll` on main thread with `TurnManager` `EndCommandBatch` coalescing (see `NetServerTurnManager.cpp`)
- [ ] `NetMessages.h` `NMT_SimulationCommand` binary matches `TurnManager::NetCommand` serialization (same `client_id`, `cmd_type`, `entity_ids`, `target_x/z`, `target_id`, `param`)
- [ ] Lobby: at least `tools/server.py` HTTP `GET /rooms` + `POST /rooms` + `GET /rooms/<id>` with `NetSession`-like `room_id`/`player_id` (or `NetServer` ENet lobby if lockstep chosen)
- [ ] `docs/MULTIPLAYER.md` decision snapshot vs lockstep documented (host-authoritative snapshots vs deterministic lockstep — see Architecture P4)

---

## G10 — Petra AI (10%)

**0AD Source**
- `ai/petra/` **32 modules** (~12k lines): `petra.js` (main `PetraBot`), `attackPlan.js`, `baseManager/`, `dangerMap.js`, `military.js`, `economy.js`, `worker.js`, `queueManager.js`, `headquarters.js`, `research.js`, `trading.js`, `diplomacy.js`, `victoryManager.js`, `config.js`, `chatHelper.js`, `helpers/`, `terrainAnalysis.js`, etc.
- `AIInterface.js` + `AIProxy.js` + `BuildingAI.js` + `BattleDetection.js` (engine → AI `AIInterface` bridge)
- `aegis/` legacy (not used), `common/` shared helpers

**RTS0 Location**
- `src/simulation/simulation.cpp:2828` `petra_manage_workers` (evacuate `threat>=7`, garrison `4m`, ungarrison `threat<2`)
- `src/simulation/simulation.cpp:2891` `petra_manage_military_production` (counter scores: `spearman 1.2 + cav*4.5`, `archer 1.0 + inf*3.0`, `siege 0.5 + forts*2.0`, champion `2.5` if phase≥3)
- `src/simulation/simulation.cpp:3010` `petra_manage_economy` (target villagers `10/18/25`, `need_*` calc)
- `src/simulation/simulation.cpp:3127` `petra_manage_buildings` (TC/Barracks/Fortress/Tower placement)
- `src/simulation/simulation.cpp:3270` `petra_manage_research` (phase town/city, econ/mil techs)
- `src/simulation/simulation.cpp:3379` `petra_manage_tactical_attacks` (`PetraStrikeForce` `MUSTER 6/10/16` threshold or `60s`, `MARCH` → pick weakest player building, `ENGAGE`)
- `src/simulation/simulation.cpp:2737` `update_petra_threat_map` `16×16` `danger = enemy - friendly*0.65`, `0.3 decay +0.7 raw`, `add_power` with falloff
- `src/simulation/simulation.h:393` `PetraThreatMap` + `PetraStrikeForce` + `ai_food_/wood_/gold_/stone` + `petra_phase_` mocks

**What is ported**
- Threat map `16×16` with `enemy_threat` / `friendly_defense` / `danger` + `update_timer 0.4s` (`simulation.cpp:2737`)
- Worker evac/garrison (`petra_manage_workers:2828`)
- Counter-unit military production scoring (`petra_manage_military_production:2891`) with phase-gated cavalry/siege/champions
- Economy villager targets + resource need (`petra_manage_economy:3010`)
- Buildings + research managers (basic)
- Strike force `MUSTER`/`MARCH`/`ENGAGE` with army thresholds `6/10/16` and weakest-building target (`petra_manage_tactical_attacks:3379`)
- Civ-aware building placement (near `base_x/base_z` for Team 1)

**What's stubbed**
- No `attackPlan.js` `AttackPlan` class (support, siege, `patrol` pathing, `state: unexecuted → walking → attacked → abort`)
- No `dangerMap.js` per-entity `dangerMap` with `InfluenceMaps` updates per tick (RTS0 `16×16` is coarse vs 0 A.D. `64×64` `NavGrid` threat)
- No `baseManager` / `queueManager` / `headquarters` (`HQ` `buildOrders`, `worker` assignment `EconomyManager`)
- No `economy.js` `EconomicState` with `resourceMaps`, `dropsite` search, `Trading` `market` selection (Petra trades via markets)
- No `military.js` `MilitaryManager` with `squads`, `attackManager`, `defenseManager`
- No `diplomacy.js` / `victoryManager.js` / `config.js` tier config
- No `AIInterface.js` JS↔C++ bridge — RTS0 Petra is native `petra_manage_*` in `tick_petra_ai`, not JS VM
- No `Trigger.js`/`TriggerPoint.js` map trigger integration

**DoD — Phase 5 Petra (G10)**
- [ ] `petra/` native modules or `ai/petra/`-like folder `src/simulation/ai/` with at least `danger_map.h`, `military.h`, `economy.h`, `attack_plan.h` (or document monolithic `simulation.cpp:2828` stays)
- [ ] `dangerMap` res matches `NavGrid` `64×64` or at least `32×32` (not coarse `16×16` only) and is used by `is_position_safe` + worker evac + attack target scoring
- [ ] `AttackPlan` `MUSTER→MARCH→ENGAGE→RETURN` with `staging_x/z`, `target_building_id`, `state_timer`, and `abort if threat > threshold`
- [ ] `QueueManager` — production queues per building respect `pop cap`, `resources`, `tech` and `civ` bonuses (Persian `0.80` cav speed)
- [ ] `EconomyManager` — `target_villagers` adaptive (`10/18/25` is stub; real is `baseManager` `worker` count based on `resourceMaps`)
- [ ] `BattleDetection.js` parity: Petra detects player army via `threat_map` + `attackDetection` and counter-produces (already partially `2891` but verify `check_parity.py` vs 0 A.D. `military.js` counters)
- [ ] `check_parity.py` — seed map, 20 player swordsmen at `(0,0)` vs 5 AI spears — Petra `spearman_score` > `archer_score` and next train is spear

---

## Phase 0 Foundation — Checklist (must be done before G1-G10)

> This checklist was requested explicitly. It is the “grid_nav.h, territory_grid_, SoA xp/waypoint, check_parity.py” foundation that `SConstruct:28` vendor boundary leaves unfinished.

- [ ] `src/simulation/grid_nav.h` tightened and wired (already exists `230` lines, but not used by `Simulation`):
  - [ ] `Grid<T>` semantics verified vs `src/0ad/simulation2/helpers/Grid.h:412` ( `reset`, `fill`, `inside`, `at`, `w/h/data`, `operator==` )
  - [ ] `NavGrid::DEFAULT_RES=64` `cells`+`dirty` owned by `Simulation` (`simulation.h` add `NavGrid nav_grid_; Grid<uint8_t> territory_grid_;`)
  - [ ] `NavGrid::mark_building` called on `spawn_building` / `tick_construction` / `build_progress>=1.0`
  - [ ] `NavGrid::set_water_from_heights` called after `generate_random_map` / `set_terrain_data`
  - [ ] `NavGrid::blur_territory` implemented (3×3 box blur, see `grid_nav.h:142` TODO)
  - [ ] `WaypointPath` SoA (`MAX_WP=64`, `xs/zs`, `count`, `cur`, `advance`, `push_back`) owned per unit or via `SoAStorage` SoA columns `waypoint_*`

- [ ] `territory_grid_` SoA:
  - [ ] `Grid<uint8_t> territory_grid_; Grid<uint8_t> influence_grid_;` in `Simulation` (`simulation.h:??` alongside `nav_grid_` and `fow_grid_[64*64]`)
  - [ ] `rebuild_territory()` called 1 Hz (mirrors `TerritoryManager.cpp` `UpdateTerritory`)
  - [ ] Minimap + `terrain.gdshader` upload path verified (texture `R8` per cell)

- [ ] SoA `xp / waypoint`:
  - [ ] `UnitSoA` extended (see `src/core/memory/soa_storage.h:21`): add columns `waypoint_x[MAX_WP]?` OR per-unit `WaypointPath` small array (preferred: `WaypointPath waypoint[UNIT_CAP]` separate array, not SoA-interleaved)
  - [ ] `xp` already exists as `kills`/`rank` (`soa_storage.h:55` `kills:uint16_t`, `rank:uint8_t`) — verify `Promotion.js` 3 kills → rank 2 `+15% HP +10% dmg`, 7 kills → rank 3 `+30% HP +25% dmg` (currently `1.15×` then `1.15×` = `1.32×`, not `1.30×` — note delta)
  - [ ] `waypoint` per unit `WaypointPath path[UNIT_CAP];` with `path[i].clear()` on `reset()` and `remove_at` swap

- [ ] `tools/check_parity.py` (new):
  - [ ] Python script that `import`s or `subprocess` runs `../0ad` helpers (or at least checks `src/0ad/simulation2/helpers/LongPathfinder.cpp:400` octile vs `grid_nav.h:76` note, `Market.js:42` `pow` vs `simulation.cpp:694` static, `Auras.js` range vs `tick_gather:1654` `10m`, `Formation.js` offsets vs `cmd_move:707`, `HierarchicalPathfinder.h:177` `CHUNK_SIZE` vs `NavGrid::DEFAULT_RES`)
  - [ ] CLI: `python3 tools/check_parity.py --unit nav --seed 42` exits `0` if within tolerance, `1` with diff
  - [ ] Also checks `SConstruct:28` vendor not built (`"0ad" in root` skip) and `Fixed.h:408` vs `float` decision logged
  - [ ] CI hook: run `check_parity.py` on `pull_request` (optional, but doc says to run `python3 tools/build.py` anyway)

- [ ] Vendor lineage documented:
  - [ ] `src/0ad/README.md` already lists `maths/Fixed.h`, `Grid.h`, `Pathfinding.h`, `HierarchicalPathfinder.h`, `LongPathfinder.cpp` as reference (`SConstruct:28` comment)
  - [ ] `docs/ARCHITECTURE.md` `### Fidelity & Vendor Boundary (2026-08-29)` updated (see below)

---

## Verification Commands

Run these after each Phase 0-5 gap closure. They are the canonical `scons` + `build.py` commands requested.

```bash
# 1. Native GDExtension (requires godot-cpp at godot-cpp/ branch 4.3)
# See SConstruct:28 — src/0ad/ is reference only, not built.
scons target=template_debug -j8
scons target=template_release -j8

# 2. Cross-platform Godot export driver (tools/build.py)
# Builds GDExtension first (via scons), then Godot headless import + export.
# Env: GODOT=/path/to/Godot_v4.3-stable_linux.x86_64
python3 tools/build.py linux --skip-scons   # if scons already ran
python3 tools/build.py linux                # full (scons + godot)
python3 tools/build.py all                  # windows+linux

# 3. Parity check (Phase 0)
python3 tools/check_parity.py --unit nav --seed 42 --verbose
python3 tools/check_parity.py --unit territory --seed 42
python3 tools/check_parity.py --unit market --trades 10
python3 tools/check_parity.py --unit auras --range 10,14,16,18
python3 tools/check_parity.py --unit formation --count 12 --type phalanx

# 4. Quick sim smoke (headless, no Godot)
# If you add a native test harness:
# ./bin/librtso.linux.template_debug.so --test-sim --ticks 1000 --seed 42

# 5. Asset bake (if you change art pipeline)
python3 tools/extract_0ad_assets.py --help
python3 tools/build_assets.py

# 6. Lint / diff vs 0 A.D.
diff -u ../0ad/source/simulation2/helpers/Grid.h src/simulation/grid_nav.h | head -n 100
diff -u ../0ad/source/simulation2/helpers/HierarchicalPathfinder.h src/simulation/grid_nav.h | head -n 100
```

> Note: `scons` compiles `src/` but skips `src/0ad/` (see `SConstruct:28` `if "0ad" in root.split(os.sep): continue`). This is intentional — vendor is reference + future porting, not linked today. Native SoA in `src/simulation/simulation.cpp:4194` collapses `31 CCmp* + 84 JS` components into a single `Simulation` class; `src/0ad/` stays as `diff -u` reference.

---

## Appendix A — Vendor Boundary & File Counts

- `src/0ad/` vendor: `maths/Fixed.h:408` (`CFixed_15_16`), `Fixed.cpp`, `Matrix3D.h`, `Vector2D/3D.h`, `Grid.h:412`, `Pathfinding.h:85`, `HierarchicalPathfinder.h:177/.h:353`, `HierarchicalPathfinder.cpp:865`, `LongPathfinder.cpp:400`, `VertexPathfinder.cpp`, `Geometry.h/.cpp`, `Rasterize.h/.cpp`, `ps/CStr.h`, `network/NetClientTurnManager.cpp:188`, `NetServer.cpp:457`, `NetServerTurnManager`, `NetMessages.h`, `lobby/XmppClient`
- `src/simulation/simulation.cpp:4194` — **single-file SoA** (no `CCmp*` split). `SConstruct:28` skips `src/0ad/` at build; `src/0ad/README.md:11` says `These files are reference + future build target; current SoA rewrite in src/core + src/simulation uses same algorithms but with arena (64B bump) + SoA cache lines`
- `src/core/memory/soa_storage.h:21` `UnitSoA` / `ResourceSoA` (`64B` aligned, `pos_x/z`, `vel_x/z`, `target_x/z`, `anchor_x/z`, `hp/max_hp`, `kills/rank`, `status_effects`, `waypoint` planned)
- `src/simulation/sim_types.h:105` `UnitType` 23 types, `BuildingType` 14, `ResourceType` 4, `Civilization` 13, `FaunaType` 4, `StatusEffect` 4, `MapType` 11
- `assets/models/manifest.json` — `animations` 40+ PSAs, `models` 24 GLBs, `origin` 0 A.D. `public.zip`, `mapping` unit↔model
- `gui/session/` 251 files — see `docs/ASSET_MAPPING.md` + `tools/extract_ui_assets.py`
- `ai/petra/` 32 modules — see G10

## Appendix B — Fidelity Scale (for new gaps)

- `0%` not started (G9 lobby 3%)
- `1-10%` stub / hard-coded (G1 5%, G4 10%, G8 5%, G10 10%)
- `11-30%` partial math (G2 15%, G3 20%, G5 25%)
- `31-50%` usable but not 1:1 (G6 40%, G7 35%)
- `51-80%` close (combat ~80%)
- `81-100%` parity (`Fixed 15.16` semantics, `Grid` `memset/bitwise_or`, `Hierarchical` flood-fill)

## Appendix C — How PORTING_TODO.md [x] Maps to Gaps

| PORTING_TODO Phase | Claim | True Gap | Fidelity |
|---|---|---|---|
| Phase 1-3 (HUD/Selection/Action) | [x] | G6 UI | 40% (layout simplified) |
| Phase 4-6 (Production/Build/Garrison) | [x] | G6 UI + sim `can_place` | 50% |
| Phase 7 (Tech) | [x] | Sim `cmd_research` OK, G6 UI | 60% |
| Phase 8 (Market) | [x] | G3 Market | 20% |
| Phase 9 (Territory/Minimap) | [x] | G2 Territory | 15% |
| Phase 12-13 (Sound/Combat) | [x] | combat 80% ✓, projectiles 70% | 80% |
| Phase 15 (Multiplayer) | [x] | G8 TurnManager 5% + G9 Lobby 3% | 5% |
| Phase 16-30 (Specials) | [x] | G4 Auras 10%, G5 Formation 25%, G10 Petra 10% | 10-25% |

> Summary: `PORTING_TODO.md` is a **feature checklist** (does the button exist?). `REMAINING_GAPS.md` is a **parity checklist** (does the math match 0 A.D.?). Both are needed — do not delete `PORTING_TODO.md`.

---

*Generated 2026-08-29. Next audit after Phase 0 + Phase 1 (G1) closed. Run `scons && python3 tools/build.py linux && python3 tools/check_parity.py` then bump date.*
