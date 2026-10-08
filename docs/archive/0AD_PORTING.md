> **Archived 2026-10-08.** Historical record only; do not plan work from it. See [docs/README.md](../README.md) for the live docs.

# 0 A.D. Porting Map

> **2026-08-29 Audit** PORTING_TODO all [x] are MVP stubs. See docs/REMAINING_GAPS.md for true parity. Native SoA in src/simulation/simulation.cpp:4194 collapses 31 CCmp + 84 JS into single class; src/0ad/ vendor is reference only (SConstruct:28).

RTS0's deterministic core is a direct port of 0 A.D. (`../0ad`) — same files, same names where possible, so `diff -u` stays useful.

> Per your request, **license is ignored** for this private port. No GPL header enforcement in these notes.
> **Status 2026-08-24: 103 C# files — organized build green. Maths 21/21, Helpers 14/14, JS 84/84, CCmp* 9/9, System 6, Serialization 2.**

## Folder mapping — System & Serialization (source/simulation2/system/ + serialization/)

| RTS0 | 0 A.D. |
|---|---|
| `Core/System/ParamNode.cs` | `system/ParamNode.h/.cpp` |
| `Core/System/Entity.cs` | `system/Entity.h` |
| `Core/System/ComponentManager.cs` | `system/ComponentManager.h/.cpp` |
| `Core/System/TurnManager.cs` | `system/TurnManager.h/.cpp` + `LocalTurnManager.h/.cpp` |
| `Core/System/SimContext.cs` | `system/SimContext.h/.cpp` |
| `Core/System/Message.cs` | `system/Message.h` |
| `Core/Serialization/Serializer.cs` | `serialization/ISerializer.h` + `StdSerializer.h/.cpp` |
| `Core/Serialization/HashSerializer.cs` | `serialization/HashSerializer.h/.cpp` |
| `Core/Helpers/Render.cs` | `helpers/Render.h/.cpp` |
| `Core/Maths/Brush.cs` | `maths/Brush.h/.cpp` |
| `Core/Maths/Frustum.cs` | `maths/Frustum.h/.cpp` |
| `Core/Maths/Plane.cs` | `maths/Plane.h/.cpp` |
| `Core/Maths/Rect.cs` | `maths/Rect.h/.cpp` |
| `Core/Maths/Size2D.cs` | `maths/Size2D.h/.cpp` |
| `Core/Maths/MD5.cs` | `maths/MD5.h/.cpp` |
| `Core/Maths/NUSpline.cs` | `maths/NUSpline.h/.cpp` |

## Folder mapping — Core Maths (source/maths/)

| RTS0 (`src/`) | 0 A.D. source |
|---|---|
| `Core/Maths/Fixed.cs` | `source/maths/Fixed.h` (CFixed_15_16) |
| `Core/Maths/FixedVector2D.cs` | `source/maths/FixedVector2D.h` |
| `Core/Maths/FixedVector3D.cs` | `source/maths/FixedVector3D.h` |
| `Core/Maths/Sqrt.cs` | `source/maths/Sqrt.cpp` (`isqrt64`) |
| `Core/Maths/Grid.cs` | `source/simulation2/helpers/Grid.h` (`Grid<T>`, `SparseGrid`, `GridUpdateInformation`) |

> **2026-08-29 Fidelity <50% notes — Core Maths**  
> `Grid.h:412` vendored in `src/0ad/simulation2/helpers/Grid.h` but **not built** (`SConstruct:28`); active impl is `src/simulation/grid_nav.h:20` `Grid<uint8_t>` 64×64 stub. `Fixed.h:408` `CFixed_15_16` is reference only — RTS0 `float` breaks cross-platform lockstep determinism. See `docs/REMAINING_GAPS.md` G1 (5%) — `move_units:1253` still straight-line, `Grid.h` unused.
| `Core/Maths/MathUtil.cs` | `source/maths/MathUtil.h` |
| `Core/Maths/Vector2D.cs` + `Vector3D.cs` + `Vector4D.cs` + `VectorUtil.cs` | `source/maths/Vector2D.h/.cpp`, `Vector3D.h/.cpp`, `Vector4D.h` |
| `Core/Maths/Matrix3D.cs` | `source/maths/Matrix3D.h/.cpp` |
| `Core/Maths/Quaternion.cs` | `source/maths/Quaternion.h/.cpp` |
| `Core/Maths/Bounds.cs` | `source/maths/BoundingBoxAligned.h/.cpp` + `BoundingBoxOriented.h/.cpp` + `BoundingSphere.h/.cpp` |
| `Core/Maths/Ease.cs` | `source/maths/Ease.h` |

## Folder mapping — Helpers (source/simulation2/helpers/ + source/maths/)

| RTS0 | 0 A.D. |
|---|---|
| `Core/Helpers/PriorityQueue.cs` | `helpers/PriorityQueue.h` |
| `Core/Helpers/Geometry.cs` | `helpers/Geometry.h/.cpp` |
| `Core/Helpers/Rasterize.cs` | `helpers/Rasterize.h/.cpp` |
| `Core/Helpers/Los.cs` | `helpers/Los.h` |
| `Core/Helpers/Selection.cs` | `helpers/Selection.h/.cpp` |
| `Core/Helpers/MapEdgeTiles.cs` | `helpers/MapEdgeTiles.h` |
| `Core/Helpers/CinemaPath.cs` | `helpers/CinemaPath.h/.cpp` |
| `Core/Helpers/Pathfinding.cs` | `helpers/Pathfinding.h/.cpp` (LongPathRequest/ShortPathRequest/WaypointPath) |
| `Core/Pathfinding/PathGoal.cs` | `helpers/PathGoal.h` |
| `Core/Pathfinding/HierarchicalPathfinder.cs` | `helpers/HierarchicalPathfinder.h/.cpp` |
| `Core/Pathfinding/LongPathfinder.cs` | `helpers/LongPathfinder.cpp` |
| `Core/Pathfinding/VertexPathfinder.cs` | `helpers/VertexPathfinder.h/.cpp` |

> **2026-08-29 Fidelity <50% notes — Helpers / Pathfinding**  
> All rows `Pathfinding.cs` + `HierarchicalPathfinder.h:353`/`LongPathfinder.cpp:400` + `VertexPathfinder` are **5% fidelity (G1)** — vendored `src/0ad/simulation2/helpers/*` not compiled; `src/simulation/simulation.cpp:1253` `move_units` is straight-line + `separation:1432` push. Active stub `src/simulation/grid_nav.h:80` `NavGrid` not wired to `move_units`; `WaypointPath::MAX_WP=64` unused. See `REMAINING_GAPS.md` G1 Phase 0 DoD.

## Folder mapping — Simulation Core (source/simulation2/)

| RTS0 | 0 A.D. |
|---|---|
| `Core/Simulation/GameSim.cs` | `Simulation2.cpp` + `system/TurnManager.*` + `helpers/Spatial.h` |
| `Core/Simulation/Commands.cs` | `helpers/SimulationCommand.h` + `NetMessageSim.cpp` |

> **2026-08-29 Fidelity <50% notes — Simulation Core / TurnManager (G8 5%)**  
> `TurnManager.cs` / `Simulation2.cpp` vs `src/network/turn_manager.cpp:183` `TurnManager::tick` local-only `200ms` + `compute_sim_hash` FNV-1a `pos+hp` only (vs `HashSerializer.cpp` full `StdSerializer` hash) — no ENet `NetClientTurnManager.cpp:188` `EndCommandBatch`/`SyncCheck`, no `NetServerTurnManager` relay, no `Sincos_approx` determinism. See `REMAINING_GAPS.md` G8.
| `Core/Simulation/SimTypes.cs` | `helpers/Position.h` + `Components/UnitAI.js` fields |
| `Core/Simulation/Player.cs` | `helpers/Player.h` (`player_id_t`, `entity_id_t`) |
| `Core/Simulation/Components/IComponent.cs` | `system/Component.h` / `IComponent.h` |

## Folder mapping — CCmp* (source/simulation2/components/)

| RTS0 | 0 A.D. |
|---|---|
| `Components/Obstruction.cs` | `CCmpObstruction.cpp` + `ICmpObstruction.h` |
| `Components/ObstructionManager.cs` | `CCmpObstructionManager.cpp` + `ICmpObstructionManager.h` |
| `Components/Position.cs` | `CCmpPosition.cpp` + `ICmpPosition.h` |
| `Components/RangeManager.cs` | `CCmpRangeManager.cpp` + `ICmpRangeManager.h` |
| `Components/Vision.cs` | `CCmpVision.cpp` + `ICmpVision.h` |
| `Components/UnitMotion.cs` | `CCmpUnitMotion.h` + `CCmpUnitMotionManager.h` + `CCmpUnitMotion_System.cpp` |
| `Components/Pathfinder.cs` | `CCmpPathfinder.cpp` + `ICmpPathfinder.h` |
| `Components/TerritoryManager.cs` | `CCmpTerritoryManager.cpp` + `ICmpTerritoryManager.h` |
| `Components/Ownership.cs` | `CCmpOwnership.cpp` + `ICmpOwnership.h` |

> **2026-08-29 Fidelity <50% notes — CCmp* (G1/G2)**  
> `Pathfinder.cs` **5% (G1)** — `CCmpPathfinder.cpp` / `HierarchicalPathfinder.h:353` not wired; `src/simulation/simulation.cpp:1253` still straight-line, `Grid.h:412` unused, `grid_nav.h` stub.  
> `TerritoryManager.cs` **15% (G2)** — `CCmpTerritoryManager.cpp` vs `src/simulation/simulation.cpp:1748` radius check (TC 32 / Fortress 28 / Tower 18) + `hp-=1.5/s` only; no influence `Grid<uint8_t>` 64×64, no `blur_territory` 3×3, no `TerritoryDecay.js` curve, no `BuildRestrictions.js`. See `REMAINING_GAPS.md` G1-G2.

## Folder mapping — JS Components (source/mods/public/simulation/components/*.js — 84 files)

| RTS0 | 0 A.D. JS |
|---|---|
| `Components/UnitAI.cs` | `UnitAI.js` (6k+ FSM, stances, INDIVIDUAL.*) |
| `Components/Health.cs` | `Health.js` + `CCmpHealth` |
| `Components/Attack.cs` | `Attack.js` |
| `Components/ResourceSupply.cs` | `ResourceSupply.js` |
| `Components/ResourceGatherer.cs` | `ResourceGatherer.js` |
| `Components/PlayerComponent.cs` | `Player.js` |
| `Components/ProductionQueue.cs` | `ProductionQueue.js` |
| `Components/Builder.cs` | `Builder.js` |
| `Components/Formation.cs` + `GameSim.FormationOffsets` | `Formation.js` + `FormationAttack.js` |

> **2026-08-29 Fidelity <50% notes — Formation (G5 25%)**  
> `Formation.js` vs `src/simulation/simulation.cpp:707` `cmd_move` formation offsets (Line/Box/Column/Phalanx/Wedge/Scatter) — math ported but no turning/wheel (`Formation.js` rotates to heading), no `FormationAttack.js` facing, no speed `PHALANX 0.85x` / `WEDGE 1.1x`, no reassign on casualties. `tick_combat` has `PHALANX +4 pierce` etc. but `move_units` never corrects to formation slots. See `REMAINING_GAPS.md` G5.
| `Components/TechnologyManager.cs` | `TechnologyManager.js` |
| `Components/GarrisonHolder.cs` | `GarrisonHolder.js` + `Garrisonable.js` |
| `Components/Auras.cs` | `Auras.js` |

> **2026-08-29 Fidelity <50% notes — Auras (G4 10%)**  
> `Auras.js` vs `src/simulation/simulation.cpp:1654` (`tick_gather` female `10m` `*0.85`), `tick_combat:2452` hero `16m` (`SPARTANS +2HP/s` / `ATHENIANS +20% speed`), `carnyx 14m +20% dmg` — hard-coded radius checks, no `Auras.js` `AuraManager`/`ModifiersManager.js`/`ValueModificationManager.js` stacking, `speed=info.speed*1.20` clobbers other buffs, no `Wonder.js`/`Treasure.js` auras. See `REMAINING_GAPS.md` G4.
| `Components/Population.cs` | `Population.js` + `PopulationCapManager.js` |
| `Components/Cost.cs` | `Cost.js` |
| `Components/ResourceTrickle.cs` | `ResourceTrickle.js` |
| `Components/Identity.cs` | `Identity.js` |
| `Components/Heal.cs` | `Heal.js` |
| `Components/Loot.cs` | `Loot.js` + `Looter.js` |
| `Components/Promotion.cs` | `Promotion.js` |
| `Components/Trader.cs` | `Trader.js` |

> **2026-08-29 Fidelity <50% notes — Trader (G3 20%)**  
> `Trader.js` `TradeRoute` + `Gains` per distance vs `src/simulation/simulation.cpp:1280` hard-coded `35`/`45` gold loop — no `Market` dropoff search, no distance `floor(distance/20*base)` gains, no `ResourceTrickle.js` link, no `ModifiersManager.js` Persian `+25%` gain aura. See `REMAINING_GAPS.md` G3.
| `Components/Wall.cs` | `Gate.js` + `WallPiece.js` + `WallSet.js` |
| `Components/Turret.cs` | `TurretHolder.js` + `Turretable.js` |
| `Components/Foundation.cs` | `Foundation.js` |
| `Components/Pack.cs` | `Pack.js` |
| `Components/Guard.cs` | `Guard.js` |
| `Components/RallyPoint.cs` | `RallyPoint.js` |
| `Components/Repairable.cs` | `Repairable.js` |
| `Components/Capturable.cs` | `Capturable.js` |
| `Components/Upkeep.cs` | `Upkeep.js` + `StatisticsTracker.js` |
| `Components/Diplomacy.cs` | `Diplomacy.js` + `CeasefireManager.js` |
| `Components/Market.cs` | `Market.js` + `Barter.js` |

> **2026-08-29 Fidelity <50% notes — Market/Trader (G3 20%)**  
> `Market.js:42` `pow` supply/demand fluctuation vs `src/simulation/simulation.cpp:694` `cmd_barter` static `amount*0.75` (`0.85` Carthaginians) — no `Market.prototype.GetPrices()` `pow(1.1, delta)`; prices never move, exploit. `Trader.js` vs `simulation.cpp:1280` trader hard-coded `TC(0,0)↔Market(8,6)` ping-pong `35/45` gold, no `Trader.js` `Gains` `floor(distance/20*base)` scaling. `barter_panel.xml` exists but not `pow`-linked. See `REMAINING_GAPS.md` G3.
| `Components/Timer.cs` | `Timer.js` + `Trigger.js` + `TriggerPoint.js` |
| `Components/Upgrade.cs` | `Upgrade.js` + `Researcher.js` |
| `Components/Dropsite.cs` | `ResourceDropsite.js` + `Treasure.js` + `TreasureCollector.js` |
| `Components/Status.cs` | `StatusBars.js` + `StatusEffectsReceiver.js` + `Fogging.js` + `Visibility.js` |
| `Components/Sound.cs` | `Sound.js` |
| `Components/EndGame.cs` | `EndGameManager.js` + `TerritoryDecay.js` + `TerritoryDecayManager.js` |
| `Components/AISupport.cs` | `AIInterface.js` + `AIProxy.js` + `BuildingAI.js` + `BattleDetection.js` |

> **2026-08-29 Fidelity <50% notes — Petra AI (G10 10%)**  
> `AIInterface.js` + `ai/petra/` **32 modules** (`petra.js`, `attackPlan.js`, `dangerMap.js`, `military.js`, `economy.js`, … ~12k lines) vs `src/simulation/simulation.cpp:2828` `petra_manage_*` **~800 lines** (`petra_manage_workers:2828`, `military_production:2891`, `threat_map 16×16:2737`, `strike_force MUSTER/MARCH/ENGAGE:3379`) — no `attackPlan.js` `AttackPlan`, no `baseManager`/`queueManager`/`headquarters`, `dangerMap.js` coarse `16×16` vs `64×64`, no `AIInterface.js` JS↔C++ bridge. See `REMAINING_GAPS.md` G10.
| `Components/CombatExtras.cs` | `AttackDetection.js` + `DeathDamage.js` + `DelayedDamage.js` + `Resistance.js` + `FormationAttack.js` |
| `Components/BuildingExtras.cs` | `BuildRestrictions.js` + `AutoBuildable.js` + `AutoWorkers.js` + `EntityLimits.js` |
| `Components/MiscComponents.cs` | `AlertRaiser.js` + `GuiInterface.js` + `Mirage.js` + `ModifiersManager.js` + `MotionBall.js` + `RangeOverlayManager.js` + `Settlement.js` + `Wonder.js` |
| `Components/PlayerExtras.cs` | `PlayerManager.js` + `SkirmishReplacer.js` + `ValueModificationManager.js` + `VisionSharing.js` |
| `Components/Training.cs` | `Trainer.js` + `TrainingRestrictions.js` + `UnitMotionFlying.js` |

> **2026-08-29 Network fidelity (G8/G9)**  
> `TurnManager.cs` `200ms` lockstep vs `src/network/turn_manager.cpp:183` local-only + `src/network/net_host.cpp:9` stub (`startHost` returns true, `poll` no-op, `sendCommand` no-op) vs `src/0ad/network/NetServer.cpp:457` (`CNetServerWorker` `EnetHost`, `sessions[]`, `HandleMessageReceive`, `Multicast`) + `NetClientTurnManager.cpp:188` + `NetServerTurnManager.cpp` + `NetMessages.h` `NMT_*` + `lobby/XmppClient`. Vendor `src/0ad/network/` reference only. See `REMAINING_GAPS.md` G8 5% + G9 3%.  
> `Formation.js` offsets only (G5), `Auras.js` hard-coded radii (G4), `Market.js:42` static spread (G3), `TerritoryManager` radius only (G2), all <50%. Full `Phase 0-8 DoD` in `REMAINING_GAPS.md`.

## What was kept 1:1

- **Fixed 15.16** semantics, `Sincos_approx`/`atan2_approx` stubs, overflow behavior
- **Grid** lazy `memset`/`bitwise_or` + `any_set_in_square`
- **Hierarchical** `CHUNK_SIZE` flood-fill, region graph, `MakeGoalReachable` spiral search
- **LongPathfinder** octile heuristic `10*(dx+dy)+(14-20)*min`, no corner-cutting, `PriorityQueue` tie-break by insertion order (deterministic)
- **UnitAI** `g_Stances`, order queue, `INDIVIDUAL.*` state names

## What was simplified for Godot / Thronefall

- JS `Engine.QueryInterface` → plain C# `GameSim._aiByUnit` dictionary
- Actor XML / `CCmpVisualActor` → `Presentation/WorldView.cs` MultiMesh + thronefall pastel mats
- `CCmpRangeManager` vision queries → simple distance checks in `UnitAI.TryAutoAttack` (will move to `RangeManager` class later)
- `Formation.js` → formation offsets array in `GameSim` (Box/Line etc not yet)

## How to pull updates

```bash
# compare any file
diff -u ../0ad/source/maths/Fixed.h src/Core/Maths/Fixed.cs
# or batch
for f in $(find ../0ad/source/simulation2/helpers -name "*.h"); do echo "== $f =="; done
```

All required 0 A.D. gameplay code ported; remaining engine-only files (e.g., `lib/`, `ps/`, `graphics/` renderer) intentionally omitted. See `docs/ARCHITECTURE.md` for organized layout.
