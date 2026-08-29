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
| Lockstep batch + hash-after-sim | `NetClientTurnManager.cpp:188`, `HashSerializer` | `TurnManager` 4×`FIXED_STEP` then `compute_sim_hash`; HUD `post_lockstep` 1–21 |
| Cardinal JPS skip + dirty regions | `LongPathfinder.cpp` AddJumpedHoriz, `HierarchicalPathfinder.cpp:480` | `find_path` jump on cardinals; `rebuild_dirty_regions` |
| Idle ship `moveApart` | `navalManager.js:568` | `petra_manage_naval` nudge off dock |

Verify: `python3 tools/check_parity.py --unit petra` and `--unit netcode`.

---

## Next work (in order)

### P1 — Two-client lockstep playtest (remaining)

**0 A.D.:** `NetClientTurnManager.cpp:188`, `HashSerializer.cpp`  
**RTS0:** Host/Join + `TurnManager` hash-after-sim + deferred player cmds 1–21. Headless dual-sim hashes: `tools/test_netcode.cpp` Test 4.

Still needed: two Godot clients for 5 minutes; `save_replay`/`load_replay` round-trip; first live OOS (Petra `rand()`, wall-drag spawn).

Verify: `python3 tools/lobby_server.py` then two `./build/linux/RTS0.x86_64` Host + Join.

### P2 — Long-path JPS + dirty chunks (remaining)

Cardinal jump + `rebuild_dirty_regions` are in. Remaining: per-chunk flood only (`HierarchicalPathfinder.cpp:480`), not full-grid remesh; optional `PASS_CAVALRY`.

### P3 — Petra naval remaining

`moveApart` + fisher counts are in. Remaining: per-sea `maintainFleet` / `assignShipsToPlans`; island `PLAN_RAID` dock targets.

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
