# 0 A.D. → RTS0 feature and code map

> Audit date: 2026-09-06 (links refreshed 2026-10-08). This maps 0 A.D. domains to RTS0 code; the status column is a 2026-09-06 snapshot, and current parity percentages live only in the [G1–G10 table](REMAINING_GAPS.md#g1-g10-summary-table). This is a repository-grounded map, not a claim that RTS0 is a complete 0 A.D. clone. `src/0ad/` is vendored **reference-only** code (excluded by `SConstruct`); the runtime implementation is the C++ SoA simulation under `src/simulation/` plus Godot presentation/networking modules.
>
> Status meanings: **Implemented** — concrete RTS0 code exists and is wired into the runtime path; **Partial** — code exists but the audit/docs identify missing behavior or reduced fidelity; **Reference-only** — 0 A.D. source is present but no equivalent runtime implementation; **Missing/not evidenced** — no corresponding code found in this checkout.

## Executive summary

| 0 A.D. domain | RTS0 code | Status | Important evidence / remaining work |
|---|---|---|---|
| Deterministic simulation and SoA state | `src/simulation/simulation.{h,cpp}`, `src/core/memory/soa_storage.h` | Implemented (float RNG) | 11 simulation ticks, seeded `SimRng`; optional fixed-point ranked lockstep is not implemented. |
| Unit, building, resource, civilization data model | `src/simulation/sim_types.h` | Implemented subset | 23 units, 14 buildings, 13 civs, resources, stances, formations, technologies, victory condition enums. |
| Commands and unit orders | `simulation.h` (`cmd_*`, `order_*`) | Implemented | Move, stop, train, gather, attack, attack-move, patrol, repair/build, stance, formation, garrison, transport, trade, herd, livestock, research, barter. |
| Production and construction | `simulation.cpp` `tick_production`, `tick_construction`; `main_node.cpp` | Implemented/partial | Queues, rally points, foundations/progress exist; full 0 A.D. construction/builder component parity is not demonstrated. |
| Gathering, resources, trickle economy | `simulation.cpp` `tick_gather`, `tick_trickle`; `sim_types.h` | Implemented subset | Resource nodes and gathering exist; passive gold is documented as a placeholder in `README.md`. |
| Markets, barter, traders | `simulation.cpp` market/trader methods; `aura_manager.h` | Partial | Barter/trade commands exist; trader gain now follows `Market.js`/`Trade.js` normalized Euclidean-distance semantics. Exact 0 A.D. barter fee, route lifecycle, and market UI remain partial (`docs/PLAN.md`). |
| Combat, damage, projectiles, status effects | `simulation.cpp` `tick_combat`, `tick_projectiles`, `tick_status_effects`; `sim_types.h` | Implemented subset | Attack/ranged/siege/ship classification and effects are present; complete 0 A.D. component/weapon/armor matrix is not established. |
| Formation and unit motion | `sim_types.h`, `simulation.cpp` `move_units`; `grid_nav.h` | Implemented/partial | Formation enum and path fields are wired; steering/formation fidelity is marked TODO by parity tooling. |
| Pathfinding | `grid_nav.h`, `nav/long_pathfinder.h`; `src/0ad/simulation2/helpers/{LongPathfinder,Hie...Pathfinder,VertexPathfinder,Pathfinding,Grid}.*` | Implemented (core) | JPS long-path search, string-pull, ship passability and MaxTerrainSlope for every land class, hierarchical regions, goal snap and dirty chunks. Short-range `VertexPathfinder` remains (see `docs/MIGRATION.md`). |
| Territory, influence, auras | `simulation.h/.cpp` `territory_grid_`, `rebuild_territory`; `aura_manager.h`; `grid_nav.h` | Implemented/partial | Territory ownership, blur, and aura infrastructure are wired. Structures now store per-team capture points, transfer ownership through enemy capture, regenerate owner points, and hash deterministically. Exact 0 A.D. rates/templates and full neighbor-aware redistribution remain partial. |
| Fog of war / line of sight | `simulation.cpp` `tick_fow`; `minimap_view.*`; terrain shader | Implemented subset | FoW tick, explored/visible shroud, reciprocal mutual-alliance sharing, and hash-stable diplomacy state are implemented and tested against `VisionSharing.js`/`Diplomacy.js`; terrain LOS occlusion and full `Visibility.js` semantics remain partial. |
| AI / Petra | `petra_ai.h`; `simulation.cpp` `tick_petra_ai`, threat map | Implemented subset | Plans, queues, raids, assaults, transport, opening/research gates, fishing, idle-ship separation, deterministic warship assignment, island-target dock priority, and water-crossing raid transport dispatch exist. Per-sea fleet accounting, full boarding, and complete dock-raid planning remain partial. |
| Random map generation | `rms_generator.{h,cpp}`; `simulation.h` `generate_random_map`; `tools/port_0ad_maps.py` | Implemented subset | Procedural map generation and imported map tooling exist; not all 0 A.D. map/script semantics are claimed. |
| Fauna/livestock | `sim_types.h`; `simulation.cpp` `tick_fauna`, herd/breed orders | Implemented subset | Fauna definitions and behavior scaffolding exist; full wildlife ecosystem parity is not evidenced. |
| Victory/end game | `sim_types.h` `VictoryCondition`; `simulation.cpp` relic/timer and `tick_waves` | Implemented subset | Wonder, relic, conquest, endless/wave, and defeat paths exist; wonder and relic timers have regression coverage. Conquest after structure-capture state redistribution and full `EndGameManager.js`/`VictoryManager.js` behavior remain partial. |
| Replay / desync detection | `src/network/turn_manager.*`; `tools/test_netcode.cpp` | Implemented internal recorder / partial 0 A.D. parity | Replay command/hash save-load, bounded parsing, replay status/OOS state, game-start map/seed/full setup sync, and hash-after-sim are implemented. Remaining: live two-client OOS validation and a portable state snapshot/replay format equivalent to 0 A.D. |
| Network protocol/transport | `src/network/net_host.*`, `net_types.h`, `src/0ad/network/*` | Implemented subset | ENet-style host, protocol framing, client/server turn managers and 0 A.D. message reference exist; full protocol surface is not claimed. |
| Lobby and sessions | `src/network/lobby_client.h`, `tools/lobby_server.py`, lobby HUD in `main_node.cpp` | Implemented/partial | Host/join handshake and HTTP room flow exist. Petra diplomacy requests now have deterministic pending state and cadence; production lobby/session UX, authentication hardening, and full request UI remain roadmap work. |
| Camera and selection | `camera_rig.*`, `selection_rect.*`, `main_node.*` | Implemented | Pan, rotate, zoom, terrain raycast, click/drag selection, move/build orders. |
| World rendering | `world_view_node.*`, `src/graphics/assets/*`, shaders, scenes | Implemented subset | MultiMesh unit/building/resource/health rendering and terrain/water/foliage shaders exist. |
| HUD/menus/UI | `main_node.*`, `scenes/main.tscn`, `popup_safe.h` | Partial | Menus, HUD, build mode, alerts, debug overlay exist; 0 A.D. session UI is explicitly still monolithic (`docs/PLAN.md`). |
| Minimap | `minimap_view.*` | Implemented subset | Interactive minimap, FoW shroud and frustum overlay. |
| Models and assets | `src/graphics/assets/*`, `assets/`, `tools/extract_0ad_assets.py`, `assets/models/manifest.json` | Partial | Unique models for every canonical unit, building and fauna; raw 0 A.D. model coverage is a long tail. Counts and gaps: `docs/ASSET_MANIFEST_TODO.md`. |
| Animation | `animation_controller.*`, `psa_loader.*`, `skeleton.*` | Implemented subset | PSA loading, skeleton transforms and controllers exist; civ-unique actors remain partial. |
| Audio/voice | `audio/`, asset tooling, `main_node.cpp` | Partial | Audio files and playback/voice scaffolding exist; complete 0 A.D. sound event coverage is not evidenced. |
| Core performance/diagnostics | `src/core/job_system.*`, `memory/*`, `diagnostics/*`, `debug_overlay.*` | Implemented infrastructure | Worker pool, pools/arenas/SoA, logger, perf tracker, debug overlay. |
| Cross-platform build/export | `SConstruct`, `Makefile`, `tools/build.*`, `docs/BUILD.md`, `rts0.gdextension` | Implemented target/partial validation | Windows/Linux builds and Android/iOS targets are documented; platform-specific runtime validation is not complete. |

## Runtime tick/component map

`tools/check_parity.py` identifies 84 public 0 A.D. simulation component JS files and 11 RTS0 tick functions:

- Combat: `tick_combat`, `tick_projectiles`, `tick_status_effects` → attack, damage, effects and projectiles.
- Economy: `tick_gather`, `tick_trickle` → resource gathering and passive income.
- Production: `tick_production`, `tick_construction` → queues, training, construction.
- World/visibility: `tick_fow` → visibility/shroud.
- AI/nature/end-game: `tick_petra_ai`, `tick_fauna`, `tick_waves` → AI, fauna and scripted/end-game behavior.

This is a component-to-tick mapping, not proof that each of the 84 0 A.D. components has an exact behavioral equivalent.

## Remaining work

The ordered todo list is [docs/PLAN.md](PLAN.md); this map doesn't keep its own.

## Evidence and reproducibility

- Todo list: `docs/PLAN.md`. Parity per subsystem: the G1–G10 table in `docs/REMAINING_GAPS.md`. Vendored-code porting: `docs/MIGRATION.md`.
- Replay implementation: `src/network/turn_manager.h/.cpp`; regression coverage: `tools/test_netcode.cpp`.
- Architecture and networking rules: `docs/ARCHITECTURE.md`, `docs/MULTIPLAYER.md`.
- Asset extraction/mapping: `docs/ASSET_MAPPING.md`, `docs/ASSET_MANIFEST_TODO.md`.
- Automated inventory/parity report: `python3 tools/check_parity.py --json`.
- 0 A.D. reference sources are under `src/0ad/`; they are not compiled into RTS0.
