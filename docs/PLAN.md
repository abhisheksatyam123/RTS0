# RTS0 plan (0 A.D. native port)

Vendor JS/C++ under `src/0ad/` and `/home/abhi/project/0ad` is **reference only** (`SConstruct` excludes it). Runtime is C++ SoA in `src/simulation/`. Every remaining task below names the 0 A.D. file to copy semantics from.

---

## Shipped (do not reopen)

| Area | 0 A.D. source | RTS0 |
|---|---|---|
| Octile A*, string-pull, ship pass | `LongPathfinder.cpp:400`, `VertexPathfinder.cpp` | `grid_nav.h` `find_path` + `PASS_SHIP` |
| Hierarchical regions + goal snap | `HierarchicalPathfinder.cpp:50 InitRegions`, `:523 ComputeNeighbors`, `:689 MakeGoalReachable` | `rebuild_regions()`, snap unreachable goal to start region |
| Territory / market / auras | `CCmpTerritoryManager`, `Market.js`, `Auras.js` | `rebuild_territory`, `cmd_barter`, `aura_manager.h` |
| Petra queues, raid+assault, transport, chat | `queueManager.js`, `attackPlan.js`, `transportPlan.js`, `chatHelper.js` | `petra_ai.h` + `tick_petra_ai` |
| Opening + research gates + fishing | `startingStrategy.js:513`, `researchManager.js:15 checkPhase`, `navalManager.js:223 canFishSafely` | wood DP → houses → barracks; `popPhase2` / `workPhase3`; skip enemy-territory fish |
| Lobby handshake + HTTP rooms | `NetMessages.h` `NMT_*`, `NetClient` handshake | `NetHost`, `lobby_client.h`, `tools/lobby_server.py`, Lobby HUD |
| Unique models / PSA / civ voice | `art/meshes`, `art/animation` | `sim_types.h` `model_id`, `init_animation_controllers`, Voice EN/Civ |

Verify: `python3 tools/check_parity.py --unit petra` (27/27) and `--unit netcode` (16/16).

---

## Next work (in order)

### P1 — Two-client lockstep playtest (blocking for MP)

**0 A.D.:** `source/network/NetClientTurnManager.cpp:188`, `NetServerTurnManager.cpp`, `HashSerializer.cpp`  
**RTS0:** `src/network/turn_manager.cpp`, `main_node.cpp` Lobby panel

1. Run `python3 tools/lobby_server.py` then Host + Join on two clients.
2. Confirm `NMT_CLIENT_HANDSHAKE` → `NMT_SERVER_HANDSHAKE_RESPONSE` → `NMT_AUTHENTICATE`.
3. Compare `TurnManager::compute_sim_hash` each turn; fix the first OOS (float order, building vector order, Petra RNG).
4. Round-trip `save_replay` / `load_replay`.

DoD: 5-minute 1v1, hashes match, no disconnect.  
Verify: `./build/linux/RTS0.x86_64` host & join + lobby on `:8088`.

### P2 — Long-path JPS + dirty chunks

**0 A.D.:** `LongPathfinder.cpp` JPS jump, `HierarchicalPathfinder.cpp:480` dirty-chunk `InitRegions` (not full rebuild)  
**RTS0:** `grid_nav.h` still full-grid A* + full `rebuild_regions()` on every `rebuild_nav_grid`

1. Port JPS skip on straight passable runs (keep octile 10/14).
2. Rebuild only dirty chunks (`cells.dirty`) then `UpdateEdges` for neighbors.
3. Optional `PASS_CAVALRY` slope: 0 A.D. pass class, not a second A*.

DoD: same seed path vs 0 A.D. within 1 cell on a 64² map.  
Verify: `python3 tools/check_parity.py` pathfinder wiring + a small A*/JPS fixture.

### P3 — Petra naval + expansion remaining

**0 A.D.:** `navalManager.js` `maintainFleet`, `assignShipsToPlans`, `moveApart`; `baseManager.js` / `headquarters.js` second CC  
**RTS0:** docks, trireme patrol, fishing, one expansion TC

1. Wanted fish/transport counts per “sea” (our `NAV_WATER` component).
2. Idle ships `moveApart` so they do not stack on the dock (`navalManager.js:564`).
3. AttackPlan already boards water crossings; add dock/ship targets for `PLAN_RAID` on island maps.
4. Keep sandbox `canExpand = false` (`startingStrategy.js:40`).

DoD: water map trains 2+ fishers, they deposit at dock, ships do not idle on the pier.  
Verify: `--unit petra` + play Aegean/archipelago.

### P4 — Session UI split + civ actors

**0 A.D.:** `binaries/data/mods/public/gui/session/` (TopPanel, selection, production, diplomacy)  
**RTS0:** monolithic `main_node.cpp`

1. Split HUD widgets only if P1 playtest is clean (not before).
2. Per-civ GLB/actor variants (`art/actors/`) after shared `model_id`s — do not add more cavalry fallbacks.

### P5 — Ranked lockstep (optional, last)

**0 A.D.:** `maths/Fixed.h` `CFixed_15_16`  
Only if 1v1 ranked is a goal. Until then keep float sim + host-authoritative hash checks.

---

## Working rules

- Read the 0 A.D. function first; port semantics, not JS.
- New Petra/nav/net code cites `file:line` in a one-line comment.
- Do not compile `src/0ad/`.
- `docs/REMAINING_GAPS.md` body below the G1–G10 table may still describe old stubs — trust the table + this file.
