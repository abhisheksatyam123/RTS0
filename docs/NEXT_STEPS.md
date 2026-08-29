# Next Steps (prioritized)

> **2026-08-29 Audit** — This file was **re-prioritized** against true parity (`docs/REMAINING_GAPS.md` G1-G10). The old P0-P4 below are kept as **legacy** (2026-08-24 MVP checklist). New P0-P5 is the source of truth, ordered by unblock (Navigation → Territory → Economy → Combat → UI/Assets → Network → Petra). Native SoA is `src/simulation/simulation.cpp:4194`; vendor is reference only `SConstruct:28`.

## P0 — Foundation + Navigation (G1) — **Blocking**

- **G1 Pathfinding 5%** `CCmpPathfinder.cpp` / `HierarchicalPathfinder.h:353` vs `src/simulation/simulation.cpp:1253` `move_units` straight-line, `separation:1432`, `Grid.h:412` unused → port `HierarchicalPathfinder` + `LongPathfinder.cpp:400` octile onto `src/simulation/grid_nav.h` `NavGrid` (64×64) + `WaypointPath::MAX_WP=64`.
- **Phase 0 checklist (must be done with G1):** `src/simulation/grid_nav.h` wiring, `territory_grid_` `Grid<uint8_t>` in `Simulation` (`simulation.h`), SoA `kills/rank` verify + `waypoint` per-unit `WaypointPath path[UNIT_CAP]`, `tools/check_parity.py` for `Grid.h`/`LongPathfinder`/`Hierarchical` octet.
- **DoD ref:** `docs/REMAINING_GAPS.md` § G1 + § Phase 0 Foundation.  
- **Verify:** `scons target=template_debug -j8 && python3 tools/build.py linux --skip-scons && python3 tools/check_parity.py --unit nav --seed 42`

## P1 — Territory + Auras (G2 + G4) — **Blocking (territory), Balance (auras)**

- **G2 Territory 15%** `CCmpTerritoryManager.cpp` vs `src/simulation/simulation.cpp:1748` radius `32/28/18m` + `hp-=1.5/s` stub → rebuild `territory_grid_` 64×64 + `blur_territory` 3×3 box (`grid_nav.h:130`), `rebuild_territory()` 1 Hz, `can_place` + `BuildRestrictions.js`, `terrain.gdshader` upload.
- **G4 Auras 10%** `Auras.js` vs `src/simulation/simulation.cpp:1654` female `10m *0.85`, `tick_combat:2452` hero `16m`, `carnyx 14m +20%`, `temple 18m +3.5HP/s` hard-coded → `AuraManager` + `ModifiersManager.js`/`ValueModificationManager.js` stacking (`src/core/memory/soa_storage.h` SoA).
- **DoD ref:** `REMAINING_GAPS.md` G2 + G4.  
- **Verify:** `python3 tools/check_parity.py --unit territory --seed 42 && python3 tools/check_parity.py --unit auras --range 10,14,16,18`

## P1b — Economy Market / Trader (G3) — **Blocking**

- **G3 Market/Trader 20%** `Market.js:42` `pow(1.1, delta)` vs `src/simulation/simulation.cpp:694` `cmd_barter` static `0.75` (`0.85` Carthaginians), `simulation.cpp:1280` trader hard-coded `TC↔(8,6)` `35/45` gold → `prices[4]` `pow`-based fluctuation, `Trader gains floor(distance/20*base)` with civ mult (Persian `1.25` etc.), `order_trade(marketA,marketB)` + `barter_panel.xml` live price labels.
- **DoD ref:** `REMAINING_GAPS.md` G3.  
- **Verify:** `python3 tools/check_parity.py --unit market --trades 10`

## P2 — Combat Polish (G5 + combat cleanup) — **Visual / Balance**

- **G5 Formation 25%** `Formation.js` / `FormationAttack.js` vs `src/simulation/simulation.cpp:707` `cmd_move` offsets (Line/Box/Column/Phalanx/Wedge/Scatter) — add heading rotation `atan2` + `cos/sin`, `move_units` formation-slot correction, `PHALANX 0.85x` / `WEDGE 1.1x` speed, `FormationAttack.js` facing. Elev. + counters + splash already ~80% in `tick_combat:1853`. See `REMAINING_GAPS.md` G5.
- Also audit `Loot.js`/`Looter.js` vs `tick_combat` loot `+8G+6F` / building loot `+25` — close but note `Damange.js` splash `4.5m*0.6` vs Catapult `4.5m quadratic falloff` parity (see `docs/PORTING_TODO.md` Phase 23).
- **Verify:** `python3 tools/check_parity.py --unit formation --count 12 --type phalanx && scons && python3 tools/build.py linux --skip-scons`

## P3 — UI + Assets (G6 40% + G7 35%) — **UX / Visual**

- **G6 UI 40%** `gui/session/` **251 files** vs `src/presentation/main_node.cpp:2322` monolithic `RtsoMain` — split `TopPanel.cpp`/`SelectionPanel.cpp`/`ActionPanel.cpp`/`ProductionQueueView.cpp`, wire `GuiInterface.js` `GetExtendedInfo` parity list, territory blur on minimap (G2 dependent). See `REMAINING_GAPS.md` G6.
- **G7 Assets 35%** `art/meshes/` ~800 PMD v5 vs `assets/models/manifest.json` **24 GLBs** + `src/graphics/assets/model_library.cpp:719` `ModelLibrary::load` + `procedural.h` fallback — cover `UNIT_TYPE_COUNT=23` + `BLD_TYPE_COUNT=14` + 4 fauna unique GLBs, `.psa` → `AnimationPlayer` (`biped_attack`/`archer_attack`) via `capture_crowd_skin`/`bake_crowd_skin`, `terrain.gdshader` splat sampling. See `REMAINING_GAPS.md` G7.
- **Verify:** `python3 tools/build.py linux && python3 tools/check_parity.py --unit assets` (manifest count vs PMD), visual diff `diff -u` not needed

## P4 — Network — TurnManager + Lobby (G8 5% + G9 3%) — **Blocking**

- **G8 TurnManager 5%** `NetClientTurnManager.cpp:188` / `TurnManager.h/.cpp` + `HashSerializer` vs `src/network/turn_manager.cpp:183` local-only `200ms` + FNV-1a `pos+hp` hash, no `EndCommandBatch`/`SyncCheck`, no `NetServerTurnManager` relay — implement `HashSerializer` full `StdSerializer` hash (all SoA fields including `territory_grid_`, `fow_grid_`, `projectiles`, `relics`), `StdSerializer` deterministic order, `EndCommandBatch` handling.
- **G9 Lobby 3%** `NetServer.cpp:457` `CNetServerWorker` `EnetHost` `sessions[]` FSM vs `src/network/net_host.cpp:9` stub (`startHost` true, `poll` no-op) → wire ENet (`enet_host_create`/`enet_host_service`), `NetMessages.h` `NMT_ClientHandshake`→`Authenticate`→`Ready`→`GameStart`→`SimulationCommand`→`SyncCheck`, `tools/server.py` HTTP room-list shim.
- **Decision snapshot vs lockstep (see `docs/MULTIPLAYER.md` + `docs/REMAINING_GAPS.md` P4):**  
  `MULTIPLAYER.md` currently chooses **host-authoritative snapshots** (one host simulates, clients render snapshots) for co-op / mobile jitter tolerance, cross-platform `float` tolerance — `RTS0 Location` snapshot-friendly (`turn_manager.h:30` `TURN_DURATION=0.200f` already lockstep-shaped). **Lockstep** (classic AoE) requires `Fixed.h:408` `CFixed_15_16` for entire sim + `isqrt64` + `Sincos_approx` determinism + `HashSerializer` OOS. **Recommendation:** keep **snapshot** for MVP (fits `hundreds of units, co-op/wave defense` scope, avoids `Fixed` rewrite), but keep **sim boundary** (`core/` vs `presentation/` split per `MULTIPLAYER.md` Architecture rules) so lockstep door stays open for ranked 1v1 later — document choice in `ARCHITECTURE.md` Fidelity & Vendor Boundary.
- **DoD ref:** `REMAINING_GAPS.md` G8+G9, `docs/MULTIPLAYER.md` M2/M3 milestones.  
- **Verify:** `scons && python3 tools/build.py linux && ./build/linux/RTS0.x86_64 --host & ./build/linux/RTS0.x86_64 --join 127.0.0.1` + replay hash `turn_manager.cpp` `save_replay`/`load_replay` round-trip

## P5 — Petra AI (G10 10%) — **Progression**

- **G10 Petra 10%** `ai/petra/` **32 modules** (`petra.js`, `attackPlan.js`, `dangerMap.js`, `military.js`, `economy.js` … ~12k lines) vs `src/simulation/simulation.cpp:2828` `petra_manage_*` ~800 lines (`workers:2828`, `military_production:2891`, `economy:3010`, `buildings:3127`, `research:3270`, `tactical_attacks:3379`, `threat_map 16×16:2737` `danger=enemy-friendly*0.65`) — add `AttackPlan` `MUSTER→MARCH→ENGAGE`, `dangerMap` `64×64` (not coarse `16×16`), `queueManager`/`headquarters`/`baseManager`, `EconomyManager` adaptive `10/18/25` villagers, `MilitaryManager` squads.
- **DoD ref:** `REMAINING_GAPS.md` G10.  
- **Verify:** seed map 20 swords vs 5 spears → Petra `spearman_score` > `archer_score`; `python3 tools/check_parity.py --unit petra --scenario counter`

---

## Legacy — Pre-audit P0-P4 (2026-08-24, from PORTING_TODO all-[x] era) — kept for history

## P0 — Make it run (do this before anything else)
1. Open the project in Godot 4.3 .NET, let it restore/build, press **F5**.
   - Nothing has been compiled yet; expect a handful of small C# fixes on first build.
   - Commit the fixes immediately (`git add -A && git commit`).
2. Verify CI goes green on GitHub after pushing those fixes.
3. One local desktop export end-to-end (`docs/BUILD.md`) → proves the toolchain.

## P1 — Close the game loop (Thronefall-style MVP)
4. **A\* pathfinding** on the sim grid in `GameSim` (deterministic, lockstep-friendly). ✓ `LongPathfinder` + `HierarchicalPathfinder` ported, `GameSim.RebuildPassability` wired.
5. **Unit state machine**: `Idle / Move / Gather / Return / Attack`. ✓ `UnitAI` FSM from 0ad, stances, carry anim stubs.
6. **Resources**: trees/gold nodes as sim entities; villagers gather → deposit at center. ✓ `ResourceGatherer/ResourceSupply/Dropsite`, 4 scattered nodes.
7. **Combat**: HP, attack range/cooldown, auto-aggro, death + respawn of views. ✓ `Health/Attack/RangeManager`, projectiles omitted (melee).
8. **Enemy waves**: spawn at map edge every N sec, march to your center → win/lose screen. ✓ `GameSim Tick` waves, `EndGame`.
   → Playable MVP done (press F5; WASD/QE, LMB drag select, RMB move/gather/attack, B build).
   → **0 A.D. art bake done 2026-04:** `tools/extract_0ad_assets.py` + GLBs + `WorldView` real-model path + music (see `assets/README.md`, `docs/0AD_ATTRIBUTION.md`).

## P2 — Multiplayer + mobile
9. Netcode M2/M3: host/join UI, ENet transport, host applies remote commands, snapshot sync (`docs/MULTIPLAYER.md`).
10. Touch input scheme feeding the same command pipeline (tap select, pinch zoom).
11. Real Android device testing weekly — `export_presets.cfg` ready, CI `workflow_dispatch android:true`.

## P3 — Look & feel
12. Art pass: ✓ done via 0 A.D. bake (player blue `0.36,0.62,0.94` / enemy red `0.92,0.32,0.28` baked, throttle: buildings 5.2/2.1m, units 1.7/1.8m). Thronefall shaders remain fallback.
13. Juice: animations (skeletal .psa → glTF not yet; T-pose static), sounds (theme loop; UI/attack/gather sfx todo), minimap (`SubViewport`) todo, health bars / wave banners todo, terrain splat wiring (grass/dirt/rock PNGs present, shader sampling todo).

## P4 — Scale & ship
14. Perf milestones from `docs/PERFORMANCE.md`: staggered AI ticks, off-thread sim, compute shaders if needed.
15. Release engineering: version stamping from git tags, release keystore, Play Console listing, TestFlight, itch.io/Steam page.
